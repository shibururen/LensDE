"""Extracted paper engine: configuration."""
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

def canonical_aiming_mode(value: object, fallback: str='robust') -> str:
    mode = str(value or fallback).strip().lower().replace('-', '_').replace(' ', '_')
    if mode in {'adaptive', 'adaptive_pilot', 'pilot_grid'}:
        return 'adaptive_pilot_grid'
    if mode in {'calibrated_paraxial_fast', 'calibrated_paraxial_auto', 'calibrated_paraxial_corrected', 'field_pupil_plane', 'field_calibrated_paraxial'}:
        return 'calibrated_paraxial'
    if mode in {'three_step_ray_aiming', 'gao_three_step', 'gao_2025', 'gaoneuro', 'opti_neuro', 'optineuro'}:
        return 'three_step'
    if mode in {'direct', 'paraxial', 'calibrated_paraxial', 'three_step', 'iterative', 'robust', 'adaptive_pilot_grid'}:
        return mode
    return fallback


def canonical_stop_radius_source(value: object, fallback: str='paraxial') -> str:
    raw = str(value if value is not None else fallback).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'': fallback, 'near_axis': 'paraxial', 'paraxial_ray': 'paraxial', 'paraxial_rays': 'paraxial', 'paraxial_stop': 'paraxial', 'paraxial_stop_radius': 'paraxial', 'actual': 'real', 'actual_ray': 'real', 'real_ray': 'real', 'real_rays': 'real', 'real_stop': 'real', 'real_stop_radius': 'real'}
    raw = aliases.get(raw, raw)
    allowed = {'paraxial', 'real'}
    return raw if raw in allowed else fallback


def sync_unified_ray_aiming_options(options: dict) -> str:
    """Copy the visible GUI aiming policy into legacy workflow fields."""
    mode = canonical_aiming_mode(options.get('aiming'), 'robust')
    options['aiming'] = mode
    for name in state.UNIFIED_AIMING_OPTION_NAMES:
        if name in options:
            options[f'initial_search_first_order_{name}'] = copy.deepcopy(options[name])
    options['initial_search_first_order_aiming'] = mode
    return mode


def canonical_population_generation_mode(value: object, fallback: str='random') -> str:
    mode = str(value or fallback).strip().lower().replace('-', '_').replace(' ', '_')
    if mode in {'restricted', 'limited', 'limited_perturbation', 'restricted_perturbation'}:
        return 'restricted_perturbation'
    if mode in {'physics_constrained', 'physical_constraints', 'manufacturing_constrained', 'manufacturing_constraints', 'opti_neuro', 'optineuro', 'gao_2025', 'gao_physics'}:
        return 'physics_constrained'
    return 'random'


def is_adaptive_aiming(value: object) -> bool:
    return canonical_aiming_mode(value, '') == 'adaptive_pilot_grid'


def is_calibrated_aiming(value: object) -> bool:
    return canonical_aiming_mode(value, '') == 'calibrated_paraxial'


def is_three_step_aiming(value: object) -> bool:
    return canonical_aiming_mode(value, '') == 'three_step'


def canonical_calibrated_fallback(value: object, fallback: str='adaptive_pilot_grid') -> str:
    raw = str(value if value is not None else fallback).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'': fallback, 'off': 'none', 'disabled': 'none', 'fail': 'none', 'failure': 'none', 'adaptive': 'adaptive_pilot_grid', 'adaptive_pilot': 'adaptive_pilot_grid', 'pilot_grid': 'adaptive_pilot_grid'}
    raw = aliases.get(raw, raw)
    allowed = {'none', 'adaptive_pilot_grid', 'iterative', 'robust', 'direct', 'paraxial'}
    return raw if raw in allowed else fallback


def canonical_calibrated_chief_mode(value: object, fallback: str='abcd_budget_angle_bridge_fast') -> str:
    raw = str(value if value is not None else fallback).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'': fallback, 'full': 'full_search', 'fullsearch': 'full_search', 'search': 'full_search', 'budget': 'abcd_budget', 'budget_abcd': 'abcd_budget', 'budgeted_abcd': 'abcd_budget', 'inward': 'abcd_budget_inward_wide', 'inward_wide': 'abcd_budget_inward_wide', 'wide_inward': 'abcd_budget_inward_wide', 'field_cont': 'abcd_budget_field_continuation', 'field_continuation': 'abcd_budget_field_continuation', 'field_continuation_75': 'abcd_budget_field_continuation', 'abcd_field_continuation': 'abcd_budget_field_continuation', 'budget_field_continuation': 'abcd_budget_field_continuation', 'field_extrapolate': 'abcd_budget_field_extrapolate', 'abcd_field_extrapolate': 'abcd_budget_field_extrapolate', 'angle_bridge': 'abcd_budget_angle_bridge_fast', 'angle_bridge_fast': 'abcd_budget_angle_bridge_fast', 'abcd_angle_bridge': 'abcd_budget_angle_bridge_fast', 'abcd_angle_bridge_fast': 'abcd_budget_angle_bridge_fast', 'budget_angle_bridge': 'abcd_budget_angle_bridge_fast', 'budget_angle_bridge_fast': 'abcd_budget_angle_bridge_fast', 'angle_bridge_then_full': 'abcd_budget_angle_bridge_fast_then_full', 'angle_bridge_fast_then_full': 'abcd_budget_angle_bridge_fast_then_full', 'budget_angle_bridge_then_full': 'abcd_budget_angle_bridge_fast_then_full', 'budget_angle_bridge_fast_then_full': 'abcd_budget_angle_bridge_fast_then_full', 'budget_then_full': 'abcd_budget_then_full', 'then_full': 'abcd_budget_then_full', 'field_continuation_then_full': 'abcd_budget_field_continuation_then_full', 'budget_field_continuation_then_full': 'abcd_budget_field_continuation_then_full', 'field_then_full': 'abcd_budget_field_continuation_then_full'}
    raw = aliases.get(raw, raw)
    allowed = {'full_search', 'none', 'off', 'disabled', 'abcd_budget', 'budget_abcd', 'budgeted_abcd', 'abcd_budget_inward', 'budget_abcd_inward', 'abcd_budget_center_inward', 'budget_abcd_center_inward', 'abcd_budget_inward_wide', 'budget_abcd_inward_wide', 'abcd_budget_center_inward_wide', 'budget_abcd_center_inward_wide', 'abcd_budget_field_continuation', 'budget_abcd_field_continuation', 'abcd_budget_field_extrapolate', 'budget_abcd_field_extrapolate', 'abcd_budget_angle_bridge_fast', 'budget_abcd_angle_bridge_fast', 'abcd_budget_angle_bridge_fast_then_full', 'budget_abcd_angle_bridge_fast_then_full', 'abcd_budget_field_continuation_then_full', 'budget_abcd_field_continuation_then_full', 'abcd_budget_then_full', 'budget_abcd_then_full'}
    return raw if raw in allowed else fallback


