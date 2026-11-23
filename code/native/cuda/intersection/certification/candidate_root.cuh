#pragma once
#include "../prelude.cuh"
#include "../geometry/sag_residual.cuh"
#include "../refinement/bounded_seed.cuh"
#include "../refinement/newton_seed.cuh"

namespace optiland::de_cuda_detail {
// namespace
__device__ bool certify_even_asphere_first_root_candidate(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double lower,
    double upper,
    double candidate,
    double* certified_candidate,
    const DeviceAsphereCandidateDiagnostic* candidate_diagnostic,
    bool reuse_candidate_pair,
    DeviceAspherePrefixDiagnostic* prefix_diagnostic) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    candidate_diagnostic = nullptr;
    reuse_candidate_pair = false;
    prefix_diagnostic = nullptr;
#endif
    if (surface.kind != kKindEvenAsphere ||
        (surface.unified_slope_domain_status != 1 &&
         surface.unified_slope_domain_status != 2) ||
        !certified_candidate || !(lower >= 0.0) || !(upper >= lower) ||
        !(candidate >= lower) || !(candidate <= upper) ||
        !finite_value(lower) || !finite_value(candidate) ||
        !(candidate > 1.0e-10)) {
        return false;
    }

    // The ordinary Newton path intentionally stops on residual.  Force two
    // additional steps here so a transverse root reaches positional accuracy;
    // a multiple root only contracts linearly and will be rejected by the
    // sign-changing micro-bracket below.
    double polished = candidate;
    #pragma unroll
    for (int iteration = 0; iteration < 2; ++iteration) {
        double residual = 0.0;
        double derivative = 0.0;
        const bool reuse_pair =
            iteration == 0 && reuse_candidate_pair && candidate_diagnostic &&
            candidate_diagnostic->cacheable_success != 0 &&
            candidate_diagnostic->cached_t == polished;
        if (reuse_pair) {
            residual = candidate_diagnostic->cached_residual;
            derivative = candidate_diagnostic->cached_derivative;
            if (prefix_diagnostic) {
                ++prefix_diagnostic->reused_candidate_pairs;
            }
        } else {
            if (prefix_diagnostic) {
                ++prefix_diagnostic->polish_evaluations;
            }
            if (!surface_residual_and_derivative_at(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    polished,
                    &residual,
                    &derivative)) {
                return false;
            }
        }
        const double derivative_guard =
            256.0 * DBL_EPSILON * fmax(1.0, fabs(nz));
        if (!(fabs(derivative) > derivative_guard)) {
            return false;
        }
        const double step = residual / derivative;
        const double next = polished - step;
        if (!finite_value(step) || !finite_value(next) ||
            next < lower || next > upper ||
            fabs(step) > 0.25 * fmax(1.0, fabs(polished))) {
            return false;
        }
        polished = next;
    }

    const double position_tolerance =
        2.5e-10 * fmax(1.0, fabs(polished));
    const double bracket_lower = fmax(lower, polished - position_tolerance);
    const double bracket_upper = fmin(upper, polished + position_tolerance);
    if (!(bracket_lower < bracket_upper)) {
        return false;
    }
    if (surface.unified_slope_domain_status == 1) {
        const double x0 = fma(bracket_lower, lx, px);
        const double y0 = fma(bracket_lower, my, py);
        const double x1 = fma(bracket_upper, lx, px);
        const double y1 = fma(bracket_upper, my, py);
        const double max_radius_squared = fmax(
            fma(x0, x0, y0 * y0),
            fma(x1, x1, y1 * y1));
        if (!finite_value(max_radius_squared) ||
            max_radius_squared >
                surface.slope_root_domain_radius_squared * (1.0 + 2.0e-12)) {
            return false;
        }
    }
    double residual_lower = 0.0;
    double residual_upper = 0.0;
    if (prefix_diagnostic) {
        ++prefix_diagnostic->endpoint_residual_evaluations;
    }
    if (!surface_residual_at<true>(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            bracket_lower,
            &residual_lower)) {
        return false;
    }
    if (prefix_diagnostic) {
        ++prefix_diagnostic->endpoint_residual_evaluations;
    }
    if (!surface_residual_at<true>(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            bracket_upper,
            &residual_upper) ||
        !((residual_lower <= 0.0 && residual_upper >= 0.0) ||
          (residual_lower >= 0.0 && residual_upper <= 0.0))) {
        return false;
    }
    if (prefix_diagnostic) {
        ++prefix_diagnostic->monotonicity_attempts;
    }
    if (!certify_asphere_residual_monotonic(
            surface, px, py, lx, my, nz, lower, bracket_upper)) {
        return false;
    }
    *certified_candidate = polished;
    return true;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
__device__ __forceinline__ bool restrict_search_to_slope_domain(
    const DeviceSurface& surface,
    double px,
    double py,
    double lx,
    double my,
    double* search_lower,
    double* search_upper);
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
__device__ __forceinline__ bool
certify_slope_domain_residual_monotonic(
    double lx,
    double my,
    double nz);
} // namespace optiland::de_cuda_detail
