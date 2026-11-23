/*
 * Convert optimizer rows for distance calculations and select fitness-ranked diverse representatives.
 * DE, hybrid and adaptive archive/selection paths supply variable metadata and coordinate modes.
 * Preserve dimension order, normalization and stable selection tie breaks across distance consumers.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

std::vector<double> physical_to_scaled(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values,
    InternalScalingMode mode ) {
    std::vector<double> out(values.size(), 0.0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto [lo, hi] = internal_bounds(variables[i], mode);
        out[i] = maybe_clip_continuous(
            variables[i],
            physical_to_internal(variables[i], values[i], mode),
            lo,
            hi);
    }
    return out;
}

double normalized_physical_value_for_distance(
    const LensSystem* system,
    const VariableSpec& var,
    double physical,
    double air_thickness_distance_cap) {
    double lo = std::min(var.lower, var.upper);
    double hi = std::max(var.lower, var.upper);
    if (system != nullptr &&
        var.kind == VariableKind::Thickness &&
        std::isfinite(air_thickness_distance_cap) &&
        air_thickness_distance_cap > 0.0 &&
        !thickness_variable_is_glass_space(*system, var)) {
        hi = std::min(hi, std::max(lo + 1.0e-12, air_thickness_distance_cap));
    }
    const double span = hi - lo;
    if (!std::isfinite(span) || span <= 0.0) {
        return 0.5;
    }
    const double clamped = std::clamp(physical, lo, std::max(var.lower, var.upper));
    return (clamped - lo) / span;
}

double normalized_from_internal_for_distance(
    const LensSystem* system,
    const VariableSpec& var,
    double internal_value,
    InternalScalingMode mode ,
    double air_thickness_distance_cap ) {
    const auto [lo, hi] = internal_bounds(var, mode);
    const double internal = std::clamp(internal_value, lo, hi);
    return normalized_physical_value_for_distance(
        system,
        var,
        internal_to_physical(var, internal, mode),
        air_thickness_distance_cap);
}

std::vector<double> scaled_to_physical(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& scaled_values,
    InternalScalingMode mode ) {
    std::vector<double> out(scaled_values.size(), 0.0);
    for (std::size_t i = 0; i < scaled_values.size(); ++i) {
        const auto& var = variables[i];
        const auto [lo, hi] = internal_bounds(var, mode);
        const double scaled = maybe_clip_continuous(var, scaled_values[i], lo, hi);
        out[i] = internal_to_physical(var, scaled, mode);
    }
    return out;
}

void physical_values_from_scaled_row(
    const std::vector<VariableSpec>& variables,
    const double* scaled_values,
    std::size_t count,
    InternalScalingMode mode,
    std::vector<double>* physical_out) {
    if (physical_out == nullptr) {
        throw std::invalid_argument("physical_out must not be null");
    }
    if (variables.size() != count || (count > 0 && scaled_values == nullptr)) {
        throw std::invalid_argument("variable/value size mismatch");
    }
    if (physical_out->size() != count) {
        physical_out->resize(count);
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto& var = variables[i];
        const auto [lo, hi] = internal_bounds(var, mode);
        const double scaled = maybe_clip_continuous(var, scaled_values[i], lo, hi);
        (*physical_out)[i] = internal_to_physical(var, scaled, mode);
    }
}

double scaled_row_distance(
    const std::vector<double>& population,
    int a,
    int b,
    int dim) {
    if (dim <= 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (int j = 0; j < dim; ++j) {
        const double d = population[static_cast<std::size_t>(a) * dim + j] -
                         population[static_cast<std::size_t>(b) * dim + j];
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(dim));
}

double normalized_row_distance(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& population,
    int a,
    int b,
    int dim,
    DistanceMetric metric,
    InternalScalingMode mode ,
    const LensSystem* system ,
    double air_thickness_distance_cap ) {
    if (dim <= 0) {
        return 0.0;
    }
    double sum = 0.0;
    int count = 0;
    for (int j = 0; j < dim; ++j) {
        const auto& var = variables[static_cast<std::size_t>(j)];
        if (metric == DistanceMetric::StructureBoundNormalized &&
            var.kind != VariableKind::Radius &&
            var.kind != VariableKind::Curvature &&
            var.kind != VariableKind::Thickness) {
            continue;
        }
        const double ua = normalized_from_internal_for_distance(
            system,
            var,
            population[static_cast<std::size_t>(a) * dim + j],
            mode,
            air_thickness_distance_cap);
        const double ub = normalized_from_internal_for_distance(
            system,
            var,
            population[static_cast<std::size_t>(b) * dim + j],
            mode,
            air_thickness_distance_cap);
        const double d = ua - ub;
        sum += d * d;
        ++count;
    }
    return std::sqrt(sum / static_cast<double>(std::max(1, count)));
}

double population_row_distance(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& population,
    int a,
    int b,
    int dim,
    DistanceMetric metric,
    InternalScalingMode mode ,
    const LensSystem* system ,
    double air_thickness_distance_cap ) {
    if (metric == DistanceMetric::BoundNormalized ||
        metric == DistanceMetric::StructureBoundNormalized) {
        return normalized_row_distance(
            variables,
            population,
            a,
            b,
            dim,
            metric,
            mode,
            system,
            air_thickness_distance_cap);
    }
    return scaled_row_distance(population, a, b, dim);
}

double scaled_vector_distance(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& a,
    const std::vector<double>& b,
    DistanceMetric metric,
    InternalScalingMode mode ,
    const LensSystem* system ,
    double air_thickness_distance_cap ) {
    const int dim = static_cast<int>(variables.size());
    if (dim <= 0 ||
        a.size() < static_cast<std::size_t>(dim) ||
        b.size() < static_cast<std::size_t>(dim)) {
        return 0.0;
    }
    double sum = 0.0;
    int count = 0;
    for (int j = 0; j < dim; ++j) {
        const auto& var = variables[static_cast<std::size_t>(j)];
        if (metric == DistanceMetric::StructureBoundNormalized &&
            var.kind != VariableKind::Radius &&
            var.kind != VariableKind::Curvature &&
            var.kind != VariableKind::Thickness) {
            continue;
        }
        double va = a[static_cast<std::size_t>(j)];
        double vb = b[static_cast<std::size_t>(j)];
        if (metric == DistanceMetric::BoundNormalized ||
            metric == DistanceMetric::StructureBoundNormalized) {
            va = normalized_from_internal_for_distance(
                system,
                var,
                va,
                mode,
                air_thickness_distance_cap);
            vb = normalized_from_internal_for_distance(
                system,
                var,
                vb,
                mode,
                air_thickness_distance_cap);
        }
        const double d = va - vb;
        sum += d * d;
        ++count;
    }
    return std::sqrt(sum / static_cast<double>(std::max(1, count)));
}

std::vector<int> sorted_population_indices(const std::vector<double>& merits) {
    std::vector<int> order(merits.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&merits](int a, int b) {
        const double ma = merits[static_cast<std::size_t>(a)];
        const double mb = merits[static_cast<std::size_t>(b)];
        const bool fa = std::isfinite(ma);
        const bool fb = std::isfinite(mb);
        if (fa != fb) {
            return fa;
        }
        return ma < mb;
    });
    return order;
}

std::vector<int> select_diverse_indices(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& population,
    const std::vector<double>& merits,
    int dim,
    int count,
    double min_distance,
    DistanceMetric metric,
    InternalScalingMode mode ) {
    std::vector<int> selected;
    if (count <= 0 || merits.empty()) {
        return selected;
    }
    const auto order = sorted_population_indices(merits);
    selected.reserve(static_cast<std::size_t>(count));
    for (int idx : order) {
        bool far_enough = true;
        for (int chosen : selected) {
            if (population_row_distance(variables, population, idx, chosen, dim, metric, mode) <
                min_distance) {
                far_enough = false;
                break;
            }
        }
        if (far_enough) {
            selected.push_back(idx);
            if (static_cast<int>(selected.size()) >= count) {
                return selected;
            }
        }
    }
    for (int idx : order) {
        if (std::find(selected.begin(), selected.end(), idx) == selected.end()) {
            selected.push_back(idx);
            if (static_cast<int>(selected.size()) >= count) {
                break;
            }
        }
    }
    return selected;
}

}  // namespace optimizer_detail

}  // namespace optiland
