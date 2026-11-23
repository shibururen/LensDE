/*
 * Build ordered DLS residual vectors for the supported merit, guard-band and per-ray formulations.
 * DlsSession scoring and batch polish translate Score contributions through these common layouts.
 * Preserve residual count/order and padding/guard replacement rules across finite-difference evaluations.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

std::vector<double> weighted_rms_residuals_from_score(const Score& score, double fail_merit) {
    if (!score.trace_ok || !std::isfinite(score.merit)) {
        const std::size_t n = std::max<std::size_t>(1, score.contributions.size());
        const double value = std::sqrt(std::max(1.0, fail_merit) / static_cast<double>(n));
        return std::vector<double>(n, value);
    }
    std::vector<double> residuals;
    residuals.reserve(std::max<std::size_t>(1, score.contributions.size()));
    for (const auto& c : score.contributions) {
        const double weight = std::isfinite(c.weight) ? std::abs(c.weight) : 0.0;
        const double delta = contribution_delta(c);
        double residual = std::sqrt(weight) * delta;
        if (!std::isfinite(residual)) {
            residual = std::sqrt(std::max(1.0, fail_merit));
        }
        residuals.push_back(residual);
    }
    if (residuals.empty()) {
        residuals.push_back(0.0);
    }
    return residuals;
}

std::vector<double> adaptive_quality_residuals_from_score(
    const Score& score,
    const MeritConfig& config) {
    if (!score.trace_ok || !std::isfinite(score.merit)) {
        const std::size_t n = std::max<std::size_t>(1, score.contributions.size());
        const double value = std::sqrt(std::max(1.0, config.fail_merit) / static_cast<double>(n));
        return std::vector<double>(n, value);
    }

    double quality_count = 0.0;
    for (const auto& c : score.contributions) {
        if (is_image_quality_contribution(c)) {
            quality_count += 1.0;
        }
    }
    const bool use_mean_wavefront_fallback =
        quality_count <= 0.0 && std::isfinite(score.mean_wavefront_rms);
    const bool use_mean_spot_fallback =
        quality_count <= 0.0 && !use_mean_wavefront_fallback && std::isfinite(score.mean_spot);
    if (use_mean_wavefront_fallback || use_mean_spot_fallback) {
        quality_count = 1.0;
    }
    const double normalizer = 1.0 / std::sqrt(std::max(1.0, quality_count));
    const double equivalent_scale = std::max(1.0e-9, config.quality_spot_scale);

    std::vector<double> residuals;
    residuals.reserve(std::max<std::size_t>(1, score.contributions.size()));
    if (use_mean_wavefront_fallback) {
        residuals.push_back(score.mean_wavefront_rms * normalizer);
    } else if (use_mean_spot_fallback) {
        residuals.push_back(score.mean_spot * normalizer);
    }
    for (const auto& c : score.contributions) {
        double residual = 0.0;
        if (is_image_quality_contribution(c)) {
            residual = c.value;
        } else if (std::isfinite(c.weight) && std::abs(c.weight) > 0.0) {
            const double delta = contribution_delta(c);
            const double tolerance = quality_tolerance_for_contribution(c, config);
            residual = equivalent_scale * delta / tolerance;
        }
        residual *= normalizer;
        if (!std::isfinite(residual)) {
            residual = std::sqrt(std::max(1.0, config.fail_merit));
        }
        residuals.push_back(residual);
    }
    if (residuals.empty()) {
        residuals.push_back(std::isfinite(score.mean_wavefront_rms)
                                ? score.mean_wavefront_rms
                                : (std::isfinite(score.mean_spot) ? score.mean_spot : 0.0));
    }
    return residuals;
}

std::vector<double> unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config) {
    constexpr std::size_t kUnifiedResidualCount = 6;
    if (!std::isfinite(score.merit) ||
        (!score.trace_ok && config.trace_policy == TraceFailurePolicy::Strict)) {
        const double value = std::sqrt(
            std::max(1.0, config.fail_merit) / static_cast<double>(kUnifiedResidualCount));
        return std::vector<double>(kUnifiedResidualCount, value);
    }
    auto residual = [](double penalty) {
        return std::sqrt(std::max(0.0, std::isfinite(penalty) ? penalty : 0.0));
    };
    return {
        residual(score.trace_penalty),
        residual(score.trace_priority_penalty),
        residual(score.constraint_penalty),
        residual(score.first_order_penalty),
        residual(score.image_penalty),
        residual(score.other_penalty),
    };
}

double signed_sqrt_penalty_residual(const MeritContribution& contribution) {
    double residual = std::sqrt(
        std::max(0.0, std::isfinite(contribution.penalty) ? contribution.penalty : 0.0));
    double sign_source = std::numeric_limits<double>::quiet_NaN();
    if (std::isfinite(contribution.normalized_residual)) {
        sign_source = contribution.normalized_residual;
    } else {
        sign_source = contribution_delta(contribution);
    }
    if (std::isfinite(sign_source) && sign_source < 0.0) {
        residual = -residual;
    }
    return std::isfinite(residual) ? residual : 0.0;
}

double signed_normalized_operand_residual(
    const MeritContribution& contribution,
    const MeritConfig& config) {
    double magnitude = normalized_contribution_violation(contribution, config);
    if (!std::isfinite(magnitude)) {
        magnitude = std::sqrt(std::max(1.0, config.fail_merit));
    }
    const double weight =
        std::max(0.0, std::isfinite(contribution.weight)
                          ? std::abs(contribution.weight)
                          : std::abs(contribution.base_weight));
    double residual = std::sqrt(weight) * magnitude;
    double sign_source = std::numeric_limits<double>::quiet_NaN();
    if (std::isfinite(contribution.normalized_residual)) {
        sign_source = contribution.normalized_residual;
    } else {
        sign_source = contribution_delta(contribution);
    }
    if (std::isfinite(sign_source) && sign_source < 0.0) {
        residual = -residual;
    }
    return std::isfinite(residual) ? residual : 0.0;
}

double guard_band_residual(
    const MeritContribution& contribution,
    const MeritConfig& config) {
    if (!contribution_mode_is_bound_like(contribution) ||
        contribution.category == MeritCategory::Trace ||
        contribution.category == MeritCategory::Regularization ||
        contribution.category == MeritCategory::Other ||
        !std::isfinite(contribution.value)) {
        return 0.0;
    }
    if (normalized_contribution_violation(contribution, config) > 1.0e-12) {
        return 0.0;
    }

    double tolerance = std::isfinite(contribution.tolerance) &&
                               std::abs(contribution.tolerance) > 1.0e-12
                           ? std::abs(contribution.tolerance)
                           : quality_tolerance_for_contribution(contribution, config);
    tolerance = std::max(1.0e-12, tolerance);
    const double guard_width = tolerance;
    double guard = 0.0;
    const std::string mode = normalized_token(contribution.mode);
    const bool lower_active =
        (mode == "greater" || mode == "greater_than" || mode == "lower" ||
         mode == "min" || mode == "ge" || mode == "gte" ||
         mode == "range" || mode == "between" || mode == "interval") &&
        std::isfinite(contribution.lower);
    const bool upper_active =
        (mode == "less" || mode == "less_than" || mode == "upper" ||
         mode == "max" || mode == "le" || mode == "lte" ||
         mode == "range" || mode == "between" || mode == "interval") &&
        std::isfinite(contribution.upper);
    if (lower_active) {
        const double slack = contribution.value - contribution.lower;
        if (std::isfinite(slack) && slack >= 0.0 && slack < guard_width) {
            guard = std::max(guard, (guard_width - slack) / guard_width);
        }
    }
    if (upper_active) {
        const double slack = contribution.upper - contribution.value;
        if (std::isfinite(slack) && slack >= 0.0 && slack < guard_width) {
            guard = std::max(guard, (guard_width - slack) / guard_width);
        }
    }
    if (guard <= 0.0) {
        return 0.0;
    }
    const double weight =
        std::max(0.0, std::isfinite(contribution.weight)
                          ? std::abs(contribution.weight)
                          : std::abs(contribution.base_weight));
    constexpr double kGuardScale = 0.25;
    const double residual = kGuardScale * std::sqrt(weight) * guard;
    return std::isfinite(residual) ? residual : 0.0;
}

void append_guard_band_residuals(
    const Score& score,
    const MeritConfig& config,
    std::vector<double>& residuals) {
    for (const auto& contribution : score.contributions) {
        if (contribution.category == MeritCategory::Trace) {
            continue;
        }
        residuals.push_back(guard_band_residual(contribution, config));
    }
}

bool is_single_ray_spot_guard_contribution(const MeritContribution& contribution) {
    const std::string name = normalized_token(contribution.name);
    return name == "single_ray_spot_guard" ||
           name == "single_ray_spot" ||
           name == "max_single_ray_spot" ||
           name == "max_ray_spot" ||
           name == "ray_spot_guard" ||
           name == "spot_ray_guard";
}

bool is_single_ray_wavefront_guard_contribution(const MeritContribution& contribution) {
    const std::string name = normalized_token(contribution.name);
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

bool single_ray_guard_residual_replaces_contribution(
    const Score& score,
    const MeritContribution& contribution) {
    if (is_single_ray_spot_guard_contribution(contribution)) {
        return !score.dls_single_ray_spot_guard_residuals.empty();
    }
    if (is_single_ray_wavefront_guard_contribution(contribution)) {
        return !score.dls_single_ray_wavefront_guard_residuals.empty();
    }
    return false;
}

std::size_t single_ray_guard_residual_count(const Score& score) {
    return score.dls_single_ray_spot_guard_residuals.size() +
           score.dls_single_ray_wavefront_guard_residuals.size();
}

void append_single_ray_guard_residuals(
    const Score& score,
    std::vector<double>& residuals,
    double scale ) {
    for (double residual : score.dls_single_ray_spot_guard_residuals) {
        if (std::isfinite(residual)) {
            residuals.push_back(scale * residual);
        }
    }
    for (double residual : score.dls_single_ray_wavefront_guard_residuals) {
        if (std::isfinite(residual)) {
            residuals.push_back(scale * residual);
        }
    }
}

void append_ray_spot_residuals(
    const Score& score,
    std::vector<double>& residuals,
    double scale ) {
    for (double residual : score.dls_ray_spot_residuals) {
        if (std::isfinite(residual)) {
            residuals.push_back(scale * residual);
        }
    }
}

std::vector<double> expanded_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    bool include_guard_band ) {
    if (!std::isfinite(score.merit) ||
        (!score.trace_ok && config.trace_policy == TraceFailurePolicy::Strict)) {
        const std::size_t n = std::max<std::size_t>(5, score.contributions.size());
        const double value = std::sqrt(
            std::max(1.0, config.fail_merit) / static_cast<double>(n));
        return std::vector<double>(n, value);
    }

    auto group_residual = [](double penalty) {
        return std::sqrt(std::max(0.0, std::isfinite(penalty) ? penalty : 0.0));
    };

    std::vector<double> residuals;
    residuals.reserve(std::max<std::size_t>(
        5,
        score.contributions.size() + 2 + single_ray_guard_residual_count(score)));
    // Keep trace as a compact guard. The optical operands below stay expanded.
    residuals.push_back(group_residual(score.trace_penalty));
    residuals.push_back(group_residual(score.trace_priority_penalty));

    bool has_constraint = false;
    bool has_first_order = false;
    bool has_image = false;
    bool has_other = false;
    for (const auto& contribution : score.contributions) {
        if (contribution.category == MeritCategory::Trace) {
            continue;
        }
        switch (contribution.category) {
            case MeritCategory::Constraint:
                has_constraint = true;
                break;
            case MeritCategory::FirstOrder:
                has_first_order = true;
                break;
            case MeritCategory::ImageQuality:
                has_image = true;
                break;
            case MeritCategory::Regularization:
            case MeritCategory::Other:
                has_other = true;
                break;
            case MeritCategory::Trace:
                break;
        }
        if (!single_ray_guard_residual_replaces_contribution(score, contribution)) {
            residuals.push_back(signed_sqrt_penalty_residual(contribution));
        }
    }

    if (!has_constraint && score.constraint_penalty > 0.0) {
        residuals.push_back(group_residual(score.constraint_penalty));
    }
    if (!has_first_order && score.first_order_penalty > 0.0) {
        residuals.push_back(group_residual(score.first_order_penalty));
    }
    if (!has_image && score.image_penalty > 0.0) {
        residuals.push_back(group_residual(score.image_penalty));
    }
    if (!has_other && score.other_penalty > 0.0) {
        residuals.push_back(group_residual(score.other_penalty));
    }
    append_single_ray_guard_residuals(score, residuals);
    if (residuals.empty()) {
        return unified_trace_gate_residuals_from_score(score, config);
    }
    if (include_guard_band) {
        append_guard_band_residuals(score, config, residuals);
    }
    return residuals;
}

std::vector<double> normalized_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    bool include_guard_band ) {
    if (!std::isfinite(score.merit) ||
        (!score.trace_ok && config.trace_policy == TraceFailurePolicy::Strict)) {
        const std::size_t n = std::max<std::size_t>(5, score.contributions.size());
        const double value = std::sqrt(
            std::max(1.0, config.fail_merit) / static_cast<double>(n));
        return std::vector<double>(n, value);
    }

    std::vector<double> residuals;
    residuals.reserve(std::max<std::size_t>(
        5,
        score.contributions.size() * 2 + 2 + single_ray_guard_residual_count(score)));
    residuals.push_back(std::sqrt(
        std::max(0.0, std::isfinite(score.trace_penalty) ? score.trace_penalty : 0.0)));
    residuals.push_back(std::sqrt(
        std::max(0.0, std::isfinite(score.trace_priority_penalty)
                          ? score.trace_priority_penalty
                          : 0.0)));
    for (const auto& contribution : score.contributions) {
        if (contribution.category == MeritCategory::Trace) {
            continue;
        }
        if (!single_ray_guard_residual_replaces_contribution(score, contribution)) {
            residuals.push_back(signed_normalized_operand_residual(contribution, config));
        }
    }
    append_single_ray_guard_residuals(score, residuals);
    if (include_guard_band) {
        append_guard_band_residuals(score, config, residuals);
    }
    if (residuals.empty()) {
        return unified_trace_gate_residuals_from_score(score, config);
    }
    return residuals;
}

std::vector<double> ray_spot_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config) {
    if (!std::isfinite(score.merit) ||
        (!score.trace_ok && config.trace_policy == TraceFailurePolicy::Strict)) {
        const std::size_t n = std::max<std::size_t>(
            5,
            score.dls_ray_spot_residuals.size() + single_ray_guard_residual_count(score));
        const double value = std::sqrt(
            std::max(1.0, config.fail_merit) / static_cast<double>(n));
        return std::vector<double>(n, value);
    }
    std::vector<double> residuals;
    residuals.reserve(std::max<std::size_t>(
        1,
        score.dls_ray_spot_residuals.size() + single_ray_guard_residual_count(score)));
    append_ray_spot_residuals(score, residuals);
    append_single_ray_guard_residuals(score, residuals);
    if (residuals.empty()) {
        return expanded_unified_trace_gate_residuals_from_score(score, config, false);
    }
    return residuals;
}

std::vector<double> hybrid_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    bool include_guard_band ) {
    std::vector<double> residuals =
        expanded_unified_trace_gate_residuals_from_score(score, config, false);
    if (!std::isfinite(score.merit) ||
        (!score.trace_ok && config.trace_policy == TraceFailurePolicy::Strict)) {
        return residuals;
    }
    for (const auto& contribution : score.contributions) {
        if (contribution.category == MeritCategory::Trace) {
            continue;
        }
        if (!single_ray_guard_residual_replaces_contribution(score, contribution)) {
            residuals.push_back(signed_normalized_operand_residual(contribution, config));
        }
    }
    if (include_guard_band) {
        append_guard_band_residuals(score, config, residuals);
    }
    return residuals;
}

std::vector<double> expanded_ray_spot_unified_trace_gate_residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    bool include_normalized ,
    double ray_scale ) {
    std::vector<double> residuals =
        include_normalized
            ? hybrid_unified_trace_gate_residuals_from_score(score, config, false)
            : expanded_unified_trace_gate_residuals_from_score(score, config, false);
    if (std::isfinite(score.merit) &&
        (score.trace_ok || config.trace_policy != TraceFailurePolicy::Strict)) {
        append_ray_spot_residuals(score, residuals, ray_scale);
    }
    return residuals;
}

bool expanded_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "expanded" ||
           token == "per_operand" ||
           token == "per_contribution" ||
           token == "contribution" ||
           token == "contributions" ||
           token == "operand";
}

bool normalized_operand_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "normalized" ||
           token == "operand_normalized" ||
           token == "normalized_operand" ||
           token == "codev_like" ||
           token == "zemax_like";
}

bool guarded_expanded_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "expanded_guarded" ||
           token == "guarded_expanded" ||
           token == "expanded_guard" ||
           token == "guarded";
}

bool guarded_normalized_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "operand_normalized_guarded" ||
           token == "normalized_guarded" ||
           token == "guarded_normalized" ||
           token == "codev_like_guarded" ||
           token == "zemax_like_guarded";
}

bool hybrid_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "expanded_normalized" ||
           token == "normalized_expanded" ||
           token == "hybrid_operand" ||
           token == "merit_normalized";
}

bool guarded_hybrid_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "expanded_normalized_guarded" ||
           token == "normalized_expanded_guarded" ||
           token == "hybrid_operand_guarded" ||
           token == "merit_normalized_guarded";
}

bool ray_spot_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "ray_spot" ||
           token == "spot_ray" ||
           token == "ray_level" ||
           token == "ray_level_spot";
}

bool expanded_ray_spot_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "expanded_ray_spot" ||
           token == "expanded_spot_ray" ||
           token == "ray_spot_expanded";
}

bool expanded_normalized_ray_spot_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "expanded_normalized_ray_spot" ||
           token == "expanded_ray_spot_normalized" ||
           token == "ray_spot_expanded_normalized" ||
           token == "codev_ray_spot" ||
           token == "zemax_ray_spot";
}

bool light_expanded_ray_spot_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "expanded_ray_spot_light" ||
           token == "light_expanded_ray_spot";
}

bool light_expanded_normalized_ray_spot_residual_mode(const std::string& mode) {
    const std::string token = normalized_token(mode);
    return token == "expanded_normalized_ray_spot_light" ||
           token == "light_expanded_normalized_ray_spot" ||
           token == "codev_ray_spot_light" ||
           token == "zemax_ray_spot_light";
}

std::vector<double> residuals_from_score(
    const Score& score,
    const MeritConfig& config,
    const std::string& residual_mode ) {
    if (unified_trace_gate_enabled(config)) {
        if (light_expanded_normalized_ray_spot_residual_mode(residual_mode)) {
            return expanded_ray_spot_unified_trace_gate_residuals_from_score(
                score,
                config,
                true,
                0.25);
        }
        if (light_expanded_ray_spot_residual_mode(residual_mode)) {
            return expanded_ray_spot_unified_trace_gate_residuals_from_score(
                score,
                config,
                false,
                0.25);
        }
        if (expanded_normalized_ray_spot_residual_mode(residual_mode)) {
            return expanded_ray_spot_unified_trace_gate_residuals_from_score(score, config, true);
        }
        if (expanded_ray_spot_residual_mode(residual_mode)) {
            return expanded_ray_spot_unified_trace_gate_residuals_from_score(score, config, false);
        }
        if (ray_spot_residual_mode(residual_mode)) {
            return ray_spot_unified_trace_gate_residuals_from_score(score, config);
        }
        if (guarded_hybrid_residual_mode(residual_mode)) {
            return hybrid_unified_trace_gate_residuals_from_score(score, config, true);
        }
        if (hybrid_residual_mode(residual_mode)) {
            return hybrid_unified_trace_gate_residuals_from_score(score, config, false);
        }
        if (guarded_normalized_residual_mode(residual_mode)) {
            return normalized_unified_trace_gate_residuals_from_score(score, config, true);
        }
        if (normalized_operand_residual_mode(residual_mode)) {
            return normalized_unified_trace_gate_residuals_from_score(score, config, false);
        }
        if (guarded_expanded_residual_mode(residual_mode)) {
            return expanded_unified_trace_gate_residuals_from_score(score, config, true);
        }
        if (expanded_residual_mode(residual_mode)) {
            return expanded_unified_trace_gate_residuals_from_score(score, config);
        }
        return unified_trace_gate_residuals_from_score(score, config);
    }
    if (adaptive_quality_enabled(config)) {
        return adaptive_quality_residuals_from_score(score, config);
    }
    return weighted_rms_residuals_from_score(score, config.fail_merit);
}

}  // namespace optimizer_detail

}  // namespace optiland
