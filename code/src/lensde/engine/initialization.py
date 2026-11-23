"""Extracted paper engine: initialization."""
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
from . import materials as _lensde_materials
from . import orchestration as _lensde_orchestration
from . import populations as _lensde_populations
from . import provenance as _lensde_provenance
from . import tracing as _lensde_tracing

def inferred_staged_asphere_initial_mode(options: dict) -> str:
    requested = _lensde_commands.parse_one_based_index_list(options.get('initial_search_asphere_lens_indices'))
    if requested:
        order = _lensde_commands.canonical_initial_search_asphere_max_order(options.get('initial_search_asphere_max_order', 16))
        return f'conic_a{order}'
    return 'conic' if _lensde_commands.option_truthy(options.get('initial_search_include_conic'), False) else 'spherical'


def canonical_staged_asphere_initial_mode(options: dict) -> str:
    raw = str(options.get('initial_search_staged_asphere_initial_mode') or '').strip().lower()
    raw = raw.replace('-', '_').replace('+', '_')
    if raw in {'spherical', 'conic', *state.STAGED_ASPHERE_INITIAL_MODE_ORDERS}:
        return raw
    return inferred_staged_asphere_initial_mode(options)


def effective_initial_search_asphere_lens_indices(options: dict) -> list[int]:
    """Resolve staged fallback lenses without mutating the explicit GUI field."""
    explicit = _lensde_commands.parse_one_based_index_list(options.get('initial_search_asphere_lens_indices'))
    staged_enabled = _lensde_commands.option_truthy(options.get('initial_search_de_dls_fixed_glass_feedback_enabled'), True) and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_enabled'), False)
    if not staged_enabled:
        return explicit
    mode = canonical_staged_asphere_initial_mode(options)
    if mode not in state.STAGED_ASPHERE_INITIAL_MODE_ORDERS:
        return []
    return explicit or _lensde_commands.parse_one_based_index_list(options.get('initial_search_post_asphere_lens_indices'))


def apply_staged_asphere_initial_mode(options: dict) -> dict:
    """Translate the staged UI choice into the existing optimizer options.

    No new optimizer-side variable kind is introduced: A4/A6/... continue to
    use the established initial-search asphere preparation and automatically
    flow into the DE archive, two-layer DLS, material funnel, and fixed-glass
    feedback.  Older projects without the staged-mode key are inferred from
    their existing conic/asphere settings and therefore keep their behavior.
    """
    staged_enabled = _lensde_commands.option_truthy(options.get('initial_search_de_dls_fixed_glass_feedback_enabled'), True) and _lensde_commands.option_truthy(options.get('initial_search_post_asphere_enabled'), False)
    mode = canonical_staged_asphere_initial_mode(options) if staged_enabled else inferred_staged_asphere_initial_mode(options)
    options['initial_search_staged_asphere_initial_mode'] = mode
    if not staged_enabled:
        return options
    if mode in {'spherical', 'conic'}:
        options['initial_search_include_conic'] = mode == 'conic'
        options['initial_search_include_asphere'] = False
        return options
    requested = effective_initial_search_asphere_lens_indices(options)
    options['initial_search_asphere_max_order'] = state.STAGED_ASPHERE_INITIAL_MODE_ORDERS[mode]
    options['initial_search_include_conic'] = True
    options['initial_search_include_asphere'] = bool(requested)
    return options


def migrate_legacy_initial_asphere_material_options(options: dict) -> dict:
    """Move retired initial-DE plastic settings onto the single material policy.

    ``initial_search_asphere_enabled`` was an internal mirror of the selected
    initial variables, while the two initial plastic keys duplicated the
    visible post-asphere material setting.  The public policy now lives only
    on ``initial_search_post_asphere_plastic_*`` and applies from the first
    continuous-material stage through the final discrete glass stage.
    """
    legacy_enabled = None
    for key in ('initial_search_asphere_plastic', 'initial_search_asphere_plastic_enabled'):
        if key in options:
            legacy_enabled = _lensde_commands.option_truthy(options.get(key), False)
            break
    if 'initial_search_post_asphere_plastic_enabled' not in options and legacy_enabled is not None:
        options['initial_search_post_asphere_plastic_enabled'] = legacy_enabled
    legacy_library = str(options.get('initial_search_asphere_plastic_library') or '').strip()
    if 'initial_search_post_asphere_plastic_library' not in options and legacy_library:
        options['initial_search_post_asphere_plastic_library'] = legacy_library
    for key in ('initial_search_asphere_enabled', 'initial_search_asphere_plastic', 'initial_search_asphere_plastic_enabled', 'initial_search_asphere_plastic_library'):
        options.pop(key, None)
    return options


def initial_search_asphere_variables_enabled(options: dict) -> bool:
    """Return the derived initial-DE asphere state without persisting a mirror."""
    return bool(options.get('initial_search_include_asphere', False)) and bool(effective_initial_search_asphere_lens_indices(options))


def normalize_initial_search_asphere_options(options: dict) -> dict:
    """Canonicalize the two public full-asphere initial-search inputs.

    The explicit initial-DE lens field remains independent from the staged
    post-asphere field. Staged A4+ modes may resolve the post-stage lenses at
    runtime without persisting them into that explicit field.
    """
    migrate_legacy_initial_asphere_material_options(options)
    apply_staged_asphere_initial_mode(options)
    explicit_requested = _lensde_commands.parse_one_based_index_list(options.get('initial_search_asphere_lens_indices'))
    options['initial_search_asphere_lens_indices'] = ','.join((str(index) for index in explicit_requested))
    requested = effective_initial_search_asphere_lens_indices(options)
    options['initial_search_post_asphere_plastic_enabled'] = _lensde_commands.option_truthy(options.get('initial_search_post_asphere_plastic_enabled'), False)
    library = str(options.get('initial_search_post_asphere_plastic_library') or 'PLASTIC2026').strip()
    options['initial_search_post_asphere_plastic_library'] = library or 'PLASTIC2026'
    raw_max_order = options.get('initial_search_asphere_max_order', options.get('initial_search_asphere_max_coefficient', 16))
    options['initial_search_asphere_max_order'] = _lensde_commands.canonical_initial_search_asphere_max_order(raw_max_order)
    options['initial_search_asphere_max_coefficient'] = options['initial_search_asphere_max_order']
    options['initial_search_include_asphere'] = bool(requested)
    if requested:
        options['initial_search_include_conic'] = True
    options.setdefault('initial_search_asphere_scout_enabled', True)
    options['initial_search_asphere_scout_enabled'] = _lensde_commands.option_truthy(options.get('initial_search_asphere_scout_enabled'), True)
    options['initial_search_asphere_scout_allow_fallback'] = _lensde_commands.option_truthy(options.get('initial_search_asphere_scout_allow_fallback'), False)
    options['initial_search_asphere_scout_samples'] = _lensde_commands.option_int_from(options, 'initial_search_asphere_scout_samples', 24, 0)
    options['initial_search_asphere_scout_timeout'] = _lensde_commands.option_int_from(options, 'initial_search_asphere_scout_timeout', 30, 1)
    options['initial_search_asphere_scout_min_trace_coverage'] = min(1.0, _lensde_commands.option_float_from(options, 'initial_search_asphere_scout_min_trace_coverage', 0.95, 0.0))
    options['initial_search_asphere_profile_scale'] = _lensde_commands.option_float_from(options, 'initial_search_asphere_profile_scale', 1.0, 0.0)
    return options


def initial_search_asphere_post_options(options: dict) -> dict:
    """Map public initial-search inputs onto the established asphere helpers."""
    normalized = normalize_initial_search_asphere_options(copy.deepcopy(options))
    return {**normalized, 'initial_search_post_asphere_lens_indices': ','.join((str(index) for index in effective_initial_search_asphere_lens_indices(normalized))), 'initial_search_post_asphere_max_order': normalized.get('initial_search_asphere_max_order', 16), 'initial_search_post_asphere_bound_mode': 'adaptive_sag'}


