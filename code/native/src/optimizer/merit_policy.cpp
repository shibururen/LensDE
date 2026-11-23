/*
 * Interpret merit/operand tokens and choose supported CUDA, residual and polish policies.
 * Evaluation and optimizer sessions share these capability checks before selecting their execution path.
 * Preserve token aliases and policy precedence so a configuration selects the same scorer and residual model.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

std::string normalized_token(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        if (ch == '-' || std::isspace(ch)) {
            return '_';
        }
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool distortion_model_allows_cuda(const MeritConfig& config) {
    const std::string model = normalized_token(config.distortion_model);
    return model.empty() || model == "auto" ||
           model == "f_tan_theta" || model == "ftantheta" ||
           model == "tan_theta" || model == "tantheta" ||
           model == "f_theta" || model == "ftheta" || model == "theta";
}

bool de_cuda_required() {
#ifdef OPTILAND_DE_CUDA_ONLY
    return true;
#else
    return std::getenv("OPTILAND_DE_CUDA_REQUIRE") != nullptr;
#endif
}

bool polish_uses_batch_merit(const PolishOptions& options) {
    const std::string method = normalized_token(options.method);
    return method == "cuda_dls" ||
           method == "batch_cuda" ||
           method == "cuda_batch" ||
           method == "batch_merit" ||
           method == "merit_batch";
}

bool polish_uses_trf(const PolishOptions& options) {
    const std::string method = normalized_token(options.method);
    return method == "trf" ||
           method == "trust_region" ||
           method == "trustregion" ||
           method == "trust_region_reflective" ||
           method == "trustregionreflective";
}

bool adaptive_quality_enabled(const MeritConfig& config) {
    const std::string model = normalized_token(config.merit_model);
    return model == "adaptive_quality" ||
           model == "quality" ||
           model == "qgso" ||
           model == "qgso_like" ||
           model == "auto";
}

bool unified_trace_gate_enabled(const MeritConfig& config) {
    const std::string model = normalized_token(config.merit_model);
    return model == "unified_trace_gate" ||
           model == "trace_guarded_adaptive" ||
           model == "unified_adaptive" ||
           model == "trace_guarded";
}

bool is_spot_contribution(const MeritContribution& contribution) {
    const std::string name = normalized_token(contribution.name);
    return (name == "spot" ||
            name == "mean_spot" ||
            name == "rms_spot" ||
            name == "mean_rms_spot") &&
           (contribution.source == "operand_field" || contribution.source == "legacy_field");
}

bool is_wavefront_contribution(const MeritContribution& contribution) {
    const std::string name = normalized_token(contribution.name);
    return (name == "wavefront" ||
            name == "rms_wavefront" ||
            name == "wavefront_rms" ||
            name == "mean_wavefront" ||
            name == "mean_rms_wavefront" ||
            name == "opd" ||
            name == "rms_opd" ||
            name == "opd_rms" ||
            name == "mean_opd_rms") &&
           contribution.source == "operand_field";
}

bool is_image_quality_contribution(const MeritContribution& contribution) {
    return is_spot_contribution(contribution) ||
           is_wavefront_contribution(contribution);
}

bool is_spot_operand_token(const std::string& name) {
    return name == "spot" ||
           name == "mean_spot" ||
           name == "rms_spot" ||
           name == "mean_rms_spot" ||
           name == "max_spot" ||
           name == "max_rms_spot";
}

bool is_wavefront_operand_token(const std::string& name) {
    return name == "wavefront" ||
           name == "rms_wavefront" ||
           name == "wavefront_rms" ||
           name == "mean_wavefront" ||
           name == "mean_rms_wavefront" ||
           name == "opd" ||
           name == "rms_opd" ||
           name == "opd_rms" ||
           name == "mean_opd_rms" ||
           name == "max_wavefront" ||
           name == "max_rms_wavefront" ||
           name == "max_wavefront_rms" ||
           name == "max_opd" ||
           name == "max_rms_opd" ||
           name == "max_opd_rms";
}

bool merit_prefers_wavefront_objective(const MeritConfig& config) {
    for (const MeritOperand& operand : config.operands) {
        const std::string name = normalized_token(operand.name);
        if (is_wavefront_operand_token(name)) {
            return true;
        }
        if (is_spot_operand_token(name)) {
            return false;
        }
    }
    return false;
}

}  // namespace optimizer_detail

}  // namespace optiland
