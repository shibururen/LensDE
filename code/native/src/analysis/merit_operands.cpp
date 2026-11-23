/*
 * Classifies operand names, required measurements and scalar operand penalties.
 * Measurement planning, explicit operand evaluation and merit grouping use these helpers.
 * Compatibility aliases, wavefront reference choices and upper/lower/range semantics
 * define how saved operand records map onto native optical measurements.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
bool is_worst_wavelength_spot_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "worst_wavelength_spot" ||
           name == "worst_wavelength_rms_spot" ||
           name == "max_monochromatic_spot" ||
           name == "max_monochromatic_rms_spot";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_spectral_spot_spread_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "spectral_spot_spread" ||
           name == "spectral_rms_spread" ||
           name == "wavelength_spot_spread" ||
           name == "chromatic_spot_spread";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_relative_illumination_proxy_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "relative_illumination_proxy" ||
           name == "relative_illumination" ||
           name == "ri_proxy";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_vignette_meridional_excess_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "vignette_meridional_excess" ||
           name == "vignetting_meridional_excess" ||
           name == "fvcy_fvdy_excess";
}
}  // namespace analysis_detail

namespace analysis_detail {
int chromatic_quality_family(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    if (name == "tca" || name == "lca" ||
        name.find("lateral_color") != std::string::npos ||
        name.find("lateral_chromatic") != std::string::npos ||
        name.find("transverse_chromatic") != std::string::npos) {
        return 0;
    }
    if (name.find("axial_color") != std::string::npos) {
        return 1;
    }
    if (is_worst_wavelength_spot_operand_name(name)) {
        return 2;
    }
    if (is_spectral_spot_spread_operand_name(name)) {
        return 3;
    }
    return -1;
}
}  // namespace analysis_detail

namespace analysis_detail {
double operand_value(const LensSystem& system, const Score& score, const MeritOperand& operand) {
    const std::string name = normalized_name(operand.name);
    if (is_relative_illumination_proxy_operand_name(name)) {
        return operand.index < score.relative_illumination_proxy_by_field.size()
                   ? score.relative_illumination_proxy_by_field[operand.index]
                   : std::numeric_limits<double>::quiet_NaN();
    }
    if (is_vignette_meridional_excess_operand_name(name)) {
        if (operand.index >= system.fields.size()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const Field& field = system.fields[operand.index];
        return std::max(0.0, std::abs(field.fvcy) - field.fvdy);
    }
    if (name == "efl" || name == "effective_focal_length") {
        return score.efl;
    }
    if (name == "ttl" || name == "total_track" || name == "track") {
        return score.ttl;
    }
    if (name == "bfl" || name == "back_focal_length") {
        return score.bfl;
    }
    if (name == "working_f_number" || name == "working_fno" ||
        name == "working_f_num" || name == "wfno" ||
        name == "image_space_working_f_number") {
        return score.working_f_number;
    }
    if (name == "spot" || name == "mean_spot" || name == "rms_spot" || name == "mean_rms_spot") {
        return score.mean_spot;
    }
    if (name == "max_spot" || name == "max_rms_spot") {
        return score.max_spot;
    }
    if (is_worst_wavelength_spot_operand_name(name)) {
        return score.max_worst_wavelength_spot;
    }
    if (is_spectral_spot_spread_operand_name(name)) {
        return score.max_spectral_spot_spread;
    }
    if (is_single_ray_spot_tail_operand_name(name)) {
        return score.max_single_ray_spot;
    }
    if (is_single_ray_spot_guard_operand_name(name)) {
        return score.max_single_ray_spot_ratio;
    }
    if (name == "wavefront" || name == "rms_wavefront" ||
        name == "wavefront_rms" || name == "mean_wavefront" ||
        name == "mean_rms_wavefront" || name == "opd" ||
        name == "rms_opd" || name == "opd_rms" || name == "mean_opd_rms") {
        return score.mean_wavefront_rms;
    }
    if (name == "max_wavefront" || name == "max_rms_wavefront" ||
        name == "max_wavefront_rms" || name == "max_opd" ||
        name == "max_rms_opd" || name == "max_opd_rms") {
        return score.max_wavefront_rms;
    }
    if (is_single_ray_wavefront_guard_operand_name(name)) {
        return score.max_single_ray_wavefront_ratio;
    }
    if (name == "distortion" || name == "max_distortion" ||
        name == "maximum_distortion") {
        return score.max_distortion;
    }
    if (name == "distortion_percent" || name == "max_distortion_percent" ||
        name == "maximum_distortion_percent") {
        return score.max_distortion * 100.0;
    }
    if (name == "chief_ray_angle" || name == "chief_ray_incidence_angle" ||
        name == "max_chief_ray_angle" || name == "max_chief_ray_incidence_angle" ||
        name == "cra" || name == "max_cra") {
        return score.max_chief_ray_angle;
    }
    if (name == "lateral_color" || name == "max_lateral_color" ||
        name == "lateral_chromatic_aberration" ||
        name == "transverse_chromatic_aberration" ||
        name == "tca" || name == "lca") {
        return score.lateral_color;
    }
    if (name == "axial_color" || name == "paraxial_axial_color" ||
        name == "axial_color_focus_shift" ||
        name == "paraxial_axial_color_focus_shift") {
        return score.paraxial_axial_color_focus_shift_mm;
    }
    if (name == "min_edge" || name == "minimum_edge_thickness") {
        return score.min_edge;
    }
    return surface_constraint_value(system, name, operand.index, operand.term);
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_rms_spot_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "spot" ||
           name == "mean_spot" ||
           name == "rms_spot" ||
           name == "mean_rms_spot";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_rms_wavefront_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "wavefront" ||
           name == "rms_wavefront" ||
           name == "wavefront_rms" ||
           name == "mean_wavefront" ||
           name == "mean_rms_wavefront" ||
           name == "opd" ||
           name == "rms_opd" ||
           name == "opd_rms" ||
           name == "mean_opd_rms";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_single_ray_spot_guard_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "single_ray_spot_guard" ||
           name == "single_ray_spot" ||
           name == "max_single_ray_spot" ||
           name == "max_ray_spot" ||
           name == "ray_spot_guard" ||
           name == "spot_ray_guard";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_single_ray_spot_tail_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "single_ray_spot_tail" ||
           name == "absolute_single_ray_spot_tail" ||
           name == "max_single_ray_spot_absolute";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_single_ray_wavefront_guard_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return name == "single_ray_wavefront_guard" ||
           name == "single_ray_wavefront" ||
           name == "max_single_ray_wavefront" ||
           name == "max_ray_wavefront" ||
           name == "single_ray_opd_guard" ||
           name == "max_single_ray_opd" ||
           name == "max_ray_opd" ||
           name == "ray_wavefront_guard" ||
           name == "wavefront_ray_guard";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_wavefront_operand_name(const std::string& raw_name) {
    const std::string name = normalized_name(raw_name);
    return is_rms_wavefront_operand_name(name) ||
           is_single_ray_wavefront_guard_operand_name(name) ||
           name == "max_wavefront" ||
           name == "max_rms_wavefront" ||
           name == "max_wavefront_rms" ||
           name == "max_opd" ||
           name == "max_rms_opd" ||
           name == "max_opd_rms";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool merit_needs_wavefront(const MeritConfig& config) {
    return std::any_of(
        config.operands.begin(),
        config.operands.end(),
        [](const MeritOperand& operand) {
            return is_wavefront_operand_name(operand.name);
        });
}
}  // namespace analysis_detail

namespace analysis_detail {
bool wavefront_uses_centroid_reference(const MeritConfig& config) {
    const std::string name = normalized_name(config.wavefront_reference);
    return name == "centroid" ||
           name == "centroid_sphere" ||
           name == "centroid_reference" ||
           name == "centroid_reference_sphere";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool operand_needs_chief_ray_metrics(const MeritOperand& operand) {
    const std::string name = normalized_name(operand.name);
    return name == "distortion" ||
           name == "max_distortion" ||
           name == "maximum_distortion" ||
           name == "distortion_percent" ||
           name == "max_distortion_percent" ||
           name == "maximum_distortion_percent" ||
           name == "chief_ray_angle" ||
           name == "chief_ray_incidence_angle" ||
           name == "max_chief_ray_angle" ||
           name == "max_chief_ray_incidence_angle" ||
           name == "cra" ||
           name == "max_cra" ||
           name == "lateral_color" ||
           name == "max_lateral_color" ||
           name == "lateral_chromatic_aberration" ||
           name == "transverse_chromatic_aberration" ||
           name == "tca" ||
           name == "lca";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool merit_needs_chief_ray_metrics(const MeritConfig& config) {
    const std::string model = normalized_name(config.merit_model);
    if (model == "unified_trace_gate" ||
        model == "trace_guarded_adaptive" ||
        model == "unified_adaptive" ||
        model == "trace_guarded" ||
        model == "adaptive_quality" ||
        model == "quality" ||
        model == "qgso" ||
        model == "qgso_like" ||
        model == "auto") {
        return true;
    }
    return std::any_of(config.operands.begin(), config.operands.end(), operand_needs_chief_ray_metrics);
}
}  // namespace analysis_detail

namespace analysis_detail {
bool ray_aiming_model_reuse_safe(RayAimingMode mode) {
    return mode == RayAimingMode::CalibratedParaxial ||
           mode == RayAimingMode::ThreeStep;
}
}  // namespace analysis_detail

namespace analysis_detail {
double operand_penalty(double value, const MeritOperand& operand) {
    const std::string mode = normalized_name(operand.mode);
    const double weight = scoring_weight(operand.weight);
    if (mode == "minimize" || mode == "minimum") {
        if (!std::isfinite(value)) {
            return weight * 1.0e12;
        }
        return weight * value * value;
    }
    if (mode == "less" || mode == "less_than" || mode == "upper" ||
        mode == "max" || mode == "le" || mode == "lte") {
        const double upper = std::isfinite(operand.upper) ? operand.upper : operand.target;
        return bound_penalty(value, -kInf, upper, operand.weight);
    }
    if (mode == "greater" || mode == "greater_than" || mode == "lower" ||
        mode == "min" || mode == "ge" || mode == "gte") {
        const double lower = std::isfinite(operand.lower) ? operand.lower : operand.target;
        return bound_penalty(value, lower, kInf, operand.weight);
    }
    if (mode == "range" || mode == "between" || mode == "interval") {
        return bound_penalty(value, operand.lower, operand.upper, operand.weight);
    }
    return weight * square_error(value, operand.target);
}
}  // namespace analysis_detail

}  // namespace optiland
