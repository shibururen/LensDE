"""Extracted paper engine: commands."""
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

def append_initial_generation_args(args: list[str], options: dict) -> None:
    mode = _lensde_configuration.canonical_population_generation_mode(options.get('initial_generation_mode', 'random'), 'random')
    args.extend(['--initial-generation-mode', mode])
    if mode != 'physics_constrained':
        return
    for key, flag, fallback in [('initial_physics_target_ttl', '--initial-physics-target-ttl', 0.0), ('initial_physics_target_efl', '--initial-physics-target-efl', 0.0), ('initial_physics_target_bfl', '--initial-physics-target-bfl', 0.0), ('initial_physics_ttl_min_fraction', '--initial-physics-ttl-min-fraction', 0.75), ('initial_physics_ttl_max_fraction', '--initial-physics-ttl-max-fraction', 1.0), ('initial_physics_power_jitter', '--initial-physics-power-jitter', 0.15), ('initial_physics_sa_temperature', '--initial-physics-sa-temperature', 0.05)]:
        args.extend([flag, str(max(0.0, _lensde_configuration.finite_float(options.get(key), fallback)))])
    args.extend(['--initial-physics-sa-iterations', str(max(0, int(_lensde_configuration.finite_float(options.get('initial_physics_sa_iterations'), 48))))])


def append_refresh_generation_args(args: list[str], options: dict, prefix: str='', seed_pool_path: Path | None=None) -> None:

    def opt(name: str, fallback):
        return options.get(f'{prefix}refresh_{name}', options.get(f'refresh_{name}', fallback))
    mode = _lensde_configuration.canonical_population_generation_mode(opt('generation_mode', 'random'), 'random')
    args.extend(['--refresh-generation-mode', mode])
    if bool(opt('trace_feasible_gate', True)):
        args.append('--refresh-trace-feasible-gate')
    else:
        args.append('--refresh-no-trace-feasible-gate')
    if bool(opt('trace_archive_enabled', True)):
        args.append('--refresh-trace-archive')
    else:
        args.append('--refresh-no-trace-archive')
    if bool(opt('adaptive_trigger_enabled', True)):
        args.append('--refresh-adaptive-trigger')
    else:
        args.append('--refresh-no-adaptive-trigger')
    int_options = [('target_count', '--refresh-target-count', 0, 0), ('max_candidates', '--refresh-max-candidates', 0, 0), ('parent_count', '--refresh-parent-count', 8, 1), ('trace_archive_capacity', '--refresh-trace-archive-capacity', 4096, 0)]
    for name, flag, fallback, minimum in int_options:
        value = max(minimum, int(_lensde_configuration.finite_float(opt(name, fallback), fallback)))
        args.extend([flag, str(value)])
    adaptive_max_fraction_default = 0.5 if prefix == 'initial_search_' else 0.1
    float_options = [('max_seconds', '--refresh-max-seconds', 0.0), ('curvature_rel', '--refresh-curvature-rel', 0.2), ('air_thickness_rel', '--refresh-air-thickness-rel', 0.15), ('glass_thickness_rel', '--refresh-glass-thickness-rel', 0.08), ('curvature_abs', '--refresh-curvature-abs', 0.05), ('air_thickness_abs', '--refresh-air-thickness-abs', 5.0), ('glass_thickness_abs', '--refresh-glass-thickness-abs', 0.5), ('seed_pool_fraction', '--refresh-seed-pool-fraction', 0.0), ('seed_pool_perturbation', '--refresh-seed-pool-perturbation', 0.0), ('trace_archive_fraction', '--refresh-trace-archive-fraction', 1.0), ('trace_archive_min_distance', '--refresh-trace-archive-min-distance', 0.02), ('adaptive_max_fraction', '--refresh-adaptive-max-fraction', adaptive_max_fraction_default), ('adaptive_min_acceptance_rate', '--refresh-adaptive-min-acceptance-rate', 0.02), ('adaptive_min_rel_improvement', '--refresh-adaptive-min-rel-improvement', 0.005), ('physics_target_ttl', '--refresh-physics-target-ttl', 0.0), ('physics_target_efl', '--refresh-physics-target-efl', 0.0), ('physics_target_bfl', '--refresh-physics-target-bfl', 0.0), ('physics_ttl_min_fraction', '--refresh-physics-ttl-min-fraction', 0.75), ('physics_ttl_max_fraction', '--refresh-physics-ttl-max-fraction', 1.0), ('physics_power_jitter', '--refresh-physics-power-jitter', 0.15), ('physics_sa_temperature', '--refresh-physics-sa-temperature', 0.05)]
    for name, flag, fallback in float_options:
        args.extend([flag, str(max(0.0, _lensde_configuration.finite_float(opt(name, fallback), fallback)))])
    args.extend(['--refresh-physics-sa-iterations', str(max(0, int(_lensde_configuration.finite_float(opt('physics_sa_iterations', 48), 48))))])
    seed_fraction = max(0.0, _lensde_configuration.finite_float(opt('seed_pool_fraction', 0.0), 0.0))
    if seed_pool_path is not None and seed_fraction > 0.0:
        args.extend(['--refresh-seed-pool', str(seed_pool_path)])
    if bool(opt('perturb_material_variables', True)):
        args.append('--refresh-perturb-material-variables')
    else:
        args.append('--refresh-no-perturb-material-variables')
    if bool(opt('perturb_other_variables', False)):
        args.append('--refresh-perturb-other-variables')


def append_pbest_guidance_args(args: list[str], options: dict, prefix: str='', *, default_scope: str='legacy', default_global_fallback: bool=True, default_inject_best: bool=True) -> None:

    def opt(name: str, fallback):
        return options.get(f'{prefix}{name}', options.get(name, fallback))
    scope = _lensde_defaults.canonical_pbest_scope(opt('pbest_scope', default_scope), default_scope)
    args.extend(['--pbest-scope', scope, '--pbest-basin-rate', str(min(1.0, max(0.0, _lensde_configuration.finite_float(opt('pbest_basin_rate', 0.7), 0.7)))), '--pbest-same-key-rate', str(min(1.0, max(0.0, _lensde_configuration.finite_float(opt('pbest_same_key_rate', 0.2), 0.2)))), '--pbest-global-rate', str(min(1.0, max(0.0, _lensde_configuration.finite_float(opt('pbest_global_rate', 0.05), 0.05))))])
    if bool(opt('pbest_global_fallback', default_global_fallback)):
        args.append('--pbest-global-fallback')
    else:
        args.append('--pbest-no-global-fallback')
    if bool(opt('pbest_basin_nearest_fallback', False)):
        args.append('--pbest-basin-nearest-fallback')
    if not bool(opt('hybrid_inject_best', default_inject_best)):
        args.append('--hybrid-no-inject-best')


def append_de_mating_args(args: list[str], options: dict, prefix: str='', *, default_scope: str='global') -> None:

    def opt(name: str, fallback):
        return options.get(f'{prefix}{name}', options.get(name, fallback))
    scope = _lensde_defaults.canonical_de_mating_scope(opt('de_mating_scope', default_scope), default_scope)
    args.extend(['--de-mating-scope', scope, '--de-mating-local-rate', str(min(1.0, max(0.0, _lensde_configuration.finite_float(opt('de_mating_local_rate', 0.85), 0.85)))), '--de-mating-min-pool', str(max(4, int(_lensde_configuration.finite_float(opt('de_mating_min_pool', 8), 8))))])
    if bool(opt('de_mating_strict_local', False)):
        args.append('--de-mating-strict-local')
    else:
        args.append('--de-mating-no-strict-local')


