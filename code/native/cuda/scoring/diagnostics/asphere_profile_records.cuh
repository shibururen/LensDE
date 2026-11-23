#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "builder_instrumentation.cuh"

namespace optiland {
namespace {
// Diagnostic-only counters for deciding whether the production asphere
// recovery path contains enough recoverable work to justify a structural
// rewrite. The pointer in DeviceConfig remains null unless the explicit
// feasibility profile is enabled, so ordinary scoring does not perform the
// atomics below.
struct DeviceAsphereFeasibilityProfile {
    unsigned long long domain_calls = 0;
    unsigned long long domain_asphere_calls = 0;
    unsigned long long domain_reuse_hits = 0;
    unsigned long long domain_recomputes = 0;
    unsigned long long domain_interval_nodes = 0;
    unsigned long long domain_leaf_tests = 0;
    unsigned long long domain_bisection_iterations = 0;
    unsigned long long mark_models_built = 0;
    unsigned long long mark_center_models_built = 0;
    unsigned long long mark_models_valid = 0;
    unsigned long long mark_models_tagged = 0;
    unsigned long long mark_center_models_tagged = 0;
    unsigned long long mark_edge_bits = 0;
    unsigned long long recovery_models_built = 0;
    unsigned long long recovery_center_models_built = 0;
    unsigned long long recovery_models_valid = 0;
    unsigned long long skipped_valid_models = 0;
    unsigned long long inactive_wave_models = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kUC5CounterBuildKinds = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kUC5CounterWaveRoles = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kUC5CounterAnchors = 4;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kUC5CounterCells =
    kUC5CounterBuildKinds * kUC5CounterWaveRoles * kUC5CounterAnchors;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// UC5 is an isolated calibrated-model experiment. Keep its flags and optional
// profile pointer out of DeviceConfig: widening that hot by-value ABI caused a
// binary-wide stack regression in the rejected UC2/UC4 prototypes.
struct DeviceAsphereUC5Profile {
    unsigned long long anchor_entered[kUC5CounterCells] = {};
    unsigned long long anchor_mirrored[kUC5CounterCells] = {};
    unsigned long long anchor_skipped_after_failure[kUC5CounterCells] = {};
    unsigned long long cached_accepted[kUC5CounterCells] = {};
    unsigned long long cached_traceable[kUC5CounterCells] = {};
    unsigned long long base_probes[kUC5CounterCells] = {};
    unsigned long long predicted_probes[kUC5CounterCells] = {};
    unsigned long long secant_calls[kUC5CounterCells] = {};
    unsigned long long secant_iterations[kUC5CounterCells] = {};
    unsigned long long secant_backtracks[kUC5CounterCells] = {};
    unsigned long long exhausted_failures[kUC5CounterCells] = {};
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __constant__ int g_asphere_uc5_symmetry_enabled;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __constant__ int g_asphere_uc5_primary_abort_enabled;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __constant__ int g_asphere_uc5_candidate_batch_enabled;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceAsphereUC5Profile* g_asphere_uc5_profile = nullptr;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kAsphereDeepProfileRows = 1024;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kAsphereDeepProfileFields = 32;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kAsphereDeepProfileWaves = 16;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kAsphereDeepProfileRoles = 3;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kAsphereDeepProfileSites = 4;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
inline constexpr int kAsphereDeepU1FailureReasons = 4;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// Diagnostic-only shadow comparison of the inexpensive P0 calibrated-model
// intersection against mode-4 U1 on the exact same incoming ray.  The scorer
// leaves this pointer null unless explicitly requested, so ordinary runs do
// not execute the shadow root or any of these atomics.
struct DeviceAsphereDeepProfile {
    unsigned long long calls = 0;
    unsigned long long both_success_same_root = 0;
    unsigned long long u1_failure_reference_success = 0;
    unsigned long long u1_success_reference_failure = 0;
    unsigned long long both_failure = 0;
    unsigned long long both_success_different_root = 0;
    unsigned long long calls_by_surface[kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long problems_by_surface[kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long u1_failures_by_surface[kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long different_roots_by_surface[kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long u1_only_by_surface[kMaxAsphereRootProfileSurfaces] = {};
    unsigned long long calls_by_role[kAsphereDeepProfileRoles] = {};
    unsigned long long problems_by_role[kAsphereDeepProfileRoles] = {};
    unsigned long long u1_failures_by_role[kAsphereDeepProfileRoles] = {};
    unsigned long long different_roots_by_role[kAsphereDeepProfileRoles] = {};
    unsigned long long problems_by_surface_role[
        kMaxAsphereRootProfileSurfaces * kAsphereDeepProfileRoles] = {};
    unsigned long long calls_by_site[kAsphereDeepProfileSites] = {};
    unsigned long long problems_by_site[kAsphereDeepProfileSites] = {};
    unsigned long long u1_failure_reference_success_by_site[
        kAsphereDeepProfileSites] = {};
    unsigned long long both_failure_by_site[kAsphereDeepProfileSites] = {};
    unsigned long long different_roots_by_site[kAsphereDeepProfileSites] = {};
    unsigned long long u1_only_by_site[kAsphereDeepProfileSites] = {};
    unsigned long long u1_failures_by_reason[
        kAsphereDeepU1FailureReasons] = {};
    unsigned long long u1_failure_reference_success_by_reason[
        kAsphereDeepU1FailureReasons] = {};
    unsigned long long both_failure_by_reason[
        kAsphereDeepU1FailureReasons] = {};
    unsigned long long model_comparisons = 0;
    unsigned long long model_p0_valid_u1_valid = 0;
    unsigned long long model_p0_valid_u1_invalid = 0;
    unsigned long long model_p0_invalid_u1_valid = 0;
    unsigned long long model_both_invalid = 0;
    unsigned long long model_both_valid_equivalent = 0;
    unsigned long long model_both_valid_different = 0;
    unsigned long long model_equal_edge_valid_masks = 0;
    unsigned long long hybrid_model_comparisons = 0;
    unsigned long long hybrid_model_p0_valid_hybrid_valid = 0;
    unsigned long long hybrid_model_p0_valid_hybrid_invalid = 0;
    unsigned long long hybrid_model_p0_invalid_hybrid_valid = 0;
    unsigned long long hybrid_model_both_invalid = 0;
    unsigned long long hybrid_model_both_valid_equivalent = 0;
    unsigned long long hybrid_model_both_valid_different = 0;
    unsigned long long hybrid_model_equal_edge_valid_masks = 0;
    unsigned long long exact_s1_calls = 0;
    unsigned long long exact_s1_expected_residual_valid = 0;
    unsigned long long exact_s1_expected_in_slope_domain = 0;
    unsigned long long exact_s1_direction_monotonic = 0;
    unsigned long long exact_s1_reference_success = 0;
    unsigned long long exact_s1_reference_same_expected = 0;
    unsigned long long exact_s1_reference_different_expected = 0;
    unsigned long long exact_s1_u1_failure_reference_success = 0;
    unsigned long long exact_s1_u1_failure_reference_same_expected = 0;
    unsigned long long exact_s1_u1_failure_reference_different_expected = 0;
    unsigned long long calls_by_field[kAsphereDeepProfileFields] = {};
    unsigned long long problems_by_field[kAsphereDeepProfileFields] = {};
    unsigned long long calls_by_wave[kAsphereDeepProfileWaves] = {};
    unsigned long long problems_by_wave[kAsphereDeepProfileWaves] = {};
    unsigned long long calls_by_row[kAsphereDeepProfileRows] = {};
    unsigned long long problems_by_row[kAsphereDeepProfileRows] = {};
    unsigned long long u1_failures_by_row[kAsphereDeepProfileRows] = {};
    unsigned long long different_roots_by_row[kAsphereDeepProfileRows] = {};
};
} // namespace
} // namespace optiland