def canonical_calibrated_sampling_mode(value: object, fallback: str='central_fit') -> str:
    raw = str(value if value is not None else fallback).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'': fallback, 'central': 'central_fit', 'jacobian': 'central_fit', 'central_jacobian': 'central_fit', 'central_fit_edge_correction': 'central_fit', 'chief_edge': 'chief_edge_interpolation', 'chief_edge_interp': 'chief_edge_interpolation', 'chief_edge_interpolate': 'chief_edge_interpolation', 'edge_interpolation': 'chief_edge_interpolation', 'geometric': 'chief_edge_interpolation', 'geometric_interpolation': 'chief_edge_interpolation', 'interpolation': 'chief_edge_interpolation'}
    raw = aliases.get(raw, raw)
    allowed = {'central_fit', 'chief_edge_interpolation'}
    return raw if raw in allowed else fallback


def canonical_calibrated_budget_solver(value: object, fallback: str='secant') -> str:
    raw = str(value if value is not None else fallback).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'': fallback, 'legacy': 'iterative', 'legacy_iterative': 'iterative', 'newton': 'iterative', 'solve_stop_aim': 'iterative', 'stop_iterative': 'iterative', 'fixed': 'fixed_jacobian', 'linear': 'fixed_jacobian', 'one_step': 'fixed_jacobian', 'fixed_jacobian_only': 'fixed_jacobian', 'broyden': 'secant', 'broyden_only': 'secant', 'secant_only': 'secant'}
    raw = aliases.get(raw, raw)
    allowed = {'hybrid', 'iterative', 'secant', 'fixed_jacobian'}
    return raw if raw in allowed else fallback


def canonical_aiming_edge_solver(value: object, fallback: str='secant') -> str:
    raw = str(value if value is not None else fallback).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'': fallback, 'legacy': 'iterative', 'legacy_iterative': 'iterative', 'newton': 'iterative', 'solve_stop_aim': 'iterative', 'fixed': 'fixed_jacobian', 'linear': 'fixed_jacobian', 'one_step': 'fixed_jacobian', 'fixed_jacobian_only': 'fixed_jacobian', 'broyden': 'secant', 'broyden_only': 'secant', 'secant_only': 'secant', 'fixed_then_iterative': 'fixed_jacobian_then_iterative', 'linear_then_iterative': 'fixed_jacobian_then_iterative', 'broyden_then_iterative': 'secant_then_iterative'}
    raw = aliases.get(raw, raw)
    allowed = {'iterative', 'secant', 'fixed_jacobian', 'fixed_jacobian_then_iterative', 'secant_then_iterative'}
    return raw if raw in allowed else fallback


def canonical_calibrated_budget_acceptance(value: object, fallback: str='residual') -> str:
    raw = str(value if value is not None else fallback).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'': fallback, 'stop': 'inside_stop', 'stop_inside': 'inside_stop', 'loose_stop': 'residual'}
    raw = aliases.get(raw, raw)
    allowed = {'residual', 'inside_stop', 'loose_stop', 'physical_stop'}
    return raw if raw in allowed else fallback


def aiming_option(options: dict, prefix: str, name: str, fallback):
    _ = prefix
    return options.get(name, fallback)


