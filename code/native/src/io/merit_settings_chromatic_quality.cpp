/*
 * Loads chromatic, distortion, CRA, conic and surface-angle quality tolerances.
 * load_merit_json invokes this family reader before sampling/trace settings.
 * Existing aliases and positive-finite tolerance fallbacks define the normalization
 * scales later used by chromatic and shape contributions in merit aggregation.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_chromatic_quality(const Json& root, MeritConfig* config) {
config->quality_surface_tangent_angle_tol_deg = root.at(
            "quality_surface_tangent_angle_tol_deg").as_number(
                config->quality_surface_tangent_angle_tol_deg);
        if (!std::isfinite(config->quality_surface_tangent_angle_tol_deg) ||
            config->quality_surface_tangent_angle_tol_deg <= 0.0) {
            config->quality_surface_tangent_angle_tol_deg =
                MeritConfig{}.quality_surface_tangent_angle_tol_deg;
        }
        config->quality_conic_coefficient_tol = root.at(
            "quality_conic_coefficient_tol").as_number(
                config->quality_conic_coefficient_tol);
        if (!std::isfinite(config->quality_conic_coefficient_tol) ||
            config->quality_conic_coefficient_tol <= 0.0) {
            config->quality_conic_coefficient_tol =
                MeritConfig{}.quality_conic_coefficient_tol;
        }
        config->quality_distortion_tol =
            root.at("quality_distortion_tol").as_number(config->quality_distortion_tol);
        config->distortion_model = canonical_distortion_model(
            root.at("distortion_model").as_string(config->distortion_model),
            config->distortion_model);
        config->quality_chief_ray_angle_tol =
            root.at("quality_chief_ray_angle_tol").as_number(config->quality_chief_ray_angle_tol);
        config->quality_lateral_color_tol =
            root.at("quality_lateral_color_tol").as_number(config->quality_lateral_color_tol);
        config->quality_axial_color_tol =
            root.at("quality_axial_color_tol").as_number(config->quality_axial_color_tol);
        if (!std::isfinite(config->quality_axial_color_tol) ||
            config->quality_axial_color_tol <= 0.0) {
            config->quality_axial_color_tol = MeritConfig{}.quality_axial_color_tol;
        }
        config->quality_spectral_spot_spread_tol =
            root.at("quality_spectral_spot_spread_tol").as_number(
                config->quality_spectral_spot_spread_tol);
        if (!std::isfinite(config->quality_spectral_spot_spread_tol) ||
            config->quality_spectral_spot_spread_tol <= 0.0) {
            config->quality_spectral_spot_spread_tol =
                MeritConfig{}.quality_spectral_spot_spread_tol;
        }
}
}  // namespace io_detail

}  // namespace optiland
