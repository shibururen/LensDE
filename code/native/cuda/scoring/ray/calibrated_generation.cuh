#pragma once
#include "../prelude.cuh"
#include "../aiming/chief_geometry.cuh"
#include "../aiming/model_builder.cuh"
#include "../aiming/seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "stop_trace.cuh"

namespace optiland {
namespace {
__device__ __noinline__ DeviceRay d_generate_calibrated_paraxial_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double pupil_x,
    double pupil_y,
    double pupil_weight,
    double wavelength_um,
    const DeviceCalibratedModel* cached_model = nullptr,
    bool predict_image_guard = true) {
    DeviceCalibratedModel local_model;
    const DeviceCalibratedModel* model = cached_model;
    if (!model) {
        d_build_calibrated_model(
            surfaces,
            cfg,
            field,
            wavelength_um,
            &local_model);
        model = &local_model;
    }
    const double rs = d_aperture_stop_radius(surfaces, cfg, wavelength_um);
    if (!model || !model->valid || !model->chief_seed.valid) {
        return d_make_failed_aim_ray(cfg, wavelength_um, pupil_weight, INFINITY);
    }
    const double pupil_qx = field.fvcx + pupil_x * (1.0 - field.fvdx);
    const double pupil_qy = field.fvcy + pupil_y * (1.0 - field.fvdy);
    DeviceSeed seed = model->chief_seed;
    double stop_vx = 0.0, stop_vy = 0.0;
    bool have_stop_samples = true;
    auto apply_axis = [&](bool x_axis, double coord) {
        if (fabs(coord) <= 1.0e-14) {
            return true;
        }
        const bool positive = coord > 0.0;
        const DeviceCalibratedEdgeAnchor* selected = nullptr;
        for (int i = 0; i < 4; ++i) {
            const DeviceCalibratedEdgeAnchor& anchor = model->edge_anchors[i];
            if (!anchor.valid || !anchor.seed.valid) {
                continue;
            }
            if (x_axis && positive && anchor.px > 0.5) {
                selected = &anchor;
            } else if (x_axis && !positive && anchor.px < -0.5) {
                selected = &anchor;
            } else if (!x_axis && positive && anchor.py > 0.5) {
                selected = &anchor;
            } else if (!x_axis && !positive && anchor.py < -0.5) {
                selected = &anchor;
            }
        }
        if (!selected) {
            return false;
        }
        const double anchor_coord = fabs(x_axis ? selected->px : selected->py);
        if (!d_finite(anchor_coord) || anchor_coord <= 1.0e-12 ||
            fabs(coord) > anchor_coord * (1.0 + 1.0e-10)) {
            return false;
        }
        const double weight = fabs(coord) / anchor_coord;
        seed.x += weight * (selected->seed.x - model->chief_seed.x);
        seed.y += weight * (selected->seed.y - model->chief_seed.y);
        if (d_finite(selected->stop_x) && d_finite(selected->stop_y)) {
            stop_vx += weight * (selected->stop_x - model->chief_stop_x);
            stop_vy += weight * (selected->stop_y - model->chief_stop_y);
        } else {
            have_stop_samples = false;
        }
        seed.valid = 1;
        return true;
    };
    if (!apply_axis(true, pupil_x) || !apply_axis(false, pupil_y) ||
        !d_seed_in_first_surface_bounds(surfaces, cfg, wavelength_um, seed)) {
        return d_make_failed_aim_ray(cfg, wavelength_um, pupil_weight, INFINITY);
    }
    double predicted_scale = 1.0;
    if (predict_image_guard && d_defer_secondary_apertures(cfg, wavelength_um) &&
        d_first_ray_aiming_surface(surfaces, cfg, wavelength_um) == cfg.first_optical_surface) {
        predicted_scale = spectral_edge_scale(model->chief_seed.x, model->chief_seed.y,
            seed.x - model->chief_seed.x, seed.y - model->chief_seed.y,
            d_spectral_surface_radius(surfaces[cfg.first_optical_surface]));
        // Use the existing CP target map to include the circular STOP in the
        // prediction. This adds arithmetic, not a probe or another ray trace.
        const int stop = d_stop_index(surfaces, cfg);
        if (stop >= 0 && stop < cfg.surface_count) {
            const double stop_scale = spectral_edge_scale(
                have_stop_samples ? model->chief_stop_x : field.fvcx * rs,
                have_stop_samples ? model->chief_stop_y : field.fvcy * rs,
                have_stop_samples ? stop_vx : pupil_x * (1.0 - field.fvdx) * rs,
                have_stop_samples ? stop_vy : pupil_y * (1.0 - field.fvdy) * rs,
                d_spectral_surface_radius(surfaces[stop]));
            const bool diagonal = fabs(pupil_x) > 1.0e-14 &&
                                  fabs(pupil_y) > 1.0e-14;
            const double stop_reserve = diagonal ? 0.96 : 0.99;
            const bool stop_guard_active =
                stop_scale < 1.0 || (diagonal && predicted_scale < 1.0);
            predicted_scale = fmin(
                predicted_scale,
                stop_guard_active ? stop_reserve * stop_scale : 1.0);
        }
        seed.x = model->chief_seed.x + predicted_scale * (seed.x - model->chief_seed.x);
        seed.y = model->chief_seed.y + predicted_scale * (seed.y - model->chief_seed.y);
    }
    DeviceRay ray = d_generate_first_surface_point_ray(
        surfaces,
        cfg,
        field,
        wavelength_um,
        seed.x,
        seed.y,
        pupil_weight);
    ray.edge_prediction_scale = predicted_scale;
    if (!ray.valid) {
        return d_make_failed_aim_ray(cfg, wavelength_um, pupil_weight, INFINITY);
    }
    ray.aim_converged = 1;
    ray.aim_residual = model->fit_rms;
    ray.aim_target_radius = rs;
    return ray;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceRay d_generate_calibrated_chief_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double pupil_weight,
    double wavelength_um,
    const DeviceCalibratedModel* cached_model = nullptr) {
    // The reference is the same anchor as the retained pupil, not a separate
    // zero-vignetting/chief-only solve. Match AimingSession::generate_chief:
    // a usable chief does not require every edge anchor to be usable.
    DeviceCalibratedModel local_model;
    const DeviceCalibratedModel* model = cached_model;
    if (!model) {
        d_build_calibrated_model(surfaces, cfg, field, wavelength_um, &local_model);
        model = &local_model;
    }
    if (!model->chief_seed.valid) {
        return d_make_failed_aim_ray(cfg, wavelength_um, pupil_weight, INFINITY);
    }
    DeviceRay ray = d_generate_first_surface_point_ray(
        surfaces, cfg, field, wavelength_um,
        model->chief_seed.x, model->chief_seed.y, pupil_weight);
    if (!ray.valid) {
        return d_make_failed_aim_ray(cfg, wavelength_um, pupil_weight, INFINITY);
    }
    const double rs = d_aperture_stop_radius(surfaces, cfg, wavelength_um);
    ray.aim_converged = 1;
    ray.aim_residual = hypot(model->chief_stop_x - field.fvcx * rs,
                             model->chief_stop_y - field.fvcy * rs);
    ray.aim_target_radius = rs;
    return ray;
}
} // namespace
} // namespace optiland
