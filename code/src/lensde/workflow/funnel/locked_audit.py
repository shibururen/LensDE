"""Audit final locked-glass DLS results against CPU/CUDA and resumed CPU contracts."""
from __future__ import annotations
from typing import Any
from .settings import FINAL_DLS_CPU_CUDA_CONTRACT
from .contracts import MaterialPolicy
from pathlib import Path
import csv
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .locked_contracts import final_locked_glass_cpu_cuda_consistency
from .common import finite_float
from .feedback_contracts import inherit_refine_like_aiming_from_system
from .common import read_csv
from .common import write_json
from lensde.workflow import run_material5_bridge_selection as material_bridge
from .common import read_json


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
    _version: MaterialPolicy,
) -> dict[str, Any]:
    audit_dir = pass_dir / "cpu_cuda_audit"
    audit_dir.mkdir(parents=True, exist_ok=True)
    population_path = audit_dir / "materialized_population.csv"
    variables = list(variables_doc.get("variables") or [])
    values = [feedback_cycles.variable_value(system, variable) for variable in variables]
    with population_path.open("w", encoding="utf-8", newline="") as stream:
        csv.writer(stream, lineterminator="\n").writerow(values)
    cuda_executable = (_version.cpp_dir / "build" / "lensde_de_cuda.exe").resolve()
    if not cuda_executable.is_file():
        raise FileNotFoundError(cuda_executable)
    command = [
        str(cuda_executable),
        "--aiming-best-effort",
        "--clear-aperture-update",
        "--sync-stop-aperture",
        "--clear-rings",
        "4",
        "--no-vignetting-merit-aperture-envelope-experimental",
        "--constraint-handling",
        "feasibility_rules",
        "--system",
        str((pass_dir / "system.json").resolve()),
        "--variables",
        str(variables_path.resolve()),
        "--merit",
        str(merit_path.resolve()),
        "--initial-population",
        str(population_path.resolve()),
        "--score-population-only",
        "--rays",
        str(sampling["num_rays"]),
        "--arms",
        str(sampling["pupil_arms"]),
        "--distribution",
        str(sampling["pupil_distribution"]),
        "--de-cuda-min-batch",
        "1",
        "--out-dir",
        str(audit_dir.resolve()),
    ]
    command = inherit_refine_like_aiming_from_system(command, system)
    write_json(audit_dir / "command.json", command)
    run_result = feedback_cycles.run_command(
        command, _version.cpp_dir, audit_dir / "run.log", timeout, child_workers
    )
    rows = (
        read_csv(audit_dir / "population_scores.csv")
        if int(finite_float(run_result.get("returncode"), -1)) == 0
        else []
    )
    if not rows:
        report = {
            "contract": FINAL_DLS_CPU_CUDA_CONTRACT,
            "consistent": False,
            "reasons": ["cuda_score_failed"],
            "run_result": run_result,
        }
    else:
        report = final_locked_glass_cpu_cuda_consistency(
            cpu_score, rows[0], min_trace=min_trace, min_field=min_field
        )
        report["run_result"] = run_result
    write_json(audit_dir / "report.json", report)
    if raise_on_inconsistent and (not bool(report.get("consistent"))):
        raise RuntimeError(
            f"final locked-glass CPU/CUDA audit failed in {pass_dir}: "
            + ", ".join((str(item) for item in report.get("reasons") or []))
        )
    return report


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
    _version: MaterialPolicy,
) -> dict[str, Any]:
    """Rescore a cached pass with the current CPU executable before reuse."""
    audit_dir = pass_dir / "resume_cpu_audit"
    audit_dir.mkdir(parents=True, exist_ok=True)
    cpu_executable = (_version.cpp_dir / "build" / "lensde_cpu.exe").resolve()
    if not cpu_executable.is_file():
        raise FileNotFoundError(cpu_executable)
    command = [
        str(cpu_executable),
        "--aiming-best-effort",
        "--clear-aperture-update",
        "--sync-stop-aperture",
        "--clear-rings",
        "4",
        "--no-vignetting-merit-aperture-envelope-experimental",
        "--constraint-handling",
        "feasibility_rules",
        "--system",
        str((pass_dir / "system.json").resolve()),
        "--variables",
        str(variables_path.resolve()),
        "--merit",
        str(merit_path.resolve()),
        "--rays",
        str(sampling["num_rays"]),
        "--arms",
        str(sampling["pupil_arms"]),
        "--distribution",
        str(sampling["pupil_distribution"]),
        "--out-dir",
        str(audit_dir.resolve()),
        "--score-only",
        "--cpu",
    ]
    command = inherit_refine_like_aiming_from_system(command, read_json(pass_dir / "system.json"))
    write_json(audit_dir / "command.json", command)
    run_result = feedback_cycles.run_command(
        command, _version.cpp_dir, audit_dir / "run.log", timeout, child_workers
    )
    current_score_path = audit_dir / "score.json"
    if int(finite_float(run_result.get("returncode"), -1)) != 0 or not current_score_path.is_file():
        report = {
            "contract": "saved_cpu_vs_current_cpu_v1",
            "consistent": False,
            "reasons": ["current_cpu_score_failed"],
            "run_result": run_result,
            "executable": material_bridge.executable_identity(cpu_executable),
        }
    else:
        current_score = read_json(current_score_path)
        report = final_locked_glass_cpu_cuda_consistency(
            saved_score, current_score, min_trace=min_trace, min_field=min_field
        )
        report["contract"] = "saved_cpu_vs_current_cpu_v1"
        report["comparison"] = "saved_cpu_score_vs_current_cpu_rescore"
        report["reasons"] = [
            str(reason).replace("cuda_", "current_cpu_") for reason in report.get("reasons") or []
        ]
        report["consistent"] = not report["reasons"]
        report["run_result"] = run_result
        report["executable"] = material_bridge.executable_identity(cpu_executable)
    write_json(audit_dir / "report.json", report)
    return report
