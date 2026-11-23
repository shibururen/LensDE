"""Generate physics-constrained first-order seeds with its original independent RNG stream."""
from __future__ import annotations
import random
from .. import state
from .. import configuration as _lensde_configuration
from .. import geometry as _lensde_geometry
from . import coordinates as _seed_coordinates
from . import domains as _seed_domains
from . import thickness as _seed_thickness
from . import materials as _seed_materials

# This path owns its original independent seed and first-order repair sequence.
def generate_budgeted_first_order_seed_population(system: dict, variables: list[dict], rows: int, target_efl: float, target_ttl: float, target_bfl: float, seed: int, curvature_cap: float, *, perturb_material_variables: bool=True, perturb_other_variables: bool=False, auto_ttl_thickness_budget: bool=True, ttl_budget_trigger_ratio: float=1.15, ttl_budget_compact_slack_fraction: float=0.55, ttl_budget_lower_factor: float=0.9, ttl_budget_upper_factor: float=1.03, ttl_budget_trace_upper_factor: float=1.2, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5) -> list[list[float]]:
    rng = random.Random(seed)
    surfaces = system.get('surfaces', [])
    curvature_vars = [v for v in variables if v.get('kind') == 'curvature']
    thickness_vars = [v for v in variables if v.get('kind') == 'thickness']
    target_phi = 1.0 / target_efl if abs(target_efl) > state.TARGET_EFL_EPS else 0.0
    target_phi_sign = 1.0 if target_phi >= 0.0 else -1.0
    _, length_longness = _lensde_geometry.ttl_efl_longness(target_ttl, target_efl)
    ttl_projection_config = _seed_thickness.ttl_seed_thickness_budget_config(system, variables, target_ttl, target_bfl, target_efl, enabled=auto_ttl_thickness_budget, trigger_ratio=ttl_budget_trigger_ratio, compact_slack_fraction=ttl_budget_compact_slack_fraction, lower_factor=ttl_budget_lower_factor, upper_factor=ttl_budget_upper_factor, trace_upper_factor=ttl_budget_trace_upper_factor, bfl_surface_index=bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor)
    thickness_keys = {_lensde_geometry.variable_key(variable) for variable in thickness_vars}
    fixed_thickness = 0.0
    for index, surface in enumerate(surfaces[:-1]):
        if ('thickness', index, 0) not in thickness_keys:
            fixed_thickness += max(0.0, _lensde_configuration.finite_float(surface.get('thickness'), 0.0))
    cap = max(1e-06, _lensde_configuration.finite_float(curvature_cap, 0.016))

    def sample_free_values(sample_index: int) -> dict[tuple[str, int, int], float]:
        free_values: dict[tuple[str, int, int], float] = {}
        catalog_material_values = _seed_materials.sample_catalog_material_seed_values(system, variables, rng, sample_index) if perturb_material_variables else {}
        for variable in variables:
            kind = str(variable.get('kind', ''))
            if kind in {'curvature', 'thickness'}:
                continue
            lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
            hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
            if lo > hi:
                lo, hi = (hi, lo)
            value = _seed_coordinates.clamp_value(_seed_coordinates.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)), lo, hi)
            is_material = kind in {'material_nd', 'material_vd', 'glass_choice'}
            should_perturb = is_material and perturb_material_variables or (not is_material and perturb_other_variables)
            if should_perturb:
                if kind in {'material_nd', 'material_vd'} and _lensde_geometry.variable_key(variable) in catalog_material_values:
                    value = catalog_material_values[_lensde_geometry.variable_key(variable)]
                elif kind == 'glass_choice':
                    lo_i = int(round(lo))
                    hi_i = max(lo_i, int(round(hi)))
                    value = float(rng.randint(lo_i, hi_i))
                elif hi > lo:
                    value = lo + (hi - lo) * rng.random()
                else:
                    value = lo
            free_values[_lensde_geometry.variable_key(variable)] = _seed_coordinates.clamp_value(value, lo, hi)
        return free_values

    def row_material_index(surface_index: int, free_values: dict[tuple[str, int, int], float]) -> float:
        if not 0 <= surface_index < len(surfaces):
            return 1.0
        nd_key = ('material_nd', surface_index, 0)
        if nd_key in free_values:
            return _lensde_configuration.finite_float(free_values[nd_key], _lensde_geometry.material_index(surfaces[surface_index], 1.0))
        return _lensde_geometry.material_index(surfaces[surface_index], 1.0)

    def row_dn_by_index(free_values: dict[tuple[str, int, int], float]) -> dict[int, float]:
        dn_by_index: dict[int, float] = {}
        for variable in curvature_vars:
            index = int(variable.get('index', 0) or 0)
            n_before = row_material_index(index - 1, free_values) if index > 0 else 1.0
            n_after = row_material_index(index, free_values)
            dn_by_index[index] = n_after - n_before
        return dn_by_index
    out = []
    for row_index in range(max(0, rows)):
        free_values = sample_free_values(row_index)
        dn_by_index = row_dn_by_index(free_values)
        curvature_values: dict[tuple[str, int, int], float] = {}
        accepted = False
        if abs(target_phi) > state.TARGET_EFL_EPS and curvature_vars:
            for _attempt in range(300):
                raw: dict[tuple[str, int, int], float] = {}
                phi = 0.0
                for variable in curvature_vars:
                    index = int(variable.get('index', 0) or 0)
                    dn = dn_by_index.get(index, 0.0)
                    sign = target_phi_sign * (1.0 if dn >= 0.0 else -1.0)
                    if rng.random() < 0.18:
                        sign *= -1.0
                    value = sign * (0.45 + 0.55 * rng.random())
                    raw[_lensde_geometry.variable_key(variable)] = value
                    phi += dn * value
                if target_phi * phi <= 1e-09:
                    continue
                scale = target_phi * rng.uniform(0.82, 1.18) / phi
                trial = {}
                inside = True
                for variable in curvature_vars:
                    key = _lensde_geometry.variable_key(variable)
                    lo = _lensde_configuration.finite_float(variable.get('lower'), -state.INITIAL_SEARCH_CURVATURE_BOUND)
                    hi = _lensde_configuration.finite_float(variable.get('upper'), state.INITIAL_SEARCH_CURVATURE_BOUND)
                    value = raw[key] * scale
                    if value < lo or value > hi or abs(value) > cap:
                        inside = False
                        break
                    trial[key] = value
                if inside:
                    curvature_values = trial
                    accepted = True
                    break
        if not accepted:
            total_power = sum((abs(dn_by_index.get(int(v.get('index', 0) or 0), 0.0)) for v in curvature_vars))
            fallback = abs(target_phi) / total_power if abs(target_phi) > state.TARGET_EFL_EPS and total_power > 0.0 else 0.004
            fallback *= rng.uniform(0.85, 1.15)
            for variable in curvature_vars:
                index = int(variable.get('index', 0) or 0)
                dn = dn_by_index.get(index, 0.0)
                sign = target_phi_sign * (1.0 if dn >= 0.0 else -1.0)
                lo = _lensde_configuration.finite_float(variable.get('lower'), -state.INITIAL_SEARCH_CURVATURE_BOUND)
                hi = _lensde_configuration.finite_float(variable.get('upper'), state.INITIAL_SEARCH_CURVATURE_BOUND)
                curvature_values[_lensde_geometry.variable_key(variable)] = min(max(sign * fallback, lo), hi)
        thickness_values: dict[tuple[str, int, int], float] = {}
        flexible_air_vars = []
        total = fixed_thickness
        for variable in thickness_vars:
            lo = _lensde_configuration.finite_float(variable.get('lower'), 0.5)
            hi = _lensde_configuration.finite_float(variable.get('upper'), max(lo + 1.0, 20.0))
            index = int(variable.get('index', 0) or 0)
            surface = surfaces[index] if 0 <= index < len(surfaces) else {}
            material = surface.get('material_after') or {}
            mode = str(material.get('mode', 'air')).lower()
            nd = _lensde_geometry.material_index(surface)
            is_glass_space = mode == 'glass' or (mode == 'abbe' and nd > 1.0005)
            if _seed_domains.is_bfl_variable(system, variable, bfl_surface_index) and target_bfl > 0.0:
                value = lo + (hi - lo) * rng.random() ** 2
            elif is_glass_space:
                upper = min(hi, max(lo, 4.5))
                value = lo + (upper - lo) * rng.random()
            else:
                value = lo
                flexible_air_vars.append(variable)
            thickness_values[_lensde_geometry.variable_key(variable)] = value
            total += value
        if target_ttl > 0.0 and flexible_air_vars:

            def distribute_air_budget(candidates: list[dict], amount: float, *, sharp: bool) -> float:
                if amount <= 1e-09 or not candidates:
                    return amount
                active = list(candidates)
                weights = [(rng.random() + 0.05) ** (0.45 if sharp else 1.0) for _ in active]
                remaining = amount
                for _pass in range(5):
                    weight_sum = sum(weights)
                    if remaining <= 1e-09 or weight_sum <= 0.0 or (not active):
                        break
                    next_vars = []
                    next_weights = []
                    for variable, weight in zip(active, weights, strict=False):
                        key = _lensde_geometry.variable_key(variable)
                        current = thickness_values[key]
                        hi = _lensde_configuration.finite_float(variable.get('upper'), max(current + 1.0, 20.0))
                        room = max(0.0, hi - current)
                        add = min(room, remaining * weight / weight_sum)
                        thickness_values[key] = current + add
                        remaining -= add
                        if room - add > 1e-09:
                            next_vars.append(variable)
                            next_weights.append(weight)
                    active = next_vars
                    weights = next_weights
                return remaining
            budget_low = max(total, target_ttl * (0.58 + 0.06 * length_longness))
            budget_high = max(budget_low, target_ttl * (0.96 + 0.04 * length_longness))
            target_total = min(1.02 * target_ttl, rng.uniform(budget_low, budget_high))
            extra = max(0.0, target_total - total)
            residual_vars = list(flexible_air_vars)
            if length_longness > 0.05 and rng.random() < min(0.65, 0.15 + 0.45 * length_longness):
                max_long_gaps = min(len(flexible_air_vars), 1 + int(round(2.0 * length_longness)))
                long_gap_count = max(1, min(max_long_gaps, 1 + int(rng.random() < 0.45 * length_longness)))
                selected_vars = rng.sample(flexible_air_vars, k=long_gap_count)
                selected_keys = {_lensde_geometry.variable_key(variable) for variable in selected_vars}
                long_gap_extra = extra * min(0.9, 0.58 + 0.26 * length_longness)
                extra = extra - long_gap_extra + distribute_air_budget(selected_vars, long_gap_extra, sharp=True)
                residual_vars = [variable for variable in flexible_air_vars if _lensde_geometry.variable_key(variable) not in selected_keys]
            distribute_air_budget(residual_vars, extra, sharp=False)
        project_values = dict(thickness_values)
        _seed_thickness.project_thickness_values_to_ttl_budget(system, variables, project_values, ttl_projection_config, rng)
        thickness_values.update({_lensde_geometry.variable_key(variable): project_values.get(_lensde_geometry.variable_key(variable), thickness_values.get(_lensde_geometry.variable_key(variable), 0.0)) for variable in thickness_vars})
        row = []
        for variable in variables:
            key = _lensde_geometry.variable_key(variable)
            if variable.get('kind') == 'curvature':
                row.append(curvature_values.get(key, _lensde_configuration.finite_float(variable.get('initial'), 0.0)))
            elif variable.get('kind') == 'thickness':
                row.append(thickness_values.get(key, _lensde_configuration.finite_float(variable.get('initial'), 1.0)))
            else:
                row.append(free_values.get(key, _lensde_configuration.finite_float(variable.get('initial'), 0.0)))
        out.append(row)
    return out
