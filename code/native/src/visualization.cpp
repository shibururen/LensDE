/*
 * Builds native SVG/CSV views of lens profiles, ray paths, spots and illumination.
 * Called by CLI result publication using the same lens, aiming and sampling models
 * as analysis; retained field/wavelength order, validity labels and writer precision
 * keep exported diagnostics interpretable alongside the numerical score.
 */

#include "optiland/visualization.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace optiland {

namespace {

std::vector<Field> fields_or_default_vis(const LensSystem& system) {
    if (!system.fields.empty()) {
        return system.fields;
    }
    return {Field{}};
}

struct PrimaryWaveSelection {
    std::size_t index = 0;
    Wavelength wave{};
};

PrimaryWaveSelection primary_wave_vis(const LensSystem& system) {
    if (system.wavelengths.empty()) {
        return PrimaryWaveSelection{0, Wavelength{system.primary_wavelength(), 1.0, true}};
    }
    for (std::size_t i = 0; i < system.wavelengths.size(); ++i) {
        if (system.wavelengths[i].primary) {
            return PrimaryWaveSelection{i, system.wavelengths[i]};
        }
    }
    return PrimaryWaveSelection{0, system.wavelengths.front()};
}

std::string color_for_index(std::size_t i) {
    static const char* colors[] = {
        "#2563eb",
        "#16a34a",
        "#dc2626",
        "#7c3aed",
        "#0891b2",
    };
    return colors[i % (sizeof(colors) / sizeof(colors[0]))];
}

std::vector<PupilPoint> make_meridional_layout_distribution(int half_count) {
    const int count = std::max(3, 2 * std::max(1, half_count) + 1);
    std::vector<PupilPoint> points;
    points.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double py = count == 1
                              ? 0.0
                              : -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(count - 1);
        points.push_back(PupilPoint{0.0, py, 1.0});
    }
    return points;
}

RayAimingMode layout_ray_aiming_mode(const LensSystem& system) {
    if (system.ray_aiming.mode == RayAimingMode::Direct) {
        return RayAimingMode::Iterative;
    }
    return system.ray_aiming.mode;
}

const char* ray_aiming_mode_label(RayAimingMode mode) {
    switch (mode) {
        case RayAimingMode::Direct:
            return "direct";
        case RayAimingMode::Paraxial:
            return "paraxial";
        case RayAimingMode::CalibratedParaxial:
            return "calibrated_paraxial";
        case RayAimingMode::ThreeStep:
            return "three_step";
        case RayAimingMode::Iterative:
            return "iterative";
        case RayAimingMode::Robust:
            return "robust";
        case RayAimingMode::AdaptivePilotGrid:
            return "adaptive_pilot_grid";
    }
    return "unknown";
}

const char* ray_failure_code_label(RayFailureCode code) {
    switch (code) {
        case RayFailureCode::None:
            return "none";
        case RayFailureCode::AimNotConverged:
            return "aim_not_converged";
        case RayFailureCode::SurfaceMiss:
            return "surface_miss";
        case RayFailureCode::ApertureClip:
            return "aperture_clip";
        case RayFailureCode::TotalInternalReflection:
            return "total_internal_reflection";
        case RayFailureCode::NonFinite:
            return "non_finite";
        case RayFailureCode::BackwardPropagation:
            return "backward_propagation";
        case RayFailureCode::SagInvalid:
            return "sag_invalid";
        case RayFailureCode::GeometryInvalid:
            return "geometry_invalid";
    }
    return "unknown";
}

bool material_is_glass(const Material& material, double wavelength_um) {
    return material.refractive_index(wavelength_um) > 1.0005;
}

struct LensElement {
    std::size_t first = 0;
    std::size_t last = 0;
    double semi = 0.0;
};

double clear_aperture_radius_vis(const Surface& surface) {
    return std::max(surface.semi_aperture, surface.aperture_radius);
}

double mechanical_aperture_radius_vis(const Surface& surface) {
    return std::max(clear_aperture_radius_vis(surface), surface.mechanical_aperture);
}

