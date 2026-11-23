#pragma once
#include "../prelude.cuh"
#include "sag_residual.cuh"

namespace optiland::de_cuda_detail {
namespace {
__device__ bool stop_surface_forward_root_interval(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double* lower,
    double* upper) {
    if (!lower || !upper || !surface.is_stop ||
        !(surface.aperture_radius > 0.0) ||
        !finite_value(surface.aperture_radius)) {
        return false;
    }

    const double radius = 1.01 * surface.aperture_radius;
    const double a = fma(lx, lx, my * my);
    const double b = 2.0 * fma(px, lx, py * my);
    const double c = fma(px, px, py * py) - radius * radius;
    const double radial_scale =
        fmax(1.0, fmax(radius * radius, fma(px, px, py * py)));
    const double radial_tol =
        128.0 * 2.2204460492503131e-16 * radial_scale;
    if (a <= kDeviceEps * kDeviceEps) {
        if (c > radial_tol) {
            return false;
        }
        *lower = 0.0;
        *upper = INFINITY;
        return true;
    }

    double discriminant = fma(-4.0 * a, c, b * b);
    const double discriminant_scale =
        fmax(1.0, fmax(b * b, fabs(4.0 * a * c)));
    const double discriminant_tol =
        128.0 * 2.2204460492503131e-16 * discriminant_scale;
    if (!finite_value(discriminant) || discriminant < -discriminant_tol) {
        return false;
    }
    discriminant = fmax(0.0, discriminant);
    const double root = sqrt(discriminant);
    const double q = -0.5 * (b + copysign(root, b));
    const double q_tol =
        128.0 * 2.2204460492503131e-16 * fmax(1.0, fabs(b) + root);
    double domain_lo = 0.0;
    double domain_hi = 0.0;
    if (fabs(q) <= q_tol) {
        domain_lo = domain_hi = -b / (2.0 * a);
    } else {
        domain_lo = q / a;
        domain_hi = c / q;
        if (domain_hi < domain_lo) {
            const double swap = domain_lo;
            domain_lo = domain_hi;
            domain_hi = swap;
        }
    }
    if (domain_hi < -1.0e-10) {
        return false;
    }
    *lower = fmax(0.0, domain_lo);
    *upper = fmax(*lower, domain_hi);
    return finite_value(*lower) && finite_value(*upper);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ bool asphere_forward_search_interval(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double requested_lower,
    double requested_upper,
    double* lower,
    double* upper) {
    if (!lower || !upper || surface.kind != kKindEvenAsphere ||
        !finite_value(requested_lower) || !finite_value(requested_upper) ||
        requested_lower < 0.0 || requested_upper < requested_lower) {
        return false;
    }

    double lo = requested_lower;
    double hi = requested_upper;
    const double conic_term = 1.0 + surface.conic;
    if (finite_value(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) <= 0.5 * kDeviceInf && conic_term > 0.0) {
        const double radial_limit_sq =
            surface.radius * surface.radius / conic_term;
        const double transverse_speed_sq = lx * lx + my * my;
        const double radial_linear = 2.0 * (px * lx + py * my);
        const double radial_constant = px * px + py * py - radial_limit_sq;
        const double radial_scale =
            fmax(1.0, fmax(radial_limit_sq, px * px + py * py));
        const double radial_tol =
            128.0 * 2.2204460492503131e-16 * radial_scale;

        if (transverse_speed_sq <= kDeviceEps * kDeviceEps) {
            if (radial_constant > radial_tol) {
                return false;
            }
        } else {
            double discriminant =
                radial_linear * radial_linear -
                4.0 * transverse_speed_sq * radial_constant;
            const double discriminant_scale = fmax(
                1.0,
                fmax(
                    radial_linear * radial_linear,
                    fabs(4.0 * transverse_speed_sq * radial_constant)));
            const double discriminant_tol =
                128.0 * 2.2204460492503131e-16 * discriminant_scale;
            if (discriminant < -discriminant_tol) {
                return false;
            }
            discriminant = fmax(0.0, discriminant);
            const double root = sqrt(discriminant);
            double domain_lo =
                (-radial_linear - root) / (2.0 * transverse_speed_sq);
            double domain_hi =
                (-radial_linear + root) / (2.0 * transverse_speed_sq);
            if (domain_hi < domain_lo) {
                const double swap = domain_lo;
                domain_lo = domain_hi;
                domain_hi = swap;
            }
            if (domain_hi < 0.0) {
                return false;
            }
            lo = fmax(lo, fmax(0.0, domain_lo));
            hi = fmin(hi, domain_hi);
        }
    }

    if (!finite_value(lo) || !finite_value(hi) || !(hi > lo)) {
        return false;
    }
    // Stay one representable value inside the conic radical domain so a
    // round-off excursion cannot turn the fallback seed into NaN.
    hi = nextafter(hi, lo);
    if (lo > 0.0) {
        lo = nextafter(lo, hi);
    }
    if (!finite_value(lo) || !finite_value(hi) || !(hi > lo)) {
        return false;
    }
    *lower = lo;
    *upper = hi;
    return true;
}
} // namespace
} // namespace optiland::de_cuda_detail
