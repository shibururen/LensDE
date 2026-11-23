"""Public two-layer funnel entry and explicit production/historical material compatibility boundary."""
from __future__ import annotations
from lensde.seeds.legacy import (
    fixed_glass_feedback_candidate_key,
    fixed_glass_feedback_seed_contract,
)
from lensde.resources import ProjectPaths

PROJECT_ROOT = ProjectPaths().root
import argparse
from lensde.runtime import concurrency as concurrent
import copy
import csv
import hashlib
import json
import math
import os
import sys
from pathlib import Path
from typing import Any

SCRIPT_PATH = Path(__file__).resolve()
GUI_DIR = PROJECT_ROOT / "web"
CPP_DIR = PROJECT_ROOT / "native"
REPO_ROOT = PROJECT_ROOT
SCRIPTS_DIR = Path(__file__).resolve().parents[1] / "helpers"
from lensde.workflow import run_basin_dls_potential_sweep as dls_sweep
from lensde.workflow import run_checkpoint_de400_layer as checkpoint_de400
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from lensde.workflow import run_material5_bridge_selection as material_bridge
from lensde.workflow import run_material_chromatic_weak_geometry_stage as material_chromatic_repair
from lensde.workflow import run_post_de_basin_refine_funnel as material_funnel
from lensde.workflow import run_selected_basin_deep_refine as deep_refine
from lensde.workflow import select_basin_potential_candidates as potential_select
from lensde.engine import core as server
from .funnel.settings import (
    DEFAULT_MAX_LOCAL_ITER_PER_RESTART,
    DE_DLS_TRACE_COVERAGE_BASELINE,
    FINAL_DLS_CPU_CUDA_CONSTRAINT_ABS_TOL,
    FINAL_DLS_CPU_CUDA_CONTRACT,
    FINAL_DLS_CPU_CUDA_COVERAGE_ABS_TOL,
    FINAL_DLS_CPU_CUDA_EDGE_ABS_TOL,
    FINAL_DLS_CPU_CUDA_MERIT_REL_TOL,
    FINAL_DLS_CPU_CUDA_SPOT_REL_TOL,
    FINAL_LOCKED_GLASS_DLS_GEOMETRY_KINDS,
    FIXED_GLASS_SEED_POLICY_LEGACY,
    FIXED_GLASS_SEED_POLICY_NUMERIC,
    FIXED_GLASS_SEED_POLICY_STABLE,
    GLASS_SCOUT_CPU_CUDA_MERIT_REL_TOL,
    LOCKED_GLASS_MATERIAL_VARIABLE_KINDS,
    STAGE_BOUNDARY_COVERAGE_ABSOLUTE_TOLERANCE,
    STAGE_BOUNDARY_MODE_APERTURE_RELATIVE_TOLERANCE,
    STAGE_BOUNDARY_SOURCE_APERTURE_RELATIVE_TOLERANCE,
    TERMINAL_ZEMAX_VIGNETTING_CONTRACT,
)
from .funnel.contracts import MaterialPolicy, FunnelStages
from .funnel.calibration import (
    recalibrate_vignetting_source_rows as _impl_recalibrate_vignetting_source_rows,
)
from .funnel.calibration_policy import (
    _vignetting_ellipse_inside_unit_pupil,
    illumination_optimization_enabled,
    stage_boundary_vignetting_recalibration_enabled,
    terminal_zemax_vignetting_finalization_enabled,
    vignetting_calibration_row_indices,
    strict_adaptive_pilot_grid_system,
    calibrated_paraxial_optimizer_system,
    stage_boundary_aperture_stability,
    stage_boundary_trace_admissible,
    terminal_zemax_set_vignetting_command,
    materialized_stage_boundary_variables,
    adaptive_pilot_grid_optimizer_system as _impl_adaptive_pilot_grid_optimizer_system,
)
from .funnel.common import (
    read_json,
    write_json,
    finite_float,
    first_existing,
    ancestor_file_paths,
    context_paths,
    variable_list,
    read_csv,
    write_csv,
    parse_int_list,
    parse_float_list,
    fixed_glass_feedback_trace_sampling,
    prescout_material_trace_sampling,
    align_prescout_material_sampling,
    validated_local_iter,
    max_local_iter_per_restart,
    restart_iter_schedule,
    field_radius,
    cycle_iter,
    record_candidate_shortfall,
    _row_metric,
    _row_truthy,
)
from .funnel.feedback import run_refine_like_feedback as _impl_run_refine_like_feedback
from .funnel.feedback_batch import (
    fixed_glass_feedback_multi_island_policy as _impl_fixed_glass_feedback_multi_island_policy,
    run_prepared_refine_like_feedback_batched as _impl_run_prepared_refine_like_feedback_batched,
)
from .funnel.feedback_case import (
    finish_refine_like_feedback_case,
    run_prepared_refine_like_feedback_serial,
    prepare_refine_like_feedback_case as _impl_prepare_refine_like_feedback_case,
    run_refine_like_feedback_case as _impl_run_refine_like_feedback_case,
)
from .funnel.feedback_contracts import (
    glass_scout_source_system,
    materialize_glass_source_paths,
    set_command_arg,
    ensure_command_flag,
    add_refine_like_feedback_args,
    configure_refine_like_feedback_evaluator,
    inherit_refine_like_aiming_from_system,
    fixed_glass_feedback_admission,
    cpu_cuda_materialization_consistency,
    materialize_locked_glass_parent_snapshot,
    fixed_glass_parent_guard_row,
    select_fixed_glass_feedback_attempt,
    canonical_contract_fingerprint,
    ordered_compatibility_cohorts,
    score_refine_like_parent_on_locked_contract as _impl_score_refine_like_parent_on_locked_contract,
)
from .funnel.fixed_glass import (
    run_fixed_glass_feedback_repair as _impl_run_fixed_glass_feedback_repair,
)
from .funnel.glass_scout import (
    resolved_glass_scout_rounds,
    glass_scout_namespace,
    scout_to_locked_boundary_minima,
    glass_scout_variable_layout_signature,
    glass_scout_batch_policy,
    run_glass_choice_scout as _impl_run_glass_choice_scout,
)
from .funnel.locked_audit import (
    run_final_locked_glass_cpu_cuda_audit as _impl_run_final_locked_glass_cpu_cuda_audit,
    run_final_locked_glass_resume_cpu_audit as _impl_run_final_locked_glass_resume_cpu_audit,
)
from .funnel.locked_contracts import (
    final_locked_glass_dls_profiles,
    final_locked_glass_dls_geometry_variables,
    final_locked_glass_cpu_cuda_consistency,
    final_locked_glass_pass_advances,
    final_locked_glass_existing_pass_contract,
    locked_glass_final_dls_source_rows,
    final_locked_glass_dls_command as _impl_final_locked_glass_dls_command,
)
from .funnel.locked_glass_tail import (
    run_final_locked_glass_dls_tail as _impl_run_final_locked_glass_dls_tail,
)
from .funnel.material import (
    source_dirs_from_material_ranked,
    continuous_material_scout_sources,
    run_material_layer as _impl_run_material_layer,
)
from .funnel.reference import (
    three_field_system,
    geometry_variables,
    load_reference_context as _impl_load_reference_context,
    make_three_field_geometry_reference as _impl_make_three_field_geometry_reference,
)
from .funnel.restarts import append_restart_report, run_restart_layer
from .funnel.runner import run_once as _impl_run_once
from .funnel.selection import (
    topology_key,
    add_unique,
    select_candidates,
    material_entry_key,
    add_material_entry_rows,
    select_material_entry_rows,
)
from .funnel.terminal_vignetting import (
    finalize_terminal_zemax_vignetting_rows as _impl_finalize_terminal_zemax_vignetting_rows,
)
from .funnel.cli import parse_args as _parse_args
from .funnel.runner import resume_final_locked_glass_dls as _resume_final_locked_glass_dls


