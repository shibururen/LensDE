"""Extracted paper engine: apertures."""
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
from . import provenance as _lensde_provenance
from . import tracing as _lensde_tracing

def protective_glass_surface_indices(system: dict, variables: list[dict] | None=None, *, legacy_geometry: bool=False) -> list[int]:
    """Recognize the terminal plane-parallel glass plate, independent of numbering.

    Explicit per-surface exclusions also survive native serialization. Never
    infer a cover plate from two arbitrary surface numbers in another lens.
    """
    surfaces = system.get('surfaces', []) or []
    excluded = {i for i, s in enumerate(surfaces) if s.get('vignetting_aperture_excluded') and (not s.get('is_stop')) and (s.get('kind') not in {'object', 'image'})}
    if len(surfaces) < 4 or surfaces[-1].get('kind') != 'image':
        return sorted(excluded)
    a, b = (len(surfaces) - 3, len(surfaces) - 2)
    if not legacy_geometry:
        if variables is None:
            return sorted(excluded)
        geometry_kinds = {'curvature', 'radius', 'thickness', 'conic', 'asphere_coeff'}
        for variable in variables:
            if variable.get('enabled', True) is False:
                continue
            if variable.get('kind') in geometry_kinds:
                try:
                    variable_index = int(variable.get('index', -1))
                except (TypeError, ValueError):
                    return sorted(excluded)
                if variable_index < 0 or variable_index >= len(surfaces) or variable_index in {a, b}:
                    return sorted(excluded)

    def plane(s):
        return s.get('kind') in {'standard', 'plane'} and (not s.get('is_stop')) and (not s.get('reflective')) and (s.get('radius') is None or abs(_lensde_configuration.finite_float(s.get('radius'), 0.0)) < 1e-12) and (not any(s.get('asphere_coefficients') or []))

    def air(s):
        material = s.get('material_after') or {}
        return material.get('mode') == 'air' or material.get('name', '').lower() == 'air'
    material = surfaces[a].get('material_after') or {}
    if plane(surfaces[a]) and plane(surfaces[b]) and air(surfaces[a - 1]) and air(surfaces[b]) and (not air(surfaces[a])) and (_lensde_configuration.finite_float(material.get('nd'), 0.0) > 1.0) and (_lensde_configuration.finite_float(surfaces[a].get('thickness'), 0.0) > 0.0):
        excluded.update((a, b))
    return sorted(excluded)


def vignetting_optimizer_aperture_copy(system: dict, variables: list[dict] | None=None, *, legacy_geometry: bool=False) -> dict:
    """Keep source aperture semantics except for the non-limiting cover glass."""
    working = copy.deepcopy(system)
    for index in protective_glass_surface_indices(working, variables, legacy_geometry=legacy_geometry):
        working['surfaces'][index].update(clear_aperture_fixed=False, aperture_radius=0.0, aperture=None, vignetting_aperture_excluded=True)
    return working


def default_vignetting_aperture_surface_indices(system: dict) -> list[int]:
    """Return the default full-aperture Set Vignetting surface set.

    The terminal protective-glass pair stays automatic; every other optical surface,
    including the stop, belongs to the default fixed-aperture contract.
    """
    return [index for index, surface in enumerate(system.get('surfaces', []) or []) if str(surface.get('kind') or '').strip().lower() not in {'object', 'image'} and index not in protective_glass_surface_indices(system)]


def zemax_set_vignetting_hard_aperture_copy(system: dict, *, support_surface_indices: list[int] | None=None) -> tuple[dict, int]:
    """Freeze the currently displayed semi-diameters for a Set Vignetting solve.

    Zemax Set Vignetting treats the exported DIAM/FLAP values as ray
    boundaries.  Optiland normally keeps traced semi-apertures automatic and
    therefore does not clip ordinary merit rays with them.  Freeze them only
    in this solver-local copy so the measured pupil matches the ZMX contract
    without mutating the user's project. The default freezes every optical
    surface except the protective-glass pair. An explicit
    ``support_surface_indices`` argument remains available for isolated
    support-union experiments.
    """
    working = vignetting_optimizer_aperture_copy(system)
    selected = set(default_vignetting_aperture_surface_indices(system)) if support_surface_indices is None else set(support_surface_indices)
    surfaces = working.get('surfaces', []) or []
    if selected is not None and any((type(index) is not int or not 0 <= index < len(surfaces) or str(surfaces[index].get('kind') or '').strip().lower() in {'object', 'image'} for index in selected)):
        raise ValueError('invalid original support surface index')
    selected.update((i for i, surface in enumerate(surfaces) if surface.get('is_stop')))
    selected.difference_update(protective_glass_surface_indices(system))
    fixed_count = 0
    for index, surface in enumerate(surfaces):
        kind = str(surface.get('kind') or '').strip().lower()
        if kind in {'object', 'image'}:
            continue
        if index not in selected:
            surface.update(clear_aperture_fixed=False, aperture_radius=0.0, aperture=None)
            continue
        radius = _lensde_provenance.surface_clear_radius(surface)
        if radius <= 0.0:
            raise ValueError(f'fixed-aperture surface {index} has no positive clear radius')
        surface['semi_aperture'] = radius
        surface['aperture_radius'] = radius
        surface['clear_aperture_fixed'] = True
        surface['aperture'] = {'type': 'RadialAperture', 'r_max': radius, 'r_min': 0.0}
        fixed_count += 1
    return (working, fixed_count)


