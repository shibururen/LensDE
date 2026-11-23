"""Validated material multi-island policy, execution and materialized publication."""
from __future__ import annotations

import argparse
import os
import shutil
from lensde.runtime import processes as subprocess
import time
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from . import cache as _module_cache
from . import contracts as _module_contracts
from . import execution as _module_execution
from . import files as _module_files
from . import inputs as _module_inputs
from . import paths as _module_paths
from . import policy as _module_policy


def material_bridge_multi_island_policy(
    args: argparse.Namespace,
    prepared: list[dict[str, Any]],
) -> dict[str, Any]:
    requested = bool(getattr(args, "material_bridge_multi_island_enabled", True))
    executable = Path(
        getattr(
            args,
            "material_bridge_multi_island_exe",
            _module_paths.CPP_DIR / "build" / "lensde_multi_island.exe",
        )
    ).resolve()
    fallback_reasons: list[str] = []
    if len(prepared) < 2:
        fallback_reasons.append("fewer than two islands")
    if not bool(getattr(args, "material_bridge_de_use_cuda", True)):
        fallback_reasons.append("CUDA is disabled")
    if not executable.is_file():
        fallback_reasons.append(f"multi-island executable is missing: {executable}")
    if str(getattr(args, "material_bridge_de_variant", "jade_pbest_archive_lpsr_feas")) != "jade_pbest_archive_lpsr_feas":
        fallback_reasons.append("unsupported DE variant")
    if str(getattr(args, "material_bridge_de_strategy", "rand1bin")) != "rand1bin":
        fallback_reasons.append("unsupported DE strategy")
    if int(getattr(args, "material_bridge_repair_epoch_generations", 50)) != 50:
        fallback_reasons.append("repair epoch generations must be 50")
    if int(getattr(args, "material_bridge_de_cuda_min_batch", 1)) != 1:
        fallback_reasons.append("CUDA minimum batch must be 1")
    requested_minimum_trace_coverage = _module_contracts.minimum_trace_coverage(args)
    if (
        requested_minimum_trace_coverage
        < _module_policy.DE_DLS_TRACE_COVERAGE_BASELINE - 1.0e-12
    ):
        fallback_reasons.append(
            "minimum trace coverage must be at least "
            f"{_module_policy.DE_DLS_TRACE_COVERAGE_BASELINE:.2f} for the batched broker"
        )
    scaling = _module_execution.normalized_internal_scaling(args)
    if scaling not in {"legacy_optical", "bound_normalized", "edge_sag_normalized"}:
        fallback_reasons.append(f"unsupported internal scaling: {scaling}")
    if prepared:
        signature = _module_execution.multi_island_layout_signature(prepared[0])
        if any(_module_execution.multi_island_layout_signature(item) != signature for item in prepared[1:]):
            fallback_reasons.append("incompatible variable or surface layout")
        merit_reference = _module_files.read_json(Path(prepared[0]["case_dir"]) / "merit.json")
        if any(
            _module_files.read_json(Path(item["case_dir"]) / "merit.json") != merit_reference
            for item in prepared[1:]
        ):
            fallback_reasons.append("incompatible merit contracts")
        profiles = {str(item["profile"]) for item in prepared}
        constraints = {str(item["constraint_handling"]) for item in prepared}
        if len(profiles) != 1:
            fallback_reasons.append("mixed wave profiles")
        elif profiles == {"material_repair"} and constraints != {"scalar_merit"}:
            fallback_reasons.append("repair batching requires scalar_merit")
        elif profiles == {"material_deepen"} and constraints != {"feasibility_rules"}:
            fallback_reasons.append("deepen batching requires feasibility_rules")
    return {
        "default_enabled": True,
        "requested": requested,
        "eligible": not fallback_reasons,
        "used": bool(requested and not fallback_reasons),
        "executable": str(executable),
        "internal_scaling": scaling,
        "broker_search_minimum_trace_coverage": (
            _module_policy.DE_DLS_TRACE_COVERAGE_BASELINE
        ),
        "final_admission_minimum_trace_coverage": (
            requested_minimum_trace_coverage
        ),
        "island_count": len(prepared),
        "populations": [int(item["population_rows"]) for item in prepared],
        "generations": [int(item["generations"]) for item in prepared],
        "continuous_material_de": {
            "enabled_for_all_islands": bool(prepared) and all(
                bool(
                    item.get(
                        "continuous_material_de",
                        _module_contracts.continuous_material_variable_contract(item.get("variables") or []),
                    ).get("optimization_enabled")
                )
                for item in prepared
            ),
            "island_contracts": [
                item.get(
                    "continuous_material_de",
                    _module_contracts.continuous_material_variable_contract(item.get("variables") or []),
                )
                for item in prepared
            ],
        },
        "fallback_reasons": fallback_reasons,
    }


