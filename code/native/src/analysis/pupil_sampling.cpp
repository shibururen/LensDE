/*
 * Provides merit pupil lists/transforms, guard-history sampling and shared bound/name helpers.
 * Spot, wavefront and ray-order analysis use these common sampling definitions.
 * Seed/profile selection, symmetry transforms and added guard-pupil ordering must
 * preserve the correspondence between traced rays, history slots and residual arrays.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
double square_error(double value, double target) {
    if (!std::isfinite(value) || !std::isfinite(target)) {
        return 1.0e12;
    }
    const double d = value - target;
    return d * d;
}
}  // namespace analysis_detail

namespace analysis_detail {
double bound_penalty(double value, double lower, double upper, double weight) {
    if (!std::isfinite(value)) {
        return std::abs(weight) * 1.0e12;
    }
    double d = 0.0;
    if (std::isfinite(lower) && value < lower) {
        d = lower - value;
    } else if (std::isfinite(upper) && value > upper) {
        d = value - upper;
    }
    if (d <= 0.0) {
        return 0.0;
    }
    return std::abs(weight) * d * d;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool thickness_constraint_is_active(
    const MeritConfig& config,
    std::size_t surface_index) {
    return !config.thickness_constraints_variable_only ||
           std::find(
               config.thickness_variable_indices.begin(),
               config.thickness_variable_indices.end(),
               surface_index) != config.thickness_variable_indices.end();
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_interval_thickness_constraint_kind(const std::string& kind) {
    return kind == "thickness" || kind == "center_thickness" ||
           kind == "edge_thickness" || kind == "atc" ||
           kind == "glass_atc" || kind == "center_thickness_od_ratio" ||
           kind == "glass_center_thickness_od_ratio" || kind == "ate" ||
           kind == "glass_ate" || kind == "edge_thickness_od_ratio" ||
           kind == "glass_edge_thickness_od_ratio";
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<PupilPoint> single_ray_guard_edge_pupils() {
    return {
        PupilPoint{1.0, 0.0, 1.0},
        PupilPoint{-1.0, 0.0, 1.0},
        PupilPoint{0.0, 1.0, 1.0},
        PupilPoint{0.0, -1.0, 1.0},
    };
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<PupilPoint> single_ray_spot_tail_extra_edge_pupils() {
    constexpr int kExtraEdgeArms = 8;
    std::vector<PupilPoint> pupils;
    pupils.reserve(kExtraEdgeArms);
    for (int arm = 0; arm < kExtraEdgeArms; ++arm) {
        const double theta =
            2.0 * kPi * (static_cast<double>(arm) + 0.5) /
            static_cast<double>(kExtraEdgeArms);
        pupils.push_back(PupilPoint{std::cos(theta), std::sin(theta), 1.0});
    }
    return pupils;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool same_pupil_point(const PupilPoint& lhs, const PupilPoint& rhs) {
    return std::abs(lhs.px - rhs.px) < 1.0e-12 &&
           std::abs(lhs.py - rhs.py) < 1.0e-12;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_single_ray_guard_edge_pupil(const PupilPoint& pupil) {
    static const auto edges = single_ray_guard_edge_pupils();
    return std::any_of(
        edges.begin(),
        edges.end(),
        [&](const PupilPoint& edge) {
            return same_pupil_point(pupil, edge);
        });
}
}  // namespace analysis_detail

namespace analysis_detail {
bool is_vignetting_boundary_support_operand_name(const std::string& raw_name) {
    return raw_name == "vignetting_boundary_aperture_support";
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<PupilPoint> missing_single_ray_guard_edge_pupils(
    const std::vector<PupilPoint>& base_pupils) {
    std::vector<PupilPoint> pupils;
    for (const PupilPoint& edge : single_ray_guard_edge_pupils()) {
        const bool exists = std::any_of(
            base_pupils.begin(),
            base_pupils.end(),
            [&](const PupilPoint& point) {
                return same_pupil_point(point, edge);
            });
        if (!exists) {
            pupils.push_back(edge);
        }
    }
    return pupils;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<PupilPoint> missing_pupil_points(
    const std::vector<PupilPoint>& candidates,
    const std::vector<PupilPoint>& base_pupils) {
    std::vector<PupilPoint> pupils;
    for (const PupilPoint& candidate : candidates) {
        const bool exists = std::any_of(
            base_pupils.begin(),
            base_pupils.end(),
            [&](const PupilPoint& point) {
                return same_pupil_point(point, candidate);
            });
        if (!exists) {
            pupils.push_back(candidate);
        }
    }
    return pupils;
}
}  // namespace analysis_detail

namespace analysis_detail {
void append_invalid_wavefront_guard_residuals(WavefrontStats& stats, std::size_t count) {
    stats.single_ray_wavefront_guard_residuals.insert(
        stats.single_ray_wavefront_guard_residuals.end(),
        count,
        std::numeric_limits<double>::quiet_NaN());
}
}  // namespace analysis_detail

namespace analysis_detail {
double active_lower_bound(double value) {
    return value > 0.0 ? value : -kInf;
}
}  // namespace analysis_detail

namespace analysis_detail {
double active_upper_bound(double value) {
    return value > 0.0 ? value : kInf;
}
}  // namespace analysis_detail

namespace analysis_detail {
double scoring_weight(double weight) {
    return std::isfinite(weight) ? std::abs(weight) : 0.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::string normalized_name(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        if (c == '-' || c == ' ') {
            return '_';
        }
        return static_cast<char>(std::tolower(c));
    });
    return name;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::string canonical_distortion_model(std::string model) {
    const std::string value = normalized_name(std::move(model));
    if (value == "f_theta" || value == "ftheta" || value == "theta") {
        return "f_theta";
    }
    if (value == "f_tan_theta" || value == "f_tantheta" ||
        value == "ftan_theta" || value == "ftantheta" ||
        value == "tan_theta" || value == "tantheta") {
        return "f_tan_theta";
    }
    return "auto";
}
}  // namespace analysis_detail

namespace analysis_detail {
bool distortion_uses_f_theta(const LensSystem& system, const MeritConfig& config) {
    const std::string model = canonical_distortion_model(config.distortion_model);
    if (model == "f_theta") {
        return true;
    }
    if (model == "f_tan_theta") {
        return false;
    }
    return std::abs(system.max_field_deg) >= 80.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::string effective_distortion_model_name(const LensSystem& system, const MeritConfig& config) {
    return distortion_uses_f_theta(system, config) ? "f_theta" : "f_tan_theta";
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<Field> fields_or_default(const LensSystem& system) {
    if (!system.fields.empty()) {
        return system.fields;
    }
    return {Field{}};
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<Wavelength> wavelengths_or_default(const LensSystem& system) {
    if (!system.wavelengths.empty()) {
        return system.wavelengths;
    }
    return {Wavelength{}};
}
}  // namespace analysis_detail

namespace analysis_detail {
bool mode_uses_prescribed_stop_target(RayAimingMode mode) {
    return mode != RayAimingMode::Direct && mode != RayAimingMode::Paraxial;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::pair<int, int> merit_field_pupil_shape(
    const LensSystem& system,
    const MeritConfig& config,
    const Field& field) {
    const double radius = std::hypot(field.hx, field.hy);
    const bool elevated =
        config.elevated_field_num_rays > 0 &&
        config.elevated_field_pupil_arms > 0 &&
        radius + 1.0e-12 >= config.elevated_field_sampling_min_radius;
    const bool order_active = config.pupil_order_prior_weight > 0.0 &&
        config.pupil_order_soft_tolerance > 0.0 && radius > 1.0e-12 &&
        radius + 1.0e-12 >= config.pupil_order_min_field_radius;
    const int balanced = fixed_budget_pupil_shape(
        is_fixed_budget_pupil_distribution(config.pupil_distribution),
        config.num_rays, config.pupil_arms, elevated, system.max_field_deg,
        field.hx,field.hy,field.fvcx,field.fvcy,field.fvdx,field.fvdy,order_active);
    if (balanced == 1) return {12,4};
    if (balanced == 2) return {8,6};
    return elevated
               ? std::pair<int, int>{
                     config.elevated_field_num_rays,
                     config.elevated_field_pupil_arms}
               : std::pair<int, int>{config.num_rays, config.pupil_arms};
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<PupilPoint> merit_field_pupils(
    const LensSystem& system,
    const MeritConfig& config,
    const Field& field,
    MeritPupilMetric metric) {
    if (is_codev_rectangular_distribution(config.pupil_distribution)) {
        double aperture_weight = 0.0;
        if (metric == MeritPupilMetric::Spot) {
            aperture_weight = config.codev_spot_aperture_weight;
        } else if (metric == MeritPupilMetric::Wavefront) {
            aperture_weight = config.codev_wavefront_aperture_weight;
        }
        return make_codev_rectangular_distribution(
            effective_codev_ray_interval(system, config),
            config.codev_obscuration,
            aperture_weight);
    }
    const auto shape = merit_field_pupil_shape(system, config, field);
    return make_pupil_distribution(
        shape.first,
        config.pupil_distribution,
        config.sampling_seed,
        shape.second);
}
}  // namespace analysis_detail

namespace analysis_detail {
PupilSymmetry field_pupil_symmetry(
    const Field& field,
    const MeritConfig& config) {
    if (!config.codev_use_symmetry) {
        return PupilSymmetry::None;
    }
    constexpr double tolerance = 1.0e-13;
    const bool axial_field =
        std::abs(field.hx) <= tolerance && std::abs(field.hy) <= tolerance;
    const bool centered_vignetting =
        std::abs(field.fvcx) <= tolerance && std::abs(field.fvcy) <= tolerance;
    if (axial_field && centered_vignetting) {
        if (std::abs(field.fvdx - field.fvdy) <= tolerance) {
            return PupilSymmetry::Dihedral4;
        }
        return PupilSymmetry::SignXY;
    }
    if (std::abs(field.hx) <= tolerance && std::abs(field.fvcx) <= tolerance) {
        return PupilSymmetry::MirrorX;
    }
    if (std::abs(field.hy) <= tolerance && std::abs(field.fvcy) <= tolerance) {
        return PupilSymmetry::MirrorY;
    }
    return PupilSymmetry::None;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool pupil_transform_between(
    const PupilPoint& source,
    const PupilPoint& target,
    PupilSymmetry symmetry,
    PupilTransform* transform) {
    if (!transform) {
        return false;
    }
    constexpr double tolerance = 2.0e-12;
    const double weight_scale = std::max(
        {1.0, std::abs(source.weight), std::abs(target.weight)});
    if (!std::isfinite(source.weight) || !std::isfinite(target.weight) ||
        std::abs(source.weight - target.weight) > tolerance * weight_scale) {
        return false;
    }
    const PupilTransform transforms[] = {
        {1.0, 0.0, 0.0, 1.0},
        {-1.0, 0.0, 0.0, 1.0},
        {1.0, 0.0, 0.0, -1.0},
        {-1.0, 0.0, 0.0, -1.0},
        {0.0, 1.0, 1.0, 0.0},
        {0.0, -1.0, 1.0, 0.0},
        {0.0, 1.0, -1.0, 0.0},
        {0.0, -1.0, -1.0, 0.0},
    };
    std::size_t allowed_begin = 0;
    std::size_t allowed_end = 1;
    if (symmetry == PupilSymmetry::MirrorX) {
        allowed_end = 2;
    } else if (symmetry == PupilSymmetry::MirrorY) {
        const PupilTransform mirror_y[] = {
            transforms[0], transforms[2]};
        for (const PupilTransform& candidate : mirror_y) {
            const double x = candidate.xx * source.px + candidate.xy * source.py;
            const double y = candidate.yx * source.px + candidate.yy * source.py;
            if (std::abs(x - target.px) <= tolerance &&
                std::abs(y - target.py) <= tolerance) {
                *transform = candidate;
                return true;
            }
        }
        return false;
    } else if (symmetry == PupilSymmetry::SignXY) {
        allowed_end = 4;
    } else if (symmetry == PupilSymmetry::Dihedral4) {
        allowed_end = 8;
    }
    for (std::size_t i = allowed_begin; i < allowed_end; ++i) {
        const PupilTransform& candidate = transforms[i];
        const double x = candidate.xx * source.px + candidate.xy * source.py;
        const double y = candidate.yx * source.px + candidate.yy * source.py;
        if (std::abs(x - target.px) <= tolerance &&
            std::abs(y - target.py) <= tolerance) {
            *transform = candidate;
            return true;
        }
    }
    return false;
}
}  // namespace analysis_detail

namespace analysis_detail {
Vec3 transform_transverse(const Vec3& value, const PupilTransform& transform) {
    return {
        transform.xx * value.x + transform.xy * value.y,
        transform.yx * value.x + transform.yy * value.y,
        value.z};
}
}  // namespace analysis_detail

namespace analysis_detail {
Ray transform_symmetric_ray(
    const Ray& source,
    const PupilTransform& transform,
    double target_weight) {
    Ray out = source;
    out.p = transform_transverse(source.p, transform);
    out.d = transform_transverse(source.d, transform);
    if (std::isfinite(source.stop_x) && std::isfinite(source.stop_y)) {
        out.stop_x = transform.xx * source.stop_x + transform.xy * source.stop_y;
        out.stop_y = transform.yx * source.stop_x + transform.yy * source.stop_y;
    }
    const double nominal = std::max(0.0, source.nominal_weight);
    const double relative_intensity = nominal > 0.0 ? source.intensity / nominal : 0.0;
    out.nominal_weight = std::max(0.0, target_weight);
    out.intensity = relative_intensity * out.nominal_weight;
    return out;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<TracePoint> transform_symmetric_history(
    const std::vector<TracePoint>& source,
    const PupilTransform& transform) {
    std::vector<TracePoint> out = source;
    for (TracePoint& point : out) {
        const double x = point.x;
        const double y = point.y;
        const double l = point.l;
        const double m = point.m;
        point.x = transform.xx * x + transform.xy * y;
        point.y = transform.yx * x + transform.yy * y;
        point.l = transform.xx * l + transform.xy * m;
        point.m = transform.yx * l + transform.yy * m;
    }
    return out;
}
}  // namespace analysis_detail

namespace analysis_detail {
SymmetricTraceBatch trace_pupil_batch(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave,
    const std::vector<PupilPoint>& pupils,
    bool keep_history ,
    std::size_t pupil_index_offset ,
    const std::vector<unsigned char>* history_mask ,
    bool predict_image_guard ) {
    SymmetricTraceBatch batch;
    batch.initial_rays.resize(pupils.size());
    batch.rays.resize(pupils.size());
    const bool keep_selected_history =
        history_mask && history_mask->size() == pupils.size() &&
        std::any_of(history_mask->begin(), history_mask->end(), [](unsigned char value) {
            return value != 0;
        });
    if (keep_history || keep_selected_history) {
        batch.histories.resize(pupils.size());
    }
    if (pupils.empty()) {
        return batch;
    }

    const PupilSymmetry symmetry = field_pupil_symmetry(field, config);
    std::vector<std::size_t> representative_for(pupils.size(), 0);
    std::vector<PupilTransform> transforms(pupils.size());
    std::vector<std::size_t> representatives;
    representatives.reserve(pupils.size());
    if (symmetry == PupilSymmetry::None &&
        detail::pupil_positions_are_distinct(pupils, 2.0e-12)) {
        // The certificate excludes identity matches, so every point is its
        // own representative. Keep original order and pupil-index semantics.
        for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
            representatives.push_back(pi);
            representative_for[pi] = pi;
        }
    } else {
    for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
        bool matched = false;
        for (std::size_t representative : representatives) {
            PupilTransform transform;
            if (pupil_transform_between(
                    pupils[representative], pupils[pi], symmetry, &transform)) {
                representative_for[pi] = representative;
                transforms[pi] = transform;
                matched = true;
                break;
            }
        }
        if (!matched) {
            representatives.push_back(pi);
            representative_for[pi] = pi;
            transforms[pi] = PupilTransform{};
        }
    }
    }
    batch.physically_traced_rays = representatives.size();

    std::vector<Ray> representative_rays;
    representative_rays.reserve(representatives.size());
    std::vector<PupilPoint> representative_pupils;
    representative_pupils.reserve(representatives.size());
    for (std::size_t representative : representatives) {
        // During primary-wavelength aperture optimization, map every
        // secondary sample through the same cheap boundary predictor. This
        // keeps a fixed quadrature inside the retained physical pupil instead
        // of dropping samples discontinuously as the primary envelope moves.
        const bool predict_secondary_sample = predict_image_guard ||
            system.defer_secondary_aperture_clipping(wave.value_um);
        const PupilPoint sample_pupil = predict_secondary_sample
            ? aiming.predict_secondary_image_guard(field_index, wave_index, field, wave, pupils[representative])
            : pupils[representative];
        Ray initial = ray_aiming_uses_session(system.ray_aiming.mode)
                          ? aiming.generate_for_trace(
                                field_index,
                                wave_index,
                                pupil_index_offset + representative,
                                field,
                                wave,
                                sample_pupil)
                          : tracer.generate_ray(field, sample_pupil, wave.value_um);
        const double source_radius = std::hypot(pupils[representative].px, pupils[representative].py);
        initial.edge_prediction_scale = source_radius > 0.0 ? std::hypot(sample_pupil.px, sample_pupil.py) / source_radius : 1.0;
        batch.initial_rays[representative] = initial;
        representative_rays.push_back(initial);
        representative_pupils.push_back(sample_pupil);
    }
    if (keep_history) {
        // Preserve the established scalar-history path used by the centered
        // structural guard.  Selective pupil-order histories below use the
        // prepared batch path, but must not perturb this older diagnostic.
        for (std::size_t ri = 0; ri < representatives.size(); ++ri) {
            const std::size_t representative = representatives[ri];
            tracer.trace_ray(
                representative_rays[ri],
                &batch.histories[representative]);
            batch.rays[representative] = representative_rays[ri];
        }
    } else if (keep_selected_history) {
        std::vector<unsigned char> representative_history_mask(
            representatives.size(), 0);
        for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
            if ((*history_mask)[pi] == 0) {
                continue;
            }
            const std::size_t representative = representative_for[pi];
            const auto it = std::find(
                representatives.begin(), representatives.end(), representative);
            if (it != representatives.end()) {
                representative_history_mask[static_cast<std::size_t>(
                    std::distance(representatives.begin(), it))] = 1;
            }
        }
        std::vector<std::vector<TracePoint>> representative_histories;
        tracer.trace_rays_with_selected_histories(
            representative_rays,
            representative_history_mask,
            &representative_histories);
        for (std::size_t ri = 0; ri < representatives.size(); ++ri) {
            const std::size_t representative = representatives[ri];
            batch.rays[representative] = representative_rays[ri];
            if (representative_history_mask[ri] != 0) {
                batch.histories[representative] =
                    std::move(representative_histories[ri]);
            }
        }
    } else {
        tracer.trace_rays(representative_rays);
        for (std::size_t ri = 0; ri < representatives.size(); ++ri) {
            batch.rays[representatives[ri]] = representative_rays[ri];
        }
    }

    if (system.ray_aiming.mode == RayAimingMode::AdaptivePilotGrid) {
        for (std::size_t ri = 0; ri < representatives.size(); ++ri) {
            const std::size_t pi = representatives[ri];
            const double edge_scale = batch.initial_rays[pi].edge_prediction_scale;
            if (aiming.finish_trace(field_index, wave_index, pupil_index_offset + pi,
                                    field, wave, representative_pupils[ri],
                                    batch.initial_rays[pi], batch.rays[pi])) {
                batch.initial_rays[pi].edge_prediction_scale = edge_scale;
                batch.rays[pi].edge_prediction_scale = edge_scale;
                if (!batch.histories.empty() && !batch.histories[pi].empty()) {
                    batch.rays[pi] = batch.initial_rays[pi];
                    batch.histories[pi].clear();
                    tracer.trace_ray(batch.rays[pi], &batch.histories[pi]);
                }
            }
        }
    }

    for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
        const std::size_t representative = representative_for[pi];
        if (representative == pi) {
            continue;
        }
        batch.initial_rays[pi] = transform_symmetric_ray(
            batch.initial_rays[representative], transforms[pi], pupils[pi].weight);
        batch.rays[pi] = transform_symmetric_ray(
            batch.rays[representative], transforms[pi], pupils[pi].weight);
        const bool logical_history_requested =
            keep_history ||
            (keep_selected_history && (*history_mask)[pi] != 0);
        if (logical_history_requested) {
            batch.histories[pi] = transform_symmetric_history(
                batch.histories[representative], transforms[pi]);
        }
    }
    return batch;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<unsigned char> pupil_order_history_mask(
    const Field& field,
    const MeritConfig& config,
    const std::vector<PupilPoint>& pupils) {
    std::vector<unsigned char> mask(pupils.size(), 0);
    const double field_radius = std::hypot(field.hx, field.hy);
    if (!(config.pupil_order_prior_weight > 0.0) ||
        !(config.pupil_order_soft_tolerance > 0.0) ||
        field_radius + 1.0e-12 < config.pupil_order_min_field_radius ||
        field_radius <= 1.0e-12) {
        return mask;
    }
    const double ux = field.hx / field_radius;
    const double uy = field.hy / field_radius;
    constexpr double kMeridionalTolerance = 1.0e-10;
    for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
        const double sagittal = -pupils[pi].px * uy + pupils[pi].py * ux;
        if (std::abs(sagittal) <= kMeridionalTolerance) {
            mask[pi] = 1;
        }
    }
    return mask;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool pupil_order_enabled_for_field(
    const Field& field,
    const MeritConfig& config) {
    const double field_radius = std::hypot(field.hx, field.hy);
    return config.pupil_order_prior_weight > 0.0 &&
           config.pupil_order_soft_tolerance > 0.0 &&
           field_radius > 1.0e-12 &&
           field_radius + 1.0e-12 >= config.pupil_order_min_field_radius;
}
}  // namespace analysis_detail

namespace analysis_detail {
std::vector<PupilPoint> missing_exact_pupil_order_pupils(
    const Field& field,
    const std::vector<PupilPoint>& base_pupils) {
    const double field_radius = std::hypot(field.hx, field.hy);
    if (field_radius <= 1.0e-12) {
        return {};
    }
    const double ux = field.hx / field_radius;
    const double uy = field.hy / field_radius;
    return missing_pupil_points(
        {
            PupilPoint{-ux, -uy, 1.0},
            PupilPoint{0.0, 0.0, 1.0},
            PupilPoint{ux, uy, 1.0},
        },
        base_pupils);
}
}  // namespace analysis_detail

namespace analysis_detail {
std::size_t pupil_order_last_optical_surface(const LensSystem& system) {
    const std::size_t first = system.first_optical_surface();
    const auto air_interval = [](const Surface& surface) {
        const Material& material = surface.material_after;
        return std::isfinite(material.nd) &&
               std::abs(material.nd - 1.0) <= 1.0e-12 &&
               std::isfinite(material.vd) && std::abs(material.vd) <= 1.0e-12;
    };
    for (std::size_t si = system.surfaces.size(); si-- > first;) {
        const Surface& surface = system.surfaces[si];
        if (surface.kind == SurfaceKind::Image ||
            surface.kind == SurfaceKind::Object) {
            continue;
        }
        // Air-to-air auxiliary/stop planes do not end the lens train. Keep
        // glass exit faces (including flat ones) and reflecting surfaces.
        const bool air_before = si == 0 || air_interval(system.surfaces[si - 1]);
        if (surface.reflective || !air_before || !air_interval(surface)) {
            return si;
        }
    }
    return first;
}
}  // namespace analysis_detail

namespace analysis_detail {
void apply_pupil_order_metrics(
    const LensSystem& system,
    const Field& field,
    const MeritConfig& config,
    const std::vector<unsigned char>& history_mask,
    const std::vector<std::vector<TracePoint>>& histories,
    SpotStats& stats) {
    if (histories.size() != history_mask.size() || system.surfaces.empty()) {
        return;
    }
    const std::size_t first = system.first_optical_surface();
    if (first >= system.surfaces.size()) {
        return;
    }
    // This is a soft optical-train prior, not a trace-validity or BFL
    // contract. Exclude the entire image-space air gap, including dummy faces.
    const std::size_t last = pupil_order_last_optical_surface(system);
    if (last <= first) {
        return;
    }
    const double field_radius = std::hypot(field.hx, field.hy);
    if (field_radius <= 1.0e-12) {
        return;
    }
    const double ux = field.hx / field_radius;
    const double uy = field.hy / field_radius;
    struct OrderedHistory {
        double entry_height = 0.0;
        const std::vector<TracePoint>* history = nullptr;
    };
    std::vector<OrderedHistory> ordered;
    ordered.reserve(histories.size());
    for (std::size_t pi = 0; pi < histories.size(); ++pi) {
        if (history_mask[pi] == 0 || histories[pi].size() <= last) {
            continue;
        }
        const TracePoint& entry = histories[pi][first];
        const double height = entry.x * ux + entry.y * uy;
        if (!entry.valid || !std::isfinite(height)) {
            continue;
        }
        ordered.push_back({height, &histories[pi]});
    }
    if (ordered.size() < 3) {
        return;
    }
    std::sort(
        ordered.begin(), ordered.end(), [](const OrderedHistory& lhs,
                                           const OrderedHistory& rhs) {
            return lhs.entry_height < rhs.entry_height;
        });
    const double soft_tolerance =
        std::max(1.0e-12, config.pupil_order_soft_tolerance);
    const int hard_required =
        std::max(1, config.pupil_order_hard_consecutive_surfaces);
    double min_jacobian = std::numeric_limits<double>::infinity();
    double worst_progress = std::numeric_limits<double>::quiet_NaN();
    double max_violation = 0.0;
    bool hard_fold = false;
    for (std::size_t pair = 0; pair + 1 < ordered.size(); ++pair) {
        const double denominator =
            ordered[pair + 1].entry_height - ordered[pair].entry_height;
        if (!(denominator > 1.0e-12) || !std::isfinite(denominator)) {
            continue;
        }
        int hard_streak = 0;
        for (std::size_t si = first + 1; si <= last; ++si) {
            const TracePoint& lo = (*ordered[pair].history)[si];
            const TracePoint& hi = (*ordered[pair + 1].history)[si];
            const double lo_height = lo.x * ux + lo.y * uy;
            const double hi_height = hi.x * ux + hi.y * uy;
            if (!lo.valid || !hi.valid || !std::isfinite(lo_height) ||
                !std::isfinite(hi_height)) {
                hard_streak = 0;
                continue;
            }
            const double jacobian = (hi_height - lo_height) / denominator;
            const double progress = static_cast<double>(si - first) /
                                    static_cast<double>(last - first);
            if (jacobian < min_jacobian) {
                min_jacobian = jacobian;
                worst_progress = progress;
            }
            const double early_weight = (1.0 - progress) * (1.0 - progress);
            max_violation = std::max(
                max_violation,
                early_weight *
                    std::max(0.0, config.pupil_order_jacobian_floor - jacobian) /
                    soft_tolerance);
            if (progress <= config.pupil_order_hard_max_progress + 1.0e-12 &&
                jacobian < config.pupil_order_hard_jacobian) {
                ++hard_streak;
                if (hard_streak >= hard_required) {
                    hard_fold = true;
                }
            } else {
                hard_streak = 0;
            }
        }
    }
    if (!std::isfinite(min_jacobian)) {
        return;
    }
    stats.min_pupil_order_jacobian = min_jacobian;
    stats.pupil_order_worst_surface_progress = worst_progress;
    stats.pupil_order_prior_violation = max_violation;
    stats.pupil_order_fold =
        min_jacobian < config.pupil_order_jacobian_floor ? 1 : 0;
    stats.pupil_order_hard_fold = hard_fold ? 1 : 0;
    // Legacy hard-fold thresholds remain diagnostic-only. The residual above
    // contributes to merit without rejecting a trace or a stage handoff.
}
}  // namespace analysis_detail

}  // namespace optiland