def materialize_global_trim_owner_apertures(system: dict, score: dict | None, merit: dict | None) -> tuple[dict, dict]:
    """Materialize optimizer-certified global-trim circular apertures.

    ``term`` encodes ``2 * surface + side`` and the corrected score contribution
    carries the actual dynamic circular radius in the physical cut-match
    branch. It is not a separate center-envelope measurement. Stage-only
    illumination trimming obtains that envelope by tracing the source pupil.
    A tiny numerical boundary allowance is added here, without clearance.
    """
    source = copy.deepcopy(system)
    support_contract = str((merit or {}).get('vignetting_boundary_support_contract') or 'global_trim_realizability').strip().lower().replace('-', '_')
    candidate_contract = _lensde_commands.option_truthy((merit or {}).get('vignetting_physical_cut_match_enabled', (merit or {}).get('vignetting_global_trim_cut_match_experimental')), False)
    report = {'contract': 'adaptive_global_trim_candidate_materialization_v2' if candidate_contract else 'adaptive_global_trim_owner_materialization_v1', 'performed': False, 'support_contract': support_contract, 'effective_clear_aperture_margin': 1.0, 'selected_surface_count': 0, 'selected_surfaces': []}
    if support_contract not in {'global_trim', 'global_trim_realizability'}:
        report['reason'] = 'support_contract_is_not_global_trim_realizability'
        return (source, report)
    fields = list(source.get('fields') or [])
    surfaces = list(source.get('surfaces') or [])
    excluded_surfaces = set(protective_glass_surface_indices(source))
    grouped: dict[int, list[dict]] = {}
    for contribution in (score or {}).get('contributions', []) or []:
        if not isinstance(contribution, dict):
            continue
        if contribution.get('name') != 'vignetting_boundary_aperture_support':
            continue
        field_index = int(_lensde_configuration.finite_float(contribution.get('index'), -1))
        term = int(_lensde_configuration.finite_float(contribution.get('term'), -1))
        surface_index = term // 2
        side_index = term % 2
        if surface_index in excluded_surfaces:
            continue
        if field_index < 0 or field_index >= len(fields) or term < 0 or (surface_index < 0) or (surface_index >= len(surfaces)):
            raise RuntimeError(f'invalid global-trim owner contribution: field={field_index}, term={term}')
        field = fields[field_index]
        fvcy = _lensde_configuration.finite_float(field.get('fvcy'), 0.0)
        fvdy = max(0.0, _lensde_configuration.finite_float(field.get('fvdy'), 0.0))
        cuts = (max(0.0, fvdy + fvcy), max(0.0, fvdy - fvcy))
        candidate_radius = _lensde_configuration.finite_float(contribution.get('aperture_candidate_radius'), math.nan)
        pretrim_radius = _lensde_configuration.finite_float(contribution.get('aperture_radius_before_trim'), math.nan)
        protection_radius = _lensde_configuration.finite_float(contribution.get('aperture_protection_radius'), math.nan)
        trim_window = _lensde_configuration.finite_float(contribution.get('aperture_trim_window'), math.nan)
        if candidate_contract and (not math.isfinite(candidate_radius) or candidate_radius <= 0.0):
            raise RuntimeError(f'global-trim contribution is missing its certified candidate radius: field={field_index}, term={term}')
        grouped.setdefault(surface_index, []).append({'field_index': field_index, 'side': 'negative_y' if side_index == 0 else 'positive_y', 'cut': cuts[side_index], 'contract_deficit': max(0.0, _lensde_configuration.finite_float(contribution.get('value'), 0.0)), 'contract_penalty': max(0.0, _lensde_configuration.finite_float(contribution.get('penalty'), 0.0)), 'candidate_aperture_radius': candidate_radius, 'pretrim_aperture_radius': pretrim_radius, 'protection_aperture_radius': protection_radius, 'available_trim_window': trim_window})
    active_threshold = max(0.0, _lensde_configuration.finite_float((merit or {}).get('vignetting_boundary_support_activation'), 0.02))
    field_radii = [math.hypot(_lensde_configuration.finite_float(field.get('hx'), 0.0), _lensde_configuration.finite_float(field.get('hy'), 0.0)) for field in fields]
    maximum_field_radius = max(field_radii, default=0.0)
    outer_tolerance = 1e-12 * max(1.0, maximum_field_radius)
    active_side_count = sum((1 for field_index, field in enumerate(fields) if not candidate_contract or abs(field_radii[field_index] - maximum_field_radius) <= outer_tolerance for cut in (max(0.0, _lensde_configuration.finite_float(field.get('fvdy'), 0.0) + _lensde_configuration.finite_float(field.get('fvcy'), 0.0)), max(0.0, _lensde_configuration.finite_float(field.get('fvdy'), 0.0) - _lensde_configuration.finite_float(field.get('fvcy'), 0.0))) if cut > active_threshold))
    owner_count = sum((len(owners) for owners in grouped.values()))
    if active_side_count > 0 and owner_count < active_side_count:
        raise RuntimeError(f'global-trim score does not certify every active vignetting side: active={active_side_count}, owners={owner_count}')
    applied: list[dict] = []
    for surface_index, owners in sorted(grouped.items()):
        surface = surfaces[surface_index]
        kind = str(surface.get('kind') or '').strip().lower()
        if kind in {'object', 'image'} or _lensde_commands.option_truthy(surface.get('is_stop'), False):
            raise RuntimeError(f'global-trim selected ineligible surface {surface_index}')
        original_radius = _lensde_configuration.finite_float(surface.get('semi_aperture'), math.nan)
        if not math.isfinite(original_radius) or original_radius <= 0.0:
            raise RuntimeError(f'global-trim selected invalid aperture at surface {surface_index}')
        certified_radii = [_lensde_configuration.finite_float(owner.get('candidate_aperture_radius'), math.nan) for owner in owners]
        if candidate_contract:
            if any((not math.isfinite(radius) or radius <= 0.0 for radius in certified_radii)):
                raise RuntimeError(f'global-trim surface {surface_index} has an invalid candidate radius')
            candidate_radius = max(certified_radii)
            compatibility_tolerance = 1e-09 * max(1.0, candidate_radius)
            if candidate_radius - min(certified_radii) > compatibility_tolerance:
                raise RuntimeError(f'one circular aperture cannot realize incompatible global-trim owners at surface {surface_index}: radii={certified_radii}')
            if candidate_radius > original_radius + compatibility_tolerance:
                raise RuntimeError(f'global-trim candidate would enlarge rather than trim surface {surface_index}: candidate={candidate_radius}, current={original_radius}')
            numerical_allowance = 1e-12 + 1e-10 * candidate_radius
            radius = candidate_radius + numerical_allowance
        else:
            candidate_radius = original_radius
            numerical_allowance = 0.0
            radius = original_radius
        already_fixed = _lensde_commands.option_truthy(surface.get('clear_aperture_fixed'), False)
        surface['semi_aperture'] = radius
        surface['aperture_radius'] = radius
        surface['clear_aperture_fixed'] = True
        surface['aperture'] = {'type': 'RadialAperture', 'r_max': radius, 'r_min': 0.0}
        applied.append({'surface_index': surface_index, 'aperture_radius': radius, 'original_aperture_radius': original_radius, 'candidate_aperture_radius': candidate_radius, 'numerical_boundary_allowance': numerical_allowance, 'already_fixed': already_fixed, 'owner_count': len(owners), 'owners': owners})
    report.update({'performed': bool(applied), 'reason': 'owners_materialized' if applied else 'no_active_owner_surfaces', 'active_side_count': active_side_count, 'owner_count': owner_count, 'selected_surface_count': len(applied), 'selected_surfaces': applied, 'geometry_changed': False, 'aperture_geometry_changed': any((abs(item['aperture_radius'] - item['original_aperture_radius']) > 1e-12 * max(1.0, item['original_aperture_radius']) for item in applied)), 'semi_apertures_scaled': candidate_contract and bool(applied), 'protected_fields': 'center_and_outer', 'protected_rays': 'optimization_envelope_all_wavelengths'})
    return (source, report)


