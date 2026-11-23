"""Production helpers extracted from run_explore_dls_feedback_cycles; unrelated historical entry points omitted."""

from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import argparse

import copy

import csv

import functools

import math

import os

import random

import re

from lensde.runtime import processes as subprocess

import time

from pathlib import Path

from typing import Any

from lensde.helpers.probe_dls_mutation_polish import read_json, write_json

from lensde.helpers.run_final_material_neighbor_feedback import append_kept_flags, finite_float, quality_flags, safe_name

from lensde.helpers.run_iterative_hammer_dls_probe import DEFAULT_DATABASE, load_glasses, normalized_glass_libraries

from lensde.helpers.run_presnap_glass_beam_branch import glass_distance, lens_sites

from lensde.helpers.run_single_parent_local_population_ablation import clamp, perturb_same_shape, variable_bounds, write_population

ROOT = PROJECT_ROOT / "native"

from lensde.optics import dynamic_aperture_contract as dynamic_aperture_contract

from lensde.runtime import native_io_paths as native_io_paths

from lensde.optics import vignetting_handoff_policy as vignetting_handoff_policy

DE_DLS_TRACE_COVERAGE_BASELINE = 0.95

CSV_FIELDS = [
    "rank",
    "cycle",
    "source_rank",
    "source",
    "source_dir",
    "material_mode",
    "algorithm",
    "merit",
    "mean_spot_um",
    "max_spot_um",
    "max_distortion",
    "constraint_group_error",
    "min_edge",
    "trace_coverage",
    "min_field_coverage",
    "min_chief_progress",
    "chief_ray_ok",
    "chief_ray_failed_fields",
    "zero_coverage_off_axis_fields",
    "parent_mean_spot_um",
    "delta_parent_mean_spot_um",
    "glass_choices",
    "trace_ok",
    "distortion_ok",
    "constraint_ok",
    "quality_ok",
    "returncode",
    "timed_out",
    "seconds",
    "out_dir",
]

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

def system_wavelengths_um(system: dict[str, Any]) -> list[float]:
    values = {
        finite_float(wave.get("value_um", wave.get("value")), math.nan)
        for wave in system.get("wavelengths", []) or []
        if isinstance(wave, dict)
    }
    return sorted(value for value in values if math.isfinite(value) and value > 0.0) or [0.5875618]

def systems_wavelengths_um(systems: list[dict[str, Any]]) -> list[float]:
    return sorted({value for system in systems for value in system_wavelengths_um(system)})

def glass_covers_system_wavelengths(glass: dict[str, Any], system: dict[str, Any]) -> bool:
    if not (system.get("wavelengths") if isinstance(system, dict) else None):
        return True
    required = system_wavelengths_um(system)
    lo = finite_float(glass.get("min_wavelength"), math.nan)
    hi = finite_float(glass.get("max_wavelength"), math.nan)
    return (
        math.isfinite(lo)
        and math.isfinite(hi)
        and lo <= required[0] + 1.0e-9
        and hi + 1.0e-9 >= required[-1]
    )

def glass_record_library(glass: dict[str, Any]) -> str:
    library = str(glass.get("library") or "").strip()
    if library:
        return library.upper()
    source = str(glass.get("path") or glass.get("source_path") or "").strip()
    if source:
        return Path(source).parent.name.upper()
    return ""

@functools.lru_cache(maxsize=4096)
def canonical_source_libraries_from_path(source: str) -> tuple[str, ...]:
    """Read merged-catalog source-library aliases embedded in a glass record."""
    path = Path(source)
    if not path.is_file():
        return ()
    aliases: list[str] = []
    seen: set[str] = set()
    try:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_number, line in enumerate(handle):
                # Converted YAML files keep catalog provenance in their header.
                # Avoid scanning the large tabulated dispersion payload.
                if line_number >= 64 or line.strip().upper() == "DATA:":
                    break
                for match in re.finditer(
                    r"canonical\s+source\s*=\s*([^:;,\s]+)\s*:[^;,\r\n]+",
                    line,
                    flags=re.IGNORECASE,
                ):
                    alias = match.group(1).strip().upper()
                    if alias and alias not in seen:
                        seen.add(alias)
                        aliases.append(alias)
    except OSError:
        return ()
    return tuple(aliases)

def glass_record_reference_aliases(glass: dict[str, Any]) -> list[str]:
    """Return catalog references that C++ may serialize for this candidate."""
    values = glass.get("reference_aliases") or []
    if isinstance(values, str):
        values = [values]
    aliases: list[str] = []
    seen: set[str] = set()
    for value in values:
        alias = str(value or "").strip().upper()
        if alias and alias not in seen:
            seen.add(alias)
            aliases.append(alias)
    source = str(glass.get("path") or glass.get("source_path") or "").strip()
    for alias in canonical_source_libraries_from_path(source) if source else ():
        if alias not in seen:
            seen.add(alias)
            aliases.append(alias)
    return aliases

def glasses_for_libraries(
    glasses: list[dict[str, Any]],
    libraries: list[str],
) -> list[dict[str, Any]]:
    allowed = {str(name).upper() for name in libraries}
    if "ALL" in allowed:
        return list(glasses)
    return [glass for glass in glasses if glass_record_library(glass) in allowed]

def glass_libraries_for_systems(systems: list[dict[str, Any]]) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for system in systems:
        for site in glass_choice_sites(system):
            for library in glass_libraries_for_surface(system, int(site.front_surface)):
                key = library.upper()
                if key in seen:
                    continue
                seen.add(key)
                out.append(key)
    return out or ["SCHOTT"]

def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS, extrasaction="ignore", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

def list_arg(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]

def source_dirs_arg(value: str) -> list[Path]:
    return [Path(item.strip()).resolve() for item in value.split(";") if item.strip()]

def file_from_source(source_dir: Path, name: str) -> Path:
    for path in [source_dir / name, source_dir.parent / name]:
        if path.exists():
            return path
    raise FileNotFoundError(f"Missing {name} in {source_dir} or {source_dir.parent}")

def optional_file_from_source(source_dir: Path, *names: str) -> Path | None:
    for name in names:
        for path in [source_dir / name, source_dir.parent / name]:
            if path.exists():
                return path
    return None

def load_command(source_dir: Path) -> list[str]:
    command_path = optional_file_from_source(source_dir, "command.json")
    if command_path:
        payload = read_json(command_path)
        if isinstance(payload, list) and payload:
            return [str(item) for item in payload]
    command_txt = optional_file_from_source(source_dir, "command.txt")
    if command_txt:
        return command_txt.read_text(encoding="utf-8", errors="replace").strip().split()
    return [str(ROOT / "build" / "lensde_cpu.exe")]

