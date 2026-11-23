"""Measure stage-boundary vignetting, independently retrace, validate and persist handoffs."""
from __future__ import annotations
from typing import Any
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .contracts import MaterialPolicy
from pathlib import Path
from .settings import STAGE_BOUNDARY_COVERAGE_ABSOLUTE_TOLERANCE
from .settings import STAGE_BOUNDARY_MODE_APERTURE_RELATIVE_TOLERANCE
from .settings import STAGE_BOUNDARY_SOURCE_APERTURE_RELATIVE_TOLERANCE
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
from .calibration_policy import materialized_stage_boundary_variables
from .calibration_policy import strict_adaptive_pilot_grid_system
from .calibration_policy import terminal_zemax_set_vignetting_command
from .calibration_policy import adaptive_pilot_grid_optimizer_system
import math
from .calibration_policy import stage_boundary_aperture_stability
from .calibration_policy import stage_boundary_trace_admissible
from .common import _row_metric


def recalibrate_vignetting_source_rows(
    args: argparse.Namespace,
    source_rows: list[dict[str, Any]],
    *,
    stage_name: str,
    sampling: dict[str, Any],
    min_trace: float = DE_DLS_TRACE_COVERAGE_BASELINE,
    min_field: float = DE_DLS_TRACE_COVERAGE_BASELINE,
    _version: MaterialPolicy,
) -> list[dict[str, Any]]:
    """Measure fixed apertures, then hand off a complete dynamic-4x8 CP score."""
    if not source_rows:
        return []
    active_positions = vignetting_calibration_row_indices(args, source_rows, terminal=False)
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
    source_aperture_tolerance = max(
        0.0,
        finite_float(
            getattr(
                args,
                "stage_boundary_source_aperture_relative_tolerance",
                STAGE_BOUNDARY_SOURCE_APERTURE_RELATIVE_TOLERANCE,
            ),
            STAGE_BOUNDARY_SOURCE_APERTURE_RELATIVE_TOLERANCE,
        ),
    )
    mode_aperture_tolerance = max(
        0.0,
        finite_float(
            getattr(
                args,
                "stage_boundary_mode_aperture_relative_tolerance",
                STAGE_BOUNDARY_MODE_APERTURE_RELATIVE_TOLERANCE,
            ),
            STAGE_BOUNDARY_MODE_APERTURE_RELATIVE_TOLERANCE,
        ),
    )
    coverage_tolerance = max(
        0.0,
        finite_float(
            getattr(
                args,
                "stage_boundary_coverage_absolute_tolerance",
                STAGE_BOUNDARY_COVERAGE_ABSOLUTE_TOLERANCE,
            ),
            STAGE_BOUNDARY_COVERAGE_ABSOLUTE_TOLERANCE,
        ),
    )
    fixed_aperture_retrace = False
    post_measurement_aperture_update = True
    boundary_contract = "stage_boundary_apg_cp_full_except_cover_dynamic_4x8_v15"
    output_rows: list[dict[str, Any]] = []
    audit_rows: list[dict[str, Any]] = []
    for position, source_row in enumerate(source_rows, start=1):
        if position not in active_positions:
            output_rows.append(dict(source_row))
            continue
        (
            source_dir,
            basin_rank,
            case_dir,
            solve_dir,
            apg_dir,
            calibrated_dir,
            handoff_dir,
            source_system,
            source_score,
            merit,
            serialized_source_variables,
            source_variables,
            removed_glass_choice_variables,
            stage_merit,
            system_input_path,
            merit_path,
            set_vignetting_command,
            summary_path,
            reusable,
        ) = prepare_recalibration_case(
            source_row,
            position,
            stage_root,
            sampling,
            cpu_executable,
            boundary_contract,
            min_trace,
            min_field,
            source_aperture_tolerance,
            mode_aperture_tolerance,
            coverage_tolerance,
            fixed_aperture_retrace,
            post_measurement_aperture_update,
            args,
        )
        if not reusable:
            write_json(case_dir / "set_vignetting_command.json", set_vignetting_command)
            run_result = feedback_cycles.run_command(
                set_vignetting_command,
                _version.cpp_dir,
                case_dir / "set_vignetting_run.log",
                timeout,
                child_workers,
            )
            if int(finite_float(run_result.get("returncode"), -1)) != 0:
                if (
                    not run_result.get("timed_out")
                    and server.vignetting_handoff_policy.optical_measurement_failure(
                        solve_dir, log_paths=(case_dir / "set_vignetting_run.log",)
                    )
                    and server.vignetting_handoff_policy.score_usable(
                        source_score, min_trace=min_trace, min_field=min_field
                    )
                ):
                    audit = {
                        "position": position,
                        "basin_rank": basin_rank,
                        "source_dir": str(source_dir),
                        "handoff_dir": str(source_dir),
                        "passed": True,
                        "calibration_diagnostics_passed": False,
                        "fallback_used": True,
                        "vignetting_recalibrated": False,
                        "reason": "measurement_unavailable_keep_source",
                    }
                    write_json(summary_path, audit)
                    audit_rows.append(audit)
                    output_rows.append({**source_row, **audit, "out_dir": str(source_dir)})
                    continue
                raise RuntimeError(f"stage-boundary Set Vignetting failed: {case_dir}")
            (
                set_vignetting_system,
                set_vignetting_score,
                illumination_trim,
                variables,
                apg_command,
                calibrated_command,
            ) = prepare_recalibration_retraces(
                solve_dir,
                case_dir,
                source_variables,
                cpu_executable,
                system_input_path,
                merit_path,
                sampling,
                source_score,
                source_system,
                stage_merit,
                child_workers,
                timeout,
                apg_dir,
                calibrated_dir,
                _version=_version,
            )
            (
                apg_system,
                calibrated_system,
                apg_score,
                calibrated_score,
                aperture,
                trace_difference,
                field_difference,
                source_aperture_change_within_diagnostic_tolerance,
                aperture_stable,
                coverage_consistent,
                stage_ok,
                proposed_calibrated_score,
                fallback_used,
                delivered_merit,
                variables,
            ) = trace_and_validate_recalibration(
                apg_command,
                case_dir,
                calibrated_command,
                timeout,
                child_workers,
                apg_dir,
                calibrated_dir,
                source_system,
                source_aperture_tolerance,
                mode_aperture_tolerance,
                coverage_tolerance,
                min_trace,
                min_field,
                stage_merit,
                source_score,
                serialized_source_variables,
                merit,
                handoff_dir,
                source_dir,
                variables,
                _version=_version,
            )
            (audit,) = persist_recalibration_handoff(
                variables,
                handoff_dir,
                calibrated_system,
                calibrated_score,
                delivered_merit,
                set_vignetting_system,
                set_vignetting_score,
                apg_system,
                apg_score,
                proposed_calibrated_score,
                illumination_trim,
                position,
                basin_rank,
                source_dir,
                fallback_used,
                min_trace,
                min_field,
                trace_difference,
                field_difference,
                aperture,
                aperture_stable,
                source_aperture_change_within_diagnostic_tolerance,
                coverage_consistent,
                source_aperture_tolerance,
                mode_aperture_tolerance,
                coverage_tolerance,
                removed_glass_choice_variables,
                source_score,
                post_measurement_aperture_update,
                fixed_aperture_retrace,
                stage_ok,
                summary_path,
            )
        else:
            audit = read_json(summary_path)
            audit["cache_hit"] = True
            write_json(summary_path, audit)
            calibrated_score = read_json(handoff_dir / "score.json")
            stage_ok = bool(audit.get("passed"))
        admit_recalibration_row(
            source_row, audit, handoff_dir, calibrated_score, audit_rows, stage_ok, output_rows
        )
    write_csv(stage_root / "ranked.csv", output_rows)
    rejected_rows = [row for row in audit_rows if not bool(row.get("passed"))]
    write_csv(stage_root / "rejected.csv", rejected_rows)
    write_json(
        stage_root / "report.json",
        {
            "contract": boundary_contract,
            "stage_name": stage_name,
            "hot_optimizer_path_unchanged": True,
            "admission_uses_merit": False,
            "cpu_cuda_audit_performed": False,
            "y_only_diagnostics_performed": False,
            "accepted_count": len(output_rows),
            "rejected_count": len(rejected_rows),
            "rows": audit_rows,
        },
    )
    record_candidate_shortfall(args, stage_name, len(source_rows), output_rows)
    return output_rows


