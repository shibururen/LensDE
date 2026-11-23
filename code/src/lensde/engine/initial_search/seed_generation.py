"""Initial seed generation, TTL projection and seed-domain conversion; RNG creation stays local to the original draw sites."""
from __future__ import annotations
import copy
import math
import random
import time
from pathlib import Path
from .. import commands as _lensde_commands
from .. import configuration as _lensde_configuration
from .. import execution as _lensde_execution
from .. import geometry as _lensde_geometry
from .. import initialization as _lensde_initialization
from .. import populations as _lensde_populations
from .. import provenance as _lensde_provenance
from . import seed_io as _m_seed_io
from . import seed_scoring as _m_seed_scoring
from .contracts import SearchState, PhaseResult

def project_seed_rows_to_ttl_budget(_flow: SearchState, rows: list[list[float]], *, rng_offset: int=0, reference_rows: list[list[float]] | None=None, reproject_local_rows: bool=True) -> list[list[float]]:
    """Initial seed generation, TTL projection and seed-domain conversion Implements `project_seed_rows_to_ttl_budget` for this responsibility."""
    if not reproject_local_rows:
        return rows
    if not bool(_flow.seeding.seed_ttl_projection.get('enabled')):
        return rows
    projection_rng = random.Random(_flow.settings.seed + 17331 + int(rng_offset))
    projected: list[list[float]] = []
    for row_index, row in enumerate(rows):
        values: dict[tuple[str, int, int], float] = {}
        mutable_thickness_keys: set[tuple[str, int, int]] = set()
        reference = reference_rows[row_index] if reference_rows is not None and row_index < len(reference_rows) else None
        for index, variable in enumerate(_flow.optics.variables):
            lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
            hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
            if lo > hi:
                lo, hi = (hi, lo)
            raw = row[index] if index < len(row) else _lensde_populations.variable_current_value_for_system(_flow.optics.base_system, variable, _lensde_configuration.finite_float(variable.get('initial'), lo))
            values[_lensde_geometry.variable_key(variable)] = _lensde_populations.clamp_value(_lensde_configuration.finite_float(raw, lo), lo, hi)
            if reference is not None and variable.get('kind') == 'thickness' and (index < len(reference)) and (abs(_lensde_configuration.finite_float(raw, lo) - _lensde_configuration.finite_float(reference[index], lo)) > _lensde_populations.seed_activity_tolerance(variable)):
                mutable_thickness_keys.add(_lensde_geometry.variable_key(variable))
        _lensde_populations.project_thickness_values_to_ttl_budget(_flow.optics.base_system, _flow.optics.variables, values, _flow.seeding.seed_ttl_projection, projection_rng, mutable_keys=mutable_thickness_keys)
        projected_row: list[float] = []
        for variable in _flow.optics.variables:
            lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
            hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
            if lo > hi:
                lo, hi = (hi, lo)
            projected_row.append(_lensde_populations.clamp_value(values.get(_lensde_geometry.variable_key(variable), _lensde_configuration.finite_float(variable.get('initial'), lo)), lo, hi))
        projected.append(projected_row)
    return projected

