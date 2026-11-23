"""Extracted paper engine: tracing."""
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
from . import provenance as _lensde_provenance
from . import ordinary_de as _ordinary_de

def run_cpp(project: dict, run_de: bool) -> dict:
    _lensde_configuration.sync_unified_ray_aiming_options(project.setdefault('options', {}))
    stamp = time.strftime('%Y%m%d_%H%M%S')
    run_dir = subprocess.run_directory(state.WORK_DIR / f'run_{stamp}_{int(time.time() * 1000) % 1000:03d}')
    run_dir.mkdir(parents=True, exist_ok=True)
    if run_de:
        _lensde_geometry.sync_variable_initials(project)
    system_path = run_dir / 'input_system.json'
    variables_path = run_dir / 'variables.json'
    glass_search_path = run_dir / 'glass_search.json'
    merit_path = run_dir / 'merit.json'
    options = project.get('options', {})
    mode = str(options.get('optimizer_mode', 'de')).strip().lower().replace('-', '_') if run_de else ''
    run_system = _lensde_configuration.system_with_ray_aiming(project['system'], options)
    _lensde_provenance.write_json(system_path, run_system)
    run_variables = project.get('variables', [])
    if run_de and mode not in {'initial_structure_search', 'first_order_seeded', 'initial-search'}:
        run_variables = _lensde_commands.apply_de_bound_overrides(project['system'], run_variables, options)
    if run_de:
        try:
            material_wavelength_audit = _lensde_materials.assert_system_material_wavelength_coverage(run_system, run_variables)
        except ValueError as exc:
            return _lensde_materials.collect_run_result(run_dir, {}, 10, '', str(exc))
        _lensde_provenance.write_json(run_dir / 'material_wavelength_audit.json', material_wavelength_audit)
    glass_project = {**project, 'system': run_system}
    glass_search_config = _lensde_materials.prepare_glass_search_config(glass_project, run_variables) if run_de else {'enabled': False, 'groups': []}
    _lensde_provenance.write_json(variables_path, {'variables': run_variables})
    if glass_search_config.get('enabled'):
        _lensde_provenance.write_json(glass_search_path, glass_search_config)
    run_merit = _lensde_geometry.auto_merit_config(project)
    if run_de and _lensde_geometry.is_fine_polish_mode(mode):
        run_merit = _lensde_geometry.fine_polish_merit_config(run_merit, options)
    if run_de and _lensde_materials.optimizer_mode_uses_local_glass_proximity(mode, options):
        run_merit = _lensde_materials.apply_local_glass_proximity_merit(run_merit, run_system, run_variables, options)
    _lensde_provenance.write_json(merit_path, run_merit)
    use_cuda = bool(options.get('use_cuda', False))
    exe = _lensde_provenance.exe_path(use_cuda)
    args = [str(exe), '--system', str(system_path), '--merit', str(merit_path), '--aiming', _lensde_configuration.canonical_aiming_mode(options.get('aiming', 'robust'), 'robust'), '--aiming-iter', str(int(options.get('aiming_iter', 20))), '--rays', str(int(run_merit.get('num_rays', 3))), '--arms', str(int(run_merit.get('pupil_arms', 6))), '--distribution', str(run_merit.get('pupil_distribution', 'gaussian_ring_arm')), '--sampling-seed', str(int(run_merit.get('sampling_seed', 1))), '--spot-rays', str(int(options.get('spot_rays', 41))), '--spot-arms', str(int(options.get('spot_arms', 64))), '--spot-distribution', str(options.get('spot_distribution', 'uniform')), '--spot-sampling-seed', str(int(options.get('spot_sampling_seed', run_merit.get('sampling_seed', 1)))), '--out-dir', str(run_dir)]
    _lensde_materials.append_aiming_policy_args(args, options)
    optimizer_internal_scaling = _lensde_geometry.effective_optimizer_internal_scaling(options, run_variables)
    if options.get('clear_aperture_update', True):
        args.append('--clear-aperture-update')
    if options.get('sync_stop_aperture', True):
        args.append('--sync-stop-aperture')
    if options.get('quick_focus', False):
        args.append('--quick-focus')
    if run_de:
        if mode in {'de+local', 'de_local_polish'}:
            mode = 'de_local'
        if _lensde_geometry.is_fine_polish_mode(mode):
            mode = 'fine_polish'
        if mode == 'de' and options.get('polish', False):
            mode = 'de_local'
        args.extend(['--variables', str(variables_path)])
        _lensde_materials.append_glass_search_args(args, glass_search_path, glass_search_config, options)
        if mode == 'hammer':
            _lensde_materials.append_hammer_args(args, options, run_dir)
        elif mode in {'adaptive', 'adaptive_hybrid', 'adaptive_hybrid_de_dls'}:
            args.extend(['--adaptive-hybrid-de-dls', '--popsize', str(int(options.get('adaptive_initial_popsize_multiplier', options.get('population', 15)))), '--gen', str(int(options.get('generations', 24))), '--hybrid-outer', str(int(options.get('hybrid_outer', 6))), '--hybrid-de-gen', str(int(options.get('hybrid_de_gen', max(1, int(options.get('generations', 24)) // 6)))), '--hybrid-parents', str(int(options.get('hybrid_parents', 4))), '--polish-candidates', str(int(options.get('adaptive_polish_candidates', 8))), '--polish-refill-max-extra', str(int(options.get('adaptive_polish_refill_max_extra', options.get('adaptive_polish_candidates', 8)) or 0)), '--hybrid-polish-workers', str(int(options.get('hybrid_polish_workers', 0))), '--optimizer-internal-scaling', 'bound_normalized', '--de-variant', str(options.get('adaptive_de_variant', 'jade_pbest_archive_lpsr_feas')), '--constraint-handling', str(options.get('adaptive_constraint_handling', 'feasibility_rules')), '--adaptive-search-purpose', 'optimize', '--first-order-epsilon', str(float(options.get('adaptive_first_order_epsilon', 1e-05))), '--pbest-fraction', str(float(options.get('adaptive_pbest_fraction', 0.1))), '--archive-size-ratio', str(float(options.get('adaptive_archive_size_ratio', 1.0))), '--adaptive-initial-popsize-multiplier', str(int(options.get('adaptive_initial_popsize_multiplier', options.get('population', 15)))), '--adaptive-min-popsize-multiplier', str(int(options.get('adaptive_min_popsize_multiplier', 4))), '--adaptive-min-population', str(int(options.get('adaptive_min_population', 20))), '--hybrid-refresh', str(float(options.get('hybrid_refresh', 0.05))), '--basin-max', str(int(options.get('adaptive_basin_max', 8))), '--basin-radius', str(float(options.get('adaptive_basin_radius', 0.05))), '--basin-distance-metric', str(options.get('adaptive_basin_distance_metric', 'structure_bound_normalized')), '--basin-air-thickness-distance-cap', str(float(options.get('adaptive_basin_air_thickness_distance_cap', 25.0))), '--basin-output-count', str(int(options.get('adaptive_basin_output_count', 8))), '--basin-active-count', str(int(options.get('adaptive_basin_active_count', 4))), '--basin-protected-fraction', str(float(options.get('adaptive_basin_protected_fraction', 0.15))), '--basin-semantic-max-per-topology', str(int(options.get('adaptive_basin_semantic_max_per_topology', 2))), '--topology-diversity-mode', str(options.get('adaptive_topology_diversity_mode', 'legacy')), '--topology-descriptor-window-lmin', str(int(options.get('adaptive_topology_descriptor_window_lmin', 2))), '--topology-descriptor-window-lmax', str(int(options.get('adaptive_topology_descriptor_window_lmax', 4))), '--topology-descriptor-max-active-axes', str(int(options.get('adaptive_topology_descriptor_max_active_axes', 5))), '--topology-similarity-threshold', str(float(options.get('adaptive_topology_similarity_threshold', 0.9))), '--topology-descriptor-audit-interval', str(int(options.get('adaptive_topology_descriptor_audit_interval', 25))), '--topology-descriptor-weak-tau-low', str(float(options.get('adaptive_topology_descriptor_weak_tau_low', 0.08))), '--topology-descriptor-weak-tau-high', str(float(options.get('adaptive_topology_descriptor_weak_tau_high', 0.2))), '--seed', str(int(options.get('seed', 20260515))), '--hybrid-local-iter', str(int(options.get('local_iterations', 40))), '--local-method', str(options.get('local_method', 'trf')), '--local-bounds-mode', str(options.get('local_bounds_mode', 'safety')), '--local-residual-mode', str(options.get('local_residual_mode', 'expanded')), '--local-lambda', str(float(options.get('local_lambda', 0.01))), '--local-fd', str(float(options.get('local_fd', 0.0001))), '--local-min-step', str(float(options.get('local_min_step', 1e-08))), '--local-tol', str(float(options.get('local_tol', 0.0001))), '--local-max-reject', str(int(options.get('local_max_reject', 4)))])
            if bool(options.get('adaptive_track_population_diversity', False)):
                args.append('--track-population-diversity')
            _lensde_commands.append_pbest_guidance_args(args, options, 'adaptive_', default_scope='legacy', default_global_fallback=True, default_inject_best=True)
            _lensde_commands.append_de_mating_args(args, options, 'adaptive_')
            _lensde_commands.append_initial_generation_args(args, options)
            _lensde_commands.append_refresh_generation_args(args, options)
            if bool(options.get('adaptive_basin_active_diversity', False)):
                args.append('--basin-active-diversity')
            else:
                args.append('--basin-no-active-diversity')
            _lensde_commands.append_basin_active_quality_args(args, options, 'adaptive_')
            if bool(options.get('adaptive_basin_balanced_refresh', True)):
                args.append('--basin-balanced-refresh')
            else:
                args.append('--basin-no-balanced-refresh')
            if bool(options.get('adaptive_basin_semantic_diversity', True)):
                args.append('--basin-semantic-diversity')
            else:
                args.append('--basin-no-semantic-diversity')
            if bool(options.get('adaptive_soft_first_order', True)):
                args.append('--adaptive-soft-first-order')
            else:
                args.append('--adaptive-hard-first-order')
            if bool(options.get('batched_polish', False)):
                args.append('--batched-polish')
        elif mode in {'hybrid', 'hybrid_de_dls'}:
            args.extend(['--hybrid-de-dls', '--popsize', str(int(options.get('population', 15))), '--gen', str(int(options.get('generations', 24))), '--hybrid-outer', str(int(options.get('hybrid_outer', 6))), '--hybrid-de-gen', str(int(options.get('hybrid_de_gen', max(1, int(options.get('generations', 24)) // 6)))), '--hybrid-parents', str(int(options.get('hybrid_parents', 4))), '--hybrid-polish-workers', str(int(options.get('hybrid_polish_workers', 0))), '--hybrid-refresh', str(float(options.get('hybrid_refresh', 0.1))), '--hybrid-distance-metric', str(options.get('hybrid_distance_metric', 'bound_normalized')), '--optimizer-internal-scaling', optimizer_internal_scaling, '--de-strategy', str(options.get('de_strategy', 'best1bin')), '--mutation-min', str(float(options.get('mutation_min', 0.5))), '--mutation-max', str(float(options.get('mutation_max', 1.0))), '--recombination', str(float(options.get('recombination', 0.7))), '--seed', str(int(options.get('seed', 20260515))), '--hybrid-local-iter', str(int(options.get('local_iterations', 40))), '--local-method', str(options.get('local_method', 'trf')), '--local-bounds-mode', str(options.get('local_bounds_mode', 'safety')), '--local-residual-mode', str(options.get('local_residual_mode', 'expanded')), '--local-lambda', str(float(options.get('local_lambda', 0.01))), '--local-fd', str(float(options.get('local_fd', 0.0001))), '--local-min-step', str(float(options.get('local_min_step', 1e-08))), '--local-tol', str(float(options.get('local_tol', 0.0001))), '--local-max-reject', str(int(options.get('local_max_reject', 4)))])
            _lensde_commands.append_initial_generation_args(args, options)
            _lensde_commands.append_refresh_generation_args(args, options)
            if bool(options.get('batched_polish', False)):
                args.append('--batched-polish')
        elif mode == 'de' and options.get('de_algorithm') == 'jade':
            _ordinary_de.append_jade_args(args, options, optimizer_internal_scaling)
            _lensde_commands.append_initial_generation_args(args, options)
        elif mode in {'local', 'fine_polish'}:
            args.append('--local')
        else:
            args.extend(['--de', '--popsize', str(int(options.get('population', 15))), '--gen', str(int(options.get('generations', 24))), '--de-strategy', str(options.get('de_strategy', 'best1bin')), '--mutation-min', str(float(options.get('mutation_min', 0.5))), '--mutation-max', str(float(options.get('mutation_max', 1.0))), '--recombination', str(float(options.get('recombination', 0.7))), '--de-tol', str(float(options.get('de_tol', 0.01))), '--de-atol', str(float(options.get('de_atol', 0.0))), '--seed', str(int(options.get('seed', 20260515))), '--optimizer-internal-scaling', optimizer_internal_scaling])
            _lensde_commands.append_initial_generation_args(args, options)
            progress_every = int(options.get('progress_every', 0) or 0)
            if progress_every > 0:
                args.extend(['--progress-every', str(progress_every)])
        if mode in {'local', 'de_local', 'fine_polish'}:
            local_iterations = int(options.get('local_iterations', 40))
            local_tol = float(options.get('local_tol', 0.0001))
            local_max_reject = int(options.get('local_max_reject', 4))
            if mode == 'fine_polish':
                local_iterations = int(max(1.0, _lensde_configuration.finite_float(options.get('fine_polish_iterations'), float(max(80, local_iterations)))))
                local_tol = max(1e-12, _lensde_configuration.finite_float(options.get('fine_polish_tol'), min(local_tol, 1e-06)))
                local_max_reject = int(max(1.0, _lensde_configuration.finite_float(options.get('fine_polish_max_reject'), 8.0)))
            args.extend(['--local-iter', str(local_iterations), '--local-method', str(options.get('local_method', 'trf')), '--local-bounds-mode', str(options.get('local_bounds_mode', 'safety')), '--local-residual-mode', str(options.get('local_residual_mode', 'expanded')), '--local-lambda', str(float(options.get('local_lambda', 0.01))), '--local-fd', str(float(options.get('local_fd', 0.0001))), '--optimizer-internal-scaling', optimizer_internal_scaling, '--local-min-step', str(float(options.get('local_min_step', 1e-08))), '--local-tol', str(local_tol), '--local-max-reject', str(local_max_reject)])
        if mode == 'de_local':
            args.append('--polish')
        if mode == 'de':
            _ordinary_de.append_population_args(args, options, run_system, run_variables, run_dir)
    _lensde_commands.append_vignetting_audit_args(args, run_merit)
    if not use_cuda:
        args.append('--cpu')
    timeout_seconds = _lensde_materials.run_timeout_seconds(project)
    run_meta = {'timeout_seconds': timeout_seconds, 'timeout_source': 'project.options.cpp_timeout', 'use_cuda': use_cuda, 'executable': str(exe), 'command': args, 'options': options, 'de_bound_overrides_applied': bool(options.get('de_override_bounds', False)) and run_de, 'glass_search': {'enabled': bool(glass_search_config.get('enabled')), 'groups': len(glass_search_config.get('groups', [])), 'config': str(glass_search_path) if glass_search_config.get('enabled') else ''}}
    _lensde_provenance.write_json(run_dir / 'run_meta.json', run_meta)
    (run_dir / 'command.txt').write_text(' '.join(args), encoding='utf-8')
    run_banner = f"[GUI] C++ timeout: {(timeout_seconds if timeout_seconds > 0 else 'disabled')} seconds\n[GUI] Run dir: {run_dir}\n"
    try:
        completed = subprocess.run(args, cwd=str(state.CPP_ROOT), text=True, encoding='utf-8', errors='replace', capture_output=True, timeout=timeout_seconds if timeout_seconds > 0 else None)
        stages = [{'name': mode or ('de' if run_de else 'score'), 'command': args, 'returncode': completed.returncode, 'timed_out': False}]
        _lensde_provenance.write_repro_manifest(run_dir, project, options, stages, exe, strategy='cpp_run', optimizer=mode or ('de' if run_de else 'score'), run_meta=run_meta, returncode=completed.returncode, timed_out=False)
        return _lensde_materials.collect_run_result(run_dir, run_merit, completed.returncode, run_banner + completed.stdout, completed.stderr, timeout_seconds=timeout_seconds if timeout_seconds > 0 else None)
    except subprocess.TimeoutExpired as exc:
        checkpoint_restored = _lensde_materials.promote_checkpoint(run_dir)
        stderr = _lensde_materials.process_text(exc.stderr)
        timeout_message = f'C++ process timed out after {timeout_seconds} seconds.'
        if checkpoint_restored:
            timeout_message += ' Restored last checkpoint system/score.'
        else:
            timeout_message += ' No checkpoint was available yet.'
        stderr = (stderr + '\n' + timeout_message).strip()
        stages = [{'name': mode or ('de' if run_de else 'score'), 'command': args, 'returncode': -9, 'timed_out': True}]
        _lensde_provenance.write_repro_manifest(run_dir, project, options, stages, exe, strategy='cpp_run', optimizer=mode or ('de' if run_de else 'score'), run_meta=run_meta, returncode=-9, timed_out=True)
        return _lensde_materials.collect_run_result(run_dir, run_merit, -9, run_banner + _lensde_materials.process_text(exc.stdout), stderr, timed_out=True, timeout_seconds=timeout_seconds, checkpoint_restored=checkpoint_restored)
