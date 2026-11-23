/*
 * Implement optimizer coordinate transforms, bounds and physically meaningful finite-difference scales.
 * VariableTransform, DE generation and DLS share frozen variable metadata and the selected scaling mode.
 * Preserve curvature/asphere/glass conversions and arithmetic order across forward and inverse transforms.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

int glass_choice_lower(const VariableSpec& var) {
    return static_cast<int>(std::ceil(std::min(var.lower, var.upper)));
}

int glass_choice_upper(const VariableSpec& var) {
    return static_cast<int>(std::floor(std::max(var.lower, var.upper)));
}

int clamp_glass_choice(const VariableSpec& var, double physical) {
    const int lo = glass_choice_lower(var);
    const int hi = std::max(lo, glass_choice_upper(var));
    return std::clamp(static_cast<int>(std::llround(physical)), lo, hi);
}

double scale_value(const VariableSpec& var, double value) {
    return value * scale_factor(var) + scale_offset(var);
}

double inverse_scale_value(const VariableSpec& var, double scaled_value) {
    return (scaled_value - scale_offset(var)) / scale_factor(var);
}

std::string variable_kind_name(VariableKind kind) {
    switch (kind) {
        case VariableKind::Radius:
            return "radius";
        case VariableKind::Thickness:
            return "thickness";
        case VariableKind::MaterialNd:
            return "material_nd";
        case VariableKind::MaterialVd:
            return "material_vd";
        case VariableKind::VignetteX:
            return "vignette_x";
        case VariableKind::VignetteY:
            return "vignette_y";
        case VariableKind::VignetteCenterY:
            return "vignette_center_y";
        case VariableKind::VignetteMagnitudeY:
            return "vignette_magnitude_y";
        case VariableKind::VignetteAsymmetryY:
            return "vignette_asymmetry_y";
        case VariableKind::Conic:
            return "conic";
        case VariableKind::AsphereCoeff:
            return "asphere_coeff";
        case VariableKind::Curvature:
            return "curvature";
        case VariableKind::GlassChoice:
            return "glass_choice";
    }
    return "unknown";
}

double asphere_reference_radius(const LensSystem& system, const VariableSpec& var) {
    if (std::isfinite(var.asphere_reference_radius) && var.asphere_reference_radius > 0.0) {
        return std::max(std::abs(var.asphere_reference_radius), 1.0e-12);
    }
    double radius = 0.0;
    if (var.index < system.surfaces.size()) {
        const auto& surface = system.surfaces[var.index];
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
    return std::max(std::abs(radius), 1.0);
}

bool uses_bound_normalized_coordinates(InternalScalingMode mode) {
    return mode == InternalScalingMode::BoundNormalized ||
           mode == InternalScalingMode::EdgeSagNormalized;
}

double frozen_asphere_radial_power(const VariableSpec& var) {
    if (var.kind != VariableKind::AsphereCoeff ||
        !std::isfinite(var.asphere_reference_radius) ||
        var.asphere_reference_radius <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double radius = std::max(std::abs(var.asphere_reference_radius), 1.0e-12);
    const int order = 2 * static_cast<int>(var.term + 1);
    double power = 1.0;
    for (int i = 0; i < order; ++i) {
        if (power > std::numeric_limits<double>::max() / radius) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        power *= radius;
    }
    return power;
}

bool has_edge_sag_transform(const VariableSpec& var, InternalScalingMode mode) {
    if (mode != InternalScalingMode::EdgeSagNormalized) {
        return false;
    }
    const double power = frozen_asphere_radial_power(var);
    return std::isfinite(power) && power > 0.0 &&
           std::isfinite(var.edge_sag_lower) &&
           std::isfinite(var.edge_sag_upper) &&
           var.edge_sag_upper > var.edge_sag_lower;
}

double asphere_sag_physical_step(const LensSystem& system, const VariableSpec& var) {
    const double radius = asphere_reference_radius(system, var);
    const int order = 2 * static_cast<int>(var.term + 1);
    double radial_power = 1.0;
    for (int i = 0; i < order; ++i) {
        if (radial_power > std::numeric_limits<double>::max() / radius) {
            radial_power = std::numeric_limits<double>::infinity();
            break;
        }
        radial_power *= radius;
    }

    constexpr double kAsphereSagProbe = 1.0e-5;
    if (std::isfinite(radial_power) && radial_power > 0.0) {
        return kAsphereSagProbe / std::max(radial_power, 1.0);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double default_physical_finite_difference_step(
    const VariableSpec& var,
    double current_physical,
    double finite_difference) {
    const double span = std::max(var.upper - var.lower, 0.0);
    const double reference = std::max({span, std::abs(current_physical), 1.0});
    return std::max(std::abs(finite_difference) * reference, 1.0e-8);
}

double finite_difference_physical_step(
    const LensSystem& system,
    const VariableSpec& var,
    double current_physical,
    double finite_difference) {
    const double fd_scale =
        (std::isfinite(finite_difference) && std::abs(finite_difference) > 0.0)
            ? std::abs(finite_difference) / 1.0e-4
            : 1.0;
    auto scaled_step = [&](double base_step, double min_step = 1.0e-12) {
        return std::max(base_step * fd_scale, min_step);
    };
    switch (var.kind) {
        case VariableKind::MaterialNd:
            return scaled_step(1.0e-4);
        case VariableKind::MaterialVd:
            return scaled_step(0.25);
        case VariableKind::Conic:
            return scaled_step(1.0e-2);
        case VariableKind::Thickness:
            return scaled_step(1.0e-3);
        case VariableKind::Curvature:
            return scaled_step(1.0e-5);
        case VariableKind::VignetteX:
        case VariableKind::VignetteY:
        case VariableKind::VignetteCenterY:
        case VariableKind::VignetteMagnitudeY:
        case VariableKind::VignetteAsymmetryY:
            return scaled_step(1.0e-4);
        case VariableKind::AsphereCoeff: {
            const double step = asphere_sag_physical_step(system, var);
            if (std::isfinite(step) && step > 0.0) {
                return scaled_step(step);
            }
            break;
        }
        default:
            break;
    }
    return default_physical_finite_difference_step(var, current_physical, finite_difference);
}

double finite_difference_step_scaled(
    const LensSystem& system,
    const VariableSpec& var,
    double current_scaled,
    double lower_scaled,
    double upper_scaled,
    double finite_difference,
    InternalScalingMode mode ,
    double* physical_step_out ) {
    const double span = std::max(upper_scaled - lower_scaled, 0.0);
    const double current_physical = internal_to_physical(var, current_scaled, mode);
    double physical_step = finite_difference_physical_step(
        system,
        var,
        current_physical,
        finite_difference);
    double h = std::abs(physical_delta_to_internal(var, physical_step, mode));
    if (!std::isfinite(h) || h <= 0.0) {
        h = std::abs(finite_difference) * std::max(span, 1.0);
        const double unit_internal = std::abs(physical_delta_to_internal(var, 1.0, mode));
        physical_step = h / std::max(unit_internal, 1.0e-300);
    }

    const double min_h = 16.0 * std::numeric_limits<double>::epsilon() *
        std::max({std::abs(current_scaled), std::abs(lower_scaled), std::abs(upper_scaled), 1.0});
    h = std::max(h, min_h);
    if (span > 0.0) {
        h = std::min(h, span * 0.25);
    }
    if (physical_step_out) {
        const double unit_internal = std::abs(physical_delta_to_internal(var, 1.0, mode));
        *physical_step_out = h / std::max(unit_internal, 1.0e-300);
    }
    return h;
}

std::pair<double, double> scaled_bounds(const VariableSpec& var) {
    double lo = scale_value(var, var.lower);
    double hi = scale_value(var, var.upper);
    if (lo > hi) {
        std::swap(lo, hi);
    }
    return {lo, hi};
}

std::pair<double, double> internal_bounds(
    const VariableSpec& var,
    InternalScalingMode mode ) {
    if (uses_bound_normalized_coordinates(mode)) {
        return {0.0, 1.0};
    }
    return scaled_bounds(var);
}

double internal_to_physical(
    const VariableSpec& var,
    double internal,
    InternalScalingMode mode ) {
    if (is_glass_choice_variable(var)) {
        if (uses_bound_normalized_coordinates(mode)) {
            const double span = static_cast<double>(glass_choice_upper(var) - glass_choice_lower(var));
            if (!std::isfinite(span) || span <= 0.0) {
                return static_cast<double>(clamp_glass_choice(var, var.initial));
            }
            return static_cast<double>(clamp_glass_choice(
                var,
                static_cast<double>(glass_choice_lower(var)) +
                    std::clamp(internal, 0.0, 1.0) * span));
        }
        const auto [lo, hi] = scaled_bounds(var);
        return static_cast<double>(clamp_glass_choice(
            var,
            inverse_scale_value(var, std::clamp(internal, lo, hi))));
    }
    if (has_edge_sag_transform(var, mode)) {
        const double power = frozen_asphere_radial_power(var);
        const double coordinate = internal;
        const double sag = var.edge_sag_lower + coordinate *
            (var.edge_sag_upper - var.edge_sag_lower);
        return project_variable_value(
            var,
            maybe_clip_continuous(var, sag / power, var.lower, var.upper));
    }
    if (uses_bound_normalized_coordinates(mode)) {
        const double span = var.upper - var.lower;
        if (!std::isfinite(span) || span <= 0.0) {
            return project_variable_value(
                var,
                maybe_clip_continuous(var, var.initial, var.lower, var.upper));
        }
        const double coordinate = internal;
        return project_variable_value(
            var,
            maybe_clip_continuous(
                var,
                var.lower + coordinate * span,
                var.lower,
                var.upper));
    }
    const auto [lo, hi] = scaled_bounds(var);
    const double coordinate = maybe_clip_continuous(var, internal, lo, hi);
    return project_variable_value(
        var,
        maybe_clip_continuous(
            var,
            inverse_scale_value(var, coordinate),
            var.lower,
            var.upper));
}

double physical_to_internal(
    const VariableSpec& var,
    double physical,
    InternalScalingMode mode ) {
    physical = project_variable_value(var, physical);
    if (is_glass_choice_variable(var)) {
        physical = static_cast<double>(clamp_glass_choice(var, physical));
    }
    if (has_edge_sag_transform(var, mode)) {
        const double power = frozen_asphere_radial_power(var);
        const double sag_span = var.edge_sag_upper - var.edge_sag_lower;
        const double coordinate =
            (physical * power - var.edge_sag_lower) / sag_span;
        return maybe_clip_continuous(var, coordinate, 0.0, 1.0);
    }
    if (uses_bound_normalized_coordinates(mode)) {
        const double span = var.upper - var.lower;
        if (!std::isfinite(span) || span <= 0.0) {
            return 0.5;
        }
        return maybe_clip_continuous(
            var,
            (physical - var.lower) / span,
            0.0,
            1.0);
    }
    const auto [lo, hi] = scaled_bounds(var);
    return maybe_clip_continuous(var, scale_value(var, physical), lo, hi);
}

double physical_delta_to_internal(
    const VariableSpec& var,
    double physical_delta,
    InternalScalingMode mode ) {
    if (is_glass_choice_variable(var)) {
        return 0.0;
    }
    if (has_edge_sag_transform(var, mode)) {
        const double power = frozen_asphere_radial_power(var);
        const double sag_span = var.edge_sag_upper - var.edge_sag_lower;
        return physical_delta * power / sag_span;
    }
    if (uses_bound_normalized_coordinates(mode)) {
        const double span = var.upper - var.lower;
        if (!std::isfinite(span) || span <= 0.0) {
            return 0.0;
        }
        return physical_delta / span;
    }
    return physical_delta * scale_factor(var);
}

}  // namespace optimizer_detail

}  // namespace optiland
