/*
 * Implements material dispersion, variable projection/application and pupil distributions
 * shared by the native ray tracer, analysis routines and optimization setup.
 * Preserve physical variable units, infinite-radius conventions, feasible vignetting
 * parameterization and seed-dependent pupil ordering when extending these primitives.
 */

#include "optiland/core.hpp"
#include "optiland/fixed_budget_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <random>
#include <stdexcept>

namespace optiland {

namespace {

double radius_from_curvature(double curvature) {
    constexpr double kCurvatureEps = 1.0e-14;
    if (!std::isfinite(curvature) || std::abs(curvature) < kCurvatureEps) {
        return kInf;
    }
    return 1.0 / curvature;
}

void force_model_abbe(Material& material, double nd, double vd) {
    material.name = "model-abbe";
    material.reference.clear();
    material.source_path.clear();
    material.model = Material::Model::Abbe;
    material.nd = nd;
    material.vd = vd;
    material.formula_type = 0;
    material.coefficients.clear();
    material.table_wavelengths.clear();
    material.table_n.clear();
}

}  // namespace

double project_variable_value(const VariableSpec& variable, double value) {
    if (!std::isfinite(value)) {
        return value;
    }
    switch (variable.kind) {
        case VariableKind::Thickness:
            // Negative propagation intervals are outside the supported
            // sequential optical model. Positive thickness has no hard cap.
            return std::max(0.0, value);
        case VariableKind::Conic:
            // Keep conic coefficients continuous during optimization. The
            // configured merit bounds provide the soft +/-100 constraint;
            // projecting here makes boundary values identical and can erase
            // the population differences that drive differential evolution.
            return value;
        case VariableKind::GlassChoice: {
            const int lo = static_cast<int>(std::ceil(std::min(variable.lower, variable.upper)));
            const int hi = std::max(
                lo,
                static_cast<int>(std::floor(std::max(variable.lower, variable.upper))));
            return static_cast<double>(std::clamp(
                static_cast<int>(std::llround(value)),
                lo,
                hi));
        }
        case VariableKind::VignetteX:
        case VariableKind::VignetteY:
        case VariableKind::VignetteCenterY:
        case VariableKind::VignetteMagnitudeY:
        case VariableKind::VignetteAsymmetryY:
            // These bounds are numerical pupil-map safety limits, not merely
            // search-scale hints. Geometry variables intentionally retain
            // their existing relaxed continuous behavior.
            return std::clamp(
                value,
                std::min(variable.lower, variable.upper),
                std::max(variable.lower, variable.upper));
        default:
            return value;
    }
}

void project_variable_values_in_place(
    const std::vector<VariableSpec>& variables,
    std::vector<double>* values) {
    if (values == nullptr || values->size() != variables.size()) {
        throw std::invalid_argument("variable/value size mismatch");
    }
    for (std::size_t i = 0; i < variables.size(); ++i) {
        (*values)[i] = project_variable_value(variables[i], (*values)[i]);
    }
}

void configure_variable_thickness_constraints(
    MeritConfig* config,
    const std::vector<VariableSpec>& variables) {
    if (config == nullptr) {
        return;
    }
    config->thickness_variable_indices.clear();
    for (const VariableSpec& variable : variables) {
        if (variable.kind != VariableKind::Thickness) {
            continue;
        }
        if (std::find(
                config->thickness_variable_indices.begin(),
                config->thickness_variable_indices.end(),
                variable.index) == config->thickness_variable_indices.end()) {
            config->thickness_variable_indices.push_back(variable.index);
        }
    }
    // An empty variable-only set has no finite minimum edge thickness and is
    // intentionally treated as the ordinary frozen-geometry constraint set.
    // This matters for material-only and vignetting-only optimization stages.
    config->thickness_constraints_variable_only =
        !config->thickness_variable_indices.empty();
}

double Material::refractive_index(double wavelength_um) const {
    if (name == "air") {
        return 1.0;
    }

    if (model == Model::Ideal) {
        return nd;
    }

    if (model == Model::Formula && !coefficients.empty()) {
        const auto& c = coefficients;
        const double w = wavelength_um;
        double n2 = 1.0;
        if (formula_type == 1) {
            n2 = 1.0 + c[0];
            for (std::size_t k = 1; k + 1 < c.size(); k += 2) {
                n2 += c[k] * w * w / (w * w - c[k + 1] * c[k + 1]);
            }
            return std::sqrt(std::max(0.0, n2));
        }
        if (formula_type == 2) {
            n2 = 1.0 + c[0];
            for (std::size_t k = 1; k + 1 < c.size(); k += 2) {
                n2 += c[k] * w * w / (w * w - c[k + 1]);
            }
            return std::sqrt(std::max(0.0, n2));
        }
        if (formula_type == 5) {
            double n = c[0];
            for (std::size_t k = 1; k + 1 < c.size(); k += 2) {
                n += c[k] * std::pow(w, c[k + 1]);
            }
            return n;
        }
    }

    if (model == Model::Tabulated && table_wavelengths.size() >= 2 &&
        table_wavelengths.size() == table_n.size()) {
        if (wavelength_um <= table_wavelengths.front()) {
            return table_n.front();
        }
        if (wavelength_um >= table_wavelengths.back()) {
            return table_n.back();
        }
        for (std::size_t i = 1; i < table_wavelengths.size(); ++i) {
            if (wavelength_um <= table_wavelengths[i]) {
                const double x0 = table_wavelengths[i - 1];
                const double x1 = table_wavelengths[i];
                const double t = (wavelength_um - x0) / (x1 - x0);
                return table_n[i - 1] * (1.0 - t) + table_n[i] * t;
            }
        }
    }

    if (vd <= 0.0 || std::abs(nd - 1.0) < 1.0e-10) {
        return nd;
    }

    const double inv_v = 1.0 / vd;
    const double inv_v2 = inv_v * inv_v;
    const double nd_sq = nd * nd;
    const double nd_div_v = nd / vd;

    const double v1 =
        0.004160 + 4.462559 * inv_v + 2.326660 * inv_v2 + 0.002330 * nd -
        0.003697 * nd_sq - 4.697604 * nd_div_v;
    const double v2 =
        0.066434 - 7.636396 * inv_v + 12.597434 * inv_v2 -
        0.037014 * nd_sq + 5.551013 * nd_div_v;
    const double v3 =
        -0.032218 + 2.230357 * inv_v - 103.318994 * inv_v2 -
        0.009654 * nd_sq + 1.934983 * nd_div_v;

    constexpr double wave_ref = 0.5875618;
    constexpr double alpha = 2.5;
    const double dlambda = wavelength_um - wave_ref;
    const double omega = dlambda / (1.0 + alpha * dlambda);
    return nd + v1 * omega + v2 * omega * omega + v3 * omega * omega * omega;
}

LensSystem apply_variables(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values) {
    return apply_variables_row(base, variables, values.data(), values.size());
}

LensSystem apply_variables_row(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const double* values,
    std::size_t count) {
    if (variables.size() != count || (count > 0 && values == nullptr)) {
        throw std::invalid_argument("variable/value size mismatch");
    }

    LensSystem lens = base;
    std::vector<double> vignetting_magnitudes(
        lens.fields.size(), std::numeric_limits<double>::quiet_NaN());
    std::vector<double> vignetting_asymmetries(
        lens.fields.size(), std::numeric_limits<double>::quiet_NaN());
    for (std::size_t i = 0; i < variables.size(); ++i) {
        const auto& var = variables[i];
        const double value = project_variable_value(var, values[i]);
        switch (var.kind) {
            case VariableKind::Radius:
                if (var.index < lens.surfaces.size()) {
                    auto& surface = lens.surfaces[var.index];
                    surface.radius = value;
                    if (surface.kind == SurfaceKind::Plane && !surface.is_stop &&
                        std::isfinite(value) && std::abs(value) >= kEps) {
                        surface.kind = SurfaceKind::Standard;
                    }
                }
                break;
            case VariableKind::Curvature:
                if (var.index < lens.surfaces.size()) {
                    auto& surface = lens.surfaces[var.index];
                    surface.radius = radius_from_curvature(value);
                    if (surface.kind == SurfaceKind::Plane && !surface.is_stop &&
                        std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps) {
                        surface.kind = SurfaceKind::Standard;
                    }
                }
                break;
            case VariableKind::Thickness:
                if (var.index < lens.surfaces.size()) {
                    lens.surfaces[var.index].thickness = value;
                }
                break;
            case VariableKind::Conic:
                if (var.index < lens.surfaces.size()) {
                    lens.surfaces[var.index].conic = value;
                }
                break;
            case VariableKind::AsphereCoeff:
                if (var.index < lens.surfaces.size()) {
                    auto& surface = lens.surfaces[var.index];
                    const bool already_asphere =
                        surface.kind == SurfaceKind::EvenAsphere ||
                        !surface.asphere_coefficients.empty();
                    if (!already_asphere && std::abs(value) <= kEps) {
                        break;
                    }
                    if (surface.asphere_coefficients.size() <= var.term) {
                        surface.asphere_coefficients.resize(var.term + 1, 0.0);
                    }
                    surface.asphere_coefficients[var.term] = value;
                    if (surface.kind == SurfaceKind::Standard ||
                        (surface.kind == SurfaceKind::Plane && !surface.is_stop)) {
                        surface.kind = SurfaceKind::EvenAsphere;
                    }
                }
                break;
            case VariableKind::MaterialNd:
                if (var.index < lens.surfaces.size()) {
                    auto& material = lens.surfaces[var.index].material_after;
                    const double new_vd =
                        std::isfinite(material.vd) && material.vd > 0.0 ? material.vd : 50.0;
                    force_model_abbe(material, value, new_vd);
                }
                break;
            case VariableKind::MaterialVd:
                if (var.index < lens.surfaces.size()) {
                    auto& material = lens.surfaces[var.index].material_after;
                    const double new_nd =
                        std::isfinite(material.nd) && material.nd > 1.0 ? material.nd : 1.5;
                    force_model_abbe(material, new_nd, value);
                }
                break;
            case VariableKind::VignetteX:
                if (var.index < lens.fields.size()) {
                    lens.fields[var.index].fvdx = value;
                    lens.fields[var.index].vignette_x = value;
                }
                break;
            case VariableKind::VignetteY:
                if (var.index < lens.fields.size()) {
                    lens.fields[var.index].fvdy = value;
                    lens.fields[var.index].vignette_y = value;
                }
                break;
            case VariableKind::VignetteCenterY:
                if (var.index < lens.fields.size()) {
                    lens.fields[var.index].fvcy = value;
                }
                break;
            case VariableKind::VignetteMagnitudeY:
                if (var.index < lens.fields.size()) {
                    vignetting_magnitudes[var.index] = value;
                }
                break;
            case VariableKind::VignetteAsymmetryY:
                if (var.index < lens.fields.size()) {
                    vignetting_asymmetries[var.index] = value;
                }
                break;
            case VariableKind::GlassChoice:
            case VariableKind::EvaluatorSemiAperture:
            case VariableKind::EvaluatorMechanicalAperture:
            case VariableKind::EvaluatorApertureRadius:
            case VariableKind::EvaluatorMaterialIndex:
            case VariableKind::EvaluatorFieldHx:
            case VariableKind::EvaluatorFieldHy:
            case VariableKind::EvaluatorVignetteCenterX:
                break;
        }
    }
    for (std::size_t field_index = 0; field_index < lens.fields.size(); ++field_index) {
        const bool has_magnitude = std::isfinite(vignetting_magnitudes[field_index]);
        const bool has_asymmetry = std::isfinite(vignetting_asymmetries[field_index]);
        if (!has_magnitude && !has_asymmetry) {
            continue;
        }
        Field& field = lens.fields[field_index];
        const double magnitude = std::clamp(
            has_magnitude ? vignetting_magnitudes[field_index]
                          : std::max(0.0, field.fvdy),
            0.0,
            1.0);
        const double inherited_asymmetry =
            field.fvdy > kEps
                ? std::clamp(field.fvcy / field.fvdy, -1.0, 1.0)
                : 0.0;
        const double asymmetry = std::clamp(
            has_asymmetry ? vignetting_asymmetries[field_index]
                          : inherited_asymmetry,
            -1.0,
            1.0);
        field.fvdy = magnitude;
        field.vignette_y = magnitude;
        field.fvcy = asymmetry * magnitude;
    }
    return lens;
}

LensSystem apply_mixed_variables(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values,
    const GlassSearchContext& glass_search) {
    return apply_mixed_variables_row(
        base,
        variables,
        values.data(),
        values.size(),
        glass_search);
}

LensSystem apply_mixed_variables_row(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const double* values,
    std::size_t count,
    const GlassSearchContext& glass_search) {
    LensSystem lens = apply_variables_row(base, variables, values, count);
    if (!glass_search.enabled) {
        return lens;
    }
    if (variables.size() != count || (count > 0 && values == nullptr)) {
        throw std::invalid_argument("variable/value size mismatch");
    }

    for (std::size_t i = 0; i < variables.size(); ++i) {
        const auto& var = variables[i];
        if (var.kind != VariableKind::GlassChoice) {
            continue;
        }
        const GlassChoiceGroup* group = nullptr;
        if (var.glass_group >= 0) {
            for (const auto& candidate_group : glass_search.groups) {
                if (candidate_group.id == var.glass_group) {
                    group = &candidate_group;
                    break;
                }
            }
        }
        if (!group) {
            for (const auto& candidate_group : glass_search.groups) {
                if (candidate_group.surface_index == var.index) {
                    group = &candidate_group;
                    break;
                }
            }
        }
        if (!group || group->candidates.empty()) {
            continue;
        }
        const int lo = 0;
        const int hi = static_cast<int>(group->candidates.size()) - 1;
        const int choice = std::clamp(
            static_cast<int>(std::llround(project_variable_value(var, values[i]))),
            lo,
            hi);
        const int initial_choice = std::clamp(
            static_cast<int>(std::llround(var.initial)),
            lo,
            hi);
        const std::size_t surface_index =
            group->surface_index < lens.surfaces.size() ? group->surface_index : var.index;
        if (surface_index >= lens.surfaces.size()) {
            continue;
        }
        if (choice == initial_choice) {
            continue;
        }
        lens.surfaces[surface_index].material_after =
            group->candidates[static_cast<std::size_t>(choice)].material;
    }
    return lens;
}

std::vector<PupilPoint> make_hexapolar_distribution(int rings) {
    std::vector<PupilPoint> points;
    points.push_back({0.0, 0.0});
    if (rings <= 0) {
        return points;
    }

    for (int ring = 1; ring <= rings; ++ring) {
        const int count = 6 * ring;
        const double rho = static_cast<double>(ring) / static_cast<double>(rings);
        for (int i = 0; i < count; ++i) {
            const double theta = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(count);
            points.push_back({rho * std::cos(theta), rho * std::sin(theta)});
        }
    }
    return points;
}

namespace {

std::string normalized_distribution(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        if (c == '-' || c == ' ') {
            return '_';
        }
        return static_cast<char>(std::tolower(c));
    });
    return name;
}

