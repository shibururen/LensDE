"""Ordered material-bridge receipt schema families and publication."""
from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any
from lensde.workflow import selection as strict_topology
from . import contracts as _module_contracts
from . import files as _module_files
from . import policy as _module_policy
from . import ranking as _module_ranking
from . import selection as _module_selection
from .state import BridgeInputs, BridgeWork, RepairWork, CommonCandidates, DeepenSelection, BridgeStageActions, BridgeWaveActions


def report_contract_and_calibration(args: argparse.Namespace, latest_rows: list[dict[str, Any]], material_context_run: Path, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, selected: list[dict[str, Any]], actions: BridgeStageActions) -> dict[str, Any]:
    """Retain the ordered contract and calibration fields of bridge_report."""
    merit_path = inputs.merit_path
    exe = inputs.exe
    input_binding = work.input_binding
    vignetting_bridge_reports = work.vignetting_bridge_reports
    return {
        "contract": _module_policy.MATERIAL_BRIDGE_STAGE_CONTRACT,
        "input_binding": (
            input_binding if input_binding == actions.bridge_stage_input_binding(
                latest_rows, merit_path, exe, args
            ) else None
        ),
        "layout_distance_limits": {
            "relative_group": strict_topology.LAYOUT_RELATIVE_DISTANCE_FLOOR,
            "individual_parameter": strict_topology.LAYOUT_PEAK_DISTANCE_FLOOR,
        },
        "stage_cache_hit": False,
        "enabled": True,
        "policy": (
            "active vignetting: fixed-aperture APG calibration then strict APG/CP "
            "aperture-updating retrace with no duplicate diagnostic/repair gate; "
            "non-vignetting fallback retains legacy aperture diagnostic; "
            "merit/potential-priority entry selection; "
            "common-contract pure-DE deepen for every handoff candidate; "
            "post-deepen material-entry selection"
        ),
        "aiming_mode_policy": "inherit_system_ray_aiming_mode",
        "vignetting_field_bridge": {
            "enabled_when_minimum_relative_illumination_positive": True,
            "target_field_hy_interpolation": False,
            "source_fields_determine_apertures": True,
            "target_fields_solved_together_by_set_vignetting": True,
            "formal_axis_policy": "full APG measured coefficients, no projection",
            "post_measurement_retrace_modes": [
                "adaptive_pilot_grid",
                "calibrated_paraxial",
            ],
            "post_measurement_clear_aperture_update": True,
            "active_vignetting_redundant_diagnostics_removed": True,
            "reports": vignetting_bridge_reports,
        },
    }


def report_measurement_inputs(args: argparse.Namespace, latest_rows: list[dict[str, Any]], material_context_run: Path, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, selected: list[dict[str, Any]], actions: BridgeStageActions) -> dict[str, Any]:
    """Retain the ordered measurement inputs fields of bridge_report."""
    merit_path = inputs.merit_path
    exe = inputs.exe
    return {
        "material_context_run": str(material_context_run.resolve()),
        "common_merit_source": str(merit_path),
        "executable": str(exe),
        "field_hy": list(_module_contracts.field_nodes(args)),
        "rings": max(1, int(getattr(args, "material_bridge_trace_rays", 6))),
        "arms": max(1, int(getattr(args, "material_bridge_trace_arms", 8))),
        "distribution": str(getattr(args, "material_bridge_trace_distribution", "gaussian_ring_arm")),
        "constraint_threshold": float(getattr(args, "material_bridge_constraint_threshold", 2.5)),
        "minimum_trace_coverage": _module_contracts.minimum_trace_coverage(args),
        "max_image_growth": float(getattr(args, "material_bridge_max_image_growth", 0.15)),
    }


def report_repair_and_continuation(args: argparse.Namespace, latest_rows: list[dict[str, Any]], material_context_run: Path, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, selected: list[dict[str, Any]], actions: BridgeStageActions) -> dict[str, Any]:
    """Retain the ordered repair and continuation fields of bridge_report."""
    multi_island_waves = work.multi_island_waves
    probe_limit = repair.probe_limit
    repair_generations = repair.repair_generations
    return {
        "repair_probe_count": probe_limit,
        "repair_population": int(getattr(args, "material_bridge_repair_population", 240)),
        "repair_generations": repair_generations,
        "repair_population_scale": str(getattr(args, "material_bridge_repair_population_scale", "tight")),
        "repair_constraint_handling": str(
            getattr(args, "material_bridge_repair_constraint_handling", "scalar_merit")
        ),
        "repair_rescue_max_image_damage": float(
            getattr(args, "material_bridge_repair_rescue_max_image_damage", 0.35)
        ),
        "repair_rescue_max_max_image_damage": float(
            getattr(args, "material_bridge_repair_rescue_max_max_image_damage", 0.50)
        ),
        "deepen_enabled": bool(getattr(args, "material_bridge_deepen_enabled", True)),
        "deepen_population": int(getattr(args, "material_bridge_deepen_population", 400)),
        "deepen_generations": int(getattr(args, "material_bridge_deepen_generations", 250)),
        "deepen_population_scale": str(getattr(args, "material_bridge_deepen_population_scale", "medium")),
        "deepen_constraint_handling": str(
            getattr(args, "material_bridge_deepen_constraint_handling", "feasibility_rules")
        ),
        "extension_count": int(getattr(args, "material_bridge_extension_count", 2)),
        "extension_target_generations": int(
            getattr(args, "material_bridge_extension_target_generations", 400)
        ),
        "continuous_material_de": {
            "enabled_for_all_executed_islands": bool(
                [
                    contract
                    for wave in multi_island_waves
                    for contract in (
                        wave.get("continuous_material_de", {}).get("island_contracts", [])
                    )
                ]
            ) and all(
                bool(contract.get("optimization_enabled"))
                for wave in multi_island_waves
                for contract in (
                    wave.get("continuous_material_de", {}).get("island_contracts", [])
                )
            ),
            "wave_contracts": [
                {
                    "wave": wave.get("wave"),
                    **wave.get("continuous_material_de", {}),
                }
                for wave in multi_island_waves
                if wave.get("continuous_material_de")
            ],
        },
    }


