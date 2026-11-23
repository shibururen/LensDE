/*
 * Loads unified first-order/image weights, coverage gates and structural-ray controls.
 * load_merit_json applies this family after the trace and sampling settings.
 * Compatibility weight aliases and gate defaults link serialized image targets to
 * the later unified merit categories, axis-crossing checks and early-focus guards.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_image_weights(const Json& root, MeritConfig* config) {
config->unified_efl_rel_tol =
            root.at("unified_efl_rel_tol").as_number(root.at("quality_efl_rel_tol").as_number(config->unified_efl_rel_tol));
        config->unified_length_abs_tol =
            root.at("unified_length_abs_tol").as_number(root.at("quality_length_abs_tol").as_number(config->unified_length_abs_tol));
        config->unified_first_order_efl_weight =
            root.at("unified_first_order_efl_weight").as_number(config->weight_efl);
        config->unified_first_order_ttl_weight =
            root.at("unified_first_order_ttl_weight").as_number(config->weight_ttl);
        config->unified_first_order_bfl_weight =
            root.at("unified_first_order_bfl_weight").as_number(config->weight_bfl);
        config->unified_first_order_working_f_number_weight = root.at(
            "unified_first_order_working_f_number_weight").as_number(
                config->unified_first_order_working_f_number_weight);
        config->unified_first_order_image_height_weight =
            root.at("unified_first_order_image_height_weight").as_number(config->unified_first_order_image_height_weight);
        config->unified_image_mean_spot_weight =
            root.at("unified_image_mean_spot_weight").as_number(config->unified_image_mean_spot_weight);
        config->unified_image_single_ray_spot_tail_weight =
            root.at("unified_image_single_ray_spot_tail_weight")
                .as_number(config->unified_image_single_ray_spot_tail_weight);
        config->single_ray_spot_tail_extra_edge_min_field_radius =
            root.at("single_ray_spot_tail_extra_edge_min_field_radius")
                .as_number(
                    config->single_ray_spot_tail_extra_edge_min_field_radius);
        if (!std::isfinite(
                config->single_ray_spot_tail_extra_edge_min_field_radius)) {
            config->single_ray_spot_tail_extra_edge_min_field_radius =
                MeritConfig{}.single_ray_spot_tail_extra_edge_min_field_radius;
        }
        config->single_ray_spot_tail_extra_edge_min_field_radius = std::clamp(
            config->single_ray_spot_tail_extra_edge_min_field_radius,
            0.0,
            2.0);
        config->unified_image_wavefront_weight =
            root.at("unified_image_wavefront_weight").as_number(config->unified_image_wavefront_weight);
        config->unified_image_max_spot_weight =
            root.at("unified_image_max_spot_weight").as_number(config->unified_image_max_spot_weight);
        config->unified_image_field_balance_weight =
            root.at("unified_image_field_balance_weight").as_number(config->unified_image_field_balance_weight);
        config->unified_image_chromatic_weight =
            root.at("unified_image_chromatic_weight").as_number(config->unified_image_chromatic_weight);
        config->unified_image_distortion_weight =
            root.at("unified_image_distortion_weight").as_number(config->unified_image_distortion_weight);
        config->unified_image_cra_weight =
            root.at("unified_image_cra_weight").as_number(config->unified_image_cra_weight);
        config->unified_gate_system_coverage_lo =
            root.at("unified_gate_system_coverage_lo").as_number(config->unified_gate_system_coverage_lo);
        config->unified_gate_min_field_lo =
            root.at("unified_gate_min_field_lo").as_number(config->unified_gate_min_field_lo);
        config->unified_gate_chief_progress_lo =
            root.at("unified_gate_chief_progress_lo").as_number(config->unified_gate_chief_progress_lo);
        config->unified_min_first_order_gate =
            root.at("unified_min_first_order_gate").as_number(config->unified_min_first_order_gate);
        config->reject_ray_path_structural_failures =
            root.at("reject_ray_path_structural_failures").as_bool(
                root.at("reject_structural_ray_path_failures").as_bool(config->reject_ray_path_structural_failures));
        config->axis_cross_allow_count =
            static_cast<int>(root.at("axis_cross_allow_count").as_number(config->axis_cross_allow_count));
        config->axis_crossing_epsilon =
            root.at("axis_crossing_epsilon").as_number(config->axis_crossing_epsilon);
        config->center_early_focus_guard_mm =
            root.at("center_early_focus_guard_mm").as_number(config->center_early_focus_guard_mm);
        config->center_early_focus_edge_ratio =
            root.at("center_early_focus_edge_ratio").as_number(config->center_early_focus_edge_ratio);
}
}  // namespace io_detail

}  // namespace optiland
