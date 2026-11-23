/*
 * Combines trace, first-order and physical health into topology-potential V2/V3 metrics.
 * Population scoring and search diagnostics consume these normalized quality summaries.
 * Reference tolerances, chief-path geometry and unavailable-data conventions keep
 * potential values comparable across the same stored candidate prescriptions.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

double potential_clamp01(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, value));
}

double robust_norm_value(double value, double tolerance) {
    if (!std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double tol = std::max(1.0e-12, std::abs(tolerance));
    return std::log1p(std::abs(value) / tol) / std::log(2.0);
}

double direction_angle_deg(const TracePoint& a, const TracePoint& b) {
    if (!a.valid || !b.valid ||
        !std::isfinite(a.l) || !std::isfinite(a.m) || !std::isfinite(a.n) ||
        !std::isfinite(b.l) || !std::isfinite(b.m) || !std::isfinite(b.n)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double an = std::sqrt(a.l * a.l + a.m * a.m + a.n * a.n);
    const double bn = std::sqrt(b.l * b.l + b.m * b.m + b.n * b.n);
    if (an <= 1.0e-12 || bn <= 1.0e-12) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double dot = std::clamp(
        (a.l * b.l + a.m * b.m + a.n * b.n) / (an * bn),
        -1.0,
        1.0);
    return rad_to_deg(std::acos(dot));
}

double chief_path_bend_budget_deg(double field_angle_deg) {
    if (!std::isfinite(field_angle_deg)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return 30.0 + 2.5 * std::abs(field_angle_deg);
}

double health_from_weighted_norms(
    std::initializer_list<std::pair<double, double>> weighted_norms,
    double* rms_out ,
    double* worst_out ) {
    double sum = 0.0;
    double weight_sum = 0.0;
    double worst = 0.0;
    for (const auto& item : weighted_norms) {
        const double norm = item.first;
        const double weight = std::max(0.0, item.second);
        if (!std::isfinite(norm) || weight <= 0.0) {
            continue;
        }
        sum += weight * norm * norm;
        weight_sum += weight;
        worst = std::max(worst, norm);
    }
    if (weight_sum <= 0.0) {
        if (rms_out) {
            *rms_out = std::numeric_limits<double>::quiet_NaN();
        }
        if (worst_out) {
            *worst_out = std::numeric_limits<double>::quiet_NaN();
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double rms = std::sqrt(sum / weight_sum);
    if (rms_out) {
        *rms_out = rms;
    }
    if (worst_out) {
        *worst_out = worst;
    }
    return 1.0 / (1.0 + rms);
}

double first_order_tolerance(const std::string& raw_key, double reference, const MeritConfig& config) {
    const std::string key = normalized_name(raw_key);
    const double ref = std::max(1.0, std::abs(reference));
    const double abs_floor = std::max(1.0e-9, config.quality_length_abs_tol);
    if (key == "efl") {
        return std::max(abs_floor, std::abs(config.quality_efl_rel_tol) * ref);
    }
    if (key == "ttl") {
        return std::max(abs_floor, std::abs(config.quality_ttl_rel_tol) * ref);
    }
    return std::max(abs_floor, std::abs(config.quality_bfl_rel_tol) * ref);
}

bool operand_matches_first_order(const MeritOperand& operand, const std::string& key) {
    const std::string name = normalized_name(operand.name);
    if (key == "efl") {
        return name == "efl" || name == "effective_focal_length" || name == "focal_length";
    }
    if (key == "ttl") {
        return name == "ttl" || name == "total_track" || name == "total_track_length" || name == "track";
    }
    return name == "bfl" || name == "back_focal_length" || name == "back_focus";
}

double first_order_norm_for_operand(double value, const MeritOperand& operand, const std::string& key, const MeritConfig& config) {
    const std::string mode = normalized_name(operand.mode);
    const double target = operand.target;
    const double upper = std::isfinite(operand.upper) ? operand.upper : target;
    const double lower = std::isfinite(operand.lower) ? operand.lower : target;
    if (!std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (mode == "less" || mode == "less_than" || mode == "upper" ||
        mode == "max" || mode == "le" || mode == "lte") {
        if (!std::isfinite(upper)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return robust_norm_value(std::max(0.0, value - upper), first_order_tolerance(key, upper, config));
    }
    if (mode == "greater" || mode == "greater_than" || mode == "lower" ||
        mode == "min" || mode == "ge" || mode == "gte") {
        if (!std::isfinite(lower)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return robust_norm_value(std::max(0.0, lower - value), first_order_tolerance(key, lower, config));
    }
    if (mode == "range" || mode == "between" || mode == "interval") {
        if (!std::isfinite(lower) || !std::isfinite(upper)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const double deficit = std::max(0.0, std::max(lower - value, value - upper));
        return robust_norm_value(deficit, first_order_tolerance(key, std::max(std::abs(lower), std::abs(upper)), config));
    }
    if (!std::isfinite(target)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return robust_norm_value(value - target, first_order_tolerance(key, target, config));
}

double first_order_norm_for_key(const Score& score, const MeritConfig& config, const std::string& key) {
    const double value = key == "efl" ? score.efl : (key == "ttl" ? score.ttl : score.bfl);
    for (const MeritOperand& operand : config.operands) {
        if (operand_matches_first_order(operand, key)) {
            return first_order_norm_for_operand(value, operand, key, config);
        }
    }
    const double target = key == "efl" ? config.target_efl : (key == "ttl" ? config.target_ttl : config.target_bfl);
    if (!std::isfinite(target) || std::abs(target) <= 1.0e-12 || !std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return robust_norm_value(value - target, first_order_tolerance(key, target, config));
}

void compute_topology_potential_v2(Score& score, const MeritConfig& config) {
    const double spot_tol = std::max({1.0e-9, std::abs(config.spot_target), std::abs(config.quality_spot_scale), 0.05});
    const double length_tol = std::max(1.0e-9, std::abs(config.quality_length_abs_tol));
    const double focus_tol = std::max(length_tol, 5.0 * spot_tol);
    const double edge_tol = std::max(1.0e-9, std::abs(config.quality_edge_tol));

    const double efl_norm = first_order_norm_for_key(score, config, "efl");
    const double ttl_norm = first_order_norm_for_key(score, config, "ttl");
    const double bfl_norm = first_order_norm_for_key(score, config, "bfl");
    double fo_rms = 0.0;
    double fo_worst = 0.0;
    double first_order_health = health_from_weighted_norms(
        {{efl_norm, 1.0}, {ttl_norm, 1.0}, {bfl_norm, 1.0}},
        &fo_rms,
        &fo_worst);
    if (!std::isfinite(first_order_health)) {
        first_order_health = 1.0;
        fo_worst = 0.0;
    }

    const double distortion_norm = robust_norm_value(score.max_distortion, config.quality_distortion_tol);
    const double lateral_color = std::max(
        std::isfinite(score.lateral_color) ? score.lateral_color : -std::numeric_limits<double>::infinity(),
        std::isfinite(score.finite_ray_lateral_color_mm) ? score.finite_ray_lateral_color_mm : -std::numeric_limits<double>::infinity());
    const double lateral_color_norm = robust_norm_value(lateral_color, config.quality_lateral_color_tol);
    const double chief_norm = robust_norm_value(score.max_chief_ray_angle, config.quality_chief_ray_angle_tol);
    const double focus_norm = robust_norm_value(score.finite_ray_field_focus_spread_mm, focus_tol);
    const double astig_norm = robust_norm_value(score.finite_ray_astig_focus_gap_mm, focus_tol);
    const double axial_norm = robust_norm_value(score.finite_ray_axial_color_focus_shift_mm, focus_tol);
    const double spherical_norm = robust_norm_value(score.finite_ray_spherical_focus_spread_mm, focus_tol);
    const double coma_norm = robust_norm_value(score.finite_ray_coma_tangential_mm, spot_tol);
    double hard_rms = 0.0;
    double hard_worst = 0.0;
    double hard_health = health_from_weighted_norms(
        {
            {distortion_norm, 1.20},
            {lateral_color_norm, 1.15},
            {chief_norm, 0.35},
            {focus_norm, 1.35},
            {astig_norm, 1.45},
            {axial_norm, 1.40},
            {spherical_norm, 0.80},
            {coma_norm, 1.00},
        },
        &hard_rms,
        &hard_worst);
    if (!std::isfinite(hard_health)) {
        hard_health = 0.55;
        hard_worst = 1.0;
    }

    int finite_core_count = 0;
    for (double norm : {focus_norm, astig_norm, axial_norm, spherical_norm, coma_norm}) {
        if (std::isfinite(norm)) {
            ++finite_core_count;
        }
    }
    const double evidence =
        potential_clamp01((score.finite_ray_valid_fraction - 0.10) / 0.50) *
        potential_clamp01(score.finite_ray_metric_count / 6.0) *
        potential_clamp01(static_cast<double>(finite_core_count) / 2.0);
    score.finite_ray_evidence = evidence;
    hard_health *= 0.35 + 0.65 * evidence;

    const double mean_spot_norm = robust_norm_value(score.mean_spot, spot_tol);
    const double max_spot_norm = robust_norm_value(score.max_spot, 2.0 * spot_tol);
    double image_rms = 0.0;
    double image_health = health_from_weighted_norms(
        {{mean_spot_norm, 0.80}, {max_spot_norm, 0.70}},
        &image_rms,
        nullptr);
    if (!std::isfinite(image_health)) {
        image_health = 0.55;
    }
    if (!score.trace_ok) {
        image_health = std::min(image_health, 0.45 + 0.55 * evidence);
    }

    const double edge_deficit = std::isfinite(score.min_edge)
                                    ? std::max(0.0, config.min_edge_thickness - score.min_edge)
                                    : std::numeric_limits<double>::quiet_NaN();
    const double edge_norm = robust_norm_value(edge_deficit, edge_tol);
    double margin_health = health_from_weighted_norms({{edge_norm, 1.0}});
    if (!std::isfinite(margin_health)) {
        margin_health = 0.80;
    }

    double max_norm = 0.0;
    bool have_norm = false;
    for (double norm : {
             efl_norm,
             ttl_norm,
             bfl_norm,
             distortion_norm,
             lateral_color_norm,
             chief_norm,
             focus_norm,
             astig_norm,
             axial_norm,
             spherical_norm,
             coma_norm,
             mean_spot_norm,
             max_spot_norm,
         }) {
        if (std::isfinite(norm)) {
            max_norm = std::max(max_norm, norm);
            have_norm = true;
        }
    }
    const double balance_health = have_norm ? 1.0 / (1.0 + max_norm) : 0.55;

    const double weighted =
        first_order_health * 0.20 +
        hard_health * 0.52 +
        image_health * 0.16 +
        balance_health * 0.09 +
        margin_health * 0.03;
    const double potential = 100.0 * potential_clamp01(weighted * (0.75 + 0.25 * first_order_health));

    score.topology_potential_score_v2 = potential;
    score.topology_potential_first_order_health_v2 = first_order_health;
    score.topology_potential_hard_aberration_health_v2 = hard_health;
    score.topology_potential_image_health_v2 = image_health;
    score.topology_potential_margin_health_v2 = margin_health;
    score.topology_potential_balance_health_v2 = balance_health;
    score.topology_potential_trace_health_v2 = score.trace_ok ? 1.0 : potential_clamp01(score.trace_coverage);
}

void compute_topology_potential_v3(Score& score, const MeritConfig& config) {
    const double spot_tol = std::max({1.0e-9, std::abs(config.spot_target), std::abs(config.quality_spot_scale), 0.05});
    const double length_tol = std::max(1.0e-9, std::abs(config.quality_length_abs_tol));
    const double focus_tol = std::max(length_tol, 5.0 * spot_tol);
    const double edge_tol = std::max(1.0e-9, std::abs(config.quality_edge_tol));

    const double efl_norm = first_order_norm_for_key(score, config, "efl");
    const double ttl_norm = first_order_norm_for_key(score, config, "ttl");
    const double bfl_norm = first_order_norm_for_key(score, config, "bfl");
    double fo_rms = 0.0;
    double fo_worst = 0.0;
    double first_order_health = health_from_weighted_norms(
        {{efl_norm, 1.0}, {ttl_norm, 1.0}, {bfl_norm, 1.0}},
        &fo_rms,
        &fo_worst);
    if (!std::isfinite(first_order_health)) {
        first_order_health = 1.0;
        fo_worst = 0.0;
    }

    const double path_excess_max_norm =
        robust_norm_value(score.chief_ray_path_excess_max, 0.12);
    const double path_excess_mean_norm =
        robust_norm_value(score.chief_ray_path_excess_mean, 0.08);
    double chief_path_health = health_from_weighted_norms(
        {{path_excess_max_norm, 1.35}, {path_excess_mean_norm, 0.65}});
    const bool have_chief_path_health = std::isfinite(chief_path_health);
    if (!have_chief_path_health) {
        chief_path_health = 0.55;
    }
    double chief_path_gate = 0.96;
    if (have_chief_path_health) {
        const double path_deficit =
            std::max(0.0, (0.70 - chief_path_health) / 0.70);
        chief_path_gate =
            potential_clamp01(1.0 - 0.35 * path_deficit * path_deficit);
    }

    const double distortion_norm = robust_norm_value(score.max_distortion, config.quality_distortion_tol);
    const double lateral_color = std::max(
        std::isfinite(score.lateral_color) ? score.lateral_color : -std::numeric_limits<double>::infinity(),
        std::isfinite(score.finite_ray_lateral_color_mm) ? score.finite_ray_lateral_color_mm : -std::numeric_limits<double>::infinity());
    const double lateral_color_norm = robust_norm_value(lateral_color, config.quality_lateral_color_tol);
    const double chief_norm = robust_norm_value(score.max_chief_ray_angle, config.quality_chief_ray_angle_tol);
    const double focus_norm = robust_norm_value(score.finite_ray_field_focus_spread_mm, focus_tol);
    const double astig_norm = robust_norm_value(score.finite_ray_astig_focus_gap_mm, focus_tol);
    const double axial_norm = robust_norm_value(score.finite_ray_axial_color_focus_shift_mm, focus_tol);
    const double spherical_norm = robust_norm_value(score.finite_ray_spherical_focus_spread_mm, focus_tol);
    const double coma_norm = robust_norm_value(score.finite_ray_coma_tangential_mm, spot_tol);
    double hard_rms = 0.0;
    double hard_worst = 0.0;
    double hard_health = health_from_weighted_norms(
        {
            {distortion_norm, 1.20},
            {lateral_color_norm, 1.15},
            {chief_norm, 0.20},
            {focus_norm, 1.35},
            {astig_norm, 1.45},
            {axial_norm, 1.40},
            {spherical_norm, 0.80},
            {coma_norm, 1.00},
        },
        &hard_rms,
        &hard_worst);
    if (!std::isfinite(hard_health)) {
        hard_health = 0.55;
        hard_worst = 1.0;
    }

    int finite_core_count = 0;
    for (double norm : {focus_norm, astig_norm, axial_norm, spherical_norm, coma_norm}) {
        if (std::isfinite(norm)) {
            ++finite_core_count;
        }
    }
    const double evidence =
        potential_clamp01((score.finite_ray_valid_fraction - 0.10) / 0.50) *
        potential_clamp01(score.finite_ray_metric_count / 6.0) *
        potential_clamp01(static_cast<double>(finite_core_count) / 2.0);
    score.finite_ray_evidence = evidence;
    hard_health *= 0.35 + 0.65 * evidence;

    const double mean_spot_norm = robust_norm_value(score.mean_spot, spot_tol);
    const double max_spot_norm = robust_norm_value(score.max_spot, 2.0 * spot_tol);
    double image_health = health_from_weighted_norms(
        {{mean_spot_norm, 0.80}, {max_spot_norm, 0.70}});
    if (!std::isfinite(image_health)) {
        image_health = 0.55;
    }
    if (!score.trace_ok) {
        image_health = std::min(image_health, 0.45 + 0.55 * evidence);
    }

    const double edge_deficit = std::isfinite(score.min_edge)
                                    ? std::max(0.0, config.min_edge_thickness - score.min_edge)
                                    : std::numeric_limits<double>::quiet_NaN();
    const double edge_norm = robust_norm_value(edge_deficit, edge_tol);
    double margin_health = health_from_weighted_norms({{edge_norm, 1.0}});
    if (!std::isfinite(margin_health)) {
        margin_health = 0.80;
    }

    double max_norm = 0.0;
    bool have_norm = false;
    for (double norm : {
             efl_norm,
             ttl_norm,
             bfl_norm,
             path_excess_max_norm,
             path_excess_mean_norm,
             distortion_norm,
             lateral_color_norm,
             chief_norm,
             focus_norm,
             astig_norm,
             axial_norm,
             spherical_norm,
             coma_norm,
             mean_spot_norm,
             max_spot_norm,
         }) {
        if (std::isfinite(norm)) {
            max_norm = std::max(max_norm, norm);
            have_norm = true;
        }
    }
    const double balance_health = have_norm ? 1.0 / (1.0 + max_norm) : 0.55;

    const double weighted =
        first_order_health * 0.18 +
        hard_health * 0.50 +
        image_health * 0.12 +
        balance_health * 0.13 +
        margin_health * 0.07;
    const double potential = 100.0 * potential_clamp01(
        weighted * chief_path_gate * (0.75 + 0.25 * first_order_health));

    score.topology_potential_score_v3 = potential;
    score.topology_potential_first_order_health_v3 = first_order_health;
    score.topology_potential_chief_path_health_v3 = chief_path_health;
    score.topology_potential_hard_aberration_health_v3 = hard_health;
    score.topology_potential_image_health_v3 = image_health;
    score.topology_potential_margin_health_v3 = margin_health;
    score.topology_potential_balance_health_v3 = balance_health;
    score.topology_potential_trace_health_v3 = score.trace_ok ? 1.0 : potential_clamp01(score.trace_coverage);
}

}  // namespace optiland
