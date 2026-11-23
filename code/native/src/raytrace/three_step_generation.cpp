// Generate three-step rays and the secondary image-guard rays used by session recovery.
// The generator consumes the prepared reverse map and fitted three-step model.
// Keep model validity, predicted guard checks, and the order of recovery candidates unchanged.

#include "diagnostics_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "spectral_aperture_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

Ray AimingSession::generate_three_step(
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index,
    const Field& field,
    const Wavelength& wave,
    const PupilPoint& pupil) {
    (void)pupil_index;
    const auto& cfg = system_.ray_aiming.three_step;
    auto& diag = diagnostics_state();
    add_diag(diag.three_step_generate_calls);
    ScopedDiagTimer generate_timer(diag.three_step_generate_ns);
    auto failed_ray = [&]() {
        return make_failed_ray(wave, pupil);
    };
    auto fallback_ray = [&]() {
        add_diag(diag.three_step_fallback_calls);
        const std::string& fallback = cfg.fallback;
        if (fallback == "none" || fallback == "off" || fallback == "disabled" ||
            fallback == "fail" || fallback == "failure") {
            add_diag(diag.three_step_fallback_none_calls);
            return failed_ray();
        }
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
        return failed_ray();
    };

    ThreeStepModel& model = ensure_three_step_model(field_index, wave_index, field, wave);
    if (!model.valid || !model.center.valid) {
        return fallback_ray();
    }

    const double field_radius = std::hypot(field.hx, field.hy);
    double mer_x = 0.0;
    double mer_y = 1.0;
    if (field_radius > 1.0e-12) {
        mer_x = field.hx / field_radius;
        mer_y = field.hy / field_radius;
    }
    const double sag_x = -mer_y;
    const double sag_y = mer_x;
    const double pupil_mer = pupil.px * mer_x + pupil.py * mer_y;
    const double pupil_sag = pupil.px * sag_x + pupil.py * sag_y;
    const double rs = tracer_.stop_target_radius(wave.value_um, RayAimingMode::ThreeStep);
    const double target_x = (field.fvcx + pupil.px * (1.0 - field.fvdx)) * rs;
    const double target_y = (field.fvcy + pupil.py * (1.0 - field.fvdy)) * rs;
    const std::string mapping_mode =
        normalized_mapping_mode(cfg.sampling_mode, "chief_edge_interpolation");
    FirstSurfaceSeed seed;
    if (mapping_mode == "central_fit") {
        if (!model.central_fit_valid) {
            return fallback_ray();
        }
        const double dx = target_x - model.center_stop_x;
        const double dy = target_y - model.center_stop_y;
        seed = {
            model.center.x + model.b11 * dx + model.b12 * dy,
            model.center.y + model.b21 * dx + model.b22 * dy,
            true};
        const double power =
            std::max(0.25, system_.ray_aiming.calibrated_paraxial.edge_calibration_weight_power);
        const std::array<double, 4> weights{{
            std::max(0.0, pupil_mer),
            std::max(0.0, -pupil_mer),
            std::max(0.0, pupil_sag),
            std::max(0.0, -pupil_sag),
        }};
        for (std::size_t i = 0; i < model.edge_corrections.size(); ++i) {
            const auto& correction = model.edge_corrections[i];
            if (!correction.valid || weights[i] <= 0.0) {
                continue;
            }
            const double weight = std::pow(std::min(1.0, weights[i]), power);
            seed.x += weight * correction.correction_x;
            seed.y += weight * correction.correction_y;
        }
    } else {
        seed = model.center;
        auto apply_axis = [&](double coord,
                              const FirstSurfaceSeed& positive_seed,
                              const FirstSurfaceSeed& negative_seed,
                              double fallback_x,
                              double fallback_y) {
            if (std::abs(coord) <= 1.0e-14) {
                return;
            }
            if (coord > 0.0 && positive_seed.valid) {
                seed.x += coord * (positive_seed.x - model.center.x);
                seed.y += coord * (positive_seed.y - model.center.y);
            } else if (coord < 0.0 && negative_seed.valid) {
                const double weight = -coord;
                seed.x += weight * (negative_seed.x - model.center.x);
                seed.y += weight * (negative_seed.y - model.center.y);
            } else {
                seed.x += coord * fallback_x;
                seed.y += coord * fallback_y;
            }
        };
        apply_axis(
            pupil_mer,
            model.meridional_plus,
            model.meridional_minus,
            model.meridional_x,
            model.meridional_y);
        apply_axis(
            pupil_sag,
            model.sagittal_plus,
            model.sagittal_minus,
            model.sagittal_x,
            model.sagittal_y);
    }
    const double hard_radius = first_surface_hard_radius(wave.value_um);
    if (!std::isfinite(seed.x) || !std::isfinite(seed.y) ||
        (hard_radius > 0.0 &&
         aperture_clip_exceeded(std::hypot(seed.x, seed.y), hard_radius))) {
        return fallback_ray();
    }

    Ray ray = tracer_.generate_ray_through_first_surface_point(
        field,
        wave.value_um,
        seed.x,
        seed.y,
        pupil.weight);
    if (!ray.valid) {
        return fallback_ray();
    }
    if (cfg.correction_iterations > 0) {
        add_diag(diag.three_step_generate_correction_calls);
        Ray corrected;
        {
            ScopedDiagTimer correction_timer(diag.three_step_generate_correction_ns);
            corrected = tracer_.solve_stop_aim_from_initial(
                field,
                pupil,
                wave.value_um,
                ray,
                system_.ray_aiming.require_convergence,
                cfg.correction_iterations);
        }
        add_diag(diag.three_step_generate_correction_iterations,
                 std::max(0, corrected.aim_iterations));
        if (corrected.valid ||
            cfg.fallback == "none" || cfg.fallback == "off" ||
            cfg.fallback == "disabled" || cfg.fallback == "fail" ||
            cfg.fallback == "failure") {
            return corrected;
        }
        return fallback_ray();
    }

    double residual =
        mapping_mode == "central_fit" && std::isfinite(model.central_fit_rms)
            ? model.central_fit_rms
            : model.fit_residual;
    if (cfg.sample_stop_residual_debug) {
        double stop_x = 0.0;
        double stop_y = 0.0;
        add_diag(diag.three_step_generate_stop_probe_calls);
        bool stop_ok = false;
        {
            ScopedDiagTimer stop_timer(diag.three_step_generate_stop_probe_ns);
            stop_ok = sample_stop_from_ray(ray, &stop_x, &stop_y);
        }
        if (stop_ok) {
            add_diag(diag.three_step_generate_stop_probe_successes);
            residual = std::hypot(stop_x - target_x, stop_y - target_y);
        }
    }
    ray.aim_residual = std::isfinite(residual) ? residual : std::numeric_limits<double>::infinity();
    ray.aim_target_radius = rs;
    ray.aim_iterations = 0;
    ray.homotopy_fraction = 1.0;
    ray.aim_converged = true;
    return ray;
}

