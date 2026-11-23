/*
 * Defines contribution construction, category classification and merit reference scales.
 * Operand stages and final adaptive/unified aggregation share these weighting helpers.
 * Preserve canonical names, field-weight policies, tolerances and robust-loss formulas
 * because they connect stored configuration values to scalar and DLS residual units.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
double zemax_style_merit(double sum, double weight_sum) {
    if (!std::isfinite(sum)) {
        return std::numeric_limits<double>::infinity();
    }
    const double denominator = weight_sum > 0.0 ? weight_sum : 1.0;
    return std::sqrt(std::max(0.0, sum) / denominator);
}
}  // namespace analysis_detail

namespace analysis_detail {
MeritContribution make_contribution(
    const std::string& source,
    const std::string& name,
    const std::string& mode,
    double value,
    double target,
    double lower,
    double upper,
    double weight,
    double penalty,
    std::size_t index ,
    std::size_t term ) {
    MeritContribution contribution;
    contribution.source = source;
    contribution.name = name;
    contribution.mode = mode;
    contribution.index = index;
    contribution.term = term;
    contribution.value = value;
    contribution.target = target;
    contribution.lower = lower;
    contribution.upper = upper;
    contribution.weight = weight;
    contribution.penalty = penalty;
    return contribution;
}
}  // namespace analysis_detail

namespace analysis_detail {
double configured_image_field_weight(
    const LensSystem& system,
    const MeritConfig& config,
    std::size_t field_index) {
    if (field_index >= system.fields.size()) {
        return 1.0;
    }
    const std::string mode = normalized_name(config.image_field_weight_mode);
    if (mode == "uniform" || mode == "none" || mode == "off" || mode == "disabled") {
        return 1.0;
    }
    const Field& field = system.fields[field_index];
    if (mode == "center_double") {
        return std::hypot(field.hx, field.hy) <= 1.0e-9 ? 2.0 : 1.0;
    }
    const double radius = std::clamp(std::hypot(field.hx, field.hy), 0.0, 1.0);
    const double edge_weight =
        std::isfinite(config.image_field_edge_weight)
            ? std::max(1.0, config.image_field_edge_weight)
            : MeritConfig{}.image_field_edge_weight;
    const double exponent =
        std::isfinite(config.image_field_weight_exponent)
            ? std::max(0.1, config.image_field_weight_exponent)
            : MeritConfig{}.image_field_weight_exponent;
    return 1.0 + (edge_weight - 1.0) * std::pow(radius, exponent);
}
}  // namespace analysis_detail

namespace analysis_detail {
bool adaptive_quality_enabled(const MeritConfig& config) {
    const std::string model = normalized_name(config.merit_model);
    return model == "adaptive_quality" ||
           model == "quality" ||
           model == "qgso" ||
           model == "qgso_like" ||
           model == "auto";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool coverage_first_merit_enabled(const MeritConfig& config) {
    const std::string model = normalized_name(config.merit_model);
    return model == "coverage_first" ||
           model == "trace_first" ||
           model == "coverage_priority" ||
           model == "trace_priority";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool unified_trace_gate_enabled(const MeritConfig& config) {
    const std::string model = normalized_name(config.merit_model);
    return model == "unified_trace_gate" ||
           model == "trace_guarded_adaptive" ||
           model == "unified_adaptive" ||
           model == "trace_guarded";
}
}  // namespace analysis_detail

namespace analysis_detail {
double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}
}  // namespace analysis_detail

namespace analysis_detail {
double smoothstep(double value, double lo, double hi) {
    if (!(hi > lo)) {
        return value >= hi ? 1.0 : 0.0;
    }
    const double t = clamp01((value - lo) / (hi - lo));
    return t * t * (3.0 - 2.0 * t);
}
}  // namespace analysis_detail

namespace analysis_detail {
double pseudo_huber(double residual, double delta) {
    if (!std::isfinite(residual)) {
        return 1.0e12;
    }
    const double d = std::max(1.0e-9, std::abs(delta));
    const double scaled = residual / d;
    return d * d * (std::sqrt(1.0 + scaled * scaled) - 1.0);
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_upper_mode(const std::string& mode) {
    return mode == "less" || mode == "less_than" || mode == "upper" ||
           mode == "max" || mode == "le" || mode == "lte";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_lower_mode(const std::string& mode) {
    return mode == "greater" || mode == "greater_than" || mode == "lower" ||
           mode == "min" || mode == "ge" || mode == "gte";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_range_mode(const std::string& mode) {
    return mode == "range" || mode == "between" || mode == "interval";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_penalty_mode(const std::string& mode) {
    return mode == "penalty" || mode == "fail" || mode == "required";
}
}  // namespace analysis_detail

namespace analysis_detail {
double contribution_delta_for_unified(const MeritContribution& contribution) {
    const std::string mode = normalized_name(contribution.mode);
    if (is_penalty_mode(mode)) {
        const double weight = std::max(1.0e-12, scoring_weight(contribution.weight));
        return std::sqrt(std::max(0.0, contribution.penalty) / weight);
    }
    if (!std::isfinite(contribution.value)) {
        return 1.0e6;
    }
    if (mode == "minimize" || mode == "minimum") {
        return contribution.value;
    }
    if (is_upper_mode(mode)) {
        const double upper = std::isfinite(contribution.upper) ? contribution.upper : contribution.target;
        return std::isfinite(upper) ? std::max(0.0, contribution.value - upper) : 0.0;
    }
    if (is_lower_mode(mode)) {
        const double lower = std::isfinite(contribution.lower) ? contribution.lower : contribution.target;
        return std::isfinite(lower) ? std::max(0.0, lower - contribution.value) : 0.0;
    }
    if (is_range_mode(mode)) {
        if (std::isfinite(contribution.lower) && contribution.value < contribution.lower) {
            return contribution.lower - contribution.value;
        }
        if (std::isfinite(contribution.upper) && contribution.value > contribution.upper) {
            return contribution.value - contribution.upper;
        }
        return 0.0;
    }
    return std::isfinite(contribution.target) ? contribution.value - contribution.target : 0.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_trace_source(const MeritContribution& contribution) {
    return contribution.source == "trace" ||
           contribution.source == "trace_field" ||
           contribution.source == "failure" ||
           contribution.source == "unified_trace";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_image_quality_name(const std::string& name) {
    return name.find("spot") != std::string::npos ||
           name.find("wavefront") != std::string::npos ||
           name.find("opd") != std::string::npos ||
           name.find("distortion") != std::string::npos ||
           name == "cra" ||
           name == "max_cra" ||
           name.find("chief_ray_angle") != std::string::npos ||
           name.find("chief_ray_incidence_angle") != std::string::npos ||
           name == "tca" ||
           name == "lca" ||
           name.find("lateral_color") != std::string::npos ||
           name.find("lateral_chromatic") != std::string::npos ||
           name.find("transverse_chromatic") != std::string::npos ||
           name.find("axial_color") != std::string::npos;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_edge_like_manufacturing_constraint_name(const std::string& name) {
    return name == "min_edge" ||
           name == "minimum_edge_thickness" ||
           name.find("edge") != std::string::npos ||
           name.find("atc") != std::string::npos ||
           name.find("ate") != std::string::npos ||
           name.find("bl0") != std::string::npos ||
           name.find("blocking_ratio") != std::string::npos ||
           name.find("od_ratio") != std::string::npos;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_ratio_like_manufacturing_constraint_name(const std::string& name) {
    return name.find("atc") != std::string::npos ||
           name.find("ate") != std::string::npos ||
           name.find("bl0") != std::string::npos ||
           name.find("blocking_ratio") != std::string::npos ||
           name.find("od_ratio") != std::string::npos;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_surface_tangent_angle_constraint_name(const std::string& name) {
    return name.find("surface_tangent") != std::string::npos ||
           name.find("tangent_axis_angle") != std::string::npos;
}
}  // namespace analysis_detail

namespace analysis_detail {
MeritCategory classify_unified_contribution(const MeritContribution& contribution) {
    if (is_trace_source(contribution)) {
        return MeritCategory::Trace;
    }
    const std::string source = normalized_name(contribution.source);
    const std::string name = normalized_name(contribution.name);
    if (source == "structural_prior" || source == "pupil_order_prior") {
        return MeritCategory::Regularization;
    }
    if (is_image_quality_name(name)) {
        return MeritCategory::ImageQuality;
    }
    if (name == "efl" || name == "effective_focal_length" ||
        name == "ttl" || name == "total_track" || name == "track" ||
        name == "bfl" || name == "back_focal_length" ||
        name == "working_f_number" || name == "working_fno" ||
        name == "working_f_num" || name == "wfno" ||
        name == "image_space_working_f_number") {
        return MeritCategory::FirstOrder;
    }
    if (source == "default_constraint" || source == "bound") {
        return MeritCategory::Constraint;
    }
    if (is_relative_illumination_proxy_operand_name(name)) {
        return MeritCategory::Constraint;
    }
    if (is_vignette_meridional_excess_operand_name(name)) {
        return MeritCategory::Constraint;
    }
    if (is_vignetting_boundary_support_operand_name(name)) {
        return MeritCategory::Constraint;
    }
    if (is_edge_like_manufacturing_constraint_name(name) ||
        name.find("thickness") != std::string::npos ||
        name.find("air") != std::string::npos ||
        name.find("gap") != std::string::npos ||
        name.find("aperture") != std::string::npos ||
        name.find("radius") != std::string::npos ||
        name.find("curvature") != std::string::npos ||
        name.find("conic") != std::string::npos ||
        name.find("asphere") != std::string::npos ||
        name.find("material") != std::string::npos) {
        return MeritCategory::Constraint;
    }
    if (name.find("image_height") != std::string::npos) {
        return MeritCategory::FirstOrder;
    }
    return MeritCategory::Other;
}
}  // namespace analysis_detail

namespace analysis_detail {
double unified_tolerance_for_contribution(
    const MeritContribution& contribution,
    MeritCategory category,
    const MeritConfig& config) {
    const std::string name = normalized_name(contribution.name);
    if (category == MeritCategory::Trace) {
        if (name.find("coverage") != std::string::npos) {
            return std::max(1.0e-9, config.unified_coverage_tol);
        }
        if (name.find("progress") != std::string::npos) {
            return std::max(1.0e-9, config.unified_progress_tol);
        }
        return std::max(1.0e-9, config.unified_failed_weight_tol);
    }
    if (category == MeritCategory::Constraint) {
        if (name == "stop_air_edge_thickness") {
            return std::max(1.0e-9, config.quality_edge_tol);
        }
        if (is_relative_illumination_proxy_operand_name(name)) {
            return std::max(
                1.0e-9,
                config.quality_relative_illumination_tol);
        }
        if (name == "conic" || name.find("conic_coefficient") != std::string::npos) {
            return std::max(1.0e-9, config.quality_conic_coefficient_tol);
        }
        if (is_surface_tangent_angle_constraint_name(name)) {
            return std::max(
                1.0e-9,
                std::abs(config.quality_surface_tangent_angle_tol_deg));
        }
        if (name == "ttl" || name == "total_track" || name == "track") {
            return std::max(1.0e-9, config.unified_ttl_tol);
        }
        if (name == "bfl" || name == "back_focal_length") {
            return std::max(1.0e-9, config.unified_bfl_tol);
        }
        if (is_edge_like_manufacturing_constraint_name(name)) {
            return std::max(1.0e-9, config.unified_edge_tol);
        }
        if (name.find("air") != std::string::npos || name.find("gap") != std::string::npos) {
            return std::max(1.0e-9, config.unified_air_gap_tol);
        }
        if (name.find("center") != std::string::npos ||
            name == "thickness" ||
            name.find("thickness") != std::string::npos) {
            return std::max(1.0e-9, config.unified_center_thickness_tol);
        }
        return std::max(1.0e-9, config.quality_bound_tol);
    }
    if (category == MeritCategory::FirstOrder) {
        const double abs_floor = std::max(1.0e-9, config.unified_length_abs_tol);
        if (name == "efl" || name == "effective_focal_length") {
            const double ref = contribution_reference_for_quality(contribution, 1.0);
            return std::max(abs_floor, std::abs(config.unified_efl_rel_tol) * ref);
        }
        if (name == "ttl" || name == "total_track" || name == "track") {
            const double ref = contribution_reference_for_quality(contribution, 1.0);
            return std::max(abs_floor, std::abs(config.quality_ttl_rel_tol) * ref);
        }
        if (name == "bfl" || name == "back_focal_length") {
            const double ref = contribution_reference_for_quality(contribution, 1.0);
            return std::max(abs_floor, std::abs(config.quality_bfl_rel_tol) * ref);
        }
        if (name == "working_f_number" || name == "working_fno" ||
            name == "working_f_num" || name == "wfno" ||
            name == "image_space_working_f_number") {
            return std::max(1.0e-9, std::abs(config.quality_working_f_number_tol));
        }
        return abs_floor;
    }
    if (category == MeritCategory::ImageQuality) {
        if (is_single_ray_spot_guard_operand_name(name) ||
            is_single_ray_wavefront_guard_operand_name(name)) {
            return 1.0;
        }
        if (is_spectral_spot_spread_operand_name(name)) {
            return std::max(1.0e-9, config.quality_spectral_spot_spread_tol);
        }
        if (name.find("spot") != std::string::npos) {
            return std::max({1.0e-9, config.spot_target, config.quality_spot_scale});
        }
        if (name.find("wavefront") != std::string::npos ||
            name.find("opd") != std::string::npos) {
            const double wavefront_scale =
                std::isfinite(config.quality_wavefront_scale) &&
                        config.quality_wavefront_scale > 0.0
                    ? config.quality_wavefront_scale
                    : MeritConfig{}.quality_wavefront_scale;
            return std::max(1.0e-9, wavefront_scale);
        }
        if (name.find("distortion") != std::string::npos) {
            return std::max(1.0e-9, config.quality_distortion_tol);
        }
        if (name == "cra" || name == "max_cra" ||
            name.find("chief_ray_angle") != std::string::npos ||
            name.find("chief_ray_incidence_angle") != std::string::npos) {
            return std::max(1.0e-9, config.quality_chief_ray_angle_tol);
        }
        if (name == "tca" || name == "lca" ||
            name.find("lateral_color") != std::string::npos ||
            name.find("lateral_chromatic") != std::string::npos ||
            name.find("transverse_chromatic") != std::string::npos) {
            return std::max(1.0e-9, config.quality_lateral_color_tol);
        }
        if (name.find("axial_color") != std::string::npos) {
            return std::max(1.0e-9, config.quality_axial_color_tol);
        }
    }
    return std::max(1.0e-9, config.quality_bound_tol);
}
}  // namespace analysis_detail

namespace analysis_detail {
double unified_base_weight_for_contribution(
    const MeritContribution& contribution,
    MeritCategory category,
    const MeritConfig& config,
    const LensSystem& system) {
    if (std::isfinite(contribution.weight) && contribution.weight <= 0.0 &&
        contribution.source != "default_constraint") {
        return 0.0;
    }
    const std::string name = normalized_name(contribution.name);
    if (name == "stop_air_edge_thickness") {
        return scoring_weight(contribution.weight);
    }
    if (category == MeritCategory::Trace) {
        if (name == "system_coverage") {
            return config.unified_trace_system_weight;
        }
        if (name == "min_field_coverage" || name == "field_coverage") {
            return config.unified_trace_min_field_weight;
        }
        if (name == "min_trace_progress" || name == "trace_progress") {
            return config.unified_trace_progress_weight;
        }
        if (name == "min_chief_progress" || name == "chief_progress") {
            return config.unified_trace_chief_progress_weight;
        }
        if (name == "failed_ray_weight" || name == "ray_failure") {
            return config.unified_trace_failed_weight;
        }
        return config.unified_trace_failure_type_weight;
    }
    if (category == MeritCategory::Constraint) {
        if (is_relative_illumination_proxy_operand_name(name)) {
            return std::max(0.0, std::abs(contribution.weight));
        }
        if (is_vignette_meridional_excess_operand_name(name)) {
            return std::max(0.0, std::abs(contribution.weight));
        }
        if (is_vignetting_boundary_support_operand_name(name)) {
            return std::max(0.0, std::abs(contribution.weight));
        }
        if (name == "ttl" || name == "total_track" || name == "track") {
            return config.unified_constraint_ttl_weight;
        }
        if (name == "bfl" || name == "back_focal_length") {
            return config.unified_constraint_bfl_weight;
        }
        if (name.find("air") != std::string::npos || name.find("gap") != std::string::npos) {
            return config.unified_constraint_air_gap_weight;
        }
        if (is_edge_like_manufacturing_constraint_name(name)) {
            return config.unified_constraint_edge_weight;
        }
        if (name.find("aperture") != std::string::npos) {
            return config.unified_constraint_aperture_weight;
        }
        if (name.find("radius") != std::string::npos ||
            name.find("curvature") != std::string::npos ||
            name.find("conic") != std::string::npos ||
            name.find("asphere") != std::string::npos) {
            return config.unified_constraint_curvature_weight;
        }
        return config.unified_constraint_center_weight;
    }
    if (category == MeritCategory::FirstOrder) {
        if (name == "efl" || name == "effective_focal_length") {
            return config.unified_first_order_efl_weight;
        }
        if (name == "ttl" || name == "total_track" || name == "track") {
            return config.unified_first_order_ttl_weight;
        }
        if (name == "bfl" || name == "back_focal_length") {
            return config.unified_first_order_bfl_weight;
        }
        if (name == "working_f_number" || name == "working_fno" ||
            name == "working_f_num" || name == "wfno" ||
            name == "image_space_working_f_number") {
            return config.unified_first_order_working_f_number_weight;
        }
        if (name.find("image_height") != std::string::npos) {
            return config.unified_first_order_image_height_weight;
        }
    }
    if (category == MeritCategory::ImageQuality) {
        const double field_weight =
            contribution.source == "operand_field" || contribution.source == "legacy_field"
                ? configured_image_field_weight(system, config, contribution.index)
                : 1.0;
        if (is_single_ray_spot_tail_operand_name(name)) {
            // add_contribution() has already folded the configured field weight
            // into operand-field contribution.weight.  Do not apply it twice.
            return scoring_weight(contribution.weight);
        }
        if (is_worst_wavelength_spot_operand_name(name)) {
            return field_weight * config.unified_image_chromatic_weight;
        }
        if (name == "max_spot" || name == "max_rms_spot") {
            return field_weight * config.unified_image_max_spot_weight;
        }
        if (is_spectral_spot_spread_operand_name(name)) {
            return field_weight * config.unified_image_chromatic_weight;
        }
        if (name.find("spot") != std::string::npos) {
            return field_weight * config.unified_image_mean_spot_weight;
        }
        if (name.find("wavefront") != std::string::npos ||
            name.find("opd") != std::string::npos) {
            return field_weight * config.unified_image_wavefront_weight;
        }
        if (name.find("distortion") != std::string::npos) {
            return field_weight * config.unified_image_distortion_weight;
        }
        if (name == "cra" || name == "max_cra" ||
            name.find("chief_ray_angle") != std::string::npos ||
            name.find("chief_ray_incidence_angle") != std::string::npos) {
            return field_weight * config.unified_image_cra_weight;
        }
        if (name == "tca" || name == "lca" ||
            name.find("lateral_color") != std::string::npos ||
            name.find("lateral_chromatic") != std::string::npos ||
            name.find("transverse_chromatic") != std::string::npos) {
            return field_weight * config.unified_image_chromatic_weight;
        }
        if (name.find("axial_color") != std::string::npos) {
            return field_weight * config.unified_image_chromatic_weight;
        }
        return field_weight * config.unified_image_field_balance_weight;
    }
    return scoring_weight(contribution.weight);
}
}  // namespace analysis_detail

namespace analysis_detail {
double group_weight_for_category(MeritCategory category, const MeritConfig& config) {
    switch (category) {
        case MeritCategory::Trace:
            return config.group_weight_trace;
        case MeritCategory::Constraint:
            return config.group_weight_constraint;
        case MeritCategory::FirstOrder:
            return config.group_weight_first_order;
        case MeritCategory::ImageQuality:
            return config.group_weight_image;
        case MeritCategory::Regularization:
        case MeritCategory::Other:
            return 1.0;
    }
    return 1.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_spot_contribution(const MeritContribution& contribution) {
    return is_rms_spot_operand_name(contribution.name) &&
           (contribution.source == "operand_field" || contribution.source == "legacy_field");
}
}  // namespace analysis_detail

namespace analysis_detail {
double contribution_delta_for_quality(const MeritContribution& contribution) {
    if (!std::isfinite(contribution.value)) {
        return std::numeric_limits<double>::infinity();
    }
    const std::string mode = normalized_name(contribution.mode);
    if (mode == "minimize" || mode == "minimum") {
        return std::max(0.0, contribution.value);
    }
    if (mode == "less" || mode == "less_than" || mode == "upper" ||
        mode == "max" || mode == "le" || mode == "lte") {
        const double upper = std::isfinite(contribution.upper) ? contribution.upper : contribution.target;
        return std::isfinite(upper) ? std::max(0.0, contribution.value - upper) : 0.0;
    }
    if (mode == "greater" || mode == "greater_than" || mode == "lower" ||
        mode == "min" || mode == "ge" || mode == "gte") {
        const double lower = std::isfinite(contribution.lower) ? contribution.lower : contribution.target;
        return std::isfinite(lower) ? std::max(0.0, lower - contribution.value) : 0.0;
    }
    if (mode == "range" || mode == "between" || mode == "interval") {
        if (std::isfinite(contribution.lower) && contribution.value < contribution.lower) {
            return contribution.lower - contribution.value;
        }
        if (std::isfinite(contribution.upper) && contribution.value > contribution.upper) {
            return contribution.value - contribution.upper;
        }
        return 0.0;
    }
    return std::isfinite(contribution.target)
               ? std::abs(contribution.value - contribution.target)
               : 0.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
double contribution_reference_for_quality(const MeritContribution& contribution, double fallback) {
    if (std::isfinite(contribution.target) && std::abs(contribution.target) > kEps) {
        return std::abs(contribution.target);
    }
    if (std::isfinite(contribution.lower) && std::abs(contribution.lower) > kEps) {
        return std::abs(contribution.lower);
    }
    if (std::isfinite(contribution.upper) && std::abs(contribution.upper) > kEps) {
        return std::abs(contribution.upper);
    }
    if (std::isfinite(contribution.value) && std::abs(contribution.value) > kEps) {
        return std::abs(contribution.value);
    }
    return fallback;
}
}  // namespace analysis_detail

namespace analysis_detail {
double quality_tolerance_for_contribution(
    const MeritContribution& contribution,
    const MeritConfig& config) {
    const std::string name = normalized_name(contribution.name);
    if (is_relative_illumination_proxy_operand_name(name)) {
        return std::max(
            1.0e-9,
            config.quality_relative_illumination_tol);
    }
    if (is_vignette_meridional_excess_operand_name(name)) {
        return std::max(
            1.0e-9,
            config.quality_relative_illumination_tol);
    }
    if (is_vignetting_boundary_support_operand_name(name)) {
        const double aperture_margin =
            std::max(1.0, config.effective_clear_aperture_margin());
        const double materialization_gap = 1.0 - 1.0 / aperture_margin;
        return std::max({
            1.0e-9,
            config.vignetting_boundary_support_tolerance,
            materialization_gap});
    }
    const double abs_floor = std::max(1.0e-9, config.quality_length_abs_tol);
    if (name == "efl" || name == "effective_focal_length") {
        const double ref = contribution_reference_for_quality(contribution, 1.0);
        return std::max(abs_floor, std::abs(config.quality_efl_rel_tol) * ref);
    }
    if (name == "ttl" || name == "total_track" || name == "track") {
        const double ref = contribution_reference_for_quality(contribution, 1.0);
        return std::max(abs_floor, std::abs(config.quality_ttl_rel_tol) * ref);
    }
    if (name == "bfl" || name == "back_focal_length") {
        const double ref = contribution_reference_for_quality(contribution, 1.0);
        return std::max(abs_floor, std::abs(config.quality_bfl_rel_tol) * ref);
    }
    if (name == "working_f_number" || name == "working_fno" ||
        name == "working_f_num" || name == "wfno" ||
        name == "image_space_working_f_number") {
        return std::max(1.0e-9, std::abs(config.quality_working_f_number_tol));
    }
    if (is_ratio_like_manufacturing_constraint_name(name)) {
        return std::max(1.0e-9, config.quality_ratio_tol);
    }
    if (is_surface_tangent_angle_constraint_name(name)) {
        return std::max(
            1.0e-9,
            std::abs(config.quality_surface_tangent_angle_tol_deg));
    }
    if (name == "conic" || name.find("conic_coefficient") != std::string::npos) {
        return std::max(1.0e-9, config.quality_conic_coefficient_tol);
    }
    if (is_edge_like_manufacturing_constraint_name(name) ||
        name.find("thickness") != std::string::npos) {
        return std::max(1.0e-9, config.quality_edge_tol);
    }
    if (name.find("distortion") != std::string::npos) {
        return std::max(1.0e-9, config.quality_distortion_tol);
    }
    if (name.find("wavefront") != std::string::npos ||
        name.find("opd") != std::string::npos) {
        const double wavefront_scale =
            std::isfinite(config.quality_wavefront_scale) &&
                    config.quality_wavefront_scale > 0.0
                ? config.quality_wavefront_scale
                : MeritConfig{}.quality_wavefront_scale;
        return std::max(1.0e-9, wavefront_scale);
    }
    if (name == "cra" || name == "max_cra" ||
        name.find("chief_ray_angle") != std::string::npos ||
        name.find("chief_ray_incidence_angle") != std::string::npos) {
        return std::max(1.0e-9, config.quality_chief_ray_angle_tol);
    }
    if (name == "tca" || name == "lca" ||
        name.find("lateral_color") != std::string::npos ||
        name.find("lateral_chromatic") != std::string::npos ||
        name.find("transverse_chromatic") != std::string::npos) {
        return std::max(1.0e-9, config.quality_lateral_color_tol);
    }
    return std::max(1.0e-9, config.quality_bound_tol);
}
}  // namespace analysis_detail

namespace analysis_detail {
double equivalent_spot_normalized_residual(
    const MeritConfig& config,
    double delta,
    double tolerance) {
    const double equivalent_scale = std::max(1.0e-9, config.quality_spot_scale);
    const double spot_normalizer =
        std::max({1.0e-9, config.spot_target, config.quality_spot_scale});
    const double equivalent_spot = equivalent_scale * delta / std::max(1.0e-9, tolerance);
    return equivalent_spot / spot_normalizer;
}
}  // namespace analysis_detail

}  // namespace optiland
