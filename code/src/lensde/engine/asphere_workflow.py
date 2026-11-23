"""Extracted paper engine: asphere_workflow."""
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
from . import asphere_contracts as _lensde_asphere_contracts
from . import asphere_population as _lensde_asphere_population
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

def run_post_asphere_field_probe(system: dict, source_variables: list[dict], merit_path: Path, probe_dir: Path, options: dict, exe: Path, timeout_seconds: int, started_at: float, child_cpu_workers: int=0) -> tuple[dict, dict]:
    field_reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, system)
    base_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_field_count', 9, 1)
    field_coordinate = _lensde_fields.canonical_post_asphere_field_coordinate(options.get('initial_search_post_asphere_field_coordinate'))
    contract_policy = _lensde_fields.canonical_post_asphere_field_contract_policy(options.get('initial_search_post_asphere_field_contract_policy'))
    adaptive_enabled = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_adaptive_fields_enabled'), True)
    preserve_existing_indices = any((str(variable.get('kind', '')).lower() in {'vignette_x', 'vignette_y', 'fvcy', 'fvdy', 'vignette_center_y', 'vignette_shift_y', 'vignette_magnitude_y', 'vignette_asymmetry_y'} for variable in source_variables if isinstance(variable, dict)))
    max_additions = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_adaptive_field_max_additions', 0, 0) if adaptive_enabled else 0
    needs_dense_probe = field_coordinate == 'chief_image_height' or max_additions > 0
    if not needs_dense_probe:
        base_system = _lensde_fields.densify_system_fields_for_asphere(system, base_count, preserve_existing_indices=preserve_existing_indices)
        existing_nodes = sorted(_lensde_fields.normalized_field_nodes(base_system))
        return (base_system, {'enabled': adaptive_enabled, 'skipped': True, 'reason': 'normalized-field contract needs no dense probe', 'field_coordinate_requested': field_coordinate, 'field_coordinate_applied': 'normalized_field', 'contract_policy': contract_policy, 'contract_locked': contract_policy == 'locked', 'base_field_count': len(existing_nodes), 'max_additions': 0, 'selected_nodes': existing_nodes, 'proposed_new_nodes': [], 'new_nodes': []})
    target_count = base_count + max_additions
    probe_count = max(target_count, _lensde_commands.option_int_from(options, 'initial_search_post_asphere_field_probe_count', 41, 3))
    dense_grid = [index / float(probe_count - 1) for index in range(probe_count)]
    dense_system = _lensde_fields.resample_system_fields_for_asphere(system, dense_grid, reference_max_radius=field_reference_radius)
    dense_variables = _lensde_initialization.append_post_asphere_variables(dense_system, source_variables, options)
    common_merit = _lensde_asphere_contracts.first_existing_json([merit_path], {})
    probe_merit = _lensde_asphere_contracts.post_asphere_spot_probe_merit(common_merit if isinstance(common_merit, dict) else {})
    probe_dir.mkdir(parents=True, exist_ok=True)
    system_path = probe_dir / 'system_input.json'
    variables_path = probe_dir / 'variables.json'
    probe_merit_path = probe_dir / 'merit.json'
    _lensde_provenance.write_json(system_path, dense_system)
    _lensde_provenance.write_json(variables_path, {'variables': dense_variables})
    _lensde_provenance.write_json(probe_merit_path, probe_merit)
    probe_options = copy.deepcopy(options)
    probe_options.update({'initial_search_post_asphere_spot_rays': 1, 'initial_search_post_asphere_spot_arms': 1, 'initial_search_post_asphere_spot_distribution': 'hexapolar'})
    command = _lensde_asphere_contracts.post_asphere_stage_base_args(exe, system_path, probe_merit_path, variables_path, probe_options, dynamic_apertures=True)
    command.extend(['--out-dir', str(probe_dir)])
    result = _lensde_asphere_contracts.run_command_logged(command, probe_dir, timeout_seconds, started_at, child_cpu_workers)
    score = _lensde_asphere_contracts.first_existing_json([probe_dir / 'score.json', probe_dir / 'initial_score.json'], {})
    contributions = score.get('contributions', []) if isinstance(score, dict) else []
    rms_by_index = {int(row.get('index', 0) or 0): 1000.0 * _lensde_configuration.finite_float(row.get('value'), math.nan) for row in contributions if isinstance(row, dict) and str(row.get('name') or '').strip().lower() == 'rms_spot' and math.isfinite(_lensde_configuration.finite_float(row.get('value'), math.nan))} if isinstance(contributions, list) else {}
    dense_nodes = _lensde_fields.normalized_field_nodes(dense_system)
    probe_rows = [{'field_index': index, 'node': node, 'rms_spot_um': rms_by_index.get(index)} for index, node in enumerate(dense_nodes)]
    image_height_contract = _lensde_fields.chief_image_height_field_contract(dense_system, probe_dir / 'spot.csv', base_count, max_node=_lensde_commands.option_float_from(options, 'initial_search_post_asphere_main_field_max_node', 1.0, 0.0), include_max_node=_lensde_commands.option_truthy(options.get('initial_search_post_asphere_main_field_include_max_node'), True))
    if field_coordinate == 'chief_image_height' and image_height_contract.get('valid'):
        base_nodes = list(image_height_contract['selected_nodes'])
        base_system = _lensde_fields.resample_system_fields_for_asphere(system, base_nodes, reference_max_radius=field_reference_radius)
        field_coordinate_applied = 'chief_image_height'
        coordinate_fallback = None
    else:
        base_system = _lensde_fields.densify_system_fields_for_asphere(system, base_count, preserve_existing_indices=preserve_existing_indices)
        base_nodes = sorted(_lensde_fields.normalized_field_nodes(base_system))
        field_coordinate_applied = 'normalized_field'
        coordinate_fallback = image_height_contract.get('reason') if field_coordinate == 'chief_image_height' else None
    existing_nodes = sorted(base_nodes)
    selection = _lensde_fields.select_adaptive_post_asphere_field_nodes(existing_nodes, probe_rows, len(existing_nodes) + max_additions, _lensde_commands.option_float_from(options, 'initial_search_post_asphere_field_probe_min_excess_um', 0.05, 0.0), max_field_deg=_lensde_configuration.finite_float(base_system.get('max_field_deg'), 0.0), sample_coverage_deg=_lensde_commands.option_float_from(options, 'initial_search_post_asphere_field_sample_coverage_deg', 2.5, 0.0), fill_to_target=_lensde_commands.option_truthy(options.get('initial_search_post_asphere_adaptive_field_fill_to_target'), False))
    proposed_new_nodes = list(selection.get('new_nodes', []))
    applied_new_nodes: list[float] = []
    selected_system = base_system
    if contract_policy == 'adaptive_additions' and proposed_new_nodes:
        applied_new_nodes = proposed_new_nodes
        selected_system = _lensde_fields.resample_system_fields_for_asphere(system, sorted({*existing_nodes, *applied_new_nodes}), reference_max_radius=field_reference_radius)
    selected_nodes = sorted({*existing_nodes, *applied_new_nodes})
    if not proposed_new_nodes:
        selection['fallback'] = 'none_no_anomaly_above_threshold'
    audit = {'enabled': True, 'adaptive_anomaly_audit_enabled': adaptive_enabled, 'field_coordinate_requested': field_coordinate, 'field_coordinate_applied': field_coordinate_applied, 'coordinate_fallback': coordinate_fallback, 'chief_image_height_contract': image_height_contract, 'contract_policy': contract_policy, 'contract_locked': contract_policy == 'locked', 'replacement_timing': 'initial_contract_only', 'mid_optimization_field_replacement': False, 'base_field_count': len(existing_nodes), 'max_additions': max_additions, 'probe_field_count_requested': probe_count, 'probe_field_count_actual': len(dense_nodes), 'field_reference_radius': field_reference_radius, 'probe_metric': 'rms_spot', 'probe_result': result, 'trace_coverage': _lensde_configuration.finite_float(score.get('trace_coverage'), 0.0) if isinstance(score, dict) else 0.0, 'min_field_coverage': _lensde_configuration.finite_float(score.get('min_field_coverage'), 0.0) if isinstance(score, dict) else 0.0, 'curve': probe_rows, **selection, 'proposed_new_nodes': proposed_new_nodes, 'new_nodes': applied_new_nodes, 'selected_nodes': selected_nodes}
    _lensde_provenance.write_json(probe_dir / 'field_selection.json', audit)
    return (selected_system, audit)


def run_post_asphere_promotion_recheck(source_dir: Path, recheck_dir: Path, merit_path: Path, options: dict, exe: Path, timeout_seconds: int, started_at: float, child_cpu_workers: int=0) -> dict:
    source_system = _lensde_asphere_contracts.first_existing_json([source_dir / 'system.json', source_dir / 'checkpoint_system.json', source_dir / 'system_input.json', source_dir / 'input_system.json'], {})
    if not isinstance(source_system, dict) or not source_system:
        return {'source_dir': str(source_dir), 'merit': math.inf, 'error': 'current selected system not found'}
    selected_surfaces = _lensde_initialization.selected_post_asphere_surface_indices(source_system, options)
    if not selected_surfaces:
        return {'source_dir': str(source_dir), 'merit': math.inf, 'error': 'no selected post-asphere surfaces'}
    variables_payload = _lensde_asphere_contracts.first_existing_json([source_dir / 'variables.json'], {'variables': []})
    source_variables = variables_payload.get('variables', variables_payload) if isinstance(variables_payload, dict) else variables_payload
    if not isinstance(source_variables, list):
        source_variables = []
    merit = _lensde_provenance.read_json(merit_path)
    if not isinstance(merit, dict) or not merit:
        raise ValueError(f'post-asphere handoff merit not found: {merit_path}')
    calibrated_source_system = _lensde_configuration.system_with_ray_aiming(source_system, options)
    system = calibrated_source_system
    target_field_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_field_count', 9, 1)
    preserve_existing_indices = any((str(variable.get('kind', '')).lower() in {'vignette_x', 'vignette_y', 'fvcy', 'fvdy', 'vignette_center_y', 'vignette_shift_y', 'vignette_magnitude_y', 'vignette_asymmetry_y'} for variable in source_variables if isinstance(variable, dict)))
    preferred_nodes = _lensde_asphere_contracts.post_asphere_preferred_field_nodes(options)
    adaptive_field_probe: dict = {}
    vignetting_field_bridge: dict = {'performed': False, 'reason': 'vignetting_disabled', 'target_field_hy_interpolation': False}
    active_vignetting = _lensde_configuration.vignetting_optimization_enabled(merit)
    if preferred_nodes:
        field_reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, source_system)
        if active_vignetting:
            system, vignetting_field_bridge = _lensde_fields.aperture_realized_vignetting_field_bridge(calibrated_source_system, merit, preferred_nodes, native_io_paths.named_child(recheck_dir, 'vignetting_field_bridge'), variables=source_variables, reference_max_radius=field_reference_radius, recalibrate_existing_contract=True)
        else:
            system = _lensde_fields.resample_system_fields_for_asphere(calibrated_source_system, preferred_nodes, reference_max_radius=field_reference_radius)
        selected_nodes = _lensde_fields.normalized_field_nodes(system, reference_max_radius=field_reference_radius)
        source_nodes = _lensde_fields.normalized_field_nodes(source_system, reference_max_radius=field_reference_radius)
        adaptive_field_probe = {'enabled': _lensde_commands.option_truthy(options.get('initial_search_post_asphere_adaptive_fields_enabled'), True), 'shared_stage_contract': True, 'contract_policy': _lensde_fields.canonical_post_asphere_field_contract_policy(options.get('initial_search_post_asphere_field_contract_policy')), 'contract_locked': True, 'mid_optimization_field_replacement': False, 'field_reference_radius': field_reference_radius, 'selected_nodes': sorted(selected_nodes), 'new_nodes': [node for node in selected_nodes if not any((abs(node - existing) <= 1e-06 for existing in source_nodes))]}
    elif _lensde_fields.canonical_post_asphere_field_coordinate(options.get('initial_search_post_asphere_field_coordinate')) == 'chief_image_height' or (_lensde_commands.option_truthy(options.get('initial_search_post_asphere_adaptive_fields_enabled'), True) and _lensde_commands.option_int_from(options, 'initial_search_post_asphere_adaptive_field_max_additions', 0, 0) > 0):
        probe_system = copy.deepcopy(calibrated_source_system)
        for surface_index in selected_surfaces:
            if 0 <= surface_index < len(probe_system.get('surfaces', []) or []):
                probe_system['surfaces'][surface_index]['kind'] = 'even_asphere'
        probed_system, adaptive_field_probe = run_post_asphere_field_probe(probe_system, source_variables, merit_path, recheck_dir / 'field_probe', options, exe, timeout_seconds, started_at, child_cpu_workers)
        target_nodes = list(adaptive_field_probe.get('selected_nodes', []))
        if not target_nodes:
            target_nodes = sorted(_lensde_fields.normalized_field_nodes(probed_system))
        field_reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, calibrated_source_system)
        if active_vignetting:
            system, vignetting_field_bridge = _lensde_fields.aperture_realized_vignetting_field_bridge(calibrated_source_system, merit, target_nodes, native_io_paths.named_child(recheck_dir, 'vignetting_field_bridge'), variables=source_variables, reference_max_radius=field_reference_radius, recalibrate_existing_contract=True)
        else:
            system = _lensde_fields.resample_system_fields_for_asphere(calibrated_source_system, target_nodes, reference_max_radius=field_reference_radius)
    else:
        densified_system = _lensde_fields.densify_system_fields_for_asphere(calibrated_source_system, target_field_count, preserve_existing_indices=preserve_existing_indices)
        field_reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, calibrated_source_system)
        target_nodes = sorted(_lensde_fields.normalized_field_nodes(densified_system, reference_max_radius=field_reference_radius))
        if active_vignetting:
            system, vignetting_field_bridge = _lensde_fields.aperture_realized_vignetting_field_bridge(calibrated_source_system, merit, target_nodes, native_io_paths.named_child(recheck_dir, 'vignetting_field_bridge'), variables=source_variables, reference_max_radius=field_reference_radius, recalibrate_existing_contract=True)
        else:
            system = densified_system
        adaptive_field_probe = {'enabled': False, 'field_coordinate_applied': 'normalized_field', 'contract_policy': _lensde_fields.canonical_post_asphere_field_contract_policy(options.get('initial_search_post_asphere_field_contract_policy')), 'contract_locked': True, 'mid_optimization_field_replacement': False, 'selected_nodes': sorted(_lensde_fields.normalized_field_nodes(system)), 'new_nodes': []}
    calibrated_source_system = copy.deepcopy(system)
    calibrated_source_dir = recheck_dir / 'calibrated_source'
    calibrated_source_dir.mkdir(parents=True, exist_ok=True)
    calibrated_source_system_path = calibrated_source_dir / 'system.json'
    _lensde_provenance.write_json(calibrated_source_system_path, calibrated_source_system)
    _lensde_provenance.write_json(calibrated_source_dir / 'variables.json', {'variables': source_variables})
    _lensde_provenance.write_json(calibrated_source_dir / 'merit.json', merit)
    handoff_contract = {'contract': 'post_asphere_promotion_handoff_v2', 'source_dir': str(source_dir), 'target_nodes': sorted(_lensde_fields.normalized_field_nodes(calibrated_source_system, reference_max_radius=field_reference_radius)), 'vignetting_active': active_vignetting, 'vignetting_calibrated_before_asphere_conversion': active_vignetting, 'target_field_hy_interpolation': False if active_vignetting else None, 'representation_conversion_after_vignetting_calibration': True, 'reusable_for_candidate_preparation': True, 'calibrated_source_system_path': str(calibrated_source_system_path)}
    _lensde_provenance.write_json(recheck_dir / 'promotion_handoff_contract.json', handoff_contract)
    _lensde_provenance.write_json(recheck_dir / 'vignetting_field_bridge_report.json', vignetting_field_bridge)
    system = copy.deepcopy(calibrated_source_system)
    for surface_index in selected_surfaces:
        if 0 <= surface_index < len(system.get('surfaces', []) or []):
            system['surfaces'][surface_index]['kind'] = 'even_asphere'
    variables = _lensde_initialization.append_post_asphere_variables(system, source_variables, options)
    system_path = recheck_dir / 'system_input.json'
    variables_path = recheck_dir / 'variables.json'
    _lensde_provenance.write_json(system_path, system)
    _lensde_provenance.write_json(variables_path, {'variables': variables})
    command = _lensde_asphere_contracts.post_asphere_stage_base_args(exe, system_path, merit_path, variables_path, options, dynamic_apertures=True)
    command.extend(['--out-dir', str(recheck_dir)])
    result = _lensde_asphere_contracts.run_command_logged(command, recheck_dir, timeout_seconds, started_at, child_cpu_workers)
    if result.get('returncode') == 0:
        scored_system = _lensde_provenance.read_json(recheck_dir / 'system.json')
        scored_system, variables, _, closure_report = post_asphere_handoff.close(sys.modules[__name__], scored_system, variables, merit, options, native_io_paths.named_child(recheck_dir, 'vignetting_closure'), exe, timeout_seconds, started_at, child_cpu_workers, baseline=_lensde_provenance.read_json(recheck_dir / 'score.json'))
        handoff_contract['vignetting_closure'] = closure_report
        if closure_report.get('accepted'):
            post_asphere_handoff.publish(sys.modules[__name__], closure_report, recheck_dir)
            result['pre_closure_merit'] = result.get('merit')
            result['merit'] = _lensde_provenance.read_json(recheck_dir / 'score.json')['merit']
            _lensde_provenance.write_json(variables_path, {'variables': variables})
            calibrated_source_system['fields'] = copy.deepcopy(scored_system['fields'])
            for original, scored in zip(calibrated_source_system['surfaces'], scored_system['surfaces'], strict=True):
                for key in ('semi_aperture', 'mechanical_aperture'):
                    if key in scored:
                        original[key] = scored[key]
            _lensde_provenance.write_json(calibrated_source_system_path, calibrated_source_system)
        _lensde_provenance.write_json(recheck_dir / 'promotion_handoff_contract.json', handoff_contract)
    result['source_dir'] = str(source_dir)
    result['result_dir'] = str(recheck_dir)
    result['selected_surface_indices'] = selected_surfaces
    result['adaptive_field_probe'] = adaptive_field_probe
    result['vignetting_field_bridge'] = vignetting_field_bridge
    result['handoff_contract'] = handoff_contract
    result['calibrated_source_dir'] = str(calibrated_source_dir)
    result['calibrated_source_system_path'] = str(calibrated_source_system_path)
    source_score = _lensde_asphere_contracts.first_existing_json([source_dir / 'score.json', source_dir / 'checkpoint_score.json', source_dir / 'initial_score.json'], {})
    common_score = _lensde_asphere_contracts.first_existing_json([recheck_dir / 'score.json', recheck_dir / 'initial_score.json'], {})
    result['rms_image_quality'] = _lensde_asphere_contracts.post_asphere_rms_image_quality(common_score)
    result['common_rms_image_quality'] = result['rms_image_quality']['value']
    result['handoff_image_quality'] = _lensde_asphere_contracts.post_asphere_handoff_image_quality(common_score, min_coverage=_lensde_asphere_contracts.post_asphere_trace_coverage_contract(options)['required_min_trace_coverage'])
    result['common_ungated_image_penalty'] = result['handoff_image_quality']['value']
    for prefix, score in [('source', source_score), ('common', common_score)]:
        if not isinstance(score, dict):
            continue
        result[f'{prefix}_merit'] = _lensde_configuration.finite_float(score.get('merit'), math.inf)
        result[f'{prefix}_image_penalty'] = _lensde_configuration.finite_float(score.get('image_penalty'), math.inf)
        result[f'{prefix}_mean_spot_um'] = 1000.0 * _lensde_configuration.finite_float(score.get('mean_spot'), math.inf)
        result[f'{prefix}_max_spot_um'] = 1000.0 * _lensde_configuration.finite_float(score.get('max_spot'), math.inf)
        result[f'{prefix}_mean_wavefront_rms'] = _lensde_configuration.finite_float(score.get('mean_wavefront_rms'), math.inf)
        result[f'{prefix}_min_edge'] = _lensde_configuration.finite_float(score.get('min_edge'), -math.inf)
        result[f'{prefix}_trace_coverage'] = _lensde_configuration.finite_float(score.get('trace_coverage'), 0.0)
    return result


