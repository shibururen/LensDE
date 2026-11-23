#pragma once
#include "optiland/core.hpp"
#include "optiland/optimizer/fitness.hpp"
#include "optiland/optimizer/basins.hpp"
#include "optiland/optimizer/timing.hpp"
#include <vector>

namespace optiland {
struct OptimizationResult {
    std::vector<double> best_values;
    Score best_score;
    int generations = 0;
    bool used_cuda = false;
    double de_seconds = 0.0;
    double polish_seconds = 0.0;
    int polish_runs = 0;
    int polish_improvements = 0;
    std::vector<double> final_population_values;
    std::vector<double> final_population_merits;
    int final_population_size = 0;
    int final_population_dim = 0;
    std::vector<CandidateFitness> final_population_fitness;
    std::vector<BasinElite> basin_elites;
    DeTimingStats timing;
};


}  // namespace optiland
