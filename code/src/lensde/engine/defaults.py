"""Extracted paper engine: defaults."""
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
from . import execution as _lensde_execution
from . import fields as _lensde_fields
from . import geometry as _lensde_geometry
from . import initialization as _lensde_initialization
from . import materials as _lensde_materials
from . import orchestration as _lensde_orchestration
from . import populations as _lensde_populations
from . import provenance as _lensde_provenance
from . import tracing as _lensde_tracing

def full_restart_early_stop_epoch_plan(generation_budget: int, generation_window: int, enabled: bool) -> dict[str, int]:
    """Split a fixed DE budget so the plateau window lands on epoch boundaries."""
    budget = max(1, int(generation_budget))
    window = max(1, int(generation_window))
    if enabled and budget >= window:
        epoch_generations = math.gcd(budget, window)
        outer_epochs = max(1, budget // epoch_generations)
        window_epochs = max(1, window // epoch_generations)
    else:
        epoch_generations = budget
        outer_epochs = 1
        window_epochs = 1
    return {'generation_budget': budget, 'generation_window': window, 'epoch_generations': epoch_generations, 'outer_epochs': outer_epochs, 'window_epochs': window_epochs}


def outer_vignetting_phase_plan(outer_epochs: int, generations_per_epoch: int, strict_fraction: float) -> dict[str, int | float]:
    """Split one fixed Outer budget into legacy-loose and physical-strict phases.

    Multi-epoch runs switch only at an epoch boundary so each epoch keeps its
    configured generation count.  A one-epoch run (including the validated
    Outer1/G1200 case) instead splits its generations directly.
    """
    epochs = max(1, int(outer_epochs))
    generations = max(1, int(generations_per_epoch))
    fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(strict_fraction, 1.0 / 3.0)))
    total_generations = epochs * generations
    if epochs > 1:
        strict_epochs = min(epochs, max(1, int(math.floor(epochs * fraction + 0.5))))
        loose_epochs = epochs - strict_epochs
        loose_generations = generations if loose_epochs > 0 else 0
        strict_generations = generations
    else:
        strict_generations = min(generations, max(1, int(math.floor(generations * fraction + 0.5))))
        loose_generations = generations - strict_generations
        loose_epochs = 1 if loose_generations > 0 else 0
        strict_epochs = 1
    return {'strict_fraction': fraction, 'total_generations': total_generations, 'loose_outer_epochs': loose_epochs, 'loose_generations_per_epoch': loose_generations, 'loose_total_generations': loose_epochs * loose_generations, 'strict_outer_epochs': strict_epochs, 'strict_generations_per_epoch': strict_generations, 'strict_total_generations': strict_epochs * strict_generations}


def sanitize_merit_model(value, default: str='unified_trace_gate') -> str:
    model = str(value or default)
    return model if model in state.MERIT_MODEL_VALUES else default


def canonical_distortion_model(value, default: str='auto') -> str:
    model = str(value or default).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'ftheta': 'f_theta', 'theta': 'f_theta', 'f_tantheta': 'f_tan_theta', 'ftan_theta': 'f_tan_theta', 'ftantheta': 'f_tan_theta', 'tan_theta': 'f_tan_theta', 'tantheta': 'f_tan_theta'}
    model = aliases.get(model, model)
    return model if model in state.DISTORTION_MODEL_VALUES else default


def canonical_image_quality_metric(value, default: str='rms_spot') -> str:
    metric = str(value or default).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'spot': 'rms_spot', 'mean_spot': 'rms_spot', 'spot_rms': 'rms_spot', 'wavefront': 'rms_wavefront', 'wavefront_rms': 'rms_wavefront', 'opd': 'rms_wavefront', 'rms_opd': 'rms_wavefront', 'opd_rms': 'rms_wavefront', 'spot_wavefront': 'rms_spot_wavefront', 'wavefront_spot': 'rms_spot_wavefront', 'spot_plus_wavefront': 'rms_spot_wavefront', 'wavefront_plus_spot': 'rms_spot_wavefront', 'rms_spot_plus_wavefront': 'rms_spot_wavefront', 'rms_wavefront_plus_spot': 'rms_spot_wavefront', 'rms_spot_and_wavefront': 'rms_spot_wavefront', 'rms_wavefront_and_spot': 'rms_spot_wavefront'}
    metric = aliases.get(metric, metric)
    return metric if metric in state.IMAGE_QUALITY_METRIC_VALUES else default


def image_quality_operands(metric: str, *, spot_weight: float=3.0, spot_tail_weight: float=0.012, wavefront_weight: float=1.0, guard_ratio: float=10.0, guard_weight: float=1.0) -> list[dict]:
    canonical = canonical_image_quality_metric(metric, 'rms_spot')
    spot_weight = max(0.0, _lensde_configuration.finite_float(spot_weight, 3.0))
    spot_tail_weight = max(0.0, _lensde_configuration.finite_float(spot_tail_weight, 0.012))
    wavefront_weight = max(0.0, _lensde_configuration.finite_float(wavefront_weight, 1.0))
    guard_ratio = max(1e-09, _lensde_configuration.finite_float(guard_ratio, 10.0))
    guard_weight = max(0.0, _lensde_configuration.finite_float(guard_weight, 1.0))
    operands: list[dict] = []
    if canonical in {'rms_spot', 'rms_spot_wavefront'}:
        operands.append({'name': 'rms_spot', 'mode': 'minimize', 'weight': spot_weight})
        if spot_tail_weight > 0.0:
            operands.append({'name': 'single_ray_spot_tail', 'mode': 'minimize', 'weight': spot_tail_weight})
    if canonical in {'rms_wavefront', 'rms_spot_wavefront'}:
        operands.append({'name': 'rms_wavefront', 'mode': 'minimize', 'weight': wavefront_weight})
        operands.append({'name': 'single_ray_wavefront_guard', 'mode': 'less_than', 'upper': guard_ratio, 'weight': guard_weight})
    if operands:
        return operands
    return [{'name': canonical, 'mode': 'minimize', 'weight': spot_weight}]


def canonical_wavefront_reference(value, default: str='centroid_sphere') -> str:
    reference = str(value or default).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'chief': 'chief_ray', 'chief_reference': 'chief_ray', 'chief_ray_reference': 'chief_ray', 'centroid': 'centroid_sphere', 'centroid_reference': 'centroid_sphere', 'centroid_reference_sphere': 'centroid_sphere'}
    reference = aliases.get(reference, reference)
    return reference if reference in state.WAVEFRONT_REFERENCE_VALUES else default


def canonical_initial_optimizer_mode(value: object) -> str:
    mode = str(value or state.INITIAL_SEARCH_FUNNEL_MODE).strip().lower().replace('-', '_').replace(' ', '_')
    if mode == 'flat_funnel':
        return state.INITIAL_SEARCH_FUNNEL_MODE
    if mode in {'global_develop_only', 'explore_develop', 'explore_develop_restart', 'quick_feasible', 'quick_initial', 'funnel_explore_develop_only'}:
        return state.INITIAL_SEARCH_EXPLORE_DEVELOP_MODE
    if mode in {'greedy_graft', 'greedy_topology_graft', 'topology_graft', 'topology_greedy', 'topology_mutation_graft'}:
        return state.INITIAL_SEARCH_TOPOLOGY_GREEDY_GRAFT_MODE
    if mode in {'de_dls_funnel', 'de_dls_topology_funnel', 'post_de_dls_funnel', 'material_funnel_after_de', 'de_material_dls_funnel'}:
        return state.INITIAL_SEARCH_DE_DLS_MATERIAL_FUNNEL_MODE
    return mode


def canonical_initial_search_run_profile(value: object) -> str:
    profile = str(value or 'single_pass_demo').strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'default': 'single_pass_demo', 'quick': 'single_pass_demo', 'demo': 'single_pass_demo', 'robust': 'robust_seed_archive', 'multi_seed': 'robust_seed_archive', 'best': 'best_effort', 'high_quality': 'best_effort', 'extreme': 'best_effort'}
    profile = aliases.get(profile, profile)
    return profile if profile in state.INITIAL_SEARCH_RUN_PROFILES else 'single_pass_demo'


def canonical_funnel_material_mode(value: object) -> str:
    mode = str(value or 'fixed').strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'real_glass': 'fixed', 'nearest_glass': 'fixed', 'snap': 'fixed', 'snap_fixed': 'fixed', 'glass': 'fixed', 'glass_fixed': 'fixed', 'glass_choice': 'discrete', 'discrete_glass': 'discrete', 'continuous_material': 'continuous', 'none': 'continuous'}
    mode = aliases.get(mode, mode)
    return mode if mode in {'continuous', 'fixed', 'discrete'} else 'fixed'


def normalize_funnel_material_stage_options(options: dict) -> dict[str, str]:
    legacy_mode = canonical_funnel_material_mode(options.get('initial_search_funnel_material_mode', 'discrete'))
    options['initial_search_funnel_material_mode'] = legacy_mode
    out: dict[str, str] = {}
    for stage, default in state.FUNNEL_MATERIAL_STAGE_DEFAULTS.items():
        key = f'initial_search_funnel_{stage}_material_mode'
        out[stage] = canonical_funnel_material_mode(options.get(key, default))
        options[key] = out[stage]
    return out


def canonical_funnel_global_develop_mode(value: object) -> str:
    mode = str(value or 'mixed').strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'de': 'mixed', 'optimize': 'mixed', 'evolve': 'mixed', 'bridge': 'materialize_only', 'materialized_only': 'materialize_only', 'materialize': 'materialize_only', 'skip': 'materialize_only', 'none': 'materialize_only'}
    mode = aliases.get(mode, mode)
    return mode if mode in {'mixed', 'materialize_only'} else 'mixed'


def canonical_pbest_scope(value: object, fallback: str='legacy') -> str:
    scope = str(value or fallback).strip().lower().replace('-', '_').replace(' ', '_')
    return scope if scope in state.PBEST_GUIDE_SCOPES else fallback


def canonical_de_mating_scope(value: object, fallback: str='global') -> str:
    scope = str(value or fallback).strip().lower().replace('-', '_').replace(' ', '_')
    return scope if scope in state.DE_MATING_SCOPES else fallback


def apply_mechanical_stratified_seed_defaults(options: dict) -> None:
    for key, value in state.MECHANICAL_STRATIFIED_SEED_DEFAULTS.items():
        options.setdefault(key, value)


