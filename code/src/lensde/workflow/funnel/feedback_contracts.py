"""Resolve fixed-glass source, command, parent and admission contracts."""
from __future__ import annotations
from typing import Any
from pathlib import Path
import copy
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from lensde.workflow import run_post_de_basin_refine_funnel as material_funnel
from .common import read_json
from .common import finite_float
import math
import argparse
from lensde.workflow import run_material5_bridge_selection as material_bridge
from .contracts import MaterialPolicy
from .common import _row_truthy
from lensde.engine import core as server
from .common import write_json
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .settings import LOCKED_GLASS_MATERIAL_VARIABLE_KINDS
import hashlib
import json


def glass_scout_source_system(
    source_dir: Path, policy_context_dir: Path | None = None
) -> dict[str, Any]:
    """Load a continuous source while retaining its per-surface glass policy.

    The C++ materializer writes the optical state to ``system.json`` but does
    not carry the Python-side ``glass_search.surface_configs`` metadata.  The
    corresponding input system is normally retained beside the materialized
    state and is the authoritative source for the strict library policy.  A
    C++-materialized fallback parent can lack that metadata entirely, so the
    prepared basin context is the final authority.  Merge only policy metadata
    into the current geometry/material state.
    """
    system_path = feedback_cycles.file_from_source(source_dir, "system.json")
    system = read_json(system_path)
    input_candidates = [
        source_dir / "input_system.json",
        source_dir / "system_input.json",
        source_dir.parent / "input_system.json",
        source_dir.parent / "system_input.json",
    ]
    if policy_context_dir is not None:
        context = policy_context_dir.resolve()
        input_candidates.extend(
            [context / "input_system.json", context / "system_input.json", context / "system.json"]
        )
    for input_path in input_candidates:
        if not input_path.is_file():
            continue
        input_system = read_json(input_path)
        policy_found = False
        for key in ("glass_search", "post_asphere_plastic_glass_search"):
            if key in input_system:
                system[key] = copy.deepcopy(input_system[key])
                policy_found = True
        if policy_found:
            break
    variable_contract_paths: list[Path] = []
    if policy_context_dir is not None:
        variable_contract_paths.append(policy_context_dir.resolve() / "variables.json")
    variable_contract_paths.extend(
        [source_dir / "variables.json", source_dir.parent / "variables.json"]
    )
    for variables_path in variable_contract_paths:
        if not variables_path.is_file():
            continue
        variables_doc = read_json(variables_path)
        system = material_funnel.apply_material_variable_search_contract(
            system, list(variables_doc.get("variables") or [])
        )
        break
    return system


def materialize_glass_source_paths(
    system: dict[str, Any], glass_search: dict[str, Any]
) -> dict[str, Any]:
    """Attach the selected catalog file to each materialized scout glass."""
    out = copy.deepcopy(system)
    surfaces = list(out.get("surfaces") or [])
    for group in list(glass_search.get("groups") or []):
        surface_index = int(group.get("surface_index", -1))
        if not 0 <= surface_index < len(surfaces):
            continue
        chosen = surfaces[surface_index].get("material_after") or {}
        chosen_name = str(chosen.get("name") or "").casefold()
        chosen_reference = str(chosen.get("reference") or chosen.get("name") or "").casefold()
        chosen_nd = finite_float(chosen.get("nd"), math.nan)
        chosen_vd = finite_float(chosen.get("vd"), math.nan)
        for candidate in list(group.get("candidates") or []):
            candidate_name = str(candidate.get("name") or "").casefold()
            candidate_reference = str(
                candidate.get("reference") or candidate.get("name") or ""
            ).casefold()
            candidate_nd = finite_float(candidate.get("nd"), math.nan)
            candidate_vd = finite_float(candidate.get("vd"), math.nan)
            if (
                candidate_name == chosen_name
                and candidate_reference == chosen_reference
                and math.isfinite(chosen_nd)
                and math.isfinite(chosen_vd)
                and (abs(candidate_nd - chosen_nd) <= 1e-08)
                and (abs(candidate_vd - chosen_vd) <= 1e-06)
            ):
                source_path = str(candidate.get("source_path") or candidate.get("path") or "")
                if source_path:
                    chosen["source_path"] = source_path
                break
    return out


def set_command_arg(command: list[str], flag: str, value: Any) -> None:
    text = str(value)
    if flag in command:
        index = command.index(flag)
        if index + 1 < len(command):
            command[index + 1] = text
        else:
            command.append(text)
    else:
        command.extend([flag, text])


