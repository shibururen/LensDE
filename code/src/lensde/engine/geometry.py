"""Extracted paper engine: geometry."""
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
from . import initialization as _lensde_initialization
from . import materials as _lensde_materials
from . import orchestration as _lensde_orchestration
from . import populations as _lensde_populations
from . import provenance as _lensde_provenance
from . import tracing as _lensde_tracing

def initial_search_thickness_constraints(options: dict, run_merit: dict, merit_strategy: str) -> dict[str, float]:
    """Return the manufacturing box used by the actual initial merit stage."""
    if str(merit_strategy).strip().lower() == 'full':
        return _lensde_configuration.normalize_thickness_constraint_fields(run_merit)
    return _lensde_configuration.normalize_thickness_constraint_fields(options, prefix='initial_search_')


def normalize_manufacturing_ratio_constraint_fields(values: dict, *, prefix: str='') -> dict[str, float]:
    out: dict[str, float] = {}
    for key, default in state.MANUFACTURING_RATIO_CONSTRAINT_DEFAULTS:
        out[key] = max(0.0, _lensde_configuration.finite_float(values.get(prefix + key), default))
    for base in ('atc', 'ate', 'bl0'):
        lower_key = f'{base}_lower'
        upper_key = f'{base}_upper'
        if out[upper_key] > 0.0 and out[upper_key] < out[lower_key]:
            out[upper_key] = out[lower_key]
    return out


def apply_first_order_weight_defaults(merit: dict) -> None:
    merit.setdefault('weight_efl', 1.0)
    merit.setdefault('weight_ttl', 0.4)
    merit.setdefault('weight_bfl', 0.5)
    merit.setdefault('weight_working_f_number', 1.0)
    merit.setdefault('unified_first_order_efl_weight', _lensde_configuration.first_order_weight_value(merit, 'efl', 1.0))
    merit.setdefault('unified_first_order_ttl_weight', _lensde_configuration.first_order_weight_value(merit, 'ttl', 0.4))
    merit.setdefault('unified_first_order_bfl_weight', _lensde_configuration.first_order_weight_value(merit, 'bfl', 0.5))
    merit.setdefault('unified_first_order_working_f_number_weight', _lensde_configuration.first_order_weight_value(merit, 'working_f_number', 1.0))


def stop_air_edge_operands(merit: dict) -> list[dict]:
    """Explicit S1 operands survive each auto-generated stage merit list."""
    return [copy.deepcopy(operand) for operand in merit.get('operands', []) if str(operand.get('name', '')).strip().lower() == 'stop_air_edge_thickness']


