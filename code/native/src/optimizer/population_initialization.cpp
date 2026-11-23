/*
 * Create the initial DE population, inject supplied physical/flat seeds and attach final population output.
 * Standard, hybrid and adaptive setup supply variable metadata, options and the owning random stream.
 * Preserve seed insertion order, Latin-hypercube draws and physical/scaled column correspondence.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

std::vector<double> scaled_population_to_physical_flat(
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& scaled_population,
    int population_size,
    InternalScalingMode mode ) {
    const int dim = static_cast<int>(variables.size());
    std::vector<double> physical(scaled_population.size(), 0.0);
    for (int i = 0; i < population_size; ++i) {
        for (int j = 0; j < dim; ++j) {
            const auto& var = variables[static_cast<std::size_t>(j)];
            const auto [lo, hi] = internal_bounds(var, mode);
            const double scaled = maybe_clip_continuous(
                var,
                scaled_population[static_cast<std::size_t>(i) * dim + j],
                lo,
                hi);
            physical[static_cast<std::size_t>(i) * dim + j] =
                internal_to_physical(var, scaled, mode);
        }
    }
    return physical;
}

std::vector<double> initial_physical_values(const std::vector<VariableSpec>& variables) {
    std::vector<double> values;
    values.reserve(variables.size());
    for (const auto& var : variables) {
        const double initial = std::isfinite(var.initial) ? var.initial : 0.0;
        values.push_back(project_variable_value(
            var,
            maybe_clip_continuous(var, initial, var.lower, var.upper)));
    }
    return values;
}

void inject_flat_start_curvature_seeds(
    const std::vector<VariableSpec>& variables,
    std::vector<double>& population,
    int population_size,
    const std::vector<double>& initial_scaled,
    InternalScalingMode mode ) {
    const int dim = static_cast<int>(variables.size());
    if (population_size <= 1 || dim <= 0 ||
        initial_scaled.size() != static_cast<std::size_t>(dim)) {
        return;
    }

    std::vector<int> curvature_columns;
    curvature_columns.reserve(variables.size());
    bool flat_start = false;
    for (int j = 0; j < dim; ++j) {
        const auto& var = variables[static_cast<std::size_t>(j)];
        if (var.kind != VariableKind::Curvature) {
            continue;
        }
        curvature_columns.push_back(j);
        if (std::abs(var.initial) <= 1.0e-10) {
            flat_start = true;
        }
    }
    if (curvature_columns.empty() || !flat_start) {
        return;
    }

    const double amplitudes[] = {0.005, 0.01, 0.02, 0.04, 0.08, 0.12};
    int row = 1;
    for (double amplitude : amplitudes) {
        for (int phase = 0; phase < 2; ++phase) {
            if (row >= population_size) {
                return;
            }
            for (int j = 0; j < dim; ++j) {
                population[static_cast<std::size_t>(row) * dim + j] =
                    initial_scaled[static_cast<std::size_t>(j)];
            }
            for (std::size_t k = 0; k < curvature_columns.size(); ++k) {
                const int j = curvature_columns[k];
                const auto& var = variables[static_cast<std::size_t>(j)];
                const double sign = ((k + static_cast<std::size_t>(phase)) % 2 == 0) ? 1.0 : -1.0;
                const double value = std::clamp(sign * amplitude, var.lower, var.upper);
                const auto [lo, hi] = internal_bounds(var, mode);
                population[static_cast<std::size_t>(row) * dim + j] =
                    std::clamp(physical_to_internal(var, value, mode), lo, hi);
            }
            ++row;
        }
    }
}

void inject_physical_population_seeds(
    const std::vector<VariableSpec>& variables,
    std::vector<double>& population,
    int population_size,
    const std::vector<std::vector<double>>& seed_values,
    int start_row,
    InternalScalingMode mode ) {
    const int dim = static_cast<int>(variables.size());
    if (population_size <= 0 || dim <= 0 || seed_values.empty()) {
        return;
    }
    int row = std::clamp(start_row, 0, population_size);
    for (const auto& values : seed_values) {
        if (row >= population_size) {
            return;
        }
        if (values.size() != static_cast<std::size_t>(dim)) {
            continue;
        }
        const std::vector<double> scaled = physical_to_scaled(variables, values, mode);
        for (int j = 0; j < dim; ++j) {
            const auto& var = variables[static_cast<std::size_t>(j)];
            const auto [lo, hi] = internal_bounds(var, mode);
            population[static_cast<std::size_t>(row) * dim + j] =
                maybe_clip_continuous(
                    var,
                    scaled[static_cast<std::size_t>(j)],
                    lo,
                    hi);
        }
        ++row;
    }
}

std::vector<double> make_latin_hypercube_population(
    const std::vector<VariableSpec>& variables,
    int population_size,
    std::mt19937& rng,
    InternalScalingMode mode ) {
    const int dim = static_cast<int>(variables.size());
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::vector<double> population(static_cast<std::size_t>(population_size) * dim, 0.0);
    std::vector<int> order(population_size);
    std::iota(order.begin(), order.end(), 0);

    for (int j = 0; j < dim; ++j) {
        std::shuffle(order.begin(), order.end(), rng);
        const auto& var = variables[static_cast<std::size_t>(j)];
        const auto [lo, hi] = internal_bounds(var, mode);
        if (is_glass_choice_variable(var)) {
            std::uniform_int_distribution<int> choice_dist(
                glass_choice_lower(var),
                std::max(glass_choice_lower(var), glass_choice_upper(var)));
            for (int i = 0; i < population_size; ++i) {
                const int choice = choice_dist(rng);
                population[static_cast<std::size_t>(i) * dim + j] =
                    std::clamp(
                        physical_to_internal(var, static_cast<double>(choice), mode),
                        lo,
                        hi);
            }
            continue;
        }
        for (int i = 0; i < population_size; ++i) {
            const double sample = (static_cast<double>(order[static_cast<std::size_t>(i)]) + uni(rng)) /
                                  static_cast<double>(population_size);
            population[static_cast<std::size_t>(i) * dim + j] = lo + sample * (hi - lo);
        }
    }

    const auto x0 = physical_to_scaled(variables, initial_physical_values(variables), mode);
    for (int j = 0; j < dim; ++j) {
        population[static_cast<std::size_t>(j)] = x0[static_cast<std::size_t>(j)];
    }
    inject_flat_start_curvature_seeds(variables, population, population_size, x0, mode);
    return population;
}

std::vector<double> make_initial_de_population(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    int population_size,
    const PopulationGenerationOptions& generation,
    std::mt19937& rng,
    InternalScalingMode mode ) {
    if (generation.mode != PopulationGenerationMode::PhysicsConstrained) {
        return make_latin_hypercube_population(variables, population_size, rng, mode);
    }
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::vector<double> empty_population;
    std::vector<int> empty_parents;
    return make_population_generation_rows(
        system,
        variables,
        empty_population,
        static_cast<int>(variables.size()),
        empty_parents,
        population_size,
        generation,
        mode,
        rng,
        uni);
}

void attach_final_population(
    OptimizationResult& result,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& scaled_population,
    int population_size,
    const std::vector<double>& merits,
    InternalScalingMode mode ) {
    result.final_population_size = population_size;
    result.final_population_dim = static_cast<int>(variables.size());
    result.final_population_values =
        scaled_population_to_physical_flat(variables, scaled_population, population_size, mode);
    result.final_population_merits = merits;
}

std::string normalized_strategy(std::string strategy) {
    strategy.erase(
        std::remove_if(strategy.begin(), strategy.end(), [](unsigned char ch) {
            return ch == '_' || ch == '-' || std::isspace(ch);
        }),
        strategy.end());
    std::transform(strategy.begin(), strategy.end(), strategy.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (strategy == "currenttobest1bin" || strategy == "rand1bin") {
        return strategy;
    }
    return "best1bin";
}

}  // namespace optimizer_detail

}  // namespace optiland
