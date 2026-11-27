#include "ce/evaluator.hpp"
#include "ce/graph.hpp"
#include "ce/solver.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

using namespace ce;

int main() {
    ExactEvaluator ev;

    // Regresión: antes 400 pesos positivos de seis decimales desbordaban
    // el numerador int64 y podían producir una suma negativa.
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> ud(0.5, 5.0);
    ExactRational total = ev.zero();
    long double reference = 0.0L;
    for (int i = 0; i < 400; ++i) {
        const double x = std::round(ud(rng) * 1e6) / 1e6;
        total = ev.add(total, ev.from_weight(x));
        reference += static_cast<long double>(x);
    }
    assert(total.num > 0);
    assert(std::fabs(total.to_double() - static_cast<double>(reference)) < 1e-9);

    // Regresión integral del solver: 400 vértices aislados positivos deben
    // seleccionarse todos y el peso no debe desbordarse al evaluar la solución.
    std::vector<double> weights400;
    weights400.reserve(400);
    std::mt19937_64 rng_graph(7);
    for (int i = 0; i < 400; ++i)
        weights400.push_back(std::round(ud(rng_graph) * 1e6) / 1e6);
    Graph isolated400(400, {}, weights400);
    SolverConfig cfg400;
    cfg400.n_restarts = 1;
    cfg400.n_perturbations = 0;
    cfg400.max_local_passes = 0;
    MWISSolver<ExactEvaluator> solver400(ExactEvaluator{}, cfg400);
    const auto sol400 = solver400.solve(isolated400);
    assert(sol400.vertices.size() == 400);
    assert(sol400.weight.num > 0);
    long double sum400 = 0.0L;
    for (double w : weights400) sum400 += static_cast<long double>(w);
    assert(std::fabs(sol400.weight.to_double() - static_cast<double>(sum400)) < 1e-9);

    // Regresión: 1e20 y DBL_MAX son finitos y se convierten sin shifts UB.
    const auto large = ev.from_weight(1e20);
    assert(std::isfinite(ev.to_float(large)));
    assert(ev.compare(large, ev.from_weight(1e20)) == 0);
    const auto largest = ev.from_weight(std::numeric_limits<double>::max());
    assert(std::isfinite(ev.to_float(largest)));
    assert(ev.compare(largest, ev.from_weight(std::numeric_limits<double>::max())) == 0);
    const double samples[] = {0.1, -0.1, 1.0 / 3.0,
                              std::numeric_limits<double>::min(),
                              std::numeric_limits<double>::denorm_min()};
    for (double sample : samples)
        assert(ev.to_float(ev.from_weight(sample)) == sample);

    // También se conserva exactamente el menor subnormal: 1 / 2^1074.
    const auto subnormal = ev.from_weight(std::numeric_limits<double>::denorm_min());
    BigInt expected_den = 1;
    expected_den <<= 1074;
    assert(subnormal.num == 1);
    assert(subnormal.den == expected_den);
    assert(ev.to_float(subnormal) == std::numeric_limits<double>::denorm_min());

    bool rejected_nan = false;
    try { (void)ev.from_weight(std::numeric_limits<double>::quiet_NaN()); }
    catch (const std::invalid_argument&) { rejected_nan = true; }
    assert(rejected_nan);

    // Un peso negativo nunca debe aparecer en un MWIS óptimo.
    Graph g(3, {}, std::vector<double>{10.0, -1.0, 9.0});
    SolverConfig cfg;
    cfg.n_restarts = 3;
    cfg.n_perturbations = 2;
    cfg.seed = 17;
    MWISSolver<FloatEvaluator> float_solver(FloatEvaluator{}, cfg);
    MWISSolver<ExactEvaluator> exact_solver(ExactEvaluator{}, cfg);
    const auto sf = float_solver.solve(g);
    const auto se = exact_solver.solve(g);
    assert((sf.vertices == std::vector<int>{0, 2}));
    assert((se.vertices == std::vector<int>{0, 2}));
    assert(std::fabs(sf.weight - 19.0) < 1e-12);
    assert(std::fabs(se.weight.to_double() - 19.0) < 1e-12);

    // Si todos los pesos son negativos, la solución vacía de peso cero es óptima.
    Graph all_negative(2, {{0, 1}}, std::vector<double>{-2.0, -1.0});
    const auto sn = exact_solver.solve(all_negative);
    assert(sn.vertices.empty());
    assert(ev.compare(sn.weight, ev.zero()) == 0);

    // Los ceros se pueden seleccionar por el desempate de cardinalidad.
    Graph zeros(3, {{0, 1}}, std::vector<double>{0.0, 0.0, 0.0});
    const auto sz = exact_solver.solve(zeros);
    assert((sz.vertices == std::vector<int>{0, 2}));
    assert(ev.compare(sz.weight, ev.zero()) == 0);

    Graph zero_path(3, {{0, 1}, {1, 2}}, std::vector<double>{0.0, 0.0, 0.0});
    const auto zp = exact_solver.solve(zero_path);
    assert((zp.vertices == std::vector<int>{0, 2}));
    assert(ev.compare(zp.weight, ev.zero()) == 0);

    std::cout << "C++ exact evaluator regression tests: OK\n";
    return 0;
}
