#pragma once
#include "jobs.hpp"
#include "optiland/fileio.hpp"
#include "optiland/analysis.hpp"
#include "optiland/optimizer.hpp"
#include "optiland/population_broker.hpp"
#include "optiland/raytrace.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>


namespace lensde::broker {

std::vector<std::string> split_tab(const std::string& line);

std::vector<Job> load_jobs(const std::string& path);

bool load_population_csv(
    const std::string& path,
    int dim,
    std::vector<std::vector<double>>* rows,
    std::string* error);

LoadedJob load_job(Job job);

optiland::AdaptiveHybridDeDlsOptions checkpoint_options(const LoadedJob& loaded);

void save_population(const optiland::OptimizationResult& result, const fs::path& path);

void save_merits(const optiland::OptimizationResult& result, const fs::path& path);

void save_result(const LoadedJob& loaded, const RunRecord& record);

void save_broker_stats(
    const optiland::BatchedPopulationBrokerStats& stats,
    const fs::path& path);

std::string arg_value(int argc, char** argv, const std::string& name);

bool has_arg(int argc, char** argv, const std::string& name);

std::vector<std::string> split_fixture_field(
    const std::string& value,
    char delimiter);

std::vector<double> parse_fixture_vector(const std::string& value);

int run_asphere_oracle_fixture(
    const std::string& fixture_path,
    const std::string& output_path);

std::string normalized_option_token(std::string value);

optiland::AsphereRootPolicy parse_asphere_root_policy(const std::string& value);

const char* asphere_root_policy_name(optiland::AsphereRootPolicy policy);

void set_process_environment(const char* name, const std::string& value);

std::string configure_calibrated_builder_schedule(const std::string& value);

std::string configure_transpose_ray_tasks(const std::string& value);

void run_island(const LoadedJob& item, RunRecord& record, const std::shared_ptr<optiland::CudaBatchedPopulationBroker>& broker);
void execute_islands(const std::vector<LoadedJob>& loaded, std::vector<RunRecord>& records, const std::shared_ptr<optiland::CudaBatchedPopulationBroker>& broker, bool batched);
std::vector<LoadedJob> load_islands(std::vector<Job>& jobs);
std::shared_ptr<optiland::CudaBatchedPopulationBroker> make_broker(int argc, char** argv, const std::vector<LoadedJob>& loaded, bool batched);
bool save_all_results(const std::vector<LoadedJob>& loaded, const std::vector<RunRecord>& records);
int run_application(int argc, char** argv);
} // namespace lensde::broker