def variable_value(system: dict[str, Any], variable: dict[str, Any]) -> float:
    kind = str(variable.get("kind", "")).lower()
    if kind in {"fvcx", "fvcy", "fvdx", "fvdy"}:
        fields = list(system.get("fields") or [])
        field_index = int(variable.get("index", 0) or 0)
        if 0 <= field_index < len(fields):
            return finite_float(
                fields[field_index].get(kind),
                finite_float(variable.get("initial"), 0.0),
            )
        return finite_float(variable.get("initial"), 0.0)
    if kind in {
        "vignette_magnitude_y",
        "vignetting_magnitude_y",
        "vignette_asymmetry_y",
        "vignetting_asymmetry_y",
    }:
        fields = list(system.get("fields") or [])
        field_index = int(variable.get("index", 0) or 0)
        if 0 <= field_index < len(fields):
            field = fields[field_index]
            magnitude = max(
                0.0,
                finite_float(
                    field.get("fvdy", field.get("vignette_y")),
                    0.0,
                ),
            )
            if kind in {"vignette_magnitude_y", "vignetting_magnitude_y"}:
                return magnitude
            return (
                max(
                    -1.0,
                    min(1.0, finite_float(field.get("fvcy"), 0.0) / magnitude),
                )
                if magnitude > 1.0e-15
                else 0.0
            )
        return finite_float(variable.get("initial"), 0.0)
    surfaces = list(system.get("surfaces") or [])
    surface_index = int(variable.get("index", 0) or 0)
    if surface_index < 0 or surface_index >= len(surfaces):
        return finite_float(variable.get("initial"), 0.0)
    surface = surfaces[surface_index]
    if kind == "curvature":
        radius = surface.get("radius")
        if radius is None:
            return 0.0
        radius_value = finite_float(radius, 0.0)
        return 0.0 if abs(radius_value) < 1.0e-12 else 1.0 / radius_value
    if kind == "thickness":
        return finite_float(surface.get("thickness"), 0.0)
    if kind == "conic":
        return finite_float(surface.get("conic"), finite_float(variable.get("initial"), 0.0))
    if kind == "asphere_coeff":
        coefficients = surface.get("asphere_coefficients")
        term = int(variable.get("term", 0) or 0)
        if isinstance(coefficients, list) and 0 <= term < len(coefficients):
            return finite_float(
                coefficients[term],
                finite_float(variable.get("initial"), 0.0),
            )
        return finite_float(variable.get("initial"), 0.0)
    material = surface.get("material_after") or {}
    if kind == "material_nd":
        return finite_float(material.get("nd"), finite_float(variable.get("initial"), 1.5))
    if kind == "material_vd":
        return finite_float(material.get("vd"), finite_float(variable.get("initial"), 50.0))
    if kind == "glass_choice":
        return round(finite_float(variable.get("initial"), 0.0))
    return finite_float(variable.get("initial"), 0.0)

def parse_field_hy_fractions(text: str) -> list[float]:
    values: list[float] = []
    for token in str(text or "").replace(";", ",").split(","):
        token = token.strip()
        if not token:
            continue
        value = float(token)
        if not math.isfinite(value) or value < 0.0 or value > 1.0:
            raise ValueError(f"field hy fraction must be finite in [0,1], got {token!r}")
        values.append(value)
    if values and any(right <= left for left, right in zip(values, values[1:])):
        raise ValueError("field hy fractions must be strictly increasing")
    return values

def with_field_hy_fractions(system: dict[str, Any], text: str) -> dict[str, Any]:
    """Return an exact meridional field grid without inventing vignetting.

    Existing nodes inherit their complete field record.  When active
    vignetting is present, adding a new node here is an error: the caller must
    first run the aperture-realized field bridge.  Zero initialization remains
    available only for systems whose source fields are all unvignetted.
    """
    fractions = parse_field_hy_fractions(text)
    out = copy.deepcopy(system)
    if not fractions:
        return out
    source_fields = [
        copy.deepcopy(field)
        for field in out.get("fields", []) or []
        if isinstance(field, dict)
    ]
    has_vignetting = any(
        abs(finite_float(field.get(key), 0.0)) > 1.0e-12
        for field in source_fields
        for key in ("fvcx", "fvcy", "fvdx", "fvdy")
    )
    fields: list[dict[str, Any]] = []
    for value in fractions:
        match = next(
            (
                field
                for field in source_fields
                if abs(
                    math.hypot(
                        finite_float(field.get("hx"), 0.0),
                        finite_float(field.get("hy"), 0.0),
                    )
                    - value
                )
                <= 1.0e-6
            ),
            None,
        )
        if match is not None:
            match["hx"] = 0.0
            match["hy"] = value
            fields.append(match)
            continue
        if has_vignetting:
            raise ValueError(
                "cannot add field Hy="
                f"{value:.12g} by zeroing or interpolation while vignetting is active; "
                "run the aperture-realized field bridge first"
            )
        fields.append({
            "hx": 0.0,
            "hy": value,
            "fvcx": 0.0,
            "fvcy": 0.0,
            "fvdx": 0.0,
            "fvdy": 0.0,
            "vignette_x": 0.0,
            "vignette_y": 0.0,
        })
    out["fields"] = fields
    return out

def variables_doc_from_system(source_dir: Path, system: dict[str, Any]) -> dict[str, Any]:
    variables_doc = copy.deepcopy(read_json(file_from_source(source_dir, "variables.json")))
    variables: list[dict[str, Any]] = []
    for variable in variables_doc.get("variables", []) or []:
        item = copy.deepcopy(variable)
        kind = str(item.get("kind", "")).lower()
        # The source variable document is the stage-to-stage optimization
        # contract.  Preserve every declared variable here and let the caller
        # explicitly remove only the kinds it intends to lock.  The previous
        # allow-list silently dropped higher-order asphere coefficients during
        # fixed-glass feedback, reducing a full asphere problem to
        # curvature/thickness/conic only.
        if not kind:
            continue
        item["initial"] = variable_value(system, item)
        initial = finite_float(item.get("initial"), math.nan)
        if math.isfinite(initial) and kind != "glass_choice":
            if "lower" in item and initial < finite_float(item.get("lower"), -math.inf):
                item["lower"] = initial
            if "upper" in item and initial > finite_float(item.get("upper"), math.inf):
                item["upper"] = initial
        variables.append(item)
    variables_doc["variables"] = variables
    return variables_doc

