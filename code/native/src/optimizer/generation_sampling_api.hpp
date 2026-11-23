#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double bounded_positive(double value, double fallback);

double restricted_generation_span(
    const LensSystem& system,
    const VariableSpec& var,
    double center_physical,
    const PopulationGenerationOptions& options);

double sample_restricted_physical_value(
    const LensSystem& system,
    const VariableSpec& var,
    double center_physical,
    const PopulationGenerationOptions& options,
    double span_scale,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni);

std::vector<double> make_sag_geometry_generation_row(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& parent_scaled,
    InternalScalingMode mode,
    double sag_perturbation_um,
    double geometry_fraction,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni);

} }  // namespace optiland::optimizer_detail
