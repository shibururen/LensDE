// Serialize and print the process aiming diagnostic snapshot after command execution.
// Counters, residual summaries, and explicitly labeled durations come from the raytrace diagnostics API.
// Keep scientific counts/residuals separate from wall-clock fields and retain their existing scaling.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

constexpr double kDiagResidualScale = 1.0e9;

void write_diag_int(std::ostream& out, const char* name, long long value, bool* first) {
    if (!*first) {
        out << ",\n";
    }
    *first = false;
    out << "  \"" << name << "\": " << value;
}

void write_diag_seconds(std::ostream& out, const char* name, long long ns, bool* first) {
    if (!*first) {
        out << ",\n";
    }
    *first = false;
    out << "  \"" << name << "\": ";
    write_json_number_or_null(out, ns_to_seconds(ns));
}

double diag_residual_value(long long scaled) {
    return static_cast<double>(scaled) / kDiagResidualScale;
}

double diag_residual_mean(long long sum_scaled, long long count) {
    if (count <= 0) {
        return std::numeric_limits<double>::infinity();
    }
    return diag_residual_value(sum_scaled) / static_cast<double>(count);
}

void write_diag_number(std::ostream& out, const char* name, double value, bool* first) {
    if (!*first) {
        out << ",\n";
    }
    *first = false;
    out << "  \"" << name << "\": ";
    write_json_number_or_null(out, value);
}

