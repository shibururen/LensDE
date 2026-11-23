"""Allocate, sample and project TTL/BFL thickness budgets without changing draw order."""
from __future__ import annotations
import math
import random
from .. import configuration as _lensde_configuration
from .. import geometry as _lensde_geometry
from . import coordinates as _seed_coordinates
from . import domains as _seed_domains

# Resolve the physical budget before allocation; preserve retry and contraction draw order.
def fixed_track_length(system: dict, variables: list[dict]) -> float:
    variable_keys = {_lensde_geometry.variable_key(v) for v in variables if v.get('kind') == 'thickness'}
    total = 0.0
    surfaces = system.get('surfaces', []) or []
    first = 1 if surfaces and str(surfaces[0].get('kind', '')).lower() == 'object' else 0
    for index in range(first, max(first, len(surfaces) - 1)):
        surface = surfaces[index]
        if ('thickness', index, 0) in variable_keys:
            continue
        total += max(0.0, _lensde_configuration.finite_float(surface.get('thickness'), 0.0))
    return total


def ttl_seed_thickness_budget_config(system: dict, variables: list[dict], target_ttl: float, target_bfl: float, target_efl: float=0.0, *, enabled: bool=True, trigger_ratio: float=1.15, compact_slack_fraction: float=0.55, lower_factor: float=0.9, upper_factor: float=1.03, trace_upper_factor: float=1.2, allow_short_seed: bool=True, seed_max_factor: float=1.0, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5) -> dict:
    thickness_vars = [variable for variable in variables if variable.get('kind') == 'thickness']
    target_total = _lensde_configuration.finite_float(target_ttl, 0.0)
    fixed_total = fixed_track_length(system, variables)
    lower_factor = _seed_coordinates.clamp_value(_lensde_configuration.finite_float(lower_factor, 0.9), 0.0, 2.0)
    upper_factor = max(1.0, _lensde_configuration.finite_float(upper_factor, 1.03))
    trace_upper_factor = max(upper_factor, _lensde_configuration.finite_float(trace_upper_factor, 1.2))
    trigger_ratio = max(1.0, _lensde_configuration.finite_float(trigger_ratio, 1.15))
    compact_slack_fraction = _seed_coordinates.clamp_value(_lensde_configuration.finite_float(compact_slack_fraction, 0.55), 0.0, 2.0)
    seed_max_factor = max(1e-09, _lensde_configuration.finite_float(seed_max_factor, 1.0))
    summary = {'enabled': False, 'feasible': False, 'requested': bool(enabled), 'reason': '', 'target_ttl': target_total, 'target_bfl': _lensde_configuration.finite_float(target_bfl, 0.0), 'target_efl': _lensde_configuration.finite_float(target_efl, 0.0), 'fixed_track': fixed_total, 'lower_total': fixed_total, 'upper_total': fixed_total, 'initial_total': fixed_total, 'initial_over_target_ratio': 0.0, 'slack_fraction': 0.0, 'compact': False, 'overlong_initial': False, 'lower_factor': lower_factor, 'upper_factor': upper_factor, 'trace_upper_factor': trace_upper_factor, 'active_upper_factor': upper_factor, 'allow_short_seed': bool(allow_short_seed), 'seed_max_factor': seed_max_factor, 'seed_target_lower': fixed_total, 'seed_target_upper': fixed_total, 'trigger_ratio': trigger_ratio, 'compact_slack_fraction': compact_slack_fraction, 'bfl_surface_index': _lensde_geometry.resolve_bfl_reference_surface(system, bfl_surface_index), 'bfl_interval_indices': sorted(_lensde_geometry.bfl_interval_indices(system, bfl_surface_index)), 'bfl_lower_factor': _seed_coordinates.clamp_value(_lensde_configuration.finite_float(bfl_lower_factor, 0.9), 0.0, 1.0), 'bfl_upper_factor': max(1.0, _lensde_configuration.finite_float(bfl_upper_factor, 1.5)), 'bfl_fixed_track': 0.0, 'bfl_feasible_lower': 0.0, 'bfl_feasible_upper': 0.0, 'bfl_target_lower': 0.0, 'bfl_target_upper': 0.0}
    if not enabled:
        summary['reason'] = 'disabled'
        return summary
    if target_total <= 0.0:
        summary['reason'] = 'no_target_ttl'
        return summary
    if not thickness_vars:
        summary['reason'] = 'no_thickness_variables'
        return summary
    bfl_indices = _lensde_geometry.bfl_interval_indices(system, bfl_surface_index)
    thickness_variable_indices = {int(variable.get('index', 0) or 0) for variable in thickness_vars}
    bfl_fixed_track = sum((max(0.0, _lensde_configuration.finite_float((system.get('surfaces', []) or [])[index].get('thickness'), 0.0)) for index in bfl_indices if index not in thickness_variable_indices and 0 <= index < len(system.get('surfaces', []) or [])))
    lower_sum = 0.0
    upper_sum = 0.0
    initial_sum = 0.0
    bfl_variable_lower = 0.0
    bfl_variable_upper = 0.0
    for variable in thickness_vars:
        lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
        hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
        if lo > hi:
            lo, hi = (hi, lo)
        current = _seed_coordinates.clamp_value(_seed_coordinates.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), lo)), lo, hi)
        lower_sum += lo
        upper_sum += hi
        initial_sum += current
        if _seed_domains.is_bfl_variable(system, variable, bfl_surface_index):
            bfl_variable_lower += lo
            bfl_variable_upper += hi
    bfl_feasible_lower = bfl_fixed_track + bfl_variable_lower
    bfl_feasible_upper = bfl_fixed_track + bfl_variable_upper
    if summary['target_bfl'] > 0.0:
        bfl_target_lower = max(bfl_feasible_lower, summary['target_bfl'] * summary['bfl_lower_factor'])
        bfl_target_upper = min(bfl_feasible_upper, summary['target_bfl'] * summary['bfl_upper_factor'])
    else:
        bfl_target_lower = bfl_feasible_lower
        bfl_target_upper = bfl_feasible_upper
    summary.update({'bfl_fixed_track': bfl_fixed_track, 'bfl_feasible_lower': bfl_feasible_lower, 'bfl_feasible_upper': bfl_feasible_upper, 'bfl_target_lower': bfl_target_lower, 'bfl_target_upper': bfl_target_upper})
    if bfl_target_upper < bfl_target_lower - 1e-09:
        summary['reason'] = 'bfl_window_infeasible'
        return summary
    lower_total = fixed_total + (lower_sum - bfl_variable_lower) + max(0.0, bfl_target_lower - bfl_fixed_track)
    upper_total = fixed_total + (upper_sum - bfl_variable_upper) + max(0.0, bfl_target_upper - bfl_fixed_track)
    initial_total = fixed_total + initial_sum
    slack_fraction = max(0.0, target_total - lower_total) / max(target_total, 1e-09)
    over_ratio = initial_total / max(target_total, 1e-09)
    compact = slack_fraction <= compact_slack_fraction
    overlong_initial = over_ratio >= trigger_ratio
    active_upper_factor = upper_factor if compact else trace_upper_factor
    summary.update({'lower_total': lower_total, 'upper_total': upper_total, 'initial_total': initial_total, 'initial_over_target_ratio': over_ratio, 'slack_fraction': slack_fraction, 'compact': compact, 'overlong_initial': overlong_initial, 'active_upper_factor': active_upper_factor})
    seed_target_lower = lower_total if bool(allow_short_seed) else max(lower_total, target_total * lower_factor)
    seed_target_upper = min(upper_total, target_total * (seed_max_factor if bool(allow_short_seed) else active_upper_factor))
    summary.update({'seed_target_lower': seed_target_lower, 'seed_target_upper': seed_target_upper})
    if seed_target_upper < seed_target_lower - 1e-09:
        summary['reason'] = 'lower_total_above_seed_window'
        return summary
    summary['feasible'] = True
    if bool(allow_short_seed):
        summary['enabled'] = True
        summary['reason'] = 'short_seed_window'
        return summary
    if upper_total < target_total * lower_factor:
        summary['reason'] = 'upper_total_below_window'
        return summary
    if not compact and (not overlong_initial):
        summary['reason'] = 'trace_first_not_compact'
        return summary
    summary['enabled'] = True
    summary['reason'] = 'compact' if compact else 'overlong_initial'
    return summary


