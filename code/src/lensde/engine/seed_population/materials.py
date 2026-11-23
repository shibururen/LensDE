"""Prepare wavelength-valid catalog sampling plans and enforce material-region coverage."""
from __future__ import annotations
import math
import random
from typing import Callable
from .. import state
from .. import configuration as _lensde_configuration
from .. import geometry as _lensde_geometry
from .. import materials as _lensde_materials
from . import coordinates as _seed_coordinates

# Prepare catalog filtering once; preserve point sorting and fallback-variable order.
def prepare_catalog_material_seed_sampling_plan(system: dict, variables: list[dict], *, current_values: dict[tuple[str, int, int], float] | None=None, nd_cap: float=0.0, vd_cap: float=0.0, variable_active: Callable[[dict], bool] | None=None) -> list[dict]:
    """Prepare the invariant catalog filtering work for one seed batch."""
    current_values = current_values or {}
    material_variables: dict[int, dict[str, dict]] = {}
    for variable in variables:
        kind = str(variable.get('kind', ''))
        if kind not in {'material_nd', 'material_vd'}:
            continue
        if variable_active is not None and (not variable_active(variable)):
            continue
        index = int(variable.get('index', 0) or 0)
        material_variables.setdefault(index, {})[kind] = variable
    point_cache: dict[tuple[str, ...], list[tuple[float, float]]] = {}
    required_wavelengths = _lensde_materials.system_wavelengths_um(system)
    plan: list[dict] = []

    def seed_bounds(variable: dict) -> tuple[float, float]:
        hard_lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
        hard_hi = _lensde_configuration.finite_float(variable.get('upper'), hard_lo)
        if hard_lo > hard_hi:
            hard_lo, hard_hi = (hard_hi, hard_lo)
        lo = max(hard_lo, _lensde_configuration.finite_float(variable.get('seed_lower'), hard_lo))
        hi = min(hard_hi, _lensde_configuration.finite_float(variable.get('seed_upper'), hard_hi))
        return (lo, hi) if lo <= hi else (hard_lo, hard_hi)
    for index in sorted(material_variables):
        by_kind = material_variables[index]
        libraries = tuple(_lensde_materials.glass_libraries_for_material_bounds(system, index))
        if libraries not in point_cache:
            point_cache[libraries] = _lensde_materials.glass_material_points_for_libraries(libraries, required_wavelengths)
        points = point_cache[libraries]
        if not points:
            raise ValueError(f'No catalog material points for surface {index} from {list(libraries)} cover all system wavelengths {required_wavelengths} um')
        candidate_points: list[tuple[float, float]] = []
        for nd, vd in points:
            accepted = True
            for kind, value, cap in (('material_nd', nd, nd_cap), ('material_vd', vd, vd_cap)):
                variable = by_kind.get(kind)
                if variable is None:
                    continue
                lo, hi = seed_bounds(variable)
                key = _lensde_geometry.variable_key(variable)
                current = _seed_coordinates.clamp_value(current_values.get(key, _seed_coordinates.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), value))), lo, hi)
                if not lo <= value <= hi or (cap > 0.0 and abs(value - current) > cap):
                    accepted = False
                    break
            if accepted:
                candidate_points.append((nd, vd))
        candidate_points.sort(key=lambda point: (point[1], point[0]))
        fallback: list[dict] = []
        for kind, variable in by_kind.items():
            lo, hi = seed_bounds(variable)
            key = _lensde_geometry.variable_key(variable)
            current = _seed_coordinates.clamp_value(current_values.get(key, _seed_coordinates.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0))), lo, hi)
            cap = nd_cap if kind == 'material_nd' else vd_cap
            if cap > 0.0:
                lo = max(lo, current - cap)
                hi = min(hi, current + cap)
            fallback.append({'key': key, 'lower': lo, 'upper': hi, 'current': current})
        plan.append({'surface_index': index, 'nd_key': _lensde_geometry.variable_key(by_kind['material_nd']) if 'material_nd' in by_kind else None, 'vd_key': _lensde_geometry.variable_key(by_kind['material_vd']) if 'material_vd' in by_kind else None, 'candidate_points': candidate_points, 'fallback': fallback})
    return plan


