"""Mejor solver MWIS medido — nb-ajustado (AdjustedSolver refactorizado + tunelamiento adaptativo).

Mantiene exactitud entera, desempate determinista (peso -> cardinalidad ->
orden lexicográfico) y verificación exacta, incorporando:
- Corte por estancamiento configurable (`DEFAULT_EARLY_STOP_RESTARTS = 6`).
- Tunelamiento `× 2` selectivo guiado por dispersión de grados (`nnz_cv`) y tamaño `n`.
- Guarda opcional de presupuesto de tiempo (`max_time_s`).
"""

from __future__ import annotations

import argparse
import json
import math
import random
import statistics
import time
from dataclasses import dataclass
from typing import List, Optional, Sequence, Set, Tuple

DEFAULT_RESTARTS = 8
DEFAULT_PERTURBATIONS = 10
DEFAULT_DROP_FRACTION = 0.25
DEFAULT_COOLING = 0.85
DEFAULT_MAX_LOCAL_PASSES = 40
DEFAULT_ENABLE_ONE_TO_TWO = True
DEFAULT_GREEDY_ALPHA = 0.50
DEFAULT_GREEDY_BETA = 0.50
DEFAULT_MAX_12_CANDIDATES = 80
DEFAULT_INITIAL_ACCEPTANCE_P = 0.50
DEFAULT_TEMP_SCALE = 0.25
DEFAULT_EARLY_STOP_RESTARTS = 6
DEFAULT_TUNNEL_MULTIPLIER = 2


@dataclass
class Graph:
    n: int
    adj: List[Set[int]]
    weights: List[int]
    family: str = "cli"
    instance_name: str = ""
    seed: int = 0

    @property
    def m_edges(self) -> int:
        return sum(len(x) for x in self.adj) // 2

    def is_independent(self, vertices: Sequence[int]) -> bool:
        chosen = set(vertices)
        if len(chosen) != len(vertices):
            return False
        for u in chosen:
            if u < 0 or u >= self.n or (self.adj[u] & chosen):
                return False
        return True


def objective(vertices: Sequence[int] | Set[int], weights: Sequence[int]) -> int:
    return sum(weights[v] for v in vertices)


def better_solution(
    weight_a: int,
    vertices_a: Sequence[int] | Set[int],
    weight_b: int,
    vertices_b: Sequence[int] | Set[int],
) -> bool:
    if weight_a != weight_b:
        return weight_a > weight_b
    na, nb = len(vertices_a), len(vertices_b)
    if na != nb:
        return na > nb
    return tuple(sorted(vertices_a)) < tuple(sorted(vertices_b))


def degree_cv(adj: Sequence[Set[int]]) -> float:
    """Coeficiente de variación de grados (inspirado en row_nnz_irregularity)."""
    n = len(adj)
    if n < 2:
        return 0.0
    degs = [len(neighbors) for neighbors in adj]
    mean_d = sum(degs) / n
    if mean_d <= 0.0:
        return 0.0
    var_d = sum((d - mean_d) ** 2 for d in degs) / n
    return math.sqrt(var_d) / mean_d