def project_thickness_values_to_ttl_budget(system: dict, variables: list[dict], values: dict[tuple[str, int, int], float], config: dict, rng: random.Random, mutable_keys: set[tuple[str, int, int]] | None=None, uniform_non_bfl_when_contracting: bool=False) -> bool:
    if not bool(config.get('enabled')):
        return False
    target_ttl = _lensde_configuration.finite_float(config.get('target_ttl'), 0.0)
    if target_ttl <= 0.0:
        return False
    thickness_vars = [variable for variable in variables if variable.get('kind') == 'thickness']
    if not thickness_vars:
        return False
    entries = []
    target_bfl = _lensde_configuration.finite_float(config.get('target_bfl'), 0.0)
    fixed_total = _lensde_configuration.finite_float(config.get('fixed_track'), fixed_track_length(system, variables))
    current_total = fixed_total
    lower_total = fixed_total
    upper_total = fixed_total
    bfl_entry_lower = 0.0
    bfl_entry_upper = 0.0
    for variable in thickness_vars:
        key = _lensde_geometry.variable_key(variable)
        lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
        hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
        if lo > hi:
            lo, hi = (hi, lo)
        is_bfl = _seed_domains.is_bfl_variable(system, variable, config.get('bfl_surface_index'))
        current = _seed_coordinates.clamp_value(values.get(key, _seed_coordinates.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), lo))), lo, hi)
        index = int(variable.get('index', 0) or 0)
        entry = {'key': key, 'index': index, 'lower': lo, 'upper': hi, 'value': current, 'is_bfl': is_bfl, 'is_glass': _seed_domains.surface_after_is_glass(system, index)}
        entries.append(entry)
        current_total += current
        lower_total += lo
        upper_total += hi
        if is_bfl:
            bfl_entry_lower += lo
            bfl_entry_upper += hi
    changed = False
    bfl_entries = [entry for entry in entries if entry['is_bfl']]
    bfl_fixed = _lensde_configuration.finite_float(config.get('bfl_fixed_track'), 0.0)
    bfl_low = _lensde_configuration.finite_float(config.get('bfl_target_lower'), bfl_fixed)
    bfl_high = _lensde_configuration.finite_float(config.get('bfl_target_upper'), bfl_low)
    lower_total = lower_total - bfl_entry_lower + max(0.0, bfl_low - bfl_fixed)
    upper_total = upper_total - bfl_entry_upper + max(0.0, bfl_high - bfl_fixed)
    if bfl_entries and bfl_high >= bfl_low - 1e-09:
        bfl_current = bfl_fixed + sum((entry['value'] for entry in bfl_entries))
        if not bfl_low <= bfl_current <= bfl_high:
            bfl_target_total = rng.uniform(bfl_low, bfl_high) if bfl_high > bfl_low else bfl_low
            expand_bfl = bfl_current < bfl_target_total
            remaining_bfl = abs(bfl_target_total - bfl_current)
            active_bfl = list(bfl_entries)
            for _pass in range(8):
                if remaining_bfl <= 1e-09 or not active_bfl:
                    break
                rooms = [max(0.0, entry['upper'] - entry['value'] if expand_bfl else entry['value'] - entry['lower']) for entry in active_bfl]
                room_sum = sum(rooms)
                if room_sum <= 1e-12:
                    break
                next_active = []
                consumed = 0.0
                for entry, room in zip(active_bfl, rooms, strict=False):
                    if room <= 1e-12:
                        continue
                    delta = min(room, remaining_bfl * room / room_sum)
                    entry['value'] += delta if expand_bfl else -delta
                    consumed += delta
                    if room > delta + 1e-09:
                        next_active.append(entry)
                remaining_bfl = max(0.0, remaining_bfl - consumed)
                active_bfl = next_active
            changed = True
            current_total = fixed_total + sum((entry['value'] for entry in entries))
    target_low = max(lower_total, _lensde_configuration.finite_float(config.get('seed_target_lower'), lower_total))
    target_high = min(upper_total, _lensde_configuration.finite_float(config.get('seed_target_upper'), target_ttl))
    if target_high < target_low - 1e-09:
        for entry in entries:
            values[entry['key']] = entry['value']
        return changed
    if target_low <= current_total <= target_high:
        for entry in entries:
            values[entry['key']] = entry['value']
        return changed
    if current_total > target_high:
        target_total = target_high

        def scale_group_down(candidates: list[dict], desired_sum: float) -> None:
            if not candidates:
                return
            original = [entry['value'] for entry in candidates]
            current_sum = sum(original)
            lower_sum = sum((entry['lower'] for entry in candidates))
            desired = _seed_coordinates.clamp_value(desired_sum, lower_sum, current_sum)
            if desired >= current_sum - 1e-12:
                return
            if desired <= lower_sum + 1e-12:
                for entry in candidates:
                    entry['value'] = entry['lower']
                return
            scale_lo = 0.0
            scale_hi = 1.0
            for _ in range(80):
                scale = 0.5 * (scale_lo + scale_hi)
                scaled_sum = sum((max(entry['lower'], value * scale) for entry, value in zip(candidates, original, strict=False)))
                if scaled_sum < desired:
                    scale_lo = scale
                else:
                    scale_hi = scale
            scale = 0.5 * (scale_lo + scale_hi)
            for entry, value in zip(candidates, original, strict=False):
                entry['value'] = max(entry['lower'], value * scale)

        def equalize_group_to_sum(candidates: list[dict], desired_sum: float) -> None:
            """Allocate one common axial interval, respecting hard bounds."""
            if not candidates:
                return
            lower_sum = sum((entry['lower'] for entry in candidates))
            upper_sum = sum((entry['upper'] for entry in candidates))
            desired = _seed_coordinates.clamp_value(desired_sum, lower_sum, upper_sum)
            level_lo = min((entry['lower'] for entry in candidates))
            level_hi = max((entry['upper'] for entry in candidates))
            for _ in range(80):
                level = 0.5 * (level_lo + level_hi)
                allocated = sum((_seed_coordinates.clamp_value(level, entry['lower'], entry['upper']) for entry in candidates))
                if allocated < desired:
                    level_lo = level
                else:
                    level_hi = level
            level = 0.5 * (level_lo + level_hi)
            for entry in candidates:
                entry['value'] = _seed_coordinates.clamp_value(level, entry['lower'], entry['upper'])
        non_bfl_entries = [entry for entry in entries if not entry['is_bfl']]
        bfl_entries = [entry for entry in entries if entry['is_bfl']]
        current_bfl_entries = sum((entry['value'] for entry in bfl_entries))
        desired_non_bfl = target_total - fixed_total - current_bfl_entries
        non_bfl_lower = sum((entry['lower'] for entry in non_bfl_entries))
        if uniform_non_bfl_when_contracting:
            equalize_group_to_sum(non_bfl_entries, desired_non_bfl)
        else:
            scale_group_down(non_bfl_entries, desired_non_bfl)
        if desired_non_bfl < non_bfl_lower - 1e-12:
            desired_bfl = target_total - fixed_total - non_bfl_lower
            scale_group_down(bfl_entries, desired_bfl)
        for entry in entries:
            values[entry['key']] = _seed_coordinates.clamp_value(entry['value'], entry['lower'], entry['upper'])
        return True
    target_total = rng.uniform(target_low, target_high) if target_high > target_low else target_low
    expand = current_total < target_low
    remaining = abs(target_total - current_total)
    if remaining <= 1e-09:
        for entry in entries:
            values[entry['key']] = entry['value']
        return changed
    mutable_keys = set(mutable_keys or [])
    preferred = [entry for entry in entries if entry['key'] in mutable_keys]
    preferred_ids = {id(entry) for entry in preferred}
    remaining_entries = [entry for entry in entries if id(entry) not in preferred_ids]

    def grouped(candidates: list[dict]) -> list[list[dict]]:
        return [[entry for entry in candidates if not entry['is_bfl'] and (not entry['is_glass'])], [entry for entry in candidates if not entry['is_bfl'] and entry['is_glass']], [entry for entry in candidates if entry['is_bfl']]]
    groups = grouped(preferred) + grouped(remaining_entries) if preferred else grouped(entries)

    def distribute(candidates: list[dict], amount: float, *, expand_values: bool, amount_cap: float | None=None) -> float:
        remaining_amount = amount
        distributable = remaining_amount if amount_cap is None else min(remaining_amount, max(0.0, amount_cap))
        undistributable = remaining_amount - distributable
        remaining_amount = distributable
        active = list(candidates)
        for _pass in range(6):
            weighted = []
            for entry in active:
                room = entry['upper'] - entry['value'] if expand_values else entry['value'] - entry['lower']
                if room > 1e-09:
                    weighted.append((entry, room, room * (0.35 + rng.random())))
            if remaining_amount <= 1e-09 or not weighted:
                break
            weight_sum = sum((weight for _entry, _room, weight in weighted))
            if weight_sum <= 0.0:
                break
            consumed = 0.0
            next_active = []
            for entry, room, weight in weighted:
                delta = min(room, remaining_amount * weight / weight_sum)
                if expand_values:
                    entry['value'] += delta
                else:
                    entry['value'] -= delta
                consumed += delta
                if room > delta + 1e-09:
                    next_active.append(entry)
            remaining_amount = max(0.0, remaining_amount - consumed)
            active = next_active
        return undistributable + remaining_amount
    for group in groups:
        group_is_bfl = bool(group) and all((entry['is_bfl'] for entry in group))
        amount_cap = None
        if group_is_bfl:
            bfl_value = bfl_fixed + sum((entry['value'] for entry in bfl_entries))
            amount_cap = bfl_high - bfl_value if expand else bfl_value - bfl_low
        remaining = distribute(group, remaining, expand_values=expand, amount_cap=amount_cap)
        if remaining <= 1e-09:
            break
    for entry in entries:
        values[entry['key']] = _seed_coordinates.clamp_value(entry['value'], entry['lower'], entry['upper'])
    return True


