"""Owned native execution and completed candidate result materialization."""
from __future__ import annotations

import math
import os
from lensde.runtime import processes as subprocess
import time
from pathlib import Path
from typing import Any, Iterable
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from lensde.engine import core as server
from . import commands as _module_commands
from . import contracts as _module_contracts
from . import files as _module_files
from . import manifest as _module_manifest
from . import paths as _module_paths
from . import policy as _module_policy


def execute_plain(
    command: list[str],
    case_dir: Path,
    timeout: int,
) -> tuple[int, float, bool]:
    _module_files.write_json(case_dir / "command.json", command)
    started = time.perf_counter()
    try:
        with server.native_io_paths.native_command(command, _module_paths.CPP_DIR, force_native=True) as native_command:
            completed = subprocess.run(
                native_command,
                cwd=_module_paths.CPP_DIR,
                env=os.environ.copy(),
                text=True,
                encoding="utf-8",
                errors="replace",
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=timeout if timeout > 0 else None,
            )
        returncode = completed.returncode
        stdout = completed.stdout
        stderr = completed.stderr
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        returncode = -9
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        timed_out = True
    wall = time.perf_counter() - started
    (case_dir / "stdout.log").write_text(str(stdout), encoding="utf-8", errors="replace")
    (case_dir / "stderr.log").write_text(str(stderr), encoding="utf-8", errors="replace")
    return returncode, wall, timed_out


def execute(
    command: list[str],
    case_dir: Path,
    snapshot_dir: Path,
    timeout: int,
    use_cuda: bool,
) -> tuple[int, float, bool]:
    _module_files.write_json(case_dir / "command.json", command)
    env = os.environ.copy()
    env["OPTILAND_CHECKPOINT_SNAPSHOT_DIR"] = str(snapshot_dir)
    if use_cuda:
        env["OPTILAND_DE_CUDA_REQUIRE"] = "1"
        env["OPTILAND_DE_CUDA_SUPPORT_DEBUG"] = "1"
    started = time.perf_counter()
    try:
        with server.native_io_paths.native_command(command, _module_paths.CPP_DIR, force_native=True) as native_command:
            completed = subprocess.run(
                native_command,
                cwd=_module_paths.CPP_DIR,
                env=env,
                text=True,
                encoding="utf-8",
                errors="replace",
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=timeout if timeout > 0 else None,
            )
        returncode = completed.returncode
        stdout = completed.stdout
        stderr = completed.stderr
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        returncode = -9
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        timed_out = True
    wall = time.perf_counter() - started
    (case_dir / "stdout.log").write_text(str(stdout), encoding="utf-8", errors="replace")
    (case_dir / "stderr.log").write_text(str(stderr), encoding="utf-8", errors="replace")
    return returncode, wall, timed_out


