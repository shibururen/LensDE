"""Common-contract deepen and extension phases preserving lineage seed order."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from . import continuation as _module_continuation
from . import contracts as _module_contracts
from . import diagnostics as _module_diagnostics
from . import execution as _module_execution
from . import files as _module_files
from . import ranking as _module_ranking
from .state import BridgeInputs, BridgeWork, RepairWork, CommonCandidates, DeepenSelection, BridgeStageActions, BridgeWaveActions


def deepen_candidates(args: argparse.Namespace, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, actions: BridgeStageActions):
    """Continue every eligible candidate under the common merit and reselect completed systems."""
    bridge_dir = inputs.bridge_dir
    merit = inputs.merit
    exe = inputs.exe
    multi_island_waves = work.multi_island_waves
    base_seed = repair.base_seed
    bridge_rows = common.bridge_rows
    deepen_summaries = common.deepen_summaries
    deepen_target = max(1, int(getattr(args, "material_bridge_deepen_generations", 250)))
    deepen_population = max(20, int(getattr(args, "material_bridge_deepen_population", 400)))
    deepen_scale = str(
        getattr(args, "material_bridge_deepen_population_scale", "medium") or "medium"
    )
    deepen_jobs: list[dict[str, Any]] = []
    deepen_contexts: list[tuple[dict[str, Any], int, int]] = []
    deepen_eligible_rows = [
        row for row in bridge_rows if bool(row.get("final_materialization_valid"))
    ]
    for position, row in enumerate(deepen_eligible_rows, start=1):
        rank = int(_module_files.finite_float(row.get("basin_rank"), position))
        prior_generations = (
            int(_module_files.finite_float(row.get("repair_generations"), 0))
            if bool(row.get("repair_success", False))
            else 0
        )
        generations = max(0, deepen_target - prior_generations)
        if generations <= 0:
            continue
        source = Path(str(row.get("out_dir"))).resolve()
        carry = source / "population.csv" if (source / "population.csv").is_file() else None
        print(
            f"[material bridge deepen {position}/{len(deepen_eligible_rows)}] rank={rank} "
            f"DE+={generations} target={deepen_target}",
            flush=True,
        )
        deepen_jobs.append(
            {
                "source": source,
                "case_dir": server.native_io_paths.named_child(bridge_dir / 'common_deepen', f'cand_{position:03d}_rank_{rank:04d}'),
                "merit": merit,
                "generations": generations,
                "population": deepen_population,
                "population_scale": deepen_scale,
                "seed": base_seed + 1000003 + rank * 101,
                "purpose": "material_bridge_common_deepen",
                "source_population": carry,
            }
        )
        deepen_contexts.append((row, rank, prior_generations))
    if deepen_jobs:
        deepen_results, deepen_policy = actions.run_de_wave(
            deepen_jobs,
            exe=exe,
            args=args,
            wave_name="deepen",
        )
        multi_island_waves.append({"wave": "deepen", **deepen_policy})
    else:
        deepen_results = []
        multi_island_waves.append(
            {
                "wave": "deepen",
                "default_enabled": True,
                "requested": bool(
                    getattr(args, "material_bridge_multi_island_enabled", True)
                ),
                "eligible": False,
                "used": False,
                "island_count": 0,
                "fallback_reasons": ["no deepen candidates"],
            }
        )
    for (row, rank, prior_generations), result in zip(
        deepen_contexts, deepen_results
    ):
        generations = int(_module_files.finite_float(result.get("requested_generations"), 0))
        accepted = (
            _module_execution.multi_island_materialization_consistent(result)
            and _module_contracts.trace_acceptable(result, args)
            and _module_files.finite_float(result.get("min_edge"), -math.inf)
            >= float(getattr(args, "material_bridge_repair_min_edge", 0.0))
            and 0.0 < _module_files.finite_float(result.get("merit"), math.inf) < 1.0e12
            and _module_files.finite_float(result.get("merit"), math.inf)
            <= _module_files.finite_float(row.get("effective_merit"), math.inf) + 1.0e-9
        )
        if accepted:
            _module_diagnostics.apply_effective_score(
                row,
                result,
                Path(str(result["result_dir"])).resolve(),
                "common_deepen",
            )
            row.update(_module_diagnostics.normalized_entry_row(row, args))
            if bool(row.get("repair_success", False)):
                assessment = _module_continuation.repair_assessment(
                    {
                        "constraint_group_error": row.get("synced_constraint_group_error"),
                        "mean_spot": row.get("synced_mean_spot"),
                        "max_spot": row.get("synced_max_spot"),
                    },
                    result,
                    args,
                )
                row.update(assessment)
                row["bridge_classification"] = (
                    "aperture_shock_repaired"
                    if assessment["repair_strict_success"]
                    else (
                        "aperture_shock_repaired_tradeoff"
                        if assessment["repair_success"]
                        else "aperture_shock_unrepaired"
                    )
                )
            _module_diagnostics.write_bridge_basin_meta(row, Path(str(result["result_dir"])).resolve())
        row["bridge_deepen_accepted"] = accepted
        if accepted and row.get("bridge_classification") not in {
            "stable_common",
            "aperture_shock_repaired",
            "aperture_shock_repaired_tradeoff",
        }:
            row["bridge_classification"] = "common_deepen_recovered"
        row["bridge_total_common_generations"] = prior_generations + generations
        deepen_summaries.append(
            {
                "stage": "deepen",
                "basin_rank": rank,
                "accepted": accepted,
                "prior_generations": prior_generations,
                **_module_diagnostics.metric_columns("result", result),
            }
        )

    selected = actions.select_lanes(args, bridge_rows)
    selected = [_module_diagnostics.normalized_entry_row(row, args) for row in selected]
    _module_files.write_csv(bridge_dir / "selected_after_deepen.csv", selected)
    return DeepenSelection(selected, deepen_target, deepen_population, deepen_scale)


def extend_selected(args: argparse.Namespace, outcome: DeepenSelection, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, actions: BridgeStageActions):
    """Extend the chosen candidates in stable order and propagate accepted updates to their original rows."""
    bridge_dir = inputs.bridge_dir
    merit = inputs.merit
    exe = inputs.exe
    multi_island_waves = work.multi_island_waves
    base_seed = repair.base_seed
    bridge_rows = common.bridge_rows
    deepen_summaries = common.deepen_summaries
    selected = outcome.selected
    deepen_target = outcome.deepen_target
    deepen_population = outcome.deepen_population
    deepen_scale = outcome.deepen_scale
    extension_count = max(
        0, int(getattr(args, "material_bridge_extension_count", 2))
    )
    extension_target = max(
        deepen_target,
        int(getattr(args, "material_bridge_extension_target_generations", 400)),
    )
    extension_rows = sorted(selected, key=_module_ranking.effective_key)[:extension_count]
    extension_jobs: list[dict[str, Any]] = []
    extension_contexts: list[tuple[dict[str, Any], int, int]] = []
    for position, row in enumerate(extension_rows, start=1):
        rank = int(_module_files.finite_float(row.get("basin_rank"), position))
        prior_generations = int(
            _module_files.finite_float(row.get("bridge_total_common_generations"), deepen_target)
        )
        generations = max(0, extension_target - prior_generations)
        if generations <= 0:
            continue
        source = Path(str(row.get("out_dir"))).resolve()
        carry = source / "population.csv" if (source / "population.csv").is_file() else None
        print(
            f"[material bridge extend {position}/{len(extension_rows)}] rank={rank} "
            f"DE+={generations} target={extension_target}",
            flush=True,
        )
        extension_jobs.append(
            {
                "source": source,
                "case_dir": server.native_io_paths.named_child(bridge_dir / 'common_extension', f'cand_{position:03d}_rank_{rank:04d}'),
                "merit": merit,
                "generations": generations,
                "population": deepen_population,
                "population_scale": deepen_scale,
                "seed": base_seed + 2000003 + rank * 103,
                "purpose": "material_bridge_common_extension",
                "source_population": carry,
            }
        )
        extension_contexts.append((row, rank, prior_generations))
    if extension_jobs:
        extension_results, extension_policy = actions.run_de_wave(
            extension_jobs,
            exe=exe,
            args=args,
            wave_name="extension",
        )
        multi_island_waves.append({"wave": "extension", **extension_policy})
    else:
        extension_results = []
        multi_island_waves.append(
            {
                "wave": "extension",
                "default_enabled": True,
                "requested": bool(
                    getattr(args, "material_bridge_multi_island_enabled", True)
                ),
                "eligible": False,
                "used": False,
                "island_count": 0,
                "fallback_reasons": ["no extension candidates"],
            }
        )
    for (row, rank, prior_generations), result in zip(
        extension_contexts, extension_results
    ):
        generations = int(_module_files.finite_float(result.get("requested_generations"), 0))
        accepted = (
            _module_execution.multi_island_materialization_consistent(result)
            and _module_contracts.trace_acceptable(result, args)
            and _module_files.finite_float(result.get("min_edge"), -math.inf)
            >= float(getattr(args, "material_bridge_repair_min_edge", 0.0))
            and 0.0 < _module_files.finite_float(result.get("merit"), math.inf) < 1.0e12
            and _module_files.finite_float(result.get("merit"), math.inf)
            <= _module_files.finite_float(row.get("effective_merit"), math.inf) + 1.0e-9
        )
        if accepted:
            _module_diagnostics.apply_effective_score(
                row,
                result,
                Path(str(result["result_dir"])).resolve(),
                "common_extension",
            )
            row.update(_module_diagnostics.normalized_entry_row(row, args))
            if bool(row.get("repair_success", False)):
                assessment = _module_continuation.repair_assessment(
                    {
                        "constraint_group_error": row.get("synced_constraint_group_error"),
                        "mean_spot": row.get("synced_mean_spot"),
                        "max_spot": row.get("synced_max_spot"),
                    },
                    result,
                    args,
                )
                row.update(assessment)
                row["bridge_classification"] = (
                    "aperture_shock_repaired"
                    if assessment["repair_strict_success"]
                    else (
                        "aperture_shock_repaired_tradeoff"
                        if assessment["repair_success"]
                        else "aperture_shock_unrepaired"
                    )
                )
            _module_diagnostics.write_bridge_basin_meta(row, Path(str(result["result_dir"])).resolve())
        row["bridge_extension_accepted"] = accepted
        row["bridge_total_common_generations"] = prior_generations + generations
        if not _module_diagnostics.propagate_bridge_row_update(bridge_rows, row):
            raise RuntimeError(
                "material bridge extension result could not be mapped back "
                f"to candidate {_module_diagnostics.bridge_row_identity(row)!r}"
            )
        deepen_summaries.append(
            {
                "stage": "extension",
                "basin_rank": rank,
                "accepted": accepted,
                "prior_generations": prior_generations,
                **_module_diagnostics.metric_columns("result", result),
            }
        )


def retain_without_deepen(args: argparse.Namespace, inputs: BridgeInputs, common: CommonCandidates, actions: BridgeStageActions):
    """Retain the original selection/publication calls when common deepening is disabled."""
    bridge_dir = inputs.bridge_dir
    bridge_rows = common.bridge_rows
    selected = actions.select_lanes(args, bridge_rows)
    selected = [_module_diagnostics.normalized_entry_row(row, args) for row in selected]
    _module_files.write_csv(bridge_dir / "selected_after_deepen.csv", selected)
