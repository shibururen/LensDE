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

from .sampling import clamp, finite_float, perturb_same_shape, variable_bounds



def normalized_glass_libraries(libraries: Any) -> list[str]:
    if isinstance(libraries, str):
        values = libraries.replace(";", ",").split(",")
    else:
        values = list(libraries or [])
    out: list[str] = []
    seen: set[str] = set()
    for value in values:
        name = str(value or "").strip()
        key = name.upper()
        if not name or key in seen:
            continue
        out.append(name)
        seen.add(key)
    return out


def glass_distance(glass: dict[str, Any], nd: float, vd: float) -> float:
    return ((float(glass["nd"]) - nd) / 0.035) ** 2 + ((float(glass["vd"]) - vd) / 7.5) ** 2


def glass_libraries_for_surface(system: dict[str, Any], surface_index: int) -> list[str]:
    glass_search = system.get("glass_search", {})
    if not isinstance(glass_search, dict):
        glass_search = {}
    configured = None
    for item in glass_search.get("surface_configs", []) or []:
        if not isinstance(item, dict):
            continue
        try:
            if int(item.get("surface_index", -1)) == int(surface_index):
                configured = item.get("libraries")
                break
        except (TypeError, ValueError):
            continue
    values = (
        configured
        or glass_search.get("default_libraries")
        or system.get("glass_libraries")
        or [system.get("glass_library", "SCHOTT")]
    )
    libraries = normalized_glass_libraries(values)
    return [name.upper() for name in libraries] or ["SCHOTT"]


def glass_record_library(glass: dict[str, Any]) -> str:
    library = str(glass.get("library") or "").strip()
    if library:
        return library.upper()
    source = str(glass.get("path") or glass.get("source_path") or "").strip()
    if source:
        return Path(source).parent.name.upper()
    return ""


def glasses_for_libraries(
    glasses: list[dict[str, Any]],
    libraries: list[str],
) -> list[dict[str, Any]]:
    allowed = {str(name).upper() for name in libraries}
    if "ALL" in allowed:
        return list(glasses)
    return [glass for glass in glasses if glass_record_library(glass) in allowed]


def ranked_glasses(glasses: list[dict[str, Any]], nd: float, vd: float, count: int) -> list[dict[str, Any]]:
    ranked = sorted(glasses, key=lambda glass: glass_distance(glass, nd, vd))
    return ranked[: max(1, count)]


def material_pairs(variables: list[dict[str, Any]]) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for nd_index, variable in enumerate(variables):
        if str(variable.get("kind", "")).lower() != "material_nd":
            continue
        surface_index = int(variable.get("index", -1) or -1)
        vd_index = next(
            (
                index
                for index, other in enumerate(variables)
                if str(other.get("kind", "")).lower() == "material_vd"
                and int(other.get("index", -2) or -2) == surface_index
            ),
            None,
        )
        if vd_index is not None:
            out.append((nd_index, vd_index))
    return out


def clamp_row(row: list[float], variables: list[dict[str, Any]]) -> list[float]:
    out: list[float] = []
    for value, variable in zip(row, variables, strict=False):
        lo, hi = variable_bounds(variable)
        clipped = clamp(finite_float(value, finite_float(variable.get("initial"), 0.0)), lo, hi)
        if str(variable.get("kind", "")).lower() == "glass_choice":
            clipped = round(clipped)
        out.append(clipped)
    return out


def mutate_glass_choices(row: list[float], variables: list[dict[str, Any]], rng: random.Random, args: argparse.Namespace) -> None:
    for index, variable in enumerate(variables):
        if str(variable.get("kind", "")).lower() != "glass_choice":
            continue
        lo, hi = variable_bounds(variable)
        span = int(max(0, round(hi - lo)))
        if span <= 0:
            row[index] = round(lo)
            continue
        roll = rng.random()
        initial = int(round(finite_float(variable.get("initial"), lo)))
        if roll < float(args.seed_glass_keep_rate):
            choice = initial
        elif roll < float(args.seed_glass_keep_rate) + float(args.seed_glass_neighbor_rate):
            radius = min(span, max(1, int(args.glass_neighbor_count)))
            choice = initial + rng.randint(-radius, radius)
        else:
            choice = rng.randint(int(round(lo)), int(round(hi)))
        row[index] = float(max(int(round(lo)), min(int(round(hi)), choice)))


def inject_material_neighbors(
    row: list[float],
    variables: list[dict[str, Any]],
    system: dict[str, Any],
    glasses: list[dict[str, Any]],
    rng: random.Random,
    args: argparse.Namespace,
) -> None:
    if not glasses:
        return
    for nd_i, vd_i in material_pairs(variables):
        if rng.random() > float(args.continuous_glass_neighbor_rate):
            continue
        nd = finite_float(row[nd_i], 1.5)
        vd = finite_float(row[vd_i], 50.0)
        surface_index = int(variables[nd_i].get("index", -1))
        libraries = glass_libraries_for_surface(system, surface_index)
        site_glasses = glasses_for_libraries(glasses, libraries)
        near = ranked_glasses(
            site_glasses,
            nd,
            vd,
            int(args.continuous_glass_neighbor_topk),
        )
        if not near:
            continue
        glass = rng.choice(near[: max(1, min(len(near), int(args.continuous_glass_neighbor_pick)))])
        row[nd_i] = float(glass["nd"]) + rng.gauss(0.0, float(args.continuous_glass_nd_noise))
        row[vd_i] = float(glass["vd"]) + rng.gauss(0.0, float(args.continuous_glass_vd_noise))


def build_population(
    *,
    system: dict[str, Any],
    variables_doc: dict[str, Any],
    glasses: list[dict[str, Any]],
    material_mode: str,
    population: int,
    seed: int,
    scale: str,
    args: argparse.Namespace,
) -> list[list[float]]:
    variables = list(variables_doc.get("variables") or [])
    parent = [finite_float(variable.get("initial"), 0.0) for variable in variables]
    rows: list[list[float]] = []
    seen: set[tuple[float, ...]] = set()

    def append(row: list[float], *, clamp_values: bool = True) -> None:
        values = clamp_row(row, variables) if clamp_values else list(row)
        key = tuple(round(value, 9) for value in values)
        if key in seen:
            return
        seen.add(key)
        rows.append(values)

    rng = random.Random(seed)
    append(parent, clamp_values=False)
    attempts = 0
    while len(rows) < population and attempts < population * 500:
        attempts += 1
        row = perturb_same_shape(parent, variables, system, rng, scale)
        if material_mode == "true_glass":
            mutate_glass_choices(row, variables, rng, args)
        else:
            inject_material_neighbors(row, variables, system, glasses, rng, args)
        append(row)
    return rows[:population]