def apply_flat_topology_funnel_defaults(options: dict) -> None:
    """Make the fixed funnel mode reproducible even for older saved GUI projects."""
    options.setdefault('initial_search_flow', 'legacy')
    options['initial_search_flat_start'] = True
    options['initial_search_population_generation_mode'] = 'restricted_perturbation'
    options['initial_search_population_trace_feasible_gate'] = True
    options['initial_search_population_local_group_perturbation'] = True
    options.setdefault('initial_search_population_local_long_air_gap_prior', True)
    options['initial_search_population_perturb_material'] = True
    options['initial_search_population_perturb_other_variables'] = False
    if _lensde_configuration.finite_float(options.get('initial_search_seed_material_nd_cap'), 0.0) <= 0.0:
        options['initial_search_seed_material_nd_cap'] = 0.45
    if _lensde_configuration.finite_float(options.get('initial_search_seed_material_vd_cap'), 0.0) <= 0.0:
        options['initial_search_seed_material_vd_cap'] = 50.0
    options.setdefault('initial_search_air_thickness_min', 0.0)
    options.setdefault('initial_search_air_thickness_max', 0.0)
    options.setdefault('initial_search_glass_thickness_min', 0.0)
    options.setdefault('initial_search_glass_thickness_max', 0.0)
    options.setdefault('initial_search_adaptive_thickness_bounds', True)
    options.setdefault('initial_search_adaptive_seed_thickness_caps', True)
    options.setdefault('initial_search_use_current_thickness_prior', False)
    options.setdefault('initial_search_bfl_reference_surface', -1)
    options.setdefault('initial_search_bfl_lower_factor', 0.9)
    options.setdefault('initial_search_bfl_upper_factor', 1.5)
    options.setdefault('initial_search_anchor_cloud_coverage_injection', True)
    _lensde_configuration.sync_unified_ray_aiming_options(options)
    options['initial_search_trace_policy'] = 'soft_coverage'
    if options.get('initial_search_trace_distribution') == 'gaussian_ring_arm_fixed_budget':
        options['initial_search_trace_rays'] = 6
        options['initial_search_trace_arms'] = 8
    else:
        options['initial_search_trace_rays'] = 3
        options['initial_search_trace_arms'] = 6
        options['initial_search_trace_distribution'] = 'gaussian_ring_arm'
    if not str(options.get('initial_search_merit_strategy', '')).strip():
        options['initial_search_merit_strategy'] = 'full'
    options['initial_search_first_order_weight_scale'] = 1.0
    options['initial_search_spot_weight_scale'] = 1.0
    options.setdefault('initial_search_first_order_edge_weight', 0.0)
    options.setdefault('initial_search_first_order_min_edge', 1.0)
    for key, default in state.THICKNESS_CONSTRAINT_DEFAULTS:
        options.setdefault('initial_search_' + key, default)
    for key, default in state.MANUFACTURING_RATIO_CONSTRAINT_DEFAULTS:
        options.setdefault('initial_search_' + key, default)
    options['initial_search_include_stop_air_thickness'] = True
    options.setdefault('initial_search_stop_air_thickness_min', 0.1)
    options.setdefault('initial_search_stop_air_thickness_max', 27.0)
    options['initial_search_hybrid_outer'] = max(1, int(options.get('initial_search_hybrid_outer', 10) or 10))
    options['initial_search_hybrid_de_gen'] = max(1, int(options.get('initial_search_hybrid_de_gen', 120) or 120))
    options['initial_search_disable_polish'] = False
    options.setdefault('initial_search_polish_selection', 'topology_balanced')
    options.setdefault('initial_search_funnel_global_enable_polish', True)
    options.setdefault('initial_search_funnel_global_micro_island_enabled', False)
    options.setdefault('initial_search_funnel_global_micro_island_replace_polish', True)
    options.setdefault('initial_search_funnel_global_micro_island_count', 0)
    if 'initial_search_funnel_global_micro_island_popsize' not in options and 'initial_search_funnel_global_micro_island_population' not in options:
        options['initial_search_funnel_global_micro_island_popsize'] = 6.0
    options.setdefault('initial_search_funnel_global_micro_island_outer', 1)
    options.setdefault('initial_search_funnel_global_micro_island_de_gen', 24)
    options.setdefault('initial_search_funnel_global_micro_island_seed_scale', 0.65)
    options.setdefault('initial_search_funnel_global_micro_island_polish_candidates', -1)
    options.setdefault('initial_search_funnel_global_micro_island_polish_refill_max_extra', -1)
    options.setdefault('initial_search_funnel_global_micro_island_survivors', 2)
    options.setdefault('initial_search_funnel_global_micro_island_family_seed_limit', 24)
    options.setdefault('initial_search_funnel_global_micro_island_refresh_archive_seed_limit', 24)
    options.setdefault('initial_search_funnel_global_micro_island_early_stop_enabled', False)
    options.setdefault('initial_search_funnel_global_micro_island_retire_on_early_stop', True)
    options.setdefault('initial_search_funnel_global_micro_island_early_stop_min_epochs', 3)
    options.setdefault('initial_search_funnel_global_micro_island_early_stop_window', 1)
    options.setdefault('initial_search_funnel_global_micro_island_early_stop_min_rel_improve', 0.01)
    options.setdefault('initial_search_funnel_global_micro_island_early_stop_min_abs_improve', 0.01)
    options.setdefault('initial_search_funnel_global_popsize', 10.0)
    options.setdefault('initial_search_funnel_global_explore_outer', 80)
    options.setdefault('initial_search_funnel_outer_vignetting_strict_fraction', 1.0 / 3.0)
    options.setdefault('initial_search_funnel_global_explore_de_use_cuda', True)
    options.setdefault('initial_search_funnel_global_explore_full_restart_enabled', False)
    options.setdefault('initial_search_funnel_global_cra_tighten_enabled', False)
    options.setdefault('initial_search_funnel_global_cra_tighten_generations', 1200)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_previous', True)
    options.setdefault('initial_search_funnel_global_explore_full_restart_shared_seed_pool', True)
    options.setdefault('initial_search_funnel_global_explore_full_restart_seed_pool_multiplier', 1.0)
    options.setdefault('initial_search_funnel_global_explore_full_restart_archive_limit', 0)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_penalty', 1000.0)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_similarity_threshold', 0.9)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_max_shape_diff', 3)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_continuous_structure_enabled', True)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_continuous_power_cosine_min', 0.99)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_continuous_power_distance_max', 0.18)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_cosine_min', 0.97)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_distance_max', 0.25)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_continuous_thickness_distance_max', 0.1)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_continuous_gap_distance_max', 0.1)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_continuous_significance_fraction', 0.08)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_parameter_auxiliary_similarity_min', 0.9)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_memory_policy', 'qualified_similarity')
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_high_quality_merit_ratio', 3.0)
    options.setdefault('initial_search_funnel_global_explore_full_restart_avoid_memory_max_merit_ratio', 6.0)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_enabled', True)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_trigger_mode', 'warmup')
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_focus_clear_target', 0)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_stop_when_ready', True)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_bridge_freeze_focus', True)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_warmup_epochs', 2)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_elite_fraction', 0.2)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_quantile', 0.0)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_margin', 0.5)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_floor', 0.05)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_smoothing', 0.75)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_decay', 0.75)
    options.setdefault('initial_search_funnel_global_adaptive_glass_tolerance_early_stop_max', 0.5)
    options.setdefault('initial_search_funnel_global_develop_outer', 15)
    options['initial_search_funnel_global_develop_mode'] = canonical_funnel_global_develop_mode(options.get('initial_search_funnel_global_develop_mode', 'mixed'))
    options.setdefault('initial_search_funnel_global_develop_lpsr_enabled', True)
    options.setdefault('initial_search_funnel_global_explore_de_gen', 1000)
    options.setdefault('initial_search_funnel_global_develop_de_gen', 240)
    options.setdefault('initial_search_funnel_global_near_trace_enabled', True)
    options['initial_search_funnel_global_near_trace_min_coverage'] = min(1.0, max(0.0, _lensde_configuration.finite_float(options.get('initial_search_funnel_global_near_trace_min_coverage'), 0.95)))
    options.setdefault('initial_search_funnel_global_refresh_trace_feasible_gate', True)
    options.setdefault('initial_search_funnel_global_basin_active_require_trace_feasible', False)
    options.setdefault('initial_search_funnel_global_plateau_retire_evict', True)
    options.setdefault('initial_search_funnel_global_plateau_retire_frontier_on_first_update', False)
    options.setdefault('initial_search_funnel_global_plateau_retire_evict_parameter_only', True)
    options.setdefault('initial_search_funnel_global_plateau_frontier_count', 1)
    options.setdefault('initial_search_funnel_global_plateau_frontier_multiplier', 0.0)
    options.setdefault('initial_search_funnel_global_plateau_retire_evict_fraction', 1.0)
    options.setdefault('initial_search_funnel_global_plateau_retire_evict_max_fraction', 0.2)
    options.setdefault('initial_search_funnel_global_plateau_retire_evict_min', 0)
    options.setdefault('initial_search_funnel_global_plateau_retire_evict_parameter_distance', 0.15)
    options.setdefault('initial_search_funnel_global_plateau_retire_defer_early_stop_epochs', 0)
    options.setdefault('initial_search_funnel_global_plateau_retire_stop_after_clears', 0)
    options.setdefault('initial_search_explore_develop_restart_count', 1)
    options.setdefault('initial_search_explore_develop_seed_stride', 10000)
    options.setdefault('initial_search_restart_seed_topology_novelty', False)
    options.setdefault('initial_search_restart_seed_topology_scan_limit', 0)
    options.setdefault('initial_search_topology_graft_enabled', True)
    options.setdefault('initial_search_topology_graft_source_pool_size', 32)
    options.setdefault('initial_search_topology_graft_count', 8)
    options.setdefault('initial_search_topology_graft_donor_count', 6)
    options.setdefault('initial_search_topology_graft_segments_per_donor', 2)
    options.setdefault('initial_search_topology_graft_window_radius', 1)
    options.setdefault('initial_search_topology_graft_blend', 0.7)
    options.setdefault('initial_search_topology_graft_scale', 0.45)
    options.setdefault('initial_search_topology_graft_popsize', 6.0)
    options.setdefault('initial_search_topology_graft_outer', 2)
    options.setdefault('initial_search_topology_graft_de_gen', 80)
    options.setdefault('initial_search_topology_graft_enable_polish', True)
    options.setdefault('initial_search_funnel_probe_cycle_enabled', False)
    options.setdefault('initial_search_funnel_probe_cycle_count', 3)
    options.setdefault('initial_search_funnel_probe_cycle_global_outer', 4)
    options.setdefault('initial_search_funnel_probe_cycle_global_develop_outer', 1)
    options.setdefault('initial_search_funnel_probe_cycle_global_de_gen', 80)
    options.setdefault('initial_search_funnel_probe_cycle_probe_count', 24)
    options.setdefault('initial_search_funnel_probe_cycle_source_pool_size', 64)
    options.setdefault('initial_search_funnel_probe_cycle_archive_per_stage', 48)
    options.setdefault('initial_search_funnel_probe_cycle_population_scan', 0)
    options.setdefault('initial_search_funnel_global_early_stop_enabled', True)
    options.setdefault('initial_search_funnel_global_early_stop_metric', 'topology_discovery')
    options.setdefault('initial_search_funnel_global_early_stop_min_epochs', 4)
    options.setdefault('initial_search_funnel_global_early_stop_window', 2)
    options.setdefault('initial_search_funnel_global_early_stop_min_topologies', 20)
    options.setdefault('initial_search_funnel_global_early_stop_min_rel_improve', 0.01)
    options.setdefault('initial_search_funnel_global_early_stop_min_abs_improve', 0.01)
    options.setdefault('initial_search_funnel_global_early_stop_require_topology_plateau', False)
    options.setdefault('initial_search_funnel_global_early_stop_best_merit_max', 200.0)
    options.setdefault('initial_search_funnel_global_early_stop_qualified_topologies', True)
    options.setdefault('initial_search_funnel_global_early_stop_qualified_topology_mode', 'merit')
    options.setdefault('initial_search_funnel_global_early_stop_qualified_topology_epsilon', 0.02)
    options.setdefault('initial_search_funnel_global_early_stop_qualified_topology_merit_ratio', 5.0)
    options.setdefault('initial_search_funnel_global_early_stop_qualified_topology_merit_floor', 200.0)
    options.setdefault('initial_search_funnel_global_early_stop_qualified_topology_merit_delta', 250.0)
    options.setdefault('initial_search_funnel_global_early_stop_qualified_topology_merit_max', 0.0)
    options.setdefault('initial_search_basin_active_diversity', False)
    options.setdefault('initial_search_basin_active_count', 9)
    options.setdefault('initial_search_basin_active_selection_mode', 'topology_merit_potential')
    options.setdefault('initial_search_basin_adaptive_counts', True)
    options.setdefault('initial_search_basin_adaptive_reference_population', 600)
    options.setdefault('initial_search_basin_adaptive_max_cap', 256)
    options.setdefault('initial_search_basin_adaptive_output_cap', 48)
    options.setdefault('initial_search_basin_adaptive_active_cap', 12)
    options.setdefault('initial_search_basin_semantic_diversity', True)
    options.setdefault('initial_search_basin_balanced_refresh', False)
    options.setdefault('initial_search_basin_active_mating_quota', False)
    options.setdefault('initial_search_basin_active_niche_tracking', True)
    options.setdefault('initial_search_basin_active_niche_start_epoch', 1)
    options.setdefault('initial_search_basin_active_niche_survival', False)
    options.setdefault('initial_search_basin_active_niche_min_quota', 0)
    options.setdefault('initial_search_basin_active_niche_target_quota', 0)
    options.setdefault('initial_search_basin_active_niche_family_cap_fraction', 0.15)
    options.setdefault('initial_search_basin_active_niche_repair_interval', 25)
    options.setdefault('initial_search_basin_active_niche_repair_fraction', 0.03)
    options.setdefault('initial_search_basin_active_niche_rebalance', False)
    options.setdefault('initial_search_basin_active_niche_rebalance_interval', 25)
    options.setdefault('initial_search_basin_active_niche_rebalance_fraction', 0.05)
    options.setdefault('initial_search_basin_active_niche_soft_fill', False)
    options.setdefault('initial_search_basin_active_niche_elite_seed_count', 1)
    options.setdefault('initial_search_basin_active_focus_rebalance', False)
    options.setdefault('initial_search_basin_active_focus_start_epoch', 1)
    options.setdefault('initial_search_basin_active_focus_initial_best_family', True)
    options.setdefault('initial_search_basin_active_focus_family_count', 1)
    options.setdefault('initial_search_basin_active_focus_population_fraction', 0.55)
    options.setdefault('initial_search_basin_active_focus_min_quota', 0)
    options.setdefault('initial_search_basin_active_focus_soft_fill', False)
    options.setdefault('initial_search_basin_active_focus_elite_seed_count', 1)
    options.setdefault('initial_search_basin_active_monitor_family_cap', 0)
    options.setdefault('initial_search_basin_active_focus_stop_rel_improvement', 0.2)
    options.setdefault('initial_search_basin_active_focus_ready_fraction', 0.7)
    options.setdefault('initial_search_basin_active_focus_cooldown_epochs', 2)
    options.setdefault('initial_search_basin_active_focus_stop_after_clears', 0)
    options.setdefault('initial_search_basin_active_focus_stop_release', True)
    options.setdefault('initial_search_basin_active_focus_stop_release_fraction', 1.0)
    options.setdefault('initial_search_basin_active_focus_rotation', False)
    options.setdefault('initial_search_basin_active_focus_rotation_history_epochs', 8)
    options.setdefault('initial_search_basin_active_focus_rotation_similarity_threshold', 0.8)
    options.setdefault('initial_search_basin_active_refresh_rebalance', True)
    options.setdefault('initial_search_basin_active_refresh_max_family_fraction', 0.0)
    options.setdefault('initial_search_basin_active_refresh_min_local_fraction', 0.2)
    options.setdefault('initial_search_basin_active_plateau_lifecycle', True)
    options.setdefault('initial_search_basin_active_plateau_track_active_niches', True)
    options.setdefault('initial_search_basin_active_plateau_window', 2)
    options.setdefault('initial_search_basin_active_plateau_min_rel_improvement', 0.005)
    options.setdefault('initial_search_basin_active_plateau_local_quota_scale', 0.35)
    options.setdefault('initial_search_basin_active_plateau_external_probe_rate', 0.15)
    options.setdefault('initial_search_basin_active_plateau_retire_after', 4)
    options.setdefault('initial_search_basin_active_plateau_cooldown_epochs', 2)
    options.setdefault('initial_search_basin_active_quality_gate', True)
    options.setdefault('initial_search_basin_active_require_trace_feasible', True)
    options.setdefault('initial_search_basin_active_mating_match_mode', 'family_similarity')
    options.setdefault('initial_search_basin_active_max_slots_per_family', 1)
    options.setdefault('initial_search_basin_active_family_similarity_threshold', 0.8)
    options.setdefault('initial_search_basin_active_family_max_shape_diff', 3)
    options.setdefault('initial_search_basin_active_family_parameter_merge_enabled', False)
    options.setdefault('initial_search_basin_active_family_parameter_merge_distance', 0.15)
    options.setdefault('initial_search_basin_active_family_parameter_merge_metric', 'structure_bound_normalized')
    options.setdefault('initial_search_basin_active_archive_only', False)
    options.setdefault('initial_search_basin_active_slot_strict_frozen_family', False)
    options.setdefault('initial_search_basin_active_local_cohort', False)
    options.setdefault('initial_search_basin_active_local_cohort_size', 0)
    options.setdefault('initial_search_basin_active_local_cohort_fraction', 0.35)
    options.setdefault('initial_search_basin_active_local_cohort_max_distance', 0.0)
    options.setdefault('initial_search_basin_active_max_merit_delta', 200.0)
    options.setdefault('initial_search_de_variant', 'jade_pbest_archive')
    options.setdefault('initial_search_track_population_diversity', False)
    options.setdefault('initial_search_constraint_handling', 'scalar_merit')
    options.setdefault('initial_search_pbest_scope', 'global')
    options.setdefault('initial_search_pbest_basin_rate', 0.0)
    options.setdefault('initial_search_pbest_same_key_rate', 0.0)
    options.setdefault('initial_search_pbest_global_rate', 1.0)
    options.setdefault('initial_search_pbest_global_fallback', True)
    options.setdefault('initial_search_pbest_basin_nearest_fallback', False)
    options.setdefault('initial_search_de_mating_scope', 'global')
    options.setdefault('initial_search_de_mating_local_rate', 0.0)
    options.setdefault('initial_search_de_mating_min_pool', 4)
    options.setdefault('initial_search_de_mating_strict_local', False)
    options.setdefault('initial_search_hybrid_inject_best', False)
    normalize_funnel_material_stage_options(options)
    options['initial_search_funnel_glass_candidates'] = max(1, int(options.get('initial_search_funnel_glass_candidates', 5) or 5))
    options.setdefault('initial_search_seed_pool_population_multiplier', 2.0)
    options.setdefault('initial_search_refresh_pool_population_multiplier', 2.0)
    options.setdefault('initial_search_funnel_probe_popsize', 5.0)
    options.setdefault('initial_search_funnel_probe_population', 160)
    options.setdefault('initial_search_funnel_probe_outer', 2)
    options.setdefault('initial_search_funnel_probe_de_gen', 50)
    options.setdefault('initial_search_funnel_probe_scale', 0.65)
    options.setdefault('initial_search_funnel_probe_coarse_topology_diversity', True)
    options.setdefault('initial_search_funnel_probe_max_per_coarse_topology', 1)
    options.setdefault('initial_search_funnel_probe_source_pool_size', 64)
    options.setdefault('initial_search_funnel_probe_parent_use_cached_topology', True)
    options.setdefault('initial_search_funnel_probe_parent_selection_priority', 'topology_merit_potential')
    options.setdefault('initial_search_funnel_population_candidate_selection', 'topology_merit_potential')
    options.setdefault('initial_search_funnel_population_candidate_scan', 0)
    options.setdefault('initial_search_funnel_probe_refill_enabled', True)
    options.setdefault('initial_search_funnel_probe_refill_target_coarse', 4)
    options.setdefault('initial_search_funnel_probe_final_target_count', 0)
    options.setdefault('initial_search_funnel_probe_refill_max_extra', 8)
    options.setdefault('initial_search_funnel_probe_refill_scan_population', 128)
    options.setdefault('initial_search_funnel_include_explore_tail_in_probe', True)
    options.setdefault('initial_search_funnel_explore_tail_per_key', 1)
    options.setdefault('initial_search_funnel_include_explore_archive_in_develop', True)
    options.setdefault('initial_search_funnel_explore_archive_develop_limit', 0)
    options.setdefault('initial_search_funnel_include_global_stage_best', True)
    options.setdefault('initial_search_funnel_include_unmaterialized_explore_best', False)
    options.setdefault('initial_search_funnel_final_common_rescore', True)
    options.setdefault('initial_search_funnel_final_rescore_stage', 'refine')
    options.setdefault('initial_search_funnel_same_key_min_distance_probe', 0.12)
    options.setdefault('initial_search_funnel_same_key_min_distance_focus', 0.15)
    options.setdefault('initial_search_funnel_same_key_min_distance_refine', 0.18)
    options.setdefault('initial_search_funnel_probe_semantic_diversity', True)
    options.setdefault('initial_search_funnel_focus_semantic_diversity', True)
    options.setdefault('initial_search_funnel_refine_scout_semantic_diversity', True)
    options.setdefault('initial_search_funnel_refine_semantic_diversity', False)
    options.setdefault('initial_search_funnel_probe_active_diversity', True)
    options.setdefault('initial_search_funnel_focus_active_diversity', True)
    options.setdefault('initial_search_funnel_refine_scout_active_diversity', True)
    options.setdefault('initial_search_funnel_refine_active_diversity', False)
    options.setdefault('initial_search_funnel_promote_count', 4)
    options.setdefault('initial_search_funnel_focus_popsize', 10.0)
    options.setdefault('initial_search_funnel_focus_population', 200)
    options.setdefault('initial_search_funnel_focus_outer', 4)
    options.setdefault('initial_search_funnel_focus_de_gen', 120)
    options.setdefault('initial_search_funnel_focus_trace_rays', 6)
    options.setdefault('initial_search_funnel_focus_trace_arms', 8)
    options['initial_search_funnel_focus_field_count'] = max(0, int(options.get('initial_search_funnel_focus_field_count', 5) or 5))
    options.setdefault('initial_search_funnel_focus_scale', 0.75)
    options.setdefault('initial_search_funnel_focus_post_dedupe_enabled', True)
    options.setdefault('initial_search_funnel_focus_refill_max_extra', 8)
    options.setdefault('initial_search_funnel_focus_duplicate_topology_distance', 10.0)
    options.setdefault('initial_search_funnel_focus_pre_dedupe_enabled', True)
    options.setdefault('initial_search_funnel_focus_pre_dedupe_topology_distance', 10.0)
    options.setdefault('initial_search_funnel_focus_polish_candidates', -1)
    options.setdefault('initial_search_funnel_focus_polish_refill_max_extra', -1)
    options.setdefault('initial_search_funnel_asphere_enabled', False)
    options.setdefault('initial_search_funnel_asphere_field_sampling_enabled', True)
    options['initial_search_funnel_focus_asphere_field_count'] = max(1, int(options.get('initial_search_funnel_focus_asphere_field_count', 5) or 5))
    options['initial_search_funnel_refine_asphere_field_count'] = max(1, int(options.get('initial_search_funnel_refine_asphere_field_count', 7) or 7))
    options['initial_search_funnel_refine_asphere_max_order'] = _lensde_populations.canonical_funnel_asphere_refine_max_order(options.get('initial_search_funnel_refine_asphere_max_order', 10))
    options.setdefault('initial_search_funnel_stage_seed_trace_gate', True)
    options.setdefault('initial_search_funnel_stage_seed_gate_candidate_factor', 8)
    options.setdefault('initial_search_funnel_stage_seed_gate_batch_factor', 1)
    options.setdefault('initial_search_funnel_stage_seed_gate_final_recheck', True)
    options.setdefault('initial_search_funnel_late_near_trace_enabled', True)
    options['initial_search_funnel_late_near_trace_min_coverage'] = min(1.0, max(0.0, _lensde_configuration.finite_float(options.get('initial_search_funnel_late_near_trace_min_coverage'), 0.92)))
    options.setdefault('initial_search_funnel_late_preserve_anchor', True)
    options.setdefault('initial_search_funnel_late_refresh_trace_feasible_gate', False)
    options.setdefault('initial_search_funnel_late_basin_active_require_trace_feasible', False)
    options.setdefault('initial_search_funnel_refine_include_probe_neighbors', True)
    options.setdefault('initial_search_funnel_refine_probe_neighbor_count', 16)
    options.setdefault('initial_search_funnel_refine_probe_neighbor_max_topology_distance', 12.0)
    options.setdefault('initial_search_funnel_refine_glass_hammer_enabled', True)
    options.setdefault('initial_search_funnel_refine_glass_hammer_neighbor_count', 5)
    options.setdefault('initial_search_funnel_refine_glass_hammer_max_passes', 1)
    options.setdefault('initial_search_funnel_refine_glass_hammer_local_iterations', 20)
    options.setdefault('initial_search_funnel_refine_glass_hammer_timeout', 120)
    options.setdefault('initial_search_funnel_refine_glass_hammer_min_improvement', 0.001)
    options.setdefault('initial_search_funnel_refine_glass_hammer_accept_mode', 'best')
    options.setdefault('initial_search_funnel_refine_count', 4)
    options.setdefault('initial_search_funnel_refine_polish_candidates', -1)
    options.setdefault('initial_search_funnel_refine_polish_refill_max_extra', -1)
    options.setdefault('initial_search_funnel_refine_promotion_mode', 'merit_scout_potential')
    options.setdefault('initial_search_funnel_refine_merit_slots', 0)
    options.setdefault('initial_search_funnel_refine_scout_merit_slots', 1)
    options.setdefault('initial_search_funnel_refine_potential_slots', 1)
    options.setdefault('initial_search_funnel_refine_scout_enabled', True)
    options.setdefault('initial_search_funnel_refine_scout_popsize', 5.0)
    options.setdefault('initial_search_funnel_refine_scout_population', 160)
    options.setdefault('initial_search_funnel_refine_scout_outer', 2)
    options.setdefault('initial_search_funnel_refine_scout_de_gen', 80)
    options.setdefault('initial_search_funnel_refine_scout_local_iterations', 10)
    options.setdefault('initial_search_funnel_refine_popsize', 10.0)
    options.setdefault('initial_search_funnel_refine_population', 320)
    options.setdefault('initial_search_funnel_refine_outer', 80)
    options.setdefault('initial_search_funnel_refine_de_gen', 160)
    options.setdefault('initial_search_funnel_refine_trace_rays', 6)
    options.setdefault('initial_search_funnel_refine_trace_arms', 8)
    options['initial_search_funnel_refine_field_count'] = max(0, int(options.get('initial_search_funnel_refine_field_count', 5) or 5))
    options.setdefault('initial_search_funnel_refine_early_stop_enabled', True)
    options.setdefault('initial_search_funnel_refine_early_stop_min_epochs', 10)
    options.setdefault('initial_search_funnel_refine_early_stop_window', 4)
    options.setdefault('initial_search_funnel_refine_early_stop_min_rel_improve', 0.005)
    options.setdefault('initial_search_funnel_refine_early_stop_min_abs_improve', 0.01)
    options.setdefault('initial_search_funnel_refine_lpsr_horizon_epochs', 24)
    options.setdefault('initial_search_funnel_refine_early_stop_min_lpsr_progress', 0.35)
    options.setdefault('initial_search_funnel_refine_early_stop_tail_epochs', 0)
    options.setdefault('initial_search_funnel_local_basin_diversity_enabled', False)
    options.setdefault('initial_search_seed_trace_strategy', 'anchor_sparse_refill')
    apply_mechanical_stratified_seed_defaults(options)
    options.setdefault('initial_search_seed_cap_scout_enabled', True)
    options.setdefault('initial_search_seed_cap_scout_list', '0.01,0.02,0.04')
    options.setdefault('initial_search_seed_cap_scout_candidates', 6144)
    options.setdefault('initial_search_anchor_target_count', 16)
    options.setdefault('initial_search_anchor_min_count', 8)
    options.setdefault('initial_search_anchor_max_candidates', 65536)
    options.setdefault('initial_search_anchor_batch_size', 4096)
    options.setdefault('initial_search_anchor_cloud_factor', 8)
    options.setdefault('initial_search_anchor_cloud_profiles', 'micro,small,medium')
    options.setdefault('initial_search_sparse_refill_enabled', True)
    options.setdefault('initial_search_sparse_refill_skip_when_sufficient', True)
    options.setdefault('initial_search_sparse_refill_pool_factor', 8)
    options.setdefault('initial_search_sparse_refill_max_candidates', 65536)
    options.setdefault('initial_search_sparse_refill_batch_size', 4096)
    options['initial_search_sparse_refill_min_profile_rounds'] = max(0, int(options.get('initial_search_sparse_refill_min_profile_rounds', 0) or 0))
    options.setdefault('initial_search_diversity_min_range_frac', 0.001)
    options.setdefault('initial_search_diversity_target_range_frac', 0.01)
    options.setdefault('initial_search_diversity_min_active_fraction', 0.08)
    options.setdefault('initial_search_diversity_min_curvature_active_fraction', 0.08)
    options.setdefault('initial_search_rest_curvature_cap', 0.5)
    options.setdefault('initial_search_final_strict_recheck', True)


