"""Explicit material-version hooks, workflow stage callbacks and immutable DLS worker inputs."""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


@dataclass(frozen=True)
class MaterialPolicy:
    """One invocation's explicitly selected production/historical material behavior."""

    cpp_dir: Path
    candidate_key: Callable[..., Any]
    seed_contract: Callable[..., Any]
    calibrated_paraxial_optimizer_system: Callable[..., Any]
    recalibrate_vignetting_source_rows: Callable[..., Any]
    finalize_terminal_zemax_vignetting_rows: Callable[..., Any]
    prepare_refine_like_feedback_case: Callable[..., Any]
    fixed_glass_feedback_multi_island_policy: Callable[..., Any]
    run_prepared_refine_like_feedback_batched: Callable[..., Any]
    run_refine_like_feedback: Callable[..., Any]
    glass_scout_batch_policy: Callable[..., Any]
    run_glass_choice_scout: Callable[..., Any]
    run_fixed_glass_feedback_repair: Callable[..., Any]


@dataclass(frozen=True)
class FunnelStages:
    """Named layer handoffs resolved at the compatibility entry on each call."""

    make_three_field_geometry_reference: Callable[..., Any]
    select_candidates: Callable[..., Any]
    run_restart_layer: Callable[..., Any]
    run_material_layer: Callable[..., Any]
    continuous_material_scout_sources: Callable[..., Any]
    recalibrate_vignetting_source_rows: Callable[..., Any]
    run_glass_choice_scout: Callable[..., Any]
    run_fixed_glass_feedback_repair: Callable[..., Any]
    locked_glass_final_dls_source_rows: Callable[..., Any]
    run_final_locked_glass_dls_tail: Callable[..., Any]
    finalize_terminal_zemax_vignetting_rows: Callable[..., Any]


@dataclass(frozen=True)
class LockedTailInputs:
    """Read-only outer inputs shared by source workers; pass decisions remain local."""

    args: Any
    sampling: dict[str, Any]
    out_dir: Path
    scoring_args: Any
    profiles: list[dict[str, Any]]
    min_trace: float
    min_field: float
    timeout: int
    child_workers: int
    boundary_recalibration_enabled: bool