def certified_global_trim_owner_apertures(system: dict, delivery_certificate: dict | None) -> tuple[dict, dict] | None:
    """Reuse a delivery certificate without re-reading optimizer-only terms."""
    if not isinstance(delivery_certificate, dict) or not _lensde_commands.option_truthy(delivery_certificate.get('passed'), False):
        return None
    if delivery_certificate.get('certified_system_sha256') != _lensde_provenance.json_fingerprint(system):
        return None
    adaptive_trim = delivery_certificate.get('adaptive_trim')
    if not isinstance(adaptive_trim, dict) or adaptive_trim.get('contract') not in {'adaptive_global_trim_owner_materialization_v1', 'adaptive_global_trim_candidate_materialization_v2'}:
        return None
    selected = adaptive_trim.get('selected_surfaces')
    if not isinstance(selected, list):
        return None
    surfaces = list(system.get('surfaces') or [])
    for item in selected:
        if not isinstance(item, dict):
            return None
        surface_index = int(_lensde_configuration.finite_float(item.get('surface_index'), -1))
        certified_radius = _lensde_configuration.finite_float(item.get('aperture_radius'), math.nan)
        if surface_index < 0 or surface_index >= len(surfaces) or (not math.isfinite(certified_radius)) or (certified_radius <= 0.0):
            return None
        surface = surfaces[surface_index]
        aperture = surface.get('aperture')
        tolerance = 1e-12 * max(1.0, certified_radius)
        if not _lensde_commands.option_truthy(surface.get('clear_aperture_fixed'), False) or abs(_lensde_configuration.finite_float(surface.get('semi_aperture'), math.nan) - certified_radius) > tolerance or abs(_lensde_configuration.finite_float(surface.get('aperture_radius'), math.nan) - certified_radius) > tolerance or (not isinstance(aperture, dict)) or (str(aperture.get('type') or '').strip().lower() != 'radialaperture') or (abs(_lensde_configuration.finite_float(aperture.get('r_max'), math.nan) - certified_radius) > tolerance):
            return None
    report = copy.deepcopy(adaptive_trim)
    report.update({'cache_hit': True, 'cache_scope': 'upstream_delivery_certificate'})
    return (copy.deepcopy(system), report)


