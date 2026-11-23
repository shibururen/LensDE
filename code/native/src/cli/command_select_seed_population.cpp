// Execute the seed-population-only command and publish its selected rows and diagnostics.
// The command pipeline can return here before any optimizer is constructed.
// Keep saved row identity, selection options, and output status separate from an optimization run.

#include "command_context.hpp"

namespace lensde { namespace cli { namespace detail {

int CommandContext::select_seed_population() {
    if (select_seed_population_only) {
        std::vector<VariableSpec> variables;
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
        const std::string candidate_pool_path =
            string_arg(argc, argv, "--candidate-pool",
                string_arg(argc, argv, "--seed-candidate-pool", ""));
        if (candidate_pool_path.empty()) {
            std::cerr << "--select-seed-population requires --candidate-pool\n";
            return 7;
        }
        const int select_target =
            std::max(0, int_arg(argc, argv, "--select-target", population));
        const unsigned int select_seed = static_cast<unsigned int>(
            std::max(0, int_arg(argc, argv, "--seed", 20260515)));
        const double select_min_active_fraction = std::clamp(
            double_arg(argc, argv, "--select-min-active-fraction", 0.05),
            0.0,
            1.0);
        const double select_min_curvature_active_fraction = std::clamp(
            double_arg(
                argc,
                argv,
                "--select-min-curvature-active-fraction",
                select_min_active_fraction),
            0.0,
            1.0);
        const std::string selected_population_path =
            string_arg(argc, argv, "--selected-population",
                join_path(out_dir, "selected_seed_population.csv"));
        const std::string selection_summary_path =
            string_arg(argc, argv, "--selection-summary",
                join_path(out_dir, "selection_summary.json"));
        std::vector<SeedPoolCandidate> candidates;
        std::string error;
        if (!load_seed_candidate_pool_csv(
                candidate_pool_path,
                static_cast<int>(variables.size()),
                &candidates,
                &error)) {
            std::cerr << "Failed to load seed candidate pool: " << error << "\n";
            return 7;
        }
        const int input_count = static_cast<int>(candidates.size());
        int deduped_count = 0;
        double selection_seconds = 0.0;
        std::vector<std::vector<double>> selected_rows =
            select_seed_population_cpp(
                std::move(candidates),
                variables,
                select_target,
                select_seed,
                select_min_active_fraction,
                select_min_curvature_active_fraction,
                &selection_seconds,
                &deduped_count);
        if (static_cast<int>(selected_rows.size()) < select_target) {
            std::cerr << "Seed selector produced " << selected_rows.size()
                      << " rows; target is " << select_target << "\n";
            return 7;
        }
        const auto write_started = std::chrono::steady_clock::now();
        if (!write_rows_csv(selected_population_path, selected_rows)) {
            std::cerr << "Failed to write selected seed population: "
                      << selected_population_path << "\n";
            return 7;
        }
        const double write_seconds =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - write_started).count();
        write_selection_summary_json(
            selection_summary_path,
            input_count,
            deduped_count,
            select_target,
            selected_rows,
            variables,
            select_min_active_fraction,
            select_min_curvature_active_fraction,
            selection_seconds,
            write_seconds);
        std::cout << "Selected " << selected_rows.size()
                  << " seed candidates from " << deduped_count
                  << " unique candidates in " << selection_seconds << " s\n";
        return 0;
    }
    return kContinueCommand;
}

} } }
