#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<double> make_population_generation_rows(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& population,
    int dim,
    const std::vector<int>& parent_indices,
    int row_count,
    const PopulationGenerationOptions& options,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni);

int population_generation_candidate_limit(
    const PopulationGenerationOptions& options,
    int target_count);

bool population_generation_time_exhausted(
    Clock::time_point start,
    const PopulationGenerationOptions& options);

bool score_trace_feasible_for_generation(const Score& score, const MeritConfig& config);

bool polish_score_trace_acceptable(
    const Score& score,
    const MeritConfig& config,
    const PolishOptions& options);

} }  // namespace optiland::optimizer_detail
