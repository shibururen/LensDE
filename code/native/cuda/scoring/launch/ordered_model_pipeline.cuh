#pragma once
#include "../prelude.cuh"
#include "../configuration/runtime_options.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../kernels/cooperative_bridge_prepare.cuh"
#include "../kernels/cooperative_bridge_solve.cuh"
#include "../kernels/ordered_chief_prepare.cuh"
#include "../kernels/ordered_chief_solve.cuh"
#include "../kernels/ordered_model_finalize.cuh"

namespace optiland {
namespace {
bool launch_calibrated_model_build_ordered_tile8(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceConfig& cfg,
    int population_size,
    int center_field_index,
    DeviceCalibratedModel* models,
    int block_size,
    DeviceOrderedChiefStats* observed_stats) {
    constexpr int kMaxTileSize = 32;
    int tile_size = 32;
    if (const char* value =
            std::getenv("OPTILAND_DE_CUDA_CALIBRATED_ORDERED_TILE_SIZE")) {
        tile_size = std::clamp(std::atoi(value), 1, kMaxTileSize);
    }
    int tiled_trial_limit = 32;
    if (const char* value = std::getenv(
            "OPTILAND_DE_CUDA_CALIBRATED_ORDERED_TRIAL_LIMIT")) {
        tiled_trial_limit = std::clamp(
            std::atoi(value),
            tile_size,
            kBudgetChiefSeedCapacity);
    }
    tiled_trial_limit =
        ((tiled_trial_limit + tile_size - 1) / tile_size) * tile_size;
    const int processed_trial_count = std::min(
        kBudgetChiefSeedCapacity,
        tiled_trial_limit <= 1
            ? 1
            : 1 +
                  ((tiled_trial_limit - 1 + tile_size - 1) / tile_size) *
                      tile_size);
    const bool compact_seed0 = env_flag_enabled(
        "OPTILAND_DE_CUDA_CALIBRATED_COMPACT_SEED0");
    const bool resume_serial_tail = env_flag_enabled_default(
        "OPTILAND_DE_CUDA_CALIBRATED_RESUME_SERIAL_TAIL",
        true);
    const bool phase_timing_enabled = env_flag_enabled(
        "OPTILAND_DE_CUDA_CALIBRATED_ORDERED_PHASE_TIMING");
    const bool stats_enabled = env_flag_enabled(
        "OPTILAND_DE_CUDA_CALIBRATED_ORDERED_STATS");
    const bool split_fallback = env_flag_enabled(
        "OPTILAND_DE_CUDA_CALIBRATED_SPLIT_FALLBACK");
    const bool cooperative_bridge =
        env_flag_enabled_default(
            "OPTILAND_DE_CUDA_CALIBRATED_COOPERATIVE_BRIDGE",
            true) &&
        resume_serial_tail && cfg.calibrated_budget_reuse_solve_context;
    enum OrderedChiefPhase {
        kOrderedPrepare = 0,
        kOrderedSeed0Solve,
        kOrderedSeed0Reduce,
        kOrderedCompactPrepare,
        kOrderedTileSolve,
        kOrderedTileReduce,
        kOrderedCooperativeBridge,
        kOrderedFinalizeSolved,
        kOrderedFinalizeFallback,
        kOrderedFallbackChief,
        kOrderedFallbackEdges,
        kOrderedPhaseCount,
    };
    struct OrderedChiefPhaseEvent {
        cudaEvent_t begin = nullptr;
        cudaEvent_t end = nullptr;
        int phase = 0;
    };
    std::vector<OrderedChiefPhaseEvent> phase_events;
    auto begin_phase = [&](int phase) -> int {
        if (!phase_timing_enabled) {
            return -1;
        }
        OrderedChiefPhaseEvent sample;
        sample.phase = phase;
        if (cudaEventCreate(&sample.begin) != cudaSuccess ||
            cudaEventCreate(&sample.end) != cudaSuccess ||
            cudaEventRecord(sample.begin) != cudaSuccess) {
            if (sample.begin) {
                cudaEventDestroy(sample.begin);
            }
            if (sample.end) {
                cudaEventDestroy(sample.end);
            }
            return -1;
        }
        phase_events.push_back(sample);
        return static_cast<int>(phase_events.size()) - 1;
    };
    auto end_phase = [&](int sample_index) {
        if (sample_index >= 0) {
            cudaEventRecord(
                phase_events[static_cast<std::size_t>(sample_index)].end);
        }
    };
    DeviceOrderedChiefTask* tasks = nullptr;
    DeviceOrderedChiefTrialResult* results = nullptr;
    DeviceCooperativeBridgeTask* bridge_tasks = nullptr;
    DeviceOrderedChiefStats* stats = nullptr;
    const std::size_t task_bytes =
        static_cast<std::size_t>(population_size) *
        sizeof(DeviceOrderedChiefTask);
    const std::size_t result_bytes =
        static_cast<std::size_t>(population_size) * kMaxTileSize *
        sizeof(DeviceOrderedChiefTrialResult);
    bool ok = cudaMalloc(&tasks, task_bytes) == cudaSuccess &&
              cudaMalloc(&results, result_bytes) == cudaSuccess &&
              cudaMalloc(&stats, sizeof(DeviceOrderedChiefStats)) ==
                  cudaSuccess;
    if (ok) {
        ok = cudaMemset(stats, 0, sizeof(DeviceOrderedChiefStats)) ==
             cudaSuccess;
    }
    if (ok && cooperative_bridge) {
        ok = cudaMalloc(
                 &bridge_tasks,
                 static_cast<std::size_t>(population_size) *
                     sizeof(DeviceCooperativeBridgeTask)) == cudaSuccess;
    }
    const int model_grid = (population_size + block_size - 1) / block_size;
    int solve_block = 32;
    if (const char* value = std::getenv(
            "OPTILAND_DE_CUDA_CALIBRATED_ORDERED_SOLVE_BLOCK_SIZE")) {
        solve_block = std::clamp(std::atoi(value), 8, 128);
    }
    const int solve_count = population_size * tile_size;
    const int solve_grid = (solve_count + solve_block - 1) / solve_block;
    const int cooperative_solve_count = population_size * kMaxTileSize;
    const int cooperative_solve_grid =
        (cooperative_solve_count + solve_block - 1) / solve_block;
    const int first_seed_grid =
        (population_size + solve_block - 1) / solve_block;
    auto build_one = [&](int field_index, int wave_index) {
        if (!ok) {
            return;
        }
            const int prepare_phase = begin_phase(kOrderedPrepare);
            prepare_ordered_chief_tasks_kernel<<<model_grid, block_size>>>(
                candidate_surfaces,
                fields,
                waves,
                cfg,
                population_size,
                center_field_index,
                field_index,
                wave_index,
                models,
                compact_seed0 ? 1 : kBudgetChiefSeedCapacity,
                false,
                tasks,
                stats);
            ok = cudaGetLastError() == cudaSuccess;
            end_phase(prepare_phase);
            if (ok) {
                const int solve_phase = begin_phase(kOrderedSeed0Solve);
                solve_ordered_chief_tile_kernel<true>
                    <<<first_seed_grid, solve_block>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        field_index,
                        wave_index,
                        0,
                        1,
                        tasks,
                        results);
                ok = cudaGetLastError() == cudaSuccess;
                end_phase(solve_phase);
            }
            if (ok) {
                const int reduce_phase = begin_phase(kOrderedSeed0Reduce);
                reduce_ordered_chief_tile_kernel
                    <<<model_grid, block_size>>>(
                        population_size,
                        0,
                        1,
                        tasks,
                        results,
                        stats);
                ok = cudaGetLastError() == cudaSuccess;
                end_phase(reduce_phase);
            }
            if (ok && compact_seed0) {
                const int compact_phase = begin_phase(kOrderedCompactPrepare);
                prepare_ordered_chief_tasks_kernel
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
                        kBudgetChiefSeedCapacity,
                        true,
                        tasks,
                        nullptr);
                ok = cudaGetLastError() == cudaSuccess;
                end_phase(compact_phase);
            }
        for (int tile_begin = 1;
             ok && tile_begin < tiled_trial_limit;
             tile_begin += tile_size) {
            const int solve_phase = begin_phase(kOrderedTileSolve);
            solve_ordered_chief_tile_kernel<true>
                <<<solve_grid, solve_block>>>(
                    candidate_surfaces,
                    fields,
                    waves,
                    cfg,
                    population_size,
                    field_index,
                    wave_index,
                    tile_begin,
                    tile_size,
                    tasks,
                    results);
            ok = cudaGetLastError() == cudaSuccess;
            end_phase(solve_phase);
            if (ok) {
                const int reduce_phase = begin_phase(kOrderedTileReduce);
                reduce_ordered_chief_tile_kernel
                    <<<model_grid, block_size>>>(
                        population_size,
                        tile_begin,
                        tile_size,
                        tasks,
                        results,
                        stats);
                ok = cudaGetLastError() == cudaSuccess;
                end_phase(reduce_phase);
            }
        }
        if (ok && cooperative_bridge) {
            const int cooperative_phase =
                begin_phase(kOrderedCooperativeBridge);
            initialize_cooperative_bridge_tasks_kernel
                <<<model_grid, block_size>>>(
                    candidate_surfaces,
                    fields,
                    waves,
                    cfg,
                    population_size,
                    center_field_index,
                    field_index,
                    wave_index,
                    processed_trial_count,
                    models,
                    tasks,
                    bridge_tasks,
                    stats);
            ok = cudaGetLastError() == cudaSuccess;
            for (int bridge_stage = 0;
                 ok && bridge_stage < 3;
                 ++bridge_stage) {
                prepare_cooperative_bridge_stage_kernel
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        center_field_index,
                        field_index,
                        wave_index,
                        bridge_tasks,
                        stats);
                ok = cudaGetLastError() == cudaSuccess;
                for (int tile_begin : {1, 33}) {
                    if (!ok) {
                        break;
                    }
                    solve_cooperative_bridge_tile_kernel
                        <<<cooperative_solve_grid, solve_block>>>(
                            candidate_surfaces,
                            waves,
                            cfg,
                            population_size,
                            wave_index,
                            tile_begin,
                            kMaxTileSize,
                            bridge_tasks,
                            results);
                    ok = cudaGetLastError() == cudaSuccess;
                    if (ok) {
                        reduce_cooperative_bridge_tile_kernel
                            <<<model_grid, block_size>>>(
                                population_size,
                                tile_begin,
                                kMaxTileSize,
                                tasks,
                                bridge_tasks,
                                results,
                                stats);
                        ok = cudaGetLastError() == cudaSuccess;
                    }
                }
            }
            if (ok) {
                prepare_cooperative_target_stage_kernel
                    <<<model_grid, block_size>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        cfg,
                        population_size,
                        field_index,
                        wave_index,
                        tasks,
                        bridge_tasks,
                        stats);
                ok = cudaGetLastError() == cudaSuccess;
            }
            for (int tile_begin : {1, 33}) {
                if (!ok) {
                    break;
                }
                solve_cooperative_bridge_tile_kernel
                    <<<cooperative_solve_grid, solve_block>>>(
                        candidate_surfaces,
                        waves,
                        cfg,
                        population_size,
                        wave_index,
                        tile_begin,
                        kMaxTileSize,
                        bridge_tasks,
                        results);
                ok = cudaGetLastError() == cudaSuccess;
                if (ok) {
                    reduce_cooperative_bridge_tile_kernel
                        <<<model_grid, block_size>>>(
                            population_size,
                            tile_begin,
                            kMaxTileSize,
                            tasks,
                            bridge_tasks,
                            results,
                            stats);
                    ok = cudaGetLastError() == cudaSuccess;
                }
            }
            if (ok) {
                publish_cooperative_bridge_exhaustion_kernel
                    <<<model_grid, block_size>>>(
                        population_size,
                        tasks,
                        bridge_tasks);
                ok = cudaGetLastError() == cudaSuccess;
            }
            end_phase(cooperative_phase);
        }
        if (ok) {
            const int finalize_phase = begin_phase(kOrderedFinalizeSolved);
            finalize_ordered_calibrated_models_kernel<false>
                <<<model_grid, block_size>>>(
                    candidate_surfaces,
                    fields,
                    waves,
                    cfg,
                    population_size,
                    center_field_index,
                    field_index,
                    wave_index,
                    processed_trial_count,
                    resume_serial_tail,
                    false,
                    models,
                    tasks,
                    stats);
            ok = cudaGetLastError() == cudaSuccess;
            end_phase(finalize_phase);
        }
        if (ok && !split_fallback) {
            const int fallback_phase = begin_phase(kOrderedFinalizeFallback);
            finalize_ordered_calibrated_models_kernel<true, false>
                <<<model_grid, block_size>>>(
                    candidate_surfaces,
                    fields,
                    waves,
                    cfg,
                    population_size,
                    center_field_index,
                    field_index,
                    wave_index,
                    processed_trial_count,
                    resume_serial_tail,
                    false,
                    models,
                    tasks,
                    stats);
            ok = cudaGetLastError() == cudaSuccess;
            end_phase(fallback_phase);
        }
        if (ok && split_fallback) {
            const int chief_phase = begin_phase(kOrderedFallbackChief);
            finalize_ordered_calibrated_models_kernel<true, true>
                <<<model_grid, block_size>>>(
                    candidate_surfaces,
                    fields,
                    waves,
                    cfg,
                    population_size,
                    center_field_index,
                    field_index,
                    wave_index,
                    processed_trial_count,
                    resume_serial_tail,
                    false,
                    models,
                    tasks,
                    stats);
            ok = cudaGetLastError() == cudaSuccess;
            end_phase(chief_phase);
        }
        if (ok && split_fallback) {
            const int edges_phase = begin_phase(kOrderedFallbackEdges);
            finalize_ordered_calibrated_models_kernel<false>
                <<<model_grid, block_size>>>(
                    candidate_surfaces,
                    fields,
                    waves,
                    cfg,
                    population_size,
                    center_field_index,
                    field_index,
                    wave_index,
                    processed_trial_count,
                    resume_serial_tail,
                    true,
                    models,
                    tasks,
                    stats);
            ok = cudaGetLastError() == cudaSuccess;
            end_phase(edges_phase);
        }
    };
    const int primary_wave_index =
        cfg.primary_wave_index >= 0 &&
                cfg.primary_wave_index < cfg.wavelength_count
            ? cfg.primary_wave_index
            : 0;
    if (ok && cfg.wavelength_count > 0) {
        build_one(center_field_index, primary_wave_index);
    }
    for (int wave_index = 0;
         ok && wave_index < cfg.wavelength_count;
         ++wave_index) {
        if (wave_index != primary_wave_index) {
            build_one(center_field_index, wave_index);
        }
    }
    for (int field_index = 0;
         ok && field_index < cfg.field_count;
         ++field_index) {
        if (field_index == center_field_index) {
            continue;
        }
        if (ok && cfg.wavelength_count > 0) {
            build_one(field_index, primary_wave_index);
        }
        for (int wave_index = 0;
             ok && wave_index < cfg.wavelength_count;
             ++wave_index) {
            if (wave_index != primary_wave_index) {
                build_one(field_index, wave_index);
            }
        }
    }
    DeviceOrderedChiefStats host_stats{};
    if (ok && stats) {
        ok = cudaMemcpy(
                 &host_stats,
                 stats,
                 sizeof(host_stats),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }
    if (ok && observed_stats) {
        *observed_stats = host_stats;
    }
    if (ok && stats_enabled) {
        std::fprintf(
            stderr,
            "DE-CUDA ordered chief stats: tile=%d limit=%d compact_seed0=%d "
            "resume_tail=%d split_fallback=%d cooperative_bridge=%d "
            "prepared=%llu "
            "paraxial=%llu trial_tasks=%llu trial_seed_sum=%llu "
            "solved=[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu] "
            "serial_fallback=%llu fallback_solved=%llu "
            "fallback_failed=%llu\n",
            tile_size,
            tiled_trial_limit,
            compact_seed0 ? 1 : 0,
            resume_serial_tail ? 1 : 0,
            split_fallback ? 1 : 0,
            cooperative_bridge ? 1 : 0,
            host_stats.prepared,
            host_stats.paraxial_accepted,
            host_stats.trial_tasks,
            host_stats.trial_seed_sum,
            host_stats.solved_buckets[0],
            host_stats.solved_buckets[1],
            host_stats.solved_buckets[2],
            host_stats.solved_buckets[3],
            host_stats.solved_buckets[4],
            host_stats.solved_buckets[5],
            host_stats.solved_buckets[6],
            host_stats.solved_buckets[7],
            host_stats.serial_fallback,
            host_stats.serial_fallback_solved,
            host_stats.serial_fallback_failed);
        if (cooperative_bridge) {
            std::fprintf(
                stderr,
                "DE-CUDA cooperative bridge stats: eligible=%llu "
                "bridge_stages=%llu bridge_seed0_solved=%llu "
                "bridge_tile_solved=%llu target_stages=%llu "
                "target_seed0_solved=%llu target_tile_solved=%llu "
                "failed=%llu\n",
                host_stats.cooperative_eligible,
                host_stats.cooperative_bridge_stages,
                host_stats.cooperative_bridge_seed0_solved,
                host_stats.cooperative_bridge_tile_solved,
                host_stats.cooperative_target_stages,
                host_stats.cooperative_target_seed0_solved,
                host_stats.cooperative_target_tile_solved,
                host_stats.cooperative_failed);
        }
    }
    if (ok && phase_timing_enabled) {
        double phase_ms[kOrderedPhaseCount] = {};
        for (const OrderedChiefPhaseEvent& sample : phase_events) {
            float sample_ms = 0.0f;
            if (cudaEventElapsedTime(
                    &sample_ms, sample.begin, sample.end) == cudaSuccess) {
                phase_ms[sample.phase] += static_cast<double>(sample_ms);
            }
        }
        std::fprintf(
            stderr,
            "DE-CUDA ordered chief phase timing: prepare=%.6f ms "
            "seed0_solve=%.6f ms seed0_reduce=%.6f ms "
            "compact_prepare=%.6f ms tile_solve=%.6f ms "
            "tile_reduce=%.6f ms cooperative_bridge=%.6f ms "
            "finalize_solved=%.6f ms "
            "finalize_fallback=%.6f ms fallback_chief=%.6f ms "
            "fallback_edges=%.6f ms total=%.6f ms\n",
            phase_ms[kOrderedPrepare],
            phase_ms[kOrderedSeed0Solve],
            phase_ms[kOrderedSeed0Reduce],
            phase_ms[kOrderedCompactPrepare],
            phase_ms[kOrderedTileSolve],
            phase_ms[kOrderedTileReduce],
            phase_ms[kOrderedCooperativeBridge],
            phase_ms[kOrderedFinalizeSolved],
            phase_ms[kOrderedFinalizeFallback],
            phase_ms[kOrderedFallbackChief],
            phase_ms[kOrderedFallbackEdges],
            phase_ms[kOrderedPrepare] + phase_ms[kOrderedSeed0Solve] +
                phase_ms[kOrderedSeed0Reduce] +
                phase_ms[kOrderedCompactPrepare] +
                phase_ms[kOrderedTileSolve] + phase_ms[kOrderedTileReduce] +
                phase_ms[kOrderedCooperativeBridge] +
                phase_ms[kOrderedFinalizeSolved] +
                phase_ms[kOrderedFinalizeFallback] +
                phase_ms[kOrderedFallbackChief] +
                phase_ms[kOrderedFallbackEdges]);
    }
    for (const OrderedChiefPhaseEvent& sample : phase_events) {
        if (sample.begin) {
            cudaEventDestroy(sample.begin);
        }
        if (sample.end) {
            cudaEventDestroy(sample.end);
        }
    }
    if (tasks) {
        cudaFree(tasks);
    }
    if (results) {
        cudaFree(results);
    }
    if (bridge_tasks) {
        cudaFree(bridge_tasks);
    }
    if (stats) {
        cudaFree(stats);
    }
    return ok;
}
} // namespace
} // namespace optiland
