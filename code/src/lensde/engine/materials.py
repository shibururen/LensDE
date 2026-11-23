"""Extracted paper engine: materials."""
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
from . import orchestration as _lensde_orchestration
from . import populations as _lensde_populations
from . import provenance as _lensde_provenance
from . import tracing as _lensde_tracing

def read_population_score_records(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open(newline='', encoding='utf-8', errors='replace') as f:
        reader = csv.DictReader(f)
        return [dict(row) for row in reader if row]


def score_record_trace_feasible(record: dict, merit: dict) -> bool:

    def number(name: str, fallback: float=0.0) -> float:
        return _lensde_configuration.finite_float(record.get(name), fallback)
    if int(number('distortion_sign_flip_fields', 0.0)) > 0:
        return False
    trace_ok = bool(int(number('trace_ok', 0.0)))
    system_coverage = number('trace_coverage', 0.0)
    field_coverage = number('min_field_coverage', 0.0)
    if not trace_ok:
        return False
    if system_coverage + 1e-09 < _lensde_configuration.finite_float(merit.get('min_system_coverage'), 1.0):
        return False
    if field_coverage + 1e-09 < _lensde_configuration.finite_float(merit.get('min_field_coverage'), 1.0):
        return False
    if bool(merit.get('require_chief_ray', False)) and int(number('chief_ray_failed_fields', 1.0)) != 0:
        return False
    if bool(merit.get('require_off_axis_field_coverage', False)) and int(number('zero_coverage_off_axis_fields', 1.0)) != 0:
        return False
    return True


def score_record_seed_budget_feasible(record: dict, config: dict, *, allow_trace_upper: bool=False) -> bool:
    """Require an initial seed to satisfy the coupled TTL/BFL budget.

    Trace feasibility and first-order track feasibility are deliberately
    separate checks.  A seed is usable only when both are true.
    """
    if not bool(config.get('requested', False)):
        return True
    target_ttl = _lensde_configuration.finite_float(config.get('target_ttl'), 0.0)
    if target_ttl <= 0.0:
        return True
    if not bool(config.get('feasible', False)):
        return False
    ttl = _lensde_configuration.finite_float(record.get('ttl'), float('nan'))
    ttl_lower = _lensde_configuration.finite_float(config.get('seed_target_lower'), 0.0)
    ttl_upper = _lensde_configuration.finite_float(config.get('seed_target_upper'), target_ttl)
    if allow_trace_upper:
        ttl_upper = max(ttl_upper, target_ttl * max(1.0, _lensde_configuration.finite_float(config.get('trace_upper_factor'), _lensde_configuration.finite_float(config.get('active_upper_factor'), 1.0))))
    ttl_tolerance = 1e-09 * max(1.0, abs(target_ttl), abs(ttl_upper))
    if not math.isfinite(ttl) or ttl < ttl_lower - ttl_tolerance or ttl > ttl_upper + ttl_tolerance:
        return False
    target_bfl = _lensde_configuration.finite_float(config.get('target_bfl'), 0.0)
    if target_bfl <= 0.0:
        return True
    bfl = _lensde_configuration.finite_float(record.get('bfl'), float('nan'))
    bfl_lower = _lensde_configuration.finite_float(config.get('bfl_target_lower'), 0.0)
    bfl_upper = _lensde_configuration.finite_float(config.get('bfl_target_upper'), target_bfl)
    bfl_tolerance = 1e-09 * max(1.0, abs(target_bfl), abs(bfl_upper))
    return math.isfinite(bfl) and bfl >= bfl_lower - bfl_tolerance and (bfl <= bfl_upper + bfl_tolerance)


def score_record_near_trace_feasible(record: dict, merit: dict, min_coverage: float) -> bool:

    def number(name: str, fallback: float=0.0) -> float:
        return _lensde_configuration.finite_float(record.get(name), fallback)
    coverage_floor = min(1.0, max(0.0, _lensde_configuration.finite_float(min_coverage, 0.92)))
    if int(number('distortion_sign_flip_fields', 0.0)) > 0:
        return False
    if number('trace_coverage', 0.0) + 1e-09 < coverage_floor:
        return False
    if number('min_field_coverage', 0.0) + 1e-09 < coverage_floor:
        return False
    if bool(merit.get('require_chief_ray', False)) and int(number('chief_ray_failed_fields', 1.0)) != 0:
        return False
    if bool(merit.get('require_off_axis_field_coverage', False)) and int(number('zero_coverage_off_axis_fields', 1.0)) != 0:
        return False
    return True


def radius_from_curvature(value: float):
    if not math.isfinite(value) or abs(value) <= 1e-14:
        return None
    return 1.0 / value


def glass_surface_config(glass_search: dict, surface_index: int) -> dict:
    for item in glass_search.get('surface_configs', []) if isinstance(glass_search, dict) else []:
        try:
            if int(item.get('surface_index', -1)) == surface_index:
                return item
        except (TypeError, ValueError):
            continue
    return {}


def glass_candidate_key(candidate: dict) -> tuple[str, str]:
    return (str(candidate.get('name', '')).strip().lower(), str(candidate.get('reference', '') or candidate.get('library', '')).strip().lower())


def material_glass_candidate(material: dict, fallback_library: str) -> dict | None:
    if not isinstance(material, dict) or str(material.get('mode', '')).lower() != 'glass':
        return None
    name = str(material.get('name') or '').strip()
    if not name or name.lower() == 'air':
        return None
    reference = str(material.get('reference') or fallback_library or '').strip()
    filename = str(material.get('filename') or material.get('source_path') or '').strip()
    return {'name': name, 'reference': reference, 'library': reference, 'filename': filename, 'source_path': str(Path(filename) if Path(filename).is_absolute() else state.DATABASE_ROOT / 'data-nk' / filename) if filename else '', 'nd': _lensde_configuration.finite_float(material.get('nd'), 1.5), 'vd': _lensde_configuration.finite_float(material.get('vd'), 50.0), 'min_wavelength': material.get('min_wavelength', material.get('wavelength_min')), 'max_wavelength': material.get('max_wavelength', material.get('wavelength_max'))}


def prepare_glass_search_config(project: dict, variables: list[dict]) -> dict:
    system = project.get('system', {})
    required_wavelengths = system_wavelengths_um(system)
    options = project.get('options', {}) if isinstance(project.get('options'), dict) else {}
    glass_search = system.get('glass_search', {}) if isinstance(system.get('glass_search'), dict) else {}
    has_glass_choice_variable = any((variable.get('kind') == 'glass_choice' for variable in variables))
    if not bool(glass_search.get('enabled', False)) and (not has_glass_choice_variable):
        return {'enabled': False, 'groups': [], 'required_wavelengths_um': required_wavelengths}
    surfaces = system.get('surfaces', [])
    default_libraries = glass_search.get('default_libraries') or system.get('glass_libraries') or [system.get('glass_library', 'SCHOTT')]
    default_libraries = [str(item) for item in default_libraries if str(item)]
    if not default_libraries:
        default_libraries = ['SCHOTT']
    groups: list[dict] = []
    wavelength_rejections: list[dict] = []
    filter_catalog_seen = 0
    filter_accepted = 0
    filter_rejected = 0
    group_id = 0
    for variable in variables:
        if variable.get('kind') != 'glass_choice':
            continue
        try:
            surface_index = int(variable.get('index', -1))
        except (TypeError, ValueError):
            continue
        if not 0 <= surface_index < len(surfaces):
            continue
        surface = surfaces[surface_index]
        material = surface.get('material_after', {}) if isinstance(surface, dict) else {}
        if str(material.get('mode', '')).lower() not in {'glass', 'abbe'}:
            continue
        surface_cfg = _lensde_configuration.ensure_glass_surface_config(glass_search, surface_index)
        surface_cfg['enabled'] = True
        libraries = split_glass_libraries(surface_cfg.get('libraries') or default_libraries)
        if not libraries:
            libraries = ['SCHOTT']
        max_candidates = int(surface_cfg.get('max_candidates', glass_search.get('max_candidates_per_surface', 64)) or 64)
        max_candidates = max(1, min(max_candidates, 512))
        candidates: list[dict] = []
        seen: set[tuple[str, str]] = set()
        strict_libraries = bool(surface_cfg.get('strict_libraries', False))
        current = None if strict_libraries else material_glass_candidate(material, libraries[0] if libraries else system.get('glass_library', 'SCHOTT'))
        if current:
            current = _lensde_fields.glass_candidate_with_source_path(current)
            if glass_covers_wavelengths(current, required_wavelengths):
                candidates.append(current)
                seen.add(glass_candidate_key(current))
            else:
                rejection = glass_wavelength_rejection(current, required_wavelengths)
                rejection['surface_index'] = surface_index
                rejection['candidate_role'] = 'current_material'
                wavelength_rejections.append(rejection)
                filter_rejected += 1
        current_nd, current_vd = _lensde_provenance.material_nd_vd(material)
        if current_nd is None:
            current_nd = _lensde_configuration.finite_float((current or {}).get('nd'), 1.5)
        if current_vd is None:
            current_vd = _lensde_configuration.finite_float((current or {}).get('vd'), 50.0)
        current_key = glass_candidate_key(current or {})
        nearest_audit: dict = {}
        for glass in nearest_glasses(','.join(libraries), current_nd, current_vd, limit=max_candidates * 3, required_wavelengths_um=required_wavelengths, audit=nearest_audit):
            key = glass_candidate_key(glass)
            if key in seen:
                if key != current_key:
                    enriched = _lensde_fields.glass_candidate_with_source_path(glass)
                    for existing in candidates:
                        if glass_candidate_key(existing) != key:
                            continue
                        if not str(existing.get('source_path') or '').strip():
                            existing['source_path'] = enriched.get('source_path', '')
                        if not str(existing.get('filename') or '').strip():
                            existing['filename'] = enriched.get('filename', '')
                        if not str(existing.get('library') or '').strip():
                            existing['library'] = enriched.get('library', '')
                        for optional_key in ('display', 'category', 'label'):
                            if optional_key not in existing and optional_key in enriched:
                                existing[optional_key] = enriched[optional_key]
                        break
                continue
            seen.add(key)
            candidates.append(_lensde_fields.glass_candidate_with_source_path(glass))
            if len(candidates) >= max_candidates:
                break
        filter_catalog_seen += int(nearest_audit.get('catalog_seen', 0) or 0)
        filter_accepted += int(nearest_audit.get('accepted_count', 0) or 0)
        filter_rejected += int(nearest_audit.get('rejected_count', 0) or 0)
        for rejection in nearest_audit.get('rejections', []) or []:
            wavelength_rejections.append({**rejection, 'surface_index': surface_index})
        if not candidates:
            raise ValueError(f'Glass-choice surface {surface_index} has no candidates in {libraries} covering all system wavelengths {required_wavelengths} um')
        current_index = 0
        for index, candidate in enumerate(candidates):
            if glass_candidate_key(candidate) == current_key:
                current_index = index
                break
        variable['glass_group'] = group_id
        variable['lower'] = 0
        variable['upper'] = len(candidates) - 1
        variable['initial'] = current_index
        variable['term'] = 0
        groups.append({'id': group_id, 'surface_index': surface_index, 'current_index': current_index, 'libraries': libraries, 'strict_libraries': strict_libraries, 'required_wavelengths_um': required_wavelengths, 'wavelength_coverage_verified': True, 'candidates': candidates})
        group_id += 1
    return {'enabled': bool(groups), 'database_root': str(state.DATABASE_ROOT), 'mutation_rate': _lensde_configuration.finite_float(glass_search.get('mutation_rate'), 0.35), 'random_rate': _lensde_configuration.finite_float(glass_search.get('random_rate'), 0.05), 'pbest_inherit_rate': _lensde_configuration.finite_float(glass_search.get('pbest_inherit_rate'), 0.35), 'neighbor_count': int(glass_search.get('neighbor_count', 12) or 12), 'neighbor_nd_scale': _lensde_configuration.finite_float(glass_search.get('neighbor_nd_scale', options.get('glass_neighbor_nd_scale', 0.035)), 0.035), 'neighbor_vd_scale': _lensde_configuration.finite_float(glass_search.get('neighbor_vd_scale', options.get('glass_neighbor_vd_scale', 7.5)), 7.5), 'local_search': bool(glass_search.get('local_search', True)), 'required_wavelengths_um': required_wavelengths, 'wavelength_filter': {'policy': 'declared_range_must_cover_all_system_wavelengths', 'catalog_seen': filter_catalog_seen, 'accepted_count': filter_accepted, 'rejected_count': filter_rejected, 'rejections': wavelength_rejections}, 'groups': groups}


def append_glass_search_args(args: list[str], config_path: Path, config: dict, options: dict) -> None:
    if not config.get('enabled'):
        return
    args.extend(['--glass-search', '--glass-search-config', str(config_path), '--glass-database', str(state.DATABASE_ROOT), '--glass-mutation-rate', str(float(config.get('mutation_rate', options.get('glass_mutation_rate', 0.35)))), '--glass-random-rate', str(float(config.get('random_rate', options.get('glass_random_rate', 0.05)))), '--glass-pbest-inherit-rate', str(float(config.get('pbest_inherit_rate', options.get('glass_pbest_inherit_rate', 0.35)))), '--glass-neighbor-count', str(int(config.get('neighbor_count', options.get('glass_neighbor_count', 12)) or 12))])
    if not bool(config.get('local_search', True)):
        args.append('--glass-no-local-search')


def append_hammer_args(args: list[str], options: dict, run_dir: Path) -> None:
    args.extend(['--hammer', '--optimizer', 'hammer', '--optimizer-internal-scaling', 'bound_normalized', '--hammer-attempts', str(max(1, int(_lensde_configuration.finite_float(options.get('hammer_attempts'), 240)))), '--hammer-seed', str(max(0, int(_lensde_configuration.finite_float(options.get('hammer_seed'), 0)))), '--hammer-pre-polish-iter', str(max(0, int(_lensde_configuration.finite_float(options.get('hammer_pre_polish_iter'), 12)))), '--hammer-hot-polish-iter', str(max(0, int(_lensde_configuration.finite_float(options.get('hammer_hot_polish_iter'), 8)))), '--hammer-cold-polish-iter', str(max(0, int(_lensde_configuration.finite_float(options.get('hammer_cold_polish_iter'), 24)))), '--hammer-final-polish-iter', str(max(0, int(_lensde_configuration.finite_float(options.get('hammer_final_polish_iter'), 80)))), '--hammer-temp-start', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_temp_start'), 0.05))), '--hammer-temp-end', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_temp_end'), 1e-06))), '--hammer-temp-power', str(max(0.1, _lensde_configuration.finite_float(options.get('hammer_temp_power'), 2.0))), '--hammer-step-start', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_step_start'), 0.08))), '--hammer-step-end', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_step_end'), 0.004))), '--hammer-glass-rate-start', str(min(1.0, max(0.0, _lensde_configuration.finite_float(options.get('hammer_glass_rate_start'), 0.2)))), '--hammer-glass-rate-end', str(min(1.0, max(0.0, _lensde_configuration.finite_float(options.get('hammer_glass_rate_end'), 0.03)))), '--hammer-glass-random-rate', str(min(1.0, max(0.0, _lensde_configuration.finite_float(options.get('hammer_glass_random_rate'), 0.03)))), '--hammer-glass-neighbor-count', str(max(1, int(_lensde_configuration.finite_float(options.get('hammer_glass_neighbor_count'), 8)))), '--hammer-archive-size', str(max(0, int(_lensde_configuration.finite_float(options.get('hammer_archive_size'), 16)))), '--hammer-weight-continuous', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_weight_continuous'), 0.3))), '--hammer-weight-gap', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_weight_gap'), 0.18))), '--hammer-weight-bend', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_weight_bend'), 0.22))), '--hammer-weight-glass', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_weight_glass'), 0.18))), '--hammer-weight-glass-repair', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_weight_glass_repair'), 0.12))), '--hammer-image-spot-weight', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_image_spot_weight'), 1000.0))), '--hammer-merit-weight', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_merit_weight'), 0.25))), '--hammer-trace-penalty-weight', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_trace_penalty_weight'), 100.0))), '--hammer-constraint-penalty-weight', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_constraint_penalty_weight'), 25.0))), '--hammer-first-order-penalty-weight', str(max(0.0, _lensde_configuration.finite_float(options.get('hammer_first_order_penalty_weight'), 25.0))), '--local-method', str(options.get('local_method', 'trf')), '--local-bounds-mode', str(options.get('local_bounds_mode', 'safety')), '--local-residual-mode', str(options.get('local_residual_mode', 'expanded')), '--local-lambda', str(max(0.0, _lensde_configuration.finite_float(options.get('local_lambda'), 0.01))), '--local-fd', str(max(0.0, _lensde_configuration.finite_float(options.get('local_fd'), 0.0001))), '--local-min-step', str(max(0.0, _lensde_configuration.finite_float(options.get('local_min_step'), 1e-08))), '--local-tol', str(max(0.0, _lensde_configuration.finite_float(options.get('local_tol'), 0.0001))), '--local-max-reject', str(max(0, int(_lensde_configuration.finite_float(options.get('local_max_reject'), 4))))])
    if bool(options.get('hammer_write_trace', True)):
        args.extend(['--hammer-trace-jsonl', str(run_dir / 'hammer_trace.jsonl')])
    else:
        args.append('--hammer-no-trace')
    if bool(options.get('hammer_reject_trace_infeasible', False)):
        args.append('--hammer-reject-trace-infeasible')


