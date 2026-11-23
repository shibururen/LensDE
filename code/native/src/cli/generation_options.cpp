// Parse initial/Refresh generation policies and load any configured Refresh seed pool.
// Ordinary, hybrid, and adaptive option preparation share the same target-default helpers.
// Keep physical target inheritance, pool dimensions, and explicit CLI overrides in their current order.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

void apply_refresh_generation_args(int argc, char** argv, PopulationGenerationOptions& options) {
    options.mode = population_generation_mode_from_string(
        string_arg(argc, argv, "--refresh-generation-mode",
            string_arg(argc, argv, "--refresh-generation",
                population_generation_mode_name(options.mode))));
    options.seed_pool_path =
        string_arg(argc, argv, "--refresh-seed-pool", options.seed_pool_path);
    options.seed_pool_fraction =
        double_arg(argc, argv, "--refresh-seed-pool-fraction", options.seed_pool_fraction);
    options.seed_pool_perturbation =
        double_arg(argc, argv, "--refresh-seed-pool-perturbation", options.seed_pool_perturbation);
    if (has_arg(argc, argv, "--refresh-trace-feasible-gate")) {
        options.trace_feasible_gate = true;
    }
    if (has_arg(argc, argv, "--refresh-no-trace-feasible-gate")) {
        options.trace_feasible_gate = false;
    }
    if (has_arg(argc, argv, "--refresh-trace-archive")) {
        options.trace_archive_enabled = true;
    }
    if (has_arg(argc, argv, "--refresh-no-trace-archive")) {
        options.trace_archive_enabled = false;
    }
    if (has_arg(argc, argv, "--refresh-adaptive-trigger")) {
        options.adaptive_trigger_enabled = true;
    }
    if (has_arg(argc, argv, "--refresh-no-adaptive-trigger")) {
        options.adaptive_trigger_enabled = false;
    }
    options.target_count =
        int_arg(argc, argv, "--refresh-target-count", options.target_count);
    options.max_candidates =
        int_arg(argc, argv, "--refresh-max-candidates", options.max_candidates);
    options.max_seconds =
        double_arg(argc, argv, "--refresh-max-seconds", options.max_seconds);
    options.parent_count =
        int_arg(argc, argv, "--refresh-parent-count", options.parent_count);
    options.trace_archive_capacity =
        int_arg(argc, argv, "--refresh-trace-archive-capacity", options.trace_archive_capacity);
    options.trace_archive_fraction =
        double_arg(argc, argv, "--refresh-trace-archive-fraction", options.trace_archive_fraction);
    options.trace_archive_min_distance =
        double_arg(argc, argv, "--refresh-trace-archive-min-distance",
                   options.trace_archive_min_distance);
    options.adaptive_max_fraction =
        double_arg(argc, argv, "--refresh-adaptive-max-fraction",
                   options.adaptive_max_fraction);
    options.adaptive_min_acceptance_rate =
        double_arg(argc, argv, "--refresh-adaptive-min-acceptance-rate",
                   options.adaptive_min_acceptance_rate);
    options.adaptive_min_rel_improvement =
        double_arg(argc, argv, "--refresh-adaptive-min-rel-improvement",
                   options.adaptive_min_rel_improvement);
    options.curvature_relative_span =
        double_arg(argc, argv, "--refresh-curvature-rel", options.curvature_relative_span);
    options.air_thickness_relative_span =
        double_arg(argc, argv, "--refresh-air-thickness-rel", options.air_thickness_relative_span);
    options.glass_thickness_relative_span =
        double_arg(argc, argv, "--refresh-glass-thickness-rel", options.glass_thickness_relative_span);
    options.curvature_absolute_span =
        double_arg(argc, argv, "--refresh-curvature-abs", options.curvature_absolute_span);
    options.air_thickness_absolute_span =
        double_arg(argc, argv, "--refresh-air-thickness-abs", options.air_thickness_absolute_span);
    options.glass_thickness_absolute_span =
        double_arg(argc, argv, "--refresh-glass-thickness-abs", options.glass_thickness_absolute_span);
    options.asphere_sag_perturbation_um = std::max(
        0.0,
        double_arg(argc, argv, "--refresh-asphere-sag-um",
                   options.asphere_sag_perturbation_um));
    options.asphere_sag_correlation_length = std::max(
        1.0e-3,
        double_arg(argc, argv, "--refresh-asphere-correlation-length",
                   options.asphere_sag_correlation_length));
    options.geometry_perturbation_fraction = std::max(
        0.0,
        double_arg(argc, argv, "--refresh-geometry-fraction",
                   options.geometry_perturbation_fraction));
    options.max_merit_ratio = std::max(
        0.0,
        double_arg(argc, argv, "--refresh-max-merit-ratio",
                   options.max_merit_ratio));
    options.geometry_child_fraction = std::clamp(
        double_arg(argc, argv, "--refresh-geometry-child-fraction",
                   options.geometry_child_fraction), 0.0, 1.0);
    options.geometry_local_scale = std::max(
        0.0,
        double_arg(argc, argv, "--refresh-geometry-local-scale",
                   options.geometry_local_scale));
    options.geometry_single_variable =
        has_arg(argc, argv, "--refresh-geometry-single-variable");
    options.vignetting_child_fraction = std::clamp(
        double_arg(argc, argv, "--refresh-vignetting-child-fraction",
                   options.vignetting_child_fraction), 0.0, 1.0);
    options.vignetting_bound_fraction = std::max(
        0.0,
        double_arg(argc, argv, "--refresh-vignetting-bound-fraction",
                   options.vignetting_bound_fraction));
    options.physics_target_ttl =
        double_arg(argc, argv, "--refresh-physics-target-ttl", options.physics_target_ttl);
    options.physics_target_efl =
        double_arg(argc, argv, "--refresh-physics-target-efl", options.physics_target_efl);
    options.physics_target_bfl =
        double_arg(argc, argv, "--refresh-physics-target-bfl", options.physics_target_bfl);
    options.physics_ttl_min_fraction =
        double_arg(argc, argv, "--refresh-physics-ttl-min-fraction",
                   options.physics_ttl_min_fraction);
    options.physics_ttl_max_fraction =
        double_arg(argc, argv, "--refresh-physics-ttl-max-fraction",
                   options.physics_ttl_max_fraction);
    options.physics_power_jitter =
        double_arg(argc, argv, "--refresh-physics-power-jitter", options.physics_power_jitter);
    options.physics_sa_iterations =
        int_arg(argc, argv, "--refresh-physics-sa-iterations",
                options.physics_sa_iterations);
    options.physics_sa_temperature =
        double_arg(argc, argv, "--refresh-physics-sa-temperature",
                   options.physics_sa_temperature);
    if (has_arg(argc, argv, "--refresh-perturb-material-variables")) {
        options.perturb_material_variables = true;
    }
    if (has_arg(argc, argv, "--refresh-no-perturb-material-variables")) {
        options.perturb_material_variables = false;
    }
    if (has_arg(argc, argv, "--refresh-perturb-other-variables")) {
        options.perturb_other_variables = true;
    }
    if (has_arg(argc, argv, "--refresh-free-all-variables")) {
        options.perturb_material_variables = true;
        options.perturb_other_variables = true;
    }
}

