"""Construct seed/search variable layouts and staged asphere dimensions."""
from __future__ import annotations
import copy
from .. import state
from .. import commands as _lensde_commands
from .. import configuration as _lensde_configuration
from .. import fields as _lensde_fields
from .. import geometry as _lensde_geometry
from .. import initialization as _lensde_initialization
from .. import materials as _lensde_materials
from . import coordinates as _seed_coordinates
from . import domains as _seed_domains

# Build variable columns in their original order before sampling any candidate rows.
def front_prior_ranges(profile: str) -> tuple[tuple[float, float], tuple[float, float]]:
    key = str(profile or 'weak').strip().lower()
    if key == 'medium':
        return ((0.002, 0.01), (0.012, 0.03))
    if key == 'strong':
        return ((0.004, 0.014), (0.018, 0.05))
    if key == 'xstrong':
        return ((0.003, 0.018), (0.022, 0.08))
    return ((0.001, 0.006), (0.008, 0.018))


def make_hybrid_initial_search_variables(system: dict, base_variables: list[dict], *, wide_angle_prior: bool, front_groups: int, front_profile: str, rest_cap: float, target_ttl: float, target_bfl: float, target_efl: float=0.0, adaptive_thickness: bool=True, thickness_constraints: dict | None=None, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5, use_current_thickness_prior: bool=False) -> tuple[list[dict], list[tuple[int, int]]]:
    surfaces = system.get('surfaces', []) or []
    pairs = _lensde_fields.glass_pairs(system)
    front_pairs = pairs[:max(0, int(front_groups))] if wide_angle_prior else []
    out: list[dict] = []
    thickness_budget = _lensde_geometry.adaptive_thickness_budget(system, target_ttl, target_bfl, target_efl, thickness_constraints=thickness_constraints, bfl_surface_index=bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor, use_current_thickness_prior=use_current_thickness_prior) if adaptive_thickness else {}
    for variable in base_variables:
        kind = str(variable.get('kind', ''))
        index = int(variable.get('index', 0) or 0)
        if kind not in {'curvature', 'thickness'} or not 0 <= index < len(surfaces):
            continue
        surface = surfaces[index]
        if str(surface.get('kind', '')).lower() in {'object', 'image'}:
            continue
        if surface.get('is_stop') and kind == 'curvature' and (not _seed_domains.is_physical_stop_surface(surface)):
            continue
        item = copy.deepcopy(variable)
        item['term'] = 0
        initial = _seed_coordinates.variable_current_value_for_system(system, variable, _lensde_configuration.finite_float(item.get('initial'), 0.0))
        active = False
        if kind == 'curvature':
            item['lower'], item['upper'] = _lensde_geometry.clamped_bounds(item.get('lower'), item.get('upper'), -abs(rest_cap), abs(rest_cap))
            item['initial'] = _seed_coordinates.clamp_value(initial, item['lower'], item['upper'])
            active = True
        elif kind == 'thickness':
            item['lower'], item['upper'] = _lensde_geometry.adaptive_interval_bounds(system, index, thickness_budget, bfl_surface_index=bfl_surface_index)
            if _seed_domains.is_bfl_anchor_variable(system, item, bfl_surface_index):
                bfl_anchor = _seed_domains.bfl_anchor_seed_target(system, target_bfl, bfl_surface_index)
                if bfl_anchor is not None:
                    initial = bfl_anchor
            item['initial'] = _seed_coordinates.clamp_value(initial, item['lower'], item['upper'])
            active = True
        if active:
            role = 'hybrid-initial'
            item['role'] = f'{role} {kind} @ S{index}'
            out.append(item)
    return (out, front_pairs)


def system_fallback_radius(system: dict) -> float:
    aperture = system.get('aperture', {}) if isinstance(system.get('aperture'), dict) else {}
    value = abs(_lensde_configuration.finite_float(aperture.get('value'), 0.0))
    if value <= 0.0:
        return 1.0
    aperture_type = str(aperture.get('type', 'EPD')).upper()
    return 0.5 * value if aperture_type == 'EPD' else value


def edge_radius_for_initial_search(system: dict, surface_index: int) -> float:
    return _lensde_geometry.asphere_stage_reference_radius(system, surface_index)