def sample_catalog_material_seed_values(system: dict, variables: list[dict], rng: random.Random, sample_index: int, *, current_values: dict[tuple[str, int, int], float] | None=None, nd_cap: float=0.0, vd_cap: float=0.0, variable_active: Callable[[dict], bool] | None=None, prepared_plan: list[dict] | None=None) -> dict[tuple[str, int, int], float]:
    """Jointly seed continuous materials from the active catalog point cloud.

    Catalog points are divided into three equal-count Vd strata.  Cycling the
    selected stratum by sample and surface prevents a catalog with many
    high-Abbe entries from dominating the initial population, while selecting
    a real (nd, Vd) pair preserves the catalog's material correlation.
    """
    if prepared_plan is None:
        prepared_plan = prepare_catalog_material_seed_sampling_plan(system, variables, current_values=current_values, nd_cap=nd_cap, vd_cap=vd_cap, variable_active=variable_active)
    sampled: dict[tuple[str, int, int], float] = {}
    for surface_order, entry in enumerate(prepared_plan):
        candidate_points = entry.get('candidate_points') or []
        if candidate_points:
            stratum_count = min(3, len(candidate_points))
            stratum = (max(0, int(sample_index)) + surface_order) % stratum_count
            start = stratum * len(candidate_points) // stratum_count
            stop = (stratum + 1) * len(candidate_points) // stratum_count
            nd, vd = candidate_points[rng.randrange(start, max(start + 1, stop))]
            if entry.get('nd_key') is not None:
                sampled[entry['nd_key']] = nd
            if entry.get('vd_key') is not None:
                sampled[entry['vd_key']] = vd
            continue
        for fallback in entry.get('fallback') or []:
            lo = _lensde_configuration.finite_float(fallback.get('lower'), 0.0)
            hi = _lensde_configuration.finite_float(fallback.get('upper'), lo)
            current = _lensde_configuration.finite_float(fallback.get('current'), lo)
            sampled[fallback['key']] = lo + (hi - lo) * rng.random() if hi > lo else current
    return sampled


