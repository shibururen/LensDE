/*
 * Adds the supervised off-axis illumination floor and serialized vignetting priors.
 * Called by the merit pipeline using proxies already measured for each field.
 * Field supervision, unit-pupil feasibility, loss budgets and weak-prior schedules
 * must remain distinct from a standalone grid-integrated illumination measurement.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void MeritEvaluation::append_illumination_floor() {
if (minimum_relative_illumination_enabled(config)) {
        constexpr double kAutomaticRelativeIlluminationWeight = 25.0;
        double minimum_value = std::numeric_limits<double>::quiet_NaN();
        std::size_t minimum_field_index = 0;
        bool have_off_axis_field = false;
        for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
            if (!illumination_fields.empty() &&
                std::find(illumination_fields.begin(), illumination_fields.end(), field_index) == illumination_fields.end()) {
                continue;
            }
            const double field_radius = std::hypot(
                field_spots[field_index].field.hx,
                field_spots[field_index].field.hy);
            if (field_radius <= 1.0e-12) {
                continue;
            }
            have_off_axis_field = true;
            const double value =
                field_index < score.relative_illumination_proxy_by_field.size()
                    ? score.relative_illumination_proxy_by_field[field_index]
                    : std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(value)) {
                minimum_value = value;
                minimum_field_index = field_index;
                break;
            }
            if (!std::isfinite(minimum_value) || value < minimum_value) {
                minimum_value = value;
                minimum_field_index = field_index;
            }
        }
        if (have_off_axis_field) {
            const double lower = config.minimum_relative_illumination;
            const double deficit = std::isfinite(minimum_value)
                                       ? std::max(0.0, lower - minimum_value)
                                       : 1.0e6;
            add_contribution(make_contribution(
                "minimum_relative_illumination",
                "relative_illumination_proxy",
                "greater_than",
                minimum_value,
                lower,
                lower,
                kInf,
                kAutomaticRelativeIlluminationWeight,
                kAutomaticRelativeIlluminationWeight * deficit * deficit,
                minimum_field_index,
                0));
        }
    }
}

void MeritEvaluation::append_vignetting_priors() {
// Automatic vignetting uses a feasible magnitude/asymmetry
    // parameterization, but imported systems and explicit legacy FVCY/FVDY
    // variables still need an auditable geometry guard.  The quartic schedule
    // is deliberately a weak Other-category prior; it no longer excludes
    // intermediate-field solutions from the search space.
    if (minimum_relative_illumination_enabled(config)) {
        double maximum_field_radius = 0.0;
        for (const FieldSpot& field_spot : field_spots) {
            maximum_field_radius = std::max(
                maximum_field_radius,
                std::hypot(field_spot.field.hx, field_spot.field.hy));
        }
        const double edge_budget = std::min(
            std::clamp(config.vignetting_edge_fvdy_cap, 0.0, 1.0),
            std::clamp(config.vignetting_loss_budget_fraction, 0.0, 1.0) *
                (1.0 - config.minimum_relative_illumination));
        const double hard_weight =
            std::max(0.0, config.vignetting_meridional_excess_weight);
        const double soft_weight =
            std::max(0.0, config.vignetting_soft_prior_weight);
        const double soft_power =
            std::max(0.0, config.vignetting_soft_field_power);
        const double support_weight =
            config.dynamic_apertures
                ? std::max(0.0, config.vignetting_boundary_support_weight)
                : 0.0;
        const double aperture_margin =
            std::max(1.0, config.effective_clear_aperture_margin());
        const double support_tolerance = std::max({
            1.0e-9,
            config.vignetting_boundary_support_tolerance,
            1.0 - 1.0 / aperture_margin});
        const double support_activation =
            std::max(0.0, config.vignetting_boundary_support_activation);
        const double support_transition =
            std::max(1.0e-9, config.vignetting_boundary_support_transition);
        for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
            const Field& field = field_spots[field_index].field;
            const double radius = std::hypot(field.hx, field.hy);
            if (radius <= 1.0e-12 || maximum_field_radius <= 1.0e-12) {
                continue;
            }
            const double rho = std::clamp(radius / maximum_field_radius, 0.0, 1.0);
            const double magnitude = std::max(0.0, field.fvdy);
            const double meridional_excess =
                std::max(0.0, std::abs(field.fvcy) - magnitude);
            const double active_hard_weight =
                meridional_excess > 0.0 ? hard_weight : 0.0;
            add_contribution(make_contribution(
                "automatic_vignetting_geometry",
                "fvcy_fvdy_excess",
                "less_than",
                meridional_excess,
                0.0,
                -kInf,
                0.0,
                active_hard_weight,
                active_hard_weight * meridional_excess * meridional_excess,
                field_index,
                0));

            const double soft_upper = edge_budget * std::pow(rho, soft_power);
            const double soft_excess = std::max(0.0, magnitude - soft_upper);
            add_contribution(make_contribution(
                "automatic_vignetting_prior",
                "vignette_y_soft_prior",
                "less_than",
                magnitude,
                soft_upper,
                -kInf,
                soft_upper,
                soft_weight,
                soft_weight * soft_excess * soft_excess,
                field_index,
                0));

            // FVDY/FVCY describe the two one-sided pupil cuts directly:
            // v-=FVDY+FVCY and v+=FVDY-FVCY.  Only an appreciable cut is
            // required to have selective physical evidence on a dynamically
            // generated clear aperture: its retained edge must be near the
            // aperture while the opposite edge/chief trio remains farther
            // inside.  The stored value is already the joint evidence
            // deficit, so zero means supported.  This never re-solves or
            // writes the anchored vignetting coefficients.
            const double cuts[2] = {
                std::max(0.0, field.fvdy + field.fvcy),
                std::max(0.0, field.fvdy - field.fvcy)};
            const double gaps[2] = {
                field.vignetting_negative_boundary_support_gap,
                field.vignetting_positive_boundary_support_gap};
            const int support_surfaces[2] = {
                field.vignetting_negative_boundary_support_surface,
                field.vignetting_positive_boundary_support_surface};
            const double candidate_radii[2] = {
                field.vignetting_negative_boundary_candidate_radius,
                field.vignetting_positive_boundary_candidate_radius};
            const double pretrim_radii[2] = {
                field.vignetting_negative_boundary_pretrim_radius,
                field.vignetting_positive_boundary_pretrim_radius};
            const double protection_radii[2] = {
                field.vignetting_negative_boundary_protection_radius,
                field.vignetting_positive_boundary_protection_radius};
            const double trim_windows[2] = {
                field.vignetting_negative_boundary_trim_window,
                field.vignetting_positive_boundary_trim_window};
            for (std::size_t side = 0; side < 2; ++side) {
                const double activation =
                    config.vignetting_physical_cut_match_enabled
                        ? (cuts[side] > 1.0e-12 ? 1.0 : 0.0)
                        : std::clamp(
                              (cuts[side] - support_activation) /
                                  support_transition,
                              0.0,
                              1.0);
                if (activation <= 0.0 || support_weight <= 0.0) {
                    continue;
                }
                const double gap = std::isfinite(gaps[side])
                                       ? std::max(0.0, gaps[side])
                                       : 1.0;
                const double active_support_weight =
                    support_weight * activation * activation;
                MeritContribution contribution = make_contribution(
                    side == 0
                        ? "automatic_vignetting_negative_boundary_support"
                        : "automatic_vignetting_positive_boundary_support",
                    "vignetting_boundary_aperture_support",
                    "less_than",
                    gap,
                    0.0,
                    -kInf,
                    0.0,
                    active_support_weight,
                    active_support_weight * gap * gap,
                    field_index,
                    config.vignetting_boundary_support_contract ==
                            VignettingBoundarySupportContract::GlobalTrimRealizability &&
                            support_surfaces[side] >= 0
                        ? static_cast<std::size_t>(
                              2 * support_surfaces[side] +
                              static_cast<int>(side))
                        : side);
                if (config.vignetting_boundary_support_contract ==
                        VignettingBoundarySupportContract::GlobalTrimRealizability &&
                    support_surfaces[side] >= 0 &&
                    std::isfinite(candidate_radii[side])) {
                    contribution.aperture_candidate_radius =
                        candidate_radii[side];
                    contribution.aperture_radius_before_trim =
                        pretrim_radii[side];
                    contribution.aperture_protection_radius =
                        protection_radii[side];
                    contribution.aperture_trim_window =
                        trim_windows[side];
                }
                add_contribution(std::move(contribution));
            }
        }
    }
}
}  // namespace analysis_detail

}  // namespace optiland