def sync_options_from_system_ray_aiming(project: dict) -> None:
    system = project.get('system', {})
    ray_aiming = system.get('ray_aiming')
    if not isinstance(ray_aiming, dict):
        return
    options = project.setdefault('options', {})
    options['aiming'] = canonical_aiming_mode(ray_aiming.get('mode'), options.get('aiming', 'robust'))
    if 'stop_radius_source' in ray_aiming:
        options['aiming_stop_radius_source'] = canonical_stop_radius_source(ray_aiming.get('stop_radius_source'), options.get('aiming_stop_radius_source', 'paraxial'))
    if 'max_iter' in ray_aiming:
        options['aiming_iter'] = int(ray_aiming.get('max_iter') or options.get('aiming_iter', 20))
    if 'require_convergence' in ray_aiming:
        options['aiming_require_convergence'] = bool(ray_aiming.get('require_convergence'))
    if 'continuation_steps' in ray_aiming:
        options['aiming_continuation_steps'] = int(ray_aiming.get('continuation_steps') or 8)
    calibrated = ray_aiming.get('calibrated_paraxial')
    if isinstance(calibrated, dict):
        if 'correction_iterations' in calibrated:
            options['aiming_calibrated_correction_iterations'] = int(calibrated.get('correction_iterations') or 0)
        if 'sampling_mode' in calibrated:
            options['aiming_calibrated_sampling_mode'] = canonical_calibrated_sampling_mode(calibrated.get('sampling_mode'), options.get('aiming_calibrated_sampling_mode', 'chief_edge_interpolation'))
        if 'fallback' in calibrated:
            options['aiming_calibrated_fallback'] = canonical_calibrated_fallback(calibrated.get('fallback'), options.get('aiming_calibrated_fallback', 'none'))
        if 'chief_anchor_mode' in calibrated:
            options['aiming_calibrated_chief_mode'] = canonical_calibrated_chief_mode(calibrated.get('chief_anchor_mode'), options.get('aiming_calibrated_chief_mode', 'abcd_budget_angle_bridge_fast'))
        if 'budget_chief_iterations' in calibrated:
            options['aiming_calibrated_budget_chief_iter'] = int(calibrated.get('budget_chief_iterations') or 8)
        if 'budget_chief_solver' in calibrated:
            options['aiming_calibrated_budget_chief_solver'] = canonical_calibrated_budget_solver(calibrated.get('budget_chief_solver'), options.get('aiming_calibrated_budget_chief_solver', 'hybrid'))
        if 'budget_chief_acceptance' in calibrated:
            options['aiming_calibrated_budget_chief_acceptance'] = canonical_calibrated_budget_acceptance(calibrated.get('budget_chief_acceptance'), options.get('aiming_calibrated_budget_chief_acceptance', 'residual'))
        if 'budget_chief_accept_stop_fraction' in calibrated:
            options['aiming_calibrated_budget_chief_stop_fraction'] = float(calibrated.get('budget_chief_accept_stop_fraction') or 0.05)
        if 'budget_chief_residual_stop_fraction' in calibrated:
            options['aiming_calibrated_budget_chief_residual_stop_fraction'] = float(calibrated.get('budget_chief_residual_stop_fraction') or 1e-06)
        if 'budget_chief_min_a' in calibrated:
            options['aiming_calibrated_budget_chief_min_a'] = float(calibrated.get('budget_chief_min_a') or 1e-06)
        if 'budget_chief_field_continuation_angle_deg' in calibrated:
            options['aiming_calibrated_budget_chief_field_angle'] = float(calibrated.get('budget_chief_field_continuation_angle_deg') or 75.0)
        if 'budget_chief_ignore_aperture_clip' in calibrated:
            options['aiming_calibrated_budget_chief_ignore_aperture_clip'] = bool(calibrated.get('budget_chief_ignore_aperture_clip'))
        if 'budget_chief_skip_abcd_angle_deg' in calibrated:
            options['aiming_calibrated_budget_chief_skip_abcd_angle'] = float(calibrated.get('budget_chief_skip_abcd_angle_deg') or 60.0)
        if 'edge_calibration_enabled' in calibrated:
            options['aiming_calibrated_edge_calibration_enabled'] = bool(calibrated.get('edge_calibration_enabled'))
        if 'edge_calibration_iterations' in calibrated:
            options['aiming_calibrated_edge_calibration_iterations'] = int(calibrated.get('edge_calibration_iterations') or 8)
        if 'edge_calibration_solver' in calibrated:
            options['aiming_calibrated_edge_calibration_solver'] = canonical_aiming_edge_solver(calibrated.get('edge_calibration_solver'), options.get('aiming_calibrated_edge_calibration_solver', 'secant'))
        if 'edge_calibration_weight_power' in calibrated:
            options['aiming_calibrated_edge_calibration_weight_power'] = float(calibrated.get('edge_calibration_weight_power') or 2.0)
        if 'edge_calibration_residual_stop_fraction' in calibrated:
            options['aiming_calibrated_edge_calibration_residual_stop_fraction'] = float(calibrated.get('edge_calibration_residual_stop_fraction') or 1e-06)
    three_step = ray_aiming.get('three_step')
    if isinstance(three_step, dict):
        if 'sampling_mode' in three_step:
            options['aiming_three_step_sampling_mode'] = canonical_calibrated_sampling_mode(three_step.get('sampling_mode'), options.get('aiming_three_step_sampling_mode', 'chief_edge_interpolation'))
        if 'chief_reverse_trial_rays' in three_step:
            options['aiming_three_step_chief_rays'] = int(three_step.get('chief_reverse_trial_rays'))
        if 'chief_reverse_max_angle_deg' in three_step:
            options['aiming_three_step_max_angle'] = float(three_step.get('chief_reverse_max_angle_deg'))
        if 'edge_passes' in three_step:
            options['aiming_three_step_edge_passes'] = int(three_step.get('edge_passes'))
        if 'meridional_iterations' in three_step:
            options['aiming_three_step_meridional_iter'] = int(three_step.get('meridional_iterations'))
        if 'sagittal_iterations' in three_step:
            options['aiming_three_step_sagittal_iter'] = int(three_step.get('sagittal_iterations'))
        if 'edge_solver' in three_step:
            options['aiming_three_step_edge_solver'] = canonical_aiming_edge_solver(three_step.get('edge_solver'), options.get('aiming_three_step_edge_solver', 'secant'))
        if 'edge_residual_stop_fraction' in three_step:
            options['aiming_three_step_edge_residual_stop_fraction'] = float(three_step.get('edge_residual_stop_fraction') or 1e-06)
        if 'edge_initial_radius_fraction' in three_step:
            options['aiming_three_step_edge_radius_fraction'] = float(three_step.get('edge_initial_radius_fraction'))
        if 'accept_stop_fraction' in three_step:
            options['aiming_three_step_accept_stop_fraction'] = float(three_step.get('accept_stop_fraction'))
        if 'correction_iterations' in three_step:
            options['aiming_three_step_correction_iterations'] = int(three_step.get('correction_iterations'))
        if 'sample_stop_residual_debug' in three_step:
            options['aiming_three_step_sample_stop_residual_debug'] = bool(three_step.get('sample_stop_residual_debug'))
        if 'fallback' in three_step:
            options['aiming_three_step_fallback'] = canonical_calibrated_fallback(three_step.get('fallback'), options.get('aiming_three_step_fallback', 'adaptive_pilot_grid'))
    adaptive = ray_aiming.get('adaptive_pilot_grid')
    if isinstance(adaptive, dict):
        if 'fine_grid_n' in adaptive:
            options['aiming_pilot_fine_grid'] = int(adaptive.get('fine_grid_n') or 5)
        if 'coarse_grid_n' in adaptive:
            options['aiming_pilot_coarse_grid'] = int(adaptive.get('coarse_grid_n') or 13)
        if 'nearest_k' in adaptive:
            options['aiming_pilot_nearest_k'] = int(adaptive.get('nearest_k') or 3)
        if 'iterative_failure_sample_limit' in adaptive:
            options['aiming_pilot_iterative_failure_limit'] = int(adaptive.get('iterative_failure_sample_limit') or 0)
        if 'try_iterative_for_each_ray' in adaptive:
            options['aiming_pilot_per_ray_iterative'] = bool(adaptive.get('try_iterative_for_each_ray'))
        if 'allow_barycentric_interpolation' in adaptive:
            options['aiming_pilot_allow_interpolation'] = bool(adaptive.get('allow_barycentric_interpolation'))