def run_post_asphere_multi_island_wave(label: str, batch_root: Path, jobs: list[dict], options: dict, timeout_seconds: int, started_at: float, child_cpu_workers: int) -> list[dict]:
    if len(jobs) < 2:
        raise ValueError('post-asphere multi-island wave requires at least two jobs')
    multi_exe = state.CPP_ROOT / 'build' / 'lensde_multi_island.exe'
    if not multi_exe.is_file():
        raise FileNotFoundError(f'multi-island executable not found: {multi_exe}')
    batch_root.mkdir(parents=True, exist_ok=True)
    prepared_jobs: list[tuple[dict, dict]] = []
    columns = ['island_id', 'system', 'variables', 'merit', 'population_file', 'snapshot_dir', 'output_dir', 'seed', 'population', 'generations', 'profile', 'constraint_handling', 'allow_near_trace_infeasible', 'trace_coverage_epsilon']
    lines = ['\t'.join(columns)]
    trace_contract = _lensde_asphere_contracts.post_asphere_trace_coverage_contract(options)
    constraint_handling = str(options.get('initial_search_post_asphere_constraint_handling') or 'feasibility_rules')
    for island_id, job in enumerate(jobs):
        stage_dir = Path(str(job['stage_dir']))
        prepared = _lensde_asphere_contracts.post_asphere_multistart_write_input(stage_dir / 'prepared_input', job['system'], job['variables'], job['merit'], job['population'], options)
        prepared_jobs.append((job, prepared))
        lines.append('\t'.join((str(value) for value in (island_id, prepared['system_path'], prepared['variables_path'], prepared['merit_path'], prepared['seed_path'], stage_dir / 'snapshots', stage_dir, job['de_seed'], len(job['population']), job['epoch_generations'], job['profile'], constraint_handling, '1' if trace_contract['allow_near_trace_infeasible'] else '0', f"{trace_contract['trace_coverage_epsilon']:.17g}"))))
    jobs_path = batch_root / 'jobs.tsv'
    jobs_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    root_policy = _lensde_asphere_contracts.post_asphere_root_scoring_policy(options)
    coalesce_us = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_multistart_multi_island_coalesce_us', 1000, 0)
    max_batch_rows = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_multistart_multi_island_max_batch_rows', 4096, 1)
    total_wave_rows = sum((len(job.get('population') or []) for job in jobs))
    large_direction_wave = bool(label == 'direction_scout_batch' and root_policy == 'confirmed_with_fallback' and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_multistart_multi_island_large_batch_auto_tune'), True) and (2048 <= total_wave_rows <= 4608))
    if large_direction_wave:
        max_batch_rows = total_wave_rows
        coalesce_us = max(coalesce_us, 500000)
    elif label == 'direction_scout_batch' and root_policy == 'unified_certified' and (len(jobs) == 5) and all((len(job.get('population') or []) == 565 for job in jobs)) and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_multistart_multi_island_large_batch_auto_tune'), True):
        max_batch_rows = total_wave_rows
        coalesce_us = max(coalesce_us, 100000)
    command = [str(multi_exe), '--jobs', str(jobs_path), '--batched', '--asphere-root-policy', root_policy, '--calibrated-builder-schedule', 'fused', '--transpose-ray-tasks', 'off', '--coalesce-us', str(coalesce_us), '--max-batch-rows', str(max_batch_rows), '--broker-stats', str(batch_root / 'broker_stats.json')]
    wave_result = _lensde_asphere_contracts.run_command_logged(command, batch_root, timeout_seconds, started_at, child_cpu_workers, env_overrides={'OPTILAND_DE_CUDA_REQUIRE': '1'})
    if int(wave_result.get('returncode', -1) or 0) != 0 or _lensde_commands.option_truthy(wave_result.get('timed_out'), False):
        raise RuntimeError(f"post-asphere multi-island wave failed: {label}; returncode={wave_result.get('returncode')}, timed_out={wave_result.get('timed_out')}")
    summaries: list[dict] = []
    for job, prepared in prepared_jobs:
        stage_dir = Path(str(job['stage_dir']))
        error_path = stage_dir / 'error.txt'
        if error_path.is_file():
            raise RuntimeError(error_path.read_text(encoding='utf-8', errors='replace'))
        for source, target_name in [(prepared['system_path'], 'system_input.json'), (prepared['variables_path'], 'variables.json'), (prepared['merit_path'], 'merit.json'), (prepared['seed_path'], 'initial_population.csv')]:
            shutil.copyfile(source, stage_dir / target_name)
        _system, _variables, output_population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(stage_dir)
        trace = _lensde_asphere_contracts.post_asphere_adaptive_trace(stage_dir)
        result_doc = _lensde_asphere_contracts.first_existing_json([stage_dir / 'result.json'], {})
        summary = {**wave_result, 'label': job['label'], 'stage_dir': str(stage_dir), 'population_origin': job['population_origin'], 'population_scale': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_scale', 0.35, 0.0) if str(job['population_origin']).startswith('fresh_') else None, 'exact_population_carry': job['population_origin'] == 'exact_population_carry', 'scheduled_epochs': job['epochs'], 'scheduled_generations': job['epochs'] * job['epoch_generations'], 'actual_generations': trace['actual_generations'], 'requested_population': len(job['population']), 'effective_population': trace['effective_population'], 'output_population': len(output_population), 'de_seed': job['de_seed'], 'trace': trace, 'multi_island': True, 'multi_island_profile': job['profile'], 'aiming_mode': _lensde_asphere_contracts.post_asphere_aiming_contract(options), 'trace_coverage_contract': copy.deepcopy(trace_contract), 'constraint_handling': constraint_handling, 'island_result': result_doc, 'batch_root': str(batch_root)}
        _lensde_provenance.write_json(stage_dir / 'stage_summary.json', summary)
        summaries.append(summary)
    return summaries


def run_post_asphere_multistart_de_stage(label: str, stage_dir: Path, system: dict, variables: list[dict], merit: dict, population_rows: list[list[float]], options: dict, de_exe: Path, de_use_cuda: bool, epochs: int, de_seed: int, population_origin: str, timeout_seconds: int, started_at: float, child_cpu_workers: int, *, glass_search_path: Path | None=None, glass_search_config: dict | None=None, checkpoint_snapshot_dir: Path | None=None, progress_every: int | None=None, epoch_generations: int | None=None, early_stop: bool | None=None) -> dict:
    if epochs <= 0:
        raise ValueError('post-asphere multistart DE stage requires at least one epoch')
    schedule = _lensde_asphere_contracts.post_asphere_multistart_schedule(options)
    prepared = _lensde_asphere_contracts.post_asphere_multistart_write_input(stage_dir / 'prepared_input', system, variables, merit, population_rows, options)
    stage_options = copy.deepcopy(options)
    stage_generations = max(1, int(epoch_generations)) if epoch_generations is not None else schedule['epoch_generations']
    stage_options.update({'initial_search_post_asphere_population': len(population_rows), 'initial_search_post_asphere_generations': stage_generations, 'initial_search_post_asphere_outer': epochs, 'initial_search_post_asphere_seed': de_seed, 'initial_search_post_asphere_early_stop': _lensde_commands.option_truthy(options.get('initial_search_post_asphere_early_stop'), True)})
    if early_stop is not None:
        stage_options['initial_search_post_asphere_early_stop'] = bool(early_stop)
    command = _lensde_asphere_contracts.post_asphere_de_command(de_exe, prepared, stage_dir, stage_options, 0, use_cuda_evaluator=de_use_cuda, glass_search_path=glass_search_path, glass_search_config=glass_search_config)
    if progress_every is not None and int(progress_every) > 0:
        try:
            progress_index = command.index('--progress-every')
        except ValueError:
            command.extend(['--progress-every', str(int(progress_every))])
        else:
            if progress_index + 1 < len(command):
                command[progress_index + 1] = str(int(progress_every))
            else:
                command.append(str(int(progress_every)))
    env_overrides: dict[str, str] = {}
    if checkpoint_snapshot_dir is not None:
        checkpoint_snapshot_dir.mkdir(parents=True, exist_ok=True)
        env_overrides['OPTILAND_CHECKPOINT_SNAPSHOT_DIR'] = str(checkpoint_snapshot_dir.resolve())
    result = _lensde_asphere_contracts.run_command_logged(command, stage_dir, timeout_seconds, started_at, child_cpu_workers, env_overrides=env_overrides)
    if int(result.get('returncode', -1) or 0) != 0 or _lensde_commands.option_truthy(result.get('timed_out'), False):
        raise RuntimeError(f"post-asphere multistart DE failed: {label}; returncode={result.get('returncode')}, timed_out={result.get('timed_out')}")
    _system, _variables, output_population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(stage_dir)
    trace = _lensde_asphere_contracts.post_asphere_adaptive_trace(stage_dir)
    summary = {**result, 'label': label, 'stage_dir': str(stage_dir), 'population_origin': population_origin, 'population_scale': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_scale', 0.35, 0.0) if population_origin.startswith('fresh_') else None, 'exact_population_carry': population_origin == 'exact_population_carry', 'scheduled_epochs': epochs, 'scheduled_generations': epochs * stage_generations, 'actual_generations': trace['actual_generations'], 'requested_population': len(population_rows), 'effective_population': trace['effective_population'], 'output_population': len(output_population), 'de_seed': de_seed, 'aiming_mode': _lensde_asphere_contracts.post_asphere_aiming_contract(options), 'trace_coverage_contract': _lensde_asphere_contracts.post_asphere_trace_coverage_contract(options), 'constraint_handling': str(options.get('initial_search_post_asphere_constraint_handling') or 'feasibility_rules'), 'trace': trace}
    if checkpoint_snapshot_dir is not None:
        summary['checkpoint_snapshot_dir'] = str(checkpoint_snapshot_dir.resolve())
        summary['checkpoint_progress_every'] = int(progress_every or 0)
    _lensde_provenance.write_json(stage_dir / 'stage_summary.json', summary)
    return summary


def run_post_asphere_multistart_audit(label: str, source_dir: Path, audit_root: Path, merit_path: Path, options: dict, cpu_exe: Path, timeout_seconds: int, started_at: float, child_cpu_workers: int, passes: int) -> dict:
    source_system = _lensde_asphere_contracts.first_existing_json([source_dir / 'system.json', source_dir / 'checkpoint_system.json'], {})
    source_fields = source_system.get('fields', []) if isinstance(source_system, dict) else []
    configured_fields = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_field_count', 9, 1)
    preferred_fields = len(_lensde_asphere_contracts.post_asphere_preferred_field_nodes(options))
    source_field_count = len(source_fields) if isinstance(source_fields, list) else 0
    expected_fields = preferred_fields if preferred_fields > 0 else max(source_field_count, configured_fields)
    min_coverage = min(1.0, _lensde_commands.option_float_from(options, 'initial_search_post_asphere_multistart_audit_min_coverage', 0.97, 0.0))
    target_mean = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_multistart_target_mean_rms_um', 2.5, 0.0)
    records: list[dict] = []
    current_dir = source_dir
    for pass_index in range(1, max(1, int(passes)) + 1):
        pass_dir = audit_root / f'pass_{pass_index:02d}'
        result = run_post_asphere_promotion_recheck(current_dir, pass_dir, merit_path, options, cpu_exe, timeout_seconds, started_at, child_cpu_workers)
        score = _lensde_asphere_contracts.post_asphere_score_summary(pass_dir, expected_fields, min_coverage, target_mean, _lensde_defaults.canonical_image_quality_metric(options.get('initial_search_post_asphere_rms_mode'), 'rms_spot_wavefront'))
        records.append({**result, 'pass': pass_index, 'score': score})
        if not score['strict_valid']:
            break
        current_dir = pass_dir
    score = records[-1]['score'] if records else {'merit': math.inf, 'mean_rms_um': math.inf, 'max_rms_um': math.inf, 'strict_valid': False}
    stable = bool(score.get('strict_valid', False))
    if len(records) >= 2:
        previous = records[-2]['score']
        stable = stable and bool(abs(score['mean_rms_um'] - previous['mean_rms_um']) <= 1e-06 and abs(score['max_rms_um'] - previous['max_rms_um']) <= 1e-06 and (abs(score['merit'] - previous['merit']) <= 1e-08))
    summary = {'label': label, 'source_dir': str(source_dir), 'result_dir': str(current_dir), 'passes': records, 'stable': stable, 'score': score}
    _lensde_provenance.write_json(audit_root / 'audit_summary.json', summary)
    return summary


def run_post_asphere_multistart_candidate(source_dir: Path, candidate_root: Path, options: dict, cpu_exe: Path, de_exe: Path, de_use_cuda: bool, fallback_merit: dict, timeout_seconds: int, started_at: float, seed_offset: int, common_merit: dict | None=None, protected_baseline: dict | None=None, child_cpu_workers: int=0) -> dict:
    flow_started = time.time()
    schedule = _lensde_asphere_contracts.post_asphere_multistart_schedule(options)

    def timeout_context() -> tuple[int, float]:
        limits: list[float] = []
        candidate_timeout = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_timeout', 0, 0)
        if candidate_timeout > 0:
            limits.append(candidate_timeout - (time.time() - flow_started))
        outer_remaining = _lensde_execution.remaining_timeout_seconds(timeout_seconds, started_at)
        if outer_remaining is not None:
            limits.append(outer_remaining)
        if not limits:
            return (0, time.time())
        allowed = min(limits)
        if allowed <= 0.0:
            raise TimeoutError('post-asphere candidate timeout exhausted')
        return (max(1, int(math.ceil(allowed))), time.time())
    prepared = _lensde_asphere_contracts.post_asphere_prepare_case(source_dir, candidate_root / 'input', options, fallback_merit, common_merit=common_merit, promotion_handoff=protected_baseline)
    merit = prepared['merit']
    merit_path = prepared['merit_path']
    variables = prepared['variables']
    system = prepared['system']
    initial_values = [_lensde_populations.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in variables]
    requested_population_size = _lensde_asphere_contracts.post_asphere_multistart_input_population_size(options, _lensde_populations.population_sizing_variable_count(variables))
    population_size = requested_population_size
    population_scale = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_scale', 0.35, 0.0)
    rng_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_rng_seed', 104729, 0)
    de_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_seed', 20260709, 0)
    audit_passes = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_multistart_audit_passes', 2, 1)
    baseline_dir = candidate_root / 'parent_baseline'
    baseline_result = copy.deepcopy(protected_baseline) if isinstance(protected_baseline, dict) else {}
    protected_text = str(baseline_result.get('result_dir') or '').strip()
    protected_dir = Path(protected_text) if protected_text else None
    if protected_dir is not None and protected_dir.is_dir():
        baseline_result['copied_files'] = _lensde_commands.copy_candidate_artifacts(protected_dir, baseline_dir)
        baseline_result['result_dir'] = str(baseline_dir)
        baseline_result['protected'] = True
    else:
        stage_timeout, stage_started = timeout_context()
        baseline_result = run_post_asphere_promotion_recheck(source_dir, baseline_dir, merit_path, options, cpu_exe, stage_timeout, stage_started, child_cpu_workers)
        baseline_result['protected'] = True
    manifest: dict = {'flow': 'conic_multistart_refine_like_restarts', 'status': 'running', 'source_dir': str(source_dir), 'schedule': schedule, 'selection_policy': 'strict_merit_improvement_only', 'long_restart_policy': 'refine_like_early_stop_then_reseed', 'stages': {}, 'decisions': [], 'multi_island_waves': []}
    _lensde_provenance.write_json(candidate_root / 'multistart_manifest.json', manifest)
    de_records: list[dict] = []

    def multi_island_reason(profile: str, specs: list[dict]) -> str | None:
        if not schedule['multi_island_enabled']:
            return 'disabled_by_option'
        if not de_use_cuda:
            return 'cuda_de_disabled'
        if len(specs) < 2:
            return 'fewer_than_two_islands'
        if not (state.CPP_ROOT / 'build' / 'lensde_multi_island.exe').is_file():
            return 'multi_island_executable_missing'
        expected_epochs = 1 if profile == 'post_asphere_scout' else 2
        if any((int(spec['epochs']) != expected_epochs for spec in specs)):
            return f'profile_requires_{expected_epochs}_epochs'
        if _lensde_commands.option_int_from(options, 'initial_search_post_asphere_hybrid_parents', 4, 1) != 4:
            return 'hybrid_parent_count_not_supported'
        if not math.isclose(_lensde_commands.option_float_from(options, 'initial_search_post_asphere_refresh', 0.05, 0.0), 0.05, rel_tol=0.0, abs_tol=1e-12):
            return 'refresh_fraction_not_supported'
        if str(options.get('initial_search_post_asphere_constraint_handling') or 'feasibility_rules') != 'feasibility_rules':
            return 'constraint_handling_not_supported'
        return None

    def run_wave(wave_label: str, specs: list[dict], profile: str, cumulative_before: dict[str, int]) -> list[dict]:
        if not specs:
            return []
        reason = multi_island_reason(profile, specs)
        de_results: list[dict] = []
        attempted = reason is None
        fallback_error = ''
        if attempted:
            try:
                stage_timeout, stage_started = timeout_context()
                de_results = run_post_asphere_multi_island_wave(wave_label, candidate_root / 'multi_island' / wave_label, specs, options, stage_timeout, stage_started, child_cpu_workers)
            except Exception as exc:
                fallback_error = str(exc)
                reason = 'multi_island_execution_failed'
                de_results = []
        if not de_results:
            for spec in specs:
                stage_timeout, stage_started = timeout_context()
                de_results.append(run_post_asphere_multistart_de_stage(spec['label'], Path(str(spec['stage_dir'])), spec['system'], spec['variables'], spec['merit'], spec['population'], options, de_exe, de_use_cuda, spec['epochs'], spec['de_seed'], spec['population_origin'], stage_timeout, stage_started, child_cpu_workers, epoch_generations=spec['epoch_generations'], early_stop=False))
        manifest['multi_island_waves'].append({'wave': wave_label, 'requested': schedule['multi_island_enabled'], 'attempted': attempted, 'used': bool(de_results and de_results[0].get('multi_island')), 'fallback_reason': reason, 'fallback_error': fallback_error, 'island_count': len(specs)})
        results: list[dict] = []
        for spec, de_result in zip(specs, de_results, strict=True):
            de_records.append(de_result)
            audit_timeout, audit_started = timeout_context()
            storage_key = str(spec.get('storage_key') or spec['label'])
            audit = run_post_asphere_multistart_audit(spec['label'], Path(str(spec['stage_dir'])), candidate_root / 'audits' / storage_key, merit_path, options, cpu_exe, audit_timeout, audit_started, child_cpu_workers, audit_passes)
            candidate = {'label': spec['label'], 'storage_key': storage_key, 'stage_dir': str(spec['stage_dir']), 'de': de_result, 'audit': audit, 'cumulative_generations': cumulative_before.get(spec['label'], 0) + spec['epochs'] * spec['epoch_generations']}
            manifest['stages'][spec['label']] = _lensde_asphere_contracts.compact_post_asphere_multistart_candidate(candidate)
            results.append(candidate)
        _lensde_provenance.write_json(candidate_root / 'multistart_manifest.json', manifest)
        return results

    def calibrated_handoffs_from_precalibration_ranking(ranked: list[dict], count: int, role: str, stage_options: dict) -> tuple[list[dict], list[dict]]:
        selected: list[dict] = []
        failures: list[dict] = []
        for raw_candidate in ranked:
            if len(selected) >= max(1, count):
                break
            candidate = dict(raw_candidate)
            if not _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, prepared['merit']):
                selected.append(candidate)
                continue
            existing_boundary = candidate.get('stage_boundary_vignetting')
            if role == 'top_to_long' and isinstance(existing_boundary, dict) and _lensde_commands.option_truthy(existing_boundary.get('passed'), False):
                selected.append(candidate)
                continue
            raw_stage_dir = Path(str(candidate['stage_dir']))
            storage_key = str(candidate.get('storage_key') or candidate.get('label') or 'candidate')
            boundary_dir = native_io_paths.named_child(candidate_root, 'stage_boundaries') / role / storage_key
            try:
                boundary_timeout, boundary_started = timeout_context()
                boundary = _lensde_asphere_contracts.recalibrate_post_asphere_stage_handoff(raw_stage_dir, boundary_dir, merit_path, stage_options, cpu_exe, boundary_timeout, boundary_started, child_cpu_workers)
                candidate['optimization_stage_dir'] = str(raw_stage_dir)
                candidate['stage_dir'] = str(boundary_dir)
                candidate['pre_vignetting_calibration_merit'] = _lensde_configuration.finite_float(candidate.get('audit', {}).get('score', {}).get('merit'), math.inf)
                candidate['stage_boundary_vignetting'] = boundary['report']
                selected.append(candidate)
            except Exception as error:
                failures.append({'label': candidate.get('label'), 'optimization_stage_dir': str(raw_stage_dir), 'error': str(error)})
        if len(selected) < max(1, count):
            raise RuntimeError(f'{role} produced {len(selected)}/{max(1, count)} strict vignetting handoffs; failures={failures}')
        return (selected, failures)
    scout_specs: list[dict] = []
    for scout_index in range(1, schedule['scout_count'] + 1):
        label = f'scout_{scout_index:02d}'
        population_seed = seed_protocol.post_asphere_seed(options, 'scout_population', (seed_offset, scout_index), (rng_seed + seed_offset + scout_index * 1000 + 101) % 2000000000)
        scout_specs.append({'label': label, 'stage_dir': native_io_paths.named_child(candidate_root / 'scouts', label), 'system': system, 'variables': variables, 'merit': merit, 'population': _lensde_initialization.make_post_asphere_population(system, variables, initial_values, population_size, population_seed, population_scale), 'de_seed': seed_protocol.post_asphere_seed(options, 'scout_optimizer', (seed_offset, scout_index), (de_seed + seed_offset + scout_index * 1000 + 501) % 2000000000), 'population_origin': 'fresh_conic_population', 'epochs': schedule['scout']['epochs'], 'epoch_generations': schedule['epoch_generations'], 'profile': 'post_asphere_scout'})
    scouts = run_wave('scout_batch', scout_specs, 'post_asphere_scout', {})
    ranked_scouts = sorted(scouts, key=lambda candidate: _lensde_asphere_contracts.post_asphere_multistart_ranking_key(candidate, _lensde_defaults.canonical_image_quality_metric(merit.get('image_quality_metric'), 'rms_spot_wavefront')))
    selected_scouts, scout_boundary_failures = calibrated_handoffs_from_precalibration_ranking(ranked_scouts, schedule['top_count'], 'scout_to_top', options)
    manifest['decisions'].append({'stage': 'scout_to_top', 'ranking': [_lensde_asphere_contracts.compact_post_asphere_multistart_candidate(row) for row in ranked_scouts], 'selected': [row['label'] for row in selected_scouts], 'selection_basis': 'pre_vignetting_calibration_ranking', 'stage_boundary_failures': scout_boundary_failures})
    if schedule['topup']['epochs'] <= 0:
        continued = selected_scouts
    else:
        top_specs: list[dict] = []
        cumulative_before: dict[str, int] = {}
        for branch_index, parent in enumerate(selected_scouts, start=1):
            label = f"top_{branch_index:02d}_{parent['label']}_plus{schedule['topup']['generations']}"
            parent_system, parent_variables, parent_population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(Path(str(parent['stage_dir'])))
            top_specs.append({'label': label, 'storage_key': _lensde_asphere_contracts.post_asphere_top_storage_key(branch_index), 'stage_dir': candidate_root / 'top' / _lensde_asphere_contracts.post_asphere_top_storage_key(branch_index), 'parent_label': parent['label'], 'system': parent_system, 'variables': parent_variables, 'merit': merit, 'population': parent_population, 'de_seed': (de_seed + seed_offset + 20000 + branch_index) % 2000000000, 'population_origin': 'exact_population_carry', 'epochs': schedule['topup']['epochs'], 'epoch_generations': schedule['epoch_generations'], 'profile': 'post_asphere_topup'})
            cumulative_before[label] = int(parent['cumulative_generations'])
        continued = run_wave('top_batch', top_specs, 'post_asphere_topup', cumulative_before)
    ranked_top = sorted(continued, key=lambda candidate: _lensde_configuration.finite_float(candidate.get('audit', {}).get('score', {}).get('merit'), math.inf))
    selected_top, top_boundary_failures = calibrated_handoffs_from_precalibration_ranking(ranked_top, 1, 'top_to_long', options)
    incumbent = selected_top[0]
    incumbent_merit = _lensde_configuration.finite_float(incumbent['audit']['score'].get('merit'), math.inf)
    manifest['decisions'].append({'stage': 'top_to_long', 'ranking': [{'label': row['label'], 'merit': row['audit']['score'].get('merit')} for row in ranked_top], 'selected': incumbent['label'], 'metric': 'merit', 'selection_basis': 'pre_vignetting_calibration_merit', 'stage_boundary_failures': top_boundary_failures})
    archive_dirs = [Path(str(row['stage_dir'])) for row in [*scouts, *continued]]
    long_rounds: list[dict] = []
    long_config = schedule['long']
    long_stage_options = copy.deepcopy(options)
    long_stage_options['initial_search_post_asphere_hybrid_parents'] = long_config['hybrid_parents']
    long_stage_options['initial_search_post_asphere_refresh'] = long_config['refresh']
    donor_fraction = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_long_donor_fraction', 0.05, 0.0)
    long_scales = _lensde_asphere_contracts.post_asphere_long_population_scales(options)
    for round_index in range(1, long_config['rounds'] + 1):
        round_stage_options = copy.deepcopy(long_stage_options)
        round_stage_options['initial_search_post_asphere_early_stop_use_min_improvement_threshold'] = _lensde_asphere_contracts.post_asphere_long_uses_relaxed_early_stop(options, round_index, long_config['rounds'])
        incumbent_system, _old_variables, _old_population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(Path(str(incumbent['stage_dir'])))
        restart_variables = _lensde_commands.sync_variables_from_system(copy.deepcopy(variables), incumbent_system, rebase_asphere=True)
        restart_population, seed_summary = _lensde_asphere_population.make_post_asphere_long_population(incumbent_system, restart_variables, archive_dirs, population_size, rng_seed + seed_offset + 50000 + round_index * 1009, donor_fraction, long_scales)
        label = f'long_round_{round_index:02d}'
        stage_dir = native_io_paths.named_child(candidate_root / 'long', label)
        stage_timeout, stage_started = timeout_context()
        de_result = run_post_asphere_multistart_de_stage(label, stage_dir, incumbent_system, restart_variables, merit, restart_population, round_stage_options, de_exe, de_use_cuda, long_config['outer_epochs'], (de_seed + seed_offset + 30000 + round_index) % 2000000000, 'fresh_refine_like_reseed', stage_timeout, stage_started, child_cpu_workers, epoch_generations=long_config['de_generations'], early_stop=True)
        de_records.append(de_result)
        audit_timeout, audit_started = timeout_context()
        audit = run_post_asphere_multistart_audit(label, stage_dir, native_io_paths.named_child(candidate_root / 'audits', label), merit_path, options, cpu_exe, audit_timeout, audit_started, child_cpu_workers, 1)
        child_merit = _lensde_configuration.finite_float(audit['score'].get('merit'), math.inf)
        accepted = _lensde_asphere_contracts.is_strict_merit_improvement(child_merit, incumbent_merit)
        candidate_stage_dir = stage_dir
        stage_boundary_vignetting: dict = {'performed': False, 'reason': 'stage_not_accepted'}
        if accepted and _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, prepared['merit']):
            boundary_dir = native_io_paths.named_child(native_io_paths.named_child(candidate_root, 'stage_boundaries') / 'long_round_to_next', label)
            try:
                boundary_timeout, boundary_started = timeout_context()
                boundary = _lensde_asphere_contracts.recalibrate_post_asphere_stage_handoff(stage_dir, boundary_dir, merit_path, round_stage_options, cpu_exe, boundary_timeout, boundary_started, child_cpu_workers)
                candidate_stage_dir = boundary_dir
                stage_boundary_vignetting = boundary['report']
            except Exception as error:
                accepted = False
                stage_boundary_vignetting = {'performed': False, 'error': str(error), 'candidate_rejected': True}
        candidate = {'label': label, 'stage_dir': str(candidate_stage_dir), 'optimization_stage_dir': str(stage_dir), 'de': de_result, 'audit': audit, 'cumulative_generations': int(incumbent.get('cumulative_generations', 0)) + int(de_result.get('actual_generations', 0) or 0), 'seed_population': seed_summary, 'incumbent_merit_before': incumbent_merit, 'child_merit': child_merit, 'pre_vignetting_calibration_merit': child_merit, 'accepted': accepted, 'stage_boundary_vignetting': stage_boundary_vignetting, 'early_stop_threshold_policy': 'min_relative_or_absolute' if round_stage_options['initial_search_post_asphere_early_stop_use_min_improvement_threshold'] else 'max_relative_or_absolute'}
        long_rounds.append(candidate)
        archive_dirs.append(stage_dir)
        manifest['stages'][label] = _lensde_asphere_contracts.compact_post_asphere_multistart_candidate(candidate) | {'seed_population': seed_summary, 'incumbent_merit_before': candidate['incumbent_merit_before'], 'child_merit': child_merit, 'pre_vignetting_calibration_merit': child_merit, 'accepted': accepted, 'stage_boundary_vignetting': stage_boundary_vignetting, 'early_stop_threshold_policy': candidate['early_stop_threshold_policy']}
        manifest['decisions'].append({'stage': label, 'rule': 'child_merit < incumbent_merit', 'incumbent_merit': incumbent_merit, 'child_merit': child_merit, 'accepted': accepted, 'actual_generations': de_result.get('actual_generations'), 'early_stop_threshold_policy': candidate['early_stop_threshold_policy']})
        if accepted:
            incumbent = candidate
            incumbent_merit = child_merit
        _lensde_provenance.write_json(candidate_root / 'multistart_manifest.json', manifest)
    baseline_merit = _lensde_configuration.finite_float(baseline_result.get('merit'), math.inf)
    protected_parent_retained = not _lensde_asphere_contracts.is_strict_merit_improvement(incumbent_merit, baseline_merit)
    best_stage_dir = baseline_dir if protected_parent_retained else Path(str(incumbent['stage_dir']))
    best_merit = baseline_merit if protected_parent_retained else incumbent_merit
    incumbent_score = incumbent.get('audit', {}).get('score', {})
    selected_mean_rms_um = _lensde_configuration.finite_float(baseline_result.get('common_mean_spot_um'), math.inf) if protected_parent_retained else _lensde_configuration.finite_float(incumbent_score.get('mean_rms_um'), math.inf)
    selected_max_rms_um = _lensde_configuration.finite_float(baseline_result.get('common_max_spot_um'), math.inf) if protected_parent_retained else _lensde_configuration.finite_float(incumbent_score.get('max_rms_um'), math.inf)
    selected_dir = candidate_root / 'best_result'
    copied = _lensde_commands.copy_candidate_artifacts(best_stage_dir, selected_dir)
    manifest.update({'status': 'complete', 'wall_seconds': time.time() - flow_started, 'long_rounds_requested': long_config['rounds'], 'long_rounds_completed': len(long_rounds), 'long_rounds_accepted': sum((1 for row in long_rounds if row['accepted'])), 'selected_candidate': incumbent['label'], 'selected_merit': incumbent_merit, 'protected_parent_retained': protected_parent_retained})
    _lensde_provenance.write_json(candidate_root / 'multistart_manifest.json', manifest)
    summary = {'flow': 'conic_multistart_refine_like_restarts', 'source_dir': str(source_dir), 'candidate_root': str(candidate_root), 'selected_result_dir': str(selected_dir), 'best_stage_dir': str(best_stage_dir), 'best_merit': best_merit, 'best_mean_rms_um': selected_mean_rms_um, 'best_max_rms_um': selected_max_rms_um, 'protected_parent_retained': protected_parent_retained, 'copied_files': copied, 'prepared': {'selected_surface_indices': prepared['selected_surface_indices'], 'lens_groups': prepared['lens_groups'], 'population_rows': population_size, 'requested_population_rows': requested_population_size, 'variable_count': len(variables), 'population_scale': population_scale, 'zero_asphere_coefficients': prepared.get('zero_asphere_coefficients', False)}, 'evaluators': {'de': 'CUDA' if de_use_cuda else 'CPU', 'de_executable': str(de_exe), 'selection_and_recheck': 'CPU merit only', 'cpu_executable': str(cpu_exe)}, 'parent_baseline': baseline_result, 'de': incumbent.get('de', {}), 'long_rounds': long_rounds, 'dls_restarts': [], 'multistart': manifest}
    _lensde_provenance.write_json(candidate_root / 'summary.json', summary)
    return summary


def run_post_asphere_dual_contract_single_mother_candidate(source_dir: Path, candidate_root: Path, options: dict, cpu_exe: Path, de_exe: Path, de_use_cuda: bool, fallback_merit: dict, timeout_seconds: int, started_at: float, seed_offset: int, common_merit: dict | None=None, protected_baseline: dict | None=None, child_cpu_workers: int=0, *, stop_before_scout: bool=False) -> dict:
    """Run fixed-10 Formation followed by one periodic refresh long run."""
    flow_started = time.time()
    scout_initialization = str(options.get('initial_search_post_asphere_direction_scout_initialization') or 'legacy_all_variable_local_perturbation_A').strip().lower()
    curvature_normalized_scouts = scout_initialization in {'graded_all_variable_local_perturbation_curvature_bound_normalized', 'curvature_normalized_graded'} or _lensde_asphere_contracts.canonical_post_asphere_flow(options.get('initial_search_post_asphere_flow')) == 'curvature_normalized_formation_periodic'
    direction_scout_generator = 'graded_all_variable_local_perturbation_curvature_bound_normalized' if curvature_normalized_scouts else 'legacy_all_variable_local_perturbation_A'
    flow_name = 'curvature_normalized_graded_scout_formation_periodic_top_half_correlated_sag_refresh' if curvature_normalized_scouts else 'direction_scout_formation_periodic_top_half_correlated_sag_refresh'
    reuse_scout_root_text = str(options.get('initial_search_post_asphere_reuse_existing_scout_root') or '').strip()
    reuse_scout_root = Path(reuse_scout_root_text).resolve() if reuse_scout_root_text else None
    reuse_existing_scouts = bool(reuse_scout_root is not None and reuse_scout_root.is_dir())
    if reuse_existing_scouts:
        native_io_paths.enable_compact_names(candidate_root)

    def timeout_context() -> tuple[int, float]:
        limits: list[float] = []
        candidate_timeout = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_timeout', 0, 0)
        if candidate_timeout > 0:
            limits.append(candidate_timeout - (time.time() - flow_started))
        outer_remaining = _lensde_execution.remaining_timeout_seconds(timeout_seconds, started_at)
        if outer_remaining is not None:
            limits.append(outer_remaining)
        if not limits:
            return (0, time.time())
        allowed = min(limits)
        if allowed <= 0.0:
            raise TimeoutError('post-asphere dual-contract candidate timeout exhausted')
        return (max(1, int(math.ceil(allowed))), time.time())
    fixed_main_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_fixed_main_field_count', _lensde_commands.option_int_from(options, 'initial_search_post_asphere_field_count', 7, 2), 2)
    prepare_options = copy.deepcopy(options)
    prepare_options['initial_search_post_asphere_field_count'] = fixed_main_count
    prepare_options.pop('initial_search_post_asphere_preferred_field_nodes', None)
    contract_nodes = _lensde_asphere_contracts.post_asphere_preferred_field_nodes(options)
    if len(contract_nodes) >= fixed_main_count:
        prepare_options['initial_search_post_asphere_preferred_field_nodes'] = contract_nodes
    prepared = _lensde_asphere_contracts.post_asphere_prepare_case(source_dir, candidate_root / 'input', prepare_options, fallback_merit, common_merit=common_merit, promotion_handoff=protected_baseline)
    merit = prepared['merit']
    merit_path = prepared['merit_path']
    illumination_supervision = str(options.get('initial_search_post_asphere_illumination_supervision') or 'key_fields').strip().lower()
    late_all_fields = illumination_supervision in {'key_fields_then_all_fields', 'late_all_fields'}
    base_variables = prepared['variables']
    source_system = prepared['system']
    requested_population = _lensde_asphere_contracts.post_asphere_multistart_input_population_size(options, _lensde_populations.population_sizing_variable_count(base_variables))
    population_size = requested_population
    rng_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_rng_seed', 104729, 0)
    de_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_seed', 20260709, 0)
    schedule = _lensde_asphere_contracts.post_asphere_multistart_schedule(options)
    long_config = schedule['long']
    epoch_generations = max(1, int(long_config['de_generations']))
    schedule_mode = str(options.get('initial_search_post_asphere_single_mother_schedule') or 'continuous_early_stop').strip().lower()
    if schedule_mode != 'continuous_early_stop':
        raise RuntimeError(f'unsupported fixed-contract single-mother schedule: {schedule_mode}')
    scout_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_scout_count', 5, 1)
    scout_generations = _lensde_asphere_contracts.post_asphere_direction_scout_generations(options, merit)
    formation_generations = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_formation_generations', 8000, epoch_generations)
    formation_min_generations = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_formation_min_generations', 2400, epoch_generations)
    periodic_refresh = _lensde_asphere_contracts.post_asphere_periodic_refresh_config(options, population_size)
    generation_budgets = {'formation_max': formation_generations, 'formation_min': formation_min_generations}
    for budget_name, generations in generation_budgets.items():
        if generations % epoch_generations != 0:
            raise RuntimeError(f'{budget_name} generations must be a multiple of the {epoch_generations}-generation DE epoch')
    if formation_min_generations > formation_generations:
        raise RuntimeError('formation minimum exceeds its maximum generation budget')
    formation_epochs = formation_generations // epoch_generations
    formation_min_epochs = formation_min_generations // epoch_generations
    fixed_contract_mode = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_single_mother_fixed_contract_enabled'), True)
    validation_audit_enabled = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_validation_audit_enabled'), False)
    validation_audit_only = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_validation_audit_only'), True)
    if validation_audit_only and (not fixed_contract_mode):
        raise RuntimeError('validation-audit-only mode requires a fixed optimizer contract')
    if not validation_audit_enabled and (not fixed_contract_mode):
        raise RuntimeError('disabling the validation audit requires a fixed optimizer contract')
    baseline_dir = candidate_root / 'parent_baseline'
    baseline_result = copy.deepcopy(protected_baseline) if isinstance(protected_baseline, dict) else {}
    protected_text = str(baseline_result.get('result_dir') or '').strip()
    protected_dir = Path(protected_text) if protected_text else None
    if protected_dir is not None and protected_dir.is_dir():
        baseline_result['copied_files'] = _lensde_commands.copy_candidate_artifacts(protected_dir, baseline_dir)
        baseline_result['result_dir'] = str(baseline_dir)
        baseline_result['protected'] = True
    else:
        stage_timeout, stage_started = timeout_context()
        baseline_result = run_post_asphere_promotion_recheck(source_dir, baseline_dir, merit_path, options, cpu_exe, stage_timeout, stage_started, child_cpu_workers)
        baseline_result['protected'] = True
    validation_nodes = _lensde_asphere_contracts.post_asphere_fixed_validation_field_nodes(options) if validation_audit_enabled else []
    validation_options = _lensde_asphere_contracts.post_asphere_fixed_validation_options(options, validation_nodes) if validation_audit_enabled else {}
    if validation_audit_enabled:
        validation_timeout, validation_started = timeout_context()
        baseline_validation = run_post_asphere_multistart_audit('fixed_validation_parent', baseline_dir, native_io_paths.named_child(candidate_root / 'validation', 'fixed_validation_parent'), merit_path, validation_options, cpu_exe, validation_timeout, validation_started, child_cpu_workers, 1)
    else:
        baseline_validation = {'performed': False, 'skipped': True, 'reason': 'fixed_validation_audit_disabled', 'audit_only': True, 'field_count': 0, 'score': {}}
    incumbent_validation_score = copy.deepcopy(baseline_validation.get('score', {}))
    baseline_validation_merit = _lensde_configuration.finite_float(incumbent_validation_score.get('merit'), math.inf)
    if validation_audit_enabled and (not _lensde_asphere_contracts.post_asphere_parent_validation_is_usable(incumbent_validation_score, audit_only=validation_audit_only)):
        raise RuntimeError('fixed 41-field parent validation is not valid')
    main_probe_options = copy.deepcopy(options)
    main_probe_options.update({'initial_search_post_asphere_field_count': fixed_main_count, 'initial_search_post_asphere_field_coordinate': 'chief_image_height', 'initial_search_post_asphere_field_contract_policy': 'locked', 'initial_search_post_asphere_adaptive_fields_enabled': False, 'initial_search_post_asphere_adaptive_field_max_additions': 0})
    if len(contract_nodes) >= fixed_main_count:
        fixed_main_nodes = list(contract_nodes[:fixed_main_count])
        main_probe = {'enabled': False, 'skipped': True, 'reason': 'reuse_measured_handoff_main_field_contract', 'base_field_count': fixed_main_count, 'selected_nodes': fixed_main_nodes, 'source_contract_nodes': list(contract_nodes)}
        _lensde_provenance.write_json(native_io_paths.named_child(candidate_root / 'contracts', 'main_field_probe') / 'field_selection.json', main_probe)
    else:
        main_timeout, main_started = timeout_context()
        _main_system, main_probe = run_post_asphere_field_probe(source_system, base_variables, merit_path, native_io_paths.named_child(candidate_root / 'contracts', 'main_field_probe'), main_probe_options, cpu_exe, main_timeout, main_started, child_cpu_workers)
        fixed_main_nodes = list(main_probe.get('selected_nodes', []))
    if len(fixed_main_nodes) != fixed_main_count:
        raise RuntimeError(f'expected {fixed_main_count} fixed main fields; received {len(fixed_main_nodes)}')
    fixed_optimizer_nodes, fixed_edge_nodes = _lensde_asphere_contracts.post_asphere_fixed_optimizer_field_nodes(fixed_main_nodes, options)
    if len(fixed_optimizer_nodes) != fixed_main_count + len(fixed_edge_nodes):
        raise RuntimeError(f'expected {fixed_main_count + len(fixed_edge_nodes)} fixed optimizer fields; received {len(fixed_optimizer_nodes)}')
    contract_version = 1
    if fixed_contract_mode:
        field_reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, source_system)
        fixed_contract_vignetting_bridge: dict | None = None
        if _lensde_configuration.vignetting_optimization_enabled(merit):
            incumbent_system, fixed_contract_vignetting_bridge = _lensde_fields.aperture_realized_vignetting_field_bridge(source_system, merit, fixed_optimizer_nodes, candidate_root / 'contracts' / 'fixed_optimizer_vignetting_field_bridge', variables=base_variables, reference_max_radius=field_reference_radius, recalibrate_existing_contract=False)
        else:
            incumbent_system = _lensde_fields.resample_system_fields_for_asphere(source_system, fixed_optimizer_nodes, reference_max_radius=field_reference_radius)
        fixed_physical_radii = [_lensde_geometry.field_radius(field) for field in incumbent_system.get('fields', []) if isinstance(field, dict)]
        if len(fixed_physical_radii) != len(fixed_optimizer_nodes) or any((abs(actual - expected) > 1e-09 for actual, expected in zip(fixed_physical_radii, fixed_optimizer_nodes, strict=True))):
            raise RuntimeError(f'fixed post-asphere optimizer system does not contain the exact normalized field nodes: expected {fixed_optimizer_nodes}, received {fixed_physical_radii}')
        contract_audit = {'contract_version': contract_version, 'role': 'fixed_de_objective_full_flow', 'fixed_main_nodes': fixed_main_nodes, 'fixed_edge_nodes': fixed_edge_nodes, 'selected_nodes': fixed_optimizer_nodes, 'field_reference_radius': field_reference_radius, 'physical_field_radii': fixed_physical_radii, 'field_count': len(fixed_optimizer_nodes), 'validation_audit_enabled': validation_audit_enabled, 'validation_field_count': len(validation_nodes), 'validation_merit_at_contract_creation': baseline_validation_merit, 'vignetting_field_bridge': fixed_contract_vignetting_bridge, 'immutable': True}
    else:
        incumbent_system, contract_audit = _lensde_asphere_contracts.post_asphere_dynamic_contract_from_validation(source_system, fixed_optimizer_nodes, validation_nodes, incumbent_validation_score, options, contract_version)
    incumbent_variables = _lensde_initialization.sync_post_asphere_variables_for_system(incumbent_system, copy.deepcopy(base_variables), merit, rebase_asphere=True)
    requested_population = _lensde_asphere_contracts.post_asphere_multistart_input_population_size(options, _lensde_populations.population_sizing_variable_count(incumbent_variables))
    population_size = requested_population
    periodic_refresh = _lensde_asphere_contracts.post_asphere_periodic_refresh_config(options, population_size)
    _lensde_provenance.write_json(candidate_root / 'contracts' / f'contract_{contract_version:03d}.json', contract_audit)
    current_contract_audit = copy.deepcopy(contract_audit)
    initial_values = [_lensde_populations.variable_current_value_for_system(incumbent_system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in incumbent_variables]
    initial_conic_nonzero = sum((1 for variable, value in zip(incumbent_variables, initial_values, strict=True) if str(variable.get('kind', '')).lower() == 'conic' and abs(value) > 1e-30))
    initial_asphere_nonzero = sum((1 for variable, value in zip(incumbent_variables, initial_values, strict=True) if str(variable.get('kind', '')).lower() == 'asphere_coeff' and abs(value) > 1e-30))
    stage_options = _lensde_asphere_contracts.post_asphere_fixed_contract_stage_options(options, long_config)
    stage_floor_options = {'scout': 'initial_search_post_asphere_scout_minimum_relative_illumination', 'formation': 'initial_search_post_asphere_formation_minimum_relative_illumination', 'periodic': 'initial_search_post_asphere_periodic_minimum_relative_illumination'}
    stage_illumination_floors = {stage: _lensde_configuration.finite_float(options.get(option_name), 0.0) for stage, option_name in stage_floor_options.items()}
    explicit_stage_fixed_illumination = any((floor > 0.0 for floor in stage_illumination_floors.values()))
    if explicit_stage_fixed_illumination:
        if not all((0.0 < floor <= 1.0 for floor in stage_illumination_floors.values())):
            raise RuntimeError('stage-fixed illumination requires Scout, Formation and Periodic floors in (0, 1]')
        if not stage_illumination_floors['scout'] <= stage_illumination_floors['formation'] <= stage_illumination_floors['periodic']:
            raise RuntimeError('stage-fixed illumination floors must be nondecreasing')
    stage_illumination_merits: dict[str, dict] = {}
    stage_illumination_merit_paths: dict[str, Path] = {}
    stage_key_illumination_indices = post_asphere_handoff.selected_fields(incumbent_system, merit)
    stage_added_illumination_indices = [index for index in range(len(incumbent_system.get('fields', []))) if index not in stage_key_illumination_indices and _lensde_geometry.field_radius(incumbent_system['fields'][index]) > 1e-12]
    stage_illumination_policy = _lensde_asphere_contracts.canonical_post_asphere_stage_illumination_policy(options.get('initial_search_post_asphere_stage_illumination_policy'))
    stage_illumination_target = _lensde_configuration.vignetting_minimum_relative_illumination(merit)
    stage_illumination_safety_margin = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_stage_illumination_safety_margin', 0.01, 0.0)
    stage_illumination_scout_closure = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_stage_illumination_scout_closure', 0.25, 0.0)
    stage_illumination_formation_closure = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_stage_illumination_formation_closure', 0.7, 0.0)
    stage_illumination_floor_quantum = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_stage_illumination_floor_quantum', 0.005, 1e-09)
    stage_illumination_auto_plan: dict = {}
    auto_stage_fixed_illumination = bool(not explicit_stage_fixed_illumination and stage_illumination_policy == 'auto_margin' and (stage_illumination_target > 0.0) and stage_added_illumination_indices)
    if auto_stage_fixed_illumination:
        parent_illumination_score = _lensde_asphere_contracts.first_existing_json([baseline_dir / 'score.json', baseline_dir / 'checkpoint_score.json', baseline_dir / 'initial_score.json'], {})
        stage_illumination_auto_plan = _lensde_asphere_contracts.post_asphere_auto_stage_illumination_plan(parent_illumination_score, stage_added_illumination_indices, stage_illumination_target, stage_illumination_safety_margin, stage_illumination_scout_closure, stage_illumination_formation_closure, stage_illumination_floor_quantum)
        stage_illumination_floors = copy.deepcopy(stage_illumination_auto_plan['floors'])
    stage_fixed_illumination = bool(explicit_stage_fixed_illumination or auto_stage_fixed_illumination)
    stage_illumination_decision_mode = 'explicit_override' if explicit_stage_fixed_illumination else 'auto_margin' if auto_stage_fixed_illumination else 'legacy_key_fields_then_all_fields'
    if stage_fixed_illumination:
        for stage, floor in stage_illumination_floors.items():
            stage_merit = _lensde_asphere_contracts.post_asphere_stage_illumination_merit(merit, stage_added_illumination_indices, floor)
            stage_merit_path = native_io_paths.named_path(candidate_root, f'merit/{stage}_stage_fixed.json')
            _lensde_provenance.write_json(stage_merit_path, stage_merit)
            stage_illumination_merits[stage] = stage_merit
            stage_illumination_merit_paths[stage] = stage_merit_path
    scout_merit = stage_illumination_merits.get('scout', merit)
    scout_merit_path = stage_illumination_merit_paths.get('scout', merit_path)
    scout_illumination_mode = 'key_fields'
    stop_after_formation = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_stop_after_formation'), False)
    manifest: dict = {'flow': flow_name, 'status': 'running', 'source_dir': str(source_dir), 'selection_policy': 'rank_fixed10_best_then_population_median_then_label_carry_rank1', 'progress_policy': 'five_direction_scouts_then_continuous_formation_then_one_periodic_top_half_correlated_sag_refresh_long_optimization', 'confirmation_policy': 'not_used', 'optimizer_field_policy': 'immutable_seven_image_height_plus_three_edge', 'illumination_supervision_policy': 'auto_stage_boundary_fixed_key_fields_plus_added_fields' if auto_stage_fixed_illumination else 'explicit_stage_fixed_key_fields_plus_added_fields' if explicit_stage_fixed_illumination else 'key_fields_then_all_fields' if late_all_fields else illumination_supervision, 'stage_fixed_illumination': {'enabled': stage_fixed_illumination, 'decision_mode': stage_illumination_decision_mode, 'configured_policy': stage_illumination_policy, 'floors': stage_illumination_floors, 'key_field_indices': stage_key_illumination_indices, 'key_field_floor': _lensde_configuration.finite_float(merit.get('minimum_relative_illumination'), math.nan), 'added_field_indices': stage_added_illumination_indices, 'added_field_operand_weight': 25.0, 'safety_margin': stage_illumination_safety_margin, 'scout_deficit_closure': stage_illumination_scout_closure, 'formation_deficit_closure': stage_illumination_formation_closure, 'floor_quantum': stage_illumination_floor_quantum, 'pre_scout_decision': stage_illumination_auto_plan, 'merit_paths': {stage: str(path) for stage, path in stage_illumination_merit_paths.items()}, 'constant_within_each_stage': True, 'stop_after_formation': stop_after_formation}, 'validation_field_policy': 'immutable_41_node_cpu_audit_only' if validation_audit_enabled else 'disabled', 'schedule': schedule_mode, 'termination_policy': {'local': 'best fixed-10 merit recent-window early stop; no wall clock', 'post_formation': 'one periodic top-half multi-parent correlated-sag optimization with the same adaptive early-stop policy', 'maximum': 'configured periodic refresh rounds', 'wall_clock_used': False}, 'fixed_contract_mode': fixed_contract_mode, 'validation_audit_enabled': validation_audit_enabled, 'validation_audit_only': validation_audit_only, 'requested_population': requested_population, 'explicit_population': population_size, 'population_multiplier': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_multiplier', 5.0, 0.01), 'absolute_population_override': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_population', 0, 0), 'variable_count': len(incumbent_variables), 'initial_conic_nonzero': initial_conic_nonzero, 'initial_asphere_nonzero': initial_asphere_nonzero, 'formation_generations': formation_generations, 'formation_min_generations': formation_min_generations, 'direction_scout_count': scout_count, 'direction_scout_generations': scout_generations, 'direction_scout_budget_mode': 'fixed_g400_formation_handoff' if curvature_normalized_scouts else 'vignetting' if _lensde_configuration.vignetting_optimization_enabled(scout_merit) else 'no_vignetting', 'direction_scout_generator': direction_scout_generator, 'direction_scout_checkpoint_branching': False, 'direction_scout_commit_policy': 'rank1_terminal_population_carry_with_fresh_adaptive_state', 'periodic_refresh': periodic_refresh, 'stage_boundary_vignetting_recalibration': {'enabled': _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, prepared['merit']), 'measurement': 'gui_exact_adaptive_pilot_grid_primary_wave', 'boundaries': ['scout_to_formation', 'formation_to_periodic', 'periodic_to_final'], 'selection_basis': 'pre_vignetting_calibration_optimizer_merit', 'optimizer_hot_loop_unchanged': True, 'projection_or_contraction': False}, 'fixed_main_nodes': fixed_main_nodes, 'fixed_edge_nodes': fixed_edge_nodes, 'fixed_optimizer_nodes': fixed_optimizer_nodes, 'field_reference_radius': _lensde_fields.post_asphere_field_reference_radius(options, source_system), 'fixed_optimizer_physical_field_radii': [_lensde_geometry.field_radius(field) for field in incumbent_system.get('fields', []) if isinstance(field, dict)], 'fixed_validation_nodes': validation_nodes, 'baseline_validation_merit': baseline_validation_merit, 'contracts': {'contract_001': contract_audit}, 'stages': {}, 'direction_scouts': {}, 'decisions': []}
    manifest_path = candidate_root / 'single_mother_manifest.json'
    _lensde_provenance.write_json(manifest_path, manifest)
    if stop_before_scout:
        entry_dir = candidate_root / 'pre_scout'
        _lensde_provenance.write_json(entry_dir / 'system_input.json', incumbent_system)
        _lensde_provenance.write_json(entry_dir / 'variables.json', {'variables': incumbent_variables})
        _lensde_provenance.write_json(entry_dir / 'merit.json', merit)
        _lensde_provenance.write_json(entry_dir / 'options.json', stage_options)
        _lensde_populations.write_population_csv(entry_dir / 'mother_population.csv', [initial_values])
        manifest.update({'status': 'stopped_before_scout', 'scout_optimization_started': False, 'pre_scout_dir': str(entry_dir), 'promotion_handoff_reused': prepared.get('promotion_handoff_reused', False)})
        _lensde_provenance.write_json(manifest_path, manifest)
        return manifest
    scout_specs: list[dict] = []
    for scout_index in range(1, scout_count + 1):
        label = f'direction_scout_{scout_index:02d}'
        if reuse_existing_scouts:
            stage_dir = reuse_scout_root / 'direction_scouts' / label
            if not (stage_dir / 'result.json').is_file() or not (stage_dir / 'score.json').is_file() or (not (stage_dir / 'population.csv').is_file()):
                raise RuntimeError(f'cannot reuse incomplete Scout stage: {stage_dir}')
            old_initialization = _lensde_asphere_contracts.first_existing_json([stage_dir / 'initialization.json'], {})
            if not isinstance(old_initialization, dict):
                old_initialization = {}
            if curvature_normalized_scouts and str(old_initialization.get('generator') or '').strip().lower() != direction_scout_generator:
                raise RuntimeError(f'cannot reuse a legacy Scout in the curvature-normalized candidate: {stage_dir}')
            population_seed = int(old_initialization.get('population_seed', rng_seed + seed_offset + scout_index * 1000 + 101))
            population_origin = 'reused_completed_direction_scout'
            direction_population = _lensde_populations.read_population_csv(stage_dir / 'population.csv')
            if len(direction_population) != population_size:
                raise RuntimeError(f'reused {label} population has {len(direction_population)} rows; expected {population_size}')
            old_stage_summary = _lensde_asphere_contracts.first_existing_json([stage_dir / 'stage_summary.json'], {})
            old_de_seed = int(old_stage_summary.get('de_seed', de_seed + seed_offset + scout_index * 1000 + 501))
            initialization = old_initialization
            scout_specs.append({'label': label, 'stage_dir': stage_dir, 'system': incumbent_system, 'variables': incumbent_variables, 'merit': scout_merit, 'population': direction_population, 'population_seed': population_seed, 'de_seed': old_de_seed, 'population_origin': population_origin, 'initialization': initialization, 'epochs': 1, 'epoch_generations': scout_generations, 'profile': 'post_asphere_direction_scout'})
            continue
        population_seed = (rng_seed + seed_offset + scout_index * 1000 + 101) % 2000000000
        if curvature_normalized_scouts:
            direction_population = _lensde_initialization.make_post_asphere_curvature_normalized_graded_population(incumbent_system, incumbent_variables, initial_values, population_size, population_seed, 0.35, 0.025)
            initialization = {'generator': direction_scout_generator, 'max_scale': 0.35, 'amplitude_power': 2.0, 'curvature_bound_fraction': 0.025, 'curvature_span': 'amplitude * curvature_bound_fraction * (upper-lower)', 'population_seed': population_seed, 'requested_population': population_size, 'variable_count': len(incumbent_variables), 'traceability_evaluated': False}
            population_origin = 'fresh_graded_curvature_bound_normalized_direction_scout'
        else:
            population_scale = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_scale', 0.35, 0.0)
            direction_population = _lensde_initialization.make_post_asphere_population(incumbent_system, incumbent_variables, initial_values, population_size, population_seed, population_scale)
            initialization = {'generator': direction_scout_generator, 'population_scale': population_scale, 'population_seed': population_seed, 'requested_population': population_size, 'variable_count': len(incumbent_variables), 'traceability_evaluated': False}
            population_origin = 'fresh_legacy_direction_scout'
        scout_specs.append({'label': label, 'stage_dir': native_io_paths.named_child(native_io_paths.named_child(candidate_root, 'direction_scouts'), label), 'system': incumbent_system, 'variables': incumbent_variables, 'merit': scout_merit, 'population': direction_population, 'population_seed': population_seed, 'de_seed': (de_seed + seed_offset + scout_index * 1000 + 501) % 2000000000, 'population_origin': population_origin, 'initialization': initialization, 'epochs': 1, 'epoch_generations': scout_generations, 'profile': 'post_asphere_direction_scout'})
    manifest['direction_scout_initialization'] = {spec['label']: spec['initialization'] for spec in scout_specs}
    for spec in scout_specs:
        if reuse_existing_scouts:
            continue
        initialization_dir = Path(str(spec['stage_dir']))
        initialization_dir.mkdir(parents=True, exist_ok=True)
        _lensde_provenance.write_json(initialization_dir / 'initialization.json', spec['initialization'])
    _lensde_provenance.write_json(manifest_path, manifest)
    scout_de_results: list[dict] = []
    scout_multi_island = bool(not reuse_existing_scouts and de_use_cuda and (len(scout_specs) >= 2) and (state.CPP_ROOT / 'build' / 'lensde_multi_island.exe').is_file())
    if reuse_existing_scouts:
        scout_de_results = [_lensde_asphere_contracts.first_existing_json([Path(str(spec['stage_dir'])) / 'stage_summary.json'], {}) for spec in scout_specs]
    elif scout_multi_island:
        scout_timeout, scout_started = timeout_context()
        scout_de_results = run_post_asphere_multi_island_wave('direction_scout_batch', native_io_paths.named_child(candidate_root, 'direction_scout_batch'), scout_specs, stage_options, scout_timeout, scout_started, child_cpu_workers)
    else:
        for spec in scout_specs:
            scout_timeout, scout_started = timeout_context()
            scout_de_results.append(run_post_asphere_multistart_de_stage(spec['label'], Path(str(spec['stage_dir'])), spec['system'], spec['variables'], spec['merit'], spec['population'], stage_options, de_exe, de_use_cuda, 1, spec['de_seed'], spec['population_origin'], scout_timeout, scout_started, child_cpu_workers, epoch_generations=scout_generations, early_stop=False))
    direction_candidates: list[dict] = []
    for spec, de_result in zip(scout_specs, scout_de_results, strict=True):
        stage_dir = Path(str(spec['stage_dir']))
        result_doc = _lensde_asphere_contracts.first_existing_json([stage_dir / 'result.json'], {})
        score_doc = _lensde_asphere_contracts.first_existing_json([stage_dir / 'score.json'], {})
        scout_merit = _lensde_configuration.finite_float(result_doc.get('best_merit', score_doc.get('merit')), math.inf)
        trace_coverage = _lensde_configuration.finite_float(score_doc.get('trace_coverage'), 0.0)
        if not math.isfinite(scout_merit) or not _lensde_asphere_contracts.post_asphere_trace_coverage_is_valid(trace_coverage, stage_options):
            trace_contract = _lensde_asphere_contracts.post_asphere_trace_coverage_contract(stage_options)
            raise RuntimeError(f"{spec['label']} has invalid fixed10 scout result: merit={scout_merit}, coverage={trace_coverage}, required_coverage={trace_contract['required_min_trace_coverage']}")
        candidate = {'label': spec['label'], 'stage_dir': str(stage_dir), 'population_seed': spec['population_seed'], 'de_seed': spec['de_seed'], 'best_merit': scout_merit, 'population_median_merit': _lensde_asphere_contracts.post_asphere_direction_scout_population_median(stage_dir, population_size), 'initialization': spec.get('initialization', {}), 'trace_coverage': trace_coverage, 'actual_generations': int(de_result.get('actual_generations', 0) or 0), 'population': population_size, 'multi_island': bool(de_result.get('multi_island'))}
        direction_candidates.append(candidate)
        manifest['direction_scouts'][spec['label']] = candidate
    direction_candidates.sort(key=_lensde_asphere_contracts.post_asphere_direction_scout_sort_key)
    raw_direction_ranking = copy.deepcopy(direction_candidates)
    selected_direction: dict | None = None
    scout_boundary_failures: list[dict] = []
    for candidate in direction_candidates:
        if not _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, prepared['merit']):
            selected_direction = candidate
            break
        raw_stage_dir = Path(str(candidate['stage_dir']))
        boundary_dir = native_io_paths.named_child(native_io_paths.named_child(native_io_paths.named_child(candidate_root, 'stage_boundaries'), 'scout_to_formation'), str(candidate['label']))
        try:
            boundary_timeout, boundary_started = timeout_context()
            boundary = _lensde_asphere_contracts.recalibrate_post_asphere_stage_handoff(raw_stage_dir, boundary_dir, scout_merit_path, {**stage_options, 'initial_search_post_asphere_illumination_supervision': scout_illumination_mode}, cpu_exe, boundary_timeout, boundary_started, child_cpu_workers)
            candidate['optimization_stage_dir'] = str(raw_stage_dir)
            candidate['stage_dir'] = str(boundary_dir)
            candidate['pre_vignetting_calibration_merit'] = candidate['best_merit']
            candidate['stage_boundary_vignetting'] = boundary['report']
            selected_direction = candidate
            break
        except Exception as error:
            failure = {'label': candidate['label'], 'optimization_stage_dir': str(raw_stage_dir), 'error': str(error)}
            candidate['stage_boundary_vignetting_error'] = str(error)
            scout_boundary_failures.append(failure)
    if selected_direction is None:
        raise RuntimeError(f'every direction Scout failed strict stage-boundary Set Vignetting: {scout_boundary_failures}')
    selected_spec = next((spec for spec in scout_specs if spec['label'] == selected_direction['label']))
    incumbent_system, incumbent_variables, formation_population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(Path(str(selected_direction['stage_dir'])))
    if len(formation_population) != population_size or any((len(row) != len(incumbent_variables) for row in formation_population)):
        raise RuntimeError('selected direction terminal population does not match the P/D contract')
    formation_population_seed = int(selected_spec['population_seed'])
    configured_formation_de_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_formation_seed', 0, 0)
    formation_de_seed = configured_formation_de_seed if configured_formation_de_seed > 0 else (int(selected_spec['de_seed']) + 1000003) % 2000000000
    formation_de_seed = seed_protocol.post_asphere_seed(options, 'formation_optimizer', (seed_offset, 1), formation_de_seed)
    formation_population_seed = seed_protocol.post_asphere_seed(options, 'formation_population', (seed_offset, 1), formation_population_seed)
    formation_scout_merit = _lensde_configuration.finite_float(selected_direction['best_merit'], math.inf)
    formation_illumination_recheck: dict = {'performed': False, 'reason': 'not_using_auto_margin_policy'}
    if auto_stage_fixed_illumination:
        if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_formation_illumination_recheck'), True):
            selected_scout_score = _lensde_asphere_contracts.first_existing_json([Path(str(selected_direction['stage_dir'])) / 'score.json', Path(str(selected_direction['stage_dir'])) / 'checkpoint_score.json'], {})
            formation_illumination_recheck = _lensde_asphere_contracts.post_asphere_auto_formation_illumination_recheck(stage_illumination_auto_plan, selected_scout_score, stage_added_illumination_indices, stage_illumination_target, stage_illumination_safety_margin, stage_illumination_formation_closure, stage_illumination_floor_quantum)
            stage_illumination_floors['formation'] = _lensde_configuration.finite_float(formation_illumination_recheck.get('formation_floor'), 0.0)
            formation_merit = _lensde_asphere_contracts.post_asphere_stage_illumination_merit(merit, stage_added_illumination_indices, stage_illumination_floors['formation'])
            formation_merit_path = stage_illumination_merit_paths['formation']
            _lensde_provenance.write_json(formation_merit_path, formation_merit)
            stage_illumination_merits['formation'] = formation_merit
        else:
            formation_illumination_recheck = {'performed': False, 'reason': 'formation_boundary_recheck_disabled', 'formation_floor': stage_illumination_floors['formation'], 'fixed_before_formation_start': True}
        manifest['stage_fixed_illumination']['floors'] = copy.deepcopy(stage_illumination_floors)
        manifest['stage_fixed_illumination']['formation_boundary_recheck'] = formation_illumination_recheck
    manifest['direction_scout_selection'] = {'ranking': raw_direction_ranking, 'selected': selected_direction['label'], 'selected_merit': formation_scout_merit, 'selected_population_median_merit': selected_direction['population_median_merit'], 'tie_break_policy': 'exact_best_merit_tie_then_full_population_median_then_label', 'selection_basis': 'pre_vignetting_calibration_optimizer_merit', 'selected_stage_boundary_dir': selected_direction['stage_dir'], 'formation_de_seed': formation_de_seed, 'formation_de_seed_policy': 'explicit_reproduction_contract' if configured_formation_de_seed > 0 else 'selected_scout_de_seed_plus_1000003', 'stage_boundary_failures': scout_boundary_failures, 'commit': 'terminal_population_carry_then_formation_then_periodic_refresh', 'adaptive_state_policy': 'fresh_JADE_archive_and_RNG_state', 'checkpoint_branching': False, 'multi_island': scout_multi_island}
    manifest['decisions'].append({'stage': 'direction_scout_to_commit', 'selected': selected_direction['label'], 'selected_merit': formation_scout_merit, 'ranking': [{'label': row['label'], 'merit': row['best_merit'], 'population_median_merit': row['population_median_merit']} for row in raw_direction_ranking], 'rule': 'lowest_precalibration_fixed10_best_then_population_median_then_label_with_boundary_fallback'})
    _lensde_provenance.write_json(manifest_path, manifest)
    rounds: list[dict] = []
    incumbent_validation_merit = baseline_validation_merit
    incumbent_stage_dir = baseline_dir
    incumbent_label = 'protected_parent'
    incumbent_de: dict = {}
    incumbent_objective_merit = math.inf
    cumulative_generations = 0
    formation_completed = False
    periodic_refresh_completed = False
    stop_reason = 'periodic_refresh_complete'
    round_index = 0
    while True:
        round_index += 1
        formation_round = round_index == 1
        periodic_refresh_round = formation_completed and (not periodic_refresh_completed)
        if not formation_round and (not periodic_refresh_round):
            break
        round_merit = stage_illumination_merits['formation'] if formation_round and stage_fixed_illumination else merit
        round_merit_path = stage_illumination_merit_paths['formation'] if formation_round and stage_fixed_illumination else merit_path
        round_illumination_mode = 'key_fields'
        if periodic_refresh_round and stage_fixed_illumination:
            round_merit = stage_illumination_merits['periodic']
            round_merit_path = stage_illumination_merit_paths['periodic']
            round_illumination_mode = 'key_fields'
        elif periodic_refresh_round and late_all_fields:
            all_field_options = copy.deepcopy(options)
            all_field_options['initial_search_post_asphere_illumination_supervision'] = 'all_fields'
            round_merit = post_asphere_handoff.configure_illumination(merit, all_field_options, incumbent_system)
            round_merit_path = native_io_paths.named_path(candidate_root, 'merit/periodic_all_fields.json')
            _lensde_provenance.write_json(round_merit_path, round_merit)
            round_illumination_mode = 'all_fields'
        restart_variables = _lensde_initialization.sync_post_asphere_variables_for_system(incumbent_system, copy.deepcopy(base_variables), round_merit, rebase_asphere=True)
        population_seed = seed_protocol.post_asphere_seed(options, 'periodic_population', (seed_offset, round_index), (rng_seed + seed_offset + 50000 + round_index * 1009) % 2000000000)
        seed_lineage: list[dict] = []
        if formation_round:
            population_seed = formation_population_seed
            restart_population = [list(row) for row in formation_population]
            seed_summary = {'generator': 'selected_direction_scout_terminal_population_carry', 'carried_rows': population_size, 'anchor_rows': 0, 'donor_rows': 0, 'fresh_rows': 0, 'selected_scout': selected_direction['label'], 'selected_scout_merit': formation_scout_merit, 'concentrated_de_seed': formation_de_seed, 'preserves_terminal_population': True, 'preserves_post_scout_adaptive_state': False}
            population_origin = 'selected_direction_scout_terminal_population'
            label = 'formation_round_01'
            scheduled_epochs = formation_epochs
            minimum_epochs = formation_min_epochs
            stage_early_stop = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_early_stop'), True)
            stage_epoch_generations = epoch_generations
        else:
            restart_population = _lensde_populations.read_population_csv(incumbent_stage_dir / 'population.csv')
            if len(restart_population) != population_size or any((len(row) != len(restart_variables) for row in restart_population)):
                raise RuntimeError('formation terminal population does not match the P/D contract')
            periodic_score_records = _lensde_materials.read_population_score_records(incumbent_stage_dir / 'population_scores.csv')
            if late_all_fields or stage_fixed_illumination:
                periodic_score_records = _lensde_asphere_contracts.rescore_post_asphere_population_for_merit(incumbent_system, restart_variables, restart_population, round_merit, {**options, 'initial_search_post_asphere_illumination_supervision': 'key_fields' if stage_fixed_illumination else 'all_fields'}, de_exe, native_io_paths.named_path(candidate_root, 'population_rescore/periodic_all_fields'), *timeout_context(), child_cpu_workers, use_cuda_evaluator=de_use_cuda)
            restart_population, seed_summary, seed_lineage = _lensde_asphere_population.make_post_asphere_top_half_periodic_refresh_population(restart_population, restart_variables, periodic_score_records, parent_count=int(periodic_refresh['parent_count']), sag_um=float(periodic_refresh['sag_um']), correlation_length=float(periodic_refresh['correlation_length']), seed=population_seed)
            seed_summary.update({'source': 'formation_terminal_population', 'adaptive_state_policy': 'fresh_JADE_archive_and_RNG_from_pre_refreshed_formation_population', 'periodic_refresh': periodic_refresh})
            population_origin = 'formation_terminal_top_half_pre_refresh'
            label = 'periodic_refresh_optimization'
            scheduled_epochs = int(periodic_refresh['max_rounds'])
            minimum_epochs = 0
            stage_early_stop = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_early_stop'), True)
            stage_epoch_generations = int(periodic_refresh['generations_per_round'])
        stage_dir = native_io_paths.named_child(candidate_root / 'rounds', label)
        if seed_lineage:
            _lensde_provenance.write_json(stage_dir / 'pre_periodic_refresh_lineage.json', seed_lineage)
        used_contract_version = contract_version
        current_contract_nodes = list(fixed_optimizer_nodes)
        round_stage_options = copy.deepcopy(stage_options)
        round_stage_options.update({'initial_search_post_asphere_field_count': len(current_contract_nodes), 'initial_search_post_asphere_preferred_field_nodes': current_contract_nodes, 'initial_search_post_asphere_early_stop_min_epochs': max(minimum_epochs, _lensde_commands.option_int_from(stage_options, 'initial_search_post_asphere_early_stop_min_epochs', 2, 0))})
        if periodic_refresh_round:
            round_stage_options.update({'initial_search_post_asphere_periodic_refresh_active': True, 'initial_search_post_asphere_periodic_refresh_target_count': int(periodic_refresh['replacement_count']), 'initial_search_post_asphere_periodic_refresh_parent_count': int(periodic_refresh['parent_count']), 'initial_search_post_asphere_periodic_refresh_sag_um': float(periodic_refresh['sag_um']), 'initial_search_post_asphere_periodic_refresh_correlation_length': float(periodic_refresh['correlation_length']), 'initial_search_post_asphere_refresh': float(periodic_refresh['replacement_fraction'])})
        stage_timeout, stage_started = timeout_context()
        de_result = run_post_asphere_multistart_de_stage(label, stage_dir, incumbent_system, restart_variables, round_merit, restart_population, round_stage_options, de_exe, de_use_cuda, scheduled_epochs, formation_de_seed if formation_round else seed_protocol.post_asphere_seed(options, 'periodic_optimizer', (seed_offset, round_index), (de_seed + seed_offset + 30000 + round_index) % 2000000000), population_origin, stage_timeout, stage_started, child_cpu_workers, epoch_generations=stage_epoch_generations, early_stop=stage_early_stop)
        actual_generations = int(de_result.get('actual_generations', 0) or 0)
        scheduled_generations = scheduled_epochs * stage_epoch_generations
        if actual_generations <= 0 or actual_generations > scheduled_generations or actual_generations % stage_epoch_generations != 0:
            raise RuntimeError(f'{label} used {actual_generations} generations; expected a positive multiple of {stage_epoch_generations} no greater than {scheduled_generations}')
        cumulative_generations += actual_generations
        if validation_audit_enabled:
            validation_timeout, validation_started = timeout_context()
            validation_audit = run_post_asphere_multistart_audit(label, stage_dir, native_io_paths.named_child(candidate_root / 'validation', label), round_merit_path, validation_options, cpu_exe, validation_timeout, validation_started, child_cpu_workers, 1)
        else:
            validation_audit = {'performed': False, 'skipped': True, 'reason': 'fixed_validation_audit_disabled', 'audit_only': True, 'field_count': 0, 'score': {}}
        child_validation_score = copy.deepcopy(validation_audit.get('score', {}))
        child_validation_merit = _lensde_configuration.finite_float(child_validation_score.get('merit'), math.inf)
        objective_parent_score = _lensde_asphere_contracts.first_existing_json([stage_dir / 'initial_score.json'], {})
        parent_objective_merit = _lensde_configuration.finite_float(objective_parent_score.get('merit'), math.inf)
        child_objective_merit = _lensde_configuration.finite_float(de_result.get('merit'), math.inf)
        if not math.isfinite(parent_objective_merit) or not math.isfinite(child_objective_merit):
            raise RuntimeError(f'{label} has a non-finite fixed objective merit')
        parent_validation_merit = incumbent_validation_merit
        parent_validation_score = copy.deepcopy(incumbent_validation_score)
        if not validation_audit_enabled or validation_audit_only:
            accepted = True
        else:
            accepted = bool(_lensde_commands.option_truthy(child_validation_score.get('strict_valid'), False) and _lensde_asphere_contracts.is_strict_merit_improvement(child_validation_merit, incumbent_validation_merit))
        endpoint_population: list[list[float]] = []
        stage_boundary_vignetting: dict = {'performed': False, 'reason': 'stage_not_accepted'}
        if accepted:
            incumbent_objective_merit = child_objective_merit
            if validation_audit_enabled:
                incumbent_validation_merit = child_validation_merit
                incumbent_validation_score = child_validation_score
            incumbent_stage_dir = stage_dir
            incumbent_label = label
            incumbent_de = de_result
            incumbent_system, incumbent_variables, endpoint_population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(stage_dir)
            if _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, round_merit):
                boundary_role = 'formation_to_periodic' if formation_round else 'periodic_to_final'
                boundary_dir = native_io_paths.named_path(candidate_root, f'stage_boundaries/{boundary_role}/{label}')
                boundary_timeout, boundary_started = timeout_context()
                boundary = _lensde_asphere_contracts.recalibrate_post_asphere_stage_handoff(stage_dir, boundary_dir, round_merit_path, {**round_stage_options, 'initial_search_post_asphere_illumination_supervision': round_illumination_mode}, cpu_exe, boundary_timeout, boundary_started, child_cpu_workers)
                incumbent_stage_dir = boundary_dir
                incumbent_system = boundary['system']
                incumbent_variables = boundary['variables']
                endpoint_population = boundary['population']
                stage_boundary_vignetting = boundary['report']
        rejection_guard = {'active': False, 'reason': 'validation_audit_disabled' if not validation_audit_enabled else 'validation_audit_only_no_rollback' if validation_audit_only else 'child_accepted'} if accepted else _lensde_asphere_contracts.post_asphere_rejection_degradation_guard(validation_nodes, fixed_optimizer_nodes, parent_validation_score, child_validation_score, options)
        if formation_round:
            formation_completed = True
        if fixed_contract_mode:
            next_contract = current_contract_audit
        else:
            contract_version += 1
            incumbent_system, next_contract = _lensde_asphere_contracts.post_asphere_dynamic_contract_from_validation(incumbent_system, fixed_optimizer_nodes, validation_nodes, incumbent_validation_score, options, contract_version, previous_contract=current_contract_audit, forced_anomaly_node=rejection_guard.get('node') if _lensde_commands.option_truthy(rejection_guard.get('active'), False) else None)
        incumbent_variables = _lensde_initialization.sync_post_asphere_variables_for_system(incumbent_system, copy.deepcopy(base_variables), round_merit, rebase_asphere=True)
        if not fixed_contract_mode:
            contract_key = f'contract_{contract_version:03d}'
            manifest['contracts'][contract_key] = next_contract
            current_contract_audit = copy.deepcopy(next_contract)
            _lensde_provenance.write_json(candidate_root / 'contracts' / f'{contract_key}.json', next_contract)
        if formation_round:
            decision = {'objective_parent_merit': parent_objective_merit, 'objective_child_merit': child_objective_merit, 'objective_gain': parent_objective_merit - child_objective_merit, 'significant_threshold': None, 'significant': True, 'confirmation_round': False, 'action': 'stop_after_formation_requested' if stop_after_formation else 'enter_periodic_top_half_correlated_sag_refresh_after_formation', 'decision_basis': 'formation_internal_early_stop', 'uses_wall_clock': False}
        else:
            decision = {'objective_parent_merit': parent_objective_merit, 'objective_child_merit': child_objective_merit, 'objective_gain': parent_objective_merit - child_objective_merit, 'significant_threshold': None, 'significant': True, 'confirmation_round': False, 'action': 'complete_periodic_top_half_correlated_sag_refresh', 'decision_basis': 'configured_max_rounds_or_adaptive_early_stop', 'uses_wall_clock': False}
            periodic_refresh_completed = True
            stop_reason = 'periodic_refresh_early_stop' if actual_generations < scheduled_generations else 'periodic_refresh_max_rounds'
        candidate = {'label': label, 'stage_dir': str(stage_dir), 'formation_round': formation_round, 'confirmation_round': False, 'periodic_refresh_round': periodic_refresh_round, 'illumination_supervision': round_illumination_mode, 'illumination_merit_path': str(round_merit_path), 'scheduled_generations': scheduled_generations, 'actual_generations': actual_generations, 'early_stopped': actual_generations < scheduled_generations, 'contract_version_used': used_contract_version, 'next_contract_version': contract_version, 'optimizer_field_nodes': current_contract_nodes, 'de': de_result, 'validation_audit': validation_audit, 'population_seed': population_seed, 'seed_population': seed_summary, 'parent_validation_merit': parent_validation_merit, 'child_validation_merit': child_validation_merit, 'incumbent_validation_merit': incumbent_validation_merit, 'parent_objective_merit': parent_objective_merit, 'child_objective_merit': child_objective_merit, 'incumbent_objective_merit': incumbent_objective_merit, 'accepted': accepted, 'pre_vignetting_calibration_merit': child_objective_merit, 'stage_boundary_handoff_dir': str(incumbent_stage_dir) if accepted else '', 'stage_boundary_vignetting': stage_boundary_vignetting, 'rejection_guard': rejection_guard, 'decision': decision, 'cumulative_generations': cumulative_generations}
        rounds.append(candidate)
        manifest['stages'][label] = {'label': label, 'stage_dir': str(stage_dir), 'formation_round': formation_round, 'confirmation_round': False, 'periodic_refresh_round': periodic_refresh_round, 'illumination_supervision': round_illumination_mode, 'illumination_merit_path': str(round_merit_path), 'scheduled_generations': scheduled_generations, 'actual_generations': actual_generations, 'early_stopped': actual_generations < scheduled_generations, 'contract_version_used': used_contract_version, 'next_contract_version': contract_version, 'optimizer_field_nodes': current_contract_nodes, 'de': de_result, 'validation_audit': validation_audit, 'population_seed': population_seed, 'seed_population': seed_summary, 'parent_validation_merit': parent_validation_merit, 'child_validation_merit': child_validation_merit, 'incumbent_validation_merit': incumbent_validation_merit, 'parent_objective_merit': parent_objective_merit, 'child_objective_merit': child_objective_merit, 'incumbent_objective_merit': incumbent_objective_merit, 'accepted': accepted, 'pre_vignetting_calibration_merit': child_objective_merit, 'stage_boundary_handoff_dir': str(incumbent_stage_dir) if accepted else '', 'stage_boundary_vignetting': stage_boundary_vignetting, 'rejection_guard': rejection_guard, 'decision': decision, 'cumulative_generations': cumulative_generations}
        manifest['decisions'].append({'stage': label, 'accepted': accepted, 'rejection_guard': rejection_guard, **decision})
        manifest.update({'current_validation_merit': incumbent_validation_merit, 'current_objective_merit': incumbent_objective_merit, 'current_contract_version': contract_version, 'formation_completed': formation_completed, 'rounds_completed': len(rounds), 'periodic_refresh_completed': periodic_refresh_completed})
        if periodic_refresh_round:
            manifest['stop_reason'] = stop_reason
            _lensde_provenance.write_json(manifest_path, manifest)
            break
        if formation_round and stop_after_formation:
            stop_reason = 'formation_complete_requested'
            manifest['stop_reason'] = stop_reason
            _lensde_provenance.write_json(manifest_path, manifest)
            break
        _lensde_provenance.write_json(manifest_path, manifest)
    if not validation_audit_enabled or validation_audit_only:
        protected_parent_retained = False
        best_stage_dir = incumbent_stage_dir
        best_validation_merit = incumbent_validation_merit
        best_objective_merit = incumbent_objective_merit
    else:
        protected_parent_retained = not _lensde_asphere_contracts.is_strict_merit_improvement(incumbent_validation_merit, baseline_validation_merit)
        best_stage_dir = baseline_dir if protected_parent_retained else incumbent_stage_dir
        best_validation_merit = baseline_validation_merit if protected_parent_retained else incumbent_validation_merit
        best_objective_merit = best_validation_merit
    selected_dir = candidate_root / 'best_result'
    copied = _lensde_commands.copy_candidate_artifacts(best_stage_dir, selected_dir)
    manifest.update({'status': 'complete_after_formation' if stop_after_formation and (not periodic_refresh_completed) else 'complete', 'wall_seconds': time.time() - flow_started, 'rounds_completed': len(rounds), 'rounds_accepted': sum((1 for row in rounds if row['accepted'])), 'periodic_refresh_rounds': sum((1 for row in rounds if row.get('periodic_refresh_round'))), 'cumulative_generations': cumulative_generations, 'selected_candidate': incumbent_label, 'selected_validation_merit': best_validation_merit, 'selected_objective_merit': best_objective_merit, 'protected_parent_retained': protected_parent_retained, 'stop_reason': stop_reason})
    _lensde_provenance.write_json(manifest_path, manifest)
    summary = {'flow': flow_name, 'source_dir': str(source_dir), 'candidate_root': str(candidate_root), 'selected_result_dir': str(selected_dir), 'best_stage_dir': str(best_stage_dir), 'best_merit': best_objective_merit, 'best_objective_merit': best_objective_merit, 'best_validation_merit': best_validation_merit, 'best_mean_rms_um': _lensde_configuration.finite_float(incumbent_validation_score.get('mean_rms_um'), math.inf), 'best_max_rms_um': _lensde_configuration.finite_float(incumbent_validation_score.get('max_rms_um'), math.inf), 'protected_parent_retained': protected_parent_retained, 'copied_files': copied, 'prepared': {'selected_surface_indices': prepared['selected_surface_indices'], 'lens_groups': prepared['lens_groups'], 'population_rows': population_size, 'requested_population_rows': requested_population, 'variable_count': len(incumbent_variables), 'initial_conic_nonzero': initial_conic_nonzero, 'initial_asphere_nonzero': initial_asphere_nonzero, 'fixed_main_nodes': fixed_main_nodes, 'fixed_edge_nodes': fixed_edge_nodes, 'fixed_optimizer_nodes': fixed_optimizer_nodes, 'fixed_validation_nodes': validation_nodes}, 'evaluators': {'de': 'CUDA' if de_use_cuda else 'CPU', 'de_executable': str(de_exe), 'de_objective': 'immutable seven below-edge chief-image-height fields plus fixed 0.925/0.975/1.0 edge fields', 'selection_and_stop': 'same immutable 10-field objective; 41-field CPU audit only' if validation_audit_enabled else 'same immutable 10-field objective; validation audit disabled', 'cpu_executable': str(cpu_exe)}, 'parent_baseline': baseline_result, 'parent_fixed_validation': baseline_validation, 'de': incumbent_de, 'rounds': rounds, 'long_rounds': rounds, 'dls_restarts': [], 'single_mother': manifest}
    _lensde_provenance.write_json(candidate_root / 'summary.json', summary)
    return summary


