#pragma once
#include "cli_internal.hpp"
#include "command_context.hpp"

namespace lensde { namespace cli { namespace detail {

struct OptimizationCommand {
    explicit OptimizationCommand(CommandContext& context) : argc(context.argc),
        argv(context.argv),
        lens(context.lens),
        optimizer_asphere_root_policy(context.optimizer_asphere_root_policy),
        input_clear_apertures_materialized(context.input_clear_apertures_materialized),
        update_input_clear_apertures(context.update_input_clear_apertures),
        merit(context.merit),
        run_adaptive_hybrid(context.run_adaptive_hybrid),
        run_hybrid(context.run_hybrid),
        score_population_only(context.score_population_only),
        score_population_detail_index(context.score_population_detail_index),
        run_de(context.run_de),
        run_local(context.run_local),
        run_polish(context.run_polish),
        allow_cuda_for_merit(context.allow_cuda_for_merit),
        population(context.population),
        popsize(context.popsize),
        generations(context.generations),
        de_cuda_min_batch_size(context.de_cuda_min_batch_size),
        out_dir(context.out_dir),
        internal_scaling_explicit(context.internal_scaling_explicit),
        internal_scaling(context.internal_scaling),
        asphere_reference_policy(context.asphere_reference_policy),
        local_bounds_mode(context.local_bounds_mode),
        initial_score(context.initial_score),
        final_optimizer_score(context.final_optimizer_score),
        final_optimizer_score_available(context.final_optimizer_score_available),
        optimizer_input_lens(context.optimizer_input_lens) {}

    decltype(CommandContext::argc)& argc;
    decltype(CommandContext::argv)& argv;
    decltype(CommandContext::lens)& lens;
    decltype(CommandContext::optimizer_asphere_root_policy)& optimizer_asphere_root_policy;
    decltype(CommandContext::input_clear_apertures_materialized)& input_clear_apertures_materialized;
    decltype(CommandContext::update_input_clear_apertures)& update_input_clear_apertures;
    decltype(CommandContext::merit)& merit;
    decltype(CommandContext::run_adaptive_hybrid)& run_adaptive_hybrid;
    decltype(CommandContext::run_hybrid)& run_hybrid;
    decltype(CommandContext::score_population_only)& score_population_only;
    decltype(CommandContext::score_population_detail_index)& score_population_detail_index;
    decltype(CommandContext::run_de)& run_de;
    decltype(CommandContext::run_local)& run_local;
    decltype(CommandContext::run_polish)& run_polish;
    decltype(CommandContext::allow_cuda_for_merit)& allow_cuda_for_merit;
    decltype(CommandContext::population)& population;
    decltype(CommandContext::popsize)& popsize;
    decltype(CommandContext::generations)& generations;
    decltype(CommandContext::de_cuda_min_batch_size)& de_cuda_min_batch_size;
    decltype(CommandContext::out_dir)& out_dir;
    decltype(CommandContext::internal_scaling_explicit)& internal_scaling_explicit;
    decltype(CommandContext::internal_scaling)& internal_scaling;
    decltype(CommandContext::asphere_reference_policy)& asphere_reference_policy;
    decltype(CommandContext::local_bounds_mode)& local_bounds_mode;
    decltype(CommandContext::initial_score)& initial_score;
    decltype(CommandContext::final_optimizer_score)& final_optimizer_score;
    decltype(CommandContext::final_optimizer_score_available)& final_optimizer_score_available;
    decltype(CommandContext::optimizer_input_lens)& optimizer_input_lens;
    std::vector<VariableSpec> variables{};
    GlassSearchContext glass_search{};
    std::vector<std::vector<double>> initial_population_values{};
    LensSystem optimization_base{};
    std::vector<VariableSpec> local_variables{};
    const std::vector<VariableSpec>* result_variables{};
    OptimizationResult result{};
    bool de_used_cuda{};
    CheckpointAdaptiveGlassState latest_adaptive_glass_checkpoint_state{};
    HybridTraceEvent latest_checkpoint_trace_event{};
    bool latest_checkpoint_trace_event_seen{};
    LensSystem last_adaptive_validated_system{};
    Score last_adaptive_validated_score{};
    std::vector<double> last_adaptive_validated_values{};
    int last_adaptive_validated_generation{};
    bool last_adaptive_validation_valid{};
    bool checkpoint_inherit_asphere_root_policy{};
    CheckpointEvaluationMode checkpoint_evaluation_mode{};
    bool checkpoint_dependency_diagnostics{};
    MaterializationTraceContract materialization_trace_contract{};
    std::vector<double> initial_checkpoint_values{};
    std::chrono::steady_clock::time_point complete_de_started{};
    bool final_matches_adaptive_boundary{};
    Score optimizer_materialized_score{};
    bool final_materialization_used_checkpoint{};
    const std::vector<VariableSpec>* primary_variables_source{};
    AdaptiveHybridDeDlsOptions adaptive_options{};
    int expected_total_de_generations{};
    int adaptive_checkpoint_every{};
    std::string adaptive_search_purpose{};
    std::vector<double> last_adaptive_persisted_values{};
    std::optional<DifferentialEvolution> optimizer_storage;
    bool checkpoint_progress_with_variables(
            const std::vector<VariableSpec>& checkpoint_variables,
            const std::string& phase,
            int step,
            const Score& best,
            const std::vector<double>& best_values,
            bool apertures_already_materialized = false,
            const LensSystem* prevalidated_system = nullptr,
            const Score* prevalidated_score = nullptr,
            LensSystem* validated_system_out = nullptr,
            Score* validated_score_out = nullptr);
    void checkpoint_progress(const std::string& phase, int step, const Score& best,
                                       const std::vector<double>& best_values);
    int adaptive_de_options();
    int adaptive_constraint_options();
    int adaptive_stop_options();
    int adaptive_material_options();
    int adaptive_avoidance_options();
    int adaptive_memory_mutation_options();
    int adaptive_memory_escape_options();
    int adaptive_memory_input();
    int adaptive_basin_options();
    int adaptive_niche_options();
    int adaptive_focus_options();
    int adaptive_plateau_options();
    int adaptive_refresh_options();
    int adaptive_polish_options();
    void print_adaptive_population();
    void print_adaptive_micro_islands();
    void print_adaptive_memory();
    void print_adaptive_constraints();
    void print_adaptive_generation();
    void print_adaptive_basins();
    void print_adaptive_niches();
    void print_adaptive_plateau();
    void print_adaptive_termination();
    int adaptive_console();
    int adaptive_callbacks();
    int adaptive_execute();
    int run_adaptive();
    int execute_hybrid();
    int execute_de();
    int prepare_variables_and_glass();
    int prepare_population();
    int prepare_optimizer();
    int configure_checkpoint();
    int checkpoint_initial_state();
    int run_algorithm();
    int polish();
    int materialize();
    int guard_materialization();
    int save_optimizer_result();
    int execute();
};

} } }
