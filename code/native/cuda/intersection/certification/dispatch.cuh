#pragma once
#include "../prelude.cuh"
#include "candidate_root.cuh"
#include "fast_bounded_candidate.cuh"
#include "first_root_interval.cuh"
#include "initial_candidate.cuh"
#include "prefix_piecewise.cuh"
#include "unified_domain.cuh"
#include "../geometry/forward_interval.cuh"
#include "../geometry/sag_residual.cuh"
#include "../refinement/conic_candidate.cuh"

namespace optiland::de_cuda_detail {
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
    int* diagnostic_u1_failure_reason) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    initial_root_mode = 23;
    confirmation_segments = 6;
    profile = nullptr;
    diagnostic_u1_failure_reason = nullptr;
#endif
    if (!t || surface.kind != kKindEvenAsphere) {
        return false;
    }
    if (diagnostic_u1_failure_reason) {
        *diagnostic_u1_failure_reason = 0;
    }
    if (path_class) {
        *path_class = 0;
    }
    if (profile) {
        increment_profile_counter(&profile->intersections);
    }
    double search_lower = 0.0;
    double search_upper = INFINITY;
    const bool unified_mode =
        initial_root_mode >= 6 && initial_root_mode <= 31;
    const bool uc1_prefix_mode =
        initial_root_mode >= 7 && initial_root_mode <= 31;
    const bool uc3_failure_mode =
        initial_root_mode >= 9 && initial_root_mode <= 31;
    const bool uc6_prefix_eval_reuse_mode = initial_root_mode == 11;
    const bool aperture_bounded_stop =
        !unified_mode && !ignore_stop_aperture && surface.is_stop &&
        surface.aperture_radius > 0.0 && finite_value(surface.aperture_radius);
    if (aperture_bounded_stop &&
        !stop_surface_forward_root_interval(
            surface, px, py, lx, my, &search_lower, &search_upper)) {
        return false;
    }
    auto root_in_search_domain = [&](double candidate) {
        const double tolerance =
            1.0e-10 * fmax(1.0, fmax(fabs(search_lower), fabs(candidate)));
        return finite_value(candidate) &&
               candidate >= search_lower - tolerance &&
               (!finite_value(search_upper) || candidate <= search_upper + tolerance);
    };
    auto root_in_materialized_slope_domain = [&](double candidate) {
        if (!finite_value(candidate) || candidate < -kDeviceEps) {
            return false;
        }
        const double radius_squared =
            surface.slope_root_domain_radius_squared;
        if (!finite_value(radius_squared) ||
            radius_squared >= 0.5 * kDeviceInf) {
            return true;
        }
        if (!(radius_squared >= 0.0)) {
            return false;
        }
        const double x = fma(candidate, lx, px);
        const double y = fma(candidate, my, py);
        const double candidate_radius_squared = fma(x, x, y * y);
        return finite_value(candidate_radius_squared) &&
               candidate_radius_squared <=
                   radius_squared * (1.0 + 2.0e-12);
    };
    DIntersectUnifiedCertifiedDomainInputs<decltype(root_in_search_domain), decltype(root_in_materialized_slope_domain)> d_intersect_unified_certified_domain_inputs{
        surface,
        px,
        py,
        pz,
        lx,
        my,
        nz,
        profile,
        path_class,
        t,
        search_lower,
        search_upper,
        unified_mode,
        uc1_prefix_mode,
        uc3_failure_mode,
        uc6_prefix_eval_reuse_mode,
        root_in_search_domain,
        root_in_materialized_slope_domain
    };
    const auto d_intersect_unified_certified_domain_outcome = d_intersect_unified_certified_domain(d_intersect_unified_certified_domain_inputs);
    if (d_intersect_unified_certified_domain_outcome.flow == native_detail::PhaseFlow::Return) { return d_intersect_unified_certified_domain_outcome.value; }

    DIntersectFastBoundedCandidateInputs<decltype(root_in_search_domain), decltype(root_in_materialized_slope_domain)> d_intersect_fast_bounded_candidate_inputs{
        surface,
        px,
        py,
        pz,
        lx,
        my,
        nz,
        initial_root_mode,
        confirmation_segments,
        profile,
        path_class,
        t,
        search_lower,
        search_upper,
        root_in_search_domain,
        root_in_materialized_slope_domain
    };
    const auto d_intersect_fast_bounded_candidate_outcome = d_intersect_fast_bounded_candidate(d_intersect_fast_bounded_candidate_inputs);
    if (d_intersect_fast_bounded_candidate_outcome.flow == native_detail::PhaseFlow::Return) { return d_intersect_fast_bounded_candidate_outcome.value; }

    if (!restrict_search_to_slope_domain(
            surface, px, py, lx, my, &search_lower, &search_upper)) {
        if (initial_root_mode == 4 && diagnostic_u1_failure_reason) {
            *diagnostic_u1_failure_reason = 1;
        }
        return false;
    }
    double candidate = NAN;
    const bool initial_candidate_converged = intersect_conic_or_asphere(
        surface,
        px,
        py,
        pz,
        lx,
        my,
        nz,
        &candidate,
        initial_root_mode != 4 && initial_root_mode != 5);
    const bool initial_candidate_in_domain =
        initial_candidate_converged && root_in_search_domain(candidate);
    const bool initial_candidate_ok = initial_candidate_in_domain;
    if (initial_root_mode == 4 && diagnostic_u1_failure_reason &&
        !initial_candidate_ok) {
        *diagnostic_u1_failure_reason =
            initial_candidate_converged ? 3 : 2;
    }
    DAcceptInitialAsphereCandidateInputs d_accept_initial_asphere_candidate_inputs{
        surface,
        px,
        py,
        pz,
        lx,
        my,
        nz,
        initial_root_mode,
        confirmation_segments,
        profile,
        path_class,
        allow_local_in_domain_candidate,
        t,
        search_lower,
        candidate,
        initial_candidate_ok
    };
    const auto d_accept_initial_asphere_candidate_outcome = d_accept_initial_asphere_candidate(d_accept_initial_asphere_candidate_inputs);
    if (d_accept_initial_asphere_candidate_outcome.flow == native_detail::PhaseFlow::Return) { return d_accept_initial_asphere_candidate_outcome.value; }

    if (initial_root_mode == 5 && !initial_candidate_ok &&
        finite_value(search_upper) && search_upper >= search_lower) {
        const double newton_tol =
            finite_value(surface.newton_tol) && surface.newton_tol > 0.0
                ? surface.newton_tol
                : 1.0e-10;
        double recovered = NAN;
        DeviceFirstRootSearchStatus recovery_status =
            DeviceFirstRootSearchStatus::Ambiguous;
        if (confirmation_segments == 4) {
            recovery_status = certify_first_asphere_root_piecewise<4>(
                surface, px, py, pz, lx, my, nz, search_lower,
                search_upper, false, newton_tol, profile, &recovered);
        } else if (confirmation_segments == 6) {
            recovery_status = certify_first_asphere_root_piecewise<6>(
                surface, px, py, pz, lx, my, nz, search_lower,
                search_upper, false, newton_tol, profile, &recovered);
        } else {
            recovery_status = certify_first_asphere_root_piecewise<8>(
                surface, px, py, pz, lx, my, nz, search_lower,
                search_upper, false, newton_tol, profile, &recovered);
        }
        if (recovery_status == DeviceFirstRootSearchStatus::Found) {
            *t = recovered;
            return finite_value(*t);
        }
        if (recovery_status == DeviceFirstRootSearchStatus::NoRoot) {
            return false;
        }
    }
    if (initial_root_mode == 3 || initial_root_mode == 4) {
        return false;
    }
    if (profile) {
        increment_profile_counter(&profile->full_fallbacks);
    }
    if (path_class) {
        *path_class |= 4;
    }
    const bool fallback_ok = intersect_asphere_nearest_forward_fallback(
        surface,
        px,
        py,
        pz,
        lx,
        my,
        nz,
        search_lower,
        search_upper,
        t);
    if (profile && fallback_ok) {
        increment_profile_counter(&profile->full_fallback_successes);
    }
    return fallback_ok;
}
} // namespace optiland::de_cuda_detail
