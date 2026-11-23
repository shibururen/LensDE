/*
 * Track retired plateau families and answer cooldown, stalled-family and retired-row queries.
 * Adaptive mating and refill share the lifecycle caches and retained parameter anchors in run_.
 * Invalidate match caches at the existing lifecycle boundaries and preserve retirement-match thresholds.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

int AdaptiveSession::active_plateau_retired_unique_count() {
        return std::max(
            static_cast<int>(run_.active_plateau_retired_filter_keys.size()),
            static_cast<int>(run_.active_plateau_retired_filter_scaled_rows.size()));
    }

void AdaptiveSession::refresh_active_plateau_retire_clear_count() {
        run_.active_plateau_retire_clear_count =
            this->active_plateau_retired_unique_count();
    }

void AdaptiveSession::clear_active_lifecycle_match_caches() {
        run_.active_lifecycle_mating_stalled_cache.clear();
        run_.active_lifecycle_mating_suppressed_cache.clear();
    }

bool AdaptiveSession::active_plateau_lifecycle_enabled() {
        return options.basins.active_plateau_lifecycle &&
               options.basins.active_plateau_window > 0;
    }

std::string AdaptiveSession::active_lifecycle_key_for_basin(const BasinElite& basin) {
        std::string key = this->active_current_family_key_for_basin(basin);
        if (key.empty()) {
            key = this->basin_key(basin);
        }
        if (key.empty()) {
            key = "__unknown__";
        }
        return key;
    }

double AdaptiveSession::active_plateau_retire_match_distance() {
        double distance = options.basins.active_plateau_retire_evict_parameter_distance;
        if (!std::isfinite(distance) || distance <= 0.0) {
            distance = std::max(0.0, options.basins.radius);
        }
        return distance;
    }

bool AdaptiveSession::active_plateau_retire_filter_enabled() {
        return this->active_plateau_lifecycle_enabled() &&
               options.basins.active_plateau_retire_evict &&
               (!run_.active_plateau_retired_filter_keys.empty() ||
                !run_.active_plateau_retired_filter_scaled_rows.empty());
    }

bool AdaptiveSession::active_plateau_retired_key_matches(const std::string& key) {
        if (!this->active_plateau_retire_filter_enabled() || key.empty()) {
            return false;
        }
        if (options.basins.active_plateau_retire_evict_parameter_only) {
            return false;
        }
        const bool allow_similarity = this->active_mating_allow_similarity();
        for (const std::string& retired_key : run_.active_plateau_retired_filter_keys) {
            if (this->active_keys_match(key, retired_key, allow_similarity)) {
                return true;
            }
        }
        return false;
    }

double AdaptiveSession::active_plateau_nearest_retired_anchor_distance(const std::vector<double>& scaled) {
            double nearest = std::numeric_limits<double>::infinity();
            if (!this->active_plateau_retire_filter_enabled() ||
                scaled.size() != static_cast<std::size_t>(run_.dim)) {
                return nearest;
            }
            for (const std::vector<double>& retired_scaled :
                 run_.active_plateau_retired_filter_scaled_rows) {
                if (retired_scaled.size() != static_cast<std::size_t>(run_.dim)) {
                    continue;
                }
                nearest = std::min(
                    nearest,
                    scaled_vector_distance(
                        owner.variables_,
                        scaled,
                        retired_scaled,
                        options.basins.distance_metric,
                        run_.internal_mode,
                        &owner.base_,
                        options.basins.air_thickness_distance_cap));
            }
            return nearest;
        }

bool AdaptiveSession::active_plateau_row_matches_retired(const std::vector<double>& scaled, const std::string& key) {
            if (!this->active_plateau_retire_filter_enabled()) {
                return false;
            }
            if (this->active_plateau_retired_key_matches(key)) {
                return true;
            }
            const double nearest =
                this->active_plateau_nearest_retired_anchor_distance(scaled);
            return std::isfinite(nearest) &&
                   nearest <= this->active_plateau_retire_match_distance();
        }

bool AdaptiveSession::active_plateau_fitness_matches_retired(const std::vector<double>& scaled, const CandidateFitness& candidate) {
            return this->active_plateau_row_matches_retired(
                scaled,
                this->fitness_diversity_key(candidate));
        }

bool AdaptiveSession::active_plateau_basin_matches_retired(const BasinElite& basin) {
        return this->active_plateau_row_matches_retired(
            basin.scaled_values,
            this->active_lifecycle_key_for_basin(basin));
    }

bool AdaptiveSession::add_active_plateau_retired_filter(const std::string& key, const std::vector<double>& scaled) {
            const bool allow_similarity = this->active_mating_allow_similarity();
            bool added_key = false;
            bool added_anchor = false;
            if (!key.empty()) {
                bool exists = false;
                for (const std::string& existing : run_.active_plateau_retired_filter_keys) {
                    if (this->active_keys_match(key, existing, allow_similarity)) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    run_.active_plateau_retired_filter_keys.push_back(key);
                    added_key = true;
                }
            }
            if (scaled.size() != static_cast<std::size_t>(run_.dim)) {
                this->refresh_active_plateau_retire_clear_count();
                return added_key;
            }
            const double duplicate_distance =
                std::max(1.0e-12, this->active_plateau_retire_match_distance());
            bool anchor_exists = false;
            for (const std::vector<double>& existing :
                 run_.active_plateau_retired_filter_scaled_rows) {
                if (existing.size() != static_cast<std::size_t>(run_.dim)) {
                    continue;
                }
                const double distance = scaled_vector_distance(
                    owner.variables_,
                    scaled,
                    existing,
                    options.basins.distance_metric,
                    run_.internal_mode,
                    &owner.base_,
                    options.basins.air_thickness_distance_cap);
                if (std::isfinite(distance) && distance <= duplicate_distance) {
                    anchor_exists = true;
                    break;
                }
            }
            if (!anchor_exists) {
                run_.active_plateau_retired_filter_scaled_rows.push_back(scaled);
                added_anchor = true;
            }
            this->refresh_active_plateau_retire_clear_count();
            return added_key || added_anchor;
        }

bool AdaptiveSession::active_lifecycle_key_in_cooldown(const std::string& key) {
        if (!this->active_plateau_lifecycle_enabled() || key.empty()) {
            return false;
        }
        const auto it = run_.active_basin_lifecycle.find(key);
        return it != run_.active_basin_lifecycle.end() &&
               run_.current_outer_epoch > 0 &&
               it->second.cooldown_until_epoch >= run_.current_outer_epoch;
    }

bool AdaptiveSession::active_lifecycle_basin_in_cooldown(const BasinElite& basin) {
        return this->active_lifecycle_key_in_cooldown(this->active_lifecycle_key_for_basin(basin));
    }

bool AdaptiveSession::active_lifecycle_key_stalled(const std::string& key) {
        if (!this->active_plateau_lifecycle_enabled() ||
            this->active_lifecycle_key_in_cooldown(key)) {
            return false;
        }
        const auto it = run_.active_basin_lifecycle.find(key);
        return it != run_.active_basin_lifecycle.end() &&
               it->second.stale_epochs >=
                   std::max(1, options.basins.active_plateau_window);
    }

bool AdaptiveSession::active_lifecycle_mating_key_stalled(const std::string& key) {
        if (!this->active_plateau_lifecycle_enabled() || key.empty()) {
            return false;
        }
        const auto cached = run_.active_lifecycle_mating_stalled_cache.find(key);
        if (cached != run_.active_lifecycle_mating_stalled_cache.end()) {
            return cached->second;
        }
        bool stalled = false;
        if (!this->active_mating_allow_similarity()) {
            stalled = this->active_lifecycle_key_stalled(key);
            run_.active_lifecycle_mating_stalled_cache[key] = stalled;
            return stalled;
        }
        for (const auto& [lifecycle_key, state] : run_.active_basin_lifecycle) {
            if (run_.current_outer_epoch > 0 &&
                state.cooldown_until_epoch >= run_.current_outer_epoch) {
                continue;
            }
            if (state.stale_epochs <
                std::max(1, options.basins.active_plateau_window)) {
                continue;
            }
            if (this->active_keys_match(key, lifecycle_key, true)) {
                stalled = true;
                break;
            }
        }
        run_.active_lifecycle_mating_stalled_cache[key] = stalled;
        return stalled;
    }

bool AdaptiveSession::active_lifecycle_mating_key_suppressed(const std::string& key) {
        if (!this->active_plateau_lifecycle_enabled() || key.empty()) {
            return false;
        }
        const auto cached = run_.active_lifecycle_mating_suppressed_cache.find(key);
        if (cached != run_.active_lifecycle_mating_suppressed_cache.end()) {
            return cached->second;
        }
        const int stale_window = std::max(1, options.basins.active_plateau_window);
        auto state_suppressed = [&](const ActiveBasinLifecycleState& state) {
            const bool cooling =
                run_.current_outer_epoch > 0 &&
                state.cooldown_until_epoch >= run_.current_outer_epoch;
            return cooling || state.stale_epochs >= stale_window;
        };
        bool suppressed = false;
        if (!this->active_mating_allow_similarity()) {
            const auto it = run_.active_basin_lifecycle.find(key);
            suppressed = it != run_.active_basin_lifecycle.end() &&
                         state_suppressed(it->second);
            run_.active_lifecycle_mating_suppressed_cache[key] = suppressed;
            return suppressed;
        }
        for (const auto& [lifecycle_key, state] : run_.active_basin_lifecycle) {
            if (!state_suppressed(state)) {
                continue;
            }
            if (this->active_keys_match(key, lifecycle_key, true)) {
                suppressed = true;
                break;
            }
        }
        run_.active_lifecycle_mating_suppressed_cache[key] = suppressed;
        return suppressed;
    }

bool AdaptiveSession::active_lifecycle_recently_drifted_key(const std::string& key) {
        if (!this->active_plateau_lifecycle_enabled() || key.empty()) {
            return false;
        }
        const auto it = run_.active_basin_lifecycle.find(key);
        if (it == run_.active_basin_lifecycle.end()) {
            return false;
        }
        return run_.current_outer_epoch > 0 &&
               it->second.cooldown_until_epoch >= run_.current_outer_epoch;
    }

std::vector<int> AdaptiveSession::active_plateau_tracking_candidate_indices() {
        std::vector<int> indices = this->active_basin_quality_candidate_indices();
        int frontier_limit = std::max(0, options.basins.active_plateau_frontier_count);
        const double frontier_multiplier =
            std::max(0.0, options.basins.active_plateau_frontier_multiplier);
        if (frontier_limit <= 0 && frontier_multiplier > 0.0 &&
            options.basins.active_count > 0) {
            frontier_limit = static_cast<int>(std::ceil(
                frontier_multiplier *
                static_cast<double>(std::max(1, options.basins.active_count))));
        }
        if (frontier_limit <= 0 ||
            static_cast<int>(indices.size()) <= frontier_limit) {
            return indices;
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
        std::vector<int> frontier;
        std::vector<std::string> frontier_keys;
        frontier.reserve(static_cast<std::size_t>(frontier_limit));
        frontier_keys.reserve(static_cast<std::size_t>(frontier_limit));
        const bool allow_similarity = this->active_mating_allow_similarity();
        for (int index : indices) {
            const std::string key =
                this->active_lifecycle_key_for_basin(run_.basins[static_cast<std::size_t>(index)]);
            bool already_selected = false;
            for (const std::string& existing : frontier_keys) {
                if (this->active_keys_match(key, existing, allow_similarity)) {
                    already_selected = true;
                    break;
                }
            }
            if (already_selected) {
                continue;
            }
            frontier.push_back(index);
            frontier_keys.push_back(key);
            if (static_cast<int>(frontier.size()) >= frontier_limit) {
                break;
            }
        }
        return frontier;
    }

} } // namespace optiland::optimizer_detail