PupilPoint AimingSession::predict_secondary_image_guard(
    std::size_t field_index, std::size_t wave_index,
    const Field& field, const Wavelength& wave, const PupilPoint& pupil) {
    if (!system_.defer_secondary_aperture_clipping(wave.value_um) ||
        system_.ray_aiming.mode != RayAimingMode::CalibratedParaxial ||
        normalized_mapping_mode(system_.ray_aiming.calibrated_paraxial.sampling_mode,
                                "central_fit") != "chief_edge_interpolation" ||
        system_.ray_aiming.calibrated_paraxial.correction_iterations != 0) return pupil;
    const std::size_t first = system_.first_optical_surface();
    if (first >= system_.surfaces.size() ||
        first != system_.first_ray_aiming_surface(wave.value_um)) return pupil;
    auto& model = ensure_calibrated_paraxial_model(field_index, wave_index, field, wave);
    if (!model.valid || !model.chief_seed.valid) return pupil;
    double vx = 0.0, vy = 0.0;
    double stop_vx = 0.0, stop_vy = 0.0;
    bool have_stop_samples = true;
    for (int axis = 0; axis < 2; ++axis) {
        const double coord = axis == 0 ? pupil.px : pupil.py;
        if (std::abs(coord) <= 1.0e-14) continue;
        bool found = false;
        for (const auto& anchor : model.edge_anchors) {
            const double ac = axis == 0 ? anchor.px : anchor.py;
            if (!anchor.valid || !anchor.seed.valid || ac * coord <= 0.0 ||
                std::abs(ac) <= 0.5 || std::abs(coord) > std::abs(ac) * (1.0 + 1.0e-10)) continue;
            const double w = coord / ac;
            vx += w * (anchor.seed.x - model.chief_seed.x);
            vy += w * (anchor.seed.y - model.chief_seed.y);
            if (std::isfinite(anchor.stop_x) && std::isfinite(anchor.stop_y)) {
                stop_vx += w * (anchor.stop_x - model.chief_stop_x);
                stop_vy += w * (anchor.stop_y - model.chief_stop_y);
            } else {
                have_stop_samples = false;
            }
            found = true;
            break;
        }
        if (!found) return pupil;
    }
    double scale = spectral_edge_scale(model.chief_seed.x, model.chief_seed.y,
        vx, vy, spectral_surface_radius(system_.surfaces[first]));
    // The translated elliptical pupil can also extend beyond the circular
    // STOP (notably its sagittal edges when FVCY != 0). Its calibrated target
    // coordinates are already known: predict this intersection as well, rather
    // than discard the image guard after its one allowed trace.
    const std::size_t stop = tracer_.stop_surface_index();
    if (stop < system_.surfaces.size()) {
        const double rs = tracer_.stop_target_radius(
            wave.value_um, RayAimingMode::CalibratedParaxial);
        const double stop_scale = spectral_edge_scale(
            have_stop_samples ? model.chief_stop_x : field.fvcx * rs,
            have_stop_samples ? model.chief_stop_y : field.fvcy * rs,
            have_stop_samples ? stop_vx : pupil.px * (1.0 - field.fvdx) * rs,
            have_stop_samples ? stop_vy : pupil.py * (1.0 - field.fvdy) * rs,
            spectral_surface_radius(system_.surfaces[stop]));
        // Edge calibration accepts a 1% stop residual. Keep the predicted
        // replacement inside by that same tolerance so interpolation error
        // cannot immediately mask the one allowed guard trace.
        const bool diagonal = std::abs(pupil.px) > 1.0e-14 &&
                              std::abs(pupil.py) > 1.0e-14;
        const double stop_reserve = diagonal ? 0.96 : 0.99;
        const bool stop_guard_active = stop_scale < 1.0 || (diagonal && scale < 1.0);
        scale = std::min(scale,
            stop_guard_active ? stop_reserve * stop_scale : 1.0);
    }
    return {pupil.px * scale, pupil.py * scale, pupil.weight};
}

Ray AimingSession::generate_image_guard(
    std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
    const Field& field, const Wavelength& wave, const PupilPoint& pupil) {
    const auto predicted = predict_secondary_image_guard(field_index, wave_index, field, wave, pupil);
    Ray ray = generate(field_index, wave_index, pupil_index, field, wave, predicted);
    const double radius = std::hypot(pupil.px, pupil.py);
    ray.edge_prediction_scale = radius > 0.0 ? std::hypot(predicted.px, predicted.py) / radius : 1.0;
    return ray;
}

} // namespace optiland