struct GaussianRadialRule {
    std::vector<double> radii;
    std::vector<double> weights;
};

GaussianRadialRule gaussian_radial_rule(int rings) {
    const int n = std::max(1, rings);
    if (n == 8 || n == 12) {
        GaussianRadialRule cached;
        cached.radii.resize(n); cached.weights.resize(n);
        for (int i=0; i<n; ++i)
            fixed_budget_gaussian_node_weight(n,i,&cached.radii[i],&cached.weights[i]);
        return cached;
    }
    switch (n) {
        case 1:
            return {{0.70711}, {0.5}};
        case 2:
            return {{0.45970, 0.88807}, {0.25, 0.25}};
        case 3:
            return {
                {0.33571, 0.70711, 0.94196},
                {0.13889, 0.22222, 0.13889}};
        case 4:
            return {
                {0.26350, 0.57446, 0.81853, 0.96466},
                {0.08696, 0.16304, 0.16304, 0.08696}};
        case 5:
            return {
                {0.21659, 0.48038, 0.70711, 0.87706, 0.97626},
                {0.059231, 0.11966, 0.14222, 0.11966, 0.059231}};
        case 6:
            return {
                {0.18375, 0.41158, 0.61700, 0.78696, 0.91138, 0.98300},
                {0.04283, 0.09019, 0.11698, 0.11698, 0.09019, 0.04283}};
        default:
            break;
    }

    // The legacy tables above are Gauss-Legendre nodes after mapping
    // x in [-1, 1] to the radial-area coordinate u = r^2 in [0, 1].
    // Generate the same rule dynamically for ring counts beyond the table.
    GaussianRadialRule rule;
    rule.radii.resize(static_cast<std::size_t>(n));
    rule.weights.resize(static_cast<std::size_t>(n));
    const int root_count = (n + 1) / 2;
    constexpr int kMaxNewtonIterations = 100;
    constexpr double kRootTolerance = 1.0e-15;
    for (int root = 0; root < root_count; ++root) {
        double z = std::cos(
            kPi * (static_cast<double>(root) + 0.75) /
            (static_cast<double>(n) + 0.5));
        double derivative = 0.0;
        for (int iteration = 0; iteration < kMaxNewtonIterations; ++iteration) {
            double p_prev = 1.0;
            double p = z;
            for (int order = 2; order <= n; ++order) {
                const double p_next =
                    ((2.0 * order - 1.0) * z * p - (order - 1.0) * p_prev) /
                    static_cast<double>(order);
                p_prev = p;
                p = p_next;
            }
            if (n == 1) {
                p_prev = 1.0;
                p = z;
            }
            derivative = static_cast<double>(n) * (z * p - p_prev) / (z * z - 1.0);
            const double next = z - p / derivative;
            if (std::abs(next - z) <= kRootTolerance) {
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
        if (n == 1) {
            p_prev = 1.0;
            p = z;
        }
        derivative = static_cast<double>(n) * (z * p - p_prev) / (z * z - 1.0);
        const double radial_weight =
            0.5 / ((1.0 - z * z) * derivative * derivative);
        const int lower = root;
        const int upper = n - 1 - root;
        rule.radii[static_cast<std::size_t>(lower)] =
            std::sqrt(std::max(0.0, 0.5 * (1.0 - z)));
        rule.weights[static_cast<std::size_t>(lower)] = radial_weight;
        rule.radii[static_cast<std::size_t>(upper)] =
            std::sqrt(std::max(0.0, 0.5 * (1.0 + z)));
        rule.weights[static_cast<std::size_t>(upper)] = radial_weight;
    }
    return rule;
}

bool is_gaussian_ring_arm_mode(const std::string& mode) {
    return mode == "gaussian_ring_arm" ||
           mode == "gaussian_ring_arms" ||
           mode == "gaussian_arm" ||
           mode == "gaussian_arms" ||
           mode.rfind("gaussian_ring_arm_", 0) == 0 ||
           mode.rfind("gaussian_ring_arms_", 0) == 0 ||
           mode.rfind("gaussian_arm_", 0) == 0 ||
           mode.rfind("gaussian_arms_", 0) == 0 ||
           mode.rfind("gaussian_ring_arm:", 0) == 0 ||
           mode.rfind("gaussian_ring_arms:", 0) == 0 ||
           mode.rfind("gaussian_arm:", 0) == 0 ||
           mode.rfind("gaussian_arms:", 0) == 0;
}

int gaussian_ring_arm_count(const std::string& mode, int arms) {
    int out = arms > 0 ? arms : 6;
    std::size_t end = mode.size();
    std::size_t begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(mode[begin - 1]))) {
        --begin;
    }
    if (begin < end) {
        try {
            out = std::stoi(mode.substr(begin, end - begin));
        } catch (...) {
            out = arms > 0 ? arms : 6;
        }
    }
    return std::max(1, out);
}