def _material_policy():
    # Resolve historical replacements here; importing these callbacks in a
    # responsibility module would retain the production version too early.
    return MaterialPolicy(
        cpp_dir=CPP_DIR,
        candidate_key=fixed_glass_feedback_candidate_key,
        seed_contract=fixed_glass_feedback_seed_contract,
        calibrated_paraxial_optimizer_system=calibrated_paraxial_optimizer_system,
        recalibrate_vignetting_source_rows=recalibrate_vignetting_source_rows,
        finalize_terminal_zemax_vignetting_rows=finalize_terminal_zemax_vignetting_rows,
        prepare_refine_like_feedback_case=prepare_refine_like_feedback_case,
        fixed_glass_feedback_multi_island_policy=fixed_glass_feedback_multi_island_policy,
        run_prepared_refine_like_feedback_batched=run_prepared_refine_like_feedback_batched,
        run_refine_like_feedback=run_refine_like_feedback,
        glass_scout_batch_policy=glass_scout_batch_policy,
        run_glass_choice_scout=run_glass_choice_scout,
        run_fixed_glass_feedback_repair=run_fixed_glass_feedback_repair,
    )


def _funnel_stages():
    # Stage names are selected per public invocation, independently of worker state.
    return FunnelStages(
        make_three_field_geometry_reference=make_three_field_geometry_reference,
        select_candidates=select_candidates,
        run_restart_layer=run_restart_layer,
        run_material_layer=run_material_layer,
        continuous_material_scout_sources=continuous_material_scout_sources,
        recalibrate_vignetting_source_rows=recalibrate_vignetting_source_rows,
        run_glass_choice_scout=run_glass_choice_scout,
        run_fixed_glass_feedback_repair=run_fixed_glass_feedback_repair,
        locked_glass_final_dls_source_rows=locked_glass_final_dls_source_rows,
        run_final_locked_glass_dls_tail=run_final_locked_glass_dls_tail,
        finalize_terminal_zemax_vignetting_rows=finalize_terminal_zemax_vignetting_rows,
    )


