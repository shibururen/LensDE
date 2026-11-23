#pragma once
#include "../prelude.cuh"
#include "chief_geometry.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../ray/paraxial_generation.cuh"

namespace optiland {
namespace {
struct DeviceBudgetChiefOpticalBaseContext {
    double a = 0.0;
    double b = 0.0;
    double inverse_a = 0.0;
    double physical_stop_radius = 0.0;
    double convergence_limit = 0.0;
    double accept_limit = 0.0;
    int valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ bool d_prepare_budget_chief_optical_base_context(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    double rs,
    DeviceBudgetChiefOpticalBaseContext* context) {
    if (!context) {
        return false;
    }
    *context = DeviceBudgetChiefOpticalBaseContext{};
    double a = 0.0;
    double b = 0.0;
    if (!d_first_surface_to_stop_abcd(surfaces, cfg, wavelength_um, &a, &b) ||
        !d_finite(a) ||
        fabs(a) < fmax(1.0e-12, cfg.calibrated_budget_min_a)) {
        return false;
    }
    context->a = a;
    context->b = b;
    context->inverse_a = 1.0 / a;
    context->physical_stop_radius =
        d_physical_stop_radius(surfaces, cfg, rs);
    context->convergence_limit = fmax(
        fmax(0.0, cfg.ray_aiming_tolerance),
        fmax(0.0, cfg.calibrated_budget_residual_stop_fraction) *
            context->physical_stop_radius);
    context->accept_limit = fmax(
        cfg.ray_aiming_tolerance,
        fmax(0.0, cfg.calibrated_budget_accept_stop_fraction) *
            fmax(1.0e-9, fabs(rs)));
    context->valid = 1;
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ DeviceSeed
d_estimate_abcd_chief_seed_precomputed(
    const DeviceConfig& cfg,
    const DeviceField& field,
    double rs,
    const DeviceBudgetChiefOpticalBaseContext& context,
    bool* singular) {
    if (singular) {
        *singular = false;
    }
    if (!context.valid) {
        if (singular) {
            *singular = true;
        }
        return {};
    }
    double lx = 0.0;
    double my = 0.0;
    double nz = 1.0;
    d_field_direction(cfg, field, &lx, &my, &nz);
    if (!d_finite(nz) || fabs(nz) <= kDeviceEps) {
        return {};
    }
    const double ux = lx / nz;
    const double uy = my / nz;
    const double target_x = field.fvcx * rs;
    const double target_y = field.fvcy * rs;
    DeviceSeed seed;
    seed.x = (target_x - context.b * ux) / context.a;
    seed.y = (target_y - context.b * uy) / context.a;
    seed.valid = d_finite(seed.x) && d_finite(seed.y) ? 1 : 0;
    return seed;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceSeed d_estimate_abcd_chief_seed(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    double rs,
    bool* singular) {
    if (singular) {
        *singular = false;
    }
    double a = 0.0;
    double b = 0.0;
    if (!d_first_surface_to_stop_abcd(surfaces, cfg, wavelength_um, &a, &b) ||
        !d_finite(a) ||
        fabs(a) < fmax(1.0e-12, cfg.calibrated_budget_min_a)) {
        if (singular) {
            *singular = true;
        }
        return {};
    }
    double lx = 0.0;
    double my = 0.0;
    double nz = 1.0;
    d_field_direction(cfg, field, &lx, &my, &nz);
    if (!d_finite(nz) || fabs(nz) <= kDeviceEps) {
        return {};
    }
    const double ux = lx / nz;
    const double uy = my / nz;
    const double target_x = field.fvcx * rs;
    const double target_y = field.fvcy * rs;
    DeviceSeed seed;
    seed.x = (target_x - b * ux) / a;
    seed.y = (target_y - b * uy) / a;
    seed.valid = d_finite(seed.x) && d_finite(seed.y) ? 1 : 0;
    return seed;
}
} // namespace
} // namespace optiland
