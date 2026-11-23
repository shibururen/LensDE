# Native lens optimization

This directory optimizes a lens prescription from its design variables, bounds, and merit settings. The caller can supply an initial population and glass candidate groups. Standard differential evolution (DE) searches the population, damped least squares (DLS) refines a candidate, and the hybrid modes alternate population search with local refinement and refresh. The optimizers return the selected design variables and merit score, with population, archive, and progress data as configured. [Public optimizer API](../../include/optiland/optimizer.hpp)

## Candidate coordinates, generation, and scoring

- Converts between physical variable values and internal optimizer coordinates and maps bounds and step sizes. [`variable_transform.cpp`](variable_transform.cpp)
- Implements the scaling formulas and finite-difference step sizes used by DE and DLS. [`scaling_api.hpp`](scaling_api.hpp) · [`scaling.cpp`](scaling.cpp)
- Classifies variables and applies bounds and safety rules for continuous, glass, vignetting, and frozen values. [`variable_policy_api.hpp`](variable_policy_api.hpp) · [`variable_policy.cpp`](variable_policy.cpp)
- Reads and writes rows in flat population arrays, summarizes their spread, and provides variable-kind predicates. [`population_values_api.hpp`](population_values_api.hpp) · [`population_values.cpp`](population_values.cpp)
- Builds the initial population, inserts supplied seeds, and attaches the final population to the result. [`population_initialization_api.hpp`](population_initialization_api.hpp) · [`population_initialization.cpp`](population_initialization.cpp)
- Selects the requested seed or refresh generation method and enforces candidate, time, and trace limits. [`generation_api.hpp`](generation_api.hpp) · [`generation.cpp`](generation.cpp)
- Samples allowed physical coordinates and asphere sag ranges for new candidates. [`generation_sampling_api.hpp`](generation_sampling_api.hpp) · [`generation_sampling.cpp`](generation_sampling.cpp)
- Generates candidates using curvature, material, and paraxial power constraints. [`generation_physics_api.hpp`](generation_physics_api.hpp) · [`generation_physics.cpp`](generation_physics.cpp)
- Generates correlated asphere sag changes and refresh moves across coupled variable blocks. [`generation_correlated_api.hpp`](generation_correlated_api.hpp) · [`generation_correlated.cpp`](generation_correlated.cpp)
- Mutates discrete glass choices using catalog neighbors, random choices, and parent inheritance. [`glass_mutation_api.hpp`](glass_mutation_api.hpp) · [`glass_mutation.cpp`](glass_mutation.cpp)
- Defines the inputs for DE and sends population scoring batches to the CPU, CUDA, or the material-search broker. [`evaluation.cpp`](evaluation.cpp)
- Prepares a lens copy under the aperture policy used for merit evaluation. [`evaluation_support_api.hpp`](evaluation_support_api.hpp) · [`evaluation_support.cpp`](evaluation_support.cpp)
- Checks which scoring backends and local refinement methods support the selected merit and operands. [`merit_policy_api.hpp`](merit_policy_api.hpp) · [`merit_policy.cpp`](merit_policy.cpp)
- Provides the CUDA evaluation entry points required by CPU-only builds. [`cuda_fallback.cpp`](cuda_fallback.cpp)
- Runs population scoring and local refinement workers and collects their results in input order. [`workers_api.hpp`](workers_api.hpp) · [`workers.cpp`](workers.cpp)
- Builds candidate fitness records and compares merit, feasibility, and constraint violations. [`fitness_api.hpp`](fitness_api.hpp) · [`fitness.cpp`](fitness.cpp)
- Applies focal-length sign, chief ray angle, and joint-constraint checks to candidate replacement. [`acceptance_api.hpp`](acceptance_api.hpp) · [`acceptance.cpp`](acceptance.cpp)
- Normalizes merit violations and classifies the terms used by candidate fitness and DLS residuals. [`contribution_residuals_api.hpp`](contribution_residuals_api.hpp) · [`contribution_residuals.cpp`](contribution_residuals.cpp)
- Computes distances between candidate rows and selects diverse representatives from a fitness ranking. [`distance_api.hpp`](distance_api.hpp) · [`distance.cpp`](distance.cpp)

## Standard DE and fixed-stage hybrid optimization