def exact_apg_portable_contract(system: dict, executable: Path, *, asphere_root_policy: str='unified_certified', support_surface_indices: list[int] | None=None) -> dict:
    frozen, fixed_count = zemax_set_vignetting_hard_aperture_copy(system, support_surface_indices=support_surface_indices)
    aperture_signature = [{'surface_index': index, 'radius': _lensde_configuration.finite_float(surface.get('aperture_radius'), 0.0)} for index, surface in enumerate(frozen.get('surfaces', []) or []) if str(surface.get('kind') or '').strip().lower() not in {'object', 'image'} and _lensde_configuration.finite_float(surface.get('aperture_radius'), 0.0) > 0.0]
    primary_wavelength = _lensde_fields.primary_wavelength_um_for_system(system)
    contract = {'contract': 'exact_apg_full_aperture_excluding_cover_v3', 'executable_sha256': _lensde_provenance.sha256_file(executable), 'measurement_aiming': 'adaptive_pilot_grid_strict', 'asphere_root_policy': _lensde_asphere_contracts.post_asphere_root_scoring_policy({'initial_search_post_asphere_root_scoring_policy': asphere_root_policy}), 'measurement_wavelength': 'primary', 'primary_wavelength_um': primary_wavelength, 'measurement_apertures': 'all_displayed_apertures_except_protective_glass', 'excluded_surface_indices': protective_glass_surface_indices(system), 'fixed_aperture_count': fixed_count, 'aperture_signature': aperture_signature, 'aiming_iterations': 40, 'continuation_steps': 16, 'coarse_samples': 33, 'centroid_grid': 98, 'boundary_iterations': 44, 'batch_apg_contract': 'current_field_chief_pilot_v1', 'all_wavelengths': False, 'allow_aiming_fallback': False, 'delivered_system_update': 'field_vignetting_and_protective_glass_exclusion', 'post_measurement_projection_or_contraction': False, 'post_measurement_clear_aperture_update': False}
    if support_surface_indices is not None:
        contract.update(measurement_apertures='original_support_union_plus_stop', original_support_surface_indices=sorted(set(support_surface_indices)), support_radius_policy='current_displayed_radii')
    return contract