def material_at(system: dict[str, Any], surface_index: int) -> dict[str, Any]:
    surfaces = list(system.get("surfaces") or [])
    if surface_index < 0 or surface_index >= len(surfaces):
        return {}
    return surfaces[surface_index].get("material_after") or {}

def glass_choice_sites(system: dict[str, Any]) -> list[Any]:
    """Return refractive sites that are enabled for discrete glass search.

    ``lens_sites`` intentionally discovers every refractive body, including
    fixed cover plates.  GlassChoice stages must additionally honor the GUI's
    per-surface search contract so a fixed glass cannot be silently promoted
    to a ``glass_choice`` variable.  The global ``glass_search.enabled`` flag
    controls the standalone GUI glass-search feature; it must not disable the
    explicit material-funnel terminal stage.
    """
    sites = list(lens_sites(system))
    glass_search = system.get("glass_search")
    if not isinstance(glass_search, dict):
        return sites
    disabled_indices: set[int] = set()
    for config in glass_search.get("surface_configs", []) or []:
        if not isinstance(config, dict) or "enabled" not in config:
            continue
        enabled = config.get("enabled")
        if isinstance(enabled, str):
            enabled = enabled.strip().lower() not in {"0", "false", "no", "off"}
        if bool(enabled):
            continue
        try:
            disabled_indices.add(int(config.get("surface_index", -1)))
        except (TypeError, ValueError):
            continue
    return [site for site in sites if int(site.front_surface) not in disabled_indices]

def glass_choices_from_system(system: dict[str, Any]) -> str:
    choices: list[str] = []
    for site in glass_choice_sites(system):
        material = material_at(system, int(site.front_surface))
        name = str(material.get("name") or material.get("reference") or "")
        if not name:
            name = f"nd{finite_float(material.get('nd'), site.nd):.5f}"
        choices.append(f"L{site.lens_no}:{name}")
    return "|".join(choices)

def ranked_glasses(glasses: list[dict[str, Any]], nd: float, vd: float, count: int) -> list[dict[str, Any]]:
    ranked = sorted(glasses, key=lambda glass: glass_distance(glass, nd, vd))
    return ranked[: max(1, count)]

def glass_candidate_item(glass: dict[str, Any]) -> dict[str, Any]:
    item = {
        "name": str(glass["name"]),
        "reference": str(glass.get("reference") or glass["name"]),
        "library": glass_record_library(glass),
        "nd": float(glass["nd"]),
        "vd": float(glass["vd"]),
        "source_path": str(glass.get("path") or glass.get("source_path") or ""),
        "min_wavelength": glass.get("min_wavelength"),
        "max_wavelength": glass.get("max_wavelength"),
    }
    reference_aliases = glass_record_reference_aliases(glass)
    if reference_aliases:
        item["reference_aliases"] = reference_aliases
    return item

