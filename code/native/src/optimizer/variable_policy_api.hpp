#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double scale_factor(const VariableSpec& var);

double scale_offset(const VariableSpec& var);

bool is_glass_choice_variable(const VariableSpec& var);

bool is_vignetting_polish_variable(const VariableSpec& var);

bool is_frozen_polish_variable(
    const VariableSpec& var,
    const PolishOptions& options);

bool is_strict_bounded_variable(const VariableSpec& var);

double maybe_clip_continuous(
    const VariableSpec& var,
    double value,
    double lower,
    double upper);

bool continuous_value_requires_repair(
    const VariableSpec& var,
    double value,
    double lower,
    double upper);

} }  // namespace optiland::optimizer_detail
