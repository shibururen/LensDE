#pragma once
#include "optiland/core/surfaces.hpp"
#include "optiland/core/fields.hpp"
#include "optiland/core/aiming.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace optiland {
inline constexpr const char* kDefaultAsphereRootPolicyToken = "unified_certified";

struct LensSystem {
    std::string name = "optiland-cpp";
    std::string glass_library = "SCHOTT";
    std::vector<std::string> glass_libraries;
    // Canonical token for the asphere intersection contract under which this
    // system was materialized. New systems use the shared UC10 default; an
    // explicitly serialized policy remains available for reproducible audits.
    std::string asphere_root_policy = kDefaultAsphereRootPolicyToken;
    Aperture aperture;
    double max_field_deg = 0.0;
    std::vector<Field> fields;
    std::vector<Wavelength> wavelengths;
    std::vector<Surface> surfaces;
    // -1 preserves the legacy definition (last pre-image surface).  A
    // non-negative S-index measures the physical back-working-distance path
    // from that surface through all following intervals to the image.
    int bfl_reference_surface = -1;
    RayAimingConfig ray_aiming;
    // Runtime merit policy; intentionally not serialized as a prescription.
    // Recomputed from MeritConfig whenever a scoring system is prepared.
    bool primary_wavelength_apertures = false;

    bool defer_secondary_aperture_clipping(double wavelength_um) const {
        return primary_wavelength_apertures &&
               std::abs(wavelength_um - primary_wavelength()) > 1.0e-10;
    }

    std::size_t first_optical_surface() const {
        return (!surfaces.empty() && surfaces.front().kind == SurfaceKind::Object) ? 1 : 0;
    }

    std::size_t first_ray_aiming_surface(double wavelength_um) const {
        const std::size_t first = first_optical_surface();
        if (first >= surfaces.size()) {
            return first;
        }
        const double wave = (std::isfinite(wavelength_um) && wavelength_um > 0.0)
                                ? wavelength_um
                                : 0.5875618;
        double n_before = 1.0;
        if (has_object_surface()) {
            const double object_n = surfaces.front().material_after.refractive_index(wave);
            if (std::isfinite(object_n) && object_n > kEps) {
                n_before = object_n;
            }
        }
        for (std::size_t i = first; i < surfaces.size(); ++i) {
            const auto& surface = surfaces[i];
            if (surface.kind == SurfaceKind::Image) {
                break;
            }
            const double n_after = surface.material_after.refractive_index(wave);
            if (!std::isfinite(n_after) || n_after <= kEps) {
                continue;
            }
            const double scale = std::max({1.0, std::abs(n_before), std::abs(n_after)});
            if (surface.reflective || std::abs(n_after - n_before) > 1.0e-9 * scale) {
                return i;
            }
            n_before = n_after;
        }
        return first;
    }

    bool has_object_surface() const {
        return !surfaces.empty() && surfaces.front().kind == SurfaceKind::Object;
    }

    double object_distance() const {
        return has_object_surface() ? surfaces.front().thickness : kInf;
    }

    double entrance_pupil_radius() const {
        if (aperture.type == ApertureType::EntrancePupilDiameter) {
            return 0.5 * aperture.value;
        }
        return 1.0;
    }

    double primary_wavelength() const {
        if (wavelengths.empty()) {
            return 0.5875618;
        }
        for (const auto& wave : wavelengths) {
            if (wave.primary) {
                return wave.value_um;
            }
        }
        return wavelengths.front().value_um;
    }

    double total_track() const {
        double sum = 0.0;
        for (std::size_t i = first_optical_surface(); i < surfaces.size(); ++i) {
            const auto& s = surfaces[i];
            if (std::isfinite(s.thickness)) {
                sum += s.thickness;
            }
        }
        return sum;
    }
};


}  // namespace optiland