def auto_merit_config(project: dict) -> dict:
    merit = copy.deepcopy(project.get('merit', {}))
    dynamic_aperture_contract.apply_merit(merit)
    apply_first_order_weight_defaults(merit)
    merit['distortion_model'] = _lensde_defaults.canonical_distortion_model(merit.get('distortion_model'), 'auto')
    merit['wavefront_reference'] = _lensde_defaults.canonical_wavefront_reference(merit.get('wavefront_reference'), 'centroid_sphere')
    if not merit.get('auto_merit', False):
        return merit
    merit['merit_model'] = _lensde_defaults.sanitize_merit_model(merit.get('merit_model'), 'unified_trace_gate')
    merit['image_quality_metric'] = _lensde_defaults.canonical_image_quality_metric(merit.get('image_quality_metric'), 'rms_spot')
    merit['quality_spot_scale'] = _lensde_configuration.finite_float(merit.get('quality_spot_scale'), 0.01)
    merit['quality_wavefront_scale'] = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_wavefront_scale'), 0.5))
    merit['unified_image_mean_spot_weight'] = max(0.0, _lensde_configuration.finite_float(merit.get('unified_image_mean_spot_weight'), 3.0))
    merit['unified_image_single_ray_spot_tail_weight'] = max(0.0, _lensde_configuration.finite_float(merit.get('unified_image_single_ray_spot_tail_weight'), 0.012))
    merit['single_ray_spot_tail_extra_edge_min_field_radius'] = min(2.0, max(0.0, _lensde_configuration.finite_float(merit.get('single_ray_spot_tail_extra_edge_min_field_radius'), 0.85)))
    merit['unified_image_wavefront_weight'] = max(0.0, _lensde_configuration.finite_float(merit.get('unified_image_wavefront_weight'), 1.0))
    merit['quality_single_ray_guard_ratio'] = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_single_ray_guard_ratio'), 10.0))
    merit['quality_efl_rel_tol'] = _lensde_configuration.finite_float(merit.get('quality_efl_rel_tol'), 0.02)
    merit['quality_ttl_rel_tol'] = _lensde_configuration.finite_float(merit.get('quality_ttl_rel_tol'), 0.01)
    merit['quality_bfl_rel_tol'] = _lensde_configuration.finite_float(merit.get('quality_bfl_rel_tol'), 0.05)
    merit['quality_working_f_number_tol'] = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_working_f_number_tol'), 0.01))
    merit['quality_length_abs_tol'] = _lensde_configuration.finite_float(merit.get('quality_length_abs_tol'), 0.1)
    merit['quality_edge_tol'] = _lensde_configuration.finite_float(merit.get('quality_edge_tol'), 0.2)
    merit['quality_ratio_tol'] = _lensde_configuration.finite_float(merit.get('quality_ratio_tol'), 0.01)
    merit['quality_bound_tol'] = _lensde_configuration.finite_float(merit.get('quality_bound_tol'), 0.05)
    merit['minimum_relative_illumination'] = min(1.0, max(0.0, _lensde_configuration.finite_float(merit.get('minimum_relative_illumination'), 0.0)))
    merit['quality_surface_tangent_angle_tol_deg'] = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_surface_tangent_angle_tol_deg'), 5.0))
    merit['quality_distortion_tol'] = _lensde_configuration.finite_float(merit.get('quality_distortion_tol'), 0.01)
    merit['quality_chief_ray_angle_tol'] = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_chief_ray_angle_tol'), 5.0))
    merit['quality_chief_ray_angle_tightened_tol'] = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_chief_ray_angle_tightened_tol'), 0.5))
    merit['quality_lateral_color_tol'] = _lensde_configuration.finite_float(merit.get('quality_lateral_color_tol'), 0.01)
    merit['quality_axial_color_tol'] = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_axial_color_tol'), 0.025))
    merit['quality_spectral_spot_spread_tol'] = max(1e-09, _lensde_configuration.finite_float(merit.get('quality_spectral_spot_spread_tol'), 0.0015))
    merit['chromatic_spot_merit_enabled'] = bool(merit.get('chromatic_spot_merit_enabled', True))
    merit.update(_lensde_configuration.normalize_thickness_constraint_fields(merit))
    merit.update(normalize_manufacturing_ratio_constraint_fields(merit))
    merit['surface_tangent_angle_lower_deg'] = min(90.0, max(0.0, _lensde_configuration.finite_float(merit.get('surface_tangent_angle_lower_deg'), 30.0)))
    merit['surface_tangent_angle_samples'] = min(4096, max(2, int(_lensde_configuration.finite_float(merit.get('surface_tangent_angle_samples'), 32))))
    operands = _lensde_defaults.image_quality_operands(merit['image_quality_metric'], spot_weight=merit['unified_image_mean_spot_weight'], spot_tail_weight=merit['unified_image_single_ray_spot_tail_weight'], wavefront_weight=merit['unified_image_wavefront_weight'], guard_ratio=merit['quality_single_ray_guard_ratio'])
    target_efl = _lensde_configuration.finite_float(merit.get('target_efl'), 0.0)
    target_ttl = _lensde_configuration.finite_float(merit.get('target_ttl'), 0.0)
    target_bfl = _lensde_configuration.finite_float(merit.get('target_bfl'), 0.0)
    working_f_number_upper = max(0.0, _lensde_configuration.finite_float(merit.get('working_f_number_upper'), 0.0))
    merit['working_f_number_upper'] = working_f_number_upper
    distortion_upper = _lensde_configuration.finite_float(merit.get('distortion_upper'), 0.0)
    chief_ray_angle_upper = _lensde_configuration.finite_float(merit.get('chief_ray_angle_upper'), 0.0)
    lateral_color_upper = _lensde_configuration.finite_float(merit.get('lateral_color_upper'), merit['quality_lateral_color_tol'])
    axial_color_upper = _lensde_configuration.finite_float(merit.get('axial_color_upper'), merit['quality_axial_color_tol'])
    worst_wavelength_spot_upper = _lensde_configuration.finite_float(merit.get('worst_wavelength_spot_upper'), 0.8 * merit['quality_spot_scale'])
    spectral_spot_spread_upper = _lensde_configuration.finite_float(merit.get('spectral_spot_spread_upper'), merit['quality_spectral_spot_spread_tol'])
    if abs(target_efl) > state.TARGET_EFL_EPS:
        operands.append({'name': 'efl', 'mode': 'target', 'target': target_efl, 'weight': _lensde_configuration.first_order_weight_value(merit, 'efl', 1.0)})
    if target_ttl > 0.0:
        operands.append({'name': 'ttl', 'mode': 'less_than', 'upper': target_ttl, 'weight': _lensde_configuration.first_order_weight_value(merit, 'ttl', 0.4)})
    if target_bfl > 0.0:
        operands.append({'name': 'bfl', 'mode': 'greater_than', 'lower': target_bfl, 'weight': _lensde_configuration.first_order_weight_value(merit, 'bfl', 0.5)})
    if working_f_number_upper > 0.0:
        operands.append({'name': 'working_f_number', 'mode': 'less_than', 'upper': working_f_number_upper, 'weight': _lensde_configuration.first_order_weight_value(merit, 'working_f_number', 1.0)})
    if distortion_upper > 0.0:
        operands.append({'name': 'max_distortion', 'mode': 'less_than', 'upper': distortion_upper, 'weight': 1.0})
    if chief_ray_angle_upper > 0.0:
        operands.append({'name': 'max_chief_ray_angle', 'mode': 'less_than', 'upper': chief_ray_angle_upper, 'weight': 1.0})
    if lateral_color_upper > 0.0:
        operands.append({'name': 'lateral_color', 'mode': 'less_than', 'upper': lateral_color_upper, 'weight': 1.0})
    if merit['chromatic_spot_merit_enabled']:
        if axial_color_upper > 0.0:
            operands.append({'name': 'axial_color', 'mode': 'less_than', 'upper': axial_color_upper, 'weight': 1.0})
        if worst_wavelength_spot_upper > 0.0:
            operands.append({'name': 'worst_wavelength_spot', 'mode': 'less_than', 'upper': worst_wavelength_spot_upper, 'weight': 1.0})
        if spectral_spot_spread_upper > 0.0:
            operands.append({'name': 'spectral_spot_spread', 'mode': 'less_than', 'upper': spectral_spot_spread_upper, 'weight': 1.0})
    merit['operands'] = operands + stop_air_edge_operands(merit)
    merit = _lensde_materials.apply_continuous_glass_proximity_merit(merit, project.get('system', {}), project.get('variables', []), project.get('options', {}))
    return merit


