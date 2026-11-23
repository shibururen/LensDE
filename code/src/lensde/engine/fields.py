"""Extracted paper engine: fields."""
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
from . import geometry as _lensde_geometry
from . import initialization as _lensde_initialization
from . import materials as _lensde_materials
from . import orchestration as _lensde_orchestration
from . import populations as _lensde_populations
from . import provenance as _lensde_provenance
from . import tracing as _lensde_tracing

def post_asphere_field_reference_radius(options: dict, system: dict) -> float:
    """Return the immutable system-field radius represented by node 1.0.

    ``hx``/``hy`` are normalized multipliers of ``max_field_deg``.  A
    main-field-only contract deliberately ends below the full field, so its
    largest member must never become the new normalization radius when edge
    guards or the 41-field audit are added later.  Historical or external
    callers may provide an explicit reference, but the native system contract
    always maps node 1.0 to radius 1.0.
    """
    configured = _lensde_configuration.finite_float(options.get(state.POST_ASPHERE_FIELD_REFERENCE_RADIUS_OPTION), math.nan)
    if math.isfinite(configured) and configured > 1e-12:
        return configured
    return 1.0


def funnel_asphere_field_nodes(target_count: int) -> list[float]:
    target_count = max(1, int(target_count or 1))
    templates = {1: [0.0], 2: [0.0, 1.0], 3: [0.0, 0.707, 1.0], 4: [0.0, 0.5, 0.85, 1.0], 5: [0.0, 0.35, 0.707, 0.9, 1.0], 6: [0.0, 0.25, 0.5, 0.707, 0.9, 1.0], 7: [0.0, 0.25, 0.5, 0.707, 0.85, 0.95, 1.0]}
    if target_count in templates:
        return templates[target_count]
    return [index / float(target_count - 1) for index in range(target_count)]


def interpolate_field_numeric_value(ranked_fields: list[tuple[float, dict]], target_radius: float, key: str) -> float | None:
    points: list[tuple[float, float]] = []
    for radius, field in ranked_fields:
        value = _lensde_geometry.optional_finite_float(field.get(key))
        if value is not None:
            points.append((radius, value))
    if not points:
        return None
    if target_radius <= points[0][0]:
        return points[0][1]
    if target_radius >= points[-1][0]:
        return points[-1][1]
    for (left_radius, left_value), (right_radius, right_value) in zip(points, points[1:], strict=False):
        if left_radius <= target_radius <= right_radius:
            span = right_radius - left_radius
            if abs(span) <= 1e-12:
                return left_value
            t = (target_radius - left_radius) / span
            return left_value + t * (right_value - left_value)
    return min(points, key=lambda item: abs(item[0] - target_radius))[1]


def densify_system_fields_for_asphere(system: dict, target_count: int, *, preserve_existing_indices: bool=False, preferred_nodes: list[float] | tuple[float, ...] | None=None) -> dict:
    out = copy.deepcopy(system)
    fields = out.get('fields')
    if not isinstance(fields, list) or not fields:
        fields = [{'hx': 0.0, 'hy': 0.0, 'fvcx': 0.0, 'fvcy': 0.0, 'fvdx': 0.0, 'fvdy': 0.0}]
    fields = [copy.deepcopy(field) for field in fields if isinstance(field, dict)]
    if not fields:
        fields = [{'hx': 0.0, 'hy': 0.0, 'fvcx': 0.0, 'fvcy': 0.0, 'fvdx': 0.0, 'fvdy': 0.0}]
    target_count = max(1, int(target_count or 1))
    if len(fields) >= target_count:
        out['fields'] = fields
        return out
    ranked = sorted(((_lensde_geometry.field_radius(field), field) for field in fields), key=lambda item: item[0])
    max_radius, max_field = ranked[-1]
    if max_radius <= 1e-12:
        out['fields'] = fields
        return out
    direction_x = _lensde_configuration.finite_float(max_field.get('hx'), 0.0) / max_radius
    direction_y = _lensde_configuration.finite_float(max_field.get('hy'), 0.0) / max_radius
    if math.hypot(direction_x, direction_y) <= 1e-12:
        direction_x, direction_y = (0.0, 1.0)
    numeric_keys: set[str] = {'fvcx', 'fvcy', 'fvdx', 'fvdy', 'vignette_x', 'vignette_y'}
    for field in fields:
        for key, value in field.items():
            if key in {'hx', 'hy'}:
                continue
            if _lensde_geometry.optional_finite_float(value) is not None:
                numeric_keys.add(str(key))
    combined = [copy.deepcopy(field) for field in fields]
    existing_norms = [radius / max_radius for radius, _ in ranked]
    preferred_candidates = [max(0.0, min(1.0, _lensde_configuration.finite_float(node, 0.0))) for node in preferred_nodes or []]
    candidate_nodes = preferred_candidates or [max(0.0, min(1.0, _lensde_configuration.finite_float(node, 0.0))) for node in funnel_asphere_field_nodes(target_count)]
    candidate_nodes = [node for node in candidate_nodes if not any((abs(node - existing) <= 1e-06 for existing in existing_norms))]
    while len(combined) < target_count and candidate_nodes:
        if preferred_candidates:
            node = candidate_nodes[0]
        else:
            node = max(candidate_nodes, key=lambda candidate: (min((abs(candidate - existing) for existing in existing_norms)), candidate))
        candidate_nodes.remove(node)
        target_radius = max_radius * node
        nearest = min(ranked, key=lambda item: abs(item[0] - target_radius))[1]
        new_field = copy.deepcopy(nearest)
        new_field['hx'] = direction_x * target_radius
        new_field['hy'] = direction_y * target_radius
        for key in numeric_keys:
            value = interpolate_field_numeric_value(ranked, target_radius, key)
            if value is not None:
                new_field[key] = value
        if 'fvdx' in new_field and 'vignette_x' not in new_field:
            new_field['vignette_x'] = new_field['fvdx']
        if 'fvdy' in new_field and 'vignette_y' not in new_field:
            new_field['vignette_y'] = new_field['fvdy']
        if 'vignette_x' in new_field and 'fvdx' not in new_field:
            new_field['fvdx'] = new_field['vignette_x']
        if 'vignette_y' in new_field and 'fvdy' not in new_field:
            new_field['fvdy'] = new_field['vignette_y']
        combined.append(new_field)
        existing_norms.append(node)
    if not preserve_existing_indices:
        combined.sort(key=_lensde_geometry.field_radius)
    out['fields'] = combined
    return out


def resample_system_fields_for_asphere(system: dict, preferred_nodes: list[float] | tuple[float, ...], *, reference_max_radius: float | None=None) -> dict:
    """Build an exact one-dimensional field contract at normalized nodes.

    Unlike ``densify_system_fields_for_asphere``, this is allowed to replace
    the source field locations.  Non-coordinate field data is inherited from
    the nearest source field and every finite numeric field property (notably
    vignetting) is interpolated along the original field radius.
    """
    out = copy.deepcopy(system)
    nodes = sorted({max(0.0, min(1.0, _lensde_configuration.finite_float(node, math.nan))) for node in preferred_nodes if math.isfinite(_lensde_configuration.finite_float(node, math.nan))})
    if not nodes:
        return out
    fields = [copy.deepcopy(field) for field in (out.get('fields', []) if isinstance(out, dict) else []) if isinstance(field, dict)]
    if not fields:
        fields = [{'hx': 0.0, 'hy': 0.0, 'fvcx': 0.0, 'fvcy': 0.0, 'fvdx': 0.0, 'fvdy': 0.0}]
    ranked = sorted(((_lensde_geometry.field_radius(field), field) for field in fields), key=lambda item: item[0])
    source_max_radius, max_field = ranked[-1]
    if source_max_radius <= 1e-12:
        out['fields'] = fields
        return out
    contract_max_radius = _lensde_configuration.finite_float(reference_max_radius, math.nan)
    if not math.isfinite(contract_max_radius) or contract_max_radius <= 1e-12:
        contract_max_radius = source_max_radius
    direction_x = _lensde_configuration.finite_float(max_field.get('hx'), 0.0) / source_max_radius
    direction_y = _lensde_configuration.finite_float(max_field.get('hy'), 0.0) / source_max_radius
    if math.hypot(direction_x, direction_y) <= 1e-12:
        direction_x, direction_y = (0.0, 1.0)
    numeric_keys: set[str] = {'fvcx', 'fvcy', 'fvdx', 'fvdy', 'vignette_x', 'vignette_y'}
    for field in fields:
        for key, value in field.items():
            if key not in {'hx', 'hy'} and _lensde_geometry.optional_finite_float(value) is not None:
                numeric_keys.add(str(key))
    sampled_fields: list[dict] = []
    for node in nodes:
        target_radius = contract_max_radius * node
        nearest = min(ranked, key=lambda item: abs(item[0] - target_radius))[1]
        new_field = copy.deepcopy(nearest)
        new_field['hx'] = direction_x * target_radius
        new_field['hy'] = direction_y * target_radius
        for key in numeric_keys:
            value = interpolate_field_numeric_value(ranked, target_radius, key)
            if value is not None:
                new_field[key] = value
        if 'fvdx' in new_field and 'vignette_x' not in new_field:
            new_field['vignette_x'] = new_field['fvdx']
        if 'fvdy' in new_field and 'vignette_y' not in new_field:
            new_field['vignette_y'] = new_field['fvdy']
        if 'vignette_x' in new_field and 'fvdx' not in new_field:
            new_field['fvdx'] = new_field['vignette_x']
        if 'vignette_y' in new_field and 'fvdy' not in new_field:
            new_field['fvdy'] = new_field['vignette_y']
        sampled_fields.append(new_field)
    out['fields'] = sampled_fields
    return out


