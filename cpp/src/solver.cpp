// solver.cpp — Puerto C++17 del AdjustedSolver con tunelamiento adaptativo ×2.
//
// Misma base que refactor_v1_es6.cpp (temp_scale 0.25, cooling 0.85,
// 1-2 activado con tope 80, early-stop 6 restarts sin mejora, 8x10, seed 42)
// más el tunelamiento adaptativo del Python medido (es6 + túnel ×2, el mejor:
// gap 0.096%, 11/15 en el óptimo):
// - degree_cv: coeficiente de variación de los grados del grafo.
// - _should_tunnel_extra: túnel extra solo si multiplier > 1, y no en
//   grafos grandes con cv >= 1.0 (tipo BA n>=200); activo si n >= 100 o cv >= 0.35.
// - En cada restart que no mejora la incumbente: UN solo intento extra de
//   perturbación + mejora local completa desde el mejor actual (o desde cur
//   si no hay mejor). Es el bloque original de un intento, no el de ×3.
//
// Pesos enteros exactos en micro-unidades (int64). RNG propio: std::mt19937
// con seed 42 (secuencia distinta a la de Python random, misma distribución);
// por eso los pesos pueden diferir ligeramente del Python: lo que se mide es
// la distribución de gaps y la velocidad, no una reproducción bit a bit.
//
// Entrada (texto simple, enteros): n m, luego m líneas "u v" (0-based),
// luego n pesos en micro-unidades (pueden ir en varias líneas).
// Salida stdout: líneas "weight <score>", "time_s <s>", "vertices <v ...>".
//
// Compilar: g++ -O3 -std=c++17 -o ce_mwis solver.cpp

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

using i64 = long long;

struct Graph {
    int n = 0;
    std::vector<std::vector<int>> adj;  // listas ordenadas
    std::vector<std::vector<char>> mat; // matriz de adyacencia (membresía O(1))
    std::vector<i64> w;                 // pesos en micro-unidades
};

static bool is_independent(const Graph& g, const std::set<int>& s) {
    for (int u : s) {
        for (int v : g.adj[u]) {
            if (s.count(v)) return false;
        }
    }
    return true;
}

static i64 objective(const std::vector<i64>& w, const std::set<int>& s) {
    i64 t = 0;
    for (int v : s) t += w[v];
    return t;
}

// Desempate determinista: peso -> cardinalidad -> orden lexicográfico.
static bool better_solution(i64 wa, const std::set<int>& A, i64 wb,
                            const std::set<int>& B) {
    if (wa != wb) return wa > wb;
    if (A.size() != B.size()) return A.size() > B.size();
    auto ia = A.begin(), ib = B.begin();
    for (; ia != A.end(); ++ia, ++ib) {
        if (*ia != *ib) return *ia < *ib;
    }
    return false;
}

struct Rng {
    std::mt19937 e;
    explicit Rng(unsigned seed) : e(seed) {}
    double uniform01() {
        std::uniform_real_distribution<double> d(0.0, 1.0);
        return d(e);
    }
    double uniform(double a, double b) { return a + (b - a) * uniform01(); }
    int uniform_int(int lo, int hi) {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(e);
    }
};

struct AdjustedSolver {
    int seed = 42;
    int n_restarts = 8;
    int n_perturbations = 10;
    double drop_fraction = 0.25;
    double cooling = 0.85;
    int max_local_passes = 40;
    bool enable_one_to_two = true;
    double greedy_alpha = 0.50;
    double greedy_beta = 0.50;
    int max_12_candidates = 80;
    double initial_acceptance_p = 0.50;
    double temp_scale = 0.25;
    int early_stop_restarts = 6;
    int tunnel_multiplier = 2;
    double max_time_s = -1.0;  // < 0: sin presupuesto de tiempo.

    // Coeficiente de variación de grados (inspirado en row_nnz_irregularity).
    static double degree_cv(const Graph& g) {
        int n = g.n;
        if (n < 2) return 0.0;
        double mean_d = 0.0;
        for (int u = 0; u < n; ++u) mean_d += (double)g.adj[u].size();
        mean_d /= (double)n;
        if (mean_d <= 0.0) return 0.0;
        double var_d = 0.0;
        for (int u = 0; u < n; ++u) {
            double d = (double)g.adj[u].size() - mean_d;
            var_d += d * d;
        }
        var_d /= (double)n;
        return std::sqrt(var_d) / mean_d;
    }

