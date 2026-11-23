"""Validated multi-island policy and ordered broker execution."""
from __future__ import annotations

import os
from lensde.runtime import processes as subprocess
import time
from pathlib import Path
from typing import Any, Iterable
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from lensde.engine import core as server
from . import commands as _module_commands
from . import contracts as _module_contracts
from . import execution as _module_execution
from . import files as _module_files
from . import manifest as _module_manifest
from . import paths as _module_paths
from . import policy as _module_policy


def run_candidates_batched_experimental(
    items: list[dict[str, Any]],
    args: Any,
    base_system: dict[str, Any],
    variables_doc: dict[str, Any],
    merit: dict[str, Any],
    glass_search_config: dict[str, Any] | None,
    fallback_command: list[str],
) -> list[dict[str, Any]]:
    """Run checkpoint islands through the validated E2 broker behind an opt-in flag."""
    layer_dir = server.native_io_paths.named_child(args.out_dir.resolve(), 'layer1_checkpoint_de400')
    experimental_exe = Path(
        getattr(
            args,
            "checkpoint_multi_island_exe",
            _module_paths.CPP_DIR / "build" / "lensde_multi_island.exe",
        )
    ).resolve()
    if not experimental_exe.is_file():
        raise FileNotFoundError(
            f"experimental multi-island executable not found: {experimental_exe}"
        )
    if not bool(args.checkpoint_de_use_cuda):
        raise ValueError("experimental multi-island checkpoint execution requires CUDA")

    prepared: list[dict[str, Any]] = []
    cached_results: dict[str, dict[str, Any]] = {}
    variables = list(variables_doc.get("variables") or [])
    population_args = type("PopulationArgs", (), {
        "continuous_glass_neighbor_rate": 0.0,
        "continuous_glass_neighbor_topk": 1,
        "continuous_glass_neighbor_pick": 1,
        "continuous_glass_nd_noise": 0.0,
        "continuous_glass_vd_noise": 0.0,
    })()
    for index, item in enumerate(items, 1):
        case_dir = layer_dir / "runs" / str(item["run_id"])
        run_dir = case_dir / "run"
        snapshot_dir = case_dir / "checkpoint_candidate_snapshots"
        prior_path = case_dir / "result.json"
        checkpoint_generation = min(
            200, max(1, int(args.checkpoint_de_generations) // 2)
        )
        if prior_path.is_file() and not args.force_rerun:
            prior = _module_files.read_json(prior_path)
            snapshots = _module_manifest.snapshot_merits(snapshot_dir / "snapshot_index.jsonl")
            if (
                str(prior.get("score_contract") or "")
                == _module_policy.CHECKPOINT_SCORE_CONTRACT
                and int(prior.get("returncode", -1)) == 0
                and checkpoint_generation in snapshots
                and int(args.checkpoint_de_generations) in snapshots
                and (run_dir / "system.json").is_file()
                and (run_dir / "score.json").is_file()
            ):
                cached_results[str(item["run_id"])] = prior
                continue
        anchor = [float(value) for value in item["anchor_values"]]
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
        population_path = case_dir / "initial_population.csv"
        feedback_cycles.write_population(population_path, population_rows)
        snapshot_dir.mkdir(parents=True, exist_ok=True)
        source_command = _module_files.load_command(Path(str(item["outer_dir"])), fallback_command)
        allow_near_trace, trace_coverage_epsilon = (
            _module_contracts.checkpoint_adaptive_trace_contract(source_command)
        )
        prepared.append(
            {
                "index": index,
                "item": item,
                "case_dir": case_dir,
                "run_dir": run_dir,
                "snapshot_dir": snapshot_dir,
                "source_system": source_system,
                "source_command": source_command,
                "allow_near_trace_infeasible": allow_near_trace,
                "trace_coverage_epsilon": trace_coverage_epsilon,
                "population_path": population_path,
                "population": len(population_rows),
                "optimizer_seed": int(args.checkpoint_optimizer_seed) + index * 10_000,
            }
        )

    if not prepared:
        return [cached_results[str(item["run_id"])] for item in items]

    batch_dir = layer_dir / "multi_island_experimental"
    batch_dir.mkdir(parents=True, exist_ok=True)
    jobs_path = batch_dir / "jobs.tsv"
    header = [
        "island_id", "system", "variables", "merit", "population_file",
        "snapshot_dir", "output_dir", "seed", "population", "generations",
        "profile", "constraint_handling", "allow_near_trace_infeasible",
        "trace_coverage_epsilon",
    ]
    lines = ["\t".join(header)]
    for row in prepared:
        case_dir = row["case_dir"]
        cells = [
            int(row["index"]),
            case_dir / "system_input.json",
            case_dir / "variables.json",
            case_dir / "merit.json",
            row["population_path"],
            row["snapshot_dir"],
            case_dir / "experimental_result",
            int(row["optimizer_seed"]),
            int(row["population"]),
            int(args.checkpoint_de_generations),
            "checkpoint",
            "scalar_merit",
            "true" if row["allow_near_trace_infeasible"] else "false",
            f"{float(row['trace_coverage_epsilon']):.17g}",
        ]
        lines.append("\t".join(str(value) for value in cells))
    jobs_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    command = [
        str(experimental_exe),
        "--jobs", str(jobs_path),
        "--batched",
        "--coalesce-us", str(
            max(0, int(getattr(args, "checkpoint_multi_island_coalesce_us", 1000)))
        ),
        "--broker-stats", str(batch_dir / "broker_stats.json"),
    ]
    _module_files.write_json(batch_dir / "command.json", command)
    env = os.environ.copy()
    env["OPTILAND_DE_CUDA_REQUIRE"] = "1"
    timeout = int(args.checkpoint_de_timeout)
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
                timeout=(timeout * len(prepared)) if timeout > 0 else None,
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
    batch_wall = time.perf_counter() - started
    (batch_dir / "stdout.log").write_text(str(stdout), encoding="utf-8", errors="replace")
    (batch_dir / "stderr.log").write_text(str(stderr), encoding="utf-8", errors="replace")
    _module_files.write_json(
        batch_dir / "run_result.json",
        {
            "returncode": returncode,
            "timed_out": timed_out,
            "wall_seconds": batch_wall,
            "island_count": len(prepared),
            "checkpoint_multi_island_default_enabled": True,
            "production_default_changed": True,
        },
    )
    if returncode != 0 or timed_out:
        raise RuntimeError(
            f"experimental multi-island checkpoint run failed: rc={returncode}, "
            f"timed_out={timed_out}, logs={batch_dir}"
        )

    results_by_id: dict[str, dict[str, Any]] = dict(cached_results)
    for row in prepared:
        case_dir = row["case_dir"]
        experimental_result_path = case_dir / "experimental_result" / "result.json"
        if not experimental_result_path.is_file():
            raise RuntimeError(f"missing experimental result: {experimental_result_path}")
        experimental_result = _module_files.read_json(experimental_result_path)
        best_values = [float(value) for value in experimental_result.get("best_values", [])]
        if len(best_values) != len(variables):
            raise RuntimeError(
                f"{row['item']['run_id']} returned {len(best_values)} values for "
                f"{len(variables)} variables"
            )
        best_system = server.apply_variable_values_to_system(
            row["source_system"], variables, best_values, glass_search_config
        )
        best_system_path = case_dir / "best_system_input.json"
        best_variables_path = case_dir / "best_variables.json"
        _module_files.write_json(best_system_path, best_system)
        _module_files.write_json(best_variables_path, _module_commands.variables_for_anchor(variables_doc, best_values))
        score_dir = case_dir / "final_score"
        score_dir.mkdir(parents=True, exist_ok=True)
        score_command = _module_commands.build_score_command(
            row["source_command"],
            exe=Path(args.checkpoint_de_exe).resolve(),
            case_dir=case_dir,
            use_cuda=bool(args.checkpoint_de_use_cuda),
        )
        feedback_cycles.set_command_arg(score_command, "--system", str(best_system_path))
        feedback_cycles.set_command_arg(score_command, "--variables", str(best_variables_path))
        feedback_cycles.set_command_arg(score_command, "--out-dir", str(row["run_dir"]))
        score_rc, _, score_timed_out = _module_execution.execute_plain(score_command, score_dir, timeout)
        if score_rc != 0 or score_timed_out:
            raise RuntimeError(
                f"final score failed for {row['item']['run_id']}: "
                f"rc={score_rc}, timed_out={score_timed_out}"
            )
        result = _module_execution.finalize_candidate_result(
            item=row["item"],
            args=args,
            case_dir=case_dir,
            run_dir=row["run_dir"],
            snapshot_dir=row["snapshot_dir"],
            returncode=0,
            wall=batch_wall / max(1, len(prepared)),
            timed_out=False,
            execution_mode="batched_multi_island_experimental",
            merit=merit,
            trace_coverage_epsilon=_module_contracts.command_materialization_trace_epsilon(
                row["source_command"]
            ),
            batch_wall=batch_wall,
        )
        results_by_id[str(row["item"]["run_id"])] = result
    return [results_by_id[str(item["run_id"])] for item in items]


def checkpoint_multi_island_policy(args: Any, island_count: int) -> dict[str, Any]:
    validated = dict(_module_policy.CHECKPOINT_MULTI_ISLAND_VALIDATED_CONTRACT)
    actual = {
        "island_count": int(island_count),
        "population": int(args.checkpoint_de_population),
        "generations": int(args.checkpoint_de_generations),
    }
    requested = bool(getattr(args, "checkpoint_multi_island_experimental", True))
    executable = Path(
        getattr(
            args,
            "checkpoint_multi_island_exe",
            _module_paths.CPP_DIR / "build" / "lensde_multi_island.exe",
        )
    ).resolve()
    fallback_reasons: list[str] = []
    if not (
        int(validated["minimum_island_count"])
        <= actual["island_count"]
        <= int(validated["maximum_island_count"])
    ):
        fallback_reasons.append(
            "unvalidated island count: expected "
            f"{validated['minimum_island_count']}..{validated['maximum_island_count']}, "
            f"got {actual['island_count']}"
        )
    for key in ("population", "generations"):
        if actual[key] != int(validated[key]):
            fallback_reasons.append(
                f"unvalidated {key}: expected {validated[key]}, got {actual[key]}"
            )
    if not bool(args.checkpoint_de_use_cuda):
        fallback_reasons.append("CUDA is disabled")
    if not executable.is_file():
        fallback_reasons.append(
            f"multi-island executable is missing: {executable}"
        )
    else:
        executable_mtime_ns = executable.stat().st_mtime_ns
        stale_inputs = [
            path
            for path in _module_policy.CHECKPOINT_MULTI_ISLAND_RUNTIME_INPUTS
            if path.is_file() and path.stat().st_mtime_ns > executable_mtime_ns
        ]
        if stale_inputs:
            relative_inputs = ", ".join(
                str(path.relative_to(_module_paths.CPP_DIR)) for path in stale_inputs
            )
            fallback_reasons.append(
                "stale multi-island executable: rebuild with "
                "scripts/build_multi_island.ps1; newer runtime inputs: "
                f"{relative_inputs}"
            )
    return {
        "default_enabled": True,
        "requested": requested,
        "eligible": not fallback_reasons,
        "used": bool(requested and not fallback_reasons),
        "executable": str(executable),
        "validated_contract": validated,
        "actual_contract": actual,
        "fallback_reasons": fallback_reasons,
    }