def apply_variable_values_to_system(system: dict, variables: list[dict], values: list[float], glass_search_config: dict | None=None) -> dict:
    out = copy.deepcopy(system)
    surfaces = out.get('surfaces', [])
    fields = out.get('fields', [])
    automatic_magnitudes: dict[int, float] = {}
    automatic_asymmetries: dict[int, float] = {}
    for variable, value in zip(variables, values, strict=False):
        kind = str(variable.get('kind', ''))
        value = _lensde_populations.project_optimization_variable_value(value, variable)
        try:
            index = int(variable.get('index', 0) or 0)
            term = int(variable.get('term', 0) or 0)
        except (TypeError, ValueError):
            continue
        if kind in {'curvature', 'radius', 'thickness', 'conic', 'asphere_coeff', 'material_nd', 'material_vd'}:
            if not 0 <= index < len(surfaces):
                continue
            surface = surfaces[index]
            if kind == 'curvature':
                surface['radius'] = radius_from_curvature(float(value))
            elif kind == 'radius':
                surface['radius'] = None if not math.isfinite(float(value)) else float(value)
            elif kind == 'thickness':
                surface['thickness'] = float(value)
            elif kind == 'conic':
                surface['conic'] = float(value)
            elif kind == 'asphere_coeff':
                coeffs = surface.setdefault('asphere_coefficients', [])
                while len(coeffs) <= term:
                    coeffs.append(0.0)
                coeffs[term] = float(value)
                if str(surface.get('kind', '')).lower() == 'standard':
                    surface['kind'] = 'even_asphere'
            elif kind in {'material_nd', 'material_vd'}:
                material = surface.setdefault('material_after', {})
                current_nd = _lensde_configuration.finite_float(material.get('nd'), math.nan)
                current_vd = _lensde_configuration.finite_float(material.get('vd'), math.nan)
                if kind == 'material_nd':
                    new_nd = float(value)
                    new_vd = current_vd if math.isfinite(current_vd) and current_vd > 0.0 else 50.0
                else:
                    new_nd = current_nd if math.isfinite(current_nd) and current_nd > 1.0 else 1.5
                    new_vd = float(value)
                material['mode'] = 'abbe'
                material['name'] = 'model-abbe'
                material['reference'] = ''
                material['source_path'] = ''
                material['nd'] = new_nd
                material['vd'] = new_vd
        elif kind == 'glass_choice':
            groups = (glass_search_config or {}).get('groups', [])
            group = None
            try:
                group_id = int(variable.get('glass_group', -1))
            except (TypeError, ValueError):
                group_id = -1
            for item in groups:
                if int(item.get('id', -999)) == group_id or int(item.get('surface_index', -1)) == index:
                    group = item
                    break
            candidates = group.get('candidates', []) if isinstance(group, dict) else []
            if not 0 <= index < len(surfaces) or not candidates:
                continue
            choice = max(0, min(len(candidates) - 1, int(round(float(value)))))
            glass = candidates[choice]
            material = surfaces[index].setdefault('material_after', {})
            material.update({'mode': 'glass', 'name': glass.get('name', material.get('name', '')), 'reference': glass.get('reference', glass.get('library', material.get('reference', ''))), 'nd': _lensde_configuration.finite_float(glass.get('nd'), _lensde_configuration.finite_float(material.get('nd'), 1.5)), 'vd': _lensde_configuration.finite_float(glass.get('vd'), _lensde_configuration.finite_float(material.get('vd'), 50.0)), 'filename': glass.get('filename', material.get('filename', '')), 'source_path': glass.get('source_path', material.get('source_path', ''))})
        elif kind == 'vignette_x' and 0 <= index < len(fields):
            fields[index]['fvdx'] = float(value)
            fields[index]['vignette_x'] = float(value)
        elif _lensde_configuration.canonical_vignetting_variable_kind(kind) == 'fvcy' and 0 <= index < len(fields):
            fields[index]['fvcy'] = float(value)
        elif _lensde_configuration.canonical_vignetting_variable_kind(kind) == 'fvdy' and 0 <= index < len(fields):
            fields[index]['fvdy'] = float(value)
            fields[index]['vignette_y'] = float(value)
        elif _lensde_configuration.canonical_vignetting_variable_kind(kind) == 'vignette_magnitude_y' and 0 <= index < len(fields):
            automatic_magnitudes[index] = max(0.0, min(1.0, float(value)))
        elif _lensde_configuration.canonical_vignetting_variable_kind(kind) == 'vignette_asymmetry_y' and 0 <= index < len(fields):
            automatic_asymmetries[index] = max(-1.0, min(1.0, float(value)))
    for index in sorted(set(automatic_magnitudes) | set(automatic_asymmetries)):
        field = fields[index]
        inherited_magnitude = max(0.0, _lensde_configuration.finite_float(field.get('fvdy', field.get('vignette_y')), 0.0))
        magnitude = automatic_magnitudes.get(index, inherited_magnitude)
        inherited_asymmetry = max(-1.0, min(1.0, _lensde_configuration.finite_float(field.get('fvcy'), 0.0) / inherited_magnitude)) if inherited_magnitude > 1e-15 else 0.0
        asymmetry = automatic_asymmetries.get(index, inherited_asymmetry)
        field['fvdy'] = magnitude
        field['vignette_y'] = magnitude
        field['fvcy'] = asymmetry * magnitude
    return out