def make_traceable_rich_initial_population_v2(_flow: SearchState, *, gate_dir: Path | None=None, seed_base: int | None=None, target_override: int | None=None) -> list[list[float]]:
    """Build the rich seed pool using the original draw, projection, score and refill order. Gate diagnostics and the failure reason remain shared with the caller."""
    seed_base = _flow.settings.seed if seed_base is None else int(seed_base)
    target = max(1, int(target_override)) if target_override is not None else max(1, _flow.seeding.seed_pool_target)
    if not bool(_flow.settings.options.get('initial_search_population_trace_feasible_gate', False)):
        return _lensde_populations.make_hybrid_initial_seed_population(_flow.optics.base_system, _flow.optics.variables, _flow.seeding.front_pairs, target, seed_base + 909, _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.seeding.rest_cap, _flow.seeding.initial_curvature_bound, population_generation_mode=str(_flow.settings.options.get('initial_search_population_generation_mode', 'restricted_perturbation')), front_profile=str(_flow.settings.options.get('initial_search_front_profile', 'weak')), seed_air_thickness_cap=_flow.seeding.seed_air_thickness_cap, seed_glass_thickness_cap=_flow.seeding.seed_glass_thickness_cap, seed_material_nd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_nd_cap'), 0.0), seed_material_vd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_vd_cap'), 0.0), local_group_perturbation=bool(_flow.settings.options.get('initial_search_population_local_group_perturbation', False)), local_long_air_gap_prior=bool(_flow.settings.options.get('initial_search_population_local_long_air_gap_prior', True)), perturb_material_variables=bool(_flow.settings.options.get('initial_search_population_perturb_material', True)), perturb_other_variables=bool(_flow.settings.options.get('initial_search_population_perturb_other_variables', False)), target_efl=_flow.inputs.target_efl, asphere_profile_metadata=_flow.seeding.asphere_profile_metadata, **_flow.seeding.seed_ttl_projection_kwargs)
    gate_dir = gate_dir or _flow.settings.search_dir / 'seed_trace_gate_v2'
    gate_dir.mkdir(parents=True, exist_ok=True)
    diversity_kwargs = _lensde_populations.seed_diversity_thresholds_from_options(_flow.settings.options)
    summaries: dict[str, object] = {}
    all_objects: list[dict] = []
    strict_pool: list[dict] = []
    projected_mother_row: list[float] | None = None
    max_seconds = max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_population_max_seconds'), 0.0))
    gate_started = time.time()

    def write_seed_generation_progress(stage: str, **details: object) -> None:
        _lensde_provenance.write_json(gate_dir / 'seed_generation_progress.json', {'status': 'complete' if stage == 'complete' else 'failed' if stage.startswith('failed_') else 'running', 'stage': stage, 'elapsed_seconds': max(0.0, time.time() - gate_started), 'target': target, **details})
    write_seed_generation_progress('cap_scout')

    def exhausted() -> bool:
        return max_seconds > 0.0 and time.time() - gate_started >= max_seconds
    cap_scout_enabled = bool(_flow.settings.options.get('initial_search_seed_cap_scout_enabled', True))
    cap_list = _m_seed_io.parse_cap_list(_flow.settings.options.get('initial_search_seed_cap_scout_list'), [0.01, 0.02, 0.04])
    if not cap_scout_enabled:
        cap_list = [max(1e-06, _lensde_configuration.finite_float(_flow.seeding.initial_curvature_bound, 0.02))]
    configured_scout_candidates = max(1, int(_flow.settings.options.get('initial_search_seed_cap_scout_candidates', 6144) or 6144))
    scout_candidates = min(configured_scout_candidates, max(512, 4 * len(_flow.optics.variables)))
    cap_summaries: list[dict] = []
    best_cap = cap_list[0]
    best_cap_score: tuple = (False, -1.0, -1.0, -1.0, float('-inf'), -1.0)
    cap_strict_by_cap: dict[float, list[dict]] = {}
    for cap_index, cap in enumerate(cap_list):
        if exhausted():
            break
        rows = _lensde_populations.make_hybrid_initial_seed_population(_flow.optics.base_system, _flow.optics.variables, _flow.seeding.front_pairs, scout_candidates, seed_base + 5100 + cap_index, _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.seeding.rest_cap, cap, population_generation_mode='restricted_perturbation', front_profile=str(_flow.settings.options.get('initial_search_front_profile', 'weak')), seed_air_thickness_cap=_flow.seeding.seed_air_thickness_cap, seed_glass_thickness_cap=_flow.seeding.seed_glass_thickness_cap, seed_material_nd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_nd_cap'), 0.0), seed_material_vd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_vd_cap'), 0.0), local_group_perturbation=True, local_long_air_gap_prior=bool(_flow.settings.options.get('initial_search_population_local_long_air_gap_prior', True)), perturb_material_variables=True, perturb_other_variables=False, target_efl=_flow.inputs.target_efl, asphere_profile_metadata=_flow.seeding.asphere_profile_metadata, reproject_local_thickness_rows=False, **_flow.seeding.seed_ttl_projection_kwargs)
        if projected_mother_row is None and rows:
            projected_mother_row = [_lensde_configuration.finite_float(value, 0.0) for value in rows[0]]
        objects, summary = _m_seed_scoring.score_seed_candidate_rows(_flow, f'cap_scout_{cap:g}', rows, gate_dir=gate_dir)
        strict = _m_seed_scoring.filter_strict_seed_objects(objects)
        diversity = _lensde_populations.seed_pool_diversity_summary([obj['row'] for obj in strict], _flow.optics.variables, **diversity_kwargs)
        summary['cap'] = cap
        summary['diversity'] = {key: diversity.get(key) for key in ('variables_with_range_frac_ge_0_001', 'variables_with_range_frac_ge_0_01', 'variables_with_active_frac_ge_min', 'curvature_variables_with_active_frac_ge_min', 'min_curvature_active_frac', 'median_active_vars', 'median_active_curvature_vars', 'unique_curvature_masks', 'duplicate_rows')}
        cap_summaries.append(summary)
        all_objects.extend(objects)
        strict_pool.extend(strict)
        cap_strict_by_cap[cap] = strict
        best_strict_merit = min((_lensde_configuration.finite_float(obj.get('merit'), float('inf')) for obj in strict), default=float('inf'))
        candidate_score = (bool(strict), _lensde_configuration.finite_float(summary.get('strict_rate'), 0.0), _lensde_configuration.finite_float(summary.get('trace_feasible_count'), 0.0) / max(1, len(rows)), _lensde_configuration.finite_float(summary.get('seed_budget_feasible_count'), 0.0) / max(1, len(rows)), -best_strict_merit, _lensde_configuration.finite_float(diversity.get('variables_with_range_frac_ge_0_001'), 0.0))
        if candidate_score > best_cap_score:
            best_cap_score = candidate_score
            best_cap = cap
    summaries['cap_scout'] = cap_summaries
    _lensde_provenance.write_json(gate_dir / 'cap_scout_summary.json', {'selected_cap': best_cap, 'stages': cap_summaries})
    write_seed_generation_progress('cap_production', selected_cap=best_cap, scout_candidates_per_cap=scout_candidates, strict_pool_count=len(strict_pool), recovering_from_empty_strict_scout=not bool(strict_pool))
    if not strict_pool:
        _flow.seeding.seed_gate_stdout_parts.append('[seed_trace_gate_v2] cap scout found no strict anchors; continuing with cap production and anchor acquisition.\n')
    best_cap_summary = next((summary for summary in cap_summaries if _lensde_configuration.finite_float(summary.get('cap'), -1.0) == best_cap), {})
    estimated_strict_rate = max(0.05, min(1.0, 0.95 * _lensde_configuration.finite_float(best_cap_summary.get('strict_rate'), 0.0)))
    production_summaries: list[dict] = []
    production_diversity_ready = False
    production_diversity: dict = {}
    production_requirements: dict = {}
    strict_rows_after_production: list[list[float]] = []
    for production_round in range(2):
        strict_rows_after_production = _lensde_populations.dedupe_variable_aware_seed_rows([obj['row'] for obj in _m_seed_scoring.filter_strict_seed_objects(strict_pool)], _flow.optics.variables)
        production_diversity_ready, production_diversity, production_requirements = _lensde_populations.seed_pool_refill_diversity_ready(strict_rows_after_production, _flow.optics.variables, active_target_count=target, **diversity_kwargs)
        if len(strict_rows_after_production) >= target and production_diversity_ready:
            break
        if exhausted():
            break
        strict_deficit = max(0, target - len(strict_rows_after_production))
        if strict_deficit <= 0:
            break
        desired_strict = strict_deficit + max(64, len(_flow.optics.variables))
        production_candidates = int(math.ceil(desired_strict / estimated_strict_rate))
        production_candidates = min(max(target * 2, configured_scout_candidates), max(1, production_candidates))
        rows = _lensde_populations.make_hybrid_initial_seed_population(_flow.optics.base_system, _flow.optics.variables, _flow.seeding.front_pairs, production_candidates, seed_base + 5700 + production_round, _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.seeding.rest_cap, best_cap, population_generation_mode='restricted_perturbation', front_profile=str(_flow.settings.options.get('initial_search_front_profile', 'weak')), seed_air_thickness_cap=_flow.seeding.seed_air_thickness_cap, seed_glass_thickness_cap=_flow.seeding.seed_glass_thickness_cap, seed_material_nd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_nd_cap'), 0.0), seed_material_vd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_vd_cap'), 0.0), local_group_perturbation=True, local_long_air_gap_prior=bool(_flow.settings.options.get('initial_search_population_local_long_air_gap_prior', True)), perturb_material_variables=True, perturb_other_variables=False, target_efl=_flow.inputs.target_efl, asphere_profile_metadata=_flow.seeding.asphere_profile_metadata, reproject_local_thickness_rows=False, **_flow.seeding.seed_ttl_projection_kwargs)
        objects, summary = _m_seed_scoring.score_seed_candidate_rows(_flow, f'cap_production_{production_round:02d}', rows, gate_dir=gate_dir)
        strict = _m_seed_scoring.filter_strict_seed_objects(objects)
        summary.update({'cap': best_cap, 'estimated_strict_rate': estimated_strict_rate, 'requested_strict_deficit': strict_deficit, 'requested_candidates': production_candidates})
        production_summaries.append(summary)
        all_objects.extend(objects)
        strict_pool.extend(strict)
        cap_strict_by_cap.setdefault(best_cap, []).extend(strict)
        observed_rate = _lensde_configuration.finite_float(summary.get('strict_rate'), 0.0)
        if observed_rate > 0.0:
            estimated_strict_rate = max(0.05, min(1.0, 0.95 * observed_rate))
    strict_rows_after_production = _lensde_populations.dedupe_variable_aware_seed_rows([obj['row'] for obj in _m_seed_scoring.filter_strict_seed_objects(strict_pool)], _flow.optics.variables)
    production_diversity_ready, production_diversity, production_requirements = _lensde_populations.seed_pool_refill_diversity_ready(strict_rows_after_production, _flow.optics.variables, active_target_count=target, **diversity_kwargs)
    pool_ready_after_production = len(strict_rows_after_production) >= target and production_diversity_ready
    summaries['cap_production'] = production_summaries
    _lensde_provenance.write_json(gate_dir / 'cap_production_summary.json', {'selected_cap': best_cap, 'strict_pool_count': len(strict_rows_after_production), 'target': target, 'diversity_ready': production_diversity_ready, 'diversity_requirements': production_requirements, 'stages': production_summaries})
    write_seed_generation_progress('anchor_or_refill', strict_pool_count=len(strict_rows_after_production), pool_ready=pool_ready_after_production)
    adaptive_anchor_target = max(1, int(math.ceil(target / 64.0)))
    configured_anchor_target = max(adaptive_anchor_target, int(_flow.settings.options.get('initial_search_anchor_target_count', 16) or 16))
    configured_anchor_target = min(configured_anchor_target, target)
    anchor_stage_skipped = pool_ready_after_production
    anchor_target = 0 if anchor_stage_skipped else configured_anchor_target
    anchor_min = max(1, int(_flow.settings.options.get('initial_search_anchor_min_count', 8) or 8), int(math.ceil(anchor_target * 0.5)))
    anchor_min = min(anchor_min, anchor_target)
    anchor_max_candidates = max(anchor_target * 64, target * 8, int(_flow.settings.options.get('initial_search_anchor_max_candidates', 65536) or 65536))
    anchor_batch = max(1, int(_flow.settings.options.get('initial_search_anchor_batch_size', 4096) or 4096), min(anchor_max_candidates, max(4096, min(target, 8192))))
    anchor_objects = [] if anchor_stage_skipped else list(cap_strict_by_cap.get(best_cap, []))
    anchor_attempts = 0
    anchor_summaries: list[dict] = []
    while len(_lensde_populations.dedupe_variable_aware_seed_rows([obj['row'] for obj in anchor_objects], _flow.optics.variables)) < anchor_target and anchor_attempts < anchor_max_candidates:
        if exhausted():
            break
        batch = min(anchor_batch, anchor_max_candidates - anchor_attempts)
        rows = _lensde_populations.make_hybrid_initial_seed_population(_flow.optics.base_system, _flow.optics.variables, _flow.seeding.front_pairs, batch, seed_base + 6100 + len(anchor_summaries), _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.seeding.rest_cap, best_cap, population_generation_mode='restricted_perturbation', front_profile=str(_flow.settings.options.get('initial_search_front_profile', 'weak')), seed_air_thickness_cap=_flow.seeding.seed_air_thickness_cap, seed_glass_thickness_cap=_flow.seeding.seed_glass_thickness_cap, seed_material_nd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_nd_cap'), 0.0), seed_material_vd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_vd_cap'), 0.0), local_group_perturbation=True, local_long_air_gap_prior=bool(_flow.settings.options.get('initial_search_population_local_long_air_gap_prior', True)), perturb_material_variables=True, perturb_other_variables=False, target_efl=_flow.inputs.target_efl, asphere_profile_metadata=_flow.seeding.asphere_profile_metadata, reproject_local_thickness_rows=False, **_flow.seeding.seed_ttl_projection_kwargs)
        anchor_attempts += len(rows)
        objects, summary = _m_seed_scoring.score_seed_candidate_rows(_flow, f'anchor_acq_{len(anchor_summaries):03d}', rows, gate_dir=gate_dir, cpu_recheck_deficit=max(0, anchor_target - len(anchor_objects)))
        all_objects.extend(objects)
        strict = _m_seed_scoring.filter_strict_seed_objects(objects)
        strict_pool.extend(strict)
        anchor_objects.extend(strict)
        anchor_summaries.append(summary)
    anchor_objects = [{'row': row, 'strict': True, 'merit': 0.0, 'source_stage': 'anchor_dedupe'} for row in _lensde_populations.dedupe_variable_aware_seed_rows([obj['row'] for obj in anchor_objects], _flow.optics.variables)]
    if len(anchor_objects) < anchor_min:
        _flow.seeding.seed_gate_failed_message = f'[seed_trace_gate_v2] anchor acquisition found {len(anchor_objects)} strict anchors; minimum is {anchor_min}.'
        _flow.seeding.seed_gate_stderr_parts.append(_flow.seeding.seed_gate_failed_message)
        _lensde_provenance.write_json(gate_dir / 'anchor_acquisition_summary.json', {'skipped': anchor_stage_skipped, 'skip_reason': 'strict_pool_sufficient_and_diverse' if anchor_stage_skipped else '', 'selected_cap': best_cap, 'configured_anchor_target': configured_anchor_target, 'minimum_anchor_count': anchor_min, 'attempts': anchor_attempts, 'strict_anchor_count': len(anchor_objects), 'selected_anchor_count': 0, 'stages': anchor_summaries, 'failure': _flow.seeding.seed_gate_failed_message})
        write_seed_generation_progress('failed_anchor_acquisition', selected_cap=best_cap, attempts=anchor_attempts, strict_anchor_count=len(anchor_objects), minimum_anchor_count=anchor_min, failure=_flow.seeding.seed_gate_failed_message)
        return []
    selected_anchor_rows = _lensde_populations.select_traceable_diverse_population(anchor_objects, _flow.optics.variables, min(anchor_target, len(anchor_objects)), seed_base + 6200, **diversity_kwargs)
    selected_anchors = [{'row': row, 'strict': True, 'merit': 0.0, 'source_stage': 'anchor_selected'} for row in selected_anchor_rows]
    _lensde_provenance.write_json(gate_dir / 'anchor_acquisition_summary.json', {'skipped': anchor_stage_skipped, 'skip_reason': 'strict_pool_sufficient_and_diverse' if anchor_stage_skipped else '', 'selected_cap': best_cap, 'configured_anchor_target': configured_anchor_target, 'attempts': anchor_attempts, 'strict_anchor_count': len(anchor_objects), 'selected_anchor_count': len(selected_anchors), 'stages': anchor_summaries})
    seed_curv_micro = max(2e-05, 0.01 * best_cap)
    seed_curv_small = max(5e-05, 0.04 * best_cap)
    seed_curv_medium = max(0.0001, 0.1 * best_cap)
    seed_curv_bold = max(0.0002, 0.18 * best_cap)
    cloud_profiles = {'micro': {'curv': seed_curv_micro, 'air': 0.06, 'glass': 0.02, 'nd': 0.001, 'vd': 0.2}, 'small': {'curv': seed_curv_small, 'air': 0.12, 'glass': 0.04, 'nd': 0.0015, 'vd': 0.35}, 'medium': {'curv': seed_curv_medium, 'air': 0.2, 'glass': 0.06, 'nd': 0.002, 'vd': 0.5}}
    profile_names = [name.strip().lower() for name in str(_flow.settings.options.get('initial_search_anchor_cloud_profiles', 'micro,small,medium')).split(',') if name.strip().lower() in cloud_profiles] or ['micro', 'small', 'medium']
    cloud_factor = max(1, int(_flow.settings.options.get('initial_search_anchor_cloud_factor', 8) or 8), int(math.ceil(target / max(1, len(selected_anchors) * len(profile_names))))) if selected_anchors else 0
    rng = random.Random(seed_base + 6300)
    cloud_rows: list[list[float]] = [obj['row'] for obj in selected_anchors]
    cloud_reference_rows: list[list[float]] = [obj['row'] for obj in selected_anchors]
    coverage_injection_enabled = bool(_flow.settings.options.get('initial_search_anchor_cloud_coverage_injection', True))
    undercovered_items = [item for item in production_diversity.get('undercovered_variables', []) if isinstance(item, dict) and 0 <= int(_lensde_configuration.finite_float(item.get('index'), -1.0)) < len(_flow.optics.variables)]
    undercovered_indices = [int(_lensde_configuration.finite_float(item.get('index'), -1.0)) for item in undercovered_items]
    planned_cloud_children = len(selected_anchors) * len(profile_names) * cloud_factor
    required_coverage_injections = sum((max(0, int(_lensde_configuration.finite_float(item.get('required_active_count'), 0.0)) - int(_lensde_configuration.finite_float(item.get('active_count'), 0.0))) for item in undercovered_items))
    coverage_injections_per_child = min(len(undercovered_indices), max(1, int(math.ceil(required_coverage_injections / max(1, planned_cloud_children))))) if coverage_injection_enabled and undercovered_indices else 0
    coverage_injection_count = 0
    cloud_child_index = 0
    activity_reference_row = projected_mother_row or [_lensde_configuration.finite_float(variable.get('initial'), 0.0) for variable in _flow.optics.variables]
    projected_mother_path = gate_dir / 'projected_mother.csv'
    _lensde_populations.write_population_csv(projected_mother_path, [activity_reference_row])
    material_variable_indices = {index for index, variable in enumerate(_flow.optics.variables) if variable.get('kind') in {'material_nd', 'material_vd'}}
    for anchor in selected_anchors:
        row = anchor['row']
        active = _lensde_populations.active_indices_from_row(row, _flow.optics.variables, reference_row=activity_reference_row)
        if not active:
            active = set(material_variable_indices)
        for profile_name in profile_names:
            profile = cloud_profiles[profile_name]
            for _ in range(cloud_factor):
                mutated = [_lensde_configuration.finite_float(value, 0.0) for value in row]
                child_active = set(active)
                if coverage_injection_enabled and undercovered_indices:
                    injection_base = cloud_child_index * coverage_injections_per_child
                    for injection_offset in range(coverage_injections_per_child):
                        coverage_index = undercovered_indices[(injection_base + injection_offset) % len(undercovered_indices)]
                        child_active.add(coverage_index)
                        coverage_injection_count += 1
                for index in child_active:
                    mutated[index] = _lensde_populations.perturb_seed_value_by_kind(mutated[index], _flow.optics.variables[index], rng, profile, system=_flow.optics.base_system)
                cloud_rows.append(mutated)
                cloud_reference_rows.append(row)
                cloud_child_index += 1
    cloud_score_rows = _lensde_populations.dedupe_variable_aware_seed_rows(project_seed_rows_to_ttl_budget(_flow, cloud_rows, rng_offset=6301, reference_rows=cloud_reference_rows, reproject_local_rows=False), _flow.optics.variables)
    if cloud_score_rows:
        cloud_objects, cloud_summary = _m_seed_scoring.score_seed_candidate_rows(_flow, 'anchor_cloud', cloud_score_rows, gate_dir=gate_dir)
        all_objects.extend(cloud_objects)
        strict_pool.extend(_m_seed_scoring.filter_strict_seed_objects(cloud_objects))
    else:
        cloud_objects = []
        cloud_summary = {'name': 'anchor_cloud', 'scored': 0, 'records': 0, 'strict_count': 0, 'strict_rate': 0.0, 'elapsed_seconds': 0.0, 'skipped': True, 'skip_reason': 'strict_pool_sufficient_and_diverse'}
    _lensde_provenance.write_json(gate_dir / 'anchor_cloud_summary.json', {**cloud_summary, 'profiles': profile_names, 'cloud_factor': cloud_factor, 'coverage_injection_enabled': coverage_injection_enabled, 'coverage_injection_variable_indices': undercovered_indices, 'coverage_injection_required_count': required_coverage_injections, 'coverage_injections_per_child': coverage_injections_per_child, 'coverage_injection_count': coverage_injection_count, 'activity_reference': 'projected_mother', 'activity_reference_path': str(projected_mother_path), 'post_local_ttl_reprojection': False, 'local_ttl_gate_upper': _lensde_configuration.finite_float(_flow.seeding.seed_ttl_projection.get('target_ttl'), 0.0) * max(1.0, _lensde_configuration.finite_float(_flow.seeding.seed_ttl_projection.get('trace_upper_factor'), _lensde_configuration.finite_float(_flow.seeding.seed_ttl_projection.get('active_upper_factor'), 1.0))), 'input_rows': len(cloud_rows), 'scored_rows_after_ttl_projection': len(cloud_score_rows)})
    refill_summaries: list[dict] = []
    refill_enabled = bool(_flow.settings.options.get('initial_search_sparse_refill_enabled', True))
    refill_target = target
    refill_max_candidates = max(refill_target * 8, int(_flow.settings.options.get('initial_search_sparse_refill_max_candidates', 65536) or 65536))
    refill_batch = max(1, int(_flow.settings.options.get('initial_search_sparse_refill_batch_size', 4096) or 4096), min(refill_max_candidates, max(4096, min(refill_target, 8192))))
    refill_profiles = [('ind_sparse_micro_p035', {'p': 0.35, 'curv': seed_curv_micro, 'air': 0.1, 'glass': 0.03, 'nd': 0.0015, 'vd': 0.25}), ('ind_sparse_small_p025', {'p': 0.25, 'curv': seed_curv_small, 'air': 0.25, 'glass': 0.08, 'nd': 0.003, 'vd': 0.8}), ('ind_sparse_medium_p015', {'p': 0.15, 'curv': seed_curv_medium, 'air': 0.5, 'glass': 0.15, 'nd': 0.006, 'vd': 1.5}), ('group_1ofN_medium', {'curv': seed_curv_medium, 'air': 0.2, 'glass': 0.06, 'nd': 0.002, 'vd': 0.5}), ('group_2ofN_small', {'curv': seed_curv_small, 'air': 0.12, 'glass': 0.04, 'nd': 0.0015, 'vd': 0.35}), ('group_1to3_bold', {'curv': seed_curv_bold, 'air': 0.18, 'glass': 0.05, 'nd': 0.002, 'vd': 0.55})]
    groups = sorted({group for variable in _flow.optics.variables for group in [_lensde_populations.seed_variable_group(_flow.optics.base_system, variable)] if group is not None})
    refill_attempts = 0
    min_refill_batches = max(0, int(_flow.settings.options.get('initial_search_sparse_refill_min_profile_rounds', 0) or 0))
    min_refill_batches = min(max(0, min_refill_batches), max(1, 2 * len(refill_profiles)))

    def current_strict_rows_and_diversity() -> tuple[list[list[float]], bool, dict, dict]:
        strict_rows = _lensde_populations.dedupe_variable_aware_seed_rows([obj['row'] for obj in _m_seed_scoring.filter_strict_seed_objects(strict_pool)], _flow.optics.variables)
        diversity_ready, diversity, requirements = _lensde_populations.seed_pool_refill_diversity_ready(strict_rows, _flow.optics.variables, active_target_count=target, **diversity_kwargs)
        return (strict_rows, diversity_ready, diversity, requirements)
    strict_rows_snapshot, pre_refill_diversity_ready, pre_refill_diversity, pre_refill_requirements = current_strict_rows_and_diversity()
    initial_refill_diversity_ready = pre_refill_diversity_ready
    initial_refill_diversity = copy.deepcopy(pre_refill_diversity)
    initial_refill_requirements = copy.deepcopy(pre_refill_requirements)
    current_refill_diversity = pre_refill_diversity
    current_refill_requirements = pre_refill_requirements
    skip_when_sufficient = bool(_flow.settings.options.get('initial_search_sparse_refill_skip_when_sufficient', True))
    refill_skipped = refill_enabled and skip_when_sufficient and (len(strict_rows_snapshot) >= refill_target) and pre_refill_diversity_ready and (min_refill_batches <= 0)
    refill_skip_reason = 'strict_pool_sufficient_and_diverse' if refill_skipped else ''
    refill_should_run = refill_enabled and (not refill_skipped)
    while refill_should_run and (len(strict_rows_snapshot) < refill_target or not pre_refill_diversity_ready or len(refill_summaries) < min_refill_batches):
        if exhausted() or refill_attempts >= refill_max_candidates:
            break
        parents = strict_rows_snapshot or selected_anchor_rows
        if not parents:
            break
        batch = min(refill_batch, refill_max_candidates - refill_attempts)
        policy_name, profile = refill_profiles[len(refill_summaries) % len(refill_profiles)]
        rows: list[list[float]] = []
        refill_reference_rows: list[list[float]] = []
        for _ in range(batch):
            parent = parents[rng.randrange(len(parents))]
            if policy_name.startswith('ind_sparse'):
                rows.append(_lensde_populations.mutate_independent_sparse_seed_row(parent, _flow.optics.variables, _lensde_configuration.finite_float(profile.get('p'), 0.25), profile, rng, system=_flow.optics.base_system))
                refill_reference_rows.append(parent)
            else:
                if not groups:
                    rows.append(_lensde_populations.mutate_independent_sparse_seed_row(parent, _flow.optics.variables, 0.2, profile, rng, system=_flow.optics.base_system))
                    refill_reference_rows.append(parent)
                    continue
                group_count = 1
                if policy_name.startswith('group_2'):
                    group_count = min(2, len(groups))
                elif policy_name.startswith('group_1to3'):
                    group_count = min(len(groups), rng.randint(1, min(3, len(groups))))
                selected_groups = set(rng.sample(groups, group_count))
                rows.append(_lensde_populations.mutate_group_masked_seed_row(parent, _flow.optics.variables, selected_groups, profile, rng, system=_flow.optics.base_system))
                refill_reference_rows.append(parent)
        refill_attempts += len(rows)
        refill_score_rows = _lensde_populations.dedupe_variable_aware_seed_rows(project_seed_rows_to_ttl_budget(_flow, rows, rng_offset=7100 + len(refill_summaries), reference_rows=refill_reference_rows, reproject_local_rows=False), _flow.optics.variables)
        objects, summary = _m_seed_scoring.score_seed_candidate_rows(_flow, f'sparse_refill_{len(refill_summaries):03d}_{policy_name}', refill_score_rows, gate_dir=gate_dir)
        summary['policy'] = policy_name
        summary['post_local_ttl_reprojection'] = False
        summary['input_rows_before_ttl_projection'] = len(rows)
        summary['scored_rows_after_ttl_projection'] = len(refill_score_rows)
        refill_summaries.append(summary)
        all_objects.extend(objects)
        strict_pool.extend(_m_seed_scoring.filter_strict_seed_objects(objects))
        strict_rows_snapshot, pre_refill_diversity_ready, current_refill_diversity, current_refill_requirements = current_strict_rows_and_diversity()
    _lensde_provenance.write_json(gate_dir / 'sparse_refill_summary.json', {'enabled': refill_enabled, 'skipped': refill_skipped, 'skip_when_sufficient': skip_when_sufficient, 'skip_reason': refill_skip_reason, 'target_strict_pool': refill_target, 'attempts': refill_attempts, 'strict_pool_count': len(strict_rows_snapshot), 'min_profile_rounds': min_refill_batches, 'ttl_thickness_projection': _flow.seeding.seed_ttl_projection, 'pre_refill_strict_pool_count': initial_refill_diversity.get('rows', 0), 'pre_refill_diversity_ready': initial_refill_diversity_ready, 'pre_refill_diversity_requirements': initial_refill_requirements, 'pre_refill_diversity': initial_refill_diversity, 'final_refill_diversity_ready': pre_refill_diversity_ready, 'final_refill_diversity_requirements': current_refill_requirements, 'final_refill_diversity': current_refill_diversity, 'curvature_profiles': {'micro': seed_curv_micro, 'small': seed_curv_small, 'medium': seed_curv_medium, 'bold': seed_curv_bold}, 'stages': refill_summaries})
    strict_pool = [{'row': row, 'strict': True, 'merit': 0.0, 'source_stage': 'strict_pool_dedupe'} for row in _lensde_populations.dedupe_variable_aware_seed_rows([obj['row'] for obj in _m_seed_scoring.filter_strict_seed_objects(strict_pool)], _flow.optics.variables)]
    strict_provenance_keys = {_lensde_populations.variable_aware_seed_row_key(obj['row'], _flow.optics.variables) for obj in strict_pool}
    if len(strict_pool) < target:
        _flow.seeding.seed_gate_failed_message = f'[seed_trace_gate_v2] strict candidate pool has {len(strict_pool)} rows; target is {target}.'
        _flow.seeding.seed_gate_stderr_parts.append(_flow.seeding.seed_gate_failed_message)
        return []
    write_seed_generation_progress('selector_prepare', strict_pool_count=len(strict_pool))
    strict_pool_path = gate_dir / 'strict_candidate_pool_values.csv'
    full_strict_pool_path = gate_dir / 'all_strict_candidate_pool.csv'
    selected_seed_population_path = gate_dir / 'selected_seed_population.csv'
    selection_summary_path = gate_dir / 'selection_summary.json'
    strict_pool_write_started = time.time()
    _lensde_populations.write_population_csv(strict_pool_path, [obj['row'] for obj in strict_pool])
    strict_pool_write_elapsed = max(0.0, time.time() - strict_pool_write_started)
    full_strict_pool_write_elapsed = None
    full_strict_pool_written = bool(_flow.settings.options.get('initial_search_write_full_seed_candidate_pool', False))
    if full_strict_pool_written:
        full_strict_pool_write_started = time.time()
        _m_seed_io.write_seed_pool_csv(_flow, full_strict_pool_path, strict_pool)
        full_strict_pool_write_elapsed = max(0.0, time.time() - full_strict_pool_write_started)

    def seed_coverage_shortfall_message(diversity: dict, requirements: dict) -> str:
        undercovered = diversity.get('undercovered_curvature_variables', [])
        labels = []
        if isinstance(undercovered, list):
            for item in undercovered[:8]:
                if not isinstance(item, dict):
                    continue
                surface = item.get('surface', '')
                active_frac = _lensde_configuration.finite_float(item.get('active_coverage_frac'), _lensde_configuration.finite_float(item.get('active_frac'), 0.0))
                required_frac = _lensde_configuration.finite_float(item.get('required_active_frac'), 0.0)
                active_count = int(_lensde_configuration.finite_float(item.get('active_count'), 0.0))
                required_count = int(_lensde_configuration.finite_float(item.get('required_active_count'), 0.0))
                if required_count > 0:
                    labels.append(f'S{surface}:{active_count}/{required_count}')
                else:
                    labels.append(f'S{surface}:{active_frac:.3g}<{required_frac:.3g}')
        suffix = f"; undercovered_curvature={','.join(labels)}" if labels else ''
        missing_vignetting_range = requirements.get('missing_mandatory_vignetting_range_indices', [])
        missing_vignetting_active = requirements.get('missing_mandatory_vignetting_active_indices', [])
        if missing_vignetting_range or missing_vignetting_active:
            suffix += f"; mandatory_vignetting_range_missing={','.join((str(index) for index in missing_vignetting_range))}; mandatory_vignetting_active_missing={','.join((str(index) for index in missing_vignetting_active))}"
        return f"coverage below target (range={diversity.get('variables_with_range_frac_ge_target')}/{requirements.get('variables_with_range_frac_ge_0_01')}, active={diversity.get('variables_with_active_frac_ge_min')}/{requirements.get('variables_with_active_frac_ge_min')}, curvature_active={diversity.get('curvature_variables_with_active_frac_ge_min')}/{requirements.get('curvature_variables_with_active_frac_ge_min')}){suffix}"
    selected_rows: list[list[float]] = []
    selection_engine = 'python'
    selection_elapsed = 0.0
    selection_fallback_reason = ''
    cpp_coverage_shortfall = ''
    coverage_repair_elapsed = 0.0
    coverage_repair_summary: dict = {'attempted': False}
    cpp_selector_dir = gate_dir / 'cpp_seed_selector'
    cpp_selector_dir.mkdir(parents=True, exist_ok=True)
    cpp_args = [str(_flow.runtime.exe), '--variables', str(_flow.optics.variables_path), '--select-seed-population', '--candidate-pool', str(strict_pool_path), '--select-target', str(target), '--selected-population', str(selected_seed_population_path), '--selection-summary', str(selection_summary_path), '--out-dir', str(cpp_selector_dir), '--seed', str(seed_base + 6400), '--select-min-active-fraction', str(diversity_kwargs['min_active_fraction']), '--select-min-curvature-active-fraction', str(diversity_kwargs['min_curvature_active_fraction'])]
    _lensde_populations.append_profiled_asphere_seed_selector_args(cpp_args, _flow.optics.variables)
    write_seed_generation_progress('selector', strict_pool_count=len(strict_pool))
    cpp_started = time.time()
    cpp_code, cpp_out, cpp_err, cpp_timed_out = _lensde_execution.run_stage(cpp_args, _flow.runtime.timeout_seconds, _flow.runtime.started_at)
    cpp_elapsed = max(0.0, time.time() - cpp_started)
    if cpp_out:
        _flow.seeding.seed_gate_stdout_parts.append('[stage:seed_selector_cpp]\n' + cpp_out)
    if cpp_err:
        _flow.seeding.seed_gate_stderr_parts.append('[stage:seed_selector_cpp]\n' + cpp_err)
    if not cpp_timed_out and cpp_code == 0 and selected_seed_population_path.exists():
        try:
            candidate_rows = _lensde_populations.read_population_csv(selected_seed_population_path)
            if len(candidate_rows) >= target:
                candidate_rows = candidate_rows[:target]
                coverage_ready, coverage_diversity, coverage_requirements = _lensde_populations.seed_pool_refill_diversity_ready(candidate_rows, _flow.optics.variables, active_target_count=target, **diversity_kwargs)
                if coverage_ready:
                    selected_rows = candidate_rows
                    selection_engine = 'cpp'
                    cpp_summary = _lensde_provenance.read_json(selection_summary_path) if selection_summary_path.exists() else {}
                    selection_elapsed = _lensde_configuration.finite_float(cpp_summary.get('elapsed_seconds'), cpp_elapsed)
                else:
                    cpp_coverage_shortfall = 'cpp selector ' + seed_coverage_shortfall_message(coverage_diversity, coverage_requirements)
                    write_seed_generation_progress('selector_active_coverage_repair', strict_pool_count=len(strict_pool), selected_count=len(candidate_rows), cpp_coverage_shortfall=cpp_coverage_shortfall)
                    repair_started = time.time()
                    repaired_rows, coverage_repair_summary = _lensde_populations.repair_seed_population_active_coverage(candidate_rows, strict_pool, _flow.optics.variables, target, min_active_fraction=diversity_kwargs['min_active_fraction'], min_curvature_active_fraction=diversity_kwargs['min_curvature_active_fraction'])
                    coverage_repair_elapsed = max(0.0, time.time() - repair_started)
                    repair_ready, repair_diversity, repair_requirements = _lensde_populations.seed_pool_refill_diversity_ready(repaired_rows, _flow.optics.variables, active_target_count=target, **diversity_kwargs)
                    coverage_repair_summary.update({'elapsed_seconds': coverage_repair_elapsed, 'readiness_complete': repair_ready, 'readiness_summary': repair_diversity, 'readiness_requirements': repair_requirements})
                    if repair_ready:
                        selected_rows = repaired_rows
                        selection_engine = 'cpp_python_active_repair'
                        cpp_summary = _lensde_provenance.read_json(selection_summary_path) if selection_summary_path.exists() else {}
                        selection_elapsed = _lensde_configuration.finite_float(cpp_summary.get('elapsed_seconds'), cpp_elapsed) + coverage_repair_elapsed
                    else:
                        repair_reason = str(coverage_repair_summary.get('reason') or 'readiness checks still failed')
                        selection_fallback_reason = f'{cpp_coverage_shortfall}; targeted repair failed: {repair_reason}'
            else:
                selection_fallback_reason = f'cpp selector produced {len(candidate_rows)} rows; target is {target}'
        except Exception as exc:
            selection_fallback_reason = f'cpp selector output read failed: {exc}'
    else:
        selection_fallback_reason = 'cpp selector timed out' if cpp_timed_out else f'cpp selector returned {cpp_code}'
    if len(selected_rows) < target:
        if selection_fallback_reason:
            _flow.seeding.seed_gate_stderr_parts.append(f'[seed_trace_gate_v2] C++ seed selector fallback: {selection_fallback_reason}\n')
        write_seed_generation_progress('selector_python_full_fallback', strict_pool_count=len(strict_pool), selected_count=len(selected_rows), fallback_reason=selection_fallback_reason)
        selection_started = time.time()
        selected_rows = _lensde_populations.select_traceable_diverse_population(strict_pool, _flow.optics.variables, target, seed_base + 6400, **diversity_kwargs)
        selection_elapsed = max(0.0, time.time() - selection_started)
        selected_population_write_started = time.time()
        _lensde_populations.write_population_csv(selected_seed_population_path, selected_rows)
        selected_population_write_elapsed = max(0.0, time.time() - selected_population_write_started)
    else:
        selected_population_write_elapsed = None
    write_seed_generation_progress('profile_reserve', selected_count=len(selected_rows), selection_engine=selection_engine)
    selected_profile_before_reserve = _lensde_initialization.initial_asphere_profile_summary(selected_rows, _flow.optics.variables, _flow.seeding.asphere_profile_metadata)
    selected_rows = _lensde_initialization.rebalance_initial_asphere_profile_rows(selected_rows, [obj['row'] for obj in strict_pool], _flow.optics.variables, target, _flow.seeding.asphere_profile_metadata)
    selected_profile_after_reserve = _lensde_initialization.initial_asphere_profile_summary(selected_rows, _flow.optics.variables, _flow.seeding.asphere_profile_metadata)
    profile_write_started = time.time()
    _lensde_populations.write_population_csv(selected_seed_population_path, selected_rows)
    profile_write_elapsed = max(0.0, time.time() - profile_write_started)
    if selected_population_write_elapsed is None:
        selected_population_write_elapsed = profile_write_elapsed
    write_seed_generation_progress('diversity_finalize', selected_count=len(selected_rows))
    diversity_started = time.time()
    diversity = _lensde_populations.seed_pool_diversity_summary(selected_rows, _flow.optics.variables, active_target_count=target, **diversity_kwargs)
    diversity_ready, _, diversity_requirements = _lensde_populations.seed_pool_refill_diversity_ready(selected_rows, _flow.optics.variables, active_target_count=target, summary=diversity, **diversity_kwargs)
    diversity_elapsed = max(0.0, time.time() - diversity_started)
    _lensde_provenance.write_json(gate_dir / 'final_diversity_summary.json', diversity)
    _m_seed_io.write_variable_ranges_csv(gate_dir / 'variable_ranges.csv', diversity)
    existing_selection_summary = _lensde_provenance.read_json(selection_summary_path) if selection_summary_path.exists() else {}
    selected_population_write_seconds = selected_population_write_elapsed if selected_population_write_elapsed is not None else existing_selection_summary.get('selected_seed_population_write_seconds')
    _lensde_provenance.write_json(selection_summary_path, {**existing_selection_summary, 'engine': selection_engine, 'strict_pool_count': len(strict_pool), 'target': target, 'selected_count': len(selected_rows), 'elapsed_seconds': selection_elapsed, 'cpp_wall_seconds': cpp_elapsed, 'cpp_returncode': cpp_code, 'cpp_timed_out': cpp_timed_out, 'fallback_reason': selection_fallback_reason, 'cpp_coverage_shortfall': cpp_coverage_shortfall, 'active_coverage_repair_seconds': coverage_repair_elapsed, 'active_coverage_repair': coverage_repair_summary, 'coverage_ready': diversity_ready, 'diversity_requirements': diversity_requirements, 'diversity_summary_seconds': diversity_elapsed, 'strict_candidate_pool_values_write_seconds': strict_pool_write_elapsed, 'full_strict_candidate_pool_written': full_strict_pool_written, 'all_strict_candidate_pool_write_seconds': full_strict_pool_write_elapsed, 'selected_seed_population_write_seconds': selected_population_write_seconds, 'asphere_profiles_before_reserve': selected_profile_before_reserve, 'asphere_profiles_after_reserve': selected_profile_after_reserve})
    final_recheck_requested = bool(_flow.settings.options.get('initial_search_final_strict_recheck', True))
    selected_rows_have_strict_provenance = len(selected_rows) >= target and all((_lensde_populations.variable_aware_seed_row_key(row, _flow.optics.variables) in strict_provenance_keys for row in selected_rows[:target]))
    final_recheck_performed = False
    if final_recheck_requested and selected_rows_have_strict_provenance:
        _lensde_provenance.write_json(gate_dir / 'final_recheck_summary.json', {'skipped': True, 'skip_reason': 'selected_rows_already_strict_and_unchanged', 'strict_count': len(selected_rows), 'target': target, 'elapsed_seconds': 0.0})
    if final_recheck_requested and (not selected_rows_have_strict_provenance):
        final_recheck_performed = True
        final_objects, final_summary = _m_seed_scoring.score_seed_candidate_rows(_flow, 'final_recheck', selected_rows, gate_dir=gate_dir)
        final_strict = _m_seed_scoring.filter_strict_seed_objects(final_objects)
        recheck_round = 0
        while len(final_strict) < target and recheck_round < 2:
            failed_recheck_keys = {_lensde_populations.variable_aware_seed_row_key(obj['row'], _flow.optics.variables) for obj in final_objects if not obj.get('strict')}
            remaining_rows = [obj['row'] for obj in strict_pool if _lensde_populations.variable_aware_seed_row_key(obj['row'], _flow.optics.variables) not in {_lensde_populations.variable_aware_seed_row_key(item['row'], _flow.optics.variables) for item in final_strict} and _lensde_populations.variable_aware_seed_row_key(obj['row'], _flow.optics.variables) not in failed_recheck_keys]
            refill_objects = [{'row': row, 'strict': True, 'merit': 0.0} for row in remaining_rows]
            selected_rows = [obj['row'] for obj in final_strict] + _lensde_populations.select_traceable_diverse_population(refill_objects, _flow.optics.variables, target - len(final_strict), seed_base + 6500 + recheck_round, **diversity_kwargs)
            selected_rows = _lensde_initialization.rebalance_initial_asphere_profile_rows(selected_rows, [obj['row'] for obj in final_strict] + remaining_rows, _flow.optics.variables, target, _flow.seeding.asphere_profile_metadata)
            final_objects, final_summary = _m_seed_scoring.score_seed_candidate_rows(_flow, f'final_recheck_retry_{recheck_round + 1}', selected_rows, gate_dir=gate_dir)
            final_strict = _m_seed_scoring.filter_strict_seed_objects(final_objects)
            recheck_round += 1
        _lensde_provenance.write_json(gate_dir / 'final_recheck_summary.json', final_summary)
        if len(final_strict) < target:
            _flow.seeding.seed_gate_failed_message = f'[seed_trace_gate_v2] final strict recheck found {len(final_strict)} / {target} rows.'
            _flow.seeding.seed_gate_stderr_parts.append(_flow.seeding.seed_gate_failed_message)
            return []
        selected_rows = [obj['row'] for obj in final_strict[:target]]
    if final_recheck_performed:
        final_diversity = _lensde_populations.seed_pool_diversity_summary(selected_rows, _flow.optics.variables, active_target_count=target, **diversity_kwargs)
        final_diversity_ready, _, final_diversity_requirements = _lensde_populations.seed_pool_refill_diversity_ready(selected_rows, _flow.optics.variables, active_target_count=target, summary=final_diversity, **diversity_kwargs)
    else:
        final_diversity = diversity
        final_diversity_ready = diversity_ready
        final_diversity_requirements = diversity_requirements
    _lensde_provenance.write_json(gate_dir / 'final_diversity_summary.json', final_diversity)
    final_profile_summary = _lensde_initialization.initial_asphere_profile_summary(selected_rows, _flow.optics.variables, _flow.seeding.asphere_profile_metadata)
    _lensde_provenance.write_json(gate_dir / 'final_asphere_profile_summary.json', final_profile_summary)
    _m_seed_io.write_variable_ranges_csv(gate_dir / 'variable_ranges.csv', final_diversity)
    _lensde_populations.write_population_csv(gate_dir / 'selected_seed_population.csv', selected_rows)
    if not final_diversity_ready:
        final_shortfall_message = '[seed_trace_gate_v2] final seed ' + seed_coverage_shortfall_message(final_diversity, final_diversity_requirements)
        _flow.seeding.seed_gate_stderr_parts.append(final_shortfall_message + '\n')
        if final_diversity_requirements.get('missing_mandatory_vignetting_range_indices', []) or final_diversity_requirements.get('missing_mandatory_vignetting_active_indices', []):
            _flow.seeding.seed_gate_failed_message = final_shortfall_message
            write_seed_generation_progress('failed_mandatory_vignetting_diversity', selected_count=len(selected_rows), strict_pool_count=len(strict_pool), diversity_ready=False)
            return []
    _flow.seeding.seed_gate_stdout_parts.append(f"[seed_trace_gate_v2] final strict={len(selected_rows)}/{target}, variables_with_range>=0.001={final_diversity.get('variables_with_range_frac_ge_0_001')}/{len(_flow.optics.variables)}, variables_with_range>=0.01={final_diversity.get('variables_with_range_frac_ge_0_01')}/{len(_flow.optics.variables)}, variables_active>={final_diversity.get('diversity_min_active_fraction'):.3g}={final_diversity.get('variables_with_active_frac_ge_min')}/{len(_flow.optics.variables)}, curvature_active>={final_diversity.get('diversity_min_curvature_active_fraction'):.3g}={final_diversity.get('curvature_variables_with_active_frac_ge_min')}/{sum((1 for variable in _flow.optics.variables if variable.get('kind') == 'curvature'))}, median_active_vars={final_diversity.get('median_active_vars')}, median_active_curvature_vars={final_diversity.get('median_active_curvature_vars')}, unique_curvature_masks={final_diversity.get('unique_curvature_masks')}\n")
    gate_elapsed = max(0.0, time.time() - gate_started)
    score_wall_seconds = sum((_lensde_configuration.finite_float(stage.get('elapsed_seconds'), 0.0) + _lensde_configuration.finite_float(stage.get('cpu_recheck_elapsed_seconds'), 0.0) for stage in _flow.seeding.seed_gate_stages))
    score_csv_write_seconds = sum((_lensde_configuration.finite_float(stage.get('csv_write_seconds'), 0.0) for stage in _flow.seeding.seed_gate_stages))
    utilization_audit = {'total_wall_seconds': gate_elapsed, 'score_subprocess_wall_seconds': score_wall_seconds, 'score_csv_write_seconds': score_csv_write_seconds, 'selector_subprocess_wall_seconds': cpp_elapsed, 'python_generation_dedupe_io_and_bookkeeping_seconds': max(0.0, gate_elapsed - score_wall_seconds - cpp_elapsed), 'scored_candidates': sum((int(_lensde_configuration.finite_float(stage.get('scored'), 0.0)) for stage in _flow.seeding.seed_gate_stages)), 'score_stage_count': len(_flow.seeding.seed_gate_stages), 'cuda_score_stage_count': sum((1 for stage in _flow.seeding.seed_gate_stages if bool(stage.get('score_used_cuda')))), 'cpu_score_stage_count': sum((1 for stage in _flow.seeding.seed_gate_stages if not bool(stage.get('score_used_cuda')))), 'production_stopped_at_strict_target': len(strict_rows_after_production) >= target and (not production_diversity_ready), 'coverage_repair_delegated_to_anchor_cloud': bool(undercovered_indices and coverage_injection_enabled), 'sparse_refill_skipped': refill_skipped}
    _lensde_provenance.write_json(gate_dir / 'utilization_audit.json', utilization_audit)
    summaries['adaptive_targets'] = {'global_effective_variable_count': _flow.seeding.global_effective_variable_count, 'global_evaluator_dimension': _flow.seeding.global_evaluator_dimension, 'global_population': _flow.settings.population, 'seed_pool_target': _flow.seeding.seed_pool_target, 'base_seed_pool_target': _flow.seeding.base_seed_pool_target, 'refresh_pool_target': _flow.seeding.refresh_pool_target, 'seed_pool_multiplier': _flow.seeding.seed_pool_multiplier, 'refresh_pool_multiplier': _flow.seeding.refresh_pool_multiplier, 'full_restart_shared_seed_pool_enabled': _flow.seeding.full_restart_shared_seed_pool_enabled, 'full_restart_shared_seed_pool_target': _flow.seeding.full_restart_shared_seed_pool_target, 'full_restart_seed_restarts': _flow.exploration.full_restart_explore_outer_seed_budget if _flow.seeding.full_restart_shared_seed_pool_enabled else 0, 'full_restart_per_outer_seed_pool_target': _flow.seeding.full_restart_per_outer_seed_pool_target, 'full_restart_seed_pool_multiplier': _flow.seeding.full_restart_seed_pool_multiplier, 'adaptive_thickness_bounds': _flow.seeding.adaptive_thickness_bounds, 'adaptive_seed_thickness_caps': _flow.seeding.adaptive_seed_caps, 'evaluator_thickness_constraints': _flow.seeding.evaluator_thickness_constraints, 'thickness_budget': _flow.seeding.seed_thickness_budget, 'ttl_thickness_projection': _flow.seeding.seed_ttl_projection, 'seed_air_thickness_cap': _flow.seeding.seed_air_thickness_cap, 'seed_glass_thickness_cap': _flow.seeding.seed_glass_thickness_cap, 'scout_candidates': scout_candidates, 'configured_scout_candidates': configured_scout_candidates, 'cap_production_candidates': sum((int(_lensde_configuration.finite_float(summary.get('scored'), 0.0)) for summary in production_summaries)), 'pool_ready_after_production': pool_ready_after_production, 'configured_anchor_target': configured_anchor_target, 'anchor_stage_skipped': anchor_stage_skipped, 'anchor_target': anchor_target, 'anchor_min': anchor_min, 'anchor_max_candidates': anchor_max_candidates, 'anchor_batch': anchor_batch, 'cloud_factor': cloud_factor, 'refill_target': refill_target, 'refill_max_candidates': refill_max_candidates, 'refill_batch': refill_batch, 'utilization_audit': utilization_audit}
    _lensde_provenance.write_json(gate_dir / 'adaptive_targets.json', summaries['adaptive_targets'])
    write_seed_generation_progress('complete', selected_count=len(selected_rows[:target]), strict_pool_count=len(strict_pool), diversity_ready=final_diversity_ready)
    return selected_rows[:target]