def lens_groups_for_system(system: dict) -> list[dict]:
    surfaces = system.get('surfaces', []) or []
    groups: list[dict] = []
    for index, surface in enumerate(surfaces):
        if not isinstance(surface, dict):
            continue
        kind = str(surface.get('kind', '')).lower()
        if kind in {'object', 'image'}:
            continue
        material = surface.get('material_after', {}) if isinstance(surface.get('material_after'), dict) else {}
        if str(material.get('mode', '')).lower() not in {'glass', 'abbe'}:
            continue
        nd = _lensde_configuration.finite_float(material.get('nd'), 1.0)
        if nd <= 1.0005:
            continue
        surface_indices = [index]
        exit_index = index + 1
        if 0 <= exit_index < len(surfaces):
            exit_kind = str((surfaces[exit_index] or {}).get('kind', '')).lower()
            if exit_kind not in {'object', 'image'}:
                surface_indices.append(exit_index)
        groups.append({'lens_index': len(groups) + 1, 'material_surface_index': index, 'surface_indices': surface_indices, 'glass_name': material.get('name'), 'glass_reference': material.get('reference') or material.get('library')})
    return groups


def selected_post_asphere_lens_groups(system: dict, options: dict) -> list[dict]:
    requested = set(_lensde_commands.parse_one_based_index_list(options.get('initial_search_post_asphere_lens_indices')))
    if not requested:
        return []
    return [group for group in lens_groups_for_system(system) if int(group.get('lens_index', 0) or 0) in requested]


def selected_post_asphere_surface_indices(system: dict, options: dict) -> list[int]:
    surfaces = system.get('surfaces', []) or []
    out: list[int] = []
    seen: set[int] = set()
    for group in selected_post_asphere_lens_groups(system, options):
        for index in group.get('surface_indices', []) or []:
            try:
                surface_index = int(index)
            except (TypeError, ValueError):
                continue
            if not 0 <= surface_index < len(surfaces) or surface_index in seen:
                continue
            kind = str((surfaces[surface_index] or {}).get('kind', '')).lower()
            if kind in {'object', 'image'}:
                continue
            out.append(surface_index)
            seen.add(surface_index)
    return out


def selected_post_asphere_material_surface_indices(system: dict, options: dict) -> list[int]:
    out: list[int] = []
    for group in selected_post_asphere_lens_groups(system, options):
        try:
            out.append(int(group.get('material_surface_index', -1)))
        except (TypeError, ValueError):
            continue
    return sorted({index for index in out if index >= 0})


def apply_post_asphere_plastic_glass_search(system: dict, options: dict) -> dict:
    if not _lensde_commands.option_truthy(options.get('initial_search_post_asphere_plastic_enabled'), False):
        return system
    library = str(options.get('initial_search_post_asphere_plastic_library') or 'PLASTIC2026').strip()
    if not library:
        library = 'PLASTIC2026'
    material_indices = selected_post_asphere_material_surface_indices(system, options)
    if not material_indices:
        return system
    out = copy.deepcopy(system)
    primary_library = str(out.get('glass_library') or '').strip()
    configured_libraries = _lensde_materials.split_glass_libraries(out.get('glass_libraries'))
    if not configured_libraries and primary_library:
        configured_libraries = [primary_library]
    if not primary_library:
        out['glass_library'] = configured_libraries[0] if configured_libraries else library
    libraries = list(configured_libraries)
    if library.upper() not in {item.upper() for item in libraries}:
        libraries.insert(0, library)
    out['glass_libraries'] = libraries or [library]
    glass_search = out.setdefault('glass_search', {})
    if not isinstance(glass_search, dict):
        glass_search = {}
        out['glass_search'] = glass_search
    glass_search['enabled'] = False
    default_libraries = _lensde_materials.split_glass_libraries(glass_search.get('default_libraries'))
    if not default_libraries:
        default_libraries = list(configured_libraries) or [str(out.get('glass_library') or library)]
    glass_search['default_libraries'] = default_libraries
    max_candidates = max(1, int(_lensde_configuration.finite_float(glass_search.get('max_candidates_per_surface'), 64)))
    glass_search['max_candidates_per_surface'] = max_candidates
    selected_material_indices = set(material_indices)
    for surface_cfg in glass_search.get('surface_configs', []):
        if not isinstance(surface_cfg, dict):
            continue
        try:
            surface_index = int(surface_cfg.get('surface_index', -1))
        except (TypeError, ValueError):
            continue
        surface_cfg['enabled'] = surface_index in selected_material_indices
    for surface_index in material_indices:
        surface_cfg = _lensde_configuration.ensure_glass_surface_config(glass_search, surface_index)
        surface_cfg['enabled'] = True
        surface_cfg['libraries'] = [library]
        surface_cfg['strict_libraries'] = True
        surface_cfg['max_candidates'] = max(1, int(surface_cfg.get('max_candidates', max_candidates) or max_candidates))
    out['post_asphere_plastic_glass_search'] = {'enabled': True, 'library': library, 'material_surface_indices': material_indices, 'lens_indices': _lensde_commands.parse_one_based_index_list(options.get('initial_search_post_asphere_lens_indices'))}
    return out


def post_asphere_max_order(options: dict) -> int:
    return _lensde_populations.canonical_post_asphere_max_order(options.get('initial_search_post_asphere_max_order', 16))


def post_asphere_variable_bound(system: dict, variables: list[dict], options: dict, surface_index: int, term: int, current: float=0.0) -> float:
    order = _lensde_populations.asphere_order_for_term(term)
    key = f'initial_search_post_asphere_a{int(order)}_abs_max'
    default_by_order = {4: 0.001, 6: 1e-05, 8: 1e-07, 10: 1e-09, 12: 1e-11, 14: 1e-13, 16: 1e-15}
    configured = _lensde_commands.option_float_from(options, key, default_by_order.get(int(order), 1e-12), 0.0)
    mode = str(options.get('initial_search_post_asphere_bound_mode') or 'adaptive_sag').strip().lower()
    if mode == 'absolute':
        return max(configured, abs(_lensde_configuration.finite_float(current, 0.0)) * 1.2, 1e-20)
    adaptive = _lensde_populations.asphere_bound_span_for_initial_search(system, variables, current, surface_index, term)
    return max(adaptive, abs(_lensde_configuration.finite_float(current, 0.0)) * 1.2, 1e-20)


def append_post_asphere_variables(system: dict, source_variables: list[dict], options: dict) -> list[dict]:
    out = _lensde_commands.sync_variables_from_system([copy.deepcopy(variable) for variable in source_variables], system)
    keys = {_lensde_geometry.variable_key(variable) for variable in out}
    max_order = post_asphere_max_order(options)
    terms = list(range(1, _lensde_populations.asphere_term_for_order(max_order) + 1))
    selected_surfaces = selected_post_asphere_surface_indices(system, options)
    conic_abs = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_conic_abs_max', 100.0, 0.0)

    def add_variable(kind: str, index: int, term: int, initial: float, lower: float, upper: float, role: str) -> None:
        key = (kind, index, term)
        if key in keys:
            for item in out:
                if _lensde_geometry.variable_key(item) != key:
                    continue
                item['initial'] = _lensde_populations.clamp_value(initial, _lensde_configuration.finite_float(item.get('lower'), lower), _lensde_configuration.finite_float(item.get('upper'), upper))
                if kind in {'conic', 'asphere_coeff'}:
                    item['lower'] = lower
                    item['upper'] = upper
                    item['initial'] = _lensde_populations.clamp_value(initial, item['lower'], item['upper'])
                break
            return
        item = {'kind': kind, 'index': index, 'term': term, 'lower': lower, 'upper': upper, 'initial': _lensde_populations.clamp_value(initial, lower, upper), 'role': role}
        out.append(item)
        keys.add(key)
    surfaces = system.get('surfaces', []) or []
    for index in selected_surfaces:
        if not 0 <= index < len(surfaces):
            continue
        surface = surfaces[index]
        if not isinstance(surface, dict):
            continue
        surface['kind'] = 'even_asphere'
        conic = _lensde_configuration.finite_float(surface.get('conic'), 0.0)
        conic_bound = max(conic_abs, abs(conic) * 1.2, 1e-09)
        add_variable('conic', index, 0, conic, -conic_bound, conic_bound, f'post-asphere conic @ S{index}')
        coeffs = surface.setdefault('asphere_coefficients', [])
        if not isinstance(coeffs, list):
            coeffs = []
            surface['asphere_coefficients'] = coeffs
        max_term = max(terms) if terms else 0
        while len(coeffs) <= max_term:
            coeffs.append(0.0)
        for term in terms:
            current = _lensde_configuration.finite_float(coeffs[term], 0.0)
            order = _lensde_populations.asphere_order_for_term(term)
            bound = post_asphere_variable_bound(system, out, options, index, term, current)
            add_variable('asphere_coeff', index, term, current, -bound, bound, f'post-asphere A{order} @ S{index}')
    synced = _lensde_commands.sync_variables_from_system(out, system, rebase_asphere=True)
    return _lensde_configuration.ensure_vignetting_variables_for_system(system, synced, options)


