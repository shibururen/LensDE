/*
 * Interpret merit contributions as normalized violations and identify constraint/image-quality categories.
 * Candidate fitness and DLS residual construction share these tolerance and classification rules.
 * Preserve bound direction, reference values and finite-value fallbacks when interpreting a contribution.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double contribution_delta(const MeritContribution& c) {
    const std::string mode = normalized_token(c.mode);
    if (mode == "penalty" || mode == "fail" || mode == "required") {
        const double weight = std::max(1.0e-12, std::isfinite(c.weight) ? std::abs(c.weight) : 0.0);
        return std::sqrt(std::max(0.0, c.penalty) / weight);
    }
    if (!std::isfinite(c.value)) {
        return 1.0e5;
    }
    const bool has_target = std::isfinite(c.target);
    if (mode == "minimize" || mode == "minimum") {
        return c.value;
    }
    if (mode == "less" || mode == "less_than" || mode == "upper" ||
        mode == "max" || mode == "le" || mode == "lte") {
        return std::isfinite(c.upper) ? std::max(0.0, c.value - c.upper) : 0.0;
    }
    if (mode == "greater" || mode == "greater_than" || mode == "lower" ||
        mode == "min" || mode == "ge" || mode == "gte") {
        return std::isfinite(c.lower) ? std::max(0.0, c.lower - c.value) : 0.0;
    }
    if (mode == "range" || mode == "between" || mode == "interval") {
        if (std::isfinite(c.lower) && c.value < c.lower) {
            return c.lower - c.value;
        }
        if (std::isfinite(c.upper) && c.value > c.upper) {
            return c.value - c.upper;
        }
        return 0.0;
    }
    if (has_target) {
        return c.value - c.target;
    }
    return std::sqrt(std::max(0.0, c.penalty));
}

double contribution_reference_for_quality(const MeritContribution& c, double fallback) {
    if (std::isfinite(c.target) && std::abs(c.target) > kEps) {
        return std::abs(c.target);
    }
    if (std::isfinite(c.lower) && std::abs(c.lower) > kEps) {
        return std::abs(c.lower);
    }
    if (std::isfinite(c.upper) && std::abs(c.upper) > kEps) {
        return std::abs(c.upper);
    }
    if (std::isfinite(c.value) && std::abs(c.value) > kEps) {
        return std::abs(c.value);
    }
    return fallback;
}

bool is_edge_like_manufacturing_constraint_token(const std::string& name) {
    return name == "min_edge" ||
           name == "minimum_edge_thickness" ||
           name.find("edge") != std::string::npos ||
           name.find("atc") != std::string::npos ||
           name.find("ate") != std::string::npos ||
           name.find("bl0") != std::string::npos ||
           name.find("blocking_ratio") != std::string::npos ||
           name.find("od_ratio") != std::string::npos;
}

bool is_ratio_like_manufacturing_constraint_token(const std::string& name) {
    return name.find("atc") != std::string::npos ||
           name.find("ate") != std::string::npos ||
           name.find("bl0") != std::string::npos ||
           name.find("blocking_ratio") != std::string::npos ||
           name.find("od_ratio") != std::string::npos;
}

bool is_surface_tangent_angle_constraint_token(const std::string& name) {
    return name.find("surface_tangent") != std::string::npos ||
           name.find("tangent_axis_angle") != std::string::npos;
}

double quality_tolerance_for_contribution(const MeritContribution& c, const MeritConfig& config) {
    const std::string name = normalized_token(c.name);
    const double abs_floor = std::max(1.0e-9, config.quality_length_abs_tol);
    if (name == "efl" || name == "effective_focal_length") {
        const double ref = contribution_reference_for_quality(c, 1.0);
        return std::max(abs_floor, std::abs(config.quality_efl_rel_tol) * ref);
    }
    if (name == "ttl" || name == "total_track" || name == "track") {
        const double ref = contribution_reference_for_quality(c, 1.0);
        return std::max(abs_floor, std::abs(config.quality_ttl_rel_tol) * ref);
    }
    if (name == "bfl" || name == "back_focal_length") {
        const double ref = contribution_reference_for_quality(c, 1.0);
        return std::max(abs_floor, std::abs(config.quality_bfl_rel_tol) * ref);
    }
    if (is_ratio_like_manufacturing_constraint_token(name)) {
        return std::max(1.0e-9, config.quality_ratio_tol);
    }
    if (is_surface_tangent_angle_constraint_token(name)) {
        return std::max(
            1.0e-9,
            std::abs(config.quality_surface_tangent_angle_tol_deg));
    }
    if (is_edge_like_manufacturing_constraint_token(name) ||
        name.find("thickness") != std::string::npos) {
        return std::max(1.0e-9, config.quality_edge_tol);
    }
    if (name.find("distortion") != std::string::npos) {
        return std::max(1.0e-9, config.quality_distortion_tol);
    }
    if (name.find("wavefront") != std::string::npos ||
        name.find("opd") != std::string::npos) {
        const double wavefront_scale =
            std::isfinite(config.quality_wavefront_scale) &&
                    config.quality_wavefront_scale > 0.0
                ? config.quality_wavefront_scale
                : MeritConfig{}.quality_wavefront_scale;
        return std::max(1.0e-9, wavefront_scale);
    }
    if (name == "cra" || name == "max_cra" ||
        name.find("chief_ray_angle") != std::string::npos ||
        name.find("chief_ray_incidence_angle") != std::string::npos) {
        return std::max(1.0e-9, config.quality_chief_ray_angle_tol);
    }
    if (name == "tca" || name == "lca" ||
        name.find("lateral_color") != std::string::npos ||
        name.find("lateral_chromatic") != std::string::npos ||
        name.find("transverse_chromatic") != std::string::npos) {
        return std::max(1.0e-9, config.quality_lateral_color_tol);
    }
    if (name == "spectral_spot_spread" ||
        name == "spectral_rms_spread" ||
        name == "wavelength_spot_spread" ||
        name == "chromatic_spot_spread") {
        return std::max(1.0e-9, config.quality_spectral_spot_spread_tol);
    }
    if (name.find("axial_color") != std::string::npos) {
        return std::max(1.0e-9, config.quality_axial_color_tol);
    }
    return std::max(1.0e-9, config.quality_bound_tol);
}

double finite_or(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

double positive_square(double value) {
    const double v = std::max(0.0, finite_or(value, 0.0));
    return v * v;
}

bool contribution_name_contains(const MeritContribution& c, const std::vector<std::string>& tokens) {
    const std::string name = normalized_token(c.name);
    const std::string source = normalized_token(c.source);
    for (const auto& token : tokens) {
        if (name.find(token) != std::string::npos || source.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

double normalized_contribution_violation(
    const MeritContribution& c,
    const MeritConfig& config) {
    if (std::isfinite(c.normalized_residual)) {
        return std::max(0.0, std::abs(c.normalized_residual));
    }
    double tolerance = std::isfinite(c.tolerance) && std::abs(c.tolerance) > 1.0e-12
                           ? std::abs(c.tolerance)
                           : quality_tolerance_for_contribution(c, config);
    tolerance = std::max(1.0e-12, tolerance);
    const double delta = contribution_delta(c);
    if (!std::isfinite(delta)) {
        return 1.0e6;
    }
    return std::max(0.0, std::abs(delta) / tolerance);
}

bool is_constraint_like_contribution(const MeritContribution& c) {
    if (c.category == MeritCategory::Constraint) {
        return true;
    }
    if (c.category == MeritCategory::FirstOrder ||
        c.category == MeritCategory::ImageQuality ||
        c.category == MeritCategory::Regularization) {
        return false;
    }
    return contribution_name_contains(
        c,
        {"constraint", "bound", "edge", "thickness", "air_gap", "gap",
         "aperture", "radius", "curvature", "ttl", "bfl",
         "atc", "ate", "bl0", "blocking_ratio", "od_ratio",
         "surface_tangent", "tangent_axis_angle"});
}

bool contribution_mode_is_bound_like(const MeritContribution& c) {
    const std::string mode = normalized_token(c.mode);
    return mode == "less" ||
           mode == "less_than" ||
           mode == "upper" ||
           mode == "max" ||
           mode == "le" ||
           mode == "lte" ||
           mode == "greater" ||
           mode == "greater_than" ||
           mode == "lower" ||
           mode == "min" ||
           mode == "ge" ||
           mode == "gte" ||
           mode == "range" ||
           mode == "between" ||
           mode == "interval";
}

bool is_first_order_like_contribution(const MeritContribution& c) {
    if (c.category == MeritCategory::FirstOrder) {
        return true;
    }
    return contribution_name_contains(
        c,
        {"first_order", "efl", "effective_focal", "ttl", "bfl",
         "track", "image_height"});
}

}  // namespace optimizer_detail

}  // namespace optiland
