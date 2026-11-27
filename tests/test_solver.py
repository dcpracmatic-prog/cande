"""Pruebas básicas de corrección del solver."""

from ce import Graph, MWISSolver, FloatEvaluator, ExactEvaluator


def test_empty_and_single():
    g = Graph.from_edges(1, edges=[], weights=[3.5])
    sol = MWISSolver(FloatEvaluator()).solve(g)
    assert sol.vertices == [0]
    assert abs(sol.weight - 3.5) < 1e-9


def test_simple_path():
    # Path de 4 vértices: 0-1-2-3, pesos [10, 1, 1, 10]
    # Óptimo obvio: {0,2} o {0,3} o {1,3} → mejor {0,3} peso 20
    g = Graph.from_edges(4, edges=[(0,1),(1,2),(2,3)], weights=[10, 1, 1, 10])
    sol = MWISSolver(FloatEvaluator()).solve(g)
    assert g.is_independent(sol.vertices)
    assert sol.weight >= 11  # al menos algo razonable


def test_independence_always():
    g = Graph.from_edges(
        6,
        edges=[(0,1),(0,2),(1,3),(2,3),(3,4),(4,5)],
        weights=[1, 2, 3, 4, 5, 6],
    )
    for ev in (FloatEvaluator(), ExactEvaluator()):
        sol = MWISSolver(ev).solve(g)
        assert g.is_independent(sol.vertices)


def test_negative_weights_are_excluded_and_empty_set_is_valid():
    # En un grafo sin aristas, seleccionar un peso negativo empeora el objetivo.
    g = Graph.from_edges(3, edges=[], weights=[10.0, -1.0, 9.0])
    cfg = __import__("ce").SolverConfig(n_restarts=1, n_perturbations=0, seed=1)
    for ev in (FloatEvaluator(), ExactEvaluator()):
        sol = MWISSolver(ev, config=cfg).solve(g)
        assert sol.vertices == [0, 2]
        assert g.is_independent(sol.vertices)
        assert float(sol.weight) == 19.0

    all_negative = Graph.from_edges(2, edges=[(0, 1)], weights=[-2.0, -1.0])
    for ev in (FloatEvaluator(), ExactEvaluator()):
        sol = MWISSolver(ev, config=cfg).solve(all_negative)
        assert sol.vertices == []
        assert float(sol.weight) == 0.0


def test_non_finite_float_weights_are_rejected():
    import math
    import pytest

    g = Graph.from_edges(1, edges=[], weights=[math.nan])
    with pytest.raises(ValueError, match="no finito"):
        MWISSolver(FloatEvaluator()).solve(g)


def test_zero_weight_tie_break_prefers_more_vertices():
    # En un camino de tres vértices, {0, 2} empata en peso y domina en tamaño a {1}.
    g = Graph.from_edges(3, edges=[(0, 1), (1, 2)], weights=[0.0, 0.0, 0.0])
    for ev in (FloatEvaluator(), ExactEvaluator()):
        sol = MWISSolver(ev).solve(g)
        assert sol.vertices == [0, 2]
        assert float(sol.weight) == 0.0


def test_solver_raises_runtime_error_not_assert_for_invalid_internal_solution(monkeypatch):
    # Contrato defensivo: la validación no desaparece con python -O.
    from ce import Graph, MWISSolver, FloatEvaluator
    g = Graph.from_edges(1, [], [1.0])
    solver = MWISSolver(FloatEvaluator())
    monkeypatch.setattr(g, 'is_independent', lambda vertices: False)
    import pytest
    with pytest.raises(RuntimeError, match='not independent'):
        solver.solve(g)
