"""Physical field, aiming, vignetting and trace-admission contracts."""
from __future__ import annotations

import argparse
import copy
import math
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from . import files as _module_files
from . import policy as _module_policy


def continuous_material_variable_contract(
    variables_doc: dict[str, Any] | list[dict[str, Any]],
) -> dict[str, Any]:
    variables = (
        list(variables_doc.get("variables") or [])
        if isinstance(variables_doc, dict)
        else list(variables_doc or [])
    )
    material_variables = [
        variable
        for variable in variables
        if str(variable.get("kind") or "") in _module_policy.CONTINUOUS_MATERIAL_KINDS
    ]
    keys: list[str] = []
    surfaces_by_kind: dict[str, set[int]] = {
        kind: set() for kind in _module_policy.CONTINUOUS_MATERIAL_KINDS
    }
    optimizable = 0
    for variable in material_variables:
        kind = str(variable.get("kind") or "")
        try:
            index = int(variable.get("index", -1))
        except (TypeError, ValueError):
            index = -1
        surfaces_by_kind[kind].add(index)
        keys.append(f"{kind}:{index}:{int(variable.get('term', 0) or 0)}")
        lower = _module_files.finite_float(variable.get("lower"))
        upper = _module_files.finite_float(variable.get("upper"))
        if math.isfinite(lower) and math.isfinite(upper) and upper > lower:
            optimizable += 1
    paired_surfaces = surfaces_by_kind["material_nd"] & surfaces_by_kind["material_vd"]
    all_material_surfaces = surfaces_by_kind["material_nd"] | surfaces_by_kind["material_vd"]
    return {
        "mode": "continuous",
        "material_variable_count": len(material_variables),
        "material_nd_count": sum(
            str(variable.get("kind") or "") == "material_nd"
            for variable in material_variables
        ),
        "material_vd_count": sum(
            str(variable.get("kind") or "") == "material_vd"
            for variable in material_variables
        ),
        "optimizable_material_variable_count": optimizable,
        "paired_material_surface_count": len(paired_surfaces),
        "unpaired_material_surface_indices": sorted(all_material_surfaces - paired_surfaces),
        "variable_keys": sorted(keys),
        "optimization_enabled": bool(material_variables)
        and optimizable == len(material_variables)
        and not (all_material_surfaces - paired_surfaces),
    }


def assert_continuous_material_variables_preserved(
    source_doc: dict[str, Any] | list[dict[str, Any]],
    prepared_doc: dict[str, Any] | list[dict[str, Any]],
    *,
    stage: str,
) -> None:
    source = continuous_material_variable_contract(source_doc)
    prepared = continuous_material_variable_contract(prepared_doc)
    missing = sorted(set(source["variable_keys"]) - set(prepared["variable_keys"]))
    if missing:
        raise ValueError(
            f"{stage} removed continuous material DE variables: {', '.join(missing)}"
        )


def field_nodes(args: argparse.Namespace) -> tuple[float, ...]:
    nodes = tuple(_module_files.parse_float_list(getattr(args, "material_bridge_field_hy_fractions", "")))
    return nodes or (0.0, 0.25, 0.5, 0.75, 1.0)


def common_fields(nodes: tuple[float, ...]) -> list[dict[str, float]]:
    return [
        {
            "hx": 0.0,
            "hy": float(node),
            "fvcx": 0.0,
            "fvcy": 0.0,
            "fvdx": 0.0,
            "fvdy": 0.0,
            "vignette_x": 0.0,
            "vignette_y": 0.0,
        }
        for node in nodes
    ]


def fields_on_exact_nodes(
    system: dict[str, Any],
    nodes: tuple[float, ...],
    *,
    allow_zero_for_missing: bool,
) -> list[dict[str, Any]]:
    """Preserve vignetting only on an already established field contract.

    Hy interpolation is intentionally forbidden here.  A vignetting-enabled
    sparse-to-dense transition must first pass through the aperture-realized
    bridge; later stages with the same nodes inherit those solved fields.
    """

    source_fields = [
        copy.deepcopy(field)
        for field in system.get("fields", []) or []
        if isinstance(field, dict)
    ]
    selected: list[dict[str, Any]] = []
    for node in nodes:
        match = next(
            (
                field
                for field in source_fields
                if abs(
                    math.hypot(
                        _module_files.finite_float(field.get("hx"), 0.0),
                        _module_files.finite_float(field.get("hy"), 0.0),
                    )
                    - node
                )
                <= 1.0e-6
            ),
            None,
        )
        if match is None:
            if allow_zero_for_missing:
                return common_fields(nodes)
            raise ValueError(
                "vignetting-enabled field contract contains a new node "
                f"Hy={node:.12g} without an aperture-realized bridge"
            )
        match["hx"] = 0.0
        match["hy"] = float(node)
        selected.append(match)
    return selected


