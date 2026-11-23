"""Stable material topology, merit/potential and Pareto ordering."""
from __future__ import annotations

import math
from typing import Any
from . import files as _module_files


def topology_key(row: dict[str, Any]) -> str:
    return str(
        row.get("structural_topology_key")
        or row.get("topology_key")
        or f"rank:{int(_module_files.finite_float(row.get('basin_rank'), -1))}"
    )


def material_topology_family_key(row: dict[str, Any]) -> str:
    """Collapse continuous bend-bin variants for material-entry diversity."""
    key = topology_key(row)
    parts = key.split("|")
    if any(part.startswith("pwr=") for part in parts) and any(
        part.startswith("shape=") for part in parts
    ):
        return "|".join(part for part in parts if not part.startswith("bend="))
    return key


def effective_key(row: dict[str, Any]) -> tuple[float, float, float]:
    return (
        _module_files.finite_float(row.get("effective_merit"), math.inf),
        _module_files.finite_float(row.get("effective_mean_spot"), math.inf),
        _module_files.finite_float(row.get("effective_constraint_group_error"), math.inf),
    )


def selection_reference_merit(row: dict[str, Any]) -> float:
    """Use the parent quality merit before strict pupil recalibration.

    Strict Set Vignetting can expose a large but readily repairable minimum-
    relative-illumination penalty.  Keep that penalty in the bridge validity
    and repair checks, but do not let it replace the merit used to prioritize
    distinct parent basins.
    """

    for key in (
        "pre_vignetting_calibration_merit",
        "sparse_final_merit",
        "source_pre_vignetting_merit",
    ):
        value = _module_files.finite_float(row.get(key), math.nan)
        if math.isfinite(value):
            return value
    return _module_files.finite_float(row.get("effective_merit"), math.inf)


def bridge_potential(row: dict[str, Any]) -> float:
    # S400 was calibrated as the checkpoint continuation-potential signal.
    # Keep it as a separate axis when the checkpoint route supplies it; legacy
    # sparse-DLS rows fall back to the historical geometry-improvement proxy.
    s400 = _module_files.finite_float(row.get("S400"), math.nan)
    if math.isfinite(s400):
        return s400
    geometry_total = max(
        0.0, _module_files.finite_float(row.get("geometry_total_improvement"), 0.0)
    )
    last_delta = max(0.0, _module_files.finite_float(row.get("delta_from_prior"), 0.0))
    sweep_delta = max(0.0, _module_files.finite_float(row.get("sweep_improvement"), 0.0))
    inherited = _module_files.finite_float(row.get("potential_score"), 0.0)
    if not math.isfinite(inherited):
        inherited = 0.0
    return (
        math.log1p(geometry_total)
        + 0.5 * math.log1p(last_delta)
        + 0.25 * math.log1p(sweep_delta)
        + 0.10 * max(0.0, inherited)
    )


def bridge_selection_key(
    row: dict[str, Any], mode: str
) -> tuple[float, float, float, float]:
    merit, _mean_spot, constraint = effective_key(row)
    reference_merit = selection_reference_merit(row)
    potential = bridge_potential(row)
    if mode == "potential":
        return -potential, reference_merit, merit, constraint
    if mode == "merit":
        return reference_merit, merit, -potential, constraint
    bounded_potential = max(-0.9, potential)
    return (
        reference_merit / (1.0 + 0.20 * bounded_potential),
        reference_merit,
        merit,
        constraint,
    )


def potential_lane_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Use S400 only for checkpoint roles when role provenance is available."""
    has_checkpoint_roles = any(str(row.get("run_role") or "") == "checkpoint" for row in rows)
    if not has_checkpoint_roles:
        return rows
    return [row for row in rows if str(row.get("run_role") or "") == "checkpoint"]


def quality_potential_pareto_order(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Deterministic non-dominated ordering for lower merit / higher potential."""
    if not rows:
        return []
    quality_order = sorted(rows, key=lambda row: bridge_selection_key(row, "merit"))
    potential_order = sorted(rows, key=lambda row: bridge_selection_key(row, "potential"))
    quality_rank = {id(row): rank for rank, row in enumerate(quality_order, 1)}
    potential_rank = {id(row): rank for rank, row in enumerate(potential_order, 1)}
    remaining = list(rows)
    ordered: list[dict[str, Any]] = []
    while remaining:
        front: list[dict[str, Any]] = []
        for row in remaining:
            merit = selection_reference_merit(row)
            potential = bridge_potential(row)
            dominated = False
            for other in remaining:
                if other is row:
                    continue
                other_merit = selection_reference_merit(other)
                other_potential = bridge_potential(other)
                if (
                    other_merit <= merit
                    and other_potential >= potential
                    and (other_merit < merit or other_potential > potential)
                ):
                    dominated = True
                    break
            if not dominated:
                front.append(row)
        front.sort(
            key=lambda row: (
                quality_rank[id(row)] + potential_rank[id(row)],
                quality_rank[id(row)],
                potential_rank[id(row)],
            )
        )
        ordered.extend(front)
        front_ids = {id(row) for row in front}
        remaining = [row for row in remaining if id(row) not in front_ids]
    return ordered