def prepare_recalibration_case(
    source_row,
    position,
    stage_root,
    sampling,
    cpu_executable,
    boundary_contract,
    min_trace,
    min_field,
    source_aperture_tolerance,
    mode_aperture_tolerance,
    coverage_tolerance,
    fixed_aperture_retrace,
    post_measurement_aperture_update,
    args,
):
    """Resolve one source, remove discrete glass choices and freeze the measurement contract."""
    source_dir = Path(str(source_row.get("out_dir") or "")).resolve()
    if not source_dir.is_dir():
        raise FileNotFoundError(source_dir)
    basin_rank = int(finite_float(source_row.get("basin_rank"), position))
    case_dir = server.native_io_paths.python_path(
        server.native_io_paths.named_child(stage_root, f"cand_{position:02d}_rank_{basin_rank:04d}")
    )
    solve_dir = server.native_io_paths.named_child(case_dir, "set_vignetting")
    apg_dir = server.native_io_paths.named_child(case_dir, "apg_retrace")
    calibrated_dir = server.native_io_paths.named_child(case_dir, "calibrated_paraxial_retrace")
    handoff_dir = case_dir / "handoff"
    source_system = read_json(feedback_cycles.file_from_source(source_dir, "system.json"))
    source_score = read_json(feedback_cycles.file_from_source(source_dir, "score.json"))
    merit = read_json(feedback_cycles.file_from_source(source_dir, "merit.json"))
    variables_payload = read_json(feedback_cycles.file_from_source(source_dir, "variables.json"))
    serialized_source_variables = list(variables_payload.get("variables") or [])
    source_variables = materialized_stage_boundary_variables(serialized_source_variables)
    removed_glass_choice_variables = len(serialized_source_variables) - len(source_variables)
    stage_merit = copy.deepcopy(merit)
    server.dynamic_aperture_contract.apply_merit(stage_merit)
    stage_merit["num_rays"] = int(sampling["num_rays"])
    stage_merit["pupil_arms"] = int(sampling["pupil_arms"])
    stage_merit["pupil_distribution"] = str(sampling["pupil_distribution"])
    stage_merit["clear_aperture_margin"] = 1.0
    (
        hard_aperture_system,
        materialized_aperture_count,
    ) = server.zemax_set_vignetting_hard_aperture_copy(source_system)
    measurement_system = strict_adaptive_pilot_grid_system(hard_aperture_system)
    system_input_path = case_dir / "system_input.json"
    merit_path = case_dir / "merit.json"
    write_json(system_input_path, measurement_system)
    write_json(merit_path, stage_merit)
    set_vignetting_command = terminal_zemax_set_vignetting_command(
        cpu_executable=cpu_executable,
        system_path=system_input_path,
        merit_path=merit_path,
        out_dir=solve_dir,
        sampling=sampling,
    )
    contract = {
        "contract": boundary_contract,
        "apg_retrace_is_diagnostic_not_admission": True,
        "cp_per_ray_convergence_gate": False,
        "source": str(source_dir),
        "source_system_sha256": hashlib.sha256(
            json.dumps(source_system, sort_keys=True).encode("utf-8")
        ).hexdigest(),
        "measurement_system_sha256": hashlib.sha256(
            json.dumps(measurement_system, sort_keys=True).encode("utf-8")
        ).hexdigest(),
        "materialized_aperture_count": materialized_aperture_count,
        "excluded_surface_indices": server.protective_glass_surface_indices(source_system),
        "merit_sha256": hashlib.sha256(
            json.dumps(stage_merit, sort_keys=True).encode("utf-8")
        ).hexdigest(),
        "sampling": sampling,
        "minimum_trace_coverage": min_trace,
        "minimum_field_coverage": min_field,
        "source_aperture_relative_tolerance": source_aperture_tolerance,
        "mode_aperture_relative_tolerance": mode_aperture_tolerance,
        "coverage_absolute_tolerance": coverage_tolerance,
        "coverage_difference_is_diagnostic_not_admission": True,
        "mode_aperture_difference_is_diagnostic_not_admission": True,
        "measurement": "fixed_aperture_exact_apg_set_vignetting",
        "post_measurement_retrace": [
            "strict_adaptive_pilot_grid_fixed_aperture"
            if fixed_aperture_retrace
            else "strict_adaptive_pilot_grid_with_aperture_update",
            "strict_calibrated_paraxial_fixed_aperture"
            if fixed_aperture_retrace
            else "strict_calibrated_paraxial_with_aperture_update",
        ],
        "post_measurement_aperture_update": bool(post_measurement_aperture_update),
        "post_measurement_projection_or_contraction": False,
        "admission_uses_merit": False,
        "set_vignetting_score_is_optimizer_comparable": False,
        "exact_apg_score_is_optimizer_comparable": False,
        "optimizer_handoff_score_mode": "calibrated_paraxial",
        "admission_uses_cpu_cuda_parity": False,
        "admission_uses_y_only_diagnostics": False,
        "source_aperture_change_is_diagnostic_not_admission": True,
        "optimizer_handoff_aiming": "calibrated_paraxial",
        "optimizer_handoff_fallback": "none",
        "cpu_executable": material_bridge.executable_identity(cpu_executable),
    }
    if removed_glass_choice_variables:
        contract["materialized_glass_choice_variables_removed"] = removed_glass_choice_variables
    contract_path = case_dir / "contract.json"
    summary_path = case_dir / "summary.json"
    reusable = (
        not bool(getattr(args, "force_rerun", False))
        and contract_path.is_file()
        and (read_json(contract_path) == contract)
        and summary_path.is_file()
        and bool(read_json(summary_path).get("passed"))
        and (handoff_dir / "system.json").is_file()
        and (handoff_dir / "score.json").is_file()
        and (handoff_dir / "variables.json").is_file()
        and (handoff_dir / "merit.json").is_file()
        and (handoff_dir / "exact_apg_final_score.json").is_file()
        and (handoff_dir / "calibrated_cpu_score.json").is_file()
    )
    write_json(contract_path, contract)
    return (
        source_dir,
        basin_rank,
        case_dir,
        solve_dir,
        apg_dir,
        calibrated_dir,
        handoff_dir,
        source_system,
        source_score,
        merit,
        serialized_source_variables,
        source_variables,
        removed_glass_choice_variables,
        stage_merit,
        system_input_path,
        merit_path,
        set_vignetting_command,
        summary_path,
        reusable,
    )