def asphere_bound_span_for_initial_search(system: dict, variables: list[dict], value: float, surface_index: int, term: int) -> float:
    order = 2 * (max(0, int(term)) + 1)
    radius = edge_radius_for_initial_search(system, surface_index)
    has_curvature = int(term) == 0 and any((_lensde_geometry.variable_key(variable) == ('curvature', surface_index, 0) for variable in variables))
    budget = _lensde_geometry.asphere_sag_budget(term, has_curvature)
    sag_limited = budget / max(radius ** order, 1.0)
    return max(abs(_lensde_configuration.finite_float(value, 0.0)) * 10.0, sag_limited)


def append_initial_search_live_variables(system: dict, variables: list[dict], source_variables: list[dict], options: dict) -> list[dict]:
    out = [copy.deepcopy(variable) for variable in variables]
    keys = {_lensde_geometry.variable_key(variable) for variable in out}
    templates = {_lensde_geometry.variable_key(variable): copy.deepcopy(variable) for variable in source_variables}
    source_keys = set(templates)
    surfaces = system.get('surfaces', []) or []
    glass_search = system.get('glass_search', {}) if isinstance(system.get('glass_search'), dict) else {}

    def add_from_template(kind: str, index: int, term: int, initial: float, lower: float, upper: float, role: str) -> None:
        key = (kind, index, term)
        if key in keys:
            return
        template = copy.deepcopy(templates.get(key, {}))
        item = {'kind': kind, 'index': index, 'term': term, 'lower': _lensde_configuration.finite_float(template.get('lower'), lower), 'upper': _lensde_configuration.finite_float(template.get('upper'), upper), 'initial': initial, 'role': template.get('role') or role}
        for seed_bound_key in ('seed_lower', 'seed_upper'):
            if seed_bound_key in template:
                item[seed_bound_key] = _lensde_configuration.finite_float(template.get(seed_bound_key), lower if seed_bound_key == 'seed_lower' else upper)
        if kind in {'material_nd', 'material_vd'}:
            item['lower'], item['upper'] = (lower, upper)
            if 'seed_lower' in item:
                item['seed_lower'] = _seed_coordinates.clamp_value(item['seed_lower'], lower, upper)
            if 'seed_upper' in item:
                item['seed_upper'] = _seed_coordinates.clamp_value(item['seed_upper'], lower, upper)
            if item.get('seed_lower', lower) > item.get('seed_upper', upper):
                item.pop('seed_lower', None)
                item.pop('seed_upper', None)
        if item['lower'] >= item['upper']:
            item['lower'], item['upper'] = (lower, upper)
        item['initial'] = _seed_coordinates.clamp_value(_lensde_configuration.finite_float(template.get('initial'), initial), item['lower'], item['upper'])
        if kind == 'glass_choice':
            try:
                item['glass_group'] = int(template.get('glass_group', index))
            except (TypeError, ValueError):
                item['glass_group'] = index
            item['initial'] = round(item['initial'])
        out.append(item)
        keys.add(key)
    include_conic = bool(options.get('initial_search_include_conic', True))
    include_asphere = bool(options.get('initial_search_include_asphere', True))
    include_material = bool(options.get('initial_search_include_material', True))
    continuous_material_stage = bool(options.get('initial_search_material_stages_enabled', False))
    max_candidates = max(1, int(glass_search.get('max_candidates_per_surface', 64) or 64))
    configured_glass_indices: set[int] = set()
    if bool(glass_search.get('enabled', False)):
        for surface_cfg in glass_search.get('surface_configs', []) if isinstance(glass_search, dict) else []:
            if not isinstance(surface_cfg, dict) or not bool(surface_cfg.get('enabled', False)):
                continue
            try:
                configured_glass_indices.add(int(surface_cfg.get('surface_index', -1)))
            except (TypeError, ValueError):
                continue
    source_glass_choice_indices = {int(variable.get('index', -1)) for variable in source_variables if variable.get('kind') == 'glass_choice'}
    staged_glass_search_indices = configured_glass_indices | source_glass_choice_indices
    selected_conic_surfaces: set[int] | None = None
    if include_conic and _lensde_commands.parse_one_based_index_list(options.get('initial_search_post_asphere_lens_indices')):
        selected_conic_surfaces = set(_lensde_initialization.selected_post_asphere_surface_indices(system, options))
    for index, surface in enumerate(surfaces):
        surface_kind = str(surface.get('kind', '')).lower()
        if surface_kind in {'object', 'image'}:
            continue
        if bool(surface.get('is_stop', False)) and (not _seed_domains.is_physical_stop_surface(surface)):
            if bool(options.get('initial_search_include_stop_air_thickness', False)):
                current = _lensde_configuration.finite_float(surface.get('thickness'), 1.0)
                add_from_template('thickness', index, 0, current, max(0.01, _lensde_configuration.finite_float(options.get('initial_search_stop_air_thickness_min'), 0.1)), max(0.02, _lensde_configuration.finite_float(options.get('initial_search_stop_air_thickness_max'), 27.0)), f'initial-search stop air thickness @ S{index}')
            continue
        if include_conic and (selected_conic_surfaces is None or index in selected_conic_surfaces):
            conic = _lensde_configuration.finite_float(surface.get('conic'), 0.0)
            add_from_template('conic', index, 0, conic, -100.0, 100.0, f'initial-search conic @ S{index}')
        if include_asphere:
            coeffs = surface.get('asphere_coefficients', []) or []
            terms = {int(variable.get('term', 0) or 0) for variable in source_variables if variable.get('kind') == 'asphere_coeff' and int(variable.get('index', -1) or -1) == index}
            for term in sorted((term for term in terms if term >= 0)):
                current = _lensde_configuration.finite_float(coeffs[term] if term < len(coeffs) else 0.0, 0.0)
                span = asphere_bound_span_for_initial_search(system, out, current, index, term)
                add_from_template('asphere_coeff', index, term, current, current - span, current + span, f'initial-search asphere A{2 * (term + 1)} @ S{index}')
        if include_material:
            material = surface.get('material_after', {}) if isinstance(surface.get('material_after'), dict) else {}
            mode = str(material.get('mode', 'air')).lower()
            nd = _lensde_configuration.finite_float(material.get('nd'), 1.0)
            vd = _lensde_configuration.finite_float(material.get('vd'), 0.0)
            if mode == 'glass' and bool(glass_search.get('enabled', False)) and (not continuous_material_stage):
                if index in staged_glass_search_indices:
                    add_from_template('glass_choice', index, 0, 0.0, 0.0, float(max_candidates - 1), f'initial-search glass choice @ S{index}')
                continue
            material_requested = ('material_nd', index, 0) in source_keys or ('material_vd', index, 0) in source_keys
            staged_continuous_material = continuous_material_stage and mode == 'glass' and (index in staged_glass_search_indices)
            if mode != 'air' and nd > 1.0005 and (material_requested or staged_continuous_material):
                nd_min, nd_max, vd_min, vd_max = _lensde_materials.glass_material_bounds_for_surface(system, index)
                add_from_template('material_nd', index, 0, nd, nd_min, nd_max, f'initial-search nd @ S{index}')
                add_from_template('material_vd', index, 0, vd if vd > 0.0 else 50.0, vd_min, vd_max, f'initial-search Vd @ S{index}')
    return _lensde_configuration.ensure_vignetting_variables_for_system(system, out, options)