def strip_first_order_field_constraints(merit: dict) -> dict:
    first_order = copy.deepcopy(merit)
    for key in ('distortion_upper', 'chief_ray_angle_upper', 'lateral_color_upper', 'axial_color_upper', 'worst_wavelength_spot_upper', 'spectral_spot_spread_upper', 'quality_distortion_tol', 'quality_chief_ray_angle_tol', 'quality_chief_ray_angle_tightened_tol', 'quality_lateral_color_tol', 'quality_axial_color_tol', 'quality_spectral_spot_spread_tol', 'chromatic_spot_merit_enabled'):
        first_order.pop(key, None)
    first_order['operands'] = [operand for operand in first_order.get('operands', []) if str(operand.get('name', '')).lower().replace('-', '_').replace(' ', '_') not in {'distortion', 'max_distortion', 'maximum_distortion', 'distortion_percent', 'max_distortion_percent', 'maximum_distortion_percent', 'chief_ray_angle', 'chief_ray_incidence_angle', 'max_chief_ray_angle', 'max_chief_ray_incidence_angle', 'cra', 'max_cra', 'lateral_color', 'max_lateral_color', 'lateral_chromatic_aberration', 'transverse_chromatic_aberration', 'tca', 'lca', 'axial_color', 'paraxial_axial_color', 'axial_color_focus_shift', 'paraxial_axial_color_focus_shift', 'worst_wavelength_spot', 'worst_wavelength_rms_spot', 'max_monochromatic_spot', 'max_monochromatic_rms_spot', 'spectral_spot_spread', 'spectral_rms_spread', 'wavelength_spot_spread', 'chromatic_spot_spread'}]
    return first_order


def is_fine_polish_mode(mode: str) -> bool:
    return mode in {'fine_polish', 'precision_polish', 'micro_polish', 'refine_polish', 'refine', 'fine'}


def fine_polish_merit_config(merit: dict, options: dict) -> dict:
    refined = copy.deepcopy(merit)
    refined['merit_model'] = 'unified_trace_gate'
    image_metric = _lensde_defaults.canonical_image_quality_metric(refined.get('image_quality_metric'), 'rms_spot')
    refined['image_quality_metric'] = image_metric
    refined['wavefront_reference'] = _lensde_defaults.canonical_wavefront_reference(refined.get('wavefront_reference'), 'centroid_sphere')
    if image_metric in {'rms_wavefront', 'rms_spot_wavefront'}:
        refined['quality_wavefront_scale'] = max(1e-09, _lensde_configuration.finite_float(options.get('fine_polish_wavefront_scale'), _lensde_configuration.finite_float(refined.get('quality_wavefront_scale'), 0.5)))
    if image_metric in {'rms_spot', 'rms_spot_wavefront'}:
        refined['quality_spot_scale'] = max(1e-09, _lensde_configuration.finite_float(options.get('fine_polish_spot_scale'), 0.01))
    refined['num_rays'] = max(int(refined.get('num_rays', 1) or 1), int(max(1.0, _lensde_configuration.finite_float(options.get('fine_polish_rays'), 5.0))))
    refined['pupil_arms'] = max(int(refined.get('pupil_arms', 1) or 1), int(max(1.0, _lensde_configuration.finite_float(options.get('fine_polish_arms'), 12.0))))
    refined.setdefault('pupil_distribution', 'hexapolar')
    refined['fine_polish'] = True
    return refined


def sync_stop_aperture(project: dict) -> None:
    """Keep the stop consistent with its fixed or front-stop EPD contract.

    For an automatic stop, the C++ tracer writes the physical actual-ray
    envelope.  The one direct exception is an automatic stop on the first
    optical surface: with no preceding optics, that stop is the entrance pupil
    and must track EPD/2 immediately when EPD changes.
    """
    if not project.get('options', {}).get('sync_stop_aperture', True):
        return
    system = project.get('system', {})
    surfaces = system.get('surfaces', [])
    first_optical_index = 1 if surfaces and str(surfaces[0].get('kind', '')).strip().lower() == 'object' else 0
    for index, surface in enumerate(surfaces):
        if not surface.get('is_stop'):
            continue
        if surface.get('clear_aperture_fixed'):
            radius = _lensde_configuration.finite_float(surface.get('aperture_radius'), _lensde_configuration.finite_float(surface.get('semi_aperture'), 0.0))
        else:
            aperture = system.get('aperture') or {}
            if index != first_optical_index or str(aperture.get('type', '')).strip().upper() != 'EPD':
                return
            radius = 0.5 * abs(_lensde_configuration.finite_float(aperture.get('value'), 0.0))
        if radius <= 0.0:
            return
        surface['semi_aperture'] = radius
        surface['aperture_radius'] = radius
        surface['aperture'] = {'type': 'RadialAperture', 'r_max': radius, 'r_min': 0.0}
        if not surface.get('mechanical_aperture_fixed'):
            surface['mechanical_aperture'] = radius
        return


def surface_edge_radius(system: dict, index: int) -> float:
    surfaces = system.get('surfaces', [])
    surface = surfaces[index] if 0 <= index < len(surfaces) else {}
    aperture = surface.get('aperture') or {}
    candidates = [abs(_lensde_configuration.finite_float(surface.get('semi_aperture'), 0.0)), abs(_lensde_configuration.finite_float(surface.get('aperture_radius'), 0.0)), abs(_lensde_configuration.finite_float(surface.get('mechanical_aperture'), 0.0)), abs(_lensde_configuration.finite_float(aperture.get('r_max'), 0.0)), _lensde_populations.system_fallback_radius(system)]
    return max([value for value in candidates if math.isfinite(value)] + [1.0])


