#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/spot_wavefront_records.cuh"

namespace optiland {
struct ReportTraceDebugInputs {
    DeviceConfig& cfg;
    int& trace_debug_row;
    const bool& trace_debug_enabled;
    const int& spot_pupil_count;
    std::vector<DeviceFieldSpotStats>& host_trace_debug_fields;
    std::vector<DeviceFieldWavefrontStats>& host_trace_debug_wavefront_fields;
    std::vector<DeviceSpotRaySample>& host_trace_debug_samples;
    std::vector<DeviceCalibratedModel>& host_trace_debug_models;
    bool& ok;
    const bool& parallel_spot_trace;
    const bool& needs_wavefront;
};
} // namespace optiland

namespace optiland {
template <class Inputs>
inline native_detail::PhaseOutcome report_trace_debug(Inputs& inputs) {
    auto& cfg = inputs.cfg;
    auto& trace_debug_row = inputs.trace_debug_row;
    auto& trace_debug_enabled = inputs.trace_debug_enabled;
    auto& spot_pupil_count = inputs.spot_pupil_count;
    auto& host_trace_debug_fields = inputs.host_trace_debug_fields;
    auto& host_trace_debug_wavefront_fields = inputs.host_trace_debug_wavefront_fields;
    auto& host_trace_debug_samples = inputs.host_trace_debug_samples;
    auto& host_trace_debug_models = inputs.host_trace_debug_models;
    auto& ok = inputs.ok;
    auto& parallel_spot_trace = inputs.parallel_spot_trace;
    auto& needs_wavefront = inputs.needs_wavefront;
    if (ok && trace_debug_enabled) {
        std::fprintf(
            stderr,
            "DE-CUDA trace debug row=%d fields=%d waves=%d pupils=%d\n",
            trace_debug_row,
            cfg.field_count,
            cfg.wavelength_count,
            spot_pupil_count);
        for (int fi = 0; fi < cfg.field_count; ++fi) {
            const DeviceFieldSpotStats& stats =
                host_trace_debug_fields[static_cast<std::size_t>(fi)];
            const double coverage = stats.total_weight > 0.0
                                        ? stats.valid_weight / stats.total_weight
                                        : 0.0;
            std::fprintf(
                stderr,
                "  field=%d coverage=%.17g valid=%d failed=%d attempted=%d "
                "valid_weight=%.17g failed_weight=%.17g progress=%.17g\n",
                fi,
                coverage,
                stats.valid_rays,
                stats.failed_rays,
                stats.attempted_rays,
                stats.valid_weight,
                stats.failed_weight,
                stats.trace_progress);
        }
        if (parallel_spot_trace && needs_wavefront) {
            for (int fi = 0; fi < cfg.field_count; ++fi) {
                const DeviceFieldWavefrontStats& stats =
                    host_trace_debug_wavefront_fields[static_cast<std::size_t>(fi)];
                std::fprintf(
                    stderr,
                    "  wavefront field=%d rms=%.17g max_abs=%.17g "
                    "coverage=%.17g chief_ok=%d\n",
                    fi,
                    stats.rms_waves,
                    stats.max_abs_waves,
                    stats.coverage,
                    stats.chief_ok);
            }
        }
        if (parallel_spot_trace) {
            for (int fi = 0; fi < cfg.field_count; ++fi) {
                for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
                    const std::size_t model_item =
                        static_cast<std::size_t>(fi) *
                            static_cast<std::size_t>(cfg.wavelength_count) +
                        static_cast<std::size_t>(wi);
                    const DeviceCalibratedModel& model =
                        host_trace_debug_models[model_item];
                    std::fprintf(
                        stderr,
                        "  model field=%d wave=%d valid=%d edges_valid=%d "
                        "chief_seed=(%.17g,%.17g) chief_stop=(%.17g,%.17g) "
                        "fit_rms=%.17g\n",
                        fi,
                        wi,
                        model.valid,
                        model.calibrated_edges_all_valid,
                        model.chief_seed.x,
                        model.chief_seed.y,
                        model.chief_stop_x,
                        model.chief_stop_y,
                        model.fit_rms);
                    for (int ei = 0; ei < 4; ++ei) {
                        const DeviceCalibratedEdgeAnchor& edge =
                            model.edge_anchors[ei];
                        std::fprintf(
                            stderr,
                            "    edge=%d valid=%d pupil=(%.17g,%.17g) "
                            "seed=(%.17g,%.17g) stop=(%.17g,%.17g)\n",
                            ei,
                            edge.valid,
                            edge.px,
                            edge.py,
                            edge.seed.x,
                            edge.seed.y,
                            edge.stop_x,
                            edge.stop_y);
                    }
                }
            }
            for (int fi = 0; fi < cfg.field_count; ++fi) {
                for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
                    for (int pi = 0; pi < spot_pupil_count; ++pi) {
                        const std::size_t item =
                            (static_cast<std::size_t>(fi) *
                                 static_cast<std::size_t>(cfg.wavelength_count) +
                             static_cast<std::size_t>(wi)) *
                                static_cast<std::size_t>(spot_pupil_count) +
                            static_cast<std::size_t>(pi);
                        const DeviceSpotRaySample& sample =
                            host_trace_debug_samples[item];
                        if (sample.weight <= 0.0 || sample.usable) {
                            continue;
                        }
                        std::fprintf(
                            stderr,
                            "    fail field=%d wave=%d pupil=%d code=%d "
                            "surface=%d trace_valid=%d aim_usable=%d "
                            "stop_reached=%d progress=%.17g px=%.17g py=%.17g "
                            "initial=(%.17g,%.17g,%.17g;%.17g,%.17g,%.17g) "
                            "stop=(%.17g,%.17g) final=(%.17g,%.17g,%.17g)\n",
                            fi,
                            wi,
                            pi,
                            sample.failure_code,
                            sample.failure_surface,
                            sample.trace_valid,
                            sample.aim_usable,
                            sample.stop_reached,
                            sample.weighted_progress,
                            sample.pupil_x,
                            sample.pupil_y,
                            sample.initial_x,
                            sample.initial_y,
                            sample.initial_z,
                            sample.initial_l,
                            sample.initial_m,
                            sample.initial_n,
                            sample.stop_x,
                            sample.stop_y,
                            sample.x,
                            sample.y,
                            sample.z);
                    }
                }
            }
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
