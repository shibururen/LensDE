"""Saved checkpoint documents, numeric parsing and stable value identities."""
from __future__ import annotations

import csv
import hashlib
import json
import math
import struct
from pathlib import Path
from typing import Any, Iterable


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.is_file():
        return rows
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        text = line.strip()
        if not text:
            continue
        try:
            value = json.loads(text)
        except json.JSONDecodeError as exc:
            raise ValueError(f"invalid JSONL at {path}:{line_number}: {exc}") from exc
        if isinstance(value, dict):
            rows.append(value)
    return rows


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields: list[str] = []
    for row in rows:
        for key in row:
            if key not in fields:
                fields.append(key)
    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def finite(value: Any, default: float = math.inf) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return default
    return parsed if math.isfinite(parsed) else default


def parse_values(row: dict[str, Any]) -> list[float]:
    raw = row.get("values")
    if isinstance(raw, list):
        return [float(value) for value in raw]
    raw = row.get("values_json")
    if isinstance(raw, str) and raw.strip():
        values = json.loads(raw)
        if isinstance(values, list):
            return [float(value) for value in values]
    return []


def value_hash(values: list[float]) -> str:
    raw = b"".join(struct.pack("!d", float(value)) for value in values)
    return hashlib.sha256(raw).hexdigest()


def flag_value(command: list[str], flag: str, default: str = "") -> str:
    try:
        return str(command[command.index(flag) + 1])
    except (ValueError, IndexError):
        return default


def load_command(run_dir: Path, fallback: list[str]) -> list[str]:
    for path in (run_dir / "command.json", run_dir / "command_audit.json"):
        if not path.is_file():
            continue
        payload = read_json(path)
        if isinstance(payload, list) and payload:
            return [str(item) for item in payload]
        if isinstance(payload, dict) and isinstance(payload.get("command"), list):
            return [str(item) for item in payload["command"]]
    return list(fallback)