def asphere_stage_reference_radius(system: dict, index: int) -> float:
    """Return the current optical clear radius used to freeze q=A*r^order.

    The traced semi-aperture is authoritative. A legacy or manufacturing
    mechanical aperture can be much larger (especially for a flat start), so
    it is fallback-only and must not silently suppress high-order terms.
    """
    surfaces = system.get('surfaces', []) or []
    surface = surfaces[index] if 0 <= index < len(surfaces) else {}
    aperture = surface.get('aperture') or {}
    for value in (surface.get('semi_aperture'), surface.get('aperture_radius'), aperture.get('r_max'), surface.get('mechanical_aperture')):
        radius = abs(_lensde_configuration.finite_float(value, 0.0))
        if math.isfinite(radius) and radius > 0.0:
            return max(radius, 1e-12)
    return max(_lensde_populations.system_fallback_radius(system), 1e-12)


def has_curvature_variable(variables: list[dict], index: int) -> bool:
    return any((str(variable.get('kind', '')) in {'curvature', 'radius'} and int(variable.get('index', -1) or -1) == index for variable in variables))


def asphere_sag_budget(term: int, has_curvature: bool=False) -> float:
    if max(0, int(term)) == 0 and has_curvature:
        return state.ASPHERE_A2_WITH_CURVATURE_SAG_BUDGET
    return state.ASPHERE_SAG_BUDGETS[min(max(0, int(term)), len(state.ASPHERE_SAG_BUDGETS) - 1)]


def asphere_edge_sag_values(system: dict, variable: dict, lower: float, upper: float) -> tuple[float, float, float]:
    """Return frozen radius and q=A*r^order bounds for an asphere variable."""
    index = int(variable.get('index', 0) or 0)
    term = max(0, int(variable.get('term', 0) or 0))
    radius = asphere_stage_reference_radius(system, index)
    order = 2 * (term + 1)
    try:
        radial_power = radius ** order
    except OverflowError:
        radial_power = math.inf
    if not math.isfinite(radial_power) or radial_power <= 0.0:
        radial_power = 1.0
    return (radius, _lensde_configuration.finite_float(lower, 0.0) * radial_power, _lensde_configuration.finite_float(upper, 0.0) * radial_power)


def annotate_asphere_edge_sag_variable(system: dict, variable: dict) -> dict:
    """Attach an explicit frozen edge-sag transform without changing A bounds."""
    if str(variable.get('kind', '')).lower() != 'asphere_coeff':
        return variable
    lower, upper = _lensde_populations.variable_bounds(variable)
    radius, sag_lower, sag_upper = asphere_edge_sag_values(system, variable, lower, upper)
    variable['asphere_parameterization'] = 'edge_sag'
    variable['asphere_reference_radius'] = radius
    variable['edge_sag_lower'] = sag_lower
    variable['edge_sag_upper'] = sag_upper
    return variable


def rebase_asphere_edge_sag_variable(system: dict, variables: list[dict], variable: dict, *, current_value: float | None=None) -> dict:
    """Rebuild A bounds at the current aperture while preserving physical A."""
    if str(variable.get('kind', '')).lower() != 'asphere_coeff':
        return variable
    index = int(variable.get('index', 0) or 0)
    term = max(0, int(variable.get('term', 0) or 0))
    radius = asphere_stage_reference_radius(system, index)
    order = 2 * (term + 1)
    try:
        radial_power = radius ** order
    except OverflowError:
        radial_power = math.inf
    if not math.isfinite(radial_power) or radial_power <= 0.0:
        radial_power = 1.0
    if current_value is None:
        current_value = variable_current_value({'system': system}, variable)
    current_value = _lensde_configuration.finite_float(current_value, _lensde_configuration.finite_float(variable.get('initial'), 0.0))
    current_sag = current_value * radial_power
    default_budget = asphere_sag_budget(term, has_curvature_variable(variables, index))
    prior_budget = max(abs(_lensde_configuration.finite_float(variable.get('edge_sag_lower'), 0.0)), abs(_lensde_configuration.finite_float(variable.get('edge_sag_upper'), 0.0)))
    sag_bound = max(default_budget, prior_budget, 1.2 * abs(current_sag), 1e-15)
    coefficient_bound = sag_bound / radial_power
    variable['lower'] = -coefficient_bound
    variable['upper'] = coefficient_bound
    variable['initial'] = current_value
    variable['asphere_parameterization'] = 'edge_sag'
    variable['asphere_reference_radius'] = radius
    variable['edge_sag_lower'] = -sag_bound
    variable['edge_sag_upper'] = sag_bound
    return variable


def rebase_asphere_edge_sag_variables(system: dict, variables: list[dict], *, force: bool=False) -> list[dict]:
    for variable in variables:
        if str(variable.get('kind', '')).lower() != 'asphere_coeff':
            continue
        marked = str(variable.get('asphere_parameterization', '')).lower() == 'edge_sag'
        if force or marked:
            rebase_asphere_edge_sag_variable(system, variables, variable, current_value=_lensde_configuration.finite_float(variable.get('initial'), 0.0))
    return variables


