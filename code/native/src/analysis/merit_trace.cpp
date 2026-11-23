/*
 * Turns chief-ray, coverage, backward-path and partial-trace health into merit terms.
 * Runs after the initial measurement/failure phase and before image operands.
 * Hard requirements and soft penalties preserve different return paths and weights;
 * trace coverage must retain its original sampling and failed-ray interpretation.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
bool MeritEvaluation::append_trace_contributions() {
const bool hard_trace_gate_failed =
        (config.require_chief_ray && !score.chief_ray_ok) ||
        (config.require_off_axis_field_coverage && score.zero_coverage_off_axis_fields > 0);
    if (hard_trace_gate_failed) {
        score.trace_ok = false;
        score.message = !score.chief_ray_ok
                            ? "chief ray trace failed"
                            : "off-axis field has zero trace coverage";
        if (!soft_trace) {
            score.uncapped_merit = config.fail_merit;
            score.merit_sum = config.fail_merit * config.fail_merit;
            score.merit_weight_sum = 1.0;
            score.merit = config.fail_merit;
            return false;
        }
        if (config.require_chief_ray && !score.chief_ray_ok) {
            const double failed = static_cast<double>(score.chief_ray_failed_fields);
            add_contribution(make_contribution(
                "failure",
                "chief_ray",
                "required",
                failed,
                0.0,
                -kInf,
                0.0,
                config.hard_trace_gate_weight,
                std::abs(config.hard_trace_gate_weight) * failed * failed));
        }
        if (config.require_off_axis_field_coverage && score.zero_coverage_off_axis_fields > 0) {
            const double failed = static_cast<double>(score.zero_coverage_off_axis_fields);
            add_contribution(make_contribution(
                "failure",
                "off_axis_field_coverage",
                "greater_than",
                0.0,
                0.0,
                std::numeric_limits<double>::min(),
                kInf,
                config.hard_trace_gate_weight,
                std::abs(config.hard_trace_gate_weight) * failed * failed));
        }
    }

    for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
        const auto& stats = field_spots[field_index].stats;
        if (stats.backward_penalty <= 0.0) {
            continue;
        }
        add_contribution(make_contribution(
            "trace_field",
            "backward_propagation",
            "penalty",
            stats.backward_weight,
            0.0,
            -kInf,
            kInf,
            config.backward_failure_weight,
            stats.backward_penalty,
            field_index,
            0));
    }

    if (soft_trace) {
        const double system_deficit = std::max(0.0, config.min_system_coverage - score.trace_coverage);
        if (system_deficit > 0.0) {
            const double penalty = std::abs(config.coverage_failure_weight) * system_deficit * system_deficit;
            add_contribution(make_contribution(
                "trace",
                "system_coverage",
                "greater_than",
                score.trace_coverage,
                config.min_system_coverage,
                config.min_system_coverage,
                kInf,
                config.coverage_failure_weight,
                penalty));
        }
        for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
            const auto& stats = field_spots[field_index].stats;
            if (config.require_chief_ray && config.chief_progress_weight > 0.0 && !stats.chief_ok) {
                const double chief_deficit = std::max(0.0, 1.0 - stats.chief_progress);
                if (chief_deficit > 0.0) {
                    const double penalty = std::abs(config.chief_progress_weight) * chief_deficit * chief_deficit;
                    add_contribution(make_contribution(
                        "trace_field",
                        "chief_progress",
                        "greater_than",
                        stats.chief_progress,
                        1.0,
                        1.0,
                        kInf,
                        config.chief_progress_weight,
                        penalty,
                        field_index,
                        0));
                }
            }
            if (config.trace_progress_weight > 0.0 && stats.trace_progress < 1.0) {
                const double progress_deficit = std::max(0.0, 1.0 - stats.trace_progress);
                const double penalty = std::abs(config.trace_progress_weight) * progress_deficit * progress_deficit;
                add_contribution(make_contribution(
                    "trace_field",
                    "trace_progress",
                    "greater_than",
                    stats.trace_progress,
                    1.0,
                    1.0,
                    kInf,
                    config.trace_progress_weight,
                    penalty,
                    field_index,
                    0));
            }
            const double field_deficit = std::max(0.0, config.min_field_coverage - stats.coverage);
            if (field_deficit > 0.0) {
                const double penalty = std::abs(config.coverage_failure_weight) * field_deficit * field_deficit;
                add_contribution(make_contribution(
                    "trace_field",
                    "field_coverage",
                    "greater_than",
                    stats.coverage,
                    config.min_field_coverage,
                    config.min_field_coverage,
                    kInf,
                    config.coverage_failure_weight,
                    penalty,
                    field_index,
                    0));
            }
            if (stats.trace_penalty > 0.0 || stats.aim_penalty > 0.0) {
                const double penalty = stats.trace_penalty + stats.aim_penalty;
                add_contribution(make_contribution(
                    "trace_field",
                    "ray_failure",
                    "penalty",
                    stats.failed_weight,
                    0.0,
                    -kInf,
                    kInf,
                    1.0,
                    penalty,
                    field_index,
                    0));
            }
        }
    }
    return true;
}
}  // namespace analysis_detail

}  // namespace optiland
