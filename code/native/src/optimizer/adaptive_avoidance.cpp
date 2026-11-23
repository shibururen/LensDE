/*
 * Match proposed structures against topology and continuous-geometry avoidance memories.
 * Adaptive scoring shares immutable avoidance inputs and mutex-protected lookup caches in run_.
 * Keep structural and parameter-match semantics identical across worker and serial scoring paths.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

bool AdaptiveSession::structural_key_like(const std::string& value) {
        return !value.empty() &&
               value[0] == 'e' &&
               value.find("|l") != std::string::npos;
    }

bool AdaptiveSession::compute_avoided_topology_key_match(const std::string& key) {
        if (key.empty() || run_.avoided_topology_key_set.empty()) {
            return false;
        }
        if (run_.avoided_topology_key_set.find(key) != run_.avoided_topology_key_set.end()) {
            return true;
        }
        for (std::size_t delimiter = key.find('|');
             delimiter != std::string::npos;
             delimiter = key.find('|', delimiter + 1)) {
            if (run_.avoided_topology_key_set.find(key.substr(0, delimiter)) !=
                run_.avoided_topology_key_set.end()) {
                return true;
            }
        }
        if (options.avoided_topology_similarity_threshold > 0.0 &&
            this->structural_key_like(key)) {
            const ParsedStructuralTopologyKey parsed_key =
                parse_structural_topology_key(key);
            for (const ParsedStructuralTopologyKey& avoided :
                 run_.avoided_structural_topology_keys) {
                const double similarity =
                    weighted_structural_family_similarity_parsed(parsed_key, avoided);
                if (similarity >= options.avoided_topology_similarity_threshold) {
                    const int max_shape_diff = options.avoided_topology_max_shape_diff;
                    if (max_shape_diff < 0 ||
                        structural_topology_shape_diff_count_parsed(parsed_key, avoided) <=
                            max_shape_diff) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

bool AdaptiveSession::avoided_topology_key_matches(const std::string& key) {
        if (key.empty() || run_.avoided_topology_key_set.empty()) {
            return false;
        }
        {
            const std::lock_guard<PortableMutex> lock(run_.avoided_topology_match_cache_mutex);
            const auto cached = run_.avoided_topology_match_cache.find(key);
            if (cached != run_.avoided_topology_match_cache.end()) {
                return cached->second;
            }
        }
        const bool matches = this->compute_avoided_topology_key_match(key);
        {
            const std::lock_guard<PortableMutex> lock(run_.avoided_topology_match_cache_mutex);
            const auto inserted = run_.avoided_topology_match_cache.emplace(key, matches);
            return inserted.first->second;
        }
    }

ContinuousAvoidMatch AdaptiveSession::compute_avoided_continuous_match(const std::string& key) {
        ContinuousAvoidMatch best;
        if (!options.avoided_topology_continuous_structure || key.empty() ||
            run_.avoided_continuous_structure_signatures.empty()) {
            return best;
        }
        ContinuousStructureSignature candidate;
        if (!parse_continuous_structure_signature(key, &candidate)) {
            return best;
        }
        for (const ContinuousStructureSignature& avoided :
             run_.avoided_continuous_structure_signatures) {
            const ContinuousAvoidMatch match =
                continuous_parameter_auxiliary_match(candidate, avoided, options);
            if (match.score > best.score) {
                best = match;
            }
            if (match.matched) {
                return match;
            }
        }
        return best;
    }

ContinuousAvoidMatch AdaptiveSession::avoided_continuous_structure_match(const std::string& key) {
        if (!options.avoided_topology_continuous_structure || key.empty() ||
            run_.avoided_continuous_structure_signatures.empty()) {
            return ContinuousAvoidMatch{};
        }
        {
            const std::lock_guard<PortableMutex> lock(
                run_.avoided_continuous_match_cache_mutex);
            const auto cached = run_.avoided_continuous_match_cache.find(key);
            if (cached != run_.avoided_continuous_match_cache.end()) {
                return cached->second;
            }
        }
        const ContinuousAvoidMatch match =
            this->compute_avoided_continuous_match(key);
        {
            const std::lock_guard<PortableMutex> lock(
                run_.avoided_continuous_match_cache_mutex);
            const auto inserted =
                run_.avoided_continuous_match_cache.emplace(key, match);
            return inserted.first->second;
        }
    }

void AdaptiveSession::apply_topology_avoidance(CandidateFitness& fitness, int generation) {
        if (options.avoided_topology_penalty <= 0.0 ||
            run_.avoided_topology_key_set.empty() ||
            generation < std::max(0, options.avoided_topology_start_generation)) {
            return;
        }
        if (this->avoided_topology_key_matches(fitness.topology_key) ||
            this->avoided_topology_key_matches(fitness.semantic_key) ||
            this->avoided_topology_key_matches(fitness.descriptor_key)) {
            fitness.avoided_topology = true;
            fitness.avoidance_penalty = options.avoided_topology_penalty;
            fitness.avoidance_match_channel = "discrete_key_or_family";
            fitness.avoidance_match_score = 1.0;
        } else {
            const ContinuousAvoidMatch auxiliary_match =
                this->avoided_continuous_structure_match(
                    fitness.continuous_topology_signature);
            if (auxiliary_match.matched) {
                fitness.avoided_topology = true;
                fitness.avoidance_penalty = options.avoided_topology_penalty;
                fitness.avoidance_match_channel = "parameter_auxiliary";
                fitness.avoidance_match_score = auxiliary_match.score;
            }
        }
    }

} } // namespace optiland::optimizer_detail
