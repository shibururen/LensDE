"""Native stage argv construction and JADE policy; preserves argument order and numeric spelling."""
from __future__ import annotations
import math
from pathlib import Path
from .. import commands as _lensde_commands
from .. import configuration as _lensde_configuration
from .. import geometry as _lensde_geometry
from .. import initialization as _lensde_initialization
from .. import materials as _lensde_materials
from .. import provenance as _lensde_provenance
from .contracts import SearchState, PhaseResult

def build_initial_search_stage_args(_flow: SearchState, stage_dir: Path, stage_system_path: Path, stage_merit_path: Path, active_merit: dict, initial_population_path: Path, outer_epochs: int, seed_offset: int, stage_variables_path: Path | None=None, stage_glass_search_path: Path | None=None, stage_glass_search_config: dict | None=None, stage_population: int | None=None, stage_de_gen: int | None=None, sampling_stage: str | None=None, stage_de_use_cuda: bool=False) -> list[str]:
    """Native stage argv construction and JADE policy Implements `build_initial_search_stage_args` for this responsibility."""
    active_variables_path = stage_variables_path or _flow.optics.variables_path
    active_initial_population_path = initial_population_path
    if _lensde_geometry.variable_list_has_asphere_coeff(_flow.optics.variables) and initial_population_path == _flow.seeding.seed_path and _flow.settings.external_seed_path.exists():
        active_initial_population_path = _flow.settings.external_seed_path
    active_glass_search_path = stage_glass_search_path or _flow.optics.glass_search_path
    active_glass_search_config = stage_glass_search_config if stage_glass_search_config is not None else _flow.optics.glass_search_config
    active_population = _flow.settings.population if stage_population is None else max(1, int(stage_population))
    active_de_gen = _flow.settings.generations if stage_de_gen is None else max(1, int(stage_de_gen))

    def stage_option_int(key: str, default: int, minimum: int=0) -> int:
        value = _flow.settings.options.get(key, default)
        if value is None or value == '':
            value = default
        try:
            return max(minimum, int(value))
        except (TypeError, ValueError):
            return max(minimum, int(default))

    def stage_sampling_value(stage: str | None, suffix: str, default: int) -> int:
        normalized = str(stage or '').strip().lower().replace('-', '_')
        if normalized in {'refine', 'refine_scout'}:
            normalized = 'refine'
        if normalized in {'focus', 'refine'}:
            value = _flow.settings.options.get(f'initial_search_funnel_{normalized}_trace_{suffix}', default)
        else:
            value = default
        try:
            return max(1, int(value))
        except (TypeError, ValueError):
            return max(1, int(default))
    active_rays = stage_sampling_value(sampling_stage, 'rays', int(active_merit.get('num_rays', _flow.optics.search_merit['num_rays'])))
    active_arms = stage_sampling_value(sampling_stage, 'arms', int(active_merit.get('pupil_arms', _flow.optics.search_merit['pupil_arms'])))
    active_variables = _lensde_geometry.read_variables_for_scaling(active_variables_path, _flow.optics.variables)
    active_internal_scaling = _lensde_geometry.effective_optimizer_internal_scaling(_flow.settings.options, active_variables, default='bound_normalized' if _flow.settings.adaptive_initial else 'legacy_optical')
    polish_selection = str(_flow.settings.options.get('initial_search_polish_selection', 'topology_balanced')).strip().lower().replace('-', '_').replace(' ', '_')
    if polish_selection not in {'merit', 'topology_balanced'}:
        polish_selection = 'topology_balanced'
    active_stage_exe, active_stage_use_cuda = _lensde_provenance.initial_search_stage_de_evaluator(_flow.runtime.exe, _flow.runtime.use_cuda, stage_de_use_cuda)
    stage_args = [str(active_stage_exe), '--system', str(stage_system_path), '--merit', str(stage_merit_path), '--aiming', _flow.settings.aiming_mode, '--aiming-iter', str(int(_flow.settings.options.get('aiming_iter', 20) or 20)), '--trace-policy', str(active_merit.get('trace_policy', 'soft_coverage')), '--rays', str(active_rays), '--arms', str(active_arms), '--distribution', str(active_merit.get('pupil_distribution', _flow.optics.search_merit['pupil_distribution'])), '--sampling-seed', str(int(active_merit.get('sampling_seed', 1))), '--spot-rays', str(int(_flow.settings.options.get('spot_rays', 41))), '--spot-arms', str(int(_flow.settings.options.get('spot_arms', 64))), '--spot-distribution', str(_flow.settings.options.get('spot_distribution', 'uniform')), '--spot-sampling-seed', str(int(_flow.settings.options.get('spot_sampling_seed', active_merit.get('sampling_seed', 1)))), '--variables', str(active_variables_path), '--initial-population', str(active_initial_population_path), '--adaptive-hybrid-de-dls' if _flow.settings.adaptive_initial else '--hybrid-de-dls', '--pop', str(active_population), '--gen', str(active_de_gen), '--hybrid-outer', str(max(1, outer_epochs)), '--hybrid-de-gen', str(active_de_gen), '--hybrid-parents', str(_flow.settings.hybrid_parents), '--hybrid-polish-workers', str(_flow.settings.hybrid_polish_workers), '--hybrid-refresh', str(_flow.settings.hybrid_refresh), '--hybrid-distance-metric', str(_flow.settings.options.get('initial_search_hybrid_distance_metric', 'bound_normalized')), '--optimizer-internal-scaling', active_internal_scaling, '--seed', str(_flow.settings.seed + seed_offset), '--hybrid-local-iter', str(int(_flow.settings.options.get('initial_search_local_iterations', 50) or 50)), '--polish-selection', polish_selection, '--local-method', str(_flow.settings.options.get('initial_search_local_method', 'dls')), '--local-bounds-mode', str(_flow.settings.options.get('initial_search_local_bounds_mode', 'safety')), '--local-residual-mode', str(_flow.settings.options.get('initial_search_local_residual_mode', 'expanded')), '--local-lambda', str(float(_flow.settings.options.get('initial_search_local_lambda', 0.01))), '--local-fd', str(float(_flow.settings.options.get('initial_search_local_fd', 0.0001))), '--local-min-step', str(float(_flow.settings.options.get('initial_search_local_min_step', 1e-08))), '--local-tol', str(float(_flow.settings.options.get('initial_search_local_tol', 0.0001))), '--local-max-reject', str(int(_flow.settings.options.get('initial_search_local_max_reject', 4) or 4)), '--out-dir', str(stage_dir)]
    _lensde_initialization.append_asphere_reference_policy_args(stage_args, active_variables, 'input')
    if _flow.settings.adaptive_initial:
        stage_args.extend(['--de-variant', str(_flow.settings.options.get('initial_search_de_variant', 'jade_pbest_archive')), '--constraint-handling', str(_flow.settings.options.get('initial_search_constraint_handling', 'feasibility_rules')), '--adaptive-search-purpose', 'initial_search', '--first-order-epsilon', str(float(_flow.settings.options.get('initial_search_first_order_epsilon', 1e-05))), '--polish-candidates', str(int(_flow.settings.options.get('initial_search_polish_candidates', 8) or 8)), '--polish-refill-max-extra', str(int(_flow.settings.options.get('initial_search_polish_refill_max_extra', _flow.settings.options.get('initial_search_polish_candidates', 8)) or 0)), '--pbest-fraction', str(float(_flow.settings.options.get('initial_search_pbest_fraction', 0.1))), '--archive-size-ratio', str(float(_flow.settings.options.get('initial_search_archive_size_ratio', 1.0))), '--adaptive-min-population', str(int(_flow.settings.options.get('initial_search_min_population', 20) or 20)), '--basin-max', str(stage_option_int('initial_search_basin_max', 96, 1)), '--basin-radius', str(float(_flow.settings.options.get('initial_search_basin_radius', 0.25))), '--basin-distance-metric', str(_flow.settings.options.get('initial_search_basin_distance_metric', 'structure_bound_normalized')), '--basin-air-thickness-distance-cap', str(float(_flow.settings.options.get('initial_search_basin_air_thickness_distance_cap', 25.0))), '--basin-output-count', str(stage_option_int('initial_search_basin_output_count', 48, 1)), '--basin-active-count', str(stage_option_int('initial_search_basin_active_count', 9, 0)), '--basin-protected-fraction', str(float(_flow.settings.options.get('initial_search_basin_protected_fraction', 0.35))), '--basin-semantic-max-per-topology', str(int(_flow.settings.options.get('initial_search_basin_semantic_max_per_topology', 1) or 1)), '--topology-diversity-mode', str(_flow.settings.options.get('initial_search_topology_diversity_mode', 'topology_family_similarity')), '--topology-descriptor-window-lmin', str(int(_flow.settings.options.get('initial_search_topology_descriptor_window_lmin', 2) or 2)), '--topology-descriptor-window-lmax', str(int(_flow.settings.options.get('initial_search_topology_descriptor_window_lmax', 4) or 4)), '--topology-descriptor-max-active-axes', str(int(_flow.settings.options.get('initial_search_topology_descriptor_max_active_axes', 5) or 5)), '--topology-similarity-threshold', str(float(_flow.settings.options.get('initial_search_topology_similarity_threshold', 0.9))), '--topology-descriptor-audit-interval', str(int(_flow.settings.options.get('initial_search_topology_descriptor_audit_interval', 25) or 25)), '--topology-descriptor-weak-tau-low', str(float(_flow.settings.options.get('initial_search_topology_descriptor_weak_tau_low', 0.08))), '--topology-descriptor-weak-tau-high', str(float(_flow.settings.options.get('initial_search_topology_descriptor_weak_tau_high', 0.2))), '--adaptive-min-popsize-multiplier', str(int(_flow.settings.options.get('initial_search_min_popsize_multiplier', 4) or 4))])
        if bool(_flow.settings.options.get('initial_search_track_population_diversity', False)):
            stage_args.append('--track-population-diversity')
        _lensde_commands.append_pbest_guidance_args(stage_args, _flow.settings.options, 'initial_search_', default_scope='basin_local', default_global_fallback=False, default_inject_best=False)
        _lensde_commands.append_de_mating_args(stage_args, _flow.settings.options, 'initial_search_')
        if bool(_flow.settings.options.get('initial_search_basin_active_diversity', True)):
            stage_args.append('--basin-active-diversity')
        else:
            stage_args.append('--basin-no-active-diversity')
        _lensde_commands.append_basin_active_quality_args(stage_args, _flow.settings.options, 'initial_search_')
        if bool(_flow.settings.options.get('initial_search_basin_balanced_refresh', True)):
            stage_args.append('--basin-balanced-refresh')
        else:
            stage_args.append('--basin-no-balanced-refresh')
        if bool(_flow.settings.options.get('initial_search_basin_semantic_diversity', True)):
            stage_args.append('--basin-semantic-diversity')
        else:
            stage_args.append('--basin-no-semantic-diversity')
        if bool(_flow.settings.options.get('initial_search_hard_first_order', True)):
            stage_args.append('--adaptive-hard-first-order')
        else:
            stage_args.append('--adaptive-soft-first-order')
    else:
        stage_args.extend(['--de-strategy', str(_flow.settings.options.get('initial_search_de_strategy', 'best1bin')), '--mutation-min', str(float(_flow.settings.options.get('initial_search_mutation_min', 0.5))), '--mutation-max', str(float(_flow.settings.options.get('initial_search_mutation_max', 1.0))), '--recombination', str(float(_flow.settings.options.get('initial_search_recombination', 0.7)))])
    _lensde_materials.append_aiming_policy_args(stage_args, _flow.settings.options)
    _lensde_commands.append_refresh_generation_args(stage_args, _flow.settings.options, 'initial_search_', seed_pool_path=_flow.seeding.seed_path)
    if _flow.settings.initial_polish_disabled:
        stage_args.append('--no-polish')
    if bool(_flow.settings.options.get('initial_search_batched_polish', False)):
        stage_args.append('--batched-polish')
    if _flow.settings.options.get('clear_aperture_update', True):
        stage_args.append('--clear-aperture-update')
    if _flow.settings.options.get('sync_stop_aperture', True):
        stage_args.append('--sync-stop-aperture')
    _lensde_materials.append_glass_search_args(stage_args, active_glass_search_path, active_glass_search_config, _flow.settings.options)
    _lensde_provenance.append_initial_search_stage_evaluator_args(stage_args, active_stage_use_cuda)
    set_arg(stage_args, '--progress-every', math.gcd(50, active_de_gen))
    return stage_args