void write_ray_aiming_diagnostics_json(
    const RayAimingDiagnosticsSnapshot& diag,
    const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        return;
    }
    bool first = true;
    out << "{\n";
    out << "  \"enabled\": " << (diag.enabled ? "true" : "false");
    first = false;
    write_diag_int(out, "calibrated_generate_calls", diag.calibrated_generate_calls, &first);
    write_diag_seconds(out, "calibrated_generate_seconds", diag.calibrated_generate_ns, &first);
    write_diag_int(out, "calibrated_model_requests", diag.calibrated_model_requests, &first);
    write_diag_int(out, "calibrated_model_cache_hits", diag.calibrated_model_cache_hits, &first);
    write_diag_int(out, "calibrated_model_builds", diag.calibrated_model_builds, &first);
    write_diag_seconds(out, "calibrated_model_build_seconds", diag.calibrated_model_build_ns, &first);
    write_diag_int(out, "calibrated_model_valid", diag.calibrated_model_valid, &first);
    write_diag_int(out, "calibrated_model_invalid", diag.calibrated_model_invalid, &first);
    write_diag_int(out, "calibrated_paraxial_chief_attempts",
                   diag.calibrated_paraxial_chief_attempts, &first);
    write_diag_int(out, "calibrated_paraxial_chief_accepted",
                   diag.calibrated_paraxial_chief_accepted, &first);
    write_diag_int(out, "calibrated_budget_chief_attempts",
                   diag.calibrated_budget_chief_attempts, &first);
    write_diag_int(out, "calibrated_budget_chief_seed_valid",
                   diag.calibrated_budget_chief_seed_valid, &first);
    write_diag_int(out, "calibrated_budget_chief_accepted",
                   diag.calibrated_budget_chief_accepted, &first);
    write_diag_int(out, "calibrated_budget_chief_rejected",
                   diag.calibrated_budget_chief_rejected, &first);
    write_diag_int(out, "calibrated_budget_chief_singular",
                   diag.calibrated_budget_chief_singular, &first);
    write_diag_int(out, "calibrated_budget_chief_oob",
                   diag.calibrated_budget_chief_oob, &first);
    write_diag_int(out, "calibrated_budget_chief_trace_fail",
                   diag.calibrated_budget_chief_trace_fail, &first);
    write_diag_int(out, "calibrated_budget_chief_residual_reject",
                   diag.calibrated_budget_chief_residual_reject, &first);
    write_diag_int(out, "calibrated_budget_chief_inside_stop_accepted",
                   diag.calibrated_budget_chief_inside_stop_accepted, &first);
    write_diag_int(out, "calibrated_budget_chief_stop_oob_reject",
                   diag.calibrated_budget_chief_stop_oob_reject, &first);
    write_diag_int(out, "calibrated_budget_chief_seed_extract_fail",
                   diag.calibrated_budget_chief_seed_extract_fail, &first);
    write_diag_seconds(out, "calibrated_budget_chief_seconds",
                       diag.calibrated_budget_chief_ns, &first);
    write_diag_int(out, "calibrated_budget_chief_trial_rays",
                   diag.calibrated_budget_chief_trial_rays, &first);
    write_diag_int(out, "calibrated_budget_chief_correction_calls",
                   diag.calibrated_budget_chief_correction_calls, &first);
    write_diag_int(out, "calibrated_budget_chief_correction_iterations",
                   diag.calibrated_budget_chief_correction_iterations, &first);
    write_diag_int(out, "calibrated_budget_chief_stop_probe_calls",
                   diag.calibrated_budget_chief_stop_probe_calls, &first);
    write_diag_int(out, "calibrated_budget_chief_stop_probe_successes",
                   diag.calibrated_budget_chief_stop_probe_successes, &first);
    write_diag_seconds(out, "calibrated_budget_chief_stop_probe_seconds",
                       diag.calibrated_budget_chief_stop_probe_ns, &first);
    write_diag_int(out, "calibrated_budget_chief_residual_count",
                   diag.calibrated_budget_chief_residual_count, &first);
    write_diag_number(out, "calibrated_budget_chief_residual_mean",
                      diag_residual_mean(diag.calibrated_budget_chief_residual_sum_scaled,
                                         diag.calibrated_budget_chief_residual_count),
                      &first);
    write_diag_number(out, "calibrated_budget_chief_residual_max",
                      diag_residual_value(diag.calibrated_budget_chief_residual_max_scaled),
                      &first);
    write_diag_int(out, "calibrated_chief_anchor_requests",
                   diag.calibrated_chief_anchor_requests, &first);
    write_diag_int(out, "calibrated_chief_anchor_cache_hits",
                   diag.calibrated_chief_anchor_cache_hits, &first);
    write_diag_int(out, "calibrated_chief_anchor_builds",
                   diag.calibrated_chief_anchor_builds, &first);
    write_diag_int(out, "calibrated_chief_anchor_valid",
                   diag.calibrated_chief_anchor_valid, &first);
    write_diag_int(out, "calibrated_chief_anchor_invalid",
                   diag.calibrated_chief_anchor_invalid, &first);
    write_diag_seconds(out, "calibrated_chief_anchor_seconds",
                       diag.calibrated_chief_anchor_ns, &first);
    write_diag_int(out, "calibrated_chief_stop_probe_calls",
                   diag.calibrated_chief_stop_probe_calls, &first);
    write_diag_int(out, "calibrated_chief_stop_probe_successes",
                   diag.calibrated_chief_stop_probe_successes, &first);
    write_diag_seconds(out, "calibrated_chief_stop_probe_seconds",
                       diag.calibrated_chief_stop_probe_ns, &first);
    write_diag_int(out, "calibrated_chief_residual_count",
                   diag.calibrated_chief_residual_count, &first);
    write_diag_number(out, "calibrated_chief_residual_mean",
                      diag_residual_mean(diag.calibrated_chief_residual_sum_scaled,
                                         diag.calibrated_chief_residual_count),
                      &first);
    write_diag_number(out, "calibrated_chief_residual_max",
                      diag_residual_value(diag.calibrated_chief_residual_max_scaled),
                      &first);
    write_diag_int(out, "calibrated_probe_sample_calls",
                   diag.calibrated_probe_sample_calls, &first);
    write_diag_int(out, "calibrated_probe_sample_successes",
                   diag.calibrated_probe_sample_successes, &first);
    write_diag_seconds(out, "calibrated_probe_sample_seconds",
                       diag.calibrated_probe_sample_ns, &first);
    write_diag_int(out, "calibrated_central_fit_attempts",
                   diag.calibrated_central_fit_attempts, &first);
    write_diag_int(out, "calibrated_central_fit_successes",
                   diag.calibrated_central_fit_successes, &first);
    write_diag_seconds(out, "calibrated_central_fit_seconds",
                       diag.calibrated_central_fit_ns, &first);
    write_diag_int(out, "calibrated_grid_fit_attempts",
                   diag.calibrated_grid_fit_attempts, &first);
    write_diag_int(out, "calibrated_grid_fit_successes",
                   diag.calibrated_grid_fit_successes, &first);
    write_diag_seconds(out, "calibrated_grid_fit_seconds",
                       diag.calibrated_grid_fit_ns, &first);
    write_diag_int(out, "calibrated_edge_solve_calls",
                   diag.calibrated_edge_solve_calls, &first);
    write_diag_int(out, "calibrated_edge_correction_calls",
                   diag.calibrated_edge_correction_calls, &first);
    write_diag_int(out, "calibrated_edge_correction_iterations",
                   diag.calibrated_edge_correction_iterations, &first);
    write_diag_seconds(out, "calibrated_edge_correction_seconds",
                       diag.calibrated_edge_correction_ns, &first);
    write_diag_int(out, "calibrated_edge_residual_count",
                   diag.calibrated_edge_residual_count, &first);
    write_diag_number(out, "calibrated_edge_residual_mean",
                      diag_residual_mean(diag.calibrated_edge_residual_sum_scaled,
                                         diag.calibrated_edge_residual_count),
                      &first);
    write_diag_number(out, "calibrated_edge_residual_max",
                       diag_residual_value(diag.calibrated_edge_residual_max_scaled),
                       &first);
    write_diag_int(out, "calibrated_interpolation_probe_calls",
                   diag.calibrated_interpolation_probe_calls, &first);
    write_diag_int(out, "calibrated_interpolation_probe_successes",
                   diag.calibrated_interpolation_probe_successes, &first);
    write_diag_int(out, "calibrated_interpolation_probe_seed_failures",
                   diag.calibrated_interpolation_probe_seed_failures, &first);
    write_diag_int(out, "calibrated_interpolation_probe_trace_failures",
                   diag.calibrated_interpolation_probe_trace_failures, &first);
    write_diag_int(out, "calibrated_interpolation_residual_over_limit",
                   diag.calibrated_interpolation_residual_over_limit, &first);
    write_diag_int(out, "calibrated_interpolation_residual_count",
                   diag.calibrated_interpolation_residual_count, &first);
    write_diag_number(
        out,
        "calibrated_interpolation_residual_mean",
        diag_residual_mean(diag.calibrated_interpolation_residual_sum_scaled,
                           diag.calibrated_interpolation_residual_count),
        &first);
    write_diag_number(
        out,
        "calibrated_interpolation_residual_max",
        diag_residual_value(diag.calibrated_interpolation_residual_max_scaled),
        &first);
    write_diag_int(out, "calibrated_fallback_calls", diag.calibrated_fallback_calls, &first);
    write_diag_int(out, "calibrated_fallback_none_calls",
                   diag.calibrated_fallback_none_calls, &first);
    write_diag_int(out, "calibrated_fallback_other_calls",
                   diag.calibrated_fallback_other_calls, &first);
    write_diag_int(out, "calibrated_fallback_model_invalid_calls",
                   diag.calibrated_fallback_model_invalid_calls, &first);
    write_diag_int(out, "calibrated_fallback_seed_oob_calls",
                   diag.calibrated_fallback_seed_oob_calls, &first);
    write_diag_int(out, "calibrated_fallback_ray_invalid_calls",
                   diag.calibrated_fallback_ray_invalid_calls, &first);
    write_diag_int(out, "calibrated_correction_calls",
                   diag.calibrated_correction_calls, &first);
    write_diag_int(out, "stop_residual_eval_calls",
                   diag.stop_residual_eval_calls, &first);
    write_diag_int(out, "stop_residual_eval_successes",
                   diag.stop_residual_eval_successes, &first);
    write_diag_seconds(out, "stop_residual_eval_seconds",
                       diag.stop_residual_eval_ns, &first);
    write_diag_int(out, "stop_aim_trace_residual_calls",
                   diag.stop_aim_trace_residual_calls, &first);
    write_diag_int(out, "stop_aim_trace_residual_successes",
                   diag.stop_aim_trace_residual_successes, &first);
    write_diag_seconds(out, "stop_aim_trace_residual_seconds",
                       diag.stop_aim_trace_residual_ns, &first);
    write_diag_int(out, "stop_aim_parallel_jacobian_pairs",
                   diag.stop_aim_parallel_jacobian_pairs, &first);
    write_diag_seconds(out, "stop_aim_parallel_jacobian_seconds",
                       diag.stop_aim_parallel_jacobian_ns, &first);
    write_diag_int(out, "three_step_generate_calls", diag.three_step_generate_calls, &first);
    write_diag_seconds(out, "three_step_generate_seconds",
                       diag.three_step_generate_ns, &first);
    write_diag_int(out, "three_step_model_requests", diag.three_step_model_requests, &first);
    write_diag_int(out, "three_step_model_cache_hits", diag.three_step_model_cache_hits, &first);
    write_diag_int(out, "three_step_model_builds", diag.three_step_model_builds, &first);
    write_diag_seconds(out, "three_step_model_build_seconds",
                       diag.three_step_model_build_ns, &first);
    write_diag_int(out, "three_step_model_valid", diag.three_step_model_valid, &first);
    write_diag_int(out, "three_step_model_invalid", diag.three_step_model_invalid, &first);
    write_diag_int(out, "three_step_reverse_trial_calls",
                   diag.three_step_reverse_trial_calls, &first);
    write_diag_int(out, "three_step_reverse_trial_successes",
                   diag.three_step_reverse_trial_successes, &first);
    write_diag_seconds(out, "three_step_reverse_trial_seconds",
                       diag.three_step_reverse_trial_ns, &first);
    write_diag_int(out, "three_step_center_from_reverse",
                   diag.three_step_center_from_reverse, &first);
    write_diag_int(out, "three_step_center_fallback_calls",
                   diag.three_step_center_fallback_calls, &first);
    write_diag_int(out, "three_step_center_residual_count",
                   diag.three_step_center_residual_count, &first);
    write_diag_number(out, "three_step_center_residual_mean",
                      diag_residual_mean(diag.three_step_center_residual_sum_scaled,
                                         diag.three_step_center_residual_count),
                      &first);
    write_diag_number(out, "three_step_center_residual_max",
                      diag_residual_value(diag.three_step_center_residual_max_scaled),
                      &first);
    write_diag_int(out, "three_step_edge_solve_calls",
                   diag.three_step_edge_solve_calls, &first);
    write_diag_int(out, "three_step_edge_initial_ray_calls",
                   diag.three_step_edge_initial_ray_calls, &first);
    write_diag_int(out, "three_step_edge_correction_calls",
                   diag.three_step_edge_correction_calls, &first);
    write_diag_int(out, "three_step_edge_correction_iterations",
                   diag.three_step_edge_correction_iterations, &first);
    write_diag_seconds(out, "three_step_edge_correction_seconds",
                       diag.three_step_edge_correction_ns, &first);
    write_diag_seconds(out, "three_step_edge_solve_seconds",
                       diag.three_step_edge_solve_ns, &first);
    write_diag_int(out, "three_step_edge_residual_count",
                   diag.three_step_edge_residual_count, &first);
    write_diag_number(out, "three_step_edge_residual_mean",
                      diag_residual_mean(diag.three_step_edge_residual_sum_scaled,
                                         diag.three_step_edge_residual_count),
                      &first);
    write_diag_number(out, "three_step_edge_residual_max",
                      diag_residual_value(diag.three_step_edge_residual_max_scaled),
                      &first);
    write_diag_int(out, "three_step_model_stop_probe_calls",
                   diag.three_step_model_stop_probe_calls, &first);
    write_diag_int(out, "three_step_model_stop_probe_successes",
                   diag.three_step_model_stop_probe_successes, &first);
    write_diag_seconds(out, "three_step_model_stop_probe_seconds",
                       diag.three_step_model_stop_probe_ns, &first);
    write_diag_int(out, "three_step_generate_stop_probe_calls",
                   diag.three_step_generate_stop_probe_calls, &first);
    write_diag_int(out, "three_step_generate_stop_probe_successes",
                   diag.three_step_generate_stop_probe_successes, &first);
    write_diag_seconds(out, "three_step_generate_stop_probe_seconds",
                       diag.three_step_generate_stop_probe_ns, &first);
    write_diag_int(out, "three_step_generate_correction_calls",
                   diag.three_step_generate_correction_calls, &first);
    write_diag_int(out, "three_step_generate_correction_iterations",
                   diag.three_step_generate_correction_iterations, &first);
    write_diag_seconds(out, "three_step_generate_correction_seconds",
                       diag.three_step_generate_correction_ns, &first);
    write_diag_int(out, "three_step_fallback_calls", diag.three_step_fallback_calls, &first);
    write_diag_int(out, "three_step_fallback_none_calls",
                   diag.three_step_fallback_none_calls, &first);
    out << "\n}\n";
}