def true_glass_problem(
    *,
    source_dir: Path,
    system: dict[str, Any],
    glasses: list[dict[str, Any]],
    args: argparse.Namespace,
    common_groups: list[dict[str, Any]] | None = None,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    base_doc = variables_doc_from_system(source_dir, system)
    problem_system = copy.deepcopy(system)
    sites = glass_choice_sites(system)
    site_indices = {int(site.front_surface) for site in sites}
    variables: list[dict[str, Any]] = []
    for variable in base_doc.get("variables", []) or []:
        kind = str(variable.get("kind", "")).lower()
        index = int(variable.get("index", -1) or -1)
        if kind in {"material_nd", "material_vd", "glass_choice"} and index in site_indices:
            continue
        variables.append(copy.deepcopy(variable))

    groups: list[dict[str, Any]] = []
    for group_id, site in enumerate(sites):
        material = material_at(system, int(site.front_surface))
        nd = finite_float(material.get("nd"), site.nd)
        vd = finite_float(material.get("vd"), site.vd)
        libraries = glass_libraries_for_surface(system, int(site.front_surface))
        if common_groups is not None:
            if group_id >= len(common_groups):
                raise ValueError("shared GlassChoice table is missing a lens group")
            common_group = common_groups[group_id]
            if int(common_group.get("surface_index", -1)) != int(site.front_surface):
                raise ValueError("shared GlassChoice table has an incompatible surface index")
            candidates = glasses_for_libraries(
                list(common_group.get("candidates") or []),
                libraries,
            )
        else:
            site_glasses = glasses_for_libraries(glasses, libraries)
            if not site_glasses:
                raise RuntimeError(
                    f"no glasses loaded for surface {site.front_surface} libraries={libraries}"
                )
            candidates = ranked_glasses(
                site_glasses,
                nd,
                vd,
                int(args.glass_candidates),
            )
        if not candidates:
            continue
        initial_index = min(range(len(candidates)), key=lambda i: glass_distance(candidates[i], nd, vd))
        initial_glass = candidates[initial_index]
        problem_system["surfaces"][int(site.front_surface)]["material_after"] = {
            "name": str(initial_glass["name"]),
            "mode": "glass",
            "reference": str(initial_glass.get("reference") or initial_glass["name"]),
            "library": glass_record_library(initial_glass),
            "nd": float(initial_glass["nd"]),
            "vd": float(initial_glass["vd"]),
            "source_path": str(
                initial_glass.get("path") or initial_glass.get("source_path") or ""
            ),
        }
        variables.append(
            {
                "kind": "glass_choice",
                "index": int(site.front_surface),
                "glass_group": group_id,
                "group": group_id,
                "lower": 0,
                "upper": len(candidates) - 1,
                "initial": initial_index,
                "role": "glass_choice",
            }
        )
        groups.append(
            {
                "id": group_id,
                "surface_index": int(site.front_surface),
                "enabled": True,
                "libraries": libraries,
                "strict_libraries": True,
                "candidates": [glass_candidate_item(glass) for glass in candidates],
            }
        )

    variables_doc = copy.deepcopy(base_doc)
    variables_doc["variables"] = variables
    glass_search = {
        "enabled": bool(groups),
        "mutation_rate": float(args.glass_mutation_rate),
        "random_rate": float(args.glass_random_rate),
        "pbest_inherit_rate": float(args.glass_pbest_inherit_rate),
        "neighbor_count": int(args.glass_neighbor_count),
        "local_search": True,
        "neighbor_nd_scale": float(args.glass_neighbor_nd_scale),
        "neighbor_vd_scale": float(args.glass_neighbor_vd_scale),
        "required_wavelengths_um": system_wavelengths_um(problem_system),
        "wavelength_filter": {
            "policy": "declared_range_must_cover_all_system_wavelengths",
            "required_wavelengths_um": system_wavelengths_um(problem_system),
            "wavelength_coverage_verified": True,
        },
        "groups": groups,
    }
    return problem_system, variables_doc, glass_search

def audit_true_glass_catalog_contract(
    system: dict[str, Any],
    glass_search: dict[str, Any],
) -> dict[str, Any]:
    surfaces = system.get("surfaces", []) or []
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    groups = list(glass_search.get("groups", []) or [])
    if not groups:
        raise RuntimeError("glass catalog contract failed: no GlassChoice groups")
    for group in groups:
        surface_index = int(group.get("surface_index", -1))
        libraries = [
            name.upper()
            for name in normalized_glass_libraries(group.get("libraries"))
        ]
        allowed = set(libraries)
        candidates = list(group.get("candidates") or [])
        invalid_wavelength_candidates = [
            str(candidate.get("name") or candidate.get("reference") or "")
            for candidate in candidates
            if not glass_covers_system_wavelengths(candidate, system)
        ]
        candidate_libraries = sorted(
            {glass_record_library(candidate) for candidate in candidates}
        )
        unexpected = (
            []
            if "ALL" in allowed
            else sorted(
                library
                for library in candidate_libraries
                if not library or library not in allowed
            )
        )
        chosen = (
            material_at(system, surface_index)
            if 0 <= surface_index < len(surfaces)
            else {}
        )
        chosen_name = str(chosen.get("name") or "").strip().casefold()
        chosen_reference = str(
            chosen.get("reference") or chosen.get("name") or ""
        ).strip().casefold()
        chosen_nd = finite_float(chosen.get("nd"), math.nan)
        chosen_vd = finite_float(chosen.get("vd"), math.nan)
        chosen_in_candidates = False
        for candidate in candidates:
            candidate_reference = str(
                candidate.get("reference") or candidate.get("name") or ""
            ).strip().casefold()
            candidate_library = glass_record_library(candidate).strip().casefold()
            candidate_reference_aliases = {
                alias.casefold()
                for alias in glass_record_reference_aliases(candidate)
            }
            # C++ catalog loading canonicalizes Material.reference to the catalog
            # library or the canonical source library of a merged catalog (for
            # example PLASTIC2026 -> PLASTIC2022), while candidate tables retain
            # the glass reference.  These forms describe the same candidate when
            # its name and optical constants also match.
            reference_matches = (
                candidate_reference == chosen_reference
                or (
                    bool(candidate_library)
                    and candidate_library == chosen_reference
                )
                or chosen_reference in candidate_reference_aliases
            )
            if (
                str(candidate.get("name") or "").strip().casefold() == chosen_name
                and reference_matches
                and math.isfinite(chosen_nd)
                and math.isfinite(chosen_vd)
                and abs(finite_float(candidate.get("nd"), math.nan) - chosen_nd) <= 1.0e-8
                and abs(finite_float(candidate.get("vd"), math.nan) - chosen_vd) <= 1.0e-6
            ):
                chosen_in_candidates = True
                break
        if not libraries:
            errors.append(f"surface {surface_index}: no declared glass libraries")
        if not candidates:
            errors.append(f"surface {surface_index}: no glass candidates")
        if unexpected:
            errors.append(
                f"surface {surface_index}: candidates outside {libraries}: {unexpected}"
            )
        if invalid_wavelength_candidates:
            errors.append(
                f"surface {surface_index}: candidates outside system wavelength range "
                f"{system_wavelengths_um(system)}: {invalid_wavelength_candidates}"
            )
        if not chosen_in_candidates:
            errors.append(
                f"surface {surface_index}: chosen glass "
                f"{(chosen_name, chosen_reference, chosen_nd, chosen_vd)!r} "
                "is not in its candidate table"
            )
        rows.append(
            {
                "surface_index": surface_index,
                "libraries": libraries,
                "candidate_count": len(candidates),
                "candidate_libraries": candidate_libraries,
                "chosen_name": chosen.get("name", ""),
                "chosen_reference": chosen.get("reference", ""),
                "chosen_in_candidates": chosen_in_candidates,
                "required_wavelengths_um": system_wavelengths_um(system),
                "wavelength_coverage_verified": not invalid_wavelength_candidates,
            }
        )
    if errors:
        raise RuntimeError("glass catalog contract failed: " + "; ".join(errors))
    return {
        "ok": True,
        "required_wavelengths_um": system_wavelengths_um(system),
        "groups": rows,
    }

def continuous_problem(source_dir: Path, system: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any], None]:
    return system, variables_doc_from_system(source_dir, system), None

def locked_glass_problem(source_dir: Path, system: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any], None]:
    """Keep materialized real glasses fixed and expose geometry variables only."""
    variables_doc = variables_doc_from_system(source_dir, system)
    variables_doc["variables"] = [
        copy.deepcopy(variable)
        for variable in variables_doc.get("variables", []) or []
        if str(variable.get("kind", "")).lower()
        not in {"material_nd", "material_vd", "glass_choice"}
    ]
    return copy.deepcopy(system), variables_doc, None

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

def canonical_image_quality_metric(value: object, default: str = "inherit") -> str:
    metric = str(value or default).strip().lower().replace("-", "_").replace(" ", "_")
    aliases = {
        "spot": "rms_spot",
        "spot_rms": "rms_spot",
        "wavefront": "rms_wavefront",
        "wavefront_rms": "rms_wavefront",
        "opd": "rms_wavefront",
        "spot_wavefront": "rms_spot_wavefront",
        "wavefront_spot": "rms_spot_wavefront",
        "spot_plus_wavefront": "rms_spot_wavefront",
        "wavefront_plus_spot": "rms_spot_wavefront",
    }
    metric = aliases.get(metric, metric)
    if metric in {"rms_spot", "rms_wavefront", "rms_spot_wavefront"}:
        return metric
    return default

def image_quality_operands(
    metric: str,
    weight: float,
    spot_tail_weight: float = 0.012,
) -> list[dict[str, Any]]:
    operands: list[dict[str, Any]] = []
    if metric in {"rms_spot", "rms_spot_wavefront"}:
        operands.append({"name": "rms_spot", "mode": "minimize", "weight": weight})
        if spot_tail_weight > 0.0:
            operands.append({
                "name": "single_ray_spot_tail",
                "mode": "minimize",
                "weight": spot_tail_weight,
            })
    if metric in {"rms_wavefront", "rms_spot_wavefront"}:
        operands.append({"name": "rms_wavefront", "mode": "minimize", "weight": weight})
        operands.append({"name": "single_ray_wavefront_guard", "mode": "less_than", "upper": 10.0, "weight": 1.0})
    return operands

