#pragma once
#include <limits>
#include <string>

namespace optiland {
struct MicroIslandTraceRecord {
    int epoch = 0;
    int island_index = 0;
    int source_basin_id = -1;
    std::string source_key;
    std::string source_topology_key;
    std::string source_descriptor_key;
    int source_similarity_family_id = 0;
    double source_merit = std::numeric_limits<double>::quiet_NaN();
    int planned_generations = 0;
    int actual_generations = 0;
    bool early_stopped = false;
    bool retired = false;
    bool retired_unique = false;
    std::string retire_action;
    std::string retire_key;
    int survivor_rank = -1;
    double survivor_merit = std::numeric_limits<double>::quiet_NaN();
    bool survivor_trace_feasible = false;
    std::string survivor_topology_key;
    std::string survivor_descriptor_key;
    std::string archive_source;
    std::string basin_archive_action;
    int basin_archive_id = -1;
    int nearest_basin_id = -1;
    double nearest_basin_distance = std::numeric_limits<double>::quiet_NaN();
    std::string previous_basin_source;
    bool population_replaced = false;
    int population_target_index = -1;
};


}  // namespace optiland
