/*
 * Generate adaptive DE trial rows from the current population, guides and mutation controls.
 * The generation controller supplies temporary pools/context while run_ owns RNG and distribution state.
 * Preserve row traversal, random-draw order and archive/guide sampling before batch evaluation.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::generate_trial_population(const GenerateTrialPopulationContext& frame_) {
    auto& epoch_pbest_stats = frame_.epoch_pbest_stats;
    auto& epoch_mating_stats = frame_.epoch_mating_stats;
    auto& order = frame_.order;
    auto& pbest_count = frame_.pbest_count;
    auto& geometry_memory_novelty_guide_index = frame_.geometry_memory_novelty_guide_index;
    auto& geometry_memory_novelty_guide_similarity = frame_.geometry_memory_novelty_guide_similarity;
    auto& geometry_memory_novelty_escape_active = frame_.geometry_memory_novelty_escape_active;
    auto& generation_best_scaled = frame_.generation_best_scaled;
    auto& local_mating_pools = frame_.local_mating_pools;
    auto& active_mating_quota = frame_.active_mating_quota;
    auto& trials = frame_.trials;
    auto& sampled_f = frame_.sampled_f;
    auto& sampled_cr = frame_.sampled_cr;
    auto& geometry_memory_qohr_step = frame_.geometry_memory_qohr_step;
    
    

for (int i = 0; i < run_.pop_size; ++i) {
                
                
                
                const auto mating_plan_start = run_.detailed_trial_timing
                    ? Clock::now()
                    : Clock::time_point{};
                const DeMatingPlan mating_plan = this->make_de_mating_plan(
                    i,
                    local_mating_pools,
                    active_mating_quota,
                    epoch_mating_stats);
                if (run_.detailed_trial_timing) {
                    run_.timing.trial_mating_plan_seconds +=
                        seconds_since(mating_plan_start);
                }
                const auto pbest_start = run_.detailed_trial_timing
                    ? Clock::now()
                    : Clock::time_point{};
                PBestGuideSelection pbest_guide = this->no_pbest_guide();
                if (run_.variant_uses_jade) {
                    const bool force_active_local_pbest =
                        options.de_mating_scope == DeMatingScope::ActiveBasinOnly &&
                        mating_plan.request_local;
                    if ((options.de_mating_strict_local || force_active_local_pbest) &&
                        mating_plan.request_local) {
                        pbest_guide = this->select_local_population_pbest_guide(
                            i,
                            order,
                            local_mating_pools,
                            mating_plan.local_scope);
                    }
                    if (!pbest_guide.has_guide && !force_active_local_pbest) {
                        pbest_guide = this->select_pbest_guide(i, order, pbest_count);
                    }
                    pbest_guide = this->apply_geometry_memory_pbest_soft(
                        pbest_guide,
                        order,
                        pbest_count,
                        epoch_pbest_stats);
                    if (geometry_memory_novelty_escape_active &&
                        geometry_memory_novelty_guide_index >= 0 &&
                        geometry_memory_novelty_guide_index != i &&
                        !this->active_slot_uses_strict_frozen_family(i)) {
                        const std::string target_key =
                            this->fitness_diversity_key(
                                run_.fitness[static_cast<std::size_t>(i)]);
                        const bool target_escape_matched =
                            (run_.geometry_memory_family_escape_active &&
                             options
                                 .geometry_memory_plateau_novelty_guide_all_targets) ||
                            (run_.geometry_memory_exact_escape_active &&
                             this->geometry_memory_key_is_exact(target_key)) ||
                            (run_.geometry_memory_family_escape_active &&
                             this->geometry_memory_key_is_family_matched(target_key));
                        if (target_escape_matched) {
                            ++epoch_pbest_stats.geometry_memory_novelty_pbest_checked;
                            const double target_similarity =
                                this->geometry_memory_key_max_similarity(target_key);
                            if (geometry_memory_novelty_guide_similarity + 1.0e-12 <
                                target_similarity) {
                                pbest_guide = this->population_pbest_guide(
                                    geometry_memory_novelty_guide_index,
                                    PBestGuideSource::GlobalPopulation);
                                ++epoch_pbest_stats.geometry_memory_novelty_pbest_redirect;
                            }
                        }
                    }
                    this->record_pbest_guide(epoch_pbest_stats, i, pbest_guide);
                }
                if (run_.detailed_trial_timing) {
                    run_.timing.trial_pbest_seconds += seconds_since(pbest_start);
                }
                const auto mating_select_start = run_.detailed_trial_timing
                    ? Clock::now()
                    : Clock::time_point{};
                const DeMatingSelection mating =
                    this->select_de_mating(
                        i,
                        pbest_guide,
                        local_mating_pools,
                        mating_plan,
                        epoch_mating_stats);
                if (run_.detailed_trial_timing) {
                    run_.timing.trial_mating_select_seconds +=
                        seconds_since(mating_select_start);
                }
                if (this->active_slot_uses_strict_frozen_family(i) &&
                    mating.r0 < 0) {
                    for (int j = 0; j < run_.dim; ++j) {
                        trials[static_cast<std::size_t>(i) * run_.dim + j] =
                            run_.population[static_cast<std::size_t>(i) * run_.dim + j];
                    }
                    continue;
                }
                const int r0 = mating.r0;
                const int r1 = mating.r1;
                const int r2_pool = mating.r2_pool;

                const bool adaptive_sampling =
                    run_.variant_uses_jade || options.variant == AdaptiveDeVariant::JdeRand1Bin;
                double f = adaptive_sampling ? this->sample_f() : this->sample_classic_f();
                double cr =
                    adaptive_sampling
                        ? std::clamp(
                              run_.mu_cr + std::max(1.0e-9, options.cr_normal_sigma) * run_.normal(run_.rng),
                              0.0,
                              1.0)
                        : std::clamp(run_.de_options.recombination, 0.0, 1.0);
                if (run_.geometry_memory_family_escape_active &&
                    options.geometry_memory_plateau_parameter_reheat) {
                    f = std::max(
                        f,
                        std::clamp(
                            options.geometry_memory_plateau_reheat_f_floor,
                            0.0,
                            1.0));
                    cr = std::max(
                        cr,
                        std::clamp(
                            options.geometry_memory_plateau_reheat_cr_floor,
                            0.0,
                            1.0));
                }
                sampled_f[static_cast<std::size_t>(i)] = f;
                sampled_cr[static_cast<std::size_t>(i)] = cr;
                const int j_rand = std::min(run_.dim - 1, static_cast<int>(run_.uni(run_.rng) * run_.dim));
                int geometry_memory_difference_index =
                    this->select_geometry_memory_difference_index(
                        i,
                        pbest_guide,
                        epoch_pbest_stats);
                double geometry_memory_difference_scale = 1.0;
                double geometry_memory_difference_orientation = 1.0;
                if (geometry_memory_difference_index >= 0 &&
                    options.geometry_memory_difference_strength > 0.0) {
                    double history_squared_sum = 0.0;
                    double de_squared_sum = 0.0;
                    double outward_dot = 0.0;
                    int geometry_dimension_count = 0;
                    for (int j = 0; j < run_.dim; ++j) {
                        const auto& var = owner.variables_[static_cast<std::size_t>(j)];
                        const bool geometry_variable =
                            var.kind == VariableKind::Radius ||
                            var.kind == VariableKind::Curvature ||
                            var.kind == VariableKind::Thickness ||
                            var.kind == VariableKind::Conic ||
                            var.kind == VariableKind::AsphereCoeff;
                        if (!geometry_variable || is_glass_choice_variable(var)) {
                            continue;
                        }
                        const double target = run_.population[
                            static_cast<std::size_t>(i) * run_.dim + j];
                        const double history_anchor =
                            options.geometry_memory_difference_random_population_anchor
                                ? run_.population[static_cast<std::size_t>(r0) * run_.dim + j]
                                : target;
                        const double memory = run_.geometry_memory_scaled[
                            static_cast<std::size_t>(geometry_memory_difference_index)]
                            [static_cast<std::size_t>(j)];
                        const double x_r1 = run_.population[
                            static_cast<std::size_t>(r1) * run_.dim + j];
                        double x_r2 = 0.0;
                        if (run_.variant_uses_archive && r2_pool >= run_.pop_size &&
                            !run_.external_archive.empty()) {
                            const int archive_index = r2_pool - run_.pop_size;
                            x_r2 = run_.external_archive[
                                static_cast<std::size_t>(archive_index)]
                                [static_cast<std::size_t>(j)];
                        } else {
                            const int r2 = std::clamp(r2_pool, 0, run_.pop_size - 1);
                            x_r2 = run_.population[
                                static_cast<std::size_t>(r2) * run_.dim + j];
                        }
                        const double history_delta = history_anchor - memory;
                        const double radial_delta = target - memory;
                        const double de_delta = x_r1 - x_r2;
                        history_squared_sum += history_delta * history_delta;
                        de_squared_sum += de_delta * de_delta;
                        outward_dot += history_delta * radial_delta;
                        ++geometry_dimension_count;
                    }
                    if (geometry_dimension_count <= 0 ||
                        !(history_squared_sum > 1.0e-24)) {
                        geometry_memory_difference_index = -1;
                    } else {
                        const double history_rms = std::sqrt(
                            history_squared_sum /
                            static_cast<double>(geometry_dimension_count));
                        const double de_rms = std::sqrt(
                            de_squared_sum /
                            static_cast<double>(geometry_dimension_count));
                        geometry_memory_difference_scale = std::min(
                            1.0,
                            de_rms / std::max(history_rms, 1.0e-12));
                        if (options.geometry_memory_difference_orient_outward &&
                            outward_dot < 0.0) {
                            geometry_memory_difference_orientation = -1.0;
                            ++epoch_pbest_stats.geometry_memory_difference_outward_flip;
                        }
                        ++epoch_pbest_stats.geometry_memory_difference_applied;
                        if (geometry_memory_difference_scale < 1.0 - 1.0e-12) {
                            ++epoch_pbest_stats.geometry_memory_difference_capped;
                        }
                    }
                } else {
                    geometry_memory_difference_index = -1;
                }
                const bool geometry_memory_qohr_applied =
                    this->build_geometry_memory_qohr_step(
                        i,
                        pbest_guide,
                        r0,
                        r1,
                        r2_pool,
                        f,
                        geometry_memory_qohr_step,
                        epoch_pbest_stats);
                for (int j = 0; j < run_.dim; ++j) {
                    const auto& var = owner.variables_[static_cast<std::size_t>(j)];
                    const auto [lo, hi] = internal_bounds(var, run_.internal_mode);
                    const double target = run_.population[static_cast<std::size_t>(i) * run_.dim + j];
                    double value = target;
                    if (is_glass_choice_variable(var)) {
                        if (run_.uni(run_.rng) < cr || j == j_rand) {
                            const double guide =
                                (run_.variant_uses_jade && pbest_guide.has_guide)
                                    ? this->pbest_guide_value(pbest_guide, j)
                                    : ((run_.variant_uses_jade && r0 >= 0 && r0 < run_.pop_size)
                                           ? run_.population[static_cast<std::size_t>(r0) * run_.dim + j]
                                           : generation_best_scaled[static_cast<std::size_t>(j)]);
                            value = mutate_glass_choice_internal(
                                var,
                                target,
                                guide,
                                run_.de_options,
                                run_.rng,
                                run_.uni,
                                run_.internal_mode);
                        } else {
                            value = physical_to_internal(
                                var,
                                internal_to_physical(var, target, run_.internal_mode),
                                run_.internal_mode);
                        }
                        trials[static_cast<std::size_t>(i) * run_.dim + j] =
                            std::clamp(value, lo, hi);
                        continue;
                    }
                    if (run_.uni(run_.rng) < cr || j == j_rand) {
                        const double x_best =
                            generation_best_scaled[static_cast<std::size_t>(j)];
                        const double x_pbest = pbest_guide.has_guide
                                                   ? this->pbest_guide_value(pbest_guide, j)
                                                   : target;
                        const double x_r0 =
                            run_.population[static_cast<std::size_t>(r0) * run_.dim + j];
                        const double x_r1 =
                            run_.population[static_cast<std::size_t>(r1) * run_.dim + j];
                        double x_r2 = 0.0;
                        if (run_.variant_uses_archive && r2_pool >= run_.pop_size && !run_.external_archive.empty()) {
                            const int archive_index = r2_pool - run_.pop_size;
                            x_r2 = run_.external_archive[static_cast<std::size_t>(archive_index)]
                                                  [static_cast<std::size_t>(j)];
                        } else {
                            const int r2 = std::clamp(r2_pool, 0, run_.pop_size - 1);
                            x_r2 = run_.population[static_cast<std::size_t>(r2) * run_.dim + j];
                        }
                        switch (options.variant) {
                            case AdaptiveDeVariant::ClassicBest1Bin:
                                value = x_best + f * (x_r1 - x_r2);
                                break;
                            case AdaptiveDeVariant::ClassicRand1Bin:
                            case AdaptiveDeVariant::JdeRand1Bin:
                                value = x_r0 + f * (x_r1 - x_r2);
                                break;
                            case AdaptiveDeVariant::ClassicCurrentToBest1Bin:
                                value = target + f * (x_best - target) + f * (x_r1 - x_r2);
                                break;
                            case AdaptiveDeVariant::JadePBestArchive:
                            case AdaptiveDeVariant::JadePBestArchiveLpsr:
                            case AdaptiveDeVariant::JadePBestArchiveLpsrFeas:
                                if (pbest_guide.has_guide) {
                                    value = target + f * (x_pbest - target) + f * (x_r1 - x_r2);
                                } else {
                                    value = target + f * (x_r0 - target) + f * (x_r1 - x_r2);
                                }
                                break;
                                value = target;
                                break;
                        }
                        const bool geometry_variable =
                            var.kind == VariableKind::Radius ||
                            var.kind == VariableKind::Curvature ||
                            var.kind == VariableKind::Thickness ||
                            var.kind == VariableKind::Conic ||
                            var.kind == VariableKind::AsphereCoeff;
                        if (geometry_memory_qohr_applied &&
                            geometry_variable) {
                            value = target +
                                geometry_memory_qohr_step[
                                    static_cast<std::size_t>(j)] *
                                run_.geometry_memory_qohr_ranges[
                                    static_cast<std::size_t>(j)];
                        } else if (geometry_memory_difference_index >= 0 &&
                            geometry_variable) {
                            const double memory = run_.geometry_memory_scaled[
                                static_cast<std::size_t>(geometry_memory_difference_index)]
                                [static_cast<std::size_t>(j)];
                            const double history_anchor =
                                options.geometry_memory_difference_random_population_anchor
                                    ? run_.population[
                                          static_cast<std::size_t>(r0) * run_.dim + j]
                                    : target;
                            value += options.geometry_memory_difference_strength * f *
                                geometry_memory_difference_scale *
                                geometry_memory_difference_orientation *
                                (history_anchor - memory);
                        }
                    }
                    if (!std::isfinite(value)) {
                        value = target;
                    } else if (continuous_value_requires_repair(var, value, lo, hi)) {
                        value = lo + run_.uni(run_.rng) * (hi - lo);
                    }
                    trials[static_cast<std::size_t>(i) * run_.dim + j] = value;
                }
                if (run_.geometry_memory_family_escape_active &&
                    options.geometry_memory_plateau_geometry_kick &&
                    run_.uni(run_.rng) < std::clamp(
                        options.geometry_memory_plateau_geometry_kick_rate,
                        0.0,
                        1.0)) {
                    std::vector<int> geometry_dimensions;
                    geometry_dimensions.reserve(static_cast<std::size_t>(run_.dim));
                    for (int j = 0; j < run_.dim; ++j) {
                        const auto& var = owner.variables_[static_cast<std::size_t>(j)];
                        const bool geometry_variable =
                            var.kind == VariableKind::Radius ||
                            var.kind == VariableKind::Curvature ||
                            var.kind == VariableKind::Thickness ||
                            var.kind == VariableKind::Conic ||
                            var.kind == VariableKind::AsphereCoeff;
                        if (geometry_variable && !is_glass_choice_variable(var)) {
                            geometry_dimensions.push_back(j);
                        }
                    }
                    if (!geometry_dimensions.empty()) {
                        const int selected = std::min(
                            static_cast<int>(geometry_dimensions.size()) - 1,
                            static_cast<int>(
                                run_.uni(run_.rng) *
                                static_cast<double>(geometry_dimensions.size())));
                        const int j = geometry_dimensions[static_cast<std::size_t>(selected)];
                        const auto& var = owner.variables_[static_cast<std::size_t>(j)];
                        const auto [lo, hi] = internal_bounds(var, run_.internal_mode);
                        const std::size_t offset =
                            static_cast<std::size_t>(i) * run_.dim +
                            static_cast<std::size_t>(j);
                        double value = trials[offset] +
                            run_.normal(run_.rng) *
                                std::max(
                                    0.0,
                                    options.geometry_memory_plateau_geometry_kick_scale) *
                                (hi - lo);
                        if (!std::isfinite(value)) {
                            value = trials[offset];
                        } else if (continuous_value_requires_repair(var, value, lo, hi)) {
                            value = lo + run_.uni(run_.rng) * (hi - lo);
                        }
                        trials[offset] = value;
                    }
                }
            }
}

} } // namespace optiland::optimizer_detail