def field_contract_matches(system: dict[str, Any], nodes: tuple[float, ...]) -> bool:
    try:
        fields_on_exact_nodes(system, nodes, allow_zero_for_missing=False)
    except ValueError:
        return False
    return True


def minimum_relative_illumination(merit: dict[str, Any]) -> float:
    return max(0.0, min(1.0, _module_files.finite_float(
        merit.get("minimum_relative_illumination"), 0.0
    )))


def vignetting_bridge_missing_pupil_samples(experiment_dir: Path) -> bool:
    return any(
        "no traceable pupil samples were found" in path.read_text(
            encoding="utf-8",
            errors="replace",
        )
        for path in experiment_dir.glob("set_vignetting/stderr.log")
        if path.is_file()
    )


def system_aiming_mode(system: dict[str, Any]) -> str:
    ray_aiming = system.get("ray_aiming")
    if isinstance(ray_aiming, dict):
        mode = str(ray_aiming.get("mode") or "").strip()
        if mode:
            return mode
    return "paraxial"


def system_stop_radius_source(system: dict[str, Any]) -> str:
    """Return the serialized aperture-radius contract for a candidate system.

    Bridge scoring is a contract transition, so its command-line policy must
    follow the system being scored.  In particular, do not silently replace a
    GUI-selected real-ray stop with the historical paraxial default.
    """
    ray_aiming = system.get("ray_aiming")
    raw = ray_aiming.get("stop_radius_source") if isinstance(ray_aiming, dict) else None
    return server.canonical_stop_radius_source(raw, "paraxial")


def system_aiming_policy(system: dict[str, Any]) -> dict[str, Any]:
    ray_aiming = system.get("ray_aiming") if isinstance(system, dict) else None
    if not isinstance(ray_aiming, dict):
        ray_aiming = {}
    return {
        "mode": server.canonical_aiming_mode(ray_aiming.get("mode"), "paraxial"),
        "max_iter": max(1, int(_module_files.finite_float(ray_aiming.get("max_iter"), 20))),
        "require_convergence": bool(ray_aiming.get("require_convergence", False)),
        "continuation_steps": max(1, int(_module_files.finite_float(ray_aiming.get("continuation_steps"), 8))),
        "stop_radius_source": system_stop_radius_source(system),
    }


def minimum_trace_coverage(args: argparse.Namespace) -> float:
    return min(
        1.0,
        max(
            0.0,
            _module_files.finite_float(
                getattr(
                    args,
                    "material_bridge_repair_min_trace_coverage",
                    _module_policy.DE_DLS_TRACE_COVERAGE_BASELINE,
                ),
                _module_policy.DE_DLS_TRACE_COVERAGE_BASELINE,
            ),
        ),
    )


def trace_acceptable(
    row: dict[str, Any],
    args: argparse.Namespace,
    *,
    prefix: str = "",
) -> bool:
    threshold = minimum_trace_coverage(args)
    consistency_key = f"{prefix}cpu_cuda_consistent"
    return (
        int(_module_files.finite_float(row.get(f"{prefix}returncode"), -1)) == 0
        and not _module_files.csv_bool(row.get(f"{prefix}timed_out"))
        and _module_files.csv_bool(row.get(f"{prefix}trace_ok"))
        and _module_files.finite_float(row.get(f"{prefix}trace_coverage"), 0.0) >= threshold
        and _module_files.finite_float(row.get(f"{prefix}min_field_coverage"), 0.0) >= threshold
        and server.vignetting_handoff_policy.chief_ray_usable(
            row,
            ok_key=f"{prefix}chief_ray_ok",
            failed_fields_key=f"{prefix}chief_ray_failed_fields",
            progress_key=f"{prefix}min_chief_progress",
        )
        and (
            consistency_key not in row
            or row.get(consistency_key) is None
            or _module_files.csv_bool(row.get(consistency_key))
        )
    )
