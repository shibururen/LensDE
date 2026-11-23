// Write CSV evidence for chief, three-step, calibrated-edge, and center-edge aiming probes.
// Command evaluation calls these writers only for the requested diagnostic outputs.
// Preserve probe ordering, failure labels, and non-finite number handling in the emitted columns.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

const char* ray_failure_code_csv_label(RayFailureCode code) {
    switch (code) {
        case RayFailureCode::None:
            return "none";
        case RayFailureCode::AimNotConverged:
            return "aim_not_converged";
        case RayFailureCode::SurfaceMiss:
            return "surface_miss";
        case RayFailureCode::ApertureClip:
            return "aperture_clip";
        case RayFailureCode::TotalInternalReflection:
            return "tir";
        case RayFailureCode::NonFinite:
            return "non_finite";
        case RayFailureCode::BackwardPropagation:
            return "backward_propagation";
        case RayFailureCode::SagInvalid:
            return "sag_invalid";
        case RayFailureCode::GeometryInvalid:
            return "geometry_invalid";
    }
    return "unknown";
}

void write_csv_number(std::ostream& out, double value) {
    if (std::isfinite(value)) {
        out << std::setprecision(12) << value;
    }
}

void save_chief_aiming_probe_csv(
    const LensSystem& lens,
    const std::string& path,
    int max_budget_iterations) {
    AimingSession session(lens);
    const auto probes = session.probe_chief_aiming(max_budget_iterations);
    std::ofstream out(path);
    out << "field_index,hx,hy,wave_index,wavelength_um,target_stop_x,target_stop_y,"
        << "stop_target_radius,physical_stop_radius,abcd_seed_valid,abcd_singular,"
        << "abcd_seed_oob,abcd_seed_x,abcd_seed_y,full_anchor_valid,full_seed_x,"
        << "full_seed_y,abcd_to_full_seed_dx,abcd_to_full_seed_dy,abcd_to_full_seed_dr,"
        << "full_stop_x,full_stop_y,full_stop_radial,full_stop_residual,"
        << "seed_strategy,inward_factor,center_meridional_radius,"
        << "budget_iterations,budget_trace_to_stop,budget_aim_converged,"
        << "budget_first_surface_seed_valid,budget_first_surface_x,budget_first_surface_y,"
        << "budget_to_full_seed_dx,budget_to_full_seed_dy,budget_to_full_seed_dr,"
        << "budget_stop_x,budget_stop_y,budget_stop_radial,budget_stop_residual,"
        << "budget_stop_to_full_dx,budget_stop_to_full_dy,budget_stop_to_full_dr,"
        << "budget_aim_residual,budget_last_valid_surface,budget_failure_code,"
        << "budget_failure_surface,budget_failure_severity,budget_trace_failure_code,"
        << "budget_trace_failure_surface,budget_trace_failure_severity\n";

    auto write_probe_prefix = [&](const AimingSession::ChiefAimingProbe& probe) {
        out << probe.field_index << "," << std::setprecision(12) << probe.hx << ","
            << probe.hy << "," << probe.wave_index << "," << probe.wavelength_um << ",";
        write_csv_number(out, probe.target_stop_x);
        out << ",";
        write_csv_number(out, probe.target_stop_y);
        out << ",";
        write_csv_number(out, probe.stop_target_radius);
        out << ",";
        write_csv_number(out, probe.physical_stop_radius);
        out << "," << (probe.abcd_seed_valid ? 1 : 0)
            << "," << (probe.abcd_singular ? 1 : 0)
            << "," << (probe.abcd_seed_oob ? 1 : 0) << ",";
        write_csv_number(out, probe.abcd_seed_x);
        out << ",";
        write_csv_number(out, probe.abcd_seed_y);
        out << "," << (probe.full_anchor_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.full_seed_x);
        out << ",";
        write_csv_number(out, probe.full_seed_y);
        out << ",";
        if (probe.abcd_seed_valid && probe.full_anchor_valid) {
            write_csv_number(out, probe.abcd_seed_x - probe.full_seed_x);
        }
        out << ",";
        if (probe.abcd_seed_valid && probe.full_anchor_valid) {
            write_csv_number(out, probe.abcd_seed_y - probe.full_seed_y);
        }
        out << ",";
        if (probe.abcd_seed_valid && probe.full_anchor_valid) {
            write_csv_number(
                out,
                std::hypot(probe.abcd_seed_x - probe.full_seed_x,
                           probe.abcd_seed_y - probe.full_seed_y));
        }
        out << ",";
        write_csv_number(out, probe.full_stop_x);
        out << ",";
        write_csv_number(out, probe.full_stop_y);
        out << ",";
        write_csv_number(out, probe.full_stop_radial);
        out << ",";
        write_csv_number(out, probe.full_stop_residual);
        out << ",";
    };

    for (const auto& probe : probes) {
        if (probe.budget_steps.empty()) {
            write_probe_prefix(probe);
            out << "none,0,0,-1,0,0,0,,,,,,,,,,,,,unknown,-1,0,unknown,-1,0\n";
            continue;
        }
        for (const auto& step : probe.budget_steps) {
            write_probe_prefix(probe);
            out << step.strategy << ",";
            write_csv_number(out, step.inward_factor);
            out << ",";
            write_csv_number(out, step.center_meridional_radius);
            out << "," << step.iterations << "," << (step.trace_to_stop ? 1 : 0)
                << "," << (step.aim_converged ? 1 : 0)
                << "," << (step.first_surface_seed_valid ? 1 : 0) << ",";
            write_csv_number(out, step.first_surface_x);
            out << ",";
            write_csv_number(out, step.first_surface_y);
            out << ",";
            if (step.first_surface_seed_valid && probe.full_anchor_valid) {
                write_csv_number(out, step.first_surface_x - probe.full_seed_x);
            }
            out << ",";
            if (step.first_surface_seed_valid && probe.full_anchor_valid) {
                write_csv_number(out, step.first_surface_y - probe.full_seed_y);
            }
            out << ",";
            if (step.first_surface_seed_valid && probe.full_anchor_valid) {
                write_csv_number(
                    out,
                    std::hypot(step.first_surface_x - probe.full_seed_x,
                               step.first_surface_y - probe.full_seed_y));
            }
            out << ",";
            write_csv_number(out, step.stop_x);
            out << ",";
            write_csv_number(out, step.stop_y);
            out << ",";
            write_csv_number(out, step.stop_radial);
            out << ",";
            write_csv_number(out, step.stop_residual);
            out << ",";
            if (step.trace_to_stop && probe.full_anchor_valid) {
                write_csv_number(out, step.stop_x - probe.full_stop_x);
            }
            out << ",";
            if (step.trace_to_stop && probe.full_anchor_valid) {
                write_csv_number(out, step.stop_y - probe.full_stop_y);
            }
            out << ",";
            if (step.trace_to_stop && probe.full_anchor_valid) {
                write_csv_number(
                    out,
                    std::hypot(step.stop_x - probe.full_stop_x,
                               step.stop_y - probe.full_stop_y));
            }
            out << ",";
            write_csv_number(out, step.aim_residual);
            out << "," << step.last_valid_surface
                << "," << ray_failure_code_csv_label(step.failure_code)
                << "," << step.failure_surface << ",";
            write_csv_number(out, step.failure_severity);
            out << "," << ray_failure_code_csv_label(step.trace_failure_code)
                << "," << step.trace_failure_surface << ",";
            write_csv_number(out, step.trace_failure_severity);
            out << "\n";
        }
    }
}