def set_arg(args: list[str], flag: str, value: object) -> None:
    """Replace the first matching option in place, otherwise append it; retain argv ordering."""
    text = str(value)
    if flag in args:
        index = args.index(flag)
        if index + 1 < len(args):
            args[index + 1] = text
        else:
            args.append(text)
    else:
        args.extend([flag, text])

def remove_flags(args: list[str], flags: set[str]) -> None:
    """Remove only the named standalone switches from the existing command."""
    index = 0
    while index < len(args):
        if args[index] in flags:
            args.pop(index)
            continue
        index += 1

def remove_arg(args: list[str], flag: str) -> None:
    """Remove the named option and its value without reordering other arguments."""
    index = 0
    while index < len(args):
        if args[index] == flag:
            del args[index:min(index + 2, len(args))]
            continue
        index += 1

def set_bool_flag(args: list[str], enabled: bool, true_flag: str, false_flag: str) -> None:
    """Native stage argv construction and JADE policy Implements `set_bool_flag` for this responsibility."""
    remove_flags(args, {true_flag, false_flag})
    args.append(true_flag if enabled else false_flag)

def enforce_standard_jade_mutation(args: list[str]) -> None:
    """Native stage argv construction and JADE policy Implements `enforce_standard_jade_mutation` for this responsibility."""
    'Keep basin/checkpoint/avoid infrastructure, but remove basin-guided mutation.'
    set_arg(args, '--de-variant', 'jade_pbest_archive')
    set_arg(args, '--pbest-scope', 'global')
    set_arg(args, '--pbest-basin-rate', 0.0)
    set_arg(args, '--pbest-same-key-rate', 0.0)
    set_arg(args, '--pbest-global-rate', 1.0)
    set_bool_flag(args, True, '--pbest-global-fallback', '--pbest-no-global-fallback')
    set_arg(args, '--de-mating-scope', 'global')
    set_arg(args, '--de-mating-local-rate', 0.0)
    set_arg(args, '--de-mating-min-pool', 4)
    set_bool_flag(args, False, '--de-mating-strict-local', '--de-mating-no-strict-local')
    set_bool_flag(args, False, '--basin-active-diversity', '--basin-no-active-diversity')
    set_bool_flag(args, False, '--basin-balanced-refresh', '--basin-no-balanced-refresh')
    set_bool_flag(args, False, '--basin-active-mating-quota', '--basin-no-active-mating-quota')
    set_bool_flag(args, False, '--basin-active-local-cohort', '--basin-no-active-local-cohort')
    set_bool_flag(args, False, '--basin-active-slot-strict-frozen-family', '--basin-no-active-slot-strict-frozen-family')
    set_bool_flag(args, False, '--basin-active-plateau-lifecycle', '--basin-no-active-plateau-lifecycle')
    for true_flag, false_flag in (('--basin-active-niche-tracking', '--basin-no-active-niche-tracking'), ('--basin-active-niche-survival', '--basin-no-active-niche-survival'), ('--basin-active-niche-rebalance', '--basin-no-active-niche-rebalance'), ('--basin-active-niche-soft-fill', '--basin-no-active-niche-soft-fill'), ('--basin-active-focus-rebalance', '--basin-no-active-focus-rebalance'), ('--basin-active-focus-soft-fill', '--basin-no-active-focus-soft-fill'), ('--basin-active-focus-rotation', '--basin-no-active-focus-rotation'), ('--basin-active-focus-stop-release', '--basin-no-active-focus-stop-release'), ('--basin-active-refresh-rebalance', '--basin-no-active-refresh-rebalance'), ('--basin-active-family-parameter-merge', '--basin-no-active-family-parameter-merge'), ('--basin-active-plateau-track-active-niches', '--basin-no-active-plateau-track-active-niches'), ('--basin-active-plateau-retire-evict', '--basin-no-active-plateau-retire-evict')):
        set_bool_flag(args, False, true_flag, false_flag)

