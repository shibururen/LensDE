#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

bool solve_correlated_sag_system(
    std::array<std::array<double, 7>, 7> matrix,
    std::array<double, 7> values,
    std::array<double, 7>* solution,
    int dimension);

std::vector<double> make_single_surface_correlated_sag_generation_row(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& parent_scaled,
    InternalScalingMode mode,
    double sag_perturbation_um,
    double correlation_length,
    std::mt19937& rng,
    std::size_t* selected_surface_output = nullptr);

std::vector<double> make_single_block_refresh_row(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& parent_scaled,
    const PopulationGenerationOptions& options,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni);

std::vector<double> make_coupled_surface_refresh_row(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& parent_scaled,
    const PopulationGenerationOptions& options,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni);

} }  // namespace optiland::optimizer_detail