std::vector<PupilPoint> make_robust_disk64_distribution() {
    // A fixed, equal-area prototype intended for sparse optimization merits.
    // The 48 interior samples use a mirror-closed low-discrepancy half-disk;
    // two staggered eight-ray rings represent the outer pupil-area quartile.
    constexpr int kInteriorPairs = 24;
    constexpr int kOuterArms = 8;
    constexpr double kInteriorArea = 0.75;
    constexpr double kGoldenConjugate = 0.6180339887498948482;

    std::vector<PupilPoint> points;
    points.reserve(64);
    for (int pair = 0; pair < kInteriorPairs; ++pair) {
        const double unit_area =
            (static_cast<double>(pair) + 0.5) / static_cast<double>(kInteriorPairs);
        const double radius = std::sqrt(kInteriorArea * unit_area);
        const double phase = std::fmod(
            (static_cast<double>(pair) + 0.5) * kGoldenConjugate,
            1.0);
        const double theta = -0.5 * kPi + kPi * phase;
        const double x = radius * std::cos(theta);
        const double y = radius * std::sin(theta);
        points.push_back({x, y, 1.0});
        points.push_back({-x, y, 1.0});
    }

    const double outer_radii[] = {
        std::sqrt(0.8125),
        std::sqrt(0.9375),
    };
    const double outer_phases[] = {
        kPi / static_cast<double>(kOuterArms),
        0.0,
    };
    for (int ring = 0; ring < 2; ++ring) {
        for (int arm = 0; arm < kOuterArms; ++arm) {
            const double theta =
                outer_phases[ring] +
                2.0 * kPi * static_cast<double>(arm) /
                    static_cast<double>(kOuterArms);
            points.push_back({
                outer_radii[ring] * std::cos(theta),
                outer_radii[ring] * std::sin(theta),
                1.0});
        }
    }
    return points;
}