def canonical_initial_thickness_sampling_mode(value: object) -> str:
    mode = str(value or 'legacy').strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'budget': 'budget_simplex', 'simplex': 'budget_simplex', 'thickness_budget': 'budget_simplex', 'thickness_budget_simplex': 'budget_simplex', 'legacy_projector': 'legacy'}
    mode = aliases.get(mode, mode)
    return mode if mode in {'legacy', 'budget_simplex'} else 'legacy'


def allocate_bounded_thickness_total(variables: list[dict], total: float, rng: random.Random, alpha: float) -> dict[tuple[str, int, int], float] | None:
    """Allocate an exact total inside member bounds without boundary clipping.

    Gamma weights produce a Dirichlet allocation.  Members that reach an upper
    bound are removed and the unallocated remainder is redistributed among the
    others.  Returning ``None`` keeps infeasible geometry out of the population
    without turning it into a pile of clipped lower/upper-bound coordinates.
    """
    if not variables:
        return {} if abs(_lensde_configuration.finite_float(total, 0.0)) <= 1e-09 else None
    bounds = [_seed_coordinates.variable_bounds(variable) for variable in variables]
    lower_total = sum((lower for lower, _upper in bounds))
    upper_total = sum((upper for _lower, upper in bounds))
    target = _lensde_configuration.finite_float(total, float('nan'))
    tolerance = 1e-09 * max(1.0, abs(lower_total), abs(upper_total), abs(target))
    if not math.isfinite(target) or target < lower_total - tolerance or target > upper_total + tolerance:
        return None
    values = [lower for lower, _upper in bounds]
    rooms = [max(0.0, upper - lower) for lower, upper in bounds]
    remaining = max(0.0, target - lower_total)
    concentration = max(1e-06, _lensde_configuration.finite_float(alpha, 2.0))
    weights = [rng.gammavariate(concentration, 1.0) if room > 1e-12 else 0.0 for room in rooms]
    active = [index for index, room in enumerate(rooms) if room > 1e-12]
    while remaining > tolerance and active:
        weight_sum = sum((weights[index] for index in active))
        if weight_sum <= 1e-15:
            for index in active:
                weights[index] = 1.0
            weight_sum = float(len(active))
        proposals = {index: remaining * weights[index] / weight_sum for index in active}
        saturated = [index for index in active if proposals[index] >= rooms[index] - tolerance]
        if not saturated:
            for index in active:
                addition = proposals[index]
                values[index] += addition
                rooms[index] -= addition
            remaining = 0.0
            break
        for index in saturated:
            addition = rooms[index]
            values[index] += addition
            remaining -= addition
            rooms[index] = 0.0
        active = [index for index in active if index not in saturated]
    if remaining > tolerance:
        return None
    if values:
        residual = target - sum(values)
        if abs(residual) > 0.0:
            candidates = [index for index, (value, (_lower, upper)) in enumerate(zip(values, bounds, strict=False)) if value + residual <= upper + tolerance and value + residual >= bounds[index][0] - tolerance]
            if candidates:
                values[candidates[0]] += residual
    return {_lensde_geometry.variable_key(variable): value for variable, value in zip(variables, values, strict=False)}


