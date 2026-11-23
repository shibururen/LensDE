// Dispatch physical surface intersection and apply refraction or reflection at the accepted hit.
// Sequential tracing uses the configured root policy and the retained certification helpers.
// Preserve forward-root selection, domain rejection, normal orientation, and failure classification.

#include "asphere_certification_inline.hpp"
#include "asphere_domain_inline.hpp"
#include "asphere_refinement_inline.hpp"
#include "asphere_topology_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "detail.hpp"
#include "calibration/intersect_unified_surface.hpp"

namespace optiland {
using namespace raytrace_detail;

bool intersect_surface(
    const Surface& surface,
    const Ray& local_ray,
    double* t,
    bool ignore_stop_aperture) {
    if (!t) {
        return false;
    }

    const Vec3& p = local_ray.p;
    const Vec3& d = local_ray.d;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        !std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) {
        return false;
    }

    if (surface.is_plane()) {
        if (std::abs(d.z) <= kEps) {
            return false;
        }
        *t = -p.z / d.z;
        return std::isfinite(*t);
    }

    const bool even_asphere = surface.kind == SurfaceKind::EvenAsphere;
    if (!even_asphere && std::abs(surface.conic) <= 1.0e-14) {
        const double radius = surface.radius;
        if (!std::isfinite(radius) || std::abs(radius) < kEps) {
            return false;
        }
        return stable_standard_sphere_intersect(
            1.0 / radius, p.x, p.y, p.z, d.x, d.y, d.z, t);
    }