    // Activa el tunelamiento extra solo en regímenes donde aporta mayor ganancia.
    bool should_tunnel_extra(int n, double cv) const {
        if (tunnel_multiplier <= 1) return false;
        // Evita tunelamiento extra costoso en grafos con fuerte asimetría
        // de grados tipo BA grande.
        if (cv >= 1.0 && n >= 200) return false;
        // Prioriza grafos medianos/grandes con dispersión moderada (típicamente ER).
        return n >= 100 || cv >= 0.35;
    }

    std::pair<std::vector<int>, i64> solve(const Graph& g) {
        const auto& weights = g.w;
        const auto t_start = std::chrono::steady_clock::now();
        auto time_up = [&]() {
            return max_time_s >= 0.0 &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count() >= max_time_s;
        };
        double cv = degree_cv(g);
        bool use_extra_tunnel = should_tunnel_extra(g.n, cv);
        Rng rng((unsigned)seed);

        std::set<int> best_set;
        i64 best_w = 0;

        std::vector<i64> positive;
        for (i64 w : weights) if (w > 0) positive.push_back(w);
        double mean_w = 1.0;
        if (!positive.empty()) {
            double s = 0.0;
            for (i64 w : positive) s += (double)w;
            mean_w = std::max(1.0, s / (double)positive.size());
        }
        double median = 1.0;
        if (!positive.empty()) {
            std::vector<i64> sp = positive;
            std::sort(sp.begin(), sp.end());
            size_t m = sp.size();
            median = (m % 2 == 1) ? (double)sp[m / 2]
                                  : ((double)sp[m / 2 - 1] + (double)sp[m / 2]) / 2.0;
        }
        double delta_typical = median * temp_scale;
        double temp0 = -delta_typical / std::log(initial_acceptance_p);

        int restarts_without_gain = 0;
        bool timed_out = false;
        for (int restart = 0; restart < n_restarts; ++restart) {
            if (time_up()) { timed_out = true; break; }

            double jitter = (restart == 0) ? 0.0 : std::min(0.35, 0.05 * restart);
            std::set<int> sol = greedy_fill(g, std::set<int>(), rng, mean_w, jitter);
            auto pr = local_improve_fast(g, sol);
            std::set<int> cur = pr.first;
            i64 cur_w = pr.second;

            bool improved_this_restart = false;
            if (better_solution(cur_w, cur, best_w, best_set)) {
                best_set = cur; best_w = cur_w;
                improved_this_restart = true;
            }

            double temp = temp0;
            for (int k = 0; k < n_perturbations; ++k) {
                if (time_up()) { timed_out = true; break; }

                auto cp = perturb_repair_fast(g, cur, rng, mean_w);
                std::set<int> cand = cp.first;
                i64 cand_w = cp.second;
                if (better_solution(cand_w, cand, best_w, best_set)) {
                    best_set = cand; best_w = cand_w;
                    improved_this_restart = true;
                }
                double delta = (double)(cand_w - cur_w);
                if (delta >= 0.0 || (temp > 1e-12 && rng.uniform01() < std::exp(delta / temp))) {
                    cur = cand; cur_w = cand_w;
                }
                temp *= cooling;
            }

            if (timed_out) break;

            auto rf = local_improve_full(g, cur);
            if (better_solution(rf.second, rf.first, best_w, best_set)) {
                best_set = rf.first; best_w = rf.second;
                improved_this_restart = true;
            }

            // Segundo tunelamiento (× 2) condicional cuando el reinicio no mejoró la incumbente.
            if (!improved_this_restart && use_extra_tunnel && !time_up()) {
                std::set<int> base_sol = best_set.empty() ? cur : best_set;
                auto tp = perturb_repair_fast(g, base_sol, rng, mean_w);
                auto tf = local_improve_full(g, tp.first);
                std::set<int> tun_cand = tf.first;
                i64 tun_w = tf.second;
                if (better_solution(tun_w, tun_cand, best_w, best_set)) {
                    best_set = tun_cand; best_w = tun_w;
                    improved_this_restart = true;
                }
                if (better_solution(tun_w, tun_cand, cur_w, cur)) {
                    cur = tun_cand; cur_w = tun_w;
                }
            }

            if (improved_this_restart) {
                restarts_without_gain = 0;
            } else if (early_stop_restarts > 0) {
                if (++restarts_without_gain >= early_stop_restarts) break;
            }
        }

        auto fr = local_improve_full(g, best_set);
        if (better_solution(fr.second, fr.first, best_w, best_set)) {
            best_set = fr.first; best_w = fr.second;
        }
        if (!is_independent(g, best_set)) {
            throw std::runtime_error("AdjustedSolver devolvió una solución no independiente");
        }
        std::vector<int> ordered(best_set.begin(), best_set.end());
        return {ordered, best_w};
    }

