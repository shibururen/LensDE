"""Normalize project glass catalogs, surface materials and optical variable columns."""
from __future__ import annotations
from .. import state
from .. import apertures as _lensde_apertures
from .. import defaults as _lensde_defaults
from .. import geometry as _lensde_geometry
from .. import materials as _lensde_materials
from .. import populations as _lensde_populations

def prepare_system_and_glass_search(project: dict, *, finite_float) -> tuple[dict, dict]:
    """Prepare the optimizer aperture copy and glass-library/search defaults before merit normalization."""
    incoming_options = project.setdefault('options', {})
    system = _lensde_apertures.vignetting_optimizer_aperture_copy(project.setdefault('system', {}), project.get('variables'), legacy_geometry=incoming_options.get('protective_glass_identification_policy') == 'legacy_geometry_v1')
    project['system'] = system
    system.setdefault('glass_library', 'SCHOTT')
    libraries = system.get('glass_libraries')
    if not isinstance(libraries, list) or not libraries:
        libraries = [system.get('glass_library', 'SCHOTT')]
    system['glass_libraries'] = [str(item) for item in libraries if str(item)]
    if not system['glass_libraries']:
        system['glass_libraries'] = ['SCHOTT']
    if system.get('glass_library') not in system['glass_libraries']:
        system['glass_libraries'].insert(0, system.get('glass_library', 'SCHOTT'))
    glass_search = system.setdefault('glass_search', {})
    glass_search.setdefault('enabled', False)
    glass_search.setdefault('default_libraries', list(system['glass_libraries']))
    glass_search.setdefault('max_candidates_per_surface', 64)
    glass_search.setdefault('mutation_rate', 0.35)
    glass_search.setdefault('random_rate', 0.05)
    glass_search.setdefault('pbest_inherit_rate', 0.35)
    glass_search.setdefault('neighbor_count', 12)
    glass_search.setdefault('local_search', True)
    if not isinstance(glass_search.get('surface_configs'), list):
        glass_search['surface_configs'] = []
    for variable in project.get('variables', []) or []:
        if str(variable.get('kind', '')) != 'curvature':
            continue
        role = str(variable.get('role', '')).strip().lower()
        lower = _lensde_geometry.optional_finite_float(variable.get('lower'))
        upper = _lensde_geometry.optional_finite_float(variable.get('upper'))
        if role.startswith('flat-start curvature @') and lower is not None and (upper is not None) and (abs(lower + state.LEGACY_FLAT_CURVATURE_BOUND) <= 1e-12) and (abs(upper - state.LEGACY_FLAT_CURVATURE_BOUND) <= 1e-12):
            variable['lower'] = -state.DEFAULT_CURVATURE_BOUND
            variable['upper'] = state.DEFAULT_CURVATURE_BOUND
            variable['initial'] = _lensde_populations.clamp_value(finite_float(variable.get('initial'), 0.0), -state.DEFAULT_CURVATURE_BOUND, state.DEFAULT_CURVATURE_BOUND)
    return system, glass_search


def normalize_surface_materials(
    project: dict,
    system: dict,
    glass_search: dict,
    *,
    ensure_glass_surface_config,
) -> tuple[set[int], set[int]]:
    """Normalize surface materials/apertures and activate eligible glass-choice catalog groups."""
    glass_mode_indices = set()
    actual_glass_indices = set()
    for surface in system.get('surfaces', []):
        material = surface.setdefault('material_after', {})
        material.setdefault('name', 'air')
        material.setdefault('reference', '')
        material.setdefault('nd', 1.0)
        material.setdefault('vd', 0.0)
        material.setdefault('mode', 'glass' if material.get('reference') else 'air' if material.get('name') == 'air' else 'abbe')
        if material.get('mode') == 'air' or material.get('name') == 'air':
            material.update({'name': 'air', 'mode': 'air', 'reference': '', 'nd': 1.0, 'vd': 0.0})
        elif material.get('mode') != 'glass':
            material['mode'] = 'abbe'
            material['reference'] = ''
            material['name'] = 'abbe'
        legacy_aperture = float(surface.get('aperture_radius', 0.0) or 0.0)
        legacy_fixed = 'clear_aperture_fixed' not in surface and legacy_aperture > 0 and (not surface.get('is_stop'))
        surface.setdefault('clear_aperture_fixed', legacy_fixed)
        surface.setdefault('mechanical_aperture_fixed', False)
        if legacy_fixed:
            surface['semi_aperture'] = legacy_aperture
        else:
            surface['semi_aperture'] = float(surface.get('semi_aperture', legacy_aperture) or 0.0)
        surface['mechanical_aperture'] = float(surface.get('mechanical_aperture', max(surface['semi_aperture'], legacy_aperture)) or 0.0)
        surface['aperture_radius'] = surface['semi_aperture'] if surface.get('is_stop') or surface.get('clear_aperture_fixed') else 0.0
        surface['aperture'] = {'type': 'RadialAperture', 'r_max': surface['aperture_radius'], 'r_min': 0.0} if surface['aperture_radius'] > 0 else None
    for index, surface in enumerate(system.get('surfaces', [])):
        if surface.get('material_after', {}).get('mode') in {'air', 'glass'}:
            glass_mode_indices.add(index)
        if surface.get('material_after', {}).get('mode') == 'glass':
            actual_glass_indices.add(index)
    glass_choice_indices = set()
    for variable in project.get('variables', []):
        if variable.get('kind') != 'glass_choice':
            continue
        try:
            glass_choice_indices.add(int(variable.get('index', -1)))
        except (TypeError, ValueError):
            continue
    if bool(glass_search.get('enabled', False)):
        for surface_index in sorted(glass_choice_indices & actual_glass_indices):
            ensure_glass_surface_config(glass_search, surface_index)['enabled'] = True
    return glass_mode_indices, actual_glass_indices


def filter_variable_columns(
    project: dict,
    system: dict,
    glass_search: dict,
    glass_mode_indices: set[int],
    actual_glass_indices: set[int],
) -> list[dict]:
    """Filter material variables and convert legacy radius columns while preserving variable identity/order."""
    filtered_variables = []
    for variable in project.get('variables', []):
        try:
            variable_index = int(variable.get('index', -1))
        except (TypeError, ValueError):
            variable_index = -1
        if variable.get('kind') in {'material_nd', 'material_vd'} and variable_index in glass_mode_indices:
            continue
        if variable.get('kind') == 'glass_choice' and variable_index not in actual_glass_indices:
            continue
        if variable.get('kind') == 'glass_choice':
            surface_cfg = _lensde_materials.glass_surface_config(glass_search, variable_index)
            if not bool(glass_search.get('enabled', False)) or not surface_cfg.get('enabled', False):
                continue
        if variable.get('kind') == 'radius':
            surfaces = system.get('surfaces', [])
            surface = surfaces[variable_index] if 0 <= variable_index < len(surfaces) else {}
            initial_radius = variable.get('initial', surface.get('radius'))
            lower, upper = _lensde_defaults.curvature_bounds_from_radius_bounds(variable.get('lower'), variable.get('upper'))
            variable['kind'] = 'curvature'
            variable['lower'] = lower
            variable['upper'] = upper
            variable['initial'] = _lensde_defaults.curvature_from_radius(initial_radius)
            role = str(variable.get('role', ''))
            if not role or 'radius' in role.lower():
                variable['role'] = f'curvature @ S{variable_index}'
        filtered_variables.append(variable)
    return filtered_variables
