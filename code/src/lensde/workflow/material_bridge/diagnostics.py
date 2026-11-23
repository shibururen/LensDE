"""Bridge classification, effective measurements and propagated row identities."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from . import contracts as _module_contracts
from . import files as _module_files


def metric_columns(prefix: str, score: dict[str, Any]) -> dict[str, Any]:
    return {
        f"{prefix}_aiming_mode": score.get("aiming_mode"),
        f"{prefix}_returncode": score.get("returncode"),
        f"{prefix}_timed_out": score.get("timed_out"),
        f"{prefix}_merit": score.get("merit"),
        f"{prefix}_trace_ok": score.get("trace_ok"),
        f"{prefix}_trace_coverage": score.get("trace_coverage"),
        f"{prefix}_min_field_coverage": score.get("min_field_coverage"),
        f"{prefix}_min_chief_progress": score.get("min_chief_progress"),
        f"{prefix}_chief_ray_ok": score.get("chief_ray_ok"),
        f"{prefix}_chief_ray_failed_fields": score.get("chief_ray_failed_fields"),
        f"{prefix}_zero_coverage_off_axis_fields": score.get(
            "zero_coverage_off_axis_fields"
        ),
        f"{prefix}_mean_spot": score.get("mean_spot"),
        f"{prefix}_mean_spot_um": 1000.0 * _module_files.finite_float(score.get("mean_spot")),
        f"{prefix}_max_spot": score.get("max_spot"),
        f"{prefix}_max_spot_um": 1000.0 * _module_files.finite_float(score.get("max_spot")),
        f"{prefix}_constraint_group_error": score.get("constraint_group_error"),
        f"{prefix}_first_order_group_error": score.get("first_order_group_error"),
        f"{prefix}_image_group_error": score.get("image_group_error"),
        f"{prefix}_min_edge": score.get("min_edge"),
        f"{prefix}_cpu_cuda_consistent": score.get("cpu_cuda_consistent"),
        f"{prefix}_cpu_cuda_trace_match": score.get("cpu_cuda_trace_match"),
        f"{prefix}_cpu_cuda_coverage_max_abs_difference": score.get(
            "cpu_cuda_coverage_max_abs_difference"
        ),
        f"{prefix}_cpu_cuda_min_edge_abs_difference": score.get(
            "cpu_cuda_min_edge_abs_difference"
        ),
        f"{prefix}_cpu_cuda_merit_relative_difference": score.get(
            "cpu_cuda_merit_relative_difference"
        ),
        f"{prefix}_optimizer_merit": score.get("optimizer_merit"),
        f"{prefix}_optimizer_trace_ok": score.get("optimizer_trace_ok"),
        f"{prefix}_optimizer_trace_coverage": score.get(
            "optimizer_trace_coverage"
        ),
        f"{prefix}_optimizer_min_field_coverage": score.get(
            "optimizer_min_field_coverage"
        ),
        f"{prefix}_optimizer_min_chief_progress": score.get(
            "optimizer_min_chief_progress"
        ),
        f"{prefix}_optimizer_min_edge": score.get("optimizer_min_edge"),
        f"{prefix}_optimizer_cache_hit": score.get("optimizer_cache_hit"),
        f"{prefix}_cpu_cache_hit": score.get("cpu_cache_hit"),
        f"{prefix}_wall_seconds": score.get("wall_seconds"),
        f"{prefix}_requested_generations": score.get("requested_generations"),
        f"{prefix}_population_rows": score.get("population_rows"),
        f"{prefix}_constraint_handling": score.get("constraint_handling"),
        f"{prefix}_execution_mode": score.get("execution_mode"),
        f"{prefix}_shared_batch_wall_seconds": score.get(
            "shared_batch_wall_seconds"
        ),
        f"{prefix}_result_dir": score.get("result_dir"),
    }


def classify_diagnostic(row: dict[str, Any], args: argparse.Namespace) -> dict[str, Any]:
    inherited_mean = _module_files.finite_float(row.get("inherited_mean_spot"), math.inf)
    synced_mean = _module_files.finite_float(row.get("synced_mean_spot"), math.inf)
    inherited_constraint = max(
        0.0, _module_files.finite_float(row.get("inherited_constraint_group_error"), math.inf)
    )
    synced_constraint = max(
        0.0, _module_files.finite_float(row.get("synced_constraint_group_error"), math.inf)
    )
    image_growth = (
        synced_mean / max(inherited_mean, 1.0e-12) - 1.0
        if math.isfinite(inherited_mean) and math.isfinite(synced_mean)
        else math.inf
    )
    constraint_delta = synced_constraint - inherited_constraint
    constraint_ratio = synced_constraint / max(inherited_constraint, 1.0e-12)
    trace_ok = _module_contracts.trace_acceptable(row, args, prefix="synced_")
    threshold = float(getattr(args, "material_bridge_constraint_threshold", 2.5))
    image_stable = image_growth <= float(
        getattr(args, "material_bridge_max_image_growth", 0.15)
    )
    constraint_jump = (
        synced_constraint > threshold
        and (
            constraint_delta
            >= float(getattr(args, "material_bridge_constraint_growth_absolute", 1.0))
            or constraint_ratio
            >= float(getattr(args, "material_bridge_constraint_growth_ratio", 2.0))
        )
    )
    if not trace_ok:
        classification = "hard_trace_reject"
    elif synced_constraint <= threshold:
        classification = "stable_common"
    elif image_stable and constraint_jump:
        classification = "aperture_shock_repairable"
    elif not image_stable:
        classification = "image_non_generalizing"
    else:
        classification = "constraint_unhealthy"
    return {
        "bridge_classification": classification,
        "bridge_image_growth": image_growth,
        "bridge_constraint_delta": constraint_delta,
        "bridge_constraint_ratio": constraint_ratio,
        "bridge_image_stable": image_stable,
        "bridge_constraint_jump": constraint_jump,
        "bridge_trace_valid": trace_ok,
    }


def apply_effective_score(
    row: dict[str, Any], score: dict[str, Any], source: Path, prefix: str
) -> None:
    row["effective_merit"] = score.get("merit")
    row["effective_mean_spot"] = score.get("mean_spot")
    row["effective_max_spot"] = score.get("max_spot")
    row["effective_constraint_group_error"] = score.get("constraint_group_error")
    row["effective_trace_ok"] = score.get("trace_ok")
    row["effective_trace_coverage"] = score.get("trace_coverage")
    row["effective_min_field_coverage"] = score.get("min_field_coverage")
    row["effective_min_chief_progress"] = score.get("min_chief_progress")
    row["effective_chief_ray_ok"] = score.get("chief_ray_ok")
    row["effective_chief_ray_failed_fields"] = score.get(
        "chief_ray_failed_fields"
    )
    row["effective_zero_coverage_off_axis_fields"] = score.get(
        "zero_coverage_off_axis_fields"
    )
    row["effective_min_edge"] = score.get("min_edge")
    row["effective_source"] = prefix
    row["out_dir"] = str(source)
    try:
        topology = server.optical_topology_features(
            _module_files.read_json(_module_files.source_file(source, "system.json"))
        )
    except Exception:
        topology = {}
    coarse_key = str(topology.get("coarse_key") or "")
    if coarse_key:
        row.setdefault(
            "source_structural_topology_key",
            row.get("structural_topology_key") or row.get("topology_key"),
        )
        row["structural_topology_key"] = coarse_key
        row["optical_fine_topology_key"] = str(topology.get("fine_key") or "")


def write_bridge_basin_meta(row: dict[str, Any], result_dir: Path) -> None:
    _module_files.write_json(
        result_dir / "basin_meta.json",
        {
            "rank": int(_module_files.finite_float(row.get("basin_rank"), -1)),
            "id": row.get("basin_id"),
            "structural_topology_key": row.get("structural_topology_key")
            or row.get("topology_key"),
            "bridge_classification": row.get("bridge_classification"),
            "geometry_out_dir": row.get("geometry_out_dir"),
        },
    )


def normalized_entry_row(
    row: dict[str, Any], args: argparse.Namespace
) -> dict[str, Any]:
    out = dict(row)
    out["sparse_final_merit"] = row.get("sparse_final_merit", row.get("final_merit"))
    out["sparse_final_mean_spot"] = row.get(
        "sparse_final_mean_spot", row.get("final_mean_spot")
    )
    out["final_merit"] = row.get("effective_merit")
    out["final_mean_spot"] = row.get("effective_mean_spot")
    out["final_constraint_group_error"] = row.get("effective_constraint_group_error")
    out["final_raw_trace_ok"] = row.get("final_trace_ok")
    threshold = _module_contracts.minimum_trace_coverage(args)
    out["final_trace_ok"] = (
        _module_files.csv_bool(row.get("effective_trace_ok"))
        and _module_files.finite_float(row.get("effective_trace_coverage"), 0.0) >= threshold
        and _module_files.finite_float(row.get("effective_min_field_coverage"), 0.0) >= threshold
        and server.vignetting_handoff_policy.chief_ray_usable(
            row,
            ok_key="effective_chief_ray_ok",
            failed_fields_key="effective_chief_ray_failed_fields",
            progress_key="effective_min_chief_progress",
        )
    )
    out["final_min_field_coverage"] = row.get("effective_min_field_coverage")
    out["final_min_chief_progress"] = row.get("effective_min_chief_progress")
    out["final_chief_ray_ok"] = row.get("effective_chief_ray_ok")
    out["final_chief_ray_failed_fields"] = row.get(
        "effective_chief_ray_failed_fields"
    )
    out["final_zero_coverage_off_axis_fields"] = row.get(
        "effective_zero_coverage_off_axis_fields"
    )
    out["final_min_edge"] = row.get("effective_min_edge")
    out["final_materialization_valid"] = (
        bool(out["final_trace_ok"])
        and _module_files.finite_float(row.get("effective_min_edge"), -math.inf)
        >= float(getattr(args, "material_bridge_repair_min_edge", 0.0))
        and 0.0 < _module_files.finite_float(row.get("effective_merit"), math.inf) < 1.0e12
    )
    out["final_trace_acceptance_threshold"] = threshold
    return out


def bridge_row_identity(row: dict[str, Any]) -> tuple[str, str]:
    for key in ("run_id", "state_id"):
        value = str(row.get(key) or "").strip()
        if value:
            return key, value
    return "basin_rank", str(int(_module_files.finite_float(row.get("basin_rank"), -1)))


def propagate_bridge_row_update(
    bridge_rows: list[dict[str, Any]], updated: dict[str, Any]
) -> bool:
    """Write an accepted selection-copy update back to the final row pool."""

    identity = bridge_row_identity(updated)
    for row in bridge_rows:
        if bridge_row_identity(row) == identity:
            row.update(updated)
            return True
    return False
