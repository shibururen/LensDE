// Load and validate optimization variables, glass choices, and optional initial population rows.
// This preparation also serves the population-score-only path before normal optimizer construction.
// Preserve variable dimensions, material wavelength coverage, and saved seed row order.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::prepare_variables_and_glass() {
    // variables is owned by this command stage.
    const std::string variables_path = string_arg(argc, argv, "--variables");
    if (!variables_path.empty()) {
        std::string error;
        if (!load_variables_json(variables_path, &variables, &error)) {
            std::cerr << "Failed to load variables: " << error << "\n";
            return 5;
        }
    } else {
        variables = make_demo_variables(lens);
    }
    if (variables.empty()) {
        std::cerr << "No optimization variables configured\n";
        return 6;
    }
    configure_variable_thickness_constraints(&merit, variables);
    if (!internal_scaling_explicit && std::any_of(
            variables.begin(), variables.end(),
            [](const VariableSpec& var) { return var.kind == VariableKind::AsphereCoeff; })) {
        internal_scaling = InternalScalingMode::EdgeSagNormalized;
    }
    std::string asphere_reference_error;
    if (!prepare_asphere_reference_variables(
            lens,
            variables,
            internal_scaling,
            asphere_reference_policy,
            &asphere_reference_error)) {
        std::cerr << "Invalid input asphere reference metadata: "
                  << asphere_reference_error << "\n";
        return 6;
    }
    std::string material_wavelength_error;
    if (!validate_fixed_material_wavelength_coverage(
            lens,
            variables,
            &material_wavelength_error)) {
        std::cerr << "Invalid material wavelength coverage: "
                  << material_wavelength_error << "\n";
        return 8;
    }
    std::vector<double> required_material_wavelengths_um;
    for (const auto& wavelength : lens.wavelengths) {
        if (std::isfinite(wavelength.value_um) && wavelength.value_um > 0.0) {
            required_material_wavelengths_um.push_back(wavelength.value_um);
        }
    }
    // glass_search is owned by this command stage.
    const std::string glass_search_config_path = string_arg(argc, argv, "--glass-search-config");
    const std::string glass_database_root = string_arg(
        argc,
        argv,
        "--glass-database",
        lensde_default_database());
    const bool has_glass_choice_variable = std::any_of(
        variables.begin(),
        variables.end(),
        [](const VariableSpec& var) { return var.kind == VariableKind::GlassChoice; });
    if (has_arg(argc, argv, "--glass-search") || !glass_search_config_path.empty() ||
        has_glass_choice_variable) {
        if (glass_search_config_path.empty()) {
            std::cerr << "Glass choice variables require --glass-search-config\n";
            return 8;
        }
        std::string error;
        if (!load_glass_search_config(
                glass_search_config_path,
                glass_database_root,
                &glass_search,
                &error,
                required_material_wavelengths_um)) {
            std::cerr << "Failed to load glass search config: " << error << "\n";
            return 8;
        }
        if (has_glass_choice_variable && !glass_search.enabled) {
            std::cerr << "Glass search config has no enabled candidate groups\n";
            return 8;
        }
        for (auto& variable : variables) {
            if (variable.kind != VariableKind::GlassChoice) {
                continue;
            }
            const GlassChoiceGroup* group = nullptr;
            if (variable.glass_group >= 0) {
                for (const auto& candidate_group : glass_search.groups) {
                    if (candidate_group.id == variable.glass_group) {
                        group = &candidate_group;
                        break;
                    }
                }
            }
            if (!group) {
                for (const auto& candidate_group : glass_search.groups) {
                    if (candidate_group.surface_index == variable.index) {
                        group = &candidate_group;
                        variable.glass_group = candidate_group.id;
                        break;
                    }
                }
            }
            if (!group || group->candidates.empty()) {
                std::cerr << "Glass choice variable for surface " << variable.index
                          << " has no candidate group\n";
                return 8;
            }
            variable.lower = 0.0;
            variable.upper = static_cast<double>(group->candidates.size() - 1);
            variable.initial = std::clamp(
                std::round(variable.initial),
                variable.lower,
                variable.upper);
        }
        std::cout << "Glass search: " << glass_search.groups.size()
                  << " surface group(s), exact catalog materials available to CPU/CUDA evaluators";
        std::cout << ", wavelength-filtered rejects: "
                  << glass_search.wavelength_rejected_candidates;
        if (!glass_search.database_root.empty()) {
            std::cout << ", database: " << glass_search.database_root;
        }
        std::cout << "\n";
    }
    return kContinueCommand;
}

