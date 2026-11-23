"""Run bounded geometry restart cycles and persist their source-aligned reports."""
from __future__ import annotations
from typing import Any
from pathlib import Path
from .common import finite_float
import math
from .common import write_csv
from .common import write_json
import argparse
from .common import cycle_iter
from lensde.workflow import run_selected_basin_deep_refine as deep_refine
from .common import read_csv
from .common import restart_iter_schedule


def append_restart_report(
    out_dir: Path, rows: list[dict[str, Any]], latest: dict[int, dict[str, Any]]
) -> None:
    write_csv(out_dir / "restart_summary.csv", rows)
    ranked = sorted(latest.values(), key=lambda row: finite_float(row.get("final_merit"), math.inf))
    write_csv(out_dir / "restart_latest_ranked.csv", ranked)
    write_json(out_dir / "restart_summary.json", {"rows": rows, "latest_by_final_merit": ranked})


def run_restart_layer(
    args: argparse.Namespace, reference_run: Path, selected: list[dict[str, Any]], sweep_dir: Path
) -> list[dict[str, Any]]:
    restart_dir = args.out_dir.resolve() / "layer1_deep_geometry_restart"
    restart_dir.mkdir(parents=True, exist_ok=True)
    schedule = restart_iter_schedule(args)
    active = [int(finite_float(row.get("basin_rank"), -1)) for row in selected]
    active = [rank for rank in active if rank >= 0]
    prior = sweep_dir
    rows: list[dict[str, Any]] = []
    latest: dict[int, dict[str, Any]] = {}
    stale_counts: dict[int, int] = {rank: 0 for rank in active}
    for row in selected:
        rank = int(finite_float(row.get("basin_rank"), -1))
        if rank < 0:
            continue
        initial = {
            "cycle": 0,
            "basin_rank": rank,
            "selection_bucket": row.get("selection_bucket"),
            "potential_score": row.get("potential_score"),
            "structural_topology_key": row.get("structural_topology_key")
            or row.get("topology_key"),
            "sweep_initial_merit": row.get("initial_merit"),
            "sweep_final_merit": row.get("final_merit"),
            "sweep_improvement": row.get("improvement"),
            "geometry_initial_merit": row.get("final_merit"),
            "geometry_total_improvement": 0.0,
            "prior_sweep": "",
            "out_dir": row.get("out_dir"),
            "prior_merit": "",
            "final_merit": row.get("final_merit"),
            "delta_from_prior": "",
            "final_trace_ok": row.get("final_trace_ok"),
            "final_mean_spot": row.get("final_mean_spot"),
            "final_efl": row.get("final_efl"),
            "final_ttl": row.get("final_ttl"),
            "final_bfl": row.get("final_bfl"),
            "stopped": False,
            "stop_reason": "",
        }
        rows.append(initial)
        latest[rank] = initial
    append_restart_report(restart_dir, rows, latest)
    for cycle in range(1, args.restart_max_cycles + 1):
        if not active:
            break
        local_iter = cycle_iter(schedule, cycle)
        cycle_dir = restart_dir / f"cycle_{cycle:02d}"
        namespace = argparse.Namespace(
            prior_sweep=prior,
            reference_dls_run=reference_run,
            out_dir=cycle_dir,
            ranks=",".join((str(rank) for rank in active)),
            local_iter=local_iter,
            local_lambda=args.restart_local_lambda,
            local_fd=args.restart_local_fd,
            local_tol=args.restart_local_tol,
            local_max_reject=args.restart_local_max_reject,
            timeout=args.timeout,
            force_rerun=args.force_rerun,
            exe=args.exe.resolve() if args.exe else None,
            workers=getattr(args, "workers", 1),
            child_cpu_workers=getattr(args, "child_cpu_workers", 0),
        )
        print(
            f"=== deep restart cycle {cycle}: ranks={active}, local_iter={local_iter} ===",
            flush=True,
        )
        deep_refine.run_once(namespace)
        cycle_rows = read_csv(cycle_dir / "deep_refine_summary.csv")
        next_active: list[int] = []
        for row in cycle_rows:
            rank = int(finite_float(row.get("basin_rank"), -1))
            delta = finite_float(row.get("delta_from_prior"), 0.0)
            prior_merit = finite_float(row.get("prior_merit"), math.nan)
            final_merit = finite_float(row.get("final_merit"), math.nan)
            previous = latest.get(rank, {})
            geometry_initial_merit = finite_float(
                previous.get("geometry_initial_merit"), prior_merit
            )
            geometry_total_improvement = (
                max(0.0, geometry_initial_merit - final_merit)
                if math.isfinite(geometry_initial_merit) and math.isfinite(final_merit)
                else 0.0
            )
            rel_delta = (
                delta / max(abs(prior_merit), 1e-09) if math.isfinite(prior_merit) else math.inf
            )
            stopped = False
            stop_reason = ""
            if cycle >= args.restart_min_cycles:
                if delta < args.restart_min_delta and rel_delta < args.restart_min_rel_delta:
                    stale_counts[rank] = stale_counts.get(rank, 0) + 1
                else:
                    stale_counts[rank] = 0
                if stale_counts.get(rank, 0) >= args.restart_patience:
                    stopped = True
                    stop_reason = (
                        f"delta<{args.restart_min_delta:g} and rel<{args.restart_min_rel_delta:g}"
                    )
            item = {
                "cycle": cycle,
                "basin_rank": rank,
                "selection_bucket": previous.get("selection_bucket"),
                "potential_score": previous.get("potential_score"),
                "structural_topology_key": previous.get("structural_topology_key")
                or previous.get("topology_key"),
                "sweep_initial_merit": previous.get("sweep_initial_merit"),
                "sweep_final_merit": previous.get("sweep_final_merit"),
                "sweep_improvement": previous.get("sweep_improvement"),
                "geometry_initial_merit": geometry_initial_merit,
                "geometry_total_improvement": geometry_total_improvement,
                "prior_sweep": str(prior),
                "out_dir": row.get("out_dir"),
                "prior_merit": row.get("prior_merit"),
                "final_merit": row.get("final_merit"),
                "delta_from_prior": row.get("delta_from_prior"),
                "relative_delta_from_prior": rel_delta,
                "final_trace_ok": row.get("final_trace_ok"),
                "final_mean_spot": row.get("final_mean_spot"),
                "final_efl": row.get("final_efl"),
                "final_ttl": row.get("final_ttl"),
                "final_bfl": row.get("final_bfl"),
                "final_constraint_group_error": row.get("final_constraint_group_error"),
                "final_first_order_group_error": row.get("final_first_order_group_error"),
                "final_image_group_error": row.get("final_image_group_error"),
                "local_iter": local_iter,
                "stopped": stopped,
                "stop_reason": stop_reason,
            }
            rows.append(item)
            latest[rank] = item
            if not stopped:
                next_active.append(rank)
        active = next_active
        append_restart_report(restart_dir, rows, latest)
        prior = cycle_dir
    return sorted(latest.values(), key=lambda row: finite_float(row.get("final_merit"), math.inf))