def adjusted_merit(source_dir: Path, args: argparse.Namespace, material_mode: str) -> dict[str, Any]:
    merit = copy.deepcopy(read_json(file_from_source(source_dir, "merit.json")))
    dynamic_aperture_contract.apply_merit(merit)
    trace_rays = int(getattr(args, "trace_rays", 0) or 0)
    trace_arms = int(getattr(args, "trace_arms", 0) or 0)
    trace_distribution = str(getattr(args, "trace_distribution", "") or "").strip()
    if trace_rays > 0:
        merit["num_rays"] = trace_rays
        # Image sampling must not change the dynamic 4x8 aperture envelope.
    if trace_arms > 0:
        merit["pupil_arms"] = trace_arms
    if trace_distribution:
        merit["pupil_distribution"] = trace_distribution
    if bool(args.drop_glass_proximity_for_true_glass) and material_mode == "true_glass":
        merit.pop("glass_proximity", None)
    # Fixed-glass feedback inherits the candidate's complete merit contract.
    # Only the field grid (stored in system.json) and ray sampling above are
    # feedback-stage execution choices.
    if bool(getattr(args, "inherit_source_merit", True)):
        return merit
    if float(args.edge_weight_override) >= 0.0:
        merit["weight_edge"] = float(args.edge_weight_override)
    if float(args.min_edge_thickness_override) >= 0.0:
        merit["min_edge_thickness"] = float(args.min_edge_thickness_override)
    if float(args.spot_weight_override) >= 0.0:
        merit["weight_spot"] = float(args.spot_weight_override)
        for operand in merit.get("operands", []) or []:
            if str(operand.get("name") or "").lower() == "rms_spot":
                operand["weight"] = float(args.spot_weight_override)
    if float(args.quality_spot_scale_override) > 0.0:
        merit["quality_spot_scale"] = float(args.quality_spot_scale_override)
    if float(getattr(args, "quality_wavefront_scale_override", 0.0)) > 0.0:
        merit["quality_wavefront_scale"] = float(getattr(args, "quality_wavefront_scale_override"))
    wavefront_reference = str(getattr(args, "wavefront_reference_override", "") or "").strip()
    if wavefront_reference:
        merit["wavefront_reference"] = wavefront_reference
    rms_mode = canonical_image_quality_metric(getattr(args, "rms_mode_override", "inherit"), "inherit")
    if rms_mode != "inherit":
        merit["image_quality_metric"] = rms_mode
        weight = float(args.spot_weight_override) if float(args.spot_weight_override) >= 0.0 else float(merit.get("weight_spot", 1.0) or 1.0)
        kept = [
            copy.deepcopy(operand)
            for operand in merit.get("operands", []) or []
            if str(operand.get("name") or "").lower() not in {
                "rms_spot",
                "rms_wavefront",
                "single_ray_spot_tail",
                "single_ray_spot_guard",
                "single_ray_wavefront_guard",
            }
        ]
        spot_tail_weight = max(
            0.0,
            float(merit.get("unified_image_single_ray_spot_tail_weight", 0.012) or 0.0),
        )
        merit["unified_image_single_ray_spot_tail_weight"] = spot_tail_weight
        merit["operands"] = kept + image_quality_operands(
            rms_mode,
            weight,
            spot_tail_weight,
        )
    if float(args.distortion_upper_override) >= 0.0:
        merit["distortion_upper"] = float(args.distortion_upper_override)
    return merit

def base_command_prefix(source_command: list[str]) -> list[str]:
    command = [source_command[0]]
    append_kept_flags(command, source_command)
    # Feedback sources are serialized optimizer outputs whose clear apertures
    # have already been materialized.  Keep dynamic aperture updates enabled
    # for trial geometries, but do not apply the non-idempotent transition once
    # more while loading the unchanged input system.
    for flag in [
        "--cpu",
        "--aiming-best-effort",
        "--clear-aperture-update",
        "--input-clear-apertures-materialized",
        "--sync-stop-aperture",
    ]:
        if flag not in command:
            command.append(flag)
    return dynamic_aperture_contract.apply_command(command, materialized_input=True)

def add_glass_args(command: list[str], glass_path: Path, args: argparse.Namespace) -> None:
    command.extend(
        [
            "--glass-search",
            "--glass-search-config",
            str(glass_path),
            "--glass-mutation-rate",
            str(args.glass_mutation_rate),
            "--glass-random-rate",
            str(args.glass_random_rate),
            "--glass-pbest-inherit-rate",
            str(args.glass_pbest_inherit_rate),
            "--glass-neighbor-count",
            str(int(args.glass_neighbor_count)),
        ]
    )

def disable_post_outer_key_tracking(command: list[str]) -> None:
    if "--basin-disable" not in command:
        command.append("--basin-disable")
    if "--adaptive-no-topology-summary" not in command:
        command.append("--adaptive-no-topology-summary")

def set_command_arg(command: list[str], flag: str, value: object) -> None:
    text = str(value)
    if flag in command:
        index = command.index(flag)
        if index + 1 < len(command):
            command[index + 1] = text
            return
    command.extend([flag, text])

def configure_near_trace_acceptance(
    command: list[str], args: argparse.Namespace, *, adaptive: bool
) -> None:
    min_trace = min(
        1.0,
        max(
            0.0,
            finite_float(
                getattr(
                    args,
                    "min_trace_coverage",
                    DE_DLS_TRACE_COVERAGE_BASELINE,
                ),
                DE_DLS_TRACE_COVERAGE_BASELINE,
            ),
        ),
    )
    min_field = min(1.0, max(0.0, finite_float(getattr(args, "min_field_coverage", min_trace), min_trace)))
    epsilon = 1.0 - min(min_trace, min_field)
    set_command_arg(command, "--trace-coverage-epsilon", f"{epsilon:.17g}")
    if adaptive and "--adaptive-allow-near-trace-infeasible" not in command:
        command.append("--adaptive-allow-near-trace-infeasible")
    if adaptive:
        command[:] = [
            token
            for token in command
            if token
            not in {
                "--local-require-trace-feasible",
                "--local-allow-trace-infeasible",
                "--local-allow-near-trace-infeasible",
            }
        ]
        command.append("--local-allow-near-trace-infeasible")
        set_command_arg(command, "--local-trace-coverage-epsilon", f"{epsilon:.17g}")

def cuda_de_executable(args: argparse.Namespace) -> Path:
    return Path(
        str(getattr(args, "de_cuda_exe", "") or (ROOT / "build" / "lensde_de_cuda.exe"))
    ).resolve()

