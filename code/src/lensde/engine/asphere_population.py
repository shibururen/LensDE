"""Extracted paper engine: asphere_population."""
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

def make_post_asphere_top_half_periodic_refresh_population(population_rows: list[list[float]], variables: list[dict], score_records: list[dict], *, parent_count: int, sag_um: float, correlation_length: float, seed: int) -> tuple[list[list[float]], dict, list[dict]]:
    """Refresh the bottom half from random parents in the untouched top half.

    Each child receives exactly one correlated A4..Amax sag move on one random
    complete active-order surface.  Candidate traceability is intentionally neither
    evaluated nor used as an acceptance gate here; the normal DE feasibility
    rules remain responsible for subsequent selection.
    """
    rows = [list(row) for row in population_rows]
    population = len(rows)
    if population <= 1:
        raise RuntimeError('periodic top-half refresh requires at least two rows')
    if not variables or any((len(row) != len(variables) for row in rows)):
        raise RuntimeError('periodic top-half refresh population violates P/D contract')
    ranked: list[tuple[int, int, float]] = []
    seen: set[int] = set()
    for record in score_records:
        index = int(_lensde_configuration.finite_float(record.get('index'), -1.0))
        if index < 0 or index >= population or index in seen:
            continue
        rank = int(_lensde_configuration.finite_float(record.get('rank'), population + 1.0))
        merit_value = _lensde_configuration.finite_float(record.get('merit'), math.inf)
        ranked.append((index, rank, merit_value))
        seen.add(index)
    if len(ranked) != population:
        raise RuntimeError('formation population_scores.csv does not rank every terminal row')
    ranked.sort(key=lambda item: (item[1], item[2], item[0]))
    protected_count = (population + 1) // 2
    protected_indices = [item[0] for item in ranked[:protected_count]]
    replaced_indices = [item[0] for item in ranked[protected_count:]]
    requested_parents = max(1, int(parent_count))
    selected_parent_count = min(requested_parents, len(protected_indices))
    rng = random.Random(int(seed))
    selected_parents = rng.sample(protected_indices, selected_parent_count)
    groups = _lensde_asphere_contracts.post_asphere_complete_coefficient_groups(variables)
    if not groups:
        raise RuntimeError('periodic top-half refresh requires contiguous active A4..Amax coefficient groups with valid reference radii')
    rank_by_index = {index: rank for index, rank, _merit in ranked}
    parent_usage: dict[int, int] = {index: 0 for index in selected_parents}
    surface_usage: dict[int, int] = {int(group['surface']): 0 for group in groups}
    lineage: list[dict] = []
    for child_index in replaced_indices:
        parent_candidates = list(selected_parents)
        group_candidates = list(groups)
        rng.shuffle(parent_candidates)
        rng.shuffle(group_candidates)
        moved = None
        parent_index = parent_candidates[0]
        group = group_candidates[0]
        for candidate_parent in parent_candidates:
            for candidate_group in group_candidates:
                candidate_move = _lensde_asphere_contracts.post_asphere_correlated_sag_move(rows[candidate_parent], variables, candidate_group, float(sag_um), float(correlation_length), rng)
                if candidate_move is not None:
                    parent_index = candidate_parent
                    group = candidate_group
                    moved = candidate_move
                    break
            if moved is not None:
                break
        if moved is None:
            raise RuntimeError('periodic top-half refresh could not make a correlated sag child')
        child, move_info = moved
        rows[child_index] = child
        surface = int(group['surface'])
        parent_usage[parent_index] += 1
        surface_usage[surface] = surface_usage.get(surface, 0) + 1
        lineage.append({'child_index': child_index, 'replaced_rank': rank_by_index[child_index], 'parent_index': parent_index, 'parent_rank': rank_by_index[parent_index], 'surface': surface, 'target_cap_um': float(sag_um), 'actual_peak_um': _lensde_configuration.finite_float(move_info.get('actual_peak_um'), 0.0), 'bound_scale': _lensde_configuration.finite_float(move_info.get('bound_scale'), 0.0)})
    actual_peaks = [float(item['actual_peak_um']) for item in lineage]
    summary = {'generator': 'top_half_single_surface_correlated_sag_pre_refresh', 'population': population, 'protected_top_half_count': protected_count, 'replaced_bottom_half_count': len(replaced_indices), 'protected_slots_unchanged': True, 'selected_parent_count': selected_parent_count, 'selected_parents': [{'index': index, 'rank': rank_by_index[index], 'child_count': parent_usage[index]} for index in selected_parents], 'surface_child_counts': {str(surface): count for surface, count in sorted(surface_usage.items())}, 'target_sag_um': float(sag_um), 'correlation_length': float(correlation_length), 'actual_peak_um_min': min(actual_peaks) if actual_peaks else None, 'actual_peak_um_median': statistics.median(actual_peaks) if actual_peaks else None, 'actual_peak_um_max': max(actual_peaks) if actual_peaks else None, 'geometry_perturbation': False, 'traceability_evaluated': False, 'trace_feasible_gate': False, 'seed': int(seed)}
    return (rows, summary, lineage)