def material_seed_plan_region_label(entry: dict, nd: float, vd: float) -> int | None:
    """Return the equal-count Vd-stratum label for one material sample."""
    candidate_points = entry.get('candidate_points') or []
    if not candidate_points or not math.isfinite(nd) or (not math.isfinite(vd)):
        return None
    stratum_count = min(3, len(candidate_points))
    exact_key = (round(nd, 12), round(vd, 12))
    point_index = next((index for index, point in enumerate(candidate_points) if (round(point[0], 12), round(point[1], 12)) == exact_key), None)
    if point_index is None:
        nd_span = max(1e-12, max((point[0] for point in candidate_points)) - min((point[0] for point in candidate_points)))
        vd_span = max(1e-12, max((point[1] for point in candidate_points)) - min((point[1] for point in candidate_points)))
        point_index = min(range(len(candidate_points)), key=lambda index: ((candidate_points[index][0] - nd) / nd_span) ** 2 + ((candidate_points[index][1] - vd) / vd_span) ** 2)
    return min(stratum_count - 1, ((point_index + 1) * stratum_count - 1) // len(candidate_points))


def material_seed_region_diversity_applicable(prepared_plan: list[dict] | None, minimum_regions: int=2) -> bool:
    paired_entries = [entry for entry in prepared_plan or [] if entry.get('nd_key') is not None and entry.get('vd_key') is not None and entry.get('candidate_points')]
    available_regions = {region for entry in paired_entries for region in range(min(3, len(entry.get('candidate_points') or [])))}
    return len(paired_entries) >= 2 and len(available_regions) >= max(2, minimum_regions)


def material_seed_row_region_labels(row: list[float], variables: list[dict], prepared_plan: list[dict] | None) -> list[int]:
    positions = {_lensde_geometry.variable_key(variable): position for position, variable in enumerate(variables)}
    labels: list[int] = []
    for entry in prepared_plan or []:
        nd_position = positions.get(entry.get('nd_key'))
        vd_position = positions.get(entry.get('vd_key'))
        if nd_position is None or vd_position is None or nd_position >= len(row) or (vd_position >= len(row)):
            continue
        label = material_seed_plan_region_label(entry, _lensde_configuration.finite_float(row[nd_position], math.nan), _lensde_configuration.finite_float(row[vd_position], math.nan))
        if label is not None:
            labels.append(label)
    return labels


def material_seed_row_region_count(row: list[float], variables: list[dict], prepared_plan: list[dict] | None) -> int:
    return len(set(material_seed_row_region_labels(row, variables, prepared_plan)))


def enforce_material_seed_population_region_diversity(system: dict, variables: list[dict], rows: list[list[float]], rng: random.Random, prepared_plan: list[dict] | None, *, minimum_regions: int=2) -> list[list[float]]:
    """Repair catalog-seeded rows whose material sites occupy one stratum."""
    minimum_regions = max(2, int(minimum_regions))
    if not material_seed_region_diversity_applicable(prepared_plan, minimum_regions):
        return rows
    positions = {_lensde_geometry.variable_key(variable): position for position, variable in enumerate(variables)}
    repaired_rows = list(rows)
    for row_index, source_row in enumerate(repaired_rows):
        if material_seed_row_region_count(source_row, variables, prepared_plan) >= minimum_regions:
            continue
        repaired = False
        for offset in range(6):
            sampled = sample_catalog_material_seed_values(system, variables, rng, row_index + offset, prepared_plan=prepared_plan)
            candidate = list(source_row)
            for key, value in sampled.items():
                position = positions.get(key)
                if position is not None and position < len(candidate):
                    candidate[position] = value
            if material_seed_row_region_count(candidate, variables, prepared_plan) >= minimum_regions:
                repaired_rows[row_index] = candidate
                repaired = True
                break
        if not repaired:
            raise ValueError(f'Material seed diversity is infeasible: each individual must span at least {minimum_regions} catalog Vd strata')
    return repaired_rows


def project_material_anchor_to_catalog(system: dict, variables: list[dict], values: dict[tuple[str, int, int], float]) -> dict[tuple[str, int, int], float]:
    projected = dict(values)
    material_variables: dict[int, dict[str, dict]] = {}
    for variable in variables:
        kind = str(variable.get('kind', ''))
        if kind in {'material_nd', 'material_vd'}:
            index = int(variable.get('index', 0) or 0)
            material_variables.setdefault(index, {})[kind] = variable

    def seed_bounds(variable: dict) -> tuple[float, float]:
        hard_lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
        hard_hi = _lensde_configuration.finite_float(variable.get('upper'), hard_lo)
        if hard_lo > hard_hi:
            hard_lo, hard_hi = (hard_hi, hard_lo)
        lo = max(hard_lo, _lensde_configuration.finite_float(variable.get('seed_lower'), hard_lo))
        hi = min(hard_hi, _lensde_configuration.finite_float(variable.get('seed_upper'), hard_hi))
        return (lo, hi) if lo <= hi else (hard_lo, hard_hi)
    for index, by_kind in material_variables.items():
        points = _lensde_materials.glass_material_points_for_libraries(_lensde_materials.glass_libraries_for_material_bounds(system, index), _lensde_materials.system_wavelengths_um(system))
        candidates = []
        for nd, vd in points:
            if any((not seed_bounds(variable)[0] <= value <= seed_bounds(variable)[1] for kind, value in (('material_nd', nd), ('material_vd', vd)) for variable in [by_kind.get(kind)] if variable is not None)):
                continue
            candidates.append((nd, vd))
        if not candidates:
            continue
        nd_variable = by_kind.get('material_nd')
        vd_variable = by_kind.get('material_vd')
        current_nd = projected.get(_lensde_geometry.variable_key(nd_variable), 1.5) if nd_variable is not None else None
        current_vd = projected.get(_lensde_geometry.variable_key(vd_variable), 50.0) if vd_variable is not None else None

        def distance(point: tuple[float, float]) -> float:
            nd, vd = point
            score = 0.0
            if current_nd is not None:
                score += ((nd - current_nd) / state.GLASS_DISTANCE_ND_SCALE) ** 2
            if current_vd is not None and current_vd > 0.0 and (vd > 0.0):
                score += (math.log(vd / current_vd) / state.GLASS_DISTANCE_VD_RELATIVE_SCALE) ** 2
            return score
        nd, vd = min(candidates, key=distance)
        if nd_variable is not None:
            projected[_lensde_geometry.variable_key(nd_variable)] = nd
        if vd_variable is not None:
            projected[_lensde_geometry.variable_key(vd_variable)] = vd
    return projected
