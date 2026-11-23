/*
 * Prepare a lens-system copy for the active merit evaluation and aperture policy.
 * Optimizer scoring paths share this preparation before invoking analysis or backend evaluation.
 * Preserve the order of system, merit and dynamic-aperture adjustments applied to each candidate.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

LensSystem prepare_for_merit(LensSystem lens, const MeritConfig& config) {
    lens.primary_wavelength_apertures = config.vignetting_optimization_enabled();
    if (config.dynamic_apertures) {
        const int aperture_rings =
            config.vignetting_merit_aperture_envelope_experimental
                ? std::max(1, config.num_rays)
                : (config.clear_aperture_rings > 0
                       ? config.clear_aperture_rings
                       : config.num_rays);
        update_clear_apertures_from_trace(
            lens,
            aperture_rings,
            "gaussian_ring_arm",
            config.sampling_seed,
            lens.ray_aiming.mode,
            config.effective_clear_aperture_margin(),
            true,
            config.clear_aperture_arms,
            false,
            false,
            config.vignetting_boundary_support_tolerance,
            config.vignetting_boundary_support_selectivity_slope,
            config.vignetting_boundary_support_contract ==
                VignettingBoundarySupportContract::GlobalTrimRealizability,
            config.vignetting_physical_cut_match_enabled,
            config.vignetting_optimization_enabled());
    } else {
        sync_stop_aperture_to_aiming_radius(lens);
    }
    return lens;
}

}  // namespace optimizer_detail

}  // namespace optiland
