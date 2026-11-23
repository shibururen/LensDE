#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "asphere_profile_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"

namespace optiland {
namespace {
__device__ __forceinline__ void d_record_asphere_deep_comparison(
    const DeviceConfig& cfg,
    int surface_index,
    int role,
    int site,
    bool u1_ok,
    double u1_t,
    bool reference_ok,
    double reference_t,
    int u1_failure_reason) {
    DeviceAsphereDeepProfile* profile = cfg.asphere_deep_profile;
    if (!profile || cfg.asphere_deep_profile_row < 0 ||
        surface_index < 0 ||
        surface_index >= kMaxAsphereRootProfileSurfaces) {
        return;
    }
    role = max(0, min(role, kAsphereDeepProfileRoles - 1));
    site = max(0, min(site, kAsphereDeepProfileSites - 1));
    u1_failure_reason = max(
        0, min(u1_failure_reason, kAsphereDeepU1FailureReasons - 1));
    const int row = cfg.asphere_deep_profile_row;
    const int field = cfg.asphere_deep_profile_field_index;
    const int wave = cfg.asphere_deep_profile_wave_index;
    atomicAdd(&profile->calls, 1ULL);
    atomicAdd(&profile->calls_by_surface[surface_index], 1ULL);
    atomicAdd(&profile->calls_by_role[role], 1ULL);
    atomicAdd(&profile->calls_by_site[site], 1ULL);
    if (field >= 0 && field < kAsphereDeepProfileFields) {
        atomicAdd(&profile->calls_by_field[field], 1ULL);
    }
    if (wave >= 0 && wave < kAsphereDeepProfileWaves) {
        atomicAdd(&profile->calls_by_wave[wave], 1ULL);
    }
    if (row < kAsphereDeepProfileRows) {
        atomicAdd(&profile->calls_by_row[row], 1ULL);
    }

    bool problem = false;
    bool u1_failure = false;
    bool different_root = false;
    bool u1_only = false;
    if (u1_ok && reference_ok) {
        const double scale = fmax(
            1.0, fmax(fabs(u1_t), fabs(reference_t)));
        if (d_finite(u1_t) && d_finite(reference_t) &&
            fabs(u1_t - reference_t) <= 1.0e-8 * scale) {
            atomicAdd(&profile->both_success_same_root, 1ULL);
        } else {
            atomicAdd(&profile->both_success_different_root, 1ULL);
            problem = true;
            different_root = true;
        }
    } else if (!u1_ok && reference_ok) {
        atomicAdd(&profile->u1_failure_reference_success, 1ULL);
        atomicAdd(
            &profile->u1_failure_reference_success_by_site[site], 1ULL);
        problem = true;
        u1_failure = true;
    } else if (u1_ok && !reference_ok) {
        atomicAdd(&profile->u1_success_reference_failure, 1ULL);
        atomicAdd(&profile->u1_only_by_site[site], 1ULL);
        problem = true;
        u1_only = true;
    } else {
        atomicAdd(&profile->both_failure, 1ULL);
        atomicAdd(&profile->both_failure_by_site[site], 1ULL);
    }
    if (!u1_ok) {
        atomicAdd(
            &profile->u1_failures_by_reason[u1_failure_reason], 1ULL);
        if (reference_ok) {
            atomicAdd(
                &profile->u1_failure_reference_success_by_reason[
                    u1_failure_reason],
                1ULL);
        } else {
            atomicAdd(
                &profile->both_failure_by_reason[u1_failure_reason], 1ULL);
        }
    }
    if (!problem) {
        return;
    }
    atomicAdd(&profile->problems_by_site[site], 1ULL);
    if (different_root) {
        atomicAdd(&profile->different_roots_by_site[site], 1ULL);
    }
    atomicAdd(&profile->problems_by_surface[surface_index], 1ULL);
    atomicAdd(&profile->problems_by_role[role], 1ULL);
    atomicAdd(
        &profile->problems_by_surface_role[
            surface_index * kAsphereDeepProfileRoles + role],
        1ULL);
    if (u1_failure) {
        atomicAdd(&profile->u1_failures_by_surface[surface_index], 1ULL);
        atomicAdd(&profile->u1_failures_by_role[role], 1ULL);
    }
    if (different_root) {
        atomicAdd(&profile->different_roots_by_surface[surface_index], 1ULL);
        atomicAdd(&profile->different_roots_by_role[role], 1ULL);
    }
    if (u1_only) {
        atomicAdd(&profile->u1_only_by_surface[surface_index], 1ULL);
    }
    if (field >= 0 && field < kAsphereDeepProfileFields) {
        atomicAdd(&profile->problems_by_field[field], 1ULL);
    }
    if (wave >= 0 && wave < kAsphereDeepProfileWaves) {
        atomicAdd(&profile->problems_by_wave[wave], 1ULL);
    }
    if (row < kAsphereDeepProfileRows) {
        atomicAdd(&profile->problems_by_row[row], 1ULL);
        if (u1_failure) {
            atomicAdd(&profile->u1_failures_by_row[row], 1ULL);
        }
        if (different_root) {
            atomicAdd(&profile->different_roots_by_row[row], 1ULL);
        }
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ void d_record_asphere_deep_exact_s1_expected_root(
    const DeviceSurface& surface,
    const DeviceConfig& cfg,
    int surface_index,
    int site,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    bool u1_ok,
    bool reference_ok,
    double reference_t) {
    DeviceAsphereDeepProfile* profile = cfg.asphere_deep_profile;
    if (!profile ||
        site != kAsphereDeepSiteExactFirstSurfaceSeed ||
        surface_index != cfg.first_optical_surface) {
        return;
    }
    atomicAdd(&profile->exact_s1_calls, 1ULL);
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
        atomicAdd(&profile->exact_s1_expected_residual_valid, 1ULL);
    }
    const double domain_radius_squared =
        surface.slope_root_domain_radius_squared;
    const double hit_radius_squared = fma(x, x, y * y);
    if ((!d_finite(domain_radius_squared) ||
         domain_radius_squared >= 0.5 * kDeviceInf) ||
        (domain_radius_squared >= 0.0 && d_finite(hit_radius_squared) &&
         hit_radius_squared <= domain_radius_squared * (1.0 + 2.0e-12))) {
        atomicAdd(&profile->exact_s1_expected_in_slope_domain, 1ULL);
    }
    const double transverse_speed_squared = fma(lx, lx, my * my);
    const double transverse_bound_squared =
        kAsphereRootDomainMaximumRadialSlopeConservative *
        kAsphereRootDomainMaximumRadialSlopeConservative *
        transverse_speed_squared;
    const double abs_nz = fabs(nz);
    const double guard =
        512.0 * 2.2204460492503131e-16 * fmax(1.0, abs_nz);
    const double guarded_nz = abs_nz - guard;
    if (d_finite(transverse_bound_squared) && guarded_nz > 0.0 &&
        guarded_nz * guarded_nz > transverse_bound_squared) {
        atomicAdd(&profile->exact_s1_direction_monotonic, 1ULL);
    }
    if (!reference_ok || !d_finite(reference_t)) {
        return;
    }
    atomicAdd(&profile->exact_s1_reference_success, 1ULL);
    const double t_scale = fmax(1.0, fmax(fabs(reference_t), expected_t));
    const bool same_expected =
        fabs(reference_t - expected_t) <= 1.0e-8 * t_scale;
    atomicAdd(
        same_expected
            ? &profile->exact_s1_reference_same_expected
            : &profile->exact_s1_reference_different_expected,
        1ULL);
    if (!u1_ok) {
        atomicAdd(
            &profile->exact_s1_u1_failure_reference_success, 1ULL);
        atomicAdd(
            same_expected
                ? &profile->exact_s1_u1_failure_reference_same_expected
                : &profile->exact_s1_u1_failure_reference_different_expected,
            1ULL);
    }
}
} // namespace
} // namespace optiland
