"""Material-bridge saved documents, numeric parsing and source-file location."""
from __future__ import annotations

import csv
import json
import math
from pathlib import Path
from typing import Any


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames: list[str] = []
    seen: set[str] = set()
    for row in rows:
        for key in row:
            if key not in seen:
                seen.add(key)
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def finite_float(value: Any, default: float = math.nan) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default


def csv_bool(value: Any) -> bool:
    return str(value).strip().lower() in {"1", "true", "yes", "y"}


def parse_float_list(text: str) -> list[float]:
    return [
        float(part.strip())
        for part in str(text or "").replace(";", ",").split(",")
        if part.strip()
    ]


def parse_int_list(text: str) -> list[int]:
    return [
        int(part.strip())
        for part in str(text or "").replace(";", ",").split(",")
        if part.strip()
    ]


def source_file(source: Path, name: str) -> Path:
    alternates = {
        "system.json": ["system.json", "system_input.json", "input_system.json", "checkpoint_system.json"],
        "variables.json": ["variables.json"],
    }.get(name, [name])
    current = source.resolve()
    for _ in range(5):
        for candidate_name in alternates:
            candidate = current / candidate_name
            if candidate.is_file():
                return candidate
        if current.parent == current:
            break
        current = current.parent
    raise FileNotFoundError(f"{name} not found from {source}")