def configure_funnel_global_args(_flow: SearchState, args: list[str], *, no_lpsr: bool, no_polish: bool, stage: str='explore', disable_basins: bool=False) -> list[str]:
    """Native stage argv construction and JADE policy Implements `configure_funnel_global_args` for this responsibility."""

    def option_int(key: str, default: int, minimum: int=0) -> int:
        value = _flow.settings.options.get(key, default)
        if value is None or value == '':
            value = default
        try:
            return max(minimum, int(value))
        except (TypeError, ValueError):
            return max(minimum, int(default))

    def option_float(key: str, default: float, minimum: float=0.0) -> float:
        value = _flow.settings.options.get(key, default)
        if value is None or value == '':
            value = default
        return max(minimum, _lensde_configuration.finite_float(value, default))
    out = list(args)
    remove_flags(out, {'--adaptive-no-lpsr', '--no-polish', '--adaptive-allow-trace-infeasible', '--adaptive-allow-near-trace-infeasible', '--adaptive-early-stop-qualified-topologies', '--adaptive-micro-island', '--adaptive-micro-island-replace-polish', '--adaptive-micro-island-keep-polish', '--adaptive-micro-island-early-stop', '--adaptive-micro-island-retire-on-early-stop', '--adaptive-micro-island-no-retire-on-early-stop', '--adaptive-glass-tolerance', '--adaptive-no-glass-tolerance', '--basin-active-plateau-retire-evict', '--basin-no-active-plateau-retire-evict', '--basin-active-plateau-retire-evict-parameter-only', '--basin-no-active-plateau-retire-evict-parameter-only', '--basin-active-plateau-retire-frontier-on-first-update', '--basin-no-active-plateau-retire-frontier-on-first-update'})
    remove_arg(out, '--trace-coverage-epsilon')
    remove_arg(out, '--adaptive-early-stop-best-merit-max')
    remove_arg(out, '--adaptive-early-stop-qualified-topology-mode')
    remove_arg(out, '--adaptive-early-stop-qualified-topology-epsilon')
    remove_arg(out, '--adaptive-early-stop-qualified-topology-merit-ratio')
    remove_arg(out, '--adaptive-early-stop-qualified-topology-merit-floor')
    remove_arg(out, '--adaptive-early-stop-qualified-topology-merit-delta')
    remove_arg(out, '--adaptive-early-stop-qualified-topology-merit-max')
    remove_arg(out, '--adaptive-micro-island-count')
    remove_arg(out, '--adaptive-micro-island-popsize')
    remove_arg(out, '--adaptive-micro-island-population')
    remove_arg(out, '--adaptive-micro-island-outer')
    remove_arg(out, '--adaptive-micro-island-de-gen')
    remove_arg(out, '--adaptive-micro-island-seed-scale')
    remove_arg(out, '--adaptive-micro-island-polish-candidates')
    remove_arg(out, '--adaptive-micro-island-polish-refill-max-extra')
    remove_arg(out, '--adaptive-micro-island-survivors')
    remove_arg(out, '--adaptive-micro-island-family-seed-limit')
    remove_arg(out, '--adaptive-micro-island-refresh-archive-seed-limit')
    remove_arg(out, '--adaptive-micro-island-early-stop-min-epochs')
    remove_arg(out, '--adaptive-micro-island-early-stop-window')
    remove_arg(out, '--adaptive-micro-island-early-stop-min-rel-improve')
    remove_arg(out, '--adaptive-micro-island-early-stop-min-abs-improve')
    remove_arg(out, '--adaptive-glass-tolerance-warmup-epochs')
    remove_arg(out, '--adaptive-glass-tolerance-trigger-mode')
    remove_arg(out, '--adaptive-glass-tolerance-focus-clear-target')
    remove_arg(out, '--adaptive-glass-tolerance-stop-when-ready')
    remove_arg(out, '--adaptive-glass-tolerance-no-stop-when-ready')
    remove_arg(out, '--adaptive-glass-tolerance-bridge-freeze-focus')
    remove_arg(out, '--adaptive-glass-tolerance-no-bridge-freeze-focus')
    remove_arg(out, '--adaptive-glass-tolerance-elite-fraction')
    remove_arg(out, '--adaptive-glass-tolerance-quantile')
    remove_arg(out, '--adaptive-glass-tolerance-margin')
    remove_arg(out, '--adaptive-glass-tolerance-floor')
    remove_arg(out, '--adaptive-glass-tolerance-smoothing')
    remove_arg(out, '--adaptive-glass-tolerance-decay')
    remove_arg(out, '--adaptive-glass-tolerance-early-stop-max')
    remove_arg(out, '--basin-active-plateau-frontier-count')
    remove_arg(out, '--basin-active-plateau-frontier-multiplier')
    remove_arg(out, '--basin-active-plateau-retire-evict-fraction')
    remove_arg(out, '--basin-active-plateau-retire-evict-max-fraction')
    remove_arg(out, '--basin-active-plateau-retire-evict-min')
    remove_arg(out, '--basin-active-plateau-retire-evict-parameter-distance')
    remove_arg(out, '--basin-active-plateau-retire-defer-early-stop-epochs')
    remove_arg(out, '--basin-active-plateau-retire-stop-after-clears')
    if no_lpsr:
        out.append('--adaptive-no-lpsr')
    if disable_basins:
        out.append('--basin-disable')
        set_bool_flag(out, False, '--basin-active-diversity', '--basin-no-active-diversity')
        set_bool_flag(out, False, '--basin-balanced-refresh', '--basin-no-balanced-refresh')
        set_bool_flag(out, False, '--basin-semantic-diversity', '--basin-no-semantic-diversity')
        set_bool_flag(out, False, '--basin-active-plateau-track-active-niches', '--basin-no-active-plateau-track-active-niches')
        set_arg(out, '--pbest-scope', 'none')
        set_arg(out, '--pbest-basin-rate', 0.0)
        set_arg(out, '--pbest-same-key-rate', 0.0)
        set_arg(out, '--pbest-global-rate', 1.0)
    else:
        set_bool_flag(out, True, '--basin-active-diversity', '--basin-no-active-diversity')
        set_bool_flag(out, True, '--basin-balanced-refresh', '--basin-no-balanced-refresh')
        set_bool_flag(out, True, '--basin-semantic-diversity', '--basin-no-semantic-diversity')
    if _flow.settings.adaptive_initial and (not disable_basins):
        set_bool_flag(out, bool(_flow.settings.options.get('initial_search_basin_active_plateau_track_active_niches', True)), '--basin-active-plateau-track-active-niches', '--basin-no-active-plateau-track-active-niches')
        set_bool_flag(out, bool(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_evict', True)), '--basin-active-plateau-retire-evict', '--basin-no-active-plateau-retire-evict')
        set_bool_flag(out, bool(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_evict_parameter_only', True)), '--basin-active-plateau-retire-evict-parameter-only', '--basin-no-active-plateau-retire-evict-parameter-only')
        set_bool_flag(out, bool(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_frontier_on_first_update', False)), '--basin-active-plateau-retire-frontier-on-first-update', '--basin-no-active-plateau-retire-frontier-on-first-update')
        set_arg(out, '--basin-active-plateau-frontier-count', option_int('initial_search_funnel_global_plateau_frontier_count', 1, 1))
        set_arg(out, '--basin-active-plateau-frontier-multiplier', option_float('initial_search_funnel_global_plateau_frontier_multiplier', 0.0, 0.0))
        set_arg(out, '--basin-active-plateau-retire-evict-fraction', min(1.0, option_float('initial_search_funnel_global_plateau_retire_evict_fraction', 1.0, 0.0)))
        set_arg(out, '--basin-active-plateau-retire-evict-max-fraction', min(1.0, option_float('initial_search_funnel_global_plateau_retire_evict_max_fraction', 0.2, 0.0)))
        set_arg(out, '--basin-active-plateau-retire-evict-min', option_int('initial_search_funnel_global_plateau_retire_evict_min', 0, 0))
        set_arg(out, '--basin-active-plateau-retire-evict-parameter-distance', option_float('initial_search_funnel_global_plateau_retire_evict_parameter_distance', 0.15, 0.0))
        set_arg(out, '--basin-active-plateau-retire-defer-early-stop-epochs', option_int('initial_search_funnel_global_plateau_retire_defer_early_stop_epochs', 0, 0))
        set_arg(out, '--basin-active-plateau-retire-stop-after-clears', option_int('initial_search_funnel_global_plateau_retire_stop_after_clears', 0, 0))
    stage_key = str(stage or 'explore').strip().lower().replace('-', '_').replace(' ', '_')
    adaptive_glass_tolerance_enabled = _flow.settings.adaptive_initial and stage_key != 'develop' and (_flow.optics.continuous_glass_constraints_count > 0) and bool(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_enabled', True))
    if adaptive_glass_tolerance_enabled:
        out.append('--adaptive-glass-tolerance')
        trigger_mode = str(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_trigger_mode', 'warmup') or 'warmup').strip().lower().replace('-', '_').replace(' ', '_')
        if trigger_mode not in {'warmup', 'focus_clear'}:
            trigger_mode = 'warmup'
        set_arg(out, '--adaptive-glass-tolerance-trigger-mode', trigger_mode)
        set_arg(out, '--adaptive-glass-tolerance-focus-clear-target', option_int('initial_search_funnel_global_adaptive_glass_tolerance_focus_clear_target', 0, 0))
        if bool(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_stop_when_ready', True)):
            out.append('--adaptive-glass-tolerance-stop-when-ready')
        else:
            out.append('--adaptive-glass-tolerance-no-stop-when-ready')
        if bool(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_bridge_freeze_focus', True)):
            out.append('--adaptive-glass-tolerance-bridge-freeze-focus')
        else:
            out.append('--adaptive-glass-tolerance-no-bridge-freeze-focus')
        set_arg(out, '--adaptive-glass-tolerance-warmup-epochs', option_int('initial_search_funnel_global_adaptive_glass_tolerance_warmup_epochs', 2, 0))
        set_arg(out, '--adaptive-glass-tolerance-elite-fraction', min(1.0, option_float('initial_search_funnel_global_adaptive_glass_tolerance_elite_fraction', 0.2, 1e-06)))
        set_arg(out, '--adaptive-glass-tolerance-quantile', min(1.0, option_float('initial_search_funnel_global_adaptive_glass_tolerance_quantile', 0.0, 0.0)))
        set_arg(out, '--adaptive-glass-tolerance-margin', option_float('initial_search_funnel_global_adaptive_glass_tolerance_margin', 0.5, 0.0))
        set_arg(out, '--adaptive-glass-tolerance-floor', option_float('initial_search_funnel_global_adaptive_glass_tolerance_floor', 0.05, 0.0))
        set_arg(out, '--adaptive-glass-tolerance-smoothing', min(1.0, option_float('initial_search_funnel_global_adaptive_glass_tolerance_smoothing', 0.75, 0.0)))
        set_arg(out, '--adaptive-glass-tolerance-decay', min(1.0, option_float('initial_search_funnel_global_adaptive_glass_tolerance_decay', 0.75, 0.0)))
        set_arg(out, '--adaptive-glass-tolerance-early-stop-max', option_float('initial_search_funnel_global_adaptive_glass_tolerance_early_stop_max', 0.5, 0.0))
    global_near_trace = bool(_flow.settings.options.get('initial_search_funnel_global_near_trace_enabled', True))
    if global_near_trace and _flow.settings.adaptive_initial:
        out.append('--adaptive-allow-near-trace-infeasible')
        global_near_trace_min_coverage = min(1.0, max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_near_trace_min_coverage'), 0.95)))
        set_arg(out, '--trace-coverage-epsilon', 1.0 - global_near_trace_min_coverage)
        global_refresh_trace_gate = bool(_flow.settings.options.get('initial_search_funnel_global_refresh_trace_feasible_gate', True)) or global_near_trace
        set_bool_flag(out, global_refresh_trace_gate, '--refresh-trace-feasible-gate', '--refresh-no-trace-feasible-gate')
        set_bool_flag(out, bool(_flow.settings.options.get('initial_search_funnel_global_basin_active_require_trace_feasible', True)), '--basin-active-require-trace-feasible', '--basin-active-allow-trace-infeasible')
    if _flow.settings.adaptive_initial and (not disable_basins) and bool(_flow.settings.options.get('initial_search_funnel_global_micro_island_enabled', False)):
        out.append('--adaptive-micro-island')
        out.append('--adaptive-micro-island-replace-polish' if bool(_flow.settings.options.get('initial_search_funnel_global_micro_island_replace_polish', True)) else '--adaptive-micro-island-keep-polish')
        set_arg(out, '--adaptive-micro-island-count', option_int('initial_search_funnel_global_micro_island_count', 0, 0))
        micro_island_popsize = _flow.settings.options.get('initial_search_funnel_global_micro_island_popsize')
        if micro_island_popsize is not None and micro_island_popsize != '':
            set_arg(out, '--adaptive-micro-island-popsize', option_float('initial_search_funnel_global_micro_island_popsize', 6.0, 1e-06))
        elif _flow.settings.options.get('initial_search_funnel_global_micro_island_population') not in {None, ''}:
            set_arg(out, '--adaptive-micro-island-population', option_int('initial_search_funnel_global_micro_island_population', 96, 8))
        set_arg(out, '--adaptive-micro-island-outer', option_int('initial_search_funnel_global_micro_island_outer', 1, 1))
        set_arg(out, '--adaptive-micro-island-de-gen', option_int('initial_search_funnel_global_micro_island_de_gen', 24, 1))
        set_arg(out, '--adaptive-micro-island-seed-scale', option_float('initial_search_funnel_global_micro_island_seed_scale', 0.65, 0.0))
        raw_micro_island_polish_candidates = option_int('initial_search_funnel_global_micro_island_polish_candidates', -1, -1)
        if raw_micro_island_polish_candidates < 0:
            resolved_micro_island_polish_candidates = int(math.ceil(max(0, option_int('initial_search_polish_candidates', 8, 0)) * 0.5))
        else:
            resolved_micro_island_polish_candidates = raw_micro_island_polish_candidates
        resolved_micro_island_polish_candidates = max(1, resolved_micro_island_polish_candidates)
        raw_micro_island_polish_refill = option_int('initial_search_funnel_global_micro_island_polish_refill_max_extra', -1, -1)
        if raw_micro_island_polish_refill < 0:
            global_micro_refill_base = option_int('initial_search_polish_refill_max_extra', option_int('initial_search_polish_candidates', 8, 0), -1)
            if global_micro_refill_base < 0:
                global_micro_refill_base = option_int('initial_search_polish_candidates', resolved_micro_island_polish_candidates, 0)
            resolved_micro_island_polish_refill = int(math.ceil(max(0, global_micro_refill_base) * 0.5))
        else:
            resolved_micro_island_polish_refill = raw_micro_island_polish_refill
        resolved_micro_island_polish_refill = max(0, resolved_micro_island_polish_refill)
        set_arg(out, '--adaptive-micro-island-polish-candidates', resolved_micro_island_polish_candidates)
        set_arg(out, '--adaptive-micro-island-polish-refill-max-extra', resolved_micro_island_polish_refill)
        set_arg(out, '--adaptive-micro-island-survivors', option_int('initial_search_funnel_global_micro_island_survivors', 2, 1))
        set_arg(out, '--adaptive-micro-island-family-seed-limit', option_int('initial_search_funnel_global_micro_island_family_seed_limit', 24, 0))
        set_arg(out, '--adaptive-micro-island-refresh-archive-seed-limit', option_int('initial_search_funnel_global_micro_island_refresh_archive_seed_limit', 24, 0))
        if bool(_flow.settings.options.get('initial_search_funnel_global_micro_island_early_stop_enabled', False)):
            out.append('--adaptive-micro-island-early-stop')
        out.append('--adaptive-micro-island-retire-on-early-stop' if bool(_flow.settings.options.get('initial_search_funnel_global_micro_island_retire_on_early_stop', True)) else '--adaptive-micro-island-no-retire-on-early-stop')
        set_arg(out, '--adaptive-micro-island-early-stop-min-epochs', option_int('initial_search_funnel_global_micro_island_early_stop_min_epochs', 3, 1))
        set_arg(out, '--adaptive-micro-island-early-stop-window', option_int('initial_search_funnel_global_micro_island_early_stop_window', 1, 1))
        set_arg(out, '--adaptive-micro-island-early-stop-min-rel-improve', option_float('initial_search_funnel_global_micro_island_early_stop_min_rel_improve', 0.01, 0.0))
        set_arg(out, '--adaptive-micro-island-early-stop-min-abs-improve', option_float('initial_search_funnel_global_micro_island_early_stop_min_abs_improve', 0.01, 0.0))
    if bool(_flow.settings.options.get('initial_search_funnel_global_early_stop_enabled', True)):
        metric_value = str(_flow.settings.options.get('initial_search_funnel_global_early_stop_metric', 'topology_discovery') or 'topology_discovery')
        out.append('--adaptive-early-stop')
        set_arg(out, '--adaptive-early-stop-metric', metric_value)
        set_arg(out, '--adaptive-early-stop-min-epochs', option_int('initial_search_funnel_global_early_stop_min_epochs', 4, 1))
        set_arg(out, '--adaptive-early-stop-window', option_int('initial_search_funnel_global_early_stop_window', 2, 1))
        set_arg(out, '--adaptive-early-stop-min-topologies', option_int('initial_search_funnel_global_early_stop_min_topologies', 20, 0))
        set_arg(out, '--adaptive-early-stop-min-rel-improve', option_float('initial_search_funnel_global_early_stop_min_rel_improve', 0.01, 0.0))
        set_arg(out, '--adaptive-early-stop-min-abs-improve', option_float('initial_search_funnel_global_early_stop_min_abs_improve', 0.01, 0.0))
        if bool(_flow.settings.options.get('initial_search_funnel_global_early_stop_include_initial_point', False)):
            out.append('--adaptive-early-stop-include-initial-point')
        if bool(_flow.settings.options.get('initial_search_funnel_global_early_stop_require_topology_plateau', False)):
            out.append('--adaptive-early-stop-require-topology-plateau')
        normalized_metric = metric_value.strip().lower().replace('-', '_').replace(' ', '_')
        topology_discovery_metric = normalized_metric in {'topology_discovery', 'family_discovery', 'topology_count', 'family_count'}
        if topology_discovery_metric and bool(_flow.settings.options.get('initial_search_funnel_global_early_stop_qualified_topologies', True)):
            set_arg(out, '--adaptive-early-stop-best-merit-max', option_float('initial_search_funnel_global_early_stop_best_merit_max', 200.0, 0.0))
            out.append('--adaptive-early-stop-qualified-topologies')
            set_arg(out, '--adaptive-early-stop-qualified-topology-mode', str(_flow.settings.options.get('initial_search_funnel_global_early_stop_qualified_topology_mode', 'merit') or 'merit'))
            set_arg(out, '--adaptive-early-stop-qualified-topology-epsilon', option_float('initial_search_funnel_global_early_stop_qualified_topology_epsilon', 0.02, 0.0))
            set_arg(out, '--adaptive-early-stop-qualified-topology-merit-ratio', option_float('initial_search_funnel_global_early_stop_qualified_topology_merit_ratio', 5.0, 0.0))
            set_arg(out, '--adaptive-early-stop-qualified-topology-merit-floor', option_float('initial_search_funnel_global_early_stop_qualified_topology_merit_floor', 200.0, 0.0))
            set_arg(out, '--adaptive-early-stop-qualified-topology-merit-delta', option_float('initial_search_funnel_global_early_stop_qualified_topology_merit_delta', 250.0, 0.0))
            set_arg(out, '--adaptive-early-stop-qualified-topology-merit-max', option_float('initial_search_funnel_global_early_stop_qualified_topology_merit_max', 0.0, 0.0))
    if _flow.settings.adaptive_initial:
        enforce_standard_jade_mutation(out)
    if no_polish:
        out.append('--no-polish')
    return out
