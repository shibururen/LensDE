/*
 * Loads physical air/glass thickness limits and structural/pupil-order priors.
 * load_merit_json delegates these settings before quality and trace normalization.
 * Center/edge bounds, ATC/ATE/BL0 ratios and tangent-angle sampling preserve their
 * existing units, defaults and activation semantics for manufacturing merit stages.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_manufacturing(const Json& root, MeritConfig* config) {
config->glass_center_thickness_lower =
            root.at("glass_center_thickness_lower").as_number(config->glass_center_thickness_lower);
        config->glass_center_thickness_upper =
            root.at("glass_center_thickness_upper").as_number(config->glass_center_thickness_upper);
        config->air_center_thickness_lower =
            root.at("air_center_thickness_lower").as_number(config->air_center_thickness_lower);
        config->air_center_thickness_upper =
            root.at("air_center_thickness_upper").as_number(config->air_center_thickness_upper);
        config->glass_edge_thickness_lower =
            root.at("glass_edge_thickness_lower").as_number(config->glass_edge_thickness_lower);
        config->glass_edge_thickness_upper =
            root.at("glass_edge_thickness_upper").as_number(config->glass_edge_thickness_upper);
        config->air_edge_thickness_lower =
            root.at("air_edge_thickness_lower").as_number(config->air_edge_thickness_lower);
        config->air_edge_thickness_upper =
            root.at("air_edge_thickness_upper").as_number(config->air_edge_thickness_upper);
        config->structural_prior_tight_triplet_weight = std::max(
            0.0,
            root.at("structural_prior_tight_triplet_weight").as_number(
                config->structural_prior_tight_triplet_weight));
        config->structural_prior_gap_tolerance = std::max(
            0.0,
            root.at("structural_prior_gap_tolerance").as_number(
                config->structural_prior_gap_tolerance));
        config->structural_prior_curvature_epsilon = std::max(
            0.0,
            root.at("structural_prior_curvature_epsilon").as_number(
                config->structural_prior_curvature_epsilon));
        config->pupil_order_prior_weight = std::max(
            0.0,
            root.at("pupil_order_prior_weight").as_number(
                config->pupil_order_prior_weight));
        config->pupil_order_jacobian_floor =
            root.at("pupil_order_jacobian_floor").as_number(
                config->pupil_order_jacobian_floor);
        config->pupil_order_soft_tolerance = std::max(
            1.0e-12,
            root.at("pupil_order_soft_tolerance").as_number(
                config->pupil_order_soft_tolerance));
        config->pupil_order_hard_jacobian =
            root.at("pupil_order_hard_jacobian").as_number(
                config->pupil_order_hard_jacobian);
        config->pupil_order_hard_max_progress = std::clamp(
            root.at("pupil_order_hard_max_progress").as_number(
                config->pupil_order_hard_max_progress),
            0.0,
            1.0);
        config->pupil_order_hard_consecutive_surfaces = std::max(
            1,
            static_cast<int>(root.at(
                "pupil_order_hard_consecutive_surfaces").as_number(
                config->pupil_order_hard_consecutive_surfaces)));
        config->pupil_order_min_field_radius = std::clamp(
            root.at("pupil_order_min_field_radius").as_number(
                config->pupil_order_min_field_radius),
            0.0,
            1.0);
        auto normalize_thickness_range = [](double& lower, double& upper) {
            lower = std::max(0.0, lower);
            upper = std::max(lower, upper);
        };
        normalize_thickness_range(
            config->glass_center_thickness_lower,
            config->glass_center_thickness_upper);
        normalize_thickness_range(
            config->air_center_thickness_lower,
            config->air_center_thickness_upper);
        normalize_thickness_range(
            config->glass_edge_thickness_lower,
            config->glass_edge_thickness_upper);
        normalize_thickness_range(
            config->air_edge_thickness_lower,
            config->air_edge_thickness_upper);
        config->atc_lower = root.at("atc_lower").as_number(config->atc_lower);
        config->atc_upper = root.at("atc_upper").as_number(config->atc_upper);
        config->ate_lower = root.at("ate_lower").as_number(config->ate_lower);
        config->ate_upper = root.at("ate_upper").as_number(config->ate_upper);
        config->bl0_lower = root.at("bl0_lower").as_number(config->bl0_lower);
        config->bl0_upper = root.at("bl0_upper").as_number(config->bl0_upper);
        config->surface_tangent_angle_lower_deg = root.at(
            "surface_tangent_angle_lower_deg").as_number(
                config->surface_tangent_angle_lower_deg);
        config->surface_tangent_angle_samples = static_cast<int>(root.at(
            "surface_tangent_angle_samples").as_number(
                config->surface_tangent_angle_samples));
        auto normalize_optional_ratio_range = [](double& lower, double& upper) {
            lower = std::max(0.0, lower);
            upper = std::max(0.0, upper);
            if (upper > 0.0 && upper < lower) {
                upper = lower;
            }
        };
        normalize_optional_ratio_range(config->atc_lower, config->atc_upper);
        normalize_optional_ratio_range(config->ate_lower, config->ate_upper);
        normalize_optional_ratio_range(config->bl0_lower, config->bl0_upper);
        config->surface_tangent_angle_lower_deg = std::clamp(
            config->surface_tangent_angle_lower_deg,
            0.0,
            90.0);
        config->surface_tangent_angle_samples = std::clamp(
            config->surface_tangent_angle_samples,
            2,
            4096);
}
}  // namespace io_detail

}  // namespace optiland