void save_three_step_chief_probe_csv(
    const LensSystem& lens,
    const std::string& path) {
    AimingSession session(lens);
    const auto probes = session.probe_three_step_chief_aiming();
    std::ofstream out(path);
    out << "field_index,hx,hy,wave_index,wavelength_um,field_radius,target_angle_deg,"
        << "trial_rays,max_reverse_angle_deg,reverse_sample_count,min_sample_angle_deg,"
        << "max_sample_angle_deg,bracketed,clamped_low,clamped_high,lo_angle_deg,"
        << "hi_angle_deg,interpolation_t,predicted_valid,predicted_x,predicted_y,"
        << "predicted_trace_to_stop,predicted_stop_residual,"
        << "predicted_stop_direction_angle_deg,predicted_failure_code,"
        << "predicted_failure_surface,predicted_failure_severity,actual_valid,"
        << "actual_x,actual_y,actual_stop_residual,actual_stop_direction_angle_deg,"
        << "reverse_actual_stop_angle_valid,"
        << "reverse_actual_stop_angle_object_angle_deg,"
        << "reverse_actual_stop_angle_x,reverse_actual_stop_angle_y,"
        << "reverse_actual_stop_angle_delta_r,"
        << "reverse_floor_stop_angle_deg,reverse_floor_object_angle_deg,"
        << "reverse_ceil_stop_angle_deg,reverse_ceil_object_angle_deg,"
        << "reverse_minus_0p1_object_angle_deg,"
        << "reverse_plus_0p1_object_angle_deg,"
        << "model_valid,model_fit_residual,"
        << "meridional_x,meridional_y,sagittal_x,sagittal_y,meridional_radius,"
        << "sagittal_radius,delta_x,delta_y,delta_r\n";
    for (const auto& probe : probes) {
        out << probe.field_index << "," << std::setprecision(12)
            << probe.hx << "," << probe.hy << ","
            << probe.wave_index << ",";
        write_csv_number(out, probe.wavelength_um);
        out << ",";
        write_csv_number(out, probe.field_radius);
        out << ",";
        write_csv_number(out, probe.target_angle_deg);
        out << "," << probe.trial_rays << ",";
        write_csv_number(out, probe.max_reverse_angle_deg);
        out << "," << probe.reverse_sample_count << ",";
        write_csv_number(out, probe.min_sample_angle_deg);
        out << ",";
        write_csv_number(out, probe.max_sample_angle_deg);
        out << "," << (probe.bracketed ? 1 : 0)
            << "," << (probe.clamped_low ? 1 : 0)
            << "," << (probe.clamped_high ? 1 : 0)
            << ",";
        write_csv_number(out, probe.lo_angle_deg);
        out << ",";
        write_csv_number(out, probe.hi_angle_deg);
        out << ",";
        write_csv_number(out, probe.interpolation_t);
        out << "," << (probe.predicted_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.predicted_x);
        out << ",";
        write_csv_number(out, probe.predicted_y);
        out << "," << (probe.predicted_trace_to_stop ? 1 : 0) << ",";
        write_csv_number(out, probe.predicted_stop_residual);
        out << ",";
        write_csv_number(out, probe.predicted_stop_direction_angle_deg);
        out << "," << ray_failure_code_csv_label(probe.predicted_failure_code)
            << "," << probe.predicted_failure_surface << ",";
        write_csv_number(out, probe.predicted_failure_severity);
        out << "," << (probe.actual_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.actual_x);
        out << ",";
        write_csv_number(out, probe.actual_y);
        out << ",";
        write_csv_number(out, probe.actual_stop_residual);
        out << ",";
        write_csv_number(out, probe.actual_stop_direction_angle_deg);
        out << ",";
        out << (probe.reverse_actual_stop_angle_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.reverse_actual_stop_angle_object_angle_deg);
        out << ",";
        write_csv_number(out, probe.reverse_actual_stop_angle_x);
        out << ",";
        write_csv_number(out, probe.reverse_actual_stop_angle_y);
        out << ",";
        write_csv_number(out, probe.reverse_actual_stop_angle_delta_r);
        out << ",";
        write_csv_number(out, probe.reverse_floor_stop_angle_deg);
        out << ",";
        write_csv_number(out, probe.reverse_floor_object_angle_deg);
        out << ",";
        write_csv_number(out, probe.reverse_ceil_stop_angle_deg);
        out << ",";
        write_csv_number(out, probe.reverse_ceil_object_angle_deg);
        out << ",";
        write_csv_number(out, probe.reverse_minus_0p1_object_angle_deg);
        out << ",";
        write_csv_number(out, probe.reverse_plus_0p1_object_angle_deg);
        out << ",";
        out << (probe.model_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.model_fit_residual);
        out << ",";
        write_csv_number(out, probe.meridional_x);
        out << ",";
        write_csv_number(out, probe.meridional_y);
        out << ",";
        write_csv_number(out, probe.sagittal_x);
        out << ",";
        write_csv_number(out, probe.sagittal_y);
        out << ",";
        write_csv_number(out, probe.meridional_radius);
        out << ",";
        write_csv_number(out, probe.sagittal_radius);
        out << ",";
        write_csv_number(out, probe.delta_x);
        out << ",";
        write_csv_number(out, probe.delta_y);
        out << ",";
        write_csv_number(out, probe.delta_r);
        out << "\n";
    }
}

