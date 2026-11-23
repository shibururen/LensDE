#pragma once
#include "../prelude.cuh"
#include "bracket_bisection.cuh"
#include "../geometry/sag_residual.cuh"
#include "../intervals/asphere_residual.cuh"
#include "../intervals/radial_geometry.cuh"
#include "../refinement/newton_seed.cuh"
#include "../refinement/preconditioned_seed.cuh"

namespace optiland::de_cuda_detail {
namespace {
// Solve a sign bracket whose interval derivative has already been certified
// away from zero.  Newton supplies the fast local convergence, while every
// accepted step remains inside the live bracket.  A midpoint step is used
// whenever Newton would leave too little numerical room at an endpoint.
__device__ bool solve_asphere_monotonic_bracket_hybrid(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lo,
    double hi,
    double f_lo,
    double f_hi,
    double newton_tol,
    DeviceAsphereRootProfile* profile,
    double* root) {
    if (!root || !(hi >= lo) || !finite_value(f_lo) ||
        !finite_value(f_hi) || f_lo * f_hi > 0.0) {
        return false;
    }
    if (profile) {
        atomicAdd(&profile->piecewise_hybrid_solves, 1ULL);
    }
    const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
    if (fabs(f_lo) <= residual_tol) {
        *root = fabs(lo) <= 1.0e-10 ? 0.0 : lo;
        return true;
    }
    if (fabs(f_hi) <= residual_tol) {
        *root = fabs(hi) <= 1.0e-10 ? 0.0 : hi;
        return true;
    }

    double current = 0.5 * (lo + hi);
    const int iteration_limit = min(80, max(16, surface.newton_max_iter));
    for (int iter = 0; iter < iteration_limit; ++iter) {
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
                current,
                &residual,
                &derivative)) {
            return false;
        }
        if (profile) {
            atomicAdd(&profile->piecewise_hybrid_iterations, 1ULL);
        }
        if (fabs(residual) <= newton_tol) {
            *root = fabs(current) <= 1.0e-10 ? 0.0 : current;
            return true;
        }

        if (f_lo * residual <= 0.0) {
            hi = current;
            f_hi = residual;
        } else {
            lo = current;
            f_lo = residual;
        }
        const double midpoint = 0.5 * (lo + hi);
        const double width = hi - lo;
        if (!(width > 0.0) ||
            width <= 1.0e-12 * fmax(1.0, fabs(midpoint))) {
            double final_residual = 0.0;
            if (!surface_residual_at<true>(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    midpoint,
                    &final_residual) ||
                fabs(final_residual) > residual_tol) {
                return false;
            }
            *root = fabs(midpoint) <= 1.0e-10 ? 0.0 : midpoint;
            return true;
        }

        double next = midpoint;
        if (fabs(derivative) > kDeviceEps) {
            const double newton = current - residual / derivative;
            const double margin = 0.0625 * width;
            if (finite_value(newton) && newton > lo + margin &&
                newton < hi - margin) {
                next = newton;
            }
        }
        if (!(next > lo) || !(next < hi) || next == current) {
            next = midpoint;
        }
        current = next;
    }
    return false;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
enum class DeviceFirstRootSearchStatus {
    NoRoot,
    Found,
    Ambiguous,
};
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ __noinline__ DeviceFirstRootSearchStatus
find_first_even_asphere_root_interval(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double initial_lo,
    double initial_hi,
    int max_nodes,
    double newton_tol,
    double* root,
    DeviceAsphereRootProfile* unified_profile = nullptr,
    int* visited_nodes = nullptr,
    bool strict_unified = false) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    unified_profile = nullptr;
    visited_nodes = nullptr;
    strict_unified = true;
#endif
    if (visited_nodes) {
        *visited_nodes = 0;
    }
    if (!root || !(initial_hi >= initial_lo) || max_nodes <= 0) {
        return DeviceFirstRootSearchStatus::Ambiguous;
    }

