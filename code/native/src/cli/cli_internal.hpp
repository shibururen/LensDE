#pragma once
#include "optiland/cli_arguments.hpp"
using namespace lensde::cli;
#include "optiland/analysis.hpp"
#include "optiland/fileio.hpp"
#include "optiland/optimizer.hpp"
#include "optiland/raytrace.hpp"
#include "optiland/solves.hpp"
#include "optiland/visualization.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#include <sys/stat.h>
#endif


#include <memory>
#include <optional>
#include <functional>
#include "lensde/cli/command.hpp"

namespace lensde { namespace cli { namespace detail {
using namespace optiland;

// Nonterminal command phases return this status; all exit codes are nonnegative.
inline constexpr int kContinueCommand = -1;

class ScopedAsphereRootPolicy {
public:
    explicit ScopedAsphereRootPolicy(AsphereRootPolicy policy)
        : previous_(asphere_root_policy()) {
        set_asphere_root_policy(policy);
    }

    ~ScopedAsphereRootPolicy() {
        set_asphere_root_policy(previous_);
    }

    ScopedAsphereRootPolicy(const ScopedAsphereRootPolicy&) = delete;
    ScopedAsphereRootPolicy& operator=(const ScopedAsphereRootPolicy&) = delete;

private:
    AsphereRootPolicy previous_;
};

struct MaterializationTraceContract {
    bool allow_near_trace_infeasible = false;
    double trace_coverage_epsilon = 0.0;
};

struct CheckpointAdaptiveGlassState {
    bool seen = false;
    std::string event;
    int epoch = 0;
    int de_generations = 0;
    bool enabled = false;
    bool updated = false;
    bool ready = true;
    bool trigger_ready = true;
    bool bridge_active = false;
    bool focus_frozen = false;
    bool stop_when_ready = false;
    std::string trigger_mode = "warmup";
    int focus_clear_count = 0;
    int focus_clear_target = 0;
    int sample_count = 0;
    int elite_count = 0;
    double previous = std::numeric_limits<double>::quiet_NaN();
    double current = std::numeric_limits<double>::quiet_NaN();
    double target = std::numeric_limits<double>::quiet_NaN();
    double metric = std::numeric_limits<double>::quiet_NaN();
};

enum class CheckpointEvaluationMode {
    Current,
    NoMid,
    PersistOnly,
    FixCandidate,
};

struct SeedPoolCandidate {
    std::vector<double> row;
    double merit = std::numeric_limits<double>::infinity();
};

std::string lensde_default_database();

void ensure_output_directory(const std::string& path);

std::string join_path(const std::string& dir, const std::string& name);

int clear_aperture_update_rings(const MeritConfig& merit);

bool score_is_materialization_valid(
    const Score& score,
    const MeritConfig& merit,
    const MaterializationTraceContract& trace_contract = {});

void sync_stop_aperture_to_system(LensSystem& lens);

double raw_current_variable_value_from_system(const LensSystem& lens, const VariableSpec& variable);

double current_variable_value_from_system(const LensSystem& lens, const VariableSpec& variable);

double asphere_stage_reference_radius(const LensSystem& lens, const VariableSpec& variable);

double asphere_radial_power(double radius, std::size_t term);

int rebase_asphere_edge_sag_variables(
    const LensSystem& lens,
    std::vector<VariableSpec>& variables);

std::vector<double> current_variable_values_from_system(
    const LensSystem& lens,
    const std::vector<VariableSpec>& variables);

void apply_local_auto_tune(PolishOptions& options, int argc, char** argv);

bool prepare_asphere_reference_variables(
    const LensSystem& lens,
    std::vector<VariableSpec>& variables,
    InternalScalingMode internal_scaling,
    AsphereReferencePolicy policy,
    std::string* error);

double finite_or(double value, double fallback);

void widen_bounds(VariableSpec& variable, double lower, double upper);

std::vector<VariableSpec> local_bounds_variables(
    const std::vector<VariableSpec>& variables,
    LocalBoundsMode mode);

void write_json_number_or_null(std::ostream& out, double value);

void write_score_checkpoint_summary(std::ostream& out, const Score& score, const std::string& indent);

void write_score_checkpoint_summary_compact(std::ostream& out, const Score& score);

void update_checkpoint_adaptive_glass_state(
    CheckpointAdaptiveGlassState& state,
    const HybridTraceEvent& event);

void write_checkpoint_adaptive_glass_state_json(
    std::ostream& out,
    const CheckpointAdaptiveGlassState& state);

double read_score_merit_from_json(const std::string& path);

std::string trim_ascii(std::string text);

std::vector<std::string> load_key_list_file(const std::string& path);

std::vector<std::string> load_aligned_key_list_file(const std::string& path);

std::vector<double> load_aligned_number_list_file(const std::string& path);

void write_checkpoint_active_trace_fields(
    std::ostream& out,
    const HybridTraceEvent* event);

const char* asphere_root_policy_token(AsphereRootPolicy policy);

const char* checkpoint_evaluation_mode_token(CheckpointEvaluationMode mode);

void write_checkpoint_aperture_state_json(
    std::ostream& out,
    const LensSystem& system);

bool write_checkpoint_dependency_snapshot(
    const LensSystem& input_system,
    const LensSystem& output_system,
    const std::vector<double>& values,
    const std::string& out_dir,
    const std::string& phase,
    int step,
    const std::string& action,
    bool validated);

bool persist_unverified_checkpoint_state(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values,
    const GlassSearchContext& glass_search,
    const std::string& out_dir,
    const std::string& phase,
    int step,
    const Score& optimizer_score,
    CheckpointEvaluationMode mode,
    bool dependency_diagnostics);

bool save_checkpoint(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit,
    const std::string& out_dir,
    const std::string& phase,
    int step,
    const Score& best_score,
    const MaterializationTraceContract& trace_contract,
    bool inherit_optimizer_root_policy,
    const CheckpointAdaptiveGlassState* adaptive_glass_state = nullptr,
    const HybridTraceEvent* active_trace_event = nullptr,
    bool apertures_already_materialized = false,
    const LensSystem* prevalidated_system = nullptr,
    const Score* prevalidated_score = nullptr,
    CheckpointEvaluationMode evaluation_mode = CheckpointEvaluationMode::Current,
    bool dependency_diagnostics = false,
    LensSystem* validated_system_out = nullptr,
    Score* validated_score_out = nullptr);

bool append_checkpoint_diagnostic_event(
    const std::string& out_dir,
    const std::string& phase,
    const HybridTraceEvent& event);

void print_de_timing_detail(const OptimizationResult& result);

double ns_to_seconds(long long ns);

void write_diag_int(std::ostream& out, const char* name, long long value, bool* first);

void write_diag_seconds(std::ostream& out, const char* name, long long ns, bool* first);

double diag_residual_value(long long scaled);

double diag_residual_mean(long long sum_scaled, long long count);

void write_diag_number(std::ostream& out, const char* name, double value, bool* first);

void write_ray_aiming_diagnostics_json(
    const RayAimingDiagnosticsSnapshot& diag,
    const std::string& path);

void print_ray_aiming_diagnostics(const std::string& out_dir);

const char* ray_failure_code_csv_label(RayFailureCode code);

void write_csv_number(std::ostream& out, double value);

void save_chief_aiming_probe_csv(
    const LensSystem& lens,
    const std::string& path,
    int max_budget_iterations);

void save_three_step_chief_probe_csv(
    const LensSystem& lens,
    const std::string& path);

void save_calibrated_edge_initial_probe_csv(
    const LensSystem& lens,
    const std::string& path);

void save_center_edge_iterative_probe_csv(
    const LensSystem& lens,
    const std::string& path);

std::string json_escape_local(const std::string& value);

std::string hex_hash_local(std::uint64_t value);

void write_json_number_local(std::ostream& out, double value);

void write_active_niche_best_merits_json(
    std::ostream& out,
    const std::vector<ActiveNicheMeritSnapshot>& snapshots);

std::string normalized_token_local(std::string text);

bool distortion_model_allows_cuda_local(const MeritConfig& merit);

bool de_cuda_required_local();

void write_score_summary_json(std::ostream& out, const Score& score);

bool append_trace_jsonl(
    const std::string& out_dir,
    const std::string& filename,
    const HybridTraceEvent& event);

bool append_hybrid_trace_jsonl(const std::string& out_dir, const HybridTraceEvent& event);

bool append_family_transition_audit_jsonl(
    const std::string& out_dir,
    const FamilyTransitionAuditEvent& event);

bool load_population_csv(
    const std::string& path,
    int dim,
    std::vector<std::vector<double>>* rows,
    std::string* error);

bool save_population_csv(const OptimizationResult& result, const std::string& path);

std::vector<std::string> split_csv_line_simple(const std::string& line);

bool parse_double_cell(const std::string& cell, double* out);

bool parse_bool_cell(const std::string& cell, bool fallback = true);

bool parse_numeric_seed_row_line(
    const std::string& line,
    int dim,
    std::vector<double>* row);

bool seed_asphere_q_coordinate_cpp(
    const VariableSpec& variable,
    double* radial_power = nullptr);

double seed_axis_coordinate_cpp(double physical, const VariableSpec& variable);

std::pair<double, double> seed_axis_bounds_cpp(const VariableSpec& variable);

double seed_axis_span_cpp(const VariableSpec& variable);

double seed_axis_normalized_cpp(double physical, const VariableSpec& variable);

std::string seed_row_key_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables);

double seed_activity_tolerance_cpp(const VariableSpec& variable);

std::string row_curvature_mask_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables,
    double tol = -1.0);

int active_count_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables,
    double tol = -1.0);