- Runs standard DE from the initial population through trial generation, batch evaluation, and replacement. [`standard_de.cpp`](standard_de.cpp)
- Starts the fixed-stage hybrid DE–DLS session. [`hybrid.cpp`](hybrid.cpp)
- Holds the hybrid population, random generator, and per-phase statistics. [`hybrid_session.hpp`](hybrid_session.hpp)
- Alternates DE, local refinement, incumbent reinsertion, and refresh, then assembles the result. [`hybrid_session.cpp`](hybrid_session.cpp)
- Sets up the hybrid options and scores the initial population. [`hybrid_initialization.cpp`](hybrid_initialization.cpp)
- Advances the hybrid population through the DE generations in one outer epoch. [`hybrid_de.cpp`](hybrid_de.cpp)
- Selects parent rows for local refinement and runs the polish work. [`hybrid_polish.cpp`](hybrid_polish.cpp)
- Replaces selected population rows with new candidates after DE and local refinement. [`hybrid_refresh.cpp`](hybrid_refresh.cpp)
- Scores scaled rows, updates the best candidate, and emits phase progress records. [`hybrid_scoring.cpp`](hybrid_scoring.cpp)

## Damped least squares

- Starts a local refinement run from a candidate's physical variable values. [`dls.cpp`](dls.cpp)
- Holds the residual cache, damping, and trust-region state of one DLS invocation. [`dls_session.hpp`](dls_session.hpp)
- Runs the DLS session and returns the best physical values, score, and iteration statistics. [`dls_session.cpp`](dls_session.cpp)
- Builds the starting residuals and sets the initial damping and trust radius. [`dls_initialization.cpp`](dls_initialization.cpp)
- Runs the iteration loop, applies convergence checks, and reports progress. [`dls_iterations.cpp`](dls_iterations.cpp)
- Constructs or reuses the finite-difference Jacobian and its normal-equation model. [`dls_jacobian.cpp`](dls_jacobian.cpp)
- Provides numerical kernels for Jacobian sampling and the dense linear solve. [`dls_linear_api.hpp`](dls_linear_api.hpp) · [`dls_linear.cpp`](dls_linear.cpp)
- Projects each proposed step into bounds and applies the damping or trust-region acceptance rule. [`dls_trial_search.cpp`](dls_trial_search.cpp)
- Evaluates a trial and converts its merit terms to residuals for the selected DLS formulation. [`dls_scoring.cpp`](dls_scoring.cpp)
- Builds ordered residuals for merit, guard-band, and per-ray formulations. [`dls_residuals_api.hpp`](dls_residuals_api.hpp) · [`dls_residuals.cpp`](dls_residuals.cpp)
- Refines multiple candidates with batched scoring and returns the updates in matching row positions. [`dls_batch_api.hpp`](dls_batch_api.hpp) · [`dls_batch.cpp`](dls_batch.cpp)
- Writes optional diagnostics for the residuals and Jacobian used by the linear model. [`dls_diagnostics.cpp`](dls_diagnostics.cpp)

## Adaptive DE and DLS

The adaptive search adjusts the differential weight and crossover rate from successful trials. Its configured policies can also shrink the population, preserve distinct lens families, and use previously scored designs to guide new candidates. [Adaptive entry point](../optimizer.cpp)

### Setup, scoring, and epochs

- Declares the methods and inputs of one adaptive optimization session. [`adaptive_session.hpp`](adaptive_session.hpp)
- Owns the population, fitness, archives, random generator, family state, and run counters. [`adaptive_state.hpp`](adaptive_state.hpp)
- Defines the context passed between generation, selection, and refinement stages. [`adaptive_contexts.hpp`](adaptive_contexts.hpp)
- Defines adaptive cache and lifecycle records for devices, topology matches, basins, and families. [`adaptive_types.hpp`](adaptive_types.hpp)
- Runs initialization, the outer search epochs, and result construction in order. [`adaptive_execute.cpp`](adaptive_execute.cpp)
- Sets up runtime options, avoidance data, the scored population, and the initial basin archive and search state. [`adaptive_initialization_configure_runtime.cpp`](adaptive_initialization_configure_runtime.cpp)
- Initializes convergence history, diagnostics, geometry-memory mutation, and active focus. [`adaptive_initialization_initialize_convergence.cpp`](adaptive_initialization_initialize_convergence.cpp)
- Converts adaptive rows to physical values and submits individual or batched scoring with the active evaluation context. [`adaptive_evaluation.cpp`](adaptive_evaluation.cpp)
- Runs the outer epochs and schedules lifecycle checks, local refinement, and population refresh. [`adaptive_epochs.cpp`](adaptive_epochs.cpp)
- Checks whether topology discovery and population-size reduction meet the configured early-stop conditions. [`adaptive_convergence.cpp`](adaptive_convergence.cpp)
- Updates glass-proximity tolerances as the search reaches the configured milestones. [`adaptive_glass_tolerance.cpp`](adaptive_glass_tolerance.cpp)

