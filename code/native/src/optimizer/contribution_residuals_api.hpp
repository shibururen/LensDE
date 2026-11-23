#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double contribution_delta(const MeritContribution& c);

double contribution_reference_for_quality(const MeritContribution& c, double fallback);

bool is_edge_like_manufacturing_constraint_token(const std::string& name);

bool is_ratio_like_manufacturing_constraint_token(const std::string& name);

bool is_surface_tangent_angle_constraint_token(const std::string& name);

double quality_tolerance_for_contribution(const MeritContribution& c, const MeritConfig& config);

double finite_or(double value, double fallback);

double positive_square(double value);

bool contribution_name_contains(const MeritContribution& c, const std::vector<std::string>& tokens);

double normalized_contribution_violation(
    const MeritContribution& c,
    const MeritConfig& config);

bool is_constraint_like_contribution(const MeritContribution& c);

bool contribution_mode_is_bound_like(const MeritContribution& c);

bool is_first_order_like_contribution(const MeritContribution& c);

} }  // namespace optiland::optimizer_detail
