#pragma once
#include "interfaces.hpp"

namespace lensde::broker {

inline double post_asphere_reference_radius(
    const optiland::LensSystem& system,
    const optiland::VariableSpec& variable) {
    double radius = 0.0;
    if (variable.index < system.surfaces.size()) {
        const auto& surface = system.surfaces[variable.index];
        for (const double candidate : {
                 surface.semi_aperture,
                 surface.aperture_radius,
                 surface.mechanical_aperture}) {
            if (std::isfinite(candidate) && std::abs(candidate) > 0.0) {
                radius = std::abs(candidate);
                break;
            }
        }
    }
    if (!std::isfinite(radius) || radius <= 0.0) {
        radius = std::abs(system.entrance_pupil_radius());
    }
    if (!std::isfinite(radius) || radius <= 0.0) {
        radius = 1.0;
    }
    return std::max(radius, 1.0e-12);
}

inline double post_asphere_radial_power(double radius, std::size_t term) {
    const int order = 2 * static_cast<int>(term + 1);
    double power = 1.0;
    for (int i = 0; i < order; ++i) {
        if (power > std::numeric_limits<double>::max() / radius) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        power *= radius;
    }
    return power;
}

inline int rebase_post_asphere_edge_sag_variables(
    const optiland::LensSystem& system,
    std::vector<optiland::VariableSpec>& variables) {
    int rebased = 0;
    for (auto& variable : variables) {
        if (variable.kind != optiland::VariableKind::AsphereCoeff) {
            continue;
        }
        const double new_radius = post_asphere_reference_radius(system, variable);
        const double new_power = post_asphere_radial_power(new_radius, variable.term);
        if (!std::isfinite(new_power) || new_power <= 0.0) {
            continue;
        }
        double sag_lower = variable.edge_sag_lower;
        double sag_upper = variable.edge_sag_upper;
        if (!std::isfinite(sag_lower) || !std::isfinite(sag_upper) ||
            sag_lower >= sag_upper) {
            const double old_radius =
                std::isfinite(variable.asphere_reference_radius) &&
                    variable.asphere_reference_radius > 0.0
                    ? variable.asphere_reference_radius
                    : new_radius;
            const double old_power =
                post_asphere_radial_power(old_radius, variable.term);
            if (!std::isfinite(old_power) || old_power <= 0.0) {
                continue;
            }
            sag_lower = variable.lower * old_power;
            sag_upper = variable.upper * old_power;
        }
        double current = variable.initial;
        if (variable.index < system.surfaces.size()) {
            const auto& coefficients =
                system.surfaces[variable.index].asphere_coefficients;
            current = variable.term < coefficients.size()
                ? coefficients[variable.term]
                : 0.0;
        }
        const double current_sag = std::isfinite(current)
            ? current * new_power
            : 0.0;
        const double sag_bound = std::max({
            std::abs(sag_lower),
            std::abs(sag_upper),
            1.2 * std::abs(current_sag),
            1.0e-15});
        variable.asphere_reference_radius = new_radius;
        variable.edge_sag_lower = -sag_bound;
        variable.edge_sag_upper = sag_bound;
        variable.lower = -sag_bound / new_power;
        variable.upper = sag_bound / new_power;
        variable.initial = std::clamp(current, variable.lower, variable.upper);
        ++rebased;
    }
    return rebased;
}
} // namespace lensde::broker