def asphere_bound_span(system: dict, variable: dict, variables: list[dict] | None=None, value: float | None=None) -> float:
    index = int(variable.get('index', 0) or 0)
    term = max(0, int(variable.get('term', 0) or 0))
    if value is None:
        surfaces = system.get('surfaces', [])
        surface = surfaces[index] if 0 <= index < len(surfaces) else {}
        coeffs = surface.get('asphere_coefficients') or []
        value = _lensde_configuration.finite_float(coeffs[term] if term < len(coeffs) else variable.get('initial'), 0.0)
    radius = surface_edge_radius(system, index)
    order = 2 * (term + 1)
    budget = asphere_sag_budget(term, has_curvature_variable(variables or [], index))
    try:
        radial_power = radius ** order
    except OverflowError:
        radial_power = math.inf
    sag_limited = 0.0 if not math.isfinite(radial_power) else budget / max(radial_power, 1.0)
    return max(abs(_lensde_configuration.finite_float(value, 0.0)) * 10.0, sag_limited)


def legacy_asphere_bounds(variable: dict, value: float) -> bool:
    lower = _lensde_configuration.finite_float(variable.get('lower'), math.nan)
    upper = _lensde_configuration.finite_float(variable.get('upper'), math.nan)
    if not math.isfinite(lower) or not math.isfinite(upper) or lower >= upper:
        return False
    legacy_span = max(abs(value) * 10.0, 0.0001)
    center = 0.5 * (lower + upper)
    span = 0.5 * (upper - lower)
    tol = max(1e-12, legacy_span * 1e-06)
    return abs(center - value) <= tol and abs(span - legacy_span) <= tol


def legacy_material_bounds(variable: dict) -> bool:
    kind = str(variable.get('kind', ''))
    lower = _lensde_configuration.finite_float(variable.get('lower'), math.nan)
    upper = _lensde_configuration.finite_float(variable.get('upper'), math.nan)
    if not math.isfinite(lower) or not math.isfinite(upper) or lower >= upper:
        return False
    if kind == 'material_nd':
        return abs(lower - 1.25) <= 1e-09 and abs(upper - 2.3) <= 1e-09
    if kind == 'material_vd':
        return (abs(lower - 25.0) <= 1e-09 or abs(lower - 15.0) <= 1e-09) and abs(upper - 100.0) <= 1e-09
    return False


def variable_default_bounds(project: dict, variable: dict) -> tuple[float, float]:
    kind = variable.get('kind', 'radius')
    index = int(variable.get('index', 0) or 0)
    system = project.get('system', {})
    surfaces = system.get('surfaces', [])
    surface = surfaces[index] if 0 <= index < len(surfaces) else {}
    material = surface.get('material_after', {})
    mode = material.get('mode', 'air')
    nd = float(material.get('nd', 1.0) or 1.0)
    if kind == 'radius':
        return (-2000.0, 2000.0)
    if kind == 'curvature':
        return (-state.DEFAULT_CURVATURE_BOUND, state.DEFAULT_CURVATURE_BOUND)
    if kind == 'thickness':
        if surface.get('kind') == 'object':
            return (0.0, 1000000000000.0)
        is_glass = mode == 'glass' or (mode == 'abbe' and nd > 1.0005)
        return (0.5, 10.0) if is_glass else (0.1, 100.0)
    if kind == 'material_nd':
        nd_min, nd_max, _, _ = _lensde_materials.glass_material_bounds_for_surface(system, index)
        return (nd_min, nd_max)
    if kind == 'material_vd':
        _, _, vd_min, vd_max = _lensde_materials.glass_material_bounds_for_surface(system, index)
        return (vd_min, vd_max)
    if kind == 'conic':
        return (-100.0, 100.0)
    if kind == 'asphere_coeff':
        value = variable_current_value(project, variable)
        span = asphere_bound_span(system, variable, project.get('variables', []), value)
        return (value - span, value + span)
    vignetting_kind = _lensde_configuration.canonical_vignetting_variable_kind(kind)
    if bool(variable.get('auto_vignetting_variable', False)) and vignetting_kind:
        if variable.get('vignetting_bound_policy') == state.VIGNETTING_HANDOFF_BOUND_POLICY:
            return (-1.0, 1.0) if vignetting_kind in {'fvcy', 'vignette_asymmetry_y'} else (0.0, 1.0)
        _, _, field_budget = _lensde_configuration.automatic_vignetting_field_budget(system, index, project.get('merit', {}))
        if field_budget > 0.0:
            if vignetting_kind == 'vignette_asymmetry_y':
                return (-1.0, 1.0)
            if vignetting_kind == 'vignette_magnitude_y':
                return (0.0, field_budget)
            return (-field_budget, field_budget) if vignetting_kind == 'fvcy' else (0.0, field_budget)
    if vignetting_kind == 'fvcy':
        return state.MANUAL_VIGNETTING_FVCY_BOUNDS
    if vignetting_kind == 'fvdy':
        return state.MANUAL_VIGNETTING_FVDY_BOUNDS
    if vignetting_kind == 'vignette_magnitude_y':
        return state.MANUAL_VIGNETTING_FVDY_BOUNDS
    if vignetting_kind == 'vignette_asymmetry_y':
        return (-1.0, 1.0)
    return (float(variable.get('lower', -1000000.0) or -1000000.0), float(variable.get('upper', 1000000.0) or 1000000.0))


