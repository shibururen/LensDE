#pragma once

#include "optiland/asphere_domain_contract.hpp"

#include <cuda_runtime.h>

namespace optiland::de_cuda_detail {

inline constexpr int kMaxDeviceAsphereCoefficients = 8;
inline constexpr int kMaxDeviceMaterialIndices = 8;
inline constexpr int kMaxAsphereRootProfileSurfaces = 64;
inline constexpr int kAsphereUc14RawPruneBit = 1 << 16;
inline constexpr int kAsphereUc14DeferHiBit = 1 << 17;
inline constexpr int kAsphereUc14KnownHiBit = 1 << 18;
inline constexpr int kAsphereUc14PositionCertificateBit = 1 << 19;
// Release-only cache populated after candidate variables are materialized.
// It replaces the per-ray scan of up to eight double coefficients without
// changing the exact `coefficient != 0.0` predicate.
inline constexpr int kAspherePolynomialDepartureBit = 1 << 20;
// Release-only exact predicate for a materialized even-asphere with a finite,
// evaluator-valid conic radius.  Hot scalar evaluators use it to avoid
// repeating kind and floating-point radius classification for every sample.
inline constexpr int kAsphereMaterializedFiniteConicBit = 1 << 21;

struct DeviceSurface {
    int kind = 0;
    int is_stop = 0;
    int reflective = 0;
    int clear_aperture_fixed = 0;
    int vignetting_aperture_excluded = 0;
    int mechanical_aperture_fixed = 0;
    // Isolated UC7 evaluator switches.  This occupies the four-byte padding
    // that previously preceded radius, so DeviceSurface does not grow.
    // bit 0: residual-only polynomial Horner, bit 1: fused residual/
    // derivative polynomial Horner.  UC8 additionally uses bits 2/3 for its
    // two isolated seed-triage ablations.  UC9 uses bit 4 for inline and bit
    // 5 for noinline fused strict-node interval evaluation.  UC10 uses bit 6
    // to additionally reuse the radial derivative interval already produced
    // while bounding radial squared.  UC12 uses bit 7 to reuse the affine x/y
    // endpoint values within that radial helper.  Bits 8-14 hold a profiler-
    // only surface index.  UC14 uses bits 16-19 for raw-prune-first, deferred
    // upper endpoint, known-upper pass-through and the adaptive positional
    // certificate.  The UC10 release specialization uses bit 20 to cache
    // whether any materialized polynomial coefficient is non-zero and bit 21
    // to cache the exact finite-conic predicate. All production and pre-UC7
    // modes use 0.
    int asphere_polynomial_eval_mode = 0;
    double radius = 1.0e100;
    double conic = 0.0;
    int coeff_count = 0;
    double coeffs[kMaxDeviceAsphereCoefficients] = {};
    int newton_max_iter = 80;
    double newton_tol = 1.0e-10;
    double thickness = 0.0;
    double semi_aperture = 0.0;
    double mechanical_aperture = 0.0;
    double aperture_radius = 0.0;
    // Squared radius of the materialized, slope-limited usable branch.
    // A value at or above the device infinity sentinel keeps unrestricted
    // legacy behavior.
    double slope_root_domain_radius_squared = 1.0e300;
    double slope_root_domain_cached_reference_radius = -1.0;
    double slope_root_domain_cached_clear_radius = -1.0;
    // 0: not materialized by the experimental contract route, 1: finite
    // vertex-connected domain, 2: proven unbounded, 3: unresolved/invalid.
    // Legacy policies deliberately ignore this field.
    int unified_slope_domain_status = 0;
    double nd = 1.0;
    double vd = 0.0;
    int material_index_offset = -1;
    int evaluator_material_index_mask = 0;
    double evaluator_material_indices[kMaxDeviceMaterialIndices] = {};
};

// Per-ray, register/local diagnostic state for the isolated UC6 solver
// experiment.  These records never contain global pointers and are updated
// without atomics; the unified caller folds them into the optional global
// profile once per logical operation.
struct DeviceAsphereCandidateDiagnostic {
    int seed_attempts = 0;
    int newton_evaluations = 0;
    int failed_seed_newton_evaluations = 0;
    int successful_seed_newton_evaluations = 0;
    int terminal_residual_evaluations = 0;
    int successful_seed = 0;
    int cacheable_success = 0;
    double cached_t = 0.0;
    double cached_residual = 0.0;
    double cached_derivative = 0.0;
};

// Per-seed scratch for the isolated UC8 diagnostic/ablation.  The classifier
// only observes values that the ordinary Newton iteration has already
// computed.  Dry-run mode fills this record without changing control flow;
// enabled ablations set actual_abort_mask and let the existing later seeds and
// strict unified fallback take over.
struct DeviceAsphereSeedAttemptDiagnostic {
    int triage_trigger_mask = 0;
    int triage_actual_abort_mask = 0;
    int triage_trigger_evaluations[2] = {};
    int first_step_observed = 0;
    // bit 0: derivative guard, bit 1: non-finite raw step,
    // bit 2: clamped step, bit 3: invalid/non-forward next t.
    int first_step_class = 0;
    // |raw step| / step_bound: <=.25, <=.75, <=1, <=2, >2.
    int first_step_ratio_bucket = -1;
    // |F(t_1)| / |F(t_0)|: <=.1, <=.5, <=.9, <=1.1, >1.1.
    int second_residual_ratio_bucket = -1;
};

struct DeviceAspherePrefixDiagnostic {
    int polish_evaluations = 0;
    int reused_candidate_pairs = 0;
    int endpoint_residual_evaluations = 0;
    int monotonicity_attempts = 0;
};

struct DeviceAsphereBracketDiagnostic {
    int calls = 0;
    int successes = 0;
    int residual_evaluations = 0;
    int residual_derivative_evaluations = 0;
};

// Optional, profiler-only counters for the production first-forward-root
// policy.  The scorer leaves the pointer null unless explicitly requested, so
// ordinary optimization runs do not pay for device atomics or extra logging.
struct DeviceAsphereRootProfile {
    unsigned long long intersections = 0;
    unsigned long long initial_candidate_failures = 0;
    unsigned long long monotonic_certificates = 0;
    unsigned long long confirmation_scans = 0;
    unsigned long long confirmation_earlier_roots = 0;
    unsigned long long confirmation_sample_roots = 0;
    unsigned long long confirmation_accepted_candidates = 0;
    unsigned long long confirmation_invalid = 0;
    unsigned long long confirmation_derivative_ambiguities = 0;
    unsigned long long confirmation_residual_ambiguities = 0;
    unsigned long long confirmation_variation_ambiguities = 0;
    unsigned long long strict_prefix_fallbacks = 0;
    unsigned long long strict_prefix_found = 0;
    unsigned long long strict_prefix_no_root = 0;
    unsigned long long strict_prefix_ambiguous = 0;
    unsigned long long full_fallbacks = 0;
    unsigned long long full_fallback_successes = 0;
    unsigned long long piecewise_prefix_attempts = 0;
    unsigned long long piecewise_prefix_no_root = 0;
    unsigned long long piecewise_prefix_found = 0;
    unsigned long long piecewise_prefix_ambiguous = 0;
    unsigned long long piecewise_segments = 0;
    unsigned long long piecewise_residual_prunes = 0;
    unsigned long long piecewise_monotonic_segments = 0;
    unsigned long long piecewise_hybrid_solves = 0;
    unsigned long long piecewise_hybrid_iterations = 0;
    unsigned long long o1_topology_ray_calls = 0;
    unsigned long long o1_topology_linear_attempts = 0;
    unsigned long long o1_topology_linear_successes = 0;
    unsigned long long o1_fast_candidate_successes = 0;
    unsigned long long o1_fast_candidate_failures = 0;
    unsigned long long o1_topology_seeded_before_attempts = 0;
    unsigned long long o1_topology_seeded_before_successes = 0;
    unsigned long long o1_failure_rescue_attempts = 0;
    unsigned long long o1_failure_rescue_successes = 0;
    unsigned long long unified_calls = 0;
    unsigned long long unified_candidate_successes = 0;
    unsigned long long unified_candidate_failures = 0;
    unsigned long long unified_direction_certificates = 0;
    unsigned long long unified_prefix_certificates = 0;
    unsigned long long unified_exact_s1_prefix_attempts = 0;
    unsigned long long unified_exact_s1_prefix_certificates = 0;
    unsigned long long unified_exact_s1_uc2_attempts = 0;
    unsigned long long unified_exact_s1_uc2_hits = 0;
    unsigned long long unified_exact_s1_uc2_domain_misses = 0;
    unsigned long long unified_exact_s1_uc2_endpoint_misses = 0;
    unsigned long long unified_exact_s1_uc2_fallbacks = 0;
    unsigned long long unified_uc3_attempts = 0;
    unsigned long long unified_uc3_roots = 0;
    unsigned long long unified_uc3_domain_misses = 0;
    unsigned long long unified_uc3_endpoint_misses = 0;
    unsigned long long unified_uc3_fallbacks = 0;
    unsigned long long unified_adaptive_search_calls = 0;
    unsigned long long unified_adaptive_search_found = 0;
    unsigned long long unified_adaptive_search_no_root = 0;
    unsigned long long unified_adaptive_search_ambiguous = 0;
    unsigned long long unified_interval_nodes = 0;
    unsigned long long unified_interval_residual_prunes = 0;
    unsigned long long unified_interval_monotonic_nodes = 0;
    unsigned long long unified_interval_hybrid_solves = 0;
    unsigned long long unified_visits_1 = 0;
    unsigned long long unified_visits_2 = 0;
    unsigned long long unified_visits_3 = 0;
    unsigned long long unified_visits_4 = 0;
    unsigned long long unified_visits_5_8 = 0;
    unsigned long long unified_visits_9_plus = 0;
    unsigned long long unified_max_visits = 0;
    unsigned long long unified_unbounded_calls = 0;
    unsigned long long unified_unbounded_candidate_failures = 0;
    unsigned long long unified_hits = 0;
    unsigned long long unified_misses = 0;
    unsigned long long unified_unresolved = 0;
    unsigned long long unified_exact_s1_calls = 0;
    unsigned long long unified_exact_s1_hits = 0;
    unsigned long long unified_exact_s1_misses = 0;
    unsigned long long unified_candidate_profiled_calls = 0;
    unsigned long long unified_candidate_seed_attempts = 0;
    unsigned long long unified_candidate_newton_evaluations = 0;
    unsigned long long unified_candidate_failed_seed_newton_evaluations = 0;
    unsigned long long unified_candidate_successful_seed_newton_evaluations = 0;
    unsigned long long unified_candidate_terminal_residual_evaluations = 0;
    unsigned long long unified_candidate_cacheable_successes = 0;
    unsigned long long unified_candidate_seed1_successes = 0;
    unsigned long long unified_candidate_seed2_successes = 0;
    unsigned long long unified_candidate_seed3_successes = 0;
    unsigned long long unified_candidate_eval_hist_0 = 0;
    unsigned long long unified_candidate_eval_hist_1 = 0;
    unsigned long long unified_candidate_eval_hist_2 = 0;
    unsigned long long unified_candidate_eval_hist_3 = 0;
    unsigned long long unified_candidate_eval_hist_4 = 0;
    unsigned long long unified_candidate_eval_hist_5_8 = 0;
    unsigned long long unified_candidate_eval_hist_9_16 = 0;
    unsigned long long unified_candidate_eval_hist_17_32 = 0;
    unsigned long long unified_candidate_eval_hist_33_plus = 0;
    // UC8 profile-only seed identity/order/outcome accounting.  Identities
    // are plane, first conic root and second conic root; positions are the
    // existing ascending-t order.  Sign classes are negative, near-zero and
    // positive.  The evaluation histograms use the same nine bins as the
    // aggregate candidate histogram above.
    unsigned long long unified_uc8_seed_identity_attempts[3] = {};
    unsigned long long unified_uc8_seed_identity_successes[3] = {};
    unsigned long long unified_uc8_seed_identity_failures[3] = {};
    unsigned long long unified_uc8_seed_identity_evaluations[3] = {};
    unsigned long long unified_uc8_seed_position_attempts[3] = {};
    unsigned long long unified_uc8_seed_position_successes[3] = {};
    unsigned long long unified_uc8_seed_position_failures[3] = {};
    unsigned long long unified_uc8_seed_position_evaluations[3] = {};
    unsigned long long unified_uc8_seed_sign_attempts[3] = {};
    unsigned long long unified_uc8_seed_success_eval_hist[9] = {};
    unsigned long long unified_uc8_seed_failure_eval_hist[9] = {};
    unsigned long long unified_uc8_success_after_evaluations[4] = {};
    unsigned long long unified_uc8_failed_evaluations_before_success_position[3] = {};
    unsigned long long unified_uc8_failed_evaluations_before_success_identity[3] = {};
    unsigned long long unified_uc8_failed_evaluations_without_candidate_success = 0;
    unsigned long long unified_uc8_first_step_total = 0;
    unsigned long long unified_uc8_first_step_derivative_guarded = 0;
    unsigned long long unified_uc8_first_step_nonfinite = 0;
    unsigned long long unified_uc8_first_step_clamped = 0;
    unsigned long long unified_uc8_first_step_next_invalid = 0;
    unsigned long long unified_uc8_first_step_ratio_hist[5] = {};
    unsigned long long unified_uc8_second_residual_ratio_hist[5] = {};
    // Classifier 0: after evaluation 4, all first three moves were clamped
    // and the best residual has not halved.  Classifier 1: after evaluation 8,
    // the best residual has not fallen below one quarter of its initial value.
    unsigned long long unified_uc8_triage_proposals[2] = {};
    unsigned long long unified_uc8_triage_eventual_seed_successes[2] = {};
    unsigned long long unified_uc8_triage_eventual_seed_failures[2] = {};
    unsigned long long unified_uc8_triage_remaining_success_evaluations[2] = {};
    unsigned long long unified_uc8_triage_remaining_failure_evaluations[2] = {};
    unsigned long long unified_uc8_triage_actual_aborts[2] = {};
    unsigned long long unified_uc8_triage_failed_seed_later_candidate_successes[2] = {};
    unsigned long long unified_uc8_triage_failed_seed_without_candidate_successes[2] = {};
    unsigned long long unified_uc8_triage_proposals_by_surface[2][
        kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long unified_uc8_triage_eventual_successes_by_surface[2][
        kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long unified_uc8_triage_eventual_failures_by_surface[2][
        kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long unified_uc8_triage_remaining_failure_evaluations_by_surface[2][
        kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long unified_uc8_triage_actual_aborts_by_surface[2][
        kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long unified_prefix_attempts = 0;
    unsigned long long unified_prefix_polish_evaluations = 0;
    unsigned long long unified_prefix_reused_candidate_pairs = 0;
    unsigned long long unified_prefix_endpoint_residual_evaluations = 0;
    unsigned long long unified_prefix_monotonicity_attempts = 0;
    unsigned long long unified_stationary_window_attempts = 0;
    unsigned long long unified_stationary_endpoint_evaluations = 0;
    unsigned long long unified_stationary_derivative_brackets = 0;
    unsigned long long unified_stationary_iterations = 0;
    unsigned long long unified_stationary_root_evaluations = 0;
    unsigned long long unified_stationary_prefix_searches = 0;
    unsigned long long unified_stationary_hits = 0;
    unsigned long long unified_uc3_bracket_calls = 0;
    unsigned long long unified_uc3_bracket_successes = 0;
    unsigned long long unified_uc3_bracket_residual_evaluations = 0;
    unsigned long long unified_uc3_bracket_residual_derivative_evaluations = 0;
    unsigned long long unified_adaptive_bracket_calls = 0;
    unsigned long long unified_adaptive_bracket_successes = 0;
    unsigned long long unified_adaptive_bracket_residual_evaluations = 0;
    unsigned long long unified_adaptive_bracket_residual_derivative_evaluations = 0;
    // UC13 strict-node and bracket shadow diagnosis.  Bracket origin index 0
    // is the UC3 candidate-failure classifier; index 1 is the adaptive strict
    // traversal.  Certificate histogram bins are 1, 2, 3-4, 5-8, 9-16, 17+.
    unsigned long long unified_uc13_raw_residual_prunes = 0;
    unsigned long long unified_uc13_mean_value_added_prunes = 0;
    unsigned long long unified_uc13_midpoint_refinement_evaluations = 0;
    unsigned long long unified_uc13_hi_residual_evaluations = 0;
    unsigned long long unified_uc13_hi_unused_lower_root = 0;
    unsigned long long unified_uc13_hi_unused_split = 0;
    unsigned long long unified_uc13_derivative_interval_evaluations[2] = {};
    unsigned long long unified_uc13_derivative_floor_calls[2] = {};
    unsigned long long unified_uc13_derivative_floor_reused_calls[2] = {};
    unsigned long long unified_uc13_shadow_proxy_qualified_calls[2] = {};
    unsigned long long unified_uc13_shadow_point_interval_evaluations[2] = {};
    unsigned long long unified_uc13_shadow_point_interval_valid[2] = {};
    unsigned long long unified_uc13_shadow_certified_calls[2] = {};
    unsigned long long unified_uc13_shadow_certified_iteration_sum[2] = {};
    unsigned long long
        unified_uc13_shadow_saved_residual_derivative_evaluations[2] = {};
    unsigned long long unified_uc13_shadow_certificate_iteration_hist[2][6] = {};
    // UC14 actual feature accounting.  These counters are written only when
    // root profiling is explicitly enabled; clean paired timing keeps the
    // profile pointer null.  Known-hi origin index 0 is UC3 and 1 is adaptive.
    unsigned long long unified_uc14_raw_prune_shortcuts = 0;
    unsigned long long unified_uc14_midpoint_refinement_skips = 0;
    unsigned long long unified_uc14_hi_residual_evaluations = 0;
    unsigned long long unified_uc14_hi_deferred_split_skips = 0;
    unsigned long long unified_uc14_known_hi_reuses[2] = {};
    unsigned long long unified_uc14_position_proxy_attempts = 0;
    unsigned long long unified_uc14_position_point_interval_evaluations = 0;
    unsigned long long unified_uc14_position_point_interval_valid = 0;
    unsigned long long unified_uc14_position_certificates = 0;
    unsigned long long unified_uc14_position_certificate_iteration_sum = 0;
    unsigned long long unified_uc14_position_certificate_iteration_hist[6] = {};
    unsigned long long warp_surface_visits = 0;
    unsigned long long warp_active_lane_visits = 0;
    unsigned long long confirmation_lane_visits = 0;
    unsigned long long confirmation_warp_visits = 0;
    unsigned long long mixed_confirmation_warp_visits = 0;
    unsigned long long all_confirmation_warp_visits = 0;
    unsigned long long confirmation_wasted_lane_slots = 0;
    unsigned long long confirmation_warps_1_lane = 0;
    unsigned long long confirmation_warps_2_4_lanes = 0;
    unsigned long long confirmation_warps_5_8_lanes = 0;
    unsigned long long confirmation_warps_9_16_lanes = 0;
    unsigned long long confirmation_warps_17_31_lanes = 0;
    unsigned long long confirmation_warps_32_lanes = 0;
    unsigned long long slow_lane_visits = 0;
    unsigned long long full_lane_visits = 0;
    unsigned long long slow_warp_visits = 0;
    unsigned long long full_warp_visits = 0;
    unsigned long long mixed_slow_warp_visits = 0;
    unsigned long long all_slow_warp_visits = 0;
    unsigned long long slow_wasted_lane_slots = 0;
    unsigned long long slow_warps_1_lane = 0;
    unsigned long long slow_warps_2_4_lanes = 0;
    unsigned long long slow_warps_5_8_lanes = 0;
    unsigned long long slow_warps_9_16_lanes = 0;
    unsigned long long slow_warps_17_31_lanes = 0;
    unsigned long long slow_warps_32_lanes = 0;
    unsigned long long confirmation_lanes_by_surface[
        kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long mixed_confirmation_warps_by_surface[
        kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long slow_lanes_by_surface[kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long full_lanes_by_surface[kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long mixed_slow_warps_by_surface[
        kMaxAsphereRootProfileSurfaces] = {};
};

__device__ bool intersect_conic_or_asphere(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double* t,
    bool allow_domain_recovery = true,
    DeviceAsphereCandidateDiagnostic* diagnostic = nullptr,
    DeviceAsphereRootProfile* uc8_profile = nullptr);

// Polish and certify a transverse candidate as the first strictly-forward
// root in the materialized unified domain.  This is a sufficient test only:
// false (including an even/near-tangent root) means that the caller must
// retain the unified interval fallback.
__device__ bool certify_even_asphere_first_root_candidate(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lower,
    double upper,
    double candidate,
    double* certified_candidate,
    const DeviceAsphereCandidateDiagnostic* candidate_diagnostic = nullptr,
    bool reuse_candidate_pair = false,
    DeviceAspherePrefixDiagnostic* prefix_diagnostic = nullptr);

// Classify the exact-first-surface construction without changing the generic
// root contract.  Returns 1 for a certified first root, -1 for certified no
// root due to an empty ray/domain intersection, -2 for an endpoint-sign
// no-root certificate, and 0 for the unchanged fallback.  The semantic
// candidate and strict-forward lower bound are reconstructed internally to
// keep this hot device-call ABI register-only.
__device__ __noinline__ int classify_even_asphere_exact_s1_constructed_root(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double* certified_candidate);

// Refine the even-asphere branch local to a geometric projection seed.  This
// is intentionally distinct from the ordinary first-forward-hit contract.
__device__ bool intersect_asphere_from_local_seed(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double seed,
    double* t);

__device__ bool intersect_asphere_nearest_forward_fallback(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double requested_lower,
    double requested_upper,
    double* t);

// Use the inexpensive Newton intersection when it can be certified as the
// first forward root, and pay for the bounded nearest-root search only for an
// ambiguous high-order profile.
__device__ bool intersect_asphere_nearest_forward_certified(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    int initial_root_mode,
    int confirmation_segments,
    DeviceAsphereRootProfile* profile,
    int* path_class,
    bool allow_local_in_domain_candidate,
    bool ignore_stop_aperture,
    double* t,
    int* diagnostic_u1_failure_reason = nullptr);

}  // namespace optiland::de_cuda_detail
