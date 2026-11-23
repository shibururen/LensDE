"""Define final locked-glass DLS profiles, commands, resume identity and advancement rules."""
from __future__ import annotations
from typing import Any
import argparse
from .common import parse_float_list
from .common import parse_int_list
from .settings import FINAL_LOCKED_GLASS_DLS_GEOMETRY_KINDS
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .contracts import MaterialPolicy
from pathlib import Path
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .common import finite_float
from .common import fixed_glass_feedback_trace_sampling
from .feedback_contracts import inherit_refine_like_aiming_from_system
from .common import read_json
from lensde.engine import core as server
from .settings import FINAL_DLS_CPU_CUDA_CONSTRAINT_ABS_TOL
from .settings import FINAL_DLS_CPU_CUDA_CONTRACT
from .settings import FINAL_DLS_CPU_CUDA_COVERAGE_ABS_TOL
from .settings import FINAL_DLS_CPU_CUDA_EDGE_ABS_TOL
from .settings import FINAL_DLS_CPU_CUDA_MERIT_REL_TOL
from .settings import FINAL_DLS_CPU_CUDA_SPOT_REL_TOL
from .common import _row_truthy
import math
import json
from .common import read_csv


def final_locked_glass_dls_profiles(args: argparse.Namespace) -> list[dict[str, Any]]:
    iterations = parse_int_list(
        str(getattr(args, "final_locked_glass_dls_iter_schedule", "400,400,400"))
    ) or [400, 400, 400]
    lambdas = parse_float_list(
        str(getattr(args, "final_locked_glass_dls_lambda_schedule", "0.01,0.001,0.001"))
    ) or [0.01, 0.001, 0.001]
    tolerances = parse_float_list(
        str(getattr(args, "final_locked_glass_dls_tol_schedule", "0.0001,0.00001,0.00001"))
    ) or [0.0001, 1e-05, 1e-05]
    if not len(iterations) == len(lambdas) == len(tolerances):
        raise ValueError(
            "final locked-glass DLS iteration/lambda/tolerance schedules must have equal lengths"
        )
    return [
        {
            "iterations": max(1, int(iteration)),
            "lambda": float(local_lambda),
            "tol": float(tolerance),
            "max_reject": max(1, int(getattr(args, "final_locked_glass_dls_max_reject", 200))),
        }
        for iteration, local_lambda, tolerance in zip(iterations, lambdas, tolerances, strict=True)
    ]


