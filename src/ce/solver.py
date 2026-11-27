"""Heurística MWIS reforzada (multi-start + local search + perturbación)."""

from __future__ import annotations

import math
import random
from dataclasses import dataclass
from itertools import combinations
from typing import List, Optional, Sequence, Set, Tuple

from .graph import Graph
from .evaluator import WeightEvaluator, FloatEvaluator


@dataclass
class Solution:
    vertices: List[int]
    weight: object
    method: str = "multi-start+perturb"

    def __repr__(self) -> str:
        return f"Solution(n={len(self.vertices)}, weight={self.weight}, method={self.method})"


@dataclass
class SolverConfig:
    n_restarts: int = 8
    n_perturbations: int = 10
    drop_fraction: float = 0.25
    initial_temp: float = 0.05
    cooling: float = 0.85
    enable_one_to_two: bool = True
    max_local_passes: int = 40
    seed: int = 42


def is_better_solution(
    eval_,
    weight_a,
    vertices_a: Sequence[int] | Set[int],
    weight_b,
    vertices_b: Sequence[int] | Set[int],
) -> bool:
    """Regla de desempate explícita (lista para puerto a C++ / RR-PF).

    Orden de decisión:
    1. Mayor peso (criterio principal).
    2. Si pesos iguales: mayor cardinalidad (|I|).
    3. Si siguen iguales: conjunto lexicográficamente menor
       (garantiza determinismo total y reproducibilidad).

    Esta regla es independiente del tipo numérico del evaluador y
    evita dependencia de orden de exploración o de ruido de punto flotante.
    """
    cmp = eval_.compare(weight_a, weight_b)
    if cmp > 0:
        return True
    if cmp < 0:
        return False
    # pesos iguales → cardinalidad
    na, nb = len(vertices_a), len(vertices_b)
    if na > nb:
        return True
    if na < nb:
        return False
    # misma cardinalidad → orden lexicográfico del conjunto ordenado
    sa = tuple(sorted(vertices_a))
    sb = tuple(sorted(vertices_b))
    return sa < sb


