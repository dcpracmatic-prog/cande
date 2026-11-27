"""Compatibilidad del antiguo nombre singular `ce.bound`."""
from .bounds import greedy_clique_cover_upper_bound, greedy_clique_cover_upper_bound_exact, is_valid_upper_bound

__all__ = ["greedy_clique_cover_upper_bound", "greedy_clique_cover_upper_bound_exact", "is_valid_upper_bound"]