def configure_de_evaluator(command: list[str], args: argparse.Namespace) -> list[str]:
    out = list(command)
    if not bool(getattr(args, "de_use_cuda", False)):
        return out
    cuda_exe = cuda_de_executable(args)
    if not cuda_exe.exists():
        raise FileNotFoundError(f"fixed-glass CUDA DE executable not found: {cuda_exe}")
    out[0] = str(cuda_exe)
    out = [value for value in out if value != "--cpu"]
    set_command_arg(out, "--de-cuda-min-batch", max(1, int(getattr(args, "de_cuda_min_batch", 1))))
    return out

def make_command(
    *,
    source_command: list[str],
    system_path: Path,
    merit_path: Path,
    variables_path: Path,
    seed_path: Path,
    glass_path: Path | None,
    out_dir: Path,
    algorithm: str,
    seed: int,
    args: argparse.Namespace,
) -> list[str]:
    variant = "basic_sa" if algorithm == "adaptive_sa" else str(args.de_variant)
    command = base_command_prefix(source_command)
    trace_rays = int(getattr(args, "trace_rays", 0) or 0)
    trace_arms = int(getattr(args, "trace_arms", 0) or 0)
    trace_distribution = str(getattr(args, "trace_distribution", "") or "").strip()
    if trace_rays > 0:
        set_command_arg(command, "--rays", trace_rays)
    if trace_arms > 0:
        set_command_arg(command, "--arms", trace_arms)
    if trace_distribution:
        set_command_arg(command, "--distribution", trace_distribution)
    disable_post_outer_key_tracking(command)
    set_command_arg(
        command,
        "--constraint-handling",
        str(getattr(args, "constraint_handling", "feasibility_rules")),
    )
    command.extend(
        [
            "--system",
            str(system_path),
            "--merit",
            str(merit_path),
            "--variables",
            str(variables_path),
            "--initial-population",
            str(seed_path),
            "--out-dir",
            str(out_dir),
        ]
    )
    if glass_path is not None:
        add_glass_args(command, glass_path, args)
    if algorithm == "pure_de":
        command.extend(
            [
                "--de",
                "--pop",
                str(int(args.population)),
                "--gen",
                str(int(args.generations)),
                "--de-tol",
                "0",
                "--de-atol",
                "0",
                "--de-strategy",
                str(args.de_strategy),
                "--seed",
                str(seed),
                "--optimizer-internal-scaling",
                "bound_normalized",
            ]
        )
        configure_near_trace_acceptance(command, args, adaptive=False)
        return configure_de_evaluator(command, args)

    command.extend(
        [
            "--adaptive-hybrid-de-dls",
            "--pop",
            str(int(args.population)),
            "--gen",
            str(int(args.generations)),
            "--hybrid-outer",
            str(int(args.outer)),
            "--hybrid-de-gen",
            str(int(args.generations)),
            "--hybrid-parents",
            str(int(args.hybrid_parents)),
            "--hybrid-polish-workers",
            "0",
            "--hybrid-refresh",
            str(args.refresh),
            "--hybrid-distance-metric",
            "bound_normalized",
            "--de-variant",
            variant,
            "--seed",
            str(seed),
            "--hybrid-local-iter",
            str(int(args.local_iter)),
            "--polish-candidates",
            str(int(args.polish_candidates)),
            "--polish-selection",
            "merit",
            "--local-method",
            str(args.method),
            "--local-bounds-mode",
            str(args.local_bounds_mode),
            "--local-residual-mode",
            str(args.local_residual_mode),
            "--local-lambda",
            str(args.local_lambda),
            "--local-fd",
            str(args.local_fd),
            "--local-tol",
            str(args.local_tol),
            "--local-max-reject",
            str(int(args.local_max_reject)),
            "--optimizer-internal-scaling",
            "bound_normalized",
        ]
    )
    if algorithm == "adaptive_sa":
        command.extend(
            [
                "--sa-step-scale-start",
                str(args.sa_step_start),
                "--sa-step-scale-end",
                str(args.sa_step_end),
                "--sa-temperature-start",
                str(args.sa_temp_start),
                "--sa-temperature-end",
                str(args.sa_temp_end),
                "--sa-temperature-power",
                str(args.sa_temp_power),
            ]
        )
    configure_near_trace_acceptance(command, args, adaptive=True)
    return configure_de_evaluator(command, args)

def run_command(
    command: list[str],
    cwd: Path,
    log_path: Path,
    timeout: int,
    child_cpu_workers: int = 0,
) -> dict[str, Any]:
    log_path = native_io_paths.python_path(log_path)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    start = time.time()
    env = None
    if child_cpu_workers > 0:
        env = os.environ.copy()
        env["OPTILAND_CPU_WORKERS"] = str(child_cpu_workers)
    with native_io_paths.native_command(command, cwd, force_native=True) as native_command:
        try:
            proc = subprocess.run(
                native_command,
                cwd=str(cwd),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
                timeout=timeout,
            )
            stdout = proc.stdout or ""
            returncode = proc.returncode
            timed_out = False
        except subprocess.TimeoutExpired as exc:
            stdout = str(exc.stdout or "")
            returncode = -999
            timed_out = True
    seconds = time.time() - start
    log_path.write_text(stdout, encoding="utf-8", errors="replace")
    return {"returncode": returncode, "timed_out": timed_out, "seconds": seconds}

