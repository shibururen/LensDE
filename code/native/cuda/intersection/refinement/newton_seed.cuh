#pragma once
#include "../prelude.cuh"
#include "../geometry/sag_residual.cuh"

namespace optiland::de_cuda_detail {
namespace {
template <bool IncludeAsphere>
__device__ bool surface_residual_at(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double tv,
    double* residual) {
    if (!residual || !finite_value(tv)) {
        return false;
    }
    const double xi = px + tv * lx;
    const double yi = py + tv * my;
    const double zi = pz + tv * nz;
    const double r2 = fma(xi, xi, yi * yi);
    const double sag = surface_sag_from_r2<IncludeAsphere>(surface, r2);
    if (!finite_value(zi) || !finite_value(sag)) {
        return false;
    }
    *residual = sag - zi;
    return finite_value(*residual);
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ void consider_surface_branch_root(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double tv,
    double* best_t,
    double* best_residual) {
    constexpr double kRootTEps = 1.0e-10;
    if (!best_t || !best_residual || !finite_value(tv) || tv < -kRootTEps) {
        return;
    }
    double residual = 0.0;
    if (!surface_residual_at<false>(
            surface, px, py, pz, lx, my, nz, tv, &residual)) {
        return;
    }
    const double xi = px + tv * lx;
    const double yi = py + tv * my;
    const double zi = pz + tv * nz;
    const double sag = zi + residual;
    const double scale =
        fmax(1.0, fmax(fabs(zi), fmax(fabs(sag), hypot(xi, yi))));
    const double radius_scale =
        finite_value(surface.radius) && fabs(surface.radius) <= 0.5 * kDeviceInf
            ? fmax(1.0, fabs(surface.radius))
            : 1.0;
    const double tol_scale = fmax(scale, radius_scale);
    const double branch_tol =
        fmax(64.0 * kDeviceEps * tol_scale, 1.0e-10 * tol_scale);
    if (fabs(residual) > branch_tol) {
        return;
    }
    const double tv_score = fmax(0.0, tv);
    const double best_score =
        finite_value(*best_t) ? fmax(0.0, *best_t) : INFINITY;
    if (!finite_value(*best_t) || tv_score < best_score - kRootTEps ||
        (fabs(tv_score - best_score) <= kRootTEps &&
         fabs(residual) < fabs(*best_residual))) {
        *best_t = tv;
        *best_residual = residual;
    }
}
} // namespace
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
namespace {
__device__ bool refine_even_asphere_root(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double start_t,
    double newton_tol,
    double* refined_t) {
    if (!refined_t || !finite_value(start_t) || start_t < -kDeviceEps) {
        return false;
    }
    const double dz_safe =
        fabs(nz) > kDeviceEps ? nz : (nz >= 0.0 ? kDeviceEps : -kDeviceEps);
    double tn = start_t;
    double initial_residual = 0.0;
    if (!surface_residual_at<true>(
            surface, px, py, pz, lx, my, nz, tn, &initial_residual)) {
        return false;
    }
    bool bracketed = false;
    if (tn > 0.0 && fabs(initial_residual) > 1.0e-2) {
        double lo = 0.0;
        double f_lo = 0.0;
        if (surface_residual_at<true>(
                surface, px, py, pz, lx, my, nz, lo, &f_lo)) {
            constexpr int kBracketSegments = 16;
            for (int segment = 1; segment <= kBracketSegments; ++segment) {
                const double hi =
                    start_t * static_cast<double>(segment) / kBracketSegments;
                double f_hi = 0.0;
                if (!surface_residual_at<true>(
                        surface, px, py, pz, lx, my, nz, hi, &f_hi)) {
                    continue;
                }
                if (f_lo == 0.0 || f_lo * f_hi <= 0.0) {
                    double left = lo;
                    double right = hi;
                    double f_left = f_lo;
                    for (int iter = 0; iter < 24; ++iter) {
                        const double mid = 0.5 * (left + right);
                        double f_mid = 0.0;
                        if (!surface_residual_at<true>(
                                surface, px, py, pz, lx, my, nz, mid, &f_mid)) {
                            right = mid;
                            continue;
                        }
                        if (f_left * f_mid <= 0.0) {
                            right = mid;
                        } else {
                            left = mid;
                            f_left = f_mid;
                        }
                    }
                    tn = 0.5 * (left + right);
                    bracketed = true;
                    break;
                }
                lo = hi;
                f_lo = f_hi;
            }
        }
    }
    if (!bracketed &&
        ((initial_residual * nz) < 0.0 || fabs(initial_residual) > 1.0e-2)) {
        const bool overshoot = (initial_residual * nz) < 0.0;
        const double t_extension = (initial_residual / dz_safe) * 2.0;
        double t_safe = overshoot ? 0.0 : tn;
        double t_risk = overshoot ? tn : tn + t_extension;
        if (finite_value(t_safe) && finite_value(t_risk)) {
            for (int iter = 0; iter < 10; ++iter) {
                const double t_mid = 0.5 * (t_safe + t_risk);
                double mid_residual = 0.0;
                const bool mid_valid = surface_residual_at<true>(
                    surface, px, py, pz, lx, my, nz, t_mid, &mid_residual);
                const bool mid_overshoot =
                    !mid_valid || (mid_residual * nz) < 0.0;
                if (mid_overshoot) {
                    t_risk = t_mid;
                } else {
                    t_safe = t_mid;
                }
            }
            tn = t_safe;
        }
    }

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
        if (fabs(residual) < newton_tol) {
            final_residual = residual;
            residual_certified = true;
            break;
        }
        if (fabs(derivative) <= kDeviceEps) {
            derivative = derivative >= 0.0 ? kDeviceEps : -kDeviceEps;
        }
        const double step = residual / derivative;
        tn -= clamp_value(step, -step_bound, step_bound);
    }

    const double final_tol = fmax(kDeviceEps, newton_tol * 10.0);
    if ((!residual_certified &&
         !surface_residual_at<true>(
             surface, px, py, pz, lx, my, nz, tn, &final_residual)) ||
        fabs(final_residual) > final_tol || tn < -kDeviceEps) {
        return false;
    }
    *refined_t = fabs(tn) <= kDeviceEps ? 0.0 : tn;
    return finite_value(*refined_t);
}
} // namespace
} // namespace optiland::de_cuda_detail