def exact_apg_certificate_payload(value: dict | None) -> dict | None:
    if not isinstance(value, dict):
        return None
    nested = value.get('exact_apg')
    return nested if isinstance(nested, dict) else value


def exact_apg_set_vignetting_output_dir(output_dir: Path) -> Path:
    """Keep exact-measurement descendants below the Windows path limit."""
    preferred = output_dir.resolve()
    if len(str(preferred / 'measurement_system.json')) <= 240:
        return preferred
    digest = hashlib.sha1(str(preferred).encode('utf-8', errors='surrogatepass')).hexdigest()[:12]
    run_root = next((ancestor for ancestor in (preferred, *preferred.parents) if ancestor.name.startswith('run_') or (ancestor / 'queue_status.json').is_file()), None)
    if run_root is None:
        raise OSError(f'Vignetting measurement path is too long outside an experiment root: {output_dir}. Shorten its stage names; temporary output relocation is disabled.')
    compact_root = run_root / '_svm'
    return compact_root / digest


def exact_apg_set_vignetting_system(system: dict, output_dir: Path, *, executable: Path | None=None, timeout_seconds: int=0, child_cpu_workers: int=0, upstream_certificate: dict | None=None, require_full_boundary_trace: bool=True, asphere_root_policy: str='unified_certified', support_surface_indices: list[int] | None=None) -> dict:
    """Measure against full displayed apertures except protective glass.

    Freeze only the calibration copy. Deliver measured fields with source
    optimizer aperture semantics, clearing protective-glass limits for CP.
    """
    if not isinstance(system, dict) or not system:
        raise ValueError('Set Vignetting requires a non-empty system')
    root_policy = _lensde_asphere_contracts.post_asphere_root_scoring_policy({'initial_search_post_asphere_root_scoring_policy': asphere_root_policy})
    system = copy.deepcopy(system)
    system['asphere_root_policy'] = root_policy
    requested_output_dir = output_dir.resolve()
    output_dir = exact_apg_set_vignetting_output_dir(requested_output_dir)
    solve_dir = native_io_paths.named_child(output_dir, 'set_vignetting')
    output_dir.mkdir(parents=True, exist_ok=True)
    cpu_executable = (executable or _lensde_provenance.exe_path(False)).resolve()
    if not cpu_executable.is_file():
        raise FileNotFoundError(cpu_executable)
    measurement_system, materialized_aperture_count = zemax_set_vignetting_hard_aperture_copy(system, support_surface_indices=support_surface_indices)
    measurement_aiming = measurement_system.get('ray_aiming')
    if not isinstance(measurement_aiming, dict):
        measurement_aiming = {}
    else:
        measurement_aiming = copy.deepcopy(measurement_aiming)
    measurement_aiming['mode'] = 'adaptive_pilot_grid'
    adaptive = measurement_aiming.get('adaptive_pilot_grid')
    if not isinstance(adaptive, dict):
        adaptive = {}
    adaptive.update({'enabled': True, 'strict': True, 'require_convergence': True, 'fallback': 'none', 'max_iterations': max(40, int(_lensde_configuration.finite_float(adaptive.get('max_iterations'), 40))), 'continuation_steps': max(16, int(_lensde_configuration.finite_float(adaptive.get('continuation_steps'), 16)))})
    measurement_aiming['adaptive_pilot_grid'] = adaptive
    measurement_system['ray_aiming'] = measurement_aiming
    measurement_path = output_dir / 'measurement_system.json'
    _lensde_provenance.write_json(measurement_path, measurement_system)
    batch_apg_workers = max(1, min(8, child_cpu_workers if child_cpu_workers > 0 else os.cpu_count() or 1))
    command = [str(cpu_executable), '--system', str(measurement_path), '--asphere-root-policy', root_policy, '--aiming', 'adaptive_pilot_grid', '--aiming-iter', '40', '--aiming-continuation-steps', '16', '--set-vignetting', '--set-vignetting-exact-measurement', '--set-vignetting-no-aiming-fallback', '--set-vignetting-batch-apg-workers', str(batch_apg_workers), '--set-vignetting-coarse-samples', '33', '--set-vignetting-centroid-grid', '98', '--set-vignetting-boundary-iter', '44', '--score-only', '--aiming-diagnostics', '--out-dir', str(solve_dir)]
    source_hash = _lensde_provenance.json_fingerprint(system)
    if not source_hash:
        raise ValueError('Set Vignetting system cannot be fingerprinted')
    portable_contract = exact_apg_portable_contract(system, cpu_executable, asphere_root_policy=root_policy, support_surface_indices=support_surface_indices)
    portable_contract_hash = _lensde_provenance.json_fingerprint(portable_contract)
    contract = {'contract': 'exact_apg_full_aperture_excluding_cover_v3', 'source_system_sha256': source_hash, 'require_full_boundary_trace': require_full_boundary_trace, 'portable_contract': portable_contract, 'portable_contract_sha256': portable_contract_hash, 'delivered_apertures': 'source_semantics_except_protective_glass', 'delivered_aiming': 'preserve_source_system', 'command': command}
    contract_path = output_dir / 'contract.json'
    final_system_path = output_dir / 'system.json'
    raw_score_path = output_dir / 'set_vignetting_score.json'
    report_path = output_dir / 'report.json'
    certificate_path = output_dir / 'exact_apg_certificate.json'
    upstream = exact_apg_certificate_payload(upstream_certificate)
    upstream_reusable = bool(upstream and _lensde_commands.option_truthy(upstream.get('passed'), False) and (upstream.get('certified_system_sha256') == source_hash) and (upstream.get('portable_contract_sha256') == portable_contract_hash) and (upstream.get('portable_contract') == portable_contract) and isinstance(upstream.get('set_vignetting_score'), dict))
    existing_contract = _lensde_provenance.read_json_optional(contract_path)
    _lensde_provenance.write_json(contract_path, contract)
    if upstream_reusable:
        final_system = vignetting_optimizer_aperture_copy(system)
        raw_score = copy.deepcopy(upstream['set_vignetting_score'])
        report = copy.deepcopy(upstream.get('measurement_report') or {})
        report.update({'contract': contract['contract'], 'passed': True, 'cache_hit': True, 'cache_scope': 'upstream_exact_apg_certificate', 'source_system_sha256': source_hash, 'certified_system_sha256': source_hash, 'portable_contract_sha256': portable_contract_hash, 'maximum_vignetting_coefficient_change': 0.0, 'requested_output_dir': str(requested_output_dir), 'working_output_dir': str(output_dir), 'working_path_compacted': output_dir != requested_output_dir, 'run_result': {'skipped': True, 'reason': 'source system already carries a matching exact APG certificate'}})
        certificate = copy.deepcopy(upstream)
        certificate['reused_at'] = time.strftime('%Y-%m-%dT%H:%M:%S%z')
        certificate['reuse_count'] = int(_lensde_configuration.finite_float(certificate.get('reuse_count'), 0)) + 1
        certificate['measurement_report'] = report
        _lensde_provenance.write_json(final_system_path, final_system)
        _lensde_provenance.write_json(output_dir / 'checkpoint_system.json', final_system)
        _lensde_provenance.write_json(raw_score_path, raw_score)
        _lensde_provenance.write_json(report_path, report)
        _lensde_provenance.write_json(certificate_path, certificate)
        return {'system': final_system, 'set_vignetting_score': raw_score, 'report': report, 'certificate': certificate, 'output_dir': str(output_dir)}
    reusable = existing_contract == contract and final_system_path.is_file() and raw_score_path.is_file() and report_path.is_file() and certificate_path.is_file() and _lensde_commands.option_truthy(_lensde_provenance.read_json(report_path).get('passed'), False)
    if reusable:
        final_system = _lensde_provenance.read_json(final_system_path)
        raw_score = _lensde_provenance.read_json(raw_score_path)
        report = _lensde_provenance.read_json(report_path)
        certificate = _lensde_provenance.read_json(certificate_path)
        report['cache_hit'] = True
        report['cache_scope'] = 'requested_output_directory'
        return {'system': final_system, 'set_vignetting_score': raw_score, 'report': report, 'certificate': certificate, 'output_dir': str(output_dir)}
    run_result = _lensde_asphere_contracts.run_command_logged(command, solve_dir, timeout_seconds, time.time(), child_cpu_workers)
    if int(_lensde_configuration.finite_float(run_result.get('returncode'), -1)) != 0:
        raise RuntimeError(f"exact APG Set Vignetting failed: rc={run_result.get('returncode')}, output={output_dir}")
    solved_path = solve_dir / 'system.json'
    score_path = solve_dir / 'score.json'
    if not solved_path.is_file() or not score_path.is_file():
        raise RuntimeError(f'exact APG Set Vignetting produced incomplete output: {solve_dir}')
    measured = _lensde_provenance.read_json(solved_path)
    if measured.get('asphere_root_policy') != root_policy:
        raise RuntimeError(f"exact APG root policy mismatch: expected {root_policy}, got {measured.get('asphere_root_policy')}: {output_dir}")
    raw_score = _lensde_provenance.read_json(score_path)
    source_fields = list(system.get('fields') or [])
    measured_fields = list(measured.get('fields') or [])
    if len(source_fields) != len(measured_fields):
        raise RuntimeError(f'Set Vignetting changed the field count: {len(source_fields)} -> {len(measured_fields)}')
    final_system = vignetting_optimizer_aperture_copy(system)
    final_fields = list(final_system.get('fields') or [])
    maximum_change = 0.0
    for source_field, target, measured_field in zip(source_fields, final_fields, measured_fields, strict=True):
        for coordinate in ('hx', 'hy'):
            if abs(_lensde_configuration.finite_float(source_field.get(coordinate), 0.0) - _lensde_configuration.finite_float(measured_field.get(coordinate), 0.0)) > 1e-10:
                raise RuntimeError(f'Set Vignetting changed field coordinate {coordinate}')
        for key in ('fvcx', 'fvcy', 'fvdx', 'fvdy'):
            value = _lensde_configuration.finite_float(measured_field.get(key), 0.0)
            maximum_change = max(maximum_change, abs(value - _lensde_configuration.finite_float(source_field.get(key), 0.0)))
            target[key] = value
    exact_trace_passed = bool(raw_score.get('trace_ok')) and all((_lensde_configuration.finite_float(raw_score.get(key), 0.0) >= 1.0 - 1e-12 for key in ('trace_coverage', 'min_field_coverage', 'min_chief_progress')))
    report = {'contract': contract['contract'], 'passed': exact_trace_passed, 'full_boundary_trace_required': require_full_boundary_trace, 'cache_hit': False, 'cache_scope': 'fresh_exact_measurement', 'source_system_sha256': source_hash, 'materialized_aperture_count': materialized_aperture_count, 'measurement_apertures': portable_contract['measurement_apertures'], 'excluded_surface_indices': protective_glass_surface_indices(system), 'measurement_surface_indices': [s['surface_index'] for s in portable_contract['aperture_signature']], 'portable_contract_sha256': portable_contract_hash, 'maximum_vignetting_coefficient_change': maximum_change, 'asphere_root_policy': root_policy, 'trace_ok': raw_score.get('trace_ok'), 'trace_coverage': raw_score.get('trace_coverage'), 'min_field_coverage': raw_score.get('min_field_coverage'), 'min_chief_progress': raw_score.get('min_chief_progress'), 'measurement_aiming': 'adaptive_pilot_grid_strict', 'measurement_wavelength': 'primary', 'delivered_system_update': 'field_vignetting_and_protective_glass_exclusion', 'post_measurement_projection_or_contraction': False, 'requested_output_dir': str(requested_output_dir), 'working_output_dir': str(output_dir), 'working_path_compacted': output_dir != requested_output_dir, 'run_result': run_result}
    certified_system_hash = _lensde_provenance.json_fingerprint(final_system)
    certified_portable_contract = exact_apg_portable_contract(final_system, cpu_executable, asphere_root_policy=root_policy, support_surface_indices=support_surface_indices)
    certificate = {'certificate': 'exact_apg_set_vignetting_certificate_v2', 'passed': exact_trace_passed, 'created_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'source_system_sha256': source_hash, 'certified_system_sha256': certified_system_hash, 'portable_contract': certified_portable_contract, 'portable_contract_sha256': _lensde_provenance.json_fingerprint(certified_portable_contract), 'measurement_report': copy.deepcopy(report), 'set_vignetting_score': copy.deepcopy(raw_score)}
    report['certified_system_sha256'] = certified_system_hash
    _lensde_provenance.write_json(final_system_path, final_system)
    _lensde_provenance.write_json(output_dir / 'checkpoint_system.json', final_system)
    _lensde_provenance.write_json(output_dir / 'set_vignetting_system.json', measured)
    _lensde_provenance.write_json(raw_score_path, raw_score)
    _lensde_provenance.write_json(report_path, report)
    _lensde_provenance.write_json(certificate_path, certificate)
    if require_full_boundary_trace and (not exact_trace_passed):
        raise RuntimeError(f'exact APG Set Vignetting did not satisfy the full boundary trace contract: {output_dir}')
    return {'system': final_system, 'set_vignetting_score': raw_score, 'report': report, 'certificate': certificate, 'output_dir': str(output_dir)}


def run_set_vignetting(project: dict) -> dict:
    """Recompute field vignetting factors without mutating other system data."""
    _lensde_commands.ensure_server_source_is_current()
    project = copy.deepcopy(project)
    options = copy.deepcopy(project.get('options', {}) or {})
    aiming_iter = max(40, _lensde_commands.option_int_from(options, 'aiming_iter', 40, 1))
    continuation_steps = max(16, _lensde_commands.option_int_from(options, 'aiming_continuation_steps', 16, 1))
    centroid_grid = _lensde_commands.option_int_from(options, 'set_vignetting_centroid_grid', 98, 4)
    coarse_samples = _lensde_commands.option_int_from(options, 'set_vignetting_coarse_samples', 33, 5)
    boundary_iterations = _lensde_commands.option_int_from(options, 'set_vignetting_boundary_iterations', 44, 1)
    system_aiming = project.get('system', {}).get('ray_aiming')
    if not isinstance(system_aiming, dict):
        system_aiming = {}
    aiming_mode = _lensde_configuration.canonical_aiming_mode(options.get('aiming'), _lensde_configuration.canonical_aiming_mode(system_aiming.get('mode'), 'paraxial'))
    options['aiming'] = aiming_mode
    options['aiming_iter'] = aiming_iter
    options['aiming_continuation_steps'] = continuation_steps
    run_system = _lensde_configuration.system_with_ray_aiming(project['system'], options)
    run_system, materialized_aperture_count = zemax_set_vignetting_hard_aperture_copy(run_system)
    stamp = time.strftime('%Y%m%d_%H%M%S')
    run_dir = subprocess.run_directory(state.WORK_DIR / f'vignetting_{stamp}_{int(time.time() * 1000) % 1000:03d}')
    run_dir.mkdir(parents=True, exist_ok=True)
    system_path = run_dir / 'input_system.json'
    _lensde_provenance.write_json(system_path, run_system)
    exe = _lensde_provenance.exe_path(False)
    if not exe.exists():
        raise FileNotFoundError(f'CPU executable not found: {exe}')
    batch_apg_workers = max(1, min(8, os.cpu_count() or 1))
    args = [str(exe), '--system', str(system_path), '--aiming', aiming_mode, '--aiming-iter', str(aiming_iter), '--aiming-continuation-steps', str(continuation_steps), '--set-vignetting', '--set-vignetting-exact-measurement', '--set-vignetting-no-aiming-fallback', '--set-vignetting-batch-apg-workers', str(batch_apg_workers), '--set-vignetting-centroid-grid', str(centroid_grid), '--set-vignetting-coarse-samples', str(coarse_samples), '--set-vignetting-boundary-iter', str(boundary_iterations), '--score-only', '--aiming-diagnostics', '--out-dir', str(run_dir)]
    timeout_seconds = _lensde_materials.run_timeout_seconds(project)
    started_at = time.time()
    returncode, stdout, stderr, timed_out = _lensde_execution.run_stage(args, timeout_seconds, started_at)
    if timed_out:
        raise RuntimeError('设置渐晕超时' + (f': {stderr}' if stderr else ''))
    if returncode != 0:
        detail = '\n'.join((part for part in (stdout, stderr) if part)).strip()
        raise RuntimeError(detail or f'设置渐晕失败，退出码 {returncode}')
    solved_path = run_dir / 'system.json'
    if not solved_path.exists():
        raise RuntimeError('设置渐晕完成，但未生成 system.json')
    solved_system = _lensde_provenance.read_json(solved_path)
    fields = solved_system.get('fields')
    source_fields = project.get('system', {}).get('fields') or []
    if not isinstance(fields, list) or len(fields) != len(source_fields):
        raise RuntimeError('设置渐晕返回的视场数量与输入系统不一致')
    return {'ok': True, 'fields': fields, 'stdout': stdout, 'stderr': stderr, 'run_id': run_dir.name, 'settings': {'aiming': aiming_mode, 'measurement_aiming': 'adaptive_pilot_grid_strict', 'boundary_solver': 'zemax_hard_aperture_iterative_boundary', 'materialized_aperture_count': materialized_aperture_count, 'aiming_iter': aiming_iter, 'continuation_steps': continuation_steps, 'centroid_grid': centroid_grid, 'coarse_samples': coarse_samples, 'boundary_iterations': boundary_iterations}}
