#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<double> scaled_population_to_physical_flat(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& scaled_population,
    int population_size,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

std::vector<double> initial_physical_values(const std::vector<VariableSpec>& variables);

void inject_flat_start_curvature_seeds(
    const std::vector<VariableSpec>& variables,
    std::vector<double>& population,
    int population_size,
    const std::vector<double>& initial_scaled,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

void inject_physical_population_seeds(
    const std::vector<VariableSpec>& variables,
    std::vector<double>& population,
    int population_size,
    const std::vector<std::vector<double>>& seed_values,
    int start_row,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

std::vector<double> make_latin_hypercube_population(
    const std::vector<VariableSpec>& variables,
    int population_size,
    std::mt19937& rng,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

std::vector<double> make_initial_de_population(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    int population_size,
    const PopulationGenerationOptions& generation,
    std::mt19937& rng,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

void attach_final_population(
    OptimizationResult& result,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& scaled_population,
    int population_size,
    const std::vector<double>& merits,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

std::string normalized_strategy(std::string strategy);

} }  // namespace optiland::optimizer_detail
