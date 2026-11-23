#pragma once
#include "../prelude.cuh"
#include "runtime_options.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"

namespace optiland {
namespace {
struct ConfigureMeritOperandsInputs {
    const MeritConfig& merit;
    DeviceConfig& cfg;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <class Inputs>
inline native_detail::PhaseOutcome configure_merit_operands(Inputs& inputs) {
    auto& merit = inputs.merit;
    auto& cfg = inputs.cfg;
    if (merit.operands.empty()) {
        cfg.operand_rms_spot = 1;
        cfg.operand_max_spot = 0;
        cfg.operand_worst_wavelength_spot = 0;
        cfg.operand_spectral_spot_spread = 0;
        cfg.operand_single_ray_spot_guard = 0;
        cfg.operand_single_ray_spot_tail =
            merit.unified_image_single_ray_spot_tail_weight > 0.0 ? 1 : 0;
        cfg.single_ray_spot_tail_weight =
            std::max(0.0, merit.unified_image_single_ray_spot_tail_weight);
        cfg.operand_rms_wavefront = 0;
        cfg.operand_single_ray_wavefront_guard = 0;
        cfg.operand_efl = std::abs(merit.target_efl) > 1.0e-12 ? 1 : 0;
        cfg.operand_ttl = merit.target_ttl > 0.0 ? 1 : 0;
        cfg.operand_bfl = merit.target_bfl > 0.0 ? 1 : 0;
        cfg.operand_working_f_number = 0;
        cfg.operand_max_distortion = 0;
        cfg.operand_max_chief_ray_angle = 0;
        cfg.operand_lateral_color = 0;
        cfg.operand_axial_color = 0;
    } else {
        cfg.operand_rms_spot = 0;
        cfg.operand_max_spot = 0;
        cfg.operand_worst_wavelength_spot = 0;
        cfg.operand_spectral_spot_spread = 0;
        cfg.operand_single_ray_spot_guard = 0;
        cfg.operand_single_ray_spot_tail = 0;
        cfg.single_ray_spot_tail_weight = 0.0;
        cfg.operand_rms_wavefront = 0;
        cfg.operand_single_ray_wavefront_guard = 0;
        cfg.operand_efl = 0;
        cfg.operand_ttl = 0;
        cfg.operand_bfl = 0;
        cfg.operand_working_f_number = 0;
        cfg.operand_max_distortion = 0;
        cfg.operand_max_chief_ray_angle = 0;
        cfg.operand_lateral_color = 0;
        cfg.operand_axial_color = 0;
        for (const MeritOperand& operand : merit.operands) {
            const std::string name = normalized_token(operand.name);
            const bool enabled =
                std::isfinite(operand.weight) && operand.weight > 0.0;
            if (name == "stop_air_edge_thickness") {
                cfg.stop_air_edge_surface = static_cast<int>(operand.index);
                cfg.stop_air_edge_lower = operand.lower;
                cfg.stop_air_edge_weight = enabled ? operand.weight : 0.0;
            }
            if (name == "rms_spot" || name == "spot" || name == "spot_rms") {
                cfg.operand_rms_spot = enabled ? 1 : 0;
                if (std::isfinite(operand.target)) {
                    cfg.spot_target = operand.target;
                }
            } else if (name == "max_spot" || name == "max_rms_spot") {
                cfg.operand_max_spot = enabled ? 1 : 0;
            } else if (name == "worst_wavelength_spot" ||
                       name == "worst_wavelength_rms_spot" ||
                       name == "max_monochromatic_spot" ||
                       name == "max_monochromatic_rms_spot") {
                cfg.operand_worst_wavelength_spot = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.worst_wavelength_spot_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.worst_wavelength_spot_upper = operand.target;
                }
            } else if (name == "spectral_spot_spread" ||
                       name == "spectral_rms_spread" ||
                       name == "wavelength_spot_spread" ||
                       name == "chromatic_spot_spread") {
                cfg.operand_spectral_spot_spread = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.spectral_spot_spread_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.spectral_spot_spread_upper = operand.target;
                }
            } else if (name == "single_ray_spot_tail" ||
                       name == "absolute_single_ray_spot_tail" ||
                       name == "max_single_ray_spot_absolute") {
                cfg.operand_single_ray_spot_tail = enabled ? 1 : 0;
                cfg.single_ray_spot_tail_weight =
                    enabled ? std::abs(operand.weight) : 0.0;
            } else if (name == "single_ray_spot_guard" ||
                       name == "single_ray_spot" ||
                       name == "max_single_ray_spot" ||
                       name == "max_ray_spot" ||
                       name == "ray_spot_guard" ||
                       name == "spot_ray_guard") {
                cfg.operand_single_ray_spot_guard = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.single_ray_spot_guard_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.single_ray_spot_guard_upper = operand.target;
                }
            } else if (name == "rms_wavefront" || name == "wavefront" ||
                       name == "wavefront_rms" || name == "mean_wavefront" ||
                       name == "mean_rms_wavefront" || name == "opd" ||
                       name == "rms_opd" || name == "opd_rms" ||
                       name == "mean_opd_rms") {
                cfg.operand_rms_wavefront = enabled ? 1 : 0;
            } else if (name == "single_ray_wavefront_guard" ||
                       name == "single_ray_wavefront" ||
                       name == "max_single_ray_wavefront" ||
                       name == "max_ray_wavefront" ||
                       name == "single_ray_opd_guard" ||
                       name == "max_single_ray_opd" ||
                       name == "max_ray_opd" ||
                       name == "ray_wavefront_guard" ||
                       name == "wavefront_ray_guard") {
                cfg.operand_single_ray_wavefront_guard = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.single_ray_wavefront_guard_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.single_ray_wavefront_guard_upper = operand.target;
                }
            } else if (name == "efl" || name == "effective_focal_length") {
                cfg.operand_efl = enabled ? 1 : 0;
                if (std::isfinite(operand.target)) {
                    cfg.target_efl = operand.target;
                }
            } else if (name == "ttl" || name == "total_track" || name == "track") {
                cfg.operand_ttl = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.ttl_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.ttl_upper = operand.target;
                }
            } else if (name == "bfl" || name == "back_focal_length") {
                cfg.operand_bfl = enabled ? 1 : 0;
                if (std::isfinite(operand.lower)) {
                    cfg.bfl_lower = operand.lower;
                } else if (std::isfinite(operand.target)) {
                    cfg.bfl_lower = operand.target;
                }
            } else if (name == "relative_illumination_proxy" ||
                       name == "relative_illumination" ||
                       name == "ri_proxy") {
                if (operand.index < kMaxDeviceFields) {
                    cfg.relative_illumination_operand_weight[operand.index] =
                        enabled ? std::abs(operand.weight) : 0.0;
                    cfg.relative_illumination_operand_lower[operand.index] =
                        std::isfinite(operand.lower)
                            ? operand.lower
                            : operand.target;
                }
            } else if (name == "working_f_number" ||
                       name == "working_fno" ||
                       name == "working_f_num" || name == "wfno" ||
                       name == "image_space_working_f_number") {
                cfg.operand_working_f_number = enabled ? 1 : 0;
                cfg.working_f_number_target_mode =
                    normalized_token(operand.mode) == "target" ? 1 : 0;
                cfg.working_f_number_target = operand.target;
                if (std::isfinite(operand.upper)) {
                    cfg.working_f_number_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.working_f_number_upper = operand.target;
                }
            } else if (name == "max_distortion" || name == "distortion") {
                cfg.operand_max_distortion = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.max_distortion_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.max_distortion_upper = operand.target;
                }
            } else if (name == "max_chief_ray_angle" ||
                       name == "chief_ray_angle" ||
                       name == "max_cra" ||
                       name == "cra") {
                cfg.operand_max_chief_ray_angle = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.max_chief_ray_angle_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.max_chief_ray_angle_upper = operand.target;
                }
            } else if (name == "lateral_color" || name == "tca" || name == "lca") {
                cfg.operand_lateral_color = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.lateral_color_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.lateral_color_upper = operand.target;
                }
            } else if (name == "axial_color" ||
                       name == "paraxial_axial_color" ||
                       name == "axial_color_focus_shift" ||
                       name == "paraxial_axial_color_focus_shift") {
                cfg.operand_axial_color = enabled ? 1 : 0;
                if (std::isfinite(operand.upper)) {
                    cfg.axial_color_upper = operand.upper;
                } else if (std::isfinite(operand.target)) {
                    cfg.axial_color_upper = operand.target;
                }
            }
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace
} // namespace optiland
