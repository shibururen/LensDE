/*
 * Sample restricted physical coordinates and asphere-sag geometry for population generation.
 * Generation strategies share these range rules with caller-owned variable metadata and RNG/distributions.
 * Preserve per-coordinate range construction, clipping and random-draw order.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double bounded_positive(double value, double fallback) {
    return (std::isfinite(value) && value > 0.0) ? value : fallback;
}

double restricted_generation_span(
    const LensSystem& system,
    const VariableSpec& var,
    double center_physical,
    const PopulationGenerationOptions& options) {
    if (var.kind == VariableKind::Curvature) {
        const double rel = bounded_positive(options.curvature_relative_span, 0.0);
        const double abs_span = bounded_positive(options.curvature_absolute_span, 0.0);
        return std::max(abs_span, rel * std::max(1.0e-12, std::abs(center_physical)));
    }
    if (var.kind == VariableKind::Thickness) {
        const bool glass_space = thickness_variable_is_glass_space(system, var);
        const double rel = bounded_positive(
            glass_space ? options.glass_thickness_relative_span
                        : options.air_thickness_relative_span,
            0.0);
        const double abs_span = bounded_positive(
            glass_space ? options.glass_thickness_absolute_span
                        : options.air_thickness_absolute_span,
            0.0);
        return std::max(abs_span, rel * std::max(1.0e-12, std::abs(center_physical)));
    }
    if (var.kind == VariableKind::Conic) {
        const double bounded_span = 0.05 * std::max(0.0, var.upper - var.lower);
        return std::max(bounded_span, 0.10 * std::max(1.0, std::abs(center_physical)));
    }
    if (var.kind == VariableKind::AsphereCoeff) {
        const double bounded_span = 0.15 * std::max(0.0, var.upper - var.lower);
        return std::max(bounded_span, 0.10 * std::max(1.0e-12, std::abs(center_physical)));
    }
    return 0.0;
}

double sample_restricted_physical_value(
    const LensSystem& system,
    const VariableSpec& var,
    double center_physical,
    const PopulationGenerationOptions& options,
    double span_scale,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni) {
    const auto [lo, hi] = internal_bounds(var, mode);
    const double safe_center = maybe_clip_continuous(
        var,
        center_physical,
        var.lower,
        var.upper);
    const double scale = std::clamp(span_scale, 0.0, 1.0);
    double value = physical_to_internal(var, safe_center, mode);
    const double span = restricted_generation_span(system, var, safe_center, options) * scale;
    if (span > 0.0) {
        const double phys_lo = safe_center - span;
        const double phys_hi = safe_center + span;
        const double sampled =
            phys_lo <= phys_hi ? phys_lo + uni(rng) * (phys_hi - phys_lo) : safe_center;
        value = physical_to_internal(var, sampled, mode);
    } else if (is_material_variable(var)) {
        value = options.perturb_material_variables && scale > 0.0
                    ? lo + uni(rng) * (hi - lo)
                    : physical_to_internal(var, safe_center, mode);
    } else if (is_glass_choice_variable(var)) {
        if (scale > 0.0 && options.perturb_material_variables) {
            std::uniform_int_distribution<int> choice_dist(
                glass_choice_lower(var),
                std::max(glass_choice_lower(var), glass_choice_upper(var)));
            value = physical_to_internal(var, static_cast<double>(choice_dist(rng)), mode);
        } else {
            value = physical_to_internal(var, safe_center, mode);
        }
    } else if (options.perturb_other_variables && scale > 0.0) {
        value = lo + uni(rng) * (hi - lo);
    }
    return maybe_clip_continuous(var, value, lo, hi);
}

std::vector<double> make_sag_geometry_generation_row(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& parent_scaled,
    InternalScalingMode mode,
    double sag_perturbation_um,
    double geometry_fraction,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni) {
    const int dim = static_cast<int>(variables.size());
    std::vector<double> physical = scaled_to_physical(variables, parent_scaled, mode);
    if (static_cast<int>(physical.size()) != dim) {
        physical.assign(static_cast<std::size_t>(dim), 0.0);
    }
    const double geometry_scale = std::max(0.0, geometry_fraction);
    for (int j = 0; j < dim; ++j) {
        const VariableSpec& var = variables[static_cast<std::size_t>(j)];
        if (var.kind != VariableKind::Radius &&
            var.kind != VariableKind::Curvature &&
            var.kind != VariableKind::Thickness &&
            var.kind != VariableKind::Conic) {
            continue;
        }
        const double span = var.upper - var.lower;
        if (!std::isfinite(span) || span <= 0.0 || geometry_scale <= 0.0) {
            continue;
        }
        physical[static_cast<std::size_t>(j)] +=
            (2.0 * uni(rng) - 1.0) * geometry_scale * span;
    }

    const double sag_cap_mm = std::max(0.0, sag_perturbation_um) * 1.0e-3;
    if (sag_cap_mm > 0.0) {
        std::unordered_map<std::size_t, std::vector<int>> groups;
        std::vector<std::size_t> group_order;
        for (int j = 0; j < dim; ++j) {
            const VariableSpec& var = variables[static_cast<std::size_t>(j)];
            if (var.kind != VariableKind::AsphereCoeff) {
                continue;
            }
            auto [it, inserted] = groups.emplace(var.index, std::vector<int>{});
            if (inserted) {
                group_order.push_back(var.index);
            }
            it->second.push_back(j);
        }
        for (std::size_t surface : group_order) {
            const auto it = groups.find(surface);
            if (it == groups.end() || it->second.empty()) {
                continue;
            }
            const std::vector<int>& columns = it->second;
            const double target_sag = (2.0 * uni(rng) - 1.0) * sag_cap_mm;
            const double edge_share = target_sag / static_cast<double>(columns.size());
            std::vector<double> deltas(columns.size(), 0.0);
            double bound_scale = 1.0;
            for (std::size_t k = 0; k < columns.size(); ++k) {
                const int column = columns[k];
                const VariableSpec& var = variables[static_cast<std::size_t>(column)];
                const double power = frozen_asphere_radial_power(var);
                if (!std::isfinite(power) || power <= 0.0) {
                    continue;
                }
                const double delta = edge_share / power;
                deltas[k] = delta;
                const double current = physical[static_cast<std::size_t>(column)];
                if (delta > 0.0 && std::isfinite(var.upper)) {
                    bound_scale = std::min(bound_scale, (var.upper - current) / delta);
                } else if (delta < 0.0 && std::isfinite(var.lower)) {
                    bound_scale = std::min(bound_scale, (current - var.lower) / (-delta));
                }
            }
            bound_scale = std::clamp(0.98 * bound_scale, 0.0, 1.0);
            for (std::size_t k = 0; k < columns.size(); ++k) {
                physical[static_cast<std::size_t>(columns[k])] +=
                    bound_scale * deltas[k];
            }
        }
    }
    return physical_to_scaled(variables, physical, mode);
}

}  // namespace optimizer_detail

}  // namespace optiland