std::vector<LensElement> find_lens_elements(
    const LensSystem& system,
    const std::vector<double>& semis,
    double wavelength_um) {
    std::vector<LensElement> elements;
    bool in_element = false;
    std::size_t first = 0;

    for (std::size_t i = 0; i < system.surfaces.size(); ++i) {
        if (system.surfaces[i].kind == SurfaceKind::Object) {
            continue;
        }
        const bool after_is_glass = material_is_glass(system.surfaces[i].material_after, wavelength_um);
        if (!in_element && after_is_glass) {
            in_element = true;
            first = i;
        }

        if (in_element && (!after_is_glass || i + 1 == system.surfaces.size())) {
            const std::size_t last = i;
            double semi = 0.0;
            for (std::size_t si = first; si <= last && si < semis.size(); ++si) {
                semi = std::max(semi, semis[si]);
            }
            if (last > first && semi > 0.0) {
                elements.push_back({first, last, semi});
            }
            in_element = false;
        }
    }
    return elements;
}

double finite_surface_sag(const Surface& surface, double y, double profile_radius) {
    const double sag = profile_surface_sag(surface, y, profile_radius);
    return std::isfinite(sag) ? sag : 0.0;
}

void append_surface_curve_range(
    std::ostringstream& path_data,
    const Surface& surface,
    double vertex_z,
    double y0,
    double y1,
    double profile_radius,
    const std::function<double(double)>& cx,
    const std::function<double(double)>& cy,
    bool move_to_first) {
    const int samples = 80;
    for (int j = 0; j <= samples; ++j) {
        const double t = static_cast<double>(j) / samples;
        const double y = y0 + (y1 - y0) * t;
        const double sag = finite_surface_sag(surface, y, profile_radius);
        path_data << ((j == 0 && move_to_first) ? "M " : " L ")
                  << cx(vertex_z + sag) << " " << cy(y);
    }
}

void append_surface_curve(
    std::ostringstream& path_data,
    const Surface& surface,
    double vertex_z,
    double semi,
    bool reverse,
    double profile_radius,
    const std::function<double(double)>& cx,
    const std::function<double(double)>& cy,
    bool move_to_first) {
    if (reverse) {
        append_surface_curve_range(
            path_data, surface, vertex_z, semi, -semi, profile_radius, cx, cy, move_to_first);
    } else {
        append_surface_curve_range(
            path_data, surface, vertex_z, -semi, semi, profile_radius, cx, cy, move_to_first);
    }
}

double nice_scale_length(double max_length) {
    if (!std::isfinite(max_length) || max_length <= 0.0) {
        return 0.0;
    }
    const double magnitude = std::pow(10.0, std::floor(std::log10(max_length)));
    const double normalized = max_length / magnitude;
    double step = 1.0;
    if (normalized >= 5.0) {
        step = 5.0;
    } else if (normalized >= 2.0) {
        step = 2.0;
    }
    return step * magnitude;
}

}  // namespace

