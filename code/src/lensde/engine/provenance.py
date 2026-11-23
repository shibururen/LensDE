"""Extracted paper engine: provenance."""
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
from . import tracing as _lensde_tracing

def sanitize_json_value(value):
    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, float):
        return value if math.isfinite(value) else None
    if isinstance(value, dict):
        return {str(key): sanitize_json_value(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [sanitize_json_value(item) for item in value]
    return str(value)


def json_dumps_strict(data, **kwargs) -> str:
    return json.dumps(sanitize_json_value(data), allow_nan=False, **kwargs)


def write_json(path: Path, data: dict) -> None:
    path = native_io_paths.python_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json_dumps_strict(data, indent=2), encoding='utf-8')


def read_json(path: Path) -> dict:
    path = native_io_paths.python_path(path)
    return json.loads(path.read_text(encoding='utf-8'))


def read_json_optional(path: Path):
    path = native_io_paths.python_path(path)
    try:
        if not path.exists() or not path.is_file():
            return None
        return read_json(path)
    except (OSError, json.JSONDecodeError):
        return None


def sha256_file(path: Path) -> str | None:
    try:
        if not path.exists() or not path.is_file():
            return None
        digest = hashlib.sha256()
        with path.open('rb') as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b''):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError:
        return None


def file_manifest(path: Path) -> dict:
    manifest = {'path': str(path), 'exists': False}
    try:
        resolved = path.resolve()
        manifest['path'] = str(resolved)
        if not resolved.exists() or not resolved.is_file():
            return manifest
        stat = resolved.stat()
        manifest.update({'exists': True, 'size_bytes': stat.st_size, 'mtime': time.strftime('%Y-%m-%dT%H:%M:%S%z', time.localtime(stat.st_mtime)), 'sha256': sha256_file(resolved)})
    except OSError as exc:
        manifest['error'] = str(exc)
    return manifest


def summarize_variables_payload(payload) -> dict:
    variables = []
    if isinstance(payload, dict) and isinstance(payload.get('variables'), list):
        variables = payload.get('variables', [])
    elif isinstance(payload, list):
        variables = payload
    by_kind = Counter((str(variable.get('kind', '')) for variable in variables if isinstance(variable, dict)))
    width_by_kind: dict[str, list[float]] = {}
    for variable in variables:
        if not isinstance(variable, dict):
            continue
        if 'lower' not in variable or 'upper' not in variable:
            continue
        lower = _lensde_configuration.finite_float(variable.get('lower'), float('nan'))
        upper = _lensde_configuration.finite_float(variable.get('upper'), float('nan'))
        if math.isfinite(lower) and math.isfinite(upper):
            width_by_kind.setdefault(str(variable.get('kind', '')), []).append(abs(upper - lower))
    width_summary = {}
    for kind, widths in width_by_kind.items():
        if widths:
            width_summary[kind] = {'min': min(widths), 'max': max(widths), 'mean': sum(widths) / len(widths)}
    return {'count': len(variables), 'by_kind': dict(by_kind), 'range_width_by_kind': width_summary, 'variables': copy.deepcopy(variables)}


def summarize_merit_payload(payload) -> dict:
    if not isinstance(payload, dict):
        return {'full_config': payload}
    headline_keys = ['auto_merit', 'merit_model', 'image_quality_metric', 'target_efl', 'target_ttl', 'target_bfl', 'efl_weight', 'ttl_weight', 'bfl_weight', 'spot_weight', 'distortion_weight', 'field_curvature_weight', 'chromatic_weight', 'lateral_color_weight', 'constraints', 'group_weights', 'num_rays', 'pupil_arms', 'pupil_distribution', 'codev_sampling_profile', 'codev_ray_interval', 'codev_obscuration', 'codev_spot_aperture_weight', 'codev_wavefront_aperture_weight', 'codev_use_symmetry', 'image_field_weight_mode', 'image_field_edge_weight', 'image_field_weight_exponent', 'wavefront_reference', 'sampling_seed', 'quality_spot_scale', 'quality_wavefront_scale', 'unified_image_mean_spot_weight', 'unified_image_single_ray_spot_tail_weight', 'single_ray_spot_tail_extra_edge_min_field_radius', 'unified_image_wavefront_weight', 'quality_single_ray_guard_ratio']
    summary = {key: copy.deepcopy(payload.get(key)) for key in headline_keys if key in payload}
    operands = payload.get('operands')
    if isinstance(operands, list):
        summary['operand_count'] = len(operands)
        summary['operands'] = copy.deepcopy(operands)
    summary['full_config'] = copy.deepcopy(payload)
    return summary


