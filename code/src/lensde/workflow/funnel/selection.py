"""Select ordered sparse-DLS and material-entry candidates without changing ranking semantics."""
from __future__ import annotations
from typing import Any
from .common import finite_float
from pathlib import Path
import argparse
import math
from .common import parse_int_list
from lensde.workflow import select_basin_potential_candidates as potential_select
from .common import read_csv
from .common import write_csv
from .common import write_json
from lensde.workflow import run_material5_bridge_selection as material_bridge


def topology_key(row: dict[str, Any]) -> str:
    return str(
        row.get("structural_topology_key") or row.get("topology_key") or row.get("basin_rank") or ""
    )


def add_unique(
    selected: list[dict[str, Any]],
    seen_ranks: set[int],
    seen_topologies: set[str],
    candidates: list[dict[str, Any]],
    limit: int,
    bucket: str,
) -> None:
    for row in candidates:
        if (
            limit > 0
            and sum((1 for item in selected if item.get("selection_bucket") == bucket)) >= limit
        ):
            break
        rank = int(finite_float(row.get("basin_rank"), -1))
        key = topology_key(row)
        if rank < 0 or rank in seen_ranks or key in seen_topologies:
            continue
        item = dict(row)
        item["selection_bucket"] = bucket
        selected.append(item)
        seen_ranks.add(rank)
        seen_topologies.add(key)


def select_candidates(args: argparse.Namespace, sweep_dir: Path) -> list[dict[str, Any]]:
    rows = [
        potential_select.score_row(row) for row in read_csv(sweep_dir / "dls_potential_summary.csv")
    ]
    rows = [
        row
        for row in rows
        if str(row.get("final_trace_ok", "")).lower() == "true"
        and finite_float(row.get("final_merit"), math.inf) <= args.selection_max_final_merit
    ]
    forced_ranks = set(parse_int_list(args.selection_ranks)) if args.selection_ranks else set()
    if forced_ranks:
        selected = []
        for row in rows:
            rank = int(finite_float(row.get("basin_rank"), -1))
            if rank in forced_ranks:
                item = dict(row)
                item["selection_bucket"] = "forced_rank"
                selected.append(item)
        selected.sort(key=lambda row: int(finite_float(row.get("basin_rank"), -1)))
        selection_dir = args.out_dir.resolve() / "layer1_selected_candidates"
        write_csv(selection_dir / "selected_candidates.csv", selected)
        write_json(
            selection_dir / "selected_candidates.json",
            {
                "sweep_dir": str(sweep_dir),
                "selection_ranks": sorted(forced_ranks),
                "selected_ranks": [
                    int(finite_float(row.get("basin_rank"), -1)) for row in selected
                ],
                "rows": selected,
            },
        )
        missing = sorted(
            forced_ranks - {int(finite_float(row.get("basin_rank"), -1)) for row in selected}
        )
        if missing:
            raise ValueError(
                f"forced selection ranks not found in trace-ok DLS sweep rows: {missing}"
            )
        return selected
    by_merit = sorted(rows, key=lambda row: finite_float(row.get("final_merit"), math.inf))
    by_potential = sorted(
        rows, key=lambda row: finite_float(row.get("potential_score"), -math.inf), reverse=True
    )
    by_diversity = sorted(
        rows,
        key=lambda row: (
            topology_key(row),
            finite_float(row.get("final_merit"), math.inf),
            -finite_float(row.get("potential_score"), -math.inf),
        ),
    )
    selected: list[dict[str, Any]] = []
    seen_ranks: set[int] = set()
    seen_topologies: set[str] = set()
    add_unique(selected, seen_ranks, seen_topologies, by_merit, args.selection_merit_count, "merit")
    add_unique(
        selected,
        seen_ranks,
        seen_topologies,
        by_potential,
        args.selection_potential_count,
        "potential",
    )
    add_unique(
        selected,
        seen_ranks,
        seen_topologies,
        by_diversity,
        args.selection_diversity_count,
        "diversity",
    )
    if args.selection_count > 0 and len(selected) < args.selection_count:
        add_unique(
            selected,
            seen_ranks,
            seen_topologies,
            by_potential,
            args.selection_count - len(selected),
            "fill",
        )
    if args.selection_count > 0:
        selected = selected[: args.selection_count]
    selection_dir = args.out_dir.resolve() / "layer1_selected_candidates"
    write_csv(selection_dir / "selected_candidates.csv", selected)
    write_json(
        selection_dir / "selected_candidates.json",
        {
            "sweep_dir": str(sweep_dir),
            "selected_ranks": [int(finite_float(row.get("basin_rank"), -1)) for row in selected],
            "rows": selected,
        },
    )
    return selected


