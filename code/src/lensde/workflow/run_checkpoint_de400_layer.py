"""Checkpoint selection and P/G continuation layer for the production funnel.

The Global Explore Outers still own basin recording, active-basin mating and
cross-Outer basin avoidance.  This module starts only after those Outers have
finished: it selects process checkpoints, continues the selected checkpoints
and each Outer rank-1 state with basin-free DE, then returns a merit/potential/
diversity handoff using the row contract consumed by the material funnel.


The public entry retains historical strategy globals; implementation lives in checkpoint.
"""

from __future__ import annotations

from lensde.resources import ProjectPaths

PROJECT_ROOT = ProjectPaths().root

import copy

import csv

import hashlib

import json

import math

import os

import struct

from lensde.runtime import processes as subprocess

import time

from collections import defaultdict

from pathlib import Path

from typing import Any, Iterable

import numpy as np

SCRIPT_PATH = Path(__file__).resolve()

GUI_DIR = PROJECT_ROOT / "web"

CPP_DIR = PROJECT_ROOT / "native"

SCRIPTS_DIR = Path(__file__).resolve().parents[1] / "helpers"

import sys

from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles

from lensde.helpers import run_single_parent_local_population_ablation as local_population

from lensde.engine import core as server

from lensde.workflow import selection as strict_topology

from .checkpoint.policy import (
    QUALITY_CAP,
    CORE_QUALITY_CAP,
    BASIN_FILL_QUALITY_CAP,
    FINAL_RANK1_CORE_QUALITY_CAP,
    FINAL_RANK1_POTENTIAL_QUALITY_CAP,
    FINAL_RANK1_POTENTIAL_MIN_IMPROVEMENT,
    CHECKPOINT_TOTAL_COUNT,
    RANK1_ANCHOR_LIMIT,
    FULL_DISTANCE_FLOOR,
    SKELETON_DISTANCE_FLOOR,
    ASPHERE_DISTANCE_FLOOR,
    CHECKPOINT_DISTANCE_RELAXATION_TIERS,
    CHECKPOINT_MERIT_RELAXATION_CAPS,
    OUTER_SHORTLIST_LIMIT,
    OUTER_PREFILTER_LIMIT,
    GLOBAL_OUTER_FLOOR,
    GLOBAL_KEY_SEAT_LIMIT,
    CHECKPOINT_TOPOLOGY_REPLACEMENT_LIMIT,
    CHECKPOINT_TOPOLOGY_POWER_DISTANCE_FLOOR,
    CHECKPOINT_TOPOLOGY_EFFECTIVE_DISTANCE_FLOOR,
    ASPHERE_KINDS,
    SKELETON_KINDS,
    CONTINUOUS_MATERIAL_KINDS,
    CHECKPOINT_MULTI_ISLAND_VALIDATED_CONTRACT,
    CHECKPOINT_MULTI_ISLAND_RUNTIME_INPUTS,
    CHECKPOINT_SCORE_CONTRACT,
    MATERIALIZATION_TOLERANCE,
)

from .checkpoint.files import (
    read_json,
    write_json,
    read_jsonl,
    read_csv,
    write_csv,
    finite,
    parse_values,
    value_hash,
    flag_value,
    load_command,
)

from .checkpoint.contracts import (
    materialization_contract,
    continuous_material_variable_contract,
    assert_continuous_material_variables_preserved,
    command_materialization_trace_epsilon,
    checkpoint_adaptive_trace_contract,
    append_checkpoint_adaptive_trace_contract,
)

from .checkpoint.snapshots import (
    discover_outer_dirs,
    candidate_stage_sources,
    annotate_stage_rows,
    event_rows_for_outer,
    _event_rows_for_stage,
    basin_rows_for_outer,
    _basin_rows_for_stage,
    final_context,
    valid_nonrank,
)

from .checkpoint.geometry import (
    variable_bounds,
    normalized_geometry_matrix,
    rms_distance_matrix,
    DistanceIndex,
    CheckpointStructureIndex,
    asphere_keys,
    topology_statistics,
    hierarchy_keys,
    checkpoint_topology_identity,
    checkpoint_topology_shape_layout,
    checkpoint_power_parameter_distance,
    key_novelty,
)

from .checkpoint.selection import (
    potential_score,
    build_outer_pool,
    select_checkpoint_candidates,
    select_rank1_anchor_outers,
)

from .checkpoint.diversity import (
    greedy_select,
    diversify_checkpoint_topology_layouts,
)

from .checkpoint.commands import (
    variables_for_anchor,
    remove_flag,
    append_glass_contract,
    build_de_command,
    build_score_command,
)

from .checkpoint.execution import (
    execute_plain,
    execute,
    run_candidate,
    finalize_candidate_result,
)

from .checkpoint.rescoring import (
    rescore_rank1,
)

from .checkpoint.manifest import (
    snapshot_merits,
    candidate_id,
    checkpoint_state_identity,
    make_run_items,
)

from .checkpoint.broker import (
    run_candidates_batched_experimental,
    checkpoint_multi_island_policy,
)

from .checkpoint.handoff import (
    material_row,
    checkpoint_handoff_identity,
    checkpoint_handoff_hierarchy,
    checkpoint_handoff_topology_identity,
    checkpoint_handoff_novelty,
    select_checkpoint_handoff_rows,
)

from .checkpoint import runner as _runner

from .checkpoint.state import CheckpointActions

def _checkpoint_actions() -> CheckpointActions:
    """Resolve replaceable entry hooks when this operation starts."""
    return CheckpointActions(
        discover_outer_dirs,
        select_checkpoint_candidates,
        rescore_rank1,
        checkpoint_multi_island_policy,
        run_candidates_batched_experimental,
        run_candidate,
        select_checkpoint_handoff_rows,
    )

def run_once(args: Any) -> list[dict[str, Any]]:
    """Run checkpoint phases with the current public strategy bindings."""
    return _runner.run_once(args, actions=_checkpoint_actions())
