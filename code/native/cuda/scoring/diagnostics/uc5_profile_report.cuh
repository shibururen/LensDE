#pragma once
#include "../prelude.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "asphere_profile_records.cuh"

namespace optiland {
struct ReportUc5ProfileInputs {
    int& population_size;
    const bool& asphere_uc5_profile_enabled;
    const unsigned long long& uc5_evaluation_index;
    DeviceAsphereUC5Profile& host_asphere_uc5_profile;
    bool& ok;
    const int& uc5_symmetry_flag;
    const int& uc5_primary_abort_flag;
    const int& uc5_candidate_batch_flag;
};
} // namespace optiland

namespace optiland {
template <class Inputs>
inline native_detail::PhaseOutcome report_uc5_profile(Inputs& inputs) {
    auto& population_size = inputs.population_size;
    auto& asphere_uc5_profile_enabled = inputs.asphere_uc5_profile_enabled;
    auto& uc5_evaluation_index = inputs.uc5_evaluation_index;
    auto& host_asphere_uc5_profile = inputs.host_asphere_uc5_profile;
    auto& ok = inputs.ok;
    auto& uc5_symmetry_flag = inputs.uc5_symmetry_flag;
    auto& uc5_primary_abort_flag = inputs.uc5_primary_abort_flag;
    auto& uc5_candidate_batch_flag = inputs.uc5_candidate_batch_flag;
    if (ok && asphere_uc5_profile_enabled) {
        const char* evaluation_phase = uc5_evaluation_index == 0
            ? "initial"
            : (uc5_evaluation_index == 1 ? "trial" : "later");
        std::fprintf(
            stderr,
            "DE-CUDA asphere UC5 profile: evaluation=%s index=%llu "
            "symmetry=%d primary_abort=%d candidate_batch=%d "
            "continuation_bytes=%zu worst_active_states=%d\n",
            evaluation_phase,
            uc5_evaluation_index,
            uc5_symmetry_flag,
            uc5_primary_abort_flag,
            uc5_candidate_batch_flag,
            sizeof(DeviceUC5CandidateFamilyState),
            population_size);
        for (int build_kind = 0;
             build_kind < kUC5CounterBuildKinds;
             ++build_kind) {
            for (int wave_role = 0;
                 wave_role < kUC5CounterWaveRoles;
                 ++wave_role) {
                for (int anchor = 0;
                     anchor < kUC5CounterAnchors;
                     ++anchor) {
                    const int cell =
                        (build_kind * kUC5CounterWaveRoles + wave_role) *
                            kUC5CounterAnchors +
                        anchor;
                    const DeviceAsphereUC5Profile& p =
                        host_asphere_uc5_profile;
                    const unsigned long long total =
                        p.anchor_entered[cell] + p.anchor_mirrored[cell] +
                        p.anchor_skipped_after_failure[cell] +
                        p.cached_accepted[cell] + p.cached_traceable[cell] +
                        p.base_probes[cell] + p.predicted_probes[cell] +
                        p.secant_calls[cell] + p.secant_iterations[cell] +
                        p.secant_backtracks[cell] +
                        p.exhausted_failures[cell];
                    if (total == 0) {
                        continue;
                    }
                    std::fprintf(
                        stderr,
                        "DE-CUDA asphere UC5 cell: evaluation=%s "
                        "build=%s wave=%s anchor=%d entered=%llu "
                        "mirrored=%llu skipped_after_failure=%llu "
                        "cached_accepted=%llu cached_traceable=%llu "
                        "base_probes=%llu predicted_probes=%llu "
                        "secant_calls=%llu secant_iterations=%llu "
                        "secant_backtracks=%llu exhausted_failures=%llu\n",
                        evaluation_phase,
                        build_kind == 0 ? "initial" : "repair",
                        wave_role == 0 ? "primary" : "secondary",
                        anchor,
                        p.anchor_entered[cell],
                        p.anchor_mirrored[cell],
                        p.anchor_skipped_after_failure[cell],
                        p.cached_accepted[cell],
                        p.cached_traceable[cell],
                        p.base_probes[cell],
                        p.predicted_probes[cell],
                        p.secant_calls[cell],
                        p.secant_iterations[cell],
                        p.secant_backtracks[cell],
                        p.exhausted_failures[cell]);
                }
            }
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
