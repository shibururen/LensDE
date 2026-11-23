"""Stable basin ranking, optical topology distance and representative selection."""
from __future__ import annotations
import math
import random
from pathlib import Path
from .. import configuration as _lensde_configuration
from .. import materials as _lensde_materials
from .. import populations as _lensde_populations
from .. import provenance as _lensde_provenance
from . import stage_context as _m_stage_context
from .contracts import SearchState, PhaseResult

def basin_merit_sort_value(basin: dict) -> float:
    """Stable basin ranking, optical topology distance and representative selection. Implements `basin_merit_sort_value` for this responsibility."""
    score = basin.get('score', {}) if isinstance(basin.get('score'), dict) else {}
    return _lensde_configuration.finite_float(score.get('merit'), _lensde_configuration.finite_float(basin.get('merit'), _lensde_configuration.finite_float(basin.get('objective'), float('inf'))))

def funnel_row_merit(row: dict) -> float:
    """Stable basin ranking, optical topology distance and representative selection. Implements `funnel_row_merit` for this responsibility."""
    score = row.get('score', {}) if isinstance(row.get('score'), dict) else {}
    return _lensde_configuration.finite_float(row.get('merit'), _lensde_configuration.finite_float(score.get('merit'), _lensde_configuration.finite_float(row.get('objective'), float('inf'))))

def funnel_row_potential(row: dict) -> float:
    """Stable basin ranking, optical topology distance and representative selection. Implements `funnel_row_potential` for this responsibility."""
    score = row.get('score', {}) if isinstance(row.get('score'), dict) else {}
    return _lensde_configuration.finite_float(row.get('topology_potential_score_v3'), _lensde_configuration.finite_float(score.get('topology_potential_score_v3'), _lensde_configuration.finite_float(row.get('topology_potential_score_v2'), _lensde_configuration.finite_float(score.get('topology_potential_score_v2'), float('-inf')))))

def row_selection_values(row: dict) -> list[float] | None:
    """Stable basin ranking, optical topology distance and representative selection. Implements `row_selection_values` for this responsibility."""
    values = row.get('best_values')
    if isinstance(values, list):
        return values
    values = row.get('values')
    return values if isinstance(values, list) else None

def funnel_row_distance(a: dict, b: dict, selection_variables: list[dict]) -> float:
    """Stable basin ranking, optical topology distance and representative selection. Implements `funnel_row_distance` for this responsibility."""
    av = row_selection_values(a)
    bv = row_selection_values(b)
    if not isinstance(av, list) or not isinstance(bv, list):
        return 0.0
    return _lensde_populations.bound_normalized_distance(av, bv, selection_variables)

def optical_key_parts_from_row(row: dict) -> dict[str, str]:
    """Stable basin ranking, optical topology distance and representative selection. Implements `optical_key_parts_from_row` for this responsibility."""
    key = str(row.get('final_optical_coarse_key') or row.get('optical_coarse_key') or funnel_result_topology_key(row) or '')
    parts: dict[str, str] = {}
    for item in key.split('|'):
        if '=' not in item:
            continue
        name, value = item.split('=', 1)
        parts[name] = value
    if not parts and key:
        parts['pwr'] = key
        parts['shape'] = key
    parts.setdefault('pwr', str(row.get('final_power_signs') or row.get('power_signs') or ''))
    parts.setdefault('shape', str(row.get('final_surface_shapes') or row.get('surface_shapes') or ''))
    parts.setdefault('bend', str(row.get('final_bend_bins') or row.get('bend_bins') or ''))
    parts.setdefault('gap', str(row.get('final_gap_bins') or row.get('gap_bins') or ''))
    return parts

def topology_token_hamming(a: list[str], b: list[str]) -> int:
    """Stable basin ranking, optical topology distance and representative selection. Implements `topology_token_hamming` for this responsibility."""
    size = max(len(a), len(b))
    total = 0
    for index in range(size):
        total += 0 if (a[index] if index < len(a) else '') == (b[index] if index < len(b) else '') else 1
    return total

