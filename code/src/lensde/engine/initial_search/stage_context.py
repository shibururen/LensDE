"""Basin archive loading, optical topology caches and saved stage variable context."""
from __future__ import annotations
from pathlib import Path
from .. import configuration as _lensde_configuration
from .. import fields as _lensde_fields
from .. import materials as _lensde_materials
from .. import provenance as _lensde_provenance
from . import basin_selection as _m_basin_selection
from .contracts import SearchState, PhaseResult

def load_basin_archive(_flow: SearchState, stage_dir: Path) -> list[dict]:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `load_basin_archive` for this responsibility."""
    path = stage_dir / 'basin_archive.json'
    if not path.exists():
        return []
    try:
        return _lensde_provenance.read_json(path).get('basins', []) or []
    except Exception as exc:
        _flow.runtime.stderr_parts.append(f'[funnel] failed to read basin archive from {stage_dir}: {exc}')
        return []

def topology_features_for_values(_flow: SearchState, stage_system: dict, stage_variables: list[dict], values: list[float], stage_glass_config: dict | None) -> dict:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `topology_features_for_values` for this responsibility."""
    if not isinstance(values, list) or len(values) != len(stage_variables):
        return {}
    try:
        materialized = _lensde_materials.apply_variable_values_to_system(stage_system, stage_variables, [_lensde_configuration.finite_float(value, 0.0) for value in values], stage_glass_config)
        return _lensde_fields.optical_topology_features(materialized)
    except Exception as exc:
        _flow.runtime.stderr_parts.append(f'[funnel] optical topology feature extraction failed: {exc}')
        return {}

def optical_topology_field_map(features: dict, *, prefix: str='') -> dict:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `optical_topology_field_map` for this responsibility."""
    if not features:
        return {}
    return {f'{prefix}optical_coarse_key': features.get('coarse_key', ''), f'{prefix}optical_fine_key': features.get('fine_key', ''), f'{prefix}power_signs': features.get('power_signs', ''), f'{prefix}surface_shapes': features.get('surface_shapes', ''), f'{prefix}bend_bins': features.get('bend_bins', ''), f'{prefix}gap_bins': features.get('gap_bins', ''), f'{prefix}power_mag_bins': features.get('power_mag_bins', ''), f'{prefix}material_bins': features.get('material_bins', '')}

def cached_source_topology_features(_flow: SearchState, values: list[float]) -> dict:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `cached_source_topology_features` for this responsibility."""
    key = _m_basin_selection.value_row_key([_lensde_configuration.finite_float(value, 0.0) for value in values])
    cached = _flow.exploration.source_topology_feature_cache.get(key)
    if cached is not None:
        return cached
    features = topology_features_for_values(_flow, _flow.optics.base_system, _flow.optics.variables, values, _flow.optics.glass_search_config)
    _flow.exploration.source_topology_feature_cache[key] = features
    return features

def cached_stage_topology_features(_flow: SearchState, stage_context: dict, values: list[float]) -> dict:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `cached_stage_topology_features` for this responsibility."""
    value_key = _m_basin_selection.value_row_key([_lensde_configuration.finite_float(value, 0.0) for value in values])
    key = (str(stage_context.get('system_path') or ''), str(stage_context.get('variables_path') or ''), str(stage_context.get('glass_search_path') or ''), value_key)
    cached = _flow.exploration.stage_topology_feature_cache.get(key)
    if cached is not None:
        return cached
    features = topology_features_for_values(_flow, stage_context['system'], stage_context['variables'], values, stage_context['glass_search_config'])
    _flow.exploration.stage_topology_feature_cache[key] = features
    return features

def attach_stage_context_fields(_flow: SearchState, row: dict, stage_context: dict | None) -> dict:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `attach_stage_context_fields` for this responsibility."""
    if not stage_context:
        return row
    row.setdefault('stage_system_input', str(stage_context['system_path']))
    row.setdefault('stage_variables_path', str(stage_context['variables_path']))
    row.setdefault('stage_glass_search_path', str(stage_context['glass_search_path']))
    row.setdefault('stage_variable_count', len(stage_context['variables']))
    row.setdefault('funnel_material_mode', stage_context.get('funnel_material_mode', _flow.exploration.funnel_material_mode))
    row.setdefault('glass_names', stage_context.get('glass_names', []))
    return row

