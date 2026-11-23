// Prepare the command vignetting state and optional automatic-vignetting operation.
// This stage runs on the loaded system before optimization inputs are finalized.
// Preserve aperture support, aiming configuration, and the distinction between measuring and applying updates.

#include "command_context.hpp"

namespace lensde { namespace cli { namespace detail {

int CommandContext::prepare_vignetting() {
    const bool aiming_diagnostics =
        has_arg(argc, argv, "--aiming-diagnostics") ||
        has_arg(argc, argv, "--aiming-diag");
    set_ray_aiming_diagnostics_enabled(aiming_diagnostics);
    reset_ray_aiming_diagnostics();
    const bool set_vignetting_requested = has_arg(argc, argv, "--set-vignetting");
    #ifdef _WIN32
        SYSTEM_INFO processor_info{};
        GetSystemInfo(&processor_info);
        int set_vignetting_aim_workers = static_cast<int>(
            std::max<DWORD>(1, processor_info.dwNumberOfProcessors));
    #else
        int set_vignetting_aim_workers = static_cast<int>(
            std::max(1u, std::thread::hardware_concurrency()));
    #endif
    for (const char* environment_name : {
             "OPTILAND_SET_VIGNETTING_AIM_WORKERS",
             "OPTILAND_CPU_WORKERS"}) {
        if (const char* value = std::getenv(environment_name)) {
            set_vignetting_aim_workers = std::max(1, std::atoi(value));
            break;
        }
    }
    const int set_vignetting_batch_apg_workers = std::max(
        1,
        int_arg(
            argc,
            argv,
            "--set-vignetting-batch-apg-workers",
            1));
    const bool parallel_set_vignetting_jacobian =
        set_vignetting_requested &&
        set_vignetting_aim_workers > 1 &&
        set_vignetting_batch_apg_workers <= 1 &&
        !has_arg(argc, argv, "--set-vignetting-no-parallel-jacobian");
    set_stop_aim_parallel_jacobian_enabled(parallel_set_vignetting_jacobian);
    if (set_vignetting_requested) {
        std::cout << "Automatic vignetting APG Jacobian pair execution: "
                  << (parallel_set_vignetting_jacobian
                          ? "adaptive serial/parallel (up to 2 workers)"
                          : "serial")
                  << "\n";
        AutomaticVignettingConfig vignetting_config;
        vignetting_config.aiming_mode = lens.ray_aiming.mode;
        vignetting_config.use_batch_apg =
            set_vignetting_batch_apg_workers > 1;
        vignetting_config.batch_apg_workers =
            set_vignetting_batch_apg_workers;
        vignetting_config.audit_batch_apg =
            has_arg(argc, argv, "--set-vignetting-batch-apg-audit");
        if (has_arg(argc, argv, "--set-vignetting-exact-measurement")) {
            vignetting_config.validation_aiming_mode = lens.ray_aiming.mode;
            vignetting_config.use_distinct_validation_aiming_mode = true;
            vignetting_config.aiming_mode = RayAimingMode::AdaptivePilotGrid;
        }
        vignetting_config.coarse_samples = std::max(
            5,
            int_arg(
                argc,
                argv,
                "--set-vignetting-coarse-samples",
                vignetting_config.coarse_samples));
        vignetting_config.boundary_iterations = std::max(
            1,
            int_arg(
                argc,
                argv,
                "--set-vignetting-boundary-iter",
                vignetting_config.boundary_iterations));
        vignetting_config.centroid_grid_cells = std::max(
            4,
            int_arg(
                argc,
                argv,
                "--set-vignetting-centroid-grid",
                vignetting_config.centroid_grid_cells));
        vignetting_config.use_current_stop_surface_radius =
            !has_arg(argc, argv, "--set-vignetting-derived-stop-radius");
        vignetting_config.aperture_tolerance_fraction = std::max(
            0.0,
            double_arg(
                argc,
                argv,
                "--set-vignetting-aperture-tolerance",
                vignetting_config.aperture_tolerance_fraction));
        vignetting_config.require_full_trace =
            has_arg(argc, argv, "--set-vignetting-full-trace");
        vignetting_config.all_wavelengths =
            has_arg(argc, argv, "--set-vignetting-all-wavelengths");
        vignetting_config.allow_aiming_fallback =
            !has_arg(argc, argv, "--set-vignetting-no-aiming-fallback");
        vignetting_config.validate_final_aiming_contract =
            has_arg(argc, argv, "--set-vignetting-validate-final-aiming");
        const AutomaticVignettingResult vignetting =
            set_automatic_vignetting(lens, vignetting_config);
        if (!vignetting.success) {
            std::cerr << "Automatic vignetting failed: "
                      << vignetting.message << "\n";
            for (const auto& field : vignetting.fields) {
                if (!field.success) {
                    std::cerr << "  field " << field.field_index
                              << ": " << field.message
                              << " connected_samples="
                              << field.connected_pupil_samples
                              << " branch_reseed="
                              << field.branch_reseed_successes << "/"
                              << field.branch_reseed_attempts
                              << " center_iterations="
                              << field.pupil_center_iterations << "\n";
                }
            }
            // A failed Set Vignetting solve is exactly where aiming diagnostics
            // are most useful.  Previously --aiming-diagnostics produced no
            // artifact on this early-return path, hiding whether the selected
            // aiming model, its seed domain, or the physical trace caused the
            // failure.
            print_ray_aiming_diagnostics(
                string_arg(argc, argv, "--out-dir", "out"));
            return 6;
        }
        std::cout << std::setprecision(12)
                  << "Automatic vignetting: wavelength["
                  << vignetting.wavelength_index << "]="
                  << vignetting.wavelength_um << " um\n";
        for (const auto& field : vignetting.fields) {
            const Field& solved = field.solved_field;
            std::cout << "  field " << field.field_index
                      << " fvcx=" << solved.fvcx
                      << " fvcy=" << solved.fvcy
                      << " fvdx=" << solved.fvdx
                      << " fvdy=" << solved.fvdy
                      << " limits(x- x+ y- y+)="
                      << field.x_min_limiting_surface << ' '
                      << field.x_max_limiting_surface << ' '
                      << field.y_min_limiting_surface << ' '
                      << field.y_max_limiting_surface
                      << " x_contraction=" << field.pupil_x_contraction
                      << " contraction=" << field.pupil_contraction
                      << " center_residual=" << field.pupil_center_residual
                      << " center_iterations=" << field.pupil_center_iterations
                      << " seed=" << field.seed_source
                      << " connected_samples="
                      << field.connected_pupil_samples
                      << " branch_reseed="
                      << field.branch_reseed_successes << "/"
                      << field.branch_reseed_attempts
                      << " pupil_evaluations=" << field.pupil_evaluations
                      << " envelope_traces=" << field.envelope_trace_calls
                      << " aperture_tests="
                      << field.hard_aperture_surface_tests
                      << " aiming_work_s=" << field.aiming_seconds
                      << " envelope_work_s="
                      << field.envelope_trace_seconds
                      << " centroid_s=" << field.centroid_seconds
                      << " centered_sections_s="
                      << field.centered_sections_seconds
                      << " batch_apg=" << field.batch_apg_evaluations
                      << " batch_fail=" << field.batch_apg_aim_failures
                      << " batch_recovery="
                      << field.batch_apg_serial_recoveries
                      << " batch_workers=" << field.batch_apg_workers
                      << " batch_aimed_mismatch="
                      << field.batch_apg_aimed_mismatches
                      << " batch_feasible_mismatch="
                      << field.batch_apg_feasible_mismatches
                      << " batch_surface_mismatch="
                      << field.batch_apg_limiting_surface_mismatches
                      << " batch_seed_mismatch="
                      << field.batch_apg_seed_mismatches
                      << " batch_max_seed_delta="
                      << field.batch_apg_max_seed_delta
                      << " batch_max_ratio_delta="
                      << field.batch_apg_max_boundary_ratio_delta << "\n";
        }
        std::cout << "Automatic vignetting timing: total_s="
                  << vignetting.total_seconds
                  << " final_validation_s="
                  << vignetting.final_validation_seconds << "\n";
    }
    lens.primary_wavelength_apertures = merit.vignetting_optimization_enabled();
    if (loaded_external_system && !update_input_clear_apertures) {
        sync_stop_aperture_to_system(lens);
    }
    if (update_input_clear_apertures) {
        update_clear_apertures_from_trace(
            lens,
            clear_aperture_update_rings(merit),
            "gaussian_ring_arm",
            merit.sampling_seed,
            lens.ray_aiming.mode,
            merit.effective_clear_aperture_margin(),
            true,
            merit.clear_aperture_arms,
            has_arg(argc, argv, "--set-vignetting") &&
                !has_arg(argc, argv, "--set-vignetting-derived-stop-radius"),
            has_arg(argc, argv, "--clear-aperture-require-full-trace"),
            merit.vignetting_boundary_support_tolerance,
            merit.vignetting_boundary_support_selectivity_slope,
            merit.vignetting_boundary_support_contract ==
                VignettingBoundarySupportContract::GlobalTrimRealizability,
            merit.vignetting_physical_cut_match_enabled,
            merit.vignetting_optimization_enabled());
    }
    return kContinueCommand;
}

} } }
