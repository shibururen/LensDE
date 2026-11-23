"""Extracted paper engine: asphere_contracts."""
from __future__ import annotations
import copy
from lensde.runtime import concurrency as concurrent
import heapq
import hashlib
import json
import math
import mimetypes
import os
import queue
import random
import shutil
import statistics
import sys
import threading
import time
import csv
from collections import Counter
from pathlib import Path
from typing import Callable
from urllib.parse import parse_qs, urlparse
from lensde.runtime import processes as subprocess
from lensde.runtime import native_io_paths
from lensde.optics import dynamic_aperture_contract, vignetting_handoff_policy, stage_illumination_trim, post_asphere_handoff
from lensde.seeds import protocol as seed_protocol
from . import state

from . import apertures as _lensde_apertures
from . import asphere_population as _lensde_asphere_population
from . import asphere_workflow as _lensde_asphere_workflow
from . import commands as _lensde_commands
from . import configuration as _lensde_configuration
from . import defaults as _lensde_defaults
from . import execution as _lensde_execution
from . import fields as _lensde_fields
from . import geometry as _lensde_geometry
from . import initialization as _lensde_initialization
from . import materials as _lensde_materials
from . import orchestration as _lensde_orchestration
from . import populations as _lensde_populations
from . import provenance as _lensde_provenance
from . import tracing as _lensde_tracing

def post_asphere_merit_from_source(source_merit: dict, options: dict, global_merit: dict | None=None) -> dict:
    merit = copy.deepcopy(source_merit)
    dynamic_aperture_contract.apply_merit(merit)
    merit.setdefault('vignetting_boundary_support_contract', 'global_trim_realizability')
    if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_plastic_enabled'), False):
        merit.pop('glass_proximity', None)
    metric = _lensde_defaults.canonical_image_quality_metric(options.get('initial_search_post_asphere_rms_mode'), 'rms_spot_wavefront')
    spot_weight = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_spot_weight', 3.0, 0.0)
    wavefront_weight = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_wavefront_weight', 1.0, 0.0)
    merit['image_quality_metric'] = metric
    merit['quality_spot_scale'] = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_quality_spot_scale', 0.01, 1e-12)
    merit['quality_wavefront_scale'] = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_quality_wavefront_scale', 0.5, 1e-12)
    merit['wavefront_reference'] = _lensde_defaults.canonical_wavefront_reference(options.get('initial_search_post_asphere_wavefront_reference'), 'centroid_sphere')
    merit['num_rays'] = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_trace_rays', 6, 1)
    merit['pupil_arms'] = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_trace_arms', 8, 1)
    merit['pupil_distribution'] = str(options.get('initial_search_post_asphere_trace_distribution') or 'gaussian_ring_arm')
    merit['sampling_seed'] = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_sampling_seed', 1, 0)
    old_operands = merit.get('operands', []) if isinstance(merit.get('operands'), list) else []
    kept_operands = [copy.deepcopy(operand) for operand in old_operands if str(operand.get('name', '')).lower() not in {'rms_spot', 'rms_wavefront', 'single_ray_spot_tail', 'single_ray_spot_guard', 'single_ray_wavefront_guard'}]
    merit['unified_image_mean_spot_weight'] = spot_weight
    merit['unified_image_single_ray_spot_tail_weight'] = max(0.0, _lensde_configuration.finite_float(merit.get('unified_image_single_ray_spot_tail_weight'), 0.012))
    merit['single_ray_spot_tail_extra_edge_min_field_radius'] = min(2.0, max(0.0, _lensde_configuration.finite_float(merit.get('single_ray_spot_tail_extra_edge_min_field_radius'), 0.85)))
    merit['unified_image_wavefront_weight'] = wavefront_weight
    merit['operands'] = kept_operands + _lensde_defaults.image_quality_operands(metric, spot_weight=spot_weight, spot_tail_weight=merit['unified_image_single_ray_spot_tail_weight'], wavefront_weight=wavefront_weight, guard_weight=1.0)
    merit['weight_spot'] = spot_weight
    if isinstance(global_merit, dict):
        for key in state.GLOBAL_EDGE_MERIT_KEYS:
            if key in global_merit:
                merit[key] = copy.deepcopy(global_merit[key])
    return post_asphere_handoff.configure_illumination(merit, options)


def post_asphere_primary_source_merit(candidate_pool: list[Path], fallback_merit: dict | None) -> tuple[dict, Path | None]:
    """Load the merit contract attached to the selected pre-asphere mother.

    The material funnel can tighten objective tolerances (notably CRA) after
    the GUI's original global merit was created.  Post-asphere refinement must
    therefore inherit the promoted candidate's merit rather than rebuilding
    from that stale global fallback.  The fallback remains useful when reading
    older run artifacts that do not contain a candidate-local merit file.
    """
    if candidate_pool:
        merit_path = candidate_pool[0] / 'merit.json'
        source_merit = first_existing_json([merit_path], None)
        if isinstance(source_merit, dict):
            return (source_merit, merit_path)
    return (copy.deepcopy(fallback_merit) if isinstance(fallback_merit, dict) else {}, None)


def first_existing_json(paths: list[Path], default=None):
    for path in paths:
        path = native_io_paths.python_path(path)
        try:
            if path.exists():
                return _lensde_provenance.read_json(path)
        except Exception:
            continue
    return copy.deepcopy(default)


def post_asphere_requires_real_materials(summary: dict) -> bool:
    terminal_mode = str(summary.get('material_terminal_mode') or '').strip().lower()
    return terminal_mode in state.POST_ASPHERE_REAL_MATERIAL_TERMINAL_MODES


def post_asphere_real_material_contract(system: dict) -> dict:
    surfaces = system.get('surfaces', []) if isinstance(system, dict) else []
    if not isinstance(surfaces, list):
        surfaces = []
    catalog_materials: list[dict] = []
    invalid_materials: list[dict] = []
    for index, surface in enumerate(surfaces):
        if not isinstance(surface, dict):
            continue
        material = surface.get('material_after')
        if not isinstance(material, dict):
            material = surface.get('material')
        if not isinstance(material, dict) or not material:
            continue
        mode = str(material.get('mode') or '').strip().lower()
        name = str(material.get('name') or '').strip()
        reference = str(material.get('reference') or material.get('library') or material.get('filename') or '').strip()
        nd = _lensde_configuration.finite_float(material.get('nd'), math.nan)
        is_air = mode == 'air' or name.lower() == 'air' or (math.isfinite(nd) and nd <= 1.0005)
        if is_air:
            continue
        entry = {'surface_index': index, 'mode': mode, 'name': name, 'reference': reference}
        if mode == 'glass' and bool(name) and bool(reference):
            catalog_materials.append(entry)
        else:
            invalid_materials.append(entry)
    eligible = bool(catalog_materials) and (not invalid_materials)
    if invalid_materials:
        reason = 'continuous or unresolved refractive materials present'
    elif not catalog_materials:
        reason = 'no refractive catalog materials found'
    else:
        reason = 'all refractive materials are catalog glasses'
    return {'eligible': eligible, 'reason': reason, 'catalog_material_count': len(catalog_materials), 'invalid_material_count': len(invalid_materials), 'catalog_materials': catalog_materials, 'invalid_materials': invalid_materials}


def post_asphere_real_material_contract_from_dir(source_dir: Path) -> dict:
    system = first_existing_json([source_dir / 'system.json', source_dir / 'checkpoint_system.json', source_dir / 'system_input.json', source_dir / 'input_system.json'], {})
    if not isinstance(system, dict) or not system:
        return {'eligible': False, 'reason': 'source system not found', 'catalog_material_count': 0, 'invalid_material_count': 0, 'catalog_materials': [], 'invalid_materials': []}
    return post_asphere_real_material_contract(system)


def post_asphere_locked_variable_contract_from_dir(source_dir: Path) -> dict:
    """Validate that a real-material post-asphere source has no material DOFs."""
    variables_path = source_dir / 'variables.json'
    if not variables_path.is_file():
        return {'eligible': True, 'reason': 'variables.json not present; no declared material variables', 'variable_count': 0, 'invalid_variable_count': 0, 'invalid_variables': []}
    try:
        payload = _lensde_provenance.read_json(variables_path)
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as exc:
        return {'eligible': False, 'reason': f'locked post-asphere variables could not be read: {exc}', 'variable_count': 0, 'invalid_variable_count': 0, 'invalid_variables': []}
    variables = payload.get('variables', payload) if isinstance(payload, dict) else payload
    if not isinstance(variables, list):
        return {'eligible': False, 'reason': 'locked post-asphere variables payload is not a list', 'variable_count': 0, 'invalid_variable_count': 0, 'invalid_variables': []}
    invalid_variables = [{'kind': str(variable.get('kind') or '').lower(), 'index': variable.get('index'), 'term': variable.get('term')} for variable in variables if isinstance(variable, dict) and str(variable.get('kind') or '').lower() in state.POST_ASPHERE_LOCKED_MATERIAL_VARIABLE_KINDS]
    return {'eligible': not invalid_variables, 'reason': 'locked post-asphere source has no material variables' if not invalid_variables else 'locked post-asphere source retains material variables', 'variable_count': len(variables), 'invalid_variable_count': len(invalid_variables), 'invalid_variables': invalid_variables}


def post_asphere_promotion_material_contract(summary: dict, source_dir: Path) -> dict:
    contract = post_asphere_real_material_contract_from_dir(source_dir)
    required = post_asphere_requires_real_materials(summary)
    return {**contract, 'required': required, 'promotion_allowed': not required or bool(contract['eligible'])}


def post_asphere_material_promotion_decision(best_merit: object, incumbent_merit: object, best_contract: dict, incumbent_contract: dict) -> dict:
    best_value = _lensde_configuration.finite_float(best_merit, math.inf)
    incumbent_value = _lensde_configuration.finite_float(incumbent_merit, math.inf)
    required = bool(best_contract.get('required') or incumbent_contract.get('required'))
    best_allowed = bool(best_contract.get('promotion_allowed'))
    incumbent_allowed = bool(incumbent_contract.get('promotion_allowed'))
    repair_invalid_incumbent = required and (not incumbent_allowed)
    strict_merit_improvement = math.isfinite(best_value) and math.isfinite(incumbent_value) and (best_value < incumbent_value)
    promote = math.isfinite(best_value) and best_allowed and (repair_invalid_incumbent or strict_merit_improvement)
    return {'promote': promote, 'repair_invalid_incumbent': repair_invalid_incumbent, 'strict_merit_improvement': strict_merit_improvement, 'reason': 'replace incumbent that violates the real-material contract' if promote and repair_invalid_incumbent else 'strict common-merit improvement' if promote else 'promotion conditions not met'}


