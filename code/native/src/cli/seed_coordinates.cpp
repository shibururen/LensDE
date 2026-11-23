// Compute seed-selection coordinates, normalized row keys, and curvature-activity masks.
// The seed selector uses these shared geometry conventions to compare and partition saved rows.
// Preserve asphere reference scaling, activity tolerances, and key rounding because they affect selection.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

bool seed_asphere_q_coordinate_cpp(
    const VariableSpec& variable,
    double* radial_power ) {
    if (variable.kind != VariableKind::AsphereCoeff ||
        !std::isfinite(variable.asphere_reference_radius) ||
        variable.asphere_reference_radius <= 0.0 ||
        !std::isfinite(variable.edge_sag_lower) ||
        !std::isfinite(variable.edge_sag_upper) ||
        variable.edge_sag_upper <= variable.edge_sag_lower) {
        return false;
    }
    const double power = asphere_radial_power(
        variable.asphere_reference_radius,
        variable.term);
    if (!std::isfinite(power) || power <= 0.0) {
        return false;
    }
    if (radial_power != nullptr) {
        *radial_power = power;
    }
    return true;
}

double seed_axis_coordinate_cpp(double physical, const VariableSpec& variable) {
    const double value = std::isfinite(physical) ? physical : 0.0;
    double radial_power = 1.0;
    if (!seed_asphere_q_coordinate_cpp(variable, &radial_power)) {
        return value;
    }
    const double q = value * radial_power;
    if (std::isfinite(q)) {
        return q;
    }
    return std::copysign(std::numeric_limits<double>::max(), value);
}

std::pair<double, double> seed_axis_bounds_cpp(const VariableSpec& variable) {
    if (seed_asphere_q_coordinate_cpp(variable)) {
        return {variable.edge_sag_lower, variable.edge_sag_upper};
    }
    return {variable.lower, variable.upper};
}

double seed_axis_span_cpp(const VariableSpec& variable) {
    const auto bounds = seed_axis_bounds_cpp(variable);
    return std::max(1.0e-12, std::abs(bounds.second - bounds.first));
}

double seed_axis_normalized_cpp(double physical, const VariableSpec& variable) {
    const auto bounds = seed_axis_bounds_cpp(variable);
    return (seed_axis_coordinate_cpp(physical, variable) - bounds.first) /
           seed_axis_span_cpp(variable);
}

std::string seed_row_key_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(12);
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i > 0) {
            out << '|';
        }
        const double value = i < variables.size()
                                 ? seed_axis_coordinate_cpp(row[i], variables[i])
                                 : (std::isfinite(row[i]) ? row[i] : 0.0);
        out << value;
    }
    return out.str();
}

double seed_activity_tolerance_cpp(const VariableSpec& variable) {
    const double span = seed_axis_span_cpp(variable);
    if (variable.kind == VariableKind::Curvature) {
        return std::max(2.0e-5, 1.0e-3 * span);
    }
    return std::max(1.0e-9, 1.0e-4 * span);
}

std::string row_curvature_mask_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables,
    double tol ) {
    std::string mask;
    for (std::size_t i = 0; i < variables.size() && i < row.size(); ++i) {
        if (variables[i].kind != VariableKind::Curvature) {
            continue;
        }
        const double value = std::isfinite(row[i]) ? row[i] : 0.0;
        const double threshold = tol >= 0.0 ? tol : seed_activity_tolerance_cpp(variables[i]);
        mask.push_back(value > threshold ? 'p' : (value < -threshold ? 'n' : 'z'));
    }
    return mask;
}

int active_count_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables,
    double tol ) {
    int count = 0;
    for (std::size_t i = 0; i < variables.size() && i < row.size(); ++i) {
        const double value = seed_axis_coordinate_cpp(row[i], variables[i]);
        const double initial = seed_axis_coordinate_cpp(variables[i].initial, variables[i]);
        const double threshold = tol >= 0.0 ? tol : seed_activity_tolerance_cpp(variables[i]);
        if (std::abs(value - initial) > threshold) {
            ++count;
        }
    }
    return count;
}

std::vector<int> active_indices_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables,
    double tol ) {
    std::vector<int> indices;
    for (std::size_t i = 0; i < variables.size() && i < row.size(); ++i) {
        const double value = seed_axis_coordinate_cpp(row[i], variables[i]);
        const double initial = seed_axis_coordinate_cpp(variables[i].initial, variables[i]);
        const double threshold = tol >= 0.0 ? tol : seed_activity_tolerance_cpp(variables[i]);
        if (std::abs(value - initial) > threshold) {
            indices.push_back(static_cast<int>(i));
        }
    }
    return indices;
}

int active_curvature_count_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables) {
    int count = 0;
    for (std::size_t i = 0; i < variables.size() && i < row.size(); ++i) {
        if (variables[i].kind != VariableKind::Curvature) {
            continue;
        }
        const double value = std::isfinite(row[i]) ? row[i] : 0.0;
        if (std::abs(value - variables[i].initial) > seed_activity_tolerance_cpp(variables[i])) {
            ++count;
        }
    }
    return count;
}

} } }
