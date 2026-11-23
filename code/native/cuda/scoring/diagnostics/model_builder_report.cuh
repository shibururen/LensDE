#pragma once
#include "../prelude.cuh"
#include "../contracts/device_config.cuh"
#include "builder_instrumentation.cuh"

namespace optiland {
struct ReportModelBuilderDiagnosticsInputs {
    DeviceConfig& cfg;
    DeviceBuilderDiagnostic*& d_builder_diagnostic;
    const char*& builder_csv_path;
    const int& builder_diag_level;
    const std::size_t& calibrated_model_count;
    bool& ok;
};
} // namespace optiland

namespace optiland {
template <class Inputs>
inline native_detail::PhaseOutcome report_model_builder_diagnostics(Inputs& inputs) {
    auto& cfg = inputs.cfg;
    auto& d_builder_diagnostic = inputs.d_builder_diagnostic;
    auto& builder_csv_path = inputs.builder_csv_path;
    auto& builder_diag_level = inputs.builder_diag_level;
    auto& calibrated_model_count = inputs.calibrated_model_count;
    auto& ok = inputs.ok;
    if (d_builder_diagnostic) {
        std::vector<DeviceBuilderDiagnostic> records(2 * calibrated_model_count);
        if (cudaMemcpy(
            records.data(),
            d_builder_diagnostic,
            records.size() * sizeof(DeviceBuilderDiagnostic),
            cudaMemcpyDeviceToHost
        ) != cudaSuccess) ok = false;
        FILE* file = ok ? std::fopen(builder_csv_path, "w") : nullptr;
        if (file) {
            std::fprintf(
                file,
                "row,field,wave,repair,calls,total_cycles,chief_cycles,paraxial_ok,budget_calls,chief_trials,chief_ok,model_ok"
            );
            for (int i = 0; i < 4; ++i) std::fprintf(file, ",edge%d_cycles,e3_%d_cycles,e3_%d_calls,e3_%d_ok,edge%d_ok", i,i,i,i,i);
            const char* work_names[9] = {"chief", "edge0", "edge1", "edge2", "edge3", "e3_0", "e3_1", "e3_2", "e3_3"};
            for (const char* name : work_names) std::fprintf(file, ",%s_solves,%s_iterations,%s_probes,%s_backtracks", name,name,name,name);
            for (const char* name : work_names) std::fprintf(
                file,
                ",%s_failed_iterations,%s_duplicate_final,%s_step_x,%s_step_y,%s_ex,%s_ey",
                name,
                name,
                name,
                name,
                name,
                name
            );
            for (int i=0;i<4;++i) std::fprintf(file, ",symmetry%d_reused,symmetry%d_probe_failed",i,i);
            for (int i=0;i<4;++i) {
                std::fprintf(file, ",warm%d_attempt,warm%d_traceable,warm%d_solved",i,i,i);
                for (int j=0;j<4;++j) std::fprintf(file, ",warm%d_initial%d",i,j);
            }
            for (int j=0;j<12;++j) std::fprintf(file, ",center_%d",j);
            for (int i=0;i<4;++i) {
                std::fprintf(file, ",edge%d_repeat_prediction,edge%d_residual_ok_before_solve",i,i);
                for (int j=0;j<6;++j) std::fprintf(file, ",edge%d_initial_%d",i,j);
                for (int j=0;j<4;++j) std::fprintf(file, ",edge%d_anchor_%d",i,j);
            }

            std::fprintf(file, "\n");
            for (std::size_t idx = 0; idx < records.size(); ++idx) {
                const auto& r = records[idx];
                if (!r.calls) continue;
                const std::size_t m = idx / 2;
                std::fprintf(
                    file,
                    "%zu,%zu,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
                    m / (cfg.field_count * cfg.wavelength_count),
                    (m / cfg.wavelength_count) % cfg.field_count,
                    m % cfg.wavelength_count,
                    idx % 2,
                    r.calls,
                    r.total_cycles,
                    r.chief_cycles,
                    r.paraxial_ok,
                    r.budget_calls,
                    r.chief_trials,
                    r.chief_ok,
                    r.model_ok
                );
                for (int i = 0; i < 4; ++i) std::fprintf(
                    file,
                    ",%llu,%llu,%llu,%llu,%llu",
                    r.edge_cycles[i],
                    r.e3_cycles[i],
                    r.e3_calls[i],
                    r.e3_ok[i],
                    r.edge_ok[i]
                );
                const DeviceBuilderWork* works[9] = {&r.chief_work,&r.edge_work[0],&r.edge_work[1],&r.edge_work[2],&r.edge_work[3],&r.e3_work[0],&r.e3_work[1],&r.e3_work[2],&r.e3_work[3]};
                for (const auto* w : works) std::fprintf(file, ",%llu,%llu,%llu,%llu", w->calls,w->iterations,w->probes,w->backtracks);
                for (const auto* w : works) std::fprintf(
                    file,
                    ",%llu,%llu,%.17g,%.17g,%.17g,%.17g",
                    w->failed_iterations,
                    w->duplicate_final,
                    w->first_step_x,
                    w->first_step_y,
                    w->first_ex,
                    w->first_ey
                );
                for (int i=0;i<4;++i) std::fprintf(file, ",%llu,%llu",r.symmetry_reused[i],r.symmetry_probe_failed[i]);
                for (int i=0;i<4;++i) {
                    std::fprintf(file, ",%llu,%llu,%llu",r.warm_attempt[i],r.warm_traceable[i],r.warm_solved[i]);
                    for (double v : r.warm_initial[i]) std::fprintf(file, ",%.17g",v);
                }
                for (double v : r.center_data) std::fprintf(file, ",%.17g",v);
                for (int i=0;i<4;++i) {
                    std::fprintf(file, ",%llu,%llu",r.repeated_prediction[i],r.residual_ok_window_rejected[i]);
                    for (double v : r.initial_data[i]) std::fprintf(file, ",%.17g",v);
                    for (double v : r.anchor_data[i]) std::fprintf(file, ",%.17g",v);
                }

                std::fprintf(file, "\n");
            }
            std::fclose(file);
            std::fprintf(
                stderr,
                "Builder diagnostic saved: level=%d records=%zu path=%s\n",
                builder_diag_level,
                records.size(),
                builder_csv_path
            );
        } else ok = false;
        DeviceBuilderDiagnostic* null_builder = nullptr;
        cudaMemcpyToSymbol(g_builder_diagnostic, &null_builder, sizeof(null_builder));
        cudaFree(d_builder_diagnostic);
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