def apply_de_dls_material_funnel_defaults(options: dict) -> None:
    """Defaults for the DE archive plus two-layer DLS material funnel mode."""
    _lensde_initialization.migrate_legacy_initial_asphere_material_options(options)
    legacy_material_terminal = 'initial_search_de_dls_material_terminal_mode' not in options
    preserved_keys = {key: copy.deepcopy(options[key]) for key in ['initial_search_flow', 'initial_search_flat_start', 'initial_search_include_conic', 'initial_search_include_asphere', 'initial_search_asphere_lens_indices', 'initial_search_staged_asphere_initial_mode', 'initial_search_asphere_max_order', 'initial_search_asphere_max_coefficient', 'initial_search_asphere_scout_enabled', 'initial_search_asphere_scout_allow_fallback', 'initial_search_asphere_scout_samples', 'initial_search_asphere_scout_timeout', 'initial_search_asphere_scout_min_trace_coverage', 'initial_search_asphere_profile_scale', 'initial_search_include_material', 'initial_search_wide_front_groups', 'initial_search_front_profile', 'initial_search_rest_curvature_cap', 'initial_search_population_generation_mode', 'initial_search_population_trace_feasible_gate', 'initial_search_seed_pool_population_multiplier', 'initial_search_population_max_candidates', 'initial_search_population_max_seconds', 'initial_search_population_batch_size', 'initial_search_seed_trace_strategy', 'initial_search_baseline_custom_ttl_range_enabled', 'initial_search_baseline_ttl_lower_fraction', 'initial_search_baseline_ttl_upper_fraction', 'initial_search_baseline_bfl_lower_factor', 'initial_search_baseline_bfl_ttl_upper_fraction', 'initial_search_baseline_glass_beta_alpha', 'initial_search_baseline_glass_beta_beta', 'initial_search_baseline_air_dirichlet_alpha', 'initial_search_baseline_conic_coverage_fraction', 'initial_search_baseline_conic_abs_max', 'initial_search_baseline_asphere_coverage_fraction', 'initial_search_baseline_asphere_bound_fraction', 'initial_search_baseline_max_active_shape_per_row', 'initial_search_seed_cap_scout_enabled', 'initial_search_seed_cap_scout_list', 'initial_search_seed_cap_scout_candidates', 'initial_search_anchor_target_count', 'initial_search_anchor_max_candidates', 'initial_search_anchor_batch_size', 'initial_search_sparse_refill_enabled', 'initial_search_sparse_refill_pool_factor', 'initial_search_sparse_refill_max_candidates', 'initial_search_sparse_refill_skip_when_sufficient', 'initial_search_sparse_refill_min_profile_rounds', 'initial_search_diversity_min_range_frac', 'initial_search_diversity_target_range_frac', 'initial_search_diversity_min_active_fraction', 'initial_search_diversity_min_curvature_active_fraction', 'initial_search_curvature_bound', 'initial_search_seed_air_thickness_cap', 'initial_search_seed_glass_thickness_cap', 'initial_search_seed_material_nd_cap', 'initial_search_seed_material_vd_cap', 'initial_search_air_thickness_min', 'initial_search_air_thickness_max', 'initial_search_glass_thickness_min', 'initial_search_glass_thickness_max', 'initial_search_population_local_group_perturbation', 'initial_search_population_local_long_air_gap_prior', 'initial_search_population_perturb_material', 'initial_search_population_perturb_other_variables', 'initial_search_first_order_aiming', 'initial_search_first_order_aiming_iter', 'initial_search_first_order_aiming_stop_radius_source', 'initial_search_first_order_aiming_require_convergence', 'initial_search_first_order_aiming_continuation_steps', 'initial_search_first_order_aiming_pilot_fine_grid', 'initial_search_first_order_aiming_pilot_coarse_grid', 'initial_search_first_order_aiming_pilot_nearest_k', 'initial_search_first_order_aiming_pilot_iterative_failure_limit', 'initial_search_first_order_aiming_pilot_per_ray_iterative', 'initial_search_first_order_aiming_pilot_allow_interpolation', 'initial_search_first_order_aiming_calibrated_correction_iterations', 'initial_search_first_order_aiming_calibrated_sampling_mode', 'initial_search_first_order_aiming_calibrated_fallback', 'initial_search_first_order_aiming_calibrated_chief_mode', 'initial_search_first_order_aiming_calibrated_budget_chief_iter', 'initial_search_first_order_aiming_calibrated_budget_chief_solver', 'initial_search_first_order_aiming_calibrated_budget_chief_acceptance', 'initial_search_first_order_aiming_calibrated_budget_chief_stop_fraction', 'initial_search_first_order_aiming_calibrated_budget_chief_residual_stop_fraction', 'initial_search_first_order_aiming_calibrated_budget_chief_min_a', 'initial_search_first_order_aiming_calibrated_budget_chief_field_angle', 'initial_search_first_order_aiming_calibrated_budget_chief_ignore_aperture_clip', 'initial_search_first_order_aiming_calibrated_budget_chief_skip_abcd_angle', 'initial_search_first_order_aiming_calibrated_edge_calibration_enabled', 'initial_search_first_order_aiming_calibrated_edge_calibration_iterations', 'initial_search_first_order_aiming_calibrated_edge_calibration_solver', 'initial_search_first_order_aiming_calibrated_edge_calibration_weight_power', 'initial_search_first_order_aiming_calibrated_edge_calibration_residual_stop_fraction', 'initial_search_first_order_aiming_three_step_sampling_mode', 'initial_search_first_order_aiming_three_step_chief_rays', 'initial_search_first_order_aiming_three_step_max_angle', 'initial_search_first_order_aiming_three_step_edge_passes', 'initial_search_first_order_aiming_three_step_meridional_iter', 'initial_search_first_order_aiming_three_step_sagittal_iter', 'initial_search_first_order_aiming_three_step_edge_solver', 'initial_search_first_order_aiming_three_step_edge_residual_stop_fraction', 'initial_search_first_order_aiming_three_step_edge_radius_fraction', 'initial_search_first_order_aiming_three_step_accept_stop_fraction', 'initial_search_first_order_aiming_three_step_correction_iterations', 'initial_search_first_order_aiming_three_step_sample_stop_residual_debug', 'initial_search_first_order_aiming_three_step_fallback', 'initial_search_funnel_global_popsize', 'initial_search_funnel_global_explore_outer', 'initial_search_funnel_global_explore_de_gen', 'initial_search_funnel_outer_vignetting_strict_fraction', 'initial_search_funnel_global_explore_de_use_cuda', 'initial_search_run_profile', 'initial_search_funnel_global_explore_full_restart_enabled', 'initial_search_funnel_global_cra_tighten_enabled', 'initial_search_funnel_global_cra_tighten_generations', 'initial_search_funnel_global_explore_full_restart_avoid_previous', 'initial_search_funnel_global_explore_full_restart_shared_seed_pool', 'initial_search_funnel_global_explore_full_restart_seed_pool_multiplier', 'initial_search_funnel_global_explore_full_restart_archive_limit', 'initial_search_funnel_global_explore_full_restart_avoid_penalty', 'initial_search_funnel_global_explore_full_restart_avoid_similarity_threshold', 'initial_search_funnel_global_explore_full_restart_avoid_max_shape_diff', 'initial_search_funnel_global_explore_full_restart_avoid_continuous_structure_enabled', 'initial_search_funnel_global_explore_full_restart_avoid_continuous_power_cosine_min', 'initial_search_funnel_global_explore_full_restart_avoid_continuous_power_distance_max', 'initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_cosine_min', 'initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_distance_max', 'initial_search_funnel_global_explore_full_restart_avoid_continuous_thickness_distance_max', 'initial_search_funnel_global_explore_full_restart_avoid_continuous_gap_distance_max', 'initial_search_funnel_global_explore_full_restart_avoid_continuous_significance_fraction', 'initial_search_funnel_global_explore_full_restart_avoid_parameter_auxiliary_similarity_min', 'initial_search_funnel_global_explore_full_restart_avoid_memory_policy', 'initial_search_funnel_global_explore_full_restart_avoid_high_quality_merit_ratio', 'initial_search_funnel_global_explore_full_restart_avoid_memory_max_merit_ratio', 'initial_search_restart_seed_topology_novelty', 'initial_search_restart_seed_topology_scan_limit', 'initial_search_funnel_probe_cycle_enabled', 'initial_search_funnel_probe_cycle_count', 'initial_search_funnel_probe_cycle_global_outer', 'initial_search_funnel_probe_cycle_global_develop_outer', 'initial_search_funnel_probe_cycle_global_de_gen', 'initial_search_funnel_probe_cycle_probe_count', 'initial_search_funnel_probe_cycle_source_pool_size', 'initial_search_funnel_probe_cycle_archive_per_stage', 'initial_search_funnel_probe_cycle_population_scan', 'initial_search_topology_graft_enabled', 'initial_search_topology_graft_source_pool_size', 'initial_search_topology_graft_count', 'initial_search_topology_graft_donor_count', 'initial_search_topology_graft_segments_per_donor', 'initial_search_topology_graft_window_radius', 'initial_search_topology_graft_blend', 'initial_search_topology_graft_scale', 'initial_search_topology_graft_popsize', 'initial_search_topology_graft_outer', 'initial_search_topology_graft_de_gen', 'initial_search_topology_graft_enable_polish', 'initial_search_de_variant', 'initial_search_constraint_handling', 'initial_search_basin_max', 'initial_search_basin_output_count', 'initial_search_basin_radius', 'initial_search_basin_adaptive_counts', 'initial_search_basin_adaptive_output_cap', 'initial_search_basin_adaptive_max_cap', 'initial_search_basin_active_diversity', 'initial_search_basin_active_count', 'initial_search_basin_adaptive_active_cap', 'initial_search_basin_balanced_refresh', 'initial_search_basin_semantic_diversity', 'initial_search_basin_semantic_max_per_topology', 'initial_search_basin_active_local_cohort', 'initial_search_basin_active_local_cohort_fraction', 'initial_search_basin_active_local_cohort_size', 'initial_search_basin_active_local_cohort_max_distance', 'initial_search_basin_active_mating_quota', 'initial_search_basin_active_quality_gate', 'initial_search_basin_active_require_trace_feasible', 'initial_search_basin_active_selection_mode', 'initial_search_basin_active_min_edge', 'initial_search_basin_active_max_merit_ratio', 'initial_search_basin_active_max_merit_delta', 'initial_search_basin_active_max_slots_per_family', 'initial_search_basin_active_mating_match_mode', 'initial_search_basin_active_family_similarity_threshold', 'initial_search_basin_active_family_max_shape_diff', 'initial_search_basin_active_archive_only', 'initial_search_basin_active_slot_strict_frozen_family', 'initial_search_basin_active_family_parameter_merge_enabled', 'initial_search_basin_active_family_parameter_merge_distance', 'initial_search_basin_active_family_parameter_merge_metric', 'initial_search_basin_active_refresh_rebalance', 'initial_search_basin_active_refresh_max_family_fraction', 'initial_search_basin_active_refresh_min_local_fraction', 'initial_search_de_mating_scope', 'initial_search_de_mating_local_rate', 'initial_search_de_mating_min_pool', 'initial_search_de_mating_strict_local', 'initial_search_pbest_scope', 'initial_search_pbest_basin_rate', 'initial_search_pbest_same_key_rate', 'initial_search_pbest_global_rate', 'initial_search_pbest_global_fallback', 'initial_search_pbest_basin_nearest_fallback', 'initial_search_checkpoint_nonrank_count', 'initial_search_checkpoint_total_count', 'initial_search_checkpoint_merit_relaxation_enabled', 'initial_search_checkpoint_basin_fill_enabled', 'initial_search_checkpoint_de_population', 'initial_search_checkpoint_de_generations', 'initial_search_checkpoint_selected_count', *state.DE_DLS_MATERIAL_TERMINAL_OPTION_KEYS, 'initial_search_checkpoint_population_scale', 'initial_search_checkpoint_optimizer_seed', 'initial_search_checkpoint_population_seed', 'initial_search_checkpoint_de_use_cuda', 'initial_search_checkpoint_multi_island_enabled', 'initial_search_checkpoint_de_timeout', 'initial_search_de_dls_workers', 'initial_search_de_dls_child_cpu_workers', 'initial_search_de_dls_max_basins', 'initial_search_de_dls_selection_count', 'initial_search_de_dls_selection_merit_count', 'initial_search_de_dls_selection_potential_count', 'initial_search_de_dls_selection_diversity_count', 'initial_search_de_dls_potential_local_iter', 'initial_search_de_dls_potential_local_lambda', 'initial_search_de_dls_potential_local_fd', 'initial_search_de_dls_potential_local_tol', 'initial_search_de_dls_potential_local_max_reject', 'initial_search_de_dls_restart_max_cycles', 'initial_search_de_dls_restart_min_cycles', 'initial_search_de_dls_restart_local_iter_schedule', 'initial_search_de_dls_max_local_iter_per_restart', 'initial_search_de_dls_restart_local_lambda', 'initial_search_de_dls_restart_local_fd', 'initial_search_de_dls_restart_local_tol', 'initial_search_de_dls_restart_local_max_reject', 'initial_search_de_dls_material_entry_count', 'initial_search_de_dls_material_entry_merit_count', 'initial_search_de_dls_material_entry_potential_count', 'initial_search_de_dls_material_entry_diversity_count', 'initial_search_de_dls_material_entry_quality_guard_ratio', 'initial_search_de_dls_material_entry_selection_mode', *state.DE_DLS_MATERIAL_BRIDGE_OPTION_KEYS, *state.DE_DLS_MATERIAL_CHROMATIC_REPAIR_OPTION_KEYS, *state.DE_DLS_GLASS_SCOUT_OPTION_KEYS, 'initial_search_de_dls_material_keep', 'initial_search_de_dls_material_max_per_topology', 'initial_search_de_dls_material_min_field_count', 'initial_search_de_dls_material_selection_mode', 'initial_search_de_dls_material_local_iter', 'initial_search_de_dls_material_stage_s01_local_iter', 'initial_search_de_dls_material_stage_s02_local_iter', 'initial_search_de_dls_material_stage_s03_local_iter', 'initial_search_de_dls_material_stage_s04_local_iter', 'initial_search_de_dls_material_stage_s05_local_iter', 'initial_search_de_dls_material_stage_s06_local_iter', 'initial_search_de_dls_material_local_lambda', 'initial_search_de_dls_material_local_fd', 'initial_search_de_dls_material_local_tol', 'initial_search_de_dls_material_local_max_reject', 'initial_search_de_dls_material_local_plateau_window', 'initial_search_de_dls_material_local_plateau_min_iterations', 'initial_search_de_dls_material_local_plateau_min_abs_improvement', 'initial_search_de_dls_material_local_plateau_min_rel_improvement', 'initial_search_de_dls_material_restart_early_stop_enabled', 'initial_search_de_dls_material_restart_early_stop_max_iterations', 'initial_search_de_dls_material_restart_early_stop_min_abs_improvement', 'initial_search_de_dls_material_restart_early_stop_min_rel_improvement', 'initial_search_de_dls_local_method', 'initial_search_de_dls_local_bounds_mode', 'initial_search_de_dls_local_residual_mode', 'initial_search_de_dls_geometry_kinds', 'initial_search_de_dls_geometry_field_hy_fractions', 'initial_search_de_dls_optimizer_internal_scaling', 'initial_search_de_dls_compact_outputs', *state.DE_DLS_FIXED_GLASS_FEEDBACK_OPTION_KEYS, *state.DE_DLS_FINAL_LOCKED_GLASS_DLS_OPTION_KEYS, *state.POST_ASPHERE_REFINE_OPTION_KEYS] if key in options}
    apply_flat_topology_funnel_defaults(options)
    options.update(preserved_keys)
    options['initial_search_optimizer_mode'] = state.INITIAL_SEARCH_DE_DLS_MATERIAL_FUNNEL_MODE
    options['initial_search_disable_polish'] = True
    options['initial_search_funnel_global_enable_polish'] = False
    options['initial_search_funnel_global_micro_island_enabled'] = False
    options['initial_search_funnel_probe_cycle_enabled'] = False
    options['initial_search_funnel_global_adaptive_glass_tolerance_enabled'] = False
    options['initial_search_funnel_global_early_stop_enabled'] = True
    options['initial_search_funnel_global_early_stop_metric'] = 'best_merit'
    options['initial_search_funnel_global_early_stop_min_epochs'] = 2
    options['initial_search_funnel_global_early_stop_window'] = 2
    options['initial_search_funnel_global_early_stop_min_topologies'] = 0
    options['initial_search_funnel_global_early_stop_min_rel_improve'] = 0.01
    options['initial_search_funnel_global_early_stop_min_abs_improve'] = 0.0
    options['initial_search_funnel_global_early_stop_include_initial_point'] = True
    options['initial_search_funnel_global_early_stop_generation_window'] = 400
    options['initial_search_funnel_global_plateau_retire_evict'] = False
    options['initial_search_funnel_global_plateau_retire_frontier_on_first_update'] = False
    options['initial_search_basin_active_niche_tracking'] = False
    options['initial_search_basin_active_niche_survival'] = False
    options['initial_search_basin_active_niche_rebalance'] = False
    options['initial_search_basin_active_niche_soft_fill'] = False
    options['initial_search_basin_active_niche_min_quota'] = 0
    options['initial_search_basin_active_niche_target_quota'] = 0
    options['initial_search_basin_active_focus_rebalance'] = False
    options['initial_search_basin_active_focus_rotation'] = False
    options['initial_search_basin_active_focus_soft_fill'] = False
    options['initial_search_basin_active_focus_stop_release'] = False
    options['initial_search_basin_active_plateau_lifecycle'] = False
    options['initial_search_basin_active_plateau_track_active_niches'] = False
    _lensde_configuration.sync_unified_ray_aiming_options(options)
    options.setdefault('initial_search_de_dls_workers', 2)
    options.setdefault('initial_search_de_dls_child_cpu_workers', 0)
    options.setdefault('initial_search_checkpoint_nonrank_count', 15)
    options.setdefault('initial_search_checkpoint_total_count', 20)
    options.setdefault('initial_search_checkpoint_merit_relaxation_enabled', True)
    options.setdefault('initial_search_checkpoint_basin_fill_enabled', True)
    options.setdefault('initial_search_checkpoint_de_population', 100)
    options.setdefault('initial_search_checkpoint_de_generations', 400)
    options.setdefault('initial_search_checkpoint_selected_count', 7)
    terminal_mode = str(options.get('initial_search_de_dls_material_terminal_mode') or 'scout_lock_fixed_de').strip().lower()
    if terminal_mode not in {'scout_lock_fixed_de', 'direct_glass_de', 'funnel'}:
        terminal_mode = 'scout_lock_fixed_de'
    options['initial_search_de_dls_material_terminal_mode'] = terminal_mode
    discrete_terminal = terminal_mode in {'scout_lock_fixed_de', 'direct_glass_de'}
    options.setdefault('initial_search_checkpoint_handoff_count', 7 if discrete_terminal else 0)
    options.setdefault('initial_search_checkpoint_handoff_merit_count', 7)
    options.setdefault('initial_search_checkpoint_handoff_potential_count', 0)
    options.setdefault('initial_search_checkpoint_handoff_diversity_count', 0)
    options.setdefault('initial_search_checkpoint_population_scale', 'medium')
    options.setdefault('initial_search_checkpoint_optimizer_seed', 20261729)
    options.setdefault('initial_search_checkpoint_population_seed', 2026072400)
    options.setdefault('initial_search_checkpoint_de_use_cuda', True)
    options.setdefault('initial_search_checkpoint_multi_island_enabled', True)
    options.setdefault('initial_search_checkpoint_de_timeout', 0)
    options.setdefault('initial_search_de_dls_max_basins', 48)
    options.setdefault('initial_search_de_dls_selection_count', 12)
    options.setdefault('initial_search_de_dls_selection_merit_count', 4)
    options.setdefault('initial_search_de_dls_selection_potential_count', 6)
    options.setdefault('initial_search_de_dls_selection_diversity_count', 2)
    options.setdefault('initial_search_de_dls_potential_local_iter', 400)
    options.setdefault('initial_search_de_dls_potential_local_lambda', 0.01)
    options.setdefault('initial_search_de_dls_potential_local_fd', 0.0001)
    options.setdefault('initial_search_de_dls_potential_local_tol', 1e-06)
    options.setdefault('initial_search_de_dls_potential_local_max_reject', 200)
    options.setdefault('initial_search_de_dls_restart_max_cycles', 4)
    options.setdefault('initial_search_de_dls_restart_min_cycles', 2)
    options.setdefault('initial_search_de_dls_restart_local_iter_schedule', '400,400,400,400')
    options.setdefault('initial_search_de_dls_max_local_iter_per_restart', 400)
    options.setdefault('initial_search_de_dls_restart_local_lambda', 0.001)
    options.setdefault('initial_search_de_dls_restart_local_fd', 2e-05)
    options.setdefault('initial_search_de_dls_restart_local_tol', 1e-08)
    options.setdefault('initial_search_de_dls_restart_local_max_reject', 500)
    if legacy_material_terminal and discrete_terminal:
        options['initial_search_checkpoint_handoff_count'] = 7
        options['initial_search_checkpoint_handoff_merit_count'] = 7
        options['initial_search_checkpoint_handoff_potential_count'] = 0
        options['initial_search_checkpoint_handoff_diversity_count'] = 0
        options['initial_search_de_dls_material_entry_count'] = 4
        options['initial_search_de_dls_material_entry_merit_count'] = 4
        options['initial_search_de_dls_material_entry_potential_count'] = 0
        options['initial_search_de_dls_material_entry_diversity_count'] = 0
        options['initial_search_de_dls_material_entry_selection_mode'] = 'merit'
        options['initial_search_de_dls_fixed_glass_feedback_source_count'] = 4
        options['initial_search_de_dls_fixed_glass_feedback_material_modes'] = 'locked_glass' if terminal_mode == 'scout_lock_fixed_de' else 'true_glass'
    else:
        options.setdefault('initial_search_de_dls_material_entry_count', 4 if discrete_terminal else 7)
        options.setdefault('initial_search_de_dls_material_entry_merit_count', 2 if discrete_terminal else 4)
        options.setdefault('initial_search_de_dls_material_entry_potential_count', 2)
        options.setdefault('initial_search_de_dls_material_entry_diversity_count', 0 if discrete_terminal else 1)
    options.setdefault('initial_search_de_dls_material_entry_quality_guard_ratio', 1.1)
    options.setdefault('initial_search_de_dls_material_entry_selection_mode', 'hybrid')
    if discrete_terminal:
        try:
            entry_count = max(1, int(options.get('initial_search_de_dls_material_entry_count', 4) or 4))
        except (TypeError, ValueError):
            entry_count = 4
        options['initial_search_de_dls_material_entry_count'] = entry_count
        options['initial_search_de_dls_material_entry_merit_count'] = entry_count
        options['initial_search_de_dls_material_entry_potential_count'] = 0
        options['initial_search_de_dls_material_entry_diversity_count'] = 0
        options['initial_search_de_dls_material_entry_selection_mode'] = 'merit'
    for key, default in state.DE_DLS_MATERIAL_BRIDGE_DEFAULTS:
        options.setdefault(key, default)
    for key, default in state.DE_DLS_MATERIAL_CHROMATIC_REPAIR_DEFAULTS:
        options.setdefault(key, default)
    for key, default in state.DE_DLS_GLASS_SCOUT_DEFAULTS:
        options.setdefault(key, default)
    options['initial_search_de_dls_material_bridge_field_hy_fractions'] = options['initial_search_de_dls_glass_scout_field_hy_fractions']
    options['initial_search_de_dls_material_bridge_trace_rays'] = options['initial_search_de_dls_glass_scout_trace_rays']
    options['initial_search_de_dls_material_bridge_trace_arms'] = options['initial_search_de_dls_glass_scout_trace_arms']
    options['initial_search_de_dls_material_bridge_trace_distribution'] = options['initial_search_de_dls_glass_scout_trace_distribution']
    options.setdefault('initial_search_de_dls_material_keep', 8)
    options.setdefault('initial_search_de_dls_material_max_per_topology', 1)
    options.setdefault('initial_search_de_dls_material_min_field_count', 5)
    options.setdefault('initial_search_de_dls_material_selection_mode', 'hybrid')
    options.setdefault('initial_search_de_dls_material_local_iter', 400)
    for key, default in state.DE_DLS_MATERIAL_STAGE_LOCAL_ITER_KEYS:
        options.setdefault(key, default)
    options.setdefault('initial_search_de_dls_material_local_lambda', 0.01)
    options.setdefault('initial_search_de_dls_material_local_fd', 0.0001)
    options.setdefault('initial_search_de_dls_material_local_tol', 0.0001)
    options.setdefault('initial_search_de_dls_material_local_max_reject', 40)
    options.setdefault('initial_search_de_dls_material_local_plateau_window', 50)
    options.setdefault('initial_search_de_dls_material_local_plateau_min_iterations', 80)
    options.setdefault('initial_search_de_dls_material_local_plateau_min_abs_improvement', 0.02)
    options.setdefault('initial_search_de_dls_material_local_plateau_min_rel_improvement', 0.01)
    options.setdefault('initial_search_de_dls_material_restart_early_stop_enabled', True)
    options.setdefault('initial_search_de_dls_material_restart_early_stop_max_iterations', 20)
    options.setdefault('initial_search_de_dls_material_restart_early_stop_min_abs_improvement', 0.002)
    options.setdefault('initial_search_de_dls_material_restart_early_stop_min_rel_improvement', 0.001)
    options.setdefault('initial_search_de_dls_local_method', 'trf')
    options.setdefault('initial_search_de_dls_local_bounds_mode', 'safety_material_strict')
    options.setdefault('initial_search_de_dls_local_residual_mode', 'expanded')
    options.setdefault('initial_search_de_dls_geometry_kinds', '')
    options.setdefault('initial_search_de_dls_geometry_field_hy_fractions', '')
    options.setdefault('initial_search_de_dls_optimizer_internal_scaling', 'edge_sag_normalized')
    options.setdefault('initial_search_de_dls_compact_outputs', True)
    if terminal_mode == 'funnel':
        options.setdefault('initial_search_de_dls_fixed_glass_feedback_source_count', 5)
        options.setdefault('initial_search_de_dls_fixed_glass_feedback_material_modes', 'continuous')
    for key, default in state.DE_DLS_FIXED_GLASS_FEEDBACK_DEFAULTS:
        options.setdefault(key, default)
    for key, default in state.DE_DLS_FINAL_LOCKED_GLASS_DLS_DEFAULTS:
        options.setdefault(key, default)
    if terminal_mode == 'scout_lock_fixed_de':
        options['initial_search_de_dls_fixed_glass_feedback_enabled'] = True
        try:
            scout_source_count = int(options.get('initial_search_de_dls_material_entry_count', 4) or 4)
        except (TypeError, ValueError):
            scout_source_count = 4
        options['initial_search_de_dls_fixed_glass_feedback_source_count'] = max(1, scout_source_count)
        options['initial_search_de_dls_fixed_glass_feedback_material_modes'] = 'locked_glass'
    if terminal_mode == 'direct_glass_de':
        options['initial_search_de_dls_fixed_glass_feedback_enabled'] = True
        try:
            direct_source_count = int(options.get('initial_search_de_dls_material_entry_count', 4) or 4)
        except (TypeError, ValueError):
            direct_source_count = 4
        options['initial_search_de_dls_fixed_glass_feedback_source_count'] = max(1, direct_source_count)
        options['initial_search_de_dls_fixed_glass_feedback_material_modes'] = 'true_glass'
    options['initial_search_de_dls_fixed_glass_feedback_mode'] = 'refine_like'
    for key in state.DE_DLS_FIXED_GLASS_FEEDBACK_LEGACY_MERIT_OPTION_KEYS:
        options.pop(key, None)
    for key in state.POST_ASPHERE_MULTISTART_LEGACY_LONG_OPTION_KEYS:
        options.pop(key, None)
    for key, default in state.POST_ASPHERE_REFINE_DEFAULTS:
        options.setdefault(key, default)
    saved_root_policy = str(options.get('initial_search_post_asphere_root_scoring_policy') or '').strip().lower().replace('-', '_').replace(' ', '_')
    if saved_root_policy in {'two_level_fast_recover', 'two_level'}:
        options['initial_search_post_asphere_root_scoring_policy'] = 'confirmed_with_fallback'
    elif saved_root_policy == 'fast_recover':
        options['initial_search_post_asphere_root_scoring_policy'] = 'experimental_fast_recover'
    options['initial_search_post_asphere_single_mother_dual_contract_enabled'] = True


