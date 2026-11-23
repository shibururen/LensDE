/*
 * Adds center/edge thickness limits and the configured tight-triplet structural prior.
 * Called by the merit pipeline after ray-geometry terms have been recorded.
 * Variable-only activation, physical air/glass intervals and image-space exclusions
 * govern which bounds contribute; their order and units remain unchanged.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void MeritEvaluation::append_thickness_constraints() {
double constrained_min_edge = score.min_edge;
    if (config.thickness_constraints_variable_only) {
        constrained_min_edge = std::numeric_limits<double>::quiet_NaN();
        for (const std::size_t index : config.thickness_variable_indices) {
            if (index >= system.surfaces.size() ||
                system.surfaces[index].kind == SurfaceKind::Object ||
                image_space_interval(system, index)) {
                continue;
            }
            const double edge = edge_thickness_at(system, index);
            if (std::isfinite(edge) &&
                (!std::isfinite(constrained_min_edge) || edge < constrained_min_edge)) {
                constrained_min_edge = edge;
            }
        }
    }
    double min_edge_penalty = 0.0;
    if (std::isfinite(constrained_min_edge) &&
        constrained_min_edge < config.min_edge_thickness) {
        const double d = config.min_edge_thickness - constrained_min_edge;
        min_edge_penalty = scoring_weight(config.weight_edge) * d * d;
    } else if (!std::isfinite(constrained_min_edge) &&
               !config.thickness_variable_indices.empty()) {
        min_edge_penalty = scoring_weight(config.weight_edge) * 1.0e12;
    }
    add_contribution(make_contribution(
        "default_constraint",
        config.thickness_constraints_variable_only
            ? "variable_min_edge_thickness"
            : "min_edge_thickness",
        "greater_than",
        constrained_min_edge,
        config.min_edge_thickness,
        config.min_edge_thickness,
        kInf,
        config.weight_edge,
        min_edge_penalty));

    for (std::size_t i = system.first_optical_surface(); i + 1 < system.surfaces.size(); ++i) {
        const auto& surface = system.surfaces[i];
        if (surface.kind == SurfaceKind::Object || image_space_interval(system, i) ||
            !std::isfinite(surface.thickness)) {
            continue;
        }
        // The BFL operand constrains the aggregate rear path beginning at the
        // configured reference surface.  The reference interval is still a
        // physical air/glass space, so it must also satisfy the same ordinary
        // center/edge thickness bounds as every other variable interval.
        if (!thickness_constraint_is_active(config, i)) {
            continue;
        }
        const bool glass = interval_is_glass(surface);
        const double center_lower = glass ? config.glass_center_thickness_lower
                                          : config.air_center_thickness_lower;
        const double center_upper = glass ? config.glass_center_thickness_upper
                                          : config.air_center_thickness_upper;
        const double edge_lower = glass ? config.glass_edge_thickness_lower
                                        : config.air_edge_thickness_lower;
        const double edge_upper = glass ? config.glass_edge_thickness_upper
                                        : config.air_edge_thickness_upper;
        const double center_penalty = bound_penalty(surface.thickness, center_lower, center_upper, config.weight_edge);
        add_contribution(make_contribution(
            "default_constraint",
            glass ? "glass_center_thickness" : "air_center_thickness",
            "range",
            surface.thickness,
            std::numeric_limits<double>::quiet_NaN(),
            center_lower,
            center_upper,
            config.weight_edge,
            center_penalty,
            i));
        const double edge_value = edge_thickness_at(system, i);
        const double edge_penalty = bound_penalty(edge_value, edge_lower, edge_upper, config.weight_edge);
        add_contribution(make_contribution(
            "default_constraint",
            glass ? "glass_edge_thickness" : "air_edge_thickness",
            "range",
            edge_value,
            std::numeric_limits<double>::quiet_NaN(),
            edge_lower,
            edge_upper,
            config.weight_edge,
            edge_penalty,
            i));
        if (glass) {
            const double atc_lower = active_lower_bound(config.atc_lower);
            const double atc_upper = active_upper_bound(config.atc_upper);
            const double atc_value = glass_center_thickness_od_ratio(system, i);
            const double atc_penalty = bound_penalty(atc_value, atc_lower, atc_upper, config.weight_edge);
            add_contribution(make_contribution(
                "default_constraint",
                "atc_center_thickness_od_ratio",
                "range",
                atc_value,
                std::numeric_limits<double>::quiet_NaN(),
                atc_lower,
                atc_upper,
                config.weight_edge,
                atc_penalty,
                i));
            const double ate_lower = active_lower_bound(config.ate_lower);
            const double ate_upper = active_upper_bound(config.ate_upper);
            const double ate_value = glass_edge_thickness_od_ratio(system, i);
            const double ate_penalty = bound_penalty(ate_value, ate_lower, ate_upper, config.weight_edge);
            add_contribution(make_contribution(
                "default_constraint",
                "ate_edge_thickness_od_ratio",
                "range",
                ate_value,
                std::numeric_limits<double>::quiet_NaN(),
                ate_lower,
                ate_upper,
                config.weight_edge,
                ate_penalty,
                i));
        }
    }
}

void MeritEvaluation::append_triplet_prior() {
if (config.structural_prior_tight_triplet_weight > 0.0 &&
        config.structural_prior_gap_tolerance > 0.0) {
        struct GlassElement {
            std::size_t front = 0;
            std::size_t rear = 0;
        };
        std::vector<GlassElement> elements;
        for (std::size_t i = system.first_optical_surface();
             i + 1 < system.surfaces.size();) {
            if (!interval_is_glass(system.surfaces[i])) {
                ++i;
                continue;
            }
            const std::size_t front = i;
            while (i + 1 < system.surfaces.size() &&
                   interval_is_glass(system.surfaces[i])) {
                ++i;
            }
            if (i < system.surfaces.size()) {
                elements.push_back(GlassElement{front, i});
            }
        }

        const auto curvature_sign = [&](std::size_t surface_index) {
            if (surface_index >= system.surfaces.size()) {
                return 0;
            }
            const Surface& surface = system.surfaces[surface_index];
            if (surface.is_plane() || !std::isfinite(surface.radius) ||
                std::abs(surface.radius) < kEps) {
                return 0;
            }
            const double curvature = 1.0 / surface.radius;
            if (!std::isfinite(curvature) ||
                std::abs(curvature) <= config.structural_prior_curvature_epsilon) {
                return 0;
            }
            return curvature > 0.0 ? 1 : -1;
        };
        const auto tight_gap_activation = [&](std::size_t surface_index) {
            if (surface_index >= system.surfaces.size() ||
                !thickness_constraint_is_active(config, surface_index)) {
                return 0.0;
            }
            const double gap = system.surfaces[surface_index].thickness;
            if (!std::isfinite(gap) || interval_is_glass(system.surfaces[surface_index])) {
                return 0.0;
            }
            const double tolerance = config.structural_prior_gap_tolerance;
            return std::max(
                0.0,
                std::min(
                    1.0,
                    (config.air_center_thickness_lower + tolerance - gap) /
                        tolerance));
        };

        for (std::size_t element_index = 0;
             element_index + 2 < elements.size();
             ++element_index) {
            const GlassElement& first = elements[element_index];
            const GlassElement& second = elements[element_index + 1];
            const GlassElement& third = elements[element_index + 2];
            if (first.rear + 1 != second.front ||
                second.rear + 1 != third.front) {
                continue;
            }
            const int sign = curvature_sign(first.front);
            if (sign == 0 || curvature_sign(first.rear) != sign ||
                curvature_sign(second.front) != sign ||
                curvature_sign(second.rear) != sign ||
                curvature_sign(third.front) != sign ||
                curvature_sign(third.rear) != sign) {
                continue;
            }
            const double activation = std::min(
                tight_gap_activation(first.rear),
                tight_gap_activation(second.rear));
            if (activation <= 0.0) {
                continue;
            }
            const double penalty =
                config.structural_prior_tight_triplet_weight *
                activation * activation;
            add_contribution(make_contribution(
                "structural_prior",
                "same_sign_tight_lens_triplet",
                "minimize",
                activation,
                0.0,
                0.0,
                1.0,
                config.structural_prior_tight_triplet_weight,
                penalty,
                first.front,
                element_index));
        }
    }
}
}  // namespace analysis_detail

}  // namespace optiland
