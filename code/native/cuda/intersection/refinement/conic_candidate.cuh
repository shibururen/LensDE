#pragma once
#include "../prelude.cuh"
#include "../certification/failure_classification.cuh"
#include "../certification/prefix_piecewise.cuh"
#include "../geometry/forward_interval.cuh"
#include "../geometry/sag_residual.cuh"
#include "bounded_seed.cuh"
#include "newton_seed.cuh"

namespace optiland::de_cuda_detail {
__device__ __noinline__ bool intersect_conic_or_asphere(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double* t,
    bool allow_domain_recovery,
    DeviceAsphereCandidateDiagnostic* diagnostic,
    DeviceAsphereRootProfile* uc8_profile) {
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
    diagnostic = nullptr;
    uc8_profile = nullptr;
#endif
    const bool even_asphere = surface.kind == kKindEvenAsphere;
    double candidates[3] = {NAN, NAN, NAN};
    int candidate_identities[3] = {-1, -1, -1};
    int candidate_count = 0;
    auto add_candidate = [&](double value, int identity) {
        if (candidate_count < 3 && finite_value(value)) {
            candidates[candidate_count] = value;
            candidate_identities[candidate_count] = identity;
            ++candidate_count;
        }
    };
    const double dz_safe =
        fabs(nz) > kDeviceEps ? nz : (nz >= 0.0 ? kDeviceEps : -kDeviceEps);
    double best_base_t = NAN;
    double best_base_residual = INFINITY;
    if (even_asphere) {
        add_candidate(-pz / dz_safe, 0);
    }

    if (!finite_value(surface.radius) || fabs(surface.radius) < kDeviceEps ||
        fabs(surface.radius) > 0.5 * kDeviceInf) {
        if (fabs(nz) <= kDeviceEps) {
            if (!even_asphere) {
                return false;
            }
        } else {
            const double guess = -pz / nz;
            if (even_asphere) {
                add_candidate(guess, 0);
            } else {
                consider_surface_branch_root(
                    surface, px, py, pz, lx, my, nz, guess,
                    &best_base_t, &best_base_residual);
            }
        }
    } else {
        const double a =
            surface.conic * nz * nz + lx * lx + my * my + nz * nz;
        const double b =
            2.0 * surface.conic * nz * pz + 2.0 * lx * px +
            2.0 * my * py - 2.0 * nz * surface.radius + 2.0 * nz * pz;
        const double c =
            surface.conic * pz * pz - 2.0 * surface.radius * pz +
            px * px + py * py + pz * pz;
        if (fabs(a) <= kDeviceEps) {
            if (fabs(b) <= kDeviceEps) {
                if (!even_asphere) {
                    return false;
                }
            } else {
                const double guess = -c / b;
                if (even_asphere) {
                    add_candidate(guess, 1);
                } else {
                    consider_surface_branch_root(
                        surface, px, py, pz, lx, my, nz, guess,
                        &best_base_t, &best_base_residual);
                }
            }
        } else {
            const double discriminant = b * b - 4.0 * a * c;
            if (discriminant < 0.0) {
                if (!even_asphere) {
                    return false;
                }
            } else {
                const double root = sqrt(fmax(0.0, discriminant));
                // Match the host's cancellation-safe quadratic root contract.
                // This keeps the near physical root stable when |a| is tiny
                // instead of making acceptance depend on CPU/GPU rounding.
                const double q = -0.5 * (b + copysign(root, b));
                const double t1 =
                    fabs(q) > kDeviceEps ? q / a : -b / (2.0 * a);
                const double t2 = fabs(q) > kDeviceEps ? c / q : t1;
                if (even_asphere) {
                    add_candidate(t1, 1);
                    add_candidate(t2, 2);
                } else {
                    consider_surface_branch_root(
                        surface, px, py, pz, lx, my, nz, t1,
                        &best_base_t, &best_base_residual);
                    consider_surface_branch_root(
                        surface, px, py, pz, lx, my, nz, t2,
                        &best_base_t, &best_base_residual);
                }
            }
        }
    }
    if (!even_asphere) {
        if (!finite_value(best_base_t)) {
            return false;
        }
        *t = fabs(best_base_t) <= 1.0e-10 ? 0.0 : best_base_t;
        return finite_value(*t);
    }

    // Keep the CUDA-DE hot path inexpensive.  Sort the analytic seeds in
    // forward order and refine only until the first usable root is found.
    // The bounded nearest-root search remains available to the aperture
    // recovery path for suspicious, physically implausible intersections.
    const double newton_tol =
        finite_value(surface.newton_tol) && surface.newton_tol > 0.0
            ? surface.newton_tol
            : 1.0e-10;
    for (int i = 1; i < candidate_count; ++i) {
        const double value = candidates[i];
        const int identity = candidate_identities[i];
        int j = i - 1;
        while (j >= 0 && candidates[j] > value) {
            candidates[j + 1] = candidates[j];
            candidate_identities[j + 1] = candidate_identities[j];
            --j;
        }
        candidates[j + 1] = value;
        candidate_identities[j + 1] = identity;
    }
    int failed_evaluations_before_winner = 0;
    int proposed_failed_seeds[2] = {0, 0};
    const int encoded_surface =
        (surface.asphere_polynomial_eval_mode >> 8) - 1;
    const int profile_surface =
        encoded_surface >= 0 &&
                encoded_surface < kMaxAsphereRootProfileSurfaces
            ? encoded_surface
            : -1;
    for (int i = 0; i < candidate_count; ++i) {
        double refined = NAN;
        DeviceAsphereSeedAttemptDiagnostic seed_diagnostic{};
        const int evaluations_before =
            diagnostic ? diagnostic->newton_evaluations : 0;
        if (diagnostic) {
            ++diagnostic->seed_attempts;
        }
        const bool refined_ok = refine_even_asphere_root_fast(
                surface,
                px,
                py,
                pz,
                lx,
                my,
                nz,
                candidates[i],
                newton_tol,
                &refined,
                diagnostic,
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
                nullptr);
#else
                (uc8_profile ||
                 (surface.asphere_polynomial_eval_mode & 12) != 0)
                    ? &seed_diagnostic
                    : nullptr);
#endif
        const int seed_evaluations = diagnostic
            ? diagnostic->newton_evaluations - evaluations_before
            : 0;
        const int identity = max(0, min(2, candidate_identities[i]));
        if (uc8_profile) {
            const int sign_class = candidates[i] < -kDeviceEps
                ? 0
                : (fabs(candidates[i]) <= kDeviceEps ? 1 : 2);
            atomicAdd(
                &uc8_profile->unified_uc8_seed_identity_attempts[identity],
                1ULL);
            atomicAdd(
                &uc8_profile->unified_uc8_seed_identity_evaluations[identity],
                static_cast<unsigned long long>(seed_evaluations));
            atomicAdd(
                &uc8_profile->unified_uc8_seed_position_attempts[i], 1ULL);
            atomicAdd(
                &uc8_profile->unified_uc8_seed_position_evaluations[i],
                static_cast<unsigned long long>(seed_evaluations));
            atomicAdd(
                &uc8_profile->unified_uc8_seed_sign_attempts[sign_class],
                1ULL);
            const int histogram_index =
                uc8_candidate_evaluation_histogram_index(seed_evaluations);
            atomicAdd(
                refined_ok
                    ? &uc8_profile
                           ->unified_uc8_seed_success_eval_hist[histogram_index]
                    : &uc8_profile
                           ->unified_uc8_seed_failure_eval_hist[histogram_index],
                1ULL);
            atomicAdd(
                refined_ok
                    ? &uc8_profile
                           ->unified_uc8_seed_identity_successes[identity]
                    : &uc8_profile
                           ->unified_uc8_seed_identity_failures[identity],
                1ULL);
            atomicAdd(
                refined_ok
                    ? &uc8_profile->unified_uc8_seed_position_successes[i]
                    : &uc8_profile->unified_uc8_seed_position_failures[i],
                1ULL);
            if (seed_diagnostic.first_step_observed) {
                increment_profile_counter(
                    &uc8_profile->unified_uc8_first_step_total);
                if ((seed_diagnostic.first_step_class & 1) != 0) {
                    increment_profile_counter(
                        &uc8_profile
                             ->unified_uc8_first_step_derivative_guarded);
                }
                if ((seed_diagnostic.first_step_class & 2) != 0) {
                    increment_profile_counter(
                        &uc8_profile->unified_uc8_first_step_nonfinite);
                }
                if ((seed_diagnostic.first_step_class & 4) != 0) {
                    increment_profile_counter(
                        &uc8_profile->unified_uc8_first_step_clamped);
                }
                if ((seed_diagnostic.first_step_class & 8) != 0) {
                    increment_profile_counter(
                        &uc8_profile->unified_uc8_first_step_next_invalid);
                }
            }
            if (seed_diagnostic.first_step_ratio_bucket >= 0) {
                increment_profile_counter(
                    &uc8_profile->unified_uc8_first_step_ratio_hist[
                        seed_diagnostic.first_step_ratio_bucket]);
            }
            if (seed_diagnostic.second_residual_ratio_bucket >= 0) {
                increment_profile_counter(
                    &uc8_profile->unified_uc8_second_residual_ratio_hist[
                        seed_diagnostic.second_residual_ratio_bucket]);
            }
            for (int rule = 0; rule < 2; ++rule) {
                const int rule_bit = 1 << rule;
                if ((seed_diagnostic.triage_trigger_mask & rule_bit) == 0) {
                    continue;
                }
                increment_profile_counter(
                    &uc8_profile->unified_uc8_triage_proposals[rule]);
                if (profile_surface >= 0) {
                    increment_profile_counter(
                        &uc8_profile->unified_uc8_triage_proposals_by_surface
                            [rule][profile_surface]);
                }
                if ((seed_diagnostic.triage_actual_abort_mask & rule_bit) != 0) {
                    increment_profile_counter(
                        &uc8_profile->unified_uc8_triage_actual_aborts[rule]);
                    if (profile_surface >= 0) {
                        increment_profile_counter(
                            &uc8_profile
                                 ->unified_uc8_triage_actual_aborts_by_surface
                                     [rule][profile_surface]);
                    }
                    continue;
                }
                const int trigger_evaluations =
                    seed_diagnostic.triage_trigger_evaluations[rule];
                const unsigned long long remaining_evaluations =
                    static_cast<unsigned long long>(max(
                        0, seed_evaluations - trigger_evaluations));
                if (refined_ok) {
                    increment_profile_counter(
                        &uc8_profile
                             ->unified_uc8_triage_eventual_seed_successes[rule]);
                    atomicAdd(
                        &uc8_profile
                             ->unified_uc8_triage_remaining_success_evaluations
                                 [rule],
                        remaining_evaluations);
                    if (profile_surface >= 0) {
                        increment_profile_counter(
                            &uc8_profile
                                 ->unified_uc8_triage_eventual_successes_by_surface
                                     [rule][profile_surface]);
                    }
                } else {
                    increment_profile_counter(
                        &uc8_profile
                             ->unified_uc8_triage_eventual_seed_failures[rule]);
                    atomicAdd(
                        &uc8_profile
                             ->unified_uc8_triage_remaining_failure_evaluations
                                 [rule],
                        remaining_evaluations);
                    if (profile_surface >= 0) {
                        increment_profile_counter(
                            &uc8_profile
                                 ->unified_uc8_triage_eventual_failures_by_surface
                                     [rule][profile_surface]);
                        atomicAdd(
                            &uc8_profile
                                 ->unified_uc8_triage_remaining_failure_evaluations_by_surface
                                     [rule][profile_surface],
                            remaining_evaluations);
                    }
                    ++proposed_failed_seeds[rule];
                }
            }
        }
        if (refined_ok) {
            if (diagnostic) {
                diagnostic->successful_seed = i + 1;
                diagnostic->successful_seed_newton_evaluations +=
                    seed_evaluations;
            }
            if (uc8_profile) {
                for (int threshold_index = 0; threshold_index < 4;
                     ++threshold_index) {
                    const int threshold = 4 << threshold_index;
                    if (seed_evaluations > threshold) {
                        increment_profile_counter(
                            &uc8_profile
                                 ->unified_uc8_success_after_evaluations
                                     [threshold_index]);
                    }
                }
                atomicAdd(
                    &uc8_profile
                         ->unified_uc8_failed_evaluations_before_success_position
                             [i],
                    static_cast<unsigned long long>(
                        failed_evaluations_before_winner));
                atomicAdd(
                    &uc8_profile
                         ->unified_uc8_failed_evaluations_before_success_identity
                             [identity],
                    static_cast<unsigned long long>(
                        failed_evaluations_before_winner));
                for (int rule = 0; rule < 2; ++rule) {
                    if (proposed_failed_seeds[rule] > 0) {
                        atomicAdd(
                            &uc8_profile
                                 ->unified_uc8_triage_failed_seed_later_candidate_successes
                                     [rule],
                            static_cast<unsigned long long>(
                                proposed_failed_seeds[rule]));
                    }
                }
            }
            *t = refined;
            return true;
        }
        failed_evaluations_before_winner += seed_evaluations;
        if (diagnostic) {
            diagnostic->failed_seed_newton_evaluations += seed_evaluations;
        }
    }

