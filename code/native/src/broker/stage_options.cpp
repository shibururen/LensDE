// Construct the retained fixed-stage adaptive DE options from one validated island job.
// Island workers consume these explicit checkpoint-stage budgets and trace acceptance settings.
// Keep the recorded seed, LPSR horizon, disabled polish/Refresh, and scalar/feasibility mode unchanged.

#include "interfaces.hpp"

namespace lensde::broker {

optiland::AdaptiveHybridDeDlsOptions checkpoint_options(const LoadedJob& loaded) {
    optiland::AdaptiveHybridDeDlsOptions options;
    options.de.population_size = loaded.job.population;
    options.de.use_popsize_multiplier = false;
    options.de.max_generations = loaded.job.generations;
    options.de.strategy = "rand1bin";
    options.de.seed = loaded.job.seed;
    options.de.use_cuda = true;
    options.de.cuda_min_batch_size = 1;
    options.de.progress_every = 200;
    options.de.initial_population_values = loaded.initial_population;
    options.de.internal_scaling = optiland::InternalScalingMode::EdgeSagNormalized;
    options.variant = optiland::AdaptiveDeVariant::JadePBestArchiveLpsrFeas;
    options.constraint_handling = optiland::ConstraintHandlingMode::ScalarMerit;
    if (loaded.job.has_trace_contract) {
        if (loaded.job.constraint_handling == "scalar_merit") {
            options.constraint_handling =
                optiland::ConstraintHandlingMode::ScalarMerit;
        } else if (loaded.job.constraint_handling == "feasibility_rules") {
            options.constraint_handling =
                optiland::ConstraintHandlingMode::FeasibilityRules;
        } else {
            throw std::invalid_argument(
                "unknown job constraint handling: " +
                loaded.job.constraint_handling);
        }
        options.allow_near_trace_infeasible =
            loaded.job.allow_near_trace_infeasible;
        options.trace_coverage_epsilon =
            loaded.job.trace_coverage_epsilon;
    }
    options.outer_epochs = 1;
    options.parent_count = 4;
    options.polish_workers = 0;
    options.parallel_polish = false;
    options.polish_enabled = false;
    options.polish.max_iterations = 0;
    options.refresh_fraction = 0.0;
    options.inject_best = false;
    options.pbest_scope = optiland::PBestGuideScope::Legacy;
    options.pbest_fraction = 0.1;
    options.de_mating_scope = optiland::DeMatingScope::Global;
    options.use_lpsr = true;
    options.allow_small_initial_population = true;
    options.minimum_population_size = 20;
    options.lpsr_horizon_generations = std::max(1000, loaded.job.generations);
    options.treat_first_order_as_feasibility = false;
    options.basins.enabled = false;
    options.topology_summary_enabled = false;
    options.write_adaptive_trace = false;
    options.internal_scaling = optiland::InternalScalingMode::EdgeSagNormalized;
    options.checkpoint_snapshot_dir = loaded.job.snapshot_dir;
    if (loaded.job.profile == "checkpoint_fixed_population_feedback50") {
        // Experimental anchor-stage profile: keep the checkpoint DE contract,
        // but preserve the caller's exact per-island population budget and
        // expose the 50-generation feedback cadence used by the anchor funnel.
        options.de.progress_every = std::max(
            1, std::gcd(loaded.job.generations, 50));
        options.use_lpsr = false;
        options.write_adaptive_trace = true;
    }
    if (loaded.job.profile == "material_repair" ||
        loaded.job.profile == "material_deepen") {
        const std::string scaling = loaded.job.internal_scaling;
        optiland::InternalScalingMode scaling_mode =
            optiland::InternalScalingMode::LegacyOptical;
        if (scaling == "edge_sag" || scaling == "edge_sag_normalized") {
            scaling_mode = optiland::InternalScalingMode::EdgeSagNormalized;
        } else if (scaling == "bound" || scaling == "bound_normalized" ||
                   scaling == "normalized") {
            scaling_mode = optiland::InternalScalingMode::BoundNormalized;
        } else if (scaling != "legacy" && scaling != "legacy_optical") {
            throw std::invalid_argument(
                "unknown material internal scaling: " + scaling);
        }
        options.de.internal_scaling = scaling_mode;
        options.internal_scaling = scaling_mode;
        const int epoch_generations = std::gcd(loaded.job.generations, 50);
        options.de.max_generations = std::max(1, epoch_generations);
        options.de.progress_every = std::max(1, epoch_generations);
        options.outer_epochs = std::max(
            1, loaded.job.generations / std::max(1, epoch_generations));
        options.refresh_fraction = 0.25;
        options.constraint_handling =
            loaded.job.profile == "material_repair"
                ? optiland::ConstraintHandlingMode::ScalarMerit
                : optiland::ConstraintHandlingMode::FeasibilityRules;
        options.allow_near_trace_infeasible = true;
        // The DE-DLS material pipeline uses one coverage contract throughout:
        // MeritConfig keeps the ideal target at 1.0 while feasibility allows
        // candidates down to 0.95.  Keep this aligned with the Python-stage
        // admission threshold instead of silently tightening it to 0.97.
        options.trace_coverage_epsilon = 0.05;
        options.lpsr_horizon_generations =
            std::max(9600, loaded.job.generations * 24);
    }
    if (loaded.job.profile == "fixed_glass_feedback" ||
        loaded.job.profile == "fixed_glass_feedback_smoke") {
        options.de.max_generations = loaded.job.generations;
        options.de.progress_every = std::max(1, loaded.job.generations);
        options.outer_epochs =
            loaded.job.profile == "fixed_glass_feedback" ? 20 : 2;
        options.parent_count = 10;
        options.polish_candidate_count = 0;
        options.polish_refill_max_extra = 0;
        options.polish_workers = 0;
        options.parallel_polish = false;
        options.batched_polish = false;
        options.polish_enabled = false;
        options.polish.max_iterations = 0;
        options.refresh_fraction = 0.25;
        options.constraint_handling =
            optiland::ConstraintHandlingMode::FeasibilityRules;
        options.allow_near_trace_infeasible = true;
        options.trace_coverage_epsilon = 0.05;
        options.treat_first_order_as_feasibility = false;
        options.lpsr_horizon_generations = 24 * loaded.job.generations;
        options.early_stop_enabled = true;
        options.early_stop_metric = "best_merit";
        options.early_stop_min_epochs = 2;
        options.early_stop_window = 1;
        options.early_stop_min_topologies = 0;
        options.early_stop_min_rel_improve = 0.01;
        options.early_stop_min_abs_improve = 0.01;
        options.early_stop_min_lpsr_progress = 0.1;
        options.early_stop_tail_epochs = 0;
        options.basins.enabled = false;
        options.topology_summary_enabled = false;
        options.write_adaptive_trace = true;
    }
    if (loaded.job.profile.rfind("glass_scout_", 0) == 0) {
        const int rounds = std::max(
            1, std::stoi(loaded.job.profile.substr(std::string("glass_scout_").size())));
        options.de.max_generations = loaded.job.generations;
        options.de.progress_every = std::max(1, loaded.job.generations);
        options.outer_epochs = rounds;
        options.parent_count = 4;
        options.polish_workers = 0;
        options.parallel_polish = false;
        options.polish_enabled = false;
        options.polish.max_iterations = 0;
        options.refresh_fraction = 0.25;
        options.constraint_handling =
            optiland::ConstraintHandlingMode::FeasibilityRules;
        options.allow_near_trace_infeasible = true;
        options.trace_coverage_epsilon = 0.05;
        options.treat_first_order_as_feasibility = false;
        options.use_lpsr = true;
        options.minimum_population_size = 20;
        options.lpsr_horizon_generations =
            std::max(9600, rounds * loaded.job.generations * 8);
        options.early_stop_enabled = false;
        options.basins.enabled = false;
        options.topology_summary_enabled = false;
        options.write_adaptive_trace = true;
    }
    if (loaded.job.profile.rfind("post_asphere_", 0) == 0) {
        // Absolute population sizes are exact, so the materialized Python
        // population is also the population evaluated by the batch broker.
        options.allow_small_initial_population = true;
        options.de.max_generations = loaded.job.generations;
        options.de.progress_every = std::max(1, loaded.job.generations);
        if (loaded.job.profile == "post_asphere_scout") {
            options.outer_epochs = 1;
        } else if (loaded.job.profile == "post_asphere_topup") {
            options.outer_epochs = 2;
        } else if (loaded.job.profile == "post_asphere_primary") {
            options.outer_epochs = 12;
        } else if (loaded.job.profile == "post_asphere_final") {
            options.outer_epochs = 7;
        } else if (loaded.job.profile == "post_asphere_smoke") {
            options.outer_epochs = 1;
        } else if (loaded.job.profile == "post_asphere_checkpoint_scout" ||
                   loaded.job.profile == "post_asphere_checkpoint_develop" ||
                   loaded.job.profile == "post_asphere_popsize_scout" ||
                   loaded.job.profile == "post_asphere_direction_scout" ||
                   loaded.job.profile == "post_asphere_concentrated" ||
                   loaded.job.profile == "post_asphere_restart_long_probe") {
            options.outer_epochs = 1;
        } else {
            throw std::invalid_argument("unknown post-asphere profile");
        }
        const bool checkpoint_redevelopment =
            loaded.job.profile == "post_asphere_checkpoint_scout" ||
            loaded.job.profile == "post_asphere_checkpoint_develop" ||
            loaded.job.profile == "post_asphere_popsize_scout" ||
            loaded.job.profile == "post_asphere_direction_scout" ||
            loaded.job.profile == "post_asphere_concentrated" ||
            loaded.job.profile == "post_asphere_restart_long_probe";
        options.de.progress_every = checkpoint_redevelopment
            ? (loaded.job.profile == "post_asphere_checkpoint_develop" ? 200 : 100)
            : std::max(1, loaded.job.generations);
        options.parent_count = checkpoint_redevelopment ? 10 : 4;
        options.polish_workers = 0;
        options.parallel_polish = false;
        options.polish_enabled = false;
        options.polish.max_iterations = 0;
        options.refresh_fraction = checkpoint_redevelopment ? 0.25 : 0.05;
        const std::string constraint_handling =
            loaded.job.has_trace_contract
                ? loaded.job.constraint_handling
                : "feasibility_rules";
        if (constraint_handling == "scalar_merit") {
            options.constraint_handling =
                optiland::ConstraintHandlingMode::ScalarMerit;
        } else if (constraint_handling == "feasibility_rules") {
            options.constraint_handling =
                optiland::ConstraintHandlingMode::FeasibilityRules;
        } else {
            throw std::invalid_argument(
                "unknown post-asphere constraint handling: " + constraint_handling);
        }
        options.require_trace_feasible = true;
        options.allow_near_trace_infeasible =
            loaded.job.has_trace_contract
                ? loaded.job.allow_near_trace_infeasible
                : true;
        options.trace_coverage_epsilon =
            loaded.job.has_trace_contract
                ? loaded.job.trace_coverage_epsilon
                : 0.03;
        options.treat_first_order_as_feasibility = false;
        options.use_lpsr = true;
        if (loaded.job.profile == "post_asphere_popsize_scout" ||
            loaded.job.profile == "post_asphere_direction_scout" ||
            loaded.job.profile == "post_asphere_concentrated" ||
            loaded.job.profile == "post_asphere_restart_long_probe") {
            options.use_lpsr = false;
        }
        options.minimum_population_size = 20;
        options.lpsr_horizon_generations = 9600;
        options.early_stop_enabled = false;
        if (loaded.job.profile == "post_asphere_concentrated" ||
            loaded.job.profile == "post_asphere_restart_long_probe") {
            constexpr int epoch_generations = 400;
            if (loaded.job.generations % epoch_generations != 0) {
                throw std::invalid_argument(
                    "long post-asphere generations must be a multiple of 400");
            }
            options.de.max_generations = epoch_generations;
            options.de.progress_every = epoch_generations;
            options.outer_epochs = loaded.job.generations / epoch_generations;
            if (loaded.job.profile == "post_asphere_concentrated") {
                options.early_stop_enabled = true;
                options.early_stop_metric = "best_merit";
                options.early_stop_min_epochs = 3;
                options.early_stop_window = 1;
                options.early_stop_min_topologies = 0;
                options.early_stop_min_rel_improve = 0.01;
                options.early_stop_min_abs_improve = 0.01;
                options.early_stop_min_lpsr_progress = 0.0;
                options.early_stop_tail_epochs = 0;
            }
        }
        options.basins.enabled = false;
        options.topology_summary_enabled = false;
        options.write_adaptive_trace = true;
    }
    return options;
}

} // namespace lensde::broker
