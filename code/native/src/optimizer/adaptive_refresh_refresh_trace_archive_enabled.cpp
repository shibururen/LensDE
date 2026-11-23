/*
 * Maintain traceable refresh archives and generate the candidate rows used for epoch refill.
 * Adaptive refresh stages and drains pending rows before applying archive/source selection policy.
 * Preserve archive key construction, capacity trimming and candidate generation order.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

bool AdaptiveSession::refresh_trace_archive_enabled() {
        return options.refresh_generation.trace_archive_enabled &&
               options.refresh_generation.trace_archive_fraction > 0.0;
    }

int AdaptiveSession::refresh_trace_archive_capacity() {
        if (options.refresh_generation.trace_archive_capacity > 0) {
            return options.refresh_generation.trace_archive_capacity;
        }
        return std::max(run_.initial_pop_size, std::max(1, run_.pop_size) * 4);
    }

std::string AdaptiveSession::refresh_archive_key(const CandidateFitness& fitness) {
        if (!fitness.descriptor_key.empty()) {
            return fitness.descriptor_key;
        }
        if (!fitness.semantic_key.empty()) {
            return fitness.semantic_key;
        }
        if (!fitness.topology_key.empty()) {
            return fitness.topology_key;
        }
        return std::string("__unknown__");
    }

void AdaptiveSession::trim_traceable_refresh_archive() {
        const int capacity = this->refresh_trace_archive_capacity();
        if (!this->refresh_trace_archive_enabled() || capacity <= 0) {
            run_.traceable_refresh_archive.clear();
            return;
        }
        if (static_cast<int>(run_.traceable_refresh_archive.size()) <= capacity &&
            !this->active_plateau_retire_filter_enabled()) {
            return;
        }
        std::vector<int> order(run_.traceable_refresh_archive.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](int a, int b) {
                return candidate_better(
                    run_.traceable_refresh_archive[static_cast<std::size_t>(a)].fitness,
                    run_.traceable_refresh_archive[static_cast<std::size_t>(b)].fitness,
                    options);
            });
        std::vector<TraceableRefreshArchiveEntry> trimmed;
        trimmed.reserve(static_cast<std::size_t>(capacity));
        std::vector<bool> selected(run_.traceable_refresh_archive.size(), false);
        std::vector<std::string> seen_keys;
        std::unordered_map<std::string, int> retired_group_counts;
        auto add_trimmed_index = [&](int index, bool enforce_unique_key) {
            if (index < 0 ||
                index >= static_cast<int>(run_.traceable_refresh_archive.size()) ||
                selected[static_cast<std::size_t>(index)] ||
                static_cast<int>(trimmed.size()) >= capacity) {
                return false;
            }
            const auto& entry =
                run_.traceable_refresh_archive[static_cast<std::size_t>(index)];
            const std::string retired_group =
                this->refresh_archive_retired_group_key(entry.scaled_values, entry.fitness);
            if (!retired_group.empty()) {
                int& group_count = retired_group_counts[retired_group];
                if (group_count >= run_.retired_refresh_archive_keep_per_family) {
                    return false;
                }
                ++group_count;
                selected[static_cast<std::size_t>(index)] = true;
                trimmed.push_back(entry);
                return true;
            }
            if (enforce_unique_key) {
                const std::string key = this->refresh_archive_key(entry.fitness);
                if (std::find(seen_keys.begin(), seen_keys.end(), key) !=
                    seen_keys.end()) {
                    return false;
                }
                seen_keys.push_back(key);
            }
            selected[static_cast<std::size_t>(index)] = true;
            trimmed.push_back(entry);
            return true;
        };
        for (int index : order) {
            if (!add_trimmed_index(index, true) &&
                static_cast<int>(trimmed.size()) >= capacity) {
                continue;
            }
        }
        for (int index : order) {
            if (!add_trimmed_index(index, false) &&
                static_cast<int>(trimmed.size()) >= capacity) {
                continue;
            }
        }
        run_.traceable_refresh_archive = std::move(trimmed);
    }

bool AdaptiveSession::add_traceable_refresh_archive(const std::vector<double>& scaled, CandidateFitness fitness, const std::string& source) {
        if (!this->refresh_trace_archive_enabled() ||
            !fitness.trace_feasible ||
            scaled.size() != static_cast<std::size_t>(run_.dim)) {
            return false;
        }
        fitness.source = source;
        fitness.population_index = -1;
        const std::string key = this->refresh_archive_key(fitness);
        const double min_distance =
            std::max(0.0, options.refresh_generation.trace_archive_min_distance);
        const std::string retired_group =
            this->refresh_archive_retired_group_key(scaled, fitness);
        if (!retired_group.empty()) {
            int nearest_retired_index = -1;
            double nearest_retired = std::numeric_limits<double>::infinity();
            std::vector<int> retired_group_indices;
            for (int i = 0; i < static_cast<int>(run_.traceable_refresh_archive.size()); ++i) {
                const auto& entry =
                    run_.traceable_refresh_archive[static_cast<std::size_t>(i)];
                if (this->refresh_archive_retired_group_key(entry.scaled_values, entry.fitness) !=
                    retired_group) {
                    continue;
                }
                retired_group_indices.push_back(i);
                const double distance = scaled_vector_distance(
                    owner.variables_,
                    scaled,
                    entry.scaled_values,
                    options.basins.distance_metric,
                    run_.internal_mode,
                    &owner.base_,
                    options.basins.air_thickness_distance_cap);
                if (distance < nearest_retired) {
                    nearest_retired = distance;
                    nearest_retired_index = i;
                }
            }
            if (nearest_retired_index >= 0 &&
                min_distance > 0.0 &&
                nearest_retired < min_distance) {
                auto& entry =
                    run_.traceable_refresh_archive[
                        static_cast<std::size_t>(nearest_retired_index)];
                if (candidate_better(fitness, entry.fitness, options)) {
                    entry.scaled_values = scaled;
                    entry.fitness = std::move(fitness);
                    this->trim_traceable_refresh_archive();
                    return true;
                }
                return false;
            }
            if (static_cast<int>(retired_group_indices.size()) >=
                run_.retired_refresh_archive_keep_per_family) {
                int worst_index = -1;
                for (int index : retired_group_indices) {
                    if (worst_index < 0 ||
                        candidate_better(
                            run_.traceable_refresh_archive[
                                static_cast<std::size_t>(worst_index)].fitness,
                            run_.traceable_refresh_archive[
                                static_cast<std::size_t>(index)].fitness,
                            options)) {
                        worst_index = index;
                    }
                }
                if (worst_index >= 0 &&
                    candidate_better(
                        fitness,
                        run_.traceable_refresh_archive[
                            static_cast<std::size_t>(worst_index)].fitness,
                        options)) {
                    auto& entry =
                        run_.traceable_refresh_archive[static_cast<std::size_t>(worst_index)];
                    entry.scaled_values = scaled;
                    entry.fitness = std::move(fitness);
                    this->trim_traceable_refresh_archive();
                    return true;
                }
                return false;
            }
            run_.traceable_refresh_archive.push_back({scaled, std::move(fitness)});
            this->trim_traceable_refresh_archive();
            return true;
        }
        int nearest_index = -1;
        double nearest = std::numeric_limits<double>::infinity();
        if (min_distance > 0.0) {
            for (int i = 0; i < static_cast<int>(run_.traceable_refresh_archive.size()); ++i) {
                const auto& entry =
                    run_.traceable_refresh_archive[static_cast<std::size_t>(i)];
                if (this->refresh_archive_key(entry.fitness) != key) {
                    continue;
                }
                const double distance = scaled_vector_distance(
                    owner.variables_,
                    scaled,
                    entry.scaled_values,
                    options.basins.distance_metric,
                    run_.internal_mode,
                    &owner.base_,
                    options.basins.air_thickness_distance_cap);
                if (distance < nearest) {
                    nearest = distance;
                    nearest_index = i;
                }
            }
        }
        if (nearest_index >= 0 && nearest < min_distance) {
            auto& entry = run_.traceable_refresh_archive[static_cast<std::size_t>(nearest_index)];
            if (candidate_better(fitness, entry.fitness, options)) {
                entry.scaled_values = scaled;
                entry.fitness = std::move(fitness);
                this->trim_traceable_refresh_archive();
                return true;
            }
            return false;
        }
        run_.traceable_refresh_archive.push_back({scaled, std::move(fitness)});
        this->trim_traceable_refresh_archive();
        return true;
    }

void AdaptiveSession::stage_traceable_refresh_archive(const std::vector<double>& scaled, CandidateFitness fitness, const std::string& source) {
        const auto stage_start = std::chrono::steady_clock::now();
        if (!this->refresh_trace_archive_enabled() ||
            !fitness.trace_feasible ||
            scaled.size() != static_cast<std::size_t>(run_.dim)) {
            run_.timing.basin_archive_stage_seconds += seconds_since(stage_start);
            return;
        }
        if (this->active_plateau_fitness_matches_retired(scaled, fitness)) {
            this->add_traceable_refresh_archive(scaled, std::move(fitness), source);
            run_.timing.basin_archive_stage_seconds += seconds_since(stage_start);
            return;
        }
        fitness.source = source;
        fitness.population_index = -1;
        const int pending_capacity =
            std::max(256, std::min(4096, this->refresh_trace_archive_capacity() * 4));
        TraceableRefreshArchiveEntry entry{scaled, std::move(fitness)};
        ++run_.pending_traceable_refresh_archive_seen;
        if (static_cast<int>(run_.pending_traceable_refresh_archive.size()) < pending_capacity) {
            run_.pending_traceable_refresh_archive.push_back(std::move(entry));
            run_.timing.basin_archive_stage_seconds += seconds_since(stage_start);
            return;
        }
        const double replace_probability =
            static_cast<double>(pending_capacity) /
            static_cast<double>(std::max<long long>(1, run_.pending_traceable_refresh_archive_seen));
        if (run_.uni(run_.rng) < replace_probability) {
            const int replace_index =
                std::min(
                    pending_capacity - 1,
                    static_cast<int>(run_.uni(run_.rng) * static_cast<double>(pending_capacity)));
            run_.pending_traceable_refresh_archive[static_cast<std::size_t>(replace_index)] =
                std::move(entry);
        }
        run_.timing.basin_archive_stage_seconds += seconds_since(stage_start);
    }

void AdaptiveSession::drain_traceable_refresh_archive_pending(int target_count) {
        if (!this->refresh_trace_archive_enabled() || run_.pending_traceable_refresh_archive.empty()) {
            run_.pending_traceable_refresh_archive.clear();
            run_.pending_traceable_refresh_archive_seen = 0;
            return;
        }
        std::stable_sort(
            run_.pending_traceable_refresh_archive.begin(),
            run_.pending_traceable_refresh_archive.end(),
            [this](const TraceableRefreshArchiveEntry& a,
                       const TraceableRefreshArchiveEntry& b) {
                return candidate_better(a.fitness, b.fitness, options);
            });
        const int capacity = this->refresh_trace_archive_capacity();
        const int update_limit = std::clamp(
            std::max(64, target_count > 0 ? target_count : capacity / 4),
            1,
            std::max(1, std::min(capacity, 512)));
        int added = 0;
        for (const auto& entry : run_.pending_traceable_refresh_archive) {
            if (added >= update_limit) {
                break;
            }
            if (this->add_traceable_refresh_archive(
                    entry.scaled_values,
                    entry.fitness,
                    entry.fitness.source.empty() ? "trace_archive_pending"
                                                 : entry.fitness.source)) {
                ++added;
            }
        }
        run_.pending_traceable_refresh_archive.clear();
        run_.pending_traceable_refresh_archive_seen = 0;
    }

std::vector<double> AdaptiveSession::make_refresh_rows(optiland::PopulationGenerationOptions& epoch_refresh_generation, std::vector<int>& refresh_indices, std::vector<int>& refresh_rebalance_target_families, std::vector<int>& refresh_parent_indices, const BasinRefreshParentPool& basin_refresh_parents, int batch, int offset) {
                        const int targeted_family_count = std::min(
                            static_cast<int>(refresh_rebalance_target_families.size()),
                            static_cast<int>(refresh_indices.size()));
                        if (basin_refresh_parents.topology_parent_indices.empty() &&
                            basin_refresh_parents.family_parent_indices.empty()) {
                            return make_population_generation_rows(
                                owner.base_,
                                owner.variables_,
                                run_.population,
                                run_.dim,
                                refresh_parent_indices,
                                batch,
                                epoch_refresh_generation,
                                run_.internal_mode,
                                run_.rng,
                                run_.uni);
                        }
                        std::vector<double> rows(static_cast<std::size_t>(batch) * run_.dim, 0.0);
                        std::vector<double> fallback_rows;
                        auto fallback_row = [&](int row) {
                            if (fallback_rows.empty()) {
                                fallback_rows = make_population_generation_rows(
                                    owner.base_,
                                    owner.variables_,
                                    run_.population,
                                    run_.dim,
                                    refresh_parent_indices,
                                    batch,
                                    epoch_refresh_generation,
                                    run_.internal_mode,
                                    run_.rng,
                                    run_.uni);
                            }
                            return population_row(fallback_rows, row, run_.dim);
                        };
                        const int topology_count =
                            static_cast<int>(basin_refresh_parents.topology_parent_indices.size());
                        for (int row = 0; row < batch; ++row) {
                            const int absolute_row = offset + row;
                            const std::vector<int>* source_parents = nullptr;
                            bool targeted_family_row = false;
                            if (absolute_row < targeted_family_count) {
                                const int family =
                                    refresh_rebalance_target_families[
                                        static_cast<std::size_t>(absolute_row)];
                                if (family >= 0 &&
                                    family < static_cast<int>(
                                                 basin_refresh_parents.family_parent_indices
                                                     .size())) {
                                    const auto& family_parents =
                                        basin_refresh_parents.family_parent_indices[
                                            static_cast<std::size_t>(family)];
                                    if (!family_parents.empty()) {
                                        source_parents = &family_parents;
                                        targeted_family_row = true;
                                    }
                                }
                            }
                            int topology_slot = -1;
                            if (source_parents == nullptr && topology_count > 0) {
                                for (int step = 0; step < topology_count; ++step) {
                                    const int candidate_slot =
                                        (absolute_row + step) % topology_count;
                                    const auto& topology_parents =
                                        basin_refresh_parents.topology_parent_indices[
                                            static_cast<std::size_t>(
                                                candidate_slot)];
                                    if (!topology_parents.empty()) {
                                        topology_slot = candidate_slot;
                                        source_parents = &topology_parents;
                                        break;
                                    }
                                }
                            }
                            std::vector<double> generated;
                            if (source_parents == nullptr || source_parents->empty()) {
                                generated = fallback_row(row);
                            } else {
                                const int parent_position =
                                    targeted_family_row
                                        ? absolute_row %
                                              static_cast<int>(source_parents->size())
                                        : ((topology_count > 0
                                                ? absolute_row / topology_count
                                                : absolute_row) %
                                           static_cast<int>(source_parents->size()));
                                const int parent =
                                    (*source_parents)[static_cast<std::size_t>(
                                        parent_position)];
                            std::vector<int> parent_indices{parent};
                                generated = make_population_generation_rows(
                                    owner.base_,
                                    owner.variables_,
                                    basin_refresh_parents.population,
                                    run_.dim,
                                    parent_indices,
                                    1,
                                    epoch_refresh_generation,
                                    run_.internal_mode,
                                    run_.rng,
                                    run_.uni);
                                if (generated.size() != static_cast<std::size_t>(run_.dim)) {
                                    generated = fallback_row(row);
                                }
                            }
                            for (int j = 0; j < run_.dim; ++j) {
                                rows[static_cast<std::size_t>(row) * run_.dim + j] =
                                    generated[static_cast<std::size_t>(j)];
                            }
                        }
                        return rows;
                    }

void AdaptiveSession::refresh_epoch_population(const RefreshEpochPopulationContext& frame_) {
    auto& outer = frame_.outer;
    auto& epoch_mating_stats = frame_.epoch_mating_stats;
    auto& epoch_start_merit = frame_.epoch_start_merit;
    auto& epoch_de_generations = frame_.epoch_de_generations;
    auto& epoch_accepted_trials = frame_.epoch_accepted_trials;
    auto& attempted_polish_population_indices = frame_.attempted_polish_population_indices;
    auto& refreshed_count = frame_.refreshed_count;
    auto& refresh_archive_used = frame_.refresh_archive_used;
    auto& epoch_refresh_effective_count = frame_.epoch_refresh_effective_count;
    auto& epoch_refresh_adaptive_skipped = frame_.epoch_refresh_adaptive_skipped;
    auto& epoch_refresh_acceptance_rate = frame_.epoch_refresh_acceptance_rate;
    auto& epoch_refresh_rel_improvement = frame_.epoch_refresh_rel_improvement;

if (outer < run_.outer_epochs) {
            const int requested_refresh_target =
                options.refresh_generation.target_count > 0
                    ? options.refresh_generation.target_count
                    : static_cast<int>(std::round(std::max(0.0, options.refresh_fraction) * run_.pop_size));
            const int refresh_target = std::clamp(
                requested_refresh_target,
                0,
                std::max(0, run_.pop_size - 1));
            int effective_refresh_target = refresh_target;
            if (run_.total_de_generations < options.refresh_warmup_generations) {
                effective_refresh_target = 0;
            }
            const double refresh_acceptance_rate =
                epoch_de_generations > 0 && run_.pop_size > 0
                    ? static_cast<double>(epoch_accepted_trials) /
                          static_cast<double>(
                              std::max(1, epoch_de_generations * run_.pop_size))
                    : std::numeric_limits<double>::quiet_NaN();
            const double before_refresh_merit = run_.best_fitness.score.merit;
            const double refresh_rel_improvement =
                this->relative_improvement(epoch_start_merit, before_refresh_merit);
            if (options.refresh_plateau_pulse_once && !run_.plateau_pulse_used) {
                run_.weak_refresh_windows =
                    std::isfinite(refresh_rel_improvement) &&
                            refresh_rel_improvement < 0.01
                        ? run_.weak_refresh_windows + 1
                        : 0;
            }
            const bool plateau_pulse =
                options.refresh_plateau_pulse_once && !run_.plateau_pulse_used &&
                run_.weak_refresh_windows >= 2;
            PopulationGenerationOptions epoch_refresh_generation =
                options.refresh_generation;
            if (plateau_pulse) {
                epoch_refresh_generation.mode =
                    PopulationGenerationMode::TopHalfSingleSurfaceCorrelatedSag;
                epoch_refresh_generation.asphere_sag_perturbation_um = 0.3;
                epoch_refresh_generation.geometry_child_fraction = 0.0;
                epoch_refresh_generation.vignetting_child_fraction = 0.0;
            }
            bool refresh_adaptive_skipped = false;
            int adaptive_capped_refresh_target = effective_refresh_target;
            if (effective_refresh_target > 0 &&
                options.refresh_generation.adaptive_trigger_enabled) {
                if (options.refresh_generation.adaptive_max_fraction > 0.0) {
                    const int adaptive_cap = std::max(
                        1,
                        static_cast<int>(std::ceil(
                            options.refresh_generation.adaptive_max_fraction *
                            static_cast<double>(run_.pop_size))));
                    effective_refresh_target =
                        std::min(effective_refresh_target, adaptive_cap);
                }
                adaptive_capped_refresh_target = effective_refresh_target;
                const bool active_acceptance =
                    std::isfinite(refresh_acceptance_rate) &&
                    refresh_acceptance_rate >=
                        options.refresh_generation.adaptive_min_acceptance_rate;
                const bool active_improvement =
                    std::isfinite(refresh_rel_improvement) &&
                    refresh_rel_improvement >=
                        options.refresh_generation.adaptive_min_rel_improvement;
                if (active_acceptance && active_improvement) {
                    effective_refresh_target = 0;
                    refresh_adaptive_skipped = true;
                }
            }
            if (refresh_adaptive_skipped &&
                options.basins.active_refresh_rebalance &&
                options.de_mating_scope == DeMatingScope::ActiveBasinOnly &&
                adaptive_capped_refresh_target > 0 &&
                !run_.active_basin_slots.empty()) {
                const int mating_total =
                    epoch_mating_stats.local +
                    epoch_mating_stats.global +
                    epoch_mating_stats.fallback;
                const double local_fraction =
                    mating_total > 0
                        ? static_cast<double>(epoch_mating_stats.local) /
                              static_cast<double>(mating_total)
                        : 0.0;
                if (local_fraction + 1.0e-12 >=
                    std::clamp(
                        options.basins.active_refresh_min_local_fraction,
                        0.0,
                        1.0)) {
                    const ActiveFamilyCoverage coverage =
                        this->active_family_coverage(this->build_local_mating_pools());
                    int overage_total = 0;
                    for (int count : coverage.family_counts) {
                        overage_total += std::max(0, count - coverage.family_limit);
                    }
                    if (coverage.family_limit > 0 && overage_total > 0) {
                        effective_refresh_target =
                            std::min(adaptive_capped_refresh_target, overage_total);
                        refresh_adaptive_skipped = false;
                    }
                }
            }
            epoch_refresh_effective_count = effective_refresh_target;
            epoch_refresh_adaptive_skipped = refresh_adaptive_skipped;
            epoch_refresh_acceptance_rate = refresh_acceptance_rate;
            epoch_refresh_rel_improvement = refresh_rel_improvement;
            
    this->generate_epoch_refresh_candidates(GenerateEpochRefreshCandidatesContext{outer, epoch_mating_stats, epoch_start_merit, attempted_polish_population_indices, refreshed_count, refresh_archive_used, refresh_target, effective_refresh_target, refresh_acceptance_rate, before_refresh_merit, refresh_rel_improvement, plateau_pulse, epoch_refresh_generation, refresh_adaptive_skipped});

        }
}

void AdaptiveSession::generate_epoch_refresh_candidates(const GenerateEpochRefreshCandidatesContext& frame_) {
    auto& outer = frame_.outer;
    auto& epoch_mating_stats = frame_.epoch_mating_stats;
    auto& epoch_start_merit = frame_.epoch_start_merit;
    auto& attempted_polish_population_indices = frame_.attempted_polish_population_indices;
    auto& refreshed_count = frame_.refreshed_count;
    auto& refresh_archive_used = frame_.refresh_archive_used;
    auto& refresh_target = frame_.refresh_target;
    auto& effective_refresh_target = frame_.effective_refresh_target;
    auto& refresh_acceptance_rate = frame_.refresh_acceptance_rate;
    auto& before_refresh_merit = frame_.before_refresh_merit;
    auto& refresh_rel_improvement = frame_.refresh_rel_improvement;
    auto& plateau_pulse = frame_.plateau_pulse;
    auto& epoch_refresh_generation = frame_.epoch_refresh_generation;
    auto& refresh_adaptive_skipped = frame_.refresh_adaptive_skipped;

if (effective_refresh_target > 0) {
                const auto refresh_start = Clock::now();
                const auto order = sorted_fitness_indices(run_.fitness, options);
                const bool top_half_single_surface_refresh =
                    epoch_refresh_generation.mode ==
                        PopulationGenerationMode::TopHalfSingleSurfaceCorrelatedSag ||
                    epoch_refresh_generation.mode ==
                        PopulationGenerationMode::TopHalfSingleBlock ||
                    epoch_refresh_generation.mode ==
                        PopulationGenerationMode::TopHalfCoupledSurface;
                std::vector<int> protected_indices = this->active_protected_indices();
                for (int population_index : attempted_polish_population_indices) {
                    protected_indices.push_back(population_index);
                }
                std::vector<int> refresh_indices;
                refresh_indices.reserve(static_cast<std::size_t>(effective_refresh_target));
                LocalMatingPools refresh_rebalance_pools;
                ActiveFamilyCoverage refresh_rebalance_coverage;
                bool refresh_rebalance_coverage_ready = false;
                std::vector<int> refresh_rebalance_target_families;
                
                
                
                
    this->prepare_surface_refresh(PrepareSurfaceRefreshContext{epoch_mating_stats, effective_refresh_target, order, top_half_single_surface_refresh, protected_indices, refresh_indices, refresh_rebalance_pools, refresh_rebalance_coverage, refresh_rebalance_coverage_ready, refresh_rebalance_target_families});

                if (!top_half_single_surface_refresh) {
                    for (auto it = order.rbegin(); it != order.rend() &&
                                               static_cast<int>(refresh_indices.size()) < effective_refresh_target;
                         ++it) {
                        const int idx = *it;
                        if (this->protected_index(protected_indices, idx) || this->refresh_index_selected(refresh_indices, idx)) {
                            continue;
                        }
                        refresh_indices.push_back(idx);
                    }
                }
                
    this->apply_refresh_population(ApplyRefreshPopulationContext{refreshed_count, refresh_archive_used, before_refresh_merit, plateau_pulse, epoch_refresh_generation, refresh_start, order, top_half_single_surface_refresh, refresh_indices, refresh_rebalance_pools, refresh_rebalance_coverage, refresh_rebalance_coverage_ready, refresh_rebalance_target_families});

                const double refresh_seconds = seconds_since(refresh_start);
                run_.de_seconds += refresh_seconds;
                this->emit_trace(
                    "adaptive_refresh_end",
                    outer,
                    refresh_seconds,
                    epoch_start_merit,
                    before_refresh_merit,
                    0,
                    0,
                    0,
                    refreshed_count,
                    0,
                    run_.used_cuda,
                    0,
                    0,
                    0,
                    refresh_archive_used,
                    effective_refresh_target,
                    refresh_adaptive_skipped,
                    refresh_acceptance_rate,
                    refresh_rel_improvement);
            } else if (refresh_target > 0) {
                this->emit_trace(
                    "adaptive_refresh_end",
                    outer,
                    0.0,
                    epoch_start_merit,
                    before_refresh_merit,
                    0,
                    0,
                    0,
                    0,
                    0,
                    run_.used_cuda,
                    0,
                    0,
                    0,
                    0,
                    effective_refresh_target,
                    refresh_adaptive_skipped,
                    refresh_acceptance_rate,
                    refresh_rel_improvement);
            }
}

} } // namespace optiland::optimizer_detail