def sync_post_asphere_variables_for_system(system: dict, variables: list[dict], merit: dict | None, *, rebase_asphere: bool=False) -> list[dict]:
    """Sync a post-asphere contract after its optimizer fields change.

    Fixed-contract post-asphere preparation first creates variables for the
    seven main fields and then appends three immutable edge guards.  Rebuild
    the internal FVCY/FVDY variables before syncing so every non-central
    optimizer field keeps pupil freedom and physical matching, independently
    of the subset selected for the relative-illumination floor.
    """
    expanded = _lensde_configuration.ensure_vignetting_variables_for_system(system, variables, merit, handoff=True)
    return _lensde_commands.sync_variables_from_system(expanded, system, rebase_asphere=rebase_asphere)


def selected_initial_search_asphere_lens_groups(system: dict, options: dict) -> list[dict]:
    return selected_post_asphere_lens_groups(system, initial_search_asphere_post_options(options))


def apply_initial_search_post_asphere_material_policy(system: dict, options: dict) -> dict:
    """Apply the selected plastic material policy before initial DE begins.

    The selected post-asphere lens groups are also the material contract for
    continuous nd/Vd optimization.  This intentionally does not depend on
    whether A4+ variables are opened at generation zero, so a spherical or
    conic initial stage cannot silently fall back to the default glass cloud.
    """
    normalized = normalize_initial_search_asphere_options(copy.deepcopy(options))
    return apply_post_asphere_plastic_glass_search(system, normalized)


def prepare_initial_search_asphere_system_and_variables(system: dict, source_variables: list[dict], options: dict) -> tuple[dict, list[dict]]:
    """Add user-selected double-sided conic and A4..Amax variables from gen 0."""
    normalized = normalize_initial_search_asphere_options(copy.deepcopy(options))
    if not initial_search_asphere_variables_enabled(normalized):
        return (copy.deepcopy(system), [copy.deepcopy(variable) for variable in source_variables])
    requested = effective_initial_search_asphere_lens_indices(normalized)
    groups = lens_groups_for_system(system)
    available = {int(group.get('lens_index', 0) or 0) for group in groups}
    missing = [index for index in requested if index not in available]
    if missing:
        raise ValueError('initial-search asphere lens indices are unavailable: ' + ','.join((str(index) for index in missing)) + '; available lens indices: ' + (','.join((str(index) for index in sorted(available))) or 'none'))
    out_system = copy.deepcopy(system)
    selected_groups = selected_initial_search_asphere_lens_groups(out_system, normalized)
    surface_to_lens: dict[int, int] = {}
    for group in selected_groups:
        lens_index = int(group.get('lens_index', 0) or 0)
        for surface_index in group.get('surface_indices', []) or []:
            surface_to_lens[int(surface_index)] = lens_index
    selected_surfaces = set(surface_to_lens)
    max_term = _lensde_populations.asphere_term_for_order(normalized['initial_search_asphere_max_order'])
    surfaces = out_system.get('surfaces', []) or []
    for surface_index in sorted(selected_surfaces):
        if not 0 <= surface_index < len(surfaces):
            continue
        surface = surfaces[surface_index]
        surface['kind'] = 'even_asphere'
        surface['conic'] = 0.0
        surface['asphere_coefficients'] = [0.0] * (max_term + 1)
    clean_variables = [copy.deepcopy(variable) for variable in source_variables if str(variable.get('kind', '')).lower() not in {'conic', 'asphere_coeff'}]
    out_variables = append_post_asphere_variables(out_system, clean_variables, initial_search_asphere_post_options(normalized))
    for variable in out_variables:
        kind = str(variable.get('kind', '')).lower()
        surface_index = int(variable.get('index', -1) or -1)
        if kind not in {'conic', 'asphere_coeff'} or surface_index not in selected_surfaces:
            continue
        variable['initial_search_asphere_profiled'] = True
        variable['initial_search_asphere_lens_index'] = surface_to_lens[surface_index]
        variable['initial_search_asphere_profile_scale'] = normalized.get('initial_search_asphere_profile_scale', 1.0)
        if kind == 'asphere_coeff':
            variable['role'] = f"initial-search asphere A{_lensde_populations.asphere_order_for_term(int(variable.get('term', 0) or 0))} @ S{surface_index}"
        else:
            variable['role'] = f'initial-search conic @ S{surface_index}'
    return (out_system, out_variables)


def apply_initial_search_asphere_reference_radii(variables: list[dict], reference_radii: dict[int, float]) -> int:
    """Rebase physical A bounds while retaining the existing normalized q bounds."""
    updated = 0
    for variable in variables:
        if str(variable.get('kind', '')).lower() != 'asphere_coeff' or not bool(variable.get('initial_search_asphere_profiled', False)):
            continue
        surface_index = int(variable.get('index', -1) or -1)
        radius = _lensde_configuration.finite_float(reference_radii.get(surface_index), 0.0)
        if radius <= 0.0:
            continue
        term = max(0, int(variable.get('term', 0) or 0))
        order = _lensde_populations.asphere_order_for_term(term)
        radial_power = radius ** order
        if not math.isfinite(radial_power) or radial_power <= 0.0:
            continue
        old_radius = max(_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), radius), 1e-12)
        old_power = old_radius ** order
        lower, upper = _lensde_populations.variable_bounds(variable)
        q_lower = _lensde_configuration.finite_float(variable.get('edge_sag_lower'), lower * old_power)
        q_upper = _lensde_configuration.finite_float(variable.get('edge_sag_upper'), upper * old_power)
        if q_lower > q_upper:
            q_lower, q_upper = (q_upper, q_lower)
        current_q = _lensde_configuration.finite_float(variable.get('initial'), 0.0) * old_power
        variable['asphere_parameterization'] = 'edge_sag'
        variable['asphere_reference_radius'] = radius
        variable['edge_sag_lower'] = q_lower
        variable['edge_sag_upper'] = q_upper
        variable['lower'] = q_lower / radial_power
        variable['upper'] = q_upper / radial_power
        variable['initial'] = _lensde_populations.clamp_value(current_q / radial_power, variable['lower'], variable['upper'])
        updated += 1
    return updated


def initial_asphere_traced_clear_radius(surface: dict) -> float:
    aperture = surface.get('aperture') if isinstance(surface.get('aperture'), dict) else {}
    for value in (surface.get('semi_aperture'), surface.get('aperture_radius'), aperture.get('r_max')):
        radius = abs(_lensde_configuration.finite_float(value, 0.0))
        if math.isfinite(radius) and radius > 0.0:
            return radius
    return 0.0


