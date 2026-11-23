// Dispatch session ray and chief generation across the configured aiming strategies.
// SequentialRayTracer and field evaluation reach calibrated, three-step, robust, or pilot modes here.
// Keep strategy precedence and recovery routing intact so an unavailable model uses the same fallback.

#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

Ray AimingSession::generate(
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index,
    const Field& field,
    const Wavelength& wave,
    const PupilPoint& pupil) {
    fields_seen_[field_index] = field;
    if (system_.ray_aiming.mode == RayAimingMode::Robust) {
        return generate_robust(field, wave, pupil);
    }
    if (system_.ray_aiming.mode == RayAimingMode::CalibratedParaxial) {
        return generate_calibrated_paraxial(field_index, wave_index, pupil_index, field, wave, pupil);
    }
    if (system_.ray_aiming.mode == RayAimingMode::ThreeStep) {
        return generate_three_step(field_index, wave_index, pupil_index, field, wave, pupil);
    }
    return generate_adaptive_pilot(field_index, wave_index, pupil_index, field, wave, pupil);
}

Ray AimingSession::generate_chief(
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave) {
    const PupilPoint chief{0.0, 0.0, 1.0};
    if (system_.ray_aiming.mode != RayAimingMode::CalibratedParaxial) {
        return generate(field_index, wave_index, 0, field, wave, chief);
    }

    fields_seen_[field_index] = field;
    CalibratedParaxialModel& model = ensure_calibrated_paraxial_model(
        field_index,
        wave_index,
        field,
        wave);
    if (!model.chief_seed.valid) {
        return make_failed_ray(
            wave,
            chief,
            std::numeric_limits<double>::infinity());
    }

    Ray ray = tracer_.generate_ray_through_first_surface_point(
        field,
        wave.value_um,
        model.chief_seed.x,
        model.chief_seed.y,
        chief.weight);
    if (!ray.valid) {
        return make_failed_ray(
            wave,
            chief,
            std::numeric_limits<double>::infinity());
    }

    const double rs = tracer_.stop_target_radius(
        wave.value_um,
        RayAimingMode::CalibratedParaxial);
    const double residual = std::hypot(
        model.chief_stop_x - field.fvcx * rs,
        model.chief_stop_y - field.fvcy * rs);
    ray.aim_converged = true;
    ray.aim_residual = std::isfinite(residual)
                           ? residual
                           : std::numeric_limits<double>::infinity();
    ray.aim_target_radius = rs;
    ray.homotopy_fraction = 1.0;
    ray.aim_iterations = 0;
    return ray;
}

} // namespace optiland
