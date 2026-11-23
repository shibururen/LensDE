#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

int glass_choice_lower(const VariableSpec& var);

int glass_choice_upper(const VariableSpec& var);

int clamp_glass_choice(const VariableSpec& var, double physical);

double scale_value(const VariableSpec& var, double value);

double inverse_scale_value(const VariableSpec& var, double scaled_value);

std::string variable_kind_name(VariableKind kind);

double asphere_reference_radius(const LensSystem& system, const VariableSpec& var);

bool uses_bound_normalized_coordinates(InternalScalingMode mode);

double frozen_asphere_radial_power(const VariableSpec& var);

bool has_edge_sag_transform(const VariableSpec& var, InternalScalingMode mode);

double asphere_sag_physical_step(const LensSystem& system, const VariableSpec& var);

double default_physical_finite_difference_step(
    const VariableSpec& var,
    double current_physical,
    double finite_difference);

double finite_difference_physical_step(
    const LensSystem& system,
    const VariableSpec& var,
    double current_physical,
    double finite_difference);

double finite_difference_step_scaled(
    const LensSystem& system,
    const VariableSpec& var,
    double current_scaled,
    double lower_scaled,
    double upper_scaled,
    double finite_difference,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical,
    double* physical_step_out = nullptr);

std::pair<double, double> scaled_bounds(const VariableSpec& var);

std::pair<double, double> internal_bounds(
    const VariableSpec& var,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

double internal_to_physical(
    const VariableSpec& var,
    double internal,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

double physical_to_internal(
    const VariableSpec& var,
    double physical,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

double physical_delta_to_internal(
    const VariableSpec& var,
    double physical_delta,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

} }  // namespace optiland::optimizer_detail
