/*
 * Maintain adaptive basin representatives, archive capacity and cached basin fitness.
 * Accepted candidates enter through consider_basin; epoch and result phases trim the shared archive.
 * Preserve representative ranking, lazy-trim boundaries and rescoring order when changing archive policy.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<int> AdaptiveSession::select_basin_descriptor_representative_indices(const std::vector<int>& candidate_indices, int requested_limit, int max_per_axis_value, double soft_min_distance, bool trace_feasible_first) {
            const auto selector_start = Clock::now();
            ++run_.timing.topology_descriptor_selector_calls;
            std::vector<int> selected;
            const int limit = std::max(0, requested_limit);
            if (limit <= 0 || candidate_indices.empty()) {
                run_.timing.topology_descriptor_selector_seconds += seconds_since(selector_start);
                return selected;
            }
            std::vector<int> eligible;
            eligible.reserve(candidate_indices.size());
            for (int index : candidate_indices) {
                if (index < 0 || index >= static_cast<int>(run_.basins.size())) {
                    continue;
                }
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                if (basin.scaled_values.size() != static_cast<std::size_t>(run_.dim)) {
                    continue;
                }
                eligible.push_back(index);
            }
            if (eligible.empty()) {
                run_.timing.topology_descriptor_selector_seconds += seconds_since(selector_start);
                return selected;
            }
            std::stable_sort(
                eligible.begin(),
                eligible.end(),
                [&](int a, int b) {
                    return candidate_better(
                        run_.basins[static_cast<std::size_t>(a)].fitness,
                        run_.basins[static_cast<std::size_t>(b)].fitness,
                        options);
                });
            const int per_value_limit = max_per_axis_value < 0
                                            ? -1
                                            : std::max(1, max_per_axis_value);
            const double min_distance = std::max(0.0, soft_min_distance);
            auto contains = [&](int index) {
                return std::find(selected.begin(), selected.end(), index) != selected.end();
            };
            auto axis_values_for = [&](int index) -> const std::vector<std::uint64_t>& {
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                if (!basin.descriptor_axis_values.empty()) {
                    return basin.descriptor_axis_values;
                }
                return basin.fitness.descriptor_axis_values;
            };
            auto scaled_distance_to_selected = [&](int index) {
                if (selected.empty()) {
                    return 0.0;
                }
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                double nearest = std::numeric_limits<double>::infinity();
                for (int selected_index : selected) {
                    nearest = std::min(
                        nearest,
                        this->basin_scaled_distance(
                            basin,
                            run_.basins[static_cast<std::size_t>(selected_index)]));
                }
                return nearest;
            };
            auto descriptor_distance_to_selected = [&](int index) {
                if (selected.empty()) {
                    return 0.0;
                }
                const auto& values = axis_values_for(index);
                if (values.empty()) {
                    return 0.0;
                }
                double nearest = std::numeric_limits<double>::infinity();
                for (int selected_index : selected) {
                    const auto& other = axis_values_for(selected_index);
                    const std::size_t n = std::max(values.size(), other.size());
                    if (n == 0) {
                        nearest = std::min(nearest, 0.0);
                        continue;
                    }
                    int mismatch = 0;
                    for (std::size_t axis = 0; axis < n; ++axis) {
                        const std::uint64_t a = axis < values.size() ? values[axis] : 0ULL;
                        const std::uint64_t b = axis < other.size() ? other[axis] : 0ULL;
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
            auto coverage_gain = [&](int index) {
                const auto& values = axis_values_for(index);
                if (values.empty()) {
                    return 0.0;
                }
                double gain = 0.0;
                int considered = 0;
                for (std::size_t axis = 0; axis < values.size(); ++axis) {
                    const std::uint64_t value = values[axis];
                    int count = 0;
                    for (int selected_index : selected) {
                        const auto& selected_values = axis_values_for(selected_index);
                        if (axis < selected_values.size() && selected_values[axis] == value) {
                            ++count;
                        }
                    }
                    if (count == 0) {
                        gain += 1.0;
                    } else if (per_value_limit < 0 || count < per_value_limit) {
                        gain += 1.0 / std::sqrt(static_cast<double>(count + 1));
                    } else {
                        gain -= 0.25;
                    }
                    ++considered;
                }
                return gain / static_cast<double>(std::max(1, considered));
            };
            auto candidate_allowed = [&](int index, bool require_trace_feasible) {
                if (contains(index)) {
                    return false;
                }
                if (require_trace_feasible &&
                    !run_.basins[static_cast<std::size_t>(index)].fitness.trace_feasible) {
                    return false;
                }
                if (!selected.empty() &&
                    min_distance > 0.0 &&
                    scaled_distance_to_selected(index) < min_distance &&
                    coverage_gain(index) <= 0.05) {
                    return false;
                }
                return true;
            };
            auto add_best_descriptor_candidate = [&](bool require_trace_feasible) {
                while (static_cast<int>(selected.size()) < limit) {
                    int best_index = -1;
                    double best_score = -std::numeric_limits<double>::infinity();
                    double best_gain = -std::numeric_limits<double>::infinity();
                    double best_descriptor_distance = -std::numeric_limits<double>::infinity();
                    double best_scaled_distance = -std::numeric_limits<double>::infinity();
                    for (int index : eligible) {
                        if (!candidate_allowed(index, require_trace_feasible)) {
                            continue;
                        }
                        const double gain = coverage_gain(index);
                        const double descriptor_distance =
                            descriptor_distance_to_selected(index);
                        const double scaled_distance = scaled_distance_to_selected(index);
                        const double score = 4.0 * gain +
                                             1.5 * descriptor_distance +
                                             0.25 * scaled_distance;
                        bool better = score > best_score + 1.0e-12;
                        if (!better && std::abs(score - best_score) <= 1.0e-12) {
                            better = gain > best_gain + 1.0e-12;
                        }
                        if (!better &&
                            std::abs(score - best_score) <= 1.0e-12 &&
                            std::abs(gain - best_gain) <= 1.0e-12) {
                            better = descriptor_distance > best_descriptor_distance + 1.0e-12;
                        }
                        if (!better &&
                            std::abs(score - best_score) <= 1.0e-12 &&
                            std::abs(gain - best_gain) <= 1.0e-12 &&
                            std::abs(descriptor_distance - best_descriptor_distance) <= 1.0e-12) {
                            better = scaled_distance > best_scaled_distance + 1.0e-12;
                        }
                        if (!better &&
                            std::abs(score - best_score) <= 1.0e-12 &&
                            std::abs(gain - best_gain) <= 1.0e-12 &&
                            std::abs(descriptor_distance - best_descriptor_distance) <= 1.0e-12 &&
                            std::abs(scaled_distance - best_scaled_distance) <= 1.0e-12) {
                            better =
                                best_index < 0 ||
                                candidate_better(
                                    run_.basins[static_cast<std::size_t>(index)].fitness,
                                    run_.basins[static_cast<std::size_t>(best_index)].fitness,
                                    options);
                        }
                        if (better) {
                            best_index = index;
                            best_score = score;
                            best_gain = gain;
                            best_descriptor_distance = descriptor_distance;
                            best_scaled_distance = scaled_distance;
                        }
                    }
                    if (best_index < 0) {
                        return false;
                    }
                    selected.push_back(best_index);
                }
                return true;
            };
            const std::vector<bool> phases =
                trace_feasible_first ? std::vector<bool>{true, false} : std::vector<bool>{false};
            for (bool require_trace_feasible : phases) {
                add_best_descriptor_candidate(require_trace_feasible);
                if (static_cast<int>(selected.size()) >= limit) {
                    break;
                }
            }
            if (static_cast<int>(selected.size()) < limit && trace_feasible_first) {
                add_best_descriptor_candidate(false);
            }
            run_.timing.topology_descriptor_selector_seconds += seconds_since(selector_start);
            return selected;
        }

void AdaptiveSession::trim_basins_to_limit(int requested_limit, bool strict_family_cap) {
        const int max_basins = std::max(0, requested_limit);
        if (max_basins <= 0 ||
            (static_cast<int>(run_.basins.size()) <= max_basins &&
             !(strict_family_cap && run_.topology_family_similarity_enabled))) {
            return;
        }
        this->sort_basins();
        if (run_.descriptor_full_enabled) {
            const int max_per_axis_value =
                options.basins.semantic_diversity
                    ? std::max(1, options.basins.semantic_max_per_topology)
                    : -1;
            const std::vector<int> basin_indices = this->all_basin_indices();
            std::vector<int> selected = this->select_basin_descriptor_representative_indices(
                basin_indices,
                max_basins,
                max_per_axis_value,
                options.basins.radius,
                true);
            std::vector<BasinElite> kept;
            kept.reserve(static_cast<std::size_t>(max_basins));
            std::vector<char> used(run_.basins.size(), 0);
            auto add_selected_index = [&](int index) {
                if (static_cast<int>(kept.size()) >= max_basins ||
                    index < 0 ||
                    index >= static_cast<int>(run_.basins.size()) ||
                    used[static_cast<std::size_t>(index)]) {
                    return false;
                }
                kept.emplace_back(std::move(run_.basins[static_cast<std::size_t>(index)]));
                used[static_cast<std::size_t>(index)] = 1;
                return true;
            };
            for (int index : selected) {
                add_selected_index(index);
            }
            for (int index : basin_indices) {
                if (static_cast<int>(kept.size()) >= max_basins) {
                    break;
                }
                add_selected_index(index);
            }
            run_.basins = std::move(kept);
            this->sort_basins();
            return;
        }
        if (!options.basins.semantic_diversity) {
            run_.basins.resize(static_cast<std::size_t>(max_basins));
            return;
        }
        const int max_per_topology = std::max(1, options.basins.semantic_max_per_topology);
        if (strict_family_cap && run_.topology_family_similarity_enabled) {
            std::vector<BasinElite> kept;
            kept.reserve(static_cast<std::size_t>(max_basins));
            std::vector<std::string> family_representative_keys;
            std::vector<int> family_counts;
            auto current_family_key = [&](const BasinElite& basin) {
                std::string key = this->basin_key(basin);
                if (key.empty()) {
                    key = this->basin_similarity_family_key(basin);
                }
                if (key.empty()) {
                    key = "__unknown__";
                }
                return key;
            };
            auto family_slot = [&](const std::string& key) {
                for (std::size_t i = 0; i < family_representative_keys.size(); ++i) {
                    if (this->topology_keys_are_similar(family_representative_keys[i], key)) {
                        return static_cast<int>(i);
                    }
                }
                family_representative_keys.push_back(key);
                family_counts.push_back(0);
                return static_cast<int>(family_representative_keys.size()) - 1;
            };
            for (std::size_t i = 0; i < run_.basins.size(); ++i) {
                if (static_cast<int>(kept.size()) >= max_basins) {
                    break;
                }
                const BasinElite& basin = run_.basins[i];
                const std::string key = current_family_key(basin);
                const int slot = family_slot(key);
                if (family_counts[static_cast<std::size_t>(slot)] >= max_per_topology) {
                    continue;
                }
                kept.emplace_back(std::move(run_.basins[i]));
                ++family_counts[static_cast<std::size_t>(slot)];
            }
            run_.basins = std::move(kept);
            return;
        }
        std::vector<BasinElite> kept;
        kept.reserve(static_cast<std::size_t>(max_basins));
        std::vector<char> used(run_.basins.size(), 0);
        std::vector<int> skipped_indices;
        skipped_indices.reserve(run_.basins.size());
        std::unordered_map<int, int> topology_counts_by_family_id;
        topology_counts_by_family_id.reserve(run_.basins.size());
        std::unordered_map<std::string, int> topology_counts_by_key;
        topology_counts_by_key.reserve(run_.basins.size());
        auto normalized_key = [&](const BasinElite& basin) {
            std::string key = run_.topology_family_similarity_enabled
                                  ? this->basin_family_group_key(basin)
                                  : this->basin_key(basin);
            if (key.empty()) {
                key = "__unknown__";
            }
            return key;
        };
        auto add_basin_index = [&](std::size_t index, bool enforce_topology_cap) {
            if (static_cast<int>(kept.size()) >= max_basins) {
                return false;
            }
            if (index >= run_.basins.size() || used[index]) {
                return false;
            }
            const BasinElite& basin = run_.basins[index];
            if (run_.topology_family_similarity_enabled && basin.similarity_family_id > 0) {
                int& family_count =
                    topology_counts_by_family_id[basin.similarity_family_id];
                if (enforce_topology_cap && family_count >= max_per_topology) {
                    if (enforce_topology_cap) {
                        skipped_indices.push_back(static_cast<int>(index));
                    }
                    return false;
                }
                kept.emplace_back(std::move(run_.basins[index]));
                used[index] = 1;
                ++family_count;
                return true;
            }
            const std::string key = normalized_key(basin);
            int& topology_count = topology_counts_by_key[key];
            if (enforce_topology_cap && topology_count >= max_per_topology) {
                if (enforce_topology_cap) {
                    skipped_indices.push_back(static_cast<int>(index));
                }
                return false;
            }
            kept.emplace_back(std::move(run_.basins[index]));
            used[index] = 1;
            ++topology_count;
            return true;
        };
        for (std::size_t i = 0; i < run_.basins.size(); ++i) {
            add_basin_index(i, true);
            if (static_cast<int>(kept.size()) >= max_basins) {
                break;
            }
        }
        if (strict_family_cap && run_.topology_family_similarity_enabled) {
            run_.basins = std::move(kept);
            return;
        }
        for (int index : skipped_indices) {
            if (add_basin_index(static_cast<std::size_t>(index), false) &&
                static_cast<int>(kept.size()) >= max_basins) {
                break;
            }
        }
        run_.basins = std::move(kept);
    }

void AdaptiveSession::trim_basins() {
        this->trim_basins_to_limit(options.basins.max_basins);
    }

int AdaptiveSession::full_lazy_basin_trim_limit() {
        const int max_basins = std::max(0, options.basins.max_basins);
        return max_basins + std::max(32, max_basins);
    }

BasinConsiderResult AdaptiveSession::consider_basin(const std::vector<double>& scaled, const CandidateFitness& candidate_fitness, const std::string& source, bool polished) {
        const auto consider_start = std::chrono::steady_clock::now();
        BasinConsiderResult result;
        if (!options.basins.enabled || options.basins.max_basins <= 0) {
            run_.timing.basin_consider_seconds += seconds_since(consider_start);
            result.action = "disabled";
            return result;
        }
        if (!candidate_fitness.trace_feasible && !options.basins.include_near_feasible) {
            run_.timing.basin_consider_seconds += seconds_since(consider_start);
            result.action = "trace_filtered";
            return result;
        }
        std::string candidate_topology = candidate_fitness.topology_key;
        std::string candidate_semantic = candidate_fitness.semantic_key;
        std::string candidate_descriptor = candidate_fitness.descriptor_key;
        std::string candidate_descriptor_diagnostic = candidate_fitness.descriptor_diagnostic;
        std::vector<std::uint64_t> candidate_descriptor_axis_values =
            candidate_fitness.descriptor_axis_values;
        std::string candidate_diversity_key = candidate_topology;
        if (run_.descriptor_full_enabled && !candidate_descriptor.empty()) {
            candidate_diversity_key = candidate_descriptor;
        }
        if ((candidate_topology.empty() || candidate_semantic.empty()) &&
            options.basins.semantic_diversity) {
            const auto keys = this->topology_keys_for_scaled(scaled);
            if (candidate_topology.empty()) {
                candidate_topology = keys.first;
            }
            if (candidate_semantic.empty()) {
                candidate_semantic = keys.second;
            }
        }
        if (candidate_diversity_key.empty()) {
            candidate_diversity_key = candidate_topology;
        }
        const std::vector<double> candidate_distance_values =
            this->basin_distance_values_for_scaled(scaled);
        double nearest = std::numeric_limits<double>::infinity();
        double nearest_similarity = 0.0;
        int nearest_index = -1;
        for (int i = 0; i < static_cast<int>(run_.basins.size()); ++i) {
            double similarity = 0.0;
            const auto diversity_match_start = std::chrono::steady_clock::now();
            bool diversity_match = true;
            if (options.basins.semantic_diversity &&
                !candidate_diversity_key.empty() &&
                !((diversity_match = this->basin_matches_candidate_diversity(
                        run_.basins[static_cast<std::size_t>(i)],
                        candidate_diversity_key,
                        &similarity)))) {
                run_.timing.basin_diversity_match_seconds +=
                    seconds_since(diversity_match_start);
                continue;
            }
            run_.timing.basin_diversity_match_seconds += seconds_since(diversity_match_start);
            if (!diversity_match) {
                continue;
            }
            const auto distance_start = std::chrono::steady_clock::now();
            const double distance = this->basin_distance_between(
                candidate_distance_values,
                run_.basins[static_cast<std::size_t>(i)].distance_values);
            run_.timing.basin_distance_seconds += seconds_since(distance_start);
            if (distance < nearest) {
                nearest = distance;
                nearest_similarity = run_.topology_similarity_enabled ? similarity : 1.0;
                nearest_index = i;
            }
        }

        auto make_elite = [&]() {
            BasinElite elite;
            elite.id = run_.next_basin_id++;
            elite.scaled_values = scaled;
            elite.distance_values = candidate_distance_values;
            elite.physical_values = scaled_to_physical(owner.variables_, scaled, run_.internal_mode);
            elite.fitness = candidate_fitness;
            elite.fitness.topology_key = candidate_topology;
            elite.fitness.semantic_key = candidate_semantic;
            elite.fitness.descriptor_key = candidate_descriptor;
            elite.fitness.descriptor_diagnostic = candidate_descriptor_diagnostic;
            elite.fitness.descriptor_axis_values = candidate_descriptor_axis_values;
            elite.nearest_distance = nearest;
            elite.generation = candidate_fitness.generation;
            elite.polish_count = polished ? 1 : 0;
            elite.source = source;
            elite.topology_key = candidate_topology;
            elite.semantic_key = candidate_semantic;
            elite.descriptor_key = candidate_descriptor;
            elite.descriptor_diagnostic = candidate_descriptor_diagnostic;
            elite.descriptor_axis_values = candidate_descriptor_axis_values;
            const bool creates_new_similarity_family =
                nearest_index < 0 ||
                (!run_.topology_family_similarity_enabled &&
                 nearest >= std::max(0.0, options.basins.radius));
            if (creates_new_similarity_family) {
                elite.similarity_family_id = run_.next_similarity_family_id++;
                elite.similarity_family_key = candidate_topology;
            } else {
                const BasinElite& family = run_.basins[static_cast<std::size_t>(nearest_index)];
                elite.similarity_family_id = family.similarity_family_id > 0
                                                 ? family.similarity_family_id
                                                 : run_.next_similarity_family_id++;
                elite.similarity_family_key = this->basin_similarity_family_key(family);
                if (elite.similarity_family_key.empty()) {
                    elite.similarity_family_key = candidate_topology;
                }
            }
            elite.similarity_to_family =
                run_.topology_similarity_enabled && !creates_new_similarity_family
                    ? nearest_similarity
                    : 1.0;
            return elite;
        };

        if (nearest_index >= 0) {
            result.nearest_basin_id =
                run_.basins[static_cast<std::size_t>(nearest_index)].id;
            result.nearest_distance = nearest;
            result.previous_source =
                run_.basins[static_cast<std::size_t>(nearest_index)].source;
        }
        if (nearest_index < 0 || nearest >= std::max(0.0, options.basins.radius)) {
            const auto make_elite_start = std::chrono::steady_clock::now();
            run_.basins.push_back(make_elite());
            run_.timing.basin_make_elite_seconds += seconds_since(make_elite_start);
            ++run_.timing.basin_new_count;
            const BasinElite& added = run_.basins.back();
            result.action = "new";
            result.basin_id = added.id;
            result.nearest_distance = nearest;
        } else {
            BasinElite& current = run_.basins[static_cast<std::size_t>(nearest_index)];
            if (candidate_better(candidate_fitness, current.fitness, options)) {
                const int id = current.id;
                const int similarity_family_id = current.similarity_family_id;
                const std::string similarity_family_key = current.similarity_family_key;
                const int prior_polish_count = current.polish_count;
                const auto make_elite_start = std::chrono::steady_clock::now();
                current = make_elite();
                run_.timing.basin_make_elite_seconds += seconds_since(make_elite_start);
                current.id = id;
                current.similarity_family_id = similarity_family_id;
                current.similarity_family_key = similarity_family_key;
                current.similarity_to_family =
                    run_.topology_similarity_enabled ? nearest_similarity : 1.0;
                current.polish_count = prior_polish_count + (polished ? 1 : 0);
                ++run_.timing.basin_replace_count;
                result.action = "replace";
                result.basin_id = current.id;
            } else if (polished) {
                ++current.polish_count;
                result.action = "merged_polish_count";
                result.basin_id = current.id;
            } else {
                result.action = "merged_not_better";
                result.basin_id = current.id;
            }
        }
        if (static_cast<int>(run_.basins.size()) > options.basins.max_basins &&
            (!run_.descriptor_full_enabled ||
             static_cast<int>(run_.basins.size()) > this->full_lazy_basin_trim_limit())) {
            const auto trim_start = std::chrono::steady_clock::now();
            this->trim_basins();
            run_.timing.basin_trim_seconds += seconds_since(trim_start);
            ++run_.timing.basin_trim_count;
        }
        run_.timing.basin_consider_seconds += seconds_since(consider_start);
        return result;
    }

bool AdaptiveSession::active_diversity_enabled() {
        return options.basins.enabled &&
               options.basins.active_diversity &&
               options.basins.active_count > 0 &&
               run_.pop_size > 1;
    }

bool AdaptiveSession::global_refill_blocked_basin(const BasinElite& basin) {
        return this->global_refill_blocked_source(basin.source) ||
               this->global_refill_blocked_source(basin.fitness.source);
    }

void AdaptiveSession::rescore_basin_fitness(BasinElite& basin) {
        if (basin.scaled_values.size() != static_cast<std::size_t>(run_.dim)) {
            return;
        }
        const int generation = basin.fitness.generation;
        const int population_index = basin.fitness.population_index;
        const std::string source =
            basin.fitness.source.empty() ? basin.source : basin.fitness.source;
        const bool polished = basin.fitness.polished;
        basin.fitness = this->score_fitness_for_scaled(
            basin.scaled_values,
            generation,
            population_index,
            source.empty() ? "adaptive_glass_tolerance_basin" : source,
            polished);
        basin.topology_key = basin.fitness.topology_key;
        basin.semantic_key = basin.fitness.semantic_key;
        basin.descriptor_key = basin.fitness.descriptor_key;
        basin.descriptor_diagnostic = basin.fitness.descriptor_diagnostic;
        basin.descriptor_axis_values = basin.fitness.descriptor_axis_values;
    }

} } // namespace optiland::optimizer_detail
