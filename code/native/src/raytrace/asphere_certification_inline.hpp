#pragma once
#include "asphere_refinement_inline.hpp"
#include "interval_arithmetic_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline FirstRootSearchStatus find_first_even_asphere_root_interval(
    const Surface& surface,
    const Ray& ray,
    double lo,
    double hi,
    int depth,
    FirstRootSearchBudget* budget,
    double* root,
    bool invalid_interval_is_ambiguous ) {
    if (!budget || !root || !(hi >= lo) || ++budget->nodes > budget->max_nodes) {
        return FirstRootSearchStatus::Ambiguous;
    }
    ScalarInterval residual_range =
        even_asphere_residual_interval(surface, ray, lo, hi);
    if (!residual_range.valid) {
        if (!invalid_interval_is_ambiguous) {
            return FirstRootSearchStatus::NoRoot;
        }
        // The conic expression can be undefined at the right edge of a
        // coarse interval even though its left prefix is physical and may
        // contain the first root.  Keep the unified traversal front-to-back
        // instead of turning one invalid endpoint into a whole-ray failure.
        const double width_tol =
            1.0e-11 *
            std::max(1.0, std::max(std::abs(lo), std::abs(hi)));
        if (depth >= 56 || hi - lo <= width_tol) {
            return FirstRootSearchStatus::Ambiguous;
        }
        const double mid = 0.5 * (lo + hi);
        if (!(mid > lo) || !(hi > mid)) {
            return FirstRootSearchStatus::Ambiguous;
        }
        const FirstRootSearchStatus left =
            find_first_even_asphere_root_interval(
                surface,
                ray,
                lo,
                mid,
                depth + 1,
                budget,
                root,
                invalid_interval_is_ambiguous);
        if (left != FirstRootSearchStatus::NoRoot) {
            return left;
        }
        return find_first_even_asphere_root_interval(
            surface,
            ray,
            mid,
            hi,
            depth + 1,
            budget,
            root,
            invalid_interval_is_ambiguous);
    }
    ScalarInterval unified_derivative_range;
    if (invalid_interval_is_ambiguous) {
        // Intersect the natural interval extension with the mean-value form
        // f(mid) + f'([lo,hi]) * ([lo,hi] - mid).  The latter preserves the
        // cancellation between sag(r(t)) and z(t), so a tangential double
        // root contracts quadratically instead of leaving O(1/sqrt(width))
        // false-positive cells for the left-first traversal.
        unified_derivative_range =
            even_asphere_derivative_interval(surface, ray, lo, hi);
        const double mid = 0.5 * (lo + hi);
        double f_mid = 0.0;
        if (unified_derivative_range.valid &&
            even_asphere_residual_at(surface, ray, mid, &f_mid)) {
            const ScalarInterval displacement =
                outward_interval(lo - mid, hi - mid);
            const ScalarInterval mean_value_range = interval_add(
                point_interval(f_mid),
                interval_multiply(
                    unified_derivative_range, displacement));
            if (mean_value_range.valid) {
                const double intersect_lo =
                    std::max(residual_range.lo, mean_value_range.lo);
                const double intersect_hi =
                    std::min(residual_range.hi, mean_value_range.hi);
                if (intersect_hi >= intersect_lo) {
                    residual_range =
                        {intersect_lo, intersect_hi, true};
                }
            }
        }
    }
    if (residual_range.lo > 0.0 || residual_range.hi < 0.0) {
        return FirstRootSearchStatus::NoRoot;
    }

    const double residual_tol = std::max(kEps, surface.newton_tol * 10.0);
    double f_lo = 0.0;
    double f_hi = 0.0;
    const bool lo_valid = even_asphere_residual_at(surface, ray, lo, &f_lo);
    const bool hi_valid = even_asphere_residual_at(surface, ray, hi, &f_hi);
    const double endpoint_scale = std::max(
        1.0,
        std::max(
            std::abs(ray.p.z + lo * ray.d.z),
            std::hypot(
                ray.p.x + lo * ray.d.x,
                ray.p.y + lo * ray.d.y)));
    const double endpoint_tol = invalid_interval_is_ambiguous
                                    ? 128.0 *
                                          std::numeric_limits<double>::epsilon() *
                                          endpoint_scale
                                    : residual_tol;
    bool lo_is_accurate_root = false;
    if (lo_valid && std::abs(f_lo) <= endpoint_tol) {
        if (!invalid_interval_is_ambiguous) {
            lo_is_accurate_root = true;
        } else {
            double endpoint_derivative = 0.0;
            if (even_asphere_derivative_at(
                    surface, ray, lo, &endpoint_derivative) &&
                std::abs(endpoint_derivative) > kEps) {
                const double correction =
                    (std::abs(f_lo) + endpoint_tol) /
                    std::abs(endpoint_derivative);
                lo_is_accurate_root =
                    correction <=
                    1.0e-11 * std::max(1.0, std::abs(lo));
            }
        }
    }
    if (lo_is_accurate_root) {
        *root = std::abs(lo) <= 1.0e-10 ? 0.0 : lo;
        return FirstRootSearchStatus::Found;
    }

    const ScalarInterval derivative_range = invalid_interval_is_ambiguous
        ? unified_derivative_range
        : even_asphere_derivative_interval(surface, ray, lo, hi);
    const bool monotonic =
        derivative_range.valid &&
        (derivative_range.lo > 0.0 || derivative_range.hi < 0.0);
    if (monotonic && lo_valid && hi_valid) {
        if (f_lo * f_hi > 0.0 &&
            (invalid_interval_is_ambiguous ||
             std::abs(f_hi) > residual_tol)) {
            return FirstRootSearchStatus::NoRoot;
        }
        const bool sign_bracket = invalid_interval_is_ambiguous
                                      ? f_lo * f_hi < 0.0
                                      : f_lo * f_hi <= 0.0;
        if (sign_bracket &&
            solve_even_asphere_bracket(
                surface,
                ray,
                lo,
                hi,
                f_lo,
                f_hi,
                root,
                invalid_interval_is_ambiguous)) {
            return FirstRootSearchStatus::Found;
        }
    }

    const double width_tol = 1.0e-11 * std::max(1.0, std::max(std::abs(lo), std::abs(hi)));
    if (depth >= 56 || hi - lo <= width_tol) {
        if (invalid_interval_is_ambiguous) {
            // A sign-changing residual root remains the ordinary case.
            if (lo_valid && hi_valid && f_lo * f_hi < 0.0 &&
                solve_even_asphere_bracket(
                    surface,
                    ray,
                    lo,
                    hi,
                    f_lo,
                    f_hi,
                    root,
                    true)) {
                return FirstRootSearchStatus::Found;
            }

            // For an even-multiplicity root the residual does not change
            // sign, but its derivative does.  Isolate that stationary point
            // inside this already tiny certified leaf, then evaluate the
            // residual there.  This provides positional convergence instead
            // of accepting a nearby value whose residual merely rounded to
            // zero.
            double derivative_lo = 0.0;
            double derivative_hi = 0.0;
            if (even_asphere_derivative_at(
                    surface, ray, lo, &derivative_lo) &&
                even_asphere_derivative_at(
                    surface, ray, hi, &derivative_hi) &&
                derivative_lo * derivative_hi <= 0.0) {
                double stationary_lo = lo;
                double stationary_hi = hi;
                double derivative_at_lo = derivative_lo;
                for (int iteration = 0; iteration < 64; ++iteration) {
                    const double stationary_mid =
                        0.5 * (stationary_lo + stationary_hi);
                    double derivative_mid = 0.0;
                    if (!even_asphere_derivative_at(
                            surface,
                            ray,
                            stationary_mid,
                            &derivative_mid)) {
                        return FirstRootSearchStatus::Ambiguous;
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
                if (even_asphere_residual_at(
                        surface,
                        ray,
                        stationary,
                        &stationary_residual)) {
                    const double stationary_scale = std::max(
                        1.0,
                        std::max(
                            std::abs(ray.p.z + stationary * ray.d.z),
                            std::hypot(
                                ray.p.x + stationary * ray.d.x,
                                ray.p.y + stationary * ray.d.y)));
                    const double stationary_tolerance =
                        128.0 *
                        std::numeric_limits<double>::epsilon() *
                        stationary_scale;
                    if (std::abs(stationary_residual) <=
                        stationary_tolerance) {
                        *root = stationary;
                        return FirstRootSearchStatus::Found;
                    }
                }
            }
            return FirstRootSearchStatus::Ambiguous;
        }
        const double mid = 0.5 * (lo + hi);
        const double seeds[3] = {lo, mid, hi};
        double best_seed = mid;
        double best_residual = std::numeric_limits<double>::infinity();
        for (double seed : seeds) {
            double value = 0.0;
            if (even_asphere_residual_at(surface, ray, seed, &value) &&
                std::abs(value) < best_residual) {
                best_seed = seed;
                best_residual = std::abs(value);
            }
        }
        double refined = 0.0;
        if (refine_even_asphere_seed(
                surface,
                ray,
                best_seed,
                lo,
                hi,
                &refined,
                invalid_interval_is_ambiguous)) {
            *root = refined;
            return FirstRootSearchStatus::Found;
        }
        return FirstRootSearchStatus::Ambiguous;
    }

    const double mid = 0.5 * (lo + hi);
    const FirstRootSearchStatus left = find_first_even_asphere_root_interval(
        surface,
        ray,
        lo,
        mid,
        depth + 1,
        budget,
        root,
        invalid_interval_is_ambiguous);
    if (left != FirstRootSearchStatus::NoRoot) {
        return left;
    }
    return find_first_even_asphere_root_interval(
        surface,
        ray,
        mid,
        hi,
        depth + 1,
        budget,
        root,
        invalid_interval_is_ambiguous);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool certify_even_asphere_residual_monotonic(
    const Surface& surface,
    const Ray& ray,
    double lower,
    double upper) {
    if (!(lower >= 0.0) || !(upper >= lower) ||
        !std::isfinite(lower) || !std::isfinite(upper)) {
        return false;
    }
    const double x0 = ray.p.x + lower * ray.d.x;
    const double y0 = ray.p.y + lower * ray.d.y;
    const double x1 = ray.p.x + upper * ray.d.x;
    const double y1 = ray.p.y + upper * ray.d.y;
    if (!std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1)) {
        return false;
    }
    // The norm of an affine transverse ray is convex, so its maximum on a
    // finite t interval occurs at an endpoint.
    const double max_radius = std::max(
        std::hypot(x0, y0),
        std::hypot(x1, y1));
    if (!std::isfinite(max_radius)) {
        return false;
    }

    double radial_slope_bound = 0.0;
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps) {
        const double root_arg =
            1.0 - (1.0 + surface.conic) * max_radius * max_radius /
                      (surface.radius * surface.radius);
        if (!(root_arg > 0.0)) {
            return false;
        }
        radial_slope_bound +=
            max_radius / (std::abs(surface.radius) * std::sqrt(root_arg));
    }

    const double r2 = max_radius * max_radius;
    double radial_power = max_radius;
    for (std::size_t i = 0; i < surface.asphere_coefficients.size(); ++i) {
        const double term =
            2.0 * static_cast<double>(i + 1) *
            std::abs(surface.asphere_coefficients[i]) * radial_power;
        if (!std::isfinite(term) || !std::isfinite(radial_slope_bound + term)) {
            return false;
        }
        radial_slope_bound += term;
        radial_power *= r2;
        if (!std::isfinite(radial_power) && i + 1 < surface.asphere_coefficients.size()) {
            return false;
        }
    }

    const double transverse_speed = std::hypot(ray.d.x, ray.d.y);
    const double transverse_derivative_bound = radial_slope_bound * transverse_speed;
    if (!std::isfinite(transverse_derivative_bound)) {
        return false;
    }
    const double scale = std::max(
        1.0,
        std::max(std::abs(ray.d.z), transverse_derivative_bound));
    const double guard = 128.0 * std::numeric_limits<double>::epsilon() * scale;
    return std::abs(ray.d.z) > transverse_derivative_bound + guard;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool polish_and_certify_even_asphere_first_root_candidate(
    const Surface& surface,
    const Ray& ray,
    double lower,
    double upper,
    double candidate,
    double* certified_candidate) {
    if (!certified_candidate || !(lower >= 0.0) || !(upper >= lower) ||
        !(candidate >= lower) || !(candidate <= upper) ||
        !std::isfinite(lower) || !std::isfinite(candidate) ||
        !(candidate > 1.0e-10)) {
        return false;
    }

    // The ordinary Newton path stops on residual.  Two unconditional polish
    // steps make a transverse root position-accurate; a multiple root only
    // contracts linearly and is rejected by the sign-changing bracket below.
    double polished = candidate;
    for (int iteration = 0; iteration < 2; ++iteration) {
        double residual = 0.0;
        double derivative = 0.0;
        if (!even_asphere_residual_at(surface, ray, polished, &residual) ||
            !even_asphere_derivative_at(surface, ray, polished, &derivative)) {
            return false;
        }
        const double derivative_guard =
            256.0 * std::numeric_limits<double>::epsilon() *
            std::max(1.0, std::abs(ray.d.z));
        if (!(std::abs(derivative) > derivative_guard)) {
            return false;
        }
        const double step = residual / derivative;
        const double next = polished - step;
        if (!std::isfinite(step) || !std::isfinite(next) ||
            next < lower || next > upper ||
            std::abs(step) > 0.25 * std::max(1.0, std::abs(polished))) {
            return false;
        }
        polished = next;
    }

    const double position_tolerance =
        2.5e-10 * std::max(1.0, std::abs(polished));
    const double bracket_lower =
        std::max(lower, polished - position_tolerance);
    const double bracket_upper =
        std::min(upper, polished + position_tolerance);
    if (!(bracket_lower < bracket_upper)) {
        return false;
    }
    double residual_lower = 0.0;
    double residual_upper = 0.0;
    if (!even_asphere_residual_at(
            surface, ray, bracket_lower, &residual_lower) ||
        !even_asphere_residual_at(
            surface, ray, bracket_upper, &residual_upper) ||
        !((residual_lower <= 0.0 && residual_upper >= 0.0) ||
          (residual_lower >= 0.0 && residual_upper <= 0.0)) ||
        !certify_even_asphere_residual_monotonic(
            surface, ray, lower, bracket_upper)) {
        return false;
    }
    *certified_candidate = polished;
    return true;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline FirstRootSearchStatus find_certified_root_before(
    const Surface& surface,
    const Ray& ray,
    double lower,
    double candidate,
    double* root) {
    if (!root || !std::isfinite(lower) || !std::isfinite(candidate) ||
        lower < 0.0 || candidate < lower) {
        return FirstRootSearchStatus::Ambiguous;
    }
    const double merge_tol = std::max(
        1.0e-10 * std::max(1.0, std::abs(candidate)),
        surface.newton_tol * 32.0);
    const double upper = candidate - merge_tol;
    if (!(upper > lower)) {
        return FirstRootSearchStatus::NoRoot;
    }
    if (certify_even_asphere_residual_monotonic(surface, ray, lower, candidate)) {
        return FirstRootSearchStatus::NoRoot;
    }
    const ScalarInterval derivative_range =
        even_asphere_derivative_interval(surface, ray, lower, candidate);
    if (derivative_range.valid &&
        (derivative_range.lo > 0.0 || derivative_range.hi < 0.0)) {
        return FirstRootSearchStatus::NoRoot;
    }
    FirstRootSearchBudget budget;
    return find_first_even_asphere_root_interval(
        surface, ray, lower, upper, 0, &budget, root);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool even_asphere_forward_search_interval(
    const Surface& surface,
    const Ray& ray,
    double requested_lower,
    double requested_upper,
    double* lower,
    double* upper) {
    if (!lower || !upper || surface.kind != SurfaceKind::EvenAsphere ||
        !std::isfinite(requested_lower) || !std::isfinite(requested_upper) ||
        requested_lower < 0.0 || requested_upper < requested_lower) {
        return false;
    }

    double lo = requested_lower;
    double hi = requested_upper;
    const double conic_term = 1.0 + surface.conic;
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps &&
        conic_term > 0.0) {
        const double radial_limit_sq =
            surface.radius * surface.radius / conic_term;
        const double transverse_speed_sq =
            ray.d.x * ray.d.x + ray.d.y * ray.d.y;
        const double radial_linear =
            2.0 * (ray.p.x * ray.d.x + ray.p.y * ray.d.y);
        const double radial_constant =
            ray.p.x * ray.p.x + ray.p.y * ray.p.y - radial_limit_sq;
        const double radial_scale = std::max(
            1.0,
            std::max(
                radial_limit_sq,
                ray.p.x * ray.p.x + ray.p.y * ray.p.y));
        const double radial_tol = 128.0 * std::numeric_limits<double>::epsilon() * radial_scale;

        if (transverse_speed_sq <= kEps * kEps) {
            if (radial_constant > radial_tol) {
                return false;
            }
        } else {
            double discriminant =
                radial_linear * radial_linear -
                4.0 * transverse_speed_sq * radial_constant;
            const double discriminant_scale = std::max(
                1.0,
                std::max(
                    radial_linear * radial_linear,
                    std::abs(4.0 * transverse_speed_sq * radial_constant)));
            const double discriminant_tol =
                128.0 * std::numeric_limits<double>::epsilon() * discriminant_scale;
            if (discriminant < -discriminant_tol) {
                return false;
            }
            discriminant = std::max(0.0, discriminant);
            const double root = std::sqrt(discriminant);
            double domain_lo = (-radial_linear - root) / (2.0 * transverse_speed_sq);
            double domain_hi = (-radial_linear + root) / (2.0 * transverse_speed_sq);
            if (domain_hi < domain_lo) {
                std::swap(domain_lo, domain_hi);
            }
            if (domain_hi < 0.0) {
                return false;
            }
            lo = std::max(lo, std::max(0.0, domain_lo));
            hi = std::min(hi, domain_hi);
        }
    }

    if (!std::isfinite(lo) || !std::isfinite(hi) || hi < lo) {
        return false;
    }
    if (!(hi > lo)) {
        double residual = 0.0;
        if (!even_asphere_residual_at(surface, ray, lo, &residual) ||
            std::abs(residual) > std::max(kEps, surface.newton_tol * 10.0)) {
            return false;
        }
    }
    *lower = lo;
    *upper = hi;
    return true;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline FirstRootSearchStatus find_first_even_asphere_root_fallback(
    const Surface& surface,
    const Ray& ray,
    double requested_lower,
    double requested_upper,
    double* root) {
    double lower = 0.0;
    double upper = 0.0;
    if (!root ||
        !even_asphere_forward_search_interval(
            surface, ray, requested_lower, requested_upper, &lower, &upper)) {
        return FirstRootSearchStatus::NoRoot;
    }

    FirstRootSearchBudget budget;
    // This path is reached only after all analytic/Newton seeds have failed.
    // Give the certified left-first search a larger bounded budget without
    // adding any work to the ordinary intersection hot path.
    budget.max_nodes = 16384;
    return find_first_even_asphere_root_interval(
        surface, ray, lower, upper, 0, &budget, root);
}
} // namespace raytrace_detail

} // namespace optiland