def load_reference_context(
    args: argparse.Namespace, basin_run_dir: Path
) -> tuple[list[str], dict[str, Any], Path]:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_load_reference_context(args, basin_run_dir, _version=_material_policy())


def make_three_field_geometry_reference(args: argparse.Namespace) -> Path:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_make_three_field_geometry_reference(args, _version=_material_policy())


def run_material_layer(
    args: argparse.Namespace, latest_rows: list[dict[str, Any]]
) -> tuple[Path | None, list[dict[str, Any]]]:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_run_material_layer(args, latest_rows, _version=_material_policy())


def adaptive_pilot_grid_optimizer_system(
    exact_system: dict[str, Any], source_system: dict[str, Any]
) -> dict[str, Any]:
    """Transfer measured coefficients to the source aperture and select APG."""
    return _impl_adaptive_pilot_grid_optimizer_system(
        exact_system, source_system, _version=_material_policy()
    )


def recalibrate_vignetting_source_rows(
    args: argparse.Namespace,
    source_rows: list[dict[str, Any]],
    *,
    stage_name: str,
    sampling: dict[str, Any],
    min_trace: float = DE_DLS_TRACE_COVERAGE_BASELINE,
    min_field: float = DE_DLS_TRACE_COVERAGE_BASELINE,
) -> list[dict[str, Any]]:
    """Measure fixed apertures, then hand off a complete dynamic-4x8 CP score."""
    return _impl_recalibrate_vignetting_source_rows(
        args,
        source_rows,
        stage_name=stage_name,
        sampling=sampling,
        min_trace=min_trace,
        min_field=min_field,
        _version=_material_policy(),
    )