std::vector<int> active_indices_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables,
    double tol = -1.0);

int active_curvature_count_cpp(
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables);

bool load_seed_candidate_pool_csv(
    const std::string& path,
    int dim,
    std::vector<SeedPoolCandidate>* candidates,
    std::string* error);

bool write_rows_csv(const std::string& path, const std::vector<std::vector<double>>& rows);

std::vector<std::vector<double>> select_seed_population_cpp(
    std::vector<SeedPoolCandidate> candidates,
    const std::vector<VariableSpec>& variables,
    int target,
    unsigned int seed,
    double min_active_fraction,
    double min_curvature_active_fraction,
    double* selection_seconds,
    int* deduped_count);

void write_selection_summary_json(
    const std::string& path,
    int input_count,
    int deduped_count,
    int target,
    const std::vector<std::vector<double>>& selected,
    const std::vector<VariableSpec>& variables,
    double min_active_fraction,
    double min_curvature_active_fraction,
    double selection_seconds,
    double write_seconds);

int population_score_worker_count(int rows);

Score evaluate_population_score_candidate(
    const OptimizationResult& result,
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit,
    int i);

std::vector<Score> evaluate_population_scores_cpu(
    const OptimizationResult& result,
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit);

bool save_population_scores_csv(
    const OptimizationResult& result,
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit,
    const std::string& path,
    bool prefer_cuda = false,
    CudaEvaluationTiming* cuda_timing = nullptr,
    bool* used_cuda = nullptr);

bool save_basin_archive_csv(const OptimizationResult& result, const std::string& path);

bool save_basin_archive_json(const OptimizationResult& result, const std::string& path);

LensSystem make_demo_lens();

std::vector<VariableSpec> make_demo_variables(const LensSystem& lens);

MeritConfig make_demo_merit();

void print_command_help(std::ostream& output);

void apply_refresh_generation_args(int argc, char** argv, PopulationGenerationOptions& options);

void apply_initial_generation_args(int argc, char** argv, PopulationGenerationOptions& options);

void apply_population_generation_target_defaults(
    PopulationGenerationOptions& options,
    const MeritConfig& merit,
    const LensSystem& lens);

bool load_refresh_seed_pool(
    PopulationGenerationOptions& options,
    int dim,
    std::ostream& err);

bool run_asphere_intersection_self_test();

bool run_material_scaling_self_test();

void print_score(const std::string& label, const Score& score);

} } }
