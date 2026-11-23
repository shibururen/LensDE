/*
 * Run ordinary differential evolution through the public DifferentialEvolution::run entry point.
 * This invocation owns its seeded RNG, flat population and incumbent while using common evaluation helpers.
 * Preserve donor/crossover draw order, batch row mapping, survivor updates and progress boundaries.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

OptimizationResult DifferentialEvolution::run(
    const DifferentialEvolutionOptions& options,
    ProgressCallback progress) {
    const auto run_start = Clock::now();
    const int dim = static_cast<int>(variables_.size());
    if (dim == 0) {
        throw std::invalid_argument("DifferentialEvolution requires at least one variable");
    }

    const int sizing_dim = population_sizing_dimension(variables_);
    const int pop_size = std::max(
        5,
        options.use_popsize_multiplier
            ? std::max(1, options.popsize_multiplier) * sizing_dim
            : options.population_size);
    DeTimingStats timing;
    auto phase_start = Clock::now();
    std::mt19937 rng(options.seed);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::uniform_real_distribution<double> f_dist(
        std::min(options.mutation_min, options.mutation_max),
        std::max(options.mutation_min, options.mutation_max));
    const std::string strategy = normalized_strategy(options.strategy);

    const InternalScalingMode internal_mode = options.internal_scaling;
    std::vector<double> population =
        make_initial_de_population(base_, variables_, pop_size, options.initial_generation, rng, internal_mode);
    std::vector<double> best_values = initial_physical_values(variables_);
    std::vector<double> best_scaled = physical_to_scaled(variables_, best_values, internal_mode);
    set_population_row(population, 0, best_scaled, variables_, internal_mode);
    inject_physical_population_seeds(
        variables_,
        population,
        pop_size,
        options.initial_population_values,
        1,
        internal_mode);
    timing.setup_seconds += seconds_since(phase_start);

    bool used_cuda = false;
    phase_start = Clock::now();
    std::vector<double> physical_population =
        scaled_population_to_physical_flat(variables_, population, pop_size, internal_mode);
    timing.physical_conversion_seconds += seconds_since(phase_start);
    phase_start = Clock::now();
    std::vector<double> merits = evaluate_population(
        physical_population,
        pop_size,
        options.use_cuda,
        &used_cuda,
        &timing);
    timing.initial_evaluation_seconds += seconds_since(phase_start);

    phase_start = Clock::now();
    Score best_score = evaluate_merit(
        prepare_for_merit(apply_mixed_variables(base_, variables_, best_values, glass_search_), merit_config_),
        merit_config_);
    merits[0] = best_score.merit;

    auto best_it = std::min_element(merits.begin(), merits.end());
    int best_idx = static_cast<int>(std::distance(merits.begin(), best_it));
    if (best_idx != 0) {
        std::vector<double> candidate_scaled = population_row(population, best_idx, dim);
        std::vector<double> candidate_values =
            scaled_to_physical(variables_, candidate_scaled, internal_mode);
        Score candidate_score = evaluate_merit(
            prepare_for_merit(apply_mixed_variables(base_, variables_, candidate_values, glass_search_), merit_config_),
            merit_config_);
        merits[static_cast<std::size_t>(best_idx)] = candidate_score.merit;
        if (candidate_score.merit < best_score.merit) {
            best_scaled = std::move(candidate_scaled);
            best_values = std::move(candidate_values);
            best_score = std::move(candidate_score);
        }
    }
    timing.best_rescore_seconds += seconds_since(phase_start);

    if (progress) {
        progress(0, best_score, best_values);
    }

    for (int gen = 1; gen <= options.max_generations; ++gen) {
        phase_start = Clock::now();
        std::vector<double> trials(static_cast<std::size_t>(pop_size) * dim);
        const double f = f_dist(rng);

        for (int i = 0; i < pop_size; ++i) {
            int a = 0;
            int b = 0;
            int c = 0;
            do {
                a = static_cast<int>(uni(rng) * pop_size);
            } while (a == i);
            do {
                b = static_cast<int>(uni(rng) * pop_size);
            } while (b == i || b == a);
            do {
                c = static_cast<int>(uni(rng) * pop_size);
            } while (c == i || c == a || c == b);

            const int j_rand = static_cast<int>(uni(rng) * dim);

            for (int j = 0; j < dim; ++j) {
                const auto& var = variables_[static_cast<std::size_t>(j)];
                const auto [lo, hi] = internal_bounds(var, internal_mode);
                const double target = population[static_cast<std::size_t>(i) * dim + j];
                double value = target;
                if (is_glass_choice_variable(var)) {
                    if (uni(rng) < options.recombination || j == j_rand) {
                        value = mutate_glass_choice_internal(
                            var,
                            target,
                            best_scaled[static_cast<std::size_t>(j)],
                            options,
                            rng,
                            uni,
                            internal_mode);
                    } else {
                        value = physical_to_internal(
                            var,
                            internal_to_physical(var, target, internal_mode),
                            internal_mode);
                    }
                    trials[static_cast<std::size_t>(i) * dim + j] =
                        std::clamp(value, lo, hi);
                    continue;
                }
                if (uni(rng) < options.recombination || j == j_rand) {
                    const double va = population[static_cast<std::size_t>(a) * dim + j];
                    const double vb = population[static_cast<std::size_t>(b) * dim + j];
                    const double vc = population[static_cast<std::size_t>(c) * dim + j];
                    const double best = best_scaled[static_cast<std::size_t>(j)];
                    if (strategy == "currenttobest1bin") {
                        value = target + f * (best - target) + f * (vb - vc);
                    } else if (strategy == "rand1bin") {
                        value = va + f * (vb - vc);
                    } else {
                        value = best + f * (vb - vc);
                    }
                }
                if (!std::isfinite(value)) {
                    value = target;
                } else if (continuous_value_requires_repair(var, value, lo, hi)) {
                    value = lo + uni(rng) * (hi - lo);
                }
                trials[static_cast<std::size_t>(i) * dim + j] = value;
            }
        }
        timing.trial_generation_seconds += seconds_since(phase_start);

        bool gen_used_cuda = false;
        phase_start = Clock::now();
        std::vector<double> physical_trials =
            scaled_population_to_physical_flat(variables_, trials, pop_size, internal_mode);
        timing.physical_conversion_seconds += seconds_since(phase_start);
        phase_start = Clock::now();
        std::vector<double> trial_merits = evaluate_population(
            physical_trials,
            pop_size,
            options.use_cuda,
            &gen_used_cuda,
            &timing);
        used_cuda = used_cuda || gen_used_cuda;

        phase_start = Clock::now();
        for (int i = 0; i < pop_size; ++i) {
            if (trial_merits[i] <= merits[i]) {
                merits[i] = trial_merits[i];
                for (int j = 0; j < dim; ++j) {
                    population[static_cast<std::size_t>(i) * dim + j] =
                        trials[static_cast<std::size_t>(i) * dim + j];
                }
            }
        }

        best_it = std::min_element(merits.begin(), merits.end());
        const int current_best_idx = static_cast<int>(std::distance(merits.begin(), best_it));
        timing.selection_seconds += seconds_since(phase_start);
        if (*best_it < best_score.merit) {
            phase_start = Clock::now();
            std::vector<double> candidate_scaled(dim);
            for (int j = 0; j < dim; ++j) {
                candidate_scaled[static_cast<std::size_t>(j)] =
                    population[static_cast<std::size_t>(current_best_idx) * dim + j];
            }
            std::vector<double> candidate_values =
                scaled_to_physical(variables_, candidate_scaled, internal_mode);
            Score candidate_score = evaluate_merit(
                prepare_for_merit(apply_mixed_variables(base_, variables_, candidate_values, glass_search_), merit_config_),
                merit_config_);
            merits[static_cast<std::size_t>(current_best_idx)] = candidate_score.merit;
            if (candidate_score.merit < best_score.merit) {
                best_scaled = std::move(candidate_scaled);
                best_values = std::move(candidate_values);
                best_score = std::move(candidate_score);
            }
            timing.best_rescore_seconds += seconds_since(phase_start);
        }

        if (progress && options.progress_every > 0 && gen % options.progress_every == 0) {
            progress(gen, best_score, best_values);
        }
        if (population_stddev(merits) <=
            options.absolute_tolerance + options.tolerance * population_mean_abs(merits)) {
            if (progress && options.progress_every > 0 && gen % options.progress_every != 0) {
                progress(gen, best_score, best_values);
            }
            OptimizationResult result;
            result.best_values = best_values;
            result.best_score = best_score;
            result.generations = gen;
            result.used_cuda = used_cuda;
            result.de_seconds = seconds_since(run_start);
            phase_start = Clock::now();
            attach_final_population(result, variables_, population, pop_size, merits, internal_mode);
            timing.attach_population_seconds += seconds_since(phase_start);
            result.timing = timing;
            return result;
        }
    }

    OptimizationResult result;
    result.best_values = best_values;
    result.best_score = best_score;
    result.generations = options.max_generations;
    result.used_cuda = used_cuda;
    result.de_seconds = seconds_since(run_start);
    phase_start = Clock::now();
    attach_final_population(result, variables_, population, pop_size, merits, internal_mode);
    timing.attach_population_seconds += seconds_since(phase_start);
    result.timing = timing;
    return result;
}

}  // namespace optiland
