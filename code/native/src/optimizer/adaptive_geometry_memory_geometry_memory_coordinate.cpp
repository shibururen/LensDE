/*
 * Measure candidate proximity to stored geometries and choose compatible difference-vector indices.
 * Adaptive mating/mutation read run_.geometry_memory_scaled, parsed keys and match caches here.
 * Preserve normalized-coordinate definitions, similarity tests and candidate ordering in memory guidance.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

double AdaptiveSession::geometry_memory_coordinate(const VariableSpec& var, double internal) {
        if (var.kind == VariableKind::AsphereCoeff) {
            const double power = frozen_asphere_radial_power(var);
            const double physical = internal_to_physical(var, internal, run_.internal_mode);
            double span = std::numeric_limits<double>::quiet_NaN();
            if (std::isfinite(var.edge_sag_lower) &&
                std::isfinite(var.edge_sag_upper) &&
                var.edge_sag_upper > var.edge_sag_lower) {
                span = var.edge_sag_upper - var.edge_sag_lower;
            } else if (std::isfinite(power) && power > 0.0) {
                span = (var.upper - var.lower) * power;
            }
            if (!std::isfinite(power) || power <= 0.0 ||
                !std::isfinite(span) || std::abs(span) <= 1.0e-300) {
                return normalized_from_internal_for_distance(
                    &owner.base_, var, internal, run_.internal_mode, 25.0);
            }
            return physical * power / span;
        }
        return normalized_from_internal_for_distance(
            &owner.base_, var, internal, run_.internal_mode, 25.0);
    }

double AdaptiveSession::geometry_memory_effective_distance(const double* scaled, const std::vector<double>& memory) {
        if (!run_.geometry_memory_enabled || scaled == nullptr) {
            return std::numeric_limits<double>::infinity();
        }
            double full_sum = 0.0;
            double skeleton_sum = 0.0;
            double asphere_sum = 0.0;
            int full_count = 0;
            int skeleton_count = 0;
            int asphere_count = 0;
            for (int j = 0; j < run_.dim; ++j) {
                const auto& var = owner.variables_[static_cast<std::size_t>(j)];
                const bool skeleton =
                    var.kind == VariableKind::Radius ||
                    var.kind == VariableKind::Curvature ||
                    var.kind == VariableKind::Thickness;
                const bool asphere =
                    var.kind == VariableKind::Conic ||
                    var.kind == VariableKind::AsphereCoeff;
                if (!skeleton && !asphere) {
                    continue;
                }
                const double candidate_coordinate =
                    this->geometry_memory_coordinate(var, scaled[j]);
                const double memory_coordinate = this->geometry_memory_coordinate(
                    var, memory[static_cast<std::size_t>(j)]);
                const double delta = candidate_coordinate - memory_coordinate;
                const double squared = delta * delta;
                full_sum += squared;
                ++full_count;
                if (skeleton) {
                    skeleton_sum += squared;
                    ++skeleton_count;
                } else {
                    asphere_sum += squared;
                    ++asphere_count;
                }
            }
            if (full_count <= 0 || skeleton_count <= 0) {
                return std::numeric_limits<double>::infinity();
            }
            const double full = std::sqrt(full_sum / static_cast<double>(full_count));
            const double skeleton =
                std::sqrt(skeleton_sum / static_cast<double>(skeleton_count));
            double effective = std::max(
                full / options.geometry_memory_full_radius,
                skeleton / options.geometry_memory_skeleton_radius);
            if (asphere_count > 0) {
                if (options.geometry_memory_asphere_radius <= 0.0) {
                    return std::numeric_limits<double>::infinity();
                }
                const double asphere =
                    std::sqrt(asphere_sum / static_cast<double>(asphere_count));
                effective = std::max(
                    effective,
                    asphere / options.geometry_memory_asphere_radius);
            }
        return effective;
    }

double AdaptiveSession::geometry_memory_min_effective_distance(const double* scaled) {
        if (!run_.geometry_memory_enabled || scaled == nullptr) {
            return std::numeric_limits<double>::infinity();
        }
        double best = std::numeric_limits<double>::infinity();
        for (const auto& memory : run_.geometry_memory_scaled) {
            best = std::min(
                best,
                this->geometry_memory_effective_distance(scaled, memory));
        }
        return best;
    }

double AdaptiveSession::geometry_memory_key_max_similarity(const std::string& topology_key) {
        if (topology_key.empty() ||
            options.geometry_memory_topology_keys.size() !=
                run_.geometry_memory_scaled.size()) {
            return 0.0;
        }
        const auto cached =
            run_.geometry_memory_key_max_similarity_cache.find(topology_key);
        if (cached != run_.geometry_memory_key_max_similarity_cache.end()) {
            return cached->second;
        }
        double maximum = 0.0;
        const ParsedStructuralTopologyKey parsed_topology_key =
            parse_structural_topology_key(topology_key);
        for (std::size_t memory_index = 0;
             memory_index < options.geometry_memory_topology_keys.size();
             ++memory_index) {
            if (options.geometry_memory_topology_keys[memory_index] == topology_key) {
                maximum = 1.0;
                break;
            }
            if (run_.geometry_memory_parsed_keys.size() !=
                options.geometry_memory_topology_keys.size()) {
                continue;
            }
            const int shape_difference =
                structural_topology_shape_diff_count_parsed(
                    parsed_topology_key,
                    run_.geometry_memory_parsed_keys[memory_index]);
            if (options.geometry_memory_difference_key_max_shape_diff >= 0 &&
                shape_difference >
                    options.geometry_memory_difference_key_max_shape_diff) {
                continue;
            }
            maximum = std::max(
                maximum,
                weighted_structural_family_similarity_parsed(
                    parsed_topology_key,
                    run_.geometry_memory_parsed_keys[memory_index]));
        }
        run_.geometry_memory_key_max_similarity_cache.emplace(topology_key, maximum);
        return maximum;
    }

bool AdaptiveSession::geometry_memory_key_is_exact(const std::string& topology_key) {
        return !topology_key.empty() &&
               std::find(
                   options.geometry_memory_topology_keys.begin(),
                   options.geometry_memory_topology_keys.end(),
                   topology_key) != options.geometry_memory_topology_keys.end();
    }

bool AdaptiveSession::geometry_memory_key_is_family_matched(const std::string& topology_key) {
        return !topology_key.empty() &&
               this->geometry_memory_key_max_similarity(topology_key) + 1.0e-12 >=
                   options.geometry_memory_difference_key_similarity_threshold;
    }

int AdaptiveSession::choose_geometry_memory_difference_index(const double* scaled, const std::string& topology_key, PBestGuideStats& stats) {
        if (!options.geometry_memory_difference_mutation ||
            !run_.geometry_memory_enabled || scaled == nullptr) {
            return -1;
        }
        const auto distance_start = Clock::now();
        ++stats.geometry_memory_difference_checked;
        std::vector<int> hit_indices;
        std::vector<double> hit_weights;
        hit_indices.reserve(run_.geometry_memory_scaled.size());
        hit_weights.reserve(run_.geometry_memory_scaled.size());
        double weight_sum = 0.0;
        int nearest_geometry_index = -1;
        double nearest_geometry_effective = std::numeric_limits<double>::infinity();
        bool nearest_geometry_same_key = false;
        int nearest_key_index = -1;
        double nearest_key_effective = std::numeric_limits<double>::infinity();
        bool nearest_key_same_key = false;
        bool any_geometry_trigger = false;
        bool any_key_trigger = false;
        bool any_exact_key_trigger = false;
        bool any_family_key_trigger = false;
        const bool aligned_memory_keys =
            options.geometry_memory_topology_keys.size() ==
                run_.geometry_memory_scaled.size();
        const std::vector<unsigned char>* key_trigger_matches = nullptr;
        if (options.geometry_memory_difference_key_trigger &&
            !topology_key.empty() && aligned_memory_keys) {
            auto cached = run_.geometry_memory_key_trigger_cache.find(topology_key);
            if (cached == run_.geometry_memory_key_trigger_cache.end()) {
                std::vector<unsigned char> matches(
                    run_.geometry_memory_scaled.size(),
                    static_cast<unsigned char>(0));
                if (options.geometry_memory_difference_key_similarity_threshold >=
                        1.0 - 1.0e-12) {
                    for (std::size_t memory_index = 0;
                         memory_index < matches.size();
                         ++memory_index) {
                        matches[memory_index] =
                            options.geometry_memory_topology_keys[memory_index] ==
                                topology_key
                                ? 1
                                : 0;
                    }
                } else if (run_.geometry_memory_parsed_keys.size() == matches.size()) {
                    const ParsedStructuralTopologyKey parsed_topology_key =
                        parse_structural_topology_key(topology_key);
                    for (std::size_t memory_index = 0;
                         memory_index < matches.size();
                         ++memory_index) {
                        const double similarity =
                            weighted_structural_family_similarity_parsed(
                                parsed_topology_key,
                                run_.geometry_memory_parsed_keys[memory_index]);
                        const int shape_difference =
                            structural_topology_shape_diff_count_parsed(
                                parsed_topology_key,
                                run_.geometry_memory_parsed_keys[memory_index]);
                        matches[memory_index] =
                            similarity + 1.0e-12 >=
                                    options.geometry_memory_difference_key_similarity_threshold &&
                                (options.geometry_memory_difference_key_max_shape_diff < 0 ||
                                 shape_difference <=
                                    options.geometry_memory_difference_key_max_shape_diff)
                                ? 1
                                : 0;
                    }
                }
                cached = run_.geometry_memory_key_trigger_cache.emplace(
                    topology_key,
                    std::move(matches)).first;
            }
            key_trigger_matches = &cached->second;
        }
        for (int memory_index = 0;
             memory_index < static_cast<int>(run_.geometry_memory_scaled.size());
             ++memory_index) {
            const double effective = this->geometry_memory_effective_distance(
                scaled,
                run_.geometry_memory_scaled[static_cast<std::size_t>(memory_index)]);
            const bool geometry_trigger =
                !options.geometry_memory_difference_near_only || effective < 1.0;
            const bool same_key =
                !topology_key.empty() &&
                aligned_memory_keys &&
                options.geometry_memory_topology_keys[
                    static_cast<std::size_t>(memory_index)] == topology_key;
            const bool family_key_match =
                key_trigger_matches != nullptr &&
                (*key_trigger_matches)[static_cast<std::size_t>(memory_index)] != 0;
            const bool exact_key_trigger =
                options.geometry_memory_difference_key_trigger && same_key &&
                (!options.geometry_memory_exact_escape_pulse ||
                 run_.geometry_memory_exact_escape_active);
            const bool family_key_trigger =
                options.geometry_memory_difference_key_trigger &&
                family_key_match && !same_key &&
                (!options.geometry_memory_family_escape_pulse ||
                 run_.geometry_memory_family_escape_active);
            const bool key_trigger = exact_key_trigger || family_key_trigger;
            if (!geometry_trigger && !key_trigger) {
                continue;
            }
            any_geometry_trigger = any_geometry_trigger || geometry_trigger;
            any_key_trigger = any_key_trigger || key_trigger;
            any_exact_key_trigger =
                any_exact_key_trigger || exact_key_trigger;
            any_family_key_trigger =
                any_family_key_trigger || family_key_trigger;
            if (options.geometry_memory_difference_select_nearest) {
                if (key_trigger) {
                    const bool closer =
                        effective < nearest_key_effective - 1.0e-12;
                    const bool same_distance_key_tiebreak =
                        std::abs(effective - nearest_key_effective) <= 1.0e-12 &&
                        same_key && !nearest_key_same_key;
                    if (closer || same_distance_key_tiebreak) {
                        nearest_key_index = memory_index;
                        nearest_key_effective = effective;
                        nearest_key_same_key = same_key;
                    }
                } else if (geometry_trigger) {
                    const bool closer =
                        effective < nearest_geometry_effective - 1.0e-12;
                    const bool same_distance_key_tiebreak =
                        std::abs(effective - nearest_geometry_effective) <= 1.0e-12 &&
                        same_key && !nearest_geometry_same_key;
                    if (closer || same_distance_key_tiebreak) {
                        nearest_geometry_index = memory_index;
                        nearest_geometry_effective = effective;
                        nearest_geometry_same_key = same_key;
                    }
                }
                continue;
            }
            double weight = 1.0;
            if (same_key) {
                weight = options.geometry_memory_same_key_weight;
            }
            hit_indices.push_back(memory_index);
            hit_weights.push_back(weight);
            weight_sum += weight;
        }
        stats.geometry_memory_difference_seconds += seconds_since(distance_start);
        if (options.geometry_memory_difference_select_nearest) {
            const int nearest_index =
                nearest_key_index >= 0 ? nearest_key_index : nearest_geometry_index;
            if (nearest_index < 0) {
                return -1;
            }
            ++stats.geometry_memory_difference_hit;
            if (any_geometry_trigger) {
                ++stats.geometry_memory_difference_geometry_trigger;
            }
            if (any_key_trigger) {
                ++stats.geometry_memory_difference_key_trigger;
            }
            if (any_exact_key_trigger) {
                ++stats.geometry_memory_difference_exact_key_trigger;
            }
            if (any_family_key_trigger) {
                ++stats.geometry_memory_difference_family_key_trigger;
            }
            const bool nearest_same_key = nearest_key_index >= 0
                                              ? nearest_key_same_key
                                              : nearest_geometry_same_key;
            if (nearest_same_key) {
                ++stats.geometry_memory_difference_same_key;
            }
            return nearest_index;
        }
        if (hit_indices.empty() || !(weight_sum > 0.0)) {
            return -1;
        }
        ++stats.geometry_memory_difference_hit;
        if (any_geometry_trigger) {
            ++stats.geometry_memory_difference_geometry_trigger;
        }
        if (any_key_trigger) {
            ++stats.geometry_memory_difference_key_trigger;
        }
        if (any_exact_key_trigger) {
            ++stats.geometry_memory_difference_exact_key_trigger;
        }
        if (any_family_key_trigger) {
            ++stats.geometry_memory_difference_family_key_trigger;
        }
        double draw = run_.uni(run_.rng) * weight_sum;
        int selected = hit_indices.back();
        for (std::size_t index = 0; index < hit_indices.size(); ++index) {
            draw -= hit_weights[index];
            if (draw <= 0.0) {
                selected = hit_indices[index];
                break;
            }
        }
        if (!topology_key.empty() &&
            options.geometry_memory_topology_keys.size() ==
                run_.geometry_memory_scaled.size() &&
            options.geometry_memory_topology_keys[
                static_cast<std::size_t>(selected)] == topology_key) {
            ++stats.geometry_memory_difference_same_key;
        }
        return selected;
    }

int AdaptiveSession::population_geometry_memory_difference_index(int population_index, PBestGuideStats& stats) {
        if (population_index < 0 || population_index >= run_.pop_size) {
            return -1;
        }
        if (run_.geometry_memory_difference_index_cache.size() ==
                static_cast<std::size_t>(run_.pop_size) &&
            run_.geometry_memory_difference_index_cache[
                static_cast<std::size_t>(population_index)] >= -1) {
            return run_.geometry_memory_difference_index_cache[
                static_cast<std::size_t>(population_index)];
        }
        const double* scaled = run_.population.data() +
            static_cast<std::size_t>(population_index) * run_.dim;
        const int selected = this->choose_geometry_memory_difference_index(
            scaled,
            run_.fitness[static_cast<std::size_t>(population_index)].topology_key,
            stats);
        if (run_.geometry_memory_difference_index_cache.size() ==
            static_cast<std::size_t>(run_.pop_size)) {
            run_.geometry_memory_difference_index_cache[
                static_cast<std::size_t>(population_index)] = selected;
        }
        return selected;
    }

int AdaptiveSession::select_geometry_memory_difference_index(int target_index, const PBestGuideSelection& pbest_guide, PBestGuideStats& stats) {
        int selected = this->population_geometry_memory_difference_index(
            target_index,
            stats);
        if (selected >= 0) {
            return selected;
        }
        if (pbest_guide.population_index >= 0) {
            return this->population_geometry_memory_difference_index(
                pbest_guide.population_index,
                stats);
        }
        if (pbest_guide.scaled_values &&
            pbest_guide.scaled_values->size() == static_cast<std::size_t>(run_.dim)) {
            return this->choose_geometry_memory_difference_index(
                pbest_guide.scaled_values->data(),
                "",
                stats);
        }
        return -1;
    }

const std::vector<double>* AdaptiveSession::geometry_memory_qohr_family_similarities(const std::string& topology_key) {
        if (options.geometry_memory_qohr_family_weight <= 0.0 ||
            topology_key.empty() ||
            run_.geometry_memory_parsed_keys.size() != run_.geometry_memory_scaled.size()) {
            return nullptr;
        }
        auto cached =
            run_.geometry_memory_qohr_family_similarity_cache.find(topology_key);
        if (cached != run_.geometry_memory_qohr_family_similarity_cache.end()) {
            return &cached->second;
        }
        const ParsedStructuralTopologyKey parsed =
            parse_structural_topology_key(topology_key);
        std::vector<double> similarities(
            run_.geometry_memory_scaled.size(),
            0.0);
        for (std::size_t memory_index = 0;
             memory_index < similarities.size();
             ++memory_index) {
            const int shape_difference =
                structural_topology_shape_diff_count_parsed(
                    parsed,
                    run_.geometry_memory_parsed_keys[memory_index]);
            if (options.geometry_memory_qohr_family_max_shape_diff >= 0 &&
                shape_difference >
                    options.geometry_memory_qohr_family_max_shape_diff) {
                continue;
            }
            similarities[memory_index] =
                weighted_structural_family_similarity_parsed(
                    parsed,
                    run_.geometry_memory_parsed_keys[memory_index]);
        }
        cached = run_.geometry_memory_qohr_family_similarity_cache.emplace(
            topology_key,
            std::move(similarities)).first;
        return &cached->second;
    }

} } // namespace optiland::optimizer_detail
