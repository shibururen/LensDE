/*
 * Define AdaptiveSession family matching, quality gates and protected-slot identity rules.
 * These predicates read run_.fitness, slot keys and cached shape distances during mating and replacement.
 * Keep structural similarity, parameter-distance tests and quality rejection precedence consistent.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

double AdaptiveSession::active_family_similarity_threshold() {
        return std::clamp(
            options.basins.active_family_similarity_threshold,
            0.0,
            1.0);
    }

int AdaptiveSession::active_family_max_shape_diff() {
        return options.basins.active_family_max_shape_diff;
    }

bool AdaptiveSession::active_family_shape_diff_ok(const std::string& left, const std::string& right) {
        const int max_shape_diff = this->active_family_max_shape_diff();
        if (max_shape_diff < 0 || left == right) {
            return true;
        }
        if (left.empty() || right.empty()) {
            return true;
        }
        const std::string& first = left < right ? left : right;
        const std::string& second = left < right ? right : left;
        const CachedParsedTopologyKey& first_parsed =
            this->parsed_topology_key_for_similarity(first);
        const CachedParsedTopologyKey& second_parsed =
            this->parsed_topology_key_for_similarity(second);
        const std::uint64_t cache_key =
            this->topology_pair_cache_key(first_parsed.id, second_parsed.id);
        auto cached = run_.topology_shape_diff_cache.find(cache_key);
        if (cached == run_.topology_shape_diff_cache.end()) {
            ++run_.timing.topology_shape_diff_cache_misses;
            cached = run_.topology_shape_diff_cache.emplace(
                cache_key,
                structural_topology_shape_diff_count_parsed(
                    first_parsed.parsed,
                    second_parsed.parsed)).first;
        } else {
            ++run_.timing.topology_shape_diff_cache_hits;
        }
        return cached->second <= max_shape_diff;
    }

bool AdaptiveSession::active_family_parameter_merge_enabled() {
        return options.basins.active_family_parameter_merge_enabled &&
               options.basins.active_family_parameter_merge_distance > 0.0;
    }

double AdaptiveSession::active_family_parameter_merge_distance() {
        return std::max(0.0, options.basins.active_family_parameter_merge_distance);
    }

bool AdaptiveSession::active_topology_keys_are_similar(const std::string& left, const std::string& right, double* similarity_out) {
        double similarity = 0.0;
        const bool fine_match =
            this->topology_keys_are_similar(left, right, &similarity);
        if (similarity_out) {
            *similarity_out = similarity;
        }
        if (!fine_match && similarity <= 0.0) {
            return false;
        }
        if (similarity < this->active_family_similarity_threshold()) {
            return false;
        }
        return this->active_family_shape_diff_ok(left, right);
    }

double AdaptiveSession::active_family_parameter_distance(const std::vector<double>& left, const std::vector<double>& right) {
            if (left.size() != static_cast<std::size_t>(run_.dim) ||
                right.size() != static_cast<std::size_t>(run_.dim)) {
                return std::numeric_limits<double>::infinity();
            }
            return scaled_vector_distance(
                owner.variables_,
                left,
                right,
                options.basins.active_family_parameter_merge_metric,
                run_.internal_mode,
                &owner.base_,
                options.basins.air_thickness_distance_cap);
        }

bool AdaptiveSession::active_family_parameter_rows_match(const std::vector<double>& left, const std::vector<double>& right) {
            return this->active_family_parameter_merge_enabled() &&
                   this->active_family_parameter_distance(left, right) <=
                       this->active_family_parameter_merge_distance();
        }

std::string AdaptiveSession::active_current_family_key_for_basin(const BasinElite& basin) {
        std::string key = this->basin_diversity_key(basin);
        if (!key.empty()) {
            return key;
        }
        key = this->basin_key(basin);
        if (!key.empty()) {
            return key;
        }
        key = this->basin_similarity_family_key(basin);
        if (!key.empty()) {
            return key;
        }
        return this->basin_family_group_key(basin);
    }

int AdaptiveSession::active_basin_slot_budget() {
        if (!this->active_diversity_enabled()) {
            return 0;
        }
        const double fraction = std::clamp(options.basins.protected_fraction, 0.0, 1.0);
        if (fraction <= 0.0) {
            return 0;
        }
        int by_fraction = static_cast<int>(std::ceil(fraction * static_cast<double>(run_.pop_size)));
        by_fraction = std::max(1, by_fraction);
        return std::clamp(
            std::min(options.basins.active_count, by_fraction),
            0,
            std::max(0, run_.pop_size - 1));
    }

bool AdaptiveSession::is_active_basin_slot(int index) {
        return std::find(run_.active_basin_slots.begin(), run_.active_basin_slots.end(), index) !=
               run_.active_basin_slots.end();
    }

double AdaptiveSession::active_basin_merit_ceiling() {
        if (!options.basins.active_quality_gate) {
            return std::numeric_limits<double>::infinity();
        }
        const double best_merit = run_.best_fitness.score.merit;
        if (!std::isfinite(best_merit)) {
            return std::numeric_limits<double>::infinity();
        }
        double ceiling = std::numeric_limits<double>::infinity();
        const double ratio = options.basins.active_max_merit_ratio;
        if (std::isfinite(ratio) && ratio > 0.0) {
            ceiling = std::min(ceiling, std::max(best_merit, best_merit * ratio));
        }
        const double delta = options.basins.active_max_merit_delta;
        if (std::isfinite(delta) && delta > 0.0) {
            ceiling = std::min(ceiling, best_merit + delta);
        }
        return ceiling;
    }

std::string AdaptiveSession::active_basin_reject_reason(const BasinElite& basin) {
        if (this->global_refill_blocked_basin(basin)) {
            return std::string("global_refill_blocked_archive");
        }
        if (!options.basins.active_quality_gate) {
            return std::string{};
        }
        if (basin.scaled_values.size() != static_cast<std::size_t>(run_.dim)) {
            return std::string("dimension_mismatch");
        }
        const CandidateFitness& candidate = basin.fitness;
        const Score& score = candidate.score;
        if (options.basins.active_require_trace_feasible &&
            (!candidate.trace_feasible || !score.trace_ok)) {
            return std::string("trace_infeasible");
        }
        const double min_edge = options.basins.active_min_edge;
        if (std::isfinite(min_edge) &&
            (!std::isfinite(score.min_edge) || score.min_edge < min_edge)) {
            return std::string("min_edge");
        }
        if (!std::isfinite(score.merit)) {
            return std::string("nonfinite_merit");
        }
        const double merit_ceiling = this->active_basin_merit_ceiling();
        if (std::isfinite(merit_ceiling) && score.merit > merit_ceiling) {
            return std::string("merit_ceiling");
        }
        return std::string{};
    }

bool AdaptiveSession::active_basin_quality_ok(const BasinElite& basin) {
        return this->active_basin_reject_reason(basin).empty();
    }

bool AdaptiveSession::active_keys_match(const std::string& candidate_key, const std::string& target_key, bool allow_similarity) {
        if (candidate_key.empty() || target_key.empty()) {
            return false;
        }
        if (candidate_key == target_key) {
            return true;
        }
        return allow_similarity && run_.topology_similarity_enabled &&
               this->active_topology_keys_are_similar(candidate_key, target_key);
    }

bool AdaptiveSession::active_family_match_basins(const BasinElite& candidate, const BasinElite& target, bool allow_similarity) {
            const std::string candidate_key = this->active_current_family_key_for_basin(candidate);
            const std::string target_key = this->active_current_family_key_for_basin(target);
            if (this->active_keys_match(candidate_key, target_key, allow_similarity)) {
                return true;
            }
            return this->active_family_shape_diff_ok(candidate_key, target_key) &&
                   this->active_family_parameter_rows_match(
                       candidate.scaled_values,
                       target.scaled_values);
        }

bool AdaptiveSession::active_family_match_basin_to_key(const BasinElite& candidate, const std::string& target_key, const std::vector<double>* target_scaled, bool allow_similarity) {
            const std::string candidate_key = this->active_current_family_key_for_basin(candidate);
            if (this->active_keys_match(candidate_key, target_key, allow_similarity)) {
                return true;
            }
            return target_scaled &&
                   this->active_family_shape_diff_ok(candidate_key, target_key) &&
                   this->active_family_parameter_rows_match(
                       candidate.scaled_values,
                       *target_scaled);
        }

bool AdaptiveSession::active_family_match_population_to_key(const std::string& candidate_key, const std::vector<double>& candidate_scaled, const std::string& target_key, const std::vector<double>* target_scaled, bool allow_similarity) {
            if (this->active_keys_match(candidate_key, target_key, allow_similarity)) {
                return true;
            }
            return target_scaled &&
                   this->active_family_shape_diff_ok(candidate_key, target_key) &&
                   this->active_family_parameter_rows_match(
                       candidate_scaled,
                       *target_scaled);
        }

std::vector<std::string > AdaptiveSession::active_slot_current_family_keys() {
        std::vector<std::string> keys;
        keys.reserve(run_.active_basin_slots.size());
        for (int slot : run_.active_basin_slots) {
            if (slot < 0 || slot >= run_.pop_size ||
                slot >= static_cast<int>(run_.fitness.size())) {
                continue;
            }
            std::string key =
                this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(slot)]);
            if (!key.empty()) {
                keys.push_back(std::move(key));
            }
        }
        return keys;
    }

std::vector<std::string > AdaptiveSession::active_slot_frozen_family_keys() {
        std::vector<std::string> keys;
        keys.reserve(run_.active_basin_slot_family_keys.size());
        for (const std::string& key : run_.active_basin_slot_family_keys) {
            if (!key.empty()) {
                keys.push_back(key);
            }
        }
        return keys;
    }

long long unsigned int AdaptiveSession::active_slot_offset_for_index(int idx) {
        auto it = std::find(run_.active_basin_slots.begin(), run_.active_basin_slots.end(), idx);
        if (it == run_.active_basin_slots.end()) {
            return static_cast<std::size_t>(-1);
        }
        return static_cast<std::size_t>(std::distance(run_.active_basin_slots.begin(), it));
    }

std::string AdaptiveSession::active_slot_frozen_family_key_for_index(int idx) {
        const std::size_t active_offset = this->active_slot_offset_for_index(idx);
        if (active_offset == static_cast<std::size_t>(-1) ||
            active_offset >= run_.active_basin_slot_family_keys.size()) {
            return std::string{};
        }
        return run_.active_basin_slot_family_keys[active_offset];
    }

bool AdaptiveSession::active_slot_strict_frozen_family_enabled() {
        return options.basins.active_slot_strict_frozen_family &&
               options.de_mating_scope == DeMatingScope::ActiveBasinOnly &&
               !run_.active_basin_slots.empty();
    }

bool AdaptiveSession::active_slot_uses_strict_frozen_family(int idx) {
        if (!this->active_slot_strict_frozen_family_enabled() || !this->is_active_basin_slot(idx)) {
            return false;
        }
        return !this->active_slot_frozen_family_key_for_index(idx).empty();
    }

std::string AdaptiveSession::active_slot_current_family_key_for_offset(std::size_t active_offset) {
        if (active_offset >= run_.active_basin_slots.size()) {
            return std::string{};
        }
        const int slot = run_.active_basin_slots[active_offset];
        if (slot < 0 || slot >= run_.pop_size ||
            slot >= static_cast<int>(run_.fitness.size())) {
            return std::string{};
        }
        return this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(slot)]);
    }

std::vector<double> AdaptiveSession::active_slot_scaled_row_for_offset(std::size_t active_offset) {
        if (active_offset >= run_.active_basin_slots.size()) {
            return std::vector<double>{};
        }
        const int slot = run_.active_basin_slots[active_offset];
        if (slot < 0 || slot >= run_.pop_size) {
            return std::vector<double>{};
        }
        return population_row(run_.population, slot, run_.dim);
    }



int AdaptiveSession::count_unique_active_family_slots(const std::vector<std::string>& keys, const std::vector<std::vector<double>>& scaled_rows) {
        
        std::vector<FamilyRepresentative> unique_families;
        unique_families.reserve(keys.size());
        const bool allow_similarity = this->active_mating_allow_similarity();
        for (std::size_t index = 0; index < keys.size(); ++index) {
            const std::string& key = keys[index];
            if (key.empty()) {
                continue;
            }
            const std::vector<double>* scaled =
                index < scaled_rows.size() ? &scaled_rows[index] : nullptr;
            bool matched = false;
            for (const FamilyRepresentative& representative : unique_families) {
                if (this->active_family_match_population_to_key(
                        key,
                        scaled ? *scaled : std::vector<double>{},
                        representative.key,
                        representative.scaled.empty() ? nullptr : &representative.scaled,
                        allow_similarity)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                unique_families.push_back({key, scaled ? *scaled : std::vector<double>{}});
            }
        }
        return static_cast<int>(unique_families.size());
    }

std::vector<int> AdaptiveSession::active_basin_quality_candidate_indices() {
        std::vector<int> indices;
        indices.reserve(run_.basins.size());
        for (int index : this->all_basin_indices()) {
            if (this->active_basin_quality_ok(run_.basins[static_cast<std::size_t>(index)])) {
                indices.push_back(index);
            }
        }
        return indices;
    }

std::vector<int> AdaptiveSession::active_basin_candidate_indices() {
        std::vector<int> indices;
        indices.reserve(run_.basins.size());
        const std::string best_key = this->fitness_diversity_key(run_.best_fitness);
        const bool suppress_best_family =
            !best_key.empty() &&
            this->active_lifecycle_mating_key_suppressed(best_key) &&
            !this->active_focus_key_matches_current(best_key);
        const bool allow_similarity = this->active_mating_allow_similarity();
        for (int index : this->active_basin_quality_candidate_indices()) {
            const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
            if (this->active_plateau_basin_matches_retired(basin)) {
                continue;
            }
            if (this->active_lifecycle_basin_in_cooldown(basin) &&
                !this->active_focus_key_matches_current(
                    this->active_lifecycle_key_for_basin(basin))) {
                continue;
            }
            if (suppress_best_family) {
                const std::string basin_key = this->active_lifecycle_key_for_basin(basin);
                if (this->active_keys_match(basin_key, best_key, allow_similarity)) {
                    continue;
                }
            }
            indices.push_back(index);
        }
        if (!indices.empty()) {
            return indices;
        }
        for (int index : this->active_basin_quality_candidate_indices()) {
            const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
            if (this->active_plateau_basin_matches_retired(basin)) {
                continue;
            }
            if (!this->active_lifecycle_basin_in_cooldown(basin) ||
                this->active_focus_key_matches_current(
                    this->active_lifecycle_key_for_basin(basin))) {
                indices.push_back(index);
            }
        }
        return indices;
    }

std::vector<int> AdaptiveSession::active_basin_family_candidate_indices() {
        const std::vector<int> candidates = this->active_basin_candidate_indices();
        if (candidates.size() <= 1) {
            return candidates;
        }
        std::vector<int> family_candidates;
        family_candidates.reserve(candidates.size());
        const bool allow_similarity = this->active_mating_allow_similarity();
        auto family_key_for_index = [&](int index) {
            if (index < 0 || index >= static_cast<int>(run_.basins.size())) {
                return std::string{};
            }
            return this->active_lifecycle_key_for_basin(
                run_.basins[static_cast<std::size_t>(index)]);
        };
        for (int index : candidates) {
            const std::string candidate_key = family_key_for_index(index);
            int existing = -1;
            for (int family = 0;
                 family < static_cast<int>(family_candidates.size());
                 ++family) {
                const int existing_index =
                    family_candidates[static_cast<std::size_t>(family)];
                if (existing_index >= 0 &&
                    existing_index < static_cast<int>(run_.basins.size()) &&
                    this->active_family_match_basins(
                        run_.basins[static_cast<std::size_t>(index)],
                        run_.basins[static_cast<std::size_t>(existing_index)],
                        allow_similarity)) {
                    existing = family;
                    break;
                }
            }
            if (existing < 0) {
                family_candidates.push_back(index);
                continue;
            }
            if (candidate_better(
                    run_.basins[static_cast<std::size_t>(index)].fitness,
                    run_.basins[static_cast<std::size_t>(
                               family_candidates[static_cast<std::size_t>(existing)])]
                        .fitness,
                    options)) {
                family_candidates[static_cast<std::size_t>(existing)] = index;
            }
        }
        return family_candidates;
    }

} } // namespace optiland::optimizer_detail
