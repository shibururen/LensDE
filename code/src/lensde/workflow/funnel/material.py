"""Prepare continuous-material entry, optional bridge/repair, and Scout source handoffs."""
from __future__ import annotations
from typing import Any
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .contracts import MaterialPolicy
from pathlib import Path
import argparse
from .common import finite_float
from lensde.workflow import run_material5_bridge_selection as material_bridge
from lensde.workflow import run_material_chromatic_weak_geometry_stage as material_chromatic_repair
from lensde.workflow import run_post_de_basin_refine_funnel as material_funnel
from .common import max_local_iter_per_restart
from .common import prescout_material_trace_sampling
from .common import record_candidate_shortfall
from .selection import select_material_entry_rows
from lensde.engine import core as server
from .calibration_policy import stage_boundary_vignetting_recalibration_enabled
from .common import write_csv
from .common import write_json
import math
from .common import read_csv
from .common import read_json
from .common import _row_metric
from .common import _row_truthy


def run_material_layer(
    args: argparse.Namespace, latest_rows: list[dict[str, Any]], *, _version: MaterialPolicy
) -> tuple[Path | None, list[dict[str, Any]]]:
    if args.skip_material_funnel:
        return (None, [])
    material_context_run = (
        args.material_reference_run.resolve()
        if args.material_reference_run
        else args.basin_run_dir.resolve()
    )
    trace_sampling = prescout_material_trace_sampling(args)
    boundary_recalibration_enabled = stage_boundary_vignetting_recalibration_enabled(args)
    bridge_min_trace = float(
        getattr(args, "material_bridge_repair_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
    )
    bridge_dir: Path | None = None
    bridge_fallback_reason = ""
    material_bridge_enabled = bool(getattr(args, "material_bridge_enabled", True))
    if material_bridge_enabled:
        bridge_role = (
            "Scout-aligned diagnostic, aperture repair, and selected-entry DE preconditioning"
            if bool(getattr(args, "material_bridge_deepen_enabled", True))
            else "Scout-aligned diagnostic and 7-to-entry screening"
        )
        print(f"=== material bridge: {bridge_role} ===", flush=True)
        entry_rows, bridge_dir = material_bridge.run_bridge(
            args, latest_rows, material_context_run=material_context_run
        )
        target_count = max(0, int(getattr(args, "material_entry_count", 0)))
        if target_count > 0 and len(entry_rows) < target_count:
            bridge_fallback_reason = (
                "bridge_selected_no_acceptable_candidates"
                if not entry_rows
                else "bridge_selected_fewer_than_material_entry_count"
            )
            record_candidate_shortfall(args, "material_bridge", target_count, entry_rows)
    else:
        entry_rows = select_material_entry_rows(args, latest_rows)
    chromatic_repair_dir: Path | None = None
    if bool(getattr(args, "material_chromatic_repair_enabled", False)):
        print(
            f"=== pre-Material-Funnel chromatic material + weak geometry repair: candidates={len(entry_rows)}, seed={int(getattr(args, 'material_chromatic_repair_seed', 20260804))} ===",
            flush=True,
        )
        entry_rows, chromatic_repair_dir = material_chromatic_repair.run_stage(
            args, entry_rows, material_context_run=material_context_run
        )
    if boundary_recalibration_enabled and (
        chromatic_repair_dir is not None or not material_bridge_enabled
    ):
        material_boundary_name = (
            "layer2_stage_boundary_chromatic_repair_to_material_funnel_vignetting"
            if chromatic_repair_dir is not None
            else "layer2_stage_boundary_material_bridge_to_material_funnel_vignetting"
        )
        entry_rows = _version.recalibrate_vignetting_source_rows(
            args,
            entry_rows,
            stage_name=material_boundary_name,
            sampling=trace_sampling,
            min_trace=bridge_min_trace,
            min_field=bridge_min_trace,
        )
    entry_dir = args.out_dir.resolve() / "layer2_material_entry_selection"
    write_csv(entry_dir / "material_entry_candidates.csv", entry_rows)
    write_json(
        entry_dir / "material_entry_candidates.json",
        {
            "selected_ranks": [int(finite_float(row.get("basin_rank"), -1)) for row in entry_rows],
            "rows": entry_rows,
            "material_entry_count": args.material_entry_count,
            "material_entry_merit_count": args.material_entry_merit_count,
            "material_entry_potential_count": args.material_entry_potential_count,
            "material_entry_diversity_count": int(
                getattr(args, "material_entry_diversity_count", 0)
            ),
            "material_entry_quality_guard_ratio": float(
                getattr(args, "material_entry_quality_guard_ratio", 1.1)
            ),
            "material_entry_selection_mode": args.material_entry_selection_mode,
            "unique_topology_count": len(
                {material_bridge.material_topology_family_key(row) for row in entry_rows}
            ),
            "topology_shortfall": max(0, max(0, int(args.material_entry_count)) - len(entry_rows)),
            "material_bridge_enabled": bool(getattr(args, "material_bridge_enabled", True)),
            "material_bridge_dir": str(bridge_dir) if bridge_dir else "",
            "material_bridge_fallback_reason": bridge_fallback_reason,
            "material_chromatic_repair_enabled": bool(
                getattr(args, "material_chromatic_repair_enabled", False)
            ),
            "material_chromatic_repair_dir": str(chromatic_repair_dir)
            if chromatic_repair_dir
            else "",
        },
    )
    candidate_dirs = [str(row.get("out_dir")) for row in entry_rows if row.get("out_dir")]
    terminal_mode = (
        str(getattr(args, "material_terminal_mode", "funnel") or "funnel").strip().lower()
    )
    if terminal_mode == "direct_glass_de":
        print(
            f"=== material terminal: bridge entries feed {terminal_mode} ({len(candidate_dirs)} candidates) ===",
            flush=True,
        )
        return (None, entry_rows)
    out_dir = server.native_io_paths.named_child(
        args.out_dir.resolve(), "layer2_material_glass_funnel"
    )
    namespace = argparse.Namespace(
        basin_run_dir=material_context_run,
        out_dir=out_dir,
        reference_run=args.material_reference_run.resolve()
        if args.material_reference_run
        else None,
        merit_template=args.material_merit_template.resolve()
        if args.material_merit_template
        else None,
        stages_json=args.material_stages_json.resolve() if args.material_stages_json else None,
        candidate_parent_dir=None,
        candidate_run_dirs=";".join(candidate_dirs),
        ranks="",
        max_basins=0,
        keep=args.material_keep,
        max_per_topology=args.material_max_per_topology,
        preserve_candidate_lineages=True,
        selection_mode=args.material_selection_mode,
        min_field_count=args.material_min_field_count,
        field_hy_fractions=trace_sampling["field_hy_fractions"],
        trace_rays=trace_sampling["num_rays"],
        trace_arms=trace_sampling["pupil_arms"],
        trace_distribution=trace_sampling["pupil_distribution"],
        min_trace_coverage=float(
            getattr(
                args, "material_bridge_repair_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE
            )
        ),
        min_field_coverage=float(
            getattr(
                args, "material_bridge_repair_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE
            )
        ),
        local_iter=args.material_local_iter,
        max_local_iter_per_restart=max_local_iter_per_restart(args),
        local_lambda=args.material_local_lambda,
        local_fd=args.material_local_fd,
        local_tol=args.material_local_tol,
        local_max_reject=args.material_local_max_reject,
        local_plateau_window=getattr(args, "material_local_plateau_window", 50),
        local_plateau_min_iterations=getattr(args, "material_local_plateau_min_iterations", 80),
        local_plateau_min_abs_improvement=getattr(
            args, "material_local_plateau_min_abs_improvement", 0.02
        ),
        local_plateau_min_rel_improvement=getattr(
            args, "material_local_plateau_min_rel_improvement", 0.01
        ),
        restart_early_stop=getattr(args, "material_restart_early_stop", True),
        restart_early_stop_max_iterations=getattr(
            args, "material_restart_early_stop_max_iterations", 20
        ),
        restart_early_stop_min_abs_improvement=getattr(
            args, "material_restart_early_stop_min_abs_improvement", 0.002
        ),
        restart_early_stop_min_rel_improvement=getattr(
            args, "material_restart_early_stop_min_rel_improvement", 0.001
        ),
        local_method=args.local_method,
        local_bounds_mode=args.local_bounds_mode,
        local_residual_mode=args.local_residual_mode,
        optimizer_internal_scaling=args.optimizer_internal_scaling,
        timeout=args.timeout,
        force_rerun=args.force_rerun,
        dry_run=False,
        exe=args.exe.resolve() if args.exe else None,
        workers=getattr(args, "workers", 1),
        child_cpu_workers=getattr(args, "child_cpu_workers", 0),
        stop_before_first_snap=terminal_mode == "scout_lock_fixed_de",
        force_global_sampling=terminal_mode == "scout_lock_fixed_de",
    )
    label = (
        "continuous Material Funnel DLS"
        if terminal_mode == "scout_lock_fixed_de"
        else "material/glass funnel"
    )
    print(f"=== {label}: candidates={len(candidate_dirs)} ===", flush=True)
    material_funnel.run_once(namespace)
    return (out_dir, entry_rows)


def source_dirs_from_material_ranked(material_dir: Path, limit: int) -> list[Path]:
    ranked_path = material_dir / "funnel_final_ranked.csv"
    if not ranked_path.exists():
        return []
    out: list[Path] = []
    seen: set[str] = set()
    for row in read_csv(ranked_path):
        for key in ["out_dir", "final_out_dir", "source_dir", "continuous_stage_out_dir"]:
            text = str(row.get(key) or "").strip()
            if not text:
                continue
            path = Path(text).resolve()
            if not path.exists() or not path.is_dir():
                continue
            if not (path / "system.json").exists() or not (path / "score.json").exists():
                continue
            score = read_json(path / "score.json")
            if (
                not bool(score.get("trace_ok", False))
                or finite_float(score.get("merit"), math.inf) >= 1000000000000.0
                or finite_float(score.get("trace_coverage"), 0.0) < DE_DLS_TRACE_COVERAGE_BASELINE
                or (
                    finite_float(score.get("min_field_coverage"), 0.0)
                    < DE_DLS_TRACE_COVERAGE_BASELINE
                )
                or (not server.vignetting_handoff_policy.chief_ray_usable(score))
                or (finite_float(score.get("min_edge"), -math.inf) < 0.0)
            ):
                continue
            path_key = str(path).lower()
            if path_key in seen:
                continue
            seen.add(path_key)
            out.append(path)
            break
        if len(out) >= max(1, limit):
            break
    return out


def continuous_material_scout_sources(
    args: argparse.Namespace, material_dir: Path | None, parent_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    """Select continuous-DLS children per basin while retaining every parent."""
    if material_dir is None:
        return parent_rows
    ranked_path = material_dir / "continuous_presnap_ranked.csv"
    if not ranked_path.is_file():
        raise FileNotFoundError(f"continuous Material Funnel DLS handoff is missing: {ranked_path}")
    children_by_rank: dict[int, list[dict[str, Any]]] = {}
    for child in read_csv(ranked_path):
        rank = int(finite_float(child.get("basin_rank"), -1))
        children_by_rank.setdefault(rank, []).append(child)
    guard_enabled = bool(getattr(args, "material_continuous_dls_guard_enabled", True))
    constraint_ratio = max(
        1.0, float(getattr(args, "material_continuous_dls_constraint_growth_ratio", 4.0))
    )
    constraint_absolute = max(
        0.0, float(getattr(args, "material_continuous_dls_constraint_growth_absolute", 0.01))
    )
    selected: list[dict[str, Any]] = []
    audit_rows: list[dict[str, Any]] = []
    for position, parent in enumerate(parent_rows, start=1):
        basin_rank = int(finite_float(parent.get("basin_rank"), position))
        candidates = children_by_rank.get(basin_rank, [])
        candidates.sort(key=lambda row: _row_metric(row, "continuous_final_merit", "final_merit"))
        child = candidates[0] if candidates else None
        reason = (
            "continuous_dls_selected" if child is not None else "continuous_dls_missing_candidate"
        )
        acceptable = child is not None
        child_dir: Path | None = None
        if child is not None:
            child_dir = Path(
                str(child.get("continuous_stage_out_dir") or child.get("out_dir") or "")
            ).resolve()
            child_merit = _row_metric(child, "continuous_final_merit", "final_merit")
            parent_merit = _row_metric(
                parent, "final_merit", "merit", "archive_merit", "source_merit"
            )
            child_constraint = _row_metric(
                child, "final_constraint_group_error", "constraint_group_error", "constraint_error"
            )
            parent_constraint = _row_metric(
                parent, "final_constraint_group_error", "constraint_group_error", "constraint_error"
            )
            trace_ok = (
                int(finite_float(child.get("returncode"), -1)) == 0
                and (not _row_truthy(child.get("timed_out", False)))
                and _row_truthy(child.get("final_trace_ok", child.get("trace_ok")))
                and (
                    _row_metric(child, "final_trace_coverage", "trace_coverage")
                    >= float(
                        getattr(
                            args, "glass_scout_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE
                        )
                    )
                )
                and (
                    _row_metric(child, "final_min_field_coverage", "min_field_coverage")
                    >= float(
                        getattr(
                            args, "glass_scout_min_field_coverage", DE_DLS_TRACE_COVERAGE_BASELINE
                        )
                    )
                )
                and server.vignetting_handoff_policy.chief_ray_usable(
                    child,
                    ok_key="final_chief_ray_ok",
                    failed_fields_key="final_chief_ray_failed_fields",
                    progress_key="final_min_chief_progress",
                )
                and (_row_metric(child, "final_min_edge", "min_edge") >= 0.0)
            )
            if not child_dir.is_dir() or not (child_dir / "system.json").is_file():
                acceptable, reason = (False, "continuous_dls_missing_artifacts")
            elif not trace_ok or not math.isfinite(child_merit):
                acceptable, reason = (False, "continuous_dls_failed_or_trace_invalid")
            elif (
                guard_enabled
                and math.isfinite(parent_constraint)
                and math.isfinite(child_constraint)
                and (
                    child_constraint
                    > max(
                        parent_constraint * constraint_ratio,
                        parent_constraint + constraint_absolute,
                    )
                )
            ):
                acceptable, reason = (False, "continuous_dls_constraint_regression")
            elif math.isfinite(parent_merit) and child_merit > parent_merit:
                reason = "continuous_dls_selected_with_expected_transition_cost"
        if acceptable and child is not None and (child_dir is not None):
            handoff = dict(child)
            handoff["out_dir"] = str(child_dir)
            handoff["continuous_dls_handoff"] = True
            handoff["continuous_dls_parent_dir"] = str(parent.get("out_dir") or "")
        else:
            handoff = dict(parent)
            handoff["continuous_dls_handoff"] = False
        handoff["continuous_dls_selection_reason"] = reason
        selected.append(handoff)
        audit_rows.append(
            {
                "basin_rank": basin_rank,
                "selected_out_dir": str(handoff.get("out_dir") or ""),
                "continuous_dls_selected": bool(acceptable),
                "selection_reason": reason,
                "parent_final_merit": _row_metric(parent, "final_merit", "merit"),
                "child_final_merit": _row_metric(
                    child or {}, "continuous_final_merit", "final_merit"
                ),
                "child_merit_regressed_vs_parent": bool(
                    child is not None
                    and math.isfinite(_row_metric(parent, "final_merit", "merit"))
                    and math.isfinite(_row_metric(child, "continuous_final_merit", "final_merit"))
                    and (
                        _row_metric(child, "continuous_final_merit", "final_merit")
                        > _row_metric(parent, "final_merit", "merit")
                    )
                ),
                "parent_constraint_error": _row_metric(
                    parent,
                    "final_constraint_group_error",
                    "constraint_group_error",
                    "constraint_error",
                ),
                "child_constraint_error": _row_metric(
                    child or {},
                    "final_constraint_group_error",
                    "constraint_group_error",
                    "constraint_error",
                ),
            }
        )
    audit_dir = material_dir
    if getattr(args, "reuse_material_funnel", None):
        audit_dir = args.out_dir.resolve() / "resume_contracts"
    write_csv(audit_dir / "continuous_dls_scout_handoff.csv", audit_rows)
    write_json(
        audit_dir / "continuous_dls_scout_handoff.json",
        {
            "guard_enabled": guard_enabled,
            "merit_regression_is_audit_only": True,
            "acceptance_policy": "continuous child by default; fallback only for missing artifacts, non-finite/trace-invalid output, or severe constraint growth",
            "constraint_growth_ratio": constraint_ratio,
            "constraint_growth_absolute": constraint_absolute,
            "rows": audit_rows,
        },
    )
    return selected
