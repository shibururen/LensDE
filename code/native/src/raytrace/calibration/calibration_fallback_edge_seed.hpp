#pragma once
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct CalibrationFallbackEdgeSeed {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Capture_seed_in_first_surface_bounds,
        class Capture_edge_pupils,
        class Capture_first_surface_edge_radius,
        class Capture_center_beam_edge_radius
    >
    static auto run(
        AimingSession& session,
        const Wavelength& wave,
        CalibratedParaxialModel& model,
        const Field& field,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_edge_pupils& edge_pupils,
        Capture_first_surface_edge_radius& first_surface_edge_radius,
        Capture_center_beam_edge_radius& center_beam_edge_radius,
        std::size_t edge_index
    ) -> FirstSurfaceSeed {
        FirstSurfaceSeed fallback;
        if (edge_index >= edge_pupils.size()) {
            return fallback;
        }
        const PupilPoint& edge = edge_pupils[edge_index];
        auto opposite_index = [&]() -> std::size_t {
            if (edge.px > 0.5) {
                return 1;
            }
            if (edge.px < -0.5) {
                return 0;
            }
            if (edge.py > 0.5) {
                return 3;
            }
            return 2;
        };
        const std::size_t opposite = opposite_index();
        if (opposite < model.edge_anchors.size()) {
            const auto& opposite_anchor = model.edge_anchors[opposite];
            if (opposite_anchor.valid && opposite_anchor.seed.valid) {
                fallback = {
                    model.chief_seed.x - (opposite_anchor.seed.x - model.chief_seed.x),
                    model.chief_seed.y - (opposite_anchor.seed.y - model.chief_seed.y),
                    true};
                if (seed_in_first_surface_bounds(fallback)) {
                    return fallback;
                }
            }
        }

        const double transverse_radius =
            center_beam_edge_radius > 1.0e-9
                ? center_beam_edge_radius
                : first_surface_edge_radius;
        fallback = session.shifted_chief_seed_by_transverse_radius(
            field,
            wave,
            model.chief_seed,
            edge,
            transverse_radius);
        if (seed_in_first_surface_bounds(fallback)) {
            return fallback;
        }

        fallback = {
            model.chief_seed.x + first_surface_edge_radius * edge.px,
            model.chief_seed.y + first_surface_edge_radius * edge.py,
            true};
        if (seed_in_first_surface_bounds(fallback)) {
            return fallback;
        }
        return FirstSurfaceSeed{};
    }
};

} // namespace optiland::raytrace_detail
