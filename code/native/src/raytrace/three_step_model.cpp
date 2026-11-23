// Build the per-field three-step aiming model from central and edge solve phases.
// Session generation consumes this cached model together with its reverse mapping.
// Preserve fit inputs, edge acceptance, and cache commit timing across the complete model build.

#include "diagnostics_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "detail.hpp"
#include "calibration/three_step_solve_seed_space_edge.hpp"
#include "calibration/three_step_build_central_fit.hpp"
#include "calibration/three_step_try_scaled_edge_anchor.hpp"
#include "calibration/three_step_solve_edge.hpp"

namespace optiland {
using namespace raytrace_detail;

AimingSession::ThreeStepModel& AimingSession::ensure_three_step_model(
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave) {
    const FieldWaveKey key{field_index, wave_index};
    auto& diag = diagnostics_state();
    add_diag(diag.three_step_model_requests);
    ThreeStepModel& model = three_step_models_[key];
    if (model.built) {
        add_diag(diag.three_step_model_cache_hits);
        return model;
    }
    model.built = true;
    add_diag(diag.three_step_model_builds);
    ScopedDiagTimer model_timer(diag.three_step_model_build_ns);
    auto finish_model = [&]() -> ThreeStepModel& {
        add_diag(model.valid ? diag.three_step_model_valid : diag.three_step_model_invalid);
        return model;
    };
    const auto& cfg = system_.ray_aiming.three_step;
    if (!cfg.enabled) {
        return finish_model();
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
    const double target_angle_deg =
        std::max(0.0, system_.max_field_deg) * field_radius;

    const ThreeStepReverseMap& reverse_map = ensure_three_step_reverse_map(wave_index, wave);
    const auto& unique_samples = reverse_map.samples;
    auto seed_from_reverse_radius = [&](double radius) {
        return FirstSurfaceSeed{radius * mer_x, radius * mer_y, true};
    };

    auto set_center_from_samples = [&]() {
        if (unique_samples.empty()) {
            return false;
        }
        if (unique_samples.size() == 1 ||
            target_angle_deg <= unique_samples.front().first) {
            model.center = seed_from_reverse_radius(unique_samples.front().second);
            return true;
        }
        if (target_angle_deg >= unique_samples.back().first) {
            model.center = seed_from_reverse_radius(unique_samples.back().second);
            return true;
        }
        for (std::size_t i = 1; i < unique_samples.size(); ++i) {
            const auto& lo = unique_samples[i - 1];
            const auto& hi = unique_samples[i];
            if (target_angle_deg > hi.first) {
                continue;
            }
            const double span = std::max(1.0e-12, hi.first - lo.first);
            const double t = std::clamp((target_angle_deg - lo.first) / span, 0.0, 1.0);
            const double radius = lo.second + t * (hi.second - lo.second);
            model.center = {
                radius * mer_x,
                radius * mer_y,
                true};
            return true;
        }
        return false;
    };

    if (set_center_from_samples()) {
        add_diag(diag.three_step_center_from_reverse);
    } else {
        const bool fallback_disabled =
            cfg.fallback == "none" || cfg.fallback == "off" ||
            cfg.fallback == "disabled" || cfg.fallback == "fail" ||
            cfg.fallback == "failure";
        if (fallback_disabled) {
            model.valid = false;
            model.fit_residual = std::numeric_limits<double>::infinity();
            return finish_model();
        }
        add_diag(diag.three_step_center_fallback_calls);
        const PupilPoint chief{0.0, 0.0, 1.0};
        Ray chief_ray = tracer_.generate_ray_with_mode(
            field,
            chief,
            wave.value_um,
            RayAimingMode::Iterative);
        model.center = seed_from_ray(chief_ray);
        if (!model.center.valid) {
            ChiefAnchor anchor = ensure_chief_anchor(field_index, wave_index, field, wave);
            model.center = anchor.seed;
        }
    }

    const double hard_radius = first_surface_hard_radius(wave.value_um);
    if (!model.center.valid ||
        !std::isfinite(model.center.x) ||
        !std::isfinite(model.center.y) ||
        (hard_radius > 0.0 &&
         aperture_clip_exceeded(
             std::hypot(model.center.x, model.center.y),
             hard_radius))) {
        model.valid = false;
        return finish_model();
    }

    const double ep_radius = std::max(1.0e-9, tracer_.entrance_pupil_radius(wave.value_um));
    const double search_radius = std::max(1.0e-9, first_surface_search_radius(wave.value_um));
    const double raw_edge_radius = std::clamp(
        ep_radius * std::max(0.0, cfg.edge_initial_radius_fraction),
        1.0e-9,
        std::max(1.0e-9, search_radius));
    double edge_radius = raw_edge_radius;
    const double center_beam_edge_radius = center_iterative_edge_radius(wave);
    if (std::isfinite(center_beam_edge_radius) && center_beam_edge_radius > 1.0e-9) {
        edge_radius = std::min(edge_radius, center_beam_edge_radius);
    }
    const double rs = tracer_.stop_target_radius(wave.value_um, RayAimingMode::ThreeStep);
    const int edge_passes = std::max(1, cfg.edge_passes);
    auto physical_stop_radius = [&]() {
        double radius = rs;
        const std::size_t stop = tracer_.stop_surface_index();
        if (stop < system_.surfaces.size()) {
            const Surface& stop_surface = system_.surfaces[stop];
            if (stop_surface.aperture_radius > 0.0) {
                radius = stop_surface.aperture_radius;
            } else if (stop_surface.semi_aperture > 0.0) {
                radius = stop_surface.semi_aperture;
            }
        }
        return radius;
    };
    const double edge_residual_limit = std::max(
        std::max(0.0, system_.ray_aiming.tolerance),
        std::max(0.0, cfg.edge_residual_stop_fraction) * physical_stop_radius());
    auto seed_in_first_surface_bounds = [&](const FirstSurfaceSeed& seed) {
        if (!seed.valid || !std::isfinite(seed.x) || !std::isfinite(seed.y)) {
            return false;
        }
        return hard_radius <= 0.0 ||
               !aperture_clip_exceeded(std::hypot(seed.x, seed.y), hard_radius);
    };
    const std::string edge_solver = cfg.edge_solver;
    const bool fixed_jacobian_edge_solver =
        edge_solver == "fixed_jacobian" ||
        edge_solver == "fixed_jacobian_only" ||
        edge_solver == "fixed" ||
        edge_solver == "linear" ||
        edge_solver == "one_step";
    const bool secant_edge_solver =
        edge_solver == "secant" ||
        edge_solver == "broyden" ||
        edge_solver == "secant_only" ||
        edge_solver == "broyden_only";
    const bool legacy_iterative_edge_solver =
        edge_solver == "iterative" ||
        edge_solver == "legacy" ||
        edge_solver == "newton" ||
        edge_solver == "solve_stop_aim";
    double max_residual = 0.0;
    std::array<double, 2> axis_scale_hints{{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
    }};
    double common_scale_hint = std::numeric_limits<double>::quiet_NaN();

    auto relative_edge_scale = [&](const FirstSurfaceSeed& full_seed,
                                   const FirstSurfaceSeed& seed) {
        if (!full_seed.valid || !seed.valid) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const double full_dx = full_seed.x - model.center.x;
        const double full_dy = full_seed.y - model.center.y;
        const double denom = full_dx * full_dx + full_dy * full_dy;
        if (!std::isfinite(denom) || denom <= 1.0e-24) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const double seed_dx = seed.x - model.center.x;
        const double seed_dy = seed.y - model.center.y;
        const double scale = (seed_dx * full_dx + seed_dy * full_dy) / denom;
        return std::isfinite(scale) ? scale : std::numeric_limits<double>::quiet_NaN();
    };

    auto remember_scale_hint = [&](std::size_t axis,
                                   const FirstSurfaceSeed& full_seed,
                                   const FirstSurfaceSeed& accepted_seed) {
        const double scale = relative_edge_scale(full_seed, accepted_seed);
        if (!std::isfinite(scale) || scale <= 0.0) {
            return;
        }
        if (axis < axis_scale_hints.size()) {
            axis_scale_hints[axis] = scale;
        }
        common_scale_hint = scale;
    };

    auto solve_edge = [&](double axis_x,
                          double axis_y,
                          double sign,
                          int iterations,
                          FirstSurfaceSeed* out_seed) {
        return ::optiland::raytrace_detail::ThreeStepSolveEdge::run(*this, 
            field,
            wave,
            diag,
            model,
            edge_radius,
            rs,
            edge_passes,
            edge_residual_limit,
            seed_in_first_surface_bounds,
            fixed_jacobian_edge_solver,
            secant_edge_solver,
            legacy_iterative_edge_solver,
            max_residual,
            axis_scale_hints,
            common_scale_hint,
            remember_scale_hint,
            axis_x,
            axis_y,
            sign,
            iterations,
            out_seed
        );
    };

    const bool mer_plus_ok = solve_edge(
        mer_x,
        mer_y,
        1.0,
        std::max(0, cfg.meridional_iterations),
        &model.meridional_plus);
    const bool mer_minus_ok = solve_edge(
        mer_x,
        mer_y,
        -1.0,
        std::max(0, cfg.meridional_iterations),
        &model.meridional_minus);
    const bool sag_plus_ok = solve_edge(
        sag_x,
        sag_y,
        1.0,
        std::max(0, cfg.sagittal_iterations),
        &model.sagittal_plus);
    const bool sag_minus_ok = solve_edge(
        sag_x,
        sag_y,
        -1.0,
        std::max(0, cfg.sagittal_iterations),
        &model.sagittal_minus);

    auto edge_vector = [&](const FirstSurfaceSeed& plus,
                           bool plus_ok,
                           const FirstSurfaceSeed& minus,
                           bool minus_ok,
                           double fallback_x,
                           double fallback_y,
                           double* vx,
                           double* vy) {
        if (plus_ok && minus_ok) {
            *vx = 0.5 * (plus.x - minus.x);
            *vy = 0.5 * (plus.y - minus.y);
        } else if (plus_ok) {
            *vx = plus.x - model.center.x;
            *vy = plus.y - model.center.y;
        } else if (minus_ok) {
            *vx = model.center.x - minus.x;
            *vy = model.center.y - minus.y;
        } else {
            *vx = edge_radius * fallback_x;
            *vy = edge_radius * fallback_y;
        }
        if (!std::isfinite(*vx) || !std::isfinite(*vy) ||
            std::hypot(*vx, *vy) <= 1.0e-12) {
            *vx = edge_radius * fallback_x;
            *vy = edge_radius * fallback_y;
        }
    };
    edge_vector(
        model.meridional_plus,
        mer_plus_ok,
        model.meridional_minus,
        mer_minus_ok,
        mer_x,
        mer_y,
        &model.meridional_x,
        &model.meridional_y);
    edge_vector(
        model.sagittal_plus,
        sag_plus_ok,
        model.sagittal_minus,
        sag_minus_ok,
        sag_x,
        sag_y,
        &model.sagittal_x,
        &model.sagittal_y);

    double center_sx = 0.0;
    double center_sy = 0.0;
    add_diag(diag.three_step_model_stop_probe_calls);
    bool center_stop_ok = false;
    {
        ScopedDiagTimer stop_timer(diag.three_step_model_stop_probe_ns);
        center_stop_ok = sample_stop_from_seed(field, wave, model.center, &center_sx, &center_sy);
    }
    if (center_stop_ok) {
        add_diag(diag.three_step_model_stop_probe_successes);
        model.center_stop_x = center_sx;
        model.center_stop_y = center_sy;
        const double residual = std::hypot(center_sx - field.fvcx * rs,
                                           center_sy - field.fvcy * rs);
        if (std::isfinite(residual)) {
            max_residual = std::max(max_residual, residual);
            add_diag_residual(
                diag.three_step_center_residual_count,
                diag.three_step_center_residual_sum_scaled,
                diag.three_step_center_residual_max_scaled,
                residual);
        }
    }

    auto build_central_fit = [&]() {
        return ::optiland::raytrace_detail::ThreeStepBuildCentralFit::run(*this, 
            field,
            wave,
            diag,
            model,
            mer_x,
            mer_y,
            sag_x,
            sag_y,
            search_radius,
            rs,
            seed_in_first_surface_bounds,
            mer_plus_ok,
            mer_minus_ok,
            sag_plus_ok,
            sag_minus_ok,
            center_stop_ok
        );
    };
    if (normalized_mapping_mode(cfg.sampling_mode, "chief_edge_interpolation") ==
        "central_fit") {
        build_central_fit();
    }
    model.fit_residual = max_residual;
    model.valid =
        std::isfinite(model.meridional_x) &&
        std::isfinite(model.meridional_y) &&
        std::isfinite(model.sagittal_x) &&
        std::isfinite(model.sagittal_y);
    return finish_model();
}

} // namespace optiland
