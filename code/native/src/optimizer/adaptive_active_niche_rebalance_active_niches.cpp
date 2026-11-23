/*
 * Rebalance underfilled active niches and resolve family-level soft-fill limits.
 * Adaptive epoch/selection phases reuse basin and traceable-seed sources through the same run state.
 * Preserve source precedence and family caps when assigning replacements to population slots.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::rebalance_active_niches(int& gen, ActiveNicheGenerationState& active_niche_state, int& active_niche_rebalance_candidates, int& active_niche_rebalance_moves) {
if (active_niche_state.survival_enabled &&
                active_niche_state.configured &&
                (options.basins.active_niche_rebalance ||
                 this->active_focus_rebalance_enabled()) &&
                options.basins.active_niche_rebalance_fraction > 0.0 &&
                options.basins.active_niche_rebalance_interval > 0 &&
                gen % std::max(1, options.basins.active_niche_rebalance_interval) == 0 &&
                run_.population_niche_id.size() == static_cast<std::size_t>(run_.pop_size)) {
                
                const int max_moves = std::clamp(
                    static_cast<int>(std::ceil(
                        std::clamp(
                            options.basins.active_niche_rebalance_fraction,
                            0.0,
                            1.0) *
                        static_cast<double>(run_.pop_size))),
                    1,
                    run_.pop_size);
                const std::vector<int> protected_indices =
                    this->active_protected_indices();
                auto target_protected = [&](int index) {
                    return std::find(
                               protected_indices.begin(),
                               protected_indices.end(),
                               index) != protected_indices.end();
                };
                auto niche_is_focus = [&](int niche_id) {
                    return niche_id >= 0 &&
                           niche_id <
                               static_cast<int>(active_niche_state.focus_family.size()) &&
                           active_niche_state.focus_family[
                               static_cast<std::size_t>(niche_id)] != 0;
                };
                int active_focus_quota_deficit = 0;
                if (this->active_focus_rebalance_enabled()) {
                    for (int niche_id = 0;
                         niche_id <
                         static_cast<int>(active_niche_state.member_counts.size());
                         ++niche_id) {
                        if (!niche_is_focus(niche_id)) {
                            continue;
                        }
                        const int target_quota =
                            this->active_niche_target_quota_for(
                                active_niche_state,
                                niche_id);
                        const int count =
                            active_niche_state.member_counts[
                                static_cast<std::size_t>(niche_id)];
                        if (count < target_quota) {
                            active_focus_quota_deficit += target_quota - count;
                        }
                    }
                }
                std::vector<int> rebalance_targets;
                rebalance_targets.reserve(static_cast<std::size_t>(run_.pop_size));
                for (int idx = 0; idx < run_.pop_size; ++idx) {
                    if (target_protected(idx)) {
                        continue;
                    }
                    const int niche_id =
                        run_.population_niche_id[static_cast<std::size_t>(idx)];
                    if (niche_id < 0 ||
                        niche_id >=
                            static_cast<int>(
                                active_niche_state.member_counts.size())) {
                        if (active_focus_quota_deficit > 0) {
                            rebalance_targets.push_back(idx);
                        }
                        continue;
                    }
                    if (active_niche_state.member_counts[
                            static_cast<std::size_t>(niche_id)] <=
                        this->active_niche_family_cap_for(
                            active_niche_state,
                            niche_id)) {
                        continue;
                    }
                    rebalance_targets.push_back(idx);
                }
                active_niche_rebalance_candidates =
                    static_cast<int>(rebalance_targets.size());
                std::stable_sort(
                    rebalance_targets.begin(),
                    rebalance_targets.end(),
                    [&](int a, int b) {
                        const int niche_a =
                            run_.population_niche_id[static_cast<std::size_t>(a)];
                        const int niche_b =
                            run_.population_niche_id[static_cast<std::size_t>(b)];
                        const int over_a =
                            niche_a >= 0 &&
                                    niche_a <
                                        static_cast<int>(
                                            active_niche_state.member_counts.size())
                                ? active_niche_state.member_counts[
                                      static_cast<std::size_t>(niche_a)] -
                                      this->active_niche_family_cap_for(
                                          active_niche_state,
                                          niche_a)
                                : 0;
                        const int over_b =
                            niche_b >= 0 &&
                                    niche_b <
                                        static_cast<int>(
                                            active_niche_state.member_counts.size())
                                ? active_niche_state.member_counts[
                                      static_cast<std::size_t>(niche_b)] -
                                      this->active_niche_family_cap_for(
                                          active_niche_state,
                                          niche_b)
                                : 0;
                        if (over_a != over_b) {
                            return over_a > over_b;
                        }
                        return candidate_better(
                            run_.fitness[static_cast<std::size_t>(b)],
                            run_.fitness[static_cast<std::size_t>(a)],
                            options);
                    });
                std::vector<ActiveNicheRebalanceCandidate> rebalance_pool;
                auto rebalance_candidate_quality_ok =
                    [&](const CandidateFitness& candidate) {
                        if (!candidate.trace_feasible ||
                            !candidate.score.trace_ok ||
                            !std::isfinite(candidate.score.merit)) {
                            return false;
                        }
                        if (!options.basins.active_quality_gate) {
                            return true;
                        }
                        const double min_edge = options.basins.active_min_edge;
                        if (std::isfinite(min_edge) &&
                            (!std::isfinite(candidate.score.min_edge) ||
                             candidate.score.min_edge < min_edge)) {
                            return false;
                        }
                        const double merit_ceiling = this->active_basin_merit_ceiling();
                        if (std::isfinite(merit_ceiling) &&
                            candidate.score.merit > merit_ceiling) {
                            return false;
                        }
                        return true;
                    };
                auto add_rebalance_candidate =
                    [&](const std::vector<double>& scaled,
                        const CandidateFitness& candidate,
                        int source_index = -1) {
                        if (scaled.size() != static_cast<std::size_t>(run_.dim) ||
                            !rebalance_candidate_quality_ok(candidate)) {
                            return;
                        }
                        const int niche_id =
                            this->active_niche_index_for_fitness(
                                candidate,
                                active_niche_state);
                        if (niche_id < 0 ||
                            niche_id >=
                                static_cast<int>(
                                    active_niche_state.member_counts.size())) {
                            return;
                        }
                        if (active_niche_state.member_counts[
                                static_cast<std::size_t>(niche_id)] >=
                            this->active_niche_family_cap_for(
                                active_niche_state,
                                niche_id)) {
                            return;
                        }
                        rebalance_pool.push_back(
                            {scaled, candidate, niche_id, source_index});
                    };
                for (const BasinElite& basin : run_.basins) {
                    if (!basin.active_eligible) {
                        continue;
                    }
                    add_rebalance_candidate(basin.scaled_values, basin.fitness);
                }
                for (const TraceableRefreshArchiveEntry& entry :
                     run_.traceable_refresh_archive) {
                    add_rebalance_candidate(entry.scaled_values, entry.fitness);
                }
                for (const TraceableRefreshArchiveEntry& entry :
                     run_.initial_traceable_seed_pool) {
                    add_rebalance_candidate(entry.scaled_values, entry.fitness);
                }
                if (this->active_focus_rebalance_enabled() &&
                    active_focus_quota_deficit > 0) {
                    for (int idx = 0; idx < run_.pop_size; ++idx) {
                        const int niche_id =
                            run_.population_niche_id[static_cast<std::size_t>(idx)];
                        if (!niche_is_focus(niche_id)) {
                            continue;
                        }
                        add_rebalance_candidate(
                            population_row(run_.population, idx, run_.dim),
                            run_.fitness[static_cast<std::size_t>(idx)],
                            idx);
                    }
                }
                std::stable_sort(
                    rebalance_pool.begin(),
                    rebalance_pool.end(),
                    [&](const ActiveNicheRebalanceCandidate& a,
                        const ActiveNicheRebalanceCandidate& b) {
                        const int count_a =
                            active_niche_state.member_counts[
                                static_cast<std::size_t>(a.niche_id)];
                        const int count_b =
                            active_niche_state.member_counts[
                                static_cast<std::size_t>(b.niche_id)];
                        const bool under_target_a =
                            count_a <
                            this->active_niche_target_quota_for(
                                active_niche_state,
                                a.niche_id);
                        const bool under_target_b =
                            count_b <
                            this->active_niche_target_quota_for(
                                active_niche_state,
                                b.niche_id);
                        if (under_target_a != under_target_b) {
                            return under_target_a;
                        }
                        return candidate_better(a.fitness, b.fitness, options);
                    });
                std::vector<bool> used_rebalance_candidate(
                    rebalance_pool.size(),
                    false);
                for (int target : rebalance_targets) {
                    if (active_niche_rebalance_moves >= max_moves) {
                        break;
                    }
                    const int old_niche =
                        run_.population_niche_id[static_cast<std::size_t>(target)];
                    const bool target_is_free = old_niche < 0;
                    if (target_is_free) {
                        if (active_focus_quota_deficit <= 0) {
                            continue;
                        }
                    } else if (
                        old_niche >=
                            static_cast<int>(
                                active_niche_state.member_counts.size()) ||
                        active_niche_state.member_counts[
                            static_cast<std::size_t>(old_niche)] <=
                            this->active_niche_family_cap_for(
                                active_niche_state,
                                old_niche)) {
                        continue;
                    }
                    int selected_candidate = -1;
                    for (int candidate_index = 0;
                         candidate_index <
                         static_cast<int>(rebalance_pool.size());
                         ++candidate_index) {
                        if (used_rebalance_candidate[
                                static_cast<std::size_t>(candidate_index)]) {
                            continue;
                        }
                        const ActiveNicheRebalanceCandidate& candidate =
                            rebalance_pool[
                                static_cast<std::size_t>(candidate_index)];
                        if (candidate.source_index == target) {
                            continue;
                        }
                        if (candidate.niche_id == old_niche) {
                            continue;
                        }
                        if (candidate.niche_id < 0 ||
                            candidate.niche_id >=
                                static_cast<int>(
                                    active_niche_state.member_counts.size())) {
                            continue;
                        }
                        if (active_niche_state.member_counts[
                                static_cast<std::size_t>(candidate.niche_id)] >=
                            this->active_niche_family_cap_for(
                                active_niche_state,
                                candidate.niche_id)) {
                            continue;
                        }
                        selected_candidate = candidate_index;
                        break;
                    }
                    if (selected_candidate < 0) {
                        continue;
                    }
                    ActiveNicheRebalanceCandidate& candidate =
                        rebalance_pool[
                            static_cast<std::size_t>(selected_candidate)];
                    const CandidateFitness replaced =
                        run_.fitness[static_cast<std::size_t>(target)];
                    const std::vector<double> replaced_scaled =
                        population_row(run_.population, target, run_.dim);
                    this->stage_traceable_refresh_archive(
                        replaced_scaled,
                        replaced,
                        "niche_rebalance_replaced");
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
                    run_.fitness[static_cast<std::size_t>(target)] =
                        candidate.fitness;
                    run_.fitness[static_cast<std::size_t>(target)].population_index =
                        target;
                    this->apply_active_niche_membership_change(
                        target,
                        candidate.niche_id,
                        active_niche_state);
                    if (niche_is_focus(candidate.niche_id) &&
                        active_focus_quota_deficit > 0) {
                        --active_focus_quota_deficit;
                    }
                    this->consider_basin(
                        candidate.scaled,
                        run_.fitness[static_cast<std::size_t>(target)],
                        "niche_rebalance",
                        false);
                    this->stage_traceable_refresh_archive(
                        candidate.scaled,
                        run_.fitness[static_cast<std::size_t>(target)],
                        "niche_rebalance_accepted");
                    used_rebalance_candidate[
                        static_cast<std::size_t>(selected_candidate)] = true;
                    ++active_niche_rebalance_moves;
                }
            }
}

bool AdaptiveSession::family_soft_fill_enabled(ActiveNicheGenerationState& state, int niche_id) {
                return this->family_is_focus(state, niche_id)
                           ? this->active_focus_soft_fill_enabled()
                           : this->active_niche_soft_fill_enabled();
            }

int AdaptiveSession::family_soft_fill_target_quota(ActiveNicheGenerationState& state, int niche_id) {
                const int target_quota =
                    std::min(
                        this->active_niche_target_quota_for(state, niche_id),
                        this->active_niche_family_cap_for(state, niche_id));
                if (this->family_is_focus(state, niche_id)) {
                    return target_quota;
                }
                const int ordinary_floor =
                    state.min_quota > 0 ? state.min_quota : state.target_quota;
                return std::min(ordinary_floor, target_quota);
            }

} } // namespace optiland::optimizer_detail
