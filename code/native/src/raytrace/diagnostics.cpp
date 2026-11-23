// Own process-wide root-policy controls and the public aiming diagnostic accessors.
// Raytrace units use the shared diagnostic state and exact wavelength-cache lookup helpers.
// Policy atomics are defined once here; resets and snapshots must refer to that same process state.

#include "diagnostics_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
std::atomic<AsphereRootPolicy> g_asphere_root_policy{
    kDefaultAsphereRootPolicy};
} // namespace raytrace_detail

namespace raytrace_detail {
std::atomic<bool> g_stop_aim_parallel_jacobian_enabled{false};
} // namespace raytrace_detail

namespace raytrace_detail {
bool environment_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (!value) {
        return false;
    }
    std::string text(value);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text == "1" || text == "true" || text == "yes" || text == "on";
}
} // namespace raytrace_detail

namespace raytrace_detail {
std::string normalized_mapping_mode(std::string value, const std::string& fallback) {
    if (value.empty()) {
        value = fallback;
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (value == "central" || value == "jacobian" ||
        value == "central_jacobian" || value == "central_fit_edge_correction") {
        return "central_fit";
    }
    if (value == "chief_edge" || value == "chief_edge_interp" ||
        value == "chief_edge_interpolate" || value == "cheif_edge" ||
        value == "cheif_edge_interp" || value == "cheif_edge_interpolation" ||
        value == "cheif_edge_interpolate" || value == "interpolation" ||
        value == "geometric" || value == "geometric_interpolation" ||
        value == "edge_interpolation") {
        return "chief_edge_interpolation";
    }
    return value;
}
} // namespace raytrace_detail

namespace raytrace_detail {

} // namespace raytrace_detail

namespace raytrace_detail {

} // namespace raytrace_detail

namespace raytrace_detail {

} // namespace raytrace_detail

namespace raytrace_detail {

} // namespace raytrace_detail

namespace raytrace_detail {

} // namespace raytrace_detail

namespace raytrace_detail {

} // namespace raytrace_detail

namespace raytrace_detail {

} // namespace raytrace_detail

namespace raytrace_detail {
bool cached_wavelength_equal(double a, double b) {
    return std::abs(a - b) <= 1.0e-12 * std::max({1.0, std::abs(a), std::abs(b)});
}
} // namespace raytrace_detail

namespace raytrace_detail {
const char* ray_failure_code_probe_label(RayFailureCode code) {
    switch (code) {
        case RayFailureCode::None: return "none";
        case RayFailureCode::AimNotConverged: return "aim_not_converged";
        case RayFailureCode::SurfaceMiss: return "surface_miss";
        case RayFailureCode::ApertureClip: return "aperture_clip";
        case RayFailureCode::TotalInternalReflection: return "tir";
        case RayFailureCode::NonFinite: return "non_finite";
        case RayFailureCode::BackwardPropagation: return "backward_propagation";
        case RayFailureCode::SagInvalid: return "sag_invalid";
        case RayFailureCode::GeometryInvalid: return "geometry_invalid";
    }
    return "unknown";
}
} // namespace raytrace_detail

namespace raytrace_detail {
bool lookup_wavelength_cache(
    const std::vector<std::pair<double, double>>& cache,
    double wavelength_um,
    double* value) {
    for (const auto& item : cache) {
        if (cached_wavelength_equal(item.first, wavelength_um)) {
            if (value) {
                *value = item.second;
            }
            return true;
        }
    }
    return false;
}
} // namespace raytrace_detail

namespace raytrace_detail {
double store_wavelength_cache(
    std::vector<std::pair<double, double>>& cache,
    double wavelength_um,
    double value) {
    cache.push_back({wavelength_um, value});
    return value;
}
} // namespace raytrace_detail

void set_asphere_root_policy(AsphereRootPolicy policy) {
    g_asphere_root_policy.store(policy, std::memory_order_relaxed);
}

AsphereRootPolicy asphere_root_policy() {
    return g_asphere_root_policy.load(std::memory_order_relaxed);
}

void set_stop_aim_parallel_jacobian_enabled(bool enabled) {
    g_stop_aim_parallel_jacobian_enabled.store(enabled, std::memory_order_relaxed);
}

bool stop_aim_parallel_jacobian_enabled() {
    return g_stop_aim_parallel_jacobian_enabled.load(std::memory_order_relaxed);
}

void set_ray_aiming_diagnostics_enabled(bool enabled) {
    diagnostics_state().enabled.store(enabled, std::memory_order_relaxed);
}

bool ray_aiming_diagnostics_enabled() {
    return diagnostics_active();
}

void reset_ray_aiming_diagnostics() {
    auto& state = diagnostics_state();
    state.calibrated_generate_calls.store(0, std::memory_order_relaxed);
    state.calibrated_generate_ns.store(0, std::memory_order_relaxed);
    state.calibrated_model_requests.store(0, std::memory_order_relaxed);
    state.calibrated_model_cache_hits.store(0, std::memory_order_relaxed);
    state.calibrated_model_builds.store(0, std::memory_order_relaxed);
    state.calibrated_model_build_ns.store(0, std::memory_order_relaxed);
    state.calibrated_model_valid.store(0, std::memory_order_relaxed);
    state.calibrated_model_invalid.store(0, std::memory_order_relaxed);
    state.calibrated_paraxial_chief_attempts.store(0, std::memory_order_relaxed);
    state.calibrated_paraxial_chief_accepted.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_attempts.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_seed_valid.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_accepted.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_rejected.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_singular.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_oob.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_trace_fail.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_residual_reject.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_inside_stop_accepted.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_stop_oob_reject.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_seed_extract_fail.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_ns.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_trial_rays.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_correction_calls.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_correction_iterations.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_stop_probe_calls.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_stop_probe_successes.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_stop_probe_ns.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_residual_count.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_residual_sum_scaled.store(0, std::memory_order_relaxed);
    state.calibrated_budget_chief_residual_max_scaled.store(0, std::memory_order_relaxed);
    state.calibrated_chief_anchor_requests.store(0, std::memory_order_relaxed);
    state.calibrated_chief_anchor_cache_hits.store(0, std::memory_order_relaxed);
    state.calibrated_chief_anchor_builds.store(0, std::memory_order_relaxed);
    state.calibrated_chief_anchor_valid.store(0, std::memory_order_relaxed);
    state.calibrated_chief_anchor_invalid.store(0, std::memory_order_relaxed);
    state.calibrated_chief_anchor_ns.store(0, std::memory_order_relaxed);
    state.calibrated_chief_stop_probe_calls.store(0, std::memory_order_relaxed);
    state.calibrated_chief_stop_probe_successes.store(0, std::memory_order_relaxed);
    state.calibrated_chief_stop_probe_ns.store(0, std::memory_order_relaxed);
    state.calibrated_chief_residual_count.store(0, std::memory_order_relaxed);
    state.calibrated_chief_residual_sum_scaled.store(0, std::memory_order_relaxed);
    state.calibrated_chief_residual_max_scaled.store(0, std::memory_order_relaxed);
    state.calibrated_probe_sample_calls.store(0, std::memory_order_relaxed);
    state.calibrated_probe_sample_successes.store(0, std::memory_order_relaxed);
    state.calibrated_probe_sample_ns.store(0, std::memory_order_relaxed);
    state.calibrated_central_fit_attempts.store(0, std::memory_order_relaxed);
    state.calibrated_central_fit_successes.store(0, std::memory_order_relaxed);
    state.calibrated_central_fit_ns.store(0, std::memory_order_relaxed);
    state.calibrated_grid_fit_attempts.store(0, std::memory_order_relaxed);
    state.calibrated_grid_fit_successes.store(0, std::memory_order_relaxed);
    state.calibrated_grid_fit_ns.store(0, std::memory_order_relaxed);
    state.calibrated_edge_solve_calls.store(0, std::memory_order_relaxed);
    state.calibrated_edge_correction_calls.store(0, std::memory_order_relaxed);
    state.calibrated_edge_correction_iterations.store(0, std::memory_order_relaxed);
    state.calibrated_edge_correction_ns.store(0, std::memory_order_relaxed);
    state.calibrated_edge_residual_count.store(0, std::memory_order_relaxed);
    state.calibrated_edge_residual_sum_scaled.store(0, std::memory_order_relaxed);
    state.calibrated_edge_residual_max_scaled.store(0, std::memory_order_relaxed);
    state.calibrated_interpolation_probe_calls.store(0, std::memory_order_relaxed);
    state.calibrated_interpolation_probe_successes.store(0, std::memory_order_relaxed);
    state.calibrated_interpolation_probe_seed_failures.store(0, std::memory_order_relaxed);
    state.calibrated_interpolation_probe_trace_failures.store(0, std::memory_order_relaxed);
    state.calibrated_interpolation_residual_over_limit.store(0, std::memory_order_relaxed);
    state.calibrated_interpolation_residual_count.store(0, std::memory_order_relaxed);
    state.calibrated_interpolation_residual_sum_scaled.store(0, std::memory_order_relaxed);
    state.calibrated_interpolation_residual_max_scaled.store(0, std::memory_order_relaxed);
    state.calibrated_fallback_calls.store(0, std::memory_order_relaxed);
    state.calibrated_fallback_none_calls.store(0, std::memory_order_relaxed);
    state.calibrated_fallback_other_calls.store(0, std::memory_order_relaxed);
    state.calibrated_fallback_model_invalid_calls.store(0, std::memory_order_relaxed);
    state.calibrated_fallback_seed_oob_calls.store(0, std::memory_order_relaxed);
    state.calibrated_fallback_ray_invalid_calls.store(0, std::memory_order_relaxed);
    state.calibrated_correction_calls.store(0, std::memory_order_relaxed);
    state.stop_residual_eval_calls.store(0, std::memory_order_relaxed);
    state.stop_residual_eval_successes.store(0, std::memory_order_relaxed);
    state.stop_residual_eval_ns.store(0, std::memory_order_relaxed);
    state.stop_aim_trace_residual_calls.store(0, std::memory_order_relaxed);
    state.stop_aim_trace_residual_successes.store(0, std::memory_order_relaxed);
    state.stop_aim_trace_residual_ns.store(0, std::memory_order_relaxed);
    state.stop_aim_parallel_jacobian_pairs.store(0, std::memory_order_relaxed);
    state.stop_aim_parallel_jacobian_ns.store(0, std::memory_order_relaxed);
    state.three_step_generate_calls.store(0, std::memory_order_relaxed);
    state.three_step_generate_ns.store(0, std::memory_order_relaxed);
    state.three_step_model_requests.store(0, std::memory_order_relaxed);
    state.three_step_model_cache_hits.store(0, std::memory_order_relaxed);
    state.three_step_model_builds.store(0, std::memory_order_relaxed);
    state.three_step_model_build_ns.store(0, std::memory_order_relaxed);
    state.three_step_model_valid.store(0, std::memory_order_relaxed);
    state.three_step_model_invalid.store(0, std::memory_order_relaxed);
    state.three_step_reverse_trial_calls.store(0, std::memory_order_relaxed);
    state.three_step_reverse_trial_successes.store(0, std::memory_order_relaxed);
    state.three_step_reverse_trial_ns.store(0, std::memory_order_relaxed);
    state.three_step_center_from_reverse.store(0, std::memory_order_relaxed);
    state.three_step_center_fallback_calls.store(0, std::memory_order_relaxed);
    state.three_step_center_residual_count.store(0, std::memory_order_relaxed);
    state.three_step_center_residual_sum_scaled.store(0, std::memory_order_relaxed);
    state.three_step_center_residual_max_scaled.store(0, std::memory_order_relaxed);
    state.three_step_edge_solve_calls.store(0, std::memory_order_relaxed);
    state.three_step_edge_initial_ray_calls.store(0, std::memory_order_relaxed);
    state.three_step_edge_correction_calls.store(0, std::memory_order_relaxed);
    state.three_step_edge_correction_iterations.store(0, std::memory_order_relaxed);
    state.three_step_edge_correction_ns.store(0, std::memory_order_relaxed);
    state.three_step_edge_solve_ns.store(0, std::memory_order_relaxed);
    state.three_step_edge_residual_count.store(0, std::memory_order_relaxed);
    state.three_step_edge_residual_sum_scaled.store(0, std::memory_order_relaxed);
    state.three_step_edge_residual_max_scaled.store(0, std::memory_order_relaxed);
    state.three_step_model_stop_probe_calls.store(0, std::memory_order_relaxed);
    state.three_step_model_stop_probe_successes.store(0, std::memory_order_relaxed);
    state.three_step_model_stop_probe_ns.store(0, std::memory_order_relaxed);
    state.three_step_generate_stop_probe_calls.store(0, std::memory_order_relaxed);
    state.three_step_generate_stop_probe_successes.store(0, std::memory_order_relaxed);
    state.three_step_generate_stop_probe_ns.store(0, std::memory_order_relaxed);
    state.three_step_generate_correction_calls.store(0, std::memory_order_relaxed);
    state.three_step_generate_correction_iterations.store(0, std::memory_order_relaxed);
    state.three_step_generate_correction_ns.store(0, std::memory_order_relaxed);
    state.three_step_fallback_calls.store(0, std::memory_order_relaxed);
    state.three_step_fallback_none_calls.store(0, std::memory_order_relaxed);
}

RayAimingDiagnosticsSnapshot ray_aiming_diagnostics_snapshot() {
    auto& state = diagnostics_state();
    RayAimingDiagnosticsSnapshot snapshot;
    snapshot.enabled = state.enabled.load(std::memory_order_relaxed);
    snapshot.calibrated_generate_calls =
        state.calibrated_generate_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_generate_ns =
        state.calibrated_generate_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_model_requests =
        state.calibrated_model_requests.load(std::memory_order_relaxed);
    snapshot.calibrated_model_cache_hits =
        state.calibrated_model_cache_hits.load(std::memory_order_relaxed);
    snapshot.calibrated_model_builds =
        state.calibrated_model_builds.load(std::memory_order_relaxed);
    snapshot.calibrated_model_build_ns =
        state.calibrated_model_build_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_model_valid =
        state.calibrated_model_valid.load(std::memory_order_relaxed);
    snapshot.calibrated_model_invalid =
        state.calibrated_model_invalid.load(std::memory_order_relaxed);
    snapshot.calibrated_paraxial_chief_attempts =
        state.calibrated_paraxial_chief_attempts.load(std::memory_order_relaxed);
    snapshot.calibrated_paraxial_chief_accepted =
        state.calibrated_paraxial_chief_accepted.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_attempts =
        state.calibrated_budget_chief_attempts.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_seed_valid =
        state.calibrated_budget_chief_seed_valid.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_accepted =
        state.calibrated_budget_chief_accepted.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_rejected =
        state.calibrated_budget_chief_rejected.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_singular =
        state.calibrated_budget_chief_singular.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_oob =
        state.calibrated_budget_chief_oob.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_trace_fail =
        state.calibrated_budget_chief_trace_fail.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_residual_reject =
        state.calibrated_budget_chief_residual_reject.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_inside_stop_accepted =
        state.calibrated_budget_chief_inside_stop_accepted.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_stop_oob_reject =
        state.calibrated_budget_chief_stop_oob_reject.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_seed_extract_fail =
        state.calibrated_budget_chief_seed_extract_fail.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_ns =
        state.calibrated_budget_chief_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_trial_rays =
        state.calibrated_budget_chief_trial_rays.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_correction_calls =
        state.calibrated_budget_chief_correction_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_correction_iterations =
        state.calibrated_budget_chief_correction_iterations.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_stop_probe_calls =
        state.calibrated_budget_chief_stop_probe_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_stop_probe_successes =
        state.calibrated_budget_chief_stop_probe_successes.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_stop_probe_ns =
        state.calibrated_budget_chief_stop_probe_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_residual_count =
        state.calibrated_budget_chief_residual_count.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_residual_sum_scaled =
        state.calibrated_budget_chief_residual_sum_scaled.load(std::memory_order_relaxed);
    snapshot.calibrated_budget_chief_residual_max_scaled =
        state.calibrated_budget_chief_residual_max_scaled.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_anchor_requests =
        state.calibrated_chief_anchor_requests.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_anchor_cache_hits =
        state.calibrated_chief_anchor_cache_hits.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_anchor_builds =
        state.calibrated_chief_anchor_builds.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_anchor_valid =
        state.calibrated_chief_anchor_valid.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_anchor_invalid =
        state.calibrated_chief_anchor_invalid.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_anchor_ns =
        state.calibrated_chief_anchor_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_stop_probe_calls =
        state.calibrated_chief_stop_probe_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_stop_probe_successes =
        state.calibrated_chief_stop_probe_successes.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_stop_probe_ns =
        state.calibrated_chief_stop_probe_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_residual_count =
        state.calibrated_chief_residual_count.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_residual_sum_scaled =
        state.calibrated_chief_residual_sum_scaled.load(std::memory_order_relaxed);
    snapshot.calibrated_chief_residual_max_scaled =
        state.calibrated_chief_residual_max_scaled.load(std::memory_order_relaxed);
    snapshot.calibrated_probe_sample_calls =
        state.calibrated_probe_sample_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_probe_sample_successes =
        state.calibrated_probe_sample_successes.load(std::memory_order_relaxed);
    snapshot.calibrated_probe_sample_ns =
        state.calibrated_probe_sample_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_central_fit_attempts =
        state.calibrated_central_fit_attempts.load(std::memory_order_relaxed);
    snapshot.calibrated_central_fit_successes =
        state.calibrated_central_fit_successes.load(std::memory_order_relaxed);
    snapshot.calibrated_central_fit_ns =
        state.calibrated_central_fit_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_grid_fit_attempts =
        state.calibrated_grid_fit_attempts.load(std::memory_order_relaxed);
    snapshot.calibrated_grid_fit_successes =
        state.calibrated_grid_fit_successes.load(std::memory_order_relaxed);
    snapshot.calibrated_grid_fit_ns =
        state.calibrated_grid_fit_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_edge_solve_calls =
        state.calibrated_edge_solve_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_edge_correction_calls =
        state.calibrated_edge_correction_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_edge_correction_iterations =
        state.calibrated_edge_correction_iterations.load(std::memory_order_relaxed);
    snapshot.calibrated_edge_correction_ns =
        state.calibrated_edge_correction_ns.load(std::memory_order_relaxed);
    snapshot.calibrated_edge_residual_count =
        state.calibrated_edge_residual_count.load(std::memory_order_relaxed);
    snapshot.calibrated_edge_residual_sum_scaled =
        state.calibrated_edge_residual_sum_scaled.load(std::memory_order_relaxed);
    snapshot.calibrated_edge_residual_max_scaled =
        state.calibrated_edge_residual_max_scaled.load(std::memory_order_relaxed);
    snapshot.calibrated_interpolation_probe_calls =
        state.calibrated_interpolation_probe_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_interpolation_probe_successes =
        state.calibrated_interpolation_probe_successes.load(std::memory_order_relaxed);
    snapshot.calibrated_interpolation_probe_seed_failures =
        state.calibrated_interpolation_probe_seed_failures.load(std::memory_order_relaxed);
    snapshot.calibrated_interpolation_probe_trace_failures =
        state.calibrated_interpolation_probe_trace_failures.load(std::memory_order_relaxed);
    snapshot.calibrated_interpolation_residual_over_limit =
        state.calibrated_interpolation_residual_over_limit.load(std::memory_order_relaxed);
    snapshot.calibrated_interpolation_residual_count =
        state.calibrated_interpolation_residual_count.load(std::memory_order_relaxed);
    snapshot.calibrated_interpolation_residual_sum_scaled =
        state.calibrated_interpolation_residual_sum_scaled.load(std::memory_order_relaxed);
    snapshot.calibrated_interpolation_residual_max_scaled =
        state.calibrated_interpolation_residual_max_scaled.load(std::memory_order_relaxed);
    snapshot.calibrated_fallback_calls =
        state.calibrated_fallback_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_fallback_none_calls =
        state.calibrated_fallback_none_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_fallback_other_calls =
        state.calibrated_fallback_other_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_fallback_model_invalid_calls =
        state.calibrated_fallback_model_invalid_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_fallback_seed_oob_calls =
        state.calibrated_fallback_seed_oob_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_fallback_ray_invalid_calls =
        state.calibrated_fallback_ray_invalid_calls.load(std::memory_order_relaxed);
    snapshot.calibrated_correction_calls =
        state.calibrated_correction_calls.load(std::memory_order_relaxed);
    snapshot.stop_residual_eval_calls =
        state.stop_residual_eval_calls.load(std::memory_order_relaxed);
    snapshot.stop_residual_eval_successes =
        state.stop_residual_eval_successes.load(std::memory_order_relaxed);
    snapshot.stop_residual_eval_ns =
        state.stop_residual_eval_ns.load(std::memory_order_relaxed);
    snapshot.stop_aim_trace_residual_calls =
        state.stop_aim_trace_residual_calls.load(std::memory_order_relaxed);
    snapshot.stop_aim_trace_residual_successes =
        state.stop_aim_trace_residual_successes.load(std::memory_order_relaxed);
    snapshot.stop_aim_trace_residual_ns =
        state.stop_aim_trace_residual_ns.load(std::memory_order_relaxed);
    snapshot.stop_aim_parallel_jacobian_pairs =
        state.stop_aim_parallel_jacobian_pairs.load(std::memory_order_relaxed);
    snapshot.stop_aim_parallel_jacobian_ns =
        state.stop_aim_parallel_jacobian_ns.load(std::memory_order_relaxed);
    snapshot.three_step_generate_calls =
        state.three_step_generate_calls.load(std::memory_order_relaxed);
    snapshot.three_step_generate_ns =
        state.three_step_generate_ns.load(std::memory_order_relaxed);
    snapshot.three_step_model_requests =
        state.three_step_model_requests.load(std::memory_order_relaxed);
    snapshot.three_step_model_cache_hits =
        state.three_step_model_cache_hits.load(std::memory_order_relaxed);
    snapshot.three_step_model_builds =
        state.three_step_model_builds.load(std::memory_order_relaxed);
    snapshot.three_step_model_build_ns =
        state.three_step_model_build_ns.load(std::memory_order_relaxed);
    snapshot.three_step_model_valid =
        state.three_step_model_valid.load(std::memory_order_relaxed);
    snapshot.three_step_model_invalid =
        state.three_step_model_invalid.load(std::memory_order_relaxed);
    snapshot.three_step_reverse_trial_calls =
        state.three_step_reverse_trial_calls.load(std::memory_order_relaxed);
    snapshot.three_step_reverse_trial_successes =
        state.three_step_reverse_trial_successes.load(std::memory_order_relaxed);
    snapshot.three_step_reverse_trial_ns =
        state.three_step_reverse_trial_ns.load(std::memory_order_relaxed);
    snapshot.three_step_center_from_reverse =
        state.three_step_center_from_reverse.load(std::memory_order_relaxed);
    snapshot.three_step_center_fallback_calls =
        state.three_step_center_fallback_calls.load(std::memory_order_relaxed);
    snapshot.three_step_center_residual_count =
        state.three_step_center_residual_count.load(std::memory_order_relaxed);
    snapshot.three_step_center_residual_sum_scaled =
        state.three_step_center_residual_sum_scaled.load(std::memory_order_relaxed);
    snapshot.three_step_center_residual_max_scaled =
        state.three_step_center_residual_max_scaled.load(std::memory_order_relaxed);
    snapshot.three_step_edge_solve_calls =
        state.three_step_edge_solve_calls.load(std::memory_order_relaxed);
    snapshot.three_step_edge_initial_ray_calls =
        state.three_step_edge_initial_ray_calls.load(std::memory_order_relaxed);
    snapshot.three_step_edge_correction_calls =
        state.three_step_edge_correction_calls.load(std::memory_order_relaxed);
    snapshot.three_step_edge_correction_iterations =
        state.three_step_edge_correction_iterations.load(std::memory_order_relaxed);
    snapshot.three_step_edge_correction_ns =
        state.three_step_edge_correction_ns.load(std::memory_order_relaxed);
    snapshot.three_step_edge_solve_ns =
        state.three_step_edge_solve_ns.load(std::memory_order_relaxed);
    snapshot.three_step_edge_residual_count =
        state.three_step_edge_residual_count.load(std::memory_order_relaxed);
    snapshot.three_step_edge_residual_sum_scaled =
        state.three_step_edge_residual_sum_scaled.load(std::memory_order_relaxed);
    snapshot.three_step_edge_residual_max_scaled =
        state.three_step_edge_residual_max_scaled.load(std::memory_order_relaxed);
    snapshot.three_step_model_stop_probe_calls =
        state.three_step_model_stop_probe_calls.load(std::memory_order_relaxed);
    snapshot.three_step_model_stop_probe_successes =
        state.three_step_model_stop_probe_successes.load(std::memory_order_relaxed);
    snapshot.three_step_model_stop_probe_ns =
        state.three_step_model_stop_probe_ns.load(std::memory_order_relaxed);
    snapshot.three_step_generate_stop_probe_calls =
        state.three_step_generate_stop_probe_calls.load(std::memory_order_relaxed);
    snapshot.three_step_generate_stop_probe_successes =
        state.three_step_generate_stop_probe_successes.load(std::memory_order_relaxed);
    snapshot.three_step_generate_stop_probe_ns =
        state.three_step_generate_stop_probe_ns.load(std::memory_order_relaxed);
    snapshot.three_step_generate_correction_calls =
        state.three_step_generate_correction_calls.load(std::memory_order_relaxed);
    snapshot.three_step_generate_correction_iterations =
        state.three_step_generate_correction_iterations.load(std::memory_order_relaxed);
    snapshot.three_step_generate_correction_ns =
        state.three_step_generate_correction_ns.load(std::memory_order_relaxed);
    snapshot.three_step_fallback_calls =
        state.three_step_fallback_calls.load(std::memory_order_relaxed);
    snapshot.three_step_fallback_none_calls =
        state.three_step_fallback_none_calls.load(std::memory_order_relaxed);
    return snapshot;
}

} // namespace optiland
