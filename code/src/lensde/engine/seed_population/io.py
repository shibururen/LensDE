"""Read and write seed CSV files with the retained precision and row ordering."""
from __future__ import annotations
import math
import csv
from pathlib import Path
from lensde.runtime import native_io_paths

# CSV precision and compact-name mapping are part of the persisted seed contract.
def write_population_csv(path: Path, rows: list[list[float]]) -> None:
    path = native_io_paths.python_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', encoding='ascii', newline='') as f:
        writer = csv.writer(f)
        writer.writerows(rows)


def read_population_csv(path: Path) -> list[list[float]]:
    path = native_io_paths.python_path(path)
    rows: list[list[float]] = []
    with path.open(newline='', encoding='utf-8', errors='replace') as f:
        for row in csv.reader(f):
            if not row:
                continue
            rows.append([float(cell) for cell in row])
    return rows


def read_population_scores_csv(path: Path) -> list[tuple[int, float]]:
    scores: list[tuple[int, float]] = []
    with path.open(newline='', encoding='utf-8', errors='replace') as f:
        reader = csv.DictReader(f)
        if reader.fieldnames and {'index', 'merit'}.issubset(set(reader.fieldnames)):
            for row in reader:
                try:
                    index = int(row.get('index', ''))
                    merit = float(row.get('merit', ''))
                except (TypeError, ValueError):
                    continue
                if math.isfinite(merit):
                    scores.append((index, merit))
            return scores
    with path.open(newline='', encoding='utf-8', errors='replace') as f:
        for index, row in enumerate(csv.reader(f)):
            if not row:
                continue
            try:
                merit = float(row[-1])
            except (TypeError, ValueError):
                continue
            if math.isfinite(merit):
                scores.append((index, merit))
    return scores
