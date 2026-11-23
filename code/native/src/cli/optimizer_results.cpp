// Run optional local polish, materialize the final candidate, and enforce publication guards.
// The command retains a validated checkpoint fallback and writes the final population/result products.
// Keep polish rejection, aperture rescoring, checkpoint recovery, and score ownership in their existing order.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::polish() {
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;
    if (run_polish && !run_hybrid && !run_adaptive_hybrid) {
        const OptimizationResult before_polish = result;
        const bool polish_uses_local_variables =
            local_bounds_mode != LocalBoundsMode::Variable;
        const std::vector<VariableSpec>& polish_variables =
            polish_uses_local_variables ? local_variables : primary_variables;
        DifferentialEvolution local_optimizer(optimization_base, polish_variables, merit);
        local_optimizer.set_glass_search_context(glass_search);
        PolishOptions polish_options;
        polish_options.max_iterations = int_arg(argc, argv, "--local-iter", polish_options.max_iterations);
        polish_options.max_rejected_steps =
            int_arg(argc, argv, "--local-max-reject", polish_options.max_rejected_steps);
        polish_options.initial_lambda = double_arg(argc, argv, "--local-lambda", polish_options.initial_lambda);
        polish_options.finite_difference = double_arg(argc, argv, "--local-fd", polish_options.finite_difference);
        polish_options.min_step = double_arg(argc, argv, "--local-min-step", polish_options.min_step);
        polish_options.tolerance = double_arg(argc, argv, "--local-tol", polish_options.tolerance);
        polish_options.plateau_window = int_arg(
            argc, argv, "--local-plateau-window", polish_options.plateau_window);
        polish_options.plateau_min_iterations = int_arg(
            argc, argv, "--local-plateau-min-iter", polish_options.plateau_min_iterations);
        polish_options.plateau_min_abs_improvement = double_arg(
            argc, argv, "--local-plateau-min-abs-improve", polish_options.plateau_min_abs_improvement);
        polish_options.plateau_min_rel_improvement = double_arg(
            argc, argv, "--local-plateau-min-rel-improve", polish_options.plateau_min_rel_improvement);
        polish_options.damping_diagonal_floor =
            double_arg(argc, argv, "--local-damping-floor",
                polish_options.damping_diagonal_floor);
        polish_options.step_search_levels =
            int_arg(argc, argv, "--local-step-search",
                polish_options.step_search_levels);
        polish_options.step_search_shrink =
            double_arg(argc, argv, "--local-step-shrink",
                polish_options.step_search_shrink);
        polish_options.column_equilibration =
            has_arg(argc, argv, "--local-column-equilibration") ||
            has_arg(argc, argv, "--local-column-scale");
        polish_options.active_set_bounds =
            has_arg(argc, argv, "--local-active-set") ||
            has_arg(argc, argv, "--local-active-bounds");
        polish_options.projected_active_set_bounds =
            has_arg(argc, argv, "--local-projected-active-set") ||
            has_arg(argc, argv, "--local-project-active-set");
        polish_options.active_set_bound_tolerance =
            double_arg(argc, argv, "--local-active-set-tol",
                polish_options.active_set_bound_tolerance);
        polish_options.qr_linear_solver =
            has_arg(argc, argv, "--local-qr-solver") ||
            normalized_option_token(
                string_arg(argc, argv, "--local-linear-solver", "")) == "qr";
        polish_options.trf_initial_radius =
            double_arg(argc, argv, "--local-trust-radius",
                polish_options.trf_initial_radius);
        polish_options.trf_min_radius =
            double_arg(argc, argv, "--local-trust-min-radius",
                polish_options.trf_min_radius);
        polish_options.trf_max_radius =
            double_arg(argc, argv, "--local-trust-max-radius",
                polish_options.trf_max_radius);
        polish_options.trf_acceptance_ratio =
            double_arg(argc, argv, "--local-trust-eta",
                polish_options.trf_acceptance_ratio);
        polish_options.method = string_arg(argc, argv, "--local-method", polish_options.method);
        polish_options.residual_mode =
            string_arg(argc, argv, "--local-residual-mode", polish_options.residual_mode);
        polish_options.use_cuda = allow_cuda_for_merit;
        polish_options.finite_difference_diagnostics =
            has_arg(argc, argv, "--local-fd-diagnostics") ||
            has_arg(argc, argv, "--dls-fd-diagnostics");
        polish_options.freeze_vignetting_variables =
            has_arg(argc, argv, "--local-freeze-vignetting") ||
            (std::isfinite(merit.minimum_relative_illumination) &&
             merit.minimum_relative_illumination > 0.0);
        polish_options.internal_scaling = internal_scaling;
        polish_options.trace_acceptance_enabled =
            has_arg(argc, argv, "--local-require-trace-feasible") ||
            has_arg(argc, argv, "--local-allow-near-trace-infeasible");
        polish_options.require_trace_feasible =
            !has_arg(argc, argv, "--local-allow-trace-infeasible");
        polish_options.allow_near_trace_infeasible =
            has_arg(argc, argv, "--local-allow-near-trace-infeasible");
        polish_options.trace_coverage_epsilon =
            double_arg(argc, argv, "--local-trace-coverage-epsilon",
                double_arg(argc, argv, "--trace-coverage-epsilon",
                    polish_options.trace_coverage_epsilon));
        apply_local_auto_tune(polish_options, argc, argv);
        std::cout << "Local DLS bounds: " << local_bounds_mode_name(local_bounds_mode) << "\n";
        OptimizationResult polished = local_optimizer.polish_damped_least_squares(
            result.best_values,
            polish_options,
            [&](int iter, const Score& best, const std::vector<double>& best_values) {
                if (iter % 5 == 0) {
                    std::cout << "  polish " << iter << " best merit "
                              << best.merit << "\n";
                }
                checkpoint_progress_with_variables(polish_variables, "polish", iter, best, best_values);
            });
        const double before_merit = before_polish.best_score.merit;
        const double after_merit = polished.best_score.merit;
        const double merit_tolerance = 1.0e-12 * std::max(1.0, std::abs(before_merit));
        const bool polish_not_worse =
            std::isfinite(after_merit) &&
            (!std::isfinite(before_merit) || after_merit <= before_merit + merit_tolerance);
        const bool polish_improved =
            std::isfinite(after_merit) &&
            std::isfinite(before_merit) &&
            after_merit + merit_tolerance < before_merit;
        const double attempted_polish_seconds = polished.polish_seconds;
        const int attempted_polish_runs = polished.polish_runs;
        if (polish_not_worse) {
            result = std::move(polished);
            result_variables = &polish_variables;
        } else {
            std::cout << "  polish rejected: merit would increase from "
                      << before_merit << " to " << after_merit << "\n";
            result = before_polish;
            result_variables = &primary_variables;
        }
        result.used_cuda = result.used_cuda || before_polish.used_cuda || de_used_cuda;
        result.de_seconds = before_polish.de_seconds;
        result.polish_seconds = before_polish.polish_seconds + attempted_polish_seconds;
        result.polish_runs = before_polish.polish_runs + attempted_polish_runs;
        result.polish_improvements = before_polish.polish_improvements + (polish_improved ? 1 : 0);
    }
    return kContinueCommand;
}