def finalize_terminal_zemax_vignetting_rows(
    args: argparse.Namespace,
    source_rows: list[dict[str, Any]],
    *,
    stage_name: str,
    sampling: dict[str, Any],
    min_trace: float = DE_DLS_TRACE_COVERAGE_BASELINE,
    min_field: float = DE_DLS_TRACE_COVERAGE_BASELINE,
) -> list[dict[str, Any]]:
    """Create terminal systems whose Set Vignetting contract matches Zemax.

    Intermediate handoffs use the same strict AdaptivePilotGrid coefficient
    measurement, followed by dynamic-4x8 optimizer handoff scoring. This
    terminal pass is deliberately separate because it publishes a strict APG
    system after the last geometry/asphere change.  The displayed DIAM/FLAP
    apertures are frozen only in the solver-local measurement copy; the
    delivered system is the source system with only the four field coefficients
    replaced.  Neither path uses an all-wave merge, unit-pupil ellipse
    projection, or calibrated-model contraction.  No optimizer consumes these
    rows afterwards, and candidate order remains the pre-calibration order."""
    return _impl_finalize_terminal_zemax_vignetting_rows(
        args,
        source_rows,
        stage_name=stage_name,
        sampling=sampling,
        min_trace=min_trace,
        min_field=min_field,
        _version=_material_policy(),
    )


def score_refine_like_parent_on_locked_contract(
    prepared: dict[str, Any], args: argparse.Namespace
) -> dict[str, Any]:
    """Score the exact DE row-0 parent with the locked 6x8 CPU contract.

    GlassChoice Scout and locked DE deliberately use different pupil sampling.
    Comparing the locked result with the Scout score therefore mixes two merit
    functions.  This one-row preflight supplies the true parent for elitist
    admission and costs one CPU evaluation per island."""
    return _impl_score_refine_like_parent_on_locked_contract(
        prepared, args, _version=_material_policy()
    )


def prepare_refine_like_feedback_case(
    *,
    source_rank: int,
    source_dir: Path,
    material_mode: str,
    algorithm: str,
    out_root: Path,
    glasses: list[dict[str, Any]],
    args: argparse.Namespace,
) -> dict[str, Any]:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_prepare_refine_like_feedback_case(
        source_rank=source_rank,
        source_dir=source_dir,
        material_mode=material_mode,
        algorithm=algorithm,
        out_root=out_root,
        glasses=glasses,
        args=args,
        _version=_material_policy(),
    )


def run_refine_like_feedback_case(
    *,
    source_rank: int,
    source_dir: Path,
    material_mode: str,
    algorithm: str,
    out_root: Path,
    glasses: list[dict[str, Any]],
    args: argparse.Namespace,
) -> dict[str, Any]:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_run_refine_like_feedback_case(
        source_rank=source_rank,
        source_dir=source_dir,
        material_mode=material_mode,
        algorithm=algorithm,
        out_root=out_root,
        glasses=glasses,
        args=args,
        _version=_material_policy(),
    )


def fixed_glass_feedback_multi_island_policy(
    args: argparse.Namespace, prepared: list[dict[str, Any]]
) -> dict[str, Any]:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_fixed_glass_feedback_multi_island_policy(
        args, prepared, _version=_material_policy()
    )


def run_prepared_refine_like_feedback_batched(
    prepared: list[dict[str, Any]],
    args: argparse.Namespace,
    policy: dict[str, Any],
    batch_dir: Path | None = None,
) -> list[dict[str, Any]]:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_run_prepared_refine_like_feedback_batched(
        prepared, args, policy, batch_dir, _version=_material_policy()
    )


def run_refine_like_feedback(
    args: argparse.Namespace, source_dirs: list[Path], feedback_dir: Path
) -> None:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_run_refine_like_feedback(
        args, source_dirs, feedback_dir, _version=_material_policy()
    )


