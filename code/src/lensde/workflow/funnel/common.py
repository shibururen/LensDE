"""Read/write ordered funnel artifacts and resolve shared sampling and restart contracts."""
from __future__ import annotations
from typing import Any
from pathlib import Path
import json
from lensde.engine import core as server
import math
import csv
import argparse
from .settings import DEFAULT_MAX_LOCAL_ITER_PER_RESTART


def read_json(path: Path) -> Any:
    path = server.native_io_paths.python_path(path)
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path = server.native_io_paths.python_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def finite_float(value: Any, default: float = math.nan) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default


def first_existing(paths: list[Path]) -> Path:
    for path in paths:
        if path.exists():
            return path
    raise FileNotFoundError(
        "none of these paths exists: " + ", ".join((str(path) for path in paths))
    )


def ancestor_file_paths(start: Path, filename: str, max_depth: int = 4) -> list[Path]:
    paths: list[Path] = []
    current = start
    for _ in range(max(1, max_depth + 1)):
        paths.append(current / filename)
        if current.parent == current:
            break
        current = current.parent
    return paths


def context_paths(run_dir: Path) -> tuple[Path, Path, Path]:
    return (
        first_existing(
            ancestor_file_paths(run_dir, "system_input.json")
            + ancestor_file_paths(run_dir, "input_system.json")
            + ancestor_file_paths(run_dir, "system.json")
            + ancestor_file_paths(run_dir, "checkpoint_system.json")
        ),
        first_existing(ancestor_file_paths(run_dir, "variables.json")),
        first_existing(ancestor_file_paths(run_dir, "merit.json")),
    )


def variable_list(path: Path) -> list[dict[str, Any]]:
    data = read_json(path)
    if isinstance(data, dict):
        values = data.get("variables", [])
        return values if isinstance(values, list) else []
    return data if isinstance(data, list) else []


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def parse_int_list(text: str) -> list[int]:
    return [int(part.strip()) for part in text.replace(";", ",").split(",") if part.strip()]


def parse_float_list(text: str) -> list[float]:
    return [float(part.strip()) for part in text.replace(";", ",").split(",") if part.strip()]


def fixed_glass_feedback_trace_sampling(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "num_rays": max(1, int(getattr(args, "fixed_glass_feedback_trace_rays", 6))),
        "pupil_arms": max(1, int(getattr(args, "fixed_glass_feedback_trace_arms", 8))),
        "pupil_distribution": str(
            getattr(args, "fixed_glass_feedback_trace_distribution", "gaussian_ring_arm")
            or "gaussian_ring_arm"
        ).strip(),
    }


def prescout_material_trace_sampling(args: argparse.Namespace) -> dict[str, Any]:
    """The single sampling contract shared before real-glass materialization."""
    return {
        "field_hy_fractions": str(
            getattr(args, "glass_scout_field_hy_fractions", "0,0.25,0.5,0.75,1")
            or "0,0.25,0.5,0.75,1"
        ).strip(),
        "num_rays": max(1, int(getattr(args, "glass_scout_trace_rays", 3))),
        "pupil_arms": max(1, int(getattr(args, "glass_scout_trace_arms", 6))),
        "pupil_distribution": str(
            getattr(args, "glass_scout_trace_distribution", "gaussian_ring_arm")
            or "gaussian_ring_arm"
        ).strip(),
    }


def align_prescout_material_sampling(args: argparse.Namespace) -> dict[str, Any]:
    """Make Bridge and continuous Material Funnel DLS inherit Scout sampling."""
    sampling = prescout_material_trace_sampling(args)
    args.material_bridge_field_hy_fractions = sampling["field_hy_fractions"]
    args.material_bridge_trace_rays = sampling["num_rays"]
    args.material_bridge_trace_arms = sampling["pupil_arms"]
    args.material_bridge_trace_distribution = sampling["pupil_distribution"]
    return sampling


def validated_local_iter(value: Any, *, name: str, max_iter: int) -> int:
    local_iter_value = finite_float(value, math.nan)
    if not math.isfinite(local_iter_value):
        raise ValueError(f"{name} must be finite, got {value!r}")
    local_iter = int(local_iter_value)
    if local_iter <= 0:
        raise ValueError(f"{name} must be positive, got {value!r}")
    if max_iter > 0 and local_iter > max_iter:
        raise ValueError(
            f"{name}={local_iter} exceeds --max-local-iter-per-restart={max_iter}; split it into multiple restart entries"
        )
    return local_iter


def max_local_iter_per_restart(args: argparse.Namespace) -> int:
    return int(
        finite_float(
            getattr(args, "max_local_iter_per_restart", DEFAULT_MAX_LOCAL_ITER_PER_RESTART),
            DEFAULT_MAX_LOCAL_ITER_PER_RESTART,
        )
    )


def restart_iter_schedule(args: argparse.Namespace) -> list[int]:
    max_iter = max_local_iter_per_restart(args)
    schedule = [int(value) for value in parse_float_list(args.restart_local_iter_schedule)]
    if not schedule:
        schedule = [max_iter if max_iter > 0 else DEFAULT_MAX_LOCAL_ITER_PER_RESTART]
    return [
        validated_local_iter(value, name=f"restart_local_iter_schedule[{index}]", max_iter=max_iter)
        for index, value in enumerate(schedule, start=1)
    ]


def field_radius(field: dict[str, Any]) -> float:
    return math.hypot(finite_float(field.get("hx"), 0.0), finite_float(field.get("hy"), 0.0))


def cycle_iter(schedule: list[int], cycle: int) -> int:
    if not schedule:
        return DEFAULT_MAX_LOCAL_ITER_PER_RESTART
    return schedule[min(cycle - 1, len(schedule) - 1)]


def record_candidate_shortfall(
    args: argparse.Namespace, stage: str, requested: int, rows: list[dict[str, Any]]
) -> None:
    """Continue valid partial pools without weakening optical admission."""
    if requested <= 0 or len(rows) >= requested:
        return
    report = {
        "stage": stage,
        "requested_count": requested,
        "actual_count": len(rows),
        "shortfall": requested - len(rows),
        "action": "continue_with_available" if rows else "no_valid_candidates",
    }
    reports = getattr(args, "candidate_shortfalls", None)
    if reports is None:
        reports = []
        args.candidate_shortfalls = reports
    reports.append(report)
    write_json(Path(args.out_dir) / "candidate_shortfalls.json", reports)
    if not rows:
        raise RuntimeError(f"{stage}: no trace/constraint-valid candidates remain")
    print(
        f"[{stage}] {len(rows)}/{requested} valid candidates; continuing with the available pool.",
        flush=True,
    )


def _row_metric(row: dict[str, Any], *keys: str) -> float:
    for key in keys:
        value = finite_float(row.get(key), math.nan)
        if math.isfinite(value):
            return value
    return math.nan


def _row_truthy(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes", "y", "on"}
