"""Measure active-coordinate coverage and gate traceable seed-pool diversity."""
from __future__ import annotations
import math
from .. import configuration as _lensde_configuration
from . import coordinates as _seed_coordinates

# Measure coverage before applying selector/refill admission thresholds.
def seed_diversity_thresholds_from_options(options: dict | None=None) -> dict[str, float]:
    options = options or {}
    min_range_frac = max(0.0, _lensde_configuration.finite_float(options.get('initial_search_diversity_min_range_frac'), 0.001))
    target_range_frac = max(0.0, _lensde_configuration.finite_float(options.get('initial_search_diversity_target_range_frac'), 0.01))
    min_active_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(options.get('initial_search_diversity_min_active_fraction'), 0.08)))
    min_curvature_active_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(options.get('initial_search_diversity_min_curvature_active_fraction'), min_active_fraction)))
    return {'min_range_frac': min_range_frac, 'target_range_frac': target_range_frac, 'min_active_fraction': min_active_fraction, 'min_curvature_active_fraction': min_curvature_active_fraction}


def seed_pool_diversity_summary(rows: list[list[float]], variables: list[dict], *, min_range_frac: float=0.001, target_range_frac: float=0.01, min_active_fraction: float=0.05, min_curvature_active_fraction: float | None=None, active_target_count: int | None=None) -> dict:
    min_range_frac = max(0.0, _lensde_configuration.finite_float(min_range_frac, 0.001))
    target_range_frac = max(0.0, _lensde_configuration.finite_float(target_range_frac, 0.01))
    min_active_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(min_active_fraction, 0.05)))
    if min_curvature_active_fraction is None:
        min_curvature_active_fraction = min_active_fraction
    min_curvature_active_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(min_curvature_active_fraction, min_active_fraction)))
    active_fraction_denominator = None
    if active_target_count is not None:
        active_fraction_denominator = max(1, int(active_target_count))
    if not rows or not variables:
        return {'rows': len(rows), 'variables': len(variables), 'variables_with_range_frac_ge_0_001': 0, 'variables_with_range_frac_ge_0_01': 0, 'variables_with_range_frac_ge_min': 0, 'variables_with_range_frac_ge_target': 0, 'variables_with_active_frac_ge_min': 0, 'curvature_variables_with_active_frac_ge_min': 0, 'min_variable_active_frac': 0.0, 'min_curvature_active_frac': 0.0, 'median_active_vars': 0, 'median_active_curvature_vars': 0, 'unique_curvature_masks': 0, 'duplicate_rows': 0, 'undercovered_variables': [], 'undercovered_curvature_variables': [], 'active_fraction_denominator': active_fraction_denominator or max(1, len(rows)), 'active_fraction_uses_target_count': active_fraction_denominator is not None, 'diversity_min_range_frac': min_range_frac, 'diversity_target_range_frac': target_range_frac, 'diversity_min_active_fraction': min_active_fraction, 'diversity_min_curvature_active_fraction': min_curvature_active_fraction}
    variable_count = len(variables)
    coordinate_scales: list[float] = []
    coordinate_initials: list[float] = []
    coordinate_lows: list[float] = []
    coordinate_highs: list[float] = []
    active_tolerances: list[float] = []
    curvature_flags: list[bool] = []
    for variable in variables:
        scale = _seed_coordinates.profiled_asphere_seed_q_scale(variable)
        raw_initial = _lensde_configuration.finite_float(variable.get('initial'), _seed_coordinates.variable_current_value_for_system({}, variable, 0.0))
        lo, hi = _seed_coordinates.seed_variable_coordinate_bounds(variable)
        coordinate_scales.append(scale)
        coordinate_initials.append(raw_initial * scale)
        coordinate_lows.append(lo)
        coordinate_highs.append(hi)
        active_tolerances.append(_seed_coordinates.seed_activity_tolerance(variable))
        curvature_flags.append(variable.get('kind') == 'curvature')
    value_counts = [0] * variable_count
    value_mins = [math.inf] * variable_count
    value_maxs = [-math.inf] * variable_count
    variable_active_counts = [0] * variable_count
    positive_active_counts = [0] * variable_count
    negative_active_counts = [0] * variable_count
    active_counts: list[int] = []
    active_curvature_counts: list[int] = []
    masks: set[str] = set()
    unique_row_keys: set[tuple[float, ...]] = set()
    for row in rows:
        row_active_count = 0
        row_active_curvature_count = 0
        curvature_signs: list[str] = []
        row_key: list[float] = []
        for index, value in enumerate(row):
            raw_value = _lensde_configuration.finite_float(value, 0.0)
            if index >= variable_count:
                row_key.append(round(raw_value, 12))
                continue
            coordinate_value = raw_value * coordinate_scales[index]
            row_key.append(round(coordinate_value, 12))
            value_counts[index] += 1
            if coordinate_value < value_mins[index]:
                value_mins[index] = coordinate_value
            if coordinate_value > value_maxs[index]:
                value_maxs[index] = coordinate_value
            delta = coordinate_value - coordinate_initials[index]
            tolerance = active_tolerances[index]
            if abs(delta) > tolerance:
                variable_active_counts[index] += 1
                row_active_count += 1
                if curvature_flags[index]:
                    row_active_curvature_count += 1
            if delta > tolerance:
                positive_active_counts[index] += 1
            elif delta < -tolerance:
                negative_active_counts[index] += 1
            if curvature_flags[index]:
                curvature_signs.append('p' if raw_value > tolerance else 'n' if raw_value < -tolerance else 'z')
        active_counts.append(row_active_count)
        active_curvature_counts.append(row_active_curvature_count)
        masks.add(''.join(curvature_signs))
        unique_row_keys.add(tuple(row_key))
    ranges: list[dict] = []
    legacy_min_count = 0
    legacy_target_count = 0
    min_count = 0
    target_count = 0
    active_covered_count = 0
    curvature_active_covered_count = 0
    active_fracs: list[float] = []
    curvature_active_fracs: list[float] = []
    pool_active_fracs: list[float] = []
    curvature_pool_active_fracs: list[float] = []
    undercovered_variables: list[dict] = []
    undercovered_curvature_variables: list[dict] = []
    for index, variable in enumerate(variables):
        value_count = value_counts[index]
        lo = coordinate_lows[index]
        hi = coordinate_highs[index]
        initial = coordinate_initials[index]
        span = max(1e-12, abs(hi - lo))
        active_tolerance = active_tolerances[index]
        active_count = variable_active_counts[index]
        positive_active_count = positive_active_counts[index]
        negative_active_count = negative_active_counts[index]
        active_frac = active_count / max(1, value_count)
        r_min = value_mins[index] if value_count else 0.0
        r_max = value_maxs[index] if value_count else 0.0
        frac = abs(r_max - r_min) / span
        if frac + 1e-15 >= 0.001:
            legacy_min_count += 1
        if frac + 1e-15 >= 0.01:
            legacy_target_count += 1
        if frac + 1e-15 >= min_range_frac:
            min_count += 1
        if frac + 1e-15 >= target_range_frac:
            target_count += 1
        kind = str(variable.get('kind', ''))
        required_active_frac = min_curvature_active_fraction if kind == 'curvature' else min_active_fraction
        variable_active_denominator = active_fraction_denominator or max(1, value_count)
        required_active_count = 0 if required_active_frac <= 0.0 else max(1, min(variable_active_denominator, int(math.ceil(variable_active_denominator * required_active_frac))))
        active_coverage_frac = min(1.0, active_count / max(1, variable_active_denominator))
        active_ready = required_active_frac <= 0.0 or active_count >= required_active_count
        if active_ready:
            active_covered_count += 1
        else:
            undercovered_variables.append({'index': index, 'kind': variable.get('kind', ''), 'surface': variable.get('index', ''), 'term': variable.get('term', 0), 'active_frac': active_frac, 'active_coverage_frac': active_coverage_frac, 'active_fraction_denominator': variable_active_denominator, 'required_active_frac': required_active_frac, 'required_active_count': required_active_count, 'active_count': active_count})
        active_fracs.append(active_coverage_frac)
        pool_active_fracs.append(active_frac)
        if kind == 'curvature':
            if active_ready:
                curvature_active_covered_count += 1
            else:
                undercovered_curvature_variables.append(undercovered_variables[-1])
            curvature_active_fracs.append(active_coverage_frac)
            curvature_pool_active_fracs.append(active_frac)
        ranges.append({'index': index, 'kind': variable.get('kind', ''), 'surface': variable.get('index', ''), 'term': variable.get('term', 0), 'initial': initial, 'min': r_min, 'max': r_max, 'range': r_max - r_min, 'range_frac': frac, 'active_tolerance': active_tolerance, 'active_count': active_count, 'active_frac': active_frac, 'active_coverage_frac': active_coverage_frac, 'active_fraction_denominator': variable_active_denominator, 'positive_active_count': positive_active_count, 'positive_active_frac': positive_active_count / max(1, value_count), 'negative_active_count': negative_active_count, 'negative_active_frac': negative_active_count / max(1, value_count), 'required_active_frac': required_active_frac, 'required_active_count': required_active_count, 'active_ready': active_ready})
    active_counts.sort()
    active_curvature_counts.sort()
    active_fracs.sort()
    curvature_active_fracs.sort()
    pool_active_fracs.sort()
    curvature_pool_active_fracs.sort()
    median_active = active_counts[len(active_counts) // 2] if active_counts else 0
    median_active_curvature = active_curvature_counts[len(active_curvature_counts) // 2] if active_curvature_counts else 0
    min_variable_active_frac = active_fracs[0] if active_fracs else 0.0
    min_curvature_active_frac = curvature_active_fracs[0] if curvature_active_fracs else 0.0
    min_variable_pool_active_frac = pool_active_fracs[0] if pool_active_fracs else 0.0
    min_curvature_pool_active_frac = curvature_pool_active_fracs[0] if curvature_pool_active_fracs else 0.0
    return {'rows': len(rows), 'variables': len(variables), 'variables_with_range_frac_ge_0_001': legacy_min_count, 'variables_with_range_frac_ge_0_01': legacy_target_count, 'variables_with_range_frac_ge_min': min_count, 'variables_with_range_frac_ge_target': target_count, 'variables_with_active_frac_ge_min': active_covered_count, 'curvature_variables_with_active_frac_ge_min': curvature_active_covered_count, 'min_variable_active_frac': min_variable_active_frac, 'min_curvature_active_frac': min_curvature_active_frac, 'min_variable_pool_active_frac': min_variable_pool_active_frac, 'min_curvature_pool_active_frac': min_curvature_pool_active_frac, 'median_active_vars': median_active, 'median_active_curvature_vars': median_active_curvature, 'unique_curvature_masks': len(masks), 'duplicate_rows': max(0, len(rows) - len(unique_row_keys)), 'undercovered_variables': undercovered_variables, 'undercovered_curvature_variables': undercovered_curvature_variables, 'active_fraction_denominator': active_fraction_denominator or max(1, len(rows)), 'active_fraction_uses_target_count': active_fraction_denominator is not None, 'diversity_min_range_frac': min_range_frac, 'diversity_target_range_frac': target_range_frac, 'diversity_min_active_fraction': min_active_fraction, 'diversity_min_curvature_active_fraction': min_curvature_active_fraction, 'variable_ranges': ranges}


def mutable_vignetting_variable_indices(variables: list[dict]) -> list[int]:
    """Return vignetting coordinates that the optimizer can actually change."""
    indices: list[int] = []
    for index, variable in enumerate(variables):
        if not _lensde_configuration.is_vignetting_variable(variable):
            continue
        lo, hi = _seed_coordinates.seed_variable_coordinate_bounds(variable)
        if abs(hi - lo) > 1e-12:
            indices.append(index)
    return indices


def mandatory_vignetting_seed_diversity_requirements(summary: dict, variables: list[dict], *, target_range_frac: float, require_active_ready: bool, minimum_active_count: int=1) -> dict:
    """Describe per-coordinate vignetting coverage that may not be averaged away."""
    target_range_frac = max(0.0, _lensde_configuration.finite_float(target_range_frac, 0.0))
    minimum_active_count = max(0, int(minimum_active_count))
    ranges_by_index = {int(_lensde_configuration.finite_float(item.get('index'), -1.0)): item for item in summary.get('variable_ranges', []) if isinstance(item, dict)}
    mandatory_indices = mutable_vignetting_variable_indices(variables)
    missing_range_indices: list[int] = []
    missing_active_indices: list[int] = []
    for index in mandatory_indices:
        item = ranges_by_index.get(index)
        if item is None or _lensde_configuration.finite_float(item.get('range_frac'), 0.0) + 1e-15 < target_range_frac:
            missing_range_indices.append(index)
        required_active_count = minimum_active_count
        if require_active_ready and item is not None:
            required_active_count = max(required_active_count, int(_lensde_configuration.finite_float(item.get('required_active_count'), 0.0)))
        if item is None or int(_lensde_configuration.finite_float(item.get('active_count'), 0.0)) < required_active_count:
            missing_active_indices.append(index)
    return {'mandatory_vignetting_variable_indices': mandatory_indices, 'mandatory_vignetting_target_range_frac': target_range_frac, 'mandatory_vignetting_require_active_ready': bool(require_active_ready), 'mandatory_vignetting_minimum_active_count': minimum_active_count, 'missing_mandatory_vignetting_range_indices': missing_range_indices, 'missing_mandatory_vignetting_active_indices': missing_active_indices}


def vignetting_seed_population_preflight(rows: list[list[float]], variables: list[dict], *, min_range_frac: float=0.001) -> tuple[bool, dict, dict]:
    """Reject an outer seed population whose vignetting dimensions are locked."""
    min_range_frac = max(0.0, _lensde_configuration.finite_float(min_range_frac, 0.001))
    summary = seed_pool_diversity_summary(rows, variables, min_range_frac=min_range_frac, target_range_frac=min_range_frac, min_active_fraction=0.0, min_curvature_active_fraction=0.0, active_target_count=len(rows))
    requirements = mandatory_vignetting_seed_diversity_requirements(summary, variables, target_range_frac=min_range_frac, require_active_ready=False, minimum_active_count=1)
    ready = not (requirements['missing_mandatory_vignetting_range_indices'] or requirements['missing_mandatory_vignetting_active_indices'])
    return (ready, summary, requirements)


def seed_pool_refill_diversity_ready(rows: list[list[float]], variables: list[dict], *, min_range_frac: float=0.001, target_range_frac: float=0.01, min_active_fraction: float=0.05, min_curvature_active_fraction: float | None=None, active_target_count: int | None=None, summary: dict | None=None) -> tuple[bool, dict, dict]:
    if summary is None:
        summary = seed_pool_diversity_summary(rows, variables, min_range_frac=min_range_frac, target_range_frac=target_range_frac, min_active_fraction=min_active_fraction, min_curvature_active_fraction=min_curvature_active_fraction, active_target_count=active_target_count)
    variable_count = len(variables)
    curvature_count = sum((1 for variable in variables if variable.get('kind') == 'curvature'))
    active_denominator = max(1, int(active_target_count)) if active_target_count is not None else max(1, len(rows))
    required_range_vars = max(1, int(math.ceil(0.9 * max(1, variable_count))))
    required_active_vars = max(1, int(math.ceil(0.9 * max(1, variable_count))))
    if min_active_fraction <= 0.0:
        required_active_vars = 0
    required_curvature_active_vars = curvature_count if curvature_count > 0 and (min_curvature_active_fraction if min_curvature_active_fraction is not None else min_active_fraction) > 0.0 else 0
    required_curvature_masks = 0
    required_median_active_curvature = 0
    if curvature_count > 0:
        required_curvature_masks = max(4, min(32, 4 * curvature_count))
        required_median_active_curvature = min(2, curvature_count)
    required_median_active = max(1, min(4, int(math.ceil(0.25 * max(1, variable_count)))))
    mandatory_vignetting = mandatory_vignetting_seed_diversity_requirements(summary, variables, target_range_frac=summary.get('diversity_target_range_frac', target_range_frac), require_active_ready=min_active_fraction > 0.0, minimum_active_count=1)
    requirements = {'variables_with_range_frac_ge_0_01': required_range_vars, 'variables_with_active_frac_ge_min': required_active_vars, 'curvature_variables_with_active_frac_ge_min': required_curvature_active_vars, 'median_active_vars': required_median_active, 'median_active_curvature_vars': required_median_active_curvature, 'unique_curvature_masks': required_curvature_masks, 'min_active_fraction': summary.get('diversity_min_active_fraction', min_active_fraction), 'min_curvature_active_fraction': summary.get('diversity_min_curvature_active_fraction', min_curvature_active_fraction if min_curvature_active_fraction is not None else min_active_fraction), 'active_fraction_denominator': active_denominator, 'active_fraction_uses_target_count': active_target_count is not None, **mandatory_vignetting}
    ready = summary.get('variables_with_range_frac_ge_target', 0) >= required_range_vars and summary.get('variables_with_active_frac_ge_min', 0) >= required_active_vars and (summary.get('curvature_variables_with_active_frac_ge_min', 0) >= required_curvature_active_vars) and (summary.get('median_active_vars', 0) >= required_median_active) and (summary.get('median_active_curvature_vars', 0) >= required_median_active_curvature) and (summary.get('unique_curvature_masks', 0) >= required_curvature_masks) and (not mandatory_vignetting['missing_mandatory_vignetting_range_indices']) and (not mandatory_vignetting['missing_mandatory_vignetting_active_indices'])
    return (ready, summary, requirements)
