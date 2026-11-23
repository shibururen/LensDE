#pragma once
#include "../prelude.cuh"

namespace optiland::de_cuda_detail {
namespace {
constexpr int kKindPlane = 1;
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
constexpr int kKindEvenAsphere = 2;
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
constexpr int kKindImage = 3;
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
constexpr int kKindObject = 4;
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
constexpr double kDeviceInf = 1.0e100;
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
constexpr double kDeviceEps = 1.0e-12;
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ bool finite_value(double value) {
    return isfinite(value);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ double clamp_value(double value, double lo, double hi) {
    return fmin(hi, fmax(lo, value));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ bool is_plane(const DeviceSurface& surface) {
    return surface.kind == kKindPlane || surface.kind == kKindImage ||
           surface.kind == kKindObject ||
           (surface.kind != kKindEvenAsphere &&
            (!finite_value(surface.radius) ||
             fabs(surface.radius) < kDeviceEps ||
             fabs(surface.radius) > 0.5 * kDeviceInf));
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ double add_even_asphere_sag(
    const DeviceSurface& surface,
    double r2,
    double z) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    if (surface.coeff_count == kMaxDeviceAsphereCoefficients) {
#else
    if ((surface.asphere_polynomial_eval_mode & 1) != 0 &&
        surface.coeff_count == kMaxDeviceAsphereCoefficients) {
#endif
        double polynomial = surface.coeffs[7];
        #pragma unroll
        for (int i = 6; i >= 0; --i) {
            polynomial = fma(polynomial, r2, surface.coeffs[i]);
        }
        return fma(r2, polynomial, z);
    }
    double power = r2;
    #pragma unroll
    for (int i = 0; i < 6; ++i) {
        if (i < surface.coeff_count) {
            z += surface.coeffs[i] * power;
        }
        if (i < 5 || surface.coeff_count > 6) {
            power *= r2;
        }
    }
    if (surface.coeff_count > 6) {
        z += surface.coeffs[6] * power;
        if (surface.coeff_count > 7) {
            power *= r2;
            z += surface.coeffs[7] * power;
        }
    }
    return z;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
template <bool IncludeAsphere>
__device__ double surface_sag_from_r2(
    const DeviceSurface& surface,
    double r2) {
    // Every caller forms r2 from x*x + y*y.  Validate that shared radial
    // invariant before the plane shortcut so a finite sag also proves that
    // the transverse coordinates were finite; callers need not classify x
    // and y again after this function returns.
    if (!finite_value(r2) || r2 < 0.0) {
        return NAN;
    }
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    const bool materialized_finite_conic =
        (surface.asphere_polynomial_eval_mode &
         kAsphereMaterializedFiniteConicBit) != 0;
    if (!materialized_finite_conic && is_plane(surface)) {
#else
    if (is_plane(surface)) {
#endif
        return 0.0;
    }
    const double radius = surface.radius;
    double z = 0.0;
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    if (materialized_finite_conic ||
        (finite_value(radius) && fabs(radius) >= kDeviceEps &&
         fabs(radius) <= 0.5 * kDeviceInf)) {
#else
    if (finite_value(radius) && fabs(radius) >= kDeviceEps &&
        fabs(radius) <= 0.5 * kDeviceInf) {
#endif
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
        // The release cache is materialized only for EvenAsphere surfaces.
        // Standard conics reach this shared evaluator too, but deliberately
        // retain the sentinel value in the cache field.  Using that sentinel
        // as a conic scale changes the surface equation (for example, -1
        // turns 1 - scale*r2 into 1 + r2) and can reject every ray at an
        // otherwise valid Standard surface.  Keep the cached hot path for a
        // materialized EvenAsphere and use the exact legacy scale for every
        // other finite conic.
        const double conic_root_scale =
            materialized_finite_conic
                ? surface.slope_root_domain_cached_clear_radius
                : (1.0 + surface.conic) / (radius * radius);
        const double under =
            1.0 - conic_root_scale * r2;
#else
        const double under =
            1.0 - (1.0 + surface.conic) * r2 / (radius * radius);
#endif
        if (under < 0.0) {
            return NAN;
        }
        const double denom = radius * (1.0 + sqrt(fmax(0.0, under)));
        if (fabs(denom) < kDeviceEps) {
            return NAN;
        }
        z = r2 / denom;
    }
    if constexpr (IncludeAsphere) {
        z = add_even_asphere_sag(surface, r2, z);
    }
    return z;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
// Newton needs the residual and its directional derivative at the same point.
// Computing them together avoids repeating the conic square root and the
// high-order departure polynomial on every iteration.
__device__ __forceinline__ bool surface_residual_and_derivative_at(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double tv,
    double* residual,
    double* derivative) {
    if (!residual || !derivative || !finite_value(tv)) {
        return false;
    }
    const double x = px + tv * lx;
    const double y = py + tv * my;
    const double z = pz + tv * nz;
    const double r2 = fma(x, x, y * y);
    // A finite sum of the two non-negative squares already proves x and y
    // finite, and the constructed r2 cannot be negative.
    if (!finite_value(z) || !finite_value(r2)) {
        return false;
    }

    double sag = 0.0;
    double directional_slope = 0.0;
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    const bool materialized_finite_conic =
        (surface.asphere_polynomial_eval_mode &
         kAsphereMaterializedFiniteConicBit) != 0;
    if (materialized_finite_conic || !is_plane(surface)) {
#else
    if (!is_plane(surface)) {
#endif
        const double radius = surface.radius;
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
        if (materialized_finite_conic ||
            (finite_value(radius) && fabs(radius) >= kDeviceEps &&
             fabs(radius) <= 0.5 * kDeviceInf)) {
#else
        if (finite_value(radius) && fabs(radius) >= kDeviceEps &&
            fabs(radius) <= 0.5 * kDeviceInf) {
#endif
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
            const double conic_root_scale =
                materialized_finite_conic
                    ? surface.slope_root_domain_cached_clear_radius
                    : (1.0 + surface.conic) / (radius * radius);
            const double root_arg =
                1.0 - conic_root_scale * r2;
#else
            const double root_arg =
                1.0 - (1.0 + surface.conic) * r2 / (radius * radius);
#endif
            if (!(root_arg > 0.0)) {
                return false;
            }
            const double root = sqrt(root_arg);
            const double sag_denom = radius * (1.0 + root);
            const double derivative_denom = 2.0 * radius * root;
            if (fabs(sag_denom) < kDeviceEps ||
                fabs(derivative_denom) < kDeviceEps) {
                return false;
            }
            sag = r2 / sag_denom;
            directional_slope =
                (x * lx + y * my) / (radius * root);
        }

        const double radial_direction = x * lx + y * my;
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
        if (surface.coeff_count == kMaxDeviceAsphereCoefficients) {
#else
        if ((surface.asphere_polynomial_eval_mode & 2) != 0 &&
            surface.coeff_count == kMaxDeviceAsphereCoefficients) {
#endif
            // P(u) = u Q(u), u = r^2.  Carry Q and Q' together so the
            // residual and directional derivative share one Horner chain.
            double polynomial = surface.coeffs[7];
            double polynomial_derivative = 0.0;
            #pragma unroll
            for (int i = 6; i >= 0; --i) {
                polynomial_derivative =
                    fma(polynomial_derivative, r2, polynomial);
                polynomial = fma(polynomial, r2, surface.coeffs[i]);
            }
            sag = fma(r2, polynomial, sag);
            const double derivative_by_r2 =
                fma(r2, polynomial_derivative, polynomial);
            directional_slope = fma(
                2.0 * radial_direction,
                derivative_by_r2,
                directional_slope);
        } else {
            double sag_power = r2;
            double slope_power = 1.0;
            #pragma unroll
            for (int i = 0; i < 6; ++i) {
                if (i < surface.coeff_count) {
                    const double coefficient = surface.coeffs[i];
                    const double order = 2.0 * static_cast<double>(i + 1);
                    sag += coefficient * sag_power;
                    directional_slope +=
                        order * radial_direction * coefficient * slope_power;
                }
                if (i < 5 || surface.coeff_count > 6) {
                    sag_power *= r2;
                    slope_power *= r2;
                }
            }
            if (surface.coeff_count > 6) {
                const double coefficient = surface.coeffs[6];
                sag += coefficient * sag_power;
                directional_slope +=
                    14.0 * radial_direction * coefficient * slope_power;
                if (surface.coeff_count > 7) {
                    sag_power *= r2;
                    slope_power *= r2;
                    const double coefficient8 = surface.coeffs[7];
                    sag += coefficient8 * sag_power;
                    directional_slope +=
                        16.0 * radial_direction * coefficient8 * slope_power;
                }
            }
        }
    }
    *residual = sag - z;
    *derivative = directional_slope - nz;
    return finite_value(*residual) && finite_value(*derivative);
}
} // namespace
} // namespace optiland::de_cuda_detail
