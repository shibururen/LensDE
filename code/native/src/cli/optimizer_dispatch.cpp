// Dispatch ordinary DE, hybrid, or adaptive optimization and assemble their native option objects.
// The command execution sequence then applies polish, materialization, guards, and result output.
// Keep mode precedence, seeds, initialization settings, and progress callbacks attached to the selected solver.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::run_algorithm() {
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;
    auto& optimizer = *optimizer_storage;
    if (run_adaptive_hybrid) return run_adaptive();
    else if (run_hybrid) return execute_hybrid();
    else if (run_de) return execute_de();

                result.best_values = current_variable_values_from_system(
                    optimizer_input_lens,
                    primary_variables);
                LensSystem candidate = apply_mixed_variables(
                    optimizer_input_lens,
                    primary_variables,
                    result.best_values,
                    glass_search);
                // With no optimizer, optimizer_input_lens already carries the
                // aperture state prepared during input loading.  Updating here
                // would materialize the same unchanged geometry a second time.
                if (!merit.dynamic_apertures) {
                    sync_stop_aperture_to_system(candidate);
                }
                result.best_score = evaluate_merit(candidate, merit);

    return kContinueCommand;
}

int OptimizationCommand::execute() {
    if (const int status = prepare_variables_and_glass(); status != kContinueCommand) return status;
    if (const int status = prepare_population(); status != kContinueCommand) return status;
    if (const int status = prepare_optimizer(); status != kContinueCommand) return status;
    if (const int status = configure_checkpoint(); status != kContinueCommand) return status;
    if (const int status = checkpoint_initial_state(); status != kContinueCommand) return status;
    if (const int status = run_algorithm(); status != kContinueCommand) return status;
    if (const int status = polish(); status != kContinueCommand) return status;
    if (const int status = materialize(); status != kContinueCommand) return status;
    if (const int status = guard_materialization(); status != kContinueCommand) return status;
    if (const int status = save_optimizer_result(); status != kContinueCommand) return status;
    return kContinueCommand;
}

int OptimizationCommand::execute_de() {
    auto& optimizer = *optimizer_storage;

                DifferentialEvolutionOptions options;
                options.population_size = population;
                options.popsize_multiplier = popsize;
                options.use_popsize_multiplier = has_arg(argc, argv, "--popsize");
                options.max_generations = generations;
                options.mutation_min = double_arg(argc, argv, "--mutation-min", options.mutation_min);
                options.mutation_max = double_arg(argc, argv, "--mutation-max", options.mutation_max);
                options.recombination = double_arg(argc, argv, "--recombination", options.recombination);
                options.tolerance = double_arg(argc, argv, "--de-tol", options.tolerance);
                options.absolute_tolerance = double_arg(argc, argv, "--de-atol", options.absolute_tolerance);
                options.strategy = string_arg(argc, argv, "--de-strategy", options.strategy);
                options.seed = static_cast<unsigned int>(int_arg(argc, argv, "--seed", 20260515));
                options.use_cuda = allow_cuda_for_merit;
                if (glass_search.enabled) {
                    options.glass_mutation_rate =
                        double_arg(argc, argv, "--glass-mutation-rate", glass_search.mutation_rate);
                    options.glass_random_rate =
                        double_arg(argc, argv, "--glass-random-rate", glass_search.random_rate);
                    options.glass_pbest_inherit_rate =
                        double_arg(argc, argv, "--glass-pbest-inherit-rate", glass_search.pbest_inherit_rate);
                    options.glass_neighbor_count =
                        int_arg(argc, argv, "--glass-neighbor-count", glass_search.neighbor_count);
                    options.glass_local_search =
                        glass_search.local_search && !has_arg(argc, argv, "--glass-no-local-search");
                }
                options.progress_every = int_arg(argc, argv, "--progress-every", std::max(1, generations / 8));
                options.initial_population_values = initial_population_values;
                options.internal_scaling = internal_scaling;
                apply_initial_generation_args(argc, argv, options.initial_generation);
                apply_population_generation_target_defaults(
                    options.initial_generation,
                    merit,
                    lens);

                std::cout << "CUDA evaluator available: "
                          << (evaluate_population_cuda_available() ? "yes" : "no") << "\n";
                std::cout << "Variables: " << variables.size()
                          << ", population: "
                          << (options.use_popsize_multiplier
                                  ? options.popsize_multiplier * population_sizing_dimension(variables)
                                  : options.population_size)
                          << ", generations: " << options.max_generations
                          << ", strategy: " << options.strategy
                          << ", initial generation: "
                          << population_generation_mode_name(options.initial_generation.mode)
                          << ", glass search: "
                          << (glass_search.enabled ? "yes" : "no")
                          << ", internal scaling: " << internal_scaling_name(options.internal_scaling)
                          << "\n";
                if (!initial_population_values.empty()) {
                    std::cout << "Initial population seeds: "
                              << initial_population_values.size() << "\n";
                }

                result = optimizer.run(options, [&](int gen, const Score& best,
                                                    const std::vector<double>& best_values) {
                    std::cout << "  gen " << gen << " best merit " << best.merit
                              << " mean spot " << best.mean_spot << "\n";
                    checkpoint_progress("de", gen, best, best_values);
                });
                de_used_cuda = result.used_cuda;

    return kContinueCommand;
}

