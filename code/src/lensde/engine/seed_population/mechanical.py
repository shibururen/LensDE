"""Mechanical stratified seed policy, BFL domains and balanced shape coverage."""
from __future__ import annotations
import copy
import math
import random
from .. import state
from .. import configuration as _lensde_configuration
from .. import geometry as _lensde_geometry
from . import coordinates as _seed_coordinates
from . import domains as _seed_domains

# Mechanical generation keeps the hybrid call late-bound through its explicit callback.
def mechanical_stratified_budget_summary(system: dict, variables: list[dict], target_ttl: float, target_bfl: float, *, custom_ttl_range_enabled: bool=False, ttl_lower_fraction: float=0.7, ttl_upper_fraction: float=1.5, bfl_lower_factor: float=0.9, bfl_ttl_upper_fraction: float=0.5, bfl_surface_index: int | None=None) -> dict:
    """Derive the effective baseline TTL interval from UI and mechanical bounds."""
    thickness_variables = [variable for variable in variables if _seed_domains.is_thickness_variable(variable)]
    thickness_by_index = {int(variable.get('index', 0) or 0): variable for variable in thickness_variables}
    bfl_indices = _lensde_geometry.bfl_interval_indices(system, bfl_surface_index)
    surfaces = system.get('surfaces', []) or []
    fixed_non_bfl = 0.0
    fixed_bfl = 0.0
    for index, surface in enumerate(surfaces[:-1]):
        if index in thickness_by_index:
            continue
        thickness = max(0.0, _lensde_configuration.finite_float(surface.get('thickness'), 0.0))
        if index in bfl_indices:
            fixed_bfl += thickness
        else:
            fixed_non_bfl += thickness
    bfl_variables = [variable for variable in thickness_variables if int(variable.get('index', 0) or 0) in bfl_indices]
    non_bfl_variables = [variable for variable in thickness_variables if variable not in bfl_variables]
    bfl_member_lower = sum((_seed_coordinates.variable_bounds(variable)[0] for variable in bfl_variables))
    bfl_member_upper = sum((_seed_coordinates.variable_bounds(variable)[1] for variable in bfl_variables))
    non_bfl_member_lower = sum((_seed_coordinates.variable_bounds(variable)[0] for variable in non_bfl_variables))
    non_bfl_member_upper = sum((_seed_coordinates.variable_bounds(variable)[1] for variable in non_bfl_variables))
    requested_bfl_lower = max(0.0, target_bfl * max(0.0, _lensde_configuration.finite_float(bfl_lower_factor, 0.9))) if target_bfl > 0.0 else fixed_bfl + bfl_member_lower
    feasible_bfl_lower = max(fixed_bfl + bfl_member_lower, requested_bfl_lower)
    feasible_bfl_upper = fixed_bfl + bfl_member_upper
    fraction = _seed_coordinates.clamp_value(_lensde_configuration.finite_float(bfl_ttl_upper_fraction, 0.5), 0.0, 1.0)
    mechanical_ttl_lower = fixed_non_bfl + non_bfl_member_lower + feasible_bfl_lower
    if fraction > 0.0:
        mechanical_ttl_lower = max(mechanical_ttl_lower, feasible_bfl_lower / fraction)
    mechanical_ttl_upper = fixed_non_bfl + non_bfl_member_upper + feasible_bfl_upper
    if 0.0 < fraction < 1.0:
        fraction_limited_upper = (fixed_non_bfl + non_bfl_member_upper) / (1.0 - fraction)
        mechanical_ttl_upper = min(mechanical_ttl_upper, fraction_limited_upper)
    requested_ttl_lower_fraction = max(0.0, _lensde_configuration.finite_float(ttl_lower_fraction, 0.7))
    requested_ttl_upper_fraction = max(requested_ttl_lower_fraction, _lensde_configuration.finite_float(ttl_upper_fraction, 1.5))
    custom_range = bool(custom_ttl_range_enabled)
    if target_ttl <= 0.0:
        effective_ttl_lower = mechanical_ttl_lower
        effective_ttl_upper = mechanical_ttl_lower
        effective_ttl_lower_fraction = 1.0
        effective_ttl_upper_fraction = 1.0
        requested_ttl_lower = None
        requested_ttl_upper = None
    else:
        requested_ttl_lower = target_ttl * requested_ttl_lower_fraction
        requested_ttl_upper = target_ttl * requested_ttl_upper_fraction
        effective_ttl_lower = max(mechanical_ttl_lower, requested_ttl_lower if custom_range else mechanical_ttl_lower)
        effective_ttl_upper = min(mechanical_ttl_upper, requested_ttl_upper if custom_range else target_ttl)
        effective_ttl_lower_fraction = effective_ttl_lower / target_ttl
        effective_ttl_upper_fraction = effective_ttl_upper / target_ttl
        if effective_ttl_lower > effective_ttl_upper + 1e-09:
            raise ValueError(f'mechanical-stratified TTL window is infeasible: effective=[{effective_ttl_lower:.12g},{effective_ttl_upper:.12g}], mechanical=[{mechanical_ttl_lower:.12g},{mechanical_ttl_upper:.12g}], requested=[{requested_ttl_lower:.12g},{requested_ttl_upper:.12g}]')
    if feasible_bfl_lower > feasible_bfl_upper + 1e-09:
        raise ValueError(f'mechanical-stratified BFL lower bound is outside the seed domain: lower={feasible_bfl_lower:.12g}, upper={feasible_bfl_upper:.12g}')
    return {'ttl_custom_range_enabled': custom_range, 'ttl_requested_lower': requested_ttl_lower, 'ttl_requested_upper': requested_ttl_upper, 'ttl_requested_lower_fraction': requested_ttl_lower_fraction, 'ttl_requested_upper_fraction': requested_ttl_upper_fraction, 'ttl_mechanical_lower': mechanical_ttl_lower, 'ttl_mechanical_upper': mechanical_ttl_upper, 'ttl_lower': effective_ttl_lower, 'ttl_upper': effective_ttl_upper, 'ttl_lower_fraction': effective_ttl_lower_fraction, 'ttl_upper_fraction': effective_ttl_upper_fraction, 'bfl_lower': feasible_bfl_lower, 'bfl_member_feasible_upper': feasible_bfl_upper, 'bfl_ttl_upper_fraction': fraction, 'fixed_non_bfl': fixed_non_bfl, 'fixed_bfl': fixed_bfl, 'thickness_variable_count': len(thickness_variables), 'bfl_variable_count': len(bfl_variables)}


