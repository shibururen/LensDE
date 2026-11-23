"""Select the initial seed strategy, materialize its input and honor seed-only completion."""
from __future__ import annotations
import copy
from .. import state
from .. import commands as _lensde_commands
from .. import configuration as _lensde_configuration
from .. import fields as _lensde_fields
from .. import geometry as _lensde_geometry
from .. import initialization as _lensde_initialization
from .. import materials as _lensde_materials
from .. import populations as _lensde_populations
from .. import provenance as _lensde_provenance
from . import seed_generation as _m_seed_generation
from . import seed_scoring as _m_seed_scoring
from .contracts import SearchState, PhaseResult

def run_phase(_flow: SearchState) -> PhaseResult:
    """Select the initial seed strategy, materialize its input and honor seed-only completion."""
    _flow.settings.baseline_seed_config: dict = {}
    if _flow.seeding.seed_strategy == 'anchor_sparse_refill':
        _flow.seeding.seed_rows = _m_seed_generation.make_traceable_rich_initial_population_v2(_flow)
    elif _flow.seeding.seed_strategy == state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY:
        _flow.seeding.seed_rows, _flow.settings.baseline_seed_config = _m_seed_generation.generate_mechanical_direct_rows(_flow, _flow.optics.variables, _flow.seeding.seed_pool_target, _flow.settings.seed + 909)
        _flow.settings.baseline_seed_config.update({'population_target': _flow.seeding.seed_pool_target, 'target_ttl': _flow.inputs.target_ttl, 'target_bfl': _flow.inputs.target_bfl, 'target_efl_not_used_for_repair': _flow.inputs.target_efl, 'bfl_surface_index': _flow.seeding.bfl_surface_index, 'seed_bfl_bound_changes': _flow.settings.baseline_bfl_bound_changes, 'five_outer_delivery': {'enabled': _flow.seeding.full_restart_shared_seed_pool_enabled, 'outer_count': _flow.exploration.full_restart_explore_outer_seed_budget, 'rows_per_outer': _flow.seeding.full_restart_per_outer_seed_pool_target, 'shared_rows': _flow.seeding.full_restart_shared_seed_pool_target}})
        _lensde_provenance.write_json(_flow.settings.search_dir / 'mechanical_stratified_direct_seed_config.json', _flow.settings.baseline_seed_config)
        _flow.seeding.seed_gate_stdout_parts.append(f"[mechanical_stratified_direct] generated={len(_flow.seeding.seed_rows)}, target={_flow.seeding.seed_pool_target}, ttl=[{_flow.settings.baseline_seed_config['budget']['ttl_lower']:.6g},{_flow.settings.baseline_seed_config['budget']['ttl_upper']:.6g}], bfl=[{_flow.settings.baseline_seed_config['budget']['bfl_lower']:.6g},{_flow.settings.baseline_seed_config['budget']['bfl_ttl_upper_fraction']:.6g}*row_ttl], shape_assignments={_flow.settings.baseline_seed_config['shape_coverage']['injected_assignments']}, optical_gate=none, selector=none, anchor_cloud=none, first_order_repair=no\n")
    else:
        _flow.seeding.seed_rows = _lensde_populations.make_hybrid_initial_seed_population(_flow.optics.base_system, _flow.optics.variables, _flow.seeding.front_pairs, _flow.settings.population, _flow.settings.seed + 909, _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.seeding.rest_cap, _flow.seeding.initial_curvature_bound, population_generation_mode=str(_flow.settings.options.get('initial_search_population_generation_mode', 'restricted_perturbation')), front_profile=str(_flow.settings.options.get('initial_search_front_profile', 'weak')), seed_air_thickness_cap=_flow.seeding.seed_air_thickness_cap, seed_glass_thickness_cap=_flow.seeding.seed_glass_thickness_cap, seed_material_nd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_nd_cap'), 0.0), seed_material_vd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_vd_cap'), 0.0), local_group_perturbation=bool(_flow.settings.options.get('initial_search_population_local_group_perturbation', False)), local_long_air_gap_prior=bool(_flow.settings.options.get('initial_search_population_local_long_air_gap_prior', True)), perturb_material_variables=bool(_flow.settings.options.get('initial_search_population_perturb_material', True)), perturb_other_variables=bool(_flow.settings.options.get('initial_search_population_perturb_other_variables', False)), target_efl=_flow.inputs.target_efl, asphere_profile_metadata=_flow.seeding.asphere_profile_metadata, **_flow.seeding.seed_ttl_projection_kwargs)
        _flow.seeding.seed_rows = _m_seed_scoring.maybe_trace_gate_initial_seed_population(_flow, _flow.seeding.seed_rows)
    if not _flow.seeding.seed_rows:
        seed_failure_message = '厚度预算初始种群未生成可用候选。' if _flow.seeding.seed_strategy == state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY else '初始种群追迹门控未找到可用候选。'
        return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.inputs.run_merit, 12, ''.join(_flow.seeding.seed_gate_stdout_parts), '\n'.join(_flow.seeding.seed_gate_stderr_parts) or _flow.seeding.seed_gate_failed_message or seed_failure_message))
    _lensde_populations.write_population_csv(_flow.seeding.seed_path, _flow.seeding.seed_rows)
    _lensde_provenance.write_json(_flow.settings.search_dir / 'initial_material_population_summary.json', _lensde_materials.initial_material_population_summary(_flow.optics.base_system, _flow.optics.variables, _flow.seeding.seed_rows))
    if _lensde_geometry.variable_list_has_asphere_coeff(_flow.optics.variables) and (not _flow.settings.mechanical_direct_seed):
        _lensde_populations.write_population_csv(_flow.settings.external_seed_path, _lensde_initialization.external_initial_asphere_population_rows(_flow.seeding.seed_rows, _flow.optics.variables, _flow.settings.population, _flow.seeding.asphere_profile_metadata))
    _flow.seeding.seed_variables = copy.deepcopy(_flow.optics.variables)
    optimization_variables = _lensde_fields.make_post_seed_optimization_variables(_flow.optics.base_system, _flow.seeding.seed_variables, _flow.inputs.project.get('variables', []), _flow.settings.options)
    bound_changes = []
    for seed_variable, optimization_variable in zip(_flow.seeding.seed_variables, optimization_variables):
        seed_lower = _lensde_geometry.optional_finite_float(seed_variable.get('lower'))
        seed_upper = _lensde_geometry.optional_finite_float(seed_variable.get('upper'))
        optimization_lower = _lensde_geometry.optional_finite_float(optimization_variable.get('lower'))
        optimization_upper = _lensde_geometry.optional_finite_float(optimization_variable.get('upper'))
        if seed_lower == optimization_lower and seed_upper == optimization_upper:
            continue
        bound_changes.append({'kind': seed_variable.get('kind'), 'index': seed_variable.get('index'), 'term': seed_variable.get('term', 0), 'seed_lower': seed_lower, 'seed_upper': seed_upper, 'optimization_lower': optimization_lower, 'optimization_upper': optimization_upper})
    _lensde_provenance.write_json(_flow.settings.optimization_variables_path, {'variables': optimization_variables})
    _lensde_provenance.write_json(_flow.settings.search_dir / 'variable_bound_transition.json', {'policy': 'compact_seed_domain_then_project_envelope', 'seed_variables_path': str(_flow.seeding.seed_variables_path), 'optimization_variables_path': str(_flow.settings.optimization_variables_path), 'seed_variable_count': len(_flow.seeding.seed_variables), 'optimization_variable_count': len(optimization_variables), 'explicit_de_bound_overrides': bool(_flow.settings.options.get('de_override_bounds', False)), 'changed_bounds': bound_changes})
    _flow.optics.variables = optimization_variables
    _flow.optics.variables_path = _flow.settings.optimization_variables_path
    if bool(_flow.settings.options.get('initial_search_seed_only', False)):
        seed_scores = _lensde_populations.read_population_scores_csv(_flow.settings.search_dir / 'seed_trace_gate_v2' / 'final_recheck' / 'score' / 'population_scores.csv') if (_flow.settings.search_dir / 'seed_trace_gate_v2' / 'final_recheck' / 'score' / 'population_scores.csv').exists() else []
        seed_summary = {'strategy': 'initial_structure_search_seed_only', 'optimizer': _flow.settings.search_optimizer_mode, 'merit_strategy': _flow.optics.merit_strategy, 'flow': _flow.settings.flow, 'wide_angle_prior': _flow.settings.wide_angle_prior, 'front_groups': _flow.seeding.front_groups, 'front_pairs': _flow.seeding.front_pairs, 'flat_start': bool(_flow.settings.options.get('initial_search_flat_start', True)), 'curvature_bound': _flow.seeding.initial_curvature_bound, 'seed_curvature_cap': _flow.seeding.initial_curvature_bound, 'rest_curvature_cap': _flow.seeding.rest_cap, 'target_efl': _flow.inputs.target_efl, 'target_ttl': _flow.inputs.target_ttl, 'target_bfl': _flow.inputs.target_bfl, 'adaptive_thickness_bounds': _flow.seeding.adaptive_thickness_bounds, 'adaptive_seed_thickness_caps': _flow.seeding.adaptive_seed_caps, 'thickness_budget': _flow.seeding.seed_thickness_budget, 'ttl_thickness_projection': _flow.seeding.seed_ttl_projection, 'seed_air_thickness_cap': _flow.seeding.seed_air_thickness_cap, 'seed_glass_thickness_cap': _flow.seeding.seed_glass_thickness_cap, 'population': _flow.settings.population, 'seed_pool_target': _flow.seeding.seed_pool_target, 'base_seed_pool_target': _flow.seeding.base_seed_pool_target, 'full_restart_shared_seed_pool_enabled': _flow.seeding.full_restart_shared_seed_pool_enabled, 'full_restart_shared_seed_pool_target': _flow.seeding.full_restart_shared_seed_pool_target, 'seed_rows': len(_flow.seeding.seed_rows), 'seed_variable_count': len(_flow.seeding.seed_variables), 'optimization_variable_count': len(optimization_variables), 'variable_count': len(_flow.optics.variables), 'final_variable_count': len(_flow.optics.variables), 'final_stage': 'seed_only', 'seed_trace_strategy': _flow.seeding.seed_strategy, 'initial_population_mode': _flow.seeding.seed_strategy, 'mechanical_stratified_direct': _flow.settings.baseline_seed_config if _flow.settings.mechanical_direct_seed else None, 'seed_gate_stages': _flow.seeding.seed_gate_stages, 'population_summary': _lensde_commands.population_score_summary(seed_scores)}
        _lensde_provenance.write_json(_flow.inputs.run_dir / 'strategy_summary.json', seed_summary)
        _flow.settings.run_meta = {'strategy': 'initial_structure_search_seed_only', 'optimizer': _flow.settings.search_optimizer_mode, 'merit_strategy': _flow.optics.merit_strategy, 'flow': _flow.settings.flow, 'timeout_seconds': _flow.runtime.timeout_seconds, 'timeout_source': 'project.options.cpp_timeout', 'use_cuda': _flow.runtime.use_cuda, 'executable': str(_flow.runtime.exe), 'stages': _flow.seeding.seed_gate_stages, 'options': _flow.settings.options}
        _lensde_provenance.write_json(_flow.inputs.run_dir / 'run_meta.json', _flow.settings.run_meta)
        (_flow.inputs.run_dir / 'command.txt').write_text('\n\n'.join((' '.join(stage.get('command', [])) for stage in _flow.seeding.seed_gate_stages)), encoding='utf-8')
        return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, _flow.seeding.seed_gate_merit, 0, ''.join(_flow.seeding.seed_gate_stdout_parts), '\n'.join(_flow.seeding.seed_gate_stderr_parts), timed_out=False, timeout_seconds=_flow.runtime.timeout_seconds if _flow.runtime.timeout_seconds > 0 else None))
    return PhaseResult(False, None)
