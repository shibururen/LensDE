#pragma once
#include "../prelude.cuh"
#include "../aiming/stop_samples.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/material_and_sag.cuh"

namespace optiland {
namespace {
// Diagnostic-only microbenchmark for the core operation that dominates the
// calibrated builder: tracing several independent chief/edge seed probes to
// the stop.  The serial kernel mirrors the current one-thread-per-model
// execution shape.  The cooperative kernel assigns one warp to a model and
// one lane to each of the five already-built model seeds.  It deliberately
// reuses the production double-precision tracing routines so the comparison
// measures scheduling granularity rather than a different optical algorithm.
struct DeviceCalibratedProbeSummary {
    double weighted_stop_x = 0.0;
    double weighted_stop_y = 0.0;
    unsigned int valid_mask = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceSeed d_calibrated_probe_seed(
    const DeviceCalibratedModel& model,
    int seed_index) {
    if (seed_index == 0) {
        return model.chief_seed;
    }
    if (seed_index >= 1 && seed_index <= 4) {
        return model.edge_anchors[seed_index - 1].seed;
    }
    return {};
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DevicePrecisionProbeSummary {
    double stop_x = 0.0;
    double stop_y = 0.0;
    int valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct FloatProbeRay {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float l = 0.0f;
    float m = 0.0f;
    float n = 1.0f;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool f_probe_normalize(float* x, float* y, float* z) {
    const float norm2 = (*x) * (*x) + (*y) * (*y) + (*z) * (*z);
    if (!(norm2 > 0.0f) || !isfinite(norm2)) {
        return false;
    }
    const float inv = rsqrtf(norm2);
    *x *= inv;
    *y *= inv;
    *z *= inv;
    return isfinite(*x) && isfinite(*y) && isfinite(*z);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool f_probe_is_plane(const DeviceSurface& surface) {
    return surface.kind == kKindPlane || surface.kind == kKindImage ||
           surface.kind == kKindObject ||
           !isfinite(static_cast<float>(surface.radius)) ||
           fabsf(static_cast<float>(surface.radius)) < 1.0e-7f ||
           fabs(surface.radius) > 0.5 * kDeviceInf;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool f_probe_sag(
    const DeviceSurface& surface,
    float x,
    float y,
    float* sag) {
    if (!sag || surface.kind == kKindEvenAsphere ||
        fabs(surface.conic) > 1.0e-14) {
        return false;
    }
    if (f_probe_is_plane(surface)) {
        *sag = 0.0f;
        return true;
    }
    const float radius = static_cast<float>(surface.radius);
    const float r2 = x * x + y * y;
    const float under = 1.0f - r2 / (radius * radius);
    if (!(under >= 0.0f)) {
        return false;
    }
    const float denom = radius * (1.0f + sqrtf(fmaxf(0.0f, under)));
    if (!(fabsf(denom) > 1.0e-7f)) {
        return false;
    }
    *sag = r2 / denom;
    return isfinite(*sag);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool f_probe_intersect(
    const DeviceSurface& surface,
    const FloatProbeRay& ray,
    float local_z,
    float* t) {
    if (!t || surface.kind == kKindEvenAsphere ||
        fabs(surface.conic) > 1.0e-14) {
        return false;
    }
    if (f_probe_is_plane(surface)) {
        if (!(fabsf(ray.n) > 1.0e-7f)) {
            return false;
        }
        *t = -local_z / ray.n;
        return isfinite(*t);
    }
    const float radius = static_cast<float>(surface.radius);
    const float a = ray.l * ray.l + ray.m * ray.m + ray.n * ray.n;
    const float dz = local_z - radius;
    const float b = 2.0f *
        (ray.x * ray.l + ray.y * ray.m + dz * ray.n);
    const float c = ray.x * ray.x + ray.y * ray.y + dz * dz -
                    radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (!(disc >= 0.0f) || !(fabsf(a) > 1.0e-7f)) {
        return false;
    }
    const float root = sqrtf(fmaxf(0.0f, disc));
    const float candidates[2] = {
        (-b - root) / (2.0f * a),
        (-b + root) / (2.0f * a)};
    float best = static_cast<float>(INFINITY);
    float best_residual = static_cast<float>(INFINITY);
    for (int i = 0; i < 2; ++i) {
        const float candidate = candidates[i];
        if (!isfinite(candidate) || candidate < -2.0e-4f) {
            continue;
        }
        const float xi = ray.x + candidate * ray.l;
        const float yi = ray.y + candidate * ray.m;
        const float zi = local_z + candidate * ray.n;
        float sag = 0.0f;
        if (!f_probe_sag(surface, xi, yi, &sag)) {
            continue;
        }
        const float residual = fabsf(sag - zi);
        const float scale = fmaxf(
            1.0f,
            fmaxf(fabsf(radius), fmaxf(fabsf(zi), hypotf(xi, yi))));
        if (residual > 4.0e-6f * scale) {
            continue;
        }
        if (!isfinite(best) || candidate < best - 2.0e-4f ||
            (fabsf(candidate - best) <= 2.0e-4f &&
             residual < best_residual)) {
            best = candidate;
            best_residual = residual;
        }
    }
    if (!isfinite(best)) {
        return false;
    }
    *t = fabsf(best) <= 1.0e-7f ? 0.0f : best;
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool f_probe_normal(
    const DeviceSurface& surface,
    float x,
    float y,
    float* nx,
    float* ny,
    float* nz) {
    if (!nx || !ny || !nz || surface.kind == kKindEvenAsphere ||
        fabs(surface.conic) > 1.0e-14) {
        return false;
    }
    if (f_probe_is_plane(surface)) {
        *nx = 0.0f;
        *ny = 0.0f;
        *nz = 1.0f;
        return true;
    }
    const float radius = static_cast<float>(surface.radius);
    const float root_arg =
        1.0f - (x * x + y * y) / (radius * radius);
    if (!(root_arg > 0.0f)) {
        return false;
    }
    const float denom = radius * sqrtf(root_arg);
    if (!(fabsf(denom) > 1.0e-7f)) {
        return false;
    }
    *nx = x / denom;
    *ny = y / denom;
    *nz = -1.0f;
    return f_probe_normalize(nx, ny, nz);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool f_probe_refract(
    FloatProbeRay* ray,
    float nx,
    float ny,
    float nz,
    float n1,
    float n2) {
    if (!ray || !(fabsf(n2) > 1.0e-7f) ||
        !f_probe_normalize(&ray->l, &ray->m, &ray->n) ||
        !f_probe_normalize(&nx, &ny, &nz)) {
        return false;
    }
    float dot = ray->l * nx + ray->m * ny + ray->n * nz;
    const float sign = dot >= 0.0f ? 1.0f : -1.0f;
    nx *= sign;
    ny *= sign;
    nz *= sign;
    dot = fabsf(dot);
    const float ratio = n1 / n2;
    const float root_arg =
        1.0f - ratio * ratio * (1.0f - dot * dot);
    if (!(root_arg >= 0.0f)) {
        return false;
    }
    const float root = sqrtf(root_arg);
    ray->l = ratio * ray->l + nx * root - ratio * nx * dot;
    ray->m = ratio * ray->m + ny * root - ratio * ny * dot;
    ray->n = ratio * ray->n + nz * root - ratio * nz * dot;
    return f_probe_normalize(&ray->l, &ray->m, &ray->n);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ bool f_sample_stop_from_seed_spherical(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    const DeviceSeed& seed,
    float* stop_x,
    float* stop_y) {
    if (!seed.valid || !stop_x || !stop_y) {
        return false;
    }
    const int first = cfg.first_optical_surface;
    const int stop = d_stop_index(surfaces, cfg);
    if (first < 0 || stop < first || stop >= cfg.surface_count) {
        return false;
    }
    for (int i = first; i <= stop; ++i) {
        if (surfaces[i].kind == kKindEvenAsphere ||
            fabs(surfaces[i].conic) > 1.0e-14) {
            return false;
        }
    }
    FloatProbeRay ray;
    ray.x = static_cast<float>(seed.x);
    ray.y = static_cast<float>(seed.y);
    float first_sag = 0.0f;
    if (!f_probe_sag(surfaces[first], ray.x, ray.y, &first_sag)) {
        return false;
    }
    float first_vertex = 0.0f;
    for (int i = 0; i < first; ++i) {
        if (surfaces[i].kind != kKindObject &&
            isfinite(static_cast<float>(surfaces[i].thickness)) &&
            fabs(surfaces[i].thickness) < 0.5 * kDeviceInf) {
            first_vertex += static_cast<float>(surfaces[i].thickness);
        }
    }
    ray.z = first_vertex + first_sag;
    const float field_radius =
        hypotf(static_cast<float>(field.hx), static_cast<float>(field.hy));
    if (field_radius > 1.0e-7f && fabs(cfg.max_field_deg) > kDeviceEps) {
        const float theta = static_cast<float>(
            3.14159265358979323846 / 180.0 * cfg.max_field_deg) *
            field_radius;
        const float transverse = sinf(theta);
        ray.l = transverse * static_cast<float>(field.hx) / field_radius;
        ray.m = transverse * static_cast<float>(field.hy) / field_radius;
        ray.n = cosf(theta);
        if (!f_probe_normalize(&ray.l, &ray.m, &ray.n)) {
            return false;
        }
    }
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 1.0f;
    if (!f_probe_normal(surfaces[first], ray.x, ray.y, &nx, &ny, &nz)) {
        return false;
    }
    float n_before = 1.0f;
    if (cfg.surface_count > 0 && surfaces[0].kind == kKindObject) {
        n_before = static_cast<float>(
            d_surface_refractive_index(surfaces[0], cfg, wavelength_um));
    }
    float n_after = static_cast<float>(
        d_surface_refractive_index(surfaces[first], cfg, wavelength_um));
    if (!f_probe_refract(&ray, nx, ny, nz, n_before, n_after)) {
        return false;
    }
    n_before = n_after;
    float vertex_z = first_vertex;
    if (isfinite(static_cast<float>(surfaces[first].thickness)) &&
        fabs(surfaces[first].thickness) < 0.5 * kDeviceInf) {
        vertex_z += static_cast<float>(surfaces[first].thickness);
    }
    for (int i = first + 1; i <= stop; ++i) {
        float t = 0.0f;
        if (!f_probe_intersect(
                surfaces[i],
                ray,
                ray.z - vertex_z,
                &t)) {
            return false;
        }
        ray.x += ray.l * t;
        ray.y += ray.m * t;
        ray.z += ray.n * t;
        if (i == stop) {
            *stop_x = ray.x;
            *stop_y = ray.y;
            return isfinite(ray.x) && isfinite(ray.y);
        }
        if (!f_probe_normal(
                surfaces[i], ray.x, ray.y, &nx, &ny, &nz)) {
            return false;
        }
        n_after = static_cast<float>(
            d_surface_refractive_index(surfaces[i], cfg, wavelength_um));
        if (!f_probe_refract(&ray, nx, ny, nz, n_before, n_after)) {
            return false;
        }
        n_before = n_after;
        if (isfinite(static_cast<float>(surfaces[i].thickness)) &&
            fabs(surfaces[i].thickness) < 0.5 * kDeviceInf) {
            vertex_z += static_cast<float>(surfaces[i].thickness);
        }
    }
    return false;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void calibrated_probe_flat_double_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    DeviceConfig cfg,
    int population_size,
    int probes_per_model,
    DevicePrecisionProbeSummary* output) {
    const int probe = blockIdx.x * blockDim.x + threadIdx.x;
    const int model_count =
        population_size * cfg.field_count * cfg.wavelength_count;
    const int probe_count = model_count * probes_per_model;
    if (probe >= probe_count) {
        return;
    }
    const int model_index = probe / probes_per_model;
    const int seed_index = (probe - model_index * probes_per_model) % 5;
    const int models_per_row = cfg.field_count * cfg.wavelength_count;
    const int row = model_index / models_per_row;
    const int local = model_index - row * models_per_row;
    const int field_index = local / cfg.wavelength_count;
    const int wave_index = local - field_index * cfg.wavelength_count;
    const DeviceSeed seed =
        d_calibrated_probe_seed(models[model_index], seed_index);
    DevicePrecisionProbeSummary summary;
    summary.valid = seed.valid && d_sample_stop_from_seed(
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count,
        cfg,
        fields[field_index],
        waves[wave_index].value_um,
        seed,
        &summary.stop_x,
        &summary.stop_y,
        nullptr,
        true,
        true);
    output[probe] = summary;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void calibrated_probe_flat_float_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    DeviceConfig cfg,
    int population_size,
    int probes_per_model,
    DevicePrecisionProbeSummary* output) {
    const int probe = blockIdx.x * blockDim.x + threadIdx.x;
    const int model_count =
        population_size * cfg.field_count * cfg.wavelength_count;
    const int probe_count = model_count * probes_per_model;
    if (probe >= probe_count) {
        return;
    }
    const int model_index = probe / probes_per_model;
    const int seed_index = (probe - model_index * probes_per_model) % 5;
    const int models_per_row = cfg.field_count * cfg.wavelength_count;
    const int row = model_index / models_per_row;
    const int local = model_index - row * models_per_row;
    const int field_index = local / cfg.wavelength_count;
    const int wave_index = local - field_index * cfg.wavelength_count;
    const DeviceSeed seed =
        d_calibrated_probe_seed(models[model_index], seed_index);
    float sx = 0.0f;
    float sy = 0.0f;
    DevicePrecisionProbeSummary summary;
    summary.valid = seed.valid && f_sample_stop_from_seed_spherical(
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count,
        cfg,
        fields[field_index],
        waves[wave_index].value_um,
        seed,
        &sx,
        &sy);
    summary.stop_x = static_cast<double>(sx);
    summary.stop_y = static_cast<double>(sy);
    output[probe] = summary;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void calibrated_probe_serial_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    DeviceConfig cfg,
    int population_size,
    DeviceCalibratedProbeSummary* output) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int models_per_row = cfg.field_count * cfg.wavelength_count;
    const int item_count = population_size * models_per_row;
    if (item >= item_count) {
        return;
    }
    const int row = item / models_per_row;
    const int local = item - row * models_per_row;
    const int field_index = local / cfg.wavelength_count;
    const int wave_index = local - field_index * cfg.wavelength_count;
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const DeviceCalibratedModel model = models[item];
    DeviceCalibratedProbeSummary summary;
    for (int seed_index = 0; seed_index < 5; ++seed_index) {
        const DeviceSeed seed = d_calibrated_probe_seed(model, seed_index);
        double sx = 0.0;
        double sy = 0.0;
        const bool ok = seed.valid &&
            d_sample_stop_from_seed(
                surfaces,
                cfg,
                fields[field_index],
                waves[wave_index].value_um,
                seed,
                &sx,
                &sy,
                nullptr,
                true,
                true);
        if (!ok) {
            continue;
        }
        const double weight = static_cast<double>(seed_index + 1);
        summary.weighted_stop_x += weight * sx;
        summary.weighted_stop_y += weight * sy;
        summary.valid_mask |= 1u << seed_index;
    }
    output[item] = summary;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void calibrated_probe_cooperative_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    DeviceConfig cfg,
    int population_size,
    DeviceCalibratedProbeSummary* output) {
    constexpr unsigned int kFullWarpMask = 0xffffffffu;
    const int global_thread = blockIdx.x * blockDim.x + threadIdx.x;
    const int lane = threadIdx.x & 31;
    const int warp = global_thread >> 5;
    const int models_per_row = cfg.field_count * cfg.wavelength_count;
    const int item_count = population_size * models_per_row;
    if (warp >= item_count) {
        return;
    }
    const int row = warp / models_per_row;
    const int local = warp - row * models_per_row;
    const int field_index = local / cfg.wavelength_count;
    const int wave_index = local - field_index * cfg.wavelength_count;
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const DeviceCalibratedModel model = models[warp];
    double weighted_x = 0.0;
    double weighted_y = 0.0;
    bool ok = false;
    if (lane < 5) {
        const DeviceSeed seed = d_calibrated_probe_seed(model, lane);
        double sx = 0.0;
        double sy = 0.0;
        ok = seed.valid &&
            d_sample_stop_from_seed(
                surfaces,
                cfg,
                fields[field_index],
                waves[wave_index].value_um,
                seed,
                &sx,
                &sy,
                nullptr,
                true,
                true);
        if (ok) {
            const double weight = static_cast<double>(lane + 1);
            weighted_x = weight * sx;
            weighted_y = weight * sy;
        }
    }
    const unsigned int valid_mask = __ballot_sync(kFullWarpMask, ok);
    for (int offset = 16; offset > 0; offset >>= 1) {
        weighted_x += __shfl_down_sync(kFullWarpMask, weighted_x, offset);
        weighted_y += __shfl_down_sync(kFullWarpMask, weighted_y, offset);
    }
    if (lane == 0) {
        DeviceCalibratedProbeSummary summary;
        summary.weighted_stop_x = weighted_x;
        summary.weighted_stop_y = weighted_y;
        summary.valid_mask = valid_mask & 0x1fu;
        output[warp] = summary;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void calibrated_probe_serial_tiled_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    DeviceConfig cfg,
    int population_size,
    int probe_count,
    DeviceCalibratedProbeSummary* output) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int models_per_row = cfg.field_count * cfg.wavelength_count;
    const int item_count = population_size * models_per_row;
    if (item >= item_count) {
        return;
    }
    const int row = item / models_per_row;
    const int local = item - row * models_per_row;
    const int field_index = local / cfg.wavelength_count;
    const int wave_index = local - field_index * cfg.wavelength_count;
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const DeviceCalibratedModel model = models[item];
    DeviceCalibratedProbeSummary summary;
    for (int probe = 0; probe < probe_count; ++probe) {
        const DeviceSeed seed = d_calibrated_probe_seed(model, probe % 5);
        double sx = 0.0;
        double sy = 0.0;
        const bool ok = seed.valid &&
            d_sample_stop_from_seed(
                surfaces,
                cfg,
                fields[field_index],
                waves[wave_index].value_um,
                seed,
                &sx,
                &sy,
                nullptr,
                true,
                true);
        if (!ok) {
            continue;
        }
        const double weight = static_cast<double>(probe + 1);
        summary.weighted_stop_x += weight * sx;
        summary.weighted_stop_y += weight * sy;
        summary.valid_mask |= 1u << probe;
    }
    output[item] = summary;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void calibrated_probe_group_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* models,
    DeviceConfig cfg,
    int population_size,
    int group_size,
    int probe_count,
    DeviceCalibratedProbeSummary* output) {
    __shared__ double shared_x[128];
    __shared__ double shared_y[128];
    __shared__ unsigned int shared_ok[128];
    const int global_thread = blockIdx.x * blockDim.x + threadIdx.x;
    const int item = global_thread / group_size;
    const int lane = threadIdx.x & (group_size - 1);
    const int models_per_row = cfg.field_count * cfg.wavelength_count;
    const int item_count = population_size * models_per_row;
    double weighted_x = 0.0;
    double weighted_y = 0.0;
    unsigned int valid = 0;
    if (item < item_count && lane < probe_count) {
        const int row = item / models_per_row;
        const int local = item - row * models_per_row;
        const int field_index = local / cfg.wavelength_count;
        const int wave_index = local - field_index * cfg.wavelength_count;
        const DeviceSurface* surfaces =
            candidate_surfaces +
            static_cast<long long>(row) * cfg.surface_count;
        const DeviceCalibratedModel model = models[item];
        const DeviceSeed seed = d_calibrated_probe_seed(model, lane % 5);
        double sx = 0.0;
        double sy = 0.0;
        const bool ok = seed.valid &&
            d_sample_stop_from_seed(
                surfaces,
                cfg,
                fields[field_index],
                waves[wave_index].value_um,
                seed,
                &sx,
                &sy,
                nullptr,
                true,
                true);
        if (ok) {
            const double weight = static_cast<double>(lane + 1);
            weighted_x = weight * sx;
            weighted_y = weight * sy;
            valid = 1u;
        }
    }
    shared_x[threadIdx.x] = weighted_x;
    shared_y[threadIdx.x] = weighted_y;
    shared_ok[threadIdx.x] = valid;
    __syncthreads();
    if (item < item_count && lane == 0) {
        DeviceCalibratedProbeSummary summary;
        const int group_begin = threadIdx.x;
        for (int i = 0; i < probe_count; ++i) {
            summary.weighted_stop_x += shared_x[group_begin + i];
            summary.weighted_stop_y += shared_y[group_begin + i];
            if (shared_ok[group_begin + i]) {
                summary.valid_mask |= 1u << i;
            }
        }
        output[item] = summary;
    }
}
} // namespace
} // namespace optiland
