/*
 * Loads vignetting priors, loss budgets and boundary-support/physical-cut contracts.
 * load_merit_json delegates these settings before chromatic and sampling families.
 * Retain the existing clamping, contract aliases and tolerance defaults that couple
 * serialized FVC/FVD factors to aperture-envelope scoring and illumination guards.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_vignetting(const Json& root, MeritConfig* config) {
config->vignetting_meridional_excess_weight = root.at(
            "vignetting_meridional_excess_weight").as_number(
                config->vignetting_meridional_excess_weight);
        config->vignetting_soft_prior_weight = root.at(
            "vignetting_soft_prior_weight").as_number(
                config->vignetting_soft_prior_weight);
        config->vignetting_soft_field_power = root.at(
            "vignetting_soft_field_power").as_number(
                config->vignetting_soft_field_power);
        config->vignetting_loss_budget_fraction = root.at(
            "vignetting_loss_budget_fraction").as_number(
                config->vignetting_loss_budget_fraction);
        config->vignetting_edge_fvdy_cap = root.at(
            "vignetting_edge_fvdy_cap").as_number(
                config->vignetting_edge_fvdy_cap);
        const std::string vignetting_boundary_support_contract = root.at(
            "vignetting_boundary_support_contract").as_string("");
        if (!vignetting_boundary_support_contract.empty()) {
            if (vignetting_boundary_support_contract ==
                    "selective_aperture_contact" ||
                vignetting_boundary_support_contract == "legacy") {
                config->vignetting_boundary_support_contract =
                    VignettingBoundarySupportContract::SelectiveApertureContact;
            } else if (vignetting_boundary_support_contract ==
                           "global_trim_realizability" ||
                       vignetting_boundary_support_contract == "global_trim") {
                config->vignetting_boundary_support_contract =
                    VignettingBoundarySupportContract::GlobalTrimRealizability;
            } else {
                throw std::runtime_error(
                    "unsupported vignetting_boundary_support_contract: " +
                    vignetting_boundary_support_contract);
            }
        }
        config->vignetting_boundary_support_weight = root.at(
            "vignetting_boundary_support_weight").as_number(
                config->vignetting_boundary_support_weight);
        config->vignetting_boundary_support_tolerance = root.at(
            "vignetting_boundary_support_tolerance").as_number(
                config->vignetting_boundary_support_tolerance);
        config->vignetting_boundary_support_activation = root.at(
            "vignetting_boundary_support_activation").as_number(
                config->vignetting_boundary_support_activation);
        config->vignetting_boundary_support_transition = root.at(
            "vignetting_boundary_support_transition").as_number(
                config->vignetting_boundary_support_transition);
        config->vignetting_boundary_support_selectivity_slope = root.at(
            "vignetting_boundary_support_selectivity_slope").as_number(
                config->vignetting_boundary_support_selectivity_slope);
        const Json& physical_cut_match = root.at(
            "vignetting_physical_cut_match_enabled");
        config->vignetting_physical_cut_match_enabled =
            physical_cut_match.is_null()
                ? root.at(
                      "vignetting_global_trim_cut_match_experimental")
                      .as_bool(config->vignetting_physical_cut_match_enabled)
                : physical_cut_match.as_bool(
                      config->vignetting_physical_cut_match_enabled);
        config->vignetting_merit_aperture_envelope_experimental = root.at(
            "vignetting_merit_aperture_envelope_experimental").as_bool(
                config->vignetting_merit_aperture_envelope_experimental);
        if (!std::isfinite(config->quality_relative_illumination_tol) ||
            config->quality_relative_illumination_tol <= 0.0) {
            config->quality_relative_illumination_tol =
                MeritConfig{}.quality_relative_illumination_tol;
        }
        if (!std::isfinite(config->vignetting_boundary_support_weight) ||
            config->vignetting_boundary_support_weight < 0.0) {
            config->vignetting_boundary_support_weight =
                MeritConfig{}.vignetting_boundary_support_weight;
        }
        if (!std::isfinite(config->vignetting_boundary_support_tolerance) ||
            config->vignetting_boundary_support_tolerance <= 0.0) {
            config->vignetting_boundary_support_tolerance =
                MeritConfig{}.vignetting_boundary_support_tolerance;
        }
        if (!std::isfinite(config->vignetting_boundary_support_activation) ||
            config->vignetting_boundary_support_activation < 0.0) {
            config->vignetting_boundary_support_activation =
                MeritConfig{}.vignetting_boundary_support_activation;
        }
        if (!std::isfinite(config->vignetting_boundary_support_transition) ||
            config->vignetting_boundary_support_transition <= 0.0) {
            config->vignetting_boundary_support_transition =
                MeritConfig{}.vignetting_boundary_support_transition;
        }
        if (!std::isfinite(config->vignetting_boundary_support_selectivity_slope) ||
            config->vignetting_boundary_support_selectivity_slope < 0.0) {
            config->vignetting_boundary_support_selectivity_slope =
                MeritConfig{}.vignetting_boundary_support_selectivity_slope;
        }
}
}  // namespace io_detail

}  // namespace optiland
