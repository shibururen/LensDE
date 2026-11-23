#pragma once
#include "../aiming_geometry_inline.hpp"
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct ThreeStepBuildCentralFit {
    using ThreeStepModel = AimingSession::ThreeStepModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;
    using PilotSample = AimingSession::PilotSample;

    template <
        class Capture_mer_x,
        class Capture_mer_y,
        class Capture_sag_x,
        class Capture_sag_y,
        class Capture_search_radius,
        class Capture_seed_in_first_surface_bounds,
        class Capture_mer_plus_ok,
        class Capture_mer_minus_ok,
        class Capture_sag_plus_ok,
        class Capture_sag_minus_ok,
        class Capture_center_stop_ok
    >
    static auto run(
        AimingSession& session,
        const Field& field,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        ThreeStepModel& model,
        Capture_mer_x& mer_x,
        Capture_mer_y& mer_y,
        Capture_sag_x& sag_x,
        Capture_sag_y& sag_y,
        Capture_search_radius& search_radius,
        const double& rs,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_mer_plus_ok& mer_plus_ok,
        Capture_mer_minus_ok& mer_minus_ok,
        Capture_sag_plus_ok& sag_plus_ok,
        Capture_sag_minus_ok& sag_minus_ok,
        Capture_center_stop_ok& center_stop_ok
    ) {
        if (!center_stop_ok) {
            return;
        }
        const auto& cp_cfg = session.system_.ray_aiming.calibrated_paraxial;
        const std::vector<double> central_probe_steps = aperture_aware_probe_steps(
            search_radius,
            std::max(1.0e-9, std::abs(rs)),
            cp_cfg.jacobian_probe_fraction,
            cp_cfg.min_probe_step_mm,
            cp_cfg.max_probe_step_mm);
        auto sample_seed = [&](const FirstSurfaceSeed& seed, double* sx, double* sy) {
            if (!seed_in_first_surface_bounds(seed)) {
                return false;
            }
            add_diag(diag.three_step_model_stop_probe_calls);
            bool ok = false;
            {
                ScopedDiagTimer stop_timer(diag.three_step_model_stop_probe_ns);
                ok = session.sample_stop_from_seed(field, wave, seed, sx, sy);
            }
            if (ok) {
                add_diag(diag.three_step_model_stop_probe_successes);
            }
            return ok;
        };
        for (double h : central_probe_steps) {
            const FirstSurfaceSeed xp{model.center.x + h, model.center.y, true};
            const FirstSurfaceSeed xm{model.center.x - h, model.center.y, true};
            const FirstSurfaceSeed yp{model.center.x, model.center.y + h, true};
            const FirstSurfaceSeed ym{model.center.x, model.center.y - h, true};
            double xp_sx = 0.0;
            double xp_sy = 0.0;
            double xm_sx = 0.0;
            double xm_sy = 0.0;
            double yp_sx = 0.0;
            double yp_sy = 0.0;
            double ym_sx = 0.0;
            double ym_sy = 0.0;
            if (!sample_seed(xp, &xp_sx, &xp_sy) ||
                !sample_seed(xm, &xm_sx, &xm_sy) ||
                !sample_seed(yp, &yp_sx, &yp_sy) ||
                !sample_seed(ym, &ym_sx, &ym_sy)) {
                continue;
            }
            const double denom = 2.0 * h;
            const double j11 = (xp_sx - xm_sx) / denom;
            const double j21 = (xp_sy - xm_sy) / denom;
            const double j12 = (yp_sx - ym_sx) / denom;
            const double j22 = (yp_sy - ym_sy) / denom;
            const double det = j11 * j22 - j12 * j21;
            if (!std::isfinite(det) || std::abs(det) <= 1.0e-14) {
                continue;
            }
            const double condition = condition_number_2x2(j11, j12, j21, j22);
            if (!std::isfinite(condition) || condition > cp_cfg.max_condition_number) {
                continue;
            }
            const std::array<PilotSample, 5> samples{{
                {model.center, model.center_stop_x, model.center_stop_y, true},
                {xp, xp_sx, xp_sy, true},
                {xm, xm_sx, xm_sy, true},
                {yp, yp_sx, yp_sy, true},
                {ym, ym_sx, ym_sy, true},
            }};
            double sum_r2 = 0.0;
            int count = 0;
            for (const auto& sample : samples) {
                const double dx = sample.seed.x - model.center.x;
                const double dy = sample.seed.y - model.center.y;
                const double pred_x = model.center_stop_x + j11 * dx + j12 * dy;
                const double pred_y = model.center_stop_y + j21 * dx + j22 * dy;
                const double ex = pred_x - sample.stop_x;
                const double ey = pred_y - sample.stop_y;
                sum_r2 += ex * ex + ey * ey;
                ++count;
            }
            const double rms = std::sqrt(sum_r2 / static_cast<double>(std::max(1, count)));
            const double fit_limit = std::max(
                session.system_.ray_aiming.tolerance,
                cp_cfg.fit_rms_tolerance_stop_fraction * std::max(1.0e-9, std::abs(rs)));
            if (!std::isfinite(rms) || rms > fit_limit) {
                continue;
            }
            model.b11 = j22 / det;
            model.b12 = -j12 / det;
            model.b21 = -j21 / det;
            model.b22 = j11 / det;
            model.central_fit_rms = rms;
            model.condition_number = condition;
            model.central_fit_valid = true;
            auto linear_seed_for_pupil = [&](const PupilPoint& edge_pupil) {
                const double target_x =
                    (field.fvcx + edge_pupil.px * (1.0 - field.fvdx)) * rs;
                const double target_y =
                    (field.fvcy + edge_pupil.py * (1.0 - field.fvdy)) * rs;
                const double dx = target_x - model.center_stop_x;
                const double dy = target_y - model.center_stop_y;
                FirstSurfaceSeed seed{
                    model.center.x + model.b11 * dx + model.b12 * dy,
                    model.center.y + model.b21 * dx + model.b22 * dy,
                    true};
                seed.valid = std::isfinite(seed.x) && std::isfinite(seed.y);
                return seed;
            };
            auto set_edge_correction = [&](std::size_t index,
                                           const PupilPoint& edge_pupil,
                                           const FirstSurfaceSeed& solved_seed,
                                           bool solved_ok) {
                if (index >= model.edge_corrections.size() ||
                    !solved_ok || !solved_seed.valid) {
                    return;
                }
                const FirstSurfaceSeed linear_seed = linear_seed_for_pupil(edge_pupil);
                if (!linear_seed.valid) {
                    return;
                }
                auto& correction = model.edge_corrections[index];
                correction.correction_x = solved_seed.x - linear_seed.x;
                correction.correction_y = solved_seed.y - linear_seed.y;
                correction.valid = true;
            };
            set_edge_correction(0, PupilPoint{mer_x, mer_y, 1.0},
                                model.meridional_plus, mer_plus_ok);
            set_edge_correction(1, PupilPoint{-mer_x, -mer_y, 1.0},
                                model.meridional_minus, mer_minus_ok);
            set_edge_correction(2, PupilPoint{sag_x, sag_y, 1.0},
                                model.sagittal_plus, sag_plus_ok);
            set_edge_correction(3, PupilPoint{-sag_x, -sag_y, 1.0},
                                model.sagittal_minus, sag_minus_ok);
            return;
        }
    }
};

} // namespace optiland::raytrace_detail
