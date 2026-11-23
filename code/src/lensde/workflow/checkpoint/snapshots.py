"""Ordered upstream outer/event/basin snapshot discovery and decoding."""
from __future__ import annotations

import math
from pathlib import Path
from typing import Any, Iterable
from . import files as _module_files


def discover_outer_dirs(root: Path) -> list[Path]:
    root = root.resolve()
    children = sorted(
        path
        for path in root.glob("outer_*")
        if path.is_dir()
        and (path / "checkpoint_candidate_snapshots" / "candidate_events.jsonl").is_file()
    )
    if children:
        return children
    if (root / "checkpoint_candidate_snapshots" / "candidate_events.jsonl").is_file():
        return [root]
    raise FileNotFoundError(
        "no completed checkpoint Outer found below "
        f"{root}; expected checkpoint_candidate_snapshots/candidate_events.jsonl"
    )


def candidate_stage_sources(outer_dir: Path) -> list[dict[str, Any]]:
    """Read all completed Outer phases, including legacy loose basin archives."""
    summary_path = outer_dir / "vignetting_two_stage_summary.json"
    summary = _module_files.read_json(summary_path) if summary_path.is_file() else {}
    loose_dir = outer_dir / "vignetting_loose"
    split = loose_dir.is_dir() and summary.get("mode", "loose_then_strict") == "loose_then_strict"
    stages = []
    offset = 0
    if split:
        stages.append({"phase": "loose", "dir": loose_dir, "offset": 0, "terminal": False})
        offset = int(summary.get("loose_total_generations") or 0)
        if not offset:
            index = loose_dir / "checkpoint_candidate_snapshots/snapshot_index.jsonl"
            if index.is_file():
                offset = max((int(row.get("generation") or 0) for row in _module_files.read_jsonl(index)), default=0)
    stages.append({"phase": "strict" if split else "single", "dir": outer_dir,
                   "offset": offset, "terminal": True})
    return stages


def annotate_stage_rows(rows: list[dict[str, Any]], stage: dict[str, Any]) -> list[dict[str, Any]]:
    for row in rows:
        generation = int(row.get("snapshot_generation") or 0)
        row.update(source_phase=stage["phase"], source_stage_dir=str(stage["dir"]),
                   source_stage_generation=generation, source_phase_terminal=stage["terminal"],
                   snapshot_generation=generation + stage["offset"],
                   event_id=f"{stage['phase']}:{row['event_id']}")
    return rows


def event_rows_for_outer(outer_dir: Path, outer: int, dimension: int) -> list[dict[str, Any]]:
    rows = []
    for stage in candidate_stage_sources(outer_dir):
        path = stage["dir"] / "checkpoint_candidate_snapshots/candidate_events.jsonl"
        if path.is_file():
            rows.extend(annotate_stage_rows(_event_rows_for_stage(stage["dir"], outer, dimension), stage))
    if not rows:
        raise ValueError(f"checkpoint event stream is empty: {outer_dir}")
    return rows


def _event_rows_for_stage(
    outer_dir: Path,
    outer: int,
    dimension: int,
) -> list[dict[str, Any]]:
    path = outer_dir / "checkpoint_candidate_snapshots" / "candidate_events.jsonl"
    raw_events = _module_files.read_jsonl(path)
    if not raw_events:
        raise ValueError(f"checkpoint event stream is empty: {path}")
    rows: list[dict[str, Any]] = []
    for event_index, raw in enumerate(raw_events):
        values = [float(value) for value in raw.get("values") or []]
        if len(values) != dimension:
            raise ValueError(
                f"Outer {outer} event {event_index} dimension {len(values)} != {dimension}"
            )
        generation = int(raw.get("snapshot_generation") or 0)
        local_id = raw.get("population_index", raw.get("basin_id", event_index))
        v_hash = _module_files.value_hash(values)
        rows.append(
            {
                **raw,
                "event_id": f"outer{outer:02d}_g{generation:04d}_{raw.get('record')}_{local_id}",
                "outer": outer,
                "outer_dir": str(outer_dir),
                "snapshot_generation": generation,
                "merit": _module_files.finite(raw.get("merit")),
                "rank1_merit": _module_files.finite(raw.get("rank1_merit")),
                "trace_feasible": int(bool(raw.get("trace_feasible"))),
                "constraint_feasible": int(bool(raw.get("constraint_feasible"))),
                "first_order_feasible": int(bool(raw.get("first_order_feasible"))),
                "topology_key": str(raw.get("topology_key") or ""),
                "semantic_key": str(raw.get("semantic_key") or ""),
                "descriptor_key": str(raw.get("descriptor_key") or ""),
                "state_id": v_hash,
                "value_hash": v_hash,
                "values": values,
            }
        )
    return rows