def append_basin_active_quality_args(args: list[str], options: dict, prefix: str='') -> None:

    def opt(name: str, fallback):
        return options.get(f'{prefix}basin_{name}', options.get(f'basin_{name}', fallback))
    match_mode = str(opt('active_mating_match_mode', 'exact_key') or 'exact_key').strip().lower()
    match_mode = match_mode.replace('-', '_').replace(' ', '_')
    if match_mode not in {'exact_key', 'family_similarity'}:
        match_mode = 'exact_key'
    if bool(opt('active_quality_gate', True)):
        args.append('--basin-active-quality-gate')
    else:
        args.append('--basin-no-active-quality-gate')
    if bool(opt('active_require_trace_feasible', True)):
        args.append('--basin-active-require-trace-feasible')
    else:
        args.append('--basin-active-allow-trace-infeasible')
    args.extend(['--basin-active-selection-mode', str(opt('active_selection_mode', 'diversity')), '--basin-active-min-edge', str(float(opt('active_min_edge', 0.0))), '--basin-active-max-merit-ratio', str(float(opt('active_max_merit_ratio', 3.0))), '--basin-active-max-merit-delta', str(float(opt('active_max_merit_delta', 125.0))), '--basin-active-max-slots-per-family', str(max(0, int(_lensde_configuration.finite_float(opt('active_max_slots_per_family', 1), 1)))), '--basin-active-mating-match-mode', match_mode])
    if bool(opt('active_local_cohort', False)):
        args.append('--basin-active-local-cohort')
    else:
        args.append('--basin-no-active-local-cohort')
    args.extend(['--basin-active-local-cohort-size', str(max(0, int(_lensde_configuration.finite_float(opt('active_local_cohort_size', 0), 0)))), '--basin-active-local-cohort-fraction', str(min(1.0, max(0.0, _lensde_configuration.finite_float(opt('active_local_cohort_fraction', 0.35), 0.35)))), '--basin-active-local-cohort-max-distance', str(max(0.0, _lensde_configuration.finite_float(opt('active_local_cohort_max_distance', 0.0), 0.0)))])
    if bool(opt('active_mating_quota', False)):
        args.append('--basin-active-mating-quota')
    else:
        args.append('--basin-no-active-mating-quota')
    if bool(opt('active_niche_tracking', False)):
        args.append('--basin-active-niche-tracking')
    else:
        args.append('--basin-no-active-niche-tracking')
    if bool(opt('active_niche_survival', False)):
        args.append('--basin-active-niche-survival')
    else:
        args.append('--basin-no-active-niche-survival')
    args.extend(['--basin-active-niche-start-epoch', str(max(1, int(_lensde_configuration.finite_float(opt('active_niche_start_epoch', 1), 1)))), '--basin-active-niche-min-quota', str(max(0, int(_lensde_configuration.finite_float(opt('active_niche_min_quota', 0), 0)))), '--basin-active-niche-target-quota', str(max(0, int(_lensde_configuration.finite_float(opt('active_niche_target_quota', 0), 0)))), '--basin-active-niche-family-cap-fraction', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_niche_family_cap_fraction', 0.15), 0.15)))), '--basin-active-niche-repair-interval', str(max(1, int(_lensde_configuration.finite_float(opt('active_niche_repair_interval', 25), 25)))), '--basin-active-niche-repair-fraction', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_niche_repair_fraction', 0.03), 0.03)))), '--basin-active-niche-rebalance-interval', str(max(1, int(_lensde_configuration.finite_float(opt('active_niche_rebalance_interval', 25), 25)))), '--basin-active-niche-rebalance-fraction', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_niche_rebalance_fraction', 0.05), 0.05)))), '--basin-active-niche-elite-seed-count', str(max(1, int(_lensde_configuration.finite_float(opt('active_niche_elite_seed_count', 1), 1))))])
    if bool(opt('active_niche_rebalance', False)):
        args.append('--basin-active-niche-rebalance')
    else:
        args.append('--basin-no-active-niche-rebalance')
    if bool(opt('active_niche_soft_fill', False)):
        args.append('--basin-active-niche-soft-fill')
    else:
        args.append('--basin-no-active-niche-soft-fill')
    if bool(opt('active_focus_rebalance', False)):
        args.append('--basin-active-focus-rebalance')
    else:
        args.append('--basin-no-active-focus-rebalance')
    args.extend(['--basin-active-focus-start-epoch', str(max(1, int(_lensde_configuration.finite_float(opt('active_focus_start_epoch', 1), 1)))), '--basin-active-focus-family-count', str(max(1, int(_lensde_configuration.finite_float(opt('active_focus_family_count', 1), 1)))), '--basin-active-focus-population-fraction', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_focus_population_fraction', 0.55), 0.55)))), '--basin-active-focus-min-quota', str(max(0, int(_lensde_configuration.finite_float(opt('active_focus_min_quota', 0), 0)))), '--basin-active-focus-elite-seed-count', str(max(1, int(_lensde_configuration.finite_float(opt('active_focus_elite_seed_count', 1), 1)))), '--basin-active-monitor-family-cap', str(max(0, int(_lensde_configuration.finite_float(opt('active_monitor_family_cap', 0), 0)))), '--basin-active-focus-stop-rel-improvement', str(max(0.0, _lensde_configuration.finite_float(opt('active_focus_stop_rel_improvement', 0.2), 0.2))), '--basin-active-focus-ready-fraction', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_focus_ready_fraction', 0.7), 0.7)))), '--basin-active-focus-cooldown-epochs', str(max(0, int(_lensde_configuration.finite_float(opt('active_focus_cooldown_epochs', 2), 2)))), '--basin-active-focus-stop-after-clears', str(max(0, int(_lensde_configuration.finite_float(opt('active_focus_stop_after_clears', 0), 0)))), '--basin-active-focus-stop-release-fraction', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_focus_stop_release_fraction', 1.0), 1.0)))), '--basin-active-focus-rotation-history-epochs', str(max(0, int(_lensde_configuration.finite_float(opt('active_focus_rotation_history_epochs', 8), 8)))), '--basin-active-focus-rotation-similarity-threshold', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_focus_rotation_similarity_threshold', 0.8), 0.8))))])
    if bool(opt('active_focus_rotation', False)):
        args.append('--basin-active-focus-rotation')
    else:
        args.append('--basin-no-active-focus-rotation')
    if bool(opt('active_focus_stop_release', True)):
        args.append('--basin-active-focus-stop-release')
    else:
        args.append('--basin-no-active-focus-stop-release')
    if bool(opt('active_focus_initial_best_family', True)):
        args.append('--basin-active-focus-initial-best-family')
    else:
        args.append('--basin-no-active-focus-initial-best-family')
    if bool(opt('active_focus_soft_fill', False)):
        args.append('--basin-active-focus-soft-fill')
    else:
        args.append('--basin-no-active-focus-soft-fill')
    if bool(opt('active_refresh_rebalance', False)):
        args.append('--basin-active-refresh-rebalance')
    else:
        args.append('--basin-no-active-refresh-rebalance')
    args.extend(['--basin-active-family-similarity-threshold', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_family_similarity_threshold', 0.8), 0.8)))), '--basin-active-family-max-shape-diff', str(max(-1, int(_lensde_configuration.finite_float(opt('active_family_max_shape_diff', 3), 3)))), '--basin-active-family-parameter-merge-distance', str(max(0.0, _lensde_configuration.finite_float(opt('active_family_parameter_merge_distance', 0.15), 0.15))), '--basin-active-family-parameter-merge-metric', str(opt('active_family_parameter_merge_metric', 'structure_bound_normalized')), '--basin-active-refresh-max-family-fraction', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_refresh_max_family_fraction', 0.0), 0.0)))), '--basin-active-refresh-min-local-fraction', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_refresh_min_local_fraction', 0.8), 0.8))))])
    if bool(opt('active_archive_only', False)):
        args.append('--basin-active-archive-only')
    else:
        args.append('--basin-no-active-archive-only')
    if bool(opt('active_slot_strict_frozen_family', False)):
        args.append('--basin-active-slot-strict-frozen-family')
    else:
        args.append('--basin-no-active-slot-strict-frozen-family')
    if bool(opt('active_family_parameter_merge_enabled', False)):
        args.append('--basin-active-family-parameter-merge')
    else:
        args.append('--basin-no-active-family-parameter-merge')
    if bool(opt('active_plateau_lifecycle', False)):
        args.append('--basin-active-plateau-lifecycle')
    else:
        args.append('--basin-no-active-plateau-lifecycle')
    if bool(opt('active_plateau_track_active_niches', False)):
        args.append('--basin-active-plateau-track-active-niches')
    else:
        args.append('--basin-no-active-plateau-track-active-niches')
    args.extend(['--basin-active-plateau-window', str(max(1, int(_lensde_configuration.finite_float(opt('active_plateau_window', 2), 2)))), '--basin-active-plateau-min-rel-improvement', str(max(0.0, _lensde_configuration.finite_float(opt('active_plateau_min_rel_improvement', 0.005), 0.005))), '--basin-active-plateau-local-quota-scale', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_plateau_local_quota_scale', 0.35), 0.35)))), '--basin-active-plateau-external-probe-rate', str(max(0.0, min(1.0, _lensde_configuration.finite_float(opt('active_plateau_external_probe_rate', 0.15), 0.15)))), '--basin-active-plateau-retire-after', str(max(0, int(_lensde_configuration.finite_float(opt('active_plateau_retire_after', 4), 4)))), '--basin-active-plateau-cooldown-epochs', str(max(0, int(_lensde_configuration.finite_float(opt('active_plateau_cooldown_epochs', 2), 2))))])


def save_ranked_candidate_outputs(run_dir: Path, stage_dir: Path, base_system: dict, variables: list[dict], glass_search_config: dict, run_merit: dict, options: dict, exe: Path, keep_count: int, timeout_seconds: int, started_at: float, stdout_parts: list[str], stderr_parts: list[str], source_name: str) -> list[dict]:
    if keep_count <= 0:
        return []
    population_path = stage_dir / 'population.csv'
    scores_path = stage_dir / 'population_scores.csv'
    basin_path = stage_dir / 'basin_archive.json'
    basin_ranked: list[tuple[int, float]] = []
    basin_rows: list[list[float]] = []
    row_sources: list[dict] = []
    score_records: dict[int, dict] = {}
    source_variable_count_cache: dict[str, int | None] = {}

    def resolve_run_local_path(path_text: object) -> Path:
        path = Path(str(path_text or '').strip())
        if not str(path):
            return path
        if path.exists():
            return path
        try:
            parts = path.parts
            work_name = run_dir.parent.name.lower()
            for index, part in enumerate(parts[:-2]):
                if part.lower() != work_name:
                    continue
                candidate = run_dir.joinpath(*parts[index + 2:])
                if candidate.exists():
                    return candidate
        except Exception:
            pass
        return path

    def source_variable_count(path_text: object) -> int | None:
        key = str(path_text or '').strip()
        if not key:
            return None
        if key in source_variable_count_cache:
            return source_variable_count_cache[key]
        count: int | None = None
        path = resolve_run_local_path(key)
        if path.exists():
            try:
                raw = _lensde_provenance.read_json(path)
                source_variables = raw.get('variables', []) if isinstance(raw, dict) else raw
                if isinstance(source_variables, list):
                    count = len(source_variables)
            except Exception as exc:
                stderr_parts.append(f'[candidates] failed to read source variables {path}: {exc}')
        source_variable_count_cache[key] = count
        return count
    if basin_path.exists():
        try:
            basin_data = _lensde_provenance.read_json(basin_path)
            for basin in basin_data.get('basins', []):
                values = basin.get('values')
                if not isinstance(values, list):
                    continue
                source_variables_path = basin.get('source_variables_path')
                expected_counts = {len(variables)}
                count_from_source = source_variable_count(source_variables_path)
                if count_from_source is not None:
                    expected_counts.add(count_from_source)
                if len(values) not in expected_counts:
                    stderr_parts.append(f"[candidates] skip basin with variable count mismatch: values={len(values)}, expected={sorted(expected_counts)}, source_variables_path={source_variables_path or ''}")
                    continue
                row = [_lensde_configuration.finite_float(value, 0.0) for value in values]
                score = basin.get('score', {}) if isinstance(basin.get('score'), dict) else {}
                merit = _lensde_configuration.finite_float(score.get('merit'), _lensde_configuration.finite_float(basin.get('objective'), float('inf')))
                row_index = len(basin_rows)
                basin_rows.append(row)
                basin_ranked.append((row_index, merit))
                trace_feasible = bool(basin.get('trace_feasible', False))
                score_records[row_index] = {'merit': merit, 'objective': _lensde_configuration.finite_float(basin.get('objective'), float('inf')), 'total_violation': _lensde_configuration.finite_float(basin.get('total_violation'), float('inf')), 'trace_violation': _lensde_configuration.finite_float(basin.get('trace_violation'), float('inf')), 'constraint_violation': _lensde_configuration.finite_float(basin.get('constraint_violation'), float('inf')), 'first_order_violation': _lensde_configuration.finite_float(basin.get('first_order_violation'), float('inf')), 'feasible': 1.0 if bool(basin.get('feasible', False)) else 0.0, 'trace_feasible': 1.0 if trace_feasible else 0.0, 'constraint_feasible': 1.0 if bool(basin.get('constraint_feasible', False)) else 0.0, 'first_order_feasible': 1.0 if bool(basin.get('first_order_feasible', False)) else 0.0, 'trace_ok': 1.0 if bool(score.get('trace_ok', False)) or trace_feasible else 0.0, 'trace_coverage': _lensde_configuration.finite_float(score.get('trace_coverage'), 1.0 if trace_feasible else 0.0), 'min_field_coverage': _lensde_configuration.finite_float(score.get('min_field_coverage'), 1.0 if trace_feasible else 0.0), 'efl': _lensde_configuration.finite_float(score.get('efl'), float('nan')), 'ttl': _lensde_configuration.finite_float(score.get('ttl'), float('nan')), 'bfl': _lensde_configuration.finite_float(score.get('bfl'), float('nan')), 'mean_spot': _lensde_configuration.finite_float(score.get('mean_spot'), float('nan')), 'max_spot': _lensde_configuration.finite_float(score.get('max_spot'), float('nan'))}
                row_sources.append({'candidate_source': 'basin_archive', 'basin_id': basin.get('id'), 'basin_rank': basin.get('rank'), 'basin_topology_key': basin.get('topology_key'), 'basin_structural_topology_key': basin.get('structural_topology_key', basin.get('topology_key')), 'basin_semantic_key': basin.get('semantic_key'), 'basin_nearest_distance': basin.get('nearest_distance'), 'basin_trace_feasible': basin.get('trace_feasible'), 'basin_feasible': basin.get('feasible'), 'basin_source': basin.get('source'), 'basin_source_dir': basin.get('source_dir'), 'source_system_input': basin.get('source_system_input'), 'source_variables_path': source_variables_path, 'source_variable_count': count_from_source, 'source_glass_search_path': basin.get('source_glass_search_path'), 'funnel_material_mode': basin.get('funnel_material_mode'), 'glass_names': basin.get('glass_names', [])})
        except Exception as exc:
            stderr_parts.append(f'[candidates] failed to read basin archive: {exc}')
    if not basin_rows and (not population_path.exists() or not scores_path.exists()):
        return []
    try:
        if basin_rows:
            rows = basin_rows
            scores = basin_ranked
        else:
            rows = _lensde_populations.read_population_csv(population_path)
            scores = _lensde_populations.read_population_scores_csv(scores_path)
            row_sources = [{'candidate_source': 'population'} for _ in rows]
    except Exception as exc:
        stderr_parts.append(f'[candidates] failed to read population: {exc}')
        return []
    ranked = [(index, merit) for index, merit in scores if 0 <= index < len(rows)]
    if not basin_rows:
        try:
            with scores_path.open(newline='', encoding='utf-8', errors='replace') as f:
                reader = csv.DictReader(f)
                for record in reader:
                    index = int(float(record.get('index', '')))
                    if not 0 <= index < len(rows):
                        continue
                    score_records[index] = record
                    if index < len(row_sources):
                        row_sources[index].update({'source_trace_ok': bool(int(float(record.get('trace_ok', '0') or 0))), 'source_trace_coverage': _lensde_configuration.finite_float(record.get('trace_coverage'), 0.0), 'source_min_field_coverage': _lensde_configuration.finite_float(record.get('min_field_coverage'), 0.0), 'source_mean_spot': _lensde_configuration.finite_float(record.get('mean_spot'), float('nan')), 'source_max_spot': _lensde_configuration.finite_float(record.get('max_spot'), float('nan')), 'source_trace_feasible': bool(int(float(record.get('trace_feasible', '0') or 0))), 'source_constraint_feasible': bool(int(float(record.get('constraint_feasible', '0') or 0))), 'source_first_order_feasible': bool(int(float(record.get('first_order_feasible', '0') or 0)))})
        except Exception as exc:
            stderr_parts.append(f'[candidates] failed to read detailed population scores: {exc}')

    def record_flag(record: dict, name: str, default: bool=False) -> bool:
        value = record.get(name)
        if isinstance(value, bool):
            return value
        try:
            return bool(int(float(value)))
        except (TypeError, ValueError):
            return default

    def candidate_sort_key(item: tuple[int, float]) -> tuple[float, float, float, float, float, float, float]:
        index, merit = item
        record = score_records.get(index, {})
        trace_ok = record_flag(record, 'trace_ok')
        trace_feasible = record_flag(record, 'trace_feasible', trace_ok)
        trace_coverage = _lensde_configuration.finite_float(record.get('trace_coverage'), 0.0) if record else 0.0
        min_field_coverage = _lensde_configuration.finite_float(record.get('min_field_coverage'), 0.0) if record else 0.0
        efl = _lensde_configuration.finite_float(record.get('efl'), float('nan')) if record else float('nan')
        ttl = _lensde_configuration.finite_float(record.get('ttl'), float('nan')) if record else float('nan')
        bfl = _lensde_configuration.finite_float(record.get('bfl'), float('nan')) if record else float('nan')
        finite_first_order = math.isfinite(efl) and math.isfinite(ttl) and math.isfinite(bfl)
        full_trace = trace_ok and trace_coverage >= 1.0 - 1e-09 and (min_field_coverage >= 1.0 - 1e-09) and finite_first_order
        total_violation = _lensde_configuration.finite_float(record.get('total_violation'), float('inf')) if record else float('inf')
        return (_lensde_configuration.finite_float(merit, float('inf')), total_violation, -trace_coverage, -min_field_coverage, 0.0 if full_trace else 1.0, 0.0 if trace_feasible else 1.0, 0.0 if finite_first_order else 1.0)
    ranked.sort(key=candidate_sort_key)
    if not ranked:
        return []
    diversity_metric = str(options.get('initial_search_candidate_diversity_metric', 'structure_bound_normalized')).lower().replace('-', '_')
    allow_diversity_relax = bool(options.get('initial_search_candidate_diversity_allow_relax', False))
    topology_exempt_diversity = bool(options.get('initial_search_candidate_diversity_topology_exempt', False))
    if 'initial_search_candidate_air_thickness_distance_cap' in options:
        air_distance_cap = positive_option(options, 'initial_search_candidate_air_thickness_distance_cap')
    else:
        air_distance_cap = positive_option(options, 'initial_search_seed_air_thickness_cap')
        if air_distance_cap is None:
            air_distance_cap = positive_option(options, 'de_air_thickness_max')

    def diversity_variable_weight(variable: dict) -> float:
        kind = str(variable.get('kind', '')).lower()
        if diversity_metric in {'structure', 'structural', 'geometry', 'structure_bound_normalized', 'structural_bound_normalized', 'geometry_bound_normalized'}:
            return 1.0 if kind in {'radius', 'curvature', 'thickness'} else 0.0
        if diversity_metric in {'structure_material_weak', 'structural_material_weak', 'geometry_material_weak'}:
            if kind in {'radius', 'curvature', 'thickness'}:
                return 1.0
            if kind in {'material_nd', 'material_vd', 'glass_choice'}:
                return 0.15
            return 0.0
        return 1.0

    def diversity_variable_span(variable: dict, av: float, bv: float) -> float:
        lo = _lensde_configuration.finite_float(variable.get('lower'), 0.0)
        hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
        if hi < lo:
            lo, hi = (hi, lo)
        if _lensde_populations.is_air_thickness_variable(base_system, variable) and air_distance_cap is not None:
            hi = min(hi, max(lo + 1e-12, air_distance_cap))
        span = abs(hi - lo)
        if span <= 1e-12 or not math.isfinite(span):
            span = max(1.0, abs(av), abs(bv))
        return span

    def variable_normalized_distance(a: list[float], b: list[float]) -> float:
        if not variables:
            return 0.0
        total = 0.0
        weight_sum = 0.0
        for variable, av, bv in zip(variables, a, b, strict=False):
            weight = diversity_variable_weight(variable)
            if weight <= 0.0:
                continue
            span = diversity_variable_span(variable, av, bv)
            if variable.get('kind') == 'glass_choice':
                delta = 0.0 if int(round(av)) == int(round(bv)) else 1.0
            else:
                delta = (_lensde_configuration.finite_float(av, 0.0) - _lensde_configuration.finite_float(bv, 0.0)) / span
            total += weight * delta * delta
            weight_sum += weight
        return math.sqrt(total / max(1e-12, weight_sum))

    def candidate_topology_key(index: int) -> str:
        if not 0 <= index < len(row_sources):
            return ''
        source = row_sources[index]
        if not isinstance(source, dict):
            return ''
        for key in ('basin_topology_key', 'basin_semantic_key', 'basin_structural_topology_key', 'topology_key', 'semantic_key', 'structural_topology_key'):
            value = source.get(key)
            if value is None:
                continue
            text = str(value).strip()
            if text:
                return text
        return ''
    requested_radius = max(0.0, _lensde_configuration.finite_float(options.get('initial_search_candidate_diversity_radius'), _lensde_configuration.finite_float(options.get('initial_search_basin_radius'), 0.25)))
    selected: list[tuple[int, float, float]] = []
    selected_rows: list[list[float]] = []
    selected_topology_keys: list[str] = []
    relaxed_radius = requested_radius
    while len(selected) < keep_count and relaxed_radius >= 0.0:
        for source_index, source_merit in ranked:
            if any((existing_index == source_index for existing_index, _, _ in selected)):
                continue
            topology_key = candidate_topology_key(source_index)
            all_min_distance = float('inf')
            scoped_min_distance = float('inf')
            for existing, existing_topology_key in zip(selected_rows, selected_topology_keys, strict=False):
                distance = variable_normalized_distance(rows[source_index], existing)
                all_min_distance = min(all_min_distance, distance)
                same_topology_scope = not topology_exempt_diversity or not topology_key or (not existing_topology_key) or (topology_key == existing_topology_key)
                if same_topology_scope:
                    scoped_min_distance = min(scoped_min_distance, distance)
            if not selected_rows or scoped_min_distance >= relaxed_radius:
                selected.append((source_index, source_merit, all_min_distance))
                selected_rows.append(rows[source_index])
                selected_topology_keys.append(topology_key)
                if len(selected) >= keep_count:
                    break
        if len(selected) >= keep_count or relaxed_radius <= 1e-12:
            break
        if not allow_diversity_relax:
            break
        relaxed_radius *= 0.5
    ranked = selected[:keep_count]
    if len(ranked) < keep_count:
        stdout_parts.append(f'[candidates] kept {len(ranked)} of {keep_count} requested candidates with metric={diversity_metric} radius={requested_radius:g} allow_relax={str(allow_diversity_relax).lower()}\n')
    root = run_dir / 'candidates'
    root.mkdir(parents=True, exist_ok=True)
    full_merit_path = run_dir / 'candidate_merit.json'
    _lensde_provenance.write_json(full_merit_path, run_merit)
    candidates: list[dict] = []

    def candidate_context_for_row(source_meta: dict, values: list[float]) -> tuple[dict, list[dict], dict, dict]:
        context_meta = {'candidate_context': 'stage_default', 'candidate_context_system_path': '', 'candidate_context_variables_path': '', 'candidate_context_glass_search_path': ''}
        system_path_text = str(source_meta.get('source_system_input') or '').strip()
        variables_path_text = str(source_meta.get('source_variables_path') or '').strip()
        glass_path_text = str(source_meta.get('source_glass_search_path') or '').strip()
        if not system_path_text or not variables_path_text:
            return (base_system, variables, glass_search_config, context_meta)
        system_path = resolve_run_local_path(system_path_text)
        variables_path = resolve_run_local_path(variables_path_text)
        if not system_path.exists() or not variables_path.exists():
            context_meta['candidate_context_warning'] = 'source_context_path_missing'
            return (base_system, variables, glass_search_config, context_meta)
        try:
            source_system = _lensde_provenance.read_json(system_path)
            source_variables_raw = _lensde_provenance.read_json(variables_path)
            source_variables = source_variables_raw.get('variables', []) if isinstance(source_variables_raw, dict) else source_variables_raw
            if not isinstance(source_system, dict) or not isinstance(source_variables, list):
                raise ValueError('invalid source context')
            if len(values) != len(source_variables):
                context_meta['candidate_context_warning'] = f'source_variable_count_mismatch:{len(values)}!={len(source_variables)}'
                return (base_system, variables, glass_search_config, context_meta)
            source_glass_search = glass_search_config
            if glass_path_text:
                glass_path = resolve_run_local_path(glass_path_text)
                if glass_path.exists():
                    loaded_glass = _lensde_provenance.read_json(glass_path)
                    if isinstance(loaded_glass, dict):
                        source_glass_search = loaded_glass
            context_meta.update({'candidate_context': 'source', 'candidate_context_system_path': str(system_path), 'candidate_context_variables_path': str(variables_path), 'candidate_context_glass_search_path': str(resolve_run_local_path(glass_path_text)) if glass_path_text else ''})
            return (source_system, source_variables, source_glass_search, context_meta)
        except Exception as exc:
            context_meta['candidate_context_warning'] = f'source_context_load_failed:{exc}'
            stderr_parts.append(f'[candidates] failed to load source context for candidate: {exc}')
            return (base_system, variables, glass_search_config, context_meta)
    for rank, (source_index, source_merit, distance_to_prior) in enumerate(ranked, start=1):
        candidate_dir = native_io_paths.named_child(root, f'candidate_{rank:03d}')
        candidate_dir.mkdir(parents=True, exist_ok=True)
        source_meta = row_sources[source_index] if 0 <= source_index < len(row_sources) else {}
        row_values = rows[source_index]
        candidate_base_system, candidate_variables, candidate_glass_search_config, context_meta = candidate_context_for_row(source_meta, row_values)
        candidate_system = _lensde_materials.apply_variable_values_to_system(candidate_base_system, candidate_variables, row_values, candidate_glass_search_config)
        candidate_input = candidate_dir / 'input_system.json'
        _lensde_provenance.write_json(candidate_input, _lensde_configuration.system_with_ray_aiming(candidate_system, options))
        args = [str(exe), *_lensde_materials.trace_args_for_project(options, run_merit, candidate_dir, system_path=candidate_input, merit_path=full_merit_path)]
        if options.get('clear_aperture_update', True):
            args.append('--clear-aperture-update')
        if options.get('sync_stop_aperture', True):
            args.append('--sync-stop-aperture')
        code, out, err, timed_out = _lensde_execution.run_stage(args, timeout_seconds, started_at)
        stdout_parts.append(f'\n[candidate:{rank:03d}]\n' + out)
        if err:
            stderr_parts.append(f'[candidate:{rank:03d}]\n' + err)
        score = _lensde_provenance.read_json(candidate_dir / 'score.json') if (candidate_dir / 'score.json').exists() else None
        candidates.append({'rank': rank, 'source': source_name, **source_meta, **context_meta, 'source_population_index': source_index, 'source_merit': source_merit, 'distance_to_prior_saved': None if not math.isfinite(distance_to_prior) else distance_to_prior, 'requested_diversity_radius': requested_radius, 'candidate_diversity_metric': diversity_metric, 'candidate_diversity_allow_relax': allow_diversity_relax, 'candidate_diversity_topology_exempt': topology_exempt_diversity, 'candidate_diversity_final_radius': relaxed_radius, 'candidate_air_thickness_distance_cap': air_distance_cap, 'returncode': code, 'timed_out': timed_out, 'directory': str(candidate_dir.relative_to(run_dir)), 'system_url': _lensde_materials.run_file_url(run_dir, f'candidates/candidate_{rank:03d}/system.json'), 'score_url': _lensde_materials.run_file_url(run_dir, f'candidates/candidate_{rank:03d}/score.json'), 'layout_url': _lensde_materials.run_file_url(run_dir, f'candidates/candidate_{rank:03d}/layout.svg'), 'spot_url': _lensde_materials.run_file_url(run_dir, f'candidates/candidate_{rank:03d}/spot.csv'), 'score': score})
        if timed_out:
            break
    return candidates


def sync_variables_from_system(variables: list[dict], system: dict, *, rebase_asphere: bool | None=None) -> list[dict]:
    project = {'system': system, 'variables': copy.deepcopy(variables)}
    _lensde_geometry.sync_variable_initials(project)
    if rebase_asphere is not False:
        _lensde_geometry.rebase_asphere_edge_sag_variables(system, project['variables'], force=rebase_asphere is True)
    return project['variables']


def map_population_rows(source_rows: list[list[float]], source_variables: list[dict], target_variables: list[dict]) -> list[list[float]]:
    source_keys = [_lensde_geometry.variable_key(variable) for variable in source_variables]
    target_rows = []
    for source_row in source_rows:
        values = {key: source_row[i] for i, key in enumerate(source_keys) if i < len(source_row)}
        source_vignetting: dict[int, dict[str, float]] = {}
        for source_index, variable in enumerate(source_variables):
            if source_index >= len(source_row):
                continue
            kind = _lensde_configuration.canonical_vignetting_variable_kind(variable.get('kind'))
            if not kind:
                continue
            try:
                field_index = int(variable.get('index', -1))
            except (TypeError, ValueError):
                continue
            source_vignetting.setdefault(field_index, {})[kind] = _lensde_configuration.finite_float(source_row[source_index], _lensde_configuration.finite_float(variable.get('initial'), 0.0))
        mapped: list[float] = []
        for variable in target_variables:
            exact = values.get(_lensde_geometry.variable_key(variable))
            if exact is not None:
                mapped.append(exact)
                continue
            kind = _lensde_configuration.canonical_vignetting_variable_kind(variable.get('kind'))
            try:
                field_index = int(variable.get('index', -1))
            except (TypeError, ValueError):
                field_index = -1
            source = source_vignetting.get(field_index, {})
            direct_fvcy = source.get('fvcy')
            direct_fvdy = source.get('fvdy')
            source_magnitude = source.get('vignette_magnitude_y')
            source_asymmetry = source.get('vignette_asymmetry_y')
            if source_magnitude is None and direct_fvdy is not None:
                source_magnitude = max(0.0, direct_fvdy, abs(direct_fvcy) if direct_fvcy is not None else 0.0)
            if source_asymmetry is None and direct_fvcy is not None:
                source_asymmetry = max(-1.0, min(1.0, direct_fvcy / source_magnitude)) if source_magnitude is not None and source_magnitude > 1e-15 else 0.0
            if kind == 'vignette_magnitude_y' and source_magnitude is not None:
                mapped.append(source_magnitude)
            elif kind == 'vignette_asymmetry_y' and source_asymmetry is not None:
                mapped.append(source_asymmetry)
            elif kind == 'fvdy' and source_magnitude is not None:
                mapped.append(source_magnitude)
            elif kind == 'fvcy' and source_magnitude is not None and (source_asymmetry is not None):
                mapped.append(source_magnitude * source_asymmetry)
            else:
                mapped.append(_lensde_configuration.finite_float(variable.get('initial'), 0.0))
        target_rows.append(mapped)
    return target_rows


def map_population_row(source_row: list[float], source_variables: list[dict], target_variables: list[dict]) -> list[float]:
    rows = map_population_rows([source_row], source_variables, target_variables)
    return rows[0] if rows else [_lensde_configuration.finite_float(variable.get('initial'), 0.0) for variable in target_variables]


def source_material_nd_vd_by_surface(source_row: list[float], source_variables: list[dict]) -> dict[int, dict[str, float]]:
    materials: dict[int, dict[str, float]] = {}
    for row_index, variable in enumerate(source_variables):
        if row_index >= len(source_row):
            continue
        kind = str(variable.get('kind', '')).lower()
        if kind not in {'material_nd', 'material_vd'}:
            continue
        try:
            surface_index = int(variable.get('index', -1))
        except (TypeError, ValueError):
            continue
        if surface_index < 0:
            continue
        entry = materials.setdefault(surface_index, {})
        if kind == 'material_nd':
            entry['nd'] = _lensde_configuration.finite_float(source_row[row_index], float('nan'))
        else:
            entry['vd'] = _lensde_configuration.finite_float(source_row[row_index], float('nan'))
    return materials


def map_population_row_with_glass_choices(source_row: list[float], source_variables: list[dict], target_variables: list[dict], glass_search_config: dict | None, *, nd_scale: float=state.GLASS_DISTANCE_ND_SCALE, vd_scale: float=state.GLASS_DISTANCE_VD_SCALE) -> list[float]:
    mapped = map_population_row(source_row, source_variables, target_variables)
    if not isinstance(glass_search_config, dict) or not glass_search_config.get('enabled'):
        return mapped
    materials = source_material_nd_vd_by_surface(source_row, source_variables)
    if not materials:
        return mapped
    groups = glass_search_config.get('groups', [])
    if not isinstance(groups, list) or not groups:
        return mapped
    groups_by_id: dict[int, dict] = {}
    groups_by_surface: dict[int, dict] = {}
    for group in groups:
        if not isinstance(group, dict):
            continue
        try:
            group_id = int(group.get('id', -1))
        except (TypeError, ValueError):
            group_id = -1
        try:
            surface_index = int(group.get('surface_index', -1))
        except (TypeError, ValueError):
            surface_index = -1
        if group_id >= 0:
            groups_by_id[group_id] = group
        if surface_index >= 0:
            groups_by_surface[surface_index] = group
    for target_index, variable in enumerate(target_variables):
        if target_index >= len(mapped) or str(variable.get('kind', '')).lower() != 'glass_choice':
            continue
        try:
            surface_index = int(variable.get('index', -1))
        except (TypeError, ValueError):
            surface_index = -1
        try:
            group_id = int(variable.get('glass_group', -1))
        except (TypeError, ValueError):
            group_id = -1
        material = materials.get(surface_index)
        if not material:
            continue
        nd = _lensde_configuration.finite_float(material.get('nd'), float('nan'))
        vd = _lensde_configuration.finite_float(material.get('vd'), float('nan'))
        if not math.isfinite(nd) or not math.isfinite(vd) or nd <= 1.0000001 or (vd <= 0.0):
            continue
        group = groups_by_id.get(group_id) or groups_by_surface.get(surface_index)
        candidates = group.get('candidates', []) if isinstance(group, dict) else []
        if not isinstance(candidates, list) or not candidates:
            continue
        best_index = min(range(len(candidates)), key=lambda index: _lensde_materials.glass_nd_vd_distance(nd, vd, candidates[index] if isinstance(candidates[index], dict) else {}, nd_scale=nd_scale, vd_scale=vd_scale))
        lower, upper = _lensde_populations.variable_bounds(variable)
        mapped[target_index] = _lensde_populations.clamp_value(float(best_index), lower, upper)
    return mapped


def activate_new_stage_variables(system: dict, row: list[float], source_variables: list[dict], target_variables: list[dict], rng: random.Random, *, activation_fraction: float=0.65, scale: float=0.35) -> list[float]:
    out = [_lensde_configuration.finite_float(value, 0.0) for value in row]
    source_keys = {_lensde_geometry.variable_key(variable) for variable in source_variables}
    activation_fraction = _lensde_populations.clamp_value(_lensde_configuration.finite_float(activation_fraction, 0.65), 0.0, 1.0)
    for index, variable in enumerate(target_variables):
        if index >= len(out) or _lensde_geometry.variable_key(variable) in source_keys:
            continue
        kind = str(variable.get('kind', '')).lower()
        if kind not in {'conic', 'asphere_coeff'} or rng.random() > activation_fraction:
            continue
        out[index] = _lensde_populations.local_funnel_perturb_value(system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0), rng, scale)
    return out


def map_population_row_with_stage_activation(system: dict, source_row: list[float], source_variables: list[dict], target_variables: list[dict], rng: random.Random, *, activate_new_variables: bool=True, activation_fraction: float=0.65, scale: float=0.35) -> list[float]:
    mapped = map_population_row(source_row, source_variables, target_variables)
    if not activate_new_variables:
        return mapped
    return activate_new_stage_variables(system, mapped, source_variables, target_variables, rng, activation_fraction=activation_fraction, scale=scale)


def population_score_summary(scores: list[tuple[int, float]]) -> dict:
    values = sorted((merit for _, merit in scores if math.isfinite(merit)))
    if not values:
        return {'count': 0}

    def quantile(p: float) -> float:
        index = min(len(values) - 1, max(0, int(math.floor((len(values) - 1) * p))))
        return values[index]
    return {'count': len(values), 'min': values[0], 'q01': quantile(0.01), 'q05': quantile(0.05), 'q10': quantile(0.1), 'median': quantile(0.5), 'q90': quantile(0.9), 'q95': quantile(0.95), 'q99': quantile(0.99), 'lt_0p05': sum((1 for value in values if value < 0.05)), 'lt_0p1': sum((1 for value in values if value < 0.1)), 'lt_0p5': sum((1 for value in values if value < 0.5)), 'lt_1': sum((1 for value in values if value < 1.0))}


def positive_option(options: dict, key: str) -> float | None:
    value = _lensde_configuration.finite_float(options.get(key), 0.0)
    return value if value > 0.0 and math.isfinite(value) else None


def set_variable_bounds(item: dict, lower: float | None, upper: float | None) -> None:
    initial = _lensde_configuration.finite_float(item.get('initial'), 0.0)
    lo = _lensde_configuration.finite_float(item.get('lower'), initial) if lower is None else float(lower)
    hi = _lensde_configuration.finite_float(item.get('upper'), initial) if upper is None else float(upper)
    if not math.isfinite(lo) or not math.isfinite(hi):
        return
    if lo >= hi:
        return
    item['lower'] = lo
    item['upper'] = hi
    item['initial'] = _lensde_populations.clamp_value(initial, lo, hi)


def apply_de_bound_overrides(system: dict, variables: list[dict], options: dict) -> list[dict]:
    out = copy.deepcopy(variables)
    if not bool(options.get('de_override_bounds', False)):
        return out
    curvature_abs = positive_option(options, 'de_curvature_abs_max')
    conic_abs = positive_option(options, 'de_conic_abs_max')
    asphere_abs = positive_option(options, 'de_asphere_abs_max')
    thickness_min = positive_option(options, 'de_thickness_min')
    thickness_max = positive_option(options, 'de_thickness_max')
    air_thickness_max = positive_option(options, 'de_air_thickness_max')
    glass_thickness_max = positive_option(options, 'de_glass_thickness_max')
    nd_min = positive_option(options, 'de_material_nd_min')
    nd_max = positive_option(options, 'de_material_nd_max')
    vd_min = positive_option(options, 'de_material_vd_min')
    vd_max = positive_option(options, 'de_material_vd_max')
    for item in out:
        kind = str(item.get('kind', ''))
        if kind == 'curvature' and curvature_abs is not None:
            set_variable_bounds(item, -curvature_abs, curvature_abs)
        elif kind == 'conic' and conic_abs is not None:
            set_variable_bounds(item, -conic_abs, conic_abs)
        elif kind == 'asphere_coeff' and asphere_abs is not None:
            set_variable_bounds(item, -asphere_abs, asphere_abs)
            has_q_metadata = _lensde_populations.is_profiled_asphere_seed_variable(item) or str(item.get('asphere_parameterization', '')).lower() == 'edge_sag' or 'edge_sag_lower' in item or ('edge_sag_upper' in item)
            if has_q_metadata:
                radius = max(_lensde_configuration.finite_float(item.get('asphere_reference_radius'), 1.0), 1e-12)
                try:
                    radial_power = radius ** _lensde_populations.asphere_order_for_term(int(item.get('term', 0) or 0))
                except OverflowError:
                    radial_power = math.inf
                if math.isfinite(radial_power) and radial_power > 0.0:
                    q_abs = asphere_abs * radial_power
                    item['edge_sag_lower'] = -q_abs
                    item['edge_sag_upper'] = q_abs
        elif kind == 'thickness':
            upper = thickness_max
            if _lensde_populations.is_air_thickness_variable(system, item) and air_thickness_max is not None:
                upper = air_thickness_max
            elif _lensde_populations.is_glass_thickness_variable(system, item) and glass_thickness_max is not None:
                upper = glass_thickness_max
            if thickness_min is not None or upper is not None:
                set_variable_bounds(item, thickness_min, upper)
        elif kind == 'material_nd' and (nd_min is not None or nd_max is not None):
            set_variable_bounds(item, nd_min, nd_max)
        elif kind == 'material_vd' and (vd_min is not None or vd_max is not None):
            set_variable_bounds(item, vd_min, vd_max)
    return out


def apply_initial_search_bound_overrides(system: dict, variables: list[dict], options: dict, thickness_constraints: dict | None=None) -> list[dict]:
    out = copy.deepcopy(variables)
    constraints = _lensde_configuration.normalize_thickness_constraint_fields(thickness_constraints or {})
    air_thickness_min = positive_option(options, 'initial_search_air_thickness_min')
    air_thickness_max = positive_option(options, 'initial_search_air_thickness_max')
    glass_thickness_min = positive_option(options, 'initial_search_glass_thickness_min')
    glass_thickness_max = positive_option(options, 'initial_search_glass_thickness_max')
    if air_thickness_min is None and air_thickness_max is None and (glass_thickness_min is None) and (glass_thickness_max is None):
        return out
    for item in out:
        if str(item.get('kind', '')) != 'thickness':
            continue
        lower = None
        upper = None
        if _lensde_populations.is_glass_thickness_variable(system, item):
            lower = glass_thickness_min
            upper = glass_thickness_max
            if lower is not None:
                lower = max(lower, constraints['glass_center_thickness_lower'])
            if upper is not None:
                upper = min(upper, constraints['glass_center_thickness_upper'])
        elif not _lensde_populations.is_bfl_variable(system, item):
            lower = air_thickness_min
            upper = air_thickness_max
            if lower is not None:
                lower = max(lower, constraints['air_center_thickness_lower'])
            if upper is not None:
                upper = min(upper, constraints['air_center_thickness_upper'])
        if lower is not None or upper is not None:
            set_variable_bounds(item, lower, upper)
    return out


def enforce_thickness_variable_manufacturing_bounds(system: dict, variables: list[dict], thickness_constraints: dict) -> list[dict]:
    """Prevent later generic overrides from crossing evaluator hard bounds."""
    out = copy.deepcopy(variables)
    constraints = _lensde_configuration.normalize_thickness_constraint_fields(thickness_constraints)
    for item in out:
        if str(item.get('kind', '')) != 'thickness':
            continue
        is_glass = _lensde_populations.is_glass_thickness_variable(system, item)
        floor = constraints['glass_center_thickness_lower' if is_glass else 'air_center_thickness_lower']
        ceiling = constraints['glass_center_thickness_upper' if is_glass else 'air_center_thickness_upper']
        current_lower = _lensde_configuration.finite_float(item.get('lower'), floor)
        current_upper = _lensde_configuration.finite_float(item.get('upper'), ceiling)
        lower = max(floor, current_lower)
        upper = min(ceiling, current_upper)
        if upper < lower:
            lower, upper = (floor, ceiling)
        if upper == lower:
            item['lower'] = lower
            item['upper'] = upper
            item['initial'] = lower
        else:
            set_variable_bounds(item, lower, upper)
    return out


def copy_stage_outputs(stage_dir: Path, run_dir: Path) -> None:
    for name in ['layout.svg', 'spot.csv', 'raytrace.csv', 'system.json', 'initial_score.json', 'score.json', 'population.csv', 'population_scores.csv', 'checkpoint_system.json', 'checkpoint_score.json', 'checkpoint_meta.json', 'hybrid_trace.jsonl', 'adaptive_hybrid_trace.jsonl', 'basin_archive.csv', 'basin_archive.json', 'relative_illumination.csv', 'vignetting_audit.json']:
        source = stage_dir / name
        if source.exists():
            shutil.copyfile(source, run_dir / name)


def read_csv_dict_rows(path: Path) -> list[dict]:
    path = native_io_paths.python_path(path)
    if not path.exists():
        return []
    try:
        with path.open(newline='', encoding='utf-8') as handle:
            return list(csv.DictReader(handle))
    except Exception:
        return []


def append_vignetting_audit_args(args: list[str], merit: dict | None) -> None:
    if not _lensde_configuration.vignetting_optimization_enabled(merit):
        return
    args.extend(['--relative-illumination', '--ri-keep-vignetting-factors', '--ri-normalize-on-axis', '--ri-ray-density', '24', '--ri-field-density', '101', '--ri-scan', '+y'])


def build_vignetting_audit(run_dir: Path, merit: dict | None) -> dict | None:
    """Record illumination compliance without vetoing a candidate or a run."""
    if not _lensde_configuration.vignetting_optimization_enabled(merit):
        return None
    target = _lensde_configuration.finite_float((merit or {}).get('minimum_relative_illumination'), 0.0)
    tolerance = max(0.0, _lensde_configuration.finite_float((merit or {}).get('quality_relative_illumination_tol'), 0.01))
    rows = read_csv_dict_rows(run_dir / 'relative_illumination.csv')
    samples: list[dict] = []
    for row in rows:
        hx = _lensde_configuration.finite_float(row.get('field_hx'), math.nan)
        hy = _lensde_configuration.finite_float(row.get('field_hy'), math.nan)
        illumination = _lensde_configuration.finite_float(row.get('relative_illumination'), math.nan)
        if not all((math.isfinite(value) for value in (hx, hy, illumination))):
            continue
        samples.append({'field_index': int(_lensde_configuration.finite_float(row.get('field_index'), len(samples))), 'field_hx': hx, 'field_hy': hy, 'field_radius': math.hypot(hx, hy), 'relative_illumination': illumination})
    if not samples:
        audit = {'enabled': True, 'acceptance_policy': 'audit_only', 'status': 'missing', 'passed': False, 'minimum_relative_illumination': target, 'tolerance': tolerance, 'message': 'High-density relative-illumination output is missing or empty.'}
        _lensde_provenance.write_json(run_dir / 'vignetting_audit.json', audit)
        return audit
    selected_samples = set(range(len(samples)))
    keyed = any(((merit or {}).get(key) is not None for key in (post_asphere_handoff.INDICES, post_asphere_handoff.RADII)))
    if keyed:
        actual_system = _lensde_asphere_contracts.first_existing_json([run_dir / 'system.json', run_dir / 'system_input.json', run_dir / 'input.json'], {})
        if not actual_system.get('fields'):
            audit = {'enabled': True, 'acceptance_policy': 'audit_only', 'status': 'missing', 'passed': False, 'message': 'RI supervision requires the scored system field contract.'}
            _lensde_provenance.write_json(run_dir / 'vignetting_audit.json', audit)
            return audit
        actual_radii = post_asphere_handoff.field_radii(actual_system)
        selected_samples = {min(range(len(samples)), key=lambda j: abs(samples[j]['field_radius'] - actual_radii[i])) for i in post_asphere_handoff.selected_fields(actual_system, merit)}
    for index, sample in enumerate(samples):
        sample['supervised'] = index in selected_samples
        sample['required_relative_illumination'] = target if sample['supervised'] else None
        sample['violation'] = max(0.0, target - sample['relative_illumination']) if sample['supervised'] else 0.0
    off_axis_samples = [sample for sample in samples if sample['field_radius'] > 1e-12 and sample['supervised']]
    worst = max(off_axis_samples or [samples[i] for i in sorted(selected_samples)], key=lambda sample: (sample['violation'], sample['required_relative_illumination'] - sample['relative_illumination']))
    minimum_sample = min(samples, key=lambda sample: sample['relative_illumination'])
    max_violation = worst['violation'] if off_axis_samples else 0.0
    checkpoint_indices = sorted({0, len(samples) // 4, len(samples) // 2, 3 * len(samples) // 4, len(samples) - 1})
    audit = {'enabled': True, 'acceptance_policy': 'audit_only', 'status': 'passed' if max_violation <= tolerance else 'failed', 'passed': max_violation <= tolerance, 'minimum_relative_illumination': target, 'tolerance': tolerance, 'illumination_requirement_policy': 'key_fields_nearest_actual_contract' if keyed else 'global_minimum_only', 'supervised_checkpoints': [samples[i] for i in sorted(selected_samples)], 'all_field_samples': samples, 'ray_density': int(_lensde_configuration.finite_float(rows[0].get('ray_density'), 24)), 'field_density': len(samples), 'minimum_measured_relative_illumination': minimum_sample['relative_illumination'], 'maximum_floor_violation': max_violation, 'worst_sample': worst, 'checkpoints': [samples[index] for index in checkpoint_indices]}
    _lensde_provenance.write_json(run_dir / 'vignetting_audit.json', audit)
    return audit


def copy_candidate_artifacts(source_dir: Path, target_dir: Path) -> list[str]:
    source_dir = native_io_paths.python_path(source_dir)
    target_dir = native_io_paths.python_path(target_dir)
    copied: list[str] = []
    if not source_dir.exists() or not source_dir.is_dir():
        return copied
    target_dir.mkdir(parents=True, exist_ok=True)
    for name in ['input_system.json', 'system_input.json', 'system.json', 'variables.json', 'merit.json', 'glass_search.json', 'initial_score.json', 'score.json', 'checkpoint_system.json', 'checkpoint_score.json', 'checkpoint_system.zmx', 'result.json', 'population.csv', 'population_scores.csv', 'layout.svg', 'spot.csv', 'raytrace.csv', 'command.json', 'command.txt', 'stdout.log', 'stderr.log', 'run.log', 'stage_meta.json', 'source_meta.json', 'summary.json', 'case_input_summary.json', 'multi_island_result.json', 'seed_population.csv', 'promotion_handoff_contract.json', 'vignetting_field_bridge_report.json', 'vignetting_closure_report.json', 'adaptive_trim_report.json', 'exact_apg_certificate.json', 'post_asphere_vignetting_certificate.json', 'terminal_vignetting_report.json', 'stage_boundary_vignetting_report.json', 'relative_illumination.csv', 'vignetting_audit.json']:
        source = source_dir / name
        if not source.exists() and source_dir.name.lower() == 'run':
            source = source_dir.parent / name
        if source.exists():
            shutil.copyfile(source, target_dir / name)
            copied.append(name)
    return copied


def copy_summary_file(source: Path, target: Path) -> str:
    if not source.exists():
        return ''
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    return str(target)


def compact_checkpoint_snapshot_inputs(run_dir: Path, summary_dir: Path) -> list[dict]:
    """Preserve small cadence indices and remove raw checkpoint event streams."""
    candidates = [run_dir / 'funnel_global_explore' / 'checkpoint_candidate_snapshots']
    outer_root = native_io_paths.named_child(run_dir, 'funnel_global_explore_outers')
    if outer_root.is_dir():
        candidates.extend((outer_dir / 'checkpoint_candidate_snapshots' for outer_dir in sorted(outer_root.glob('outer_*')) if outer_dir.is_dir()))
    run_root = run_dir.resolve()
    index_dir = summary_dir / 'layer1_checkpoint_snapshot_indices'
    records: list[dict] = []
    seen: set[Path] = set()
    for snapshot_dir in candidates:
        if not snapshot_dir.is_dir():
            continue
        resolved = snapshot_dir.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        if snapshot_dir.name != 'checkpoint_candidate_snapshots' or not resolved.is_relative_to(run_root):
            records.append({'source_dir': str(snapshot_dir), 'removed': False, 'error': 'snapshot directory failed compact safety check'})
            continue
        files = [{'name': path.name, 'bytes': path.stat().st_size} for path in sorted(snapshot_dir.iterdir()) if path.is_file()]
        label = snapshot_dir.parent.name
        index_source = snapshot_dir / 'snapshot_index.jsonl'
        index_target = index_dir / f'{label}_snapshot_index.jsonl'
        record = {'source_dir': str(snapshot_dir), 'files': files, 'bytes_removed': sum((int(item['bytes']) for item in files)), 'snapshot_index': '', 'removed': False}
        try:
            if index_source.is_file():
                record['snapshot_index'] = copy_summary_file(index_source, index_target)
            shutil.rmtree(snapshot_dir)
            record['removed'] = True
        except Exception as exc:
            record['error'] = str(exc)
        records.append(record)
    return records


def promote_de_dls_material_funnel_outputs(run_dir: Path, dls_dir: Path, *, compact: bool) -> dict:
    key_dir = run_dir / 'de_dls_key_outputs'
    summary_dir = key_dir / 'summaries'
    pre_material_dir = key_dir / 'pre_material_topologies'
    continuous_presnap_dir = key_dir / 'continuous_presnap_topologies'
    final_material_dir = key_dir / 'final_material_topologies'
    fixed_glass_feedback_dir = key_dir / 'fixed_glass_feedback_repairs'
    final_locked_glass_dls_dir = key_dir / 'final_locked_glass_dls_topologies'
    final_selected_dir = key_dir / 'final_selected_result'
    key_dir.mkdir(parents=True, exist_ok=True)
    fixed_glass_feedback_source_dir = native_io_paths.named_child(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel'), 'fixed_glass_feedback_repair')
    final_locked_glass_dls_source_dir = native_io_paths.named_child(dls_dir, 'layer2_final_locked_glass_dls')
    two_layer_config: dict = {}
    material_terminal_mode = 'funnel'
    two_layer_config_path = dls_dir / 'two_layer_funnel_config.json'
    if two_layer_config_path.is_file():
        try:
            two_layer_config = json.loads(two_layer_config_path.read_text(encoding='utf-8'))
            material_terminal_mode = str(two_layer_config.get('material_terminal_mode') or 'funnel').strip().lower()
            configured_feedback_dir = Path(str(two_layer_config.get('fixed_glass_feedback_repair') or ''))
            if configured_feedback_dir.is_dir():
                fixed_glass_feedback_source_dir = configured_feedback_dir
            configured_final_dls_dir = Path(str(two_layer_config.get('final_locked_glass_dls') or ''))
            if configured_final_dls_dir.is_dir():
                final_locked_glass_dls_source_dir = configured_final_dls_dir
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            pass
    copied_summaries = {'two_layer_config': copy_summary_file(dls_dir / 'two_layer_funnel_config.json', summary_dir / 'two_layer_funnel_config.json'), 'checkpoint_selection_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_selection.csv', summary_dir / 'layer1_checkpoint_selection.csv'), 'checkpoint_selection_json': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_selection.json', summary_dir / 'layer1_checkpoint_selection.json'), 'checkpoint_de_config': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_config.json', summary_dir / 'layer1_checkpoint_de_config.json'), 'checkpoint_rank1_references': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'rank1_reference_manifest.json', summary_dir / 'layer1_checkpoint_rank1_reference_manifest.json'), 'checkpoint_run_manifest': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'run_manifest.json', summary_dir / 'layer1_checkpoint_run_manifest.json'), 'checkpoint_de_results_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_results.csv', summary_dir / 'layer1_checkpoint_de_results.csv'), 'checkpoint_de_results_json': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_results.json', summary_dir / 'layer1_checkpoint_de_results.json'), 'checkpoint_de_ranked_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_latest_ranked.csv', summary_dir / 'layer1_checkpoint_de_latest_ranked.csv'), 'checkpoint_de_ranked_json': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_latest_ranked.json', summary_dir / 'layer1_checkpoint_de_latest_ranked.json'), 'checkpoint_de_all_viable_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_all_viable.csv', summary_dir / 'layer1_checkpoint_de_all_viable.csv'), 'checkpoint_de_all_viable_json': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_all_viable.json', summary_dir / 'layer1_checkpoint_de_all_viable.json'), 'checkpoint_handoff_selection_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_handoff_selection.csv', summary_dir / 'layer1_checkpoint_handoff_selection.csv'), 'checkpoint_handoff_selection_json': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_handoff_selection.json', summary_dir / 'layer1_checkpoint_handoff_selection.json'), 'checkpoint_de_s400_preview_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_s400_preview.csv', summary_dir / 'layer1_checkpoint_de_s400_preview.csv'), 'checkpoint_de_s400_preview_json': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_s400_preview.json', summary_dir / 'layer1_checkpoint_de_s400_preview.json'), 'dls_potential_summary': copy_summary_file(dls_dir / 'layer1_dls_potential_sweep' / 'dls_potential_summary.csv', summary_dir / 'layer1_dls_potential_summary.csv'), 'selected_candidates_csv': copy_summary_file(dls_dir / 'layer1_selected_candidates' / 'selected_candidates.csv', summary_dir / 'layer1_selected_candidates.csv'), 'selected_candidates_json': copy_summary_file(dls_dir / 'layer1_selected_candidates' / 'selected_candidates.json', summary_dir / 'layer1_selected_candidates.json'), 'geometry_restart_ranked': copy_summary_file(dls_dir / 'layer1_deep_geometry_restart' / 'restart_latest_ranked.csv', summary_dir / 'layer1_geometry_restart_latest_ranked.csv'), 'material_entry_candidates_csv': copy_summary_file(dls_dir / 'layer2_material_entry_selection' / 'material_entry_candidates.csv', summary_dir / 'layer2_material_entry_candidates.csv'), 'material_entry_candidates_json': copy_summary_file(dls_dir / 'layer2_material_entry_selection' / 'material_entry_candidates.json', summary_dir / 'layer2_material_entry_candidates.json'), 'material_bridge_report': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material5_bridge') / 'bridge_report.json', summary_dir / 'layer2_material5_bridge_report.json'), 'material_bridge_diagnostics': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material5_bridge') / 'bridge_candidate_diagnostics.csv', summary_dir / 'layer2_material5_bridge_diagnostics.csv'), 'material_bridge_repairs': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material5_bridge') / 'repair_probe_summary.csv', summary_dir / 'layer2_material5_bridge_repairs.csv'), 'material_bridge_deepen': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material5_bridge') / 'deepen_summary.csv', summary_dir / 'layer2_material5_bridge_deepen.csv'), 'glass_choice_scout_report': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_glass_choice_scout') / 'glass_scout_report.json', summary_dir / 'layer2_glass_choice_scout_report.json'), 'glass_choice_scout_ranked': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_glass_choice_scout') / 'glass_scout_ranked.csv', summary_dir / 'layer2_glass_choice_scout_ranked.csv'), 'glass_choice_scout_broker_stats': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_glass_choice_scout') / 'broker_stats.json', summary_dir / 'layer2_glass_choice_scout_broker_stats.json'), 'continuous_presnap_candidates_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_presnap_candidates.csv', summary_dir / 'layer2_continuous_presnap_candidates.csv'), 'continuous_presnap_ranked_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_presnap_ranked.csv', summary_dir / 'layer2_continuous_presnap_ranked.csv'), 'continuous_presnap_candidates_json': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_presnap_candidates.json', summary_dir / 'layer2_continuous_presnap_candidates.json'), 'continuous_dls_scout_handoff_csv': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_dls_scout_handoff.csv', summary_dir / 'layer2_continuous_dls_scout_handoff.csv'), 'continuous_dls_scout_handoff_json': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_dls_scout_handoff.json', summary_dir / 'layer2_continuous_dls_scout_handoff.json'), 'material_restart_early_stop': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'restart_early_stop.json', summary_dir / 'layer2_material_restart_early_stop.json'), 'final_material_ranked': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_presnap_ranked.csv' if material_terminal_mode == 'scout_lock_fixed_de' else dls_dir / 'layer2_material_entry_selection' / 'material_entry_candidates.csv' if material_terminal_mode == 'direct_glass_de' else native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'funnel_final_ranked.csv', summary_dir / 'layer2_final_material_ranked.csv'), 'final_material_summary': copy_summary_file(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'funnel_summary.json', summary_dir / 'layer2_final_material_summary.json'), 'fixed_glass_feedback_config': copy_summary_file(fixed_glass_feedback_source_dir / 'fixed_glass_feedback_config.json', summary_dir / 'layer2_fixed_glass_feedback_config.json'), 'fixed_glass_feedback_multi_island_policy': copy_summary_file(fixed_glass_feedback_source_dir / 'fixed_glass_feedback_multi_island_policy.json', summary_dir / 'layer2_fixed_glass_feedback_multi_island_policy.json'), 'fixed_glass_feedback_manifest': copy_summary_file(fixed_glass_feedback_source_dir / 'manifest.json', summary_dir / 'layer2_fixed_glass_feedback_manifest.json'), 'fixed_glass_feedback_ranked': copy_summary_file(fixed_glass_feedback_source_dir / 'fixed_glass_feedback_ranked.csv', summary_dir / 'layer2_fixed_glass_feedback_ranked.csv'), 'fixed_glass_feedback_cycle_summary': copy_summary_file(fixed_glass_feedback_source_dir / 'cycle_summary.csv', summary_dir / 'layer2_fixed_glass_feedback_cycle_summary.csv'), 'final_locked_glass_dls_config': copy_summary_file(final_locked_glass_dls_source_dir / 'final_locked_glass_dls_config.json', summary_dir / 'layer2_final_locked_glass_dls_config.json'), 'final_locked_glass_dls_ranked_csv': copy_summary_file(final_locked_glass_dls_source_dir / 'final_locked_glass_dls_ranked.csv', summary_dir / 'layer2_final_locked_glass_dls_ranked.csv'), 'final_locked_glass_dls_ranked_json': copy_summary_file(final_locked_glass_dls_source_dir / 'final_locked_glass_dls_ranked.json', summary_dir / 'layer2_final_locked_glass_dls_ranked.json')}
    pre_rows = read_csv_dict_rows(dls_dir / 'layer2_material_entry_selection' / 'material_entry_candidates.csv')
    copied_pre_material: list[dict] = []
    for index, row in enumerate(pre_rows, start=1):
        source_dir = Path(str(row.get('out_dir') or ''))
        if not source_dir.exists():
            continue
        rank = int(_lensde_configuration.finite_float(row.get('basin_rank'), index))
        target = native_io_paths.named_child(pre_material_dir, f'cand_{index:03d}_rank_{rank:04d}')
        copied = copy_candidate_artifacts(source_dir, target)
        copied_pre_material.append({'index': index, 'basin_rank': row.get('basin_rank'), 'source_dir': str(source_dir), 'target_dir': str(target), 'copied_files': copied, 'final_merit': row.get('final_merit')})
    continuous_rows = read_csv_dict_rows(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_presnap_ranked.csv')
    if not continuous_rows:
        continuous_rows = read_csv_dict_rows(native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_presnap_candidates.csv')
    copied_continuous_presnap: list[dict] = []
    for index, row in enumerate(continuous_rows, start=1):
        source_text = str(row.get('source_dir') or row.get('continuous_stage_out_dir') or row.get('out_dir') or '').strip()
        if not source_text:
            continue
        source_dir = Path(source_text)
        if not source_dir.exists():
            continue
        rank = int(_lensde_configuration.finite_float(row.get('basin_rank'), index))
        target = native_io_paths.named_child(continuous_presnap_dir, f'cand_{index:03d}_rank_{rank:04d}')
        copied = copy_candidate_artifacts(source_dir, target)
        copied_continuous_presnap.append({'index': index, 'basin_rank': row.get('basin_rank'), 'source_dir': str(source_dir), 'target_dir': str(target), 'copied_files': copied, 'continuous_final_merit': row.get('continuous_final_merit') or row.get('final_merit'), 'continuous_stage': row.get('continuous_stage') or row.get('stage'), 'checkpoint_reason': row.get('checkpoint_reason')})
    final_csv = native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'continuous_presnap_ranked.csv' if material_terminal_mode == 'scout_lock_fixed_de' else dls_dir / 'layer2_material_entry_selection' / 'material_entry_candidates.csv' if material_terminal_mode == 'direct_glass_de' else native_io_paths.named_child(dls_dir, 'layer2_material_glass_funnel') / 'funnel_final_ranked.csv'
    if not final_csv.exists():
        final_csv = native_io_paths.named_child(dls_dir, 'layer1_checkpoint_de400') / 'checkpoint_de_latest_ranked.csv'
    if not final_csv.exists():
        final_csv = dls_dir / 'layer1_deep_geometry_restart' / 'restart_latest_ranked.csv'
    final_rows = read_csv_dict_rows(final_csv)
    copied_final_material: list[dict] = []
    copied_fixed_glass_feedback: list[dict] = []
    skipped_fixed_glass_feedback: list[dict] = []
    copied_final_locked_glass_dls: list[dict] = []
    promoted_final: dict = {}
    selected_final_result: dict = {}
    promoted_final_mean_spot_um = math.inf
    promoted_final_merit = math.inf
    promoted_final_source: Path | None = None

    def csv_bool(value: object) -> bool:
        return str(value).strip().lower() in {'1', 'true', 'yes', 'y'}

    def row_mean_spot_um(row: dict, *keys: str) -> float:
        for key in keys:
            value = _lensde_configuration.finite_float(row.get(key), math.nan)
            if not math.isfinite(value):
                continue
            return 1000.0 * value if value < 1.0 else value
        return math.inf

    def row_merit(row: dict, *keys: str) -> float:
        for key in keys:
            value = _lensde_configuration.finite_float(row.get(key), math.nan)
            if math.isfinite(value):
                return value
        return math.inf

    def source_score_admissible(source_dir: Path) -> tuple[bool, list[str], dict]:
        """Validate the exact artifact that would be copied into the run root."""
        score_path = source_dir / 'score.json'
        if not score_path.is_file():
            return (False, ['missing_score_json'], {})
        try:
            score = _lensde_provenance.read_json(score_path)
        except (OSError, ValueError, TypeError, json.JSONDecodeError) as exc:
            return (False, [f'invalid_score_json:{exc}'], {})
        reasons: list[str] = []
        merit = _lensde_configuration.finite_float(score.get('merit'), math.inf)
        if not math.isfinite(merit) or merit <= 0.0 or merit >= 1000000000000.0:
            reasons.append('failed_or_nonfinite_merit')
        if not bool(score.get('trace_ok', False)):
            reasons.append('trace_not_ok')
        if _lensde_configuration.finite_float(score.get('trace_coverage'), 0.0) < state.DE_DLS_TRACE_COVERAGE_BASELINE or _lensde_configuration.finite_float(score.get('min_field_coverage'), 0.0) < state.DE_DLS_TRACE_COVERAGE_BASELINE:
            reasons.append('trace_coverage_below_contract')
        if not vignetting_handoff_policy.chief_ray_usable(score):
            reasons.append('chief_ray_below_contract')
        if _lensde_configuration.finite_float(score.get('min_edge'), -math.inf) < 0.0:
            reasons.append('negative_or_nonfinite_edge_thickness')
        return (not reasons, reasons, score)

    def fixed_glass_feedback_row_admissible(row: dict) -> tuple[bool, list[str]]:
        """Honor the producer admission contract and safely read legacy rows."""
        reasons: list[str] = []
        admission_text = str(row.get('admission_ok') or '').strip()
        if admission_text:
            if not csv_bool(admission_text):
                reasons.append(str(row.get('admission_reasons') or 'producer_rejected'))
        if int(_lensde_configuration.finite_float(row.get('returncode'), -1)) != 0:
            reasons.append('nonzero_returncode')
        if csv_bool(row.get('timed_out')):
            reasons.append('timed_out')
        merit = row_merit(row, 'merit')
        if not math.isfinite(merit) or merit >= 1000000000000.0:
            reasons.append('failed_or_nonfinite_merit')
        min_edge_text = str(row.get('min_edge') or '').strip()
        if min_edge_text:
            min_edge = _lensde_configuration.finite_float(min_edge_text, -math.inf)
            if not math.isfinite(min_edge) or min_edge < 0.0:
                reasons.append('negative_or_nonfinite_edge_thickness')
        trace_text = str(row.get('trace_coverage') or '').strip()
        field_text = str(row.get('min_field_coverage') or '').strip()
        trace_ok_text = str(row.get('trace_ok') or '').strip()
        if trace_ok_text and (not csv_bool(trace_ok_text)):
            reasons.append('trace_not_ok')
        if trace_text and _lensde_configuration.finite_float(trace_text, 0.0) < state.DE_DLS_TRACE_COVERAGE_BASELINE or (field_text and _lensde_configuration.finite_float(field_text, 0.0) < state.DE_DLS_TRACE_COVERAGE_BASELINE):
            reasons.append('trace_coverage_below_contract')
        constraint_text = str(row.get('constraint_group_error') or '').strip()
        if constraint_text and (not math.isfinite(_lensde_configuration.finite_float(constraint_text, math.inf))):
            reasons.append('nonfinite_constraint_error')
        return (not reasons, reasons)
    for index, row in enumerate(final_rows, start=1):
        source_text = str(row.get('out_dir') or '').strip()
        if not source_text:
            continue
        source_dir = Path(source_text)
        if not source_dir.exists():
            continue
        rank = int(_lensde_configuration.finite_float(row.get('basin_rank'), index))
        target = native_io_paths.named_child(final_material_dir, f'cand_{index:03d}_rank_{rank:04d}')
        copied = copy_candidate_artifacts(source_dir, target)
        item = {'index': index, 'basin_rank': row.get('basin_rank'), 'source_dir': str(source_dir), 'target_dir': str(target), 'copied_files': copied, 'final_merit': row.get('final_merit'), 'final_mean_spot_um': row_mean_spot_um(row, 'final_mean_spot', 'mean_spot_um')}
        copied_final_material.append(item)
        admissible, rejection_reasons, source_score = source_score_admissible(source_dir)
        item['materialization_valid'] = admissible
        item['materialization_rejection_reasons'] = rejection_reasons
        item['cpu_materialized_merit'] = source_score.get('merit')
        if promoted_final_source is None and admissible:
            copy_stage_outputs(source_dir, run_dir)
            if (source_dir / 'merit.json').exists():
                shutil.copyfile(source_dir / 'merit.json', run_dir / 'merit.json')
            if not (run_dir / 'system.json').exists() and (source_dir / 'input_system.json').exists():
                shutil.copyfile(source_dir / 'input_system.json', run_dir / 'system.json')
            promoted_final = item
            promoted_final_mean_spot_um = row_mean_spot_um(row, 'final_mean_spot', 'mean_spot_um')
            promoted_final_merit = row_merit(row, 'final_merit', 'merit')
            promoted_final_source = source_dir
    feedback_csv = fixed_glass_feedback_source_dir / 'fixed_glass_feedback_ranked.csv'
    if not feedback_csv.exists():
        feedback_csv = fixed_glass_feedback_source_dir / 'cycle_summary.csv'
    feedback_rows = read_csv_dict_rows(feedback_csv)
    promoted_feedback_item: dict = {}
    promoted_feedback_source: Path | None = None
    for index, row in enumerate(feedback_rows, start=1):
        admissible, rejection_reasons = fixed_glass_feedback_row_admissible(row)
        if not admissible:
            skipped_fixed_glass_feedback.append({'source_row': index, 'out_dir': row.get('out_dir'), 'merit': row.get('merit'), 'min_edge': row.get('min_edge'), 'reasons': rejection_reasons})
            continue
        source_text = str(row.get('out_dir') or '').strip()
        if not source_text:
            continue
        source_dir = Path(source_text)
        if not source_dir.exists():
            continue
        source_admissible, source_reasons, source_score = source_score_admissible(source_dir)
        if not source_admissible:
            skipped_fixed_glass_feedback.append({'source_row': index, 'out_dir': str(source_dir), 'merit': row.get('merit'), 'min_edge': row.get('min_edge'), 'reasons': source_reasons})
            continue
        rank = int(_lensde_configuration.finite_float(row.get('rank'), index))
        target = fixed_glass_feedback_dir / f'repair_{index:03d}_rank_{rank:04d}'
        copied = copy_candidate_artifacts(source_dir, target)
        mean_spot_um = row_mean_spot_um(row, 'mean_spot_um')
        merit = row_merit(row, 'merit')
        item = {'index': index, 'rank': row.get('rank'), 'cycle': row.get('cycle'), 'source_dir': str(source_dir), 'parent_source_dir': row.get('source_dir'), 'target_dir': str(target), 'copied_files': copied, 'material_mode': row.get('material_mode'), 'algorithm': row.get('algorithm'), 'merit': row.get('merit'), 'mean_spot_um': row.get('mean_spot_um'), 'max_spot_um': row.get('max_spot_um'), 'max_distortion': row.get('max_distortion'), 'constraint_group_error': row.get('constraint_group_error'), 'min_edge': row.get('min_edge'), 'trace_coverage': row.get('trace_coverage'), 'min_field_coverage': row.get('min_field_coverage'), 'cpu_materialized_merit': source_score.get('merit'), 'materialization_valid': True}
        copied_fixed_glass_feedback.append(item)
        if promoted_feedback_item:
            continue
        if int(_lensde_configuration.finite_float(row.get('returncode'), -1)) != 0 or csv_bool(row.get('timed_out')):
            continue
        if material_terminal_mode in {'direct_glass_de', 'scout_lock_fixed_de'} or merit < promoted_final_merit:
            promoted_feedback_item = {**item, 'promoted_from': material_terminal_mode if material_terminal_mode in {'direct_glass_de', 'scout_lock_fixed_de'} else 'fixed_glass_feedback_repair', 'replaced_final_mean_spot_um': promoted_final_mean_spot_um, 'replaced_final_merit': promoted_final_merit}
            promoted_feedback_source = source_dir
    if promoted_feedback_item and promoted_feedback_source is not None:
        copy_stage_outputs(promoted_feedback_source, run_dir)
        for merit_source in [promoted_feedback_source / 'merit.json', promoted_feedback_source.parent / 'merit.json']:
            if merit_source.exists():
                shutil.copyfile(merit_source, run_dir / 'merit.json')
                break
        if not (run_dir / 'system.json').exists():
            for system_source in [promoted_feedback_source / 'input_system.json', promoted_feedback_source / 'system_input.json', promoted_feedback_source.parent / 'system_input.json']:
                if system_source.exists():
                    shutil.copyfile(system_source, run_dir / 'system.json')
                    break
        promoted_final = promoted_feedback_item
        promoted_final_source = promoted_feedback_source
    final_dls_rows = read_csv_dict_rows(final_locked_glass_dls_source_dir / 'final_locked_glass_dls_ranked.csv')
    promoted_final_dls_item: dict = {}
    promoted_final_dls_source: Path | None = None
    for index, row in enumerate(final_dls_rows, start=1):
        source_text = str(row.get('out_dir') or '').strip()
        if not source_text:
            continue
        source_dir = Path(source_text)
        if not source_dir.exists():
            continue
        source_admissible, source_reasons, source_score = source_score_admissible(source_dir)
        if not source_admissible:
            copied_final_locked_glass_dls.append({'index': index, 'source_dir': str(source_dir), 'materialization_valid': False, 'materialization_rejection_reasons': source_reasons})
            continue
        rank = int(_lensde_configuration.finite_float(row.get('rank'), index))
        target = final_locked_glass_dls_dir / f'tail_{index:03d}_rank_{rank:04d}'
        copied = copy_candidate_artifacts(source_dir, target)
        item = {'index': index, 'rank': row.get('rank'), 'source_rank': row.get('source_rank'), 'source_dir': str(source_dir), 'parent_source_dir': row.get('source_dir'), 'target_dir': str(target), 'copied_files': copied, 'algorithm': row.get('algorithm'), 'selected_from': row.get('selected_from'), 'parent_retained': row.get('parent_retained'), 'merit': row.get('merit'), 'mean_spot_um': row.get('mean_spot_um'), 'max_spot_um': row.get('max_spot_um'), 'trace_coverage': row.get('trace_coverage'), 'min_field_coverage': row.get('min_field_coverage'), 'cpu_materialized_merit': source_score.get('merit'), 'materialization_valid': True}
        copied_final_locked_glass_dls.append(item)
        if promoted_final_dls_item:
            continue
        if int(_lensde_configuration.finite_float(row.get('returncode'), -1)) != 0 or csv_bool(row.get('timed_out')):
            continue
        promoted_final_dls_item = {**item, 'promoted_from': 'final_locked_glass_dls', 'replaced_final_mean_spot_um': row_mean_spot_um(promoted_final, 'mean_spot_um', 'final_mean_spot_um'), 'replaced_final_merit': row_merit(promoted_final, 'merit', 'final_merit')}
        promoted_final_dls_source = source_dir
    if promoted_final_dls_item and promoted_final_dls_source is not None:
        copy_stage_outputs(promoted_final_dls_source, run_dir)
        for merit_source in [promoted_final_dls_source / 'merit.json', promoted_final_dls_source.parent / 'merit.json']:
            if merit_source.exists():
                shutil.copyfile(merit_source, run_dir / 'merit.json')
                break
        promoted_final = promoted_final_dls_item
        promoted_final_source = promoted_final_dls_source
    if promoted_final_source is not None:
        copied = copy_candidate_artifacts(promoted_final_source, final_selected_dir)
        selected_final_result = {**promoted_final, 'selected_result_dir': str(final_selected_dir), 'selected_result_files': copied}
        _lensde_provenance.write_json(key_dir / 'final_selected_result.json', selected_final_result)
        _lensde_provenance.write_json(run_dir / 'final_selected_result.json', selected_final_result)
    compacted_checkpoint_snapshots = compact_checkpoint_snapshot_inputs(run_dir, summary_dir) if compact else []
    if compacted_checkpoint_snapshots:
        cleanup_path = summary_dir / 'layer1_checkpoint_snapshot_cleanup.json'
        _lensde_provenance.write_json(cleanup_path, compacted_checkpoint_snapshots)
        copied_summaries['checkpoint_snapshot_cleanup'] = str(cleanup_path)
    summary = {'key_outputs_dir': str(key_dir), 'summaries': copied_summaries, 'pre_material_topologies': copied_pre_material, 'continuous_presnap_topologies': copied_continuous_presnap, 'final_material_topologies': copied_final_material, 'fixed_glass_feedback_topologies': copied_fixed_glass_feedback, 'skipped_fixed_glass_feedback_topologies': skipped_fixed_glass_feedback, 'fixed_glass_feedback_source_dir': str(fixed_glass_feedback_source_dir), 'final_locked_glass_dls_topologies': copied_final_locked_glass_dls, 'final_locked_glass_dls_source_dir': str(final_locked_glass_dls_source_dir), 'promoted_final': promoted_final, 'selected_final_result': selected_final_result, 'compacted_checkpoint_snapshots': compacted_checkpoint_snapshots, 'source_dls_dir': str(dls_dir), 'compact_outputs': bool(compact), 'material_terminal_mode': material_terminal_mode}
    _lensde_provenance.write_json(key_dir / 'key_outputs_summary.json', summary)
    _lensde_provenance.write_json(run_dir / 'de_dls_material_funnel_summary.json', summary)
    if compact and dls_dir.exists():
        try:
            if dls_dir.resolve().parent == run_dir.resolve():
                shutil.rmtree(dls_dir)
                summary['source_dls_dir_removed'] = True
                _lensde_provenance.write_json(key_dir / 'key_outputs_summary.json', summary)
                _lensde_provenance.write_json(run_dir / 'de_dls_material_funnel_summary.json', summary)
        except Exception as exc:
            summary['compact_error'] = str(exc)
            _lensde_provenance.write_json(key_dir / 'key_outputs_summary.json', summary)
            _lensde_provenance.write_json(run_dir / 'de_dls_material_funnel_summary.json', summary)
    return summary


def option_truthy(value: object, default: bool=False) -> bool:
    if value is None or value == '':
        return default
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {'1', 'true', 'yes', 'y', 'on'}


def ensure_server_source_is_current() -> None:
    from lensde.runtime.source_guard import ensure_current
    ensure_current()


def option_int_from(options: dict, key: str, default: int, minimum: int=0) -> int:
    try:
        value = options.get(key, default)
        if value is None or value == '':
            value = default
        return max(minimum, int(float(value)))
    except (TypeError, ValueError):
        return max(minimum, int(default))


def option_float_from(options: dict, key: str, default: float, minimum: float=0.0) -> float:
    try:
        value = options.get(key, default)
        if value is None or value == '':
            value = default
        return max(minimum, float(value))
    except (TypeError, ValueError):
        return max(minimum, float(default))


def parse_one_based_index_list(value: object) -> list[int]:
    if isinstance(value, (list, tuple, set)):
        out: list[int] = []
        seen: set[int] = set()
        for raw in value:
            try:
                item = int(raw)
            except (TypeError, ValueError):
                continue
            if item > 0 and item not in seen:
                out.append(item)
                seen.add(item)
        return out
    text = str(value or '').replace(';', ',').replace(' ', ',')
    out: list[int] = []
    seen: set[int] = set()
    for raw in text.split(','):
        token = raw.strip()
        if not token:
            continue
        if '-' in token:
            left, right = token.split('-', 1)
            try:
                start = int(left.strip())
                stop = int(right.strip())
            except ValueError:
                continue
            if stop < start:
                start, stop = (stop, start)
            values = range(start, stop + 1)
        else:
            try:
                values = [int(token)]
            except ValueError:
                continue
        for item in values:
            if item <= 0 or item in seen:
                continue
            out.append(item)
            seen.add(item)
    return out


def canonical_initial_search_asphere_max_order(value: object) -> int:
    try:
        order = int(float(value))
    except (TypeError, ValueError):
        order = 16
    return order if order in state.ASPHERE_SUPPORTED_MAX_ORDERS else 16
