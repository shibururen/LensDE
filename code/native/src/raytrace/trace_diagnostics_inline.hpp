#pragma once
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline void mark_ray_failure(
    Ray& ray,
    RayFailureCode code,
    int surface ,
    double severity ,
    bool invalidate ) {
    if (code != RayFailureCode::AimNotConverged ||
        ray.failure_code == RayFailureCode::None) {
        ray.failure_code = code;
        ray.failure_surface = surface;
        ray.failure_severity = std::isfinite(severity) ? std::max(0.0, severity) : 1.0;
    }
    if (invalidate) {
        ray.valid = false;
        ray.intensity = 0.0;
    }
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline void record_backward_surface_overlap(
    Ray& ray,
    const LensSystem& system,
    std::size_t surface_index,
    double vertex_z,
    double x,
    double y,
    double z) {
    if (surface_index <= system.first_optical_surface() ||
        ray.aperture_blocked) {
        return;
    }
    const Surface& previous = system.surfaces[surface_index - 1];
    const Surface& next = system.surfaces[surface_index];
    // Signed distances after a fold and the image-space intercept are not
    // overlaps between consecutive forward lens faces.
    if (previous.kind == SurfaceKind::Object || previous.reflective ||
        previous.thickness < 0.0 || next.kind == SurfaceKind::Object ||
        next.kind == SurfaceKind::Image) {
        return;
    }
    const double gap = vertex_z + surface_sag(next, x, y) - z;
    if (!std::isfinite(gap) || gap >= -1.0e-6) {
        return;
    }
    double& minimum = previous.material_after.refractive_index(0.5875618) > 1.0005
                          ? ray.min_backward_glass_gap
                          : ray.min_backward_air_gap;
    minimum = std::min(minimum, gap);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline void mark_backward_propagation_warning(
    Ray& ray,
    int surface,
    double severity) {
    const double clean_severity = std::isfinite(severity) ? std::max(0.0, severity) : 1.0;
    if (!ray.backward_propagation || clean_severity > ray.backward_severity) {
        ray.backward_propagation = true;
        ray.backward_surface = surface;
        ray.backward_severity = clean_severity;
    }
}
} // namespace raytrace_detail

} // namespace optiland