def ray_aiming_config_from_options(options: dict, *, prefix: str='') -> dict:
    mode = canonical_aiming_mode(options.get('aiming', 'robust'))
    stop_radius_source = canonical_stop_radius_source(aiming_option(options, prefix, 'aiming_stop_radius_source', 'paraxial'), 'paraxial')
    max_iter = int(options.get('aiming_iter', 20) or 20)
    require = bool(options.get('aiming_require_convergence', False))
    steps = int(options.get('aiming_continuation_steps', 8) or 8)
    config = {'mode': mode, 'stop_radius_source': stop_radius_source, 'max_iter': max(1, max_iter), 'tolerance': 1e-08, 'start_offset': -1, 'require_convergence': require, 'continuation_steps': max(1, steps)}
    if is_calibrated_aiming(mode):
        calibrated_fallback_default = 'none'
        calibrated_chief_default = 'abcd_budget_angle_bridge_fast'
        budget_solver_default = 'hybrid'
        config['calibrated_paraxial'] = {'enabled': True, 'sampling_mode': canonical_calibrated_sampling_mode(aiming_option(options, prefix, 'aiming_calibrated_sampling_mode', 'chief_edge_interpolation'), 'chief_edge_interpolation'), 'allow_grid_upgrade': True, 'pilot_grid_n': 3, 'jacobian_probe_fraction': 0.02, 'min_probe_step_mm': 0.03, 'max_probe_step_mm': 0.3, 'max_condition_number': 1000.0, 'fit_rms_tolerance_stop_fraction': 0.03, 'validation_sample_count': 5, 'validation_residual_stop_fraction': 0.05, 'use_previous_field_model': True, 'use_previous_wave_model': True, 'fallback': canonical_calibrated_fallback(aiming_option(options, prefix, 'aiming_calibrated_fallback', calibrated_fallback_default), calibrated_fallback_default), 'correction_iterations': max(0, int(aiming_option(options, prefix, 'aiming_calibrated_correction_iterations', 0) or 0)), 'chief_anchor_mode': canonical_calibrated_chief_mode(aiming_option(options, prefix, 'aiming_calibrated_chief_mode', calibrated_chief_default), calibrated_chief_default), 'budget_chief_iterations': max(0, int(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_iter', 8) or 8)), 'budget_chief_solver': canonical_calibrated_budget_solver(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_solver', budget_solver_default), budget_solver_default), 'budget_chief_acceptance': canonical_calibrated_budget_acceptance(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_acceptance', 'residual')), 'budget_chief_accept_stop_fraction': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_stop_fraction', 0.05), 0.05)), 'budget_chief_residual_stop_fraction': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_residual_stop_fraction', 1e-06), 1e-06)), 'budget_chief_min_a': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_min_a', 1e-06), 1e-06)), 'budget_chief_field_continuation_angle_deg': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_field_angle', 75.0), 75.0)), 'budget_chief_ignore_aperture_clip': bool(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_ignore_aperture_clip', True)), 'budget_chief_skip_abcd_angle_deg': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_calibrated_budget_chief_skip_abcd_angle', 60.0), 60.0)), 'edge_calibration_enabled': bool(aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_enabled', True)), 'edge_calibration_iterations': max(0, int(aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_iterations', 8) or 8)), 'edge_calibration_solver': canonical_aiming_edge_solver(aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_solver', 'secant'), 'secant'), 'edge_calibration_weight_power': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_weight_power', 2.0), 2.0)), 'edge_calibration_residual_stop_fraction': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_calibrated_edge_calibration_residual_stop_fraction', 1e-06), 1e-06))}
    if is_three_step_aiming(mode):
        three_step_fallback_default = 'adaptive_pilot_grid'
        config['three_step'] = {'enabled': True, 'sampling_mode': canonical_calibrated_sampling_mode(aiming_option(options, prefix, 'aiming_three_step_sampling_mode', 'chief_edge_interpolation'), 'chief_edge_interpolation'), 'chief_reverse_trial_rays': max(3, int(finite_float(aiming_option(options, prefix, 'aiming_three_step_chief_rays', 33), 33))), 'chief_reverse_max_angle_deg': max(0.1, min(89.9, finite_float(aiming_option(options, prefix, 'aiming_three_step_max_angle', 89.0), 89.0))), 'edge_passes': max(1, int(finite_float(aiming_option(options, prefix, 'aiming_three_step_edge_passes', 2), 2))), 'meridional_iterations': max(0, int(finite_float(aiming_option(options, prefix, 'aiming_three_step_meridional_iter', 8), 8))), 'sagittal_iterations': max(0, int(finite_float(aiming_option(options, prefix, 'aiming_three_step_sagittal_iter', 8), 8))), 'edge_solver': canonical_aiming_edge_solver(aiming_option(options, prefix, 'aiming_three_step_edge_solver', 'secant'), 'secant'), 'edge_residual_stop_fraction': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_three_step_edge_residual_stop_fraction', 1e-06), 1e-06)), 'edge_initial_radius_fraction': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_three_step_edge_radius_fraction', 1.0), 1.0)), 'accept_stop_fraction': max(0.0, finite_float(aiming_option(options, prefix, 'aiming_three_step_accept_stop_fraction', 0.05), 0.05)), 'correction_iterations': max(0, int(finite_float(aiming_option(options, prefix, 'aiming_three_step_correction_iterations', 0), 0))), 'sample_stop_residual_debug': bool(aiming_option(options, prefix, 'aiming_three_step_sample_stop_residual_debug', False)), 'fallback': canonical_calibrated_fallback(aiming_option(options, prefix, 'aiming_three_step_fallback', three_step_fallback_default), three_step_fallback_default)}
    if is_adaptive_aiming(mode):
        config['require_convergence'] = True if require else config['require_convergence']
        config['adaptive_pilot_grid'] = {'enabled': True, 'try_iterative_first': True, 'try_iterative_for_each_ray': bool(aiming_option(options, prefix, 'aiming_pilot_per_ray_iterative', True)), 'iterative_failure_sample_limit': max(0, int(aiming_option(options, prefix, 'aiming_pilot_iterative_failure_limit', 0) or 0)), 'require_final_convergence': True, 'target_field_direction_tolerance': 1e-10, 'use_chief_gate': True, 'use_primary_wave_as_anchor': True, 'use_previous_field_chief': True, 'use_previous_field_extrapolation': True, 'max_field_extrapolation_factor': 2.0, 'use_previous_wave_seed': True, 'use_previous_field_seed': True, 'use_previous_pupil_seed': True, 'use_chief_jacobian_seed': True, 'coarse_grid_n': max(3, int(aiming_option(options, prefix, 'aiming_pilot_coarse_grid', 13) or 13)), 'fine_grid_n': max(3, int(aiming_option(options, prefix, 'aiming_pilot_fine_grid', 5) or 5)), 'nearest_k': max(1, int(aiming_option(options, prefix, 'aiming_pilot_nearest_k', 3) or 3)), 'max_coarse_expansions': 3, 'max_fine_expansions': 2, 'coarse_min_halfwidth_fraction': 0.1, 'coarse_max_halfwidth_fraction': 0.6, 'fine_min_halfwidth_fraction': 0.03, 'fine_max_halfwidth_fraction': 0.25, 'fine_default_halfwidth_fraction': 0.06, 'jacobian_probe_fraction': 0.03, 'jacobian_safety_factor': 1.4, 'allow_delaunay_interpolation': False, 'allow_barycentric_interpolation': bool(aiming_option(options, prefix, 'aiming_pilot_allow_interpolation', False)), 'use_multistart': True, 'emit_diagnostics': True}
    return config