def sample_budget_simplex_thickness_values(system: dict, variables: list[dict], rng: random.Random, target_ttl: float, target_bfl: float, *, ttl_quantile: float | None=None, bfl_quantile: float | None=None, ttl_lower_fraction: float=0.9, ttl_upper_fraction: float=1.0, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.0, bfl_ttl_upper_fraction: float=0.0, glass_beta_alpha: float=2.0, glass_beta_beta: float=4.0, air_dirichlet_alpha: float=2.0, bfl_surface_index: int | None=None, max_attempts: int=256) -> tuple[dict[tuple[str, int, int], float], dict]:
    """Sample a mechanically valid thickness architecture with no optical gate.

    A bounded beta prior chooses the total glass budget that is feasible for
    the sampled TTL/BFL row; bounded Dirichlet simplexes then distribute the
    glass and internal-air budgets without hard clipping.  When
    ``bfl_ttl_upper_fraction`` is positive, it replaces the target-BFL upper
    factor with a per-row upper bound derived from that row's sampled TTL.
    Curvature, material, conic and asphere variables are deliberately outside
    this helper.
    """
    thickness_variables = [variable for variable in variables if _seed_domains.is_thickness_variable(variable)]
    if not thickness_variables:
        return ({}, {'mode': 'budget_simplex', 'target_total': 0.0, 'target_bfl': 0.0, 'fixed_non_bfl': 0.0, 'fixed_bfl': 0.0})
    surfaces = system.get('surfaces', []) or []
    bfl_indices = _lensde_geometry.bfl_interval_indices(system, bfl_surface_index)
    thickness_by_index = {int(variable.get('index', 0) or 0): variable for variable in thickness_variables}
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
    glass_variables = [variable for variable in thickness_variables if variable not in bfl_variables and _seed_domains.is_glass_thickness_variable(system, variable)]
    air_variables = [variable for variable in thickness_variables if variable not in bfl_variables and _seed_domains.is_air_thickness_variable(system, variable)]
    ttl_lo_fraction = max(0.0, _lensde_configuration.finite_float(ttl_lower_fraction, 0.9))
    ttl_hi_fraction = max(ttl_lo_fraction, _lensde_configuration.finite_float(ttl_upper_fraction, 1.0))
    ttl_q = _seed_coordinates.clamp_value(rng.random() if ttl_quantile is None else _lensde_configuration.finite_float(ttl_quantile, 0.5), 0.0, 1.0)
    if target_ttl > 0.0:
        sampled_total = target_ttl * (ttl_lo_fraction + (ttl_hi_fraction - ttl_lo_fraction) * ttl_q)
    else:
        sampled_total = fixed_non_bfl + fixed_bfl + sum((_seed_coordinates.variable_current_value_for_system(system, variable) for variable in thickness_variables))
    bfl_member_lower = sum((_seed_coordinates.variable_bounds(variable)[0] for variable in bfl_variables))
    bfl_member_upper = sum((_seed_coordinates.variable_bounds(variable)[1] for variable in bfl_variables))
    feasible_bfl_lower = fixed_bfl + bfl_member_lower
    feasible_bfl_upper = fixed_bfl + bfl_member_upper
    ttl_bfl_fraction = max(0.0, _lensde_configuration.finite_float(bfl_ttl_upper_fraction, 0.0))
    if target_bfl > 0.0:
        requested_bfl_lower = target_bfl * max(0.0, _lensde_configuration.finite_float(bfl_lower_factor, 0.9))
        if ttl_bfl_fraction > 0.0:
            requested_bfl_upper = sampled_total * ttl_bfl_fraction
        else:
            requested_bfl_upper = target_bfl * max(max(0.0, _lensde_configuration.finite_float(bfl_lower_factor, 0.9)), _lensde_configuration.finite_float(bfl_upper_factor, 1.0))
        sampled_bfl_lower = max(feasible_bfl_lower, requested_bfl_lower)
        sampled_bfl_upper = min(feasible_bfl_upper, requested_bfl_upper)
    else:
        sampled_bfl_lower = feasible_bfl_lower
        sampled_bfl_upper = feasible_bfl_upper
    non_bfl_member_lower = sum((_seed_coordinates.variable_bounds(variable)[0] for variable in glass_variables + air_variables))
    non_bfl_member_upper = sum((_seed_coordinates.variable_bounds(variable)[1] for variable in glass_variables + air_variables))
    sampled_bfl_lower = max(sampled_bfl_lower, sampled_total - fixed_non_bfl - non_bfl_member_upper)
    sampled_bfl_upper = min(sampled_bfl_upper, sampled_total - fixed_non_bfl - non_bfl_member_lower)
    tolerance = 1e-09 * max(1.0, abs(sampled_total), abs(target_bfl))
    if sampled_bfl_lower > sampled_bfl_upper + tolerance:
        raise ValueError(f'budget-simplex BFL window is infeasible: requested=[{sampled_bfl_lower:.12g},{sampled_bfl_upper:.12g}], member_feasible=[{feasible_bfl_lower:.12g},{feasible_bfl_upper:.12g}]')
    bfl_q = _seed_coordinates.clamp_value(rng.random() if bfl_quantile is None else _lensde_configuration.finite_float(bfl_quantile, 0.5), 0.0, 1.0)
    sampled_bfl = sampled_bfl_lower + (sampled_bfl_upper - sampled_bfl_lower) * bfl_q
    glass_alpha = max(1e-06, _lensde_configuration.finite_float(glass_beta_alpha, 2.0))
    glass_beta = max(1e-06, _lensde_configuration.finite_float(glass_beta_beta, 4.0))
    glass_member_lower = sum((_seed_coordinates.variable_bounds(variable)[0] for variable in glass_variables))
    glass_member_upper = sum((_seed_coordinates.variable_bounds(variable)[1] for variable in glass_variables))
    air_member_lower = sum((_seed_coordinates.variable_bounds(variable)[0] for variable in air_variables))
    air_member_upper = sum((_seed_coordinates.variable_bounds(variable)[1] for variable in air_variables))
    available_internal = sampled_total - fixed_non_bfl - sampled_bfl
    feasible_glass_lower = max(glass_member_lower, available_internal - air_member_upper)
    feasible_glass_upper = min(glass_member_upper, available_internal - air_member_lower)
    if feasible_glass_lower > feasible_glass_upper + tolerance:
        raise ValueError(f'budget-simplex glass/air window is infeasible: glass=[{feasible_glass_lower:.12g},{feasible_glass_upper:.12g}], available_internal={available_internal:.12g}')
    glass_fraction = rng.betavariate(glass_alpha, glass_beta)
    sampled_glass_total = feasible_glass_lower + (feasible_glass_upper - feasible_glass_lower) * glass_fraction
    attempts = max(1, int(max_attempts))
    for attempt in range(attempts):
        glass_values = allocate_bounded_thickness_total(glass_variables, sampled_glass_total, rng, glass_alpha)
        if glass_values is None:
            continue
        bfl_values = allocate_bounded_thickness_total(bfl_variables, sampled_bfl - fixed_bfl, rng, air_dirichlet_alpha)
        if bfl_values is None:
            continue
        glass_total = sum(glass_values.values())
        internal_air_total = available_internal - glass_total
        air_values = allocate_bounded_thickness_total(air_variables, internal_air_total, rng, air_dirichlet_alpha)
        if air_values is None:
            continue
        values = {**glass_values, **air_values, **bfl_values}
        actual_total = fixed_non_bfl + fixed_bfl + sum(values.values())
        return (values, {'mode': 'budget_simplex', 'attempt': attempt + 1, 'target_total': sampled_total, 'actual_total': actual_total, 'target_bfl': sampled_bfl, 'fixed_non_bfl': fixed_non_bfl, 'fixed_bfl': fixed_bfl, 'glass_total': glass_total, 'glass_budget_lower': feasible_glass_lower, 'glass_budget_upper': feasible_glass_upper, 'glass_budget_fraction': glass_fraction, 'internal_air_total': sum(air_values.values()), 'bfl_variable_total': sum(bfl_values.values()), 'glass_variable_count': len(glass_variables), 'air_variable_count': len(air_variables), 'bfl_variable_count': len(bfl_variables)})
    raise ValueError(f'budget-simplex thickness sampling exhausted attempts: attempts={attempts}, sampled_total={sampled_total:.12g}, sampled_bfl={sampled_bfl:.12g}')
