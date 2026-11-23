"""Exact rank-one reconstruction and CPU materialization validation."""
from __future__ import annotations

import math
from pathlib import Path
from typing import Any, Iterable
from lensde.engine import core as server
from . import commands as _module_commands
from . import contracts as _module_contracts
from . import execution as _module_execution
from . import files as _module_files
from . import policy as _module_policy


def rescore_rank1(
    *,
    outer: int,
    row: dict[str, Any],
    args: Any,
    base_system: dict[str, Any],
    variables_doc: dict[str, Any],
    merit: dict[str, Any],
    glass_search_config: dict[str, Any] | None,
    fallback_command: list[str],
) -> float:
    case_dir = (
        server.native_io_paths.named_child(
            args.out_dir.resolve(), 'layer1_checkpoint_de400'
        )
        / "rank1_rescore"
        / f"outer_{outer:02d}"
    )
    score_path = case_dir / "result" / "score.json"
    rescore_result_path = case_dir / "rescore_result.json"
    source_command = _module_files.load_command(Path(str(row["outer_dir"])), fallback_command)
    trace_coverage_epsilon = _module_contracts.command_materialization_trace_epsilon(source_command)
    if score_path.is_file() and rescore_result_path.is_file() and not args.force_rerun:
        cached_score = _module_files.read_json(score_path)
        cached_result = _module_files.read_json(rescore_result_path)
        cached_valid, _ = _module_contracts.materialization_contract(
            cached_score,
            merit,
            require_nonnegative_edge=False,
            trace_coverage_epsilon=trace_coverage_epsilon,
        )
        if (
            cached_valid
            and str(cached_result.get("score_contract") or "")
            == _module_policy.CHECKPOINT_SCORE_CONTRACT
        ):
            cached_result["materialization_valid"] = True
            cached_result["materialization_invalid_reasons"] = []
            cached_result["materialization_trace_contract"] = (
                "near" if trace_coverage_epsilon > 0.0 else "strict"
            )
            cached_result["trace_coverage_epsilon"] = trace_coverage_epsilon
            _module_files.write_json(rescore_result_path, cached_result)
            return _module_files.finite(cached_score.get("merit"))
    values = _module_files.parse_values(row)
    variables = list(variables_doc.get("variables") or [])
    system = server.apply_variable_values_to_system(
        base_system,
        variables,
        values,
        glass_search_config,
    )
    anchored_variables = _module_commands.variables_for_anchor(variables_doc, values)
    case_dir.mkdir(parents=True, exist_ok=True)
    _module_files.write_json(case_dir / "system_input.json", system)
    _module_files.write_json(case_dir / "variables.json", anchored_variables)
    _module_files.write_json(case_dir / "merit.json", merit)
    if glass_search_config is not None:
        _module_files.write_json(case_dir / "glass_search.json", glass_search_config)
    command = _module_commands.build_score_command(
        source_command,
        exe=Path(args.checkpoint_de_exe).resolve(),
        case_dir=case_dir,
        use_cuda=bool(args.checkpoint_de_use_cuda),
    )
    returncode, wall, timed_out = _module_execution.execute_plain(
        command,
        case_dir,
        int(args.checkpoint_de_timeout),
    )
    score = _module_files.read_json(score_path) if score_path.is_file() else {}
    value = _module_files.finite(score.get("merit"))
    materialization_valid, invalid_reasons = _module_contracts.materialization_contract(
        score,
        merit,
        require_nonnegative_edge=False,
        trace_coverage_epsilon=trace_coverage_epsilon,
    )
    _module_files.write_json(
        case_dir / "rescore_result.json",
        {
            "outer": outer,
            "state_id": row["state_id"],
            "source_snapshot_merit": _module_files.finite(row.get("merit")),
            "rescore_merit": value,
            "score_contract": _module_policy.CHECKPOINT_SCORE_CONTRACT,
            "materialization_valid": materialization_valid,
            "materialization_invalid_reasons": invalid_reasons,
            "materialization_trace_contract": (
                "near" if trace_coverage_epsilon > 0.0 else "strict"
            ),
            "trace_coverage_epsilon": trace_coverage_epsilon,
            "trace_ok": bool(score.get("trace_ok", False)),
            "trace_coverage": _module_files.finite(score.get("trace_coverage"), math.nan),
            "min_field_coverage": _module_files.finite(score.get("min_field_coverage"), math.nan),
            "min_chief_progress": _module_files.finite(score.get("min_chief_progress"), math.nan),
            "chief_ray_ok": score.get("chief_ray_ok"),
            "chief_ray_failed_fields": score.get("chief_ray_failed_fields"),
            "zero_coverage_off_axis_fields": score.get(
                "zero_coverage_off_axis_fields"
            ),
            "min_edge": _module_files.finite(score.get("min_edge"), math.nan),
            "returncode": returncode,
            "timed_out": timed_out,
            "wall_seconds": wall,
        },
    )
    if returncode != 0 or timed_out or not materialization_valid:
        raise RuntimeError(
            f"Outer {outer} rank1 rescore failed: rc={returncode}, "
            f"timed_out={timed_out}, merit={value}, reasons={invalid_reasons}"
        )
    return value