def system_with_ray_aiming(system: dict, options: dict, *, prefix: str='') -> dict:
    out = copy.deepcopy(system)
    existing = out.get('ray_aiming')
    config = ray_aiming_config_from_options(options, prefix=prefix)
    if isinstance(existing, dict):
        config = {**existing, **config}
        if is_adaptive_aiming(config.get('mode')):
            adaptive = existing.get('adaptive_pilot_grid') if isinstance(existing.get('adaptive_pilot_grid'), dict) else {}
            config['adaptive_pilot_grid'] = {**adaptive, **ray_aiming_config_from_options(options, prefix=prefix).get('adaptive_pilot_grid', {})}
        if is_calibrated_aiming(config.get('mode')):
            calibrated = existing.get('calibrated_paraxial') if isinstance(existing.get('calibrated_paraxial'), dict) else {}
            config['calibrated_paraxial'] = {**calibrated, **ray_aiming_config_from_options(options, prefix=prefix).get('calibrated_paraxial', {})}
        if is_three_step_aiming(config.get('mode')):
            three_step = existing.get('three_step') if isinstance(existing.get('three_step'), dict) else {}
            config['three_step'] = {**three_step, **ray_aiming_config_from_options(options, prefix=prefix).get('three_step', {})}
    out['ray_aiming'] = config
    return out


def ensure_glass_surface_config(glass_search: dict, surface_index: int) -> dict:
    if not isinstance(glass_search.get('surface_configs'), list):
        glass_search['surface_configs'] = []
    for item in glass_search['surface_configs']:
        if not isinstance(item, dict):
            continue
        try:
            if int(item.get('surface_index', -1)) == int(surface_index):
                return item
        except (TypeError, ValueError):
            continue
    default_libraries = glass_search.get('default_libraries') or ['SCHOTT']
    item = {'surface_index': int(surface_index), 'enabled': False, 'libraries': list(default_libraries), 'max_candidates': int(glass_search.get('max_candidates_per_surface', 64) or 64)}
    glass_search['surface_configs'].append(item)
    return item


def ensure_conic_coefficient_merit_bounds(project: dict) -> None:
    """Materialize the global conic guard as ordinary merit bounds.

    Keeping the generated constraints in ``merit.bounds`` makes the penalty
    visible in score contributions and preserves CPU/CUDA scoring parity.
    Explicit user-authored conic bounds take precedence surface by surface.
    """
    merit = project.setdefault('merit', {})
    enabled = bool(merit.get('conic_coefficient_penalty_enabled', True))
    lower = finite_float(merit.get('conic_coefficient_lower'), -100.0)
    upper = finite_float(merit.get('conic_coefficient_upper'), 100.0)
    if lower > upper:
        lower, upper = (upper, lower)
    if upper - lower <= 1e-12:
        lower, upper = (-100.0, 100.0)
    weight = max(0.0, finite_float(merit.get('conic_coefficient_penalty_weight'), 5.0))
    merit['conic_coefficient_penalty_enabled'] = enabled
    merit['conic_coefficient_lower'] = lower
    merit['conic_coefficient_upper'] = upper
    merit['conic_coefficient_penalty_weight'] = weight
    merit['quality_conic_coefficient_tol'] = max(1e-09, finite_float(merit.get('quality_conic_coefficient_tol'), 1.0))
    raw_bounds = merit.get('bounds', [])
    bounds = [dict(item) for item in raw_bounds if isinstance(item, dict)]
    bounds = [item for item in bounds if str(item.get('source') or '') != state.AUTO_CONIC_BOUND_SOURCE]
    explicit_indices: set[int] = set()
    for item in bounds:
        if str(item.get('kind') or '').strip().lower() != 'conic':
            continue
        try:
            explicit_indices.add(int(item.get('index', -1)))
        except (TypeError, ValueError):
            continue
    if enabled and weight > 0.0:
        for index, surface in enumerate(project.get('system', {}).get('surfaces', []) or []):
            if not isinstance(surface, dict):
                continue
            kind = str(surface.get('kind') or '').strip().lower()
            if kind in {'object', 'image'} or (bool(surface.get('is_stop', False)) and (not _lensde_populations.is_physical_stop_surface(surface))):
                continue
            if index in explicit_indices:
                continue
            bounds.append({'kind': 'conic', 'index': index, 'term': 0, 'lower': lower, 'upper': upper, 'weight': weight, 'source': state.AUTO_CONIC_BOUND_SOURCE})
    merit['bounds'] = bounds