def generate_mechanical_direct_rows(_flow: SearchState, active_variables: list[dict], target_count: int, generation_seed: int) -> tuple[list[list[float]], dict]:
    """Initial seed generation, TTL projection and seed-domain conversion Implements `generate_mechanical_direct_rows` for this responsibility."""
    return _lensde_populations.make_mechanical_stratified_direct_population(_flow.optics.base_system, active_variables, _flow.seeding.front_pairs, target_count, generation_seed, _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.inputs.target_efl, rest_cap=_flow.seeding.rest_cap, seed_curvature_cap=_flow.seeding.initial_curvature_bound, front_profile=str(_flow.settings.options.get('initial_search_front_profile', 'weak')), perturb_material_variables=bool(_flow.settings.options.get('initial_search_population_perturb_material', True)), seed_material_nd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_nd_cap'), 0.45), seed_material_vd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_vd_cap'), 50.0), bfl_surface_index=_flow.seeding.bfl_surface_index, custom_ttl_range_enabled=_lensde_commands.option_truthy(_flow.settings.options.get('initial_search_baseline_custom_ttl_range_enabled'), False), ttl_lower_fraction=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_ttl_lower_fraction'), 0.7), ttl_upper_fraction=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_ttl_upper_fraction'), 1.5), bfl_lower_factor=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_bfl_lower_factor'), 0.9), bfl_ttl_upper_fraction=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_bfl_ttl_upper_fraction'), 0.5), glass_beta_alpha=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_glass_beta_alpha'), 2.0), glass_beta_beta=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_glass_beta_beta'), 4.0), air_dirichlet_alpha=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_air_dirichlet_alpha'), 2.0), conic_coverage_fraction=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_conic_coverage_fraction'), 0.08), conic_abs_max=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_conic_abs_max'), 100.0), asphere_coverage_fraction=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_asphere_coverage_fraction'), 0.02), asphere_bound_fraction=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_baseline_asphere_bound_fraction'), 1.0), max_active_shape_per_row=max(1, int(_flow.settings.options.get('initial_search_baseline_max_active_shape_per_row', 4) or 4)))