class MWISSolver:
    """Solver heurístico de Maximum Weight Independent Set.

    Componentes:
    - Construcción greedy residual-ponderada (peso / (grado residual + 1))
      con jitter opcional para diversificación.
    - Mejora local: adición, 1-1, 2-1 y (opcional) 1-2.
    - Multi-start + perturbación con aceptación tipo simulated annealing acotada.
    - Evaluador intercambiable (float / exacto).
    """

    def __init__(
        self,
        evaluator: Optional[WeightEvaluator] = None,
        config: Optional[SolverConfig] = None,
        **kwargs,
    ):
        self.eval = evaluator or FloatEvaluator()
        if config is None:
            config = SolverConfig(
                n_restarts=kwargs.get("n_restarts", 8),
                n_perturbations=kwargs.get("n_perturbations", 10),
                seed=kwargs.get("seed", 42),
            )
        self.cfg = config

    def solve(self, graph: Graph) -> Solution:
        adj = [set(neigh) for neigh in graph.adj]
        w_vals = [self.eval.from_weight(x) for x in graph.weights]
        rng = random.Random(self.cfg.seed)

        best_set: Set[int] = set()
        best_w = self.eval.zero()

        for restart in range(self.cfg.n_restarts):
            jitter = 0.0 if restart == 0 else min(0.35, 0.05 * restart)
            sol = self._greedy_construct(adj, w_vals, rng, jitter=jitter)
            sol = self._local_improve(adj, w_vals, sol)
            w = self._eval_weight(sol, w_vals)

            if is_better_solution(self.eval, w, sol, best_w, best_set):
                best_set = set(sol)
                best_w = w

            # Perturbaciones con aceptación controlada
            cur = set(sol)
            cur_w = w
            temp = self.cfg.initial_temp
            for _ in range(self.cfg.n_perturbations):
                cand = self._perturb_and_repair(adj, w_vals, cur, rng)
                cand_w = self._eval_weight(cand, w_vals)

                if is_better_solution(self.eval, cand_w, cand, best_w, best_set):
                    best_set = set(cand)
                    best_w = cand_w

                delta_w = self.eval.add(cand_w, self.eval.mul_int(cur_w, -1))
                delta = self.eval.to_float(delta_w)
                if delta >= 0.0 or (
                    temp > 1e-9 and rng.random() < math.exp(delta / max(temp, 1e-12))
                ):
                    cur = cand
                    cur_w = cand_w
                temp *= self.cfg.cooling

        if not graph.is_independent(list(best_set)):
            raise RuntimeError("Internal solver error: returned set is not independent")
        return Solution(vertices=sorted(best_set), weight=best_w)

    def _eval_weight(self, selected: Set[int], w_vals: Sequence) -> object:
        acc = self.eval.zero()
        for u in selected:
            acc = self.eval.add(acc, w_vals[u])
        return acc

    def _residual_degree(
        self, u: int, adj: Sequence[Set[int]], selected: Set[int], blocked: Set[int]
    ) -> int:
        return sum(1 for v in adj[u] if v not in selected and v not in blocked)

    def _greedy_construct(
        self,
        adj: Sequence[Set[int]],
        w_vals: Sequence,
        rng: random.Random,
        jitter: float = 0.0,
    ) -> Set[int]:
        n = len(adj)
        selected: Set[int] = set()
        # Un vértice de peso negativo nunca pertenece a un MWIS óptimo:
        # quitarlo conserva independencia y aumenta el peso. Los pesos cero
        # sí permanecen porque el desempate favorece mayor cardinalidad.
        zero = self.eval.zero()
        blocked: Set[int] = {
            u for u in range(n)
            if self.eval.compare(w_vals[u], zero) < 0
        }
        remaining = set(range(n)) - blocked

        while remaining:
            best_u: Optional[int] = None
            best_w = None
            best_denom = 1

            for u in remaining:
                denom = self._residual_degree(u, adj, selected, blocked) + 1
                if jitter > 0.0:
                    factor = 1.0 + rng.uniform(-jitter, jitter)
                    w_u = self.eval.from_weight(
                        max(0.0, self.eval.to_float(w_vals[u]) * factor)
                    )
                else:
                    w_u = w_vals[u]

                if best_u is None:
                    best_u = u
                    best_w = w_u
                    best_denom = denom
                else:
                    cmp = self.eval.compare_ratios(w_u, denom, best_w, best_denom)
                    if cmp > 0 or (cmp == 0 and rng.random() < 0.5):
                        best_u = u
                        best_w = w_u
                        best_denom = denom

            if best_u is None:
                raise RuntimeError("Greedy construction invariant failed: no selectable vertex")
            selected.add(best_u)
            blocked.add(best_u)
            blocked.update(adj[best_u])
            remaining = set(range(n)) - blocked

        return selected

    def _local_improve(
        self, adj: Sequence[Set[int]], w_vals: Sequence, selected: Set[int]
    ) -> Set[int]:
        n = len(adj)
        cur = set(selected)
        cur_w = self._eval_weight(cur, w_vals)

        for _ in range(self.cfg.max_local_passes):
            improved = False

            # 1. Adición de vértices factibles
            blocked = set(cur)
            for u in cur:
                blocked.update(adj[u])
            for v in range(n):
                if (v not in blocked and
                        self.eval.compare(w_vals[v], self.eval.zero()) >= 0 and
                        not (adj[v] & cur)):
                    cur.add(v)
                    cur_w = self.eval.add(cur_w, w_vals[v])
                    improved = True
                    break
            if improved:
                continue

            # 2. Intercambio 1-1
            rejected = [
                x for x in range(n)
                if x not in cur and self.eval.compare(w_vals[x], self.eval.zero()) >= 0
            ]
            for v in list(cur):
                others = cur - {v}
                base = self._eval_weight(others, w_vals)
                for u in rejected:
                    if not (adj[u] & others):
                        cand_w = self.eval.add(base, w_vals[u])
                        candidate = others | {u}
                        if is_better_solution(self.eval, cand_w, candidate, cur_w, cur):
                            cur = candidate
                            cur_w = cand_w
                            improved = True
                            break
                if improved:
                    break
            if improved:
                continue

            # 3. Intercambio 2-1
            if len(cur) >= 2:
                for a, b in combinations(list(cur), 2):
                    others = cur - {a, b}
                    base = self._eval_weight(others, w_vals)
                    for u in rejected:
                        if not (adj[u] & others):
                            cand_w = self.eval.add(base, w_vals[u])
                            candidate = others | {u}
                            if is_better_solution(self.eval, cand_w, candidate, cur_w, cur):
                                cur = candidate
                                cur_w = cand_w
                                improved = True
                                break
                    if improved:
                        break
                if improved:
                    continue

            # 4. Intercambio 1-2 (opcional)
            if self.cfg.enable_one_to_two and len(cur) >= 1:
                for v in list(cur):
                    others = cur - {v}
                    base = self._eval_weight(others, w_vals)
                    compat = [u for u in rejected if not (adj[u] & others)]
                    for u, x in combinations(compat, 2):
                        if x not in adj[u]:
                            pair_w = self.eval.add(w_vals[u], w_vals[x])
                            cand_w = self.eval.add(base, pair_w)
                            candidate = others | {u, x}
                            if is_better_solution(self.eval, cand_w, candidate, cur_w, cur):
                                cur = candidate
                                cur_w = cand_w
                                improved = True
                                break
                    if improved:
                        break

            if not improved:
                break

        return cur

    def _perturb_and_repair(
        self,
        adj: Sequence[Set[int]],
        w_vals: Sequence,
        selected: Set[int],
        rng: random.Random,
    ) -> Set[int]:
        if not selected:
            return self._greedy_construct(adj, w_vals, rng, jitter=0.15)

        k_drop = max(1, int(round(len(selected) * self.cfg.drop_fraction)))
        k_drop = min(k_drop, len(selected))
        to_drop = set(rng.sample(list(selected), k_drop))
        cur = set(selected) - to_drop

        blocked = set(cur)
        for u in cur:
            blocked.update(adj[u])
        remaining = [
            v for v in range(len(adj))
            if v not in blocked and self.eval.compare(w_vals[v], self.eval.zero()) >= 0
        ]
        rng.shuffle(remaining)

        for v in remaining:
            if not (adj[v] & cur):
                cur.add(v)

        return self._local_improve(adj, w_vals, cur)