bool save_layout_svg(
    const LensSystem& system,
    const std::string& path,
    int meridional_half_count,
    int width,
    int height) {
    std::vector<std::vector<TracePoint>> ray_histories;
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    const auto pupils = make_meridional_layout_distribution(meridional_half_count);
    const auto fields = fields_or_default_vis(system);
    const auto primary = primary_wave_vis(system);
    const auto waves = system.wavelengths.empty()
                           ? std::vector<Wavelength>{primary.wave}
                           : system.wavelengths;
    const double primary_wave = primary.wave.value_um;
    const RayAimingMode layout_mode = layout_ray_aiming_mode(system);

    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto& field = fields[fi];
        if (ray_aiming_uses_session(layout_mode)) {
            for (std::size_t wi = 0; wi < primary.index && wi < waves.size(); ++wi) {
                for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
                    Ray warmup = aiming.generate(fi, wi, pi, field, waves[wi], pupils[pi]);
                    tracer.trace_ray(warmup, nullptr);
                }
            }
        }
        for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
            const auto& pupil = pupils[pi];
            Ray ray = ray_aiming_uses_session(layout_mode)
                          ? aiming.generate(fi, primary.index, pi, field, primary.wave, pupil)
                          : tracer.generate_ray_with_mode(field, pupil, primary_wave, layout_mode);
            std::vector<TracePoint> history;
            tracer.trace_ray(ray, &history);
            ray_histories.push_back(std::move(history));
        }
    }

    std::vector<double> vertex_z(system.surfaces.size(), 0.0);
    std::vector<double> clear_semis(system.surfaces.size(), 0.0);
    std::vector<double> mechanical_semis(system.surfaces.size(), 0.0);
    const std::size_t first_draw_surface = system.first_optical_surface();
    double z = 0.0;
    double z_min = std::numeric_limits<double>::infinity();
    double z_max = -std::numeric_limits<double>::infinity();
    double y_abs = std::max(1.0, system.entrance_pupil_radius());
    const auto include_z = [&](double value) {
        if (std::isfinite(value)) {
            z_min = std::min(z_min, value);
            z_max = std::max(z_max, value);
        }
    };
    for (std::size_t si = 0; si < system.surfaces.size(); ++si) {
        const auto& surface = system.surfaces[si];
        if (surface.kind == SurfaceKind::Object) {
            if (std::isfinite(surface.thickness)) {
                vertex_z[si] = -surface.thickness;
            } else {
                vertex_z[si] = 0.0;
            }
            continue;
        }
        vertex_z[si] = z;
        clear_semis[si] = clear_aperture_radius_vis(surface);
        mechanical_semis[si] = mechanical_aperture_radius_vis(surface);
        y_abs = std::max(y_abs, mechanical_semis[si]);
        const double semi_for_bounds = std::max(1.0e-6, mechanical_semis[si]);
        include_z(vertex_z[si] + finite_surface_sag(surface, -semi_for_bounds, clear_semis[si]));
        include_z(vertex_z[si]);
        include_z(vertex_z[si] + finite_surface_sag(surface, semi_for_bounds, clear_semis[si]));
        if (std::isfinite(surface.thickness)) {
            z += surface.thickness;
            include_z(z);
        }
    }
    for (const auto& history : ray_histories) {
        for (std::size_t si = first_draw_surface; si < history.size() && si < mechanical_semis.size(); ++si) {
            const auto& p = history[si];
            if (p.valid && std::isfinite(p.z) && std::isfinite(p.x) && std::isfinite(p.y)) {
                include_z(p.z);
                const double radial = std::sqrt(p.x * p.x + p.y * p.y);
                if (std::isfinite(radial)) {
                    y_abs = std::max(y_abs, radial * 1.02);
                }
            }
        }
    }
    if (!std::isfinite(z_min) || !std::isfinite(z_max) || z_max <= z_min) {
        z_min = 0.0;
        z_max = std::max(1.0, system.total_track());
    }

    const auto lens_elements = find_lens_elements(system, mechanical_semis, primary_wave);
    std::vector<double> draw_mechanical_semis = mechanical_semis;
    for (const auto& element : lens_elements) {
        for (std::size_t si = element.first; si <= element.last && si < draw_mechanical_semis.size(); ++si) {
            draw_mechanical_semis[si] = std::max(draw_mechanical_semis[si], element.semi);
        }
        y_abs = std::max(y_abs, element.semi);
    }

    y_abs *= 1.2;
    const double margin_left = 42.0;
    const double margin_right = 28.0;
    const double margin_top = 34.0;
    const double margin_bottom = 64.0;
    const double plot_width = std::max(1.0, width - margin_left - margin_right);
    const double plot_height = std::max(1.0, height - margin_top - margin_bottom);
    const double sx = plot_width / std::max(1.0e-9, z_max - z_min);
    const double sy = plot_height / std::max(1.0e-9, 2.0 * y_abs);
    const double scale = std::min(sx, sy);
    const auto cx = [&](double zz) { return margin_left + (zz - z_min) * scale; };
    const auto cy = [&](double yy) { return margin_top + plot_height * 0.5 - yy * scale; };

    std::ofstream out(path);
    if (!out) {
        return false;
    }

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " "
        << height << "\">\n";
    out << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    out << "<line x1=\"" << margin_left << "\" y1=\"" << cy(0.0) << "\" x2=\""
        << (width - margin_right) << "\" y2=\"" << cy(0.0)
        << "\" stroke=\"#9ca3af\" stroke-width=\"1\" stroke-dasharray=\"5 5\"/>\n";

    for (const auto& element : lens_elements) {
        const auto& first_surface = system.surfaces[element.first];
        const auto& last_surface = system.surfaces[element.last];
        const double first_clear = std::max(1.0e-6, clear_semis[element.first]);
        const double last_clear = std::max(1.0e-6, clear_semis[element.last]);
        const double semi = std::max({1.0e-6, element.semi, first_clear, last_clear});
        const auto point = [&](const Surface& surface, std::size_t si, double sag_y, double draw_y, double profile_radius) {
            const double sag = finite_surface_sag(surface, sag_y, profile_radius);
            return std::pair<double, double>{cx(vertex_z[si] + sag), cy(draw_y)};
        };

        std::ostringstream glass_path;
        const auto first_top_mech = point(first_surface, element.first, first_clear, semi, first_clear);
        const auto first_top_clear = point(first_surface, element.first, first_clear, first_clear, first_clear);
        const auto first_bottom_clear = point(first_surface, element.first, -first_clear, -first_clear, first_clear);
        const auto first_bottom_mech = point(first_surface, element.first, -first_clear, -semi, first_clear);
        const auto last_top_mech = point(last_surface, element.last, last_clear, semi, last_clear);
        const auto last_top_clear = point(last_surface, element.last, last_clear, last_clear, last_clear);
        const auto last_bottom_clear = point(last_surface, element.last, -last_clear, -last_clear, last_clear);
        const auto last_bottom_mech = point(last_surface, element.last, -last_clear, -semi, last_clear);

        glass_path << "M " << first_top_mech.first << " " << first_top_mech.second
                   << " L " << first_top_clear.first << " " << first_top_clear.second;
        append_surface_curve_range(
            glass_path,
            first_surface,
            vertex_z[element.first],
            first_clear,
            -first_clear,
            first_clear,
            cx,
            cy,
            false);
        glass_path << " L " << first_bottom_mech.first << " " << first_bottom_mech.second
                   << " L " << last_bottom_mech.first << " " << last_bottom_mech.second
                   << " L " << last_bottom_clear.first << " " << last_bottom_clear.second;
        append_surface_curve_range(
            glass_path,
            last_surface,
            vertex_z[element.last],
            -last_clear,
            last_clear,
            last_clear,
            cx,
            cy,
            false);
        glass_path << " L " << last_top_mech.first << " " << last_top_mech.second;
        out << "<path d=\"" << glass_path.str()
            << " Z\" fill=\"#dbeafe\" opacity=\"0.55\" stroke=\"#64748b\" stroke-width=\"1.0\"/>\n";

        out << "<line x1=\"" << first_top_mech.first << "\" y1=\"" << first_top_mech.second
            << "\" x2=\"" << last_top_mech.first << "\" y2=\"" << last_top_mech.second
            << "\" stroke=\"#334155\" stroke-width=\"1.0\"/>\n";
        out << "<line x1=\"" << first_bottom_mech.first << "\" y1=\"" << first_bottom_mech.second
            << "\" x2=\"" << last_bottom_mech.first << "\" y2=\"" << last_bottom_mech.second
            << "\" stroke=\"#334155\" stroke-width=\"1.0\"/>\n";
    }

    for (std::size_t si = 0; si < system.surfaces.size(); ++si) {
        const auto& surface = system.surfaces[si];
        if (surface.kind == SurfaceKind::Object) {
            continue;
        }
        double clear_semi = clear_semis[si];
        if (clear_semi <= 0.0) {
            clear_semi = mechanical_semis[si];
        }
        const double clear = std::max(1.0e-6, clear_semi);
        const double mechanical = std::max(clear, draw_mechanical_semis[si]);
        std::ostringstream path_data;
        append_surface_curve(path_data, surface, vertex_z[si], clear, false, clear, cx, cy, true);
        if (mechanical > clear * (1.0 + 1.0e-9)) {
            const double top_sag = finite_surface_sag(surface, clear, clear);
            const double bottom_sag = finite_surface_sag(surface, -clear, clear);
            path_data << " M " << cx(vertex_z[si] + top_sag) << " " << cy(clear)
                      << " L " << cx(vertex_z[si] + top_sag) << " " << cy(mechanical)
                      << " M " << cx(vertex_z[si] + bottom_sag) << " " << cy(-clear)
                      << " L " << cx(vertex_z[si] + bottom_sag) << " " << cy(-mechanical);
        }
        out << "<path d=\"" << path_data.str()
            << "\" fill=\"none\" stroke=\"" << (surface.is_stop ? "#111827" : "#334155")
            << "\" stroke-width=\"" << (surface.is_stop ? 2.5 : 1.5) << "\"/>\n";
    }

    std::size_t rays_per_field = pupils.size();
    for (std::size_t ri = 0; ri < ray_histories.size(); ++ri) {
        const std::size_t field_idx = rays_per_field > 0 ? ri / rays_per_field : 0;
        const auto& history = ray_histories[ri];
        if (history.size() < 2) {
            continue;
        }
        std::ostringstream path_data;
        bool started = false;
        for (std::size_t si = first_draw_surface; si < history.size(); ++si) {
            const auto& p = history[si];
            if (!p.valid || !std::isfinite(p.z) || !std::isfinite(p.y)) {
                continue;
            }
            path_data << (started ? " L " : "M ") << cx(p.z) << " " << cy(p.y);
            started = true;
        }
        if (started) {
            out << "<path d=\"" << path_data.str() << "\" fill=\"none\" stroke=\""
                << color_for_index(field_idx) << "\" stroke-width=\"0.85\" opacity=\"0.55\"/>\n";
        }
    }

    const double max_bar_world = 0.24 * plot_width / std::max(1.0e-9, scale);
    const double bar_world = nice_scale_length(max_bar_world);
    if (bar_world > 0.0) {
        const double bar_px = bar_world * scale;
        const double bar_x1 = width - margin_right;
        const double bar_x0 = bar_x1 - bar_px;
        const double bar_y = height - 34.0;
        out << "<line x1=\"" << bar_x0 << "\" y1=\"" << bar_y << "\" x2=\""
            << bar_x1 << "\" y2=\"" << bar_y
            << "\" stroke=\"#111827\" stroke-width=\"2\"/>\n";
        out << "<line x1=\"" << bar_x0 << "\" y1=\"" << (bar_y - 5.0) << "\" x2=\""
            << bar_x0 << "\" y2=\"" << (bar_y + 5.0)
            << "\" stroke=\"#111827\" stroke-width=\"1.5\"/>\n";
        out << "<line x1=\"" << bar_x1 << "\" y1=\"" << (bar_y - 5.0) << "\" x2=\""
            << bar_x1 << "\" y2=\"" << (bar_y + 5.0)
            << "\" stroke=\"#111827\" stroke-width=\"1.5\"/>\n";
        out << "<text x=\"" << (0.5 * (bar_x0 + bar_x1)) << "\" y=\"" << (bar_y + 20.0)
            << "\" text-anchor=\"middle\" font-family=\"Segoe UI, Arial\""
            << " font-size=\"12\" fill=\"#111827\">" << bar_world
            << " mm</text>\n";
    }

    out << "<text x=\"" << margin_left << "\" y=\"24\" font-family=\"Segoe UI, Arial\""
        << " font-size=\"13\" fill=\"#111827\">" << system.name << "</text>\n";
    out << "</svg>\n";
    return true;
}