def expand_mechanical_stratified_bfl_seed_domain(system: dict, seed_variables: list[dict], optimization_preview_variables: list[dict], target_ttl: float, *, bfl_ttl_upper_fraction: float=0.5, bfl_surface_index: int | None=None) -> tuple[list[dict], list[dict]]:
    """Let baseline BFL sampling use the later optimizer envelope, up to its TTL cap."""
    out = copy.deepcopy(seed_variables)
    bfl_indices = _lensde_geometry.bfl_interval_indices(system, bfl_surface_index)
    optimizer_by_key = {_lensde_geometry.variable_key(variable): variable for variable in optimization_preview_variables}
    bfl_variables = [variable for variable in out if _seed_domains.is_thickness_variable(variable) and int(variable.get('index', 0) or 0) in bfl_indices]
    surfaces = system.get('surfaces', []) or []
    variable_indices = {int(variable.get('index', 0) or 0) for variable in bfl_variables}
    fixed_bfl = sum((max(0.0, _lensde_configuration.finite_float(surfaces[index].get('thickness'), 0.0)) for index in bfl_indices if 0 <= index < len(surfaces) - 1 and index not in variable_indices))
    fraction = _seed_coordinates.clamp_value(_lensde_configuration.finite_float(bfl_ttl_upper_fraction, 0.5), 0.0, 1.0)
    requested_total = max(0.0, target_ttl * fraction - fixed_bfl)
    changes: list[dict] = []
    for variable in bfl_variables:
        key = _lensde_geometry.variable_key(variable)
        optimizer_variable = optimizer_by_key.get(key)
        if optimizer_variable is None:
            continue
        old_lower, old_upper = _seed_coordinates.variable_bounds(variable)
        _optimizer_lower, optimizer_upper = _seed_coordinates.variable_bounds(optimizer_variable)
        other_lower = sum((_seed_coordinates.variable_bounds(other)[0] for other in bfl_variables if other is not variable))
        needed_upper = max(old_upper, requested_total - other_lower)
        new_upper = max(old_upper, min(optimizer_upper, needed_upper))
        variable['upper'] = new_upper
        if new_upper > old_upper + 1e-12:
            changes.append({'kind': variable.get('kind'), 'index': variable.get('index'), 'term': variable.get('term', 0), 'old_upper': old_upper, 'new_upper': new_upper, 'optimizer_upper': optimizer_upper})
    return (out, changes)


