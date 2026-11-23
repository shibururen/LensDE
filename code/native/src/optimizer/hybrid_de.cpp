/*
 * Advance the HybridSession population through its configured inner DE generations.
 * Each outer epoch supplies its reference merit; the session owns population, incumbents and RNG.
 * Preserve donor/crossover draws, row evaluation order and replacement decisions within each generation.
 */
#include "hybrid_session.hpp"

namespace optiland { namespace optimizer_detail {

void HybridSession::run_de(int outer, double epoch_start_merit) {
const auto de_start = Clock::now();
        bool epoch_used_cuda = false;
        for (int gen = 1; gen <= inner_generations; ++gen) {
            auto [current_best_candidate_scaled, current_best_candidate_score] = update_best_from_population();
            if (current_best_candidate_score.merit < best_score.merit) {
                best_scaled = std::move(current_best_candidate_scaled);
                best_score = std::move(current_best_candidate_score);
            }

            phase_start = Clock::now();
            std::vector<double> trials(static_cast<std::size_t>(pop_size) * dim);
            const double f = f_dist(rng);
            const std::vector<double> current_best_scaled = best_scaled;

            for (int i = 0; i < pop_size; ++i) {
                int a = 0;
                int b = 0;
                int c = 0;
                do {
                    a = std::min(pop_size - 1, static_cast<int>(uni(rng) * pop_size));
                } while (a == i);
                do {
                    b = std::min(pop_size - 1, static_cast<int>(uni(rng) * pop_size));
                } while (b == i || b == a);
                do {
                    c = std::min(pop_size - 1, static_cast<int>(uni(rng) * pop_size));
                } while (c == i || c == a || c == b);

                const int j_rand = std::min(dim - 1, static_cast<int>(uni(rng) * dim));
                for (int j = 0; j < dim; ++j) {
                    const auto& var = variables_[static_cast<std::size_t>(j)];
                    const auto [lo, hi] = internal_bounds(var, internal_mode);
                    const double target = population[static_cast<std::size_t>(i) * dim + j];
                    double value = target;
                    if (is_glass_choice_variable(var)) {
                        if (uni(rng) < de_options.recombination || j == j_rand) {
                            value = mutate_glass_choice_internal(
                                var,
                                target,
                                current_best_scaled[static_cast<std::size_t>(j)],
                                de_options,
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
                    if (uni(rng) < de_options.recombination || j == j_rand) {
                        const double va = population[static_cast<std::size_t>(a) * dim + j];
                        const double vb = population[static_cast<std::size_t>(b) * dim + j];
                        const double vc = population[static_cast<std::size_t>(c) * dim + j];
                        const double best = current_best_scaled[static_cast<std::size_t>(j)];
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

            phase_start = Clock::now();
            physical_population =
                scaled_population_to_physical_flat(variables_, trials, pop_size, internal_mode);
            timing.physical_conversion_seconds += seconds_since(phase_start);
            phase_start = Clock::now();
            std::vector<double> trial_merits = evaluate_population(
                physical_population,
                pop_size,
                de_options.use_cuda,
                &eval_used_cuda,
                &timing);
            used_cuda = used_cuda || eval_used_cuda;
            epoch_used_cuda = epoch_used_cuda || eval_used_cuda;

            phase_start = Clock::now();
            for (int i = 0; i < pop_size; ++i) {
                if (trial_merits[static_cast<std::size_t>(i)] <= merits[static_cast<std::size_t>(i)]) {
                    merits[static_cast<std::size_t>(i)] = trial_merits[static_cast<std::size_t>(i)];
                    for (int j = 0; j < dim; ++j) {
                        population[static_cast<std::size_t>(i) * dim + j] =
                            trials[static_cast<std::size_t>(i) * dim + j];
                    }
                }
            }
            timing.selection_seconds += seconds_since(phase_start);
            ++total_de_generations;
        }

        auto [epoch_best_scaled, epoch_best_score] = update_best_from_population();
        if (epoch_best_score.merit < best_score.merit) {
            best_scaled = std::move(epoch_best_scaled);
            best_score = std::move(epoch_best_score);
        }
        const double epoch_de_seconds = seconds_since(de_start);
        de_seconds += epoch_de_seconds;
        emit_hybrid_trace(
            "de_end",
            outer,
            epoch_de_seconds,
            epoch_start_merit,
            epoch_start_merit,
            best_score,
            best_scaled,
            inner_generations,
            0,
            0,
            0,
            epoch_used_cuda);
}

} }  // namespace optiland::optimizer_detail