def prepare_recalibration_retraces(
    solve_dir,
    case_dir,
    source_variables,
    cpu_executable,
    system_input_path,
    merit_path,
    sampling,
    source_score,
    source_system,
    stage_merit,
    child_workers,
    timeout,
    apg_dir,
    calibrated_dir,
    *,
    _version: MaterialPolicy,
):
    """Materialize the measured pupil and build independent APG and calibrated retraces."""
    set_vignetting_system = read_json(solve_dir / "system.json")
    set_vignetting_score = read_json(solve_dir / "score.json")
    trim_dir = server.native_io_paths.named_child(case_dir, "illumination_trim")
    write_json(trim_dir / "variables.json", {"variables": source_variables})
    trim_command = [
        str(cpu_executable),
        "--system",
        str(system_input_path),
        "--merit",
        str(merit_path),
        "--variables",
        str(trim_dir / "variables.json"),
        "--cpu",
        "--aiming",
        "calibrated_paraxial",
        "--rays",
        str(sampling["num_rays"]),
        "--arms",
        str(sampling["pupil_arms"]),
        "--distribution",
        str(sampling["pupil_distribution"]),
    ]
    trim_measurement, illumination_trim = server.stage_illumination_trim.suggest(
        server,
        source_system,
        source_score,
        stage_merit,
        {
            "system": server.post_asphere_optimizer_system_from_measurement(
                source_system, set_vignetting_system
            )
        },
        trim_dir,
        cpu_executable,
        trim_command,
        timeout,
        child_workers,
    )
    if illumination_trim["performed"]:
        set_vignetting_system = trim_measurement["system"]
        set_vignetting_score = trim_measurement["set_vignetting_score"]
    apg_input = adaptive_pilot_grid_optimizer_system(
        set_vignetting_system, source_system, _version=_version
    )
    calibrated_input = _version.calibrated_paraxial_optimizer_system(
        set_vignetting_system, source_system
    )
    variables = server.ensure_vignetting_variables_for_system(
        calibrated_input, source_variables, stage_merit, handoff=True
    )
    variables = server.sync_variables_from_system(variables, calibrated_input)
    variables_doc = {"variables": variables}
    apg_input_path = case_dir / "apg_retrace_input_system.json"
    calibrated_input_path = case_dir / "calibrated_paraxial_retrace_input_system.json"
    variables_path = case_dir / "variables.json"
    write_json(apg_input_path, apg_input)
    write_json(calibrated_input_path, calibrated_input)
    write_json(variables_path, variables_doc)
    aperture_retrace_flags = server.dynamic_aperture_contract.apply_command([])
    common_retrace = [
        "--variables",
        str(variables_path.resolve()),
        "--merit",
        str(merit_path.resolve()),
        "--cpu",
        *aperture_retrace_flags,
        "--rays",
        str(sampling["num_rays"]),
        "--arms",
        str(sampling["pupil_arms"]),
        "--distribution",
        str(sampling["pupil_distribution"]),
    ]
    apg_command = [
        str(cpu_executable),
        "--system",
        str(apg_input_path.resolve()),
        *common_retrace,
        "--aiming",
        "adaptive_pilot_grid",
        "--aiming-require-convergence",
        "--aiming-iter",
        "40",
        "--aiming-continuation-steps",
        "16",
        "--out-dir",
        str(apg_dir.resolve()),
    ]
    calibrated_command = [
        str(cpu_executable),
        "--system",
        str(calibrated_input_path.resolve()),
        *common_retrace,
        "--aiming",
        "calibrated_paraxial",
        "--out-dir",
        str(calibrated_dir.resolve()),
    ]
    write_json(case_dir / "apg_retrace_command.json", apg_command)
    write_json(case_dir / "calibrated_paraxial_retrace_command.json", calibrated_command)
    return (
        set_vignetting_system,
        set_vignetting_score,
        illumination_trim,
        variables,
        apg_command,
        calibrated_command,
    )


