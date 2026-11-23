/*
 * Generate physics-constrained candidate rows using curvature, material and paraxial-power estimates.
 * Initialization/refresh generation supplies the system, variable metadata and shared seeded RNG.
 * Preserve physical feasibility checks, projection order and retry draws before accepting a row.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double generation_surface_curvature(const Surface& surface) {
    return (std::isfinite(surface.radius) && std::abs(surface.radius) > kEps)
               ? 1.0 / surface.radius
               : 0.0;
}

double generation_material_after_index(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um) {
    if (surface_index >= system.surfaces.size()) {
        return 1.0;
    }
    const double n = system.surfaces[surface_index].material_after.refractive_index(wavelength_um);
    return std::isfinite(n) && n > 0.0 ? n : 1.0;
}

double generation_material_before_index(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um) {
    const std::size_t first = system.first_optical_surface();
    if (surface_index <= first || surface_index == 0) {
        return 1.0;
    }
    return generation_material_after_index(system, surface_index - 1, wavelength_um);
}

double generation_paraxial_efl_estimate(const LensSystem& system, double wavelength_um) {
    double y = 1.0;
    double u = 0.0;
    double z_current = -1.0;
    double vertex_z = 0.0;
    for (std::size_t i = system.first_optical_surface(); i < system.surfaces.size(); ++i) {
        const Surface& surface = system.surfaces[i];
        y += u * (vertex_z - z_current);
        z_current = vertex_z;
        if (surface.kind != SurfaceKind::Object && surface.kind != SurfaceKind::Image) {
            const double n1 = generation_material_before_index(system, i, wavelength_um);
            const double n2 = generation_material_after_index(system, i, wavelength_um);
            if (std::abs(n2) > kEps) {
                const double power = (n2 - n1) * generation_surface_curvature(surface);
                u = (n1 * u - y * power) / n2;
            }
        }
        if (std::isfinite(surface.thickness)) {
            vertex_z += surface.thickness;
        }
    }
    if (!std::isfinite(u) || std::abs(u) <= kEps) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return -1.0 / u;
}

std::vector<double> make_physics_constrained_generation_row(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    const PopulationGenerationOptions& options,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni) {
    const int dim = static_cast<int>(variables.size());
    std::vector<double> values(static_cast<std::size_t>(dim), 0.0);
    const double wavelength_um = system.primary_wavelength();

    for (int j = 0; j < dim; ++j) {
        const VariableSpec& var = variables[static_cast<std::size_t>(j)];
        const double lo = std::min(var.lower, var.upper);
        const double hi = std::max(var.lower, var.upper);
        double value = std::clamp(var.initial, lo, hi);
        if (var.kind == VariableKind::GlassChoice) {
            const int lo_choice = glass_choice_lower(var);
            const int hi_choice = std::max(lo_choice, glass_choice_upper(var));
            std::uniform_int_distribution<int> choice_dist(lo_choice, hi_choice);
            value = static_cast<double>(choice_dist(rng));
        } else if (is_material_variable(var)) {
            value = options.perturb_material_variables && hi > lo
                        ? lo + uni(rng) * (hi - lo)
                        : value;
        } else if (var.kind != VariableKind::Curvature &&
                   var.kind != VariableKind::Thickness &&
                   options.perturb_other_variables && hi > lo) {
            value = lo + uni(rng) * (hi - lo);
        }
        values[static_cast<std::size_t>(j)] = std::clamp(value, lo, hi);
    }

    std::vector<int> thickness_columns;
    thickness_columns.reserve(variables.size());
    std::vector<int> flexible_thickness_columns;
    double fixed_track = 0.0;
    for (std::size_t i = system.first_optical_surface(); i + 1 < system.surfaces.size(); ++i) {
        bool variable_thickness = false;
        for (int j = 0; j < dim; ++j) {
            const VariableSpec& var = variables[static_cast<std::size_t>(j)];
            if (var.kind == VariableKind::Thickness && var.index == i) {
                variable_thickness = true;
                thickness_columns.push_back(j);
                break;
            }
        }
        if (!variable_thickness && std::isfinite(system.surfaces[i].thickness)) {
            fixed_track += std::max(0.0, system.surfaces[i].thickness);
        }
    }
    double min_variable_track = 0.0;
    for (int j : thickness_columns) {
        const VariableSpec& var = variables[static_cast<std::size_t>(j)];
        const double lo = std::min(var.lower, var.upper);
        const double hi = std::max(var.lower, var.upper);
        double value = lo;
        const bool is_bfl =
            options.physics_target_bfl > 0.0 &&
            var.index + 2 >= system.surfaces.size();
        if (is_bfl) {
            const double target = options.physics_target_bfl *
                                  (0.90 + 0.30 * uni(rng));
            value = std::clamp(target, lo, hi);
        } else if (thickness_variable_is_glass_space(system, var)) {
            const double upper = std::min(hi, std::max(lo, lo + std::max(0.0, options.glass_thickness_absolute_span)));
            value = upper > lo ? lo + uni(rng) * (upper - lo) : lo;
        } else {
            flexible_thickness_columns.push_back(j);
        }
        values[static_cast<std::size_t>(j)] = value;
        min_variable_track += value;
    }
    double target_ttl = options.physics_target_ttl;
    if (!(target_ttl > 0.0) && std::isfinite(system.total_track())) {
        target_ttl = system.total_track();
    }
    if (target_ttl > 0.0 && !flexible_thickness_columns.empty()) {
        const double f0 = std::clamp(options.physics_ttl_min_fraction, 0.0, 2.0);
        const double f1 = std::clamp(options.physics_ttl_max_fraction, f0, 2.0);
        const double target_total = target_ttl * (f0 + uni(rng) * (f1 - f0));
        double extra = std::max(0.0, target_total - fixed_track - min_variable_track);
        std::vector<double> weights;
        weights.reserve(flexible_thickness_columns.size());
        for (std::size_t i = 0; i < flexible_thickness_columns.size(); ++i) {
            weights.push_back(0.05 + uni(rng));
        }
        std::vector<int> active = flexible_thickness_columns;
        for (int pass = 0; pass < 6 && extra > 1.0e-9 && !active.empty(); ++pass) {
            double weight_sum = 0.0;
            for (int j : active) {
                const auto it = std::find(
                    flexible_thickness_columns.begin(),
                    flexible_thickness_columns.end(),
                    j);
                const std::size_t wi = static_cast<std::size_t>(
                    std::distance(flexible_thickness_columns.begin(), it));
                weight_sum += weights[wi];
            }
            if (weight_sum <= 0.0) {
                break;
            }
            std::vector<int> next_active;
            for (int j : active) {
                const auto it = std::find(
                    flexible_thickness_columns.begin(),
                    flexible_thickness_columns.end(),
                    j);
                const std::size_t wi = static_cast<std::size_t>(
                    std::distance(flexible_thickness_columns.begin(), it));
                const VariableSpec& var = variables[static_cast<std::size_t>(j)];
                const double hi = std::max(var.lower, var.upper);
                const double room = std::max(0.0, hi - values[static_cast<std::size_t>(j)]);
                const double add = std::min(room, extra * weights[wi] / weight_sum);
                values[static_cast<std::size_t>(j)] += add;
                extra -= add;
                if (room > add + 1.0e-9) {
                    next_active.push_back(j);
                }
            }
            active = std::move(next_active);
        }
    } else {
        for (int j : flexible_thickness_columns) {
            const VariableSpec& var = variables[static_cast<std::size_t>(j)];
            const double lo = std::min(var.lower, var.upper);
            const double hi = std::max(var.lower, var.upper);
            values[static_cast<std::size_t>(j)] =
                hi > lo ? lo + uni(rng) * (hi - lo) : lo;
        }
    }

    std::vector<int> curvature_columns;
    curvature_columns.reserve(variables.size());
    std::vector<double> dn;
    dn.reserve(variables.size());
    for (int j = 0; j < dim; ++j) {
        const VariableSpec& var = variables[static_cast<std::size_t>(j)];
        if (var.kind != VariableKind::Curvature) {
            continue;
        }
        curvature_columns.push_back(j);
        const double n1 = generation_material_before_index(system, var.index, wavelength_um);
        const double n2 = generation_material_after_index(system, var.index, wavelength_um);
        dn.push_back(n2 - n1);
    }
    double target_efl = options.physics_target_efl;
    if (!(target_efl > 0.0)) {
        target_efl = generation_paraxial_efl_estimate(system, wavelength_um);
    }
    const double target_power =
        std::isfinite(target_efl) && std::abs(target_efl) > 1.0e-12
            ? 1.0 / target_efl
            : 0.0;
    if (!curvature_columns.empty() && std::abs(target_power) > 1.0e-12) {
        bool accepted = false;
        for (int attempt = 0; attempt < 200 && !accepted; ++attempt) {
            double raw_power = 0.0;
            std::vector<double> raw(curvature_columns.size(), 0.0);
            for (std::size_t k = 0; k < curvature_columns.size(); ++k) {
                const int j = curvature_columns[k];
                const VariableSpec& var = variables[static_cast<std::size_t>(j)];
                const double lo = std::min(var.lower, var.upper);
                const double hi = std::max(var.lower, var.upper);
                const double span = std::max(1.0e-9, hi - lo);
                double sign = target_power >= 0.0 ? 1.0 : -1.0;
                if (dn[k] < 0.0) {
                    sign *= -1.0;
                }
                if (uni(rng) < 0.20) {
                    sign *= -1.0;
                }
                const double magnitude = 0.15 * span + 0.70 * span * uni(rng);
                raw[k] = sign * magnitude;
                raw_power += dn[k] * raw[k];
            }
            if (std::abs(raw_power) <= 1.0e-12 ||
                target_power * raw_power <= 0.0) {
                continue;
            }
            const double jitter = 1.0 +
                                  std::clamp(options.physics_power_jitter, 0.0, 2.0) *
                                      (2.0 * uni(rng) - 1.0);
            const double scale = target_power * jitter / raw_power;
            accepted = true;
            for (std::size_t k = 0; k < curvature_columns.size(); ++k) {
                const int j = curvature_columns[k];
                const VariableSpec& var = variables[static_cast<std::size_t>(j)];
                const double lo = std::min(var.lower, var.upper);
                const double hi = std::max(var.lower, var.upper);
                const double value = raw[k] * scale;
                if (value < lo || value > hi) {
                    accepted = false;
                    break;
                }
                values[static_cast<std::size_t>(j)] = value;
            }
        }
        if (!accepted) {
            double dn_sum = 0.0;
            for (double item : dn) {
                dn_sum += std::abs(item);
            }
            const double shared = std::abs(target_power) /
                                  std::max(1.0e-12, dn_sum);
            for (std::size_t k = 0; k < curvature_columns.size(); ++k) {
                const int j = curvature_columns[k];
                const VariableSpec& var = variables[static_cast<std::size_t>(j)];
                double sign = target_power >= 0.0 ? 1.0 : -1.0;
                if (dn[k] < 0.0) {
                    sign *= -1.0;
                }
                values[static_cast<std::size_t>(j)] =
                    std::clamp(sign * shared, std::min(var.lower, var.upper), std::max(var.lower, var.upper));
            }
        }

        auto curvature_merit = [&]() {
            double power = 0.0;
            double bend = 0.0;
            for (std::size_t k = 0; k < curvature_columns.size(); ++k) {
                const double c = values[static_cast<std::size_t>(curvature_columns[k])];
                power += dn[k] * c;
                bend += c * c;
            }
            const double power_scale = std::max(1.0e-9, std::abs(target_power));
            return std::abs(power - target_power) / power_scale +
                   0.01 * std::sqrt(bend / std::max<std::size_t>(1, curvature_columns.size()));
        };
        double best_merit = curvature_merit();
        std::vector<double> best_values = values;
        const int sa_iterations = std::max(0, options.physics_sa_iterations);
        double temperature = std::max(1.0e-9, options.physics_sa_temperature);
        for (int iter = 0; iter < sa_iterations; ++iter) {
            const std::size_t k = static_cast<std::size_t>(
                std::uniform_int_distribution<int>(
                    0,
                    static_cast<int>(curvature_columns.size()) - 1)(rng));
            const int j = curvature_columns[k];
            const VariableSpec& var = variables[static_cast<std::size_t>(j)];
            const double lo = std::min(var.lower, var.upper);
            const double hi = std::max(var.lower, var.upper);
            const double span = std::max(1.0e-12, hi - lo);
            const double previous = values[static_cast<std::size_t>(j)];
            values[static_cast<std::size_t>(j)] =
                std::clamp(previous + (2.0 * uni(rng) - 1.0) * span * temperature, lo, hi);
            const double merit = curvature_merit();
            const bool accept = merit < best_merit ||
                                uni(rng) < std::exp((best_merit - merit) /
                                                    std::max(1.0e-12, temperature));
            if (accept) {
                if (merit < best_merit) {
                    best_merit = merit;
                    best_values = values;
                }
            } else {
                values[static_cast<std::size_t>(j)] = previous;
            }
            temperature *= 0.95;
        }
        values = std::move(best_values);
    } else {
        for (int j : curvature_columns) {
            const VariableSpec& var = variables[static_cast<std::size_t>(j)];
            const double lo = std::min(var.lower, var.upper);
            const double hi = std::max(var.lower, var.upper);
            values[static_cast<std::size_t>(j)] = hi > lo ? lo + uni(rng) * (hi - lo) : lo;
        }
    }

    std::vector<double> scaled(static_cast<std::size_t>(dim), 0.0);
    for (int j = 0; j < dim; ++j) {
        const VariableSpec& var = variables[static_cast<std::size_t>(j)];
        const auto [lo, hi] = internal_bounds(var, mode);
        scaled[static_cast<std::size_t>(j)] =
            std::clamp(physical_to_internal(var, values[static_cast<std::size_t>(j)], mode), lo, hi);
    }
    return scaled;
}

}  // namespace optimizer_detail

}  // namespace optiland
