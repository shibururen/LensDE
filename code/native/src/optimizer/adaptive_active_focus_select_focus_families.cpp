/*
 * Choose the next focus-family set from the current adaptive population and lifecycle policy.
 * Epoch transitions update run_.active_focus_family_keys and the associated rotation statistics here.
 * Preserve merit ordering, cooldown exclusions and fallback order when filling focus quotas.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::select_focus_families(ActiveNicheGenerationState& state, const bool& focus_bridge_frozen, const int& focus_count) {
if (!focus_bridge_frozen &&
                static_cast<int>(run_.active_focus_family_keys.size()) < focus_count) {
                const bool initial_focus_selection =
                    run_.active_focus_family_keys.empty();
                std::vector<int> family_order(state.family_keys.size(), 0);
                std::iota(family_order.begin(), family_order.end(), 0);
                std::vector<double> family_best_merit(
                    state.family_keys.size(),
                    std::numeric_limits<double>::infinity());
                for (int idx = 0; idx < run_.pop_size; ++idx) {
                    const int niche_id =
                        run_.population_niche_id[static_cast<std::size_t>(idx)];
                    if (niche_id < 0 ||
                        niche_id >=
                            static_cast<int>(state.family_keys.size())) {
                        continue;
                    }
                    const double merit =
                        run_.fitness[static_cast<std::size_t>(idx)].score.merit;
                    if (std::isfinite(merit)) {
                        double& best =
                            family_best_merit[static_cast<std::size_t>(niche_id)];
                        best = std::min(best, merit);
                    }
                }
                std::stable_sort(
                    family_order.begin(),
                    family_order.end(),
                    [&](int a, int b) {
                        const bool cooldown_a =
                            this->active_focus_key_in_cooldown(
                                state.family_keys[static_cast<std::size_t>(a)]);
                        const bool cooldown_b =
                            this->active_focus_key_in_cooldown(
                                state.family_keys[static_cast<std::size_t>(b)]);
                        if (cooldown_a != cooldown_b) {
                            return !cooldown_a;
                        }
                        const double merit_a =
                            family_best_merit[static_cast<std::size_t>(a)];
                        const double merit_b =
                            family_best_merit[static_cast<std::size_t>(b)];
                        const bool finite_a = std::isfinite(merit_a);
                        const bool finite_b = std::isfinite(merit_b);
                        if (finite_a != finite_b) {
                            return finite_a;
                        }
                        if (finite_a && merit_a != merit_b) {
                            return merit_a < merit_b;
                        }
                        const int count_a =
                            state.member_counts[static_cast<std::size_t>(a)];
                        const int count_b =
                            state.member_counts[static_cast<std::size_t>(b)];
                        if (count_a != count_b) {
                            return count_a > count_b;
                        }
                        return a < b;
                    });
                auto select_focus_family =
                    [&](const std::string& key, bool allow_rotation_fallback) {
                        if (this->active_focus_key_in_cooldown(key)) {
                            return false;
                        }
                        if (this->active_focus_key_already_selected(
                                run_.active_focus_family_keys,
                                key)) {
                            return false;
                        }
                        const bool rotation_blocked =
                            this->active_focus_key_recently_rotated(key);
                        if (rotation_blocked && !allow_rotation_fallback) {
                            ++run_.epoch_active_focus_rotation_blocked_count;
                            return false;
                        }
                        run_.active_focus_family_keys.push_back(key);
                        ++run_.epoch_active_focus_selection_count;
                        if (rotation_blocked) {
                            ++run_.epoch_active_focus_rotation_fallback_count;
                        }
                        return true;
                    };
                if (initial_focus_selection &&
                    options.basins.active_focus_initial_best_family) {
                    int best_family = -1;
                    if (run_.best_idx >= 0 &&
                        run_.best_idx < run_.pop_size &&
                        run_.best_idx < static_cast<int>(run_.population_niche_id.size())) {
                        best_family =
                            run_.population_niche_id[static_cast<std::size_t>(run_.best_idx)];
                    }
                    if (best_family < 0 ||
                        best_family >=
                            static_cast<int>(state.family_keys.size())) {
                        const std::string best_key =
                            this->fitness_diversity_key(run_.best_fitness);
                        best_family =
                            this->active_family_index_for_key(best_key, state.family_keys);
                    }
                    if (best_family >= 0 &&
                        best_family < static_cast<int>(state.family_keys.size())) {
                        const std::string& key =
                            state.family_keys[static_cast<std::size_t>(best_family)];
                        select_focus_family(key, true);
                    }
                }
                for (int rank = 0;
                     rank < static_cast<int>(family_order.size()) &&
                     static_cast<int>(run_.active_focus_family_keys.size()) < focus_count;
                     ++rank) {
                    const int family =
                        family_order[static_cast<std::size_t>(rank)];
                    const std::string& key =
                        state.family_keys[static_cast<std::size_t>(family)];
                    select_focus_family(key, false);
                }
                for (int rank = 0;
                     rank < static_cast<int>(family_order.size()) &&
                     static_cast<int>(run_.active_focus_family_keys.size()) < focus_count;
                     ++rank) {
                    const int family =
                        family_order[static_cast<std::size_t>(rank)];
                    const std::string& key =
                        state.family_keys[static_cast<std::size_t>(family)];
                    select_focus_family(key, true);
                }
            }
}

} } // namespace optiland::optimizer_detail