def topology_char_l1(a: str, b: str) -> int:
    """Stable basin ranking, optical topology distance and representative selection. Implements `topology_char_l1` for this responsibility."""
    size = max(len(a), len(b))
    total = 0
    for index in range(size):
        ca = a[index] if index < len(a) else '0'
        cb = b[index] if index < len(b) else '0'
        try:
            total += abs(int(ca) - int(cb))
        except ValueError:
            total += 0 if ca == cb else 1
    return total

def optical_topology_row_distance(a: dict, b: dict) -> float:
    """Stable basin ranking, optical topology distance and representative selection. Implements `optical_topology_row_distance` for this responsibility."""
    ap = optical_key_parts_from_row(a)
    bp = optical_key_parts_from_row(b)
    power = topology_token_hamming(list(ap.get('pwr', '')), list(bp.get('pwr', '')))
    shape = topology_token_hamming([part for part in ap.get('shape', '').split('.') if part], [part for part in bp.get('shape', '').split('.') if part])
    bend = topology_char_l1(ap.get('bend', ''), bp.get('bend', ''))
    gap = topology_char_l1(ap.get('gap', ''), bp.get('gap', ''))
    return float(10 * power + 4 * shape + bend + gap)

def select_optical_topology_representative_rows(_flow: SearchState, rows: list[dict], count: int, selection_variables: list[dict], *, per_coarse_key: int=2, same_key_min_distance: float=0.0, selection_priority_override: str | None=None) -> list[dict]:
    """Select representatives with the original quality seats, topology budget and stable tie ordering."""
    if count <= 0:
        return []
    valid = [row for row in rows if isinstance(row_selection_values(row), list) and math.isfinite(funnel_row_merit(row))]
    by_power: dict[str, list[dict]] = {}
    by_key: dict[str, list[dict]] = {}
    for row in valid:
        power = str(row.get('final_power_signs') or row.get('power_signs') or '?')
        key = str(row.get('final_optical_coarse_key') or row.get('optical_coarse_key') or funnel_result_topology_key(row) or '?')
        by_power.setdefault(power, []).append(row)
        by_key.setdefault(key, []).append(row)
    selected: list[dict] = []
    seen_ids: set[str] = set()
    per_coarse_limit = None if per_coarse_key < 0 else max(1, int(per_coarse_key or 1))
    same_key_min_distance = max(0.0, _lensde_configuration.finite_float(same_key_min_distance, 0.0))

    def source_id(row: dict) -> str:
        return str(row.get('source_id') or row.get('name') or row.get('id') or row.get('rank') or id(row))

    def row_coarse_key(row: dict) -> str:
        return str(row.get('final_optical_coarse_key') or row.get('optical_coarse_key') or funnel_result_topology_key(row) or '?')

    def same_key_distance_ok(row: dict) -> bool:
        if per_coarse_limit is None:
            return True
        key = row_coarse_key(row)
        same_key_rows = [chosen for chosen in selected if row_coarse_key(chosen) == key]
        if len(same_key_rows) < per_coarse_limit:
            return True
        if same_key_min_distance <= 0.0 or not same_key_rows:
            return False
        return min((funnel_row_distance(row, chosen, selection_variables) for chosen in same_key_rows)) >= same_key_min_distance

    def add(row: dict, reason: str) -> None:
        rid = source_id(row)
        if rid in seen_ids or len(selected) >= count:
            return
        if not same_key_distance_ok(row):
            return
        out = dict(row)
        out['selection_reason'] = reason
        selected.append(out)
        seen_ids.add(rid)
    selection_priority = str(selection_priority_override if selection_priority_override is not None else _flow.settings.options.get('initial_search_funnel_probe_parent_selection_priority', 'topology_merit_potential')).strip().lower()
    if selection_priority in {'topology_merit_potential', 'merit_potential', 'potential_merit', 'merit_potential_diversity', 'potential_topology'}:
        merit_quota = min(count, max(1, int(round(count * 0.4)))) if count > 0 else 0
        potential_quota = min(count - merit_quota, max(0, int(round(count * 0.4))))
        diversity_quota = max(0, count - merit_quota - potential_quota)

        def available_candidates() -> list[dict]:
            return [row for row in valid if source_id(row) not in seen_ids and same_key_distance_ok(row)]
        for row in sorted(available_candidates(), key=lambda row: (funnel_row_merit(row), source_id(row))):
            if len(selected) >= merit_quota:
                break
            add(row, 'merit_slot')
        potential_candidates = [row for row in available_candidates() if math.isfinite(funnel_row_potential(row))]
        for row in sorted(potential_candidates, key=lambda row: (-funnel_row_potential(row), funnel_row_merit(row), source_id(row))):
            if len(selected) >= merit_quota + potential_quota:
                break
            add(row, 'potential_slot')
        while len(selected) < merit_quota + potential_quota:
            candidates = available_candidates()
            if not candidates:
                break
            add(min(candidates, key=lambda row: (funnel_row_merit(row), source_id(row))), 'potential_slot_merit_fallback')
        while len(selected) < count and diversity_quota > 0:
            candidates = available_candidates()
            if not candidates:
                break
            if not selected:
                add(min(candidates, key=lambda row: (funnel_row_merit(row), source_id(row))), 'diversity_slot_seed')
            else:
                add(max(candidates, key=lambda row: (min((optical_topology_row_distance(row, chosen) for chosen in selected)), min((funnel_row_distance(row, chosen, selection_variables) for chosen in selected)), funnel_row_potential(row) if math.isfinite(funnel_row_potential(row)) else -funnel_row_merit(row), source_id(row))), 'diversity_slot')
        while len(selected) < count:
            candidates = available_candidates()
            if not candidates:
                break
            add(min(candidates, key=lambda row: (funnel_row_merit(row), source_id(row))), 'merit_potential_fill')
        return selected[:count]
    if selection_priority in {'topology_diversity', 'diversity', 'structure_diversity'}:

        def available_candidates() -> list[dict]:
            return [row for row in valid if source_id(row) not in seen_ids and same_key_distance_ok(row)]

        def medoid_row(candidates: list[dict]) -> dict:
            return min(candidates, key=lambda row: (sum((optical_topology_row_distance(row, other) for other in candidates)), source_id(row)))
        candidates = available_candidates()
        if candidates:
            add(medoid_row(candidates), 'topology_medoid_seed')
            if len(selected) >= count:
                return selected[:count]
        while len(selected) < count:
            candidates = available_candidates()
            if not candidates:
                break
            add(max(candidates, key=lambda row: (min((optical_topology_row_distance(row, chosen) for chosen in selected)) if selected else 0.0, min((funnel_row_distance(row, chosen, selection_variables) for chosen in selected)) if selected else 0.0, source_id(row))), 'topology_k_center_fill')
        return selected[:count]
    if selection_priority in {'topology_quality_gravity', 'quality_gravity', 'topology_diversity_quality', 'diversity_quality'}:
        raw_diversity_count = _flow.settings.options.get('initial_search_funnel_probe_gravity_diversity_count')
        if raw_diversity_count is not None:
            try:
                diversity_quota = max(0, int(raw_diversity_count))
            except (TypeError, ValueError):
                diversity_quota = -1
        else:
            diversity_quota = -1
        if diversity_quota < 0:
            try:
                diversity_fraction = _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_funnel_probe_gravity_diversity_fraction'), 0.5)
            except (TypeError, ValueError):
                diversity_fraction = 0.5
            diversity_fraction = min(1.0, max(0.0, diversity_fraction))
            diversity_quota = min(count, max(1, int(round(count * diversity_fraction)))) if count > 1 else count
        else:
            diversity_quota = min(count, diversity_quota)

        def available_candidates() -> list[dict]:
            return [row for row in valid if source_id(row) not in seen_ids and same_key_distance_ok(row)]

        def medoid_row(candidates: list[dict]) -> dict:
            return min(candidates, key=lambda row: (sum((optical_topology_row_distance(row, other) for other in candidates)), source_id(row)))
        candidates = available_candidates()
        if candidates and len(selected) < diversity_quota:
            add(medoid_row(candidates), 'topology_quality_gravity_medoid_seed')
        while len(selected) < diversity_quota:
            candidates = available_candidates()
            if not candidates:
                break
            add(max(candidates, key=lambda row: (min((optical_topology_row_distance(row, chosen) for chosen in selected)) if selected else 0.0, min((funnel_row_distance(row, chosen, selection_variables) for chosen in selected)) if selected else 0.0, source_id(row))), 'topology_quality_gravity_k_center')
        ordered_keys = sorted(by_key, key=lambda key: (min((funnel_row_merit(row) for row in by_key.get(key, []))), -len(by_key.get(key, [])), key))
        while len(selected) < count:
            added_this_round = 0
            for key in ordered_keys:
                pool = [row for row in by_key.get(key, []) if source_id(row) not in seen_ids and same_key_distance_ok(row)]
                if not pool:
                    continue
                add(min(pool, key=lambda row: (funnel_row_merit(row), source_id(row))), 'topology_quality_gravity_best_per_key')
                added_this_round += 1
                if len(selected) >= count:
                    break
            if added_this_round == 0:
                break
        while len(selected) < count:
            candidates = available_candidates()
            if not candidates:
                break
            add(max(candidates, key=lambda row: (min((optical_topology_row_distance(row, chosen) for chosen in selected)) if selected else 0.0, min((funnel_row_distance(row, chosen, selection_variables) for chosen in selected)) if selected else 0.0, -funnel_row_merit(row), source_id(row))), 'topology_quality_gravity_final_fill')
        return selected[:count]
    if selection_priority in {'topology_stratified_random', 'stratified_random', 'topology_reservoir'}:
        try:
            selection_seed = int(_flow.settings.options.get('initial_search_funnel_probe_selection_seed', _flow.settings.options.get('seed', 20260515)))
        except (TypeError, ValueError):
            selection_seed = 20260515
        rng = random.Random(selection_seed + 77001)

        def shuffled(values: list) -> list:
            out = list(values)
            rng.shuffle(out)
            return out
        keys = shuffled(sorted(by_key))
        while len(selected) < count:
            added_this_round = 0
            for key in keys:
                pool = [row for row in shuffled(by_key.get(key, [])) if source_id(row) not in seen_ids and same_key_distance_ok(row)]
                if not pool:
                    continue
                add(pool[0], 'topology_stratified_random')
                added_this_round += 1
                if len(selected) >= count:
                    break
            if added_this_round == 0:
                break
        return selected[:count]
    if selection_priority in {'merit_topology', 'topology_merit', 'merit_diversity', 'quality_topology', 'best_per_topology'}:
        ordered_keys = sorted(by_key, key=lambda key: (min((funnel_row_merit(row) for row in by_key.get(key, []))), -len(by_key.get(key, [])), key))
        while len(selected) < count:
            added_this_round = 0
            for key in ordered_keys:
                pool = [row for row in by_key.get(key, []) if source_id(row) not in seen_ids and same_key_distance_ok(row)]
                if not pool:
                    continue
                add(min(pool, key=lambda row: (funnel_row_merit(row), source_id(row))), 'merit_topology_best_per_key')
                added_this_round += 1
                if len(selected) >= count:
                    break
            if added_this_round == 0:
                break
        while len(selected) < count:
            candidates = [row for row in valid if source_id(row) not in seen_ids and same_key_distance_ok(row)]
            if not candidates:
                break
            add(min(candidates, key=lambda row: (funnel_row_merit(row), source_id(row))), 'merit_topology_fill')
        return selected[:count]
    for _power, group in sorted(by_power.items(), key=lambda item: min((funnel_row_merit(row) for row in item[1]))):
        add(min(group, key=funnel_row_merit), 'best_per_power_sign_family')
        if len(selected) >= count:
            return selected[:count]
    for _key, group in sorted(by_key.items(), key=lambda item: min((funnel_row_merit(row) for row in item[1]))):
        add(min(group, key=funnel_row_merit), 'best_per_optical_coarse_key')
        if len(selected) >= count:
            return selected[:count]
    for key, group in sorted(by_key.items(), key=lambda item: min((funnel_row_merit(row) for row in item[1]))):
        if per_coarse_limit is not None and sum((1 for row in selected if row_coarse_key(row) == key)) >= per_coarse_limit and (same_key_min_distance <= 0.0):
            continue
        fine_seen = {row.get('optical_fine_key') or row.get('final_optical_fine_key') for row in selected if row_coarse_key(row) == key}
        candidates = [row for row in group if source_id(row) not in seen_ids]
        if not candidates:
            continue
        novel = [row for row in candidates if (row.get('optical_fine_key') or row.get('final_optical_fine_key')) not in fine_seen]
        pool = novel or candidates
        if selected:
            add(max(pool, key=lambda row: (min((funnel_row_distance(row, chosen, selection_variables) for chosen in selected)), -funnel_row_merit(row))), 'second_per_key_by_fine_novelty_distance')
        else:
            add(min(pool, key=funnel_row_merit), 'second_per_key')
        if len(selected) >= count:
            return selected[:count]
    while len(selected) < count:
        candidates = [row for row in valid if source_id(row) not in seen_ids and same_key_distance_ok(row)]
        if not candidates:
            break
        if not selected:
            add(min(candidates, key=funnel_row_merit), 'merit_fill')
        else:
            add(max(candidates, key=lambda row: (min((funnel_row_distance(row, chosen, selection_variables) for chosen in selected)), -funnel_row_merit(row))), 'farthest_distance_fill')
    return selected[:count]

