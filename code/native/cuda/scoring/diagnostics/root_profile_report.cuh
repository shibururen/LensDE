#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"

namespace optiland {
struct ReportAsphereRootProfileInputs {
    DeviceConfig& cfg;
    const bool& asphere_root_profile_enabled;
    DeviceAsphereRootProfile& host_asphere_root_profile;
    bool& ok;
};
} // namespace optiland

namespace optiland {
template <class Inputs>
inline native_detail::PhaseOutcome report_asphere_root_profile(Inputs& inputs) {
    auto& cfg = inputs.cfg;
    auto& asphere_root_profile_enabled = inputs.asphere_root_profile_enabled;
    auto& host_asphere_root_profile = inputs.host_asphere_root_profile;
    auto& ok = inputs.ok;
    if (ok && asphere_root_profile_enabled) {
        const DeviceAsphereRootProfile& p = host_asphere_root_profile;
        std::fprintf(
            stderr,
            "DE-CUDA asphere root profile: segments=%d intersections=%llu "
            "initial_candidate_failures=%llu monotonic_certificates=%llu "
            "confirmation_scans=%llu confirmation_earlier_roots=%llu "
            "confirmation_sample_roots=%llu "
            "confirmation_accepted_candidates=%llu confirmation_invalid=%llu "
            "confirmation_derivative_ambiguities=%llu "
            "confirmation_residual_ambiguities=%llu "
            "confirmation_variation_ambiguities=%llu "
            "strict_prefix_fallbacks=%llu strict_prefix_found=%llu "
            "strict_prefix_no_root=%llu strict_prefix_ambiguous=%llu "
            "full_fallbacks=%llu full_fallback_successes=%llu "
            "warp_surface_visits=%llu warp_active_lane_visits=%llu "
            "confirmation_lane_visits=%llu confirmation_warp_visits=%llu "
            "mixed_confirmation_warp_visits=%llu "
            "all_confirmation_warp_visits=%llu "
            "confirmation_wasted_lane_slots=%llu "
            "confirmation_warp_lane_hist=%llu/%llu/%llu/%llu/%llu/%llu "
            "slow_lane_visits=%llu full_lane_visits=%llu "
            "slow_warp_visits=%llu full_warp_visits=%llu "
            "mixed_slow_warp_visits=%llu all_slow_warp_visits=%llu "
            "slow_wasted_lane_slots=%llu "
            "slow_warp_lane_hist=%llu/%llu/%llu/%llu/%llu/%llu\n",
            cfg.asphere_confirmation_segments,
            p.intersections,
            p.initial_candidate_failures,
            p.monotonic_certificates,
            p.confirmation_scans,
            p.confirmation_earlier_roots,
            p.confirmation_sample_roots,
            p.confirmation_accepted_candidates,
            p.confirmation_invalid,
            p.confirmation_derivative_ambiguities,
            p.confirmation_residual_ambiguities,
            p.confirmation_variation_ambiguities,
            p.strict_prefix_fallbacks,
            p.strict_prefix_found,
            p.strict_prefix_no_root,
            p.strict_prefix_ambiguous,
            p.full_fallbacks,
            p.full_fallback_successes,
            p.warp_surface_visits,
            p.warp_active_lane_visits,
            p.confirmation_lane_visits,
            p.confirmation_warp_visits,
            p.mixed_confirmation_warp_visits,
            p.all_confirmation_warp_visits,
            p.confirmation_wasted_lane_slots,
            p.confirmation_warps_1_lane,
            p.confirmation_warps_2_4_lanes,
            p.confirmation_warps_5_8_lanes,
            p.confirmation_warps_9_16_lanes,
            p.confirmation_warps_17_31_lanes,
            p.confirmation_warps_32_lanes,
            p.slow_lane_visits,
            p.full_lane_visits,
            p.slow_warp_visits,
            p.full_warp_visits,
            p.mixed_slow_warp_visits,
            p.all_slow_warp_visits,
            p.slow_wasted_lane_slots,
            p.slow_warps_1_lane,
            p.slow_warps_2_4_lanes,
            p.slow_warps_5_8_lanes,
            p.slow_warps_9_16_lanes,
            p.slow_warps_17_31_lanes,
            p.slow_warps_32_lanes);
        std::fprintf(
            stderr,
            "DE-CUDA asphere O1 path profile: "
            "topology_ray_calls=%llu topology_linear_attempts=%llu "
            "topology_linear_successes=%llu fast_candidate_successes=%llu "
            "fast_candidate_failures=%llu "
            "topology_seeded_before_attempts=%llu "
            "topology_seeded_before_successes=%llu "
            "failure_rescue_attempts=%llu failure_rescue_successes=%llu\n",
            p.o1_topology_ray_calls,
            p.o1_topology_linear_attempts,
            p.o1_topology_linear_successes,
            p.o1_fast_candidate_successes,
            p.o1_fast_candidate_failures,
            p.o1_topology_seeded_before_attempts,
            p.o1_topology_seeded_before_successes,
            p.o1_failure_rescue_attempts,
            p.o1_failure_rescue_successes);
        std::fprintf(
            stderr,
            "DE-CUDA asphere unified profile: mode=%d calls=%llu "
            "candidate_successes=%llu candidate_failures=%llu "
            "direction_certificates=%llu prefix_certificates=%llu "
            "exact_s1_prefix_attempts=%llu "
            "exact_s1_prefix_certificates=%llu "
            "exact_s1_uc2=%llu/%llu/%llu/%llu/%llu "
            "uc3=%llu/%llu/%llu/%llu/%llu "
            "adaptive_calls=%llu "
            "adaptive_found=%llu adaptive_no_root=%llu "
            "adaptive_ambiguous=%llu interval_nodes=%llu "
            "residual_prunes=%llu monotonic_nodes=%llu hybrid_solves=%llu "
            "visit_hist=%llu/%llu/%llu/%llu/%llu/%llu max_visits=%llu "
            "unbounded_calls=%llu unbounded_candidate_failures=%llu "
            "hits=%llu misses=%llu unresolved=%llu "
            "exact_s1_calls=%llu exact_s1_hits=%llu exact_s1_misses=%llu\n",
            cfg.asphere_initial_root_mode,
            p.unified_calls,
            p.unified_candidate_successes,
            p.unified_candidate_failures,
            p.unified_direction_certificates,
            p.unified_prefix_certificates,
            p.unified_exact_s1_prefix_attempts,
            p.unified_exact_s1_prefix_certificates,
            p.unified_exact_s1_uc2_attempts,
            p.unified_exact_s1_uc2_hits,
            p.unified_exact_s1_uc2_domain_misses,
            p.unified_exact_s1_uc2_endpoint_misses,
            p.unified_exact_s1_uc2_fallbacks,
            p.unified_uc3_attempts,
            p.unified_uc3_roots,
            p.unified_uc3_domain_misses,
            p.unified_uc3_endpoint_misses,
            p.unified_uc3_fallbacks,
            p.unified_adaptive_search_calls,
            p.unified_adaptive_search_found,
            p.unified_adaptive_search_no_root,
            p.unified_adaptive_search_ambiguous,
            p.unified_interval_nodes,
            p.unified_interval_residual_prunes,
            p.unified_interval_monotonic_nodes,
            p.unified_interval_hybrid_solves,
            p.unified_visits_1,
            p.unified_visits_2,
            p.unified_visits_3,
            p.unified_visits_4,
            p.unified_visits_5_8,
            p.unified_visits_9_plus,
            p.unified_max_visits,
            p.unified_unbounded_calls,
            p.unified_unbounded_candidate_failures,
            p.unified_hits,
            p.unified_misses,
            p.unified_unresolved,
            p.unified_exact_s1_calls,
            p.unified_exact_s1_hits,
            p.unified_exact_s1_misses);
        std::fprintf(
            stderr,
            "DE-CUDA asphere solver work: "
            "candidate=%llu/%llu/%llu/%llu/%llu/%llu/%llu "
            "candidate_seed_success=%llu/%llu/%llu "
            "candidate_eval_hist=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
            "prefix=%llu/%llu/%llu/%llu/%llu "
            "stationary=%llu/%llu/%llu/%llu/%llu/%llu/%llu "
            "uc3_bracket=%llu/%llu/%llu/%llu "
            "adaptive_bracket=%llu/%llu/%llu/%llu\n",
            p.unified_candidate_profiled_calls,
            p.unified_candidate_seed_attempts,
            p.unified_candidate_newton_evaluations,
            p.unified_candidate_failed_seed_newton_evaluations,
            p.unified_candidate_successful_seed_newton_evaluations,
            p.unified_candidate_terminal_residual_evaluations,
            p.unified_candidate_cacheable_successes,
            p.unified_candidate_seed1_successes,
            p.unified_candidate_seed2_successes,
            p.unified_candidate_seed3_successes,
            p.unified_candidate_eval_hist_0,
            p.unified_candidate_eval_hist_1,
            p.unified_candidate_eval_hist_2,
            p.unified_candidate_eval_hist_3,
            p.unified_candidate_eval_hist_4,
            p.unified_candidate_eval_hist_5_8,
            p.unified_candidate_eval_hist_9_16,
            p.unified_candidate_eval_hist_17_32,
            p.unified_candidate_eval_hist_33_plus,
            p.unified_prefix_attempts,
            p.unified_prefix_polish_evaluations,
            p.unified_prefix_reused_candidate_pairs,
            p.unified_prefix_endpoint_residual_evaluations,
            p.unified_prefix_monotonicity_attempts,
            p.unified_stationary_window_attempts,
            p.unified_stationary_endpoint_evaluations,
            p.unified_stationary_derivative_brackets,
            p.unified_stationary_iterations,
            p.unified_stationary_root_evaluations,
            p.unified_stationary_prefix_searches,
            p.unified_stationary_hits,
            p.unified_uc3_bracket_calls,
            p.unified_uc3_bracket_successes,
            p.unified_uc3_bracket_residual_evaluations,
            p.unified_uc3_bracket_residual_derivative_evaluations,
            p.unified_adaptive_bracket_calls,
            p.unified_adaptive_bracket_successes,
            p.unified_adaptive_bracket_residual_evaluations,
            p.unified_adaptive_bracket_residual_derivative_evaluations);
        std::fprintf(
            stderr,
            "DE-CUDA asphere UC13 strict shadow: "
            "node=%llu/%llu/%llu/%llu/%llu/%llu "
            "uc3=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
            "uc3_hist=%llu/%llu/%llu/%llu/%llu/%llu "
            "adaptive=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
            "adaptive_hist=%llu/%llu/%llu/%llu/%llu/%llu\n",
            p.unified_uc13_raw_residual_prunes,
            p.unified_uc13_mean_value_added_prunes,
            p.unified_uc13_midpoint_refinement_evaluations,
            p.unified_uc13_hi_residual_evaluations,
            p.unified_uc13_hi_unused_lower_root,
            p.unified_uc13_hi_unused_split,
            p.unified_uc13_derivative_interval_evaluations[0],
            p.unified_uc13_derivative_floor_calls[0],
            p.unified_uc13_derivative_floor_reused_calls[0],
            p.unified_uc13_shadow_proxy_qualified_calls[0],
            p.unified_uc13_shadow_point_interval_evaluations[0],
            p.unified_uc13_shadow_point_interval_valid[0],
            p.unified_uc13_shadow_certified_calls[0],
            p.unified_uc13_shadow_certified_iteration_sum[0],
            p.unified_uc13_shadow_saved_residual_derivative_evaluations[0],
            p.unified_uc13_shadow_certificate_iteration_hist[0][0],
            p.unified_uc13_shadow_certificate_iteration_hist[0][1],
            p.unified_uc13_shadow_certificate_iteration_hist[0][2],
            p.unified_uc13_shadow_certificate_iteration_hist[0][3],
            p.unified_uc13_shadow_certificate_iteration_hist[0][4],
            p.unified_uc13_shadow_certificate_iteration_hist[0][5],
            p.unified_uc13_derivative_interval_evaluations[1],
            p.unified_uc13_derivative_floor_calls[1],
            p.unified_uc13_derivative_floor_reused_calls[1],
            p.unified_uc13_shadow_proxy_qualified_calls[1],
            p.unified_uc13_shadow_point_interval_evaluations[1],
            p.unified_uc13_shadow_point_interval_valid[1],
            p.unified_uc13_shadow_certified_calls[1],
            p.unified_uc13_shadow_certified_iteration_sum[1],
            p.unified_uc13_shadow_saved_residual_derivative_evaluations[1],
            p.unified_uc13_shadow_certificate_iteration_hist[1][0],
            p.unified_uc13_shadow_certificate_iteration_hist[1][1],
            p.unified_uc13_shadow_certificate_iteration_hist[1][2],
            p.unified_uc13_shadow_certificate_iteration_hist[1][3],
            p.unified_uc13_shadow_certificate_iteration_hist[1][4],
            p.unified_uc13_shadow_certificate_iteration_hist[1][5]);
        std::fprintf(
            stderr,
            "DE-CUDA asphere UC14 feature profile: "
            "raw=%llu/%llu hi=%llu/%llu known_hi=%llu/%llu "
            "position=%llu/%llu/%llu/%llu/%llu "
            "position_hist=%llu/%llu/%llu/%llu/%llu/%llu\n",
            p.unified_uc14_raw_prune_shortcuts,
            p.unified_uc14_midpoint_refinement_skips,
            p.unified_uc14_hi_residual_evaluations,
            p.unified_uc14_hi_deferred_split_skips,
            p.unified_uc14_known_hi_reuses[0],
            p.unified_uc14_known_hi_reuses[1],
            p.unified_uc14_position_proxy_attempts,
            p.unified_uc14_position_point_interval_evaluations,
            p.unified_uc14_position_point_interval_valid,
            p.unified_uc14_position_certificates,
            p.unified_uc14_position_certificate_iteration_sum,
            p.unified_uc14_position_certificate_iteration_hist[0],
            p.unified_uc14_position_certificate_iteration_hist[1],
            p.unified_uc14_position_certificate_iteration_hist[2],
            p.unified_uc14_position_certificate_iteration_hist[3],
            p.unified_uc14_position_certificate_iteration_hist[4],
            p.unified_uc14_position_certificate_iteration_hist[5]);
        std::fprintf(
            stderr,
            "DE-CUDA asphere UC8 seed profile: "
            "identity_attempt=%llu/%llu/%llu "
            "identity_success=%llu/%llu/%llu "
            "identity_failure=%llu/%llu/%llu "
            "identity_evaluations=%llu/%llu/%llu "
            "position_attempt=%llu/%llu/%llu "
            "position_success=%llu/%llu/%llu "
            "position_failure=%llu/%llu/%llu "
            "position_evaluations=%llu/%llu/%llu "
            "sign_attempt=%llu/%llu/%llu "
            "success_eval_hist=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
            "failure_eval_hist=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
            "success_after=%llu/%llu/%llu/%llu "
            "failed_before_position=%llu/%llu/%llu "
            "failed_before_identity=%llu/%llu/%llu "
            "failed_without_candidate_success=%llu\n",
            p.unified_uc8_seed_identity_attempts[0],
            p.unified_uc8_seed_identity_attempts[1],
            p.unified_uc8_seed_identity_attempts[2],
            p.unified_uc8_seed_identity_successes[0],
            p.unified_uc8_seed_identity_successes[1],
            p.unified_uc8_seed_identity_successes[2],
            p.unified_uc8_seed_identity_failures[0],
            p.unified_uc8_seed_identity_failures[1],
            p.unified_uc8_seed_identity_failures[2],
            p.unified_uc8_seed_identity_evaluations[0],
            p.unified_uc8_seed_identity_evaluations[1],
            p.unified_uc8_seed_identity_evaluations[2],
            p.unified_uc8_seed_position_attempts[0],
            p.unified_uc8_seed_position_attempts[1],
            p.unified_uc8_seed_position_attempts[2],
            p.unified_uc8_seed_position_successes[0],
            p.unified_uc8_seed_position_successes[1],
            p.unified_uc8_seed_position_successes[2],
            p.unified_uc8_seed_position_failures[0],
            p.unified_uc8_seed_position_failures[1],
            p.unified_uc8_seed_position_failures[2],
            p.unified_uc8_seed_position_evaluations[0],
            p.unified_uc8_seed_position_evaluations[1],
            p.unified_uc8_seed_position_evaluations[2],
            p.unified_uc8_seed_sign_attempts[0],
            p.unified_uc8_seed_sign_attempts[1],
            p.unified_uc8_seed_sign_attempts[2],
            p.unified_uc8_seed_success_eval_hist[0],
            p.unified_uc8_seed_success_eval_hist[1],
            p.unified_uc8_seed_success_eval_hist[2],
            p.unified_uc8_seed_success_eval_hist[3],
            p.unified_uc8_seed_success_eval_hist[4],
            p.unified_uc8_seed_success_eval_hist[5],
            p.unified_uc8_seed_success_eval_hist[6],
            p.unified_uc8_seed_success_eval_hist[7],
            p.unified_uc8_seed_success_eval_hist[8],
            p.unified_uc8_seed_failure_eval_hist[0],
            p.unified_uc8_seed_failure_eval_hist[1],
            p.unified_uc8_seed_failure_eval_hist[2],
            p.unified_uc8_seed_failure_eval_hist[3],
            p.unified_uc8_seed_failure_eval_hist[4],
            p.unified_uc8_seed_failure_eval_hist[5],
            p.unified_uc8_seed_failure_eval_hist[6],
            p.unified_uc8_seed_failure_eval_hist[7],
            p.unified_uc8_seed_failure_eval_hist[8],
            p.unified_uc8_success_after_evaluations[0],
            p.unified_uc8_success_after_evaluations[1],
            p.unified_uc8_success_after_evaluations[2],
            p.unified_uc8_success_after_evaluations[3],
            p.unified_uc8_failed_evaluations_before_success_position[0],
            p.unified_uc8_failed_evaluations_before_success_position[1],
            p.unified_uc8_failed_evaluations_before_success_position[2],
            p.unified_uc8_failed_evaluations_before_success_identity[0],
            p.unified_uc8_failed_evaluations_before_success_identity[1],
            p.unified_uc8_failed_evaluations_before_success_identity[2],
            p.unified_uc8_failed_evaluations_without_candidate_success);
        std::fprintf(
            stderr,
            "DE-CUDA asphere UC8 first-step profile: "
            "total=%llu derivative_guarded=%llu nonfinite=%llu clamped=%llu "
            "next_invalid=%llu step_ratio_hist=%llu/%llu/%llu/%llu/%llu "
            "second_ratio_hist=%llu/%llu/%llu/%llu/%llu\n",
            p.unified_uc8_first_step_total,
            p.unified_uc8_first_step_derivative_guarded,
            p.unified_uc8_first_step_nonfinite,
            p.unified_uc8_first_step_clamped,
            p.unified_uc8_first_step_next_invalid,
            p.unified_uc8_first_step_ratio_hist[0],
            p.unified_uc8_first_step_ratio_hist[1],
            p.unified_uc8_first_step_ratio_hist[2],
            p.unified_uc8_first_step_ratio_hist[3],
            p.unified_uc8_first_step_ratio_hist[4],
            p.unified_uc8_second_residual_ratio_hist[0],
            p.unified_uc8_second_residual_ratio_hist[1],
            p.unified_uc8_second_residual_ratio_hist[2],
            p.unified_uc8_second_residual_ratio_hist[3],
            p.unified_uc8_second_residual_ratio_hist[4]);
        std::fprintf(
            stderr,
            "DE-CUDA asphere UC8 triage profile: "
            "proposals=%llu/%llu eventual_success=%llu/%llu "
            "eventual_failure=%llu/%llu remaining_success_evaluations=%llu/%llu "
            "remaining_failure_evaluations=%llu/%llu actual_aborts=%llu/%llu "
            "failed_seed_later_candidate_success=%llu/%llu "
            "failed_seed_without_candidate_success=%llu/%llu\n",
            p.unified_uc8_triage_proposals[0],
            p.unified_uc8_triage_proposals[1],
            p.unified_uc8_triage_eventual_seed_successes[0],
            p.unified_uc8_triage_eventual_seed_successes[1],
            p.unified_uc8_triage_eventual_seed_failures[0],
            p.unified_uc8_triage_eventual_seed_failures[1],
            p.unified_uc8_triage_remaining_success_evaluations[0],
            p.unified_uc8_triage_remaining_success_evaluations[1],
            p.unified_uc8_triage_remaining_failure_evaluations[0],
            p.unified_uc8_triage_remaining_failure_evaluations[1],
            p.unified_uc8_triage_actual_aborts[0],
            p.unified_uc8_triage_actual_aborts[1],
            p.unified_uc8_triage_failed_seed_later_candidate_successes[0],
            p.unified_uc8_triage_failed_seed_later_candidate_successes[1],
            p.unified_uc8_triage_failed_seed_without_candidate_successes[0],
            p.unified_uc8_triage_failed_seed_without_candidate_successes[1]);
        for (int surface_index = 0;
             surface_index < cfg.surface_count &&
             surface_index < kMaxAsphereRootProfileSurfaces;
             ++surface_index) {
            const unsigned long long proposals =
                p.unified_uc8_triage_proposals_by_surface[0][surface_index] +
                p.unified_uc8_triage_proposals_by_surface[1][surface_index];
            const unsigned long long aborts =
                p.unified_uc8_triage_actual_aborts_by_surface[0][surface_index] +
                p.unified_uc8_triage_actual_aborts_by_surface[1][surface_index];
            if (proposals == 0 && aborts == 0) {
                continue;
            }
            std::fprintf(
                stderr,
                "DE-CUDA asphere UC8 triage surface: index=%d "
                "proposals=%llu/%llu eventual_success=%llu/%llu "
                "eventual_failure=%llu/%llu "
                "remaining_failure_evaluations=%llu/%llu "
                "actual_aborts=%llu/%llu\n",
                surface_index,
                p.unified_uc8_triage_proposals_by_surface[0][surface_index],
                p.unified_uc8_triage_proposals_by_surface[1][surface_index],
                p.unified_uc8_triage_eventual_successes_by_surface[0][surface_index],
                p.unified_uc8_triage_eventual_successes_by_surface[1][surface_index],
                p.unified_uc8_triage_eventual_failures_by_surface[0][surface_index],
                p.unified_uc8_triage_eventual_failures_by_surface[1][surface_index],
                p.unified_uc8_triage_remaining_failure_evaluations_by_surface[0][surface_index],
                p.unified_uc8_triage_remaining_failure_evaluations_by_surface[1][surface_index],
                p.unified_uc8_triage_actual_aborts_by_surface[0][surface_index],
                p.unified_uc8_triage_actual_aborts_by_surface[1][surface_index]);
        }
        for (int surface_index = 0;
             surface_index < cfg.surface_count &&
             surface_index < kMaxAsphereRootProfileSurfaces;
             ++surface_index) {
            if (p.confirmation_lanes_by_surface[surface_index] == 0 &&
                p.slow_lanes_by_surface[surface_index] == 0 &&
                p.full_lanes_by_surface[surface_index] == 0) {
                continue;
            }
            std::fprintf(
                stderr,
                "DE-CUDA asphere root warp surface: index=%d "
                "confirmation_lanes=%llu mixed_confirmation_warps=%llu "
                "slow_lanes=%llu full_lanes=%llu mixed_slow_warps=%llu\n",
                surface_index,
                p.confirmation_lanes_by_surface[surface_index],
                p.mixed_confirmation_warps_by_surface[surface_index],
                p.slow_lanes_by_surface[surface_index],
                p.full_lanes_by_surface[surface_index],
                p.mixed_slow_warps_by_surface[surface_index]);
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