def vignetting_minimum_relative_illumination(contract: dict | float | None) -> float:
    if isinstance(contract, dict):
        raw = contract.get('minimum_relative_illumination', contract.get('vignetting_minimum_relative_illumination', 0.0))
    else:
        raw = contract
    return min(1.0, max(0.0, finite_float(raw, 0.0)))


def vignetting_optimization_enabled(merit: dict | None) -> bool:
    minimum = vignetting_minimum_relative_illumination(merit)
    return 0.0 < minimum <= 1.0


def outer_physical_cut_staging_enabled(merit: dict | None) -> bool:
    """Use loose/strict Outer phases whenever illumination is optimized.

    Zero disables illumination and uses one phase. Any positive illumination
    floor enables both phases; it does not need to request full illumination.
    """
    return vignetting_optimization_enabled(merit)


def automatic_vignetting_field_budget(system: dict, field_index: int, illumination_contract: dict | float | None) -> tuple[float, float, float]:
    """Return normalized radius, edge budget, and initial-search field budget.

    This envelope is not used as the hard domain after a vignetting handoff;
    ensure_vignetting_variables_for_system(handoff=True) uses [0, 1] instead.

    Two thirds of the allowed illumination loss is assigned to controllable
    meridional vignetting; the rest is reserved for natural illumination and
    pupil/clipping losses.  A quadratic envelope is a permissive numerical
    safety boundary.  The former quartic envelope is retained separately as a
    merit prior, not a hard search-space exclusion.
    """
    minimum = vignetting_minimum_relative_illumination(illumination_contract)
    fields = system.get('fields', []) if isinstance(system, dict) else []
    if not 0.0 < minimum <= 1.0 or not isinstance(fields, list):
        return (0.0, 0.0, 0.0)
    radii = [math.hypot(finite_float(field.get('hx'), 0.0), finite_float(field.get('hy'), 0.0)) for field in fields if isinstance(field, dict)]
    maximum_radius = max(radii, default=0.0)
    if not 0 <= field_index < len(fields) or maximum_radius <= 1e-12:
        return (0.0, 0.0, 0.0)
    field = fields[field_index]
    if not isinstance(field, dict):
        return (0.0, 0.0, 0.0)
    radius = math.hypot(finite_float(field.get('hx'), 0.0), finite_float(field.get('hy'), 0.0))
    rho = min(1.0, max(0.0, radius / maximum_radius))
    contract_dict = illumination_contract if isinstance(illumination_contract, dict) else {}
    edge_cap = min(1.0, max(0.0, finite_float(contract_dict.get('vignetting_edge_fvdy_cap'), state.AUTO_VIGNETTING_EDGE_FVDY_CAP)))
    loss_fraction = min(1.0, max(0.0, finite_float(contract_dict.get('vignetting_loss_budget_fraction'), state.AUTO_VIGNETTING_LOSS_BUDGET_FRACTION)))
    hard_power = max(0.0, finite_float(contract_dict.get('vignetting_hard_field_power'), state.AUTO_VIGNETTING_HARD_FIELD_POWER))
    edge_budget = min(edge_cap, loss_fraction * (1.0 - minimum))
    scheduled_budget = edge_budget * rho ** hard_power
    realized_magnitude = max(abs(finite_float(field.get('fvcy'), 0.0)), max(0.0, finite_float(field.get('fvdy', field.get('vignette_y')), 0.0)))
    realized_budget = min(state.AUTO_VIGNETTING_EDGE_FVDY_CAP, state.AUTO_VIGNETTING_REALIZED_HEADROOM * realized_magnitude + state.AUTO_VIGNETTING_REALIZED_HEADROOM_FLOOR)
    field_budget = max(scheduled_budget, realized_budget)
    return (rho, edge_budget, field_budget)


def canonical_vignetting_variable_kind(kind: object) -> str:
    token = str(kind or '').strip().lower()
    if token in {'fvcy', 'vignette_center_y', 'vignette_shift_y'}:
        return 'fvcy'
    if token in {'fvdy', 'vignette_y'}:
        return 'fvdy'
    if token in {'vignette_magnitude_y', 'vignetting_magnitude_y'}:
        return 'vignette_magnitude_y'
    if token in {'vignette_asymmetry_y', 'vignetting_asymmetry_y'}:
        return 'vignette_asymmetry_y'
    return ''


def is_vignetting_variable(variable: dict | None) -> bool:
    if not isinstance(variable, dict):
        return False
    token = str(variable.get('kind') or '').strip().lower()
    return token in {'fvcx', 'fvcy', 'fvdx', 'fvdy', 'vignette_x', 'vignette_y', 'vignette_center_x', 'vignette_center_y', 'vignette_shift_x', 'vignette_shift_y', 'vignette_magnitude_y', 'vignetting_magnitude_y', 'vignette_asymmetry_y', 'vignetting_asymmetry_y'}


