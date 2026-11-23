#pragma once
#include "../prelude.cuh"
#include "first_root_interval.cuh"
#include "prefix_piecewise.cuh"
#include "../geometry/forward_interval.cuh"
#include "../geometry/sag_residual.cuh"
#include "../refinement/newton_seed.cuh"

namespace optiland::de_cuda_detail {
__device__ __noinline__ bool intersect_asphere_nearest_forward_fallback(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double requested_lower,
    double requested_upper,
    double* t) {
    if (!t || surface.kind != kKindEvenAsphere) {
        return false;
    }
    double candidates[3] = {NAN, NAN, NAN};
    int candidate_count = 0;
    const double dz_safe =
        fabs(nz) > kDeviceEps ? nz : (nz >= 0.0 ? kDeviceEps : -kDeviceEps);
    candidates[candidate_count++] = -pz / dz_safe;
    if (finite_value(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) <= 0.5 * kDeviceInf) {
        const double a =
            surface.conic * nz * nz + lx * lx + my * my + nz * nz;
        const double b =
            2.0 * surface.conic * nz * pz + 2.0 * lx * px +
            2.0 * my * py - 2.0 * nz * surface.radius + 2.0 * nz * pz;
        const double c =
            surface.conic * pz * pz - 2.0 * surface.radius * pz +
            px * px + py * py + pz * pz;
        if (fabs(a) > kDeviceEps) {
            const double discriminant = b * b - 4.0 * a * c;
            if (discriminant >= 0.0) {
                const double root = sqrt(fmax(0.0, discriminant));
                candidates[candidate_count++] = (-b + root) / (2.0 * a);
                candidates[candidate_count++] = (-b - root) / (2.0 * a);
            }
        } else if (fabs(b) > kDeviceEps) {
            candidates[candidate_count++] = -c / b;
        }
    }
    const double newton_tol =
        finite_value(surface.newton_tol) && surface.newton_tol > 0.0
            ? surface.newton_tol
            : 1.0e-10;
    const double plane_seed = -pz / dz_safe;
    double farthest_seed = NAN;
    for (int i = 0; i < candidate_count; ++i) {
        if (finite_value(candidates[i]) && candidates[i] >= 0.0 &&
            (!finite_value(farthest_seed) || candidates[i] > farthest_seed)) {
            farthest_seed = candidates[i];
        }
    }
    const double finite_seed =
        finite_value(farthest_seed) && farthest_seed >= 0.0
            ? farthest_seed
            : fmax(0.0, plane_seed);
    const double search_ceiling =
        finite_seed + fmax(1.0, fabs(finite_seed) + 1.0);
    const double search_lower =
        finite_value(requested_lower) ? fmax(0.0, requested_lower) : 0.0;
    const double search_upper =
        finite_value(requested_upper) ? requested_upper : search_ceiling;
    if (!(search_upper >= search_lower)) {
        return false;
    }
    double nearest = INFINITY;
    for (int i = 0; i < candidate_count; ++i) {
        double refined = NAN;
        if (refine_even_asphere_root(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                candidates[i],
                newton_tol,
                &refined) &&
            refined >= search_lower - kDeviceEps &&
            refined <= search_upper + kDeviceEps && refined < nearest) {
            nearest = refined;
        }
    }
    if (finite_value(nearest)) {
        double earlier = NAN;
        const DeviceFirstRootSearchStatus prefix_status =
            find_certified_root_before(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                search_lower,
                nearest,
                newton_tol,
                &earlier);
        if (prefix_status == DeviceFirstRootSearchStatus::Found) {
            *t = earlier;
            return finite_value(*t);
        }
        if (prefix_status == DeviceFirstRootSearchStatus::NoRoot) {
            *t = nearest;
            return finite_value(*t);
        }
    }

    // Candidate Newton paths can fail near the conic radical boundary or can
    // converge to a root whose prefix exhausts the small certification budget.
    // Search the bounded physical forward domain once with the larger recovery
    // budget.  Ambiguous is deliberately treated as a miss: accepting an
    // uncertified later branch would underestimate the aperture envelope.
    double lower = 0.0;
    double upper = 0.0;
    if (!asphere_forward_search_interval(
            surface,
            px,
            py,
            lx,
            my,
            search_lower,
            search_upper,
            &lower,
            &upper)) {
        return false;
    }
    double fallback_root = NAN;
    const DeviceFirstRootSearchStatus fallback_status =
        find_first_even_asphere_root_interval(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            lower,
            upper,
            16384,
            newton_tol,
            &fallback_root);
    if (fallback_status != DeviceFirstRootSearchStatus::Found) {
        return false;
    }
    *t = fallback_root;
    return finite_value(*t);
}
} // namespace optiland::de_cuda_detail