def run_initial_search_asphere_geometry_scout(system: dict, variables: list[dict], merit: dict, options: dict, out_dir: Path, *, seed: int, target_efl: float, target_ttl: float, target_bfl: float, rest_cap: float, curvature_cap: float) -> tuple[dict[int, float], dict]:
    """Estimate one shared q reference radius per selected surface before DE."""
    selected_surfaces = sorted({int(variable.get('index', -1) or -1) for variable in variables if bool(variable.get('initial_search_asphere_profiled', False)) and str(variable.get('kind', '')).lower() == 'asphere_coeff'})
    fallback = {surface_index: max([_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), 0.0) for variable in variables if str(variable.get('kind', '')).lower() == 'asphere_coeff' and int(variable.get('index', -1) or -1) == surface_index and (_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), 0.0) > 0.0)] or [_lensde_geometry.asphere_stage_reference_radius(system, surface_index)]) for surface_index in selected_surfaces}
    diagnostics = {'enabled': _lensde_commands.option_truthy(options.get('initial_search_asphere_scout_enabled'), True), 'requested_samples': _lensde_commands.option_int_from(options, 'initial_search_asphere_scout_samples', 24, 0), 'traceable_samples': 0, 'selected_surface_indices': selected_surfaces, 'fallback_reference_radii_mm': {str(key): value for key, value in fallback.items()}, 'reference_radii_mm': {}, 'reference_source': {}, 'radius_samples_mm': {str(index): [] for index in selected_surfaces}, 'status': 'pending', 'samples': []}
    if not selected_surfaces:
        diagnostics['status'] = 'not_applicable'
        return (fallback, diagnostics)
    if not diagnostics['enabled'] or diagnostics['requested_samples'] <= 0:
        diagnostics['status'] = 'disabled_fallback'
        diagnostics['reference_radii_mm'] = {str(key): value for key, value in fallback.items()}
        return (fallback, diagnostics)
    cpu_exe = _lensde_provenance.exe_path(False)
    if not cpu_exe.exists():
        diagnostics['status'] = 'cpu_missing_fallback'
        diagnostics['reference_radii_mm'] = {str(key): value for key, value in fallback.items()}
        return (fallback, diagnostics)
    root = out_dir / 'initial_asphere_aperture_scout'
    root.mkdir(parents=True, exist_ok=True)
    merit_path = root / 'merit.json'
    _lensde_provenance.write_json(merit_path, merit)
    geometry_variables = [copy.deepcopy(variable) for variable in variables if str(variable.get('kind', '')).lower() not in {'conic', 'asphere_coeff'}]
    sample_count = min(64, int(diagnostics['requested_samples']))
    rows = _lensde_populations.make_hybrid_initial_seed_population(system, geometry_variables, [], sample_count + 1, seed + 4701, target_ttl, target_bfl, rest_cap, curvature_cap, population_generation_mode='random', local_group_perturbation=False, perturb_material_variables=True, perturb_other_variables=False, target_efl=target_efl)[1:sample_count + 1]
    radius_samples: dict[int, list[float]] = {index: [] for index in selected_surfaces}
    minimum_coverage = min(1.0, _lensde_commands.option_float_from(options, 'initial_search_asphere_scout_min_trace_coverage', 0.95, 0.0))
    timeout = _lensde_commands.option_int_from(options, 'initial_search_asphere_scout_timeout', 30, 1)
    for sample_index, row in enumerate(rows):
        sample_dir = root / f'sample_{sample_index:03d}'
        sample_dir.mkdir(parents=True, exist_ok=True)
        candidate = _lensde_materials.apply_variable_values_to_system(system, geometry_variables, row)
        surfaces = candidate.get('surfaces', []) or []
        for surface_index in selected_surfaces:
            if 0 <= surface_index < len(surfaces):
                surfaces[surface_index]['conic'] = 0.0
                coeffs = surfaces[surface_index].get('asphere_coefficients', []) or []
                surfaces[surface_index]['asphere_coefficients'] = [0.0 for _ in coeffs]
        input_path = sample_dir / 'input_system.json'
        _lensde_provenance.write_json(input_path, candidate)
        command = [str(cpu_exe), '--system', str(input_path), '--merit', str(merit_path), '--aiming', _lensde_configuration.canonical_aiming_mode(options.get('aiming'), 'robust'), '--aiming-iter', str(_lensde_commands.option_int_from(options, 'aiming_iter', 20, 1)), '--trace-policy', 'soft_coverage', '--rays', str(max(1, _lensde_commands.option_int_from(options, 'initial_search_trace_rays', 3, 1))), '--arms', str(max(1, _lensde_commands.option_int_from(options, 'initial_search_trace_arms', 6, 1))), '--distribution', str(options.get('initial_search_trace_distribution', 'gaussian_ring_arm')), '--sampling-seed', '1', '--clear-aperture-update', '--sync-stop-aperture', '--score-only', '--cpu', '--out-dir', str(sample_dir)]
        _lensde_materials.append_aiming_policy_args(command, options)
        record = {'sample': sample_index, 'returncode': None, 'traceable': False, 'radii_mm': {}}
        try:
            completed = subprocess.run(command, cwd=str(state.CPP_ROOT), capture_output=True, text=True, timeout=timeout)
            record['returncode'] = completed.returncode
            (sample_dir / 'stdout.log').write_text(completed.stdout or '', encoding='utf-8', errors='replace')
            (sample_dir / 'stderr.log').write_text(completed.stderr or '', encoding='utf-8', errors='replace')
        except (OSError, subprocess.TimeoutExpired) as exc:
            record['error'] = str(exc)
        if record['returncode'] == 0:
            try:
                score = _lensde_provenance.read_json(sample_dir / 'score.json')
            except (OSError, ValueError, TypeError):
                score = {}
            coverage = _lensde_configuration.finite_float(score.get('trace_coverage'), 0.0)
            min_field = _lensde_configuration.finite_float(score.get('min_field_coverage'), 0.0)
            record['trace_coverage'] = coverage
            record['min_field_coverage'] = min_field
            record['traceable'] = bool(score.get('trace_ok', False)) and coverage >= minimum_coverage and (min_field >= minimum_coverage)
            if record['traceable']:
                try:
                    traced = _lensde_provenance.read_json(sample_dir / 'system.json')
                except (OSError, ValueError, TypeError):
                    traced = {}
                traced_surfaces = traced.get('surfaces', []) if isinstance(traced, dict) else []
                for surface_index in selected_surfaces:
                    surface = traced_surfaces[surface_index] if 0 <= surface_index < len(traced_surfaces) else {}
                    radius = initial_asphere_traced_clear_radius(surface if isinstance(surface, dict) else {})
                    if radius > 0.0:
                        radius_samples[surface_index].append(radius)
                        record['radii_mm'][str(surface_index)] = radius
                diagnostics['traceable_samples'] += 1
        diagnostics['samples'].append(record)
    selected = dict(fallback)
    for surface_index, values in radius_samples.items():
        diagnostics['radius_samples_mm'][str(surface_index)] = list(values)
        if values:
            ordered = sorted(values)
            middle = len(ordered) // 2
            selected[surface_index] = ordered[middle] if len(ordered) % 2 else 0.5 * (ordered[middle - 1] + ordered[middle])
    missing_surfaces = [index for index in selected_surfaces if not radius_samples[index]]
    if diagnostics['traceable_samples'] <= 0:
        diagnostics['status'] = 'no_traceable_samples_fallback'
    elif missing_surfaces:
        diagnostics['status'] = 'partial_surface_fallback'
        diagnostics['missing_surface_indices'] = missing_surfaces
    else:
        diagnostics['status'] = 'ok'
    diagnostics['reference_radii_mm'] = {str(key): value for key, value in selected.items()}
    diagnostics['reference_source'] = {str(index): 'geometry_scout_median' if radius_samples[index] else 'flat_input_fallback' for index in selected_surfaces}
    diagnostics['updated_asphere_variables'] = apply_initial_search_asphere_reference_radii(variables, selected)
    return (selected, diagnostics)


def initial_asphere_conic_sag(curvature: float, conic: float, radius: float) -> float:
    curvature = _lensde_configuration.finite_float(curvature, 0.0)
    radius = abs(_lensde_configuration.finite_float(radius, 0.0))
    if abs(curvature) <= 1e-16 or radius <= 0.0:
        return 0.0
    radicand = 1.0 - (1.0 + conic) * curvature * curvature * radius * radius
    if radicand <= 1e-12:
        return math.inf
    denominator = 1.0 + math.sqrt(radicand)
    return curvature * radius * radius / denominator if denominator > 1e-15 else math.inf


