#pragma once
#include "../prelude.cuh"
#include "../aiming/chief_geometry.cuh"
#include "../aiming/chief_secant_solver.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/model_builder.cuh"
#include "../aiming/seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../geometry/pupil_paraxial.cuh"

namespace optiland {
namespace {
template <bool kRepairBuild>
__global__ void uc5_finish_primary_xplus_models_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int center_field_index,
    int field_index,
    int wave_index,
    DeviceCalibratedModel* models,
    const DeviceUC5CandidateFamilyState* states) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size || field_index < 0 ||
        field_index >= cfg.field_count || wave_index < 0 ||
        wave_index >= cfg.wavelength_count) {
        return;
    }
    cfg.asphere_deep_profile_row = row;
    cfg.asphere_deep_profile_field_index = field_index;
    cfg.asphere_deep_profile_wave_index = wave_index;
    const DeviceUC5CandidateFamilyState state = states[row];
    if (state.skipped_existing_valid || !state.prepared) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    const DeviceField& field = fields[field_index];
    const DeviceWave wave = waves[wave_index];
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const long long model_index =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
            cfg.wavelength_count +
        wave_index;
    DeviceCalibratedModel& model = models[model_index];
    if (!model.chief_seed.valid) {
        return;
    }
    DeviceCalibratedEdgeAnchor anchor = state.anchor;
    double max_residual = state.max_residual;
    const double rs = d_aperture_stop_radius(surfaces, cfg, wave.value_um);
    const double residual_limit = fmax(
        fmax(0.0, cfg.ray_aiming_tolerance),
        fmax(0.0, cfg.calibrated_edge_residual_stop_fraction) *
            d_physical_stop_radius(surfaces, cfg, rs));
    if (!anchor.valid && state.best_sample.valid) {
        const double target_x = (field.fvcx + (1.0 - field.fvdx)) * rs;
        const double target_y = field.fvcy * rs;
        const double target_dr =
            fmax(1.0e-9, fabs((1.0 - field.fvdx) * rs));
        const double seed_dr = fmax(
            1.0e-9,
            hypot(
                state.best_sample.seed.x - model.chief_seed.x,
                state.best_sample.seed.y - model.chief_seed.y));
        const double initial_scale = seed_dr / target_dr;
        DeviceSeed solved;
        double edge_sx = 0.0;
        double edge_sy = 0.0;
        double residual = INFINITY;
        const int counter_cell =
            d_uc5_counter_cell(cfg, wave.value_um, 0, kRepairBuild);
        DeviceAsphereUC5Profile* profile = g_asphere_uc5_profile;
        if (profile) {
            atomicAdd(&profile->secant_calls[counter_cell], 1ULL);
        }
        bool accepted = d_solve_seed_to_stop_secant<false>(
            surfaces,
            cfg,
            field,
            wave.value_um,
            state.best_sample.seed,
            target_x,
            target_y,
            residual_limit,
            cfg.calibrated_edge_iterations,
            initial_scale,
            0.0,
            0.0,
            initial_scale,
            true,
            &solved,
            &edge_sx,
            &edge_sy,
            &residual,
            nullptr,
            &state.best_sample,
            state.previous_sample.valid ? &state.previous_sample : nullptr,
            true,
            2,
            d_uc5_encode_edge_role(counter_cell));
        if (!accepted && state.previous_sample.valid) {
            if (profile) {
                atomicAdd(&profile->secant_calls[counter_cell], 1ULL);
            }
            accepted = d_solve_seed_to_stop_secant<false>(
                surfaces,
                cfg,
                field,
                wave.value_um,
                state.best_sample.seed,
                target_x,
                target_y,
                residual_limit,
                cfg.calibrated_edge_iterations,
                initial_scale,
                0.0,
                0.0,
                initial_scale,
                true,
                &solved,
                &edge_sx,
                &edge_sy,
                &residual,
                nullptr,
                &state.best_sample,
                nullptr,
                true,
                2,
                d_uc5_encode_edge_role(counter_cell));
        }
        if (accepted &&
            d_seed_in_first_surface_bounds(
                surfaces, cfg, wave.value_um, solved)) {
            anchor.seed = solved;
            anchor.valid = 1;
            max_residual = fmax(max_residual, residual);
        }
    }
    model.edge_anchors[0] = anchor;
    model.fit_rms = max_residual;
    if (!anchor.valid || !anchor.seed.valid) {
        DeviceAsphereUC5Profile* profile = g_asphere_uc5_profile;
        const int counter_cell =
            d_uc5_counter_cell(cfg, wave.value_um, 0, kRepairBuild);
        if (profile) {
            atomicAdd(&profile->exhausted_failures[counter_cell], 1ULL);
        }
        // Match the fused-builder dependency rule above: initial primary
        // partial anchors remain available to secondary rescue.
        if (g_asphere_uc5_primary_abort_enabled && kRepairBuild) {
            if (profile) {
                for (int skipped = 1; skipped < 4; ++skipped) {
                    const int skipped_cell = d_uc5_counter_cell(
                        cfg, wave.value_um, skipped, kRepairBuild);
                    atomicAdd(
                        &profile
                             ->anchor_skipped_after_failure[skipped_cell],
                        1ULL);
                }
            }
            model.calibrated_edges_all_valid = 0;
            model.valid = 0;
            return;
        }
    }

    const DeviceSeed chief_seed = model.chief_seed;
    const double chief_stop_x = model.chief_stop_x;
    const double chief_stop_y = model.chief_stop_y;
    const double precomputed_residual = model.fit_rms;
    const DeviceCalibratedModel* same_field_wave_model =
        d_primary_anchored_same_field_wave_model(
            models, model_index, cfg, wave_index);
    const DeviceCalibratedCenterFootprint center_footprint =
        field_index == center_field_index
        ? DeviceCalibratedCenterFootprint{}
        : d_make_center_footprint(
              models[(static_cast<long long>(row) * cfg.field_count +
                      center_field_index) *
                         cfg.wavelength_count +
                     wave_index]);
    const DeviceCalibratedCenterFootprint* center_footprint_ptr =
        center_footprint.valid ? &center_footprint : nullptr;
    const DeviceCalibratedModel* inner_model = nullptr;
    const DeviceField* inner_field = nullptr;
    const DeviceCalibratedModel* previous_model = nullptr;
    const DeviceField* previous_field = nullptr;
    const DeviceCalibratedModel* previous2_model = nullptr;
    const DeviceField* previous2_field = nullptr;
    if (field_index != center_field_index) {
        if (field_index > 0) {
            previous_model = models +
                (static_cast<long long>(row) * cfg.field_count +
                 field_index - 1) *
                    cfg.wavelength_count +
                wave_index;
            previous_field = fields + field_index - 1;
        }
        if (field_index > 1) {
            previous2_model = models +
                (static_cast<long long>(row) * cfg.field_count +
                 field_index - 2) *
                    cfg.wavelength_count +
                wave_index;
            previous2_field = fields + field_index - 2;
        }
        double best_inner_rho = -1.0;
        const double target_rho = hypot(field.hx, field.hy);
        for (int candidate = 0; candidate < field_index; ++candidate) {
            if (!d_same_field_direction(fields[candidate], field)) {
                continue;
            }
            const double candidate_rho =
                hypot(fields[candidate].hx, fields[candidate].hy);
            if (candidate_rho >= target_rho - 1.0e-9 ||
                candidate_rho <= best_inner_rho) {
                continue;
            }
            const DeviceCalibratedModel* candidate_model = models +
                (static_cast<long long>(row) * cfg.field_count + candidate) *
                    cfg.wavelength_count +
                wave_index;
            if (!candidate_model->valid || !candidate_model->chief_seed.valid) {
                continue;
            }
            best_inner_rho = candidate_rho;
            inner_model = candidate_model;
            inner_field = fields + candidate;
        }
    }
    d_build_calibrated_model<true, 4, 0, kRepairBuild, true>(
        surfaces,
        cfg,
        field,
        wave.value_um,
        &model,
        fields + center_field_index,
        center_footprint_ptr,
        inner_model,
        inner_field,
        field_index,
        previous_model,
        previous_field,
        previous2_model,
        previous2_field,
        same_field_wave_model,
        &chief_seed,
        chief_stop_x,
        chief_stop_y,
        precomputed_residual,
        0);
}
} // namespace
} // namespace optiland