def canonical_funnel_asphere_refine_max_order(value: object) -> int:
    try:
        order = int(value)
    except (TypeError, ValueError):
        order = 10
    return order if order in state.ASPHERE_REFINE_MAX_ORDERS else 10


def canonical_post_asphere_max_order(value: object) -> int:
    """Accept every optimizer-supported even coefficient order.

    The funnel's historical staged-refine control still starts at A10, while
    the shared variable builder is also used by generation-zero initial
    searches where A4- and A6-only experiments are useful.
    """
    try:
        order = int(float(value))
    except (TypeError, ValueError):
        order = 16
    return order if order in state.ASPHERE_SUPPORTED_MAX_ORDERS else 16


def surface_has_prescribed_asphere(surface: dict, source_terms: set[int] | None=None) -> bool:
    kind = str(surface.get('kind', '')).lower()
    coeffs = surface.get('asphere_coefficients')
    return kind == 'even_asphere' or (isinstance(coeffs, list) and len(coeffs) > 0) or bool(source_terms)


def funnel_asphere_stage_terms(stage: str, options: dict) -> list[int]:
    if not bool(options.get('initial_search_funnel_asphere_enabled', False)):
        return []
    normalized = str(stage or '').strip().lower().replace('-', '_')
    if normalized in {'focus', 'refine_scout'}:
        return [1, 2]
    if normalized == 'refine':
        max_order = canonical_funnel_asphere_refine_max_order(options.get('initial_search_funnel_refine_asphere_max_order', 10))
        return list(range(1, _seed_coordinates.asphere_term_for_order(max_order) + 1))
    return []


