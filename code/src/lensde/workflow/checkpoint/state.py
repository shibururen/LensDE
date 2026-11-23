"""Explicit phase records and named injectable checkpoint actions."""
from __future__ import annotations

from pathlib import Path
from typing import Any, Iterable
from dataclasses import dataclass
from typing import Any, Callable


@dataclass
class CheckpointInputs:
    base_system: dict[str, Any]
    variables_doc: dict[str, Any]
    merit: dict[str, Any]
    glass_search_config: dict[str, Any] | None
    variables: list[dict[str, Any]]
    material_variable_contract: dict[str, Any]
    roles: list[str]
    fallback_command: list[str]
    reuse_manifest: Path | None

@dataclass
class CheckpointSelection:
    outer_dirs: list[Path]
    events_by_outer: dict[int, list[dict[str, Any]]]
    rank1_by_outer: dict[int, dict[str, Any]]
    basin_rows_by_outer: dict[int, list[dict[str, Any]]]
    requested_total_count: int
    rank1_anchor_outers: list[int]
    checkpoints: list[dict[str, Any]]

@dataclass
class ContinuationPlan:
    items: list[dict[str, Any]]
    multi_island: dict[str, Any]
    use_multi_island: bool

@dataclass
class CheckpointHandoff:
    all_viable: list[dict[str, Any]]
    handoff: list[dict[str, Any]]

@dataclass(frozen=True)
class CheckpointActions:
    discover_outer_dirs: Callable[..., Any]
    select_checkpoint_candidates: Callable[..., Any]
    rescore_rank1: Callable[..., Any]
    checkpoint_multi_island_policy: Callable[..., Any]
    run_candidates_batched_experimental: Callable[..., Any]
    run_candidate: Callable[..., Any]
    select_checkpoint_handoff_rows: Callable[..., Any]
