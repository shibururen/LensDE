// Load avoidance and geometry-Memory inputs and configure their mutation/escape policies.
// Adaptive option preparation relies on aligned historical rows, topology keys, and merit records.
// Keep file alignment, dimensions, and seed-related settings intact when enabling Memory paths.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::adaptive_avoidance_options() {
    adaptive_options.avoided_topology_keys = load_key_list_file(
        string_arg(argc, argv, "--avoid-topology-keys", ""));
    adaptive_options.avoided_topology_exact_only_keys = load_key_list_file(
        string_arg(argc, argv, "--avoid-topology-exact-only-keys", ""));
    adaptive_options.avoided_topology_penalty = std::max(
        0.0,
        double_arg(argc, argv, "--avoid-topology-penalty",
            adaptive_options.avoided_topology_penalty));
    adaptive_options.avoided_topology_start_generation = std::max(
        0,
        int_arg(argc, argv, "--avoid-topology-start-generation",
            adaptive_options.avoided_topology_start_generation));
    adaptive_options.avoided_topology_similarity_threshold = std::clamp(
        double_arg(argc, argv,
            "--avoid-topology-similarity-threshold",
            adaptive_options.avoided_topology_similarity_threshold),
        0.0,
        1.0);
    adaptive_options.avoided_topology_max_shape_diff = std::max(
        -1,
        int_arg(argc, argv,
            "--avoid-topology-max-shape-diff",
            adaptive_options.avoided_topology_max_shape_diff));
    adaptive_options.avoided_topology_continuous_structure =
        has_arg(argc, argv, "--avoid-topology-continuous-structure");
    adaptive_options.avoided_topology_continuous_power_cosine_min =
        std::clamp(
            double_arg(
                argc, argv,
                "--avoid-topology-continuous-power-cosine-min",
                adaptive_options
                    .avoided_topology_continuous_power_cosine_min),
            0.0,
            1.0);
    adaptive_options.avoided_topology_continuous_power_distance_max =
        std::max(
            0.0,
            double_arg(
                argc, argv,
                "--avoid-topology-continuous-power-distance-max",
                adaptive_options
                    .avoided_topology_continuous_power_distance_max));
    adaptive_options.avoided_topology_continuous_curvature_cosine_min =
        std::clamp(
            double_arg(
                argc, argv,
                "--avoid-topology-continuous-curvature-cosine-min",
                adaptive_options
                    .avoided_topology_continuous_curvature_cosine_min),
            0.0,
            1.0);
    adaptive_options.avoided_topology_continuous_curvature_distance_max =
        std::max(
            0.0,
            double_arg(
                argc, argv,
                "--avoid-topology-continuous-curvature-distance-max",
                adaptive_options
                    .avoided_topology_continuous_curvature_distance_max));
    adaptive_options.avoided_topology_continuous_thickness_distance_max =
        std::max(
            0.0,
            double_arg(
                argc, argv,
                "--avoid-topology-continuous-thickness-distance-max",
                adaptive_options
                    .avoided_topology_continuous_thickness_distance_max));
    adaptive_options.avoided_topology_continuous_gap_distance_max =
        std::max(
            0.0,
            double_arg(
                argc, argv,
                "--avoid-topology-continuous-gap-distance-max",
                adaptive_options
                    .avoided_topology_continuous_gap_distance_max));
    adaptive_options.avoided_topology_continuous_significance_fraction =
        std::max(
            0.0,
            double_arg(
                argc, argv,
                "--avoid-topology-continuous-significance-fraction",
                adaptive_options
                    .avoided_topology_continuous_significance_fraction));
    adaptive_options.avoided_topology_parameter_auxiliary_similarity_min =
        std::clamp(
            double_arg(
                argc, argv,
                "--avoid-topology-parameter-auxiliary-similarity-min",
                adaptive_options
                    .avoided_topology_parameter_auxiliary_similarity_min),
            0.0,
            1.0);
    return kContinueCommand;
}

