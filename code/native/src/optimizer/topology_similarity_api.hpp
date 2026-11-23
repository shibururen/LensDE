#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

ParsedStructuralTopologyKey parse_structural_topology_key(const std::string& key);

double topology_similarity_char_cost(char a, char b, double weak_cost);

int structural_topology_shape_diff_count_parsed(
    const ParsedStructuralTopologyKey& a,
    const ParsedStructuralTopologyKey& b);

int structural_topology_shape_diff_count(const std::string& a_key,
                                         const std::string& b_key);

double weighted_structural_topology_similarity(const std::string& a_key,
                                               const std::string& b_key);

double weighted_structural_family_similarity_parsed(
    const ParsedStructuralTopologyKey& a,
    const ParsedStructuralTopologyKey& b);

double weighted_structural_family_similarity(const std::string& a_key,
                                             const std::string& b_key);

} }  // namespace optiland::optimizer_detail
