"""Variable-aware physical coordinates, bounded mutations and deterministic row identities."""
from __future__ import annotations
import math
import random
from .. import configuration as _lensde_configuration
from .. import defaults as _lensde_defaults
from .. import fields as _lensde_fields
from . import domains as _seed_domains

# Row identities and distances share the same physical/q coordinate convention.
def is_profiled_asphere_seed_variable(variable: dict) -> bool:
    return bool(variable.get('initial_search_asphere_profiled', False)) and str(variable.get('kind', '')).lower() == 'asphere_coeff'


def profiled_asphere_seed_q_scale(variable: dict) -> float:
    if not is_profiled_asphere_seed_variable(variable):
        return 1.0
    radius = max(_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), 1.0), 1e-12)
    try:
        scale = radius ** asphere_order_for_term(int(variable.get('term', 0) or 0))
    except OverflowError:
        scale = math.inf
    return scale if math.isfinite(scale) and scale > 0.0 else 1.0


def _finite_seed_number(value, default: float=0.0) -> float:
    """Fast finite conversion for the numeric rows used by seed-pool hot paths."""
    if isinstance(value, float):
        return value if math.isfinite(value) else default
    if isinstance(value, (int, bool)):
        return float(value)
    return _lensde_configuration.finite_float(value, default)


def _variable_aware_seed_scales(variables: list[dict]) -> tuple[float, ...]:
    return tuple((profiled_asphere_seed_q_scale(variable) for variable in variables))


def _variable_aware_seed_row_key_from_scales(row: list[float], scales: tuple[float, ...], digits: int) -> tuple[float, ...]:
    scale_count = len(scales)
    return tuple((round(_finite_seed_number(value, 0.0) * (scales[index] if index < scale_count else 1.0), digits) for index, value in enumerate(row)))


def seed_variable_coordinate(value: float, variable: dict) -> float:
    number = _lensde_configuration.finite_float(value, 0.0)
    return number * profiled_asphere_seed_q_scale(variable)


def seed_variable_coordinate_bounds(variable: dict) -> tuple[float, float]:
    lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
    hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
    if not is_profiled_asphere_seed_variable(variable):
        return (lo, hi)
    scale = profiled_asphere_seed_q_scale(variable)
    q_lo = _lensde_configuration.finite_float(variable.get('edge_sag_lower'), lo * scale)
    q_hi = _lensde_configuration.finite_float(variable.get('edge_sag_upper'), hi * scale)
    return (q_lo, q_hi) if q_lo <= q_hi else (q_hi, q_lo)


def variable_aware_seed_row_key(row: list[float], variables: list[dict], digits: int=12) -> tuple[float, ...]:
    return _variable_aware_seed_row_key_from_scales(row, _variable_aware_seed_scales(variables), digits)


def dedupe_variable_aware_seed_rows(rows: list[list[float]], variables: list[dict], digits: int=12) -> list[list[float]]:
    scales = _variable_aware_seed_scales(variables)
    seen: set[tuple[float, ...]] = set()
    out: list[list[float]] = []
    for row in rows:
        key = _variable_aware_seed_row_key_from_scales(row, scales, digits)
        if key in seen:
            continue
        seen.add(key)
        out.append([_finite_seed_number(value, 0.0) for value in row])
    return out


def append_profiled_asphere_seed_selector_args(args: list[str], variables: list[dict]) -> None:
    if not any((is_profiled_asphere_seed_variable(variable) for variable in variables)):
        return
    args.extend(['--optimizer-internal-scaling', 'edge_sag_normalized', '--asphere-reference-policy', 'input'])


def bound_normalized_delta(x: float, y: float, variable: dict) -> float:
    if is_profiled_asphere_seed_variable(variable):
        lo, hi = seed_variable_coordinate_bounds(variable)
        span = max(1e-12, abs(hi - lo))
        return (seed_variable_coordinate(x, variable) - seed_variable_coordinate(y, variable)) / span
    lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
    hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
    span = max(1e-12, abs(hi - lo))
    return (_lensde_configuration.finite_float(x, 0.0) - _lensde_configuration.finite_float(y, 0.0)) / span


def bound_normalized_distance(a: list[float], b: list[float], variables: list[dict]) -> float:
    acc = 0.0
    n = 0
    for x, y, variable in zip(a, b, variables, strict=False):
        d = bound_normalized_delta(x, y, variable)
        acc += d * d
        n += 1
    return math.sqrt(acc / max(1, n))


