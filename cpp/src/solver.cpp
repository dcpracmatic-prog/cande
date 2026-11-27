/**
 * Implementación del solver MWIS (esqueleto funcional).
 * Enfocado en equivalencia con Python y en estructuras listas para optimización.
 */

#include "ce/solver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <queue>
#include <set>

namespace ce {

namespace {
// Bitsets de n bits en palabras de 64, para probar compatibilidad
// (vértice sin vecinos en un conjunto) con un AND por palabra en vez de
// recorrer la lista de adyacencia (grado ~80 en n=400, dens 0.2).
// El test es exactamente equivalente al recorrido: (adj[u] & S) == 0.
// No cambia ninguna decisión, solo el costo.
using Bits = std::vector<uint64_t>;
inline size_t nwords(int n) { return static_cast<size_t>((n + 63) / 64); }
inline void bit_set(Bits& b, int v) { b[static_cast<size_t>(v >> 6)] |= (1ULL << (v & 63)); }
inline void bit_clear(Bits& b, int v) { b[static_cast<size_t>(v >> 6)] &= ~(1ULL << (v & 63)); }
inline bool bit_test(const Bits& b, int v) {
    return (b[static_cast<size_t>(v >> 6)] >> (v & 63)) & 1ULL;
}
inline bool bits_disjoint(const Bits& a, const Bits& b) {
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] & b[i]) return false;
    return true;
}
inline std::vector<Bits> build_adj_bits(const Graph& g) {
    std::vector<Bits> ab(static_cast<size_t>(g.n), Bits(nwords(g.n), 0));
    for (int u = 0; u < g.n; ++u)
        for (int v : g.adj[u]) bit_set(ab[u], v);
    return ab;
}
}  // namespace

template <typename Evaluator>
const std::vector<std::vector<uint64_t>>&
MWISSolver<Evaluator>::adjacency_bits(const Graph& g) const
{
    if (cached_graph_ != &g) {
        adj_bits_cache_ = build_adj_bits(g);
        cached_graph_ = &g;
    }
    return adj_bits_cache_;
}

template <typename Evaluator>
typename Evaluator::weight_t
MWISSolver<Evaluator>::eval_weight(
    const std::vector<int>& selected,
    const std::vector<typename Evaluator::weight_t>& w) const
{
    auto acc = ev_.zero();
    for (int v : selected) acc = ev_.add(acc, w[v]);
    return acc;
}

