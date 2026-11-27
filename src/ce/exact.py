"""MWIS exacto por branch-and-bound con cota de cobertura de cliques.

Diseñado como oráculo para instancias pequeñas/medianas, tests y evaluación
heurística; no se promete escalabilidad a grafos grandes.
"""
from __future__ import annotations
from dataclasses import dataclass
from fractions import Fraction
from typing import Iterable
from .graph import Graph
from .evaluator import ExactEvaluator
from .bounds import greedy_clique_cover_upper_bound_exact

@dataclass
class ExactResult:
    vertices: list[int]
    weight: Fraction
    upper_bound: Fraction
    nodes: int
    pruned: int
    optimal: bool = True


def solve_exact(graph: Graph, candidates: Iterable[int] | None = None) -> ExactResult:
    ev = ExactEvaluator()
    allowed = list(range(graph.n)) if candidates is None else sorted(set(candidates))
    for v in allowed:
        if not 0 <= v < graph.n:
            raise ValueError(f"candidate vertex {v} out of range")
    allowed = [v for v in allowed if ev.compare(ev.from_weight(graph.weights[v]), ev.zero()) >= 0]
    weights = {v: ev.from_weight(graph.weights[v]) for v in allowed}
    best_vertices: tuple[int, ...] = ()
    best_weight = ev.zero()
    nodes = 0
    pruned = 0

    # Más conflictivo primero para reducir el árbol; desempate estable.
    def recurse(cands: tuple[int, ...], chosen: tuple[int, ...], current: Fraction) -> None:
        nonlocal best_vertices, best_weight, nodes, pruned
        nodes += 1
        if not cands:
            candidate = tuple(sorted(chosen))
            if (current > best_weight or
                (current == best_weight and (len(candidate) > len(best_vertices) or
                 (len(candidate) == len(best_vertices) and candidate < best_vertices)))):
                best_vertices, best_weight = candidate, current
            return
        # Cota y objetivo en Fraction: la poda no depende de redondeo float.
        remaining_ub = greedy_clique_cover_upper_bound_exact(graph, cands)
        if current + remaining_ub < best_weight:
            pruned += 1
            return
        pivot = max(cands, key=lambda v: (sum(u in graph.adj[v] for u in cands), float(weights[v]), -v))
        rest = tuple(v for v in cands if v != pivot)
        # Rama incluir: elimina vecinos del pivot.
        include_cands = tuple(v for v in rest if v not in graph.adj[pivot])
        recurse(include_cands, chosen + (pivot,), current + weights[pivot])
        # Rama excluir.
        recurse(rest, chosen, current)

    recurse(tuple(allowed), (), ev.zero())
    return ExactResult(list(best_vertices), best_weight, best_weight, nodes, pruned, True)
