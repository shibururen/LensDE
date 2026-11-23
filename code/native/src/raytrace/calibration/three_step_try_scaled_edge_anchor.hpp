#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct ThreeStepTryScaledEdgeAnchor {
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_EdgeBackoffResult,
        class Capture_seed_in_first_surface_bounds,
        class Capture_axis_scale_hints,
        class Capture_out_seed,
        class Capture_full_seed,
        class Capture_target_x,
        class Capture_target_y,
        class Capture_target_dx,
        class Capture_target_dy,
        class Capture_target_dr,
        class Capture_center_target_x,
        class Capture_center_target_y,
        class Capture_axis_index,
        class Capture_accept_edge_stop_sample,
        class Capture_scaled_edge_seed
    >
    static auto run(
        AimingSession& session,
        const Field& field,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_axis_scale_hints& axis_scale_hints,
        double& common_scale_hint,
        Capture_out_seed& out_seed,
        Capture_full_seed& full_seed,
        Capture_target_x& target_x,
        Capture_target_y& target_y,
        Capture_target_dx& target_dx,
        Capture_target_dy& target_dy,
        Capture_target_dr& target_dr,
        Capture_center_target_x& center_target_x,
        Capture_center_target_y& center_target_y,
        Capture_axis_index& axis_index,
        Capture_accept_edge_stop_sample& accept_edge_stop_sample,
        Capture_scaled_edge_seed& scaled_edge_seed
    ) {
        Type_EdgeBackoffResult result;
        if (!full_seed.valid) {
            return result;
        }
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
        push_scale(axis_scale_hints[axis_index]);
        push_scale(common_scale_hint);
        push_scale(0.25);
        push_scale(0.5);
        push_scale(1.0);

        std::vector<double> attempted_scales;
        auto scale_was_attempted = [&](double scale) {
            for (const double attempted : attempted_scales) {
                if (std::abs(attempted - scale) <=
                    1.0e-7 * std::max(1.0, std::abs(scale))) {
                    return true;
                }
            }
            return false;
        };

        auto record_sample = [&](const FirstSurfaceSeed& seed,
                                 double sx,
                                 double sy,
                                 double residual) {
            if (result.sample.valid &&
                std::hypot(result.sample.seed.x - seed.x,
                           result.sample.seed.y - seed.y) > 1.0e-12 &&
                (!result.previous_sample.valid ||
                 result.sample.residual < result.previous_sample.residual)) {
                result.previous_sample = result.sample;
            }
            result.sample = {true, seed, sx, sy, residual};
            result.seed = seed;
            result.have_seed = true;
            result.residual = residual;
        };

        auto probe_scale = [&](double scale,
                               FirstSurfaceSeed* out_seed,
                               double* out_sx,
                               double* out_sy,
                               double* out_residual) {
            if (!std::isfinite(scale) || scale <= 0.0 || scale_was_attempted(scale)) {
                return false;
            }
            attempted_scales.push_back(scale);
            const FirstSurfaceSeed seed = scaled_edge_seed(scale);
            if (!seed_in_first_surface_bounds(seed)) {
                return false;
            }
            double sx = 0.0;
            double sy = 0.0;
            add_diag(diag.three_step_edge_initial_ray_calls);
            if (!session.sample_stop_from_seed(field, wave, seed, &sx, &sy, nullptr, true, true)) {
                return false;
            }
            const double residual = std::hypot(sx - target_x, sy - target_y);
            if (!std::isfinite(residual)) {
                return false;
            }
            if (!result.have_seed || residual < result.residual) {
                record_sample(seed, sx, sy, residual);
            } else if (result.sample.valid &&
                       std::hypot(result.sample.seed.x - seed.x,
                                  result.sample.seed.y - seed.y) > 1.0e-12 &&
                       (!result.previous_sample.valid ||
                        residual < result.previous_sample.residual)) {
                result.previous_sample = {true, seed, sx, sy, residual};
            }
            if (out_seed) {
                *out_seed = seed;
            }
            if (out_sx) {
                *out_sx = sx;
            }
            if (out_sy) {
                *out_sy = sy;
            }
            if (out_residual) {
                *out_residual = residual;
            }
            return true;
        };

        auto projected_stop_radius = [&](double sx, double sy) {
            if (target_dr <= 1.0e-12) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            const double stop_dx = sx - center_target_x;
            const double stop_dy = sy - center_target_y;
            const double projected = (stop_dx * target_dx + stop_dy * target_dy) / target_dr;
            return std::isfinite(projected)
                       ? projected
                       : std::numeric_limits<double>::quiet_NaN();
        };

        for (const double scale : scales) {
            FirstSurfaceSeed sampled_seed;
            double sx = 0.0;
            double sy = 0.0;
            double residual = 0.0;
            if (!probe_scale(scale, &sampled_seed, &sx, &sy, &residual)) {
                continue;
            }
            if (accept_edge_stop_sample(sampled_seed, sx, sy)) {
                result.accepted = true;
                result.seed = sampled_seed;
                result.have_seed = true;
                return result;
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
            if (accept_edge_stop_sample(predicted_seed, predicted_sx, predicted_sy)) {
                result.accepted = true;
                result.seed = predicted_seed;
                result.have_seed = true;
                return result;
            }
        }
        return result;
    }
};

} // namespace optiland::raytrace_detail
