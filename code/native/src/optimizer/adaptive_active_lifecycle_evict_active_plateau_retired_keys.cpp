/*
 * Evict retired plateau members and maintain the adaptive retirement and pbest-suppression records.
 * Epoch lifecycle transitions coordinate population replacement, refresh sources and retired basin history.
 * Preserve removal limits, refill source order and synchronized index updates during eviction.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::evict_active_plateau_retired_keys(const std::vector<std::string>& retired_keys, const std::vector<std::vector<double>>& retired_scaled_rows, int epoch) {
            run_.last_active_plateau_evict_count = 0;
            run_.last_active_plateau_evict_candidate_count = 0;
            run_.last_active_plateau_evict_cap_count = 0;
            run_.last_active_plateau_evict_distance_min =
                std::numeric_limits<double>::quiet_NaN();
            run_.last_active_plateau_evict_distance_median =
                std::numeric_limits<double>::quiet_NaN();
            run_.last_active_plateau_evict_distance_max =
                std::numeric_limits<double>::quiet_NaN();
            run_.last_active_plateau_refill_basin_count = 0;
            run_.last_active_plateau_refill_population_count = 0;
            run_.last_active_plateau_refill_refresh_count = 0;
            run_.last_active_plateau_refill_initial_seed_count = 0;
            run_.last_active_plateau_refill_generated_count = 0;
            run_.last_active_plateau_refill_lhs_count = 0;
            run_.last_active_plateau_refill_clone_fallback_count = 0;
            run_.last_active_plateau_refill_rejected_trace_count = 0;
            run_.last_active_plateau_refill_rejected_retired_count = 0;
            if (!options.basins.active_plateau_retire_evict || retired_keys.empty()) {
                return;
            }
            
            std::vector<ActivePlateauEvictCandidate> evict_candidates;
            evict_candidates.reserve(static_cast<std::size_t>(run_.pop_size));
            const bool allow_similarity = this->active_mating_allow_similarity();
            const bool parameter_only =
                options.basins.active_plateau_retire_evict_parameter_only;
            double parameter_match_distance =
                options.basins.active_plateau_retire_evict_parameter_distance;
            if (!std::isfinite(parameter_match_distance) ||
                parameter_match_distance <= 0.0) {
                parameter_match_distance = std::max(0.0, options.basins.radius);
            }
            auto nearest_retired_anchor_distance = [&](const std::vector<double>& row) {
                double nearest = std::numeric_limits<double>::infinity();
                for (const std::vector<double>& retired_scaled : retired_scaled_rows) {
                    if (static_cast<int>(retired_scaled.size()) != run_.dim) {
                        continue;
                    }
                    nearest = std::min(
                        nearest,
                        scaled_vector_distance(
                            owner.variables_,
                            row,
                            retired_scaled,
                            options.basins.distance_metric,
                            run_.internal_mode,
                            &owner.base_,
                            options.basins.air_thickness_distance_cap));
                }
                return nearest;
            };
            for (int i = 0; i < run_.pop_size; ++i) {
                const std::string key =
                    this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(i)]);
                const std::vector<double> row = population_row(run_.population, i, run_.dim);
                const double nearest_distance =
                    nearest_retired_anchor_distance(row);
                bool matches_retired = false;
                if (!parameter_only && !key.empty()) {
                    for (const std::string& retired_key : retired_keys) {
                        if (this->active_keys_match(key, retired_key, allow_similarity)) {
                            matches_retired = true;
                            break;
                        }
                    }
                }
                if (!matches_retired && !retired_scaled_rows.empty()) {
                    if (nearest_distance <= parameter_match_distance) {
                        matches_retired = true;
                    }
                }
                if (matches_retired) {
                    evict_candidates.push_back({i, nearest_distance});
                }
            }
            run_.last_active_plateau_evict_candidate_count =
                static_cast<int>(evict_candidates.size());
            std::stable_sort(
                evict_candidates.begin(),
                evict_candidates.end(),
                [&](const ActivePlateauEvictCandidate& a,
                    const ActivePlateauEvictCandidate& b) {
                    const bool a_finite = std::isfinite(a.nearest_anchor_distance);
                    const bool b_finite = std::isfinite(b.nearest_anchor_distance);
                    if (a_finite != b_finite) {
                        return a_finite;
                    }
                    if (a_finite &&
                        std::abs(a.nearest_anchor_distance -
                                 b.nearest_anchor_distance) > 1.0e-12) {
                        return a.nearest_anchor_distance <
                               b.nearest_anchor_distance;
                    }
                    return candidate_better(
                        run_.fitness[static_cast<std::size_t>(a.index)],
                        run_.fitness[static_cast<std::size_t>(b.index)],
                        options);
                });
            const double evict_fraction = std::clamp(
                options.basins.active_plateau_retire_evict_fraction,
                0.0,
                1.0);
            int evict_count = static_cast<int>(
                std::ceil(evict_fraction *
                          static_cast<double>(evict_candidates.size())));
            evict_count = std::max(
                evict_count,
                std::max(0, options.basins.active_plateau_retire_evict_min));
            const double max_fraction = std::clamp(
                options.basins.active_plateau_retire_evict_max_fraction,
                0.0,
                1.0);
            const int max_evict_count = static_cast<int>(
                std::ceil(max_fraction * static_cast<double>(run_.pop_size)));
            run_.last_active_plateau_evict_cap_count = max_evict_count;
            evict_count = std::clamp(
                evict_count,
                0,
                std::min(max_evict_count,
                         static_cast<int>(evict_candidates.size())));
            if (evict_count <= 0) {
                return;
            }
            {
                std::vector<double> evicted_distances;
                evicted_distances.reserve(static_cast<std::size_t>(evict_count));
                for (int row = 0; row < evict_count; ++row) {
                    const double distance =
                        evict_candidates[static_cast<std::size_t>(row)]
                            .nearest_anchor_distance;
                    if (std::isfinite(distance)) {
                        evicted_distances.push_back(distance);
                    }
                }
                if (!evicted_distances.empty()) {
                    std::sort(evicted_distances.begin(), evicted_distances.end());
                    run_.last_active_plateau_evict_distance_min =
                        evicted_distances.front();
                    const std::size_t mid = evicted_distances.size() / 2U;
                    run_.last_active_plateau_evict_distance_median =
                        (evicted_distances.size() % 2U == 0U)
                            ? 0.5 * (evicted_distances[mid - 1U] +
                                     evicted_distances[mid])
                            : evicted_distances[mid];
                    run_.last_active_plateau_evict_distance_max =
                        evicted_distances.back();
                }
            }
            std::vector<char> evict_mask(static_cast<std::size_t>(run_.pop_size), 0);
            for (int row = 0; row < evict_count; ++row) {
                const int idx =
                    evict_candidates[static_cast<std::size_t>(row)].index;
                if (idx >= 0 && idx < run_.pop_size) {
                    evict_mask[static_cast<std::size_t>(idx)] = 1;
                }
            }

            
            std::vector<ActivePlateauReplacement> replacements;
            replacements.reserve(static_cast<std::size_t>(evict_count));
            std::vector<ActivePlateauReplacement> refill_seed_pool;
            std::vector<std::vector<double>> selected_replacement_rows;
            selected_replacement_rows.reserve(static_cast<std::size_t>(evict_count));
            const double replacement_min_distance =
                std::max(1.0e-12,
                         std::max(0.0, options.refresh_generation.trace_archive_min_distance));
            
            

            std::vector<int> basin_order(run_.basins.size());
            std::iota(basin_order.begin(), basin_order.end(), 0);
            std::stable_sort(
                basin_order.begin(),
                basin_order.end(),
                [&](int a, int b) {
                    return candidate_better(
                        run_.basins[static_cast<std::size_t>(a)].fitness,
                        run_.basins[static_cast<std::size_t>(b)].fitness,
                        options);
                });
            for (int basin_index : basin_order) {
                if (static_cast<int>(replacements.size()) >= evict_count) {
                    break;
                }
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(basin_index)];
                if (!basin.fitness.trace_feasible ||
                    this->global_refill_blocked_basin(basin) ||
                    this->active_plateau_basin_matches_retired(basin)) {
                    continue;
                }
                CandidateFitness candidate = basin.fitness;
                if (this->add_replacement(evict_count, replacements, selected_replacement_rows, replacement_min_distance, 
                        basin.scaled_values,
                        std::move(candidate),
                        "active_plateau_retire_refill_basin",
                        true,
                        true,
                        &run_.last_active_plateau_refill_basin_count)) {
                    refill_seed_pool.push_back(replacements.back());
                }
            }

            const std::vector<int> population_order =
                sorted_fitness_indices(run_.fitness, options);
            for (int idx : population_order) {
                if (idx < 0 || idx >= run_.pop_size ||
                    evict_mask[static_cast<std::size_t>(idx)] != 0) {
                    continue;
                }
                const std::vector<double> scaled = population_row(run_.population, idx, run_.dim);
                const CandidateFitness& current =
                    run_.fitness[static_cast<std::size_t>(idx)];
                if (!current.trace_feasible ||
                    this->global_refill_blocked_fitness(current) ||
                    this->active_plateau_fitness_matches_retired(scaled, current)) {
                    continue;
                }
                if (static_cast<int>(replacements.size()) < evict_count) {
                    CandidateFitness candidate = current;
                    if (this->add_replacement(evict_count, replacements, selected_replacement_rows, replacement_min_distance, 
                            scaled,
                            std::move(candidate),
                            "active_plateau_retire_refill_population",
                            true,
                            true,
                            &run_.last_active_plateau_refill_population_count)) {
                        refill_seed_pool.push_back(replacements.back());
                    }
                }
            }

            auto add_traceable_refill_entries =
                [&](const std::vector<TraceableRefreshArchiveEntry>& pool,
                    const std::string& accepted_source,
                    bool skip_initial_archive,
                    int* accepted_counter) {
                    if (static_cast<int>(replacements.size()) >= evict_count ||
                        pool.empty()) {
                        return;
                    }
                    std::vector<int> order(pool.size());
                    std::iota(order.begin(), order.end(), 0);
                    std::stable_sort(
                        order.begin(),
                        order.end(),
                        [&](int a, int b) {
                            return candidate_better(
                                pool[static_cast<std::size_t>(a)].fitness,
                                pool[static_cast<std::size_t>(b)].fitness,
                                options);
                        });
                    for (int index : order) {
                        if (static_cast<int>(replacements.size()) >= evict_count) {
                            break;
                        }
                        const auto& entry = pool[static_cast<std::size_t>(index)];
                        if (entry.scaled_values.size() != static_cast<std::size_t>(run_.dim) ||
                            !entry.fitness.trace_feasible) {
                            continue;
                        }
                        if (skip_initial_archive &&
                            (entry.fitness.source == "initial_archive" ||
                             entry.fitness.source == "initial_traceable_seed")) {
                            continue;
                        }
                        CandidateFitness candidate = entry.fitness;
                        if (this->add_replacement(evict_count, replacements, selected_replacement_rows, replacement_min_distance, 
                                entry.scaled_values,
                                std::move(candidate),
                                accepted_source,
                                true,
                                true,
                                accepted_counter)) {
                            refill_seed_pool.push_back(replacements.back());
                        }
                    }
                };

            add_traceable_refill_entries(
                run_.traceable_refresh_archive,
                "active_plateau_retire_refill_refresh",
                true,
                &run_.last_active_plateau_refill_refresh_count);
            add_traceable_refill_entries(
                run_.initial_traceable_seed_pool,
                "active_plateau_retire_refill_initial_seed",
                false,
                &run_.last_active_plateau_refill_initial_seed_count);

            

            auto generate_lhs_refill_rows =
                [&](int target,
                    bool require_trace,
                    const std::string& candidate_source,
                    const std::string& accepted_source) {
                    if (target <= 0) {
                        return;
                    }
                    PopulationGenerationOptions refill_options =
                        options.refresh_generation;
                    refill_options.trace_feasible_gate = require_trace;
                    const auto refill_start = Clock::now();
                    const int max_attempts =
                        require_trace
                            ? std::max(
                                  target,
                                  population_generation_candidate_limit(
                                      refill_options,
                                      target))
                            : std::max(target, target * 64);
                    int attempts = 0;
                    while (static_cast<int>(replacements.size()) < evict_count &&
                           attempts < max_attempts &&
                           !population_generation_time_exhausted(
                               refill_start,
                               refill_options)) {
                        const int remaining =
                            evict_count - static_cast<int>(replacements.size());
                        const int batch = std::min(
                            max_attempts - attempts,
                            std::max(remaining, std::min(64, std::max(8, target))));
                        std::vector<double> rows =
                            make_latin_hypercube_population(
                                owner.variables_,
                                batch,
                                run_.rng,
                                run_.internal_mode);
                        attempts += batch;
                        std::vector<CandidateFitness> generated_fitness =
                            this->evaluate_population_fitness(
                                rows,
                                batch,
                                epoch,
                                candidate_source);
                        for (int row = 0;
                             row < batch &&
                             static_cast<int>(replacements.size()) < evict_count;
                             ++row) {
                            std::vector<double> scaled =
                                population_row(rows, row, run_.dim);
                            CandidateFitness candidate =
                                std::move(generated_fitness[static_cast<std::size_t>(row)]);
                            if (this->add_replacement(evict_count, replacements, selected_replacement_rows, replacement_min_distance, 
                                    scaled,
                                    std::move(candidate),
                                    accepted_source,
                                    require_trace,
                                    true,
                                    &run_.last_active_plateau_refill_lhs_count)) {
                                refill_seed_pool.push_back(replacements.back());
                            }
                        }
                    }
                };

            this->generate_seed_perturb_refill_rows(epoch, evict_count, replacements, refill_seed_pool, selected_replacement_rows, replacement_min_distance, 
                evict_count - static_cast<int>(replacements.size())
            );
            generate_lhs_refill_rows(
                evict_count - static_cast<int>(replacements.size()),
                true,
                "active_plateau_retire_refill_lhs_candidate",
                "active_plateau_retire_refill_lhs");
            generate_lhs_refill_rows(
                evict_count - static_cast<int>(replacements.size()),
                false,
                "active_plateau_retire_refill_lhs_relaxed_candidate",
                "active_plateau_retire_refill_lhs_relaxed");
            const int last_resort_attempt_limit =
                std::max(1024, std::max(1, evict_count) * 128);
            int last_resort_attempts = 0;
            while (static_cast<int>(replacements.size()) < evict_count &&
                   last_resort_attempts < last_resort_attempt_limit) {
                ++last_resort_attempts;
                std::vector<double> rows =
                    make_latin_hypercube_population(owner.variables_, 1, run_.rng, run_.internal_mode);
                std::vector<double> scaled = population_row(rows, 0, run_.dim);
                CandidateFitness candidate = this->evaluate_fitness_for_scaled(
                    scaled,
                    epoch,
                    -1,
                    "active_plateau_retire_refill_last_resort",
                    false);
                (void)this->add_replacement(evict_count, replacements, selected_replacement_rows, replacement_min_distance, 
                    scaled,
                    std::move(candidate),
                    "active_plateau_retire_refill_last_resort",
                    false,
                    true,
                    &run_.last_active_plateau_refill_lhs_count);
            }
            while (static_cast<int>(replacements.size()) < evict_count) {
                std::vector<double> rows =
                    make_latin_hypercube_population(owner.variables_, 1, run_.rng, run_.internal_mode);
                std::vector<double> scaled = population_row(rows, 0, run_.dim);
                CandidateFitness candidate = this->evaluate_fitness_for_scaled(
                    scaled,
                    epoch,
                    -1,
                    "active_plateau_retire_refill_emergency_lhs",
                    false);
                candidate.source = "active_plateau_retire_refill_emergency_lhs";
                candidate.population_index = -1;
                replacements.push_back(
                    {scaled,
                     std::move(candidate),
                     "active_plateau_retire_refill_emergency_lhs"});
                selected_replacement_rows.push_back(scaled);
                ++run_.last_active_plateau_refill_lhs_count;
            }

            for (int row = 0; row < evict_count; ++row) {
                const int idx =
                    evict_candidates[static_cast<std::size_t>(row)].index;
                ActivePlateauReplacement replacement =
                    replacements[static_cast<std::size_t>(row)];
                set_population_row(
                    run_.population,
                    idx,
                    replacement.scaled,
                    owner.variables_,
                    run_.internal_mode);
                replacement.fitness.population_index = idx;
                replacement.fitness.source = replacement.source;
                run_.fitness[static_cast<std::size_t>(idx)] =
                    std::move(replacement.fitness);
                this->consider_basin(
                    replacement.scaled,
                    run_.fitness[static_cast<std::size_t>(idx)],
                    replacement.source,
                    false);
            }
            this->sync_merits();
            this->update_best_from_population();
            run_.last_active_plateau_evict_count = evict_count;
            this->refresh_active_plateau_retire_clear_count();
            const int defer_epochs = std::max(
                0,
                options.basins.active_plateau_retire_defer_early_stop_epochs);
            if (defer_epochs > 0) {
                run_.active_plateau_retire_defer_until_epoch =
                    std::max(run_.active_plateau_retire_defer_until_epoch,
                             epoch + defer_epochs);
            }
        }

bool AdaptiveSession::force_retire_initial_active_plateau_frontier(int epoch) {
        if (!this->active_plateau_lifecycle_enabled() ||
            !options.basins.active_plateau_retire_frontier_on_first_update ||
            run_.active_plateau_initial_frontier_retired) {
            return false;
        }
        std::vector<int> indices = this->active_plateau_tracking_candidate_indices();
        if (indices.empty()) {
            return false;
        }
        std::stable_sort(
            indices.begin(),
            indices.end(),
            [&](int a, int b) {
                return candidate_better(
                    run_.basins[static_cast<std::size_t>(a)].fitness,
                    run_.basins[static_cast<std::size_t>(b)].fitness,
                    options);
            });
        std::vector<std::string> retired_keys;
        std::vector<std::vector<double>> retired_scaled_rows;
        const bool allow_similarity = this->active_mating_allow_similarity();
        auto add_retired_key = [&](const std::string& key,
                                   const std::vector<double>& scaled) {
            if (key.empty()) {
                return false;
            }
            for (const std::string& existing : retired_keys) {
                if (this->active_keys_match(key, existing, allow_similarity)) {
                    return false;
                }
            }
            const bool added_unique =
                this->add_active_plateau_retired_filter(key, scaled);
            if (!added_unique) {
                return false;
            }
            retired_keys.push_back(key);
            retired_scaled_rows.push_back(scaled);
            return true;
        };
        for (int index : indices) {
            const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
            const std::string key = this->active_lifecycle_key_for_basin(basin);
            if (!add_retired_key(key, basin.scaled_values)) {
                continue;
            }
            ActiveBasinLifecycleState& state = run_.active_basin_lifecycle[key];
            state.last_seen_epoch = epoch;
            state.best_merit = basin.fitness.score.merit;
            state.stale_epochs = 0;
            const int cooldown_epochs =
                std::max(0, options.basins.active_plateau_cooldown_epochs);
            if (cooldown_epochs > 0) {
                state.cooldown_until_epoch = epoch + cooldown_epochs;
            }
        }
        run_.active_plateau_initial_frontier_retired = true;
        run_.last_active_plateau_new_cooldown_count =
            static_cast<int>(retired_keys.size());
        run_.last_active_plateau_retired_family_count =
            static_cast<int>(retired_keys.size());
        this->evict_active_plateau_retired_keys(retired_keys, retired_scaled_rows, epoch);
        return !retired_keys.empty();
    }

std::string AdaptiveSession::refresh_archive_retired_group_key(const std::vector<double>& scaled, const CandidateFitness& fitness) {
            if (!this->active_plateau_retire_filter_enabled()) {
                return std::string{};
            }
            const std::string candidate_key = this->fitness_diversity_key(fitness);
            if (!options.basins.active_plateau_retire_evict_parameter_only &&
                !candidate_key.empty()) {
                const bool allow_similarity = this->active_mating_allow_similarity();
                for (const std::string& retired_key :
                     run_.active_plateau_retired_filter_keys) {
                    if (this->active_keys_match(candidate_key, retired_key, allow_similarity)) {
                        return std::string("key:") + retired_key;
                    }
                }
            }
            if (scaled.size() != static_cast<std::size_t>(run_.dim)) {
                return std::string{};
            }
            double nearest = std::numeric_limits<double>::infinity();
            int nearest_index = -1;
            for (int index = 0;
                 index < static_cast<int>(
                             run_.active_plateau_retired_filter_scaled_rows.size());
                 ++index) {
                const std::vector<double>& retired_scaled =
                    run_.active_plateau_retired_filter_scaled_rows[
                        static_cast<std::size_t>(index)];
                if (retired_scaled.size() != static_cast<std::size_t>(run_.dim)) {
                    continue;
                }
                const double distance = scaled_vector_distance(
                    owner.variables_,
                    scaled,
                    retired_scaled,
                    options.basins.distance_metric,
                    run_.internal_mode,
                    &owner.base_,
                    options.basins.air_thickness_distance_cap);
                if (std::isfinite(distance) && distance < nearest) {
                    nearest = distance;
                    nearest_index = index;
                }
            }
            if (nearest_index >= 0 &&
                nearest <= this->active_plateau_retire_match_distance()) {
                return std::string("anchor:") + std::to_string(nearest_index);
            }
            return std::string{};
        }

PBestGuideSelection AdaptiveSession::mark_active_plateau_pbest_suppressed(PBestGuideSelection guide, bool global_rescue) {
            guide.active_plateau_suppressed = true;
            guide.active_plateau_global_rescue = global_rescue && guide.has_guide;
            return guide;
        }

MicroRetireDecision AdaptiveSession::add_micro_retired_basin(int& outer, std::vector<std::string >& micro_retired_keys, std::vector<std::vector<double> >& micro_retired_scaled_rows, const bool& micro_retire_allow_similarity, const BasinElite& retired_basin) {
                MicroRetireDecision decision;
                if (!this->active_plateau_lifecycle_enabled() ||
                    !options.basins.active_plateau_retire_evict ||
                    retired_basin.scaled_values.size() !=
                        static_cast<std::size_t>(run_.dim)) {
                    decision.action = "retire_disabled";
                    return decision;
                }
                const std::string key =
                    this->active_lifecycle_key_for_basin(retired_basin);
                decision.key = key;
                if (key.empty()) {
                    decision.action = "empty_key";
                    return decision;
                }
                for (const std::string& existing : micro_retired_keys) {
                    if (this->active_keys_match(
                            key,
                            existing,
                            micro_retire_allow_similarity)) {
                        decision.action = "duplicate_in_micro_batch";
                        return decision;
                    }
                }
                const bool added_unique = this->add_active_plateau_retired_filter(
                    key,
                    retired_basin.scaled_values);
                if (!added_unique) {
                    decision.action = "duplicate_global_retired";
                    return decision;
                }
                micro_retired_keys.push_back(key);
                micro_retired_scaled_rows.push_back(retired_basin.scaled_values);
                ActiveBasinLifecycleState& state = run_.active_basin_lifecycle[key];
                state.last_seen_epoch = outer;
                if (std::isfinite(retired_basin.fitness.score.merit)) {
                    state.best_merit = retired_basin.fitness.score.merit;
                }
                state.stale_epochs = 0;
                const int cooldown_epochs =
                    std::max(0, options.basins.active_plateau_cooldown_epochs);
                if (cooldown_epochs > 0) {
                    state.cooldown_until_epoch =
                        std::max(state.cooldown_until_epoch,
                                 outer + cooldown_epochs);
                }
                decision.retired = true;
                decision.unique = true;
                decision.action = "retired_unique";
                return decision;
            }

} } // namespace optiland::optimizer_detail
