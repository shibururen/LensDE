#pragma once
#include "../prelude.cuh"
#include "../geometry/sag_residual.cuh"
#include "../refinement/bounded_seed.cuh"
#include "../refinement/newton_seed.cuh"

namespace optiland::de_cuda_detail {
namespace {
__device__ __forceinline__ int uc14_certificate_iteration_histogram_index(
    int iteration) {
    if (iteration <= 1) {
        return 0;
    }
    if (iteration == 2) {
        return 1;
    }
    if (iteration <= 4) {
        return 2;
    }
    if (iteration <= 8) {
        return 3;
    }
    if (iteration <= 16) {
        return 4;
    }
    return 5;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ bool bisect_asphere_residual_root(
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
    double newton_tol,
    double* root,
    DeviceAsphereBracketDiagnostic* diagnostic = nullptr,
    double known_f_hi = NAN,
    bool use_known_f_hi = false,
    double certified_derivative_floor = 0.0,
    bool enable_position_certificate = false,
    DeviceAsphereRootProfile* profile = nullptr,
    int known_hi_origin = 1) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    diagnostic = nullptr;
    use_known_f_hi = false;
    certified_derivative_floor = 0.0;
    enable_position_certificate = false;
    profile = nullptr;
#endif
    if (diagnostic) {
        ++diagnostic->calls;
    }
    if (!root || !finite_value(f_lo)) {
        return false;
    }
    const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
    const bool strict_position = newton_tol <= 0.0;
    if (strict_position) {
        // The caller has already certified that the derivative interval does
        // not contain zero.  Keep the live sign bracket as the correctness
        // invariant, but use an in-bracket Newton step to contract it much
        // faster than fixed bisection.  A rounded zero is deliberately kept
        // as a bracket endpoint; strict mode returns only after the positional
        // width criterion is met.
        double f_hi = known_f_hi;
        bool hi_valid = use_known_f_hi && finite_value(f_hi);
        if (hi_valid) {
            if (profile && known_hi_origin >= 0 && known_hi_origin < 2) {
                atomicAdd(
                    &profile->unified_uc14_known_hi_reuses[known_hi_origin],
                    1ULL);
            }
        } else {
            if (diagnostic) {
                ++diagnostic->residual_evaluations;
            }
            hi_valid = surface_residual_at<true>(
                surface, px, py, pz, lx, my, nz, hi, &f_hi);
        }
        if (!hi_valid ||
            !(f_lo * f_hi < 0.0)) {
            return false;
        }
        const bool position_certificate_available =
            enable_position_certificate &&
            finite_value(certified_derivative_floor) &&
            certified_derivative_floor > 0.0;
        double current = 0.5 * (lo + hi);
        for (int iter = 0; iter < 80; ++iter) {
            double residual = 0.0;
            double derivative = 0.0;
            if (diagnostic) {
                ++diagnostic->residual_derivative_evaluations;
            }
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
            if (position_certificate_available) {
                const double position_tolerance =
                    1.0e-12 * fmax(1.0, fabs(current));
                const double proxy_position_error =
                    fabs(residual) / certified_derivative_floor;
                // The factor-two gate is scheduling only: it avoids a full
                // outward point interval until the scalar result is close
                // enough to have a realistic chance.  The actual certificate
                // below remains fully outward and uses the certified floor.
                if (finite_value(proxy_position_error) &&
                    proxy_position_error <= 2.0 * position_tolerance) {
                    if (profile) {
                        atomicAdd(
                            &profile->unified_uc14_position_proxy_attempts,
                            1ULL);
                        atomicAdd(
                            &profile
                                 ->unified_uc14_position_point_interval_evaluations,
                            1ULL);
                    }
                    double residual_abs_upper = INFINITY;
                    if (outward_point_residual_abs_upper(
                            surface,
                            px,
                            py,
                            pz,
                            lx,
                            my,
                            nz,
                            current,
                            &residual_abs_upper)) {
                        if (profile) {
                            atomicAdd(
                                &profile
                                     ->unified_uc14_position_point_interval_valid,
                                1ULL);
                        }
                        const double certified_position_error =
                            residual_abs_upper / certified_derivative_floor;
                        if (finite_value(certified_position_error) &&
                            certified_position_error <= position_tolerance) {
                            *root = current;
                            const bool success = finite_value(*root);
                            if (success && diagnostic) {
                                ++diagnostic->successes;
                            }
                            if (success && profile) {
                                atomicAdd(
                                    &profile
                                         ->unified_uc14_position_certificates,
                                    1ULL);
                                atomicAdd(
                                    &profile
                                         ->unified_uc14_position_certificate_iteration_sum,
                                    static_cast<unsigned long long>(iter + 1));
                                atomicAdd(
                                    &profile
                                         ->unified_uc14_position_certificate_iteration_hist[
                                             uc14_certificate_iteration_histogram_index(
                                                 iter + 1)],
                                    1ULL);
                            }
                            return success;
                        }
                    }
                }
            }
            if (residual == 0.0 ||
                signbit(f_lo) != signbit(residual)) {
                hi = current;
                f_hi = residual;
            } else {
                lo = current;
                f_lo = residual;
            }
            const double midpoint = 0.5 * (lo + hi);
            const double width = hi - lo;
            // UC1 already accepts a polished root after proving a
            // sign-changing bracket at +/-2.5e-10 of the same t scale.
            // A midpoint of this live, unique-root bracket has the identical
            // positional guarantee once its full width is at most 5e-10.
            if (!(width > 0.0) ||
                width <= 5.0e-10 * fmax(1.0, fabs(midpoint))) {
                *root = midpoint;
                const bool success = finite_value(*root);
                if (success && diagnostic) {
                    ++diagnostic->successes;
                }
                return success;
            }

            double next = midpoint;
            if (fabs(derivative) > kDeviceEps) {
                const double newton = current - residual / derivative;
                if (finite_value(newton) && newton > lo && newton < hi) {
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
    for (int iter = 0; iter < 80; ++iter) {
        const double mid = 0.5 * (lo + hi);
        double f_mid = 0.0;
        if (diagnostic) {
            ++diagnostic->residual_evaluations;
        }
        if (!surface_residual_at<true>(
                surface, px, py, pz, lx, my, nz, mid, &f_mid)) {
            return false;
        }
        if ((!strict_position && fabs(f_mid) <= residual_tol) ||
            hi - lo <= 1.0e-12 * fmax(1.0, fabs(mid))) {
            *root = mid;
            if (diagnostic) {
                ++diagnostic->successes;
            }
            return true;
        }
        if (f_lo * f_mid <= 0.0) {
            hi = mid;
        } else {
            lo = mid;
            f_lo = f_mid;
        }
    }
    const double candidate = 0.5 * (lo + hi);
    double residual = 0.0;
    if (diagnostic) {
        ++diagnostic->residual_evaluations;
    }
    if (!surface_residual_at<true>(
            surface, px, py, pz, lx, my, nz, candidate, &residual) ||
        fabs(residual) > residual_tol) {
        return false;
    }
    *root = candidate;
    const bool success = finite_value(*root);
    if (success && diagnostic) {
        ++diagnostic->successes;
    }
    return success;
}
} // namespace
} // namespace optiland::de_cuda_detail