def run_candidate(
    item: dict[str, Any],
    index: int,
    args: Any,
    base_system: dict[str, Any],
    variables_doc: dict[str, Any],
    merit: dict[str, Any],
    glass_search_config: dict[str, Any] | None,
    fallback_command: list[str],
) -> dict[str, Any]:
    case_dir = server.native_io_paths.named_child(args.out_dir.resolve(), 'layer1_checkpoint_de400') / "runs" / str(item["run_id"])
    run_dir = case_dir / "run"
    snapshot_dir = case_dir / "checkpoint_candidate_snapshots"
    checkpoint_generation = min(200, max(1, int(args.checkpoint_de_generations) // 2))
    prior_path = case_dir / "result.json"
    if prior_path.is_file() and not args.force_rerun:
        prior = _module_files.read_json(prior_path)
        snapshots = _module_manifest.snapshot_merits(snapshot_dir / "snapshot_index.jsonl")
        if (
            str(prior.get("score_contract") or "") == _module_policy.CHECKPOINT_SCORE_CONTRACT
            and int(prior.get("returncode", -1)) == 0
            and checkpoint_generation in snapshots
            and int(args.checkpoint_de_generations) in snapshots
            and (run_dir / "system.json").is_file()
            and (run_dir / "score.json").is_file()
        ):
            return prior

    anchor = [float(value) for value in item["anchor_values"]]
    variables = list(variables_doc.get("variables") or [])
    source_system = server.apply_variable_values_to_system(
        base_system,
        variables,
        anchor,
        glass_search_config,
    )
    source_variables_doc = _module_commands.variables_for_anchor(variables_doc, anchor)
    case_dir.mkdir(parents=True, exist_ok=True)
    _module_files.write_json(case_dir / "system_input.json", source_system)
    _module_files.write_json(case_dir / "variables.json", source_variables_doc)
    _module_files.write_json(case_dir / "merit.json", merit)
    if glass_search_config is not None:
        _module_files.write_json(case_dir / "glass_search.json", glass_search_config)
    population_args = type("PopulationArgs", (), {
        "continuous_glass_neighbor_rate": 0.0,
        "continuous_glass_neighbor_topk": 1,
        "continuous_glass_neighbor_pick": 1,
        "continuous_glass_nd_noise": 0.0,
        "continuous_glass_vd_noise": 0.0,
    })()
    population_rows = feedback_cycles.build_population(
        system=source_system,
        variables_doc=source_variables_doc,
        glasses=[],
        material_mode="continuous",
        population=max(20, int(args.checkpoint_de_population)),
        seed=int(args.checkpoint_population_seed) + index * 100_000,
        scale=str(args.checkpoint_population_scale),
        args=population_args,
    )
    if len(population_rows) < max(20, int(args.checkpoint_de_population)):
        raise RuntimeError(
            f"{item['run_id']} produced only {len(population_rows)} unique population rows"
        )
    feedback_cycles.write_population(case_dir / "initial_population.csv", population_rows)
    source_command = _module_files.load_command(Path(str(item["outer_dir"])), fallback_command)
    command = _module_commands.build_de_command(
        source_command,
        exe=Path(args.checkpoint_de_exe).resolve(),
        case_dir=case_dir,
        population=len(population_rows),
        generations=int(args.checkpoint_de_generations),
        seed=int(args.checkpoint_optimizer_seed) + index * 10_000,
        checkpoint_generation=math.gcd(
            checkpoint_generation,
            int(args.checkpoint_de_generations),
        ),
        use_cuda=bool(args.checkpoint_de_use_cuda),
    )
    snapshot_dir.mkdir(parents=True, exist_ok=True)
    print(
        f"[checkpoint DE {index}] {item['run_id']} role={item['run_role']} "
        f"P{len(population_rows)}/G{int(args.checkpoint_de_generations)}",
        flush=True,
    )
    returncode, wall, timed_out = execute(
        command,
        case_dir,
        snapshot_dir,
        int(args.checkpoint_de_timeout),
        bool(args.checkpoint_de_use_cuda),
    )
    return finalize_candidate_result(
        item=item,
        args=args,
        case_dir=case_dir,
        run_dir=run_dir,
        snapshot_dir=snapshot_dir,
        returncode=returncode,
        wall=wall,
        timed_out=timed_out,
        execution_mode="serial_process",
        merit=merit,
        trace_coverage_epsilon=_module_contracts.command_materialization_trace_epsilon(command),
    )


def finalize_candidate_result(
    *,
    item: dict[str, Any],
    args: Any,
    case_dir: Path,
    run_dir: Path,
    snapshot_dir: Path,
    returncode: int,
    wall: float,
    timed_out: bool,
    execution_mode: str,
    merit: dict[str, Any],
    trace_coverage_epsilon: float = 0.0,
    batch_wall: float | None = None,
) -> dict[str, Any]:
    prior_path = case_dir / "result.json"
    checkpoint_generation = min(200, max(1, int(args.checkpoint_de_generations) // 2))
    snapshots = _module_manifest.snapshot_merits(snapshot_dir / "snapshot_index.jsonl")
    optimizer_midpoint = _module_files.finite(snapshots.get(checkpoint_generation))
    optimizer_final = _module_files.finite(snapshots.get(int(args.checkpoint_de_generations)))
    reference_merit = _module_files.finite(
        item.get("matched_rank1_rescore_merit"),
        _module_files.finite(item.get("matched_rank1_source_merit")),
    )
    score = _module_files.read_json(run_dir / "score.json") if (run_dir / "score.json").is_file() else {}
    final_merit = _module_files.finite(score.get("merit"))
    materialization_valid, invalid_reasons = _module_contracts.materialization_contract(
        score,
        merit,
        require_nonnegative_edge=True,
        trace_coverage_epsilon=trace_coverage_epsilon,
    )
    # Never divide a CPU reference by a CUDA merit. The first term measures
    # CPU materialization improvement; the second measures CUDA progress.
    s_score = (
        math.log(reference_merit / final_merit)
        + math.log(optimizer_midpoint / optimizer_final)
        if reference_merit > 0.0
        and final_merit > 0.0
        and optimizer_midpoint > 0.0
        and optimizer_final > 0.0
        else math.nan
    )
    final_system_path = run_dir / "system.json"
    final_optical_features: dict[str, Any] = {}
    if final_system_path.is_file():
        try:
            final_optical_features = server.optical_topology_features(_module_files.read_json(final_system_path))
        except Exception:
            final_optical_features = {}
    result = {
        "run_id": item["run_id"],
        "run_role": item["run_role"],
        "outer": int(item["outer"]),
        "state_id": item["state_id"],
        "selection_reason": item.get("selection_reason"),
        "source_generation": int(item.get("snapshot_generation") or 0),
        "source_merit": _module_files.finite(item.get("merit")),
        "source_quality_ratio": _module_files.finite(item.get("quality_ratio_to_source_rank1"), 1.0),
        "checkpoint_selection_score": _module_files.finite(item.get("potential_score"), math.nan),
        "global_selection_rank": item.get("global_selection_rank", ""),
        "topology_key": item.get("topology_key", ""),
        "source_topology_key": item.get("topology_key", ""),
        "final_optical_coarse_key": str(final_optical_features.get("coarse_key") or ""),
        "final_optical_fine_key": str(final_optical_features.get("fine_key") or ""),
        "semantic_key": item.get("semantic_key", ""),
        "descriptor_key": item.get("descriptor_key", ""),
        "matched_rank1_source_merit": _module_files.finite(item.get("matched_rank1_source_merit")),
        "matched_rank1_rescore_merit": reference_merit,
        "matched_rank1_merit": reference_merit,
        "checkpoint_generation": checkpoint_generation,
        "M_checkpoint": optimizer_midpoint,
        "M200": optimizer_midpoint if checkpoint_generation == 200 else math.nan,
        "M_final": final_merit,
        "M400": final_merit if int(args.checkpoint_de_generations) == 400 else math.nan,
        "optimizer_M_checkpoint": optimizer_midpoint,
        "optimizer_M_final": optimizer_final,
        "cpu_materialized_M_final": final_merit,
        "S_final": s_score,
        "S400": s_score if int(args.checkpoint_de_generations) == 400 else math.nan,
        "returncode": returncode,
        "timed_out": timed_out,
        "wall_seconds": wall,
        "execution_mode": execution_mode,
        "shared_batch_wall_seconds": batch_wall,
        "used_cuda": bool(args.checkpoint_de_use_cuda and snapshots),
        "score_contract": _module_policy.CHECKPOINT_SCORE_CONTRACT,
        "materialization_valid": materialization_valid,
        "materialization_invalid_reasons": invalid_reasons,
        "trace_ok": bool(score.get("trace_ok", False)),
        "trace_coverage": _module_files.finite(score.get("trace_coverage"), math.nan),
        "min_field_coverage": _module_files.finite(score.get("min_field_coverage"), math.nan),
        "min_chief_progress": _module_files.finite(score.get("min_chief_progress"), math.nan),
        "chief_ray_ok": score.get("chief_ray_ok"),
        "chief_ray_failed_fields": score.get("chief_ray_failed_fields"),
        "zero_coverage_off_axis_fields": score.get(
            "zero_coverage_off_axis_fields"
        ),
        "mean_spot": _module_files.finite(score.get("mean_spot"), math.nan),
        "max_spot": _module_files.finite(score.get("max_spot"), math.nan),
        "max_distortion": _module_files.finite(score.get("max_distortion"), math.nan),
        "constraint_group_error": _module_files.finite(score.get("constraint_group_error"), math.nan),
        "first_order_group_error": _module_files.finite(score.get("first_order_group_error"), math.nan),
        "image_group_error": _module_files.finite(score.get("image_group_error"), math.nan),
        "min_edge": _module_files.finite(score.get("min_edge"), math.nan),
        "run_dir": str(run_dir.resolve()),
        "case_dir": str(case_dir.resolve()),
    }
    _module_files.write_json(prior_path, result)
    if (
        returncode != 0
        or timed_out
        or not math.isfinite(optimizer_midpoint)
        or not math.isfinite(optimizer_final)
    ):
        raise RuntimeError(f"incomplete checkpoint DE continuation: {result}")
    return result
