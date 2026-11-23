#pragma once
#include "../prelude.cuh"
#include "../geometry/sag_residual.cuh"
#include "newton_seed.cuh"

namespace optiland::de_cuda_detail {
namespace {
__device__ bool refine_even_asphere_root_fast(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double start_t,
    double newton_tol,
    double* refined_t,
    DeviceAsphereCandidateDiagnostic* diagnostic = nullptr,
    DeviceAsphereSeedAttemptDiagnostic* seed_diagnostic = nullptr) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    diagnostic = nullptr;
    seed_diagnostic = nullptr;
#endif
    if (!refined_t || !finite_value(start_t) || start_t < -kDeviceEps) {
        return false;
    }
    double tn = fmax(0.0, start_t);
    const double step_bound = fmax(1.0, fabs(tn) + 1.0);
    double final_residual = INFINITY;
    bool residual_certified = false;
    double certified_derivative = 0.0;
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    constexpr int triage_mode = 0;
#else
    const int triage_mode = surface.asphere_polynomial_eval_mode & 12;
#endif
    const bool observe_triage = seed_diagnostic || triage_mode != 0;
    double first_abs_residual = INFINITY;
    double best_abs_residual = INFINITY;
    int clamped_steps = 0;
    for (int iter = 0; iter < max(0, surface.newton_max_iter); ++iter) {
        double residual = 0.0;
        double derivative = 0.0;
        if (diagnostic) {
            ++diagnostic->newton_evaluations;
        }
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
        const double abs_residual = fabs(residual);
        if (observe_triage) {
            if (iter == 0) {
                first_abs_residual = abs_residual;
                best_abs_residual = abs_residual;
                if (seed_diagnostic) {
                    seed_diagnostic->first_step_observed = 1;
                }
            } else {
                best_abs_residual = fmin(best_abs_residual, abs_residual);
                if (iter == 1 && seed_diagnostic &&
                    finite_value(first_abs_residual) &&
                    first_abs_residual > 0.0) {
                    const double ratio = abs_residual / first_abs_residual;
                    seed_diagnostic->second_residual_ratio_bucket =
                        ratio <= 0.1 ? 0
                        : ratio <= 0.5 ? 1
                        : ratio <= 0.9 ? 2
                        : ratio <= 1.1 ? 3
                                       : 4;
                }
            }
        }
        if (abs_residual < newton_tol) {
            final_residual = residual;
            certified_derivative = derivative;
            residual_certified = true;
            break;
        }
        int proposed_rule = -1;
        if (observe_triage && iter == 3 && clamped_steps == 3 &&
            finite_value(first_abs_residual) && first_abs_residual > 0.0 &&
            best_abs_residual >= 0.5 * first_abs_residual) {
            proposed_rule = 0;
        } else if (observe_triage && iter == 7 &&
                   finite_value(first_abs_residual) &&
                   first_abs_residual > 0.0 &&
                   best_abs_residual >= 0.25 * first_abs_residual) {
            proposed_rule = 1;
        }
        if (proposed_rule >= 0) {
            const int rule_bit = 1 << proposed_rule;
            if (seed_diagnostic) {
                seed_diagnostic->triage_trigger_mask |= rule_bit;
                seed_diagnostic->triage_trigger_evaluations[proposed_rule] =
                    iter + 1;
            }
            const int enable_bit = proposed_rule == 0 ? 4 : 8;
            if ((triage_mode & enable_bit) != 0) {
                if (seed_diagnostic) {
                    seed_diagnostic->triage_actual_abort_mask |= rule_bit;
                }
                return false;
            }
        }
        const bool derivative_guarded = fabs(derivative) <= kDeviceEps;
        if (fabs(derivative) <= kDeviceEps) {
            derivative = derivative >= 0.0 ? kDeviceEps : -kDeviceEps;
        }
        const double step = residual / derivative;
        if (iter == 0 && seed_diagnostic && derivative_guarded) {
            seed_diagnostic->first_step_class |= 1;
        }
        if (!finite_value(step)) {
            if (iter == 0 && seed_diagnostic) {
                seed_diagnostic->first_step_class |= 2;
            }
            return false;
        }
        if (iter == 0 && seed_diagnostic) {
            const double step_ratio = fabs(step) / step_bound;
            seed_diagnostic->first_step_ratio_bucket =
                step_ratio <= 0.25 ? 0
                : step_ratio <= 0.75 ? 1
                : step_ratio <= 1.0 ? 2
                : step_ratio <= 2.0 ? 3
                                    : 4;
        }
        const bool step_clamped = fabs(step) > step_bound;
        if (step_clamped) {
            ++clamped_steps;
            if (iter == 0 && seed_diagnostic) {
                seed_diagnostic->first_step_class |= 4;
            }
        }
        const double next_t = tn - clamp_value(step, -step_bound, step_bound);
        if (next_t < -kDeviceEps || !finite_value(next_t)) {
            if (iter == 0 && seed_diagnostic) {
                seed_diagnostic->first_step_class |= 8;
            }
            return false;
        }
        tn = next_t;
    }
    const double final_tol = fmax(kDeviceEps, newton_tol * 10.0);
    bool terminal_residual_valid = true;
    if (!residual_certified) {
        if (diagnostic) {
            ++diagnostic->terminal_residual_evaluations;
        }
        terminal_residual_valid = surface_residual_at<true>(
            surface, px, py, pz, lx, my, nz, tn, &final_residual);
    }
    if (!terminal_residual_valid ||
        fabs(final_residual) > final_tol || tn < -kDeviceEps) {
        return false;
    }
    *refined_t = fabs(tn) <= kDeviceEps ? 0.0 : tn;
    const bool success = finite_value(*refined_t);
    if (success && residual_certified && diagnostic) {
        diagnostic->cacheable_success = 1;
        diagnostic->cached_t = *refined_t;
        diagnostic->cached_residual = final_residual;
        diagnostic->cached_derivative = certified_derivative;
    }
    return success;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ bool certify_asphere_residual_monotonic(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double nz,
    double lower,
    double upper) {
    if (!(lower >= 0.0) || !(upper >= lower) ||
        !finite_value(lower) || !finite_value(upper)) {
        return false;
    }
    const double x0 = fma(lower, lx, px);
    const double y0 = fma(lower, my, py);
    const double x1 = fma(upper, lx, px);
    const double y1 = fma(upper, my, py);
    const double radius0_squared = fma(x0, x0, y0 * y0);
    const double radius1_squared = fma(x1, x1, y1 * y1);
    const double max_radius_squared = fmax(radius0_squared, radius1_squared);
    if (!finite_value(max_radius_squared) || max_radius_squared < 0.0) {
        return false;
    }
    const double max_radius = sqrt(max_radius_squared);
    double radial_slope_bound = 0.0;
    if (finite_value(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
        fabs(surface.radius) <= 0.5 * kDeviceInf) {
        const double root_arg =
            1.0 - (1.0 + surface.conic) * max_radius_squared /
                      (surface.radius * surface.radius);
        if (!(root_arg > 0.0)) {
            return false;
        }
        radial_slope_bound +=
            max_radius / (fabs(surface.radius) * sqrt(root_arg));
    }
    double polynomial_slope_bound = 0.0;
    const int count = max(
        0, min(surface.coeff_count, kMaxDeviceAsphereCoefficients));
    #pragma unroll
    for (int i = kMaxDeviceAsphereCoefficients - 1; i >= 0; --i) {
        if (i < count) {
            polynomial_slope_bound = fma(
                polynomial_slope_bound,
                max_radius_squared,
                static_cast<double>(i + 1) * fabs(surface.coeffs[i]));
        }
    }
    radial_slope_bound = fma(
        2.0 * max_radius, polynomial_slope_bound, radial_slope_bound);
    const double transverse_speed_squared = fma(lx, lx, my * my);
    if (!finite_value(radial_slope_bound) ||
        !finite_value(transverse_speed_squared) || transverse_speed_squared < 0.0) {
        return false;
    }
    const double transverse_bound_squared =
        radial_slope_bound * radial_slope_bound * transverse_speed_squared;
    if (!finite_value(transverse_bound_squared) ||
        transverse_bound_squared < 0.0) {
        return false;
    }
    // A certificate can only succeed when the transverse bound is below
    // |nz|, so the original max(1, |nz|, transverse_bound) guard reduces to
    // max(1, |nz|).  Comparing squared values avoids one FP64 square root.
    // The doubled guard also covers the few extra rounded multiplications in
    // the squared comparison, keeping this a conservative sufficient test.
    const double abs_nz = fabs(nz);
    const double guard =
        256.0 * 2.2204460492503131e-16 * fmax(1.0, abs_nz);
    const double guarded_nz = abs_nz - guard;
    return guarded_nz > 0.0 &&
           guarded_nz * guarded_nz > transverse_bound_squared;
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
// Defined after the interval primitives.  Keeping the interface scalar avoids
// exposing DeviceScalarInterval in the strict bracket solver and keeps the
// UC14 point enclosure in a separate device helper.
__device__ __noinline__ bool outward_point_residual_abs_upper(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double t,
    double* abs_upper);
} // namespace
} // namespace optiland::de_cuda_detail
