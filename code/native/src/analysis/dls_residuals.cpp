/*
 * Builds ray-spot and single-ray guard residual arrays from completed field statistics.
 * Merit finalization supplies these arrays to the native DLS residual interface.
 * Keep contribution scaling, invalid-sample slots and per-ray correspondence stable
 * so residual dimensions and the scalar merit retain their existing relationship.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

void populate_dls_ray_spot_residuals(
    Score& score,
    const std::vector<FieldSpot>& field_spots,
    const MeritConfig& config) {
    score.dls_ray_spot_residuals.clear();
    for (const auto& contribution : score.contributions) {
        if (!is_rms_spot_operand_name(contribution.name) ||
            contribution.index >= field_spots.size()) {
            continue;
        }
        const auto& raw_residuals =
            field_spots[contribution.index].stats.ray_spot_residuals;
        if (raw_residuals.empty()) {
            continue;
        }
        double tolerance =
            std::isfinite(contribution.tolerance) &&
                    std::abs(contribution.tolerance) > 1.0e-12
                ? std::abs(contribution.tolerance)
                : std::max({1.0e-9,
                            std::abs(config.spot_target),
                            std::abs(config.quality_spot_scale),
                            0.02});
        tolerance = std::max(1.0e-12, tolerance);
        const double weight =
            std::max(0.0, std::isfinite(contribution.weight)
                              ? std::abs(contribution.weight)
                              : std::abs(contribution.base_weight));
        const double scale = std::sqrt(weight) / tolerance;
        for (double residual : raw_residuals) {
            if (std::isfinite(residual)) {
                score.dls_ray_spot_residuals.push_back(scale * residual);
            }
        }
    }
}

double single_ray_guard_upper(
    const std::string& raw_mode,
    double target,
    double raw_upper) {
    const std::string mode = normalized_name(raw_mode);
    double upper = std::numeric_limits<double>::quiet_NaN();
    if (mode == "less" || mode == "less_than" || mode == "upper" ||
        mode == "max" || mode == "le" || mode == "lte") {
        upper = std::isfinite(raw_upper) ? raw_upper : target;
    } else if (mode == "range" || mode == "between" || mode == "interval") {
        upper = raw_upper;
    }
    if (!std::isfinite(upper)) {
        upper = std::isfinite(target) && target > 0.0 ? target : raw_upper;
    }
    if (!std::isfinite(upper) || upper < 0.0) {
        upper = 10.0;
    }
    return std::max(0.0, upper);
}

double single_ray_guard_upper_from_contribution(const MeritContribution& contribution) {
    return single_ray_guard_upper(
        contribution.mode,
        contribution.target,
        contribution.upper);
}

double single_ray_guard_upper_from_operand(const MeritOperand& operand) {
    return single_ray_guard_upper(operand.mode, operand.target, operand.upper);
}

double dls_contribution_residual_scale(
    const MeritContribution& contribution,
    double tolerance) {
    const double weight =
        std::max(0.0, std::isfinite(contribution.weight)
                          ? std::abs(contribution.weight)
                          : std::abs(contribution.base_weight));
    return std::sqrt(weight) / std::max(1.0e-12, std::abs(tolerance));
}

void populate_dls_single_ray_guard_residuals(
    Score& score,
    const std::vector<FieldSpot>& field_spots,
    const std::vector<FieldWavefront>& field_wavefronts,
    const MeritConfig& config) {
    score.dls_single_ray_spot_guard_residuals.clear();
    score.dls_single_ray_wavefront_guard_residuals.clear();
    for (const auto& contribution : score.contributions) {
        if (is_single_ray_spot_guard_operand_name(contribution.name)) {
            if (contribution.index >= field_spots.size()) {
                continue;
            }
            const SpotStats& stats = field_spots[contribution.index].stats;
            const auto& raw_residuals = stats.single_ray_spot_guard_residuals;
            if (raw_residuals.size() < 2 ||
                !std::isfinite(stats.rms_radius) ||
                std::abs(stats.rms_radius) <= 1.0e-12) {
                continue;
            }
            const double upper = single_ray_guard_upper_from_contribution(contribution);
            const double threshold = upper * std::max(0.0, std::abs(stats.rms_radius));
            const double tolerance =
                std::max({1.0e-9,
                          std::abs(config.spot_target),
                          std::abs(config.quality_spot_scale)});
            const double scale = dls_contribution_residual_scale(contribution, tolerance);
            const auto& failure_strengths =
                stats.single_ray_spot_guard_failure_strengths;
            for (std::size_t i = 0; i + 1 < raw_residuals.size(); i += 2) {
                double residual_x = 0.0;
                double residual_y = 0.0;
                const double dx = raw_residuals[i];
                const double dy = raw_residuals[i + 1];
                const double radius = std::hypot(dx, dy);
                const std::size_t ray_index = i / 2;
                const double failure_strength =
                    ray_index < failure_strengths.size() &&
                            std::isfinite(failure_strengths[ray_index])
                        ? std::max(0.0, failure_strengths[ray_index])
                        : 0.0;
                if (failure_strength > 0.0) {
                    // This is a normalized feasibility residual, not a fake
                    // image-plane intercept. It therefore remains finite and
                    // does not feed clear-aperture estimation.
                    residual_x = scale * tolerance * failure_strength;
                } else if (!std::isfinite(radius)) {
                    // A guard ray that cannot be traced must remain an active DLS
                    // violation.  Treating its NaN residual as zero made DLS
                    // prefer candidates whose exact pupil-edge rays disappeared.
                    residual_x = scale * std::max(
                        tolerance,
                        std::abs(threshold) + tolerance);
                } else if (radius > 1.0e-15) {
                    const double excess = radius - threshold;
                    if (std::isfinite(excess) && excess > 0.0) {
                        const double residual = scale * excess / radius;
                        residual_x = residual * dx;
                        residual_y = residual * dy;
                    }
                }
                score.dls_single_ray_spot_guard_residuals.push_back(residual_x);
                score.dls_single_ray_spot_guard_residuals.push_back(residual_y);
            }
            continue;
        }

        if (is_single_ray_wavefront_guard_operand_name(contribution.name)) {
            const double upper = single_ray_guard_upper_from_contribution(contribution);
            const double wavefront_tolerance =
                std::isfinite(config.quality_wavefront_scale) &&
                        config.quality_wavefront_scale > 0.0
                    ? config.quality_wavefront_scale
                    : MeritConfig{}.quality_wavefront_scale;
            const double scale =
                dls_contribution_residual_scale(contribution, wavefront_tolerance);
            for (const FieldWavefront& field_wavefront : field_wavefronts) {
                if (field_wavefront.field_index != contribution.index ||
                    field_wavefront.wavelength_index != contribution.term) {
                    continue;
                }
                const WavefrontStats& stats = field_wavefront.stats;
                if (!std::isfinite(stats.rms_waves) ||
                    std::abs(stats.rms_waves) <= 1.0e-12) {
                    continue;
                }
                const double threshold = upper * std::max(0.0, std::abs(stats.rms_waves));
                for (double opd_waves : stats.single_ray_wavefront_guard_residuals) {
                    double residual = 0.0;
                    const double magnitude = std::abs(opd_waves);
                    if (std::isfinite(magnitude)) {
                        const double excess = magnitude - threshold;
                        if (std::isfinite(excess) && excess > 0.0) {
                            residual = scale * excess * (opd_waves < 0.0 ? -1.0 : 1.0);
                        }
                    }
                    score.dls_single_ray_wavefront_guard_residuals.push_back(residual);
                }
            }
        }
    }
}

}  // namespace optiland