def trace_args_for_project(options: dict, run_merit: dict, out_dir: Path, *, system_path: Path, merit_path: Path) -> list[str]:
    args = ['--system', str(system_path), '--merit', str(merit_path), '--aiming', _lensde_configuration.canonical_aiming_mode(options.get('aiming', 'robust'), 'robust'), '--aiming-iter', str(int(options.get('aiming_iter', 20))), '--rays', str(int(run_merit.get('num_rays', 3))), '--arms', str(int(run_merit.get('pupil_arms', 6))), '--distribution', str(run_merit.get('pupil_distribution', 'gaussian_ring_arm')), '--sampling-seed', str(int(run_merit.get('sampling_seed', 1))), '--spot-rays', str(int(options.get('spot_rays', 41))), '--spot-arms', str(int(options.get('spot_arms', 64))), '--spot-distribution', str(options.get('spot_distribution', 'uniform')), '--spot-sampling-seed', str(int(options.get('spot_sampling_seed', run_merit.get('sampling_seed', 1)))), '--out-dir', str(out_dir)]
    append_aiming_policy_args(args, options)
    return args


def validate_unified_aiming_command(args: list[str], options: dict) -> str:
    """Reject a stage command that overrides the GUI-selected aiming mode."""
    expected = _lensde_configuration.canonical_aiming_mode(options.get('aiming', 'robust'), 'robust')
    positions = [index for index, value in enumerate(args) if value == '--aiming']
    if len(positions) != 1 or positions[0] + 1 >= len(args):
        raise ValueError('optimization command must contain exactly one --aiming value')
    actual = _lensde_configuration.canonical_aiming_mode(args[positions[0] + 1], '')
    if actual != expected:
        raise ValueError(f'optimization command aiming mode {actual!r} does not match GUI mode {expected!r}')
    return expected


def append_aiming_policy_args(args: list[str], options: dict, prefix: str='') -> None:
    _ = prefix
    mode = validate_unified_aiming_command(args, options)
    if bool(options.get('aiming_require_convergence', False)):
        args.append('--aiming-require-convergence')
    else:
        args.append('--aiming-best-effort')
    args.extend(['--aiming-continuation-steps', str(max(1, int(options.get('aiming_continuation_steps', 8) or 8))), '--aiming-stop-radius-source', _lensde_configuration.canonical_stop_radius_source(_lensde_configuration.aiming_option(options, prefix, 'aiming_stop_radius_source', 'paraxial'), 'paraxial')])
    if _lensde_configuration.is_adaptive_aiming(mode):
        args.extend(['--aiming-pilot-fine-grid', str(max(3, int(_lensde_configuration.aiming_option(options, prefix, 'aiming_pilot_fine_grid', 5) or 5))), '--aiming-pilot-coarse-grid', str(max(3, int(_lensde_configuration.aiming_option(options, prefix, 'aiming_pilot_coarse_grid', 13) or 13))), '--aiming-pilot-nearest-k', str(max(1, int(_lensde_configuration.aiming_option(options, prefix, 'aiming_pilot_nearest_k', 3) or 3))), '--aiming-pilot-iterative-failure-limit', str(max(0, int(_lensde_configuration.aiming_option(options, prefix, 'aiming_pilot_iterative_failure_limit', 0) or 0)))])
        if not bool(_lensde_configuration.aiming_option(options, prefix, 'aiming_pilot_per_ray_iterative', True)):
            args.append('--aiming-pilot-no-per-ray-iterative')
        if bool(_lensde_configuration.aiming_option(options, prefix, 'aiming_pilot_allow_interpolation', False)):
            args.append('--aiming-pilot-allow-interpolation')
    if _lensde_configuration.is_calibrated_aiming(mode):
        calibrated_fallback_default = 'none'
        calibrated_chief_default = 'abcd_budget_angle_bridge_fast'
        budget_solver_default = 'hybrid'
        args.extend(['--aiming-calibrated-sampling-mode', _lensde_configuration.canonical_calibrated_sampling_mode(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_sampling_mode', 'chief_edge_interpolation'), 'chief_edge_interpolation'), '--aiming-calibrated-correction-iterations', str(max(0, int(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_correction_iterations', 0) or 0))), '--aiming-calibrated-fallback', _lensde_configuration.canonical_calibrated_fallback(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_fallback', calibrated_fallback_default), calibrated_fallback_default), '--aiming-calibrated-chief-mode', _lensde_configuration.canonical_calibrated_chief_mode(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_chief_mode', calibrated_chief_default), calibrated_chief_default), '--aiming-calibrated-budget-chief-iter', str(max(0, int(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_iter', 8) or 8))), '--aiming-calibrated-budget-chief-solver', _lensde_configuration.canonical_calibrated_budget_solver(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_solver', budget_solver_default), budget_solver_default), '--aiming-calibrated-budget-chief-acceptance', _lensde_configuration.canonical_calibrated_budget_acceptance(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_acceptance', 'residual')), '--aiming-calibrated-budget-chief-stop-fraction', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_stop_fraction', 0.05), 0.05))), '--aiming-calibrated-budget-chief-residual-stop-fraction', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_residual_stop_fraction', 1e-06), 1e-06))), '--aiming-calibrated-budget-chief-min-a', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_min_a', 1e-06), 1e-06))), '--aiming-calibrated-budget-chief-field-angle', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_field_angle', 75.0), 75.0))), '--aiming-calibrated-budget-chief-skip-abcd-angle', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_skip_abcd_angle', 60.0), 60.0))), '--aiming-calibrated-edge-calibration-iterations', str(max(0, int(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_iterations', 8) or 8))), '--aiming-calibrated-edge-calibration-solver', _lensde_configuration.canonical_aiming_edge_solver(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_solver', 'secant'), 'secant'), '--aiming-calibrated-edge-calibration-weight-power', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_weight_power', 2.0), 2.0))), '--aiming-calibrated-edge-calibration-residual-stop-fraction', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_residual_stop_fraction', 1e-06), 1e-06)))])
        if bool(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_enabled', True)):
            args.append('--aiming-calibrated-edge-calibration')
        else:
            args.append('--aiming-calibrated-no-edge-calibration')
        if bool(_lensde_configuration.aiming_option(options, prefix, 'aiming_calibrated_budget_chief_ignore_aperture_clip', True)):
            args.append('--aiming-calibrated-budget-chief-ignore-aperture-clip')
        else:
            args.append('--aiming-calibrated-budget-chief-respect-aperture-clip')
    if _lensde_configuration.is_three_step_aiming(mode):
        three_step_fallback_default = 'adaptive_pilot_grid'
        args.extend(['--aiming-three-step-sampling-mode', _lensde_configuration.canonical_calibrated_sampling_mode(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_sampling_mode', 'chief_edge_interpolation'), 'chief_edge_interpolation'), '--aiming-three-step-chief-rays', str(max(3, int(_lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_chief_rays', 33), 33)))), '--aiming-three-step-max-angle', str(max(0.1, min(89.9, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_max_angle', 89.0), 89.0)))), '--aiming-three-step-edge-passes', str(max(1, int(_lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_edge_passes', 2), 2)))), '--aiming-three-step-meridional-iter', str(max(0, int(_lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_meridional_iter', 8), 8)))), '--aiming-three-step-sagittal-iter', str(max(0, int(_lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_sagittal_iter', 8), 8)))), '--aiming-three-step-edge-solver', _lensde_configuration.canonical_aiming_edge_solver(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_edge_solver', 'secant'), 'secant'), '--aiming-three-step-edge-residual-stop-fraction', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_edge_residual_stop_fraction', 1e-06), 1e-06))), '--aiming-three-step-edge-radius-fraction', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_edge_radius_fraction', 1.0), 1.0))), '--aiming-three-step-accept-stop-fraction', str(max(0.0, _lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_accept_stop_fraction', 0.05), 0.05))), '--aiming-three-step-correction-iterations', str(max(0, int(_lensde_configuration.finite_float(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_correction_iterations', 0), 0)))), '--aiming-three-step-fallback', _lensde_configuration.canonical_calibrated_fallback(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_fallback', three_step_fallback_default), three_step_fallback_default)])
        if bool(_lensde_configuration.aiming_option(options, prefix, 'aiming_three_step_sample_stop_residual_debug', False)):
            args.append('--aiming-three-step-debug-stop-residual')
        else:
            args.append('--aiming-three-step-no-debug-stop-residual')


