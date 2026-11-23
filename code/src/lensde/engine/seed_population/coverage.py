"""Repair active-coordinate coverage using caller-owned RNG and stable donor order."""
from __future__ import annotations
import math
from .. import configuration as _lensde_configuration
from . import coordinates as _seed_coordinates

# Repair draws use the supplied RNG after the original donor/column selection.
def repair_seed_population_active_coverage(selected_rows: list[list[float]], objects: list[dict], variables: list[dict], target: int, *, min_active_fraction: float=0.05, min_curvature_active_fraction: float | None=None) -> tuple[list[list[float]], dict]:
    """Repair a nearly valid selector result with targeted row swaps.

    The C++ selector normally returns a sufficiently diverse population.  When a
    handful of activity counters miss their threshold, rebuilding all distances
    and greedily selecting the whole population in Python is disproportionately
    expensive.  This routine preserves the selected population and swaps in only
    strict-pool rows that close those deficits.
    """
    target = max(0, int(target))
    min_active_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(min_active_fraction, 0.05)))
    if min_curvature_active_fraction is None:
        min_curvature_active_fraction = min_active_fraction
    min_curvature_active_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(min_curvature_active_fraction, min_active_fraction)))
    dimension = len(variables)
    repaired_rows: list[list[float]] = []
    selected_keys: set[tuple[float, ...]] = set()
    for row in selected_rows:
        if not isinstance(row, list) or len(row) != dimension:
            continue
        normalized_row = [_lensde_configuration.finite_float(value, 0.0) for value in row]
        key = _seed_coordinates.variable_aware_seed_row_key(normalized_row, variables)
        if key in selected_keys:
            continue
        selected_keys.add(key)
        repaired_rows.append(normalized_row)
        if len(repaired_rows) >= target:
            break
    source_rows: list[list[float]] = []
    source_keys: list[tuple[float, ...]] = []
    source_active_sets: list[set[int]] = []
    source_masks: list[str] = []
    seen_source: set[tuple[float, ...]] = set()
    for obj in objects:
        if obj.get('strict') is False:
            continue
        row = obj.get('row')
        if not isinstance(row, list) or len(row) != dimension:
            continue
        normalized_row = [_lensde_configuration.finite_float(value, 0.0) for value in row]
        key = _seed_coordinates.variable_aware_seed_row_key(normalized_row, variables)
        if key in seen_source:
            continue
        seen_source.add(key)
        source_rows.append(normalized_row)
        source_keys.append(key)
        source_active_sets.append(_seed_coordinates.active_indices_from_row(normalized_row, variables))
        source_masks.append(_seed_coordinates.row_curvature_mask(normalized_row, variables))
    result = {'attempted': True, 'target': target, 'selected_count': len(repaired_rows), 'strict_source_count': len(source_rows), 'swap_count': 0, 'complete': False, 'reason': ''}
    if target <= 0 or dimension <= 0:
        result.update({'complete': target <= 0, 'reason': 'empty target or variable set'})
        return (repaired_rows, result)
    if len(repaired_rows) < target:
        result['reason'] = f'selected population has {len(repaired_rows)} rows; target is {target}'
        return (repaired_rows, result)
    if not source_rows:
        result['reason'] = 'strict source pool is empty'
        return (repaired_rows, result)
    required_counts: list[int] = []
    for variable in variables:
        fraction = min_curvature_active_fraction if variable.get('kind') == 'curvature' else min_active_fraction
        required_counts.append(0 if fraction <= 0.0 else max(1, min(target, int(math.ceil(target * fraction)))))
    available_counts = [0 for _ in variables]
    candidates_by_variable: list[list[int]] = [[] for _ in variables]
    for source_index, active_set in enumerate(source_active_sets):
        for variable_index in active_set:
            if 0 <= variable_index < dimension:
                available_counts[variable_index] += 1
                candidates_by_variable[variable_index].append(source_index)
    required_counts = [min(required, available_counts[index]) for index, required in enumerate(required_counts)]
    source_index_by_key = {key: index for index, key in enumerate(source_keys)}
    selected_active_sets: list[set[int]] = []
    selected_masks: list[str] = []
    selected_counts = [0 for _ in variables]
    for row in repaired_rows:
        key = _seed_coordinates.variable_aware_seed_row_key(row, variables)
        source_index = source_index_by_key.get(key)
        active_set = set(source_active_sets[source_index]) if source_index is not None else _seed_coordinates.active_indices_from_row(row, variables)
        mask = source_masks[source_index] if source_index is not None else _seed_coordinates.row_curvature_mask(row, variables)
        selected_active_sets.append(active_set)
        selected_masks.append(mask)
        for variable_index in active_set:
            if 0 <= variable_index < dimension:
                selected_counts[variable_index] += 1
    counts_before = list(selected_counts)
    protected_positions: set[int] = set()
    for variable_index, variable in enumerate(variables):
        ordered = sorted(range(len(repaired_rows)), key=lambda row_index: _seed_coordinates.seed_variable_coordinate(repaired_rows[row_index][variable_index], variable))
        if ordered:
            protected_positions.add(ordered[0])
            protected_positions.add(ordered[-1])
    mask_counts: dict[str, int] = {}
    for mask in selected_masks:
        mask_counts[mask] = mask_counts.get(mask, 0) + 1
    max_swaps = min(target, sum((max(0, required_counts[index] - selected_counts[index]) for index in range(dimension))))
    swaps = 0
    stalled = False
    while swaps < max_swaps:
        deficits = [index for index, required in enumerate(required_counts) if selected_counts[index] < required]
        if not deficits:
            break
        deficits.sort(key=lambda index: (variables[index].get('kind') == 'curvature', (required_counts[index] - selected_counts[index]) / max(1, required_counts[index])), reverse=True)
        deficit_index = deficits[0]
        candidate_options: list[tuple[tuple[float, float, float], int]] = []
        for source_index in candidates_by_variable[deficit_index]:
            key = source_keys[source_index]
            if key in selected_keys:
                continue
            active_set = source_active_sets[source_index]
            covered_deficits = [index for index in deficits if index in active_set]
            weighted_gain = sum((2.0 if variables[index].get('kind') == 'curvature' else 1.0 for index in covered_deficits))
            normalized_gain = sum((1.0 / max(1, required_counts[index]) for index in covered_deficits))
            candidate_options.append(((weighted_gain, normalized_gain, float(len(active_set))), source_index))
        candidate_options.sort(reverse=True)
        replacement: tuple[int, int] | None = None
        for _, source_index in candidate_options:
            candidate_active = source_active_sets[source_index]
            candidate_mask = source_masks[source_index]
            best_position: int | None = None
            best_removal_score: tuple[int, int] | None = None
            for position, old_active in enumerate(selected_active_sets):
                if position in protected_positions or deficit_index in old_active:
                    continue
                old_mask = selected_masks[position]
                if old_mask != candidate_mask and mask_counts.get(old_mask, 0) <= 1:
                    continue
                safe = True
                for variable_index in old_active:
                    post_count = selected_counts[variable_index] - 1
                    if variable_index in candidate_active:
                        post_count += 1
                    if post_count < required_counts[variable_index]:
                        safe = False
                        break
                if not safe:
                    continue
                removal_score = (len(old_active - candidate_active), len(old_active))
                if best_removal_score is None or removal_score < best_removal_score:
                    best_position = position
                    best_removal_score = removal_score
            if best_position is not None:
                replacement = (source_index, best_position)
                break
        if replacement is None:
            stalled = True
            break
        source_index, position = replacement
        old_row = repaired_rows[position]
        old_key = _seed_coordinates.variable_aware_seed_row_key(old_row, variables)
        old_active = selected_active_sets[position]
        old_mask = selected_masks[position]
        new_row = list(source_rows[source_index])
        new_key = source_keys[source_index]
        new_active = set(source_active_sets[source_index])
        new_mask = source_masks[source_index]
        for variable_index in old_active:
            selected_counts[variable_index] -= 1
        for variable_index in new_active:
            selected_counts[variable_index] += 1
        selected_keys.discard(old_key)
        selected_keys.add(new_key)
        mask_counts[old_mask] = max(0, mask_counts.get(old_mask, 1) - 1)
        mask_counts[new_mask] = mask_counts.get(new_mask, 0) + 1
        repaired_rows[position] = new_row
        selected_active_sets[position] = new_active
        selected_masks[position] = new_mask
        swaps += 1
    remaining_deficits = [index for index, required in enumerate(required_counts) if selected_counts[index] < required]
    result.update({'swap_count': swaps, 'complete': not remaining_deficits, 'reason': 'repair stalled before satisfying all activity thresholds' if stalled else '', 'required_active_counts': required_counts, 'available_active_counts': available_counts, 'selected_active_counts_before': counts_before, 'selected_active_counts_after': selected_counts, 'remaining_deficit_variable_indices': remaining_deficits})
    return (repaired_rows, result)