def run_post_asphere_graded_direction_scouts(candidate_root: Path, system: dict, variables: list[dict], merit: dict, mother: list[float], population_size: int, options: dict, cpu_exe: Path, de_exe: Path, de_use_cuda: bool, timeout_seconds: int, started_at: float, seed_offset: int, child_cpu_workers: int) -> dict:
    """Run graded Scouts as one multi-island wave and carry the winning population."""
    scout_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_scout_count', 5, 1)
    scout_generations = _lensde_asphere_contracts.post_asphere_direction_scout_generations(options, merit)
    rng_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_rng_seed', 104729, 0)
    de_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_seed', 20260709, 0)
    scout_options = _lensde_asphere_contracts.post_asphere_fixed_contract_stage_options(options, _lensde_asphere_contracts.post_asphere_multistart_schedule(options)['long'])
    scout_options.update({'initial_search_post_asphere_lpsr_enabled': False, 'initial_search_post_asphere_inject_best': False})
    scout_specs: list[dict] = []
    for scout_index in range(1, scout_count + 1):
        label = f'graded_scout_{scout_index:02d}'
        stage_dir = native_io_paths.named_child(native_io_paths.named_child(candidate_root, 'graded_scouts'), label)
        population_seed = (rng_seed + seed_offset + scout_index * 1000 + 101) % 2000000000
        scout_de_seed = (de_seed + seed_offset + scout_index * 1000 + 501) % 2000000000
        population = _lensde_initialization.make_post_asphere_graded_population(system, variables, mother, population_size, population_seed, 0.35)
        _lensde_provenance.write_json(stage_dir / 'initialization.json', {'generator': 'graded_all_variable_local_perturbation', 'max_scale': 0.35, 'amplitude_power': 2.0, 'population_seed': population_seed, 'requested_population': population_size, 'traceability_evaluated': False})
        scout_specs.append({'label': label, 'stage_dir': stage_dir, 'system': system, 'variables': variables, 'merit': merit, 'population': population, 'population_seed': population_seed, 'de_seed': scout_de_seed, 'population_origin': 'fresh_graded_direction_scout', 'epochs': 1, 'epoch_generations': scout_generations, 'profile': 'post_asphere_direction_scout'})
    if not de_use_cuda:
        raise RuntimeError('graded Scouts require the CUDA multi-island evaluator')
    scout_results = run_post_asphere_multi_island_wave('direction_scout_batch', native_io_paths.named_child(candidate_root, 'graded_scout_batch'), scout_specs, scout_options, timeout_seconds, started_at, child_cpu_workers)
    scout_rows: list[dict] = []
    for spec, stage_result in zip(scout_specs, scout_results, strict=True):
        label = spec['label']
        stage_dir = Path(spec['stage_dir'])
        score = _lensde_provenance.read_json(stage_dir / 'score.json')
        best_merit = _lensde_configuration.finite_float(score.get('merit'), math.inf)
        coverage = _lensde_configuration.finite_float(score.get('trace_coverage'), 0.0)
        if not math.isfinite(best_merit) or not _lensde_asphere_contracts.post_asphere_trace_coverage_is_valid(coverage, scout_options):
            raise RuntimeError(f'{label} has invalid terminal merit or trace coverage: {best_merit}, {coverage}')
        scout_rows.append({'label': label, 'stage_dir': str(stage_dir), 'best_merit': best_merit, 'mean_spot_um': 1000 * _lensde_configuration.finite_float(score.get('mean_spot'), math.inf), 'population_median_merit': _lensde_asphere_contracts.post_asphere_direction_scout_population_median(stage_dir, population_size), 'trace_coverage': coverage, 'population_seed': spec['population_seed'], 'de_seed': spec['de_seed'], 'actual_generations': int(stage_result['actual_generations']), 'multi_island': True})
    scout_rows.sort(key=_lensde_asphere_contracts.post_asphere_direction_scout_sort_key)
    selected = None
    boundary_failures: list[dict] = []
    if _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, merit):
        merit_path = candidate_root / 'graded_scout_merit.json'
        _lensde_provenance.write_json(merit_path, merit)
        for row in scout_rows:
            boundary_dir = native_io_paths.named_child(native_io_paths.named_child(candidate_root, 'scout_boundaries'), row['label'])
            try:
                boundary = _lensde_asphere_contracts.recalibrate_post_asphere_stage_handoff(Path(row['stage_dir']), boundary_dir, merit_path, scout_options, cpu_exe, timeout_seconds, started_at, child_cpu_workers)
            except Exception as error:
                boundary_failures.append({'label': row['label'], 'error': str(error)})
                continue
            selected = {**row, 'handoff_dir': str(boundary_dir), 'boundary': boundary['report']}
            break
    else:
        selected = {**scout_rows[0], 'handoff_dir': scout_rows[0]['stage_dir']}
    if selected is None:
        raise RuntimeError(f'all graded Scout stage boundaries failed: {boundary_failures}')
    next_system, source_variables, next_population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(Path(selected['handoff_dir']))
    next_variables = _lensde_initialization.sync_post_asphere_variables_for_system(next_system, source_variables, merit, rebase_asphere=True)
    if len(next_population) != population_size or any((len(row) != len(next_variables) for row in next_population)):
        raise RuntimeError('selected graded Scout violates the population contract')
    continuation_seed = (int(selected['de_seed']) + 1000003) % 2000000000
    report = {'count': scout_count, 'generations_per_scout': scout_generations, 'total_scout_generations': sum((row['actual_generations'] for row in scout_rows)), 'ranking': scout_rows, 'selected': selected, 'selection_policy': 'lowest_terminal_merit_then_population_median_then_label', 'boundary_failures': boundary_failures, 'continuation_seed': continuation_seed, 'adaptive_state_policy': 'fresh_JADE_archive_and_RNG_state', 'execution': 'multi_island'}
    _lensde_provenance.write_json(candidate_root / 'graded_scout_manifest.json', report)
    return {'system': next_system, 'variables': next_variables, 'population': next_population, 'de_seed': continuation_seed, 'report': report}