int OptimizationCommand::adaptive_memory_mutation_options() {
    adaptive_options.geometry_memory_pbest_soft =
        has_arg(argc, argv, "--geometry-memory-pbest-soft");
    adaptive_options.geometry_memory_difference_mutation =
        has_arg(argc, argv, "--geometry-memory-difference-mutation");
    adaptive_options.geometry_memory_qohr_mutation =
        has_arg(argc, argv, "--geometry-memory-qohr-mutation");
    adaptive_options.geometry_memory_difference_near_only =
        !has_arg(argc, argv, "--geometry-memory-difference-always");
    adaptive_options.geometry_memory_difference_select_nearest =
        has_arg(argc, argv, "--geometry-memory-difference-nearest");
    adaptive_options.geometry_memory_difference_random_population_anchor =
        has_arg(argc, argv,
            "--geometry-memory-difference-random-population-anchor");
    adaptive_options.geometry_memory_difference_key_trigger =
        has_arg(argc, argv, "--geometry-memory-difference-key-trigger");
    adaptive_options.geometry_memory_difference_key_similarity_threshold =
        std::clamp(
            double_arg(argc, argv,
                "--geometry-memory-difference-key-similarity-threshold",
                adaptive_options.geometry_memory_difference_key_similarity_threshold),
            0.0,
            1.0);
    adaptive_options.geometry_memory_difference_key_max_shape_diff =
        std::max(
            -1,
            int_arg(argc, argv,
                "--geometry-memory-difference-key-max-shape-diff",
                adaptive_options.geometry_memory_difference_key_max_shape_diff));
    adaptive_options.geometry_memory_difference_orient_outward =
        has_arg(argc, argv, "--geometry-memory-difference-orient-outward");
    adaptive_options.geometry_memory_qohr_max_rotation = std::clamp(
        double_arg(argc, argv,
            "--geometry-memory-qohr-max-rotation",
            adaptive_options.geometry_memory_qohr_max_rotation),
        0.0,
        1.0);
    adaptive_options.geometry_memory_qohr_backtrack_fraction = std::clamp(
        double_arg(argc, argv,
            "--geometry-memory-qohr-backtrack-fraction",
            adaptive_options.geometry_memory_qohr_backtrack_fraction),
        0.0,
        1.0);
    adaptive_options.geometry_memory_qohr_family_weight = std::max(
        0.0,
        double_arg(argc, argv,
            "--geometry-memory-qohr-family-weight",
            adaptive_options.geometry_memory_qohr_family_weight));
    adaptive_options.geometry_memory_qohr_family_similarity_threshold =
        std::clamp(
            double_arg(argc, argv,
                "--geometry-memory-qohr-family-similarity-threshold",
                adaptive_options
                    .geometry_memory_qohr_family_similarity_threshold),
            0.0,
            1.0);
    adaptive_options.geometry_memory_qohr_family_max_shape_diff = std::max(
        -1,
        int_arg(argc, argv,
            "--geometry-memory-qohr-family-max-shape-diff",
            adaptive_options.geometry_memory_qohr_family_max_shape_diff));
    adaptive_options.geometry_memory_qohr_quality_full_ratio = std::max(
        1.0,
        double_arg(argc, argv,
            "--geometry-memory-qohr-quality-full-ratio",
            adaptive_options.geometry_memory_qohr_quality_full_ratio));
    adaptive_options.geometry_memory_qohr_quality_off_ratio = std::max(
        adaptive_options.geometry_memory_qohr_quality_full_ratio + 1.0e-12,
        double_arg(argc, argv,
            "--geometry-memory-qohr-quality-off-ratio",
            adaptive_options.geometry_memory_qohr_quality_off_ratio));
    return kContinueCommand;
}

