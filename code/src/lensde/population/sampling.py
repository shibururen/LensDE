"""Static extraction of deterministic local population helpers.

See extraction_sources.json for source functions and frozen hashes.
No original scripts are imported or read at runtime.
"""
from __future__ import annotations

import argparse
import math
import random
from pathlib import Path
from typing import Any



def finite_float(value: Any, default: float = math.inf) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default


def variable_bounds(variable: dict[str, Any]) -> tuple[float, float]:
    return float(variable.get("lower", -math.inf)), float(variable.get("upper", math.inf))


def clamp(value: float, lo: float, hi: float) -> float:
    return min(hi, max(lo, value))


def surface_material_mode(system: dict[str, Any], surface_index: int) -> str:
    surfaces = list(system.get("surfaces") or [])
    if surface_index < 0 or surface_index >= len(surfaces):
        return ""
    material = surfaces[surface_index].get("material_after") or {}
    return str(material.get("mode", "")).lower()


def thickness_role(system: dict[str, Any], variable: dict[str, Any]) -> str:
    if str(variable.get("kind", "")).lower() != "thickness":
        return ""
    mode = surface_material_mode(system, int(variable.get("index", -1)))
    if mode == "glass":
        return "glass"
    if mode == "air":
        return "air"
    return ""


def local_sigma(
    variable: dict[str, Any],
    value: float,
    role: str,
    scale: str,
) -> float:
    kind = str(variable.get("kind", "")).lower()
    if scale == "tight":
        curv_rel, curv_abs = 0.08, 0.0012
        air_rel, air_abs = 0.06, 0.45
        glass_rel, glass_abs = 0.025, 0.08
    elif scale == "medium":
        curv_rel, curv_abs = 0.18, 0.0030
        air_rel, air_abs = 0.13, 1.10
        glass_rel, glass_abs = 0.055, 0.18
    else:
        curv_rel, curv_abs = 0.30, 0.0060
        air_rel, air_abs = 0.22, 2.00
        glass_rel, glass_abs = 0.090, 0.32
    if kind == "curvature":
        return max(abs(value) * curv_rel, curv_abs)
    if kind == "thickness" and role == "glass":
        return max(abs(value) * glass_rel, glass_abs)
    if kind == "thickness":
        return max(abs(value) * air_rel, air_abs)
    return max(abs(value) * 0.05, 0.01)


def curvature_parent_sign(parent_value: float) -> str:
    return "N" if parent_value < 0.0 else "P"


def same_shape_curvature_sample(
    parent_value: float,
    variable: dict[str, Any],
    rng: random.Random,
    scale: str,
) -> float:
    lo, hi = variable_bounds(variable)
    target_sign = curvature_parent_sign(parent_value)
    base = max(abs(parent_value), 2.0e-5)
    sigma = 0.08 if scale == "tight" else (0.18 if scale == "medium" else 0.32)
    value = base * math.exp(rng.gauss(0.0, sigma))
    if rng.random() < (0.10 if scale == "wide" else 0.04):
        value *= math.exp(rng.uniform(math.log(0.25), math.log(3.0)))
    if target_sign == "N":
        value = -value
        hi = min(hi, -1.0e-8)
    else:
        lo = max(lo, 1.0e-8)
    return clamp(value, lo, hi)


def perturb_same_shape(
    parent: list[float],
    variables: list[dict[str, Any]],
    system: dict[str, Any],
    rng: random.Random,
    scale: str,
) -> list[float]:
    row = list(parent)
    for i, variable in enumerate(variables):
        lo, hi = variable_bounds(variable)
        kind = str(variable.get("kind", "")).lower()
        role = thickness_role(system, variable)
        if kind == "curvature":
            row[i] = same_shape_curvature_sample(parent[i], variable, rng, scale)
        else:
            row[i] = clamp(row[i] + rng.gauss(0.0, local_sigma(variable, row[i], role, scale)), lo, hi)
    return row