def population_record_flag(record: dict, name: str, default: bool=False) -> bool:
    """Stable basin ranking, optical topology distance and representative selection. Implements `population_record_flag` for this responsibility."""
    value = record.get(name)
    if isinstance(value, bool):
        return value
    try:
        return bool(int(float(value)))
    except (TypeError, ValueError):
        return default

def population_record_sort_key(record: dict) -> tuple[float, float, float, float, float, float, float, float]:
    """Stable basin ranking, optical topology distance and representative selection. Implements `population_record_sort_key` for this responsibility."""
    trace_ok = population_record_flag(record, 'trace_ok')
    trace_feasible = population_record_flag(record, 'trace_feasible', trace_ok)
    trace_coverage = _lensde_configuration.finite_float(record.get('trace_coverage'), 0.0)
    min_field_coverage = _lensde_configuration.finite_float(record.get('min_field_coverage'), 0.0)
    efl = _lensde_configuration.finite_float(record.get('efl'), float('nan'))
    ttl = _lensde_configuration.finite_float(record.get('ttl'), float('nan'))
    bfl = _lensde_configuration.finite_float(record.get('bfl'), float('nan'))
    finite_first_order = math.isfinite(efl) and math.isfinite(ttl) and math.isfinite(bfl)
    potential = _lensde_configuration.finite_float(record.get('topology_potential_score_v3'), _lensde_configuration.finite_float(record.get('topology_potential_score_v2'), float('-inf')))
    full_trace = trace_ok and trace_coverage >= 1.0 - 1e-09 and (min_field_coverage >= 1.0 - 1e-09) and finite_first_order
    return (0.0 if full_trace else 1.0, 0.0 if trace_feasible else 1.0, 0.0 if finite_first_order else 1.0, -trace_coverage, -min_field_coverage, -potential if math.isfinite(potential) else float('inf'), _lensde_configuration.finite_float(record.get('merit'), float('inf')), _lensde_configuration.finite_float(record.get('total_violation'), float('inf')))

