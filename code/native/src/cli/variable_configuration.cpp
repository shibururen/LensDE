// Translate physical system values into variable vectors and prepare local/asphere bound policies.
// Command setup, polish preparation, and publication share these reference-radius conventions.
// Preserve edge-sag rebasing, unit scaling, variable order, and the materialization acceptance contract.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

int clear_aperture_update_rings(const MeritConfig& merit) {
    if (merit.vignetting_merit_aperture_envelope_experimental) {
        return std::max(1, merit.num_rays);
    }
    return merit.clear_aperture_rings > 0 ? merit.clear_aperture_rings : merit.num_rays;
}

bool score_is_materialization_valid(
    const Score& score,
    const MeritConfig& merit,
    const MaterializationTraceContract& trace_contract ) {
    constexpr double tolerance = 1.0e-12;
    const double coverage_epsilon =
        trace_contract.allow_near_trace_infeasible
            ? std::max(0.0, trace_contract.trace_coverage_epsilon)
            : 0.0;
    const bool coverage_ok =
        std::isfinite(score.trace_coverage) &&
        score.trace_coverage + coverage_epsilon + tolerance >=
            merit.min_system_coverage &&
        std::isfinite(score.min_field_coverage) &&
        score.min_field_coverage + coverage_epsilon + tolerance >=
            merit.min_field_coverage;
    const bool chief_ok =
        !merit.require_chief_ray ||
        (score.chief_ray_ok && score.chief_ray_failed_fields == 0);
    const bool off_axis_ok =
        !merit.require_off_axis_field_coverage ||
        score.zero_coverage_off_axis_fields == 0;
    const bool trace_contract_ok =
        (score.trace_ok && coverage_ok && chief_ok && off_axis_ok) ||
        (trace_contract.allow_near_trace_infeasible && coverage_ok && chief_ok && off_axis_ok);
    return trace_contract_ok &&
           std::isfinite(score.merit) &&
           score.merit + tolerance < merit.fail_merit &&
           // Edge thickness is a soft manufacturing term in the optimizer's
           // merit.  Checkpoints and final publication must keep that same
           // contract: a finite negative edge is penalized, not rejected.
           std::isfinite(score.min_edge);
}

void sync_stop_aperture_to_system(LensSystem& lens) {
    sync_stop_aperture_to_aiming_radius(lens);
}

double raw_current_variable_value_from_system(const LensSystem& lens, const VariableSpec& variable) {
    switch (variable.kind) {
        case VariableKind::Radius:
            if (variable.index < lens.surfaces.size()) {
                const double radius = lens.surfaces[variable.index].radius;
                if (std::isfinite(radius)) {
                    return radius;
                }
            }
            return variable.initial;
        case VariableKind::Curvature:
            if (variable.index < lens.surfaces.size()) {
                const double radius = lens.surfaces[variable.index].radius;
                if (std::isfinite(radius) && std::abs(radius) > 1.0e-14) {
                    return 1.0 / radius;
                }
                return 0.0;
            }
            return variable.initial;
        case VariableKind::Thickness:
            if (variable.index < lens.surfaces.size()) {
                return lens.surfaces[variable.index].thickness;
            }
            return variable.initial;
        case VariableKind::Conic:
            if (variable.index < lens.surfaces.size()) {
                return lens.surfaces[variable.index].conic;
            }
            return variable.initial;
        case VariableKind::AsphereCoeff:
            if (variable.index < lens.surfaces.size()) {
                const auto& coeffs = lens.surfaces[variable.index].asphere_coefficients;
                if (variable.term < coeffs.size()) {
                    return coeffs[variable.term];
                }
                return 0.0;
            }
            return variable.initial;
        case VariableKind::MaterialNd:
            if (variable.index < lens.surfaces.size()) {
                return lens.surfaces[variable.index].material_after.nd;
            }
            return variable.initial;
        case VariableKind::MaterialVd:
            if (variable.index < lens.surfaces.size()) {
                return lens.surfaces[variable.index].material_after.vd;
            }
            return variable.initial;
        case VariableKind::VignetteX:
            if (variable.index < lens.fields.size()) {
                return lens.fields[variable.index].fvdx;
            }
            return variable.initial;
        case VariableKind::VignetteY:
            if (variable.index < lens.fields.size()) {
                return lens.fields[variable.index].fvdy;
            }
            return variable.initial;
        case VariableKind::VignetteCenterY:
            if (variable.index < lens.fields.size()) {
                return lens.fields[variable.index].fvcy;
            }
            return variable.initial;
        case VariableKind::VignetteMagnitudeY:
            if (variable.index < lens.fields.size()) {
                return std::max(0.0, lens.fields[variable.index].fvdy);
            }
            return variable.initial;
        case VariableKind::VignetteAsymmetryY:
            if (variable.index < lens.fields.size()) {
                const Field& field = lens.fields[variable.index];
                return field.fvdy > kEps
                           ? std::clamp(field.fvcy / field.fvdy, -1.0, 1.0)
                           : 0.0;
            }
            return variable.initial;
        case VariableKind::GlassChoice:
            return variable.initial;
    }
    return variable.initial;
}

