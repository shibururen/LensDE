"""Material bridge cache, calibration, repair and handoff coordination."""
from __future__ import annotations

import argparse
import math
from lensde.runtime import processes as subprocess
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from . import continuation as _module_continuation
from . import contracts as _module_contracts
from . import diagnostics as _module_diagnostics
from . import files as _module_files
from . import inputs as _module_inputs
from . import policy as _module_policy
from .state import BridgeInputs, BridgeWork, RepairWork, CommonCandidates, DeepenSelection, BridgeStageActions, BridgeWaveActions
from .deepening import deepen_candidates, extend_selected, retain_without_deepen
from .reporting import publish_report


def prepare_inputs(args: argparse.Namespace, material_context_run: Path):
    """Resolve the common merit/executable inputs before considering a completed-stage cache."""
    bridge_dir = server.native_io_paths.named_child(Path(args.out_dir).resolve(), 'layer2_material5_bridge')
    bridge_dir.mkdir(parents=True, exist_ok=True)
    merit_path = (
        Path(args.material_merit_template).resolve()
        if getattr(args, "material_merit_template", None)
        else _module_files.source_file(material_context_run.resolve(), "merit.json")
    )
    merit = _module_files.read_json(merit_path)
    exe = _module_inputs.bridge_executable(args)
    report_path = bridge_dir / "bridge_report.json"
    return BridgeInputs(bridge_dir, merit_path, merit, exe, report_path)


def reuse_completed_stage(args: argparse.Namespace, latest_rows: list[dict[str, Any]], material_context_run: Path, inputs: BridgeInputs, actions: BridgeStageActions):
    """Reuse only a fully bound completed bridge stage; preserve its exact saved selection."""
    bridge_dir = inputs.bridge_dir
    merit_path = inputs.merit_path
    exe = inputs.exe
    report_path = inputs.report_path
    if report_path.is_file():
        cached_report = _module_files.read_json(report_path)
        if actions.cached_bridge_stage_is_usable(
            cached_report,
            latest_rows=latest_rows,
            material_context_run=material_context_run,
            merit_path=merit_path,
            exe=exe,
            args=args,
        ):
            cached_report["contract"] = _module_policy.MATERIAL_BRIDGE_STAGE_CONTRACT
            cached_report["stage_cache_hit"] = True
            _module_files.write_json(report_path, cached_report)
            print(
                "[material bridge] reuse completed CPU-materialized bridge stage",
                flush=True,
            )
            return list(cached_report.get("selected") or []), bridge_dir
    return None


def prepare_work(args: argparse.Namespace, latest_rows: list[dict[str, Any]], inputs: BridgeInputs, actions: BridgeStageActions):
    """Invalidate incomplete publication and initialize explicit bridge diagnostic products."""
    merit_path = inputs.merit_path
    exe = inputs.exe
    report_path = inputs.report_path
    report_path.unlink(missing_ok=True)
    input_binding = actions.bridge_stage_input_binding(latest_rows, merit_path, exe, args)
    diagnostics: list[dict[str, Any]] = []
    vignetting_bridge_reports: list[dict[str, Any]] = []
    multi_island_waves: list[dict[str, Any]] = []
    return BridgeWork(input_binding, diagnostics, vignetting_bridge_reports, multi_island_waves)