def make_initial_population_in_seed_domain(_flow: SearchState, **kwargs) -> list[list[float]]:
    """Temporarily select seed-domain variables and restore the optimization-domain references in the original finally scope."""
    'Run later restart seed acquisition against the seed-only box.'
    active_variables = _flow.optics.variables
    active_variables_path = _flow.optics.variables_path
    _flow.optics.variables = _flow.seeding.seed_variables
    _flow.optics.variables_path = _flow.seeding.seed_variables_path
    try:
        if _flow.settings.mechanical_direct_seed:
            target_override = max(0, int(kwargs.get('target_override', _flow.seeding.seed_pool_target) or 0))
            seed_base = int(kwargs.get('seed_base', _flow.settings.seed + 909) or 0)
            rows, restart_config = generate_mechanical_direct_rows(_flow, _flow.seeding.seed_variables, target_override, seed_base)
            gate_dir_value = kwargs.get('gate_dir')
            if gate_dir_value:
                restart_dir = Path(gate_dir_value)
                restart_dir.mkdir(parents=True, exist_ok=True)
                restart_config.update({'delivery_role': 'per_outer_fallback', 'population_target': target_override, 'outer_seed': seed_base})
                _lensde_provenance.write_json(restart_dir / 'mechanical_stratified_direct_seed_config.json', restart_config)
            return rows
        return make_traceable_rich_initial_population_v2(_flow, **kwargs)
    finally:
        _flow.optics.variables = active_variables
        _flow.optics.variables_path = active_variables_path
