#pragma once
#include "../prelude.cuh"
#include "../geometry/sag_residual.cuh"

namespace optiland::de_cuda_detail {
namespace {
struct DeviceScalarInterval {
    double lo = 0.0;
    double hi = 0.0;
    bool valid = false;
};
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval outward_interval(
    double lo,
    double hi) {
    if (isnan(lo) || isnan(hi)) {
        return {};
    }
    if (lo > hi) {
        const double swap = lo;
        lo = hi;
        hi = swap;
    }
    if (finite_value(lo)) {
        lo = nextafter(lo, -DBL_MAX);
    }
    if (finite_value(hi)) {
        hi = nextafter(hi, DBL_MAX);
    }
    return {lo, hi, true};
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval point_interval(double value) {
    return outward_interval(value, value);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval whole_interval() {
    return {-INFINITY, INFINITY, true};
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval interval_add(
    const DeviceScalarInterval& a,
    const DeviceScalarInterval& b) {
    if (!a.valid || !b.valid) {
        return {};
    }
    return outward_interval(a.lo + b.lo, a.hi + b.hi);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval interval_subtract(
    const DeviceScalarInterval& a,
    const DeviceScalarInterval& b) {
    if (!a.valid || !b.valid) {
        return {};
    }
    return outward_interval(a.lo - b.hi, a.hi - b.lo);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval interval_multiply(
    const DeviceScalarInterval& a,
    const DeviceScalarInterval& b) {
    if (!a.valid || !b.valid) {
        return {};
    }
    if (!finite_value(a.lo) || !finite_value(a.hi) ||
        !finite_value(b.lo) || !finite_value(b.hi)) {
        if ((a.lo == 0.0 && a.hi == 0.0) ||
            (b.lo == 0.0 && b.hi == 0.0)) {
            return point_interval(0.0);
        }
        return whole_interval();
    }
    const double p0 = a.lo * b.lo;
    const double p1 = a.lo * b.hi;
    const double p2 = a.hi * b.lo;
    const double p3 = a.hi * b.hi;
    return outward_interval(
        fmin(fmin(p0, p1), fmin(p2, p3)),
        fmax(fmax(p0, p1), fmax(p2, p3)));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval interval_scale(
    const DeviceScalarInterval& value,
    double scale) {
    if (!value.valid || isnan(scale)) {
        return {};
    }
    if (!finite_value(scale)) {
        return whole_interval();
    }
    return scale >= 0.0
               ? outward_interval(value.lo * scale, value.hi * scale)
               : outward_interval(value.hi * scale, value.lo * scale);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval interval_divide(
    const DeviceScalarInterval& numerator,
    const DeviceScalarInterval& denominator) {
    if (!numerator.valid || !denominator.valid) {
        return {};
    }
    if (denominator.lo <= 0.0 && denominator.hi >= 0.0) {
        return whole_interval();
    }
    return interval_multiply(
        numerator,
        outward_interval(1.0 / denominator.hi, 1.0 / denominator.lo));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval interval_square(
    const DeviceScalarInterval& value) {
    if (!value.valid) {
        return {};
    }
    if (!finite_value(value.lo) || !finite_value(value.hi)) {
        return outward_interval(0.0, INFINITY);
    }
    if (value.lo <= 0.0 && value.hi >= 0.0) {
        return outward_interval(
            0.0, fmax(value.lo * value.lo, value.hi * value.hi));
    }
    const double lo2 = value.lo * value.lo;
    const double hi2 = value.hi * value.hi;
    return outward_interval(fmin(lo2, hi2), fmax(lo2, hi2));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval interval_sqrt(
    const DeviceScalarInterval& value) {
    if (!value.valid || value.hi < 0.0) {
        return {};
    }
    return outward_interval(
        sqrt(fmax(0.0, value.lo)), sqrt(fmax(0.0, value.hi)));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval affine_interval(
    double origin,
    double direction,
    double lo,
    double hi) {
    return outward_interval(origin + direction * lo, origin + direction * hi);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval radial_squared_at_interval(
    double px,
    double py,
    double lx,
    double my,
    double t) {
    const DeviceScalarInterval x = affine_interval(px, lx, t, t);
    const DeviceScalarInterval y = affine_interval(py, my, t, t);
    return interval_add(interval_square(x), interval_square(y));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ DeviceScalarInterval
radial_squared_derivative_interval(
    double px,
    double py,
    double lx,
    double my,
    double lo,
    double hi) {
    const DeviceScalarInterval x = affine_interval(px, lx, lo, hi);
    const DeviceScalarInterval y = affine_interval(py, my, lo, hi);
    return interval_scale(
        interval_add(interval_scale(x, lx), interval_scale(y, my)), 2.0);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
// For a rotationally symmetric surface the ray radius is governed by the
// scalar quadratic q(t) = r(t)^2.  Bounding that quadratic directly is both
// tighter and cheaper than independently intervalizing x(t) and y(t), whose
// dependency otherwise makes the certified fallback subdivide unnecessarily.
__device__ __forceinline__ DeviceScalarInterval radial_squared_interval(
    double px,
    double py,
    double lx,
    double my,
    double lo,
    double hi) {
    const DeviceScalarInterval at_lo =
        radial_squared_at_interval(px, py, lx, my, lo);
    const DeviceScalarInterval at_hi =
        radial_squared_at_interval(px, py, lx, my, hi);
    const DeviceScalarInterval q_prime =
        radial_squared_derivative_interval(px, py, lx, my, lo, hi);
    if (!at_lo.valid || !at_hi.valid || !q_prime.valid) {
        return {};
    }

    double minimum = 0.0;
    if (q_prime.lo > 0.0) {
        minimum = at_lo.lo;
    } else if (q_prime.hi < 0.0) {
        minimum = at_hi.lo;
    } else {
        // The interval contains the closest point on the transverse ray.
        // In two dimensions its exact squared distance from the axis is
        // (p x d)^2 / |d|^2.  Intervalizing that expression avoids using a
        // rounded vertex t as a potentially unsafe lower bound.
        const DeviceScalarInterval cross = interval_subtract(
            interval_multiply(point_interval(px), point_interval(my)),
            interval_multiply(point_interval(py), point_interval(lx)));
        const DeviceScalarInterval speed_squared = interval_add(
            interval_square(point_interval(lx)),
            interval_square(point_interval(my)));
        const DeviceScalarInterval closest = interval_divide(
            interval_square(cross), speed_squared);
        minimum = closest.valid ? closest.lo : 0.0;
    }
    const double maximum = fmax(at_lo.hi, at_hi.hi);
    return outward_interval(fmax(0.0, minimum), fmax(0.0, maximum));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
struct DeviceRadialSquaredDerivativeInterval {
    DeviceScalarInterval radial_squared{};
    DeviceScalarInterval radial_derivative{};
};
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
// UC10: preserve radial_squared_interval's exact operation order while also
// returning the q'(t) interval that it already constructs for the minimum
// classification.  The fused strict-node evaluator can then reuse q'(t)
// instead of rebuilding the same affine x/y intervals a second time.
__device__ __forceinline__ DeviceRadialSquaredDerivativeInterval
radial_squared_and_derivative_interval(
    double px,
    double py,
    double lx,
    double my,
    double lo,
    double hi) {
    DeviceRadialSquaredDerivativeInterval result{};
    const DeviceScalarInterval at_lo =
        radial_squared_at_interval(px, py, lx, my, lo);
    const DeviceScalarInterval at_hi =
        radial_squared_at_interval(px, py, lx, my, hi);
    result.radial_derivative =
        radial_squared_derivative_interval(px, py, lx, my, lo, hi);
    if (!at_lo.valid || !at_hi.valid || !result.radial_derivative.valid) {
        return result;
    }

    double minimum = 0.0;
    if (result.radial_derivative.lo > 0.0) {
        minimum = at_lo.lo;
    } else if (result.radial_derivative.hi < 0.0) {
        minimum = at_hi.lo;
    } else {
        const DeviceScalarInterval cross = interval_subtract(
            interval_multiply(point_interval(px), point_interval(my)),
            interval_multiply(point_interval(py), point_interval(lx)));
        const DeviceScalarInterval speed_squared = interval_add(
            interval_square(point_interval(lx)),
            interval_square(point_interval(my)));
        const DeviceScalarInterval closest = interval_divide(
            interval_square(cross), speed_squared);
        minimum = closest.valid ? closest.lo : 0.0;
    }
    const double maximum = fmax(at_lo.hi, at_hi.hi);
    result.radial_squared =
        outward_interval(fmax(0.0, minimum), fmax(0.0, maximum));
    return result;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
// UC12: radial_squared_at_interval(lo/hi) and
// radial_squared_derivative_interval(lo, hi) use the same four affine x/y
// endpoint values.  Materialize each scalar endpoint once, then reconstruct
// the exact point and span intervals with the same outward_interval calls.
__device__ __forceinline__ DeviceRadialSquaredDerivativeInterval
radial_squared_and_derivative_interval_affine_reuse(
    double px,
    double py,
    double lx,
    double my,
    double lo,
    double hi) {
    DeviceRadialSquaredDerivativeInterval result{};
    const double x_at_lo = px + lx * lo;
    const double y_at_lo = py + my * lo;
    const double x_at_hi = px + lx * hi;
    const double y_at_hi = py + my * hi;

    const DeviceScalarInterval x_lo =
        outward_interval(x_at_lo, x_at_lo);
    const DeviceScalarInterval y_lo =
        outward_interval(y_at_lo, y_at_lo);
    const DeviceScalarInterval at_lo =
        interval_add(interval_square(x_lo), interval_square(y_lo));
    const DeviceScalarInterval x_hi =
        outward_interval(x_at_hi, x_at_hi);
    const DeviceScalarInterval y_hi =
        outward_interval(y_at_hi, y_at_hi);
    const DeviceScalarInterval at_hi =
        interval_add(interval_square(x_hi), interval_square(y_hi));

    const DeviceScalarInterval x =
        outward_interval(x_at_lo, x_at_hi);
    const DeviceScalarInterval y =
        outward_interval(y_at_lo, y_at_hi);
    result.radial_derivative = interval_scale(
        interval_add(interval_scale(x, lx), interval_scale(y, my)), 2.0);
    if (!at_lo.valid || !at_hi.valid || !result.radial_derivative.valid) {
        return result;
    }

    double minimum = 0.0;
    if (result.radial_derivative.lo > 0.0) {
        minimum = at_lo.lo;
    } else if (result.radial_derivative.hi < 0.0) {
        minimum = at_hi.lo;
    } else {
        const DeviceScalarInterval cross = interval_subtract(
            interval_multiply(point_interval(px), point_interval(my)),
            interval_multiply(point_interval(py), point_interval(lx)));
        const DeviceScalarInterval speed_squared = interval_add(
            interval_square(point_interval(lx)),
            interval_square(point_interval(my)));
        const DeviceScalarInterval closest = interval_divide(
            interval_square(cross), speed_squared);
        minimum = closest.valid ? closest.lo : 0.0;
    }
    const double maximum = fmax(at_lo.hi, at_hi.hi);
    result.radial_squared =
        outward_interval(fmax(0.0, minimum), fmax(0.0, maximum));
    return result;
}
} // namespace
} // namespace optiland::de_cuda_detail