def post_asphere_candidate_source_dirs(summary: dict, run_dir: Path, limit: int | None=None, audit: list[dict] | None=None) -> list[Path]:
    out: list[Path] = []
    seen_paths: set[str] = set()
    seen_systems: set[str] = set()
    require_real_materials = post_asphere_requires_real_materials(summary)

    def record(item: dict) -> None:
        if audit is not None:
            audit.append(item)

    def add(path_value: object, origin: str) -> None:
        text = str(path_value or '').strip()
        if not text:
            return
        path = Path(text)
        if not path.exists() or not path.is_dir():
            record({'origin': origin, 'source_dir': str(path), 'status': 'rejected', 'reason': 'candidate directory not found'})
            return
        key = str(path.resolve()).lower()
        if key in seen_paths:
            record({'origin': origin, 'source_dir': str(path), 'status': 'duplicate_path'})
            return
        seen_paths.add(key)
        system = first_existing_json([path / 'system.json', path / 'checkpoint_system.json', path / 'system_input.json', path / 'input_system.json'], {})
        material_contract = post_asphere_real_material_contract(system) if isinstance(system, dict) and system else post_asphere_real_material_contract_from_dir(path)
        if require_real_materials and (not material_contract['eligible']):
            record({'origin': origin, 'source_dir': str(path), 'status': 'rejected', 'reason': material_contract['reason'], 'material_contract': material_contract})
            return
        variable_contract = post_asphere_locked_variable_contract_from_dir(path)
        if require_real_materials and (not variable_contract['eligible']):
            record({'origin': origin, 'source_dir': str(path), 'status': 'rejected', 'reason': variable_contract['reason'], 'material_contract': material_contract, 'variable_contract': variable_contract})
            return
        if isinstance(system, dict) and system:
            encoded = json.dumps(system, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('utf-8')
            fingerprint = hashlib.sha256(encoded).hexdigest()
            if fingerprint in seen_systems:
                record({'origin': origin, 'source_dir': str(path), 'status': 'duplicate_system'})
                return
            seen_systems.add(fingerprint)
        out.append(path)
        item = {'origin': origin, 'source_dir': str(path), 'status': 'accepted'}
        if require_real_materials:
            item['material_contract'] = material_contract
            item['variable_contract'] = variable_contract
        record(item)
    selected = summary.get('selected_final_result', {}) if isinstance(summary.get('selected_final_result'), dict) else {}
    add(selected.get('selected_result_dir') or selected.get('target_dir') or selected.get('source_dir'), 'selected_final_result')
    add(run_dir / 'de_dls_key_outputs' / 'final_selected_result', 'de_dls_key_outputs/final_selected_result')
    topology_keys = ['fixed_glass_feedback_topologies', 'final_material_topologies']
    if require_real_materials:
        topology_keys.insert(0, 'final_locked_glass_dls_topologies')
    for key in topology_keys:
        rows = summary.get(key, []) if isinstance(summary.get(key), list) else []
        for index, row in enumerate(rows, start=1):
            if not isinstance(row, dict):
                continue
            add(row.get('target_dir') or row.get('selected_result_dir') or row.get('source_dir'), f'{key}[{index}]')
    if limit is None or limit <= 0:
        return out
    return out[:limit]


def post_asphere_prepare_case(source_dir: Path, case_root: Path, options: dict, fallback_merit: dict, common_merit: dict | None=None, promotion_handoff: dict | None=None) -> dict:
    promotion_contract = promotion_handoff.get('handoff_contract', {}) if isinstance(promotion_handoff, dict) else {}
    calibrated_source_text = str(promotion_handoff.get('calibrated_source_system_path') or '').strip() if isinstance(promotion_handoff, dict) else ''
    calibrated_source_path = Path(calibrated_source_text) if calibrated_source_text else None
    reusable_promotion_source = bool(isinstance(promotion_contract, dict) and _lensde_commands.option_truthy(promotion_contract.get('reusable_for_candidate_preparation'), False) and (calibrated_source_path is not None) and calibrated_source_path.is_file())
    if reusable_promotion_source:
        source_system = _lensde_provenance.read_json(calibrated_source_path)
    else:
        source_system = first_existing_json([source_dir / 'system.json', source_dir / 'checkpoint_system.json', source_dir / 'system_input.json', source_dir / 'input_system.json'], {})
    if not isinstance(source_system, dict) or not source_system:
        raise ValueError(f'source system not found: {source_dir}')
    variables_payload = first_existing_json([source_dir / 'variables.json'], {'variables': []})
    source_variables = variables_payload.get('variables', variables_payload) if isinstance(variables_payload, dict) else variables_payload
    if not isinstance(source_variables, list):
        source_variables = []
    selected_surfaces = _lensde_initialization.selected_post_asphere_surface_indices(source_system, options)
    if not selected_surfaces:
        raise ValueError('no selected post-asphere lens surfaces')
    post_asphere_aiming_contract(options)
    post_system = _lensde_configuration.system_with_ray_aiming(source_system, options)
    post_system['asphere_root_policy'] = post_asphere_root_scoring_policy(options)
    zero_initialized_flow = canonical_post_asphere_flow(options.get('initial_search_post_asphere_flow')) in {'curvature_normalized_formation_periodic', 'single_mother', 'single_stage', 'conic_multistart'}
    zero_asphere_coefficients = zero_initialized_flow and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_multistart_zero_asphere_coefficients'), True)
    inherited_asphere_orders: dict[str, list[int]] = {}
    zero_initialized_asphere_orders: dict[str, list[int]] = {}
    max_asphere_term = _lensde_populations.asphere_term_for_order(_lensde_initialization.post_asphere_max_order(options))
    source_merit = first_existing_json([source_dir / 'merit.json'], fallback_merit if isinstance(fallback_merit, dict) else {})
    post_merit = copy.deepcopy(common_merit) if isinstance(common_merit, dict) else post_asphere_merit_from_source(source_merit if isinstance(source_merit, dict) else {}, options, fallback_merit if isinstance(fallback_merit, dict) else None)
    preferred_field_nodes = post_asphere_preferred_field_nodes(options)
    vignetting_bridge_report: dict = {'performed': False, 'reason': 'vignetting_disabled', 'target_field_hy_interpolation': False}
    active_vignetting = _lensde_configuration.vignetting_optimization_enabled(post_merit)

    def reuse_promotion_vignetting_contract(target_nodes: list[float], reference_radius: float) -> bool:
        nonlocal post_system, vignetting_bridge_report
        if not (active_vignetting and reusable_promotion_source):
            return False
        exact_fields = _lensde_fields.exact_fields_for_normalized_nodes(post_system, target_nodes, reference_max_radius=reference_radius)
        if exact_fields is None or len(exact_fields) != len(post_system.get('fields', []) or []):
            return False
        promotion_bridge = promotion_handoff.get('vignetting_field_bridge', {}) if isinstance(promotion_handoff, dict) else {}
        if not (isinstance(promotion_bridge, dict) and promotion_bridge.get('target_field_hy_interpolation') is False and _lensde_commands.option_truthy(promotion_bridge.get('source_fields_determine_apertures'), False)):
            return False
        post_system['fields'] = exact_fields
        vignetting_bridge_report = {'performed': False, 'reason': 'reused_calibrated_promotion_handoff', 'target_nodes': list(target_nodes), 'target_field_hy_interpolation': False, 'source_fields_determine_apertures': True, 'calibrated_source_system_path': str(calibrated_source_path), 'promotion_vignetting_field_bridge': copy.deepcopy(promotion_bridge)}
        return True
    if preferred_field_nodes:
        reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, post_system)
        if active_vignetting:
            if not reuse_promotion_vignetting_contract(preferred_field_nodes, reference_radius):
                post_system, vignetting_bridge_report = _lensde_fields.aperture_realized_vignetting_field_bridge(post_system, post_merit, preferred_field_nodes, native_io_paths.named_child(case_root, 'vignetting_field_bridge'), variables=source_variables, reference_max_radius=reference_radius, recalibrate_existing_contract=True)
        else:
            post_system = _lensde_fields.resample_system_fields_for_asphere(post_system, preferred_field_nodes, reference_max_radius=reference_radius)
    else:
        target_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_field_count', 9, 1)
        preserve_existing_indices = any((str(variable.get('kind', '')).lower() in {'vignette_x', 'vignette_y', 'fvcy', 'fvdy', 'vignette_center_y', 'vignette_shift_y', 'vignette_magnitude_y', 'vignette_asymmetry_y'} for variable in source_variables if isinstance(variable, dict)))
        densified_contract = _lensde_fields.densify_system_fields_for_asphere(post_system, target_count, preserve_existing_indices=preserve_existing_indices)
        if active_vignetting:
            reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, post_system)
            target_nodes = _lensde_fields.normalized_field_nodes(densified_contract, reference_max_radius=reference_radius)
            if not reuse_promotion_vignetting_contract(target_nodes, reference_radius):
                post_system, vignetting_bridge_report = _lensde_fields.aperture_realized_vignetting_field_bridge(post_system, post_merit, target_nodes, native_io_paths.named_child(case_root, 'vignetting_field_bridge'), variables=source_variables, reference_max_radius=reference_radius, recalibrate_existing_contract=True)
        else:
            post_system = densified_contract
    for surface_index in selected_surfaces:
        if 0 <= surface_index < len(post_system.get('surfaces', []) or []):
            surface = post_system['surfaces'][surface_index]
            surface['kind'] = 'even_asphere'
            if zero_asphere_coefficients:
                existing = surface.get('asphere_coefficients', [])
                if not isinstance(existing, list):
                    existing = []
                existing = list(existing)
                required_count = max(len(existing), max_asphere_term + 1)
                surface['asphere_coefficients'] = existing + [0.0] * (required_count - len(existing))
                inherited_asphere_orders[str(surface_index)] = [_lensde_populations.asphere_order_for_term(term) for term in range(1, len(existing))]
                zero_initialized_asphere_orders[str(surface_index)] = [_lensde_populations.asphere_order_for_term(term) for term in range(max(1, len(existing)), max_asphere_term + 1)]
    post_variables = _lensde_initialization.append_post_asphere_variables(post_system, source_variables, options)
    post_variables = _lensde_initialization.sync_post_asphere_variables_for_system(post_system, post_variables, post_merit)
    closure_report = copy.deepcopy(promotion_contract.get('vignetting_closure', {}))
    if not (reusable_promotion_source and closure_report.get('accepted') and (vignetting_bridge_report.get('reason') == 'reused_calibrated_promotion_handoff')):
        post_system, post_variables, _, closure_report = post_asphere_handoff.close(sys.modules[__name__], post_system, post_variables, post_merit, options, native_io_paths.named_child(case_root, 'vignetting_closure'), _lensde_provenance.exe_path(False))
    initial_values = [_lensde_populations.variable_current_value_for_system(post_system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in post_variables]
    seed_rows = _lensde_initialization.make_post_asphere_population(post_system, post_variables, initial_values, post_asphere_multistart_input_population_size(options, _lensde_populations.population_sizing_variable_count(post_variables)), _lensde_commands.option_int_from(options, 'initial_search_post_asphere_rng_seed', 104729, 0), _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_scale', 0.35, 0.0))
    case_root.mkdir(parents=True, exist_ok=True)
    system_path = case_root / 'system_input.json'
    variables_path = case_root / 'variables.json'
    merit_path = case_root / 'merit.json'
    seed_path = case_root / 'initial_population.csv'
    _lensde_provenance.write_json(system_path, post_system)
    _lensde_provenance.write_json(variables_path, {'variables': post_variables})
    _lensde_provenance.write_json(merit_path, post_merit)
    _lensde_populations.write_population_csv(seed_path, seed_rows)
    return {'source_dir': str(source_dir), 'system': post_system, 'variables': post_variables, 'merit': post_merit, 'system_path': system_path, 'variables_path': variables_path, 'merit_path': merit_path, 'seed_path': seed_path, 'selected_surface_indices': selected_surfaces, 'lens_groups': _lensde_initialization.selected_post_asphere_lens_groups(source_system, options), 'population_rows': len(seed_rows), 'hard_bound_mode': str(options.get('initial_search_post_asphere_bound_mode') or 'adaptive_sag').strip().lower(), 'population_scale_source': 'variable_lower_upper', 'population_scale': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_scale', 0.35, 0.0), 'zero_asphere_coefficients': zero_asphere_coefficients, 'asphere_initialization_policy': 'preserve_existing_zero_new' if zero_asphere_coefficients else 'preserve_all', 'inherited_asphere_orders': inherited_asphere_orders, 'zero_initialized_asphere_orders': zero_initialized_asphere_orders, 'vignetting_field_bridge': vignetting_bridge_report, 'promotion_handoff_reused': reusable_promotion_source, 'vignetting_closure': closure_report}


def post_asphere_stage_vignetting_recalibration_enabled(options: dict, merit: dict | None=None) -> bool:
    """Automatic stage/terminal calibration requires illumination optimization."""
    return _lensde_configuration.vignetting_optimization_enabled(merit) and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_stage_boundary_vignetting_recalibration'), True)


def post_asphere_aiming_contract(options: dict) -> str:
    """Reject any stage-local override of the post-asphere aiming mode."""
    actual = _lensde_configuration.canonical_aiming_mode(options.get('aiming'), 'robust')
    expected = _lensde_configuration.canonical_aiming_mode(options.get(state.POST_ASPHERE_AIMING_CONTRACT_OPTION, actual), actual)
    if actual != expected:
        raise ValueError(f'post-asphere aiming mode changed from {expected!r} to {actual!r}')
    return expected


def post_asphere_trace_coverage_contract(options: dict) -> dict:
    """Return the effective near-trace contract shared by every post-asphere stage."""
    allow_near_trace = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_allow_near_trace_infeasible'), True)
    configured_minimum = min(1.0, _lensde_commands.option_float_from(options, 'initial_search_post_asphere_min_trace_coverage', 0.97, 0.0))
    required_minimum = configured_minimum if allow_near_trace else 1.0
    return {'allow_near_trace_infeasible': allow_near_trace, 'configured_min_trace_coverage': configured_minimum, 'required_min_trace_coverage': required_minimum, 'trace_coverage_epsilon': max(0.0, 1.0 - required_minimum)}


def post_asphere_trace_coverage_is_valid(coverage: object, options: dict) -> bool:
    value = _lensde_configuration.finite_float(coverage, math.nan)
    required = post_asphere_trace_coverage_contract(options)['required_min_trace_coverage']
    return math.isfinite(value) and value + 1e-12 >= required


def post_asphere_stage_base_args(exe: Path, system_path: Path, merit_path: Path, variables_path: Path, options: dict, *, dynamic_apertures: bool=True, use_cuda_evaluator: bool=False) -> list[str]:
    args = [str(exe), '--system', str(system_path), '--merit', str(merit_path), '--aiming', post_asphere_aiming_contract(options), '--aiming-iter', str(_lensde_commands.option_int_from(options, 'aiming_iter', 20, 1)), '--trace-policy', 'soft_coverage', '--rays', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_trace_rays', 6, 1)), '--arms', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_trace_arms', 8, 1)), '--distribution', str(options.get('initial_search_post_asphere_trace_distribution') or 'gaussian_ring_arm'), '--sampling-seed', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_sampling_seed', 1, 0)), '--spot-rays', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_spot_rays', 41, 1)), '--spot-arms', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_spot_arms', 64, 1)), '--spot-distribution', str(options.get('initial_search_post_asphere_spot_distribution') or 'hexapolar'), '--spot-sampling-seed', str(_lensde_commands.option_int_from(options, 'spot_sampling_seed', 1, 0)), '--wavefront-reference', _lensde_defaults.canonical_wavefront_reference(options.get('initial_search_post_asphere_wavefront_reference'), 'centroid_sphere'), '--variables', str(variables_path), '--optimizer-internal-scaling', 'edge_sag_normalized', '--asphere-root-policy', post_asphere_root_scoring_policy(options)]
    if dynamic_apertures:
        dynamic_aperture_contract.apply_command(args)
    _lensde_materials.append_aiming_policy_args(args, options)
    if not use_cuda_evaluator:
        args.append('--cpu')
    return args


def post_asphere_root_scoring_policy(options: dict) -> str:
    root_policy_token = str(options.get('initial_search_post_asphere_root_scoring_policy') or 'unified_certified').strip().lower().replace('-', '_').replace(' ', '_')
    if root_policy_token in {'unified_certified', 'unified_certified_experimental'}:
        return 'unified_certified'
    if root_policy_token in {'certified_first_root', 'strict', 'strict_first_root'}:
        return 'certified_first_root'
    if root_policy_token in {'experimental_fast_recover', 'fast_recover'}:
        return 'fast_recover'
    if root_policy_token in {'confirmed_with_fallback', 'two_level_fast_recover', 'two_level'}:
        return 'confirmed_with_fallback'
    return 'unified_certified'


