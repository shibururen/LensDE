#pragma once
#include "asphere_refinement_inline.hpp"
#include "interval_arithmetic_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline bool production_asphere_requires_topology_seed(
    const Surface& surface,
    double domain_radius_squared) {
    if (surface.asphere_coefficients.size() < 2 ||
        !std::isfinite(domain_radius_squared) ||
        domain_radius_squared < 0.0) {
        return false;
    }
    const double fourth_order_departure =
        std::abs(surface.asphere_coefficients[1]) *
        domain_radius_squared * domain_radius_squared;
    return std::isfinite(fourth_order_departure) &&
           fourth_order_departure > 0.25;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool production_ray_requires_topology_seed(
    const Surface& surface,
    const Ray& ray,
    double domain_radius_squared) {
    if (!production_asphere_requires_topology_seed(
            surface, domain_radius_squared)) {
        return false;
    }
    const double transverse_speed_squared =
        ray.d.x * ray.d.x + ray.d.y * ray.d.y;
    if (!std::isfinite(transverse_speed_squared) ||
        transverse_speed_squared < 0.0) {
        return false;
    }
    if (transverse_speed_squared <= std::numeric_limits<double>::min()) {
        return true;
    }
    const double radius_squared =
        ray.p.x * ray.p.x + ray.p.y * ray.p.y;
    if (!std::isfinite(radius_squared) ||
        radius_squared < 0.36 * domain_radius_squared ||
        radius_squared > 0.5776 * domain_radius_squared) {
        return false;
    }
    const double radial_direction =
        ray.p.x * ray.d.x + ray.p.y * ray.d.y;
    if (!(radial_direction > 0.0) ||
        !(transverse_speed_squared > ray.d.z * ray.d.z)) {
        return false;
    }
    const double cross = ray.p.x * ray.d.y - ray.p.y * ray.d.x;
    const double impact_scaled_squared = cross * cross;
    const double allowed_scaled_squared =
        (1.0 / 16.0) * domain_radius_squared * transverse_speed_squared;
    return std::isfinite(impact_scaled_squared) &&
           std::isfinite(allowed_scaled_squared) &&
           impact_scaled_squared <= allowed_scaled_squared;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool refine_production_asphere_root_fast(
    const Surface& surface,
    const Ray& ray,
    double start_t,
    double* root) {
    if (!root || !std::isfinite(start_t) || start_t < -kEps) {
        return false;
    }
    const double newton_tol =
        std::isfinite(surface.newton_tol) && surface.newton_tol > 0.0
            ? surface.newton_tol
            : 1.0e-10;
    double tn = std::max(0.0, start_t);
    const double step_bound = std::max(1.0, std::abs(tn) + 1.0);
    double final_residual = std::numeric_limits<double>::infinity();
    bool residual_certified = false;
    for (int iteration = 0;
         iteration < std::max(0, surface.newton_max_iter);
         ++iteration) {
        double residual = 0.0;
        double derivative = 0.0;
        if (!even_asphere_residual_at(surface, ray, tn, &residual) ||
            !even_asphere_derivative_at(surface, ray, tn, &derivative)) {
            return false;
        }
        if (std::abs(residual) < newton_tol) {
            final_residual = residual;
            residual_certified = true;
            break;
        }
        if (std::abs(derivative) <= kEps) {
            derivative = derivative >= 0.0 ? kEps : -kEps;
        }
        const double step = residual / derivative;
        if (!std::isfinite(step)) {
            return false;
        }
        tn -= std::clamp(step, -step_bound, step_bound);
        if (tn < -kEps || !std::isfinite(tn)) {
            return false;
        }
    }
    const double final_tol = std::max(kEps, newton_tol * 10.0);
    if ((!residual_certified &&
         !even_asphere_residual_at(surface, ray, tn, &final_residual)) ||
        std::abs(final_residual) > final_tol || tn < -kEps) {
        return false;
    }
    *root = std::abs(tn) <= kEps ? 0.0 : tn;
    return std::isfinite(*root);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool production_topology_linear_seed(
    const Surface& surface,
    const Ray& ray,
    double search_lower,
    double search_upper,
    double* root) {
    if (!root || !std::isfinite(search_lower) ||
        !std::isfinite(search_upper) || search_upper <= search_lower) {
        return false;
    }
    double residual = 0.0;
    double derivative = 0.0;
    if (!even_asphere_residual_at(surface, ray, search_lower, &residual) ||
        !even_asphere_derivative_at(surface, ray, search_lower, &derivative)) {
        return false;
    }
    const double residual_tol = std::max(kEps, surface.newton_tol * 10.0);
    if (std::abs(residual) <= residual_tol) {
        *root = std::abs(search_lower) <= kEps ? 0.0 : search_lower;
        return true;
    }
    const double derivative_guard =
        64.0 * std::numeric_limits<double>::epsilon() *
        std::max(1.0, std::abs(derivative));
    if (!(residual > residual_tol && derivative < -derivative_guard)) {
        return false;
    }
    const double seed = search_lower - residual / derivative;
    if (!std::isfinite(seed) || seed <= search_lower || seed > search_upper) {
        return false;
    }
    return refine_production_asphere_root_fast(
        surface, ray, seed, root);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool production_topology_seeded_before(
    const Surface& surface,
    const Ray& ray,
    double search_lower,
    double candidate,
    double* root) {
    if (!root || !std::isfinite(search_lower) ||
        !std::isfinite(candidate) || candidate <= search_lower) {
        return false;
    }
    double previous_t = search_lower;
    double previous_f = 0.0;
    double previous_d = 0.0;
    if (!even_asphere_residual_at(surface, ray, previous_t, &previous_f) ||
        !even_asphere_derivative_at(surface, ray, previous_t, &previous_d)) {
        return false;
    }
    const double residual_tol = std::max(kEps, surface.newton_tol * 10.0);
    if (std::abs(previous_f) <= residual_tol) {
        *root = std::abs(previous_t) <= kEps ? 0.0 : previous_t;
        return true;
    }
    const double derivative_guard =
        64.0 * std::numeric_limits<double>::epsilon() *
        std::max(1.0, std::abs(previous_d));
    if (!(previous_f > residual_tol && previous_d < -derivative_guard)) {
        return false;
    }
    constexpr double kFractions[2] = {0.5, 0.75};
    for (double fraction : kFractions) {
        const double sample_t =
            std::fma(fraction, candidate - search_lower, search_lower);
        double sample_f = 0.0;
        if (!even_asphere_residual_at(surface, ray, sample_t, &sample_f)) {
            return false;
        }
        if (previous_f * sample_f <= 0.0) {
            return solve_even_asphere_bracket(
                surface,
                ray,
                previous_t,
                sample_t,
                previous_f,
                sample_f,
                root);
        }
        previous_t = sample_t;
        previous_f = sample_f;
    }
    return false;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline int collect_production_precondition_seeds(
    const Surface& surface,
    const Ray& ray,
    double* seeds) {
    if (!seeds) {
        return 0;
    }
    int count = 0;
    auto add_seed = [&](double value) {
        if (count < 3 && std::isfinite(value)) {
            seeds[count++] = value;
        }
    };
    const double dz_safe =
        std::abs(ray.d.z) > kEps
            ? ray.d.z
            : (ray.d.z >= 0.0 ? kEps : -kEps);
    add_seed(-ray.p.z / dz_safe);
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps &&
        std::abs(surface.radius) <= 0.5 * kInf) {
        const double a =
            surface.conic * ray.d.z * ray.d.z + dot(ray.d, ray.d);
        const double b =
            2.0 * surface.conic * ray.d.z * ray.p.z +
            2.0 * ray.d.x * ray.p.x + 2.0 * ray.d.y * ray.p.y -
            2.0 * ray.d.z * surface.radius +
            2.0 * ray.d.z * ray.p.z;
        const double c =
            surface.conic * ray.p.z * ray.p.z -
            2.0 * surface.radius * ray.p.z + dot(ray.p, ray.p);
        if (std::abs(a) <= kEps) {
            if (std::abs(b) > kEps) {
                add_seed(-c / b);
            }
        } else {
            const double discriminant = b * b - 4.0 * a * c;
            if (discriminant >= 0.0) {
                const double root = std::sqrt(std::max(0.0, discriminant));
                const double q = -0.5 * (b + std::copysign(root, b));
                const double t1 =
                    std::abs(q) > kEps ? q / a : -b / (2.0 * a);
                const double t2 = std::abs(q) > kEps ? c / q : t1;
                add_seed(t1);
                add_seed(t2);
            }
        }
    }
    std::sort(seeds, seeds + count);
    return count;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool refine_production_preconditioned_seed(
    const Surface& surface,
    const Ray& ray,
    double raw_seed,
    double lower,
    double upper,
    double* root) {
    if (!root || !std::isfinite(raw_seed) || !std::isfinite(lower) ||
        !std::isfinite(upper) || lower < 0.0 || upper < lower) {
        return false;
    }
    constexpr int kSteps = 6;
    double seed = std::clamp(raw_seed, lower, upper);
    double initial_residual = 0.0;
    if (!even_asphere_residual_at(surface, ray, seed, &initial_residual)) {
        return false;
    }
    double newton_lower = lower;
    double newton_upper = upper;
    bool bracketed = false;
    if (seed > lower && std::abs(initial_residual) > 1.0e-2) {
        double scan_lo = lower;
        double f_lo = 0.0;
        if (even_asphere_residual_at(surface, ray, scan_lo, &f_lo)) {
            for (int segment = 1; segment <= 16; ++segment) {
                const double scan_hi =
                    lower + (seed - lower) *
                                static_cast<double>(segment) / 16.0;
                double f_hi = 0.0;
                if (!even_asphere_residual_at(surface, ray, scan_hi, &f_hi)) {
                    continue;
                }
                if ((f_lo <= 0.0 && f_hi >= 0.0) ||
                    (f_lo >= 0.0 && f_hi <= 0.0)) {
                    double left = scan_lo;
                    double right = scan_hi;
                    double f_left = f_lo;
                    for (int iteration = 0; iteration < kSteps; ++iteration) {
                        const double mid = 0.5 * (left + right);
                        double f_mid = 0.0;
                        if (!even_asphere_residual_at(
                                surface, ray, mid, &f_mid)) {
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
        ((initial_residual * ray.d.z) < 0.0 ||
         std::abs(initial_residual) > 1.0e-2)) {
        const bool overshoot = (initial_residual * ray.d.z) < 0.0;
        const double extension = (initial_residual /
            (std::abs(ray.d.z) > kEps
                 ? ray.d.z
                 : (ray.d.z >= 0.0 ? kEps : -kEps))) * 2.0;
        double safe = overshoot ? lower : seed;
        double risk = overshoot
                          ? seed
                          : std::clamp(seed + extension, lower, upper);
        if (risk < safe) {
            std::swap(risk, safe);
        }
        if (std::isfinite(safe) && std::isfinite(risk) && risk > safe) {
            for (int iteration = 0; iteration < kSteps; ++iteration) {
                const double mid = 0.5 * (safe + risk);
                double f_mid = 0.0;
                const bool valid =
                    even_asphere_residual_at(surface, ray, mid, &f_mid);
                if (!valid || (f_mid * ray.d.z) < 0.0) {
                    risk = mid;
                } else {
                    safe = mid;
                }
            }
            seed = safe;
        }
    }
    return refine_even_asphere_seed(
        surface, ray, seed, newton_lower, newton_upper, root);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool production_seed_preconditioned_no_b2(
    const Surface& surface,
    const Ray& ray,
    double lower,
    double upper,
    double* root) {
    if (!root || !std::isfinite(lower) || lower < 0.0 || upper < lower) {
        return false;
    }
    if (!std::isfinite(upper)) {
        const double transverse_squared =
            ray.d.x * ray.d.x + ray.d.y * ray.d.y;
        double f_lower = 0.0;
        if (!std::isfinite(transverse_squared) ||
            transverse_squared > 64.0 *
                std::numeric_limits<double>::epsilon() *
                std::numeric_limits<double>::epsilon() ||
            !even_asphere_residual_at(surface, ray, lower, &f_lower)) {
            return false;
        }
        const double axial_root =
            std::abs(ray.d.z) > kEps
                ? lower + f_lower / ray.d.z
                : lower;
        double final_residual = 0.0;
        if (std::isfinite(axial_root) && axial_root >= lower &&
            even_asphere_residual_at(
                surface, ray, axial_root, &final_residual) &&
            std::abs(final_residual) <=
                std::max(kEps, surface.newton_tol * 10.0)) {
            *root = std::abs(axial_root) <= 1.0e-10 ? 0.0 : axial_root;
            return true;
        }
        return false;
    }
    double seeds[3] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
    };
    const int seed_count =
        collect_production_precondition_seeds(surface, ray, seeds);
    for (int i = 0; i < seed_count; ++i) {
        if (refine_production_preconditioned_seed(
                surface, ray, seeds[i], lower, upper, root)) {
            return true;
        }
    }
    double scan_lo = lower;
    double f_lo = 0.0;
    if (even_asphere_residual_at(surface, ray, scan_lo, &f_lo)) {
        for (int segment = 1; segment <= 16; ++segment) {
            const double scan_hi =
                lower + (upper - lower) *
                            static_cast<double>(segment) / 16.0;
            double f_hi = 0.0;
            if (!even_asphere_residual_at(surface, ray, scan_hi, &f_hi)) {
                continue;
            }
            if ((f_lo <= 0.0 && f_hi >= 0.0) ||
                (f_lo >= 0.0 && f_hi <= 0.0)) {
                return refine_production_preconditioned_seed(
                    surface,
                    ray,
                    0.5 * (scan_lo + scan_hi),
                    scan_lo,
                    scan_hi,
                    root);
            }
            scan_lo = scan_hi;
            f_lo = f_hi;
        }
    }
    return false;
}
} // namespace raytrace_detail

} // namespace optiland
