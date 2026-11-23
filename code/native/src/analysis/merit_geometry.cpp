/*
 * Adds ray-path edge-separation and pupil-order geometry priors to MeritEvaluation.
 * Runs after illumination/vignetting terms and before manufacturing constraints.
 * These priors use measured guard-ray geometry and configured Jacobian thresholds;
 * preserve their field indices, activation rules and normalized penalty scaling.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void MeritEvaluation::append_ray_geometry_priors() {
// Missed guards and successfully traced backward rays can expose overlaps
    // outside the aperture envelope. Use their actual same-height separation
    // with the same lower bound, weight and tolerance as the edge constraint.
    for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
        const double deficit = std::max(
            0.0,
            field_spots[field_index].stats.single_ray_guard_surface_order_edge_deficit);
        const double active_weight = deficit > 0.0 ? config.weight_edge : 0.0;
        const double penalty = scoring_weight(active_weight) * deficit * deficit;
        add_contribution(make_contribution(
            "default_constraint",
            "guard_surface_order_edge_deficit",
            "minimize",
            deficit,
            0.0,
            -kInf,
            kInf,
            active_weight,
            penalty,
            field_index));
    }

    // This prior consumes histories captured during the ordinary primary-wave
    // RMS trace.  Its early-surface weighting makes a late, mild caustic almost
    // neutral while strongly separating the persistent front-train fold that
    // motivated the guard.
    if (config.pupil_order_prior_weight > 0.0) {
        for (std::size_t field_index = 0;
             field_index < field_spots.size();
             ++field_index) {
            const SpotStats& stats = field_spots[field_index].stats;
            if (!std::isfinite(stats.min_pupil_order_jacobian)) {
                continue;
            }
            const double violation =
                std::max(0.0, stats.pupil_order_prior_violation);
            const double penalty =
                config.pupil_order_prior_weight * violation * violation;
            add_contribution(make_contribution(
                "pupil_order_prior",
                "meridional_adjacent_order_jacobian",
                "greater_than",
                stats.min_pupil_order_jacobian,
                config.pupil_order_jacobian_floor,
                config.pupil_order_jacobian_floor,
                kInf,
                config.pupil_order_prior_weight,
                penalty,
                field_index,
                0));
        }
    }
}
}  // namespace analysis_detail

}  // namespace optiland
