/*
 * Choose adaptive polish candidates and dispatch standard or micro-island polish batches.
 * The epoch controller shares population/basin ordering and receives the resulting polish statistics.
 * Keep candidate quotas, physical-row mapping and batch-result order aligned with the selected sources.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<AdaptivePolishCandidate> AdaptiveSession::select_polish_candidates(int requested_count, const std::vector<int>& excluded_population_indices, const std::vector<std::vector<double>>& excluded_scaled_rows) {
            std::vector<AdaptivePolishCandidate> selected;
            const int limit = std::min(
                std::max(0, requested_count),
                std::max(1, run_.pop_size + static_cast<int>(run_.basins.size())));
            if (limit <= 0) {
                return selected;
            }
            auto topology_group_key = [](const std::string& topology_key) {
                return topology_key.empty() ? std::string("__unknown__") : topology_key;
            };
            auto population_index_excluded = [&](int population_index) {
                if (population_index < 0) {
                    return false;
                }
                return std::find(
                           excluded_population_indices.begin(),
                           excluded_population_indices.end(),
                           population_index) != excluded_population_indices.end();
            };
            auto scaled_row_excluded = [&](const std::vector<double>& scaled) {
                return std::any_of(
                    excluded_scaled_rows.begin(),
                    excluded_scaled_rows.end(),
                    [&](const std::vector<double>& excluded_scaled) {
                        return this->scaled_rows_equivalent(scaled, excluded_scaled);
                    });
            };
            auto candidate_excluded = [&](const AdaptivePolishCandidate& candidate) {
                return population_index_excluded(candidate.population_index) ||
                       scaled_row_excluded(candidate.scaled) ||
                       this->active_plateau_fitness_matches_retired(
                           candidate.scaled,
                           candidate.fitness);
            };
            auto selected_contains_population_index = [&](int population_index) {
                if (population_index < 0) {
                    return false;
                }
                if (population_index_excluded(population_index)) {
                    return true;
                }
                return std::any_of(
                    selected.begin(),
                    selected.end(),
                    [population_index](const AdaptivePolishCandidate& candidate) {
                        return candidate.population_index == population_index;
                    });
            };
            auto selected_topology_count = [&](const std::string& topology_key) {
                const std::string key = topology_group_key(topology_key);
                return static_cast<int>(std::count_if(
                    selected.begin(),
                    selected.end(),
                    [&key, &topology_group_key](const AdaptivePolishCandidate& candidate) {
                        return topology_group_key(candidate.topology_key) == key;
                    }));
            };
            auto far_enough = [&](const std::vector<double>& scaled, const std::string& topology_key) {
                const double min_distance = std::max(0.0, options.basins.radius * 0.5);
                for (const auto& existing : selected) {
                    if ((options.basins.semantic_diversity || options.polish_topology_balanced) &&
                        !topology_key.empty() &&
                        !existing.topology_key.empty() &&
                        topology_key != existing.topology_key) {
                        continue;
                    }
                    if (scaled_vector_distance(
                            owner.variables_,
                            scaled,
                            existing.scaled,
                            options.basins.distance_metric,
                            run_.internal_mode,
                            &owner.base_,
                            options.basins.air_thickness_distance_cap) < min_distance) {
                        return false;
                    }
                }
                return true;
            };
            auto try_add_candidate = [&](AdaptivePolishCandidate candidate,
                                         bool require_distance,
                                         int max_per_topology) {
                if (static_cast<int>(selected.size()) >= limit) {
                    return false;
                }
                if (candidate_excluded(candidate)) {
                    return false;
                }
                if (selected_contains_population_index(candidate.population_index)) {
                    return false;
                }
                if (max_per_topology > 0 &&
                    selected_topology_count(candidate.topology_key) >= max_per_topology) {
                    return false;
                }
                if (require_distance && !far_enough(candidate.scaled, candidate.topology_key)) {
                    return false;
                }
                selected.push_back(std::move(candidate));
                return true;
            };
            this->sort_basins();
            if (options.polish_topology_balanced) {
                std::vector<AdaptivePolishCandidate> pool;
                pool.reserve(static_cast<std::size_t>(run_.pop_size) + run_.basins.size());
                if (options.basins.polish_basins) {
                    for (const auto& basin : run_.basins) {
                        if (this->active_plateau_basin_matches_retired(basin)) {
                            continue;
                        }
                        pool.push_back({
                            this->matching_active_basin_slot(basin.scaled_values),
                            basin.scaled_values,
                            basin.fitness,
                            "basin",
                            this->basin_key(basin),
                            basin.descriptor_key.empty()
                                ? basin.fitness.descriptor_key
                                : basin.descriptor_key,
                            basin.descriptor_axis_values.empty()
                                ? basin.fitness.descriptor_axis_values
                                : basin.descriptor_axis_values});
                    }
                }
                const auto order = sorted_fitness_indices(run_.fitness, options);
                for (int idx : order) {
                    pool.push_back({idx, population_row(run_.population, idx, run_.dim),
                                    run_.fitness[static_cast<std::size_t>(idx)],
                                    "population",
                                    run_.fitness[static_cast<std::size_t>(idx)].topology_key,
                                    run_.fitness[static_cast<std::size_t>(idx)].descriptor_key,
                                    run_.fitness[static_cast<std::size_t>(idx)].descriptor_axis_values});
                }

                if (run_.descriptor_select_enabled) {
                    const auto selector_start = Clock::now();
                    ++run_.timing.topology_descriptor_selector_calls;
                    auto selected_descriptor_axis_count =
                        [&](std::size_t axis, std::uint64_t value) {
                            int count = 0;
                            for (const auto& existing : selected) {
                                if (axis < existing.descriptor_axis_values.size() &&
                                    existing.descriptor_axis_values[axis] == value) {
                                    ++count;
                                }
                            }
                            return count;
                        };
                    auto descriptor_coverage_gain = [&](const AdaptivePolishCandidate& candidate) {
                        if (candidate.descriptor_axis_values.empty()) {
                            return 0.0;
                        }
                        double gain = 0.0;
                        for (std::size_t axis = 0; axis < candidate.descriptor_axis_values.size(); ++axis) {
                            const int count =
                                selected_descriptor_axis_count(axis, candidate.descriptor_axis_values[axis]);
                            if (count == 0) {
                                gain += 1.0;
                            } else {
                                gain += 1.0 / std::sqrt(static_cast<double>(count + 1));
                            }
                        }
                        return gain /
                               static_cast<double>(std::max<std::size_t>(
                                   1, candidate.descriptor_axis_values.size()));
                    };
                    auto descriptor_distance = [&](const AdaptivePolishCandidate& candidate) {
                        if (selected.empty() || candidate.descriptor_axis_values.empty()) {
                            return 0.0;
                        }
                        double nearest = std::numeric_limits<double>::infinity();
                        for (const auto& existing : selected) {
                            const std::size_t n = std::max(
                                candidate.descriptor_axis_values.size(),
                                existing.descriptor_axis_values.size());
                            if (n == 0) {
                                nearest = std::min(nearest, 0.0);
                                continue;
                            }
                            int mismatch = 0;
                            for (std::size_t axis = 0; axis < n; ++axis) {
                                const std::uint64_t a =
                                    axis < candidate.descriptor_axis_values.size()
                                        ? candidate.descriptor_axis_values[axis]
                                        : 0ULL;
                                const std::uint64_t b =
                                    axis < existing.descriptor_axis_values.size()
                                        ? existing.descriptor_axis_values[axis]
                                        : 0ULL;
                                if (a != b) {
                                    ++mismatch;
                                }
                            }
                            nearest = std::min(
                                nearest,
                                static_cast<double>(mismatch) /
                                    static_cast<double>(std::max<std::size_t>(1, n)));
                        }
                        return nearest;
                    };
                    auto scaled_distance = [&](const AdaptivePolishCandidate& candidate) {
                        if (selected.empty()) {
                            return 0.0;
                        }
                        double nearest = std::numeric_limits<double>::infinity();
                        for (const auto& existing : selected) {
                            nearest = std::min(
                                nearest,
                                scaled_vector_distance(
                                    owner.variables_,
                                    candidate.scaled,
                                    existing.scaled,
                                    options.basins.distance_metric,
                                    run_.internal_mode,
                                    &owner.base_,
                                    options.basins.air_thickness_distance_cap));
                        }
                        return nearest;
                    };
                    while (static_cast<int>(selected.size()) < limit) {
                        int best_pos = -1;
                        double best_score = -std::numeric_limits<double>::infinity();
                        double best_gain = -std::numeric_limits<double>::infinity();
                        double best_descriptor_distance = -std::numeric_limits<double>::infinity();
                        double best_scaled_distance = -std::numeric_limits<double>::infinity();
                        for (int pos = 0; pos < static_cast<int>(pool.size()); ++pos) {
                            const AdaptivePolishCandidate& candidate =
                                pool[static_cast<std::size_t>(pos)];
                            if (candidate_excluded(candidate)) {
                                continue;
                            }
                            if (selected_contains_population_index(candidate.population_index)) {
                                continue;
                            }
                            const double gain = descriptor_coverage_gain(candidate);
                            const double ddesc = descriptor_distance(candidate);
                            const double dscaled = scaled_distance(candidate);
                            const double score = 4.0 * gain + 1.5 * ddesc + 0.25 * dscaled;
                            bool better = score > best_score + 1.0e-12;
                            if (!better && std::abs(score - best_score) <= 1.0e-12) {
                                better = gain > best_gain + 1.0e-12;
                            }
                            if (!better &&
                                std::abs(score - best_score) <= 1.0e-12 &&
                                std::abs(gain - best_gain) <= 1.0e-12) {
                                better = ddesc > best_descriptor_distance + 1.0e-12;
                            }
                            if (!better &&
                                std::abs(score - best_score) <= 1.0e-12 &&
                                std::abs(gain - best_gain) <= 1.0e-12 &&
                                std::abs(ddesc - best_descriptor_distance) <= 1.0e-12) {
                                better = dscaled > best_scaled_distance + 1.0e-12;
                            }
                            if (!better &&
                                std::abs(score - best_score) <= 1.0e-12 &&
                                std::abs(gain - best_gain) <= 1.0e-12 &&
                                std::abs(ddesc - best_descriptor_distance) <= 1.0e-12 &&
                                std::abs(dscaled - best_scaled_distance) <= 1.0e-12) {
                                better =
                                    best_pos < 0 ||
                                    candidate_better(
                                        candidate.fitness,
                                        pool[static_cast<std::size_t>(best_pos)].fitness,
                                        options);
                            }
                            if (better) {
                                best_pos = pos;
                                best_score = score;
                                best_gain = gain;
                                best_descriptor_distance = ddesc;
                                best_scaled_distance = dscaled;
                            }
                        }
                        if (best_pos < 0) {
                            break;
                        }
                        selected.push_back(pool[static_cast<std::size_t>(best_pos)]);
                    }
                    run_.timing.topology_descriptor_selector_seconds += seconds_since(selector_start);
                    return selected;
                }

                for (const auto& candidate : pool) {
                    try_add_candidate(candidate, true, 1);
                    if (static_cast<int>(selected.size()) >= limit) {
                        return selected;
                    }
                }
                const int configured_topology_limit =
                    std::max(1, options.basins.semantic_max_per_topology);
                if (configured_topology_limit > 1) {
                    for (const auto& candidate : pool) {
                        try_add_candidate(candidate, true, configured_topology_limit);
                        if (static_cast<int>(selected.size()) >= limit) {
                            return selected;
                        }
                    }
                }
                for (const auto& candidate : pool) {
                    try_add_candidate(candidate, true, 0);
                    if (static_cast<int>(selected.size()) >= limit) {
                        return selected;
                    }
                }
                for (const auto& candidate : pool) {
                    try_add_candidate(candidate, false, 0);
                    if (static_cast<int>(selected.size()) >= limit) {
                        return selected;
                    }
                }
                return selected;
            }
            if (options.basins.polish_basins) {
                for (const auto& basin : run_.basins) {
                    if (static_cast<int>(selected.size()) >= limit) {
                        break;
                    }
                    AdaptivePolishCandidate candidate{
                        this->matching_active_basin_slot(basin.scaled_values),
                        basin.scaled_values,
                        basin.fitness,
                        "basin",
                        this->basin_key(basin),
                        basin.descriptor_key.empty()
                            ? basin.fitness.descriptor_key
                            : basin.descriptor_key,
                        basin.descriptor_axis_values.empty()
                            ? basin.fitness.descriptor_axis_values
                            : basin.descriptor_axis_values};
                    if (candidate_excluded(candidate) ||
                        !far_enough(candidate.scaled, candidate.topology_key)) {
                        continue;
                    }
                    selected.push_back(std::move(candidate));
                }
            }
            const auto order = sorted_fitness_indices(run_.fitness, options);
            for (int idx : order) {
                if (static_cast<int>(selected.size()) >= limit) {
                    break;
                }
                std::vector<double> scaled = population_row(run_.population, idx, run_.dim);
                const std::string& topology_key =
                    run_.fitness[static_cast<std::size_t>(idx)].topology_key;
                if (!far_enough(scaled, topology_key)) {
                    continue;
                }
                AdaptivePolishCandidate candidate{
                    idx,
                    std::move(scaled),
                    run_.fitness[static_cast<std::size_t>(idx)],
                    "population",
                    topology_key,
                    run_.fitness[static_cast<std::size_t>(idx)].descriptor_key,
                    run_.fitness[static_cast<std::size_t>(idx)].descriptor_axis_values};
                if (!candidate_excluded(candidate)) {
                    selected.push_back(std::move(candidate));
                }
            }
            for (int idx : order) {
                if (static_cast<int>(selected.size()) >= limit) {
                    break;
                }
                if (selected_contains_population_index(idx)) {
                    continue;
                }
                AdaptivePolishCandidate candidate{
                    idx,
                    population_row(run_.population, idx, run_.dim),
                    run_.fitness[static_cast<std::size_t>(idx)],
                    "population",
                    run_.fitness[static_cast<std::size_t>(idx)].topology_key,
                    run_.fitness[static_cast<std::size_t>(idx)].descriptor_key,
                    run_.fitness[static_cast<std::size_t>(idx)].descriptor_axis_values};
                if (!candidate_excluded(candidate)) {
                    selected.push_back(std::move(candidate));
                }
            }
            return selected;
        }

std::vector<AdaptivePolishResult> AdaptiveSession::run_polish_batch(const std::vector<AdaptivePolishCandidate>& polish_candidates) {
                std::vector<AdaptivePolishResult> polish_results(polish_candidates.size());
                auto run_adaptive_polish_candidate = [&](std::size_t pos) {
                    AdaptivePolishResult result;
                    result.candidate_index = pos;
                    try {
                        const AdaptivePolishCandidate& candidate = polish_candidates[pos];
                        const std::vector<double> before_values =
                            scaled_to_physical(owner.variables_, candidate.scaled, run_.internal_mode);
                        result.polished = owner.polish_damped_least_squares(before_values, run_.polish_options);
                        result.polished_scaled =
                            physical_to_scaled(owner.variables_, result.polished.best_values, run_.internal_mode);
                        result.fitness = this->score_fitness_for_scaled(
                            result.polished_scaled,
                            run_.total_de_generations,
                            candidate.population_index,
                            "polish",
                            true);
                        result.completed = true;
                    } catch (...) {
                        result.error = std::current_exception();
                    }
                    polish_results[pos] = std::move(result);
                };
                if (options.batched_polish) {
                    std::vector<MicroDlsInput> micro_inputs;
                    micro_inputs.reserve(polish_candidates.size());
                    for (std::size_t pos = 0; pos < polish_candidates.size(); ++pos) {
                        const AdaptivePolishCandidate& candidate = polish_candidates[pos];
                        micro_inputs.push_back({
                            static_cast<int>(pos),
                            candidate.scaled,
                            candidate.fitness.score.merit});
                    }
                    auto evaluate_micro_scores = [&](const std::vector<double>& rows,
                                                     int row_count,
                                                     bool* micro_used_cuda) {
                        return owner.evaluate_population_scores_scaled(
                            rows,
                            row_count,
                            run_.internal_mode,
                            run_.polish_options.use_cuda,
                            micro_used_cuda,
                            &run_.timing);
                    };
                    const std::vector<PolishWorkResult> micro_results =
                        run_batched_micro_dls_scaled(
                            owner.base_,
                            owner.variables_,
                            owner.merit_config_,
                            micro_inputs,
                            run_.polish_options,
                            run_.internal_mode,
                            options.rollback,
                            evaluate_micro_scores);
                    for (const PolishWorkResult& work : micro_results) {
                        if (work.index < 0 || work.index >= static_cast<int>(polish_candidates.size())) {
                            continue;
                        }
                        const std::size_t pos = static_cast<std::size_t>(work.index);
                        const AdaptivePolishCandidate& candidate = polish_candidates[pos];
                        AdaptivePolishResult result;
                        result.candidate_index = pos;
                        result.polished_scaled = work.polished_scaled;
                        result.polished.best_values =
                            scaled_to_physical(owner.variables_, work.polished_scaled, run_.internal_mode);
                        result.polished.best_score = work.score;
                        result.polished.used_cuda = work.used_cuda;
                        result.polished.polish_runs = 1;
                        result.fitness = make_candidate_fitness(work.score, owner.merit_config_, options);
                        result.fitness.generation = run_.total_de_generations;
                        result.fitness.population_index = candidate.population_index;
                        result.fitness.source = "polish";
                        result.fitness.polished = true;
                        this->assign_topology_keys_for_physical(result.fitness, result.polished.best_values);
                        result.completed = true;
                        polish_results[pos] = std::move(result);
                    }
                    return polish_results;
                }

                const int hardware_workers = available_worker_count();
                const bool polish_batch_cuda =
                    run_.polish_options.use_cuda && polish_uses_batch_merit(run_.polish_options);
                const int requested_workers =
                    options.polish_workers > 0
                        ? std::min(options.polish_workers, hardware_workers)
                        : hardware_workers;
                const int polish_worker_count =
                    (options.parallel_polish && !polish_batch_cuda)
                        ? std::max(1, std::min<int>(
                                          static_cast<int>(polish_candidates.size()),
                                          std::min(requested_workers, 64)))
                        : 1;
                if (polish_worker_count <= 1 || polish_candidates.size() < 2) {
                    for (std::size_t pos = 0; pos < polish_candidates.size(); ++pos) {
                        run_adaptive_polish_candidate(pos);
                    }
                } else {
#ifdef _WIN32
                    volatile LONG next_polish = 0;
                    std::vector<FunctionThreadContext> contexts(static_cast<std::size_t>(polish_worker_count));
                    std::vector<HANDLE> handles;
                    handles.reserve(static_cast<std::size_t>(polish_worker_count));
                    for (int worker = 0; worker < polish_worker_count; ++worker) {
                        contexts[static_cast<std::size_t>(worker)].fn = [&]() {
                            while (true) {
                                const int pos = static_cast<int>(InterlockedIncrement(&next_polish)) - 1;
                                if (pos >= static_cast<int>(polish_candidates.size())) {
                                    break;
                                }
                                run_adaptive_polish_candidate(static_cast<std::size_t>(pos));
                            }
                        };
                        HANDLE handle = CreateThread(
                            nullptr,
                            0,
                            run_function_thread,
                            &contexts[static_cast<std::size_t>(worker)],
                            0,
                            nullptr);
                        if (handle) {
                            handles.push_back(handle);
                        } else {
                            contexts[static_cast<std::size_t>(worker)].fn();
                        }
                    }
                    if (!handles.empty()) {
                        WaitForMultipleObjects(
                            static_cast<DWORD>(handles.size()),
                            handles.data(),
                            TRUE,
                            INFINITE);
                        for (HANDLE handle : handles) {
                            CloseHandle(handle);
                        }
                    }
                    for (const auto& context : contexts) {
                        if (context.error) {
                            std::rethrow_exception(context.error);
                        }
                    }
#else
                    for (std::size_t pos = 0; pos < polish_candidates.size(); ++pos) {
                        run_adaptive_polish_candidate(pos);
                    }
#endif
                }
                return polish_results;
            }

MicroIslandSummary AdaptiveSession::run_micro_island_stage(int& outer) {
            MicroIslandSummary summary;
            if (!options.micro_island_enabled || run_.basins.empty()) {
                summary.requested_runs = std::max(0, options.micro_island_count);
                return summary;
            }

            const int micro_effective_variable_count = [&]() {
                int count = 0;
                for (const auto& variable : owner.variables_) {
                    if (std::isfinite(variable.lower) &&
                        std::isfinite(variable.upper) &&
                        std::abs(variable.upper - variable.lower) > 1.0e-12) {
                        ++count;
                    }
                }
                return std::max(1, count > 0 ? count : run_.dim);
            }();
            const int resolved_micro_population =
                options.micro_island_popsize > 0.0
                    ? std::max(
                          8,
                          static_cast<int>(std::ceil(
                              options.micro_island_popsize *
                              static_cast<double>(micro_effective_variable_count))))
                    : std::max(8, options.micro_island_population);

            

            

            

            

            

            std::vector<int> source_basin_indices = this->select_active_basin_indices();
            if (source_basin_indices.empty()) {
                source_basin_indices = this->active_basin_family_candidate_indices();
                std::stable_sort(
                    source_basin_indices.begin(),
                    source_basin_indices.end(),
                    [&](int a, int b) {
                        return candidate_better(
                            run_.basins[static_cast<std::size_t>(a)].fitness,
                            run_.basins[static_cast<std::size_t>(b)].fitness,
                            options);
                    });
            }
            if (source_basin_indices.empty()) {
                summary.requested_runs = std::max(0, options.micro_island_count);
                return summary;
            }

            const int requested_island_count =
                options.micro_island_count > 0
                    ? options.micro_island_count
                    : static_cast<int>(source_basin_indices.size());
            summary.requested_runs = requested_island_count;
            const int island_count = std::clamp(
                requested_island_count,
                0,
                static_cast<int>(source_basin_indices.size()));
            if (island_count <= 0) {
                return summary;
            }
            source_basin_indices.resize(static_cast<std::size_t>(island_count));
            std::vector<BasinElite> source_basins;
            source_basins.reserve(source_basin_indices.size());
            for (int basin_index : source_basin_indices) {
                if (basin_index < 0 ||
                    basin_index >= static_cast<int>(run_.basins.size())) {
                    continue;
                }
                source_basins.push_back(run_.basins[static_cast<std::size_t>(basin_index)]);
            }
            std::vector<std::string> micro_retired_keys;
            std::vector<std::vector<double>> micro_retired_scaled_rows;
            micro_retired_keys.reserve(source_basins.size());
            micro_retired_scaled_rows.reserve(source_basins.size());
            const bool micro_retire_allow_similarity =
                this->active_mating_allow_similarity();
            
            

            
    this->refine_micro_island_basins(RefineMicroIslandBasinsContext{outer, summary, micro_effective_variable_count, resolved_micro_population, source_basins, micro_retired_keys, micro_retired_scaled_rows, micro_retire_allow_similarity});

            if (!micro_retired_keys.empty()) {
                run_.last_active_plateau_new_cooldown_count +=
                    static_cast<int>(micro_retired_keys.size());
                run_.last_active_plateau_retired_family_count +=
                    static_cast<int>(micro_retired_keys.size());
                this->evict_active_plateau_retired_keys(
                    micro_retired_keys,
                    micro_retired_scaled_rows,
                    outer);
            }

            return summary;
        }

} } // namespace optiland::optimizer_detail
