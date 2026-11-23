#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/domain_materialization.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "../merit/first_order_constraints.cuh"
#include "../merit/residual_primitives.cuh"
#include "../merit/supported_ray_generation.cuh"
#include "../ray/sequential_trace.cuh"

namespace optiland {
namespace {
// Domain materialization is surface-local. One thread per candidate/surface
// exposes the interval traversal across the whole population.
__global__ void materialize_asphere_slope_domains_kernel(
    DeviceSurface* candidate_surfaces,
    DeviceConfig cfg,
    int population_size) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * cfg.surface_count;
    if (item >= item_count) {
        return;
    }
    d_materialize_asphere_slope_root_domain(&candidate_surfaces[item], cfg);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void aperture_trace_kernel(
    DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* calibrated_models,
    DeviceConfig cfg,
    int population_size,
    int aperture_pupil_count,
    double* max_heights,
    int transpose_rows,
    int track_unused_opd) {
    const int items_per_candidate =
        cfg.field_count * cfg.wavelength_count * aperture_pupil_count;
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * items_per_candidate;
    if (item >= item_count) {
        return;
    }
    int row = 0;
    int field_index = 0;
    int wave_index = 0;
    int pupil_index = 0;
    if (transpose_rows) {
        int remainder = item;
        row = remainder % population_size;
        remainder /= population_size;
        pupil_index = remainder % aperture_pupil_count;
        remainder /= aperture_pupil_count;
        wave_index = remainder % cfg.wavelength_count;
        field_index = remainder / cfg.wavelength_count;
    } else {
        row = item / items_per_candidate;
        int remainder = item - row * items_per_candidate;
        const int field_wave = remainder / aperture_pupil_count;
        pupil_index = remainder - field_wave * aperture_pupil_count;
        field_index = field_wave / cfg.wavelength_count;
        wave_index = field_wave - field_index * cfg.wavelength_count;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    const DeviceWave wave = waves[wave_index];
    if ((cfg.primary_wavelength_apertures && wave_index != cfg.primary_wave_index) || wave.weight <= 0.0) {
        return;
    }
    DeviceConfig aperture_cfg = cfg;
    aperture_cfg.fixed_budget_sampling = 0; // Physical aperture envelope is unchanged.
    aperture_cfg.num_rays = max(1, cfg.clear_aperture_rings);
    aperture_cfg.pupil_arms = cfg.clear_aperture_arms;
    aperture_cfg.pupil_sampling_type = kPupilSamplingGaussianRingArm;
    double px = 0.0;
    double py = 0.0;
    double pw = 1.0;
    d_shadow_aperture_pupil_at(
        aperture_cfg, pupil_index, &px, &py, &pw);
    DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const long long model_index =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
            cfg.wavelength_count +
        wave_index;
    const DeviceCalibratedModel* calibrated_model =
        calibrated_models ? calibrated_models + model_index : nullptr;
    DeviceRay ray = d_generate_supported_ray(
        surfaces,
        cfg,
        fields[field_index],
        px,
        py,
        pw,
        wave.value_um,
        calibrated_model);
    d_trace_ray_for_aperture_envelope<true>(
        surfaces,
        cfg,
        &ray,
        max_heights + static_cast<long long>(row) * cfg.surface_count,
        true,
        track_unused_opd != 0,
        cfg.aiming_mode == 0 || cfg.aiming_mode == 1,
        nullptr,
        nullptr,
        0.0,
        1.0,
        nullptr,
        nullptr,
        nullptr,
        0.0,
        0.0,
        nullptr,
        true);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void aperture_guard_trace_kernel(
    DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* calibrated_models,
    DeviceConfig cfg,
    int population_size,
    double* max_heights,
    double* meridional_boundary_heights,
    DeviceSpotRaySample* wavefront_guard_samples,
    DeviceChiefRaySample* chief_samples,
    int reuse_y_guards,
    int transpose_rows,
    int track_unused_opd) {
    const int items_per_candidate =
        cfg.field_count * cfg.wavelength_count * kApertureTraceProbeCount;
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * items_per_candidate;
    if (item >= item_count) {
        return;
    }
    int row = 0;
    int field_index = 0;
    int wave_index = 0;
    int guard_offset = 0;
    if (transpose_rows) {
        int remainder = item;
        row = remainder % population_size;
        remainder /= population_size;
        guard_offset = remainder % kApertureTraceProbeCount;
        remainder /= kApertureTraceProbeCount;
        wave_index = remainder % cfg.wavelength_count;
        field_index = remainder / cfg.wavelength_count;
    } else {
        row = item / items_per_candidate;
        int remainder = item - row * items_per_candidate;
        const int field_wave = remainder / kApertureTraceProbeCount;
        guard_offset = remainder - field_wave * kApertureTraceProbeCount;
        field_index = field_wave / cfg.wavelength_count;
        wave_index = field_wave - field_index * cfg.wavelength_count;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    const DeviceWave wave = waves[wave_index];
    DeviceChiefRaySample* chief_sample = nullptr;
    if (guard_offset == kApertureGuardCenterOffset && chief_samples) {
        const long long chief_item =
            (static_cast<long long>(row) * cfg.field_count + field_index) *
                cfg.wavelength_count +
            wave_index;
        chief_sample = chief_samples + chief_item;
        chief_sample->vignetted_cra_traced = 0;
        chief_sample->vignetted_cra_valid = 0;
    }
    if (cfg.primary_wavelength_apertures && wave_index != cfg.primary_wave_index) return;
    if (wave.weight <= 0.0) {
        return;
    }
    double px = 0.0;
    double py = 0.0;
    d_aperture_guard_pupil(guard_offset, &px, &py);
    DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const long long model_index =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
            cfg.wavelength_count +
        wave_index;
    const DeviceCalibratedModel* calibrated_model =
        calibrated_models ? calibrated_models + model_index : nullptr;
    DeviceRay ray = d_generate_supported_ray(
        surfaces,
        cfg,
        fields[field_index],
        px,
        py,
        1.0,
        wave.value_um,
        calibrated_model);
    DeviceSpotRaySample cached_guard{};
    int guard_index = -1;
    if (reuse_y_guards && wavefront_guard_samples &&
        guard_offset == kApertureGuardNegativeYOffset) {
        guard_index = 3;
    } else if (reuse_y_guards && wavefront_guard_samples &&
               guard_offset == kApertureGuardPositiveYOffset) {
        guard_index = 2;
    }
    int support_trace = -1;
    if (guard_offset == kApertureGuardNegativeYOffset) {
        support_trace = 0;
    } else if (guard_offset == kApertureGuardPositiveYOffset) {
        support_trace = 1;
    } else if (guard_offset == kApertureGuardCenterOffset) {
        support_trace = kMeridionalChiefTrace;
    }
    double* committed_boundary_heights = nullptr;
    double* committed_boundary_signed_y = nullptr;
    if (meridional_boundary_heights && support_trace >= 0) {
        const long long boundary_base =
            ((((static_cast<long long>(row) * cfg.field_count + field_index) *
                   cfg.wavelength_count +
               wave_index) *
                  kMeridionalSupportTraceCount +
              support_trace) *
             cfg.surface_count);
        committed_boundary_heights =
            meridional_boundary_heights + boundary_base;
        const long long signed_base =
            ((((static_cast<long long>(row) * cfg.field_count + field_index) *
                   cfg.wavelength_count +
               wave_index) *
                  kMeridionalSupportTraceCount +
              kMeridionalSignedTraceOffset + support_trace) *
             cfg.surface_count);
        committed_boundary_signed_y =
            meridional_boundary_heights + signed_base;
    }
    if (guard_index >= 0) {
        cached_guard.initial_x = ray.x;
        cached_guard.initial_y = ray.y;
        cached_guard.initial_z = ray.z;
        cached_guard.initial_l = ray.l;
        cached_guard.initial_m = ray.m;
        cached_guard.initial_n = ray.n;
    }
    d_trace_ray_for_aperture_envelope<true>(
        surfaces,
        cfg,
        &ray,
        max_heights + static_cast<long long>(row) * cfg.surface_count,
        true,
        track_unused_opd || guard_index >= 0,
        cfg.aiming_mode == 0 || cfg.aiming_mode == 1,
        committed_boundary_heights,
        committed_boundary_signed_y,
        0.0,
        1.0,
        nullptr,
        nullptr,
        nullptr,
        0.0,
        0.0,
        nullptr,
        true);
    if (chief_sample && cfg.operand_max_chief_ray_angle) {
        chief_sample->vignetted_cra_l = ray.l;
        chief_sample->vignetted_cra_m = ray.m;
        chief_sample->vignetted_cra_n = ray.n;
        chief_sample->vignetted_cra_traced = 1;
        chief_sample->vignetted_cra_valid = ray.valid;
    }
    if (guard_index >= 0) {
        ray.nominal_weight *= wave.weight;
        ray.intensity *= wave.weight;
        cached_guard.x = ray.x;
        cached_guard.y = ray.y;
        cached_guard.z = ray.z;
        cached_guard.l = ray.l;
        cached_guard.m = ray.m;
        cached_guard.n = ray.n;
        cached_guard.opd = ray.opd;
        cached_guard.pupil_x = px;
        cached_guard.pupil_y = py;
        cached_guard.stop_x = ray.stop_x;
        cached_guard.stop_y = ray.stop_y;
        cached_guard.stop_reached = ray.stop_reached;
        cached_guard.failure_code = ray.failure_code;
        cached_guard.min_backward_air_gap = ray.min_backward_air_gap;
        cached_guard.min_backward_glass_gap = ray.min_backward_glass_gap;
        cached_guard.failure_surface = ray.failure_surface;
        cached_guard.weight = d_ray_nominal_weight(ray);
        cached_guard.wavefront_weight = cached_guard.weight;
        cached_guard.weighted_progress =
            cached_guard.weight * d_ray_progress(ray, cfg.surface_count);
        cached_guard.aim_usable = d_ray_aim_usable_for_spot(ray) ? 1 : 0;
        cached_guard.normal_aperture_blockage = d_normal_secondary_blockage(ray) ? 1 : 0;
        cached_guard.usable = d_ray_usable_for_spot(ray) ? 1 : 0;
        const long long guard_item =
            ((static_cast<long long>(row) * cfg.field_count + field_index) *
                 cfg.wavelength_count +
             wave_index) *
                4 +
            guard_index;
        wavefront_guard_samples[guard_item] = cached_guard;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void finalize_apertures_kernel(
    const DeviceSurface* base_surfaces,
    DeviceSurface* candidate_surfaces,
    DeviceConfig cfg,
    int population_size,
    const double* max_heights,
    int defer_slope_domains) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const double* row_heights =
        max_heights + static_cast<long long>(row) * cfg.surface_count;
    const double scale = fmax(1.0, cfg.clear_aperture_margin);
    for (int i = 0; i < cfg.surface_count; ++i) {
        DeviceSurface& surface = surfaces[i];
        if (surface.kind == kKindObject) {
            continue;
        }
        const bool explicit_stop =
            surface.clear_aperture_fixed ||
            (cfg.aperture_type == 3 && surface.aperture_radius > 0.0);
        if (surface.is_stop) {
            if (explicit_stop) {
                if (surface.clear_aperture_fixed &&
                    surface.semi_aperture <= 0.0 &&
                    surface.aperture_radius > 0.0) {
                    surface.semi_aperture = surface.aperture_radius;
                }
                continue;
            }
            const double stop_radius =
                (cfg.aperture_type != 3 &&
                 (cfg.aiming_mode == 0 || cfg.aiming_mode == 1))
                    ? row_heights[i]
                    : d_aperture_stop_radius(
                          surfaces, cfg, cfg.primary_wavelength);
            if (d_finite(stop_radius) && stop_radius > 0.0) {
                surface.semi_aperture = stop_radius;
                surface.aperture_radius = stop_radius;
                if (!surface.mechanical_aperture_fixed) {
                    surface.mechanical_aperture = stop_radius;
                }
            }
            continue;
        }
        if (surface.clear_aperture_fixed) {
            if (surface.semi_aperture <= 0.0 && surface.aperture_radius > 0.0) {
                surface.semi_aperture = surface.aperture_radius;
            }
            continue;
        }
        const double original = base_surfaces[i].semi_aperture;
        if (surface.aperture_radius > 0.0) {
            surface.semi_aperture = fmax(original, surface.aperture_radius);
        } else if (row_heights[i] > 0.0) {
            surface.semi_aperture = row_heights[i] * scale;
        } else if (original > 0.0) {
            surface.semi_aperture = original;
        }
    }
    d_update_mechanical_apertures_from_groups(surfaces, cfg);
    if (!defer_slope_domains) {
        d_update_asphere_slope_root_domains(surfaces, cfg);
    }
}
} // namespace
} // namespace optiland
