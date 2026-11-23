#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/edge_scale_solver.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../host/certificate_context.cuh"
#include "../merit/first_order_constraints.cuh"
#include "../merit/residual_primitives.cuh"
#include "../merit/spot_statistics.cuh"
#include "../merit/supported_ray_generation.cuh"
#include "../ray/calibrated_generation.cuh"
#include "../ray/sequential_trace.cuh"
#include "../ray/structural_trace.cuh"
#include "../sampling/pupil_nodes.cuh"
#include "../sampling/symmetry_mapping.cuh"

namespace optiland {
namespace {
__global__ void trace_spot_fields_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    DeviceFieldSpotStats* field_stats) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * cfg.field_count;
    if (item >= item_count) {
        return;
    }
    const int row = item / cfg.field_count;
    fields = d_candidate_row_fields(fields, cfg, row);
    const int field_index = item - row * cfg.field_count;
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const DeviceField field = fields[field_index];
    field_stats[item] = d_trace_field_spot_stats(
        surfaces,
        fields,
        field_index,
        field,
        waves,
        cfg);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void trace_paraxial_spot_rays_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* calibrated_models,
    DeviceConfig cfg,
    int population_size,
    int pupil_count,
    DeviceSpotRaySample* samples,
    double* pupil_order_heights,
    double* aperture_max_heights,
    int transpose_rows,
    int track_opd, int evaluation_phase=0) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count =
        population_size * cfg.field_count * cfg.wavelength_count * pupil_count;
    if (item >= item_count) {
        return;
    }
    int row = 0;
    int field_index = 0;
    int wave_index = 0;
    int pupil_index = 0;
    int output_item = item;
    if (transpose_rows) {
        int remainder = item;
        row = remainder % population_size;
        remainder /= population_size;
        pupil_index = remainder % pupil_count;
        remainder /= pupil_count;
        wave_index = remainder % cfg.wavelength_count;
        field_index = remainder / cfg.wavelength_count;
        output_item =
            ((row * cfg.field_count + field_index) * cfg.wavelength_count +
             wave_index) * pupil_count + pupil_index;
    } else {
        int remainder = item;
        pupil_index = remainder % pupil_count;
        remainder /= pupil_count;
        wave_index = remainder % cfg.wavelength_count;
        remainder /= cfg.wavelength_count;
        field_index = remainder % cfg.field_count;
        row = remainder / cfg.field_count;
    }
    // A phase writes only its own fields; in-place symmetry expansion must
    // likewise run exactly once per field, not again over an expanded prefix.
    if ((evaluation_phase==1 && field_index<cfg.field_count-3) ||
        (evaluation_phase==2 && field_index>=cfg.field_count-3)) return;
    if (evaluation_skip_outer_row(row)) { samples[output_item]=DeviceSpotRaySample{}; return; }
    fields = d_candidate_row_fields(fields, cfg, row);
    DeviceSpotRaySample sample{};
    const DeviceWave wave = waves[wave_index];
    const DeviceField field = fields[field_index];
    const int active_physical_pupil_count =
        d_field_physical_pupil_count(cfg, field);
    if (wave.weight > 0.0 && pupil_index < active_physical_pupil_count) {
        double px = 0.0;
        double py = 0.0;
        double pw = 1.0;
        double wavefront_pw = 1.0;
        d_field_physical_pupil_at(
            cfg, field, pupil_index, &px, &py, &pw, &wavefront_pw);
        const DeviceSurface* surfaces =
            candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
        const long long model_index =
            (static_cast<long long>(row) * cfg.field_count + field_index) *
                cfg.wavelength_count +
            wave_index;
        const DeviceCalibratedModel* calibrated_model =
            calibrated_models ? calibrated_models + model_index : nullptr;
        DeviceRay ray = d_generate_supported_ray(
            surfaces,
            cfg,
            field,
            px,
            py,
            pw,
            wave.value_um,
            calibrated_model,
            true);
        sample.initial_x = ray.x;
        sample.initial_y = ray.y;
        sample.initial_z = ray.z;
        sample.initial_l = ray.l;
        sample.initial_m = ray.m;
        sample.initial_n = ray.n;
        DeviceStructuralTrace structure{};
        DeviceStructuralTrace* structure_ptr = nullptr;
        DeviceStructuralTrace structure_y{};
        DeviceStructuralTrace* structure_y_ptr = nullptr;
        if (cfg.reject_ray_path_structural_failures &&
            !d_field_is_off_axis(fields[field_index])) {
            structure.ux = 1.0;
            structure.uy = 0.0;
            structure.epsilon = fmax(0.0, cfg.axis_crossing_epsilon);
            structure.input_axis = ray.l;
            d_initialize_structural_trace(&structure, ray);
            structure_ptr = &structure;
            structure_y.ux = 0.0;
            structure_y.uy = 1.0;
            structure_y.epsilon = fmax(0.0, cfg.axis_crossing_epsilon);
            structure_y.input_axis = ray.m;
            d_initialize_structural_trace(&structure_y, ray);
            structure_y_ptr = &structure_y;
        }
        double* order_height_slice = nullptr;
        double order_ux = 0.0;
        double order_uy = 0.0;
        const double order_field_radius = hypot(field.hx, field.hy);
        if (pupil_order_heights && cfg.pupil_order_prior_weight > 0.0 &&
            wave_index == cfg.primary_wave_index &&
            order_field_radius > 1.0e-12 &&
            order_field_radius + 1.0e-12 >=
                cfg.pupil_order_min_field_radius) {
            order_ux = field.hx / order_field_radius;
            order_uy = field.hy / order_field_radius;
            const double sagittal = -px * order_uy + py * order_ux;
            if (fabs(sagittal) <= 1.0e-10) {
                const long long order_base =
                    ((static_cast<long long>(row) * cfg.field_count +
                      field_index) *
                         pupil_count +
                     pupil_index) *
                    cfg.surface_count;
                order_height_slice = pupil_order_heights + order_base;
                for (int si = 0; si < cfg.surface_count; ++si) {
                    order_height_slice[si] = NAN;
                }
            }
        }
        if (aperture_max_heights) {
            // The experimental fused-aperture path makes the actual merit
            // pupil the clear-aperture contract.  Preserve the ordinary spot
            // trace semantics while committing the completed sequential ray
            // history to the per-candidate radial envelope.
            d_trace_ray_for_aperture_envelope<false>(
                surfaces,
                cfg,
                &ray,
                aperture_max_heights +
                    static_cast<long long>(row) * cfg.surface_count,
                true,
                track_opd != 0,
                false,
                nullptr,
                nullptr,
                0.0,
                1.0,
                structure_ptr,
                structure_y_ptr,
                order_height_slice,
                order_ux,
                order_uy,
                nullptr,
                true);
        } else {
            d_trace_ray(
                surfaces,
                cfg,
                &ray,
                nullptr,
                false,
                structure_ptr,
                track_opd != 0,
                structure_y_ptr,
                false,
                order_height_slice,
                order_ux,
                order_uy);
        }
        ray.nominal_weight *= wave.weight;
        ray.intensity *= wave.weight;
        sample.x = ray.x;
        sample.y = ray.y;
        sample.z = ray.z;
        sample.l = ray.l;
        sample.m = ray.m;
        sample.n = ray.n;
        sample.opd = ray.opd;
        sample.pupil_x = px;
        sample.pupil_y = py;
        sample.stop_x = ray.stop_x;
        sample.stop_y = ray.stop_y;
        sample.stop_reached = ray.stop_reached;
        sample.failure_code = ray.failure_code;
        sample.min_backward_air_gap = ray.min_backward_air_gap;
        sample.min_backward_glass_gap = ray.min_backward_glass_gap;
        sample.failure_surface = ray.failure_surface;
        sample.weight = d_ray_nominal_weight(ray);
        sample.intensity = ray.intensity;
        sample.wavefront_weight = wavefront_pw * wave.weight;
        sample.weighted_progress =
            sample.weight * d_ray_progress(ray, cfg.surface_count);
        sample.aim_usable = d_ray_aim_usable_for_spot(ray) ? 1 : 0;
        sample.normal_aperture_blockage = d_normal_secondary_blockage(ray) ? 1 : 0;
        sample.usable = d_ray_usable_for_spot(ray) ? 1 : 0;
        sample.trace_valid = ray.valid ? 1 : 0;
        sample.backward = ray.backward_propagation ? 1 : 0;
        if (structure_ptr) {
            sample.axis_cross_count = structure.axis_cross_count;
            sample.first_cross_z = structure.first_cross_z;
            sample.has_axis_cross = structure.has_cross;
            sample.axis_cross_count_y = structure_y.axis_cross_count;
            sample.first_cross_z_y = structure_y.first_cross_z;
            sample.has_axis_cross_y = structure_y.has_cross;
        }
        if (!sample.aim_usable) {
            sample.aim_penalty = d_ray_failure_penalty(ray, cfg);
        }
        if (!sample.usable) {
            sample.trace_penalty = d_ray_failure_penalty(ray, cfg);
        }
    }
    samples[output_item] = sample;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceSpotRaySample d_transform_symmetric_spot_sample(
    const DeviceSpotRaySample& source,
    int transform_code) {
    DeviceSpotRaySample out = source;
    double xx = 1.0;
    double xy = 0.0;
    double yx = 0.0;
    double yy = 1.0;
    if (transform_code == 1) {
        xx = -1.0;
    } else if (transform_code == 2) {
        yy = -1.0;
    } else if (transform_code == 3) {
        xx = -1.0;
        yy = -1.0;
    } else if (transform_code == 4) {
        xx = 0.0;
        xy = 1.0;
        yx = 1.0;
        yy = 0.0;
    } else if (transform_code == 5) {
        xx = 0.0;
        xy = -1.0;
        yx = 1.0;
        yy = 0.0;
    } else if (transform_code == 6) {
        xx = 0.0;
        xy = 1.0;
        yx = -1.0;
        yy = 0.0;
    } else if (transform_code == 7) {
        xx = 0.0;
        xy = -1.0;
        yx = -1.0;
        yy = 0.0;
    }
    const double source_x = source.x;
    const double source_y = source.y;
    const double source_l = source.l;
    const double source_m = source.m;
    out.x = xx * source_x + xy * source_y;
    out.y = yx * source_x + yy * source_y;
    out.l = xx * source_l + xy * source_m;
    out.m = yx * source_l + yy * source_m;
    const double initial_x = source.initial_x;
    const double initial_y = source.initial_y;
    const double initial_l = source.initial_l;
    const double initial_m = source.initial_m;
    out.initial_x = xx * initial_x + xy * initial_y;
    out.initial_y = yx * initial_x + yy * initial_y;
    out.initial_l = xx * initial_l + xy * initial_m;
    out.initial_m = yx * initial_l + yy * initial_m;
    const double pupil_x = source.pupil_x;
    const double pupil_y = source.pupil_y;
    out.pupil_x = xx * pupil_x + xy * pupil_y;
    out.pupil_y = yx * pupil_x + yy * pupil_y;
    if (d_finite(source.stop_x) && d_finite(source.stop_y)) {
        out.stop_x = xx * source.stop_x + xy * source.stop_y;
        out.stop_y = yx * source.stop_x + yy * source.stop_y;
    }
    if (fabs(xy) > 0.5) {
        out.axis_cross_count = source.axis_cross_count_y;
        out.first_cross_z = source.first_cross_z_y;
        out.has_axis_cross = source.has_axis_cross_y;
        out.axis_cross_count_y = source.axis_cross_count;
        out.first_cross_z_y = source.first_cross_z;
        out.has_axis_cross_y = source.has_axis_cross;
    }
    return out;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void reconstruct_symmetric_spot_samples_kernel(
    const DeviceField* fields,
    DeviceConfig cfg,
    int population_size,
    int pupil_stride,
    DeviceSpotRaySample* samples, int evaluation_phase=0) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count =
        population_size * cfg.field_count * cfg.wavelength_count;
    if (item >= item_count) {
        return;
    }
    int remainder = item;
    const int wave_index = remainder % cfg.wavelength_count;
    remainder /= cfg.wavelength_count;
    const int field_index = remainder % cfg.field_count;
    const int row = remainder / cfg.field_count;
    if ((evaluation_phase==1 && field_index<cfg.field_count-3) ||
        (evaluation_phase==2 && field_index>=cfg.field_count-3)) return;
    fields = d_candidate_row_fields(fields, cfg, row);
    const DeviceField field = fields[field_index];
    const int full_count = d_field_pupil_count(cfg, field);
    const int physical_count = d_field_physical_pupil_count(cfg, field);
    if (physical_count >= full_count) {
        return;
    }
    const long long sample_base =
        ((static_cast<long long>(row) * cfg.field_count + field_index) *
             cfg.wavelength_count +
         wave_index) *
        pupil_stride;
    // Descending in-place expansion is safe because every compact
    // representative index is no greater than its first logical destination.
    for (int logical_index = full_count - 1; logical_index >= 0;
         --logical_index) {
        int physical_index = -1;
        int transform_code = 0;
        if (!d_field_logical_pupil_mapping(
                cfg,
                field,
                logical_index,
                &physical_index,
                &transform_code) ||
            physical_index < 0 || physical_index >= physical_count) {
            samples[sample_base + logical_index] = DeviceSpotRaySample{};
            continue;
        }
        const DeviceSpotRaySample source = samples[sample_base + physical_index];
        samples[sample_base + logical_index] =
            d_transform_symmetric_spot_sample(source, transform_code);
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void trace_wavefront_guard_rays_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* calibrated_models,
    DeviceConfig cfg,
    int population_size,
    DeviceSpotRaySample* samples,
    int reuse_y_guards,
    int track_opd) {
    constexpr int kGuardCount = 4;
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count =
        population_size * cfg.field_count * cfg.wavelength_count * kGuardCount;
    if (item >= item_count) {
        return;
    }
    int remainder = item;
    const int guard_index = remainder % kGuardCount;
    remainder /= kGuardCount;
    const int wave_index = remainder % cfg.wavelength_count;
    if (reuse_y_guards && guard_index >= 2 &&
        (!cfg.primary_wavelength_apertures || wave_index == cfg.primary_wave_index)) return;
    remainder /= cfg.wavelength_count;
    const int field_index = remainder % cfg.field_count;
    const int row = remainder / cfg.field_count;
    if (evaluation_skip_outer_row(row)) { samples[item]=DeviceSpotRaySample{}; return; }
    fields = d_candidate_row_fields(fields, cfg, row);
    DeviceSpotRaySample sample{};
    const DeviceWave wave = waves[wave_index];
    if (wave.weight > 0.0) {
        const double px = guard_index == 0 ? 1.0 : guard_index == 1 ? -1.0 : 0.0;
        const double py = guard_index == 2 ? 1.0 : guard_index == 3 ? -1.0 : 0.0;
        const DeviceSurface* surfaces =
            candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
        const long long model_index =
            (static_cast<long long>(row) * cfg.field_count + field_index) *
                cfg.wavelength_count +
            wave_index;
        const DeviceCalibratedModel* calibrated_model =
            calibrated_models ? calibrated_models + model_index : nullptr;
        DeviceRay ray = d_generate_supported_ray(
            surfaces,
            cfg,
            fields[field_index],
            px,
            py,
            1.0,
            wave.value_um,
            calibrated_model, true);
        sample.initial_x = ray.x;
        sample.initial_y = ray.y;
        sample.initial_z = ray.z;
        sample.initial_l = ray.l;
        sample.initial_m = ray.m;
        sample.initial_n = ray.n;
        d_trace_ray(
            surfaces,
            cfg,
            &ray,
            nullptr,
            false,
            nullptr,
            track_opd != 0);
        ray.nominal_weight *= wave.weight;
        ray.intensity *= wave.weight;
        sample.x = ray.x;
        sample.y = ray.y;
        sample.z = ray.z;
        sample.l = ray.l;
        sample.m = ray.m;
        sample.n = ray.n;
        sample.opd = ray.opd;
        sample.pupil_x = px;
        sample.pupil_y = py;
        sample.stop_x = ray.stop_x;
        sample.stop_y = ray.stop_y;
        sample.stop_reached = ray.stop_reached;
        sample.failure_code = ray.failure_code;
        sample.min_backward_air_gap = ray.min_backward_air_gap;
        sample.min_backward_glass_gap = ray.min_backward_glass_gap;
        sample.failure_surface = ray.failure_surface;
        sample.weight = d_ray_nominal_weight(ray);
        sample.wavefront_weight = sample.weight;
        sample.weighted_progress =
            sample.weight * d_ray_progress(ray, cfg.surface_count);
        sample.aim_usable = d_ray_aim_usable_for_spot(ray) ? 1 : 0;
        sample.normal_aperture_blockage = d_normal_secondary_blockage(ray) ? 1 : 0;
        sample.usable = d_ray_usable_for_spot(ray) ? 1 : 0;
    }
    samples[item] = sample;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void trace_spot_tail_extra_edge_rays_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* calibrated_models,
    DeviceConfig cfg,
    int population_size,
    DeviceSpotRaySample* samples) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count =
        population_size * cfg.field_count * cfg.wavelength_count *
        kSpotTailExtraEdgeCount;
    if (item >= item_count) {
        return;
    }
    int remainder = item;
    const int logical_arm = remainder % kSpotTailExtraEdgeCount;
    remainder /= kSpotTailExtraEdgeCount;
    const int wave_index = remainder % cfg.wavelength_count;
    remainder /= cfg.wavelength_count;
    const int field_index = remainder % cfg.field_count;
    const int row = remainder / cfg.field_count;
    if (evaluation_skip_outer_row(row)) { samples[item]=DeviceSpotRaySample{}; return; }
    fields = d_candidate_row_fields(fields, cfg, row);
    DeviceSpotRaySample sample{};
    const DeviceField field = fields[field_index];
    const DeviceWave wave = waves[wave_index];
    const int symmetry = d_field_pupil_symmetry(cfg, field);
    const int representative_arm =
        d_spot_tail_extra_canonical_arm(logical_arm, symmetry);
    if (wave.weight > 0.0 &&
        d_uses_selective_spot_tail_edge_sampling(cfg, field) &&
        representative_arm == logical_arm) {
        double px = 0.0;
        double py = 0.0;
        d_spot_tail_extra_edge_pupil_at(logical_arm, &px, &py);
        const DeviceSurface* surfaces =
            candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
        const long long model_index =
            (static_cast<long long>(row) * cfg.field_count + field_index) *
                cfg.wavelength_count +
            wave_index;
        const DeviceCalibratedModel* calibrated_model =
            calibrated_models ? calibrated_models + model_index : nullptr;
        DeviceRay ray = d_generate_supported_ray(
            surfaces,
            cfg,
            field,
            px,
            py,
            1.0,
            wave.value_um,
            calibrated_model, true);
        sample.initial_x = ray.x;
        sample.initial_y = ray.y;
        sample.initial_z = ray.z;
        sample.initial_l = ray.l;
        sample.initial_m = ray.m;
        sample.initial_n = ray.n;
        d_trace_ray(surfaces, cfg, &ray);
        ray.nominal_weight *= wave.weight;
        ray.intensity *= wave.weight;
        sample.x = ray.x;
        sample.y = ray.y;
        sample.z = ray.z;
        sample.l = ray.l;
        sample.m = ray.m;
        sample.n = ray.n;
        sample.pupil_x = px;
        sample.pupil_y = py;
        sample.stop_x = ray.stop_x;
        sample.stop_y = ray.stop_y;
        sample.stop_reached = ray.stop_reached;
        sample.failure_code = ray.failure_code;
        sample.min_backward_air_gap = ray.min_backward_air_gap;
        sample.min_backward_glass_gap = ray.min_backward_glass_gap;
        sample.failure_surface = ray.failure_surface;
        sample.weight = d_ray_nominal_weight(ray);
        sample.weighted_progress =
            sample.weight * d_ray_progress(ray, cfg.surface_count);
        sample.aim_usable = d_ray_aim_usable_for_spot(ray) ? 1 : 0;
        sample.normal_aperture_blockage = d_normal_secondary_blockage(ray) ? 1 : 0;
        sample.usable = d_ray_usable_for_spot(ray) ? 1 : 0;
    }
    samples[item] = sample;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void trace_paraxial_chief_rays_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* calibrated_models,
    DeviceConfig cfg,
    int population_size,
    DeviceChiefRaySample* samples,
    int track_opd) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count =
        population_size * cfg.field_count * cfg.wavelength_count;
    if (item >= item_count) {
        return;
    }
    int remainder = item;
    const int wave_index = remainder % cfg.wavelength_count;
    remainder /= cfg.wavelength_count;
    const int field_index = remainder % cfg.field_count;
    const int row = remainder / cfg.field_count;
    fields = d_candidate_row_fields(fields, cfg, row);
    DeviceChiefRaySample sample{};
    const DeviceWave wave = waves[wave_index];
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const DeviceField& chief_field = fields[field_index];
    const long long model_index =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
            cfg.wavelength_count + wave_index;
    const DeviceCalibratedModel* calibrated_model =
        calibrated_models ? calibrated_models + model_index : nullptr;
    DeviceRay ray = cfg.aiming_mode == 5
                        ? d_generate_calibrated_chief_ray(
                              surfaces,
                              cfg,
                              chief_field,
                              1.0,
                              wave.value_um,
                              calibrated_model)
                        : d_generate_supported_ray(
                              surfaces,
                              cfg,
                              chief_field,
                              0.0,
                              0.0,
                              1.0,
                              wave.value_um,
                              nullptr);
    sample.initial_x = ray.x;
    sample.initial_y = ray.y;
    sample.initial_z = ray.z;
    sample.initial_l = ray.l;
    sample.initial_m = ray.m;
    sample.initial_n = ray.n;
    double ux = 0.0;
    double uy = 0.0;
    d_field_transverse_unit(chief_field, &ux, &uy);
    sample.input_component = ray.l * ux + ray.m * uy;
    DeviceStructuralTrace structure{};
    DeviceStructuralTrace* structure_ptr = nullptr;
    if (cfg.reject_ray_path_structural_failures &&
        d_field_is_off_axis(chief_field)) {
        structure.ux = ux;
        structure.uy = uy;
        structure.epsilon = fmax(0.0, cfg.axis_crossing_epsilon);
        structure.input_axis = sample.input_component;
        d_initialize_structural_trace(&structure, ray);
        structure_ptr = &structure;
    }
    d_trace_ray(
        surfaces, cfg, &ray, nullptr, false, structure_ptr, track_opd != 0);
    sample.wave_weight = wave.weight;
    sample.weighted_progress =
        wave.weight * d_ray_progress(ray, cfg.surface_count);
    sample.usable = (d_ray_usable_for_spot(ray) || d_normal_secondary_blockage(ray)) ? 1 : 0;
    sample.image_x = ray.x;
    sample.image_y = ray.y;
    sample.image_z = ray.z;
    sample.image_l = ray.l;
    sample.image_m = ray.m;
    sample.image_n = ray.n;
    sample.opd = ray.opd;
    sample.traced = 1;
    sample.valid = ray.valid;
    if (structure_ptr) {
        sample.axis_cross_count = structure.axis_cross_count;
        sample.first_surface_axis = structure.first_surface_axis;
        sample.input_axis = structure.input_axis;
        sample.first_surface_valid = structure.first_surface_valid;
    }
    samples[item] = sample;
}
} // namespace
} // namespace optiland