def material_entry_key(row: dict[str, Any], mode: str) -> tuple[float, float]:
    final = finite_float(row.get("final_merit"), math.inf)
    if str(row.get("final_trace_ok", "")).lower() not in {"true", "1"}:
        final += 1000000000.0
    geometry_total = max(0.0, finite_float(row.get("geometry_total_improvement"), 0.0))
    last_delta = max(0.0, finite_float(row.get("delta_from_prior"), 0.0))
    sweep_delta = max(0.0, finite_float(row.get("sweep_improvement"), 0.0))
    potential = (
        math.log1p(geometry_total) + 0.5 * math.log1p(last_delta) + 0.25 * math.log1p(sweep_delta)
    )
    if mode == "potential":
        return (-potential, final)
    if mode == "merit":
        return (final, -potential)
    return (final / (1.0 + 0.2 * potential), final)


def add_material_entry_rows(
    out: list[dict[str, Any]],
    seen: set[int],
    seen_topologies: set[str],
    rows: list[dict[str, Any]],
    count: int,
    bucket: str,
    *,
    allow_duplicate_topology: bool = False,
) -> None:
    if count <= 0:
        return
    for row in rows:
        rank = int(finite_float(row.get("basin_rank"), -1))
        key = material_bridge.material_topology_family_key(row)
        if rank < 0 or rank in seen or (not allow_duplicate_topology and key in seen_topologies):
            continue
        item = dict(row)
        item["material_entry_bucket"] = bucket
        item["material_topology_family_key"] = key
        out.append(item)
        seen.add(rank)
        seen_topologies.add(key)
        if len([entry for entry in out if entry.get("material_entry_bucket") == bucket]) >= count:
            break


def select_material_entry_rows(
    args: argparse.Namespace, latest_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    limit = int(args.material_entry_count)
    if limit <= 0:
        selected: list[dict[str, Any]] = []
        seen: set[int] = set()
        seen_topologies: set[str] = set()
        add_material_entry_rows(
            selected, seen, seen_topologies, latest_rows, len(latest_rows), "all"
        )
        return selected
    forced_ranks = set(parse_int_list(args.selection_ranks)) | set(
        parse_int_list(args.material_entry_ranks)
    )
    max_final = finite_float(args.material_entry_max_final_merit, math.inf)
    rows = [
        row
        for row in latest_rows
        if int(finite_float(row.get("basin_rank"), -1)) in forced_ranks
        or finite_float(row.get("final_merit"), math.inf) <= max_final
    ]
    selected: list[dict[str, Any]] = []
    seen: set[int] = set()
    seen_topologies: set[str] = set()
    forced = [row for row in rows if int(finite_float(row.get("basin_rank"), -1)) in forced_ranks]
    forced.sort(key=lambda row: material_entry_key(row, args.material_entry_selection_mode))
    add_material_entry_rows(selected, seen, seen_topologies, forced, len(forced), "forced_rank")
    by_merit = sorted(rows, key=lambda row: material_entry_key(row, "merit"))
    by_potential = sorted(rows, key=lambda row: material_entry_key(row, "potential"))
    by_mode = sorted(
        rows, key=lambda row: material_entry_key(row, args.material_entry_selection_mode)
    )
    remaining = max(0, limit - len(selected))
    add_material_entry_rows(
        selected,
        seen,
        seen_topologies,
        by_merit,
        min(args.material_entry_merit_count, remaining),
        "merit",
    )
    remaining = max(0, limit - len(selected))
    add_material_entry_rows(
        selected,
        seen,
        seen_topologies,
        by_potential,
        min(args.material_entry_potential_count, remaining),
        "potential",
    )
    remaining = max(0, limit - len(selected))
    add_material_entry_rows(selected, seen, seen_topologies, by_mode, remaining, "fill")
    return selected[: max(limit, len(forced))]
