#pragma once
#include "../prelude.cuh"
#include "../certification/candidate_root.cuh"
#include "../certification/first_root_interval.cuh"
#include "../certification/prefix_piecewise.cuh"
#include "../geometry/sag_residual.cuh"
#include "../refinement/bounded_seed.cuh"
#include "../refinement/newton_seed.cuh"

namespace optiland::de_cuda_detail {
// Intersect the forward search interval with the materialized radial domain.
// Because (x(t), y(t)) is affine, the ray/disk intersection is one exact
// contiguous interval.  The existing certified-first-root state machine can
// therefore operate unchanged, but on the physically usable branch only.
__device__ __forceinline__ bool restrict_search_to_slope_domain(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double* search_lower,
    double* search_upper) {
    if (!search_lower || !search_upper ||
        !finite_value(*search_lower) || *search_lower < 0.0 ||
        *search_upper < *search_lower) {
        return false;
    }
    const double radius_squared = surface.slope_root_domain_radius_squared;
    if (!finite_value(radius_squared) ||
        radius_squared >= 0.5 * kDeviceInf) {
        return true;
    }
    if (!(radius_squared >= 0.0)) {
        return false;
    }

    // Match the final radial-domain acceptance tolerance used in Round7/9.
    const double accepted_radius_squared =
        radius_squared * (1.0 + 2.0e-12);
    const double a = lx * lx + my * my;
    const double b = 2.0 * (px * lx + py * my);
    const double c = px * px + py * py - accepted_radius_squared;
    if (!finite_value(a) || !finite_value(b) || !finite_value(c)) {
        return false;
    }
    if (!(a > 0.0)) {
        return c <= 0.0;
    }

    double discriminant = b * b - 4.0 * a * c;
    const double discriminant_scale =
        fmax(1.0, fabs(b * b) + fabs(4.0 * a * c));
    const double discriminant_tolerance =
        64.0 * DBL_EPSILON * discriminant_scale;
    if (discriminant < -discriminant_tolerance) {
        return false;
    }
    discriminant = fmax(0.0, discriminant);
    const double sqrt_discriminant = sqrt(discriminant);

    double domain_lower = 0.0;
    double domain_upper = 0.0;
    const double q = -0.5 * (b + copysign(sqrt_discriminant, b));
    if (fabs(q) > DBL_MIN) {
        const double root_a = q / a;
        const double root_b = c / q;
        domain_lower = fmin(root_a, root_b);
        domain_upper = fmax(root_a, root_b);
    } else {
        domain_lower = -b / (2.0 * a);
        domain_upper = domain_lower;
    }
    if (!finite_value(domain_lower) || !finite_value(domain_upper)) {
        return false;
    }

    const double clipped_lower = fmax(*search_lower, domain_lower);
    const double clipped_upper = fmin(*search_upper, domain_upper);
    const double scale = fmax(
        1.0,
        fmax(
            fabs(clipped_lower),
            finite_value(clipped_upper) ? fabs(clipped_upper) : 1.0));
    const double interval_tolerance = 64.0 * DBL_EPSILON * scale;
    if (clipped_upper < clipped_lower - interval_tolerance) {
        return false;
    }
    *search_lower = clipped_lower;
    *search_upper = fmax(clipped_lower, clipped_upper);
    return true;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
// Within the materialized 20-degree slope domain, |dsag/dr| is contractually
// bounded by cot(20 degrees).  If the axial ray speed dominates the largest
// possible transverse sag change, the ray/surface residual derivative cannot
// change sign anywhere in the domain.  A residual-certified Newton root is
// then necessarily the first domain root.  This direction-only certificate is
// deliberately cheaper than rebuilding a coefficient interval per ray.
__device__ __forceinline__ bool
certify_slope_domain_residual_monotonic(
    double lx,
    double my,
    double nz) {
    const double transverse_speed_squared = fma(lx, lx, my * my);
    if (!finite_value(transverse_speed_squared) ||
        transverse_speed_squared < 0.0 || !finite_value(nz)) {
        return false;
    }
    const double transverse_bound_squared =
        kAsphereRootDomainMaximumRadialSlopeConservative *
        kAsphereRootDomainMaximumRadialSlopeConservative *
        transverse_speed_squared;
    if (!finite_value(transverse_bound_squared)) {
        return false;
    }
    const double abs_nz = fabs(nz);
    const double guard =
        512.0 * DBL_EPSILON * fmax(1.0, abs_nz);
    const double guarded_nz = abs_nz - guard;
    return guarded_nz > 0.0 &&
           guarded_nz * guarded_nz > transverse_bound_squared;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
// Round17 topology-seed prototype.  The diagnostic wide failures form a
// sharply separated class of strong fourth-order departures.  Use a compact,
// dimensionless surface test to keep ordinary surfaces on the pure U1 path.
// A production version should materialize a general topology flag once per
// surface; this per-intersection expression exists only for the isolated
// hypothesis test.
__device__ __forceinline__ bool asphere_requires_topology_seed(
    const DeviceSurface& surface) {
    if (surface.coeff_count < 2 ||
        !finite_value(surface.slope_root_domain_radius_squared) ||
        surface.slope_root_domain_radius_squared < 0.0 ||
        surface.slope_root_domain_radius_squared >= 0.5 * kDeviceInf) {
        return false;
    }
    const double radius_squared = surface.slope_root_domain_radius_squared;
    const double fourth_order_departure =
        fabs(surface.coeffs[1]) * radius_squared * radius_squared;
    return finite_value(fourth_order_departure) &&
           fourth_order_departure > 0.25;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
// Data-guided folded-branch corridor.  Continuous transverse perturbation of
// the 50 observed cases keeps the extra roots out to 0.05--0.21 domain radii.
// A 1/4 impact corridor covers that family; the starting fold annulus and
// outward/oblique direction tests reject rays that cannot follow the observed
// branch geometry.  This remains an isolated hypothesis, not a formal proof.
__device__ __forceinline__ bool asphere_ray_requires_topology_seed(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double nz) {
    if (!asphere_requires_topology_seed(surface)) {
        return false;
    }
    const double transverse_speed_squared = fma(lx, lx, my * my);
    if (!finite_value(transverse_speed_squared) ||
        transverse_speed_squared < 0.0) {
        return false;
    }
    if (transverse_speed_squared <= DBL_MIN) {
        return true;
    }
    const double radius_squared = fma(px, px, py * py);
    const double domain_radius_squared =
        surface.slope_root_domain_radius_squared;
    if (!finite_value(radius_squared) ||
        radius_squared < 0.36 * domain_radius_squared ||
        radius_squared > 0.5776 * domain_radius_squared) {
        return false;
    }
    const double radial_direction = fma(px, lx, py * my);
    if (!(radial_direction > 0.0) ||
        !(transverse_speed_squared > nz * nz)) {
        return false;
    }
    const double cross = fma(px, my, -py * lx);
    const double impact_scaled_squared = cross * cross;
    constexpr double kImpactFractionSquared = 1.0 / 16.0;
    const double allowed_scaled_squared =
        kImpactFractionSquared *
        surface.slope_root_domain_radius_squared *
        transverse_speed_squared;
    return finite_value(impact_scaled_squared) &&
           finite_value(allowed_scaled_squared) &&
           impact_scaled_squared <= allowed_scaled_squared;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
// For the preclassified folded profiles, start Newton at the forward
// intercept of the residual tangent at the domain entrance.  In every
// observed missed-first-root case this lands next to the first branch
// (roughly t=0.18--0.21 versus the late U1 roots at t=0.40--0.85).  A
// residual-certified bounded Newton success is accepted directly; failure is
// intentionally non-fatal so the ordinary U1/two-probe path remains a guard.
__device__ __noinline__ bool intersect_asphere_topology_linear_seed(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double search_lower,
    double search_upper,
    double newton_tol,
    DeviceAsphereRootProfile* profile,
    double* root) {
    if (!root || !finite_value(search_lower) ||
        !finite_value(search_upper) || search_upper <= search_lower ||
        !asphere_requires_topology_seed(surface)) {
        return false;
    }
    if (profile) {
        increment_profile_counter(&profile->confirmation_scans);
    }
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
            search_lower,
            &residual,
            &derivative)) {
        if (profile) {
            increment_profile_counter(&profile->confirmation_invalid);
        }
        return false;
    }
    const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
    if (fabs(residual) <= residual_tol) {
        *root = fabs(search_lower) <= kDeviceEps ? 0.0 : search_lower;
        if (profile) {
            increment_profile_counter(&profile->confirmation_accepted_candidates);
        }
        return true;
    }
    const double derivative_guard =
        64.0 * DBL_EPSILON * fmax(1.0, fabs(derivative));
    if (!(residual > residual_tol && derivative < -derivative_guard)) {
        return false;
    }
    const double seed = search_lower - residual / derivative;
    if (!finite_value(seed) || seed <= search_lower || seed > search_upper) {
        return false;
    }
    if (profile) {
        increment_profile_counter(&profile->confirmation_derivative_ambiguities);
    }
    double refined = NAN;
    if (!refine_even_asphere_root_fast(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            seed,
            newton_tol,
            &refined)) {
        return false;
    }
    *root = refined;
    if (profile) {
        increment_profile_counter(&profile->confirmation_accepted_candidates);
    }
    return true;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
// Before final acceptance, generate a first-branch seed only for surfaces
// classified as topologically complex.  P4 uses one midpoint probe.  P6 first
// probes the midpoint and only then 3/4: diagnostics show that the midpoint
// catches 40/50 observed true branch separations and the remaining 10 all lie
// in the midpoint-to-3/4 interval.  P8 retains the full quarter grid.  The
// first sign bracket is solved locally and no B2 or whole-prefix posterior
// verifier is called.
__device__ __noinline__ bool intersect_asphere_topology_seeded_before(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double search_lower,
    double candidate,
    int seed_budget,
    double newton_tol,
    DeviceAsphereRootProfile* profile,
    double* root) {
    if (!root || !finite_value(search_lower) ||
        !finite_value(candidate) || candidate <= search_lower ||
        !asphere_requires_topology_seed(surface)) {
        return false;
    }
    if (profile) {
        increment_profile_counter(&profile->confirmation_scans);
    }
    double previous_t = search_lower;
    double previous_f = 0.0;
    double previous_d = 0.0;
    if (!surface_residual_and_derivative_at(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            previous_t,
            &previous_f,
            &previous_d)) {
        if (profile) {
            increment_profile_counter(&profile->confirmation_invalid);
        }
        return false;
    }
    const double residual_tol = fmax(kDeviceEps, newton_tol * 10.0);
    if (fabs(previous_f) <= residual_tol) {
        *root = fabs(previous_t) <= kDeviceEps ? 0.0 : previous_t;
        if (profile) {
            increment_profile_counter(&profile->confirmation_earlier_roots);
        }
        return true;
    }
    // The observed missed-first-root topology starts above the surface and
    // initially travels toward it: F(lower) > 0 and F'(lower) < 0.  If either
    // condition is absent, do not pay for midpoint probes.  This is a ray-level
    // gate layered on the preclassified complex surface, not a posterior scan.
    const double derivative_guard =
        64.0 * DBL_EPSILON * fmax(1.0, fabs(previous_d));
    if (!(previous_f > residual_tol && previous_d < -derivative_guard)) {
        return false;
    }
    if (profile) {
        increment_profile_counter(&profile->confirmation_derivative_ambiguities);
    }
    const int sample_count = seed_budget <= 4 ? 1 : (seed_budget <= 6 ? 2 : 3);
    for (int sample = 0; sample < sample_count; ++sample) {
        const double fraction = sample_count == 1
            ? 0.5
            : (sample_count == 2
                ? 0.5 + 0.25 * static_cast<double>(sample)
                : 0.25 * static_cast<double>(sample + 1));
        const double sample_t =
            fma(fraction, candidate - search_lower, search_lower);
        double sample_f = 0.0;
        if (profile) {
            increment_profile_counter(&profile->confirmation_sample_roots);
        }
        if (!surface_residual_at<true>(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                sample_t,
                &sample_f)) {
            if (profile) {
                increment_profile_counter(&profile->confirmation_invalid);
            }
            return false;
        }
        if (previous_f * sample_f <= 0.0) {
            double earlier = NAN;
            if (!solve_asphere_monotonic_bracket_hybrid(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    previous_t,
                    sample_t,
                    previous_f,
                    sample_f,
                    newton_tol,
                    profile,
                    &earlier)) {
                if (profile) {
                    increment_profile_counter(&profile->confirmation_invalid);
                }
                return false;
            }
            *root = earlier;
            if (profile) {
                increment_profile_counter(&profile->confirmation_earlier_roots);
            }
            return true;
        }
        previous_t = sample_t;
        previous_f = sample_f;
    }
    return false;
}
} // namespace optiland::de_cuda_detail
