"""Prepare the retained paper flow, optical inputs and seed settings in their original order."""
from __future__ import annotations
import copy
import math
import time
from lensde.runtime import native_io_paths
from lensde.seeds import protocol as seed_protocol
from .. import state
from .. import commands as _lensde_commands
from .. import configuration as _lensde_configuration
from .. import defaults as _lensde_defaults
from .. import fields as _lensde_fields
from .. import geometry as _lensde_geometry
from .. import initialization as _lensde_initialization
from .. import materials as _lensde_materials
from .. import populations as _lensde_populations
from .. import provenance as _lensde_provenance
from .contracts import SearchState, PhaseResult

def run_phase(_flow: SearchState) -> PhaseResult:
    """Prepare the retained paper flow, optical inputs and seed settings in their original order."""
    if _flow.inputs.project.get('options', {}).get('initial_search_optimizer_mode', 'de_dls_material_funnel') != 'de_dls_material_funnel':
        raise ValueError('Only the retained paper initial-search workflow is supported')
    if seed_protocol.policy(_flow.inputs.project.get('options', {})) == seed_protocol.NUMERIC_POLICY:
        _flow.inputs.project = _lensde_configuration.finish_project(copy.deepcopy(_flow.inputs.project))
    if not (_flow.inputs.run_dir / 'run_meta.json').is_file():
        native_io_paths.enable_compact_names(_flow.inputs.run_dir)
    _flow.settings.options = copy.deepcopy(_flow.inputs.project.get('options', {}))
    _lensde_initialization.normalize_initial_search_asphere_options(_flow.settings.options)
    _flow.settings.search_optimizer_mode = _lensde_defaults.canonical_initial_optimizer_mode(_flow.settings.options.get('initial_search_optimizer_mode', state.INITIAL_SEARCH_FUNNEL_MODE))
    explore_develop_only = False
    topology_greedy_graft = False
    de_dls_material_funnel = True
    greedy_global_pool_mode = False
    funnel_initial = True
    _lensde_defaults.apply_de_dls_material_funnel_defaults(_flow.settings.options)
    _lensde_defaults.apply_initial_search_run_profile_defaults(_flow.settings.options)
    _lensde_configuration.sync_unified_ray_aiming_options(_flow.settings.options)
    _lensde_initialization.normalize_initial_search_asphere_options(_flow.settings.options)
    seed_protocol.apply_options(_flow.settings.options)
    _flow.seeding.seed_strategy = _lensde_populations.canonical_initial_seed_strategy(_flow.settings.options.get('initial_search_seed_trace_strategy'), 'anchor_sparse_refill')
    _flow.settings.options['initial_search_seed_trace_strategy'] = _flow.seeding.seed_strategy
    _flow.settings.mechanical_direct_seed = _flow.seeding.seed_strategy == state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY
    if _flow.settings.mechanical_direct_seed:
        _flow.settings.options['initial_search_population_trace_feasible_gate'] = False
        _flow.settings.options['initial_search_final_strict_recheck'] = False
        _flow.settings.options['initial_search_anchor_cloud_coverage_injection'] = False
        _flow.settings.options['initial_search_asphere_scout_enabled'] = False
    _flow.inputs.project = {**_flow.inputs.project, 'options': _flow.settings.options}
    _flow.settings.flow = str(_flow.settings.options.get('initial_search_flow', 'wide_angle')).strip().lower().replace('-', '_')
    _flow.settings.wide_angle_prior = _flow.settings.flow in {'wide', 'wide_angle', 'ultrawide', 'ultra_wide'}
    if _flow.settings.flow not in {'legacy', 'wide', 'wide_angle', 'ultrawide', 'ultra_wide'}:
        _flow.settings.wide_angle_prior = False
        _flow.settings.flow = 'legacy'
    _flow.settings.hybrid_outer = max(1, int(_flow.settings.options.get('initial_search_hybrid_outer', 10) or 10))
    _flow.settings.hybrid_de_gen = max(1, int(_flow.settings.options.get('initial_search_hybrid_de_gen', 120) or 120))
    _flow.settings.generations = _flow.settings.hybrid_de_gen
    _flow.settings.hybrid_parents = max(1, int(_flow.settings.options.get('initial_search_hybrid_parents', 4) or 4))
    _flow.settings.hybrid_polish_workers = max(0, int(_flow.settings.options.get('initial_search_hybrid_polish_workers', 0) or 0))
    _flow.settings.hybrid_refresh = max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_hybrid_refresh'), 0.3))
    _flow.settings.seed = seed_protocol.initial_seed(_flow.settings.options)
    _flow.runtime.use_cuda = bool(_flow.settings.options.get('use_cuda', False))
    basin_de_modes = {'adaptive_de_basin_restart', 'de_basin_restart', 'basin_de_restart', 'basin_de', 'de_basin'}
    _flow.settings.basin_de_initial = _flow.settings.search_optimizer_mode in basin_de_modes
    _flow.settings.adaptive_initial = _flow.settings.search_optimizer_mode in {'adaptive', 'adaptive_hybrid', 'adaptive_hybrid_de_dls'} or _flow.settings.basin_de_initial or True
    _flow.settings.initial_polish_disabled = _flow.settings.basin_de_initial or bool(_flow.settings.options.get('initial_search_disable_polish', False))
    _flow.exploration.funnel_global_polish_enabled = True and bool(_flow.settings.options.get('initial_search_funnel_global_enable_polish', True)) and (not _flow.settings.initial_polish_disabled)
    _flow.exploration.funnel_global_micro_island_enabled = True and _flow.settings.adaptive_initial and bool(_flow.settings.options.get('initial_search_funnel_global_micro_island_enabled', False))
    funnel_global_micro_island_popsize = _flow.settings.options.get('initial_search_funnel_global_micro_island_popsize')
    if funnel_global_micro_island_popsize not in {None, ''}:
        _flow.exploration.funnel_global_micro_island_size_summary = f'popsize={max(1e-06, _lensde_configuration.finite_float(funnel_global_micro_island_popsize, 6.0)):.3g}'
    else:
        _flow.exploration.funnel_global_micro_island_size_summary = f"pop={max(8, int(_flow.settings.options.get('initial_search_funnel_global_micro_island_population', 96) or 96))}"
    _flow.runtime.exe = _lensde_provenance.exe_path(_flow.runtime.use_cuda)
    _flow.runtime.timeout_seconds = _lensde_materials.run_timeout_seconds(_flow.inputs.project)
    _flow.runtime.started_at = time.time()
    _flow.settings.search_dir = _flow.inputs.run_dir / 'hybrid_initial'
    strict_dir = _flow.inputs.run_dir / 'strict_recheck'
    _flow.settings.search_dir.mkdir(parents=True, exist_ok=True)
    strict_dir.mkdir(parents=True, exist_ok=True)
    _flow.optics.base_system = _lensde_geometry.make_first_order_system(_flow.inputs.project, full_field=True)
    if bool(_flow.settings.options.get('initial_search_flat_start', True)):
        _flow.optics.base_system = _lensde_fields.make_flat_search_system(_flow.optics.base_system)
    _flow.optics.base_system = _lensde_initialization.apply_initial_search_post_asphere_material_policy(_flow.optics.base_system, _flow.settings.options)
    _flow.seeding.initial_curvature_bound = max(1e-06, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_curvature_bound'), state.INITIAL_SEARCH_CURVATURE_BOUND))
    curvature_domain_bound = _lensde_fields.initial_search_curvature_domain_bound(_flow.settings.options)
    _flow.seeding.adaptive_thickness_bounds = bool(_flow.settings.options.get('initial_search_adaptive_thickness_bounds', True))
    _flow.optics.merit_strategy = str(_flow.settings.options.get('initial_search_merit_strategy', 'full')).strip().lower().replace('-', '_')
    if _flow.optics.merit_strategy == 'current':
        _flow.optics.merit_strategy = 'search'
    elif _flow.optics.merit_strategy in {'full_merit', 'complete'}:
        _flow.optics.merit_strategy = 'full'
    elif _flow.optics.merit_strategy in {'stage', 'staged_full'}:
        _flow.optics.merit_strategy = 'staged'
    if _flow.optics.merit_strategy not in {'search', 'full', 'staged'}:
        _flow.optics.merit_strategy = 'full'
    if _flow.optics.merit_strategy == 'staged' and _flow.settings.hybrid_outer <= 1:
        _flow.optics.merit_strategy = 'full'
    _flow.settings.staged_search_outer = 0
    _flow.settings.staged_full_outer = 0
    if _flow.optics.merit_strategy == 'staged':
        _flow.settings.staged_search_outer = min(max(1, _flow.settings.hybrid_outer // 5), _flow.settings.hybrid_outer - 1)
        _flow.settings.staged_full_outer = _flow.settings.hybrid_outer - _flow.settings.staged_search_outer
    initial_thickness_constraints = _lensde_geometry.initial_search_thickness_constraints(_flow.settings.options, _flow.inputs.run_merit, _flow.optics.merit_strategy)
    requested_bfl_surface = int(_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_bfl_reference_surface'), -1))
    _flow.seeding.bfl_surface_index = _lensde_geometry.resolve_bfl_reference_surface(_flow.optics.base_system, requested_bfl_surface if requested_bfl_surface >= 0 else None)
    _flow.optics.base_system['bfl_reference_surface'] = _flow.seeding.bfl_surface_index
    bfl_lower_factor = _lensde_populations.clamp_value(_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_bfl_lower_factor'), 0.9), 0.0, 1.0)
    bfl_upper_factor = max(1.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_bfl_upper_factor'), 1.5))
    use_current_thickness_prior = bool(_flow.settings.options.get('initial_search_use_current_thickness_prior', False))
    base_variables = _lensde_commands.sync_variables_from_system(_lensde_fields.make_first_order_variables(_flow.inputs.project, _flow.optics.base_system, _flow.inputs.target_bfl, _flow.inputs.target_ttl, curvature_domain_bound, allow_default_variables=False, adaptive_thickness=_flow.seeding.adaptive_thickness_bounds, target_efl=_flow.inputs.target_efl, thickness_constraints=initial_thickness_constraints, bfl_surface_index=_flow.seeding.bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor, use_current_thickness_prior=use_current_thickness_prior), _flow.optics.base_system)
    if not base_variables:
        return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 10, '', '初始结构搜索需要至少一个已勾选的曲率或厚度变量。'))
    _flow.seeding.rest_cap = curvature_domain_bound
    _flow.seeding.front_groups = max(0, min(2, int(_flow.settings.options.get('initial_search_wide_front_groups', 1) or 1)))
    _flow.optics.variables, _flow.seeding.front_pairs = _lensde_populations.make_hybrid_initial_search_variables(_flow.optics.base_system, base_variables, wide_angle_prior=_flow.settings.wide_angle_prior, front_groups=_flow.seeding.front_groups, front_profile=str(_flow.settings.options.get('initial_search_front_profile', 'weak')), rest_cap=_flow.seeding.rest_cap, target_ttl=_flow.inputs.target_ttl, target_bfl=_flow.inputs.target_bfl, target_efl=_flow.inputs.target_efl, adaptive_thickness=_flow.seeding.adaptive_thickness_bounds, thickness_constraints=initial_thickness_constraints, bfl_surface_index=_flow.seeding.bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor, use_current_thickness_prior=use_current_thickness_prior)
    _flow.optics.variables = _lensde_populations.append_initial_search_live_variables(_flow.optics.base_system, _flow.optics.variables, _flow.inputs.project.get('variables', []), _flow.settings.options)
    try:
        _flow.optics.base_system, _flow.optics.variables = _lensde_initialization.prepare_initial_search_asphere_system_and_variables(_flow.optics.base_system, _flow.optics.variables, _flow.settings.options)
    except ValueError as exc:
        return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 10, '', str(exc)))
    if not _flow.optics.variables:
        return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 10, '', '初始结构搜索没有生成可优化变量。'))
    try:
        material_wavelength_audit = _lensde_materials.assert_system_material_wavelength_coverage(_flow.optics.base_system, _flow.optics.variables)
    except ValueError as exc:
        return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 10, '', str(exc)))
    _lensde_provenance.write_json(_flow.inputs.run_dir / 'material_wavelength_audit.json', material_wavelength_audit)
    _flow.optics.search_merit = _lensde_fields.make_trace_gate_first_order_merit({**_flow.inputs.project, 'options': _flow.settings.options}, rms_upper=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_trace_rms_upper'), 0.0), min_edge=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_first_order_min_edge'), 1.0), edge_weight=max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_first_order_edge_weight'), 5000.0)))
    _flow.optics.search_merit['merit_model'] = _lensde_defaults.sanitize_merit_model(_flow.settings.options.get('initial_search_merit_model'), 'unified_trace_gate')
    _flow.optics.search_merit['trace_policy'] = str(_flow.settings.options.get('initial_search_trace_policy', 'soft_coverage'))
    _flow.optics.search_merit['min_field_coverage'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_min_field_coverage'), 1.0)
    _flow.optics.search_merit['min_system_coverage'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_min_system_coverage'), 1.0)
    _flow.optics.search_merit['min_coverage_for_image_merit'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_min_coverage_for_image_merit'), 0.5)
    _flow.optics.search_merit['trace_failure_weight'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_trace_failure_weight'), 5.0)
    _flow.optics.search_merit['coverage_failure_weight'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_coverage_failure_weight'), 800.0)
    _flow.optics.search_merit['aim_failure_weight'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_aim_failure_weight'), 2.0)
    _flow.optics.search_merit['backward_failure_weight'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_backward_failure_weight'), 10.0)
    _flow.optics.search_merit['require_chief_ray'] = bool(_flow.settings.options.get('initial_search_require_chief_ray', True))
    _flow.optics.search_merit['require_off_axis_field_coverage'] = bool(_flow.settings.options.get('initial_search_require_off_axis_field_coverage', True))
    _flow.optics.search_merit['hard_trace_gate_weight'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_hard_trace_gate_weight'), 20000.0)
    _flow.optics.search_merit['trace_progress_weight'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_trace_progress_weight'), 1000.0)
    _flow.optics.search_merit['chief_progress_weight'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_chief_progress_weight'), 2000.0)
    _flow.optics.search_merit['trace_priority_weight'] = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_trace_priority_weight'), 150.0)
    _lensde_fields.scale_initial_search_operand_weights(_flow.optics.search_merit, first_order_weight_scale=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_first_order_weight_scale'), 10.0), spot_weight_scale=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_spot_weight_scale'), 0.1))
    _flow.optics.full_merit = copy.deepcopy(_flow.inputs.run_merit)
    _flow.optics.full_merit.setdefault('trace_policy', 'soft_coverage')
    _flow.optics.full_merit.setdefault('min_field_coverage', 1.0)
    _flow.optics.full_merit.setdefault('min_system_coverage', 1.0)
    _flow.optics.full_merit.setdefault('min_coverage_for_image_merit', 0.5)
    _flow.optics.full_merit.setdefault('num_rays', _flow.inputs.run_merit.get('num_rays', 3))
    _flow.optics.full_merit.setdefault('pupil_arms', _flow.inputs.run_merit.get('pupil_arms', 6))
    _flow.optics.full_merit.setdefault('pupil_distribution', _flow.inputs.run_merit.get('pupil_distribution', 'gaussian_ring_arm'))
    _flow.optics.full_merit['wavefront_reference'] = _lensde_defaults.canonical_wavefront_reference(_flow.optics.full_merit.get('wavefront_reference', _flow.inputs.run_merit.get('wavefront_reference')), 'centroid_sphere')
    _flow.optics.full_merit.setdefault('sampling_seed', _flow.inputs.run_merit.get('sampling_seed', 1))
    _lensde_fields.scale_initial_search_operand_weights(_flow.optics.full_merit, first_order_weight_scale=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_first_order_weight_scale'), 10.0), spot_weight_scale=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_spot_weight_scale'), 0.1))
    _flow.settings.material_stages_enabled = bool(_flow.settings.options.get('initial_search_material_stages_enabled', False))
    _flow.optics.system_path = _flow.settings.search_dir / 'system_input.json'
    _flow.seeding.seed_variables_path = _flow.settings.search_dir / 'seed_variables.json'
    _flow.settings.optimization_variables_path = _flow.settings.search_dir / 'variables.json'
    _flow.optics.variables_path = _flow.seeding.seed_variables_path
    _flow.optics.glass_search_path = _flow.settings.search_dir / 'glass_search.json'
    _flow.optics.merit_path = _flow.settings.search_dir / 'merit.json'
    _flow.seeding.seed_path = _flow.settings.search_dir / 'seed_population.csv'
    _flow.settings.external_seed_path = _flow.settings.search_dir / 'seed_population_external.csv'
    _flow.optics.glass_search_config = _lensde_materials.prepare_glass_search_config({**_flow.inputs.project, 'system': _flow.optics.base_system}, _flow.optics.variables)
    if _flow.optics.glass_search_config.get('enabled'):
        valid_glass_groups = {int(group.get('id', -1)) for group in _flow.optics.glass_search_config.get('groups', [])}
        _flow.optics.variables = [variable for variable in _flow.optics.variables if variable.get('kind') != 'glass_choice' or int(variable.get('glass_group', -999)) in valid_glass_groups]
    else:
        _flow.optics.variables = [variable for variable in _flow.optics.variables if variable.get('kind') != 'glass_choice']
    _flow.seeding.evaluator_thickness_constraints = dict(initial_thickness_constraints)
    _flow.optics.variables = _lensde_commands.apply_initial_search_bound_overrides(_flow.optics.base_system, _flow.optics.variables, _flow.settings.options, _flow.seeding.evaluator_thickness_constraints)
    _flow.optics.variables = _lensde_commands.apply_de_bound_overrides(_flow.optics.base_system, _flow.optics.variables, _flow.settings.options)
    _flow.optics.variables = _lensde_commands.enforce_thickness_variable_manufacturing_bounds(_flow.optics.base_system, _flow.optics.variables, _flow.seeding.evaluator_thickness_constraints)
    _flow.settings.baseline_bfl_bound_changes: list[dict] = []
    if _flow.settings.mechanical_direct_seed:
        optimization_preview = _lensde_fields.make_post_seed_optimization_variables(_flow.optics.base_system, _flow.optics.variables, _flow.inputs.project.get('variables', []), _flow.settings.options)
        _flow.optics.variables, _flow.settings.baseline_bfl_bound_changes = _lensde_populations.expand_mechanical_stratified_bfl_seed_domain(_flow.optics.base_system, _flow.optics.variables, optimization_preview, _flow.inputs.target_ttl, bfl_ttl_upper_fraction=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_bfl_ttl_upper_fraction'), 0.5), bfl_surface_index=_flow.seeding.bfl_surface_index)
    if not _flow.optics.variables:
        return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 10, '', '初始结构搜索没有生成可优化变量。'))
    _flow.seeding.global_effective_variable_count = _lensde_populations.effective_variable_count(_flow.optics.variables)
    _flow.seeding.global_evaluator_dimension = _lensde_populations.population_sizing_variable_count(_flow.optics.variables)
    configured_popsize = max(1e-06, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_popsize'), 10.0))
    requested_population = max(8, int(math.ceil(_flow.seeding.global_effective_variable_count * configured_popsize)))
    evaluator_population_multiplier = max(4, int(_flow.settings.options.get('initial_search_min_popsize_multiplier', 4) or 4))
    evaluator_population_floor = max(int(_flow.settings.options.get('initial_search_min_population', 20) or 20), evaluator_population_multiplier * _flow.seeding.global_evaluator_dimension)
    _flow.settings.population = max(requested_population, evaluator_population_floor)
    _flow.seeding.seed_pool_multiplier = max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_pool_population_multiplier'), 2.0))
    _flow.seeding.refresh_pool_multiplier = max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_refresh_pool_population_multiplier'), 2.0))
    _flow.seeding.base_seed_pool_target = max(_flow.settings.population, int(math.ceil(_flow.settings.population * _flow.seeding.seed_pool_multiplier)))
    _flow.seeding.full_restart_shared_seed_pool_enabled = True and bool(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_enabled', False)) and bool(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_shared_seed_pool', True))
    _flow.exploration.full_restart_explore_outer_seed_budget = max(1, int(_flow.settings.options.get('initial_search_funnel_global_explore_outer', 24) or 24))
    if bool(_flow.settings.options.get('initial_search_funnel_probe_cycle_enabled', False)):
        _flow.exploration.full_restart_explore_outer_seed_budget = max(1, int(_flow.settings.options.get('initial_search_funnel_probe_cycle_global_outer', _flow.exploration.full_restart_explore_outer_seed_budget) or _flow.exploration.full_restart_explore_outer_seed_budget))
    _flow.seeding.full_restart_seed_pool_multiplier = 1.0
    _flow.seeding.full_restart_per_outer_seed_pool_target = _flow.settings.population
    _flow.settings.options['initial_search_funnel_global_explore_full_restart_seed_pool_multiplier'] = 1.0
    _flow.settings.options['initial_search_restart_seed_topology_novelty'] = False
    _flow.settings.options['initial_search_restart_seed_topology_scan_limit'] = 0
    _flow.seeding.full_restart_shared_seed_pool_target = _flow.exploration.full_restart_explore_outer_seed_budget * _flow.seeding.full_restart_per_outer_seed_pool_target if _flow.seeding.full_restart_shared_seed_pool_enabled else 0
    _flow.seeding.seed_pool_target = max(_flow.seeding.base_seed_pool_target, _flow.seeding.full_restart_shared_seed_pool_target)
    _flow.seeding.refresh_pool_target = max(_flow.settings.population, int(math.ceil(_flow.settings.population * _flow.seeding.refresh_pool_multiplier)))
    _flow.settings.options['initial_search_population'] = _flow.settings.population
    _flow.settings.options['initial_search_population_target_count'] = _flow.seeding.seed_pool_target
    _flow.settings.options['initial_search_refresh_trace_archive_capacity'] = _flow.seeding.refresh_pool_target
    _flow.settings.options['initial_search_funnel_global_effective_variable_count'] = _flow.seeding.global_effective_variable_count
    _flow.settings.options['initial_search_funnel_global_evaluator_dimension'] = _flow.seeding.global_evaluator_dimension
    _flow.settings.options['initial_search_funnel_global_population'] = _flow.settings.population
    _flow.settings.options['initial_search_de_requested_population'] = requested_population
    _flow.settings.options['initial_search_de_evaluator_population_floor'] = evaluator_population_floor
    _flow.settings.options['initial_search_de_evaluator_population_multiplier'] = evaluator_population_multiplier
    _flow.settings.options['initial_search_de_actual_population'] = _flow.settings.population
    _flow.settings.options['initial_search_de_population_floor_reason'] = 'max(configured_minimum,cpp_de_minimum_4x_non_vignetting_dimension)'
    _flow.settings.options['initial_search_seed_pool_target_count'] = _flow.seeding.seed_pool_target
    _flow.settings.options['initial_search_refresh_pool_target_count'] = _flow.seeding.refresh_pool_target
    basin_counts = _lensde_populations.adaptive_initial_search_basin_counts(_flow.settings.options, _flow.settings.population)
    _flow.settings.options['initial_search_basin_base_max'] = max(1, int(_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_basin_max'), 96)))
    _flow.settings.options['initial_search_basin_base_output_count'] = max(1, int(_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_basin_output_count'), 48)))
    _flow.settings.options['initial_search_basin_base_active_count'] = max(0, int(_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_basin_active_count'), 9)))
    _flow.settings.options['initial_search_basin_max'] = basin_counts['basin_max']
    _flow.settings.options['initial_search_basin_output_count'] = basin_counts['basin_output_count']
    _flow.settings.options['initial_search_basin_active_count'] = basin_counts['basin_active_count']
    _flow.settings.options['initial_search_basin_effective_max'] = basin_counts['basin_max']
    _flow.settings.options['initial_search_basin_effective_output_count'] = basin_counts['basin_output_count']
    _flow.settings.options['initial_search_basin_effective_active_count'] = basin_counts['basin_active_count']
    _lensde_provenance.write_json(_flow.optics.system_path, _lensde_configuration.system_with_ray_aiming(_flow.optics.base_system, _flow.settings.options))
    _lensde_provenance.write_json(_flow.optics.variables_path, {'variables': _flow.optics.variables})
    if _flow.optics.glass_search_config.get('enabled'):
        _lensde_provenance.write_json(_flow.optics.glass_search_path, _flow.optics.glass_search_config)
    _flow.optics.initial_stage_merit = _flow.optics.full_merit if _flow.optics.merit_strategy == 'full' else _flow.optics.search_merit
    _flow.optics.initial_stage_merit = _lensde_materials.apply_continuous_glass_proximity_merit(_flow.optics.initial_stage_merit, _flow.optics.base_system, _flow.optics.variables, _flow.settings.options)
    physical_cut_match_enabled = _lensde_commands.option_truthy(_flow.optics.initial_stage_merit.get('vignetting_physical_cut_match_enabled', _flow.optics.initial_stage_merit.get('vignetting_global_trim_cut_match_experimental', True)), True)
    _flow.optics.initial_stage_merit.pop('vignetting_global_trim_cut_match_experimental', None)
    _flow.optics.initial_stage_merit['vignetting_physical_cut_match_enabled'] = physical_cut_match_enabled
    for downstream_merit in (_flow.optics.search_merit, _flow.optics.full_merit):
        downstream_merit.pop('vignetting_global_trim_cut_match_experimental', None)
        downstream_merit['vignetting_physical_cut_match_enabled'] = physical_cut_match_enabled
    outer_loose_merit = copy.deepcopy(_flow.optics.initial_stage_merit)
    outer_loose_merit['vignetting_physical_cut_match_enabled'] = False
    _flow.exploration.outer_two_stage_enabled = _lensde_configuration.outer_physical_cut_staging_enabled(_flow.optics.initial_stage_merit)
    initial_asphere_scout_summary: dict = {'status': 'not_applicable'}
    if _lensde_initialization.initial_search_asphere_variables_enabled(_flow.settings.options) and (not _flow.settings.mechanical_direct_seed):
        _reference_radii, initial_asphere_scout_summary = _lensde_initialization.run_initial_search_asphere_geometry_scout(_flow.optics.base_system, _flow.optics.variables, _flow.optics.initial_stage_merit, _flow.settings.options, _flow.settings.search_dir, seed=_flow.settings.seed, target_efl=_flow.inputs.target_efl, target_ttl=_flow.inputs.target_ttl, target_bfl=_flow.inputs.target_bfl, rest_cap=_flow.seeding.rest_cap, curvature_cap=_flow.seeding.initial_curvature_bound)
        if bool(_flow.settings.options.get('de_override_bounds', False)):
            _flow.optics.variables = _lensde_commands.apply_de_bound_overrides(_flow.optics.base_system, _flow.optics.variables, _flow.settings.options)
            initial_asphere_scout_summary['de_bound_overrides_reapplied_after_reference'] = True
        _lensde_provenance.write_json(_flow.optics.variables_path, {'variables': _flow.optics.variables})
        scout_summary_path = _flow.settings.search_dir / 'initial_asphere_aperture_scout_summary.json'
        _lensde_provenance.write_json(scout_summary_path, initial_asphere_scout_summary)
        scout_status = str(initial_asphere_scout_summary.get('status') or 'unknown')
        if bool(initial_asphere_scout_summary.get('enabled', False)) and scout_status != 'ok' and (not bool(_flow.settings.options.get('initial_search_asphere_scout_allow_fallback', False))):
            return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 13, '', f'Initial asphere geometry scout did not produce a complete shared aperture reference (status={scout_status}). See {scout_summary_path}. Set initial_search_asphere_scout_allow_fallback=true only to explicitly accept flat-input radii.'))
        _flow.settings.options['initial_search_asphere_reference_radii'] = initial_asphere_scout_summary.get('reference_radii_mm', {})
        _flow.settings.options['initial_search_asphere_scout_status'] = initial_asphere_scout_summary.get('status', 'unknown')
    elif _lensde_initialization.initial_search_asphere_variables_enabled(_flow.settings.options) and _flow.settings.mechanical_direct_seed:
        initial_asphere_scout_summary = {'status': 'disabled_by_mechanical_stratified_direct', 'enabled': False, 'reason': 'baseline seed generation uses saved mechanical aperture bounds without optical scouting'}
        _flow.settings.options['initial_search_asphere_scout_status'] = initial_asphere_scout_summary['status']
        _lensde_provenance.write_json(_flow.settings.search_dir / 'initial_asphere_aperture_scout_summary.json', initial_asphere_scout_summary)
    _flow.optics.continuous_glass_constraints_count = len(_flow.optics.initial_stage_merit.get('glass_proximity', []) or [])
    _lensde_provenance.write_json(_flow.optics.merit_path, _flow.optics.initial_stage_merit)
    _flow.optics.outer_loose_merit_path = _flow.settings.search_dir / 'merit_outer_vignetting_loose.json'
    _lensde_provenance.write_json(_flow.optics.outer_loose_merit_path, outer_loose_merit)
    _flow.seeding.seed_gate_merit = copy.deepcopy(outer_loose_merit if _flow.exploration.outer_two_stage_enabled else _flow.optics.initial_stage_merit)
    _flow.seeding.seed_gate_merit.pop('glass_proximity', None)
    _flow.seeding.seed_gate_merit_path = _flow.settings.search_dir / 'seed_gate_merit.json'
    _lensde_provenance.write_json(_flow.seeding.seed_gate_merit_path, _flow.seeding.seed_gate_merit)
    _flow.settings.aiming_mode = _lensde_configuration.sync_unified_ray_aiming_options(_flow.settings.options)
    _flow.seeding.seed_gate_stdout_parts: list[str] = []
    _flow.seeding.seed_gate_stderr_parts: list[str] = []
    _flow.seeding.seed_gate_stages: list[dict] = []
    _flow.seeding.seed_gate_failed_message = ''
    _flow.seeding.adaptive_seed_caps = _flow.seeding.adaptive_thickness_bounds and bool(_flow.settings.options.get('initial_search_adaptive_seed_thickness_caps', True))
    _flow.seeding.seed_thickness_budget = _lensde_geometry.adaptive_thickness_budget(_flow.optics.base_system, _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.inputs.target_efl, thickness_constraints=initial_thickness_constraints, bfl_surface_index=_flow.seeding.bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor, use_current_thickness_prior=use_current_thickness_prior) if _flow.seeding.adaptive_seed_caps else {}
    _flow.seeding.seed_air_thickness_cap = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_air_thickness_cap'), 25.0)
    _flow.seeding.seed_glass_thickness_cap = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_glass_thickness_cap'), 2.0)
    if _flow.seeding.adaptive_seed_caps:
        _flow.seeding.seed_air_thickness_cap = _lensde_configuration.finite_float(_flow.seeding.seed_thickness_budget.get('seed_air_cap'), _flow.seeding.seed_air_thickness_cap)
        _flow.seeding.seed_glass_thickness_cap = _lensde_configuration.finite_float(_flow.seeding.seed_thickness_budget.get('seed_glass_cap'), _flow.seeding.seed_glass_thickness_cap)
    _flow.seeding.seed_ttl_projection_kwargs = {'auto_ttl_thickness_budget': bool(_flow.settings.options.get('initial_search_auto_ttl_thickness_budget', True)), 'ttl_budget_trigger_ratio': _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_ttl_budget_trigger_ratio'), 1.15), 'ttl_budget_compact_slack_fraction': _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_ttl_budget_compact_slack_fraction'), 0.55), 'ttl_budget_lower_factor': _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_ttl_budget_lower_factor'), 0.9), 'ttl_budget_upper_factor': _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_ttl_budget_upper_factor'), 1.03), 'ttl_budget_trace_upper_factor': _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_ttl_budget_trace_upper_factor'), 1.2), 'seed_ttl_allow_short': bool(_flow.settings.options.get('initial_search_seed_ttl_allow_short', True)), 'seed_ttl_max_factor': max(1e-09, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_ttl_max_factor'), 1.0)), 'bfl_surface_index': _flow.seeding.bfl_surface_index, 'bfl_lower_factor': bfl_lower_factor, 'bfl_upper_factor': bfl_upper_factor}
    _flow.seeding.seed_ttl_projection = _lensde_populations.ttl_seed_thickness_budget_config(_flow.optics.base_system, _flow.optics.variables, _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.inputs.target_efl, enabled=_flow.seeding.seed_ttl_projection_kwargs['auto_ttl_thickness_budget'], trigger_ratio=_flow.seeding.seed_ttl_projection_kwargs['ttl_budget_trigger_ratio'], compact_slack_fraction=_flow.seeding.seed_ttl_projection_kwargs['ttl_budget_compact_slack_fraction'], lower_factor=_flow.seeding.seed_ttl_projection_kwargs['ttl_budget_lower_factor'], upper_factor=_flow.seeding.seed_ttl_projection_kwargs['ttl_budget_upper_factor'], trace_upper_factor=_flow.seeding.seed_ttl_projection_kwargs['ttl_budget_trace_upper_factor'], allow_short_seed=_flow.seeding.seed_ttl_projection_kwargs['seed_ttl_allow_short'], seed_max_factor=_flow.seeding.seed_ttl_projection_kwargs['seed_ttl_max_factor'], bfl_surface_index=_flow.seeding.bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor)
    if bool(_flow.seeding.seed_ttl_projection.get('requested')) and _lensde_configuration.finite_float(_flow.seeding.seed_ttl_projection.get('target_ttl'), 0.0) > 0.0 and (not bool(_flow.seeding.seed_ttl_projection.get('feasible'))):
        failure_path = _flow.settings.search_dir / 'seed_budget_failure.json'
        _lensde_provenance.write_json(failure_path, _flow.seeding.seed_ttl_projection)
        return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 12, '', f"初始种群 TTL/BFL 预算与当前制造厚度边界不相容，已在评分前停止。 reason={_flow.seeding.seed_ttl_projection.get('reason', 'unknown')}; see {failure_path}"))
    _flow.seeding.asphere_profile_metadata: dict[tuple[float, ...], dict] = {}
    _flow.seeding.material_region_sampling_plan = _lensde_populations.prepare_catalog_material_seed_sampling_plan(_flow.optics.base_system, _flow.optics.variables, nd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_nd_cap'), 0.0), vd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_vd_cap'), 0.0)) if bool(_flow.settings.options.get('initial_search_population_perturb_material', True)) else None
    _flow.seeding.material_region_diversity_required = _lensde_populations.material_seed_region_diversity_applicable(_flow.seeding.material_region_sampling_plan, 2)
    return PhaseResult(False, None)
