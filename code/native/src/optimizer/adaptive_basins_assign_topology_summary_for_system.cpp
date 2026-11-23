/*
 * Build adaptive candidate topology identities, distance embeddings and cached similarity queries.
 * Scoring and basin management share parsed topology keys and timing/cache synchronization in run_.
 * Preserve variable/descriptor order and canonical key construction because archives use these identities.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::assign_topology_summary_for_system(CandidateFitness& fitness, const LensSystem& candidate_system) {
        if (!options.topology_summary_enabled) {
            return;
        }
        const auto key_start = Clock::now();
        fitness.topology_key =
            optical_topology_key(candidate_system, owner.merit_config_.target_efl);
        fitness.semantic_key =
            optical_semantic_key(candidate_system, owner.merit_config_.target_efl);
        fitness.continuous_topology_signature =
            optical_continuous_structure_signature(
                candidate_system, owner.merit_config_.target_efl);
        const double key_seconds = seconds_since(key_start);
        double descriptor_seconds = 0.0;
        if (run_.descriptor_enabled) {
            const auto descriptor_start = Clock::now();
            OpticalTopologyDescriptor desc = build_optical_topology_descriptor(
                candidate_system,
                options.basins.topology_descriptor);
            fitness.descriptor_key = std::move(desc.key);
            fitness.descriptor_diagnostic = std::move(desc.diagnostic);
            fitness.descriptor_axis_values = std::move(desc.axis_values);
            descriptor_seconds = seconds_since(descriptor_start);
        }
        {
            const std::lock_guard<PortableMutex> lock(run_.topology_timing_mutex);
            run_.timing.topology_key_seconds += key_seconds;
            ++run_.timing.topology_key_builds;
            if (run_.descriptor_enabled) {
                run_.timing.topology_descriptor_seconds += descriptor_seconds;
                ++run_.timing.topology_descriptor_builds;
            }
        }
    }

std::pair<std::string, std::string > AdaptiveSession::topology_keys_for_physical(const std::vector<double>& values) {
        if (!options.topology_summary_enabled) {
            return std::make_pair(std::string{}, std::string{});
        }
        LensSystem candidate_system =
            apply_mixed_variables(owner.base_, owner.variables_, values, owner.glass_search_);
        CandidateFitness summary;
        this->assign_topology_summary_for_system(summary, candidate_system);
        return std::make_pair(summary.topology_key, summary.semantic_key);
    }

void AdaptiveSession::assign_topology_keys_for_physical(CandidateFitness& fitness, const std::vector<double>& values) {
        if (!options.topology_summary_enabled) {
            return;
        }
        LensSystem candidate_system =
            apply_mixed_variables(owner.base_, owner.variables_, values, owner.glass_search_);
        this->assign_topology_summary_for_system(fitness, candidate_system);
    }

void AdaptiveSession::assign_topology_keys_for_physical_row(CandidateFitness& fitness, const double* values, std::size_t count) {
        if (!options.topology_summary_enabled) {
            return;
        }
        LensSystem candidate_system =
            apply_mixed_variables_row(owner.base_, owner.variables_, values, count, owner.glass_search_);
        this->assign_topology_summary_for_system(fitness, candidate_system);
    }

std::string AdaptiveSession::topology_key_for_physical(const std::vector<double>& values) {
        return this->topology_keys_for_physical(values).first;
    }

std::string AdaptiveSession::topology_key_for_scaled(const std::vector<double>& scaled) {
        return this->topology_key_for_physical(scaled_to_physical(owner.variables_, scaled, run_.internal_mode));
    }

std::pair<std::string, std::string > AdaptiveSession::topology_keys_for_scaled(const std::vector<double>& scaled) {
        return this->topology_keys_for_physical(scaled_to_physical(owner.variables_, scaled, run_.internal_mode));
    }

std::vector<double> AdaptiveSession::basin_distance_values_for_scaled(const std::vector<double>& scaled_values) {
        if (!run_.basin_uses_distance_embedding) {
            return scaled_values;
        }
        std::vector<double> values;
        values.reserve(run_.basin_distance_columns.size());
        for (int column : run_.basin_distance_columns) {
            const auto& var = owner.variables_[static_cast<std::size_t>(column)];
            values.push_back(normalized_from_internal_for_distance(
                &owner.base_,
                var,
                scaled_values[static_cast<std::size_t>(column)],
                run_.internal_mode,
                options.basins.air_thickness_distance_cap));
        }
        return values;
    }

double AdaptiveSession::basin_distance_between(const std::vector<double>& left, const std::vector<double>& right) {
        if (!run_.basin_uses_distance_embedding) {
            return scaled_vector_distance(
                owner.variables_,
                left,
                right,
                options.basins.distance_metric,
                run_.internal_mode,
                &owner.base_,
                options.basins.air_thickness_distance_cap);
        }
        const std::size_t count = std::min(left.size(), right.size());
        if (count == 0U) {
            return 0.0;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double d = left[i] - right[i];
            sum += d * d;
        }
        return std::sqrt(sum / static_cast<double>(count));
    }

const std::string& AdaptiveSession::basin_key(const BasinElite& basin) {
        if (!basin.topology_key.empty()) {
            return basin.topology_key;
        }
        return basin.fitness.topology_key;
    }

const std::string& AdaptiveSession::basin_descriptor_key(const BasinElite& basin) {
        if (!basin.descriptor_key.empty()) {
            return basin.descriptor_key;
        }
        return basin.fitness.descriptor_key;
    }

const std::string& AdaptiveSession::basin_diversity_key(const BasinElite& basin) {
        if (run_.descriptor_full_enabled) {
            const std::string& descriptor = this->basin_descriptor_key(basin);
            if (!descriptor.empty()) {
                return descriptor;
            }
        }
        return this->basin_key(basin);
    }

const std::string& AdaptiveSession::fitness_diversity_key(const CandidateFitness& candidate) {
        if (run_.descriptor_full_enabled) {
            const std::string& descriptor = this->fitness_descriptor_key(candidate);
            if (!descriptor.empty()) {
                return descriptor;
            }
        }
        return candidate.topology_key;
    }

const std::string& AdaptiveSession::basin_similarity_family_key(const BasinElite& basin) {
        if (!basin.similarity_family_key.empty()) {
            return basin.similarity_family_key;
        }
        if (!basin.topology_key.empty()) {
            return basin.topology_key;
        }
        return basin.fitness.topology_key;
    }

double AdaptiveSession::topology_similarity_threshold() {
        return std::clamp(
            options.basins.topology_descriptor.similarity_threshold,
            0.0,
            1.0);
    }

const CachedParsedTopologyKey& AdaptiveSession::parsed_topology_key_for_similarity(const std::string& key) {
            auto cached = run_.parsed_topology_similarity_cache.find(key);
            if (cached == run_.parsed_topology_similarity_cache.end()) {
                CachedParsedTopologyKey entry;
                entry.parsed = parse_structural_topology_key(key);
                entry.id = run_.next_parsed_topology_key_id++;
                cached = run_.parsed_topology_similarity_cache.emplace(
                    key, std::move(entry)).first;
            }
            return cached->second;
        }

long long unsigned int AdaptiveSession::topology_pair_cache_key(std::uint32_t first, std::uint32_t second) {
        if (second < first) {
            std::swap(first, second);
        }
        return (static_cast<std::uint64_t>(first) << 32) |
               static_cast<std::uint64_t>(second);
    }

bool AdaptiveSession::topology_keys_are_similar(const std::string& left, const std::string& right, double* similarity_out) {
        if (left.empty() || right.empty()) {
            if (similarity_out) {
                *similarity_out = 0.0;
            }
            return false;
        }
        if (left == right) {
            if (similarity_out) {
                *similarity_out = 1.0;
            }
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
        auto cached = run_.topology_similarity_cache.find(cache_key);
        if (cached == run_.topology_similarity_cache.end()) {
            ++run_.timing.topology_similarity_cache_misses;
            CachedTopologySimilarity entry;
            if (run_.topology_family_similarity_enabled) {
                entry.similarity = weighted_structural_family_similarity_parsed(
                    first_parsed.parsed,
                    second_parsed.parsed);
            } else {
                entry.similarity = weighted_structural_topology_similarity(first, second);
            }
            cached = run_.topology_similarity_cache.emplace(cache_key, entry).first;
        } else {
            ++run_.timing.topology_similarity_cache_hits;
        }
        const double similarity = cached->second.similarity;
        if (similarity_out) {
            *similarity_out = similarity;
        }
        return similarity >= this->topology_similarity_threshold();
    }

bool AdaptiveSession::basin_matches_candidate_diversity(const BasinElite& basin, const std::string& candidate_key, double* similarity_out) {
        if (run_.topology_similarity_enabled) {
            const std::string& family_key = this->basin_similarity_family_key(basin);
            if (family_key.empty() || candidate_key.empty()) {
                if (similarity_out) {
                    *similarity_out = 0.0;
                }
                return false;
            }
            double family_similarity = 0.0;
            const bool family_match =
                this->topology_keys_are_similar(family_key, candidate_key, &family_similarity);
            double current_similarity = 0.0;
            bool current_match = false;
            if (run_.topology_family_similarity_enabled) {
                const std::string& current_key = this->basin_key(basin);
                if (!current_key.empty()) {
                    current_match =
                        this->topology_keys_are_similar(current_key, candidate_key, &current_similarity);
                }
            }
            if (similarity_out) {
                *similarity_out = std::max(family_similarity, current_similarity);
            }
            return family_match || current_match;
        }
        const bool same = this->basin_diversity_key(basin) == candidate_key;
        if (similarity_out) {
            *similarity_out = same ? 1.0 : 0.0;
        }
        return same;
    }

std::string AdaptiveSession::basin_family_group_key(const BasinElite& basin) {
        if (run_.topology_family_similarity_enabled) {
            if (basin.similarity_family_id > 0) {
                return std::string("family:") + std::to_string(basin.similarity_family_id);
            }
            std::string key = this->basin_similarity_family_key(basin);
            if (!key.empty()) {
                return key;
            }
        }
        return this->basin_key(basin);
    }

void AdaptiveSession::sort_basins() {
        std::stable_sort(run_.basins.begin(), run_.basins.end(), [this](const BasinElite& a,
                                                                  const BasinElite& b) {
            return candidate_better(a.fitness, b.fitness, options);
        });
    }

int AdaptiveSession::basin_topology_count() {
        std::vector<std::string> keys;
        for (const auto& basin : run_.basins) {
            std::string key = this->basin_key(basin);
            if (key.empty()) {
                key = "__unknown__";
            }
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(std::move(key));
            }
        }
        return static_cast<int>(keys.size());
    }

std::vector<int> AdaptiveSession::all_basin_indices() {
        std::vector<int> indices(run_.basins.size());
        std::iota(indices.begin(), indices.end(), 0);
        return indices;
    }

double AdaptiveSession::basin_topology_distance(const std::string& a, const std::string& b) {
        if (a == b) {
            return 0.0;
        }
        if (a.empty() || b.empty()) {
            return 1.0;
        }
        const std::size_t n = std::max(a.size(), b.size());
        int mismatch = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const char ca = i < a.size() ? a[i] : '\0';
            const char cb = i < b.size() ? b[i] : '\0';
            if (ca != cb) {
                ++mismatch;
            }
        }
        return static_cast<double>(mismatch) / static_cast<double>(std::max<std::size_t>(1, n));
    }

double AdaptiveSession::basin_scaled_distance(const BasinElite& a, const BasinElite& b) {
        return this->basin_distance_between(a.distance_values, b.distance_values);
    }

std::vector<int> AdaptiveSession::select_basin_representative_indices(const std::vector<int>& candidate_indices, int requested_limit, int max_per_topology, double same_key_min_distance, bool trace_feasible_first) {
            std::vector<int> selected;
            const int limit = std::max(0, requested_limit);
            if (limit <= 0 || candidate_indices.empty()) {
                return selected;
            }
            std::vector<int> eligible;
            eligible.reserve(candidate_indices.size());
            for (int index : candidate_indices) {
                if (index < 0 || index >= static_cast<int>(run_.basins.size())) {
                    continue;
                }
                if (run_.basins[static_cast<std::size_t>(index)].scaled_values.size() !=
                    static_cast<std::size_t>(run_.dim)) {
                    continue;
                }
                eligible.push_back(index);
            }
            if (eligible.empty()) {
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
            const int per_topology_limit =
                max_per_topology < 0 ? -1 : std::max(1, max_per_topology);
            const double min_same_key_distance = std::max(0.0, same_key_min_distance);
            auto contains = [&](int index) {
                return std::find(selected.begin(), selected.end(), index) != selected.end();
            };
            auto key_for_index = [&](int index) {
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                return run_.topology_family_similarity_enabled
                           ? this->basin_family_group_key(basin)
                           : this->basin_key(basin);
            };
            auto selected_same_key_count = [&](const std::string& key) {
                return static_cast<int>(std::count_if(
                    selected.begin(),
                    selected.end(),
                    [&](int selected_index) {
                        return key_for_index(selected_index) == key;
                    }));
            };
            auto same_key_distance_ok = [&](int index) {
                if (per_topology_limit < 0) {
                    return true;
                }
                const std::string key = key_for_index(index);
                const int same_key_count = selected_same_key_count(key);
                if (same_key_count < per_topology_limit) {
                    return true;
                }
                if (min_same_key_distance <= 0.0) {
                    return false;
                }
                double nearest_same_key = std::numeric_limits<double>::infinity();
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                for (int selected_index : selected) {
                    if (key_for_index(selected_index) != key) {
                        continue;
                    }
                    nearest_same_key = std::min(
                        nearest_same_key,
                        this->basin_scaled_distance(
                            basin,
                            run_.basins[static_cast<std::size_t>(selected_index)]));
                }
                return nearest_same_key >= min_same_key_distance;
            };
            auto candidate_allowed_in_phase = [&](int index, bool require_trace_feasible) {
                if (contains(index) || !same_key_distance_ok(index)) {
                    return false;
                }
                if (require_trace_feasible &&
                    !run_.basins[static_cast<std::size_t>(index)].fitness.trace_feasible) {
                    return false;
                }
                return true;
            };
            auto add = [&](int index) {
                if (index < 0 || contains(index) || static_cast<int>(selected.size()) >= limit) {
                    return false;
                }
                selected.push_back(index);
                return true;
            };
            auto add_medoid_seed = [&](bool require_trace_feasible) {
                if (!selected.empty() || static_cast<int>(selected.size()) >= limit) {
                    return false;
                }
                int best_index = -1;
                double best_sum = std::numeric_limits<double>::infinity();
                for (int index : eligible) {
                    if (!candidate_allowed_in_phase(index, require_trace_feasible)) {
                        continue;
                    }
                    const std::string key = key_for_index(index);
                    double sum = 0.0;
                    for (int other_index : eligible) {
                        if (require_trace_feasible &&
                            !run_.basins[static_cast<std::size_t>(other_index)].fitness.trace_feasible) {
                            continue;
                        }
                        sum += this->basin_topology_distance(key, key_for_index(other_index));
                    }
                    if (sum < best_sum - 1.0e-12 ||
                        (std::abs(sum - best_sum) <= 1.0e-12 &&
                         (best_index < 0 ||
                          candidate_better(
                              run_.basins[static_cast<std::size_t>(index)].fitness,
                              run_.basins[static_cast<std::size_t>(best_index)].fitness,
                              options)))) {
                        best_sum = sum;
                        best_index = index;
                    }
                }
                return add(best_index);
            };
            auto add_k_center_fill = [&](bool require_trace_feasible) {
                bool added_any = false;
                while (static_cast<int>(selected.size()) < limit) {
                    int best_index = -1;
                    double best_topology_distance = -1.0;
                    double best_scaled_distance = -1.0;
                    for (int index : eligible) {
                        if (!candidate_allowed_in_phase(index, require_trace_feasible)) {
                            continue;
                        }
                        const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                        const std::string key = key_for_index(index);
                        double nearest_topology = std::numeric_limits<double>::infinity();
                        double nearest_scaled = std::numeric_limits<double>::infinity();
                        for (int selected_index : selected) {
                            const BasinElite& selected_basin =
                                run_.basins[static_cast<std::size_t>(selected_index)];
                            nearest_topology = std::min(
                                nearest_topology,
                                this->basin_topology_distance(key, key_for_index(selected_index)));
                            nearest_scaled = std::min(
                                nearest_scaled,
                                this->basin_scaled_distance(basin, selected_basin));
                        }
                        if (selected.empty()) {
                            nearest_topology = 0.0;
                            nearest_scaled = 0.0;
                        }
                        const bool better =
                            nearest_topology > best_topology_distance + 1.0e-12 ||
                            (std::abs(nearest_topology - best_topology_distance) <= 1.0e-12 &&
                             (nearest_scaled > best_scaled_distance + 1.0e-12 ||
                              (std::abs(nearest_scaled - best_scaled_distance) <= 1.0e-12 &&
                               (best_index < 0 ||
                                candidate_better(
                                    basin.fitness,
                                    run_.basins[static_cast<std::size_t>(best_index)].fitness,
                                    options)))));
                        if (better) {
                            best_topology_distance = nearest_topology;
                            best_scaled_distance = nearest_scaled;
                            best_index = index;
                        }
                    }
                    if (!add(best_index)) {
                        break;
                    }
                    added_any = true;
                }
                return added_any;
            };
            const std::vector<bool> phases =
                trace_feasible_first ? std::vector<bool>{true, false} : std::vector<bool>{false};
            for (bool require_trace_feasible : phases) {
                if (selected.empty()) {
                    add_medoid_seed(require_trace_feasible);
                }
                add_k_center_fill(require_trace_feasible);
                if (static_cast<int>(selected.size()) >= limit) {
                    break;
                }
            }
            if (static_cast<int>(selected.size()) < limit && trace_feasible_first) {
                if (selected.empty()) {
                    add_medoid_seed(false);
                }
                add_k_center_fill(false);
            }
            return selected;
        }

} } // namespace optiland::optimizer_detail
