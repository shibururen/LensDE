"""Resolve seed strategies, effective population dimensions and basin counts."""
from __future__ import annotations
import math
from .. import state
from .. import configuration as _lensde_configuration
from . import coordinates as _seed_coordinates

# Population dimensions exclude auxiliary vignetting coordinates by the retained policy.
def canonical_initial_seed_strategy(value: object, fallback: str='anchor_sparse_refill') -> str:
    mode = str(value or fallback).strip().lower().replace('-', '_').replace(' ', '_')
    aliases = {'mechanical_direct': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'mechanical_stratified': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'stratified_direct': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'baseline': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'baseline_direct': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'thickness_budget_simplex': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'budget_simplex': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'mechanical_budget_simplex': state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY, 'rich_traceable': 'anchor_sparse_refill', 'trace_anchor_v2': 'anchor_sparse_refill'}
    mode = aliases.get(mode, mode)
    return mode if mode in {'classic_gate', 'anchor_sparse_refill', state.MECHANICAL_STRATIFIED_DIRECT_SEED_STRATEGY} else fallback


def effective_variable_count(variables: list[dict]) -> int:
    sizing_variables = [variable for variable in variables if not _lensde_configuration.is_vignetting_variable(variable)]
    count = 0
    for variable in sizing_variables:
        if _seed_coordinates.is_profiled_asphere_seed_variable(variable):
            lower, upper = _seed_coordinates.seed_variable_coordinate_bounds(variable)
        else:
            lower, upper = _seed_coordinates.variable_bounds(variable)
        if math.isfinite(lower) and math.isfinite(upper) and (upper - lower > 1e-12):
            count += 1
    return max(1, count or len(sizing_variables))


def population_sizing_variable_count(variables: list[dict]) -> int:
    """Return the DE sizing dimension without vignetting coordinates."""
    return max(1, sum((1 for variable in variables if isinstance(variable, dict) and (not _lensde_configuration.is_vignetting_variable(variable)))))


def population_from_effective_popsize(variables: list[dict], popsize: object, fallback_popsize: float, minimum: int=1) -> int:
    factor = max(1e-06, _lensde_configuration.finite_float(popsize, fallback_popsize))
    effective_count = effective_variable_count(variables)
    evaluator_dimension = population_sizing_variable_count(variables)
    evaluator_floor = 4 * evaluator_dimension
    return max(int(minimum), evaluator_floor, int(math.ceil(effective_count * factor)))


def round_to_multiple(value: float, multiple: int) -> int:
    step = max(1, int(multiple))
    return int(step * round(float(value) / float(step)))


def adaptive_initial_search_basin_counts(options: dict, global_population: int) -> dict[str, int]:
    base_max = max(1, int(_lensde_configuration.finite_float(options.get('initial_search_basin_max'), 96)))
    base_output = max(1, int(_lensde_configuration.finite_float(options.get('initial_search_basin_output_count'), 48)))
    base_active = max(0, int(_lensde_configuration.finite_float(options.get('initial_search_basin_active_count'), 9)))
    if not bool(options.get('initial_search_basin_adaptive_counts', True)):
        return {'basin_max': base_max, 'basin_output_count': min(base_output, base_max), 'basin_active_count': min(base_active, base_max)}
    reference = max(1.0, _lensde_configuration.finite_float(options.get('initial_search_basin_adaptive_reference_population'), 600.0))
    scale = math.sqrt(max(1.0, float(max(1, global_population)) / reference))
    max_cap = max(base_max, int(_lensde_configuration.finite_float(options.get('initial_search_basin_adaptive_max_cap'), 256)))
    output_cap = max(base_output, int(_lensde_configuration.finite_float(options.get('initial_search_basin_adaptive_output_cap'), 48)))
    active_cap = max(base_active, int(_lensde_configuration.finite_float(options.get('initial_search_basin_adaptive_active_cap'), 12)))
    basin_max = max(base_max, round_to_multiple(base_max * scale, 8))
    basin_max = min(max_cap, basin_max)
    basin_output = max(base_output, round_to_multiple(base_output * scale, 4))
    basin_output = min(output_cap, basin_output, basin_max)
    active_extra = max(0, int(round(2.0 * math.log2(max(1.0, scale)))))
    basin_active = min(active_cap, basin_max, max(base_active, base_active + active_extra))
    return {'basin_max': basin_max, 'basin_output_count': basin_output, 'basin_active_count': basin_active}
