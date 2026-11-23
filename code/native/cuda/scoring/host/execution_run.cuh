#pragma once
#include "../prelude.cuh"
#include "execution_context.cuh"

namespace optiland {
bool PopulationScoringExecution::run() {
    if (const auto result = validate_inputs(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = prepare_host_configuration(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = prepare_device_context(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = allocate_device_buffers(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = upload_inputs(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = launch_kernels(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = readback_results(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = report_diagnostics(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = release_device_resources(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    if (const auto result = finish_call(); result.flow == native_detail::PhaseFlow::Return) {
        return result.value;
    }
    return true;
}
} // namespace optiland
