#pragma once
/**
 * Representación de grafo: listas de adyacencia ordenadas (std::vector<int>).
 * Compatible con el contrato Python y con desempate lexicográfico.
 */

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace ce {

class Graph {
public:
    int n = 0;
    std::vector<std::vector<int>> adj;   // listas ordenadas, sin duplicados
    std::vector<double> weights;         // pesos de entrada (se convierten en el evaluador)

    Graph() = default;

    Graph(int n_, const std::vector<std::pair<int,int>>& edges,
          const std::vector<double>& weights_)
        : n(n_), weights(weights_)
    {
        if (n_ <= 0) throw std::invalid_argument("n must be positive");
        if (static_cast<int>(weights_.size()) != n_)
            throw std::invalid_argument("weights size != n");
        for (double w : weights_)
            if (!std::isfinite(w)) throw std::invalid_argument("weights must be finite");
        adj.resize(static_cast<size_t>(n_));
        for (auto [u, v] : edges) {
            if (u < 0 || v < 0 || u >= n_ || v >= n_)
                throw std::invalid_argument("edge endpoint out of range");
            if (u == v) continue;
            adj[u].push_back(v);
            adj[v].push_back(u);
        }
        for (int i = 0; i < n_; ++i) {
            auto& a = adj[i];
            std::sort(a.begin(), a.end());
            a.erase(std::unique(a.begin(), a.end()), a.end());
        }
    }

    int degree(int v) const { return static_cast<int>(adj[v].size()); }

    bool is_independent(const std::vector<int>& vertices) const {
        std::vector<char> in(n, 0);
        for (int v : vertices) in[v] = 1;
        for (int v : vertices)
            for (int u : adj[v])
                if (in[u]) return false;
        return true;
    }
};

}  // namespace ce