def system_wavelengths_um(system: dict | None) -> list[float]:
    """Return every finite wavelength in the optical contract, in micrometres."""
    waves = system.get('wavelengths', []) if isinstance(system, dict) else []
    values: set[float] = set()
    for wave in waves if isinstance(waves, list) else []:
        if not isinstance(wave, dict):
            continue
        value = _lensde_configuration.finite_float(wave.get('value_um', wave.get('value')), math.nan)
        if math.isfinite(value) and value > 0.0:
            values.add(value)
    return sorted(values) or [0.5875618]


def normalized_required_wavelengths(required_wavelengths_um) -> list[float]:
    values: set[float] = set()
    for value in required_wavelengths_um or []:
        number = _lensde_configuration.finite_float(value, math.nan)
        if math.isfinite(number) and number > 0.0:
            values.add(number)
    return sorted(values)


def glass_wavelength_range(glass: dict | None) -> tuple[float | None, float | None]:
    if not isinstance(glass, dict):
        return (None, None)
    lo = _lensde_configuration.finite_float(glass.get('min_wavelength', glass.get('wavelength_min')), math.nan)
    hi = _lensde_configuration.finite_float(glass.get('max_wavelength', glass.get('wavelength_max')), math.nan)
    if math.isfinite(lo) and math.isfinite(hi) and (lo <= hi):
        return (lo, hi)
    source = str(glass.get('source_path') or glass.get('filename') or '').strip()
    if source:
        specs = _lensde_provenance.glass_specs(source)
        lo = _lensde_configuration.finite_float(specs.get('min_wavelength'), math.nan)
        hi = _lensde_configuration.finite_float(specs.get('max_wavelength'), math.nan)
        if math.isfinite(lo) and math.isfinite(hi) and (lo <= hi):
            return (lo, hi)
    return (None, None)


def glass_covers_wavelengths(glass: dict | None, required_wavelengths_um, *, tolerance_um: float=1e-09) -> bool:
    required = normalized_required_wavelengths(required_wavelengths_um)
    if not required:
        return True
    lo, hi = glass_wavelength_range(glass)
    if lo is None or hi is None:
        return False
    tolerance = max(0.0, _lensde_configuration.finite_float(tolerance_um, 1e-09))
    return lo <= required[0] + tolerance and hi + tolerance >= required[-1]


def glass_wavelength_rejection(glass: dict, required_wavelengths_um) -> dict:
    lo, hi = glass_wavelength_range(glass)
    return {'name': str(glass.get('name') or glass.get('reference') or ''), 'library': str(glass.get('library') or glass.get('reference') or ''), 'min_wavelength_um': lo, 'max_wavelength_um': hi, 'required_wavelengths_um': normalized_required_wavelengths(required_wavelengths_um), 'reason': 'missing_declared_wavelength_range' if lo is None or hi is None else 'wavelength_range_not_covering_system'}


def resolve_material_catalog_candidate(material: dict, fallback_library: str='') -> dict | None:
    candidate = material_glass_candidate(material, fallback_library)
    if candidate is None:
        return None
    candidate = _lensde_fields.glass_candidate_with_source_path(candidate)
    lo, hi = glass_wavelength_range(candidate)
    if lo is not None and hi is not None:
        return candidate
    name = str(candidate.get('name') or '').strip()
    reference = str(candidate.get('reference') or fallback_library or '').strip()
    for catalog_candidate in list_glasses(name, reference, limit=128):
        if glass_candidate_key(catalog_candidate) == glass_candidate_key(candidate):
            return _lensde_fields.glass_candidate_with_source_path(catalog_candidate)
    return candidate


def validate_system_material_wavelength_coverage(system: dict, variables: list[dict] | None=None) -> dict:
    """Audit exact, fixed catalog materials before an optimization starts.

    Surfaces controlled by continuous material variables or GlassChoice are
    checked through their wavelength-filtered point cloud/candidate group.
    """
    required = system_wavelengths_um(system)
    variable_material_indices = {int(variable.get('index', -1)) for variable in variables or [] if str(variable.get('kind') or '') in {'material_nd', 'material_vd', 'glass_choice'}}
    rejections: list[dict] = []
    checked = 0
    surfaces = system.get('surfaces', []) if isinstance(system, dict) else []
    for index, surface in enumerate(surfaces if isinstance(surfaces, list) else []):
        if index in variable_material_indices or not isinstance(surface, dict):
            continue
        material = surface.get('material_after', {})
        if not isinstance(material, dict) or str(material.get('mode', '')).lower() != 'glass':
            continue
        candidate = resolve_material_catalog_candidate(material, str(material.get('reference') or system.get('glass_library') or ''))
        if candidate is None:
            continue
        checked += 1
        if glass_covers_wavelengths(candidate, required):
            continue
        rejection = glass_wavelength_rejection(candidate, required)
        rejection['surface_index'] = index
        rejection['candidate_role'] = 'fixed_system_material'
        rejections.append(rejection)
    return {'ok': not rejections, 'policy': 'declared_range_must_cover_all_system_wavelengths', 'required_wavelengths_um': required, 'fixed_materials_checked': checked, 'rejected_count': len(rejections), 'rejections': rejections}


def assert_system_material_wavelength_coverage(system: dict, variables: list[dict] | None=None) -> dict:
    audit = validate_system_material_wavelength_coverage(system, variables)
    if audit['ok']:
        return audit
    details = '; '.join((f"S{item['surface_index']} {item['name']} [{item['min_wavelength_um']}, {item['max_wavelength_um']}] um" for item in audit['rejections']))
    raise ValueError(f"Fixed system materials do not cover all configured wavelengths {audit['required_wavelengths_um']} um: {details}")


def glass_library_name(row: dict) -> str:
    parts = row.get('filename', '').replace('\\', '/').split('/')
    if len(parts) >= 2 and parts[0] == 'glass':
        return parts[1].upper()
    return row.get('reference', '').strip()


def list_glass_libraries() -> list[dict]:
    if not state.CATALOG_PATH.exists():
        return []
    counts: dict[str, int] = {}
    with state.CATALOG_PATH.open(newline='', encoding='utf-8', errors='ignore') as f:
        for row in csv.DictReader(f):
            filename = row.get('filename', '')
            group = row.get('group', '')
            if group != 'glass' and (not filename.startswith('glass/')):
                continue
            library = glass_library_name(row)
            if not library:
                continue
            counts[library] = counts.get(library, 0) + 1
    return [{'name': name, 'count': count} for name, count in sorted(counts.items())]


def split_glass_libraries(value) -> list[str]:
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        items = value
    else:
        items = str(value or '').replace(';', ',').split(',')
    out: list[str] = []
    seen: set[str] = set()
    for item in items:
        name = str(item or '').strip()
        if not name:
            continue
        key = name.upper()
        if key in seen:
            continue
        out.append(name)
        seen.add(key)
    return out


def summarize_glass_points(points: list[tuple[float, float]]) -> dict | None:
    valid = [(_lensde_configuration.finite_float(nd, math.nan), _lensde_configuration.finite_float(vd, math.nan)) for nd, vd in points if math.isfinite(_lensde_configuration.finite_float(nd, math.nan)) and math.isfinite(_lensde_configuration.finite_float(vd, math.nan))]
    valid = [(nd, vd) for nd, vd in valid if nd > 1.0 and vd > 0.0]
    if not valid:
        return None
    nds = [nd for nd, _ in valid]
    vds = [vd for _, vd in valid]
    return {'count': len(valid), 'nd_min': min(nds), 'nd_max': max(nds), 'vd_min': min(vds), 'vd_max': max(vds), 'nd_mean': sum(nds) / len(nds), 'vd_mean': sum(vds) / len(vds)}


def glass_library_material_bounds_cache() -> dict:
    if not state.CATALOG_PATH.exists():
        return {}
    catalog_mtime = state.CATALOG_PATH.stat().st_mtime
    version = 1
    if isinstance(state.GLASS_LIBRARY_BOUNDS_CACHE, dict) and state.GLASS_LIBRARY_BOUNDS_CACHE.get('version') == version and (state.GLASS_LIBRARY_BOUNDS_CACHE.get('catalog_mtime') == catalog_mtime):
        return state.GLASS_LIBRARY_BOUNDS_CACHE.get('libraries', {})
    try:
        cached = json.loads(state.GLASS_LIBRARY_BOUNDS_CACHE_PATH.read_text(encoding='utf-8'))
        if isinstance(cached, dict) and cached.get('version') == version and (cached.get('catalog_mtime') == catalog_mtime):
            state.GLASS_LIBRARY_BOUNDS_CACHE = cached
            return cached.get('libraries', {})
    except Exception:
        pass
    by_library: dict[str, list[tuple[float, float]]] = {}
    all_points: list[tuple[float, float]] = []
    with state.CATALOG_PATH.open(newline='', encoding='utf-8', errors='ignore') as f:
        for row in csv.DictReader(f):
            filename = row.get('filename', '')
            group = row.get('group', '')
            if group != 'glass' and (not filename.startswith('glass/')):
                continue
            library = glass_library_name(row)
            if not library:
                continue
            specs = _lensde_provenance.glass_specs(filename)
            nd = _lensde_configuration.finite_float(specs.get('nd'), math.nan)
            vd = _lensde_configuration.finite_float(specs.get('vd'), math.nan)
            if not (math.isfinite(nd) and math.isfinite(vd) and (nd > 1.0) and (vd > 0.0)):
                continue
            key = library.upper()
            by_library.setdefault(key, []).append((nd, vd))
            all_points.append((nd, vd))
    libraries: dict[str, dict] = {}
    for name, points in by_library.items():
        summary = summarize_glass_points(points)
        if summary:
            libraries[name] = summary
    all_summary = summarize_glass_points(all_points)
    if all_summary:
        libraries['ALL'] = all_summary
    payload = {'version': version, 'catalog': str(state.CATALOG_PATH), 'catalog_mtime': catalog_mtime, 'libraries': libraries, 'generated_at': time.strftime('%Y-%m-%dT%H:%M:%S')}
    state.GLASS_LIBRARY_BOUNDS_CACHE = payload
    try:
        state.GLASS_LIBRARY_BOUNDS_CACHE_PATH.write_text(_lensde_provenance.json_dumps_strict(payload, ensure_ascii=False, indent=2), encoding='utf-8')
    except Exception:
        pass
    return libraries