def apply_initial_search_run_profile_defaults(options: dict) -> None:
    profile = canonical_initial_search_run_profile(options.get('initial_search_run_profile'))
    options['initial_search_run_profile'] = profile
    options['initial_search_optimizer_mode'] = state.INITIAL_SEARCH_DE_DLS_MATERIAL_FUNNEL_MODE
    if profile == 'custom':
        return
    options['initial_search_checkpoint_nonrank_count'] = 15
    options['initial_search_checkpoint_total_count'] = 20
    options['initial_search_checkpoint_merit_relaxation_enabled'] = True
    options['initial_search_checkpoint_basin_fill_enabled'] = True
    options['initial_search_checkpoint_de_population'] = 100
    options['initial_search_checkpoint_de_generations'] = 400
    options['initial_search_checkpoint_selected_count'] = 7
    options['initial_search_de_dls_material_terminal_mode'] = 'scout_lock_fixed_de'
    options['initial_search_checkpoint_handoff_count'] = 7
    options['initial_search_checkpoint_handoff_merit_count'] = 7
    options['initial_search_checkpoint_handoff_potential_count'] = 0
    options['initial_search_checkpoint_handoff_diversity_count'] = 0
    options['initial_search_checkpoint_population_scale'] = 'medium'
    options['initial_search_checkpoint_de_use_cuda'] = True
    options['initial_search_checkpoint_multi_island_enabled'] = True
    options['initial_search_de_dls_workers'] = 2
    options['initial_search_de_dls_material_max_per_topology'] = 1
    options['initial_search_de_dls_material_local_iter'] = 400
    options['initial_search_de_dls_material_entry_count'] = 4
    options['initial_search_de_dls_material_entry_merit_count'] = 4
    options['initial_search_de_dls_material_entry_potential_count'] = 0
    options['initial_search_de_dls_material_entry_diversity_count'] = 0
    options['initial_search_de_dls_material_entry_selection_mode'] = 'merit'
    options['initial_search_de_dls_fixed_glass_feedback_enabled'] = True
    options['initial_search_de_dls_fixed_glass_feedback_source_count'] = 4
    options['initial_search_de_dls_fixed_glass_feedback_material_modes'] = 'locked_glass'
    options['initial_search_de_dls_fixed_glass_feedback_glass_candidates'] = 14
    options['initial_search_de_dls_glass_scout_round_mode'] = 'auto'
    options['initial_search_de_dls_glass_scout_fixed_rounds'] = 3
    options['initial_search_de_dls_glass_scout_population'] = 240
    options['initial_search_de_dls_glass_scout_generations'] = 200
    options['initial_search_de_dls_glass_scout_multi_island_enabled'] = True
    options['initial_search_topology_graft_enabled'] = False
    if profile == 'single_pass_demo':
        options['initial_search_funnel_global_explore_full_restart_enabled'] = True
        options['initial_search_funnel_global_explore_full_restart_avoid_previous'] = True
        options['initial_search_restart_seed_topology_novelty'] = False
        options['initial_search_funnel_probe_cycle_enabled'] = False
        options['initial_search_funnel_global_explore_outer'] = 5
        options['initial_search_funnel_global_explore_de_gen'] = 1000
        options['initial_search_basin_output_count'] = 48
        options['initial_search_de_dls_max_basins'] = 48
        options['initial_search_de_dls_selection_count'] = 12
        options['initial_search_de_dls_selection_merit_count'] = 4
        options['initial_search_de_dls_selection_potential_count'] = 6
        options['initial_search_de_dls_selection_diversity_count'] = 2
        options['initial_search_de_dls_material_entry_quality_guard_ratio'] = 1.1
        options['initial_search_de_dls_material_entry_selection_mode'] = 'merit'
        options['initial_search_de_dls_material_keep'] = 8
        return
    if profile == 'robust_seed_archive':
        options['initial_search_funnel_global_explore_full_restart_enabled'] = True
        options['initial_search_funnel_global_explore_full_restart_avoid_previous'] = True
        options['initial_search_funnel_global_explore_full_restart_shared_seed_pool'] = True
        options['initial_search_funnel_global_explore_full_restart_seed_pool_multiplier'] = 1.0
        options['initial_search_funnel_global_explore_full_restart_archive_limit'] = 0
        options['initial_search_funnel_global_explore_full_restart_avoid_penalty'] = 1000.0
        options['initial_search_funnel_global_explore_full_restart_avoid_similarity_threshold'] = 0.9
        options['initial_search_funnel_global_explore_full_restart_avoid_max_shape_diff'] = 3
        options['initial_search_funnel_global_explore_full_restart_avoid_continuous_structure_enabled'] = True
        options['initial_search_funnel_global_explore_full_restart_avoid_continuous_power_cosine_min'] = 0.99
        options['initial_search_funnel_global_explore_full_restart_avoid_continuous_power_distance_max'] = 0.18
        options['initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_cosine_min'] = 0.97
        options['initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_distance_max'] = 0.25
        options['initial_search_funnel_global_explore_full_restart_avoid_continuous_thickness_distance_max'] = 0.1
        options['initial_search_funnel_global_explore_full_restart_avoid_continuous_gap_distance_max'] = 0.1
        options['initial_search_funnel_global_explore_full_restart_avoid_continuous_significance_fraction'] = 0.08
        options['initial_search_funnel_global_explore_full_restart_avoid_parameter_auxiliary_similarity_min'] = 0.9
        options['initial_search_funnel_global_explore_full_restart_avoid_memory_policy'] = 'qualified_similarity'
        options['initial_search_funnel_global_explore_full_restart_avoid_high_quality_merit_ratio'] = 3.0
        options['initial_search_funnel_global_explore_full_restart_avoid_memory_max_merit_ratio'] = 6.0
        options['initial_search_restart_seed_topology_novelty'] = False
        options['initial_search_restart_seed_topology_scan_limit'] = 0
        options['initial_search_funnel_global_explore_outer'] = 5
        options['initial_search_funnel_global_explore_de_gen'] = 1000
        options['initial_search_funnel_probe_cycle_enabled'] = False
        options['initial_search_de_dls_max_basins'] = 48
        options['initial_search_de_dls_selection_count'] = 12
        options['initial_search_de_dls_selection_merit_count'] = 4
        options['initial_search_de_dls_selection_potential_count'] = 6
        options['initial_search_de_dls_selection_diversity_count'] = 2
        options['initial_search_de_dls_material_entry_quality_guard_ratio'] = 1.1
        options['initial_search_de_dls_material_entry_selection_mode'] = 'merit'
        options['initial_search_de_dls_material_keep'] = 8
        return
    options['initial_search_funnel_global_explore_full_restart_enabled'] = True
    options['initial_search_funnel_global_explore_full_restart_avoid_previous'] = True
    options['initial_search_funnel_global_explore_full_restart_shared_seed_pool'] = True
    options['initial_search_funnel_global_explore_full_restart_seed_pool_multiplier'] = 1.0
    options['initial_search_funnel_global_explore_full_restart_archive_limit'] = 0
    options['initial_search_funnel_global_explore_full_restart_avoid_penalty'] = 1000.0
    options['initial_search_funnel_global_explore_full_restart_avoid_similarity_threshold'] = 0.9
    options['initial_search_funnel_global_explore_full_restart_avoid_max_shape_diff'] = 3
    options['initial_search_funnel_global_explore_full_restart_avoid_continuous_structure_enabled'] = True
    options['initial_search_funnel_global_explore_full_restart_avoid_continuous_power_cosine_min'] = 0.99
    options['initial_search_funnel_global_explore_full_restart_avoid_continuous_power_distance_max'] = 0.18
    options['initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_cosine_min'] = 0.97
    options['initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_distance_max'] = 0.25
    options['initial_search_funnel_global_explore_full_restart_avoid_continuous_thickness_distance_max'] = 0.1
    options['initial_search_funnel_global_explore_full_restart_avoid_continuous_gap_distance_max'] = 0.1
    options['initial_search_funnel_global_explore_full_restart_avoid_continuous_significance_fraction'] = 0.08
    options['initial_search_funnel_global_explore_full_restart_avoid_parameter_auxiliary_similarity_min'] = 0.9
    options['initial_search_funnel_global_explore_full_restart_avoid_memory_policy'] = 'qualified_similarity'
    options['initial_search_funnel_global_explore_full_restart_avoid_high_quality_merit_ratio'] = 3.0
    options['initial_search_funnel_global_explore_full_restart_avoid_memory_max_merit_ratio'] = 6.0
    options['initial_search_restart_seed_topology_novelty'] = False
    options['initial_search_restart_seed_topology_scan_limit'] = 0
    options['initial_search_funnel_global_explore_outer'] = 5
    options['initial_search_funnel_global_explore_de_gen'] = 1000
    options['initial_search_funnel_probe_cycle_enabled'] = False
    options['initial_search_funnel_probe_cycle_count'] = 3
    options['initial_search_funnel_probe_cycle_global_outer'] = 2
    options['initial_search_funnel_probe_cycle_global_develop_outer'] = 1
    options['initial_search_funnel_probe_cycle_global_de_gen'] = 80
    options['initial_search_funnel_probe_cycle_probe_count'] = 24
    options['initial_search_funnel_probe_cycle_source_pool_size'] = 64
    options['initial_search_funnel_probe_cycle_archive_per_stage'] = 48
    options['initial_search_funnel_probe_cycle_population_scan'] = 0
    options['initial_search_de_dls_max_basins'] = 72
    options['initial_search_de_dls_selection_count'] = 16
    options['initial_search_de_dls_selection_merit_count'] = 5
    options['initial_search_de_dls_selection_potential_count'] = 8
    options['initial_search_de_dls_selection_diversity_count'] = 3
    options['initial_search_de_dls_material_entry_quality_guard_ratio'] = 1.1
    options['initial_search_de_dls_material_entry_selection_mode'] = 'merit'
    options['initial_search_de_dls_material_keep'] = 12


