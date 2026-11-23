"""Finalize terminal Set Vignetting systems and persist their exact APG evidence."""
from __future__ import annotations
from typing import Any
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .contracts import MaterialPolicy
from pathlib import Path
from .settings import TERMINAL_ZEMAX_VIGNETTING_CONTRACT
import argparse
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .common import finite_float
from .common import read_json
from .common import record_candidate_shortfall
from lensde.engine import core as server
from .calibration_policy import vignetting_calibration_row_indices
from .common import write_csv
from .common import write_json
import copy
import hashlib
import json
from lensde.workflow import run_material5_bridge_selection as material_bridge
from .calibration_policy import strict_adaptive_pilot_grid_system
from .calibration_policy import terminal_zemax_set_vignetting_command
from .common import _row_metric


def finalize_terminal_zemax_vignetting_rows(
    args: argparse.Namespace,
    source_rows: list[dict[str, Any]],
    *,
    stage_name: str,
    sampling: dict[str, Any],
    min_trace: float = DE_DLS_TRACE_COVERAGE_BASELINE,
    min_field: float = DE_DLS_TRACE_COVERAGE_BASELINE,
    _version: MaterialPolicy,
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
    rows afterwards, and candidate order remains the pre-calibration order.
    """
    if not source_rows:
        return []
    active_positions = vignetting_calibration_row_indices(args, source_rows, terminal=True)
    if not active_positions:
        return list(source_rows)
    stage_root = server.native_io_paths.python_path(
        server.native_io_paths.named_path(args.out_dir.resolve(), stage_name)
    )
    stage_root.mkdir(parents=True, exist_ok=True)
    cpu_executable = Path(
        str(getattr(args, "exe", None) or _version.cpp_dir / "build" / "lensde_cpu.exe")
    ).resolve()
    if not cpu_executable.is_file():
        raise FileNotFoundError(cpu_executable)
    timeout = int(finite_float(getattr(args, "timeout", 0), 0.0) or 3600)
    child_workers = max(0, int(getattr(args, "child_cpu_workers", 0)))
    scoring_args = argparse.Namespace(
        inherit_source_merit=True, min_trace_coverage=min_trace, min_field_coverage=min_field
    )
    output_rows: list[dict[str, Any]] = []
    audit_rows: list[dict[str, Any]] = []
    for position, source_row in enumerate(source_rows, start=1):
        if position not in active_positions:
            output_rows.append(dict(source_row))
            continue
        (
            source_dir,
            source_rank,
            case_dir,
            solve_dir,
            final_dir,
            source_system,
            source_score,
            source_variables,
            stage_merit,
            materialized_aperture_count,
            command,
            summary_path,
            reusable,
        ) = prepare_terminal_vignetting_case(
            source_row, position, stage_root, sampling, cpu_executable, min_trace, min_field, args
        )
        if reusable:
            run_result = {"returncode": 0, "timed_out": False, "seconds": 0.0, "cache_hit": True}
            solved_system = read_json(final_dir / "system.json")
            exact_score = read_json(final_dir / "score.json")
            variables_doc = read_json(final_dir / "variables.json")
        else:
            write_json(case_dir / "command.json", command)
            run_result = feedback_cycles.run_command(
                command, _version.cpp_dir, case_dir / "run.log", timeout, child_workers
            )
            if int(finite_float(run_result.get("returncode"), -1)) != 0:
                raise RuntimeError(f"terminal Zemax-equivalent Set Vignetting failed: {case_dir}")
            solved_system, variables_doc = materialize_terminal_vignetting(
                solve_dir, source_system, source_variables, stage_merit, final_dir
            )
            (exact_score,) = score_terminal_vignetting(
                case_dir,
                cpu_executable,
                final_dir,
                sampling,
                timeout,
                child_workers,
                solved_system,
                stage_merit,
                variables_doc,
                _version=_version,
            )
        admit_terminal_vignetting(
            source_system,
            solved_system,
            exact_score,
            min_trace,
            min_field,
            source_row,
            source_rank,
            source_dir,
            source_score,
            run_result,
            final_dir,
            scoring_args,
            stage_merit,
            materialized_aperture_count,
            position,
            reusable,
            summary_path,
            audit_rows,
            output_rows,
        )
    write_csv(stage_root / "ranked.csv", output_rows)
    write_csv(
        stage_root / "rejected.csv", [row for row in audit_rows if not bool(row.get("passed"))]
    )
    write_json(
        stage_root / "report.json",
        {
            "contract": TERMINAL_ZEMAX_VIGNETTING_CONTRACT,
            "stage_name": stage_name,
            "terminal_only": True,
            "optimizer_hot_path_unchanged": True,
            "authoritative_final_system": True,
            "accepted_count": len(output_rows),
            "rejected_count": len(audit_rows) - len(output_rows),
            "rows": audit_rows,
        },
    )
    record_candidate_shortfall(args, stage_name, len(source_rows), output_rows)
    return output_rows


def prepare_terminal_vignetting_case(
    source_row, position, stage_root, sampling, cpu_executable, min_trace, min_field, args
):
    """Bind the terminal source and its input-sensitive Set Vignetting cache identity."""
    source_dir = Path(str(source_row.get("out_dir") or "")).resolve()
    if not source_dir.is_dir():
        raise FileNotFoundError(source_dir)
    source_rank = int(
        finite_float(source_row.get("rank"), finite_float(source_row.get("source_rank"), position))
    )
    case_dir = server.native_io_paths.python_path(
        server.native_io_paths.named_child(
            stage_root, f"cand_{position:02d}_rank_{source_rank:04d}"
        )
    )
    solve_dir = server.native_io_paths.named_child(case_dir, "set_vignetting")
    final_dir = case_dir / "final"
    source_system = read_json(feedback_cycles.file_from_source(source_dir, "system.json"))
    source_score = read_json(feedback_cycles.file_from_source(source_dir, "score.json"))
    source_merit = read_json(feedback_cycles.file_from_source(source_dir, "merit.json"))
    variables_payload = read_json(feedback_cycles.file_from_source(source_dir, "variables.json"))
    source_variables = list(variables_payload.get("variables") or [])
    stage_merit = copy.deepcopy(source_merit)
    stage_merit["num_rays"] = int(sampling["num_rays"])
    stage_merit["pupil_arms"] = int(sampling["pupil_arms"])
    stage_merit["pupil_distribution"] = str(sampling["pupil_distribution"])
    exact_system = strict_adaptive_pilot_grid_system(source_system)
    (
        measurement_system,
        materialized_aperture_count,
    ) = server.zemax_set_vignetting_hard_aperture_copy(exact_system)
    system_input_path = case_dir / "system_input.json"
    merit_path = case_dir / "merit.json"
    write_json(system_input_path, measurement_system)
    write_json(merit_path, stage_merit)
    command = terminal_zemax_set_vignetting_command(
        cpu_executable=cpu_executable,
        system_path=system_input_path,
        merit_path=merit_path,
        out_dir=solve_dir,
        sampling=sampling,
    )
    contract = {
        "contract": TERMINAL_ZEMAX_VIGNETTING_CONTRACT,
        "source": str(source_dir),
        "source_system_sha256": hashlib.sha256(
            json.dumps(source_system, sort_keys=True).encode("utf-8")
        ).hexdigest(),
        "measurement_system_sha256": hashlib.sha256(
            json.dumps(measurement_system, sort_keys=True).encode("utf-8")
        ).hexdigest(),
        "merit_sha256": hashlib.sha256(
            json.dumps(stage_merit, sort_keys=True).encode("utf-8")
        ).hexdigest(),
        "sampling": sampling,
        "minimum_trace_coverage": min_trace,
        "minimum_field_coverage": min_field,
        "measurement_wavelength": "primary",
        "measurement_aiming": "adaptive_pilot_grid_strict",
        "measurement_hard_aperture_semantics": "frozen_displayed_DIAM_FLAP",
        "delivered_aperture_semantics": "preserve_source_system",
        "delivered_system_update": "field_vignetting_coefficients_only",
        "all_wavelength_boundary_merge": False,
        "unit_pupil_ellipse_projection": False,
        "calibrated_aiming_contraction": False,
        "materialized_aperture_count": materialized_aperture_count,
        "excluded_surface_indices": server.protective_glass_surface_indices(source_system),
        "cpu_executable": material_bridge.executable_identity(cpu_executable),
    }
    contract_path = case_dir / "contract.json"
    summary_path = case_dir / "summary.json"
    reusable = (
        not bool(getattr(args, "force_rerun", False))
        and contract_path.is_file()
        and (read_json(contract_path) == contract)
        and (final_dir / "system.json").is_file()
        and (final_dir / "score.json").is_file()
        and (final_dir / "variables.json").is_file()
        and (final_dir / "checkpoint_system.zmx").is_file()
        and summary_path.is_file()
        and bool(read_json(summary_path).get("passed"))
    )
    write_json(contract_path, contract)
    return (
        source_dir,
        source_rank,
        case_dir,
        solve_dir,
        final_dir,
        source_system,
        source_score,
        source_variables,
        stage_merit,
        materialized_aperture_count,
        command,
        summary_path,
        reusable,
    )


def materialize_terminal_vignetting(
    solve_dir, source_system, source_variables, stage_merit, final_dir
):
    """Copy the solved pupil into the authoritative terminal system without optimizing it."""
    set_vignetting_system = read_json(solve_dir / "system.json")
    set_vignetting_score = read_json(solve_dir / "score.json")
    solved_system = server.vignetting_optimizer_aperture_copy(source_system)
    source_fields = list(solved_system.get("fields") or [])
    measured_fields = list(set_vignetting_system.get("fields") or [])
    if len(source_fields) != len(measured_fields):
        raise RuntimeError(
            f"terminal Set Vignetting changed the field count: {len(source_fields)} -> {len(measured_fields)}"
        )
    for target, measured in zip(source_fields, measured_fields, strict=True):
        for key in ("fvcx", "fvcy", "fvdx", "fvdy"):
            target[key] = finite_float(measured.get(key), 0.0)
    solved_system = strict_adaptive_pilot_grid_system(solved_system)
    variables = server.ensure_vignetting_variables_for_system(
        solved_system, source_variables, stage_merit, handoff=True
    )
    variables = server.sync_variables_from_system(variables, solved_system)
    variables_doc = {"variables": variables}
    final_dir.mkdir(parents=True, exist_ok=True)
    write_json(final_dir / "system.json", solved_system)
    write_json(final_dir / "checkpoint_system.json", solved_system)
    write_json(final_dir / "merit.json", stage_merit)
    write_json(final_dir / "variables.json", variables_doc)
    write_json(final_dir / "set_vignetting_system.json", set_vignetting_system)
    write_json(final_dir / "set_vignetting_score.json", set_vignetting_score)
    return (solved_system, variables_doc)


def score_terminal_vignetting(
    case_dir,
    cpu_executable,
    final_dir,
    sampling,
    timeout,
    child_workers,
    solved_system,
    stage_merit,
    variables_doc,
    *,
    _version: MaterialPolicy,
):
    """Evaluate the exact terminal APG system under its final sampling contract."""
    exact_score_dir = case_dir / "exact_apg_final_score"
    exact_score_command = [
        str(cpu_executable),
        "--system",
        str((final_dir / "system.json").resolve()),
        "--variables",
        str((final_dir / "variables.json").resolve()),
        "--merit",
        str((final_dir / "merit.json").resolve()),
        "--cpu",
        "--aiming",
        "adaptive_pilot_grid",
        "--aiming-iter",
        "40",
        "--aiming-continuation-steps",
        "16",
        "--aiming-require-convergence",
        "--rays",
        str(sampling["num_rays"]),
        "--arms",
        str(sampling["pupil_arms"]),
        "--distribution",
        str(sampling["pupil_distribution"]),
        "--out-dir",
        str(exact_score_dir.resolve()),
    ]
    write_json(case_dir / "exact_apg_final_command.json", exact_score_command)
    exact_score_result = feedback_cycles.run_command(
        exact_score_command,
        _version.cpp_dir,
        case_dir / "exact_apg_final_run.log",
        timeout,
        child_workers,
    )
    if int(finite_float(exact_score_result.get("returncode"), -1)) != 0:
        raise RuntimeError(f"terminal exact APG final score failed: {case_dir}")
    exact_score = read_json(exact_score_dir / "score.json")
    write_json(final_dir / "score.json", exact_score)
    server.export_zemax_zmx(
        {
            "system": solved_system,
            "merit": stage_merit,
            "variables": variables_doc["variables"],
            "options": {},
        },
        final_dir / "checkpoint_system.zmx",
    )
    return (exact_score,)


def admit_terminal_vignetting(
    source_system,
    solved_system,
    exact_score,
    min_trace,
    min_field,
    source_row,
    source_rank,
    source_dir,
    source_score,
    run_result,
    final_dir,
    scoring_args,
    stage_merit,
    materialized_aperture_count,
    position,
    reusable,
    summary_path,
    audit_rows,
    output_rows,
):
    """Record terminal coverage, aperture evidence and the admitted final row."""
    coefficient_delta = 0.0
    for source_field, solved_field in zip(
        source_system.get("fields", []) or [], solved_system.get("fields", []) or []
    ):
        for key in ("fvcx", "fvcy", "fvdx", "fvdy"):
            coefficient_delta = max(
                coefficient_delta,
                abs(
                    finite_float(solved_field.get(key), 0.0)
                    - finite_float(source_field.get(key), 0.0)
                ),
            )
    stage_ok = (
        bool(exact_score.get("trace_ok"))
        and finite_float(exact_score.get("trace_coverage"), 0.0) >= min_trace
        and (finite_float(exact_score.get("min_field_coverage"), 0.0) >= min_field)
        and server.vignetting_handoff_policy.chief_ray_usable(exact_score)
    )
    row = feedback_cycles.score_to_row(
        cycle=int(finite_float(source_row.get("cycle"), 0)),
        source_rank=source_rank,
        source_dir=source_dir,
        source_name=source_dir.name,
        material_mode="locked_glass",
        algorithm="terminal_zemax_set_vignetting",
        parent_score=source_score,
        score=exact_score,
        system=solved_system,
        run_result=run_result,
        out_dir=final_dir,
        args=scoring_args,
        merit=stage_merit,
    )
    row.update(
        {
            "terminal_zemax_vignetting_finalized": True,
            "terminal_zemax_vignetting_passed": stage_ok,
            "optimization_out_dir": str(source_dir),
            "optimization_merit": _row_metric(
                source_row,
                "pre_vignetting_calibration_merit",
                "raw_dls_merit",
                "final_merit",
                "merit",
            ),
            "max_vignetting_coefficient_change": coefficient_delta,
            "materialized_aperture_count": materialized_aperture_count,
            "excluded_surface_indices": server.protective_glass_surface_indices(source_system),
            "measurement_aiming": "adaptive_pilot_grid_strict",
            "measurement_wavelength": "primary",
            "zmx_path": str(final_dir / "checkpoint_system.zmx"),
        }
    )
    audit = {
        "position": position,
        "source_rank": source_rank,
        "source_dir": str(source_dir),
        "final_dir": str(final_dir),
        "cache_hit": reusable,
        "merit": exact_score.get("merit"),
        "trace_ok": exact_score.get("trace_ok"),
        "trace_coverage": exact_score.get("trace_coverage"),
        "min_field_coverage": exact_score.get("min_field_coverage"),
        "min_chief_progress": exact_score.get("min_chief_progress"),
        "max_vignetting_coefficient_change": coefficient_delta,
        "passed": stage_ok,
    }
    write_json(summary_path, audit)
    audit_rows.append(audit)
    if stage_ok:
        output_rows.append(row)