def make_post_asphere_mixed_restart_population(system: dict, variables: list[dict], archive_dirs: list[Path], population: int, seed: int, donor_fraction: float, options: dict) -> tuple[list[list[float]], dict]:
    """Production Restart population selected by the G1200 perturbation probe.

    ``donor_fraction`` remains in the signature for call-site compatibility;
    the mixed scheme's quality-diverse allocation supersedes the old fixed 5%
    ranked-donor slot.
    """
    del donor_fraction
    population = max(1, int(population))
    anchor = [_lensde_populations.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in variables]
    groups = _lensde_asphere_contracts.post_asphere_complete_coefficient_groups(variables)
    if not groups:
        raise RuntimeError('mixed post-asphere restart needs complete active A4..Amax groups')
    ranked: list[tuple[float, list[float]]] = []
    for stage_dir in archive_dirs:
        ranked.extend(_lensde_asphere_contracts.post_asphere_ranked_population_rows(stage_dir, variables))
    ranked.sort(key=lambda item: item[0])
    available = population - 1
    carry_target = int(math.floor(available * 0.2))
    single_target = int(math.ceil(available * 0.35))
    coupled_target = int(math.floor(available * 0.3)) if len(groups) >= 2 else 0
    direction_target = max(0, available - carry_target - single_target - coupled_target)
    carries = _lensde_asphere_contracts.post_asphere_restart_quality_diverse_rows(ranked, anchor, variables, carry_target)
    single_target += carry_target - len(carries)
    if len(ranked) < 2:
        single_target += direction_target
        direction_target = 0
    caps = _lensde_asphere_contracts.post_asphere_sag_height_caps_um(options)
    correlation_length = _lensde_commands.option_float_from(options, 'initial_search_post_asphere_sag_correlation_length', 0.35, 0.001)
    rng = random.Random(seed + 20000033)
    rows = [anchor, *carries]
    seen = {tuple((float(value).hex() for value in row)) for row in rows}
    single_rows, single_infos = _lensde_asphere_contracts.post_asphere_restart_single_sag_rows(anchor, variables, groups, caps, correlation_length, single_target, rng, seen)
    rows.extend(single_rows)
    coupled_rows, coupled_infos = _lensde_asphere_contracts.post_asphere_restart_coupled_sag_rows(anchor, variables, groups, caps, correlation_length, coupled_target, rng, seen)
    rows.extend(coupled_rows)
    source_rows: list[list[float]] = []
    source_seen: set[tuple[str, ...]] = set()
    for _merit, row in ranked:
        key = tuple((float(value).hex() for value in row))
        if key in source_seen:
            continue
        source_seen.add(key)
        source_rows.append(row)
        if len(source_rows) >= 360:
            break
    direction_rows, direction_infos = _lensde_asphere_contracts.post_asphere_restart_direction_rows(anchor, variables, source_rows, direction_target, rng, seen) if direction_target > 0 else ([], [])
    rows.extend(direction_rows)
    if len(rows) != population:
        raise AssertionError(f'mixed restart expected {population} rows, got {len(rows)}')
    distances = sorted((_lensde_asphere_contracts.post_asphere_restart_raw_bound_distance(row, anchor, variables) for row in rows))
    distance_at = lambda fraction: distances[min(len(distances) - 1, max(0, int(round(fraction * (len(distances) - 1)))))]
    return (rows, {'generator': 'mixed_quality_structure_restart_v1', 'experiment_basis': 'fixed10_P605_G1200_mixed_winner_20260806', 'anchor_rows': 1, 'archive_stage_count': len(archive_dirs), 'quality_diverse_rows': len(carries), 'single_surface_rows': len(single_rows), 'coupled_surface_rows': len(coupled_rows), 'terminal_difference_rows': len(direction_rows), 'height_caps_um': caps, 'correlation_length': correlation_length, 'coupled_surface_count_distribution': dict(Counter((str(info['surface_count']) for info in coupled_infos))), 'single_surface_counts': dict(Counter((str(info['surface']) for info in single_infos))), 'direction_alpha_counts': dict(Counter((str(info['alpha']) for info in direction_infos))), 'bound_normalized_rms_to_anchor': {'min': distances[0], 'median': distance_at(0.5), 'p90': distance_at(0.9), 'max': distances[-1]}})