def post_asphere_de_command(exe: Path, prepared: dict, out_dir: Path, options: dict, seed_offset: int, *, use_cuda_evaluator: bool=False, glass_search_path: Path | None=None, glass_search_config: dict | None=None) -> list[str]:
    population_size = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_population', 0, 0)
    if population_size <= 0:
        population_size = max(1, int(prepared.get('population_rows', 0) or 0))
    if population_size <= 1 and (not prepared.get('population_rows')):
        population_size = post_asphere_multistart_input_population_size(options, _lensde_populations.population_sizing_variable_count(prepared.get('variables', [])))
    root_policy = post_asphere_root_scoring_policy(options)
    command = post_asphere_stage_base_args(exe, prepared['system_path'], prepared['merit_path'], prepared['variables_path'], options, use_cuda_evaluator=use_cuda_evaluator)
    if root_policy == 'certified_first_root' and '--cpu' not in command:
        command.append('--cpu')
    command.extend(['--initial-population', str(prepared['seed_path']), '--adaptive-hybrid-de-dls', '--pop', str(population_size), '--gen', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_generations', 400, 1)), '--hybrid-outer', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_outer', 20, 1)), '--hybrid-de-gen', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_generations', 400, 1)), '--hybrid-parents', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_hybrid_parents', 4, 1)), '--hybrid-polish-workers', '0', '--hybrid-refresh', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_refresh', 0.05, 0.0):.17g}", '--hybrid-distance-metric', 'bound_normalized', '--hybrid-local-iter', '0', '--no-polish', '--de-variant', str(options.get('initial_search_post_asphere_de_variant') or 'jade_pbest_archive_lpsr_feas'), '--de-strategy', str(options.get('initial_search_post_asphere_de_strategy') or 'rand1bin'), '--constraint-handling', str(options.get('initial_search_post_asphere_constraint_handling') or 'feasibility_rules'), '--adaptive-search-purpose', 'post_asphere_refine', '--adaptive-min-population', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_min_population', 20, 1)), '--basin-disable', '--adaptive-no-topology-summary', '--seed', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_seed', 20260709, 0) + seed_offset), '--out-dir', str(out_dir)])
    if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_single_stage_active'), False):
        command.extend(['--refresh-target-count', str(population_size // 2), '--refresh-parent-count', '16', '--refresh-generation-mode', 'top_half_single_block', '--refresh-asphere-sag-um', '0.1', '--refresh-asphere-correlation-length', '0.35', '--refresh-trace-feasible-gate', '--refresh-no-trace-archive', '--refresh-max-merit-ratio', '2.0', '--refresh-max-candidates', str(3 * population_size), '--refresh-no-adaptive-trigger', '--refresh-geometry-child-fraction', '0.2', '--refresh-geometry-local-scale', '0.03', '--refresh-vignetting-child-fraction', '0.05' if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_single_stage_has_vignetting'), False) else '0', '--refresh-vignetting-bound-fraction', '0.001'])
    elif _lensde_commands.option_truthy(options.get('initial_search_post_asphere_periodic_refresh_active'), False):
        command.extend(['--refresh-target-count', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_periodic_refresh_target_count', 0, 0)), '--refresh-parent-count', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_periodic_refresh_parent_count', 16, 1)), '--refresh-generation-mode', 'top_half_single_surface_correlated_sag', '--refresh-asphere-sag-um', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_periodic_refresh_sag_um', 0.3, 0.0):.17g}", '--refresh-asphere-correlation-length', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_periodic_refresh_correlation_length', 0.35, 0.001):.17g}", '--refresh-no-adaptive-trigger', '--refresh-no-trace-feasible-gate', '--refresh-no-trace-archive'])
    if not _lensde_commands.option_truthy(options.get('initial_search_post_asphere_inject_best'), True):
        command.append('--hybrid-no-inject-best')
    if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_allow_small_initial_population'), False):
        command.append('--adaptive-allow-small-initial-population')
    if use_cuda_evaluator and root_policy != 'certified_first_root':
        command.extend(['--de-cuda-min-batch', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_de_cuda_min_batch', 1, 1))])
    trace_contract = post_asphere_trace_coverage_contract(options)
    if trace_contract['allow_near_trace_infeasible']:
        command.extend(['--adaptive-allow-near-trace-infeasible', '--trace-coverage-epsilon', f"{trace_contract['trace_coverage_epsilon']:.17g}"])
    generations = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_generations', 400, 1)
    lpsr_horizon_epochs = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_lpsr_horizon_epochs', 24, 0)
    lpsr_horizon_generations = lpsr_horizon_epochs if lpsr_horizon_epochs >= generations * 2 else lpsr_horizon_epochs * generations
    if not _lensde_commands.option_truthy(options.get('initial_search_post_asphere_lpsr_enabled'), False):
        command.append('--adaptive-no-lpsr')
    if lpsr_horizon_generations > 0:
        command.extend(['--adaptive-lpsr-horizon-generations', str(lpsr_horizon_generations)])
    if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_early_stop'), True):
        command.extend(['--adaptive-early-stop', '--adaptive-early-stop-metric', 'best_merit', '--adaptive-early-stop-min-epochs', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_early_stop_min_epochs', 2, 0)), '--adaptive-early-stop-window', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_early_stop_window', 1, 1)), '--adaptive-early-stop-min-rel-improve', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_early_stop_min_rel_improve', 0.01, 0.0):.17g}", '--adaptive-early-stop-min-abs-improve', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_early_stop_min_abs_improve', 0.01, 0.0):.17g}", '--adaptive-early-stop-min-lpsr-progress', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_early_stop_min_lpsr_progress', 0.1, 0.0):.17g}", '--adaptive-early-stop-tail-epochs', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_early_stop_tail_epochs', 0, 0))])
        if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_early_stop_use_min_improvement_threshold'), False):
            command.append('--adaptive-early-stop-use-min-improvement-threshold')
    if glass_search_path is not None and isinstance(glass_search_config, dict):
        _lensde_materials.append_glass_search_args(command, glass_search_path, glass_search_config, options)
    return command


def post_asphere_dls_command(exe: Path, system_path: Path, merit_path: Path, variables_path: Path, out_dir: Path, options: dict, iterations: int) -> list[str]:
    command = post_asphere_stage_base_args(exe, system_path, merit_path, variables_path, options, dynamic_apertures=True)
    command.extend(['--local', '--local-iter', str(max(1, int(iterations))), '--local-lambda', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_local_lambda', 0.01, 0.0):.17g}", '--local-fd', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_local_fd', 0.0001, 0.0):.17g}", '--local-min-step', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_local_min_step', 1e-08, 0.0):.17g}", '--local-tol', f"{_lensde_commands.option_float_from(options, 'initial_search_post_asphere_local_tol', 0.0001, 0.0):.17g}", '--local-max-reject', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_local_max_reject', 80, 0)), '--local-method', str(options.get('initial_search_post_asphere_method') or 'trf'), '--local-bounds-mode', str(options.get('initial_search_post_asphere_local_bounds_mode') or 'safety'), '--local-residual-mode', str(options.get('initial_search_post_asphere_local_residual_mode') or 'expanded'), '--out-dir', str(out_dir)])
    trace_contract = post_asphere_trace_coverage_contract(options)
    if trace_contract['allow_near_trace_infeasible']:
        command.extend(['--local-allow-near-trace-infeasible', '--local-trace-coverage-epsilon', f"{trace_contract['trace_coverage_epsilon']:.17g}"])
    return command


def score_merit_from_dir(stage_dir: Path) -> float:
    merits = []
    for score_path in [stage_dir / 'score.json', stage_dir / 'checkpoint_score.json']:
        score = _lensde_provenance.read_json_optional(score_path)
        if isinstance(score, dict):
            merits.append(_lensde_configuration.finite_float(score.get('merit'), math.inf))
    return min(merits, default=math.inf)


def is_strict_merit_improvement(candidate_merit: object, incumbent_merit: object) -> bool:
    candidate = _lensde_configuration.finite_float(candidate_merit, math.inf)
    incumbent = _lensde_configuration.finite_float(incumbent_merit, math.inf)
    return math.isfinite(candidate) and (not math.isfinite(incumbent) or candidate < incumbent)


def post_asphere_rms_image_quality(score: dict) -> dict:
    """Combine both RMS families using their existing scales and field weights.

    Robust penalties precede the trace gate and category normalization. Using
    them prevents partial coverage from improving the image-quality ranking by
    merely attenuating the image gate, and excludes all non-RMS constraints.
    """
    totals = {'rms_spot': 0.0, 'rms_wavefront': 0.0}
    weights = {name: 0.0 for name in totals}
    for item in score.get('contributions', []):
        name = item.get('name')
        if name not in totals:
            continue
        weight = _lensde_configuration.finite_float(item.get('base_weight'), math.nan)
        penalty = _lensde_configuration.finite_float(item.get('robust_penalty'), math.nan)
        if not (math.isfinite(weight) and weight >= 0 and math.isfinite(penalty) and (penalty >= 0)):
            return {'value': math.inf, 'reason': 'invalid_rms_contribution'}
        weights[name] += weight
        totals[name] += penalty
    if any((weight <= 0 for weight in weights.values())):
        return {'value': math.inf, 'reason': 'missing_active_rms_family'}
    weight_sum = sum(weights.values())
    return {'value': sum(totals.values()) / weight_sum, 'rms_spot_component': totals['rms_spot'] / weight_sum, 'rms_wavefront_component': totals['rms_wavefront'] / weight_sum, 'weight_sum': weight_sum, 'policy': 'weighted_normalized_robust_rms_spot_plus_wavefront_ungated'}


def post_asphere_handoff_image_quality(score: dict, *, min_coverage: float=0.0) -> dict:
    """Use the complete image penalty without rewarding a reduced trace gate.

    In the common merit contract, image_penalty / image_gate includes all
    active image operands (including chief angle and distortion), with their
    configured weights. RMS-only diagnostics remain separate from selection.
    """
    if not vignetting_handoff_policy.score_usable(score, min_trace=min_coverage, min_field=min_coverage):
        return {'value': math.inf, 'reason': 'invalid_handoff_trace'}
    penalty = _lensde_configuration.finite_float(score.get('image_penalty'), math.nan)
    gate = _lensde_configuration.finite_float(score.get('image_gate'), math.nan)
    if not (math.isfinite(penalty) and penalty >= 0 and math.isfinite(gate) and (0 < gate <= 1)):
        return {'value': math.inf, 'reason': 'invalid_image_penalty_or_gate'}
    value = penalty / gate
    if not math.isfinite(value):
        return {'value': math.inf, 'reason': 'invalid_ungated_image_penalty'}
    return {'value': value, 'image_penalty': penalty, 'image_gate': gate, 'policy': 'complete_image_penalty_without_trace_gate'}


def post_asphere_handoff_ranking_key(row: dict) -> tuple:
    """Rank mothers by complete image quality, then Merit, on common sampling."""
    return (_lensde_configuration.finite_float(row.get('common_ungated_image_penalty'), math.inf), _lensde_configuration.finite_float(row.get('merit'), math.inf), str(row.get('source_dir') or ''))


def select_post_asphere_handoffs(rows: list[dict], limit: int) -> list[dict]:
    """Rank completed handoffs while removing explicit fallback results.

    ``vignetting_closure.accepted`` is an audit result, rather than the sole
    admission bit.  A normal closure that reaches the configured maximum
    rounds is usable even when that flag is false.  A failed closure that
    retained the source system, however, is a fallback and must not seed the
    direction Scout.
    """

    def handoff_is_usable(row: dict) -> bool:
        contract = row.get('handoff_contract', {})
        closure = contract.get('vignetting_closure', {}) if isinstance(contract, dict) else {}
        if not isinstance(closure, dict):
            return False
        if _lensde_commands.option_truthy(closure.get('source_retained'), False):
            return False
        if closure.get('reason') == 'disabled_or_no_vignetting':
            return True
        if _lensde_commands.option_truthy(closure.get('accepted'), False):
            return True
        completed = closure.get('completed_rounds')
        maximum = closure.get('max_rounds')
        try:
            return int(completed) >= int(maximum) > 0
        except (TypeError, ValueError):
            return False
    ranked = sorted(rows, key=post_asphere_handoff_ranking_key)
    return [row for row in ranked if handoff_is_usable(row) and all((math.isfinite(value) and value >= 0 for value in post_asphere_handoff_ranking_key(row)[:2]))][:max(1, int(limit))]


def run_command_logged(command: list[str], out_dir: Path, timeout_seconds: int, started_at: float, child_cpu_workers: int=0, env_overrides: dict[str, str] | None=None) -> dict:
    out_dir = native_io_paths.python_path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    _lensde_provenance.write_json(out_dir / 'command.json', command)
    try:
        (out_dir / 'command.txt').write_text(subprocess.list2cmdline(command), encoding='utf-8')
    except Exception:
        pass
    for flag, name in [('--system', 'system_input.json'), ('--merit', 'merit.json'), ('--variables', 'variables.json'), ('--initial-population', 'initial_population.csv')]:
        try:
            index = command.index(flag)
            source = Path(str(command[index + 1]))
            target = out_dir / name
            if source.exists() and source.resolve() != target.resolve():
                shutil.copyfile(source, target)
        except Exception:
            continue
    started = time.time()
    env = None
    if child_cpu_workers > 0 or env_overrides:
        env = os.environ.copy()
        if child_cpu_workers > 0:
            env['OPTILAND_CPU_WORKERS'] = str(child_cpu_workers)
        if env_overrides:
            env.update({str(key): str(value) for key, value in env_overrides.items()})
    with native_io_paths.native_command(command, state.CPP_ROOT, force_native=True) as native_command:
        code, stdout, stderr, timed_out = _lensde_execution.run_stage(native_command, timeout_seconds, started_at, env=env)
    elapsed = max(0.0, time.time() - started)
    (out_dir / 'stdout.log').write_text(stdout or '', encoding='utf-8', errors='replace')
    (out_dir / 'stderr.log').write_text(stderr or '', encoding='utf-8', errors='replace')
    return {'command': command, 'returncode': code, 'timed_out': timed_out, 'elapsed_seconds': elapsed, 'stdout_log': str(out_dir / 'stdout.log'), 'stderr_log': str(out_dir / 'stderr.log'), 'merit': score_merit_from_dir(out_dir)}


def post_asphere_preferred_field_nodes(options: dict) -> list[float]:
    raw = options.get('initial_search_post_asphere_preferred_field_nodes')
    if not isinstance(raw, (list, tuple)):
        return []
    nodes: list[float] = []
    for value in raw:
        node = _lensde_configuration.finite_float(value, math.nan)
        if not math.isfinite(node):
            continue
        node = max(0.0, min(1.0, node))
        if not any((abs(node - existing) <= 1e-06 for existing in nodes)):
            nodes.append(node)
    return nodes


def post_asphere_spot_probe_merit(merit: dict) -> dict:
    probe_merit = copy.deepcopy(merit if isinstance(merit, dict) else {})
    old_operands = probe_merit.get('operands', []) if isinstance(probe_merit.get('operands'), list) else []
    kept_operands = [copy.deepcopy(operand) for operand in old_operands if isinstance(operand, dict) and str(operand.get('name', '')).lower() not in {'rms_spot', 'rms_wavefront', 'single_ray_spot_tail', 'single_ray_spot_guard', 'single_ray_wavefront_guard'}]
    weight = max(0.0, _lensde_configuration.finite_float(probe_merit.get('weight_spot'), 3.0))
    probe_merit['image_quality_metric'] = 'rms_spot'
    probe_merit['unified_image_mean_spot_weight'] = weight
    probe_merit['unified_image_single_ray_spot_tail_weight'] = max(0.0, _lensde_configuration.finite_float(probe_merit.get('unified_image_single_ray_spot_tail_weight'), 0.012))
    probe_merit['single_ray_spot_tail_extra_edge_min_field_radius'] = min(2.0, max(0.0, _lensde_configuration.finite_float(probe_merit.get('single_ray_spot_tail_extra_edge_min_field_radius'), 0.85)))
    probe_merit['operands'] = kept_operands + _lensde_defaults.image_quality_operands('rms_spot', spot_weight=weight, spot_tail_weight=probe_merit['unified_image_single_ray_spot_tail_weight'], guard_weight=1.0)
    return probe_merit


def post_asphere_optimizer_system_from_measurement(source_system: dict, measured_system: dict) -> dict:
    """Transfer measured fields, preserving optimizer aperture semantics.

    Full displayed apertures are frozen for calibration only. Restore source
    flags for ordinary CP optimization, with protective glass excluded.
    """
    working = _lensde_apertures.vignetting_optimizer_aperture_copy(source_system)
    fields = list(working.get('fields') or [])
    measured_fields = list(measured_system.get('fields') or [])
    if len(fields) != len(measured_fields):
        raise RuntimeError('post-asphere measurement changed the field count')
    for target, measured in zip(fields, measured_fields, strict=True):
        for coordinate in ('hx', 'hy'):
            if abs(_lensde_configuration.finite_float(target.get(coordinate), 0.0) - _lensde_configuration.finite_float(measured.get(coordinate), 0.0)) > 1e-10:
                raise RuntimeError(f'post-asphere measurement changed field coordinate {coordinate}')
        for key in ('fvcx', 'fvcy', 'fvdx', 'fvdy'):
            target[key] = _lensde_configuration.finite_float(measured.get(key), 0.0)
    return working


def retain_post_asphere_handoff_source(source_dir: Path, target_dir: Path, reason: str, diagnostics: dict) -> dict:
    """Keep a usable optimizer endpoint when a calibration proposal fails."""
    system = score = None
    for system_name, score_name in (('system.json', 'score.json'), ('checkpoint_system.json', 'checkpoint_score.json')):
        if (source_dir / system_name).is_file() and (source_dir / score_name).is_file():
            system = _lensde_provenance.read_json(source_dir / system_name)
            score = _lensde_provenance.read_json(source_dir / score_name)
            break
    variables_doc = _lensde_provenance.read_json(source_dir / 'variables.json')
    variables = variables_doc.get('variables', []) if isinstance(variables_doc, dict) else variables_doc
    population_path = source_dir / 'population.csv'
    population = _lensde_populations.read_population_csv(population_path) if population_path.is_file() else []
    if not isinstance(system, dict) or not system or (not isinstance(variables, list)):
        raise RuntimeError(f'post-asphere source artifacts are incomplete: {source_dir}')
    if not isinstance(score, dict):
        raise RuntimeError(f'post-asphere source score is missing: {source_dir}')
    if not vignetting_handoff_policy.score_usable(score):
        raise RuntimeError(f'post-asphere handoff has neither a usable proposal nor source: {source_dir}')
    _lensde_commands.copy_candidate_artifacts(source_dir, target_dir)
    _lensde_provenance.write_json(target_dir / 'system.json', system)
    _lensde_provenance.write_json(target_dir / 'checkpoint_system.json', system)
    _lensde_provenance.write_json(target_dir / 'variables.json', {'variables': variables})
    _lensde_provenance.write_json(target_dir / 'score.json', score)
    _lensde_provenance.write_json(target_dir / 'checkpoint_score.json', score)
    report = {'contract': vignetting_handoff_policy.CONTRACT, 'purpose': 'optimizer_handoff', 'passed': True, 'calibration_diagnostics_passed': False, 'recalibration_applied': False, 'fallback_used': True, 'original_optimizer_state_preserved': True, 'reason': reason, 'source_dir': str(source_dir), 'selected_result_dir': str(target_dir), 'optimizer_merit': score.get('merit'), 'diagnostics': diagnostics}
    certificate = {'passed': False, 'purpose': 'optimizer_handoff', 'reason': reason}
    _lensde_provenance.write_json(target_dir / 'exact_apg_certificate.json', certificate)
    _lensde_provenance.write_json(target_dir / 'post_asphere_vignetting_certificate.json', certificate)
    _lensde_provenance.write_json(target_dir / 'terminal_vignetting_report.json', report)
    return {'system': system, 'variables': variables, 'population': population, 'score': score, 'report': report, 'certificate': certificate, 'selected_result_dir': str(target_dir)}


def finalize_post_asphere_zemax_vignetting(source_dir: Path, target_dir: Path, merit_path: Path, options: dict, cpu_exe: Path, timeout_seconds: int, started_at: float, child_cpu_workers: int=0, *, for_optimization: bool=False) -> dict:
    """Publish a post-asphere endpoint with a fresh exact APG pupil.

    Ranking and promotion continue to use the optimizer endpoint merit.  This
    operation runs only for the candidate that would otherwise be promoted.
    APG measures against all displayed optical apertures except protective
    glass throughout calibration. Intermediate handoffs preserve the source aperture semantics
    and audit dynamically; final delivery retains materialized owner apertures.
    Illumination compliance is reported separately from delivery acceptance.
    """
    source_dir = source_dir.resolve()
    target_dir = native_io_paths.python_path(target_dir.resolve())
    merit_path = merit_path.resolve()
    cpu_exe = cpu_exe.resolve()
    source_merit = _lensde_provenance.read_json(merit_path)
    if not post_asphere_stage_vignetting_recalibration_enabled(options, source_merit):
        retained = retain_post_asphere_handoff_source(source_dir, target_dir, 'automatic_vignetting_calibration_disabled', {'illumination_optimization_enabled': _lensde_configuration.vignetting_optimization_enabled(source_merit)})
        retained['report'].update(purpose='optimizer_handoff' if for_optimization else 'final_delivery', fallback_used=False, calibration_skipped=True)
        _lensde_provenance.write_json(target_dir / 'terminal_vignetting_report.json', retained['report'])
        return retained
    source_system = first_existing_json([source_dir / 'system.json', source_dir / 'checkpoint_system.json', source_dir / 'system_input.json', source_dir / 'input_system.json'], {})
    if not isinstance(source_system, dict) or not source_system:
        raise ValueError(f'post-asphere terminal system not found: {source_dir}')
    root_policy = post_asphere_root_scoring_policy(options)
    source_system['asphere_root_policy'] = root_policy
    optimizer_source_system = copy.deepcopy(source_system)
    purpose = 'optimizer_handoff' if for_optimization else 'final_delivery'
    if for_optimization:
        dynamic_aperture_contract.apply_merit(source_merit)
    upstream_delivery_certificate = _lensde_provenance.read_json_optional(source_dir / 'post_asphere_vignetting_certificate.json')
    source_score = first_existing_json([source_dir / 'score.json', source_dir / 'checkpoint_score.json'], {})
    certified_trim = _lensde_apertures.certified_global_trim_owner_apertures(source_system, upstream_delivery_certificate)
    if certified_trim is not None:
        source_system, adaptive_trim = certified_trim
    else:
        try:
            source_system, adaptive_trim = _lensde_apertures.materialize_global_trim_owner_apertures(source_system, source_score, source_merit)
        except RuntimeError as exc:
            if not for_optimization or not str(exc).startswith(('one circular aperture cannot realize incompatible', 'global-trim candidate would enlarge rather than trim', 'global-trim score does not certify every active vignetting side')):
                raise
            return retain_post_asphere_handoff_source(source_dir, target_dir, 'aperture_measurement_unavailable_keep_source', {'error': str(exc)})
    adaptive_trim_support_surface_indices = sorted({item['surface_index'] for item in adaptive_trim['selected_surfaces']})
    support_surface_indices = None
    target_dir.mkdir(parents=True, exist_ok=True)
    _lensde_provenance.write_json(target_dir / 'adaptive_trim_report.json', adaptive_trim)
    variables_payload = first_existing_json([source_dir / 'variables.json'], {'variables': []})
    source_variables = variables_payload.get('variables', variables_payload) if isinstance(variables_payload, dict) else variables_payload
    if not isinstance(source_variables, list):
        source_variables = []
    upstream_certificate = first_existing_json([source_dir / 'exact_apg_certificate.json', source_dir / 'post_asphere_vignetting_certificate.json'], {})
    try:
        measurement = _lensde_apertures.exact_apg_set_vignetting_system(source_system, native_io_paths.named_child(target_dir, 'field_measurement'), executable=cpu_exe, timeout_seconds=timeout_seconds, child_cpu_workers=child_cpu_workers, upstream_certificate=upstream_certificate, require_full_boundary_trace=not for_optimization, asphere_root_policy=root_policy, support_surface_indices=support_surface_indices)
    except RuntimeError as exc:
        measurement_dir = _lensde_apertures.exact_apg_set_vignetting_output_dir(native_io_paths.named_child(target_dir, 'field_measurement'))
        if not for_optimization or not str(exc).startswith('exact APG Set Vignetting failed:') or (not vignetting_handoff_policy.optical_measurement_failure(native_io_paths.named_child(measurement_dir, 'set_vignetting'))):
            raise
        return retain_post_asphere_handoff_source(source_dir, target_dir, 'measurement_unavailable_keep_source', {'error': str(exc), 'measurement_dir': str(measurement_dir)})
    if for_optimization:
        trim_dir = native_io_paths.named_child(target_dir, 'illumination_trim')
        _lensde_provenance.write_json(trim_dir / 'variables.json', {'variables': source_variables})
        _lensde_provenance.write_json(trim_dir / 'merit.json', source_merit)
        trim_command = post_asphere_stage_base_args(cpu_exe, trim_dir / 'input.json', trim_dir / 'merit.json', trim_dir / 'variables.json', options, dynamic_apertures=True)
        measurement, margin_trim = stage_illumination_trim.suggest(sys.modules[__name__], optimizer_source_system, source_score, source_merit, measurement, trim_dir, cpu_exe, trim_command, timeout_seconds, child_cpu_workers, support_surface_indices=support_surface_indices)
        adaptive_trim['illumination_margin_trim'] = margin_trim
        _lensde_provenance.write_json(target_dir / 'adaptive_trim_report.json', adaptive_trim)
    terminal_system = post_asphere_optimizer_system_from_measurement(optimizer_source_system, measurement['system']) if for_optimization else copy.deepcopy(measurement['system'])
    terminal_variables = _lensde_initialization.sync_post_asphere_variables_for_system(terminal_system, copy.deepcopy(source_variables), source_merit, rebase_asphere=True)
    terminal_variables = _lensde_commands.sync_variables_from_system(terminal_variables, terminal_system, rebase_asphere=True)
    system_path = target_dir / 'system.json'
    variables_path = target_dir / 'variables.json'
    terminal_merit_path = target_dir / 'merit.json'
    _lensde_provenance.write_json(system_path, terminal_system)
    _lensde_provenance.write_json(target_dir / 'checkpoint_system.json', terminal_system)
    _lensde_provenance.write_json(variables_path, {'variables': terminal_variables})
    _lensde_provenance.write_json(terminal_merit_path, source_merit)
    audit_dir = native_io_paths.named_child(target_dir, 'serialized_aiming_audit')
    audit_command = post_asphere_stage_base_args(cpu_exe, system_path, terminal_merit_path, variables_path, options, dynamic_apertures=for_optimization)
    _lensde_commands.append_vignetting_audit_args(audit_command, source_merit)
    audit_command.extend(['--out-dir', str(audit_dir)])
    normalized_audit_command: list[str] = []
    path_flags = {'--system', '--merit', '--variables', '--out-dir'}
    command_index = 0
    while command_index < len(audit_command):
        token = audit_command[command_index]
        if token in path_flags and command_index + 1 < len(audit_command):
            normalized_audit_command.extend([token, f'<{token[2:]}>'])
            command_index += 2
            continue
        normalized_audit_command.append('<executable>' if command_index == 0 else token)
        command_index += 1
    audit_contract = {'contract': 'post_asphere_serialized_aiming_audit_v4', 'relative_illumination_acceptance_policy': 'audit_only', 'purpose': purpose, 'system_sha256': _lensde_provenance.json_fingerprint(terminal_system), 'merit_sha256': _lensde_provenance.json_fingerprint(source_merit), 'variables_sha256': _lensde_provenance.json_fingerprint(terminal_variables), 'executable_sha256': _lensde_provenance.sha256_file(cpu_exe), 'command': normalized_audit_command}
    audit_contract_hash = _lensde_provenance.json_fingerprint(audit_contract)
    upstream_audit = upstream_delivery_certificate.get('serialized_aiming_audit') if isinstance(upstream_delivery_certificate, dict) else None
    reusable_audit = bool(not for_optimization and isinstance(upstream_audit, dict) and _lensde_commands.option_truthy(upstream_audit.get('passed'), False) and (upstream_audit.get('contract_sha256') == audit_contract_hash) and isinstance(upstream_audit.get('score'), dict) and all(((source_dir / name).is_file() for name in ('layout.svg', 'spot.csv', 'raytrace.csv'))))
    if reusable_audit:
        audit_dir.mkdir(parents=True, exist_ok=True)
        audit_score = copy.deepcopy(upstream_audit['score'])
        _lensde_provenance.write_json(audit_dir / 'system.json', terminal_system)
        _lensde_provenance.write_json(audit_dir / 'score.json', audit_score)
        for name in ('layout.svg', 'spot.csv', 'raytrace.csv', 'relative_illumination.csv', 'vignetting_audit.json'):
            source = source_dir / name
            if source.is_file():
                shutil.copyfile(source, audit_dir / name)
        audit_result = {'returncode': 0, 'skipped': True, 'cache_hit': True, 'cache_scope': 'upstream_serialized_aiming_audit_certificate', 'elapsed_seconds': 0.0}
    else:
        audit_result = run_command_logged(audit_command, audit_dir, timeout_seconds, started_at, child_cpu_workers)
        if int(_lensde_configuration.finite_float(audit_result.get('returncode'), -1)) != 0:
            raise RuntimeError(f'post-asphere serialized-aiming audit failed: {target_dir}')
        audit_score = _lensde_provenance.read_json(audit_dir / 'score.json')
    if for_optimization:
        terminal_system = _lensde_provenance.read_json(audit_dir / 'system.json')
        if not isinstance(terminal_system, dict) or not terminal_system.get('surfaces'):
            raise RuntimeError('post-asphere dynamic audit produced no system')
        terminal_variables = _lensde_initialization.sync_post_asphere_variables_for_system(terminal_system, terminal_variables, source_merit, rebase_asphere=True)
        terminal_system, terminal_variables, audit_score, closure_report = post_asphere_handoff.close(sys.modules[__name__], terminal_system, terminal_variables, source_merit, options, native_io_paths.named_child(target_dir, 'vignetting_closure'), cpu_exe, timeout_seconds, started_at, child_cpu_workers, baseline=audit_score, support_surface_indices=support_surface_indices)
        if closure_report.get('accepted'):
            audit_dir = Path(closure_report['accepted_score_dir'])
            measurement = closure_report['last_measurement']
            audit_contract['closure_contract'] = post_asphere_handoff.CONTRACT
            audit_contract['system_sha256'] = _lensde_provenance.json_fingerprint(terminal_system)
            audit_contract['variables_sha256'] = _lensde_provenance.json_fingerprint(terminal_variables)
            audit_contract_hash = _lensde_provenance.json_fingerprint(audit_contract)
            audit_result = {'returncode': 0, 'merit': audit_score['merit'], 'superseded_by_vignetting_closure': True, 'command': _lensde_provenance.read_json(audit_dir / 'command.json'), 'stdout_log': str(audit_dir / 'stdout.log'), 'stderr_log': str(audit_dir / 'stderr.log')}
        _lensde_provenance.write_json(target_dir / 'vignetting_closure_report.json', closure_report)
        _lensde_provenance.write_json(system_path, terminal_system)
        _lensde_provenance.write_json(target_dir / 'checkpoint_system.json', terminal_system)
        _lensde_provenance.write_json(variables_path, {'variables': terminal_variables})
    vignetting_audit = _lensde_commands.build_vignetting_audit(audit_dir, source_merit)
    if for_optimization and (not vignetting_handoff_policy.score_usable(audit_score)):
        return retain_post_asphere_handoff_source(source_dir, target_dir, 'recalibrated_optimizer_score_unusable_keep_source', {'measurement': measurement['report'], 'proposed_score': audit_score, 'serialized_aiming_audit_dir': str(audit_dir), 'relative_illumination_audit': vignetting_audit})
    published_artifacts: list[str] = []
    for name in ('layout.svg', 'spot.csv', 'raytrace.csv', 'relative_illumination.csv', 'vignetting_audit.json'):
        source = audit_dir / name
        if source.is_file():
            shutil.copyfile(source, target_dir / name)
            published_artifacts.append(name)
    _lensde_provenance.write_json(target_dir / 'score.json', audit_score)
    _lensde_provenance.write_json(target_dir / 'set_vignetting_score.json', measurement['set_vignetting_score'])
    _lensde_provenance.export_zemax_zmx(copy.deepcopy({'system': terminal_system, 'merit': source_merit, 'variables': terminal_variables, 'options': options}), target_dir / 'checkpoint_system.zmx')
    serialized_audit_passed = bool(audit_score.get('trace_ok'))
    illumination_audit_passed = vignetting_audit is None or _lensde_commands.option_truthy(vignetting_audit.get('passed'), False)
    report = {'contract': 'post_asphere_terminal_zemax_vignetting_v3', 'purpose': purpose, 'relative_illumination_acceptance_policy': 'audit_only', 'passed': vignetting_handoff_policy.score_usable(audit_score) if for_optimization else bool(measurement['report'].get('passed')) and serialized_audit_passed, 'calibration_diagnostics_passed': bool(measurement['report'].get('passed')) and serialized_audit_passed and illumination_audit_passed, 'recalibration_applied': True, 'fallback_used': False, 'apg_diagnostics_are_advisory': for_optimization, 'source_dir': str(source_dir), 'selected_result_dir': str(target_dir), 'selection_basis': 'pre_vignetting_calibration_optimizer_merit', 'optimizer_merit': source_score.get('merit'), 'serialized_aiming_merit_after_calibration': audit_score.get('merit'), 'set_vignetting_measurement': measurement['report'], 'adaptive_trim': adaptive_trim, 'vignetting_closure': closure_report if for_optimization else {'performed': False, 'reason': 'fixed_final_delivery'}, 'serialized_aiming_audit': {'diagnostic_only': True, 'aiming': post_asphere_aiming_contract(options), 'trace_ok': audit_score.get('trace_ok'), 'trace_coverage': audit_score.get('trace_coverage'), 'min_field_coverage': audit_score.get('min_field_coverage'), 'min_chief_progress': audit_score.get('min_chief_progress'), 'relative_illumination_audit': vignetting_audit, 'published_artifacts': published_artifacts, 'contract_sha256': audit_contract_hash, 'cache_hit': reusable_audit, 'run_result': audit_result}, 'delivered_system_update': 'field_vignetting_and_dynamic_aperture_audit' if for_optimization else 'materialized_owner_apertures_and_field_vignetting', 'delivered_aperture_policy': 'source_fixed_apertures_preserved_automatic_apertures_dynamic_4x8' if for_optimization else 'materialized_owner_apertures', 'post_measurement_projection_or_contraction': False, 'optimizer_hot_path_contract': 'global_trim_realizability', 'all_displayed_apertures_frozen_for_exact_apg': True, 'measurement_apertures': 'all_displayed_apertures_except_protective_glass', 'adaptive_trim_support_surface_indices': adaptive_trim_support_surface_indices, 'measurement_surface_indices': _lensde_apertures.default_vignetting_aperture_surface_indices(terminal_system), 'excluded_surface_indices': _lensde_apertures.protective_glass_surface_indices(terminal_system), 'support_set_policy': 'full_aperture_default_except_protective_glass', 'post_apg_clear_aperture_update': for_optimization, 'zmx_path': str(target_dir / 'checkpoint_system.zmx')}
    _lensde_provenance.write_json(target_dir / 'terminal_vignetting_report.json', report)
    exact_certificate = copy.deepcopy(measurement['certificate'])
    _lensde_provenance.write_json(target_dir / 'exact_apg_certificate.json', exact_certificate)
    delivery_certificate = {'certificate': 'post_asphere_vignetting_delivery_certificate_v3', 'purpose': purpose, 'relative_illumination_acceptance_policy': 'audit_only', 'passed': bool(report['passed']), 'certified_system_sha256': _lensde_provenance.json_fingerprint(terminal_system), 'exact_apg': exact_certificate, 'adaptive_trim': copy.deepcopy(adaptive_trim), 'serialized_aiming_audit': {'passed': serialized_audit_passed, 'relative_illumination_acceptance_policy': 'audit_only', 'system_sha256': _lensde_provenance.json_fingerprint(terminal_system), 'merit_sha256': _lensde_provenance.json_fingerprint(source_merit), 'contract': audit_contract, 'contract_sha256': audit_contract_hash, 'score': copy.deepcopy(audit_score), 'relative_illumination_audit': copy.deepcopy(vignetting_audit), 'published_artifacts': published_artifacts}}
    _lensde_provenance.write_json(target_dir / 'post_asphere_vignetting_certificate.json', delivery_certificate)
    return {'system': terminal_system, 'variables': terminal_variables, 'score': audit_score, 'set_vignetting_score': measurement['set_vignetting_score'], 'report': report, 'certificate': delivery_certificate, 'selected_result_dir': str(target_dir)}


def post_asphere_population_with_recalibrated_anchor(raw_system: dict, calibrated_system: dict, raw_variables: list[dict], calibrated_variables: list[dict], population: list[list[float]]) -> tuple[list[list[float]], dict]:
    """Replace the raw endpoint row by its calibrated physical-pupil row."""
    rows = [list(row) for row in population]
    if not rows or not raw_variables:
        return (rows, {'performed': False, 'reason': 'empty_population_or_variables'})
    if len(raw_variables) != len(calibrated_variables):
        raise RuntimeError(f'stage-boundary vignetting changed the variable dimension: {len(raw_variables)} -> {len(calibrated_variables)}')
    raw_anchor = [_lensde_populations.variable_current_value_for_system(raw_system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in raw_variables]
    calibrated_anchor = [_lensde_populations.variable_current_value_for_system(calibrated_system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in calibrated_variables]
    candidates = [(index, row) for index, row in enumerate(rows) if len(row) == len(raw_variables)]
    if not candidates:
        raise RuntimeError('post-asphere population has no row matching the variable dimension')
    nearest_index, nearest_row = min(candidates, key=lambda item: sum((((_lensde_configuration.finite_float(value, 0.0) - raw_value) / max(abs(_lensde_configuration.finite_float(variable.get('upper'), 0.0) - _lensde_configuration.finite_float(variable.get('lower'), 0.0)), 1e-12)) ** 2 for value, raw_value, variable in zip(item[1], raw_anchor, raw_variables, strict=True))))
    distance = math.sqrt(sum((((_lensde_configuration.finite_float(value, 0.0) - raw_value) / max(abs(_lensde_configuration.finite_float(variable.get('upper'), 0.0) - _lensde_configuration.finite_float(variable.get('lower'), 0.0)), 1e-12)) ** 2 for value, raw_value, variable in zip(nearest_row, raw_anchor, raw_variables, strict=True))))
    rows[nearest_index] = calibrated_anchor
    changed_variables = [{'position': index, 'kind': str(variable.get('kind') or ''), 'index': variable.get('index'), 'field': variable.get('field'), 'before': raw_anchor[index], 'after': calibrated_anchor[index]} for index, variable in enumerate(calibrated_variables) if abs(calibrated_anchor[index] - raw_anchor[index]) > 1e-14]
    return (rows, {'performed': True, 'replaced_population_row': nearest_index, 'raw_endpoint_normalized_distance': distance, 'changed_variable_count': len(changed_variables), 'changed_variables': changed_variables, 'other_population_rows_preserved': True, 'population_ranking_scores_preserved_from_precalibration': True})


def recalibrate_post_asphere_stage_handoff(source_dir: Path, handoff_dir: Path, merit_path: Path, options: dict, cpu_exe: Path, timeout_seconds: int, started_at: float, child_cpu_workers: int=0) -> dict:
    """Recalibrate one accepted stage endpoint before the next stage starts."""
    source_dir = source_dir.resolve()
    handoff_dir = native_io_paths.python_path(handoff_dir.resolve())
    merit_path = merit_path.resolve()
    cpu_exe = cpu_exe.resolve()
    raw_system, raw_variables, raw_population = post_asphere_multistart_endpoint(source_dir)
    _lensde_commands.copy_candidate_artifacts(source_dir, handoff_dir)
    finalized = finalize_post_asphere_zemax_vignetting(source_dir, handoff_dir, merit_path, options, cpu_exe, timeout_seconds, started_at, child_cpu_workers, for_optimization=True)
    if not finalized['report'].get('recalibration_applied', True):
        population = [list(row) for row in raw_population]
        population_report = {'performed': False, 'reason': 'calibration_proposal_declined_source_population_preserved', 'other_population_rows_preserved': True}
    else:
        population, population_report = post_asphere_population_with_recalibrated_anchor(raw_system, finalized['system'], raw_variables, finalized['variables'], raw_population)
    if population:
        _lensde_populations.write_population_csv(handoff_dir / 'population.csv', population)
    report = finalized['report']
    report.update({'contract': 'post_asphere_stage_boundary_vignetting_v3', 'stage_boundary': True, 'selection_basis': 'pre_vignetting_calibration_optimizer_merit', 'next_stage_uses_recalibrated_system': report.get('recalibration_applied', True), 'population_rebase': population_report})
    _lensde_provenance.write_json(handoff_dir / 'stage_boundary_vignetting_report.json', report)
    finalized['population'] = population
    finalized['population_rebase'] = population_report
    finalized['report'] = report
    return finalized


def canonical_post_asphere_flow(value: object) -> str:
    text = str(value or 'curvature_normalized_formation_periodic').strip().lower()
    if text in {'curvature_normalized_formation_periodic', 'graded_formation_periodic', 'default_candidate'}:
        return 'curvature_normalized_formation_periodic'
    if text in {'single_stage', 'single_stage_refresh'}:
        return 'single_stage'
    if text in {'legacy', 'legacy_de_dls', 'de_dls'}:
        return 'legacy_de_dls'
    if text in {'conic_multistart', 'multistart', 'scout_top_long'}:
        return 'conic_multistart'
    if text in {'single_mother', 'single_parent', 'mother_restart'}:
        return 'single_mother'
    return 'curvature_normalized_formation_periodic'


def post_asphere_multistart_schedule(options: dict) -> dict:
    epoch_generations = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_multistart_epoch_generations', 400, 1)

    def stage(key: str, default: int, *, minimum: int=0) -> dict:
        requested = _lensde_commands.option_int_from(options, key, default, minimum)
        if requested <= 0:
            return {'requested_generations': requested, 'epochs': 0, 'generations': 0}
        epochs = max(1, int(math.ceil(requested / epoch_generations)))
        return {'requested_generations': requested, 'epochs': epochs, 'generations': epochs * epoch_generations}
    scout_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_multistart_scout_count', 4, 1)
    top_count = min(scout_count, _lensde_commands.option_int_from(options, 'initial_search_post_asphere_multistart_top_count', 2, 1))
    return {'scout_count': scout_count, 'top_count': top_count, 'epoch_generations': epoch_generations, 'scout': stage('initial_search_post_asphere_multistart_scout_generations', 400, minimum=1), 'topup': stage('initial_search_post_asphere_multistart_topup_generations', 800), 'multi_island_enabled': _lensde_commands.option_truthy(options.get('initial_search_post_asphere_multistart_multi_island_enabled'), True), 'long': {'rounds': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_rounds', 3, 0), 'outer_epochs': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_outer', 20, 1), 'de_generations': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_de_gen', 400, 1), 'hybrid_parents': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_hybrid_parents', 10, 1), 'refresh': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_long_refresh', 0.25, 0.0)}}


def post_asphere_periodic_refresh_config(options: dict, population_size: int) -> dict:
    """Return the fixed top-half, single-surface periodic refresh contract."""
    configured_fraction = min(1.0, _lensde_commands.option_float_from(options, 'initial_search_post_asphere_periodic_refresh_fraction', 0.5, 0.0))
    fraction = 0.5
    rows = max(0, int(population_size))
    protected_count = (rows + 1) // 2
    return {'max_rounds': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_periodic_refresh_max_rounds', 20, 1), 'generations_per_round': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_periodic_refresh_generations', 400, 1), 'replacement_fraction': fraction, 'configured_replacement_fraction': configured_fraction, 'protected_count': protected_count, 'replacement_count': rows - protected_count, 'parent_count': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_periodic_refresh_parent_count', 16, 1), 'sag_um': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_periodic_refresh_sag_um', 0.3, 0.0), 'correlation_length': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_periodic_refresh_correlation_length', 0.35, 0.001), 'geometry_fraction': 0.0, 'generation_mode': 'top_half_single_surface_correlated_sag', 'parent_selection': 'uniform_random_without_replacement_from_protected_top_half', 'surface_selection': 'one_uniform_random_complete_active_order_surface_per_child', 'pre_stage_refresh': True, 'trace_feasible_gate': False}


def post_asphere_fixed_contract_stage_options(options: dict, long_config: dict) -> dict:
    """Build stage options that preserve the Formation/refresh P-D contract."""
    stage_options = copy.deepcopy(options)
    stage_options.update({'initial_search_post_asphere_lpsr_enabled': False, 'initial_search_post_asphere_hybrid_parents': long_config['hybrid_parents'], 'initial_search_post_asphere_refresh': long_config['refresh'], 'initial_search_post_asphere_inject_best': False, 'initial_search_post_asphere_early_stop': _lensde_commands.option_truthy(options.get('initial_search_post_asphere_early_stop'), True), 'initial_search_post_asphere_early_stop_use_min_improvement_threshold': False})
    return stage_options


def canonical_post_asphere_stage_illumination_policy(value: object) -> str:
    policy = str(value or 'auto_margin').strip().lower().replace('-', '_')
    aliases = {'auto': 'auto_margin', 'adaptive': 'auto_margin', 'stage_boundary_auto': 'auto_margin', 'off': 'legacy', 'disabled': 'legacy', 'key_fields_then_all_fields': 'legacy'}
    policy = aliases.get(policy, policy)
    return policy if policy in {'auto_margin', 'legacy'} else 'auto_margin'


def post_asphere_added_field_illumination_audit(score: dict, added_field_indices: list[int], target: float, safety_margin: float) -> dict:
    """Audit the fixed optimizer fields that are absent from the key contract."""
    indices = sorted({int(index) for index in added_field_indices})
    values = score.get('relative_illumination_proxy_by_field') if isinstance(score, dict) else None
    missing_indices: list[int] = []
    measured: dict[str, float] = {}
    if isinstance(values, list):
        for index in indices:
            value = _lensde_configuration.finite_float(values[index], math.nan) if 0 <= index < len(values) else math.nan
            if math.isfinite(value):
                measured[str(index)] = value
            else:
                missing_indices.append(index)
    else:
        missing_indices = list(indices)
    available = not missing_indices
    minimum = min(measured.values()) if measured else None
    required_with_margin = min(1.0, max(0.0, target + safety_margin))
    sufficient_margin = bool(available and minimum is not None and (minimum + 1e-12 >= required_with_margin))
    return {'available': available, 'added_field_indices': indices, 'values_by_field': measured, 'missing_indices': missing_indices, 'minimum': minimum, 'target': target, 'safety_margin': safety_margin, 'required_with_margin': required_with_margin, 'sufficient_margin': sufficient_margin}


def post_asphere_quantized_illumination_floor(measured_minimum: float, target: float, closure: float, quantum: float) -> float:
    """Close a fixed fraction of the RI deficit and round to a stable floor."""
    target = min(1.0, max(0.0, _lensde_configuration.finite_float(target, 0.0)))
    minimum = min(target, max(0.0, _lensde_configuration.finite_float(measured_minimum, 0.0)))
    closure = min(1.0, max(0.0, _lensde_configuration.finite_float(closure, 0.0)))
    quantum = max(1e-09, _lensde_configuration.finite_float(quantum, 0.005))
    raw_floor = minimum + closure * (target - minimum)
    quantized = math.floor(raw_floor / quantum + 0.5) * quantum
    return round(min(target, max(0.0, quantized)), 12)


def post_asphere_auto_stage_illumination_plan(score: dict, added_field_indices: list[int], target: float, safety_margin: float, scout_closure: float, formation_closure: float, quantum: float) -> dict:
    """Choose fixed Scout/Form/Periodic floors from the pre-Scout audit."""
    audit = post_asphere_added_field_illumination_audit(score, added_field_indices, target, safety_margin)
    if not audit['available'] or audit['minimum'] is None:
        raise RuntimeError('automatic stage illumination policy requires a complete pre-Scout relative-illumination score for every added field')
    if audit['sufficient_margin']:
        profile = 'margin_available'
        scout_floor = 0.0
        formation_floor = target
    else:
        profile = 'deficit_recovery'
        scout_floor = post_asphere_quantized_illumination_floor(audit['minimum'], target, scout_closure, quantum)
        formation_floor = post_asphere_quantized_illumination_floor(audit['minimum'], target, formation_closure, quantum)
        formation_floor = max(scout_floor, formation_floor)
    return {'policy': 'auto_margin', 'profile': profile, 'audit': audit, 'floors': {'scout': scout_floor, 'formation': formation_floor, 'periodic': target}, 'constant_within_each_stage': True}


def post_asphere_auto_formation_illumination_recheck(initial_plan: dict, score: dict, added_field_indices: list[int], target: float, safety_margin: float, formation_closure: float, quantum: float) -> dict:
    """Freeze Formation's merit after auditing the selected Scout endpoint."""
    audit = post_asphere_added_field_illumination_audit(score, added_field_indices, target, safety_margin)
    if not audit['available'] or audit['minimum'] is None:
        raise RuntimeError('automatic stage illumination policy requires a complete selected-Scout relative-illumination score before Formation')
    initial_floor = _lensde_configuration.finite_float((initial_plan.get('floors') or {}).get('formation'), 0.0)
    if initial_plan.get('profile') == 'deficit_recovery':
        floor = initial_floor
        reason = 'pre_scout_deficit_plan_preserved'
    elif audit['sufficient_margin']:
        floor = target
        reason = 'selected_scout_retains_safety_margin_target_floor'
    else:
        minimum = _lensde_configuration.finite_float(audit['minimum'], 0.0)
        floor = target if minimum >= target else post_asphere_quantized_illumination_floor(minimum, target, formation_closure, quantum)
        reason = 'selected_scout_lost_safety_margin'
    return {'performed': True, 'audit': audit, 'formation_floor': floor, 'changed_from_pre_scout_plan': abs(floor - initial_floor) > 1e-12, 'reason': reason, 'fixed_before_formation_start': True}


def post_asphere_stage_illumination_merit(merit: dict, added_field_indices: list[int], floor: float) -> dict:
    """Return one immutable stage merit with explicit added-field RI operands."""
    added = set(added_field_indices)
    stage_merit = copy.deepcopy(merit)
    stage_operands: list[dict] = []
    for operand in list(stage_merit.get('operands') or []):
        try:
            operand_index = int(operand.get('index', -1))
        except (TypeError, ValueError):
            operand_index = -1
        is_added_illumination = bool(str(operand.get('name') or '').strip().lower() in {'relative_illumination_proxy', 'relative_illumination', 'ri_proxy'} and operand_index in added)
        if not is_added_illumination:
            stage_operands.append(operand)
    if floor > 0.0:
        stage_operands.extend(({'name': 'relative_illumination_proxy', 'mode': 'greater_than', 'index': index, 'lower': floor, 'weight': 25.0} for index in sorted(added)))
    stage_merit['operands'] = stage_operands
    return stage_merit


def post_asphere_long_uses_relaxed_early_stop(options: dict, round_index: int, total_rounds: int) -> bool:
    return bool(total_rounds > 0 and round_index == total_rounds and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_final_long_early_stop_use_min_threshold'), True))


def post_asphere_single_mother_progress_decision(reference_merit: float, incumbent_merit: float, confirmation_round: bool, no_progress_generations: int, options: dict) -> dict:
    relative_threshold = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_single_mother_significant_rel_improve', 0.01, 0.0)
    absolute_threshold = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_single_mother_significant_abs_improve', 0.01, 0.0)
    hard_limit = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_hard_no_progress_generations', 3200, 1)
    threshold = max(absolute_threshold, relative_threshold * max(0.0, reference_merit))
    gain = reference_merit - incumbent_merit
    significant = bool(math.isfinite(gain) and gain + 1e-12 >= threshold)
    if significant:
        action = 'reset_and_continue'
    elif no_progress_generations >= hard_limit:
        action = 'stop_hard_no_progress_limit'
    elif confirmation_round:
        action = 'stop_after_confirmation'
    else:
        action = 'run_one_confirmation'
    return {'reference_merit': reference_merit, 'incumbent_merit': incumbent_merit, 'gain_from_reference': gain, 'significant_threshold': threshold, 'significant': significant, 'confirmation_round': bool(confirmation_round), 'no_progress_generations': int(no_progress_generations), 'hard_no_progress_generations': hard_limit, 'action': action}


def post_asphere_fixed_validation_field_nodes(options: dict) -> list[float]:
    count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_validation_field_count', 41, 3)
    return [index / float(count - 1) for index in range(count)]


def post_asphere_fixed_optimizer_field_nodes(fixed_main_nodes: list[float], options: dict) -> tuple[list[float], list[float]]:
    """Append the immutable edge-prior nodes to the image-height main nodes."""
    raw = options.get('initial_search_post_asphere_fixed_edge_nodes', [0.925, 0.975, 1.0])
    values = raw if isinstance(raw, (list, tuple)) else str(raw or '').split(',')
    edge_nodes: list[float] = []
    for value in values:
        node = _lensde_configuration.finite_float(value, math.nan)
        if not math.isfinite(node):
            continue
        node = max(0.0, min(1.0, node))
        if not any((abs(node - existing) <= 1e-06 for existing in edge_nodes)):
            edge_nodes.append(node)
    edge_nodes.sort()
    if len(edge_nodes) < 2:
        raise RuntimeError(f'fixed optimizer contract expected at least two edge nodes; received {edge_nodes}')
    fixed_nodes = sorted({*(max(0.0, min(1.0, _lensde_configuration.finite_float(node, 0.0))) for node in fixed_main_nodes), *edge_nodes})
    if len(fixed_nodes) != len(fixed_main_nodes) + len(edge_nodes):
        raise RuntimeError('fixed edge nodes overlap the image-height main fields')
    return (fixed_nodes, edge_nodes)


def post_asphere_fixed_validation_options(options: dict, validation_nodes: list[float]) -> dict:
    out = copy.deepcopy(options)
    out.update({'initial_search_post_asphere_field_count': len(validation_nodes), 'initial_search_post_asphere_preferred_field_nodes': list(validation_nodes), 'initial_search_post_asphere_field_coordinate': 'normalized_field', 'initial_search_post_asphere_field_contract_policy': 'locked', 'initial_search_post_asphere_adaptive_fields_enabled': False, 'initial_search_post_asphere_adaptive_field_max_additions': 0})
    return out


def post_asphere_parent_validation_is_usable(validation_score: dict, *, audit_only: bool) -> bool:
    """Return whether the dense parent audit permits fixed-contract startup.

    Audit-only mode records the 41-field result but deliberately does not use
    it as a feasibility gate.  A finite merit is still required so an actual
    audit execution failure cannot be mistaken for a diagnostic result.
    """
    validation_merit = _lensde_configuration.finite_float(validation_score.get('merit'), math.inf)
    if not math.isfinite(validation_merit):
        return False
    return bool(audit_only or _lensde_commands.option_truthy(validation_score.get('strict_valid'), False))


def post_asphere_validation_auxiliary_rows(validation_nodes: list[float], validation_score: dict, options: dict) -> list[dict]:
    field_rms = validation_score.get('field_rms_um', [])
    if not isinstance(field_rms, list) or len(field_rms) != len(validation_nodes):
        raise RuntimeError('fixed validation score does not contain the complete field curve')
    field_wavefront = validation_score.get('field_wavefront_rms', [])
    has_wavefront = bool(isinstance(field_wavefront, list) and len(field_wavefront) == len(validation_nodes))
    spot_scale_um = 1000.0 * _lensde_commands.option_float_from(options, 'initial_search_post_asphere_quality_spot_scale', 0.01, 1e-12)
    wavefront_scale = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_quality_wavefront_scale', 0.5, 1e-12)
    spot_weight = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_spot_weight', 3.0, 0.0)
    wavefront_weight = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_wavefront_weight', 1.0, 0.0)
    rows: list[dict] = []
    for index, (node, rms_value) in enumerate(zip(validation_nodes, field_rms, strict=True)):
        rms_um = _lensde_configuration.finite_float(rms_value, math.nan)
        wavefront_rms = _lensde_configuration.finite_float(field_wavefront[index], math.nan) if has_wavefront else math.nan
        risk = 0.0
        if math.isfinite(rms_um):
            risk += spot_weight * (max(0.0, rms_um) / spot_scale_um) ** 2
        if math.isfinite(wavefront_rms):
            risk += wavefront_weight * (max(0.0, wavefront_rms) / wavefront_scale) ** 2
        rows.append({'field_index': index, 'node': _lensde_configuration.finite_float(node, math.nan), 'rms_spot_um': rms_um, 'wavefront_rms': wavefront_rms, 'absolute_risk': risk})
    return rows


def post_asphere_rejection_degradation_guard(validation_nodes: list[float], fixed_main_nodes: list[float], parent_validation_score: dict, child_validation_score: dict, options: dict) -> dict:
    """Return the non-main validation node most degraded by a rejected child."""
    if not _lensde_commands.option_truthy(options.get('initial_search_post_asphere_rejection_guard_enabled'), True):
        return {'active': False, 'reason': 'disabled'}
    parent_rows = post_asphere_validation_auxiliary_rows(validation_nodes, parent_validation_score, options)
    child_rows = post_asphere_validation_auxiliary_rows(validation_nodes, child_validation_score, options)
    spacing = min((right - left for left, right in zip(validation_nodes, validation_nodes[1:]) if right > left), default=0.0)
    main_tolerance = max(1e-06, 0.5 * spacing)
    spot_scale_um = 1000.0 * _lensde_commands.option_float_from(options, 'initial_search_post_asphere_quality_spot_scale', 0.01, 1e-12)
    wavefront_scale = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_quality_wavefront_scale', 0.5, 1e-12)
    spot_weight = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_spot_weight', 3.0, 0.0)
    wavefront_weight = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_wavefront_weight', 1.0, 0.0)
    candidates: list[dict] = []
    for parent, child in zip(parent_rows, child_rows, strict=True):
        node = _lensde_configuration.finite_float(child.get('node'), math.nan)
        if not math.isfinite(node) or any((abs(node - main) <= main_tolerance for main in fixed_main_nodes)):
            continue
        spot_delta = max(0.0, _lensde_configuration.finite_float(child.get('rms_spot_um'), 0.0) - _lensde_configuration.finite_float(parent.get('rms_spot_um'), 0.0))
        parent_wavefront = _lensde_configuration.finite_float(parent.get('wavefront_rms'), math.nan)
        child_wavefront = _lensde_configuration.finite_float(child.get('wavefront_rms'), math.nan)
        wavefront_delta = max(0.0, child_wavefront - parent_wavefront) if math.isfinite(parent_wavefront) and math.isfinite(child_wavefront) else 0.0
        degradation_risk = spot_weight * (spot_delta / spot_scale_um) ** 2 + wavefront_weight * (wavefront_delta / wavefront_scale) ** 2
        if degradation_risk > 0.0:
            candidates.append({'node': node, 'spot_delta_um': spot_delta, 'wavefront_delta': wavefront_delta, 'degradation_risk': degradation_risk})
    if not candidates:
        return {'active': False, 'reason': 'no_positive_non_main_degradation'}
    selected = max(candidates, key=lambda row: row['degradation_risk'])
    return {'active': True, 'reason': 'rejected_child_max_fixed_validation_degradation', **selected}


def post_asphere_dynamic_contract_from_validation(system: dict, fixed_main_nodes: list[float], validation_nodes: list[float], validation_score: dict, options: dict, contract_version: int, previous_contract: dict | None=None, forced_anomaly_node: float | None=None) -> tuple[dict, dict]:
    """Build a 6+2 objective with an edge guard and a global anomaly probe.

    The returned contract changes only the DE objective.  Acceptance and
    progress decisions remain on the immutable validation grid.
    """
    main_nodes = sorted({max(0.0, min(1.0, _lensde_configuration.finite_float(node, math.nan))) for node in fixed_main_nodes if math.isfinite(_lensde_configuration.finite_float(node, math.nan))})
    if not main_nodes:
        raise RuntimeError('dynamic field contract has no fixed main fields')
    validation_rows = post_asphere_validation_auxiliary_rows(validation_nodes, validation_score, options)
    probe_rows = [{'field_index': row['field_index'], 'node': row['node'], 'rms_spot_um': row['rms_spot_um']} for row in validation_rows]
    auxiliary_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_adaptive_field_max_additions', 2, 0)
    target_count = len(main_nodes) + auxiliary_count
    selection = _lensde_fields.select_adaptive_post_asphere_field_nodes(main_nodes, probe_rows, target_count, _lensde_commands.option_float_from(options, 'initial_search_post_asphere_field_probe_min_excess_um', 0.05, 0.0), max_field_deg=_lensde_configuration.finite_float(system.get('max_field_deg'), 0.0), sample_coverage_deg=_lensde_commands.option_float_from(options, 'initial_search_post_asphere_field_sample_coverage_deg', 2.5, 0.0), fill_to_target=True)
    raw_selected_nodes = list(selection.get('selected_nodes', []))
    if len(raw_selected_nodes) != target_count:
        raise RuntimeError(f'dynamic field contract expected {target_count} fields; selected {len(raw_selected_nodes)}')
    raw_auxiliary_nodes = [node for node in raw_selected_nodes if not any((abs(node - main) <= 1e-06 for main in main_nodes))]
    auxiliary_nodes = list(raw_auxiliary_nodes)
    auxiliary_roles: dict = {'policy': 'legacy_two_dynamic_anomalies'}
    if auxiliary_count == 2 and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_auxiliary_region_slots_enabled'), True):
        spacing = min((right - left for left, right in zip(validation_nodes, validation_nodes[1:]) if right > left), default=0.0)
        main_tolerance = max(1e-06, 0.5 * spacing)
        dynamic_max_node = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_dynamic_anomaly_max_node', 0.85, 0.0)
        regions = [region for region in selection.get('regions', []) if isinstance(region, dict) and _lensde_configuration.finite_float(region.get('representative_node'), math.inf) < dynamic_max_node]
        ranked_regions = sorted(regions, key=lambda region: (_lensde_configuration.finite_float(region.get('severity_um_deg'), 0.0), _lensde_configuration.finite_float(region.get('peak_excess_um'), 0.0), _lensde_configuration.finite_float(region.get('representative_node'), 0.0)), reverse=True)
        previous_dynamic_nodes: list[float] = []
        if isinstance(previous_contract, dict):
            previous_roles = previous_contract.get('auxiliary_roles', {})
            if isinstance(previous_roles, dict):
                previous_values = previous_roles.get('dynamic_nodes', [])
                if isinstance(previous_values, list):
                    previous_dynamic_nodes = [_lensde_configuration.finite_float(node, math.nan) for node in previous_values]
        chosen_nodes: list[float] = []
        chosen_slots: list[dict] = []
        assigned_region_ids: set[int] = set()

        def append_dynamic_node(node_value: object, source: str, region: dict | None=None) -> bool:
            node = _lensde_configuration.finite_float(node_value, math.nan)
            if not math.isfinite(node) or any((abs(node - main) <= main_tolerance for main in main_nodes)) or any((abs(node - existing) <= 1e-06 for existing in chosen_nodes)):
                return False
            region_id = int(region.get('region_id', 0) or 0) if isinstance(region, dict) else 0
            if region_id > 0 and region_id in assigned_region_ids:
                return False
            chosen_nodes.append(node)
            if region_id > 0:
                assigned_region_ids.add(region_id)
            chosen_slots.append({'node': node, 'source': source, 'region_id': region_id if region_id > 0 else None, 'region_start_node': region.get('start_node') if isinstance(region, dict) else None, 'region_end_node': region.get('end_node') if isinstance(region, dict) else None, 'region_severity_um_deg': region.get('severity_um_deg') if isinstance(region, dict) else None})
            return True
        forced_node = _lensde_configuration.finite_float(forced_anomaly_node, math.nan)
        if math.isfinite(forced_node):
            forced_region = next((region for region in selection.get('regions', []) if isinstance(region, dict) and _lensde_configuration.finite_float(region.get('start_node'), math.inf) <= forced_node <= _lensde_configuration.finite_float(region.get('end_node'), -math.inf)), None)
            append_dynamic_node(forced_node, 'rejected_child_degradation_guard', forced_region)
        for previous_node in previous_dynamic_nodes:
            if len(chosen_nodes) >= auxiliary_count:
                break
            if not math.isfinite(previous_node) or previous_node >= dynamic_max_node:
                continue
            matching_regions = [region for region in ranked_regions if int(region.get('region_id', 0) or 0) not in assigned_region_ids and _lensde_configuration.finite_float(region.get('start_node'), math.inf) - main_tolerance <= previous_node <= _lensde_configuration.finite_float(region.get('end_node'), -math.inf) + main_tolerance]
            if matching_regions:
                append_dynamic_node(previous_node, 'retained_previous_anomaly_region', matching_regions[0])
        for region in ranked_regions:
            if len(chosen_nodes) >= auxiliary_count:
                break
            append_dynamic_node(region.get('representative_node'), 'new_anomaly_region_representative', region)
        raw_candidates = list(selection.get('new_nodes', []))
        raw_candidates.extend(raw_auxiliary_nodes)
        for node_value in raw_candidates:
            if len(chosen_nodes) >= auxiliary_count:
                break
            node = _lensde_configuration.finite_float(node_value, math.nan)
            if math.isfinite(node) and node < dynamic_max_node:
                append_dynamic_node(node, 'nonedge_selector_fallback')
        eligible_rows = [row for row in validation_rows if _lensde_configuration.finite_float(row.get('node'), math.inf) < dynamic_max_node and (not any((abs(_lensde_configuration.finite_float(row.get('node'), math.nan) - main) <= main_tolerance for main in main_nodes)))]
        while len(chosen_nodes) < auxiliary_count and eligible_rows:
            anchors = [*main_nodes, *chosen_nodes]
            row = max(eligible_rows, key=lambda candidate: (min((abs(_lensde_configuration.finite_float(candidate.get('node'), 0.0) - anchor) for anchor in anchors)), _lensde_configuration.finite_float(candidate.get('absolute_risk'), 0.0)))
            if not append_dynamic_node(row.get('node'), 'largest_nonedge_coverage_gap'):
                eligible_rows.remove(row)
        if len(chosen_nodes) != auxiliary_count:
            raise RuntimeError('fixed-edge contract could not allocate two dynamic regions')
        auxiliary_nodes = chosen_nodes
        auxiliary_roles = {'policy': 'fixed_edge_prior_plus_two_region_slots', 'dynamic_nodes': chosen_nodes, 'dynamic_slots': chosen_slots, 'dynamic_max_node': dynamic_max_node, 'available_nonedge_region_count': len(ranked_regions), 'assigned_region_ids': sorted(assigned_region_ids), 'forced_anomaly_node': forced_node if math.isfinite(forced_node) else None}
    elif auxiliary_count == 2 and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_auxiliary_edge_guard_enabled'), True):
        spacing = min((right - left for left, right in zip(validation_nodes, validation_nodes[1:]) if right > left), default=0.0)
        main_tolerance = max(1e-06, 0.5 * spacing)
        eligible_rows = [row for row in validation_rows if math.isfinite(_lensde_configuration.finite_float(row.get('node'), math.nan)) and (not any((abs(_lensde_configuration.finite_float(row.get('node'), math.nan) - main) <= main_tolerance for main in main_nodes)))]
        edge_min_node = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_auxiliary_edge_guard_min_node', 0.85, 0.0)
        edge_rows = [row for row in eligible_rows if _lensde_configuration.finite_float(row.get('node'), -math.inf) >= edge_min_node]
        if not edge_rows:
            edge_rows = list(eligible_rows)
        if not edge_rows:
            raise RuntimeError('dynamic field contract has no edge guard candidate')
        best_edge = max(edge_rows, key=lambda row: row['absolute_risk'])
        guard_row = best_edge
        guard_source = 'current_fixed_validation_edge_risk'
        previous_guard = math.nan
        if isinstance(previous_contract, dict):
            previous_roles = previous_contract.get('auxiliary_roles', {})
            if isinstance(previous_roles, dict):
                previous_guard = _lensde_configuration.finite_float(previous_roles.get('edge_guard_node'), math.nan)
        if math.isfinite(previous_guard):
            previous_rows = sorted(edge_rows, key=lambda row: abs(row['node'] - previous_guard))
            if previous_rows and abs(previous_rows[0]['node'] - previous_guard) <= max(1e-06, 0.5 * spacing):
                hysteresis_ratio = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_auxiliary_guard_hysteresis_ratio', 0.9, 0.0)
                if previous_rows[0]['absolute_risk'] >= hysteresis_ratio * best_edge['absolute_risk']:
                    guard_row = previous_rows[0]
                    guard_source = 'retained_previous_edge_guard_hysteresis'
        edge_guard_node = _lensde_configuration.finite_float(guard_row.get('node'), math.nan)
        coverage_node = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_field_sample_coverage_deg', 2.5, 0.0) / max(1e-12, _lensde_configuration.finite_float(system.get('max_field_deg'), 0.0))
        forced_node = _lensde_configuration.finite_float(forced_anomaly_node, math.nan)
        anomaly_node = math.nan
        anomaly_source = 'dense_interpolation_anomaly'
        if math.isfinite(forced_node) and (not any((abs(forced_node - main) <= main_tolerance for main in main_nodes))) and (abs(forced_node - edge_guard_node) > 1e-06):
            anomaly_node = forced_node
            anomaly_source = 'rejected_child_degradation_guard'
        if not math.isfinite(anomaly_node):
            raw_candidates = list(selection.get('new_nodes', []))
            raw_candidates.extend(raw_auxiliary_nodes)
            for node_value in raw_candidates:
                node = _lensde_configuration.finite_float(node_value, math.nan)
                if math.isfinite(node) and (not any((abs(node - main) <= main_tolerance for main in main_nodes))) and (abs(node - edge_guard_node) > max(1e-06, coverage_node)):
                    anomaly_node = node
                    break
        if not math.isfinite(anomaly_node):
            fallback_rows = sorted(eligible_rows, key=lambda row: row['absolute_risk'], reverse=True)
            for row in fallback_rows:
                node = _lensde_configuration.finite_float(row.get('node'), math.nan)
                if abs(node - edge_guard_node) > max(1e-06, coverage_node):
                    anomaly_node = node
                    anomaly_source = 'highest_remaining_absolute_risk'
                    break
        if not math.isfinite(anomaly_node):
            raise RuntimeError('dynamic field contract has no anomaly probe candidate')
        auxiliary_nodes = [edge_guard_node, anomaly_node]
        auxiliary_roles = {'policy': 'persistent_edge_guard_plus_global_anomaly', 'edge_guard_node': edge_guard_node, 'edge_guard_source': guard_source, 'edge_guard_min_node': edge_min_node, 'edge_guard_absolute_risk': guard_row['absolute_risk'], 'current_best_edge_node': best_edge['node'], 'current_best_edge_absolute_risk': best_edge['absolute_risk'], 'anomaly_node': anomaly_node, 'anomaly_source': anomaly_source, 'forced_anomaly_node': forced_node if math.isfinite(forced_node) else None}
    selected_nodes = sorted({*main_nodes, *auxiliary_nodes})
    if len(selected_nodes) != target_count:
        raise RuntimeError(f'dynamic field contract expected {target_count} distinct fields; selected {len(selected_nodes)}')
    field_reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, system)
    selected_system = _lensde_fields.resample_system_fields_for_asphere(system, selected_nodes, reference_max_radius=field_reference_radius)
    audit = {'contract_version': int(contract_version), 'role': 'dynamic_de_objective_only', 'main_nodes': main_nodes, 'auxiliary_nodes': auxiliary_nodes, 'auxiliary_roles': auxiliary_roles, 'selected_nodes': selected_nodes, 'field_reference_radius': field_reference_radius, 'physical_field_radii': [_lensde_geometry.field_radius(field) for field in selected_system.get('fields', []) if isinstance(field, dict)], 'validation_field_count': len(validation_nodes), 'validation_merit': _lensde_configuration.finite_float(validation_score.get('merit'), math.inf), 'selection': selection}
    return (selected_system, audit)


def post_asphere_multistart_input_population_size(options: dict, variable_count: int | None=None) -> int:
    """Resolve an exact absolute override or the default dimension multiplier."""
    absolute = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_population', 0, 0)
    if absolute > 0:
        return absolute
    dimension = max(1, int(variable_count or 1))
    multiplier = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_multiplier', 5.0, 0.01)
    return max(5, int(math.ceil(multiplier * dimension)))


def post_asphere_top_storage_key(branch_index: int) -> str:
    """Bounded on-disk key; descriptive ancestry remains in the logical label."""
    return f'top_{max(1, int(branch_index)):02d}'


def post_asphere_score_summary(stage_dir: Path, expected_field_count: int, min_coverage: float, target_mean_rms_um: float, image_quality_metric: str='rms_spot') -> dict:
    score = first_existing_json([stage_dir / 'score.json', stage_dir / 'checkpoint_score.json', stage_dir / 'initial_score.json'], {})
    score = score if isinstance(score, dict) else {}
    contributions = score.get('contributions', [])
    rms_rows = [row for row in contributions if isinstance(row, dict) and str(row.get('name') or '').strip().lower() == 'rms_spot'] if isinstance(contributions, list) else []
    rms_rows.sort(key=lambda row: int(row.get('index', 0) or 0))
    field_rms_um = [1000.0 * _lensde_configuration.finite_float(row.get('value'), math.inf) for row in rms_rows]
    wavefront_rows = [row for row in contributions if isinstance(row, dict) and str(row.get('name') or '').strip().lower() == 'rms_wavefront'] if isinstance(contributions, list) else []
    wavefront_rows.sort(key=lambda row: int(row.get('index', 0) or 0))
    field_wavefront_rms = [_lensde_configuration.finite_float(row.get('value'), math.inf) for row in wavefront_rows]
    finite_fields = [value for value in field_rms_um if math.isfinite(value)]
    if finite_fields and len(finite_fields) == len(field_rms_um):
        mean_rms_um = sum(finite_fields) / len(finite_fields)
        max_rms_um = max(finite_fields)
    else:
        mean_rms_um = 1000.0 * _lensde_configuration.finite_float(score.get('mean_spot'), math.inf)
        max_rms_um = 1000.0 * _lensde_configuration.finite_float(score.get('max_spot'), math.inf)
    coverage = _lensde_configuration.finite_float(score.get('trace_coverage'), 0.0)
    min_field_coverage = _lensde_configuration.finite_float(score.get('min_field_coverage'), 0.0)
    metric = _lensde_defaults.canonical_image_quality_metric(image_quality_metric, 'rms_spot')
    required_rows_complete = bool((metric not in {'rms_spot', 'rms_spot_wavefront'} or len(field_rms_um) == expected_field_count) and (metric not in {'rms_wavefront', 'rms_spot_wavefront'} or len(field_wavefront_rms) == expected_field_count))
    strict_valid = bool(math.isfinite(_lensde_configuration.finite_float(score.get('merit'), math.inf)) and math.isfinite(mean_rms_um) and math.isfinite(max_rms_um) and required_rows_complete and (coverage + 1e-12 >= min_coverage) and (min_field_coverage + 1e-12 >= min_coverage))
    return {'merit': _lensde_configuration.finite_float(score.get('merit'), math.inf), 'field_rms_um': field_rms_um, 'field_wavefront_rms': field_wavefront_rms, 'mean_rms_um': mean_rms_um, 'max_rms_um': max_rms_um, 'mean_wavefront_rms': _lensde_configuration.finite_float(score.get('mean_wavefront_rms'), math.inf), 'max_wavefront_rms': _lensde_configuration.finite_float(score.get('max_wavefront_rms'), math.inf), 'image_quality_metric': metric, 'trace_ok': _lensde_commands.option_truthy(score.get('trace_ok'), False), 'trace_coverage': coverage, 'min_field_coverage': min_field_coverage, 'constraint_group_error': _lensde_configuration.finite_float(score.get('constraint_group_error'), math.inf), 'first_order_group_error': _lensde_configuration.finite_float(score.get('first_order_group_error'), math.inf), 'image_group_error': _lensde_configuration.finite_float(score.get('image_group_error'), math.inf), 'strict_valid': strict_valid, 'target_mean_rms_um': target_mean_rms_um, 'target_reached': strict_valid and mean_rms_um < target_mean_rms_um}


def post_asphere_sampling_underestimation(low_score: dict, high_score: dict, threshold_um: float) -> dict:
    """Compare the same fields under base and denser pupil sampling."""
    low_values = [_lensde_configuration.finite_float(value, math.inf) for value in low_score.get('field_rms_um', [])]
    high_values = [_lensde_configuration.finite_float(value, math.inf) for value in high_score.get('field_rms_um', [])]
    threshold = max(0.0, _lensde_configuration.finite_float(threshold_um, 1.0))
    if not low_values or len(low_values) != len(high_values):
        return {'triggered': False, 'valid': False, 'reason': 'field RMS vectors do not share a complete contract', 'threshold_um': threshold, 'fields': []}
    rows: list[dict] = []
    for index, (low_value, high_value) in enumerate(zip(low_values, high_values, strict=True)):
        delta = high_value - low_value
        rows.append({'field_index': index, 'base_rms_um': low_value, 'probe_rms_um': high_value, 'underestimate_um': delta, 'threshold_um': threshold, 'underestimated': bool(math.isfinite(delta) and delta > threshold)})
    valid = bool(all((math.isfinite(row['base_rms_um']) and math.isfinite(row['probe_rms_um']) for row in rows)))
    triggered = valid and any((row['underestimated'] for row in rows))
    return {'triggered': triggered, 'valid': valid, 'reason': 'denser pupil exceeds base RMS by more than the configured threshold' if triggered else 'base pupil sampling retained', 'threshold_um': threshold, 'base_strict_valid': bool(low_score.get('strict_valid', False)), 'probe_strict_valid': bool(high_score.get('strict_valid', False)), 'max_underestimate_um': max((row['underestimate_um'] for row in rows), default=0.0), 'fields': rows}


def post_asphere_adaptive_trace(stage_dir: Path) -> dict:
    trace_path = stage_dir / 'adaptive_hybrid_trace.jsonl'
    epochs: list[dict] = []
    effective_population = 0
    actual_generations = 0
    if trace_path.is_file():
        try:
            lines = trace_path.read_text(encoding='utf-8', errors='replace').splitlines()
        except OSError:
            lines = []
        for line in lines:
            try:
                row = json.loads(line)
            except (TypeError, ValueError):
                continue
            event = str(row.get('event') or '')
            if event == 'adaptive_initial':
                effective_population = int(row.get('population_size', 0) or 0)
            actual_generations = max(actual_generations, int(row.get('de_generations', 0) or 0))
            if event != 'adaptive_epoch_end':
                continue
            best = row.get('population_best')
            if not isinstance(best, dict):
                best = row.get('best') if isinstance(row.get('best'), dict) else {}
            epochs.append({'epoch': int(row.get('epoch', 0) or 0), 'generations': int(row.get('de_generations', 0) or 0), 'population_size': int(row.get('population_size', 0) or 0), 'merit': _lensde_configuration.finite_float(best.get('merit'), math.inf), 'mean_rms_um': 1000.0 * _lensde_configuration.finite_float(best.get('mean_spot'), math.inf), 'max_rms_um': 1000.0 * _lensde_configuration.finite_float(best.get('max_spot'), math.inf), 'phase_seconds': _lensde_configuration.finite_float(row.get('phase_seconds'), 0.0)})
    if not epochs:
        timeline_path = stage_dir / 'timeline.jsonl'
        if timeline_path.is_file():
            trace_path = timeline_path
            try:
                lines = timeline_path.read_text(encoding='utf-8', errors='replace').splitlines()
            except OSError:
                lines = []
            for line in lines:
                try:
                    row = json.loads(line)
                except (TypeError, ValueError):
                    continue
                actual_generations = max(actual_generations, int(row.get('de_generations', 0) or 0))
                if str(row.get('event') or '') != 'adaptive_epoch_end':
                    continue
                population_size = int(row.get('population_size', row.get('pop', 0)) or 0)
                effective_population = max(effective_population, population_size)
                epochs.append({'epoch': int(row.get('epoch', 0) or 0), 'generations': int(row.get('de_generations', 0) or 0), 'population_size': population_size, 'merit': _lensde_configuration.finite_float(row.get('population_best_merit'), math.inf), 'mean_rms_um': 1000.0 * _lensde_configuration.finite_float(row.get('population_best_mean_spot'), math.inf), 'max_rms_um': 1000.0 * _lensde_configuration.finite_float(row.get('population_best_max_spot'), math.inf), 'phase_seconds': _lensde_configuration.finite_float(row.get('phase_seconds'), 0.0)})
    result_doc = first_existing_json([stage_dir / 'result.json'], {})
    if isinstance(result_doc, dict):
        actual_generations = max(actual_generations, int(result_doc.get('generations', 0) or 0))
        effective_population = max(effective_population, int(result_doc.get('population_size', 0) or 0))
    return {'path': str(trace_path), 'effective_population': effective_population, 'actual_generations': actual_generations, 'epochs': epochs}


def post_asphere_multistart_write_input(input_dir: Path, system: dict, variables: list[dict], merit: dict, population_rows: list[list[float]], options: dict | None=None) -> dict:
    if not variables or not population_rows:
        raise ValueError('post-asphere multistart input requires variables and a population')
    if any((len(row) != len(variables) for row in population_rows)):
        raise ValueError('post-asphere multistart population width does not match variables')
    input_dir.mkdir(parents=True, exist_ok=True)
    system_path = input_dir / 'system_input.json'
    variables_path = input_dir / 'variables.json'
    merit_path = input_dir / 'merit.json'
    seed_path = input_dir / 'initial_population.csv'
    input_system = _lensde_configuration.system_with_ray_aiming(system, options) if isinstance(options, dict) else copy.deepcopy(system)
    if isinstance(options, dict):
        expected_aiming = post_asphere_aiming_contract(options)
        actual_aiming = _lensde_configuration.canonical_aiming_mode(input_system.get('ray_aiming', {}).get('mode'), '')
        if actual_aiming != expected_aiming:
            raise ValueError(f'prepared post-asphere system aiming mode {actual_aiming!r} does not match contract {expected_aiming!r}')
    _lensde_provenance.write_json(system_path, input_system)
    _lensde_provenance.write_json(variables_path, {'variables': variables})
    _lensde_provenance.write_json(merit_path, merit)
    _lensde_populations.write_population_csv(seed_path, population_rows)
    return {'system_path': system_path, 'variables_path': variables_path, 'merit_path': merit_path, 'seed_path': seed_path, 'population_rows': len(population_rows)}


def post_asphere_multistart_endpoint(stage_dir: Path) -> tuple[dict, list[dict], list[list[float]]]:
    final_score = _lensde_provenance.read_json_optional(stage_dir / 'score.json')
    checkpoint_score = _lensde_provenance.read_json_optional(stage_dir / 'checkpoint_score.json')
    final_merit = _lensde_configuration.finite_float(final_score.get('merit') if isinstance(final_score, dict) else None, math.inf)
    checkpoint_merit = _lensde_configuration.finite_float(checkpoint_score.get('merit') if isinstance(checkpoint_score, dict) else None, math.inf)
    system_candidates = [stage_dir / 'checkpoint_system.json', stage_dir / 'system.json'] if checkpoint_merit < final_merit else [stage_dir / 'system.json', stage_dir / 'checkpoint_system.json']
    system = first_existing_json(system_candidates, {})
    variables_payload = first_existing_json([stage_dir / 'variables.json', stage_dir / 'prepared_input' / 'variables.json'], {'variables': []})
    variables = variables_payload.get('variables', variables_payload) if isinstance(variables_payload, dict) else variables_payload
    population = _lensde_populations.read_population_csv(stage_dir / 'population.csv')
    if not isinstance(system, dict) or not system:
        raise RuntimeError(f'post-asphere DE stage has no output system: {stage_dir}')
    if not isinstance(variables, list) or not variables:
        raise RuntimeError(f'post-asphere DE stage has no variables: {stage_dir}')
    if not population:
        raise RuntimeError(f'post-asphere DE stage has no final population: {stage_dir}')
    return (system, variables, population)


def post_asphere_long_population_scales(options: dict) -> list[float]:
    raw = options.get('initial_search_post_asphere_long_population_scales', '0.0125,0.025,0.05')
    values = raw if isinstance(raw, (list, tuple)) else str(raw or '').split(',')
    scales = [_lensde_configuration.finite_float(value, math.nan) for value in values if math.isfinite(_lensde_configuration.finite_float(value, math.nan)) and _lensde_configuration.finite_float(value, math.nan) > 0.0]
    return scales or [0.0125, 0.025, 0.05]


def post_asphere_ranked_population_rows(stage_dir: Path, target_variables: list[dict]) -> list[tuple[float, list[float]]]:
    try:
        _system, source_variables, rows = post_asphere_multistart_endpoint(stage_dir)
    except (OSError, RuntimeError, ValueError):
        return []
    merits: list[float] = []
    merit_path = stage_dir / 'merits.csv'
    if merit_path.is_file():
        try:
            merits = [_lensde_configuration.finite_float(line.strip().split(',')[0], math.inf) for line in merit_path.read_text(encoding='utf-8', errors='replace').splitlines() if line.strip()]
        except OSError:
            merits = []
    ranked: list[tuple[float, list[float]]] = []
    for row_index, source_row in enumerate(rows):
        mapped = _lensde_commands.map_population_row(source_row, source_variables, target_variables)
        if len(mapped) != len(target_variables):
            continue
        within_bounds = all((math.isfinite(value) and _lensde_configuration.finite_float(variable.get('lower'), -math.inf) <= value <= _lensde_configuration.finite_float(variable.get('upper'), math.inf) for value, variable in zip(mapped, target_variables, strict=True)))
        if not within_bounds:
            continue
        merit_value = merits[row_index] if row_index < len(merits) else math.inf
        ranked.append((merit_value, mapped))
    ranked.sort(key=lambda item: item[0])
    return ranked


def post_asphere_sag_height_caps_um(options: dict) -> list[float]:
    raw = options.get('initial_search_post_asphere_sag_height_caps_um', '0.1,0.3,1.0')
    values = raw if isinstance(raw, (list, tuple)) else str(raw or '').split(',')
    caps = [_lensde_configuration.finite_float(value, math.nan) for value in values if math.isfinite(_lensde_configuration.finite_float(value, math.nan)) and _lensde_configuration.finite_float(value, math.nan) > 0.0]
    return caps or [0.1, 0.3, 1.0]


def post_asphere_complete_coefficient_groups(variables: list[dict]) -> list[dict]:
    """Group contiguous A4..Amax variables without opening higher orders."""
    grouped: dict[int, dict[int, int]] = {}
    for column, variable in enumerate(variables):
        if str(variable.get('kind', '')).lower() != 'asphere_coeff':
            continue
        surface = int(_lensde_configuration.finite_float(variable.get('index'), -1.0))
        term = int(_lensde_configuration.finite_float(variable.get('term'), 0.0))
        if 1 <= term <= 7:
            grouped.setdefault(surface, {})[term] = column
    result: list[dict] = []
    for surface, terms in sorted(grouped.items()):
        active_terms = list(range(1, max(terms) + 1))
        if set(terms) != set(active_terms):
            continue
        columns = [terms[term] for term in active_terms]
        radii = [_lensde_configuration.finite_float(variables[column].get('asphere_reference_radius'), math.nan) for column in columns]
        if not all((math.isfinite(radius) and radius > 0.0 for radius in radii)):
            continue
        if max(radii) - min(radii) > 1e-09 * max(radii):
            continue
        result.append({'surface': surface, 'columns': columns, 'radius': radii[0], 'terms': active_terms})
    return result


def solve_small_dense_system(matrix: list[list[float]], values: list[float]) -> list[float] | None:
    size = len(values)
    if size <= 0 or len(matrix) != size or any((len(row) != size for row in matrix)):
        return None
    augmented = [[float(value) for value in row] + [float(values[index])] for index, row in enumerate(matrix)]
    for column in range(size):
        pivot = max(range(column, size), key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) <= 1e-18:
            return None
        if pivot != column:
            augmented[column], augmented[pivot] = (augmented[pivot], augmented[column])
        divisor = augmented[column][column]
        for item in range(column, size + 1):
            augmented[column][item] /= divisor
        for row in range(size):
            if row == column:
                continue
            factor = augmented[row][column]
            if factor == 0.0:
                continue
            for item in range(column, size + 1):
                augmented[row][item] -= factor * augmented[column][item]
    solution = [augmented[index][size] for index in range(size)]
    return solution if all((math.isfinite(value) for value in solution)) else None


def cholesky_lower(matrix: list[list[float]]) -> list[list[float]] | None:
    size = len(matrix)
    if size <= 0 or any((len(row) != size for row in matrix)):
        return None
    lower = [[0.0] * size for _ in range(size)]
    for row in range(size):
        for column in range(row + 1):
            residual = matrix[row][column] - sum((lower[row][index] * lower[column][index] for index in range(column)))
            if row == column:
                if residual <= 1e-15:
                    return None
                lower[row][column] = math.sqrt(residual)
            else:
                if abs(lower[column][column]) <= 1e-18:
                    return None
                lower[row][column] = residual / lower[column][column]
    return lower


def post_asphere_correlated_sag_move(values: list[float], variables: list[dict], group: dict, target_cap_um: float, correlation_length: float, rng: random.Random) -> tuple[list[float], dict] | None:
    columns = [int(column) for column in group['columns']]
    terms = [int(variables[column]['term']) for column in columns]
    dimension = len(columns)
    if not dimension or terms != list(range(1, dimension + 1)):
        return None
    nodes = [(index + 1) / float(dimension) for index in range(dimension)]
    powers = [term + 1 for term in terms]
    vandermonde = [[node ** (2 * power) for power in powers] for node in nodes]
    length = max(0.001, float(correlation_length))
    covariance = [[math.exp(-0.5 * ((left - right) / length) ** 2) + (1e-10 if left_index == right_index else 0.0) for right_index, right in enumerate(nodes)] for left_index, left in enumerate(nodes)]
    lower = cholesky_lower(covariance)
    if lower is None:
        return None
    target_cap_mm = max(0.0, target_cap_um) * 0.001
    radius = _lensde_configuration.finite_float(group.get('radius'), math.nan)
    if target_cap_mm <= 0.0 or not math.isfinite(radius) or radius <= 0.0:
        return None
    best: tuple[float, list[float], dict] | None = None
    for _attempt in range(128):
        normal = [rng.gauss(0.0, 1.0) for _ in range(dimension)]
        profile = [sum((lower[row][column] * normal[column] for column in range(row + 1))) for row in range(dimension)]
        profile_scale = max((abs(value) for value in profile))
        if profile_scale <= 1e-15:
            continue
        desired = [target_cap_mm * node ** 4 * profile[index] / profile_scale for index, node in enumerate(nodes)]
        delta_edge = solve_small_dense_system(vandermonde, desired)
        if delta_edge is None:
            continue
        dense_peak = max((abs(sum((delta_edge[index] * (sample / 256.0) ** (2 * power) for index, power in enumerate(powers)))) for sample in range(257)))
        if not math.isfinite(dense_peak) or dense_peak <= 1e-18:
            continue
        delta_edge = [value * target_cap_mm / dense_peak for value in delta_edge]
        delta_coefficients = [delta_edge[index] / radius ** (2 * (term + 1)) for index, term in enumerate(terms)]
        bound_scale = 1.0
        for column, delta in zip(columns, delta_coefficients, strict=True):
            lower_bound, upper_bound = _lensde_populations.variable_bounds(variables[column])
            current = _lensde_configuration.finite_float(values[column], 0.0)
            if delta > 0.0:
                bound_scale = min(bound_scale, (upper_bound - current) / delta)
            elif delta < 0.0:
                bound_scale = min(bound_scale, (current - lower_bound) / -delta)
        bound_scale = max(0.0, min(1.0, 0.98 * bound_scale))
        candidate = list(values)
        for column, delta in zip(columns, delta_coefficients, strict=True):
            candidate[column] += bound_scale * delta
        actual_peak_um = target_cap_um * bound_scale
        info = {'surface': int(group['surface']), 'target_cap_um': target_cap_um, 'actual_peak_um': actual_peak_um, 'bound_scale': bound_scale}
        if best is None or actual_peak_um > best[0]:
            best = (actual_peak_um, candidate, info)
        if actual_peak_um >= 0.2 * target_cap_um:
            return (candidate, info)
    if best is None or best[0] <= 1e-09:
        return None
    return (best[1], best[2])


def post_asphere_restart_raw_bound_distance(left: list[float], right: list[float], variables: list[dict]) -> float:
    """Match the bound-normalized population geometry used by the winning probe."""
    total = 0.0
    count = 0
    for a, b, variable in zip(left, right, variables, strict=True):
        lower, upper = _lensde_populations.variable_bounds(variable)
        span = max(abs(upper - lower), 1e-30)
        delta = (a - b) / span
        total += delta * delta
        count += 1
    return math.sqrt(total / max(1, count))


def post_asphere_restart_quality_diverse_rows(ranked: list[tuple[float, list[float]]], anchor: list[float], variables: list[dict], count: int) -> list[list[float]]:
    unique: list[list[float]] = []
    seen = {tuple((float(value).hex() for value in anchor))}
    for _merit, row in ranked:
        key = tuple((float(value).hex() for value in row))
        if key in seen:
            continue
        seen.add(key)
        unique.append(row)
    pool = unique[:max(count * 3, count)]
    if count <= 0 or not pool:
        return []
    count = min(count, len(pool))
    selected = [pool.pop(0)]
    minimum_distances = [post_asphere_restart_raw_bound_distance(row, selected[0], variables) for row in pool]
    while len(selected) < count:
        denominator = max(1, len(pool) - 1)
        choice = max(range(len(pool)), key=lambda index: minimum_distances[index] * (1.0 - 0.4 * index / denominator))
        chosen = pool.pop(choice)
        minimum_distances.pop(choice)
        selected.append(chosen)
        for index, row in enumerate(pool):
            minimum_distances[index] = min(minimum_distances[index], post_asphere_restart_raw_bound_distance(row, chosen, variables))
    return selected


def post_asphere_restart_single_sag_rows(anchor: list[float], variables: list[dict], groups: list[dict], caps: list[float], correlation_length: float, count: int, rng: random.Random, seen: set[tuple[str, ...]]) -> tuple[list[list[float]], list[dict]]:
    schedule = [caps[index % len(caps)] for index in range(count)]
    rng.shuffle(schedule)
    rows: list[list[float]] = []
    infos: list[dict] = []
    for target_cap_um in schedule:
        accepted: tuple[list[float], dict] | None = None
        for _attempt in range(512):
            result = post_asphere_correlated_sag_move(anchor, variables, rng.choice(groups), target_cap_um, correlation_length, rng)
            if result is None:
                continue
            key = tuple((float(value).hex() for value in result[0]))
            if key in seen:
                continue
            accepted = result
            seen.add(key)
            break
        if accepted is None:
            raise RuntimeError('could not generate a unique single-surface sag row')
        rows.append(accepted[0])
        infos.append(accepted[1])
    return (rows, infos)


def post_asphere_restart_coupled_sag_rows(anchor: list[float], variables: list[dict], groups: list[dict], caps: list[float], correlation_length: float, count: int, rng: random.Random, seen: set[tuple[str, ...]]) -> tuple[list[list[float]], list[dict]]:
    schedule = [caps[index % len(caps)] for index in range(count)]
    rng.shuffle(schedule)
    rows: list[list[float]] = []
    infos: list[dict] = []
    for target_cap_um in schedule:
        accepted_row: list[float] | None = None
        accepted_info: dict | None = None
        for _attempt in range(512):
            surface_count = rng.choices([2, 3, 4], weights=[0.45, 0.35, 0.2], k=1)[0]
            surface_count = min(surface_count, len(groups))
            chosen_groups = rng.sample(groups, surface_count)
            candidate = list(anchor)
            move_infos: list[dict] = []
            per_surface_cap = target_cap_um / math.sqrt(surface_count)
            for group in chosen_groups:
                result = post_asphere_correlated_sag_move(candidate, variables, group, per_surface_cap, correlation_length, rng)
                if result is None:
                    move_infos = []
                    break
                candidate, info = result
                move_infos.append(info)
            key = tuple((float(value).hex() for value in candidate))
            if len(move_infos) != surface_count or key in seen:
                continue
            accepted_row = candidate
            accepted_info = {'surface_count': surface_count, 'surfaces': [int(info['surface']) for info in move_infos], 'global_target_cap_um': target_cap_um, 'per_surface_target_cap_um': per_surface_cap, 'minimum_bound_scale': min((_lensde_configuration.finite_float(info.get('bound_scale'), 0.0) for info in move_infos))}
            seen.add(key)
            break
        if accepted_row is None or accepted_info is None:
            raise RuntimeError('could not generate a unique coupled-surface sag row')
        rows.append(accepted_row)
        infos.append(accepted_info)
    return (rows, infos)


def post_asphere_restart_direction_rows(anchor: list[float], variables: list[dict], source_rows: list[list[float]], count: int, rng: random.Random, seen: set[tuple[str, ...]]) -> tuple[list[list[float]], list[dict]]:
    rows: list[list[float]] = []
    infos: list[dict] = []
    for _row_index in range(count):
        accepted: tuple[list[float], dict] | None = None
        for _attempt in range(1024):
            left, right = rng.sample(source_rows, 2)
            alpha = rng.choice([0.25, 0.5, 0.75, 1.0])
            deltas = [alpha * (a - b) for a, b in zip(left, right, strict=True)]
            bound_scale = 1.0
            for value, delta, variable in zip(anchor, deltas, variables, strict=True):
                lower, upper = _lensde_populations.variable_bounds(variable)
                if delta > 0.0:
                    bound_scale = min(bound_scale, (upper - value) / delta)
                elif delta < 0.0:
                    bound_scale = min(bound_scale, (value - lower) / -delta)
            bound_scale = max(0.0, min(1.0, 0.98 * bound_scale))
            candidate = [value + bound_scale * delta for value, delta in zip(anchor, deltas, strict=True)]
            distance = post_asphere_restart_raw_bound_distance(candidate, anchor, variables)
            key = tuple((float(value).hex() for value in candidate))
            if distance <= 1e-08 or key in seen:
                continue
            accepted = (candidate, {'alpha': alpha, 'bound_scale': bound_scale, 'bound_normalized_rms': distance})
            seen.add(key)
            break
        if accepted is None:
            raise RuntimeError('could not generate a unique terminal-direction row')
        rows.append(accepted[0])
        infos.append(accepted[1])
    return (rows, infos)


def post_asphere_multistart_ranking_key(candidate: dict, image_quality_metric: str='rms_spot') -> tuple:
    score = candidate.get('audit', {}).get('score', {})
    island_result = candidate.get('de', {}).get('island_result', {})
    parity_ok = 'cpu_cuda_materialization_consistent' not in island_result or _lensde_commands.option_truthy(island_result.get('cpu_cuda_materialization_consistent'), False)
    validity = (not parity_ok, not _lensde_commands.option_truthy(score.get('strict_valid'), False), -_lensde_configuration.finite_float(score.get('min_field_coverage'), 0.0))
    metric = _lensde_defaults.canonical_image_quality_metric(image_quality_metric, 'rms_spot')
    if metric == 'rms_spot':
        objective = (_lensde_configuration.finite_float(score.get('mean_rms_um'), math.inf), _lensde_configuration.finite_float(score.get('max_rms_um'), math.inf), _lensde_configuration.finite_float(score.get('merit'), math.inf))
    else:
        objective = (_lensde_configuration.finite_float(score.get('merit'), math.inf), _lensde_configuration.finite_float(score.get('mean_rms_um'), math.inf), _lensde_configuration.finite_float(score.get('max_rms_um'), math.inf))
    return validity + objective + (str(candidate.get('label') or ''),)


def compact_post_asphere_multistart_candidate(candidate: dict) -> dict:
    return {'label': candidate.get('label'), 'stage_dir': candidate.get('stage_dir'), 'de': candidate.get('de', {}), 'audit': candidate.get('audit', {}), 'cumulative_generations': candidate.get('cumulative_generations', 0)}


def post_asphere_direction_scout_generations(options: dict, merit: dict) -> int:
    """Choose the independently configurable budget from the actual merit.

    Do not use cached options or stage-boundary Set Vignetting: that boundary
    can also be enabled for a no-vignetting optimization. Scout runs one
    continuous epoch, independently of Formation's early-stop block length.
    """
    if canonical_post_asphere_flow(options.get('initial_search_post_asphere_flow')) == 'curvature_normalized_formation_periodic':
        return _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_scout_generations', 400, 1)
    active_vignetting = _lensde_configuration.vignetting_optimization_enabled(merit)
    key = 'initial_search_post_asphere_single_mother_scout_vignetting_generations' if active_vignetting else 'initial_search_post_asphere_single_mother_scout_generations'
    return _lensde_commands.option_int_from(options, key, 800 if active_vignetting else 400, 1)


def post_asphere_direction_scout_population_median(stage_dir: Path, population_size: int) -> float:
    """Use every terminal row, without rescoring or dropping invalid merits."""
    if population_size <= 0:
        raise ValueError('direction scout population must be nonempty')
    path = stage_dir / 'merits.csv'
    if path.is_file():
        with path.open(newline='', encoding='utf-8-sig') as stream:
            rows = list(csv.reader(stream))
        if len(rows) != population_size or any((len(row) != 1 for row in rows)):
            raise RuntimeError(f'incomplete direction scout population merits: {path}')
        values = [_lensde_configuration.finite_float(row[0], math.inf) for row in rows]
    else:
        path = stage_dir / 'population_scores.csv'
        with path.open(newline='', encoding='utf-8-sig') as stream:
            reader = csv.DictReader(stream)
            if not {'index', 'merit'}.issubset(reader.fieldnames or []):
                raise RuntimeError(f'missing direction scout merit columns: {path}')
            records = list(reader)
        if len(records) != population_size or {row['index'] for row in records} != {str(i) for i in range(population_size)}:
            raise RuntimeError(f'incomplete direction scout population merits: {path}')
        values = [_lensde_configuration.finite_float(row['merit'], math.inf) for row in records]
    return statistics.median(values)


def post_asphere_direction_scout_sort_key(candidate: dict) -> tuple:
    return (_lensde_configuration.finite_float(candidate.get('best_merit'), math.inf), _lensde_configuration.finite_float(candidate.get('population_median_merit'), math.inf), candidate['label'])


def rank_post_asphere_complete_trace_population(records: list[dict], population_size: int) -> list[dict]:
    """Assign the native scalar order after a merit-only population rescore.

    The score-only evaluator deliberately does not emit optimizer ranks.  This
    helper is safe only when every candidate has a complete trace, because in
    that case the DE feasibility tier is identical for all rows and Merit is
    the native ordering key.
    """
    if len(records) != population_size:
        raise RuntimeError('post-asphere population rescore did not return every candidate')
    seen: set[int] = set()
    checked: list[dict] = []
    for record in records:
        index = int(_lensde_configuration.finite_float(record.get('index'), -1.0))
        if index < 0 or index >= population_size or index in seen:
            raise RuntimeError('post-asphere population rescore has invalid indices')
        seen.add(index)
        if not math.isfinite(_lensde_configuration.finite_float(record.get('merit'), math.inf)) or _lensde_configuration.finite_float(record.get('trace_ok'), 0.0) != 1.0 or _lensde_configuration.finite_float(record.get('trace_coverage'), 0.0) < 1.0 - 1e-12 or (_lensde_configuration.finite_float(record.get('min_field_coverage'), 0.0) < 1.0 - 1e-12) or any((_lensde_configuration.finite_float(record.get(key), 0.0) != 0.0 for key in ('chief_ray_failed_fields', 'zero_coverage_off_axis_fields', 'distortion_sign_flip_fields', 'ray_path_structural_failure_fields'))):
            raise RuntimeError('post-asphere population rescore cannot rank an incomplete trace')
        checked.append(dict(record))
    checked.sort(key=lambda row: (_lensde_configuration.finite_float(row.get('merit'), math.inf), int(_lensde_configuration.finite_float(row.get('index'), population_size))))
    for rank, record in enumerate(checked, 1):
        record['rank'] = rank
    return checked


def rescore_post_asphere_population_for_merit(system: dict, variables: list[dict], population_rows: list[list[float]], merit: dict, options: dict, executable: Path, output_dir: Path, timeout_seconds: int, started_at: float, child_cpu_workers: int, *, use_cuda_evaluator: bool) -> list[dict]:
    """Rescore and rank a terminal population under a changed stage merit."""
    output_dir = native_io_paths.python_path(output_dir.resolve())
    prepared = post_asphere_multistart_write_input(output_dir / 'prepared_input', system, variables, merit, population_rows, options)
    command = post_asphere_stage_base_args(executable, prepared['system_path'], prepared['merit_path'], prepared['variables_path'], options, use_cuda_evaluator=use_cuda_evaluator)
    command.extend(['--initial-population', str(prepared['seed_path']), '--score-population-only', '--out-dir', str(output_dir)])
    if use_cuda_evaluator:
        command.extend(['--de-cuda-min-batch', str(_lensde_commands.option_int_from(options, 'initial_search_post_asphere_de_cuda_min_batch', 1, 1))])
    if _lensde_commands.option_truthy(options.get('clear_aperture_update'), True):
        command.append('--clear-aperture-update')
    if _lensde_commands.option_truthy(options.get('sync_stop_aperture'), True):
        command.append('--sync-stop-aperture')
    result = run_command_logged(command, output_dir, timeout_seconds, started_at, child_cpu_workers)
    if int(result.get('returncode', -1) or 0) != 0 or _lensde_commands.option_truthy(result.get('timed_out'), False):
        raise RuntimeError(f"post-asphere merit rescore failed: returncode={result.get('returncode')}, timed_out={result.get('timed_out')}")
    records = _lensde_materials.read_population_score_records(output_dir / 'population_scores.csv')
    ranked = rank_post_asphere_complete_trace_population(records, len(population_rows))
    with (output_dir / 'ranked_population_scores.csv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(ranked[0]))
        writer.writeheader()
        writer.writerows(ranked)
    _lensde_provenance.write_json(output_dir / 'rescore_summary.json', {'population': len(population_rows), 'best_merit': _lensde_configuration.finite_float(ranked[0].get('merit'), math.inf), 'complete_trace_required': True, 'ranked_by': 'merit_then_population_index', 'evaluator': 'CUDA' if use_cuda_evaluator else 'CPU', 'command': command})
    return ranked


def post_asphere_work_dir(run_dir: Path) -> Path:
    """Choose a post-asphere root with headroom for deeply nested workers."""
    if native_io_paths.compact_names_enabled(run_dir):
        return run_dir / '_pa'
    reserve = 115
    preferred = run_dir / 'de_dls_key_outputs' / 'post_asphere_refine'
    if len(str(preferred.resolve())) + reserve <= 247:
        return preferred
    compact = run_dir / '_pa'
    if len(str(compact.resolve())) + reserve <= 247:
        return compact
    raise OSError(f'Post-asphere output root is too long: {run_dir}; choose shorter run/stage names. Temporary output relocation is disabled.')
