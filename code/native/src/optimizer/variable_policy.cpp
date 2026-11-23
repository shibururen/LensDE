/*
 * Classify optimizer variables and apply the selected continuous/discrete safety-bound policy.
 * Scaling, mutation and polish share these rules for glass choices, vignetting and frozen variables.
 * Preserve hard-domain versus optional-bound behavior before downstream coordinate conversion.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double scale_factor(const VariableSpec& var) {
    switch (var.kind) {
        case VariableKind::Radius:
            return 1.0 / 100.0;
        case VariableKind::Curvature:
            return 100.0;
        case VariableKind::Thickness:
            return 1.0 / 10.0;
        case VariableKind::AsphereCoeff:
            return std::pow(10.0, 4.0 + 2.0 * static_cast<double>(var.term));
        default:
            return 1.0;
    }
}

double scale_offset(const VariableSpec& var) {
    switch (var.kind) {
        case VariableKind::Radius:
        case VariableKind::Thickness:
            return -1.0;
        case VariableKind::MaterialNd:
            return -1.5;
        default:
            return 0.0;
    }
}

bool is_glass_choice_variable(const VariableSpec& var) {
    return var.kind == VariableKind::GlassChoice;
}

bool is_vignetting_polish_variable(const VariableSpec& var) {
    return var.kind == VariableKind::VignetteX ||
           var.kind == VariableKind::VignetteY ||
           var.kind == VariableKind::VignetteCenterY ||
           var.kind == VariableKind::VignetteMagnitudeY ||
           var.kind == VariableKind::VignetteAsymmetryY;
}

bool is_frozen_polish_variable(
    const VariableSpec& var,
    const PolishOptions& options) {
    return is_glass_choice_variable(var) ||
           (options.freeze_vignetting_variables &&
            is_vignetting_polish_variable(var));
}

bool is_strict_bounded_variable(const VariableSpec& var) {
    return is_glass_choice_variable(var) ||
           var.kind == VariableKind::VignetteX ||
           var.kind == VariableKind::VignetteY ||
           var.kind == VariableKind::VignetteCenterY ||
           var.kind == VariableKind::VignetteMagnitudeY ||
           var.kind == VariableKind::VignetteAsymmetryY;
}

double maybe_clip_continuous(
    const VariableSpec& var,
    double value,
    double lower,
    double upper) {
    if (is_strict_bounded_variable(var)) {
        return std::clamp(value, lower, upper);
    }
    return value;
}

bool continuous_value_requires_repair(
    const VariableSpec& var,
    double value,
    double lower,
    double upper) {
    return !std::isfinite(value) ||
           (is_strict_bounded_variable(var) &&
            (value < lower || value > upper));
}

}  // namespace optimizer_detail

}  // namespace optiland
