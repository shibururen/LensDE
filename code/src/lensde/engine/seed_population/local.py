"""Create local funnel perturbations with the caller-prescribed variable and RNG order."""
from __future__ import annotations
import random
from .. import configuration as _lensde_configuration
from .. import geometry as _lensde_geometry
from . import coordinates as _seed_coordinates
from . import domains as _seed_domains

# Reuse one seeded stream while appending local candidates in the original row order.
def local_funnel_perturb_value(system: dict, variable: dict, value: float, rng: random.Random, scale: float) -> float:
    kind = str(variable.get('kind', '')).lower()
    lower, upper = _seed_coordinates.variable_bounds(variable)
    value = _lensde_configuration.finite_float(value, _lensde_configuration.finite_float(variable.get('initial'), 0.0))
    scale = max(0.0, scale)
    if kind == 'curvature':
        span = scale * max(abs(value) * 0.14, 0.0035)
    elif kind == 'thickness':
        if _seed_domains.is_glass_thickness_variable(system, variable):
            span = scale * max(abs(value) * 0.05, 0.18)
        else:
            span = scale * max(abs(value) * 0.08, 1.0)
    elif kind == 'material_nd':
        span = scale * 0.025
    elif kind == 'material_vd':
        span = scale * 8.0
    elif kind == 'glass_choice':
        span = scale * max(1.0, 0.05 * max(1.0, abs(value)))
    elif kind == 'conic':
        span = scale * max(abs(value) * 0.2, 5.0)
    elif kind == 'asphere_coeff':
        if str(variable.get('asphere_parameterization', '')).lower() != 'edge_sag':
            _lensde_geometry.annotate_asphere_edge_sag_variable(system, variable)
        radius = max(_lensde_configuration.finite_float(variable.get('asphere_reference_radius'), 1.0), 1e-12)
        term = max(0, int(_lensde_configuration.finite_float(variable.get('term'), 0.0)))
        radial_power = radius ** (2 * (term + 1))
        sag_lower = _lensde_configuration.finite_float(variable.get('edge_sag_lower'), lower * radial_power)
        sag_upper = _lensde_configuration.finite_float(variable.get('edge_sag_upper'), upper * radial_power)
        if sag_lower > sag_upper:
            sag_lower, sag_upper = (sag_upper, sag_lower)
        sag_value = value * radial_power
        sag_range = max(0.0, sag_upper - sag_lower)
        sag_span = scale * max(sag_range * 0.15, abs(sag_value) * 0.1, sag_range * 1e-09)
        if sag_span <= 0.0:
            return _seed_coordinates.clamp_value(value, lower, upper)
        sag_trial = _seed_coordinates.clamp_value(sag_value + rng.gauss(0.0, sag_span), sag_lower, sag_upper)
        return _seed_coordinates.clamp_value(sag_trial / radial_power, lower, upper)
    else:
        span = scale * max(abs(value) * 0.05, 0.01)
    if span <= 0.0:
        return _seed_coordinates.clamp_value(value, lower, upper)
    trial = value + rng.gauss(0.0, span)
    if kind == 'glass_choice':
        trial = round(trial)
    return _seed_coordinates.clamp_value(trial, lower, upper)


def make_local_funnel_population(system: dict, variables: list[dict], values: list[float], population: int, seed: int, scale: float) -> list[list[float]]:
    if len(values) != len(variables):
        return []
    population = max(1, population)
    rng = random.Random(seed)
    base = [_seed_coordinates.clamp_value(_lensde_configuration.finite_float(value, _lensde_configuration.finite_float(variable.get('initial'), 0.0)), *_seed_coordinates.variable_bounds(variable)) for variable, value in zip(variables, values, strict=False)]
    rows = [base]
    while len(rows) < population:
        rows.append([local_funnel_perturb_value(system, variable, value, rng, scale) for variable, value in zip(variables, base, strict=False)])
    return rows