def ensure_command_flag(command: list[str], flag: str) -> None:
    if flag not in command:
        command.append(flag)


def add_refine_like_feedback_args(command: list[str], args: argparse.Namespace) -> list[str]:
    out = list(command)
    refine_de_gen = int(getattr(args, "refine_de_gen", getattr(args, "generations", 420)))
    set_command_arg(out, "--gen", refine_de_gen)
    set_command_arg(out, "--hybrid-de-gen", refine_de_gen)
    set_command_arg(out, "--hybrid-outer", int(getattr(args, "refine_outer", 80)))
    set_command_arg(out, "--hybrid-parents", int(getattr(args, "hybrid_parents", 10)))
    set_command_arg(out, "--hybrid-local-iter", 0)
    set_command_arg(out, "--hybrid-polish-workers", 0)
    set_command_arg(out, "--polish-candidates", 0)
    set_command_arg(out, "--polish-refill-max-extra", 0)
    ensure_command_flag(out, "--hybrid-no-polish")
    set_command_arg(out, "--local-method", str(getattr(args, "method", "trf")))
    set_command_arg(out, "--local-bounds-mode", str(getattr(args, "local_bounds_mode", "safety")))
    set_command_arg(
        out, "--local-residual-mode", str(getattr(args, "local_residual_mode", "expanded"))
    )
    set_command_arg(out, "--local-lambda", str(getattr(args, "local_lambda", "0.03")))
    set_command_arg(out, "--local-fd", str(getattr(args, "local_fd", "0.0001")))
    set_command_arg(out, "--local-tol", str(getattr(args, "local_tol", "0.00008")))
    set_command_arg(out, "--local-max-reject", int(getattr(args, "local_max_reject", 100)))
    set_command_arg(out, "--optimizer-internal-scaling", "edge_sag_normalized")
    lpsr_horizon_epochs = int(getattr(args, "refine_lpsr_horizon_epochs", 24))
    if lpsr_horizon_epochs > 0:
        set_command_arg(
            out, "--adaptive-lpsr-horizon-generations", lpsr_horizon_epochs * refine_de_gen
        )
    if bool(getattr(args, "refine_early_stop", True)):
        ensure_command_flag(out, "--adaptive-early-stop")
        set_command_arg(out, "--adaptive-early-stop-metric", "best_merit")
        set_command_arg(
            out,
            "--adaptive-early-stop-min-epochs",
            int(getattr(args, "refine_early_stop_min_epochs", 4)),
        )
        set_command_arg(
            out, "--adaptive-early-stop-window", int(getattr(args, "refine_early_stop_window", 3))
        )
        set_command_arg(out, "--adaptive-early-stop-min-topologies", 0)
        set_command_arg(
            out,
            "--adaptive-early-stop-min-rel-improve",
            float(getattr(args, "refine_early_stop_min_rel_improve", 0.01)),
        )
        set_command_arg(
            out,
            "--adaptive-early-stop-min-abs-improve",
            float(getattr(args, "refine_early_stop_min_abs_improve", 0.01)),
        )
        set_command_arg(
            out,
            "--adaptive-early-stop-min-lpsr-progress",
            float(getattr(args, "refine_early_stop_min_lpsr_progress", 0.35)),
        )
        set_command_arg(
            out,
            "--adaptive-early-stop-tail-epochs",
            int(getattr(args, "refine_early_stop_tail_epochs", 0)),
        )
    return out


def configure_refine_like_feedback_evaluator(
    command: list[str], args: argparse.Namespace
) -> list[str]:
    return feedback_cycles.configure_de_evaluator(command, args)


def inherit_refine_like_aiming_from_system(command: list[str], system: dict[str, Any]) -> list[str]:
    """Use the serialized optimizer handoff, never its APG measurement command."""
    out = list(command)
    ray_aiming = system.get("ray_aiming")
    aiming = ray_aiming if isinstance(ray_aiming, dict) else {}
    mode = material_bridge.system_aiming_mode(system)
    set_command_arg(out, "--aiming", mode)
    set_command_arg(out, "--aiming-iter", int(finite_float(aiming.get("max_iter"), 20)))
    set_command_arg(
        out, "--aiming-continuation-steps", int(finite_float(aiming.get("continuation_steps"), 8))
    )
    if not bool(aiming.get("require_convergence", False)):
        out = [token for token in out if token != "--aiming-require-convergence"]
    return out