### Trial generation and population replacement

- Runs the inner generation loop from trial generation through evaluation and survival. [`adaptive_generation_run_epoch_de_generations.cpp`](adaptive_generation_run_epoch_de_generations.cpp)
- Builds trial rows using the selected parents, guides, and mutation settings. [`adaptive_generation_generate_trial_population.cpp`](adaptive_generation_generate_trial_population.cpp)
- Selects trial survivors using candidate fitness and the applicable anchor and family rules. [`adaptive_generation_select_trial_population.cpp`](adaptive_generation_select_trial_population.cpp)
- Maintains the incumbent, shrinks the population on schedule, and updates the external archive of replaced rows. [`adaptive_population.cpp`](adaptive_population.cpp)
- Supplies incumbent lookup, population-size reduction, differential-weight sampling, and replacement policy. [`adaptive_policy_best_index.cpp`](adaptive_policy_best_index.cpp)
- Perturbs local search seeds in physical coordinates and rejects duplicate scaled rows. [`adaptive_policy_local_funnel_perturb_physical.cpp`](adaptive_policy_local_funnel_perturb_physical.cpp)
- Builds mating pools and draws population or archive parents under family quotas. [`adaptive_mating_active_mating_allow_similarity.cpp`](adaptive_mating_active_mating_allow_similarity.cpp)
- Selects a mating strategy and prepares parent pools for basin-guided refresh. [`adaptive_mating_select_de_mating.cpp`](adaptive_mating_select_de_mating.cpp)
- Selects and records p-best guides from the ranked population, local families, or other allowed pools. [`adaptive_pbest.cpp`](adaptive_pbest.cpp)

### Lens structure, avoidance, and basin archive

- Builds descriptors and stable keys for lens elements and optical windows. [`topology_descriptor_api.hpp`](topology_descriptor_api.hpp) · [`topology_descriptor.cpp`](topology_descriptor.cpp)
- Parses topology keys and compares shape and family similarities using the configured costs. [`topology_similarity_api.hpp`](topology_similarity_api.hpp) · [`topology_similarity.cpp`](topology_similarity.cpp)
- Encodes continuous geometry and parameter signatures for structural comparison. [`continuous_structure_api.hpp`](continuous_structure_api.hpp) · [`continuous_structure.cpp`](continuous_structure.cpp)
- Checks proposed candidates against stored topologies and continuous geometries marked for avoidance. [`adaptive_avoidance.cpp`](adaptive_avoidance.cpp)
- Measures distance to remembered geometries and chooses compatible difference-vector indices. [`adaptive_geometry_memory_geometry_memory_coordinate.cpp`](adaptive_geometry_memory_geometry_memory_coordinate.cpp)
- Builds geometry-memory mutation guidance and adjusts p-best selection near remembered designs. [`adaptive_geometry_memory_build_geometry_memory_qohr_step.cpp`](adaptive_geometry_memory_build_geometry_memory_qohr_step.cpp)
- Assigns topology keys and distance representations to scored lens systems and caches similarity lookups. [`adaptive_basins_assign_topology_summary_for_system.cpp`](adaptive_basins_assign_topology_summary_for_system.cpp)
- Selects representatives from the basin archive to guide mutation. [`adaptive_basins_select_basin_archive_guide.cpp`](adaptive_basins_select_basin_archive_guide.cpp)
- Adds eligible candidates to the basin archive and keeps a bounded, diverse set of representatives. [`adaptive_basins_select_basin_descriptor_representative_indices.cpp`](adaptive_basins_select_basin_descriptor_representative_indices.cpp)

### Active lens families

