#pragma once
#include "../prelude.cuh"
#include "candidate_root.cuh"
#include "prefix_piecewise.cuh"
#include "../geometry/sag_residual.cuh"
#include "../refinement/conic_candidate.cuh"
#include "../refinement/preconditioned_seed.cuh"
#include "../topology/slope_domain.cuh"

namespace optiland::de_cuda_detail {
template <class Capture_root_in_search_domain, class Capture_root_in_materialized_slope_domain>
struct DIntersectFastBoundedCandidateInputs {
    const DeviceSurface& surface;
    double& px;
    double& py;
    double& pz;
    double& lx;
    double& my;
    double& nz;
    int& initial_root_mode;
    int& confirmation_segments;
    DeviceAsphereRootProfile*& profile;
    int*& path_class;
    double*& t;
    double& search_lower;
    double& search_upper;
    Capture_root_in_search_domain& root_in_search_domain;
    Capture_root_in_materialized_slope_domain& root_in_materialized_slope_domain;
};
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
template <class Inputs>
__device__ __forceinline__ native_detail::PhaseOutcome d_intersect_fast_bounded_candidate(Inputs& inputs) {
    auto& surface = inputs.surface;
    auto& px = inputs.px;
    auto& py = inputs.py;
    auto& pz = inputs.pz;
    auto& lx = inputs.lx;
    auto& my = inputs.my;
    auto& nz = inputs.nz;
    auto& initial_root_mode = inputs.initial_root_mode;
    auto& confirmation_segments = inputs.confirmation_segments;
    auto& profile = inputs.profile;
    auto& path_class = inputs.path_class;
    auto& t = inputs.t;
    auto& search_lower = inputs.search_lower;
    auto& search_upper = inputs.search_upper;
    auto& root_in_search_domain = inputs.root_in_search_domain;
    auto& root_in_materialized_slope_domain = inputs.root_in_materialized_slope_domain;
    if (initial_root_mode == 5) {
        // Round17 bottom-up arm: accept every residual-certified U1 Newton
        // candidate in the finite slope domain without first-root posterior
        // verification.  Only an actual U1 seed/Newton failure pays for the
        // older seed-preconditioning rescue.  This isolates the untested
        // algorithmic hypothesis from all certificate and B2 overhead.
        const double newton_tol =
            finite_value(surface.newton_tol) && surface.newton_tol > 0.0
                ? surface.newton_tol
                : 1.0e-10;
        const bool topology_surface = asphere_ray_requires_topology_seed(
            surface, px, py, lx, my, nz);
        if (profile && topology_surface) {
            increment_profile_counter(&profile->o1_topology_ray_calls);
        }
        bool slope_domain_restricted = false;
        auto ensure_slope_domain_interval = [&]() {
            if (slope_domain_restricted) {
                return true;
            }
            if (!restrict_search_to_slope_domain(
                    surface,
                    px,
                    py,
                    lx,
                    my,
                    &search_lower,
                    &search_upper)) {
                return false;
            }
            slope_domain_restricted = true;
            return true;
        };
        double linear_seeded = NAN;
        if (topology_surface && ensure_slope_domain_interval()) {
            if (profile) {
                increment_profile_counter(
                    &profile->o1_topology_linear_attempts);
            }
            if (intersect_asphere_topology_linear_seed(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    search_lower,
                    search_upper,
                    newton_tol,
                    profile,
                    &linear_seeded) &&
                root_in_search_domain(linear_seeded)) {
                if (profile) {
                    increment_profile_counter(
                        &profile->o1_topology_linear_successes);
                }
                if (path_class) {
                    *path_class |= 2;
                }
                *t = fabs(linear_seeded) <= kDeviceEps ? 0.0 : linear_seeded;
                return {native_detail::PhaseFlow::Return, finite_value(*t)};
            }
        }
        double fast_candidate = NAN;
        const bool fast_candidate_ok = intersect_conic_or_asphere(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                &fast_candidate,
                false) &&
            root_in_search_domain(fast_candidate) &&
            root_in_materialized_slope_domain(fast_candidate);
        if (profile) {
            increment_profile_counter(
                fast_candidate_ok
                    ? &profile->o1_fast_candidate_successes
                    : &profile->o1_fast_candidate_failures);
        }
        if (fast_candidate_ok) {
            double topology_seeded = NAN;
            if (topology_surface && slope_domain_restricted) {
                if (profile) {
                    increment_profile_counter(
                        &profile->o1_topology_seeded_before_attempts);
                }
                if (intersect_asphere_topology_seeded_before(
                        surface,
                        px,
                        py,
                        pz,
                        lx,
                        my,
                        nz,
                        search_lower,
                        fast_candidate,
                        confirmation_segments,
                        newton_tol,
                        profile,
                        &topology_seeded)) {
                    if (profile) {
                        increment_profile_counter(
                            &profile->o1_topology_seeded_before_successes);
                    }
                    if (path_class) {
                        *path_class |= 2;
                    }
                    *t = fabs(topology_seeded) <= kDeviceEps
                        ? 0.0
                        : topology_seeded;
                    return {native_detail::PhaseFlow::Return, finite_value(*t)};
                }
            }
            *t = fabs(fast_candidate) <= kDeviceEps ? 0.0 : fast_candidate;
            return {native_detail::PhaseFlow::Return, finite_value(*t)};
        }
        if (profile) {
            increment_profile_counter(&profile->initial_candidate_failures);
        }
        if (!ensure_slope_domain_interval()) {
            return {native_detail::PhaseFlow::Return, false};
        }
        if (profile) {
            increment_profile_counter(&profile->o1_failure_rescue_attempts);
        }
        double rescued = NAN;
        if (!intersect_asphere_seed_preconditioned_no_b2(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            search_lower,
            search_upper,
            confirmation_segments,
            newton_tol,
            profile,
            &rescued) ||
            !root_in_search_domain(rescued)) {
            return {native_detail::PhaseFlow::Return, false};
        }
        if (profile) {
            increment_profile_counter(&profile->o1_failure_rescue_successes);
        }
        *t = fabs(rescued) <= kDeviceEps ? 0.0 : rescued;
        return {native_detail::PhaseFlow::Return, finite_value(*t)};
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland::de_cuda_detail