def normalized_field_nodes(system: dict, *, reference_max_radius: float | None=None) -> list[float]:
    fields = [field for field in (system.get('fields', []) if isinstance(system, dict) else []) if isinstance(field, dict)]
    if not fields:
        return []
    radii = [_lensde_geometry.field_radius(field) for field in fields]
    max_radius = _lensde_configuration.finite_float(reference_max_radius, math.nan)
    if not math.isfinite(max_radius) or max_radius <= 1e-12:
        max_radius = max(radii, default=0.0)
    if max_radius <= 1e-12:
        return [0.0 for _field in fields]
    return [radius / max_radius for radius in radii]


def exact_fields_for_normalized_nodes(system: dict, nodes: list[float] | tuple[float, ...], *, reference_max_radius: float=1.0) -> list[dict] | None:
    """Select an existing one-dimensional field contract without interpolation."""
    fields = [copy.deepcopy(field) for field in (system.get('fields', []) if isinstance(system, dict) else []) if isinstance(field, dict)]
    reference = _lensde_configuration.finite_float(reference_max_radius, 1.0)
    if reference <= 1e-12:
        return None
    selected: list[dict] = []
    for node in nodes:
        target = reference * _lensde_configuration.finite_float(node, math.nan)
        if not math.isfinite(target):
            return None
        match = next((field for field in fields if abs(_lensde_geometry.field_radius(field) - target) <= 1e-06), None)
        if match is None:
            return None
        selected.append(copy.deepcopy(match))
    return selected


def vignetting_bridge_missing_pupil_samples(experiment_dir: Path) -> bool:
    return any(('no traceable pupil samples were found' in path.read_text(encoding='utf-8', errors='replace') for path in (native_io_paths.named_child(experiment_dir, 'set_vignetting') / 'stderr.log',) if path.is_file()))


def vignetting_bridge_experiment_dir(output_root: Path) -> Path:
    """Keep bridge worker paths below the legacy Windows path limit."""
    preferred = native_io_paths.named_child(output_root, 'experiment')
    if len(str(preferred.resolve())) <= 180:
        return preferred
    digest = hashlib.sha1(str(output_root.resolve()).encode('utf-8', errors='surrogatepass')).hexdigest()[:12]
    run_root = next((ancestor for ancestor in (output_root, *output_root.parents) if ancestor.name.startswith('run_') or (ancestor / 'queue_status.json').is_file()), None)
    if run_root is None:
        raise OSError(f'Vignetting bridge path is too long outside an experiment root: {output_root}. Shorten its stage names; temporary output relocation is disabled.')
    compact_root = run_root / '_vfb'
    return compact_root / digest


def aperture_realized_vignetting_field_bridge(system: dict, merit: dict, target_nodes: list[float] | tuple[float, ...], output_root: Path, *, variables: list[dict] | None=None, reference_max_radius: float=1.0, timeout_seconds: int=0, recalibrate_existing_contract: bool=False) -> tuple[dict, dict]:
    """Solve a new field contract from apertures established by old fields.

    The operation is intentionally one-dimensional (Hy), matching the current
    optimization contract.  Existing field coefficients are never interpolated:
    old fields first materialize and freeze the clear apertures, then the normal
    GUI-equivalent strict AdaptivePilotGrid Set Vignetting finds every target
    field.  Strict APG and calibrated-paraxial retraces then update the aperture
    envelope; the calibrated-paraxial result is the optimizer handoff.
    """
    minimum = _lensde_configuration.vignetting_minimum_relative_illumination(merit)
    if not 0.0 < minimum <= 1.0:
        raise ValueError('aperture-realized vignetting bridge requires an RI floor')
    requested_nodes = [_lensde_configuration.finite_float(node, math.nan) for node in target_nodes]
    if not requested_nodes or any((not math.isfinite(node) or node < 0.0 for node in requested_nodes)):
        raise ValueError('target field nodes must be finite and non-negative')
    if len(set(requested_nodes)) != len(requested_nodes):
        raise ValueError('target field nodes must be unique')
    reference = _lensde_configuration.finite_float(reference_max_radius, 1.0)
    if reference <= 1e-12:
        raise ValueError('field reference radius must be positive')
    unique_sorted_nodes = sorted((node * reference for node in requested_nodes))
    if abs(unique_sorted_nodes[0]) > 1e-12:
        raise ValueError('target field contract must include the on-axis node')
    exact = exact_fields_for_normalized_nodes(system, requested_nodes, reference_max_radius=reference)
    ray_aiming = system.get('ray_aiming') if isinstance(system, dict) else None
    source_mode = _lensde_configuration.canonical_aiming_mode(ray_aiming.get('mode') if isinstance(ray_aiming, dict) else None, 'paraxial')
    if not recalibrate_existing_contract and exact is not None and (len(exact) == len(system.get('fields', []) or [])):
        inherited = copy.deepcopy(system)
        inherited['fields'] = exact
        return (inherited, {'performed': False, 'reason': 'target_field_contract_already_present', 'target_nodes': requested_nodes, 'target_physical_field_radii': unique_sorted_nodes, 'target_field_hy_interpolation': False, 'source_aiming_mode': source_mode, 'set_vignetting_aiming_mode': source_mode})
    output_root.mkdir(parents=True, exist_ok=True)
    source_path = output_root / 'source_system.json'
    merit_path = output_root / 'source_merit.json'
    variables_path = output_root / 'source_variables.json'
    _lensde_provenance.write_json(source_path, copy.deepcopy(system))
    _lensde_provenance.write_json(merit_path, copy.deepcopy(merit))
    scoped_variables = [copy.deepcopy(variable) for variable in variables or [] if isinstance(variable, dict)]
    if scoped_variables:
        _lensde_provenance.write_json(variables_path, {'variables': scoped_variables})
    executable = state.CPP_ROOT / 'build' / 'lensde_cpu.exe'
    if not executable.is_file():
        raise FileNotFoundError(f'vignetting bridge CPU executable not found: {executable}')
    aperture_margin = 1.0

    def make_command(experiment_path: Path, aiming_policy: str) -> list[str]:
        command = [_lensde_defaults.dependency_python_executable(('numpy',)), '-m', 'lensde.helpers.run_vignetting_aperture_realized_bridge_experiment', '--source-system', str(source_path), '--source-merit', str(merit_path), '--executable', str(executable), '--output-dir', str(experiment_path), '--field-nodes', ','.join((f'{node:.17g}' for node in unique_sorted_nodes)), '--minimum-relative-illumination', f'{minimum:.17g}', '--aperture-margin', f'{aperture_margin:.17g}', '--set-vignetting-aiming', aiming_policy]
        if scoped_variables:
            command.extend(['--source-variables', str(variables_path)])
        return command
    experiment_dir = vignetting_bridge_experiment_dir(output_root)
    command = make_command(experiment_dir, 'gui_default')
    _lensde_provenance.write_json(output_root / 'command.json', {'command': command})
    started = time.perf_counter()
    attempts: list[dict] = []

    def run_attempt(experiment_path: Path, aiming_policy: str, log_suffix: str) -> tuple[subprocess.CompletedProcess[str], Path, dict]:
        attempt_command = make_command(experiment_path, aiming_policy)
        attempt_started = time.perf_counter()
        result = subprocess.run(attempt_command, cwd=state.CPP_ROOT, text=True, encoding='utf-8', errors='replace', stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False, timeout=timeout_seconds if timeout_seconds > 0 else None)
        (output_root / f'stdout{log_suffix}.log').write_text(result.stdout or '', encoding='utf-8', errors='replace')
        (output_root / f'stderr{log_suffix}.log').write_text(result.stderr or '', encoding='utf-8', errors='replace')
        attempt_comparison_path = experiment_path / 'bridge_comparison.json'
        attempt_comparison = _lensde_provenance.read_json(attempt_comparison_path) if attempt_comparison_path.is_file() else {}
        attempts.append({'aiming_policy': aiming_policy, 'command': attempt_command, 'experiment_dir': str(experiment_path.resolve()), 'returncode': result.returncode, 'passed': bool(attempt_comparison.get('passed', False)), 'missing_pupil_samples': vignetting_bridge_missing_pupil_samples(experiment_path), 'elapsed_seconds': time.perf_counter() - attempt_started})
        return (result, attempt_comparison_path, attempt_comparison)
    completed, comparison_path, comparison = run_attempt(experiment_dir, 'gui_default', '')
    fallback_used = False
    _lensde_provenance.write_json(output_root / 'bridge_attempts.json', attempts)
    if completed.returncode != 0 or not bool(comparison.get('passed', False)):
        raise RuntimeError(f'aperture-realized vignetting field bridge failed: rc={completed.returncode}, output={output_root}')
    realized_path = experiment_dir / 'aperture_realized_y_only_system.json'
    realized = _lensde_provenance.read_json(realized_path) if realized_path.is_file() else {}
    if not isinstance(realized, dict) or not realized:
        raise FileNotFoundError(realized_path)
    reordered = exact_fields_for_normalized_nodes(realized, requested_nodes, reference_max_radius=reference)
    if reordered is None:
        raise RuntimeError('vignetting bridge output does not match the target nodes')
    realized['fields'] = reordered
    acceptance = comparison.get('acceptance', {})
    bridge_policy = comparison.get('bridge_policy', {})
    report = {'performed': True, 'passed': True, 'calibration_diagnostics_passed': comparison.get('calibration_diagnostics_passed'), 'fallback_used': bool(comparison.get('fallback_used')), 'source_pupil_retained': bool(comparison.get('source_pupil_retained')), 'apg_retrace_is_diagnostic_not_admission': True, 'cp_per_ray_convergence_gate': False, 'target_nodes': requested_nodes, 'target_physical_field_radii': unique_sorted_nodes, 'target_field_hy_interpolation': False, 'source_fields_determine_apertures': True, 'aperture_materialization_requires_full_trace': bool(bridge_policy.get('aperture_materialization_requires_full_trace', False)), 'source_aiming_mode': source_mode, 'set_vignetting_aiming_mode': bridge_policy.get('set_vignetting_aiming_mode'), 'set_vignetting_aiming_fallback_used': fallback_used, 'handoff_aiming_mode': _lensde_configuration.canonical_aiming_mode(realized.get('ray_aiming', {}).get('mode') if isinstance(realized.get('ray_aiming'), dict) else None, source_mode), 'attempts': attempts, 'minimum_relative_illumination': minimum, 'apg_trace_coverage': acceptance.get('apg_trace_coverage'), 'calibrated_trace_coverage': acceptance.get('calibrated_trace_coverage'), 'trace_coverage_absolute_difference': acceptance.get('trace_coverage_absolute_difference'), 'apg_min_field_coverage': acceptance.get('apg_min_field_coverage'), 'calibrated_min_field_coverage': acceptance.get('calibrated_min_field_coverage'), 'min_field_coverage_absolute_difference': acceptance.get('min_field_coverage_absolute_difference'), 'aperture_stability': comparison.get('aperture_stability', {}), 'admission_uses_merit': bridge_policy.get('admission_uses_merit'), 'post_measurement_clear_aperture_update': bridge_policy.get('post_measurement_clear_aperture_update'), 'comparison': str(comparison_path.resolve()), 'elapsed_seconds': time.perf_counter() - started}
    _lensde_provenance.write_json(output_root / 'vignetting_bridge_report.json', report)
    return (realized, report)


