#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../diagnostics/intersection_shadow.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"
#include "intersection_gate.cuh"

namespace optiland {
namespace {
__device__ bool d_intersect_even_asphere_configured(
    const DeviceSurface& surface,
    const DeviceConfig& cfg,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    bool require_first_root_contract,
    bool allow_local_in_domain_candidate,
    bool ignore_stop_aperture,
    int* path_class,
    double* t,
    int deep_profile_surface_index = -1,
    int deep_profile_role = kAsphereDeepRoleAuxiliary,
    int deep_profile_site = kAsphereDeepSiteOther) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    // Keep the exact-S1 construction that is part of the retained UC10 route,
    // but do not materialize deep-shadow, profile, or earlier experimental
    // mode branches into the release scorer.
    if (deep_profile_site == kAsphereDeepSiteExactFirstSurfaceSeed &&
        deep_profile_surface_index == cfg.first_optical_surface) {
        const double surface_scale = fmax(
            1.0,
            fmax(fabs(surface.aperture_radius), fabs(surface.semi_aperture)));
        const double expected_t = 1.0e-4 * surface_scale;
        double certified_t = NAN;
        if (de_cuda_detail::certify_even_asphere_first_root_candidate(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                nextafter(1.0e-10, DBL_MAX),
                DBL_MAX,
                expected_t,
                &certified_t,
                nullptr,
                false,
                nullptr)) {
            *t = certified_t;
            return true;
        }
    }
    if (cfg.asphere_upper_bound_mode == 1) {
        return de_cuda_detail::intersect_conic_or_asphere(
            surface, px, py, pz, lx, my, nz, t, false);
    }
    if (cfg.asphere_upper_bound_mode == 2 ||
        require_first_root_contract ||
        cfg.asphere_slope_root_domain_enabled) {
        return de_cuda_detail::intersect_asphere_nearest_forward_certified(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            23,
            6,
            nullptr,
            path_class,
            allow_local_in_domain_candidate,
            ignore_stop_aperture,
            t,
            nullptr);
    }
    return d_intersect_surface(surface, px, py, pz, lx, my, nz, t);
#else
    bool shadow_reference_ok = false;
    double shadow_reference_t = NAN;
    const bool run_deep_shadow =
        cfg.asphere_deep_profile != nullptr &&
        cfg.asphere_deep_profile_row >= 0 &&
        deep_profile_surface_index >= 0;
    if (run_deep_shadow) {
        DeviceSurface p0_surface = surface;
        p0_surface.slope_root_domain_radius_squared = INFINITY;
        p0_surface.slope_root_domain_cached_reference_radius = -1.0;
        p0_surface.slope_root_domain_cached_clear_radius = -1.0;
        double u1_t = NAN;
        int u1_failure_reason = 0;
        const bool u1_ok =
            de_cuda_detail::intersect_asphere_nearest_forward_certified(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                4,
                cfg.asphere_confirmation_segments,
                nullptr,
                nullptr,
                false,
                ignore_stop_aperture,
                &u1_t,
                &u1_failure_reason);
        shadow_reference_ok = d_intersect_surface(
            p0_surface, px, py, pz, lx, my, nz, &shadow_reference_t);
        d_record_asphere_deep_comparison(
            cfg,
            deep_profile_surface_index,
            deep_profile_role,
            deep_profile_site,
            u1_ok,
            u1_t,
            shadow_reference_ok,
            shadow_reference_t,
            u1_failure_reason);
        d_record_asphere_deep_exact_s1_expected_root(
            surface,
            cfg,
            deep_profile_surface_index,
            deep_profile_site,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            u1_ok,
            shadow_reference_ok,
            shadow_reference_t);
    }
    if (cfg.asphere_exact_s1_seed_bypass &&
        deep_profile_site == kAsphereDeepSiteExactFirstSurfaceSeed &&
        deep_profile_surface_index == cfg.first_optical_surface) {
        const double surface_scale = fmax(
            1.0,
            fmax(fabs(surface.aperture_radius), fabs(surface.semi_aperture)));
        const double expected_t = 1.0e-4 * surface_scale;
        const double x = fma(expected_t, lx, px);
        const double y = fma(expected_t, my, py);
        const double z = fma(expected_t, nz, pz);
        const double sag = d_sag(surface, hypot(x, y));
        const double residual_scale = fmax(
            1.0,
            fmax(fabs(z), fmax(fabs(sag), hypot(x, y))));
        if (d_finite(sag) && d_finite(z) &&
            fabs(sag - z) <= 1.0e-10 * residual_scale) {
            *t = expected_t;
            return true;
        }
        return false;
    }
    if (cfg.asphere_initial_root_mode == 8 &&
        deep_profile_site == kAsphereDeepSiteExactFirstSurfaceSeed &&
        deep_profile_surface_index == cfg.first_optical_surface) {
        if (cfg.asphere_root_profile) {
            atomicAdd(
                &cfg.asphere_root_profile->unified_exact_s1_uc2_attempts,
                1ULL);
            atomicAdd(
                &cfg.asphere_root_profile->unified_exact_s1_prefix_attempts,
                1ULL);
        }
        const int classification =
            de_cuda_detail::classify_even_asphere_exact_s1_constructed_root(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                t);
        if (classification != 0) {
            if (cfg.asphere_root_profile) {
                atomicAdd(&cfg.asphere_root_profile->intersections, 1ULL);
                atomicAdd(&cfg.asphere_root_profile->unified_calls, 1ULL);
                atomicAdd(
                    &cfg.asphere_root_profile->unified_exact_s1_calls,
                    1ULL);
                if (classification > 0) {
                    atomicAdd(
                        &cfg.asphere_root_profile
                             ->unified_candidate_successes,
                        1ULL);
                    atomicAdd(
                        &cfg.asphere_root_profile
                             ->unified_prefix_certificates,
                        1ULL);
                    atomicAdd(
                        &cfg.asphere_root_profile
                             ->unified_exact_s1_prefix_certificates,
                        1ULL);
                    atomicAdd(
                        &cfg.asphere_root_profile->unified_hits, 1ULL);
                    atomicAdd(
                        &cfg.asphere_root_profile->unified_exact_s1_hits,
                        1ULL);
                    atomicAdd(
                        &cfg.asphere_root_profile->unified_exact_s1_uc2_hits,
                        1ULL);
                } else {
                    atomicAdd(
                        &cfg.asphere_root_profile
                             ->unified_candidate_failures,
                        1ULL);
                    atomicAdd(
                        &cfg.asphere_root_profile->unified_misses, 1ULL);
                    atomicAdd(
                        &cfg.asphere_root_profile->unified_exact_s1_misses,
                        1ULL);
                    atomicAdd(
                        classification == -1
                            ? &cfg.asphere_root_profile
                                   ->unified_exact_s1_uc2_domain_misses
                            : &cfg.asphere_root_profile
                                   ->unified_exact_s1_uc2_endpoint_misses,
                        1ULL);
                }
            }
            if (classification > 0) {
                return true;
            }
            return false;
        }
        if (cfg.asphere_root_profile) {
            atomicAdd(
                &cfg.asphere_root_profile->unified_exact_s1_uc2_fallbacks,
                1ULL);
        }
    }
    if (((cfg.asphere_initial_root_mode >= 7 &&
          cfg.asphere_initial_root_mode <= 31) &&
         cfg.asphere_initial_root_mode != 8) &&
        deep_profile_site == kAsphereDeepSiteExactFirstSurfaceSeed &&
        deep_profile_surface_index == cfg.first_optical_surface) {
        const double surface_scale = fmax(
            1.0,
            fmax(fabs(surface.aperture_radius), fabs(surface.semi_aperture)));
        const double expected_t = 1.0e-4 * surface_scale;
        double certified_t = NAN;
        if (cfg.asphere_root_profile) {
            atomicAdd(
                &cfg.asphere_root_profile->unified_exact_s1_prefix_attempts,
                1ULL);
        }
        DeviceAspherePrefixDiagnostic prefix_diagnostic{};
        const bool prefix_ok =
            de_cuda_detail::certify_even_asphere_first_root_candidate(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                nextafter(1.0e-10, DBL_MAX),
                DBL_MAX,
                expected_t,
                &certified_t,
                nullptr,
                false,
                cfg.asphere_root_profile ? &prefix_diagnostic : nullptr);
        if (cfg.asphere_root_profile) {
            atomicAdd(
                &cfg.asphere_root_profile->unified_prefix_attempts,
                1ULL);
            atomicAdd(
                &cfg.asphere_root_profile->unified_prefix_polish_evaluations,
                static_cast<unsigned long long>(
                    prefix_diagnostic.polish_evaluations));
            atomicAdd(
                &cfg.asphere_root_profile
                     ->unified_prefix_reused_candidate_pairs,
                static_cast<unsigned long long>(
                    prefix_diagnostic.reused_candidate_pairs));
            atomicAdd(
                &cfg.asphere_root_profile
                     ->unified_prefix_endpoint_residual_evaluations,
                static_cast<unsigned long long>(
                    prefix_diagnostic.endpoint_residual_evaluations));
            atomicAdd(
                &cfg.asphere_root_profile
                     ->unified_prefix_monotonicity_attempts,
                static_cast<unsigned long long>(
                    prefix_diagnostic.monotonicity_attempts));
        }
        if (prefix_ok) {
            if (cfg.asphere_root_profile) {
                atomicAdd(&cfg.asphere_root_profile->intersections, 1ULL);
                atomicAdd(&cfg.asphere_root_profile->unified_calls, 1ULL);
                atomicAdd(
                    &cfg.asphere_root_profile->unified_candidate_successes,
                    1ULL);
                atomicAdd(
                    &cfg.asphere_root_profile->unified_prefix_certificates,
                    1ULL);
                atomicAdd(
                    &cfg.asphere_root_profile
                         ->unified_exact_s1_prefix_certificates,
                    1ULL);
                atomicAdd(&cfg.asphere_root_profile->unified_hits, 1ULL);
                atomicAdd(
                    &cfg.asphere_root_profile->unified_exact_s1_calls,
                    1ULL);
                atomicAdd(
                    &cfg.asphere_root_profile->unified_exact_s1_hits,
                    1ULL);
            }
            *t = certified_t;
            return true;
        }
    }
    if (cfg.asphere_upper_bound_mode == 1) {
        // U0: analytic/conic seeds followed only by the bounded residual
        // Newton refinement.  Do not enter either recovery implementation.
        return de_cuda_detail::intersect_conic_or_asphere(
            surface, px, py, pz, lx, my, nz, t, false);
    }
    if (cfg.asphere_upper_bound_mode == 2 ||
        require_first_root_contract ||
        cfg.asphere_slope_root_domain_enabled) {
        const bool intersection_ok =
            de_cuda_detail::intersect_asphere_nearest_forward_certified(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            cfg.asphere_initial_root_mode,
            cfg.asphere_confirmation_segments,
            cfg.asphere_root_profile,
            path_class,
            allow_local_in_domain_candidate,
            ignore_stop_aperture,
            t);
        if ((cfg.asphere_initial_root_mode >= 6 &&
             cfg.asphere_initial_root_mode <= 31) &&
            cfg.asphere_root_profile &&
            deep_profile_site == kAsphereDeepSiteExactFirstSurfaceSeed &&
            deep_profile_surface_index == cfg.first_optical_surface) {
            atomicAdd(
                &cfg.asphere_root_profile->unified_exact_s1_calls, 1ULL);
            atomicAdd(
                intersection_ok
                    ? &cfg.asphere_root_profile->unified_exact_s1_hits
                    : &cfg.asphere_root_profile->unified_exact_s1_misses,
                1ULL);
        }
        return intersection_ok;
    }
    if (run_deep_shadow) {
        *t = shadow_reference_t;
        return shadow_reference_ok;
    }
    return d_intersect_surface(surface, px, py, pz, lx, my, nz, t);
#endif
}
} // namespace
} // namespace optiland