def project_initial_asphere_conic(curvature: float, conic: float, edge_radius: float, max_departure: float) -> float:
    if conic == 0.0 or max_departure <= 0.0:
        return 0.0
    sphere_sag = initial_asphere_conic_sag(curvature, 0.0, edge_radius)
    if not math.isfinite(sphere_sag):
        return 0.0

    def valid(scale: float) -> bool:
        sag = initial_asphere_conic_sag(curvature, conic * scale, edge_radius)
        return math.isfinite(sag) and abs(sag - sphere_sag) <= max_departure
    if valid(1.0):
        return conic
    lower, upper = (0.0, 1.0)
    for _ in range(48):
        midpoint = 0.5 * (lower + upper)
        if valid(midpoint):
            lower = midpoint
        else:
            upper = midpoint
    return conic * lower


def initial_asphere_polynomial_envelope(coefficients: dict[int, float], edge_radius: float) -> tuple[float, float]:
    max_sag = 0.0
    max_slope = 0.0
    for radial_index in range(17):
        radius = edge_radius * radial_index / 16.0
        sag = 0.0
        slope = 0.0
        for term, coefficient in coefficients.items():
            order = _lensde_populations.asphere_order_for_term(term)
            sag += coefficient * radius ** order
            if radius > 0.0:
                slope += order * coefficient * radius ** (order - 1)
        max_sag = max(max_sag, abs(sag))
        max_slope = max(max_slope, abs(slope))
    return (max_sag, max_slope)


def sample_initial_asphere_surface_coefficients(variables_by_term: dict[int, tuple[int, dict]], active_terms: list[int], rho: float, rng: random.Random, edge_radius: float, scale: float) -> dict[int, float]:
    if not active_terms or rho <= 0.0:
        return {}
    direction = {term: rng.uniform(-1.0, 1.0) for term in active_terms}
    norm = sum((abs(value) for value in direction.values()))
    if norm <= 1e-15:
        direction[active_terms[0]] = 1.0
        norm = 1.0
    effective_rho = max(0.0, scale) * rho
    radial_scale = effective_rho * rng.uniform(0.45, 1.0)
    coefficients: dict[int, float] = {}
    for term, value in direction.items():
        _column, variable = variables_by_term[term]
        reference_radius = max(_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), edge_radius), 1e-12)
        order = _lensde_populations.asphere_order_for_term(term)
        radial_power = reference_radius ** order
        lower, upper = _lensde_populations.variable_bounds(variable)
        sag_bound = max(abs(_lensde_configuration.finite_float(variable.get('edge_sag_lower'), lower * radial_power)), abs(_lensde_configuration.finite_float(variable.get('edge_sag_upper'), upper * radial_power)))
        coefficients[term] = sag_bound * radial_scale * value / norm / radial_power
    max_sag, max_slope = initial_asphere_polynomial_envelope(coefficients, edge_radius)
    target_sag = 1.5 * effective_rho
    target_slope = 16.0 * target_sag / max(edge_radius, 1e-09)
    envelope_scale = 1.0
    if max_sag > target_sag > 0.0:
        envelope_scale = min(envelope_scale, target_sag / max_sag)
    if max_slope > target_slope > 0.0:
        envelope_scale = min(envelope_scale, target_slope / max_slope)
    if envelope_scale < 1.0:
        coefficients = {term: value * envelope_scale for term, value in coefficients.items()}
    return coefficients


def _initial_asphere_profile_key_specs(variables: list[dict]) -> tuple[tuple[int, float, float], ...]:
    specs: list[tuple[int, float, float]] = []
    for index, variable in enumerate(variables):
        if not bool(variable.get('initial_search_asphere_profiled', False)):
            continue
        default = _lensde_configuration.finite_float(variable.get('initial'), 0.0)
        scale = 1.0
        if str(variable.get('kind', '')).lower() == 'asphere_coeff':
            radius = max(_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), 1.0), 1e-12)
            scale = radius ** _lensde_populations.asphere_order_for_term(int(variable.get('term', 0) or 0))
        specs.append((index, default, scale))
    return tuple(specs)


def _initial_asphere_profile_key_from_specs(row: list[float], specs: tuple[tuple[int, float, float], ...], digits: int) -> tuple[float, ...]:
    row_count = len(row)
    return tuple((round(_lensde_populations._finite_seed_number(row[index], default) * scale, digits) for index, default, scale in specs if index < row_count))


def initial_asphere_profile_key(row: list[float], variables: list[dict], digits: int=14) -> tuple[float, ...]:
    return _initial_asphere_profile_key_from_specs(row, _initial_asphere_profile_key_specs(variables), digits)


def _initial_asphere_classifier_specs(variables: list[dict]) -> tuple[tuple[int, int, str, float, float, float, int, int], ...]:
    specs: list[tuple[int, int, str, float, float, float, int, int]] = []
    for index, variable in enumerate(variables):
        if not bool(variable.get('initial_search_asphere_profiled', False)):
            continue
        lens_index = int(variable.get('initial_search_asphere_lens_index', 0) or 0)
        kind = str(variable.get('kind', '')).lower()
        default = _lensde_configuration.finite_float(variable.get('initial'), 0.0)
        if kind == 'conic':
            lower, upper = _lensde_populations.variable_bounds(variable)
            tolerance = max(1e-10, 1e-09 * max(abs(lower), abs(upper), 1.0))
            specs.append((index, lens_index, kind, default, tolerance, 1.0, -1, 0))
            continue
        if kind == 'asphere_coeff':
            surface_index = int(variable.get('index', -1) or -1)
            term = int(variable.get('term', 0) or 0)
            radius = max(_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), 1.0), 1e-12)
            q_scale = radius ** _lensde_populations.asphere_order_for_term(term)
            q_lower = _lensde_configuration.finite_float(variable.get('edge_sag_lower'), 0.0)
            q_upper = _lensde_configuration.finite_float(variable.get('edge_sag_upper'), 0.0)
            q_extent = max(abs(q_lower), abs(q_upper), 1e-15)
            specs.append((index, lens_index, kind, default, q_scale, q_extent, surface_index, term))
            continue
        specs.append((index, lens_index, kind, default, 0.0, 0.0, -1, 0))
    return tuple(specs)


def _classify_initial_asphere_profile_from_specs(row: list[float], specs: tuple[tuple[int, int, str, float, float, float, int, int], ...]) -> str:
    coeff_terms_by_surface: dict[int, set[int]] = {}
    active_lenses: set[int] = set()
    all_lenses: set[int] = set()
    surface_q_l1: dict[int, float] = {}
    active_conic = False
    row_count = len(row)
    for index, lens_index, kind, default, first, second, surface_index, term in specs:
        if index >= row_count:
            continue
        all_lenses.add(lens_index)
        value = _lensde_populations._finite_seed_number(row[index], default)
        if kind == 'conic':
            if abs(value - default) > first:
                active_conic = True
                active_lenses.add(lens_index)
            continue
        if kind != 'asphere_coeff':
            continue
        q_value = value * first
        if abs(q_value) > max(1e-14, 1e-09 * second):
            coeff_terms_by_surface.setdefault(surface_index, set()).add(term)
            active_lenses.add(lens_index)
            surface_q_l1[surface_index] = surface_q_l1.get(surface_index, 0.0) + abs(q_value) / second
    max_surface_q_l1 = max(surface_q_l1.values()) if surface_q_l1 else 0.0
    max_terms = max((len(terms) for terms in coeff_terms_by_surface.values()), default=0)
    if max_terms <= 0 and (not active_conic):
        return 'Z'
    lens_count = len(active_lenses)
    if lens_count <= 1 and max_terms <= 2:
        return 'L1'
    if lens_count <= 2 and max_terms <= 3:
        return 'L2'
    if lens_count >= len(all_lenses) and max_surface_q_l1 > 0.22:
        return 'W'
    return 'D'


def classify_initial_asphere_profile(row: list[float], variables: list[dict]) -> str:
    return _classify_initial_asphere_profile_from_specs(row, _initial_asphere_classifier_specs(variables))


def initial_asphere_profile_label(row: list[float], variables: list[dict], profile_metadata: dict[tuple[float, ...], dict] | None=None) -> str:
    if profile_metadata is not None:
        item = profile_metadata.get(initial_asphere_profile_key(row, variables))
        if isinstance(item, dict) and str(item.get('profile') or '') in state.INITIAL_ASPHERE_PROFILE_CONFIG:
            return str(item['profile'])
    return classify_initial_asphere_profile(row, variables)


