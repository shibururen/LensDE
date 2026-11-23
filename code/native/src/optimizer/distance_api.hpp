#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<double> physical_to_scaled(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

double normalized_physical_value_for_distance(
    const LensSystem* system,
    const VariableSpec& var,
    double physical,
    double air_thickness_distance_cap);

double normalized_from_internal_for_distance(
    const LensSystem* system,
    const VariableSpec& var,
    double internal_value,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical,
    double air_thickness_distance_cap = 25.0);

std::vector<double> scaled_to_physical(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& scaled_values,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

void physical_values_from_scaled_row(
    const std::vector<VariableSpec>& variables,
    const double* scaled_values,
    std::size_t count,
    InternalScalingMode mode,
    std::vector<double>* physical_out);

double scaled_row_distance(
    const std::vector<double>& population,
    int a,
    int b,
    int dim);

double normalized_row_distance(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& population,
    int a,
    int b,
    int dim,
    DistanceMetric metric,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical,
    const LensSystem* system = nullptr,
    double air_thickness_distance_cap = 25.0);

double population_row_distance(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& population,
    int a,
    int b,
    int dim,
    DistanceMetric metric,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical,
    const LensSystem* system = nullptr,
    double air_thickness_distance_cap = 25.0);

double scaled_vector_distance(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& a,
    const std::vector<double>& b,
    DistanceMetric metric,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical,
    const LensSystem* system = nullptr,
    double air_thickness_distance_cap = 25.0);

std::vector<int> sorted_population_indices(const std::vector<double>& merits);

std::vector<int> select_diverse_indices(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& population,
    const std::vector<double>& merits,
    int dim,
    int count,
    double min_distance,
    DistanceMetric metric,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

} }  // namespace optiland::optimizer_detail