def value_row_key(values: list[float], digits: int=10) -> tuple[float, ...]:
    """Stable basin ranking, optical topology distance and representative selection. Implements `value_row_key` for this responsibility."""
    return tuple((round(_lensde_configuration.finite_float(value, 0.0), digits) for value in values))

def population_candidate_basins(_flow: SearchState, stage_dir: Path, source_stage: str, limit: int, stage_context: dict | None=None) -> list[dict]:
    """Stable basin ranking, optical topology distance and representative selection. Implements `population_candidate_basins` for this responsibility."""
    if limit <= 0:
        return []
    stage_variables = stage_context['variables'] if stage_context else _flow.optics.variables
    population_path = stage_dir / 'population.csv'
    scores_path = stage_dir / 'population_scores.csv'
    if not population_path.exists() or not scores_path.exists():
        return []
    try:
        rows = _lensde_populations.read_population_csv(population_path)
        records = _lensde_materials.read_population_score_records(scores_path)
    except Exception as exc:
        _flow.runtime.stderr_parts.append(f'[funnel] failed to read population candidates from {stage_dir}: {exc}')
        return []
    records.sort(key=population_record_sort_key)
    selection_mode = str(_flow.settings.options.get('initial_search_funnel_population_candidate_selection', 'topology_merit_potential')).strip().lower()
    diversity_selection_modes = {'topology_diversity', 'diversity', 'structure_diversity'}
    mixed_potential_selection_modes = {'topology_merit_potential', 'merit_potential', 'potential_merit', 'merit_potential_diversity', 'potential_topology'}
    try:
        scan_limit = int(_flow.settings.options.get('initial_search_funnel_population_candidate_scan', 0) or 0)
    except (TypeError, ValueError):
        scan_limit = 0
    if selection_mode in diversity_selection_modes:
        window_limit = scan_limit if scan_limit > 0 else limit
        record_window = records[:window_limit] if window_limit > 0 else records
    elif selection_mode in mixed_potential_selection_modes:
        window_limit = scan_limit if scan_limit > 0 else max(limit * 4, 128)
        window_limit = min(len(records), max(limit, window_limit))
        record_window = list(records[:window_limit]) if window_limit > 0 else list(records)
        merit_window = sorted(records, key=lambda record: (_lensde_configuration.finite_float(record.get('merit'), float('inf')), _lensde_configuration.finite_float(record.get('total_violation'), float('inf'))))[:window_limit]
        seen_record_indices = {str(record.get('index', '')) for record in record_window}
        for record in merit_window:
            record_index = str(record.get('index', ''))
            if record_index in seen_record_indices:
                continue
            record_window.append(record)
            seen_record_indices.add(record_index)
    else:
        record_window = records[:limit]
    out: list[dict] = []
    seen: set[tuple[float, ...]] = set()
    for rank, record in enumerate(record_window, start=1):
        try:
            row_index = int(float(record.get('index', -1)))
        except (TypeError, ValueError):
            continue
        if not 0 <= row_index < len(rows):
            continue
        values = [_lensde_configuration.finite_float(value, 0.0) for value in rows[row_index]]
        if len(values) != len(stage_variables):
            continue
        key = value_row_key(values)
        if key in seen:
            continue
        seen.add(key)
        candidate = {'id': f'{source_stage}_population_{row_index}', 'source_id': f'{source_stage}:population:{row_index}', 'rank': rank, 'source': 'population', 'source_stage': source_stage, 'population_index': row_index, 'values': values, 'topology_key': str(record.get('topology_key') or ''), 'structural_topology_key': str(record.get('structural_topology_key') or record.get('topology_key') or ''), 'semantic_key': str(record.get('semantic_key') or ''), 'descriptor_key': str(record.get('descriptor_key') or ''), 'score': {'merit': _lensde_configuration.finite_float(record.get('merit'), float('inf')), 'efl': _lensde_configuration.finite_float(record.get('efl'), float('nan')), 'ttl': _lensde_configuration.finite_float(record.get('ttl'), float('nan')), 'bfl': _lensde_configuration.finite_float(record.get('bfl'), float('nan')), 'mean_spot': _lensde_configuration.finite_float(record.get('mean_spot'), float('nan')), 'max_spot': _lensde_configuration.finite_float(record.get('max_spot'), float('nan')), 'mean_worst_wavelength_spot': _lensde_configuration.finite_float(record.get('mean_worst_wavelength_spot'), float('nan')), 'max_worst_wavelength_spot': _lensde_configuration.finite_float(record.get('max_worst_wavelength_spot'), float('nan')), 'mean_spectral_spot_spread': _lensde_configuration.finite_float(record.get('mean_spectral_spot_spread'), float('nan')), 'max_spectral_spot_spread': _lensde_configuration.finite_float(record.get('max_spectral_spot_spread'), float('nan')), 'paraxial_axial_color_focus_shift_mm': _lensde_configuration.finite_float(record.get('paraxial_axial_color_focus_shift_mm'), float('nan')), 'max_distortion': _lensde_configuration.finite_float(record.get('max_distortion'), float('nan')), 'max_chief_ray_angle': _lensde_configuration.finite_float(record.get('max_chief_ray_angle'), float('nan')), 'chief_ray_path_bend_mean_deg': _lensde_configuration.finite_float(record.get('chief_ray_path_bend_mean_deg'), float('nan')), 'chief_ray_path_bend_mean_off_axis_deg': _lensde_configuration.finite_float(record.get('chief_ray_path_bend_mean_off_axis_deg'), float('nan')), 'chief_ray_path_bend_max_deg': _lensde_configuration.finite_float(record.get('chief_ray_path_bend_max_deg'), float('nan')), 'chief_ray_path_bend_max_step_deg': _lensde_configuration.finite_float(record.get('chief_ray_path_bend_max_step_deg'), float('nan')), 'chief_ray_path_excess_mean': _lensde_configuration.finite_float(record.get('chief_ray_path_excess_mean'), float('nan')), 'chief_ray_path_excess_max': _lensde_configuration.finite_float(record.get('chief_ray_path_excess_max'), float('nan')), 'lateral_color': _lensde_configuration.finite_float(record.get('lateral_color'), float('nan')), 'finite_ray_field_focus_spread_mm': _lensde_configuration.finite_float(record.get('finite_ray_field_focus_spread_mm'), float('nan')), 'finite_ray_astig_focus_gap_mm': _lensde_configuration.finite_float(record.get('finite_ray_astig_focus_gap_mm'), float('nan')), 'finite_ray_axial_color_focus_shift_mm': _lensde_configuration.finite_float(record.get('finite_ray_axial_color_focus_shift_mm'), float('nan')), 'finite_ray_lateral_color_mm': _lensde_configuration.finite_float(record.get('finite_ray_lateral_color_mm'), float('nan')), 'finite_ray_spherical_focus_spread_mm': _lensde_configuration.finite_float(record.get('finite_ray_spherical_focus_spread_mm'), float('nan')), 'finite_ray_coma_tangential_mm': _lensde_configuration.finite_float(record.get('finite_ray_coma_tangential_mm'), float('nan')), 'finite_ray_valid_fraction': _lensde_configuration.finite_float(record.get('finite_ray_valid_fraction'), float('nan')), 'finite_ray_metric_count': _lensde_configuration.finite_float(record.get('finite_ray_metric_count'), float('nan')), 'finite_ray_evidence': _lensde_configuration.finite_float(record.get('finite_ray_evidence'), float('nan')), 'topology_potential_score_v2': _lensde_configuration.finite_float(record.get('topology_potential_score_v2'), float('nan')), 'topology_potential_first_order_health_v2': _lensde_configuration.finite_float(record.get('topology_potential_first_order_health_v2'), float('nan')), 'topology_potential_hard_aberration_health_v2': _lensde_configuration.finite_float(record.get('topology_potential_hard_aberration_health_v2'), float('nan')), 'topology_potential_image_health_v2': _lensde_configuration.finite_float(record.get('topology_potential_image_health_v2'), float('nan')), 'topology_potential_margin_health_v2': _lensde_configuration.finite_float(record.get('topology_potential_margin_health_v2'), float('nan')), 'topology_potential_balance_health_v2': _lensde_configuration.finite_float(record.get('topology_potential_balance_health_v2'), float('nan')), 'topology_potential_trace_health_v2': _lensde_configuration.finite_float(record.get('topology_potential_trace_health_v2'), float('nan')), 'topology_potential_score_v3': _lensde_configuration.finite_float(record.get('topology_potential_score_v3'), float('nan')), 'topology_potential_first_order_health_v3': _lensde_configuration.finite_float(record.get('topology_potential_first_order_health_v3'), float('nan')), 'topology_potential_chief_path_health_v3': _lensde_configuration.finite_float(record.get('topology_potential_chief_path_health_v3'), float('nan')), 'topology_potential_hard_aberration_health_v3': _lensde_configuration.finite_float(record.get('topology_potential_hard_aberration_health_v3'), float('nan')), 'topology_potential_image_health_v3': _lensde_configuration.finite_float(record.get('topology_potential_image_health_v3'), float('nan')), 'topology_potential_margin_health_v3': _lensde_configuration.finite_float(record.get('topology_potential_margin_health_v3'), float('nan')), 'topology_potential_balance_health_v3': _lensde_configuration.finite_float(record.get('topology_potential_balance_health_v3'), float('nan')), 'topology_potential_trace_health_v3': _lensde_configuration.finite_float(record.get('topology_potential_trace_health_v3'), float('nan')), 'trace_ok': population_record_flag(record, 'trace_ok'), 'trace_coverage': _lensde_configuration.finite_float(record.get('trace_coverage'), 0.0), 'min_field_coverage': _lensde_configuration.finite_float(record.get('min_field_coverage'), 0.0)}, 'topology_potential_score_v2': _lensde_configuration.finite_float(record.get('topology_potential_score_v2'), float('nan')), 'topology_potential_score_v3': _lensde_configuration.finite_float(record.get('topology_potential_score_v3'), float('nan')), 'trace_feasible': population_record_flag(record, 'trace_feasible', population_record_flag(record, 'trace_ok')), 'feasible': population_record_flag(record, 'feasible', False)}
        _m_stage_context.attach_stage_context_fields(_flow, candidate, stage_context)
        out.append(candidate)
        if selection_mode not in diversity_selection_modes and selection_mode not in mixed_potential_selection_modes and (len(out) >= limit):
            break
    if selection_mode in mixed_potential_selection_modes and len(out) > limit:
        out = select_optical_topology_representative_rows(_flow, out, limit, stage_variables, per_coarse_key=-1, same_key_min_distance=0.0, selection_priority_override=selection_mode)
    elif selection_mode in diversity_selection_modes and len(out) > limit:
        selected: list[dict] = []
        remaining = list(out)
        remaining.sort(key=lambda row: (funnel_row_merit(row), int(row.get('rank', 0) or 0)))
        selected.append(remaining.pop(0))
        while remaining and len(selected) < limit:
            best_index = max(range(len(remaining)), key=lambda idx: (min((funnel_row_distance(remaining[idx], chosen, stage_variables) for chosen in selected)), -funnel_row_merit(remaining[idx]), -int(remaining[idx].get('rank', 0) or 0)))
            selected.append(remaining.pop(best_index))
        out = selected
    return out