def summarize_score_payload(payload) -> dict:
    if not isinstance(payload, dict):
        return {'full_score': payload}
    contributions = payload.get('contributions')
    summary = {'merit': payload.get('merit'), 'status': payload.get('status'), 'trace_ok': payload.get('trace_ok'), 'mean_spot': payload.get('mean_spot'), 'max_spot': payload.get('max_spot'), 'mean_wavefront_rms': payload.get('mean_wavefront_rms'), 'max_wavefront_rms': payload.get('max_wavefront_rms'), 'full_score': copy.deepcopy(payload)}
    if isinstance(contributions, list):
        summary['contribution_count'] = len(contributions)
        summary['contributions'] = copy.deepcopy(contributions)
        summary['contribution_terms'] = [{'name': item.get('name'), 'group': item.get('group'), 'weight': item.get('weight'), 'tolerance': item.get('tolerance'), 'penalty': item.get('penalty'), 'gate': item.get('gate')} for item in contributions if isinstance(item, dict)]
    return summary


def summarize_system_payload(payload) -> dict:
    if not isinstance(payload, dict):
        return {'full_config': payload}
    surfaces = payload.get('surfaces', [])
    return {'name': payload.get('name'), 'surface_count': len(surfaces) if isinstance(surfaces, list) else 0, 'stop_surface': payload.get('stop_surface'), 'aperture': copy.deepcopy(payload.get('aperture')), 'fields': copy.deepcopy(payload.get('fields')), 'wavelengths': copy.deepcopy(payload.get('wavelengths')), 'full_config': copy.deepcopy(payload)}


def summarize_glass_search_payload(payload) -> dict:
    if not isinstance(payload, dict):
        return {'full_config': payload}
    groups = payload.get('groups', [])
    return {'enabled': bool(payload.get('enabled', False)), 'group_count': len(groups) if isinstance(groups, list) else 0, 'groups': copy.deepcopy(groups), 'full_config': copy.deepcopy(payload)}


def summarize_basin_archive_payload(payload) -> dict:
    if isinstance(payload, list):
        return {'count': len(payload), 'first_items': copy.deepcopy(payload[:10])}
    if isinstance(payload, dict):
        rows = payload.get('basins')
        if isinstance(rows, list):
            return {'count': len(rows), 'first_items': copy.deepcopy(rows[:10]), 'keys': sorted((str(key) for key in payload.keys()))}
        return {'keys': sorted((str(key) for key in payload.keys())), 'summary': copy.deepcopy({key: value for key, value in payload.items() if key not in {'basins', 'archive'}})}
    return {'summary': payload}


def summarize_repro_json_file(path: Path) -> dict | None:
    payload = read_json_optional(path)
    if payload is None:
        return None
    name = path.name.lower()
    if name == 'variables.json':
        return {'type': 'variables', 'summary': summarize_variables_payload(payload)}
    if name == 'merit.json' or name.endswith('_merit.json') or 'merit' in name:
        return {'type': 'merit', 'summary': summarize_merit_payload(payload)}
    if name in {'score.json', 'initial_score.json', 'checkpoint_score.json'}:
        return {'type': 'score', 'summary': summarize_score_payload(payload)}
    if name in {'system.json', 'input_system.json', 'system_input.json', 'checkpoint_system.json'}:
        return {'type': 'system', 'summary': summarize_system_payload(payload)}
    if name == 'glass_search.json':
        return {'type': 'glass_search', 'summary': summarize_glass_search_payload(payload)}
    if name in {'run_meta.json', 'strategy_summary.json', 'checkpoint_meta.json'}:
        return {'type': name.removesuffix('.json'), 'summary': copy.deepcopy(payload)}
    if name == 'basin_archive.json':
        return {'type': 'basin_archive', 'summary': summarize_basin_archive_payload(payload)}
    if isinstance(payload, dict):
        return {'type': 'json', 'summary': {'keys': sorted((str(key) for key in payload.keys()))}}
    if isinstance(payload, list):
        return {'type': 'json', 'summary': {'count': len(payload), 'first_items': copy.deepcopy(payload[:10])}}
    return {'type': 'json', 'summary': {'value': payload}}


def command_list(command) -> list[str]:
    if isinstance(command, (list, tuple)):
        return [str(item) for item in command]
    if command is None:
        return []
    return [str(command)]


def command_path_refs(command) -> dict:
    args = command_list(command)
    refs = {}
    index = 0
    while index < len(args):
        flag = args[index]
        role = state.REPRO_COMMAND_PATH_FLAGS.get(flag)
        if role and index + 1 < len(args):
            refs[role] = args[index + 1]
            index += 2
            continue
        index += 1
    return refs


