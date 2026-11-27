#pragma once
/**
 * Solución MWIS: vector de vértices siempre ordenado.
 * La comparación lexicográfica es entonces O(k) sin costo de ordenamiento.
 */

#include <algorithm>
#include <vector>
#include "evaluator.hpp"

namespace ce {

template <typename Evaluator>
struct Solution {
    std::vector<int> vertices;          // siempre ordenado
    typename Evaluator::weight_t weight;
    std::string method = "cpp-multi-start";

    void sort_vertices() {
        std::sort(vertices.begin(), vertices.end());
    }
};

/**
 * Regla de desempate idéntica a Python (docs/TIEBREAK.md):
 * 1. mayor peso
 * 2. mayor cardinalidad
 * 3. orden lexicográfico del vector de vértices (ya ordenado)
 */
template <typename Evaluator>
bool is_better_solution(
    const Evaluator& ev,
    const typename Evaluator::weight_t& wa, const std::vector<int>& va,
    const typename Evaluator::weight_t& wb, const std::vector<int>& vb)
{
    int cmp = ev.compare(wa, wb);
    if (cmp > 0) return true;
    if (cmp < 0) return false;
    if (va.size() > vb.size()) return true;
    if (va.size() < vb.size()) return false;
    // ambos vectores se asumen ordenados
    return std::lexicographical_compare(va.begin(), va.end(), vb.begin(), vb.end());
}

}  // namespace ce
