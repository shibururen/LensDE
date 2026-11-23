#pragma once
#include "../detail.hpp"
#include "calibration_probe_scale.hpp"

namespace optiland::raytrace_detail {

struct CalibrationTryScaledEdgeAnchor {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_EdgeBackoffResult,
        class Capture_existing,
        class Capture_seed_in_first_surface_bounds,
        class Capture_accept_edge_stop_sample,
        class Capture_scaled_edge_seed
    >
    static auto run(
        AimingSession& session,
        const Wavelength& wave,
        Capture_existing& existing,
        CalibratedParaxialModel& model,
        const Field& field,
        const double& rs,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_accept_edge_stop_sample& accept_edge_stop_sample,
        Capture_scaled_edge_seed& scaled_edge_seed,
        CalibratedParaxialModel::EdgeAnchor& anchor,
        const PupilPoint& edge,
        const FirstSurfaceSeed& full_seed,
        const std::vector<double>& scale_hints,
        bool include_default_backoff
    ) {
        Type_EdgeBackoffResult result;
        if (!full_seed.valid) {
            return result;
        }
        const double target_x = (field.fvcx + edge.px * (1.0 - field.fvdx)) * rs;
        const double target_y = (field.fvcy + edge.py * (1.0 - field.fvdy)) * rs;
        const double target_dx = edge.px * (1.0 - field.fvdx) * rs;
        const double target_dy = edge.py * (1.0 - field.fvdy) * rs;
        const double target_dr = std::hypot(target_dx, target_dy);
        std::vector<double> scales;
        auto push_scale = [&](double scale) {
            if (!std::isfinite(scale) || scale <= 0.0) {
                return;
            }
            const double clamped = std::clamp(scale, 1.0e-3, 1.25);
            for (const double existing : scales) {
                if (std::abs(existing - clamped) <=
                    1.0e-7 * std::max(1.0, std::abs(clamped))) {
                    return;
                }
            }
            scales.push_back(clamped);
        };
        for (const double hinted_scale : scale_hints) {
            push_scale(hinted_scale);
        }
        if (include_default_backoff) {
            push_scale(0.25);
            push_scale(0.5);
            push_scale(1.0);
        } else if (scales.empty()) {
            push_scale(1.0);
        }

        std::vector<double> attempted_scales;
        double best_residual = std::numeric_limits<double>::infinity();
        auto scale_was_attempted = [&](double scale) {
            for (const double attempted : attempted_scales) {
                if (std::abs(attempted - scale) <=
                    1.0e-7 * std::max(1.0, std::abs(scale))) {
                    return true;
                }
            }
            return false;
        };

        auto probe_scale = [&](double scale,
                               FirstSurfaceSeed* out_seed,
                               double* out_sx,
                               double* out_sy,
                               double* out_residual) {
return ::optiland::raytrace_detail::CalibrationProbeScale::run(
    session,
    wave,
    field,
    seed_in_first_surface_bounds,
    scaled_edge_seed,
    full_seed,
    result,
    target_x,
    target_y,
    attempted_scales,
    best_residual,
    scale_was_attempted,
    scale,
    out_seed,
    out_sx,
    out_sy,
    out_residual
);
};
        auto projected_stop_radius = [&](double sx, double sy) {
            if (target_dr <= 1.0e-12) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            const double stop_dx = sx - model.chief_stop_x;
            const double stop_dy = sy - model.chief_stop_y;
            const double projected =
                (stop_dx * target_dx + stop_dy * target_dy) / target_dr;
            return std::isfinite(projected)
                       ? projected
                       : std::numeric_limits<double>::quiet_NaN();
        };

        for (const double scale : scales) {
            FirstSurfaceSeed sampled_seed;
            double sx = 0.0;
            double sy = 0.0;
            double residual = 0.0;
            const bool sampled = probe_scale(scale, &sampled_seed, &sx, &sy, &residual);
            if (!sampled) {
                continue;
            }
            if (accept_edge_stop_sample(anchor, edge, sampled_seed, sx, sy)) {
                result.accepted = true;
                result.seed = anchor.seed;
                result.have_seed = true;
                return result;
            }
            if (target_dr <= 1.0e-12) {
                continue;
            }
            const double projected_stop_dr = projected_stop_radius(sx, sy);
            if (!std::isfinite(projected_stop_dr) || projected_stop_dr <= 1.0e-12) {
                continue;
            }
            const double predicted_scale =
                std::clamp(scale * target_dr / projected_stop_dr, 1.0e-3, 1.25);
            FirstSurfaceSeed predicted_seed;
            double predicted_sx = 0.0;
            double predicted_sy = 0.0;
            double predicted_residual = 0.0;
            if (!probe_scale(
                    predicted_scale,
                    &predicted_seed,
                    &predicted_sx,
                    &predicted_sy,
                    &predicted_residual)) {
                continue;
            }
            if (accept_edge_stop_sample(
                    anchor,
                    edge,
                    predicted_seed,
                    predicted_sx,
                    predicted_sy)) {
                result.accepted = true;
                result.seed = anchor.seed;
                result.have_seed = true;
                return result;
            }
        }
        return result;
    }
};

} // namespace optiland::raytrace_detail