int OptimizationCommand::prepare_population() {
    // initial_population_values is owned by this command stage.
    const std::string initial_population_path = string_arg(argc, argv, "--initial-population");
    if (!initial_population_path.empty()) {
        std::string error;
        if (!load_population_csv(
                initial_population_path,
                static_cast<int>(variables.size()),
                &initial_population_values,
                &error)) {
            std::cerr << "Failed to load initial population: " << error << "\n";
            return 7;
        }
    }
    if (score_population_only) {
        if (initial_population_values.empty()) {
            std::cerr << "--score-population-only requires --initial-population\n";
            return 7;
        }
        OptimizationResult scored_population;
        scored_population.final_population_size =
            static_cast<int>(initial_population_values.size());
        scored_population.final_population_dim = static_cast<int>(variables.size());
        scored_population.final_population_values.reserve(
            static_cast<std::size_t>(scored_population.final_population_size) *
            variables.size());
        for (const auto& row : initial_population_values) {
            if (row.size() != variables.size()) {
                std::cerr << "Initial population row has wrong dimension\n";
                return 7;
            }
            scored_population.final_population_values.insert(
                scored_population.final_population_values.end(),
                row.begin(),
                row.end());
        }
        save_population_csv(scored_population, join_path(out_dir, "population.csv"));
        CudaEvaluationTiming score_cuda_timing;
        bool score_used_cuda = false;
        const bool score_written = save_population_scores_csv(
            scored_population,
            lens,
            variables,
            glass_search,
            merit,
            join_path(out_dir, "population_scores.csv"),
            allow_cuda_for_merit,
            &score_cuda_timing,
            &score_used_cuda);
        if (!score_written) {
            std::cerr << "Failed to score population";
            if (allow_cuda_for_merit && de_cuda_required_local()) {
                std::cerr << " with required CUDA-DE evaluator";
            }
            std::cerr << "\n";
            return 9;
        }
        if (score_population_detail_index >= 0) {
            if (score_population_detail_index >= scored_population.final_population_size) {
                std::cerr << "--score-population-detail-index is outside the population\n";
                return 7;
            }
            const Score detail_score = evaluate_population_score_candidate(
                scored_population,
                lens,
                variables,
                glass_search,
                merit,
                score_population_detail_index);
            const std::string detail_path = join_path(
                out_dir,
                "population_score_detail_" +
                    std::to_string(score_population_detail_index) + ".json");
            if (!save_score_json(detail_score, detail_path)) {
                std::cerr << "Failed to write population score detail: "
                          << detail_path << "\n";
                return 9;
            }
            const int detail_dim = static_cast<int>(variables.size());
            std::vector<double> detail_values;
            detail_values.reserve(static_cast<std::size_t>(detail_dim));
            for (int j = 0; j < detail_dim; ++j) {
                detail_values.push_back(
                    scored_population.final_population_values[
                        static_cast<std::size_t>(score_population_detail_index) *
                            detail_dim +
                        j]);
            }
            LensSystem detail_system = apply_mixed_variables(
                lens,
                variables,
                detail_values,
                glass_search);
            if (merit.dynamic_apertures) {
                update_clear_apertures_from_trace(
                    detail_system,
                    clear_aperture_update_rings(merit),
                    "gaussian_ring_arm",
                    merit.sampling_seed,
                    detail_system.ray_aiming.mode,
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
            } else {
                sync_stop_aperture_to_system(detail_system);
            }
            const std::string detail_system_path = join_path(
                out_dir,
                "population_system_detail_" +
                    std::to_string(score_population_detail_index) + ".json");
            if (!save_system_json(detail_system, detail_system_path)) {
                std::cerr << "Failed to write population system detail: "
                          << detail_system_path << "\n";
                return 9;
            }
            std::cout << "Wrote CPU population score detail: "
                      << detail_path << "\n";
        }
        std::cout << "Scored "
                  << scored_population.final_population_size
                  << " population candidates\n";
        std::cout << "Score population evaluator: "
                  << (score_used_cuda ? "CUDA" : "CPU") << "\n";
        if (score_cuda_timing.calls > 0) {
            std::cout << "CUDA score detail:\n";
            std::cout << "  calls " << score_cuda_timing.calls
                      << ", candidates " << score_cuda_timing.candidates
                      << ", total " << score_cuda_timing.total_seconds << " s\n";
            std::cout << "  context init " << score_cuda_timing.context_seconds
                      << " s, host setup " << score_cuda_timing.host_setup_seconds
                      << " s, H2D " << score_cuda_timing.host_to_device_seconds
                      << " s, kernel+sync " << score_cuda_timing.kernel_seconds
                      << " s, D2H " << score_cuda_timing.device_to_host_seconds
                      << " s, cleanup " << score_cuda_timing.cleanup_seconds << " s\n";
        }
        print_ray_aiming_diagnostics(out_dir);
        return 0;
    }
    return kContinueCommand;
}

int OptimizationCommand::prepare_optimizer() {
    optimization_base = lens;
    local_variables = local_bounds_variables(variables, local_bounds_mode);
    const bool primary_optimizer_uses_local_bounds =
        run_polish && !run_de && !run_hybrid && !run_adaptive_hybrid && local_bounds_mode != LocalBoundsMode::Variable;
    primary_variables_source = primary_optimizer_uses_local_bounds ? &local_variables : &variables;
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;
    result_variables = &primary_variables;
    optimizer_storage.emplace(optimization_base, primary_variables, merit);
    auto& optimizer = *optimizer_storage;
    optimizer.set_glass_search_context(glass_search);
    // result is owned by this command stage.
    de_used_cuda = false;
    // latest_adaptive_glass_checkpoint_state is owned by this command stage.
    // latest_checkpoint_trace_event is owned by this command stage.
    latest_checkpoint_trace_event_seen = false;
    // last_adaptive_validated_system is owned by this command stage.
    // last_adaptive_validated_score is owned by this command stage.
    // last_adaptive_validated_values is owned by this command stage.
    last_adaptive_validated_generation = -1;
    last_adaptive_validation_valid = false;
    return kContinueCommand;
}

} } }
