"""Utilidades menores."""

from __future__ import annotations

from .graph import Graph
from .evaluator import FloatEvaluator, ExactEvaluator
from .solver import MWISSolver, SolverConfig


def solve_both(
    graph: Graph,
    n_restarts: int = 6,
    n_perturbations: int = 8,
    seed: int = 42,
):
    """Resuelve el mismo grafo con ambos evaluadores."""
    cfg = SolverConfig(
        n_restarts=n_restarts,
        n_perturbations=n_perturbations,
        seed=seed,
    )
    sol_f = MWISSolver(FloatEvaluator(), config=cfg).solve(graph)
    sol_e = MWISSolver(ExactEvaluator(), config=cfg).solve(graph)
    return sol_f, sol_e
