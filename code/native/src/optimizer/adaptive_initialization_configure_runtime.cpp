/*
 * Initialize adaptive dimensions, options, avoidance, scored population, basin state and search dynamics.
 * AdaptiveSession::execute invokes these phases in order on the newly owned run_ and solver inputs.
 * Preserve seed setup, population generation/evaluation and archive initialization order.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::configure_runtime() {

    run_.run_start = Clock::now();
    run_.dim = static_cast<int>(owner.variables_.size());
    if (run_.dim == 0) {
        throw std::invalid_argument("Adaptive Hybrid DE-DLS requires at least one variable");
    }

    run_.sizing_dim = population_sizing_dimension(owner.variables_);
    run_.internal_mode = options.internal_scaling;
    run_.de_options = options.de;
    run_.de_options.internal_scaling = run_.internal_mode;
    run_.polish_options = options.polish;
    run_.polish_options.internal_scaling = run_.internal_mode;

    run_.configured_multiplier = std::max(
        1,
        run_.de_options.use_popsize_multiplier
            ? run_.de_options.popsize_multiplier
            : options.initial_popsize_multiplier);
    run_.initial_pop_size = std::max(
        5,
        run_.de_options.use_popsize_multiplier
            ? run_.configured_multiplier * run_.sizing_dim
            : run_.de_options.population_size);
    run_.min_pop_size = std::min(
        run_.initial_pop_size,
        std::max({5, options.minimum_population_size,
                  std::max(1, options.minimum_popsize_multiplier) * run_.sizing_dim}));
    run_.outer_epochs = std::max(1, options.outer_epochs);
    run_.inner_generations = std::max(1, run_.de_options.max_generations);
    run_.max_total_generations = run_.outer_epochs * run_.inner_generations;
    run_.requested_polish_count = options.polish_enabled
            ? std::max(
                  0,
                  options.polish_candidate_count > 0
                      ? options.polish_candidate_count
                      : options.parent_count)
            : 0;
    run_.polish_refill_max_extra = run_.requested_polish_count > 0
            ? std::max(
                  0,
                  options.polish_refill_max_extra >= 0
                      ? options.polish_refill_max_extra
                      : run_.requested_polish_count)
            : 0;
    run_.max_polish_attempts = run_.requested_polish_count > 0
            ? run_.requested_polish_count + run_.polish_refill_max_extra
            : 0;
    
    
    
    run_.variant_uses_jade = options.variant == AdaptiveDeVariant::JadePBestArchive ||
        options.variant == AdaptiveDeVariant::JadePBestArchiveLpsr ||
        options.variant == AdaptiveDeVariant::JadePBestArchiveLpsrFeas;
    run_.variant_uses_de = options.variant == AdaptiveDeVariant::ClassicBest1Bin ||
        options.variant == AdaptiveDeVariant::ClassicRand1Bin ||
        options.variant == AdaptiveDeVariant::ClassicCurrentToBest1Bin ||
        options.variant == AdaptiveDeVariant::JdeRand1Bin ||
        run_.variant_uses_jade;
    run_.variant_uses_archive = run_.variant_uses_jade && options.archive_size_ratio > 0.0;
    run_.variant_uses_lpsr = options.use_lpsr &&
        (options.variant == AdaptiveDeVariant::JadePBestArchiveLpsr ||
         options.variant == AdaptiveDeVariant::JadePBestArchiveLpsrFeas);
    run_.scalar_merit_fast_path = options.constraint_handling == ConstraintHandlingMode::ScalarMerit &&
        !unified_trace_gate_enabled(owner.merit_config_);
    run_.cuda_score_summary_fast_path = run_.de_options.use_cuda && unified_trace_gate_enabled(owner.merit_config_);
    run_.classic_f_min = std::min(run_.de_options.mutation_min, run_.de_options.mutation_max);
    run_.classic_f_max = std::max(run_.de_options.mutation_min, run_.de_options.mutation_max);
    run_.default_gene_mutation_rate = std::clamp(1.0 / static_cast<double>(std::max(1, run_.dim)), 0.02, 0.25);
    run_.effective_ga_tournament_size = std::max(1, (3));
    run_.effective_ga_crossover_rate = std::clamp(std::isfinite((0.90))
                       ? (0.90)
                       : run_.de_options.recombination,
                   0.0,
                   1.0);
    run_.effective_ga_mutation_rate = std::clamp((0.10) >= 0.0
                       ? (0.10)
                       : run_.default_gene_mutation_rate,
                   0.0,
                   1.0);
    run_.effective_ga_mutation_scale_start = std::max(0.0, finite_or((0.20), 0.20));
    run_.effective_ga_mutation_scale_end = std::max(0.0, finite_or((0.02), 0.02));
    run_.effective_pso_inertia = finite_or((0.50), 0.50);
    run_.effective_pso_cognitive = finite_or((1.50), 1.50);
    run_.effective_pso_social = finite_or((1.50), 1.50);
    run_.effective_pso_initial_velocity_fraction = std::max(0.0, finite_or((0.10), 0.10));
    run_.effective_pso_max_velocity_fraction = std::max(0.0, finite_or((0.25), 0.25));
    run_.effective_pso_boundary_damping = finite_or((-0.50), -0.50);
    run_.effective_sa_step_scale_start = std::max(0.0, finite_or((0.20), 0.20));
    run_.effective_sa_step_scale_end = std::max(0.0, finite_or((0.02), 0.02));
    run_.effective_sa_temperature_start = std::max(0.0, finite_or((0.20), 0.20));
    run_.effective_sa_temperature_end = std::max(0.0, finite_or((1.0e-6), 1.0e-6));
    run_.effective_sa_temperature_power = std::max(0.0, finite_or((2.0), 2.0));
    run_.effective_sa_glass_mutation_rate = std::clamp((-1.0) >= 0.0
                       ? (-1.0)
                       : std::max(run_.default_gene_mutation_rate, 0.10),
                   0.0,
                   1.0);

    run_.timing = optiland::DeTimingStats{};
    run_.detailed_trial_timing_env = std::getenv("OPTILAND_DE_MATING_TIMING");
    run_.detailed_trial_timing = run_.detailed_trial_timing_env != nullptr &&
        std::string(run_.detailed_trial_timing_env) != "0";
    run_.phase_start = Clock::now();
    run_.descriptor_enabled = topology_descriptor_enabled(options.basins.topology_descriptor.mode);
    run_.descriptor_select_enabled = topology_descriptor_selects(options.basins.topology_descriptor.mode);
    run_.descriptor_full_enabled = topology_descriptor_full(options.basins.topology_descriptor.mode);
    run_.topology_similarity_enabled = topology_similarity_mode(options.basins.topology_descriptor.mode);
    run_.topology_family_similarity_enabled = topology_family_similarity_mode(options.basins.topology_descriptor.mode);
    run_.rng = std::mt19937(run_.de_options.seed);
    run_.uni = std::uniform_real_distribution<double>(0.0, 1.0);
    run_.normal = std::normal_distribution<double>(0.0, 1.0);
    run_.used_cuda = false;
    run_.population_cuda_locked = false;

    run_.pop_size = run_.initial_pop_size;
    run_.population = make_initial_de_population(owner.base_, owner.variables_, run_.pop_size, run_.de_options.initial_generation, run_.rng, run_.internal_mode);
    run_.initial_values = initial_physical_values(owner.variables_);
    run_.initial_scaled = physical_to_scaled(owner.variables_, run_.initial_values, run_.internal_mode);
    set_population_row(run_.population, 0, run_.initial_scaled, owner.variables_, run_.internal_mode);
    inject_physical_population_seeds(
        owner.variables_,
        run_.population,
        run_.pop_size,
        run_.de_options.initial_population_values,
        1,
        run_.internal_mode);
    run_.timing.setup_seconds += seconds_since(run_.phase_start);

    run_.geometry_memory_enabled = (options.geometry_memory_pbest_soft ||
         options.geometry_memory_difference_mutation ||
         options.geometry_memory_qohr_mutation) &&
        !options.geometry_memory_values.empty() &&
        options.geometry_memory_full_radius > 0.0 &&
        options.geometry_memory_skeleton_radius > 0.0;
    if (options.geometry_memory_qohr_mutation &&
        options.geometry_memory_difference_mutation) {
        throw std::invalid_argument(
            "geometry-memory QOHR and history-difference mutation are mutually exclusive");
    }
    if (options.geometry_memory_qohr_mutation &&
        options.geometry_memory_merits.size() !=
            options.geometry_memory_values.size()) {
        throw std::invalid_argument(
            "geometry-memory QOHR requires one aligned merit per history row");
    }
    run_.geometry_memory_qohr_reference_merit = std::numeric_limits<double>::infinity();
    if (options.geometry_memory_qohr_mutation) {
        std::vector<double> valid_memory_merits;
        valid_memory_merits.reserve(options.geometry_memory_merits.size());
        for (double merit : options.geometry_memory_merits) {
            if (std::isfinite(merit) && merit > 0.0) {
                valid_memory_merits.push_back(merit);
            }
        }
        if (valid_memory_merits.empty()) {
            throw std::invalid_argument(
                "geometry-memory QOHR merits must contain a finite positive value");
        }
        std::sort(valid_memory_merits.begin(), valid_memory_merits.end());
        run_.geometry_memory_qohr_reference_merit = valid_memory_merits.front();
    }
    run_.geometry_memory_scaled = std::vector<std::vector<double> >{};
    run_.geometry_memory_parsed_keys = std::vector<ParsedStructuralTopologyKey>{};
    if (run_.geometry_memory_enabled) {
        run_.geometry_memory_scaled.reserve(options.geometry_memory_values.size());
        for (const auto& physical : options.geometry_memory_values) {
            if (physical.size() != static_cast<std::size_t>(run_.dim)) {
                throw std::invalid_argument(
                    "geometry memory row dimension does not match optimizer variables");
            }
            run_.geometry_memory_scaled.push_back(
                physical_to_scaled(owner.variables_, physical, run_.internal_mode));
        }
        if (((options.geometry_memory_difference_key_trigger &&
              options.geometry_memory_difference_key_similarity_threshold <
                  1.0 - 1.0e-12) ||
             (options.geometry_memory_qohr_mutation &&
              options.geometry_memory_qohr_family_weight > 0.0)) &&
            options.geometry_memory_topology_keys.size() ==
                run_.geometry_memory_scaled.size()) {
            run_.geometry_memory_parsed_keys.reserve(
                options.geometry_memory_topology_keys.size());
            for (const std::string& key : options.geometry_memory_topology_keys) {
                run_.geometry_memory_parsed_keys.push_back(
                    parse_structural_topology_key(key));
            }
        }
    }
    
    
    

    
}

void AdaptiveSession::initialize_topology_avoidance() {

    
    
    
    
    
    
    
    run_.avoided_topology_match_cache = std::unordered_map<std::string, bool>{};
    run_.avoided_continuous_match_cache = std::unordered_map<std::string, ContinuousAvoidMatch>{};
    run_.avoided_topology_key_count = options.avoided_topology_keys.size() +
        options.avoided_topology_exact_only_keys.size();
    run_.avoided_topology_match_cache.reserve(
        std::max<std::size_t>(1024, run_.avoided_topology_key_count * 8));
    run_.avoided_continuous_match_cache.reserve(
        std::max<std::size_t>(1024, run_.avoided_topology_key_count * 8));
    
    
    
    run_.avoided_topology_key_set = std::unordered_set<std::string >{};
    run_.avoided_topology_key_set.reserve(run_.avoided_topology_key_count * 2);
    run_.avoided_structural_topology_keys = std::vector<ParsedStructuralTopologyKey>{};
    run_.avoided_structural_topology_keys.reserve(options.avoided_topology_keys.size());
    run_.avoided_continuous_structure_signatures = std::vector<ContinuousStructureSignature>{};
    run_.avoided_continuous_structure_signatures.reserve(
        options.avoided_topology_keys.size());
    for (const std::string& avoided : options.avoided_topology_keys) {
        if (avoided.empty()) {
            continue;
        }
        run_.avoided_topology_key_set.insert(avoided);
        if (this->structural_key_like(avoided)) {
            run_.avoided_structural_topology_keys.push_back(
                parse_structural_topology_key(avoided));
        }
        if (options.avoided_topology_continuous_structure) {
            ContinuousStructureSignature signature;
            if (parse_continuous_structure_signature(avoided, &signature)) {
                run_.avoided_continuous_structure_signatures.push_back(
                    std::move(signature));
            }
        }
    }
    for (const std::string& avoided : options.avoided_topology_exact_only_keys) {
        if (!avoided.empty()) {
            run_.avoided_topology_key_set.insert(avoided);
        }
    }
    
    
    
    
    

    
}

void AdaptiveSession::initialize_population_evaluation() {


    

    

    

    run_.evaluation_certificate_mode = evaluation_pruning_mode();
    run_.evaluation_certificate_allowed = (!owner.population_evaluation_broker_ || owner.population_evaluation_broker_->supports_evaluation_certificates()) &&
        run_.evaluation_certificate_mode > 0 && evaluation_diagnostics_safe() &&
        options.constraint_handling == ConstraintHandlingMode::FeasibilityRules &&
        run_.variant_uses_jade && run_.cuda_score_summary_fast_path &&
        !run_.scalar_merit_fast_path && options.require_trace_feasible &&
        options.hard_reject_structural_failures &&
        !options.geometry_memory_family_escape_novelty_acceptance &&
        !options.family_transition_audit &&
        std::isfinite(options.trace_coverage_epsilon) && options.trace_coverage_epsilon >= 0.0;
    run_.evaluation_batch_certificates = std::vector<optiland::EvaluationCertificate>{};
    

    run_.fitness = this->evaluate_population_fitness(run_.population, run_.pop_size, 0, "initial");
    run_.fitness[0] = this->evaluate_fitness_for_scaled(run_.initial_scaled, 0, 0, "initial", false);

    run_.joint_constraint_anchors = std::vector<JointConstraintAnchorState>(
        static_cast<std::size_t>(run_.pop_size));
    if (options.joint_constraint_anchor_guard_enabled) {
        for (int i = 0; i < run_.pop_size; ++i) {
            latch_or_improve_joint_constraint_anchor(
                run_.joint_constraint_anchors[static_cast<std::size_t>(i)],
                run_.fitness[static_cast<std::size_t>(i)],
                options);
        }
    }

    run_.merits = std::vector<double>(run_.pop_size, owner.merit_config_.fail_merit);
    
    this->sync_merits();
    if (options.family_transition_audit) {
        for (int i = 0; i < run_.pop_size; ++i) {
            const CandidateFitness& item = run_.fitness[static_cast<std::size_t>(i)];
            FamilyTransitionAuditEvent initial_audit;
            initial_audit.event = "initial_population_member";
            initial_audit.epoch = 0;
            initial_audit.generation = 0;
            initial_audit.population_index = i;
            initial_audit.trial_topology_key = item.topology_key;
            initial_audit.trial_semantic_key = item.semantic_key;
            initial_audit.trial_descriptor_key = item.descriptor_key;
            initial_audit.trial_merit = item.score.merit;
            initial_audit.trial_feasible = item.feasible;
            initial_audit.trial_trace_feasible = item.trace_feasible;
            options.family_transition_audit(initial_audit);
        }
    }

    
}

void AdaptiveSession::initialize_basin_archive() {
run_.basins = std::vector<optiland::BasinElite>{};
    run_.next_basin_id = 1;
    run_.next_similarity_family_id = 1;
    run_.basin_uses_distance_embedding = options.basins.distance_metric == DistanceMetric::BoundNormalized ||
        options.basins.distance_metric == DistanceMetric::StructureBoundNormalized;
    run_.basin_distance_structure_only = options.basins.distance_metric == DistanceMetric::StructureBoundNormalized;
    run_.basin_distance_columns = std::vector<int>{};
    run_.basin_distance_columns.reserve(static_cast<std::size_t>(run_.dim));
    for (int j = 0; j < run_.dim; ++j) {
        const auto& var = owner.variables_[static_cast<std::size_t>(j)];
        if (run_.basin_distance_structure_only &&
            var.kind != VariableKind::Radius &&
            var.kind != VariableKind::Curvature &&
            var.kind != VariableKind::Thickness &&
            !(options.basins.distance_include_conic &&
              var.kind == VariableKind::Conic)) {
            continue;
        }
        run_.basin_distance_columns.push_back(j);
    }
    
    
    
    
    
    
    
    
    
    
    
    run_.topology_similarity_cache = std::unordered_map<long long unsigned int, CachedTopologySimilarity>{};
    run_.topology_shape_diff_cache = std::unordered_map<long long unsigned int, int>{};
    run_.parsed_topology_similarity_cache = std::unordered_map<std::string, CachedParsedTopologyKey>{};
    run_.next_parsed_topology_key_id = 0;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

    for (int i = 0; i < run_.pop_size; ++i) {
        this->consider_basin(population_row(run_.population, i, run_.dim), run_.fitness[static_cast<std::size_t>(i)],
                       "initial", false);
    }

    
    
}

void AdaptiveSession::initialize_basin_lifecycle() {
run_.best_idx = this->best_index();
    run_.best_scaled = population_row(run_.population, run_.best_idx, run_.dim);
    run_.best_fitness = run_.fitness[static_cast<std::size_t>(run_.best_idx)];

    
    
    
    

    

    run_.active_basin_slots = std::vector<int>{};
    run_.active_basin_slot_origin_keys = std::vector<std::string >{};
    run_.active_basin_slot_family_keys = std::vector<std::string >{};
    run_.population_niche_id = std::vector<int>{};
    run_.active_niche_family_keys = std::vector<std::string >{};
    run_.active_focus_family_keys = std::vector<std::string >{};
    run_.active_niche_membership_reset_pending = true;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    run_.active_basin_lifecycle = std::unordered_map<std::string, ActiveBasinLifecycleState>{};
    run_.active_focus_lifecycle = std::unordered_map<std::string, ActiveFocusLifecycleState>{};
    run_.active_focus_rotation_history = std::vector<ActiveFocusRotationEntry>{};
    run_.active_lifecycle_mating_stalled_cache = std::unordered_map<std::string, bool>{};
    run_.active_lifecycle_mating_suppressed_cache = std::unordered_map<std::string, bool>{};
    run_.current_outer_epoch = 0;
    run_.last_active_plateau_new_cooldown_count = 0;
    run_.last_active_plateau_retired_family_count = 0;
    run_.last_active_plateau_active_niche_tracked_count = 0;
    run_.last_active_plateau_evict_count = 0;
    run_.last_active_plateau_evict_candidate_count = 0;
    run_.last_active_plateau_evict_cap_count = 0;
    run_.last_active_plateau_evict_distance_min = std::numeric_limits<double>::quiet_NaN();
    run_.last_active_plateau_evict_distance_median = std::numeric_limits<double>::quiet_NaN();
    run_.last_active_plateau_evict_distance_max = std::numeric_limits<double>::quiet_NaN();
    run_.last_active_plateau_refill_basin_count = 0;
    run_.last_active_plateau_refill_population_count = 0;
    run_.last_active_plateau_refill_refresh_count = 0;
    run_.last_active_plateau_refill_initial_seed_count = 0;
    run_.last_active_plateau_refill_generated_count = 0;
    run_.last_active_plateau_refill_lhs_count = 0;
    run_.last_active_plateau_refill_clone_fallback_count = 0;
    run_.last_active_plateau_refill_rejected_trace_count = 0;
    run_.last_active_plateau_refill_rejected_retired_count = 0;
    run_.last_active_plateau_retired_trial_reject_count = 0;
    run_.last_active_focus_ready_count = 0;
    run_.last_active_focus_cooldown_family_count = 0;
    run_.last_active_focus_stopped_family_count = 0;
    run_.active_focus_stop_clear_count = 0;
    run_.epoch_active_focus_remap_count = 0;
    run_.epoch_active_focus_reset_count = 0;
    run_.epoch_active_focus_selection_count = 0;
    run_.epoch_active_focus_rotation_blocked_count = 0;
    run_.epoch_active_focus_rotation_fallback_count = 0;
    run_.epoch_active_niche_hard_cap_release_count = 0;
    run_.epoch_active_niche_soft_fill_candidate_count = 0;
    run_.epoch_active_niche_soft_fill_move_count = 0;
    run_.epoch_active_niche_soft_fill_anchor_count = 0;
    run_.epoch_active_focus_soft_fill_candidate_count = 0;
    run_.epoch_active_focus_soft_fill_move_count = 0;
    run_.epoch_active_focus_soft_fill_anchor_count = 0;
    run_.epoch_active_focus_stop_release_candidate_count = 0;
    run_.epoch_active_focus_stop_release_move_count = 0;
    run_.last_active_focus_rel_improvement = std::numeric_limits<double>::quiet_NaN();
    run_.active_plateau_retire_defer_until_epoch = 0;
    run_.active_plateau_retire_clear_count = 0;
    run_.active_plateau_initial_frontier_retired = false;
    run_.active_plateau_retired_filter_keys = std::vector<std::string >{};
    run_.active_plateau_retired_filter_scaled_rows = std::vector<std::vector<double> >{};
    
    
    
    run_.traceable_refresh_archive = std::vector<TraceableRefreshArchiveEntry>{};
    run_.pending_traceable_refresh_archive = std::vector<TraceableRefreshArchiveEntry>{};
    run_.initial_traceable_seed_pool = std::vector<TraceableRefreshArchiveEntry>{};
    run_.pending_traceable_refresh_archive_seen = 0;

    
    run_.adaptive_glass_tolerance = AdaptiveGlassToleranceState{};

    
    
    run_.adaptive_glass_tolerance.enabled =
        options.adaptive_glass_tolerance_enabled && this->glass_proximity_enabled();
    run_.adaptive_glass_tolerance.current = this->current_glass_tolerance();

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

    

    

    
}

void AdaptiveSession::initialize_search_dynamics() {
run_.de_seconds = seconds_since(run_.run_start);
    run_.polish_seconds = 0.0;
    run_.total_de_generations = 0;
    run_.lpsr_reduction_count = 0;
    run_.lpsr_removed_total = 0;
    run_.polish_runs = 0;
    run_.polish_improvements = 0;
    run_.last_trace_population_best_merit = std::numeric_limits<double>::quiet_NaN();
    run_.mu_f = std::clamp(options.mu_f, 1.0e-6, 1.0);
    run_.mu_cr = std::clamp(options.mu_cr, 0.0, 1.0);
    run_.external_archive = std::vector<std::vector<double> >{};
    run_.external_archive_keys = std::vector<std::string >{};

    
    
    
    run_.retired_refresh_archive_keep_per_family = 2;
    
    
    
    
    

    for (int i = 0; i < run_.pop_size; ++i) {
        const std::vector<double> initial_row = population_row(run_.population, i, run_.dim);
        CandidateFitness initial_fitness = run_.fitness[static_cast<std::size_t>(i)];
        if (initial_row.size() == static_cast<std::size_t>(run_.dim) &&
            initial_fitness.trace_feasible) {
            initial_fitness.source = "initial_traceable_seed";
            initial_fitness.population_index = -1;
            run_.initial_traceable_seed_pool.push_back(
                {initial_row, initial_fitness});
        }
        this->add_traceable_refresh_archive(
            initial_row,
            run_.fitness[static_cast<std::size_t>(i)],
            "initial_archive");
    }

    
    
}

} } // namespace optiland::optimizer_detail
