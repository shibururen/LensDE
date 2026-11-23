"""Common-contract bridge between sparse geometry DLS and the material funnel.

With active vignetting, the bridge performs one fixed-aperture strict APG
calibration and strict APG/CP aperture-updating retraces, then sends every
accepted handoff directly to the common deepen stage.  The historical dual-
aperture/constraint-shock diagnostic remains only as the non-vignetted fallback.


The public entry retains historical strategy globals; implementation lives in material_bridge.
"""

from __future__ import annotations

from lensde.resources import ProjectPaths

PROJECT_ROOT = ProjectPaths().root

import argparse

import copy

import csv

import hashlib

import json

import math

import os

import shutil

from lensde.runtime import processes as subprocess

import sys

import time

from pathlib import Path

from typing import Any

SCRIPT_PATH = Path(__file__).resolve()

GUI_DIR = PROJECT_ROOT / "web"

CPP_DIR = PROJECT_ROOT / "native"

SCRIPTS_DIR = Path(__file__).resolve().parents[1] / "helpers"

from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles

from lensde.engine import core as server

from lensde.workflow import selection as strict_topology

from .material_bridge.policy import (
    CONTINUOUS_MATERIAL_KINDS,
    MATERIAL_BRIDGE_VIGNETTING_CONTRACT,
    MATERIAL_BRIDGE_SCORE_CONTRACT,
    MATERIAL_BRIDGE_STAGE_CONTRACT,
    MATERIAL_BRIDGE_SELECTION_MERIT_CONTRACT,
    MATERIAL_BRIDGE_CPU_CUDA_MERIT_REL_TOL,
    MATERIAL_BRIDGE_CPU_CUDA_COVERAGE_TOL,
    DE_DLS_TRACE_COVERAGE_BASELINE,
    MATERIAL_BRIDGE_CPU_CUDA_EDGE_TOL,
)

from .material_bridge.files import (
    read_json,
    write_json,
    write_csv,
    finite_float,
    csv_bool,
    parse_float_list,
    parse_int_list,
    source_file,
)

from .material_bridge.contracts import (
    continuous_material_variable_contract,
    assert_continuous_material_variables_preserved,
    field_nodes,
    common_fields,
    fields_on_exact_nodes,
    field_contract_matches,
    minimum_relative_illumination,
    vignetting_bridge_missing_pupil_samples,
    system_aiming_mode,
    system_stop_radius_source,
    system_aiming_policy,
    minimum_trace_coverage,
    trace_acceptable,
)

from .material_bridge.cache import (
    bridge_content_binding,
    vignetting_bridge_input_binding,
    cached_vignetting_bridge_is_usable,
    score_population_complete,
    executable_identity,
    bridge_score_fingerprint,
    execute_score_cached,
    bridge_stage_input_binding,
    cached_bridge_stage_is_usable,
)

from .material_bridge.calibration import (
    prepare_aperture_realized_bridge_source,
)

from .material_bridge.inputs import (
    bridge_executable,
    prepare_case_inputs,
    read_population,
    retained_population,
    prepare_de_case,
    anchored_variables_doc,
)

from .material_bridge.commands import (
    base_trace_command,
    cpu_score_command,
    serial_de_command,
)

from .material_bridge.scoring import (
    score_population_row,
    cpu_cuda_score_consistency,
    score_parent,
)

from .material_bridge.execution import (
    execute,
    de_result_from_score,
    multi_island_materialization_consistent,
    run_prepared_de_serial,
    run_de,
    normalized_internal_scaling,
    multi_island_layout_signature,
)

from .material_bridge.broker import (
    material_bridge_multi_island_policy,
    publish_materialized_multi_island_result,
    run_prepared_de_batched,
)

from .material_bridge.continuation import (
    run_de_wave,
    repair_assessment,
)

from .material_bridge.diagnostics import (
    metric_columns,
    classify_diagnostic,
    apply_effective_score,
    write_bridge_basin_meta,
    normalized_entry_row,
    bridge_row_identity,
    propagate_bridge_row_update,
)

from .material_bridge.ranking import (
    topology_key,
    material_topology_family_key,
    effective_key,
    selection_reference_merit,
    bridge_potential,
    bridge_selection_key,
    potential_lane_rows,
    quality_potential_pareto_order,
)

from .material_bridge.selection import (
    lane_quotas,
    legacy_lane_override_enabled,
    pure_merit_entry_enabled,
    add_lane,
    select_lanes,
)

from .material_bridge import runner as _runner

from .material_bridge import continuation as _continuation

from .material_bridge.state import BridgeStageActions, BridgeWaveActions

def _bridge_actions() -> BridgeStageActions:
    """Resolve stage strategy hooks at the compatible public operation boundary."""
    return BridgeStageActions(
        cached_bridge_stage_is_usable,
        bridge_stage_input_binding,
        prepare_aperture_realized_bridge_source,
        score_parent,
        run_de_wave,
        select_lanes,
    )

def run_de_wave(jobs: list[dict[str, Any]], *, exe: Path, args: argparse.Namespace, wave_name: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Resolve the paper-version batched hook when this continuation wave starts."""
    actions = BridgeWaveActions(prepare_de_case, material_bridge_multi_island_policy, run_prepared_de_batched, run_prepared_de_serial)
    return _continuation.run_de_wave(jobs, exe=exe, args=args, wave_name=wave_name, actions=actions)

def run_bridge(args: argparse.Namespace, latest_rows: list[dict[str, Any]], *, material_context_run: Path) -> tuple[list[dict[str, Any]], Path]:
    """Run bridge phases with the current public strategy bindings."""
    return _runner.run_bridge(args, latest_rows, material_context_run=material_context_run, actions=_bridge_actions())
