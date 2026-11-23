#pragma once
#include "interval_arithmetic_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline bool even_asphere_residual_and_derivative_uc10(
    const Surface& surface,
    const Ray& ray,
    double tv,
    double* residual,
    double* derivative) {
    if (!residual || !derivative || !std::isfinite(tv)) {
        return false;
    }
    const double x = ray.p.x + tv * ray.d.x;
    const double y = ray.p.y + tv * ray.d.y;
    const double z = ray.p.z + tv * ray.d.z;
    const double r2 = x * x + y * y;
    if (!std::isfinite(z) || !std::isfinite(r2)) {
        return false;
    }

    double sag = 0.0;
    double directional_slope = 0.0;
    if (std::isfinite(surface.radius) &&
        std::abs(surface.radius) >= kEps &&
        std::abs(surface.radius) <= 0.5 * kInf) {
        const double root_arg =
            1.0 - (1.0 + surface.conic) * r2 /
                      (surface.radius * surface.radius);
        if (!(root_arg > 0.0)) {
            return false;
        }
        const double root = std::sqrt(root_arg);
        const double sag_denom = surface.radius * (1.0 + root);
        const double derivative_denom = surface.radius * root;
        if (std::abs(sag_denom) < kEps ||
            std::abs(derivative_denom) < kEps) {
            return false;
        }
        sag = r2 / sag_denom;
        directional_slope =
            (x * ray.d.x + y * ray.d.y) / derivative_denom;
    }

    if (!surface.asphere_coefficients.empty()) {
        const std::size_t last = surface.asphere_coefficients.size() - 1;
        double polynomial = surface.asphere_coefficients[last];
        double polynomial_derivative = 0.0;
        for (std::size_t i = last; i > 0; --i) {
            polynomial_derivative =
                polynomial_derivative * r2 + polynomial;
            polynomial =
                polynomial * r2 + surface.asphere_coefficients[i - 1];
        }
        sag += r2 * polynomial;
        const double derivative_by_r2 =
            polynomial + r2 * polynomial_derivative;
        directional_slope +=
            2.0 * (x * ray.d.x + y * ray.d.y) * derivative_by_r2;
    }
    *residual = sag - z;
    *derivative = directional_slope - ray.d.z;
    return std::isfinite(*residual) && std::isfinite(*derivative);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool refine_even_asphere_seed(
    const Surface& surface,
    const Ray& ray,
    double seed,
    double lower,
    double upper,
    double* refined,
    bool strict_position ) {
    if (!refined || !std::isfinite(seed) || !std::isfinite(lower) ||
        !std::isfinite(upper) || lower > upper) {
        return false;
    }
    double tn = std::clamp(seed, lower, upper);
    const double residual_tol = std::max(kEps, surface.newton_tol * 10.0);
    const double step_bound = std::max(1.0, std::abs(tn) + 1.0);
    for (int iter = 0; iter < std::max(0, surface.newton_max_iter); ++iter) {
        double residual = 0.0;
        if (!even_asphere_residual_at(surface, ray, tn, &residual)) {
            return false;
        }
        if (!strict_position &&
            std::abs(residual) <= surface.newton_tol) {
            break;
        }
        double derivative = 0.0;
        if (!even_asphere_derivative_at(surface, ray, tn, &derivative)) {
            return false;
        }
        if (std::abs(derivative) <= kEps) {
            if (strict_position && std::abs(residual) <= kEps) {
                break;
            }
            return false;
        }
        const double step = std::clamp(residual / derivative, -step_bound, step_bound);
        double next = tn - step;
        if (!std::isfinite(next)) {
            return false;
        }
        if (next < lower || next > upper) {
            next = 0.5 * (tn + (next < lower ? lower : upper));
        }
        const bool position_converged =
            strict_position &&
            std::abs(next - tn) <=
                1.0e-12 * std::max(1.0, std::abs(next));
        tn = next;
        if (position_converged) {
            break;
        }
    }
    double residual = 0.0;
    if (!even_asphere_residual_at(surface, ray, tn, &residual) ||
        std::abs(residual) > residual_tol || tn < lower || tn > upper) {
        return false;
    }
    *refined = std::abs(tn) <= 1.0e-10 ? 0.0 : tn;
    return std::isfinite(*refined);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool refine_even_asphere_seed_uc10(
    const Surface& surface,
    const Ray& ray,
    double seed,
    double lower,
    double upper,
    double* refined) {
    if (!refined || !std::isfinite(seed) || !std::isfinite(lower) ||
        !std::isfinite(upper) || lower > upper) {
        return false;
    }
    double tn = std::clamp(seed, lower, upper);
    const double residual_tol = std::max(kEps, surface.newton_tol * 10.0);
    const double step_bound = std::max(1.0, std::abs(tn) + 1.0);
    for (int iter = 0; iter < std::max(0, surface.newton_max_iter); ++iter) {
        double residual = 0.0;
        double derivative = 0.0;
        if (!even_asphere_residual_and_derivative_uc10(
                surface, ray, tn, &residual, &derivative)) {
            return false;
        }
        if (std::abs(residual) <= surface.newton_tol) {
            break;
        }
        if (std::abs(derivative) <= kEps) {
            return false;
        }
        const double step =
            std::clamp(residual / derivative, -step_bound, step_bound);
        double next = tn - step;
        if (!std::isfinite(next)) {
            return false;
        }
        if (next < lower || next > upper) {
            next = 0.5 * (tn + (next < lower ? lower : upper));
        }
        tn = next;
    }
    double residual = 0.0;
    double derivative = 0.0;
    if (!even_asphere_residual_and_derivative_uc10(
            surface, ray, tn, &residual, &derivative) ||
        std::abs(residual) > residual_tol || tn < lower || tn > upper) {
        return false;
    }
    *refined = std::abs(tn) <= 1.0e-10 ? 0.0 : tn;
    return std::isfinite(*refined);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool intersect_even_asphere_from_local_seed(
    const Surface& surface,
    const Ray& ray,
    double seed,
    double* root) {
    if (!root || surface.kind != SurfaceKind::EvenAsphere ||
        !std::isfinite(seed) || seed < 0.0) {
        return false;
    }
    const double upper = seed + std::max(1.0, std::abs(seed) + 1.0);
    return refine_even_asphere_seed(surface, ray, seed, 0.0, upper, root);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool solve_even_asphere_bracket(
    const Surface& surface,
    const Ray& ray,
    double lo,
    double hi,
    double f_lo,
    double f_hi,
    double* root,
    bool strict_residual ) {
    if (!root || !std::isfinite(f_lo) || !std::isfinite(f_hi) || f_lo * f_hi > 0.0) {
        return false;
    }
    const double residual_tol = strict_residual
                                    ? kEps
                                    : std::max(
                                          kEps,
                                          surface.newton_tol * 10.0);
    for (int iter = 0; iter < 80; ++iter) {
        const double mid = 0.5 * (lo + hi);
        double f_mid = 0.0;
        if (!even_asphere_residual_at(surface, ray, mid, &f_mid)) {
            return false;
        }
        if ((!strict_residual && std::abs(f_mid) <= residual_tol) ||
            hi - lo <= 1.0e-12 * std::max(1.0, std::abs(mid))) {
            *root = mid;
            return true;
        }
        double derivative = 0.0;
        double trial = mid;
        if (even_asphere_derivative_at(surface, ray, mid, &derivative) &&
            std::abs(derivative) > kEps) {
            const double newton = mid - f_mid / derivative;
            if (newton > lo && newton < hi) {
                trial = newton;
            }
        }
        double f_trial = f_mid;
        if (trial != mid && !even_asphere_residual_at(surface, ray, trial, &f_trial)) {
            trial = mid;
            f_trial = f_mid;
        }
        if (!strict_residual && std::abs(f_trial) <= residual_tol) {
            *root = trial;
            return true;
        }
        if (f_lo * f_trial <= 0.0) {
            hi = trial;
            f_hi = f_trial;
        } else {
            lo = trial;
            f_lo = f_trial;
        }
    }
    const double candidate = 0.5 * (lo + hi);
    double residual = 0.0;
    if (!even_asphere_residual_at(surface, ray, candidate, &residual) ||
        std::abs(residual) > residual_tol) {
        return false;
    }
    *root = candidate;
    return true;
}
} // namespace raytrace_detail

} // namespace optiland
