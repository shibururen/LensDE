#pragma once
#include "../prelude.cuh"
#include "../configuration/runtime_options.cuh"
#include "../contracts/constants.cuh"
#include "asphere_profile_records.cuh"

namespace optiland {
struct ReportAsphereFeasibilityProfileInputs {
    const bool& asphere_feasibility_profile_enabled;
    AsphereFeasibilityHostTiming& asphere_feasibility_timing;
    const bool& enable_calibrated_safeguarded_recovery;
    DeviceAsphereRootProfile& host_asphere_root_profile;
    DeviceAsphereRootProfile& root_profile_after_geometry;
    DeviceAsphereRootProfile& root_profile_after_initial_models;
    DeviceAsphereRootProfile& root_profile_after_aperture;
    DeviceAsphereRootProfile& root_profile_after_final_models;
    DeviceAsphereFeasibilityProfile& host_asphere_feasibility_profile;
    bool& ok;
};
} // namespace optiland

namespace optiland {
template <class Inputs>
inline native_detail::PhaseOutcome report_asphere_feasibility_profile(Inputs& inputs) {
    auto& asphere_feasibility_profile_enabled = inputs.asphere_feasibility_profile_enabled;
    auto& asphere_feasibility_timing = inputs.asphere_feasibility_timing;
    auto& enable_calibrated_safeguarded_recovery = inputs.enable_calibrated_safeguarded_recovery;
    auto& host_asphere_root_profile = inputs.host_asphere_root_profile;
    auto& root_profile_after_geometry = inputs.root_profile_after_geometry;
    auto& root_profile_after_initial_models = inputs.root_profile_after_initial_models;
    auto& root_profile_after_aperture = inputs.root_profile_after_aperture;
    auto& root_profile_after_final_models = inputs.root_profile_after_final_models;
    auto& host_asphere_feasibility_profile = inputs.host_asphere_feasibility_profile;
    auto& ok = inputs.ok;
    if (ok && asphere_feasibility_profile_enabled) {
        const DeviceAsphereFeasibilityProfile& p =
            host_asphere_feasibility_profile;
        std::fprintf(
            stderr,
            "DE-CUDA asphere feasibility profile: "
            "domain_calls=%llu domain_asphere_calls=%llu "
            "domain_reuse_hits=%llu domain_recomputes=%llu "
            "domain_interval_nodes=%llu domain_leaf_tests=%llu "
            "domain_bisection_iterations=%llu "
            "mark_models_built=%llu mark_center_models_built=%llu "
            "mark_models_valid=%llu mark_models_tagged=%llu "
            "mark_center_models_tagged=%llu mark_edge_bits=%llu "
            "recovery_models_built=%llu "
            "recovery_center_models_built=%llu "
            "recovery_models_valid=%llu skipped_valid_models=%llu "
            "inactive_wave_models=%llu\n",
            p.domain_calls,
            p.domain_asphere_calls,
            p.domain_reuse_hits,
            p.domain_recomputes,
            p.domain_interval_nodes,
            p.domain_leaf_tests,
            p.domain_bisection_iterations,
            p.mark_models_built,
            p.mark_center_models_built,
            p.mark_models_valid,
            p.mark_models_tagged,
            p.mark_center_models_tagged,
            p.mark_edge_bits,
            p.recovery_models_built,
            p.recovery_center_models_built,
            p.recovery_models_valid,
            p.skipped_valid_models,
            p.inactive_wave_models);
        std::fprintf(
            stderr,
            "DE-CUDA asphere feasibility timing: "
            "safeguarded_recovery_enabled=%d "
            "domain=%.9f s domain_launches=%d "
            "calibrated_mark=%.9f s calibrated_mark_launches=%d "
            "calibrated_recovery=%.9f s "
            "calibrated_recovery_launches=%d\n",
            enable_calibrated_safeguarded_recovery ? 1 : 0,
            asphere_feasibility_timing.domain_seconds,
            asphere_feasibility_timing.domain_kernel_launches,
            asphere_feasibility_timing.calibrated_mark_seconds,
            asphere_feasibility_timing.calibrated_mark_kernel_launches,
            asphere_feasibility_timing.calibrated_recovery_seconds,
            asphere_feasibility_timing.calibrated_recovery_kernel_launches);
        auto print_root_phase = [](
                                    const char* phase,
                                    const DeviceAsphereRootProfile& before,
                                    const DeviceAsphereRootProfile& after) {
            auto delta = [](unsigned long long lo, unsigned long long hi) {
                return hi >= lo ? hi - lo : 0ULL;
            };
            std::fprintf(
                stderr,
                "DE-CUDA asphere feasibility root phase: phase=%s "
                "intersections=%llu initial_candidate_failures=%llu "
                "monotonic_certificates=%llu confirmation_scans=%llu "
                "strict_prefix_fallbacks=%llu full_fallbacks=%llu "
                "piecewise_prefix_attempts=%llu "
                "piecewise_hybrid_iterations=%llu slow_lane_visits=%llu "
                "full_lane_visits=%llu unified_calls=%llu "
                "unified_direction_certificates=%llu "
                "unified_prefix_certificates=%llu "
                "unified_exact_s1_prefix_certificates=%llu "
                "unified_uc3=%llu/%llu/%llu/%llu/%llu "
                "unified_adaptive_calls=%llu unified_interval_nodes=%llu "
                "unified_hits=%llu unified_misses=%llu "
                "unified_unresolved=%llu\n",
                phase,
                delta(before.intersections, after.intersections),
                delta(
                    before.initial_candidate_failures,
                    after.initial_candidate_failures),
                delta(
                    before.monotonic_certificates,
                    after.monotonic_certificates),
                delta(before.confirmation_scans, after.confirmation_scans),
                delta(
                    before.strict_prefix_fallbacks,
                    after.strict_prefix_fallbacks),
                delta(before.full_fallbacks, after.full_fallbacks),
                delta(
                    before.piecewise_prefix_attempts,
                    after.piecewise_prefix_attempts),
                delta(
                    before.piecewise_hybrid_iterations,
                    after.piecewise_hybrid_iterations),
                delta(before.slow_lane_visits, after.slow_lane_visits),
                delta(before.full_lane_visits, after.full_lane_visits),
                delta(before.unified_calls, after.unified_calls),
                delta(
                    before.unified_direction_certificates,
                    after.unified_direction_certificates),
                delta(
                    before.unified_prefix_certificates,
                    after.unified_prefix_certificates),
                delta(
                    before.unified_exact_s1_prefix_certificates,
                    after.unified_exact_s1_prefix_certificates),
                delta(before.unified_uc3_attempts, after.unified_uc3_attempts),
                delta(before.unified_uc3_roots, after.unified_uc3_roots),
                delta(
                    before.unified_uc3_domain_misses,
                    after.unified_uc3_domain_misses),
                delta(
                    before.unified_uc3_endpoint_misses,
                    after.unified_uc3_endpoint_misses),
                delta(before.unified_uc3_fallbacks, after.unified_uc3_fallbacks),
                delta(
                    before.unified_adaptive_search_calls,
                    after.unified_adaptive_search_calls),
                delta(
                    before.unified_interval_nodes,
                    after.unified_interval_nodes),
                delta(before.unified_hits, after.unified_hits),
                delta(before.unified_misses, after.unified_misses),
                delta(before.unified_unresolved, after.unified_unresolved));
            std::fprintf(
                stderr,
                "DE-CUDA asphere solver phase: phase=%s "
                "candidate=%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                "prefix=%llu/%llu/%llu/%llu/%llu "
                "stationary=%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                "uc3_bracket=%llu/%llu/%llu/%llu "
                "adaptive_bracket=%llu/%llu/%llu/%llu\n",
                phase,
                delta(
                    before.unified_candidate_profiled_calls,
                    after.unified_candidate_profiled_calls),
                delta(
                    before.unified_candidate_seed_attempts,
                    after.unified_candidate_seed_attempts),
                delta(
                    before.unified_candidate_newton_evaluations,
                    after.unified_candidate_newton_evaluations),
                delta(
                    before.unified_candidate_failed_seed_newton_evaluations,
                    after.unified_candidate_failed_seed_newton_evaluations),
                delta(
                    before.unified_candidate_successful_seed_newton_evaluations,
                    after.unified_candidate_successful_seed_newton_evaluations),
                delta(
                    before.unified_candidate_terminal_residual_evaluations,
                    after.unified_candidate_terminal_residual_evaluations),
                delta(
                    before.unified_candidate_cacheable_successes,
                    after.unified_candidate_cacheable_successes),
                delta(
                    before.unified_prefix_attempts,
                    after.unified_prefix_attempts),
                delta(
                    before.unified_prefix_polish_evaluations,
                    after.unified_prefix_polish_evaluations),
                delta(
                    before.unified_prefix_reused_candidate_pairs,
                    after.unified_prefix_reused_candidate_pairs),
                delta(
                    before.unified_prefix_endpoint_residual_evaluations,
                    after.unified_prefix_endpoint_residual_evaluations),
                delta(
                    before.unified_prefix_monotonicity_attempts,
                    after.unified_prefix_monotonicity_attempts),
                delta(
                    before.unified_stationary_window_attempts,
                    after.unified_stationary_window_attempts),
                delta(
                    before.unified_stationary_endpoint_evaluations,
                    after.unified_stationary_endpoint_evaluations),
                delta(
                    before.unified_stationary_derivative_brackets,
                    after.unified_stationary_derivative_brackets),
                delta(
                    before.unified_stationary_iterations,
                    after.unified_stationary_iterations),
                delta(
                    before.unified_stationary_root_evaluations,
                    after.unified_stationary_root_evaluations),
                delta(
                    before.unified_stationary_prefix_searches,
                    after.unified_stationary_prefix_searches),
                delta(
                    before.unified_stationary_hits,
                    after.unified_stationary_hits),
                delta(
                    before.unified_uc3_bracket_calls,
                    after.unified_uc3_bracket_calls),
                delta(
                    before.unified_uc3_bracket_successes,
                    after.unified_uc3_bracket_successes),
                delta(
                    before.unified_uc3_bracket_residual_evaluations,
                    after.unified_uc3_bracket_residual_evaluations),
                delta(
                    before
                        .unified_uc3_bracket_residual_derivative_evaluations,
                    after.unified_uc3_bracket_residual_derivative_evaluations),
                delta(
                    before.unified_adaptive_bracket_calls,
                    after.unified_adaptive_bracket_calls),
                delta(
                    before.unified_adaptive_bracket_successes,
                    after.unified_adaptive_bracket_successes),
                delta(
                    before.unified_adaptive_bracket_residual_evaluations,
                    after.unified_adaptive_bracket_residual_evaluations),
                delta(
                    before
                        .unified_adaptive_bracket_residual_derivative_evaluations,
                    after
                        .unified_adaptive_bracket_residual_derivative_evaluations));
            std::fprintf(
                stderr,
                "DE-CUDA asphere UC13 strict shadow phase: phase=%s "
                "node=%llu/%llu/%llu/%llu/%llu/%llu "
                "uc3=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                "uc3_hist=%llu/%llu/%llu/%llu/%llu/%llu "
                "adaptive=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                "adaptive_hist=%llu/%llu/%llu/%llu/%llu/%llu\n",
                phase,
                delta(before.unified_uc13_raw_residual_prunes,
                      after.unified_uc13_raw_residual_prunes),
                delta(before.unified_uc13_mean_value_added_prunes,
                      after.unified_uc13_mean_value_added_prunes),
                delta(before.unified_uc13_midpoint_refinement_evaluations,
                      after.unified_uc13_midpoint_refinement_evaluations),
                delta(before.unified_uc13_hi_residual_evaluations,
                      after.unified_uc13_hi_residual_evaluations),
                delta(before.unified_uc13_hi_unused_lower_root,
                      after.unified_uc13_hi_unused_lower_root),
                delta(before.unified_uc13_hi_unused_split,
                      after.unified_uc13_hi_unused_split),
                delta(before.unified_uc13_derivative_interval_evaluations[0],
                      after.unified_uc13_derivative_interval_evaluations[0]),
                delta(before.unified_uc13_derivative_floor_calls[0],
                      after.unified_uc13_derivative_floor_calls[0]),
                delta(before.unified_uc13_derivative_floor_reused_calls[0],
                      after.unified_uc13_derivative_floor_reused_calls[0]),
                delta(before.unified_uc13_shadow_proxy_qualified_calls[0],
                      after.unified_uc13_shadow_proxy_qualified_calls[0]),
                delta(before.unified_uc13_shadow_point_interval_evaluations[0],
                      after.unified_uc13_shadow_point_interval_evaluations[0]),
                delta(before.unified_uc13_shadow_point_interval_valid[0],
                      after.unified_uc13_shadow_point_interval_valid[0]),
                delta(before.unified_uc13_shadow_certified_calls[0],
                      after.unified_uc13_shadow_certified_calls[0]),
                delta(before.unified_uc13_shadow_certified_iteration_sum[0],
                      after.unified_uc13_shadow_certified_iteration_sum[0]),
                delta(
                    before
                        .unified_uc13_shadow_saved_residual_derivative_evaluations[0],
                    after
                        .unified_uc13_shadow_saved_residual_derivative_evaluations[0]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[0][0],
                      after.unified_uc13_shadow_certificate_iteration_hist[0][0]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[0][1],
                      after.unified_uc13_shadow_certificate_iteration_hist[0][1]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[0][2],
                      after.unified_uc13_shadow_certificate_iteration_hist[0][2]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[0][3],
                      after.unified_uc13_shadow_certificate_iteration_hist[0][3]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[0][4],
                      after.unified_uc13_shadow_certificate_iteration_hist[0][4]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[0][5],
                      after.unified_uc13_shadow_certificate_iteration_hist[0][5]),
                delta(before.unified_uc13_derivative_interval_evaluations[1],
                      after.unified_uc13_derivative_interval_evaluations[1]),
                delta(before.unified_uc13_derivative_floor_calls[1],
                      after.unified_uc13_derivative_floor_calls[1]),
                delta(before.unified_uc13_derivative_floor_reused_calls[1],
                      after.unified_uc13_derivative_floor_reused_calls[1]),
                delta(before.unified_uc13_shadow_proxy_qualified_calls[1],
                      after.unified_uc13_shadow_proxy_qualified_calls[1]),
                delta(before.unified_uc13_shadow_point_interval_evaluations[1],
                      after.unified_uc13_shadow_point_interval_evaluations[1]),
                delta(before.unified_uc13_shadow_point_interval_valid[1],
                      after.unified_uc13_shadow_point_interval_valid[1]),
                delta(before.unified_uc13_shadow_certified_calls[1],
                      after.unified_uc13_shadow_certified_calls[1]),
                delta(before.unified_uc13_shadow_certified_iteration_sum[1],
                      after.unified_uc13_shadow_certified_iteration_sum[1]),
                delta(
                    before
                        .unified_uc13_shadow_saved_residual_derivative_evaluations[1],
                    after
                        .unified_uc13_shadow_saved_residual_derivative_evaluations[1]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[1][0],
                      after.unified_uc13_shadow_certificate_iteration_hist[1][0]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[1][1],
                      after.unified_uc13_shadow_certificate_iteration_hist[1][1]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[1][2],
                      after.unified_uc13_shadow_certificate_iteration_hist[1][2]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[1][3],
                      after.unified_uc13_shadow_certificate_iteration_hist[1][3]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[1][4],
                      after.unified_uc13_shadow_certificate_iteration_hist[1][4]),
                delta(before.unified_uc13_shadow_certificate_iteration_hist[1][5],
                      after.unified_uc13_shadow_certificate_iteration_hist[1][5]));
            std::fprintf(
                stderr,
                "DE-CUDA asphere UC14 feature phase: phase=%s "
                "raw=%llu/%llu hi=%llu/%llu known_hi=%llu/%llu "
                "position=%llu/%llu/%llu/%llu/%llu "
                "position_hist=%llu/%llu/%llu/%llu/%llu/%llu\n",
                phase,
                delta(before.unified_uc14_raw_prune_shortcuts,
                      after.unified_uc14_raw_prune_shortcuts),
                delta(before.unified_uc14_midpoint_refinement_skips,
                      after.unified_uc14_midpoint_refinement_skips),
                delta(before.unified_uc14_hi_residual_evaluations,
                      after.unified_uc14_hi_residual_evaluations),
                delta(before.unified_uc14_hi_deferred_split_skips,
                      after.unified_uc14_hi_deferred_split_skips),
                delta(before.unified_uc14_known_hi_reuses[0],
                      after.unified_uc14_known_hi_reuses[0]),
                delta(before.unified_uc14_known_hi_reuses[1],
                      after.unified_uc14_known_hi_reuses[1]),
                delta(before.unified_uc14_position_proxy_attempts,
                      after.unified_uc14_position_proxy_attempts),
                delta(before.unified_uc14_position_point_interval_evaluations,
                      after.unified_uc14_position_point_interval_evaluations),
                delta(before.unified_uc14_position_point_interval_valid,
                      after.unified_uc14_position_point_interval_valid),
                delta(before.unified_uc14_position_certificates,
                      after.unified_uc14_position_certificates),
                delta(before.unified_uc14_position_certificate_iteration_sum,
                      after.unified_uc14_position_certificate_iteration_sum),
                delta(before.unified_uc14_position_certificate_iteration_hist[0],
                      after.unified_uc14_position_certificate_iteration_hist[0]),
                delta(before.unified_uc14_position_certificate_iteration_hist[1],
                      after.unified_uc14_position_certificate_iteration_hist[1]),
                delta(before.unified_uc14_position_certificate_iteration_hist[2],
                      after.unified_uc14_position_certificate_iteration_hist[2]),
                delta(before.unified_uc14_position_certificate_iteration_hist[3],
                      after.unified_uc14_position_certificate_iteration_hist[3]),
                delta(before.unified_uc14_position_certificate_iteration_hist[4],
                      after.unified_uc14_position_certificate_iteration_hist[4]),
                delta(before.unified_uc14_position_certificate_iteration_hist[5],
                      after.unified_uc14_position_certificate_iteration_hist[5]));
            std::fprintf(
                stderr,
                "DE-CUDA asphere UC8 triage phase: phase=%s "
                "proposals=%llu/%llu eventual_success=%llu/%llu "
                "eventual_failure=%llu/%llu "
                "remaining_success_evaluations=%llu/%llu "
                "remaining_failure_evaluations=%llu/%llu "
                "actual_aborts=%llu/%llu\n",
                phase,
                delta(
                    before.unified_uc8_triage_proposals[0],
                    after.unified_uc8_triage_proposals[0]),
                delta(
                    before.unified_uc8_triage_proposals[1],
                    after.unified_uc8_triage_proposals[1]),
                delta(
                    before.unified_uc8_triage_eventual_seed_successes[0],
                    after.unified_uc8_triage_eventual_seed_successes[0]),
                delta(
                    before.unified_uc8_triage_eventual_seed_successes[1],
                    after.unified_uc8_triage_eventual_seed_successes[1]),
                delta(
                    before.unified_uc8_triage_eventual_seed_failures[0],
                    after.unified_uc8_triage_eventual_seed_failures[0]),
                delta(
                    before.unified_uc8_triage_eventual_seed_failures[1],
                    after.unified_uc8_triage_eventual_seed_failures[1]),
                delta(
                    before.unified_uc8_triage_remaining_success_evaluations[0],
                    after.unified_uc8_triage_remaining_success_evaluations[0]),
                delta(
                    before.unified_uc8_triage_remaining_success_evaluations[1],
                    after.unified_uc8_triage_remaining_success_evaluations[1]),
                delta(
                    before.unified_uc8_triage_remaining_failure_evaluations[0],
                    after.unified_uc8_triage_remaining_failure_evaluations[0]),
                delta(
                    before.unified_uc8_triage_remaining_failure_evaluations[1],
                    after.unified_uc8_triage_remaining_failure_evaluations[1]),
                delta(
                    before.unified_uc8_triage_actual_aborts[0],
                    after.unified_uc8_triage_actual_aborts[0]),
                delta(
                    before.unified_uc8_triage_actual_aborts[1],
                    after.unified_uc8_triage_actual_aborts[1]));
        };
        const DeviceAsphereRootProfile zero_root_profile{};
        print_root_phase(
            "geometry",
            zero_root_profile,
            root_profile_after_geometry);
        print_root_phase(
            "initial_models",
            root_profile_after_geometry,
            root_profile_after_initial_models);
        print_root_phase(
            "aperture",
            root_profile_after_initial_models,
            root_profile_after_aperture);
        print_root_phase(
            "final_models",
            root_profile_after_aperture,
            root_profile_after_final_models);
        print_root_phase(
            "spot_merit",
            root_profile_after_final_models,
            host_asphere_root_profile);
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
