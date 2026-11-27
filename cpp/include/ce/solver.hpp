# pragma once
# include <random>
# include <cstdint>
# include <vector>
# include "graph.hpp"
# include "solution.hpp"
# include "evaluator.hpp"

namespace ce {

struct SolverConfig {
    int n_restarts       = 8;
    int n_perturbations  = 10;
    double drop_fraction = 0.25;
    double initial_temp  = 0.05;
    double cooling       = 0.85;
    bool enable_one_to_two = true;
    int max_local_passes = 40;
    uint32_t seed        = 42;
};

template <typename Evaluator>
class MWISSolver {
public:
    explicit MWISSolver(Evaluator ev = Evaluator{}, SolverConfig cfg = {})
        : ev_(std::move(ev)), cfg_(cfg) {}

    Solution<Evaluator> solve(const Graph& g);

private:
    Evaluator ev_;
    SolverConfig cfg_;
    // Cache por instancia: evita reconstruir la matriz de bitsets en cada
    // reinicio y perturbación. El grafo se trata como inmutable durante solve.
    mutable const Graph* cached_graph_ = nullptr;
    mutable std::vector<std::vector<uint64_t>> adj_bits_cache_;
    const std::vector<std::vector<uint64_t>>& adjacency_bits(const Graph& g) const;

    using weight_t = typename Evaluator::weight_t;

    weight_t eval_weight(const std::vector<int>& selected,
                         const std::vector<weight_t>& w) const;

    std::vector<int> greedy_construct(
        const Graph& g,
        const std::vector<weight_t>& w,
        std::mt19937& rng,
        double jitter) const;

    std::vector<int> local_improve(
        const Graph& g,
        const std::vector<weight_t>& w,
        std::vector<int> selected) const;

    std::vector<int> perturb_and_repair(
        const Graph& g,
        const std::vector<weight_t>& w,
        const std::vector<int>& selected,
        std::mt19937& rng) const;
};

}  // namespace ce