void print_ray_aiming_diagnostics(const std::string& out_dir) {
    if (!ray_aiming_diagnostics_enabled()) {
        return;
    }
    const RayAimingDiagnosticsSnapshot diag = ray_aiming_diagnostics_snapshot();
    write_ray_aiming_diagnostics_json(diag, join_path(out_dir, "aiming_diagnostics.json"));
    std::cout << "Ray aiming diagnostics:\n";
    std::cout << "  calibrated generate calls " << diag.calibrated_generate_calls
              << ", generate " << ns_to_seconds(diag.calibrated_generate_ns) << " s\n";
    std::cout << "  model requests " << diag.calibrated_model_requests
              << ", builds " << diag.calibrated_model_builds
              << ", cache hits " << diag.calibrated_model_cache_hits
              << ", build time " << ns_to_seconds(diag.calibrated_model_build_ns) << " s\n";
    std::cout << "  model valid " << diag.calibrated_model_valid
              << ", invalid " << diag.calibrated_model_invalid << "\n";
    std::cout << "  paraxial chief accepted " << diag.calibrated_paraxial_chief_accepted
              << "/" << diag.calibrated_paraxial_chief_attempts
              << ", budget chief accepted " << diag.calibrated_budget_chief_accepted
              << "/" << diag.calibrated_budget_chief_attempts
              << ", budget time " << ns_to_seconds(diag.calibrated_budget_chief_ns) << " s\n";
    std::cout << "  budget chief seed valid " << diag.calibrated_budget_chief_seed_valid
              << ", rejected " << diag.calibrated_budget_chief_rejected
              << ", singular " << diag.calibrated_budget_chief_singular
              << ", oob " << diag.calibrated_budget_chief_oob
              << ", trace fail " << diag.calibrated_budget_chief_trace_fail
              << ", residual reject " << diag.calibrated_budget_chief_residual_reject
              << ", inside-stop accepted " << diag.calibrated_budget_chief_inside_stop_accepted
              << ", stop-oob reject " << diag.calibrated_budget_chief_stop_oob_reject
              << ", seed extract fail " << diag.calibrated_budget_chief_seed_extract_fail << "\n";
    std::cout << "  budget chief trials " << diag.calibrated_budget_chief_trial_rays
              << ", corrections " << diag.calibrated_budget_chief_correction_calls
              << " calls/" << diag.calibrated_budget_chief_correction_iterations
              << " iters, stop probes "
              << diag.calibrated_budget_chief_stop_probe_successes
              << "/" << diag.calibrated_budget_chief_stop_probe_calls
              << ", stop probe time "
              << ns_to_seconds(diag.calibrated_budget_chief_stop_probe_ns) << " s\n";
    if (diag.calibrated_budget_chief_residual_count > 0) {
        std::cout << "  budget chief accepted residual mean/max "
                  << diag_residual_mean(diag.calibrated_budget_chief_residual_sum_scaled,
                                        diag.calibrated_budget_chief_residual_count)
                  << "/" << diag_residual_value(diag.calibrated_budget_chief_residual_max_scaled)
                  << "\n";
    }
    std::cout << "  chief anchor requests " << diag.calibrated_chief_anchor_requests
              << ", builds " << diag.calibrated_chief_anchor_builds
              << ", time " << ns_to_seconds(diag.calibrated_chief_anchor_ns) << " s\n";
    std::cout << "  chief stop probes "
              << diag.calibrated_chief_stop_probe_successes
              << "/" << diag.calibrated_chief_stop_probe_calls
              << ", time " << ns_to_seconds(diag.calibrated_chief_stop_probe_ns) << " s\n";
    if (diag.calibrated_chief_residual_count > 0) {
        std::cout << "  calibrated chief residual mean/max "
                  << diag_residual_mean(diag.calibrated_chief_residual_sum_scaled,
                                        diag.calibrated_chief_residual_count)
                  << "/" << diag_residual_value(diag.calibrated_chief_residual_max_scaled)
                  << "\n";
    }
    std::cout << "  central fit " << diag.calibrated_central_fit_successes
              << "/" << diag.calibrated_central_fit_attempts
              << ", grid fit " << diag.calibrated_grid_fit_successes
              << "/" << diag.calibrated_grid_fit_attempts << "\n";
    std::cout << "  calibrated edge solves " << diag.calibrated_edge_solve_calls
              << ", corrections " << diag.calibrated_edge_correction_calls
              << " calls/" << diag.calibrated_edge_correction_iterations
              << " iters, correction time "
              << ns_to_seconds(diag.calibrated_edge_correction_ns) << " s\n";
    if (diag.calibrated_edge_residual_count > 0) {
        std::cout << "  calibrated edge residual mean/max "
                  << diag_residual_mean(diag.calibrated_edge_residual_sum_scaled,
                                        diag.calibrated_edge_residual_count)
                  << "/" << diag_residual_value(diag.calibrated_edge_residual_max_scaled)
                  << "\n";
    }
    if (diag.calibrated_interpolation_probe_calls > 0) {
        std::cout << "  calibrated interpolation probes "
                  << diag.calibrated_interpolation_probe_successes
                  << "/" << diag.calibrated_interpolation_probe_calls
                  << ", seed/trace failures "
                  << diag.calibrated_interpolation_probe_seed_failures
                  << "/" << diag.calibrated_interpolation_probe_trace_failures
                  << ", over reference limit "
                  << diag.calibrated_interpolation_residual_over_limit << "\n";
    }
    if (diag.calibrated_interpolation_residual_count > 0) {
        std::cout << "  calibrated interpolation residual mean/max "
                  << diag_residual_mean(
                         diag.calibrated_interpolation_residual_sum_scaled,
                         diag.calibrated_interpolation_residual_count)
                  << "/" << diag_residual_value(
                         diag.calibrated_interpolation_residual_max_scaled)
                  << "\n";
    }
    std::cout << "  probe samples " << diag.calibrated_probe_sample_successes
              << "/" << diag.calibrated_probe_sample_calls
              << ", probe time " << ns_to_seconds(diag.calibrated_probe_sample_ns) << " s"
              << ", fallback " << diag.calibrated_fallback_calls
              << " (none " << diag.calibrated_fallback_none_calls << ")\n";
    std::cout << "  stop residual evals "
              << diag.stop_residual_eval_successes
              << "/" << diag.stop_residual_eval_calls
              << ", time " << ns_to_seconds(diag.stop_residual_eval_ns) << " s\n";
    std::cout << "  stop-aim residual traces "
              << diag.stop_aim_trace_residual_successes
              << "/" << diag.stop_aim_trace_residual_calls
              << ", time " << ns_to_seconds(diag.stop_aim_trace_residual_ns) << " s\n";
    if (diag.stop_aim_parallel_jacobian_pairs > 0) {
        std::cout << "  parallel stop-aim Jacobian pairs "
                  << diag.stop_aim_parallel_jacobian_pairs
                  << ", wall "
                  << ns_to_seconds(diag.stop_aim_parallel_jacobian_ns)
                  << " s\n";
    }
    if (diag.three_step_generate_calls > 0 ||
        diag.three_step_model_requests > 0 ||
        diag.three_step_fallback_calls > 0) {
        std::cout << "  three-step generate calls " << diag.three_step_generate_calls
                  << ", generate " << ns_to_seconds(diag.three_step_generate_ns) << " s\n";
        std::cout << "  three-step models requests " << diag.three_step_model_requests
                  << ", builds " << diag.three_step_model_builds
                  << ", cache hits " << diag.three_step_model_cache_hits
                  << ", build time " << ns_to_seconds(diag.three_step_model_build_ns) << " s\n";
        std::cout << "  three-step model valid " << diag.three_step_model_valid
                  << ", invalid " << diag.three_step_model_invalid
                  << ", center reverse " << diag.three_step_center_from_reverse
                  << ", center fallback " << diag.three_step_center_fallback_calls << "\n";
        if (diag.three_step_center_residual_count > 0) {
            std::cout << "  three-step center residual mean/max "
                      << diag_residual_mean(diag.three_step_center_residual_sum_scaled,
                                            diag.three_step_center_residual_count)
                      << "/" << diag_residual_value(diag.three_step_center_residual_max_scaled)
                      << "\n";
        }
        std::cout << "  three-step reverse trials "
                  << diag.three_step_reverse_trial_successes
                  << "/" << diag.three_step_reverse_trial_calls
                  << ", time " << ns_to_seconds(diag.three_step_reverse_trial_ns) << " s\n";
        std::cout << "  three-step edge solves " << diag.three_step_edge_solve_calls
                  << ", initial rays " << diag.three_step_edge_initial_ray_calls
                  << ", corrections " << diag.three_step_edge_correction_calls
                  << " calls/" << diag.three_step_edge_correction_iterations
                  << " iters, correction time "
                  << ns_to_seconds(diag.three_step_edge_correction_ns)
                  << " s, solve time "
                  << ns_to_seconds(diag.three_step_edge_solve_ns) << " s\n";
        if (diag.three_step_edge_residual_count > 0) {
            std::cout << "  three-step edge residual mean/max "
                      << diag_residual_mean(diag.three_step_edge_residual_sum_scaled,
                                            diag.three_step_edge_residual_count)
                      << "/" << diag_residual_value(diag.three_step_edge_residual_max_scaled)
                      << "\n";
        }
        std::cout << "  three-step model stop probes "
                  << diag.three_step_model_stop_probe_successes
                  << "/" << diag.three_step_model_stop_probe_calls
                  << ", time " << ns_to_seconds(diag.three_step_model_stop_probe_ns) << " s\n";
        std::cout << "  three-step generate stop probes "
                  << diag.three_step_generate_stop_probe_successes
                  << "/" << diag.three_step_generate_stop_probe_calls
                  << ", time " << ns_to_seconds(diag.three_step_generate_stop_probe_ns)
                  << " s, corrections "
                  << diag.three_step_generate_correction_calls
                  << " calls/" << diag.three_step_generate_correction_iterations
                  << " iters, fallback " << diag.three_step_fallback_calls
                  << " (none " << diag.three_step_fallback_none_calls << ")\n";
    }
}

} } }
