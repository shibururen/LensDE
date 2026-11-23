#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "execution_context.cuh"

namespace optiland {
bool PopulationScoringExecution::reject(const char* reason) {
    if (debug_reject) {
        std::fprintf(stderr, "DE-CUDA scorer unavailable: %s\n", reason);
    }
    return false;
}
} // namespace optiland

namespace optiland {
bool PopulationScoringExecution::track_cuda_status(cudaError_t status) {
    if (status != cudaSuccess && first_cuda_error == cudaSuccess) {
        first_cuda_error = status;
    }
    return status == cudaSuccess;
}
} // namespace optiland

namespace optiland {
void PopulationScoringExecution::print_asphere_deep_profile() {
    const DeviceAsphereDeepProfile& p = host_asphere_deep_profile;
    const unsigned long long problems =
        p.u1_failure_reference_success +
        p.u1_success_reference_failure +
        p.both_success_different_root;
    std::fprintf(
        stderr,
        "DE-CUDA asphere deep shadow: calls=%llu problems=%llu "
        "both_success_same_root=%llu "
        "u1_failure_reference_success=%llu "
        "u1_success_reference_failure=%llu both_failure=%llu "
        "both_success_different_root=%llu\n",
        p.calls,
        problems,
        p.both_success_same_root,
        p.u1_failure_reference_success,
        p.u1_success_reference_failure,
        p.both_failure,
        p.both_success_different_root);
    const char* role_names[kAsphereDeepProfileRoles] = {
        "chief", "edge", "auxiliary"};
    for (int role = 0; role < kAsphereDeepProfileRoles; ++role) {
        if (p.calls_by_role[role] == 0) {
            continue;
        }
        std::fprintf(
            stderr,
            "DE-CUDA asphere deep role: role=%s calls=%llu problems=%llu "
            "u1_failures=%llu different_roots=%llu\n",
            role_names[role],
            p.calls_by_role[role],
            p.problems_by_role[role],
            p.u1_failures_by_role[role],
            p.different_roots_by_role[role]);
    }
    const char* site_names[kAsphereDeepProfileSites] = {
        "other",
        "far_paraxial",
        "exact_first_surface_seed",
        "transverse_chief_shift"};
    for (int site = 0; site < kAsphereDeepProfileSites; ++site) {
        if (p.calls_by_site[site] == 0) {
            continue;
        }
        std::fprintf(
            stderr,
            "DE-CUDA asphere deep site: site=%s calls=%llu problems=%llu "
            "u1_failure_reference_success=%llu both_failure=%llu "
            "different_roots=%llu u1_only=%llu\n",
            site_names[site],
            p.calls_by_site[site],
            p.problems_by_site[site],
            p.u1_failure_reference_success_by_site[site],
            p.both_failure_by_site[site],
            p.different_roots_by_site[site],
            p.u1_only_by_site[site]);
    }
    const char* reason_names[kAsphereDeepU1FailureReasons] = {
        "unknown",
        "slope_domain_interval",
        "fast_candidate_no_convergence",
        "candidate_outside_interval"};
    for (int reason = 0;
         reason < kAsphereDeepU1FailureReasons;
         ++reason) {
        if (p.u1_failures_by_reason[reason] == 0) {
            continue;
        }
        std::fprintf(
            stderr,
            "DE-CUDA asphere deep U1 failure reason: reason=%s "
            "failures=%llu reference_success=%llu both_failure=%llu\n",
            reason_names[reason],
            p.u1_failures_by_reason[reason],
            p.u1_failure_reference_success_by_reason[reason],
            p.both_failure_by_reason[reason]);
    }
    std::fprintf(
        stderr,
        "DE-CUDA asphere deep model shadow: comparisons=%llu "
        "p0_valid_u1_valid=%llu p0_valid_u1_invalid=%llu "
        "p0_invalid_u1_valid=%llu both_invalid=%llu "
        "both_valid_equivalent=%llu both_valid_different=%llu "
        "equal_edge_valid_masks=%llu\n",
        p.model_comparisons,
        p.model_p0_valid_u1_valid,
        p.model_p0_valid_u1_invalid,
        p.model_p0_invalid_u1_valid,
        p.model_both_invalid,
        p.model_both_valid_equivalent,
        p.model_both_valid_different,
        p.model_equal_edge_valid_masks);
    std::fprintf(
        stderr,
        "DE-CUDA asphere deep hybrid model shadow: comparisons=%llu "
        "p0_valid_hybrid_valid=%llu p0_valid_hybrid_invalid=%llu "
        "p0_invalid_hybrid_valid=%llu both_invalid=%llu "
        "both_valid_equivalent=%llu both_valid_different=%llu "
        "equal_edge_valid_masks=%llu\n",
        p.hybrid_model_comparisons,
        p.hybrid_model_p0_valid_hybrid_valid,
        p.hybrid_model_p0_valid_hybrid_invalid,
        p.hybrid_model_p0_invalid_hybrid_valid,
        p.hybrid_model_both_invalid,
        p.hybrid_model_both_valid_equivalent,
        p.hybrid_model_both_valid_different,
        p.hybrid_model_equal_edge_valid_masks);
    std::fprintf(
        stderr,
        "DE-CUDA asphere deep exact S1 expected root: calls=%llu "
        "residual_valid=%llu in_slope_domain=%llu "
        "direction_monotonic=%llu reference_success=%llu "
        "reference_same_expected=%llu "
        "reference_different_expected=%llu "
        "u1_failure_reference_success=%llu "
        "u1_failure_reference_same_expected=%llu "
        "u1_failure_reference_different_expected=%llu\n",
        p.exact_s1_calls,
        p.exact_s1_expected_residual_valid,
        p.exact_s1_expected_in_slope_domain,
        p.exact_s1_direction_monotonic,
        p.exact_s1_reference_success,
        p.exact_s1_reference_same_expected,
        p.exact_s1_reference_different_expected,
        p.exact_s1_u1_failure_reference_success,
        p.exact_s1_u1_failure_reference_same_expected,
        p.exact_s1_u1_failure_reference_different_expected);
    for (int surface = 0;
         surface < cfg.surface_count &&
         surface < kMaxAsphereRootProfileSurfaces;
         ++surface) {
        if (p.calls_by_surface[surface] == 0) {
            continue;
        }
        std::fprintf(
            stderr,
            "DE-CUDA asphere deep surface: index=%d calls=%llu "
            "problems=%llu u1_failures=%llu different_roots=%llu "
            "u1_only=%llu role_problems=%llu/%llu/%llu\n",
            surface,
            p.calls_by_surface[surface],
            p.problems_by_surface[surface],
            p.u1_failures_by_surface[surface],
            p.different_roots_by_surface[surface],
            p.u1_only_by_surface[surface],
            p.problems_by_surface_role[
                surface * kAsphereDeepProfileRoles +
                kAsphereDeepRoleChief],
            p.problems_by_surface_role[
                surface * kAsphereDeepProfileRoles +
                kAsphereDeepRoleEdge],
            p.problems_by_surface_role[
                surface * kAsphereDeepProfileRoles +
                kAsphereDeepRoleAuxiliary]);
    }
    for (int field = 0;
         field < cfg.field_count && field < kAsphereDeepProfileFields;
         ++field) {
        if (p.calls_by_field[field] == 0) {
            continue;
        }
        std::fprintf(
            stderr,
            "DE-CUDA asphere deep field: index=%d calls=%llu problems=%llu\n",
            field,
            p.calls_by_field[field],
            p.problems_by_field[field]);
    }
    for (int wave = 0;
         wave < cfg.wavelength_count && wave < kAsphereDeepProfileWaves;
         ++wave) {
        if (p.calls_by_wave[wave] == 0) {
            continue;
        }
        std::fprintf(
            stderr,
            "DE-CUDA asphere deep wave: index=%d calls=%llu problems=%llu\n",
            wave,
            p.calls_by_wave[wave],
            p.problems_by_wave[wave]);
    }
    std::vector<std::pair<unsigned long long, int>> problem_rows;
    const int row_limit = std::min(population_size, kAsphereDeepProfileRows);
    int called_rows = 0;
    for (int row = 0; row < row_limit; ++row) {
        if (p.calls_by_row[row] > 0) {
            ++called_rows;
        }
        if (p.problems_by_row[row] > 0) {
            problem_rows.emplace_back(p.problems_by_row[row], row);
        }
    }
    std::sort(
        problem_rows.begin(),
        problem_rows.end(),
        [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first
                                      : a.second < b.second;
        });
    auto rows_for_fraction = [&](double fraction) {
        if (problems == 0) {
            return 0;
        }
        const unsigned long long target = static_cast<unsigned long long>(
            std::ceil(fraction * static_cast<double>(problems)));
        unsigned long long cumulative = 0;
        for (std::size_t i = 0; i < problem_rows.size(); ++i) {
            cumulative += problem_rows[i].first;
            if (cumulative >= target) {
                return static_cast<int>(i + 1);
            }
        }
        return static_cast<int>(problem_rows.size());
    };
    unsigned long long top10_problems = 0;
    for (std::size_t i = 0;
         i < problem_rows.size() && i < 10;
         ++i) {
        top10_problems += problem_rows[i].first;
    }
    std::fprintf(
        stderr,
        "DE-CUDA asphere deep row concentration: called_rows=%d "
        "problem_rows=%zu rows_for_50pct=%d rows_for_90pct=%d "
        "top10_problems=%llu top10_fraction=%.9f\n",
        called_rows,
        problem_rows.size(),
        rows_for_fraction(0.5),
        rows_for_fraction(0.9),
        top10_problems,
        problems > 0
            ? static_cast<double>(top10_problems) /
                  static_cast<double>(problems)
            : 0.0);
    for (std::size_t rank = 0;
         rank < problem_rows.size() && rank < 10;
         ++rank) {
        const int row = problem_rows[rank].second;
        std::fprintf(
            stderr,
            "DE-CUDA asphere deep top row: rank=%zu row=%d calls=%llu "
            "problems=%llu u1_failures=%llu different_roots=%llu\n",
            rank + 1,
            row,
            p.calls_by_row[row],
            p.problems_by_row[row],
            p.u1_failures_by_row[row],
            p.different_roots_by_row[row]);
    }
}
} // namespace optiland

namespace optiland {
bool PopulationScoringExecution::CudaStatusAdapter::operator()(cudaError_t status) const {
    return owner->track_cuda_status(status);
}
} // namespace optiland
