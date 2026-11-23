#pragma once
#include "../prelude.cuh"
#include "../aiming/edge_scale_solver.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/asphere_slope.cuh"

namespace optiland {
namespace {
__device__ void d_gaussian_node_weight(
    int rings,
    int i,
    double* radius,
    double* weight) {
    const int n = max(rings, 1);
    const int index = min(max(i, 0), n - 1);
    if (fixed_budget_gaussian_node_weight(n,index,radius,weight)) return;
    if (n == 1) {
        *radius = 0.70711;
        *weight = 0.5;
        return;
    }
    if (n == 2) {
        *radius = index == 0 ? 0.45970 : 0.88807;
        *weight = 0.25;
        return;
    }
    if (n == 3) {
        const double radii[3] = {0.33571, 0.70711, 0.94196};
        const double weights[3] = {0.13889, 0.22222, 0.13889};
        *radius = radii[index];
        *weight = weights[index];
        return;
    }
    if (n == 4) {
        const double radii[4] = {0.26350, 0.57446, 0.81853, 0.96466};
        const double weights[4] = {0.08696, 0.16304, 0.16304, 0.08696};
        *radius = radii[index];
        *weight = weights[index];
        return;
    }
    if (n == 5) {
        const double radii[5] = {0.21659, 0.48038, 0.70711, 0.87706, 0.97626};
        const double weights[5] = {0.059231, 0.11966, 0.14222, 0.11966, 0.059231};
        *radius = radii[index];
        *weight = weights[index];
        return;
    }
    if (n == 6) {
        const double radii[6] = {0.18375, 0.41158, 0.61700, 0.78696, 0.91138, 0.98300};
        const double weights[6] = {0.04283, 0.09019, 0.11698, 0.11698, 0.09019, 0.04283};
        *radius = radii[index];
        *weight = weights[index];
        return;
    }

    const int root = min(index, n - 1 - index);
    double z = cos(
        3.14159265358979323846 * (static_cast<double>(root) + 0.75) /
        (static_cast<double>(n) + 0.5));
    double derivative = 0.0;
    for (int iteration = 0; iteration < 32; ++iteration) {
        double p_prev = 1.0;
        double p = z;
        for (int order = 2; order <= n; ++order) {
            const double p_next =
                ((2.0 * order - 1.0) * z * p - (order - 1.0) * p_prev) /
                static_cast<double>(order);
            p_prev = p;
            p = p_next;
        }
        derivative = static_cast<double>(n) * (z * p - p_prev) / (z * z - 1.0);
        const double next = z - p / derivative;
        if (fabs(next - z) <= 1.0e-15) {
            z = next;
            break;
        }
        z = next;
    }

    double p_prev = 1.0;
    double p = z;
    for (int order = 2; order <= n; ++order) {
        const double p_next =
            ((2.0 * order - 1.0) * z * p - (order - 1.0) * p_prev) /
            static_cast<double>(order);
        p_prev = p;
        p = p_next;
    }
    derivative = static_cast<double>(n) * (z * p - p_prev) / (z * z - 1.0);
    const bool lower_half = index < n / 2;
    const double u = 0.5 * (1.0 + (lower_half ? -z : z));
    *radius = sqrt(max(0.0, u));
    *weight = 0.5 / ((1.0 - z * z) * derivative * derivative);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_codev_quadrant_at(
    const DeviceConfig& cfg,
    int target,
    double* selected_x,
    double* selected_y,
    double* radius_sq) {
    if (target < 0 || !selected_x || !selected_y || !radius_sq) {
        return false;
    }
    const double interval =
        d_finite(cfg.codev_ray_interval) && cfg.codev_ray_interval > 0.0
            ? fmin(1.0, cfg.codev_ray_interval)
            : 0.385;
    const double obscuration = d_clamp(cfg.codev_obscuration, 0.0, 1.0);
    const double obscuration_sq = obscuration * obscuration;
    int quadrant_index = 0;
    for (int iy = 0;; ++iy) {
        const double y = (static_cast<double>(iy) + 0.5) * interval;
        if (y >= 1.0 + 1.0e-12) {
            break;
        }
        for (int ix = 0;; ++ix) {
            const double x = (static_cast<double>(ix) + 0.5) * interval;
            if (x >= 1.0 + 1.0e-12) {
                break;
            }
            const double candidate_radius_sq = x * x + y * y;
            if (candidate_radius_sq > 1.0 + 1.0e-12 ||
                candidate_radius_sq < obscuration_sq - 1.0e-12) {
                continue;
            }
            if (quadrant_index == target) {
                *selected_x = x;
                *selected_y = y;
                *radius_sq = candidate_radius_sq;
                return true;
            }
            ++quadrant_index;
        }
    }
    return false;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_codev_quadrant_count(const DeviceConfig& cfg) {
    const double interval =
        d_finite(cfg.codev_ray_interval) && cfg.codev_ray_interval > 0.0
            ? fmin(1.0, cfg.codev_ray_interval)
            : 0.385;
    const double obscuration = d_clamp(cfg.codev_obscuration, 0.0, 1.0);
    const double obscuration_sq = obscuration * obscuration;
    int count = 0;
    for (int iy = 0;; ++iy) {
        const double y = (static_cast<double>(iy) + 0.5) * interval;
        if (y >= 1.0 + 1.0e-12) {
            break;
        }
        for (int ix = 0;; ++ix) {
            const double x = (static_cast<double>(ix) + 0.5) * interval;
            if (x >= 1.0 + 1.0e-12) {
                break;
            }
            const double candidate_radius_sq = x * x + y * y;
            if (candidate_radius_sq <= 1.0 + 1.0e-12 &&
                candidate_radius_sq >= obscuration_sq - 1.0e-12) {
                ++count;
            }
        }
    }
    return count;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_pupil_count(const DeviceConfig& cfg) {
    if (cfg.pupil_sampling_type == kPupilSamplingCodevRectangular) {
        return 4 * d_codev_quadrant_count(cfg);
    }
    return max(cfg.num_rays, 1) * max(cfg.pupil_arms, 1);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_uses_elevated_field_sampling(
    const DeviceConfig& cfg,
    const DeviceField& field) {
    return cfg.pupil_sampling_type != kPupilSamplingCodevRectangular &&
           cfg.elevated_field_num_rays > 0 &&
           cfg.elevated_field_pupil_arms > 0 &&
           hypot(field.hx, field.hy) + 1.0e-12 >=
               cfg.elevated_field_sampling_min_radius;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_fixed_budget_pupil_shape(
    const DeviceConfig& cfg, const DeviceField& field) {
    if (!cfg.fixed_budget_sampling) return 0;
    const double radius = hypot(field.hx,field.hy);
    const bool order_active = cfg.pupil_order_prior_weight > 0.0 &&
        cfg.pupil_order_soft_tolerance > 0.0 && radius > 1.0e-12 &&
        radius + 1.0e-12 >= cfg.pupil_order_min_field_radius;
    return fixed_budget_pupil_shape(true,cfg.num_rays,cfg.pupil_arms,
        d_uses_elevated_field_sampling(cfg,field),cfg.max_field_deg,
        field.hx,field.hy,field.fvcx,field.fvcy,field.fvdx,field.fvdy,order_active);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_field_pupil_count(
    const DeviceConfig& cfg,
    const DeviceField& field) {
    if (d_uses_elevated_field_sampling(cfg, field)) {
        return max(cfg.elevated_field_num_rays, 1) *
               max(cfg.elevated_field_pupil_arms, 1);
    }
    return d_pupil_count(cfg);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_pupil_at(
    const DeviceConfig& cfg,
    int idx,
    double* px,
    double* py,
    double* weight,
    double* wavefront_weight = nullptr) {
    if (cfg.pupil_sampling_type == kPupilSamplingCodevRectangular) {
        const int quadrant_target = max(0, idx) / 4;
        const int sign_index = max(0, idx) % 4;
        double selected_x = 0.0;
        double selected_y = 0.0;
        double radius_sq = 0.0;
        if (!d_codev_quadrant_at(
                cfg, quadrant_target, &selected_x, &selected_y, &radius_sq)) {
            *px = 0.0;
            *py = 0.0;
            *weight = 0.0;
            if (wavefront_weight) {
                *wavefront_weight = 0.0;
            }
            return;
        }
        const double sx = (sign_index == 0 || sign_index == 2) ? -1.0 : 1.0;
        const double sy = sign_index < 2 ? -1.0 : 1.0;
        *px = sx * selected_x;
        *py = sy * selected_y;
        *weight = pow(
            fmax(radius_sq, kDeviceEps),
            -cfg.codev_spot_aperture_weight);
        if (wavefront_weight) {
            *wavefront_weight = pow(
                fmax(radius_sq, kDeviceEps),
                -cfg.codev_wavefront_aperture_weight);
        }
        return;
    }
    const int arms = max(cfg.pupil_arms, 1);
    const int ring = idx / arms;
    const int arm = idx - ring * arms;
    const int rings = max(cfg.num_rays, 1);
    double r = 0.0;
    double radial_weight = 0.0;
    d_gaussian_node_weight(rings, ring, &r, &radial_weight);
    const double theta =
        2.0 * 3.14159265358979323846 * static_cast<double>(arm) /
        static_cast<double>(arms);
    *px = r * cos(theta);
    *py = r * sin(theta);
    *weight = radial_weight / static_cast<double>(arms);
    if (wavefront_weight) {
        *wavefront_weight = *weight;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_field_pupil_at(
    const DeviceConfig& cfg,
    const DeviceField& field,
    int idx,
    double* px,
    double* py,
    double* weight,
    double* wavefront_weight = nullptr) {
    const int balanced = d_fixed_budget_pupil_shape(cfg,field);
    if (balanced) {
        const int rings = balanced == 1 ? 12 : 8;
        const int arms = balanced == 1 ? 4 : 6;
        const int ring = idx / arms;
        const int arm = idx - ring * arms;
        double r = 0.0, radial_weight = 0.0;
        d_gaussian_node_weight(rings,ring,&r,&radial_weight);
        const double theta = 2.0 * 3.14159265358979323846 *
            static_cast<double>(arm) / static_cast<double>(arms);
        *px = r * cos(theta); *py = r * sin(theta);
        *weight = radial_weight / static_cast<double>(arms);
        if (wavefront_weight) *wavefront_weight = *weight;
        return;
    }
    if (!d_uses_elevated_field_sampling(cfg, field)) {
        d_pupil_at(cfg, idx, px, py, weight, wavefront_weight);
        return;
    }
    DeviceConfig elevated = cfg;
    elevated.num_rays = cfg.elevated_field_num_rays;
    elevated.pupil_arms = cfg.elevated_field_pupil_arms;
    d_pupil_at(elevated, idx, px, py, weight, wavefront_weight);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_field_pupil_symmetry(
    const DeviceConfig& cfg,
    const DeviceField& field) {
    if (!cfg.codev_use_symmetry) {
        return kPupilSymmetryNone;
    }
    constexpr double tolerance = 1.0e-13;
    const bool axial =
        fabs(field.hx) <= tolerance && fabs(field.hy) <= tolerance;
    const bool centered_vignetting =
        fabs(field.fvcx) <= tolerance && fabs(field.fvcy) <= tolerance;
    if (axial && centered_vignetting) {
        return fabs(field.fvdx - field.fvdy) <= tolerance
                   ? kPupilSymmetryDihedral4
                   : kPupilSymmetrySignXY;
    }
    if (fabs(field.hx) <= tolerance && fabs(field.fvcx) <= tolerance) {
        return kPupilSymmetryMirrorX;
    }
    if (fabs(field.hy) <= tolerance && fabs(field.fvcy) <= tolerance) {
        return kPupilSymmetryMirrorY;
    }
    return kPupilSymmetryNone;
}
} // namespace
} // namespace optiland
