"""Explicit material-bridge phase records and named injectable actions."""
from __future__ import annotations

from pathlib import Path
from typing import Any
from dataclasses import dataclass
from typing import Any, Callable


@dataclass
class BridgeInputs:
    bridge_dir: Path
    merit_path: Path
    merit: dict[str, Any]
    exe: Path
    report_path: Path

@dataclass
class BridgeWork:
    input_binding: dict[str, Any]
    diagnostics: list[dict[str, Any]]
    vignetting_bridge_reports: list[dict[str, Any]]
    multi_island_waves: list[dict[str, Any]]

@dataclass
class RepairWork:
    repair_summaries: list[dict[str, Any]]
    base_seed: int
    probe_limit: int
    repair_generations: int

@dataclass
class CommonCandidates:
    bridge_rows: list[dict[str, Any]]
    deepen_summaries: list[dict[str, Any]]

@dataclass
class DeepenSelection:
    selected: list[dict[str, Any]]
    deepen_target: int
    deepen_population: int
    deepen_scale: str

@dataclass(frozen=True)
class BridgeStageActions:
    cached_bridge_stage_is_usable: Callable[..., Any]
    bridge_stage_input_binding: Callable[..., Any]
    prepare_aperture_realized_bridge_source: Callable[..., Any]
    score_parent: Callable[..., Any]
    run_de_wave: Callable[..., Any]
    select_lanes: Callable[..., Any]

@dataclass(frozen=True)
class BridgeWaveActions:
    prepare_de_case: Callable[..., Any]
    material_bridge_multi_island_policy: Callable[..., Any]
    run_prepared_de_batched: Callable[..., Any]
    run_prepared_de_serial: Callable[..., Any]
