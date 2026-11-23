/*
 * Builds reference-pupil/exact-fallback lens variants and resolves vignetting options.
 * The automatic-vignetting coordinator and field-solver constructors use these helpers.
 * Wavelength selection, physical hard-aperture rules and APG worker settings preserve
 * the distinction between the caller prescription and temporary measurement models.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
double automatic_vignetting_hard_radius(const Surface& surface) {
    if (std::isfinite(surface.aperture_radius) && surface.aperture_radius > 0.0) {
        return surface.aperture_radius;
    }
    if (surface.clear_aperture_fixed &&
        std::isfinite(surface.semi_aperture) && surface.semi_aperture > 0.0) {
        return surface.semi_aperture;
    }
    return 0.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
LensSystem automatic_vignetting_exact_fallback_system(const LensSystem& system) {
    LensSystem fallback = system;
    fallback.ray_aiming.mode = RayAimingMode::AdaptivePilotGrid;
    fallback.ray_aiming.require_convergence = true;
    fallback.ray_aiming.adaptive_pilot_grid.enabled = true;
    fallback.ray_aiming.adaptive_pilot_grid.require_final_convergence = true;
    return fallback;
}
}  // namespace analysis_detail

namespace analysis_detail {
LensSystem automatic_vignetting_reference_pupil_system(
    const LensSystem& system) {
    LensSystem reference = system;
    for (Surface& surface : reference.surfaces) {
        if (surface.is_stop) {
            continue;
        }
        // The mapping that defines a mode's unvignetted reference pupil must
        // exist before the apertures being measured are applied.  Keep the
        // physical STOP, but remove all other hard clear-aperture gates from
        // this solver-local ray generator.  Feasibility is still evaluated
        // against the untouched system_ tracer below.
        surface.aperture_radius = 0.0;
        surface.clear_aperture_fixed = false;
    }
    return reference;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::size_t automatic_vignetting_wavelength_index(
    const LensSystem& system,
    const AutomaticVignettingConfig& config) {
    if (config.wavelength_index < system.wavelengths.size()) {
        return config.wavelength_index;
    }
    const auto primary = std::find_if(
        system.wavelengths.begin(),
        system.wavelengths.end(),
        [](const Wavelength& wave) { return wave.primary; });
    return primary != system.wavelengths.end()
               ? static_cast<std::size_t>(
                     std::distance(system.wavelengths.begin(), primary))
               : 0;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool automatic_vignetting_batch_apg_experiment_enabled() {
    const char* value =
        std::getenv("OPTILAND_SET_VIGNETTING_BATCH_APG_EXPERIMENT");
    if (!value) {
        return false;
    }
    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized != "" && normalized != "0" && normalized != "false" &&
           normalized != "off" && normalized != "no";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool automatic_vignetting_batch_apg_audit_enabled() {
    const char* value =
        std::getenv("OPTILAND_SET_VIGNETTING_BATCH_APG_AUDIT");
    if (!value) {
        return false;
    }
    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized != "" && normalized != "0" && normalized != "false" &&
           normalized != "off" && normalized != "no";
}
}  // namespace analysis_detail

namespace analysis_detail {
int automatic_vignetting_batch_apg_workers() {
    const char* value =
        std::getenv("OPTILAND_SET_VIGNETTING_BATCH_APG_WORKERS");
    if (!value) {
        return 1;
    }
    return std::max(1, std::atoi(value));
}
}  // namespace analysis_detail

namespace analysis_detail {
LensSystem automatic_vignetting_local_apg_worker_system(
    const LensSystem& system) {
    LensSystem worker = system;
    auto& cfg = worker.ray_aiming.adaptive_pilot_grid;
    // These are cross-request warm starts, not inputs to the current-field
    // chief/pilot model. Worker-local sessions freeze the APG problem to the
    // current field and wavelength so scheduling cannot change visible state.
    cfg.use_primary_wave_as_anchor = false;
    cfg.use_previous_field_chief = false;
    cfg.use_previous_field_extrapolation = false;
    cfg.use_previous_wave_seed = false;
    cfg.use_previous_field_seed = false;
    cfg.use_previous_pupil_seed = false;
    return worker;
}
}  // namespace analysis_detail

}  // namespace optiland
