#pragma once
#include "../prelude.cuh"
#include "first_root_interval.cuh"
#include "prefix_piecewise.cuh"
#include "../geometry/sag_residual.cuh"

namespace optiland::de_cuda_detail {
struct DAcceptInitialAsphereCandidateInputs {
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
    bool& allow_local_in_domain_candidate;
    double*& t;
    double& search_lower;
    double& candidate;
    const bool& initial_candidate_ok;
};
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
template <class Inputs>
__device__ __forceinline__ native_detail::PhaseOutcome d_accept_initial_asphere_candidate(Inputs& inputs) {
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
    auto& allow_local_in_domain_candidate = inputs.allow_local_in_domain_candidate;
    auto& t = inputs.t;
    auto& search_lower = inputs.search_lower;
    auto& candidate = inputs.candidate;
    auto& initial_candidate_ok = inputs.initial_candidate_ok;
    if (initial_candidate_ok) {
        candidate = fabs(candidate) <= kDeviceEps ? 0.0 : candidate;
        if (initial_root_mode == 1 || initial_root_mode == 4 ||
            allow_local_in_domain_candidate) {
            // Accuracy/performance ablation: the analytic/plane seed has
            // already passed the residual-certified bounded Newton refine.
            // The candidate has also passed the exact finite-domain interval
            // check above, so aiming can use it without searching the prefix.
            *t = candidate;
            return {native_detail::PhaseFlow::Return, finite_value(*t)};
        }
        const double newton_tol =
            finite_value(surface.newton_tol) && surface.newton_tol > 0.0
                ? surface.newton_tol
                : 1.0e-10;
        double earlier = NAN;
        DeviceFirstRootSearchStatus prefix_status =
            DeviceFirstRootSearchStatus::Ambiguous;
        if (initial_root_mode == 5) {
            // Keep the successful-U1-candidate path minimal.  The whole-prefix
            // sufficient condition accepts the overwhelmingly common simple
            // case; only its failures enter the existing strict resolver.
            // Piecewise bracket location is reserved for U1 seed failures,
            // where it actually supplies information U1 did not have.
            if (certify_asphere_prefix_monotonic_only(
                    surface, px, py, lx, my, nz, search_lower, candidate)) {
                prefix_status = DeviceFirstRootSearchStatus::NoRoot;
                if (profile) {
                    atomicAdd(&profile->monotonic_certificates, 1ULL);
                }
            }
        } else if (initial_root_mode == 2 || initial_root_mode == 3) {
            if (confirmation_segments == 4) {
                prefix_status = confirm_initial_root_by_sampling<4>(
                    surface, px, py, pz, lx, my, nz, search_lower,
                    candidate, newton_tol,
                    profile, path_class, &earlier);
            } else if (confirmation_segments == 6) {
                prefix_status = confirm_initial_root_by_sampling<6>(
                    surface, px, py, pz, lx, my, nz, search_lower,
                    candidate, newton_tol,
                    profile, path_class, &earlier);
            } else {
                prefix_status = confirm_initial_root_by_sampling<8>(
                    surface, px, py, pz, lx, my, nz, search_lower,
                    candidate, newton_tol,
                    profile, path_class, &earlier);
            }
        }
        if (prefix_status == DeviceFirstRootSearchStatus::Ambiguous &&
            initial_root_mode != 3) {
            if (path_class) {
                *path_class |= 2;
            }
            if (profile) {
                increment_profile_counter(&profile->strict_prefix_fallbacks);
            }
            prefix_status = find_certified_root_before(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                search_lower,
                candidate,
                newton_tol,
                &earlier,
                initial_root_mode == 5);
            if (profile) {
                if (prefix_status == DeviceFirstRootSearchStatus::Found) {
                    increment_profile_counter(&profile->strict_prefix_found);
                } else if (prefix_status == DeviceFirstRootSearchStatus::NoRoot) {
                    increment_profile_counter(&profile->strict_prefix_no_root);
                } else {
                    increment_profile_counter(&profile->strict_prefix_ambiguous);
                }
            }
        }
        if (prefix_status == DeviceFirstRootSearchStatus::Found) {
            *t = earlier;
            return {native_detail::PhaseFlow::Return, finite_value(*t)};
        }
        if (prefix_status == DeviceFirstRootSearchStatus::NoRoot) {
            *t = candidate;
            return {native_detail::PhaseFlow::Return, finite_value(*t)};
        }
        if (prefix_status == DeviceFirstRootSearchStatus::Ambiguous &&
            initial_root_mode == 3) {
            // No-fallback ablation: measure whether front-to-back initial-root
            // confirmation alone is sufficient for the operational domain.
            *t = candidate;
            return {native_detail::PhaseFlow::Return, finite_value(*t)};
        }
    } else if (profile) {
        increment_profile_counter(&profile->initial_candidate_failures);
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland::de_cuda_detail