def inject_balanced_initial_shape_coverage(rows: list[list[float]], variables: list[dict], seed: int, *, conic_fraction: float=0.08, conic_abs_max: float=100.0, asphere_fraction: float=0.02, asphere_bound_fraction: float=1.0, max_active_per_row: int=4) -> dict:
    """Add explicit coordinate coverage without optical scoring or profile clouds."""
    row_count = len(rows)
    conic_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(conic_fraction, 0.08)))
    asphere_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(asphere_fraction, 0.02)))
    conic_abs_max = max(0.0, _lensde_configuration.finite_float(conic_abs_max, 100.0))
    asphere_bound_fraction = min(1.0, max(0.0, _lensde_configuration.finite_float(asphere_bound_fraction, 1.0)))
    per_row = max(1, int(max_active_per_row))
    columns_by_kind = {'conic': [index for index, variable in enumerate(variables) if str(variable.get('kind', '')).lower() == 'conic'], 'asphere_coeff': [index for index, variable in enumerate(variables) if str(variable.get('kind', '')).lower() == 'asphere_coeff']}
    targets: dict[int, int] = {}
    for kind, columns in columns_by_kind.items():
        fraction = conic_fraction if kind == 'conic' else asphere_fraction
        count = min(row_count, int(math.ceil(row_count * fraction))) if fraction > 0.0 else 0
        for column in columns:
            targets[column] = count
    requested_assignments = sum(targets.values())
    capacity = row_count * per_row
    if requested_assignments > capacity:
        raise ValueError(f'mechanical-stratified shape coverage exceeds row capacity: assignments={requested_assignments}, capacity={capacity}; reduce coverage fractions or increase max active shape variables per row')
    if not rows or requested_assignments <= 0:
        return {'enabled': False, 'conic_columns': columns_by_kind['conic'], 'asphere_columns': columns_by_kind['asphere_coeff'], 'requested_assignments': requested_assignments, 'injected_assignments': 0, 'max_active_per_row': per_row}
    rng = random.Random(int(seed))
    assignments: list[int] = []
    max_target = max(targets.values(), default=0)
    for repeat in range(max_target):
        cycle = [column for column, count in targets.items() if repeat < count]
        rng.shuffle(cycle)
        assignments.extend(cycle)
    row_order = list(range(row_count))
    rng.shuffle(row_order)
    row_rank = {row_index: rank for rank, row_index in enumerate(row_order)}
    row_active: list[set[int]] = [set() for _ in rows]
    active_counts: dict[int, int] = {column: 0 for column in targets}
    values_by_kind: dict[str, list[float]] = {'conic': [], 'asphere_coeff': []}
    skipped_zero_width: list[int] = []
    for column in assignments:
        candidates = [row_index for row_index in row_order if len(row_active[row_index]) < per_row and column not in row_active[row_index]]
        if not candidates:
            raise ValueError('mechanical-stratified shape coverage allocator exhausted row capacity')
        row_index = min(candidates, key=lambda item: (len(row_active[item]), row_rank[item]))
        variable = variables[column]
        kind = str(variable.get('kind', '')).lower()
        hard_lower, hard_upper = _seed_coordinates.variable_bounds(variable)
        initial = _seed_coordinates.clamp_value(_lensde_configuration.finite_float(variable.get('initial'), 0.0), hard_lower, hard_upper)
        if kind == 'conic':
            lower = max(hard_lower, -conic_abs_max)
            upper = min(hard_upper, conic_abs_max)
        else:
            lower = initial + (hard_lower - initial) * asphere_bound_fraction
            upper = initial + (hard_upper - initial) * asphere_bound_fraction
        tolerance = _seed_coordinates.seed_activity_tolerance(variable)
        if upper - lower <= 2.0 * tolerance:
            skipped_zero_width.append(column)
            continue
        value = initial
        for _attempt in range(64):
            value = rng.uniform(lower, upper)
            if abs(value - initial) > tolerance:
                break
        if abs(value - initial) <= tolerance:
            value = upper if abs(upper - initial) >= abs(lower - initial) else lower
        rows[row_index][column] = _seed_coordinates.clamp_value(value, hard_lower, hard_upper)
        row_active[row_index].add(column)
        active_counts[column] += 1
        values_by_kind.setdefault(kind, []).append(rows[row_index][column])
    return {'enabled': requested_assignments > 0, 'sampling': 'balanced_coordinate_uniform_no_optical_gate', 'seed': int(seed), 'conic_columns': columns_by_kind['conic'], 'asphere_columns': columns_by_kind['asphere_coeff'], 'conic_fraction': conic_fraction, 'asphere_fraction': asphere_fraction, 'conic_abs_max': conic_abs_max, 'asphere_bound_fraction': asphere_bound_fraction, 'max_active_per_row': per_row, 'requested_assignments': requested_assignments, 'injected_assignments': sum(active_counts.values()), 'active_rows': sum((bool(columns) for columns in row_active)), 'observed_max_active_per_row': max((len(columns) for columns in row_active), default=0), 'active_counts': {str(column): count for column, count in active_counts.items()}, 'skipped_zero_width_columns': sorted(set(skipped_zero_width)), 'conic_value_min': min(values_by_kind['conic']) if values_by_kind['conic'] else None, 'conic_value_max': max(values_by_kind['conic']) if values_by_kind['conic'] else None, 'asphere_value_min': min(values_by_kind['asphere_coeff']) if values_by_kind['asphere_coeff'] else None, 'asphere_value_max': max(values_by_kind['asphere_coeff']) if values_by_kind['asphere_coeff'] else None}