double current_variable_value_from_system(const LensSystem& lens, const VariableSpec& variable) {
    double value = raw_current_variable_value_from_system(lens, variable);
    if (!std::isfinite(value)) {
        value = variable.initial;
    }
    if (variable.kind == VariableKind::GlassChoice) {
        return std::clamp(value, variable.lower, variable.upper);
    }
    return value;
}

double asphere_stage_reference_radius(const LensSystem& lens, const VariableSpec& variable) {
    double radius = 0.0;
    if (variable.index < lens.surfaces.size()) {
        const auto& surface = lens.surfaces[variable.index];
        // The trace-updated clear semi-aperture defines the physical edge for
        // q=A*r_ref^order. Mechanical apertures may retain a much larger
        // flat-start envelope and are therefore fallback-only.
        for (const double candidate : {
                 surface.semi_aperture,
                 surface.aperture_radius,
                 surface.mechanical_aperture}) {
            if (std::isfinite(candidate) && std::abs(candidate) > 0.0) {
                radius = std::abs(candidate);
                break;
            }
        }
    }
    if (!std::isfinite(radius) || radius <= 0.0) {
        radius = std::abs(lens.entrance_pupil_radius());
    }
    if (!std::isfinite(radius) || radius <= 0.0) {
        radius = 1.0;
    }
    return std::max(radius, 1.0e-12);
}

double asphere_radial_power(double radius, std::size_t term) {
    const int order = 2 * static_cast<int>(term + 1);
    double power = 1.0;
    for (int i = 0; i < order; ++i) {
        if (power > std::numeric_limits<double>::max() / radius) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        power *= radius;
    }
    return power;
}

int rebase_asphere_edge_sag_variables(
    const LensSystem& lens,
    std::vector<VariableSpec>& variables) {
    int rebased = 0;
    for (auto& variable : variables) {
        if (variable.kind != VariableKind::AsphereCoeff) {
            continue;
        }
        const double new_radius = asphere_stage_reference_radius(lens, variable);
        const double new_power = asphere_radial_power(new_radius, variable.term);
        if (!std::isfinite(new_power) || new_power <= 0.0) {
            continue;
        }
        double sag_lower = variable.edge_sag_lower;
        double sag_upper = variable.edge_sag_upper;
        if (!std::isfinite(sag_lower) || !std::isfinite(sag_upper) || sag_lower >= sag_upper) {
            const double old_radius =
                std::isfinite(variable.asphere_reference_radius) && variable.asphere_reference_radius > 0.0
                    ? variable.asphere_reference_radius
                    : new_radius;
            const double old_power = asphere_radial_power(old_radius, variable.term);
            if (!std::isfinite(old_power) || old_power <= 0.0) {
                continue;
            }
            sag_lower = variable.lower * old_power;
            sag_upper = variable.upper * old_power;
        }
        const double current = raw_current_variable_value_from_system(lens, variable);
        const double current_sag = std::isfinite(current) ? current * new_power : 0.0;
        const double sag_bound = std::max({
            std::abs(sag_lower),
            std::abs(sag_upper),
            1.2 * std::abs(current_sag),
            1.0e-15});
        variable.asphere_reference_radius = new_radius;
        variable.edge_sag_lower = -sag_bound;
        variable.edge_sag_upper = sag_bound;
        variable.lower = -sag_bound / new_power;
        variable.upper = sag_bound / new_power;
        variable.initial = std::clamp(current, variable.lower, variable.upper);
        ++rebased;
    }
    return rebased;
}

