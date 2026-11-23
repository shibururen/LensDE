#pragma once
#include "../prelude.cuh"
#include "bracket_bisection.cuh"
#include "first_root_interval.cuh"
#include "../geometry/sag_residual.cuh"
#include "../intervals/asphere_residual.cuh"
#include "../intervals/radial_geometry.cuh"
#include "../refinement/bounded_seed.cuh"
#include "../refinement/newton_seed.cuh"

namespace optiland::de_cuda_detail {
namespace {
__device__ bool certify_asphere_prefix_monotonic_only(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double nz,
    double lower,
    double candidate) {
    if (certify_asphere_residual_monotonic(
            surface, px, py, lx, my, nz, lower, candidate)) {
        return true;
    }
    const DeviceScalarInterval derivative_range =
        even_asphere_derivative_interval(
            surface, px, py, lx, my, nz, lower, candidate);
    return derivative_range.valid &&
           (derivative_range.lo > 0.0 || derivative_range.hi < 0.0);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
// Conservatively classify the prefix in a small number of front-to-back
// intervals.  A segment is skipped only when its residual interval excludes
// zero, or when a derivative interval proves strict monotonicity and the
// endpoint signs exclude a root.  The first certified sign bracket is the
// first forward root.  Any other segment is ambiguous and must retain the
// existing strict recursive fallback.
template <int kSegments>
__device__ __noinline__ DeviceFirstRootSearchStatus
certify_first_asphere_root_piecewise(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lower,
    double interval_upper,
    bool upper_is_known_root,
    double newton_tol,
    DeviceAsphereRootProfile* profile,
    double* root) {
    if (!root || !finite_value(lower) || !finite_value(interval_upper) ||
        lower < 0.0 || interval_upper < lower) {
        return DeviceFirstRootSearchStatus::Ambiguous;
    }
    if (profile) {
        atomicAdd(&profile->piecewise_prefix_attempts, 1ULL);
    }
    const double merge_tol = fmax(
        1.0e-10 * fmax(1.0, fabs(interval_upper)), newton_tol * 32.0);
    const double upper = upper_is_known_root
        ? interval_upper - merge_tol
        : interval_upper;
    if (!(upper > lower)) {
        if (profile) {
            atomicAdd(&profile->piecewise_prefix_no_root, 1ULL);
        }
        return DeviceFirstRootSearchStatus::NoRoot;
    }

    // Preserve the two existing whole-prefix sufficient conditions.  The
    // piecewise intervals are paid for only when both coarse bounds fail.
    if (upper_is_known_root && certify_asphere_prefix_monotonic_only(
            surface, px, py, lx, my, nz, lower, interval_upper)) {
        if (profile) {
            atomicAdd(&profile->monotonic_certificates, 1ULL);
            atomicAdd(&profile->piecewise_prefix_no_root, 1ULL);
        }
        return DeviceFirstRootSearchStatus::NoRoot;
    }

    const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
    #pragma unroll
    for (int segment = 0; segment < kSegments; ++segment) {
        const double segment_lo =
            lower + (upper - lower) * static_cast<double>(segment) /
                        static_cast<double>(kSegments);
        const double segment_hi =
            lower + (upper - lower) * static_cast<double>(segment + 1) /
                        static_cast<double>(kSegments);
        if (profile) {
            atomicAdd(&profile->piecewise_segments, 1ULL);
        }

        const DeviceScalarInterval residual_range =
            even_asphere_residual_interval(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                segment_lo,
                segment_hi);
        if (!residual_range.valid) {
            if (profile) {
                atomicAdd(&profile->piecewise_prefix_ambiguous, 1ULL);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
        // Match the operational root contract, which treats a residual
        // within residual_tol as a root even when the exact interval does
        // not contain mathematical zero.  Pruning only exact zero caused a
        // few near-tangent B2 roots to be skipped on the wide fixed64 gate.
        if (residual_range.lo > residual_tol ||
            residual_range.hi < -residual_tol) {
            if (profile) {
                atomicAdd(&profile->piecewise_residual_prunes, 1ULL);
            }
            continue;
        }

        double f_lo = 0.0;
        double f_hi = 0.0;
        if (!surface_residual_at<true>(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                segment_lo,
                &f_lo) ||
            !surface_residual_at<true>(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                segment_hi,
                &f_hi)) {
            if (profile) {
                atomicAdd(&profile->piecewise_prefix_ambiguous, 1ULL);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
        const DeviceScalarInterval derivative_range =
            even_asphere_derivative_interval(
                surface,
                px,
                py,
                lx,
                my,
                nz,
                segment_lo,
                segment_hi);
        const bool monotonic = derivative_range.valid &&
            (derivative_range.lo > 0.0 || derivative_range.hi < 0.0);
        if (!monotonic) {
            if (profile) {
                atomicAdd(&profile->piecewise_prefix_ambiguous, 1ULL);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
        if (profile) {
            atomicAdd(&profile->piecewise_monotonic_segments, 1ULL);
        }
        if (fabs(f_lo) <= residual_tol || fabs(f_hi) <= residual_tol) {
            *root = fabs(f_lo) <= residual_tol ? segment_lo : segment_hi;
            *root = fabs(*root) <= 1.0e-10 ? 0.0 : *root;
            if (profile) {
                atomicAdd(&profile->piecewise_prefix_found, 1ULL);
            }
            return DeviceFirstRootSearchStatus::Found;
        }
        if (f_lo * f_hi <= 0.0) {
            if (solve_asphere_monotonic_bracket_hybrid(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    segment_lo,
                    segment_hi,
                    f_lo,
                    f_hi,
                    newton_tol,
                    profile,
                    root)) {
                if (profile) {
                    atomicAdd(&profile->piecewise_prefix_found, 1ULL);
                }
                return DeviceFirstRootSearchStatus::Found;
            }
            if (profile) {
                atomicAdd(&profile->piecewise_prefix_ambiguous, 1ULL);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
    }
    if (profile) {
        atomicAdd(&profile->piecewise_prefix_no_root, 1ULL);
    }
    return DeviceFirstRootSearchStatus::NoRoot;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ DeviceFirstRootSearchStatus find_certified_root_before(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lower,
    double candidate,
    double newton_tol,
    double* root,
    bool skip_monotonic_checks = false) {
    if (!root || !finite_value(lower) || !finite_value(candidate) ||
        lower < 0.0 || candidate < lower) {
        return DeviceFirstRootSearchStatus::Ambiguous;
    }
    const double merge_tol = fmax(
        1.0e-10 * fmax(1.0, fabs(candidate)), newton_tol * 32.0);
    const double upper = candidate - merge_tol;
    if (!(upper > lower)) {
        return DeviceFirstRootSearchStatus::NoRoot;
    }
    if (!skip_monotonic_checks && certify_asphere_prefix_monotonic_only(
            surface, px, py, lx, my, nz, lower, candidate)) {
        return DeviceFirstRootSearchStatus::NoRoot;
    }
    return find_first_even_asphere_root_interval(
        surface,
        px,
        py,
        pz,
        lx,
        my,
        nz,
        lower,
        upper,
        4096,
        newton_tol,
        root);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ void increment_profile_counter(
    unsigned long long* counter) {
    atomicAdd(counter, 1ULL);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
template <int kConfirmationSegments>
__device__ __noinline__ DeviceFirstRootSearchStatus
confirm_initial_root_by_sampling(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lower,
    double candidate,
    double newton_tol,
    DeviceAsphereRootProfile* profile,
    int* path_flags,
    double* root) {
    if (!root || !finite_value(lower) || !finite_value(candidate) ||
        lower < 0.0 || candidate < lower) {
        if (profile) {
            increment_profile_counter(&profile->confirmation_invalid);
        }
        return DeviceFirstRootSearchStatus::Ambiguous;
    }
    const double merge_tol = fmax(
        1.0e-10 * fmax(1.0, fabs(candidate)), newton_tol * 32.0);
    const double upper = candidate - merge_tol;
    if (!(upper > lower)) {
        return DeviceFirstRootSearchStatus::NoRoot;
    }

    // Preserve the inexpensive whole-prefix certificate for ordinary rays.
    if (certify_asphere_residual_monotonic(
            surface, px, py, lx, my, nz, lower, candidate)) {
        if (profile) {
            increment_profile_counter(&profile->monotonic_certificates);
        }
        return DeviceFirstRootSearchStatus::NoRoot;
    }
    const DeviceScalarInterval derivative_range =
        even_asphere_derivative_interval(
            surface, px, py, lx, my, nz, lower, candidate);
    if (derivative_range.valid &&
        (derivative_range.lo > 0.0 || derivative_range.hi < 0.0)) {
        if (profile) {
            increment_profile_counter(&profile->monotonic_certificates);
        }
        return DeviceFirstRootSearchStatus::NoRoot;
    }
    if (profile) {
        increment_profile_counter(&profile->confirmation_scans);
    }
    if (path_flags) {
        *path_flags |= 1;
    }

    // Experimental initial-root policy: confirm that the analytic/plane seed
    // lies in a simple, well-conditioned Newton basin before accepting its
    // converged root.  A short front-to-back scan detects earlier sign roots,
    // tangent/turning behaviour, and poorly conditioned derivatives.  Only a
    // consistently monotonic, well-separated sequence is accepted; every
    // suspicious sequence falls back to the exact interval search.
    //
    // This is intentionally a practical confidence test rather than a formal
    // interval proof.  Mode 0 remains the strict reference implementation.
    const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
    double previous_t = lower;
    double previous_residual = 0.0;
    double previous_derivative = 0.0;
    if (!surface_residual_and_derivative_at(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            previous_t,
            &previous_residual,
            &previous_derivative)) {
        if (profile) {
            increment_profile_counter(&profile->confirmation_invalid);
        }
        return DeviceFirstRootSearchStatus::Ambiguous;
    }
    if (fabs(previous_residual) <= residual_tol) {
        *root = lower;
        if (profile) {
            increment_profile_counter(&profile->confirmation_sample_roots);
        }
        return DeviceFirstRootSearchStatus::Found;
    }
    const double derivative_guard =
        1.0e-6 * fmax(1.0, fabs(nz));
    if (fabs(previous_derivative) <= derivative_guard) {
        if (profile) {
            increment_profile_counter(
                &profile->confirmation_derivative_ambiguities);
        }
        return DeviceFirstRootSearchStatus::Ambiguous;
    }
    const bool increasing = previous_derivative > 0.0;
    double min_abs_derivative = fabs(previous_derivative);
    double max_derivative_step = 0.0;
    #pragma unroll
    for (int segment = 1; segment <= kConfirmationSegments; ++segment) {
        const double current_t =
            lower + (upper - lower) * static_cast<double>(segment) /
                        static_cast<double>(kConfirmationSegments);
        double current_residual = 0.0;
        double current_derivative = 0.0;
        if (!surface_residual_and_derivative_at(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                current_t,
                &current_residual,
                &current_derivative)) {
            if (profile) {
                increment_profile_counter(&profile->confirmation_invalid);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
        if (previous_residual * current_residual <= 0.0) {
            if (bisect_asphere_residual_root(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    previous_t,
                    current_t,
                    previous_residual,
                    newton_tol,
                    root)) {
                if (profile) {
                    increment_profile_counter(
                        &profile->confirmation_earlier_roots);
                }
                return DeviceFirstRootSearchStatus::Found;
            }
            if (profile) {
                increment_profile_counter(&profile->confirmation_invalid);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
        if (fabs(current_residual) <= residual_tol) {
            // A confirmation sample may be within the relaxed residual
            // tolerance without being accurate enough for downstream spot
            // and wavefront metrics.  Polish it with the same bounded Newton
            // contract used by the ordinary fast path; otherwise defer to the
            // exact search instead of returning a grid point as an endpoint.
            double refined = NAN;
            if (refine_even_asphere_root_fast(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    current_t,
                    newton_tol,
                    &refined)) {
                *root = refined;
                if (profile) {
                    increment_profile_counter(
                        &profile->confirmation_sample_roots);
                }
                return DeviceFirstRootSearchStatus::Found;
            }
            if (profile) {
                increment_profile_counter(&profile->confirmation_invalid);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
        const double abs_derivative = fabs(current_derivative);
        if (abs_derivative <= derivative_guard ||
            (current_derivative > 0.0) != increasing) {
            if (profile) {
                increment_profile_counter(
                    &profile->confirmation_derivative_ambiguities);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
        const double residual_step = current_residual - previous_residual;
        if ((increasing && residual_step < -residual_tol) ||
            (!increasing && residual_step > residual_tol)) {
            if (profile) {
                increment_profile_counter(
                    &profile->confirmation_residual_ambiguities);
            }
            return DeviceFirstRootSearchStatus::Ambiguous;
        }
        min_abs_derivative = fmin(min_abs_derivative, abs_derivative);
        max_derivative_step = fmax(
            max_derivative_step,
            fabs(current_derivative - previous_derivative));
        previous_t = current_t;
        previous_residual = current_residual;
        previous_derivative = current_derivative;
    }

    // Reject a rapidly varying derivative even when the sparse samples have
    // the same sign.  This cheaply catches narrow near-tangent excursions and
    // reserves them for the exact fallback.
    if (max_derivative_step > 0.5 * min_abs_derivative) {
        if (profile) {
            increment_profile_counter(
                &profile->confirmation_variation_ambiguities);
        }
        return DeviceFirstRootSearchStatus::Ambiguous;
    }
    if (profile) {
        increment_profile_counter(
            &profile->confirmation_accepted_candidates);
    }
    return DeviceFirstRootSearchStatus::NoRoot;
}
} // namespace
} // namespace optiland::de_cuda_detail
