"""Full-restart search, geometry-memory exclusion and ordered archive merging."""
from __future__ import annotations
import copy
import json
import math
import csv
from pathlib import Path
from lensde.runtime import native_io_paths
from .. import commands as _lensde_commands
from .. import configuration as _lensde_configuration
from .. import defaults as _lensde_defaults
from .. import initialization as _lensde_initialization
from .. import materials as _lensde_materials
from .. import populations as _lensde_populations
from .. import provenance as _lensde_provenance
from . import basin_selection as _m_basin_selection
from . import execution as _m_execution
from . import seed_generation as _m_seed_generation
from . import stage_commands as _m_stage_commands
from . import stage_context as _m_stage_context
from .contracts import SearchState, PhaseResult

def stage_best_merit(_flow: SearchState, stage_dir: Path) -> float:
    """Full-restart search, geometry-memory exclusion and ordered archive merging. Implements `stage_best_merit` for this responsibility."""
    basin = _m_basin_selection.best_basin(_flow, stage_dir, _flow.exploration.global_explore_stage_context)
    basin_merit = _m_basin_selection.basin_merit_sort_value(basin) if basin else float('inf')
    score = _m_basin_selection.score_summary(stage_dir)
    score_merit = _lensde_configuration.finite_float(score.get('merit'), float('inf')) if score else float('inf')
    return min(basin_merit, score_merit)

def stage_plateau_retire_clear_count(stage_dir: Path) -> int:
    """Full-restart search, geometry-memory exclusion and ordered archive merging. Implements `stage_plateau_retire_clear_count` for this responsibility."""
    trace_path = stage_dir / 'adaptive_hybrid_trace.jsonl'
    if not trace_path.exists():
        return 0
    clear_count = 0
    try:
        with trace_path.open('r', encoding='utf-8') as f:
            for line in f:
                if 'active_plateau_retire_clear_count' not in line:
                    continue
                try:
                    row = json.loads(line)
                except Exception:
                    continue
                value = row.get('active_plateau_retire_clear_count')
                if isinstance(value, (int, float)) and math.isfinite(float(value)):
                    clear_count = max(clear_count, int(value))
    except Exception:
        return clear_count
    return max(0, clear_count)

def explore_outer_seen_avoid_topology_keys(_flow: SearchState, stage_dirs: list[Path], high_quality_merit_ratio: float, memory_max_merit_ratio: float, memory_policy: str) -> tuple[set[str], set[str], dict]:
    """Full-restart search, geometry-memory exclusion and ordered archive merging. Implements `explore_outer_seen_avoid_topology_keys` for this responsibility."""
    "Partition qualified history into exact-only and family avoid keys.\n\n            Every completed Outer's rank-1 basin is operational memory.  The\n            merit-ratio gate filters only the remaining archive basins, so a\n            poor terminal basin cannot disappear from cross-Outer history.\n            "
    stage_rows: list[list[tuple[dict, float]]] = []
    key_fields = ['topology_key', 'structural_topology_key', 'semantic_key', 'descriptor_key', 'continuous_topology_signature', 'optical_fine_key', 'final_optical_fine_key', 'final_structural_topology_key']
    for stage_dir in stage_dirs:
        rows: list[tuple[dict, float]] = []
        for index, basin in enumerate(_m_stage_context.load_basin_archive(_flow, stage_dir)):
            row = _m_stage_context.annotate_source_basin_optical_topology(_flow, basin, index, _flow.exploration.global_explore_stage_context)
            score = row.get('score') if isinstance(row.get('score'), dict) else {}
            merit = _lensde_configuration.finite_float(score.get('merit'), _lensde_configuration.finite_float(row.get('merit'), float('inf')))
            if not math.isfinite(merit) or merit <= 0.0:
                continue
            rows.append((row, merit))
        stage_rows.append(rows)
    policy = str(memory_policy or 'qualified_similarity').strip().lower()
    allowed_policies = {'legacy_all_similarity', 'qualified_similarity', 'all_exact_only', 'qualified_exact_only', 'merit_partitioned'}
    if policy not in allowed_policies:
        policy = 'qualified_similarity'
    if not any(stage_rows):
        return (set(), set(), {'policy': policy, 'frontier_merit': None, 'qualified_basin_count': 0, 'exact_only_basin_count': 0, 'similarity_basin_count': 0, 'discarded_basin_count': 0, 'rank1_default_admission': True, 'rank1_admission_count': 0, 'rank1_merit_override_count': 0, 'rank1_admissions': [], 'admission_frontiers': []})
    high_ratio = max(1.0, _lensde_configuration.finite_float(high_quality_merit_ratio, 3.0))
    max_ratio = max(high_ratio, _lensde_configuration.finite_float(memory_max_merit_ratio, 6.0))
    exact_only_keys: set[str] = set()
    similarity_keys: set[str] = set()
    exact_only_basin_count = 0
    similarity_basin_count = 0
    discarded_basin_count = 0
    rank1_admission_count = 0
    rank1_merit_override_count = 0
    rank1_admissions: list[dict] = []
    frontier_merit = float('inf')
    admission_frontiers: list[dict] = []
    for stage_index, rows in enumerate(stage_rows, start=1):
        if not rows:
            continue
        explicit_rank1_rows = [item for item in rows if int(_lensde_configuration.finite_float(item[0].get('rank'), 0.0)) == 1]
        rank1_row, stage_frontier = min(explicit_rank1_rows or rows, key=lambda item: item[1])
        frontier_merit = min(frontier_merit, stage_frontier)
        rank1_ratio = stage_frontier / max(frontier_merit, 1e-12)
        rank1_overrides_merit_gate = policy not in {'legacy_all_similarity', 'all_exact_only'} and rank1_ratio > max_ratio
        admission_frontiers.append({'history_outer': stage_index, 'stage_frontier_merit': stage_frontier, 'admission_frontier_merit': frontier_merit, 'basin_count': len(rows), 'rank1_merit_ratio': rank1_ratio, 'rank1_default_admitted': True, 'rank1_merit_override': rank1_overrides_merit_gate})
        for row, merit in rows:
            ratio = merit / max(frontier_merit, 1e-12)
            is_rank1 = row is rank1_row
            target_name = _lensde_configuration.cross_outer_memory_admission_target(policy, ratio, high_ratio, max_ratio, is_rank1=is_rank1)
            if target_name is None:
                discarded_basin_count += 1
                continue
            if target_name == 'exact_only':
                target = exact_only_keys
            else:
                target = similarity_keys
            if target is exact_only_keys:
                exact_only_basin_count += 1
            else:
                similarity_basin_count += 1
            if is_rank1:
                rank1_admission_count += 1
                if rank1_overrides_merit_gate:
                    rank1_merit_override_count += 1
                rank1_admissions.append({'history_outer': stage_index, 'merit': merit, 'merit_ratio': ratio, 'target': target_name, 'merit_override': rank1_overrides_merit_gate, 'topology_key': str(row.get('structural_topology_key') or row.get('topology_key') or '').strip()})
            for field in key_fields:
                if field == 'continuous_topology_signature' and target is exact_only_keys:
                    continue
                key = str(row.get(field) or '').strip()
                if key:
                    target.add(key)
    return (exact_only_keys, similarity_keys, {'policy': policy, 'frontier_merit': frontier_merit, 'high_quality_merit_ratio': high_ratio, 'memory_max_merit_ratio': max_ratio, 'qualified_basin_count': exact_only_basin_count + similarity_basin_count, 'exact_only_basin_count': exact_only_basin_count, 'similarity_basin_count': similarity_basin_count, 'discarded_basin_count': discarded_basin_count, 'rank1_default_admission': True, 'rank1_admission_count': rank1_admission_count, 'rank1_merit_override_count': rank1_merit_override_count, 'rank1_admissions': rank1_admissions, 'admission_frontiers': admission_frontiers})

