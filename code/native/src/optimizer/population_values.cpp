/*
 * Access flat population rows and compute population-scale summaries and variable-kind predicates.
 * Optimizer generation/selection code shares these row operations under the supplied dimension layout.
 * Preserve row-major indexing, per-variable projection and aggregation order.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double population_stddev(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    double var = 0.0;
    for (double value : values) {
        const double d = value - mean;
        var += d * d;
    }
    return std::sqrt(var / static_cast<double>(values.size()));
}

double population_mean_abs(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    return std::abs(mean);
}

std::vector<double> population_row(const std::vector<double>& population, int index, int dim) {
    std::vector<double> row(static_cast<std::size_t>(dim), 0.0);
    for (int j = 0; j < dim; ++j) {
        row[static_cast<std::size_t>(j)] =
            population[static_cast<std::size_t>(index) * dim + j];
    }
    return row;
}

void set_population_row(
    std::vector<double>& population,
    int index,
    const std::vector<double>& row,
    const std::vector<VariableSpec>& variables,
    InternalScalingMode mode ) {
    const int dim = static_cast<int>(variables.size());
    for (int j = 0; j < dim; ++j) {
        const auto& var = variables[static_cast<std::size_t>(j)];
        const auto [lo, hi] = internal_bounds(var, mode);
        population[static_cast<std::size_t>(index) * dim + j] =
            maybe_clip_continuous(var, row[static_cast<std::size_t>(j)], lo, hi);
    }
}

bool thickness_variable_is_glass_space(const LensSystem& system, const VariableSpec& var) {
    if (var.kind != VariableKind::Thickness || var.index >= system.surfaces.size()) {
        return false;
    }
    const Material& material = system.surfaces[var.index].material_after;
    if (material.nd > 1.0005) {
        return true;
    }
    const double n = material.refractive_index(system.primary_wavelength());
    return std::isfinite(n) && n > 1.0005;
}

bool is_material_variable(const VariableSpec& var) {
    return var.kind == VariableKind::MaterialNd ||
           var.kind == VariableKind::MaterialVd;
}

}  // namespace optimizer_detail

}  // namespace optiland
