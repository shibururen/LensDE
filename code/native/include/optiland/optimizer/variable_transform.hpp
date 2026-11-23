#pragma once
#include "optiland/optimizer/scaling.hpp"
#include "optiland/core.hpp"
#include <utility>

namespace optiland {
struct VariableTransform {
    InternalScalingMode mode = InternalScalingMode::LegacyOptical;
    VariableSpec var;

    double to_internal(double physical) const;
    double to_physical(double internal) const;
    std::pair<double, double> internal_bounds() const;
    double physical_delta_to_internal(double physical_delta) const;
};


}  // namespace optiland
