#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double mutate_glass_choice_internal(
    const VariableSpec& var,
    double target_internal,
    double guide_internal,
    const DifferentialEvolutionOptions& options,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

} }  // namespace optiland::optimizer_detail