    if (uc8_profile) {
        atomicAdd(
            &uc8_profile
                 ->unified_uc8_failed_evaluations_without_candidate_success,
            static_cast<unsigned long long>(failed_evaluations_before_winner));
        for (int rule = 0; rule < 2; ++rule) {
            if (proposed_failed_seeds[rule] > 0) {
                atomicAdd(
                    &uc8_profile
                         ->unified_uc8_triage_failed_seed_without_candidate_successes
                             [rule],
                    static_cast<unsigned long long>(proposed_failed_seeds[rule]));
            }
        }
    }

    if (!allow_domain_recovery) {
        return false;
    }

    // Keep the ordinary CUDA-DE path unchanged. Only rays for which every
    // analytic/Newton seed failed pay for a domain-bounded recovery attempt.
    // High-order departures can create a real intersection even when the base
    // conic discriminant is negative and the plane seed lies outside the
    // conic's real radial domain.
    const double plane_seed = -pz / dz_safe;
    const double finite_seed =
        finite_value(plane_seed) && plane_seed >= 0.0 ? plane_seed : 0.0;
    const double search_ceiling =
        finite_seed + fmax(1.0, fabs(finite_seed) + 1.0);
    double fallback_lo = 0.0;
    double fallback_hi = 0.0;
    if (asphere_forward_search_interval(
            surface,
            px,
            py,
            lx,
            my,
            0.0,
            search_ceiling,
            &fallback_lo,
            &fallback_hi)) {
        const double shifted_px = px + fallback_lo * lx;
        const double shifted_py = py + fallback_lo * my;
        const double shifted_pz = pz + fallback_lo * nz;
        double local_root = NAN;
        if (refine_even_asphere_root(
                surface,
                shifted_px,
                shifted_py,
                shifted_pz,
                lx,
                my,
                nz,
                fallback_hi - fallback_lo,
                newton_tol,
                &local_root)) {
            const double recovered = fallback_lo + local_root;
            if (finite_value(recovered) && recovered >= -kDeviceEps) {
                *t = fabs(recovered) <= kDeviceEps ? 0.0 : recovered;
                return true;
            }
        }
    }
    return false;
}
} // namespace optiland::de_cuda_detail

namespace optiland::de_cuda_detail {
__device__ __noinline__ bool intersect_asphere_from_local_seed(
    const DeviceSurface& surface,
    double px,
    double py,
    double pz,
    double lx,
    double my,
    double nz,
    double seed,
    double* t) {
    if (!t || surface.kind != kKindEvenAsphere ||
        !finite_value(seed) || seed < 0.0) {
        return false;
    }
    const double newton_tol =
        finite_value(surface.newton_tol) && surface.newton_tol > 0.0
            ? surface.newton_tol
            : 1.0e-10;
    return refine_even_asphere_root_fast(
        surface, px, py, pz, lx, my, nz, seed, newton_tol, t);
}
} // namespace optiland::de_cuda_detail