def score_to_row(
    *,
    cycle: int,
    source_rank: int,
    source_dir: Path,
    source_name: str,
    material_mode: str,
    algorithm: str,
    parent_score: dict[str, Any],
    score: dict[str, Any],
    system: dict[str, Any],
    run_result: dict[str, Any],
    out_dir: Path,
    args: argparse.Namespace,
    merit: dict[str, Any] | None = None,
) -> dict[str, Any]:
    parent_mean = 1000.0 * finite_float(parent_score.get("mean_spot"))
    mean_spot = 1000.0 * finite_float(score.get("mean_spot"))
    row: dict[str, Any] = {
        "cycle": cycle,
        "source_rank": source_rank,
        "source": source_name,
        "source_dir": str(source_dir),
        "material_mode": material_mode,
        "algorithm": algorithm,
        "merit": score.get("merit"),
        "mean_spot_um": mean_spot,
        "max_spot_um": 1000.0 * finite_float(score.get("max_spot")),
        "max_distortion": score.get("max_distortion"),
        "constraint_group_error": score.get("constraint_group_error"),
        "min_edge": score.get("min_edge"),
        "trace_coverage": score.get("trace_coverage"),
        "min_field_coverage": score.get("min_field_coverage"),
        "min_chief_progress": score.get("min_chief_progress"),
        "chief_ray_ok": score.get("chief_ray_ok"),
        "chief_ray_failed_fields": score.get("chief_ray_failed_fields"),
        "zero_coverage_off_axis_fields": score.get("zero_coverage_off_axis_fields"),
        "parent_mean_spot_um": parent_mean,
        "delta_parent_mean_spot_um": mean_spot - parent_mean,
        "glass_choices": glass_choices_from_system(system),
        "returncode": run_result.get("returncode"),
        "timed_out": run_result.get("timed_out"),
        "seconds": run_result.get("seconds"),
        "out_dir": str(out_dir),
    }
    if bool(getattr(args, "inherit_source_merit", True)) and isinstance(merit, dict):
        trace_coverage = finite_float(score.get("trace_coverage"), 0.0)
        field_coverage = finite_float(score.get("min_field_coverage"), 0.0)
        trace_ok = (
            bool(score.get("trace_ok", False))
            and trace_coverage >= float(args.min_trace_coverage)
            and field_coverage >= float(args.min_field_coverage)
            and vignetting_handoff_policy.chief_ray_usable(score)
        )
        distortion_limit = finite_float(merit.get("distortion_upper"), math.inf)
        for operand in merit.get("operands", []) or []:
            if str(operand.get("name") or "").lower() == "max_distortion":
                distortion_limit = finite_float(operand.get("upper"), distortion_limit)
                break
        contributions = score.get("contributions", []) or []
        constraint_residuals = [
            finite_float(item.get("normalized_residual"), math.inf)
            for item in contributions
            if str(item.get("category") or "").lower() == "constraint"
        ]
        required_image_residuals = [
            finite_float(item.get("normalized_residual"), math.inf)
            for item in contributions
            if str(item.get("category") or "").lower() == "image_quality"
            and str(item.get("mode") or "").lower() != "minimize"
        ]
        min_edge_limit = finite_float(merit.get("min_edge_thickness"), -math.inf)
        row.update({
            "trace_ok": trace_ok,
            "distortion_ok": finite_float(score.get("max_distortion"), math.inf) <= distortion_limit,
            "constraint_ok": (
                all(value <= 0.0 for value in constraint_residuals)
                and finite_float(score.get("min_edge"), -math.inf) >= min_edge_limit
            ),
            # Minimize-only image operands are ranked through the inherited
            # merit itself.  quality_ok reports only explicit image limits.
            "quality_ok": all(value <= 0.0 for value in required_image_residuals),
        })
    else:
        row.update(quality_flags(score, args))
    return row

def sort_rows(rows: list[dict[str, Any]], args: argparse.Namespace | None = None) -> list[dict[str, Any]]:
    def truthy(value: object) -> bool:
        return str(value).strip().lower() in {"1", "true", "yes", "y"}

    def key(row: dict[str, Any]) -> tuple[int, float, float]:
        return_bad = 0
        if int(finite_float(row.get("returncode"), -1)) != 0 or truthy(row.get("timed_out")):
            return_bad = 1
        mean_spot = finite_float(row.get("mean_spot_um"))
        merit = finite_float(row.get("merit"))
        return return_bad, merit, mean_spot

    out = sorted(rows, key=key)
    for index, row in enumerate(out, start=1):
        row["rank"] = index
    return out

def default_sources(run_dir: Path, args: argparse.Namespace) -> list[Path]:
    sources: list[Path] = []
    base = run_dir / "de_dls_key_outputs"
    for folder, limit in [
        ("continuous_presnap_topologies", int(args.default_continuous_sources)),
        ("final_material_topologies", int(args.default_final_sources)),
    ]:
        parent = base / folder
        if not parent.exists():
            continue
        sources.extend(sorted([path for path in parent.iterdir() if path.is_dir()])[:limit])
    return sources

def selected_next_sources(rows: list[dict[str, Any]], limit: int, args: argparse.Namespace | None = None) -> list[Path]:
    def truthy(value: object) -> bool:
        return str(value).strip().lower() in {"1", "true", "yes", "y"}

    out: list[Path] = []
    seen: set[str] = set()
    for row in sort_rows(rows, args):
        if int(finite_float(row.get("returncode"), -1)) != 0 or truthy(row.get("timed_out")):
            continue
        path = Path(str(row.get("out_dir") or "")).resolve()
        if not (path / "system.json").exists():
            continue
        key = str(path).lower()
        if key in seen:
            continue
        seen.add(key)
        out.append(path)
        if len(out) >= limit:
            break
    return out

