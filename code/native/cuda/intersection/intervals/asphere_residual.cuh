#pragma once
#include "../prelude.cuh"
#include "../geometry/sag_residual.cuh"
#include "radial_geometry.cuh"
#include "../refinement/bounded_seed.cuh"

namespace optiland::de_cuda_detail {
namespace {
__device__ DeviceScalarInterval even_asphere_sag_interval(
    const DeviceSurface& surface,
    const DeviceScalarInterval& r2) {
    if (!r2.valid) {
        return {};
    }
    DeviceScalarInterval sag = point_interval(0.0);
    if (finite_value(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) <= 0.5 * kDeviceInf) {
        const double conic_scale =
            -(1.0 + surface.conic) / (surface.radius * surface.radius);
        const DeviceScalarInterval root_arg = interval_add(
            point_interval(1.0), interval_scale(r2, conic_scale));
        const DeviceScalarInterval root = interval_sqrt(root_arg);
        if (!root.valid) {
            return {};
        }
        const DeviceScalarInterval denominator = interval_scale(
            interval_add(point_interval(1.0), root), surface.radius);
        sag = interval_divide(r2, denominator);
        if (!sag.valid) {
            return {};
        }
    }
    const int count = max(0, min(surface.coeff_count, kMaxDeviceAsphereCoefficients));
    if (count > 0) {
        DeviceScalarInterval polynomial = point_interval(surface.coeffs[count - 1]);
        for (int i = count - 1; i > 0; --i) {
            polynomial = interval_add(
                point_interval(surface.coeffs[i - 1]),
                interval_multiply(r2, polynomial));
        }
        sag = interval_add(sag, interval_multiply(r2, polynomial));
    }
    return sag;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ DeviceScalarInterval even_asphere_residual_interval(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lo,
    double hi) {
    const DeviceScalarInterval z = affine_interval(pz, nz, lo, hi);
    const DeviceScalarInterval r2 =
        radial_squared_interval(px, py, lx, my, lo, hi);
    return interval_subtract(even_asphere_sag_interval(surface, r2), z);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ bool outward_point_residual_abs_upper(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double t,
    double* abs_upper) {
    if (!abs_upper || !finite_value(t)) {
        return false;
    }
    // A point certificate does not need the general interval's quadratic
    // minimum classification.  Build only the outward affine point values and
    // their radial square, then retain the same sag interval primitives.
    const DeviceScalarInterval z = affine_interval(pz, nz, t, t);
    const DeviceScalarInterval r2 =
        radial_squared_at_interval(px, py, lx, my, t);
    const DeviceScalarInterval residual = interval_subtract(
        even_asphere_sag_interval(surface, r2), z);
    if (!residual.valid) {
        return false;
    }
    *abs_upper = fmax(fabs(residual.lo), fabs(residual.hi));
    return finite_value(*abs_upper);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ DeviceScalarInterval even_asphere_derivative_interval(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double nz,
    double lo,
    double hi) {
    const DeviceScalarInterval r2 =
        radial_squared_interval(px, py, lx, my, lo, hi);
    if (!r2.valid) {
        return {};
    }

    DeviceScalarInterval sag_q = point_interval(0.0);
    if (finite_value(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) <= 0.5 * kDeviceInf) {
        const double conic_scale =
            -(1.0 + surface.conic) / (surface.radius * surface.radius);
        const DeviceScalarInterval root_arg = interval_add(
            point_interval(1.0), interval_scale(r2, conic_scale));
        const DeviceScalarInterval root = interval_sqrt(root_arg);
        if (!root.valid || root.lo <= 0.0) {
            return whole_interval();
        }
        sag_q = interval_divide(
            point_interval(1.0),
            interval_scale(root, 2.0 * surface.radius));
    }

    const int count = max(0, min(surface.coeff_count, kMaxDeviceAsphereCoefficients));
    if (count > 0) {
        const int last = count - 1;
        DeviceScalarInterval polynomial_q = point_interval(
            static_cast<double>(last + 1) * surface.coeffs[last]);
        for (int i = last; i > 0; --i) {
            polynomial_q = interval_add(
                point_interval(static_cast<double>(i) * surface.coeffs[i - 1]),
                interval_multiply(r2, polynomial_q));
        }
        sag_q = interval_add(sag_q, polynomial_q);
    }
    const DeviceScalarInterval q_prime =
        radial_squared_derivative_interval(px, py, lx, my, lo, hi);
    return interval_subtract(
        interval_multiply(sag_q, q_prime),
        point_interval(nz));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
struct DeviceAsphereResidualDerivativeInterval {
    DeviceScalarInterval residual{};
    DeviceScalarInterval derivative{};
};
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
// UC9: the strict traversal always asks for the residual interval followed by
// the derivative interval over the same node.  Preserve the existing interval
// operations and outward rounding, but construct the shared radial interval
// and conic square-root interval only once.
template <bool kReuseRadialDerivative, bool kReuseAffineEndpoints = false>
__device__ __forceinline__ DeviceAsphereResidualDerivativeInterval
even_asphere_residual_derivative_interval_fused_impl(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lo,
    double hi) {
    DeviceAsphereResidualDerivativeInterval result{};
    const DeviceScalarInterval z = affine_interval(pz, nz, lo, hi);
    DeviceScalarInterval r2{};
    DeviceScalarInterval reused_q_prime{};
    if constexpr (kReuseRadialDerivative) {
        const DeviceRadialSquaredDerivativeInterval radial =
            kReuseAffineEndpoints
                ? radial_squared_and_derivative_interval_affine_reuse(
                      px, py, lx, my, lo, hi)
                : radial_squared_and_derivative_interval(
                      px, py, lx, my, lo, hi);
        r2 = radial.radial_squared;
        reused_q_prime = radial.radial_derivative;
    } else {
        r2 = radial_squared_interval(px, py, lx, my, lo, hi);
    }
    if (!r2.valid) {
        return result;
    }

    DeviceScalarInterval sag = point_interval(0.0);
    DeviceScalarInterval sag_q = point_interval(0.0);
    bool derivative_is_whole = false;
    if (finite_value(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) <= 0.5 * kDeviceInf) {
        const double conic_scale =
            -(1.0 + surface.conic) / (surface.radius * surface.radius);
        const DeviceScalarInterval root_arg = interval_add(
            point_interval(1.0), interval_scale(r2, conic_scale));
        const DeviceScalarInterval root = interval_sqrt(root_arg);
        if (!root.valid) {
            result.derivative = whole_interval();
            return result;
        }
        const DeviceScalarInterval denominator = interval_scale(
            interval_add(point_interval(1.0), root), surface.radius);
        sag = interval_divide(r2, denominator);
        if (root.lo <= 0.0) {
            derivative_is_whole = true;
        } else {
            sag_q = interval_divide(
                point_interval(1.0),
                interval_scale(root, 2.0 * surface.radius));
        }
    }

    const int count = max(
        0, min(surface.coeff_count, kMaxDeviceAsphereCoefficients));
    if (count > 0) {
        const int last = count - 1;
        DeviceScalarInterval polynomial =
            point_interval(surface.coeffs[last]);
        DeviceScalarInterval polynomial_q = point_interval(
            static_cast<double>(last + 1) * surface.coeffs[last]);
        for (int i = last; i > 0; --i) {
            polynomial = interval_add(
                point_interval(surface.coeffs[i - 1]),
                interval_multiply(r2, polynomial));
            polynomial_q = interval_add(
                point_interval(
                    static_cast<double>(i) * surface.coeffs[i - 1]),
                interval_multiply(r2, polynomial_q));
        }
        sag = interval_add(sag, interval_multiply(r2, polynomial));
        if (!derivative_is_whole) {
            sag_q = interval_add(sag_q, polynomial_q);
        }
    }
    result.residual = interval_subtract(sag, z);
    if (derivative_is_whole) {
        result.derivative = whole_interval();
    } else {
        const DeviceScalarInterval q_prime = kReuseRadialDerivative
            ? reused_q_prime
            : radial_squared_derivative_interval(px, py, lx, my, lo, hi);
        result.derivative = interval_subtract(
            interval_multiply(sag_q, q_prime), point_interval(nz));
    }
    return result;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __noinline__ DeviceAsphereResidualDerivativeInterval
even_asphere_residual_derivative_interval_fused_noinline(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lo,
    double hi) {
    return even_asphere_residual_derivative_interval_fused_impl<false>(
        surface, px, py, pz, lx, my, nz, lo, hi);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __noinline__ DeviceAsphereResidualDerivativeInterval
even_asphere_residual_derivative_interval_qprime_reuse_noinline(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lo,
    double hi) {
    return even_asphere_residual_derivative_interval_fused_impl<true>(
        surface, px, py, pz, lx, my, nz, lo, hi);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __noinline__ DeviceAsphereResidualDerivativeInterval
even_asphere_residual_derivative_interval_affine_reuse_noinline(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lo,
    double hi) {
    return even_asphere_residual_derivative_interval_fused_impl<true, true>(
        surface, px, py, pz, lx, my, nz, lo, hi);
}
} // namespace
} // namespace optiland::de_cuda_detail