def score_refine_like_parent_on_locked_contract(
    prepared: dict[str, Any], args: argparse.Namespace, *, _version: MaterialPolicy
) -> dict[str, Any]:
    """Score the exact DE row-0 parent with the locked 6x8 CPU contract.

    GlassChoice Scout and locked DE deliberately use different pupil sampling.
    Comparing the locked result with the Scout score therefore mixes two merit
    functions.  This one-row preflight supplies the true parent for elitist
    admission and costs one CPU evaluation per island.
    """
    case_root = Path(prepared["case_dir"])
    population = server.read_population_csv(Path(prepared["population_path"]))
    if not population:
        raise RuntimeError(f"fixed-glass seed population is empty: {prepared['population_path']}")
    parent_population = [population[0]]
    parent_population_path = case_root / "locked_parent_population.csv"
    feedback_cycles.write_population(parent_population_path, parent_population)
    parent_case_dir = server.native_io_paths.named_child(case_root, "locked_parent_cpu_score")
    parent_score_dir = parent_case_dir / "score"
    cpu_exe = Path(
        str(getattr(args, "exe", None) or _version.cpp_dir / "build" / "lensde_cpu.exe")
    ).resolve()
    if not cpu_exe.is_file():
        raise FileNotFoundError(f"fixed-glass locked-parent CPU executable not found: {cpu_exe}")
    command = material_bridge.cpu_score_command(list(prepared["command"]), cpu_exe, parent_case_dir)
    set_command_arg(command, "--initial-population", parent_population_path.resolve())
    set_command_arg(command, "--out-dir", parent_score_dir.resolve())
    if "--score-population-only" not in command:
        command.append("--score-population-only")
    fingerprint = material_bridge.bridge_score_fingerprint(
        system=prepared["system"],
        variables_doc=prepared["variables_doc"],
        merit=prepared["merit"],
        population=parent_population,
    )
    run_result = material_bridge.execute_score_cached(
        command=command,
        case_dir=parent_case_dir,
        score_path=parent_score_dir / "population_scores.csv",
        fingerprint=fingerprint,
        timeout=int(getattr(args, "timeout", 0)),
    )
    parent_score = material_bridge.score_population_row(parent_score_dir / "population_scores.csv")
    if (
        int(finite_float(run_result.get("returncode"), -1)) != 0
        or _row_truthy(run_result.get("timed_out"))
        or (not material_bridge.score_population_complete(parent_score))
    ):
        raise RuntimeError(
            f"fixed-glass locked-parent CPU score failed: case={parent_case_dir}, returncode={run_result.get('returncode')}, timed_out={run_result.get('timed_out')}"
        )
    score_path = case_root / "locked_parent_score.json"
    write_json(score_path, parent_score)
    write_json(
        case_root / "locked_parent_score_contract.json",
        {
            "contract": "fixed_glass_locked_sampling_parent_v1",
            "source_score": prepared["parent_score"],
            "locked_score": parent_score,
            "population_row": 0,
            "sampling": {
                "num_rays": prepared["merit"].get("num_rays"),
                "pupil_arms": prepared["merit"].get("pupil_arms"),
                "pupil_distribution": prepared["merit"].get("pupil_distribution"),
            },
            "cpu_score_cache_hit": bool(run_result.get("cache_hit", False)),
        },
    )
    prepared["source_parent_score"] = prepared["parent_score"]
    prepared["parent_score"] = parent_score
    prepared["locked_parent_score_path"] = score_path
    return parent_score


