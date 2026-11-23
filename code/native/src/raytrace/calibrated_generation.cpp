// Generate a physical ray from the session calibrated-paraxial model and pupil mapping.
// Session mode dispatch calls this after model preparation; recovery still uses the shared caches.
// Keep edge interpolation, strict STOP checks, and fallback selection consistent with model validity.

#include "diagnostics_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

Ray AimingSession::generate_calibrated_paraxial(
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index,
    const Field& field,
    const Wavelength& wave,
    const PupilPoint& pupil) {
    auto& diag = diagnostics_state();
    add_diag(diag.calibrated_generate_calls);
    ScopedDiagTimer generate_timer(diag.calibrated_generate_ns);
    const auto& cfg = system_.ray_aiming.calibrated_paraxial;
    enum class FallbackReason {
        ModelInvalid,
        SeedOutOfBounds,
        RayInvalid,
    };
    auto fallback_ray = [&](FallbackReason reason) {
        add_diag(diag.calibrated_fallback_calls);
        if (reason == FallbackReason::ModelInvalid) {
            add_diag(diag.calibrated_fallback_model_invalid_calls);
        } else if (reason == FallbackReason::SeedOutOfBounds) {
            add_diag(diag.calibrated_fallback_seed_oob_calls);
        } else {
            add_diag(diag.calibrated_fallback_ray_invalid_calls);
        }
        const std::string& fallback = cfg.fallback;
        if (fallback == "none" || fallback == "off" || fallback == "disabled" ||
            fallback == "fail" || fallback == "failure") {
            add_diag(diag.calibrated_fallback_none_calls);
            Ray failed;
            failed.wavelength_um = wave.value_um;
            failed.nominal_weight = std::max(0.0, pupil.weight);
            failed.intensity = failed.nominal_weight;
            failed.valid = false;
            failed.aim_converged = false;
            failed.aim_residual = std::numeric_limits<double>::infinity();
            failed.failure_code = RayFailureCode::AimNotConverged;
            failed.failure_severity = 1.0;
            return failed;
        }
        add_diag(diag.calibrated_fallback_other_calls);
        if (fallback == "adaptive_pilot_grid" || fallback == "adaptive" ||
            fallback == "pilot_grid" || fallback == "adaptive_pilot") {
            return generate_adaptive_pilot(field_index, wave_index, pupil_index, field, wave, pupil);
        }
        if (fallback == "iterative") {
            return tracer_.generate_ray_with_mode(field, pupil, wave.value_um, RayAimingMode::Iterative);
        }
        if (fallback == "robust") {
            return tracer_.generate_ray_with_mode(field, pupil, wave.value_um, RayAimingMode::Robust);
        }
        if (fallback == "direct") {
            return tracer_.generate_ray_with_mode(field, pupil, wave.value_um, RayAimingMode::Direct);
        }
        if (fallback == "paraxial") {
            return tracer_.generate_ray_with_mode(field, pupil, wave.value_um, RayAimingMode::Paraxial);
        }
        Ray failed;
        failed.valid = false;
        failed.aim_converged = false;
        failed.aim_residual = std::numeric_limits<double>::infinity();
        failed.failure_code = RayFailureCode::AimNotConverged;
        failed.failure_severity = 1.0;
        return failed;
    };

    CalibratedParaxialModel& model =
        ensure_calibrated_paraxial_model(field_index, wave_index, field, wave);
    const bool domain_center_sample =
        std::abs(pupil.px) <= 1.0e-14 && std::abs(pupil.py) <= 1.0e-14;
    if (!model.chief_seed.valid || (!model.valid && !domain_center_sample)) {
        return fallback_ray(FallbackReason::ModelInvalid);
    }

    const double rs =
        tracer_.stop_target_radius(wave.value_um, RayAimingMode::CalibratedParaxial);
    const double pupil_qx = field.fvcx + pupil.px * (1.0 - field.fvdx);
    const double pupil_qy = field.fvcy + pupil.py * (1.0 - field.fvdy);
    const double target_x = pupil_qx * rs;
    const double target_y = pupil_qy * rs;
    const std::string mapping_mode =
        normalized_mapping_mode(cfg.sampling_mode, "central_fit");
    FirstSurfaceSeed seed;
    if (mapping_mode == "chief_edge_interpolation") {
        auto edge_axis_anchor = [&](bool x_axis, bool positive) {
            const CalibratedParaxialModel::EdgeAnchor* plus = nullptr;
            const CalibratedParaxialModel::EdgeAnchor* minus = nullptr;
            for (const auto& anchor : model.edge_anchors) {
                if (!anchor.valid || !anchor.seed.valid) {
                    continue;
                }
                if (x_axis && anchor.px > 0.5) {
                    plus = &anchor;
                } else if (x_axis && anchor.px < -0.5) {
                    minus = &anchor;
                } else if (!x_axis && anchor.py > 0.5) {
                    plus = &anchor;
                } else if (!x_axis && anchor.py < -0.5) {
                    minus = &anchor;
                }
            }
            return positive ? plus : minus;
        };
        seed = model.chief_seed;
        auto apply_axis = [&](bool x_axis, double coord) {
            if (std::abs(coord) <= 1.0e-14) {
                return true;
            }
            const bool positive = coord > 0.0;
            const auto* anchor = edge_axis_anchor(x_axis, positive);
                if (!anchor) {
                    return false;
                }
                const double anchor_coord =
                    std::abs(x_axis ? anchor->px : anchor->py);
                if (!std::isfinite(anchor_coord) || anchor_coord <= 1.0e-12 ||
                    std::abs(coord) > anchor_coord * (1.0 + 1.0e-10)) {
                    return false;
                }
                const double weight = std::abs(coord) / anchor_coord;
                seed.x += weight * (anchor->seed.x - model.chief_seed.x);
                seed.y += weight * (anchor->seed.y - model.chief_seed.y);
                return true;
        };
        if (!apply_axis(true, pupil.px) || !apply_axis(false, pupil.py)) {
            return fallback_ray(FallbackReason::ModelInvalid);
        }
    } else {
        const double dx = target_x - model.chief_stop_x;
        const double dy = target_y - model.chief_stop_y;
        seed = {
            model.chief_seed.x + model.b11 * dx + model.b12 * dy,
            model.chief_seed.y + model.b21 * dx + model.b22 * dy,
            true};
        if (cfg.edge_calibration_enabled) {
            const double power = std::max(0.25, cfg.edge_calibration_weight_power);
            bool correction_domain_ok = true;
            for (const auto& anchor : model.edge_anchors) {
                if (!anchor.valid) {
                    continue;
                }
                double weight = 0.0;
                double anchor_coord = 0.0;
                if (anchor.px > 0.0) {
                    weight = std::max(0.0, pupil.px);
                    anchor_coord = anchor.px;
                } else if (anchor.px < 0.0) {
                    weight = std::max(0.0, -pupil.px);
                    anchor_coord = -anchor.px;
                } else if (anchor.py > 0.0) {
                    weight = std::max(0.0, pupil.py);
                    anchor_coord = anchor.py;
                } else if (anchor.py < 0.0) {
                    weight = std::max(0.0, -pupil.py);
                    anchor_coord = -anchor.py;
                }
                if (weight <= 0.0) {
                    continue;
                }
                if (!std::isfinite(anchor_coord) || anchor_coord <= 1.0e-12 ||
                    weight > anchor_coord * (1.0 + 1.0e-10)) {
                    correction_domain_ok = false;
                    break;
                }
                weight = std::pow(weight / anchor_coord, power);
                seed.x += weight * anchor.correction_x;
                seed.y += weight * anchor.correction_y;
            }
            if (!correction_domain_ok) {
                return fallback_ray(FallbackReason::ModelInvalid);
            }
        }
    }
    const double hard_radius = first_surface_hard_radius(wave.value_um);
    if (!std::isfinite(seed.x) || !std::isfinite(seed.y) ||
        (hard_radius > 0.0 &&
         aperture_clip_exceeded(std::hypot(seed.x, seed.y), hard_radius))) {
        return fallback_ray(FallbackReason::SeedOutOfBounds);
    }

    Ray ray = tracer_.generate_ray_through_first_surface_point(
        field,
        wave.value_um,
        seed.x,
        seed.y,
        pupil.weight);
    if (!ray.valid) {
        return fallback_ray(FallbackReason::RayInvalid);
    }
    if (cfg.correction_iterations > 0) {
        add_diag(diag.calibrated_correction_calls);
        const bool strict = system_.ray_aiming.require_convergence;
        Ray corrected = tracer_.solve_stop_aim_from_initial(
            field,
            pupil,
            wave.value_um,
            ray,
            strict,
            cfg.correction_iterations);
        if (corrected.valid) {
            return corrected;
        }
        const std::string& fallback = cfg.fallback;
        const bool fallback_disabled =
            fallback == "none" || fallback == "off" || fallback == "disabled" ||
            fallback == "fail" || fallback == "failure";
        if (!fallback_disabled) {
            return fallback_ray(FallbackReason::RayInvalid);
        }
        return corrected;
    }
    ray.aim_converged = true;
    ray.aim_residual = model.fit_rms;
    ray.aim_target_radius = rs;
    ray.homotopy_fraction = 1.0;
    ray.aim_iterations = 0;
    return ray;
}

} // namespace optiland
