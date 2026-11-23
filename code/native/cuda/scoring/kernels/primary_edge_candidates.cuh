#pragma once
#include "../prelude.cuh"
#include "../aiming/chief_geometry.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/edge_scale_solver.cuh"
#include "../aiming/seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/pupil_paraxial.cuh"

namespace optiland {
namespace {
template <bool kRepairBuild>
__global__ void uc5_batch_primary_xplus_candidates_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int center_field_index,
    int field_index,
    int wave_index,
    DeviceCalibratedModel* models,
    DeviceUC5CandidateFamilyState* states,
    int skip_valid_models) {
    constexpr int kGroupSize = 8;
    constexpr int kSharedSampleWords =
        (sizeof(DeviceEdgeStopSample) + sizeof(double) - 1) / sizeof(double);
    // CUDA 11.2 rejects a __shared__ array whose element type has default
    // member initializers.  Double-word backing keeps the required alignment;
    // every logical slot is explicitly initialized below before use.
    __shared__ double base_sample_storage[128 * kSharedSampleWords];
    __shared__ double predicted_sample_storage[128 * kSharedSampleWords];
    DeviceEdgeStopSample* base_samples =
        reinterpret_cast<DeviceEdgeStopSample*>(base_sample_storage);
    DeviceEdgeStopSample* predicted_samples =
        reinterpret_cast<DeviceEdgeStopSample*>(predicted_sample_storage);
    const int global_thread = blockIdx.x * blockDim.x + threadIdx.x;
    const int item = global_thread / kGroupSize;
    const int lane = threadIdx.x & (kGroupSize - 1);
    const int group_begin = threadIdx.x - lane;
    const int lane_in_warp = threadIdx.x & 31;
    const unsigned int group_mask =
        0xffu << (lane_in_warp - lane);
    base_samples[threadIdx.x] = DeviceEdgeStopSample{};
    predicted_samples[threadIdx.x] = DeviceEdgeStopSample{};
    if (item >= population_size || field_index < 0 ||
        field_index >= cfg.field_count || wave_index < 0 ||
        wave_index >= cfg.wavelength_count) {
        return;
    }
    const int models_per_row = cfg.field_count * cfg.wavelength_count;
    const long long model_index =
        static_cast<long long>(item) * models_per_row +
        field_index * cfg.wavelength_count + wave_index;
    DeviceUC5CandidateFamilyState& state = states[item];
    fields = d_candidate_row_fields(fields, cfg, item);
    const DeviceField& field = fields[field_index];
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(item) * cfg.surface_count;
    const DeviceWave wave = waves[wave_index];
    const int counter_cell =
        d_uc5_counter_cell(cfg, wave.value_um, 0, kRepairBuild);

    if (lane == 0) {
        state = DeviceUC5CandidateFamilyState{};
        if (skip_valid_models && models[model_index].valid &&
            models[model_index].calibrated_edges_all_valid) {
            state.skipped_existing_valid = 1;
        } else {
            DeviceCalibratedModel& model = models[model_index];
            if (wave.weight > 0.0 && model.chief_seed.valid) {
                state.prepared = 1;
                state.anchor.px = 1.0;
                state.anchor.py = 0.0;
                state.max_residual = d_finite(model.fit_rms)
                    ? fmax(0.0, model.fit_rms)
                    : 0.0;
                DeviceAsphereUC5Profile* profile = g_asphere_uc5_profile;
                if (profile) {
                    atomicAdd(
                        &profile->anchor_entered[counter_cell], 1ULL);
                }
                const double rs =
                    d_aperture_stop_radius(surfaces, cfg, wave.value_um);
                const double search_radius =
                    d_first_surface_search_radius(surfaces, cfg, wave.value_um);
                const double edge_radius = d_clamp(
                    fmax(
                        1.0e-9,
                        d_entrance_pupil_radius_from_cfg(
                            surfaces, cfg, wave.value_um)),
                    1.0e-9,
                    fmax(1.0e-9, search_radius));
                const DeviceField& radius_center_field =
                    fields[center_field_index];
                const double center_beam_edge_radius =
                    d_center_meridional_radius(
                        surfaces, cfg, radius_center_field, wave.value_um);
                const double first_surface_edge_radius =
                    d_finite(center_beam_edge_radius) &&
                        center_beam_edge_radius > 1.0e-9
                    ? fmin(edge_radius, center_beam_edge_radius)
                    : edge_radius;
                DeviceSeed canonical_full_seed;
                canonical_full_seed.x =
                    model.chief_seed.x + first_surface_edge_radius;
                canonical_full_seed.y = model.chief_seed.y;
                canonical_full_seed.valid = 1;
                const DeviceCalibratedModel* same_field_wave_model =
                    d_primary_anchored_same_field_wave_model(
                        models, model_index, cfg, wave_index);
                if (same_field_wave_model && same_field_wave_model->valid &&
                    same_field_wave_model->chief_seed.valid &&
                    same_field_wave_model->edge_anchors[0].valid &&
                    same_field_wave_model->edge_anchors[0].seed.valid) {
                    DeviceSeed cached_full_seed;
                    cached_full_seed.x = model.chief_seed.x +
                        (same_field_wave_model->edge_anchors[0].seed.x -
                         same_field_wave_model->chief_seed.x);
                    cached_full_seed.y = model.chief_seed.y +
                        (same_field_wave_model->edge_anchors[0].seed.y -
                         same_field_wave_model->chief_seed.y);
                    cached_full_seed.valid = 1;
                    DeviceEdgeStopSample cached_sample =
                        d_probe_calibrated_edge_seed(
                            surfaces,
                            cfg,
                            field,
                            wave.value_um,
                            cached_full_seed,
                            1.0,
                            0.0,
                            rs,
                            true);
                    if (cached_sample.valid) {
                        state.cached_traceable = 1;
                        state.best_sample = cached_sample;
                        state.best_full_seed = cached_full_seed;
                        if (profile) {
                            atomicAdd(
                                &profile->cached_traceable[counter_cell],
                                1ULL);
                        }
                        const int first_aiming_surface =
                            d_first_ray_aiming_surface(
                                surfaces, cfg, wave.value_um);
                        const bool enforce_edge_branch_continuity =
                            first_aiming_surface >= 0 &&
                            first_aiming_surface < cfg.surface_count &&
                            surfaces[first_aiming_surface].kind ==
                                kKindEvenAsphere;
                        const double residual_limit = fmax(
                            fmax(0.0, cfg.ray_aiming_tolerance),
                            fmax(
                                0.0,
                                cfg.calibrated_edge_residual_stop_fraction) *
                                d_physical_stop_radius(surfaces, cfg, rs));
                        d_accept_calibrated_edge_sample(
                            surfaces,
                            cfg,
                            wave.value_um,
                            &state.anchor,
                            field,
                            cached_sample.seed,
                            model.chief_seed,
                            first_surface_edge_radius,
                            1.0,
                            0.0,
                            enforce_edge_branch_continuity,
                            rs,
                            cached_sample.sx,
                            cached_sample.sy,
                            residual_limit,
                            &state.max_residual);
                        if (!state.anchor.valid) {
                            const double target_dx =
                                (1.0 - field.fvdx) * rs;
                            const double target_dr = fabs(target_dx);
                            const double stop_dx =
                                cached_sample.sx - model.chief_stop_x;
                            const double projected = target_dr > 1.0e-12
                                ? stop_dx * target_dx / target_dr
                                : NAN;
                            if (d_finite(projected) && projected > 1.0e-12) {
                                const double predicted_scale = d_clamp(
                                    target_dr / projected, 1.0e-3, 1.25);
                                if (fabs(predicted_scale - 1.0) >
                                    1.0e-7 *
                                        fmax(1.0, fabs(predicted_scale))) {
                                    if (profile) {
                                        atomicAdd(
                                            &profile
                                                 ->predicted_probes[counter_cell],
                                            1ULL);
                                    }
                                    const DeviceSeed predicted_seed =
                                        d_scaled_edge_seed(
                                            model.chief_seed,
                                            cached_full_seed,
                                            predicted_scale);
                                    DeviceEdgeStopSample predicted =
                                        d_probe_calibrated_edge_seed(
                                            surfaces,
                                            cfg,
                                            field,
                                            wave.value_um,
                                            predicted_seed,
                                            1.0,
                                            0.0,
                                            rs,
                                            true);
                                    if (predicted.valid) {
                                        if (predicted.residual <
                                            state.best_sample.residual) {
                                            if (hypot(
                                                    state.best_sample.seed.x -
                                                        predicted.seed.x,
                                                    state.best_sample.seed.y -
                                                        predicted.seed.y) >
                                                1.0e-12) {
                                                state.previous_sample =
                                                    state.best_sample;
                                            }
                                            state.best_sample = predicted;
                                        } else if (
                                            hypot(
                                                state.best_sample.seed.x -
                                                    predicted.seed.x,
                                                state.best_sample.seed.y -
                                                    predicted.seed.y) >
                                                1.0e-12) {
                                            state.previous_sample = predicted;
                                        }
                                        d_accept_calibrated_edge_sample(
                                            surfaces,
                                            cfg,
                                            wave.value_um,
                                            &state.anchor,
                                            field,
                                            predicted.seed,
                                            model.chief_seed,
                                            first_surface_edge_radius,
                                            1.0,
                                            0.0,
                                            enforce_edge_branch_continuity,
                                            rs,
                                            predicted.sx,
                                            predicted.sy,
                                            residual_limit,
                                            &state.max_residual);
                                    }
                                }
                            }
                        }
                        if (state.anchor.valid && profile) {
                            atomicAdd(
                                &profile->cached_accepted[counter_cell], 1ULL);
                        }
                    }
                }
            }
        }
    }
    __syncwarp(group_mask);

    if (!state.prepared || state.skipped_existing_valid ||
        state.anchor.valid || state.cached_traceable) {
        return;
    }
    const DeviceCalibratedModel model = models[model_index];
    const double rs = d_aperture_stop_radius(surfaces, cfg, wave.value_um);
    const double search_radius =
        d_first_surface_search_radius(surfaces, cfg, wave.value_um);
    const double edge_radius = d_clamp(
        fmax(
            1.0e-9,
            d_entrance_pupil_radius_from_cfg(surfaces, cfg, wave.value_um)),
        1.0e-9,
        fmax(1.0e-9, search_radius));
    const double center_beam_edge_radius = d_center_meridional_radius(
        surfaces, cfg, fields[center_field_index], wave.value_um);
    const double first_surface_edge_radius =
        d_finite(center_beam_edge_radius) && center_beam_edge_radius > 1.0e-9
        ? fmin(edge_radius, center_beam_edge_radius)
        : edge_radius;
    DeviceSeed canonical_full_seed;
    canonical_full_seed.x = model.chief_seed.x + first_surface_edge_radius;
    canonical_full_seed.y = model.chief_seed.y;
    canonical_full_seed.valid = 1;
    if (lane < 3) {
        const double scales[3] = {0.25, 0.5, 1.0};
        DeviceAsphereUC5Profile* profile = g_asphere_uc5_profile;
        if (profile) {
            atomicAdd(&profile->base_probes[counter_cell], 1ULL);
        }
        const DeviceSeed seed = d_scaled_edge_seed(
            model.chief_seed, canonical_full_seed, scales[lane]);
        base_samples[threadIdx.x] = d_probe_calibrated_edge_seed(
            surfaces,
            cfg,
            field,
            wave.value_um,
            seed,
            1.0,
            0.0,
            rs,
            true);
    }
    __syncwarp(group_mask);
    if (lane >= 3 && lane < 6) {
        const int scale_index = lane - 3;
        const DeviceEdgeStopSample base =
            base_samples[group_begin + scale_index];
        if (base.valid) {
            const double scales[3] = {0.25, 0.5, 1.0};
            const double target_dx = (1.0 - field.fvdx) * rs;
            const double target_dr = fabs(target_dx);
            const double stop_dx = base.sx - model.chief_stop_x;
            const double projected = target_dr > 1.0e-12
                ? stop_dx * target_dx / target_dr
                : NAN;
            if (d_finite(projected) && projected > 1.0e-12) {
                const double predicted_scale = d_clamp(
                    scales[scale_index] * target_dr / projected,
                    1.0e-3,
                    1.25);
                if (fabs(predicted_scale - scales[scale_index]) >
                    1.0e-7 * fmax(1.0, fabs(predicted_scale))) {
                    DeviceAsphereUC5Profile* profile = g_asphere_uc5_profile;
                    if (profile) {
                        atomicAdd(
                            &profile->predicted_probes[counter_cell], 1ULL);
                    }
                    const DeviceSeed predicted_seed = d_scaled_edge_seed(
                        model.chief_seed,
                        canonical_full_seed,
                        predicted_scale);
                    predicted_samples[group_begin + scale_index] =
                        d_probe_calibrated_edge_seed(
                            surfaces,
                            cfg,
                            field,
                            wave.value_um,
                            predicted_seed,
                            1.0,
                            0.0,
                            rs,
                            true);
                }
            }
        }
    }
    __syncwarp(group_mask);
    if (lane != 0) {
        return;
    }

    const int first_aiming_surface =
        d_first_ray_aiming_surface(surfaces, cfg, wave.value_um);
    const bool enforce_edge_branch_continuity =
        first_aiming_surface >= 0 && first_aiming_surface < cfg.surface_count &&
        surfaces[first_aiming_surface].kind == kKindEvenAsphere;
    const double residual_limit = fmax(
        fmax(0.0, cfg.ray_aiming_tolerance),
        fmax(0.0, cfg.calibrated_edge_residual_stop_fraction) *
            d_physical_stop_radius(surfaces, cfg, rs));
    auto remember_sample = [&](const DeviceEdgeStopSample& sample) {
        if (!sample.valid) {
            return;
        }
        if (!state.best_sample.valid ||
            sample.residual < state.best_sample.residual) {
            if (state.best_sample.valid &&
                hypot(
                    state.best_sample.seed.x - sample.seed.x,
                    state.best_sample.seed.y - sample.seed.y) > 1.0e-12) {
                state.previous_sample = state.best_sample;
            }
            state.best_sample = sample;
            state.best_full_seed = canonical_full_seed;
        } else if (
            hypot(
                state.best_sample.seed.x - sample.seed.x,
                state.best_sample.seed.y - sample.seed.y) > 1.0e-12 &&
            (!state.previous_sample.valid ||
             sample.residual < state.previous_sample.residual)) {
            state.previous_sample = sample;
        }
    };
    for (int scale_index = 0;
         scale_index < 3 && !state.anchor.valid;
         ++scale_index) {
        const DeviceEdgeStopSample samples[2] = {
            base_samples[group_begin + scale_index],
            predicted_samples[group_begin + scale_index]};
        for (int kind = 0; kind < 2 && !state.anchor.valid; ++kind) {
            const DeviceEdgeStopSample& sample = samples[kind];
            if (!sample.valid) {
                continue;
            }
            if (!d_accept_calibrated_edge_sample(
                    surfaces,
                    cfg,
                    wave.value_um,
                    &state.anchor,
                    field,
                    sample.seed,
                    model.chief_seed,
                    first_surface_edge_radius,
                    1.0,
                    0.0,
                    enforce_edge_branch_continuity,
                    rs,
                    sample.sx,
                    sample.sy,
                    residual_limit,
                    &state.max_residual)) {
                remember_sample(sample);
            }
        }
    }
}
} // namespace
} // namespace optiland