def seed_activity_tolerance(variable: dict) -> float:
    if is_profiled_asphere_seed_variable(variable):
        lo, hi = seed_variable_coordinate_bounds(variable)
        span = max(1e-12, abs(hi - lo))
        return max(1e-09, 0.0001 * span)
    lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
    hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
    span = max(1e-12, abs(hi - lo))
    if str(variable.get('kind', '')) == 'curvature':
        return max(2e-05, 0.001 * span)
    return max(1e-09, 0.0001 * span)


def active_indices_from_row(row: list[float], variables: list[dict], *, reference_row: list[float] | None=None, tol: float | None=None) -> set[int]:
    out: set[int] = set()
    for index, (value, variable) in enumerate(zip(row, variables, strict=False)):
        reference = _lensde_configuration.finite_float(reference_row[index], 0.0) if reference_row is not None and index < len(reference_row) else _lensde_configuration.finite_float(variable.get('initial'), variable_current_value_for_system({}, variable, 0.0))
        threshold = seed_activity_tolerance(variable) if tol is None else tol
        if abs(seed_variable_coordinate(value, variable) - seed_variable_coordinate(reference, variable)) > threshold:
            out.add(index)
    return out


def row_curvature_mask(row: list[float], variables: list[dict], *, tol: float | None=None) -> str:
    signs: list[str] = []
    for value, variable in zip(row, variables, strict=False):
        if variable.get('kind') != 'curvature':
            continue
        threshold = seed_activity_tolerance(variable) if tol is None else tol
        number = _lensde_configuration.finite_float(value, 0.0)
        signs.append('p' if number > threshold else 'n' if number < -threshold else 'z')
    return ''.join(signs)


def seed_variable_group(system: dict, variable: dict) -> int | None:
    try:
        index = int(variable.get('index', -1))
    except (TypeError, ValueError):
        return None
    if index < 0:
        return None
    for group_id, (front, rear) in enumerate(_lensde_fields.glass_pairs(system)):
        if front <= index <= rear:
            return group_id
    if variable.get('kind') == 'thickness':
        return index
    return None


def clamp_variable_value(value: float, variable: dict) -> float:
    lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
    hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
    if lo > hi:
        lo, hi = (hi, lo)
    if str(variable.get('kind', '')) == 'glass_choice':
        return float(max(int(round(lo)), min(int(round(hi)), int(round(_lensde_configuration.finite_float(value, lo))))))
    return clamp_value(_lensde_configuration.finite_float(value, lo), lo, hi)


def project_optimization_variable_value(value: float, variable: dict) -> float:
    """Apply the selective hard-domain projection after initialization.

    Seed generation still uses the configured lower/upper box. During later
    optimization only negative thickness and discrete glass indices are
    projected; conic, curvature, and other continuous variables pass through
    unchanged. Conic limits are enforced by merit penalties so DE retains its
    population differences near the configured boundary.
    """
    try:
        number = float(value)
    except (TypeError, ValueError):
        return value
    if not math.isfinite(number):
        return number
    kind = str(variable.get('kind', '')).strip().lower()
    if kind == 'thickness':
        return max(0.0, number)
    if kind == 'glass_choice':
        lo, hi = variable_bounds(variable)
        discrete_lo = int(math.ceil(lo))
        discrete_hi = max(discrete_lo, int(math.floor(hi)))
        return float(max(discrete_lo, min(discrete_hi, int(round(number)))))
    if _lensde_configuration.is_vignetting_variable(variable):
        lo, hi = variable_bounds(variable)
        return clamp_value(number, lo, hi)
    return number


def perturb_seed_value_by_kind(value: float, variable: dict, rng: random.Random, profile: dict, *, system: dict | None=None) -> float:
    kind = str(variable.get('kind', ''))
    if kind == 'curvature':
        sigma = _lensde_configuration.finite_float(profile.get('curv'), 1e-05)
        delta = next_normal(rng) * sigma
    elif kind == 'thickness':
        scale = 'glass' if system is not None and _seed_domains.is_glass_thickness_variable(system, variable) else 'air'
        sigma = _lensde_configuration.finite_float(profile.get(scale), _lensde_configuration.finite_float(profile.get('air'), 0.1))
        delta = next_normal(rng) * sigma
    elif kind == 'material_nd':
        delta = next_normal(rng) * _lensde_configuration.finite_float(profile.get('nd'), 0.0015)
    elif kind == 'material_vd':
        delta = next_normal(rng) * _lensde_configuration.finite_float(profile.get('vd'), 0.25)
    elif kind == 'glass_choice':
        lo = int(round(_lensde_configuration.finite_float(variable.get('lower'), 0.0)))
        hi = int(round(_lensde_configuration.finite_float(variable.get('upper'), lo)))
        if hi <= lo:
            return float(lo)
        step = 1 if rng.random() < 0.85 else max(1, int(round(abs(next_normal(rng)))))
        return float(max(lo, min(hi, int(round(value)) + (step if rng.random() < 0.5 else -step))))
    else:
        lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(value, 0.0))
        hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
        delta = next_normal(rng) * _lensde_configuration.finite_float(profile.get('other'), 0.02 * abs(hi - lo))
    return clamp_variable_value(_lensde_configuration.finite_float(value, 0.0) + delta, variable)


