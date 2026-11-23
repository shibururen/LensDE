"""Production helpers extracted from probe_dls_mutation_polish; unrelated historical entry points omitted."""

from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import json

import math

from pathlib import Path

from typing import Any

def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))

def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")

def finite_float(value: Any, default: float = math.inf) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default
