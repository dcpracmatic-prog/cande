/**
 * CLI del solver C++ — salida machine-readable para pruebas diferenciales.
 *
 * Uso:
 *   ./ce_mwis --n 12 --dens 0.2 --seed 42
 *   ./ce_mwis --n 12 --dens 0.2 --seed 42 --json
 *   ./ce_mwis --graph grafo.json --seed 42 --json            # mismo grafo que Python
 *   ./ce_mwis --graph grafo.json --seed 42 --only float --json
 *   ./ce_mwis --graph grafo.json --seed 42 --json  # escaneo original (v3)
 *   (Los experimentos --greedy heap y --jitter per-vertex viven en el
 *   binario ce_mwis_exp; este binario limpio solo acepta sus valores por
 *   defecto y rechaza cualquier otro con un error claro.)
 */
#include "ce/solver.hpp"
#include "ce/graph.hpp"
#include "ce/evaluator.hpp"

#include <chrono>
#include <cctype>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <iomanip>

using namespace ce;

// Parser JSON mínimo para {"n":int,"edges":[[u,v],...],"weights":[w,...]}.
// Sin dependencias: busca las claves y lee los números en orden.
static Graph load_graph_json(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("no se pudo abrir --graph " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    auto find_num = [&](const std::string& key, size_t from = 0) {
        size_t p = s.find(key, from);
        if (p == std::string::npos) throw std::runtime_error("clave no encontrada: " + key);
        p = s.find(':', p);
        return std::strtod(s.c_str() + p + 1, nullptr);
    };
    int n = static_cast<int>(find_num("\"n\""));
    size_t ep = s.find("\"edges\"");
    size_t wp = s.find("\"weights\"");
    if (ep == std::string::npos || wp == std::string::npos)
        throw std::runtime_error("JSON sin edges/weights");
    std::vector<std::pair<int,int>> edges;
    {
        const char* p = s.c_str() + ep;
        const char* end = s.c_str() + wp;
        while (p < end) {
            if (*p == '[' && p + 1 < end && (std::isdigit(p[1]) || p[1] == '-')) {
                char* e1 = nullptr;
                long u = std::strtol(p + 1, &e1, 10);
                long v = std::strtol(e1 + 1, nullptr, 10);
                edges.emplace_back(static_cast<int>(u), static_cast<int>(v));
                p = e1;
            }
            ++p;
        }
    }
    std::vector<double> weights;
    {
        const char* p = s.c_str() + wp;
        p = std::strchr(p, '[');
        const char* end = std::strchr(p, ']');
        while (p && p < end) {
            char* e = nullptr;
            double w = std::strtod(p + 1, &e);
            if (e == p + 1) { ++p; continue; }
            weights.push_back(w);
            p = e;
        }
    }
    return Graph(n, edges, weights);
}

using namespace ce;

static Graph make_random(int n, double dens, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    std::vector<std::pair<int,int>> edges;
    for (int i = 0; i < n; ++i)
        for (int j = i+1; j < n; ++j)
            if (ud(rng) < dens) edges.emplace_back(i, j);
    std::vector<double> weights(n);
    for (int i = 0; i < n; ++i)
        weights[i] = std::round((0.5 + 4.5 * ud(rng)) * 1e6) / 1e6;  // 6 decimales
    return Graph(n, edges, weights);
}

int main(int argc, char** argv) {
    int n = 12;
    double dens = 0.2;
    uint32_t seed = 42;
    bool json = false;
    int restarts = 6;
    int perturbs = 8;
    std::string graph_path;
    std::string only = "both";  // float | exact | both
    std::string greedy_mode = "scan";  // solo se acepta "scan" (ver validación abajo)
    std::string jitter_mode = "per-step";  // solo se acepta "per-step" (ver validación abajo)

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--n" && i+1 < argc) n = std::stoi(argv[++i]);
        else if (a == "--dens" && i+1 < argc) dens = std::stod(argv[++i]);
        else if (a == "--seed" && i+1 < argc) seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (a == "--json") json = true;
        else if (a == "--restarts" && i+1 < argc) restarts = std::stoi(argv[++i]);
        else if (a == "--perturbs" && i+1 < argc) perturbs = std::stoi(argv[++i]);
        else if (a == "--graph" && i+1 < argc) graph_path = argv[++i];
        else if (a == "--greedy" && i+1 < argc) greedy_mode = argv[++i];
        else if (a == "--jitter" && i+1 < argc) jitter_mode = argv[++i];
        else if (a == "--only" && i+1 < argc) only = argv[++i];
    }

    Graph g = graph_path.empty() ? make_random(n, dens, seed) : load_graph_json(graph_path);
    n = g.n;
    SolverConfig cfg;
    cfg.seed = seed;
    cfg.n_restarts = restarts;
    cfg.n_perturbations = perturbs;
    // Binario limpio (v6): los experimentos no existen aquí. Se aceptan los
    // flags solo con sus valores por defecto (para que los scripts sigan
    // funcionando) y cualquier otro valor es un error claro, no un silencio.
    if (greedy_mode != "scan" || jitter_mode != "per-step") {
        std::cerr << "Binario limpio: --greedy heap y --jitter per-vertex no están disponibles aquí; usa ce_mwis_exp.\n";
        return 2;
    }

    bool run_f = (only == "both" || only == "float");
    bool run_e = (only == "both" || only == "exact");

    Solution<FloatEvaluator> sol_f;
    Solution<ExactEvaluator> sol_e;
    double ms_f = 0.0, ms_e = 0.0;
    if (run_f) {
        auto t0 = std::chrono::steady_clock::now();
        MWISSolver<FloatEvaluator> solver_f(FloatEvaluator{}, cfg);
        sol_f = solver_f.solve(g);
        ms_f = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    if (run_e) {
        auto t0 = std::chrono::steady_clock::now();
        MWISSolver<ExactEvaluator> solver_e(ExactEvaluator{}, cfg);
        sol_e = solver_e.solve(g);
        ms_e = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    bool ind_f = !run_f || g.is_independent(sol_f.vertices);
    bool ind_e = !run_e || g.is_independent(sol_e.vertices);

    if (json) {
        std::cout << std::setprecision(10);
        std::cout << "{"
                  << "\"n\":" << n
                  << ",\"seed\":" << seed
                  << ",\"only\":\"" << only << "\""
                  << ",\"elapsed_ms_float\":" << ms_f
                  << ",\"elapsed_ms_exact\":" << ms_e;
        if (run_f) {
            std::cout << ",\"float_size\":" << sol_f.vertices.size()
                      << ",\"float_weight\":" << sol_f.weight
                      << ",\"float_independent\":" << (ind_f ? "true" : "false")
                      << ",\"vertices_float\":[";
            for (size_t i = 0; i < sol_f.vertices.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << sol_f.vertices[i];
            }
            std::cout << "]";
        }
        if (run_e) {
            std::cout << ",\"exact_size\":" << sol_e.vertices.size()
                      << ",\"exact_weight\":" << sol_e.weight.to_double()
                      << ",\"exact_weight_num\":\"" << sol_e.weight.numerator_string() << "\""
                      << ",\"exact_weight_den\":\"" << sol_e.weight.denominator_string() << "\""
                      << ",\"exact_independent\":" << (ind_e ? "true" : "false")
                      << ",\"vertices_exact\":[";
            for (size_t i = 0; i < sol_e.vertices.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << sol_e.vertices[i];
            }
            std::cout << "]";
        }
        std::cout << "}\n";
    } else {
        std::cout << "n=" << n << " dens=" << dens << " seed=" << seed << "\n";
        if (run_f)
            std::cout << "float  : size=" << sol_f.vertices.size()
                      << " weight=" << std::setprecision(8) << sol_f.weight
                      << " independent=" << ind_f << " ms=" << ms_f << "\n";
        if (run_e)
            std::cout << "exact  : size=" << sol_e.vertices.size()
                      << " weight=" << sol_e.weight.to_double()
                      << " independent=" << ind_e << " ms=" << ms_e << "\n";
    }
    return (ind_f && ind_e) ? 0 : 1;
}