def fixed_glass_feedback_admission(
    row: dict[str, Any], args: argparse.Namespace, *, parent_score: dict[str, Any] | None = None
) -> tuple[bool, list[str]]:
    """Apply structural validity and scalar-Merit elitism for locked DE."""
    reasons: list[str] = []
    if int(finite_float(row.get("returncode"), -1)) != 0:
        reasons.append("nonzero_returncode")
    if _row_truthy(row.get("timed_out")):
        reasons.append("timed_out")
    merit = finite_float(row.get("merit"), math.inf)
    if not math.isfinite(merit) or merit >= 1000000000000.0:
        reasons.append("failed_or_nonfinite_merit")
    trace_coverage = finite_float(row.get("trace_coverage"), 0.0)
    field_coverage = finite_float(row.get("min_field_coverage"), 0.0)
    if not _row_truthy(row.get("trace_ok")):
        reasons.append("trace_not_ok")
    min_trace = float(getattr(args, "min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE))
    min_field = float(getattr(args, "min_field_coverage", min_trace))
    if trace_coverage < min_trace or field_coverage < min_field:
        reasons.append("trace_coverage_below_contract")
    if not server.vignetting_handoff_policy.chief_ray_usable(row):
        reasons.append("chief_ray_below_contract")
    min_edge = finite_float(row.get("min_edge"), -math.inf)
    if not math.isfinite(min_edge) or min_edge < 0.0:
        reasons.append("negative_or_nonfinite_edge_thickness")
    constraint = finite_float(row.get("constraint_group_error"), math.inf)
    if not math.isfinite(constraint):
        reasons.append("nonfinite_constraint_error")
    if parent_score is not None:
        parent_merit = finite_float(parent_score.get("merit"), math.inf)
        merit_tolerance = 1e-09 * max(1.0, abs(parent_merit))
        if math.isfinite(parent_merit) and merit > parent_merit + merit_tolerance:
            reasons.append("merit_regressed_vs_parent")
    if "cpu_cuda_materialization_consistent" in row and (
        not _row_truthy(row.get("cpu_cuda_materialization_consistent"))
    ):
        reasons.append("cpu_cuda_materialization_inconsistent")
    return (not reasons, reasons)


def cpu_cuda_materialization_consistency(
    result: dict[str, Any], relative_difference: float, tolerance: float
) -> bool:
    """Read the numerical parity contract without folding in feasibility gates."""
    if "cpu_cuda_materialization_consistent" in result:
        return _row_truthy(result.get("cpu_cuda_materialization_consistent"))
    return math.isfinite(relative_difference) and relative_difference <= tolerance


def materialize_locked_glass_parent_snapshot(
    source_dir: Path, snapshot_dir: Path
) -> dict[str, Any]:
    """Persist a parent-guard result with a genuinely fixed material contract.

    A GlassChoice Scout output contains materialized catalog glasses in
    ``system.json`` but may retain the now-stale ``glass_choice`` variables.
    Pointing a parent-guard row directly at that directory leaks those variables
    into later locked-glass stages.  Copy the useful parent artifacts, then
    overwrite every optimization input with a canonical geometry-only contract.
    """
    source_dir = source_dir.resolve()
    snapshot_dir = snapshot_dir.resolve()
    source_system = read_json(feedback_cycles.file_from_source(source_dir, "system.json"))
    source_variables_doc = read_json(feedback_cycles.file_from_source(source_dir, "variables.json"))
    locked_system, locked_variables_doc, _glass_search = feedback_cycles.locked_glass_problem(
        source_dir, source_system
    )
    locked_system = copy.deepcopy(locked_system)
    locked_system.pop("glass_search", None)
    locked_system.pop("post_asphere_plastic_glass_search", None)
    source_variables = source_variables_doc.get("variables", []) or []
    removed_variables = [
        {
            "kind": str(variable.get("kind") or "").lower(),
            "index": variable.get("index"),
            "term": variable.get("term"),
        }
        for variable in source_variables
        if str(variable.get("kind") or "").lower() in LOCKED_GLASS_MATERIAL_VARIABLE_KINDS
    ]
    remaining_material_variables = [
        variable
        for variable in locked_variables_doc.get("variables", []) or []
        if str(variable.get("kind") or "").lower() in LOCKED_GLASS_MATERIAL_VARIABLE_KINDS
    ]
    if remaining_material_variables:
        raise RuntimeError("locked-glass parent snapshot still contains material variables")
    copied_files = server.copy_candidate_artifacts(source_dir, snapshot_dir)
    for stale_path in (snapshot_dir / "glass_search.json",):
        stale_path.unlink(missing_ok=True)
    write_json(snapshot_dir / "system.json", locked_system)
    write_json(snapshot_dir / "system_input.json", locked_system)
    write_json(snapshot_dir / "input_system.json", locked_system)
    write_json(snapshot_dir / "variables.json", locked_variables_doc)
    source_score = read_json(feedback_cycles.file_from_source(source_dir, "score.json"))
    write_json(snapshot_dir / "score.json", source_score)
    if not (snapshot_dir / "initial_score.json").is_file():
        write_json(snapshot_dir / "initial_score.json", source_score)
    source_merit = read_json(feedback_cycles.file_from_source(source_dir, "merit.json"))
    write_json(snapshot_dir / "merit.json", source_merit)
    contract = {
        "role": "fixed_glass_parent_guard",
        "source_dir": str(source_dir),
        "snapshot_dir": str(snapshot_dir),
        "copied_files": copied_files,
        "source_variable_count": len(source_variables),
        "locked_variable_count": len(locked_variables_doc.get("variables", []) or []),
        "removed_material_variable_count": len(removed_variables),
        "removed_material_variables": removed_variables,
        "glass_search_removed": True,
    }
    write_json(snapshot_dir / "parent_guard_contract.json", contract)
    return contract


def fixed_glass_parent_guard_row(
    attempt: dict[str, Any], args: argparse.Namespace
) -> dict[str, Any]:
    """Return a canonical locked snapshot when a DE attempt is not admissible."""
    source_dir = Path(str(attempt.get("source_dir") or "")).resolve()
    locked_parent_score_path_text = str(attempt.get("locked_parent_score_path") or "").strip()
    source_score = (
        read_json(Path(locked_parent_score_path_text))
        if locked_parent_score_path_text and Path(locked_parent_score_path_text).is_file()
        else read_json(feedback_cycles.file_from_source(source_dir, "score.json"))
    )
    source_system = read_json(feedback_cycles.file_from_source(source_dir, "system.json"))
    merit = read_json(feedback_cycles.file_from_source(source_dir, "merit.json"))
    case_dir_text = str(attempt.get("case_dir") or "").strip()
    case_dir = (
        Path(case_dir_text)
        if case_dir_text
        else Path(str(attempt.get("out_dir") or source_dir)).parent
    )
    parent_guard_contract = materialize_locked_glass_parent_snapshot(
        source_dir, case_dir / "parent_guard"
    )
    parent_guard_dir = Path(parent_guard_contract["snapshot_dir"])
    locked_merit_path = case_dir / "merit.json"
    if locked_parent_score_path_text:
        write_json(parent_guard_dir / "score.json", source_score)
        write_json(parent_guard_dir / "initial_score.json", source_score)
    if locked_merit_path.is_file():
        merit = read_json(locked_merit_path)
        write_json(parent_guard_dir / "merit.json", merit)
    row = feedback_cycles.score_to_row(
        cycle=int(finite_float(attempt.get("cycle"), 1)),
        source_rank=int(finite_float(attempt.get("source_rank"), 1)),
        source_dir=source_dir,
        source_name=source_dir.name,
        material_mode="locked_glass",
        algorithm="parent_guard",
        parent_score=source_score,
        score=source_score,
        system=source_system,
        run_result={"returncode": 0, "timed_out": False, "seconds": 0.0},
        out_dir=parent_guard_dir,
        args=args,
        merit=merit,
    )
    admitted, reasons = fixed_glass_feedback_admission(row, args)
    row.update(
        {
            "admission_ok": admitted,
            "admission_reasons": ";".join(reasons),
            "selected_from": "locked_glass_parent_guard",
            "parent_retained": True,
            "feedback_attempt_out_dir": attempt.get("out_dir"),
            "feedback_attempt_merit": attempt.get("merit"),
            "feedback_attempt_constraint_group_error": attempt.get("constraint_group_error"),
            "feedback_attempt_min_edge": attempt.get("min_edge"),
            "feedback_attempt_rejection_reasons": attempt.get("admission_reasons"),
            "parent_guard_source_dir": str(source_dir),
            "parent_guard_snapshot_dir": str(parent_guard_dir),
            "parent_guard_contract": parent_guard_contract,
            "locked_parent_score_path": locked_parent_score_path_text,
        }
    )
    write_json(parent_guard_dir / "summary.json", row)
    return row


def select_fixed_glass_feedback_attempt(
    attempt: dict[str, Any], args: argparse.Namespace
) -> dict[str, Any]:
    parent_score = {
        "merit": attempt.get("parent_merit"),
        "constraint_group_error": attempt.get("parent_constraint_group_error"),
    }
    admitted, reasons = fixed_glass_feedback_admission(attempt, args, parent_score=parent_score)
    attempt["admission_ok"] = admitted
    attempt["admission_reasons"] = ";".join(reasons)
    attempt["selected_from"] = "locked_glass_de" if admitted else "rejected"
    attempt["parent_retained"] = not admitted
    if admitted:
        return dict(attempt)
    return fixed_glass_parent_guard_row(attempt, args)


def canonical_contract_fingerprint(payload: Any) -> str:
    encoded = json.dumps(payload, sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode(
        "utf-8"
    )
    return hashlib.sha256(encoded).hexdigest()


def ordered_compatibility_cohorts(keys: list[tuple[Any, ...]]) -> list[list[int]]:
    grouped: dict[tuple[Any, ...], list[int]] = {}
    for index, key in enumerate(keys):
        grouped.setdefault(key, []).append(index)
    return list(grouped.values())