def glass_library_material_points_cache() -> dict[str, list[tuple[float, float]]]:
    if not state.CATALOG_PATH.exists():
        return {}
    catalog_mtime = state.CATALOG_PATH.stat().st_mtime
    version = 1
    if isinstance(state.GLASS_LIBRARY_POINTS_CACHE, dict) and state.GLASS_LIBRARY_POINTS_CACHE.get('version') == version and (state.GLASS_LIBRARY_POINTS_CACHE.get('catalog_mtime') == catalog_mtime):
        return state.GLASS_LIBRARY_POINTS_CACHE.get('libraries', {})
    by_library: dict[str, set[tuple[float, float]]] = {}
    all_points: set[tuple[float, float]] = set()
    with state.CATALOG_PATH.open(newline='', encoding='utf-8', errors='ignore') as f:
        for row in csv.DictReader(f):
            filename = row.get('filename', '')
            group = row.get('group', '')
            if group != 'glass' and (not filename.startswith('glass/')):
                continue
            library = glass_library_name(row)
            if not library:
                continue
            specs = _lensde_provenance.glass_specs(filename)
            nd = _lensde_configuration.finite_float(specs.get('nd'), math.nan)
            vd = _lensde_configuration.finite_float(specs.get('vd'), math.nan)
            if not (math.isfinite(nd) and math.isfinite(vd) and (nd > 1.0) and (vd > 0.0)):
                continue
            point = (nd, vd)
            by_library.setdefault(library.upper(), set()).add(point)
            all_points.add(point)
    libraries = {name: sorted(points, key=lambda point: (point[1], point[0])) for name, points in by_library.items()}
    libraries['ALL'] = sorted(all_points, key=lambda point: (point[1], point[0]))
    state.GLASS_LIBRARY_POINTS_CACHE = {'version': version, 'catalog_mtime': catalog_mtime, 'libraries': libraries}
    return libraries


def glass_material_points_for_libraries(libraries, required_wavelengths_um=None) -> list[tuple[float, float]]:
    names = split_glass_libraries(libraries)
    required = normalized_required_wavelengths(required_wavelengths_um)
    if required:
        selected = list_glasses('', ','.join(names), limit=1000000, required_wavelengths_um=required)
        points = {(_lensde_configuration.finite_float(glass.get('nd'), math.nan), _lensde_configuration.finite_float(glass.get('vd'), math.nan)) for glass in selected}
        return sorted(((nd, vd) for nd, vd in points if math.isfinite(nd) and math.isfinite(vd) and (nd > 1.0) and (vd > 0.0)), key=lambda point: (point[1], point[0]))
    cache = glass_library_material_points_cache()
    if not names or any((name.upper() == 'ALL' for name in names)):
        return list(cache.get('ALL', []))
    points: set[tuple[float, float]] = set()
    for name in names:
        points.update(cache.get(name.upper(), []))
    return sorted(points, key=lambda point: (point[1], point[0]))