class AdjustedSolver:
    def __init__(
        self,
        seed: int = 42,
        n_restarts: int = DEFAULT_RESTARTS,
        n_perturbations: int = DEFAULT_PERTURBATIONS,
        drop_fraction: float = DEFAULT_DROP_FRACTION,
        cooling: float = DEFAULT_COOLING,
        max_local_passes: int = DEFAULT_MAX_LOCAL_PASSES,
        enable_one_to_two: bool = DEFAULT_ENABLE_ONE_TO_TWO,
        greedy_alpha: float = DEFAULT_GREEDY_ALPHA,
        greedy_beta: float = DEFAULT_GREEDY_BETA,
        max_12_candidates: Optional[int] = DEFAULT_MAX_12_CANDIDATES,
        initial_acceptance_p: float = DEFAULT_INITIAL_ACCEPTANCE_P,
        temp_scale: float = DEFAULT_TEMP_SCALE,
        early_stop_restarts: Optional[int] = DEFAULT_EARLY_STOP_RESTARTS,
        tunnel_multiplier: int = DEFAULT_TUNNEL_MULTIPLIER,
        max_time_s: Optional[float] = None,
    ):
        self.seed = seed
        self.n_restarts = max(1, int(n_restarts))
        self.n_perturbations = max(0, int(n_perturbations))
        self.drop_fraction = float(drop_fraction)
        self.cooling = float(cooling)
        self.max_local_passes = max(1, int(max_local_passes))
        self.enable_one_to_two = bool(enable_one_to_two)
        self.greedy_alpha = float(greedy_alpha)
        self.greedy_beta = float(greedy_beta)
        self.max_12_candidates = max_12_candidates
        self.initial_acceptance_p = float(initial_acceptance_p)
        self.temp_scale = float(temp_scale)
        self.early_stop_restarts = early_stop_restarts
        self.tunnel_multiplier = max(1, int(tunnel_multiplier))
        self.max_time_s = max_time_s
        self.last_stats: dict = {}

    def _should_tunnel_extra(self, n: int, cv: float) -> bool:
        """Activa el segundo tunelamiento solo en regímenes donde aporta mayor ganancia."""
        if self.tunnel_multiplier <= 1:
            return False
        # Evita tunelamiento extra costoso en grafos con fuerte asimetría de grados tipo BA grande.
        if cv >= 1.0 and n >= 200:
            return False
        # Prioriza grafos medianos/grandes con dispersión moderada (típicamente ER / frustrados).
        return n >= 100 or cv >= 0.35

    def solve(self, graph: Graph) -> Tuple[List[int], int]:
        t_start = time.perf_counter()
        adj = graph.adj
        weights = list(graph.weights)
        n = len(adj)
        cv = degree_cv(adj)
        use_extra_tunnel = self._should_tunnel_extra(n, cv)

        rng = random.Random(self.seed)
        best_set: Set[int] = set()
        best_w = 0

        positive = [w for w in weights if w > 0]
        mean_w = max(1.0, statistics.fmean(positive)) if positive else 1.0
        delta_typical = (statistics.median(positive) if positive else 1.0) * self.temp_scale
        temp0 = -float(delta_typical) / math.log(self.initial_acceptance_p)

        restarts_without_gain = 0
        restarts_run = 0
        timed_out = False
        stopped_early = False

        for restart in range(self.n_restarts):
            if self.max_time_s is not None and (time.perf_counter() - t_start) >= self.max_time_s:
                timed_out = True
                break

            restarts_run = restart + 1
            jitter = 0.0 if restart == 0 else min(0.35, 0.05 * restart)
            sol = self._greedy_fill(adj, weights, set(), rng, mean_w=mean_w, jitter=jitter)
            sol, cur_w = self._local_improve_fast(adj, weights, sol)
            cur = set(sol)

            improved_this_restart = False
            if better_solution(cur_w, cur, best_w, best_set):
                best_set, best_w = set(cur), cur_w
                improved_this_restart = True

            temp = temp0
            for _ in range(self.n_perturbations):
                if self.max_time_s is not None and (time.perf_counter() - t_start) >= self.max_time_s:
                    timed_out = True
                    break

                cand, cand_w = self._perturb_repair_fast(adj, weights, cur, rng, mean_w)
                if better_solution(cand_w, cand, best_w, best_set):
                    best_set, best_w = set(cand), cand_w
                    improved_this_restart = True

                delta = cand_w - cur_w
                if delta >= 0 or (temp > 1e-12 and rng.random() < math.exp(delta / temp)):
                    cur, cur_w = cand, cand_w
                temp *= self.cooling

            if timed_out:
                break

            refined, refined_w = self._local_improve_full(adj, weights, cur)
            if better_solution(refined_w, refined, best_w, best_set):
                best_set, best_w = set(refined), refined_w
                improved_this_restart = True

            # Segundo tunelamiento (× 2) condicional cuando el reinicio no mejoró la incumbente.
            if (
                not improved_this_restart
                and use_extra_tunnel
                and (self.max_time_s is None or (time.perf_counter() - t_start) < self.max_time_s)
            ):
                tun_cand, tun_w = self._perturb_repair_fast(adj, weights, best_set if best_set else cur, rng, mean_w)
                tun_cand, tun_w = self._local_improve_full(adj, weights, tun_cand)
                if better_solution(tun_w, tun_cand, best_w, best_set):
                    best_set, best_w = set(tun_cand), tun_w
                    improved_this_restart = True
                if better_solution(tun_w, tun_cand, cur_w, cur):
                    cur, cur_w = set(tun_cand), tun_w

            if improved_this_restart:
                restarts_without_gain = 0
            elif self.early_stop_restarts is not None and self.early_stop_restarts > 0:
                restarts_without_gain += 1
                if restarts_without_gain >= self.early_stop_restarts:
                    stopped_early = True
                    break

        best_refined, refined_w = self._local_improve_full(adj, weights, best_set)
        if better_solution(refined_w, best_refined, best_w, best_set):
            best_set, best_w = set(best_refined), refined_w

        ordered_best = sorted(best_set)
        if not graph.is_independent(ordered_best):
            raise RuntimeError("AdjustedSolver devolvió una solución no independiente")

        self.last_stats = {
            "degree_cv": round(cv, 6),
            "extra_tunnel_enabled": use_extra_tunnel,
            "restarts_run": restarts_run,
            "stopped_early": stopped_early,
            "timed_out": timed_out,
        }
        return ordered_best, best_w

    def _greedy_fill(
        self,
        adj: Sequence[Set[int]],
        weights: Sequence[int],
        initial_selected: Set[int],
        rng: random.Random,
        mean_w: float,
        jitter: float = 0.0,
    ) -> Set[int]:
        n = len(adj)
        selected = set(initial_selected)
        blocked = set(selected)
        for u in selected:
            blocked.update(adj[u])

        candidates = {u for u in range(n) if u not in blocked and weights[u] >= 0}
        residual_degree = [sum(1 for v in adj[u] if v in candidates) for u in range(n)]
        residual_neighbor_weight = [
            sum(weights[v] for v in adj[u] if v in candidates) for u in range(n)
        ]

        alpha = self.greedy_alpha
        beta = self.greedy_beta

        while candidates:
            best_u: Optional[int] = None
            best_score = -float("inf")
            for u in candidates:
                denom = (
                    1.0
                    + alpha * residual_degree[u]
                    + beta * residual_neighbor_weight[u] / mean_w
                )
                score = float(weights[u]) / denom
                if jitter > 0.0:
                    score *= 1.0 + rng.uniform(-jitter, jitter)
                if score > best_score or (score == best_score and rng.random() < 0.5):
                    best_u, best_score = u, score

            if best_u is None:
                raise RuntimeError("Greedy ajustado sin candidato")

            selected.add(best_u)
            removed = {best_u} | (adj[best_u] & candidates)
            candidates.difference_update(removed)

            for x in removed:
                wx = weights[x]
                for y in adj[x]:
                    if y in candidates:
                        residual_degree[y] -= 1
                        residual_neighbor_weight[y] -= wx

        return selected

    @staticmethod
    def _build_conflict_counts(adj: Sequence[Set[int]], cur: Set[int]) -> List[int]:
        counts = [0] * len(adj)
        for u in cur:
            for v in adj[u]:
                counts[v] += 1
        return counts

    @staticmethod
    def _insert_vertex(
        v: int, cur: Set[int], cur_w: int, adj: Sequence[Set[int]], weights: Sequence[int], counts: List[int]
    ) -> int:
        cur.add(v)
        for u in adj[v]:
            counts[u] += 1
        return cur_w + weights[v]

    @staticmethod
    def _remove_vertex(
        v: int, cur: Set[int], cur_w: int, adj: Sequence[Set[int]], weights: Sequence[int], counts: List[int]
    ) -> int:
        cur.remove(v)
        for u in adj[v]:
            counts[u] -= 1
        return cur_w - weights[v]

    def _add_and_one_to_one(
        self,
        adj: Sequence[Set[int]],
        weights: Sequence[int],
        cur: Set[int],
        cur_w: int,
        counts: List[int],
    ) -> Tuple[Set[int], int]:
        n = len(adj)
        while True:
            improved = False

            for v in range(n):
                if v not in cur and counts[v] == 0 and weights[v] >= 0:
                    cur_w = self._insert_vertex(v, cur, cur_w, adj, weights, counts)
                    improved = True
                    break
            if improved:
                continue

            for v in sorted(cur):
                wv = weights[v]
                for u in adj[v]:
                    if u in cur or counts[u] != 1:
                        continue
                    wu = weights[u]
                    if wu > wv or (wu == wv and u < v):
                        cur_w = self._remove_vertex(v, cur, cur_w, adj, weights, counts)
                        cur_w = self._insert_vertex(u, cur, cur_w, adj, weights, counts)
                        improved = True
                        break
                if improved:
                    break

            if not improved:
                return cur, cur_w

    def _local_improve_fast(
        self, adj: Sequence[Set[int]], weights: Sequence[int], selected: Set[int]
    ) -> Tuple[Set[int], int]:
        cur = set(selected)
        cur_w = objective(cur, weights)
        counts = self._build_conflict_counts(adj, cur)
        return self._add_and_one_to_one(adj, weights, cur, cur_w, counts)

    def _local_improve_full(
        self, adj: Sequence[Set[int]], weights: Sequence[int], selected: Set[int]
    ) -> Tuple[Set[int], int]:
        n = len(adj)
        cur = set(selected)
        cur_w = objective(cur, weights)
        counts = self._build_conflict_counts(adj, cur)

        for _ in range(self.max_local_passes):
            cur, cur_w = self._add_and_one_to_one(adj, weights, cur, cur_w, counts)
            improved = False

            if len(cur) >= 2:
                candidates_21 = [
                    u for u in range(n) if u not in cur and counts[u] == 2 and weights[u] > 0
                ]
                candidates_21.sort(key=lambda u: weights[u], reverse=True)
                for u in candidates_21:
                    conflict_pair = [v for v in adj[u] if v in cur]
                    if len(conflict_pair) != 2:
                        continue
                    a, b = conflict_pair[0], conflict_pair[1]
                    if weights[u] > weights[a] + weights[b]:
                        cur_w = self._remove_vertex(a, cur, cur_w, adj, weights, counts)
                        cur_w = self._remove_vertex(b, cur, cur_w, adj, weights, counts)
                        cur_w = self._insert_vertex(u, cur, cur_w, adj, weights, counts)
                        improved = True
                        break
            if improved:
                continue

            if self.enable_one_to_two and cur:
                for v in sorted(cur):
                    wv = weights[v]
                    compat = [
                        u for u in adj[v] if u not in cur and counts[u] == 1 and weights[u] >= 0
                    ]
                    if len(compat) < 2:
                        continue
                    compat.sort(key=lambda u: weights[u], reverse=True)
                    max_partner = weights[compat[0]]
                    compat = [u for u in compat if weights[u] + max_partner >= wv]
                    if self.max_12_candidates is not None:
                        compat = compat[: self.max_12_candidates]

                    L = len(compat)
                    for i in range(L - 1):
                        u = compat[i]
                        if weights[u] + weights[compat[i + 1]] < wv:
                            break
                        adj_u = adj[u]
                        for j in range(i + 1, L):
                            x = compat[j]
                            pair_w = weights[u] + weights[x]
                            if pair_w < wv:
                                break
                            if x not in adj_u:
                                cur_w = self._remove_vertex(v, cur, cur_w, adj, weights, counts)
                                cur_w = self._insert_vertex(u, cur, cur_w, adj, weights, counts)
                                cur_w = self._insert_vertex(x, cur, cur_w, adj, weights, counts)
                                improved = True
                                break
                        if improved:
                            break
                    if improved:
                        break

            if not improved:
                break

        return cur, cur_w

    def _perturb_repair_fast(
        self,
        adj: Sequence[Set[int]],
        weights: Sequence[int],
        selected: Set[int],
        rng: random.Random,
        mean_w: float,
    ) -> Tuple[Set[int], int]:
        if not selected:
            filled = self._greedy_fill(adj, weights, set(), rng, mean_w=mean_w, jitter=0.15)
            return self._local_improve_fast(adj, weights, filled)

        k_drop = max(1, int(round(len(selected) * self.drop_fraction)))
        k_drop = min(k_drop, len(selected))
        to_drop = set(rng.sample(sorted(selected), k_drop))
        partial = set(selected) - to_drop
        repaired = self._greedy_fill(adj, weights, partial, rng, mean_w=mean_w, jitter=0.10)
        return self._local_improve_fast(adj, weights, repaired)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Mejor solver MWIS (nb-ajustado refactorizado).")
    ap.add_argument("instancia", help="JSON con n, edges, weights")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--restarts", type=int, default=DEFAULT_RESTARTS)
    ap.add_argument("--perturbations", type=int, default=DEFAULT_PERTURBATIONS)
    ap.add_argument("--early-stop", type=int, default=DEFAULT_EARLY_STOP_RESTARTS)
    ap.add_argument("--tunnel-mult", type=int, default=DEFAULT_TUNNEL_MULTIPLIER)
    ap.add_argument("--max-time-s", type=float, default=None)
    args = ap.parse_args()

    with open(args.instancia, "r", encoding="utf-8") as fh:
        data = json.load(fh)

    n = int(data["n"])
    adj = [set() for _ in range(n)]
    for u, v in data["edges"]:
        if u != v:
            adj[u].add(v)
            adj[v].add(u)

    raw_weights = data["weights"]
    is_exact_int = all(isinstance(w, int) or float(w).is_integer() for w in raw_weights)
    scale = 1 if is_exact_int else 1_000_000
    wu = [int(w) if is_exact_int else int(round(w * scale)) for w in raw_weights]

    g = Graph(n=n, adj=adj, weights=wu, family="cli", instance_name=args.instancia, seed=0)
    solver = AdjustedSolver(
        seed=args.seed,
        n_restarts=args.restarts,
        n_perturbations=args.perturbations,
        early_stop_restarts=None if args.early_stop < 0 else args.early_stop,
        tunnel_multiplier=args.tunnel_mult,
        max_time_s=args.max_time_s,
    )

    t0 = time.perf_counter()
    verts, score = solver.solve(g)
    dt = time.perf_counter() - t0

    s = set(verts)
    ok = g.is_independent(verts) and all(not (u in s and v in s) for u, v in data["edges"])
    peso_exact = sum(wu[v] for v in verts)
    assert peso_exact == score, "peso verificado != peso del solver"

    print(
        json.dumps(
            {
                "instancia": args.instancia,
                "n": n,
                "peso": score / scale,
                "peso_exacto_escalado": score,
                "escala": scale,
                "vertices": verts,
                "tiempo_s": round(dt, 4),
                "independiente_y_verificado": bool(ok),
                "seed": args.seed,
                "restarts": args.restarts,
                "perturbations": args.perturbations,
                "early_stop": args.early_stop,
                "tunnel_mult": args.tunnel_mult,
                "stats": solver.last_stats,
            },
            indent=2,
        )
    )
