from fractions import Fraction
import pytest
from ce import Graph, solve_exact, greedy_clique_cover_upper_bound_exact, ExactEvaluator
from ce.instances import make_instance, FAMILIES


def brute_force(graph):
    best=Fraction(0); best_set=()
    for mask in range(1 << graph.n):
        vs=tuple(i for i in range(graph.n) if mask & (1 << i))
        if not graph.is_independent(vs): continue
        w=sum((ExactEvaluator().from_weight(graph.weights[i]) for i in vs),Fraction(0))
        if w>best or (w==best and (len(vs)>len(best_set) or (len(vs)==len(best_set) and vs<best_set))):
            best,best_set=w,vs
    return best,best_set

@pytest.mark.parametrize('family', FAMILIES)
def test_exact_solver_matches_bruteforce(family):
    g=make_instance(family,9,12345)
    result=solve_exact(g)
    expected,vertices=brute_force(g)
    assert result.weight==expected
    assert tuple(result.vertices)==vertices
    assert result.optimal


def test_clique_cover_bound_is_upper_bound_on_random_small_graphs():
    for family in FAMILIES:
        for seed in range(8):
            g=make_instance(family,10,seed)
            ub=greedy_clique_cover_upper_bound_exact(g)
            opt=solve_exact(g).weight
            assert ub>=opt


def test_negative_weights_and_empty_candidate_set():
    g=Graph.from_edges(3,[],[10,-1,9])
    r=solve_exact(g)
    assert r.vertices==[0,2]
    assert r.weight==19
    assert solve_exact(g,candidates=[]).weight==0


def test_graph_n_zero_contract_is_explicit():
    with pytest.raises(ValueError,match='n must be positive'):
        Graph.from_edges(0,[],[])
