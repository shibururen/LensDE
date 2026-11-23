"""Named wave execution strategy and physical repair acceptance."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Any
from . import contracts as _module_contracts
from . import execution as _module_execution
from . import files as _module_files
from .state import BridgeInputs, BridgeWork, RepairWork, CommonCandidates, DeepenSelection, BridgeStageActions, BridgeWaveActions


def repair_assessment(
    before: dict[str, Any], after: dict[str, Any], args: argparse.Namespace
) -> dict[str, Any]:
    before_constraint = max(0.0, _module_files.finite_float(before.get("constraint_group_error"), math.inf))
    after_constraint = max(0.0, _module_files.finite_float(after.get("constraint_group_error"), math.inf))
    before_mean = _module_files.finite_float(before.get("mean_spot"), math.inf)
    after_mean = _module_files.finite_float(after.get("mean_spot"), math.inf)
    before_max = _module_files.finite_float(before.get("max_spot"), math.inf)
    after_max = _module_files.finite_float(after.get("max_spot"), math.inf)
    constraint_reduction = (
        max(0.0, 1.0 - after_constraint / max(before_constraint, 1.0e-12))
        if math.isfinite(before_constraint) and math.isfinite(after_constraint)
        else -math.inf
    )
    image_damage = (
        max(0.0, after_mean / max(before_mean, 1.0e-12) - 1.0)
        if math.isfinite(before_mean) and math.isfinite(after_mean)
        else math.inf
    )
    max_image_damage = (
        max(0.0, after_max / max(before_max, 1.0e-12) - 1.0)
        if math.isfinite(before_max) and math.isfinite(after_max)
        else math.inf
    )
    threshold = float(getattr(args, "material_bridge_constraint_threshold", 2.5))
    constraint_repaired = (
        after_constraint <= threshold
        or constraint_reduction
        >= float(getattr(args, "material_bridge_repair_min_constraint_reduction", 0.80))
    )
    trace_ok = _module_contracts.trace_acceptable(after, args)
    materialization_consistent = _module_execution.multi_island_materialization_consistent(after)
    strict_success = (
        materialization_consistent
        and
        trace_ok
        and constraint_repaired
        and image_damage
        <= float(getattr(args, "material_bridge_repair_max_image_damage", 0.15))
        and max_image_damage
        <= float(getattr(args, "material_bridge_repair_max_max_image_damage", 0.25))
        and _module_files.finite_float(after.get("min_edge"), -math.inf)
        >= float(getattr(args, "material_bridge_repair_min_edge", 0.0))
    )
    provisional_success = (
        materialization_consistent
        and
        trace_ok
        and constraint_repaired
        and image_damage
        <= float(getattr(args, "material_bridge_repair_rescue_max_image_damage", 0.35))
        and max_image_damage
        <= float(
            getattr(args, "material_bridge_repair_rescue_max_max_image_damage", 0.50)
        )
        and _module_files.finite_float(after.get("min_edge"), -math.inf)
        >= float(getattr(args, "material_bridge_repair_min_edge", 0.0))
    )
    wall_seconds = max(_module_files.finite_float(after.get("wall_seconds"), 0.0), 1.0e-9)
    repair_gain = (
        (math.log1p(before_constraint) - math.log1p(after_constraint)) / wall_seconds
        if math.isfinite(before_constraint) and math.isfinite(after_constraint)
        else -math.inf
    )
    return {
        "repair_success": provisional_success,
        "repair_strict_success": strict_success,
        "repair_tradeoff_success": provisional_success and not strict_success,
        "repair_constraint_repaired": constraint_repaired,
        "repair_constraint_reduction": constraint_reduction,
        "repair_image_damage": image_damage,
        "repair_max_image_damage": max_image_damage,
        "repair_gain_per_second": repair_gain,
        "cpu_cuda_materialization_consistent": materialization_consistent,
    }


def run_de_wave(
    jobs: list[dict[str, Any]],
    *,
    exe: Path,
    args: argparse.Namespace,
    wave_name: str,
    actions: BridgeWaveActions,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    prepared = [actions.prepare_de_case(args=args, **job) for job in jobs]
    policy = actions.material_bridge_multi_island_policy(args, prepared)
    if policy["used"]:
        print(
            f"[material bridge {wave_name}] default batched multi-island mode: "
            f"{len(prepared)} islands",
            flush=True,
        )
        results = actions.run_prepared_de_batched(
            prepared,
            exe=exe,
            args=args,
            wave_name=wave_name,
            policy=policy,
        )
        return results, policy
    elif policy["requested"] and policy["fallback_reasons"]:
        print(
            f"[material bridge {wave_name}] multi-island fallback to serial: "
            + "; ".join(policy["fallback_reasons"]),
            flush=True,
        )
    return [actions.run_prepared_de_serial(item, exe, args) for item in prepared], policy
