"""Owned native bridge execution and completed continuation result extraction."""
from __future__ import annotations

import argparse
from lensde.runtime import processes as subprocess
import time
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from . import commands as _module_commands
from . import contracts as _module_contracts
from . import files as _module_files
from . import inputs as _module_inputs
from . import paths as _module_paths


def execute(command: list[str], case_dir: Path, timeout: int) -> tuple[int, float, bool]:
    _module_files.write_json(case_dir / "command.json", command)
    started = time.perf_counter()
    try:
        with server.native_io_paths.native_command(command, _module_paths.CPP_DIR, force_native=True) as native_command:
            completed = subprocess.run(
                native_command,
                cwd=_module_paths.CPP_DIR,
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
    wall_seconds = time.perf_counter() - started
    (case_dir / "stdout.log").write_text(str(stdout), encoding="utf-8", errors="replace")
    (case_dir / "stderr.log").write_text(str(stderr), encoding="utf-8", errors="replace")
    return returncode, wall_seconds, timed_out


def de_result_from_score(
    prepared: dict[str, Any],
    *,
    returncode: int,
    wall_seconds: float,
    timed_out: bool,
    execution_mode: str,
    shared_batch_wall_seconds: float | None = None,
) -> dict[str, Any]:
    case_dir = Path(prepared["case_dir"])
    score_path = case_dir / "run" / "score.json"
    score = _module_files.read_json(score_path) if score_path.is_file() else {}
    return {
        "returncode": returncode,
        "timed_out": timed_out,
        "wall_seconds": wall_seconds,
        "execution_mode": execution_mode,
        "shared_batch_wall_seconds": shared_batch_wall_seconds,
        "used_cuda": bool(score.get("used_cuda", True)),
        "aiming_mode": prepared["aiming_mode"],
        "requested_generations": prepared["generations"],
        "population_rows": prepared["population_rows"],
        "constraint_handling": prepared["constraint_handling"],
        "continuous_material_de": prepared.get(
            "continuous_material_de",
            _module_contracts.continuous_material_variable_contract(prepared.get("variables") or []),
        ),
        "merit": _module_files.finite_float(score.get("merit")),
        "trace_ok": bool(score.get("trace_ok", False)),
        "trace_coverage": _module_files.finite_float(score.get("trace_coverage")),
        "min_field_coverage": _module_files.finite_float(score.get("min_field_coverage")),
        "min_chief_progress": _module_files.finite_float(score.get("min_chief_progress")),
        "chief_ray_ok": score.get("chief_ray_ok"),
        "chief_ray_failed_fields": score.get("chief_ray_failed_fields"),
        "zero_coverage_off_axis_fields": score.get(
            "zero_coverage_off_axis_fields"
        ),
        "mean_spot": _module_files.finite_float(score.get("mean_spot")),
        "max_spot": _module_files.finite_float(score.get("max_spot")),
        "max_distortion": _module_files.finite_float(score.get("max_distortion")),
        "constraint_group_error": _module_files.finite_float(score.get("constraint_group_error")),
        "first_order_group_error": _module_files.finite_float(score.get("first_order_group_error")),
        "image_group_error": _module_files.finite_float(score.get("image_group_error")),
        "min_edge": _module_files.finite_float(score.get("min_edge")),
        "result_dir": str(case_dir / "run"),
        "population_path": str(case_dir / "run" / "population.csv"),
    }


def multi_island_materialization_consistent(result: dict[str, Any]) -> bool:
    """Serial results have no parity field; batched results must pass it."""
    if "cpu_cuda_materialization_consistent" not in result:
        return True
    return _module_files.csv_bool(result.get("cpu_cuda_materialization_consistent"))


def run_prepared_de_serial(
    prepared: dict[str, Any], exe: Path, args: argparse.Namespace
) -> dict[str, Any]:
    case_dir = Path(prepared["case_dir"])
    command = _module_commands.serial_de_command(prepared, exe, args)
    returncode, wall_seconds, timed_out = execute(
        command,
        case_dir,
        int(getattr(args, "material_bridge_timeout", 0)),
    )
    return de_result_from_score(
        prepared,
        returncode=returncode,
        wall_seconds=wall_seconds,
        timed_out=timed_out,
        execution_mode="serial",
    )


def run_de(
    *,
    source: Path,
    case_dir: Path,
    merit: dict[str, Any],
    exe: Path,
    args: argparse.Namespace,
    generations: int,
    population: int,
    population_scale: str,
    seed: int,
    purpose: str,
    source_population: Path | None = None,
) -> dict[str, Any]:
    prepared = _module_inputs.prepare_de_case(
        source=source,
        case_dir=case_dir,
        merit=merit,
        args=args,
        generations=generations,
        population=population,
        population_scale=population_scale,
        seed=seed,
        purpose=purpose,
        source_population=source_population,
    )
    return run_prepared_de_serial(prepared, exe, args)


def normalized_internal_scaling(args: argparse.Namespace) -> str:
    value = str(
        getattr(args, "optimizer_internal_scaling", "edge_sag_normalized")
        or "edge_sag_normalized"
    ).strip().lower().replace("-", "_")
    aliases = {
        "legacy": "legacy_optical",
        "bound": "bound_normalized",
        "normalized": "bound_normalized",
        "edge_sag": "edge_sag_normalized",
    }
    return aliases.get(value, value)


def multi_island_layout_signature(prepared: dict[str, Any]) -> tuple[Any, ...]:
    return (
        len(prepared["system"].get("surfaces") or []),
        tuple(
            (
                str(variable.get("kind") or ""),
                int(_module_files.finite_float(variable.get("index"), -1)),
                int(_module_files.finite_float(variable.get("term"), 0)),
            )
            for variable in prepared["variables"]
        ),
    )
