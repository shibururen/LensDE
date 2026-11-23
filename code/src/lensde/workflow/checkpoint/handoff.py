"""Strict final checkpoint ranking, diversity admission and material-funnel rows."""
from __future__ import annotations

import math
from typing import Any, Iterable
from lensde.workflow import selection as strict_topology
from . import files as _module_files


def material_row(result: dict[str, Any], rank: int) -> dict[str, Any]:
    reference = _module_files.finite(result.get("matched_rank1_merit"))
    midpoint = _module_files.finite(result.get("M_checkpoint"))
    final = _module_files.finite(result.get("M_final"))
    optimizer_midpoint = _module_files.finite(result.get("optimizer_M_checkpoint"))
    optimizer_final = _module_files.finite(result.get("optimizer_M_final"))
    s_score = _module_files.finite(result.get("S_final"), -math.inf)
    return {
        **result,
        "basin_rank": rank,
        "out_dir": result["run_dir"],
        "geometry_out_dir": result["run_dir"],
        "final_merit": final,
        "final_trace_ok": bool(result.get("trace_ok", False)),
        "final_trace_coverage": result.get("trace_coverage"),
        "final_min_field_coverage": result.get("min_field_coverage"),
        "final_min_chief_progress": result.get("min_chief_progress"),
        "final_chief_ray_ok": result.get("chief_ray_ok"),
        "final_chief_ray_failed_fields": result.get("chief_ray_failed_fields"),
        "final_zero_coverage_off_axis_fields": result.get(
            "zero_coverage_off_axis_fields"
        ),
        "final_mean_spot": result.get("mean_spot"),
        "final_max_spot": result.get("max_spot"),
        "final_distortion": result.get("max_distortion"),
        "final_min_edge": result.get("min_edge"),
        "final_constraint_group_error": result.get("constraint_group_error"),
        "final_first_order_group_error": result.get("first_order_group_error"),
        "final_image_group_error": result.get("image_group_error"),
        "structural_topology_key": (
            result.get("final_optical_coarse_key") or result.get("topology_key", "")
        ),
        "potential_score": max(0.0, s_score) if math.isfinite(s_score) else 0.0,
        "geometry_total_improvement": max(0.0, reference - final),
        "sweep_improvement": max(0.0, optimizer_midpoint - optimizer_final),
        "delta_from_prior": max(0.0, optimizer_midpoint - optimizer_final),
    }


def checkpoint_handoff_identity(row: dict[str, Any]) -> str:
    for key in ("run_id", "state_id", "out_dir", "run_dir"):
        value = str(row.get(key) or "")
        if value:
            return f"{key}:{value}"
    return f"object:{id(row)}"


def checkpoint_handoff_hierarchy(row: dict[str, Any]) -> tuple[str, ...]:
    return tuple(
        str(row.get(key) or "")
        for key in (
            "final_optical_coarse_key",
            "topology_key",
            "semantic_key",
            "descriptor_key",
        )
    )


def checkpoint_handoff_topology_identity(row: dict[str, Any]) -> str:
    """Use the post-DE structural family for the 7-way material handoff.

    The coarse optical key's ``bend=`` component is a continuous bending-bin
    fingerprint. Treating adjacent bend bins as different topologies allowed
    a checkpoint to occupy a second seat after converging to essentially the
    same rank-1 structure. Power-sign, surface-shape and gap-pattern changes
    remain topology changes; bend alone does not.
    """
    for key in (
        "final_optical_coarse_key",
        "structural_topology_key",
        "topology_key",
    ):
        value = str(row.get(key) or "").strip()
        if value:
            parts = value.split("|")
            if any(part.startswith("pwr=") for part in parts) and any(
                part.startswith("shape=") for part in parts
            ):
                return "|".join(
                    part for part in parts if not part.startswith("bend=")
                )
            return value
    return f"unknown:{checkpoint_handoff_identity(row)}"


def checkpoint_handoff_novelty(
    row: dict[str, Any], selected: list[dict[str, Any]]
) -> float:
    keys = checkpoint_handoff_hierarchy(row)
    populated = [(index, key) for index, key in enumerate(keys) if key]
    if not populated:
        return 0.0
    selected_keys = [checkpoint_handoff_hierarchy(item) for item in selected]
    return sum(
        all(other[index] != key for other in selected_keys)
        for index, key in populated
    ) / len(populated)


def select_checkpoint_handoff_rows(
    rows: list[dict[str, Any]],
    *,
    limit: int,
    merit_count: int = 7,
    potential_count: int = 0,
    diversity_count: int = 0,
    structure_distance_policy: str = strict_topology.LEGACY_DISTANCE_POLICY,
) -> list[dict[str, Any]]:
    """Select structurally distinct handoffs; a short pool remains a short pool."""
    selected: list[dict[str, Any]] = []
    seen: set[str] = set()

    def add(
        candidates: Iterable[dict[str, Any]],
        count: int,
        bucket: str,
    ) -> None:
        if count <= 0:
            return
        added = 0
        for row in candidates:
            identity = checkpoint_handoff_identity(row)
            if identity in seen:
                continue
            admission = strict_topology.assess_candidate(
                row,
                selected,
                topology_identity=checkpoint_handoff_topology_identity,
                distance_policy=structure_distance_policy,
            )
            if not admission.accepted:
                continue
            selected.append(
                strict_topology.annotate_admission(
                    {
                        **row,
                        "checkpoint_handoff_bucket": bucket,
                        "strict_topology_duplicate_fill": False,
                    },
                    admission,
                )
            )
            seen.add(identity)
            added += 1
            if (limit > 0 and len(selected) >= limit) or added >= max(0, count):
                break

    by_merit = sorted(
        rows,
        key=lambda row: (
            _module_files.finite(row.get("M_final"), math.inf),
            _module_files.finite(row.get("mean_spot"), math.inf),
            -_module_files.finite(row.get("S_final"), -math.inf),
        ),
    )
    if limit <= 0:
        add(by_merit, len(by_merit), "all_viable_unique_topology")
        return selected
    add(by_merit, min(max(0, merit_count), limit), "merit")
    pure_merit = (
        merit_count >= limit and potential_count == 0 and diversity_count == 0
    )
    if pure_merit:
        add(
            by_merit,
            max(0, limit - len(selected)),
            "merit_distinct_fill",
        )
        return selected[:limit]

    checkpoint_rows = [
        row for row in rows if str(row.get("run_role") or "") == "checkpoint"
    ]
    by_potential = sorted(
        checkpoint_rows or rows,
        key=lambda row: (
            -_module_files.finite(row.get("S_final"), -math.inf),
            _module_files.finite(row.get("M_final"), math.inf),
        ),
    )
    add(
        by_potential,
        min(max(0, potential_count), max(0, limit - len(selected))),
        "potential",
    )

    remaining = [
        row for row in rows if checkpoint_handoff_identity(row) not in seen
    ]
    remaining.sort(
        key=lambda row: (
            -checkpoint_handoff_novelty(row, selected),
            -_module_files.finite(row.get("S_final"), -math.inf),
            _module_files.finite(row.get("M_final"), math.inf),
        )
    )
    add(
        remaining,
        min(max(0, diversity_count), max(0, limit - len(selected))),
        "diversity",
    )

    # All selection modes preserve structure admission when filling a short pool.
    add(by_merit, max(0, limit - len(selected)), "merit_fill")
    return selected[:limit]
