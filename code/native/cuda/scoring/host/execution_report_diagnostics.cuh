#pragma once
#include "../prelude.cuh"
#include "../diagnostics/feasibility_report.cuh"
#include "../diagnostics/model_builder_report.cuh"
#include "../diagnostics/root_profile_report.cuh"
#include "../diagnostics/trace_report.cuh"
#include "../diagnostics/uc5_profile_report.cuh"
#include "execution_context.cuh"
#include "../merit/unified_residuals.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::report_diagnostics() {
    if (ok && asphere_deep_profile_enabled) {
        print_asphere_deep_profile();
    }

    ReportUc5ProfileInputs report_uc5_profile_inputs{
        population_size,
        asphere_uc5_profile_enabled,
        uc5_evaluation_index,
        host_asphere_uc5_profile,
        ok,
        uc5_symmetry_flag,
        uc5_primary_abort_flag,
        uc5_candidate_batch_flag
    };

    report_uc5_profile(report_uc5_profile_inputs);

    ReportTraceDebugInputs report_trace_debug_inputs{
        cfg,
        trace_debug_row,
        trace_debug_enabled,
        spot_pupil_count,
        host_trace_debug_fields,
        host_trace_debug_wavefront_fields,
        host_trace_debug_samples,
        host_trace_debug_models,
        ok,
        parallel_spot_trace,
        needs_wavefront
    };

    report_trace_debug(report_trace_debug_inputs);

    if (ok && constraint_debug_enabled) {
        std::fprintf(
            stderr,
            "DE-CUDA constraint debug row=%d surface_count=%d\n",
            constraint_debug_row,
            cfg.surface_count);
        for (int i = 0; i < cfg.surface_count; ++i) {
            const double* value =
                host_constraint_debug.data() +
                static_cast<std::size_t>(i) * kConstraintDebugStride;
            std::fprintf(
                stderr,
                "  i=%d kind=%.0f semi=%.17g aperture=%.17g mechanical=%.17g "
                "center=%.17g edge=%.17g radius=%.17g atc=%.17g ate=%.17g "
                "edge_delta=%.17g ate_delta=%.17g max_height=%.17g "
                "field=%.0f wave=%.0f pupil=%.0f px=%.17g py=%.17g\n",
                i,
                value[0],
                value[1],
                value[2],
                value[3],
                value[4],
                value[5],
                value[6],
                value[7],
                value[8],
                value[9],
                value[10],
                value[11],
                value[12],
                value[13],
                value[14],
                value[15],
                value[16]);
        }
    }

    if (memory_debug_enabled) {
        cudaMemGetInfo(&memory_free_before_cleanup, &memory_total);
    }

    ReportAsphereRootProfileInputs report_asphere_root_profile_inputs{cfg, asphere_root_profile_enabled, host_asphere_root_profile, ok};

    report_asphere_root_profile(report_asphere_root_profile_inputs);

    ReportAsphereFeasibilityProfileInputs report_asphere_feasibility_profile_inputs{
        asphere_feasibility_profile_enabled,
        asphere_feasibility_timing,
        enable_calibrated_safeguarded_recovery,
        host_asphere_root_profile,
        root_profile_after_geometry,
        root_profile_after_initial_models,
        root_profile_after_aperture,
        root_profile_after_final_models,
        host_asphere_feasibility_profile,
        ok
    };

    report_asphere_feasibility_profile(report_asphere_feasibility_profile_inputs);

    ReportModelBuilderDiagnosticsInputs report_model_builder_diagnostics_inputs{cfg, d_builder_diagnostic, builder_csv_path, builder_diag_level, calibrated_model_count, ok};

    report_model_builder_diagnostics(report_model_builder_diagnostics_inputs);

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
