/*
 * Mutate a discrete glass-choice coordinate through catalog-neighbor and inheritance policy.
 * DE and adaptive trial generation provide the parent values, catalog context and existing random stream.
 * Preserve neighbor ordering, probability tests and index clamping when selecting a replacement glass.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double mutate_glass_choice_internal(
    const VariableSpec& var,
    double target_internal,
    double guide_internal,
    const DifferentialEvolutionOptions& options,
    std::mt19937& rng,
    std::uniform_real_distribution<double>& uni,
    InternalScalingMode mode ) {
    const int lo_choice = glass_choice_lower(var);
    const int hi_choice = std::max(lo_choice, glass_choice_upper(var));
    if (hi_choice <= lo_choice) {
        return physical_to_internal(var, static_cast<double>(lo_choice), mode);
    }
    std::uniform_int_distribution<int> random_choice(lo_choice, hi_choice);
    int target = clamp_glass_choice(var, internal_to_physical(var, target_internal, mode));
    const int guide = clamp_glass_choice(var, internal_to_physical(var, guide_internal, mode));

    const double random_rate = std::clamp(options.glass_random_rate, 0.0, 1.0);
    const double guide_rate = std::clamp(options.glass_pbest_inherit_rate, 0.0, 1.0);
    const double mutation_rate = std::clamp(options.glass_mutation_rate, 0.0, 1.0);
    const double roll = uni(rng);
    int choice = target;
    if (roll < random_rate) {
        choice = random_choice(rng);
    } else if (roll < random_rate + guide_rate) {
        choice = guide;
    } else if (roll < random_rate + guide_rate + mutation_rate) {
        const int span = std::max(1, hi_choice - lo_choice);
        const int radius = options.glass_local_search
                               ? std::clamp(options.glass_neighbor_count, 1, span)
                               : span;
        std::uniform_int_distribution<int> step_dist(-radius, radius);
        int step = step_dist(rng);
        if (step == 0) {
            step = uni(rng) < 0.5 ? -1 : 1;
        }
        const int center = options.glass_local_search ? target : guide;
        choice = std::clamp(center + step, lo_choice, hi_choice);
    }
    return physical_to_internal(var, static_cast<double>(choice), mode);
}

}  // namespace optimizer_detail

}  // namespace optiland