def final_locked_glass_dls_geometry_variables(
    variables: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    """Keep pupil coefficients fixed during the inner CP geometry solve."""
    return [
        variable
        for variable in variables
        if str(variable.get("kind") or "").strip().lower() in FINAL_LOCKED_GLASS_DLS_GEOMETRY_KINDS
    ]


def final_locked_glass_dls_command(
    args: argparse.Namespace,
    *,
    source_dir: Path,
    system_path: Path,
    variables_path: Path,
    merit_path: Path,
    out_dir: Path,
    profile: dict[str, Any],
    _version: MaterialPolicy,
) -> list[str]:
    command = feedback_cycles.base_command_prefix(feedback_cycles.load_command(source_dir))
    if system_path.is_file():
        command = inherit_refine_like_aiming_from_system(command, read_json(system_path))
    server.dynamic_aperture_contract.apply_command(command)
    command[0] = str(
        Path(getattr(args, "exe", None) or _version.cpp_dir / "build" / "lensde_cpu.exe").resolve()
    )
    sampling = fixed_glass_feedback_trace_sampling(args)
    for flag, value in [
        ("--system", system_path),
        ("--variables", variables_path),
        ("--merit", merit_path),
        ("--out-dir", out_dir),
        ("--rays", sampling["num_rays"]),
        ("--arms", sampling["pupil_arms"]),
        ("--distribution", sampling["pupil_distribution"]),
        ("--local-iter", int(profile["iterations"])),
        ("--local-method", str(getattr(args, "final_locked_glass_dls_method", "trf"))),
        ("--local-bounds-mode", str(getattr(args, "final_locked_glass_dls_bounds_mode", "safety"))),
        (
            "--local-residual-mode",
            str(getattr(args, "final_locked_glass_dls_residual_mode", "expanded")),
        ),
        ("--local-lambda", f"{float(profile['lambda']):.17g}"),
        ("--local-fd", f"{float(getattr(args, 'final_locked_glass_dls_fd', 0.0001)):.17g}"),
        ("--local-tol", f"{float(profile['tol']):.17g}"),
        ("--local-max-reject", int(profile["max_reject"])),
        (
            "--optimizer-internal-scaling",
            str(getattr(args, "final_locked_glass_dls_scaling", "legacy_optical")),
        ),
    ]:
        feedback_cycles.set_command_arg(command, flag, value)
    min_trace = min(
        1.0,
        max(
            0.0,
            finite_float(
                getattr(
                    args,
                    "final_locked_glass_dls_min_trace_coverage",
                    DE_DLS_TRACE_COVERAGE_BASELINE,
                ),
                DE_DLS_TRACE_COVERAGE_BASELINE,
            ),
        ),
    )
    min_field = min(
        1.0,
        max(
            0.0,
            finite_float(
                getattr(args, "final_locked_glass_dls_min_field_coverage", min_trace), min_trace
            ),
        ),
    )
    coverage_epsilon = 1.0 - min(min_trace, min_field)
    feedback_cycles.set_command_arg(command, "--trace-coverage-epsilon", f"{coverage_epsilon:.17g}")
    feedback_cycles.set_command_arg(
        command, "--local-trace-coverage-epsilon", f"{coverage_epsilon:.17g}"
    )
    command[:] = [
        token
        for token in command
        if token
        not in {
            "--local-require-trace-feasible",
            "--local-allow-trace-infeasible",
            "--local-allow-near-trace-infeasible",
        }
    ]
    command.append("--local-allow-near-trace-infeasible")
    if "--local" not in command:
        command.append("--local")
    if "--cpu" not in command:
        command.append("--cpu")
    return command


def final_locked_glass_cpu_cuda_consistency(
    cpu_score: dict[str, Any], cuda_score: dict[str, Any], *, min_trace: float, min_field: float
) -> dict[str, Any]:
    cpu_merit = finite_float(cpu_score.get("merit"), math.inf)
    cuda_merit = finite_float(cuda_score.get("merit"), math.inf)
    merit_relative_difference = abs(cpu_merit - cuda_merit) / max(1.0, abs(cpu_merit))
    coverage_difference = abs(
        finite_float(cpu_score.get("trace_coverage"), 0.0)
        - finite_float(cuda_score.get("trace_coverage"), 0.0)
    )
    field_difference = abs(
        finite_float(cpu_score.get("min_field_coverage"), 0.0)
        - finite_float(cuda_score.get("min_field_coverage"), 0.0)
    )
    chief_difference = abs(
        finite_float(cpu_score.get("min_chief_progress"), 0.0)
        - finite_float(cuda_score.get("min_chief_progress"), 0.0)
    )
    edge_difference = abs(
        finite_float(cpu_score.get("min_edge"), -math.inf)
        - finite_float(cuda_score.get("min_edge"), -math.inf)
    )
    constraint_difference = abs(
        finite_float(cpu_score.get("constraint_penalty"), 0.0)
        - finite_float(cuda_score.get("constraint_penalty"), 0.0)
    )
    cpu_spot = finite_float(cpu_score.get("mean_spot"), math.inf)
    cuda_spot = finite_float(cuda_score.get("mean_spot"), math.inf)
    spot_relative_difference = abs(cpu_spot - cuda_spot) / max(1e-12, abs(cpu_spot))
    reasons: list[str] = []
    if not (math.isfinite(cpu_merit) and math.isfinite(cuda_merit)):
        reasons.append("nonfinite_merit")
    elif merit_relative_difference > FINAL_DLS_CPU_CUDA_MERIT_REL_TOL:
        reasons.append("merit_mismatch")
    cpu_trace_ok = _row_truthy(cpu_score.get("trace_ok"))
    cuda_trace_ok = _row_truthy(cuda_score.get("trace_ok"))
    cpu_trace_coverage = finite_float(cpu_score.get("trace_coverage"), 0.0)
    cuda_trace_coverage = finite_float(cuda_score.get("trace_coverage"), 0.0)
    cpu_field_coverage = finite_float(cpu_score.get("min_field_coverage"), 0.0)
    cuda_field_coverage = finite_float(cuda_score.get("min_field_coverage"), 0.0)
    cpu_chief_progress = finite_float(cpu_score.get("min_chief_progress"), 0.0)
    cuda_chief_progress = finite_float(cuda_score.get("min_chief_progress"), 0.0)
    cpu_min_edge = finite_float(cpu_score.get("min_edge"), -math.inf)
    cuda_min_edge = finite_float(cuda_score.get("min_edge"), -math.inf)
    if cpu_trace_ok != cuda_trace_ok:
        reasons.append("trace_validity_mismatch")
    if cpu_trace_ok and (not cuda_trace_ok):
        reasons.append("cuda_trace_invalid")
    if cpu_trace_coverage >= min_trace and cuda_trace_coverage < min_trace:
        reasons.append("cuda_trace_coverage_below_threshold")
    if cpu_field_coverage >= min_field and cuda_field_coverage < min_field:
        reasons.append("cuda_field_coverage_below_threshold")
    if cpu_chief_progress >= 1.0 - 1e-12 and cuda_chief_progress < 1.0 - 1e-12:
        reasons.append("cuda_chief_progress_incomplete")
    if cpu_min_edge >= 0.0 and cuda_min_edge < 0.0:
        reasons.append("cuda_negative_edge")
    if coverage_difference > FINAL_DLS_CPU_CUDA_COVERAGE_ABS_TOL:
        reasons.append("trace_coverage_mismatch")
    if field_difference > FINAL_DLS_CPU_CUDA_COVERAGE_ABS_TOL:
        reasons.append("field_coverage_mismatch")
    if chief_difference > FINAL_DLS_CPU_CUDA_COVERAGE_ABS_TOL:
        reasons.append("chief_progress_mismatch")
    if edge_difference > FINAL_DLS_CPU_CUDA_EDGE_ABS_TOL:
        reasons.append("edge_mismatch")
    if constraint_difference > FINAL_DLS_CPU_CUDA_CONSTRAINT_ABS_TOL:
        reasons.append("constraint_penalty_mismatch")
    if spot_relative_difference > FINAL_DLS_CPU_CUDA_SPOT_REL_TOL:
        reasons.append("mean_spot_mismatch")
    return {
        "contract": FINAL_DLS_CPU_CUDA_CONTRACT,
        "consistent": not reasons,
        "reasons": reasons,
        "cpu_merit": cpu_merit,
        "cuda_merit": cuda_merit,
        "merit_relative_difference": merit_relative_difference,
        "coverage_absolute_difference": coverage_difference,
        "field_coverage_absolute_difference": field_difference,
        "chief_progress_absolute_difference": chief_difference,
        "edge_absolute_difference": edge_difference,
        "constraint_penalty_absolute_difference": constraint_difference,
        "mean_spot_relative_difference": spot_relative_difference,
        "tolerances": {
            "merit_relative": FINAL_DLS_CPU_CUDA_MERIT_REL_TOL,
            "coverage_absolute": FINAL_DLS_CPU_CUDA_COVERAGE_ABS_TOL,
            "edge_absolute_mm": FINAL_DLS_CPU_CUDA_EDGE_ABS_TOL,
            "constraint_penalty_absolute": FINAL_DLS_CPU_CUDA_CONSTRAINT_ABS_TOL,
            "mean_spot_relative": FINAL_DLS_CPU_CUDA_SPOT_REL_TOL,
        },
    }


def final_locked_glass_pass_advances(
    row: dict[str, Any], current_score: dict[str, Any], *, min_trace: float, min_field: float
) -> bool:
    """Only an admissible, parity-checked improvement may seed the next pass."""
    return (
        int(finite_float(row.get("returncode"), -1)) == 0
        and (not _row_truthy(row.get("timed_out")))
        and _row_truthy(row.get("trace_ok"))
        and (finite_float(row.get("trace_coverage"), 0.0) >= min_trace)
        and (finite_float(row.get("min_field_coverage"), 0.0) >= min_field)
        and server.vignetting_handoff_policy.chief_ray_usable(row)
        and (finite_float(row.get("min_edge"), -math.inf) >= 0.0)
        and (
            "stage_boundary_vignetting_passed" not in row
            or _row_truthy(row.get("stage_boundary_vignetting_passed"))
        )
        and _row_truthy(row.get("cpu_cuda_consistent"))
        and (
            finite_float(row.get("merit"), math.inf)
            < finite_float(current_score.get("merit"), math.inf)
        )
    )


def final_locked_glass_existing_pass_contract(
    *,
    pass_dir: Path,
    system_input: dict[str, Any],
    variables_doc: dict[str, Any],
    merit: dict[str, Any],
    command: list[str],
) -> dict[str, Any]:
    """Check whether a completed pass is the exact pass requested now.

    A matching directory is only a resume *candidate*.  The caller must still
    rescore its materialized system with the current CPU and CUDA executables
    before accepting it, because executable behavior can change without any
    JSON input changing.
    """
    expected_documents = {
        "system_input.json": system_input,
        "variables.json": variables_doc,
        "merit.json": merit,
        "command.json": command,
    }
    reasons: list[str] = []
    for name, expected in expected_documents.items():
        path = pass_dir / name
        if not path.is_file():
            reasons.append(f"missing_{name}")
            continue
        try:
            actual = read_json(path)
        except (OSError, json.JSONDecodeError):
            reasons.append(f"invalid_{name}")
            continue
        if actual != expected:
            reasons.append(f"mismatch_{name}")
    for name in ("system.json", "score.json"):
        if not (pass_dir / name).is_file():
            reasons.append(f"missing_{name}")
    guard_path = pass_dir / "final_materialization_guard.json"
    if not guard_path.is_file():
        reasons.append("missing_final_materialization_guard.json")
    else:
        try:
            guard = read_json(guard_path)
        except (OSError, json.JSONDecodeError):
            reasons.append("invalid_final_materialization_guard.json")
        else:
            if not bool(guard.get("selected_materialization_valid")):
                reasons.append("invalid_selected_materialization")
    return {
        "contract": "exact_final_dls_pass_inputs_v2",
        "matches": not reasons,
        "reasons": reasons,
    }


def locked_glass_final_dls_source_rows(
    args: argparse.Namespace,
    feedback_dir: Path,
    ranked_source_rows: list[dict[str, Any]] | None = None,
) -> list[dict[str, Any]]:
    """Apply the final-DLS source admission contract exactly once."""
    ranked_path = feedback_dir / "fixed_glass_feedback_ranked.csv"
    if ranked_source_rows is None:
        if not ranked_path.is_file():
            ranked_path = feedback_dir / "cycle_summary.csv"
        if not ranked_path.is_file():
            raise FileNotFoundError(f"locked-glass DLS tail has no source ranking: {feedback_dir}")
        ranked_source_rows = read_csv(ranked_path)
    min_trace = float(
        getattr(args, "final_locked_glass_dls_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
    )
    min_field = float(
        getattr(args, "final_locked_glass_dls_min_field_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
    )
    source_rows = [
        row
        for row in ranked_source_rows
        if (
            _row_truthy(row.get("admission_ok"))
            if str(row.get("admission_ok") or "").strip()
            else int(finite_float(row.get("returncode"), -1)) == 0
            and (not _row_truthy(row.get("timed_out")))
            and (finite_float(row.get("merit"), math.inf) < 1000000000000.0)
            and _row_truthy(row.get("trace_ok"))
            and (finite_float(row.get("trace_coverage"), 0.0) >= min_trace)
            and (finite_float(row.get("min_field_coverage"), 0.0) >= min_field)
            and server.vignetting_handoff_policy.chief_ray_usable(row)
            and (finite_float(row.get("min_edge"), -math.inf) >= 0.0)
        )
    ][: max(1, int(getattr(args, "final_locked_glass_dls_source_count", 4)))]
    return source_rows