def trace_and_validate_recalibration(
    apg_command,
    case_dir,
    calibrated_command,
    timeout,
    child_workers,
    apg_dir,
    calibrated_dir,
    source_system,
    source_aperture_tolerance,
    mode_aperture_tolerance,
    coverage_tolerance,
    min_trace,
    min_field,
    stage_merit,
    source_score,
    serialized_source_variables,
    merit,
    handoff_dir,
    source_dir,
    variables,
    *,
    _version: MaterialPolicy,
):
    """Run both retraces, compare coverage and enforce the aperture handoff contract."""
    for label, command, log_path in (
        ("APG", apg_command, case_dir / "apg_retrace_run.log"),
        (
            "calibrated-paraxial",
            calibrated_command,
            case_dir / "calibrated_paraxial_retrace_run.log",
        ),
    ):
        result = feedback_cycles.run_command(
            command, _version.cpp_dir, log_path, timeout, child_workers
        )
        if int(finite_float(result.get("returncode"), -1)) != 0:
            raise RuntimeError(f"stage-boundary {label} retrace failed: {case_dir}")
    apg_system = read_json(apg_dir / "system.json")
    calibrated_system = read_json(calibrated_dir / "system.json")
    apg_score = read_json(apg_dir / "score.json")
    calibrated_score = read_json(calibrated_dir / "score.json")
    aperture = stage_boundary_aperture_stability(source_system, apg_system, calibrated_system)
    trace_difference = abs(
        finite_float(apg_score.get("trace_coverage"), 0.0)
        - finite_float(calibrated_score.get("trace_coverage"), 0.0)
    )
    field_difference = abs(
        finite_float(apg_score.get("min_field_coverage"), 0.0)
        - finite_float(calibrated_score.get("min_field_coverage"), 0.0)
    )
    source_aperture_change_within_diagnostic_tolerance = (
        finite_float(aperture.get("source_to_apg_max_relative_change"), math.inf)
        <= source_aperture_tolerance
        and finite_float(aperture.get("source_to_calibrated_max_relative_change"), math.inf)
        <= source_aperture_tolerance
    )
    aperture_stable = (
        bool(aperture.get("surface_count_consistent"))
        and finite_float(aperture.get("apg_to_calibrated_max_relative_difference"), math.inf)
        <= mode_aperture_tolerance
    )
    coverage_consistent = (
        trace_difference <= coverage_tolerance and field_difference <= coverage_tolerance
    )
    stage_ok = stage_boundary_trace_admissible(
        apg_score, calibrated_score, min_trace=min_trace, min_field=min_field
    )
    proposed_calibrated_score = copy.deepcopy(calibrated_score)
    fallback_used = False
    delivered_merit = stage_merit
    if not stage_ok and server.vignetting_handoff_policy.score_usable(
        source_score, min_trace=min_trace, min_field=min_field
    ):
        calibrated_system = copy.deepcopy(source_system)
        calibrated_score = copy.deepcopy(source_score)
        variables = copy.deepcopy(serialized_source_variables)
        delivered_merit = merit
        fallback_used = True
        stage_ok = True
        server.copy_candidate_artifacts(source_dir, handoff_dir)
    else:
        variables = server.sync_variables_from_system(variables, calibrated_system)
    return (
        apg_system,
        calibrated_system,
        apg_score,
        calibrated_score,
        aperture,
        trace_difference,
        field_difference,
        source_aperture_change_within_diagnostic_tolerance,
        aperture_stable,
        coverage_consistent,
        stage_ok,
        proposed_calibrated_score,
        fallback_used,
        delivered_merit,
        variables,
    )


