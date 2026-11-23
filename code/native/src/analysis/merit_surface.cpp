/*
 * Adds surface-shape, explicit bound and material-proximity constraints to the score.
 * Called near the end of the merit pipeline after interval manufacturing terms.
 * Object/image and independent-stop exclusions, tangent-angle normalization and
 * stored operand indices determine the physical surface each contribution constrains.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void MeritEvaluation::append_surface_constraints() {
for (std::size_t i = system.first_optical_surface(); i < system.surfaces.size(); ++i) {
        const auto& surface = system.surfaces[i];
        if (surface.kind == SurfaceKind::Object || surface.kind == SurfaceKind::Image ||
            independent_air_stop_surface(surface)) {
            continue;
        }
        const double bl0_lower = active_lower_bound(config.bl0_lower);
        const double bl0_upper = active_upper_bound(config.bl0_upper);
        const double bl0_value = surface_bl0_ratio(surface);
        const double bl0_penalty = bound_penalty(bl0_value, bl0_lower, bl0_upper, config.weight_edge);
        add_contribution(make_contribution(
            "default_constraint",
            "surface_bl0",
            "range",
            bl0_value,
            std::numeric_limits<double>::quiet_NaN(),
            bl0_lower,
            bl0_upper,
            config.weight_edge,
            bl0_penalty,
            i));
    }

    const double tangent_angle_lower = std::clamp(
        config.surface_tangent_angle_lower_deg,
        0.0,
        90.0);
    const double tangent_angle_tolerance = std::max(
        1.0e-9,
        std::abs(config.quality_surface_tangent_angle_tol_deg));
    for (std::size_t i = system.first_optical_surface(); i < system.surfaces.size(); ++i) {
        const Surface& surface = system.surfaces[i];
        if (surface.kind == SurfaceKind::Object || surface.kind == SurfaceKind::Image) {
            continue;
        }
        const double value = minimum_surface_tangent_axis_angle_deg(
            surface,
            config.surface_tangent_angle_samples);
        const double deficit = std::isfinite(value)
                                   ? std::max(0.0, tangent_angle_lower - value)
                                   : tangent_angle_lower;
        const double active_weight = tangent_angle_lower > 0.0 ? config.weight_edge : 0.0;
        const double normalized_deficit = deficit / tangent_angle_tolerance;
        const double penalty = scoring_weight(active_weight) *
                               normalized_deficit * normalized_deficit;
        add_contribution(make_contribution(
            "default_constraint",
            "surface_tangent_axis_angle_deg",
            "greater_than",
            value,
            tangent_angle_lower,
            tangent_angle_lower,
            kInf,
            active_weight,
            penalty,
            i));
    }

    for (const auto& bound : config.bounds) {
        if (is_interval_thickness_constraint_kind(bound.kind) &&
            !thickness_constraint_is_active(config, bound.index)) {
            continue;
        }
        const double value = constraint_value(system, bound);
        const double penalty = bound_penalty(value, bound.lower, bound.upper, bound.weight);
        add_contribution(make_contribution(
            "bound",
            bound.kind,
            "range",
            value,
            std::numeric_limits<double>::quiet_NaN(),
            bound.lower,
            bound.upper,
            bound.weight,
            penalty,
            bound.index,
            bound.term));
    }
}

void MeritEvaluation::append_material_constraints() {
for (const auto& constraint : config.glass_proximity) {
        if (scoring_weight(constraint.weight) > 0.0) {
            const double value = glass_proximity_distance(system, constraint);
            const double excess =
                std::max(0.0, value - std::max(0.0, constraint.tolerance));
            const double penalty = std::isfinite(value)
                                       ? scoring_weight(constraint.weight) * excess * excess
                                       : scoring_weight(constraint.weight) * 1.0e12;
            add_contribution(make_contribution(
                "regularization",
                "glass_proximity",
                "less_than",
                value,
                constraint.tolerance,
                -kInf,
                constraint.tolerance,
                constraint.weight,
                penalty,
                constraint.index,
                0));
        }
        if (scoring_weight(constraint.region_weight) > 0.0) {
            const double value = glass_region_distance(system, constraint);
            const double excess =
                std::max(0.0, value - std::max(0.0, constraint.region_tolerance));
            const double penalty = std::isfinite(value)
                                       ? scoring_weight(constraint.region_weight) * excess * excess
                                       : scoring_weight(constraint.region_weight) * 1.0e12;
            add_contribution(make_contribution(
                "regularization",
                "glass_connected_region",
                "less_than",
                value,
                constraint.region_tolerance,
                -kInf,
                constraint.region_tolerance,
                constraint.region_weight,
                penalty,
                constraint.index,
                0));
        }
    }
}
}  // namespace analysis_detail

}  // namespace optiland