def apply_variable_bounds(project: dict) -> None:
    bounds = project.get('merit', {}).get('bounds', []) or project.get('merit', {}).get('variable_bounds', []) or []
    for variable in project.get('variables', []):
        lower = variable.get('lower')
        upper = variable.get('upper')
        current_value = variable_current_value(project, variable)
        if variable.get('kind') == 'asphere_coeff' and lower is not None and (upper is not None) and legacy_asphere_bounds(variable, current_value):
            lower, upper = variable_default_bounds(project, variable)
        elif variable.get('kind') in {'material_nd', 'material_vd'} and lower is not None and (upper is not None) and legacy_material_bounds(variable):
            lower, upper = variable_default_bounds(project, variable)
        elif lower is None or upper is None:
            lower, upper = variable_default_bounds(project, variable)
        for bound in bounds:
            if bound.get('kind') != variable.get('kind'):
                continue
            if int(bound.get('index', -1)) != int(variable.get('index', -2)):
                continue
            if variable.get('kind') == 'asphere_coeff' and int(bound.get('term', 0)) != int(variable.get('term', 0)):
                continue
            lower = bound.get('lower', lower)
            upper = bound.get('upper', upper)
        lo = optional_finite_float(lower)
        hi = optional_finite_float(upper)
        if lo is None or hi is None or lo >= hi:
            lo, hi = variable_default_bounds(project, variable)
        lower, upper = (lo, hi)
        variable['lower'] = lower
        variable['upper'] = upper
        variable['initial'] = _lensde_populations.clamp_value(_lensde_configuration.finite_float(variable.get('initial'), variable_current_value(project, variable)), lower, upper)
        if str(variable.get('kind', '')).lower() == 'asphere_coeff':
            annotate_asphere_edge_sag_variable(project.get('system', {}), variable)


def variable_current_value(project: dict, variable: dict) -> float:
    kind = variable.get('kind', 'radius')
    try:
        index = int(variable.get('index', 0) or 0)
    except (TypeError, ValueError):
        index = 0
    try:
        term = int(variable.get('term', 0) or 0)
    except (TypeError, ValueError):
        term = 0
    try:
        fallback = float(variable.get('initial', 0.0) or 0.0)
    except (TypeError, ValueError):
        fallback = 0.0
    system = project.get('system', {})
    surfaces = system.get('surfaces', [])
    surface = surfaces[index] if 0 <= index < len(surfaces) else {}
    fields = system.get('fields', [])
    field = fields[index] if 0 <= index < len(fields) else {}

    def number(value, default=fallback):
        try:
            return float(value)
        except (TypeError, ValueError):
            return default
    if kind == 'radius':
        return number(surface.get('radius'))
    if kind == 'curvature':
        return _lensde_defaults.curvature_from_radius(surface.get('radius'))
    if kind == 'thickness':
        return number(surface.get('thickness'))
    if kind == 'conic':
        return number(surface.get('conic'), 0.0)
    if kind == 'asphere_coeff':
        coeffs = surface.get('asphere_coefficients', [])
        return number(coeffs[term] if 0 <= term < len(coeffs) else 0.0, 0.0)
    material = surface.get('material_after', {})
    if kind == 'material_nd':
        return number(material.get('nd'), 1.0)
    if kind == 'material_vd':
        return number(material.get('vd'), 0.0)
    if kind == 'vignette_x':
        return number(field.get('fvdx', field.get('vignette_x', 0.0)), 0.0)
    if _lensde_configuration.canonical_vignetting_variable_kind(kind) == 'fvcy':
        return number(field.get('fvcy', 0.0), 0.0)
    if _lensde_configuration.canonical_vignetting_variable_kind(kind) == 'fvdy':
        return number(field.get('fvdy', field.get('vignette_y', 0.0)), 0.0)
    if _lensde_configuration.canonical_vignetting_variable_kind(kind) == 'vignette_magnitude_y':
        return max(0.0, number(field.get('fvdy', field.get('vignette_y', 0.0)), 0.0))
    if _lensde_configuration.canonical_vignetting_variable_kind(kind) == 'vignette_asymmetry_y':
        magnitude = max(0.0, number(field.get('fvdy', field.get('vignette_y', 0.0)), 0.0))
        return max(-1.0, min(1.0, number(field.get('fvcy', 0.0), 0.0) / magnitude)) if magnitude > 1e-15 else 0.0
    return fallback


def sync_variable_initials(project: dict) -> None:
    for variable in project.get('variables', []):
        variable['initial'] = variable_current_value(project, variable)


def material_index(surface: dict, default: float=1.0) -> float:
    material = surface.get('material_after') or {}
    return _lensde_configuration.finite_float(material.get('nd'), default)


def surface_material_is_glass(surface: dict) -> bool:
    material = surface.get('material_after') or {}
    mode = str(material.get('mode', 'air')).lower()
    nd = material_index(surface)
    return mode in {'glass', 'abbe'} and nd > 1.0005


def ttl_efl_longness(target_ttl: float, target_efl: float) -> tuple[float, float]:
    target_total = _lensde_configuration.finite_float(target_ttl, 0.0)
    efl = abs(_lensde_configuration.finite_float(target_efl, 0.0))
    if target_total <= 0.0 or efl <= state.TARGET_EFL_EPS:
        return (0.0, 0.0)
    ratio = target_total / max(efl, state.TARGET_EFL_EPS)
    if ratio <= 2.0:
        return (ratio, 0.0)
    longness = (math.log(ratio) - math.log(2.0)) / (math.log(18.0) - math.log(2.0))
    return (ratio, _lensde_populations.clamp_value(longness, 0.0, 1.0))


def resolve_bfl_reference_surface(system: dict, surface_index: int | None=None) -> int:
    """Return the surface whose following interval starts the BFL path.

    Surface indices use the same zero-based S-index displayed by the GUI.  A
    negative or missing value preserves the legacy definition: the last
    surface before the image.  Choosing an earlier surface makes BFL the sum
    of every axial interval from that surface through the image, which lets a
    cover-glass stack live inside the protected back-working-distance path.
    """
    surfaces = system.get('surfaces', []) or []
    last_interval = max(0, len(surfaces) - 2)
    if surface_index is None:
        surface_index = system.get('bfl_reference_surface', -1)
    try:
        requested = int(surface_index)
    except (TypeError, ValueError):
        return last_interval
    if requested < 0:
        return last_interval
    return max(0, min(last_interval, requested))


