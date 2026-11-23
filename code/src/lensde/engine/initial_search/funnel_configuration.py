"""Configure shared funnel, CRA-tightening, restart and cache state before exploration."""
from __future__ import annotations
import copy
from pathlib import Path
from .. import commands as _lensde_commands
from .. import configuration as _lensde_configuration
from .. import defaults as _lensde_defaults
from .. import materials as _lensde_materials
from .. import provenance as _lensde_provenance
from . import material_handoff as _m_material_handoff
from .contracts import SearchState, PhaseResult

def run_phase(_flow: SearchState) -> PhaseResult:
    """Configure shared funnel, CRA-tightening, restart and cache state before exploration."""
    _flow.exploration.stage_variables_file_cache: dict[str, list[dict]] = {}
    _flow.exploration.stage_context_cache: dict[tuple[str, str, str], dict] = {}
    _flow.exploration.source_topology_feature_cache: dict[tuple[float, ...], dict] = {}
    _flow.exploration.stage_topology_feature_cache: dict[tuple[str, str, str, tuple[float, ...]], dict] = {}
    _flow.exploration.global_explore_stage_context: dict | None = None
    global_develop_stage_context: dict | None = None
    global_develop_materialized = False
    global_develop_materialize_only = False
    material_stage_outers: tuple[int, int, int] | None = None
    initial_stage_outer = _flow.settings.staged_search_outer if _flow.optics.merit_strategy == 'staged' else _flow.settings.hybrid_outer
    if _flow.settings.material_stages_enabled:
        ok, message = _lensde_materials.initial_material_stage_requirements_ok(_flow.optics.base_system, _flow.optics.variables)
        if not ok:
            return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 11, '', message))
        material_stage_outers = _lensde_materials.allocate_three_stage_outer(_flow.settings.hybrid_outer, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_material_stage_weak_ratio'), 0.3), _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_material_stage_proximity_ratio'), 0.3), _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_material_stage_discrete_ratio'), 0.4))
        initial_stage_outer = material_stage_outers[0]
    _flow.exploration.funnel_stage_material_modes = _lensde_defaults.normalize_funnel_material_stage_options(_flow.settings.options)
    _flow.exploration.funnel_material_mode = _flow.exploration.funnel_stage_material_modes['develop']
    funnel_global_develop_mode = _lensde_defaults.canonical_funnel_global_develop_mode(_flow.settings.options.get('initial_search_funnel_global_develop_mode', 'mixed'))
    _flow.settings.options['initial_search_funnel_global_develop_mode'] = funnel_global_develop_mode
    funnel_global_develop_lpsr_enabled = bool(_flow.settings.options.get('initial_search_funnel_global_develop_lpsr_enabled', True))
    _flow.settings.options['initial_search_funnel_global_develop_lpsr_enabled'] = funnel_global_develop_lpsr_enabled
    funnel_has_material_variables = any((variable.get('kind') in {'material_nd', 'material_vd', 'glass_choice'} for variable in _flow.optics.variables))
    funnel_develop_materialized = True and funnel_has_material_variables and _m_material_handoff.funnel_stage_requests_real_glass(_flow, 'develop')
    funnel_materialized = True and funnel_has_material_variables and any((mode in {'fixed', 'discrete'} for mode in _flow.exploration.funnel_stage_material_modes.values()))
    funnel_glass_candidate_count = max(1, int(_flow.settings.options.get('initial_search_funnel_glass_candidates', 5) or 5))
    initial_search_label = '[GUI] Topology greedy graft initial structure search\n' if False else '[GUI] DE + DLS material funnel initial structure search\n' if True else '[GUI] Flat topology funnel initial structure search\n' if True else '[GUI] Adaptive DE basin-restart initial structure search\n' if _flow.settings.basin_de_initial else '[GUI] Adaptive hybrid initial structure search\n' if _flow.settings.adaptive_initial else '[GUI] Hybrid initial structure search\n'
    _flow.runtime.stdout_parts = [initial_search_label, f'[GUI] Flow={_flow.settings.flow}, wide_angle_prior={_flow.settings.wide_angle_prior}, front_pairs={_flow.seeding.front_pairs}\n', f"[GUI] C++ timeout: {(_flow.runtime.timeout_seconds if _flow.runtime.timeout_seconds > 0 else 'disabled')} seconds\n", f'[GUI] Run dir: {_flow.inputs.run_dir}\n', f"[GUI] Optimizer={_flow.settings.search_optimizer_mode}, population={_flow.settings.population}, hybrid_outer={_flow.settings.hybrid_outer}, hybrid_de_gen={_flow.settings.hybrid_de_gen}, polish={('off' if _flow.settings.initial_polish_disabled else 'on')}\n", f'[GUI] Population sizing: effective_variables={_flow.seeding.global_effective_variable_count}, global_pop={_flow.settings.population}, seed_pool_target={_flow.seeding.seed_pool_target} (base={_flow.seeding.base_seed_pool_target}, x{_flow.seeding.seed_pool_multiplier:g}), refresh_pool_target={_flow.seeding.refresh_pool_target} (x{_flow.seeding.refresh_pool_multiplier:g})\n', f"[GUI] Initial population generation={_lensde_configuration.canonical_population_generation_mode(_flow.settings.options.get('initial_search_population_generation_mode'), 'restricted_perturbation')}, local_group={bool(_flow.settings.options.get('initial_search_population_local_group_perturbation', False))}, local_long_air_gap_prior={bool(_flow.settings.options.get('initial_search_population_local_long_air_gap_prior', True))}, trace_gate={bool(_flow.settings.options.get('initial_search_population_trace_feasible_gate', False))}\n", f'[GUI] Merit strategy={_flow.optics.merit_strategy}' + (f', search_outer={_flow.settings.staged_search_outer}, full_outer={_flow.settings.staged_full_outer}\n' if _flow.optics.merit_strategy == 'staged' else '\n')]
    if _flow.seeding.full_restart_shared_seed_pool_enabled:
        _flow.runtime.stdout_parts.append(f'[GUI] Explore full-restart shared seed pool=enabled, seed_restarts={_flow.exploration.full_restart_explore_outer_seed_budget}, per_restart_raw={_flow.seeding.full_restart_per_outer_seed_pool_target}, shared_target={_flow.seeding.full_restart_shared_seed_pool_target}, seed_pool_multiplier={_flow.seeding.full_restart_seed_pool_multiplier:g}\n')
    _flow.runtime.stdout_parts.append(f"[GUI] Funnel material modes=develop:{_m_material_handoff.funnel_material_mode_for_stage(_flow, 'develop')}, probe:{_m_material_handoff.funnel_material_mode_for_stage(_flow, 'probe')}, focus:{_m_material_handoff.funnel_material_mode_for_stage(_flow, 'focus')}, refine:{_m_material_handoff.funnel_material_mode_for_stage(_flow, 'refine')}" + (f', glass_candidates={funnel_glass_candidate_count}\n' if 'discrete' in _flow.exploration.funnel_stage_material_modes.values() else '\n'))
    _flow.runtime.stdout_parts.append(f'[GUI] Funnel global develop mode={funnel_global_develop_mode}\n')
    _flow.runtime.stdout_parts.append(f"[GUI] Funnel global develop LPSR={('enabled' if funnel_global_develop_lpsr_enabled else 'disabled')}\n")
    _flow.runtime.stdout_parts.append(f"[GUI] Funnel global DLS polish={('enabled' if _flow.exploration.funnel_global_polish_enabled else 'disabled')}\n")
    _flow.runtime.stdout_parts.append(f"[GUI] Funnel global micro-island={('enabled' if _flow.exploration.funnel_global_micro_island_enabled else 'disabled')}" + (f", replace_polish={('yes' if bool(_flow.settings.options.get('initial_search_funnel_global_micro_island_replace_polish', True)) else 'no')}, islands={max(0, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_count', 0) or 0))}, {_flow.exploration.funnel_global_micro_island_size_summary}, outer={max(1, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_outer', 1) or 1))}, de_gen={max(1, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_de_gen', 24) or 24))}, seed_scale={max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_micro_island_seed_scale'), 0.65)):.3g}, survivors={max(1, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_survivors', 2) or 2))}, family_seed_limit={max(0, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_family_seed_limit', 24) or 24))}, refresh_seed_limit={max(0, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_refresh_archive_seed_limit', 24) or 24))}, early_stop={('yes' if bool(_flow.settings.options.get('initial_search_funnel_global_micro_island_early_stop_enabled', False)) else 'no')}, retire_on_stop={('yes' if bool(_flow.settings.options.get('initial_search_funnel_global_micro_island_retire_on_early_stop', True)) else 'no')}, stop_min/window={max(1, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_early_stop_min_epochs', 3) or 3))}/{max(1, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_early_stop_window', 1) or 1))}, stop_rel/abs={max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_micro_island_early_stop_min_rel_improve'), 0.01)):.3g}/{max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_micro_island_early_stop_min_abs_improve'), 0.01)):.3g}\n" if _flow.exploration.funnel_global_micro_island_enabled else '\n'))
    _flow.runtime.stdout_parts.append(f"[GUI] Funnel global near trace={('enabled' if bool(_flow.settings.options.get('initial_search_funnel_global_near_trace_enabled', True)) else 'disabled')}, min_coverage={_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_near_trace_min_coverage'), 0.95):.3g}\n")
    _flow.runtime.stdout_parts.append(f"[GUI] Funnel global plateau retire clear={('enabled' if bool(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_evict', True)) else 'disabled')}, frontier_count={max(1, int(_flow.settings.options.get('initial_search_funnel_global_plateau_frontier_count', 1) or 1))}, frontier_multiplier={max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_plateau_frontier_multiplier'), 0.0)):.3g}, parameter_only={('yes' if bool(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_evict_parameter_only', True)) else 'no')}, parameter_distance={max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_evict_parameter_distance'), 0.15)):.3g}, evict_fraction={min(1.0, max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_evict_fraction'), 1.0))):.3g}, max_fraction={min(1.0, max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_evict_max_fraction'), 0.2))):.3g}, force_first_frontier={('yes' if bool(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_frontier_on_first_update', False)) else 'no')}, defer_early_stop_epochs={max(0, int(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_defer_early_stop_epochs', 0) or 0))}, stop_after_clears={max(0, int(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_stop_after_clears', 0) or 0))}\n")
    _flow.runtime.stdout_parts.append(f"[GUI] Funnel global adaptive glass tolerance={('enabled' if _flow.optics.continuous_glass_constraints_count > 0 and bool(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_enabled', True)) else 'disabled')}, constraints={_flow.optics.continuous_glass_constraints_count}, trigger={str(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_trigger_mode', 'warmup') or 'warmup')}, focus_clear_target={max(0, int(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_focus_clear_target', 0) or 0))}, stop_when_ready={bool(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_stop_when_ready', True))}, freeze_focus={bool(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_bridge_freeze_focus', True))}, warmup={max(0, int(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_warmup_epochs', 2) or 2))}, elite_fraction={min(1.0, max(1e-06, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_elite_fraction'), 0.2))):.3g}, margin={max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_margin'), 0.5)):.3g}, floor={max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_floor'), 0.05)):.3g}, smoothing={min(1.0, max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_smoothing'), 0.75))):.3g}, decay={min(1.0, max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_decay'), 0.75))):.3g}, early_stop_max={max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_adaptive_glass_tolerance_early_stop_max'), 0.5)):.3g}\n")
    _flow.runtime.stdout_parts.extend(_flow.seeding.seed_gate_stdout_parts)
    _flow.runtime.stderr_parts: list[str] = list(_flow.seeding.seed_gate_stderr_parts)
    _flow.runtime.stages = list(_flow.seeding.seed_gate_stages)
    final_stage_name = 'hybrid_initial'
    final_stage_dir = _flow.settings.search_dir
    _flow.optics.final_stage_merit = _flow.optics.initial_stage_merit
    _flow.optics.final_merit_path = _flow.optics.merit_path
    final_stage_system_input = _flow.optics.system_path
    final_variables = _flow.optics.variables
    final_variables_path = _flow.optics.variables_path
    final_glass_search_config = _flow.optics.glass_search_config
    final_glass_search_path = _flow.optics.glass_search_path
    _flow.exploration.funnel_summary: dict = {'global': [], 'probe_cycles': [], 'probe': [], 'focus': [], 'topology_graft': [], 'refine_scout': [], 'refine': []}
    requested_probe_cycle_enabled = bool(_flow.settings.options.get('initial_search_funnel_probe_cycle_enabled', False))
    _flow.settings.probe_cycle_enabled = requested_probe_cycle_enabled
    if True and requested_probe_cycle_enabled:
        _flow.settings.probe_cycle_enabled = False
        _flow.settings.options['initial_search_funnel_probe_cycle_enabled'] = False
        _flow.settings.options['initial_search_funnel_probe_cycle_requested'] = True
        _flow.runtime.stdout_parts.append('[GUI] Probe-cycle is disabled for de_dls_material_funnel P0; using full-restart archive plus one DLS/material funnel instead.\n')
    _flow.exploration.resume_existing_funnel_stages = bool(_flow.settings.options.get('initial_search_resume_existing_funnel_stages', False))
    probe_cycle_global_dirs: list[Path] = []
    explore_develop_extra_stage_entries: list[dict] = []
    cra_tighten_requested = _lensde_commands.option_truthy(_flow.settings.options.get('initial_search_funnel_global_cra_tighten_enabled'), False)
    _flow.exploration.cra_tighten_enabled = cra_tighten_requested and _lensde_commands.option_truthy(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_enabled'), False)
    _flow.exploration.cra_tighten_generations = max(1, int(_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_cra_tighten_generations'), 1200.0)))
    _flow.exploration.cra_start_tolerance = max(1e-09, _lensde_configuration.finite_float(_flow.optics.initial_stage_merit.get('quality_chief_ray_angle_tol'), 5.0))
    _flow.exploration.cra_tightened_tolerance = max(1e-09, _lensde_configuration.finite_float(_flow.optics.initial_stage_merit.get('quality_chief_ray_angle_tightened_tol'), 0.5))
    _flow.exploration.cra_tightened_outer_root = _flow.inputs.run_dir / 'funnel_global_explore_cra_tightened_outers'
    _flow.exploration.cra_tightened_merit_path = _flow.settings.search_dir / 'merit_cra_tightened.json'
    _flow.exploration.cra_tightened_merit = copy.deepcopy(_flow.optics.initial_stage_merit)
    _flow.exploration.cra_tightened_merit['quality_chief_ray_angle_tol'] = _flow.exploration.cra_tightened_tolerance
    _flow.exploration.cra_tightened_merit['quality_chief_ray_angle_tightened_tol'] = _flow.exploration.cra_tightened_tolerance
    _flow.exploration.cra_source_merit_fingerprint = _lensde_provenance.json_fingerprint(_flow.optics.initial_stage_merit)
    _flow.exploration.cra_tightened_merit_fingerprint = _lensde_provenance.json_fingerprint(_flow.exploration.cra_tightened_merit)
    if _flow.exploration.cra_tighten_enabled:
        _lensde_provenance.write_json(_flow.exploration.cra_tightened_merit_path, _flow.exploration.cra_tightened_merit)
        _flow.runtime.stdout_parts.append(f'[GUI] Outer CRA tighten enabled: tolerance={_flow.exploration.cra_start_tolerance:g}->{_flow.exploration.cra_tightened_tolerance:g} deg, additional_generations={_flow.exploration.cra_tighten_generations}; tightened outputs will drive checkpoint and downstream stages.\n')
    elif cra_tighten_requested:
        _flow.runtime.stderr_parts.append('[funnel] Outer CRA tighten requires Explore full-restart mode; the requested tighten continuation is disabled for this run.\n')
    _flow.optics.downstream_funnel_merit_path = _flow.exploration.cra_tightened_merit_path if _flow.exploration.cra_tighten_enabled else _flow.optics.merit_path
    _flow.optics.downstream_funnel_merit = _flow.exploration.cra_tightened_merit if _flow.exploration.cra_tighten_enabled else _flow.optics.initial_stage_merit
    return PhaseResult(False, None)