int OptimizationCommand::adaptive_memory_escape_options() {
    adaptive_options.geometry_memory_exact_escape_pulse =
        has_arg(argc, argv, "--geometry-memory-exact-escape-pulse");
    adaptive_options.geometry_memory_exact_escape_residence_generations =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--geometry-memory-exact-escape-residence-generations",
                adaptive_options
                    .geometry_memory_exact_escape_residence_generations));
    adaptive_options.geometry_memory_exact_escape_pulse_generations =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--geometry-memory-exact-escape-pulse-generations",
                adaptive_options
                    .geometry_memory_exact_escape_pulse_generations));
    adaptive_options.geometry_memory_exact_escape_cooldown_generations =
        std::max(
            0,
            int_arg(
                argc,
                argv,
                "--geometry-memory-exact-escape-cooldown-generations",
                adaptive_options
                    .geometry_memory_exact_escape_cooldown_generations));
    adaptive_options.geometry_memory_family_escape_pulse =
        has_arg(argc, argv, "--geometry-memory-family-escape-pulse");
    adaptive_options.geometry_memory_family_escape_stagnation_generations =
        std::max(
            1,
            int_arg(argc, argv,
                "--geometry-memory-family-escape-stagnation-generations",
                adaptive_options.geometry_memory_family_escape_stagnation_generations));
    adaptive_options.geometry_memory_family_escape_min_relative_improvement =
        std::max(
            0.0,
            double_arg(argc, argv,
                "--geometry-memory-family-escape-min-relative-improvement",
                adaptive_options.geometry_memory_family_escape_min_relative_improvement));
    adaptive_options.geometry_memory_family_escape_pulse_generations =
        std::max(
            1,
            int_arg(argc, argv,
                "--geometry-memory-family-escape-pulse-generations",
                adaptive_options.geometry_memory_family_escape_pulse_generations));
    adaptive_options.geometry_memory_family_escape_cooldown_generations =
        std::max(
            0,
            int_arg(argc, argv,
                "--geometry-memory-family-escape-cooldown-generations",
                adaptive_options.geometry_memory_family_escape_cooldown_generations));
    adaptive_options.geometry_memory_novelty_guide_quality_fraction =
        std::clamp(
            double_arg(argc, argv,
                "--geometry-memory-novelty-guide-quality-fraction",
                adaptive_options.geometry_memory_novelty_guide_quality_fraction),
            0.01,
            1.0);
    adaptive_options.geometry_memory_plateau_novelty_guide_all_targets =
        has_arg(
            argc,
            argv,
            "--geometry-memory-plateau-novelty-guide-all-targets");
    adaptive_options.geometry_memory_plateau_parameter_reheat =
        has_arg(argc, argv, "--geometry-memory-plateau-parameter-reheat");
    adaptive_options.geometry_memory_plateau_reheat_f_floor =
        std::clamp(
            double_arg(argc, argv,
                "--geometry-memory-plateau-reheat-f-floor",
                adaptive_options.geometry_memory_plateau_reheat_f_floor),
            0.0,
            1.0);
    adaptive_options.geometry_memory_plateau_reheat_cr_floor =
        std::clamp(
            double_arg(argc, argv,
                "--geometry-memory-plateau-reheat-cr-floor",
                adaptive_options.geometry_memory_plateau_reheat_cr_floor),
            0.0,
            1.0);
    adaptive_options.geometry_memory_plateau_geometry_kick =
        has_arg(argc, argv, "--geometry-memory-plateau-geometry-kick");
    adaptive_options.geometry_memory_plateau_geometry_kick_rate =
        std::clamp(
            double_arg(argc, argv,
                "--geometry-memory-plateau-geometry-kick-rate",
                adaptive_options.geometry_memory_plateau_geometry_kick_rate),
            0.0,
            1.0);
    adaptive_options.geometry_memory_plateau_geometry_kick_scale =
        std::max(
            0.0,
            double_arg(argc, argv,
                "--geometry-memory-plateau-geometry-kick-scale",
                adaptive_options.geometry_memory_plateau_geometry_kick_scale));
    adaptive_options.geometry_memory_family_escape_novelty_acceptance =
        has_arg(argc, argv,
            "--geometry-memory-family-escape-novelty-acceptance");
    adaptive_options.geometry_memory_family_escape_novelty_max_merit_ratio =
        std::max(
            1.0,
            double_arg(argc, argv,
                "--geometry-memory-family-escape-novelty-max-merit-ratio",
                adaptive_options.geometry_memory_family_escape_novelty_max_merit_ratio));
    adaptive_options.geometry_memory_family_escape_novelty_min_similarity_drop =
        std::clamp(
            double_arg(argc, argv,
                "--geometry-memory-family-escape-novelty-min-similarity-drop",
                adaptive_options.geometry_memory_family_escape_novelty_min_similarity_drop),
            0.0,
            1.0);
    adaptive_options.geometry_memory_family_escape_novelty_max_per_generation =
        std::max(
            1,
            int_arg(argc, argv,
                "--geometry-memory-family-escape-novelty-max-per-generation",
                adaptive_options.geometry_memory_family_escape_novelty_max_per_generation));
    return kContinueCommand;
}