def initial_search_profile_summary(options: dict) -> dict:
    return {'profile': canonical_initial_search_run_profile(options.get('initial_search_run_profile')), 'full_restart_enabled': bool(options.get('initial_search_funnel_global_explore_full_restart_enabled', False)), 'full_restart_seed_restarts': options.get('initial_search_funnel_global_explore_outer') if bool(options.get('initial_search_funnel_global_explore_full_restart_enabled', False)) else None, 'full_restart_avoid_previous': bool(options.get('initial_search_funnel_global_explore_full_restart_avoid_previous', False)), 'full_restart_shared_seed_pool': bool(options.get('initial_search_funnel_global_explore_full_restart_shared_seed_pool', True)), 'restart_seed_multiplier': options.get('initial_search_funnel_global_explore_full_restart_seed_pool_multiplier'), 'restart_archive_limit': options.get('initial_search_funnel_global_explore_full_restart_archive_limit'), 'restart_avoid_penalty': options.get('initial_search_funnel_global_explore_full_restart_avoid_penalty'), 'restart_avoid_similarity_threshold': options.get('initial_search_funnel_global_explore_full_restart_avoid_similarity_threshold'), 'restart_avoid_max_shape_diff': options.get('initial_search_funnel_global_explore_full_restart_avoid_max_shape_diff'), 'restart_avoid_continuous_structure_enabled': bool(options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_structure_enabled', True)), 'restart_avoid_continuous_power_cosine_min': options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_power_cosine_min'), 'restart_avoid_continuous_power_distance_max': options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_power_distance_max'), 'restart_avoid_continuous_curvature_cosine_min': options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_cosine_min'), 'restart_avoid_continuous_curvature_distance_max': options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_distance_max'), 'restart_avoid_continuous_thickness_distance_max': options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_thickness_distance_max'), 'restart_avoid_continuous_gap_distance_max': options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_gap_distance_max'), 'restart_avoid_continuous_significance_fraction': options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_significance_fraction'), 'restart_avoid_parameter_auxiliary_similarity_min': options.get('initial_search_funnel_global_explore_full_restart_avoid_parameter_auxiliary_similarity_min'), 'restart_avoid_memory_policy': options.get('initial_search_funnel_global_explore_full_restart_avoid_memory_policy'), 'restart_avoid_high_quality_merit_ratio': options.get('initial_search_funnel_global_explore_full_restart_avoid_high_quality_merit_ratio'), 'restart_avoid_memory_max_merit_ratio': options.get('initial_search_funnel_global_explore_full_restart_avoid_memory_max_merit_ratio'), 'restart_topology_novelty': bool(options.get('initial_search_restart_seed_topology_novelty', False)), 'restart_topology_scan_limit': options.get('initial_search_restart_seed_topology_scan_limit'), 'probe_cycle_enabled': bool(options.get('initial_search_funnel_probe_cycle_enabled', False)), 'probe_cycle_count': options.get('initial_search_funnel_probe_cycle_count'), 'topology_graft_enabled': bool(options.get('initial_search_topology_graft_enabled', False)), 'global_explore_outer': options.get('initial_search_funnel_global_explore_outer'), 'global_explore_de_gen': options.get('initial_search_funnel_global_explore_de_gen'), 'global_explore_de_use_cuda': _lensde_commands.option_truthy(options.get('initial_search_funnel_global_explore_de_use_cuda'), True), 'global_cra_tighten_enabled': _lensde_commands.option_truthy(options.get('initial_search_funnel_global_cra_tighten_enabled'), False), 'global_cra_tighten_generations': options.get('initial_search_funnel_global_cra_tighten_generations'), 'checkpoint_layer': {'total_count': options.get('initial_search_checkpoint_total_count'), 'nonrank_count': options.get('initial_search_checkpoint_nonrank_count'), 'population': options.get('initial_search_checkpoint_de_population'), 'generations': options.get('initial_search_checkpoint_de_generations'), 'selected_count': options.get('initial_search_checkpoint_selected_count'), 'handoff_count': options.get('initial_search_checkpoint_handoff_count'), 'handoff_priority_quotas': {'merit': options.get('initial_search_checkpoint_handoff_merit_count'), 'potential': options.get('initial_search_checkpoint_handoff_potential_count'), 'diversity': options.get('initial_search_checkpoint_handoff_diversity_count')}, 'population_scale': options.get('initial_search_checkpoint_population_scale'), 'use_cuda': _lensde_commands.option_truthy(options.get('initial_search_checkpoint_de_use_cuda'), True), 'multi_island_enabled': _lensde_commands.option_truthy(options.get('initial_search_checkpoint_multi_island_enabled'), True)}, 'dls_max_basins': options.get('initial_search_de_dls_max_basins'), 'dls_selection_count': options.get('initial_search_de_dls_selection_count'), 'material_terminal_mode': options.get('initial_search_de_dls_material_terminal_mode'), 'dls_material_entry_count': options.get('initial_search_de_dls_material_entry_count'), 'dls_material_bridge': {'enabled': bool(options.get('initial_search_de_dls_material_bridge_enabled', True)), 'field_hy_fractions': options.get('initial_search_de_dls_material_bridge_field_hy_fractions'), 'trace_rays': options.get('initial_search_de_dls_material_bridge_trace_rays'), 'trace_arms': options.get('initial_search_de_dls_material_bridge_trace_arms'), 'repair_population': options.get('initial_search_de_dls_material_bridge_repair_population'), 'repair_generations': options.get('initial_search_de_dls_material_bridge_repair_generations'), 'repair_population_scale': options.get('initial_search_de_dls_material_bridge_repair_population_scale'), 'deepen_population': options.get('initial_search_de_dls_material_bridge_deepen_population'), 'deepen_generations': options.get('initial_search_de_dls_material_bridge_deepen_generations'), 'extension_target_generations': options.get('initial_search_de_dls_material_bridge_extension_target_generations'), 'de_use_cuda': bool(options.get('initial_search_de_dls_material_bridge_de_use_cuda', True)), 'multi_island_enabled': _lensde_commands.option_truthy(options.get('initial_search_de_dls_material_bridge_multi_island_enabled'), True), 'multi_island_coalesce_us': options.get('initial_search_de_dls_material_bridge_multi_island_coalesce_us')}, 'dls_material_keep': options.get('initial_search_de_dls_material_keep'), 'dls_material_stage_local_iters': {key.removeprefix('initial_search_de_dls_material_stage_').removesuffix('_local_iter'): options.get(key) for key, _default in state.DE_DLS_MATERIAL_STAGE_LOCAL_ITER_KEYS}, 'fixed_glass_feedback_enabled': bool(options.get('initial_search_de_dls_fixed_glass_feedback_enabled', True)), 'fixed_glass_feedback_optimizer_contract': 'pure_adaptive_de_no_dls', 'fixed_glass_feedback_multi_island_enabled': bool(options.get('initial_search_de_dls_fixed_glass_feedback_multi_island_enabled', True)), 'fixed_glass_feedback_mode': options.get('initial_search_de_dls_fixed_glass_feedback_mode'), 'fixed_glass_feedback_generations': options.get('initial_search_de_dls_fixed_glass_feedback_generations'), 'fixed_glass_feedback_cycles': options.get('initial_search_de_dls_fixed_glass_feedback_cycles'), 'fixed_glass_feedback_refine_outer': options.get('initial_search_de_dls_fixed_glass_feedback_refine_outer'), 'fixed_glass_feedback_refine_de_gen': options.get('initial_search_de_dls_fixed_glass_feedback_refine_de_gen'), 'fixed_glass_feedback_source_count': options.get('initial_search_de_dls_fixed_glass_feedback_source_count'), 'fixed_glass_feedback_material_modes': options.get('initial_search_de_dls_fixed_glass_feedback_material_modes'), 'fixed_glass_feedback_glass_candidates': options.get('initial_search_de_dls_fixed_glass_feedback_glass_candidates'), 'fixed_glass_feedback_refine_early_stop': options.get('initial_search_de_dls_fixed_glass_feedback_refine_early_stop'), 'fixed_glass_feedback_edge_constraints': {'source': 'global_initial_search', 'min_edge_thickness': options.get('initial_search_first_order_min_edge'), 'glass_edge_thickness_lower': options.get('initial_search_glass_edge_thickness_lower'), 'glass_edge_thickness_upper': options.get('initial_search_glass_edge_thickness_upper'), 'air_edge_thickness_lower': options.get('initial_search_air_edge_thickness_lower'), 'air_edge_thickness_upper': options.get('initial_search_air_edge_thickness_upper')}, 'fixed_glass_feedback_merit_policy': 'inherit_per_source_candidate', 'fixed_glass_feedback_field_hy_fractions': options.get('initial_search_de_dls_fixed_glass_feedback_field_hy_fractions'), 'post_asphere_enabled': bool(options.get('initial_search_post_asphere_enabled', False)), 'post_asphere_flow': _lensde_asphere_contracts.canonical_post_asphere_flow(options.get('initial_search_post_asphere_flow')), 'post_asphere_lens_indices': options.get('initial_search_post_asphere_lens_indices'), 'post_asphere_candidate_count': options.get('initial_search_post_asphere_candidate_count'), 'post_asphere_max_order': options.get('initial_search_post_asphere_max_order'), 'post_asphere_bound_mode': options.get('initial_search_post_asphere_bound_mode'), 'post_asphere_rms_mode': options.get('initial_search_post_asphere_rms_mode'), 'post_asphere_plastic_enabled': bool(options.get('initial_search_post_asphere_plastic_enabled', False)), 'post_asphere_plastic_library': options.get('initial_search_post_asphere_plastic_library'), 'post_asphere_single_mother_schedule': {'mode': options.get('initial_search_post_asphere_single_mother_schedule'), 'formation_min_generations': options.get('initial_search_post_asphere_single_mother_formation_min_generations'), 'formation_max_generations': options.get('initial_search_post_asphere_single_mother_formation_generations'), 'post_formation_periodic_refresh': {'max_rounds': options.get('initial_search_post_asphere_periodic_refresh_max_rounds'), 'generations_per_round': options.get('initial_search_post_asphere_periodic_refresh_generations'), 'replacement_fraction': options.get('initial_search_post_asphere_periodic_refresh_fraction'), 'parent_count': options.get('initial_search_post_asphere_periodic_refresh_parent_count'), 'sag_um': options.get('initial_search_post_asphere_periodic_refresh_sag_um'), 'correlation_length': options.get('initial_search_post_asphere_periodic_refresh_correlation_length'), 'generation_mode': 'top_half_single_surface_correlated_sag', 'pre_stage_refresh': True, 'trace_feasible_gate': False, 'early_stop': options.get('initial_search_post_asphere_early_stop')}, 'uses_wall_clock': False}, 'post_asphere_fixed_field_contract': {'main_field_count': options.get('initial_search_post_asphere_field_count'), 'main_field_max_node': options.get('initial_search_post_asphere_main_field_max_node'), 'main_field_include_max_node': options.get('initial_search_post_asphere_main_field_include_max_node'), 'edge_nodes': options.get('initial_search_post_asphere_fixed_edge_nodes'), 'validation_field_count': options.get('initial_search_post_asphere_validation_field_count'), 'validation_audit_enabled': options.get('initial_search_post_asphere_validation_audit_enabled'), 'validation_audit_only': options.get('initial_search_post_asphere_validation_audit_only')}, 'post_asphere_multistart_schedule': _lensde_asphere_contracts.post_asphere_multistart_schedule(options), 'post_asphere_long_rounds': options.get('initial_search_post_asphere_long_rounds'), 'post_asphere_population_scale': options.get('initial_search_post_asphere_population_scale')}


def restart_decision_summary(options: dict, stage_summary: dict, global_summary: dict) -> dict:
    profile = canonical_initial_search_run_profile(options.get('initial_search_run_profile'))
    full_restart = bool(options.get('initial_search_funnel_global_explore_full_restart_enabled', False))
    probe_cycle = bool(options.get('initial_search_funnel_probe_cycle_enabled', False))

    def number_or_none(value) -> float | None:
        try:
            numeric = float(value)
        except (TypeError, ValueError):
            return None
        return numeric if math.isfinite(numeric) else None

    def first_number(mapping: dict, keys: list[str]) -> float | None:
        for key in keys:
            value = number_or_none(mapping.get(key))
            if value is not None:
                return value
        return None

    def best_candidate_merit(rows: list[dict], keys: list[str]) -> float | None:
        values: list[float] = []
        for row in rows:
            if not isinstance(row, dict):
                continue
            value = first_number(row, keys)
            if value is not None:
                values.append(value)
        return min(values) if values else None

    def top_merits(rows: list[dict], keys: list[str], count: int=3) -> list[float]:
        values: list[float] = []
        for row in rows:
            if not isinstance(row, dict):
                continue
            value = first_number(row, keys)
            if value is not None:
                values.append(value)
        return sorted(values)[:max(0, count)]
    global_payload = global_summary.get('summary') if isinstance(global_summary.get('summary'), dict) else global_summary
    pre_rows = stage_summary.get('pre_material_topologies') if isinstance(stage_summary, dict) else []
    continuous_rows = stage_summary.get('continuous_presnap_topologies') if isinstance(stage_summary, dict) else []
    final_rows = stage_summary.get('final_material_topologies') if isinstance(stage_summary, dict) else []
    pre_rows = pre_rows if isinstance(pre_rows, list) else []
    continuous_rows = continuous_rows if isinstance(continuous_rows, list) else []
    final_rows = final_rows if isinstance(final_rows, list) else []
    archive_basin_count = first_number(global_payload, ['count', 'basin_count', 'archive_basin_count'])
    effective_family_count = first_number(global_payload, ['optical_coarse_count', 'structural_topology_count', 'topology_count', 'semantic_count'])
    duplicate_rate = None
    if archive_basin_count and archive_basin_count > 0 and (effective_family_count is not None):
        duplicate_rate = max(0.0, min(1.0, 1.0 - effective_family_count / archive_basin_count))
    pre_best = best_candidate_merit(pre_rows, ['final_merit', 'merit'])
    continuous_best = best_candidate_merit(continuous_rows, ['continuous_final_merit', 'final_merit', 'merit'])
    final_best = best_candidate_merit(final_rows, ['final_merit', 'merit'])
    best_before_material = min([value for value in [pre_best, continuous_best] if value is not None], default=None)
    material_loss_ratio = final_best / best_before_material if final_best is not None and best_before_material is not None and (best_before_material > 0.0) else None
    metrics = {'effective_family_count': effective_family_count, 'duplicate_rate': duplicate_rate, 'best_final_merit': final_best, 'top3_final_merit': top_merits(final_rows, ['final_merit', 'merit'], 3), 'pre_material_best_merit': pre_best, 'continuous_presnap_best_merit': continuous_best, 'final_material_best_merit': final_best, 'material_loss_ratio': material_loss_ratio, 'trace_ok_count': first_number(global_payload, ['trace_ok_count', 'traceable_count']), 'archive_basin_count': archive_basin_count, 'global_explore_elapsed_seconds': first_number(global_summary, ['elapsed_seconds']), 'de_dls_elapsed_seconds': first_number(stage_summary, ['elapsed_seconds']) if isinstance(stage_summary, dict) else None, 'pre_material_candidate_count': len(pre_rows), 'continuous_presnap_candidate_count': len(continuous_rows), 'final_material_candidate_count': len(final_rows)}
    restart_need_score = 0
    reasons: list[str] = []
    if profile == 'best_effort':
        restart_need_score += 2
        reasons.append('best_effort profile requested the high-quality archive/funnel preset')
    if full_restart:
        reasons.append('global explore uses a merged full-restart archive before the single DLS funnel')
    else:
        reasons.append('single-pass archive was used before the DLS funnel')
    if probe_cycle:
        restart_need_score += 1
        reasons.append('probe-cycle is enabled for archive expansion')
    if metrics['final_material_candidate_count'] < 2:
        restart_need_score += 2
        reasons.append('fewer than two final-material candidates were promoted')
    if effective_family_count is not None and effective_family_count < 3:
        restart_need_score += 1
        reasons.append('global archive family count is low')
    if material_loss_ratio is not None and material_loss_ratio > 1.35:
        restart_need_score += 2
        reasons.append('material funnel degraded the best continuous/pre-material merit')
    if final_best is None and archive_basin_count and (archive_basin_count > 0):
        restart_need_score += 2
        reasons.append('archive exists but no final-material candidate was produced')
    if profile == 'best_effort':
        decision = 'best_effort'
    elif final_best is not None and metrics['final_material_candidate_count'] >= 2 and (restart_need_score <= 1):
        decision = 'accept'
    elif final_best is None and archive_basin_count and (archive_basin_count > 0):
        decision = 'archive_probe'
    elif effective_family_count is not None and effective_family_count <= 2:
        decision = 'full_restart'
    elif material_loss_ratio is not None and material_loss_ratio > 1.35:
        decision = 'best_effort'
    else:
        decision = 'graft' if final_best is not None else 'full_restart'
    return {'profile': profile, 'decision': decision, 'restart_need_score': restart_need_score, 'reasons': reasons, 'metrics': metrics, 'profile_summary': initial_search_profile_summary(options), 'global_explore_summary': global_summary, 'de_dls_material_funnel': stage_summary}


def safe_print(message: str) -> None:
    try:
        print(message, flush=True)
    except Exception:
        pass


def dependency_python_executable(required_modules: tuple[str, ...]=('numpy',)) -> str:
    """Return a Python interpreter capable of importing worker dependencies.

    The desktop GUI can run from a lightweight Python installation while the
    material tools need NumPy. Prefer the current interpreter, but probe an
    explicit override and the interpreter on PATH before launching a worker.
    """
    modules = tuple((str(module).strip() for module in required_modules if str(module).strip()))
    cached = state.DEPENDENCY_PYTHON_CACHE.get(modules)
    if cached:
        return cached
    conda_prefix = os.environ.get('CONDA_PREFIX', '').strip()
    user_home = Path.home()
    candidates = [os.environ.get('OPTILAND_DEPENDENCY_PYTHON', '').strip(), sys.executable, shutil.which('python') or '', str(Path(conda_prefix) / 'python.exe') if conda_prefix else '', str(user_home / 'anaconda3' / 'python.exe'), str(user_home / 'miniconda3' / 'python.exe')]
    unique_candidates: list[str] = []
    seen: set[str] = set()
    for candidate in candidates:
        if not candidate:
            continue
        key = os.path.normcase(os.path.abspath(candidate))
        if key in seen:
            continue
        seen.add(key)
        unique_candidates.append(candidate)
    probe = 'import importlib.util,sys;sys.exit(0 if all(importlib.util.find_spec(name) is not None for name in sys.argv[1:]) else 1)'
    for candidate in unique_candidates:
        try:
            completed = subprocess.run([candidate, '-c', probe, *modules], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10, check=False)
        except (OSError, subprocess.SubprocessError):
            continue
        if completed.returncode == 0:
            state.DEPENDENCY_PYTHON_CACHE[modules] = candidate
            if os.path.normcase(os.path.abspath(candidate)) != os.path.normcase(os.path.abspath(sys.executable)):
                safe_print(f"[python-runtime] selected dependency-capable interpreter: {candidate} (modules={','.join(modules)})")
            return candidate
    fallback = sys.executable
    state.DEPENDENCY_PYTHON_CACHE[modules] = fallback
    safe_print(f"[python-runtime] no interpreter satisfied required modules; falling back to {fallback} (modules={','.join(modules)})")
    return fallback


def curvature_from_radius(radius, default: float=0.0) -> float:
    try:
        value = float(radius)
    except (TypeError, ValueError):
        return default
    if not math.isfinite(value) or abs(value) <= 1e-14:
        return default
    return 1.0 / value


def curvature_bounds_from_radius_bounds(lower, upper) -> tuple[float, float]:
    try:
        lo = float(lower)
        hi = float(upper)
    except (TypeError, ValueError):
        return (-state.DEFAULT_CURVATURE_BOUND, state.DEFAULT_CURVATURE_BOUND)
    if math.isfinite(lo) and math.isfinite(hi) and (lo < hi) and (lo * hi > 0.0):
        c0 = 1.0 / lo
        c1 = 1.0 / hi
        return (min(c0, c1), max(c0, c1))
    return (-state.DEFAULT_CURVATURE_BOUND, state.DEFAULT_CURVATURE_BOUND)