def diagnose_sources(args: argparse.Namespace, latest_rows: list[dict[str, Any]], inputs: BridgeInputs, work: BridgeWork, actions: BridgeStageActions):
    """Calibrate each source or record its hard rejection, then evaluate the inherited/common contract."""
    bridge_dir = inputs.bridge_dir
    merit = inputs.merit
    exe = inputs.exe
    diagnostics = work.diagnostics
    vignetting_bridge_reports = work.vignetting_bridge_reports
    for position, source_row in enumerate(latest_rows, start=1):
        rank = int(_module_files.finite_float(source_row.get("basin_rank"), position))
        original_source = Path(str(source_row.get("out_dir") or "")).resolve()
        case_root = server.native_io_paths.named_child(server.native_io_paths.named_child(bridge_dir, 'diagnostics'), f'cand_{position:03d}_rank_{rank:04d}')
        try:
            source, vignetting_bridge_report = actions.prepare_aperture_realized_bridge_source(
                source=original_source,
                bridge_root=server.native_io_paths.named_child(case_root, 'vignetting_field_bridge'),
                merit=merit,
                nodes=_module_contracts.field_nodes(args),
                args=args,
            )
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            failure = {
                "contract": _module_policy.MATERIAL_BRIDGE_VIGNETTING_CONTRACT,
                "performed": True,
                "passed": False,
                "hard_reject": True,
                "source": str(original_source),
                "target_nodes": list(_module_contracts.field_nodes(args)),
                "failure_type": type(exc).__name__,
                "failure": str(exc),
            }
            failure_path = (
                server.native_io_paths.named_child(case_root, 'vignetting_field_bridge')
                / "vignetting_bridge_failure.json"
            )
            _module_files.write_json(failure_path, failure)
            vignetting_bridge_reports.append({"basin_rank": rank, **failure})
            rejected_score = {
                "merit": 1.0e12,
                "trace_ok": False,
                "trace_coverage": 0.0,
                "min_field_coverage": 0.0,
                "min_chief_progress": 0.0,
                "mean_spot": math.nan,
                "max_spot": math.nan,
                "constraint_group_error": math.inf,
                "min_edge": -1.0e300,
            }
            item = {
                **source_row,
                "geometry_out_dir": str(original_source),
                "pre_vignetting_bridge_out_dir": str(original_source),
                "vignetting_bridge_performed": True,
                "vignetting_bridge_report": str(failure_path),
                "structural_topology_key": source_row.get(
                    "structural_topology_key"
                )
                or source_row.get("topology_key"),
                "bridge_classification": "hard_vignetting_bridge_reject",
                "bridge_trace_valid": False,
                "bridge_failure": str(exc),
            }
            _module_diagnostics.apply_effective_score(
                item,
                rejected_score,
                original_source,
                "vignetting_bridge_reject",
            )
            diagnostics.append(item)
            _module_files.write_csv(
                bridge_dir / "bridge_candidate_diagnostics_partial.csv",
                diagnostics,
            )
            print(
                f"[material bridge {position}/{len(latest_rows)}] rank={rank} "
                f"hard reject: {exc}",
                flush=True,
            )
            continue
        vignetting_bridge_reports.append({
            "basin_rank": rank,
            **vignetting_bridge_report,
        })
        print(
            f"[material bridge {position}/{len(latest_rows)}] rank={rank} "
            + ("fixed-aperture APG calibration + APG/CP retrace"
               if vignetting_bridge_report.get("performed", False)
               else "illumination disabled; original pupil scoring"),
            flush=True,
        )
        vignetting_bridge_performed = bool(
            vignetting_bridge_report.get("performed", False)
        )
        if vignetting_bridge_performed:
            # The active-vignetting bridge has already executed the complete
            # transition contract: fixed-aperture APG Set Vignetting, followed
            # by strict APG and CP retraces with aperture updates.  Its CP score
            # and system are the handoff.  Do not repeat inherited/synchronized
            # scoring, CPU/CUDA diagnostics, constraint-shock classification,
            # or repair probes before the common deepen stage.
            contract_score = _module_files.read_json(_module_files.source_file(source, "score.json"))
            contract_score = {
                **contract_score,
                "returncode": 0,
                "timed_out": False,
                "aiming_mode": "calibrated_paraxial",
                "result_dir": str(source),
            }
            item = {
                **source_row,
                "geometry_out_dir": str(source),
                "pre_vignetting_bridge_out_dir": str(original_source),
                "vignetting_bridge_performed": True,
                "vignetting_bridge_report": str(
                    server.native_io_paths.named_child(case_root, 'vignetting_field_bridge')
                    / "vignetting_bridge_report.json"
                ),
                "structural_topology_key": source_row.get(
                    "structural_topology_key"
                ) or source_row.get("topology_key"),
                "bridge_classification": "stable_common",
                "bridge_trace_valid": True,
                "bridge_retrace_contract_passed": True,
                "redundant_dual_aperture_score_skipped": True,
                "constraint_shock_diagnostic_skipped": True,
                "repair_probe_eligible": False,
                **_module_diagnostics.metric_columns("synced", contract_score),
            }
            _module_diagnostics.apply_effective_score(
                item,
                contract_score,
                source,
                "minimal_vignetting_retrace_handoff",
            )
            diagnostics.append(item)
            _module_files.write_csv(
                bridge_dir / "bridge_candidate_diagnostics_partial.csv",
                diagnostics,
            )
            continue
        inherited = actions.score_parent(
            source=(original_source if vignetting_bridge_performed else source),
            case_dir=case_root / "inherited_aperture",
            merit=merit,
            exe=exe,
            args=args,
        )
        synced = actions.score_parent(
            source=source,
            case_dir=case_root / "synchronized_aperture",
            merit=merit,
            exe=exe,
            args=args,
        )
        item = {
            **source_row,
            "geometry_out_dir": str(source),
            "pre_vignetting_bridge_out_dir": str(original_source),
            "vignetting_bridge_performed": bool(
                vignetting_bridge_report.get("performed", False)
            ),
            "vignetting_bridge_report": str(
                server.native_io_paths.named_child(case_root, 'vignetting_field_bridge') / "vignetting_bridge_report.json"
            ) if vignetting_bridge_report.get("performed", False) else "",
            "structural_topology_key": source_row.get("structural_topology_key") or source_row.get("topology_key"),
            **_module_diagnostics.metric_columns("inherited", inherited),
            **_module_diagnostics.metric_columns("synced", synced),
        }
        item.update(_module_diagnostics.classify_diagnostic(item, args))
        _module_diagnostics.apply_effective_score(item, synced, source, "synchronized_parent")
        diagnostics.append(item)
        _module_files.write_csv(bridge_dir / "bridge_candidate_diagnostics_partial.csv", diagnostics)


