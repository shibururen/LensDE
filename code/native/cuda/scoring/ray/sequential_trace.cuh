#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"
#include "asphere_intersection.cuh"
#include "intersection_gate.cuh"
#include "paraxial_generation.cuh"
#include "refraction.cuh"
#include "structural_trace.cuh"

namespace optiland {
namespace {
template <bool RecoverApertureRoot = false>
__device__ bool d_trace_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    DeviceRay* ray,
    double* max_heights = nullptr,
    bool atomic_heights = false,
    DeviceStructuralTrace* structure = nullptr,
    bool track_opd = false,
    DeviceStructuralTrace* structure_y = nullptr,
    bool ignore_automatic_stop_aperture = false,
    double* pupil_order_heights = nullptr,
    double pupil_order_ux = 0.0,
    double pupil_order_uy = 0.0) {
    if (!ray) {
        return false;
    }
    ray->stop_x = NAN;
    ray->stop_y = NAN;
    ray->stop_reached = 0;
    ray->min_backward_air_gap = 0.0;
    ray->min_backward_glass_gap = 0.0;
    const bool secondary_mask = d_defer_secondary_apertures(cfg, ray->wavelength_um);
    double vertex_z = 0.0;
    double n_before = 1.0;
    const double object_space_dz = ray->n;
    for (int i = 0; i < cfg.surface_count; ++i) {
        const DeviceSurface& surface = surfaces[i];
        if (i < cfg.first_optical_surface || surface.kind == kKindObject) {
            continue;
        }
        const double local_z = ray->z - vertex_z;
        double t = 0.0;
        bool intersection_ok = false;
        int asphere_root_path = 0;
        if (ray->valid) {
            // Every ray that contributes to aperture sizing or merit scoring
            // uses the CPU-equivalent first-forward-root contract.  Auxiliary
            // aiming/calibration probes outside d_trace_ray retain the fast
            // Newton path because they only provide iterative seeds.
            intersection_ok =
                surface.kind == kKindEvenAsphere
                    ? d_intersect_even_asphere_configured(
                          surface,
                          cfg,
                          ray->x,
                          ray->y,
                          local_z,
                          ray->l,
                          ray->m,
                          ray->n,
                          true,
                          false,
                          false,
                          cfg.asphere_root_profile ? &asphere_root_path : nullptr,
                          &t)
                    : d_intersect_surface(
                          surface,
                          ray->x,
                          ray->y,
                          local_z,
                          ray->l,
                          ray->m,
                          ray->n,
                          &t);
            // Match the CPU aiming contract: prefer an in-stop-domain root,
            // and use the unbounded extension only when an aperture-ignoring
            // residual probe has no physical-domain intersection at all.
            if (!intersection_ok && surface.kind == kKindEvenAsphere &&
                (ignore_automatic_stop_aperture || secondary_mask) && surface.is_stop) {
                intersection_ok =
                    d_intersect_even_asphere_configured(
                        surface,
                        cfg,
                        ray->x,
                        ray->y,
                        local_z,
                        ray->l,
                        ray->m,
                        ray->n,
                        true,
                        false,
                        true,
                        cfg.asphere_root_profile ? &asphere_root_path : nullptr,
                        &t);
            }
        }
        if (ray->valid && surface.kind == kKindEvenAsphere &&
            cfg.asphere_root_profile) {
            const unsigned int active_mask = __activemask();
            const unsigned int confirmation_mask = __ballot_sync(
                active_mask, (asphere_root_path & 1) != 0);
            const unsigned int slow_mask = __ballot_sync(
                active_mask, (asphere_root_path & 2) != 0);
            const unsigned int full_mask = __ballot_sync(
                active_mask, (asphere_root_path & 4) != 0);
            const int lane = threadIdx.x & 31;
            const int leader = __ffs(active_mask) - 1;
            if (lane == leader) {
                DeviceAsphereRootProfile* profile = cfg.asphere_root_profile;
                const unsigned long long active_count = __popc(active_mask);
                const unsigned long long confirmation_count =
                    __popc(confirmation_mask);
                const unsigned long long slow_count = __popc(slow_mask);
                const unsigned long long full_count = __popc(full_mask);
                atomicAdd(&profile->warp_surface_visits, 1ULL);
                atomicAdd(&profile->warp_active_lane_visits, active_count);
                if (confirmation_count > 0) {
                    atomicAdd(
                        &profile->confirmation_lane_visits,
                        confirmation_count);
                    atomicAdd(&profile->confirmation_warp_visits, 1ULL);
                    atomicAdd(
                        &profile->confirmation_wasted_lane_slots,
                        active_count - confirmation_count);
                    if (confirmation_count < active_count) {
                        atomicAdd(
                            &profile->mixed_confirmation_warp_visits,
                            1ULL);
                    } else {
                        atomicAdd(
                            &profile->all_confirmation_warp_visits,
                            1ULL);
                    }
                    if (confirmation_count == 1) {
                        atomicAdd(
                            &profile->confirmation_warps_1_lane,
                            1ULL);
                    } else if (confirmation_count <= 4) {
                        atomicAdd(
                            &profile->confirmation_warps_2_4_lanes,
                            1ULL);
                    } else if (confirmation_count <= 8) {
                        atomicAdd(
                            &profile->confirmation_warps_5_8_lanes,
                            1ULL);
                    } else if (confirmation_count <= 16) {
                        atomicAdd(
                            &profile->confirmation_warps_9_16_lanes,
                            1ULL);
                    } else if (confirmation_count <= 31) {
                        atomicAdd(
                            &profile->confirmation_warps_17_31_lanes,
                            1ULL);
                    } else {
                        atomicAdd(
                            &profile->confirmation_warps_32_lanes,
                            1ULL);
                    }
                    if (i < kMaxAsphereRootProfileSurfaces) {
                        atomicAdd(
                            &profile->confirmation_lanes_by_surface[i],
                            confirmation_count);
                        if (confirmation_count < active_count) {
                            atomicAdd(
                                &profile->mixed_confirmation_warps_by_surface[i],
                                1ULL);
                        }
                    }
                }
                if (slow_count > 0) {
                    atomicAdd(&profile->slow_lane_visits, slow_count);
                    atomicAdd(&profile->slow_warp_visits, 1ULL);
                    atomicAdd(
                        &profile->slow_wasted_lane_slots,
                        active_count - slow_count);
                    if (slow_count < active_count) {
                        atomicAdd(&profile->mixed_slow_warp_visits, 1ULL);
                    } else {
                        atomicAdd(&profile->all_slow_warp_visits, 1ULL);
                    }
                    if (slow_count == 1) {
                        atomicAdd(&profile->slow_warps_1_lane, 1ULL);
                    } else if (slow_count <= 4) {
                        atomicAdd(&profile->slow_warps_2_4_lanes, 1ULL);
                    } else if (slow_count <= 8) {
                        atomicAdd(&profile->slow_warps_5_8_lanes, 1ULL);
                    } else if (slow_count <= 16) {
                        atomicAdd(&profile->slow_warps_9_16_lanes, 1ULL);
                    } else if (slow_count <= 31) {
                        atomicAdd(&profile->slow_warps_17_31_lanes, 1ULL);
                    } else {
                        atomicAdd(&profile->slow_warps_32_lanes, 1ULL);
                    }
                    if (i < kMaxAsphereRootProfileSurfaces) {
                        atomicAdd(
                            &profile->slow_lanes_by_surface[i], slow_count);
                        if (slow_count < active_count) {
                            atomicAdd(
                                &profile->mixed_slow_warps_by_surface[i],
                                1ULL);
                        }
                    }
                }
                if (full_count > 0) {
                    atomicAdd(&profile->full_lane_visits, full_count);
                    atomicAdd(&profile->full_warp_visits, 1ULL);
                    if (i < kMaxAsphereRootProfileSurfaces) {
                        atomicAdd(
                            &profile->full_lanes_by_surface[i], full_count);
                    }
                }
            }
        }
        if (!ray->valid) {
            ray->intensity = 0.0;
        } else if (!intersection_ok) {
            d_mark_ray_failure(ray, 2, i, 1.0);
        } else {
            if (track_opd) {
                ray->opd += fabs(t * n_before);
            }
            const double step_z = t * ray->n;
            const double z_tol =
                1.0e-10 * fmax(1.0, fabs(surface.thickness));
            const bool backward_by_parameter = t < -1.0e-10;
            const bool backward_by_z = step_z < -z_tol;
            const bool allowed_object_space_retro =
                i == cfg.first_optical_surface &&
                object_space_dz < -1.0e-12 &&
                t >= -1.0e-10 &&
                backward_by_z;
            if ((backward_by_parameter || backward_by_z) &&
                !allowed_object_space_retro) {
                const double reference = fmax(1.0e-9, fabs(surface.thickness));
                const double severity = fmax(fabs(t), fabs(step_z) / reference);
                d_mark_backward_warning(ray, i, severity);
                // Inspect the next face at the previous intercept's height.
                // Aperture sizing intentionally excludes backward histories,
                // so its radius cannot be used to detect this overlap.
                if (i > cfg.first_optical_surface && !ray->aperture_blocked &&
                    surface.kind != kKindObject && surface.kind != kKindImage) {
                    const DeviceSurface& previous = surfaces[i - 1];
                    if (previous.kind != kKindObject && !previous.reflective &&
                        previous.thickness >= 0.0) {
                        const double gap = vertex_z +
                            d_sag(surface, hypot(ray->x, ray->y)) - ray->z;
                        if (d_finite(gap) && gap < -1.0e-6) {
                            double& minimum = d_interval_is_glass(previous)
                                ? ray->min_backward_glass_gap
                                : ray->min_backward_air_gap;
                            minimum = fmin(minimum, gap);
                        }
                    }
                }
            }
            ray->x += ray->l * t;
            ray->y += ray->m * t;
            ray->z += ray->n * t;
            bool recovered_nearest_root_before_checks = false;
            if constexpr (RecoverApertureRoot) {
                if (max_heights &&
                    d_aperture_asphere_root_looks_far(surface, ray->x, ray->y)) {
                    const double n_post =
                        d_surface_refractive_index(surface, cfg, ray->wavelength_um);
                    recovered_nearest_root_before_checks =
                        d_try_recover_aperture_asphere_root(
                            surfaces,
                            cfg,
                            i,
                            ray->x - ray->l * t,
                            ray->y - ray->m * t,
                            ray->z - ray->n * t,
                            ray->z - ray->n * t - vertex_z,
                            ray->l,
                            ray->m,
                            ray->n,
                            n_before,
                            n_post,
                            t,
                            track_opd,
                            ray);
                }
            }
            if (surface.is_stop && !ray->stop_reached &&
                d_finite(ray->x) && d_finite(ray->y)) {
                ray->stop_x = ray->x;
                ray->stop_y = ray->y;
                ray->stop_reached = 1;
            }
            d_record_structural_hit(structure, cfg, i, *ray);
            d_record_structural_hit(structure_y, cfg, i, *ray);
            if (!recovered_nearest_root_before_checks) {
                const bool skip_automatic_stop_clip =
                    ignore_automatic_stop_aperture && surface.is_stop &&
                    !surface.clear_aperture_fixed && cfg.aperture_type != 3 &&
                    (cfg.aiming_mode == 0 || cfg.aiming_mode == 1);
                const double clip_radius = skip_automatic_stop_clip
                                               ? 0.0
                                               : d_effective_aperture_clip_radius(
                                                     surfaces,
                                                     cfg,
                                                     i,
                                                     ray->wavelength_um);
                if (secondary_mask) {
                    const double radius = d_spectral_surface_radius(surface);
                    const double limit = 1.01 * radius;
                    if (radius > 0.0 && ray->x * ray->x + ray->y * ray->y > limit * limit) {
                        ray->aperture_blocked = 1;
                        if (ray->aperture_blocked_surface < 0) ray->aperture_blocked_surface = i;
                    }
                } else if (clip_radius > 0.0) {
                    const double radial = hypot(ray->x, ray->y);
                    const double limit = 1.01 * clip_radius;
                    if (radial > limit) {
                        d_mark_ray_failure(
                            ray,
                            3,
                            i,
                            (radial - clip_radius) / fmax(clip_radius, kDeviceEps));
                    }
                }
                if (ray->valid && surface.kind != kKindImage) {
                    double nx = 0.0;
                    double ny = 0.0;
                    double nz = -1.0;
                    if (!d_surface_normal(
                            surface,
                            ray->x,
                            ray->y,
                            &nx,
                            &ny,
                            &nz)) {
                        d_mark_ray_failure(ray, 7, i, 1.0);
                    } else {
                        const double n_post =
                            d_surface_refractive_index(surface, cfg, ray->wavelength_um);
                        double ox = ray->l;
                        double oy = ray->m;
                        double oz = ray->n;
                        if (!d_refract(
                                ray->l,
                                ray->m,
                                ray->n,
                                nx,
                                ny,
                                nz,
                                n_before,
                                n_post,
                                &ox,
                                &oy,
                                &oz)) {
                            bool recovered_nearest_root = false;
                            if constexpr (RecoverApertureRoot) {
                                recovered_nearest_root =
                                    max_heights &&
                                    d_try_recover_aperture_asphere_root(
                                        surfaces,
                                        cfg,
                                        i,
                                        ray->x - ray->l * t,
                                        ray->y - ray->m * t,
                                        ray->z - ray->n * t,
                                        ray->z - ray->n * t - vertex_z,
                                        ray->l,
                                        ray->m,
                                        ray->n,
                                        n_before,
                                        n_post,
                                        t,
                                        track_opd,
                                        ray);
                            }
                            if (!recovered_nearest_root) {
                                d_mark_ray_failure(ray, 4, i, 1.0);
                            }
                        } else {
                            ray->l = ox;
                            ray->m = oy;
                            ray->n = oz;
                        }
                    }
                }
            }
            if (max_heights && ray->valid) {
                const double height = hypot(ray->x, ray->y);
                if (atomic_heights) {
                    d_atomic_max_nonnegative(max_heights + i, height);
                } else {
                    max_heights[i] = fmax(max_heights[i], height);
                }
            }
        }
        if (pupil_order_heights) {
            pupil_order_heights[i] =
                ray->valid && d_finite(ray->x) && d_finite(ray->y)
                    ? ray->x * pupil_order_ux + ray->y * pupil_order_uy
                    : NAN;
        }
        n_before = d_surface_refractive_index(surface, cfg, ray->wavelength_um);
        if (d_finite(surface.thickness) &&
            fabs(surface.thickness) < 0.5 * kDeviceInf) {
            vertex_z += surface.thickness;
        }
    }
    return ray->valid != 0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// CPU clear-aperture sizing records a ray history only after the complete
// trace has shown that the path stayed sequential. Updating a shared maximum
// while the ray is still in flight is not equivalent: a ray can reach a large
// height on an early surface and only be identified as backward-propagating on
// a later one. Keep per-ray heights until that decision is known, then commit
// them to the candidate envelope. Invalid (but sequential) histories remain
// eligible because the CPU production calls use require_full_trace=false.
constexpr int kMaxApertureHistorySurfaces = 64;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <bool RecoverApertureRoot = false>
__device__ bool d_trace_ray_for_aperture_envelope(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    DeviceRay* ray,
    double* envelope_heights,
    bool atomic_heights,
    bool track_opd = false,
    bool ignore_automatic_stop_aperture = false,
    double* committed_ray_heights = nullptr,
    double* committed_signed_heights = nullptr,
    double signed_ux = 0.0,
    double signed_uy = 0.0,
    DeviceStructuralTrace* structure = nullptr,
    DeviceStructuralTrace* structure_y = nullptr,
    double* pupil_order_heights = nullptr,
    double pupil_order_ux = 0.0,
    double pupil_order_uy = 0.0,
    double* secondary_envelope_heights = nullptr,
    bool atomic_secondary_heights = false) {
    double* const trace_signed_heights =
        committed_signed_heights ? nullptr : pupil_order_heights;
    const double trace_signed_ux =
        committed_signed_heights ? signed_ux : pupil_order_ux;
    const double trace_signed_uy =
        committed_signed_heights ? signed_uy : pupil_order_uy;
    if (cfg.surface_count > kMaxApertureHistorySurfaces) {
        // Preserve the historical behavior for an unsupported oversized
        // device system instead of overflowing the bounded local history.
        return d_trace_ray<RecoverApertureRoot>(
            surfaces,
            cfg,
            ray,
            envelope_heights,
            atomic_heights,
            structure,
            track_opd,
            structure_y,
            ignore_automatic_stop_aperture,
            committed_signed_heights ? committed_signed_heights : trace_signed_heights,
            trace_signed_ux,
            trace_signed_uy);
    }
    double ray_heights[kMaxApertureHistorySurfaces];
    double ray_signed_heights[kMaxApertureHistorySurfaces];
    for (int i = 0; i < cfg.surface_count; ++i) {
        ray_heights[i] = 0.0;
        ray_signed_heights[i] = NAN;
    }
    const bool valid = d_trace_ray<RecoverApertureRoot>(
        surfaces,
        cfg,
        ray,
        ray_heights,
        false,
        structure,
        track_opd,
        structure_y,
        ignore_automatic_stop_aperture,
        committed_signed_heights ? ray_signed_heights : pupil_order_heights,
        trace_signed_ux,
        trace_signed_uy);
    if (committed_signed_heights) {
        for (int i = 0; i < cfg.surface_count; ++i) {
            committed_signed_heights[i] = ray_signed_heights[i];
        }
    }
    if (!ray || ray->backward_propagation) {
        return valid;
    }
    for (int i = 0; i < cfg.surface_count; ++i) {
        const double height = ray_heights[i];
        if (committed_ray_heights) {
            committed_ray_heights[i] =
                d_finite(height) && height >= 0.0 ? height : 0.0;
        }
        if (!envelope_heights) {
            continue;
        }
        if (!(height > 0.0) || !d_finite(height)) {
            continue;
        }
        if (atomic_heights) {
            d_atomic_max_nonnegative(envelope_heights + i, height);
        } else {
            envelope_heights[i] = fmax(envelope_heights[i], height);
        }
        if (secondary_envelope_heights) {
            if (atomic_secondary_heights) {
                d_atomic_max_nonnegative(
                    secondary_envelope_heights + i, height);
            } else {
                secondary_envelope_heights[i] = fmax(
                    secondary_envelope_heights[i], height);
            }
        }
    }
    return valid;
}
} // namespace
} // namespace optiland
