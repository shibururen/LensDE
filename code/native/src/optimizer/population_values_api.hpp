#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double population_stddev(const std::vector<double>& values);

double population_mean_abs(const std::vector<double>& values);

std::vector<double> population_row(const std::vector<double>& population, int index, int dim);

void set_population_row(
    std::vector<double>& population,
    int index,
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables,
    InternalScalingMode mode = InternalScalingMode::LegacyOptical);

bool thickness_variable_is_glass_space(const LensSystem& system, const VariableSpec& var);

bool is_material_variable(const VariableSpec& var);

} }  // namespace optiland::optimizer_detail