def funnel_asphere_field_count_for_stage(stage: str, options: dict) -> int:
    normalized = str(stage or '').strip().lower().replace('-', '_')
    if normalized in {'refine', 'refine_scout'}:
        return max(1, int(options.get('initial_search_funnel_refine_asphere_field_count', 7) or 7))
    return max(1, int(options.get('initial_search_funnel_focus_asphere_field_count', 5) or 5))


def funnel_field_count_for_stage(stage: str, options: dict) -> int:
    normalized = str(stage or '').strip().lower().replace('-', '_')
    key = 'initial_search_funnel_refine_field_count' if normalized in {'refine', 'refine_scout'} else 'initial_search_funnel_focus_field_count'
    try:
        return max(0, int(options.get(key, 0) or 0))
    except (TypeError, ValueError):
        return 0


def append_funnel_asphere_variables(system: dict, variables: list[dict], source_variables: list[dict], options: dict, stage: str) -> list[dict]:
    terms = funnel_asphere_stage_terms(stage, options)
    if not terms:
        return _lensde_configuration.ensure_vignetting_variables_for_system(system, variables, options)
    out = [copy.deepcopy(variable) for variable in variables]
    keys = {_lensde_geometry.variable_key(variable) for variable in out}
    templates = {_lensde_geometry.variable_key(variable): copy.deepcopy(variable) for variable in source_variables}
    source_terms_by_surface: dict[int, set[int]] = {}
    for variable in source_variables:
        if str(variable.get('kind', '')).lower() != 'asphere_coeff':
            continue
        try:
            source_index = int(variable.get('index', -1) or -1)
            source_term = int(variable.get('term', 0) or 0)
        except (TypeError, ValueError):
            continue
        source_terms_by_surface.setdefault(source_index, set()).add(source_term)

    def add_variable(kind: str, index: int, term: int, initial: float, lower: float, upper: float, role: str) -> None:
        key = (kind, index, term)
        if key in keys:
            return
        template = copy.deepcopy(templates.get(key, {}))
        item = {'kind': kind, 'index': index, 'term': term, 'lower': _lensde_configuration.finite_float(template.get('lower'), lower), 'upper': _lensde_configuration.finite_float(template.get('upper'), upper), 'initial': _lensde_configuration.finite_float(template.get('initial'), initial), 'role': template.get('role') or role}
        if item['lower'] >= item['upper']:
            item['lower'], item['upper'] = (lower, upper)
        item['initial'] = _seed_coordinates.clamp_value(item['initial'], item['lower'], item['upper'])
        out.append(item)
        keys.add(key)
    surfaces = system.get('surfaces', []) or []
    for index, surface in enumerate(surfaces):
        surface_kind = str(surface.get('kind', '')).lower()
        if surface_kind in {'object', 'image'} or (bool(surface.get('is_stop', False)) and (not _seed_domains.is_physical_stop_surface(surface))):
            continue
        source_terms = source_terms_by_surface.get(index, set())
        if not surface_has_prescribed_asphere(surface, source_terms):
            continue
        surface['kind'] = 'even_asphere'
        conic = _lensde_configuration.finite_float(surface.get('conic'), 0.0)
        add_variable('conic', index, 0, conic, -100.0, 100.0, f'funnel {stage} conic @ S{index}')
        coeffs = surface.setdefault('asphere_coefficients', [])
        if not isinstance(coeffs, list):
            coeffs = []
            surface['asphere_coefficients'] = coeffs
        max_term = max(terms)
        while len(coeffs) <= max_term:
            coeffs.append(0.0)
        for term in terms:
            current = _lensde_configuration.finite_float(coeffs[term], 0.0)
            span = asphere_bound_span_for_initial_search(system, out, current, index, term)
            add_variable('asphere_coeff', index, term, current, current - span, current + span, f'funnel {stage} asphere A{_seed_coordinates.asphere_order_for_term(term)} @ S{index}')
    return _lensde_configuration.ensure_vignetting_variables_for_system(system, out, options)