def make_post_asphere_long_population(system: dict, variables: list[dict], archive_dirs: list[Path], population: int, seed: int, donor_fraction: float, scales: list[float]) -> tuple[list[list[float]], dict]:
    anchor = [_lensde_populations.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)) for variable in variables]
    donor_limit = min(max(0, population - 1), max(0, int(math.floor(population * max(0.0, donor_fraction)))))
    ranked_sources = [_lensde_asphere_contracts.post_asphere_ranked_population_rows(stage_dir, variables) for stage_dir in archive_dirs]
    donors: list[list[float]] = []
    seen = {tuple((float(value).hex() for value in anchor))}
    depth = 0
    while len(donors) < donor_limit and any((depth < len(rows) for rows in ranked_sources)):
        for rows in ranked_sources:
            if depth >= len(rows):
                continue
            row = rows[depth][1]
            key = tuple((float(value).hex() for value in row))
            if key in seen:
                continue
            seen.add(key)
            donors.append(row)
            if len(donors) >= donor_limit:
                break
        depth += 1
    scales = scales or [0.0125, 0.025, 0.05]
    fresh_count = max(0, population - 1 - len(donors))
    base_count, remainder = divmod(fresh_count, len(scales))
    fresh_rows: list[list[float]] = []
    scale_counts: dict[str, int] = {}
    for scale_index, scale in enumerate(scales):
        count = base_count + (1 if scale_index < remainder else 0)
        scale_counts[f'{scale:.17g}'] = count
        if count <= 0:
            continue
        shell = _lensde_initialization.make_post_asphere_population(system, variables, anchor, count + 1, seed + scale_index * 100003, scale)
        fresh_rows.extend(shell[1:count + 1])
    rows = [anchor, *donors, *fresh_rows]
    if len(rows) != population:
        raise AssertionError(f'long reseed expected {population} rows, got {len(rows)}')
    return (rows, {'anchor_rows': 1, 'donor_rows': len(donors), 'donor_limit': donor_limit, 'archive_stage_count': len(archive_dirs), 'fresh_rows': len(fresh_rows), 'fresh_scale_counts': scale_counts})


def write_post_asphere_summary(work_dir: Path, canonical_dir: Path, summary: dict) -> None:
    _lensde_provenance.write_json(work_dir / 'summary.json', summary)
    if work_dir.resolve() != canonical_dir.resolve():
        canonical_dir.mkdir(parents=True, exist_ok=True)
        _lensde_provenance.write_json(canonical_dir / 'summary.json', summary)
        _lensde_provenance.write_json(canonical_dir / 'work_dir_redirect.json', {'work_dir': str(work_dir), 'reason': 'windows_path_headroom'})