def bfl_interval_indices(system: dict, surface_index: int | None=None) -> set[int]:
    surfaces = system.get('surfaces', []) or []
    if len(surfaces) < 2:
        return set()
    reference = resolve_bfl_reference_surface(system, surface_index)
    last_interval = max(0, len(surfaces) - 2)
    return set(range(reference, last_interval + 1))


def adaptive_thickness_budget(system: dict, target_ttl: float, target_bfl: float, target_efl: float=0.0, thickness_constraints: dict | None=None, *, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5, use_current_thickness_prior: bool=False) -> dict[str, float]:
    surfaces = system.get('surfaces', []) or []
    if len(surfaces) < 2:
        return {}
    bfl_indices = bfl_interval_indices(system, bfl_surface_index)
    current_total = 0.0
    current_body = 0.0
    air_count = 0
    glass_count = 0
    air_max_current = 0.0
    glass_max_current = 0.0
    bfl_current = 0.0
    for index, surface in enumerate(surfaces[:-1]):
        if str(surface.get('kind', '')).lower() in {'object', 'image'}:
            continue
        thickness = max(0.0, _lensde_configuration.finite_float(surface.get('thickness'), 0.0))
        current_total += thickness
        if index in bfl_indices:
            bfl_current += thickness
            continue
        if surface_material_is_glass(surface):
            glass_count += 1
            glass_max_current = max(glass_max_current, thickness)
        else:
            air_count += 1
            air_max_current = max(air_max_current, thickness)
        current_body += thickness
    target_total = _lensde_configuration.finite_float(target_ttl, 0.0)
    if target_total <= 0.0:
        target_total = current_total if current_total > 0.0 else max(current_body + bfl_current, 1.0)
    target_bfl_value = _lensde_configuration.finite_float(target_bfl, 0.0)
    if target_bfl_value <= 0.0:
        target_bfl_value = bfl_current if bfl_current > 0.0 else max(1.0, 0.18 * target_total)
    body_budget = max(1.0, target_total - max(0.0, target_bfl_value))
    body_count = max(1, air_count + glass_count)
    per_body_interval = body_budget / body_count
    ttl_efl_ratio, length_longness = ttl_efl_longness(target_total, target_efl)
    constraints = _lensde_configuration.normalize_thickness_constraint_fields(thickness_constraints) if isinstance(thickness_constraints, dict) else {}
    glass_lower = _lensde_configuration.finite_float(constraints.get('glass_center_thickness_lower'), 0.3)
    air_lower = _lensde_configuration.finite_float(constraints.get('air_center_thickness_lower'), 0.1)
    glass_multiplier = 1.9 + 0.6 * length_longness
    glass_upper = max(1.0, per_body_interval * glass_multiplier)
    if use_current_thickness_prior and glass_count > 0:
        glass_upper = max(glass_upper, 1.25 * glass_max_current)
    glass_upper = min(max(glass_upper, 1.0), max(6.0, 0.3 * target_total))
    if constraints:
        glass_upper = min(glass_upper, max(glass_lower, _lensde_configuration.finite_float(constraints.get('glass_center_thickness_upper'), glass_upper)))
    glass_upper = max(glass_lower, glass_upper)
    air_multiplier = 2.6 + 4.2 * length_longness
    air_upper = max(1.0, per_body_interval * air_multiplier)
    if length_longness > 0.0:
        long_gap_upper = body_budget * (0.12 + 0.33 * length_longness)
        air_upper = max(air_upper, long_gap_upper)
    if use_current_thickness_prior and air_count > 0:
        air_upper = max(air_upper, 1.2 * air_max_current)
    air_upper = min(max(air_upper, 1.0), max(8.0, 0.75 * target_total))
    if constraints:
        air_upper = min(air_upper, max(air_lower, _lensde_configuration.finite_float(constraints.get('air_center_thickness_upper'), air_upper)))
    air_upper = max(air_lower, air_upper)
    bfl_lower_factor = _lensde_populations.clamp_value(_lensde_configuration.finite_float(bfl_lower_factor, 0.9), 0.0, 1.0)
    bfl_upper_factor = max(1.0, _lensde_configuration.finite_float(bfl_upper_factor, 1.5))
    if target_bfl_value > 0.0:
        bfl_lower = max(0.0, target_bfl_value * bfl_lower_factor)
        bfl_upper = max(bfl_lower, target_bfl_value * bfl_upper_factor)
    else:
        bfl_lower = max(0.0, bfl_current * bfl_lower_factor)
        bfl_upper = max(bfl_lower, bfl_current * bfl_upper_factor)
    return {'air_lower': air_lower, 'air_upper': air_upper, 'glass_lower': glass_lower, 'glass_upper': glass_upper, 'bfl_lower': bfl_lower, 'bfl_upper': bfl_upper, 'bfl_surface_index': resolve_bfl_reference_surface(system, bfl_surface_index), 'bfl_interval_indices': sorted(bfl_indices), 'use_current_thickness_prior': bool(use_current_thickness_prior), 'seed_air_cap': max(air_lower, min(air_upper, max(air_lower, 1.0, (0.16 + 0.24 * length_longness) * body_budget))), 'seed_glass_cap': max(glass_lower, min(glass_upper, max(glass_lower, 0.25, (0.05 + 0.04 * length_longness) * body_budget))), 'ttl_efl_ratio': ttl_efl_ratio, 'length_longness': length_longness}