def run_glass_choice_scout(
    args: argparse.Namespace, source_rows: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], Path]:
    """Run one short, batched GlassChoice wave and materialize its glass choices."""
    return _impl_run_glass_choice_scout(args, source_rows, _version=_material_policy())


def run_fixed_glass_feedback_repair(
    args: argparse.Namespace,
    material_dir: Path | None,
    direct_source_rows: list[dict[str, Any]] | None = None,
) -> Path | None:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_run_fixed_glass_feedback_repair(
        args, material_dir, direct_source_rows, _version=_material_policy()
    )


def final_locked_glass_dls_command(
    args: argparse.Namespace,
    *,
    source_dir: Path,
    system_path: Path,
    variables_path: Path,
    merit_path: Path,
    out_dir: Path,
    profile: dict[str, Any],
) -> list[str]:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_final_locked_glass_dls_command(
        args,
        source_dir=source_dir,
        system_path=system_path,
        variables_path=variables_path,
        merit_path=merit_path,
        out_dir=out_dir,
        profile=profile,
        _version=_material_policy(),
    )


def run_final_locked_glass_cpu_cuda_audit(
    *,
    pass_dir: Path,
    system: dict[str, Any],
    cpu_score: dict[str, Any],
    variables_doc: dict[str, Any],
    variables_path: Path,
    merit_path: Path,
    sampling: dict[str, Any],
    min_trace: float,
    min_field: float,
    timeout: int,
    child_workers: int,
    raise_on_inconsistent: bool = True,
) -> dict[str, Any]:
    """Resolve the current material version at this public compatibility boundary."""
    return _impl_run_final_locked_glass_cpu_cuda_audit(
        pass_dir=pass_dir,
        system=system,
        cpu_score=cpu_score,
        variables_doc=variables_doc,
        variables_path=variables_path,
        merit_path=merit_path,
        sampling=sampling,
        min_trace=min_trace,
        min_field=min_field,
        timeout=timeout,
        child_workers=child_workers,
        raise_on_inconsistent=raise_on_inconsistent,
        _version=_material_policy(),
    )


def run_final_locked_glass_resume_cpu_audit(
    *,
    pass_dir: Path,
    saved_score: dict[str, Any],
    variables_path: Path,
    merit_path: Path,
    sampling: dict[str, Any],
    min_trace: float,
    min_field: float,
    timeout: int,
    child_workers: int,
) -> dict[str, Any]:
    """Rescore a cached pass with the current CPU executable before reuse."""
    return _impl_run_final_locked_glass_resume_cpu_audit(
        pass_dir=pass_dir,
        saved_score=saved_score,
        variables_path=variables_path,
        merit_path=merit_path,
        sampling=sampling,
        min_trace=min_trace,
        min_field=min_field,
        timeout=timeout,
        child_workers=child_workers,
        _version=_material_policy(),
    )


def run_final_locked_glass_dls_tail(
    args: argparse.Namespace,
    feedback_dir: Path | None,
    source_rows_override: list[dict[str, Any]] | None = None,
) -> Path | None:
    """Run an independent CPU DLS tail after pure-DE locked-glass feedback."""
    return _impl_run_final_locked_glass_dls_tail(
        args, feedback_dir, source_rows_override, _version=_material_policy()
    )


def run_once(args: argparse.Namespace) -> Path:
    """Run the public funnel with the currently selected stage and material policies."""
    return _impl_run_once(args, _stages=_funnel_stages(), _version=_material_policy())


def main() -> int:
    """Preserve the original CLI identity, seed application and tail-only resume route."""
    args = _parse_args(cpp_dir=CPP_DIR)
    server.seed_protocol.apply_cli_args(args)
    if args.resume_final_locked_glass_dls_from is not None:
        return _resume_final_locked_glass_dls(
            args, _stages=_funnel_stages(), _version=_material_policy()
        )
    run_once(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
