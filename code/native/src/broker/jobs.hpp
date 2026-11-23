#pragma once
#include "optiland/optimizer.hpp"
#include "optiland/population_broker.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace lensde::broker {
namespace fs = std::filesystem;

struct Job {
    int island_id = -1;
    std::string system_path;
    std::string variables_path;
    std::string merit_path;
    std::string population_path;
    std::string snapshot_dir;
    std::string output_dir;
    unsigned int seed = 1;
    int population = 100;
    int generations = 400;
    std::string profile = "checkpoint";
    std::string internal_scaling = "edge_sag_normalized";
    std::string glass_search_path;
    bool has_trace_contract = false;
    std::string constraint_handling = "feasibility_rules";
    bool allow_near_trace_infeasible = true;
    double trace_coverage_epsilon = 0.03;
};

struct LoadedJob {
    Job job;
    optiland::LensSystem system;
    std::vector<optiland::VariableSpec> variables;
    optiland::MeritConfig merit;
    optiland::GlassSearchContext glass;
    std::vector<std::vector<double>> initial_population;
};

struct RunRecord {
    optiland::OptimizationResult result;
    std::string error;
};
} // namespace lensde::broker
