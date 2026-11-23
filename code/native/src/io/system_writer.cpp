/*
 * Serializes LensSystem prescriptions for checkpoints, saved results and CLI handoff.
 * The writer preserves ordered fields/wavelengths/surfaces and their aiming, aperture,
 * asphere and material metadata; its 12-digit numeric format and null conventions
 * are the existing public JSON representation consumed by later readers and tools.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

bool save_system_json(const LensSystem& system, const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << std::setprecision(12);
    out << "{\n";
    out << "  \"name\": \"" << system.name << "\",\n";
    out << "  \"glass_library\": \"" << system.glass_library << "\",\n";
    out << "  \"glass_libraries\": [";
    const auto& libraries = system.glass_libraries.empty()
                                ? std::vector<std::string>{system.glass_library}
                                : system.glass_libraries;
    for (std::size_t i = 0; i < libraries.size(); ++i) {
        out << (i == 0 ? "" : ", ") << "\"" << json_escape(libraries[i]) << "\"";
    }
    out << "],\n";
    out << "  \"bfl_reference_surface\": " << system.bfl_reference_surface << ",\n";
    if (!system.asphere_root_policy.empty()) {
        out << "  \"asphere_root_policy\": \""
            << json_escape(system.asphere_root_policy) << "\",\n";
    }
    const char* aperture_type_name = "EPD";
    if (system.aperture.type == ApertureType::ImageFNumber) {
        aperture_type_name = "imageFNO";
    } else if (system.aperture.type == ApertureType::ObjectNumericalAperture) {
        aperture_type_name = "objectNA";
    } else if (system.aperture.type == ApertureType::FloatByStopSize) {
        aperture_type_name = "float_by_stop_size";
    }
    out << "  \"aperture\": {\"type\": \"" << aperture_type_name
        << "\", \"value\": " << system.aperture.value << "},\n";
    out << "  \"max_field_deg\": " << system.max_field_deg << ",\n";
    out << "  \"fields\": [\n";
    for (std::size_t i = 0; i < system.fields.size(); ++i) {
        const auto& f = system.fields[i];
        out << "    {\"hx\": " << f.hx << ", \"hy\": " << f.hy
            << ", \"fvcx\": " << f.fvcx
            << ", \"fvcy\": " << f.fvcy
            << ", \"fvdx\": " << f.fvdx
            << ", \"fvdy\": " << f.fvdy << "}";
        out << (i + 1 == system.fields.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"wavelengths\": [\n";
    for (std::size_t i = 0; i < system.wavelengths.size(); ++i) {
        const auto& w = system.wavelengths[i];
        out << "    {\"value_um\": " << w.value_um << ", \"weight\": " << w.weight
            << ", \"primary\": " << (w.primary ? "true" : "false") << "}";
        out << (i + 1 == system.wavelengths.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"ray_aiming\": {\"mode\": \"" << ray_aiming_mode_name(system.ray_aiming.mode)
        << "\", \"max_iter\": " << system.ray_aiming.max_iter
        << ", \"tolerance\": ";
    write_json_number(out, system.ray_aiming.tolerance);
    out << ", \"start_offset\": ";
    write_json_number(out, system.ray_aiming.start_offset);
    out << ", \"require_convergence\": "
        << (system.ray_aiming.require_convergence ? "true" : "false")
        << ", \"continuation_steps\": " << system.ray_aiming.continuation_steps
        << ", \"stop_radius_source\": \""
        << ray_aiming_stop_radius_source_name(system.ray_aiming.stop_radius_source) << "\"";
    const auto& cp = system.ray_aiming.calibrated_paraxial;
    out << ", \"calibrated_paraxial\": {"
        << "\"enabled\": " << (cp.enabled ? "true" : "false")
        << ", \"sampling_mode\": \"" << json_escape(cp.sampling_mode) << "\""
        << ", \"allow_grid_upgrade\": " << (cp.allow_grid_upgrade ? "true" : "false")
        << ", \"pilot_grid_n\": " << cp.pilot_grid_n
        << ", \"jacobian_probe_fraction\": ";
    write_json_number(out, cp.jacobian_probe_fraction);
    out << ", \"min_probe_step_mm\": ";
    write_json_number(out, cp.min_probe_step_mm);
    out << ", \"max_probe_step_mm\": ";
    write_json_number(out, cp.max_probe_step_mm);
    out << ", \"max_condition_number\": ";
    write_json_number(out, cp.max_condition_number);
    out << ", \"fit_rms_tolerance_stop_fraction\": ";
    write_json_number(out, cp.fit_rms_tolerance_stop_fraction);
    out << ", \"validation_sample_count\": " << cp.validation_sample_count
        << ", \"validation_residual_stop_fraction\": ";
    write_json_number(out, cp.validation_residual_stop_fraction);
    out << ", \"use_previous_field_model\": " << (cp.use_previous_field_model ? "true" : "false")
        << ", \"use_previous_wave_model\": " << (cp.use_previous_wave_model ? "true" : "false")
        << ", \"chief_anchor_mode\": \"" << json_escape(cp.chief_anchor_mode) << "\""
        << ", \"budget_chief_iterations\": " << cp.budget_chief_iterations
        << ", \"budget_chief_solver\": \"" << json_escape(cp.budget_chief_solver) << "\""
        << ", \"budget_chief_acceptance\": \"" << json_escape(cp.budget_chief_acceptance) << "\""
        << ", \"budget_chief_accept_stop_fraction\": ";
    write_json_number(out, cp.budget_chief_accept_stop_fraction);
    out << ", \"budget_chief_residual_stop_fraction\": ";
    write_json_number(out, cp.budget_chief_residual_stop_fraction);
    out << ", \"budget_chief_min_a\": ";
    write_json_number(out, cp.budget_chief_min_a);
    out << ", \"budget_chief_field_continuation_angle_deg\": ";
    write_json_number(out, cp.budget_chief_field_continuation_angle_deg);
    out << ", \"budget_chief_ignore_aperture_clip\": "
        << (cp.budget_chief_ignore_aperture_clip ? "true" : "false");
    out << ", \"budget_chief_skip_abcd_angle_deg\": ";
    write_json_number(out, cp.budget_chief_skip_abcd_angle_deg);
    out
        << ", \"fallback\": \"" << json_escape(cp.fallback) << "\""
        << ", \"correction_iterations\": " << cp.correction_iterations
        << ", \"edge_calibration_enabled\": "
        << (cp.edge_calibration_enabled ? "true" : "false")
        << ", \"edge_calibration_iterations\": " << cp.edge_calibration_iterations
        << ", \"edge_calibration_solver\": \"" << json_escape(cp.edge_calibration_solver) << "\""
        << ", \"edge_calibration_weight_power\": ";
    write_json_number(out, cp.edge_calibration_weight_power);
    out << ", \"edge_calibration_residual_stop_fraction\": ";
    write_json_number(out, cp.edge_calibration_residual_stop_fraction);
    out
        << "}";
    const auto& ts = system.ray_aiming.three_step;
    out << ", \"three_step\": {"
        << "\"enabled\": " << (ts.enabled ? "true" : "false")
        << ", \"sampling_mode\": \"" << json_escape(ts.sampling_mode) << "\""
        << ", \"chief_reverse_trial_rays\": " << ts.chief_reverse_trial_rays
        << ", \"chief_reverse_max_angle_deg\": ";
    write_json_number(out, ts.chief_reverse_max_angle_deg);
    out << ", \"edge_passes\": " << ts.edge_passes
        << ", \"meridional_iterations\": " << ts.meridional_iterations
        << ", \"sagittal_iterations\": " << ts.sagittal_iterations
        << ", \"edge_solver\": \"" << json_escape(ts.edge_solver) << "\""
        << ", \"edge_residual_stop_fraction\": ";
    write_json_number(out, ts.edge_residual_stop_fraction);
    out
        << ", \"edge_initial_radius_fraction\": ";
    write_json_number(out, ts.edge_initial_radius_fraction);
    out << ", \"accept_stop_fraction\": ";
    write_json_number(out, ts.accept_stop_fraction);
    out << ", \"correction_iterations\": " << ts.correction_iterations
        << ", \"sample_stop_residual_debug\": "
        << (ts.sample_stop_residual_debug ? "true" : "false")
        << ", \"fallback\": \"" << json_escape(ts.fallback) << "\""
        << "}";
    const auto& apg = system.ray_aiming.adaptive_pilot_grid;
    out << ", \"adaptive_pilot_grid\": {"
        << "\"enabled\": " << (apg.enabled ? "true" : "false")
        << ", \"try_iterative_first\": " << (apg.try_iterative_first ? "true" : "false")
        << ", \"try_iterative_for_each_ray\": " << (apg.try_iterative_for_each_ray ? "true" : "false")
        << ", \"iterative_failure_sample_limit\": " << apg.iterative_failure_sample_limit
        << ", \"require_final_convergence\": " << (apg.require_final_convergence ? "true" : "false")
        << ", \"target_field_direction_tolerance\": ";
    write_json_number(out, apg.target_field_direction_tolerance);
    out << ", \"use_chief_gate\": " << (apg.use_chief_gate ? "true" : "false")
        << ", \"use_primary_wave_as_anchor\": " << (apg.use_primary_wave_as_anchor ? "true" : "false")
        << ", \"use_previous_field_chief\": " << (apg.use_previous_field_chief ? "true" : "false")
        << ", \"use_previous_field_extrapolation\": " << (apg.use_previous_field_extrapolation ? "true" : "false")
        << ", \"max_field_extrapolation_factor\": ";
    write_json_number(out, apg.max_field_extrapolation_factor);
    out << ", \"use_previous_wave_seed\": " << (apg.use_previous_wave_seed ? "true" : "false")
        << ", \"use_previous_field_seed\": " << (apg.use_previous_field_seed ? "true" : "false")
        << ", \"use_previous_pupil_seed\": " << (apg.use_previous_pupil_seed ? "true" : "false")
        << ", \"use_chief_jacobian_seed\": " << (apg.use_chief_jacobian_seed ? "true" : "false")
        << ", \"coarse_grid_n\": " << apg.coarse_grid_n
        << ", \"fine_grid_n\": " << apg.fine_grid_n
        << ", \"nearest_k\": " << apg.nearest_k
        << ", \"max_coarse_expansions\": " << apg.max_coarse_expansions
        << ", \"max_fine_expansions\": " << apg.max_fine_expansions
        << ", \"coarse_min_halfwidth_fraction\": ";
    write_json_number(out, apg.coarse_min_halfwidth_fraction);
    out << ", \"coarse_max_halfwidth_fraction\": ";
    write_json_number(out, apg.coarse_max_halfwidth_fraction);
    out << ", \"fine_min_halfwidth_fraction\": ";
    write_json_number(out, apg.fine_min_halfwidth_fraction);
    out << ", \"fine_max_halfwidth_fraction\": ";
    write_json_number(out, apg.fine_max_halfwidth_fraction);
    out << ", \"fine_default_halfwidth_fraction\": ";
    write_json_number(out, apg.fine_default_halfwidth_fraction);
    out << ", \"jacobian_probe_fraction\": ";
    write_json_number(out, apg.jacobian_probe_fraction);
    out << ", \"jacobian_safety_factor\": ";
    write_json_number(out, apg.jacobian_safety_factor);
    out << ", \"allow_delaunay_interpolation\": " << (apg.allow_delaunay_interpolation ? "true" : "false")
        << ", \"allow_barycentric_interpolation\": " << (apg.allow_barycentric_interpolation ? "true" : "false")
        << ", \"use_multistart\": " << (apg.use_multistart ? "true" : "false")
        << ", \"emit_diagnostics\": " << (apg.emit_diagnostics ? "true" : "false")
        << "}},\n";
    out << "  \"surfaces\": [\n";
    for (std::size_t i = 0; i < system.surfaces.size(); ++i) {
        const auto& s = system.surfaces[i];
        out << "    {\"kind\": \"" << surface_kind_name(s.kind) << "\", ";
        out << "\"radius\": ";
        write_json_number(out, s.radius);
        out << ", \"conic\": ";
        write_json_number(out, s.conic);
        out << ", \"thickness\": ";
        write_json_number(out, s.thickness);
        out << ", \"semi_aperture\": ";
        write_json_number(out, s.semi_aperture);
        out << ", \"mechanical_aperture\": ";
        write_json_number(out, s.mechanical_aperture);
        out << ", \"aperture_radius\": ";
        write_json_number(out, s.aperture_radius);
        out << ", \"clear_aperture_fixed\": " << (s.clear_aperture_fixed ? "true" : "false");
        out << ", \"vignetting_aperture_excluded\": " << (s.vignetting_aperture_excluded ? "true" : "false");
        out << ", \"mechanical_aperture_fixed\": " << (s.mechanical_aperture_fixed ? "true" : "false");
        out << ", \"asphere_coefficients\": [";
        for (std::size_t j = 0; j < s.asphere_coefficients.size(); ++j) {
            out << (j == 0 ? "" : ", ") << s.asphere_coefficients[j];
        }
        out << "]";
        out << ", \"aperture\": ";
        if (s.aperture_radius > 0.0) {
            out << "{\"type\": \"RadialAperture\", \"r_max\": " << s.aperture_radius
                << ", \"r_min\": 0}";
        } else {
            out << "null";
        }
        out << ", \"is_stop\": " << (s.is_stop ? "true" : "false")
            << ", \"reflective\": " << (s.reflective ? "true" : "false")
            << ", \"material_after\": {\"name\": \"" << s.material_after.name
            << "\", \"mode\": \""
            << (s.material_after.name == "air" ? "air" : (s.material_after.reference.empty() ? "abbe" : "glass"))
            << "\", \"reference\": \"" << s.material_after.reference
            << "\", \"nd\": " << s.material_after.nd
            << ", \"vd\": " << s.material_after.vd
            << ", \"source_path\": \"" << json_escape(s.material_after.source_path)
            << "\"}}";
        out << (i + 1 == system.surfaces.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

}  // namespace optiland