def ensure_vignetting_variables_for_system(system: dict, variables: list[dict] | None, illumination_contract: dict | float | None, *, handoff: bool=False) -> list[dict]:
    """Build a field-adaptive feasible meridional-vignetting contract.

    Automatic fields optimize FVDY magnitude and eta=FVCY/FVDY.  This removes
    |FVCY| > FVDY from the search space while keeping legacy explicit FVCY/FVDY
    variables intact for user-authored contracts.

    After Set Vignetting, the seed's illumination-budget envelope is no longer
    an optimizer bound. Keep the full physical magnitude domain [0, 1]; actual
    illumination and realizability remain merit constraints. Persist this
    policy through subsequent field rebuilds and saved-project normalization.
    """
    handoff = handoff or any((isinstance(variable, dict) and bool(variable.get('auto_vignetting_variable', False)) and (variable.get('vignetting_bound_policy') == state.VIGNETTING_HANDOFF_BOUND_POLICY) for variable in variables or []))
    out = [copy.deepcopy(variable) for variable in variables or [] if isinstance(variable, dict) and (not bool(variable.get('auto_vignetting_variable', False)))]
    minimum = vignetting_minimum_relative_illumination(illumination_contract)
    if not 0.0 < minimum <= 1.0:
        return out
    existing: set[tuple[str, int]] = set()
    explicit_legacy_fields: set[int] = set()
    for variable in out:
        kind = canonical_vignetting_variable_kind(variable.get('kind'))
        if not kind:
            continue
        try:
            variable_index = int(variable.get('index', -1))
            existing.add((kind, variable_index))
            if kind in {'fvcy', 'fvdy'}:
                explicit_legacy_fields.add(variable_index)
        except (TypeError, ValueError):
            continue
    fields = system.get('fields', []) if isinstance(system, dict) else []
    if not isinstance(fields, list):
        return out
    for index, field in enumerate(fields):
        if not isinstance(field, dict):
            continue
        radius = math.hypot(finite_float(field.get('hx'), 0.0), finite_float(field.get('hy'), 0.0))
        if radius <= 1e-12:
            continue
        if index in explicit_legacy_fields:
            continue
        current_fvcy = finite_float(field.get('fvcy'), 0.0)
        current_fvdy = max(0.0, finite_float(field.get('fvdy', field.get('vignette_y')), 0.0))
        projected_fvdy = max(current_fvdy, abs(current_fvcy))
        if projected_fvdy > current_fvdy + 1e-15:
            field['fvdy'] = projected_fvdy
            field['vignette_y'] = projected_fvdy
            audit = system.setdefault('vignetting_projection_audit', [])
            if isinstance(audit, list):
                audit.append({'field_index': index, 'reason': 'project_abs_fvcy_le_fvdy', 'source_fvcy': current_fvcy, 'source_fvdy': current_fvdy, 'projected_fvcy': current_fvcy, 'projected_fvdy': projected_fvdy})
            current_fvdy = projected_fvdy
        rho, edge_budget, field_budget = automatic_vignetting_field_budget(system, index, illumination_contract)
        if handoff:
            if not 0.0 <= current_fvdy <= 1.0:
                raise ValueError(f'handoff Field {index}: FVDY={current_fvdy} is outside the physical [0, 1] domain')
            field_budget = 1.0
        contract_dict = illumination_contract if isinstance(illumination_contract, dict) else {}
        hard_power = max(0.0, finite_float(contract_dict.get('vignetting_hard_field_power'), state.AUTO_VIGNETTING_HARD_FIELD_POWER))
        soft_power = max(0.0, finite_float(contract_dict.get('vignetting_soft_field_power'), state.AUTO_VIGNETTING_SOFT_FIELD_POWER))
        scheduled_budget = edge_budget * rho ** hard_power
        soft_budget = edge_budget * rho ** soft_power
        realized_bound_expanded = field_budget > scheduled_budget + 1e-15
        if field_budget <= 0.0:
            continue
        asymmetry = max(-1.0, min(1.0, current_fvcy / current_fvdy)) if current_fvdy > 1e-15 else 0.0
        for kind, bounds, current in (('vignette_magnitude_y', (0.0, field_budget), current_fvdy), ('vignette_asymmetry_y', (-1.0, 1.0), asymmetry)):
            if (kind, index) in existing:
                continue
            lower, upper = bounds
            out.append({'kind': kind, 'index': index, 'term': 0, 'lower': lower, 'upper': upper, 'initial': _lensde_populations.clamp_value(current, lower, upper), 'role': f'{state.AUTO_VIGNETTING_ROLE_PREFIX}{kind} @ Field {index}', 'auto_vignetting_variable': True, 'vignetting_bound_policy': state.VIGNETTING_HANDOFF_BOUND_POLICY if handoff else 'illumination_budget_quadratic_with_realized_headroom' if realized_bound_expanded else 'illumination_budget_quadratic', 'vignetting_parameterization': 'magnitude_asymmetry_y_v1', 'normalized_field_radius': rho, 'edge_fvdy_upper': edge_budget, 'scheduled_field_budget': scheduled_budget, 'soft_field_budget': soft_budget, 'hard_field_power': hard_power, 'soft_field_power': soft_power, 'soft_prior_weight': finite_float(contract_dict.get('vignetting_soft_prior_weight'), state.AUTO_VIGNETTING_SOFT_PRIOR_WEIGHT), 'realized_bound_expanded': realized_bound_expanded})
            existing.add((kind, index))
    return out


def finish_project(project: dict) -> dict:
    """Normalize settings by explicit domains, retaining mutation and call order."""
    from .project_normalization import (
        materials as _normalize_materials,
        merit as _normalize_merit,
        runtime as _normalize_runtime,
        seeding as _normalize_seeding,
        search_policy as _normalize_search_policy,
        funnel as _normalize_funnel,
        aiming as _normalize_aiming,
        finalization as _normalize_finalization,
    )
    system, glass_search = _normalize_materials.prepare_system_and_glass_search(project, finite_float=finite_float)
    merit = _normalize_merit.normalize_merit_basics(project, finite_float=finite_float)
    _normalize_merit.normalize_vignetting_merit(merit, finite_float=finite_float)
    _normalize_merit.normalize_image_trace_merit(merit, finite_float=finite_float)
    _normalize_merit.normalize_manufacturing_merit(merit, finite_float=finite_float)
    options = _normalize_runtime.normalize_runtime_and_refresh(project, system, merit, glass_search)
    _normalize_seeding.normalize_initial_seed_options(options)
    _normalize_search_policy.normalize_search_strategy(options, finite_float=finite_float)
    _normalize_search_policy.normalize_initial_basin_policy(options)
    _normalize_funnel.normalize_global_funnel(options, finite_float=finite_float)
    _normalize_funnel.normalize_restart_and_topology(options)
    _normalize_seeding.normalize_search_refresh_and_polish(options)
    _normalize_aiming.normalize_ray_aiming(options)
    _normalize_aiming.normalize_search_trace(options)
    _normalize_funnel.normalize_probe_focus_refine(options)
    _normalize_seeding.normalize_seed_gate_and_local_refresh(options)
    _normalize_search_policy.normalize_adaptive_optimizer(options, finite_float=finite_float)
    _normalize_runtime.normalize_bounds_and_polish(options, glass_search)
    glass_mode_indices, actual_glass_indices = _normalize_materials.normalize_surface_materials(
        project,
        system,
        glass_search,
        ensure_glass_surface_config=ensure_glass_surface_config,
    )
    filtered_variables = _normalize_materials.filter_variable_columns(
        project,
        system,
        glass_search,
        glass_mode_indices,
        actual_glass_indices,
    )
    _normalize_finalization.finalize_optical_and_seed_contracts(
        project,
        system,
        merit,
        options,
        filtered_variables,
        ensure_conic_coefficient_merit_bounds=ensure_conic_coefficient_merit_bounds,
        ensure_vignetting_variables_for_system=ensure_vignetting_variables_for_system,
        sync_unified_ray_aiming_options=sync_unified_ray_aiming_options,
        vignetting_minimum_relative_illumination=vignetting_minimum_relative_illumination,
        vignetting_optimization_enabled=vignetting_optimization_enabled,
    )
    return project


