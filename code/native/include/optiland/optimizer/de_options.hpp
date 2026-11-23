#pragma once
#include "optiland/optimizer/generation.hpp"
#include "optiland/optimizer/scaling.hpp"
#include <string>
#include <vector>

namespace optiland {
struct DifferentialEvolutionOptions {
    int max_generations = 64;
    // Legacy absolute population size. When <= 0, SciPy-style popsize * dim is used.
    int population_size = 256;
    int popsize_multiplier = 15;
    bool use_popsize_multiplier = false;
    double mutation_min = 0.5;
    double mutation_max = 1.0;
    double recombination = 0.7;
    double tolerance = 0.01;
    double absolute_tolerance = 0.0;
    std::string strategy = "best1bin";
    unsigned int seed = 1;
    bool use_cuda = true;
    // 0 keeps the environment/default adaptive-DE CUDA batch threshold.
    int cuda_min_batch_size = 0;
    int progress_every = 10;
    std::vector<std::vector<double>> initial_population_values;
    PopulationGenerationOptions initial_generation;
    InternalScalingMode internal_scaling = InternalScalingMode::LegacyOptical;
    double glass_mutation_rate = 0.35;
    double glass_random_rate = 0.05;
    double glass_pbest_inherit_rate = 0.35;
    int glass_neighbor_count = 12;
    bool glass_local_search = true;
};


}  // namespace optiland
