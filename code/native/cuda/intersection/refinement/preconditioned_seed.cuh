#pragma once
#include "../prelude.cuh"
#include "../geometry/sag_residual.cuh"
#include "newton_seed.cuh"

namespace optiland::de_cuda_detail {
namespace {
__device__ bool refine_even_asphere_root_bounded(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double seed,
    double lower,
    double upper,
    double newton_tol,
    double* refined) {
    if (!refined || !finite_value(seed) || !finite_value(lower) ||
        !finite_value(upper) || lower > upper) {
        return false;
    }
    double tn = clamp_value(seed, lower, upper);
    const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
    const double step_bound = fmax(1.0, fabs(tn) + 1.0);
    double final_residual = INFINITY;
    bool residual_certified = false;
    for (int iter = 0; iter < max(0, surface.newton_max_iter); ++iter) {
        double residual = 0.0;
        double derivative = 0.0;
        if (!surface_residual_and_derivative_at(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                tn,
                &residual,
                &derivative)) {
            return false;
        }
        if (fabs(residual) <= newton_tol) {
            final_residual = residual;
            residual_certified = true;
            break;
        }
        if (fabs(derivative) <= kDeviceEps) {
            return false;
        }
        const double step = clamp_value(
            residual / derivative, -step_bound, step_bound);
        double next = tn - step;
        if (!finite_value(next)) {
            return false;
        }
        if (next < lower || next > upper) {
            next = 0.5 * (tn + (next < lower ? lower : upper));
        }
        tn = next;
    }
    if ((!residual_certified &&
         !surface_residual_at<true>(
             surface, px, py, pz, lx, my, nz, tn, &final_residual)) ||
        fabs(final_residual) > residual_tol || tn < lower || tn > upper) {
        return false;
    }
    *refined = fabs(tn) <= 1.0e-10 ? 0.0 : tn;
    return finite_value(*refined);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
// Round16 isolated experiment: reproduce the older initial-value
// preconditioning idea inside the finite slope domain.  Plane/base-conic
// guesses are inspected before Newton.  A suspicious guess first receives a
// short front-to-back sign scan and a configurable number of bisections; the
// resulting value is then passed to bounded Newton.  No B2 resolver is used.
__device__ int collect_even_asphere_precondition_seeds(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double* seeds) {
    if (!seeds) {
        return 0;
    }
    int count = 0;
    auto add_seed = [&](double value) {
        if (count < 3 && finite_value(value)) {
            seeds[count++] = value;
        }
    };
    const double dz_safe =
        fabs(nz) > kDeviceEps ? nz : (nz >= 0.0 ? kDeviceEps : -kDeviceEps);
    add_seed(-pz / dz_safe);
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
        if (fabs(a) <= kDeviceEps) {
            if (fabs(b) > kDeviceEps) {
                add_seed(-c / b);
            }
        } else {
            const double discriminant = b * b - 4.0 * a * c;
            if (discriminant >= 0.0) {
                const double discriminant_root = sqrt(fmax(0.0, discriminant));
                const double q =
                    -0.5 * (b + copysign(discriminant_root, b));
                const double t1 =
                    fabs(q) > kDeviceEps ? q / a : -b / (2.0 * a);
                const double t2 = fabs(q) > kDeviceEps ? c / q : t1;
                add_seed(t1);
                add_seed(t2);
            }
        }
    }
    for (int i = 1; i < count; ++i) {
        const double value = seeds[i];
        int j = i - 1;
        while (j >= 0 && seeds[j] > value) {
            seeds[j + 1] = seeds[j];
            --j;
        }
        seeds[j + 1] = value;
    }
    return count;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ bool refine_even_asphere_root_seed_preconditioned(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double raw_seed,
    double lower,
    double upper,
    int precondition_steps,
    double newton_tol,
    DeviceAsphereRootProfile* profile,
    double* root) {
    if (!root || !finite_value(raw_seed) || !finite_value(lower) ||
        !finite_value(upper) || lower < 0.0 || !(upper >= lower)) {
        return false;
    }
    const int steps = max(1, min(precondition_steps, 8));
    double seed = clamp_value(raw_seed, lower, upper);
    double initial_residual = 0.0;
    if (!surface_residual_at<true>(
            surface, px, py, pz, lx, my, nz, seed, &initial_residual)) {
        return false;
    }
    double newton_lower = lower;
    double newton_upper = upper;
    bool bracketed = false;

    if (seed > lower && fabs(initial_residual) > 1.0e-2) {
        double scan_lo = lower;
        double f_lo = 0.0;
        if (surface_residual_at<true>(
                surface, px, py, pz, lx, my, nz, scan_lo, &f_lo)) {
            constexpr int kBracketSegments = 16;
            #pragma unroll
            for (int segment = 1; segment <= kBracketSegments; ++segment) {
                const double scan_hi =
                    lower + (seed - lower) *
                                static_cast<double>(segment) /
                                static_cast<double>(kBracketSegments);
                double f_hi = 0.0;
                if (profile) {
                    atomicAdd(&profile->piecewise_segments, 1ULL);
                }
                if (!surface_residual_at<true>(
                        surface,
                        px,
                        py,
                        pz,
                        lx,
                        my,
                        nz,
                        scan_hi,
                        &f_hi)) {
                    continue;
                }
                const bool sign_bracket =
                    (f_lo <= 0.0 && f_hi >= 0.0) ||
                    (f_lo >= 0.0 && f_hi <= 0.0);
                if (sign_bracket) {
                    double left = scan_lo;
                    double right = scan_hi;
                    double f_left = f_lo;
                    for (int iteration = 0; iteration < steps; ++iteration) {
                        const double mid = 0.5 * (left + right);
                        double f_mid = 0.0;
                        if (profile) {
                            atomicAdd(&profile->piecewise_segments, 1ULL);
                        }
                        if (!surface_residual_at<true>(
                                surface,
                                px,
                                py,
                                pz,
                                lx,
                                my,
                                nz,
                                mid,
                                &f_mid)) {
                            right = mid;
                            continue;
                        }
                        if ((f_left <= 0.0 && f_mid >= 0.0) ||
                            (f_left >= 0.0 && f_mid <= 0.0)) {
                            right = mid;
                        } else {
                            left = mid;
                            f_left = f_mid;
                        }
                    }
                    seed = 0.5 * (left + right);
                    newton_lower = left;
                    newton_upper = right;
                    bracketed = true;
                    break;
                }
                scan_lo = scan_hi;
                f_lo = f_hi;
            }
        }
    }

    if (!bracketed &&
        ((initial_residual * nz) < 0.0 || fabs(initial_residual) > 1.0e-2)) {
        const bool overshoot = (initial_residual * nz) < 0.0;
        const double dz_safe =
            fabs(nz) > kDeviceEps ? nz :
                (nz >= 0.0 ? kDeviceEps : -kDeviceEps);
        const double extension = (initial_residual / dz_safe) * 2.0;
        double safe = overshoot ? lower : seed;
        double risk = overshoot ? seed : clamp_value(seed + extension, lower, upper);
        if (risk < safe) {
            const double swap = safe;
            safe = risk;
            risk = swap;
        }
        if (finite_value(safe) && finite_value(risk) && risk > safe) {
            for (int iteration = 0; iteration < steps; ++iteration) {
                const double mid = 0.5 * (safe + risk);
                double f_mid = 0.0;
                if (profile) {
                    atomicAdd(&profile->piecewise_segments, 1ULL);
                }
                const bool valid = surface_residual_at<true>(
                    surface, px, py, pz, lx, my, nz, mid, &f_mid);
                const bool mid_overshoot =
                    !valid || (f_mid * nz) < 0.0;
                if (mid_overshoot) {
                    risk = mid;
                } else {
                    safe = mid;
                }
            }
            seed = safe;
        }
    }

    return refine_even_asphere_root_bounded(
        surface,
        px,
        py,
        pz,
        lx,
        my,
        nz,
        seed,
        newton_lower,
        newton_upper,
        newton_tol,
        root);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __noinline__ bool intersect_asphere_seed_preconditioned_no_b2(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lower,
    double upper,
    int precondition_steps,
    double newton_tol,
    DeviceAsphereRootProfile* profile,
    double* root) {
    if (!root || !finite_value(lower) || lower < 0.0 || !(upper >= lower)) {
        return false;
    }
    if (profile) {
        atomicAdd(&profile->piecewise_prefix_attempts, 1ULL);
    }

    if (!finite_value(upper)) {
        const double transverse_squared = fma(lx, lx, my * my);
        double f_lower = 0.0;
        if (!finite_value(transverse_squared) ||
            transverse_squared > 64.0 * DBL_EPSILON * DBL_EPSILON ||
            !surface_residual_at<true>(
                surface, px, py, pz, lx, my, nz, lower, &f_lower)) {
            if (profile) {
                atomicAdd(&profile->piecewise_prefix_ambiguous, 1ULL);
            }
            return false;
        }
        const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
        const double axial_root =
            fabs(nz) > kDeviceEps ? lower + f_lower / nz : lower;
        double final_residual = 0.0;
        if (finite_value(axial_root) && axial_root >= lower &&
            surface_residual_at<true>(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                axial_root,
                &final_residual) &&
            fabs(final_residual) <= residual_tol) {
            *root = fabs(axial_root) <= 1.0e-10 ? 0.0 : axial_root;
            if (profile) {
                atomicAdd(&profile->piecewise_prefix_found, 1ULL);
            }
            return true;
        }
        if (profile) {
            atomicAdd(&profile->piecewise_prefix_ambiguous, 1ULL);
        }
        return false;
    }

    double seeds[3] = {NAN, NAN, NAN};
    const int seed_count = collect_even_asphere_precondition_seeds(
        surface, px, py, pz, lx, my, nz, seeds);
    #pragma unroll
    for (int i = 0; i < 3; ++i) {
        if (i < seed_count && refine_even_asphere_root_seed_preconditioned(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                seeds[i],
                lower,
                upper,
                precondition_steps,
                newton_tol,
                profile,
                root)) {
            if (profile) {
                atomicAdd(&profile->piecewise_prefix_found, 1ULL);
            }
            return true;
        }
    }

    // If every analytic seed fails, use a small front-to-back domain scan to
    // obtain a new sign-bracketed seed.  This is fixed-budget preconditioning,
    // not the B2 interval resolver.
    constexpr int kRecoverySegments = 16;
    double scan_lo = lower;
    double f_lo = 0.0;
    if (surface_residual_at<true>(
            surface, px, py, pz, lx, my, nz, scan_lo, &f_lo)) {
        for (int segment = 1; segment <= kRecoverySegments; ++segment) {
            const double scan_hi =
                lower + (upper - lower) *
                            static_cast<double>(segment) /
                            static_cast<double>(kRecoverySegments);
            double f_hi = 0.0;
            if (profile) {
                atomicAdd(&profile->piecewise_segments, 1ULL);
            }
            if (!surface_residual_at<true>(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    scan_hi,
                    &f_hi)) {
                continue;
            }
            if ((f_lo <= 0.0 && f_hi >= 0.0) ||
                (f_lo >= 0.0 && f_hi <= 0.0)) {
                double recovered = NAN;
                if (refine_even_asphere_root_seed_preconditioned(
                        surface,
                        px,
                        py,
                        pz,
                        lx,
                        my,
                        nz,
                        0.5 * (scan_lo + scan_hi),
                        scan_lo,
                        scan_hi,
                        precondition_steps,
                        newton_tol,
                        profile,
                        &recovered)) {
                    *root = recovered;
                    if (profile) {
                        atomicAdd(&profile->piecewise_prefix_found, 1ULL);
                    }
                    return true;
                }
                break;
            }
            scan_lo = scan_hi;
            f_lo = f_hi;
        }
    }
    if (profile) {
        atomicAdd(&profile->piecewise_prefix_ambiguous, 1ULL);
    }
    return false;
}
} // namespace
} // namespace optiland::de_cuda_detail