def command_seed_refs(command) -> dict:
    args = command_list(command)
    seeds = {}
    index = 0
    while index < len(args):
        flag = args[index]
        if flag in state.REPRO_SEED_FLAGS and index + 1 < len(args):
            seeds[flag.removeprefix('--').replace('-', '_')] = args[index + 1]
            index += 2
            continue
        index += 1
    return seeds


def command_flag_refs(command) -> dict:
    args = command_list(command)
    flags: dict[str, object] = {}

    def add_flag(key: str, value) -> None:
        if key not in flags:
            flags[key] = value
            return
        current = flags[key]
        if isinstance(current, list):
            current.append(value)
        else:
            flags[key] = [current, value]
    index = 0
    while index < len(args):
        item = args[index]
        if not item.startswith('--'):
            index += 1
            continue
        key = item.removeprefix('--').replace('-', '_')
        if index + 1 < len(args) and (not args[index + 1].startswith('--')):
            add_flag(key, args[index + 1])
            index += 2
            continue
        add_flag(key, True)
        index += 1
    return flags


def resolve_repro_path(path: Path, run_dir: Path) -> Path:
    if path.exists():
        return path
    parts = list(path.parts)
    lower_parts = [part.lower() for part in parts]
    try:
        work_index = lower_parts.index('work')
    except ValueError:
        return path
    suffix = parts[work_index + 2:]
    if not suffix:
        return path
    candidate = run_dir.joinpath(*suffix)
    return candidate if candidate.exists() else path


def add_manifest_file(files: dict, path: Path, role: str) -> None:
    try:
        resolved = path.resolve()
    except OSError:
        resolved = path
    key = str(resolved)
    entry = files.setdefault(key, {'roles': [], 'file': file_manifest(resolved)})
    if role not in entry['roles']:
        entry['roles'].append(role)
    json_summary = summarize_repro_json_file(resolved) if resolved.suffix.lower() == '.json' else None
    if json_summary is not None:
        entry.update(json_summary)


