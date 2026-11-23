/*
 * Initialize HybridSession options, dimensions, population and the initial scored incumbent.
 * HybridSession::run calls this once before outer epochs and establishes the session random stream.
 * Preserve population seeding, physical/scaled conversion and initial evaluation order.
 */
#include "hybrid_session.hpp"

namespace optiland { namespace optimizer_detail {

void HybridSession::initialize() {
    run_start = Clock::now();
    dim = static_cast<int>(variables_.size());
    if (dim == 0) {
        throw std::invalid_argument("Hybrid DE-DLS requires at least one variable");
    }
    sizing_dim = population_sizing_dimension(variables_);
    internal_mode = options.internal_scaling;
    de_options = options.de;
    de_options.internal_scaling = internal_mode;
    polish_options = options.polish;
    polish_options.internal_scaling = internal_mode;
    pop_size = std::max(
        5,
        de_options.use_popsize_multiplier
            ? std::max(1, de_options.popsize_multiplier) * sizing_dim
            : de_options.population_size);
    outer_epochs = std::max(1, options.outer_epochs);
    inner_generations = std::max(1, de_options.max_generations);
    parent_count = std::clamp(
        options.parent_count > 0 ? options.parent_count : std::max(1, pop_size / 20),
        1,
        pop_size);
    requested_refresh_count = options.refresh_generation.target_count > 0
            ? options.refresh_generation.target_count
            : static_cast<int>(std::round(std::max(0.0, options.refresh_fraction) * pop_size));
    refresh_count = std::clamp(
        requested_refresh_count,
        0,
        std::max(0, pop_size - 1));
    timing = {};
    phase_start = Clock::now();
    rng = std::mt19937(de_options.seed);
    uni = std::uniform_real_distribution<double>(0.0, 1.0);
    f_dist = std::uniform_real_distribution<double>(
        std::min(de_options.mutation_min, de_options.mutation_max),
        std::max(de_options.mutation_min, de_options.mutation_max));
    strategy = normalized_strategy(de_options.strategy);
    population = make_initial_de_population(base_, variables_, pop_size, de_options.initial_generation, rng, internal_mode);
    initial_values = initial_physical_values(variables_);
    initial_scaled = physical_to_scaled(variables_, initial_values, internal_mode);
    set_population_row(population, 0, initial_scaled, variables_, internal_mode);
    inject_physical_population_seeds(
        variables_,
        population,
        pop_size,
        de_options.initial_population_values,
        1,
        internal_mode);
    timing.setup_seconds += seconds_since(phase_start);
    used_cuda = false;
    eval_used_cuda = false;
    phase_start = Clock::now();
    physical_population = scaled_population_to_physical_flat(variables_, population, pop_size, internal_mode);
    timing.physical_conversion_seconds += seconds_since(phase_start);
    phase_start = Clock::now();
    merits = evaluate_population(
        physical_population,
        pop_size,
        de_options.use_cuda,
        &eval_used_cuda,
        &timing);
    timing.initial_evaluation_seconds += seconds_since(phase_start);
    used_cuda = used_cuda || eval_used_cuda;
    initial_score = evaluate_score_for_scaled(initial_scaled);
    merits[0] = initial_score.merit;
    best_scaled = initial_scaled;
    best_score = initial_score;
    auto [population_best_scaled, population_best_score] = update_best_from_population();
    if (population_best_score.merit < best_score.merit) {
        best_scaled = std::move(population_best_scaled);
        best_score = std::move(population_best_score);
    }
    if (progress) {
        progress(0, best_score, scaled_to_physical(variables_, best_scaled, internal_mode));
    }
    total_de_generations = 0;
    de_seconds = seconds_since(run_start);
    polish_seconds = 0.0;
    polish_runs = 0;
    polish_improvements = 0;
    emit_hybrid_trace(
        "initial",
        0,
        de_seconds,
        best_score.merit,
        std::numeric_limits<double>::quiet_NaN(),
        best_score,
        best_scaled,
        0,
        0,
        0,
        0,
        used_cuda);
}

} }  // namespace optiland::optimizer_detail