bool save_spot_csv(
    const LensSystem& system,
    const std::string& path,
    int num_points,
    const std::string& distribution,
    unsigned int seed,
    int arms) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "field_hx,field_hy,wavelength_um,ray_index,pupil_weight,x_mm,y_mm,valid\n";
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    const auto fields = fields_or_default_vis(system);
    const auto waves = system.wavelengths.empty() ? std::vector<Wavelength>{Wavelength{}} : system.wavelengths;
    const auto pupils = make_pupil_distribution(num_points, distribution, seed, arms);
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto& field = fields[fi];
        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            const auto& wave = waves[wi];
            if (ray_aiming_uses_session(system.ray_aiming.mode)) {
                for (std::size_t i = 0; i < pupils.size(); ++i) {
                    Ray ray = aiming.generate(fi, wi, i, field, wave, pupils[i]);
                    tracer.trace_ray(ray, nullptr);
                    out << field.hx << "," << field.hy << "," << wave.value_um << ","
                        << i << "," << pupils[i].weight << "," << std::setprecision(12) << ray.p.x << ","
                        << ray.p.y << "," << (ray.valid ? 1 : 0) << "\n";
                }
            } else {
                const auto traced = tracer.trace_field(field, wave.value_um, pupils, false);
                for (std::size_t i = 0; i < traced.rays.size(); ++i) {
                    const auto& ray = traced.rays[i];
                    out << field.hx << "," << field.hy << "," << wave.value_um << ","
                        << i << "," << pupils[i].weight << "," << std::setprecision(12) << ray.p.x << ","
                        << ray.p.y << "," << (ray.valid ? 1 : 0) << "\n";
                }
            }
        }
    }
    return true;
}