// Greedy por escaneo. El modo de jitter es parámetro de plantilla para que
// el camino por defecto (por paso) no pague ninguna rama en tiempo de
// ejecución por el experimento v5: `if constexpr` elimina la rama al compilar.
//
// v5: jitter por vértice. Con el jitter por paso original se sorteaba un
// factor nuevo por candidato y por paso; con jitter por vértice se sortea uno
// por vértice al empezar la construcción y queda fijo. Misma distribución,
// sorteos distintos: el camino de búsqueda cambia y la calidad se re-mide
// (hipótesis que dejó v4: la ganancia de calidad del heap en n grande venía
// del jitter, no del heap).
template <typename Evaluator>
std::vector<int>
MWISSolver<Evaluator>::greedy_construct(
    const Graph& g,
    const std::vector<typename Evaluator::weight_t>& w,
    std::mt19937& rng,
    double jitter) const
{
    const int n = g.n;
    std::vector<char> blocked(n, 0);
    std::vector<int> selected;
    selected.reserve(n);

    std::vector<int> residual(n);
    for (int i = 0; i < n; ++i) residual[i] = g.degree(i);

    int remaining = n;
    const auto zero = ev_.zero();
    // Un vértice con peso negativo nunca forma parte de un MWIS óptimo.
    // Los pesos cero se conservan por el desempate de mayor cardinalidad.
    for (int v = 0; v < n; ++v) {
        if (ev_.compare(w[v], zero) < 0) {
            blocked[v] = 1;
            --remaining;
        }
    }
    for (int v = 0; v < n; ++v) {
        if (blocked[v]) {
            for (int u : g.adj[v])
                if (!blocked[u]) --residual[u];
        }
    }
    while (remaining > 0) {
        int best_u = -1;
        typename Evaluator::weight_t best_w = ev_.zero();
        int best_denom = 1;

        for (int u = 0; u < n; ++u) {
            if (blocked[u]) continue;
            int denom = residual[u] + 1;
            auto wu = w[u];
            if (jitter > 0.0) {
                std::uniform_real_distribution<double> dist(-jitter, jitter);
                double factor = 1.0 + dist(rng);
                double noisy = std::max(0.0, ev_.to_float(w[u]) * factor);
                wu = ev_.from_weight(noisy);
            }
            if (best_u < 0) {
                best_u = u;
                best_w = wu;
                best_denom = denom;
            } else {
                int cmp = ev_.compare_ratios(wu, denom, best_w, best_denom);
                if (cmp > 0 || (cmp == 0 && (rng() & 1))) {
                    best_u = u;
                    best_w = wu;
                    best_denom = denom;
                }
            }
        }
        if (best_u < 0) break;

        selected.push_back(best_u);
        blocked[best_u] = 1;
        --remaining;
        for (int v : g.adj[best_u]) {
            if (!blocked[v]) {
                blocked[v] = 1;
                --remaining;
                for (int x : g.adj[v])
                    if (!blocked[x]) --residual[x];
            }
        }
        for (int x : g.adj[best_u])
            if (!blocked[x]) --residual[x];
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}


template <typename Evaluator>
std::vector<int>
MWISSolver<Evaluator>::local_improve(
    const Graph& g,
    const std::vector<typename Evaluator::weight_t>& w,
    std::vector<int> selected) const
{
    const int n = g.n;
    std::vector<char> in(n, 0);
    for (int v : selected) in[v] = 1;
    auto cur_w = eval_weight(selected, w);
    const auto zero = ev_.zero();
    // Bitsets: adjBits[u] = vecinos de u; curBits = vértices en `selected`.
    // Se mantienen sincronizados con `in` en cada cambio.
    const auto& adjBits = adjacency_bits(g);
    Bits curBits(nwords(n), 0);
    for (int v : selected) bit_set(curBits, v);

    for (int pass = 0; pass < cfg_.max_local_passes; ++pass) {
        bool improved = false;

        // 1. Adición (mismo orden: primer v factible en orden ascendente)
        Bits blockedBits = curBits;
        for (int v : selected)
            for (size_t k = 0; k < blockedBits.size(); ++k) blockedBits[k] |= adjBits[static_cast<size_t>(v)][k];
        for (int v = 0; v < n; ++v) {
            if (!bit_test(blockedBits, v) && ev_.compare(w[v], zero) >= 0) {
                selected.push_back(v);
                in[v] = 1;
                bit_set(curBits, v);
                cur_w = ev_.add(cur_w, w[v]);
                improved = true;
                break;
            }
        }
        if (improved) {
            std::sort(selected.begin(), selected.end());
            continue;
        }

        // 2. Intercambio 1-1 (first improvement)
        for (size_t i = 0; i < selected.size() && !improved; ++i) {
            int v = selected[i];
            // construir others
            Bits othersBits = curBits;
            bit_clear(othersBits, v);
            // base = peso de los demás = cur_w - w[v] (exacto en racional;
            // en float cambia el orden de redondeo en el último bit, sin
            // cambiar el algoritmo). O(1) sumas en vez de O(k) por cada v.
            auto base = ev_.add(cur_w, ev_.mul_int(w[v], -1));

            for (int u = 0; u < n; ++u) {
                if (in[u]) continue;
                if (ev_.compare(w[u], zero) < 0) continue;
                if (!bits_disjoint(adjBits[static_cast<size_t>(u)], othersBits)) continue;
                auto cand_w = ev_.add(base, w[u]);
                std::vector<int> candidate = selected;
                candidate[i] = u;
                std::sort(candidate.begin(), candidate.end());
                if (is_better_solution(ev_, cand_w, candidate, cur_w, selected)) {
                    selected = candidate;
                    in[v] = 0; in[u] = 1;
                    bit_clear(curBits, v); bit_set(curBits, u);
                    cur_w = cand_w;
                    improved = true;
                    break;
                }
            }
        }
        if (improved) {
            std::sort(selected.begin(), selected.end());
            continue;
        }

        // 3. Intercambio 2-1 (port fiel de Python: _local_improve paso 3).
        //    Quita dos vértices y mete uno compatible con el resto, si pesa más.
        //    First improvement, como en Python; solo se alcanza si 1-1 no mejoró.
        if (selected.size() >= 2) {
            for (size_t i = 0; i < selected.size() && !improved; ++i) {
                for (size_t j = i + 1; j < selected.size() && !improved; ++j) {
                    int a = selected[i], b = selected[j];
                    Bits othersBits = curBits;
                    bit_clear(othersBits, a);
                    bit_clear(othersBits, b);
                    // base = cur_w - w[a] - w[b], precalculada fuera del
                    // bucle sobre u: O(1) sumas por par en vez de O(k).
                    auto base = ev_.add(ev_.add(cur_w, ev_.mul_int(w[a], -1)),
                                        ev_.mul_int(w[b], -1));
                    for (int u = 0; u < n; ++u) {
                        if (in[u]) continue;
                        if (ev_.compare(w[u], zero) < 0) continue;
                        if (!bits_disjoint(adjBits[static_cast<size_t>(u)], othersBits)) continue;
                        auto cand_w = ev_.add(base, w[u]);
                        std::vector<int> next;
                        next.reserve(selected.size() - 1);
                        for (int x : selected) if (x != a && x != b) next.push_back(x);
                        next.push_back(u);
                        std::sort(next.begin(), next.end());
                        if (is_better_solution(ev_, cand_w, next, cur_w, selected)) {
                            selected = next;
                            in[a] = 0; in[b] = 0; in[u] = 1;
                            bit_clear(curBits, a); bit_clear(curBits, b); bit_set(curBits, u);
                            cur_w = cand_w;
                            improved = true;
                            break;
                        }
                    }
                }
            }
        }
        if (improved) {
            std::sort(selected.begin(), selected.end());
            continue;
        }

        // 4. Intercambio 1-2 (port fiel de Python: _local_improve paso 4).
        //    Quita un vértice y mete dos compatibles entre sí y con el resto.
        if (cfg_.enable_one_to_two && !selected.empty()) {
            for (size_t i = 0; i < selected.size() && !improved; ++i) {
                int v = selected[i];
                Bits othersBits = curBits;
                bit_clear(othersBits, v);
                // base = cur_w - w[v], precalculada fuera del bucle sobre
                // los pares (u, x): O(1) sumas por v en vez de O(k).
                auto base = ev_.add(cur_w, ev_.mul_int(w[v], -1));
                std::vector<int> compat;
                for (int u = 0; u < n; ++u) {
                    if (in[u]) continue;
                    if (ev_.compare(w[u], zero) >= 0 &&
                        bits_disjoint(adjBits[static_cast<size_t>(u)], othersBits)) compat.push_back(u);
                }
                for (size_t p = 0; p < compat.size() && !improved; ++p) {
                    for (size_t q = p + 1; q < compat.size() && !improved; ++q) {
                        int u = compat[p], x = compat[q];
                        if (bit_test(adjBits[static_cast<size_t>(u)], x)) continue;
                        auto pair_w = ev_.add(w[u], w[x]);
                        auto cand_w = ev_.add(base, pair_w);
                        std::vector<int> next;
                        next.reserve(selected.size() + 1);
                        for (int y : selected) if (y != v) next.push_back(y);
                        next.push_back(u);
                        next.push_back(x);
                        std::sort(next.begin(), next.end());
                        if (is_better_solution(ev_, cand_w, next, cur_w, selected)) {
                            selected = next;
                            in[v] = 0; in[u] = 1; in[x] = 1;
                            bit_clear(curBits, v); bit_set(curBits, u); bit_set(curBits, x);
                            cur_w = cand_w;
                            improved = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!improved) break;
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}

template <typename Evaluator>
std::vector<int>
MWISSolver<Evaluator>::perturb_and_repair(
    const Graph& g,
    const std::vector<typename Evaluator::weight_t>& w,
    const std::vector<int>& selected,
    std::mt19937& rng) const
{
    if (selected.empty())
        return greedy_construct(g, w, rng, 0.15);

    int k_drop = std::max(1, static_cast<int>(std::round(selected.size() * cfg_.drop_fraction)));
    k_drop = std::min(k_drop, static_cast<int>(selected.size()));

    std::vector<int> idx(selected.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::shuffle(idx.begin(), idx.end(), rng);

    std::vector<char> drop(selected.size(), 0);
    for (int i = 0; i < k_drop; ++i) drop[idx[i]] = 1;

    std::vector<int> cur;
    std::vector<char> blocked(g.n, 0);
    for (size_t i = 0; i < selected.size(); ++i) {
        if (!drop[i]) {
            cur.push_back(selected[i]);
            blocked[selected[i]] = 1;
            for (int u : g.adj[selected[i]]) blocked[u] = 1;
        }
    }

    std::vector<int> remaining;
    for (int v = 0; v < g.n; ++v) if (!blocked[v]) remaining.push_back(v);
    std::shuffle(remaining.begin(), remaining.end(), rng);
    // Reparación: añade vértices en el orden barajado si son compatibles
    // con el `cur` actual. Antes esto reconstruía un vector in_cur de
    // tamaño n por cada vértice (O(n²)) y tenía un bucle muerto con
    // std::find; ahora es un bitset incremental O(1) por vértice.
    // Mismas decisiones, mismo orden: solo cambia el costo.
    const auto& adjBits = adjacency_bits(g);
    Bits curBits(nwords(g.n), 0);
    for (int x : cur) bit_set(curBits, x);
    for (int v : remaining) {
        if (ev_.compare(w[v], ev_.zero()) >= 0 &&
            bits_disjoint(adjBits[static_cast<size_t>(v)], curBits)) {
            cur.push_back(v);
            bit_set(curBits, v);
        }
    }
    return local_improve(g, w, cur);
}

template <typename Evaluator>
Solution<Evaluator>
MWISSolver<Evaluator>::solve(const Graph& g)
{
    cached_graph_ = nullptr; // refrescar incluso si el llamador mutó el mismo objeto
    adj_bits_cache_.clear();
    (void)adjacency_bits(g); // construir una sola vez durante esta resolución
    std::vector<typename Evaluator::weight_t> w(g.n);
    for (int i = 0; i < g.n; ++i) w[i] = ev_.from_weight(g.weights[i]);

    std::mt19937 rng(cfg_.seed);

    Solution<Evaluator> best;
    best.weight = ev_.zero();
    best.vertices.clear();

    for (int r = 0; r < cfg_.n_restarts; ++r) {
        double jitter = (r == 0) ? 0.0 : std::min(0.35, 0.05 * r);
        auto sol = greedy_construct(g, w, rng, jitter);
        sol = local_improve(g, w, sol);
        auto wt = eval_weight(sol, w);

        if (is_better_solution(ev_, wt, sol, best.weight, best.vertices)) {
            best.vertices = sol;
            best.weight = wt;
        }

        // perturbaciones
        auto cur = sol;
        auto cur_w = wt;
        double temp = cfg_.initial_temp;
        for (int p = 0; p < cfg_.n_perturbations; ++p) {
            auto cand = perturb_and_repair(g, w, cur, rng);
            auto cand_w = eval_weight(cand, w);
            if (is_better_solution(ev_, cand_w, cand, best.weight, best.vertices)) {
                best.vertices = cand;
                best.weight = cand_w;
            }
            const auto delta_w = ev_.add(cand_w, ev_.mul_int(cur_w, -1));
            double delta = ev_.to_float(delta_w);
            if (delta >= 0.0 || (temp > 1e-9 &&
                std::generate_canonical<double, 53>(rng) < std::exp(delta / std::max(temp, 1e-12)))) {
                cur = cand;
                cur_w = cand_w;
            }
            temp *= cfg_.cooling;
        }
    }
    best.sort_vertices();
    return best;
}

// Instanciaciones explícitas
template class MWISSolver<FloatEvaluator>;
template class MWISSolver<ExactEvaluator>;

}  // namespace ce