void add_line_points(std::vector<PupilPoint>& points, int count, bool x_axis, bool positive_only) {
    if (count <= 0) {
        return;
    }
    if (count == 1) {
        points.push_back({0.0, 0.0});
        return;
    }
    for (int i = 0; i < count; ++i) {
        const double t = positive_only
                             ? static_cast<double>(i) / static_cast<double>(count - 1)
                             : -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(count - 1);
        points.push_back(x_axis ? PupilPoint{t, 0.0} : PupilPoint{0.0, t});
    }
}

}  // namespace

bool is_codev_rectangular_distribution(const std::string& distribution) {
    const std::string mode = normalized_distribution(distribution);
    return mode == "codev_rectangular" ||
           mode == "code_v_rectangular" ||
           mode == "codev_grid" ||
           mode == "code_v_grid" ||
           mode == "codev";
}

double effective_codev_ray_interval(
    const LensSystem& system,
    const MeritConfig& config) {
    if (std::isfinite(config.codev_ray_interval) && config.codev_ray_interval > 0.0) {
        return std::min(1.0, config.codev_ray_interval);
    }
    const std::string profile = normalized_distribution(config.codev_sampling_profile);
    if (profile == "aspherical" || profile == "asphere" ||
        profile == "non_spherical" || profile == "nonspherical" ||
        profile == "non_sphere") {
        return 0.22;
    }
    if (profile == "auto" || profile == "automatic" || profile == "system") {
        for (const Surface& surface : system.surfaces) {
            if (surface.kind == SurfaceKind::EvenAsphere ||
                std::abs(surface.conic) > 1.0e-14 ||
                std::any_of(
                    surface.asphere_coefficients.begin(),
                    surface.asphere_coefficients.end(),
                    [](double coefficient) {
                        return std::isfinite(coefficient) &&
                               std::abs(coefficient) > 1.0e-20;
                    })) {
                return 0.22;
            }
        }
    }
    return 0.385;
}