def initial_asphere_base_geometry_key(row: list[float], variables: list[dict], digits: int=12) -> tuple[float, ...]:
    specs = tuple(((index, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for index, variable in enumerate(variables) if not bool(variable.get('initial_search_asphere_profiled', False))))
    return _initial_asphere_base_geometry_key_from_specs(row, specs, digits)


def _initial_asphere_base_geometry_key_from_specs(row: list[float], specs: tuple[tuple[int, float], ...], digits: int) -> tuple[float, ...]:
    row_count = len(row)
    return tuple((round(_lensde_populations._finite_seed_number(row[index], default), digits) for index, default in specs if index < row_count))


def allocate_full_restart_raw_seed_pools(rows: list[list[float]], variables: list[dict], outer_count: int, per_outer_rows: int, seed: int) -> tuple[list[list[list[float]]], dict]:
    """Deal disjoint full-restart raw pools, keeping sibling geometries together when possible."""
    outer_count = max(0, int(outer_count))
    per_outer_rows = max(0, int(per_outer_rows))
    required_rows = outer_count * per_outer_rows
    if outer_count <= 0 or per_outer_rows <= 0:
        return ([], {'mode': 'base_geometry_group_stratified_deal', 'source_rows': len(rows), 'required_rows': required_rows, 'allocated_rows': 0, 'unused_source_rows': len(rows), 'outer_count': outer_count, 'per_outer_target': per_outer_rows, 'per_outer_rows': [], 'base_geometry_group_count': 0, 'split_group_count': 0, 'split_group_row_count': 0, 'all_allocation_rows_assigned_once': required_rows == 0})
    if len(rows) < required_rows:
        raise ValueError(f'full-restart raw seed allocator needs {required_rows} rows; received {len(rows)}')
    rng = random.Random(int(seed))
    source_grouped_indices: dict[tuple[float, ...], list[int]] = {}
    for row_index, row in enumerate(rows):
        key = initial_asphere_base_geometry_key(row, variables)
        source_grouped_indices.setdefault(key, []).append(row_index)
    source_groups = list(source_grouped_indices.values())
    rng.shuffle(source_groups)
    selected_source_indices: list[int] = []
    source_selection_split_group_count = 0
    source_selection_split_row_count = 0
    for group in source_groups:
        remaining_source_capacity = required_rows - len(selected_source_indices)
        if remaining_source_capacity <= 0:
            break
        take = min(remaining_source_capacity, len(group))
        selected_source_indices.extend(group[:take])
        if take < len(group):
            source_selection_split_group_count += 1
            source_selection_split_row_count += take
    if len(selected_source_indices) != required_rows:
        raise RuntimeError('full-restart raw seed allocator could not sample the requested rows')
    allocation_rows = [list(rows[index]) for index in selected_source_indices]
    grouped_indices: dict[tuple[float, ...], list[int]] = {}
    for row_index, row in enumerate(allocation_rows):
        key = initial_asphere_base_geometry_key(row, variables)
        grouped_indices.setdefault(key, []).append(row_index)
    outer_priority = list(range(outer_count))
    rng.shuffle(outer_priority)
    priority_rank = {outer_index: rank for rank, outer_index in enumerate(outer_priority)}
    pools: list[list[list[float]]] = [[] for _ in range(outer_count)]
    assigned_indices: list[list[int]] = [[] for _ in range(outer_count)]
    remaining = [per_outer_rows for _ in range(outer_count)]
    split_group_count = 0
    split_group_row_count = 0
    for group in grouped_indices.values():
        group_size = len(group)
        eligible = [outer_index for outer_index in range(outer_count) if remaining[outer_index] >= group_size]
        if eligible:
            outer_index = min(eligible, key=lambda index: (-remaining[index], priority_rank[index]))
            assigned_indices[outer_index].extend(group)
            remaining[outer_index] -= group_size
            continue
        split_group_count += 1
        split_group_row_count += group_size
        cursor = 0
        for outer_index in sorted(range(outer_count), key=lambda index: (-remaining[index], priority_rank[index])):
            take = min(remaining[outer_index], group_size - cursor)
            if take <= 0:
                continue
            assigned_indices[outer_index].extend(group[cursor:cursor + take])
            remaining[outer_index] -= take
            cursor += take
            if cursor >= group_size:
                break
        if cursor != group_size:
            raise RuntimeError('full-restart raw seed allocator exhausted capacity unexpectedly')
    flat_assigned_indices = [row_index for indices in assigned_indices for row_index in indices]
    if any(remaining) or sorted(flat_assigned_indices) != list(range(required_rows)):
        raise RuntimeError('full-restart raw seed allocator did not assign every row exactly once')
    for outer_index, indices in enumerate(assigned_indices):
        pools[outer_index] = [allocation_rows[row_index] for row_index in indices]
    duplicate_source_keys = required_rows - len({_lensde_populations.variable_aware_seed_row_key(row, variables) for row in allocation_rows})
    summary = {'mode': 'base_geometry_group_stratified_deal', 'source_rows': len(rows), 'required_rows': required_rows, 'allocated_rows': len(flat_assigned_indices), 'unused_source_rows': max(0, len(rows) - required_rows), 'outer_count': outer_count, 'per_outer_target': per_outer_rows, 'per_outer_rows': [len(pool) for pool in pools], 'base_geometry_group_count': len(grouped_indices), 'source_base_geometry_group_count': len(source_grouped_indices), 'source_selection_mode': 'seeded_base_geometry_group_sample', 'source_selection_split_group_count': source_selection_split_group_count, 'source_selection_split_row_count': source_selection_split_row_count, 'split_group_count': split_group_count, 'split_group_row_count': split_group_row_count, 'duplicate_source_row_key_count': duplicate_source_keys, 'all_allocation_rows_assigned_once': len(flat_assigned_indices) == required_rows and len(set(flat_assigned_indices)) == required_rows, 'seed': int(seed)}
    return (pools, summary)


def direct_full_restart_seed_rows(rows: list[list[float]], target_rows: int) -> list[list[float]]:
    """Return an already-partitioned outer seed population without reselection."""
    target_rows = max(0, int(target_rows))
    if len(rows) != target_rows:
        raise ValueError(f'direct full-restart seed partition expected {target_rows} rows; received {len(rows)}')
    return [list(row) for row in rows]


def initial_asphere_profile_summary(rows: list[list[float]], variables: list[dict], profile_metadata: dict[tuple[float, ...], dict] | None=None) -> dict:
    profile_key_specs = _initial_asphere_profile_key_specs(variables) if profile_metadata is not None else ()
    classifier_specs = _initial_asphere_classifier_specs(variables)
    geometry_specs = tuple(((index, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for index, variable in enumerate(variables) if not bool(variable.get('initial_search_asphere_profiled', False))))
    profile_counts: Counter[str] = Counter()
    geometry_counts: Counter[tuple[float, ...]] = Counter()
    metadata_siblings = 0
    for row in rows:
        metadata_item = None
        if profile_metadata is not None:
            metadata_item = profile_metadata.get(_initial_asphere_profile_key_from_specs(row, profile_key_specs, 14))
        metadata_profile = str(metadata_item.get('profile') or '') if isinstance(metadata_item, dict) else ''
        profile = metadata_profile if metadata_profile in state.INITIAL_ASPHERE_PROFILE_CONFIG else _classify_initial_asphere_profile_from_specs(row, classifier_specs)
        profile_counts[profile] += 1
        geometry_counts[_initial_asphere_base_geometry_key_from_specs(row, geometry_specs, 12)] += 1
        if isinstance(metadata_item, dict) and bool(metadata_item.get('sibling', False)):
            metadata_siblings += 1
    return {'rows': len(rows), 'profile_counts': {profile: profile_counts.get(profile, 0) for profile in state.INITIAL_ASPHERE_PROFILE_CONFIG}, 'metadata_sibling_rows': metadata_siblings, 'shared_base_geometry_rows': sum((max(0, count - 1) for count in geometry_counts.values())), 'unique_base_geometries': len(geometry_counts)}


def rebalance_initial_asphere_profile_rows(selected_rows: list[list[float]], eligible_rows: list[list[float]], variables: list[dict], target: int, profile_metadata: dict[tuple[float, ...], dict] | None=None) -> list[list[float]]:
    """Reserve modest profile quotas without changing trace/merit decisions."""
    target = max(0, int(target))
    if target <= 0 or not _lensde_geometry.variable_list_has_asphere_coeff(variables):
        return [list(row) for row in selected_rows[:target]]
    scales = _lensde_populations._variable_aware_seed_scales(variables)
    profile_key_specs = _initial_asphere_profile_key_specs(variables) if profile_metadata is not None else ()
    classifier_specs = _initial_asphere_classifier_specs(variables)

    def normalized_unique_records(rows: list[list[float]], limit: int | None=None) -> list[tuple[list[float], tuple[float, ...]]]:
        records: list[tuple[list[float], tuple[float, ...]]] = []
        seen: set[tuple[float, ...]] = set()
        for source_row in rows:
            key = _lensde_populations._variable_aware_seed_row_key_from_scales(source_row, scales, 12)
            if key in seen:
                continue
            seen.add(key)
            records.append(([_lensde_populations._finite_seed_number(value, 0.0) for value in source_row], key))
            if limit is not None and len(records) >= limit:
                break
        return records

    def profile_label(row: list[float]) -> str:
        if profile_metadata is not None:
            item = profile_metadata.get(_initial_asphere_profile_key_from_specs(row, profile_key_specs, 14))
            if isinstance(item, dict):
                profile = str(item.get('profile') or '')
                if profile in state.INITIAL_ASPHERE_PROFILE_CONFIG:
                    return profile
        return _classify_initial_asphere_profile_from_specs(row, classifier_specs)
    eligible_unique_records = normalized_unique_records(eligible_rows)
    selected_unique_records = normalized_unique_records(selected_rows, target)
    selected = [row for row, _key in selected_unique_records]
    selected_key_list = [key for _row, key in selected_unique_records]
    eligible_records: list[tuple[list[float], tuple[float, ...], str]] = []
    label_by_key: dict[tuple[float, ...], str] = {}
    available_by_profile: dict[str, list[tuple[list[float], tuple[float, ...]]]] = {profile: [] for profile in state.INITIAL_ASPHERE_PROFILE_CONFIG}
    for row, key in eligible_unique_records:
        profile = profile_label(row)
        eligible_records.append((row, key, profile))
        label_by_key[key] = profile
        available_by_profile.setdefault(profile, []).append((row, key))
    selected_keys: set[tuple[float, ...]] = set()
    selected_labels: list[str] = []
    for row, key in zip(selected, selected_key_list, strict=False):
        selected_keys.add(key)
        selected_labels.append(label_by_key.get(key) or profile_label(row))
    current_counts = Counter(selected_labels)
    quotas = {profile: min(len(available_by_profile.get(profile, [])), max(1, int(math.ceil(target * fraction))) if available_by_profile[profile] else 0) for profile, fraction in state.INITIAL_ASPHERE_PROFILE_RESERVE_FRACTIONS.items()}
    candidate_cursors = {profile: 0 for profile in state.INITIAL_ASPHERE_PROFILE_CONFIG}
    victim_heap = [-index for index in range(len(selected))]
    heapq.heapify(victim_heap)
    for profile in ('W', 'D', 'L2', 'L1', 'Z'):
        candidates = available_by_profile.get(profile, [])
        while current_counts.get(profile, 0) < quotas.get(profile, 0):
            cursor = candidate_cursors.get(profile, 0)
            while cursor < len(candidates) and candidates[cursor][1] in selected_keys:
                cursor += 1
            candidate_cursors[profile] = cursor + 1
            if cursor >= len(candidates):
                break
            replacement, replacement_key = candidates[cursor]
            victim_index = None
            while victim_heap:
                candidate_index = -heapq.heappop(victim_heap)
                victim_profile = selected_labels[candidate_index]
                if current_counts.get(victim_profile, 0) > quotas.get(victim_profile, 0):
                    victim_index = candidate_index
                    break
            if victim_index is None:
                if len(selected) >= target:
                    break
                selected.append(list(replacement))
                selected_key_list.append(replacement_key)
                selected_labels.append(profile)
            else:
                victim_key = selected_key_list[victim_index]
                victim_profile = selected_labels[victim_index]
                selected_keys.discard(victim_key)
                selected[victim_index] = list(replacement)
                selected_key_list[victim_index] = replacement_key
                selected_labels[victim_index] = profile
                current_counts[victim_profile] -= 1
            selected_keys.add(replacement_key)
            current_counts[profile] += 1
    for row, key, profile in eligible_records:
        if len(selected) >= target:
            break
        if key not in selected_keys:
            selected.append(list(row))
            selected_key_list.append(key)
            selected_labels.append(profile)
            selected_keys.add(key)
    return selected[:target]


def make_profiled_initial_asphere_population(rows: list[list[float]], variables: list[dict], seed: int, scale: float=1.0, *, profile_metadata: dict[tuple[float, ...], dict] | None=None) -> list[list[float]]:
    """Overlay sparse-to-wide q profiles while preserving one exact flat row."""
    marked = [index for index, variable in enumerate(variables) if bool(variable.get('initial_search_asphere_profiled', False))]
    if not rows or not marked:
        return rows
    out = [[_lensde_configuration.finite_float(value, 0.0) for value in row] for row in rows]
    rng = random.Random(int(seed) + 73001)
    scale = max(0.0, _lensde_configuration.finite_float(scale, 1.0))
    shape_indices = [index for index, variable in enumerate(variables) if str(variable.get('kind', '')).lower() in {'curvature', 'radius', 'conic', 'asphere_coeff'}]
    for index in shape_indices:
        if index < len(out[0]):
            out[0][index] = _lensde_configuration.finite_float(variables[index].get('initial'), 0.0)
    conic_by_surface: dict[int, tuple[int, dict]] = {}
    coeffs_by_surface: dict[int, dict[int, tuple[int, dict]]] = {}
    curvature_by_surface: dict[int, int] = {}
    surfaces_by_lens: dict[int, set[int]] = {}
    for index in marked:
        variable = variables[index]
        lens_index = int(variable.get('initial_search_asphere_lens_index', 0) or 0)
        surface_index = int(variable.get('index', -1) or -1)
        surfaces_by_lens.setdefault(lens_index, set()).add(surface_index)
        if str(variable.get('kind', '')).lower() == 'asphere_coeff':
            term = int(variable.get('term', 0) or 0)
            coeffs_by_surface.setdefault(surface_index, {})[term] = (index, variable)
        else:
            conic_by_surface[surface_index] = (index, variable)
    for index, variable in enumerate(variables):
        if str(variable.get('kind', '')).lower() == 'curvature':
            curvature_by_surface[int(variable.get('index', -1) or -1)] = index
    lens_indices = sorted(surfaces_by_lens)
    if not lens_indices:
        return out
    profiled_count = max(0, len(out) - 1)
    fractions = (('Z', 0.2), ('L1', 0.3), ('L2', 0.25), ('D', 0.2))
    profiles: list[str] = []
    for name, fraction in fractions:
        profiles.extend([name] * int(round(profiled_count * fraction)))
    if len(profiles) < profiled_count:
        profiles.extend(['W'] * (profiled_count - len(profiles)))
    elif len(profiles) > profiled_count:
        profiles = profiles[:profiled_count]
    rng.shuffle(profiles)
    z_row_indices = [index + 1 for index, profile in enumerate(profiles) if profile == 'Z']
    non_z_row_indices = [index + 1 for index, profile in enumerate(profiles) if profile != 'Z']
    rng.shuffle(non_z_row_indices)
    sibling_target = min(len(non_z_row_indices), int(round(profiled_count * 0.25)))
    sibling_sources = {row_index: z_row_indices[offset % len(z_row_indices)] for offset, row_index in enumerate(non_z_row_indices[:sibling_target]) if z_row_indices}
    base_columns = [index for index in range(len(variables)) if index not in set(marked)]
    if profile_metadata is not None:
        profile_metadata[initial_asphere_profile_key(out[0], variables)] = {'profile': 'Z', 'strict_flat_anchor': True, 'sibling': False}
    for row_index in range(1, len(out)):
        row = out[row_index]
        sibling_source = sibling_sources.get(row_index)
        if sibling_source is not None:
            source_row = out[sibling_source]
            for column in base_columns:
                if column < len(row) and column < len(source_row):
                    row[column] = source_row[column]
        for variable_index in marked:
            if variable_index < len(row):
                row[variable_index] = _lensde_configuration.finite_float(variables[variable_index].get('initial'), 0.0)
        profile = profiles[row_index - 1]
        min_groups, max_groups, term_count, rho, conic_span = state.INITIAL_ASPHERE_PROFILE_CONFIG[profile]
        group_count = min_groups if min_groups == max_groups else rng.randint(min_groups, max_groups)
        if group_count <= 0:
            if profile_metadata is not None:
                profile_metadata[initial_asphere_profile_key(row, variables)] = {'profile': profile, 'sibling': sibling_source is not None, 'sibling_source_row': sibling_source}
            continue
        if group_count >= len(lens_indices):
            selected_lenses = list(lens_indices)
        elif profile in {'L1', 'L2'}:
            offset = (row_index - 1) % len(lens_indices)
            selected_lenses = [lens_indices[(offset + step) % len(lens_indices)] for step in range(group_count)]
        else:
            selected_lenses = rng.sample(lens_indices, group_count)
        for lens_index in selected_lenses:
            for surface_index in sorted(surfaces_by_lens.get(lens_index, set())):
                variables_by_term = coeffs_by_surface.get(surface_index, {})
                reference_radii = [_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), 0.0) for _column, variable in variables_by_term.values() if _lensde_configuration.finite_float(variable.get('asphere_reference_radius'), 0.0) > 0.0]
                reference_radii.sort()
                edge_radius = reference_radii[len(reference_radii) // 2] if reference_radii else 1.0
                terms_available = sorted(variables_by_term)
                count = min(term_count, len(terms_available))
                if count >= len(terms_available):
                    active_terms = terms_available
                elif count > 0:
                    offset = (row_index + surface_index) % len(terms_available)
                    active_terms = [terms_available[(offset + step) % len(terms_available)] for step in range(count)]
                else:
                    active_terms = []
                sampled = sample_initial_asphere_surface_coefficients(variables_by_term, active_terms, rho, rng, edge_radius, scale)
                for term, coefficient in sampled.items():
                    column, variable = variables_by_term[term]
                    row[column] = _lensde_populations.clamp_value(coefficient, *_lensde_populations.variable_bounds(variable))
                conic_entry = conic_by_surface.get(surface_index)
                if conic_entry is not None and conic_span > 0.0:
                    trial_span = 100.0 if profile == 'W' and rng.random() < 0.2 else conic_span
                    trial = rng.uniform(-trial_span, trial_span)
                    curvature_column = curvature_by_surface.get(surface_index)
                    curvature = row[curvature_column] if curvature_column is not None else 0.0
                    conic = project_initial_asphere_conic(curvature, trial, edge_radius, 1.5 * rho * scale)
                    column, variable = conic_entry
                    row[column] = _lensde_populations.clamp_value(conic, *_lensde_populations.variable_bounds(variable))
        if profile_metadata is not None:
            profile_metadata[initial_asphere_profile_key(row, variables)] = {'profile': profile, 'sibling': sibling_source is not None, 'sibling_source_row': sibling_source}
    return out


def external_initial_asphere_population_rows(rows: list[list[float]], variables: list[dict], target_population: int, profile_metadata: dict[tuple[float, ...], dict] | None=None) -> list[list[float]]:
    """Leave C++ row zero as the sole flat anchor and fill all other DE slots."""
    if not any((bool(variable.get('initial_search_asphere_profiled', False)) for variable in variables)):
        return rows
    shape_indices = [index for index, variable in enumerate(variables) if str(variable.get('kind', '')).lower() in {'curvature', 'radius', 'conic', 'asphere_coeff'}]

    def is_flat(row: list[float]) -> bool:
        for index in shape_indices:
            if index >= len(row):
                return False
            variable = variables[index]
            lower, upper = _lensde_populations.variable_bounds(variable)
            initial = _lensde_configuration.finite_float(variable.get('initial'), 0.0)
            tolerance = max(1e-30, 1e-12 * max(abs(upper - lower), abs(initial), 1e-15))
            if abs(_lensde_configuration.finite_float(row[index], initial) - initial) > tolerance:
                return False
        return True
    nonflat = [list(row) for row in rows if not is_flat(row)]
    target_external = max(0, int(target_population) - 1)
    return rebalance_initial_asphere_profile_rows(nonflat[:target_external], nonflat, variables, target_external, profile_metadata)


def derive_de_dls_geometry_kinds(variables: list[dict], configured: object='') -> str:
    configured_text = str(configured or '').strip()
    if configured_text:
        return configured_text
    available = {str(variable.get('kind') or '').strip() for variable in variables if isinstance(variable, dict)}
    kinds = ['curvature', 'thickness']
    for kind in ('radius', 'conic', 'asphere_coeff'):
        if kind in available:
            kinds.append(kind)
    return ','.join(kinds)


def append_asphere_reference_policy_args(args: list[str], variables: list[dict], policy: str) -> None:
    if not _lensde_geometry.variable_list_has_asphere_coeff(variables):
        return
    canonical = str(policy or 'current').strip().lower()
    if canonical not in {'current', 'input'}:
        canonical = 'current'
    args.extend(['--asphere-reference-policy', canonical])


def make_post_asphere_population(system: dict, variables: list[dict], values: list[float], population: int, seed: int, scale: float) -> list[list[float]]:
    return _lensde_populations.make_local_funnel_population(system, variables, values, population, seed, scale)


def make_post_asphere_graded_population(system: dict, variables: list[dict], values: list[float], population: int, seed: int, max_scale: float=0.35) -> list[list[float]]:
    """One whole-variable perturbation rule with near-to-wide amplitudes."""
    if population < 1 or len(values) != len(variables):
        raise ValueError('invalid graded post-asphere population dimensions')
    if population == 1:
        return [list(values)]
    quantiles = [(index + 0.5) / (population - 1) for index in range(population - 1)]
    random.Random(seed).shuffle(quantiles)
    rng = random.Random(seed + 1)
    rows = [list(values)]
    for quantile in quantiles:
        amplitude = max_scale * quantile ** 2.0
        rows.append([_lensde_populations.local_funnel_perturb_value(system, variable, value, rng, amplitude) for variable, value in zip(variables, values, strict=True)])
    return rows


def make_post_asphere_curvature_normalized_graded_population(system: dict, variables: list[dict], values: list[float], population: int, seed: int, max_scale: float=0.35, curvature_bound_fraction: float=0.025) -> list[list[float]]:
    """Graded Scout population with curvature scaled by its hard bounds.

    This is the production form of the paired s16/9ap ablation.  It preserves
    the exact mother, the shuffled ``max_scale*u^2`` schedule, RNG consumption,
    and every non-curvature value from ``make_post_asphere_graded_population``.
    Only curvature uses ``amplitude*0.025*(upper-lower)`` instead of the older
    parent-relative floor.
    """
    if population < 1 or len(values) != len(variables):
        raise ValueError('invalid curvature-normalized Scout population dimensions')
    if population == 1:
        return [list(values)]
    quantiles = [(index + 0.5) / (population - 1) for index in range(population - 1)]
    random.Random(seed).shuffle(quantiles)
    rng = random.Random(seed + 1)
    rows = [list(values)]
    for quantile in quantiles:
        amplitude = max_scale * quantile ** 2.0
        row: list[float] = []
        for variable, value in zip(variables, values, strict=True):
            if str(variable.get('kind', '')).lower() == 'curvature':
                lower, upper = _lensde_populations.variable_bounds(variable)
                span = amplitude * curvature_bound_fraction * (upper - lower)
                row.append(_lensde_populations.clamp_value(value + rng.gauss(0.0, span), lower, upper))
            else:
                row.append(_lensde_populations.local_funnel_perturb_value(system, variable, value, rng, amplitude))
        rows.append(row)
    return rows
