#pragma once
#include "../prelude.cuh"
#include "candidate_prefix.cuh"
#include "candidate_root.cuh"
#include "failure_classification.cuh"
#include "first_root_interval.cuh"
#include "prefix_piecewise.cuh"
#include "../geometry/sag_residual.cuh"
#include "../refinement/conic_candidate.cuh"
#include "../refinement/newton_seed.cuh"

namespace optiland::de_cuda_detail {
template <class Capture_root_in_search_domain, class Capture_root_in_materialized_slope_domain>
struct DIntersectUnifiedCertifiedDomainInputs {
    const DeviceSurface& surface;
    double& px;
    double& py;
    double& pz;
    double& lx;
    double& my;
    double& nz;
    DeviceAsphereRootProfile*& profile;
    int*& path_class;
    double*& t;
    double& search_lower;
    double& search_upper;
    const bool& unified_mode;
    const bool& uc1_prefix_mode;
    const bool& uc3_failure_mode;
    const bool& uc6_prefix_eval_reuse_mode;
    Capture_root_in_search_domain& root_in_search_domain;
    Capture_root_in_materialized_slope_domain& root_in_materialized_slope_domain;
};
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
template <class Inputs>
__device__ __forceinline__ native_detail::PhaseOutcome d_intersect_unified_certified_domain(Inputs& inputs) {
    auto& surface = inputs.surface;
    auto& px = inputs.px;
    auto& py = inputs.py;
    auto& pz = inputs.pz;
    auto& lx = inputs.lx;
    auto& my = inputs.my;
    auto& nz = inputs.nz;
    auto& profile = inputs.profile;
    auto& path_class = inputs.path_class;
    auto& t = inputs.t;
    auto& search_lower = inputs.search_lower;
    auto& search_upper = inputs.search_upper;
    auto& unified_mode = inputs.unified_mode;
    auto& uc1_prefix_mode = inputs.uc1_prefix_mode;
    auto& uc3_failure_mode = inputs.uc3_failure_mode;
    auto& uc6_prefix_eval_reuse_mode = inputs.uc6_prefix_eval_reuse_mode;
    auto& root_in_search_domain = inputs.root_in_search_domain;
    auto& root_in_materialized_slope_domain = inputs.root_in_materialized_slope_domain;
    if (unified_mode) {
        if (profile) {
            increment_profile_counter(&profile->unified_calls);
        }
        if (surface.unified_slope_domain_status == 3 ||
            surface.unified_slope_domain_status == 0) {
            if (profile) {
                increment_profile_counter(&profile->unified_unresolved);
            }
            return {native_detail::PhaseFlow::Return, false};
        }
        const bool unbounded_domain =
            surface.unified_slope_domain_status == 2;
        if (profile && unbounded_domain) {
            increment_profile_counter(&profile->unified_unbounded_calls);
        }
        // The locked contract excludes t <= 1e-10.  Starting immediately
        // above that threshold also prevents a previous-surface self root
        // from being accepted by the interval traversal.
        search_lower = fmax(search_lower, nextafter(1.0e-10, DBL_MAX));
        const double newton_tol =
            finite_value(surface.newton_tol) && surface.newton_tol > 0.0
                ? surface.newton_tol
                : 1.0e-10;
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
        const bool has_polynomial_departure =
            (surface.asphere_polynomial_eval_mode &
             kAspherePolynomialDepartureBit) != 0;
#else
        bool has_polynomial_departure = false;
        const int coefficient_count = max(
            0, min(surface.coeff_count, kMaxDeviceAsphereCoefficients));
        for (int i = 0; i < coefficient_count; ++i) {
            has_polynomial_departure =
                has_polynomial_departure || surface.coeffs[i] != 0.0;
        }
#endif
        const bool pure_plane =
            (!finite_value(surface.radius) ||
             fabs(surface.radius) >= 0.5 * kDeviceInf) &&
            !has_polynomial_departure;
        if (pure_plane || (lx == 0.0 && my == 0.0)) {
            double exact_root = NAN;
            if (pure_plane) {
                if (fabs(nz) > kDeviceEps) {
                    exact_root = -pz / nz;
                }
            } else if (fabs(nz) > kDeviceEps) {
                double residual_at_zero = 0.0;
                if (surface_residual_at<true>(
                        surface,
                        px,
                        py,
                        pz,
                        lx,
                        my,
                        nz,
                        0.0,
                        &residual_at_zero)) {
                    exact_root = residual_at_zero / nz;
                }
            }
            const bool exact_ok =
                exact_root > 1.0e-10 &&
                root_in_search_domain(exact_root) &&
                root_in_materialized_slope_domain(exact_root);
            if (profile) {
                increment_profile_counter(
                    exact_ok
                        ? &profile->unified_candidate_successes
                        : &profile->unified_candidate_failures);
                increment_profile_counter(
                    exact_ok
                        ? &profile->unified_hits
                        : &profile->unified_misses);
                if (exact_ok) {
                    increment_profile_counter(
                        &profile->unified_direction_certificates);
                }
            }
            if (exact_ok) {
                *t = exact_root;
                return {native_detail::PhaseFlow::Return, true};
            }
            return {native_detail::PhaseFlow::Return, false};
        }
        double candidate = NAN;
        DeviceAsphereCandidateDiagnostic candidate_diagnostic{};
        const bool collect_candidate_diagnostic =
            profile || uc6_prefix_eval_reuse_mode;
        const bool candidate_solver_ok = intersect_conic_or_asphere(
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            &candidate,
            false,
            collect_candidate_diagnostic ? &candidate_diagnostic : nullptr,
            profile);
        const bool candidate_ok =
            candidate_solver_ok &&
            root_in_search_domain(candidate) &&
            root_in_materialized_slope_domain(candidate) &&
            candidate > 1.0e-10;
        if (profile) {
            increment_profile_counter(
                &profile->unified_candidate_profiled_calls);
            atomicAdd(
                &profile->unified_candidate_seed_attempts,
                static_cast<unsigned long long>(
                    candidate_diagnostic.seed_attempts));
            atomicAdd(
                &profile->unified_candidate_newton_evaluations,
                static_cast<unsigned long long>(
                    candidate_diagnostic.newton_evaluations));
            atomicAdd(
                &profile->unified_candidate_failed_seed_newton_evaluations,
                static_cast<unsigned long long>(
                    candidate_diagnostic.failed_seed_newton_evaluations));
            atomicAdd(
                &profile->unified_candidate_successful_seed_newton_evaluations,
                static_cast<unsigned long long>(
                    candidate_diagnostic.successful_seed_newton_evaluations));
            atomicAdd(
                &profile->unified_candidate_terminal_residual_evaluations,
                static_cast<unsigned long long>(
                    candidate_diagnostic.terminal_residual_evaluations));
            if (candidate_diagnostic.cacheable_success) {
                increment_profile_counter(
                    &profile->unified_candidate_cacheable_successes);
            }
            if (candidate_diagnostic.successful_seed == 1) {
                increment_profile_counter(
                    &profile->unified_candidate_seed1_successes);
            } else if (candidate_diagnostic.successful_seed == 2) {
                increment_profile_counter(
                    &profile->unified_candidate_seed2_successes);
            } else if (candidate_diagnostic.successful_seed == 3) {
                increment_profile_counter(
                    &profile->unified_candidate_seed3_successes);
            }
            const int evaluations = candidate_diagnostic.newton_evaluations;
            unsigned long long* evaluation_histogram =
                &profile->unified_candidate_eval_hist_33_plus;
            if (evaluations <= 0) {
                evaluation_histogram =
                    &profile->unified_candidate_eval_hist_0;
            } else if (evaluations == 1) {
                evaluation_histogram =
                    &profile->unified_candidate_eval_hist_1;
            } else if (evaluations == 2) {
                evaluation_histogram =
                    &profile->unified_candidate_eval_hist_2;
            } else if (evaluations == 3) {
                evaluation_histogram =
                    &profile->unified_candidate_eval_hist_3;
            } else if (evaluations == 4) {
                evaluation_histogram =
                    &profile->unified_candidate_eval_hist_4;
            } else if (evaluations <= 8) {
                evaluation_histogram =
                    &profile->unified_candidate_eval_hist_5_8;
            } else if (evaluations <= 16) {
                evaluation_histogram =
                    &profile->unified_candidate_eval_hist_9_16;
            } else if (evaluations <= 32) {
                evaluation_histogram =
                    &profile->unified_candidate_eval_hist_17_32;
            }
            increment_profile_counter(evaluation_histogram);
            increment_profile_counter(
                candidate_ok
                    ? &profile->unified_candidate_successes
                    : &profile->unified_candidate_failures);
        }
        if (candidate_ok &&
            certify_slope_domain_residual_monotonic(lx, my, nz)) {
            if (profile) {
                increment_profile_counter(
                    &profile->unified_direction_certificates);
                increment_profile_counter(&profile->unified_hits);
            }
            *t = candidate;
            return {native_detail::PhaseFlow::Return, finite_value(*t)};
        }
        double certified_candidate = NAN;
        bool prefix_candidate_ok = false;
        if (uc1_prefix_mode && candidate_ok) {
            DeviceAspherePrefixDiagnostic prefix_diagnostic{};
            prefix_candidate_ok = certify_even_asphere_first_root_candidate(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                search_lower,
                search_upper,
                candidate,
                &certified_candidate,
                collect_candidate_diagnostic ? &candidate_diagnostic : nullptr,
                uc6_prefix_eval_reuse_mode,
                profile ? &prefix_diagnostic : nullptr);
            if (profile) {
                increment_profile_counter(&profile->unified_prefix_attempts);
                atomicAdd(
                    &profile->unified_prefix_polish_evaluations,
                    static_cast<unsigned long long>(
                        prefix_diagnostic.polish_evaluations));
                atomicAdd(
                    &profile->unified_prefix_reused_candidate_pairs,
                    static_cast<unsigned long long>(
                        prefix_diagnostic.reused_candidate_pairs));
                atomicAdd(
                    &profile->unified_prefix_endpoint_residual_evaluations,
                    static_cast<unsigned long long>(
                        prefix_diagnostic.endpoint_residual_evaluations));
                atomicAdd(
                    &profile->unified_prefix_monotonicity_attempts,
                    static_cast<unsigned long long>(
                        prefix_diagnostic.monotonicity_attempts));
            }
        }
        if (prefix_candidate_ok) {
            if (profile) {
                increment_profile_counter(
                    &profile->unified_prefix_certificates);
                increment_profile_counter(&profile->unified_hits);
            }
            *t = certified_candidate;
            return {native_detail::PhaseFlow::Return, finite_value(*t)};
        }

        if (uc3_failure_mode && !candidate_ok && profile) {
            increment_profile_counter(&profile->unified_uc3_attempts);
        }

        // The direction certificate is global over the locked radial domain.
        // A residual-certified candidate that is itself in that domain is
        // therefore the unique domain root; constructing the ray/disk interval
        // cannot add evidence on this overwhelmingly common path.  Defer the
        // quadratic interval (including its square root) until the unified
        // traversal actually needs domain endpoints.
        if (!restrict_search_to_slope_domain(
                surface, px, py, lx, my, &search_lower, &search_upper)) {
            if (profile) {
                increment_profile_counter(&profile->unified_misses);
                if (uc3_failure_mode && !candidate_ok) {
                    increment_profile_counter(
                        &profile->unified_uc3_domain_misses);
                }
            }
            return {native_detail::PhaseFlow::Return, false};
        }

        if (uc3_failure_mode && !candidate_ok) {
            double uc3_root = NAN;
            DeviceAsphereBracketDiagnostic uc3_bracket_diagnostic{};
            const int uc3_classification =
                classify_even_asphere_candidate_failure_monotonic(
                    surface,
                    px,
                    py,
                    pz,
                    lx,
                    my,
                    nz,
                    search_lower,
                    search_upper,
                    &uc3_root,
                    profile ? &uc3_bracket_diagnostic : nullptr,
                    profile);
            if (profile) {
                atomicAdd(
                    &profile->unified_uc3_bracket_calls,
                    static_cast<unsigned long long>(
                        uc3_bracket_diagnostic.calls));
                atomicAdd(
                    &profile->unified_uc3_bracket_successes,
                    static_cast<unsigned long long>(
                        uc3_bracket_diagnostic.successes));
                atomicAdd(
                    &profile->unified_uc3_bracket_residual_evaluations,
                    static_cast<unsigned long long>(
                        uc3_bracket_diagnostic.residual_evaluations));
                atomicAdd(
                    &profile
                         ->unified_uc3_bracket_residual_derivative_evaluations,
                    static_cast<unsigned long long>(
                        uc3_bracket_diagnostic
                            .residual_derivative_evaluations));
            }
            if (uc3_classification != 0) {
                if (profile) {
                    increment_profile_counter(
                        uc3_classification > 0
                            ? &profile->unified_uc3_roots
                            : &profile->unified_uc3_endpoint_misses);
                    increment_profile_counter(
                        uc3_classification > 0
                            ? &profile->unified_hits
                            : &profile->unified_misses);
                }
                if (uc3_classification > 0) {
                    *t = uc3_root;
                    return {native_detail::PhaseFlow::Return, true};
                }
                return {native_detail::PhaseFlow::Return, false};
            }
            if (profile) {
                increment_profile_counter(&profile->unified_uc3_fallbacks);
            }
        }

        const double candidate_guard = candidate_ok
            ? 64.0 * sqrt(fmax(kDeviceEps, newton_tol)) *
                  fmax(1.0, fabs(candidate))
            : 0.0;
        DCertifyUnifiedCandidatePrefixInputs d_certify_unified_candidate_prefix_inputs{
            surface,
            px,
            py,
            pz,
            lx,
            my,
            nz,
            profile,
            path_class,
            t,
            search_lower,
            search_upper,
            newton_tol,
            candidate,
            candidate_ok,
            candidate_guard
        };
const auto d_certify_unified_candidate_prefix_outcome = d_certify_unified_candidate_prefix(d_certify_unified_candidate_prefix_inputs);
if (d_certify_unified_candidate_prefix_outcome.flow == native_detail::PhaseFlow::Return) { return {native_detail::PhaseFlow::Return, d_certify_unified_candidate_prefix_outcome.value}; }


        const double candidate_ceiling = candidate_ok
            ? candidate + candidate_guard
            : search_upper;
        double interval_upper = finite_value(search_upper)
            ? fmin(search_upper, candidate_ceiling)
            : candidate_ceiling;
        if (!finite_value(interval_upper)) {
            if (!has_polynomial_departure) {
                // For a plane or pure conic, the analytic q-form candidate
                // generator is complete.  Its failure is therefore a proof
                // of no strictly-forward principal-branch root.
                if (profile) {
                    increment_profile_counter(&profile->unified_misses);
                    increment_profile_counter(
                        &profile->unified_unbounded_candidate_failures);
                }
                return {native_detail::PhaseFlow::Return, false};
            }
            if (profile) {
                increment_profile_counter(
                    &profile->unified_unbounded_candidate_failures);
                increment_profile_counter(&profile->unified_unresolved);
            }
            return {native_detail::PhaseFlow::Return, false};
        }
        if (!(interval_upper >= search_lower)) {
            if (profile) {
                increment_profile_counter(&profile->unified_misses);
            }
            return {native_detail::PhaseFlow::Return, false};
        }
        if (profile) {
            increment_profile_counter(
                &profile->unified_adaptive_search_calls);
        }
        if (path_class) {
            *path_class |= 2;
        }
        double certified_root = NAN;
        int visited_nodes = 0;
        const DeviceFirstRootSearchStatus status =
            find_first_even_asphere_root_interval(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                search_lower,
                interval_upper,
                candidate_ok ? 4096 : 16384,
                newton_tol,
                &certified_root,
                profile,
                &visited_nodes,
                true);
        if (profile) {
            unsigned long long* histogram_counter =
                visited_nodes <= 1
                    ? &profile->unified_visits_1
                    : (visited_nodes == 2
                           ? &profile->unified_visits_2
                           : (visited_nodes == 3
                                  ? &profile->unified_visits_3
                                  : (visited_nodes == 4
                                         ? &profile->unified_visits_4
                                         : (visited_nodes <= 8
                                                ? &profile->unified_visits_5_8
                                                : &profile->unified_visits_9_plus))));
            increment_profile_counter(histogram_counter);
            atomicMax(
                &profile->unified_max_visits,
                static_cast<unsigned long long>(max(0, visited_nodes)));
        }
        if (status == DeviceFirstRootSearchStatus::Found &&
            root_in_search_domain(certified_root) &&
            root_in_materialized_slope_domain(certified_root) &&
            certified_root > 1.0e-10) {
            if (profile) {
                increment_profile_counter(
                    &profile->unified_adaptive_search_found);
                increment_profile_counter(&profile->unified_hits);
            }
            *t = certified_root;
            return {native_detail::PhaseFlow::Return, finite_value(*t)};
        }
        if (status == DeviceFirstRootSearchStatus::NoRoot) {
            if (profile) {
                increment_profile_counter(
                    &profile->unified_adaptive_search_no_root);
                increment_profile_counter(&profile->unified_misses);
            }
            return {native_detail::PhaseFlow::Return, false};
        }
        if (profile) {
            increment_profile_counter(
                &profile->unified_adaptive_search_ambiguous);
            increment_profile_counter(&profile->unified_unresolved);
        }
        return {native_detail::PhaseFlow::Return, false};
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland::de_cuda_detail
