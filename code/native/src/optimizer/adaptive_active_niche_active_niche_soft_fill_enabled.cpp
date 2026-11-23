/*
 * Resolve active-niche quotas, membership and merit snapshots for one adaptive generation.
 * Generation selection shares run_.population_niche_id and family keys with focus and lifecycle policy.
 * Keep membership resets, cap enforcement and published quota statistics in their existing sequence.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

bool AdaptiveSession::active_niche_soft_fill_enabled() {
        return options.basins.active_niche_tracking &&
               options.basins.active_niche_soft_fill;
    }

bool AdaptiveSession::active_niche_epoch_enabled() {
        const int start_epoch =
            std::max(1, options.basins.active_niche_start_epoch);
        return run_.current_outer_epoch <= 0 || run_.current_outer_epoch >= start_epoch;
    }

bool AdaptiveSession::active_niche_tracking_enabled() {
        return this->active_niche_epoch_enabled() &&
               (options.basins.active_niche_tracking ||
                options.basins.active_niche_survival ||
                this->active_focus_rebalance_enabled());
    }

std::vector<std::string > AdaptiveSession::unique_active_niche_keys(std::vector<std::string> keys) {
        std::vector<std::string> unique;
        unique.reserve(keys.size());
        const bool allow_similarity = this->active_mating_allow_similarity();
        for (std::string& key : keys) {
            if (key.empty()) {
                continue;
            }
            bool matched = false;
            for (const std::string& existing : unique) {
                if (this->active_keys_match(key, existing, allow_similarity)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                unique.push_back(std::move(key));
            }
        }
        return unique;
    }

int AdaptiveSession::resolve_active_niche_min_quota() {
        if (options.basins.active_niche_min_quota > 0) {
            return std::min(run_.pop_size, options.basins.active_niche_min_quota);
        }
        const int by_fraction =
            static_cast<int>(std::ceil(0.02 * static_cast<double>(run_.pop_size)));
        return std::clamp(
            std::max(std::max(1, options.de_mating_min_pool), by_fraction),
            1,
            std::max(1, run_.pop_size));
    }

int AdaptiveSession::resolve_active_niche_family_cap(int min_quota) {
        const double fraction = std::clamp(
            options.basins.active_niche_family_cap_fraction,
            0.0,
            1.0);
        if (fraction <= 0.0) {
            return run_.pop_size;
        }
        return std::clamp(
            std::max(
                min_quota,
                static_cast<int>(std::ceil(fraction * static_cast<double>(run_.pop_size)))),
            1,
            std::max(1, run_.pop_size));
    }

int AdaptiveSession::resolve_active_niche_target_quota(int min_quota, int family_cap) {
        int target = options.basins.active_niche_target_quota > 0
                         ? options.basins.active_niche_target_quota
                         : min_quota;
        target = std::max(target, min_quota);
        return std::clamp(target, 1, std::max(1, family_cap));
    }

void AdaptiveSession::refresh_active_niche_membership(const LocalMatingPools& pools, bool force_reset) {
            if (!this->active_niche_tracking_enabled()) {
                run_.population_niche_id.clear();
                run_.active_niche_family_keys.clear();
                run_.active_focus_family_keys.clear();
                run_.active_niche_membership_reset_pending = false;
                return;
            }
            std::vector<std::string> family_keys = this->active_family_keys_from_frozen_slots();
            if (family_keys.empty()) {
                family_keys = this->active_family_keys_from_slots(pools);
            }
            if (this->active_focus_rebalance_enabled() &&
                !run_.active_focus_family_keys.empty()) {
                family_keys.insert(
                    family_keys.end(),
                    run_.active_focus_family_keys.begin(),
                    run_.active_focus_family_keys.end());
            }
            if (this->active_focus_rebalance_enabled() &&
                options.basins.active_focus_initial_best_family &&
                run_.active_focus_family_keys.empty() &&
                run_.best_idx >= 0 &&
                run_.best_idx < run_.pop_size) {
                std::string best_key = this->active_target_key_for_index(run_.best_idx, pools);
                if (best_key.empty()) {
                    best_key = this->fitness_diversity_key(run_.best_fitness);
                }
                if (!best_key.empty()) {
                    family_keys.insert(family_keys.begin(), std::move(best_key));
                }
            }
            family_keys = this->unique_active_niche_keys(std::move(family_keys));
            const bool keys_changed = family_keys != run_.active_niche_family_keys;
            const bool focus_bridge_frozen =
                this->adaptive_glass_tolerance_focus_bridge_frozen();
            if (focus_bridge_frozen && !run_.active_focus_family_keys.empty()) {
                run_.epoch_active_focus_reset_count +=
                    static_cast<int>(run_.active_focus_family_keys.size());
                run_.active_focus_family_keys.clear();
            }
            if (keys_changed && !focus_bridge_frozen) {
                this->remap_active_focus_keys_to_families(family_keys);
            }
            if (force_reset || keys_changed ||
                run_.population_niche_id.size() != static_cast<std::size_t>(run_.pop_size)) {
                run_.population_niche_id.assign(static_cast<std::size_t>(run_.pop_size), -1);
            } else if (run_.population_niche_id.size() < static_cast<std::size_t>(run_.pop_size)) {
                run_.population_niche_id.resize(static_cast<std::size_t>(run_.pop_size), -1);
            }
            run_.active_niche_family_keys = std::move(family_keys);
            if (run_.active_niche_family_keys.empty()) {
                std::fill(run_.population_niche_id.begin(), run_.population_niche_id.end(), -1);
                run_.active_focus_family_keys.clear();
                run_.active_niche_membership_reset_pending = false;
                return;
            }
            for (int idx = 0; idx < run_.pop_size; ++idx) {
                const bool needs_assignment =
                    force_reset ||
                    keys_changed ||
                    idx >= static_cast<int>(run_.population_niche_id.size()) ||
                    run_.population_niche_id[static_cast<std::size_t>(idx)] >=
                        static_cast<int>(run_.active_niche_family_keys.size());
                if (!needs_assignment) {
                    continue;
                }
                const std::string key = this->active_target_key_for_index(idx, pools);
                run_.population_niche_id[static_cast<std::size_t>(idx)] =
                    this->active_family_index_for_key(key, run_.active_niche_family_keys);
            }
            run_.active_niche_membership_reset_pending = false;
        }

ActiveNicheGenerationState AdaptiveSession::make_active_niche_generation_state() {
        ActiveNicheGenerationState state;
        if (!this->active_niche_tracking_enabled() ||
            run_.active_niche_family_keys.empty() ||
            run_.population_niche_id.size() != static_cast<std::size_t>(run_.pop_size)) {
            return state;
        }
        state.configured = true;
        state.survival_enabled =
            options.basins.active_niche_survival ||
            this->active_focus_rebalance_enabled();
        state.family_keys = run_.active_niche_family_keys;
        state.member_counts.assign(state.family_keys.size(), 0);
        state.min_quota = this->resolve_active_niche_min_quota();
        state.family_cap = this->resolve_active_niche_family_cap(state.min_quota);
        state.target_quota =
            this->resolve_active_niche_target_quota(state.min_quota, state.family_cap);
        state.family_target_quotas.assign(
            state.family_keys.size(),
            state.target_quota);
        state.family_caps.assign(state.family_keys.size(), state.family_cap);
        state.focus_family.assign(state.family_keys.size(), 0);
        for (int niche_id : run_.population_niche_id) {
            if (niche_id >= 0 &&
                niche_id < static_cast<int>(state.member_counts.size())) {
                int& count = state.member_counts[static_cast<std::size_t>(niche_id)];
                ++count;
                ++state.member_count;
                state.max_family_size = std::max(state.max_family_size, count);
            } else {
                ++state.free_count;
            }
        }

        const bool focus_enabled =
            this->active_focus_rebalance_enabled() &&
            state.survival_enabled &&
            !state.family_keys.empty();
        
    this->configure_focus_niches(state, focus_enabled);


        for (std::size_t family = 0;
             family < state.member_counts.size();
             ++family) {
            const int count = state.member_counts[family];
            if (count < state.min_quota) {
                ++state.below_min_count;
            }
            const int target_quota =
                family < state.family_target_quotas.size()
                    ? state.family_target_quotas[family]
                    : state.target_quota;
            if (count < target_quota) {
                state.quota_deficit += target_quota - count;
            }
        }
        if (run_.pop_size > 0) {
            state.max_family_fraction =
                static_cast<double>(state.max_family_size) /
                static_cast<double>(run_.pop_size);
        }
        if (!state.member_counts.empty()) {
            const double mean =
                static_cast<double>(state.member_count) /
                static_cast<double>(state.member_counts.size());
            if (mean > 0.0) {
                double abs_sum = 0.0;
                for (int left : state.member_counts) {
                    for (int right : state.member_counts) {
                        abs_sum += std::abs(static_cast<double>(left - right));
                    }
                }
                const double n = static_cast<double>(state.member_counts.size());
                state.occupancy_gini = abs_sum / (2.0 * n * n * mean);
            } else {
                state.occupancy_gini = 0.0;
            }
        }
        return state;
    }

int AdaptiveSession::active_niche_family_cap_for(const ActiveNicheGenerationState& state, int niche_id) {
            if (niche_id >= 0 &&
                niche_id < static_cast<int>(state.family_caps.size())) {
                return state.family_caps[static_cast<std::size_t>(niche_id)];
            }
            return state.family_cap;
        }

int AdaptiveSession::active_niche_target_quota_for(const ActiveNicheGenerationState& state, int niche_id) {
            if (niche_id >= 0 &&
                niche_id < static_cast<int>(state.family_target_quotas.size())) {
                return state.family_target_quotas[
                    static_cast<std::size_t>(niche_id)];
            }
            return state.target_quota;
        }

std::vector<optiland::ActiveNicheMeritSnapshot> AdaptiveSession::build_active_niche_merit_snapshots(const ActiveNicheGenerationState& state) {
            std::vector<ActiveNicheMeritSnapshot> snapshots;
            if (!state.configured || state.family_keys.empty() ||
                run_.population_niche_id.size() != static_cast<std::size_t>(run_.pop_size)) {
                return snapshots;
            }
            snapshots.reserve(state.family_keys.size());
            for (int niche = 0; niche < static_cast<int>(state.family_keys.size());
                 ++niche) {
                ActiveNicheMeritSnapshot snapshot;
                snapshot.niche_index = niche;
                snapshot.key = state.family_keys[static_cast<std::size_t>(niche)];
                snapshot.focus =
                    niche < static_cast<int>(state.focus_family.size()) &&
                    state.focus_family[static_cast<std::size_t>(niche)] != 0;
                snapshots.push_back(std::move(snapshot));
            }
            for (int idx = 0; idx < run_.pop_size &&
                              idx < static_cast<int>(run_.fitness.size());
                 ++idx) {
                const int niche =
                    run_.population_niche_id[static_cast<std::size_t>(idx)];
                if (niche < 0 || niche >= static_cast<int>(snapshots.size())) {
                    continue;
                }
                ActiveNicheMeritSnapshot& snapshot =
                    snapshots[static_cast<std::size_t>(niche)];
                ++snapshot.member_count;
                const double merit =
                    run_.fitness[static_cast<std::size_t>(idx)].score.merit;
                if (!std::isfinite(merit)) {
                    continue;
                }
                if (!std::isfinite(snapshot.best_merit) ||
                    merit < snapshot.best_merit) {
                    snapshot.best_merit = merit;
                    snapshot.best_population_index = idx;
                }
            }
            return snapshots;
        }

void AdaptiveSession::sync_active_niche_state_snapshot(const ActiveNicheGenerationState& state) {
            if (!state.configured) {
                run_.last_active_niche_merit_snapshots.clear();
                run_.last_active_niche_stats.focus_family_count =
                    static_cast<int>(run_.active_focus_family_keys.size());
                run_.last_active_niche_stats.focus_member_count = 0;
                if (run_.active_focus_family_keys.empty()) {
                    run_.last_active_niche_stats.focus_target_quota = 0;
                    run_.last_active_niche_stats.focus_family_cap = 0;
                }
                run_.last_active_niche_stats.focus_ready_count =
                    run_.last_active_focus_ready_count;
                run_.last_active_niche_stats.focus_cooldown_family_count =
                    run_.last_active_focus_cooldown_family_count;
                run_.last_active_niche_stats.focus_stopped_family_count =
                    run_.last_active_focus_stopped_family_count;
                run_.last_active_niche_stats.focus_last_rel_improvement =
                    run_.last_active_focus_rel_improvement;
                return;
            }
            run_.last_active_niche_stats.family_count =
                static_cast<int>(state.family_keys.size());
            run_.last_active_niche_stats.member_count = state.member_count;
            run_.last_active_niche_stats.free_count = state.free_count;
            run_.last_active_niche_stats.min_quota = state.min_quota;
            run_.last_active_niche_stats.target_quota = state.target_quota;
            run_.last_active_niche_stats.family_cap = state.family_cap;
            run_.last_active_niche_stats.quota_deficit = state.quota_deficit;
            run_.last_active_niche_stats.below_min_count = state.below_min_count;
            run_.last_active_niche_stats.max_family_size = state.max_family_size;
            run_.last_active_niche_stats.max_family_fraction =
                state.max_family_fraction;
            run_.last_active_niche_stats.occupancy_gini = state.occupancy_gini;
            run_.last_active_niche_stats.focus_family_count =
                state.focus_family_count;
            run_.last_active_niche_stats.focus_member_count =
                state.focus_member_count;
            run_.last_active_niche_stats.focus_target_quota =
                state.focus_target_quota;
            run_.last_active_niche_stats.focus_family_cap =
                state.focus_family_cap;
            run_.last_active_niche_stats.monitor_family_cap =
                state.monitor_family_cap;
            run_.last_active_niche_stats.focus_ready_count =
                run_.last_active_focus_ready_count;
            run_.last_active_niche_stats.focus_cooldown_family_count =
                run_.last_active_focus_cooldown_family_count;
            run_.last_active_niche_stats.focus_stopped_family_count =
                run_.last_active_focus_stopped_family_count;
            run_.last_active_niche_stats.focus_last_rel_improvement =
                run_.last_active_focus_rel_improvement;
            run_.last_active_niche_merit_snapshots =
                this->build_active_niche_merit_snapshots(state);
        }

void AdaptiveSession::publish_active_niche_stats(const ActiveNicheGenerationState& state, int floor_blocked, int family_cap_blocked, int deferred_candidates, int repair_moves, int rebalance_candidates, int rebalance_moves) {
            run_.last_active_niche_stats = ActiveNicheStats{};
            if (!state.configured) {
                run_.last_active_niche_merit_snapshots.clear();
                return;
            }
            run_.last_active_niche_stats.family_count =
                static_cast<int>(state.family_keys.size());
            run_.last_active_niche_stats.member_count = state.member_count;
            run_.last_active_niche_stats.free_count = state.free_count;
            run_.last_active_niche_stats.min_quota = state.min_quota;
            run_.last_active_niche_stats.target_quota = state.target_quota;
            run_.last_active_niche_stats.family_cap = state.family_cap;
            run_.last_active_niche_stats.quota_deficit = state.quota_deficit;
            run_.last_active_niche_stats.below_min_count = state.below_min_count;
            run_.last_active_niche_stats.max_family_size = state.max_family_size;
            run_.last_active_niche_stats.max_family_fraction =
                state.max_family_fraction;
            run_.last_active_niche_stats.occupancy_gini = state.occupancy_gini;
            run_.last_active_niche_stats.floor_blocked = floor_blocked;
            run_.last_active_niche_stats.family_cap_blocked = family_cap_blocked;
            run_.last_active_niche_stats.deferred_candidates = deferred_candidates;
            run_.last_active_niche_stats.repair_moves = repair_moves;
            run_.last_active_niche_stats.rebalance_candidates = rebalance_candidates;
            run_.last_active_niche_stats.rebalance_moves = rebalance_moves;
            run_.last_active_niche_stats.focus_family_count =
                state.focus_family_count;
            run_.last_active_niche_stats.focus_member_count =
                state.focus_member_count;
            run_.last_active_niche_stats.focus_target_quota =
                state.focus_target_quota;
            run_.last_active_niche_stats.focus_family_cap =
                state.focus_family_cap;
            run_.last_active_niche_stats.monitor_family_cap =
                state.monitor_family_cap;
            run_.last_active_niche_stats.focus_ready_count =
                run_.last_active_focus_ready_count;
            run_.last_active_niche_stats.focus_cooldown_family_count =
                run_.last_active_focus_cooldown_family_count;
            run_.last_active_niche_stats.focus_stopped_family_count =
                run_.last_active_focus_stopped_family_count;
            run_.last_active_niche_stats.focus_last_rel_improvement =
                run_.last_active_focus_rel_improvement;
            run_.last_active_niche_merit_snapshots =
                this->build_active_niche_merit_snapshots(state);
        }

int AdaptiveSession::active_niche_index_for_fitness(const CandidateFitness& candidate, const ActiveNicheGenerationState& state) {
            if (!state.configured) {
                return -1;
            }
            return this->active_family_index_for_key(
                this->fitness_diversity_key(candidate),
                state.family_keys);
        }

void AdaptiveSession::apply_active_niche_membership_change(int population_index, int new_niche_id, ActiveNicheGenerationState& state) {
            if (!state.configured ||
                population_index < 0 ||
                population_index >= run_.pop_size ||
                run_.population_niche_id.size() != static_cast<std::size_t>(run_.pop_size)) {
                return;
            }
            if (new_niche_id < 0 ||
                new_niche_id >= static_cast<int>(state.member_counts.size())) {
                new_niche_id = -1;
            }
            int& old_niche_id =
                run_.population_niche_id[static_cast<std::size_t>(population_index)];
            if (old_niche_id == new_niche_id) {
                return;
            }
            if (old_niche_id >= 0 &&
                old_niche_id < static_cast<int>(state.member_counts.size())) {
                int& old_count =
                    state.member_counts[static_cast<std::size_t>(old_niche_id)];
                old_count = std::max(0, old_count - 1);
                state.member_count = std::max(0, state.member_count - 1);
            } else {
                state.free_count = std::max(0, state.free_count - 1);
            }
            if (new_niche_id >= 0) {
                ++state.member_counts[static_cast<std::size_t>(new_niche_id)];
                ++state.member_count;
            } else {
                ++state.free_count;
            }
            old_niche_id = new_niche_id;
        }

int AdaptiveSession::release_active_niche_hard_cap_excess(ActiveNicheGenerationState& state) {
            if (!state.configured ||
                !state.survival_enabled ||
                run_.population_niche_id.size() != static_cast<std::size_t>(run_.pop_size)) {
                return 0;
            }
            const std::vector<int> protected_indices = this->active_protected_indices();
            auto target_protected = [&](int index) {
                return std::find(
                           protected_indices.begin(),
                           protected_indices.end(),
                           index) != protected_indices.end();
            };
            int release_count = 0;
            for (int niche_id = 0;
                 niche_id < static_cast<int>(state.member_counts.size());
                 ++niche_id) {
                const int cap = this->active_niche_family_cap_for(state, niche_id);
                int excess =
                    state.member_counts[static_cast<std::size_t>(niche_id)] - cap;
                if (excess <= 0) {
                    continue;
                }
                std::vector<int> members;
                members.reserve(
                    static_cast<std::size_t>(
                        std::max(0, state.member_counts[
                                           static_cast<std::size_t>(niche_id)])));
                for (int idx = 0; idx < run_.pop_size; ++idx) {
                    if (target_protected(idx) ||
                        run_.population_niche_id[static_cast<std::size_t>(idx)] !=
                            niche_id) {
                        continue;
                    }
                    members.push_back(idx);
                }
                std::stable_sort(
                    members.begin(),
                    members.end(),
                    [&](int a, int b) {
                        return candidate_better(
                            run_.fitness[static_cast<std::size_t>(b)],
                            run_.fitness[static_cast<std::size_t>(a)],
                            options);
                    });
                for (int idx : members) {
                    if (excess <= 0) {
                        break;
                    }
                    this->apply_active_niche_membership_change(idx, -1, state);
                    --excess;
                    ++release_count;
                }
            }
            run_.epoch_active_niche_hard_cap_release_count += release_count;
            return release_count;
        }

int AdaptiveSession::active_soft_fill_population(ActiveNicheGenerationState& state) {
            if ((!this->active_niche_soft_fill_enabled() &&
                 !this->active_focus_soft_fill_enabled()) ||
                !state.configured ||
                !state.survival_enabled ||
                run_.population_niche_id.size() != static_cast<std::size_t>(run_.pop_size)) {
                return 0;
            }
            
            
            
            
            auto precheck_population_matches_family =
                [&](int idx, const std::string& family_key) {
                    if (idx < 0 || idx >= run_.pop_size) {
                        return false;
                    }
                    return this->active_keys_match(
                        this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(idx)]),
                        family_key,
                        this->active_mating_allow_similarity());
                };
            bool fill_needed = false;
            bool focus_fill_needed = false;
            bool niche_fill_needed = false;
            for (int niche_id = 0;
                 niche_id < static_cast<int>(state.family_keys.size());
                 ++niche_id) {
                if (!this->family_soft_fill_enabled(state, niche_id)) {
                    continue;
                }
                const int target_quota =
                    this->family_soft_fill_target_quota(state, niche_id);
                const int member_count =
                    niche_id < static_cast<int>(state.member_counts.size())
                        ? state.member_counts[static_cast<std::size_t>(niche_id)]
                        : 0;
                bool family_needs_fill = false;
                if (member_count < target_quota) {
                    family_needs_fill = true;
                }
                const std::string& family_key =
                    state.family_keys[static_cast<std::size_t>(niche_id)];
                int matching_member_count = 0;
                for (int idx = 0; idx < run_.pop_size; ++idx) {
                    if (run_.population_niche_id[static_cast<std::size_t>(idx)] ==
                            niche_id &&
                        precheck_population_matches_family(idx, family_key)) {
                        ++matching_member_count;
                    }
                }
                if (matching_member_count < this->family_elite_seed_count(state, niche_id)) {
                    family_needs_fill = true;
                }
                if (family_needs_fill) {
                    fill_needed = true;
                    if (this->family_is_focus(state, niche_id)) {
                        focus_fill_needed = true;
                    } else {
                        niche_fill_needed = true;
                    }
                }
            }
            if (!fill_needed) {
                return 0;
            }
            
            auto candidate_soft_fill_valid = [&](const CandidateFitness& candidate) {
                if (!std::isfinite(candidate.score.merit)) {
                    return false;
                }
                if (options.basins.active_require_trace_feasible &&
                    (!candidate.trace_feasible || !candidate.score.trace_ok)) {
                    return false;
                }
                return true;
            };
            std::vector<FocusFillCandidate> candidates;
            auto add_candidate =
                [&](const std::vector<double>& scaled,
                    const CandidateFitness& candidate,
                    int source_index,
                    int source_priority) {
                    if (scaled.size() != static_cast<std::size_t>(run_.dim) ||
                        !candidate_soft_fill_valid(candidate)) {
                        return;
                    }
                    FocusFillCandidate entry;
                    entry.scaled = scaled;
                    entry.fitness = candidate;
                    entry.key = this->fitness_diversity_key(candidate);
                    entry.source_index = source_index;
                    entry.source_priority = source_priority;
                    candidates.push_back(std::move(entry));
                };
            for (const TraceableRefreshArchiveEntry& entry :
                 run_.traceable_refresh_archive) {
                add_candidate(entry.scaled_values, entry.fitness, -1, 0);
            }
            for (const TraceableRefreshArchiveEntry& entry :
                 run_.initial_traceable_seed_pool) {
                add_candidate(entry.scaled_values, entry.fitness, -1, 1);
            }
            for (const BasinElite& basin : run_.basins) {
                if (basin.active_eligible) {
                    add_candidate(basin.scaled_values, basin.fitness, -1, 2);
                }
            }
            for (int idx = 0; idx < run_.pop_size; ++idx) {
                add_candidate(
                    population_row(run_.population, idx, run_.dim),
                    run_.fitness[static_cast<std::size_t>(idx)],
                    idx,
                    3);
            }
            if (focus_fill_needed) {
                run_.epoch_active_focus_soft_fill_candidate_count +=
                    static_cast<int>(candidates.size());
            }
            if (niche_fill_needed) {
                run_.epoch_active_niche_soft_fill_candidate_count +=
                    static_cast<int>(candidates.size());
            }
            if (candidates.empty()) {
                return 0;
            }
            std::stable_sort(
                candidates.begin(),
                candidates.end(),
                [&](const FocusFillCandidate& a, const FocusFillCandidate& b) {
                    if (a.source_priority != b.source_priority) {
                        return a.source_priority < b.source_priority;
                    }
                    return candidate_better(a.fitness, b.fitness, options);
                });
            std::vector<char> used_candidate(candidates.size(), 0);
            std::vector<char> used_target(static_cast<std::size_t>(run_.pop_size), 0);
            const std::vector<int> protected_indices = this->active_protected_indices();
            
            
            
            
            
            int moves = 0;
            
    this->fill_niche_member_quotas(state, candidates, used_candidate, used_target, protected_indices, moves);

            return moves;
        }

} } // namespace optiland::optimizer_detail
