from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import argparse
from lensde.runtime import concurrency as concurrent
import csv
import hashlib
import json
import math
import os
import re
import shutil
from lensde.runtime import processes as subprocess
import sys
import time
from pathlib import Path
from typing import Any


SCRIPT_PATH = Path(__file__).resolve()
GUI_DIR = PROJECT_ROOT / "web"
CPP_DIR = PROJECT_ROOT / "native"
REPO_ROOT = PROJECT_ROOT


from lensde.engine import core as server  # noqa: E402


POLISH_RE = re.compile(r"polish\s+(\d+)\s+best merit\s+([0-9.eE+-]+)")
DEFAULT_MAX_LOCAL_ITER_PER_RESTART = 400
MATERIAL_FUNNEL_STAGE_CONTRACT = "sampled_pupil_cpu_materialized_dls_v2"
DE_DLS_TRACE_COVERAGE_BASELINE = 0.95

COMMAND_VALUE_FLAGS = {
    "--system",
    "--merit",
    "--variables",
    "--out-dir",
    "--local-iter",
    "--local-lambda",
    "--local-fd",
    "--local-tol",
    "--local-max-reject",
    "--local-plateau-window",
    "--local-plateau-min-iter",
    "--local-plateau-min-abs-improve",
    "--local-plateau-min-rel-improve",
    "--optimizer-internal-scaling",
    "--local-method",
    "--local-bounds-mode",
    "--local-residual-mode",
}

REMOVE_VALUE_FLAGS = {
    "--popsize",
    "--gen",
    "--de-strategy",
    "--mutation-min",
    "--mutation-max",
    "--recombination",
    "--de-tol",
    "--de-atol",
    "--seed",
    "--hybrid-outer",
    "--hybrid-de-gen",
    "--hybrid-parents",
    "--hybrid-refresh",
    "--hybrid-distance-metric",
    "--hybrid-local-iter",
    "--hybrid-polish-workers",
    "--polish-candidates",
    "--polish-refill-max-extra",
    "--glass-search-config",
    "--candidate-pool",
    "--seed-candidate-pool",
    "--initial-population",
    "--progress-every",
}

REMOVE_BOOL_FLAGS = {
    "--de",
    "--polish",
    "--hybrid-de-dls",
    "--adaptive-hybrid-de-dls",
    "--batched-polish",
    "--select-seed-population",
    "--adaptive-glass-tolerance",
    "--adaptive-no-glass-tolerance",
}

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
    "--aiming-continuation-steps",
    "--aiming-stop-radius-source",
    "--local-min-step",
    "--local-active-set-tol",
    "--local-trust-radius",
    "--local-trust-min-radius",
    "--local-trust-max-radius",
    "--local-trust-eta",
    "--local-trace-coverage-epsilon",
}

KEEP_VALUE_PREFIXES = (
    "--aiming-calibrated-",
    "--aiming-pilot-",
    "--aiming-three-step-",
)

KEEP_BOOL_FLAGS = {
    "--aiming-best-effort",
    "--aiming-require-convergence",
    "--clear-aperture-update",
    "--sync-stop-aperture",
    "--cpu",
    "--local",
    "--local-require-trace-feasible",
    "--local-allow-trace-infeasible",
    "--local-allow-near-trace-infeasible",
    "--local-projected-active-set",
    "--local-project-active-set",
    "--local-qr-solver",
    "--local-fd-diagnostics",
    "--dls-fd-diagnostics",
}

DEFAULT_STAGES: list[dict[str, Any]] = [
    {
        "name": "s01_continuous_vd5_tol1p5",
        "variable_mode": "geometry_material",
        "local_iter": 500,
        "glass_tolerance": 1.5,
        "glass_weight": 0.5,
        "nd_scale": 0.01,
        "vd_scale": 5.0,
        "vd_metric": "relative_log",
        "vd_relative_scale": 0.05,
        "keep": 16,
        "local_bounds_mode": "safety_material_strict",
    },
    {
        "name": "s02_continuous_vd2_tol1p0",
        "variable_mode": "geometry_material",
        "local_iter": 700,
        "glass_tolerance": 1.0,
        "glass_weight": 0.5,
        "nd_scale": 0.01,
        "vd_scale": 2.0,
        "vd_metric": "relative_log",
        "vd_relative_scale": 0.05,
        "keep": 12,
        "local_bounds_mode": "safety_material_strict",
    },
    {
        "name": "s03_continuous_vd2_tol0p5",
        "variable_mode": "geometry_material",
        "local_iter": 900,
        "glass_tolerance": 0.5,
        "glass_weight": 0.5,
        "nd_scale": 0.01,
        "vd_scale": 2.0,
        "vd_metric": "relative_log",
        "vd_relative_scale": 0.05,
        "keep": 8,
        "local_bounds_mode": "safety_material_strict",
    },
    *[
        {
            "name": f"s04_s05_snap_one_{index + 1:02d}",
            "operation": "snap_nearest",
            "snap_count": 1,
            "variable_mode": "geometry_material",
            "local_iter": 350,
            "glass_tolerance": 0.5,
            "glass_weight": 0.5,
            "nd_scale": 0.01,
            "vd_scale": 2.0,
            "vd_metric": "relative_log",
            "vd_relative_scale": 0.05,
            "keep": 8 if index < 3 else 6,
            "local_bounds_mode": "safety_material_strict",
        }
        for index in range(6)
    ],
    {
        "name": "s06_locked_glass_refine",
        "variable_mode": "geometry",
        "local_iter": 1500,
        "keep": 6,
        "local_bounds_mode": "safety_material_strict",
    },
]


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def finite_float(value: Any, default: float = math.nan) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default


def positive_int(value: Any, default: int = 1) -> int:
    try:
        out = int(float(value))
    except (TypeError, ValueError):
        return default
    return max(1, out)


def nonnegative_int(value: Any, default: int = 0) -> int:
    try:
        out = int(float(value))
    except (TypeError, ValueError):
        return default
    return max(0, out)


def parse_field_hy_fractions(text: str) -> list[float]:
    values: list[float] = []
    for token in str(text or "").replace(";", ",").split(","):
        token = token.strip()
        if not token:
            continue
        value = float(token)
        if not math.isfinite(value) or value < 0.0 or value > 1.0:
            raise ValueError(
                f"field hy fraction must be finite in [0,1], got {token!r}"
            )
        values.append(value)
    if values and any(right <= left for left, right in zip(values, values[1:])):
        raise ValueError("field hy fractions must be strictly increasing")
    return values


def with_field_hy_fractions(system: dict[str, Any], text: str) -> dict[str, Any]:
    """Select an exact meridional field grid without erasing vignetting."""
    fractions = parse_field_hy_fractions(text)
    out = json.loads(json.dumps(system))
    if not fractions:
        return out
    source_fields = [
        json.loads(json.dumps(field))
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
                f"active-vignetting system has no aperture-realized Hy={value:.12g} field"
            )
        fields.append(
            {
                "hx": 0.0,
                "hy": value,
                "fvcx": 0.0,
                "fvcy": 0.0,
                "fvdx": 0.0,
                "fvdy": 0.0,
                "vignette_x": 0.0,
                "vignette_y": 0.0,
            }
        )
    out["fields"] = fields
    return out


def requested_field_hy_fractions(
    args: argparse.Namespace,
    stage: dict[str, Any] | None = None,
) -> str:
    if (
        not bool(getattr(args, "force_global_sampling", False))
        and stage is not None
        and "field_hy_fractions" in stage
    ):
        return str(stage.get("field_hy_fractions") or "").strip()
    return str(getattr(args, "field_hy_fractions", "") or "").strip()