def canonical_post_asphere_field_coordinate(value: object) -> str:
    token = str(value or 'chief_image_height').strip().lower().replace('-', '_')
    if token in {'chief_image_height', 'chief_ray_image_height', 'image_height', 'actual_image_height'}:
        return 'chief_image_height'
    return 'normalized_field'


def canonical_post_asphere_field_contract_policy(value: object) -> str:
    token = str(value or 'locked').strip().lower().replace('-', '_')
    if token in {'adaptive_additions', 'initial_adaptive_additions', 'legacy_adaptive'}:
        return 'adaptive_additions'
    return 'locked'


def primary_wavelength_um_for_system(system: dict) -> float | None:
    waves = system.get('wavelengths', []) if isinstance(system, dict) else []
    if not isinstance(waves, list):
        return None
    finite_waves: list[float] = []
    for wave in waves:
        if not isinstance(wave, dict):
            continue
        value = _lensde_configuration.finite_float(wave.get('value_um'), math.nan)
        if not math.isfinite(value):
            continue
        finite_waves.append(value)
        if _lensde_commands.option_truthy(wave.get('primary'), False):
            return value
    return finite_waves[0] if finite_waves else None


def chief_image_height_field_contract(system: dict, spot_path: Path, target_count: int, *, max_node: float=1.0, include_max_node: bool=True) -> dict:
    """Invert the primary-wavelength chief-ray image-height curve.

    The field probe deliberately writes a one-ring hexapolar spot file, whose
    ray index zero is the pupil centre.  The returned field nodes are therefore
    uniformly spaced in traced chief-ray height rather than in object-space Hy.
    """
    audit: dict = {'valid': False, 'source': 'spot_csv_primary_wavelength_central_pupil', 'spot_path': str(spot_path), 'target_count': max(1, int(target_count or 1)), 'max_node': max(0.0, min(1.0, _lensde_configuration.finite_float(max_node, 1.0))), 'include_max_node': bool(include_max_node), 'selected_nodes': [], 'curve': []}
    if not spot_path.exists():
        audit['reason'] = 'spot.csv not found'
        return audit
    centre_rows: list[dict] = []
    try:
        with spot_path.open('r', encoding='utf-8', errors='replace', newline='') as handle:
            for row in csv.DictReader(handle):
                try:
                    ray_index = int(float(row.get('ray_index', '-1')))
                except (TypeError, ValueError):
                    continue
                valid = str(row.get('valid', '0')).strip().lower() not in {'', '0', 'false', 'no'}
                values = {key: _lensde_configuration.finite_float(row.get(key), math.nan) for key in ('field_hx', 'field_hy', 'wavelength_um', 'x_mm', 'y_mm')}
                if ray_index == 0 and valid and all((math.isfinite(value) for value in values.values())):
                    centre_rows.append(values)
    except OSError as exc:
        audit['reason'] = f'spot.csv read failed: {exc}'
        return audit
    if not centre_rows:
        audit['reason'] = 'no valid central-pupil rays'
        return audit
    requested_primary = primary_wavelength_um_for_system(system)
    available_waves = sorted({row['wavelength_um'] for row in centre_rows})
    selected_wave = min(available_waves, key=lambda value: abs(value - requested_primary)) if requested_primary is not None else available_waves[0]
    wave_tolerance = max(1e-10, 1e-08 * abs(selected_wave))
    wave_rows = [row for row in centre_rows if abs(row['wavelength_um'] - selected_wave) <= wave_tolerance]
    audit['requested_primary_wavelength_um'] = requested_primary
    audit['selected_wavelength_um'] = selected_wave
    fields = [field for field in (system.get('fields', []) if isinstance(system, dict) else []) if isinstance(field, dict)]
    if len(wave_rows) < len(fields) or not fields:
        audit['reason'] = 'primary-wavelength chief rays do not cover every probe field'
        return audit
    max_field_radius = max((_lensde_geometry.field_radius(field) for field in fields), default=0.0)
    if max_field_radius <= 1e-12:
        audit['reason'] = 'probe system has no finite off-axis field'
        return audit
    coordinate_tolerance = max(1e-09, 1e-07 * max_field_radius)
    traced: list[dict] = []
    used_rows: set[int] = set()
    for field in fields:
        hx = _lensde_configuration.finite_float(field.get('hx'), 0.0)
        hy = _lensde_configuration.finite_float(field.get('hy'), 0.0)
        candidates = [(math.hypot(row['field_hx'] - hx, row['field_hy'] - hy), index, row) for index, row in enumerate(wave_rows) if index not in used_rows]
        if not candidates:
            audit['reason'] = 'missing chief ray for a probe field'
            return audit
        distance, row_index, row = min(candidates, key=lambda item: item[0])
        if distance > coordinate_tolerance:
            audit['reason'] = 'spot field coordinates do not match the probe contract'
            return audit
        used_rows.add(row_index)
        traced.append({'node': _lensde_geometry.field_radius(field) / max_field_radius, 'field_hx': hx, 'field_hy': hy, 'chief_x_mm': row['x_mm'], 'chief_y_mm': row['y_mm']})
    traced.sort(key=lambda row: row['node'])
    if traced[0]['node'] > 1e-06 or traced[-1]['node'] < 1.0 - 1e-06:
        audit['reason'] = 'probe contract does not span normalized field 0 to 1'
        return audit
    origin_x = traced[0]['chief_x_mm']
    origin_y = traced[0]['chief_y_mm']
    raw_heights = [math.hypot(row['chief_x_mm'] - origin_x, row['chief_y_mm'] - origin_y) for row in traced]
    max_height = max(raw_heights, default=0.0)
    if max_height <= 1e-12:
        audit['reason'] = 'chief-ray image height has zero span'
        return audit
    max_backward_step = max((left - right for left, right in zip(raw_heights, raw_heights[1:], strict=False)), default=0.0)
    monotonic_tolerance = max(1e-09, 0.005 * max_height)
    if max_backward_step > monotonic_tolerance:
        audit.update({'reason': 'chief-ray image-height curve is not monotonic', 'max_backward_step_mm': max_backward_step, 'monotonic_tolerance_mm': monotonic_tolerance})
        return audit
    monotonic_heights: list[float] = []
    running_height = 0.0
    for row, height in zip(traced, raw_heights, strict=False):
        running_height = max(running_height, height)
        monotonic_heights.append(running_height)
        row['image_height_mm'] = height
        row['monotonic_image_height_mm'] = running_height
        row['normalized_image_height'] = running_height / max_height
    audit['curve'] = traced
    audit['max_image_height_mm'] = max_height
    audit['max_backward_step_mm'] = max_backward_step
    audit['monotonic_tolerance_mm'] = monotonic_tolerance
    target_count = max(1, int(target_count or 1))
    max_node = max(0.0, min(1.0, _lensde_configuration.finite_float(max_node, 1.0)))
    target_max_height: float | None = None
    for left_index in range(len(traced) - 1):
        left_node = traced[left_index]['node']
        right_node = traced[left_index + 1]['node']
        if left_node <= max_node <= right_node:
            node_span = right_node - left_node
            if node_span <= 1e-12:
                target_max_height = monotonic_heights[left_index + 1]
            else:
                ratio = (max_node - left_node) / node_span
                target_max_height = monotonic_heights[left_index] + ratio * (monotonic_heights[left_index + 1] - monotonic_heights[left_index])
            break
    if target_max_height is None or target_max_height <= 1e-12:
        audit['reason'] = 'main-field image-height range has zero span'
        return audit
    if target_count == 1:
        nodes = [0.0]
        target_fractions = [0.0]
    else:
        nodes = []
        denominator = float(target_count - 1) if include_max_node else float(target_count)
        target_fractions = [index / denominator for index in range(target_count)]
        for index in range(target_count):
            fraction = target_fractions[index]
            target_height = fraction * target_max_height
            if index == 0:
                nodes.append(0.0)
                continue
            if include_max_node and index == target_count - 1:
                nodes.append(max_node)
                continue
            selected_node: float | None = None
            for left_index in range(len(traced) - 1):
                left_height = monotonic_heights[left_index]
                right_height = monotonic_heights[left_index + 1]
                if left_height <= target_height <= right_height:
                    span = right_height - left_height
                    if span <= 1e-12:
                        selected_node = traced[left_index + 1]['node']
                    else:
                        ratio = (target_height - left_height) / span
                        selected_node = traced[left_index]['node'] + ratio * (traced[left_index + 1]['node'] - traced[left_index]['node'])
                    break
            if selected_node is None:
                audit['reason'] = 'could not invert chief-ray image-height curve'
                return audit
            nodes.append(max(0.0, min(1.0, selected_node)))
    if len(nodes) != len({round(node, 9) for node in nodes}):
        audit['reason'] = 'chief-ray image-height inversion produced duplicate fields'
        return audit
    audit.update({'valid': True, 'reason': 'ok', 'target_image_height_fractions': target_fractions, 'target_max_image_height_mm': target_max_height, 'selected_nodes': nodes})
    return audit


