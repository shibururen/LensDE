"""Select traceable seed representatives in the original ranking and distance order."""
from __future__ import annotations
import math
import random
from .. import configuration as _lensde_configuration
from . import coordinates as _seed_coordinates

# Stable candidate and tie order determine which coverage representatives survive.
def select_traceable_diverse_population(objects: list[dict], variables: list[dict], target: int, seed: int, *, min_range_frac: float=0.001, target_range_frac: float=0.01, min_active_fraction: float=0.05, min_curvature_active_fraction: float | None=None) -> list[list[float]]:
    target = max(0, int(target))
    min_active_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(min_active_fraction, 0.05)))
    if min_curvature_active_fraction is None:
        min_curvature_active_fraction = min_active_fraction
    min_curvature_active_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(min_curvature_active_fraction, min_active_fraction)))
    strict_objects: list[dict] = []
    seen: set[tuple[float, ...]] = set()
    for obj in objects:
        if not obj.get('strict'):
            continue
        row = obj.get('row')
        if not isinstance(row, list) or len(row) != len(variables):
            continue
        key = _seed_coordinates.variable_aware_seed_row_key(row, variables)
        if key in seen:
            continue
        seen.add(key)
        strict_objects.append({**obj, 'row': [_lensde_configuration.finite_float(value, 0.0) for value in row]})
    if target <= 0 or not strict_objects:
        return []
    rng = random.Random(seed)
    old_selection_limit = max(2048, target * 2, target + 2 * len(variables))
    selection_limit = max(target, min(old_selection_limit, max(512, int(math.ceil(target * 1.25)), target + max(256, 6 * len(variables)))))
    if len(strict_objects) > selection_limit:
        reserved_indices: set[int] = set()
        for variable_index in range(len(variables)):
            ordered = sorted(range(len(strict_objects)), key=lambda obj_index: _seed_coordinates.seed_variable_coordinate(strict_objects[obj_index]['row'][variable_index], variables[variable_index]))
            if ordered:
                reserved_indices.add(ordered[0])
                reserved_indices.add(ordered[-1])
        merit_count = min(max(64, target // 4), len(strict_objects))
        merit_ordered = sorted(range(len(strict_objects)), key=lambda obj_index: _lensde_configuration.finite_float(strict_objects[obj_index].get('merit'), float('inf')))
        reserved_indices.update(merit_ordered[:merit_count])
        mask_representatives: dict[tuple[int, ...], int] = {}
        for obj_index, obj in enumerate(strict_objects):
            mask = _seed_coordinates.row_curvature_mask(obj['row'], variables)
            current_index = mask_representatives.get(mask)
            if current_index is None:
                mask_representatives[mask] = obj_index
                continue
            current_merit = _lensde_configuration.finite_float(strict_objects[current_index].get('merit'), float('inf'))
            obj_merit = _lensde_configuration.finite_float(obj.get('merit'), float('inf'))
            if obj_merit < current_merit:
                mask_representatives[mask] = obj_index
        reserved_indices.update(mask_representatives.values())
        curvature_index_set_for_prelimit = {index for index, variable in enumerate(variables) if variable.get('kind') == 'curvature'}
        prelimit_active_sets = [_seed_coordinates.active_indices_from_row(obj['row'], variables) for obj in strict_objects]
        prelimit_active_counts = [len(active_set) for active_set in prelimit_active_sets]
        prelimit_curvature_counts = [sum((1 for index in active_set if index in curvature_index_set_for_prelimit)) for active_set in prelimit_active_sets]
        preserve_jitter = [rng.random() for _ in strict_objects]

        def prelimit_required_active_count(variable_index: int) -> int:
            if target <= 0:
                return 0
            fraction = min_curvature_active_fraction if variable_index in curvature_index_set_for_prelimit else min_active_fraction
            if fraction <= 0.0:
                return 0
            return max(1, min(target, int(math.ceil(target * fraction))))
        for variable_index in range(len(variables)):
            required = prelimit_required_active_count(variable_index)
            if required <= 0:
                continue
            already_reserved = sum((1 for obj_index in reserved_indices if variable_index in prelimit_active_sets[obj_index]))
            need = max(0, required - already_reserved)
            if need <= 0:
                continue
            active_candidates = [obj_index for obj_index, active_set in enumerate(prelimit_active_sets) if obj_index not in reserved_indices and variable_index in active_set]
            if not active_candidates:
                continue
            if len(active_candidates) > need:
                active_candidates.sort(key=lambda obj_index: (-prelimit_curvature_counts[obj_index], -prelimit_active_counts[obj_index], _lensde_configuration.finite_float(strict_objects[obj_index].get('merit'), float('inf')), preserve_jitter[obj_index]))
                active_candidates = active_candidates[:need]
            reserved_indices.update(active_candidates)
        remaining = [index for index in range(len(strict_objects)) if index not in reserved_indices]
        rng.shuffle(remaining)
        keep_indices = list(reserved_indices) + remaining[:max(0, selection_limit - len(reserved_indices))]
        strict_objects = [strict_objects[index] for index in keep_indices]
    spans = []
    offsets = []
    for variable in variables:
        lo, hi = _seed_coordinates.seed_variable_coordinate_bounds(variable)
        spans.append(max(1e-12, abs(hi - lo)))
        offsets.append(lo)
    normalized_rows = [[(_seed_coordinates.seed_variable_coordinate(value, variables[index]) - offsets[index]) / spans[index] for index, value in enumerate(obj['row'])] for obj in strict_objects]
    numpy_module = None
    normalized_array = None
    selected_mask_array = None
    try:
        import numpy as imported_numpy
        numpy_module = imported_numpy
        normalized_array = numpy_module.asarray(normalized_rows, dtype=float)
        selected_mask_array = numpy_module.zeros(len(strict_objects), dtype=bool)
    except Exception:
        numpy_module = None
        normalized_array = None
        selected_mask_array = None
    curvature_indices = [index for index, variable in enumerate(variables) if variable.get('kind') == 'curvature']
    curvature_index_set = set(curvature_indices)
    candidate_active_sets = [_seed_coordinates.active_indices_from_row(obj['row'], variables) for obj in strict_objects]
    active_counts = [len(active_set) for active_set in candidate_active_sets]
    active_curvature_counts = [sum((1 for index in active_set if index in curvature_index_set)) for active_set in candidate_active_sets]
    curvature_masks = [_seed_coordinates.row_curvature_mask(obj['row'], variables) for obj in strict_objects]

    def normalized_distance_sq(index_a: int, index_b: int) -> float:
        a = normalized_rows[index_a]
        b = normalized_rows[index_b]
        acc = 0.0
        for x, y in zip(a, b, strict=False):
            delta = x - y
            acc += delta * delta
        return acc / max(1, len(a))
    selected: list[dict] = []
    selected_indices: set[int] = set()
    selected_keys: set[tuple[float, ...]] = set()
    if numpy_module is not None and normalized_array is not None:
        min_distances = numpy_module.full(len(strict_objects), float('inf'), dtype=float)
    else:
        min_distances = [float('inf') for _ in strict_objects]

    def add_index(obj_index: int) -> None:
        if len(selected) >= target or obj_index in selected_indices:
            return
        obj = strict_objects[obj_index]
        key = _seed_coordinates.variable_aware_seed_row_key(obj['row'], variables)
        if key in selected_keys:
            return
        selected.append(obj)
        selected_indices.add(obj_index)
        selected_keys.add(key)
        if numpy_module is not None and normalized_array is not None and (selected_mask_array is not None):
            delta = normalized_array - normalized_array[obj_index]
            distances = numpy_module.einsum('ij,ij->i', delta, delta) / max(1, len(variables))
            numpy_module.minimum(min_distances, distances, out=min_distances)
            selected_mask_array[obj_index] = True
            min_distances[selected_mask_array] = 0.0
            return
        for candidate_index, candidate in enumerate(strict_objects):
            if candidate_index in selected_indices:
                min_distances[candidate_index] = 0.0
                continue
            distance = normalized_distance_sq(candidate_index, obj_index)
            if distance < min_distances[candidate_index]:
                min_distances[candidate_index] = distance
    for index in range(len(variables)):
        if len(selected) >= target:
            break
        ordered = sorted(range(len(strict_objects)), key=lambda obj_index: _seed_coordinates.seed_variable_coordinate(strict_objects[obj_index]['row'][index], variables[index]))
        if ordered:
            add_index(ordered[0])
        if len(selected) >= target:
            break
        if ordered:
            add_index(ordered[-1])
    if not selected:
        first = min(range(len(strict_objects)), key=lambda obj_index: _lensde_configuration.finite_float(strict_objects[obj_index].get('merit'), float('inf')))
        add_index(first)
    greedy_target = target
    if target > 0:
        greedy_target = min(target, max(256, 8 * max(1, len(variables)), int(math.sqrt(target) * 8)))
    while len(selected) < greedy_target:
        candidate_indices = [index for index in range(len(strict_objects)) if index not in selected_indices]
        if not candidate_indices:
            break
        selected_masks = {curvature_masks[index] for index in selected_indices}

        def score(obj_index: int) -> tuple[float, float, float, float]:
            obj = strict_objects[obj_index]
            min_distance = float(min_distances[obj_index])
            if not math.isfinite(min_distance):
                min_distance = 0.0
            active_bonus = active_counts[obj_index] / max(1, len(variables))
            curvature_active_bonus = active_curvature_counts[obj_index] / max(1, len(curvature_indices))
            mask_bonus = 0.15 if curvature_masks[obj_index] not in selected_masks else 0.0
            merit = _lensde_configuration.finite_float(obj.get('merit'), float('inf'))
            return (min_distance + 0.05 * active_bonus + 0.2 * curvature_active_bonus + mask_bonus, -merit, rng.random(), 0.0)
        add_index(max(candidate_indices, key=score))
    if len(selected) < target:
        selected_masks = {curvature_masks[index] for index in selected_indices}
        candidate_indices = [index for index in range(len(strict_objects)) if index not in selected_indices]

        def append_without_distance_update(obj_index: int) -> bool:
            obj = strict_objects[obj_index]
            key = _seed_coordinates.variable_aware_seed_row_key(obj['row'], variables)
            if key in selected_keys:
                return False
            selected.append(obj)
            selected_indices.add(obj_index)
            selected_keys.add(key)
            return True

        def fill_score(obj_index: int, *, include_mask_bonus: bool=True, random_tiebreak: bool=True) -> tuple[float, float, float, float]:
            obj = strict_objects[obj_index]
            min_distance = float(min_distances[obj_index])
            if not math.isfinite(min_distance):
                min_distance = 0.0
            active_bonus = active_counts[obj_index] / max(1, len(variables))
            curvature_active_bonus = active_curvature_counts[obj_index] / max(1, len(curvature_indices))
            mask_bonus = 0.15 if include_mask_bonus and curvature_masks[obj_index] not in selected_masks else 0.0
            merit = _lensde_configuration.finite_float(obj.get('merit'), float('inf'))
            tie_break = rng.random() if random_tiebreak else 0.0
            return (min_distance + 0.05 * active_bonus + 0.2 * curvature_active_bonus + mask_bonus, -merit, tie_break, 0.0)

        def required_active_count(variable_index: int) -> int:
            variable = variables[variable_index]
            fraction = min_curvature_active_fraction if variable.get('kind') == 'curvature' else min_active_fraction
            if fraction <= 0.0 or target <= 0:
                return 0
            return max(1, min(target, int(math.ceil(target * fraction))))

        def ensure_variable_active_coverage() -> None:
            if len(selected) >= target:
                return
            required_counts = [required_active_count(index) for index in range(len(variables))]
            if not any(required_counts):
                return
            selected_active_counts = [0 for _ in variables]
            for obj_index in selected_indices:
                for variable_index in candidate_active_sets[obj_index]:
                    if variable_index < len(selected_active_counts):
                        selected_active_counts[variable_index] += 1
            available_active_counts = [0 for _ in variables]
            for active_set in candidate_active_sets:
                for variable_index in active_set:
                    if variable_index < len(available_active_counts):
                        available_active_counts[variable_index] += 1
            for index, available in enumerate(available_active_counts):
                required_counts[index] = min(required_counts[index], available)
            skipped: set[int] = set()
            while len(selected) < target:
                deficits = {index for index, required in enumerate(required_counts) if required > selected_active_counts[index]}
                if not deficits:
                    break
                best_index: int | None = None
                best_score: tuple[float, float, float, float] | None = None
                for obj_index in candidate_indices:
                    if obj_index in selected_indices or obj_index in skipped:
                        continue
                    active_set = candidate_active_sets[obj_index]
                    covered = active_set.intersection(deficits)
                    if not covered:
                        continue
                    coverage_gain = 0.0
                    for variable_index in covered:
                        weight = 2.0 if variable_index in curvature_index_set else 1.0
                        coverage_gain += weight / max(1, required_counts[variable_index])
                    merit = _lensde_configuration.finite_float(strict_objects[obj_index].get('merit'), float('inf'))
                    score = (coverage_gain, active_curvature_counts[obj_index] / max(1, len(curvature_indices)), active_counts[obj_index] / max(1, len(variables)), -merit + 1e-09 * rng.random())
                    if best_score is None or score > best_score:
                        best_index = obj_index
                        best_score = score
                if best_index is None:
                    break
                if append_without_distance_update(best_index):
                    selected_masks.add(curvature_masks[best_index])
                    for variable_index in candidate_active_sets[best_index]:
                        if variable_index < len(selected_active_counts):
                            selected_active_counts[variable_index] += 1
                else:
                    skipped.add(best_index)
        ensure_variable_active_coverage()
        candidate_indices = [index for index in candidate_indices if index not in selected_indices]
        mask_representatives: dict[tuple[int, ...], int] = {}
        for obj_index in candidate_indices:
            mask = curvature_masks[obj_index]
            if mask in selected_masks:
                continue
            current_index = mask_representatives.get(mask)
            if current_index is None or fill_score(obj_index, include_mask_bonus=False, random_tiebreak=False) > fill_score(current_index, include_mask_bonus=False, random_tiebreak=False):
                mask_representatives[mask] = obj_index
        for obj_index in sorted(mask_representatives.values(), key=lambda index: fill_score(index, include_mask_bonus=False), reverse=True):
            if len(selected) >= target:
                break
            if append_without_distance_update(obj_index):
                selected_masks.add(curvature_masks[obj_index])
        candidate_indices = [index for index in candidate_indices if index not in selected_indices]
        for obj_index in sorted(candidate_indices, key=fill_score, reverse=True):
            if len(selected) >= target:
                break
            if append_without_distance_update(obj_index):
                selected_masks.add(curvature_masks[obj_index])
    return [obj['row'] for obj in selected[:target]]
