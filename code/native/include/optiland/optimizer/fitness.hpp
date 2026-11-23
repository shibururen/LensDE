#pragma once
#include "optiland/core.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace optiland {
struct CandidateFitness {
    Score score;
    double objective = 0.0;
    double trace_violation = 0.0;
    double constraint_violation = 0.0;
    double first_order_violation = 0.0;
    double image_objective = 0.0;
    double total_violation = 0.0;
    bool trace_feasible = false;
    bool constraint_feasible = false;
    bool first_order_feasible = true;
    bool feasible = false;
    int generation = 0;
    int population_index = -1;
    bool polished = false;
    std::string source;
    std::string topology_key;
    std::string semantic_key;
    std::string descriptor_key;
    // Versioned continuous optical-structure signature used only by the
    // cross-Outer avoid channel. Unlike the structural key, it retains
    // normalized curvature, element-power, and axial-spacing coordinates so
    // weak-power bucket boundaries cannot hide a near duplicate.
    std::string continuous_topology_signature;
    std::string descriptor_diagnostic;
    std::vector<std::uint64_t> descriptor_axis_values;
    bool avoided_topology = false;
    double avoidance_penalty = 0.0;
    std::string avoidance_match_channel;
    // 1.0 for exact curvature-sign skeleton matches; otherwise the single
    // weighted parameter-auxiliary similarity that triggered avoidance.
    double avoidance_match_score = 0.0;
};


}  // namespace optiland