def finite_float(value, default: float=0.0) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return default
    return number if number == number and number not in {float('inf'), float('-inf')} else default


def cross_outer_memory_admission_target(policy: str, merit_ratio: float, high_quality_merit_ratio: float, memory_max_merit_ratio: float, *, is_rank1: bool) -> str | None:
    """Return the avoid-memory bucket for one historical basin.

    A completed Outer's rank-1 basin is terminal evidence that the search has
    already spent an Outer on that basin.  It is therefore operational memory
    regardless of its merit relative to the cross-Outer frontier.  The maximum
    merit ratio continues to filter only non-rank-1 archive basins.
    """
    normalized = str(policy or 'qualified_similarity').strip().lower()
    if normalized in {'no_avoid', 'a0_no_avoid'}:
        return None
    if normalized in {'all_exact_only', 'a1_all_exact_only'}:
        return 'exact_only'
    if normalized in {'legacy_all_similarity', 'a5_legacy_all_similarity'}:
        return 'similarity'
    if not is_rank1 and merit_ratio > memory_max_merit_ratio:
        return None
    if normalized in {'qualified_exact_only', 'a2_qualified_exact_only'}:
        return 'exact_only'
    if normalized in {'qualified_similarity', 'a4_qualified_similarity'}:
        return 'similarity'
    return 'exact_only' if merit_ratio <= high_quality_merit_ratio else 'similarity'


def build_de_dls_material_stages(options: dict) -> list[dict]:
    configured: list[dict] = []
    for template in state.DE_DLS_MATERIAL_STAGE_TEMPLATES:
        stage = {key: copy.deepcopy(value) for key, value in template.items() if key != 'option_key'}
        default_iter = max(1, int(finite_float(stage.get('local_iter'), 400)))
        option_key = str(template.get('option_key') or '')
        stage['local_iter'] = max(1, int(finite_float(options.get(option_key), default_iter)))
        configured.append(stage)
    stages: list[dict] = []
    snap_stages: list[dict] = []
    for stage in configured:
        if str(stage.get('name', '')).startswith(('s04_snap_', 's05_snap_')):
            snap_stages.append(stage)
            continue
        stages.append(stage)
    if len(snap_stages) == 2:
        total_snap_iter = sum((max(1, int(stage.get('local_iter', 1))) for stage in snap_stages))
        per_snap, remainder = divmod(total_snap_iter, 6)
        insert_at = next((index for index, stage in enumerate(stages) if str(stage.get('name', '')).startswith('s06_')), len(stages))
        sequential: list[dict] = []
        for index in range(6):
            stage = copy.deepcopy(snap_stages[0] if index < 3 else snap_stages[1])
            stage['name'] = f's04_s05_snap_one_{index + 1:02d}'
            stage['operation'] = 'snap_nearest'
            stage.pop('snap_fraction', None)
            stage['snap_count'] = 1
            stage['variable_mode'] = 'geometry_material'
            stage['local_iter'] = max(1, per_snap + int(index < remainder))
            stage['keep'] = 8 if index < 3 else 6
            sequential.append(stage)
        stages[insert_at:insert_at] = sequential
    return stages


def first_order_weight_value(merit: dict, name: str, default: float=1.0) -> float:
    key = f'weight_{name}'
    return max(0.0, finite_float(merit.get(key), default))


def normalize_thickness_constraint_fields(values: dict, *, prefix: str='') -> dict[str, float]:
    out: dict[str, float] = {}
    for key, default in state.THICKNESS_CONSTRAINT_DEFAULTS:
        out[key] = max(0.0, finite_float(values.get(prefix + key), default))
    for base in ('glass_center_thickness', 'air_center_thickness', 'glass_edge_thickness', 'air_edge_thickness'):
        lower_key = f'{base}_lower'
        upper_key = f'{base}_upper'
        out[upper_key] = max(out[lower_key], out[upper_key])
    return out


def normalise_project(data: dict) -> dict:
    project = _lensde_defaults.default_project()
    if 'system' in data:
        project['system'] = data.get('system', project['system'])
        project['variables'] = data.get('variables', project['variables'])
        incoming_merit = copy.deepcopy(data.get('merit', {}))
        if 'vignetting_physical_cut_match_enabled' not in incoming_merit and 'vignetting_global_trim_cut_match_experimental' in incoming_merit:
            incoming_merit['vignetting_physical_cut_match_enabled'] = incoming_merit['vignetting_global_trim_cut_match_experimental']
        project['merit'] = {**project['merit'], **incoming_merit}
        incoming_options = copy.deepcopy(data.get('options', {}))
        if 'initial_search_seed_policy' not in incoming_options:
            incoming_options['initial_search_seed_policy'] = seed_protocol.LEGACY_POLICY
            incoming_options.setdefault('protective_glass_identification_policy', 'legacy_geometry_v1')
        project['options'] = {**project['options'], **incoming_options}
        if 'options' not in data:
            sync_options_from_system_ray_aiming(project)
        return finish_project(project)
    project['system'] = data
    sync_options_from_system_ray_aiming(project)
    return finish_project(project)