    std::set<int> greedy_fill(const Graph& g, const std::set<int>& initial,
                              Rng& rng, double mean_w, double jitter) {
        const auto& adj = g.adj;
        const auto& weights = g.w;
        const int n = g.n;
        std::set<int> selected = initial;
        std::set<int> blocked = selected;
        for (int u : selected) for (int v : adj[u]) blocked.insert(v);

        std::set<int> candidates;
        for (int u = 0; u < n; ++u)
            if (!blocked.count(u) && weights[u] >= 0) candidates.insert(u);

        std::vector<int> residual_degree(n, 0);
        std::vector<i64> residual_neighbor_weight(n, 0);
        for (int u = 0; u < n; ++u) {
            for (int v : adj[u]) {
                if (candidates.count(v)) {
                    residual_degree[u] += 1;
                    residual_neighbor_weight[u] += weights[v];
                }
            }
        }

        while (!candidates.empty()) {
            int best_u = -1;
            double best_score = -1e300;
            for (int u : candidates) {
                double denom = 1.0 + greedy_alpha * residual_degree[u]
                             + greedy_beta * (double)residual_neighbor_weight[u] / mean_w;
                double score = (double)weights[u] / denom;
                if (jitter > 0.0) score *= 1.0 + rng.uniform(-jitter, jitter);
                if (score > best_score || (score == best_score && rng.uniform01() < 0.5)) {
                    best_u = u; best_score = score;
                }
            }
            if (best_u < 0) throw std::runtime_error("Greedy ajustado sin candidato");

            selected.insert(best_u);
            std::set<int> removed;
            removed.insert(best_u);
            for (int v : adj[best_u]) if (candidates.count(v)) removed.insert(v);
            for (int x : removed) candidates.erase(x);

            for (int x : removed) {
                i64 wx = weights[x];
                for (int y : adj[x]) {
                    if (candidates.count(y)) {
                        residual_degree[y] -= 1;
                        residual_neighbor_weight[y] -= wx;
                    }
                }
            }
        }
        return selected;
    }

    static std::vector<int> build_conflict_counts(const Graph& g,
                                                  const std::set<int>& cur) {
        std::vector<int> counts(g.n, 0);
        for (int u : cur) for (int v : g.adj[u]) counts[v] += 1;
        return counts;
    }

    static i64 insert_vertex(const Graph& g, int v, std::set<int>& cur, i64 cur_w,
                             std::vector<int>& counts) {
        cur.insert(v);
        for (int u : g.adj[v]) counts[u] += 1;
        return cur_w + g.w[v];
    }

    static i64 remove_vertex(const Graph& g, int v, std::set<int>& cur, i64 cur_w,
                             std::vector<int>& counts) {
        cur.erase(v);
        for (int u : g.adj[v]) counts[u] -= 1;
        return cur_w - g.w[v];
    }

    // Adición + 1-1 (rápido, con conteos).
    std::pair<std::set<int>, i64> add_and_one_to_one(const Graph& g,
                                                     std::set<int> cur, i64 cur_w,
                                                     std::vector<int>& counts) {
        const auto& adj = g.adj;
        const auto& weights = g.w;
        const int n = g.n;
        while (true) {
            bool improved = false;
            for (int v = 0; v < n; ++v) {
                if (!cur.count(v) && counts[v] == 0 && weights[v] >= 0) {
                    cur_w = insert_vertex(g, v, cur, cur_w, counts);
                    improved = true;
                    break;
                }
            }
            if (improved) continue;

            for (int v : cur) {
                i64 wv = weights[v];
                bool done = false;
                for (int u : adj[v]) {
                    if (cur.count(u) || counts[u] != 1) continue;
                    i64 wu = weights[u];
                    if (wu > wv || (wu == wv && u < v)) {
                        cur_w = remove_vertex(g, v, cur, cur_w, counts);
                        cur_w = insert_vertex(g, u, cur, cur_w, counts);
                        improved = true; done = true;
                        break;
                    }
                }
                if (done) break;
            }
            if (!improved) return {cur, cur_w};
        }
    }