- Applies family similarity and quality thresholds and identifies protected population slots. [`adaptive_active_families_active_family_similarity_threshold.cpp`](adaptive_active_families_active_family_similarity_threshold.cpp)
- Selects archive basins for active population slots and inserts their candidate anchors. [`adaptive_active_families_select_active_basin_indices.cpp`](adaptive_active_families_select_active_basin_indices.cpp)
- Updates basin lifetimes and builds family anchors and restricted mating pools. [`adaptive_active_families_update_active_basin_lifecycle.cpp`](adaptive_active_families_update_active_basin_lifecycle.cpp)
- Sets active-focus quotas, cooldowns, and family rotation rules. [`adaptive_active_focus_active_focus_soft_fill_enabled.cpp`](adaptive_active_focus_active_focus_soft_fill_enabled.cpp)
- Releases stopped focus families and draws replacement candidates from the available archives and seed pool. [`adaptive_active_focus_release_stopped_active_focus_families.cpp`](adaptive_active_focus_release_stopped_active_focus_families.cpp)
- Selects the focus families for the next epoch and tracks their rotation. [`adaptive_active_focus_select_focus_families.cpp`](adaptive_active_focus_select_focus_families.cpp)
- Tracks families retired after a plateau and answers stalled-family and cooldown queries. [`adaptive_active_lifecycle_active_plateau_retired_unique_count.cpp`](adaptive_active_lifecycle_active_plateau_retired_unique_count.cpp)
- Evicts retired members and updates the records that keep retired families out of p-best selection. [`adaptive_active_lifecycle_evict_active_plateau_retired_keys.cpp`](adaptive_active_lifecycle_evict_active_plateau_retired_keys.cpp)

### Local refinement, refresh, and final results

- Selects adaptive refinement candidates and dispatches standard polish or micro-island batches. [`adaptive_polish_select_polish_candidates.cpp`](adaptive_polish_select_polish_candidates.cpp)
- Prepares seed rows for micro-island refinement and records attempted sources. [`adaptive_polish_make_micro_local_seed_rows.cpp`](adaptive_polish_make_micro_local_seed_rows.cpp)
- Refines selected micro-island basins and uses remaining eligible candidates for standard refinement. [`adaptive_polish_refine_micro_island_basins.cpp`](adaptive_polish_refine_micro_island_basins.cpp)
- Selects replaceable population slots and rebalances seed families before refresh. [`adaptive_refresh_prepare_surface_refresh.cpp`](adaptive_refresh_prepare_surface_refresh.cpp)
- Maintains a refresh archive of traceable candidates and creates rows for population refill. [`adaptive_refresh_refresh_trace_archive_enabled.cpp`](adaptive_refresh_refresh_trace_archive_enabled.cpp)
- Constructs the final result with the best physical variable values, selected population, basin archive, and timing. [`adaptive_result.cpp`](adaptive_result.cpp)

## Adaptive progress records and shared interfaces

- Resolves output snapshot paths and formats timing and JSON values used by progress writers. [`diagnostics_api.hpp`](diagnostics_api.hpp) · [`diagnostics.cpp`](diagnostics.cpp)
- Normalizes convergence metric names and reads their values from progress records. [`adaptive_diagnostics_normalize_metric_token.cpp`](adaptive_diagnostics_normalize_metric_token.cpp)
- Assembles the current optimizer progress record and sends it to the caller's callback. [`adaptive_diagnostics_emit_trace.cpp`](adaptive_diagnostics_emit_trace.cpp)
- Writes ordered population and basin snapshots at configured generation boundaries. [`adaptive_diagnostics_emit_research_snapshot.cpp`](adaptive_diagnostics_emit_research_snapshot.cpp)
- Adds phase identity, feasibility counts, refresh settings, and constraint checks to progress records. [`adaptive_trace_assembly_input.cpp`](adaptive_trace_assembly_input.cpp)
- Adds active family, basin, mating, and retirement statistics to progress records. [`adaptive_trace_assembly_active_families.cpp`](adaptive_trace_assembly_active_families.cpp)
- Adds mutation parameters, memory guidance, and mating-source statistics to progress records. [`adaptive_trace_assembly_memory.cpp`](adaptive_trace_assembly_memory.cpp)
- Adds population merit, topology descriptors, basin merit, and glass-tolerance data to progress records. [`adaptive_trace_assembly_population.cpp`](adaptive_trace_assembly_population.cpp)
- Adds best-candidate metrics, cumulative timing, and local refinement results to progress records. [`adaptive_trace_assembly_timing.cpp`](adaptive_trace_assembly_timing.cpp)
- Defines shared structures for topology descriptors, worker contexts, and acceptance decisions. [`support_types.hpp`](support_types.hpp)
- Collects the internal helper interfaces used by optimizer implementations. [`support.hpp`](support.hpp)