std::vector<double> current_variable_values_from_system(
    const LensSystem& lens,
    const std::vector<VariableSpec>& variables) {
    std::vector<double> values;
    values.reserve(variables.size());
    for (const auto& variable : variables) {
        values.push_back(current_variable_value_from_system(lens, variable));
    }
    return values;
}

void apply_local_auto_tune(PolishOptions& options, int argc, char** argv) {
    options.auto_tune =
        has_arg(argc, argv, "--local-auto-tune") ||
        has_arg(argc, argv, "--local-auto");
    if (!options.auto_tune) {
        return;
    }

    // These are dimensionless once column equilibration and bound/edge-sag
    // coordinates are enabled.  They are starting values only: the DLS loop
    // adapts lambda and the trust radius from the model agreement ratio.
    options.initial_lambda = 0.1;
    options.finite_difference = 1.0e-5;
    options.min_step = 1.0e-10;
    options.tolerance = 1.0e-7;
    options.max_rejected_steps = std::max(options.max_rejected_steps, 40);
    options.step_search_levels = 6;
    options.step_search_shrink = 0.5;
    options.column_equilibration = true;
    options.qr_linear_solver = true;
    options.trf_initial_radius = 0.25;

    // Auto mode never accepts a broken ray trace unless the caller explicitly
    // requests the existing infeasible-trace escape hatch.
    if (!has_arg(argc, argv, "--local-allow-trace-infeasible")) {
        options.trace_acceptance_enabled = true;
        options.require_trace_feasible = true;
    }
}

bool prepare_asphere_reference_variables(
    const LensSystem& lens,
    std::vector<VariableSpec>& variables,
    InternalScalingMode internal_scaling,
    AsphereReferencePolicy policy,
    std::string* error) {
    const std::size_t asphere_count = static_cast<std::size_t>(std::count_if(
        variables.begin(),
        variables.end(),
        [](const VariableSpec& variable) {
            return variable.kind == VariableKind::AsphereCoeff;
        }));

    if (internal_scaling != InternalScalingMode::EdgeSagNormalized) {
        std::cout << "Asphere reference policy: " << asphere_reference_policy_name(policy)
                  << " (inactive because optimizer internal scaling is "
                  << internal_scaling_name(internal_scaling) << ")\n";
        return true;
    }
    if (asphere_count == 0) {
        std::cout << "Asphere reference policy: " << asphere_reference_policy_name(policy)
                  << " (no asphere coefficient variables)\n";
        return true;
    }

    if (policy == AsphereReferencePolicy::Input) {
        for (std::size_t variable_index = 0; variable_index < variables.size(); ++variable_index) {
            const VariableSpec& variable = variables[variable_index];
            if (variable.kind != VariableKind::AsphereCoeff) {
                continue;
            }
            const int order = 2 * static_cast<int>(variable.term + 1);
            const std::string label =
                "asphere variable " + std::to_string(variable_index) +
                " (surface " + std::to_string(variable.index) +
                ", A" + std::to_string(order) + ")";
            if (!std::isfinite(variable.asphere_reference_radius) ||
                variable.asphere_reference_radius <= 0.0) {
                if (error != nullptr) {
                    *error = label +
                        " requires an explicit finite positive asphere_reference_radius";
                }
                return false;
            }
            if (!std::isfinite(variable.edge_sag_lower) ||
                !std::isfinite(variable.edge_sag_upper) ||
                variable.edge_sag_upper <= variable.edge_sag_lower) {
                if (error != nullptr) {
                    *error = label +
                        " requires explicit finite edge_sag_lower/edge_sag_upper q bounds "
                        "with lower < upper";
                }
                return false;
            }
            const double radial_power =
                asphere_radial_power(variable.asphere_reference_radius, variable.term);
            if (!std::isfinite(radial_power) || radial_power <= 0.0) {
                if (error != nullptr) {
                    *error = label +
                        " has an asphere_reference_radius whose radial power is not finite";
                }
                return false;
            }
        }
        std::cout << "Asphere reference policy: input (preserving explicit reference radius/q "
                  << "bounds for " << asphere_count << " coefficient variable(s))\n";
        return true;
    }

    const int rebased = rebase_asphere_edge_sag_variables(lens, variables);
    std::cout << "Asphere reference policy: current (rebased " << rebased << " of "
              << asphere_count << " coefficient variable(s) from current-system apertures)\n";
    return true;
}

