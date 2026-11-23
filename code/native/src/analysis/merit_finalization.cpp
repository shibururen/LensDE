/*
 * Finalizes the scalar merit and DLS residual products after all contribution stages.
 * Selects the configured unified-gate, adaptive-quality or weighted aggregate path.
 * Trace-priority addition, non-finite failure conversion and fail_merit capping keep
 * the same reduction order and score/residual contract used by native optimizers.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void MeritEvaluation::finalize_score() {
score.trace_priority_metric = trace_priority_metric(score, config);
    score.trace_priority_penalty = 0.0;
    if (unified_trace_gate_enabled(config)) {
        append_unified_trace_contributions(score, config);
        apply_unified_trace_gate_merit(score, config, system);
        populate_dls_ray_spot_residuals(score, field_spots, config);
        populate_dls_single_ray_guard_residuals(score, field_spots, field_wavefronts, config);
        return;
    }

    if (adaptive_quality_enabled(config)) {
        if (soft_trace && (spot_failed || !std::isfinite(score.mean_spot))) {
            score.merit_sum = merit_sum;
            score.merit_weight_sum = merit_weight_sum;
            score.uncapped_merit = zemax_style_merit(merit_sum, merit_weight_sum);
        } else {
            const double adaptive_merit = adaptive_quality_merit(score, config);
            score.merit_sum = adaptive_merit * adaptive_merit;
            score.merit_weight_sum = 1.0;
            score.uncapped_merit = adaptive_merit;
        }
    } else {
        score.merit_sum = merit_sum;
        score.merit_weight_sum = merit_weight_sum;
        score.uncapped_merit = zemax_style_merit(merit_sum, merit_weight_sum);
    }
    if (coverage_first_merit_enabled(config) && config.trace_priority_weight > 0.0) {
        score.trace_priority_penalty =
            std::abs(config.trace_priority_weight) * score.trace_priority_metric;
        if (std::isfinite(score.uncapped_merit) && std::isfinite(score.trace_priority_penalty)) {
            score.uncapped_merit += score.trace_priority_penalty;
        } else {
            score.uncapped_merit = std::numeric_limits<double>::infinity();
        }
    }
    if (!std::isfinite(score.uncapped_merit)) {
        score.trace_ok = false;
        score.merit = config.fail_merit;
    } else {
        score.merit = std::min(score.uncapped_merit, config.fail_merit);
    }
    populate_dls_ray_spot_residuals(score, field_spots, config);
    populate_dls_single_ray_guard_residuals(score, field_spots, field_wavefronts, config);
}
}  // namespace analysis_detail

}  // namespace optiland
