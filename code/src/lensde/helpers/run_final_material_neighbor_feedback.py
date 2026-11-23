"""Production helpers extracted from run_final_material_neighbor_feedback; unrelated historical entry points omitted."""

from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import argparse

import math

import re

from typing import Any

KEEP_VALUE_FLAGS = {
    "--aiming",
    "--aiming-iter",
    "--trace-policy",
    "--rays",
    "--arms",
    "--distribution",
    "--sampling-seed",
    "--spot-rays",
    "--spot-arms",
    "--spot-distribution",
    "--spot-sampling-seed",
}

KEEP_BOOL_FLAGS = {
    "--aiming-best-effort",
    "--clear-aperture-update",
    "--sync-stop-aperture",
    "--cpu",
}

def finite_float(value: Any, default: float = math.inf) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default

def safe_name(value: str) -> str:
    out = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return out.strip("._") or "case"

def append_kept_flags(command: list[str], source: list[str]) -> None:
    i = 1
    while i < len(source):
        token = source[i]
        if token in KEEP_VALUE_FLAGS and i + 1 < len(source):
            command.extend([token, source[i + 1]])
            i += 2
            continue
        if token in KEEP_BOOL_FLAGS and token not in command:
            command.append(token)
        i += 1

def quality_flags(score: dict[str, Any], args: argparse.Namespace) -> dict[str, bool]:
    trace_coverage = finite_float(score.get("trace_coverage"), 0.0)
    min_field_coverage = finite_float(score.get("min_field_coverage"), 0.0)
    max_distortion = finite_float(score.get("max_distortion"))
    constraint = finite_float(score.get("constraint_group_error"))
    min_edge = finite_float(score.get("min_edge"), -math.inf)
    mean_spot = finite_float(score.get("mean_spot"))
    trace_ok = trace_coverage >= float(args.min_trace_coverage) and min_field_coverage >= float(args.min_field_coverage)
    distortion_ok = max_distortion <= float(args.max_distortion)
    constraint_ok = constraint <= float(args.max_constraint_error) and min_edge >= float(args.min_edge)
    quality_ok = mean_spot <= float(args.target_mean_spot)
    return {
        "trace_ok": trace_ok,
        "distortion_ok": distortion_ok,
        "constraint_ok": constraint_ok,
        "quality_ok": quality_ok,
    }
