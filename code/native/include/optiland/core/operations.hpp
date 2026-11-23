#pragma once
#include "optiland/core/model.hpp"
#include "optiland/core/variables.hpp"
#include "optiland/core/glass.hpp"
#include "optiland/core/merit_config.hpp"
#include "optiland/core/trace.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace optiland {
LensSystem apply_variables(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values);

LensSystem apply_variables_row(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const double* values,
    std::size_t count);

LensSystem apply_mixed_variables(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values,
    const GlassSearchContext& glass_search);

LensSystem apply_mixed_variables_row(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const double* values,
    std::size_t count,
    const GlassSearchContext& glass_search);

std::vector<PupilPoint> make_hexapolar_distribution(int rings);
std::vector<std::size_t> relative_illumination_supervised_fields(
    const LensSystem& system, const MeritConfig& config);
bool is_codev_rectangular_distribution(const std::string& distribution);
double effective_codev_ray_interval(
    const LensSystem& system,
    const MeritConfig& config);
std::vector<PupilPoint> make_codev_rectangular_distribution(
    double ray_interval,
    double obscuration = 0.0,
    double aperture_weight = 0.0);
std::vector<PupilPoint> make_pupil_distribution(
    int num_points,
    const std::string& distribution,
    unsigned int seed = 1,
    int arms = 6);


}  // namespace optiland
