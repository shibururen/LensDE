/*
 * Loads image/first-order quality scales and the supervised relative-illumination floor.
 * load_merit_json supplies the current MeritConfig so omitted keys retain defaults.
 * Positive-finite scale fallbacks and field-index/radius supervision determine which
 * measured image and illumination values contribute to the configured quality merit.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_quality(const Json& root, MeritConfig* config) {
config->quality_spot_scale =
            root.at("quality_spot_scale").as_number(config->quality_spot_scale);
        config->quality_wavefront_scale =
            root.at("quality_wavefront_scale").as_number(config->quality_wavefront_scale);
        if (!std::isfinite(config->quality_wavefront_scale) ||
            config->quality_wavefront_scale <= 0.0) {
            config->quality_wavefront_scale = MeritConfig{}.quality_wavefront_scale;
        }
        config->quality_efl_rel_tol =
            root.at("quality_efl_rel_tol").as_number(config->quality_efl_rel_tol);
        config->quality_ttl_rel_tol =
            root.at("quality_ttl_rel_tol").as_number(config->quality_ttl_rel_tol);
        config->quality_bfl_rel_tol =
            root.at("quality_bfl_rel_tol").as_number(config->quality_bfl_rel_tol);
        config->quality_working_f_number_tol = root.at(
            "quality_working_f_number_tol").as_number(
                config->quality_working_f_number_tol);
        if (!std::isfinite(config->quality_working_f_number_tol) ||
            config->quality_working_f_number_tol <= 0.0) {
            config->quality_working_f_number_tol =
                MeritConfig{}.quality_working_f_number_tol;
        }
        config->quality_length_abs_tol =
            root.at("quality_length_abs_tol").as_number(config->quality_length_abs_tol);
        config->quality_edge_tol =
            root.at("quality_edge_tol").as_number(config->quality_edge_tol);
        config->quality_ratio_tol =
            root.at("quality_ratio_tol").as_number(config->quality_ratio_tol);
        config->quality_bound_tol =
            root.at("quality_bound_tol").as_number(config->quality_bound_tol);
        config->minimum_relative_illumination = root.at(
            "minimum_relative_illumination").as_number(
                config->minimum_relative_illumination);
        if (std::isfinite(config->minimum_relative_illumination) &&
            config->minimum_relative_illumination <= 0.0) {
            config->minimum_relative_illumination =
                std::numeric_limits<double>::quiet_NaN();
        } else if (std::isfinite(config->minimum_relative_illumination) &&
                   config->minimum_relative_illumination > 1.0) {
            throw std::runtime_error(
                "minimum_relative_illumination must not exceed 1");
        }
        config->minimum_relative_illumination_field_indices.clear();
        config->minimum_relative_illumination_field_radii.clear();
        const Json& ri_indices = root.at("minimum_relative_illumination_field_indices");
        const Json& ri_radii = root.at("minimum_relative_illumination_field_radii");
        if (!ri_indices.is_null() && !ri_radii.is_null()) {
            throw std::runtime_error("RI indices and reference radii are mutually exclusive");
        }
        for (const auto* selection : {&ri_indices, &ri_radii}) {
            if (selection->is_null()) continue;
            if (!selection->is_array() || selection->array.empty()) {
                throw std::runtime_error("RI field selector must be a non-empty array");
            }
            for (const Json& item : selection->array) {
                const double value = item.as_number(-1.0);
                if (!std::isfinite(value) || value < 0.0 || value > 1000000.0 ||
                    (selection == &ri_indices && value != std::floor(value))) {
                    throw std::runtime_error("invalid RI field selector value");
                }
                if (selection == &ri_indices) {
                    config->minimum_relative_illumination_field_indices.push_back(
                        static_cast<std::size_t>(value));
                } else {
                    config->minimum_relative_illumination_field_radii.push_back(value);
                }
            }
        }
        config->quality_relative_illumination_tol = root.at(
            "quality_relative_illumination_tol").as_number(
                config->quality_relative_illumination_tol);
}
}  // namespace io_detail

}  // namespace optiland