def initial_material_population_summary(system: dict, variables: list[dict], rows: list[list[float]]) -> dict:
    positions: dict[int, dict[str, int]] = {}
    for position, variable in enumerate(variables):
        kind = str(variable.get('kind', ''))
        if kind in {'material_nd', 'material_vd'}:
            index = int(variable.get('index', 0) or 0)
            positions.setdefault(index, {})[kind] = position
    try:
        region_plan = _lensde_populations.prepare_catalog_material_seed_sampling_plan(system, variables)
    except ValueError:
        region_plan = []
    region_diversity_applicable = _lensde_populations.material_seed_region_diversity_applicable(region_plan, 2)
    row_region_counts = [_lensde_populations.material_seed_row_region_count(row, variables, region_plan) for row in rows]
    region_count_histogram = Counter(row_region_counts)

    def distribution(values: list[float]) -> dict:
        ordered = sorted((value for value in values if math.isfinite(value)))
        if not ordered:
            return {'min': None, 'median': None, 'max': None}
        middle = len(ordered) // 2
        median = ordered[middle] if len(ordered) % 2 else 0.5 * (ordered[middle - 1] + ordered[middle])
        return {'min': ordered[0], 'median': median, 'max': ordered[-1]}
    surfaces = []
    for index in sorted(positions):
        by_kind = positions[index]
        nd_position = by_kind.get('material_nd')
        vd_position = by_kind.get('material_vd')
        nd_values = [_lensde_configuration.finite_float(row[nd_position], math.nan) for row in rows if nd_position is not None and nd_position < len(row)]
        vd_values = [_lensde_configuration.finite_float(row[vd_position], math.nan) for row in rows if vd_position is not None and vd_position < len(row)]
        libraries = glass_libraries_for_material_bounds(system, index)
        catalog_points = glass_material_points_for_libraries(libraries, system_wavelengths_um(system))
        catalog_vds = sorted((vd for _nd, vd in catalog_points))
        strata = {'low': 0, 'middle': 0, 'high': 0}
        low_upper = None
        middle_upper = None
        if catalog_vds:
            low_upper = catalog_vds[max(0, len(catalog_vds) // 3 - 1)]
            middle_upper = catalog_vds[max(0, 2 * len(catalog_vds) // 3 - 1)]
            for vd in vd_values:
                if vd <= low_upper:
                    strata['low'] += 1
                elif vd <= middle_upper:
                    strata['middle'] += 1
                else:
                    strata['high'] += 1
        catalog_exact_count = 0
        if nd_position is not None and vd_position is not None and catalog_points:
            exact_points = {(round(nd, 10), round(vd, 10)) for nd, vd in catalog_points}
            catalog_exact_count = sum(((round(_lensde_configuration.finite_float(row[nd_position], math.nan), 10), round(_lensde_configuration.finite_float(row[vd_position], math.nan), 10)) in exact_points for row in rows if nd_position < len(row) and vd_position < len(row)))
        surfaces.append({'surface_index': index, 'libraries': libraries, 'catalog_point_count': len(catalog_points), 'sample_count': max(len(nd_values), len(vd_values)), 'unique_pair_count': len({(round(_lensde_configuration.finite_float(row[nd_position], math.nan), 10), round(_lensde_configuration.finite_float(row[vd_position], math.nan), 10)) for row in rows if nd_position is not None and vd_position is not None and (nd_position < len(row)) and (vd_position < len(row))}), 'nd': distribution(nd_values), 'vd': distribution(vd_values), 'vd_strata_upper': {'low': low_upper, 'middle': middle_upper}, 'vd_strata_counts': strata, 'catalog_exact_count': catalog_exact_count, 'catalog_exact_fraction': catalog_exact_count / len(rows) if rows and nd_position is not None and (vd_position is not None) else None})
    return {'policy': 'catalog_joint_nd_vd_equal_count_vd_strata', 'row_count': len(rows), 'surface_count': len(surfaces), 'individual_region_coverage': {'applicable': region_diversity_applicable, 'minimum_regions': 2 if region_diversity_applicable else None, 'region_count_histogram': {str(count): region_count_histogram[count] for count in sorted(region_count_histogram)}, 'single_region_row_count': sum((count < 2 for count in row_region_counts)), 'all_rows_satisfy_minimum': all((count >= 2 for count in row_region_counts)) if region_diversity_applicable else None}, 'surfaces': surfaces}


def glass_material_bounds_for_libraries(libraries, required_wavelengths_um=None) -> tuple[float, float, float, float]:
    names = split_glass_libraries(libraries)
    required = normalized_required_wavelengths(required_wavelengths_um)
    if required:
        points = glass_material_points_for_libraries(names, required)
        summary = summarize_glass_points(points)
        if not summary:
            raise ValueError(f"No materials in libraries {names or ['ALL']} cover all system wavelengths {required} um")
        selected = [summary]
    else:
        cache = glass_library_material_bounds_cache()
        selected = []
        if not names or any((name.upper() == 'ALL' for name in names)):
            if 'ALL' in cache:
                selected.append(cache['ALL'])
        else:
            for name in names:
                item = cache.get(name.upper())
                if item:
                    selected.append(item)
        if not selected and 'ALL' in cache:
            selected.append(cache['ALL'])
    if not selected:
        return (1.25, 2.3, 15.0, 100.0)
    nd_min = min((_lensde_configuration.finite_float(item.get('nd_min'), 1.25) for item in selected))
    nd_max = max((_lensde_configuration.finite_float(item.get('nd_max'), 2.3) for item in selected))
    vd_min = min((_lensde_configuration.finite_float(item.get('vd_min'), 15.0) for item in selected))
    vd_max = max((_lensde_configuration.finite_float(item.get('vd_max'), 100.0) for item in selected))
    nd_span = max(0.0, nd_max - nd_min)
    vd_span = max(0.0, vd_max - vd_min)
    nd_pad = max(state.GLASS_MATERIAL_ND_MARGIN, 0.02 * nd_span)
    vd_pad = max(state.GLASS_MATERIAL_VD_MARGIN, 0.02 * vd_span)
    return (max(1.0, nd_min - nd_pad), min(3.0, nd_max + nd_pad), max(1.0, vd_min - vd_pad), min(150.0, vd_max + vd_pad))


def glass_libraries_for_material_bounds(system: dict, surface_index: int) -> list[str]:
    glass_search = system.get('glass_search', {}) if isinstance(system.get('glass_search'), dict) else {}
    default_libraries = glass_search.get('default_libraries') or system.get('glass_libraries') or [system.get('glass_library', 'SCHOTT')]
    surface_cfg = glass_surface_config(glass_search, surface_index)
    return split_glass_libraries(surface_cfg.get('libraries') or default_libraries) or ['SCHOTT']


def glass_material_bounds_for_surface(system: dict, surface_index: int) -> tuple[float, float, float, float]:
    return glass_material_bounds_for_libraries(glass_libraries_for_material_bounds(system, surface_index), system_wavelengths_um(system))


def list_glasses(query: str='', library: str='', limit: int=250, *, required_wavelengths_um=None, audit: dict | None=None) -> list[dict]:
    if not state.CATALOG_PATH.exists():
        return []
    required = normalized_required_wavelengths(required_wavelengths_um)
    q = query.strip().lower()
    requested_libraries = {item.lower() for item in split_glass_libraries(library)}
    all_libraries = not requested_libraries or 'all' in requested_libraries
    rows = []
    seen = set()
    with state.CATALOG_PATH.open(newline='', encoding='utf-8', errors='ignore') as f:
        for row in csv.DictReader(f):
            filename = row.get('filename', '')
            group = row.get('group', '')
            if group != 'glass' and (not filename.startswith('glass/')):
                continue
            reference = row.get('reference', '')
            library_name = glass_library_name(row)
            if not all_libraries and library_name.lower() not in requested_libraries:
                continue
            key = (row.get('filename_no_ext', ''), library_name, filename)
            text = ' '.join([row.get('filename_no_ext', ''), library_name, reference, row.get('name', ''), row.get('category_name_full', '')]).lower()
            if q and q not in text:
                continue
            if key in seen:
                continue
            seen.add(key)
            specs = _lensde_provenance.glass_specs(filename)
            name = row.get('filename_no_ext') or row.get('reference') or row.get('name')
            label = f'{name} / {reference}' if reference else name
            glass = {'name': name, 'reference': reference, 'library': library_name, 'display': row.get('name', ''), 'category': row.get('category_name_full', ''), 'filename': filename, 'source_path': str(state.DATABASE_ROOT / 'data-nk' / filename) if filename else '', 'min_wavelength': _lensde_configuration.finite_float(row.get('min_wavelength'), _lensde_configuration.finite_float(specs.get('min_wavelength'), math.nan)), 'max_wavelength': _lensde_configuration.finite_float(row.get('max_wavelength'), _lensde_configuration.finite_float(specs.get('max_wavelength'), math.nan)), 'nd': specs['nd'], 'vd': specs['vd'], 'label': label}
            if audit is not None:
                audit['catalog_seen'] = int(audit.get('catalog_seen', 0) or 0) + 1
            if required and (not glass_covers_wavelengths(glass, required)):
                if audit is not None:
                    audit['rejected_count'] = int(audit.get('rejected_count', 0) or 0) + 1
                    audit.setdefault('rejections', []).append(glass_wavelength_rejection(glass, required))
                continue
            rows.append(glass)
            if audit is not None:
                audit['accepted_count'] = int(audit.get('accepted_count', 0) or 0) + 1
            if len(rows) >= limit:
                break
    if audit is not None:
        audit['required_wavelengths_um'] = required
    return rows


def glass_nd_vd_distance(nd: float, vd: float, glass: dict, *, nd_scale: float=state.GLASS_DISTANCE_ND_SCALE, vd_scale: float=state.GLASS_DISTANCE_VD_SCALE, vd_metric: str=state.GLASS_DISTANCE_VD_METRIC, vd_relative_scale: float=state.GLASS_DISTANCE_VD_RELATIVE_SCALE) -> float:
    g_nd = glass.get('nd')
    g_vd = glass.get('vd')
    if g_nd is None or g_vd is None:
        return float('inf')
    nd_scale = max(1e-12, abs(_lensde_configuration.finite_float(nd_scale, state.GLASS_DISTANCE_ND_SCALE)))
    vd_scale = max(1e-12, abs(_lensde_configuration.finite_float(vd_scale, state.GLASS_DISTANCE_VD_SCALE)))
    metric = str(vd_metric or state.GLASS_DISTANCE_VD_METRIC).strip().lower()
    if metric in {'relative_log', 'relative', 'log'}:
        relative_scale = max(1e-12, abs(_lensde_configuration.finite_float(vd_relative_scale, state.GLASS_DISTANCE_VD_RELATIVE_SCALE)))
        vd_component = math.log(vd / _lensde_configuration.finite_float(g_vd, vd)) / relative_scale
    else:
        vd_component = (_lensde_configuration.finite_float(g_vd, 0.0) - vd) / vd_scale
    return math.hypot((_lensde_configuration.finite_float(g_nd, 0.0) - nd) / nd_scale, vd_component)


def nearest_glasses(library: str, nd: float, vd: float, *, limit: int=64, nd_scale: float=state.GLASS_DISTANCE_ND_SCALE, vd_scale: float=state.GLASS_DISTANCE_VD_SCALE, vd_metric: str=state.GLASS_DISTANCE_VD_METRIC, vd_relative_scale: float=state.GLASS_DISTANCE_VD_RELATIVE_SCALE, required_wavelengths_um=None, audit: dict | None=None) -> list[dict]:
    scored = []
    for glass in list_glasses('', library, limit=1000000, required_wavelengths_um=required_wavelengths_um, audit=audit):
        distance = glass_nd_vd_distance(nd, vd, glass, nd_scale=nd_scale, vd_scale=vd_scale, vd_metric=vd_metric, vd_relative_scale=vd_relative_scale)
        if math.isfinite(distance):
            scored.append((distance, glass))
    scored.sort(key=lambda item: item[0])
    return [{**glass, 'distance': distance} for distance, glass in scored[:max(1, int(limit or 1))]]


def nearest_glass(library: str, nd: float, vd: float, *, required_wavelengths_um=None) -> dict | None:
    nearest = nearest_glasses(library, nd, vd, limit=1, required_wavelengths_um=required_wavelengths_um)
    return nearest[0] if nearest else None


def glass_libraries_for_surface(system: dict, glass_search: dict, surface_index: int) -> list[str]:
    default_libraries = glass_search.get('default_libraries') or system.get('glass_libraries') or [system.get('glass_library', 'SCHOTT')]
    surface_cfg = glass_surface_config(glass_search, surface_index)
    libraries = surface_cfg.get('libraries') or default_libraries
    out = split_glass_libraries(libraries)
    return out or ['SCHOTT']


def glass_proximity_constraints_for_system(system: dict, variables: list[dict], *, weight: float, tolerance: float, nd_scale: float, vd_scale: float, candidate_count: int, smooth_temperature: float=0.0, vd_metric: str=state.GLASS_DISTANCE_VD_METRIC, vd_relative_scale: float=state.GLASS_DISTANCE_VD_RELATIVE_SCALE, region_enabled: bool=False, region_weight: float=0.0, region_tolerance: float=0.0) -> list[dict]:
    surfaces = system.get('surfaces', []) or []
    glass_search = system.get('glass_search', {}) if isinstance(system.get('glass_search'), dict) else {}
    required_wavelengths = system_wavelengths_um(system)
    material_indices = {int(variable.get('index', -1)) for variable in variables if variable.get('kind') in {'material_nd', 'material_vd'}}
    constraints: list[dict] = []
    for index in sorted(material_indices):
        if not 0 <= index < len(surfaces):
            continue
        material = surfaces[index].get('material_after', {}) if isinstance(surfaces[index], dict) else {}
        nd, vd = _lensde_provenance.material_nd_vd(material)
        if nd is None or vd is None:
            continue
        libraries = glass_libraries_for_surface(system, glass_search, index)
        candidates = nearest_glasses(','.join(libraries), nd, vd, limit=1000000 if region_enabled else max(1, int(candidate_count or 1)), nd_scale=nd_scale, vd_scale=vd_scale, vd_metric=vd_metric, vd_relative_scale=vd_relative_scale, required_wavelengths_um=required_wavelengths)
        if not candidates:
            raise ValueError(f'No glass points are available for surface {index} from declared libraries {libraries}')
        allowed_libraries = {name.upper() for name in libraries}
        candidate_libraries = sorted({str(glass.get('library') or '').strip().upper() for glass in candidates if str(glass.get('library') or '').strip()})
        unexpected_libraries = [] if 'ALL' in allowed_libraries else sorted((library for library in candidate_libraries if library not in allowed_libraries))
        if unexpected_libraries:
            raise ValueError(f'Glass point cloud for surface {index} escaped declared libraries {libraries}: {unexpected_libraries}')
        points = [{'nd': _lensde_configuration.finite_float(glass.get('nd'), 0.0), 'vd': _lensde_configuration.finite_float(glass.get('vd'), 0.0)} for glass in candidates[:max(1, int(candidate_count or 1))] if glass.get('nd') is not None and glass.get('vd') is not None]
        if points:
            constraint = {'index': index, 'libraries': libraries, 'catalog_libraries': candidate_libraries, 'catalog_candidate_count': len(candidates), 'catalog_membership_verified': bool(candidate_libraries), 'required_wavelengths_um': required_wavelengths, 'wavelength_coverage_verified': True, 'weight': weight, 'tolerance': tolerance, 'nd_scale': nd_scale, 'vd_scale': vd_scale, 'vd_metric': str(vd_metric or state.GLASS_DISTANCE_VD_METRIC), 'vd_relative_scale': max(1e-12, abs(_lensde_configuration.finite_float(vd_relative_scale, state.GLASS_DISTANCE_VD_RELATIVE_SCALE))), 'points': points}
            smooth = max(0.0, _lensde_configuration.finite_float(smooth_temperature, 0.0))
            if smooth > 0.0:
                constraint['smooth_temperature'] = smooth
            if region_enabled:
                all_points = [{'nd': _lensde_configuration.finite_float(glass.get('nd'), 0.0), 'vd': _lensde_configuration.finite_float(glass.get('vd'), 0.0)} for glass in candidates if glass.get('nd') is not None and glass.get('vd') is not None]
                region_points = glass_metric_convex_hull(all_points, nd_scale=nd_scale, vd_scale=vd_scale, vd_metric=vd_metric, vd_relative_scale=vd_relative_scale)
                if region_points:
                    constraint['region_weight'] = max(0.0, _lensde_configuration.finite_float(region_weight, 0.0))
                    constraint['region_tolerance'] = max(0.0, _lensde_configuration.finite_float(region_tolerance, 0.0))
                    constraint['region_points'] = region_points
            constraints.append(constraint)
    return constraints


def glass_metric_convex_hull(points: list[dict], *, nd_scale: float, vd_scale: float, vd_metric: str=state.GLASS_DISTANCE_VD_METRIC, vd_relative_scale: float=state.GLASS_DISTANCE_VD_RELATIVE_SCALE) -> list[dict]:
    """Return the catalog convex hull in the same normalized metric used by scoring."""
    nd_scale = max(1e-12, abs(_lensde_configuration.finite_float(nd_scale, state.GLASS_DISTANCE_ND_SCALE)))
    vd_scale = max(1e-12, abs(_lensde_configuration.finite_float(vd_scale, state.GLASS_DISTANCE_VD_SCALE)))
    vd_relative_scale = max(1e-12, abs(_lensde_configuration.finite_float(vd_relative_scale, state.GLASS_DISTANCE_VD_RELATIVE_SCALE)))
    normalized_metric = str(vd_metric or '').strip().lower().replace('-', '_')
    relative_log = normalized_metric in {'relative_log', 'relative', 'log'}
    unique: dict[tuple[float, float], dict] = {}
    for point in points or []:
        nd = _lensde_configuration.finite_float(point.get('nd'), float('nan'))
        vd = _lensde_configuration.finite_float(point.get('vd'), float('nan'))
        if not math.isfinite(nd) or not math.isfinite(vd) or nd <= 1.0 or (vd <= 0.0):
            continue
        x = nd / nd_scale
        y = math.log(vd) / vd_relative_scale if relative_log else vd / vd_scale
        key = (x, y)
        unique.setdefault(key, {'nd': nd, 'vd': vd})
    ordered = sorted(((x, y, point) for (x, y), point in unique.items()))
    if len(ordered) <= 2:
        return [point for _, _, point in ordered]

    def cross(origin, a, b) -> float:
        return (a[0] - origin[0]) * (b[1] - origin[1]) - (a[1] - origin[1]) * (b[0] - origin[0])
    lower = []
    for item in ordered:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], item) <= 0.0:
            lower.pop()
        lower.append(item)
    upper = []
    for item in reversed(ordered):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], item) <= 0.0:
            upper.pop()
        upper.append(item)
    return [point for _, _, point in lower[:-1] + upper[:-1]]


