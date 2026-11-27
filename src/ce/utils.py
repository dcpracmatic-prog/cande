"""Utilidades menores."""

from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
from typing import List

from .graph import Graph
from .solver import AdjustedSolver, Graph as SolverGraph


@dataclass
class Solution:
    vertices: List[int]
    weight: object
    method: str = "multi-start+perturb"

    def __repr__(self) -> str:
        return f"Solution(n={len(self.vertices)}, weight={self.weight}, method={self.method})"


def _to_solver_graph(graph: Graph) -> SolverGraph:
    """Adapta un grafo del repo (listas, pesos float) al Graph del solver
    (conjuntos, pesos enteros en micro-unidades). La conversión es exacta
    para pesos de 6 decimales."""
    adj = [set(neigh) for neigh in graph.adj]
    weights = [int(round(w * 1_000_000)) for w in graph.weights]
    return SolverGraph(n=graph.n, adj=adj, weights=weights)


def solve_both(
    graph: Graph,
    n_restarts: int = 6,
    n_perturbations: int = 8,
    seed: int = 42,
):
    """Resuelve el mismo grafo con el AdjustedSolver (exacto en micro-unidades).

    Antes corría el solver viejo en modo float y exacto; el solver nuevo es
    exacto en micro-unidades, así que ambas presentaciones describen la misma
    solución: float en unidades y exacto como Fraction en micro-unidades.
    """
    sg = _to_solver_graph(graph)
    solver = AdjustedSolver(
        seed=seed, n_restarts=n_restarts, n_perturbations=n_perturbations
    )
    verts, score = solver.solve(sg)
    sol_f = Solution(vertices=sorted(verts), weight=score / 1_000_000)
    sol_e = Solution(vertices=sorted(verts), weight=Fraction(score, 1_000_000))
    return sol_f, sol_e
