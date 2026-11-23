/*
 * Release stopped focus families and configure replacement focus niches in the adaptive population.
 * Lifecycle handling draws anchors from basins, traceable archives and the initial seed pool.
 * Keep source preference and population/niche updates synchronized when reusing a vacated slot.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

int AdaptiveSession::release_stopped_active_focus_families(const std::vector<std::string>& stopped_keys, int epoch) {
            (void)epoch;
            if (!this->active_focus_rebalance_enabled() ||
                !options.basins.active_focus_stop_release ||
                stopped_keys.empty() ||
                run_.pop_size <= 0) {
                return 0;
            }
            const double release_fraction = std::clamp(
                options.basins.active_focus_stop_release_fraction,
                0.0,
                1.0);
            if (release_fraction <= 0.0) {
                return 0;
            }

            LocalMatingPools pools = this->build_local_mating_pools();
            this->refresh_active_niche_membership(pools, false);
            ActiveNicheGenerationState state = this->make_active_niche_generation_state();
            if (!state.configured ||
                state.focus_family_count <= 0 ||
                run_.population_niche_id.size() != static_cast<std::size_t>(run_.pop_size)) {
                this->sync_active_niche_state_snapshot(state);
                return 0;
            }

            auto niche_is_focus = [&](int niche_id) {
                return niche_id >= 0 &&
                       niche_id < static_cast<int>(state.focus_family.size()) &&
                       state.focus_family[static_cast<std::size_t>(niche_id)] != 0;
            };
            const bool allow_similarity = this->active_mating_allow_similarity();
            auto key_matches_any =
                [&](const std::string& key,
                    const std::vector<std::string>& keys) {
                    if (key.empty()) {
                        return false;
                    }
                    for (const std::string& match_key : keys) {
                        if (this->active_keys_match(
                                key,
                                match_key,
                                allow_similarity)) {
                            return true;
                        }
                    }
                    return false;
                };

            int focus_deficit = 0;
            for (int niche_id = 0;
                 niche_id < static_cast<int>(state.member_counts.size());
                 ++niche_id) {
                if (!niche_is_focus(niche_id)) {
                    continue;
                }
                const int target_quota =
                    this->active_niche_target_quota_for(state, niche_id);
                const int count =
                    state.member_counts[static_cast<std::size_t>(niche_id)];
                focus_deficit += std::max(0, target_quota - count);
            }
            if (focus_deficit <= 0) {
                this->sync_active_niche_state_snapshot(state);
                return 0;
            }

            const std::vector<int> protected_indices = this->active_protected_indices();
            auto target_protected = [&](int index) {
                return std::find(
                           protected_indices.begin(),
                           protected_indices.end(),
                           index) != protected_indices.end();
            };
            std::vector<char> release_target_used(
                static_cast<std::size_t>(run_.pop_size),
                0);
            std::vector<int> release_targets;
            release_targets.reserve(static_cast<std::size_t>(focus_deficit));

            for (const std::string& stopped_key : stopped_keys) {
                if (stopped_key.empty()) {
                    continue;
                }
                int stopped_niche = -1;
                for (int niche_id = 0;
                     niche_id < static_cast<int>(state.family_keys.size());
                     ++niche_id) {
                    if (niche_is_focus(niche_id)) {
                        continue;
                    }
                    if (this->active_keys_match(
                            state.family_keys[static_cast<std::size_t>(niche_id)],
                            stopped_key,
                            allow_similarity)) {
                        stopped_niche = niche_id;
                        break;
                    }
                }
                int keep_quota = stopped_niche >= 0
                                     ? this->active_niche_target_quota_for(
                                           state,
                                           stopped_niche)
                                     : state.target_quota;
                keep_quota = std::clamp(
                    std::max(keep_quota, state.min_quota),
                    0,
                    std::max(0, run_.pop_size));
                std::vector<int> members;
                members.reserve(static_cast<std::size_t>(run_.pop_size));
                for (int idx = 0; idx < run_.pop_size; ++idx) {
                    if (release_target_used[static_cast<std::size_t>(idx)] ||
                        niche_is_focus(
                            run_.population_niche_id[static_cast<std::size_t>(idx)])) {
                        continue;
                    }
                    const std::string key =
                        this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(idx)]);
                    if (this->active_keys_match(key, stopped_key, allow_similarity)) {
                        members.push_back(idx);
                    }
                }
                if (static_cast<int>(members.size()) <= keep_quota) {
                    continue;
                }
                std::stable_sort(
                    members.begin(),
                    members.end(),
                    [&](int a, int b) {
                        return candidate_better(
                            run_.fitness[static_cast<std::size_t>(a)],
                            run_.fitness[static_cast<std::size_t>(b)],
                            options);
                    });
                int release_for_key = static_cast<int>(std::ceil(
                    release_fraction *
                    static_cast<double>(
                        static_cast<int>(members.size()) - keep_quota)));
                for (int pos = static_cast<int>(members.size()) - 1;
                     pos >= keep_quota && release_for_key > 0;
                     --pos) {
                    const int idx = members[static_cast<std::size_t>(pos)];
                    if (target_protected(idx) ||
                        release_target_used[static_cast<std::size_t>(idx)]) {
                        continue;
                    }
                    release_target_used[static_cast<std::size_t>(idx)] = 1;
                    release_targets.push_back(idx);
                    --release_for_key;
                }
            }

            if (release_targets.empty()) {
                this->sync_active_niche_state_snapshot(state);
                return 0;
            }
            std::stable_sort(
                release_targets.begin(),
                release_targets.end(),
                [&](int a, int b) {
                    return candidate_better(
                        run_.fitness[static_cast<std::size_t>(b)],
                        run_.fitness[static_cast<std::size_t>(a)],
                        options);
                });
            if (static_cast<int>(release_targets.size()) > focus_deficit) {
                release_targets.resize(static_cast<std::size_t>(focus_deficit));
            }

            
            auto candidate_valid = [&](const CandidateFitness& candidate) {
                if (!std::isfinite(candidate.score.merit)) {
                    return false;
                }
                if (options.basins.active_require_trace_feasible &&
                    (!candidate.trace_feasible || !candidate.score.trace_ok)) {
                    return false;
                }
                const std::string key = this->fitness_diversity_key(candidate);
                if (key_matches_any(key, stopped_keys)) {
                    return false;
                }
                const int niche_id =
                    this->active_niche_index_for_fitness(candidate, state);
                return niche_is_focus(niche_id);
            };

            std::vector<FocusStopReleaseCandidate> candidates;
            candidates.reserve(release_targets.size());
            auto add_candidate =
                [&](const std::vector<double>& scaled,
                    const CandidateFitness& candidate,
                    int source_index,
                    int source_priority) {
                    if (scaled.size() != static_cast<std::size_t>(run_.dim) ||
                        !candidate_valid(candidate)) {
                        return;
                    }
                    const int niche_id =
                        this->active_niche_index_for_fitness(candidate, state);
                    if (!niche_is_focus(niche_id)) {
                        return;
                    }
                    candidates.push_back(
                        {scaled,
                         candidate,
                         this->fitness_diversity_key(candidate),
                         niche_id,
                         source_index,
                         source_priority});
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

            if (static_cast<int>(candidates.size()) <
                static_cast<int>(release_targets.size())) {
                std::vector<int> focus_parent_indices;
                focus_parent_indices.reserve(static_cast<std::size_t>(run_.pop_size));
                for (int idx = 0; idx < run_.pop_size; ++idx) {
                    if (niche_is_focus(
                            run_.population_niche_id[static_cast<std::size_t>(idx)])) {
                        focus_parent_indices.push_back(idx);
                    }
                }
                if (!focus_parent_indices.empty()) {
                    const int target =
                        static_cast<int>(release_targets.size()) -
                        static_cast<int>(candidates.size());
                    PopulationGenerationOptions release_options =
                        options.refresh_generation;
                    const int max_attempts =
                        population_generation_candidate_limit(
                            release_options,
                            std::max(1, target));
                    int attempts = 0;
                    const auto release_start = Clock::now();
                    while (static_cast<int>(candidates.size()) <
                               static_cast<int>(release_targets.size()) &&
                           attempts < max_attempts &&
                           !population_generation_time_exhausted(
                               release_start,
                               release_options)) {
                        const int remaining_attempts = max_attempts - attempts;
                        const int remaining_target =
                            static_cast<int>(release_targets.size()) -
                            static_cast<int>(candidates.size());
                        const int batch = std::min(
                            remaining_attempts,
                            std::max(
                                remaining_target,
                                std::min(
                                    64,
                                    std::max(8, remaining_target))));
                        std::vector<double> generated_rows =
                            make_population_generation_rows(
                                owner.base_,
                                owner.variables_,
                                run_.population,
                                run_.dim,
                                focus_parent_indices,
                                batch,
                                release_options,
                                run_.internal_mode,
                                run_.rng,
                                run_.uni);
                        attempts += batch;
                        std::vector<CandidateFitness> generated_fitness =
                            this->evaluate_population_fitness(
                                generated_rows,
                                batch,
                                run_.total_de_generations,
                                "active_focus_stop_release_refresh_candidate");
                        for (int row = 0;
                             row < batch &&
                             static_cast<int>(candidates.size()) <
                                 static_cast<int>(release_targets.size());
                             ++row) {
                            std::vector<double> scaled =
                                population_row(generated_rows, row, run_.dim);
                            const CandidateFitness& candidate =
                                generated_fitness[static_cast<std::size_t>(row)];
                            if (this->active_plateau_fitness_matches_retired(
                                    scaled,
                                    candidate)) {
                                continue;
                            }
                            add_candidate(scaled, candidate, -1, 1);
                        }
                    }
                }
            }

            run_.epoch_active_focus_stop_release_candidate_count +=
                static_cast<int>(candidates.size());
            if (candidates.empty()) {
                this->sync_active_niche_state_snapshot(state);
                return 0;
            }

            std::stable_sort(
                candidates.begin(),
                candidates.end(),
                [&](const FocusStopReleaseCandidate& a,
                    const FocusStopReleaseCandidate& b) {
                    const int count_a =
                        a.niche_id >= 0 &&
                                a.niche_id <
                                    static_cast<int>(state.member_counts.size())
                            ? state.member_counts[
                                  static_cast<std::size_t>(a.niche_id)]
                            : run_.pop_size;
                    const int count_b =
                        b.niche_id >= 0 &&
                                b.niche_id <
                                    static_cast<int>(state.member_counts.size())
                            ? state.member_counts[
                                  static_cast<std::size_t>(b.niche_id)]
                            : run_.pop_size;
                    const bool under_target_a =
                        count_a < this->active_niche_target_quota_for(
                                      state,
                                      a.niche_id);
                    const bool under_target_b =
                        count_b < this->active_niche_target_quota_for(
                                      state,
                                      b.niche_id);
                    if (under_target_a != under_target_b) {
                        return under_target_a;
                    }
                    if (a.source_priority != b.source_priority) {
                        return a.source_priority < b.source_priority;
                    }
                    return candidate_better(a.fitness, b.fitness, options);
                });

            std::vector<char> used_candidate(candidates.size(), 0);
            int moves = 0;
            int remaining_focus_deficit = focus_deficit;
            for (int target : release_targets) {
                if (remaining_focus_deficit <= 0) {
                    break;
                }
                int selected_candidate = -1;
                for (int candidate_index = 0;
                     candidate_index < static_cast<int>(candidates.size());
                     ++candidate_index) {
                    if (used_candidate[static_cast<std::size_t>(candidate_index)]) {
                        continue;
                    }
                    const FocusStopReleaseCandidate& candidate =
                        candidates[static_cast<std::size_t>(candidate_index)];
                    if (candidate.source_index == target ||
                        !niche_is_focus(candidate.niche_id)) {
                        continue;
                    }
                    if (candidate.niche_id < 0 ||
                        candidate.niche_id >=
                            static_cast<int>(state.member_counts.size())) {
                        continue;
                    }
                    if (state.member_counts[
                            static_cast<std::size_t>(candidate.niche_id)] >=
                        this->active_niche_target_quota_for(
                            state,
                            candidate.niche_id)) {
                        continue;
                    }
                    if (state.member_counts[
                            static_cast<std::size_t>(candidate.niche_id)] >=
                        this->active_niche_family_cap_for(
                            state,
                            candidate.niche_id)) {
                        continue;
                    }
                    selected_candidate = candidate_index;
                    break;
                }
                if (selected_candidate < 0) {
                    break;
                }

                const FocusStopReleaseCandidate& candidate =
                    candidates[static_cast<std::size_t>(selected_candidate)];
                const CandidateFitness replaced =
                    run_.fitness[static_cast<std::size_t>(target)];
                const std::vector<double> replaced_scaled =
                    population_row(run_.population, target, run_.dim);
                this->stage_traceable_refresh_archive(
                    replaced_scaled,
                    replaced,
                    "active_focus_stop_release_replaced");
                if (run_.variant_uses_archive) {
                    run_.external_archive.push_back(replaced_scaled);
                    run_.external_archive_keys.push_back(
                        this->fitness_diversity_key(replaced));
                }
                set_population_row(
                    run_.population,
                    target,
                    candidate.scaled,
                    owner.variables_,
                    run_.internal_mode);
                CandidateFitness accepted = candidate.fitness;
                accepted.population_index = target;
                accepted.source = "active_focus_stop_release";
                run_.fitness[static_cast<std::size_t>(target)] = accepted;
                this->apply_active_niche_membership_change(
                    target,
                    candidate.niche_id,
                    state);
                this->consider_basin(
                    candidate.scaled,
                    run_.fitness[static_cast<std::size_t>(target)],
                    "active_focus_stop_release",
                    false);
                this->stage_traceable_refresh_archive(
                    candidate.scaled,
                    run_.fitness[static_cast<std::size_t>(target)],
                    "active_focus_stop_release");
                used_candidate[static_cast<std::size_t>(selected_candidate)] = 1;
                ++moves;
                --remaining_focus_deficit;
            }

            if (moves > 0) {
                run_.epoch_active_focus_stop_release_move_count += moves;
                this->sync_merits();
                this->trim_external_archive();
                this->update_best_from_population();
                pools = this->build_local_mating_pools();
                this->refresh_active_niche_membership(pools, false);
                state = this->make_active_niche_generation_state();
            }
            this->sync_active_niche_state_snapshot(state);
            return moves;
        }

void AdaptiveSession::configure_focus_niches(ActiveNicheGenerationState& state, const bool& focus_enabled) {
if (focus_enabled) {
            const bool focus_bridge_frozen =
                this->adaptive_glass_tolerance_focus_bridge_frozen();
            if (focus_bridge_frozen && !run_.active_focus_family_keys.empty()) {
                run_.epoch_active_focus_reset_count +=
                    static_cast<int>(run_.active_focus_family_keys.size());
                run_.active_focus_family_keys.clear();
            }
            if (!focus_bridge_frozen) {
                this->remap_active_focus_keys_to_families(state.family_keys);
            }
            const int focus_count = std::clamp(
                options.basins.active_focus_family_count,
                1,
                static_cast<int>(state.family_keys.size()));
            if (static_cast<int>(run_.active_focus_family_keys.size()) > focus_count) {
                run_.epoch_active_focus_reset_count +=
                    static_cast<int>(run_.active_focus_family_keys.size()) - focus_count;
                run_.active_focus_family_keys.resize(static_cast<std::size_t>(focus_count));
            }
            
    this->select_focus_families(state, focus_bridge_frozen, focus_count);


            const bool allow_similarity = this->active_mating_allow_similarity();
            for (const std::string& focus_key : run_.active_focus_family_keys) {
                for (std::size_t family = 0;
                     family < state.family_keys.size();
                     ++family) {
                    if (state.focus_family[family]) {
                        continue;
                    }
                    if (this->active_keys_match(
                            focus_key,
                            state.family_keys[family],
                            allow_similarity)) {
                        state.focus_family[family] = 1;
                        ++state.focus_family_count;
                        break;
                    }
                }
            }
            if (state.focus_family_count > 0) {
                const double focus_fraction = std::clamp(
                    options.basins.active_focus_population_fraction,
                    0.0,
                    1.0);
                const int focus_population = std::clamp(
                    static_cast<int>(std::ceil(
                        focus_fraction * static_cast<double>(run_.pop_size))),
                    state.focus_family_count,
                    std::max(1, run_.pop_size));
                int focus_cap = static_cast<int>(std::ceil(
                    static_cast<double>(focus_population) /
                    static_cast<double>(state.focus_family_count)));
                if (options.basins.active_focus_min_quota > 0) {
                    focus_cap = std::max(
                        focus_cap,
                        options.basins.active_focus_min_quota);
                }
                focus_cap = std::clamp(
                    std::max(focus_cap, state.target_quota),
                    1,
                    std::max(1, run_.pop_size));
                int monitor_cap =
                    options.basins.active_monitor_family_cap > 0
                        ? options.basins.active_monitor_family_cap
                        : state.target_quota;
                monitor_cap = std::clamp(
                    std::max(monitor_cap, state.min_quota),
                    1,
                    std::max(1, run_.pop_size));
                state.focus_target_quota = focus_cap;
                state.focus_family_cap = focus_cap;
                state.monitor_family_cap = monitor_cap;
                for (std::size_t family = 0;
                     family < state.family_keys.size();
                     ++family) {
                    if (state.focus_family[family]) {
                        state.family_caps[family] = focus_cap;
                        state.family_target_quotas[family] = focus_cap;
                        state.focus_member_count += state.member_counts[family];
                    } else {
                        state.family_caps[family] = monitor_cap;
                        state.family_target_quotas[family] =
                            std::min(monitor_cap, state.target_quota);
                    }
                }
            }
        }
}

} } // namespace optiland::optimizer_detail