void save_calibrated_edge_initial_probe_csv(
    const LensSystem& lens,
    const std::string& path) {
    AimingSession session(lens);
    const auto probes = session.probe_calibrated_edge_initials();
    std::ofstream out(path);
    out << "field_index,hx,hy,is_center_field,wave_index,wavelength_um,"
        << "edge_index,edge_label,px,py,stop_target_radius,target_stop_x,target_stop_y,"
        << "entrance_pupil_radius,near_pupil_edge_radius,model_valid,"
        << "center_footprint_valid,chief_seed_valid,chief_x,chief_y,"
        << "truth_valid,truth_x,truth_y,truth_stop_residual,"
        << "calibrated_anchor_valid,calibrated_anchor_x,calibrated_anchor_y,"
        << "calibrated_anchor_to_truth_dx,calibrated_anchor_to_truth_dy,"
        << "calibrated_anchor_to_truth_dr,"
        << "near_seed_valid,near_seed_x,near_seed_y,near_seed_to_truth_dx,"
        << "near_seed_to_truth_dy,near_seed_to_truth_dr,near_seed_stop_residual,"
        << "center_seed_valid,center_vector_x,center_vector_y,center_vector_radius,"
        << "center_seed_x,center_seed_y,center_seed_to_truth_dx,"
        << "center_seed_to_truth_dy,center_seed_to_truth_dr,center_seed_stop_residual\n";

    auto edge_label = [](double px, double py) {
        if (px > 0.5) {
            return "x_plus";
        }
        if (px < -0.5) {
            return "x_minus";
        }
        if (py > 0.5) {
            return "y_plus";
        }
        if (py < -0.5) {
            return "y_minus";
        }
        return "unknown";
    };

    for (const auto& probe : probes) {
        out << probe.field_index << "," << std::setprecision(12)
            << probe.hx << "," << probe.hy << ","
            << (probe.is_center_field ? 1 : 0) << ","
            << probe.wave_index << ",";
        write_csv_number(out, probe.wavelength_um);
        out << "," << probe.edge_index << "," << edge_label(probe.px, probe.py) << ",";
        write_csv_number(out, probe.px);
        out << ",";
        write_csv_number(out, probe.py);
        out << ",";
        write_csv_number(out, probe.stop_target_radius);
        out << ",";
        write_csv_number(out, probe.target_stop_x);
        out << ",";
        write_csv_number(out, probe.target_stop_y);
        out << ",";
        write_csv_number(out, probe.entrance_pupil_radius);
        out << ",";
        write_csv_number(out, probe.near_pupil_edge_radius);
        out << "," << (probe.model_valid ? 1 : 0)
            << "," << (probe.center_footprint_valid ? 1 : 0)
            << "," << (probe.chief_seed_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.chief_x);
        out << ",";
        write_csv_number(out, probe.chief_y);
        out << "," << (probe.truth_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.truth_x);
        out << ",";
        write_csv_number(out, probe.truth_y);
        out << ",";
        write_csv_number(out, probe.truth_stop_residual);
        out << "," << (probe.calibrated_anchor_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.calibrated_anchor_x);
        out << ",";
        write_csv_number(out, probe.calibrated_anchor_y);
        out << ",";
        write_csv_number(out, probe.calibrated_anchor_to_truth_dx);
        out << ",";
        write_csv_number(out, probe.calibrated_anchor_to_truth_dy);
        out << ",";
        write_csv_number(out, probe.calibrated_anchor_to_truth_dr);
        out << "," << (probe.near_seed_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.near_seed_x);
        out << ",";
        write_csv_number(out, probe.near_seed_y);
        out << ",";
        write_csv_number(out, probe.near_seed_to_truth_dx);
        out << ",";
        write_csv_number(out, probe.near_seed_to_truth_dy);
        out << ",";
        write_csv_number(out, probe.near_seed_to_truth_dr);
        out << ",";
        write_csv_number(out, probe.near_seed_stop_residual);
        out << "," << (probe.center_seed_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.center_vector_x);
        out << ",";
        write_csv_number(out, probe.center_vector_y);
        out << ",";
        write_csv_number(out, probe.center_vector_radius);
        out << ",";
        write_csv_number(out, probe.center_seed_x);
        out << ",";
        write_csv_number(out, probe.center_seed_y);
        out << ",";
        write_csv_number(out, probe.center_seed_to_truth_dx);
        out << ",";
        write_csv_number(out, probe.center_seed_to_truth_dy);
        out << ",";
        write_csv_number(out, probe.center_seed_to_truth_dr);
        out << ",";
        write_csv_number(out, probe.center_seed_stop_residual);
        out << "\n";
    }
}