def interpolate_sampled_curve(samples: list[tuple[float, float]], node: float) -> float | None:
    finite_samples = sorted(((_lensde_configuration.finite_float(sample_node, math.nan), _lensde_configuration.finite_float(value, math.nan)) for sample_node, value in samples), key=lambda item: item[0])
    finite_samples = [(sample_node, value) for sample_node, value in finite_samples if math.isfinite(sample_node) and math.isfinite(value)]
    if not finite_samples:
        return None
    if node <= finite_samples[0][0]:
        return finite_samples[0][1]
    if node >= finite_samples[-1][0]:
        return finite_samples[-1][1]
    for (left_node, left_value), (right_node, right_value) in zip(finite_samples, finite_samples[1:], strict=False):
        if left_node <= node <= right_node:
            span = right_node - left_node
            if abs(span) <= 1e-12:
                return left_value
            fraction = (node - left_node) / span
            return left_value + fraction * (right_value - left_value)
    return None


def select_adaptive_post_asphere_field_nodes(existing_nodes: list[float], probe_rows: list[dict], target_count: int, min_excess_um: float=0.05, *, max_field_deg: float=0.0, sample_coverage_deg: float=2.5, fill_to_target: bool=True) -> dict:
    """Select a shared field contract from physically grouped RMS anomalies.

    Positive interpolation bulges are grouped by repeatedly taking the worst
    remaining peak and assigning every anomaly within one field's physical
    coverage radius to that region.  Every independent region receives one
    representative before any region receives a second field.  When there are
    more regions than slots, integrated excess and peak height rank them.
    """
    normalized_existing = sorted({max(0.0, min(1.0, _lensde_configuration.finite_float(node, 0.0))) for node in existing_nodes})
    target_count = max(len(normalized_existing), int(target_count or 0))
    additions_needed = max(0, target_count - len(normalized_existing))
    measurements = sorted([{**row, 'node': max(0.0, min(1.0, _lensde_configuration.finite_float(row.get('node'), math.nan))), 'rms_spot_um': _lensde_configuration.finite_float(row.get('rms_spot_um'), math.nan)} for row in probe_rows if isinstance(row, dict) and math.isfinite(_lensde_configuration.finite_float(row.get('node'), math.nan)) and math.isfinite(_lensde_configuration.finite_float(row.get('rms_spot_um'), math.nan))], key=lambda row: row['node'])

    def measurement_at(node: float) -> dict | None:
        if not measurements:
            return None
        exact = min(measurements, key=lambda item: abs(item['node'] - node))
        if abs(exact['node'] - node) <= 1e-06:
            return exact
        value = interpolate_sampled_curve([(row['node'], row['rms_spot_um']) for row in measurements], node)
        if value is None or not math.isfinite(value):
            return None
        return {'node': node, 'rms_spot_um': value, 'interpolated': True}
    threshold = max(0.0, _lensde_configuration.finite_float(min_excess_um, 0.05))
    physical_max_field_deg = abs(_lensde_configuration.finite_float(max_field_deg, 0.0))
    physical_angles_available = physical_max_field_deg > 1e-09
    coverage_deg = max(0.0, _lensde_configuration.finite_float(sample_coverage_deg, 2.5))
    node_differences = [right['node'] - left['node'] for left, right in zip(measurements, measurements[1:], strict=False) if right['node'] - left['node'] > 1e-09]
    sorted_differences = sorted(node_differences)
    probe_step_node = sorted_differences[len(sorted_differences) // 2] if sorted_differences else 1.0
    if physical_angles_available:
        coverage_node = min(1.0, coverage_deg / physical_max_field_deg)
        probe_step_deg = probe_step_node * physical_max_field_deg
    else:
        coverage_node = min(1.0, max(probe_step_node, 2.5 * probe_step_node))
        probe_step_deg = probe_step_node
    measured_curve = [(row['node'], row['rms_spot_um']) for row in measurements]
    base_anchors = [(node, value) for node in normalized_existing for value in [interpolate_sampled_curve(measured_curve, node)] if value is not None and math.isfinite(value)]
    annotated_rows: list[dict] = []
    for row in measurements:
        if any((abs(row['node'] - node) <= 1e-06 for node in normalized_existing)):
            continue
        predicted = interpolate_sampled_curve(base_anchors, row['node'])
        signed_error = row['rms_spot_um'] - predicted if predicted is not None and math.isfinite(predicted) else -math.inf
        annotated_rows.append({**row, 'angle_deg': row['node'] * physical_max_field_deg if physical_angles_available else None, 'base_predicted_rms_spot_um': predicted, 'base_signed_interpolation_error_um': signed_error if math.isfinite(signed_error) else None, 'base_positive_excess_um': max(0.0, signed_error) if math.isfinite(signed_error) else 0.0})
    anomaly_rows = [row for row in annotated_rows if row['base_positive_excess_um'] >= threshold and row['base_positive_excess_um'] > 1e-12]
    region_join_gap_node = max(1.5 * probe_step_node, coverage_node)
    grouped_rows: list[list[dict]] = []
    for row in anomaly_rows:
        if not grouped_rows or row['node'] - grouped_rows[-1][-1]['node'] > region_join_gap_node + 1e-09:
            grouped_rows.append([row])
        else:
            grouped_rows[-1].append(row)
    regions: list[dict] = []
    for members in grouped_rows:
        peak = max(members, key=lambda row: (row['base_positive_excess_um'], row['rms_spot_um'], row['node']))
        start_node = min((row['node'] for row in members))
        end_node = max((row['node'] for row in members))
        integrated_excess = sum((row['base_positive_excess_um'] * probe_step_deg for row in members))
        peak_excess = peak['base_positive_excess_um']
        severity = integrated_excess + peak_excess * max(probe_step_deg, coverage_deg if physical_angles_available else coverage_node)
        regions.append({'region_id': len(regions) + 1, 'representative_node': peak['node'], 'representative_angle_deg': peak['angle_deg'], 'peak_rms_spot_um': peak['rms_spot_um'], 'peak_excess_um': peak_excess, 'start_node': start_node, 'end_node': end_node, 'start_angle_deg': start_node * physical_max_field_deg if physical_angles_available else None, 'end_angle_deg': end_node * physical_max_field_deg if physical_angles_available else None, 'covered_angle_min_deg': max(0.0, peak['angle_deg'] - coverage_deg) if physical_angles_available else None, 'covered_angle_max_deg': min(physical_max_field_deg, peak['angle_deg'] + coverage_deg) if physical_angles_available else None, 'member_count': len(members), 'integrated_excess_um_deg': integrated_excess, 'severity_um_deg': severity, 'member_nodes': [row['node'] for row in members], 'allocated': False, 'allocated_field_nodes': []})
    ranked_regions = sorted(regions, key=lambda region: (region['severity_um_deg'], region['peak_excess_um'], region['representative_node']), reverse=True)
    selected = list(normalized_existing)
    new_nodes: list[float] = []
    steps: list[dict] = []
    for region in ranked_regions[:additions_needed]:
        node = region['representative_node']
        selected.append(node)
        selected.sort()
        new_nodes.append(node)
        region['allocated'] = True
        region['allocated_field_nodes'].append(node)
        steps.append({'node': node, 'angle_deg': region['representative_angle_deg'], 'rms_spot_um': region['peak_rms_spot_um'], 'predicted_rms_spot_um': region['peak_rms_spot_um'] - region['peak_excess_um'], 'signed_interpolation_error_um': region['peak_excess_um'], 'positive_excess_um': region['peak_excess_um'], 'reason': 'anomaly_region_representative', 'region_id': region['region_id'], 'coverage_radius_deg': coverage_deg if physical_angles_available else None})
    annotation_by_node = {round(row['node'], 12): row for row in annotated_rows}

    def anomaly_region_for_node(node: float) -> dict | None:
        return next((region for region in regions if any((abs(node - member) <= 1e-06 for member in region['member_nodes']))), None)
    for _index in range(max(0, additions_needed - len(new_nodes))):
        candidates = [row for row in measurements if not any((abs(row['node'] - node) <= 1e-06 for node in selected))]
        if not candidates:
            break
        outside_covered_radius = [row for row in candidates if not new_nodes or all((abs(row['node'] - node) >= coverage_node - 1e-09 for node in new_nodes))]
        if outside_covered_radius:
            candidates = outside_covered_radius
        anchors = [(node, row['rms_spot_um']) for node in selected for row in [measurement_at(node)] if row is not None]
        uncovered_anomaly_candidates: list[tuple[dict, dict, dict]] = []
        for row in candidates:
            annotation = annotation_by_node.get(round(row['node'], 12))
            region = anomaly_region_for_node(row['node'])
            if annotation is not None and region is not None and (annotation['base_positive_excess_um'] >= threshold):
                uncovered_anomaly_candidates.append((row, annotation, region))
        chosen_region: dict | None = None
        if uncovered_anomaly_candidates:
            row, annotation, chosen_region = max(uncovered_anomaly_candidates, key=lambda item: (item[2]['severity_um_deg'], item[1]['base_positive_excess_um'], min((abs(item[0]['node'] - node) for node in selected)), item[0]['node']))
            node = row['node']
            excess = annotation['base_signed_interpolation_error_um']
            predicted = annotation['base_predicted_rms_spot_um']
            nearest_gap = min((abs(node - selected_node) for selected_node in selected))
            reason = 'anomaly_region_additional_coverage'
        else:
            if not fill_to_target:
                break
            scored: list[tuple[float, float, float, dict]] = []
            for row in candidates:
                predicted = interpolate_sampled_curve(anchors, row['node'])
                excess = row['rms_spot_um'] - predicted if predicted is not None and math.isfinite(predicted) else -math.inf
                nearest_gap = min((abs(row['node'] - node) for node in selected))
                scored.append((excess, nearest_gap, row['node'], row))
            best_excess = max(scored, key=lambda item: (item[0], item[1], item[2]))
            if math.isfinite(best_excess[0]) and best_excess[0] >= threshold:
                chosen = best_excess
                reason = 'positive_interpolation_excess'
            else:
                best_shape = max(scored, key=lambda item: (abs(item[0]) if math.isfinite(item[0]) else -math.inf, item[1], item[2]))
                if math.isfinite(best_shape[0]) and abs(best_shape[0]) >= threshold:
                    chosen = best_shape
                    reason = 'interpolation_shape_resolution'
                else:
                    chosen = max(scored, key=lambda item: (item[1], item[2], item[0]))
                    reason = 'largest_gap_fallback'
            excess, nearest_gap, node, row = chosen
            predicted = interpolate_sampled_curve(anchors, node)
            chosen_region = anomaly_region_for_node(node)
        if chosen_region is not None and any((abs(node - member) <= 1e-06 for member in chosen_region['member_nodes'])):
            chosen_region['allocated_field_nodes'].append(node)
        selected.append(node)
        selected.sort()
        new_nodes.append(node)
        steps.append({'node': node, 'angle_deg': node * physical_max_field_deg if physical_angles_available else None, 'rms_spot_um': row['rms_spot_um'], 'predicted_rms_spot_um': predicted, 'signed_interpolation_error_um': excess if math.isfinite(excess) else None, 'positive_excess_um': max(0.0, excess) if math.isfinite(excess) else None, 'nearest_selected_gap': nearest_gap, 'reason': reason, 'region_id': chosen_region['region_id'] if chosen_region is not None else None})
    return {'policy': 'dense_probe_grouped_anomaly_regions', 'existing_nodes': normalized_existing, 'target_count': target_count, 'min_excess_um': threshold, 'max_field_deg': physical_max_field_deg if physical_angles_available else None, 'sample_coverage_radius_deg': coverage_deg if physical_angles_available else None, 'sample_coverage_node': coverage_node, 'probe_step_deg': probe_step_deg if physical_angles_available else None, 'anomaly_region_count': len(regions), 'allocated_region_count': sum((1 for region in regions if region['allocated'])), 'regions': sorted(regions, key=lambda region: region['region_id']), 'unallocated_region_ids': [region['region_id'] for region in regions if not region['allocated']], 'new_nodes': new_nodes, 'selected_nodes': selected, 'steps': steps, 'fill_to_target': bool(fill_to_target), 'complete': len(selected) >= target_count}


def make_post_seed_optimization_variables(system: dict, seed_variables: list[dict], source_variables: list[dict], options: dict) -> list[dict]:
    """Build the DE variable box without inheriting compact seed-only bounds.

    Initial population generation deliberately uses a compact thickness
    domain to make short, traceable systems easier to acquire.  That domain is
    a sampling policy, not a design constraint.  Once a traceable seed pool
    exists, thickness variables use the envelope of the seed domain and the
    user's project-variable domain.  Other templated variables return to their
    project bounds.  Explicit ``de_*`` overrides remain authoritative because
    they are the user's opt-in global DE constraints.
    """
    templates = {_lensde_geometry.variable_key(variable): variable for variable in source_variables}
    out = copy.deepcopy(seed_variables)
    for item in out:
        template = templates.get(_lensde_geometry.variable_key(item))
        if not template:
            continue
        source_lower = _lensde_geometry.optional_finite_float(template.get('lower'))
        source_upper = _lensde_geometry.optional_finite_float(template.get('upper'))
        if source_lower is None or source_upper is None:
            continue
        if str(item.get('kind', '')) == 'thickness':
            seed_lower = _lensde_geometry.optional_finite_float(item.get('lower'))
            seed_upper = _lensde_geometry.optional_finite_float(item.get('upper'))
            lower = min((value for value in (seed_lower, source_lower) if value is not None))
            upper = max((value for value in (seed_upper, source_upper) if value is not None))
        else:
            lower = source_lower
            upper = source_upper
        if lower >= upper:
            continue
        item['lower'] = lower
        item['upper'] = upper
        item['initial'] = _lensde_populations.clamp_value(_lensde_configuration.finite_float(item.get('initial'), lower), lower, upper)
    return _lensde_commands.apply_de_bound_overrides(system, out, options)


def initial_search_curvature_domain_bound(options: dict) -> float:
    """Return the seed-variable domain cap, independent of sampling scouts.

    ``initial_search_curvature_bound`` is a population sampling scale.  Using
    it to build ``seed_variables.json`` made every larger cap-scout proposal
    collapse back into the same smaller box.  The variable domain instead
    follows the project/rest curvature envelope; individual seed generators
    remain free to apply their requested 0.01/0.02/0.04 sampling caps inside
    that domain.
    """
    return max(1e-06, _lensde_configuration.finite_float(options.get('initial_search_rest_curvature_cap'), state.DEFAULT_CURVATURE_BOUND))


def make_first_order_variables(project: dict, system: dict, target_bfl: float, target_ttl: float, curvature_bound: float=state.INITIAL_SEARCH_CURVATURE_BOUND, allow_default_variables: bool=True, adaptive_thickness: bool=True, target_efl: float=0.0, thickness_constraints: dict | None=None, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5, use_current_thickness_prior: bool=False) -> list[dict]:
    surfaces = system.get('surfaces', [])
    variables = []
    source_variables = [copy.deepcopy(v) for v in project.get('variables', [])]
    if not source_variables and (not allow_default_variables):
        return []
    if not source_variables:
        for index, surface in enumerate(surfaces):
            kind = str(surface.get('kind', '')).lower()
            if kind in {'object', 'image'}:
                continue
            if not surface.get('is_stop') or _lensde_populations.is_physical_stop_surface(surface):
                source_variables.append({'kind': 'curvature', 'index': index, 'term': 0, 'lower': -state.DEFAULT_CURVATURE_BOUND, 'upper': state.DEFAULT_CURVATURE_BOUND, 'initial': _lensde_defaults.curvature_from_radius(surface.get('radius')), 'role': f'initial-search curvature @ S{index}'})
            source_variables.append({'kind': 'thickness', 'index': index, 'term': 0, 'lower': None, 'upper': None, 'initial': _lensde_configuration.finite_float(surface.get('thickness'), 2.0), 'role': f'initial-search thickness @ S{index}'})
    thickness_budget = _lensde_geometry.adaptive_thickness_budget(system, target_ttl, target_bfl, target_efl, thickness_constraints=thickness_constraints, bfl_surface_index=bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor, use_current_thickness_prior=use_current_thickness_prior) if adaptive_thickness else {}
    for variable in source_variables:
        kind = str(variable.get('kind', ''))
        if kind not in {'curvature', 'thickness'}:
            continue
        index = int(variable.get('index', 0) or 0)
        if not 0 <= index < len(surfaces):
            continue
        surface = surfaces[index]
        if kind == 'curvature' and surface.get('is_stop') and (not _lensde_populations.is_physical_stop_surface(surface)):
            continue
        item = copy.deepcopy(variable)
        item['term'] = 0
        if kind == 'curvature':
            bound = max(1e-06, _lensde_configuration.finite_float(curvature_bound, state.INITIAL_SEARCH_CURVATURE_BOUND))
            lo, hi = _lensde_geometry.clamped_bounds(item.get('lower'), item.get('upper'), -bound, bound)
            item['lower'] = lo
            item['upper'] = hi
            item['initial'] = min(max(_lensde_configuration.finite_float(item.get('initial'), 0.0), lo), hi)
            item['role'] = item.get('role') or f'first-order curvature @ S{index}'
        else:
            limit_lower, limit_upper = _lensde_geometry.adaptive_interval_bounds(system, index, thickness_budget, bfl_surface_index=bfl_surface_index)
            lo, hi = _lensde_geometry.clamped_bounds(item.get('lower'), item.get('upper'), limit_lower, limit_upper)
            item['lower'] = lo
            item['upper'] = hi
            initial = _lensde_configuration.finite_float(item.get('initial'), limit_lower)
            if _lensde_populations.is_bfl_anchor_variable(system, item, bfl_surface_index):
                bfl_anchor = _lensde_populations.bfl_anchor_seed_target(system, target_bfl, bfl_surface_index)
                if bfl_anchor is not None:
                    initial = bfl_anchor
            item['initial'] = _lensde_populations.clamp_value(initial, lo, hi)
            item['role'] = item.get('role') or f'first-order thickness @ S{index}'
        variables.append(item)
    return variables


def make_trace_gate_first_order_merit(project: dict, *, rms_upper: float, min_edge: float, edge_weight: float) -> dict:
    merit = _lensde_geometry.strip_first_order_field_constraints(_lensde_geometry.auto_merit_config(project))
    options = project.get('options', {})
    target_efl = _lensde_configuration.finite_float(merit.get('target_efl'), 0.0)
    target_ttl = _lensde_configuration.finite_float(merit.get('target_ttl'), 0.0)
    target_bfl = _lensde_configuration.finite_float(merit.get('target_bfl'), 0.0)
    working_f_number_upper = _lensde_configuration.finite_float(merit.get('working_f_number_upper'), 0.0)
    image_metric = _lensde_defaults.canonical_image_quality_metric(merit.get('image_quality_metric'), 'rms_spot')
    guard_ratio = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_single_ray_guard_ratio'), 10.0))
    operands = _lensde_defaults.image_quality_operands(image_metric, spot_weight=max(0.0, _lensde_configuration.finite_float(merit.get('unified_image_mean_spot_weight'), 3.0)), spot_tail_weight=max(0.0, _lensde_configuration.finite_float(merit.get('unified_image_single_ray_spot_tail_weight'), 0.012)), wavefront_weight=max(0.0, _lensde_configuration.finite_float(merit.get('unified_image_wavefront_weight'), 1.0)), guard_ratio=guard_ratio)
    if rms_upper > 0.0:
        if image_metric == 'rms_wavefront':
            merit['quality_wavefront_scale'] = max(1e-09, rms_upper)
        else:
            merit['quality_spot_scale'] = max(1e-09, rms_upper)
    if edge_weight > 0.0:
        operands.append({'name': 'min_edge', 'mode': 'greater_than', 'lower': min_edge, 'weight': edge_weight})
    if abs(target_efl) > state.TARGET_EFL_EPS:
        operands.append({'name': 'efl', 'mode': 'target', 'target': target_efl, 'weight': _lensde_configuration.first_order_weight_value(merit, 'efl', 1.0)})
    if target_ttl > 0.0:
        operands.append({'name': 'ttl', 'mode': 'less_than', 'upper': target_ttl, 'weight': _lensde_configuration.first_order_weight_value(merit, 'ttl', 0.4)})
    if target_bfl > 0.0:
        operands.append({'name': 'bfl', 'mode': 'greater_than', 'lower': target_bfl, 'weight': _lensde_configuration.first_order_weight_value(merit, 'bfl', 0.5)})
    if working_f_number_upper > 0.0:
        operands.append({'name': 'working_f_number', 'mode': 'less_than', 'upper': working_f_number_upper, 'weight': _lensde_configuration.first_order_weight_value(merit, 'working_f_number', 1.0)})
    return {**merit, 'auto_merit': False, 'merit_model': _lensde_defaults.sanitize_merit_model(options.get('initial_search_merit_model'), 'unified_trace_gate'), 'trace_policy': str(options.get('initial_search_trace_policy', merit.get('trace_policy', 'soft_coverage'))), 'min_field_coverage': _lensde_configuration.finite_float(options.get('initial_search_min_field_coverage'), 1.0), 'min_system_coverage': _lensde_configuration.finite_float(options.get('initial_search_min_system_coverage'), 1.0), 'min_coverage_for_image_merit': _lensde_configuration.finite_float(options.get('initial_search_min_coverage_for_image_merit'), 0.5), 'trace_failure_weight': _lensde_configuration.finite_float(options.get('initial_search_trace_failure_weight'), 1.0), 'coverage_failure_weight': _lensde_configuration.finite_float(options.get('initial_search_coverage_failure_weight'), 10.0), 'aim_failure_weight': _lensde_configuration.finite_float(options.get('initial_search_aim_failure_weight'), 1.0), 'backward_failure_weight': _lensde_configuration.finite_float(options.get('initial_search_backward_failure_weight'), 10.0), 'require_chief_ray': bool(options.get('initial_search_require_chief_ray', True)), 'require_off_axis_field_coverage': bool(options.get('initial_search_require_off_axis_field_coverage', True)), 'hard_trace_gate_weight': _lensde_configuration.finite_float(options.get('initial_search_hard_trace_gate_weight'), 1000.0), 'trace_progress_weight': _lensde_configuration.finite_float(options.get('initial_search_trace_progress_weight'), 0.0), 'chief_progress_weight': _lensde_configuration.finite_float(options.get('initial_search_chief_progress_weight'), 0.0), 'trace_priority_weight': _lensde_configuration.finite_float(options.get('initial_search_trace_priority_weight'), 0.0), 'weight_edge': 1.0, 'min_edge_thickness': min_edge, **_lensde_configuration.normalize_thickness_constraint_fields(options, prefix='initial_search_'), **_lensde_geometry.normalize_manufacturing_ratio_constraint_fields(options, prefix='initial_search_'), 'num_rays': max(1, int(options.get('initial_search_trace_rays', 3) or 3)), 'pupil_arms': max(1, int(options.get('initial_search_trace_arms', 6) or 6)), 'pupil_distribution': str(options.get('initial_search_trace_distribution', 'gaussian_ring_arm')), 'sampling_seed': int(merit.get('sampling_seed', 1) or 1), 'fail_merit': _lensde_configuration.finite_float(merit.get('fail_merit'), 1000000000000.0), 'operands': operands + _lensde_geometry.stop_air_edge_operands(merit), 'bounds': copy.deepcopy(merit.get('bounds', []))}


def scale_initial_search_operand_weights(merit: dict, *, first_order_weight_scale: float, spot_weight_scale: float) -> None:
    first_scale = max(0.0, _lensde_configuration.finite_float(first_order_weight_scale, 1.0))
    spot_scale = max(0.0, _lensde_configuration.finite_float(spot_weight_scale, 1.0))
    merit['group_weight_first_order'] = _lensde_configuration.finite_float(merit.get('group_weight_first_order'), 1.0) * first_scale
    merit['group_weight_image'] = _lensde_configuration.finite_float(merit.get('group_weight_image'), 1.0) * spot_scale
    for operand in merit.get('operands', []) or []:
        if not isinstance(operand, dict):
            continue
        name = str(operand.get('name', '')).strip().lower().replace('-', '_').replace(' ', '_')
        if name in {'efl', 'ttl', 'bfl'}:
            operand['weight'] = _lensde_configuration.finite_float(operand.get('weight'), 1.0) * first_scale
        elif name in {'rms_spot', 'spot', 'mean_spot', 'max_spot', 'rms_wavefront', 'wavefront', 'wavefront_rms', 'mean_wavefront', 'mean_rms_wavefront', 'opd', 'rms_opd', 'opd_rms'}:
            operand['weight'] = _lensde_configuration.finite_float(operand.get('weight'), 1.0) * spot_scale


def make_flat_search_system(system: dict) -> dict:
    flat = copy.deepcopy(system)
    for surface in flat.get('surfaces', []) or []:
        kind = str(surface.get('kind', '')).lower()
        if kind in {'object', 'image'}:
            continue
        coeffs = surface.get('asphere_coefficients')
        preserve_asphere = kind == 'even_asphere' or (isinstance(coeffs, list) and len(coeffs) > 0)
        if surface.get('is_stop'):
            if _lensde_populations.is_physical_stop_surface(surface) and kind == 'plane':
                surface['kind'] = 'even_asphere' if preserve_asphere else 'standard'
                if preserve_asphere and (not isinstance(coeffs, list)):
                    surface['asphere_coefficients'] = []
            continue
        surface['kind'] = 'even_asphere' if preserve_asphere else 'standard'
        if preserve_asphere and (not isinstance(coeffs, list)):
            surface['asphere_coefficients'] = []
        surface['radius'] = None
    return flat


def glass_pairs(system: dict) -> list[tuple[int, int]]:
    surfaces = system.get('surfaces', []) or []
    pairs: list[tuple[int, int]] = []
    start: int | None = None
    for index, surface in enumerate(surfaces):
        if str(surface.get('kind', '')).lower() in {'object', 'image'}:
            continue
        is_glass = _lensde_populations.surface_after_is_glass(system, index)
        if start is None and is_glass:
            start = index
            continue
        if start is not None and (not is_glass):
            pairs.append((start, index))
            start = None
    return pairs


def glass_hammer_sign(value: float, eps: float=1e-10) -> str:
    if value > eps:
        return 'P'
    if value < -eps:
        return 'N'
    return 'Z'


def glass_hammer_thick_lens_power(c1: float, c2: float, t: float, nd: float) -> float:
    n_minus_1 = nd - 1.0
    if abs(nd) < 1e-12:
        return float('nan')
    return n_minus_1 * (c1 - c2 + n_minus_1 / nd * t * c1 * c2)


def glass_hammer_solve_c2_for_power(c1: float, t: float, nd: float, phi: float) -> float | None:
    n_minus_1 = nd - 1.0
    if abs(n_minus_1) < 1e-12 or abs(nd) < 1e-12:
        return None
    k = phi / n_minus_1
    a = n_minus_1 / nd * t
    denom = a * c1 - 1.0
    if abs(denom) < 1e-12:
        return None
    return (k - c1) / denom


def glass_hammer_solve_c1_for_power(c2: float, t: float, nd: float, phi: float) -> float | None:
    n_minus_1 = nd - 1.0
    if abs(n_minus_1) < 1e-12 or abs(nd) < 1e-12:
        return None
    k = phi / n_minus_1
    a = n_minus_1 / nd * t
    denom = 1.0 + a * c2
    if abs(denom) < 1e-12:
        return None
    return (k + c2) / denom


def glass_hammer_signed_log_sample(target_sign: str, reference: float, lo: float, hi: float, rng: random.Random, *, min_abs: float=0.0008, max_abs_floor: float=0.006) -> float:
    if target_sign == 'P' and hi <= 1e-08:
        return float('nan')
    if target_sign == 'N' and lo >= -1e-08:
        return float('nan')
    ref = max(abs(reference), min_abs)
    low_mag = max(ref * 0.25, min_abs)
    high_mag = max(ref * 4.5, max_abs_floor)
    value = math.exp(rng.uniform(math.log(low_mag), math.log(high_mag)))
    if target_sign == 'N':
        value = -value
    if target_sign == 'P':
        lo = max(lo, 1e-08)
    elif target_sign == 'N':
        hi = min(hi, -1e-08)
    return min(max(value, lo), hi)


def glass_hammer_lens_groups(system: dict, variables: list[dict]) -> list[dict]:
    lookup: dict[tuple[str, int], int] = {}
    for col, variable in enumerate(variables):
        kind = str(variable.get('kind', '')).lower()
        if kind not in {'curvature', 'thickness'}:
            continue
        try:
            surface_index = int(variable.get('index', -1))
            term = int(variable.get('term', 0) or 0)
        except (TypeError, ValueError):
            continue
        if term != 0:
            continue
        lookup.setdefault((kind, surface_index), col)
    lenses: list[dict] = []
    surfaces = system.get('surfaces', []) or []
    for front in range(1, max(1, len(surfaces) - 1)):
        if not _lensde_populations.surface_after_is_glass(system, front):
            continue
        rear = front + 1
        c1_col = lookup.get(('curvature', front))
        c2_col = lookup.get(('curvature', rear))
        glass_t_col = lookup.get(('thickness', front))
        if c1_col is None or c2_col is None or glass_t_col is None:
            continue
        material = surfaces[front].get('material_after', {}) if isinstance(surfaces[front], dict) else {}
        nd, vd = _lensde_provenance.material_nd_vd(material)
        if nd is None or vd is None:
            continue
        lenses.append({'lens_no': len(lenses) + 1, 'front_surface': front, 'rear_surface': rear, 'c1_col': c1_col, 'c2_col': c2_col, 'glass_t_col': glass_t_col, 'nd': nd, 'vd': vd, 'glass_name': str(material.get('name') or material.get('reference') or 'glass'), 'material': material})
    return lenses


def glass_hammer_repair_lens_power(values: list[float], variables: list[dict], lens: dict, target_pair: str, target_nd: float, rng: random.Random) -> list[float] | None:
    if len(target_pair) != 2 or 'Z' in target_pair:
        return None
    row = list(values)
    c1_col = int(lens['c1_col'])
    c2_col = int(lens['c2_col'])
    t_col = int(lens['glass_t_col'])
    c1_lo, c1_hi = _lensde_populations.variable_bounds(variables[c1_col])
    c2_lo, c2_hi = _lensde_populations.variable_bounds(variables[c2_col])
    t_lo, t_hi = _lensde_populations.variable_bounds(variables[t_col])
    old_phi = glass_hammer_thick_lens_power(row[c1_col], row[c2_col], row[t_col], _lensde_configuration.finite_float(lens.get('nd'), 1.5))
    if not math.isfinite(old_phi):
        return None
    original = list(row)
    for _ in range(160):
        row[t_col] = min(max(original[t_col] * math.exp(rng.gauss(0.0, 0.1)), t_lo), t_hi)
        if rng.random() < 0.5:
            c1 = glass_hammer_signed_log_sample(target_pair[0], original[c1_col], c1_lo, c1_hi, rng)
            if not math.isfinite(c1):
                continue
            c2 = glass_hammer_solve_c2_for_power(c1, row[t_col], target_nd, old_phi)
            if c2 is None:
                continue
        else:
            c2 = glass_hammer_signed_log_sample(target_pair[1], original[c2_col], c2_lo, c2_hi, rng)
            if not math.isfinite(c2):
                continue
            c1 = glass_hammer_solve_c1_for_power(c2, row[t_col], target_nd, old_phi)
            if c1 is None:
                continue
        if not (c1_lo <= c1 <= c1_hi and c2_lo <= c2 <= c2_hi):
            continue
        if glass_hammer_sign(c1) + glass_hammer_sign(c2) != target_pair:
            continue
        row[c1_col] = c1
        row[c2_col] = c2
        return row
    return None


def glass_hammer_replace_lens_glass(system: dict, lens: dict, glass: dict) -> dict:
    out = copy.deepcopy(system)
    surfaces = out.get('surfaces', []) or []
    front = int(lens.get('front_surface', -1))
    if 0 <= front < len(surfaces):
        material = {'mode': 'glass', 'name': glass.get('name', ''), 'reference': glass.get('reference', glass.get('library', '')), 'nd': _lensde_configuration.finite_float(glass.get('nd'), _lensde_configuration.finite_float(lens.get('nd'), 1.5)), 'vd': _lensde_configuration.finite_float(glass.get('vd'), _lensde_configuration.finite_float(lens.get('vd'), 50.0))}
        if glass.get('filename'):
            material['filename'] = glass.get('filename')
        if glass.get('source_path'):
            material['source_path'] = glass.get('source_path')
        surfaces[front]['material_after'] = material
    return out


def glass_hammer_values_from_system(system: dict, variables: list[dict]) -> list[float]:
    return [_lensde_populations.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in variables]


def glass_hammer_variables_doc(variables: list[dict], values: list[float]) -> dict:
    out = {'variables': [copy.deepcopy(variable) for variable in variables]}
    for variable, value in zip(out['variables'], values, strict=False):
        variable['initial'] = _lensde_configuration.finite_float(value, _lensde_configuration.finite_float(variable.get('initial'), 0.0))
    return out


def glass_hammer_topology_key(system: dict) -> str:
    try:
        return str(optical_topology_features(system).get('coarse_key') or '')
    except Exception:
        return ''


def glass_hammer_glass_names(system: dict) -> list[str]:
    names: list[str] = []
    for surface in system.get('surfaces', []) or []:
        material = surface.get('material_after', {}) if isinstance(surface, dict) else {}
        if str(material.get('mode', '')).lower() == 'glass':
            names.append(str(material.get('name') or material.get('reference') or 'glass'))
    return names


def topology_quantile(values: list[float], q: float, default: float) -> float:
    finite = sorted((abs(_lensde_configuration.finite_float(value, float('nan'))) for value in values if math.isfinite(_lensde_configuration.finite_float(value, float('nan')))))
    if not finite:
        return default
    pos = max(0.0, min(1.0, _lensde_configuration.finite_float(q, 0.5))) * (len(finite) - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return finite[lo]
    t = pos - lo
    return finite[lo] * (1.0 - t) + finite[hi] * t


def topology_sign_bucket(value: float, eps: float) -> str:
    if value > eps:
        return 'p'
    if value < -eps:
        return 'n'
    return 'z'


def topology_ordinal_bucket(value: float, edges: list[float]) -> str:
    number = _lensde_configuration.finite_float(value, 0.0)
    for index, edge in enumerate(edges):
        if number < edge:
            return str(index)
    return str(len(edges))


def optical_topology_material_class(material: dict) -> str:
    nd, vd = _lensde_provenance.material_nd_vd(material)
    nd = _lensde_configuration.finite_float(nd, 1.5)
    vd = _lensde_configuration.finite_float(vd, 50.0)
    nd_bucket = 'u'
    if nd >= 1.8:
        nd_bucket = 'x'
    elif nd >= 1.7:
        nd_bucket = 'h'
    elif nd >= 1.6:
        nd_bucket = 'm'
    elif nd >= 1.48:
        nd_bucket = 'l'
    vd_bucket = 'u'
    if vd >= 70.0:
        vd_bucket = 'c'
    elif vd >= 55.0:
        vd_bucket = 'm'
    elif vd >= 40.0:
        vd_bucket = 'd'
    elif vd > 0.0:
        vd_bucket = 'f'
    return nd_bucket + vd_bucket


def optical_lens_shape_bucket(front_curvature: float, rear_curvature: float, curvature_eps: float) -> str:
    front = topology_sign_bucket(front_curvature, curvature_eps)
    rear = topology_sign_bucket(rear_curvature, curvature_eps)
    if front == 'z' or rear == 'z':
        return 'pl'
    if front == 'p' and rear == 'p':
        return 'mo'
    if front == 'p' and rear == 'n':
        return 'bv'
    if front == 'n' and rear == 'p':
        return 'bc'
    if front == 'n' and rear == 'n':
        return 'mi'
    return 'x'


def optical_topology_features(system: dict) -> dict:
    surfaces = system.get('surfaces', []) or []
    pairs = glass_pairs(system)
    curvatures = [abs(_lensde_defaults.curvature_from_radius((surfaces[index] or {}).get('radius'))) for pair in pairs for index in pair if 0 <= index < len(surfaces)]
    c_med = topology_quantile(curvatures, 0.5, 0.001)
    c_eps = max(1e-05, 0.03 * c_med)
    raw_phi: list[float] = []
    for front, rear in pairs:
        if not (0 <= front < len(surfaces) and 0 <= rear < len(surfaces)):
            continue
        material = surfaces[front].get('material_after', {}) if isinstance(surfaces[front], dict) else {}
        nd, _vd = _lensde_provenance.material_nd_vd(material)
        nd = _lensde_configuration.finite_float(nd, 1.5)
        cf = _lensde_defaults.curvature_from_radius(surfaces[front].get('radius'))
        cr = _lensde_defaults.curvature_from_radius(surfaces[rear].get('radius'))
        raw_phi.append((nd - 1.0) * (cf - cr))
    phi_med = topology_quantile(raw_phi, 0.5, 0.001)
    phi_eps = max(1e-05, 0.08 * phi_med)
    power_signs: list[str] = []
    surface_shapes: list[str] = []
    bend_bins: list[str] = []
    power_mag_bins: list[str] = []
    material_bins: list[str] = []
    for (front, rear), phi in zip(pairs, raw_phi, strict=False):
        cf = _lensde_defaults.curvature_from_radius(surfaces[front].get('radius'))
        cr = _lensde_defaults.curvature_from_radius(surfaces[rear].get('radius'))
        surface_shapes.append(optical_lens_shape_bucket(cf, cr, c_eps))
        bending = (cf + cr) / (abs(cf - cr) + 1e-09)
        bend_bins.append(topology_ordinal_bucket(bending, [-2.0, -0.75, -0.25, 0.25, 0.75, 2.0]))
        rel_power = abs(phi) / (phi_med + 1e-12)
        power_mag = topology_ordinal_bucket(rel_power, [0.35, 0.75, 1.5, 3.0])
        power_signs.append('W' if power_mag in {'0', '1'} else topology_sign_bucket(phi, phi_eps).upper())
        power_mag_bins.append(power_mag)
        material = surfaces[front].get('material_after', {}) if isinstance(surfaces[front], dict) else {}
        material_bins.append(optical_topology_material_class(material))
    gaps: list[float] = []
    for (_prev_front, rear), (front, _next_rear) in zip(pairs, pairs[1:], strict=False):
        lo = max(0, min(rear, front))
        hi = min(len(surfaces) - 1, max(rear, front))
        gaps.append(sum((max(0.0, _lensde_configuration.finite_float(surfaces[index].get('thickness'), 0.0)) for index in range(lo, hi))))
    g_med = topology_quantile(gaps, 0.5, 1.0)
    gap_bins = [topology_ordinal_bucket(gap / (g_med + 1e-12), [0.5, 1.5]) for gap in gaps]
    coarse = '|'.join(['pwr=' + ''.join(power_signs), 'shape=' + '.'.join(surface_shapes), 'bend=' + ''.join(bend_bins), 'gap=' + ''.join(gap_bins)])
    fine = coarse + '|pmag=' + ''.join(power_mag_bins) + '|mat=' + '.'.join(material_bins)
    return {'element_pairs': pairs, 'power_signs': ''.join(power_signs), 'surface_shapes': '.'.join(surface_shapes), 'bend_bins': ''.join(bend_bins), 'gap_bins': ''.join(gap_bins), 'power_mag_bins': ''.join(power_mag_bins), 'material_bins': '.'.join(material_bins), 'coarse_key': coarse, 'fine_key': fine, 'raw_phi': raw_phi}


def glass_candidate_with_source_path(candidate: dict) -> dict:
    out = dict(candidate or {})
    filename = str(out.get('filename') or out.get('source_path') or '').strip()
    if filename:
        path = Path(filename)
        out['source_path'] = str(path if path.is_absolute() else state.DATABASE_ROOT / 'data-nk' / filename)
        specs = _lensde_provenance.glass_specs(out['source_path'])
        if not math.isfinite(_lensde_configuration.finite_float(out.get('min_wavelength'), math.nan)):
            out['min_wavelength'] = specs.get('min_wavelength')
        if not math.isfinite(_lensde_configuration.finite_float(out.get('max_wavelength'), math.nan)):
            out['max_wavelength'] = specs.get('max_wavelength')
    out.setdefault('reference', out.get('library', ''))
    return out