def report_broker_and_selection(args: argparse.Namespace, latest_rows: list[dict[str, Any]], material_context_run: Path, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, selected: list[dict[str, Any]], actions: BridgeStageActions) -> dict[str, Any]:
    """Retain the ordered broker and selection fields of bridge_report."""
    multi_island_waves = work.multi_island_waves
    return {
        "multi_island": {
            "default_enabled": True,
            "requested": bool(
                getattr(args, "material_bridge_multi_island_enabled", True)
            ),
            "coalesce_microseconds": int(
                getattr(args, "material_bridge_multi_island_coalesce_us", 1000)
            ),
            "waves": multi_island_waves,
        },
        "entry_priority_quotas": {
            "merit": int(getattr(args, "material_entry_merit_count", 2)),
            "potential": int(getattr(args, "material_entry_potential_count", 2)),
            "diversity": int(getattr(args, "material_entry_diversity_count", 0)),
        },
        "quality_guard_ratio": float(
            getattr(args, "material_entry_quality_guard_ratio", 1.10)
        ),
        "selection_policy": (
            "legacy_explicit_lane_override"
            if _module_selection.legacy_lane_override_enabled(args)
            else (
                "pure_merit"
                if _module_selection.pure_merit_entry_enabled(
                    args, max(0, int(getattr(args, "material_entry_count", 0)))
                )
                else "merit_potential_priority"
            )
        ),
        "selection_merit_contract": _module_policy.MATERIAL_BRIDGE_SELECTION_MERIT_CONTRACT,
        "strict_structure_selection_policy": str(
            getattr(
                args,
                "strict_structure_selection_policy",
                strict_topology.LEGACY_DISTANCE_POLICY,
            )
        ),
        "legacy_lane_quotas": (
            _module_selection.lane_quotas(
                args, max(0, int(getattr(args, "material_entry_count", 0)))
            )
            if _module_selection.legacy_lane_override_enabled(args)
            else None
        ),
    }


def report_selection_outcome(args: argparse.Namespace, latest_rows: list[dict[str, Any]], material_context_run: Path, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, selected: list[dict[str, Any]], actions: BridgeStageActions) -> dict[str, Any]:
    """Retain the ordered selection outcome fields of bridge_report."""
    bridge_rows = common.bridge_rows
    return {
        "selected_ranks": [int(_module_files.finite_float(row.get("basin_rank"), -1)) for row in selected],
        "pre_deepen_candidate_count": len(bridge_rows),
        "requested_material_entry_count": max(
            0, int(getattr(args, "material_entry_count", 0))
        ),
        "post_deepen_selected_count": len(selected),
        "post_deepen_unique_topology_count": len(
            {_module_ranking.material_topology_family_key(row) for row in selected}
        ),
        "post_deepen_unique_curvature_sign_mask_count": len(
            {
                str(row.get("strict_curvature_sign_mask") or "")
                for row in selected
                if str(row.get("strict_curvature_sign_mask") or "")
            }
        ),
        "strict_skeleton_distance_floor": (
            strict_topology.STRICT_SKELETON_DISTANCE_FLOOR
        ),
        "material_entry_topology_shortfall": max(
            0,
            max(0, int(getattr(args, "material_entry_count", 0))) - len(selected),
        ),
    }


def report_result_details(args: argparse.Namespace, latest_rows: list[dict[str, Any]], material_context_run: Path, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, selected: list[dict[str, Any]], actions: BridgeStageActions) -> dict[str, Any]:
    """Retain the ordered result details fields of bridge_report."""
    diagnostics = work.diagnostics
    repair_summaries = repair.repair_summaries
    deepen_summaries = common.deepen_summaries
    return {
        "diagnostics": diagnostics,
        "repair_probes": repair_summaries,
        "deepen": deepen_summaries,
        "selected": selected,
    }


def publish_report(args: argparse.Namespace, latest_rows: list[dict[str, Any]], material_context_run: Path, inputs: BridgeInputs, work: BridgeWork, repair: RepairWork, common: CommonCandidates, selected: list[dict[str, Any]], actions: BridgeStageActions) -> None:
    """Write the completed stage receipt in the original schema/field evaluation order."""
    report: dict[str, Any] = {}
    report.update(report_contract_and_calibration(args, latest_rows, material_context_run, inputs, work, repair, common, selected, actions))
    report.update(report_measurement_inputs(args, latest_rows, material_context_run, inputs, work, repair, common, selected, actions))
    report.update(report_repair_and_continuation(args, latest_rows, material_context_run, inputs, work, repair, common, selected, actions))
    report.update(report_broker_and_selection(args, latest_rows, material_context_run, inputs, work, repair, common, selected, actions))
    report.update(report_selection_outcome(args, latest_rows, material_context_run, inputs, work, repair, common, selected, actions))
    report.update(report_result_details(args, latest_rows, material_context_run, inputs, work, repair, common, selected, actions))
    _module_files.write_json(inputs.bridge_dir / "bridge_report.json", report)
