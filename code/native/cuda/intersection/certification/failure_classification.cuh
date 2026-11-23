#pragma once
#include "../prelude.cuh"
#include "bracket_bisection.cuh"
#include "candidate_root.cuh"
#include "../geometry/sag_residual.cuh"
#include "../refinement/newton_seed.cuh"

namespace optiland::de_cuda_detail {
__device__ __noinline__ int classify_even_asphere_exact_s1_constructed_root(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double* certified_candidate) {
    const double surface_scale = fmax(
        1.0,
        fmax(fabs(surface.aperture_radius), fabs(surface.semi_aperture)));
    const double candidate = 1.0e-4 * surface_scale;
    const double lower = nextafter(1.0e-10, DBL_MAX);
    if (!certified_candidate || surface.kind != kKindEvenAsphere ||
        (surface.unified_slope_domain_status != 1 &&
         surface.unified_slope_domain_status != 2) ||
        !(lower >= 0.0) || !(candidate >= lower) ||
        !finite_value(lower) || !finite_value(candidate) ||
        !finite_value(px) || !finite_value(py) || !finite_value(pz) ||
        !finite_value(lx) || !finite_value(my) || !finite_value(nz) ||
        !certify_slope_domain_residual_monotonic(lx, my, nz)) {
        return 0;
    }

    bool candidate_in_domain = false;
    if (surface.unified_slope_domain_status == 2) {
        candidate_in_domain = true;
    } else {
        const double x = fma(candidate, lx, px);
        const double y = fma(candidate, my, py);
        const double radius_squared = fma(x, x, y * y);
        candidate_in_domain =
            finite_value(radius_squared) &&
            radius_squared <=
                surface.slope_root_domain_radius_squared * (1.0 + 2.0e-12);
    }
    if (candidate_in_domain) {
        return certify_even_asphere_first_root_candidate(
                   surface,
                   px,
                   py,
                   pz,
                   lx,
                   my,
                   nz,
                   lower,
                   DBL_MAX,
                   candidate,
                   certified_candidate)
                   ? 1
                   : 0;
    }

    // An out-of-domain constructed target is not itself a valid contract
    // root.  The radial slope domain is a disk, so its intersection with this
    // affine ray is one connected interval.  If there is no interval, there
    // is no contract root.  Otherwise the 20-degree direction certificate
    // fixes the residual derivative sign throughout the interval; endpoint
    // residuals with the same strict sign then exclude every root in it.
    if (surface.unified_slope_domain_status != 1) {
        return 0;
    }
    if (!finite_value(surface.slope_root_domain_radius_squared) ||
        surface.slope_root_domain_radius_squared < 0.0) {
        return 0;
    }
    double domain_lower = lower;
    double domain_upper = INFINITY;
    if (!restrict_search_to_slope_domain(
            surface,
            px,
            py,
            lx,
            my,
            &domain_lower,
            &domain_upper)) {
        return -1;
    }
    if (!finite_value(domain_lower) || !finite_value(domain_upper) ||
        !(domain_upper > domain_lower)) {
        return 0;
    }
    double residual_lower = 0.0;
    double residual_upper = 0.0;
    if (!surface_residual_at<true>(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            domain_lower,
            &residual_lower) ||
        !surface_residual_at<true>(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            domain_upper,
            &residual_upper)) {
        return 0;
    }
    const double x0 = fma(domain_lower, lx, px);
    const double y0 = fma(domain_lower, my, py);
    const double z0 = fma(domain_lower, nz, pz);
    const double x1 = fma(domain_upper, lx, px);
    const double y1 = fma(domain_upper, my, py);
    const double z1 = fma(domain_upper, nz, pz);
    const double residual_scale = fmax(
        1.0,
        fmax(
            fabs(z0),
            fmax(fabs(z1), fmax(hypot(x0, y0), hypot(x1, y1)))));
    const double residual_guard = fmax(
        kDeviceEps, 256.0 * DBL_EPSILON * residual_scale);
    if ((residual_lower > residual_guard &&
         residual_upper > residual_guard) ||
        (residual_lower < -residual_guard &&
         residual_upper < -residual_guard)) {
        return -2;
    }
    return 0;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
// UC3 is deliberately limited to ordinary-candidate failures.  The locked
// radial slope domain intersects an affine ray in one connected interval.
// When the conservative 20-degree direction bound makes the residual
// strictly monotonic on that interval, two guarded endpoint residuals are
// sufficient: equal strict signs prove no root, while opposite strict signs
// bracket the unique (and therefore first) root.  Every numerically marginal
// case returns zero and continues through the unchanged UC1 traversal.
//
// Return values: 1 = unique root, -2 = certified no root from endpoint
// signs, 0 = UC1 fallback required.  Empty-domain handling stays in the
// caller so the ray/disk quadratic is never evaluated twice.
__device__ __noinline__ int
classify_even_asphere_candidate_failure_monotonic(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double domain_lower,
    double domain_upper,
    double* certified_root,
    DeviceAsphereBracketDiagnostic* bracket_diagnostic,
    DeviceAsphereRootProfile* profile) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    bracket_diagnostic = nullptr;
    profile = nullptr;
#endif
    if (!certified_root || surface.kind != kKindEvenAsphere ||
        surface.unified_slope_domain_status != 1 ||
        !finite_value(px) || !finite_value(py) || !finite_value(pz) ||
        !finite_value(lx) || !finite_value(my) || !finite_value(nz) ||
        !finite_value(domain_lower) || !finite_value(domain_upper) ||
        !(domain_upper > domain_lower)) {
        return 0;
    }
    if (!certify_slope_domain_residual_monotonic(lx, my, nz)) {
        return 0;
    }

    double residual_lower = 0.0;
    double residual_upper = 0.0;
    if (!surface_residual_at<true>(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            domain_lower,
            &residual_lower) ||
        !surface_residual_at<true>(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            domain_upper,
            &residual_upper)) {
        return 0;
    }
    const double x0 = fma(domain_lower, lx, px);
    const double y0 = fma(domain_lower, my, py);
    const double z0 = fma(domain_lower, nz, pz);
    const double x1 = fma(domain_upper, lx, px);
    const double y1 = fma(domain_upper, my, py);
    const double z1 = fma(domain_upper, nz, pz);
    const double residual_scale = fmax(
        1.0,
        fmax(
            fabs(z0),
            fmax(fabs(z1), fmax(hypot(x0, y0), hypot(x1, y1)))));
    const double residual_guard = fmax(
        kDeviceEps, 256.0 * DBL_EPSILON * residual_scale);
    const bool lower_positive = residual_lower > residual_guard;
    const bool lower_negative = residual_lower < -residual_guard;
    const bool upper_positive = residual_upper > residual_guard;
    const bool upper_negative = residual_upper < -residual_guard;
    if ((lower_positive && upper_positive) ||
        (lower_negative && upper_negative)) {
        return -2;
    }
    if (!((lower_positive && upper_negative) ||
          (lower_negative && upper_positive))) {
        return 0;
    }
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    constexpr bool uc14_known_hi = false;
#else
    const bool uc14_known_hi =
        (surface.asphere_polynomial_eval_mode & kAsphereUc14KnownHiBit) != 0;
#endif
    if (!bisect_asphere_residual_root(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            domain_lower,
            domain_upper,
            residual_lower,
            0.0,
            certified_root,
            bracket_diagnostic,
            residual_upper,
            uc14_known_hi,
            0.0,
            false,
            profile,
            0) ||
        !finite_value(*certified_root) || *certified_root <= 1.0e-10) {
        return 0;
    }
    return 1;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
__device__ __forceinline__ int uc8_candidate_evaluation_histogram_index(
    int evaluations) {
    if (evaluations <= 0) {
        return 0;
    }
    if (evaluations <= 4) {
        return evaluations;
    }
    if (evaluations <= 8) {
        return 5;
    }
    if (evaluations <= 16) {
        return 6;
    }
    if (evaluations <= 32) {
        return 7;
    }
    return 8;
}
} // namespace optiland::de_cuda_detail
