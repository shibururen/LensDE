#pragma once
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline double spectral_surface_radius(const Surface& surface) {
    if (surface.kind == SurfaceKind::Object) return 0.0;
    if (surface.aperture_radius > 0.0) return surface.aperture_radius;
    if (surface.kind == SurfaceKind::Image && !surface.clear_aperture_fixed) return 0.0;
    return surface.semi_aperture;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline void record_secondary_aperture_hit(
    Ray& ray, const Surface& surface, int index, double x, double y) {
    const double radius = spectral_surface_radius(surface);
    if (!(radius > 0.0)) return;
    // Match the established sequential-trace engineering tolerance. Without
    // this, a secondary ray only a few parts per thousand beyond the primary
    // envelope disappears discontinuously although the ordinary tracer keeps
    // the same ray inside its 1% aperture tolerance.
    const double limit = 1.01 * radius;
    if (x * x + y * y > limit * limit) {
        ray.aperture_blocked = true;
        if (ray.aperture_blocked_surface < 0) ray.aperture_blocked_surface = index;
    }
}
} // namespace raytrace_detail

} // namespace optiland
