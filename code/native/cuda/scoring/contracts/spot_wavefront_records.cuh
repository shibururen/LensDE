#pragma once
#include "../prelude.cuh"
#include "constants.cuh"
#include "device_config.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../merit/residual_primitives.cuh"

namespace optiland {
namespace {
struct DeviceFieldSpotStats {
    double rms_radius = NAN;
    double max_radius = NAN;
    double worst_wavelength_rms_radius = NAN;
    double spectral_rms_spread = NAN;
    double centroid_x = NAN;
    double centroid_y = NAN;
    double total_weight = 0.0;
    double valid_weight = 0.0;
    double failed_weight = 0.0;
    double sampling_rejected_weight = 0.0;
    double trace_progress = 0.0;
    double chief_progress = 1.0;
    double trace_penalty = 0.0;
    double aim_penalty = 0.0;
    double backward_weight = 0.0;
    double single_ray_spot_guard_failure_squared = 0.0;
    double single_ray_guard_surface_order_edge_deficit = 0.0;
    double min_pupil_order_jacobian = NAN;
    double pupil_order_worst_surface_progress = NAN;
    double pupil_order_prior_violation = 0.0;
    double working_na_squared_sum = 0.0;
    double relative_illumination_proxy_area = NAN;
    int working_valid_marginal_rays = 0;
    int valid_rays = 0;
    int failed_rays = 0;
    int sampling_rejected_rays = 0;
    int attempted_rays = 0;
    int chief_ok = 1;
    int axis_cross_count = 0;
    int axis_cross_excess = 0;
    int center_early_focus = 0;
    int chief_entry_side_failed = 0;
    int structural_failure = 0;
    int pupil_order_fold = 0;
    int pupil_order_hard_fold = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceSpotRaySample {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double l = 0.0;
    double m = 0.0;
    double n = 1.0;
    double opd = 0.0;
    double initial_x = 0.0;
    double initial_y = 0.0;
    double initial_z = 0.0;
    double initial_l = 0.0;
    double initial_m = 0.0;
    double initial_n = 1.0;
    double weight = 0.0;
    double intensity = 0.0;
    double wavefront_weight = 0.0;
    double weighted_progress = 0.0;
    double trace_penalty = 0.0;
    double aim_penalty = 0.0;
    double pupil_x = 0.0;
    double pupil_y = 0.0;
    double stop_x = NAN;
    double stop_y = NAN;
    double first_cross_z = NAN;
    double first_cross_z_y = NAN;
    double min_backward_air_gap = 0.0;
    double min_backward_glass_gap = 0.0;
    int normal_aperture_blockage = 0;
    int usable = 0;
    int trace_valid = 0;
    int aim_usable = 0;
    int backward = 0;
    int stop_reached = 0;
    int failure_code = 0;
    int failure_surface = -1;
    int axis_cross_count = 0;
    int has_axis_cross = 0;
    int axis_cross_count_y = 0;
    int has_axis_cross_y = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_calibrated_paraxial_stop_sampling_rejection(
    const DeviceSpotRaySample& sample,
    const DeviceConfig& cfg,
    int stop_surface_index) {
    if (sample.normal_aperture_blockage) return true;
    return cfg.aiming_mode == 5 &&
           stop_surface_index >= 0 &&
           sample.aim_usable != 0 &&
           sample.backward == 0 &&
           sample.failure_code == 3 &&
           sample.failure_surface == stop_surface_index &&
           sample.stop_reached != 0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_wavefront_sample_weight(
    const DeviceSpotRaySample& sample) {
    return d_finite(sample.wavefront_weight) && sample.wavefront_weight > 0.0
               ? sample.wavefront_weight
               : 0.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <typename Sample>
__device__ double d_backward_surface_order_edge_deficit(
    const Sample& sample, const DeviceConfig& cfg) {
    const double air = sample.min_backward_air_gap < -1.0e-6
        ? fmax(0.0, cfg.air_edge_lower) - sample.min_backward_air_gap : 0.0;
    const double glass = sample.min_backward_glass_gap < -1.0e-6
        ? fmax(0.0, cfg.glass_edge_lower) - sample.min_backward_glass_gap : 0.0;
    return fmax(air, glass);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// A SurfaceMiss at the next ordered surface can still expose a real geometry
// violation: the missed surface lies behind the ray's current point at the
// same transverse coordinate.  Keep negative propagation rejected, but turn
// that virtual overlap into the same lower-edge deficit used by the ordinary
// manufacturing constraints.
__device__ double d_guard_surface_order_edge_deficit(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceSpotRaySample& sample) {
    const double traced_deficit = d_backward_surface_order_edge_deficit(sample, cfg);
    if (!surfaces || sample.failure_code != 2 ||
        sample.failure_surface <= cfg.first_optical_surface ||
        sample.failure_surface >= cfg.surface_count ||
        !d_finite(sample.x) || !d_finite(sample.y) || !d_finite(sample.z)) {
        return traced_deficit;
    }
    const int missed_index = sample.failure_surface;
    const DeviceSurface& missed = surfaces[missed_index];
    if (missed.kind == kKindObject || missed.kind == kKindImage) {
        return traced_deficit;
    }
    const double sag = d_sag(missed, hypot(sample.x, sample.y));
    if (!d_finite(sag)) {
        return traced_deficit;
    }
    const double missed_z =
        d_surface_vertex_z(surfaces, cfg.surface_count, missed_index) + sag;
    const double virtual_edge = missed_z - sample.z;
    // Match the CPU guard contract: a sub-nanometre difference between the
    // failed-ray intercept and the independently evaluated missed-surface sag
    // is root-refinement noise, not a physical surface-order overlap.  Without
    // this dead band, a ~1e-7 mm residue becomes a complete 0.7 mm glass-edge
    // deficit and is amplified by the 0.001 mm constraint tolerance.
    constexpr double kSurfaceOrderOverlapToleranceMm = 1.0e-6;
    if (!d_finite(virtual_edge) ||
        virtual_edge >= -kSurfaceOrderOverlapToleranceMm) {
        return traced_deficit;
    }
    const DeviceSurface& preceding = surfaces[missed_index - 1];
    const double required_edge = d_interval_is_glass(preceding)
                                     ? cfg.glass_edge_lower
                                     : cfg.air_edge_lower;
    return fmax(traced_deficit, required_edge - virtual_edge);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceChiefRaySample {
    double wave_weight = 0.0;
    double weighted_progress = 0.0;
    double first_surface_axis = NAN;
    double input_axis = NAN;
    double image_x = NAN;
    double image_y = NAN;
    double image_z = NAN;
    double image_l = NAN;
    double image_m = NAN;
    double image_n = NAN;
    double vignetted_cra_l = NAN;
    double vignetted_cra_m = NAN;
    double vignetted_cra_n = NAN;
    double opd = NAN;
    double initial_x = NAN;
    double initial_y = NAN;
    double initial_z = NAN;
    double initial_l = NAN;
    double initial_m = NAN;
    double initial_n = NAN;
    double input_component = NAN;
    int usable = 1;
    int axis_cross_count = 0;
    int first_surface_valid = 0;
    int traced = 0;
    int valid = 0;
    int vignetted_cra_traced = 0;
    int vignetted_cra_valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceFieldWavefrontStats {
    double rms_waves = NAN;
    double max_abs_waves = NAN;
    double coverage = 0.0;
    int chief_ok = 1;
};
} // namespace
} // namespace optiland
