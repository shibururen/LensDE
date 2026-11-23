/*
 * Constructs per-field/per-wavelength solver contexts and the physical hard-aperture list.
 * The vignetting coordinator supplies field indices, optional shared aiming and seeds.
 * Reference/fallback tracers and additional wavelength solvers must outlive all branch
 * probes while preserving the supplied lens tables and aperture-exclusion rules.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
AutomaticVignettingFieldSolver::AutomaticVignettingFieldSolver(
        const LensSystem& system,
        std::size_t field_index,
        std::size_t wave_index,
        const AutomaticVignettingConfig& config,
        AimingSession* shared_aiming ,
        std::vector<std::pair<double, double>> preferred_seeds ) : system_(system),
          tracer_(system_),
          reference_system_(
              automatic_vignetting_reference_pupil_system(system_)),
          reference_tracer_(reference_system_),
          aiming_(reference_system_),
          exact_fallback_system_(
              automatic_vignetting_exact_fallback_system(reference_system_)),
          exact_fallback_aiming_(exact_fallback_system_),
          field_index_(field_index),
          wave_index_(wave_index),
          config_(config),
          field_(system_.fields[field_index_]),
          wave_(system_.wavelengths[wave_index_]),
          shared_aiming_(shared_aiming),
          preferred_seeds_(std::move(preferred_seeds)) {
        initialize_hard_apertures();
    }

AutomaticVignettingFieldSolver::AutomaticVignettingFieldSolver(
        const LensSystem& system,
        std::size_t field_index,
        const std::vector<std::size_t>& wave_indices,
        const AutomaticVignettingConfig& config,
        AimingSession* shared_aiming ,
        std::vector<std::pair<double, double>> preferred_seeds ) : AutomaticVignettingFieldSolver(
              system,
              field_index,
              wave_indices.empty() ? 0 : wave_indices.front(),
              config,
              shared_aiming,
              preferred_seeds) {
        for (std::size_t position = 1; position < wave_indices.size(); ++position) {
            additional_wave_solvers_.push_back(
                std::make_unique<AutomaticVignettingFieldSolver>(
                    system,
                    field_index,
                    wave_indices[position],
                    config,
                    shared_aiming,
                    preferred_seeds));
        }
    }

void AutomaticVignettingFieldSolver::initialize_hard_apertures() {
        hard_apertures_.clear();
        for (std::size_t surface_index = system_.first_optical_surface();
             surface_index < system_.surfaces.size();
             ++surface_index) {
            const Surface& surface = system_.surfaces[surface_index];
            if (surface.kind == SurfaceKind::Object ||
                surface.kind == SurfaceKind::Image) {
                continue;
            }
            const double hard_radius =
                automatic_vignetting_hard_radius(surface);
            if (hard_radius > 0.0) {
                hard_apertures_.push_back({surface_index, hard_radius});
            }
        }
    }
}  // namespace analysis_detail

}  // namespace optiland
