"""c&e — Causa & Efecto: MWIS con evaluador de pesos desacoplado."""

from .graph import Graph
from .evaluator import FloatEvaluator, ExactEvaluator, WeightEvaluator
from .solver import MWISSolver, Solution, SolverConfig, is_better_solution
from .utils import solve_both
from .exact import solve_exact, ExactResult
from .bounds import greedy_clique_cover_upper_bound, greedy_clique_cover_upper_bound_exact

__all__ = [
    "Graph",
    "FloatEvaluator",
    "ExactEvaluator",
    "WeightEvaluator",
    "MWISSolver",
    "Solution",
    "SolverConfig",
    "is_better_solution",
    "solve_both",
    "solve_exact",
    "ExactResult",
    "greedy_clique_cover_upper_bound",
    "greedy_clique_cover_upper_bound_exact",
]

__version__ = "0.1.3"