    std::pair<std::set<int>, i64> local_improve_fast(const Graph& g,
                                                     const std::set<int>& selected) {
        std::set<int> cur = selected;
        i64 cur_w = objective(g.w, cur);
        std::vector<int> counts = build_conflict_counts(g, cur);
        return add_and_one_to_one(g, cur, cur_w, counts);
    }

    std::pair<std::set<int>, i64> local_improve_full(const Graph& g,
                                                     const std::set<int>& selected) {
        const auto& adj = g.adj;
        const auto& weights = g.w;
        const int n = g.n;
        std::set<int> cur = selected;
        i64 cur_w = objective(g.w, cur);
        std::vector<int> counts = build_conflict_counts(g, cur);

        for (int pass = 0; pass < max_local_passes; ++pass) {
            auto a = add_and_one_to_one(g, cur, cur_w, counts);
            cur = a.first; cur_w = a.second;
            bool improved = false;

            // 2-1 guiado por conflict_count == 2
            if (cur.size() >= 2) {
                std::vector<int> cand21;
                for (int u = 0; u < n; ++u)
                    if (!cur.count(u) && counts[u] == 2 && weights[u] > 0) cand21.push_back(u);
                std::stable_sort(cand21.begin(), cand21.end(),
                                 [&](int a, int b) { return weights[a] > weights[b]; });
                for (int u : cand21) {
                    std::vector<int> pair;
                    for (int v : adj[u]) if (cur.count(v)) pair.push_back(v);
                    if (pair.size() != 2) continue;
                    int av = pair[0], bv = pair[1];
                    if (weights[u] > weights[av] + weights[bv]) {
                        cur_w = remove_vertex(g, av, cur, cur_w, counts);
                        cur_w = remove_vertex(g, bv, cur, cur_w, counts);
                        cur_w = insert_vertex(g, u, cur, cur_w, counts);
                        improved = true;
                        break;
                    }
                }
            }
            if (improved) continue;

            // 1-2: u, x entran al sacar v; ambos con conflict_count == 1
            if (enable_one_to_two && !cur.empty()) {
                for (int v : cur) {
                    i64 wv = weights[v];
                    std::vector<int> compat;
                    for (int u : adj[v])
                        if (!cur.count(u) && counts[u] == 1 && weights[u] >= 0) compat.push_back(u);
                    if (compat.size() < 2) continue;
                    std::stable_sort(compat.begin(), compat.end(),
                                     [&](int a, int b) { return weights[a] > weights[b]; });
                    i64 max_partner = weights[compat[0]];
                    std::vector<int> filt;
                    for (int u : compat)
                        if (weights[u] + max_partner >= wv) filt.push_back(u);
                    compat.swap(filt);
                    if (max_12_candidates >= 0 && (int)compat.size() > max_12_candidates)
                        compat.resize((size_t)max_12_candidates);

                    int L = (int)compat.size();
                    for (int i = 0; i + 1 < L; ++i) {
                        int u = compat[i];
                        if (weights[u] + weights[compat[i + 1]] < wv) break;
                        for (int j = i + 1; j < L; ++j) {
                            int x = compat[j];
                            if (weights[u] + weights[x] < wv) break;
                            if (!g.mat[u][x]) {
                                cur_w = remove_vertex(g, v, cur, cur_w, counts);
                                cur_w = insert_vertex(g, u, cur, cur_w, counts);
                                cur_w = insert_vertex(g, x, cur, cur_w, counts);
                                improved = true;
                                break;
                            }
                        }
                        if (improved) break;
                    }
                    if (improved) break;
                }
            }
            if (!improved) break;
        }
        return {cur, cur_w};
    }

