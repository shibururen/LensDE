#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "supported_ray_generation.cuh"
#include "../ray/calibrated_generation.cuh"
#include "../ray/sequential_trace.cuh"
#include "../sampling/pupil_nodes.cuh"

namespace optiland {
namespace {
__device__ double d_field_angle_rad(const DeviceConfig& cfg, const DeviceField& field) {
    return fabs(cfg.max_field_deg) * (3.14159265358979323846 / 180.0) *
           hypot(field.hx, field.hy);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_field_transverse_unit(const DeviceField& field, double* ux, double* uy) {
    const double field_radius = hypot(field.hx, field.hy);
    if (field_radius <= kDeviceEps) {
        *ux = 0.0;
        *uy = 0.0;
        return;
    }
    *ux = field.hx / field_radius;
    *uy = field.hy / field_radius;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_image_height_side_flipped(double ideal_height, double spot_height) {
    if (!d_finite(ideal_height) || !d_finite(spot_height)) {
        return false;
    }
    const double ideal_scale = fabs(ideal_height);
    if (ideal_scale <= 1.0e-12) {
        return false;
    }
    return ideal_height * spot_height < -1.0e-9 * ideal_scale * ideal_scale;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_paraxial_reference_focal_length(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    const double max_field = fabs(cfg.max_field_deg);
    if (max_field <= kDeviceEps) {
        return NAN;
    }
    const double sample_deg = fmin(0.1, max_field);
    if (sample_deg <= kDeviceEps) {
        return NAN;
    }
    DeviceField field;
    field.hy = sample_deg / max_field;
    DeviceRay ray = d_generate_supported_ray(
        surfaces,
        cfg,
        field,
        0.0,
        0.0,
        1.0,
        cfg.primary_wavelength);
    d_trace_ray(surfaces, cfg, &ray);
    if (!ray.valid || !d_finite(ray.y)) {
        return NAN;
    }
    const double tangent = tan(sample_deg * (3.14159265358979323846 / 180.0));
    if (fabs(tangent) <= kDeviceEps) {
        return NAN;
    }
    return fabs(ray.y / tangent);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_populate_chief_metrics(
    const DeviceSurface* surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* calibrated_models,
    const DeviceChiefRaySample* chief_samples,
    const DeviceConfig& cfg,
    CudaScoreSummary* summary) {
    if (!summary || !fields || !waves || cfg.field_count <= 0 ||
        cfg.wavelength_count <= 0) {
        return;
    }
    int primary_index = cfg.primary_wave_index;
    if (primary_index < 0 || primary_index >= cfg.wavelength_count) {
        primary_index = 0;
    }
    const DeviceWave reference_wave = waves[primary_index];
    const double reference_wave_um = reference_wave.value_um;
    double reference_focal_length = d_paraxial_reference_focal_length(surfaces, cfg);
    if (!d_finite(reference_focal_length) || reference_focal_length <= kDeviceEps) {
        reference_focal_length = fabs(summary->efl);
    }

    double max_distortion = 0.0;
    double max_chief_ray_angle = 0.0;
    double max_lateral_color = 0.0;
    bool have_distortion = false;
    bool have_chief_ray_angle = false;
    bool have_lateral_color = false;
    bool have_lateral_reference = false;
    int distortion_sign_flip_fields = 0;
    bool chiefs_complete = true;
    bool references_complete = true;

    for (int fi = 0; fi < cfg.field_count; ++fi) {
        const DeviceField field = fields[fi];
        const double angle_rad = d_field_angle_rad(cfg, field);
        const double tan_radius = tan(angle_rad);
        double ux = 0.0;
        double uy = 0.0;
        d_field_transverse_unit(field, &ux, &uy);
        bool field_sign_flip = false;

        double reference_x = NAN;
        double reference_y = NAN;
        bool have_reference = false;
        const long long field_wave_base =
            static_cast<long long>(fi) * cfg.wavelength_count;
        const DeviceChiefRaySample* reference_sample =
            chief_samples ? chief_samples + field_wave_base + primary_index
                          : nullptr;
        if (reference_sample && reference_sample->traced) {
            have_reference =
                reference_sample->valid && d_finite(reference_sample->image_x) &&
                d_finite(reference_sample->image_y);
            if (have_reference) {
                reference_x = reference_sample->image_x;
                reference_y = reference_sample->image_y;
            }
        } else {
            DeviceRay reference_ray = cfg.aiming_mode == 5
                                          ? d_generate_calibrated_chief_ray(
                                                surfaces,
                                                cfg,
                                                field,
                                                1.0,
                                                reference_wave_um,
                                                calibrated_models
                                                    ? calibrated_models + field_wave_base + primary_index
                                                    : nullptr)
                                          : d_generate_supported_ray(
                                                surfaces,
                                                cfg,
                                                field,
                                                0.0,
                                                0.0,
                                                1.0,
                                                reference_wave_um,
                                                nullptr);
            d_trace_ray(surfaces, cfg, &reference_ray);
            have_reference =
                reference_ray.valid && d_finite(reference_ray.x) &&
                d_finite(reference_ray.y);
            if (have_reference) {
                reference_x = reference_ray.x;
                reference_y = reference_ray.y;
            }
        }
        references_complete = references_complete && have_reference;
        if (have_reference) {
            have_lateral_reference = true;
        }

        for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
            const DeviceWave wave = waves[wi];
            if (wave.weight <= 0.0) {
                continue;
            }
            DeviceRay ray;
            double input_component = NAN;
            const DeviceChiefRaySample* cached_sample =
                chief_samples ? chief_samples + field_wave_base + wi : nullptr;
            if (cached_sample && cached_sample->traced) {
                ray.x = cached_sample->image_x;
                ray.y = cached_sample->image_y;
                ray.l = cached_sample->image_l;
                ray.m = cached_sample->image_m;
                ray.n = cached_sample->image_n;
                ray.valid = cached_sample->valid;
                input_component = cached_sample->input_component;
            } else {
                ray = cfg.aiming_mode == 5
                          ? d_generate_calibrated_chief_ray(
                                surfaces, cfg, field, 1.0, wave.value_um,
                                calibrated_models
                                    ? calibrated_models + field_wave_base + wi
                                    : nullptr)
                          : d_generate_supported_ray(
                                surfaces, cfg, field, 0.0, 0.0, 1.0, wave.value_um);
                input_component = ray.l * ux + ray.m * uy;
                d_trace_ray(surfaces, cfg, &ray);
            }
            if (!ray.valid) {
                chiefs_complete = false;
                continue;
            }

            // CRA, distortion and color consume exactly the same center ray.
            const double chief_angle =
                (180.0 / 3.14159265358979323846) *
                atan2(hypot(ray.l, ray.m), fabs(ray.n));
            if (d_finite(chief_angle)) {
                max_chief_ray_angle = fmax(max_chief_ray_angle, chief_angle);
                have_chief_ray_angle = true;
            }

            if (have_reference &&
                fabs(wave.value_um - reference_wave_um) > 1.0e-12 &&
                d_finite(ray.x) &&
                d_finite(ray.y)) {
                const double color = hypot(ray.x - reference_x, ray.y - reference_y);
                if (d_finite(color)) {
                    max_lateral_color = fmax(max_lateral_color, color);
                    have_lateral_color = true;
                }
            }

            const double ideal_field_radius =
                cfg.distortion_use_f_theta ? angle_rad : tan_radius;
            if (ideal_field_radius <= 1.0e-12 ||
                !d_finite(reference_focal_length) ||
                reference_focal_length <= kDeviceEps) {
                continue;
            }
            const double ideal_magnitude =
                reference_focal_length * ideal_field_radius;
            if (!d_finite(ideal_magnitude) || ideal_magnitude <= 1.0e-12) {
                continue;
            }
            const double sign = input_component < 0.0 ? -1.0 : 1.0;
            const double ideal_height = sign * ideal_magnitude;
            const double spot_height = ray.x * ux + ray.y * uy;
            if (d_image_height_side_flipped(ideal_height, spot_height)) {
                field_sign_flip = true;
            }
            const double distortion =
                (spot_height - ideal_height) / fabs(ideal_height);
            if (d_finite(distortion)) {
                max_distortion = fmax(max_distortion, fabs(distortion));
                have_distortion = true;
            }
        }
        if (field_sign_flip) {
            ++distortion_sign_flip_fields;
        }
    }

    if (have_distortion) {
        summary->max_distortion = max_distortion;
    } else if (have_chief_ray_angle) {
        summary->max_distortion = 0.0;
    } else {
        summary->max_distortion = NAN;
    }
    summary->max_chief_ray_angle =
        have_chief_ray_angle ? max_chief_ray_angle : NAN;
    if (have_lateral_color) {
        summary->lateral_color = max_lateral_color;
    } else if (have_lateral_reference) {
        summary->lateral_color = 0.0;
    } else {
        summary->lateral_color = NAN;
    }
    summary->distortion_sign_flip_fields = distortion_sign_flip_fields;
    if (!chiefs_complete) {
        summary->max_distortion = NAN;
        summary->max_chief_ray_angle = NAN;
    }
    if (!chiefs_complete || !references_complete) {
        summary->lateral_color = NAN;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kApertureBoundaryArmCount = 4;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kApertureGuardCount = 1 + kApertureBoundaryArmCount;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kApertureTraceProbeCount = kApertureGuardCount;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kApertureGuardCenterOffset = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kApertureGuardPositiveXOffset = 1;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kApertureGuardPositiveYOffset = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kApertureGuardNegativeXOffset = 3;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kApertureGuardNegativeYOffset = 4;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMeridionalBoundaryCount = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMeridionalChiefTrace = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMeridionalRadialTraceCount = 3;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMeridionalSignedTraceOffset = kMeridionalRadialTraceCount;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMeridionalSupportTraceCount = 2 * kMeridionalRadialTraceCount;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ int d_shadow_aperture_pupil_count(
    const DeviceConfig& aperture_cfg) {
    return d_pupil_count(aperture_cfg);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ void d_shadow_aperture_pupil_at(
    const DeviceConfig& aperture_cfg,
    int pupil_index,
    double* pupil_x,
    double* pupil_y,
    double* pupil_weight) {
    d_pupil_at(
        aperture_cfg,
        pupil_index,
        pupil_x,
        pupil_y,
        pupil_weight);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_aperture_guard_pupil(
    int guard_offset,
    double* pupil_x,
    double* pupil_y) {
    if (guard_offset == kApertureGuardCenterOffset) {
        *pupil_x = 0.0;
        *pupil_y = 0.0;
        return;
    }
    const int arm = min(
        max(guard_offset - 1, 0),
        kApertureBoundaryArmCount - 1);
    const double theta =
        2.0 * 3.14159265358979323846 * static_cast<double>(arm) /
        static_cast<double>(kApertureBoundaryArmCount);
    *pupil_x = cos(theta);
    *pupil_y = sin(theta);
}
} // namespace
} // namespace optiland
