/*
 * Implement the public VariableTransform adapter over shared optimizer scaling functions.
 * Callers supply one variable specification and scaling mode for coordinate, bounds and step conversion.
 * Keep this adapter consistent with the same transforms used internally by DE and DLS.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

double VariableTransform::to_internal(double physical) const {
    return physical_to_internal(var, physical, mode);
}

double VariableTransform::to_physical(double internal) const {
    return internal_to_physical(var, internal, mode);
}

std::pair<double, double> VariableTransform::internal_bounds() const {
    return optimizer_detail::internal_bounds(var, mode);
}

double VariableTransform::physical_delta_to_internal(double physical_delta) const {
    return optimizer_detail::physical_delta_to_internal(var, physical_delta, mode);
}

}  // namespace optiland
