/*
 * Prepare epoch refresh slots and rebalance seed families before replacing adaptive population rows.
 * Refresh shares protected slots, incumbent fitness, pending traceable seeds and run_.rng.
 * Keep slot selection, family quotas and seed-source order synchronized with replacement evaluation.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::prepare_surface_refresh(const PrepareSurfaceRefreshContext& frame_) {
    auto& epoch_mating_stats = frame_.epoch_mating_stats;
    auto& effective_refresh_target = frame_.effective_refresh_target;
    auto& order = frame_.order;
    auto& top_half_single_surface_refresh = frame_.top_half_single_surface_refresh;
    auto& protected_indices = frame_.protected_indices;
    auto& refresh_indices = frame_.refresh_indices;
    auto& refresh_rebalance_pools = frame_.refresh_rebalance_pools;
    auto& refresh_rebalance_coverage = frame_.refresh_rebalance_coverage;
    auto& refresh_rebalance_coverage_ready = frame_.refresh_rebalance_coverage_ready;
    auto& refresh_rebalance_target_families = frame_.refresh_rebalance_target_families;

if (top_half_single_surface_refresh) {
                    const int protected_count = (run_.pop_size + 1) / 2;
                    const int maximum_replacement = run_.pop_size - protected_count;
                    const int selected_count = std::min(
                        effective_refresh_target,
                        maximum_replacement);
                    for (auto it = order.rbegin();
                         it != order.rend() &&
                         static_cast<int>(refresh_indices.size()) < selected_count;
                         ++it) {
                        const int idx = *it;
                        const auto position = std::find(order.begin(), order.end(), idx);
                        if (position == order.end() ||
                            std::distance(order.begin(), position) < protected_count) {
                            continue;
                        }
                        refresh_indices.push_back(idx);
                    }
                } else 
    this->rebalance_refresh_family_seeds(RebalanceRefreshFamilySeedsContext{epoch_mating_stats, effective_refresh_target, order, protected_indices, refresh_indices, refresh_rebalance_pools, refresh_rebalance_coverage, refresh_rebalance_coverage_ready, refresh_rebalance_target_families});

}

void AdaptiveSession::rebalance_refresh_family_seeds(const RebalanceRefreshFamilySeedsContext& frame_) {
    auto& epoch_mating_stats = frame_.epoch_mating_stats;
    auto& effective_refresh_target = frame_.effective_refresh_target;
    auto& order = frame_.order;
    auto& protected_indices = frame_.protected_indices;
    auto& refresh_indices = frame_.refresh_indices;
    auto& refresh_rebalance_pools = frame_.refresh_rebalance_pools;
    auto& refresh_rebalance_coverage = frame_.refresh_rebalance_coverage;
    auto& refresh_rebalance_coverage_ready = frame_.refresh_rebalance_coverage_ready;
    auto& refresh_rebalance_target_families = frame_.refresh_rebalance_target_families;

if (options.basins.active_refresh_rebalance &&
                    options.de_mating_scope == DeMatingScope::ActiveBasinOnly &&
                    effective_refresh_target > 0 &&
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
                    
    this->replace_refresh_seed_slots(ReplaceRefreshSeedSlotsContext{effective_refresh_target, order, protected_indices, refresh_indices, refresh_rebalance_pools, refresh_rebalance_coverage, refresh_rebalance_coverage_ready, refresh_rebalance_target_families, local_fraction});

                }
}

void AdaptiveSession::replace_refresh_seed_slots(const ReplaceRefreshSeedSlotsContext& frame_) {
    auto& effective_refresh_target = frame_.effective_refresh_target;
    auto& order = frame_.order;
    auto& protected_indices = frame_.protected_indices;
    auto& refresh_indices = frame_.refresh_indices;
    auto& refresh_rebalance_pools = frame_.refresh_rebalance_pools;
    auto& refresh_rebalance_coverage = frame_.refresh_rebalance_coverage;
    auto& refresh_rebalance_coverage_ready = frame_.refresh_rebalance_coverage_ready;
    auto& refresh_rebalance_target_families = frame_.refresh_rebalance_target_families;
    auto& local_fraction = frame_.local_fraction;

if (local_fraction + 1.0e-12 >=
                        std::clamp(
                            options.basins.active_refresh_min_local_fraction,
                            0.0,
                            1.0)) {
                        refresh_rebalance_pools = this->build_local_mating_pools();
                        refresh_rebalance_coverage =
                            this->active_family_coverage(refresh_rebalance_pools);
                        refresh_rebalance_coverage_ready = true;
                        const ActiveFamilyCoverage& coverage =
                            refresh_rebalance_coverage;
                        run_.last_active_refresh_rebalance_family_count =
                            static_cast<int>(coverage.family_keys.size());
                        run_.last_active_refresh_rebalance_family_limit =
                            coverage.family_limit;
                        run_.last_active_refresh_rebalance_max_family_size =
                            coverage.max_family_size;
                        std::vector<int> overage = coverage.family_counts;
                        std::vector<int> shortage = coverage.family_counts;
                        int overage_total = 0;
                        int shortage_total = 0;
                        for (int& count : overage) {
                            count = std::max(0, count - coverage.family_limit);
                            overage_total += count;
                        }
                        for (int& count : shortage) {
                            count = std::max(0, coverage.family_limit - count);
                            shortage_total += count;
                        }
                        run_.last_active_refresh_rebalance_candidate_count = overage_total;
                        if (coverage.family_limit > 0 && overage_total > 0) {
                            const int rebalance_target = std::min(
                                effective_refresh_target,
                                overage_total);
                            for (auto it = order.rbegin();
                                 it != order.rend() &&
                                 static_cast<int>(refresh_indices.size()) < rebalance_target;
                                 ++it) {
                                const int idx = *it;
                                if (idx < 0 || idx >= run_.pop_size ||
                                    this->protected_index(protected_indices, idx) ||
                                    this->refresh_index_selected(refresh_indices, idx) ||
                                    idx >= static_cast<int>(
                                               coverage.family_for_population.size())) {
                                    continue;
                                }
                                const int family =
                                    coverage.family_for_population[
                                        static_cast<std::size_t>(idx)];
                                if (family < 0 ||
                                    family >= static_cast<int>(overage.size()) ||
                                    overage[static_cast<std::size_t>(family)] <= 0) {
                                    continue;
                                }
                                refresh_indices.push_back(idx);
                                --overage[static_cast<std::size_t>(family)];
                            }
                            run_.last_active_refresh_rebalance_selected_count =
                                static_cast<int>(refresh_indices.size());
                            if (!coverage.family_keys.empty() &&
                                !refresh_indices.empty()) {
                                std::vector<int> ranked_families(
                                    coverage.family_keys.size());
                                std::iota(
                                    ranked_families.begin(),
                                    ranked_families.end(),
                                    0);
                                std::stable_sort(
                                    ranked_families.begin(),
                                    ranked_families.end(),
                                    [&](int a, int b) {
                                        const int shortage_a =
                                            shortage[static_cast<std::size_t>(a)];
                                        const int shortage_b =
                                            shortage[static_cast<std::size_t>(b)];
                                        if (shortage_a != shortage_b) {
                                            return shortage_a > shortage_b;
                                        }
                                        const int count_a =
                                            coverage.family_counts[
                                                static_cast<std::size_t>(a)];
                                        const int count_b =
                                            coverage.family_counts[
                                                static_cast<std::size_t>(b)];
                                        if (count_a != count_b) {
                                            return count_a < count_b;
                                        }
                                        return a < b;
                                    });
                                refresh_rebalance_target_families.reserve(
                                    refresh_indices.size());
                                std::vector<int> remaining_shortage = shortage;
                                while (static_cast<int>(
                                           refresh_rebalance_target_families.size()) <
                                       static_cast<int>(refresh_indices.size())) {
                                    bool added = false;
                                    for (int family : ranked_families) {
                                        if (family < 0 ||
                                            family >= static_cast<int>(
                                                          remaining_shortage.size())) {
                                            continue;
                                        }
                                        if (shortage_total > 0 &&
                                            remaining_shortage[static_cast<std::size_t>(
                                                family)] <= 0 &&
                                            static_cast<int>(
                                                refresh_rebalance_target_families.size()) <
                                                shortage_total) {
                                            continue;
                                        }
                                        refresh_rebalance_target_families.push_back(
                                            family);
                                        if (remaining_shortage[static_cast<std::size_t>(
                                                family)] > 0) {
                                            --remaining_shortage[static_cast<std::size_t>(
                                                family)];
                                        }
                                        added = true;
                                        if (static_cast<int>(
                                                refresh_rebalance_target_families.size()) >=
                                            static_cast<int>(refresh_indices.size())) {
                                            break;
                                        }
                                    }
                                    if (!added) {
                                        break;
                                    }
                                }
                            }
                        }
                    }
}

void AdaptiveSession::apply_refresh_population(const ApplyRefreshPopulationContext& frame_) {
    auto& refreshed_count = frame_.refreshed_count;
    auto& refresh_archive_used = frame_.refresh_archive_used;
    auto& before_refresh_merit = frame_.before_refresh_merit;
    auto& plateau_pulse = frame_.plateau_pulse;
    auto& epoch_refresh_generation = frame_.epoch_refresh_generation;
    auto& refresh_start = frame_.refresh_start;
    auto& order = frame_.order;
    auto& top_half_single_surface_refresh = frame_.top_half_single_surface_refresh;
    auto& refresh_indices = frame_.refresh_indices;
    auto& refresh_rebalance_pools = frame_.refresh_rebalance_pools;
    auto& refresh_rebalance_coverage = frame_.refresh_rebalance_coverage;
    auto& refresh_rebalance_coverage_ready = frame_.refresh_rebalance_coverage_ready;
    auto& refresh_rebalance_target_families = frame_.refresh_rebalance_target_families;

if (!refresh_indices.empty()) {
                    std::vector<int> refresh_parent_indices;
                    refresh_parent_indices.reserve(static_cast<std::size_t>(
                        std::max(1, options.refresh_generation.parent_count)));
                    if (top_half_single_surface_refresh) {
                        const int protected_count = (run_.pop_size + 1) / 2;
                        std::vector<int> parent_candidates(
                            order.begin(),
                            order.begin() + std::min(
                                protected_count,
                                static_cast<int>(order.size())));
                        std::shuffle(parent_candidates.begin(), parent_candidates.end(), run_.rng);
                        const int parent_limit = std::min(
                            static_cast<int>(parent_candidates.size()),
                            std::max(1, options.refresh_generation.parent_count));
                        refresh_parent_indices.assign(
                            parent_candidates.begin(),
                            parent_candidates.begin() + parent_limit);
                    } else {
                        for (int idx : order) {
                            if (std::find(refresh_indices.begin(), refresh_indices.end(), idx) !=
                                refresh_indices.end()) {
                                continue;
                            }
                            if (idx < 0 || idx >= static_cast<int>(run_.fitness.size()) ||
                                this->global_refill_blocked_fitness(
                                    run_.fitness[static_cast<std::size_t>(idx)])) {
                                continue;
                            }
                            refresh_parent_indices.push_back(idx);
                            if (static_cast<int>(refresh_parent_indices.size()) >=
                                std::max(1, options.refresh_generation.parent_count)) {
                                break;
                            }
                        }
                    }
                    if (refresh_parent_indices.empty()) {
                        refresh_parent_indices.push_back(run_.best_idx);
                    }
                    
                    
                    const BasinRefreshParentPool basin_refresh_parents =
                        this->make_basin_refresh_parent_pool(top_half_single_surface_refresh, refresh_rebalance_pools, refresh_rebalance_coverage, refresh_rebalance_coverage_ready);
                    

                    const int target = static_cast<int>(refresh_indices.size());
                    const bool refresh_trace_filter_enabled =
                        options.refresh_generation.trace_feasible_gate ||
                        (!top_half_single_surface_refresh &&
                         options.allow_near_trace_infeasible);
                    PopulationGenerationOptions refresh_candidate_limit_options =
                        options.refresh_generation;
                    refresh_candidate_limit_options.trace_feasible_gate =
                        refresh_trace_filter_enabled;
                    const int max_attempts =
                        population_generation_candidate_limit(refresh_candidate_limit_options, target);
                    int attempts = 0;
                    std::vector<double> accepted_rows;
                    std::vector<CandidateFitness> accepted_fitness;
                    accepted_rows.reserve(static_cast<std::size_t>(target) * run_.dim);
                    accepted_fitness.reserve(static_cast<std::size_t>(target));
                    while (static_cast<int>(accepted_fitness.size()) < target &&
                           attempts < max_attempts &&
                           !population_generation_time_exhausted(refresh_start, options.refresh_generation)) {
                        const int remaining_attempts = max_attempts - attempts;
                        const int remaining_target =
                            target - static_cast<int>(accepted_fitness.size());
                        const int trace_gate_floor =
                            refresh_trace_filter_enabled
                                ? std::min(64, std::max(8, target))
                                : remaining_target;
                        const int cuda_batch_floor =
                            (run_.de_options.use_cuda &&
                             distortion_model_allows_cuda(owner.merit_config_) &&
                             evaluate_population_cuda_available())
                                ? cuda_adaptive_min_batch_size(run_.de_options.cuda_min_batch_size)
                                : 0;
                        const int batch_floor =
                            cuda_batch_floor > 0
                                ? std::max(trace_gate_floor, cuda_batch_floor)
                                : trace_gate_floor;
                        const int batch = std::min(
                            remaining_attempts,
                            std::max(remaining_target, batch_floor));
                        std::vector<double> candidate_rows = this->make_refresh_rows(epoch_refresh_generation, refresh_indices, refresh_rebalance_target_families, refresh_parent_indices, basin_refresh_parents, batch, attempts);
                        attempts += batch;
                        std::vector<CandidateFitness> candidate_fitness =
                            this->evaluate_population_fitness(
                                candidate_rows,
                                batch,
                                run_.total_de_generations,
                                "refresh_candidate");
                        for (int row = 0; row < batch &&
                                          static_cast<int>(accepted_fitness.size()) < target;
                             ++row) {
                            if (refresh_trace_filter_enabled &&
                                !candidate_fitness[static_cast<std::size_t>(row)].trace_feasible) {
                                continue;
                            }
                            const double refresh_merit =
                                candidate_fitness[static_cast<std::size_t>(row)].score.merit;
                            if (options.refresh_generation.max_merit_ratio > 0.0 &&
                                (!std::isfinite(refresh_merit) ||
                                 refresh_merit >
                                     options.refresh_generation.max_merit_ratio *
                                         before_refresh_merit)) {
                                continue;
                            }
                            std::vector<double> scaled =
                                population_row(candidate_rows, row, run_.dim);
                            if (this->active_plateau_fitness_matches_retired(
                                    scaled,
                                    candidate_fitness[static_cast<std::size_t>(row)])) {
                                continue;
                            }
                            accepted_rows.insert(accepted_rows.end(), scaled.begin(), scaled.end());
                            accepted_fitness.push_back(
                                std::move(candidate_fitness[static_cast<std::size_t>(row)]));
                        }
                    }
                    if (static_cast<int>(accepted_fitness.size()) < target &&
                        this->refresh_trace_archive_enabled() &&
                        (!run_.traceable_refresh_archive.empty() ||
                         !run_.pending_traceable_refresh_archive.empty())) {
                        if (!run_.pending_traceable_refresh_archive.empty()) {
                            this->drain_traceable_refresh_archive_pending(target);
                        }
                        const int remaining =
                            target - static_cast<int>(accepted_fitness.size());
                        const int archive_target = std::clamp(
                            static_cast<int>(std::ceil(
                                static_cast<double>(remaining) *
                                std::clamp(
                                    options.refresh_generation.trace_archive_fraction,
                                    0.0,
                                    1.0))),
                            0,
                            remaining);
                        std::vector<int> archive_order(run_.traceable_refresh_archive.size());
                        std::iota(archive_order.begin(), archive_order.end(), 0);
                        std::shuffle(archive_order.begin(), archive_order.end(), run_.rng);
                        for (int archive_index : archive_order) {
                            if (refresh_archive_used >= archive_target ||
                                static_cast<int>(accepted_fitness.size()) >= target) {
                                break;
                            }
                            const auto& entry = run_.traceable_refresh_archive[
                                static_cast<std::size_t>(archive_index)];
                            if (entry.scaled_values.size() != static_cast<std::size_t>(run_.dim) ||
                                !entry.fitness.trace_feasible ||
                                this->global_refill_blocked_fitness(entry.fitness)) {
                                continue;
                            }
                            if (options.refresh_generation.max_merit_ratio > 0.0 &&
                                (!std::isfinite(entry.fitness.score.merit) ||
                                 entry.fitness.score.merit >
                                     options.refresh_generation.max_merit_ratio *
                                         before_refresh_merit)) {
                                continue;
                            }
                            if (this->active_plateau_fitness_matches_retired(
                                    entry.scaled_values,
                                    entry.fitness)) {
                                continue;
                            }
                            accepted_rows.insert(
                                accepted_rows.end(),
                                entry.scaled_values.begin(),
                                entry.scaled_values.end());
                            CandidateFitness archive_fitness = entry.fitness;
                            archive_fitness.generation = run_.total_de_generations;
                            archive_fitness.source = "refresh_archive_candidate";
                            accepted_fitness.push_back(std::move(archive_fitness));
                            ++refresh_archive_used;
                        }
                    }
                    refreshed_count = static_cast<int>(accepted_fitness.size());
                    for (int row = 0; row < refreshed_count; ++row) {
                        const int idx = refresh_indices[static_cast<std::size_t>(row)];
                        std::vector<double> scaled = population_row(accepted_rows, row, run_.dim);
                        this->stage_traceable_refresh_archive(
                            population_row(run_.population, idx, run_.dim),
                            run_.fitness[static_cast<std::size_t>(idx)],
                            "refresh_replaced");
                        const bool from_archive =
                            accepted_fitness[static_cast<std::size_t>(row)].source ==
                            "refresh_archive_candidate";
                        set_population_row(run_.population, idx, scaled, owner.variables_, run_.internal_mode);
                        accepted_fitness[static_cast<std::size_t>(row)].population_index = idx;
                        accepted_fitness[static_cast<std::size_t>(row)].source =
                            from_archive ? "refresh_archive" : "refresh";
                        run_.fitness[static_cast<std::size_t>(idx)] =
                            accepted_fitness[static_cast<std::size_t>(row)];
                        this->consider_basin(
                            scaled,
                            run_.fitness[static_cast<std::size_t>(idx)],
                            from_archive ? "refresh_archive" : "refresh",
                            false);
                        this->stage_traceable_refresh_archive(
                            scaled,
                            run_.fitness[static_cast<std::size_t>(idx)],
                            from_archive ? "refresh_archive" : "refresh");
                    }
                    if (refreshed_count > 0) {
                        this->sync_merits();
                        this->trim_external_archive();
                        this->update_best_from_population();
                        if (plateau_pulse) {
                            run_.mu_f = std::clamp(options.mu_f, 1.0e-6, 1.0);
                            run_.mu_cr = std::clamp(options.mu_cr, 0.0, 1.0);
                            run_.external_archive.clear();
                            run_.external_archive_keys.clear();
                            run_.plateau_pulse_used = true;
                            run_.weak_refresh_windows = 0;
                            std::cerr << "[post_asphere_plateau_pulse] generation="
                                      << run_.total_de_generations
                                      << " refreshed=" << refreshed_count
                                      << " sag_um=0.3 jade_reset=1\n";
                        }
                    }
                }
}

bool AdaptiveSession::refresh_index_selected(std::vector<int>& refresh_indices, int idx) {
                    return this->contains_index(refresh_indices, idx);
                }

} } // namespace optiland::optimizer_detail