def annotate_source_basin_optical_topology(_flow: SearchState, basin: dict, source_index: int, stage_context: dict | None=None, *, prefer_cached_topology: bool=False) -> dict:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `annotate_source_basin_optical_topology` for this responsibility."""
    out = dict(basin)
    if stage_context is None and out.get('stage_system_input') and out.get('stage_variables_path'):
        try:
            stage_context = funnel_stage_context_from_row(_flow, out)
        except Exception:
            stage_context = None
    attach_stage_context_fields(_flow, out, stage_context)
    if prefer_cached_topology:
        cached_key = str(out.get('optical_coarse_key') or out.get('final_optical_coarse_key') or out.get('topology_key') or out.get('structural_topology_key') or '').strip()
        if cached_key:
            if not str(out.get('optical_coarse_key') or '').strip():
                out['optical_coarse_key'] = cached_key
            if not str(out.get('optical_fine_key') or '').strip():
                out['optical_fine_key'] = str(out.get('semantic_key') or out.get('descriptor_key') or cached_key)
            if not str(out.get('power_signs') or '').strip():
                out['power_signs'] = '?'
            out['probe_parent_topology_source'] = 'cached'
            out.setdefault('source_id', f'basin:{source_index}')
            return out
    values = out.get('values')
    stage_variables = stage_context['variables'] if stage_context else _flow.optics.variables
    if isinstance(values, list) and len(values) == len(stage_variables):
        if stage_context:
            features = cached_stage_topology_features(_flow, stage_context, values)
        else:
            features = cached_source_topology_features(_flow, values)
        out.update(optical_topology_field_map(features))
        out.setdefault('source_id', f'basin:{source_index}')
        out.setdefault('power_signs', features.get('power_signs', '') if features else '')
        out['probe_parent_topology_source'] = 'computed'
    return out

def read_stage_variables(_flow: SearchState, path: Path) -> list[dict]:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `read_stage_variables` for this responsibility."""
    cache_key = str(path)
    cached = _flow.exploration.stage_variables_file_cache.get(cache_key)
    if cached is not None:
        return cached
    try:
        data = _lensde_provenance.read_json(path)
    except Exception:
        return []
    if isinstance(data, dict):
        values = data.get('variables', [])
        out = values if isinstance(values, list) else []
    else:
        out = data if isinstance(data, list) else []
    _flow.exploration.stage_variables_file_cache[cache_key] = out
    return out

def funnel_stage_context_from_row(_flow: SearchState, row: dict) -> dict:
    """Basin archive loading, optical topology caches and saved stage variable context. Implements `funnel_stage_context_from_row` for this responsibility."""
    system_input = Path(str(row.get('stage_system_input') or _flow.optics.system_path))
    variables_file = Path(str(row.get('stage_variables_path') or _flow.optics.variables_path))
    glass_file = Path(str(row.get('stage_glass_search_path') or _flow.optics.glass_search_path))
    context_key = (str(system_input), str(variables_file), str(glass_file))
    base_context = _flow.exploration.stage_context_cache.get(context_key)
    if base_context is None:
        stage_variables = read_stage_variables(_flow, variables_file) or _flow.optics.variables
        try:
            stage_system = _lensde_provenance.read_json(system_input)
        except Exception:
            stage_system = _flow.optics.base_system
        if glass_file.exists():
            try:
                stage_glass_config = _lensde_provenance.read_json(glass_file)
            except Exception:
                stage_glass_config = {'enabled': False, 'groups': []}
        else:
            stage_glass_config = {'enabled': False, 'groups': []}
        base_context = {'system': stage_system, 'system_path': system_input, 'variables': stage_variables, 'variables_path': variables_file, 'glass_search_config': stage_glass_config, 'glass_search_path': glass_file, 'glass_names': row.get('glass_names', []), 'funnel_material_mode': row.get('funnel_material_mode', _flow.exploration.funnel_material_mode)}
        _flow.exploration.stage_context_cache[context_key] = base_context
    seed_values = _m_basin_selection.row_selection_values(row)
    stage_variables = base_context['variables']
    if not isinstance(seed_values, list) or len(seed_values) != len(stage_variables):
        seed_values = [_lensde_configuration.finite_float(variable.get('initial'), 0.0) for variable in stage_variables]
    return {**base_context, 'seed_values': [_lensde_configuration.finite_float(value, 0.0) for value in seed_values], 'glass_names': row.get('glass_names', base_context.get('glass_names', [])), 'funnel_material_mode': row.get('funnel_material_mode', base_context.get('funnel_material_mode', _flow.exploration.funnel_material_mode))}
