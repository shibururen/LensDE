"""Common-field parent scores and CPU/CUDA measurement consistency."""
from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from . import cache as _module_cache
from . import commands as _module_commands
from . import contracts as _module_contracts
from . import files as _module_files
from . import inputs as _module_inputs
from . import paths as _module_paths
from . import policy as _module_policy


def score_population_row(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    with path.open("r", newline="", encoding="utf-8") as handle:
        raw = next(csv.DictReader(handle), {})
    result = {
        "merit": _module_files.finite_float(raw.get("merit")),
        "trace_ok": _module_files.csv_bool(raw.get("trace_ok")),
        "trace_coverage": _module_files.finite_float(raw.get("trace_coverage")),
        "min_field_coverage": _module_files.finite_float(raw.get("min_field_coverage")),
        "min_chief_progress": _module_files.finite_float(raw.get("min_chief_progress")),
        "mean_spot": _module_files.finite_float(raw.get("mean_spot")),
        "max_spot": _module_files.finite_float(raw.get("max_spot")),
        "max_distortion": _module_files.finite_float(raw.get("max_distortion")),
        "constraint_group_error": _module_files.finite_float(
            raw.get("constraint_group_error"), _module_files.finite_float(raw.get("constraint_penalty"))
        ),
        "first_order_group_error": _module_files.finite_float(
            raw.get("first_order_group_error"), _module_files.finite_float(raw.get("first_order_penalty"))
        ),
        "image_group_error": _module_files.finite_float(
            raw.get("image_group_error"), _module_files.finite_float(raw.get("image_penalty"))
        ),
        "min_edge": _module_files.finite_float(raw.get("min_edge")),
    }
    failed_fields = str(raw.get("chief_ray_failed_fields") or "").strip()
    if failed_fields:
        result["chief_ray_failed_fields"] = int(_module_files.finite_float(failed_fields, 1.0))
        result["chief_ray_ok"] = result["chief_ray_failed_fields"] == 0
    zero_off_axis = str(raw.get("zero_coverage_off_axis_fields") or "").strip()
    if zero_off_axis:
        result["zero_coverage_off_axis_fields"] = int(
            _module_files.finite_float(zero_off_axis, 1.0)
        )
    return result


def cpu_cuda_score_consistency(
    cpu: dict[str, Any], optimizer: dict[str, Any]
) -> dict[str, Any]:
    merit_scale = max(
        1.0,
        abs(_module_files.finite_float(cpu.get("merit"), math.inf)),
        abs(_module_files.finite_float(optimizer.get("merit"), math.inf)),
    )
    merit_relative_difference = abs(
        _module_files.finite_float(cpu.get("merit"), math.inf)
        - _module_files.finite_float(optimizer.get("merit"), -math.inf)
    ) / merit_scale
    trace_match = bool(cpu.get("trace_ok", False)) == bool(
        optimizer.get("trace_ok", False)
    )
    coverage_difference = max(
        abs(
            _module_files.finite_float(cpu.get(key), math.inf)
            - _module_files.finite_float(optimizer.get(key), -math.inf)
        )
        for key in (
            "trace_coverage",
            "min_field_coverage",
            "min_chief_progress",
        )
    )
    edge_difference = abs(
        _module_files.finite_float(cpu.get("min_edge"), math.inf)
        - _module_files.finite_float(optimizer.get("min_edge"), -math.inf)
    )
    consistent = (
        _module_cache.score_population_complete(cpu)
        and _module_cache.score_population_complete(optimizer)
        and trace_match
        and coverage_difference <= _module_policy.MATERIAL_BRIDGE_CPU_CUDA_COVERAGE_TOL
        and edge_difference <= _module_policy.MATERIAL_BRIDGE_CPU_CUDA_EDGE_TOL
        and merit_relative_difference <= _module_policy.MATERIAL_BRIDGE_CPU_CUDA_MERIT_REL_TOL
    )
    return {
        "cpu_cuda_consistent": consistent,
        "cpu_cuda_trace_match": trace_match,
        "cpu_cuda_coverage_max_abs_difference": coverage_difference,
        "cpu_cuda_min_edge_abs_difference": edge_difference,
        "cpu_cuda_merit_relative_difference": merit_relative_difference,
        "cpu_cuda_merit_relative_tolerance": _module_policy.MATERIAL_BRIDGE_CPU_CUDA_MERIT_REL_TOL,
    }


def score_parent(
    *,
    source: Path,
    case_dir: Path,
    merit: dict[str, Any],
    exe: Path,
    args: argparse.Namespace,
) -> dict[str, Any]:
    nodes = _module_contracts.field_nodes(args)
    rays = max(1, int(getattr(args, "material_bridge_trace_rays", 6)))
    arms = max(1, int(getattr(args, "material_bridge_trace_arms", 8)))
    distribution = str(
        getattr(args, "material_bridge_trace_distribution", "gaussian_ring_arm")
        or "gaussian_ring_arm"
    )
    old_system = (
        _module_files.read_json(case_dir / "system_input.json")
        if (case_dir / "system_input.json").is_file()
        else None
    )
    old_variables = (
        _module_files.read_json(case_dir / "variables.json")
        if (case_dir / "variables.json").is_file()
        else None
    )
    old_merit = (
        _module_files.read_json(case_dir / "merit.json")
        if (case_dir / "merit.json").is_file()
        else None
    )
    old_candidate = (
        (case_dir / "candidate.csv").read_text(encoding="utf-8")
        if (case_dir / "candidate.csv").is_file()
        else None
    )
    old_command = (
        _module_files.read_json(case_dir / "command.json")
        if (case_dir / "command.json").is_file()
        else None
    )
    system, variables_doc = _module_inputs.prepare_case_inputs(
        source=source,
        case_dir=case_dir,
        merit=merit,
        nodes=nodes,
        rays=rays,
        arms=arms,
        distribution=distribution,
    )
    variables = list(variables_doc.get("variables") or [])
    population = [
        [_module_files.finite_float(variable.get("initial"), 0.0) for variable in variables]
    ]
    server.write_population_csv(
        case_dir / "candidate.csv",
        population,
    )
    command = _module_commands.base_trace_command(exe=exe, case_dir=case_dir, args=args)
    command.extend(
        [
            "--initial-population", str(case_dir / "candidate.csv"),
            "--score-population-only",
            "--out-dir", str(case_dir / "score"),
            "--de-cuda-min-batch", str(
                max(1, int(getattr(args, "material_bridge_de_cuda_min_batch", 1)))
            ),
        ]
    )
    server.dynamic_aperture_contract.apply_command(command)
    fingerprint = _module_cache.bridge_score_fingerprint(
        system=system,
        variables_doc=variables_doc,
        merit=_module_files.read_json(case_dir / "merit.json"),
        population=population,
    )
    legacy_exact_match = (
        old_system == system
        and old_variables == variables_doc
        and old_merit == _module_files.read_json(case_dir / "merit.json")
        and old_candidate == (case_dir / "candidate.csv").read_text(encoding="utf-8")
        and old_command == command
    )
    timeout = int(getattr(args, "material_bridge_timeout", 0))
    optimizer_score = _module_cache.execute_score_cached(
        command=command,
        case_dir=case_dir,
        score_path=case_dir / "score" / "population_scores.csv",
        fingerprint=fingerprint,
        timeout=timeout,
        legacy_exact_match=legacy_exact_match,
    )
    cpu_exe = _module_paths.CPP_DIR / "build" / "lensde_cpu.exe"
    if not cpu_exe.is_file():
        raise FileNotFoundError(f"material bridge CPU executable not found: {cpu_exe}")
    if exe.resolve() == cpu_exe.resolve():
        cpu_score = dict(optimizer_score)
        consistency = {
            "cpu_cuda_consistent": True,
            "cpu_cuda_trace_match": True,
            "cpu_cuda_coverage_max_abs_difference": 0.0,
            "cpu_cuda_min_edge_abs_difference": 0.0,
            "cpu_cuda_merit_relative_difference": 0.0,
            "cpu_cuda_merit_relative_tolerance": _module_policy.MATERIAL_BRIDGE_CPU_CUDA_MERIT_REL_TOL,
        }
    else:
        cpu_case_dir = case_dir / "cpu_materialized"
        cpu_case_dir.mkdir(parents=True, exist_ok=True)
        cpu_command = _module_commands.cpu_score_command(command, cpu_exe, cpu_case_dir)
        cpu_score = _module_cache.execute_score_cached(
            command=cpu_command,
            case_dir=cpu_case_dir,
            score_path=cpu_case_dir / "score" / "population_scores.csv",
            fingerprint=fingerprint,
            timeout=timeout,
        )
        consistency = cpu_cuda_score_consistency(cpu_score, optimizer_score)
    return {
        # CPU is the canonical materialized score.  CUDA is retained as
        # optimizer evidence and must agree before this parent is admissible.
        **cpu_score,
        **consistency,
        "optimizer_merit": optimizer_score.get("merit"),
        "optimizer_trace_ok": optimizer_score.get("trace_ok"),
        "optimizer_trace_coverage": optimizer_score.get("trace_coverage"),
        "optimizer_min_field_coverage": optimizer_score.get("min_field_coverage"),
        "optimizer_min_chief_progress": optimizer_score.get("min_chief_progress"),
        "optimizer_min_edge": optimizer_score.get("min_edge"),
        "optimizer_cache_hit": optimizer_score.get("cache_hit", False),
        "cpu_cache_hit": cpu_score.get("cache_hit", False),
        "aiming_mode": _module_contracts.system_aiming_mode(system),
        "wall_seconds": _module_files.finite_float(optimizer_score.get("wall_seconds"), 0.0)
        + _module_files.finite_float(cpu_score.get("wall_seconds"), 0.0),
        "result_dir": str(case_dir),
    }