std::vector<PupilPoint> make_codev_rectangular_distribution(
    double ray_interval,
    double obscuration,
    double aperture_weight) {
    const double interval =
        std::isfinite(ray_interval) && ray_interval > 0.0
            ? std::min(1.0, ray_interval)
            : 0.22;
    const double obscuration_radius =
        std::isfinite(obscuration) ? std::clamp(obscuration, 0.0, 1.0) : 0.0;
    const double exponent = std::isfinite(aperture_weight) ? aperture_weight : 0.0;
    const double obscuration_sq = obscuration_radius * obscuration_radius;
    constexpr double kPupilTolerance = 1.0e-12;

    std::vector<PupilPoint> points;
    for (int iy = 0;; ++iy) {
        const double y = (static_cast<double>(iy) + 0.5) * interval;
        if (y >= 1.0 + kPupilTolerance) {
            break;
        }
        for (int ix = 0;; ++ix) {
            const double x = (static_cast<double>(ix) + 0.5) * interval;
            if (x >= 1.0 + kPupilTolerance) {
                break;
            }
            const double radius_sq = x * x + y * y;
            if (radius_sq > 1.0 + kPupilTolerance ||
                radius_sq < obscuration_sq - kPupilTolerance) {
                continue;
            }
            const double weight = std::pow(std::max(radius_sq, kEps), -exponent);
            for (double sy : {-1.0, 1.0}) {
                for (double sx : {-1.0, 1.0}) {
                    points.push_back({sx * x, sy * y, weight});
                }
            }
        }
    }
    return points;
}

