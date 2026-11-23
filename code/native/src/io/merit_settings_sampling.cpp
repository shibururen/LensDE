/*
 * Loads pupil/ray sampling, CODEV profiles, field weighting and dynamic-aperture settings.
 * load_merit_json calls this reader before trace-group and image-weight configuration.
 * Stored sampling seeds, distribution aliases and wavefront-reference choices retain
 * the sample ordering and aperture policy used by the native analysis routines.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_sampling(const Json& root, MeritConfig* config) {
config->num_rays = static_cast<int>(root.at("num_rays").as_number(config->num_rays));
        config->pupil_arms = static_cast<int>(
            root.at("pupil_arms").as_number(root.at("arms").as_number(config->pupil_arms)));
        config->elevated_field_sampling_min_radius =
            root.at("elevated_field_sampling_min_radius").as_number(
                config->elevated_field_sampling_min_radius);
        config->elevated_field_num_rays = static_cast<int>(
            root.at("elevated_field_num_rays").as_number(
                config->elevated_field_num_rays));
        config->elevated_field_pupil_arms = static_cast<int>(
            root.at("elevated_field_pupil_arms").as_number(
                config->elevated_field_pupil_arms));
        if (!std::isfinite(config->elevated_field_sampling_min_radius)) {
            config->elevated_field_sampling_min_radius = 2.0;
        }
        config->elevated_field_num_rays =
            std::max(0, config->elevated_field_num_rays);
        config->elevated_field_pupil_arms =
            std::max(0, config->elevated_field_pupil_arms);
        config->pupil_distribution = root.at("pupil_distribution").as_string(
            root.at("distribution").as_string(config->pupil_distribution));
        config->codev_sampling_profile =
            root.at("codev_sampling_profile").as_string(
                config->codev_sampling_profile);
        config->codev_ray_interval =
            root.at("codev_ray_interval").as_number(config->codev_ray_interval);
        config->codev_obscuration =
            root.at("codev_obscuration").as_number(config->codev_obscuration);
        config->codev_spot_aperture_weight =
            root.at("codev_spot_aperture_weight").as_number(
                config->codev_spot_aperture_weight);
        config->codev_wavefront_aperture_weight =
            root.at("codev_wavefront_aperture_weight").as_number(
                config->codev_wavefront_aperture_weight);
        config->codev_use_symmetry =
            root.at("codev_use_symmetry").as_bool(config->codev_use_symmetry);
        config->image_field_weight_mode =
            root.at("image_field_weight_mode").as_string(
                config->image_field_weight_mode);
        config->image_field_edge_weight =
            root.at("image_field_edge_weight").as_number(
                config->image_field_edge_weight);
        config->image_field_weight_exponent =
            root.at("image_field_weight_exponent").as_number(
                config->image_field_weight_exponent);
        if (!std::isfinite(config->codev_ray_interval) || config->codev_ray_interval < 0.0) {
            config->codev_ray_interval = 0.0;
        }
        config->codev_ray_interval = std::min(1.0, config->codev_ray_interval);
        if (!std::isfinite(config->codev_obscuration)) {
            config->codev_obscuration = 0.0;
        }
        config->codev_obscuration = std::clamp(config->codev_obscuration, 0.0, 1.0);
        if (!std::isfinite(config->codev_spot_aperture_weight)) {
            config->codev_spot_aperture_weight = MeritConfig{}.codev_spot_aperture_weight;
        }
        if (!std::isfinite(config->codev_wavefront_aperture_weight)) {
            config->codev_wavefront_aperture_weight =
                MeritConfig{}.codev_wavefront_aperture_weight;
        }
        const std::string field_weight_mode = normalized_name(config->image_field_weight_mode);
        config->image_field_weight_mode =
            field_weight_mode == "uniform" || field_weight_mode == "none" ||
                    field_weight_mode == "off" || field_weight_mode == "disabled"
                ? "uniform"
                : field_weight_mode == "center_double" ? "center_double" : "edge_power";
        if (!std::isfinite(config->image_field_edge_weight)) {
            config->image_field_edge_weight = MeritConfig{}.image_field_edge_weight;
        }
        config->image_field_edge_weight =
            std::max(1.0, config->image_field_edge_weight);
        if (!std::isfinite(config->image_field_weight_exponent)) {
            config->image_field_weight_exponent = MeritConfig{}.image_field_weight_exponent;
        }
        config->image_field_weight_exponent =
            std::max(0.1, config->image_field_weight_exponent);
        config->wavefront_reference = canonical_wavefront_reference(
            root.at("wavefront_reference").as_string(
                root.at("wavefront_strategy").as_string(
                    root.at("reference_sphere").as_string(config->wavefront_reference))),
            config->wavefront_reference);
        config->sampling_seed = static_cast<unsigned int>(
            std::max(0.0, root.at("sampling_seed").as_number(static_cast<double>(config->sampling_seed))));
        config->dynamic_apertures = root.at("dynamic_apertures").as_bool(config->dynamic_apertures);
        config->clear_aperture_rings =
            static_cast<int>(root.at("clear_aperture_rings").as_number(config->clear_aperture_rings));
        config->clear_aperture_arms =
            static_cast<int>(root.at("clear_aperture_arms").as_number(config->clear_aperture_arms));
        config->clear_aperture_margin =
            root.at("clear_aperture_margin").as_number(config->clear_aperture_margin);
        config->trace_policy = trace_failure_policy_from_string(
            root.at("trace_policy").as_string(trace_failure_policy_name(config->trace_policy)));
}
}  // namespace io_detail

}  // namespace optiland