int OptimizationCommand::execute_hybrid() {
    auto& optimizer = *optimizer_storage;

                HybridDeDlsOptions hybrid_options;
                hybrid_options.de.population_size = population;
                hybrid_options.de.popsize_multiplier = popsize;
                hybrid_options.de.use_popsize_multiplier = has_arg(argc, argv, "--popsize");
                hybrid_options.de.max_generations = int_arg(argc, argv, "--hybrid-de-gen",
                    std::max(1, int_arg(argc, argv, "--de-inner-gen", std::max(1, generations / 6))));
                hybrid_options.de.mutation_min = double_arg(argc, argv, "--mutation-min", hybrid_options.de.mutation_min);
                hybrid_options.de.mutation_max = double_arg(argc, argv, "--mutation-max", hybrid_options.de.mutation_max);
                hybrid_options.de.recombination = double_arg(argc, argv, "--recombination", hybrid_options.de.recombination);
                hybrid_options.de.tolerance = 0.0;
                hybrid_options.de.absolute_tolerance = 0.0;
                hybrid_options.de.strategy = string_arg(argc, argv, "--de-strategy", hybrid_options.de.strategy);
                hybrid_options.de.seed = static_cast<unsigned int>(int_arg(argc, argv, "--seed", 20260515));
                hybrid_options.de.use_cuda = allow_cuda_for_merit;
                hybrid_options.de.cuda_min_batch_size = de_cuda_min_batch_size;
                if (glass_search.enabled) {
                    hybrid_options.de.glass_mutation_rate =
                        double_arg(argc, argv, "--glass-mutation-rate", glass_search.mutation_rate);
                    hybrid_options.de.glass_random_rate =
                        double_arg(argc, argv, "--glass-random-rate", glass_search.random_rate);
                    hybrid_options.de.glass_pbest_inherit_rate =
                        double_arg(argc, argv, "--glass-pbest-inherit-rate", glass_search.pbest_inherit_rate);
                    hybrid_options.de.glass_neighbor_count =
                        int_arg(argc, argv, "--glass-neighbor-count", glass_search.neighbor_count);
                    hybrid_options.de.glass_local_search =
                        glass_search.local_search && !has_arg(argc, argv, "--glass-no-local-search");
                }
                hybrid_options.de.initial_population_values = initial_population_values;
                hybrid_options.de.internal_scaling = internal_scaling;
                apply_initial_generation_args(argc, argv, hybrid_options.de.initial_generation);
                apply_population_generation_target_defaults(
                    hybrid_options.de.initial_generation,
                    merit,
                    lens);
                hybrid_options.outer_epochs = int_arg(argc, argv, "--hybrid-outer",
                    std::max(1, int_arg(argc, argv, "--outer", 6)));
                hybrid_options.parent_count = int_arg(argc, argv, "--hybrid-parents",
                    int_arg(argc, argv, "--parents", hybrid_options.parent_count));
                hybrid_options.diversity = double_arg(argc, argv, "--hybrid-diversity", hybrid_options.diversity);
                hybrid_options.distance_metric = distance_metric_from_string(
                    string_arg(argc, argv, "--hybrid-distance-metric",
                               distance_metric_name(hybrid_options.distance_metric)));
                hybrid_options.internal_scaling = internal_scaling;
                hybrid_options.refresh_fraction =
                    double_arg(argc, argv, "--hybrid-refresh", hybrid_options.refresh_fraction);
                apply_refresh_generation_args(argc, argv, hybrid_options.refresh_generation);
                apply_population_generation_target_defaults(
                    hybrid_options.refresh_generation,
                    merit,
                    lens);
                if (!load_refresh_seed_pool(
                        hybrid_options.refresh_generation,
                        static_cast<int>(variables.size()),
                        std::cerr)) {
                    return 7;
                }
                hybrid_options.rollback = !has_arg(argc, argv, "--hybrid-no-rollback");
                hybrid_options.inject_best = !has_arg(argc, argv, "--hybrid-no-inject-best");
                hybrid_options.parallel_polish = !has_arg(argc, argv, "--hybrid-serial-polish");
                hybrid_options.polish_enabled =
                    !has_arg(argc, argv, "--no-polish") &&
                    !has_arg(argc, argv, "--disable-polish") &&
                    !has_arg(argc, argv, "--hybrid-no-polish");
                hybrid_options.batched_polish =
                    has_arg(argc, argv, "--batched-polish") ||
                    has_arg(argc, argv, "--batch-polish") ||
                    has_arg(argc, argv, "--micro-dls-batch-polish");
                hybrid_options.polish_workers =
                    std::max(0, int_arg(argc, argv, "--hybrid-polish-workers", hybrid_options.polish_workers));
                hybrid_options.polish.max_iterations =
                    int_arg(argc, argv, "--hybrid-local-iter",
                        int_arg(argc, argv, "--local-iter", 12));
                hybrid_options.polish.max_rejected_steps =
                    int_arg(argc, argv, "--local-max-reject", hybrid_options.polish.max_rejected_steps);
                hybrid_options.polish.initial_lambda =
                    double_arg(argc, argv, "--local-lambda", hybrid_options.polish.initial_lambda);
                hybrid_options.polish.finite_difference =
                    double_arg(argc, argv, "--local-fd", hybrid_options.polish.finite_difference);
                hybrid_options.polish.min_step =
                    double_arg(argc, argv, "--local-min-step", hybrid_options.polish.min_step);
                hybrid_options.polish.tolerance =
                    double_arg(argc, argv, "--local-tol", hybrid_options.polish.tolerance);
                hybrid_options.polish.plateau_window = int_arg(
                    argc, argv, "--local-plateau-window", hybrid_options.polish.plateau_window);
                hybrid_options.polish.plateau_min_iterations = int_arg(
                    argc, argv, "--local-plateau-min-iter", hybrid_options.polish.plateau_min_iterations);
                hybrid_options.polish.plateau_min_abs_improvement = double_arg(
                    argc, argv, "--local-plateau-min-abs-improve", hybrid_options.polish.plateau_min_abs_improvement);
                hybrid_options.polish.plateau_min_rel_improvement = double_arg(
                    argc, argv, "--local-plateau-min-rel-improve", hybrid_options.polish.plateau_min_rel_improvement);
                hybrid_options.polish.damping_diagonal_floor =
                    double_arg(argc, argv, "--local-damping-floor",
                        hybrid_options.polish.damping_diagonal_floor);
                hybrid_options.polish.step_search_levels =
                    int_arg(argc, argv, "--local-step-search",
                        hybrid_options.polish.step_search_levels);
                hybrid_options.polish.step_search_shrink =
                    double_arg(argc, argv, "--local-step-shrink",
                        hybrid_options.polish.step_search_shrink);
                hybrid_options.polish.column_equilibration =
                    has_arg(argc, argv, "--local-column-equilibration") ||
                    has_arg(argc, argv, "--local-column-scale");
                hybrid_options.polish.active_set_bounds =
                    has_arg(argc, argv, "--local-active-set") ||
                    has_arg(argc, argv, "--local-active-bounds");
                hybrid_options.polish.projected_active_set_bounds =
                    has_arg(argc, argv, "--local-projected-active-set") ||
                    has_arg(argc, argv, "--local-project-active-set");
                hybrid_options.polish.active_set_bound_tolerance =
                    double_arg(argc, argv, "--local-active-set-tol",
                        hybrid_options.polish.active_set_bound_tolerance);
                hybrid_options.polish.qr_linear_solver =
                    has_arg(argc, argv, "--local-qr-solver") ||
                    normalized_option_token(
                        string_arg(argc, argv, "--local-linear-solver", "")) == "qr";
                hybrid_options.polish.trf_initial_radius =
                    double_arg(argc, argv, "--local-trust-radius",
                        hybrid_options.polish.trf_initial_radius);
                hybrid_options.polish.trf_min_radius =
                    double_arg(argc, argv, "--local-trust-min-radius",
                        hybrid_options.polish.trf_min_radius);
                hybrid_options.polish.trf_max_radius =
                    double_arg(argc, argv, "--local-trust-max-radius",
                        hybrid_options.polish.trf_max_radius);
                hybrid_options.polish.trf_acceptance_ratio =
                    double_arg(argc, argv, "--local-trust-eta",
                        hybrid_options.polish.trf_acceptance_ratio);
                hybrid_options.polish.method = string_arg(argc, argv, "--local-method", hybrid_options.polish.method);
                hybrid_options.polish.residual_mode =
                    string_arg(argc, argv, "--local-residual-mode",
                        hybrid_options.polish.residual_mode);
                hybrid_options.polish.use_cuda = hybrid_options.de.use_cuda;
                hybrid_options.polish.finite_difference_diagnostics =
                    has_arg(argc, argv, "--local-fd-diagnostics") ||
                    has_arg(argc, argv, "--dls-fd-diagnostics");
                hybrid_options.polish.freeze_vignetting_variables =
                    has_arg(argc, argv, "--local-freeze-vignetting") ||
                    (std::isfinite(merit.minimum_relative_illumination) &&
                     merit.minimum_relative_illumination > 0.0);
                hybrid_options.polish.internal_scaling = internal_scaling;
                hybrid_options.polish.trace_acceptance_enabled =
                    has_arg(argc, argv, "--local-require-trace-feasible") ||
                    has_arg(argc, argv, "--local-allow-near-trace-infeasible");
                hybrid_options.polish.require_trace_feasible =
                    !has_arg(argc, argv, "--local-allow-trace-infeasible");
                hybrid_options.polish.allow_near_trace_infeasible =
                    has_arg(argc, argv, "--local-allow-near-trace-infeasible");
                hybrid_options.polish.trace_coverage_epsilon =
                    double_arg(argc, argv, "--local-trace-coverage-epsilon",
                        double_arg(argc, argv, "--trace-coverage-epsilon",
                            hybrid_options.polish.trace_coverage_epsilon));
                apply_local_auto_tune(hybrid_options.polish, argc, argv);

                std::cout << "CUDA evaluator available: "
                          << (evaluate_population_cuda_available() ? "yes" : "no") << "\n";
                std::cout << "Variables: " << variables.size()
                          << ", population: "
                          << (hybrid_options.de.use_popsize_multiplier
                                  ? hybrid_options.de.popsize_multiplier * population_sizing_dimension(variables)
                                  : hybrid_options.de.population_size)
                          << ", hybrid outer: " << hybrid_options.outer_epochs
                          << ", de inner generations: " << hybrid_options.de.max_generations
                          << ", DLS parents: " << hybrid_options.parent_count
                          << ", DLS iterations: " << hybrid_options.polish.max_iterations
                          << ", DLS residuals: " << hybrid_options.polish.residual_mode
                          << ", DLS polish: "
                          << (hybrid_options.polish_enabled ? "enabled" : "disabled")
                          << ", DLS parent parallel: "
                          << (hybrid_options.parallel_polish ? "yes" : "no")
                          << ", batched DLS polish: "
                          << (hybrid_options.batched_polish ? "yes" : "no")
                          << ", strategy: " << hybrid_options.de.strategy
                          << ", glass search: "
                          << (glass_search.enabled ? "yes" : "no")
                          << ", hybrid distance: " << distance_metric_name(hybrid_options.distance_metric)
                          << ", refresh fraction: " << hybrid_options.refresh_fraction
                          << ", initial generation: "
                          << population_generation_mode_name(hybrid_options.de.initial_generation.mode)
                          << ", refresh generation: "
                          << population_generation_mode_name(hybrid_options.refresh_generation.mode)
                          << ", refresh trace gate: "
                          << (hybrid_options.refresh_generation.trace_feasible_gate ? "yes" : "no")
                          << ", refresh seed pool: "
                          << hybrid_options.refresh_generation.seed_pool_values.size()
                          << " @ " << hybrid_options.refresh_generation.seed_pool_fraction
                          << ", seed perturb: "
                          << hybrid_options.refresh_generation.seed_pool_perturbation
                          << ", refresh trace archive: "
                          << (hybrid_options.refresh_generation.trace_archive_enabled ? "yes" : "no")
                          << " @ " << hybrid_options.refresh_generation.trace_archive_fraction
                          << ", archive cap: "
                          << hybrid_options.refresh_generation.trace_archive_capacity
                          << ", adaptive refresh: "
                          << (hybrid_options.refresh_generation.adaptive_trigger_enabled ? "yes" : "no")
                          << ", max fraction: "
                          << hybrid_options.refresh_generation.adaptive_max_fraction
                          << ", internal scaling: " << internal_scaling_name(hybrid_options.internal_scaling)
                          << "\n";
                if (!initial_population_values.empty()) {
                    std::cout << "Initial population seeds: "
                              << initial_population_values.size() << "\n";
                }

                if (!out_dir.empty()) {
                    std::ofstream(join_path(out_dir, "hybrid_trace.jsonl"), std::ios::trunc);
                }
                hybrid_options.trace = [&](const HybridTraceEvent& event) {
                    append_hybrid_trace_jsonl(out_dir, event);
                };
                result = optimizer.run_hybrid_de_dls(hybrid_options, [&](int epoch, const Score& best,
                                                                         const std::vector<double>& best_values) {
                    std::cout << "  hybrid " << epoch << " best merit " << best.merit
                              << " mean spot " << best.mean_spot << "\n";
                    checkpoint_progress("hybrid", epoch, best, best_values);
                });
                de_used_cuda = result.used_cuda;

    return kContinueCommand;
}

int OptimizationCommand::run_adaptive() {
    if (const int status = adaptive_de_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_constraint_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_stop_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_material_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_avoidance_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_memory_mutation_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_memory_escape_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_memory_input(); status != kContinueCommand) return status;
    if (const int status = adaptive_basin_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_niche_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_focus_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_plateau_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_refresh_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_polish_options(); status != kContinueCommand) return status;
    if (const int status = adaptive_console(); status != kContinueCommand) return status;
    if (const int status = adaptive_callbacks(); status != kContinueCommand) return status;
    if (const int status = adaptive_execute(); status != kContinueCommand) return status;
    return kContinueCommand;
}

} } }
