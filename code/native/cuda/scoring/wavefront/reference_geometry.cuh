#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "../merit/residual_primitives.cuh"
#include "../ray/asphere_intersection.cuh"
#include "../ray/intersection_gate.cuh"

namespace optiland {
namespace {
struct DeviceWavefrontReference {
    double center_x = NAN;
    double center_y = NAN;
    double center_z = NAN;
    double radius = NAN;
    int valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceWavefrontObjectReference {
    double x = NAN;
    double y = NAN;
    double z = NAN;
    double n_object = 1.0;
    double chief_offset = 0.0;
    int valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_exit_pupil_location(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    const int stop = d_stop_index(surfaces, cfg);
    if (stop < 0 || stop >= cfg.surface_count || stop + 1 >= cfg.surface_count) {
        return 0.0;
    }
    double y = 0.0;
    double u = 0.1;
    double z = d_surface_vertex_z(surfaces, cfg.surface_count, stop);
    for (int i = stop + 1; i < cfg.surface_count; ++i) {
        const double vertex_z = d_surface_vertex_z(surfaces, cfg.surface_count, i);
        y += (vertex_z - z) * u;
        z = vertex_z;
        d_paraxial_refract_forward(surfaces, cfg, i, wavelength_um, y, &u);
    }
    if (fabs(u) <= kDeviceEps || !d_finite(y) || !d_finite(u)) {
        return 0.0;
    }
    return -y / u;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_image_space_index(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    if (cfg.surface_count <= 0) {
        return 1.0;
    }
    int index = cfg.surface_count - 1;
    if (surfaces[index].kind == kKindImage && index > cfg.first_optical_surface) {
        --index;
    }
    const double n = d_surface_refractive_index(surfaces[index], cfg, wavelength_um);
    return d_finite(n) && fabs(n) > kDeviceEps ? n : 1.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_image_to_reference_sphere_opd(
    double x,
    double y,
    double z,
    double l,
    double m,
    double n,
    const DeviceWavefrontReference& reference,
    double n_image,
    double* opd_image_to_sphere) {
    if (!opd_image_to_sphere || !reference.valid ||
        !d_finite(reference.radius) || reference.radius <= kDeviceEps ||
        !d_finite(n_image) || fabs(n_image) <= kDeviceEps) {
        return false;
    }
    const double bx = -l;
    const double by = -m;
    const double bz = -n;
    const double rx = x - reference.center_x;
    const double ry = y - reference.center_y;
    const double rz = z - reference.center_z;
    const double a = bx * bx + by * by + bz * bz;
    const double b = 2.0 * (bx * rx + by * ry + bz * rz);
    const double c = rx * rx + ry * ry + rz * rz -
                     reference.radius * reference.radius;
    if (!d_finite(a) || a <= kDeviceEps || !d_finite(b) || !d_finite(c)) {
        return false;
    }
    double discriminant = b * b - 4.0 * a * c;
    if (!d_finite(discriminant) || discriminant < -1.0e-9) {
        return false;
    }
    discriminant = fmax(0.0, discriminant);
    const double root = sqrt(discriminant);
    const double denom = 2.0 * a;
    double t = (-b - root) / denom;
    if (t < 0.0) {
        t = (-b + root) / denom;
    }
    if (!d_finite(t) || t < 0.0) {
        return false;
    }
    *opd_image_to_sphere = n_image * t;
    return d_finite(*opd_image_to_sphere);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceWavefrontObjectReference d_make_wavefront_object_reference(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceChiefRaySample& chief,
    double wavelength_um) {
    DeviceWavefrontObjectReference reference;
    if (cfg.surface_count <= 0 ||
        (surfaces[0].kind == kKindObject &&
         d_effectively_finite_distance(surfaces[0].thickness))) {
        return reference;
    }
    const int first = cfg.first_optical_surface;
    if (first < 0 || first >= cfg.surface_count ||
        !d_finite(chief.initial_x) || !d_finite(chief.initial_y) ||
        !d_finite(chief.initial_z) || !d_finite(chief.initial_l) ||
        !d_finite(chief.initial_m) || !d_finite(chief.initial_n)) {
        return reference;
    }
    const double vertex_z = d_surface_vertex_z(surfaces, cfg.surface_count, first);
    double t = 0.0;
    const DeviceSurface& first_surface = surfaces[first];
    const bool intersection_ok =
        first_surface.kind == kKindEvenAsphere
            ? d_intersect_even_asphere_configured(
                  first_surface,
                  cfg,
                  chief.initial_x,
                  chief.initial_y,
                  chief.initial_z - vertex_z,
                  chief.initial_l,
                  chief.initial_m,
                  chief.initial_n,
                  false,
                  cfg.asphere_auxiliary_local_branch != 0,
                  false,
                  nullptr,
                  &t)
            : d_intersect_surface(
                  first_surface,
                  chief.initial_x,
                  chief.initial_y,
                  chief.initial_z - vertex_z,
                  chief.initial_l,
                  chief.initial_m,
                  chief.initial_n,
                  &t);
    if (!intersection_ok) {
        return reference;
    }
    reference.x = chief.initial_x + chief.initial_l * t;
    reference.y = chief.initial_y + chief.initial_m * t;
    reference.z = chief.initial_z + chief.initial_n * t;
    double n_object = 1.0;
    if (surfaces[0].kind == kKindObject) {
        n_object = d_surface_refractive_index(surfaces[0], cfg, wavelength_um);
        if (!d_finite(n_object) || fabs(n_object) <= kDeviceEps) {
            n_object = 1.0;
        }
    }
    reference.n_object = n_object;
    reference.chief_offset = n_object *
        ((reference.x - chief.initial_x) * chief.initial_l +
         (reference.y - chief.initial_y) * chief.initial_m +
         (reference.z - chief.initial_z) * chief.initial_n);
    reference.valid = 1;
    return reference;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ double d_object_reference_offset(
    const DeviceWavefrontObjectReference& reference,
    const DeviceChiefRaySample& chief,
    double sample_x,
    double sample_y,
    double sample_z) {
    if (!reference.valid) {
        return 0.0;
    }
    return reference.n_object *
           ((reference.x - sample_x) * chief.initial_l +
            (reference.y - sample_y) * chief.initial_m +
            (reference.z - sample_z) * chief.initial_n);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceWavefrontReference d_make_wavefront_reference(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceChiefRaySample* chiefs) {
    DeviceWavefrontReference reference;
    if (!chiefs || cfg.primary_wave_index < 0 ||
        cfg.primary_wave_index >= cfg.wavelength_count) {
        return reference;
    }
    const DeviceChiefRaySample& chief = chiefs[cfg.primary_wave_index];
    if (!chief.usable || !d_finite(chief.image_x) || !d_finite(chief.image_y) ||
        !d_finite(chief.image_z)) {
        return reference;
    }
    const double image_z =
        d_surface_vertex_z(surfaces, cfg.surface_count, cfg.surface_count - 1);
    const double pupil_z = image_z +
        d_exit_pupil_location(surfaces, cfg, cfg.primary_wavelength);
    const double radius = sqrt(
        chief.image_x * chief.image_x + chief.image_y * chief.image_y +
        (chief.image_z - pupil_z) * (chief.image_z - pupil_z));
    if (!d_finite(radius) || radius <= kDeviceEps) {
        return reference;
    }
    reference.center_x = chief.image_x;
    reference.center_y = chief.image_y;
    reference.center_z = chief.image_z;
    reference.radius = radius;
    reference.valid = 1;
    return reference;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_wavefront_opd_waves(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceWave& wave,
    const DeviceChiefRaySample& chief,
    const DeviceSpotRaySample& sample,
    const DeviceWavefrontReference& reference,
    const DeviceWavefrontObjectReference& object_reference,
    double* opd_waves) {
    if (!opd_waves || !chief.usable || !sample.usable ||
        !d_finite(chief.opd) || !d_finite(sample.opd) ||
        !d_finite(sample.l) || !d_finite(sample.m) || !d_finite(sample.n)) {
        return false;
    }
    const double n_image = d_image_space_index(surfaces, cfg, wave.value_um);
    double chief_image_opd = 0.0;
    double sample_image_opd = 0.0;
    if (!d_image_to_reference_sphere_opd(
            chief.image_x, chief.image_y, chief.image_z,
            chief.image_l, chief.image_m, chief.image_n,
            reference, n_image, &chief_image_opd) ||
        !d_image_to_reference_sphere_opd(
            sample.x, sample.y, sample.z,
            sample.l, sample.m, sample.n,
            reference, n_image, &sample_image_opd)) {
        return false;
    }
    const double chief_object_offset = object_reference.valid
        ? object_reference.chief_offset
        : 0.0;
    const double sample_object_offset = d_object_reference_offset(
        object_reference, chief,
        sample.initial_x, sample.initial_y, sample.initial_z);
    const double opd_ref = chief.opd - chief_object_offset - chief_image_opd;
    const double opd = sample.opd - sample_object_offset - sample_image_opd;
    const double wavelength_mm = fmax(1.0e-12, wave.value_um * 1.0e-3);
    *opd_waves = (opd_ref - opd) / wavelength_mm;
    return d_finite(*opd_waves);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_solve_wavefront_normal(
    double a[kMaxDeviceWaves + 2][kMaxDeviceWaves + 3],
    int n,
    double* solution) {
    if (!solution || n <= 0 || n > kMaxDeviceWaves + 2) {
        return false;
    }
    double scale = 0.0;
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            scale = fmax(scale, fabs(a[r][c]));
        }
    }
    const double pivot_tolerance = fmax(1.0e-14, scale * 1.0e-12);
    for (int col = 0; col < n; ++col) {
        int pivot_row = col;
        double pivot_abs = fabs(a[col][col]);
        for (int r = col + 1; r < n; ++r) {
            const double candidate = fabs(a[r][col]);
            if (candidate > pivot_abs) {
                pivot_abs = candidate;
                pivot_row = r;
            }
        }
        if (!d_finite(pivot_abs) || pivot_abs <= pivot_tolerance) {
            return false;
        }
        if (pivot_row != col) {
            for (int c = col; c <= n; ++c) {
                const double tmp = a[pivot_row][c];
                a[pivot_row][c] = a[col][c];
                a[col][c] = tmp;
            }
        }
        const double pivot = a[col][col];
        for (int c = col; c <= n; ++c) {
            a[col][c] /= pivot;
        }
        a[col][col] = 1.0;
        for (int r = 0; r < n; ++r) {
            if (r == col) {
                continue;
            }
            const double factor = a[r][col];
            if (fabs(factor) <= pivot_tolerance) {
                a[r][col] = 0.0;
                continue;
            }
            for (int c = col; c <= n; ++c) {
                a[r][c] -= factor * a[col][c];
            }
            a[r][col] = 0.0;
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!d_finite(a[i][n])) {
            return false;
        }
        solution[i] = a[i][n];
    }
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_fit_wavefront_model(
    const DeviceSurface* surfaces,
    const DeviceWave* waves,
    const DeviceConfig& cfg,
    const DeviceSpotRaySample* samples,
    int pupil_stride,
    int active_pupil_count,
    const DeviceChiefRaySample* chiefs,
    const DeviceWavefrontReference& reference,
    const DeviceWavefrontObjectReference* object_references,
    bool use_dir_x,
    bool use_dir_y,
    int* piston_columns,
    int* dir_x_column,
    int* dir_y_column,
    double* coefficients,
    int* coefficient_count) {
    const int stop_surface_index = d_stop_index(surfaces, cfg);
    for (int wi = 0; wi < kMaxDeviceWaves; ++wi) {
        piston_columns[wi] = -1;
    }
    int columns = 0;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        bool active = false;
        for (int pi = 0; pi < active_pupil_count && !active; ++pi) {
            const DeviceSpotRaySample& sample = samples[wi * pupil_stride + pi];
            if (d_calibrated_paraxial_stop_sampling_rejection(
                    sample, cfg, stop_surface_index)) {
                continue;
            }
            const double sample_weight = d_wavefront_sample_weight(sample);
            double opd_waves = NAN;
            active = sample_weight > 0.0 && d_wavefront_opd_waves(
                surfaces, cfg, waves[wi], chiefs[wi], sample, reference,
                object_references[wi], &opd_waves);
        }
        if (active) {
            piston_columns[wi] = columns++;
        }
    }
    if (columns == 0) {
        return false;
    }
    *dir_x_column = use_dir_x ? columns++ : -1;
    *dir_y_column = use_dir_y ? columns++ : -1;
    if (columns > kMaxDeviceWaves + 2) {
        return false;
    }
    double normal[kMaxDeviceWaves + 2][kMaxDeviceWaves + 3] = {};
    int fit_sample_count = 0;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        const int piston = piston_columns[wi];
        if (piston < 0) {
            continue;
        }
        for (int pi = 0; pi < active_pupil_count; ++pi) {
            const DeviceSpotRaySample& sample = samples[wi * pupil_stride + pi];
            if (d_calibrated_paraxial_stop_sampling_rejection(
                    sample, cfg, stop_surface_index)) {
                continue;
            }
            const double sample_weight = d_wavefront_sample_weight(sample);
            double opd_waves = NAN;
            if (sample_weight <= 0.0 || !d_wavefront_opd_waves(
                    surfaces, cfg, waves[wi], chiefs[wi], sample,
                    reference, object_references[wi], &opd_waves)) {
                continue;
            }
            const int sample_columns[3] = {piston, *dir_x_column, *dir_y_column};
            const double values[3] = {1.0, sample.l, sample.m};
            for (int i = 0; i < 3; ++i) {
                if (sample_columns[i] < 0) {
                    continue;
                }
                const int ci = sample_columns[i];
                normal[ci][columns] += sample_weight * values[i] * opd_waves;
                for (int j = 0; j < 3; ++j) {
                    if (sample_columns[j] >= 0) {
                        normal[ci][sample_columns[j]] +=
                            sample_weight * values[i] * values[j];
                    }
                }
            }
            ++fit_sample_count;
        }
    }
    if (fit_sample_count < columns ||
        !d_solve_wavefront_normal(normal, columns, coefficients)) {
        return false;
    }
    *coefficient_count = columns;
    return true;
}
} // namespace
} // namespace optiland
