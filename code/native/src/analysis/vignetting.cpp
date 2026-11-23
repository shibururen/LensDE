/*
 * Coordinates automatic vignetting across fields and selected wavelengths.
 * The CLI measurement path calls this entry point to solve and validate FVC/FVD factors.
 * Reference-stop scans and final caller-aperture validation remain separate, and
 * solved fields are committed to the input lens only when the complete solve succeeds.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

AutomaticVignettingResult set_automatic_vignetting(
    LensSystem& system,
    const AutomaticVignettingConfig& config) {
    const auto solve_started = std::chrono::steady_clock::now();
    AutomaticVignettingResult result;
    if (system.fields.empty()) {
        result.message = "system has no fields";
        return result;
    }
    if (system.wavelengths.empty()) {
        result.message = "system has no wavelengths";
        return result;
    }
    if (system.surfaces.empty()) {
        result.message = "system has no surfaces";
        return result;
    }

    const int requested_batch_workers = std::max(
        std::max(1, config.batch_apg_workers),
        automatic_vignetting_batch_apg_workers());
    const bool parallel_current_fields =
        (config.use_batch_apg ||
         automatic_vignetting_batch_apg_experiment_enabled()) &&
        requested_batch_workers > 1 &&
        config.aiming_mode == RayAimingMode::AdaptivePilotGrid &&
        !config.all_wavelengths &&
        !config.validate_final_aiming_contract &&
        system.fields.size() > 1;
    if (parallel_current_fields) {
        const std::size_t worker_count = std::min<std::size_t>(
            system.fields.size(),
            static_cast<std::size_t>(requested_batch_workers));
        std::vector<AutomaticVignettingResult> field_runs(
            system.fields.size());
        std::vector<Field> solved_fields = system.fields;
        std::atomic<std::size_t> next_field{0};
        std::vector<AutomaticVignettingFieldBatchContext> contexts(
            worker_count);
#ifdef _WIN32
        std::vector<HANDLE> threads;
        threads.reserve(worker_count);
#else
        std::vector<std::thread> threads;
        threads.reserve(worker_count);
#endif
        for (std::size_t worker_index = 0;
             worker_index < worker_count;
             ++worker_index) {
            contexts[worker_index] = {
                &system,
                &config,
                &field_runs,
                &solved_fields,
                &next_field};
#ifdef _WIN32
            HANDLE thread = CreateThread(
                nullptr,
                0,
                &automatic_vignetting_field_batch_thread_entry,
                &contexts[worker_index],
                0,
                nullptr);
            if (thread) {
                threads.push_back(thread);
            } else {
                run_automatic_vignetting_field_batch(
                    &contexts[worker_index]);
            }
#else
            threads.emplace_back(
                &run_automatic_vignetting_field_batch,
                &contexts[worker_index]);
#endif
        }
#ifdef _WIN32
        for (HANDLE thread : threads) {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
        }
#else
        for (auto& thread : threads) {
            thread.join();
        }
#endif

        result.fields.reserve(system.fields.size());
        bool all_success = true;
        for (std::size_t field_index = 0;
             field_index < field_runs.size();
             ++field_index) {
            AutomaticVignettingResult& field_run = field_runs[field_index];
            if (field_index == 0 ||
                !std::isfinite(result.wavelength_um)) {
                result.wavelength_index = field_run.wavelength_index;
                result.wavelength_um = field_run.wavelength_um;
            }
            if (!field_run.success || field_run.fields.size() != 1) {
                all_success = false;
                AutomaticVignettingFieldResult failed;
                failed.field_index = field_index;
                failed.original_field = system.fields[field_index];
                failed.message = field_run.message.empty()
                                     ? "parallel current-field solve failed"
                                     : field_run.message;
                result.fields.push_back(std::move(failed));
                continue;
            }
            AutomaticVignettingFieldResult field_result =
                std::move(field_run.fields.front());
            field_result.field_index = field_index;
            field_result.original_field = system.fields[field_index];
            field_result.batch_apg_workers = std::max<std::size_t>(
                field_result.batch_apg_workers, worker_count);
            result.fields.push_back(std::move(field_result));
        }
        result.success = all_success;
        result.total_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - solve_started).count();
        if (!all_success) {
            result.message =
                "automatic vignetting failed; the input system was left unchanged";
            return result;
        }
        system.fields = std::move(solved_fields);
        result.message = "automatic vignetting solved for all fields";
        return result;
    }

    LensSystem working = system;
    working.ray_aiming.mode = config.aiming_mode;
    if (config.aiming_mode != RayAimingMode::Direct &&
        config.aiming_mode != RayAimingMode::Paraxial) {
        working.ray_aiming.require_convergence = true;
    }
    if (config.aiming_mode == RayAimingMode::AdaptivePilotGrid) {
        working.ray_aiming.adaptive_pilot_grid.enabled = true;
        working.ray_aiming.adaptive_pilot_grid.require_final_convergence = true;
    }
    if (config.use_current_stop_surface_radius) {
        const auto stop = std::find_if(
            working.surfaces.begin(),
            working.surfaces.end(),
            [](const Surface& surface) { return surface.is_stop; });
        if (stop != working.surfaces.end() &&
            std::isfinite(stop->semi_aperture) &&
            stop->semi_aperture > 0.0) {
            // This is a solver-local policy.  Only fields are copied back to
            // the caller, so the original aperture specification is retained.
            working.aperture.type = ApertureType::FloatByStopSize;
        }
    }
    for (Field& field : working.fields) {
        field.fvcx = 0.0;
        field.fvcy = 0.0;
        field.fvdx = 0.0;
        field.fvdy = 0.0;
        field.vignette_x = 0.0;
        field.vignette_y = 0.0;
    }

    const std::size_t wave_index =
        automatic_vignetting_wavelength_index(working, config);
    std::vector<std::size_t> wave_indices{wave_index};
    if (config.all_wavelengths) {
        for (std::size_t candidate = 0;
             candidate < working.wavelengths.size();
             ++candidate) {
            if (candidate != wave_index) {
                wave_indices.push_back(candidate);
            }
        }
    }
    std::vector<std::size_t> final_validation_wave_indices = wave_indices;
    if (config.all_wavelengths) {
        final_validation_wave_indices.clear();
        for (std::size_t candidate = 0;
             candidate < working.wavelengths.size();
             ++candidate) {
            final_validation_wave_indices.push_back(candidate);
        }
    }
    result.wavelength_index = wave_index;
    result.wavelength_um = working.wavelengths[wave_index].value_um;
    result.fields.reserve(working.fields.size());

    // CalibratedParaxial and ThreeStep deliberately reuse center-field,
    // previous-field, and primary-wavelength anchors.  Merit evaluation uses
    // one AimingSession across the field/wavelength traversal, but the former
    // Set Vignetting implementation created an isolated session for every
    // field and wavelength.  That made valid off-axis sampled pupils appear
    // untraceable whenever their model needed the preceding anchor chain.  Use
    // the same reference-pupil system and session for the complete solve so a
    // strict no-fallback bridge follows the serialized aiming contract.
    LensSystem shared_reference_system =
        automatic_vignetting_reference_pupil_system(working);
    AimingSession shared_reference_aiming(shared_reference_system);

    bool all_success = true;
    for (std::size_t field_index = 0;
         field_index < working.fields.size();
         ++field_index) {
        AutomaticVignettingFieldResult field_result;
        field_result.field_index = field_index;
        field_result.original_field = system.fields[field_index];
        const Field& source_field = system.fields[field_index];
        std::vector<std::pair<double, double>> preferred_seeds;
        if (std::isfinite(source_field.fvcx) &&
            std::isfinite(source_field.fvcy)) {
            preferred_seeds.push_back({source_field.fvcx, source_field.fvcy});
        }
        AutomaticVignettingFieldSolver solver(
            working,
            field_index,
            wave_indices,
            config,
            &shared_reference_aiming,
            std::move(preferred_seeds));
        double pupil_centroid_x = 0.0;
        double pupil_centroid_y = 0.0;
        const auto centroid_started = std::chrono::steady_clock::now();
        const bool centroid_valid =
            solver.grid_centroid(&pupil_centroid_x, &pupil_centroid_y);
        field_result.centroid_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - centroid_started).count();
        auto capture_solver_stats = [&]() {
            field_result.pupil_evaluations =
                solver.pupil_evaluation_count();
            field_result.branch_reseed_attempts =
                solver.branch_reseed_attempt_count();
            field_result.branch_reseed_successes =
                solver.branch_reseed_success_count();
            field_result.envelope_trace_calls =
                solver.envelope_trace_count();
            field_result.hard_aperture_surface_tests =
                solver.hard_aperture_surface_test_count();
            field_result.aiming_seconds = solver.aiming_seconds();
            field_result.envelope_trace_seconds =
                solver.envelope_trace_seconds();
            field_result.batch_apg_evaluations =
                solver.batch_apg_evaluation_count();
            field_result.batch_apg_aim_failures =
                solver.batch_apg_aim_failure_count();
            field_result.batch_apg_serial_recoveries =
                solver.batch_apg_serial_recovery_count();
            field_result.batch_apg_aimed_mismatches =
                solver.batch_apg_aimed_mismatch_count();
            field_result.batch_apg_feasible_mismatches =
                solver.batch_apg_feasible_mismatch_count();
            field_result.batch_apg_limiting_surface_mismatches =
                solver.batch_apg_limiting_surface_mismatch_count();
            field_result.batch_apg_seed_mismatches =
                solver.batch_apg_seed_mismatch_count();
            field_result.batch_apg_workers =
                solver.batch_apg_worker_count();
            field_result.batch_apg_max_seed_delta =
                solver.batch_apg_max_seed_delta();
            field_result.batch_apg_max_boundary_ratio_delta =
                solver.batch_apg_max_boundary_ratio_delta();
        };
        if (!centroid_valid) {
            capture_solver_stats();
            field_result.message =
                "no traceable pupil samples were found in the pupil grid";
            result.fields.push_back(std::move(field_result));
            all_success = false;
            continue;
        }
        field_result.seed_source = solver.seed_source();
        field_result.connected_pupil_samples =
            solver.connected_sample_count();
        // The connected-component centroid selects the intended feasible pupil
        // and initializes the solve.  Refine it until it is also the midpoint
        // of both orthogonal hard-aperture chords.  Then the four traced chord
        // endpoints are the exact four axial endpoints serialized by FVC/FVD.
        const auto centered_sections_started =
            std::chrono::steady_clock::now();
        AutomaticVignettingCenteredSections centered_sections =
            solve_automatic_vignetting_centered_sections(
                solver,
                pupil_centroid_x,
                pupil_centroid_y,
                config.pupil_tolerance);
        if (!centered_sections.valid && solver.enable_branch_recovery()) {
            centered_sections = solve_automatic_vignetting_centered_sections(
                solver, pupil_centroid_x, pupil_centroid_y, config.pupil_tolerance);
        }
        field_result.centered_sections_seconds =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now()
                - centered_sections_started).count();
        capture_solver_stats();
        const AutomaticVignettingFieldSolver::Interval& x_interval =
            centered_sections.x_interval;
        const AutomaticVignettingFieldSolver::Interval& y_interval =
            centered_sections.y_interval;
        field_result.pupil_center_residual =
            centered_sections.center_residual;
        field_result.pupil_center_iterations = centered_sections.iterations;

        if (centered_sections.valid) {
            Field solved = working.fields[field_index];
            const double center_x = centered_sections.center_x;
            const double center_y = centered_sections.center_y;
            const double half_width_x =
                0.5 * (x_interval.upper - x_interval.lower);
            const double half_width_y =
                0.5 * (y_interval.upper - y_interval.lower);
            solved.fvcx = center_x;
            solved.fvcy = center_y;
            solved.fvdx = std::clamp(1.0 - half_width_x, 0.0, 1.0);
            solved.fvdy = std::clamp(1.0 - half_width_y, 0.0, 1.0);
            solved.vignette_x = solved.fvdx;
            solved.vignette_y = solved.fvdy;
            if (std::hypot(solved.hx, solved.hy) <= 1.0e-12) {
                solved.fvcx = 0.0;
                solved.fvcy = 0.0;
                solved.fvdx = 0.0;
                solved.fvdy = 0.0;
                solved.vignette_x = 0.0;
                solved.vignette_y = 0.0;
            }
            double pupil_x_contraction = 1.0;
            if (config.validate_final_aiming_contract) {
                if (!project_automatic_vignetting_x_inside_unit_pupil(
                        &solved, &pupil_x_contraction)) {
                    all_success = false;
                    field_result.message =
                        "vignetting ellipse cannot fit inside the unit pupil";
                    result.fields.push_back(std::move(field_result));
                    continue;
                }
            }
            working.fields[field_index] = solved;

            field_result.solved_field = solved;
            field_result.pupil_x_min = x_interval.lower;
            field_result.pupil_x_max = x_interval.upper;
            field_result.pupil_y_min = y_interval.lower;
            field_result.pupil_y_max = y_interval.upper;
            field_result.x_min_limiting_surface = x_interval.lower_surface;
            field_result.x_max_limiting_surface = x_interval.upper_surface;
            field_result.y_min_limiting_surface = y_interval.lower_surface;
            field_result.y_max_limiting_surface = y_interval.upper_surface;
            field_result.pupil_x_contraction = pupil_x_contraction;
            field_result.pupil_contraction = 1.0;
            field_result.success = true;
        } else {
            all_success = false;
            field_result.message = centered_sections.message.empty()
                                       ? "self-consistent pupil sections failed"
                                       : centered_sections.message;
        }
        result.fields.push_back(std::move(field_result));
    }

    if (all_success && config.validate_final_aiming_contract) {
        const auto final_validation_started =
            std::chrono::steady_clock::now();
        // The hard-aperture scan above temporarily switches to
        // FloatByStopSize.  Validate the serialized factors only after all
        // fields have been solved, using the caller's original aperture
        // specification and the same field-major history as merit scoring.
        LensSystem validation_system = system;
        validation_system.fields = working.fields;
        const RayAimingMode validation_mode =
            config.use_distinct_validation_aiming_mode
                ? config.validation_aiming_mode
                : config.aiming_mode;
        validation_system.ray_aiming.mode = validation_mode;
        if (validation_mode != RayAimingMode::Direct &&
            validation_mode != RayAimingMode::Paraxial) {
            validation_system.ray_aiming.require_convergence = true;
        }
        if (!config.allow_aiming_fallback) {
            if (validation_mode == RayAimingMode::CalibratedParaxial) {
                validation_system.ray_aiming.calibrated_paraxial.fallback = "none";
            } else if (validation_mode == RayAimingMode::ThreeStep) {
                validation_system.ray_aiming.three_step.fallback = "none";
            }
        }
        for (std::size_t field_index = 0;
             field_index < validation_system.fields.size();
             ++field_index) {
            Field solved = validation_system.fields[field_index];
            double pupil_contraction = 1.0;
            if (!contract_to_valid_automatic_vignetting_field(
                    validation_system,
                    field_index,
                    final_validation_wave_indices,
                    &solved,
                    &pupil_contraction)) {
                all_success = false;
                result.fields[field_index].success = false;
                result.fields[field_index].message =
                    "final serialized aiming contract failed for the polychromatic pupil";
                break;
            }
            validation_system.fields[field_index] = solved;
            working.fields[field_index] = solved;
            result.fields[field_index].solved_field = solved;
            result.fields[field_index].pupil_contraction = pupil_contraction;
        }
        result.final_validation_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now()
            - final_validation_started).count();
    }

    result.success = all_success;
    result.total_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - solve_started).count();
    if (!all_success) {
        result.message =
            "automatic vignetting failed; the input system was left unchanged";
        return result;
    }
    system.fields = std::move(working.fields);
    result.message = "automatic vignetting solved for all fields";
    return result;
}

}  // namespace optiland