    // Continuación: desde el mejor actual, SOLO repite el bucle de
    // perturbación + recocido con una semilla nueva (sin reconstruir
    // greedy desde cero ni repetir los restarts). La temperatura se
    // reinicia cada n_perturbations rondas, como en un restart.
    // Devuelve el mejor actualizado.
    std::pair<std::set<int>, i64> continue_search(const Graph& g,
                                                  std::set<int> best_set, i64 best_w,
                                                  unsigned seed, int rounds) {
        const auto& weights = g.w;
        Rng rng(seed);
        std::vector<i64> positive;
        for (i64 w : weights) if (w > 0) positive.push_back(w);
        double mean_w = 1.0;
        if (!positive.empty()) {
            double s = 0.0;
            for (i64 w : positive) s += (double)w;
            mean_w = std::max(1.0, s / (double)positive.size());
        }
        double median = 1.0;
        if (!positive.empty()) {
            std::vector<i64> sp = positive;
            std::sort(sp.begin(), sp.end());
            size_t m = sp.size();
            median = (m % 2 == 1) ? (double)sp[m / 2]
                                  : ((double)sp[m / 2 - 1] + (double)sp[m / 2]) / 2.0;
        }
        double temp0 = -median * temp_scale / std::log(initial_acceptance_p);
        double temp = temp0;
        std::set<int> cur = best_set;
        i64 cur_w = best_w;
        for (int k = 0; k < rounds; ++k) {
            if (k % n_perturbations == 0) temp = temp0;
            auto cp = perturb_repair_fast(g, cur, rng, mean_w);
            std::set<int> cand = cp.first;
            i64 cand_w = cp.second;
            if (better_solution(cand_w, cand, best_w, best_set)) {
                best_set = cand; best_w = cand_w;
            }
            double delta = (double)(cand_w - cur_w);
            if (delta >= 0.0 || (temp > 1e-12 && rng.uniform01() < std::exp(delta / temp))) {
                cur = cand; cur_w = cand_w;
            }
            temp *= cooling;
        }
        auto rf = local_improve_full(g, cur);
        if (better_solution(rf.second, rf.first, best_w, best_set)) {
            best_set = rf.first; best_w = rf.second;
        }
        return {best_set, best_w};
    }

    std::pair<std::set<int>, i64> perturb_repair_fast(const Graph& g,
                                                      const std::set<int>& selected,
                                                      Rng& rng, double mean_w) {
        if (selected.empty()) {
            std::set<int> filled = greedy_fill(g, std::set<int>(), rng, mean_w, 0.15);
            return local_improve_fast(g, filled);
        }
        int k_drop = std::max(1, (int)std::lround((double)selected.size() * drop_fraction));
        k_drop = std::min(k_drop, (int)selected.size());
        std::vector<int> sorted_sel(selected.begin(), selected.end());
        for (int i = 0; i < k_drop; ++i) {
            int j = rng.uniform_int(i, (int)sorted_sel.size() - 1);
            std::swap(sorted_sel[i], sorted_sel[j]);
        }
        std::set<int> partial;
        for (size_t i = (size_t)k_drop; i < sorted_sel.size(); ++i) partial.insert(sorted_sel[i]);
        std::set<int> repaired = greedy_fill(g, partial, rng, mean_w, 0.10);
        return local_improve_fast(g, repaired);
    }
};