def publish_materialized_multi_island_result(
    *,
    case_dir: Path,
    experimental_dir: Path,
) -> Path:
    """Publish the broker's already-materialized CPU score without mutating it.

    ``optiland_multi_island_experiment`` applies the winning variables, updates
    the dynamic clear apertures, and evaluates the materialized system on the
    CPU before writing ``experimental_result/system.json`` and ``score.json``.
    Running the ordinary CLI scorer over that saved system performs the
    aperture transition again.  The transition is not generally idempotent:
    in particular, the paraxial stop and grouped mechanical apertures can move
    enough to turn a valid edge-thickness result into a structural failure.

    The exact system/score pair emitted by the broker is therefore the locked
    validation artifact.  Copy it into the conventional ``run`` directory so
    downstream code can consume it without a second aperture materialization.
    """
    system_path = experimental_dir / "system.json"
    score_path = experimental_dir / "score.json"
    missing = [str(path) for path in (system_path, score_path) if not path.is_file()]
    if missing:
        raise RuntimeError(
            "multi-island materialized result is incomplete: " + ", ".join(missing)
        )

    run_dir = case_dir / "run"
    run_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(system_path, run_dir / "system.json")
    shutil.copy2(score_path, run_dir / "score.json")
    shutil.copy2(score_path, run_dir / "initial_score.json")
    for name in ("population.csv", "merits.csv", "timeline.jsonl"):
        source = experimental_dir / name
        if source.is_file():
            shutil.copy2(source, run_dir / name)

    provenance_dir = case_dir / "final_score"
    provenance_dir.mkdir(parents=True, exist_ok=True)
    _module_files.write_json(
        provenance_dir / "materialized_score_reuse.json",
        {
            "mode": "reuse_exact_multi_island_materialized_cpu_score",
            "system": str(system_path.resolve()),
            "score": str(score_path.resolve()),
            "out_dir": str(run_dir.resolve()),
            "aperture_rematerialized": False,
        },
    )
    return run_dir


