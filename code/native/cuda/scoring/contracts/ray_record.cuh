#pragma once
#include "../prelude.cuh"
#include "constants.cuh"
#include "device_config.cuh"

namespace optiland {
namespace {
struct DeviceRay {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double l = 0.0;
    double m = 0.0;
    double n = 1.0;
    double nominal_weight = 1.0;
    double intensity = 1.0;
    double wavelength_um = 0.5875618;
    double opd = 0.0;
    double aim_residual = 0.0;
    double aim_target_radius = 1.0;
    double failure_severity = 0.0;
    double backward_severity = 0.0;
    double min_backward_air_gap = 0.0;
    double min_backward_glass_gap = 0.0;
    int valid = 1;
    int aperture_blocked = 0;
    int aperture_blocked_surface = -1;
    double edge_prediction_scale = 1.0;
    int aim_converged = 1;
    int failure_code = 0;
    int failure_surface = -1;
    int backward_propagation = 0;
    int backward_surface = -1;
    double stop_x = NAN;
    double stop_y = NAN;
    int stop_reached = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_defer_secondary_apertures(const DeviceConfig& cfg, double wave) {
    return cfg.primary_wavelength_apertures && fabs(wave - cfg.primary_wavelength) > 1.0e-10;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_spectral_surface_radius(const DeviceSurface& surface) {
    if (surface.kind == kKindObject) return 0.0;
    if (surface.aperture_radius > 0.0) return surface.aperture_radius;
    if (surface.kind == kKindImage && !surface.clear_aperture_fixed) return 0.0;
    return surface.semi_aperture;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_normal_secondary_blockage(const DeviceRay& ray) {
    if (!ray.aperture_blocked || !ray.aim_converged ||
        ray.aperture_blocked_surface < 0) {
        return false;
    }
    // A secondary ray continues geometrically after its first physical mask
    // hit.  Failures occurring later in that synthetic continuation are not
    // observable and therefore cannot re-enter trace or image penalties.
    if (!ray.valid &&
        (ray.failure_surface < 0 ||
         ray.failure_surface <= ray.aperture_blocked_surface)) {
        return false;
    }
    if (ray.backward_propagation &&
        (ray.backward_surface < 0 ||
         ray.backward_surface <= ray.aperture_blocked_surface)) {
        return false;
    }
    return true;
}
} // namespace
} // namespace optiland
