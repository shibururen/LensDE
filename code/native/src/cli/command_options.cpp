// Resolve execution selectors, run budgets, output scope, and variable preparation controls.
// The shared CommandContext carries these mode-dependent choices into evaluation and optimization.
// Keep adaptive/hybrid/ordinary precedence, scaling defaults, and input aperture preparation intact.

#include "command_context.hpp"

namespace lensde { namespace cli { namespace detail {

int CommandContext::configure_execution() {
    const std::string optimizer_mode =
        normalized_option_token(string_arg(argc, argv, "--optimizer", ""));
    const bool optimizer_requests_adaptive =
        (optimizer_mode == "adaptive_hybrid_de_dls" ||
         optimizer_mode == "adaptive_hybrid" ||
         optimizer_mode == "adaptive");
    run_adaptive_hybrid = (has_arg(argc, argv, "--adaptive-hybrid-de-dls") ||
             has_arg(argc, argv, "--adaptive-hybrid") ||
             optimizer_requests_adaptive);
    run_hybrid = !run_adaptive_hybrid &&
            (has_arg(argc, argv, "--hybrid-de-dls") ||
             has_arg(argc, argv, "--hybrid") ||
             optimizer_mode == "hybrid_de_dls" ||
             optimizer_mode == "hybrid");
    score_population_only = has_arg(argc, argv, "--score-population-only");
    score_population_detail_index = int_arg(argc, argv, "--score-population-detail-index", -1);
    select_seed_population_only = has_arg(argc, argv, "--select-seed-population");
    run_de = score_population_only ||
            select_seed_population_only ||
            has_arg(argc, argv, "--de") ||
            run_hybrid ||
            run_adaptive_hybrid ||
            optimizer_mode == "de" ||
            optimizer_mode == "differential_evolution";
    const bool apply_variables_only =
        has_arg(argc, argv, "--apply-variables") ||
        has_arg(argc, argv, "--score-variables");
    run_local = apply_variables_only ||
                               has_arg(argc, argv, "--local") || has_arg(argc, argv, "--local-only") ||
                               has_arg(argc, argv, "--polish-only") ||
                               optimizer_mode == "local" ||
                               optimizer_mode == "fine_polish";
    run_quick_focus = has_arg(argc, argv, "--quick-focus");
    run_polish = !apply_variables_only && (run_local || has_arg(argc, argv, "--polish"));
    const bool force_cpu = has_arg(argc, argv, "--cpu");
    allow_cuda_for_merit = !force_cpu && distortion_model_allows_cuda_local(merit);
    if (std::getenv("OPTILAND_DE_CUDA_SUPPORT_DEBUG")) {
        std::cerr << "DE-CUDA main gate: force_cpu=" << (force_cpu ? 1 : 0)
                  << " distortion_model='" << merit.distortion_model << "'"
                  << " normalized='"
                  << normalized_token_local(merit.distortion_model) << "'"
                  << " allow_cuda_for_merit="
                  << (allow_cuda_for_merit ? 1 : 0) << "\n";
    }
    population = int_arg(argc, argv, "--pop", 256);
    popsize = int_arg(argc, argv, "--popsize", 15);
    generations = int_arg(argc, argv, "--gen", 48);
    de_cuda_min_batch_size = std::max(
            0,
            int_arg(argc, argv, "--de-cuda-min-batch",
                int_arg(argc, argv, "--cuda-min-batch", 0)));
    out_dir = string_arg(argc, argv, "--out-dir", "out");
    score_only_output = has_arg(argc, argv, "--score-only") ||
            has_arg(argc, argv, "--scores-only") ||
            has_arg(argc, argv, "--no-detail-output");
    chief_aiming_probe = has_arg(argc, argv, "--chief-aiming-probe");
    three_step_chief_probe = has_arg(argc, argv, "--three-step-chief-probe") ||
            has_arg(argc, argv, "--three-step-aiming-probe");
    calibrated_edge_initial_probe = has_arg(argc, argv, "--calibrated-edge-initial-probe") ||
            has_arg(argc, argv, "--calibrated-edge-seed-probe") ||
            has_arg(argc, argv, "--aiming-calibrated-edge-initial-probe");
    center_edge_iterative_probe = has_arg(argc, argv, "--center-edge-iterative-probe") ||
            has_arg(argc, argv, "--calibrated-edge-iterative-probe") ||
            has_arg(argc, argv, "--aiming-calibrated-edge-iterative-probe");
    chief_aiming_probe_iterations = std::max(0, int_arg(argc, argv, "--chief-aiming-probe-iter", 3));
    internal_scaling_explicit = has_arg(argc, argv, "--optimizer-internal-scaling");
    internal_scaling = internal_scaling_from_string(
            string_arg(argc, argv, "--optimizer-internal-scaling",
                       (run_adaptive_hybrid) ? "edge_sag_normalized" : "legacy_optical"));
    const bool asphere_reference_policy_explicit =
        has_arg(argc, argv, "--asphere-reference-policy");
    const std::string asphere_reference_policy_raw = string_arg(
        argc,
        argv,
        "--asphere-reference-policy",
        asphere_reference_policy_explicit ? "" : "current");
    asphere_reference_policy = AsphereReferencePolicy::Current;
    if (!parse_asphere_reference_policy(
            asphere_reference_policy_raw,
            &asphere_reference_policy)) {
        std::cerr << "Invalid --asphere-reference-policy value '"
                  << asphere_reference_policy_raw
                  << "'; expected current or input\n";
        return 4;
    }
    local_bounds_mode = local_bounds_mode_arg(argc, argv);
    spot_num_rays = std::max(
            1,
            int_arg(argc, argv, "--spot-rays",
                int_arg(argc, argv, "--spot-samples", std::max(41, merit.num_rays))));
    spot_pupil_arms = std::max(
            1,
            int_arg(argc, argv, "--spot-arms",
                int_arg(argc, argv, "--spot-pupil-arms", std::max(64, merit.pupil_arms))));
    spot_distribution = string_arg(argc, argv, "--spot-distribution", "uniform");
    spot_sampling_seed = static_cast<unsigned int>(
            std::max(0, int_arg(argc, argv, "--spot-sampling-seed",
                static_cast<int>(merit.sampling_seed))));
    relative_illumination_requested = has_arg(argc, argv, "--relative-illumination") ||
            has_arg(argc, argv, "--rel-illumination") ||
            has_arg(argc, argv, "--ri");
    // relative_illumination_config is owned by this command stage.
    relative_illumination_config.ray_density =
        std::max(5, int_arg(argc, argv, "--ri-ray-density", 20));
    relative_illumination_config.field_density =
        std::max(1, int_arg(argc, argv, "--ri-field-density", 21));
    relative_illumination_config.wavelength_um = double_arg(
        argc,
        argv,
        "--ri-wavelength",
        std::numeric_limits<double>::quiet_NaN());
    relative_illumination_config.remove_vignetting_factors =
        !has_arg(argc, argv, "--ri-keep-vignetting-factors");
    relative_illumination_config.normalize_to_maximum =
        !has_arg(argc, argv, "--ri-normalize-on-axis");
    const std::string ri_scan = normalized_option_token(
        string_arg(argc, argv, "--ri-scan", "+y"));
    if (ri_scan == "x" || ri_scan == "+x" || ri_scan == "positive_x") {
        relative_illumination_config.scan_hx = 1.0;
        relative_illumination_config.scan_hy = 0.0;
    } else if (ri_scan == "-x" || ri_scan == "negative_x") {
        relative_illumination_config.scan_hx = -1.0;
        relative_illumination_config.scan_hy = 0.0;
    } else if (ri_scan == "-y" || ri_scan == "negative_y") {
        relative_illumination_config.scan_hx = 0.0;
        relative_illumination_config.scan_hy = -1.0;
    }
    return kContinueCommand;
}

int CommandContext::prepare_output() {
    if (run_adaptive_hybrid) {
        merit.dynamic_apertures = true;
        if (!update_input_clear_apertures && !input_clear_apertures_materialized) {
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
        }
    }
    ensure_output_directory(out_dir);
    if (has_arg(argc, argv, "--benchmark-single-system") ||
        has_arg(argc, argv, "--single-system-benchmark") ||
        has_arg(argc, argv, "--diagnose-single-system-time")) {
        throw std::invalid_argument("Historical single-system microbenchmarks are not part of LensDE");
    }
    return kContinueCommand;
}

int CommandContext::configure_variables() {
    const std::string configured_variables_path = string_arg(argc, argv, "--variables");
    if (!configured_variables_path.empty()) {
        std::vector<VariableSpec> configured_variables;
        std::string error;
        if (!load_variables_json(configured_variables_path, &configured_variables, &error)) {
            std::cerr << "Failed to load variables: " << error << "\n";
            return 5;
        }
        configure_variable_thickness_constraints(&merit, configured_variables);
    }
    return kContinueCommand;
}

} } }
