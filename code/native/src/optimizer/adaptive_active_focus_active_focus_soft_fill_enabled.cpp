/*
 * Resolve focus quotas, cooldowns and family-rotation policy for AdaptiveSession.
 * Focus selection shares run_.active_focus_lifecycle, family keys and rotation history across epochs.
 * Preserve history pruning, key remapping and readiness checks so families retain their intended state.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

bool AdaptiveSession::active_focus_soft_fill_enabled() {
        return options.basins.active_focus_rebalance &&
               options.basins.active_focus_soft_fill;
    }

int AdaptiveSession::adaptive_glass_tolerance_focus_clear_target() {
        const int configured =
            std::max(0, options.adaptive_glass_tolerance_focus_clear_target);
        if (configured > 0) {
            return configured;
        }
        const int focus_stop_target =
            std::max(0, options.basins.active_focus_stop_after_clears);
        if (focus_stop_target > 0) {
            return focus_stop_target;
        }
        return 1;
    }

bool AdaptiveSession::adaptive_glass_tolerance_focus_bridge_frozen() {
        return options.adaptive_glass_tolerance_bridge_freeze_focus &&
               this->adaptive_glass_tolerance_bridge_active();
    }

bool AdaptiveSession::active_focus_key_matches_current(const std::string& key) {
        if (!options.basins.active_focus_rebalance || key.empty()) {
            return false;
        }
        const bool allow_similarity = this->active_mating_allow_similarity();
        for (const std::string& focus_key : run_.active_focus_family_keys) {
            if (!focus_key.empty() &&
                this->active_keys_match(key, focus_key, allow_similarity)) {
                return true;
            }
        }
        return false;
    }

bool AdaptiveSession::active_focus_key_in_cooldown(const std::string& key) {
        if (!options.basins.active_focus_rebalance || key.empty()) {
            return false;
        }
        const bool allow_similarity = this->active_mating_allow_similarity();
        for (const auto& [focus_key, state] : run_.active_focus_lifecycle) {
            if (run_.current_outer_epoch <= 0 ||
                state.cooldown_until_epoch < run_.current_outer_epoch) {
                continue;
            }
            if (this->active_keys_match(key, focus_key, allow_similarity)) {
                return true;
            }
        }
        return false;
    }

int AdaptiveSession::count_active_focus_cooldown_families() {
        if (!options.basins.active_focus_rebalance || run_.active_focus_lifecycle.empty()) {
            return 0;
        }
        int count = 0;
        for (const auto& [key, state] : run_.active_focus_lifecycle) {
            if (key.empty() ||
                run_.current_outer_epoch <= 0 ||
                state.cooldown_until_epoch < run_.current_outer_epoch) {
                continue;
            }
            ++count;
        }
        return count;
    }

bool AdaptiveSession::active_focus_rotation_enabled() {
        return options.basins.active_focus_rebalance &&
               options.basins.active_focus_rotation &&
               options.basins.active_focus_rotation_history_epochs > 0;
    }

double AdaptiveSession::active_focus_rotation_similarity_threshold() {
        return std::clamp(
            options.basins.active_focus_rotation_similarity_threshold,
            0.0,
            1.0);
    }

bool AdaptiveSession::active_focus_rotation_keys_match(const std::string& left, const std::string& right) {
        if (left.empty() || right.empty()) {
            return false;
        }
        if (left == right) {
            return true;
        }
        if (!run_.topology_similarity_enabled) {
            return false;
        }
        const double similarity =
            run_.topology_family_similarity_enabled
                ? weighted_structural_family_similarity(left, right)
                : weighted_structural_topology_similarity(left, right);
        return std::isfinite(similarity) &&
               similarity >= this->active_focus_rotation_similarity_threshold() &&
               this->active_family_shape_diff_ok(left, right);
    }

void AdaptiveSession::prune_active_focus_rotation_history() {
        if (!this->active_focus_rotation_enabled()) {
            run_.active_focus_rotation_history.clear();
            return;
        }
        const int history_epochs =
            std::max(0, options.basins.active_focus_rotation_history_epochs);
        if (run_.current_outer_epoch > 0 && history_epochs > 0) {
            const int cutoff_epoch = run_.current_outer_epoch - history_epochs;
            run_.active_focus_rotation_history.erase(
                std::remove_if(
                    run_.active_focus_rotation_history.begin(),
                    run_.active_focus_rotation_history.end(),
                    [&](const ActiveFocusRotationEntry& entry) {
                        return entry.last_epoch < cutoff_epoch;
                    }),
                run_.active_focus_rotation_history.end());
        }
        const int max_history = std::max(
            16,
            std::max(1, options.basins.active_focus_family_count) *
                std::max(1, history_epochs) * 2);
        if (static_cast<int>(run_.active_focus_rotation_history.size()) > max_history) {
            run_.active_focus_rotation_history.erase(
                run_.active_focus_rotation_history.begin(),
                run_.active_focus_rotation_history.begin() +
                    (static_cast<int>(run_.active_focus_rotation_history.size()) -
                     max_history));
        }
    }

bool AdaptiveSession::active_focus_key_recently_rotated(const std::string& key) {
        if (!this->active_focus_rotation_enabled() || key.empty()) {
            return false;
        }
        this->prune_active_focus_rotation_history();
        for (const ActiveFocusRotationEntry& entry : run_.active_focus_rotation_history) {
            if (this->active_focus_rotation_keys_match(key, entry.key)) {
                return true;
            }
        }
        return false;
    }

void AdaptiveSession::remember_active_focus_rotation_key(const std::string& key, int epoch) {
            if (!this->active_focus_rotation_enabled() || key.empty()) {
                return;
            }
            this->prune_active_focus_rotation_history();
            for (ActiveFocusRotationEntry& entry : run_.active_focus_rotation_history) {
                if (this->active_focus_rotation_keys_match(key, entry.key)) {
                    entry.key = key;
                    entry.last_epoch = epoch;
                    return;
                }
            }
            run_.active_focus_rotation_history.push_back({key, epoch});
            this->prune_active_focus_rotation_history();
        }

bool AdaptiveSession::active_focus_epoch_enabled() {
        const int start_epoch =
            std::max(1, options.basins.active_focus_start_epoch);
        return this->active_niche_epoch_enabled() &&
               (run_.current_outer_epoch <= 0 || run_.current_outer_epoch >= start_epoch);
    }

bool AdaptiveSession::active_focus_rebalance_enabled() {
        return options.basins.active_focus_rebalance &&
               this->active_focus_epoch_enabled();
    }

bool AdaptiveSession::active_focus_key_already_selected(const std::vector<std::string>& keys, const std::string& candidate) {
            const bool allow_similarity = this->active_mating_allow_similarity();
            for (const std::string& existing : keys) {
                if (this->active_keys_match(candidate, existing, allow_similarity)) {
                    return true;
                }
            }
            return false;
        }

void AdaptiveSession::merge_active_focus_lifecycle_state(ActiveFocusLifecycleState& target, const ActiveFocusLifecycleState& source) {
            const int cooldown_until_epoch =
                std::max(target.cooldown_until_epoch, source.cooldown_until_epoch);
            const int last_seen_epoch =
                std::max(target.last_seen_epoch, source.last_seen_epoch);
            if (source.ready &&
                (!target.ready ||
                 source.ready_epoch <= target.ready_epoch ||
                 !std::isfinite(target.baseline_merit))) {
                target = source;
            } else if (!target.ready &&
                       std::isfinite(source.baseline_merit) &&
                       !std::isfinite(target.baseline_merit)) {
                target.baseline_merit = source.baseline_merit;
                target.best_merit = source.best_merit;
                target.last_rel_improvement = source.last_rel_improvement;
                target.ready_epoch = source.ready_epoch;
            }
            target.cooldown_until_epoch =
                std::max(target.cooldown_until_epoch, cooldown_until_epoch);
            target.last_seen_epoch =
                std::max(target.last_seen_epoch, last_seen_epoch);
        }

void AdaptiveSession::remap_active_focus_lifecycle_state(const std::string& old_key, const std::string& new_key) {
            if (old_key.empty() || new_key.empty() || old_key == new_key) {
                return;
            }
            const auto old_it = run_.active_focus_lifecycle.find(old_key);
            if (old_it == run_.active_focus_lifecycle.end()) {
                return;
            }
            const ActiveFocusLifecycleState old_state = old_it->second;
            run_.active_focus_lifecycle.erase(old_it);
            const auto [new_it, inserted] =
                run_.active_focus_lifecycle.emplace(new_key, old_state);
            if (!inserted) {
                this->merge_active_focus_lifecycle_state(new_it->second, old_state);
            }
        }

void AdaptiveSession::remap_active_focus_keys_to_families(const std::vector<std::string>& family_keys) {
            if (!this->active_focus_rebalance_enabled() ||
                run_.active_focus_family_keys.empty()) {
                return;
            }
            std::vector<std::string> remapped_focus_keys;
            remapped_focus_keys.reserve(run_.active_focus_family_keys.size());
            int remap_count = 0;
            int reset_count = 0;
            for (const std::string& old_focus_key : run_.active_focus_family_keys) {
                const std::string new_focus_key =
                    this->find_matching_active_family_key(old_focus_key, family_keys);
                if (new_focus_key.empty()) {
                    ++reset_count;
                    continue;
                }
                if (this->active_focus_key_in_cooldown(new_focus_key)) {
                    this->remap_active_focus_lifecycle_state(
                        old_focus_key,
                        new_focus_key);
                    if (old_focus_key != new_focus_key) {
                        ++remap_count;
                    }
                    ++reset_count;
                    continue;
                }
                if (this->active_focus_key_already_selected(
                        remapped_focus_keys,
                        new_focus_key)) {
                    this->remap_active_focus_lifecycle_state(
                        old_focus_key,
                        new_focus_key);
                    if (old_focus_key != new_focus_key) {
                        ++remap_count;
                    }
                    continue;
                }
                this->remap_active_focus_lifecycle_state(old_focus_key, new_focus_key);
                if (old_focus_key != new_focus_key) {
                    ++remap_count;
                }
                remapped_focus_keys.push_back(new_focus_key);
            }
            if (remap_count > 0 || reset_count > 0) {
                run_.epoch_active_focus_remap_count += remap_count;
                run_.epoch_active_focus_reset_count += reset_count;
            }
            run_.active_focus_family_keys = std::move(remapped_focus_keys);
        }

int AdaptiveSession::resolved_active_focus_target_quota() {
        const int focus_count =
            std::max(1, static_cast<int>(run_.active_focus_family_keys.size()));
        const double focus_fraction = std::clamp(
            options.basins.active_focus_population_fraction,
            0.0,
            1.0);
        const int focus_population = std::clamp(
            static_cast<int>(std::ceil(
                focus_fraction * static_cast<double>(run_.pop_size))),
            focus_count,
            std::max(1, run_.pop_size));
        int target = static_cast<int>(std::ceil(
            static_cast<double>(focus_population) /
            static_cast<double>(focus_count)));
        if (options.basins.active_focus_min_quota > 0) {
            target = std::max(target, options.basins.active_focus_min_quota);
        }
        return std::clamp(target, 1, std::max(1, run_.pop_size));
    }

std::pair<int, double> AdaptiveSession::active_focus_family_best_and_count(const std::string& focus_key) {
            int count = 0;
            double best_merit = std::numeric_limits<double>::infinity();
            if (focus_key.empty()) {
                return std::pair<int, double>{count, best_merit};
            }
            const bool allow_similarity = this->active_mating_allow_similarity();
            if (this->active_focus_soft_fill_enabled() &&
                run_.population_niche_id.size() == static_cast<std::size_t>(run_.pop_size) &&
                !run_.active_niche_family_keys.empty()) {
                std::vector<int> matching_focus_niches;
                for (int family = 0;
                     family < static_cast<int>(run_.active_niche_family_keys.size());
                     ++family) {
                    if (this->active_keys_match(
                            run_.active_niche_family_keys[static_cast<std::size_t>(family)],
                            focus_key,
                            allow_similarity)) {
                        matching_focus_niches.push_back(family);
                    }
                }
                if (!matching_focus_niches.empty()) {
                    for (int idx = 0; idx < run_.pop_size; ++idx) {
                        const int niche_id =
                            run_.population_niche_id[static_cast<std::size_t>(idx)];
                        if (std::find(
                                matching_focus_niches.begin(),
                                matching_focus_niches.end(),
                                niche_id) == matching_focus_niches.end()) {
                            continue;
                        }
                        ++count;
                        const double merit =
                            run_.fitness[static_cast<std::size_t>(idx)].score.merit;
                        if (std::isfinite(merit)) {
                            best_merit = std::min(best_merit, merit);
                        }
                    }
                    if (count > 0) {
                        return std::pair<int, double>{count, best_merit};
                    }
                }
            }
            for (int idx = 0; idx < run_.pop_size; ++idx) {
                const std::string key =
                    this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(idx)]);
                if (key.empty() ||
                    !this->active_keys_match(key, focus_key, allow_similarity)) {
                    continue;
                }
                ++count;
                const double merit =
                    run_.fitness[static_cast<std::size_t>(idx)].score.merit;
                if (std::isfinite(merit)) {
                    best_merit = std::min(best_merit, merit);
                }
            }
            return std::pair<int, double>{count, best_merit};
        }

void AdaptiveSession::update_active_focus_lifecycle(int epoch) {
        run_.last_active_focus_ready_count = 0;
        run_.last_active_focus_cooldown_family_count =
            this->count_active_focus_cooldown_families();
        run_.last_active_focus_stopped_family_count = 0;
        run_.last_active_focus_rel_improvement =
            std::numeric_limits<double>::quiet_NaN();
        auto sync_active_focus_published_stats = [&]() {
            run_.last_active_niche_stats.focus_family_count =
                static_cast<int>(run_.active_focus_family_keys.size());
            run_.last_active_niche_stats.focus_member_count = 0;
            for (const std::string& focus_key : run_.active_focus_family_keys) {
                run_.last_active_niche_stats.focus_member_count +=
                    this->active_focus_family_best_and_count(focus_key).first;
            }
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
        };
        if (!this->active_focus_rebalance_enabled() ||
            run_.active_focus_family_keys.empty()) {
            if (!this->active_focus_rebalance_enabled()) {
                run_.active_focus_lifecycle.clear();
                run_.active_focus_rotation_history.clear();
                run_.active_focus_stop_clear_count = 0;
            }
            sync_active_focus_published_stats();
            return;
        }
        const int focus_target = this->resolved_active_focus_target_quota();
        const double ready_fraction = std::clamp(
            options.basins.active_focus_ready_fraction,
            0.0,
            1.0);
        const int ready_count = std::clamp(
            static_cast<int>(std::ceil(
                ready_fraction * static_cast<double>(focus_target))),
            1,
            std::max(1, focus_target));
        const double stop_threshold = std::max(
            0.0,
            options.basins.active_focus_stop_rel_improvement);
        const int cooldown_epochs = std::max(
            0,
            options.basins.active_focus_cooldown_epochs);
        std::vector<std::string> cooldown_keys;
        std::vector<std::string> stopped_keys;
        cooldown_keys.reserve(run_.active_focus_family_keys.size());
        stopped_keys.reserve(run_.active_focus_family_keys.size());
        for (const std::string& focus_key : run_.active_focus_family_keys) {
            const auto [member_count, best_merit] =
                this->active_focus_family_best_and_count(focus_key);
            if (this->active_focus_key_in_cooldown(focus_key)) {
                cooldown_keys.push_back(focus_key);
                continue;
            }
            ActiveFocusLifecycleState& state =
                run_.active_focus_lifecycle[focus_key];
            state.last_seen_epoch = epoch;
            if (member_count < ready_count || !std::isfinite(best_merit)) {
                state.ready = false;
                if (std::isfinite(best_merit)) {
                    state.baseline_merit = best_merit;
                    state.best_merit = best_merit;
                }
                continue;
            }
            ++run_.last_active_focus_ready_count;
            if (!state.ready || !std::isfinite(state.baseline_merit)) {
                state.ready = true;
                state.ready_epoch = epoch;
                state.baseline_merit = best_merit;
                state.best_merit = best_merit;
                state.last_rel_improvement =
                    std::numeric_limits<double>::quiet_NaN();
                continue;
            }
            if (epoch <= state.ready_epoch) {
                state.best_merit = best_merit;
                continue;
            }
            const double denom =
                std::max(1.0e-12, std::abs(state.baseline_merit));
            double rel_improvement =
                (state.baseline_merit - best_merit) / denom;
            if (!std::isfinite(rel_improvement)) {
                rel_improvement = 0.0;
            }
            state.last_rel_improvement = rel_improvement;
            run_.last_active_focus_rel_improvement = rel_improvement;
            if (rel_improvement + 1.0e-12 < stop_threshold) {
                if (cooldown_epochs > 0) {
                    state.cooldown_until_epoch = std::max(
                        state.cooldown_until_epoch,
                        epoch + cooldown_epochs);
                }
                state.ready = false;
                state.ready_epoch = 0;
                state.baseline_merit = best_merit;
                state.best_merit = best_merit;
                stopped_keys.push_back(focus_key);
                continue;
            }
            state.baseline_merit = best_merit;
            state.best_merit = best_merit;
            state.ready_epoch = epoch;
        }
        auto erase_active_focus_keys_matching =
            [&](const std::vector<std::string>& keys) {
                if (keys.empty()) {
                    return 0;
                }
                const bool allow_similarity = this->active_mating_allow_similarity();
                int removed_count = 0;
                run_.active_focus_family_keys.erase(
                    std::remove_if(
                        run_.active_focus_family_keys.begin(),
                        run_.active_focus_family_keys.end(),
                        [&](const std::string& focus_key) {
                            for (const std::string& key : keys) {
                                if (this->active_keys_match(
                                        focus_key,
                                        key,
                                        allow_similarity)) {
                                    ++removed_count;
                                    return true;
                                }
                            }
                            return false;
                        }),
                    run_.active_focus_family_keys.end());
                return removed_count;
            };
        if (!cooldown_keys.empty()) {
            run_.epoch_active_focus_reset_count +=
                erase_active_focus_keys_matching(cooldown_keys);
        }
        if (!stopped_keys.empty()) {
            for (const std::string& stopped_key : stopped_keys) {
                this->remember_active_focus_rotation_key(stopped_key, epoch);
            }
            erase_active_focus_keys_matching(stopped_keys);
            run_.last_active_focus_stopped_family_count =
                static_cast<int>(stopped_keys.size());
            run_.active_focus_stop_clear_count +=
                static_cast<int>(stopped_keys.size());
            if (run_.release_stopped_active_focus_ready) {
                this->release_stopped_active_focus_families(stopped_keys, epoch);
            }
        }
        run_.last_active_focus_cooldown_family_count =
            this->count_active_focus_cooldown_families();
        for (auto it = run_.active_focus_lifecycle.begin();
             it != run_.active_focus_lifecycle.end();) {
            if (epoch - it->second.last_seen_epoch >
                std::max(8, options.basins.active_focus_cooldown_epochs * 4)) {
                it = run_.active_focus_lifecycle.erase(it);
            } else {
                ++it;
            }
        }
        sync_active_focus_published_stats();
    }

} } // namespace optiland::optimizer_detail