def write_explore_avoid_topology_keys(path: Path, keys: set[str]) -> Path | None:
    """Full-restart search, geometry-memory exclusion and ordered archive merging. Implements `write_explore_avoid_topology_keys` for this responsibility."""
    cleaned = sorted({str(key).strip() for key in keys if str(key).strip()})
    if not cleaned:
        return None
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(cleaned) + '\n', encoding='utf-8')
    return path

def write_explore_archive_csv(path: Path, basins: list[dict]) -> None:
    """Full-restart search, geometry-memory exclusion and ordered archive merging. Implements `write_explore_archive_csv` for this responsibility."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ['id', 'rank', 'source_stage', 'source_id', 'source_dir', 'explore_outer_index', 'source_rank', 'source', 'merit', 'mean_spot', 'max_spot', 'efl', 'ttl', 'bfl', 'cra', 'min_edge', 'topology_key', 'structural_topology_key', 'semantic_key', 'descriptor_key', 'family_key', 'optical_coarse_key', 'optical_fine_key', 'power_signs', 'values']
    with path.open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        for basin in basins:
            row = dict(basin)
            score = row.get('score', {}) if isinstance(row.get('score'), dict) else {}
            row.setdefault('merit', _lensde_configuration.finite_float(score.get('merit'), _m_basin_selection.basin_merit_sort_value(row)))
            row.setdefault('mean_spot', score.get('mean_spot', row.get('mean_spot', '')))
            row.setdefault('max_spot', score.get('max_spot', row.get('max_spot', '')))
            row.setdefault('efl', score.get('efl', row.get('efl', '')))
            row.setdefault('ttl', score.get('ttl', row.get('ttl', '')))
            row.setdefault('bfl', score.get('bfl', row.get('bfl', '')))
            row.setdefault('cra', score.get('max_chief_ray_angle', row.get('cra', '')))
            row.setdefault('min_edge', score.get('min_edge', row.get('min_edge', '')))
            if isinstance(row.get('values'), list):
                row['values'] = json.dumps(row['values'], ensure_ascii=False)
            writer.writerow(row)

def merge_explore_outer_results(_flow: SearchState, outer_entries: list[dict], target_dir: Path, target_population: int) -> dict:
    """Full-restart search, geometry-memory exclusion and ordered archive merging. Implements `merge_explore_outer_results` for this responsibility."""

    def entry_returncode(entry: dict) -> int:
        try:
            return int(entry.get('returncode', 1))
        except (TypeError, ValueError):
            return 1
    completed = [entry for entry in outer_entries if entry_returncode(entry) == 0 and Path(str(entry.get('dir') or '')).exists()]
    if not completed:
        return {'outer_count': len(outer_entries), 'completed_outer_count': 0, 'basin_count': 0, 'best_merit': float('inf'), 'cra_tighten_enabled': _flow.exploration.cra_tighten_enabled, 'cra_tighten_generations': _flow.exploration.cra_tighten_generations if _flow.exploration.cra_tighten_enabled else 0, 'cra_start_tolerance': _flow.exploration.cra_start_tolerance, 'cra_tightened_tolerance': _flow.exploration.cra_tightened_tolerance if _flow.exploration.cra_tighten_enabled else None, 'cra_tightened_merit_fingerprint': _flow.exploration.cra_tightened_merit_fingerprint if _flow.exploration.cra_tighten_enabled else None, 'cra_source_merit_fingerprint': _flow.exploration.cra_source_merit_fingerprint if _flow.exploration.cra_tighten_enabled else None}

    def entry_dir(entry: dict) -> Path:
        return Path(str(entry.get('dir') or ''))
    best_entry = min(completed, key=lambda entry: stage_best_merit(_flow, entry_dir(entry)))
    best_dir = entry_dir(best_entry)
    target_dir.mkdir(parents=True, exist_ok=True)
    _lensde_commands.copy_stage_outputs(best_dir, target_dir)
    merged: list[dict] = []
    seen_value_keys: set[tuple[str, tuple[float, ...]]] = set()
    for entry in completed:
        stage_dir = entry_dir(entry)
        outer_index = int(entry.get('explore_outer_index', 0) or 0)
        stage_name = str(entry.get('name') or f'funnel_global_explore_outer_{outer_index:02d}')
        for source_rank, basin in enumerate(_m_stage_context.load_basin_archive(_flow, stage_dir), start=1):
            values = basin.get('values')
            if not isinstance(values, list) or len(values) != len(_flow.optics.variables):
                continue
            value_key = ('source', _m_basin_selection.value_row_key([_lensde_configuration.finite_float(value, 0.0) for value in values]))
            if value_key in seen_value_keys:
                continue
            seen_value_keys.add(value_key)
            row = copy.deepcopy(basin)
            row.update({'source_stage': stage_name, 'source_dir': str(stage_dir), 'source_rank': source_rank, 'explore_outer_index': outer_index, 'source_id': f"{stage_name}:archive:{basin.get('id', source_rank)}"})
            row = _m_stage_context.annotate_source_basin_optical_topology(_flow, row, len(merged), _flow.exploration.global_explore_stage_context)
            merged.append(row)
    merged.sort(key=lambda basin: (_m_basin_selection.basin_merit_sort_value(basin), int(basin.get('explore_outer_index', 0) or 0), int(basin.get('source_rank', 0) or 0)))
    archive_limit = max(0, int(_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_archive_limit'), 0.0)))
    if archive_limit > 0:
        merged = merged[:archive_limit]
    for rank, basin in enumerate(merged, start=1):
        basin['id'] = rank
        basin['rank'] = rank
    population_rows: list[list[float]] = []
    seen_population_rows: set[tuple[float, ...]] = set()
    for basin in merged:
        values = basin.get('values')
        if not isinstance(values, list) or len(values) != len(_flow.optics.variables):
            continue
        row = [_lensde_configuration.finite_float(value, 0.0) for value in values]
        key = _m_basin_selection.value_row_key(row)
        if key in seen_population_rows:
            continue
        seen_population_rows.add(key)
        population_rows.append(row)
        if len(population_rows) >= target_population:
            break
    try:
        for row in _lensde_populations.read_population_csv(best_dir / 'population.csv'):
            if len(row) != len(_flow.optics.variables):
                continue
            key = _m_basin_selection.value_row_key(row)
            if key in seen_population_rows:
                continue
            seen_population_rows.add(key)
            population_rows.append([_lensde_configuration.finite_float(value, 0.0) for value in row])
            if len(population_rows) >= target_population:
                break
    except Exception:
        pass
    if population_rows:
        _lensde_populations.write_population_csv(target_dir / 'population.csv', population_rows)
    archive_payload = {'count': len(merged), 'source_stage': 'global_explore_full_restart', 'full_restart': True, 'cra_tighten_enabled': _flow.exploration.cra_tighten_enabled, 'cra_tighten_generations': _flow.exploration.cra_tighten_generations if _flow.exploration.cra_tighten_enabled else 0, 'cra_start_tolerance': _flow.exploration.cra_start_tolerance, 'cra_tightened_tolerance': _flow.exploration.cra_tightened_tolerance if _flow.exploration.cra_tighten_enabled else None, 'cra_tightened_merit_fingerprint': _flow.exploration.cra_tightened_merit_fingerprint if _flow.exploration.cra_tighten_enabled else None, 'cra_source_merit_fingerprint': _flow.exploration.cra_source_merit_fingerprint if _flow.exploration.cra_tighten_enabled else None, 'checkpoint_source_outer_root': str(_flow.exploration.cra_tightened_outer_root if _flow.exploration.cra_tighten_enabled else native_io_paths.named_child(_flow.inputs.run_dir, 'funnel_global_explore_outers')), 'outer_count': len(outer_entries), 'completed_outer_count': len(completed), 'best_outer_index': int(best_entry.get('explore_outer_index', 0) or 0), 'best_outer_dir': str(best_dir), 'topology_count': len({basin.get('topology_key') for basin in merged if basin.get('topology_key')}), 'structural_topology_count': len({basin.get('structural_topology_key') for basin in merged if basin.get('structural_topology_key')}), 'semantic_count': len({basin.get('semantic_key') for basin in merged if basin.get('semantic_key')}), 'optical_coarse_count': len({basin.get('optical_coarse_key') for basin in merged if basin.get('optical_coarse_key')}), 'optical_fine_count': len({basin.get('optical_fine_key') for basin in merged if basin.get('optical_fine_key')}), 'population_rows': len(population_rows), 'basins': merged}
    _lensde_provenance.write_json(target_dir / 'basin_archive.json', archive_payload)
    write_explore_archive_csv(target_dir / 'basin_archive.csv', merged)
    summary = {key: value for key, value in archive_payload.items() if key != 'basins'}
    summary.update({'best_merit': _m_basin_selection.basin_merit_sort_value(merged[0]) if merged else stage_best_merit(_flow, best_dir), 'outer_summaries': [{'name': entry.get('name'), 'dir': entry.get('dir'), 'explore_outer_index': entry.get('explore_outer_index'), 'returncode': entry.get('returncode'), 'timed_out': entry.get('timed_out'), 'merit': entry.get('merit'), 'mean_spot': entry.get('mean_spot'), 'cra_tighten_phase': entry.get('cra_tighten_phase'), 'source_outer_dir': entry.get('source_outer_dir'), 'cra_start_tolerance': entry.get('cra_start_tolerance'), 'cra_tightened_tolerance': entry.get('cra_tightened_tolerance'), 'cra_tighten_generations': entry.get('cra_tighten_generations'), 'plateau_retire_clear_count': entry.get('plateau_retire_clear_count'), 'cumulative_plateau_retire_clear_count': entry.get('cumulative_plateau_retire_clear_count'), 'full_restart_stop_reason': entry.get('full_restart_stop_reason')} for entry in outer_entries], 'plateau_retire_clear_stop_target': max(0, int(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_stop_after_clears', 0) or 0)), 'cumulative_plateau_retire_clear_count': max([int(entry.get('cumulative_plateau_retire_clear_count') or 0) for entry in outer_entries] or [0]), 'full_restart_stop_reason': next((str(entry.get('full_restart_stop_reason') or '') for entry in outer_entries if entry.get('full_restart_stop_reason')), '')})
    summary['merit'] = summary['best_merit']
    _lensde_provenance.write_json(target_dir / 'full_restart_summary.json', summary)
    return summary

def run_full_restart_global_explore(_flow: SearchState, outer_count: int, de_gen: int, seed_stride: int) -> tuple[int, bool, dict]:
    """Execute ordered restart searches, reusing only completed stages and carrying the same memory/exclusion pool between outers."""
    outer_root = native_io_paths.named_child(_flow.inputs.run_dir, 'funnel_global_explore_outers')
    seed_pool_root = native_io_paths.named_child(_flow.inputs.run_dir, 'funnel_global_explore_seed_pools')
    explore_de_use_cuda = _lensde_commands.option_truthy(_flow.settings.options.get('initial_search_funnel_global_explore_de_use_cuda'), True)
    target_population = max(1, _flow.settings.population)
    raw_pool_rows = max(target_population, _flow.seeding.full_restart_per_outer_seed_pool_target)
    avoid_previous = bool(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_previous', True))
    merit_early_stop_enabled = bool(_flow.settings.options.get('initial_search_funnel_global_early_stop_enabled', False)) and str(_flow.settings.options.get('initial_search_funnel_global_early_stop_metric', '') or '').strip().lower().replace('-', '_') in {'best_merit', 'merit', 'best'}
    early_stop_generation_window = max(1, int(_flow.settings.options.get('initial_search_funnel_global_early_stop_generation_window', 400) or 400))
    epoch_plan = _lensde_defaults.full_restart_early_stop_epoch_plan(de_gen, early_stop_generation_window, merit_early_stop_enabled)
    internal_epoch_generations = epoch_plan['epoch_generations']
    internal_outer_epochs = epoch_plan['outer_epochs']
    early_stop_window_epochs = epoch_plan['window_epochs']
    cra_tighten_epoch_plan = _lensde_defaults.full_restart_early_stop_epoch_plan(_flow.exploration.cra_tighten_generations, min(400, _flow.exploration.cra_tighten_generations), _flow.exploration.cra_tighten_enabled)
    if cra_tighten_epoch_plan['outer_epochs'] > 20:
        cra_tighten_epoch_plan = _lensde_defaults.full_restart_early_stop_epoch_plan(_flow.exploration.cra_tighten_generations, _flow.exploration.cra_tighten_generations, False)
    cra_tighten_epoch_generations = cra_tighten_epoch_plan['epoch_generations']
    cra_tighten_outer_epochs = cra_tighten_epoch_plan['outer_epochs']
    shared_seed_pool_enabled = bool(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_shared_seed_pool', True))
    _flow.runtime.stdout_parts.append(f"[GUI] Explore full-restart mode: seed_restarts={outer_count}, per_restart_hybrid_outer={internal_outer_epochs}, per_epoch_generations={internal_epoch_generations}, early_stop_generation_window={early_stop_generation_window}, seed_stride={seed_stride}, raw_seed_rows={raw_pool_rows}, de_evaluator={('CUDA' if explore_de_use_cuda else 'CPU')}, avoid_previous={('yes' if avoid_previous else 'no')}, shared_seed_pool={('yes' if shared_seed_pool_enabled else 'no')}\n")
    if _flow.exploration.cra_tighten_enabled:
        _flow.runtime.stdout_parts.append(f'[GUI] Explore CRA-tighten continuation per restart: hybrid_outer={cra_tighten_outer_epochs}, per_epoch_generations={cra_tighten_epoch_generations}, total_generations={_flow.exploration.cra_tighten_generations}, tolerance={_flow.exploration.cra_start_tolerance:g}->{_flow.exploration.cra_tightened_tolerance:g} deg\n')
    prebuilt_seed_pools: list[dict] = []
    shared_seed_rows: list[list[float]] = []
    if shared_seed_pool_enabled:
        required_shared_rows = outer_count * raw_pool_rows
        shared_seed_sort_metric = 'selector_order'
        try:
            raw_shared_seed_rows = [[_lensde_configuration.finite_float(value, 0.0) for value in row] for row in _lensde_populations.read_population_csv(_flow.seeding.seed_path) if isinstance(row, list) and len(row) == len(_flow.optics.variables)]
            merit_by_key: dict[tuple[float, ...], float] = {}
            final_recheck_scores = _flow.settings.search_dir / 'seed_trace_gate_v2' / 'final_recheck' / 'score' / 'population_scores.csv'
            if final_recheck_scores.exists():
                for record in _lensde_materials.read_population_score_records(final_recheck_scores):
                    try:
                        row_index = int(float(record.get('index', '')))
                    except (TypeError, ValueError):
                        continue
                    if 0 <= row_index < len(raw_shared_seed_rows):
                        merit_by_key[_lensde_populations.variable_aware_seed_row_key(raw_shared_seed_rows[row_index], _flow.optics.variables)] = _lensde_configuration.finite_float(record.get('merit'), float('inf'))
            original_order = {_lensde_populations.variable_aware_seed_row_key(row, _flow.optics.variables): index for index, row in enumerate(raw_shared_seed_rows)}
            shared_seed_rows = _lensde_populations.dedupe_variable_aware_seed_rows(raw_shared_seed_rows, _flow.optics.variables)
            if merit_by_key:
                shared_seed_rows.sort(key=lambda row: (merit_by_key.get(_lensde_populations.variable_aware_seed_row_key(row, _flow.optics.variables), float('inf')), original_order.get(_lensde_populations.variable_aware_seed_row_key(row, _flow.optics.variables), len(raw_shared_seed_rows))))
                shared_seed_sort_metric = 'final_recheck_merit'
        except Exception as exc:
            _flow.runtime.stderr_parts.append(f'[funnel] shared full-restart seed pool read failed; falling back to per-restart generation: {exc}\n')
            shared_seed_rows = []
        if len(shared_seed_rows) >= required_shared_rows:
            seed_pool_root.mkdir(parents=True, exist_ok=True)
            shared_pool_path = seed_pool_root / 'shared_ranked_seed_pool.csv'
            _lensde_populations.write_population_csv(shared_pool_path, shared_seed_rows)
            allocation_seed = _flow.settings.seed + 13001 + outer_count * seed_stride
            try:
                allocated_rows, allocation_summary = _lensde_initialization.allocate_full_restart_raw_seed_pools(shared_seed_rows, _flow.optics.variables, outer_count, raw_pool_rows, allocation_seed)
            except Exception as exc:
                allocated_rows = []
                allocation_summary = {'failure': str(exc)}
            if allocation_summary.get('split_group_count', 0):
                _flow.runtime.stderr_parts.append(f"[funnel] shared full-restart seed allocator split {allocation_summary['split_group_count']} base-geometry groups to preserve exact disjoint raw pool sizes; see allocation summary.\n")
            if len(allocated_rows) == outer_count and all((len(rows) == raw_pool_rows for rows in allocated_rows)):
                for outer_index, rows in enumerate(allocated_rows, start=1):
                    raw_seed_path = seed_pool_root / f'outer_{outer_index:02d}_raw_seed_pool.csv'
                    outer_rows = rows
                    _lensde_populations.write_population_csv(raw_seed_path, outer_rows)
                    prebuilt_seed_pools.append({'outer_index': outer_index, 'raw_seed_path': raw_seed_path, 'gate_dir': _flow.settings.search_dir / 'seed_trace_gate_v2', 'seed_base': _flow.settings.seed, 'raw_rows': len(outer_rows), 'shared_seed_pool': True, 'shared_seed_pool_path': shared_pool_path, 'shared_seed_pool_source_rows': len(shared_seed_rows), 'seed_allocation_mode': allocation_summary.get('mode', ''), 'seed_allocation_split_group_count': allocation_summary.get('split_group_count', 0)})
                _lensde_provenance.write_json(seed_pool_root / 'shared_seed_pool_allocation_summary.json', {**allocation_summary, 'enabled': True, 'source_seed_population': str(_flow.seeding.seed_path), 'shared_seed_pool': str(shared_pool_path), 'target_population': target_population, 'raw_pool_rows': raw_pool_rows, 'sort_metric': shared_seed_sort_metric, 'seed_selection_mode': 'direct_partition_no_topology_exclusion', 'seed_topology_exclusion_enabled': False})
                _flow.runtime.stdout_parts.append(f"[GUI] Explore full-restart shared seed pool allocated: source_rows={len(shared_seed_rows)}, used_rows={required_shared_rows}, outer_count={outer_count}, per_outer_raw={raw_pool_rows}, per_outer_final={target_population}, seed_selection=direct_partition_no_topology_exclusion, split_groups={allocation_summary.get('split_group_count', 0)}, sort={shared_seed_sort_metric}, path={shared_pool_path}\n")
            else:
                _flow.runtime.stderr_parts.append('[funnel] shared full-restart seed pool allocation failed; falling back to per-restart generation.\n')
        else:
            _flow.runtime.stderr_parts.append(f'[funnel] shared full-restart seed pool has {len(shared_seed_rows)} rows; need {required_shared_rows}. Falling back to per-restart generation.\n')
    for outer_index in [] if prebuilt_seed_pools else range(1, outer_count + 1):
        raw_seed_path = seed_pool_root / f'outer_{outer_index:02d}_raw_seed_pool.csv'
        outer_gate_dir = native_io_paths.named_child(seed_pool_root, f'outer_{outer_index:02d}_seed_trace_gate_v2')
        outer_seed_base = _flow.settings.seed + 503 + outer_index * seed_stride
        _flow.runtime.stdout_parts.append(f'[GUI] Explore full-restart seed pool {outer_index:02d}: seed_base={outer_seed_base}, target_rows={raw_pool_rows}, gate_dir={outer_gate_dir}\n')
        raw_seed_rows = _m_seed_generation.make_initial_population_in_seed_domain(_flow, gate_dir=outer_gate_dir, seed_base=outer_seed_base, target_override=raw_pool_rows)
        if not raw_seed_rows:
            _flow.runtime.stderr_parts.append(f'[funnel] explore full-restart seed pool {outer_index:02d} failed to produce seed rows; skipping this outer.\n')
            continue
        _lensde_populations.write_population_csv(raw_seed_path, raw_seed_rows)
        prebuilt_seed_pools.append({'outer_index': outer_index, 'raw_seed_path': raw_seed_path, 'gate_dir': outer_gate_dir, 'seed_base': outer_seed_base, 'raw_rows': len(raw_seed_rows), 'shared_seed_pool': False, 'seed_allocation_mode': 'per_restart_mechanical_stratified_direct' if _flow.settings.mechanical_direct_seed else 'per_restart_trace_gate'})
    if not prebuilt_seed_pools:
        return (12, False, {'outer_count': outer_count, 'completed_outer_count': 0, 'basin_count': 0, 'best_merit': float('inf'), 'merit': float('inf'), 'failure': 'no_full_restart_seed_pools'})
    completed_stage_dirs: list[Path] = []
    outer_entries: list[dict] = []
    final_code = 0
    final_timed_out = False
    clear_stop_target = max(0, int(_flow.settings.options.get('initial_search_funnel_global_plateau_retire_stop_after_clears', 0) or 0))
    cumulative_clear_count = 0
    for seed_pool_entry in prebuilt_seed_pools:
        if final_timed_out:
            break
        outer_index = int(seed_pool_entry.get('outer_index', 0) or 0)
        raw_seed_path = Path(str(seed_pool_entry.get('raw_seed_path') or _flow.seeding.seed_path))
        outer_dir = outer_root / f'outer_{outer_index:02d}'
        final_seed_path = outer_dir / 'seed_population.csv'
        try:
            raw_rows = [[_lensde_configuration.finite_float(value, 0.0) for value in row] for row in _lensde_populations.read_population_csv(raw_seed_path) if isinstance(row, list) and len(row) == len(_flow.optics.variables)]
        except Exception:
            raw_rows = []
        avoid_high_quality_merit_ratio = max(1.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_high_quality_merit_ratio'), 3.0))
        avoid_memory_max_merit_ratio = max(avoid_high_quality_merit_ratio, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_memory_max_merit_ratio'), 6.0))
        avoid_memory_policy = str(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_memory_policy', 'qualified_similarity') or 'qualified_similarity').strip().lower()
        if avoid_previous:
            seen_exact_avoid_keys, seen_similarity_avoid_keys, avoid_memory_diagnostics = explore_outer_seen_avoid_topology_keys(_flow, completed_stage_dirs, avoid_high_quality_merit_ratio, avoid_memory_max_merit_ratio, avoid_memory_policy)
        else:
            seen_exact_avoid_keys = set()
            seen_similarity_avoid_keys = set()
            avoid_memory_diagnostics = {'policy': avoid_memory_policy, 'frontier_merit': None, 'high_quality_merit_ratio': avoid_high_quality_merit_ratio, 'memory_max_merit_ratio': avoid_memory_max_merit_ratio, 'qualified_basin_count': 0, 'exact_only_basin_count': 0, 'similarity_basin_count': 0, 'discarded_basin_count': 0, 'rank1_default_admission': True, 'rank1_admission_count': 0, 'rank1_merit_override_count': 0, 'rank1_admissions': [], 'admission_frontiers': []}
        seen_avoid_keys = seen_exact_avoid_keys | seen_similarity_avoid_keys
        avoid_penalty = max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_penalty'), 1000.0))
        avoid_similarity_threshold = min(1.0, max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_similarity_threshold'), 0.9)))
        avoid_max_shape_diff = max(-1, int(_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_max_shape_diff'), 3.0)))
        avoid_continuous_structure_enabled = _lensde_commands.option_truthy(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_structure_enabled'), True)
        avoid_continuous_significance_fraction = max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_continuous_significance_fraction'), 0.08))
        avoid_parameter_auxiliary_similarity_min = min(1.0, max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_avoid_parameter_auxiliary_similarity_min'), 0.9)))
        avoid_exact_key_path = write_explore_avoid_topology_keys(outer_dir / 'avoid_topology_exact_only_keys.txt', seen_exact_avoid_keys) if avoid_previous and seen_exact_avoid_keys and (avoid_penalty > 0.0) else None
        avoid_key_path = write_explore_avoid_topology_keys(outer_dir / 'avoid_topology_similarity_keys.txt', seen_similarity_avoid_keys) if avoid_previous and seen_similarity_avoid_keys and (avoid_penalty > 0.0) else None
        outer_profile_summary: dict = {}
        outer_seed_vignetting_preflight: dict = {'ready': True, 'skipped': True, 'skip_reason': 'no_materialized_outer_seed_rows'}
        outer_seed_preflight_failed = False
        if raw_rows:
            selected_rows = _lensde_initialization.direct_full_restart_seed_rows(raw_rows, target_population)
            selected_rows = _lensde_initialization.rebalance_initial_asphere_profile_rows(selected_rows, raw_rows, _flow.optics.variables, target_population, _flow.seeding.asphere_profile_metadata)
            outer_profile_summary = _lensde_initialization.initial_asphere_profile_summary(selected_rows, _flow.optics.variables, _flow.seeding.asphere_profile_metadata)
            external_rows = _lensde_initialization.external_initial_asphere_population_rows(selected_rows, _flow.optics.variables, target_population, _flow.seeding.asphere_profile_metadata)
            outer_seed_vignetting_ready, outer_seed_vignetting_summary, outer_seed_vignetting_requirements = _lensde_populations.vignetting_seed_population_preflight(external_rows, _flow.optics.variables, min_range_frac=_lensde_populations.seed_diversity_thresholds_from_options(_flow.settings.options)['min_range_frac'])
            outer_seed_vignetting_preflight = {'ready': outer_seed_vignetting_ready, 'skipped': False, 'summary': outer_seed_vignetting_summary, 'requirements': outer_seed_vignetting_requirements}
            _lensde_provenance.write_json(outer_dir / 'vignetting_seed_preflight.json', outer_seed_vignetting_preflight)
            _lensde_populations.write_population_csv(final_seed_path, external_rows)
            outer_seed_path = final_seed_path
            if not outer_seed_vignetting_ready:
                outer_seed_preflight_failed = True
                _flow.runtime.stderr_parts.append(f"[funnel] outer {outer_index:02d} seed population rejected before launch: mandatory vignetting dimensions have no usable range/activity; see {outer_dir / 'vignetting_seed_preflight.json'}.\n")
        else:
            outer_seed_path = raw_seed_path if raw_seed_path.exists() else _flow.seeding.seed_path

        def append_outer_avoid_args(stage_args: list[str]) -> None:
            if avoid_exact_key_path is None and avoid_key_path is None:
                return
            if avoid_exact_key_path is not None:
                stage_args.extend(['--avoid-topology-exact-only-keys', str(avoid_exact_key_path)])
            if avoid_key_path is not None:
                stage_args.extend(['--avoid-topology-keys', str(avoid_key_path)])
            stage_args.extend(['--avoid-topology-penalty', str(avoid_penalty)])
            if avoid_key_path is not None and avoid_similarity_threshold > 0.0:
                stage_args.extend(['--avoid-topology-similarity-threshold', str(avoid_similarity_threshold), '--avoid-topology-max-shape-diff', str(avoid_max_shape_diff)])
            if avoid_key_path is not None and avoid_continuous_structure_enabled:
                stage_args.extend(['--avoid-topology-continuous-structure', '--avoid-topology-continuous-significance-fraction', str(avoid_continuous_significance_fraction), '--avoid-topology-parameter-auxiliary-similarity-min', str(avoid_parameter_auxiliary_similarity_min)])
        outer_name = f'funnel_global_explore_outer_{outer_index:02d}'
        if outer_seed_preflight_failed:
            outer_code = 12
            outer_timed_out = False
            outer_score = {'failure': 'mandatory_vignetting_seed_diversity', 'vignetting_seed_preflight': outer_seed_vignetting_preflight}
        else:
            outer_args = _m_stage_commands.build_initial_search_stage_args(_flow, outer_dir, _flow.optics.system_path, _flow.optics.merit_path, _flow.optics.initial_stage_merit, outer_seed_path, internal_outer_epochs, 1009 + (outer_index - 1) * seed_stride, stage_population=_flow.settings.population, stage_de_gen=internal_epoch_generations, stage_de_use_cuda=explore_de_use_cuda)
            append_outer_avoid_args(outer_args)
            resumed_outer = _m_execution.resume_existing_funnel_stage(_flow, outer_name, outer_dir, require_checkpoint_snapshots=True and (not _flow.exploration.cra_tighten_enabled))
            if resumed_outer is not None:
                outer_code, outer_timed_out, outer_score = resumed_outer
            else:
                configured_outer_args = _m_stage_commands.configure_funnel_global_args(_flow, outer_args, no_lpsr=not False, no_polish=not _flow.exploration.funnel_global_polish_enabled, disable_basins=False)
                if merit_early_stop_enabled and internal_outer_epochs > 1:
                    _m_stage_commands.set_arg(configured_outer_args, '--adaptive-early-stop-window', early_stop_window_epochs)
                    _m_stage_commands.set_arg(configured_outer_args, '--adaptive-early-stop-min-epochs', early_stop_window_epochs)
                outer_code, outer_timed_out, outer_score = _m_execution.run_outer_vignetting_two_stage(_flow, outer_name, configured_outer_args, outer_dir, internal_outer_epochs, internal_epoch_generations, outer_dir / 'checkpoint_candidate_snapshots' if True and (not _flow.exploration.cra_tighten_enabled) else None)
        outer_clear_count = stage_plateau_retire_clear_count(outer_dir)
        entry = {'name': f'funnel_global_explore_outer_{outer_index:02d}', 'dir': str(outer_dir), 'explore_outer_index': outer_index, 'cra_tighten_phase': 'source' if _flow.exploration.cra_tighten_enabled else 'disabled', 'cra_start_tolerance': _flow.exploration.cra_start_tolerance, 'returncode': outer_code, 'timed_out': outer_timed_out, 'de_evaluator': 'CUDA' if explore_de_use_cuda else 'CPU', 'configured_generation_budget': de_gen, 'internal_outer_epochs': internal_outer_epochs, 'internal_epoch_generations': internal_epoch_generations, 'early_stop_generation_window': early_stop_generation_window, 'early_stop_window_epochs': early_stop_window_epochs, 'seed_pool': str(raw_seed_path), 'seed_gate_dir': str(seed_pool_entry.get('gate_dir') or ''), 'seed_base': seed_pool_entry.get('seed_base'), 'raw_seed_rows': seed_pool_entry.get('raw_rows'), 'seed_population': str(outer_seed_path), 'vignetting_seed_preflight': outer_seed_vignetting_preflight, 'shared_seed_pool': bool(seed_pool_entry.get('shared_seed_pool', False)), 'seed_allocation_mode': seed_pool_entry.get('seed_allocation_mode', ''), 'shared_seed_pool_path': str(seed_pool_entry.get('shared_seed_pool_path') or ''), 'shared_seed_pool_source_rows': seed_pool_entry.get('shared_seed_pool_source_rows'), 'seed_allocation_split_group_count': seed_pool_entry.get('seed_allocation_split_group_count', 0), 'asphere_profile_summary': outer_profile_summary, 'seen_topology_keys': len(seen_avoid_keys), 'seed_selection_mode': 'direct_partition_no_topology_exclusion', 'seed_topology_exclusion_enabled': False, 'seen_seed_topology_keys': 0, 'seen_avoid_topology_keys': len(seen_avoid_keys), 'avoid_topology_key_path': str(avoid_key_path or ''), 'avoid_topology_exact_only_key_path': str(avoid_exact_key_path or ''), 'seen_exact_avoid_topology_keys': len(seen_exact_avoid_keys), 'seen_similarity_avoid_topology_keys': len(seen_similarity_avoid_keys), 'avoid_memory': avoid_memory_diagnostics, 'avoid_topology_penalty': avoid_penalty if avoid_exact_key_path is not None or avoid_key_path is not None else 0.0, 'avoid_topology_similarity_threshold': avoid_similarity_threshold if avoid_key_path is not None else 0.0, 'avoid_topology_max_shape_diff': avoid_max_shape_diff if avoid_key_path is not None else -1, 'avoid_topology_continuous_structure': {'enabled': bool(avoid_key_path is not None and avoid_continuous_structure_enabled), 'rule': 'sign_compatible_plus_single_weighted_parameter_score', 'parameter_auxiliary_similarity_min': avoid_parameter_auxiliary_similarity_min, 'significance_fraction': avoid_continuous_significance_fraction, 'weights': {'curvature_sign_compatible': 0.5, 'curvature_cosine': 0.2, 'power_cosine': 0.15, 'thickness_similarity': 0.075, 'gap_similarity': 0.075}}, 'plateau_retire_clear_count': outer_clear_count, 'cumulative_plateau_retire_clear_count': cumulative_clear_count + outer_clear_count, **outer_score}
        _flow.exploration.funnel_summary['global'].append(entry)
        if outer_timed_out:
            final_timed_out = True
            final_code = outer_code
        elif outer_code != 0 and final_code == 0:
            final_code = outer_code
        if outer_code != 0 or outer_timed_out:
            outer_entries.append(entry)
            cumulative_clear_count += outer_clear_count
            if explore_de_use_cuda and (not outer_timed_out):
                _flow.runtime.stderr_parts.append(f'[funnel] CUDA explore outer {outer_index:02d} failed with exit code {outer_code}; CPU fallback is disabled.\n')
            if outer_timed_out or explore_de_use_cuda:
                break
            continue
        final_outer_entry = entry
        final_outer_dir = outer_dir
        final_outer_code = outer_code
        final_outer_timed_out = outer_timed_out
        final_outer_clear_count = outer_clear_count
        if _flow.exploration.cra_tighten_enabled:
            tightened_dir = _flow.exploration.cra_tightened_outer_root / f'outer_{outer_index:02d}'
            tightened_population_path = outer_dir / 'population.csv'
            tightened_name = f'funnel_global_explore_outer_{outer_index:02d}_cra_tighten'
            if not tightened_population_path.is_file():
                tightened_code = 12
                tightened_timed_out = False
                tightened_score = {'failure': 'missing_outer_terminal_population', 'source_population': str(tightened_population_path)}
                _flow.runtime.stderr_parts.append(f'[funnel] CRA tighten outer {outer_index:02d} requires the full terminal population, but {tightened_population_path} is missing.\n')
            else:
                tightened_dir.mkdir(parents=True, exist_ok=True)
                tightened_meta_path = tightened_dir / 'cra_tighten_stage.json'
                tightened_meta = _lensde_provenance.read_json_optional(tightened_meta_path)
                tightened_resume_compatible = isinstance(tightened_meta, dict) and int(_lensde_configuration.finite_float(tightened_meta.get('cra_tighten_generations'), -1.0)) == _flow.exploration.cra_tighten_generations and (tightened_meta.get('cra_source_merit_fingerprint') == _flow.exploration.cra_source_merit_fingerprint) and (tightened_meta.get('cra_tightened_merit_fingerprint') == _flow.exploration.cra_tightened_merit_fingerprint)
                tightened_args = _m_stage_commands.build_initial_search_stage_args(_flow, tightened_dir, _flow.optics.system_path, _flow.exploration.cra_tightened_merit_path, _flow.exploration.cra_tightened_merit, tightened_population_path, cra_tighten_outer_epochs, 700109 + (outer_index - 1) * seed_stride, stage_population=_flow.settings.population, stage_de_gen=cra_tighten_epoch_generations, stage_de_use_cuda=explore_de_use_cuda)
                append_outer_avoid_args(tightened_args)
                resumed_tightened = _m_execution.resume_existing_funnel_stage(_flow, tightened_name, tightened_dir, require_checkpoint_snapshots=True) if tightened_resume_compatible else None
                if resumed_tightened is not None:
                    tightened_code, tightened_timed_out, tightened_score = resumed_tightened
                else:
                    _lensde_provenance.write_json(tightened_dir / 'merit.json', _flow.exploration.cra_tightened_merit)
                    configured_tightened_args = _m_stage_commands.configure_funnel_global_args(_flow, tightened_args, no_lpsr=not False, no_polish=not _flow.exploration.funnel_global_polish_enabled, disable_basins=False)
                    _m_stage_commands.set_arg(configured_tightened_args, '--adaptive-early-stop-min-epochs', cra_tighten_outer_epochs)
                    tightened_code, tightened_timed_out, tightened_score = _m_execution.run_funnel_stage(_flow, tightened_name, configured_tightened_args, tightened_dir, tightened_dir / 'checkpoint_candidate_snapshots' if True else None)
                    if tightened_code == 0 and (not tightened_timed_out):
                        _lensde_provenance.write_json(tightened_meta_path, {'source_outer_dir': str(outer_dir), 'source_population': str(tightened_population_path), 'cra_start_tolerance': _flow.exploration.cra_start_tolerance, 'cra_tightened_tolerance': _flow.exploration.cra_tightened_tolerance, 'cra_tighten_generations': _flow.exploration.cra_tighten_generations, 'internal_outer_epochs': cra_tighten_outer_epochs, 'internal_epoch_generations': cra_tighten_epoch_generations, 'cra_source_merit_fingerprint': _flow.exploration.cra_source_merit_fingerprint, 'cra_tightened_merit_fingerprint': _flow.exploration.cra_tightened_merit_fingerprint})
            tightened_clear_count = stage_plateau_retire_clear_count(tightened_dir)
            tightened_entry = {**entry, 'name': tightened_name, 'dir': str(tightened_dir), 'source_outer_dir': str(outer_dir), 'source_population': str(tightened_population_path), 'cra_tighten_phase': 'tightened', 'cra_start_tolerance': _flow.exploration.cra_start_tolerance, 'cra_tightened_tolerance': _flow.exploration.cra_tightened_tolerance, 'cra_tighten_generations': _flow.exploration.cra_tighten_generations, 'cra_tightened_merit_fingerprint': _flow.exploration.cra_tightened_merit_fingerprint, 'cra_source_merit_fingerprint': _flow.exploration.cra_source_merit_fingerprint, 'configured_generation_budget': _flow.exploration.cra_tighten_generations, 'internal_outer_epochs': cra_tighten_outer_epochs, 'internal_epoch_generations': cra_tighten_epoch_generations, 'returncode': tightened_code, 'timed_out': tightened_timed_out, 'plateau_retire_clear_count': tightened_clear_count, **tightened_score}
            _flow.exploration.funnel_summary['global'].append(tightened_entry)
            final_outer_entry = tightened_entry
            final_outer_dir = tightened_dir
            final_outer_code = tightened_code
            final_outer_timed_out = tightened_timed_out
            final_outer_clear_count = tightened_clear_count
            if tightened_timed_out:
                final_timed_out = True
                final_code = tightened_code
            elif tightened_code != 0 and final_code == 0:
                final_code = tightened_code
        cumulative_clear_count += final_outer_clear_count
        final_outer_entry['cumulative_plateau_retire_clear_count'] = cumulative_clear_count
        outer_entries.append(final_outer_entry)
        if final_outer_code == 0 and (not final_outer_timed_out):
            completed_stage_dirs.append(final_outer_dir)
        elif explore_de_use_cuda:
            _flow.runtime.stderr_parts.append(f'[funnel] CUDA CRA-tighten outer {outer_index:02d} failed with exit code {final_outer_code}; CPU fallback is disabled.\n')
            break
        if not final_outer_timed_out and final_outer_code == 0 and (clear_stop_target > 0) and (cumulative_clear_count >= clear_stop_target):
            reason = 'plateau_retire_clear_count'
            final_outer_entry['full_restart_stop_reason'] = reason
            _flow.runtime.stdout_parts.append(f'[GUI] Explore full-restart early stop: cumulative_plateau_retire_clear_count={cumulative_clear_count} >= target={clear_stop_target} after outer {outer_index:02d}\n')
            break
    merged_summary = merge_explore_outer_results(_flow, outer_entries, _flow.exploration.global_explore_dir, target_population)
    _lensde_provenance.write_json(_flow.inputs.run_dir / 'funnel_global_explore_full_restart_summary.json', merged_summary)
    return (final_code, final_timed_out, merged_summary)
