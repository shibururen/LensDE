/*
 * Applies early score failures after MeritEvaluation measures the optical fields.
 * The merit pipeline consults this stage before accumulating ordinary operands.
 * Image-sign and structural failures, soft-trace handling and fail_merit accounting
 * retain distinct outcomes rather than silently treating missing measurements as zero.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
bool MeritEvaluation::accept_trace_results() {
if (score.distortion_sign_flip_fields > 0) {
        const double failed = static_cast<double>(score.distortion_sign_flip_fields);
        score.trace_ok = false;
        score.message = "actual image height is opposite to ideal image height";
        score.trace_group_error = std::max(score.trace_group_error, failed);
        score.image_group_error = std::max(score.image_group_error, config.fail_merit);
        score.trace_penalty = config.fail_merit;
        score.uncapped_merit = config.fail_merit;
        score.merit_sum = config.fail_merit * config.fail_merit;
        score.merit_weight_sum = 1.0;
        score.merit = config.fail_merit;
        score.contributions.push_back(make_contribution(
            "failure",
            "distortion_sign_flip",
            "fail",
            failed,
            0.0,
            -kInf,
            0.0,
            config.fail_merit,
            config.fail_merit));
        return false;
    }

    if (config.reject_ray_path_structural_failures &&
        score.ray_path_structural_failure_fields > 0) {
        const double failed = static_cast<double>(score.ray_path_structural_failure_fields);
        score.trace_ok = false;
        score.message = "ray path structural failure";
        score.trace_group_error = std::max(score.trace_group_error, failed);
        score.image_group_error = std::max(score.image_group_error, config.fail_merit);
        score.trace_penalty = config.fail_merit;
        score.uncapped_merit = config.fail_merit;
        score.merit_sum = config.fail_merit * config.fail_merit;
        score.merit_weight_sum = 1.0;
        score.merit = config.fail_merit;
        score.contributions.push_back(make_contribution(
            "failure",
            "ray_path_structure",
            "fail",
            failed,
            0.0,
            -kInf,
            0.0,
            config.fail_merit,
            config.fail_merit));
        return false;
    }

    if (spot_failed || !std::isfinite(score.mean_spot)) {
        score.trace_ok = false;
        score.message = soft_trace ? "ray trace partially failed" : "ray trace failed";
        if (!soft_trace) {
            score.uncapped_merit = config.fail_merit;
            score.merit_sum = config.fail_merit * config.fail_merit;
            score.merit_weight_sum = 1.0;
            score.merit = config.fail_merit;
            score.contributions.push_back(make_contribution(
                "failure",
                "ray_trace",
                "fail",
                score.mean_spot,
                std::numeric_limits<double>::quiet_NaN(),
                -kInf,
                kInf,
                1.0,
                config.fail_merit));
            return false;
        }
    }

    if (need_wavefront && (wavefront_failed || !std::isfinite(score.mean_wavefront_rms))) {
        score.trace_ok = false;
        if (score.message.empty()) {
            score.message = soft_trace ? "wavefront calculation partially failed" : "wavefront calculation failed";
        }
        if (!soft_trace) {
            score.uncapped_merit = config.fail_merit;
            score.merit_sum = config.fail_merit * config.fail_merit;
            score.merit_weight_sum = 1.0;
            score.merit = config.fail_merit;
            score.contributions.push_back(make_contribution(
                "failure",
                "rms_wavefront",
                "fail",
                score.mean_wavefront_rms,
                std::numeric_limits<double>::quiet_NaN(),
                -kInf,
                kInf,
                1.0,
                config.fail_merit));
            return false;
        }
    }
    return true;
}
}  // namespace analysis_detail

}  // namespace optiland
