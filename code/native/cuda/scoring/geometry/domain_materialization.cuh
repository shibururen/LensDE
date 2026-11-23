#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "asphere_slope.cuh"

namespace optiland {
namespace {
// Locate the first |dsag/dr| = cot(40 deg) boundary without consulting any
// aperture or reference radius.  A finite conic branch supplies its own
// endpoint.  For an unbounded branch, the leading polynomial term supplies a
// certified finite search ceiling after all lower terms and the bounded conic
// contribution are dominated.  Returning status 3 is intentional: numerical
// uncertainty must remain visible to the experiment instead of being turned
// into a guessed domain or a legacy fallback.
__device__ int d_materialize_unified_contract_domain(
    const DeviceSurface& surface,
    double max_slope,
    unsigned long long* interval_nodes,
    unsigned long long* leaf_tests,
    unsigned long long* bisection_iterations,
    double* domain_radius) {
    if (!domain_radius || surface.kind != kKindEvenAsphere ||
        !(max_slope > 0.0) || !d_finite(max_slope)) {
        return 3;
    }
    *domain_radius = INFINITY;
    const bool finite_radius =
        d_finite(surface.radius) && fabs(surface.radius) < 0.5 * kDeviceInf;
    if (finite_radius && fabs(surface.radius) < kDeviceEps) {
        return 3;
    }

    const double conic_term = 1.0 + surface.conic;
    double search_upper = INFINITY;
    if (finite_radius && conic_term > 0.0) {
        const double physical_limit =
            fabs(surface.radius) / sqrt(conic_term);
        search_upper = nextafter(physical_limit, 0.0);
        if (!(search_upper > 0.0) || !d_finite(search_upper)) {
            return 3;
        }
    } else {
        double slope_coefficients[kMaxDeviceAsphereCoefficients] = {};
        const int count = max(
            0, min(surface.coeff_count, kMaxDeviceAsphereCoefficients));
        for (int i = 0; i < count; ++i) {
            slope_coefficients[i] =
                2.0 * static_cast<double>(i + 1) * surface.coeffs[i];
        }
        // For k=-1 the conic slope is exactly r/R and belongs in the linear
        // polynomial coefficient; treating it separately would miss exact
        // cancellation and incorrectly manufacture a finite domain.
        if (finite_radius && conic_term == 0.0) {
            slope_coefficients[0] += 1.0 / surface.radius;
        }
        int highest = -1;
        for (int i = kMaxDeviceAsphereCoefficients - 1; i >= 0; --i) {
            if (slope_coefficients[i] != 0.0 &&
                d_finite(slope_coefficients[i])) {
                highest = i;
                break;
            }
        }
        double conic_bound = 0.0;
        if (finite_radius && conic_term < 0.0) {
            conic_bound = 1.0 / sqrt(-conic_term);
        }
        if (highest < 0) {
            if (!(conic_bound > max_slope)) {
                return 2;
            }
            const double denominator =
                1.0 - (-conic_term) * max_slope * max_slope;
            if (!(denominator > 0.0)) {
                return 2;
            }
            const double boundary =
                fabs(surface.radius) * max_slope / sqrt(denominator);
            if (!(boundary > 0.0) || !d_finite(boundary)) {
                return 3;
            }
            *domain_radius = boundary;
            return 1;
        }

        const double leading = fabs(slope_coefficients[highest]);
        if (!(leading > 0.0) || !d_finite(leading)) {
            return 3;
        }
        double dominance_radius_squared = 1.0;
        if (highest > 0) {
            const double factor = 2.0 * static_cast<double>(highest);
            for (int i = 0; i < highest; ++i) {
                const double lower = fabs(slope_coefficients[i]);
                if (!(lower > 0.0)) {
                    continue;
                }
                const double exponent =
                    1.0 / static_cast<double>(highest - i);
                const double required = pow(factor * lower / leading, exponent);
                if (!d_finite(required)) {
                    return 3;
                }
                dominance_radius_squared =
                    fmax(dominance_radius_squared, required);
            }
        }
        const double target = 2.0 * (max_slope + conic_bound + 1.0);
        const double magnitude_radius = pow(
            target / leading,
            1.0 / static_cast<double>(2 * highest + 1));
        search_upper = fmax(1.0, fmax(sqrt(dominance_radius_squared), magnitude_radius));
        if (!(search_upper > 0.0) || !d_finite(search_upper)) {
            return 3;
        }
        // One outward step preserves the strict dominance argument after the
        // binary64 power/root evaluations used to construct the ceiling.
        search_upper = nextafter(search_upper * (1.0 + 64.0 * DBL_EPSILON), DBL_MAX);
    }

    constexpr int kMaxDepth = 56;
    constexpr int kStackCapacity = kMaxDepth + 1;
    constexpr int kMaxNodes = 65536;
    double pending_lo[kStackCapacity];
    double pending_hi[kStackCapacity];
    int pending_depth[kStackCapacity];
    int pending_count = 0;
    int nodes = 0;
    double lo = 0.0;
    double hi = search_upper;
    int depth = 0;
    while (true) {
        if (++nodes > kMaxNodes) {
            return 3;
        }
        if (interval_nodes) {
            ++(*interval_nodes);
        }
        if (!d_asphere_slope_interval_below_limit(
                surface, lo, hi, max_slope)) {
            const double width_tolerance =
                2.0e-13 * fmax(1.0, fmax(fabs(lo), fabs(hi)));
            if (depth >= kMaxDepth || hi - lo <= width_tolerance) {
                if (leaf_tests) {
                    ++(*leaf_tests);
                }
                const double mid = 0.5 * (lo + hi);
                const double margin_lo =
                    d_asphere_slope_margin(surface, lo, max_slope);
                const double margin_mid =
                    d_asphere_slope_margin(surface, mid, max_slope);
                const double margin_hi =
                    d_asphere_slope_margin(surface, hi, max_slope);
                double bracket_lo = lo;
                double bracket_hi = hi;
                if (margin_lo <= 0.0) {
                    *domain_radius = lo;
                    return 1;
                }
                if (margin_mid <= 0.0) {
                    bracket_hi = mid;
                } else if (!(margin_hi <= 0.0)) {
                    // All earlier left-first intervals were strictly proved
                    // physical.  At machine-scale width, retain their right
                    // endpoint (this leaf's left edge) as a conservative
                    // finite domain instead of converting the whole surface
                    // to unresolved.  No possibly invalid radius is admitted.
                    *domain_radius = lo;
                    return d_finite(*domain_radius) ? 1 : 3;
                } else {
                    bracket_lo = mid;
                }
                for (int iteration = 0; iteration < 64; ++iteration) {
                    const double probe = 0.5 * (bracket_lo + bracket_hi);
                    if (d_asphere_slope_margin(
                            surface, probe, max_slope) > 0.0) {
                        bracket_lo = probe;
                    } else {
                        bracket_hi = probe;
                    }
                }
                if (bisection_iterations) {
                    *bisection_iterations += 64;
                }
                *domain_radius = bracket_hi;
                return d_finite(*domain_radius) ? 1 : 3;
            }
            const double mid = 0.5 * (lo + hi);
            if (!(mid > lo) || !(hi > mid) ||
                pending_count >= kStackCapacity) {
                return 3;
            }
            pending_lo[pending_count] = mid;
            pending_hi[pending_count] = hi;
            pending_depth[pending_count] = depth + 1;
            ++pending_count;
            hi = mid;
            ++depth;
            continue;
        }
        if (pending_count <= 0) {
            // The constructed ceiling is guaranteed to be outside for every
            // non-zero unbounded polynomial, and finite conics diverge at the
            // physical edge.  Failure to observe that crossing is numerical
            // uncertainty, not proof of an unbounded domain.
            return 3;
        }
        --pending_count;
        lo = pending_lo[pending_count];
        hi = pending_hi[pending_count];
        depth = pending_depth[pending_count];
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_materialize_asphere_slope_root_domain(
    DeviceSurface* surface,
    const DeviceConfig& cfg) {
    if (!surface) {
        return;
    }
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    // Candidate variables may replace any asphere coefficient after the host
    // surface was copied.  Refresh the exact departure predicate once per
    // candidate/surface materialization so every later ray avoids rescanning
    // the same coefficient array.
    surface->asphere_polynomial_eval_mode &=
        ~(de_cuda_detail::kAspherePolynomialDepartureBit |
          de_cuda_detail::kAsphereMaterializedFiniteConicBit);
    if (surface->kind == kKindEvenAsphere) {
        const int coefficient_count = max(
            0, min(surface->coeff_count, kMaxDeviceAsphereCoefficients));
        bool has_polynomial_departure = false;
        for (int i = 0; i < coefficient_count; ++i) {
            has_polynomial_departure =
                has_polynomial_departure || surface->coeffs[i] != 0.0;
        }
        if (has_polynomial_departure) {
            surface->asphere_polynomial_eval_mode |=
                de_cuda_detail::kAspherePolynomialDepartureBit;
        }
        if (d_finite(surface->radius) &&
            fabs(surface->radius) >= kDeviceEps &&
            fabs(surface->radius) <= 0.5 * kDeviceInf) {
            surface->asphere_polynomial_eval_mode |=
                de_cuda_detail::kAsphereMaterializedFiniteConicBit;
        }
        // Under the fixed UC10 unified-domain contract this field stops
        // carrying an aperture radius once the surface is materialized.
        // Reuse it for the conic root-argument scale so the hot scalar sag
        // and residual evaluators do not divide by radius squared per call.
        const double radius_squared = surface->radius * surface->radius;
        surface->slope_root_domain_cached_clear_radius =
            (1.0 + surface->conic) / radius_squared;
    }
#endif
    DeviceAsphereFeasibilityProfile* feasibility =
        cfg.asphere_feasibility_profile;
    if (feasibility) {
        atomicAdd(&feasibility->domain_calls, 1ULL);
    }
    if (!cfg.asphere_slope_root_domain_enabled ||
        surface->kind != kKindEvenAsphere) {
        surface->slope_root_domain_radius_squared = INFINITY;
        surface->slope_root_domain_cached_reference_radius = -1.0;
        surface->slope_root_domain_cached_clear_radius = -1.0;
        surface->unified_slope_domain_status = 0;
        return;
    }
    if (feasibility) {
        atomicAdd(&feasibility->domain_asphere_calls, 1ULL);
    }
    if (cfg.asphere_unified_contract_domain_enabled &&
        cfg.asphere_slope_root_domain_reuse &&
        surface->slope_root_domain_cached_reference_radius == -2.0
#if !defined(OPTILAND_ASPHERE_UC10_RELEASE)
        && surface->slope_root_domain_cached_clear_radius == -2.0
#endif
        ) {
        if (feasibility) {
            atomicAdd(&feasibility->domain_reuse_hits, 1ULL);
        }
        return;
    }

    const double reference_radius = fmax(
        0.0,
        fmax(surface->semi_aperture,
             fmax(surface->mechanical_aperture, surface->aperture_radius)));
    const double clear_radius =
        d_finite(surface->semi_aperture)
            ? fmax(0.0, surface->semi_aperture)
            : 0.0;
    if (cfg.asphere_slope_root_domain_reuse &&
        surface->slope_root_domain_cached_reference_radius == reference_radius &&
        surface->slope_root_domain_cached_clear_radius == clear_radius) {
        // No geometry changes occur between the candidate preparation,
        // aperture trace, and finalization stages.  Equal aperture-derived
        // inputs therefore make the deterministic materialization identical.
        if (feasibility) {
            atomicAdd(&feasibility->domain_reuse_hits, 1ULL);
        }
        return;
    }
    if (feasibility) {
        atomicAdd(&feasibility->domain_recomputes, 1ULL);
    }
    unsigned long long interval_nodes = 0;
    unsigned long long leaf_tests = 0;
    unsigned long long bisection_iterations = 0;
    auto record_domain_work = [&]() {
        if (!feasibility) {
            return;
        }
        atomicAdd(&feasibility->domain_interval_nodes, interval_nodes);
        atomicAdd(&feasibility->domain_leaf_tests, leaf_tests);
        atomicAdd(
            &feasibility->domain_bisection_iterations,
            bisection_iterations);
    };
    if (cfg.asphere_unified_contract_domain_enabled) {
        double domain = INFINITY;
        const int status = d_materialize_unified_contract_domain(
            *surface,
            kAsphereRootDomainMaximumRadialSlope,
            &interval_nodes,
            &leaf_tests,
            &bisection_iterations,
            &domain);
        surface->unified_slope_domain_status = status;
        if (status == 1 && d_finite(domain) && domain >= 0.0) {
            const double radius_squared = domain * domain;
            surface->slope_root_domain_radius_squared =
                d_finite(radius_squared) ? radius_squared : -1.0;
            if (!d_finite(radius_squared)) {
                surface->unified_slope_domain_status = 3;
            }
        } else if (status == 2) {
            surface->slope_root_domain_radius_squared = INFINITY;
        } else {
            surface->slope_root_domain_radius_squared = -1.0;
        }
        // Fixed sentinels make reuse independent of aperture materialization.
        surface->slope_root_domain_cached_reference_radius = -2.0;
#if !defined(OPTILAND_ASPHERE_UC10_RELEASE)
        surface->slope_root_domain_cached_clear_radius = -2.0;
#endif
        record_domain_work();
        return;
    }
    surface->unified_slope_domain_status = 0;
    double physical_limit = INFINITY;
    if (d_finite(surface->radius) && fabs(surface->radius) >= kDeviceEps &&
        fabs(surface->radius) < 0.5 * kDeviceInf &&
        1.0 + surface->conic > 0.0) {
        physical_limit =
            fabs(surface->radius) / sqrt(1.0 + surface->conic);
    }

    double domain = INFINITY;
    if (!(reference_radius > 0.0) || !d_finite(reference_radius)) {
        domain = physical_limit;
    } else {
        constexpr double kPi = 3.14159265358979323846;
        const double max_slope =
            1.0 /
            tan(cfg.asphere_slope_root_domain_min_tangent_deg * kPi / 180.0);
        // If the clear-radius floor is already beyond the slope threshold,
        // the post-scan floor makes the exact final domain equal to that
        // radius regardless of where the first crossing occurred below it.
        if (clear_radius > 0.0 &&
            d_asphere_slope_margin(*surface, clear_radius, max_slope) <= 0.0) {
            domain = clear_radius;
        } else {
            double previous_radius = 0.0;
            double interval_end = fmin(reference_radius, physical_limit);
            constexpr int kSamplesPerExpansion = 256;
            for (int expansion = 0; expansion < 7; ++expansion) {
                const double interval_start = previous_radius;
                if (!(interval_end > interval_start) || !d_finite(interval_end)) {
                    break;
                }
                // Traverse the original 256-cell grid left-to-right.  A
                // signed interval certificate may consume an aligned block
                // of cells at once; ambiguous blocks split until the exact
                // original endpoint test is reached at a one-cell leaf.
                int stack_begin[9];
                int stack_end[9];
                int stack_size = 1;
                stack_begin[0] = 0;
                stack_end[0] = kSamplesPerExpansion;
                const double interval_span = interval_end - interval_start;
                while (stack_size > 0 && !d_finite(domain)) {
                    --stack_size;
                    ++interval_nodes;
                    const int cell_begin = stack_begin[stack_size];
                    const int cell_end = stack_end[stack_size];
                    const double radius_begin =
                        interval_start + interval_span *
                            static_cast<double>(cell_begin) /
                            static_cast<double>(kSamplesPerExpansion);
                    const double radius_end =
                        interval_start + interval_span *
                            static_cast<double>(cell_end) /
                            static_cast<double>(kSamplesPerExpansion);
                    if (d_asphere_slope_interval_below_limit(
                            *surface,
                            radius_begin,
                            radius_end,
                            max_slope)) {
                        previous_radius = radius_end;
                        continue;
                    }
                    if (cell_end - cell_begin > 1) {
                        const int cell_mid =
                            cell_begin + (cell_end - cell_begin) / 2;
                        // LIFO: push right first so the left half is processed
                        // first and the first-crossing order stays unchanged.
                        stack_begin[stack_size] = cell_mid;
                        stack_end[stack_size] = cell_end;
                        ++stack_size;
                        stack_begin[stack_size] = cell_begin;
                        stack_end[stack_size] = cell_mid;
                        ++stack_size;
                        continue;
                    }
                    ++leaf_tests;
                    if (d_asphere_slope_margin(
                            *surface, radius_end, max_slope) <= 0.0) {
                        double lo = previous_radius;
                        double hi = radius_end;
                        for (int iteration = 0; iteration < 64; ++iteration) {
                            const double mid = 0.5 * (lo + hi);
                            if (d_asphere_slope_margin(
                                    *surface, mid, max_slope) > 0.0) {
                                lo = mid;
                            } else {
                                hi = mid;
                            }
                        }
                        bisection_iterations += 64;
                        domain = hi;
                    } else {
                        previous_radius = radius_end;
                    }
                }
                if (d_finite(domain) ||
                    (d_finite(physical_limit) && interval_end >= physical_limit)) {
                    break;
                }
                interval_end = fmin(interval_end * 2.0, physical_limit);
            }
        }
    }

    // The current clear aperture is a lower floor, never an upper clipping
    // radius. Re-running after aperture finalization closes the same
    // root/aperture loop as the validated Round7/9 implementation.
    if (clear_radius > 0.0) {
        domain = fmax(domain, clear_radius);
        if (d_finite(physical_limit)) {
            domain = fmin(domain, physical_limit);
        }
    }
    if (!d_finite(domain)) {
        surface->slope_root_domain_radius_squared = INFINITY;
        surface->slope_root_domain_cached_reference_radius = reference_radius;
        surface->slope_root_domain_cached_clear_radius = clear_radius;
        record_domain_work();
        return;
    }
    const double radius_squared = domain * domain;
    surface->slope_root_domain_radius_squared =
        d_finite(radius_squared) ? radius_squared : INFINITY;
    surface->slope_root_domain_cached_reference_radius = reference_radius;
    surface->slope_root_domain_cached_clear_radius = clear_radius;
    record_domain_work();
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_update_asphere_slope_root_domains(
    DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    if (!surfaces) {
        return;
    }
    for (int i = 0; i < cfg.surface_count; ++i) {
        d_materialize_asphere_slope_root_domain(&surfaces[i], cfg);
    }
}
} // namespace
} // namespace optiland