def run_post_asphere_single_stage_candidate(source_dir: Path, candidate_root: Path, options: dict, cpu_exe: Path, de_exe: Path, de_use_cuda: bool, fallback_merit: dict, timeout_seconds: int, started_at: float, seed_offset: int, common_merit: dict | None=None, protected_baseline: dict | None=None, child_cpu_workers: int=0, *, prepared_input_dir: Path | None=None) -> dict:
    """One population and refresh rule, with late all-field RI when needed."""
    options = copy.deepcopy(options)
    options['initial_search_post_asphere_illumination_supervision'] = 'key_fields_then_all_fields'
    if prepared_input_dir is None:
        prepared_manifest = run_post_asphere_dual_contract_single_mother_candidate(source_dir, candidate_root, options, cpu_exe, de_exe, de_use_cuda, fallback_merit, timeout_seconds, started_at, seed_offset, common_merit=common_merit, protected_baseline=protected_baseline, child_cpu_workers=child_cpu_workers, stop_before_scout=True)
        input_dir = Path(prepared_manifest['pre_scout_dir'])
        population_size = int(prepared_manifest['requested_population'])
    else:
        input_dir = prepared_input_dir.resolve()
        population_size = len(_lensde_populations.read_population_csv(input_dir / 'initial_population.csv'))
        _lensde_provenance.write_json(candidate_root / 'archived_handoff.json', {'source_input_dir': str(input_dir), 'population_size': population_size, 'source_files': ['system_input.json', 'variables.json', 'merit.json', 'initial_population.csv']})
    system = _lensde_provenance.read_json(input_dir / 'system_input.json')
    variables_payload = _lensde_provenance.read_json(input_dir / 'variables.json')
    variables = variables_payload['variables']
    merit = _lensde_provenance.read_json(input_dir / 'merit.json')
    mother_path = input_dir / 'mother_population.csv'
    if not mother_path.is_file():
        mother_path = input_dir / 'initial_population.csv'
    mother = _lensde_populations.read_population_csv(mother_path)[0]
    scout_handoff = run_post_asphere_graded_direction_scouts(candidate_root, system, variables, merit, mother, population_size, options, cpu_exe, de_exe, de_use_cuda, timeout_seconds, started_at, seed_offset, child_cpu_workers)
    system = scout_handoff['system']
    variables = scout_handoff['variables']
    population = scout_handoff['population']
    epoch_generations = 400
    max_generations = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_stage_max_generations', 16000, 1600)
    if max_generations % epoch_generations:
        raise ValueError('single-stage maximum must be a multiple of 400 generations')
    has_vignetting = _lensde_configuration.vignetting_optimization_enabled(merit)
    supervision = 'key_fields_then_all_fields'
    late_all_fields = has_vignetting
    reserved_generations = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_stage_all_field_reserve_generations', 2400, 1200) if late_all_fields else 0
    if reserved_generations % epoch_generations:
        raise ValueError('all-field reserve must be a multiple of 400 generations')
    if late_all_fields and max_generations - reserved_generations < 1600:
        raise ValueError('single-stage budget leaves fewer than 1600 key-field generations')
    key_generations = max_generations - reserved_generations
    stage_options = _lensde_asphere_contracts.post_asphere_fixed_contract_stage_options(options, _lensde_asphere_contracts.post_asphere_multistart_schedule(options)['long'])
    stage_options.update({'initial_search_post_asphere_single_stage_active': True, 'initial_search_post_asphere_single_stage_has_vignetting': has_vignetting, 'initial_search_post_asphere_lpsr_enabled': False, 'initial_search_post_asphere_inject_best': False, 'initial_search_post_asphere_refresh': 0.5, 'initial_search_post_asphere_early_stop': True, 'initial_search_post_asphere_early_stop_min_epochs': 4, 'initial_search_post_asphere_early_stop_window': 3, 'initial_search_post_asphere_early_stop_min_rel_improve': 0.01, 'initial_search_post_asphere_early_stop_min_abs_improve': 0.0, 'initial_search_post_asphere_early_stop_min_lpsr_progress': 0.0, 'initial_search_post_asphere_early_stop_tail_epochs': 0})
    de_seed = int(scout_handoff['de_seed'])
    stages: list[dict] = []
    first_dir = candidate_root / 'single_stage_key_fields'
    first_result = run_post_asphere_multistart_de_stage('single_stage_key_fields', first_dir, system, variables, merit, population, stage_options, de_exe, de_use_cuda, key_generations // epoch_generations, de_seed, 'selected_graded_scout_terminal_population', timeout_seconds, started_at, child_cpu_workers, epoch_generations=epoch_generations, early_stop=True)
    stages.append(first_result)
    used_generations = int(first_result['actual_generations'])
    current_dir = first_dir
    current_merit = merit
    if late_all_fields:
        if _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, merit):
            boundary_dir = candidate_root / 'key_to_all_field_vignetting'
            boundary = _lensde_asphere_contracts.recalibrate_post_asphere_stage_handoff(first_dir, boundary_dir, first_dir / 'prepared_input/merit.json', {**stage_options, 'initial_search_post_asphere_illumination_supervision': 'key_fields'}, cpu_exe, timeout_seconds, started_at, child_cpu_workers)
            system = boundary['system']
            population = boundary['population']
            current_dir = boundary_dir
        else:
            system, _old_variables, population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(first_dir)
        current_merit = post_asphere_handoff.configure_illumination(merit, {**options, 'initial_search_post_asphere_illumination_supervision': 'all_fields'}, system)
        all_merit_path = candidate_root / 'merit_all_fields.json'
        _lensde_provenance.write_json(all_merit_path, current_merit)
        next_variables = _lensde_initialization.sync_post_asphere_variables_for_system(system, copy.deepcopy(variables), current_merit, rebase_asphere=True)
        all_dir = candidate_root / 'single_stage_all_fields'
        remaining_generations = max_generations - used_generations
        all_field_options = {**stage_options, 'initial_search_post_asphere_early_stop_min_epochs': max(4, reserved_generations // epoch_generations)}
        all_result = run_post_asphere_multistart_de_stage('single_stage_all_fields', all_dir, system, next_variables, current_merit, population, all_field_options, de_exe, de_use_cuda, remaining_generations // epoch_generations, de_seed + 1, 'same_population_all_field_rescore', timeout_seconds, started_at, child_cpu_workers, epoch_generations=epoch_generations, early_stop=True)
        stages.append(all_result)
        used_generations += int(all_result['actual_generations'])
        current_dir = all_dir
        if _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, current_merit):
            boundary_dir = candidate_root / 'final_vignetting'
            _lensde_asphere_contracts.recalibrate_post_asphere_stage_handoff(all_dir, boundary_dir, all_merit_path, {**stage_options, 'initial_search_post_asphere_illumination_supervision': 'all_fields'}, cpu_exe, timeout_seconds, started_at, child_cpu_workers)
            current_dir = boundary_dir
    selected_dir = candidate_root / 'best_result'
    copied = _lensde_commands.copy_candidate_artifacts(current_dir, selected_dir)
    score = _lensde_asphere_contracts.first_existing_json([selected_dir / 'score.json', selected_dir / 'checkpoint_score.json'], {})
    best_merit = _lensde_configuration.finite_float(score.get('merit'), math.inf)
    if not math.isfinite(best_merit):
        raise RuntimeError('single-stage endpoint has no finite Merit')
    illumination_values = score.get('relative_illumination_proxy_by_field')
    required_illumination = _lensde_configuration.vignetting_minimum_relative_illumination(current_merit)
    illumination_report = {'supervision': 'all_fields' if late_all_fields else supervision, 'required': required_illumination, 'field_count': len(system.get('fields', [])), 'scored_field_count': len(illumination_values) if isinstance(illumination_values, list) else 0, 'all_fields_meet_requirement': isinstance(illumination_values, list) and len(illumination_values) == len(system.get('fields', [])) and all((_lensde_configuration.finite_float(value, -math.inf) >= required_illumination for value in illumination_values)) if has_vignetting else None}
    manifest = {'flow': 'five_graded_scouts_single_stage_periodic_refresh', 'initialization': 'five graded 0.35 * u**2 whole-variable Scouts', 'scouts': scout_handoff['report'], 'refresh': 'bottom-half single-block every 400 generations', 'early_stop': 'best Merit gain <1% over 1200 generations after generation 1600', 'late_all_field_illumination': late_all_fields, 'all_field_reserve_generations': reserved_generations, 'scheduled_generations': max_generations, 'actual_generations': used_generations, 'total_actual_generations_including_scouts': used_generations + scout_handoff['report']['total_scout_generations'], 'stages': stages, 'selected_result_dir': str(selected_dir), 'illumination': illumination_report}
    _lensde_provenance.write_json(candidate_root / 'single_stage_manifest.json', manifest)
    summary = {'flow': manifest['flow'], 'source_dir': str(source_dir), 'candidate_root': str(candidate_root), 'selected_result_dir': str(selected_dir), 'best_stage_dir': str(current_dir), 'best_merit': best_merit, 'best_objective_merit': best_merit, 'best_mean_rms_um': 1000 * _lensde_configuration.finite_float(score.get('mean_spot'), math.inf), 'best_max_rms_um': 1000 * _lensde_configuration.finite_float(score.get('max_spot'), math.inf), 'protected_parent_retained': False, 'copied_files': copied, 'de': stages[-1], 'rounds': stages, 'single_stage': manifest, 'illumination': illumination_report}
    _lensde_provenance.write_json(candidate_root / 'summary.json', summary)
    return summary


def run_post_asphere_curvature_normalized_formation_periodic_candidate(source_dir: Path, candidate_root: Path, options: dict, cpu_exe: Path, de_exe: Path, de_use_cuda: bool, fallback_merit: dict, timeout_seconds: int, started_at: float, seed_offset: int, common_merit: dict | None=None, protected_baseline: dict | None=None, child_cpu_workers: int=0) -> dict:
    """New Scout initialization followed by the unchanged formal flow."""
    candidate_options = copy.deepcopy(options)
    candidate_options['initial_search_post_asphere_flow'] = 'curvature_normalized_formation_periodic'
    candidate_options['initial_search_post_asphere_direction_scout_initialization'] = 'graded_all_variable_local_perturbation_curvature_bound_normalized'
    return run_post_asphere_dual_contract_single_mother_candidate(source_dir, candidate_root, candidate_options, cpu_exe, de_exe, de_use_cuda, fallback_merit, timeout_seconds, started_at, seed_offset, common_merit=common_merit, protected_baseline=protected_baseline, child_cpu_workers=child_cpu_workers)


def run_post_asphere_single_mother_candidate(source_dir: Path, candidate_root: Path, options: dict, cpu_exe: Path, de_exe: Path, de_use_cuda: bool, fallback_merit: dict, timeout_seconds: int, started_at: float, seed_offset: int, common_merit: dict | None=None, protected_baseline: dict | None=None, child_cpu_workers: int=0) -> dict:
    if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_single_mother_dual_contract_enabled'), True):
        return run_post_asphere_dual_contract_single_mother_candidate(source_dir, candidate_root, options, cpu_exe, de_exe, de_use_cuda, fallback_merit, timeout_seconds, started_at, seed_offset, common_merit=common_merit, protected_baseline=protected_baseline, child_cpu_workers=child_cpu_workers)
    flow_started = time.time()

    def timeout_context() -> tuple[int, float]:
        limits: list[float] = []
        candidate_timeout = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_timeout', 0, 0)
        if candidate_timeout > 0:
            limits.append(candidate_timeout - (time.time() - flow_started))
        outer_remaining = _lensde_execution.remaining_timeout_seconds(timeout_seconds, started_at)
        if outer_remaining is not None:
            limits.append(outer_remaining)
        if not limits:
            return (0, time.time())
        allowed = min(limits)
        if allowed <= 0.0:
            raise TimeoutError('post-asphere single-mother candidate timeout exhausted')
        return (max(1, int(math.ceil(allowed))), time.time())
    prepared = _lensde_asphere_contracts.post_asphere_prepare_case(source_dir, candidate_root / 'input', options, fallback_merit, common_merit=common_merit, promotion_handoff=protected_baseline)
    merit = prepared['merit']
    merit_path = prepared['merit_path']
    base_variables = prepared['variables']
    incumbent_system = prepared['system']
    incumbent_variables = base_variables
    initial_values = [_lensde_populations.variable_current_value_for_system(incumbent_system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in incumbent_variables]
    requested_population = _lensde_asphere_contracts.post_asphere_multistart_input_population_size(options, _lensde_populations.population_sizing_variable_count(base_variables))
    population_size = requested_population
    population_scale = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_scale', 0.35, 0.0)
    rng_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_rng_seed', 104729, 0)
    de_seed = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_seed', 20260709, 0)
    schedule = _lensde_asphere_contracts.post_asphere_multistart_schedule(options)
    long_config = schedule['long']
    max_rounds = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_max_rounds', 12, 1)
    confirmation_generations = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_confirmation_generations', 1200, 1)
    confirmation_epochs = max(1, int(math.ceil(confirmation_generations / max(1, long_config['de_generations']))))
    donor_fraction = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_long_donor_fraction', 0.05, 0.0)
    baseline_dir = candidate_root / 'parent_baseline'
    baseline_result = copy.deepcopy(protected_baseline) if isinstance(protected_baseline, dict) else {}
    protected_text = str(baseline_result.get('result_dir') or '').strip()
    protected_dir = Path(protected_text) if protected_text else None
    if protected_dir is not None and protected_dir.is_dir():
        baseline_result['copied_files'] = _lensde_commands.copy_candidate_artifacts(protected_dir, baseline_dir)
        baseline_result['result_dir'] = str(baseline_dir)
        baseline_result['protected'] = True
    else:
        stage_timeout, stage_started = timeout_context()
        baseline_result = run_post_asphere_promotion_recheck(source_dir, baseline_dir, merit_path, options, cpu_exe, stage_timeout, stage_started, child_cpu_workers)
        baseline_result['protected'] = True
    baseline_merit = _lensde_configuration.finite_float(baseline_result.get('merit'), math.inf)
    if not math.isfinite(baseline_merit):
        raise RuntimeError('single-mother protected baseline merit is not finite')
    long_stage_options = copy.deepcopy(options)
    long_stage_options.update({'initial_search_post_asphere_hybrid_parents': long_config['hybrid_parents'], 'initial_search_post_asphere_refresh': long_config['refresh'], 'initial_search_post_asphere_early_stop_use_min_improvement_threshold': False})
    initial_conic_nonzero = sum((1 for variable, value in zip(base_variables, initial_values, strict=True) if str(variable.get('kind', '')).lower() == 'conic' and abs(value) > 1e-30))
    initial_asphere_nonzero = sum((1 for variable, value in zip(base_variables, initial_values, strict=True) if str(variable.get('kind', '')).lower() == 'asphere_coeff' and abs(value) > 1e-30))
    manifest: dict = {'flow': 'single_mother_sag_restart', 'status': 'running', 'source_dir': str(source_dir), 'selection_policy': 'strict_cpu_merit_improvement_only', 'continuation_policy': 'significant_success_resets; one_1200_generation_confirmation; then_stop', 'population_policy': 'one_all_variable_bootstrap_then_mixed_quality_structure_restarts', 'requested_population': requested_population, 'explicit_population': population_size, 'variable_count': len(base_variables), 'initial_conic_nonzero': initial_conic_nonzero, 'initial_asphere_nonzero': initial_asphere_nonzero, 'max_rounds_safety_limit': max_rounds, 'confirmation_generations': confirmation_generations, 'hard_no_progress_generations': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_hard_no_progress_generations', 3200, 1), 'stages': {}, 'decisions': []}
    manifest_path = candidate_root / 'single_mother_manifest.json'
    _lensde_provenance.write_json(manifest_path, manifest)
    rounds: list[dict] = []
    archive_dirs: list[Path] = []
    incumbent_merit = baseline_merit
    incumbent_stage_dir = baseline_dir
    incumbent_label = 'protected_parent'
    incumbent_score: dict = {}
    incumbent_de: dict = {}
    reference_merit = baseline_merit
    no_progress_generations = 0
    confirmation_next = False
    cumulative_generations = 0
    stop_reason = 'max_rounds_safety_limit'
    for round_index in range(1, max_rounds + 1):
        bootstrap = round_index == 1
        confirmation_round = bool(confirmation_next and (not bootstrap))
        restart_variables = _lensde_commands.sync_variables_from_system(copy.deepcopy(base_variables), incumbent_system, rebase_asphere=True)
        population_seed = (rng_seed + seed_offset + 50000 + round_index * 1009) % 2000000000
        if bootstrap:
            restart_values = [_lensde_populations.variable_current_value_for_system(incumbent_system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in restart_variables]
            restart_population = _lensde_initialization.make_post_asphere_population(incumbent_system, restart_variables, restart_values, population_size, population_seed, population_scale)
            seed_summary = {'generator': 'all_variable_bootstrap', 'anchor_rows': 1, 'donor_rows': 0, 'fresh_rows': population_size - 1, 'population_scale': population_scale}
            population_origin = 'single_mother_all_variable_bootstrap'
            label = 'bootstrap_round_01'
        else:
            restart_population, seed_summary = _lensde_asphere_population.make_post_asphere_mixed_restart_population(incumbent_system, restart_variables, archive_dirs, population_size, population_seed, donor_fraction, options)
            population_origin = 'single_mother_mixed_quality_structure_restart'
            label = f'confirmation_round_{round_index:02d}' if confirmation_round else f'mother_round_{round_index:02d}'
        stage_dir = native_io_paths.named_child(candidate_root / 'rounds', label)
        epochs = confirmation_epochs if confirmation_round else long_config['outer_epochs']
        stage_timeout, stage_started = timeout_context()
        de_result = run_post_asphere_multistart_de_stage(label, stage_dir, incumbent_system, restart_variables, merit, restart_population, long_stage_options, de_exe, de_use_cuda, epochs, (de_seed + seed_offset + 30000 + round_index) % 2000000000, population_origin, stage_timeout, stage_started, child_cpu_workers, epoch_generations=long_config['de_generations'], early_stop=True)
        actual_generations = int(de_result.get('actual_generations', 0) or 0)
        if confirmation_round and actual_generations > confirmation_generations:
            raise RuntimeError(f'confirmation used {actual_generations} generations; limit is {confirmation_generations}')
        cumulative_generations += actual_generations
        audit_timeout, audit_started = timeout_context()
        audit = run_post_asphere_multistart_audit(label, stage_dir, native_io_paths.named_child(candidate_root / 'audits', label), merit_path, options, cpu_exe, audit_timeout, audit_started, child_cpu_workers, 1)
        child_merit = _lensde_configuration.finite_float(audit.get('score', {}).get('merit'), math.inf)
        incumbent_before = incumbent_merit
        accepted = _lensde_asphere_contracts.is_strict_merit_improvement(child_merit, incumbent_merit)
        stage_boundary_vignetting: dict = {'performed': False, 'reason': 'stage_not_accepted'}
        if accepted:
            incumbent_merit = child_merit
            incumbent_label = label
            incumbent_score = copy.deepcopy(audit.get('score', {}))
            incumbent_de = de_result
            if _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, prepared['merit']):
                boundary_dir = native_io_paths.named_child(native_io_paths.named_child(candidate_root, 'stage_boundaries') / 'accepted_round_to_next', label)
                boundary_timeout, boundary_started = timeout_context()
                boundary = _lensde_asphere_contracts.recalibrate_post_asphere_stage_handoff(stage_dir, boundary_dir, merit_path, long_stage_options, cpu_exe, boundary_timeout, boundary_started, child_cpu_workers)
                incumbent_stage_dir = boundary_dir
                incumbent_system = boundary['system']
                incumbent_variables = boundary['variables']
                stage_boundary_vignetting = boundary['report']
            else:
                incumbent_stage_dir = stage_dir
                incumbent_system, incumbent_variables, _population = _lensde_asphere_contracts.post_asphere_multistart_endpoint(stage_dir)
        no_progress_generations += actual_generations
        decision = _lensde_asphere_contracts.post_asphere_single_mother_progress_decision(reference_merit, incumbent_merit, confirmation_round, no_progress_generations, options)
        candidate = {'label': label, 'stage_dir': str(stage_dir), 'bootstrap': bootstrap, 'confirmation_round': confirmation_round, 'de': de_result, 'audit': audit, 'population_seed': population_seed, 'seed_population': seed_summary, 'incumbent_merit_before': incumbent_before, 'child_merit': child_merit, 'pre_vignetting_calibration_merit': child_merit, 'accepted': accepted, 'stage_boundary_handoff_dir': str(incumbent_stage_dir) if accepted else '', 'stage_boundary_vignetting': stage_boundary_vignetting, 'reference_merit_before': reference_merit, 'decision': decision, 'cumulative_generations': cumulative_generations}
        rounds.append(candidate)
        archive_dirs.append(stage_dir)
        manifest['stages'][label] = _lensde_asphere_contracts.compact_post_asphere_multistart_candidate(candidate) | {'bootstrap': bootstrap, 'confirmation_round': confirmation_round, 'population_seed': population_seed, 'seed_population': seed_summary, 'incumbent_merit_before': incumbent_before, 'child_merit': child_merit, 'pre_vignetting_calibration_merit': child_merit, 'accepted': accepted, 'stage_boundary_handoff_dir': str(incumbent_stage_dir) if accepted else '', 'stage_boundary_vignetting': stage_boundary_vignetting, 'decision': decision, 'cumulative_generations': cumulative_generations}
        manifest['decisions'].append({'stage': label, 'incumbent_merit_before': incumbent_before, 'child_merit': child_merit, 'accepted': accepted, **decision})
        if decision['significant']:
            reference_merit = incumbent_merit
            no_progress_generations = 0
            confirmation_next = False
        elif decision['action'] == 'run_one_confirmation':
            confirmation_next = True
        else:
            stop_reason = str(decision['action'])
            manifest.update({'current_incumbent_merit': incumbent_merit, 'reference_merit': reference_merit, 'no_progress_generations': no_progress_generations, 'stop_reason': stop_reason})
            _lensde_provenance.write_json(manifest_path, manifest)
            break
        manifest.update({'current_incumbent_merit': incumbent_merit, 'reference_merit': reference_merit, 'no_progress_generations': no_progress_generations, 'next_round_confirmation': confirmation_next})
        _lensde_provenance.write_json(manifest_path, manifest)
    protected_parent_retained = not _lensde_asphere_contracts.is_strict_merit_improvement(incumbent_merit, baseline_merit)
    best_stage_dir = baseline_dir if protected_parent_retained else incumbent_stage_dir
    best_merit = baseline_merit if protected_parent_retained else incumbent_merit
    selected_mean_rms_um = _lensde_configuration.finite_float(baseline_result.get('common_mean_spot_um'), math.inf) if protected_parent_retained else _lensde_configuration.finite_float(incumbent_score.get('mean_rms_um'), math.inf)
    selected_max_rms_um = _lensde_configuration.finite_float(baseline_result.get('common_max_spot_um'), math.inf) if protected_parent_retained else _lensde_configuration.finite_float(incumbent_score.get('max_rms_um'), math.inf)
    selected_dir = candidate_root / 'best_result'
    copied = _lensde_commands.copy_candidate_artifacts(best_stage_dir, selected_dir)
    manifest.update({'status': 'complete', 'wall_seconds': time.time() - flow_started, 'rounds_completed': len(rounds), 'rounds_accepted': sum((1 for row in rounds if row['accepted'])), 'confirmation_rounds': sum((1 for row in rounds if row['confirmation_round'])), 'cumulative_generations': cumulative_generations, 'selected_candidate': incumbent_label, 'selected_merit': best_merit, 'protected_parent_retained': protected_parent_retained, 'stop_reason': stop_reason})
    _lensde_provenance.write_json(manifest_path, manifest)
    summary = {'flow': 'single_mother_sag_restart', 'source_dir': str(source_dir), 'candidate_root': str(candidate_root), 'selected_result_dir': str(selected_dir), 'best_stage_dir': str(best_stage_dir), 'best_merit': best_merit, 'best_mean_rms_um': selected_mean_rms_um, 'best_max_rms_um': selected_max_rms_um, 'protected_parent_retained': protected_parent_retained, 'copied_files': copied, 'prepared': {'selected_surface_indices': prepared['selected_surface_indices'], 'lens_groups': prepared['lens_groups'], 'population_rows': population_size, 'requested_population_rows': requested_population, 'variable_count': len(base_variables), 'population_scale': population_scale, 'zero_asphere_coefficients': prepared.get('zero_asphere_coefficients', False), 'initial_conic_nonzero': initial_conic_nonzero, 'initial_asphere_nonzero': initial_asphere_nonzero}, 'evaluators': {'de': 'CUDA' if de_use_cuda else 'CPU', 'de_executable': str(de_exe), 'selection_and_recheck': 'CPU merit only', 'cpu_executable': str(cpu_exe)}, 'parent_baseline': baseline_result, 'de': incumbent_de, 'rounds': rounds, 'long_rounds': rounds, 'dls_restarts': [], 'single_mother': manifest}
    _lensde_provenance.write_json(candidate_root / 'summary.json', summary)
    return summary


def run_post_asphere_candidate(source_dir: Path, candidate_root: Path, options: dict, cpu_exe: Path, de_exe: Path, de_use_cuda: bool, fallback_merit: dict, timeout_seconds: int, started_at: float, seed_offset: int, common_merit: dict | None=None, protected_baseline: dict | None=None, child_cpu_workers: int=0) -> dict:

    def timeout_context() -> tuple[int, float]:
        post_timeout = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_timeout', 0, 0)
        if post_timeout <= 0:
            return (timeout_seconds, started_at)
        if timeout_seconds <= 0:
            return (post_timeout, time.time())
        remaining = int(math.ceil(_lensde_execution.remaining_timeout_seconds(timeout_seconds, started_at) or post_timeout))
        return (max(1, min(post_timeout, remaining)), time.time())
    prepared = _lensde_asphere_contracts.post_asphere_prepare_case(source_dir, candidate_root / 'input', options, fallback_merit, common_merit=common_merit, promotion_handoff=protected_baseline)
    baseline_dir = candidate_root / 'parent_baseline'
    baseline_result = copy.deepcopy(protected_baseline) if isinstance(protected_baseline, dict) else {}
    protected_text = str(baseline_result.get('result_dir') or '').strip()
    protected_dir = Path(protected_text) if protected_text else None
    if protected_dir is not None and protected_dir.exists() and protected_dir.is_dir():
        baseline_result['copied_files'] = _lensde_commands.copy_candidate_artifacts(protected_dir, baseline_dir)
        baseline_result['result_dir'] = str(baseline_dir)
        baseline_result['protected'] = True
    else:
        stage_timeout, stage_started = timeout_context()
        baseline_result = run_post_asphere_promotion_recheck(source_dir, baseline_dir, prepared['merit_path'], options, cpu_exe, stage_timeout, stage_started, child_cpu_workers)
        baseline_result['protected'] = True
    baseline_merit = _lensde_configuration.finite_float(baseline_result.get('merit'), math.inf)
    de_dir = candidate_root / 'jade_lpsr'
    de_command = _lensde_asphere_contracts.post_asphere_de_command(de_exe, prepared, de_dir, options, seed_offset, use_cuda_evaluator=de_use_cuda)
    stage_timeout, stage_started = timeout_context()
    de_result = _lensde_asphere_contracts.run_command_logged(de_command, de_dir, stage_timeout, stage_started, child_cpu_workers)
    variables = prepared['variables']
    merit_path = prepared['merit_path']
    best_dir = baseline_dir if math.isfinite(baseline_merit) else candidate_root / 'input'
    best_merit = baseline_merit
    best_system = _lensde_asphere_contracts.first_existing_json([baseline_dir / 'system.json', baseline_dir / 'checkpoint_system.json', prepared['system_path']], prepared['system'])
    de_merit = _lensde_configuration.finite_float(de_result.get('merit'), math.inf)
    de_result['protected_parent_merit'] = baseline_merit
    de_result['improved_over_protected_parent'] = _lensde_asphere_contracts.is_strict_merit_improvement(de_merit, baseline_merit)
    if de_result['improved_over_protected_parent']:
        de_result['pre_vignetting_calibration_merit'] = de_merit
        if _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, prepared['merit']):
            de_boundary_dir = native_io_paths.named_child(candidate_root, 'stage_boundaries') / 'de_to_dls'
            try:
                boundary_timeout, boundary_started = timeout_context()
                de_boundary = _lensde_asphere_contracts.finalize_post_asphere_zemax_vignetting(de_dir, de_boundary_dir, merit_path, options, cpu_exe, boundary_timeout, boundary_started, child_cpu_workers, for_optimization=True)
            except Exception as exc:
                de_result.update({'stage_boundary_vignetting_passed': False, 'stage_boundary_vignetting_error': str(exc), 'accepted_for_dls': False})
            else:
                best_dir = de_boundary_dir
                best_merit = de_merit
                best_system = de_boundary['system']
                de_result.update({'stage_boundary_vignetting_passed': True, 'stage_boundary_handoff_dir': str(de_boundary_dir), 'stage_boundary_vignetting': de_boundary['report'], 'accepted_for_dls': True})
        else:
            best_dir = de_dir
            best_merit = de_merit
            best_system = _lensde_asphere_contracts.first_existing_json([de_dir / 'checkpoint_system.json', de_dir / 'system.json', prepared['system_path']], prepared['system'])
            de_result['accepted_for_dls'] = True
    dls_runs: list[dict] = []
    stale_count = 0
    max_restarts = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_dls_max_restarts', 6, 0)
    patience = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_dls_patience', 2, 1)
    min_rel = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_dls_min_rel_improve', 0.003, 0.0)
    min_abs = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_dls_min_abs_improve', 0.001, 0.0)
    for restart_index in range(1, max_restarts + 1):
        restart_dir = candidate_root / 'dls_restarts' / f'restart_{restart_index:02d}'
        restart_system = copy.deepcopy(best_system if isinstance(best_system, dict) else prepared['system'])
        restart_variables = _lensde_commands.sync_variables_from_system([copy.deepcopy(variable) for variable in variables], restart_system, rebase_asphere=True)
        restart_system_path = restart_dir / 'system_input.json'
        restart_variables_path = restart_dir / 'variables.json'
        _lensde_provenance.write_json(restart_system_path, restart_system)
        _lensde_provenance.write_json(restart_variables_path, {'variables': restart_variables})
        iterations = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_dls_final_iter' if restart_index == max_restarts else 'initial_search_post_asphere_dls_iter', 180, 1)
        dls_command = _lensde_asphere_contracts.post_asphere_dls_command(cpu_exe, restart_system_path, merit_path, restart_variables_path, restart_dir, options, iterations)
        stage_timeout, stage_started = timeout_context()
        result = _lensde_asphere_contracts.run_command_logged(dls_command, restart_dir, stage_timeout, stage_started, child_cpu_workers)
        merit = _lensde_configuration.finite_float(result.get('merit'), math.inf)
        previous_best = best_merit
        abs_gain = previous_best - merit if math.isfinite(previous_best) and math.isfinite(merit) else -math.inf
        rel_gain = abs_gain / max(abs(previous_best), 1e-12) if math.isfinite(abs_gain) else -math.inf
        accepted_raw = _lensde_asphere_contracts.is_strict_merit_improvement(merit, previous_best)
        accepted = accepted_raw
        stage_boundary_vignetting: dict = {'performed': False, 'reason': 'raw_merit_not_improved'}
        accepted_system = _lensde_asphere_contracts.first_existing_json([restart_dir / 'checkpoint_system.json', restart_dir / 'system.json'], restart_system)
        accepted_dir = restart_dir
        if accepted_raw and _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, prepared['merit']):
            boundary_dir = native_io_paths.named_child(candidate_root, 'stage_boundaries') / 'dls_to_next' / f'restart_{restart_index:02d}'
            try:
                boundary_timeout, boundary_started = timeout_context()
                boundary = _lensde_asphere_contracts.finalize_post_asphere_zemax_vignetting(restart_dir, boundary_dir, merit_path, options, cpu_exe, boundary_timeout, boundary_started, child_cpu_workers, for_optimization=True)
            except Exception as exc:
                accepted = False
                stage_boundary_vignetting = {'performed': True, 'passed': False, 'error': str(exc)}
            else:
                accepted_dir = boundary_dir
                accepted_system = boundary['system']
                stage_boundary_vignetting = boundary['report']
        meaningful_improvement = accepted and (abs_gain >= min_abs or rel_gain >= min_rel)
        incumbent_after_merit = merit if accepted else previous_best
        result.update({'restart_index': restart_index, 'restart_mode': 'continue_from_incumbent', 'previous_best_merit': previous_best, 'incumbent_after_merit': incumbent_after_merit, 'abs_gain': abs_gain, 'rel_gain': rel_gain, 'pre_vignetting_calibration_merit': merit, 'accepted_by_raw_merit': accepted_raw, 'accepted': accepted, 'improved': accepted, 'meaningful_improvement': meaningful_improvement, 'stage_boundary_handoff_dir': str(accepted_dir) if accepted else '', 'stage_boundary_vignetting': stage_boundary_vignetting})
        dls_runs.append(result)
        if accepted:
            best_merit = merit
            best_dir = accepted_dir
            best_system = accepted_system
        if meaningful_improvement:
            stale_count = 0
        else:
            stale_count += 1
        if stale_count >= patience:
            result['early_stop_triggered'] = True
            break
    selected_dir = candidate_root / 'best_result'
    copied = _lensde_commands.copy_candidate_artifacts(best_dir, selected_dir)
    summary = {'source_dir': str(source_dir), 'candidate_root': str(candidate_root), 'selected_result_dir': str(selected_dir), 'best_stage_dir': str(best_dir), 'best_merit': best_merit, 'protected_parent_retained': Path(best_dir).resolve() == baseline_dir.resolve(), 'copied_files': copied, 'prepared': {'selected_surface_indices': prepared['selected_surface_indices'], 'lens_groups': prepared['lens_groups'], 'population_rows': prepared['population_rows'], 'variable_count': len(variables)}, 'evaluators': {'de': 'CUDA' if de_use_cuda else 'CPU', 'de_executable': str(de_exe), 'recheck_and_dls': 'CPU', 'cpu_executable': str(cpu_exe)}, 'parent_baseline': baseline_result, 'de': de_result, 'dls_restarts': dls_runs}
    _lensde_provenance.write_json(candidate_root / 'summary.json', summary)
    return summary


def run_post_asphere_refine_stage(run_dir: Path, material_summary: dict, options: dict, exe: Path, timeout_seconds: int, started_at: float, fallback_merit: dict, *, stop_after_handoff: bool=False, post_work_dir: Path | None=None) -> dict:
    options = seed_protocol.apply_options(copy.deepcopy(options))
    cpu_exe = _lensde_provenance.exe_path(False)
    options[state.POST_ASPHERE_AIMING_CONTRACT_OPTION] = _lensde_configuration.canonical_aiming_mode(options.get('aiming'), 'robust')
    require_real_materials = _lensde_asphere_contracts.post_asphere_requires_real_materials(material_summary)
    post_flow = _lensde_asphere_contracts.canonical_post_asphere_flow(options.get('initial_search_post_asphere_flow'))
    if post_flow == 'single_stage':
        options = copy.deepcopy(options)
        options['initial_search_post_asphere_illumination_supervision'] = 'key_fields_then_all_fields'
    de_use_cuda = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_de_use_cuda'), True)
    de_exe = state.CPP_ROOT / 'build' / 'lensde_de_cuda.exe' if de_use_cuda else cpu_exe
    trace_contract = _lensde_asphere_contracts.post_asphere_trace_coverage_contract(options)
    post_summary: dict = {'enabled': _lensde_commands.option_truthy(options.get('initial_search_post_asphere_enabled'), False), 'flow': post_flow, 'lens_indices': _lensde_commands.parse_one_based_index_list(options.get('initial_search_post_asphere_lens_indices')), 'de_evaluator': 'CUDA' if de_use_cuda else 'CPU', 'de_executable': str(de_exe), 'allow_near_trace_infeasible': trace_contract['allow_near_trace_infeasible'], 'min_trace_coverage': trace_contract['required_min_trace_coverage'], 'trace_coverage_contract': trace_contract, 'constraint_handling': str(options.get('initial_search_post_asphere_constraint_handling') or 'feasibility_rules'), 'recheck_and_dls_evaluator': 'CPU', 'cpu_executable': str(cpu_exe), 'material_terminal_mode': str(material_summary.get('material_terminal_mode') or ''), 'require_real_materials': require_real_materials}
    if not post_summary['enabled']:
        return post_summary
    if not post_summary['lens_indices']:
        post_summary.update({'skipped': True, 'reason': 'no post-asphere lens indices configured'})
        return post_summary
    if de_use_cuda and (not de_exe.exists()):
        post_summary.update({'skipped': True, 'reason': f'post-asphere CUDA DE executable not found: {de_exe}'})
        return post_summary
    key_dir = run_dir / 'de_dls_key_outputs'
    canonical_post_dir = key_dir / 'post_asphere_refine'
    post_dir = post_work_dir or _lensde_asphere_contracts.post_asphere_work_dir(run_dir)
    candidate_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_candidate_count', 1, 1)
    requested_workers = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_workers', 1, 1)
    candidate_workers = min(candidate_count, requested_workers)
    if post_flow == 'single_stage':
        candidate_workers = 1
    requested_child_cpu_workers = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_child_cpu_workers', 0, 0)
    child_cpu_workers = requested_child_cpu_workers
    if child_cpu_workers <= 0 and candidate_workers > 1:
        child_cpu_workers = max(1, (os.cpu_count() or 1) // candidate_workers)
    candidate_pool_audit: list[dict] = []
    candidate_pool = _lensde_asphere_contracts.post_asphere_candidate_source_dirs(material_summary, run_dir, audit=candidate_pool_audit)
    remaining_handoff_count = max(0, len(candidate_pool) - 1)
    handoff_workers = min(remaining_handoff_count, max(1, requested_workers))
    handoff_child_cpu_workers = requested_child_cpu_workers
    if handoff_child_cpu_workers <= 0 and handoff_workers > 1:
        handoff_child_cpu_workers = max(1, (os.cpu_count() or 1) // handoff_workers)
    primary_source_merit, primary_source_merit_path = _lensde_asphere_contracts.post_asphere_primary_source_merit(candidate_pool, fallback_merit)
    common_merit = _lensde_asphere_contracts.post_asphere_merit_from_source(primary_source_merit, options, fallback_merit if isinstance(fallback_merit, dict) else None)
    common_merit_path = post_dir / 'common_post_asphere_merit.json'
    _lensde_provenance.write_json(common_merit_path, common_merit)
    post_summary.update({'output_dir': str(post_dir), 'canonical_output_dir': str(canonical_post_dir), 'compact_work_dir_used': post_dir != canonical_post_dir, 'candidate_count': candidate_count, 'candidate_workers': candidate_workers, 'child_cpu_workers': child_cpu_workers, 'handoff_workers': handoff_workers, 'handoff_child_cpu_workers': handoff_child_cpu_workers, 'candidate_pool_dirs': [str(path) for path in candidate_pool], 'candidate_pool_audit': candidate_pool_audit, 'merit_inheritance': {'source': 'primary_pre_asphere_candidate' if primary_source_merit_path is not None else 'global_fallback', 'source_merit_path': str(primary_source_merit_path) if primary_source_merit_path is not None else None, 'quality_chief_ray_angle_tol': common_merit.get('quality_chief_ray_angle_tol'), 'quality_chief_ray_angle_tightened_tol': common_merit.get('quality_chief_ray_angle_tightened_tol'), 'chief_ray_angle_upper': common_merit.get('chief_ray_angle_upper')}, 'max_order': _lensde_initialization.post_asphere_max_order(options), 'bound_mode': str(options.get('initial_search_post_asphere_bound_mode') or 'adaptive_sag'), 'rms_mode': _lensde_defaults.canonical_image_quality_metric(options.get('initial_search_post_asphere_rms_mode'), 'rms_spot_wavefront'), 'common_merit_path': str(common_merit_path), 'multistart_config': {'schedule': _lensde_asphere_contracts.post_asphere_multistart_schedule(options), 'population_scale': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_scale', 0.35, 0.0), 'multi_island_enabled': _lensde_commands.option_truthy(options.get('initial_search_post_asphere_multistart_multi_island_enabled'), True), 'long_rounds': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_rounds', 3, 0), 'long_outer': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_outer', 20, 1), 'long_de_gen': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_de_gen', 400, 1), 'long_hybrid_parents': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_hybrid_parents', 10, 1), 'long_refresh': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_long_refresh', 0.25, 0.0), 'long_donor_fraction': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_long_donor_fraction', 0.05, 0.0), 'long_population_scales': _lensde_asphere_contracts.post_asphere_long_population_scales(options), 'audit_passes': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_multistart_audit_passes', 2, 1), 'long_selection_metric': 'strict_cpu_scalar_merit', 'long_promotion_rule': 'child_merit < incumbent_merit', 'final_long_early_stop_threshold_policy': 'min(relative, absolute)' if _lensde_commands.option_truthy(options.get('initial_search_post_asphere_final_long_early_stop_use_min_threshold'), True) else 'max(relative, absolute)', 'dls_enabled': False, 'variable_activation_order_tested': False} if post_flow == 'conic_multistart' else None, 'single_mother_config': {'population_policy': 'five_direction_scouts_then_rank1_formation_then_periodic_top_half_correlated_sag_long_run', 'population_rows': 'absolute_override_or_ceil(population_multiplier*variable_count)', 'population_multiplier': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_population_multiplier', 5.0, 0.01), 'direction_scout_count': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_single_mother_scout_count', 5, 1), 'direction_scout_generations': _lensde_asphere_contracts.post_asphere_direction_scout_generations(options, common_merit), 'direction_scout_budget_mode': 'fixed_g400_formation_handoff' if post_flow == 'curvature_normalized_formation_periodic' else 'vignetting' if _lensde_configuration.vignetting_optimization_enabled(common_merit) else 'no_vignetting', 'direction_scout_generator': 'graded_all_variable_local_perturbation_curvature_bound_normalized' if post_flow == 'curvature_normalized_formation_periodic' else 'legacy_all_variable_local_perturbation_A', 'direction_commit_policy': 'rank1_terminal_population_carry', 'checkpoint_branching': False, 'periodic_refresh': {'max_rounds': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_periodic_refresh_max_rounds', 20, 1), 'generations_per_round': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_periodic_refresh_generations', 400, 1), 'replacement_fraction': 0.5, 'protected_fraction': 0.5, 'parent_count': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_periodic_refresh_parent_count', 16, 1), 'sag_um': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_periodic_refresh_sag_um', 0.3, 0.0), 'correlation_length': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_periodic_refresh_correlation_length', 0.35, 0.001), 'geometry_fraction': 0.0, 'generation_mode': 'top_half_single_surface_correlated_sag', 'pre_stage_refresh': True, 'trace_feasible_gate': False}, 'legacy_sag_height_caps_um': _lensde_asphere_contracts.post_asphere_sag_height_caps_um(options), 'legacy_sag_correlation_length': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_sag_correlation_length', 0.35, 0.001), 'long_outer': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_outer', 20, 1), 'long_de_gen': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_de_gen', 400, 1), 'long_hybrid_parents': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_long_hybrid_parents', 10, 1), 'long_refresh': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_long_refresh', 0.25, 0.0), 'donor_fraction': _lensde_commands.option_float_from(options, 'initial_search_post_asphere_long_donor_fraction', 0.05, 0.0), 'selection_metric': 'strict_cpu_scalar_merit'} if post_flow in {'curvature_normalized_formation_periodic', 'single_mother'} else None, 'edge_constraints': {'source': 'global_merit', **{key: common_merit.get(key) for key in state.GLOBAL_EDGE_MERIT_KEYS if key in common_merit}}})
    if not candidate_pool:
        reason = 'no eligible real-material candidates available' if require_real_materials else 'no material/fixed-glass candidates available'
        post_summary.update({'skipped': True, 'reason': reason})
        _lensde_asphere_population.write_post_asphere_summary(post_dir, canonical_post_dir, post_summary)
        return post_summary
    contract_source_system = _lensde_asphere_contracts.first_existing_json([candidate_pool[0] / 'system.json', candidate_pool[0] / 'checkpoint_system.json', candidate_pool[0] / 'system_input.json', candidate_pool[0] / 'input_system.json'], {})
    common_merit = post_asphere_handoff.configure_illumination(common_merit, options, contract_source_system)
    _lensde_provenance.write_json(common_merit_path, common_merit)
    post_summary['illumination_supervision'] = {'mode': str(options.get('initial_search_post_asphere_illumination_supervision') or 'key_fields'), 'reference_radii': common_merit.get(post_asphere_handoff.RADII), 'field_indices': common_merit.get(post_asphere_handoff.INDICES), 'image_quality_and_physical_matching': 'all_fields', 'periodic_stage': 'all_fields' if str(options.get('initial_search_post_asphere_illumination_supervision') or 'key_fields').strip().lower() in {'key_fields_then_all_fields', 'late_all_fields'} else str(options.get('initial_search_post_asphere_illumination_supervision') or 'key_fields')}
    field_reference_radius = _lensde_fields.post_asphere_field_reference_radius(options, contract_source_system)
    field_contract_options = copy.deepcopy(options)
    if field_reference_radius > 1e-12:
        field_contract_options[state.POST_ASPHERE_FIELD_REFERENCE_RADIUS_OPTION] = field_reference_radius
    screening_options = copy.deepcopy(field_contract_options)
    screening_contract: dict[str, Any] | None = None
    if post_flow in {'curvature_normalized_formation_periodic', 'single_mother', 'single_stage'} and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_single_mother_fixed_contract_enabled'), True):
        fixed_main_count = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_field_count', 7, 2)
        main_probe_options = copy.deepcopy(field_contract_options)
        main_probe_options.update({'initial_search_post_asphere_field_count': fixed_main_count, 'initial_search_post_asphere_field_contract_policy': 'locked', 'initial_search_post_asphere_adaptive_fields_enabled': False, 'initial_search_post_asphere_adaptive_field_max_additions': 0})
        probe_system = copy.deepcopy(contract_source_system)
        for surface_index in _lensde_initialization.selected_post_asphere_surface_indices(probe_system, options):
            if 0 <= surface_index < len(probe_system.get('surfaces', []) or []):
                probe_system['surfaces'][surface_index]['kind'] = 'even_asphere'
        variables_payload = _lensde_asphere_contracts.first_existing_json([candidate_pool[0] / 'variables.json'], {'variables': []})
        probe_variables = variables_payload.get('variables', variables_payload) if isinstance(variables_payload, dict) else variables_payload
        if not isinstance(probe_variables, list):
            probe_variables = []
        _screen_probe_system, main_probe = run_post_asphere_field_probe(probe_system, probe_variables, common_merit_path, native_io_paths.named_child(native_io_paths.named_child(post_dir, 'screening_contract'), 'main_field_probe'), main_probe_options, cpu_exe, timeout_seconds, started_at, child_cpu_workers)
        fixed_main_nodes = list(main_probe.get('selected_nodes', []))
        if len(fixed_main_nodes) != fixed_main_count:
            raise RuntimeError(f'screening fixed-contract probe returned {len(fixed_main_nodes)} main fields; expected {fixed_main_count}')
        fixed_optimizer_nodes, fixed_edge_nodes = _lensde_asphere_contracts.post_asphere_fixed_optimizer_field_nodes(fixed_main_nodes, options)
        screening_options.update({'initial_search_post_asphere_field_count': len(fixed_optimizer_nodes), 'initial_search_post_asphere_fixed_main_field_count': fixed_main_count, 'initial_search_post_asphere_preferred_field_nodes': fixed_optimizer_nodes, 'initial_search_post_asphere_field_coordinate': 'normalized_field', 'initial_search_post_asphere_field_contract_policy': 'locked', 'initial_search_post_asphere_adaptive_fields_enabled': False, 'initial_search_post_asphere_adaptive_field_max_additions': 0})
        screening_contract = {'policy': 'single_mother_fixed_optimizer_contract', 'main_field_count': fixed_main_count, 'main_nodes': fixed_main_nodes, 'edge_nodes': fixed_edge_nodes, 'optimizer_field_count': len(fixed_optimizer_nodes), 'optimizer_nodes': fixed_optimizer_nodes, 'source': str(candidate_pool[0])}
    handoff_rows: list[dict] = []
    selected_pool_source = candidate_pool[0]
    selected_before = material_summary.get('selected_final_result', {}) if isinstance(material_summary.get('selected_final_result'), dict) else {}
    selected_before_text = str(selected_before.get('selected_result_dir') or selected_before.get('target_dir') or selected_before.get('source_dir') or '').strip()
    selected_before_dir = Path(selected_before_text) if selected_before_text else None

    def run_handoff_recheck(pool_index: int, source_dir: Path, recheck_options: dict, cpu_workers: int | None=None) -> dict:
        recheck_dir = native_io_paths.named_child(native_io_paths.named_child(post_dir, 'handoff_rechecks'), f'source_{pool_index:03d}_{source_dir.name}')
        try:
            recheck = run_post_asphere_promotion_recheck(source_dir, recheck_dir, common_merit_path, recheck_options, cpu_exe, timeout_seconds, started_at, child_cpu_workers if cpu_workers is None else cpu_workers)
        except Exception as exc:
            recheck = {'source_dir': str(source_dir), 'result_dir': str(recheck_dir), 'merit': math.inf, 'error': str(exc)}
        recheck['pool_index'] = pool_index
        recheck['primary_pool_source'] = source_dir.resolve() == selected_pool_source.resolve()
        recheck['selected_before_post_asphere'] = selected_before_dir is not None and source_dir.resolve() == selected_before_dir.resolve()
        _lensde_provenance.write_json(recheck_dir / 'handoff_summary.json', recheck)
        return recheck
    primary_handoff = run_handoff_recheck(1, candidate_pool[0], screening_options)
    field_probe_summary = copy.deepcopy(primary_handoff.get('adaptive_field_probe', {}))
    shared_field_nodes = primary_handoff.get('adaptive_field_probe', {}).get('selected_nodes', [])
    if not isinstance(shared_field_nodes, list) or not shared_field_nodes:
        contract_system = _lensde_asphere_contracts.first_existing_json([candidate_pool[0] / 'system.json', candidate_pool[0] / 'checkpoint_system.json', candidate_pool[0] / 'system_input.json', candidate_pool[0] / 'input_system.json'], {})
        if isinstance(contract_system, dict) and contract_system:
            contract_system = _lensde_fields.densify_system_fields_for_asphere(contract_system, _lensde_commands.option_int_from(screening_options, 'initial_search_post_asphere_field_count', 9, 1))
            shared_field_nodes = sorted(_lensde_fields.normalized_field_nodes(contract_system))
    shared_options = copy.deepcopy(screening_options)
    if isinstance(shared_field_nodes, list) and shared_field_nodes:
        shared_options['initial_search_post_asphere_field_count'] = len(shared_field_nodes)
        shared_options['initial_search_post_asphere_preferred_field_nodes'] = copy.deepcopy(shared_field_nodes)
    sampling_enabled = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_adaptive_sampling_enabled'), True)
    base_rays = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_trace_rays', 6, 1)
    base_arms = _lensde_commands.option_int_from(options, 'initial_search_post_asphere_trace_arms', 8, 1)
    probe_rays = max(base_rays, _lensde_commands.option_int_from(options, 'initial_search_post_asphere_sampling_probe_rays', 8, 1))
    probe_arms = max(base_arms, _lensde_commands.option_int_from(options, 'initial_search_post_asphere_sampling_probe_arms', 10, 1))
    sampling_threshold_um = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_sampling_min_underestimate_um', 1.0, 0.0)
    expected_fields = max(1, len(shared_field_nodes))
    min_coverage = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_multistart_audit_min_coverage', 0.97, 0.0)
    target_mean = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_multistart_target_mean_rms_um', 2.5, 0.0)
    metric = _lensde_defaults.canonical_image_quality_metric(common_merit.get('image_quality_metric'), 'rms_spot_wavefront')
    base_sampling_score = _lensde_asphere_contracts.post_asphere_score_summary(Path(str(primary_handoff.get('result_dir') or '')), expected_fields, min_coverage, target_mean, metric)
    sampling_probe_recheck: dict = {}
    sampling_probe_score: dict = {}
    sampling_comparison = {'triggered': False, 'valid': False, 'reason': 'adaptive pupil sampling disabled', 'threshold_um': sampling_threshold_um, 'fields': []}
    if sampling_enabled and (probe_rays > base_rays or probe_arms > base_arms):
        probe_options = copy.deepcopy(shared_options)
        probe_options.update({'initial_search_post_asphere_trace_rays': probe_rays, 'initial_search_post_asphere_trace_arms': probe_arms})
        sampling_probe_recheck = run_post_asphere_promotion_recheck(candidate_pool[0], native_io_paths.named_child(post_dir, 'sampling_audit') / f'probe_{probe_rays}x{probe_arms}', common_merit_path, probe_options, cpu_exe, timeout_seconds, started_at, child_cpu_workers)
        sampling_probe_score = _lensde_asphere_contracts.post_asphere_score_summary(Path(str(sampling_probe_recheck.get('result_dir') or '')), expected_fields, min_coverage, target_mean, metric)
        sampling_comparison = _lensde_asphere_contracts.post_asphere_sampling_underestimation(base_sampling_score, sampling_probe_score, sampling_threshold_um)
        if sampling_comparison['triggered']:
            shared_options = probe_options
            primary_handoff = sampling_probe_recheck
            primary_handoff['pool_index'] = 1
            primary_handoff['primary_pool_source'] = True
            primary_handoff['selected_before_post_asphere'] = selected_before_dir is not None and candidate_pool[0].resolve() == selected_before_dir.resolve()
            primary_handoff['adaptive_field_probe'] = copy.deepcopy(field_probe_summary)
            common_merit = _lensde_asphere_contracts.post_asphere_merit_from_source(primary_source_merit, shared_options, fallback_merit if isinstance(fallback_merit, dict) else None)
            common_merit = post_asphere_handoff.configure_illumination(common_merit, shared_options, contract_source_system)
            _lensde_provenance.write_json(common_merit_path, common_merit)
    handoff_rows.append(primary_handoff)
    post_summary['field_sampling'] = {'policy': field_probe_summary.get('contract_policy', _lensde_fields.canonical_post_asphere_field_contract_policy(options.get('initial_search_post_asphere_field_contract_policy'))), 'field_coordinate_requested': _lensde_fields.canonical_post_asphere_field_coordinate(options.get('initial_search_post_asphere_field_coordinate')), 'field_coordinate_applied': field_probe_summary.get('field_coordinate_applied', 'normalized_field'), 'contract_locked': True, 'replacement_timing': 'initial_contract_only', 'mid_optimization_field_replacement': False, 'contract_source_dir': str(candidate_pool[0]), 'field_reference_radius': field_reference_radius, 'target_field_count': len(shared_field_nodes), 'max_auxiliary_fields': _lensde_commands.option_int_from(options, 'initial_search_post_asphere_adaptive_field_max_additions', 1, 0), 'selected_nodes': copy.deepcopy(shared_field_nodes), 'screening_contract': screening_contract, 'proposed_auxiliary_nodes': copy.deepcopy(field_probe_summary.get('proposed_new_nodes', [])), 'applied_auxiliary_nodes': copy.deepcopy(field_probe_summary.get('new_nodes', [])), 'probe': field_probe_summary, 'sampling': {'enabled': sampling_enabled, 'base': {'rays': base_rays, 'arms': base_arms, 'score': base_sampling_score}, 'probe': {'rays': probe_rays, 'arms': probe_arms, 'recheck': sampling_probe_recheck, 'score': sampling_probe_score}, 'comparison': sampling_comparison, 'escalated': bool(sampling_comparison.get('triggered', False)), 'selected': {'rays': _lensde_commands.option_int_from(shared_options, 'initial_search_post_asphere_trace_rays', base_rays, 1), 'arms': _lensde_commands.option_int_from(shared_options, 'initial_search_post_asphere_trace_arms', base_arms, 1)}}}
    remaining_pool = list(enumerate(candidate_pool[1:], start=2))
    if handoff_workers <= 1:
        for pool_index, source_dir in remaining_pool:
            handoff_rows.append(run_handoff_recheck(pool_index, source_dir, shared_options, handoff_child_cpu_workers))
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=handoff_workers) as pool:
            futures = [pool.submit(run_handoff_recheck, pool_index, source_dir, shared_options, handoff_child_cpu_workers) for pool_index, source_dir in remaining_pool]
            for future in concurrent.futures.as_completed(futures):
                handoff_rows.append(future.result())
    handoff_rows.sort(key=_lensde_asphere_contracts.post_asphere_handoff_ranking_key)
    selected_handoffs = _lensde_asphere_contracts.select_post_asphere_handoffs(handoff_rows, candidate_count)
    post_summary['handoff_selection_policy'] = 'exclude_source_retained_fallbacks_then_ungated_complete_image_penalty_then_merit'
    sources = [Path(str(row.get('source_dir'))) for row in selected_handoffs]
    post_summary['handoff_rechecks'] = handoff_rows
    post_summary['source_dirs'] = [str(path) for path in sources]
    if stop_after_handoff:
        post_summary.update({'stopped_after_handoff': True, 'asphere_optimization_started': False, 'handoff_ready': bool(sources), 'stop_reason': 'requested_pre_asphere_experiment_boundary'})
        _lensde_asphere_population.write_post_asphere_summary(post_dir, canonical_post_dir, post_summary)
        return post_summary
    if not sources:
        post_summary.update({'skipped': True, 'reason': 'all common post-asphere handoff rechecks failed'})
        _lensde_asphere_population.write_post_asphere_summary(post_dir, canonical_post_dir, post_summary)
        return post_summary
    rows: list[dict] = []

    def run_candidate(index: int, source_dir: Path, handoff: dict) -> dict:
        case_root = native_io_paths.named_child(post_dir, f'candidate_{index:03d}_{source_dir.name}')
        try:
            if post_flow == 'curvature_normalized_formation_periodic':
                return run_post_asphere_curvature_normalized_formation_periodic_candidate(source_dir, case_root, shared_options, cpu_exe, de_exe, de_use_cuda, fallback_merit, timeout_seconds, started_at, seed_offset=index * 1009, common_merit=common_merit, protected_baseline=handoff, child_cpu_workers=child_cpu_workers)
            if post_flow == 'single_stage':
                return run_post_asphere_single_stage_candidate(source_dir, case_root, shared_options, cpu_exe, de_exe, de_use_cuda, fallback_merit, timeout_seconds, started_at, seed_offset=index * 1009, common_merit=common_merit, protected_baseline=handoff, child_cpu_workers=child_cpu_workers)
            if post_flow == 'single_mother':
                return run_post_asphere_single_mother_candidate(source_dir, case_root, shared_options, cpu_exe, de_exe, de_use_cuda, fallback_merit, timeout_seconds, started_at, seed_offset=index * 1009, common_merit=common_merit, protected_baseline=handoff, child_cpu_workers=child_cpu_workers)
            if post_flow == 'conic_multistart':
                return run_post_asphere_multistart_candidate(source_dir, case_root, shared_options, cpu_exe, de_exe, de_use_cuda, fallback_merit, timeout_seconds, started_at, seed_offset=index * 1009, common_merit=common_merit, protected_baseline=handoff, child_cpu_workers=child_cpu_workers)
            return run_post_asphere_candidate(source_dir, case_root, shared_options, cpu_exe, de_exe, de_use_cuda, fallback_merit, timeout_seconds, started_at, seed_offset=index * 1009, common_merit=common_merit, protected_baseline=handoff, child_cpu_workers=child_cpu_workers)
        except Exception as exc:
            error_row = {'source_dir': str(source_dir), 'candidate_root': str(case_root), 'error': str(exc), 'best_merit': math.inf}
            candidate_manifest_path = case_root / 'multistart_manifest.json'
            if candidate_manifest_path.is_file():
                candidate_manifest = _lensde_provenance.read_json(candidate_manifest_path)
                candidate_manifest.update({'status': 'failed', 'failed_at': time.strftime('%Y-%m-%dT%H:%M:%S'), 'error': str(exc)})
                _lensde_provenance.write_json(candidate_manifest_path, candidate_manifest)
            _lensde_provenance.write_json(case_root / 'summary.json', error_row)
            return error_row
    candidate_jobs = [(index, source_dir, handoff) for index, (source_dir, handoff) in enumerate(zip(sources, selected_handoffs, strict=False), start=1)]
    active_candidate_workers = min(len(candidate_jobs), candidate_workers) if candidate_jobs else 1
    if active_candidate_workers <= 1:
        for job in candidate_jobs:
            rows.append(run_candidate(*job))
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=active_candidate_workers) as pool:
            futures = [pool.submit(run_candidate, *job) for job in candidate_jobs]
            for future in concurrent.futures.as_completed(futures):
                rows.append(future.result())
    material_rejected_rows: list[dict] = []
    if require_real_materials:
        for row in rows:
            result_text = str(row.get('selected_result_dir') or row.get('best_stage_dir') or '').strip()
            contract = _lensde_asphere_contracts.post_asphere_real_material_contract_from_dir(Path(result_text)) if result_text else {'eligible': False, 'reason': 'candidate result directory not reported', 'catalog_material_count': 0, 'invalid_material_count': 0, 'catalog_materials': [], 'invalid_materials': []}
            row['material_contract'] = contract
            if not contract['eligible']:
                row['material_rejection_reason'] = contract['reason']
                material_rejected_rows.append(row)
    failed_rows = [row for row in rows if str(row.get('error') or '').strip()]
    successful_rows = [row for row in rows if not str(row.get('error') or '').strip() and math.isfinite(_lensde_configuration.finite_float(row.get('best_merit'), math.inf)) and (not require_real_materials or _lensde_commands.option_truthy(row.get('material_contract', {}).get('eligible'), False))]
    if rows and (not successful_rows):
        post_summary['candidates'] = rows
        post_summary['failed'] = True
        post_summary['candidate_failures'] = failed_rows
        post_summary['candidate_material_rejections'] = material_rejected_rows
        error_text = '; '.join((str(row.get('error') or row.get('material_rejection_reason') or 'candidate returned no finite merit') for row in rows))
        post_summary['error'] = f'all post-asphere candidates failed: {error_text}'
        _lensde_asphere_population.write_post_asphere_summary(post_dir, canonical_post_dir, post_summary)
        raise RuntimeError(post_summary['error'])
    rows.sort(key=lambda row: _lensde_configuration.finite_float(row.get('best_merit'), math.inf))
    successful_rows.sort(key=lambda row: _lensde_configuration.finite_float(row.get('best_merit'), math.inf))
    best = successful_rows[0] if successful_rows else {}
    post_summary['candidates'] = rows
    post_summary['candidate_failures'] = failed_rows
    post_summary['candidate_material_rejections'] = material_rejected_rows
    post_summary['best'] = best
    selected_current = material_summary.get('selected_final_result', {}) if isinstance(material_summary.get('selected_final_result'), dict) else {}
    original_current_merit = _lensde_configuration.finite_float(selected_current.get('merit', selected_current.get('final_merit', selected_current.get('best_merit'))), math.inf)
    if not math.isfinite(original_current_merit):
        original_current_merit = _lensde_configuration.finite_float(material_summary.get('promoted_final', {}).get('merit'), math.inf) if isinstance(material_summary.get('promoted_final'), dict) else math.inf
    best_merit = _lensde_configuration.finite_float(best.get('best_merit'), math.inf)
    current_dir = key_dir / 'final_selected_result'
    promotion_merit_path = common_merit_path
    if post_flow == 'single_stage' and _lensde_configuration.vignetting_optimization_enabled(common_merit):
        promotion_merit = post_asphere_handoff.configure_illumination(common_merit, {**shared_options, 'initial_search_post_asphere_illumination_supervision': 'all_fields'})
        promotion_merit_path = post_dir / 'promotion_merit_all_fields.json'
        _lensde_provenance.write_json(promotion_merit_path, promotion_merit)
    promotion_recheck: dict = {'source_dir': str(current_dir), 'original_merit': original_current_merit, 'merit': math.inf}
    selected_handoff = next((row for row in handoff_rows if _lensde_commands.option_truthy(row.get('selected_before_post_asphere'), False)), None)
    if isinstance(selected_handoff, dict) and promotion_merit_path == common_merit_path:
        promotion_recheck = copy.deepcopy(selected_handoff)
        promotion_recheck['source_dir'] = str(current_dir)
        promotion_recheck['reused_handoff_recheck'] = True
    elif current_dir.exists() and common_merit_path.exists():
        promotion_recheck = run_post_asphere_promotion_recheck(current_dir, post_dir / 'promotion_baseline_recheck', promotion_merit_path, shared_options, cpu_exe, timeout_seconds, started_at, child_cpu_workers)
    promotion_recheck['original_merit'] = original_current_merit
    current_merit = _lensde_configuration.finite_float(promotion_recheck.get('merit'), math.inf)
    post_summary['promotion_recheck'] = promotion_recheck
    current_material_contract = _lensde_asphere_contracts.post_asphere_promotion_material_contract(material_summary, current_dir)
    post_summary['incumbent_material_contract'] = current_material_contract
    best_result_text = str(best.get('selected_result_dir') or best.get('best_stage_dir') or '').strip()
    best_material_contract = _lensde_asphere_contracts.post_asphere_promotion_material_contract(material_summary, Path(best_result_text)) if best_result_text else {'eligible': False, 'reason': 'candidate result directory not reported', 'required': require_real_materials, 'promotion_allowed': not require_real_materials}
    material_promotion_allowed = bool(best_material_contract['promotion_allowed'])
    post_summary['promotion_material_contract'] = best_material_contract
    promotion_decision = _lensde_asphere_contracts.post_asphere_material_promotion_decision(best_merit, current_merit, best_material_contract, current_material_contract)
    terminal_vignetting: dict = {}
    promotion_best_dir = Path(best_result_text) if best_result_text else None
    if promotion_best_dir is not None and _lensde_commands.option_truthy(promotion_decision.get('promote'), False) and _lensde_asphere_contracts.post_asphere_stage_vignetting_recalibration_enabled(options, common_merit):
        terminal_dir = native_io_paths.named_child(post_dir, 'terminal_zemax_vignetting')
        try:
            terminal_timeout = timeout_seconds
            terminal_started = started_at
            terminal_vignetting = _lensde_asphere_contracts.finalize_post_asphere_zemax_vignetting(promotion_best_dir, terminal_dir, promotion_merit_path, shared_options, cpu_exe, terminal_timeout, terminal_started, child_cpu_workers)
            promotion_best_dir = terminal_dir
            terminal_material_contract = _lensde_asphere_contracts.post_asphere_promotion_material_contract(material_summary, terminal_dir)
            best_material_contract = terminal_material_contract
            post_summary['promotion_material_contract'] = best_material_contract
            promotion_decision = _lensde_asphere_contracts.post_asphere_material_promotion_decision(best_merit, current_merit, best_material_contract, current_material_contract)
        except Exception as error:
            terminal_vignetting = {'passed': False, 'error': str(error), 'selection_basis': 'pre_vignetting_calibration_optimizer_merit'}
            best_material_contract = {**best_material_contract, 'promotion_allowed': False, 'reason': f'terminal strict APG Set Vignetting failed: {error}'}
            post_summary['promotion_material_contract'] = best_material_contract
            promotion_decision = _lensde_asphere_contracts.post_asphere_material_promotion_decision(best_merit, current_merit, best_material_contract, current_material_contract)
    post_summary['terminal_zemax_vignetting'] = terminal_vignetting
    material_promotion_allowed = bool(best_material_contract.get('promotion_allowed', False))
    repair_invalid_incumbent = bool(promotion_decision['repair_invalid_incumbent'])
    promote = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_promote_best'), True) and bool(promotion_decision['promote'])
    post_summary['promotion_decision'] = promotion_decision
    post_summary['promoted'] = False
    post_summary['replaced_merit'] = current_merit
    post_summary['replaced_original_merit'] = original_current_merit
    if not material_promotion_allowed:
        post_summary['promotion_blocked_reason'] = f"best post-asphere result violates the real-material contract: {best_material_contract.get('reason') or 'unknown material state'}"
    elif not math.isfinite(current_merit) and (not repair_invalid_incumbent):
        post_summary['promotion_blocked_reason'] = 'common post-asphere baseline recheck failed'
    if promote:
        post_summary['promotion_reason'] = promotion_decision['reason']
        best_dir = promotion_best_dir or Path(str(best.get('selected_result_dir') or best.get('best_stage_dir') or ''))
        final_selected_dir = key_dir / 'final_selected_result'
        copied = _lensde_commands.copy_candidate_artifacts(best_dir, final_selected_dir)
        _lensde_commands.copy_stage_outputs(best_dir, run_dir)
        for merit_source in [best_dir / 'merit.json', post_dir / 'merit.json']:
            if merit_source.exists():
                shutil.copyfile(merit_source, run_dir / 'merit.json')
                break
        selected = {**best, 'promoted_from': 'post_asphere_refine', 'optimization_result_dir': best_result_text, 'pre_vignetting_calibration_merit': best_merit, 'terminal_zemax_vignetting': terminal_vignetting, 'selected_result_dir': str(final_selected_dir), 'selected_result_files': copied, 'replaced_merit': current_merit}
        material_summary['selected_final_result'] = selected
        material_summary['promoted_final'] = selected
        _lensde_provenance.write_json(key_dir / 'final_selected_result.json', selected)
        _lensde_provenance.write_json(run_dir / 'final_selected_result.json', selected)
        post_summary['promoted'] = True
        post_summary['selected_final_result'] = selected
    _lensde_asphere_population.write_post_asphere_summary(post_dir, canonical_post_dir, post_summary)
    material_summary['post_asphere_refine'] = post_summary
    _lensde_provenance.write_json(key_dir / 'key_outputs_summary.json', material_summary)
    _lensde_provenance.write_json(run_dir / 'de_dls_material_funnel_summary.json', material_summary)
    return post_summary