    double guess = 0.0;
    double candidates[3] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
    };
    int candidate_count = 0;
    auto add_candidate = [&](double value) {
        if (candidate_count < 3 && std::isfinite(value)) {
            candidates[candidate_count++] = value;
        }
    };

    struct SurfaceRootCandidate {
        double t = std::numeric_limits<double>::quiet_NaN();
        double residual = std::numeric_limits<double>::infinity();
    };
    constexpr double kRootTEps = 1.0e-10;
    auto consider_sag_branch_root = [&](double tv, SurfaceRootCandidate& best) {
        if (!std::isfinite(tv) || tv < -kRootTEps) {
            return;
        }
        const double xi = p.x + tv * d.x;
        const double yi = p.y + tv * d.y;
        const double zi = p.z + tv * d.z;
        const double sag = surface_sag(surface, xi, yi);
        if (!std::isfinite(xi) || !std::isfinite(yi) ||
            !std::isfinite(zi) || !std::isfinite(sag)) {
            return;
        }
        const double residual = sag - zi;
        const double scale = std::max(
            1.0,
            std::max(std::abs(zi), std::max(std::abs(sag), std::hypot(xi, yi))));
        const double radius_scale =
            std::isfinite(surface.radius) ? std::max(1.0, std::abs(surface.radius)) : 1.0;
        const double tol_scale = std::max(scale, radius_scale);
        const double branch_tol = std::max(64.0 * kEps * tol_scale, 1.0e-10 * tol_scale);
        if (std::abs(residual) > branch_tol) {
            return;
        }
        const double tv_score = std::max(0.0, tv);
        const double best_score = std::isfinite(best.t)
                                      ? std::max(0.0, best.t)
                                      : std::numeric_limits<double>::infinity();
        if (!std::isfinite(best.t) ||
            tv_score < best_score - kRootTEps ||
            (std::abs(tv_score - best_score) <= kRootTEps &&
             std::abs(residual) < std::abs(best.residual))) {
            best.t = tv;
            best.residual = residual;
        }
    };

    const double dz_safe = std::abs(d.z) > kEps ? d.z : (d.z >= 0.0 ? kEps : -kEps);
    SurfaceRootCandidate best_base_root;
    if (even_asphere) {
        add_candidate(-p.z / dz_safe);
    }

    if (!std::isfinite(surface.radius) || std::abs(surface.radius) < kEps) {
        if (std::abs(d.z) <= kEps) {
            if (!even_asphere) {
                return false;
            }
        } else {
            guess = -p.z / d.z;
            if (even_asphere) {
                add_candidate(guess);
            } else {
                consider_sag_branch_root(guess, best_base_root);
            }
        }
    } else {
        const double a = surface.conic * d.z * d.z + dot(d, d);
        const double b =
            2.0 * surface.conic * d.z * p.z +
            2.0 * d.x * p.x +
            2.0 * d.y * p.y -
            2.0 * d.z * surface.radius +
            2.0 * d.z * p.z;
        const double c =
            surface.conic * p.z * p.z -
            2.0 * surface.radius * p.z +
            p.x * p.x +
            p.y * p.y +
            p.z * p.z;

        if (std::abs(a) <= kEps) {
            if (std::abs(b) <= kEps) {
                if (!even_asphere) {
                    return false;
                }
            } else {
                guess = -c / b;
                if (even_asphere) {
                    add_candidate(guess);
                } else {
                    consider_sag_branch_root(guess, best_base_root);
                }
            }
        } else {
            const double discr = b * b - 4.0 * a * c;
            if (discr < 0.0) {
                if (!even_asphere) {
                    return false;
                }
            } else {
                const double root = std::sqrt(discr);
                // Use the q-form of the quadratic roots.  The ordinary
                // (-b +/- sqrt(discriminant)) / (2a) form catastrophically
                // cancels the near root when |a| is small and |b| is large.
                // A slightly inaccurate near root can then fail the sag-
                // branch residual check, leaving only a physically unrelated
                // far root (observed as a 6e8 mm clear aperture in locked DE).
                const double q = -0.5 * (b + std::copysign(root, b));
                const double t1 =
                    std::abs(q) > kEps ? q / a : -b / (2.0 * a);
                const double t2 =
                    std::abs(q) > kEps ? c / q : t1;
                if (even_asphere) {
                    add_candidate(t1);
                    add_candidate(t2);
                } else {
                    consider_sag_branch_root(t1, best_base_root);
                    consider_sag_branch_root(t2, best_base_root);
                }
            }
        }
    }
    if (!even_asphere) {
        if (!std::isfinite(best_base_root.t)) {
            return false;
        }
        *t = std::abs(best_base_root.t) <= kRootTEps ? 0.0 : best_base_root.t;
        return true;
    }

    std::sort(candidates, candidates + candidate_count);
    int forward_count = 0;
    for (int i = 0; i < candidate_count; ++i) {
        if (!std::isfinite(candidates[i]) || candidates[i] < -kRootTEps) {
            continue;
        }
        const double value = std::max(0.0, candidates[i]);
        if (forward_count > 0 &&
            std::abs(value - candidates[forward_count - 1]) <=
                kRootTEps * std::max(1.0, std::abs(value))) {
            continue;
        }
        candidates[forward_count++] = value;
    }
    const double plane_seed = -p.z / dz_safe;
    const double farthest_seed =
        forward_count > 0 ? candidates[forward_count - 1] : plane_seed;
    const double finite_seed =
        std::isfinite(farthest_seed) && farthest_seed >= 0.0
            ? farthest_seed
            : std::max(0.0, plane_seed);
    const double search_ceiling =
        finite_seed + std::max(1.0, std::abs(finite_seed) + 1.0);
    double search_lower = 0.0;
    double search_upper = search_ceiling;
    double production_search_upper =
        std::numeric_limits<double>::infinity();
    const bool aperture_bounded_stop =
        even_asphere &&
        asphere_root_policy() !=
            AsphereRootPolicy::UnifiedCertifiedExperimental &&
        !ignore_stop_aperture && surface.is_stop &&
        surface.aperture_radius > 0.0 && std::isfinite(surface.aperture_radius);
    if (aperture_bounded_stop) {
        double stop_upper = 0.0;
        if (!stop_surface_forward_root_interval(
                surface, local_ray, &search_lower, &stop_upper)) {
            return false;
        }
        if (std::isfinite(stop_upper)) {
            search_upper = stop_upper;
            production_search_upper = stop_upper;
        }
        if (!(search_upper >= search_lower)) {
            return false;
        }
    }
    if (asphere_root_policy() ==
        AsphereRootPolicy::UnifiedCertifiedExperimental) {
        return intersect_unified_surface(surface, local_ray, t, candidates, forward_count, search_ceiling, search_lower);
    }
    if (asphere_root_policy() ==
        AsphereRootPolicy::SlopeDomainTopologySeed) {
        double production_search_lower = search_lower;
        const double domain_radius_squared =
            production_asphere_slope_domain_radius_squared(surface);
        if (!restrict_search_to_production_slope_domain(
                surface,
                local_ray,
                domain_radius_squared,
                &production_search_lower,
                &production_search_upper)) {
            return false;
        }
        const auto root_in_production_domain = [&](double candidate) {
            const double tolerance = 1.0e-10 * std::max(
                1.0,
                std::max(std::abs(production_search_lower),
                         std::abs(candidate)));
            if (!std::isfinite(candidate) ||
                candidate < production_search_lower - tolerance ||
                (std::isfinite(production_search_upper) &&
                 candidate > production_search_upper + tolerance)) {
                return false;
            }
            if (!std::isfinite(domain_radius_squared)) {
                return true;
            }
            const double x = p.x + candidate * d.x;
            const double y = p.y + candidate * d.y;
            const double hit_radius_squared = x * x + y * y;
            return std::isfinite(hit_radius_squared) &&
                   hit_radius_squared <=
                       domain_radius_squared * (1.0 + 2.0e-12);
        };
        const bool topology_ray = production_ray_requires_topology_seed(
            surface, local_ray, domain_radius_squared);
        double topology_root = 0.0;
        if (topology_ray &&
            production_topology_linear_seed(
                surface,
                local_ray,
                production_search_lower,
                production_search_upper,
                &topology_root) &&
            root_in_production_domain(topology_root)) {
            *t = topology_root;
            return true;
        }

        for (int i = 0; i < forward_count; ++i) {
            double candidate_root = 0.0;
            if (!refine_production_asphere_root_fast(
                    surface, local_ray, candidates[i], &candidate_root) ||
                !root_in_production_domain(candidate_root)) {
                continue;
            }
            double earlier_root = 0.0;
            if (topology_ray &&
                production_topology_seeded_before(
                    surface,
                    local_ray,
                    production_search_lower,
                    candidate_root,
                    &earlier_root)) {
                *t = earlier_root;
                return std::isfinite(*t);
            }
            *t = candidate_root;
            return std::isfinite(*t);
        }
        double rescued_root = 0.0;
        if (production_seed_preconditioned_no_b2(
                surface,
                local_ray,
                production_search_lower,
                production_search_upper,
                &rescued_root) &&
            root_in_production_domain(rescued_root)) {
            *t = rescued_root;
            return std::isfinite(*t);
        }
        return false;
    }
    bool encountered_ambiguous_prefix = false;
    for (int i = 0; i < forward_count; ++i) {
        double candidate_root = 0.0;
        if (!refine_even_asphere_seed(
                surface,
                local_ray,
                candidates[i],
                search_lower,
                search_upper,
                &candidate_root)) {
            continue;
        }

        if (asphere_root_policy() == AsphereRootPolicy::FastRecover) {
            *t = candidate_root;
            return std::isfinite(*t);
        }

        double earlier_root = 0.0;
        const FirstRootSearchStatus prefix_status = find_certified_root_before(
            surface,
            local_ray,
            search_lower,
            candidate_root,
            &earlier_root);
        if (prefix_status == FirstRootSearchStatus::Found) {
            *t = earlier_root;
            return std::isfinite(*t);
        }
        if (prefix_status == FirstRootSearchStatus::NoRoot) {
            *t = candidate_root;
            return std::isfinite(*t);
        }
        encountered_ambiguous_prefix = true;
    }

    // The polynomial departure can create a real sag-branch intersection even
    // when the base conic has no analytic root and the plane seed lies outside
    // the conic's real radial domain. Search that forward domain only after the
    // inexpensive candidate path has failed, preserving ordinary trace speed.
    double fallback_root = 0.0;
    const FirstRootSearchStatus fallback_status =
        find_first_even_asphere_root_fallback(
            surface, local_ray, search_lower, search_upper, &fallback_root);
    if (fallback_status == FirstRootSearchStatus::Found) {
        *t = fallback_root;
        return std::isfinite(*t);
    }

    // Never accept a converged root when the interval before it could not be
    // certified root-free. A reported miss is safer than silently tracing a
    // later mathematical branch of a high-order asphere.
    (void)encountered_ambiguous_prefix;
    return false;
}

bool refract(Vec3 incident, Vec3 normal, double n1, double n2, Vec3* transmitted) {
    if (!transmitted || std::abs(n2) <= kEps) {
        return false;
    }
    incident = normalize(incident);
    normal = normalize(normal);

    double d = dot(incident, normal);
    const double sgn = (d >= 0.0) ? 1.0 : -1.0;
    normal = normal * sgn;
    d = std::abs(d);

    const double u = n1 / n2;
    const double root_arg = 1.0 - u * u * (1.0 - d * d);
    if (root_arg < 0.0) {
        return false;
    }
    const double root = std::sqrt(root_arg);
    *transmitted = normalize({
        u * incident.x + normal.x * root - u * normal.x * d,
        u * incident.y + normal.y * root - u * normal.y * d,
        u * incident.z + normal.z * root - u * normal.z * d,
    });
    return true;
}

Vec3 reflect(Vec3 incident, Vec3 normal) {
    incident = normalize(incident);
    normal = normalize(normal);
    double d = dot(incident, normal);
    const double sgn = (d >= 0.0) ? 1.0 : -1.0;
    normal = normal * sgn;
    d = std::abs(d);
    return normalize(incident - normal * (2.0 * d));
}

} // namespace optiland
