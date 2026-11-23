"""Run independent locked-glass DLS sources with explicit worker inputs and rollback state."""
from __future__ import annotations
from typing import Any
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .settings import FINAL_DLS_CPU_CUDA_CONTRACT
from .contracts import LockedTailInputs
from .contracts import MaterialPolicy
from pathlib import Path
import argparse
from lensde.runtime import concurrency as concurrent
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .locked_contracts import final_locked_glass_dls_profiles
from .common import finite_float
from .common import fixed_glass_feedback_trace_sampling
from .locked_contracts import locked_glass_final_dls_source_rows
import os
from lensde.engine import core as server
from .calibration_policy import stage_boundary_vignetting_recalibration_enabled
from .common import write_csv
from .common import write_json
import copy
from .locked_contracts import final_locked_glass_pass_advances
from .common import read_json
from .locked_audit import run_final_locked_glass_cpu_cuda_audit
from .locked_audit import run_final_locked_glass_resume_cpu_audit
from .locked_contracts import final_locked_glass_dls_command
from .locked_contracts import final_locked_glass_dls_geometry_variables
from .locked_contracts import final_locked_glass_existing_pass_contract
from .common import _row_truthy
import math


def run_final_locked_glass_dls_tail(
    args: argparse.Namespace,
    feedback_dir: Path | None,
    source_rows_override: list[dict[str, Any]] | None = None,
    *,
    _version: MaterialPolicy,
) -> Path | None:
    """Run an independent CPU DLS tail after pure-DE locked-glass feedback."""
    if feedback_dir is None or not bool(getattr(args, "final_locked_glass_dls_enabled", True)):
        return None
    min_trace = float(
        getattr(args, "final_locked_glass_dls_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
    )
    min_field = float(
        getattr(args, "final_locked_glass_dls_min_field_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
    )
    source_rows = locked_glass_final_dls_source_rows(args, feedback_dir, source_rows_override)
    if not source_rows:
        raise RuntimeError("locked-glass DLS tail received an empty feedback ranking")
    out_dir = server.native_io_paths.named_child(
        args.out_dir.resolve(), "layer2_final_locked_glass_dls"
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    profiles = final_locked_glass_dls_profiles(args)
    sampling = fixed_glass_feedback_trace_sampling(args)
    boundary_recalibration_enabled = stage_boundary_vignetting_recalibration_enabled(args)
    scoring_args = argparse.Namespace(
        inherit_source_merit=True, min_trace_coverage=min_trace, min_field_coverage=min_field
    )
    jobs = max(1, int(getattr(args, "final_locked_glass_dls_jobs", 2)))
    requested_child_workers = max(
        0, int(getattr(args, "final_locked_glass_dls_child_cpu_workers", 0))
    )
    child_workers = requested_child_workers or max(1, (os.cpu_count() or 1) // jobs)
    timeout = int(
        finite_float(getattr(args, "final_locked_glass_dls_timeout", 0), 0.0)
        or finite_float(getattr(args, "timeout", 0), 0.0)
        or 3600
    )
    # Retain the original closure's object references and capture order. Mutable
    # pass decisions belong to each source worker rather than to this record.
    source_inputs = LockedTailInputs(
        args,
        sampling,
        out_dir,
        scoring_args,
        profiles,
        min_trace,
        min_field,
        timeout,
        child_workers,
        boundary_recalibration_enabled,
    )
    selected_rows: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {
            pool.submit(
                run_locked_glass_source, position, row, inputs=source_inputs, _version=_version
            ): position
            for position, row in enumerate(source_rows, start=1)
        }
        for future in concurrent.futures.as_completed(futures):
            selected_rows.append(future.result())
    ranked = feedback_cycles.sort_rows(selected_rows, scoring_args)
    write_csv(out_dir / "final_locked_glass_dls_ranked.csv", ranked)
    write_json(out_dir / "final_locked_glass_dls_ranked.json", {"rows": ranked})
    write_json(
        out_dir / "final_locked_glass_dls_config.json",
        {
            "enabled": True,
            "source_feedback_dir": str(feedback_dir),
            "source_count": len(source_rows),
            "profiles": profiles,
            "trace_sampling": sampling,
            "field_hy_fractions": feedback_cycles.parse_field_hy_fractions(
                str(getattr(args, "fixed_glass_feedback_field_hy_fractions", "") or "")
            ),
            "sampling_source": "fixed_glass_feedback",
            "vignetting_optimization_frozen": True,
            "clear_aperture_update_within_dls_block": False,
            "post_calibration_retrace_aperture_policy": "fixed_non_stop_apertures",
            "stage_boundary_vignetting_recalibration_enabled": boundary_recalibration_enabled,
            "selection_uses_recalibrated_state": boundary_recalibration_enabled,
            "parent_retention": "select and chain only stage-boundary-recalibrated, CPU/CUDA-consistent, 0.95-admissible, lower-merit DLS output",
            "cpu_cuda_audit_contract": FINAL_DLS_CPU_CUDA_CONTRACT,
            "jobs": jobs,
            "child_cpu_workers": child_workers,
        },
    )
    return out_dir


def run_locked_glass_source(
    position: int, source_row: dict[str, Any], *, inputs: LockedTailInputs, _version: MaterialPolicy
) -> dict[str, Any]:
    source_dir = Path(str(source_row.get("out_dir") or "")).resolve()
    source_system_path = feedback_cycles.file_from_source(source_dir, "system.json")
    source_score = read_json(feedback_cycles.file_from_source(source_dir, "score.json"))
    source_system = read_json(source_system_path)
    field_grid = str(getattr(inputs.args, "fixed_glass_feedback_field_hy_fractions", "") or "")
    current_system = feedback_cycles.with_field_hy_fractions(source_system, field_grid)
    current_score = source_score
    merit = copy.deepcopy(read_json(feedback_cycles.file_from_source(source_dir, "merit.json")))
    merit.pop("glass_proximity", None)
    merit["num_rays"] = inputs.sampling["num_rays"]
    server.dynamic_aperture_contract.apply_merit(merit)
    merit["pupil_arms"] = inputs.sampling["pupil_arms"]
    merit["pupil_distribution"] = inputs.sampling["pupil_distribution"]
    source_rank = int(finite_float(source_row.get("source_rank"), position))
    case_dir = server.native_io_paths.named_child(inputs.out_dir, f"source_{source_rank:02d}")
    case_dir.mkdir(parents=True, exist_ok=True)
    parent_result = {"returncode": 0, "timed_out": False, "seconds": 0.0}
    parent = feedback_cycles.score_to_row(
        cycle=0,
        source_rank=source_rank,
        source_dir=source_dir,
        source_name=source_dir.name,
        material_mode="locked_glass",
        algorithm="parent_fallback",
        parent_score=source_score,
        score=source_score,
        system=source_system,
        run_result=parent_result,
        out_dir=source_dir,
        args=inputs.scoring_args,
        merit=merit,
    )
    parent["dls_pass"] = 0
    parent["selected_from"] = "locked_glass_feedback_parent"
    pass_rows: list[dict[str, Any]] = []
    for pass_index, profile in enumerate(inputs.profiles, start=1):
        # A cached directory is only a candidate until current executable audits
        # agree. A rejected pass must not replace this source's current parent.
        (
            pass_dir,
            system_path,
            variables_path,
            merit_path,
            variables_doc,
            command,
            score_path,
            system_out_path,
            resume_contract,
            reuse_existing,
            resume_cpu_audit,
        ) = prepare_locked_dls_pass(
            case_dir,
            pass_index,
            current_system,
            source_dir,
            inputs,
            profile,
            merit,
            _version=_version,
        )
        if reuse_existing:
            saved_score = read_json(score_path)
            resume_cpu_audit = run_final_locked_glass_resume_cpu_audit(
                pass_dir=pass_dir,
                saved_score=saved_score,
                variables_path=variables_path,
                merit_path=merit_path,
                sampling=inputs.sampling,
                min_trace=inputs.min_trace,
                min_field=inputs.min_field,
                timeout=inputs.timeout,
                child_workers=inputs.child_workers,
                _version=_version,
            )
            reuse_existing = bool(resume_cpu_audit.get("consistent"))
        if reuse_existing and (not inputs.boundary_recalibration_enabled):
            system_out = read_json(system_out_path)
            score = read_json(score_path)
            try:
                consistency = run_final_locked_glass_cpu_cuda_audit(
                    pass_dir=pass_dir,
                    system=system_out,
                    cpu_score=score,
                    variables_doc=variables_doc,
                    variables_path=variables_path,
                    merit_path=merit_path,
                    sampling=inputs.sampling,
                    min_trace=inputs.min_trace,
                    min_field=inputs.min_field,
                    timeout=inputs.timeout,
                    child_workers=inputs.child_workers,
                    _version=_version,
                )
            except RuntimeError:
                reuse_existing = False
        if reuse_existing:
            system_out = read_json(system_out_path)
            score = read_json(score_path)
            run_result = {
                "returncode": 0,
                "timed_out": False,
                "seconds": 0.0,
                "reused_existing": True,
            }
        else:
            write_json(system_path, current_system)
            write_json(variables_path, variables_doc)
            write_json(merit_path, merit)
            write_json(pass_dir / "command.json", command)
            run_result = feedback_cycles.run_command(
                command,
                _version.cpp_dir,
                pass_dir / "run.log",
                inputs.timeout,
                inputs.child_workers,
            )
            score = read_json(score_path) if score_path.is_file() else {}
            system_out = read_json(system_out_path) if system_out_path.is_file() else current_system
        row, candidate_system, candidate_score = describe_locked_dls_candidate(
            pass_index,
            source_rank,
            source_dir,
            source_score,
            score,
            system_out,
            run_result,
            pass_dir,
            inputs,
            merit,
            reuse_existing,
            resume_contract,
            resume_cpu_audit,
        )
        if (
            int(finite_float(run_result.get("returncode"), -1)) == 0
            and score_path.is_file()
            and system_out_path.is_file()
        ):
            if not inputs.boundary_recalibration_enabled and (not reuse_existing):
                consistency = run_final_locked_glass_cpu_cuda_audit(
                    pass_dir=pass_dir,
                    system=system_out,
                    cpu_score=score,
                    variables_doc=variables_doc,
                    variables_path=variables_path,
                    merit_path=merit_path,
                    sampling=inputs.sampling,
                    min_trace=inputs.min_trace,
                    min_field=inputs.min_field,
                    timeout=inputs.timeout,
                    child_workers=inputs.child_workers,
                    _version=_version,
                )
            if inputs.boundary_recalibration_enabled:
                row["raw_cpu_cuda_audit_skipped"] = True
                row[
                    "raw_cpu_cuda_audit_skip_reason"
                ] = "authoritative_audit_runs_after_stage_boundary_recalibration"
            else:
                row["cpu_cuda_consistent"] = bool(consistency["consistent"])
                row["cpu_cuda_merit_relative_difference"] = consistency["merit_relative_difference"]
                row["cpu_cuda_constraint_penalty_absolute_difference"] = consistency[
                    "constraint_penalty_absolute_difference"
                ]
        else:
            write_json(
                pass_dir / "cpu_cuda_audit" / "report.json",
                {
                    "contract": FINAL_DLS_CPU_CUDA_CONTRACT,
                    "consistent": False,
                    "reasons": ["cpu_dls_stage_failed"],
                    "run_result": run_result,
                },
            )
        if (
            inputs.boundary_recalibration_enabled
            and int(finite_float(run_result.get("returncode"), -1)) == 0
            and score_path.is_file()
            and system_out_path.is_file()
        ):
            raw_row = dict(row)
            raw_row["basin_rank"] = int(finite_float(source_row.get("basin_rank"), source_rank))
            try:
                row = _version.recalibrate_vignetting_source_rows(
                    inputs.args,
                    [raw_row],
                    stage_name=f"layer2_final_locked_glass_dls_vignetting/source_{position:02d}_rank_{source_rank:02d}/pass_{pass_index:02d}",
                    sampling=inputs.sampling,
                    min_trace=inputs.min_trace,
                    min_field=inputs.min_field,
                )[0]
                row["dls_pass"] = pass_index
                row["selected_from"] = f"dls_pass_{pass_index:02d}"
                row["raw_dls_out_dir"] = str(pass_dir)
                row["raw_dls_merit"] = raw_row.get("merit")
                row["stage_boundary_vignetting_passed"] = True
                row["vignetting_optimization_frozen"] = True
                candidate_dir = Path(str(row["out_dir"])).resolve()
                candidate_system = read_json(candidate_dir / "system.json")
                candidate_score = read_json(candidate_dir / "score.json")
                consistency = run_final_locked_glass_cpu_cuda_audit(
                    pass_dir=candidate_dir,
                    system=candidate_system,
                    cpu_score=candidate_score,
                    variables_doc=variables_doc,
                    variables_path=variables_path,
                    merit_path=merit_path,
                    sampling=inputs.sampling,
                    min_trace=inputs.min_trace,
                    min_field=inputs.min_field,
                    timeout=inputs.timeout,
                    child_workers=inputs.child_workers,
                    raise_on_inconsistent=False,
                    _version=_version,
                )
                row["cpu_cuda_consistent"] = bool(consistency.get("consistent"))
                row["cpu_cuda_audit_performed"] = True
                row["cpu_cuda_merit_relative_difference"] = consistency.get(
                    "merit_relative_difference"
                )
                row["cpu_cuda_constraint_penalty_absolute_difference"] = consistency.get(
                    "constraint_penalty_absolute_difference"
                )
                boundary_summary_path = candidate_dir.parent / "summary.json"
                if boundary_summary_path.is_file():
                    boundary_summary = read_json(boundary_summary_path)
                    boundary_summary["cpu_cuda_audit_performed"] = True
                    boundary_summary["cpu_cuda_consistent"] = bool(consistency.get("consistent"))
                    boundary_summary["cpu_cuda_audit_report"] = str(
                        candidate_dir / "cpu_cuda_audit" / "report.json"
                    )
                    write_json(boundary_summary_path, boundary_summary)
            except RuntimeError as error:
                row = raw_row
                row["stage_boundary_vignetting_passed"] = False
                row["vignetting_recalibration_error"] = str(error)
                row["cpu_cuda_consistent"] = False
        elif inputs.boundary_recalibration_enabled:
            row["stage_boundary_vignetting_passed"] = False
        pass_rows.append(row)
        if (
            int(finite_float(run_result.get("returncode"), -1)) != 0
            or not system_out_path.is_file()
        ):
            continue
        advances_state = final_locked_glass_pass_advances(
            row, current_score, min_trace=inputs.min_trace, min_field=inputs.min_field
        )
        row["advanced_to_next_pass"] = advances_state
        if advances_state:
            current_system = candidate_system
            current_score = candidate_score
    return select_locked_dls_source_result(
        pass_rows, inputs, parent, position, source_row, case_dir
    )


def prepare_locked_dls_pass(
    case_dir,
    pass_index,
    current_system,
    source_dir,
    inputs,
    profile,
    merit,
    *,
    _version: MaterialPolicy,
):
    """Build one pass input and compare every saved input before considering reuse."""
    pass_dir = case_dir / f"pass_{pass_index:02d}"
    pass_dir.mkdir(parents=True, exist_ok=True)
    system_path = pass_dir / "system_input.json"
    variables_path = pass_dir / "variables.json"
    merit_path = pass_dir / "merit.json"
    write_json(system_path, current_system)
    variables_doc = feedback_cycles.variables_doc_from_system(source_dir, current_system)
    variables_doc["variables"] = final_locked_glass_dls_geometry_variables(
        list(variables_doc.get("variables", []) or [])
    )
    if not variables_doc["variables"]:
        raise RuntimeError(f"no locked-glass geometry variables in {source_dir}")
    command = final_locked_glass_dls_command(
        inputs.args,
        source_dir=source_dir,
        system_path=system_path,
        variables_path=variables_path,
        merit_path=merit_path,
        out_dir=pass_dir,
        profile=profile,
        _version=_version,
    )
    score_path = pass_dir / "score.json"
    system_out_path = pass_dir / "system.json"
    resume_contract = final_locked_glass_existing_pass_contract(
        pass_dir=pass_dir,
        system_input=current_system,
        variables_doc=variables_doc,
        merit=merit,
        command=command,
    )
    reuse_existing = bool(resume_contract["matches"]) and (
        not bool(getattr(inputs.args, "force_rerun", False))
    )
    resume_cpu_audit: dict[str, Any] = {}
    return (
        pass_dir,
        system_path,
        variables_path,
        merit_path,
        variables_doc,
        command,
        score_path,
        system_out_path,
        resume_contract,
        reuse_existing,
        resume_cpu_audit,
    )


def describe_locked_dls_candidate(
    pass_index,
    source_rank,
    source_dir,
    source_score,
    score,
    system_out,
    run_result,
    pass_dir,
    inputs,
    merit,
    reuse_existing,
    resume_contract,
    resume_cpu_audit,
):
    """Describe the materialized pass and retain its resume and parent-comparison evidence."""
    row = feedback_cycles.score_to_row(
        cycle=pass_index,
        source_rank=source_rank,
        source_dir=source_dir,
        source_name=source_dir.name,
        material_mode="locked_glass",
        algorithm="final_locked_glass_dls",
        parent_score=source_score,
        score=score,
        system=system_out,
        run_result=run_result,
        out_dir=pass_dir,
        args=inputs.scoring_args,
        merit=merit,
    )
    row["dls_pass"] = pass_index
    row["selected_from"] = f"dls_pass_{pass_index:02d}"
    row["reused_existing"] = reuse_existing
    row["resume_contract"] = resume_contract["contract"]
    row["resume_contract_matches"] = bool(resume_contract["matches"])
    row["resume_contract_reasons"] = resume_contract["reasons"]
    row["advanced_to_next_pass"] = False
    row["vignetting_optimization_frozen"] = True
    candidate_system = system_out
    candidate_score = score
    if resume_cpu_audit:
        row["resume_cpu_consistent"] = bool(resume_cpu_audit.get("consistent"))
    return (row, candidate_system, candidate_score)


def select_locked_dls_source_result(pass_rows, inputs, parent, position, source_row, case_dir):
    """Choose the best admitted pass or preserve the original parent fallback."""
    valid = [
        row
        for row in pass_rows
        if int(finite_float(row.get("returncode"), -1)) == 0
        and (not bool(row.get("timed_out", False)))
        and _row_truthy(row.get("trace_ok"))
        and (finite_float(row.get("trace_coverage"), 0.0) >= inputs.min_trace)
        and (finite_float(row.get("min_field_coverage"), 0.0) >= inputs.min_field)
        and server.vignetting_handoff_policy.chief_ray_usable(row)
        and (finite_float(row.get("min_edge"), -math.inf) >= 0.0)
        and (
            "stage_boundary_vignetting_passed" not in row
            or _row_truthy(row.get("stage_boundary_vignetting_passed"))
        )
        and _row_truthy(row.get("cpu_cuda_consistent"))
        and (finite_float(row.get("merit"), math.inf) < finite_float(parent.get("merit"), math.inf))
    ]
    selected = min(valid, key=lambda row: finite_float(row.get("merit"))) if valid else parent
    selected = dict(selected)
    selected["parent_retained"] = not bool(valid)
    selected["source_feedback_rank"] = int(finite_float(source_row.get("rank"), position))
    write_json(
        case_dir / "summary.json", {"parent": parent, "passes": pass_rows, "selected": selected}
    )
    return selected