def merit_with_glass_proximity(merit: dict, system: dict, variables: list[dict], options: dict, *, weight: float, tolerance: float, candidate_count: int | None=None, smooth_temperature: float=0.0, nd_scale: float | None=None, vd_scale: float | None=None, vd_metric: str=state.GLASS_DISTANCE_VD_METRIC, vd_relative_scale: float=state.GLASS_DISTANCE_VD_RELATIVE_SCALE, region_enabled: bool=False, region_weight: float=0.0, region_tolerance: float=0.0) -> dict:
    out = copy.deepcopy(merit)
    resolved_nd_scale = max(1e-12, abs(_lensde_configuration.finite_float(nd_scale if nd_scale is not None else options.get('initial_search_glass_distance_nd_scale'), state.GLASS_DISTANCE_ND_SCALE)))
    resolved_vd_scale = max(1e-12, abs(_lensde_configuration.finite_float(vd_scale if vd_scale is not None else options.get('initial_search_glass_distance_vd_scale'), state.GLASS_DISTANCE_VD_SCALE)))
    if candidate_count is None:
        candidate_count = max(1, int(options.get('initial_search_glass_candidate_count', 64) or 64))
    else:
        candidate_count = max(1, int(candidate_count or 1))
    out['glass_proximity'] = glass_proximity_constraints_for_system(system, variables, weight=weight, tolerance=tolerance, nd_scale=resolved_nd_scale, vd_scale=resolved_vd_scale, candidate_count=candidate_count, smooth_temperature=smooth_temperature, vd_metric=vd_metric, vd_relative_scale=vd_relative_scale, region_enabled=region_enabled, region_weight=region_weight, region_tolerance=region_tolerance)
    return out


def has_continuous_material_variables(variables: list[dict]) -> bool:
    return any((variable.get('kind') in {'material_nd', 'material_vd'} for variable in variables or []))


def apply_continuous_glass_proximity_merit(merit: dict, system: dict, variables: list[dict], options: dict) -> dict:
    if not bool(options.get('initial_search_continuous_glass_penalty_enabled', True)):
        return merit
    if bool(options.get('initial_search_material_stages_enabled', False)):
        return merit
    if not has_continuous_material_variables(variables):
        return merit
    candidate_count = max(1, int(options.get('initial_search_continuous_glass_candidate_count', 256) or 256))
    out = merit_with_glass_proximity(merit, system, variables, options, weight=max(0.0, _lensde_configuration.finite_float(options.get('initial_search_continuous_glass_penalty_weight'), 0.5)), tolerance=max(0.0, _lensde_configuration.finite_float(options.get('initial_search_continuous_glass_penalty_tolerance'), 0.5)), candidate_count=candidate_count, smooth_temperature=max(0.0, _lensde_configuration.finite_float(options.get('initial_search_continuous_glass_smooth_temperature'), 0.25)), vd_metric=str(options.get('initial_search_glass_distance_vd_metric') or state.GLASS_DISTANCE_VD_METRIC), vd_relative_scale=max(1e-12, abs(_lensde_configuration.finite_float(options.get('initial_search_glass_distance_vd_relative_scale'), state.GLASS_DISTANCE_VD_RELATIVE_SCALE))), region_enabled=bool(options.get('initial_search_continuous_glass_region_enabled', False)), region_weight=max(0.0, _lensde_configuration.finite_float(options.get('initial_search_continuous_glass_region_weight'), 0.5)), region_tolerance=max(0.0, _lensde_configuration.finite_float(options.get('initial_search_continuous_glass_region_tolerance'), 0.0)))
    if not out.get('glass_proximity'):
        out.pop('glass_proximity', None)
    return out


def optimizer_mode_uses_local_glass_proximity(mode: str, options: dict) -> bool:
    normalized = str(mode or '').strip().lower().replace('-', '_')
    if normalized in {'de+local', 'de_local_polish'}:
        normalized = 'de_local'
    if normalized == 'de' and bool(options.get('polish', False)):
        normalized = 'de_local'
    return normalized in {'local', 'de_local', 'hybrid', 'hybrid_de_dls', 'adaptive', 'adaptive_hybrid', 'adaptive_hybrid_de_dls'} or _lensde_geometry.is_fine_polish_mode(normalized)


def apply_local_glass_proximity_merit(merit: dict, system: dict, variables: list[dict], options: dict) -> dict:
    if not bool(options.get('local_glass_proximity_enabled', True)):
        out = copy.deepcopy(merit)
        out.pop('glass_proximity', None)
        return out
    if not has_continuous_material_variables(variables):
        return merit
    out = merit_with_glass_proximity(merit, system, variables, options, weight=max(0.0, _lensde_configuration.finite_float(options.get('local_glass_proximity_weight'), 0.5)), tolerance=max(0.0, _lensde_configuration.finite_float(options.get('local_glass_proximity_tolerance'), 1.5)), candidate_count=max(1, int(_lensde_configuration.finite_float(options.get('local_glass_proximity_candidate_count'), 256))), smooth_temperature=max(0.0, _lensde_configuration.finite_float(options.get('local_glass_proximity_smooth_temperature'), 0.25)), nd_scale=max(1e-12, abs(_lensde_configuration.finite_float(options.get('local_glass_proximity_nd_scale'), state.GLASS_DISTANCE_ND_SCALE))), vd_scale=max(1e-12, abs(_lensde_configuration.finite_float(options.get('local_glass_proximity_vd_scale'), state.GLASS_DISTANCE_VD_SCALE))), vd_metric=str(options.get('local_glass_proximity_vd_metric') or state.GLASS_DISTANCE_VD_METRIC), vd_relative_scale=max(1e-12, abs(_lensde_configuration.finite_float(options.get('local_glass_proximity_vd_relative_scale'), state.GLASS_DISTANCE_VD_RELATIVE_SCALE))))
    if not out.get('glass_proximity'):
        out.pop('glass_proximity', None)
    return out


def snap_system_materials_to_nearest_glass(system: dict, variables: list[dict], options: dict) -> dict:
    out = copy.deepcopy(system)
    surfaces = out.get('surfaces', []) or []
    glass_search = out.get('glass_search', {}) if isinstance(out.get('glass_search'), dict) else {}
    required_wavelengths = system_wavelengths_um(out)
    nd_scale = max(1e-12, abs(_lensde_configuration.finite_float(options.get('initial_search_glass_distance_nd_scale'), state.GLASS_DISTANCE_ND_SCALE)))
    vd_scale = max(1e-12, abs(_lensde_configuration.finite_float(options.get('initial_search_glass_distance_vd_scale'), state.GLASS_DISTANCE_VD_SCALE)))
    material_indices = {int(variable.get('index', -1)) for variable in variables if variable.get('kind') in {'material_nd', 'material_vd'}}
    for index, surface in enumerate(surfaces):
        material = surface.get('material_after', {}) if isinstance(surface, dict) else {}
        mode = str(material.get('mode', '')).lower()
        nd, vd = _lensde_provenance.material_nd_vd(material)
        if mode == 'abbe' and nd is not None and (vd is not None) and (nd > 1.0000001):
            material_indices.add(index)
    for index in sorted(material_indices):
        if not 0 <= index < len(surfaces):
            continue
        material = surfaces[index].get('material_after', {}) if isinstance(surfaces[index], dict) else {}
        nd, vd = _lensde_provenance.material_nd_vd(material)
        if nd is None or vd is None:
            continue
        libraries = glass_libraries_for_surface(out, glass_search, index)
        nearest_candidates = nearest_glasses(','.join(libraries), nd, vd, limit=1, nd_scale=nd_scale, vd_scale=vd_scale, required_wavelengths_um=required_wavelengths)
        nearest = nearest_candidates[0] if nearest_candidates else None
        if nearest is None:
            raise ValueError(f'No eligible material for surface {index} from {libraries} covers all system wavelengths {required_wavelengths} um')
        surfaces[index]['material_after'] = {'mode': 'glass', 'name': nearest.get('name', material.get('name', '')), 'reference': nearest.get('reference', nearest.get('library', libraries[0] if libraries else '')), 'nd': _lensde_configuration.finite_float(nearest.get('nd'), nd), 'vd': _lensde_configuration.finite_float(nearest.get('vd'), vd), 'filename': nearest.get('filename', material.get('filename', '')), 'source_path': nearest.get('source_path', material.get('source_path', '')), 'distance': glass_nd_vd_distance(nd, vd, nearest, nd_scale=nd_scale, vd_scale=vd_scale)}
    out['surfaces'] = surfaces
    return out