def mutate_independent_sparse_seed_row(parent: list[float], variables: list[dict], p: float, profile: dict, rng: random.Random, *, system: dict) -> list[float]:
    probability = max(0.0, min(1.0, _lensde_configuration.finite_float(p, 0.25)))
    out = [_lensde_configuration.finite_float(value, 0.0) for value in parent]
    changed = False
    for index, variable in enumerate(variables):
        if rng.random() > probability:
            continue
        out[index] = perturb_seed_value_by_kind(out[index], variable, rng, profile, system=system)
        changed = True
    if not changed and variables:
        index = rng.randrange(len(variables))
        out[index] = perturb_seed_value_by_kind(out[index], variables[index], rng, profile, system=system)
    return out


def mutate_group_masked_seed_row(parent: list[float], variables: list[dict], selected_groups: set[int], profile: dict, rng: random.Random, *, system: dict) -> list[float]:
    out = [_lensde_configuration.finite_float(value, 0.0) for value in parent]
    changed = False
    for index, variable in enumerate(variables):
        group = seed_variable_group(system, variable)
        if group is None or group not in selected_groups:
            continue
        out[index] = perturb_seed_value_by_kind(out[index], variable, rng, profile, system=system)
        changed = True
    if not changed:
        return mutate_independent_sparse_seed_row(parent, variables, 0.1, profile, rng, system=system)
    return out


def variable_current_value_for_system(system: dict, variable: dict, default: float=0.0) -> float:
    surfaces = system.get('surfaces', []) or []
    index = int(variable.get('index', 0) or 0)
    if not 0 <= index < len(surfaces):
        return _lensde_configuration.finite_float(variable.get('initial'), default)
    surface = surfaces[index]
    kind = str(variable.get('kind', ''))
    if kind == 'curvature':
        return _lensde_defaults.curvature_from_radius(surface.get('radius'))
    if kind == 'thickness':
        return _lensde_configuration.finite_float(surface.get('thickness'), _lensde_configuration.finite_float(variable.get('initial'), default))
    if kind == 'conic':
        return _lensde_configuration.finite_float(surface.get('conic'), _lensde_configuration.finite_float(variable.get('initial'), default))
    if kind == 'asphere_coeff':
        term = int(variable.get('term', 0) or 0)
        coeffs = surface.get('asphere_coefficients', []) or []
        return _lensde_configuration.finite_float(coeffs[term] if 0 <= term < len(coeffs) else 0.0, _lensde_configuration.finite_float(variable.get('initial'), default))
    if kind == 'material_nd':
        return _lensde_configuration.finite_float(surface.get('material_after', {}).get('nd'), _lensde_configuration.finite_float(variable.get('initial'), 1.0))
    if kind == 'material_vd':
        return _lensde_configuration.finite_float(surface.get('material_after', {}).get('vd'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
    if kind == 'glass_choice':
        return round(_lensde_configuration.finite_float(variable.get('initial'), 0.0))
    return _lensde_configuration.finite_float(variable.get('initial'), default)


def asphere_order_for_term(term: int) -> int:
    return 2 * (max(0, int(term)) + 1)


def asphere_term_for_order(order: int) -> int:
    return max(0, int(order) // 2 - 1)


def clamp_value(value: float, lower: float, upper: float) -> float:
    if lower > upper:
        lower, upper = (upper, lower)
    return min(max(value, lower), upper)


def next_normal(rng: random.Random) -> float:
    u1 = max(rng.random(), 1e-12)
    u2 = rng.random()
    return math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * math.pi * u2)


def variable_bounds(variable: dict) -> tuple[float, float]:
    initial = _lensde_configuration.finite_float(variable.get('initial'), 0.0)
    lower = _lensde_configuration.finite_float(variable.get('lower'), initial)
    upper = _lensde_configuration.finite_float(variable.get('upper'), initial)
    if lower > upper:
        lower, upper = (upper, lower)
    return (lower, upper)