def _make_mechanical_stratified_direct_population(system: dict, variables: list[dict], front_pairs: list[tuple[int, int]], population: int, seed: int, target_ttl: float, target_bfl: float, target_efl: float, *, _hybrid_generator, rest_cap: float, seed_curvature_cap: float, front_profile: str='weak', perturb_material_variables: bool=True, seed_material_nd_cap: float=0.45, seed_material_vd_cap: float=50.0, bfl_surface_index: int | None=None, custom_ttl_range_enabled: bool=False, ttl_lower_fraction: float=0.7, ttl_upper_fraction: float=1.5, bfl_lower_factor: float=0.9, bfl_ttl_upper_fraction: float=0.5, glass_beta_alpha: float=2.0, glass_beta_beta: float=4.0, air_dirichlet_alpha: float=2.0, conic_coverage_fraction: float=0.08, conic_abs_max: float=100.0, asphere_coverage_fraction: float=0.02, asphere_bound_fraction: float=1.0, max_active_shape_per_row: int=4) -> tuple[list[list[float]], dict]:
    budget = mechanical_stratified_budget_summary(system, variables, target_ttl, target_bfl, custom_ttl_range_enabled=custom_ttl_range_enabled, ttl_lower_fraction=ttl_lower_fraction, ttl_upper_fraction=ttl_upper_fraction, bfl_lower_factor=bfl_lower_factor, bfl_ttl_upper_fraction=bfl_ttl_upper_fraction, bfl_surface_index=bfl_surface_index)
    rows = _hybrid_generator(system, variables, front_pairs, max(0, int(population)), int(seed), target_ttl, target_bfl, rest_cap, seed_curvature_cap, population_generation_mode='restricted_perturbation', front_profile=front_profile, seed_material_nd_cap=seed_material_nd_cap, seed_material_vd_cap=seed_material_vd_cap, local_group_perturbation=True, perturb_material_variables=perturb_material_variables, perturb_other_variables=False, target_efl=target_efl, auto_ttl_thickness_budget=False, bfl_surface_index=bfl_surface_index, thickness_sampling_mode='budget_simplex', budget_simplex_ttl_lower_fraction=budget['ttl_lower_fraction'], budget_simplex_ttl_upper_fraction=budget['ttl_upper_fraction'], budget_simplex_bfl_lower_factor=bfl_lower_factor, budget_simplex_bfl_upper_factor=1.0, budget_simplex_bfl_ttl_upper_fraction=bfl_ttl_upper_fraction, budget_simplex_glass_beta_alpha=glass_beta_alpha, budget_simplex_glass_beta_beta=glass_beta_beta, budget_simplex_air_dirichlet_alpha=air_dirichlet_alpha, apply_profiled_asphere_population=False)
    shape_coverage = inject_balanced_initial_shape_coverage(rows, variables, int(seed) + 6300, conic_fraction=conic_coverage_fraction, conic_abs_max=conic_abs_max, asphere_fraction=asphere_coverage_fraction, asphere_bound_fraction=asphere_bound_fraction, max_active_per_row=max_active_shape_per_row)
    return (rows, {'strategy': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'optical_seed_gate': 'none', 'first_order_repair': False, 'candidate_oversampling': False, 'anchor_cloud': False, 'diversity_selector': False, 'wide_angle_prior': bool(front_pairs), 'front_pairs': [list(pair) for pair in front_pairs], 'front_profile': front_profile, 'population': len(rows), 'seed': int(seed), 'budget': budget, 'glass_beta_alpha': glass_beta_alpha, 'glass_beta_beta': glass_beta_beta, 'air_dirichlet_alpha': air_dirichlet_alpha, 'shape_coverage': shape_coverage})
