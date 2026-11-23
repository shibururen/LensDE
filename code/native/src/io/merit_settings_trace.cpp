/*
 * Loads trace coverage/failure penalties and unified trace/constraint group settings.
 * load_merit_json applies this family between sampling and image-weight readers.
 * Chief/off-axis requirements, compatibility aliases and numeric fallback rules must
 * remain consistent with hard/soft trace gating and contribution normalization.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_trace(const Json& root, MeritConfig* config) {
config->min_field_coverage =
            root.at("min_field_coverage").as_number(config->min_field_coverage);
        config->min_system_coverage =
            root.at("min_system_coverage").as_number(config->min_system_coverage);
        config->min_coverage_for_image_merit =
            root.at("min_coverage_for_image_merit").as_number(config->min_coverage_for_image_merit);
        config->trace_failure_weight =
            root.at("trace_failure_weight").as_number(config->trace_failure_weight);
        config->coverage_failure_weight =
            root.at("coverage_failure_weight").as_number(config->coverage_failure_weight);
        config->aim_failure_weight =
            root.at("aim_failure_weight").as_number(config->aim_failure_weight);
        config->tir_failure_weight =
            root.at("tir_failure_weight").as_number(config->tir_failure_weight);
        config->aperture_failure_weight =
            root.at("aperture_failure_weight").as_number(config->aperture_failure_weight);
        config->surface_miss_weight =
            root.at("surface_miss_weight").as_number(config->surface_miss_weight);
        config->backward_failure_weight =
            root.at("backward_failure_weight").as_number(config->backward_failure_weight);
        config->require_chief_ray =
            root.at("require_chief_ray").as_bool(config->require_chief_ray);
        config->require_off_axis_field_coverage =
            root.at("require_off_axis_field_coverage").as_bool(config->require_off_axis_field_coverage);
        config->hard_trace_gate_weight =
            root.at("hard_trace_gate_weight").as_number(config->hard_trace_gate_weight);
        config->trace_progress_weight =
            root.at("trace_progress_weight").as_number(config->trace_progress_weight);
        config->chief_progress_weight =
            root.at("chief_progress_weight").as_number(config->chief_progress_weight);
        config->trace_priority_weight =
            root.at("trace_priority_weight").as_number(config->trace_priority_weight);
        config->allow_soft_aim_rays_for_spot =
            root.at("allow_soft_aim_rays_for_spot").as_bool(config->allow_soft_aim_rays_for_spot);
        config->soft_aim_residual_for_spot =
            root.at("soft_aim_residual_for_spot").as_number(config->soft_aim_residual_for_spot);
        config->unified_loss_delta =
            root.at("unified_loss_delta").as_number(config->unified_loss_delta);
        config->group_weight_trace =
            root.at("group_weight_trace").as_number(config->group_weight_trace);
        config->group_weight_constraint =
            root.at("group_weight_constraint").as_number(config->group_weight_constraint);
        config->group_weight_first_order =
            root.at("group_weight_first_order").as_number(config->group_weight_first_order);
        config->group_weight_image =
            root.at("group_weight_image").as_number(config->group_weight_image);
        config->unified_coverage_tol =
            root.at("unified_coverage_tol").as_number(config->unified_coverage_tol);
        config->unified_progress_tol =
            root.at("unified_progress_tol").as_number(config->unified_progress_tol);
        config->unified_failed_weight_tol =
            root.at("unified_failed_weight_tol").as_number(config->unified_failed_weight_tol);
        config->unified_trace_system_weight =
            root.at("unified_trace_system_weight").as_number(config->unified_trace_system_weight);
        config->unified_trace_min_field_weight =
            root.at("unified_trace_min_field_weight").as_number(config->unified_trace_min_field_weight);
        config->unified_trace_progress_weight =
            root.at("unified_trace_progress_weight").as_number(config->unified_trace_progress_weight);
        config->unified_trace_chief_progress_weight =
            root.at("unified_trace_chief_progress_weight").as_number(config->unified_trace_chief_progress_weight);
        config->unified_trace_failed_weight =
            root.at("unified_trace_failed_weight").as_number(config->unified_trace_failed_weight);
        config->unified_trace_failure_type_weight =
            root.at("unified_trace_failure_type_weight").as_number(config->unified_trace_failure_type_weight);
        config->unified_edge_tol =
            root.at("unified_edge_tol").as_number(root.at("quality_edge_tol").as_number(config->unified_edge_tol));
        config->unified_air_gap_tol =
            root.at("unified_air_gap_tol").as_number(config->unified_air_gap_tol);
        config->unified_center_thickness_tol =
            root.at("unified_center_thickness_tol").as_number(config->unified_center_thickness_tol);
        config->unified_ttl_tol =
            root.at("unified_ttl_tol").as_number(config->unified_ttl_tol);
        config->unified_bfl_tol =
            root.at("unified_bfl_tol").as_number(config->unified_bfl_tol);
        config->unified_constraint_edge_weight =
            root.at("unified_constraint_edge_weight").as_number(config->unified_constraint_edge_weight);
        config->unified_constraint_air_gap_weight =
            root.at("unified_constraint_air_gap_weight").as_number(config->unified_constraint_air_gap_weight);
        config->unified_constraint_center_weight =
            root.at("unified_constraint_center_weight").as_number(config->unified_constraint_center_weight);
        config->unified_constraint_bfl_weight =
            root.at("unified_constraint_bfl_weight").as_number(config->unified_constraint_bfl_weight);
        config->unified_constraint_ttl_weight =
            root.at("unified_constraint_ttl_weight").as_number(config->unified_constraint_ttl_weight);
        config->unified_constraint_curvature_weight =
            root.at("unified_constraint_curvature_weight").as_number(config->unified_constraint_curvature_weight);
        config->unified_constraint_aperture_weight =
            root.at("unified_constraint_aperture_weight").as_number(config->unified_constraint_aperture_weight);
}
}  // namespace io_detail

}  // namespace optiland
