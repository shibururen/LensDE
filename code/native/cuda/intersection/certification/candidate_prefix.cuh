#pragma once
#include "../prelude.cuh"
#include "first_root_interval.cuh"
#include "prefix_piecewise.cuh"
#include "../geometry/sag_residual.cuh"
#include "../intervals/asphere_residual.cuh"
#include "../intervals/radial_geometry.cuh"
#include "../refinement/newton_seed.cuh"

namespace optiland::de_cuda_detail {
// Rejected PTX3 function-splitting experiment retained disabled for audit:
// device-call argument preservation raised the ordinary path to 200-byte
// stack / 140-byte spills and the public dispatcher to 80/32.
struct DCertifyUnifiedCandidatePrefixInputs {
    const DeviceSurface& surface;
    double& px;
    double& py;
    double& pz;
    double& lx;
    double& my;
    double& nz;
    DeviceAsphereRootProfile*& profile;
    int*& path_class;
    double*& t;
    double& search_lower;
    double& search_upper;
    const double& newton_tol;
    double& candidate;
    const bool& candidate_ok;
    const double& candidate_guard;
};
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
template <class Inputs>
__device__ __forceinline__ native_detail::PhaseOutcome d_certify_unified_candidate_prefix(Inputs& inputs) {
    auto& surface = inputs.surface;
    auto& px = inputs.px;
    auto& py = inputs.py;
    auto& pz = inputs.pz;
    auto& lx = inputs.lx;
    auto& my = inputs.my;
    auto& nz = inputs.nz;
    auto& profile = inputs.profile;
    auto& path_class = inputs.path_class;
    auto& t = inputs.t;
    auto& search_lower = inputs.search_lower;
    auto& search_upper = inputs.search_upper;
    auto& newton_tol = inputs.newton_tol;
    auto& candidate = inputs.candidate;
    auto& candidate_ok = inputs.candidate_ok;
    auto& candidate_guard = inputs.candidate_guard;
    if (candidate_ok && candidate_guard > 0.0) {
        if (profile) {
            increment_profile_counter(
                &profile->unified_stationary_window_attempts);
        }
        const double stationary_window_lo =
            fmax(search_lower, candidate - candidate_guard);
        const double stationary_window_hi = finite_value(search_upper)
            ? fmin(search_upper, candidate + candidate_guard)
            : candidate + candidate_guard;
        double window_residual_lo = 0.0;
        double window_residual_hi = 0.0;
        double derivative_lo = 0.0;
        double derivative_hi = 0.0;
        int stationary_endpoint_evaluations = 0;
        const bool stationary_derivative_bracketed =
            stationary_window_hi > stationary_window_lo &&
            (++stationary_endpoint_evaluations,
             surface_residual_and_derivative_at(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                stationary_window_lo,
                &window_residual_lo,
                &derivative_lo)) &&
            (++stationary_endpoint_evaluations,
             surface_residual_and_derivative_at(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                stationary_window_hi,
                &window_residual_hi,
                &derivative_hi)) &&
            derivative_lo * derivative_hi <= 0.0 &&
            (derivative_lo != 0.0 || derivative_hi != 0.0);
        if (profile) {
            atomicAdd(
                &profile->unified_stationary_endpoint_evaluations,
                static_cast<unsigned long long>(
                    stationary_endpoint_evaluations));
        }
        if (stationary_derivative_bracketed) {
            if (profile) {
                increment_profile_counter(
                    &profile->unified_stationary_derivative_brackets);
            }
            double stationary_lo = stationary_window_lo;
            double stationary_hi = stationary_window_hi;
            double derivative_at_lo = derivative_lo;
            bool stationary_valid = true;
            int stationary_iterations = 0;
            for (int iteration = 0; iteration < 64; ++iteration) {
                const double stationary_mid =
                    0.5 * (stationary_lo + stationary_hi);
                double stationary_residual = 0.0;
                double derivative_mid = 0.0;
                ++stationary_iterations;
                if (!surface_residual_and_derivative_at(
                        surface,
                        px,
                        py,
                        pz,
                        lx,
                        my,
                        nz,
                        stationary_mid,
                        &stationary_residual,
                        &derivative_mid)) {
                    stationary_valid = false;
                    break;
                }
                if (derivative_at_lo * derivative_mid <= 0.0) {
                    stationary_hi = stationary_mid;
                } else {
                    stationary_lo = stationary_mid;
                    derivative_at_lo = derivative_mid;
                }
            }
            if (profile) {
                atomicAdd(
                    &profile->unified_stationary_iterations,
                    static_cast<unsigned long long>(stationary_iterations));
            }
            const double stationary_root =
                0.5 * (stationary_lo + stationary_hi);
            const DeviceScalarInterval stationary_derivative_range =
                even_asphere_derivative_interval(
                    surface,
                    px,
                    py,
                    lx,
                    my,
                    nz,
                    stationary_window_lo,
                    stationary_root);
            const double derivative_tolerance =
                256.0 * DBL_EPSILON *
                fmax(1.0, fmax(fabs(derivative_lo), fabs(derivative_hi)));
            const bool one_sided_stationary =
                stationary_valid && stationary_derivative_range.valid &&
                ((derivative_lo < 0.0 &&
                  stationary_derivative_range.hi <=
                      derivative_tolerance) ||
                 (derivative_lo > 0.0 &&
                  stationary_derivative_range.lo >=
                      -derivative_tolerance));
            double stationary_residual = 0.0;
            if (one_sided_stationary) {
                if (profile) {
                    increment_profile_counter(
                        &profile->unified_stationary_root_evaluations);
                }
            }
            if (one_sided_stationary && surface_residual_at<true>(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    stationary_root,
                    &stationary_residual)) {
                const double stationary_x = fma(stationary_root, lx, px);
                const double stationary_y = fma(stationary_root, my, py);
                const double stationary_z = fma(stationary_root, nz, pz);
                const double stationary_scale = fmax(
                    1.0,
                    fmax(
                        fabs(stationary_z),
                        hypot(stationary_x, stationary_y)));
                const double stationary_residual_tolerance =
                    128.0 * DBL_EPSILON * stationary_scale;
                if (fabs(stationary_residual) <=
                    stationary_residual_tolerance) {
                    DeviceFirstRootSearchStatus prefix_status =
                        DeviceFirstRootSearchStatus::NoRoot;
                    if (stationary_window_lo > search_lower) {
                        if (profile) {
                            increment_profile_counter(
                                &profile->unified_stationary_prefix_searches);
                        }
                        double prefix_root = NAN;
                        int prefix_nodes = 0;
                        prefix_status =
                            find_first_even_asphere_root_interval(
                                surface,
                                px,
                                py,
                                pz,
                                lx,
                                my,
                                nz,
                                search_lower,
                                stationary_window_lo,
                                4096,
                                newton_tol,
                                &prefix_root,
                                profile,
                                &prefix_nodes,
                                true);
                    }
                    if (prefix_status ==
                        DeviceFirstRootSearchStatus::NoRoot) {
                        if (profile) {
                            increment_profile_counter(
                                &profile->unified_adaptive_search_calls);
                            increment_profile_counter(
                                &profile->unified_adaptive_search_found);
                            increment_profile_counter(
                                &profile->unified_stationary_hits);
                            increment_profile_counter(
                                &profile->unified_hits);
                        }
                        if (path_class) {
                            *path_class |= 2;
                        }
                        *t = stationary_root;
                        return {native_detail::PhaseFlow::Return, finite_value(*t) && *t > 1.0e-10};
                    }
                }
            }
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland::de_cuda_detail
