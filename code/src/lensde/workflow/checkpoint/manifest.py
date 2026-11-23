"""Stable candidate identities and rank-one/checkpoint continuation manifests."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable
from . import files as _module_files


def snapshot_merits(path: Path) -> dict[int, float]:
    return {
        int(row["generation"]): _module_files.finite(row.get("best_merit"))
        for row in _module_files.read_jsonl(path)
        if "generation" in row
    }


def candidate_id(item: dict[str, Any]) -> str:
    role = "r" if item["run_role"] == "rank1" else "c"
    return f"o{int(item['outer']):02d}_{role}_{str(item['state_id'])[:12]}"


def checkpoint_state_identity(row: dict[str, Any]) -> str:
    state = str(row.get("state_id") or "").strip()
    if state:
        return state
    return hashlib.sha256(
        json.dumps(_module_files.parse_values(row), separators=(",", ":")).encode("utf-8")
    ).hexdigest()


def make_run_items(
    checkpoints: list[dict[str, Any]],
    rank1_by_outer: dict[int, dict[str, Any]],
    rank1_anchor_outers: Iterable[int] | None = None,
) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    for row in checkpoints:
        outer = int(row["outer"])
        rank1 = rank1_by_outer[outer]
        items.append(
            {
                **row,
                "run_role": "checkpoint",
                "anchor_values": _module_files.parse_values(row),
                "matched_rank1_state_id": rank1["state_id"],
                "matched_rank1_values": _module_files.parse_values(rank1),
                "matched_rank1_source_merit": _module_files.finite(rank1.get("merit")),
            }
        )
    anchor_outers = set(
        rank1_by_outer if rank1_anchor_outers is None else rank1_anchor_outers
    )
    for outer, rank1 in sorted(rank1_by_outer.items()):
        if outer not in anchor_outers:
            continue
        values = _module_files.parse_values(rank1)
        items.append(
            {
                **rank1,
                "run_role": "rank1",
                "selection_reason": "fixed_outer_rank1",
                "source_quality_ratio": 1.0,
                "anchor_values": values,
                "matched_rank1_state_id": rank1["state_id"],
                "matched_rank1_values": values,
                "matched_rank1_source_merit": _module_files.finite(rank1.get("merit")),
            }
        )
    for item in items:
        item["run_id"] = candidate_id(item)
    # Rank-1 controls win only an exact state collision (principally archived
    # replay manifests). Structural-family uniqueness is intentionally
    # deferred to the 20 -> 7 and 7 -> 4 selectors.
    unique_items: list[dict[str, Any]] = []
    seen_states: set[str] = set()
    for item in sorted(
        items,
        key=lambda row: (
            0 if row["run_role"] == "rank1" else 1,
            _module_files.finite(row.get("merit")),
            int(row["outer"]),
        ),
    ):
        state = checkpoint_state_identity(item)
        if state in seen_states:
            continue
        seen_states.add(state)
        unique_items.append(item)
    return sorted(
        unique_items,
        key=lambda row: (
            int(row["outer"]),
            0 if row["run_role"] == "rank1" else 1,
            int(row.get("global_selection_rank") or 10**6),
        ),
    )
