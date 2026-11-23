#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/model_builder.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../diagnostics/model_shadow.cuh"
#include "../host/certificate_context.cuh"

namespace optiland {
namespace {
template <
    int kSafeguardedRecoveryMode = 0,
    int kEdgeCount = 4,
    bool kRepairBuild = false>
__global__ void build_calibrated_field_models_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int center_field_index,
    int field_index,
    int wave_index,
    DeviceCalibratedModel* models,
    int skip_valid_models) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size || wave_index < 0 ||
        wave_index >= cfg.wavelength_count) {
        return;
    }
    if (g_evaluation_certificate_mode == 2 && g_evaluation_certificate &&
        g_evaluation_certificate[row].field >= 0) return;
    cfg.asphere_deep_profile_row = row;
    cfg.asphere_deep_profile_field_index = field_index;
    cfg.asphere_deep_profile_wave_index = wave_index;
    fields = d_candidate_row_fields(fields, cfg, row);
    if (field_index == center_field_index) {
        return;
    }
    const long long model_index =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
            cfg.wavelength_count +
        wave_index;
    if (skip_valid_models && models[model_index].valid) {
        if constexpr (kSafeguardedRecoveryMode == 1) {
            if (cfg.asphere_feasibility_profile) {
                atomicAdd(
                    &cfg.asphere_feasibility_profile->skipped_valid_models,
                    1ULL);
            }
        }
        return;
    }
    if constexpr (kSafeguardedRecoveryMode == 2) {
        const double recovery_tag =
            models[model_index].edge_anchors[0].correction_x;
        if (!(recovery_tag >= 1.0)) {
            return;
        }
        if (cfg.asphere_feasibility_profile) {
            atomicAdd(
                &cfg.asphere_feasibility_profile->recovery_models_built,
                1ULL);
        }
    }
    const DeviceWave wave = waves[wave_index];
    if (wave.weight <= 0.0) {
        if constexpr (kSafeguardedRecoveryMode == 1) {
            if (cfg.asphere_feasibility_profile) {
                atomicAdd(
                    &cfg.asphere_feasibility_profile->inactive_wave_models,
                    1ULL);
            }
        }
        models[model_index] = DeviceCalibratedModel{};
        return;
    }
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const long long center_model_index =
        (static_cast<long long>(row) * cfg.field_count + center_field_index) *
            cfg.wavelength_count +
        wave_index;
    const DeviceCalibratedCenterFootprint center_footprint =
        d_make_center_footprint(models[center_model_index]);
    const DeviceCalibratedCenterFootprint* center_footprint_ptr =
        center_footprint.valid ? &center_footprint : nullptr;
    const DeviceCalibratedModel* inner_model = nullptr;
    const DeviceField* inner_field = nullptr;
    const DeviceCalibratedModel* previous_model = nullptr;
    const DeviceField* previous_field = nullptr;
    const DeviceCalibratedModel* previous2_model = nullptr;
    const DeviceField* previous2_field = nullptr;
    const DeviceCalibratedModel* same_field_wave_model =
        d_primary_anchored_same_field_wave_model(
            models, model_index, cfg, wave_index);
    if (field_index > 0) {
        const long long previous_index =
            (static_cast<long long>(row) * cfg.field_count + field_index - 1) *
                cfg.wavelength_count +
            wave_index;
        previous_model = models + previous_index;
        previous_field = fields + field_index - 1;
    }
    if (field_index > 1) {
        const long long previous2_index =
            (static_cast<long long>(row) * cfg.field_count + field_index - 2) *
                cfg.wavelength_count +
            wave_index;
        previous2_model = models + previous2_index;
        previous2_field = fields + field_index - 2;
    }
    double best_inner_rho = -1.0;
    const double target_rho = hypot(fields[field_index].hx, fields[field_index].hy);
    for (int candidate = 0; candidate < field_index; ++candidate) {
        if (candidate == field_index ||
            !d_same_field_direction(fields[candidate], fields[field_index])) {
            continue;
        }
        const double candidate_rho = hypot(fields[candidate].hx, fields[candidate].hy);
        if (candidate_rho >= target_rho - 1.0e-9 ||
            candidate_rho <= best_inner_rho) {
            continue;
        }
        const long long candidate_model_index =
            (static_cast<long long>(row) * cfg.field_count + candidate) *
                cfg.wavelength_count +
            wave_index;
        if (!models[candidate_model_index].valid ||
            !models[candidate_model_index].chief_seed.valid) {
            continue;
        }
        best_inner_rho = candidate_rho;
        inner_model = models + candidate_model_index;
        inner_field = fields + candidate;
    }
    d_build_calibrated_model<
        false,
        kEdgeCount,
        kSafeguardedRecoveryMode,
        kRepairBuild,
        false>(
        surfaces,
        cfg,
        fields[field_index],
        wave.value_um,
        models + model_index,
        fields + center_field_index,
        center_footprint_ptr,
        inner_model,
        inner_field,
        field_index,
        previous_model,
        previous_field,
        previous2_model,
        previous2_field,
        same_field_wave_model);
    if constexpr (
        kSafeguardedRecoveryMode == 0 && kEdgeCount == 4) {
        if (cfg.asphere_deep_profile) {
            DeviceCalibratedModel u1_model;
            const DeviceConfig u1_cfg = d_asphere_deep_u1_model_config(cfg);
            d_build_calibrated_model<false, 4, 0>(
                surfaces,
                u1_cfg,
                fields[field_index],
                wave.value_um,
                &u1_model,
                fields + center_field_index,
                center_footprint_ptr,
                inner_model,
                inner_field,
                field_index,
                previous_model,
                previous_field,
                previous2_model,
                previous2_field,
                same_field_wave_model);
            d_record_asphere_deep_model_comparison<false>(
                cfg, models[model_index], u1_model);
            DeviceCalibratedModel hybrid_model;
            const DeviceConfig hybrid_cfg =
                d_asphere_deep_hybrid_model_config(cfg);
            d_build_calibrated_model<false, 4, 0>(
                surfaces,
                hybrid_cfg,
                fields[field_index],
                wave.value_um,
                &hybrid_model,
                fields + center_field_index,
                center_footprint_ptr,
                inner_model,
                inner_field,
                field_index,
                previous_model,
                previous_field,
                previous2_model,
                previous2_field,
                same_field_wave_model);
            d_record_asphere_deep_model_comparison<true>(
                cfg, models[model_index], hybrid_model);
        }
    }
    if constexpr (kSafeguardedRecoveryMode == 1) {
        if (cfg.asphere_feasibility_profile) {
            DeviceAsphereFeasibilityProfile* profile =
                cfg.asphere_feasibility_profile;
            atomicAdd(&profile->mark_models_built, 1ULL);
            if (models[model_index].valid) {
                atomicAdd(&profile->mark_models_valid, 1ULL);
            }
            const double recovery_tag =
                models[model_index].edge_anchors[0].correction_x;
            if (recovery_tag >= 1.0) {
                const unsigned int mask =
                    static_cast<unsigned int>(recovery_tag);
                atomicAdd(&profile->mark_models_tagged, 1ULL);
                atomicAdd(
                    &profile->mark_edge_bits,
                    static_cast<unsigned long long>(__popc(mask)));
            }
        }
    }
    if constexpr (kSafeguardedRecoveryMode == 2) {
        if (cfg.asphere_feasibility_profile && models[model_index].valid) {
            atomicAdd(
                &cfg.asphere_feasibility_profile->recovery_models_valid,
                1ULL);
        }
    }
}
} // namespace
} // namespace optiland