def enable_glass_search_for_material_surfaces(system: dict, variables: list[dict]) -> dict:
    out = copy.deepcopy(system)
    glass_search = out.setdefault('glass_search', {})
    if not isinstance(glass_search, dict):
        glass_search = {}
        out['glass_search'] = glass_search
    glass_search['enabled'] = True
    default_libraries = glass_search.get('default_libraries') or out.get('glass_libraries') or [out.get('glass_library', 'SCHOTT')]
    glass_search['default_libraries'] = split_glass_libraries(default_libraries) or ['SCHOTT']
    material_indices = {int(variable.get('index', -1)) for variable in variables if variable.get('kind') in {'material_nd', 'material_vd', 'glass_choice'}}
    surfaces = out.get('surfaces', []) or []
    for index, surface in enumerate(surfaces):
        material = surface.get('material_after', {}) if isinstance(surface, dict) else {}
        mode = str(material.get('mode', '')).lower()
        nd, vd = _lensde_provenance.material_nd_vd(material)
        if mode in {'glass', 'abbe'} and nd is not None and (vd is not None) and (nd > 1.0000001):
            material_indices.add(index)
    for index in sorted(material_indices):
        surface_cfg = _lensde_configuration.ensure_glass_surface_config(glass_search, index)
        surface_cfg['enabled'] = True
        surface_cfg['libraries'] = split_glass_libraries(surface_cfg.get('libraries') or glass_search['default_libraries'])
        if not surface_cfg['libraries']:
            surface_cfg['libraries'] = list(glass_search['default_libraries'])
        surface_cfg['max_candidates'] = max(1, int(surface_cfg.get('max_candidates', glass_search.get('max_candidates_per_surface', 64)) or 64))
    return out


def discrete_glass_variables_for_system(system: dict, continuous_variables: list[dict], source_variables: list[dict], options: dict) -> list[dict]:
    geometry_variables = [copy.deepcopy(variable) for variable in continuous_variables if variable.get('kind') not in {'material_nd', 'material_vd', 'glass_choice'}]
    stage_options = {**options, 'initial_search_material_stages_enabled': False}
    variables = _lensde_populations.append_initial_search_live_variables(system, geometry_variables, source_variables, stage_options)
    return variables


def allocate_three_stage_outer(total_outer: int, weak_ratio: float=0.3, proximity_ratio: float=0.3, discrete_ratio: float=0.4) -> tuple[int, int, int]:
    effective_total = max(3, int(total_outer or 0))
    ratios = [max(0.0, _lensde_configuration.finite_float(weak_ratio, 0.3)), max(0.0, _lensde_configuration.finite_float(proximity_ratio, 0.3)), max(0.0, _lensde_configuration.finite_float(discrete_ratio, 0.4))]
    total_ratio = sum(ratios)
    if total_ratio <= 0.0 or not math.isfinite(total_ratio):
        ratios = [0.3, 0.3, 0.4]
        total_ratio = 1.0
    weights = [ratio / total_ratio for ratio in ratios]
    raw = [weight * effective_total for weight in weights]
    counts = [max(1, int(math.floor(value))) for value in raw]
    while sum(counts) > effective_total:
        reducible = [i for i, count in enumerate(counts) if count > 1]
        if not reducible:
            break
        index = min(reducible, key=lambda i: (raw[i] - math.floor(raw[i]), -counts[i], i))
        counts[index] -= 1
    while sum(counts) < effective_total:
        index = max(range(3), key=lambda i: (raw[i] - math.floor(raw[i]), weights[i], i))
        counts[index] += 1
    return (counts[0], counts[1], counts[2])


def initial_material_stage_requirements_ok(system: dict, variables: list[dict]) -> tuple[bool, str]:
    surfaces = system.get('surfaces', []) or []
    material_indices = sorted({int(variable.get('index', -1)) for variable in variables if variable.get('kind') in {'material_nd', 'material_vd'}})
    if not material_indices:
        return (False, '材料三阶段流程需要 material_nd/material_vd 连续材料变量。')
    glass_search = system.get('glass_search', {}) if isinstance(system.get('glass_search'), dict) else {}
    libraries = split_glass_libraries(system.get('glass_libraries') or glass_search.get('default_libraries') or [system.get('glass_library', '')])
    if not libraries:
        return (False, '材料三阶段流程需要配置玻璃库。')
    for index in material_indices:
        if not 0 <= index < len(surfaces):
            continue
        material = surfaces[index].get('material_after', {}) if isinstance(surfaces[index], dict) else {}
        if str(material.get('mode', '')).lower() != 'glass' or not str(material.get('reference', '') or material.get('filename', '')).strip():
            return (False, f'材料三阶段流程要求 S{index} 初始材料是真实玻璃。')
    return (True, '')


def process_text(value) -> str:
    if value is None:
        return ''
    if isinstance(value, bytes):
        return value.decode('utf-8', errors='replace')
    return str(value)


def run_timeout_seconds(project: dict) -> int:
    options = project.get('options', {}) or {}
    for source in (options, project):
        for key in ('cpp_timeout', 'cpp_timeout_seconds', 'timeout_seconds', 'timeout'):
            if key not in source:
                continue
            try:
                return max(0, int(float(source.get(key) or 0)))
            except (TypeError, ValueError):
                continue
    return state.DEFAULT_CPP_TIMEOUT


def promote_checkpoint(run_dir: Path) -> bool:
    promoted = False
    for source_name, target_name in [('checkpoint_system.json', 'system.json'), ('checkpoint_score.json', 'score.json'), ('checkpoint_meta.json', 'checkpoint_meta.json')]:
        source = run_dir / source_name
        target = run_dir / target_name
        if source.exists():
            if source.resolve() != target.resolve():
                shutil.copyfile(source, target)
            promoted = True
    return promoted


def run_file_url(run_dir: Path, file_name: str) -> str | None:
    return f'/api/output?run={run_dir.name}&file={file_name}' if (run_dir / file_name).exists() else None


def collect_run_result(run_dir: Path, run_merit: dict, returncode, stdout: str, stderr: str, *, timed_out: bool=False, timeout_seconds: int | None=None, checkpoint_restored: bool=False) -> dict:
    result = {'returncode': returncode, 'stdout': stdout, 'stderr': stderr, 'run_id': run_dir.name, 'effective_merit': run_merit, 'timed_out': timed_out, 'checkpoint_restored': checkpoint_restored}
    if timeout_seconds is not None:
        result['timeout_seconds'] = timeout_seconds
    for key, file_name in [('layout_url', 'layout.svg'), ('spot_url', 'spot.csv'), ('raytrace_url', 'raytrace.csv'), ('system_url', 'system.json'), ('merit_url', 'merit.json'), ('initial_score_url', 'initial_score.json'), ('score_url', 'score.json'), ('checkpoint_score_url', 'checkpoint_score.json'), ('checkpoint_system_url', 'checkpoint_system.json'), ('checkpoint_meta_url', 'checkpoint_meta.json'), ('run_meta_url', 'run_meta.json'), ('effective_repro_settings_url', 'effective_repro_settings.json'), ('repro_manifest_url', 'repro_manifest.json'), ('population_url', 'population.csv'), ('population_scores_url', 'population_scores.csv'), ('hybrid_trace_url', 'hybrid_trace.jsonl'), ('adaptive_hybrid_trace_url', 'adaptive_hybrid_trace.jsonl'), ('basin_archive_url', 'basin_archive.csv'), ('basin_archive_json_url', 'basin_archive.json'), ('strategy_summary_url', 'strategy_summary.json'), ('candidates_summary_url', 'candidates_summary.json'), ('relative_illumination_url', 'relative_illumination.csv'), ('vignetting_audit_url', 'vignetting_audit.json')]:
        url = run_file_url(run_dir, file_name)
        if url:
            result[key] = url
    if (run_dir / 'initial_score.json').exists():
        result['initial_score'] = _lensde_provenance.read_json(run_dir / 'initial_score.json')
    if (run_dir / 'score.json').exists():
        result['score'] = _lensde_provenance.read_json(run_dir / 'score.json')
    if (run_dir / 'system.json').exists():
        result['system'] = _lensde_provenance.read_json(run_dir / 'system.json')
    if (run_dir / 'checkpoint_meta.json').exists():
        result['checkpoint_meta'] = _lensde_provenance.read_json(run_dir / 'checkpoint_meta.json')
    if (run_dir / 'run_meta.json').exists():
        result['run_meta'] = _lensde_provenance.read_json(run_dir / 'run_meta.json')
    if (run_dir / 'effective_repro_settings.json').exists():
        result['effective_repro_settings'] = _lensde_provenance.read_json(run_dir / 'effective_repro_settings.json')
    if (run_dir / 'strategy_summary.json').exists():
        result['strategy_summary'] = _lensde_provenance.read_json(run_dir / 'strategy_summary.json')
    if (run_dir / 'candidates_summary.json').exists():
        result['candidates_summary'] = _lensde_provenance.read_json(run_dir / 'candidates_summary.json')
    if (run_dir / 'basin_archive.json').exists():
        result['basin_archive'] = _lensde_provenance.read_json(run_dir / 'basin_archive.json')
    vignetting_audit = _lensde_commands.build_vignetting_audit(run_dir, run_merit)
    if vignetting_audit is not None:
        result['vignetting_audit'] = vignetting_audit
        result['vignetting_audit_url'] = run_file_url(run_dir, 'vignetting_audit.json')
        result['relative_illumination_url'] = run_file_url(run_dir, 'relative_illumination.csv')
    return result