int OptimizationCommand::materialize() {
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;
    // Keep the production contract identical through optimizer scoring,
    // aperture materialization, acceptance guards, serialized scores, and
    // GUI handoff. Legacy experimental policies retain their historical
    // certified final-rescore behavior.
    set_asphere_root_policy(
        optimizer_asphere_root_policy == AsphereRootPolicy::SlopeDomainTopologySeed ||
                optimizer_asphere_root_policy ==
                    AsphereRootPolicy::UnifiedCertifiedExperimental
            ? optimizer_asphere_root_policy
            : AsphereRootPolicy::CertifiedFirstRoot);
    final_matches_adaptive_boundary = run_adaptive_hybrid &&
                checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate &&
                result_variables == &primary_variables &&
                last_adaptive_validation_valid &&
                result.best_values == last_adaptive_validated_values;
    if (final_matches_adaptive_boundary) {
        // The outer-boundary CPU validation is already the exact final
        // optimizer state.  Reuse it so terminal publication does not pay
        // for a duplicate aperture update and static merit evaluation.
        lens = last_adaptive_validated_system;
        result.best_score = last_adaptive_validated_score;
    } else {
        lens = apply_mixed_variables(
            optimizer_input_lens,
            *result_variables,
            result.best_values,
            glass_search);
        const bool result_requires_aperture_materialization =
            run_de || run_hybrid || run_adaptive_hybrid || run_polish;
        if (merit.dynamic_apertures && result_requires_aperture_materialization) {
            update_clear_apertures_from_trace(
                lens,
                clear_aperture_update_rings(merit),
                "gaussian_ring_arm",
                merit.sampling_seed,
                lens.ray_aiming.mode,
                merit.effective_clear_aperture_margin(),
                true,
                merit.clear_aperture_arms,
                false,
                false,
                merit.vignetting_boundary_support_tolerance,
                merit.vignetting_boundary_support_selectivity_slope,
                merit.vignetting_boundary_support_contract ==
                    VignettingBoundarySupportContract::GlobalTrimRealizability,
                merit.vignetting_physical_cut_match_enabled,
        merit.vignetting_optimization_enabled());
            result.best_score = evaluate_merit(lens, merit);
        } else if (merit.dynamic_apertures) {
            // Plain scoring reuses the one aperture update performed while
            // loading the input instead of applying a non-idempotent update
            // for the third time before serialization.
            result.best_score = evaluate_merit(lens, merit);
        } else {
            sync_stop_aperture_to_system(lens);
        }
    }
    optimizer_materialized_score = result.best_score;
    final_materialization_used_checkpoint = false;
    return kContinueCommand;
}