def repair_candidates(args: argparse.Namespace, inputs: BridgeInputs, work: BridgeWork, actions: BridgeStageActions):
    """Select repair probes, retain original seed order and publish completed/failing probe diagnostics."""
    bridge_dir = inputs.bridge_dir
    merit = inputs.merit
    exe = inputs.exe
    diagnostics = work.diagnostics
    multi_island_waves = work.multi_island_waves
    shock_rows = [
        row
        for row in diagnostics
        if row.get("bridge_classification") == "aperture_shock_repairable"
    ]
    forced_ranks = set(_module_files.parse_int_list(getattr(args, "selection_ranks", ""))) | set(
        _module_files.parse_int_list(getattr(args, "material_entry_ranks", ""))
    )
    shock_rows.sort(
        key=lambda row: (
            int(_module_files.finite_float(row.get("basin_rank"), -1)) not in forced_ranks,
            _module_files.finite_float(row.get("synced_mean_spot"), math.inf),
            _module_files.finite_float(row.get("synced_merit"), math.inf),
        )
    )
    probe_limit = max(0, int(getattr(args, "material_bridge_repair_probe_count", 4)))
    probe_rows = shock_rows[:probe_limit]
    repair_summaries: list[dict[str, Any]] = []
    base_seed = int(getattr(args, "material_bridge_seed", 20260719))
    repair_generations = max(
        1, int(getattr(args, "material_bridge_repair_generations", 150))
    )
    repair_jobs: list[dict[str, Any]] = []
    repair_contexts: list[tuple[dict[str, Any], int]] = []
    for position, row in enumerate(probe_rows, start=1):
        rank = int(_module_files.finite_float(row.get("basin_rank"), position))
        source = Path(str(row.get("geometry_out_dir"))).resolve()
        print(
            f"[material bridge repair {position}/{len(probe_rows)}] rank={rank} "
            f"DE={repair_generations}",
            flush=True,
        )
        repair_jobs.append(
            {
                "source": source,
                "case_dir": server.native_io_paths.named_child(bridge_dir / 'repair_probes', f'cand_{position:03d}_rank_{rank:04d}'),
                "merit": merit,
                "generations": repair_generations,
                "population": max(
                    20, int(getattr(args, "material_bridge_repair_population", 240))
                ),
                "population_scale": str(
                    getattr(args, "material_bridge_repair_population_scale", "tight")
                    or "tight"
                ),
                "seed": base_seed + rank * 1009,
                "purpose": "material_bridge_aperture_repair",
            }
        )
        repair_contexts.append((row, rank))
    if repair_jobs:
        repair_results, repair_policy = actions.run_de_wave(
            repair_jobs,
            exe=exe,
            args=args,
            wave_name="repair",
        )
        multi_island_waves.append({"wave": "repair", **repair_policy})
    else:
        repair_results = []
        multi_island_waves.append(
            {
                "wave": "repair",
                "default_enabled": True,
                "requested": bool(
                    getattr(args, "material_bridge_multi_island_enabled", True)
                ),
                "eligible": False,
                "used": False,
                "island_count": 0,
                "fallback_reasons": ["no repair candidates"],
            }
        )
    for (row, rank), result in zip(repair_contexts, repair_results):
        before = {
            "constraint_group_error": row.get("synced_constraint_group_error"),
            "mean_spot": row.get("synced_mean_spot"),
            "max_spot": row.get("synced_max_spot"),
        }
        assessment = _module_continuation.repair_assessment(before, result, args)
        row.update(_module_diagnostics.metric_columns("repair", result))
        row.update(assessment)
        row["repair_generations"] = repair_generations
        if assessment["repair_success"]:
            row["bridge_classification"] = (
                "aperture_shock_repaired"
                if assessment["repair_strict_success"]
                else "aperture_shock_repaired_tradeoff"
            )
            _module_diagnostics.apply_effective_score(
                row,
                result,
                Path(str(result["result_dir"])).resolve(),
                "repair_probe",
            )
            _module_diagnostics.write_bridge_basin_meta(row, Path(str(result["result_dir"])).resolve())
        else:
            row["bridge_classification"] = "aperture_shock_unrepaired"
        repair_summaries.append(
            {
                "basin_rank": rank,
                "geometry_out_dir": row.get("geometry_out_dir"),
                **_module_diagnostics.metric_columns("before", before),
                **_module_diagnostics.metric_columns("after", result),
                **assessment,
                "result_dir": result.get("result_dir"),
            }
        )
        _module_files.write_csv(bridge_dir / "repair_probe_summary_partial.csv", repair_summaries)

    probed_ranks = {
        int(_module_files.finite_float(row.get("basin_rank"), -1)) for row in probe_rows
    }
    for row in shock_rows:
        rank = int(_module_files.finite_float(row.get("basin_rank"), -1))
        if rank not in probed_ranks:
            row["bridge_classification"] = "aperture_shock_not_probed"
            row["repair_success"] = False

    _module_files.write_csv(bridge_dir / "bridge_candidate_diagnostics.csv", diagnostics)
    _module_files.write_csv(bridge_dir / "repair_probe_summary.csv", repair_summaries)
    return RepairWork(repair_summaries, base_seed, probe_limit, repair_generations)