int OptimizationCommand::adaptive_memory_input() {
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;
    adaptive_options.geometry_memory_full_radius = std::max(
        0.0,
        double_arg(argc, argv,
            "--geometry-memory-full-radius",
            adaptive_options.geometry_memory_full_radius));
    adaptive_options.geometry_memory_skeleton_radius = std::max(
        0.0,
        double_arg(argc, argv,
            "--geometry-memory-skeleton-radius",
            adaptive_options.geometry_memory_skeleton_radius));
    adaptive_options.geometry_memory_asphere_radius = std::max(
        0.0,
        double_arg(argc, argv,
            "--geometry-memory-asphere-radius",
            adaptive_options.geometry_memory_asphere_radius));
    adaptive_options.geometry_memory_difference_strength = std::max(
        0.0,
        double_arg(argc, argv,
            "--geometry-memory-difference-strength",
            adaptive_options.geometry_memory_difference_strength));
    adaptive_options.geometry_memory_same_key_weight = std::max(
        1.0,
        double_arg(argc, argv,
            "--geometry-memory-same-key-weight",
            adaptive_options.geometry_memory_same_key_weight));
    const std::string geometry_memory_path =
        string_arg(argc, argv, "--geometry-memory-values", "");
    if (!geometry_memory_path.empty()) {
        std::string memory_error;
        if (!load_population_csv(
                geometry_memory_path,
                static_cast<int>(primary_variables.size()),
                &adaptive_options.geometry_memory_values,
                &memory_error)) {
            std::cerr << "Failed to load geometry memory: "
                      << memory_error << "\n";
            return 7;
        }
    }
    const std::string geometry_memory_keys_path =
        string_arg(argc, argv, "--geometry-memory-topology-keys", "");
    if (!geometry_memory_keys_path.empty()) {
        adaptive_options.geometry_memory_topology_keys =
            load_aligned_key_list_file(geometry_memory_keys_path);
    }
    if (!adaptive_options.geometry_memory_topology_keys.empty() &&
        adaptive_options.geometry_memory_topology_keys.size() !=
            adaptive_options.geometry_memory_values.size()) {
        std::cerr << "--geometry-memory-topology-keys must contain one line "
                     "per geometry-memory row\n";
        return 7;
    }
    const std::string geometry_memory_merits_path =
        string_arg(argc, argv, "--geometry-memory-merits", "");
    if (!geometry_memory_merits_path.empty()) {
        adaptive_options.geometry_memory_merits =
            load_aligned_number_list_file(geometry_memory_merits_path);
    }
    if (adaptive_options.geometry_memory_qohr_mutation &&
        adaptive_options.geometry_memory_merits.size() !=
            adaptive_options.geometry_memory_values.size()) {
        std::cerr << "--geometry-memory-merits must contain one finite merit "
                     "per QOHR geometry-memory row\n";
        return 7;
    }
    if ((adaptive_options.geometry_memory_pbest_soft ||
         adaptive_options.geometry_memory_difference_mutation ||
         adaptive_options.geometry_memory_qohr_mutation) &&
        adaptive_options.geometry_memory_values.empty()) {
        std::cerr << "geometry-memory search guidance requires a non-empty "
                     "--geometry-memory-values file\n";
        return 7;
    }
    adaptive_options.family_transition_similarity_threshold = std::clamp(
        double_arg(
            argc,
            argv,
            "--family-transition-similarity-threshold",
            adaptive_options.family_transition_similarity_threshold),
        0.0,
        1.0);
    adaptive_options.family_transition_max_shape_diff = std::max(
        -1,
        int_arg(
            argc,
            argv,
            "--family-transition-max-shape-diff",
            adaptive_options.family_transition_max_shape_diff));
    return kContinueCommand;
}

} } }
