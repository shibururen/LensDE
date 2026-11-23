#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../kernels/ordered_chief_prepare.cuh"
#include "../kernels/ordered_chief_solve.cuh"

namespace optiland {
namespace {
bool run_calibrated_chief_seed0_diagnostic(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    const DeviceConfig& cfg,
    int population_size,
    int center_field_index) {
    DeviceOrderedChiefTask* tasks = nullptr;
    DeviceOrderedChiefTrialResult* tracked_results = nullptr;
    DeviceOrderedChiefTrialResult* seed_only_results = nullptr;
    cudaEvent_t begin = nullptr;
    cudaEvent_t end = nullptr;
    const std::size_t task_bytes =
        static_cast<std::size_t>(population_size) *
        sizeof(DeviceOrderedChiefTask);
    const std::size_t result_bytes =
        static_cast<std::size_t>(population_size) *
        sizeof(DeviceOrderedChiefTrialResult);
    bool ok =
        cudaMalloc(&tasks, task_bytes) == cudaSuccess &&
        cudaMalloc(&tracked_results, result_bytes) == cudaSuccess &&
        cudaMalloc(&seed_only_results, result_bytes) == cudaSuccess &&
        cudaEventCreate(&begin) == cudaSuccess &&
        cudaEventCreate(&end) == cudaSuccess;
    int repeats = 20;
    if (const char* value = std::getenv(
            "OPTILAND_DE_CUDA_CHIEF_SEED0_DIAGNOSTIC_REPEATS")) {
        repeats = std::max(1, std::atoi(value));
    }
    constexpr int kPrepareBlock = 32;
    constexpr int kSolveBlock = 32;
    const int prepare_grid =
        (population_size + kPrepareBlock - 1) / kPrepareBlock;
    const int solve_grid =
        (population_size + kSolveBlock - 1) / kSolveBlock;
    std::vector<DeviceOrderedChiefTask> host_tasks(
        static_cast<std::size_t>(population_size));
    std::vector<DeviceOrderedChiefTrialResult> host_tracked(
        static_cast<std::size_t>(population_size));
    std::vector<DeviceOrderedChiefTrialResult> host_seed_only(
        static_cast<std::size_t>(population_size));
    long long eligible_tasks = 0;
    long long success_mismatches = 0;
    long long seed_exact_mismatches = 0;
    double max_seed_delta = 0.0;
    double max_stop_delta = 0.0;
    double max_residual_delta = 0.0;
    double tracked_seconds = 0.0;
    double seed_only_seconds = 0.0;
    auto run_one = [&](int field_index, int wave_index) {
        if (!ok) {
            return;
        }
        prepare_ordered_chief_tasks_kernel<<<prepare_grid, kPrepareBlock>>>(
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
                false,
                tasks,
                nullptr);
        ok = cudaGetLastError() == cudaSuccess &&
             cudaDeviceSynchronize() == cudaSuccess;
        if (!ok) {
            return;
        }
        solve_ordered_chief_tile_kernel<true>
            <<<solve_grid, kSolveBlock>>>(
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
                tracked_results);
        solve_ordered_chief_tile_kernel<false>
            <<<solve_grid, kSolveBlock>>>(
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
                seed_only_results);
        ok = cudaGetLastError() == cudaSuccess &&
             cudaDeviceSynchronize() == cudaSuccess;
        if (!ok) {
            return;
        }
        float tracked_ms = 0.0f;
        float seed_only_ms = 0.0f;
        cudaEventRecord(begin);
        for (int repeat = 0; repeat < repeats; ++repeat) {
            solve_ordered_chief_tile_kernel<true>
                <<<solve_grid, kSolveBlock>>>(
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
                    tracked_results);
        }
        cudaEventRecord(end);
        ok = cudaEventSynchronize(end) == cudaSuccess &&
             cudaEventElapsedTime(&tracked_ms, begin, end) == cudaSuccess;
        if (!ok) {
            return;
        }
        cudaEventRecord(begin);
        for (int repeat = 0; repeat < repeats; ++repeat) {
            solve_ordered_chief_tile_kernel<false>
                <<<solve_grid, kSolveBlock>>>(
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
                    seed_only_results);
        }
        cudaEventRecord(end);
        ok = cudaEventSynchronize(end) == cudaSuccess &&
             cudaEventElapsedTime(&seed_only_ms, begin, end) == cudaSuccess;
        if (!ok) {
            return;
        }
        tracked_seconds +=
            static_cast<double>(tracked_ms) * 1.0e-3 / repeats;
        seed_only_seconds +=
            static_cast<double>(seed_only_ms) * 1.0e-3 / repeats;
        ok = cudaMemcpy(
                 host_tasks.data(),
                 tasks,
                 task_bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess &&
             cudaMemcpy(
                 host_tracked.data(),
                 tracked_results,
                 result_bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess &&
             cudaMemcpy(
                 host_seed_only.data(),
                 seed_only_results,
                 result_bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess;
        if (!ok) {
            return;
        }
        for (int row = 0; row < population_size; ++row) {
            const DeviceOrderedChiefTask& task =
                host_tasks[static_cast<std::size_t>(row)];
            if (task.chief_valid || task.trial_count <= 0) {
                continue;
            }
            ++eligible_tasks;
            const DeviceOrderedChiefTrialResult& tracked =
                host_tracked[static_cast<std::size_t>(row)];
            const DeviceOrderedChiefTrialResult& seed_only =
                host_seed_only[static_cast<std::size_t>(row)];
            if ((tracked.success != 0) != (seed_only.success != 0) ||
                (tracked.chief_seed.valid != 0) !=
                    (seed_only.chief_seed.valid != 0)) {
                ++success_mismatches;
            }
            if (tracked.success && seed_only.success &&
                tracked.chief_seed.valid && seed_only.chief_seed.valid) {
                const double seed_delta = hypot(
                    tracked.chief_seed.x - seed_only.chief_seed.x,
                    tracked.chief_seed.y - seed_only.chief_seed.y);
                max_seed_delta = std::max(max_seed_delta, seed_delta);
                if (tracked.chief_seed.x != seed_only.chief_seed.x ||
                    tracked.chief_seed.y != seed_only.chief_seed.y) {
                    ++seed_exact_mismatches;
                }
                max_stop_delta = std::max(
                    max_stop_delta,
                    hypot(
                        tracked.stop_x - seed_only.stop_x,
                        tracked.stop_y - seed_only.stop_y));
                max_residual_delta = std::max(
                    max_residual_delta,
                    fabs(tracked.residual - seed_only.residual));
            }
        }
    };
    for (int wave_index = 0;
         ok && wave_index < cfg.wavelength_count;
         ++wave_index) {
        run_one(center_field_index, wave_index);
    }
    for (int field_index = 0;
         ok && field_index < cfg.field_count;
         ++field_index) {
        if (field_index == center_field_index) {
            continue;
        }
        for (int wave_index = 0;
             ok && wave_index < cfg.wavelength_count;
             ++wave_index) {
            run_one(field_index, wave_index);
        }
    }
    if (ok) {
        std::fprintf(
            stderr,
            "DE-CUDA chief seed0 ray-state diagnostic: eligible=%lld "
            "repeats=%d tracked=%.9f s seed_only=%.9f s speedup=%.4fx "
            "success_mismatches=%lld seed_exact_mismatches=%lld "
            "max_seed_delta=%.17g max_stop_delta=%.17g "
            "max_residual_delta=%.17g\n",
            eligible_tasks,
            repeats,
            tracked_seconds,
            seed_only_seconds,
            seed_only_seconds > 0.0
                ? tracked_seconds / seed_only_seconds
                : 0.0,
            success_mismatches,
            seed_exact_mismatches,
            max_seed_delta,
            max_stop_delta,
            max_residual_delta);
    }
    if (tasks) {
        cudaFree(tasks);
    }
    if (tracked_results) {
        cudaFree(tracked_results);
    }
    if (seed_only_results) {
        cudaFree(seed_only_results);
    }
    if (begin) {
        cudaEventDestroy(begin);
    }
    if (end) {
        cudaEventDestroy(end);
    }
    return ok;
}
} // namespace
} // namespace optiland