def prepare_common_candidates(args: argparse.Namespace, inputs: BridgeInputs, work: BridgeWork):
    """Normalize the measured candidates before the shared common-contract continuation."""
    bridge_dir = inputs.bridge_dir
    diagnostics = work.diagnostics
    bridge_rows = [_module_diagnostics.normalized_entry_row(row, args) for row in diagnostics]
    _module_files.write_csv(bridge_dir / "candidates_before_deepen.csv", bridge_rows)

    deepen_summaries: list[dict[str, Any]] = []
    return CommonCandidates(bridge_rows, deepen_summaries)


def publish_selection(args: argparse.Namespace, inputs: BridgeInputs, common: CommonCandidates, actions: BridgeStageActions):
    """Reselect actual post-extension systems and write all compatibility handoff artifacts."""
    bridge_dir = inputs.bridge_dir
    bridge_rows = common.bridge_rows
    deepen_summaries = common.deepen_summaries
    selected = actions.select_lanes(args, bridge_rows)
    selected = [_module_diagnostics.normalized_entry_row(row, args) for row in selected]
    _module_files.write_csv(bridge_dir / "selected_after_deepen.csv", selected)

    # Compatibility artifact: its contents now deliberately represent the
    # post-deepen selection.  New consumers should use selected_after_deepen.
    _module_files.write_csv(bridge_dir / "selected_before_deepen.csv", selected)
    _module_files.write_csv(bridge_dir / "deepen_summary.csv", deepen_summaries)
    _module_files.write_csv(bridge_dir / "selected_material_entries.csv", selected)
    return selected


def run_bridge(args: argparse.Namespace, latest_rows: list[dict[str, Any]], *, material_context_run: Path, actions: BridgeStageActions) -> tuple[list[dict[str, Any]], Path]:
    """Coordinate bound cache, calibration, repair, continuation and publication phases."""
    inputs = prepare_inputs(args, material_context_run)
    cached = reuse_completed_stage(args, latest_rows, material_context_run, inputs, actions)
    if cached is not None:
        return cached
    work = prepare_work(args, latest_rows, inputs, actions)
    diagnose_sources(args, latest_rows, inputs, work, actions)
    repair = repair_candidates(args, inputs, work, actions)
    common = prepare_common_candidates(args, inputs, work)
    if bool(getattr(args, "material_bridge_deepen_enabled", True)):
        outcome = deepen_candidates(args, inputs, work, repair, common, actions)
        extend_selected(args, outcome, inputs, work, repair, common, actions)
    else:
        retain_without_deepen(args, inputs, common, actions)
    selected = publish_selection(args, inputs, common, actions)
    publish_report(args, latest_rows, material_context_run, inputs, work, repair, common, selected, actions)
    return selected, inputs.bridge_dir