def make_case(
    *,
    cycle: int,
    source_rank: int,
    source_dir: Path,
    material_mode: str,
    algorithm: str,
    out_root: Path,
    glasses: list[dict[str, Any]],
    args: argparse.Namespace,
) -> dict[str, Any]:
    source_name = source_dir.name
    source_system = read_json(file_from_source(source_dir, "system.json"))
    parent_score = read_json(file_from_source(source_dir, "score.json"))
    source_command = load_command(source_dir)
    if material_mode == "true_glass":
        problem_system, variables_doc, glass_search = true_glass_problem(
            source_dir=source_dir,
            system=source_system,
            glasses=glasses,
            args=args,
        )
    elif material_mode == "continuous":
        problem_system, variables_doc, glass_search = continuous_problem(source_dir, source_system)
    else:
        raise ValueError(f"Unknown material mode: {material_mode}")
    problem_system = with_field_hy_fractions(
        problem_system, str(getattr(args, "field_hy_fractions", "") or "")
    )

    case_root = (
        out_root
        / f"cycle_{cycle:02d}"
        / f"{source_rank:02d}_{safe_name(source_name)}"
        / f"{safe_name(material_mode)}_{safe_name(algorithm)}"
    )
    system_path = case_root / "system_input.json"
    variables_path = case_root / "variables.json"
    merit_path = case_root / "merit.json"
    seed_path = case_root / "seed_population.csv"
    glass_path = case_root / "glass_search_config.json" if glass_search else None
    case_out = case_root / "run"

    write_json(system_path, problem_system)
    write_json(variables_path, variables_doc)
    feedback_merit = adjusted_merit(source_dir, args, material_mode)
    write_json(merit_path, feedback_merit)
    if glass_search and glass_path:
        write_json(glass_path, glass_search)
    population = build_population(
        system=problem_system,
        variables_doc=variables_doc,
        glasses=glasses,
        material_mode=material_mode,
        population=int(args.population),
        seed=int(args.rng_seed) + cycle * 100000 + source_rank * 1009 + len(algorithm),
        scale=str(args.population_scale),
        args=args,
    )
    write_population(seed_path, population)

    command = make_command(
        source_command=source_command,
        system_path=system_path,
        merit_path=merit_path,
        variables_path=variables_path,
        seed_path=seed_path,
        glass_path=glass_path,
        out_dir=case_out,
        algorithm=algorithm,
        seed=int(args.seed) + cycle * 10000 + source_rank * 100 + len(algorithm),
        args=args,
    )
    trace_rays = int(getattr(args, "trace_rays", 0) or 0)
    trace_arms = int(getattr(args, "trace_arms", 0) or 0)
    trace_distribution = str(getattr(args, "trace_distribution", "") or "").strip()
    write_json(case_root / "command.json", command)
    write_json(
        case_root / "case_input_summary.json",
        {
            "cycle": cycle,
            "source_dir": str(source_dir),
            "material_mode": material_mode,
            "algorithm": algorithm,
            "de_evaluator": "CUDA" if bool(getattr(args, "de_use_cuda", False)) else "CPU",
            "de_executable": command[0],
            "trace_rays": trace_rays,
            "trace_arms": trace_arms,
            "trace_distribution": trace_distribution,
            "field_hy_fractions": parse_field_hy_fractions(
                str(getattr(args, "field_hy_fractions", "") or "")
            ),
            "field_count": len(problem_system.get("fields", []) or []),
            "population_rows": len(population),
            "variable_count": len(variables_doc.get("variables") or []),
            "glass_group_count": len((glass_search or {}).get("groups") or []),
        },
    )
    print(
        f"cycle {cycle} source {source_rank} {source_name} {material_mode}/{algorithm}: "
        f"pop={len(population)} vars={len(variables_doc.get('variables') or [])} gen={int(args.generations)}"
    )
    result = run_command(command, ROOT, case_root / "run.log", int(args.timeout))
    score_path = case_out / "score.json"
    system_out_path = case_out / "system.json"
    score = read_json(score_path) if score_path.exists() else {}
    system_out = read_json(system_out_path) if system_out_path.exists() else source_system
    row = score_to_row(
        cycle=cycle,
        source_rank=source_rank,
        source_dir=source_dir,
        source_name=source_name,
        material_mode=material_mode,
        algorithm=algorithm,
        parent_score=parent_score,
        score=score,
        system=system_out,
        run_result=result,
        out_dir=case_out,
        args=args,
        merit=feedback_merit,
    )
    write_json(case_root / "summary.json", row)
    print(
        f"  -> mean={finite_float(row.get('mean_spot_um')):.3f}um "
        f"merit={finite_float(row.get('merit')):.6g} "
        f"dist={finite_float(row.get('max_distortion')):.4g} "
        f"constraint={finite_float(row.get('constraint_group_error')):.4g} "
        f"edge={finite_float(row.get('min_edge'), -math.inf):.4g} "
        f"return={result.get('returncode')} seconds={finite_float(result.get('seconds'), 0.0):.1f}"
    )
    return row

def run(args: argparse.Namespace) -> None:
    if int(args.generations) < 400 and not bool(args.allow_short_generations):
        print(f"Requested generations={args.generations}; promoting to 400 for warm-up.")
        args.generations = 400
    out_root = Path(args.out_dir).resolve()
    out_root.mkdir(parents=True, exist_ok=True)
    source_dirs = source_dirs_arg(str(args.source_dirs))
    if not source_dirs:
        source_dirs = default_sources(Path(args.run_dir).resolve(), args)
    source_dirs = source_dirs[: max(1, int(args.initial_source_limit))]
    algorithms = list_arg(str(args.algorithms))
    material_modes = list_arg(str(args.material_modes))
    source_systems = [
        read_json(file_from_source(source_dir, "system.json"))
        for source_dir in source_dirs
    ]
    libraries = glass_libraries_for_systems(source_systems)
    glass_wavelength_audit: dict[str, Any] = {}
    glasses = load_glasses(
        Path(args.glass_database),
        libraries,
        systems_wavelengths_um(source_systems),
        audit=glass_wavelength_audit,
    )

    all_rows: list[dict[str, Any]] = []
    current_sources = source_dirs
    write_json(
        out_root / "manifest.json",
        {
            "initial_sources": [str(path) for path in source_dirs],
            "algorithms": algorithms,
            "material_modes": material_modes,
            "generations": int(args.generations),
            "population": int(args.population),
            "cycles": int(args.cycles),
            "de_evaluator": "CUDA" if bool(getattr(args, "de_use_cuda", False)) else "CPU",
            "de_executable": str(cuda_de_executable(args)) if bool(getattr(args, "de_use_cuda", False)) else "source CPU executable",
            "de_cuda_min_batch": max(1, int(getattr(args, "de_cuda_min_batch", 1))),
            "material_wavelength_filter": glass_wavelength_audit,
        },
    )
    for cycle in range(1, int(args.cycles) + 1):
        cycle_rows: list[dict[str, Any]] = []
        for source_rank, source_dir in enumerate(current_sources, start=1):
            for material_mode in material_modes:
                for algorithm in algorithms:
                    row = make_case(
                        cycle=cycle,
                        source_rank=source_rank,
                        source_dir=source_dir.resolve(),
                        material_mode=material_mode,
                        algorithm=algorithm,
                        out_root=out_root,
                        glasses=glasses,
                        args=args,
                    )
                    cycle_rows.append(row)
                    all_rows.append(row)
                    write_csv(out_root / "cycle_summary_partial.csv", sort_rows(all_rows, args))
                    write_json(out_root / "cycle_summary_partial.json", {"rows": sort_rows(all_rows, args)})
        cycle_sorted = sort_rows(cycle_rows, args)
        write_csv(out_root / f"cycle_{cycle:02d}_summary.csv", cycle_sorted)
        write_json(out_root / f"cycle_{cycle:02d}_summary.json", {"rows": cycle_sorted})
        current_sources = selected_next_sources(cycle_rows, int(args.elites_per_cycle), args)
        if not current_sources:
            print(f"cycle {cycle}: no valid next sources, stopping")
            break
        print("next sources:")
        for path in current_sources:
            print(f"  {path}")

    rows = sort_rows(all_rows, args)
    write_csv(out_root / "cycle_summary.csv", rows)
    write_json(out_root / "cycle_summary.json", {"rows": rows})
    print(f"Wrote {out_root / 'cycle_summary.csv'}")