def build_repro_manifest(run_dir: Path, project: dict, options: dict, stages: list[dict], executable: Path, *, strategy: str, optimizer: str, merit_strategy: str | None=None, flow: str | None=None, run_meta: dict | None=None, returncode=None, timed_out: bool | None=None) -> dict:
    files: dict[str, dict] = {}
    for name in state.REPRO_TOP_LEVEL_FILES:
        path = run_dir / name
        if path.exists():
            add_manifest_file(files, path, f'top_level:{name}')
    stage_manifests = []
    for stage in stages:
        command = command_list(stage.get('command'))
        path_refs = command_path_refs(command)
        output_dir = Path(path_refs['out_dir']) if path_refs.get('out_dir') else None
        for role, ref in path_refs.items():
            path = resolve_repro_path(Path(ref), run_dir)
            if role == 'out_dir':
                continue
            add_manifest_file(files, path, f"stage:{stage.get('name', '')}:{role}")
        if output_dir is not None:
            output_dir = resolve_repro_path(output_dir, run_dir)
        if output_dir is not None and output_dir.exists():
            for file_name in state.REPRO_OUTPUT_FILES:
                path = output_dir / file_name
                if path.exists():
                    add_manifest_file(files, path, f"stage:{stage.get('name', '')}:output:{file_name}")
        stage_manifests.append({'name': stage.get('name'), 'returncode': stage.get('returncode'), 'timed_out': stage.get('timed_out', False), 'command': command, 'path_refs': path_refs, 'seeds': command_seed_refs(command)})
    code_files = {'gui_server': state.ENGINE_ROOT / 'core.py', 'cpp_main': state.CPP_ROOT / 'src' / 'main.cpp', 'cpp_optimizer': state.CPP_ROOT / 'src' / 'optimizer.cpp', 'cpp_optimizer_header': state.CPP_ROOT / 'include' / 'optiland' / 'optimizer.hpp'}
    if seed_protocol.policy(options) == seed_protocol.NUMERIC_POLICY:
        code_files['seed_protocol'] = Path(seed_protocol.__file__)
        code_files['two_layer_seed_consumer'] = state.PACKAGE_ROOT / 'workflow' / 'run_two_layer_basin_refine_funnel.py'
        profile_name = str(options.get('initial_search_seed_profile', seed_protocol.GENERIC_PROFILE))
        if profile_name != seed_protocol.GENERIC_PROFILE:
            seed_protocol.load_profile(profile_name)
            code_files['seed_reference_profile'] = seed_protocol.PROFILE_ROOT / f'{profile_name}.json'
    reproduction_notes = ['This manifest records normalized GUI inputs and the concrete files passed to each C++ stage.', 'Use input_files entries and their SHA-256 hashes to detect default drift or accidental input changes.', 'Variable ranges are in variables.json summaries; merit configuration is in merit.json summaries.', 'Expanded merit contributions are captured from score.json files when the C++ stage produced them.']
    if options.get('adaptive_pbest_scope') is None and options.get('pbest_scope') is None:
        reproduction_notes.append('No explicit pbest scope option was present; command flags show the effective backend default.')
    return {'manifest_version': 1, 'created_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'run_dir': str(run_dir.resolve()), 'strategy': strategy, 'optimizer': optimizer, 'merit_strategy': merit_strategy, 'flow': flow, 'returncode': returncode, 'timed_out': timed_out, 'executable': file_manifest(Path(executable)), 'code_files': {name: file_manifest(path) for name, path in code_files.items()}, 'project': copy.deepcopy(project), 'options': copy.deepcopy(options), 'run_meta': copy.deepcopy(run_meta) if run_meta is not None else None, 'stage_count': len(stage_manifests), 'stages': stage_manifests, 'input_file_count': len(files), 'input_files': files, 'reproduction_notes': reproduction_notes}


def json_fingerprint(payload) -> str | None:
    try:
        encoded = json_dumps_strict(payload, sort_keys=True, ensure_ascii=False).encode('utf-8')
    except (TypeError, ValueError):
        return None
    return hashlib.sha256(encoded).hexdigest()


def option_snapshot(options: dict, keys: list[str]) -> dict:
    return {key: copy.deepcopy(options.get(key)) for key in keys if key in options}


def summarize_effective_input_file(run_dir: Path, name: str) -> dict:
    path = run_dir / name
    entry = {'file': file_manifest(path)}
    if path.exists() and path.suffix.lower() == '.json':
        summary = summarize_repro_json_file(path)
        if summary is not None:
            entry.update(summary)
    return entry


def build_effective_repro_settings(run_dir: Path, project: dict, options: dict, stages: list[dict], executable: Path, *, strategy: str, optimizer: str, merit_strategy: str | None=None, flow: str | None=None, run_meta: dict | None=None, returncode=None, timed_out: bool | None=None) -> dict:
    strategy_summary = read_json_optional(run_dir / 'strategy_summary.json')
    source_overrides = read_json_optional(run_dir / 'active_basin_test_overrides.json')
    option_groups = {group: option_snapshot(options, keys) for group, keys in state.EFFECTIVE_REPRO_OPTION_GROUPS.items()}
    stage_summaries = []
    for stage in stages:
        command = command_list(stage.get('command'))
        stage_summaries.append({'name': stage.get('name'), 'returncode': stage.get('returncode'), 'timed_out': stage.get('timed_out', False), 'command': command, 'flags': command_flag_refs(command), 'path_refs': command_path_refs(command), 'seeds': command_seed_refs(command), 'adaptive_early_stop_flag_present': '--adaptive-early-stop' in command})
    global_stage_names = ('global', 'explore', 'develop')
    global_stage_commands = [row for row in stage_summaries if any((token in str(row.get('name') or '').lower() for token in global_stage_names))]
    early_stop_enabled = bool(options.get('initial_search_funnel_global_early_stop_enabled', True))
    early_stop_cli_present = any((bool(row.get('adaptive_early_stop_flag_present')) for row in global_stage_commands))
    warnings = []
    if early_stop_enabled and global_stage_commands and (not early_stop_cli_present):
        warnings.append('initial_search_funnel_global_early_stop_enabled is true, but no global stage command contains --adaptive-early-stop.')
    if source_overrides and isinstance(source_overrides, dict):
        overrides = source_overrides.get('option_overrides')
        if isinstance(overrides, dict):
            missing_defaults = [key for key in ['initial_search_funnel_global_early_stop_enabled', 'initial_search_funnel_include_global_stage_best', 'initial_search_pbest_scope', 'initial_search_refresh_seed_pool_fraction', 'initial_search_rest_curvature_cap', 'initial_search_glass_thickness_min', 'initial_search_glass_thickness_max'] if key not in overrides and key in options]
            if missing_defaults:
                warnings.append('active_basin_test_overrides.json only records explicit wrapper overrides; inherited effective values are listed here instead.')
    critical_inputs = {name: summarize_effective_input_file(run_dir, name) for name in ['input_system.json', 'variables.json', 'glass_search.json', 'merit.json'] if (run_dir / name).exists()}
    initial_search_options = {key: copy.deepcopy(value) for key, value in sorted(options.items()) if str(key).startswith('initial_search_')}
    return {'manifest_version': 1, 'created_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'run_dir': str(run_dir.resolve()), 'strategy': strategy, 'optimizer': optimizer, 'merit_strategy': merit_strategy, 'flow': flow, 'returncode': returncode, 'timed_out': timed_out, 'executable': file_manifest(Path(executable)), 'source_overrides': source_overrides, 'option_groups': option_groups, 'all_initial_search_options': initial_search_options, 'all_project_options': copy.deepcopy(options), 'option_hashes': {'all_project_options_sha256': json_fingerprint(options), 'initial_search_options_sha256': json_fingerprint(initial_search_options), 'option_groups_sha256': json_fingerprint(option_groups)}, 'critical_input_files': critical_inputs, 'critical_input_hashes': {name: entry.get('file', {}).get('sha256') for name, entry in critical_inputs.items()}, 'strategy_summary': copy.deepcopy(strategy_summary), 'run_meta_without_options': {key: copy.deepcopy(value) for key, value in run_meta.items() if key != 'options'} if isinstance(run_meta, dict) else None, 'stage_count': len(stage_summaries), 'stages': stage_summaries, 'reproduction_notes': ['This compact snapshot records effective GUI/Python options after defaults and overrides have been applied.', 'Use option_groups for quick audit, all_project_options for exact GUI-side reproduction, and stages for exact C++ CLI flags.', 'Global early stop is controlled by both the GUI option and the --adaptive-early-stop flag in each global stage command.', 'Variable ranges and merit construction are summarized from variables.json and merit.json under critical_input_files.'], 'recording_warnings': warnings}


def write_repro_manifest(run_dir: Path, project: dict, options: dict, stages: list[dict], executable: Path, *, strategy: str, optimizer: str, merit_strategy: str | None=None, flow: str | None=None, run_meta: dict | None=None, returncode=None, timed_out: bool | None=None) -> None:
    effective_settings = build_effective_repro_settings(run_dir, project, options, stages, executable, strategy=strategy, optimizer=optimizer, merit_strategy=merit_strategy, flow=flow, run_meta=run_meta, returncode=returncode, timed_out=timed_out)
    write_json(run_dir / 'effective_repro_settings.json', effective_settings)
    manifest = build_repro_manifest(run_dir, project, options, stages, executable, strategy=strategy, optimizer=optimizer, merit_strategy=merit_strategy, flow=flow, run_meta=run_meta, returncode=returncode, timed_out=timed_out)
    write_json(run_dir / 'repro_manifest.json', manifest)


def format_lens_number(value, *, infinity: str='INFINITY') -> str:
    if value is None:
        return infinity
    try:
        number = float(value)
    except (TypeError, ValueError):
        return '0'
    if not math.isfinite(number):
        return infinity
    if number == 0.0:
        return '0'
    text = repr(number)
    return text[:-2] if text.endswith('.0') else text


def safe_lens_name(value: str | None, fallback: str='optiland_cpp') -> str:
    text = ''.join((ch if ch.isalnum() or ch in '._- ' else '_' for ch in str(value or fallback))).strip()
    return text or fallback


def surface_radius(surface: dict, *, plane_value=None):
    kind = str(surface.get('kind', '')).lower()
    radius = surface.get('radius')
    if kind in {'plane', 'object', 'image'} or radius is None:
        return plane_value
    return radius


def surface_thickness(surface: dict, default=0.0):
    thickness = surface.get('thickness', default)
    if thickness is None:
        return math.inf
    return _lensde_configuration.finite_float(thickness, default)


def field_angles(system: dict) -> tuple[list[float], list[float]]:
    max_field = _lensde_configuration.finite_float(system.get('max_field_deg'), 0.0)
    fields = system.get('fields') or [{'hx': 0.0, 'hy': 0.0}]
    x = [_lensde_configuration.finite_float(field.get('hx'), 0.0) * max_field for field in fields]
    y = [_lensde_configuration.finite_float(field.get('hy'), 0.0) * max_field for field in fields]
    return (x, y)


def wavelength_data(system: dict) -> tuple[list[float], list[float], int]:
    waves = system.get('wavelengths') or [{'value_um': 0.5875618, 'weight': 1.0, 'primary': True}]
    values = [_lensde_configuration.finite_float(wave.get('value_um'), 0.5875618) for wave in waves]
    weights = [_lensde_configuration.finite_float(wave.get('weight'), 1.0) for wave in waves]
    primary = 0
    for index, wave in enumerate(waves):
        if wave.get('primary'):
            primary = index
            break
    return (values, weights, primary)


def material_nd_vd(material: dict) -> tuple[float | None, float | None]:
    if str(material.get('mode', '')).lower() == 'air' or str(material.get('name', '')).lower() == 'air':
        return (None, None)
    nd = _lensde_configuration.finite_float(material.get('nd'), 0.0)
    vd = _lensde_configuration.finite_float(material.get('vd'), 0.0)
    if nd <= 1.0000001 or vd <= 0.0:
        return (None, None)
    return (nd, vd)


def codev_material_token(material: dict) -> str:
    nd, vd = material_nd_vd(material)
    if nd is None or vd is None:
        return ''
    return f'{format_lens_number(nd)}:{format_lens_number(vd)}'


def zemax_material_lines(surface: dict) -> list[str]:
    if surface.get('reflective'):
        return ['  GLAS MIRROR']
    material = surface.get('material_after') or {}
    if str(material.get('mode', '')).lower() == 'glass':
        name = str(material.get('name') or material.get('reference') or '').strip()
        if name and name.lower() != 'air':
            nd = _lensde_configuration.finite_float(material.get('nd'), 0.0)
            vd = _lensde_configuration.finite_float(material.get('vd'), 0.0)
            return [f'  GLAS {safe_lens_name(name)} 3 0 {format_lens_number(nd)} {format_lens_number(vd)} 0 0 0 0 0 0']
    nd, vd = material_nd_vd(material)
    if nd is None or vd is None:
        return []
    name = safe_lens_name(material.get('reference') or material.get('name'), 'MODEL')
    return [f'  GLAS {name} 1 0 {format_lens_number(nd)} {format_lens_number(vd)}']


def zemax_glass_catalogs(system: dict) -> list[str]:
    """Resolve catalog declarations without confusing a glass name for a library."""
    aliases = {'CDGM': 'CDGM-ZEMAX202509'}
    aliases.update(system.get('zemax_catalog_aliases') or {})
    catalogs = _lensde_materials.split_glass_libraries(system.get('zemax_glass_catalogs'))
    for surface in system.get('surfaces', []):
        material = surface.get('material_after') or {}
        if surface.get('reflective') or str(material.get('mode', '')).lower() != 'glass':
            continue
        library = str(material.get('library') or '').strip()
        parts = str(material.get('source_path') or material.get('filename') or '').replace('\\', '/').split('/')
        if not library and 'glass' in parts:
            index = parts.index('glass')
            if index + 2 < len(parts):
                library = parts[index + 1]
        reference = str(material.get('reference') or '').strip()
        if not library and reference.lower() != str(material.get('name') or '').strip().lower():
            library = reference
        libraries = [library] if library else _lensde_materials.split_glass_libraries(system.get('glass_libraries') or system.get('glass_library'))
        for item in libraries:
            name = aliases.get(item.upper(), item.upper())
            if name and name not in catalogs:
                catalogs.append(name)
    return catalogs


def surface_clear_radius(surface: dict) -> float:
    aperture = surface.get('aperture') or {}
    candidates = [_lensde_configuration.finite_float(surface.get('aperture_radius'), 0.0), _lensde_configuration.finite_float(surface.get('semi_aperture'), 0.0), _lensde_configuration.finite_float(aperture.get('r_max'), 0.0)]
    return max(candidates)


def export_codev_seq(project: dict, path: Path) -> None:
    system = _lensde_configuration.normalise_project(project).get('system', {})
    waves_um, weights, primary = wavelength_data(system)
    x_angles, y_angles = field_angles(system)
    aperture = system.get('aperture') or {}
    ap_type = str(aperture.get('type', 'EPD'))
    ap_value = _lensde_configuration.finite_float(aperture.get('value'), 0.0)
    title = safe_lens_name(system.get('name'), 'optiland_cpp').replace("'", '')
    lines = ['! Generated by Optiland C++ Workbench', 'RDM;LEN', f"TITLE '{title}'", 'DIM   M', 'WL    ' + ' '.join((format_lens_number(value * 1000.0) for value in waves_um)), f'REF   {primary + 1}', 'WTW   ' + ' '.join((format_lens_number(weight) for weight in weights))]
    if ap_type in {'EPD', 'float_by_stop_size'} and ap_value > 0.0:
        lines.append(f'EPD   {format_lens_number(ap_value)}')
    elif ap_type == 'imageFNO' and ap_value > 0.0:
        lines.append(f'FNO   {format_lens_number(ap_value)}')
    elif ap_type == 'objectNA' and ap_value > 0.0:
        lines.append(f'NAO   {format_lens_number(ap_value)}')
    lines.extend(['XAN   ' + ' '.join((format_lens_number(value) for value in x_angles)), 'YAN   ' + ' '.join((format_lens_number(value) for value in y_angles))])
    surfaces = system.get('surfaces') or []
    object_distance = math.inf
    if surfaces and str(surfaces[0].get('kind', '')).lower() == 'object':
        object_distance = surface_thickness(surfaces[0], math.inf)
    lines.append(f"SO    0.0 {format_lens_number(object_distance, infinity='0.1E11')}")
    for surface in surfaces:
        kind = str(surface.get('kind', '')).lower()
        if kind in {'object', 'image'}:
            continue
        radius = surface_radius(surface, plane_value=0.0)
        thickness = surface_thickness(surface, 0.0)
        row = f"S     {format_lens_number(radius, infinity='0.0')} {format_lens_number(thickness, infinity='0.0')}"
        token = codev_material_token(surface.get('material_after') or {})
        if token:
            row += f' {token}'
        lines.append(row)
        if surface.get('is_stop'):
            lines.append('  STO')
        if surface.get('reflective'):
            lines.append('  GLA REFL')
        conic = _lensde_configuration.finite_float(surface.get('conic'), 0.0)
        coeffs = [_lensde_configuration.finite_float(coeff, 0.0) for coeff in surface.get('asphere_coefficients') or []]
        if abs(conic) > 0.0 or any((abs(coeff) > 0.0 for coeff in coeffs)):
            names = ['CUF', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H']
            padded = (coeffs + [0.0] * len(names))[:len(names)]
            lines.extend(['  ASP', f'  K   {format_lens_number(conic)}', f'  IC  Yes; CUF {format_lens_number(padded[0])}'])
            for start in range(1, len(names), 4):
                values = list(zip(names[start:start + 4], padded[start:start + 4], strict=True))
                if any((abs(value) > 0.0 for _, value in values)):
                    lines.append('  ' + '; '.join((f'{name} {format_lens_number(value)}' for name, value in values)))
    lines.extend(['SI    0.0 0.0', 'GO'])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def export_zemax_zmx(project: dict, path: Path) -> None:
    system = _lensde_configuration.normalise_project(project).get('system', {})
    waves_um, weights, primary = wavelength_data(system)
    x_angles, y_angles = field_angles(system)
    fields = system.get('fields') or [{'hx': 0.0, 'hy': 0.0, 'fvcx': 0.0, 'fvcy': 0.0}]
    aperture = system.get('aperture') or {}
    ap_type = str(aperture.get('type', 'EPD'))
    ap_value = _lensde_configuration.finite_float(aperture.get('value'), 0.0)
    lines = ['VERS 231205 289 20120530 20120530', 'MODE SEQ', f"NAME {safe_lens_name(system.get('name'), 'optiland_cpp')}", 'UNIT MM X W X CM MR CPMM']
    catalogs = zemax_glass_catalogs(system)
    if catalogs:
        lines.append('GCAT ' + ' '.join(catalogs))
    if ap_type == 'EPD' and ap_value > 0.0:
        lines.append(f'ENPD {format_lens_number(ap_value)}')
    elif ap_type == 'imageFNO' and ap_value > 0.0:
        lines.append(f'FNUM {format_lens_number(ap_value)} 0')
    elif ap_type == 'objectNA' and ap_value > 0.0:
        lines.append(f'OBNA {format_lens_number(ap_value)} 0')
    elif ap_type == 'float_by_stop_size':
        lines.append('FLOA')
    lines.extend([f'FTYP 0 0 {len(x_angles)} {len(waves_um)} 0 0 0', 'XFLN ' + ' '.join((format_lens_number(value) for value in x_angles)), 'YFLN ' + ' '.join((format_lens_number(value) for value in y_angles)), 'FWGN ' + ' '.join(('1' for _ in x_angles)), 'VDXN ' + ' '.join((format_lens_number(_lensde_configuration.finite_float(field.get('fvcx'), 0.0)) for field in fields)), 'VDYN ' + ' '.join((format_lens_number(_lensde_configuration.finite_float(field.get('fvcy'), 0.0)) for field in fields)), 'VCXN ' + ' '.join((format_lens_number(_lensde_configuration.finite_float(field.get('fvdx'), 0.0)) for field in fields)), 'VCYN ' + ' '.join((format_lens_number(_lensde_configuration.finite_float(field.get('fvdy'), 0.0)) for field in fields)), 'VANN ' + ' '.join(('0' for _ in x_angles))])
    for index, (value, weight) in enumerate(zip(waves_um, weights, strict=True), start=1):
        lines.append(f'WAVM {index} {format_lens_number(value)} {format_lens_number(weight)}')
    lines.append(f'PWAV {primary + 1}')
    surfaces = system.get('surfaces') or []
    for index, surface in enumerate(surfaces):
        kind = str(surface.get('kind', '')).lower()
        coeffs = [_lensde_configuration.finite_float(coeff, 0.0) for coeff in surface.get('asphere_coefficients') or []]
        surface_type = 'EVENASPH' if kind == 'even_asphere' or any((abs(coeff) > 0.0 for coeff in coeffs)) else 'STANDARD'
        radius = surface_radius(surface, plane_value=None)
        radius_number = _lensde_configuration.finite_float(radius, math.inf)
        curvature = 0.0 if radius is None or abs(radius_number) <= 1e-14 else 1.0 / radius_number
        lines.extend([f'SURF {index}', f'  TYPE {surface_type}', f'  CURV {format_lens_number(curvature)} 0 0 0 0 ""'])
        if surface.get('is_stop'):
            lines.append('  STOP')
        if coeffs:
            for parm_index, coeff in enumerate(coeffs, start=1):
                if abs(coeff) > 0.0:
                    lines.append(f'  PARM {parm_index} {format_lens_number(coeff)}')
        conic = _lensde_configuration.finite_float(surface.get('conic'), 0.0)
        if abs(conic) > 0.0:
            lines.append(f'  CONI {format_lens_number(conic)}')
        if index == 0 and kind == 'object':
            thickness = surface_thickness(surface, math.inf)
        else:
            thickness = surface_thickness(surface, 0.0)
        lines.append(f'  DISZ {format_lens_number(thickness)}')
        lines.extend(zemax_material_lines(surface))
        clear = surface_clear_radius(surface)
        if clear > 0.0:
            lines.append(f'  DIAM {format_lens_number(clear)} 1 0 0 1 ""')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def exe_path(use_cuda: bool) -> Path:
    cuda = state.CPP_ROOT / 'build' / 'lensde_de_cuda.exe'
    cpu = state.CPP_ROOT / 'build' / 'lensde_cpu.exe'
    return cuda if use_cuda and cuda.exists() else cpu


def initial_search_stage_de_evaluator(default_exe: Path, default_use_cuda: bool, stage_de_use_cuda: bool) -> tuple[Path, bool]:
    if not stage_de_use_cuda:
        return (default_exe, default_use_cuda)
    cuda_exe = state.CPP_ROOT / 'build' / 'lensde_de_cuda.exe'
    if not cuda_exe.exists():
        raise FileNotFoundError(f'global-explore CUDA DE executable not found: {cuda_exe}')
    return (cuda_exe, True)


def append_initial_search_stage_evaluator_args(args: list[str], use_cuda: bool) -> None:
    """Make the stage evaluator request explicit instead of silently falling back."""
    if use_cuda:
        args.extend(['--de-cuda-min-batch', '1'])
    else:
        args.append('--cpu')


def initial_search_stage_environment(args: list[str]) -> dict[str, str] | None:
    if not args or Path(args[0]).name.lower() != 'lensde_de_cuda.exe':
        return None
    env = os.environ.copy()
    env['OPTILAND_DE_CUDA_SUPPORT_DEBUG'] = '1'
    return env


def glass_specs(filename: str) -> dict:
    if filename in state.GLASS_SPECS_CACHE:
        return state.GLASS_SPECS_CACHE[filename]
    requested_path = Path(filename)
    path = requested_path if requested_path.is_absolute() else state.DATABASE_ROOT / 'data-nk' / requested_path
    out = {'nd': None, 'vd': None, 'min_wavelength': None, 'max_wavelength': None}
    if not path.exists():
        state.GLASS_SPECS_CACHE[filename] = out
        return out
    table_min = math.inf
    table_max = -math.inf
    in_tabulated_data = False
    for line in path.read_text(encoding='utf-8', errors='ignore').splitlines():
        stripped = line.strip()
        if stripped.startswith('- type:') or stripped.startswith('type:'):
            in_tabulated_data = 'tabulated n' in stripped.lower()
        elif in_tabulated_data and stripped and (stripped[0] in '+-.0123456789'):
            values = stripped.split()
            if len(values) >= 2:
                wavelength = _lensde_configuration.finite_float(values[0], math.nan)
                if math.isfinite(wavelength):
                    table_min = min(table_min, wavelength)
                    table_max = max(table_max, wavelength)
        if stripped.startswith('nd:'):
            try:
                out['nd'] = float(stripped.split(':', 1)[1].strip().split()[0])
            except ValueError:
                pass
        elif stripped.startswith('Vd:'):
            try:
                out['vd'] = float(stripped.split(':', 1)[1].strip().split()[0])
            except ValueError:
                pass
        elif stripped.startswith('wavelength_range:'):
            values = stripped.split(':', 1)[1].strip().split()
            if len(values) >= 2:
                try:
                    out['min_wavelength'] = float(values[0])
                    out['max_wavelength'] = float(values[1])
                except ValueError:
                    pass
    if out['min_wavelength'] is None and math.isfinite(table_min):
        out['min_wavelength'] = table_min
    if out['max_wavelength'] is None and math.isfinite(table_max):
        out['max_wavelength'] = table_max
    state.GLASS_SPECS_CACHE[filename] = out
    return out
