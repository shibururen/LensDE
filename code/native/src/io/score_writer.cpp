/*
 * Serializes Score measurements and ordered merit contributions for CLI/checkpoint output.
 * Native scoring and optimizer publication call this writer for structured diagnostics.
 * Keep the output keys and array order stable: finite values use 12 significant digits
 * and unavailable numeric values use the shared JSON-null representation.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

bool save_score_json(const Score& score, const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << std::setprecision(12);
    out << "{\n";
    out << "  \"merit_model\": \"" << json_escape(score.merit_model) << "\",\n";
    out << "  \"chief_reference_contract\": \"" << kChiefReferenceContract << "\",\n";
    out << "  \"principal_ray_required\": false,\n";
    out << "  \"merit\": ";
    write_json_number(out, score.merit);
    out << ",\n  \"uncapped_merit\": ";
    write_json_number(out, score.uncapped_merit);
    out << ",\n  \"merit_sum\": ";
    write_json_number(out, score.merit_sum);
    out << ",\n  \"merit_weight_sum\": ";
    write_json_number(out, score.merit_weight_sum);
    out << ",\n  \"efl\": ";
    write_json_number(out, score.efl);
    out << ",\n  \"ttl\": ";
    write_json_number(out, score.ttl);
    out << ",\n  \"bfl\": ";
    write_json_number(out, score.bfl);
    out << ",\n  \"working_f_number\": ";
    write_json_number(out, score.working_f_number);
    out << ",\n  \"image_space_numerical_aperture\": ";
    write_json_number(out, score.image_space_numerical_aperture);
    out << ",\n  \"working_f_number_valid_marginal_rays\": "
        << score.working_f_number_valid_marginal_rays;
    out << ",\n  \"mean_spot\": ";
    write_json_number(out, score.mean_spot);
    out << ",\n  \"max_spot\": ";
    write_json_number(out, score.max_spot);
    out << ",\n  \"mean_worst_wavelength_spot\": ";
    write_json_number(out, score.mean_worst_wavelength_spot);
    out << ",\n  \"max_worst_wavelength_spot\": ";
    write_json_number(out, score.max_worst_wavelength_spot);
    out << ",\n  \"mean_spectral_spot_spread\": ";
    write_json_number(out, score.mean_spectral_spot_spread);
    out << ",\n  \"max_spectral_spot_spread\": ";
    write_json_number(out, score.max_spectral_spot_spread);
    out << ",\n  \"paraxial_axial_color_focus_shift_mm\": ";
    write_json_number(out, score.paraxial_axial_color_focus_shift_mm);
    out << ",\n  \"max_single_ray_spot\": ";
    write_json_number(out, score.max_single_ray_spot);
    out << ",\n  \"max_single_ray_spot_ratio\": ";
    write_json_number(out, score.max_single_ray_spot_ratio);
    out << ",\n  \"mean_wavefront_rms\": ";
    write_json_number(out, score.mean_wavefront_rms);
    out << ",\n  \"max_wavefront_rms\": ";
    write_json_number(out, score.max_wavefront_rms);
    out << ",\n  \"max_single_ray_wavefront\": ";
    write_json_number(out, score.max_single_ray_wavefront);
    out << ",\n  \"max_single_ray_wavefront_ratio\": ";
    write_json_number(out, score.max_single_ray_wavefront_ratio);
    out << ",\n  \"max_distortion\": ";
    write_json_number(out, score.max_distortion);
    out << ",\n  \"max_distortion_percent\": ";
    write_json_number(out, score.max_distortion * 100.0);
    out << ",\n  \"distortion_model\": \"" << json_escape(score.distortion_model) << "\"";
    out << ",\n  \"distortion_sign_flip_fields\": " << score.distortion_sign_flip_fields;
    out << ",\n  \"max_chief_ray_angle\": ";
    write_json_number(out, score.max_chief_ray_angle);
    out << ",\n  \"chief_ray_path_bend_mean_deg\": ";
    write_json_number(out, score.chief_ray_path_bend_mean_deg);
    out << ",\n  \"chief_ray_path_bend_mean_off_axis_deg\": ";
    write_json_number(out, score.chief_ray_path_bend_mean_off_axis_deg);
    out << ",\n  \"chief_ray_path_bend_max_deg\": ";
    write_json_number(out, score.chief_ray_path_bend_max_deg);
    out << ",\n  \"chief_ray_path_bend_max_step_deg\": ";
    write_json_number(out, score.chief_ray_path_bend_max_step_deg);
    out << ",\n  \"chief_ray_path_excess_mean\": ";
    write_json_number(out, score.chief_ray_path_excess_mean);
    out << ",\n  \"chief_ray_path_excess_max\": ";
    write_json_number(out, score.chief_ray_path_excess_max);
    out << ",\n  \"lateral_color\": ";
    write_json_number(out, score.lateral_color);
    out << ",\n  \"max_lateral_color\": ";
    write_json_number(out, score.lateral_color);
    out << ",\n  \"finite_ray_field_focus_spread_mm\": ";
    write_json_number(out, score.finite_ray_field_focus_spread_mm);
    out << ",\n  \"finite_ray_astig_focus_gap_mm\": ";
    write_json_number(out, score.finite_ray_astig_focus_gap_mm);
    out << ",\n  \"finite_ray_axial_color_focus_shift_mm\": ";
    write_json_number(out, score.finite_ray_axial_color_focus_shift_mm);
    out << ",\n  \"finite_ray_lateral_color_mm\": ";
    write_json_number(out, score.finite_ray_lateral_color_mm);
    out << ",\n  \"finite_ray_spherical_focus_spread_mm\": ";
    write_json_number(out, score.finite_ray_spherical_focus_spread_mm);
    out << ",\n  \"finite_ray_coma_tangential_mm\": ";
    write_json_number(out, score.finite_ray_coma_tangential_mm);
    out << ",\n  \"finite_ray_valid_fraction\": ";
    write_json_number(out, score.finite_ray_valid_fraction);
    out << ",\n  \"finite_ray_metric_count\": ";
    write_json_number(out, score.finite_ray_metric_count);
    out << ",\n  \"finite_ray_evidence\": ";
    write_json_number(out, score.finite_ray_evidence);
    out << ",\n  \"topology_potential_score_v2\": ";
    write_json_number(out, score.topology_potential_score_v2);
    out << ",\n  \"topology_potential_first_order_health_v2\": ";
    write_json_number(out, score.topology_potential_first_order_health_v2);
    out << ",\n  \"topology_potential_hard_aberration_health_v2\": ";
    write_json_number(out, score.topology_potential_hard_aberration_health_v2);
    out << ",\n  \"topology_potential_image_health_v2\": ";
    write_json_number(out, score.topology_potential_image_health_v2);
    out << ",\n  \"topology_potential_margin_health_v2\": ";
    write_json_number(out, score.topology_potential_margin_health_v2);
    out << ",\n  \"topology_potential_balance_health_v2\": ";
    write_json_number(out, score.topology_potential_balance_health_v2);
    out << ",\n  \"topology_potential_trace_health_v2\": ";
    write_json_number(out, score.topology_potential_trace_health_v2);
    out << ",\n  \"topology_potential_score_v3\": ";
    write_json_number(out, score.topology_potential_score_v3);
    out << ",\n  \"topology_potential_first_order_health_v3\": ";
    write_json_number(out, score.topology_potential_first_order_health_v3);
    out << ",\n  \"topology_potential_chief_path_health_v3\": ";
    write_json_number(out, score.topology_potential_chief_path_health_v3);
    out << ",\n  \"topology_potential_hard_aberration_health_v3\": ";
    write_json_number(out, score.topology_potential_hard_aberration_health_v3);
    out << ",\n  \"topology_potential_image_health_v3\": ";
    write_json_number(out, score.topology_potential_image_health_v3);
    out << ",\n  \"topology_potential_margin_health_v3\": ";
    write_json_number(out, score.topology_potential_margin_health_v3);
    out << ",\n  \"topology_potential_balance_health_v3\": ";
    write_json_number(out, score.topology_potential_balance_health_v3);
    out << ",\n  \"topology_potential_trace_health_v3\": ";
    write_json_number(out, score.topology_potential_trace_health_v3);
    out << ",\n  \"min_edge\": ";
    write_json_number(out, score.min_edge);
    out << ",\n  \"trace_coverage\": ";
    write_json_number(out, score.trace_coverage);
    out << ",\n  \"min_field_coverage\": ";
    write_json_number(out, score.min_field_coverage);
    out << ",\n  \"trace_progress\": ";
    write_json_number(out, score.trace_progress);
    out << ",\n  \"min_trace_progress\": ";
    write_json_number(out, score.min_trace_progress);
    out << ",\n  \"min_chief_progress\": ";
    write_json_number(out, score.min_chief_progress);
    out << ",\n  \"min_relative_illumination_proxy\": ";
    write_json_number(out, score.min_relative_illumination_proxy);
    out << ",\n  \"relative_illumination_proxy_by_field\": [";
    for (std::size_t i = 0;
         i < score.relative_illumination_proxy_by_field.size();
         ++i) {
        if (i > 0) {
            out << ", ";
        }
        write_json_number(out, score.relative_illumination_proxy_by_field[i]);
    }
    out << "]";
    out << ",\n  \"relative_illumination_floor_by_field\": [";
    for (std::size_t i = 0;
         i < score.relative_illumination_floor_by_field.size();
         ++i) {
        if (i > 0) {
            out << ", ";
        }
        write_json_number(out, score.relative_illumination_floor_by_field[i]);
    }
    out << "]";
    out << ",\n  \"trace_priority_metric\": ";
    write_json_number(out, score.trace_priority_metric);
    out << ",\n  \"trace_priority_penalty\": ";
    write_json_number(out, score.trace_priority_penalty);
    out << ",\n  \"trace_group_error\": ";
    write_json_number(out, score.trace_group_error);
    out << ",\n  \"constraint_group_error\": ";
    write_json_number(out, score.constraint_group_error);
    out << ",\n  \"first_order_group_error\": ";
    write_json_number(out, score.first_order_group_error);
    out << ",\n  \"image_group_error\": ";
    write_json_number(out, score.image_group_error);
    out << ",\n  \"trace_gate\": ";
    write_json_number(out, score.trace_gate);
    out << ",\n  \"constraint_gate\": ";
    write_json_number(out, score.constraint_gate);
    out << ",\n  \"first_order_gate\": ";
    write_json_number(out, score.first_order_gate);
    out << ",\n  \"image_gate\": ";
    write_json_number(out, score.image_gate);
    out << ",\n  \"trace_penalty\": ";
    write_json_number(out, score.trace_penalty);
    out << ",\n  \"constraint_penalty\": ";
    write_json_number(out, score.constraint_penalty);
    out << ",\n  \"first_order_penalty\": ";
    write_json_number(out, score.first_order_penalty);
    out << ",\n  \"image_penalty\": ";
    write_json_number(out, score.image_penalty);
    out << ",\n  \"other_penalty\": ";
    write_json_number(out, score.other_penalty);
    out << ",\n  \"failed_ray_weight\": ";
    write_json_number(out, score.failed_ray_weight);
    out << ",\n  \"chief_ray_ok\": " << (score.chief_ray_ok ? "true" : "false");
    out << ",\n  \"chief_ray_failed_fields\": " << score.chief_ray_failed_fields;
    out << ",\n  \"zero_coverage_off_axis_fields\": " << score.zero_coverage_off_axis_fields;
    out << ",\n  \"ray_path_axis_cross_fields\": " << score.ray_path_axis_cross_fields;
    out << ",\n  \"center_early_focus_fields\": " << score.center_early_focus_fields;
    out << ",\n  \"chief_entry_side_failed_fields\": " << score.chief_entry_side_failed_fields;
    out << ",\n  \"ray_path_structural_failure_fields\": " << score.ray_path_structural_failure_fields;
    out << ",\n  \"max_axis_cross_count\": " << score.max_axis_cross_count;
    out << ",\n  \"min_pupil_order_jacobian\": ";
    write_json_number(out, score.min_pupil_order_jacobian);
    out << ",\n  \"pupil_order_worst_surface_progress\": ";
    write_json_number(out, score.pupil_order_worst_surface_progress);
    out << ",\n  \"pupil_order_prior_penalty\": ";
    write_json_number(out, score.pupil_order_prior_penalty);
    out << ",\n  \"pupil_order_fold_fields\": " << score.pupil_order_fold_fields;
    out << ",\n  \"pupil_order_hard_fold_fields\": "
        << score.pupil_order_hard_fold_fields;
    out << ",\n";
    out << "  \"trace_ok\": " << (score.trace_ok ? "true" : "false");
    out << ",\n  \"message\": \"" << json_escape(score.message) << "\"";
    out << ",\n  \"contributions\": [\n";
    for (std::size_t i = 0; i < score.contributions.size(); ++i) {
        const auto& c = score.contributions[i];
        out << "    {\"source\": \"" << json_escape(c.source)
            << "\", \"name\": \"" << json_escape(c.name)
            << "\", \"mode\": \"" << json_escape(c.mode)
            << "\", \"index\": " << c.index
            << ", \"term\": " << c.term
            << ", \"value\": ";
        write_json_number(out, c.value);
        out << ", \"target\": ";
        write_json_number(out, c.target);
        out << ", \"lower\": ";
        write_json_number(out, c.lower);
        out << ", \"upper\": ";
        write_json_number(out, c.upper);
        out << ", \"weight\": ";
        write_json_number(out, c.weight);
        out << ", \"field_weight\": ";
        write_json_number(out, c.field_weight);
        out << ", \"penalty\": ";
        write_json_number(out, c.penalty);
        out << ", \"category\": \"" << merit_category_name(c.category) << "\"";
        out << ", \"tolerance\": ";
        write_json_number(out, c.tolerance);
        out << ", \"base_weight\": ";
        write_json_number(out, c.base_weight);
        out << ", \"gate\": ";
        write_json_number(out, c.gate);
        out << ", \"normalized_residual\": ";
        write_json_number(out, c.normalized_residual);
        out << ", \"robust_penalty\": ";
        write_json_number(out, c.robust_penalty);
        if (std::isfinite(c.aperture_candidate_radius)) {
            out << ", \"aperture_candidate_radius\": ";
            write_json_number(out, c.aperture_candidate_radius);
        }
        if (std::isfinite(c.aperture_radius_before_trim)) {
            out << ", \"aperture_radius_before_trim\": ";
            write_json_number(out, c.aperture_radius_before_trim);
        }
        if (std::isfinite(c.aperture_protection_radius)) {
            out << ", \"aperture_protection_radius\": ";
            write_json_number(out, c.aperture_protection_radius);
        }
        if (std::isfinite(c.aperture_trim_window)) {
            out << ", \"aperture_trim_window\": ";
            write_json_number(out, c.aperture_trim_window);
        }
        out << "}";
        out << (i + 1 == score.contributions.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

}  // namespace optiland