void apply_initial_generation_args(int argc, char** argv, PopulationGenerationOptions& options) {
    options.mode = population_generation_mode_from_string(
        string_arg(argc, argv, "--initial-generation-mode",
            string_arg(argc, argv, "--initial-generation",
                population_generation_mode_name(options.mode))));
    options.physics_target_ttl =
        double_arg(argc, argv, "--initial-physics-target-ttl", options.physics_target_ttl);
    options.physics_target_efl =
        double_arg(argc, argv, "--initial-physics-target-efl", options.physics_target_efl);
    options.physics_target_bfl =
        double_arg(argc, argv, "--initial-physics-target-bfl", options.physics_target_bfl);
    options.physics_ttl_min_fraction =
        double_arg(argc, argv, "--initial-physics-ttl-min-fraction",
                   options.physics_ttl_min_fraction);
    options.physics_ttl_max_fraction =
        double_arg(argc, argv, "--initial-physics-ttl-max-fraction",
                   options.physics_ttl_max_fraction);
    options.physics_power_jitter =
        double_arg(argc, argv, "--initial-physics-power-jitter", options.physics_power_jitter);
    options.physics_sa_iterations =
        int_arg(argc, argv, "--initial-physics-sa-iterations",
                options.physics_sa_iterations);
    options.physics_sa_temperature =
        double_arg(argc, argv, "--initial-physics-sa-temperature",
                   options.physics_sa_temperature);
    if (has_arg(argc, argv, "--initial-perturb-material-variables")) {
        options.perturb_material_variables = true;
    }
    if (has_arg(argc, argv, "--initial-no-perturb-material-variables")) {
        options.perturb_material_variables = false;
    }
    if (has_arg(argc, argv, "--initial-perturb-other-variables")) {
        options.perturb_other_variables = true;
    }
}

void apply_population_generation_target_defaults(
    PopulationGenerationOptions& options,
    const MeritConfig& merit,
    const LensSystem& lens) {
    if (options.physics_target_ttl <= 0.0 && merit.target_ttl > 0.0) {
        options.physics_target_ttl = merit.target_ttl;
    }
    if (options.physics_target_efl <= 0.0 && merit.target_efl > 0.0) {
        options.physics_target_efl = merit.target_efl;
    }
    if (options.physics_target_bfl <= 0.0 && merit.target_bfl > 0.0) {
        options.physics_target_bfl = merit.target_bfl;
    }
    if (options.physics_target_ttl <= 0.0 && std::isfinite(lens.total_track())) {
        options.physics_target_ttl = lens.total_track();
    }
}

bool load_refresh_seed_pool(
    PopulationGenerationOptions& options,
    int dim,
    std::ostream& err) {
    options.seed_pool_fraction = std::clamp(options.seed_pool_fraction, 0.0, 1.0);
    options.seed_pool_perturbation = std::clamp(options.seed_pool_perturbation, 0.0, 1.0);
    options.trace_archive_capacity = std::max(0, options.trace_archive_capacity);
    options.trace_archive_fraction = std::clamp(options.trace_archive_fraction, 0.0, 1.0);
    options.trace_archive_min_distance =
        std::max(0.0, options.trace_archive_min_distance);
    options.adaptive_max_fraction = std::clamp(options.adaptive_max_fraction, 0.0, 1.0);
    options.adaptive_min_acceptance_rate =
        std::max(0.0, options.adaptive_min_acceptance_rate);
    options.adaptive_min_rel_improvement =
        std::max(0.0, options.adaptive_min_rel_improvement);
    options.seed_pool_values.clear();
    if (options.seed_pool_path.empty() || options.seed_pool_fraction <= 0.0) {
        return true;
    }
    std::string error;
    if (!load_population_csv(options.seed_pool_path, dim, &options.seed_pool_values, &error)) {
        err << "Warning: disabled refresh seed pool because it could not be loaded: "
            << error << "\n";
        options.seed_pool_fraction = 0.0;
        options.seed_pool_values.clear();
        return true;
    }
    if (options.seed_pool_values.empty()) {
        err << "Warning: disabled refresh seed pool because it is empty: "
            << options.seed_pool_path << "\n";
        options.seed_pool_fraction = 0.0;
        options.seed_pool_values.clear();
        return true;
    }
    return true;
}

} } }