def default_project() -> dict:
    project = {'system': {'name': 'optiland-cpp-gui', 'glass_library': 'SCHOTT', 'aperture': {'type': 'EPD', 'value': 8.0}, 'max_field_deg': 20.0, 'fields': [{'hx': 0.0, 'hy': 0.0, 'fvcx': 0.0, 'fvcy': 0.0, 'fvdx': 0.0, 'fvdy': 0.0}, {'hx': 0.0, 'hy': 0.707, 'fvcx': 0.0, 'fvcy': 0.0, 'fvdx': 0.0, 'fvdy': 0.0}, {'hx': 0.0, 'hy': 1.0, 'fvcx': 0.0, 'fvcy': 0.0, 'fvdx': 0.0, 'fvdy': 0.0}], 'wavelengths': [{'value_um': 0.4861, 'weight': 1.0, 'primary': False}, {'value_um': 0.5876, 'weight': 1.0, 'primary': True}, {'value_um': 0.6563, 'weight': 1.0, 'primary': False}], 'surfaces': [{'kind': 'object', 'radius': None, 'conic': 0.0, 'thickness': None, 'semi_aperture': 0.0, 'aperture_radius': 0.0, 'aperture': None, 'is_stop': False, 'reflective': False, 'asphere_coefficients': [], 'material_after': {'name': 'air', 'mode': 'air', 'reference': '', 'nd': 1.0, 'vd': 0.0}}, {'kind': 'plane', 'radius': None, 'conic': 0.0, 'thickness': 1.0, 'semi_aperture': 4.0, 'aperture_radius': 4.0, 'aperture': {'type': 'RadialAperture', 'r_max': 4.0, 'r_min': 0.0}, 'is_stop': True, 'reflective': False, 'asphere_coefficients': [], 'material_after': {'name': 'air', 'mode': 'air', 'reference': '', 'nd': 1.0, 'vd': 0.0}}, {'kind': 'standard', 'radius': 18.0, 'conic': 0.0, 'thickness': 2.0, 'semi_aperture': 6.0, 'aperture_radius': 0.0, 'aperture': None, 'is_stop': False, 'reflective': False, 'asphere_coefficients': [], 'material_after': {'name': 'demo-crown', 'mode': 'abbe', 'reference': '', 'nd': 1.5168, 'vd': 64.17}}, {'kind': 'standard', 'radius': -18.0, 'conic': 0.0, 'thickness': 10.0, 'semi_aperture': 6.0, 'aperture_radius': 0.0, 'aperture': None, 'is_stop': False, 'reflective': False, 'asphere_coefficients': [], 'material_after': {'name': 'air', 'mode': 'air', 'reference': '', 'nd': 1.0, 'vd': 0.0}}, {'kind': 'image', 'radius': None, 'conic': 0.0, 'thickness': 0.0, 'semi_aperture': 8.0, 'aperture_radius': 0.0, 'aperture': None, 'is_stop': False, 'reflective': False, 'asphere_coefficients': [], 'material_after': {'name': 'air', 'mode': 'air', 'reference': '', 'nd': 1.0, 'vd': 0.0}}]}, 'variables': [], 'merit': {'auto_merit': True, 'merit_model': 'unified_trace_gate', 'target_efl': 0.0, 'target_ttl': 0.0, 'target_bfl': 0.0, 'working_f_number_upper': 0.0, 'spot_target': 0.0, 'weight_efl': 1.0, 'weight_ttl': 0.4, 'weight_bfl': 0.5, 'weight_working_f_number': 1.0, 'weight_spot': 1.0, 'weight_edge': 0.5, 'image_quality_metric': 'rms_spot', 'fail_merit': 1000000000000.0, 'min_edge_thickness': 0.0, 'glass_center_thickness_lower': 0.5, 'glass_center_thickness_upper': 10.0, 'air_center_thickness_lower': 0.1, 'air_center_thickness_upper': 100.0, 'glass_edge_thickness_lower': 0.5, 'glass_edge_thickness_upper': 10.0, 'air_edge_thickness_lower': 0.1, 'air_edge_thickness_upper': 100.0, 'structural_prior_tight_triplet_weight': 5.0, 'structural_prior_gap_tolerance': 0.05, 'structural_prior_curvature_epsilon': 1e-09, 'pupil_order_prior_weight': 20.0, 'pupil_order_jacobian_floor': 0.05, 'pupil_order_soft_tolerance': 0.25, 'pupil_order_hard_jacobian': -0.0001, 'pupil_order_hard_max_progress': 1.0, 'pupil_order_hard_consecutive_surfaces': 1, 'pupil_order_min_field_radius': 0.75, 'vignetting_boundary_support_contract': 'global_trim_realizability', 'vignetting_physical_cut_match_enabled': True, 'vignetting_boundary_support_weight': 25.0, 'atc_lower': 0.1, 'atc_upper': 0.0, 'ate_lower': 0.1, 'ate_upper': 0.0, 'bl0_lower': 0.0, 'bl0_upper': 0.9, 'surface_tangent_angle_lower_deg': 30.0, 'surface_tangent_angle_samples': 32, 'trace_policy': 'soft_coverage', 'min_field_coverage': 1.0, 'min_system_coverage': 1.0, 'min_coverage_for_image_merit': 0.5, 'trace_failure_weight': 1.0, 'coverage_failure_weight': 10.0, 'aim_failure_weight': 1.0, 'tir_failure_weight': 10.0, 'aperture_failure_weight': 2.0, 'surface_miss_weight': 5.0, 'backward_failure_weight': 10.0, 'require_chief_ray': True, 'require_off_axis_field_coverage': True, 'hard_trace_gate_weight': 1000.0, 'trace_progress_weight': 0.0, 'chief_progress_weight': 0.0, 'trace_priority_weight': 100.0, 'allow_soft_aim_rays_for_spot': False, 'soft_aim_residual_for_spot': 0.0001, 'quality_spot_scale': 0.01, 'quality_wavefront_scale': 0.5, 'unified_image_mean_spot_weight': 3.0, 'unified_image_single_ray_spot_tail_weight': 0.012, 'single_ray_spot_tail_extra_edge_min_field_radius': 0.85, 'unified_image_wavefront_weight': 1.0, 'quality_single_ray_guard_ratio': 10.0, 'quality_efl_rel_tol': 0.02, 'quality_ttl_rel_tol': 0.01, 'quality_bfl_rel_tol': 0.05, 'quality_working_f_number_tol': 0.01, 'quality_length_abs_tol': 0.1, 'quality_edge_tol': 0.2, 'quality_ratio_tol': 0.01, 'quality_bound_tol': 0.05, 'minimum_relative_illumination': 0.0, 'quality_surface_tangent_angle_tol_deg': 5.0, 'quality_conic_coefficient_tol': 1.0, 'conic_coefficient_penalty_enabled': True, 'conic_coefficient_lower': -100.0, 'conic_coefficient_upper': 100.0, 'conic_coefficient_penalty_weight': 5.0, 'conic_coefficient_constraint_version': 2, 'quality_distortion_tol': 0.01, 'distortion_model': 'auto', 'quality_chief_ray_angle_tol': 5.0, 'quality_chief_ray_angle_tightened_tol': 0.5, 'quality_lateral_color_tol': 0.01, 'quality_axial_color_tol': 0.025, 'quality_spectral_spot_spread_tol': 0.0015, 'chromatic_spot_merit_enabled': True, 'distortion_upper': 0.0, 'chief_ray_angle_upper': 0.0, 'lateral_color_upper': 0.01, 'num_rays': 3, 'pupil_arms': 6, 'pupil_distribution': 'gaussian_ring_arm', 'codev_sampling_profile': 'spherical', 'codev_ray_interval': 0.0, 'codev_obscuration': 0.0, 'codev_spot_aperture_weight': 0.0, 'codev_wavefront_aperture_weight': 0.0, 'codev_use_symmetry': True, 'image_field_weight_mode': 'edge_power', 'image_field_edge_weight': 2.0, 'image_field_weight_exponent': 2.0, 'wavefront_reference': 'centroid_sphere', 'sampling_seed': 1, 'bounds': [], 'operands': [{'name': 'rms_spot', 'mode': 'minimize', 'weight': 1.0}, {'name': 'single_ray_spot_tail', 'mode': 'minimize', 'weight': 0.012}]}, 'options': {'aiming': 'paraxial', 'aiming_stop_radius_source': 'paraxial', 'aiming_iter': 20, 'aiming_require_convergence': False, 'aiming_continuation_steps': 8, 'aiming_pilot_fine_grid': 5, 'aiming_pilot_coarse_grid': 13, 'aiming_pilot_nearest_k': 3, 'aiming_pilot_iterative_failure_limit': 0, 'aiming_pilot_per_ray_iterative': True, 'aiming_pilot_allow_interpolation': False, 'aiming_calibrated_correction_iterations': 0, 'aiming_calibrated_sampling_mode': 'chief_edge_interpolation', 'aiming_calibrated_fallback': 'none', 'aiming_calibrated_chief_mode': 'abcd_budget_angle_bridge_fast', 'aiming_calibrated_budget_chief_iter': 8, 'aiming_calibrated_budget_chief_solver': 'hybrid', 'aiming_calibrated_budget_chief_acceptance': 'residual', 'aiming_calibrated_budget_chief_stop_fraction': 0.05, 'aiming_calibrated_budget_chief_residual_stop_fraction': 1e-06, 'aiming_calibrated_budget_chief_min_a': 1e-06, 'aiming_calibrated_budget_chief_field_angle': 75.0, 'aiming_calibrated_budget_chief_ignore_aperture_clip': True, 'aiming_calibrated_budget_chief_skip_abcd_angle': 60.0, 'aiming_calibrated_edge_calibration_enabled': True, 'aiming_calibrated_edge_calibration_iterations': 8, 'aiming_calibrated_edge_calibration_solver': 'secant', 'aiming_calibrated_edge_calibration_weight_power': 2.0, 'aiming_calibrated_edge_calibration_residual_stop_fraction': 1e-06, 'aiming_three_step_sampling_mode': 'chief_edge_interpolation', 'aiming_three_step_chief_rays': 33, 'aiming_three_step_max_angle': 89.0, 'aiming_three_step_edge_passes': 2, 'aiming_three_step_meridional_iter': 8, 'aiming_three_step_sagittal_iter': 8, 'aiming_three_step_edge_solver': 'secant', 'aiming_three_step_edge_residual_stop_fraction': 1e-06, 'aiming_three_step_edge_radius_fraction': 1.0, 'aiming_three_step_accept_stop_fraction': 0.05, 'aiming_three_step_correction_iterations': 0, 'aiming_three_step_sample_stop_residual_debug': False, 'aiming_three_step_fallback': 'adaptive_pilot_grid', 'initial_search_first_order_aiming_stop_radius_source': 'paraxial', 'initial_search_first_order_aiming_pilot_fine_grid': 5, 'initial_search_first_order_aiming_pilot_coarse_grid': 13, 'initial_search_first_order_aiming_pilot_nearest_k': 3, 'initial_search_first_order_aiming_pilot_iterative_failure_limit': 0, 'initial_search_first_order_aiming_pilot_per_ray_iterative': True, 'initial_search_first_order_aiming_pilot_allow_interpolation': False, 'clear_rings': 4, 'clear_aperture_update': True, 'sync_stop_aperture': True, 'spot_distribution': 'uniform', 'spot_rays': 41, 'spot_arms': 64, 'spot_sampling_seed': 1, 'spot_scale_mode': 'shared', 'use_cuda': False, 'quick_focus': False, 'optimizer_mode': 'initial_structure_search', 'adaptive_de_variant': 'jade_pbest_archive_lpsr_feas', 'adaptive_track_population_diversity': False, 'adaptive_constraint_handling': 'feasibility_rules', 'adaptive_pbest_fraction': 0.1, 'adaptive_pbest_scope': 'legacy', 'adaptive_pbest_basin_rate': 0.7, 'adaptive_pbest_same_key_rate': 0.2, 'adaptive_pbest_global_rate': 0.05, 'adaptive_pbest_global_fallback': True, 'adaptive_pbest_basin_nearest_fallback': False, 'adaptive_de_mating_scope': 'global', 'adaptive_de_mating_local_rate': 0.85, 'adaptive_de_mating_min_pool': 8, 'adaptive_de_mating_strict_local': False, 'adaptive_hybrid_inject_best': True, 'adaptive_archive_size_ratio': 1.0, 'adaptive_initial_popsize_multiplier': 15, 'adaptive_min_popsize_multiplier': 4, 'adaptive_min_population': 20, 'adaptive_soft_first_order': True, 'adaptive_first_order_epsilon': 1e-05, 'adaptive_polish_candidates': 8, 'adaptive_polish_refill_max_extra': 8, 'adaptive_basin_max': 8, 'adaptive_basin_radius': 0.05, 'adaptive_basin_distance_metric': 'structure_bound_normalized', 'adaptive_basin_air_thickness_distance_cap': 25.0, 'adaptive_basin_output_count': 8, 'adaptive_basin_active_diversity': False, 'adaptive_basin_active_count': 4, 'adaptive_basin_protected_fraction': 0.15, 'adaptive_basin_active_mating_match_mode': 'exact_key', 'adaptive_basin_active_max_slots_per_family': 1, 'adaptive_basin_active_family_similarity_threshold': 0.8, 'adaptive_basin_active_family_max_shape_diff': 3, 'adaptive_basin_active_archive_only': False, 'adaptive_basin_active_slot_strict_frozen_family': False, 'adaptive_basin_active_local_cohort': False, 'adaptive_basin_active_local_cohort_size': 0, 'adaptive_basin_active_local_cohort_fraction': 0.35, 'adaptive_basin_active_local_cohort_max_distance': 0.0, 'adaptive_basin_balanced_refresh': True, 'adaptive_basin_active_mating_quota': False, 'adaptive_basin_active_niche_tracking': False, 'adaptive_basin_active_niche_start_epoch': 1, 'adaptive_basin_active_niche_survival': False, 'adaptive_basin_active_niche_min_quota': 0, 'adaptive_basin_active_niche_target_quota': 0, 'adaptive_basin_active_niche_family_cap_fraction': 0.15, 'adaptive_basin_active_niche_repair_interval': 25, 'adaptive_basin_active_niche_repair_fraction': 0.03, 'adaptive_basin_active_niche_rebalance': False, 'adaptive_basin_active_niche_rebalance_interval': 25, 'adaptive_basin_active_niche_rebalance_fraction': 0.05, 'adaptive_basin_active_niche_soft_fill': False, 'adaptive_basin_active_niche_elite_seed_count': 1, 'adaptive_basin_active_focus_rebalance': False, 'adaptive_basin_active_focus_start_epoch': 1, 'adaptive_basin_active_focus_initial_best_family': True, 'adaptive_basin_active_focus_family_count': 1, 'adaptive_basin_active_focus_population_fraction': 0.55, 'adaptive_basin_active_focus_min_quota': 0, 'adaptive_basin_active_focus_soft_fill': False, 'adaptive_basin_active_focus_elite_seed_count': 1, 'adaptive_basin_active_monitor_family_cap': 0, 'adaptive_basin_active_focus_stop_rel_improvement': 0.2, 'adaptive_basin_active_focus_ready_fraction': 0.7, 'adaptive_basin_active_focus_cooldown_epochs': 2, 'adaptive_basin_active_focus_stop_after_clears': 0, 'adaptive_basin_active_focus_stop_release': True, 'adaptive_basin_active_focus_stop_release_fraction': 1.0, 'adaptive_basin_active_focus_rotation': False, 'adaptive_basin_active_focus_rotation_history_epochs': 8, 'adaptive_basin_active_focus_rotation_similarity_threshold': 0.8, 'adaptive_basin_active_refresh_rebalance': False, 'adaptive_basin_active_refresh_max_family_fraction': 0.0, 'adaptive_basin_active_refresh_min_local_fraction': 0.8, 'adaptive_basin_active_plateau_lifecycle': False, 'adaptive_basin_active_plateau_track_active_niches': False, 'adaptive_basin_active_plateau_window': 2, 'adaptive_basin_active_plateau_min_rel_improvement': 0.005, 'adaptive_basin_active_plateau_local_quota_scale': 0.35, 'adaptive_basin_active_plateau_external_probe_rate': 0.15, 'adaptive_basin_active_plateau_retire_after': 4, 'adaptive_basin_active_plateau_cooldown_epochs': 2, 'adaptive_basin_semantic_diversity': True, 'adaptive_basin_semantic_max_per_topology': 2, 'adaptive_topology_diversity_mode': 'legacy', 'adaptive_topology_descriptor_window_lmin': 2, 'adaptive_topology_descriptor_window_lmax': 4, 'adaptive_topology_descriptor_max_active_axes': 5, 'adaptive_topology_descriptor_audit_interval': 25, 'adaptive_topology_descriptor_weak_tau_low': 0.08, 'adaptive_topology_descriptor_weak_tau_high': 0.2, 'adaptive_topology_similarity_threshold': 0.9, 'population': 15, 'generations': 24, 'hybrid_outer': 6, 'hybrid_de_gen': 8, 'hybrid_parents': 4, 'hybrid_polish_workers': 0, 'hybrid_refresh': 0.1, 'refresh_generation_mode': 'restricted_perturbation', 'refresh_trace_feasible_gate': True, 'refresh_target_count': 0, 'refresh_max_candidates': 0, 'refresh_max_seconds': 0.0, 'refresh_parent_count': 8, 'refresh_curvature_rel': 0.2, 'refresh_air_thickness_rel': 0.15, 'refresh_glass_thickness_rel': 0.08, 'refresh_curvature_abs': 0.05, 'refresh_air_thickness_abs': 5.0, 'refresh_glass_thickness_abs': 0.5, 'refresh_perturb_material_variables': True, 'refresh_seed_pool_fraction': 0.0, 'refresh_seed_pool_perturbation': 0.0, 'refresh_trace_archive_enabled': True, 'refresh_trace_archive_capacity': 4096, 'refresh_trace_archive_fraction': 1.0, 'refresh_trace_archive_min_distance': 0.02, 'refresh_adaptive_trigger_enabled': True, 'refresh_adaptive_max_fraction': 0.1, 'refresh_adaptive_min_acceptance_rate': 0.02, 'refresh_adaptive_min_rel_improvement': 0.005, 'initial_generation_mode': 'random', 'initial_physics_target_ttl': 0.0, 'initial_physics_target_efl': 0.0, 'initial_physics_target_bfl': 0.0, 'initial_physics_ttl_min_fraction': 0.75, 'initial_physics_ttl_max_fraction': 1.0, 'initial_physics_power_jitter': 0.15, 'initial_physics_sa_iterations': 48, 'initial_physics_sa_temperature': 0.05, 'hybrid_distance_metric': 'bound_normalized', 'optimizer_internal_scaling': 'legacy_optical', 'de_strategy': 'best1bin', 'mutation_min': 0.5, 'mutation_max': 1.0, 'recombination': 0.7, 'de_tol': 0.01, 'de_atol': 0.0, 'de_override_bounds': False, 'de_curvature_abs_max': 0.0, 'de_conic_abs_max': 0.0, 'de_asphere_abs_max': 0.0, 'de_thickness_min': 0.0, 'de_thickness_max': 0.0, 'de_air_thickness_max': 0.0, 'de_glass_thickness_max': 0.0, 'de_material_nd_min': 0.0, 'de_material_nd_max': 0.0, 'de_material_vd_min': 0.0, 'de_material_vd_max': 0.0, 'seed': 20260515, 'initial_search_seed_policy': seed_protocol.NUMERIC_POLICY, 'initial_search_seed_profile': seed_protocol.GENERIC_PROFILE, 'protective_glass_identification_policy': 'fixed_geometry_v2', 'progress_every': 0, 'cpp_timeout': state.DEFAULT_CPP_TIMEOUT, 'hammer_attempts': 240, 'hammer_seed': 0, 'hammer_pre_polish_iter': 12, 'hammer_hot_polish_iter': 8, 'hammer_cold_polish_iter': 24, 'hammer_final_polish_iter': 80, 'hammer_temp_start': 0.05, 'hammer_temp_end': 1e-06, 'hammer_temp_power': 2.0, 'hammer_step_start': 0.08, 'hammer_step_end': 0.004, 'hammer_glass_rate_start': 0.2, 'hammer_glass_rate_end': 0.03, 'hammer_glass_random_rate': 0.03, 'hammer_glass_neighbor_count': 8, 'hammer_weight_continuous': 0.3, 'hammer_weight_gap': 0.18, 'hammer_weight_bend': 0.22, 'hammer_weight_glass': 0.18, 'hammer_weight_glass_repair': 0.12, 'hammer_image_spot_weight': 1000.0, 'hammer_merit_weight': 0.25, 'hammer_trace_penalty_weight': 100.0, 'hammer_constraint_penalty_weight': 25.0, 'hammer_first_order_penalty_weight': 25.0, 'hammer_write_trace': True, 'hammer_reject_trace_infeasible': False, 'initial_search_flow': 'legacy', 'initial_search_keep_candidates': 32, 'initial_search_candidate_diversity_radius': 0.15, 'initial_search_candidate_diversity_metric': 'structure_bound_normalized', 'initial_search_candidate_diversity_allow_relax': False, 'initial_search_candidate_diversity_topology_exempt': False, 'initial_search_candidate_air_thickness_distance_cap': 25.0, 'initial_search_candidate_quality_reserve_count': 3, 'initial_search_candidate_quality_gate': True, 'initial_search_candidate_quality_max_merit_ratio': 25.0, 'initial_search_candidate_quality_max_mean_spot_ratio': 4.0, 'initial_search_candidate_quality_mean_spot_floor': 0.08, 'initial_search_flat_start': True, 'initial_search_include_conic': False, 'initial_search_include_asphere': False, 'initial_search_asphere_lens_indices': '', 'initial_search_asphere_max_order': 16, 'initial_search_asphere_scout_enabled': True, 'initial_search_asphere_scout_allow_fallback': False, 'initial_search_asphere_scout_samples': 24, 'initial_search_asphere_scout_timeout': 30, 'initial_search_asphere_scout_min_trace_coverage': 0.95, 'initial_search_asphere_profile_scale': 1.0, 'initial_search_include_material': True, 'initial_search_wide_front_groups': 1, 'initial_search_front_profile': 'weak', 'initial_search_curvature_bound': state.DEFAULT_CURVATURE_BOUND, 'initial_search_rest_curvature_cap': 0.5, 'initial_search_population_generation_mode': 'restricted_perturbation', 'initial_search_population_trace_feasible_gate': True, 'initial_search_population_target_count': 2200, 'initial_search_population_max_candidates': 0, 'initial_search_population_max_seconds': 0.0, 'initial_search_population_batch_size': 0, 'initial_search_seed_air_thickness_cap': 25.0, 'initial_search_seed_glass_thickness_cap': 2.0, 'initial_search_adaptive_thickness_bounds': True, 'initial_search_adaptive_seed_thickness_caps': True, 'initial_search_use_current_thickness_prior': False, 'initial_search_bfl_reference_surface': -1, 'initial_search_bfl_lower_factor': 0.9, 'initial_search_bfl_upper_factor': 1.5, 'initial_search_anchor_cloud_coverage_injection': True, 'initial_search_auto_ttl_thickness_budget': True, 'initial_search_ttl_budget_trigger_ratio': 1.15, 'initial_search_ttl_budget_compact_slack_fraction': 0.55, 'initial_search_ttl_budget_lower_factor': 0.9, 'initial_search_ttl_budget_upper_factor': 1.03, 'initial_search_ttl_budget_trace_upper_factor': 1.2, 'initial_search_seed_ttl_allow_short': True, 'initial_search_seed_ttl_max_factor': 1.0, 'initial_search_budget_simplex_ttl_lower_fraction': 0.9, 'initial_search_budget_simplex_ttl_upper_fraction': 1.0, 'initial_search_budget_simplex_bfl_lower_factor': 0.9, 'initial_search_budget_simplex_bfl_upper_factor': 1.0, 'initial_search_budget_simplex_glass_beta_alpha': 2.0, 'initial_search_budget_simplex_glass_beta_beta': 4.0, 'initial_search_budget_simplex_air_dirichlet_alpha': 2.0, 'initial_search_air_thickness_min': 0.0, 'initial_search_air_thickness_max': 0.0, 'initial_search_glass_thickness_min': 0.0, 'initial_search_glass_thickness_max': 0.0, 'initial_search_seed_material_nd_cap': 0.45, 'initial_search_seed_material_vd_cap': 50.0, 'initial_search_population_local_group_perturbation': True, 'initial_search_population_local_long_air_gap_prior': True, 'initial_search_population_perturb_material': True, 'initial_search_population_perturb_other_variables': False, 'initial_search_material_stages_enabled': False, 'initial_search_material_stage_weak_ratio': 0.3, 'initial_search_material_stage_proximity_ratio': 0.3, 'initial_search_material_stage_discrete_ratio': 0.4, 'initial_search_glass_weak_weight': 0.05, 'initial_search_glass_weak_tolerance': 2.0, 'initial_search_glass_penalty_weight': 0.5, 'initial_search_glass_penalty_tolerance': 0.5, 'initial_search_glass_distance_nd_scale': state.GLASS_DISTANCE_ND_SCALE, 'initial_search_glass_distance_vd_scale': state.GLASS_DISTANCE_VD_SCALE, 'initial_search_glass_distance_vd_metric': state.GLASS_DISTANCE_VD_METRIC, 'initial_search_glass_distance_vd_relative_scale': state.GLASS_DISTANCE_VD_RELATIVE_SCALE, 'initial_search_glass_candidate_count': 64, 'initial_search_continuous_glass_penalty_enabled': True, 'initial_search_continuous_glass_penalty_weight': 0.5, 'initial_search_continuous_glass_penalty_tolerance': 1.5, 'initial_search_continuous_glass_smooth_temperature': 0.25, 'initial_search_continuous_glass_candidate_count': 256, 'initial_search_continuous_glass_region_enabled': False, 'initial_search_continuous_glass_region_weight': 0.5, 'initial_search_continuous_glass_region_tolerance': 0.0, 'initial_search_population': 1100, 'initial_search_seed_pool_population_multiplier': 2.0, 'initial_search_optimizer_mode': state.INITIAL_SEARCH_FUNNEL_MODE, 'initial_search_run_profile': 'single_pass_demo', 'initial_search_merit_strategy': 'full', 'initial_search_full_rescore_after_search': False, 'initial_search_hybrid_outer': 10, 'initial_search_hybrid_de_gen': 120, 'initial_search_hybrid_parents': 10, 'initial_search_polish_candidates': 16, 'initial_search_polish_refill_max_extra': 16, 'initial_search_polish_selection': 'topology_balanced', 'initial_search_de_variant': 'jade_pbest_archive', 'initial_search_track_population_diversity': False, 'initial_search_constraint_handling': 'scalar_merit', 'initial_search_hard_first_order': False, 'initial_search_first_order_epsilon': 1e-05, 'initial_search_pbest_fraction': 0.1, 'initial_search_pbest_scope': 'global', 'initial_search_pbest_basin_rate': 0.0, 'initial_search_pbest_same_key_rate': 0.0, 'initial_search_pbest_global_rate': 1.0, 'initial_search_pbest_global_fallback': True, 'initial_search_pbest_basin_nearest_fallback': False, 'initial_search_de_mating_scope': 'global', 'initial_search_de_mating_local_rate': 0.0, 'initial_search_de_mating_min_pool': 4, 'initial_search_de_mating_strict_local': False, 'initial_search_hybrid_inject_best': False, 'initial_search_archive_size_ratio': 1.0, 'initial_search_use_adaptive_multiplier': False, 'initial_search_initial_popsize_multiplier': 15, 'initial_search_min_popsize_multiplier': 4, 'initial_search_min_population': 20, 'initial_search_basin_max': 128, 'initial_search_basin_radius': 0.25, 'initial_search_basin_distance_metric': 'structure_bound_normalized', 'initial_search_basin_air_thickness_distance_cap': 25.0, 'initial_search_basin_output_count': 48, 'initial_search_basin_active_diversity': False, 'initial_search_basin_active_count': 9, 'initial_search_basin_active_selection_mode': 'topology_merit_potential', 'initial_search_basin_adaptive_counts': True, 'initial_search_basin_adaptive_reference_population': 600, 'initial_search_basin_adaptive_max_cap': 256, 'initial_search_basin_adaptive_output_cap': 48, 'initial_search_basin_adaptive_active_cap': 12, 'initial_search_basin_protected_fraction': 0.35, 'initial_search_basin_active_quality_gate': True, 'initial_search_basin_active_require_trace_feasible': True, 'initial_search_basin_active_mating_match_mode': 'family_similarity', 'initial_search_basin_active_max_slots_per_family': 1, 'initial_search_basin_active_family_similarity_threshold': 0.8, 'initial_search_basin_active_family_max_shape_diff': 3, 'initial_search_basin_active_archive_only': False, 'initial_search_basin_active_slot_strict_frozen_family': False, 'initial_search_basin_active_family_parameter_merge_enabled': False, 'initial_search_basin_active_family_parameter_merge_distance': 0.15, 'initial_search_basin_active_family_parameter_merge_metric': 'structure_bound_normalized', 'initial_search_basin_active_local_cohort': False, 'initial_search_basin_active_local_cohort_size': 0, 'initial_search_basin_active_local_cohort_fraction': 0.35, 'initial_search_basin_active_local_cohort_max_distance': 0.0, 'initial_search_basin_active_min_edge': 0.0, 'initial_search_basin_active_max_merit_ratio': 0.0, 'initial_search_basin_active_max_merit_delta': 200.0, 'initial_search_basin_balanced_refresh': False, 'initial_search_basin_active_mating_quota': False, 'initial_search_basin_active_niche_tracking': True, 'initial_search_basin_active_niche_start_epoch': 1, 'initial_search_basin_active_niche_survival': False, 'initial_search_basin_active_niche_min_quota': 0, 'initial_search_basin_active_niche_target_quota': 0, 'initial_search_basin_active_niche_family_cap_fraction': 0.15, 'initial_search_basin_active_niche_repair_interval': 25, 'initial_search_basin_active_niche_repair_fraction': 0.03, 'initial_search_basin_active_niche_rebalance': False, 'initial_search_basin_active_niche_rebalance_interval': 25, 'initial_search_basin_active_niche_rebalance_fraction': 0.05, 'initial_search_basin_active_niche_soft_fill': False, 'initial_search_basin_active_niche_elite_seed_count': 1, 'initial_search_basin_active_focus_rebalance': False, 'initial_search_basin_active_focus_start_epoch': 1, 'initial_search_basin_active_focus_initial_best_family': True, 'initial_search_basin_active_focus_family_count': 1, 'initial_search_basin_active_focus_population_fraction': 0.55, 'initial_search_basin_active_focus_min_quota': 0, 'initial_search_basin_active_focus_soft_fill': False, 'initial_search_basin_active_focus_elite_seed_count': 1, 'initial_search_basin_active_monitor_family_cap': 0, 'initial_search_basin_active_focus_stop_rel_improvement': 0.2, 'initial_search_basin_active_focus_ready_fraction': 0.7, 'initial_search_basin_active_focus_cooldown_epochs': 2, 'initial_search_basin_active_focus_stop_after_clears': 0, 'initial_search_basin_active_focus_stop_release': True, 'initial_search_basin_active_focus_stop_release_fraction': 1.0, 'initial_search_basin_active_focus_rotation': False, 'initial_search_basin_active_focus_rotation_history_epochs': 8, 'initial_search_basin_active_focus_rotation_similarity_threshold': 0.8, 'initial_search_basin_active_refresh_rebalance': True, 'initial_search_basin_active_refresh_max_family_fraction': 0.0, 'initial_search_basin_active_refresh_min_local_fraction': 0.2, 'initial_search_basin_active_plateau_lifecycle': True, 'initial_search_basin_active_plateau_track_active_niches': True, 'initial_search_basin_active_plateau_window': 2, 'initial_search_basin_active_plateau_min_rel_improvement': 0.005, 'initial_search_basin_active_plateau_local_quota_scale': 0.35, 'initial_search_basin_active_plateau_external_probe_rate': 0.15, 'initial_search_basin_active_plateau_retire_after': 4, 'initial_search_basin_active_plateau_cooldown_epochs': 2, 'initial_search_basin_semantic_diversity': True, 'initial_search_basin_semantic_max_per_topology': 1, 'initial_search_topology_diversity_mode': 'topology_family_similarity', 'initial_search_topology_descriptor_window_lmin': 2, 'initial_search_topology_descriptor_window_lmax': 4, 'initial_search_topology_descriptor_max_active_axes': 5, 'initial_search_topology_descriptor_audit_interval': 25, 'initial_search_topology_descriptor_weak_tau_low': 0.08, 'initial_search_topology_descriptor_weak_tau_high': 0.2, 'initial_search_topology_similarity_threshold': 0.9, 'initial_search_hybrid_polish_workers': 0, 'initial_search_hybrid_refresh': 0.3, 'initial_search_funnel_global_enable_polish': True, 'initial_search_funnel_global_micro_island_enabled': False, 'initial_search_funnel_global_micro_island_replace_polish': True, 'initial_search_funnel_global_micro_island_count': 0, 'initial_search_funnel_global_micro_island_popsize': 6.0, 'initial_search_funnel_global_micro_island_outer': 1, 'initial_search_funnel_global_micro_island_de_gen': 24, 'initial_search_funnel_global_micro_island_seed_scale': 0.65, 'initial_search_funnel_global_micro_island_polish_candidates': -1, 'initial_search_funnel_global_micro_island_polish_refill_max_extra': -1, 'initial_search_funnel_global_micro_island_survivors': 2, 'initial_search_funnel_global_micro_island_family_seed_limit': 24, 'initial_search_funnel_global_micro_island_refresh_archive_seed_limit': 24, 'initial_search_funnel_global_micro_island_early_stop_enabled': False, 'initial_search_funnel_global_micro_island_retire_on_early_stop': True, 'initial_search_funnel_global_micro_island_early_stop_min_epochs': 3, 'initial_search_funnel_global_micro_island_early_stop_window': 1, 'initial_search_funnel_global_micro_island_early_stop_min_rel_improve': 0.01, 'initial_search_funnel_global_micro_island_early_stop_min_abs_improve': 0.01, 'initial_search_funnel_global_popsize': 10.0, 'initial_search_funnel_global_explore_outer': 80, 'initial_search_funnel_outer_vignetting_strict_fraction': 1.0 / 3.0, 'initial_search_funnel_global_explore_de_use_cuda': True, 'initial_search_funnel_global_explore_full_restart_enabled': False, 'initial_search_funnel_global_cra_tighten_enabled': False, 'initial_search_funnel_global_cra_tighten_generations': 1200, 'initial_search_funnel_global_explore_full_restart_avoid_previous': True, 'initial_search_funnel_global_explore_full_restart_shared_seed_pool': True, 'initial_search_funnel_global_explore_full_restart_seed_pool_multiplier': 1.0, 'initial_search_funnel_global_explore_full_restart_archive_limit': 0, 'initial_search_funnel_global_explore_full_restart_avoid_penalty': 1000.0, 'initial_search_funnel_global_explore_full_restart_avoid_similarity_threshold': 0.9, 'initial_search_funnel_global_explore_full_restart_avoid_max_shape_diff': 3, 'initial_search_funnel_global_explore_full_restart_avoid_continuous_structure_enabled': True, 'initial_search_funnel_global_explore_full_restart_avoid_continuous_power_cosine_min': 0.99, 'initial_search_funnel_global_explore_full_restart_avoid_continuous_power_distance_max': 0.18, 'initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_cosine_min': 0.97, 'initial_search_funnel_global_explore_full_restart_avoid_continuous_curvature_distance_max': 0.25, 'initial_search_funnel_global_explore_full_restart_avoid_continuous_thickness_distance_max': 0.1, 'initial_search_funnel_global_explore_full_restart_avoid_continuous_gap_distance_max': 0.1, 'initial_search_funnel_global_explore_full_restart_avoid_continuous_significance_fraction': 0.08, 'initial_search_funnel_global_explore_full_restart_avoid_parameter_auxiliary_similarity_min': 0.9, 'initial_search_funnel_global_explore_full_restart_avoid_memory_policy': 'qualified_similarity', 'initial_search_funnel_global_explore_full_restart_avoid_high_quality_merit_ratio': 3.0, 'initial_search_funnel_global_explore_full_restart_avoid_memory_max_merit_ratio': 6.0, 'initial_search_funnel_global_adaptive_glass_tolerance_enabled': True, 'initial_search_funnel_global_adaptive_glass_tolerance_trigger_mode': 'warmup', 'initial_search_funnel_global_adaptive_glass_tolerance_focus_clear_target': 0, 'initial_search_funnel_global_adaptive_glass_tolerance_stop_when_ready': True, 'initial_search_funnel_global_adaptive_glass_tolerance_bridge_freeze_focus': True, 'initial_search_funnel_global_adaptive_glass_tolerance_warmup_epochs': 2, 'initial_search_funnel_global_adaptive_glass_tolerance_elite_fraction': 0.2, 'initial_search_funnel_global_adaptive_glass_tolerance_quantile': 0.0, 'initial_search_funnel_global_adaptive_glass_tolerance_margin': 0.5, 'initial_search_funnel_global_adaptive_glass_tolerance_floor': 0.05, 'initial_search_funnel_global_adaptive_glass_tolerance_smoothing': 0.75, 'initial_search_funnel_global_adaptive_glass_tolerance_decay': 0.75, 'initial_search_funnel_global_adaptive_glass_tolerance_early_stop_max': 0.5, 'initial_search_funnel_global_develop_outer': 15, 'initial_search_funnel_global_develop_mode': 'mixed', 'initial_search_funnel_global_develop_lpsr_enabled': True, 'initial_search_funnel_global_explore_de_gen': 1000, 'initial_search_funnel_global_develop_de_gen': 240, 'initial_search_funnel_global_near_trace_enabled': True, 'initial_search_funnel_global_near_trace_min_coverage': 0.95, 'initial_search_funnel_global_refresh_trace_feasible_gate': True, 'initial_search_funnel_global_basin_active_require_trace_feasible': False, 'initial_search_funnel_global_plateau_retire_evict': True, 'initial_search_funnel_global_plateau_retire_frontier_on_first_update': False, 'initial_search_funnel_global_plateau_retire_evict_parameter_only': True, 'initial_search_funnel_global_plateau_frontier_count': 1, 'initial_search_funnel_global_plateau_frontier_multiplier': 0.0, 'initial_search_funnel_global_plateau_retire_evict_fraction': 1.0, 'initial_search_funnel_global_plateau_retire_evict_max_fraction': 0.2, 'initial_search_funnel_global_plateau_retire_evict_min': 0, 'initial_search_funnel_global_plateau_retire_evict_parameter_distance': 0.15, 'initial_search_funnel_global_plateau_retire_defer_early_stop_epochs': 0, 'initial_search_funnel_global_plateau_retire_stop_after_clears': 0, 'initial_search_explore_develop_restart_count': 1, 'initial_search_explore_develop_seed_stride': 10000, 'initial_search_restart_seed_topology_novelty': False, 'initial_search_restart_seed_topology_scan_limit': 0, 'initial_search_topology_graft_enabled': True, 'initial_search_topology_graft_source_pool_size': 32, 'initial_search_topology_graft_count': 8, 'initial_search_topology_graft_donor_count': 6, 'initial_search_topology_graft_segments_per_donor': 2, 'initial_search_topology_graft_window_radius': 1, 'initial_search_topology_graft_blend': 0.7, 'initial_search_topology_graft_scale': 0.45, 'initial_search_topology_graft_popsize': 6.0, 'initial_search_topology_graft_outer': 2, 'initial_search_topology_graft_de_gen': 80, 'initial_search_topology_graft_enable_polish': True, 'initial_search_funnel_probe_cycle_enabled': False, 'initial_search_funnel_probe_cycle_count': 3, 'initial_search_funnel_probe_cycle_global_outer': 4, 'initial_search_funnel_probe_cycle_global_develop_outer': 1, 'initial_search_funnel_probe_cycle_global_de_gen': 80, 'initial_search_funnel_probe_cycle_probe_count': 24, 'initial_search_funnel_probe_cycle_source_pool_size': 64, 'initial_search_funnel_probe_cycle_archive_per_stage': 48, 'initial_search_funnel_probe_cycle_population_scan': 0, 'initial_search_funnel_global_early_stop_enabled': True, 'initial_search_funnel_global_early_stop_metric': 'topology_discovery', 'initial_search_funnel_global_early_stop_min_epochs': 4, 'initial_search_funnel_global_early_stop_window': 2, 'initial_search_funnel_global_early_stop_min_topologies': 20, 'initial_search_funnel_global_early_stop_min_rel_improve': 0.01, 'initial_search_funnel_global_early_stop_min_abs_improve': 0.01, 'initial_search_funnel_global_early_stop_include_initial_point': False, 'initial_search_funnel_global_early_stop_generation_window': 400, 'initial_search_funnel_global_early_stop_require_topology_plateau': False, 'initial_search_funnel_global_early_stop_best_merit_max': 200.0, 'initial_search_funnel_global_early_stop_qualified_topologies': True, 'initial_search_funnel_global_early_stop_qualified_topology_mode': 'merit', 'initial_search_funnel_global_early_stop_qualified_topology_epsilon': 0.02, 'initial_search_funnel_global_early_stop_qualified_topology_merit_ratio': 5.0, 'initial_search_funnel_global_early_stop_qualified_topology_merit_floor': 200.0, 'initial_search_funnel_global_early_stop_qualified_topology_merit_delta': 250.0, 'initial_search_funnel_global_early_stop_qualified_topology_merit_max': 0.0, 'initial_search_refresh_generation_mode': 'random', 'initial_search_refresh_trace_feasible_gate': True, 'initial_search_refresh_target_count': 0, 'initial_search_refresh_max_candidates': 0, 'initial_search_refresh_max_seconds': 0.0, 'initial_search_refresh_parent_count': 8, 'initial_search_refresh_curvature_rel': 0.2, 'initial_search_refresh_air_thickness_rel': 0.15, 'initial_search_refresh_glass_thickness_rel': 0.08, 'initial_search_refresh_curvature_abs': 0.05, 'initial_search_refresh_air_thickness_abs': 5.0, 'initial_search_refresh_glass_thickness_abs': 0.5, 'initial_search_refresh_perturb_material_variables': True, 'initial_search_refresh_seed_pool_fraction': 0.0, 'initial_search_refresh_seed_pool_perturbation': 0.0, 'initial_search_refresh_trace_archive_enabled': True, 'initial_search_refresh_pool_population_multiplier': 2.0, 'initial_search_refresh_trace_archive_capacity': 2200, 'initial_search_refresh_trace_archive_fraction': 1.0, 'initial_search_refresh_trace_archive_min_distance': 0.02, 'initial_search_refresh_adaptive_trigger_enabled': True, 'initial_search_refresh_adaptive_max_fraction': 0.3, 'initial_search_refresh_adaptive_min_acceptance_rate': 0.02, 'initial_search_refresh_adaptive_min_rel_improvement': 0.02, 'initial_search_hybrid_distance_metric': 'bound_normalized', 'initial_search_de_strategy': 'best1bin', 'initial_search_mutation_min': 0.5, 'initial_search_mutation_max': 1.0, 'initial_search_recombination': 0.7, 'initial_search_local_iterations': 50, 'initial_search_local_max_reject': 5, 'initial_search_local_method': 'trf', 'initial_search_local_bounds_mode': 'safety', 'initial_search_disable_polish': False, 'initial_search_batched_polish': False, 'initial_search_local_residual_mode': 'expanded', 'initial_search_local_lambda': 0.1, 'initial_search_local_fd': 0.0001, 'initial_search_local_min_step': 1e-08, 'initial_search_local_tol': 0.0001, 'initial_search_first_order_aiming': 'calibrated_paraxial', 'initial_search_first_order_aiming_iter': 20, 'initial_search_first_order_aiming_require_convergence': False, 'initial_search_first_order_aiming_continuation_steps': 8, 'initial_search_first_order_aiming_calibrated_correction_iterations': 0, 'initial_search_first_order_aiming_calibrated_sampling_mode': 'chief_edge_interpolation', 'initial_search_first_order_aiming_calibrated_fallback': 'none', 'initial_search_first_order_aiming_calibrated_chief_mode': 'abcd_budget_angle_bridge_fast', 'initial_search_first_order_aiming_calibrated_budget_chief_iter': 8, 'initial_search_first_order_aiming_calibrated_budget_chief_solver': 'hybrid', 'initial_search_first_order_aiming_calibrated_budget_chief_acceptance': 'residual', 'initial_search_first_order_aiming_calibrated_budget_chief_stop_fraction': 0.05, 'initial_search_first_order_aiming_calibrated_budget_chief_residual_stop_fraction': 1e-06, 'initial_search_first_order_aiming_calibrated_budget_chief_min_a': 1e-06, 'initial_search_first_order_aiming_calibrated_budget_chief_field_angle': 75.0, 'initial_search_first_order_aiming_calibrated_budget_chief_ignore_aperture_clip': True, 'initial_search_first_order_aiming_calibrated_budget_chief_skip_abcd_angle': 60.0, 'initial_search_first_order_aiming_calibrated_edge_calibration_enabled': True, 'initial_search_first_order_aiming_calibrated_edge_calibration_iterations': 8, 'initial_search_first_order_aiming_calibrated_edge_calibration_solver': 'secant', 'initial_search_first_order_aiming_calibrated_edge_calibration_weight_power': 2.0, 'initial_search_first_order_aiming_calibrated_edge_calibration_residual_stop_fraction': 1e-06, 'initial_search_first_order_aiming_three_step_sampling_mode': 'chief_edge_interpolation', 'initial_search_first_order_aiming_three_step_chief_rays': 33, 'initial_search_first_order_aiming_three_step_max_angle': 89.0, 'initial_search_first_order_aiming_three_step_edge_passes': 2, 'initial_search_first_order_aiming_three_step_meridional_iter': 8, 'initial_search_first_order_aiming_three_step_sagittal_iter': 8, 'initial_search_first_order_aiming_three_step_edge_solver': 'secant', 'initial_search_first_order_aiming_three_step_edge_residual_stop_fraction': 1e-06, 'initial_search_first_order_aiming_three_step_edge_radius_fraction': 1.0, 'initial_search_first_order_aiming_three_step_accept_stop_fraction': 0.05, 'initial_search_first_order_aiming_three_step_correction_iterations': 0, 'initial_search_first_order_aiming_three_step_sample_stop_residual_debug': False, 'initial_search_first_order_aiming_three_step_fallback': 'none', 'initial_search_trace_policy': 'soft_coverage', 'initial_search_min_field_coverage': 1.0, 'initial_search_min_system_coverage': 1.0, 'initial_search_min_coverage_for_image_merit': 0.5, 'initial_search_trace_failure_weight': 5.0, 'initial_search_coverage_failure_weight': 800.0, 'initial_search_aim_failure_weight': 2.0, 'initial_search_backward_failure_weight': 10.0, 'initial_search_require_chief_ray': True, 'initial_search_require_off_axis_field_coverage': True, 'initial_search_hard_trace_gate_weight': 20000.0, 'initial_search_trace_progress_weight': 1000.0, 'initial_search_chief_progress_weight': 2000.0, 'initial_search_trace_priority_weight': 150.0, 'initial_search_first_order_weight_scale': 1.0, 'initial_search_spot_weight_scale': 1.0, 'initial_search_first_order_edge_weight': 0.0, 'initial_search_first_order_min_edge': 1.0, 'initial_search_glass_center_thickness_lower': 0.5, 'initial_search_glass_center_thickness_upper': 10.0, 'initial_search_air_center_thickness_lower': 0.1, 'initial_search_air_center_thickness_upper': 100.0, 'initial_search_glass_edge_thickness_lower': 0.5, 'initial_search_glass_edge_thickness_upper': 10.0, 'initial_search_air_edge_thickness_lower': 0.1, 'initial_search_air_edge_thickness_upper': 100.0, 'initial_search_atc_lower': 0.1, 'initial_search_atc_upper': 0.0, 'initial_search_ate_lower': 0.1, 'initial_search_ate_upper': 0.0, 'initial_search_bl0_lower': 0.0, 'initial_search_bl0_upper': 0.9, 'initial_search_trace_rms_upper': 0.0, 'initial_search_trace_rays': 3, 'initial_search_trace_arms': 6, 'initial_search_trace_distribution': 'gaussian_ring_arm', 'initial_search_include_stop_air_thickness': True, 'initial_search_stop_air_thickness_min': 0.1, 'initial_search_stop_air_thickness_max': 27.0, 'initial_search_funnel_material_mode': 'discrete', 'initial_search_funnel_develop_material_mode': 'discrete', 'initial_search_funnel_probe_material_mode': 'fixed', 'initial_search_funnel_focus_material_mode': 'fixed', 'initial_search_funnel_refine_material_mode': 'fixed', 'initial_search_funnel_glass_candidates': 5, 'initial_search_funnel_probe_popsize': 5.0, 'initial_search_funnel_probe_population': 160, 'initial_search_funnel_probe_outer': 2, 'initial_search_funnel_probe_de_gen': 50, 'initial_search_funnel_probe_scale': 0.65, 'initial_search_funnel_probe_max_per_coarse_topology': 1, 'initial_search_funnel_probe_source_pool_size': 64, 'initial_search_funnel_probe_parent_use_cached_topology': True, 'initial_search_funnel_probe_parent_selection_priority': 'topology_merit_potential', 'initial_search_funnel_population_candidate_selection': 'topology_merit_potential', 'initial_search_funnel_population_candidate_scan': 0, 'initial_search_funnel_probe_refill_enabled': True, 'initial_search_funnel_probe_refill_target_coarse': 4, 'initial_search_funnel_probe_final_target_count': 0, 'initial_search_funnel_probe_refill_max_extra': 8, 'initial_search_funnel_probe_refill_scan_population': 128, 'initial_search_funnel_include_explore_tail_in_probe': True, 'initial_search_funnel_explore_tail_per_key': 1, 'initial_search_funnel_include_explore_archive_in_develop': True, 'initial_search_funnel_explore_archive_develop_limit': 0, 'initial_search_funnel_final_common_rescore': True, 'initial_search_funnel_final_rescore_stage': 'refine', 'initial_search_funnel_same_key_min_distance_probe': 0.12, 'initial_search_funnel_same_key_min_distance_focus': 0.15, 'initial_search_funnel_same_key_min_distance_refine': 0.18, 'initial_search_funnel_probe_semantic_diversity': True, 'initial_search_funnel_focus_semantic_diversity': True, 'initial_search_funnel_refine_scout_semantic_diversity': True, 'initial_search_funnel_refine_semantic_diversity': False, 'initial_search_funnel_probe_active_diversity': True, 'initial_search_funnel_focus_active_diversity': True, 'initial_search_funnel_refine_scout_active_diversity': True, 'initial_search_funnel_refine_active_diversity': False, 'initial_search_funnel_promote_count': 4, 'initial_search_funnel_focus_popsize': 10.0, 'initial_search_funnel_focus_population': 200, 'initial_search_funnel_focus_outer': 4, 'initial_search_funnel_focus_de_gen': 120, 'initial_search_funnel_focus_trace_rays': 6, 'initial_search_funnel_focus_trace_arms': 8, 'initial_search_funnel_focus_field_count': 5, 'initial_search_funnel_focus_scale': 0.75, 'initial_search_funnel_focus_post_dedupe_enabled': True, 'initial_search_funnel_focus_refill_max_extra': 8, 'initial_search_funnel_focus_duplicate_topology_distance': 10.0, 'initial_search_funnel_focus_pre_dedupe_enabled': True, 'initial_search_funnel_focus_pre_dedupe_topology_distance': 10.0, 'initial_search_funnel_focus_polish_candidates': -1, 'initial_search_funnel_focus_polish_refill_max_extra': -1, 'initial_search_funnel_asphere_enabled': False, 'initial_search_funnel_asphere_field_sampling_enabled': True, 'initial_search_funnel_focus_asphere_field_count': 5, 'initial_search_funnel_refine_asphere_field_count': 7, 'initial_search_funnel_refine_asphere_max_order': 10, 'initial_search_funnel_stage_seed_trace_gate': True, 'initial_search_funnel_stage_seed_gate_candidate_factor': 8, 'initial_search_funnel_stage_seed_gate_batch_factor': 1, 'initial_search_funnel_stage_seed_gate_final_recheck': True, 'initial_search_funnel_late_near_trace_enabled': True, 'initial_search_funnel_late_near_trace_min_coverage': 0.92, 'initial_search_funnel_late_preserve_anchor': True, 'initial_search_funnel_late_refresh_trace_feasible_gate': False, 'initial_search_funnel_late_basin_active_require_trace_feasible': False, 'initial_search_funnel_refine_include_probe_neighbors': True, 'initial_search_funnel_refine_probe_neighbor_count': 16, 'initial_search_funnel_refine_probe_neighbor_max_topology_distance': 12.0, 'initial_search_funnel_refine_glass_hammer_enabled': True, 'initial_search_funnel_refine_glass_hammer_neighbor_count': 5, 'initial_search_funnel_refine_glass_hammer_max_passes': 1, 'initial_search_funnel_refine_glass_hammer_local_iterations': 20, 'initial_search_funnel_refine_glass_hammer_timeout': 120, 'initial_search_funnel_refine_glass_hammer_min_improvement': 0.001, 'initial_search_funnel_refine_glass_hammer_accept_mode': 'best', 'initial_search_funnel_refine_count': 4, 'initial_search_funnel_refine_polish_candidates': -1, 'initial_search_funnel_refine_polish_refill_max_extra': -1, 'initial_search_funnel_refine_promotion_mode': 'merit_scout_potential', 'initial_search_funnel_refine_merit_slots': 0, 'initial_search_funnel_refine_scout_merit_slots': 1, 'initial_search_funnel_refine_potential_slots': 1, 'initial_search_funnel_refine_scout_enabled': True, 'initial_search_funnel_refine_scout_popsize': 5.0, 'initial_search_funnel_refine_scout_population': 160, 'initial_search_funnel_refine_scout_outer': 2, 'initial_search_funnel_refine_scout_de_gen': 80, 'initial_search_funnel_refine_scout_local_iterations': 10, 'initial_search_funnel_refine_popsize': 10.0, 'initial_search_funnel_refine_population': 320, 'initial_search_funnel_refine_outer': 80, 'initial_search_funnel_refine_de_gen': 160, 'initial_search_funnel_refine_trace_rays': 6, 'initial_search_funnel_refine_trace_arms': 8, 'initial_search_funnel_refine_field_count': 5, 'initial_search_funnel_refine_early_stop_enabled': True, 'initial_search_funnel_refine_early_stop_min_epochs': 4, 'initial_search_funnel_refine_early_stop_window': 3, 'initial_search_funnel_refine_early_stop_min_rel_improve': 0.01, 'initial_search_funnel_refine_early_stop_min_abs_improve': 0.01, 'initial_search_funnel_refine_lpsr_horizon_epochs': 24, 'initial_search_funnel_refine_early_stop_min_lpsr_progress': 0.35, 'initial_search_funnel_refine_early_stop_tail_epochs': 0, 'initial_search_funnel_local_basin_diversity_enabled': False, 'initial_search_funnel_local_basin_max': 8, 'initial_search_funnel_local_basin_active_count': 4, 'initial_search_funnel_local_refresh_parent_count': 4, 'initial_search_funnel_local_refresh_curvature_rel': 0.16, 'initial_search_funnel_local_refresh_air_thickness_rel': 0.1, 'initial_search_funnel_local_refresh_glass_thickness_rel': 0.06, 'initial_search_funnel_local_refresh_curvature_abs': 0.0025, 'initial_search_funnel_local_refresh_air_thickness_abs': 1.5, 'initial_search_funnel_local_refresh_glass_thickness_abs': 0.25, 'initial_search_funnel_refine_refresh_curvature_rel': 0.12, 'initial_search_funnel_refine_refresh_air_thickness_rel': 0.08, 'initial_search_funnel_refine_refresh_glass_thickness_rel': 0.05, 'initial_search_funnel_refine_refresh_curvature_abs': 0.002, 'initial_search_funnel_refine_refresh_air_thickness_abs': 1.0, 'initial_search_funnel_refine_refresh_glass_thickness_abs': 0.2, 'initial_search_seed_trace_strategy': 'anchor_sparse_refill', 'initial_search_baseline_custom_ttl_range_enabled': False, 'initial_search_baseline_ttl_lower_fraction': 0.7, 'initial_search_baseline_ttl_upper_fraction': 1.5, 'initial_search_baseline_bfl_lower_factor': 0.9, 'initial_search_baseline_bfl_ttl_upper_fraction': 0.5, 'initial_search_baseline_glass_beta_alpha': 2.0, 'initial_search_baseline_glass_beta_beta': 4.0, 'initial_search_baseline_air_dirichlet_alpha': 2.0, 'initial_search_baseline_conic_coverage_fraction': 0.08, 'initial_search_baseline_conic_abs_max': 100.0, 'initial_search_baseline_asphere_coverage_fraction': 0.02, 'initial_search_baseline_asphere_bound_fraction': 1.0, 'initial_search_baseline_max_active_shape_per_row': 4, 'initial_search_seed_cap_scout_enabled': True, 'initial_search_seed_cap_scout_list': '0.01,0.02,0.04', 'initial_search_seed_cap_scout_candidates': 6144, 'initial_search_anchor_target_count': 16, 'initial_search_anchor_min_count': 8, 'initial_search_anchor_max_candidates': 65536, 'initial_search_anchor_batch_size': 4096, 'initial_search_anchor_cloud_factor': 8, 'initial_search_anchor_cloud_profiles': 'micro,small,medium', 'initial_search_sparse_refill_enabled': True, 'initial_search_sparse_refill_skip_when_sufficient': True, 'initial_search_sparse_refill_pool_factor': 8, 'initial_search_sparse_refill_max_candidates': 65536, 'initial_search_sparse_refill_batch_size': 4096, 'initial_search_sparse_refill_min_profile_rounds': 0, 'initial_search_diversity_min_range_frac': 0.001, 'initial_search_diversity_target_range_frac': 0.01, 'initial_search_diversity_min_active_fraction': 0.08, 'initial_search_diversity_min_curvature_active_fraction': 0.08, 'initial_search_final_strict_recheck': True, 'fine_polish_spot_scale': 0.01, 'fine_polish_wavefront_scale': 0.5, 'fine_polish_rays': 5, 'fine_polish_arms': 12, 'fine_polish_iterations': 80, 'fine_polish_tol': 1e-06, 'fine_polish_max_reject': 8, 'local_glass_proximity_enabled': True, 'local_glass_proximity_weight': 0.5, 'local_glass_proximity_tolerance': 1.5, 'local_glass_proximity_smooth_temperature': 0.25, 'local_glass_proximity_candidate_count': 256, 'local_glass_proximity_nd_scale': state.GLASS_DISTANCE_ND_SCALE, 'local_glass_proximity_vd_scale': state.GLASS_DISTANCE_VD_SCALE, 'local_glass_proximity_vd_metric': state.GLASS_DISTANCE_VD_METRIC, 'local_glass_proximity_vd_relative_scale': state.GLASS_DISTANCE_VD_RELATIVE_SCALE, 'local_iterations': 40, 'local_max_reject': 4, 'local_method': 'trf', 'local_bounds_mode': 'safety', 'batched_polish': False, 'local_residual_mode': 'expanded', 'local_lambda': 0.01, 'local_fd': 0.0001, 'local_min_step': 1e-08, 'local_tol': 0.0001}}
    _lensde_configuration.sync_unified_ray_aiming_options(project['options'])
    return project
