#pragma once
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct CalibrationAcceptEdgeStopSample {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_DeferredEdgeSample,
        class Capture_seed_in_first_surface_bounds,
        class Capture_first_surface_edge_radius,
        class Capture_enforce_edge_branch_continuity,
        class Capture_deferred_edge_samples,
        class Capture_commit_edge_stop_sample
    >
    static auto run(
        AimingSession& session,
        CalibratedParaxialModel& model,
        const Field& field,
        const double& rs,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        const double& residual_limit,
        Capture_first_surface_edge_radius& first_surface_edge_radius,
        Capture_enforce_edge_branch_continuity& enforce_edge_branch_continuity,
        Capture_deferred_edge_samples& deferred_edge_samples,
        Capture_commit_edge_stop_sample& commit_edge_stop_sample,
        CalibratedParaxialModel::EdgeAnchor& anchor,
        const PupilPoint& edge,
        const FirstSurfaceSeed& seed,
        double sx,
        double sy
    ) {
        if (!seed_in_first_surface_bounds(seed)) {
            return false;
        }
        const double target_x = (field.fvcx + edge.px * (1.0 - field.fvdx)) * rs;
        const double target_y = (field.fvcy + edge.py * (1.0 - field.fvdy)) * rs;
        const double residual = std::hypot(sx - target_x, sy - target_y);
        if (!std::isfinite(residual) || residual > residual_limit) {
            return false;
        }
        if (enforce_edge_branch_continuity) {
            const double continuity_x =
                model.chief_seed.x + first_surface_edge_radius * edge.px;
            const double continuity_y =
                model.chief_seed.y + first_surface_edge_radius * edge.py;
            constexpr double kEdgeContinuityFraction = 0.5;
            const double continuity_limit = std::max(
                1.0e-6,
                kEdgeContinuityFraction * first_surface_edge_radius);
            const double continuity_dx = seed.x - continuity_x;
            const double continuity_dy = seed.y - continuity_y;
            const double continuity_distance_sq =
                continuity_dx * continuity_dx + continuity_dy * continuity_dy;
            if (continuity_distance_sq > continuity_limit * continuity_limit) {
                std::size_t edge_index = deferred_edge_samples.size();
                for (std::size_t i = 0; i < model.edge_anchors.size(); ++i) {
                    if (&model.edge_anchors[i] == &anchor) {
                        edge_index = i;
                        break;
                    }
                }
                if (edge_index < deferred_edge_samples.size()) {
                    Type_DeferredEdgeSample& deferred = deferred_edge_samples[edge_index];
                    const double residual_tie = deferred.valid
                                                    ? 1.0e-14 * std::max(
                                                          1.0,
                                                          std::max(residual, deferred.residual))
                                                    : 0.0;
                    if (!deferred.valid ||
                        residual < deferred.residual - residual_tie ||
                        (std::abs(residual - deferred.residual) <= residual_tie &&
                         continuity_distance_sq < deferred.continuity_distance_sq)) {
                        deferred.valid = true;
                        deferred.seed = seed;
                        deferred.stop_x = sx;
                        deferred.stop_y = sy;
                        deferred.residual = residual;
                        deferred.continuity_distance_sq = continuity_distance_sq;
                    }
                }
                return false;
            }
        }
        return commit_edge_stop_sample(anchor, seed, residual, sx, sy);
    }
};

} // namespace optiland::raytrace_detail