def run_prepared_de_batched(
    prepared: list[dict[str, Any]],
    *,
    exe: Path,
    args: argparse.Namespace,
    wave_name: str,
    policy: dict[str, Any],
    batch_dir: Path | None = None,
) -> list[dict[str, Any]]:
    batch_dir = (
        Path(batch_dir)
        if batch_dir is not None
        else Path(prepared[0]["case_dir"]).parent / "multi_island_batch"
    )
    batch_dir.mkdir(parents=True, exist_ok=True)
    jobs_path = batch_dir / "jobs.tsv"
    header = [
        "island_id", "system", "variables", "merit", "population_file",
        "snapshot_dir", "output_dir", "seed", "population", "generations",
        "profile", "internal_scaling",
    ]
    lines = ["\t".join(header)]
    for island_id, item in enumerate(prepared):
        case_dir = Path(item["case_dir"])
        cells = [
            island_id,
            case_dir / "system_input.json",
            case_dir / "variables.json",
            case_dir / "merit.json",
            item["population_path"],
            case_dir / "multi_island_snapshots",
            case_dir / "experimental_result",
            item["seed"],
            item["population_rows"],
            item["generations"],
            item["profile"],
            policy["internal_scaling"],
        ]
        lines.append("\t".join(str(value) for value in cells))
    jobs_text = "\n".join(lines) + "\n"
    previous_jobs_text = (
        jobs_path.read_text(encoding="utf-8") if jobs_path.is_file() else None
    )
    command = [
        str(policy["executable"]),
        "--jobs", str(jobs_path),
        "--batched",
        "--coalesce-us", str(
            max(0, int(getattr(args, "material_bridge_multi_island_coalesce_us", 1000)))
        ),
        "--broker-stats", str(batch_dir / "broker_stats.json"),
    ]
    command_path = batch_dir / "command.json"
    previous_command = _module_files.read_json(command_path) if command_path.is_file() else None
    previous_run_result_path = batch_dir / "run_result.json"
    previous_run_result = (
        _module_files.read_json(previous_run_result_path)
        if previous_run_result_path.is_file()
        else {}
    )
    executable_fingerprint = _module_cache.executable_identity(Path(policy["executable"]))
    cached_outputs_complete = all(
        all(
            (Path(item["case_dir"]) / "experimental_result" / name).is_file()
            for name in ("result.json", "system.json", "score.json")
        )
        for item in prepared
    )
    cache_hit = (
        previous_jobs_text == jobs_text
        and previous_command == command
        and int(_module_files.finite_float(previous_run_result.get("returncode"), -1)) == 0
        and not _module_files.csv_bool(previous_run_result.get("timed_out"))
        and int(_module_files.finite_float(previous_run_result.get("island_count"), -1))
        == len(prepared)
        and previous_run_result.get("executable") == executable_fingerprint
        and cached_outputs_complete
    )
    jobs_path.write_text(jobs_text, encoding="utf-8")
    _module_files.write_json(command_path, command)
    environment = os.environ.copy()
    environment["OPTILAND_DE_CUDA_REQUIRE"] = "1"
    timeout = int(getattr(args, "material_bridge_timeout", 0))
    if cache_hit:
        returncode = 0
        timed_out = False
        batch_wall = _module_files.finite_float(previous_run_result.get("wall_seconds"), 0.0)
    else:
        started = time.perf_counter()
        try:
            with server.native_io_paths.native_command(command, _module_paths.CPP_DIR, force_native=True) as native_command:
                completed = subprocess.run(
                    native_command,
                    cwd=_module_paths.CPP_DIR,
                    env=environment,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                    timeout=(timeout * len(prepared)) if timeout > 0 else None,
                )
            returncode = completed.returncode
            stdout = completed.stdout or ""
            stderr = completed.stderr or ""
            timed_out = False
        except subprocess.TimeoutExpired as exc:
            returncode = -9
            stdout = str(exc.stdout or "")
            stderr = str(exc.stderr or "")
            timed_out = True
        batch_wall = time.perf_counter() - started
        (batch_dir / "stdout.log").write_text(
            stdout, encoding="utf-8", errors="replace"
        )
        (batch_dir / "stderr.log").write_text(
            stderr, encoding="utf-8", errors="replace"
        )
    _module_files.write_json(
        batch_dir / "run_result.json",
        {
            "wave": wave_name,
            "returncode": returncode,
            "timed_out": timed_out,
            "wall_seconds": batch_wall,
            "island_count": len(prepared),
            "cache_hit": cache_hit,
            "executable": executable_fingerprint,
        },
    )
    if returncode != 0 or timed_out:
        raise RuntimeError(
            f"material bridge {wave_name} multi-island failed: "
            f"rc={returncode}, timed_out={timed_out}, logs={batch_dir}"
        )

    results: list[dict[str, Any]] = []
    for item in prepared:
        case_dir = Path(item["case_dir"])
        experimental_dir = case_dir / "experimental_result"
        experimental_result_path = experimental_dir / "result.json"
        if not experimental_result_path.is_file():
            raise RuntimeError(f"missing multi-island result: {experimental_result_path}")
        experimental_result = _module_files.read_json(experimental_result_path)
        if not bool(experimental_result.get("used_cuda", False)):
            raise RuntimeError(f"multi-island result did not use CUDA: {experimental_result_path}")
        best_values = [float(value) for value in experimental_result.get("best_values", [])]
        if len(best_values) != len(item["variables"]):
            raise RuntimeError(
                f"multi-island returned {len(best_values)} values for "
                f"{len(item['variables'])} variables"
            )
        # The multi-island executable saves the best candidate after
        # materializing its dynamic-aperture state.  Reuse that exact system
        # for the Locked rescore; rebuilding from the pre-DE input would lose
        # the aperture baseline used by the CUDA candidate evaluation.
        saved_system_path = experimental_dir / "system.json"
        if saved_system_path.is_file():
            best_system = _module_files.read_json(saved_system_path)
        else:
            best_system = server.apply_variable_values_to_system(
                item["system"], item["variables"], best_values, None
            )
        best_system_path = case_dir / "best_system_input.json"
        best_variables_path = case_dir / "best_variables.json"
        _module_files.write_json(best_system_path, best_system)
        _module_files.write_json(
            best_variables_path,
            _module_inputs.anchored_variables_doc(item["variables_doc"], best_values),
        )
        run_dir = publish_materialized_multi_island_result(
            case_dir=case_dir,
            experimental_dir=experimental_dir,
        )
        result = _module_execution.de_result_from_score(
            item,
            returncode=0,
            wall_seconds=batch_wall / len(prepared),
            timed_out=False,
            execution_mode="batched_multi_island_materialized_cpu_score",
            shared_batch_wall_seconds=batch_wall,
        )
        for key in (
            "optimizer_best_merit",
            "materialized_merit",
            "materialization_merit_delta",
            "materialization_merit_relative_difference",
            "materialization_merit_relative_tolerance",
            "cpu_cuda_materialization_consistent",
            "materialization_valid",
        ):
            result[key] = experimental_result.get(key)
        result["multi_island_cache_hit"] = cache_hit
        results.append(result)
    return results