def basin_rows_for_outer(outer_dir: Path, outer: int, dimension: int,
                         rank1: dict[str, Any]) -> list[dict[str, Any]]:
    rows = []
    for stage in candidate_stage_sources(outer_dir):
        stage_rank1 = dict(rank1)
        score_path = stage["dir"] / "score.json"
        if not stage["terminal"] and score_path.is_file():
            stage_rank1["merit"] = _module_files.finite(_module_files.read_json(score_path).get("merit"), _module_files.finite(rank1.get("merit")))
        rows.extend(annotate_stage_rows(
            _basin_rows_for_stage(stage["dir"], outer, dimension, stage_rank1), stage
        ))
    return rows


def _basin_rows_for_stage(
    outer_dir: Path,
    outer: int,
    dimension: int,
    rank1: dict[str, Any],
) -> list[dict[str, Any]]:
    """Return archived basin states in the checkpoint candidate row contract.

    Checkpoint snapshots remain the primary source.  These rows are consumed
    only when the snapshot selector cannot fill its requested independent
    checkpoint budget, so a sparse snapshot stream does not force the whole
    continuation layer back to serial execution.
    """
    path = outer_dir / "basin_archive.json"
    if not path.is_file():
        return []
    payload = _module_files.read_json(path)
    raw_basins = payload.get("basins", []) if isinstance(payload, dict) else payload
    if not isinstance(raw_basins, list):
        return []
    rank1_merit = _module_files.finite(rank1.get("merit"))
    rows: list[dict[str, Any]] = []
    for basin_index, raw in enumerate(raw_basins):
        if not isinstance(raw, dict):
            continue
        values = [float(value) for value in raw.get("values") or []]
        if len(values) != dimension:
            continue
        score = raw.get("score") if isinstance(raw.get("score"), dict) else {}
        merit = _module_files.finite(score.get("merit"), _module_files.finite(raw.get("merit")))
        if not math.isfinite(merit):
            continue
        generation = int(raw.get("generation") or raw.get("snapshot_generation") or 0)
        basin_id = raw.get("id", basin_index)
        v_hash = _module_files.value_hash(values)
        rows.append(
            {
                **raw,
                "record": "basin_archive",
                "event_id": f"outer{outer:02d}_basin_{basin_id}",
                "outer": outer,
                "outer_dir": str(outer_dir),
                "snapshot_generation": generation,
                "population_index": basin_id,
                "merit_rank": int(raw.get("rank") or basin_index + 1),
                "merit": merit,
                "rank1_merit": rank1_merit,
                "trace_feasible": int(
                    bool(raw.get("trace_feasible", score.get("trace_ok", False)))
                ),
                "constraint_feasible": int(bool(raw.get("constraint_feasible", False))),
                "first_order_feasible": int(bool(raw.get("first_order_feasible", False))),
                "topology_key": str(
                    raw.get("topology_key") or raw.get("structural_topology_key") or ""
                ),
                "semantic_key": str(raw.get("semantic_key") or ""),
                "descriptor_key": str(raw.get("descriptor_key") or ""),
                "state_id": v_hash,
                "value_hash": v_hash,
                "values": values,
                "basin_archive_path": str(path),
                "source_basin_id": basin_id,
            }
        )
    return rows


def final_context(rows: list[dict[str, Any]]) -> tuple[int, dict[str, Any]]:
    terminal_rows = [row for row in rows if row.get("source_phase_terminal", True)]
    final_generation = max(int(row["snapshot_generation"]) for row in terminal_rows)
    final_population = [
        row
        for row in terminal_rows
        if row.get("record") == "population"
        and int(row["snapshot_generation"]) == final_generation
    ]
    if not final_population:
        raise ValueError(f"Outer {rows[0].get('outer')} has no final population snapshot")
    return final_generation, min(final_population, key=lambda row: _module_files.finite(row.get("merit")))


def valid_nonrank(row: dict[str, Any], rank1: dict[str, Any]) -> bool:
    merit = _module_files.finite(row.get("merit"))
    source_rank1 = _module_files.finite(row.get("rank1_merit"), _module_files.finite(rank1.get("merit")))
    return (
        str(row.get("state_id")) != str(rank1.get("state_id"))
        and bool(int(row.get("trace_feasible") or 0))
        and math.isfinite(merit)
        and merit > 0.0
        and math.isfinite(source_rank1)
        and source_rank1 > 0.0
        # An earlier rank-1 can be structurally different from the final anchor.
        # Exact final-anchor/state duplication is checked above and by the selector.
    )
