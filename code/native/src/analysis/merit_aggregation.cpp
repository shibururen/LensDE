/*
 * Aggregates contribution groups into adaptive-quality and unified-gate merit models.
 * Called by merit finalization after image, trace and physical constraints are recorded.
 * Contribution order, category weights, tolerance normalization and trace gates define
 * the resulting score and must stay consistent with the DLS residual conventions.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
double adaptive_quality_merit(const Score& score, const MeritConfig& config) {
    double quality_sum = 0.0;
    double spot_count = 0.0;
    for (const auto& contribution : score.contributions) {
        if (!is_spot_contribution(contribution)) {
            continue;
        }
        if (!std::isfinite(contribution.value)) {
            return config.fail_merit;
        }
        const double field_weight = std::max(0.0, contribution.field_weight);
        quality_sum += field_weight * contribution.value * contribution.value;
        spot_count += field_weight;
    }
    if (spot_count <= 0.0) {
        if (!std::isfinite(score.mean_spot)) {
            return config.fail_merit;
        }
        quality_sum += score.mean_spot * score.mean_spot;
        spot_count = 1.0;
    }

    const double equivalent_scale = std::max(1.0e-9, config.quality_spot_scale);
    for (const auto& contribution : score.contributions) {
        if (is_spot_contribution(contribution) || scoring_weight(contribution.weight) <= 0.0) {
            continue;
        }
        const double delta = contribution_delta_for_quality(contribution);
        if (!std::isfinite(delta)) {
            return config.fail_merit;
        }
        if (delta <= 0.0) {
            continue;
        }
        const double tolerance = quality_tolerance_for_contribution(contribution, config);
        const double equivalent_spot = equivalent_scale * delta / tolerance;
        const double field_weight = std::max(0.0, contribution.field_weight);
        quality_sum += field_weight * equivalent_spot * equivalent_spot;
    }

    return std::sqrt(std::max(0.0, quality_sum) / std::max(1.0, spot_count));
}
}  // namespace analysis_detail

namespace analysis_detail {
void append_unified_trace_contributions(Score& score, const MeritConfig& config) {
    score.contributions.push_back(make_contribution(
        "unified_trace",
        "system_coverage",
        "greater_than",
        std::isfinite(score.trace_coverage) ? score.trace_coverage : 0.0,
        1.0,
        1.0,
        kInf,
        config.unified_trace_system_weight,
        0.0));
    score.contributions.push_back(make_contribution(
        "unified_trace",
        "min_field_coverage",
        "greater_than",
        std::isfinite(score.min_field_coverage) ? score.min_field_coverage : 0.0,
        1.0,
        1.0,
        kInf,
        config.unified_trace_min_field_weight,
        0.0));
    score.contributions.push_back(make_contribution(
        "unified_trace",
        "min_trace_progress",
        "greater_than",
        std::isfinite(score.min_trace_progress) ? score.min_trace_progress : 0.0,
        1.0,
        1.0,
        kInf,
        config.unified_trace_progress_weight,
        0.0));
    score.contributions.push_back(make_contribution(
        "unified_trace",
        "min_chief_progress",
        "greater_than",
        std::isfinite(score.min_chief_progress) ? score.min_chief_progress : 0.0,
        1.0,
        1.0,
        kInf,
        config.unified_trace_chief_progress_weight,
        0.0));
    score.contributions.push_back(make_contribution(
        "unified_trace",
        "failed_ray_weight",
        "minimize",
        std::max(0.0, score.failed_ray_weight),
        0.0,
        -kInf,
        kInf,
        config.unified_trace_failed_weight,
        0.0));

    double failure_type_severity = 0.0;
    for (const auto& contribution : score.contributions) {
        if (contribution.source == "unified_trace") {
            continue;
        }
        if (!is_trace_source(contribution)) {
            continue;
        }
        const std::string name = normalized_name(contribution.name);
        if (name.find("coverage") != std::string::npos ||
            name.find("progress") != std::string::npos) {
            continue;
        }
        if (std::isfinite(contribution.value)) {
            failure_type_severity += std::max(0.0, contribution.value);
        } else if (std::isfinite(contribution.penalty) && contribution.penalty > 0.0) {
            failure_type_severity += 1.0;
        }
    }
    score.contributions.push_back(make_contribution(
        "unified_trace",
        "failure_type_severity",
        "minimize",
        failure_type_severity,
        0.0,
        -kInf,
        kInf,
        config.unified_trace_failure_type_weight,
        0.0));
}
}  // namespace analysis_detail

namespace analysis_detail {
double group_gate_for_category(MeritCategory category, const Score& score) {
    switch (category) {
        case MeritCategory::FirstOrder:
            return score.first_order_gate;
        case MeritCategory::ImageQuality:
            return score.image_gate;
        case MeritCategory::Trace:
        case MeritCategory::Constraint:
        case MeritCategory::Regularization:
        case MeritCategory::Other:
            return 1.0;
    }
    return 1.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
void apply_unified_trace_gate_merit(
    Score& score,
    const MeritConfig& config,
    const LensSystem& system) {
    double trace_sum = 0.0;
    double trace_weight = 0.0;
    double constraint_sum = 0.0;
    double constraint_weight = 0.0;
    double first_order_sum = 0.0;
    double first_order_weight = 0.0;
    double image_sum = 0.0;
    double image_weight = 0.0;
    score.other_penalty = 0.0;

    // Preserve the original chromatic influence: all enabled chromatic
    // families share the single weight budget previously owned by lateral
    // color.  Field-wise families divide their share by configured field
    // weight, so adding diagnostics cannot inflate the image-group weight.
    double chromatic_family_raw_weight[4] = {0.0, 0.0, 0.0, 0.0};
    for (const auto& contribution : score.contributions) {
        if (is_trace_source(contribution) && contribution.source != "unified_trace") {
            continue;
        }
        const MeritCategory category = classify_unified_contribution(contribution);
        const int family = chromatic_quality_family(contribution.name);
        if (category != MeritCategory::ImageQuality || family < 0) {
            continue;
        }
        chromatic_family_raw_weight[family] += std::max(
            0.0,
            unified_base_weight_for_contribution(
                contribution, category, config, system));
    }
    int active_chromatic_family_count = 0;
    for (double weight : chromatic_family_raw_weight) {
        if (weight > 0.0) {
            ++active_chromatic_family_count;
        }
    }
    const double chromatic_family_budget =
        active_chromatic_family_count > 0
            ? std::max(0.0, config.unified_image_chromatic_weight) /
                  static_cast<double>(active_chromatic_family_count)
            : 0.0;

    auto accumulate = [&](MeritCategory category, double robust_penalty, double base_weight) {
        switch (category) {
            case MeritCategory::Trace:
                trace_sum += robust_penalty;
                trace_weight += base_weight;
                break;
            case MeritCategory::Constraint:
                constraint_sum += robust_penalty;
                constraint_weight += base_weight;
                break;
            case MeritCategory::FirstOrder:
                first_order_sum += robust_penalty;
                first_order_weight += base_weight;
                break;
            case MeritCategory::ImageQuality:
                image_sum += robust_penalty;
                image_weight += base_weight;
                break;
            case MeritCategory::Regularization:
            case MeritCategory::Other:
                break;
        }
    };

    for (auto& contribution : score.contributions) {
        if (is_trace_source(contribution) && contribution.source != "unified_trace") {
            continue;
        }
        const MeritCategory category = classify_unified_contribution(contribution);
        contribution.category = category;
        if (category == MeritCategory::Other || category == MeritCategory::Regularization) {
            if (std::isfinite(contribution.penalty)) {
                score.other_penalty += std::max(0.0, contribution.penalty);
            }
            continue;
        }
        double base_weight = std::max(
            0.0,
            unified_base_weight_for_contribution(
                contribution, category, config, system));
        if (category == MeritCategory::Constraint &&
            contribution.name != "stop_air_edge_thickness" &&
            !is_relative_illumination_proxy_operand_name(contribution.name) &&
            !is_vignette_meridional_excess_operand_name(contribution.name) &&
            !is_vignetting_boundary_support_operand_name(contribution.name)) {
            base_weight = scoring_weight(contribution.weight) > 0.0 ? 1.0 : 0.0;
        }
        const int chromatic_family = chromatic_quality_family(contribution.name);
        if (category == MeritCategory::ImageQuality && chromatic_family >= 0 &&
            chromatic_family_raw_weight[chromatic_family] > 0.0) {
            base_weight = chromatic_family_budget * base_weight /
                          chromatic_family_raw_weight[chromatic_family];
        }
        contribution.base_weight = base_weight;
        if (base_weight <= 0.0) {
            contribution.tolerance = std::numeric_limits<double>::quiet_NaN();
            contribution.normalized_residual = 0.0;
            contribution.robust_penalty = 0.0;
            contribution.penalty = 0.0;
            continue;
        }
        const double delta = contribution_delta_for_unified(contribution);
        const double tolerance = category == MeritCategory::Constraint
                                     ? quality_tolerance_for_contribution(contribution, config)
                                     : unified_tolerance_for_contribution(contribution, category, config);
        contribution.tolerance = tolerance;
        if (category == MeritCategory::Constraint) {
            contribution.normalized_residual =
                equivalent_spot_normalized_residual(config, delta, tolerance);
            contribution.robust_penalty =
                base_weight * contribution.normalized_residual * contribution.normalized_residual;
        } else {
            contribution.normalized_residual = delta / tolerance;
            contribution.robust_penalty =
                base_weight * pseudo_huber(contribution.normalized_residual, config.unified_loss_delta);
        }
        accumulate(category, contribution.robust_penalty, base_weight);
    }

    score.trace_group_error =
        trace_weight > 0.0 ? trace_sum / std::max(1.0e-12, trace_weight) : 0.0;
    score.constraint_group_error = constraint_sum;
    score.first_order_group_error =
        first_order_weight > 0.0 ? first_order_sum / std::max(1.0e-12, first_order_weight) : 0.0;
    score.image_group_error =
        image_weight > 0.0 ? image_sum / std::max(1.0e-12, image_weight) : 0.0;

    const double system_coverage = std::isfinite(score.trace_coverage) ? score.trace_coverage : 0.0;
    const double min_field = std::isfinite(score.min_field_coverage) ? score.min_field_coverage : 0.0;
    const double chief_progress =
        std::isfinite(score.min_chief_progress) ? score.min_chief_progress : 0.0;
    score.trace_gate =
        smoothstep(system_coverage, config.unified_gate_system_coverage_lo, 1.0) *
        smoothstep(min_field, config.unified_gate_min_field_lo, 1.0) *
        smoothstep(chief_progress, config.unified_gate_chief_progress_lo, 1.0);
    score.constraint_gate = 1.0 / (1.0 + std::max(0.0, score.constraint_group_error));
    const double min_first_order_gate = clamp01(config.unified_min_first_order_gate);
    score.first_order_gate =
        min_first_order_gate + (1.0 - min_first_order_gate) * score.trace_gate;
    score.image_gate = score.trace_gate * score.trace_gate;

    const double trace_group_weight = std::abs(config.group_weight_trace);
    const double constraint_group_weight = std::abs(config.group_weight_constraint);
    const double first_order_group_weight = std::abs(config.group_weight_first_order);
    const double image_group_weight = std::abs(config.group_weight_image);
    score.trace_penalty = trace_group_weight * score.trace_group_error;
    score.trace_priority_penalty = 0.0;
    if (config.trace_priority_weight > 0.0) {
        score.trace_priority_penalty =
            std::abs(config.trace_priority_weight) * score.trace_priority_metric;
    }
    score.constraint_penalty = constraint_group_weight * score.constraint_group_error;
    score.first_order_penalty =
        score.first_order_gate * first_order_group_weight * score.first_order_group_error;
    score.image_penalty = score.image_gate * image_group_weight * score.image_group_error;

    auto category_weight_sum = [&](MeritCategory category) {
        switch (category) {
            case MeritCategory::Trace:
                return trace_weight;
            case MeritCategory::Constraint:
                return constraint_weight;
            case MeritCategory::FirstOrder:
                return first_order_weight;
            case MeritCategory::ImageQuality:
                return image_weight;
            case MeritCategory::Regularization:
            case MeritCategory::Other:
                return 0.0;
        }
        return 0.0;
    };

    for (auto& contribution : score.contributions) {
        if (is_trace_source(contribution) && contribution.source != "unified_trace") {
            contribution.category = MeritCategory::Trace;
            contribution.gate = 0.0;
            contribution.penalty = 0.0;
            continue;
        }
        if (contribution.category == MeritCategory::Other ||
            contribution.category == MeritCategory::Regularization) {
            continue;
        }
        const double gate = group_gate_for_category(contribution.category, score);
        const double group_weight = std::abs(group_weight_for_category(contribution.category, config));
        contribution.gate = gate;
        if (contribution.category == MeritCategory::Constraint) {
            contribution.penalty = group_weight * gate * contribution.robust_penalty;
            contribution.weight = group_weight * gate * contribution.base_weight;
        } else {
            const double denom =
                std::max(1.0e-12, category_weight_sum(contribution.category));
            contribution.penalty = group_weight * gate * contribution.robust_penalty / denom;
            contribution.weight = group_weight * gate * contribution.base_weight / denom;
        }
    }

    const double total_penalty =
        score.trace_penalty +
        score.trace_priority_penalty +
        score.constraint_penalty +
        score.first_order_penalty +
        score.image_penalty +
        score.other_penalty;
    score.merit_sum = total_penalty;
    score.merit_weight_sum = 1.0;
    score.uncapped_merit = total_penalty;
    if (!std::isfinite(score.uncapped_merit)) {
        score.trace_ok = false;
        score.merit = config.fail_merit;
    } else {
        score.merit = std::min(score.uncapped_merit, config.fail_merit);
    }
}
}  // namespace analysis_detail

namespace analysis_detail {
bool soft_trace_policy(const MeritConfig& config) {
    return config.trace_policy != TraceFailurePolicy::Strict;
}
}  // namespace analysis_detail

namespace analysis_detail {
double trace_priority_metric(const Score& score, const MeritConfig& config) {
    if (!soft_trace_policy(config)) {
        return 0.0;
    }
    double metric = 0.0;
    if (config.require_chief_ray && score.chief_ray_failed_fields > 0) {
        metric += static_cast<double>(score.chief_ray_failed_fields);
    }
    if (config.require_off_axis_field_coverage && score.zero_coverage_off_axis_fields > 0) {
        metric += static_cast<double>(score.zero_coverage_off_axis_fields);
    }
    if (std::isfinite(score.trace_coverage)) {
        const double deficit = std::max(0.0, config.min_system_coverage - score.trace_coverage);
        metric += deficit * deficit;
    } else {
        metric += 1.0;
    }
    if (std::isfinite(score.min_field_coverage)) {
        const double deficit = std::max(0.0, config.min_field_coverage - score.min_field_coverage);
        metric += deficit * deficit;
    } else {
        metric += 1.0;
    }
    if (std::isfinite(score.trace_progress)) {
        const double deficit = std::max(0.0, 1.0 - score.trace_progress);
        metric += 0.5 * deficit * deficit;
    } else {
        metric += 0.5;
    }
    if (std::isfinite(score.min_trace_progress)) {
        const double deficit = std::max(0.0, 1.0 - score.min_trace_progress);
        metric += 0.5 * deficit * deficit;
    } else {
        metric += 0.5;
    }
    if (config.require_chief_ray) {
        if (std::isfinite(score.min_chief_progress)) {
            const double deficit = std::max(0.0, 1.0 - score.min_chief_progress);
            metric += 0.5 * deficit * deficit;
        } else {
            metric += 0.5;
        }
    }
    return metric;
}
}  // namespace analysis_detail

}  // namespace optiland
