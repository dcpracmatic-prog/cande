#include "ce/evaluator.hpp"
#include "ce/graph.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// El solver nuevo es un programa autocontenido (src/solver.cpp) y se prueba
// a través de su binario: la prueba escribe la instancia en formato de texto
// (n m, aristas, pesos en micro-unidades) y lee weight/vertices de su salida.
#ifndef CE_MWIS_BIN
#define CE_MWIS_BIN "./ce_mwis"
#endif

using namespace ce;

struct BinResult {
    int64_t weight_micro = -1;
    std::vector<int> vertices;
};

static BinResult run_solver_bin(int n, const std::vector<std::pair<int, int>>& edges,
                                const std::vector<int64_t>& weights_micro) {
    const std::string in_path = "/tmp/ce_test_exact_instance.txt";
    const std::string out_path = "/tmp/ce_test_exact_out.txt";
    {
        std::ofstream out(in_path);
        out << n << ' ' << edges.size() << '\n';
        for (const auto& e : edges) out << e.first << ' ' << e.second << '\n';
        for (size_t i = 0; i < weights_micro.size(); ++i) {
            if (i) out << ' ';
            out << weights_micro[i];
        }
        out << '\n';
    }
    const std::string cmd = std::string(CE_MWIS_BIN) + " --seed 42 --reps 3 \"" +
                            in_path + "\" > \"" + out_path + "\" 2>&1";
    const int rc = std::system(cmd.c_str());
    assert(rc == 0);
    BinResult r;
    std::ifstream in(out_path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("weight ", 0) == 0) r.weight_micro = std::stoll(line.substr(7));
        else if (line.rfind("vertices", 0) == 0) {
            std::istringstream iss(line.substr(8));
            int v;
            while (iss >> v) r.vertices.push_back(v);
        }
    }
    return r;
}

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
    std::vector<int64_t> w400micro;
    w400micro.reserve(400);
    int64_t sum400micro = 0;
    for (double w : weights400) {
        const int64_t mu = std::llround(w * 1e6);
        w400micro.push_back(mu);
        sum400micro += mu;
    }
    const auto r400 = run_solver_bin(400, {}, w400micro);
    assert(r400.vertices.size() == 400);
    assert(r400.weight_micro == sum400micro);

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
    const auto rneg = run_solver_bin(3, {}, {10000000, -1000000, 9000000});
    assert((rneg.vertices == std::vector<int>{0, 2}));
    assert(rneg.weight_micro == 19000000);

    // Si todos los pesos son negativos, la solución vacía de peso cero es óptima.
    const auto rall = run_solver_bin(2, {{0, 1}}, {-2000000, -1000000});
    assert(rall.vertices.empty());
    assert(rall.weight_micro == 0);

    // Los ceros se pueden seleccionar por el desempate de cardinalidad.
    const auto rz = run_solver_bin(3, {{0, 1}}, {0, 0, 0});
    assert((rz.vertices == std::vector<int>{0, 2}));
    assert(rz.weight_micro == 0);

    const auto rzp = run_solver_bin(3, {{0, 1}, {1, 2}}, {0, 0, 0});
    assert((rzp.vertices == std::vector<int>{0, 2}));
    assert(rzp.weight_micro == 0);

    std::cout << "C++ exact evaluator regression tests: OK\n";
    return 0;
}