int main(int argc, char** argv) {
    std::string input;
    AdjustedSolver solver;
    int reps = 1;
    int seed_base = 42;
    int cont_rounds = 20;
    std::string mode = "multi";  // multi: solves completos (el mejor medido).
                                 // cont: 1 solve + continuaciones (experimento:
                                 // no recuperó las ganancias del multi).
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](int& j) {
            if (j + 1 >= argc) { std::cerr << "falta valor para " << a << "\n"; return false; }
            return true;
        };
        if (a == "--seed") { if (!need(i)) return 2; solver.seed = std::atoi(argv[++i]); seed_base = solver.seed; }
        else if (a == "--seed-base") { if (!need(i)) return 2; seed_base = std::atoi(argv[++i]); }
        else if (a == "--reps") { if (!need(i)) return 2; reps = std::atoi(argv[++i]); }
        else if (a == "--cont-rounds") { if (!need(i)) return 2; cont_rounds = std::atoi(argv[++i]); }
        else if (a == "--mode") { if (!need(i)) return 2; mode = argv[++i]; }
        else if (a == "--restarts") { if (!need(i)) return 2; solver.n_restarts = std::atoi(argv[++i]); }
        else if (a == "--perturbations") { if (!need(i)) return 2; solver.n_perturbations = std::atoi(argv[++i]); }
        else if (a == "--early-stop") { if (!need(i)) return 2; solver.early_stop_restarts = std::atoi(argv[++i]); }
        else if (a == "--tunnel-mult") { if (!need(i)) return 2; solver.tunnel_multiplier = std::atoi(argv[++i]); }
        else if (a == "--max-time-s") { if (!need(i)) return 2; solver.max_time_s = std::atof(argv[++i]); }
        else if (a == "--one-to-two") { if (!need(i)) return 2; solver.enable_one_to_two = std::atoi(argv[++i]) != 0; }
        else if (!a.empty() && a[0] != '-') input = a;
        else { std::cerr << "argumento desconocido: " << a << "\n"; return 2; }
    }
    if (input.empty()) { std::cerr << "uso: ce_mwis <instancia.txt> [--seed N] [--reps R] [--mode multi|cont] [--tunnel-mult N] [--max-time-s S]\n"; return 2; }

    std::ifstream in(input);
    if (!in) { std::cerr << "no puedo abrir " << input << "\n"; return 2; }
    int n, m;
    if (!(in >> n >> m)) { std::cerr << "formato inválido\n"; return 2; }
    Graph g;
    g.n = n;
    g.adj.assign(n, {});
    g.mat.assign(n, std::vector<char>(n, 0));
    g.w.assign(n, 0);
    for (int k = 0; k < m; ++k) {
        int u, v;
        in >> u >> v;
        g.adj[u].push_back(v);
        g.adj[v].push_back(u);
        g.mat[u][v] = g.mat[v][u] = 1;
    }
    for (int i = 0; i < n; ++i) {
        std::sort(g.adj[i].begin(), g.adj[i].end());
        g.adj[i].erase(std::unique(g.adj[i].begin(), g.adj[i].end()), g.adj[i].end());
    }
    for (int i = 0; i < n; ++i) {
        i64 w;
        in >> w;
        g.w[i] = w;
    }

    // Modo multi (por defecto, el mejor medido): reps solves completos
    // con semillas base, base+1, ...; cada uno validado exacto y se
    // queda el mejor. Modo cont: 1 solve + continuaciones (solo el
    // bucle de perturbación desde el mejor); medido peor que multi.
    i64 best_score = -1;
    std::vector<int> best_verts;
    double total_dt = 0.0;
    if (mode == "multi") {
        for (int rep = 0; rep < reps; ++rep) {
            solver.seed = seed_base + rep;
            auto t0 = std::chrono::steady_clock::now();
            auto res = solver.solve(g);
            auto t1 = std::chrono::steady_clock::now();
            total_dt += std::chrono::duration<double>(t1 - t0).count();
            std::set<int> vs(res.first.begin(), res.first.end());
            i64 check_w = 0;
            for (int v : vs) check_w += g.w[v];
            if (check_w != res.second || !is_independent(g, vs)) {
                std::cerr << "rep " << rep << " inválida, se descarta\n";
                continue;
            }
            std::set<int> best_set(best_verts.begin(), best_verts.end());
            if (best_score < 0 || better_solution(res.second, vs, best_score, best_set)) {
                best_score = res.second;
                best_verts = res.first;
            }
        }
    } else if (mode == "cont") {
        // Rep 0: solve completo. Reps siguientes: SOLO la continuación
        // (bucle de perturbación + recocido) desde el mejor actual.
        {
            solver.seed = seed_base;
            auto t0 = std::chrono::steady_clock::now();
            auto res = solver.solve(g);
            auto t1 = std::chrono::steady_clock::now();
            total_dt += std::chrono::duration<double>(t1 - t0).count();
            std::set<int> vs(res.first.begin(), res.first.end());
            i64 check_w = 0;
            for (int v : vs) check_w += g.w[v];
            if (check_w != res.second || !is_independent(g, vs)) {
                std::cerr << "solve inicial inválido\n";
                return 3;
            }
            best_score = res.second;
            best_verts = res.first;
        }
        for (int rep = 1; rep < reps; ++rep) {
            std::set<int> best_set(best_verts.begin(), best_verts.end());
            auto t0 = std::chrono::steady_clock::now();
            auto res = solver.continue_search(g, best_set, best_score,
                                              (unsigned)(seed_base + rep), cont_rounds);
            auto t1 = std::chrono::steady_clock::now();
            total_dt += std::chrono::duration<double>(t1 - t0).count();
            std::set<int> vs(res.first.begin(), res.first.end());
            i64 check_w = 0;
            for (int v : vs) check_w += g.w[v];
            if (check_w != res.second || !is_independent(g, vs)) {
                std::cerr << "continuación " << rep << " inválida, se descarta\n";
                continue;
            }
            if (better_solution(res.second, vs, best_score, best_set)) {
                best_score = res.second;
                best_verts.assign(res.first.begin(), res.first.end());
            }
        }
    } else {
        std::cerr << "modo desconocido: " << mode << " (multi|cont)\n";
        return 2;
    }
    if (best_score < 0) { std::cerr << "ninguna repetición válida\n"; return 3; }

    std::cout << "weight " << best_score << "\n";
    std::cout << "time_s " << total_dt << "\n";
    std::cout << "reps " << reps << "\n";
    std::cout << "vertices";
    for (int v : best_verts) std::cout << " " << v;
    std::cout << "\n";
    return 0;
}