void save_center_edge_iterative_probe_csv(
    const LensSystem& lens,
    const std::string& path) {
    AimingSession session(lens);
    const auto probes = session.probe_center_edge_iterative_steps();
    std::ofstream out(path);
    out << "field_index,wave_index,edge_index,hx,hy,wavelength_um,"
        << "edge_label,px,py,iteration,target_stop_x,target_stop_y,"
        << "seed_x,seed_y,guess_valid,trace_ok,stop_valid,stop_x,stop_y,residual,"
        << "accepted,terminated,failure_code,failure_surface,failure_severity,"
        << "trace_failure_reason,trace_failure_surface,reason\n";

    auto edge_label = [](double px, double py) {
        if (px > 0.5) {
            return "x_plus";
        }
        if (px < -0.5) {
            return "x_minus";
        }
        if (py > 0.5) {
            return "y_plus";
        }
        if (py < -0.5) {
            return "y_minus";
        }
        return "unknown";
    };

    for (const auto& probe : probes) {
        out << probe.field_index << "," << probe.wave_index << "," << probe.edge_index << ","
            << std::setprecision(12) << probe.hx << "," << probe.hy << ",";
        write_csv_number(out, probe.wavelength_um);
        out << "," << edge_label(probe.px, probe.py) << ",";
        write_csv_number(out, probe.px);
        out << ",";
        write_csv_number(out, probe.py);
        out << "," << probe.iteration << ",";
        write_csv_number(out, probe.target_stop_x);
        out << ",";
        write_csv_number(out, probe.target_stop_y);
        out << ",";
        write_csv_number(out, probe.seed_x);
        out << ",";
        write_csv_number(out, probe.seed_y);
        out << "," << (probe.guess_valid ? 1 : 0)
            << "," << (probe.trace_ok ? 1 : 0)
            << "," << (probe.stop_valid ? 1 : 0) << ",";
        write_csv_number(out, probe.stop_x);
        out << ",";
        write_csv_number(out, probe.stop_y);
        out << ",";
        write_csv_number(out, probe.residual);
        out << "," << (probe.accepted ? 1 : 0)
            << "," << (probe.terminated ? 1 : 0)
            << "," << ray_failure_code_csv_label(probe.failure_code)
            << "," << probe.failure_surface << ",";
        write_csv_number(out, probe.failure_severity);
        out << "," << probe.trace_failure_reason
            << "," << probe.trace_failure_surface
            << "," << probe.reason << "\n";
    }
}

} } }
