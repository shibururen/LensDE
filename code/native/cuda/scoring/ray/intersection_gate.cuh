#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"

namespace optiland {
namespace {
__device__ bool d_surface_slopes(
    const DeviceSurface& surface,
    double x,
    double y,
    double* dzdx,
    double* dzdy) {
    if (!dzdx || !dzdy) {
        return false;
    }
    *dzdx = 0.0;
    *dzdy = 0.0;
    if (d_is_plane(surface)) {
        return true;
    }
    const double r2 = x * x + y * y;
    if (d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) <= 0.5 * kDeviceInf) {
        const double root_arg =
            1.0 - (1.0 + surface.conic) * r2 /
                      (surface.radius * surface.radius);
        if (root_arg <= 0.0) {
            return false;
        }
        const double denom = surface.radius * sqrt(root_arg);
        if (fabs(denom) <= kDeviceEps) {
            return false;
        }
        *dzdx = x / denom;
        *dzdy = y / denom;
    }
    if (surface.kind == kKindEvenAsphere) {
        double power = 1.0;
        for (int i = 0;
             i < surface.coeff_count && i < kMaxDeviceAsphereCoefficients;
             ++i) {
            const double order = 2.0 * static_cast<double>(i + 1);
            *dzdx += order * x * surface.coeffs[i] * power;
            *dzdy += order * y * surface.coeffs[i] * power;
            power *= r2;
        }
    }
    return d_finite(*dzdx) && d_finite(*dzdy);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_intersect_surface(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double* t) {
    if (!t || !d_finite(px) || !d_finite(py) || !d_finite(pz) ||
        !d_finite(lx) || !d_finite(my) || !d_finite(nz)) {
        return false;
    }
    if (d_is_plane(surface)) {
        if (fabs(nz) <= kDeviceEps) {
            return false;
        }
        *t = -pz / nz;
        return d_finite(*t);
    }
    if (surface.kind == kKindEvenAsphere || fabs(surface.conic) > 1.0e-14) {
        return de_cuda_detail::intersect_conic_or_asphere(
            surface, px, py, pz, lx, my, nz, t);
    }
    const double radius = surface.radius;
    if (!d_finite(radius) || fabs(radius) < kDeviceEps ||
        fabs(radius) > 0.5 * kDeviceInf) {
        return false;
    }
    // Use the curvature-normalized sphere equation so the coefficients stay
    // local in scale as curvature approaches zero.  Expanding
    // (pz - radius)^2 - radius^2 discards the intersection information for
    // radii around 1e12 mm and above.  The q-formula below also avoids the
    // cancellation in the near quadratic root.
    constexpr double kMachineEps = 2.2204460492503131e-16;
    constexpr double kRootTEps = 1.0e-10;
    const double curvature = 1.0 / radius;
    const double direction_norm2 = lx * lx + my * my + nz * nz;
    const double position_dot_direction = px * lx + py * my + pz * nz;
    const double position_norm2 = px * px + py * py + pz * pz;
    const double a = curvature * direction_norm2;
    const double b = 2.0 * curvature * position_dot_direction - 2.0 * nz;
    const double c = curvature * position_norm2 - 2.0 * pz;
    if (!d_finite(a) || !d_finite(b) || !d_finite(c)) {
        return false;
    }
    const double coefficient_scale = fmax(1.0, fmax(fabs(b), fabs(c)));
    const double linear_tolerance = 64.0 * kMachineEps * coefficient_scale;
    double roots[2] = {NAN, NAN};
    int root_count = 0;
    if (fabs(a) <= linear_tolerance) {
        if (fabs(b) <= linear_tolerance) {
            return false;
        }
        roots[root_count++] = -c / b;
    } else {
        const double four_ac = 4.0 * a * c;
        double discriminant = b * b - four_ac;
        const double discriminant_scale =
            fmax(1.0, b * b + fabs(four_ac));
        const double discriminant_tolerance =
            64.0 * kMachineEps * discriminant_scale;
        if (!d_finite(discriminant) ||
            discriminant < -discriminant_tolerance) {
            return false;
        }
        discriminant = fmax(0.0, discriminant);
        const double root = sqrt(discriminant);
        const double q = -0.5 * (b + copysign(root, b));
        const double q_tolerance =
            64.0 * kMachineEps * fmax(1.0, fabs(b) + root);
        if (fabs(q) <= q_tolerance) {
            roots[root_count++] = -b / (2.0 * a);
        } else {
            roots[root_count++] = q / a;
            roots[root_count++] = c / q;
        }
    }
    double best = INFINITY;
    for (int i = 0; i < root_count; ++i) {
        const double candidate = roots[i];
        if (!d_finite(candidate) || candidate < -kRootTEps) {
            continue;
        }
        const double zi = pz + candidate * nz;
        if (!d_finite(zi)) {
            continue;
        }
        // Select the vertex-connected sag branch directly from the implicit
        // sphere.  This is equivalent to the sag residual test but avoids a
        // hypot and an additional square root in the per-ray hot path.
        const double curvature_z = curvature * zi;
        const double branch = curvature_z - 1.0;
        const double branch_tolerance =
            1.0e-10 * fmax(1.0, fabs(curvature_z));
        if (branch > branch_tolerance) {
            continue;
        }
        const double candidate_score = fmax(0.0, candidate);
        const double best_score = d_finite(best) ? fmax(0.0, best) : INFINITY;
        if (!d_finite(best) || candidate_score < best_score - kRootTEps) {
            best = candidate;
        }
    }
    if (!d_finite(best)) {
        return false;
    }
    *t = fabs(best) <= kRootTEps ? 0.0 : best;
    return true;
}
} // namespace
} // namespace optiland