int OptimizationCommand::guard_materialization() {
    bool final_materialization_valid =
        score_is_materialization_valid(
            result.best_score, merit, materialization_trace_contract);
    Score checkpoint_recovery_score;
    if (run_adaptive_hybrid &&
        checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate &&
        last_adaptive_validation_valid &&
        score_is_materialization_valid(
            last_adaptive_validated_score,
            merit,
            materialization_trace_contract) &&
        (!final_materialization_valid ||
         last_adaptive_validated_score.merit + 1.0e-12 <
             result.best_score.merit)) {
        lens = last_adaptive_validated_system;
        result.best_score = last_adaptive_validated_score;
        result.best_values = last_adaptive_validated_values;
        checkpoint_recovery_score = last_adaptive_validated_score;
        final_materialization_used_checkpoint = true;
        final_materialization_valid = true;
    } else if (!final_materialization_valid) {
        LensSystem checkpoint_recovery;
        std::string checkpoint_error;
        if (load_system_json(
                join_path(out_dir, "checkpoint_system.json"),
                &checkpoint_recovery,
                &checkpoint_error)) {
            checkpoint_recovery_score = evaluate_merit(checkpoint_recovery, merit);
            if (score_is_materialization_valid(
                    checkpoint_recovery_score,
                    merit,
                    materialization_trace_contract)) {
                lens = std::move(checkpoint_recovery);
                result.best_score = checkpoint_recovery_score;
                final_materialization_used_checkpoint = true;
                final_materialization_valid = true;
            }
        }
    }
    lens.asphere_root_policy =
        asphere_root_policy_token(asphere_root_policy());
    {
        std::ofstream guard(join_path(out_dir, "final_materialization_guard.json"));
        if (guard) {
            guard << "{\n  \"optimizer_materialization_valid\": "
                  << (score_is_materialization_valid(
                          optimizer_materialized_score,
                          merit,
                          materialization_trace_contract)
                          ? "true"
                          : "false");
            guard << ",\n  \"materialization_trace_contract\": \""
                  << (materialization_trace_contract.allow_near_trace_infeasible
                          ? "near"
                          : "strict")
                  << "\"";
            guard << ",\n  \"trace_coverage_epsilon\": "
                  << (materialization_trace_contract.allow_near_trace_infeasible
                          ? std::max(
                                0.0,
                                materialization_trace_contract.trace_coverage_epsilon)
                          : 0.0);
            guard << ",\n  \"selected_source\": \""
                  << (final_materialization_used_checkpoint
                          ? "validated_checkpoint"
                          : "optimizer_final")
                  << "\"";
            guard << ",\n  \"selected_materialization_valid\": "
                  << (final_materialization_valid ? "true" : "false");
            guard << ",\n  \"edge_thickness_publication_policy\": \"finite_soft_merit\"";
            guard << ",\n  \"optimizer_materialized_score\": ";
            write_score_checkpoint_summary(
                guard, optimizer_materialized_score, "  ");
            if (final_materialization_used_checkpoint) {
                guard << ",\n  \"checkpoint_recovery_score\": ";
                write_score_checkpoint_summary(
                    guard, checkpoint_recovery_score, "  ");
            }
            guard << "\n}\n";
        }
    }
    if (!final_materialization_valid) {
        save_system_json(lens, join_path(out_dir, "system.json"));
        save_score_json(result.best_score, join_path(out_dir, "score.json"));
        std::cerr
            << "Final CPU materialization is invalid and no validated checkpoint "
               "is available; refusing to publish a successful stage.\n";
        return 10;
    }
    if (run_adaptive_hybrid &&
        checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate &&
        !final_materialization_used_checkpoint &&
        !final_matches_adaptive_boundary) {
        // Early-stop tails may finish after the last outer-boundary trace.
        // Publish that already validated final materialization as the
        // checkpoint without another CPU rescore.
        checkpoint_progress_with_variables(
            *result_variables,
            "adaptive_final",
            latest_checkpoint_trace_event_seen
                ? latest_checkpoint_trace_event.de_generations
                : last_adaptive_validated_generation,
            result.best_score,
            result.best_values,
            true,
            &lens,
            &result.best_score);
    }
    return kContinueCommand;
}

