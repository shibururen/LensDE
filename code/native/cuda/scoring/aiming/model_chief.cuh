#pragma once
#include "../prelude.cuh"
#include "budget_chief.cuh"
#include "stop_samples.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../ray/paraxial_generation.cuh"
#include "../ray/stop_trace.cuh"

namespace optiland {
namespace {
template <
    bool kUsePrecomputedChief,
    int kEdgeCount,
    int kSafeguardedRecoveryMode,
    bool kRepairBuild,
    bool kUsePrecomputedFirstEdge
>
struct DPrepareParaxialChiefInputs {
    const DeviceField*& center_field;
    const DeviceCalibratedCenterFootprint*& center_footprint;
    const DeviceCalibratedModel*& inner_model;
    const DeviceField*& inner_field;
    int& field_index;
    const DeviceCalibratedModel*& previous_model;
    const DeviceField*& previous_field;
    const DeviceCalibratedModel*& previous2_model;
    const DeviceField*& previous2_field;
    const DeviceCalibratedModel*& same_field_wave_model;
    int& precomputed_initial_trial_start;
    const DeviceSurface*& surfaces;
    const DeviceConfig& cfg;
    const DeviceField& field;
    double& wavelength_um;
    DeviceCalibratedModel& model;
    DeviceBuilderDiagnostic*& builder_diag;
    const double& rs;
    const double& stop_scale;
    const double& chief_target_x;
    const double& chief_target_y;
    bool& chief_ok;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <
    bool kUsePrecomputedChief,
    int kEdgeCount,
    int kSafeguardedRecoveryMode,
    bool kRepairBuild,
    bool kUsePrecomputedFirstEdge,
    class Inputs
>
__device__ __forceinline__ native_detail::PhaseOutcome d_prepare_paraxial_chief(Inputs& inputs) {
    auto& center_field = inputs.center_field;
    auto& center_footprint = inputs.center_footprint;
    auto& inner_model = inputs.inner_model;
    auto& inner_field = inputs.inner_field;
    auto& field_index = inputs.field_index;
    auto& previous_model = inputs.previous_model;
    auto& previous_field = inputs.previous_field;
    auto& previous2_model = inputs.previous2_model;
    auto& previous2_field = inputs.previous2_field;
    auto& same_field_wave_model = inputs.same_field_wave_model;
    auto& precomputed_initial_trial_start = inputs.precomputed_initial_trial_start;
    auto& surfaces = inputs.surfaces;
    auto& cfg = inputs.cfg;
    auto& field = inputs.field;
    auto& wavelength_um = inputs.wavelength_um;
    auto& model = inputs.model;
    auto& builder_diag = inputs.builder_diag;
    auto& rs = inputs.rs;
    auto& stop_scale = inputs.stop_scale;
    auto& chief_target_x = inputs.chief_target_x;
    auto& chief_target_y = inputs.chief_target_y;
    auto& chief_ok = inputs.chief_ok;
    if constexpr (!kUsePrecomputedChief) {
        DeviceRay paraxial_chief = d_generate_paraxial_ray(
            surfaces,
            cfg,
            field,
            0.0,
            0.0,
            1.0,
            wavelength_um);
        double sx = 0.0;
        double sy = 0.0;
        chief_ok = d_trace_ray_to_surface(
            surfaces,
            cfg,
            paraxial_chief,
            d_stop_index(surfaces, cfg),
            &sx,
            &sy,
            false,
            false,
            kAsphereDeepRoleChief,
            kAsphereDeepSiteFarParaxial);
        if (chief_ok) {
            const double residual =
                hypot(sx - chief_target_x, sy - chief_target_y);
            const double limit = fmax(
                cfg.ray_aiming_tolerance,
                fmax(0.0, cfg.calibrated_validation_residual_stop_fraction) *
                    stop_scale);
            if (!d_finite(residual) || residual > limit) {
                chief_ok = false;
            }
        }
        if (chief_ok) {
            model.chief_seed =
                d_seed_from_ray(
                    surfaces,
                    cfg,
                    paraxial_chief,
                    kAsphereDeepRoleChief,
                    kAsphereDeepSiteFarParaxial);
            if (!model.chief_seed.valid) {
                chief_ok = false;
            }
            model.chief_stop_x = sx;
            model.chief_stop_y = sy;
            model.fit_rms = hypot(sx - chief_target_x, sy - chief_target_y);
        }
        if (builder_diag && chief_ok) ++builder_diag->paraxial_ok;
        if (!chief_ok) {
            if (builder_diag) ++builder_diag->budget_calls;
            double residual = INFINITY;
            chief_ok = d_try_calibrated_budget_chief(
                surfaces,
                cfg,
                field,
                wavelength_um,
                rs,
                center_footprint && center_footprint->valid
                    ? &center_footprint->chief_seed
                    : nullptr,
                center_field,
                inner_model && inner_model->valid
                    ? &inner_model->chief_seed
                    : nullptr,
                inner_field ? hypot(inner_field->hx, inner_field->hy) : 0.0,
                field_index,
                previous_model && previous_model->valid
                    ? &previous_model->chief_seed
                    : nullptr,
                previous_field,
                previous2_model && previous2_model->valid
                    ? &previous2_model->chief_seed
                    : nullptr,
                previous2_field,
                same_field_wave_model && same_field_wave_model->valid
                    ? &same_field_wave_model->chief_seed
                    : nullptr,
                precomputed_initial_trial_start,
                &model.chief_seed,
                &model.chief_stop_x,
                &model.chief_stop_y,
                &residual, builder_diag);
            model.fit_rms = residual;
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace
} // namespace optiland
