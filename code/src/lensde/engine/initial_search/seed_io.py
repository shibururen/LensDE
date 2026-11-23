"""Seed-pool CSV and numeric cap parsing; keeps variable and row order stable."""
from __future__ import annotations
import math
import csv
from pathlib import Path
from .. import configuration as _lensde_configuration
from .contracts import SearchState, PhaseResult

def parse_cap_list(value: object, fallback: list[float]) -> list[float]:
    """Seed-pool CSV and numeric cap parsing Implements `parse_cap_list` for this responsibility."""
    text = str(value or '').replace(';', ',')
    out: list[float] = []
    for part in text.split(','):
        number = _lensde_configuration.finite_float(part.strip(), float('nan'))
        if math.isfinite(number) and number > 0.0:
            out.append(number)
    return out or list(fallback)

def write_seed_pool_csv(_flow: SearchState, path: Path, objects: list[dict]) -> None:
    """Seed-pool CSV and numeric cap parsing Implements `write_seed_pool_csv` for this responsibility."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['index', 'strict', 'merit', 'trace_coverage', 'min_field_coverage', 'source_stage'] + [f'v{i}' for i in range(len(_flow.optics.variables))])
        for index, obj in enumerate(objects):
            row = obj.get('row') if isinstance(obj.get('row'), list) else []
            writer.writerow([index, 1 if obj.get('strict') else 0, obj.get('merit', ''), obj.get('trace_coverage', ''), obj.get('min_field_coverage', ''), obj.get('source_stage', ''), *[_lensde_configuration.finite_float(value, 0.0) for value in row]])

def write_variable_ranges_csv(path: Path, summary: dict) -> None:
    """Seed-pool CSV and numeric cap parsing Implements `write_variable_ranges_csv` for this responsibility."""
    path.parent.mkdir(parents=True, exist_ok=True)
    ranges = summary.get('variable_ranges', []) if isinstance(summary, dict) else []
    with path.open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=['index', 'kind', 'surface', 'term', 'initial', 'min', 'max', 'range', 'range_frac', 'active_tolerance', 'active_count', 'active_frac', 'active_coverage_frac', 'active_fraction_denominator', 'positive_active_count', 'positive_active_frac', 'negative_active_count', 'negative_active_frac', 'required_active_frac', 'required_active_count', 'active_ready'])
        writer.writeheader()
        for row in ranges:
            writer.writerow({key: row.get(key, '') for key in writer.fieldnames})