int OptimizationCommand::save_optimizer_result() {
    const double complete_de_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - complete_de_started).count();
    std::string label = run_adaptive_hybrid
                                  ? (result.used_cuda
                                         ? "Optimized system (adaptive hybrid DE-DLS, CUDA polish)"
                                         : "Optimized system (adaptive hybrid DE-DLS, CPU evaluator)")
                                  : ((run_hybrid
                                         ? (result.used_cuda
                                                ? "Optimized system (hybrid DE-DLS, CUDA batch evaluator)"
                                                : "Optimized system (hybrid DE-DLS, CPU evaluator)")
                                         : (result.used_cuda
                                                ? "Optimized system (CUDA batch evaluator)"
                                                : (run_de ? "Optimized system (CPU evaluator)" : "Optimized system (local DLS)"))));
    if (final_materialization_used_checkpoint) {
        label += " [validated checkpoint fallback]";
        std::cout
            << "Final materialization guard selected the existing validated "
               "checkpoint; optimizer final was not published.\n";
    }
    print_score(label, result.best_score);
    if (run_de || run_hybrid || run_polish) {
        std::cout << "Timing: DE " << result.de_seconds
                  << " s, local " << result.polish_seconds
                  << " s, total " << (result.de_seconds + result.polish_seconds)
                  << " s\n";
        std::cout << "Timing: complete DE " << complete_de_seconds
                  << " s (validated initial checkpoint through final CPU materialization)\n";
        if (result.polish_runs > 0) {
            std::cout << "Local polish runs: " << result.polish_runs
                      << ", accepted: " << result.polish_improvements << "\n";
        }
        print_de_timing_detail(result);
    }
    if (run_de || run_hybrid) {
        save_population_csv(result, join_path(out_dir, "population.csv"));
        const std::vector<VariableSpec>& population_score_variables =
            variables;
        save_population_scores_csv(
            result,
            optimization_base,
            population_score_variables,
            glass_search,
            merit,
            join_path(out_dir, "population_scores.csv"));
        if (run_adaptive_hybrid) {
            save_basin_archive_csv(result, join_path(out_dir, "basin_archive.csv"));
            save_basin_archive_json(result, join_path(out_dir, "basin_archive.json"));
        }
    }
    final_optimizer_score = result.best_score;
    final_optimizer_score_available = true;
    return kContinueCommand;
}

} } }