def requested_trace_sampling(
    args: argparse.Namespace,
    stage: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Return optional pupil-sampling overrides for a material DLS stage."""
    stage = {} if bool(getattr(args, "force_global_sampling", False)) else (stage or {})
    rays = nonnegative_int(stage.get("trace_rays", getattr(args, "trace_rays", 0)), 0)
    arms = nonnegative_int(stage.get("trace_arms", getattr(args, "trace_arms", 0)), 0)
    distribution = str(
        stage.get(
            "trace_distribution",
            getattr(args, "trace_distribution", ""),
        )
        or ""
    ).strip()
    return {
        "num_rays": rays,
        "pupil_arms": arms,
        "pupil_distribution": distribution,
    }


def minimum_trace_coverages(args: argparse.Namespace) -> tuple[float, float]:
    trace = min(
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
    field = min(1.0, max(0.0, finite_float(getattr(args, "min_field_coverage", trace), trace)))
    return trace, field


def apply_trace_sampling_to_merit(
    merit: dict[str, Any],
    args: argparse.Namespace,
    stage: dict[str, Any] | None = None,
) -> dict[str, Any]:
    out = dict(merit)
    sampling = requested_trace_sampling(args, stage)
    if sampling["num_rays"] > 0:
        out["num_rays"] = sampling["num_rays"]
    if sampling["pupil_arms"] > 0:
        out["pupil_arms"] = sampling["pupil_arms"]
    if sampling["pupil_distribution"]:
        out["pupil_distribution"] = sampling["pupil_distribution"]
    return out


def resolved_child_cpu_workers(workers: int, requested: int) -> int:
    if requested > 0:
        return requested
    if workers <= 1:
        return 0
    detected = os.cpu_count() or 1
    return max(1, detected // workers)


def variable_key(variable: dict[str, Any]) -> tuple[str, int, int]:
    return (
        str(variable.get("kind", "")),
        int(finite_float(variable.get("index"), 0)),
        int(finite_float(variable.get("term"), 0)),
    )


def field_radius(field: dict[str, Any]) -> float:
    return math.hypot(finite_float(field.get("hx"), 0.0), finite_float(field.get("hy"), 0.0))


def set_arg(command: list[str], flag: str, value: str) -> None:
    try:
        index = command.index(flag)
    except ValueError:
        command.extend([flag, value])
        return
    if index + 1 >= len(command):
        command.append(value)
    else:
        command[index + 1] = value


def system_stop_radius_source(system_path: Path) -> str:
    """Read the aperture-radius policy from the concrete stage input."""
    try:
        system = read_json(system_path)
    except (OSError, json.JSONDecodeError):
        return "paraxial"
    ray_aiming = system.get("ray_aiming") if isinstance(system, dict) else None
    raw = ray_aiming.get("stop_radius_source") if isinstance(ray_aiming, dict) else None
    return server.canonical_stop_radius_source(raw, "paraxial")


def system_aiming_policy(system_path: Path) -> dict[str, Any]:
    try:
        system = read_json(system_path)
    except (OSError, json.JSONDecodeError):
        system = {}
    ray_aiming = system.get("ray_aiming") if isinstance(system, dict) else None
    if not isinstance(ray_aiming, dict):
        ray_aiming = {}
    return {
        "mode": server.canonical_aiming_mode(ray_aiming.get("mode"), "paraxial"),
        "max_iter": max(1, int(finite_float(ray_aiming.get("max_iter"), 20))),
        "require_convergence": bool(ray_aiming.get("require_convergence", False)),
        "continuation_steps": max(1, int(finite_float(ray_aiming.get("continuation_steps"), 8))),
        "stop_radius_source": server.canonical_stop_radius_source(
            ray_aiming.get("stop_radius_source"), "paraxial"
        ),
    }


def remove_optimizer_args(command: list[str]) -> list[str]:
    out: list[str] = []
    skip = False
    for token in command:
        if skip:
            skip = False
            continue
        if token in REMOVE_BOOL_FLAGS:
            continue
        if token in REMOVE_VALUE_FLAGS:
            skip = True
            continue
        out.append(token)
    return out


def sanitize_reference_command(command: list[str], exe: Path) -> list[str]:
    tokens = remove_optimizer_args([str(item) for item in command])
    out = [str(exe)]
    index = 1 if tokens else 0
    while index < len(tokens):
        token = tokens[index]
        next_token = tokens[index + 1] if index + 1 < len(tokens) else ""
        keep_value = token in KEEP_VALUE_FLAGS or any(token.startswith(prefix) for prefix in KEEP_VALUE_PREFIXES)
        if keep_value:
            out.append(token)
            if next_token and not next_token.startswith("--"):
                out.append(next_token)
                index += 2
            else:
                index += 1
            continue
        if token in KEEP_BOOL_FLAGS:
            out.append(token)
        index += 1
    if "--local" not in out:
        out.append("--local")
    if "--cpu" not in out:
        out.append("--cpu")
    return out


def variable_list(path: Path) -> list[dict[str, Any]]:
    data = read_json(path)
    if isinstance(data, dict):
        values = data.get("variables", [])
        return values if isinstance(values, list) else []
    return data if isinstance(data, list) else []


def first_existing(paths: list[Path]) -> Path:
    for path in paths:
        if path.exists():
            return path
    raise FileNotFoundError("none of these paths exists: " + ", ".join(str(path) for path in paths))


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


def load_reference_context(args: argparse.Namespace, basin_run_dir: Path) -> tuple[list[str], dict[str, Any], Path]:
    reference_run = args.reference_run.resolve() if args.reference_run else basin_run_dir
    run_meta: dict[str, Any] = {}
    reference_command: list[str] = []
    try:
        run_meta_path = first_existing(ancestor_file_paths(reference_run, "run_meta.json") + ancestor_file_paths(basin_run_dir, "run_meta.json"))
        run_meta = read_json(run_meta_path)
        reference_command = run_meta.get("command") if isinstance(run_meta.get("command"), list) else []
    except FileNotFoundError:
        try:
            command_path = first_existing(ancestor_file_paths(reference_run, "command.json") + ancestor_file_paths(basin_run_dir, "command.json"))
            command_data = read_json(command_path)
            if isinstance(command_data, list):
                reference_command = [str(item) for item in command_data]
            run_meta = {"command": reference_command, "command_source": str(command_path)}
        except FileNotFoundError:
            reference_command = []
            run_meta = {"command": [], "command_source": "generated_default"}
    exe = args.exe.resolve() if args.exe else Path(str(run_meta.get("executable") or (reference_command[0] if reference_command else CPP_DIR / "build" / "lensde_cpu.exe")))
    if not exe.is_absolute():
        exe = (CPP_DIR / exe).resolve()
    return reference_command, run_meta, exe


def materialize_basin_system(
    base_system: dict[str, Any],
    full_variables: list[dict[str, Any]],
    basin: dict[str, Any],
    *,
    reference_system: dict[str, Any] | None = None,
    min_field_count: int = 0,
) -> dict[str, Any]:
    values = [finite_float(value, 0.0) for value in basin.get("values", [])]
    system = server.apply_variable_values_to_system(base_system, full_variables, values, None)
    if reference_system and isinstance(reference_system.get("ray_aiming"), dict):
        system["ray_aiming"] = reference_system["ray_aiming"]
    if min_field_count > 0:
        system = server.densify_system_fields_for_asphere(system, min_field_count)
    return system


def surface_current_value(system: dict[str, Any], variable: dict[str, Any]) -> float:
    surfaces = system.get("surfaces", []) or []
    index = int(finite_float(variable.get("index"), -1))
    if not (0 <= index < len(surfaces)):
        return finite_float(variable.get("initial"), 0.0)
    surface = surfaces[index]
    kind = str(variable.get("kind") or "")
    if kind == "thickness":
        return finite_float(surface.get("thickness"), finite_float(variable.get("initial"), 0.0))
    if kind == "curvature":
        radius = finite_float(surface.get("radius"), math.nan)
        if not math.isfinite(radius) or abs(radius) <= 1.0e-14:
            return 0.0
        return 1.0 / radius
    if kind == "material_nd":
        material = surface.get("material_after", {}) if isinstance(surface, dict) else {}
        nd, _vd = server.material_nd_vd(material)
        return finite_float(nd, finite_float(variable.get("initial"), 1.5))
    if kind == "material_vd":
        material = surface.get("material_after", {}) if isinstance(surface, dict) else {}
        _nd, vd = server.material_nd_vd(material)
        return finite_float(vd, finite_float(variable.get("initial"), 50.0))
    if kind == "conic":
        return finite_float(surface.get("conic"), finite_float(variable.get("initial"), 0.0))
    if kind == "asphere_coeff":
        coeffs = surface.get("asphere_coefficients", []) or []
        term = int(finite_float(variable.get("term"), 0))
        if 0 <= term < len(coeffs):
            return finite_float(coeffs[term], finite_float(variable.get("initial"), 0.0))
    return finite_float(variable.get("initial"), 0.0)


def make_variable_from_template(system: dict[str, Any], template: dict[str, Any]) -> tuple[dict[str, Any], bool]:
    item = dict(template)
    value = surface_current_value(system, item)
    item["initial"] = value
    lower = finite_float(item.get("lower"), math.nan)
    upper = finite_float(item.get("upper"), math.nan)
    if not math.isfinite(lower) or not math.isfinite(upper) or lower >= upper:
        lower, upper = server.variable_default_bounds({"system": system, "variables": []}, item)
        item["lower"] = lower
        item["upper"] = upper
    if str(item.get("kind", "")).lower() == "asphere_coeff":
        server.rebase_asphere_edge_sag_variable(
            system,
            [item],
            item,
            current_value=value,
        )
        lower, upper = server.variable_bounds(item)
    return item, value < lower - 1.0e-12 or value > upper + 1.0e-12


def is_material_surface(surface: dict[str, Any]) -> bool:
    material = surface.get("material_after", {}) if isinstance(surface, dict) else {}
    nd, _vd = server.material_nd_vd(material)
    return finite_float(nd, 1.0) > 1.0005


def material_search_enabled(system: dict[str, Any], surface_index: int) -> bool:
    """Honor per-surface material locks for continuous and snap stages.

    ``glass_search.enabled`` controls the GUI/discrete glass-choice feature.  It
    is intentionally independent from explicit continuous ``material_nd`` /
    ``material_vd`` variables, so only a matching per-surface config can lock a
    surface out of the material funnel.
    """
    glass_search = system.get("glass_search")
    if not isinstance(glass_search, dict):
        return True

    def enabled_value(value: Any, default: bool = True) -> bool:
        if value is None:
            return default
        if isinstance(value, str):
            return value.strip().lower() not in {"0", "false", "no", "off"}
        return bool(value)

    surface_config = server.glass_surface_config(glass_search, surface_index)
    if "enabled" not in surface_config:
        return True
    return enabled_value(surface_config.get("enabled"))


def material_variable_surface_indices(variables: list[dict[str, Any]]) -> set[int]:
    indices: set[int] = set()
    for variable in variables:
        if str(variable.get("kind") or "").lower() not in {
            "material_nd",
            "material_vd",
            "glass_choice",
        }:
            continue
        try:
            indices.add(int(variable.get("index", -1)))
        except (TypeError, ValueError):
            continue
    return {index for index in indices if index >= 0}


def apply_material_variable_search_contract(
    system: dict[str, Any],
    variables: list[dict[str, Any]],
) -> dict[str, Any]:
    """Make the explicit material-variable set authoritative for the funnel.

    GUI surface rows may retain default ``enabled: false`` records after an
    actual glass is converted to continuous nd/Vd variables.  Those stale
    records must not override an explicit optimization variable.  Conversely,
    a refractive surface without a material/glass-choice variable must remain
    fixed even when it has no per-surface GUI record.
    """
    out = json.loads(json.dumps(system))
    surfaces = list(out.get("surfaces") or [])
    requested = material_variable_surface_indices(variables)
    active = {
        index
        for index in requested
        if 0 <= index < len(surfaces)
        and isinstance(surfaces[index], dict)
        and is_material_surface(surfaces[index])
    }
    glass_search = out.setdefault("glass_search", {})
    if not isinstance(glass_search, dict):
        glass_search = {}
        out["glass_search"] = glass_search
    default_libraries = (
        glass_search.get("default_libraries")
        or out.get("glass_libraries")
        or [out.get("glass_library", "SCHOTT")]
    )
    glass_search["default_libraries"] = (
        server.split_glass_libraries(default_libraries) or ["SCHOTT"]
    )
    glass_search["enabled"] = bool(active)
    refractive_indices = {
        index
        for index, surface in enumerate(surfaces)
        if isinstance(surface, dict)
        and str(surface.get("kind", "")).lower() not in {"object", "image"}
        and is_material_surface(surface)
    }
    for index in sorted(refractive_indices | active):
        surface_config = server.ensure_glass_surface_config(glass_search, index)
        surface_config["enabled"] = index in active
        libraries = server.split_glass_libraries(
            surface_config.get("libraries") or glass_search["default_libraries"]
        )
        surface_config["libraries"] = libraries or list(
            glass_search["default_libraries"]
        )
    out["material_variable_search_contract"] = {
        "source": "explicit_material_variables",
        "surface_indices": sorted(active),
        "requested_surface_indices": sorted(requested),
    }
    return out


def audit_material_catalog_contract(system: dict[str, Any]) -> dict[str, Any]:
    glass_search = system.get("glass_search", {})
    if not isinstance(glass_search, dict):
        glass_search = {}
    grouped_indices: dict[tuple[str, ...], list[int]] = {}
    for index, surface in enumerate(system.get("surfaces", []) or []):
        if (
            not isinstance(surface, dict)
            or not is_material_surface(surface)
            or not material_search_enabled(system, index)
        ):
            continue
        libraries = tuple(
            name.upper()
            for name in server.glass_libraries_for_surface(system, glass_search, index)
        )
        grouped_indices.setdefault(libraries, []).append(index)

    rows: list[dict[str, Any]] = []
    required_wavelengths = server.system_wavelengths_um(system)
    for libraries, surface_indices in sorted(grouped_indices.items()):
        candidates = [
            glass
            for glass in server.list_glasses(
                "",
                ",".join(libraries),
                limit=1000000,
                required_wavelengths_um=required_wavelengths,
            )
            if glass.get("nd") is not None and glass.get("vd") is not None
        ]
        if not candidates:
            raise RuntimeError(
                f"No usable glass point cloud for surfaces {surface_indices} "
                f"from declared libraries {list(libraries)}"
            )
        actual_libraries = sorted(
            {
                str(glass.get("library") or "").strip().upper()
                for glass in candidates
                if str(glass.get("library") or "").strip()
            }
        )
        all_libraries_allowed = "ALL" in libraries
        missing_libraries = (
            [] if all_libraries_allowed else sorted(set(libraries) - set(actual_libraries))
        )
        unexpected_libraries = (
            [] if all_libraries_allowed else sorted(set(actual_libraries) - set(libraries))
        )
        if missing_libraries or unexpected_libraries:
            raise RuntimeError(
                f"Glass point-cloud catalog mismatch for surfaces {surface_indices}: "
                f"declared={list(libraries)} actual={actual_libraries}"
            )
        signature_rows = sorted(
            (
                str(glass.get("library") or "").upper(),
                str(glass.get("name") or ""),
                finite_float(glass.get("nd"), math.nan),
                finite_float(glass.get("vd"), math.nan),
            )
            for glass in candidates
        )
        signature = hashlib.sha256(
            json.dumps(signature_rows, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        ).hexdigest()
        bounds = server.glass_material_bounds_for_libraries(
            list(libraries),
            required_wavelengths,
        )
        rows.append(
            {
                "surface_indices": surface_indices,
                "libraries": list(libraries),
                "actual_libraries": actual_libraries,
                "point_count": len(candidates),
                "required_wavelengths_um": required_wavelengths,
                "wavelength_coverage_verified": True,
                "point_cloud_sha256": signature,
                "bounds": {
                    "nd_min": bounds[0],
                    "nd_max": bounds[1],
                    "vd_min": bounds[2],
                    "vd_max": bounds[3],
                },
            }
        )
    if not rows:
        raise RuntimeError("Material funnel has no material surfaces to constrain")
    return {"ok": True, "catalogs": rows}


def strict_material_bound_violations(
    system: dict[str, Any],
    locked_indices: set[int],
) -> list[dict[str, Any]]:
    surfaces = system.get("surfaces", []) or []
    glass_search = system.get("glass_search", {}) if isinstance(system.get("glass_search"), dict) else {}
    violations: list[dict[str, Any]] = []
    for index, surface in enumerate(surfaces):
        if (
            index in locked_indices
            or not isinstance(surface, dict)
            or not is_material_surface(surface)
            or not material_search_enabled(system, index)
        ):
            continue
        material = surface.get("material_after", {}) if isinstance(surface.get("material_after"), dict) else {}
        if str(material.get("mode", "")).lower() == "glass":
            continue
        surface_config = server.glass_surface_config(glass_search, index)
        if not bool(surface_config.get("strict_libraries", False)):
            continue
        nd, vd = server.material_nd_vd(material)
        nd = finite_float(nd, math.nan)
        vd = finite_float(vd, math.nan)
        nd_min, nd_max, vd_min, vd_max = server.glass_material_bounds_for_surface(system, index)
        clamped_nd = min(nd_max, max(nd_min, nd)) if math.isfinite(nd) else nd_min
        clamped_vd = min(vd_max, max(vd_min, vd)) if math.isfinite(vd) else vd_min
        if abs(clamped_nd - nd) <= 1.0e-12 and abs(clamped_vd - vd) <= 1.0e-12:
            continue
        violations.append(
            {
                "surface_index": index,
                "libraries": server.glass_libraries_for_surface(system, glass_search, index),
                "nd": nd,
                "vd": vd,
                "nd_lower": nd_min,
                "nd_upper": nd_max,
                "vd_lower": vd_min,
                "vd_upper": vd_max,
                "clamped_nd": clamped_nd,
                "clamped_vd": clamped_vd,
                "nd_violation": abs(clamped_nd - nd),
                "vd_violation": abs(clamped_vd - vd),
            }
        )
    return violations


def project_strict_material_bounds(
    system: dict[str, Any],
    locked_indices: set[int],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    out = json.loads(json.dumps(system))
    violations = strict_material_bound_violations(out, locked_indices)
    surfaces = out.get("surfaces", []) or []
    for violation in violations:
        index = int(violation["surface_index"])
        material = surfaces[index].get("material_after", {})
        material["nd"] = float(violation["clamped_nd"])
        material["vd"] = float(violation["clamped_vd"])
    return out, violations


def variable_bound_violation_count(variables: list[dict[str, Any]]) -> int:
    return sum(
        1
        for variable in variables
        if finite_float(variable.get("initial"), 0.0) < finite_float(variable.get("lower"), -math.inf) - 1.0e-12
        or finite_float(variable.get("initial"), 0.0) > finite_float(variable.get("upper"), math.inf) + 1.0e-12
    )


def add_material_variables(
    system: dict[str, Any],
    variables: list[dict[str, Any]],
    source_templates: dict[tuple[str, int, int], dict[str, Any]],
    locked_indices: set[int],
) -> None:
    existing = {variable_key(variable) for variable in variables}
    surfaces = system.get("surfaces", []) or []
    for index, surface in enumerate(surfaces):
        if not isinstance(surface, dict) or index in locked_indices:
            continue
        if str(surface.get("kind", "")).lower() in {"object", "image"}:
            continue
        if not is_material_surface(surface) or not material_search_enabled(system, index):
            continue
        material = surface.get("material_after", {}) if isinstance(surface.get("material_after"), dict) else {}
        nd, vd = server.material_nd_vd(material)
        nd = finite_float(nd, 1.5)
        vd = finite_float(vd, 50.0)
        nd_min, nd_max, vd_min, vd_max = server.glass_material_bounds_for_surface(system, index)
        for kind, initial, lower, upper, label in (
            ("material_nd", nd, nd_min, nd_max, "nd"),
            ("material_vd", vd, vd_min, vd_max, "Vd"),
        ):
            key = (kind, index, 0)
            if key in existing:
                continue
            template = source_templates.get(key, {})
            glass_search = system.get("glass_search", {}) if isinstance(system.get("glass_search"), dict) else {}
            strict_libraries = bool(server.glass_surface_config(glass_search, index).get("strict_libraries", False))
            item = {
                "kind": kind,
                "index": index,
                "term": 0,
                "lower": lower if strict_libraries else finite_float(template.get("lower"), lower),
                "upper": upper if strict_libraries else finite_float(template.get("upper"), upper),
                "initial": initial,
                "role": template.get("role") or f"{label} @ S{index}",
            }
            if item["lower"] >= item["upper"]:
                item["lower"], item["upper"] = lower, upper
            variables.append(item)
            existing.add(key)


def build_stage_variables(
    system: dict[str, Any],
    source_variables: list[dict[str, Any]],
    variable_mode: str,
    locked_indices: set[int],
) -> tuple[list[dict[str, Any]], int]:
    mode = str(variable_mode or "geometry_material").lower()
    source_templates = {variable_key(variable): variable for variable in source_variables}
    keep_material = "material" in mode
    keep_geometry = mode in {"geometry", "geometry_material", "all", "remaining_material"}
    variables: list[dict[str, Any]] = []
    out_of_bounds = 0
    geometry_kinds = {"curvature", "radius", "thickness", "conic", "asphere_coeff"}
    for variable in source_variables:
        kind = str(variable.get("kind") or "")
        if kind in {"material_nd", "material_vd", "glass_choice"}:
            continue
        if keep_geometry and kind in geometry_kinds:
            item, bad = make_variable_from_template(system, variable)
            variables.append(item)
            out_of_bounds += int(bad)
    if keep_material:
        add_material_variables(system, variables, source_templates, locked_indices)
    return variables, out_of_bounds


def build_stage_merit(
    base_merit: dict[str, Any],
    system: dict[str, Any],
    variables: list[dict[str, Any]],
    stage: dict[str, Any],
) -> dict[str, Any]:
    out = dict(base_merit)
    has_material_vars = any(variable.get("kind") in {"material_nd", "material_vd"} for variable in variables)
    if not has_material_vars or not bool(stage.get("glass_proximity", True)):
        out.pop("glass_proximity", None)
        return out
    out = server.merit_with_glass_proximity(
        base_merit,
        system,
        variables,
        {},
        weight=max(0.0, finite_float(stage.get("glass_weight"), 0.5)),
        tolerance=max(0.0, finite_float(stage.get("glass_tolerance"), 1.5)),
        candidate_count=max(1, int(finite_float(stage.get("glass_candidate_count"), 256))),
        smooth_temperature=max(0.0, finite_float(stage.get("glass_smooth_temperature"), 0.25)),
        nd_scale=max(1.0e-12, finite_float(stage.get("nd_scale"), 0.01)),
        vd_scale=max(1.0e-12, finite_float(stage.get("vd_scale"), 5.0)),
        vd_metric=str(stage.get("vd_metric", server.GLASS_DISTANCE_VD_METRIC)),
        vd_relative_scale=max(
            1.0e-12,
            finite_float(stage.get("vd_relative_scale"), server.GLASS_DISTANCE_VD_RELATIVE_SCALE),
        ),
    )
    if not out.get("glass_proximity"):
        out.pop("glass_proximity", None)
    return out


def material_distance_candidates(system: dict[str, Any], unlocked_indices: set[int], stage: dict[str, Any]) -> list[dict[str, Any]]:
    surfaces = system.get("surfaces", []) or []
    nd_scale = max(1.0e-12, finite_float(stage.get("nd_scale"), 0.01))
    vd_scale = max(1.0e-12, finite_float(stage.get("vd_scale"), 5.0))
    vd_metric = str(stage.get("vd_metric", server.GLASS_DISTANCE_VD_METRIC))
    vd_relative_scale = max(
        1.0e-12,
        finite_float(stage.get("vd_relative_scale"), server.GLASS_DISTANCE_VD_RELATIVE_SCALE),
    )
    out: list[dict[str, Any]] = []
    glass_search = system.get("glass_search", {}) if isinstance(system.get("glass_search"), dict) else {}
    for index in sorted(unlocked_indices):
        if not (0 <= index < len(surfaces)):
            continue
        if not material_search_enabled(system, index):
            continue
        material = surfaces[index].get("material_after", {}) if isinstance(surfaces[index], dict) else {}
        nd, vd = server.material_nd_vd(material)
        if nd is None or vd is None:
            continue
        libraries = server.glass_libraries_for_surface(system, glass_search, index)
        nearest = server.nearest_glasses(
            ",".join(libraries),
            nd,
            vd,
            limit=1,
            nd_scale=nd_scale,
            vd_scale=vd_scale,
            vd_metric=vd_metric,
            vd_relative_scale=vd_relative_scale,
            required_wavelengths_um=server.system_wavelengths_um(system),
        )
        if not nearest:
            continue
        glass = nearest[0]
        out.append(
            {
                "index": index,
                "glass": glass,
                "distance": server.glass_nd_vd_distance(
                    nd,
                    vd,
                    glass,
                    nd_scale=nd_scale,
                    vd_scale=vd_scale,
                    vd_metric=vd_metric,
                    vd_relative_scale=vd_relative_scale,
                ),
            }
        )
    out.sort(key=lambda item: finite_float(item.get("distance"), math.inf))
    return out


def snap_nearest_subset(
    system: dict[str, Any],
    locked_indices: set[int],
    stage: dict[str, Any],
) -> tuple[dict[str, Any], set[int], list[dict[str, Any]]]:
    out = json.loads(json.dumps(system))
    surfaces = out.get("surfaces", []) or []
    material_indices = {
        index
        for index, surface in enumerate(surfaces)
        if isinstance(surface, dict)
        and str(surface.get("kind", "")).lower() not in {"object", "image"}
        and is_material_surface(surface)
        and material_search_enabled(out, index)
        and index not in locked_indices
    }
    candidates = material_distance_candidates(out, material_indices, stage)
    if not candidates:
        return out, set(locked_indices), []
    if "snap_count" in stage:
        count = max(0, int(finite_float(stage.get("snap_count"), 0)))
    else:
        fraction = max(0.0, min(1.0, finite_float(stage.get("snap_fraction"), 1.0)))
        count = int(math.ceil(len(candidates) * fraction))
    count = min(len(candidates), max(0, count))
    selected = candidates[:count]
    new_locked = set(locked_indices)
    for item in selected:
        index = int(item["index"])
        glass = item["glass"]
        old_material = surfaces[index].get("material_after", {}) if isinstance(surfaces[index], dict) else {}
        surfaces[index]["material_after"] = {
            "mode": "glass",
            "name": glass.get("name", old_material.get("name", "")),
            "reference": glass.get("reference", glass.get("library", "")),
            "nd": finite_float(glass.get("nd"), finite_float(old_material.get("nd"), 1.5)),
            "vd": finite_float(glass.get("vd"), finite_float(old_material.get("vd"), 50.0)),
            "filename": glass.get("filename", old_material.get("filename", "")),
            "source_path": glass.get("source_path", old_material.get("source_path", "")),
            "distance": item.get("distance"),
        }
        new_locked.add(index)
    out["surfaces"] = surfaces
    return out, new_locked, selected


def build_command(
    reference_command: list[str],
    *,
    exe: Path,
    system_path: Path,
    merit_path: Path,
    variables_path: Path,
    out_dir: Path,
    stage: dict[str, Any],
    args: argparse.Namespace,
) -> list[str]:
    command = sanitize_reference_command(reference_command, exe)
    for flag in ("--clear-aperture-update", "--sync-stop-aperture"):
        if flag not in command:
            command.append(flag)
    sampling = requested_trace_sampling(args, stage)
    if sampling["num_rays"] > 0:
        set_arg(command, "--rays", str(sampling["num_rays"]))
    if sampling["pupil_arms"] > 0:
        set_arg(command, "--arms", str(sampling["pupil_arms"]))
    if sampling["pupil_distribution"]:
        set_arg(command, "--distribution", sampling["pupil_distribution"])
    set_arg(command, "--system", str(system_path))
    # The reference command may have been produced by an earlier workflow
    # stage.  The concrete candidate system is authoritative for the optical
    # contract, so synchronize the command before running this stage.
    aiming = system_aiming_policy(system_path)
    set_arg(command, "--aiming", aiming["mode"])
    set_arg(command, "--aiming-iter", str(aiming["max_iter"]))
    set_arg(command, "--aiming-continuation-steps", str(aiming["continuation_steps"]))
    set_arg(command, "--aiming-stop-radius-source", aiming["stop_radius_source"])
    command[:] = [
        token for token in command
        if token not in {"--aiming-best-effort", "--aiming-require-convergence"}
    ]
    command.append(
        "--aiming-require-convergence"
        if aiming["require_convergence"]
        else "--aiming-best-effort"
    )
    set_arg(command, "--merit", str(merit_path))
    set_arg(command, "--variables", str(variables_path))
    set_arg(command, "--out-dir", str(out_dir))
    set_arg(command, "--local-iter", str(int(finite_float(stage.get("local_iter"), args.local_iter))))
    set_arg(command, "--local-lambda", f"{finite_float(stage.get('local_lambda'), args.local_lambda):.17g}")
    set_arg(command, "--local-fd", f"{finite_float(stage.get('local_fd'), args.local_fd):.17g}")
    set_arg(command, "--local-tol", f"{finite_float(stage.get('local_tol'), args.local_tol):.17g}")
    set_arg(command, "--local-max-reject", str(int(finite_float(stage.get("local_max_reject"), args.local_max_reject))))
    set_arg(
        command,
        "--local-plateau-window",
        str(
            int(
                finite_float(
                    stage.get("local_plateau_window"),
                    getattr(args, "local_plateau_window", 50),
                )
            )
        ),
    )
    set_arg(
        command,
        "--local-plateau-min-iter",
        str(
            int(
                finite_float(
                    stage.get("local_plateau_min_iterations"),
                    getattr(args, "local_plateau_min_iterations", 80),
                )
            )
        ),
    )
    set_arg(
        command,
        "--local-plateau-min-abs-improve",
        f"{finite_float(stage.get('local_plateau_min_abs_improvement'), getattr(args, 'local_plateau_min_abs_improvement', 0.02)):.17g}",
    )
    set_arg(
        command,
        "--local-plateau-min-rel-improve",
        f"{finite_float(stage.get('local_plateau_min_rel_improvement'), getattr(args, 'local_plateau_min_rel_improvement', 0.01)):.17g}",
    )
    set_arg(command, "--local-method", str(stage.get("local_method", args.local_method)))
    set_arg(command, "--local-bounds-mode", str(stage.get("local_bounds_mode", args.local_bounds_mode)))
    set_arg(command, "--local-residual-mode", str(stage.get("local_residual_mode", args.local_residual_mode)))
    set_arg(command, "--optimizer-internal-scaling", str(stage.get("optimizer_internal_scaling", args.optimizer_internal_scaling)))
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
    min_trace, min_field = minimum_trace_coverages(args)
    command.append("--local-allow-near-trace-infeasible")
    set_arg(
        command,
        "--local-trace-coverage-epsilon",
        f"{1.0 - min(min_trace, min_field):.17g}",
    )
    # Each post-DE DLS stage reconstructs or updates one concrete basin
    # system. Rebuild normalized asphere bounds from that system's current
    # clear apertures instead of inheriting the shared DE scout reference.
    set_arg(command, "--asphere-reference-policy", "current")
    return command


def run_command(command: list[str], out_dir: Path, timeout: float, dry_run: bool, child_cpu_workers: int = 0) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    write_json(out_dir / "command.json", command)
    (out_dir / "command.txt").write_text(" ".join(command), encoding="utf-8")
    if dry_run:
        return {"returncode": None, "timed_out": False, "elapsed_seconds": 0.0, "dry_run": True}
    env = None
    if child_cpu_workers > 0:
        env = os.environ.copy()
        env["OPTILAND_CPU_WORKERS"] = str(child_cpu_workers)
    started = time.perf_counter()
    try:
        proc = subprocess.run(
            command,
            cwd=REPO_ROOT,
            env=env,
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            check=False,
            timeout=timeout if timeout > 0 else None,
        )
        stdout = proc.stdout
        stderr = proc.stderr
        returncode = proc.returncode
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        returncode = -999
        timed_out = True
    (out_dir / "stdout.log").write_text(stdout, encoding="utf-8", errors="replace")
    (out_dir / "stderr.log").write_text(stderr, encoding="utf-8", errors="replace")
    return {
        "returncode": returncode,
        "timed_out": timed_out,
        "elapsed_seconds": max(0.0, time.perf_counter() - started),
    }


def stage_execution_fingerprint(
    *,
    command: list[str],
    system: dict[str, Any],
    variables: list[dict[str, Any]],
    merit: dict[str, Any],
) -> str:
    executable = Path(command[0]).resolve()
    stat = executable.stat()
    payload = {
        "contract": MATERIAL_FUNNEL_STAGE_CONTRACT,
        "command": command,
        "system": system,
        "variables": variables,
        "merit": merit,
        "executable": {
            "path": str(executable),
            "size": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
        },
    }
    return hashlib.sha256(
        json.dumps(
            payload,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    ).hexdigest()


def score_digest(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    score = read_json(path)
    keys = [
        "merit",
        "uncapped_merit",
        "trace_ok",
        "trace_coverage",
        "min_field_coverage",
        "min_chief_progress",
        "mean_spot",
        "max_spot",
        "max_distortion",
        "max_distortion_percent",
        "efl",
        "ttl",
        "bfl",
        "min_edge",
        "constraint_group_error",
        "first_order_group_error",
        "image_group_error",
        "lateral_color",
    ]
    return {key: score.get(key) for key in keys if key in score}


def parse_polish_curve(path: Path) -> list[tuple[int, float]]:
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8", errors="replace")
    return [(int(match.group(1)), float(match.group(2))) for match in POLISH_RE.finditer(text)]


def tail_improvement(curve: list[tuple[int, float]], final: float, window: int) -> float:
    if not curve or not math.isfinite(final):
        return math.nan
    last_iter = curve[-1][0]
    cutoff = max(0, last_iter - window)
    before = next((value for iteration, value in reversed(curve) if iteration <= cutoff), curve[0][1])
    return before - final


def material_counts(system_path: Path) -> dict[str, int]:
    if not system_path.exists():
        return {}
    system = read_json(system_path)
    counts: dict[str, int] = {}
    for surface in system.get("surfaces", []) or []:
        material = surface.get("material_after", {}) if isinstance(surface, dict) else {}
        if not is_material_surface(surface):
            continue
        mode = str(material.get("mode", "")).lower() or "unknown"
        counts[mode] = counts.get(mode, 0) + 1
    return counts


def basin_key(basin: dict[str, Any], fallback: Any) -> str:
    return str(
        basin.get("structural_topology_key")
        or basin.get("topology_key")
        or basin.get("semantic_key")
        or fallback
    )


def archive_merit(basin: dict[str, Any]) -> float:
    score = basin.get("score") if isinstance(basin.get("score"), dict) else {}
    return finite_float(score.get("merit"), finite_float(basin.get("merit"), basin.get("objective")))


def stage_row(
    *,
    stage_name: str,
    position: int,
    candidate: dict[str, Any],
    out_dir: Path,
    result: dict[str, Any],
    variables: list[dict[str, Any]],
    out_of_bounds: int,
    locked_indices: set[int],
    snap_records: list[dict[str, Any]],
    min_trace_coverage: float,
    min_field_coverage: float,
) -> dict[str, Any]:
    initial = score_digest(out_dir / "initial_score.json")
    final = score_digest(out_dir / "score.json")
    initial_merit = finite_float(initial.get("merit"), math.nan)
    final_merit = finite_float(final.get("merit"), math.nan)
    source_merit = finite_float(candidate.get("final_merit"), finite_float(candidate.get("archive_merit"), math.nan))
    curve = parse_polish_curve(out_dir / "stdout.log")
    total = initial_merit - final_merit if math.isfinite(initial_merit) and math.isfinite(final_merit) else math.nan
    source_delta = source_merit - final_merit if math.isfinite(source_merit) and math.isfinite(final_merit) else math.nan
    material_var_count = sum(1 for variable in variables if variable.get("kind") in {"material_nd", "material_vd"})
    counts = material_counts(out_dir / "system.json")
    if not counts:
        counts = material_counts(out_dir / "input_system.json")
    final_trace_acceptable = (
        int(finite_float(result.get("returncode"), -1)) == 0
        and not bool(result.get("timed_out", False))
        and bool(final.get("trace_ok", False))
        and finite_float(final.get("trace_coverage"), 0.0) >= min_trace_coverage
        and finite_float(final.get("min_field_coverage"), 0.0) >= min_field_coverage
        and finite_float(final.get("min_chief_progress"), 0.0)
        >= 1.0 - 1.0e-12
        and finite_float(final.get("min_edge"), -math.inf) >= 0.0
        and 0.0 < finite_float(final.get("merit"), math.inf) < 1.0e12
    )
    return {
        "stage": stage_name,
        "stage_position": position,
        "basin_rank": candidate.get("basin_rank"),
        "basin_id": candidate.get("basin_id"),
        "topology_key": candidate.get("topology_key"),
        "source_dir": candidate.get("system_dir"),
        "out_dir": str(out_dir),
        "archive_merit": candidate.get("archive_merit"),
        "source_merit": source_merit,
        "initial_merit": initial_merit,
        "final_merit": final_merit,
        "source_delta": source_delta,
        "total_improvement": total,
        "tail100_improvement": tail_improvement(curve, final_merit, 100),
        "tail50_improvement": tail_improvement(curve, final_merit, 50),
        "tail25_improvement": tail_improvement(curve, final_merit, 25),
        "last_polish_iter": curve[-1][0] if curve else 0,
        "final_trace_ok": final.get("trace_ok"),
        "final_trace_acceptable": final_trace_acceptable,
        "final_trace_acceptance_threshold": min(min_trace_coverage, min_field_coverage),
        "final_trace_coverage": final.get("trace_coverage"),
        "final_min_field_coverage": final.get("min_field_coverage"),
        "final_min_chief_progress": final.get("min_chief_progress"),
        "final_mean_spot": final.get("mean_spot"),
        "final_max_spot": final.get("max_spot"),
        "final_max_distortion": final.get("max_distortion"),
        "final_efl": final.get("efl"),
        "final_ttl": final.get("ttl"),
        "final_bfl": final.get("bfl"),
        "final_min_edge": final.get("min_edge"),
        "final_constraint_group_error": final.get("constraint_group_error"),
        "final_first_order_group_error": final.get("first_order_group_error"),
        "final_image_group_error": final.get("image_group_error"),
        "variable_count": len(variables),
        "material_variable_count": material_var_count,
        "locked_material_count": len(locked_indices),
        "system_abbe_count": counts.get("abbe", 0),
        "system_glass_count": counts.get("glass", 0),
        "snap_count": len(snap_records),
        "initial_out_of_bounds": out_of_bounds,
        "returncode": result.get("returncode"),
        "timed_out": result.get("timed_out"),
        "elapsed_seconds": result.get("elapsed_seconds"),
    }


def selection_key(row: dict[str, Any], mode: str) -> tuple[float, float]:
    final = finite_float(row.get("final_merit"), math.inf)
    trace_flag = row.get("final_trace_acceptable", row.get("final_trace_ok", ""))
    if str(trace_flag).lower() not in {"true", "1"}:
        final += 1.0e9
    total = max(0.0, finite_float(row.get("total_improvement"), 0.0))
    tail = max(0.0, finite_float(row.get("tail50_improvement"), 0.0))
    if mode == "potential":
        score = -(math.log1p(total) + 2.0 * math.log1p(tail))
        return score, final
    if mode == "merit":
        return final, -tail
    hybrid = final / (1.0 + 0.15 * math.log1p(total) + 0.30 * math.log1p(tail))
    return hybrid, final


def select_candidates(
    candidates: list[dict[str, Any]],
    rows: list[dict[str, Any]],
    *,
    keep: int,
    mode: str,
    max_per_topology: int,
    preserve_lineages: bool = False,
) -> list[dict[str, Any]]:
    # The production two-layer route has already fixed and topology-filtered
    # its entry lineages.  In that mode, stage ranking is diagnostic only.
    if preserve_lineages:
        return list(candidates)
    by_dir = {str(candidate.get("system_dir")): candidate for candidate in candidates}
    ordered_rows = sorted(rows, key=lambda row: selection_key(row, mode))
    selected: list[dict[str, Any]] = []
    topology_counts: dict[str, int] = {}
    for row in ordered_rows:
        candidate = by_dir.get(str(row.get("out_dir")))
        if candidate is None:
            continue
        topology = str(candidate.get("topology_key") or candidate.get("basin_rank"))
        if max_per_topology > 0 and topology_counts.get(topology, 0) >= max_per_topology:
            continue
        selected.append(candidate)
        topology_counts[topology] = topology_counts.get(topology, 0) + 1
        if keep > 0 and len(selected) >= keep:
            break
    return selected


def preserved_lineage_audit(
    before: list[dict[str, Any]],
    after: list[dict[str, Any]],
) -> dict[str, Any]:
    """Verify that a failed stage attempt retained every input lineage."""

    expected = [int(finite_float(row.get("basin_rank"), -1)) for row in before]
    actual = [int(finite_float(row.get("basin_rank"), -1)) for row in after]
    expected_keys = [fixed_lineage_identity(row) for row in before]
    actual_keys = [fixed_lineage_identity(row) for row in after]
    missing_systems: list[str] = []
    for row in after:
        system_path_text = str(row.get("system_path") or "").strip()
        system_dir_text = str(row.get("system_dir") or "").strip()
        system_path = (
            Path(system_path_text)
            if system_path_text
            else Path(system_dir_text) / "system.json"
        )
        if not system_path.is_file():
            missing_systems.append(str(system_path))
    return {
        "contract": "fixed_material_lineage_preservation_v1",
        "expected_count": len(before),
        "actual_count": len(after),
        "expected_ranks": expected,
        "actual_ranks": actual,
        "expected_lineages": expected_keys,
        "actual_lineages": actual_keys,
        "missing_systems": missing_systems,
        "complete": (
            len(before) == len(after)
            and expected_keys == actual_keys
            and not missing_systems
        ),
    }


def fixed_lineage_identity(row: dict[str, Any]) -> str:
    """Return a stable identity even when multiple entries share one rank."""

    rank = int(finite_float(row.get("basin_rank"), -1))
    basin_id = str(row.get("basin_id") or "")
    topology = str(row.get("topology_key") or "")
    return f"rank={rank}|basin={basin_id}|topology={topology}"


def order_fixed_lineages_like_input(
    before: list[dict[str, Any]],
    after: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Restore fixed-lineage order after concurrent stage completion."""

    positions_by_lineage: dict[str, list[int]] = {}
    for position, row in enumerate(before):
        positions_by_lineage.setdefault(fixed_lineage_identity(row), []).append(
            position
        )
    occurrences: dict[str, int] = {}
    decorated: list[tuple[int, int, dict[str, Any]]] = []
    for completion_position, row in enumerate(after):
        identity = fixed_lineage_identity(row)
        occurrence = occurrences.get(identity, 0)
        occurrences[identity] = occurrence + 1
        positions = positions_by_lineage.get(identity, [])
        input_position = (
            positions[occurrence] if occurrence < len(positions) else len(before)
        )
        decorated.append((input_position, completion_position, row))
    decorated.sort(key=lambda item: (item[0], item[1]))
    return [row for _, _, row in decorated]


def is_snap_stage(stage: dict[str, Any]) -> bool:
    return str(stage.get("operation", "dls")).strip().lower() == "snap_nearest"


def latest_candidate_row(candidate: dict[str, Any]) -> dict[str, Any]:
    history = candidate.get("history")
    if isinstance(history, list) and history:
        latest = history[-1]
        if isinstance(latest, dict):
            return latest
    return {}


def write_continuous_presnap_checkpoint(
    out_dir: Path,
    candidates: list[dict[str, Any]],
    *,
    checkpoint_reason: str,
    selection_mode: str,
) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    for position, candidate in enumerate(candidates, start=1):
        latest = latest_candidate_row(candidate)
        source_dir = Path(str(candidate.get("system_dir") or latest.get("out_dir") or ""))
        row = dict(latest)
        row.update(
            {
                "continuous_presnap_index": position,
                "checkpoint_reason": checkpoint_reason,
                "basin_rank": candidate.get("basin_rank", latest.get("basin_rank")),
                "basin_id": candidate.get("basin_id", latest.get("basin_id")),
                "topology_key": candidate.get("topology_key", latest.get("topology_key")),
                "source_dir": str(source_dir),
                "system_path": str(candidate.get("system_path") or source_dir / "system.json"),
                "continuous_final_merit": candidate.get("final_merit", latest.get("final_merit")),
                "continuous_final_mean_spot": candidate.get("final_mean_spot", latest.get("final_mean_spot")),
                "continuous_stage": latest.get("stage", ""),
                # system_dir is the last valid lineage state.  The latest
                # history row may describe a failed attempt that was rolled back.
                "continuous_stage_out_dir": str(source_dir),
            }
        )
        rows.append(row)
    ranked = sorted(rows, key=lambda row: selection_key(row, selection_mode))
    write_csv(out_dir / "continuous_presnap_candidates.csv", rows)
    write_csv(out_dir / "continuous_presnap_ranked.csv", ranked)
    summary = {
        "checkpoint_reason": checkpoint_reason,
        "count": len(rows),
        "rows": rows,
        "ranked": ranked,
    }
    write_json(out_dir / "continuous_presnap_candidates.json", summary)
    return summary


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


def load_stages(args: argparse.Namespace) -> list[dict[str, Any]]:
    if args.stages_json:
        text = args.stages_json.read_text(encoding="utf-8")
        stages = json.loads(text)
    else:
        stages = DEFAULT_STAGES
    if not isinstance(stages, list) or not stages:
        raise ValueError("stage configuration must be a non-empty list")
    out: list[dict[str, Any]] = []
    max_iter = max_local_iter_per_restart(args)
    for index, stage in enumerate(stages, start=1):
        if not isinstance(stage, dict):
            raise ValueError(f"stage {index} is not an object")
        item = dict(stage)
        item.setdefault("name", f"stage_{index:02d}")
        item.setdefault("variable_mode", "geometry_material")
        item.setdefault("local_bounds_mode", "safety_material_strict")
        out.extend(split_stage_restarts(item, args.local_iter, max_iter))
    return out


def split_stage_restarts(stage: dict[str, Any], fallback_iter: int, max_iter: int) -> list[dict[str, Any]]:
    requested = max(1, int(finite_float(stage.get("local_iter"), fallback_iter)))
    if max_iter <= 0 or requested <= max_iter:
        item = dict(stage)
        item["local_iter"] = requested
        return [item]

    count = int(math.ceil(requested / max_iter))
    base = requested // count
    remainder = requested % count
    out: list[dict[str, Any]] = []
    original_name = str(stage.get("name", "stage"))
    for pass_index in range(1, count + 1):
        item = dict(stage)
        item["name"] = f"{original_name}_r{pass_index:02d}"
        item["local_iter"] = base + (1 if pass_index <= remainder else 0)
        item["restart_of"] = original_name
        item["restart_pass"] = pass_index
        item["restart_count"] = count
        item["requested_local_iter"] = requested
        item["max_local_iter_per_restart"] = max_iter
        if pass_index > 1:
            item.pop("operation", None)
        out.append(item)
    return out


def max_local_iter_per_restart(args: argparse.Namespace) -> int:
    return int(finite_float(getattr(args, "max_local_iter_per_restart", DEFAULT_MAX_LOCAL_ITER_PER_RESTART), DEFAULT_MAX_LOCAL_ITER_PER_RESTART))


def restart_early_stop_decision(
    stage: dict[str, Any],
    rows: list[dict[str, Any]],
    args: argparse.Namespace,
) -> dict[str, Any]:
    """Decide whether later passes of this exact stage objective are low-yield.

    A decision never crosses ``restart_of`` groups, so convergence at one glass
    tolerance cannot suppress the first pass at the next stricter tolerance.
    """
    group = str(stage.get("restart_of") or "")
    restart_pass = int(finite_float(stage.get("restart_pass"), 0))
    restart_count = int(finite_float(stage.get("restart_count"), 0))
    enabled = bool(getattr(args, "restart_early_stop", True))
    decision: dict[str, Any] = {
        "enabled": enabled,
        "restart_of": group,
        "restart_pass": restart_pass,
        "restart_count": restart_count,
        "stop_remaining_restarts": False,
        "reason": "not_a_split_restart",
        "rows": [],
    }
    if not enabled:
        decision["reason"] = "disabled"
        return decision
    if not group or restart_pass <= 0 or restart_pass >= restart_count:
        return decision
    if not rows:
        decision["reason"] = "no_rows"
        return decision

    max_iterations = max(
        1,
        int(finite_float(getattr(args, "restart_early_stop_max_iterations", 20), 20)),
    )
    min_abs_improvement = max(
        0.0,
        finite_float(getattr(args, "restart_early_stop_min_abs_improvement", 0.002), 0.002),
    )
    min_rel_improvement = max(
        0.0,
        finite_float(getattr(args, "restart_early_stop_min_rel_improvement", 0.001), 0.001),
    )
    decision.update(
        {
            "max_iterations": max_iterations,
            "min_abs_improvement": min_abs_improvement,
            "min_rel_improvement": min_rel_improvement,
        }
    )

    all_low_yield = True
    for row in rows:
        initial_merit = finite_float(row.get("initial_merit"), math.nan)
        improvement = finite_float(row.get("total_improvement"), math.nan)
        last_iter = int(finite_float(row.get("last_polish_iter"), 0))
        returncode = int(finite_float(row.get("returncode"), -1))
        timed_out = str(row.get("timed_out", False)).strip().lower() in {
            "1",
            "true",
            "yes",
            "on",
        }
        trace_acceptable = str(row.get("final_trace_acceptable", False)).strip().lower() in {
            "1",
            "true",
            "yes",
            "on",
        }
        valid = (
            returncode == 0
            and not timed_out
            and trace_acceptable
            and last_iter > 0
            and math.isfinite(initial_merit)
            and math.isfinite(improvement)
            and improvement >= 0.0
        )
        threshold = max(
            min_abs_improvement,
            min_rel_improvement * max(1.0, abs(initial_merit)),
        )
        low_yield = valid and last_iter <= max_iterations and improvement <= threshold
        decision["rows"].append(
            {
                "basin_rank": row.get("basin_rank"),
                "valid": valid,
                "last_polish_iter": last_iter,
                "total_improvement": improvement,
                "improvement_threshold": threshold,
                "low_yield": low_yield,
            }
        )
        all_low_yield = all_low_yield and low_yield

    decision["stop_remaining_restarts"] = all_low_yield
    decision["reason"] = (
        "all_candidates_fast_low_yield"
        if all_low_yield
        else "candidate_progress_or_invalid_result_requires_next_restart"
    )
    return decision


def parse_rank_from_dir(path: Path, fallback: int) -> int:
    match = re.search(r"rank[_-]?(\d+)", path.name, flags=re.IGNORECASE)
    if match:
        return int(match.group(1))
    match = re.search(r"basin[_-]?(\d+)", path.name, flags=re.IGNORECASE)
    if match:
        return int(match.group(1))
    return fallback


def candidate_run_dirs(args: argparse.Namespace) -> list[Path]:
    paths: list[Path] = []
    if args.candidate_parent_dir:
        parent = args.candidate_parent_dir.resolve()
        paths.extend(
            path
            for path in sorted(parent.iterdir(), key=lambda item: item.name)
            if path.is_dir() and (path / "score.json").exists()
        )
    if args.candidate_run_dirs:
        for part in args.candidate_run_dirs.replace(";", ",").split(","):
            text = part.strip()
            if text:
                paths.append(Path(text).resolve())
    seen: set[str] = set()
    out: list[Path] = []
    for path in paths:
        key = str(path.resolve()).lower()
        if key in seen:
            continue
        seen.add(key)
        out.append(path)
    return out


def initial_candidates_from_run_dirs(args: argparse.Namespace) -> list[dict[str, Any]]:
    dirs = candidate_run_dirs(args)
    if args.ranks:
        wanted = {int(part.strip()) for part in args.ranks.replace(";", ",").split(",") if part.strip()}
        dirs = [path for position, path in enumerate(dirs, start=1) if parse_rank_from_dir(path, position) in wanted]
    if args.max_basins > 0:
        dirs = dirs[: args.max_basins]
    out: list[dict[str, Any]] = []
    for position, path in enumerate(dirs, start=1):
        system_path = first_existing(
            [
                path / "system.json",
                path / "checkpoint_system.json",
                path / "input_system.json",
            ]
        )
        meta_path = path / "basin_meta.json"
        basin = read_json(meta_path) if meta_path.exists() else {}
        score = read_json(path / "score.json") if (path / "score.json").exists() else {}
        rank = int(finite_float(basin.get("rank"), parse_rank_from_dir(path, position)))
        out.append(
            {
                "basin_rank": rank,
                "basin_id": basin.get("id"),
                "archive_merit": finite_float(score.get("merit"), archive_merit(basin)),
                "topology_key": basin_key(basin, rank),
                "system_dir": str(path),
                "system_path": str(system_path),
                "locked_material_indices": [],
                "history": [],
            }
        )
    if not out:
        raise ValueError("no candidate run directories were found")
    return out


def initial_candidates(args: argparse.Namespace, reference_system: dict[str, Any] | None) -> tuple[list[dict[str, Any]], dict[str, Any], list[dict[str, Any]], dict[str, Any]]:
    basin_run_dir = args.basin_run_dir.resolve()
    system_path, variables_path, merit_path = context_paths(basin_run_dir)
    base_system = read_json(system_path)
    source_variables = variable_list(variables_path)
    base_merit = read_json(args.merit_template.resolve() if args.merit_template else merit_path)
    if args.candidate_parent_dir or args.candidate_run_dirs:
        candidates = initial_candidates_from_run_dirs(args)
        return candidates, base_system, source_variables, base_merit
    archive = read_json(basin_run_dir / "basin_archive.json")
    basins = list(archive.get("basins", []))
    if args.ranks:
        wanted = {int(part.strip()) for part in args.ranks.replace(";", ",").split(",") if part.strip()}
        basins = [
            basin
            for position, basin in enumerate(basins, start=1)
            if int(finite_float(basin.get("rank"), position)) in wanted
        ]
    if args.max_basins > 0:
        basins = basins[: args.max_basins]
    out: list[dict[str, Any]] = []
    stage0 = args.out_dir.resolve() / "stage_00_materialized_basins"
    for position, basin in enumerate(basins, start=1):
        rank = int(finite_float(basin.get("rank"), position))
        item_dir = stage0 / f"basin_{position:03d}_rank_{rank:04d}"
        system = materialize_basin_system(
            base_system,
            source_variables,
            basin,
            reference_system=reference_system,
            min_field_count=args.min_field_count,
        )
        write_json(item_dir / "system.json", system)
        write_json(item_dir / "basin_meta.json", basin)
        out.append(
            {
                "basin_rank": rank,
                "basin_id": basin.get("id"),
                "archive_merit": archive_merit(basin),
                "topology_key": basin_key(basin, rank),
                "system_dir": str(item_dir),
                "system_path": str(item_dir / "system.json"),
                "locked_material_indices": [],
                "history": [],
            }
        )
    return out, base_system, source_variables, base_merit


def inherit_material_search_policy(system: dict[str, Any], policy_system: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(policy_system, dict):
        return system
    out = json.loads(json.dumps(system))
    for key in (
        "glass_library",
        "glass_libraries",
        "glass_search",
        "post_asphere_plastic_glass_search",
    ):
        if key in policy_system:
            out[key] = json.loads(json.dumps(policy_system[key]))
    return out


def run_stage(
    *,
    stage_index: int,
    stage: dict[str, Any],
    candidates: list[dict[str, Any]],
    source_variables: list[dict[str, Any]],
    base_merit: dict[str, Any],
    material_policy_system: dict[str, Any],
    reference_command: list[str],
    exe: Path,
    args: argparse.Namespace,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    stage_name = str(stage["name"])
    stage_dir = args.out_dir.resolve() / f"stage_{stage_index:02d}_{stage_name}"
    rows: list[dict[str, Any]] = []
    next_candidates: list[dict[str, Any]] = []
    workers = min(len(candidates), positive_int(getattr(args, "workers", 1), 1)) if candidates else 1
    child_cpu_workers = resolved_child_cpu_workers(
        workers,
        nonnegative_int(getattr(args, "child_cpu_workers", 0), 0),
    )

    def process_candidate(position: int, candidate: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
        rank = int(finite_float(candidate.get("basin_rank"), position))
        out_dir = stage_dir / f"cand_{position:03d}_rank_{rank:04d}"
        system = read_json(Path(str(candidate["system_path"])))
        system = inherit_material_search_policy(system, material_policy_system)
        field_hy_fractions = requested_field_hy_fractions(args, stage)
        if field_hy_fractions:
            system = with_field_hy_fractions(system, field_hy_fractions)
        elif args.min_field_count > 0:
            system = server.densify_system_fields_for_asphere(system, args.min_field_count)
        locked_indices = {int(value) for value in candidate.get("locked_material_indices", [])}
        system, material_bound_projections = project_strict_material_bounds(system, locked_indices)
        snap_records: list[dict[str, Any]] = []
        if str(stage.get("operation", "dls")).lower() == "snap_nearest":
            system, locked_indices, snap_records = snap_nearest_subset(system, locked_indices, stage)
        variables, out_of_bounds = build_stage_variables(
            system,
            source_variables,
            str(stage.get("variable_mode", "geometry_material")),
            locked_indices,
        )
        out_of_bounds = variable_bound_violation_count(variables)
        merit = apply_trace_sampling_to_merit(
            build_stage_merit(base_merit, system, variables, stage),
            args,
            stage,
        )
        input_system_path = out_dir / "input_system.json"
        variables_path = out_dir / "variables.json"
        merit_path = out_dir / "merit.json"
        write_json(input_system_path, system)
        write_json(variables_path, {"variables": variables})
        write_json(merit_path, merit)
        write_json(
            out_dir / "stage_meta.json",
            {
                "stage": stage,
                "candidate": candidate,
                "locked_material_indices": sorted(locked_indices),
                "snap_records": snap_records,
                "material_bound_projections": material_bound_projections,
                "field_count": len(system.get("fields", []) or []),
                "field_hy": [
                    finite_float(field.get("hy"), 0.0)
                    for field in system.get("fields", []) or []
                    if isinstance(field, dict)
                ],
                "trace_sampling": {
                    "num_rays": merit.get("num_rays"),
                    "pupil_arms": merit.get("pupil_arms"),
                    "pupil_distribution": merit.get("pupil_distribution"),
                },
            },
        )
        command = build_command(
            reference_command,
            exe=exe,
            system_path=input_system_path,
            merit_path=merit_path,
            variables_path=variables_path,
            out_dir=out_dir,
            stage=stage,
            args=args,
        )
        execution_fingerprint = stage_execution_fingerprint(
            command=command,
            system=system,
            variables=variables,
            merit=merit,
        )
        execution_contract_path = out_dir / "stage_execution_contract.json"
        execution_contract = (
            read_json(execution_contract_path)
            if execution_contract_path.is_file()
            else {}
        )
        reusable = (
            not args.force_rerun
            and not args.dry_run
            and execution_contract.get("contract")
            == MATERIAL_FUNNEL_STAGE_CONTRACT
            and execution_contract.get("fingerprint") == execution_fingerprint
            and (out_dir / "score.json").is_file()
            and (out_dir / "system.json").is_file()
        )
        print(f"[stage {stage_index} {stage_name}] {position}/{len(candidates)} basin rank {rank}", flush=True)
        if not reusable:
            result = run_command(command, out_dir, args.timeout, args.dry_run, child_cpu_workers)
        else:
            result = {
                "returncode": int(finite_float(execution_contract.get("returncode"), -1)),
                "timed_out": bool(execution_contract.get("timed_out", False)),
                "elapsed_seconds": 0.0,
                "reused": True,
            }
        write_json(
            execution_contract_path,
            {
                "contract": MATERIAL_FUNNEL_STAGE_CONTRACT,
                "fingerprint": execution_fingerprint,
                "returncode": result.get("returncode"),
                "timed_out": result.get("timed_out"),
                "reused": reusable,
            },
        )
        row = stage_row(
            stage_name=stage_name,
            position=position,
            candidate=candidate,
            out_dir=out_dir,
            result=result,
            variables=variables,
            out_of_bounds=out_of_bounds,
            locked_indices=locked_indices,
            snap_records=snap_records,
            min_trace_coverage=minimum_trace_coverages(args)[0],
            min_field_coverage=minimum_trace_coverages(args)[1],
        )
        output_system_path = out_dir / "system.json"
        output_system = read_json(output_system_path) if output_system_path.exists() else system
        output_material_violations = strict_material_bound_violations(output_system, locked_indices)
        row["material_bound_projection_count"] = len(material_bound_projections)
        row["material_bound_output_violation_count"] = len(output_material_violations)
        write_json(
            out_dir / "material_bounds_audit.json",
            {
                "local_bounds_mode": str(stage.get("local_bounds_mode", args.local_bounds_mode)),
                "input_projections": material_bound_projections,
                "output_violations": output_material_violations,
            },
        )
        next_candidate = dict(candidate)
        preserve_lineages = bool(
            getattr(args, "preserve_candidate_lineages", False)
        )
        trace_acceptable = str(
            row.get("final_trace_acceptable", row.get("final_trace_ok", False))
        ).strip().lower() in {"true", "1"}
        stage_advanced = (
            int(finite_float(result.get("returncode"), -1)) == 0
            and not bool(result.get("timed_out", False))
            and output_system_path.exists()
            and trace_acceptable
        )
        row["lineage_state_advanced"] = stage_advanced
        if preserve_lineages and not stage_advanced:
            # A failed stage attempt must not delete the fixed lineage or
            # replace its last valid state with a partial output.
            row["lineage_fallback_out_dir"] = candidate.get("system_dir")
            next_candidate["history"] = list(candidate.get("history", [])) + [row]
            next_candidate["lineage_last_stage_advanced"] = False
            return row, next_candidate
        if args.dry_run or not output_system_path.exists():
            output_system_path = out_dir / "input_system.json"
        next_candidate.update(
            {
                "system_dir": str(out_dir),
                "system_path": str(output_system_path),
                "final_merit": row.get("final_merit"),
                "final_mean_spot": row.get("final_mean_spot"),
                "locked_material_indices": sorted(locked_indices),
                "history": list(candidate.get("history", [])) + [row],
                "lineage_last_stage_advanced": stage_advanced,
            }
        )
        return row, next_candidate

    if workers <= 1 or len(candidates) <= 1:
        for position, candidate in enumerate(candidates, start=1):
            row, next_candidate = process_candidate(position, candidate)
            rows.append(row)
            next_candidates.append(next_candidate)
    else:
        print(
            f"[stage {stage_index} {stage_name}] workers={workers}, "
            f"child_cpu_workers={child_cpu_workers}",
            flush=True,
        )
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [
                pool.submit(process_candidate, position, candidate)
                for position, candidate in enumerate(candidates, start=1)
            ]
            for future in concurrent.futures.as_completed(futures):
                row, next_candidate = future.result()
                rows.append(row)
                next_candidates.append(next_candidate)
        rows.sort(key=lambda row: int(finite_float(row.get("stage_position"), 0)))
        if bool(getattr(args, "preserve_candidate_lineages", False)):
            # Failed attempts retain their prior system_dir, so sorting them
            # through this stage's out_dir map moves every rollback to the
            # front and corrupts fixed lineage order.
            next_candidates = order_fixed_lineages_like_input(
                candidates,
                next_candidates,
            )
        else:
            position_by_dir = {
                str(row.get("out_dir")): int(
                    finite_float(row.get("stage_position"), 0)
                )
                for row in rows
            }
            next_candidates.sort(
                key=lambda item: position_by_dir.get(
                    str(item.get("system_dir")), 0
                )
            )
    write_csv(stage_dir / "stage_summary.csv", rows)
    ranked = sorted(rows, key=lambda row: selection_key(row, args.selection_mode))
    write_csv(stage_dir / "stage_ranked.csv", ranked)
    write_json(stage_dir / "stage_summary.json", {"stage": stage, "rows": rows, "ranked": ranked})
    invalid_rows = [
        row
        for row in rows
        if str(row.get("final_trace_acceptable", False)).strip().lower()
        not in {"true", "1"}
    ]
    if bool(getattr(args, "preserve_candidate_lineages", False)) and invalid_rows:
        write_json(
            stage_dir / "invalid_stage_results.json",
            {
                "contract": MATERIAL_FUNNEL_STAGE_CONTRACT,
                "stage": stage,
                "invalid_count": len(invalid_rows),
                "rows": invalid_rows,
            },
        )
        # These are failed *attempts*, not deleted candidates.  Each lineage
        # was already rolled back to its previous valid system above; later
        # repair stages must receive the complete fixed pool.
    keep = int(finite_float(stage.get("keep"), args.keep))
    selected = select_candidates(
        next_candidates,
        rows,
        keep=keep,
        mode=args.selection_mode,
        max_per_topology=args.max_per_topology,
        preserve_lineages=bool(
            getattr(args, "preserve_candidate_lineages", False)
        ),
    )
    if bool(getattr(args, "preserve_candidate_lineages", False)):
        lineage_audit = preserved_lineage_audit(candidates, selected)
        write_json(stage_dir / "lineage_preservation_audit.json", lineage_audit)
        if not bool(lineage_audit.get("complete", False)):
            raise RuntimeError(
                f"material funnel stage {stage_name} lost fixed candidate "
                f"lineages; audit={stage_dir / 'lineage_preservation_audit.json'}"
            )
    write_json(stage_dir / "selected_candidates.json", selected)
    return selected, rows


def run_once(args: argparse.Namespace) -> Path:
    started = time.perf_counter()
    args.out_dir = args.out_dir.resolve()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    stages = load_stages(args)
    reference_system = read_json(args.reference_run / "input_system.json") if args.reference_run and (args.reference_run / "input_system.json").exists() else None
    reference_command, run_meta, exe = load_reference_context(args, args.basin_run_dir.resolve())
    candidates, base_system, source_variables, base_merit = initial_candidates(args, reference_system)
    base_system = apply_material_variable_search_contract(base_system, source_variables)
    material_catalog_contract = audit_material_catalog_contract(base_system)
    write_json(args.out_dir / "material_catalog_contract.json", material_catalog_contract)
    write_json(
        args.out_dir / "funnel_config.json",
        {
            "basin_run_dir": str(args.basin_run_dir.resolve()),
            "reference_run": str(args.reference_run.resolve()) if args.reference_run else "",
            "candidate_parent_dir": str(args.candidate_parent_dir.resolve()) if args.candidate_parent_dir else "",
            "candidate_run_dirs": args.candidate_run_dirs,
            "out_dir": str(args.out_dir),
            "exe": str(exe),
            "source_variable_count": len(source_variables),
            "material_catalog_contract": material_catalog_contract,
            "min_field_count": args.min_field_count,
            "field_hy_fractions_override": str(
                getattr(args, "field_hy_fractions", "") or ""
            ),
            "stop_before_first_snap": bool(
                getattr(args, "stop_before_first_snap", False)
            ),
            "selection_mode": args.selection_mode,
            "max_per_topology": args.max_per_topology,
            "preserve_candidate_lineages": bool(
                getattr(args, "preserve_candidate_lineages", False)
            ),
            "stages": stages,
            "max_local_iter_per_restart": max_local_iter_per_restart(args),
            "local_plateau": {
                "window": int(getattr(args, "local_plateau_window", 50)),
                "min_iterations": int(getattr(args, "local_plateau_min_iterations", 80)),
                "min_abs_improvement": float(
                    getattr(args, "local_plateau_min_abs_improvement", 0.02)
                ),
                "min_rel_improvement": float(
                    getattr(args, "local_plateau_min_rel_improvement", 0.01)
                ),
            },
            "restart_early_stop": {
                "enabled": bool(getattr(args, "restart_early_stop", True)),
                "max_iterations": int(
                    getattr(args, "restart_early_stop_max_iterations", 20)
                ),
                "min_abs_improvement": float(
                    getattr(args, "restart_early_stop_min_abs_improvement", 0.002)
                ),
                "min_rel_improvement": float(
                    getattr(args, "restart_early_stop_min_rel_improvement", 0.001)
                ),
            },
            "workers": positive_int(getattr(args, "workers", 1), 1),
            "child_cpu_workers": resolved_child_cpu_workers(
                positive_int(getattr(args, "workers", 1), 1),
                nonnegative_int(getattr(args, "child_cpu_workers", 0), 0),
            ),
            "reference_command": reference_command,
            "reference_run_meta": run_meta,
            "trace_sampling_override": requested_trace_sampling(args),
            "force_global_sampling": bool(
                getattr(args, "force_global_sampling", False)
            ),
            "minimum_trace_coverage": minimum_trace_coverages(args)[0],
            "minimum_field_coverage": minimum_trace_coverages(args)[1],
        },
    )
    all_rows: list[dict[str, Any]] = []
    current = candidates
    continuous_presnap_summary: dict[str, Any] | None = None
    stopped_restart_groups: dict[str, dict[str, Any]] = {}
    restart_early_stop_decisions: list[dict[str, Any]] = []
    skipped_restart_stages: list[dict[str, Any]] = []
    for index, stage in enumerate(stages, start=1):
        if not current:
            break
        restart_group = str(stage.get("restart_of") or "")
        if restart_group and restart_group in stopped_restart_groups:
            skipped = {
                "stage_index": index,
                "stage": str(stage.get("name") or ""),
                "restart_of": restart_group,
                "restart_pass": int(finite_float(stage.get("restart_pass"), 0)),
                "reason": "prior_same_objective_restart_was_fast_low_yield",
                "trigger": stopped_restart_groups[restart_group],
            }
            skipped_restart_stages.append(skipped)
            print(
                f"[stage {index} {stage.get('name', '')}] skipped: "
                f"same-objective restart group {restart_group!r} already converged",
                flush=True,
            )
            write_json(
                args.out_dir / "restart_early_stop.json",
                {
                    "decisions": restart_early_stop_decisions,
                    "skipped_stages": skipped_restart_stages,
                },
            )
            continue
        if continuous_presnap_summary is None and is_snap_stage(stage):
            continuous_presnap_summary = write_continuous_presnap_checkpoint(
                args.out_dir,
                current,
                checkpoint_reason=f"before_stage_{index:02d}_{stage.get('name', 'snap_nearest')}",
                selection_mode=args.selection_mode,
            )
            if bool(getattr(args, "stop_before_first_snap", False)):
                break
        current, rows = run_stage(
            stage_index=index,
            stage=stage,
            candidates=current,
            source_variables=source_variables,
            base_merit=base_merit,
            material_policy_system=base_system,
            reference_command=reference_command,
            exe=exe,
            args=args,
        )
        all_rows.extend(rows)
        restart_decision = restart_early_stop_decision(stage, rows, args)
        if str(stage.get("restart_of") or ""):
            restart_early_stop_decisions.append(restart_decision)
            if bool(restart_decision.get("stop_remaining_restarts")):
                stopped_restart_groups[str(stage["restart_of"])] = restart_decision
            write_json(
                args.out_dir / "restart_early_stop.json",
                {
                    "decisions": restart_early_stop_decisions,
                    "skipped_stages": skipped_restart_stages,
                },
            )
        write_csv(args.out_dir / "funnel_all_stage_rows.csv", all_rows)
        write_json(args.out_dir / "funnel_latest_candidates.json", current)
    if continuous_presnap_summary is None and current:
        continuous_presnap_summary = write_continuous_presnap_checkpoint(
            args.out_dir,
            current,
            checkpoint_reason="no_snap_stage_final_candidates",
            selection_mode=args.selection_mode,
        )
    final_rows = [candidate["history"][-1] for candidate in current if candidate.get("history")]
    write_csv(args.out_dir / "funnel_final_ranked.csv", sorted(final_rows, key=lambda row: selection_key(row, args.selection_mode)))
    write_json(
        args.out_dir / "funnel_summary.json",
        {
            "elapsed_seconds": max(0.0, time.perf_counter() - started),
            "all_rows": all_rows,
            "latest_candidates": current,
            "continuous_presnap": continuous_presnap_summary or {},
            "restart_early_stop_decisions": restart_early_stop_decisions,
            "skipped_restart_stages": skipped_restart_stages,
        },
    )
    return args.out_dir


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a staged post-DE basin DLS/material/glass refinement funnel.")
    parser.add_argument("--basin-run-dir", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--reference-run", type=Path, default=None)
    parser.add_argument("--merit-template", type=Path, default=None)
    parser.add_argument("--stages-json", type=Path, default=None)
    parser.add_argument("--candidate-parent-dir", type=Path, default=None)
    parser.add_argument("--candidate-run-dirs", default="")
    parser.add_argument("--ranks", default="")
    parser.add_argument("--max-basins", type=int, default=24)
    parser.add_argument("--keep", type=int, default=8)
    parser.add_argument("--max-per-topology", type=int, default=1)
    parser.add_argument(
        "--preserve-candidate-lineages",
        action=argparse.BooleanOptionalAction,
        default=False,
        help=(
            "Keep every input candidate through every stage; stage ranking and "
            "topology counts become diagnostics only."
        ),
    )
    parser.add_argument("--selection-mode", choices=["hybrid", "merit", "potential"], default="hybrid")
    parser.add_argument("--min-field-count", type=int, default=5)
    parser.add_argument(
        "--field-hy-fractions",
        default="",
        help=(
            "Optional exact meridional field grid for material DLS. "
            "Stage JSON field_hy_fractions values override this global grid."
        ),
    )
    parser.add_argument("--trace-rays", type=int, default=0)
    parser.add_argument("--trace-arms", type=int, default=0)
    parser.add_argument("--trace-distribution", default="")
    parser.add_argument(
        "--stop-before-first-snap",
        action="store_true",
        help="Stop after continuous-material DLS and emit the pre-snap checkpoint.",
    )
    parser.add_argument(
        "--min-trace-coverage",
        type=float,
        default=DE_DLS_TRACE_COVERAGE_BASELINE,
    )
    parser.add_argument(
        "--min-field-coverage",
        type=float,
        default=DE_DLS_TRACE_COVERAGE_BASELINE,
    )
    parser.add_argument("--local-iter", type=int, default=400)
    parser.add_argument("--max-local-iter-per-restart", type=int, default=DEFAULT_MAX_LOCAL_ITER_PER_RESTART)
    parser.add_argument("--local-lambda", type=float, default=0.01)
    parser.add_argument("--local-fd", type=float, default=1.0e-5)
    parser.add_argument("--local-tol", type=float, default=1.0e-5)
    parser.add_argument("--local-max-reject", type=int, default=40)
    parser.add_argument("--local-plateau-window", type=int, default=50)
    parser.add_argument("--local-plateau-min-iterations", type=int, default=80)
    parser.add_argument("--local-plateau-min-abs-improvement", type=float, default=0.02)
    parser.add_argument("--local-plateau-min-rel-improvement", type=float, default=0.01)
    parser.add_argument(
        "--restart-early-stop",
        action=argparse.BooleanOptionalAction,
        default=True,
        help=(
            "Skip only later split passes of the same stage objective when every "
            "candidate exits quickly with negligible improvement."
        ),
    )
    parser.add_argument("--restart-early-stop-max-iterations", type=int, default=20)
    parser.add_argument("--restart-early-stop-min-abs-improvement", type=float, default=0.002)
    parser.add_argument("--restart-early-stop-min-rel-improvement", type=float, default=0.001)
    parser.add_argument("--local-method", default="trf")
    parser.add_argument(
        "--local-bounds-mode",
        choices=["safety", "safety_material_strict", "variable"],
        default="safety_material_strict",
    )
    parser.add_argument("--local-residual-mode", default="expanded")
    parser.add_argument("--optimizer-internal-scaling", default="edge_sag_normalized")
    parser.add_argument("--timeout", type=float, default=0.0)
    parser.add_argument("--force-rerun", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--exe", type=Path, default=None)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--child-cpu-workers", type=int, default=0)
    args = parser.parse_args()
    out_dir = run_once(args)
    print(out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