def persist_recalibration_handoff(
    variables,
    handoff_dir,
    calibrated_system,
    calibrated_score,
    delivered_merit,
    set_vignetting_system,
    set_vignetting_score,
    apg_system,
    apg_score,
    proposed_calibrated_score,
    illumination_trim,
    position,
    basin_rank,
    source_dir,
    fallback_used,
    min_trace,
    min_field,
    trace_difference,
    field_difference,
    aperture,
    aperture_stable,
    source_aperture_change_within_diagnostic_tolerance,
    coverage_consistent,
    source_aperture_tolerance,
    mode_aperture_tolerance,
    coverage_tolerance,
    removed_glass_choice_variables,
    source_score,
    post_measurement_aperture_update,
    fixed_aperture_retrace,
    stage_ok,
    summary_path,
):
    """Persist the accepted system, merit, variables and ordered diagnostic evidence."""
    variables_doc = {"variables": variables}
    handoff_dir.mkdir(parents=True, exist_ok=True)
    write_json(handoff_dir / "system.json", calibrated_system)
    write_json(handoff_dir / "score.json", calibrated_score)
    write_json(handoff_dir / "variables.json", variables_doc)
    write_json(handoff_dir / "merit.json", delivered_merit)
    write_json(handoff_dir / "set_vignetting_system.json", set_vignetting_system)
    write_json(handoff_dir / "set_vignetting_score.json", set_vignetting_score)
    write_json(handoff_dir / "exact_apg_final_system.json", apg_system)
    write_json(handoff_dir / "exact_apg_final_score.json", apg_score)
    write_json(handoff_dir / "calibrated_cpu_score.json", proposed_calibrated_score)
    audit = {
        "illumination_margin_trim": illumination_trim,
        "position": position,
        "basin_rank": basin_rank,
        "source_dir": str(source_dir),
        "handoff_dir": str(handoff_dir),
        "cache_hit": False,
        "fallback_used": fallback_used,
        "vignetting_recalibrated": not fallback_used,
        "selected_score_source": "original_optimizer_endpoint"
        if fallback_used
        else "calibrated_paraxial_retrace",
        "calibration_diagnostics_passed": bool(apg_score.get("trace_ok"))
        and finite_float(apg_score.get("trace_coverage"), 0.0) >= min_trace
        and (finite_float(apg_score.get("min_field_coverage"), 0.0) >= min_field)
        and (not fallback_used),
        "apg_retrace_is_diagnostic_not_admission": True,
        "cp_per_ray_convergence_gate": False,
        "proposed_calibrated_score": proposed_calibrated_score,
        "trace_coverage": calibrated_score.get("trace_coverage"),
        "min_field_coverage": calibrated_score.get("min_field_coverage"),
        "apg_trace_coverage": apg_score.get("trace_coverage"),
        "calibrated_trace_coverage": calibrated_score.get("trace_coverage"),
        "trace_coverage_absolute_difference": trace_difference,
        "apg_min_field_coverage": apg_score.get("min_field_coverage"),
        "calibrated_min_field_coverage": calibrated_score.get("min_field_coverage"),
        "min_field_coverage_absolute_difference": field_difference,
        "aperture_stability": aperture,
        "aperture_stable": aperture_stable,
        "source_aperture_change_within_diagnostic_tolerance": source_aperture_change_within_diagnostic_tolerance,
        "coverage_consistent": coverage_consistent,
        "coverage_difference_is_diagnostic_not_admission": True,
        "mode_aperture_difference_is_diagnostic_not_admission": True,
        "source_aperture_relative_tolerance": source_aperture_tolerance,
        "mode_aperture_relative_tolerance": mode_aperture_tolerance,
        "coverage_absolute_tolerance": coverage_tolerance,
        "materialized_glass_choice_variables_removed": removed_glass_choice_variables,
        "admission_uses_merit": False,
        "pre_vignetting_calibration_merit": source_score.get("merit"),
        "post_vignetting_calibration_optimizer_merit": calibrated_score.get("merit"),
        "set_vignetting_score_is_optimizer_comparable": False,
        "exact_apg_score_is_optimizer_comparable": False,
        "optimizer_handoff_score_mode": "calibrated_paraxial",
        "cpu_cuda_audit_performed": False,
        "y_only_diagnostics_performed": False,
        "projection_or_contraction_applied": False,
        "post_measurement_aperture_update": bool(post_measurement_aperture_update),
        "fixed_non_stop_aperture_retrace": fixed_aperture_retrace,
        "passed": stage_ok,
    }
    write_json(summary_path, audit)
    return (audit,)


def admit_recalibration_row(
    source_row, audit, handoff_dir, calibrated_score, audit_rows, stage_ok, output_rows
):
    """Publish only a handoff that passes the existing stage admission checks."""
    row = dict(source_row)
    row.update(audit)
    row["pre_vignetting_calibration_merit"] = _row_metric(source_row, "final_merit", "merit")
    row["out_dir"] = str(handoff_dir)
    row["vignetting_recalibrated"] = not bool(audit.get("fallback_used"))
    for key in (
        "merit",
        "max_distortion",
        "trace_ok",
        "trace_coverage",
        "min_field_coverage",
        "min_chief_progress",
        "min_edge",
        "constraint_group_error",
    ):
        if key in calibrated_score:
            row[key] = calibrated_score[key]
    if "mean_spot" in calibrated_score:
        row["mean_spot_um"] = 1000.0 * finite_float(calibrated_score.get("mean_spot"), math.inf)
    if "max_spot" in calibrated_score:
        row["max_spot_um"] = 1000.0 * finite_float(calibrated_score.get("max_spot"), math.inf)
    row["post_vignetting_calibration_optimizer_merit"] = calibrated_score.get("merit")
    audit_rows.append(audit)
    if stage_ok:
        output_rows.append(row)
