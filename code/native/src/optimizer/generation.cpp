/*
 * Dispatch configured population-generation modes and enforce candidate/time/trace acceptance limits.
 * Initialization and refresh provide variable/system metadata, a seeded RNG and generation options.
 * Preserve strategy dispatch, retry accounting and accepted-row order while consuming the supplied stream.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

std::vector<double> make_population_generation_rows(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& population,
    int dim,
    const std::vector<int>& parent_indices,
    int row_count,
    const PopulationGenerationOptions& options,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni) {
    std::vector<double> rows(static_cast<std::size_t>(std::max(0, row_count)) * dim, 0.0);
    if (row_count <= 0 || dim <= 0) {
        return rows;
    }
    std::uniform_int_distribution<int> parent_dist(
        0,
        std::max(0, static_cast<int>(parent_indices.size()) - 1));
    const bool restricted =
        options.mode == PopulationGenerationMode::RestrictedPerturbation &&
        !parent_indices.empty();
    std::uniform_int_distribution<int> seed_pool_dist(
        0,
        std::max(0, static_cast<int>(options.seed_pool_values.size()) - 1));
    const double seed_pool_fraction = std::clamp(options.seed_pool_fraction, 0.0, 1.0);
    const double seed_pool_perturbation =
        std::clamp(options.seed_pool_perturbation, 0.0, 1.0);
    for (int row = 0; row < row_count; ++row) {
        std::vector<double> parent;
        const std::vector<double>* seed_row = nullptr;
        if (!options.seed_pool_values.empty() &&
            seed_pool_fraction > 0.0 &&
            uni(rng) < seed_pool_fraction) {
            const auto& candidate_seed =
                options.seed_pool_values[static_cast<std::size_t>(seed_pool_dist(rng))];
            if (candidate_seed.size() == static_cast<std::size_t>(dim)) {
                seed_row = &candidate_seed;
            }
        }
        if (restricted) {
            const int parent_index = parent_indices[static_cast<std::size_t>(parent_dist(rng))];
            parent = population_row(population, parent_index, dim);
        }
        if (options.mode == PopulationGenerationMode::SagGeometryPerturbation) {
            const int parent_index = parent_indices.empty()
                                         ? 0
                                         : parent_indices.front();
            const std::vector<double> parent_row =
                population_row(population, parent_index, dim);
            const std::vector<double> generated = make_sag_geometry_generation_row(
                variables,
                parent_row,
                mode,
                options.asphere_sag_perturbation_um,
                options.geometry_perturbation_fraction,
                rng,
                uni);
            for (int j = 0; j < dim; ++j) {
                rows[static_cast<std::size_t>(row) * dim + j] =
                    generated[static_cast<std::size_t>(j)];
            }
            continue;
        }
        if (options.mode == PopulationGenerationMode::TopHalfSingleSurfaceCorrelatedSag) {
            const int parent_index = parent_indices.empty()
                                         ? 0
                                         : parent_indices[static_cast<std::size_t>(
                                               parent_dist(rng))];
            const std::vector<double> parent_row =
                population_row(population, parent_index, dim);
            const std::vector<double> generated =
                make_single_surface_correlated_sag_generation_row(
                    variables,
                    parent_row,
                    mode,
                    options.asphere_sag_perturbation_um,
                    options.asphere_sag_correlation_length,
                    rng);
            for (int j = 0; j < dim; ++j) {
                rows[static_cast<std::size_t>(row) * dim + j] =
                    generated[static_cast<std::size_t>(j)];
            }
            continue;
        }
        if (options.mode == PopulationGenerationMode::TopHalfSingleBlock) {
            const int parent_index = parent_indices.empty()
                                         ? 0
                                         : parent_indices[static_cast<std::size_t>(parent_dist(rng))];
            const std::vector<double> parent_row =
                population_row(population, parent_index, dim);
            const std::vector<double> generated = make_single_block_refresh_row(
                variables, parent_row, options, mode, rng, uni);
            for (int j = 0; j < dim; ++j) {
                rows[static_cast<std::size_t>(row) * dim + j] =
                    generated[static_cast<std::size_t>(j)];
            }
            continue;
        }
        if (options.mode == PopulationGenerationMode::TopHalfCoupledSurface) {
            const int parent_index = parent_indices.empty()
                                         ? 0
                                         : parent_indices[static_cast<std::size_t>(parent_dist(rng))];
            const std::vector<double> parent_row =
                population_row(population, parent_index, dim);
            const std::vector<double> generated = make_coupled_surface_refresh_row(
                variables, parent_row, options, mode, rng, uni);
            for (int j = 0; j < dim; ++j) {
                rows[static_cast<std::size_t>(row) * dim + j] =
                    generated[static_cast<std::size_t>(j)];
            }
            continue;
        }
        if (!seed_row && options.mode == PopulationGenerationMode::PhysicsConstrained) {
            const std::vector<double> generated = make_physics_constrained_generation_row(
                system,
                variables,
                options,
                mode,
                rng,
                uni);
            for (int j = 0; j < dim; ++j) {
                rows[static_cast<std::size_t>(row) * dim + j] =
                    generated[static_cast<std::size_t>(j)];
            }
            continue;
        }
        for (int j = 0; j < dim; ++j) {
            const auto& var = variables[static_cast<std::size_t>(j)];
            const auto [lo, hi] = internal_bounds(var, mode);
            double value = lo + uni(rng) * (hi - lo);
            if (seed_row) {
                const double center_physical =
                    (*seed_row)[static_cast<std::size_t>(j)];
                value = sample_restricted_physical_value(
                    system,
                    var,
                    center_physical,
                    options,
                    seed_pool_perturbation,
                    mode,
                    rng,
                    uni);
            } else if (restricted) {
                const double center_internal = parent[static_cast<std::size_t>(j)];
                const double center_physical = internal_to_physical(var, center_internal, mode);
                const double span = restricted_generation_span(system, var, center_physical, options);
                if (span > 0.0) {
                    const double phys_lo = center_physical - span;
                    const double phys_hi = center_physical + span;
                    const double sampled =
                        phys_lo <= phys_hi ? phys_lo + uni(rng) * (phys_hi - phys_lo)
                                           : maybe_clip_continuous(
                                                 var,
                                                 center_physical,
                                                 var.lower,
                                                 var.upper);
                    value = physical_to_internal(var, sampled, mode);
                } else if (is_material_variable(var)) {
                    value = options.perturb_material_variables
                                ? lo + uni(rng) * (hi - lo)
                                : physical_to_internal(var, center_physical, mode);
                } else if (is_glass_choice_variable(var)) {
                    if (options.perturb_material_variables) {
                        std::uniform_int_distribution<int> choice_dist(
                            glass_choice_lower(var),
                            std::max(glass_choice_lower(var), glass_choice_upper(var)));
                        value = physical_to_internal(var, static_cast<double>(choice_dist(rng)), mode);
                    } else {
                        value = physical_to_internal(var, center_physical, mode);
                    }
                } else if (!options.perturb_other_variables) {
                    value = physical_to_internal(var, center_physical, mode);
                }
            } else if (is_glass_choice_variable(var)) {
                std::uniform_int_distribution<int> choice_dist(
                    glass_choice_lower(var),
                    std::max(glass_choice_lower(var), glass_choice_upper(var)));
                value = physical_to_internal(var, static_cast<double>(choice_dist(rng)), mode);
            }
            rows[static_cast<std::size_t>(row) * dim + j] =
                maybe_clip_continuous(var, value, lo, hi);
        }
    }
    return rows;
}

int population_generation_candidate_limit(
    const PopulationGenerationOptions& options,
    int target_count) {
    if (!options.trace_feasible_gate) {
        return std::max(0, target_count);
    }
    if (options.max_candidates > 0) {
        return std::max(target_count, options.max_candidates);
    }
    return std::max(target_count, target_count * 32);
}

bool population_generation_time_exhausted(
    Clock::time_point start,
    const PopulationGenerationOptions& options) {
    return options.max_seconds > 0.0 && seconds_since(start) >= options.max_seconds;
}

bool score_trace_feasible_for_generation(const Score& score, const MeritConfig& config) {
    const double system_coverage = std::isfinite(score.trace_coverage) ? score.trace_coverage : 0.0;
    const double field_coverage =
        std::isfinite(score.min_field_coverage) ? score.min_field_coverage : 0.0;
    return score.trace_ok &&
           score.distortion_sign_flip_fields <= 0 &&
           system_coverage + 1.0e-9 >= config.min_system_coverage &&
           field_coverage + 1.0e-9 >= config.min_field_coverage &&
           (!config.require_chief_ray ||
            (score.chief_ray_ok && score.chief_ray_failed_fields == 0)) &&
           (!config.require_off_axis_field_coverage ||
            score.zero_coverage_off_axis_fields == 0);
}

bool polish_score_trace_acceptable(
    const Score& score,
    const MeritConfig& config,
    const PolishOptions& options) {
    if (!options.trace_acceptance_enabled) {
        return true;
    }
    if (score.distortion_sign_flip_fields > 0) {
        return false;
    }
    const double system_coverage =
        std::isfinite(score.trace_coverage) ? score.trace_coverage : 0.0;
    const double field_coverage =
        std::isfinite(score.min_field_coverage) ? score.min_field_coverage : 0.0;
    const bool coverage_ok =
        system_coverage + options.trace_coverage_epsilon >= config.min_system_coverage &&
        field_coverage + options.trace_coverage_epsilon >= config.min_field_coverage;
    const bool chief_ok =
        !config.require_chief_ray ||
        (score.chief_ray_ok && score.chief_ray_failed_fields == 0);
    const bool off_axis_ok =
        !config.require_off_axis_field_coverage ||
        score.zero_coverage_off_axis_fields == 0;
    const bool strict_trace_ok = score.trace_ok && coverage_ok && chief_ok && off_axis_ok;
    const bool near_trace_ok = coverage_ok && chief_ok && off_axis_ok;
    return strict_trace_ok ||
           (options.allow_near_trace_infeasible && near_trace_ok) ||
           !options.require_trace_feasible;
}

}  // namespace optimizer_detail

}  // namespace optiland