bool save_raytrace_csv(
    const LensSystem& system,
    const std::string& path,
    int num_points,
    const std::string& distribution,
    unsigned int seed,
    RayAimingMode mode,
    int arms) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "field_index,hx,hy,wavelength_um,pupil_index,px,py,pupil_weight,"
        << "surface_index,z,x,y,l,m,n,intensity,valid,aim_mode,aim_converged,"
        << "aim_residual,aim_target_radius,aim_iterations,homotopy_fraction,"
        << "first_surface_x,first_surface_y,failure_code,failure_surface,failure_severity,"
        << "backward_propagation,backward_surface,backward_severity,aperture_blocked,aperture_blocked_surface,edge_prediction_scale\n";
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    const auto fields = fields_or_default_vis(system);
    const auto waves = system.wavelengths.empty() ? std::vector<Wavelength>{Wavelength{}} : system.wavelengths;
    const auto pupils = make_pupil_distribution(num_points, distribution, seed, arms);
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            const auto& wave = waves[wi];
            for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
                Ray ray = ray_aiming_uses_session(mode)
                              ? aiming.generate(fi, wi, pi, fields[fi], wave, pupils[pi])
                              : tracer.generate_ray_with_mode(fields[fi], pupils[pi], wave.value_um, mode);
                Vec3 first_surface_hit;
                bool has_first_surface_hit = tracer.first_surface_point(ray, &first_surface_hit);
                if (!has_first_surface_hit && ray.failure_code == RayFailureCode::AimNotConverged) {
                    Ray diagnostic_ray = ray;
                    diagnostic_ray.valid = true;
                    diagnostic_ray.intensity = std::max(diagnostic_ray.intensity, diagnostic_ray.nominal_weight);
                    has_first_surface_hit = tracer.first_surface_point(diagnostic_ray, &first_surface_hit);
                }
                std::vector<TracePoint> history;
                tracer.trace_ray(ray, &history);
                for (std::size_t si = 0; si < history.size(); ++si) {
                    const auto& h = history[si];
                    out << fi << "," << fields[fi].hx << "," << fields[fi].hy << ","
                        << wave.value_um << "," << pi << "," << pupils[pi].px << ","
                        << pupils[pi].py << "," << pupils[pi].weight << "," << si << "," << std::setprecision(14)
                        << h.z << "," << h.x << "," << h.y << "," << h.l << ","
                        << h.m << "," << h.n << "," << h.intensity << ","
                        << (h.valid ? 1 : 0) << "," << ray_aiming_mode_label(mode) << ","
                        << (ray.aim_converged ? 1 : 0) << "," << ray.aim_residual << ","
                        << ray.aim_target_radius << "," << ray.aim_iterations << ","
                        << ray.homotopy_fraction << ",";
                    if (has_first_surface_hit) {
                        out << first_surface_hit.x << "," << first_surface_hit.y;
                    } else {
                        out << "nan,nan";
                    }
                    out << "," << ray_failure_code_label(ray.failure_code) << ","
                        << ray.failure_surface << "," << ray.failure_severity << ","
                        << (ray.backward_propagation ? 1 : 0) << ","
                        << ray.backward_surface << "," << ray.backward_severity << ","
                        << (ray.aperture_blocked ? 1 : 0) << "," << ray.aperture_blocked_surface
                        << "," << ray.edge_prediction_scale << "\n";
                }
            }
        }
    }
    return true;
}

bool save_relative_illumination_csv(
    const RelativeIlluminationResult& result,
    const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "field_index,field_hx,field_hy,field_angle_deg,wavelength_um,"
           "relative_illumination,effective_f_number,projected_cosine_area,"
           "attempted_rays,valid_rays,integrated_samples,orientation_flip_samples,"
           "ray_density,remove_vignetting_factors,normalization_field_index\n";
    out << std::setprecision(14);
    for (std::size_t i = 0; i < result.points.size(); ++i) {
        const RelativeIlluminationPoint& point = result.points[i];
        out << i << "," << point.field_hx << "," << point.field_hy << ","
            << point.field_angle_deg << "," << point.wavelength_um << ","
            << point.relative_illumination << "," << point.effective_f_number << ","
            << point.projected_cosine_area << "," << point.attempted_rays << ","
            << point.valid_rays << "," << point.integrated_samples << ","
            << point.orientation_flip_samples << "," << result.config.ray_density << ","
            << (result.config.remove_vignetting_factors ? 1 : 0) << ","
            << result.normalization_field_index << "\n";
    }
    return true;
}

}  // namespace optiland
