#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"

namespace optiland {
namespace {
__device__ double d_clamp(double value, double lo, double hi) {
    return fmin(fmax(value, lo), hi);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_finite(double value) {
    return isfinite(value) != 0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_asphere_root_in_slope_domain(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double root) {
    const double radius_squared = surface.slope_root_domain_radius_squared;
    if (!d_finite(radius_squared) || radius_squared >= 0.5 * kDeviceInf) {
        return true;
    }
    if (!d_finite(root)) {
        return false;
    }
    const double x = px + root * lx;
    const double y = py + root * my;
    const double hit_radius_squared = x * x + y * y;
    return d_finite(hit_radius_squared) &&
           hit_radius_squared <= radius_squared * (1.0 + 2.0e-12);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_experimental_asphere_radial_slope(
    const DeviceSurface& surface,
    double radius) {
    double radial_slope = 0.0;
    if (d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) < 0.5 * kDeviceInf) {
        const double root_arg =
            1.0 - (1.0 + surface.conic) * radius * radius /
                      (surface.radius * surface.radius);
        if (!(root_arg > 0.0)) {
            return NAN;
        }
        radial_slope += radius / (surface.radius * sqrt(root_arg));
    }
    double radial_power = radius;
    for (int i = 0;
         i < surface.coeff_count && i < kMaxDeviceAsphereCoefficients;
         ++i) {
        radial_slope +=
            2.0 * static_cast<double>(i + 1) *
            surface.coeffs[i] * radial_power;
        radial_power *= radius * radius;
    }
    return radial_slope;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_asphere_slope_margin(
    const DeviceSurface& surface,
    double radius,
    double max_slope) {
    const double slope = d_experimental_asphere_radial_slope(surface, radius);
    return d_finite(slope) ? max_slope - fabs(slope) : -INFINITY;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// Outward-rounded enclosure of dsag/dr on a non-negative radial interval.
// The even-polynomial derivative is evaluated as r * q(r^2) with interval
// Horner, preserving coefficient cancellation much more tightly than a sum
// of absolute term bounds.  The conic derivative is monotone before its real
// boundary, so its endpoint values enclose that contribution.
__device__ bool d_asphere_radial_slope_interval(
    const DeviceSurface& surface,
    double radius_lo,
    double radius_hi,
    double* slope_lo,
    double* slope_hi) {
    if (!slope_lo || !slope_hi || !d_finite(radius_lo) ||
        !d_finite(radius_hi) || radius_lo < 0.0 ||
        radius_hi < radius_lo) {
        return false;
    }
    double conic_lo = 0.0;
    double conic_hi = 0.0;
    if (d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) < 0.5 * kDeviceInf) {
        const double radius_squared = surface.radius * surface.radius;
        const double root_arg_lo =
            1.0 - (1.0 + surface.conic) * radius_lo * radius_lo /
                      radius_squared;
        const double root_arg_hi =
            1.0 - (1.0 + surface.conic) * radius_hi * radius_hi /
                      radius_squared;
        if (!(root_arg_lo > 0.0) || !(root_arg_hi > 0.0) ||
            !d_finite(root_arg_lo) || !d_finite(root_arg_hi)) {
            return false;
        }
        const double endpoint_lo =
            radius_lo / (surface.radius * sqrt(root_arg_lo));
        const double endpoint_hi =
            radius_hi / (surface.radius * sqrt(root_arg_hi));
        if (!d_finite(endpoint_lo) || !d_finite(endpoint_hi)) {
            return false;
        }
        conic_lo = nextafter(fmin(endpoint_lo, endpoint_hi), -DBL_MAX);
        conic_hi = nextafter(fmax(endpoint_lo, endpoint_hi), DBL_MAX);
    }
    const double u_lo = nextafter(radius_lo * radius_lo, -DBL_MAX);
    const double u_hi = nextafter(radius_hi * radius_hi, DBL_MAX);
    if (!d_finite(u_lo) || !d_finite(u_hi)) {
        return false;
    }
    double polynomial_lo = 0.0;
    double polynomial_hi = 0.0;
    const int count = max(
        0, min(surface.coeff_count, kMaxDeviceAsphereCoefficients));
    for (int i = count - 1; i >= 0; --i) {
        if (!d_finite(surface.coeffs[i])) {
            return false;
        }
        const double p0 = polynomial_lo * u_lo;
        const double p1 = polynomial_lo * u_hi;
        const double p2 = polynomial_hi * u_lo;
        const double p3 = polynomial_hi * u_hi;
        if (!d_finite(p0) || !d_finite(p1) ||
            !d_finite(p2) || !d_finite(p3)) {
            return false;
        }
        const double coefficient =
            2.0 * static_cast<double>(i + 1) * surface.coeffs[i];
        const double product_lo = nextafter(
            fmin(fmin(p0, p1), fmin(p2, p3)), -DBL_MAX);
        const double product_hi = nextafter(
            fmax(fmax(p0, p1), fmax(p2, p3)), DBL_MAX);
        polynomial_lo = nextafter(product_lo + coefficient, -DBL_MAX);
        polynomial_hi = nextafter(product_hi + coefficient, DBL_MAX);
    }
    const double p0 = polynomial_lo * radius_lo;
    const double p1 = polynomial_lo * radius_hi;
    const double p2 = polynomial_hi * radius_lo;
    const double p3 = polynomial_hi * radius_hi;
    if (!d_finite(p0) || !d_finite(p1) ||
        !d_finite(p2) || !d_finite(p3)) {
        return false;
    }
    const double bounded_polynomial_lo = nextafter(
        fmin(fmin(p0, p1), fmin(p2, p3)), -DBL_MAX);
    const double bounded_polynomial_hi = nextafter(
        fmax(fmax(p0, p1), fmax(p2, p3)), DBL_MAX);
    *slope_lo = nextafter(
        conic_lo + bounded_polynomial_lo, -DBL_MAX);
    *slope_hi = nextafter(
        conic_hi + bounded_polynomial_hi, DBL_MAX);
    return d_finite(*slope_lo) && d_finite(*slope_hi);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_asphere_slope_interval_below_limit(
    const DeviceSurface& surface,
    double radius_lo,
    double radius_hi,
    double max_slope) {
    double slope_lo = 0.0;
    double slope_hi = 0.0;
    if (!d_asphere_radial_slope_interval(
            surface, radius_lo, radius_hi, &slope_lo, &slope_hi)) {
        return false;
    }
    const double guard =
        512.0 * DBL_EPSILON * fmax(1.0, max_slope);
    return slope_lo > -max_slope + guard &&
           slope_hi < max_slope - guard;
}
} // namespace
} // namespace optiland
