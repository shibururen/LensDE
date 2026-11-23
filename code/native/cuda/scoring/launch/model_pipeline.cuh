#pragma once
#include "../prelude.cuh"
#include "../configuration/runtime_options.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../host/certificate_context.cuh"
#include "../kernels/center_model_build.cuh"
#include "../kernels/completed_field_certificate.cuh"
#include "../kernels/field_model_build.cuh"
#include "../kernels/primary_edge_candidates.cuh"
#include "../kernels/primary_model_finish.cuh"
#include "ordered_model_pipeline.cuh"

namespace optiland {
namespace {
bool launch_calibrated_model_build(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceConfig& cfg,
    int population_size,
    int center_field_index,
    DeviceCalibratedModel* models,
    DeviceUC5CandidateFamilyState* uc5_states,
    int block_size,
    bool enable_safeguarded_recovery,
    bool enable_uc5_candidate_batch,
    AsphereFeasibilityHostTiming* feasibility_timing) {
    auto evaluation_finish_field = [&](int field_index) {
        if (!evaluation_device_mode) return true;
        evaluation_certify_completed_field_kernel<<<(population_size + 127) / 128, 128>>>(
            fields, waves, models, cfg, population_size, field_index, evaluation_context_epsilon);
        return cudaGetLastError() == cudaSuccess;
    };
    auto launch_fused = [&]() {
        const int model_grid = (population_size + block_size - 1) / block_size;
        const int primary_wave_index =
            cfg.primary_wave_index >= 0 &&
                    cfg.primary_wave_index < cfg.wavelength_count
                ? cfg.primary_wave_index
                : 0;
        auto launch_uc5_primary_build = [&](
            int field_index,
            int wave_index,
            bool repair_build,
            bool skip_valid_models) -> cudaError_t {
            if (!uc5_states) {
                return cudaErrorInvalidDevicePointer;
            }
            if (field_index == center_field_index) {
                if (repair_build) {
                    build_calibrated_center_models_kernel<0, 0, true>
                        <<<model_grid, block_size>>>(
                            candidate_surfaces,
                            fields,
                            waves,
                            cfg,
                            population_size,
                            center_field_index,
                            wave_index,
                            models,
                            skip_valid_models ? 1 : 0);
                } else {
                    build_calibrated_center_models_kernel<0, 0, false>
                        <<<model_grid, block_size>>>(
                            candidate_surfaces,
                            fields,
                            waves,
                            cfg,
                            population_size,
                            center_field_index,
                            wave_index,
                            models,
                            skip_valid_models ? 1 : 0);
                }
            } else if (repair_build) {
                build_calibrated_field_models_kernel<0, 0, true>
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        field_index,
                        wave_index,
                        models,
                        skip_valid_models ? 1 : 0);
            } else {
                build_calibrated_field_models_kernel<0, 0, false>
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        field_index,
                        wave_index,
                        models,
                        skip_valid_models ? 1 : 0);
            }
            cudaError_t status = cudaGetLastError();
            if (status != cudaSuccess) {
                return status;
            }
            constexpr int kBatchBlock = 128;
            constexpr int kGroupSize = 8;
            const int batch_threads = population_size * kGroupSize;
            const int batch_grid =
                (batch_threads + kBatchBlock - 1) / kBatchBlock;
            if (repair_build) {
                uc5_batch_primary_xplus_candidates_kernel<true>
                    <<<batch_grid, kBatchBlock>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        field_index,
                        wave_index,
                        models,
                        uc5_states,
                        skip_valid_models ? 1 : 0);
            } else {
                uc5_batch_primary_xplus_candidates_kernel<false>
                    <<<batch_grid, kBatchBlock>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        field_index,
                        wave_index,
                        models,
                        uc5_states,
                        skip_valid_models ? 1 : 0);
            }
            status = cudaGetLastError();
            if (status != cudaSuccess) {
                return status;
            }
            if (repair_build) {
                uc5_finish_primary_xplus_models_kernel<true>
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        field_index,
                        wave_index,
                        models,
                        uc5_states);
            } else {
                uc5_finish_primary_xplus_models_kernel<false>
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        field_index,
                        wave_index,
                        models,
                        uc5_states);
            }
            return cudaGetLastError();
        };
        auto build_center_wave = [&](int wave_index, bool skip_valid_models = false) {
            cudaEvent_t mark_start = nullptr;
            cudaEvent_t mark_stop = nullptr;
            if (!begin_diagnostic_cuda_timer(
                    feasibility_timing != nullptr,
                    &mark_start,
                    &mark_stop)) {
                return false;
            }
            const bool uc5_batched_primary =
                enable_uc5_candidate_batch && !enable_safeguarded_recovery &&
                wave_index == primary_wave_index;
            cudaError_t mark_launch_status = cudaSuccess;
            if (uc5_batched_primary) {
                mark_launch_status = launch_uc5_primary_build(
                    center_field_index,
                    wave_index,
                    skip_valid_models,
                    skip_valid_models);
            } else if (enable_safeguarded_recovery) {
                build_calibrated_center_models_kernel<1>
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        wave_index,
                        models,
                        skip_valid_models ? 1 : 0);
            } else if (skip_valid_models) {
                build_calibrated_center_models_kernel<0, 4, true>
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        wave_index,
                        models,
                        1);
            } else {
                build_calibrated_center_models_kernel<0, 4, false>
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        wave_index,
                        models,
                        0);
            }
            if (!uc5_batched_primary) {
                mark_launch_status = cudaGetLastError();
            }
            if (!finish_diagnostic_cuda_timer(
                    feasibility_timing != nullptr,
                    mark_start,
                    mark_stop,
                    feasibility_timing
                        ? &feasibility_timing->calibrated_mark_seconds
                        : nullptr)) {
                return false;
            }
            if (feasibility_timing) {
                feasibility_timing->calibrated_mark_kernel_launches +=
                    uc5_batched_primary ? 3 : 1;
            }
            if (mark_launch_status != cudaSuccess) {
                return false;
            }
            if (enable_safeguarded_recovery) {
                cudaEvent_t recovery_start = nullptr;
                cudaEvent_t recovery_stop = nullptr;
                if (!begin_diagnostic_cuda_timer(
                        feasibility_timing != nullptr,
                        &recovery_start,
                        &recovery_stop)) {
                    return false;
                }
                build_calibrated_center_models_kernel<2>
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        wave_index,
                        models,
                        0);
                const cudaError_t recovery_launch_status = cudaGetLastError();
                if (!finish_diagnostic_cuda_timer(
                        feasibility_timing != nullptr,
                        recovery_start,
                        recovery_stop,
                        feasibility_timing
                            ? &feasibility_timing->calibrated_recovery_seconds
                            : nullptr)) {
                    return false;
                }
                if (feasibility_timing) {
                    ++feasibility_timing->calibrated_recovery_kernel_launches;
                }
                if (recovery_launch_status != cudaSuccess) {
                    return false;
                }
            }
            return true;
        };
        if (cfg.wavelength_count > 0 &&
            !build_center_wave(primary_wave_index)) {
            return false;
        }
        for (int wave_index = 0; wave_index < cfg.wavelength_count; ++wave_index) {
            if (wave_index != primary_wave_index &&
                !build_center_wave(wave_index)) {
                return false;
            }
        }
        if (cfg.wavelength_count > 1 &&
            !build_center_wave(primary_wave_index, true)) {
            return false;
        }
        if (!evaluation_finish_field(center_field_index)) return false;
        for (int field_index = 0; field_index < cfg.field_count; ++field_index) {
            if (field_index == center_field_index) {
                continue;
            }
            auto build_field_wave = [&](int wave_index, bool skip_valid_models = false) {
                cudaEvent_t mark_start = nullptr;
                cudaEvent_t mark_stop = nullptr;
                if (!begin_diagnostic_cuda_timer(
                        feasibility_timing != nullptr,
                        &mark_start,
                        &mark_stop)) {
                    return false;
                }
                const bool uc5_batched_primary =
                    enable_uc5_candidate_batch &&
                    !enable_safeguarded_recovery &&
                    wave_index == primary_wave_index;
                cudaError_t mark_launch_status = cudaSuccess;
                if (uc5_batched_primary) {
                    mark_launch_status = launch_uc5_primary_build(
                        field_index,
                        wave_index,
                        skip_valid_models,
                        skip_valid_models);
                } else if (enable_safeguarded_recovery) {
                    build_calibrated_field_models_kernel<1>
                        <<<model_grid, block_size>>>(
                            candidate_surfaces,
                            fields,
                            waves,
                            cfg,
                            population_size,
                            center_field_index,
                            field_index,
                            wave_index,
                            models,
                            skip_valid_models ? 1 : 0);
                } else if (skip_valid_models) {
                    build_calibrated_field_models_kernel<0, 4, true>
                        <<<model_grid, block_size>>>(
                            candidate_surfaces,
                            fields,
                            waves,
                            cfg,
                            population_size,
                            center_field_index,
                            field_index,
                            wave_index,
                            models,
                            1);
                } else {
                    build_calibrated_field_models_kernel<0, 4, false>
                        <<<model_grid, block_size>>>(
                            candidate_surfaces,
                            fields,
                            waves,
                            cfg,
                            population_size,
                            center_field_index,
                            field_index,
                            wave_index,
                            models,
                            0);
                }
                if (!uc5_batched_primary) {
                    mark_launch_status = cudaGetLastError();
                }
                if (!finish_diagnostic_cuda_timer(
                        feasibility_timing != nullptr,
                        mark_start,
                        mark_stop,
                        feasibility_timing
                            ? &feasibility_timing->calibrated_mark_seconds
                            : nullptr)) {
                    return false;
                }
                if (feasibility_timing) {
                    feasibility_timing->calibrated_mark_kernel_launches +=
                        uc5_batched_primary ? 3 : 1;
                }
                if (mark_launch_status != cudaSuccess) {
                    return false;
                }
                if (enable_safeguarded_recovery) {
                    cudaEvent_t recovery_start = nullptr;
                    cudaEvent_t recovery_stop = nullptr;
                    if (!begin_diagnostic_cuda_timer(
                            feasibility_timing != nullptr,
                            &recovery_start,
                            &recovery_stop)) {
                        return false;
                    }
                    build_calibrated_field_models_kernel<2>
                        <<<model_grid, block_size>>>(
                            candidate_surfaces,
                            fields,
                            waves,
                            cfg,
                            population_size,
                            center_field_index,
                            field_index,
                            wave_index,
                            models,
                            0);
                    const cudaError_t recovery_launch_status = cudaGetLastError();
                    if (!finish_diagnostic_cuda_timer(
                            feasibility_timing != nullptr,
                            recovery_start,
                            recovery_stop,
                            feasibility_timing
                                ? &feasibility_timing->calibrated_recovery_seconds
                                : nullptr)) {
                        return false;
                    }
                    if (feasibility_timing) {
                        ++feasibility_timing->calibrated_recovery_kernel_launches;
                    }
                    if (recovery_launch_status != cudaSuccess) {
                        return false;
                    }
                }
                return true;
            };
            if (cfg.wavelength_count > 0 &&
                !build_field_wave(primary_wave_index)) {
                return false;
            }
            for (int wave_index = 0; wave_index < cfg.wavelength_count; ++wave_index) {
                if (wave_index != primary_wave_index &&
                    !build_field_wave(wave_index)) {
                    return false;
                }
            }
            if (cfg.wavelength_count > 1 &&
                !build_field_wave(primary_wave_index, true)) {
                return false;
            }
            if (!evaluation_finish_field(field_index)) return false;
        }
        return true;
    };

    auto repair_invalid_primary_models = [&]() {
        if (cfg.wavelength_count <= 1) {
            return true;
        }
        const int primary_wave_index =
            cfg.primary_wave_index >= 0 &&
                    cfg.primary_wave_index < cfg.wavelength_count
                ? cfg.primary_wave_index
                : 0;
        const int model_grid =
            (population_size + block_size - 1) / block_size;
        build_calibrated_center_models_kernel<0, 4, true>
            <<<model_grid, block_size>>>(
                candidate_surfaces,
                fields,
                waves,
                cfg,
                population_size,
                center_field_index,
                primary_wave_index,
                models,
                1);
        if (cudaGetLastError() != cudaSuccess) {
            return false;
        }
        for (int field_index = 0; field_index < cfg.field_count; ++field_index) {
            if (field_index == center_field_index) {
                continue;
            }
            build_calibrated_field_models_kernel<0, 4, true>
                <<<model_grid, block_size>>>(
                    candidate_surfaces,
                    fields,
                    waves,
                    cfg,
                    population_size,
                    center_field_index,
                    field_index,
                    primary_wave_index,
                    models,
                    1);
            if (cudaGetLastError() != cudaSuccess) {
                return false;
            }
        }
        return true;
    };

    auto launch_ordered_with_primary_repair = [&]() {
        const bool built = launch_calibrated_model_build_ordered_tile8(
            candidate_surfaces,
            fields,
            waves,
            cfg,
            population_size,
            center_field_index,
            models,
            block_size,
            nullptr);
        return built && repair_invalid_primary_models();
    };

    if (enable_safeguarded_recovery) {
        // Recovery depends on immediately preceding center/field/wave models;
        // keep mark and recovery kernels interleaved in the fused route.
        return launch_fused();
    }

    const char* ordered_override =
        std::getenv("OPTILAND_DE_CUDA_CALIBRATED_ORDERED_TILE8");
    if (ordered_override) {
        if (!env_flag_enabled("OPTILAND_DE_CUDA_CALIBRATED_ORDERED_TILE8")) {
            return launch_fused();
        }
        return launch_ordered_with_primary_repair();
    }

    // The production asphere route was validated with the fused initial-model
    // builder. Keep an explicit environment override for audit/ablation runs.
    if (asphere_root_policy() ==
            AsphereRootPolicy::SlopeDomainTopologySeed ||
        asphere_root_policy() ==
            AsphereRootPolicy::UnifiedCertifiedExperimental) {
        return launch_fused();
    }

    if (!env_flag_enabled_default(
            "OPTILAND_DE_CUDA_CALIBRATED_ADAPTIVE_EASY_PATH",
            true)) {
        return launch_ordered_with_primary_repair();
    }

    struct AdaptiveEasyPathState {
        unsigned long long signature = 0;
        int fused_calls_since_probe = 0;
        bool use_fused = false;
    };
    static thread_local AdaptiveEasyPathState route;
    const unsigned long long signature =
        (static_cast<unsigned long long>(cfg.surface_count) << 48) ^
        (static_cast<unsigned long long>(cfg.field_count) << 40) ^
        (static_cast<unsigned long long>(cfg.wavelength_count) << 32) ^
        (static_cast<unsigned long long>(cfg.calibrated_budget_iterations) << 24) ^
        (static_cast<unsigned long long>(cfg.calibrated_edge_iterations) << 16);
    if (route.signature != signature) {
        route = AdaptiveEasyPathState{};
        route.signature = signature;
    }
    int probe_interval = 32;
    if (const char* value = std::getenv(
            "OPTILAND_DE_CUDA_CALIBRATED_EASY_PATH_PROBE_INTERVAL")) {
        probe_interval = std::clamp(std::atoi(value), 1, 10000);
    }
    if (route.use_fused && route.fused_calls_since_probe < probe_interval) {
        ++route.fused_calls_since_probe;
        return launch_fused();
    }

    DeviceOrderedChiefStats observed_stats{};
    const bool ok = launch_calibrated_model_build_ordered_tile8(
        candidate_surfaces,
        fields,
        waves,
        cfg,
        population_size,
        center_field_index,
        models,
        block_size,
        &observed_stats) &&
        repair_invalid_primary_models();
    if (!ok) {
        return false;
    }
    double max_trial_fraction = 0.01;
    if (const char* value = std::getenv(
            "OPTILAND_DE_CUDA_CALIBRATED_EASY_PATH_MAX_TRIAL_FRACTION")) {
        const double parsed = std::atof(value);
        if (std::isfinite(parsed)) {
            max_trial_fraction = std::clamp(parsed, 0.0, 1.0);
        }
    }
    const double trial_fraction =
        observed_stats.prepared > 0
            ? static_cast<double>(observed_stats.trial_tasks) /
                  static_cast<double>(observed_stats.prepared)
            : 1.0;
    route.use_fused = trial_fraction <= max_trial_fraction;
    route.fused_calls_since_probe = 0;
    return true;
}
} // namespace
} // namespace optiland