double finite_or(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

void widen_bounds(VariableSpec& variable, double lower, double upper) {
    if (!std::isfinite(lower) || !std::isfinite(upper)) {
        return;
    }
    if (lower > upper) {
        std::swap(lower, upper);
    }
    variable.lower = std::min(variable.lower, lower);
    variable.upper = std::max(variable.upper, upper);
    variable.initial = std::clamp(variable.initial, variable.lower, variable.upper);
}

std::vector<VariableSpec> local_bounds_variables(
    const std::vector<VariableSpec>& variables,
    LocalBoundsMode mode) {
    if (mode == LocalBoundsMode::Variable) {
        return variables;
    }
    std::vector<VariableSpec> relaxed = variables;
    for (auto& variable : relaxed) {
        if (!std::isfinite(variable.lower) ||
            !std::isfinite(variable.upper) ||
            variable.upper <= variable.lower) {
            continue;
        }
        if (mode == LocalBoundsMode::SafetyMaterialStrict &&
            (variable.kind == VariableKind::MaterialNd ||
             variable.kind == VariableKind::MaterialVd)) {
            variable.initial = std::clamp(variable.initial, variable.lower, variable.upper);
            continue;
        }
        switch (variable.kind) {
            case VariableKind::Radius:
                widen_bounds(variable, -1.0e6, 1.0e6);
                break;
            case VariableKind::Curvature:
                widen_bounds(variable, -1.0, 1.0);
                break;
            case VariableKind::Thickness: {
                const double upper = std::max(
                    1000.0,
                    std::max(
                        variable.upper,
                        10.0 * std::abs(finite_or(variable.initial, 0.0)) + 10.0));
                variable.lower = std::min(variable.lower, 1.0e-4);
                variable.upper = std::max(variable.upper, upper);
                variable.initial = std::clamp(variable.initial, variable.lower, variable.upper);
                break;
            }
            case VariableKind::MaterialNd:
                widen_bounds(variable, 1.0, 2.5);
                break;
            case VariableKind::MaterialVd:
                widen_bounds(variable, 1.0, 150.0);
                break;
            case VariableKind::Conic:
                widen_bounds(variable, -100.0, 100.0);
                break;
            case VariableKind::GlassChoice:
            case VariableKind::VignetteX:
            case VariableKind::VignetteY:
            case VariableKind::VignetteCenterY:
            case VariableKind::VignetteMagnitudeY:
            case VariableKind::VignetteAsymmetryY:
            case VariableKind::AsphereCoeff:
                break;
        }
    }
    return relaxed;
}

} } }