def adaptive_interval_bounds(system: dict, surface_index: int, thickness_budget: dict, *, bfl_surface_index: int | None=None) -> tuple[float, float]:
    """Return an explainable per-interval box around the coupled budget.

    BFL may span several intervals.  Each member keeps its material-specific
    lower bound, while its independent upper is the aggregate BFL upper minus
    the other members' lower bounds.  The coupled seed projector remains the
    authoritative constraint on their sum.
    """
    index = int(surface_index)
    is_glass = _lensde_populations.surface_after_is_glass(system, index)
    lower_key = 'glass_lower' if is_glass else 'air_lower'
    upper_key = 'glass_upper' if is_glass else 'air_upper'
    lower = max(0.0, _lensde_configuration.finite_float(thickness_budget.get(lower_key), 0.3 if is_glass else 0.1))
    upper = max(lower, _lensde_configuration.finite_float(thickness_budget.get(upper_key), 6.0 if is_glass else 20.0))
    bfl_indices = bfl_interval_indices(system, bfl_surface_index)
    if index not in bfl_indices:
        return (lower, upper)
    if len(bfl_indices) == 1:
        lower = max(lower, _lensde_configuration.finite_float(thickness_budget.get('bfl_lower'), lower))
    aggregate_upper = max(lower, _lensde_configuration.finite_float(thickness_budget.get('bfl_upper'), upper))
    other_lower = 0.0
    for other in bfl_indices:
        if other == index:
            continue
        other_is_glass = _lensde_populations.surface_after_is_glass(system, other)
        other_lower += max(0.0, _lensde_configuration.finite_float(thickness_budget.get('glass_lower' if other_is_glass else 'air_lower'), 0.3 if other_is_glass else 0.1))
    return (lower, max(lower, aggregate_upper - other_lower))


def primary_wavelength(system: dict) -> dict:
    waves = system.get('wavelengths') or [{'value_um': 0.5876, 'weight': 1.0, 'primary': True}]
    for wave in waves:
        if wave.get('primary'):
            return copy.deepcopy(wave)
    return copy.deepcopy(waves[0])


def central_field(system: dict) -> dict:
    fields = system.get('fields') or [{'hx': 0.0, 'hy': 0.0, 'fvcx': 0.0, 'fvcy': 0.0, 'fvdx': 0.0, 'fvdy': 0.0}]
    for field in fields:
        if abs(_lensde_configuration.finite_float(field.get('hx'), 0.0)) < 1e-12 and abs(_lensde_configuration.finite_float(field.get('hy'), 0.0)) < 1e-12:
            return copy.deepcopy(field)
    return copy.deepcopy(fields[0])


def make_first_order_system(project: dict, *, full_field: bool=False) -> dict:
    system = copy.deepcopy(project.get('system', {}))
    if not full_field:
        system['fields'] = [central_field(system)]
        system['wavelengths'] = [primary_wavelength(system)]
    return system


def variable_key(variable: dict) -> tuple[str, int, int]:
    return (str(variable.get('kind', '')), int(variable.get('index', 0) or 0), int(variable.get('term', 0) or 0))


def variable_list_has_asphere_coeff(variables: list[dict] | dict | None) -> bool:
    if isinstance(variables, dict):
        variables = variables.get('variables', [])
    if not isinstance(variables, list):
        return False
    return any((str(variable.get('kind', '')).lower() == 'asphere_coeff' for variable in variables))


def read_variables_for_scaling(path: Path | None, fallback: list[dict] | None=None) -> list[dict]:
    if path is not None:
        try:
            data = _lensde_provenance.read_json(path)
            variables = data.get('variables', data) if isinstance(data, dict) else data
            if isinstance(variables, list):
                return variables
        except Exception:
            pass
    return fallback or []


def canonical_optimizer_internal_scaling(value, default: str='legacy_optical') -> str:
    token = str(value if value is not None else default).strip().lower().replace('-', '_').replace(' ', '_')
    if token in {'edge_sag', 'edge_sag_normalized', 'asphere_edge_sag', 'asphere_edge_sag_normalized'}:
        return 'edge_sag_normalized'
    if token in {'bound', 'normalized', 'bound_normalized'}:
        return 'bound_normalized'
    if token in {'legacy', 'legacy_optical', 'optical'}:
        return 'legacy_optical'
    if token == 'auto':
        return 'auto'
    return default


def effective_optimizer_internal_scaling(options: dict, variables: list[dict] | dict | None, key: str='optimizer_internal_scaling', default: str='legacy_optical') -> str:
    scaling = canonical_optimizer_internal_scaling(options.get(key, default), default)
    has_asphere = variable_list_has_asphere_coeff(variables)
    if scaling == 'auto':
        return 'edge_sag_normalized' if has_asphere else default
    if has_asphere and scaling in {'legacy_optical', 'bound_normalized'}:
        return 'edge_sag_normalized'
    return scaling


def clamped_bounds(lower: float, upper: float, limit_lower: float, limit_upper: float) -> tuple[float, float]:
    lo = max(_lensde_configuration.finite_float(lower, limit_lower), limit_lower)
    hi = min(_lensde_configuration.finite_float(upper, limit_upper), limit_upper)
    if lo >= hi:
        return (limit_lower, limit_upper)
    return (lo, hi)


def optional_finite_float(value) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def field_radius(field: dict) -> float:
    return math.hypot(_lensde_configuration.finite_float(field.get('hx'), 0.0), _lensde_configuration.finite_float(field.get('hy'), 0.0))
