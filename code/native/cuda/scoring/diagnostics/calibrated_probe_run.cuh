#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "calibrated_probe_records.cuh"
#include "../kernels/ordered_chief_prepare.cuh"

namespace optiland {
namespace {
bool run_calibrated_probe_diagnostic(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    const DeviceConfig& cfg,
    int population_size) {
    if (!candidate_surfaces || !fields || !waves || !models ||
        population_size <= 0) {
        return false;
    }
    const int item_count =
        population_size * cfg.field_count * cfg.wavelength_count;
    const std::size_t bytes =
        static_cast<std::size_t>(item_count) *
        sizeof(DeviceCalibratedProbeSummary);
    DeviceCalibratedProbeSummary* serial_output = nullptr;
    DeviceCalibratedProbeSummary* cooperative_output = nullptr;
    cudaEvent_t begin = nullptr;
    cudaEvent_t end = nullptr;
    bool ok =
        cudaMalloc(&serial_output, bytes) == cudaSuccess &&
        cudaMalloc(&cooperative_output, bytes) == cudaSuccess &&
        cudaEventCreate(&begin) == cudaSuccess &&
        cudaEventCreate(&end) == cudaSuccess;
    int repeats = 20;
    if (const char* value =
            std::getenv("OPTILAND_DE_CUDA_COOPERATIVE_PROBE_REPEATS")) {
        repeats = std::max(1, std::atoi(value));
    }
    constexpr int kSerialBlock = 32;
    constexpr int kCooperativeBlock = 128;
    const int serial_grid = (item_count + kSerialBlock - 1) / kSerialBlock;
    const int cooperative_threads = item_count * 32;
    const int cooperative_grid =
        (cooperative_threads + kCooperativeBlock - 1) / kCooperativeBlock;
    float serial_ms = 0.0f;
    float cooperative_ms = 0.0f;
    if (ok) {
        calibrated_probe_serial_kernel<<<serial_grid, kSerialBlock>>>(
            candidate_surfaces,
            fields,
            waves,
            models,
            cfg,
            population_size,
            serial_output);
        calibrated_probe_cooperative_kernel
            <<<cooperative_grid, kCooperativeBlock>>>(
                candidate_surfaces,
                fields,
                waves,
                models,
                cfg,
                population_size,
                cooperative_output);
        ok = cudaGetLastError() == cudaSuccess &&
             cudaDeviceSynchronize() == cudaSuccess;
    }
    if (ok) {
        cudaEventRecord(begin);
        for (int i = 0; i < repeats; ++i) {
            calibrated_probe_serial_kernel<<<serial_grid, kSerialBlock>>>(
                candidate_surfaces,
                fields,
                waves,
                models,
                cfg,
                population_size,
                serial_output);
        }
        cudaEventRecord(end);
        ok = cudaEventSynchronize(end) == cudaSuccess &&
             cudaEventElapsedTime(&serial_ms, begin, end) == cudaSuccess;
    }
    if (ok) {
        cudaEventRecord(begin);
        for (int i = 0; i < repeats; ++i) {
            calibrated_probe_cooperative_kernel
                <<<cooperative_grid, kCooperativeBlock>>>(
                    candidate_surfaces,
                    fields,
                    waves,
                    models,
                    cfg,
                    population_size,
                    cooperative_output);
        }
        cudaEventRecord(end);
        ok = cudaEventSynchronize(end) == cudaSuccess &&
             cudaEventElapsedTime(&cooperative_ms, begin, end) == cudaSuccess;
    }
    std::vector<DeviceCalibratedProbeSummary> host_serial;
    std::vector<DeviceCalibratedProbeSummary> host_cooperative;
    if (ok) {
        host_serial.resize(static_cast<std::size_t>(item_count));
        host_cooperative.resize(static_cast<std::size_t>(item_count));
        ok = cudaMemcpy(
                 host_serial.data(),
                 serial_output,
                 bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess &&
             cudaMemcpy(
                 host_cooperative.data(),
                 cooperative_output,
                 bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }
    long long mask_mismatches = 0;
    double max_abs_delta = 0.0;
    if (ok) {
        for (int i = 0; i < item_count; ++i) {
            const auto& a = host_serial[static_cast<std::size_t>(i)];
            const auto& b = host_cooperative[static_cast<std::size_t>(i)];
            if (a.valid_mask != b.valid_mask) {
                ++mask_mismatches;
            }
            max_abs_delta = std::max(
                max_abs_delta,
                std::fabs(a.weighted_stop_x - b.weighted_stop_x));
            max_abs_delta = std::max(
                max_abs_delta,
                std::fabs(a.weighted_stop_y - b.weighted_stop_y));
        }
        const double serial_per_repeat =
            static_cast<double>(serial_ms) * 1.0e-3 / repeats;
        const double cooperative_per_repeat =
            static_cast<double>(cooperative_ms) * 1.0e-3 / repeats;
        std::fprintf(
            stderr,
            "DE-CUDA cooperative probe diagnostic: models=%d probes=%d "
            "repeats=%d serial=%.9f s cooperative=%.9f s speedup=%.4fx "
            "mask_mismatches=%lld max_abs_delta=%.17g\n",
            item_count,
            item_count * 5,
            repeats,
            serial_per_repeat,
            cooperative_per_repeat,
            cooperative_per_repeat > 0.0
                ? serial_per_repeat / cooperative_per_repeat
                : 0.0,
            mask_mismatches,
            max_abs_delta);
    }
    if (ok) {
        const int group_sizes[3] = {8, 16, 32};
        for (int config_index = 0; config_index < 3 && ok; ++config_index) {
            const int group_size = group_sizes[config_index];
            const int probe_count = group_size;
            const int grouped_threads = item_count * group_size;
            const int grouped_grid =
                (grouped_threads + kCooperativeBlock - 1) /
                kCooperativeBlock;
            float tiled_serial_ms = 0.0f;
            float grouped_ms = 0.0f;
            cudaEventRecord(begin);
            for (int i = 0; i < repeats; ++i) {
                calibrated_probe_serial_tiled_kernel
                    <<<serial_grid, kSerialBlock>>>(
                        candidate_surfaces,
                        fields,
                        waves,
                        models,
                        cfg,
                        population_size,
                        probe_count,
                        serial_output);
            }
            cudaEventRecord(end);
            ok = cudaEventSynchronize(end) == cudaSuccess &&
                 cudaEventElapsedTime(
                     &tiled_serial_ms,
                     begin,
                     end) == cudaSuccess;
            if (ok) {
                cudaEventRecord(begin);
                for (int i = 0; i < repeats; ++i) {
                    calibrated_probe_group_kernel
                        <<<grouped_grid, kCooperativeBlock>>>(
                            candidate_surfaces,
                            fields,
                            waves,
                            models,
                            cfg,
                            population_size,
                            group_size,
                            probe_count,
                            cooperative_output);
                }
                cudaEventRecord(end);
                ok = cudaEventSynchronize(end) == cudaSuccess &&
                     cudaEventElapsedTime(
                         &grouped_ms,
                         begin,
                         end) == cudaSuccess;
            }
            if (ok) {
                ok = cudaMemcpy(
                         host_serial.data(),
                         serial_output,
                         bytes,
                         cudaMemcpyDeviceToHost) == cudaSuccess &&
                     cudaMemcpy(
                         host_cooperative.data(),
                         cooperative_output,
                         bytes,
                         cudaMemcpyDeviceToHost) == cudaSuccess;
            }
            mask_mismatches = 0;
            max_abs_delta = 0.0;
            if (ok) {
                for (int i = 0; i < item_count; ++i) {
                    const auto& a = host_serial[static_cast<std::size_t>(i)];
                    const auto& b =
                        host_cooperative[static_cast<std::size_t>(i)];
                    if (a.valid_mask != b.valid_mask) {
                        ++mask_mismatches;
                    }
                    max_abs_delta = std::max(
                        max_abs_delta,
                        std::fabs(
                            a.weighted_stop_x - b.weighted_stop_x));
                    max_abs_delta = std::max(
                        max_abs_delta,
                        std::fabs(
                            a.weighted_stop_y - b.weighted_stop_y));
                }
                const double serial_per_repeat =
                    static_cast<double>(tiled_serial_ms) * 1.0e-3 /
                    repeats;
                const double grouped_per_repeat =
                    static_cast<double>(grouped_ms) * 1.0e-3 / repeats;
                std::fprintf(
                    stderr,
                    "DE-CUDA grouped probe diagnostic: group=%d probes=%d "
                    "models=%d serial=%.9f s grouped=%.9f s speedup=%.4fx "
                    "mask_mismatches=%lld max_abs_delta=%.17g\n",
                    group_size,
                    probe_count,
                    item_count,
                    serial_per_repeat,
                    grouped_per_repeat,
                    grouped_per_repeat > 0.0
                        ? serial_per_repeat / grouped_per_repeat
                        : 0.0,
                    mask_mismatches,
                    max_abs_delta);
            }
        }
    }
    if (serial_output) {
        cudaFree(serial_output);
    }
    if (cooperative_output) {
        cudaFree(cooperative_output);
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

namespace optiland {
namespace {
bool run_calibrated_precision_probe_diagnostic(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    const DeviceConfig& cfg,
    int population_size) {
    constexpr int kProbesPerModel = 16;
    constexpr int kBlock = 128;
    const int model_count =
        population_size * cfg.field_count * cfg.wavelength_count;
    const int probe_count = model_count * kProbesPerModel;
    const int grid = (probe_count + kBlock - 1) / kBlock;
    const std::size_t bytes =
        static_cast<std::size_t>(probe_count) *
        sizeof(DevicePrecisionProbeSummary);
    DevicePrecisionProbeSummary* double_output = nullptr;
    DevicePrecisionProbeSummary* float_output = nullptr;
    cudaEvent_t begin = nullptr;
    cudaEvent_t end = nullptr;
    bool ok =
        cudaMalloc(&double_output, bytes) == cudaSuccess &&
        cudaMalloc(&float_output, bytes) == cudaSuccess &&
        cudaEventCreate(&begin) == cudaSuccess &&
        cudaEventCreate(&end) == cudaSuccess;
    int repeats = 20;
    if (const char* value =
            std::getenv("OPTILAND_DE_CUDA_MIXED_PRECISION_PROBE_REPEATS")) {
        repeats = std::max(1, std::atoi(value));
    }
    float double_ms = 0.0f;
    float float_ms = 0.0f;
    if (ok) {
        calibrated_probe_flat_double_kernel<<<grid, kBlock>>>(
            candidate_surfaces,
            fields,
            waves,
            models,
            cfg,
            population_size,
            kProbesPerModel,
            double_output);
        calibrated_probe_flat_float_kernel<<<grid, kBlock>>>(
            candidate_surfaces,
            fields,
            waves,
            models,
            cfg,
            population_size,
            kProbesPerModel,
            float_output);
        ok = cudaGetLastError() == cudaSuccess &&
             cudaDeviceSynchronize() == cudaSuccess;
    }
    if (ok) {
        cudaEventRecord(begin);
        for (int i = 0; i < repeats; ++i) {
            calibrated_probe_flat_double_kernel<<<grid, kBlock>>>(
                candidate_surfaces,
                fields,
                waves,
                models,
                cfg,
                population_size,
                kProbesPerModel,
                double_output);
        }
        cudaEventRecord(end);
        ok = cudaEventSynchronize(end) == cudaSuccess &&
             cudaEventElapsedTime(&double_ms, begin, end) == cudaSuccess;
    }
    if (ok) {
        cudaEventRecord(begin);
        for (int i = 0; i < repeats; ++i) {
            calibrated_probe_flat_float_kernel<<<grid, kBlock>>>(
                candidate_surfaces,
                fields,
                waves,
                models,
                cfg,
                population_size,
                kProbesPerModel,
                float_output);
        }
        cudaEventRecord(end);
        ok = cudaEventSynchronize(end) == cudaSuccess &&
             cudaEventElapsedTime(&float_ms, begin, end) == cudaSuccess;
    }
    std::vector<DevicePrecisionProbeSummary> host_double;
    std::vector<DevicePrecisionProbeSummary> host_float;
    if (ok) {
        host_double.resize(static_cast<std::size_t>(probe_count));
        host_float.resize(static_cast<std::size_t>(probe_count));
        ok = cudaMemcpy(
                 host_double.data(),
                 double_output,
                 bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess &&
             cudaMemcpy(
                 host_float.data(),
                 float_output,
                 bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }
    long long double_valid = 0;
    long long float_valid = 0;
    long long validity_mismatches = 0;
    std::vector<double> coordinate_errors;
    if (ok) {
        coordinate_errors.reserve(static_cast<std::size_t>(probe_count));
        for (int i = 0; i < probe_count; ++i) {
            const auto& reference = host_double[static_cast<std::size_t>(i)];
            const auto& approximate = host_float[static_cast<std::size_t>(i)];
            double_valid += reference.valid != 0;
            float_valid += approximate.valid != 0;
            if ((reference.valid != 0) != (approximate.valid != 0)) {
                ++validity_mismatches;
            }
            if (reference.valid && approximate.valid) {
                coordinate_errors.push_back(hypot(
                    reference.stop_x - approximate.stop_x,
                    reference.stop_y - approximate.stop_y));
            }
        }
        std::sort(coordinate_errors.begin(), coordinate_errors.end());
        auto percentile = [&](double fraction) {
            if (coordinate_errors.empty()) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            const std::size_t index = static_cast<std::size_t>(
                fraction * static_cast<double>(coordinate_errors.size() - 1));
            return coordinate_errors[index];
        };
        const double double_per_repeat =
            static_cast<double>(double_ms) * 1.0e-3 / repeats;
        const double float_per_repeat =
            static_cast<double>(float_ms) * 1.0e-3 / repeats;
        std::fprintf(
            stderr,
            "DE-CUDA mixed precision probe diagnostic: models=%d probes=%d "
            "repeats=%d fp64=%.9f s fp32=%.9f s speedup=%.4fx "
            "fp64_valid=%lld fp32_valid=%lld validity_mismatches=%lld "
            "error_p50=%.9g error_p95=%.9g error_p99=%.9g "
            "error_max=%.9g\n",
            model_count,
            probe_count,
            repeats,
            double_per_repeat,
            float_per_repeat,
            float_per_repeat > 0.0
                ? double_per_repeat / float_per_repeat
                : 0.0,
            double_valid,
            float_valid,
            validity_mismatches,
            percentile(0.50),
            percentile(0.95),
            percentile(0.99),
            coordinate_errors.empty() ? NAN : coordinate_errors.back());
    }
    if (double_output) {
        cudaFree(double_output);
    }
    if (float_output) {
        cudaFree(float_output);
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

namespace optiland {
namespace {
bool run_calibrated_edge_replay_diagnostic(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    const DeviceConfig& cfg,
    int population_size,
    int center_field_index) {
    if (!candidate_surfaces || !fields || !waves || !models ||
        population_size <= 0) {
        return false;
    }
    const int model_count =
        population_size * cfg.field_count * cfg.wavelength_count;
    const std::size_t bytes =
        static_cast<std::size_t>(model_count) *
        sizeof(DeviceCalibratedModel);
    DeviceCalibratedModel* edge0_models = nullptr;
    DeviceCalibratedModel* full_models = nullptr;
    cudaEvent_t begin = nullptr;
    cudaEvent_t end = nullptr;
    bool ok =
        cudaMalloc(&edge0_models, bytes) == cudaSuccess &&
        cudaMalloc(&full_models, bytes) == cudaSuccess &&
        cudaEventCreate(&begin) == cudaSuccess &&
        cudaEventCreate(&end) == cudaSuccess;
    int repeats = 20;
    if (const char* value = std::getenv(
            "OPTILAND_DE_CUDA_EDGE_REPLAY_DIAGNOSTIC_REPEATS")) {
        repeats = std::max(1, std::atoi(value));
    }
    constexpr int kBlock = 32;
    const int grid = (model_count + kBlock - 1) / kBlock;
    float edge0_ms = 0.0f;
    float full_ms = 0.0f;
    if (ok) {
        replay_calibrated_edges_kernel<1><<<grid, kBlock>>>(
            candidate_surfaces,
            fields,
            waves,
            cfg,
            population_size,
            center_field_index,
            models,
            edge0_models);
        replay_calibrated_edges_kernel<4><<<grid, kBlock>>>(
            candidate_surfaces,
            fields,
            waves,
            cfg,
            population_size,
            center_field_index,
            models,
            full_models);
        ok = cudaGetLastError() == cudaSuccess &&
             cudaDeviceSynchronize() == cudaSuccess;
    }
    if (ok) {
        cudaEventRecord(begin);
        for (int i = 0; i < repeats; ++i) {
            replay_calibrated_edges_kernel<1><<<grid, kBlock>>>(
                candidate_surfaces,
                fields,
                waves,
                cfg,
                population_size,
                center_field_index,
                models,
                edge0_models);
        }
        cudaEventRecord(end);
        ok = cudaEventSynchronize(end) == cudaSuccess &&
             cudaEventElapsedTime(&edge0_ms, begin, end) == cudaSuccess;
    }
    if (ok) {
        cudaEventRecord(begin);
        for (int i = 0; i < repeats; ++i) {
            replay_calibrated_edges_kernel<4><<<grid, kBlock>>>(
                candidate_surfaces,
                fields,
                waves,
                cfg,
                population_size,
                center_field_index,
                models,
                full_models);
        }
        cudaEventRecord(end);
        ok = cudaEventSynchronize(end) == cudaSuccess &&
             cudaEventElapsedTime(&full_ms, begin, end) == cudaSuccess;
    }
    std::vector<DeviceCalibratedModel> host_source;
    std::vector<DeviceCalibratedModel> host_edge0;
    std::vector<DeviceCalibratedModel> host_full;
    if (ok) {
        host_source.resize(static_cast<std::size_t>(model_count));
        host_edge0.resize(static_cast<std::size_t>(model_count));
        host_full.resize(static_cast<std::size_t>(model_count));
        ok = cudaMemcpy(
                 host_source.data(),
                 models,
                 bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess &&
             cudaMemcpy(
                 host_edge0.data(),
                 edge0_models,
                 bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess &&
             cudaMemcpy(
                 host_full.data(),
                 full_models,
                 bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }
    long long edge0_valid_mismatches = 0;
    long long edge0_seed_mismatches = 0;
    long long full_valid_mismatches = 0;
    long long full_seed_mismatches = 0;
    long long full_model_flag_mismatches = 0;
    double edge0_max_seed_delta = 0.0;
    double full_max_seed_delta = 0.0;
    if (ok) {
        for (int item = 0; item < model_count; ++item) {
            const DeviceCalibratedModel& source =
                host_source[static_cast<std::size_t>(item)];
            const DeviceCalibratedModel& edge0 =
                host_edge0[static_cast<std::size_t>(item)];
            const DeviceCalibratedModel& full =
                host_full[static_cast<std::size_t>(item)];
            const DeviceCalibratedEdgeAnchor& source_edge0 =
                source.edge_anchors[0];
            const DeviceCalibratedEdgeAnchor& replay_edge0 =
                edge0.edge_anchors[0];
            if ((source_edge0.valid != 0) != (replay_edge0.valid != 0) ||
                (source_edge0.seed.valid != 0) !=
                    (replay_edge0.seed.valid != 0)) {
                ++edge0_valid_mismatches;
            }
            if (source_edge0.seed.valid && replay_edge0.seed.valid) {
                const double delta = hypot(
                    source_edge0.seed.x - replay_edge0.seed.x,
                    source_edge0.seed.y - replay_edge0.seed.y);
                edge0_max_seed_delta = std::max(edge0_max_seed_delta, delta);
                if (source_edge0.seed.x != replay_edge0.seed.x ||
                    source_edge0.seed.y != replay_edge0.seed.y) {
                    ++edge0_seed_mismatches;
                }
            }
            if (source.calibrated_edges_all_valid !=
                    full.calibrated_edges_all_valid ||
                source.valid != full.valid) {
                ++full_model_flag_mismatches;
            }
            for (int anchor_index = 0; anchor_index < 4; ++anchor_index) {
                const DeviceCalibratedEdgeAnchor& a =
                    source.edge_anchors[anchor_index];
                const DeviceCalibratedEdgeAnchor& b =
                    full.edge_anchors[anchor_index];
                if ((a.valid != 0) != (b.valid != 0) ||
                    (a.seed.valid != 0) != (b.seed.valid != 0)) {
                    ++full_valid_mismatches;
                }
                if (a.seed.valid && b.seed.valid) {
                    const double delta = hypot(
                        a.seed.x - b.seed.x,
                        a.seed.y - b.seed.y);
                    full_max_seed_delta =
                        std::max(full_max_seed_delta, delta);
                    if (a.seed.x != b.seed.x || a.seed.y != b.seed.y) {
                        ++full_seed_mismatches;
                    }
                }
            }
        }
        const double edge0_per_repeat =
            static_cast<double>(edge0_ms) * 1.0e-3 / repeats;
        const double full_per_repeat =
            static_cast<double>(full_ms) * 1.0e-3 / repeats;
        std::fprintf(
            stderr,
            "DE-CUDA calibrated edge replay diagnostic: models=%d "
            "repeats=%d edge0=%.9f s full4=%.9f s full_over_edge0=%.4fx "
            "edge0_valid_mismatches=%lld edge0_seed_mismatches=%lld "
            "edge0_max_seed_delta=%.17g full_valid_mismatches=%lld "
            "full_seed_mismatches=%lld full_max_seed_delta=%.17g "
            "full_model_flag_mismatches=%lld\n",
            model_count,
            repeats,
            edge0_per_repeat,
            full_per_repeat,
            edge0_per_repeat > 0.0
                ? full_per_repeat / edge0_per_repeat
                : 0.0,
            edge0_valid_mismatches,
            edge0_seed_mismatches,
            edge0_max_seed_delta,
            full_valid_mismatches,
            full_seed_mismatches,
            full_max_seed_delta,
            full_model_flag_mismatches);
    }
    if (edge0_models) {
        cudaFree(edge0_models);
    }
    if (full_models) {
        cudaFree(full_models);
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
