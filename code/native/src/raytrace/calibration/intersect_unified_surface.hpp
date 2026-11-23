#include "../asphere_certification_inline.hpp"
#include "../asphere_domain_inline.hpp"
#include "../asphere_refinement_inline.hpp"
#include "../interval_arithmetic_inline.hpp"
#pragma once
#include "../detail.hpp"

namespace optiland {
using namespace raytrace_detail;

template <
    class Capture_surface,
    class Capture_local_ray,
    class Capture_t,
    class Capture_candidates,
    class Capture_forward_count,
    class Capture_search_ceiling,
    class Capture_search_lower
>
bool intersect_unified_surface(
    Capture_surface& surface,
    Capture_local_ray& local_ray,
    Capture_t& t,
    Capture_candidates& candidates,
    Capture_forward_count& forward_count,
    Capture_search_ceiling& search_ceiling,
    Capture_search_lower& search_lower
) {
    thread_local const bool unified_debug =
        std::getenv("OPTILAND_ASPHERE_UNIFIED_CPU_DEBUG") != nullptr;
    thread_local const bool uc1_prefix_certificate =
        environment_flag_enabled("OPTILAND_ASPHERE_UC1_PREFIX_CERT") ||
        environment_flag_enabled("OPTILAND_ASPHERE_UC2_EXACT_S1_CERT") ||
        environment_flag_enabled("OPTILAND_ASPHERE_UC3_FAILURE_CERT");
    const UnifiedAsphereDomain domain = unified_asphere_domain(surface);
    if (domain.status == UnifiedAsphereDomainStatus::Unresolved) {
        if (unified_debug) {
            std::fprintf(
                stderr,
                "UNIFIED-CPU domain=unresolved radius=%.17g conic=%.17g coeffs=%zu\n",
                surface.radius,
                surface.conic,
                surface.asphere_coefficients.size());
        }
        return false;
    }
    const bool has_polynomial_departure = std::any_of(
        surface.asphere_coefficients.begin(),
        surface.asphere_coefficients.end(),
        [](double coefficient) { return coefficient != 0.0; });
    const bool pure_plane =
        (!std::isfinite(surface.radius) ||
         std::abs(surface.radius) >= 0.5 * kInf) &&
        !has_polynomial_departure;
    const auto root_in_unified_domain = [&](double candidate) {
        if (!(candidate > 1.0e-10) || !std::isfinite(candidate)) {
            return false;
        }
        if (domain.status == UnifiedAsphereDomainStatus::Unbounded) {
            return true;
        }
        const double hit_x =
            local_ray.p.x + candidate * local_ray.d.x;
        const double hit_y =
            local_ray.p.y + candidate * local_ray.d.y;
        const double hit_radius_squared =
            hit_x * hit_x + hit_y * hit_y;
        const double accepted_radius_squared =
            domain.radius_squared * (1.0 + 2.0e-12);
        return std::isfinite(hit_radius_squared) &&
               std::isfinite(accepted_radius_squared) &&
               hit_radius_squared <= accepted_radius_squared;
    };

    // CUDA validates the candidate against the materialized radial domain
    // and uses the direction certificate before constructing a ray/disk
    // interval.  Mirror that order on CPU; the full interval path below
    // remains the fallback for uncertified directions or candidate misses.
    if (pure_plane) {
        if (std::abs(local_ray.d.z) <= kEps) {
            return false;
        }
        const double exact_root = -local_ray.p.z / local_ray.d.z;
        if (root_in_unified_domain(exact_root)) {
            *t = exact_root;
            return true;
        }
    } else if (local_ray.d.x == 0.0 && local_ray.d.y == 0.0) {
        double residual_at_zero = 0.0;
        if (std::abs(local_ray.d.z) > kEps &&
            even_asphere_residual_at(
                surface, local_ray, 0.0, &residual_at_zero)) {
            const double exact_root = residual_at_zero / local_ray.d.z;
            if (root_in_unified_domain(exact_root)) {
                *t = exact_root;
                return true;
            }
        }
    } else {
        const double strict_lower = std::nextafter(
            1.0e-10, std::numeric_limits<double>::infinity());
        double fast_candidate =
            std::numeric_limits<double>::quiet_NaN();
        for (int i = 0; i < forward_count; ++i) {
            double refined = 0.0;
            if (refine_even_asphere_seed_uc10(
                    surface,
                    local_ray,
                    candidates[i],
                    strict_lower,
                    search_ceiling,
                    &refined) &&
                root_in_unified_domain(refined)) {
                fast_candidate = refined;
                break;
            }
        }
        if (std::isfinite(fast_candidate) &&
            unified_direction_certificate(local_ray)) {
            *t = fast_candidate;
            return true;
        }
    }
    double unified_search_lower = std::max(
        search_lower,
        std::nextafter(
            1.0e-10, std::numeric_limits<double>::infinity()));
    double unified_search_upper =
        domain.status == UnifiedAsphereDomainStatus::Finite
            ? std::numeric_limits<double>::infinity()
            : search_ceiling;
    if (!restrict_search_to_production_slope_domain(
            surface,
            local_ray,
            domain.radius_squared,
            &unified_search_lower,
            &unified_search_upper)) {
        return false;
    }

    if (pure_plane) {
        // The residual is exactly pz + t*dz.  Solving it directly avoids
        // manufacturing a root at the strict-forward lower bound when a
        // bounded Newton iteration is started from a self hit.
        if (std::abs(local_ray.d.z) <= kEps) {
            return false;
        }
        const double exact_root = -local_ray.p.z / local_ray.d.z;
        if (!(exact_root > 1.0e-10) ||
            !std::isfinite(exact_root) ||
            exact_root < unified_search_lower ||
            (std::isfinite(unified_search_upper) &&
             exact_root > unified_search_upper)) {
            return false;
        }
        *t = exact_root;
        return true;
    }
    if (local_ray.d.x == 0.0 && local_ray.d.y == 0.0) {
        // At fixed radius every rotational asphere has a linear ray
        // residual.  This exact O(1) solve is both complete and cheaper
        // than sending axial rays through the generic candidate search.
        double residual_at_zero = 0.0;
        if (std::abs(local_ray.d.z) <= kEps ||
            !even_asphere_residual_at(
                surface, local_ray, 0.0, &residual_at_zero)) {
            return false;
        }
        const double exact_root =
            residual_at_zero / local_ray.d.z;
        if (!(exact_root > 1.0e-10) ||
            !std::isfinite(exact_root) ||
            exact_root < unified_search_lower ||
            (std::isfinite(unified_search_upper) &&
             exact_root > unified_search_upper)) {
            return false;
        }
        *t = exact_root;
        return true;
    }

    double candidate_root = std::numeric_limits<double>::quiet_NaN();
    bool candidate_ok = false;
    for (int i = 0; i < forward_count; ++i) {
        double refined = 0.0;
        if (refine_even_asphere_seed(
                surface,
                local_ray,
                candidates[i],
                unified_search_lower,
                unified_search_upper,
                &refined) &&
            refined > 1.0e-10) {
            candidate_root = refined;
            candidate_ok = true;
            break;
        }
    }
    if (candidate_ok && unified_direction_certificate(local_ray)) {
        if (unified_debug) {
            std::fprintf(
                stderr,
                "UNIFIED-CPU domain=%s r2=%.17g candidate=hit t=%.17g direction=1\n",
                domain.status == UnifiedAsphereDomainStatus::Finite
                    ? "finite"
                    : "unbounded",
                domain.radius_squared,
                candidate_root);
        }
        *t = candidate_root;
        return std::isfinite(*t);
    }
    double certified_candidate = std::numeric_limits<double>::quiet_NaN();
    if (uc1_prefix_certificate && candidate_ok &&
        polish_and_certify_even_asphere_first_root_candidate(
            surface,
            local_ray,
            unified_search_lower,
            unified_search_upper,
            candidate_root,
            &certified_candidate)) {
        *t = certified_candidate;
        return std::isfinite(*t);
    }

    // Newton's residual stopping rule can leave an even-multiplicity root
    // slightly beyond the returned candidate.  Add a small scale-aware
    // enclosure derived from that stopping tolerance, then let the same
    // interval solver prove the entire prefix and locate the actual root.
    // This avoids traversing the remainder of a large finite domain.
    const double candidate_guard = candidate_ok
        ? 64.0 *
              std::sqrt(std::max(kEps, surface.newton_tol)) *
              std::max(1.0, std::abs(candidate_root))
        : 0.0;
    if (candidate_ok && candidate_guard > 0.0) {
        const double stationary_window_lo = std::max(
            unified_search_lower, candidate_root - candidate_guard);
        const double stationary_window_hi = std::isfinite(
                unified_search_upper)
            ? std::min(
                  unified_search_upper,
                  candidate_root + candidate_guard)
            : candidate_root + candidate_guard;
        double derivative_lo = 0.0;
        double derivative_hi = 0.0;
        if (stationary_window_hi > stationary_window_lo &&
            even_asphere_derivative_at(
                surface,
                local_ray,
                stationary_window_lo,
                &derivative_lo) &&
            even_asphere_derivative_at(
                surface,
                local_ray,
                stationary_window_hi,
                &derivative_hi) &&
            derivative_lo * derivative_hi <= 0.0 &&
            (derivative_lo != 0.0 || derivative_hi != 0.0)) {
            double stationary_lo = stationary_window_lo;
            double stationary_hi = stationary_window_hi;
            double derivative_at_lo = derivative_lo;
            for (int iteration = 0; iteration < 64; ++iteration) {
                const double stationary_mid =
                    0.5 * (stationary_lo + stationary_hi);
                double derivative_mid = 0.0;
                if (!even_asphere_derivative_at(
                        surface,
                        local_ray,
                        stationary_mid,
                        &derivative_mid)) {
                    stationary_lo = stationary_hi;
                    break;
                }
                if (derivative_at_lo * derivative_mid <= 0.0) {
                    stationary_hi = stationary_mid;
                } else {
                    stationary_lo = stationary_mid;
                    derivative_at_lo = derivative_mid;
                }
            }
            const double stationary_root =
                0.5 * (stationary_lo + stationary_hi);
            double stationary_residual = 0.0;
            const ScalarInterval stationary_derivative_range =
                even_asphere_derivative_interval(
                    surface,
                    local_ray,
                    stationary_window_lo,
                    stationary_root);
            const double derivative_tolerance =
                256.0 * std::numeric_limits<double>::epsilon() *
                std::max(
                    1.0,
                    std::max(
                        std::abs(derivative_lo),
                        std::abs(derivative_hi)));
            const bool one_sided_stationary =
                stationary_derivative_range.valid &&
                ((derivative_lo < 0.0 &&
                  stationary_derivative_range.hi <=
                      derivative_tolerance) ||
                 (derivative_lo > 0.0 &&
                  stationary_derivative_range.lo >=
                      -derivative_tolerance));
            if (one_sided_stationary &&
                even_asphere_residual_at(
                    surface,
                    local_ray,
                    stationary_root,
                    &stationary_residual)) {
                const double stationary_scale = std::max(
                    1.0,
                    std::max(
                        std::abs(
                            local_ray.p.z +
                            stationary_root * local_ray.d.z),
                        std::hypot(
                            local_ray.p.x +
                                stationary_root * local_ray.d.x,
                            local_ray.p.y +
                                stationary_root * local_ray.d.y)));
                const double stationary_residual_tolerance =
                    128.0 *
                    std::numeric_limits<double>::epsilon() *
                    stationary_scale;
                if (std::abs(stationary_residual) <=
                    stationary_residual_tolerance) {
                    FirstRootSearchStatus prefix_status =
                        FirstRootSearchStatus::NoRoot;
                    if (stationary_window_lo > unified_search_lower) {
                        FirstRootSearchBudget prefix_budget;
                        prefix_budget.max_nodes = 4096;
                        double prefix_root = 0.0;
                        prefix_status =
                            find_first_even_asphere_root_interval(
                                surface,
                                local_ray,
                                unified_search_lower,
                                stationary_window_lo,
                                0,
                                &prefix_budget,
                                &prefix_root,
                                true);
                    }
                    if (prefix_status ==
                        FirstRootSearchStatus::NoRoot) {
                        *t = stationary_root;
                        return std::isfinite(*t) && *t > 1.0e-10;
                    }
                }
            }
        }
    }
    const double candidate_ceiling = candidate_ok
        ? candidate_root + candidate_guard
        : unified_search_upper;
    const double interval_upper = std::isfinite(unified_search_upper)
        ? std::min(unified_search_upper, candidate_ceiling)
        : candidate_ceiling;
    if (!std::isfinite(interval_upper)) {
        // A plane/pure-conic analytic candidate generator is complete.
        // A polynomial unbounded miss needs an explicit asymptotic proof,
        // which this feasibility implementation reports as unresolved.
        if (unified_debug) {
            std::fprintf(
                stderr,
                "UNIFIED-CPU domain=unbounded candidate=miss polynomial=%d\n",
                has_polynomial_departure ? 1 : 0);
        }
        if (!has_polynomial_departure) {
            return false;
        }
        return false;
    }
    if (!(interval_upper >= unified_search_lower)) {
        return false;
    }
    FirstRootSearchBudget budget;
    budget.max_nodes = candidate_ok ? 4096 : 16384;
    double certified_root = 0.0;
    const FirstRootSearchStatus status =
        find_first_even_asphere_root_interval(
            surface,
            local_ray,
            unified_search_lower,
            interval_upper,
            0,
            &budget,
            &certified_root,
            true);
    if (unified_debug) {
        const char* status_name =
            status == FirstRootSearchStatus::Found
                ? "found"
                : (status == FirstRootSearchStatus::NoRoot
                       ? "no_root"
                       : "ambiguous");
        std::fprintf(
            stderr,
            "UNIFIED-CPU domain=%s r2=%.17g candidate=%s candidate_t=%.17g "
            "direction=0 interval=%s nodes=%d root=%.17g range=%.17g:%.17g\n",
            domain.status == UnifiedAsphereDomainStatus::Finite
                ? "finite"
                : "unbounded",
            domain.radius_squared,
            candidate_ok ? "hit" : "miss",
            candidate_root,
            status_name,
            budget.nodes,
            certified_root,
            unified_search_lower,
            interval_upper);
    }
    if (status != FirstRootSearchStatus::Found ||
        !(certified_root > 1.0e-10)) {
        return false;
    }
    *t = certified_root;
    return std::isfinite(*t);
}
} // namespace optiland
