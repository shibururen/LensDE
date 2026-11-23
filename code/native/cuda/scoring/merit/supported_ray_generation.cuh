#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../ray/calibrated_generation.cuh"
#include "../ray/paraxial_generation.cuh"

namespace optiland {
namespace {
struct DeviceMeritTerms {
    double image_sum = 0.0;
    double image_weight = 0.0;
    double trace_failure_type_severity = 0.0;
    double guard_surface_order_edge_deficit_squared = 0.0;
    double relative_illumination_constraint_squared = 0.0;
    double vignetting_geometry_constraint_squared = 0.0;
    double vignetting_boundary_support_constraint_squared = 0.0;
    double vignetting_soft_prior_penalty = 0.0;
    double pupil_order_soft_prior_penalty = 0.0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceRay d_generate_supported_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double pupil_x,
    double pupil_y,
    double pupil_weight,
    double wavelength_um,
    const DeviceCalibratedModel* calibrated_model = nullptr,
    bool predict_image_guard = false) {
    if (cfg.aiming_mode == 1) {
        return d_generate_paraxial_ray(
            surfaces,
            cfg,
            field,
            pupil_x,
            pupil_y,
            pupil_weight,
            wavelength_um);
    }
    if (cfg.aiming_mode == 5) {
        return d_generate_calibrated_paraxial_ray(
            surfaces,
            cfg,
            field,
            pupil_x,
            pupil_y,
            pupil_weight,
            wavelength_um,
            calibrated_model, predict_image_guard);
    }
    DeviceRay failed;
    failed.nominal_weight = fmax(0.0, pupil_weight);
    failed.intensity = 0.0;
    failed.wavelength_um = wavelength_um;
    d_mark_ray_failure(&failed, 1, -1, 1.0);
    return failed;
}
} // namespace
} // namespace optiland
