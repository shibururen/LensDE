"""Surface-space, stop and BFL classifications used by seed algorithms."""
from __future__ import annotations
from .. import configuration as _lensde_configuration
from .. import geometry as _lensde_geometry

# Keep physical interval membership separate from optimization-variable ordering.
def surface_after_is_glass(system: dict, index: int) -> bool:
    surfaces = system.get('surfaces', [])
    if index < 0 or index >= len(surfaces):
        return False
    return _lensde_geometry.surface_material_is_glass(surfaces[index])


def is_thickness_variable(variable: dict) -> bool:
    return variable.get('kind') == 'thickness'


def is_glass_thickness_variable(system: dict, variable: dict) -> bool:
    return is_thickness_variable(variable) and surface_after_is_glass(system, int(variable.get('index', 0) or 0))


def is_air_thickness_variable(system: dict, variable: dict) -> bool:
    return is_thickness_variable(variable) and (not is_glass_thickness_variable(system, variable))


def is_physical_stop_surface(surface: dict) -> bool:
    """Whether a stop is also an optical surface rather than a bare aperture."""
    if not bool(surface.get('is_stop', False)):
        return False
    if bool(surface.get('reflective', False)):
        return True
    material = surface.get('material_after', {})
    if not isinstance(material, dict):
        return False
    mode = str(material.get('mode') or '').strip().lower()
    if mode in {'glass', 'abbe'}:
        return True
    name = str(material.get('name') or '').strip().lower()
    return name not in {'', 'air'}


def is_bfl_variable(system: dict, variable: dict, bfl_surface_index: int | None=None) -> bool:
    if not is_thickness_variable(variable):
        return False
    return int(variable.get('index', 0) or 0) in _lensde_geometry.bfl_interval_indices(system, bfl_surface_index)


def is_bfl_anchor_variable(system: dict, variable: dict, bfl_surface_index: int | None=None) -> bool:
    """Whether a thickness variable is the physical BFL reference interval."""
    return is_thickness_variable(variable) and int(variable.get('index', 0) or 0) == _lensde_geometry.resolve_bfl_reference_surface(system, bfl_surface_index)


def bfl_anchor_seed_target(system: dict, target_bfl: float, bfl_surface_index: int | None=None) -> float | None:
    """Return the BFL-reference interval needed after the protected rear stack.

    A reference before a cover glass intentionally makes BFL span multiple
    intervals. The reference air interval is therefore not an ordinary air
    gap during initialisation: its anchor is the requested BFL minus the
    current thickness of every following BFL interval.
    """
    requested_bfl = _lensde_configuration.finite_float(target_bfl, 0.0)
    if requested_bfl <= 0.0:
        return None
    reference = _lensde_geometry.resolve_bfl_reference_surface(system, bfl_surface_index)
    surfaces = system.get('surfaces', []) or []
    tail = sum((max(0.0, _lensde_configuration.finite_float(surfaces[index].get('thickness'), 0.0)) for index in _lensde_geometry.bfl_interval_indices(system, bfl_surface_index) if index > reference and 0 <= index < len(surfaces)))
    return max(0.0, requested_bfl - tail)