def best_basin(_flow: SearchState, stage_dir: Path, stage_context: dict | None=None) -> dict:
    """Stable basin ranking, optical topology distance and representative selection. Implements `best_basin` for this responsibility."""
    basins = _m_stage_context.load_basin_archive(_flow, stage_dir)
    if basins:
        return basins[0]
    population_candidates = population_candidate_basins(_flow, stage_dir, stage_dir.name, 1, stage_context)
    if not population_candidates:
        return {}
    candidate = population_candidates[0]
    values = candidate.get('values')
    if not isinstance(values, list):
        return {}
    out = dict(candidate)
    out['values'] = [_lensde_configuration.finite_float(value, 0.0) for value in values]
    out.setdefault('score', candidate.get('score', {}))
    out.setdefault('source', 'population')
    out.setdefault('source_stage', candidate.get('source_stage', stage_dir.name))
    return out

def score_summary(stage_dir: Path) -> dict:
    """Stable basin ranking, optical topology distance and representative selection. Implements `score_summary` for this responsibility."""
    path = stage_dir / 'score.json'
    if not path.exists():
        return {}
    try:
        score = _lensde_provenance.read_json(path)
    except Exception:
        return {}
    return {'merit': score.get('merit'), 'mean_spot': score.get('mean_spot'), 'max_spot': score.get('max_spot'), 'efl': score.get('efl'), 'ttl': score.get('ttl'), 'bfl': score.get('bfl'), 'cra': score.get('max_chief_ray_angle'), 'min_edge': score.get('min_edge')}

def funnel_result_topology_key(row: dict) -> str:
    """Stable basin ranking, optical topology distance and representative selection. Implements `funnel_result_topology_key` for this responsibility."""
    return str(row.get('final_optical_coarse_key') or row.get('optical_coarse_key') or row.get('parent_optical_coarse_key') or row.get('final_structural_topology_key') or row.get('parent_structural_topology_key') or '')