    // Depth-first traversal needs only the pending right siblings.  The array
    // is touched exclusively on the rare non-monotonic aperture path and keeps
    // the ordinary CUDA merit tracer free of interval-search overhead.
    constexpr int kMaxDepth = 56;
    constexpr int kStackCapacity = kMaxDepth + 1;
    double pending_lo[kStackCapacity];
    double pending_hi[kStackCapacity];
    int pending_depth[kStackCapacity];
    int pending_count = 0;
    int nodes = 0;
    DeviceAsphereBracketDiagnostic bracket_diagnostic{};
    double lo = initial_lo;
    double hi = initial_hi;
    int depth = 0;
    const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    constexpr int uc14_mode = 64;
#else
    const int uc14_mode = surface.asphere_polynomial_eval_mode;
#endif
    const bool uc14_raw_prune =
        strict_unified && (uc14_mode & kAsphereUc14RawPruneBit) != 0;
    const bool uc14_defer_hi =
        strict_unified && (uc14_mode & kAsphereUc14DeferHiBit) != 0;
    const bool uc14_known_hi =
        strict_unified && (uc14_mode & kAsphereUc14KnownHiBit) != 0;
    const bool uc14_position_certificate =
        strict_unified &&
        (uc14_mode & kAsphereUc14PositionCertificateBit) != 0;
    auto finish = [&](DeviceFirstRootSearchStatus status) {
        if (visited_nodes) {
            *visited_nodes = nodes;
        }
        if (strict_unified && unified_profile) {
            atomicAdd(
                &unified_profile->unified_adaptive_bracket_calls,
                static_cast<unsigned long long>(bracket_diagnostic.calls));
            atomicAdd(
                &unified_profile->unified_adaptive_bracket_successes,
                static_cast<unsigned long long>(bracket_diagnostic.successes));
            atomicAdd(
                &unified_profile->unified_adaptive_bracket_residual_evaluations,
                static_cast<unsigned long long>(
                    bracket_diagnostic.residual_evaluations));
            atomicAdd(
                &unified_profile
                     ->unified_adaptive_bracket_residual_derivative_evaluations,
                static_cast<unsigned long long>(
                    bracket_diagnostic.residual_derivative_evaluations));
        }
        return status;
    };

