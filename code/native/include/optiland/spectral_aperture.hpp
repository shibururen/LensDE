#pragma once

#include <cmath>

#ifdef __CUDACC__
#define OPTILAND_SPECTRAL_HD __host__ __device__
#else
#define OPTILAND_SPECTRAL_HD
#endif

namespace optiland {

// Numerical tolerance only, not the legacy 1% engineering aperture margin.
OPTILAND_SPECTRAL_HD inline double spectral_aperture_limit(double radius) {
    return radius + 1.0e-9 * (radius > 1.0 ? radius : 1.0);
}

// Intersect the existing CP first-surface seed C + t*V with the aperture.
// Keep the original point unless it is predicted outside. No ray tracing,
// boundary search, or outward extrapolation is performed here.
OPTILAND_SPECTRAL_HD inline double spectral_edge_scale(
    double cx, double cy, double vx, double vy, double radius) {
    if (!(radius > 0.0)) return 1.0;
    const double ex = cx + vx;
    const double ey = cy + vy;
    const double limit = spectral_aperture_limit(radius);
    if (ex * ex + ey * ey <= limit * limit) return 1.0;
    const double a = vx * vx + vy * vy;
    const double b = cx * vx + cy * vy;
    const double c = cx * cx + cy * cy - radius * radius;
    // An outside center does not define a connected retained pupil here.
    if (!(a > 0.0) || !(c < 0.0)) return 1.0;
    const double disc = b * b - a * c;
    if (!(disc >= 0.0)) return 1.0;
    const double root = sqrt(disc);
    const double t = b >= 0.0 ? -c / (b + root) : (-b + root) / a;
    return t > 0.0 && t < 1.0 ? t * (1.0 - 1.0e-6) : 1.0;
}

} // namespace optiland

#undef OPTILAND_SPECTRAL_HD
