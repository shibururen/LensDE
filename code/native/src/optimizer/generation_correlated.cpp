/*
 * Generate correlated asphere-sag and coupled/single-block refresh moves.
 * Population generation supplies the reference system, variable blocks and the caller-owned random stream.
 * Preserve block/term traversal, linear solve order and draw sequence when forming correlated candidates.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

bool solve_correlated_sag_system(
    std::array<std::array<double, 7>, 7> matrix,
    std::array<double, 7> values,
    std::array<double, 7>* solution,
    int dimension) {
    if (solution == nullptr || dimension < 1 || dimension > 7) {
        return false;
    }
    for (int column = 0; column < dimension; ++column) {
        int pivot = column;
        for (int row = column + 1; row < dimension; ++row) {
            if (std::abs(matrix[static_cast<std::size_t>(row)]
                                [static_cast<std::size_t>(column)]) >
                std::abs(matrix[static_cast<std::size_t>(pivot)]
                                [static_cast<std::size_t>(column)])) {
                pivot = row;
            }
        }
        const double divisor = matrix[static_cast<std::size_t>(pivot)]
                                     [static_cast<std::size_t>(column)];
        if (!std::isfinite(divisor) || std::abs(divisor) <= 1.0e-18) {
            return false;
        }
        if (pivot != column) {
            std::swap(matrix[static_cast<std::size_t>(pivot)],
                      matrix[static_cast<std::size_t>(column)]);
            std::swap(values[static_cast<std::size_t>(pivot)],
                      values[static_cast<std::size_t>(column)]);
        }
        const double scale = matrix[static_cast<std::size_t>(column)]
                                   [static_cast<std::size_t>(column)];
        for (int item = column; item < dimension; ++item) {
            matrix[static_cast<std::size_t>(column)][static_cast<std::size_t>(item)] /=
                scale;
        }
        values[static_cast<std::size_t>(column)] /= scale;
        for (int row = 0; row < dimension; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = matrix[static_cast<std::size_t>(row)]
                                        [static_cast<std::size_t>(column)];
            if (factor == 0.0) {
                continue;
            }
            for (int item = column; item < dimension; ++item) {
                matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(item)] -=
                    factor * matrix[static_cast<std::size_t>(column)]
                                   [static_cast<std::size_t>(item)];
            }
            values[static_cast<std::size_t>(row)] -=
                factor * values[static_cast<std::size_t>(column)];
        }
    }
    *solution = values;
    return std::all_of(solution->begin(), solution->end(), [](double value) {
        return std::isfinite(value);
    });
}

std::vector<double> make_single_surface_correlated_sag_generation_row(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& parent_scaled,
    InternalScalingMode mode,
    double sag_perturbation_um,
    double correlation_length,
    std::mt19937& rng,
    std::size_t* selected_surface_output ) {
    if (selected_surface_output != nullptr) {
        *selected_surface_output = std::numeric_limits<std::size_t>::max();
    }
    const int dim = static_cast<int>(variables.size());
    const std::vector<double> parent_physical =
        scaled_to_physical(variables, parent_scaled, mode);
    if (static_cast<int>(parent_physical.size()) != dim) {
        return parent_scaled;
    }

    std::unordered_map<std::size_t, std::array<int, 7>> grouped_columns;
    for (int column = 0; column < dim; ++column) {
        const VariableSpec& variable = variables[static_cast<std::size_t>(column)];
        if (variable.kind != VariableKind::AsphereCoeff ||
            variable.term < 1 || variable.term > 7) {
            continue;
        }
        auto [it, inserted] = grouped_columns.try_emplace(variable.index);
        if (inserted) {
            it->second.fill(-1);
        }
        it->second[variable.term - 1] = column;
    }
    // Keep the A16-sized stack storage, but solve only the configured A4..Amax
    // prefix. Missing higher orders are not new optimization variables.
    const auto active_dimension = [](const std::array<int, 7>& columns) {
        int dimension = 7;
        while (dimension > 0 && columns[static_cast<std::size_t>(dimension - 1)] < 0) {
            --dimension;
        }
        return dimension;
    };
    std::vector<std::size_t> complete_surfaces;
    for (const auto& [surface, columns] : grouped_columns) {
        const int dimension = active_dimension(columns);
        const bool complete = dimension > 0 && std::all_of(
            columns.begin(), columns.begin() + dimension, [&](int column) {
            return column >= 0 && column < dim &&
                   std::isfinite(frozen_asphere_radial_power(
                       variables[static_cast<std::size_t>(column)])) &&
                   frozen_asphere_radial_power(
                       variables[static_cast<std::size_t>(column)]) > 0.0;
        });
        if (complete) {
            complete_surfaces.push_back(surface);
        }
    }
    if (complete_surfaces.empty()) {
        return parent_scaled;
    }
    std::sort(complete_surfaces.begin(), complete_surfaces.end());
    const std::size_t selected_surface = complete_surfaces[static_cast<std::size_t>(
        std::uniform_int_distribution<int>(
            0,
            static_cast<int>(complete_surfaces.size()) - 1)(rng))];
    if (selected_surface_output != nullptr) {
        *selected_surface_output = selected_surface;
    }
    const auto& columns = grouped_columns.at(selected_surface);
    const int dimension = active_dimension(columns);

    const double target_cap_mm = std::max(0.0, sag_perturbation_um) * 1.0e-3;
    if (!(target_cap_mm > 0.0)) {
        return parent_scaled;
    }
    const double length = std::max(1.0e-3, correlation_length);
    std::array<double, 7> nodes{};
    std::array<std::array<double, 7>, 7> vandermonde{};
    std::array<std::array<double, 7>, 7> covariance{};
    std::array<std::array<double, 7>, 7> cholesky{};
    for (int row = 0; row < dimension; ++row) {
        nodes[static_cast<std::size_t>(row)] =
            static_cast<double>(row + 1) / static_cast<double>(dimension);
        for (int column = 0; column < dimension; ++column) {
            vandermonde[static_cast<std::size_t>(row)]
                       [static_cast<std::size_t>(column)] =
                std::pow(nodes[static_cast<std::size_t>(row)], 2 * (column + 2));
            const double difference =
                (nodes[static_cast<std::size_t>(row)] -
                 nodes[static_cast<std::size_t>(column)]) /
                length;
            covariance[static_cast<std::size_t>(row)]
                      [static_cast<std::size_t>(column)] =
                std::exp(-0.5 * difference * difference) +
                (row == column ? 1.0e-10 : 0.0);
        }
    }
    for (int row = 0; row < dimension; ++row) {
        for (int column = 0; column <= row; ++column) {
            double residual = covariance[static_cast<std::size_t>(row)]
                                         [static_cast<std::size_t>(column)];
            for (int index = 0; index < column; ++index) {
                residual -= cholesky[static_cast<std::size_t>(row)]
                                    [static_cast<std::size_t>(index)] *
                            cholesky[static_cast<std::size_t>(column)]
                                    [static_cast<std::size_t>(index)];
            }
            if (row == column) {
                if (!(residual > 1.0e-15)) {
                    return parent_scaled;
                }
                cholesky[static_cast<std::size_t>(row)]
                        [static_cast<std::size_t>(column)] = std::sqrt(residual);
            } else {
                const double divisor = cholesky[static_cast<std::size_t>(column)]
                                               [static_cast<std::size_t>(column)];
                if (std::abs(divisor) <= 1.0e-18) {
                    return parent_scaled;
                }
                cholesky[static_cast<std::size_t>(row)]
                        [static_cast<std::size_t>(column)] = residual / divisor;
            }
        }
    }

    std::normal_distribution<double> normal(0.0, 1.0);
    std::vector<double> best_physical = parent_physical;
    double best_actual_peak_um = 0.0;
    for (int attempt = 0; attempt < 128; ++attempt) {
        std::array<double, 7> independent{};
        std::array<double, 7> profile{};
        for (int index = 0; index < dimension; ++index) {
            independent[static_cast<std::size_t>(index)] = normal(rng);
        }
        for (int row = 0; row < dimension; ++row) {
            for (int column = 0; column <= row; ++column) {
                profile[static_cast<std::size_t>(row)] +=
                    cholesky[static_cast<std::size_t>(row)]
                            [static_cast<std::size_t>(column)] *
                    independent[static_cast<std::size_t>(column)];
            }
        }
        double profile_scale = 0.0;
        for (double value : profile) {
            profile_scale = std::max(profile_scale, std::abs(value));
        }
        if (!(profile_scale > 1.0e-15)) {
            continue;
        }
        std::array<double, 7> desired{};
        for (int index = 0; index < dimension; ++index) {
            const double node = nodes[static_cast<std::size_t>(index)];
            desired[static_cast<std::size_t>(index)] =
                target_cap_mm * std::pow(node, 4) *
                profile[static_cast<std::size_t>(index)] / profile_scale;
        }
        std::array<double, 7> edge_deltas{};
        if (!solve_correlated_sag_system(vandermonde, desired, &edge_deltas, dimension)) {
            continue;
        }
        double dense_peak = 0.0;
        for (int sample = 0; sample <= 256; ++sample) {
            const double node = static_cast<double>(sample) / 256.0;
            double sag = 0.0;
            for (int term = 0; term < dimension; ++term) {
                sag += edge_deltas[static_cast<std::size_t>(term)] *
                       std::pow(node, 2 * (term + 2));
            }
            dense_peak = std::max(dense_peak, std::abs(sag));
        }
        if (!std::isfinite(dense_peak) || dense_peak <= 1.0e-18) {
            continue;
        }
        for (double& value : edge_deltas) {
            value *= target_cap_mm / dense_peak;
        }
        std::array<double, 7> coefficient_deltas{};
        double bound_scale = 1.0;
        for (int term = 0; term < dimension; ++term) {
            const int column = columns[static_cast<std::size_t>(term)];
            const VariableSpec& variable = variables[static_cast<std::size_t>(column)];
            const double radial_power = frozen_asphere_radial_power(variable);
            const double delta = edge_deltas[static_cast<std::size_t>(term)] /
                                 radial_power;
            coefficient_deltas[static_cast<std::size_t>(term)] = delta;
            const double current = parent_physical[static_cast<std::size_t>(column)];
            if (delta > 0.0 && std::isfinite(variable.upper)) {
                bound_scale = std::min(bound_scale, (variable.upper - current) / delta);
            } else if (delta < 0.0 && std::isfinite(variable.lower)) {
                bound_scale = std::min(bound_scale, (current - variable.lower) / (-delta));
            }
        }
        bound_scale = std::clamp(0.98 * bound_scale, 0.0, 1.0);
        std::vector<double> candidate = parent_physical;
        for (int term = 0; term < dimension; ++term) {
            const int column = columns[static_cast<std::size_t>(term)];
            candidate[static_cast<std::size_t>(column)] +=
                bound_scale * coefficient_deltas[static_cast<std::size_t>(term)];
        }
        const double actual_peak_um = sag_perturbation_um * bound_scale;
        if (actual_peak_um > best_actual_peak_um) {
            best_actual_peak_um = actual_peak_um;
            best_physical = std::move(candidate);
        }
        if (actual_peak_um >= 0.20 * sag_perturbation_um) {
            break;
        }
    }
    return physical_to_scaled(variables, best_physical, mode);
}

std::vector<double> make_single_block_refresh_row(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& parent_scaled,
    const PopulationGenerationOptions& options,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni) {
    const double choice = uni(rng);
    const double vignette_probability =
        std::clamp(options.vignetting_child_fraction, 0.0, 1.0);
    const double geometry_probability = std::clamp(
        options.geometry_child_fraction, 0.0, 1.0 - vignette_probability);
    const bool vignette_child = choice < vignette_probability;
    const bool geometry_child =
        !vignette_child && choice < vignette_probability + geometry_probability;
    if (!vignette_child && !geometry_child) {
        return make_single_surface_correlated_sag_generation_row(
            variables, parent_scaled, mode,
            options.asphere_sag_perturbation_um,
            options.asphere_sag_correlation_length, rng);
    }
    std::vector<double> physical =
        scaled_to_physical(variables, parent_scaled, mode);
    if (physical.size() != variables.size()) {
        return parent_scaled;
    }
    std::vector<std::size_t> groups;
    for (const VariableSpec& var : variables) {
        const bool selected_kind = vignette_child
            ? (var.kind == VariableKind::VignetteMagnitudeY ||
               var.kind == VariableKind::VignetteAsymmetryY)
            : (var.kind == VariableKind::Radius ||
               var.kind == VariableKind::Curvature ||
               var.kind == VariableKind::Thickness ||
               var.kind == VariableKind::Conic);
        if (selected_kind &&
            std::find(groups.begin(), groups.end(), var.index) == groups.end()) {
            groups.push_back(var.index);
        }
    }
    if (groups.empty()) {
        return make_single_surface_correlated_sag_generation_row(
            variables, parent_scaled, mode,
            options.asphere_sag_perturbation_um,
            options.asphere_sag_correlation_length, rng);
    }
    const std::size_t selected_group = groups[static_cast<std::size_t>(
        std::uniform_int_distribution<int>(0, static_cast<int>(groups.size()) - 1)(rng))];
    std::size_t selected_geometry_column = std::numeric_limits<std::size_t>::max();
    if (geometry_child && options.geometry_single_variable) {
        std::vector<std::size_t> columns;
        for (std::size_t column = 0; column < variables.size(); ++column) {
            const VariableSpec& var = variables[column];
            if (var.index == selected_group &&
                (var.kind == VariableKind::Radius ||
                 var.kind == VariableKind::Curvature ||
                 var.kind == VariableKind::Thickness ||
                 var.kind == VariableKind::Conic)) {
                columns.push_back(column);
            }
        }
        if (!columns.empty()) {
            selected_geometry_column = columns[static_cast<std::size_t>(
                std::uniform_int_distribution<int>(0, static_cast<int>(columns.size()) - 1)(rng))];
        }
    }
    std::normal_distribution<double> normal(0.0, 1.0);
    for (std::size_t column = 0; column < variables.size(); ++column) {
        const VariableSpec& var = variables[column];
        if (var.index != selected_group) {
            continue;
        }
        if (geometry_child && options.geometry_single_variable &&
            column != selected_geometry_column) {
            continue;
        }
        const double current = physical[column];
        double sigma = 0.0;
        if (vignette_child) {
            if (var.kind == VariableKind::VignetteMagnitudeY ||
                var.kind == VariableKind::VignetteAsymmetryY) {
                sigma = options.vignetting_bound_fraction *
                    (var.upper - var.lower);
            }
        } else if (var.kind == VariableKind::Radius ||
                   var.kind == VariableKind::Curvature) {
            sigma = options.geometry_local_scale *
                std::max(0.14 * std::abs(current), 0.0035);
        } else if (var.kind == VariableKind::Thickness) {
            sigma = options.geometry_local_scale *
                std::max(0.08 * std::abs(current), 0.18);
        } else if (var.kind == VariableKind::Conic) {
            sigma = options.geometry_local_scale *
                std::max(0.20 * std::abs(current), 5.0);
        }
        if (sigma > 0.0 && var.lower <= var.upper) {
            physical[column] = std::clamp(
                current + sigma * normal(rng), var.lower, var.upper);
        }
    }
    return physical_to_scaled(variables, physical, mode);
}

std::vector<double> make_coupled_surface_refresh_row(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& parent_scaled,
    const PopulationGenerationOptions& options,
    InternalScalingMode mode,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni) {
    std::size_t surface = std::numeric_limits<std::size_t>::max();
    const std::vector<double> sag_scaled =
        make_single_surface_correlated_sag_generation_row(
            variables, parent_scaled, mode,
            options.asphere_sag_perturbation_um,
            options.asphere_sag_correlation_length, rng, &surface);
    if (surface == std::numeric_limits<std::size_t>::max()) {
        return sag_scaled;
    }
    std::vector<double> physical = scaled_to_physical(variables, sag_scaled, mode);
    if (physical.size() != variables.size()) {
        return sag_scaled;
    }
    std::normal_distribution<double> normal(0.0, 1.0);
    for (std::size_t column = 0; column < variables.size(); ++column) {
        const VariableSpec& var = variables[column];
        if (var.index != surface) {
            continue;
        }
        const double current = physical[column];
        double sigma = 0.0;
        if (var.kind == VariableKind::Radius ||
            var.kind == VariableKind::Curvature) {
            sigma = options.geometry_local_scale *
                std::max(0.14 * std::abs(current), 0.0035);
        } else if (var.kind == VariableKind::Thickness) {
            sigma = options.geometry_local_scale *
                std::max(0.08 * std::abs(current), 0.18);
        } else if (var.kind == VariableKind::Conic) {
            sigma = options.geometry_local_scale *
                std::max(0.20 * std::abs(current), 5.0);
        }
        if (sigma > 0.0 && var.lower <= var.upper) {
            physical[column] = std::clamp(
                current + sigma * normal(rng), var.lower, var.upper);
        }
    }
    if (uni(rng) < std::clamp(options.vignetting_child_fraction, 0.0, 1.0)) {
        std::vector<std::size_t> fields;
        for (const VariableSpec& var : variables) {
            if ((var.kind == VariableKind::VignetteMagnitudeY ||
                 var.kind == VariableKind::VignetteAsymmetryY) &&
                std::find(fields.begin(), fields.end(), var.index) == fields.end()) {
                fields.push_back(var.index);
            }
        }
        if (!fields.empty()) {
            const std::size_t field = fields[static_cast<std::size_t>(
                std::uniform_int_distribution<int>(0, static_cast<int>(fields.size()) - 1)(rng))];
            for (std::size_t column = 0; column < variables.size(); ++column) {
                const VariableSpec& var = variables[column];
                if (var.index != field ||
                    (var.kind != VariableKind::VignetteMagnitudeY &&
                     var.kind != VariableKind::VignetteAsymmetryY)) {
                    continue;
                }
                const double sigma = options.vignetting_bound_fraction *
                    (var.upper - var.lower);
                if (sigma > 0.0 && var.lower <= var.upper) {
                    physical[column] = std::clamp(
                        physical[column] + sigma * normal(rng), var.lower, var.upper);
                }
            }
        }
    }
    return physical_to_scaled(variables, physical, mode);
}

}  // namespace optimizer_detail

}  // namespace optiland