    while (true) {
        if (++nodes > max_nodes) {
            return finish(DeviceFirstRootSearchStatus::Ambiguous);
        }
        if (unified_profile) {
            atomicAdd(
                &unified_profile->unified_interval_nodes, 1ULL);
        }
        bool no_root = false;
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
        constexpr int fused_interval_mode = 64;
#else
        const int fused_interval_mode =
            surface.asphere_polynomial_eval_mode & 240;
#endif
        DeviceScalarInterval residual_range{};
        DeviceScalarInterval unified_derivative_range{};
        bool fused_derivative_available = false;
        if (strict_unified && fused_interval_mode != 0) {
            const DeviceAsphereResidualDerivativeInterval fused =
                fused_interval_mode == 16
                    ? even_asphere_residual_derivative_interval_fused_impl<false>(
                          surface, px, py, pz, lx, my, nz, lo, hi)
                    : (fused_interval_mode == 192
                           ? even_asphere_residual_derivative_interval_affine_reuse_noinline(
                                 surface, px, py, pz, lx, my, nz, lo, hi)
                           : (fused_interval_mode == 64
                           ? even_asphere_residual_derivative_interval_qprime_reuse_noinline(
                                 surface, px, py, pz, lx, my, nz, lo, hi)
                           : even_asphere_residual_derivative_interval_fused_noinline(
                                 surface, px, py, pz, lx, my, nz, lo, hi)));
            residual_range = fused.residual;
            unified_derivative_range = fused.derivative;
            fused_derivative_available = true;
        } else {
            residual_range = even_asphere_residual_interval(
                surface, px, py, pz, lx, my, nz, lo, hi);
        }
        const bool raw_residual_excludes_zero =
            residual_range.valid && strict_unified &&
            (residual_range.lo > 0.0 || residual_range.hi < 0.0);
        if (uc14_raw_prune && raw_residual_excludes_zero && unified_profile) {
            atomicAdd(
                &unified_profile->unified_uc14_raw_prune_shortcuts, 1ULL);
            atomicAdd(
                &unified_profile->unified_uc14_midpoint_refinement_skips,
                1ULL);
        }
        if (residual_range.valid && strict_unified &&
            !(uc14_raw_prune && raw_residual_excludes_zero)) {
            if (!fused_derivative_available) {
                unified_derivative_range =
                    even_asphere_derivative_interval(
                        surface, px, py, lx, my, nz, lo, hi);
            }
            const double mid = 0.5 * (lo + hi);
            double f_mid = 0.0;
            if (unified_derivative_range.valid &&
                surface_residual_at<true>(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    mid,
                    &f_mid)) {
                const DeviceScalarInterval displacement =
                    outward_interval(lo - mid, hi - mid);
                const DeviceScalarInterval mean_value_range = interval_add(
                    point_interval(f_mid),
                    interval_multiply(
                        unified_derivative_range, displacement));
                if (mean_value_range.valid) {
                    const double intersect_lo =
                        fmax(residual_range.lo, mean_value_range.lo);
                    const double intersect_hi =
                        fmin(residual_range.hi, mean_value_range.hi);
                    if (intersect_hi >= intersect_lo) {
                        residual_range = {
                            intersect_lo, intersect_hi, true};
                    }
                }
            }
        }
        if (!residual_range.valid) {
            if (strict_unified) {
                // A non-physical right endpoint does not invalidate the
                // physical prefix.  Continue the same left-first traversal;
                // no legacy recovery path is involved.
                const double width_tol = 1.0e-11 *
                    fmax(1.0, fmax(fabs(lo), fabs(hi)));
                if (depth >= kMaxDepth || hi - lo <= width_tol) {
                    return finish(DeviceFirstRootSearchStatus::Ambiguous);
                }
                const double mid = 0.5 * (lo + hi);
                if (!(mid > lo) || !(hi > mid) ||
                    pending_count >= kStackCapacity) {
                    return finish(DeviceFirstRootSearchStatus::Ambiguous);
                }
                pending_lo[pending_count] = mid;
                pending_hi[pending_count] = hi;
                pending_depth[pending_count] = depth + 1;
                ++pending_count;
                hi = mid;
                ++depth;
                continue;
            }
            no_root = true;
        } else if (residual_range.lo > 0.0 ||
                   residual_range.hi < 0.0) {
            no_root = true;
            if (unified_profile) {
                atomicAdd(
                    &unified_profile->unified_interval_residual_prunes,
                    1ULL);
            }
        } else {
            double f_lo = 0.0;
            double f_hi = 0.0;
            const bool lo_valid = surface_residual_at<true>(
                surface, px, py, pz, lx, my, nz, lo, &f_lo);
            bool hi_valid = false;
            bool hi_evaluated = false;
            if (!uc14_defer_hi) {
                hi_valid = surface_residual_at<true>(
                    surface, px, py, pz, lx, my, nz, hi, &f_hi);
                hi_evaluated = true;
                if (strict_unified && unified_profile) {
                    atomicAdd(
                        &unified_profile
                             ->unified_uc14_hi_residual_evaluations,
                        1ULL);
                }
            }
            const double endpoint_x = fma(lo, lx, px);
            const double endpoint_y = fma(lo, my, py);
            const double endpoint_z = fma(lo, nz, pz);
            const double endpoint_scale = fmax(
                1.0,
                fmax(fabs(endpoint_z), hypot(endpoint_x, endpoint_y)));
            const double endpoint_tol = strict_unified
                ? 128.0 * DBL_EPSILON * endpoint_scale
                : residual_tol;
            bool lo_is_accurate_root = false;
            if (lo_valid && fabs(f_lo) <= endpoint_tol) {
                if (!strict_unified) {
                    lo_is_accurate_root = true;
                } else {
                    double endpoint_residual = 0.0;
                    double endpoint_derivative = 0.0;
                    if (surface_residual_and_derivative_at(
                            surface,
                            px,
                            py,
                            pz,
                            lx,
                            my,
                            nz,
                            lo,
                            &endpoint_residual,
                            &endpoint_derivative) &&
                        fabs(endpoint_derivative) > kDeviceEps) {
                        const double correction =
                            (fabs(f_lo) + endpoint_tol) /
                            fabs(endpoint_derivative);
                        lo_is_accurate_root =
                            correction <=
                            1.0e-11 * fmax(1.0, fabs(lo));
                    }
                }
            }
            if (lo_is_accurate_root) {
                *root = fabs(lo) <= 1.0e-10 ? 0.0 : lo;
                return finish(DeviceFirstRootSearchStatus::Found);
            }

            const DeviceScalarInterval derivative_range = strict_unified
                ? unified_derivative_range
                : even_asphere_derivative_interval(
                      surface, px, py, lx, my, nz, lo, hi);
            const bool monotonic = derivative_range.valid &&
                (derivative_range.lo > 0.0 || derivative_range.hi < 0.0);
            if (monotonic && lo_valid && !hi_evaluated) {
                hi_valid = surface_residual_at<true>(
                    surface, px, py, pz, lx, my, nz, hi, &f_hi);
                hi_evaluated = true;
                if (unified_profile) {
                    atomicAdd(
                        &unified_profile
                             ->unified_uc14_hi_residual_evaluations,
                        1ULL);
                }
            }
            if (monotonic && lo_valid && hi_valid) {
                if (unified_profile) {
                    atomicAdd(
                        &unified_profile->unified_interval_monotonic_nodes,
                        1ULL);
                }
                if (f_lo * f_hi > 0.0 &&
                    (strict_unified || fabs(f_hi) > residual_tol)) {
                    no_root = true;
                } else if (strict_unified
                               ? f_lo * f_hi < 0.0
                               : f_lo * f_hi <= 0.0) {
                    if (unified_profile) {
                        atomicAdd(
                            &unified_profile->unified_interval_hybrid_solves,
                            1ULL);
                    }
                    if (bisect_asphere_residual_root(
                               surface,
                               px,
                               py,
                               pz,
                               lx,
                               my,
                               nz,
                               lo,
                               hi,
                               f_lo,
                               strict_unified ? 0.0 : newton_tol,
                               root,
                               strict_unified ? &bracket_diagnostic : nullptr,
                               f_hi,
                               uc14_known_hi,
                               strict_unified
                                   ? fmin(
                                         fabs(derivative_range.lo),
                                         fabs(derivative_range.hi))
                                   : 0.0,
                               uc14_position_certificate,
                               strict_unified ? unified_profile : nullptr,
                               1)) {
                        return finish(DeviceFirstRootSearchStatus::Found);
                    }
                }
            }

            if (!no_root) {
                const double width_tol = 1.0e-11 *
                    fmax(1.0, fmax(fabs(lo), fabs(hi)));
                if (depth >= kMaxDepth || hi - lo <= width_tol) {
                    if (strict_unified) {
                        if (!hi_evaluated) {
                            hi_valid = surface_residual_at<true>(
                                surface,
                                px,
                                py,
                                pz,
                                lx,
                                my,
                                nz,
                                hi,
                                &f_hi);
                            hi_evaluated = true;
                            if (unified_profile) {
                                atomicAdd(
                                    &unified_profile
                                         ->unified_uc14_hi_residual_evaluations,
                                    1ULL);
                            }
                        }
                        if (lo_valid && hi_valid && f_lo * f_hi < 0.0 &&
                            bisect_asphere_residual_root(
                                surface,
                                px,
                                py,
                                pz,
                                lx,
                                my,
                                nz,
                                lo,
                                hi,
                                f_lo,
                                0.0,
                                root,
                                &bracket_diagnostic,
                                f_hi,
                                uc14_known_hi,
                                0.0,
                                false,
                                unified_profile,
                                1)) {
                            return finish(
                                DeviceFirstRootSearchStatus::Found);
                        }
                        double residual_lo = 0.0;
                        double residual_hi = 0.0;
                        double derivative_lo = 0.0;
                        double derivative_hi = 0.0;
                        if (surface_residual_and_derivative_at(
                                surface,
                                px,
                                py,
                                pz,
                                lx,
                                my,
                                nz,
                                lo,
                                &residual_lo,
                                &derivative_lo) &&
                            surface_residual_and_derivative_at(
                                surface,
                                px,
                                py,
                                pz,
                                lx,
                                my,
                                nz,
                                hi,
                                &residual_hi,
                                &derivative_hi) &&
                            derivative_lo * derivative_hi <= 0.0) {
                            double stationary_lo = lo;
                            double stationary_hi = hi;
                            double derivative_at_lo = derivative_lo;
                            for (int iteration = 0;
                                 iteration < 64;
                                 ++iteration) {
                                const double stationary_mid =
                                    0.5 * (stationary_lo + stationary_hi);
                                double stationary_residual = 0.0;
                                double derivative_mid = 0.0;
                                if (!surface_residual_and_derivative_at(
                                        surface,
                                        px,
                                        py,
                                        pz,
                                        lx,
                                        my,
                                        nz,
                                        stationary_mid,
                                        &stationary_residual,
                                        &derivative_mid)) {
                                    return finish(
                                        DeviceFirstRootSearchStatus::Ambiguous);
                                }
                                if (derivative_at_lo * derivative_mid <= 0.0) {
                                    stationary_hi = stationary_mid;
                                } else {
                                    stationary_lo = stationary_mid;
                                    derivative_at_lo = derivative_mid;
                                }
                            }
                            const double stationary =
                                0.5 * (stationary_lo + stationary_hi);
                            double stationary_residual = 0.0;
                            if (surface_residual_at<true>(
                                    surface,
                                    px,
                                    py,
                                    pz,
                                    lx,
                                    my,
                                    nz,
                                    stationary,
                                    &stationary_residual)) {
                                const double stationary_x =
                                    fma(stationary, lx, px);
                                const double stationary_y =
                                    fma(stationary, my, py);
                                const double stationary_z =
                                    fma(stationary, nz, pz);
                                const double stationary_scale = fmax(
                                    1.0,
                                    fmax(
                                        fabs(stationary_z),
                                        hypot(stationary_x, stationary_y)));
                                const double stationary_tolerance =
                                    128.0 * DBL_EPSILON * stationary_scale;
                                if (fabs(stationary_residual) <=
                                    stationary_tolerance) {
                                    *root = stationary;
                                    return finish(
                                        DeviceFirstRootSearchStatus::Found);
                                }
                            }
                        }
                        return finish(
                            DeviceFirstRootSearchStatus::Ambiguous);
                    }
                    const double mid = 0.5 * (lo + hi);
                    double best_seed = mid;
                    double best_residual = INFINITY;
                    const double seeds[3] = {lo, mid, hi};
                    #pragma unroll
                    for (int i = 0; i < 3; ++i) {
                        double value = 0.0;
                        if (surface_residual_at<true>(
                                surface,
                                px,
                                py,
                                pz,
                                lx,
                                my,
                                nz,
                                seeds[i],
                                &value) &&
                            fabs(value) < best_residual) {
                            best_seed = seeds[i];
                            best_residual = fabs(value);
                        }
                    }
                    double refined = NAN;
                    if (refine_even_asphere_root_bounded(
                            surface,
                            px,
                            py,
                            pz,
                            lx,
                            my,
                            nz,
                            best_seed,
                            lo,
                            hi,
                            newton_tol,
                            &refined)) {
                        *root = refined;
                        return finish(DeviceFirstRootSearchStatus::Found);
                    }
                    return finish(DeviceFirstRootSearchStatus::Ambiguous);
                }

                const double mid = 0.5 * (lo + hi);
                if (uc14_defer_hi && unified_profile && !hi_evaluated) {
                    atomicAdd(
                        &unified_profile
                             ->unified_uc14_hi_deferred_split_skips,
                        1ULL);
                }
                if (!(mid > lo) || !(hi > mid) ||
                    pending_count >= kStackCapacity) {
                    return finish(DeviceFirstRootSearchStatus::Ambiguous);
                }
                pending_lo[pending_count] = mid;
                pending_hi[pending_count] = hi;
                pending_depth[pending_count] = depth + 1;
                ++pending_count;
                hi = mid;
                ++depth;
                continue;
            }
        }

        if (pending_count <= 0) {
            return finish(DeviceFirstRootSearchStatus::NoRoot);
        }
        --pending_count;
        lo = pending_lo[pending_count];
        hi = pending_hi[pending_count];
        depth = pending_depth[pending_count];
    }
}
} // namespace
} // namespace optiland::de_cuda_detail
