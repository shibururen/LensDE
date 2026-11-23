#pragma once
#include "../prelude.cuh"
#include "../configuration/material_upload.cuh"
#include "../configuration/runtime_options.cuh"
#include "../configuration/supported_merit.cuh"
#include "../contracts/constants.cuh"
#include "execution_context.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::validate_inputs() {
    geometry_only = de_cuda_v2_geometry_only_enabled();

    full_scorer = de_cuda_v2_full_enabled();

    debug_reject = std::getenv("OPTILAND_DE_CUDA_SUPPORT_DEBUG") != nullptr;

    if ((!geometry_only && !full_scorer) || population_size <= 0 ||
        variables.empty() ||
        flat_population.size() <
            static_cast<std::size_t>(population_size) * variables.size() ||
        base.surfaces.empty()) {
        return {native_detail::PhaseFlow::Return, reject("disabled, empty inputs, or invalid population shape")};
    }

    if (!de_cuda_merit_supported(base, merit_config)) {
        return {native_detail::PhaseFlow::Return, reject("merit/system support check failed")};
    }

    if (base.surfaces.size() > 64) {
        return {native_detail::PhaseFlow::Return, reject("too many surfaces")};
    }

    field_count = base.fields.empty() ? 1 : base.fields.size();

    wavelength_count = base.wavelengths.empty() ? 1 : base.wavelengths.size();

    if (field_count > kMaxDeviceFields ||
        wavelength_count > kMaxDeviceWaves ||
        merit_config.glass_proximity.size() > kMaxDeviceGlassProximity) {
        return {native_detail::PhaseFlow::Return, reject("too many fields, wavelengths, or glass proximity constraints")};
    }

    glass_proximity_point_count = 0;

    for (const GlassProximityConstraint& constraint : merit_config.glass_proximity) {
        glass_proximity_point_count +=
            constraint.points.size() + constraint.region_points.size();
    }

    if (glass_proximity_point_count > kMaxDeviceGlassProximityPoints) {
        return {native_detail::PhaseFlow::Return, reject("too many glass proximity points")};
    }

    for (const Surface& surface : base.surfaces) {
        if (surface.reflective) {
            return {native_detail::PhaseFlow::Return, reject("reflective surfaces are unsupported")};
        }
        if (surface.asphere_coefficients.size() >
            static_cast<std::size_t>(kMaxDeviceAsphereCoefficients)) {
            return {native_detail::PhaseFlow::Return, reject("more than eight asphere coefficients are unsupported")};
        }
    }

    for (const VariableSpec& var : variables) {
        if (var.kind == VariableKind::AsphereCoeff &&
            var.term >= static_cast<std::size_t>(kMaxDeviceAsphereCoefficients)) {
            return {native_detail::PhaseFlow::Return, reject("asphere coefficient term is outside the CUDA limit")};
        }
    }

    if (!make_device_variables(
            base,
            variables,
            glass_search,
            &host_variables,
            &host_glass_candidates)) {
        return {native_detail::PhaseFlow::Return, reject("glass choice mapping or candidate table is invalid")};
    }

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