std::vector<PupilPoint> make_pupil_distribution(
    int num_points,
    const std::string& distribution,
    unsigned int seed,
    int arms) {
    const int n = std::max(1, num_points);
    const std::string mode = normalized_distribution(distribution.empty() ? "hexapolar" : distribution);
    if (mode == "hexapolar" || mode == "hexagonal") {
        return make_hexapolar_distribution(n);
    }
    if (is_codev_rectangular_distribution(mode)) {
        return make_codev_rectangular_distribution(0.22, 0.0, 0.0);
    }
    if (mode == "robust_disk64" || mode == "robust_disk_64" ||
        mode == "robust_disk") {
        return make_robust_disk64_distribution();
    }

    std::vector<PupilPoint> points;
    if (mode == "random") {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        points.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double r = std::sqrt(unit(rng));
            const double theta = 2.0 * kPi * unit(rng);
            points.push_back({r * std::cos(theta), r * std::sin(theta)});
        }
        return points;
    }
    if (mode == "uniform" || mode == "grid") {
        points.reserve(static_cast<std::size_t>(n * n));
        if (n == 1) {
            points.push_back({0.0, 0.0});
            return points;
        }
        for (int iy = 0; iy < n; ++iy) {
            const double y = -1.0 + 2.0 * static_cast<double>(iy) / static_cast<double>(n - 1);
            for (int ix = 0; ix < n; ++ix) {
                const double x = -1.0 + 2.0 * static_cast<double>(ix) / static_cast<double>(n - 1);
                if (x * x + y * y <= 1.0 + 1.0e-12) {
                    points.push_back({x, y});
                }
            }
        }
        return points;
    }
    if (is_gaussian_ring_arm_mode(mode)) {
        const int ring_count = n;
        const int arm_count = gaussian_ring_arm_count(mode, arms);
        const auto rule = gaussian_radial_rule(ring_count);
        points.reserve(rule.radii.size() * static_cast<std::size_t>(arm_count));
        for (std::size_t ring = 0; ring < rule.radii.size(); ++ring) {
            const double r = rule.radii[ring];
            const double w =
                ring < rule.weights.size()
                    ? rule.weights[ring] / static_cast<double>(arm_count)
                    : 1.0 / static_cast<double>(arm_count);
            for (int arm = 0; arm < arm_count; ++arm) {
                const double theta =
                    2.0 * kPi * static_cast<double>(arm) / static_cast<double>(arm_count);
                points.push_back({r * std::cos(theta), r * std::sin(theta), w});
            }
        }
        return points;
    }
    if (mode == "gaussian_quad" || mode == "gaussian") {
        const auto radii = gaussian_radial_rule(n).radii;
        const double theta_values[3] = {-kPi / 3.0, 0.0, kPi / 3.0};
        for (double r : radii) {
            for (double theta : theta_values) {
                points.push_back({r * std::cos(theta), r * std::sin(theta)});
            }
        }
        return points;
    }
    if (mode == "gaussian_quad_symmetric" || mode == "gaussian_symmetric") {
        for (double r : gaussian_radial_rule(n).radii) {
            points.push_back({r, 0.0});
        }
        return points;
    }
    if (mode == "ring") {
        points.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double theta = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n);
            points.push_back({std::cos(theta), std::sin(theta)});
        }
        return points;
    }
    if (mode == "cross") {
        add_line_points(points, n, false, false);
        if (n % 2 == 1) {
            for (int i = 0; i < n; ++i) {
                if (i == n / 2) {
                    continue;
                }
                const double x = -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(n - 1);
                points.push_back({x, 0.0});
            }
        } else {
            add_line_points(points, n, true, false);
        }
        return points;
    }
    if (mode == "line_x" || mode == "positive_line_x") {
        add_line_points(points, n, true, mode == "positive_line_x");
        return points;
    }
    if (mode == "line_y" || mode == "positive_line_y") {
        add_line_points(points, n, false, mode == "positive_line_y");
        return points;
    }
    return make_hexapolar_distribution(n);
}

std::vector<std::size_t> relative_illumination_supervised_fields(
    const LensSystem& system, const MeritConfig& config) {
    auto selected = config.minimum_relative_illumination_field_indices;
    const auto& references = config.minimum_relative_illumination_field_radii;
    if (!selected.empty() && !references.empty()) {
        throw std::invalid_argument("RI indices and reference radii are mutually exclusive");
    }
    for (const auto index : selected) {
        if (index >= system.fields.size()) {
            throw std::invalid_argument("RI field index outside system fields");
        }
    }
    for (const double reference : references) {
        if (!std::isfinite(reference) || reference < 0 || system.fields.empty()) {
            throw std::invalid_argument("invalid RI reference field radius");
        }
        double distance = std::numeric_limits<double>::infinity();
        std::size_t nearest = 0;
        for (std::size_t i = 0; i < system.fields.size(); ++i) {
            const auto& field = system.fields[i];
            const double delta = std::abs(std::hypot(field.hx, field.hy) - reference);
            if (delta < distance) { distance = delta; nearest = i; }
        }
        selected.push_back(nearest);
    }
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
}

}  // namespace optiland
