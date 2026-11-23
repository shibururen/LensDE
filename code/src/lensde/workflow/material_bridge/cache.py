"""Content-bound calibration/scoring/completed-stage reuse and invalidation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from lensde.workflow import selection as strict_topology
from . import contracts as _module_contracts
from . import execution as _module_execution
from . import files as _module_files
from . import identity as _module_identity
from . import paths as _module_paths
from . import policy as _module_policy
from . import scoring as _module_scoring


def bridge_content_binding(files: dict[str, Path]) -> dict[str, Any] | None:
    """Bind completed work to content, including trim defaults and implementation."""
    files = {
        **files,
        "bridge_code": _module_paths.SCRIPT_PATH,
        "trim_code": Path(server.stage_illumination_trim.__file__),
    }
    identities = {}
    for name, path in files.items():
        digest = server.sha256_file(path)
        if name == "bridge_code":
            digest = _module_identity.implementation_identity()
        if not digest:
            return None
        identities[name] = {"path": str(path.resolve()), "sha256": digest}
    return {
        "contract": "bridge_input_content_v1",
        "trim_contract": server.stage_illumination_trim.CONTRACT,
        "files": identities,
    }


def vignetting_bridge_input_binding(command: list[str]) -> dict[str, Any] | None:
    try:
        files = {flag: Path(command[command.index(flag) + 1]) for flag in (
            "--source-system", "--source-merit", "--source-variables", "--executable",
        )}
        files["experiment_code"] = Path(command[1])
    except (ValueError, IndexError):
        return None
    return bridge_content_binding(files)


def cached_vignetting_bridge_is_usable(
    *,
    command: list[str],
    cached_command: Any,
    comparison: dict[str, Any],
    nodes: tuple[float, ...],
    minimum: float,
    aperture_margin: float,
    realized_path: Path,
    input_binding: dict[str, Any] | None = None,
    cached_input_binding: Any = None,
) -> bool:
    """Accept only the current dynamic-4x8 APG/CP handoff contract.

    Both the command and a success-only content binding are required. Matching
    paths alone cannot identify an in-place merit/policy or prescription edit.
    """

    if (not input_binding or cached_input_binding != input_binding
            or cached_command != command or not realized_path.is_file()):
        return False
    if not bool(comparison.get("passed", False)):
        return False
    if (
        comparison.get("experiment")
        != "vignetting_sparse_to_dense_dynamic_4x8_bridge_v16"
    ):
        return False
    if [_module_files.finite_float(value) for value in comparison.get("field_radius_nodes", [])] != [
        float(value) for value in nodes
    ]:
        return False
    if abs(
        _module_files.finite_float(comparison.get("minimum_relative_illumination"), math.inf)
        - minimum
    ) > 1.0e-12:
        return False
    if abs(
        _module_files.finite_float(comparison.get("aperture_margin"), math.inf)
        - aperture_margin
    ) > 1.0e-12:
        return False
    policy = comparison.get("bridge_policy") or {}
    acceptance = comparison.get("acceptance") or {}
    return (
        str(policy.get("set_vignetting_aiming_policy") or "") == "gui_default"
        and str(policy.get("set_vignetting_aiming_mode") or "")
        == "adaptive_pilot_grid"
        and bool(acceptance.get("source_fields_preserved_during_materialization"))
        and (
            bool(acceptance.get("set_vignetting_solved_every_target_field"))
            or bool(acceptance.get("inherited_fields_complete"))
        )
        and policy.get("apg_retrace_is_diagnostic_not_admission") is True
        and policy.get("cp_per_ray_convergence_gate") is False
        and bool(
            acceptance.get("calibrated_retrace_with_aperture_update_passed")
        )
        and bool(
            acceptance.get("coverage_difference_is_diagnostic_not_admission")
        )
        and bool(
            acceptance.get(
                "mode_aperture_difference_is_diagnostic_not_admission"
            )
        )
        and policy.get("set_vignetting_primary_wavelength_only") is True
        and policy.get("set_vignetting_all_wavelengths") is False
        and policy.get("set_vignetting_requires_full_trace") is False
        and policy.get("post_measurement_projection_or_contraction") is False
        and policy.get("post_measurement_clear_aperture_update") is True
        and policy.get("optimizer_aperture_contract")
        == server.dynamic_aperture_contract.CONTRACT
        and policy.get("admission_uses_merit") is False
        and policy.get("admission_uses_aperture_stability_and_coverage_only")
        is False
        and policy.get(
            "admission_uses_trace_coverage_not_mode_aperture_parity"
        ) is True
        and policy.get(
            "mode_aperture_difference_is_diagnostic_not_admission"
        ) is True
        and policy.get("coverage_difference_is_diagnostic_not_admission") is True
        and policy.get("source_aperture_change_is_diagnostic_not_admission")
        is True
        and str(policy.get("set_vignetting_measurement_aiming") or "")
        == "adaptive_pilot_grid_strict"
        and policy.get("set_vignetting_per_ray_fallback_allowed") is False
        and policy.get("final_handoff_aiming_fallback_allowed") is False
    )


def score_population_complete(score: dict[str, Any]) -> bool:
    return all(
        math.isfinite(_module_files.finite_float(score.get(key)))
        for key in (
            "merit",
            "trace_coverage",
            "min_field_coverage",
            "min_chief_progress",
            "min_edge",
        )
    )


def executable_identity(executable: Path) -> dict[str, Any]:
    stat = executable.stat()
    return {
        "path": str(executable.resolve()),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
    }


def bridge_score_fingerprint(
    *,
    system: dict[str, Any],
    variables_doc: dict[str, Any],
    merit: dict[str, Any],
    population: list[list[float]],
) -> str:
    payload = json.dumps(
        {
            "system": system,
            "variables": variables_doc,
            "merit": merit,
            "population": population,
        },
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def execute_score_cached(
    *,
    command: list[str],
    case_dir: Path,
    score_path: Path,
    fingerprint: str,
    timeout: int,
    legacy_exact_match: bool = False,
) -> dict[str, Any]:
    """Execute one score command or adopt an exactly matching completed score."""

    cache_path = case_dir / "score_contract.json"
    executable = Path(command[0]).resolve()
    identity = executable_identity(executable)
    cache = _module_files.read_json(cache_path) if cache_path.is_file() else {}
    score = _module_scoring.score_population_row(score_path)
    versioned_hit = (
        cache.get("contract") == _module_policy.MATERIAL_BRIDGE_SCORE_CONTRACT
        and cache.get("command") == command
        and cache.get("fingerprint") == fingerprint
        and cache.get("executable") == identity
        and int(_module_files.finite_float(cache.get("returncode"), -1)) == 0
        and score_population_complete(score)
    )
    legacy_hit = (
        not cache
        and legacy_exact_match
        and score_population_complete(score)
        and score_path.stat().st_mtime_ns >= executable.stat().st_mtime_ns
    )
    if versioned_hit or legacy_hit:
        result = {
            **score,
            "returncode": 0,
            "wall_seconds": 0.0,
            "timed_out": False,
            "cache_hit": True,
            "adopted_legacy_cache": legacy_hit,
        }
    else:
        returncode, wall_seconds, timed_out = _module_execution.execute(command, case_dir, timeout)
        score = _module_scoring.score_population_row(score_path)
        result = {
            **score,
            "returncode": returncode,
            "wall_seconds": wall_seconds,
            "timed_out": timed_out,
            "cache_hit": False,
            "adopted_legacy_cache": False,
        }
    _module_files.write_json(
        cache_path,
        {
            "contract": _module_policy.MATERIAL_BRIDGE_SCORE_CONTRACT,
            "command": command,
            "fingerprint": fingerprint,
            "executable": identity,
            "returncode": result["returncode"],
            "timed_out": result["timed_out"],
            "score_complete": score_population_complete(result),
            "score": {
                key: result.get(key)
                for key in (
                    "merit",
                    "trace_ok",
                    "trace_coverage",
                    "min_field_coverage",
                    "min_chief_progress",
                    "mean_spot",
                    "max_spot",
                    "min_edge",
                )
            },
        },
    )
    return result


def bridge_stage_input_binding(
    latest_rows: list[dict[str, Any]], merit_path: Path,
    exe: Path, args: argparse.Namespace,
) -> dict[str, Any] | None:
    files = {
        "merit": merit_path, "executable": exe,
        "cpu_executable": _module_paths.CPP_DIR / "build" / "lensde_cpu.exe",
        "selection_code": Path(strict_topology.__file__),
        "experiment_code": _module_paths.SCRIPTS_DIR / "run_vignetting_aperture_realized_bridge_experiment.py",
    }
    try:
        for i, row in enumerate(latest_rows):
            source = Path(str(row.get("out_dir") or ""))
            for name in ("system.json", "variables.json"):
                files[f"source_{i}_{name}"] = _module_files.source_file(source, name)
    except FileNotFoundError:
        return None
    binding = bridge_content_binding(files)
    if binding is not None:
        binding["configuration"] = json.loads(json.dumps(vars(args), default=str))
    return binding


def cached_bridge_stage_is_usable(
    report: dict[str, Any],
    *,
    latest_rows: list[dict[str, Any]],
    material_context_run: Path,
    merit_path: Path,
    exe: Path,
    args: argparse.Namespace,
) -> bool:
    """Validate a completed bridge before skipping every expensive wave."""

    # A bridge report is an executable optical handoff, not merely a progress
    # marker.  In particular, v2 reports may contain the old coupled-XY solve
    # followed by unconditional X-vignetting removal.  Never adopt such a
    # report after the pupil contract changes just because its output paths
    # and requested field nodes still match.
    binding = bridge_stage_input_binding(latest_rows, merit_path, exe, args)
    if not binding or report.get("input_binding") != binding:
        return False
    if report.get("contract") != _module_policy.MATERIAL_BRIDGE_STAGE_CONTRACT:
        return False
    if (
        report.get("selection_merit_contract")
        != _module_policy.MATERIAL_BRIDGE_SELECTION_MERIT_CONTRACT
    ):
        return False
    if str(report.get("strict_structure_selection_policy") or "") != str(
        getattr(
            args,
            "strict_structure_selection_policy",
            strict_topology.LEGACY_DISTANCE_POLICY,
        )
    ):
        return False
    if not bool(report.get("enabled", False)):
        return False
    if Path(str(report.get("material_context_run") or "")).resolve() != material_context_run.resolve():
        return False
    if Path(str(report.get("common_merit_source") or "")).resolve() != merit_path.resolve():
        return False
    if Path(str(report.get("executable") or "")).resolve() != exe.resolve():
        return False
    expected_nodes = [float(value) for value in _module_contracts.field_nodes(args)]
    if [_module_files.finite_float(value) for value in report.get("field_hy", [])] != expected_nodes:
        return False
    if int(_module_files.finite_float(report.get("rings"), -1)) != max(
        1, int(getattr(args, "material_bridge_trace_rays", 6))
    ):
        return False
    if int(_module_files.finite_float(report.get("arms"), -1)) != max(
        1, int(getattr(args, "material_bridge_trace_arms", 8))
    ):
        return False
    if str(report.get("distribution") or "") != str(
        getattr(args, "material_bridge_trace_distribution", "gaussian_ring_arm")
        or "gaussian_ring_arm"
    ):
        return False
    if int(_module_files.finite_float(report.get("requested_material_entry_count"), -1)) != max(
        0, int(getattr(args, "material_entry_count", 0))
    ):
        return False

    diagnostics = list(report.get("diagnostics") or [])
    if len(diagnostics) != len(latest_rows):
        return False
    expected_identities = [
        (
            int(_module_files.finite_float(row.get("basin_rank"), position)),
            str(Path(str(row.get("out_dir") or "")).resolve()),
        )
        for position, row in enumerate(latest_rows, start=1)
    ]
    actual_identities = [
        (
            int(_module_files.finite_float(row.get("basin_rank"), position)),
            str(
                Path(
                    str(
                        row.get("pre_vignetting_bridge_out_dir")
                        or row.get("geometry_out_dir")
                        or ""
                    )
                ).resolve()
            ),
        )
        for position, row in enumerate(diagnostics, start=1)
    ]
    if actual_identities != expected_identities:
        return False

    vignetting_reports = list(
        (report.get("vignetting_field_bridge") or {}).get("reports") or []
    )
    if len(vignetting_reports) != len(latest_rows):
        return False
    for item in vignetting_reports:
        if item.get("contract") != _module_policy.MATERIAL_BRIDGE_VIGNETTING_CONTRACT:
            return False
        if bool(item.get("hard_reject", False)):
            return False
        if bool(item.get("performed", False)) and (
            bool(item.get("set_vignetting_aiming_fallback_used", False))
            or str(item.get("set_vignetting_aiming_mode") or "")
            != "adaptive_pilot_grid"
            or str(item.get("handoff_aiming_mode") or "")
            != "calibrated_paraxial"
        ):
            return False

    waves = list((report.get("multi_island") or {}).get("waves") or [])
    if any(
        bool(wave.get("requested", False))
        and bool(wave.get("eligible", False))
        and not bool(wave.get("used", False))
        for wave in waves
    ):
        return False

    selected = list(report.get("selected") or [])
    if len(selected) != int(_module_files.finite_float(report.get("post_deepen_selected_count"), -1)):
        return False
    threshold = _module_contracts.minimum_trace_coverage(args)
    min_edge = float(getattr(args, "material_bridge_repair_min_edge", 0.0))
    for row in selected:
        if not bool(row.get("final_materialization_valid", False)):
            return False
        out_dir = Path(str(row.get("out_dir") or "")).resolve()
        try:
            score = _module_files.read_json(_module_files.source_file(out_dir, "score.json"))
        except (FileNotFoundError, json.JSONDecodeError):
            return False
        if not (
            bool(score.get("trace_ok", False))
            and _module_files.finite_float(score.get("trace_coverage"), 0.0) >= threshold
            and _module_files.finite_float(score.get("min_field_coverage"), 0.0) >= threshold
            and server.vignetting_handoff_policy.chief_ray_usable(score)
            and _module_files.finite_float(score.get("min_edge"), -math.inf) >= min_edge
            and 0.0 < _module_files.finite_float(score.get("merit"), math.inf) < 1.0e12
        ):
            return False
    return bool(selected)
