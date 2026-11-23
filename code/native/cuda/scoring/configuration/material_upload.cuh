#pragma once
#include "../prelude.cuh"
#include "variable_kinds.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/optical_records.cuh"

namespace optiland {
namespace {
std::vector<DeviceSurface> make_device_surfaces(
    const LensSystem& base,
    int asphere_initial_root_mode) {
    std::vector<DeviceSurface> out;
    out.reserve(base.surfaces.size());
    for (std::size_t surface_index = 0; surface_index < base.surfaces.size(); ++surface_index) {
        const Surface& surface = base.surfaces[surface_index];
        DeviceSurface dst;
        dst.kind = surface_kind_to_device(surface.kind);
        dst.is_stop = surface.is_stop ? 1 : 0;
        dst.reflective = surface.reflective ? 1 : 0;
        dst.clear_aperture_fixed = surface.clear_aperture_fixed ? 1 : 0;
        dst.vignetting_aperture_excluded = surface.vignetting_aperture_excluded ? 1 : 0;
        dst.mechanical_aperture_fixed = surface.mechanical_aperture_fixed ? 1 : 0;
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
        // UC10 release: residual Horner, fused residual/derivative Horner and
        // qprime interval reuse.  Do not encode profiler surface identity or
        // any later UC12/UC14 experiment bits.
        if (asphere_initial_root_mode == 23) {
            dst.asphere_polynomial_eval_mode = 1 | 2 | 64;
        }
#else
        if (asphere_initial_root_mode == 12 ||
            asphere_initial_root_mode == 14 ||
            (asphere_initial_root_mode >= 15 &&
             asphere_initial_root_mode <= 31)) {
            dst.asphere_polynomial_eval_mode |= 1;
        }
        if (asphere_initial_root_mode == 13 ||
            asphere_initial_root_mode == 14 ||
            (asphere_initial_root_mode >= 15 &&
             asphere_initial_root_mode <= 31)) {
            dst.asphere_polynomial_eval_mode |= 2;
        }
        if (asphere_initial_root_mode == 16 ||
            asphere_initial_root_mode == 18) {
            dst.asphere_polynomial_eval_mode |= 4;
        }
        if (asphere_initial_root_mode == 17 ||
            asphere_initial_root_mode == 18) {
            dst.asphere_polynomial_eval_mode |= 8;
        }
        if (asphere_initial_root_mode == 20) {
            dst.asphere_polynomial_eval_mode |= 16;
        }
        if (asphere_initial_root_mode == 21 ||
            asphere_initial_root_mode == 22) {
            dst.asphere_polynomial_eval_mode |= 32;
        }
        if (asphere_initial_root_mode >= 23 &&
            asphere_initial_root_mode <= 31) {
            dst.asphere_polynomial_eval_mode |= 64;
        }
        if (asphere_initial_root_mode == 25) {
            dst.asphere_polynomial_eval_mode |= 128;
        }
        if (asphere_initial_root_mode == 27 ||
            asphere_initial_root_mode == 31) {
            dst.asphere_polynomial_eval_mode |=
                de_cuda_detail::kAsphereUc14RawPruneBit;
        }
        if (asphere_initial_root_mode == 28 ||
            asphere_initial_root_mode == 31) {
            dst.asphere_polynomial_eval_mode |=
                de_cuda_detail::kAsphereUc14DeferHiBit;
        }
        if (asphere_initial_root_mode == 29 ||
            asphere_initial_root_mode == 31) {
            dst.asphere_polynomial_eval_mode |=
                de_cuda_detail::kAsphereUc14KnownHiBit;
        }
        if (asphere_initial_root_mode == 30 ||
            asphere_initial_root_mode == 31) {
            dst.asphere_polynomial_eval_mode |=
                de_cuda_detail::kAsphereUc14PositionCertificateBit;
        }
        if (asphere_initial_root_mode >= 15 &&
            asphere_initial_root_mode <= 31 &&
            surface_index <
                static_cast<std::size_t>(kMaxAsphereRootProfileSurfaces)) {
            dst.asphere_polynomial_eval_mode |=
                (static_cast<int>(surface_index) + 1) << 8;
        }
#endif
        dst.radius = std::isfinite(surface.radius) ? surface.radius : kDeviceInf;
        dst.conic = surface.conic;
        dst.coeff_count = static_cast<int>(std::min<std::size_t>(
            surface.asphere_coefficients.size(),
            kMaxDeviceAsphereCoefficients));
        bool has_polynomial_departure = false;
        for (int i = 0; i < dst.coeff_count; ++i) {
            dst.coeffs[i] = surface.asphere_coefficients[static_cast<std::size_t>(i)];
            has_polynomial_departure =
                has_polynomial_departure || dst.coeffs[i] != 0.0;
        }
#if defined(OPTILAND_ASPHERE_UC10_RELEASE)
        if (has_polynomial_departure) {
            dst.asphere_polynomial_eval_mode |=
                de_cuda_detail::kAspherePolynomialDepartureBit;
        }
#endif
        dst.newton_max_iter = surface.newton_max_iter;
        dst.newton_tol = surface.newton_tol;
        dst.thickness = std::isfinite(surface.thickness) ? surface.thickness : kDeviceInf;
        dst.semi_aperture = surface.semi_aperture;
        dst.mechanical_aperture = surface.mechanical_aperture;
        dst.aperture_radius = surface.aperture_radius;
        dst.nd = surface.material_after.nd;
        dst.vd = surface.material_after.vd;
        dst.material_index_offset =
            static_cast<int>(surface_index) * static_cast<int>(kMaxDeviceWaves);
        out.push_back(dst);
    }
    return out;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
std::vector<double> make_device_material_indices(const LensSystem& base) {
    std::vector<double> out(
        base.surfaces.size() * static_cast<std::size_t>(kMaxDeviceWaves),
        std::numeric_limits<double>::quiet_NaN());
    const std::size_t wave_count = base.wavelengths.empty() ? 1 : base.wavelengths.size();
    for (std::size_t surface_index = 0; surface_index < base.surfaces.size(); ++surface_index) {
        const Material& material = base.surfaces[surface_index].material_after;
        for (std::size_t wave_index = 0;
             wave_index < wave_count && wave_index < static_cast<std::size_t>(kMaxDeviceWaves);
             ++wave_index) {
            const double wavelength_um = base.wavelengths.empty()
                                             ? 0.5875618
                                             : base.wavelengths[wave_index].value_um;
            out[surface_index * static_cast<std::size_t>(kMaxDeviceWaves) + wave_index] =
                material.refractive_index(wavelength_um);
        }
    }
    return out;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
bool make_device_variables(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext* glass_search,
    std::vector<DeviceVariable>* out,
    std::vector<DeviceGlassCandidate>* glass_candidates) {
    if (!out || !glass_candidates) {
        return false;
    }
    out->clear();
    glass_candidates->clear();
    out->reserve(variables.size());
    for (const VariableSpec& var : variables) {
        DeviceVariable dst;
        dst.kind = variable_kind_to_device(var.kind);
        dst.index = static_cast<int>(var.index);
        dst.term = static_cast<int>(var.term);
        dst.lower = var.lower;
        dst.upper = var.upper;
        if (var.kind == VariableKind::GlassChoice) {
            if (!glass_search || !glass_search->enabled) {
                return false;
            }
            const GlassChoiceGroup* group = nullptr;
            if (var.glass_group >= 0) {
                for (const GlassChoiceGroup& candidate_group : glass_search->groups) {
                    if (candidate_group.id == var.glass_group) {
                        group = &candidate_group;
                        break;
                    }
                }
            }
            if (!group) {
                for (const GlassChoiceGroup& candidate_group : glass_search->groups) {
                    if (candidate_group.surface_index == var.index) {
                        group = &candidate_group;
                        break;
                    }
                }
            }
            if (!group || group->candidates.empty()) {
                return false;
            }
            const std::size_t surface_index =
                group->surface_index < base.surfaces.size()
                    ? group->surface_index
                    : var.index;
            if (surface_index >= base.surfaces.size()) {
                return false;
            }
            dst.glass_surface_index = static_cast<int>(surface_index);
            dst.glass_candidate_offset = static_cast<int>(glass_candidates->size());
            dst.glass_candidate_count = static_cast<int>(group->candidates.size());
            dst.glass_initial_choice = std::clamp(
                static_cast<int>(std::llround(var.initial)),
                0,
                dst.glass_candidate_count - 1);
            dst.lower = 0.0;
            dst.upper = static_cast<double>(dst.glass_candidate_count - 1);
            for (const GlassCandidate& candidate : group->candidates) {
                DeviceGlassCandidate device_candidate;
                device_candidate.nd = candidate.material.nd;
                device_candidate.vd = candidate.material.vd;
                const std::size_t wave_count =
                    base.wavelengths.empty() ? 1 : base.wavelengths.size();
                for (std::size_t wave_index = 0;
                     wave_index < wave_count &&
                     wave_index < static_cast<std::size_t>(kMaxDeviceWaves);
                     ++wave_index) {
                    const double wavelength_um =
                        base.wavelengths.empty()
                            ? 0.5875618
                            : base.wavelengths[wave_index].value_um;
                    device_candidate.indices[wave_index] =
                        candidate.material.refractive_index(wavelength_um);
                }
                glass_candidates->push_back(device_candidate);
            }
        }
        out->push_back(dst);
    }
    return true;
}
} // namespace
} // namespace optiland
