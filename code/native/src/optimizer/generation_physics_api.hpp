#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double generation_surface_curvature(const Surface& surface);

double generation_material_after_index(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um);

double generation_material_before_index(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um);

double generation_paraxial_efl_estimate(const LensSystem& system, double wavelength_um);

std::vector<double> make_physics_constrained_generation_row(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    const PopulationGenerationOptions& options,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni);

} }  // namespace optiland::optimizer_detail
