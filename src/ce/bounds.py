"""Cotas superiores válidas para MWIS.

La cota por cobertura de cliques particiona los vértices candidatos en cliques
(disjuntas como bloques). Un conjunto independiente puede tomar como máximo
un vértice de cada clique; por tanto, la suma de los máximos pesos no negativos
por bloque es una cota superior válida.
"""
from __future__ import annotations
from typing import Iterable
from fractions import Fraction
from .graph import Graph
from .evaluator import ExactEvaluator


def greedy_clique_cover_upper_bound(graph: Graph, candidates: Iterable[int] | None = None) -> float:
    """Cota superior en float, redondeada hacia arriba de forma conservadora."""
    import math
    exact = greedy_clique_cover_upper_bound_exact(graph, candidates)
    try:
        value = float(exact)
    except OverflowError:
        return math.inf
    if math.isfinite(value) and Fraction.from_float(value) < exact:
        value = math.nextafter(value, math.inf)
    return value


def greedy_clique_cover_upper_bound_exact(graph: Graph, candidates: Iterable[int] | None = None) -> Fraction:
    """La misma cota, calculada con Fraction para poda exacta y segura."""
    vertices = list(range(graph.n)) if candidates is None else sorted(set(candidates))
    if not vertices:
        return Fraction(0)
    vset = set(vertices)
    order = sorted(vertices, key=lambda v: (-sum(u in vset for u in graph.adj[v]), v))
    cliques: list[list[int]] = []
    for v in order:
        for clique in cliques:
            if all(u in graph.adj[v] for u in clique):
                clique.append(v)
                break
        else:
            cliques.append([v])
    evaluator = ExactEvaluator()
    exact_weights = {v: evaluator.from_weight(graph.weights[v]) for v in vertices}
    return sum((max(Fraction(0), max(exact_weights[v] for v in clique))
                for clique in cliques), Fraction(0))


def is_valid_upper_bound(graph: Graph, bound: float, candidates=None) -> bool:
    """Diagnostic helper for tests: compare against exhaustive optimum."""
    from .exact import solve_exact
    result = solve_exact(graph, candidates=candidates)
    return bound + 1e-12 >= float(result.weight)
