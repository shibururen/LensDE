#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/mechanical_constraints.cuh"
#include "paraxial_generation.cuh"
#include "refraction.cuh"

namespace optiland {
namespace {
struct DeviceStructuralTrace {
    double ux = 0.0;
    double uy = 0.0;
    double epsilon = 0.0;
    double last_axis = 0.0;
    double last_z = 0.0;
    double first_cross_z = NAN;
    double first_surface_axis = NAN;
    double input_axis = NAN;
    int last_sign = 0;
    int have_last = 0;
    int has_cross = 0;
    int axis_cross_count = 0;
    int first_surface_valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_axis_sign(double value, double epsilon) {
    if (!d_finite(value) || fabs(value) <= epsilon) {
        return 0;
    }
    return value < 0.0 ? -1 : 1;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_initialize_structural_trace(
    DeviceStructuralTrace* structure,
    const DeviceRay& ray) {
    if (!structure || !ray.valid || !d_finite(ray.x) || !d_finite(ray.y) ||
        !d_finite(ray.z)) {
        return;
    }
    const double axis = ray.x * structure->ux + ray.y * structure->uy;
    const int sign = d_axis_sign(axis, structure->epsilon);
    if (sign == 0) {
        return;
    }
    structure->last_sign = sign;
    structure->last_axis = axis;
    structure->last_z = ray.z;
    structure->have_last = 1;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_record_structural_hit(
    DeviceStructuralTrace* structure,
    const DeviceConfig& cfg,
    int surface_index,
    const DeviceRay& ray) {
    if (!structure || !ray.valid || !d_finite(ray.x) || !d_finite(ray.y) ||
        !d_finite(ray.z)) {
        return;
    }
    const double axis = ray.x * structure->ux + ray.y * structure->uy;
    if (surface_index == cfg.first_optical_surface) {
        structure->first_surface_axis = axis;
        structure->first_surface_valid = d_finite(axis) ? 1 : 0;
    }
    const int sign = d_axis_sign(axis, structure->epsilon);
    if (sign == 0) {
        return;
    }
    if (structure->last_sign != 0 && sign != structure->last_sign) {
        ++structure->axis_cross_count;
        if (!structure->has_cross) {
            const double denom = fabs(structure->last_axis) + fabs(axis);
            const double fraction =
                denom > 0.0 ? fabs(structure->last_axis) / denom : 0.5;
            structure->first_cross_z =
                structure->last_z + fraction * (ray.z - structure->last_z);
            structure->has_cross = d_finite(structure->first_cross_z) ? 1 : 0;
        }
    }
    structure->last_sign = sign;
    structure->last_axis = axis;
    structure->last_z = ray.z;
    structure->have_last = 1;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_atomic_max_nonnegative(double* address, double value) {
    if (!address || !d_finite(value) || value <= 0.0) {
        return;
    }
    auto* bits = reinterpret_cast<unsigned long long*>(address);
    unsigned long long old = *bits;
    while (__longlong_as_double(old) < value) {
        const unsigned long long assumed = old;
        old = atomicCAS(bits, assumed, __double_as_longlong(value));
        if (old == assumed) {
            break;
        }
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ bool d_aperture_asphere_root_looks_far(
    const DeviceSurface& surface,
    double x,
    double y) {
    constexpr double kFarRootEnvelope = 1.50;
    const double profile_envelope = d_edge_sample_radius(surface);
    return surface.kind == kKindEvenAsphere && profile_envelope > 0.0 &&
           hypot(x, y) > kFarRootEnvelope * profile_envelope;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ bool d_try_recover_aperture_asphere_root(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    int surface_index,
    double origin_x,
    double origin_y,
    double origin_z,
    double local_z,
    double incident_l,
    double incident_m,
    double incident_n,
    double n_before,
    double n_post,
    double original_t,
    bool track_opd,
    DeviceRay* ray) {
    if (!surfaces || !ray || surface_index < 0 ||
        surface_index >= cfg.surface_count) {
        return false;
    }
    const DeviceSurface& surface = surfaces[surface_index];
    const double profile_envelope = d_edge_sample_radius(surface);
    constexpr double kRecoveredRootEnvelope = 1.15;
    if (!d_aperture_asphere_root_looks_far(surface, ray->x, ray->y)) {
        return false;
    }
    double retry_t = 0.0;
    if (!de_cuda_detail::intersect_asphere_nearest_forward_fallback(
            surface,
            origin_x,
            origin_y,
            local_z,
            incident_l,
            incident_m,
            incident_n,
            0.0,
            NAN,
            &retry_t)) {
        return false;
    }
    const double retry_x = origin_x + incident_l * retry_t;
    const double retry_y = origin_y + incident_m * retry_t;
    const double retry_z = origin_z + incident_n * retry_t;
    const double retry_radius = hypot(retry_x, retry_y);
    const double retry_clip_radius = d_effective_aperture_clip_radius(
        surfaces,
        cfg,
        surface_index,
        ray->wavelength_um);
    if (retry_radius > kRecoveredRootEnvelope * profile_envelope ||
        (retry_clip_radius > 0.0 && retry_radius > 1.01 * retry_clip_radius)) {
        return false;
    }
    double retry_nx = 0.0;
    double retry_ny = 0.0;
    double retry_nz = -1.0;
    double retry_ox = incident_l;
    double retry_oy = incident_m;
    double retry_oz = incident_n;
    if (!d_surface_normal(
            surface,
            retry_x,
            retry_y,
            &retry_nx,
            &retry_ny,
            &retry_nz) ||
        !d_refract(
            incident_l,
            incident_m,
            incident_n,
            retry_nx,
            retry_ny,
            retry_nz,
            n_before,
            n_post,
            &retry_ox,
            &retry_oy,
            &retry_oz)) {
        return false;
    }
    if (track_opd) {
        ray->opd -= fabs(original_t * n_before);
        ray->opd += fabs(retry_t * n_before);
    }
    ray->x = retry_x;
    ray->y = retry_y;
    ray->z = retry_z;
    ray->l = retry_ox;
    ray->m = retry_oy;
    ray->n = retry_oz;
    return true;
}
} // namespace
} // namespace optiland
