"""Ordered checkpoint configuration schema families and publication."""
from __future__ import annotations

from pathlib import Path
from typing import Any, Iterable
from lensde.workflow import selection as strict_topology
from . import files as _module_files
from . import geometry as _module_geometry
from . import policy as _module_policy
from . import snapshots as _module_snapshots
from .state import CheckpointInputs, CheckpointSelection, ContinuationPlan, CheckpointHandoff, CheckpointActions


def config_start_inventory(args: Any, inputs: CheckpointInputs, selection: CheckpointSelection, plan: ContinuationPlan) -> dict[str, Any]:
    """Retain the ordered start inventory fields of checkpoint_de_config."""
    reuse_manifest = inputs.reuse_manifest
    rank1_by_outer = selection.rank1_by_outer
    requested_total_count = selection.requested_total_count
    rank1_anchor_outers = selection.rank1_anchor_outers
    checkpoints = selection.checkpoints
    items = plan.items
    return {
        "checkpoint_outer_root": (
            str(Path(args.checkpoint_outer_root).resolve())
            if getattr(args, "checkpoint_outer_root", None)
            else ""
        ),
        "checkpoint_reuse_run_manifest": (
            str(reuse_manifest) if reuse_manifest else ""
        ),
        "outer_count": len(rank1_by_outer),
        "available_rank1_count": len(rank1_by_outer),
        "rank1_anchor_outers": rank1_anchor_outers,
        "rank1_arm_count": sum(
            str(item.get("run_role") or "") == "rank1" for item in items
        ),
        "checkpoint_parent_count": len(checkpoints),
        "checkpoint_arm_count": sum(
            str(item.get("run_role") or "") == "checkpoint" for item in items
        ),
        "checkpoint_budget_shortfall": max(0, requested_total_count - len(items)),
    }


def config_source_admission(args: Any, inputs: CheckpointInputs, selection: CheckpointSelection, plan: ContinuationPlan) -> dict[str, Any]:
    """Retain the ordered source admission fields of checkpoint_de_config."""
    reuse_manifest = inputs.reuse_manifest
    outer_dirs = selection.outer_dirs
    checkpoints = selection.checkpoints
    items = plan.items
    return {
        "unique_source_topology_count": len(
            {_module_geometry.checkpoint_topology_identity(item) for item in items}
        ),
        "duplicate_source_topology_arm_count": len(items) - len(
            {_module_geometry.checkpoint_topology_identity(item) for item in items}
        ),
        "unique_source_topology_shape_layout_count": len(
            {_module_geometry.checkpoint_topology_shape_layout(item) for item in items}
        ),
        "source_topology_uniqueness_required": False,
        "checkpoint_topology_uniqueness_required": not bool(reuse_manifest),
        "checkpoint_structure_distance_policy": (
            "reused_manifest" if reuse_manifest else str(getattr(
                args, "strict_structure_selection_policy", strict_topology.LEGACY_DISTANCE_POLICY
            ))
        ),
        "source_topology_novelty_role": (
            "fixed_outer_anchors_preserved; additional_checkpoints_use_downstream_strict_admission"
        ),
        "topology_power_swap": {
            "enabled": False,
            "reason": "replaced_by_shared_downstream_structural_admission",
            "replacement_limit": _module_policy.CHECKPOINT_TOPOLOGY_REPLACEMENT_LIMIT,
            "power_distance_floor": (
                _module_policy.CHECKPOINT_TOPOLOGY_POWER_DISTANCE_FLOOR
            ),
            "effective_distance_floor": (
                _module_policy.CHECKPOINT_TOPOLOGY_EFFECTIVE_DISTANCE_FLOOR
            ),
            "actual_replacement_count": sum(
                str(row.get("selection_reason") or "")
                == "topology_power_swap"
                for row in checkpoints
            ),
        },
        "exact_state_uniqueness_required": True,
        "initial_parameter_distance_multiplier": 1.0,
        "parameter_distance_relaxation_enabled": False,
        "merit_relaxation_caps": list(_module_policy.CHECKPOINT_MERIT_RELAXATION_CAPS),
        "shortfall_policy": "continue_with_available_distinct_candidates",
        "candidate_sources": [
            {"outer": outer, "stage": stage["phase"], "directory": str(stage["dir"]),
             "generation_offset": stage["offset"],
             "snapshot_file": str(stage["dir"] / "checkpoint_candidate_snapshots/candidate_events.jsonl"),
             "basin_file": str(stage["dir"] / "basin_archive.json")}
            for outer, outer_dir in enumerate(outer_dirs, 1)
            for stage in _module_snapshots.candidate_stage_sources(outer_dir)
        ],
        "parameter_distance_relaxation_tiers": list(
            _module_policy.CHECKPOINT_DISTANCE_RELAXATION_TIERS
        ),
    }


def config_handoff_admission(args: Any, inputs: CheckpointInputs, selection: CheckpointSelection, plan: ContinuationPlan) -> dict[str, Any]:
    """Retain the ordered handoff admission fields of checkpoint_de_config."""
    requested_total_count = selection.requested_total_count
    rank1_anchor_outers = selection.rank1_anchor_outers
    checkpoints = selection.checkpoints
    return {
        "handoff_topology_uniqueness_required": True,
        "handoff_curvature_sign_parameter_uniqueness_required": True,
        "handoff_strict_skeleton_distance_floor": (
            strict_topology.STRICT_SKELETON_DISTANCE_FLOOR
        ),
        "handoff_structure_distance_policy": str(
            getattr(
                args,
                "strict_structure_selection_policy",
                strict_topology.LEGACY_DISTANCE_POLICY,
            )
        ),
        "requested_total_count": requested_total_count,
        "requested_checkpoint_arm_count": max(
            0, requested_total_count - len(rank1_anchor_outers)
        ),
        "selection_reason_counts": dict(
            sorted(
                {
                    reason: sum(
                        str(row.get("selection_reason") or "") == reason
                        for row in checkpoints
                    )
                    for reason in {
                        str(row.get("selection_reason") or "unknown")
                        for row in checkpoints
                    }
                }.items()
            )
        ),
        "merit_relaxation_enabled": bool(
            getattr(args, "checkpoint_merit_relaxation", True)
        ),
        "basin_fill_enabled": bool(
            getattr(args, "checkpoint_basin_fill", True)
        ),
        "basin_archive_fill_count": sum(
            str(row.get("record") or "") == "basin_archive"
            for row in checkpoints
        ),
        "distance_admission_counts": dict(
            sorted(
                {
                    admission: sum(
                        str(row.get("selection_distance_admission") or "")
                        == admission
                        for row in checkpoints
                    )
                    for admission in {
                        str(
                            row.get("selection_distance_admission")
                            or "strict_distance"
                        )
                        for row in checkpoints
                    }
                }.items()
            )
        ),
        "distance_relaxed_arm_count": sum(
            str(row.get("selection_distance_admission") or "strict_distance")
            != "strict_distance"
            for row in checkpoints
        ),
        "minimum_used_distance_multiplier": min(
            (
                _module_files.finite(row.get("selection_distance_multiplier"), 1.0)
                for row in checkpoints
            ),
            default=None,
        ),
        "basin_archive_fill_quality_cap": _module_policy.BASIN_FILL_QUALITY_CAP,
        "maximum_relaxed_merit_ratio": _module_policy.CHECKPOINT_MERIT_RELAXATION_CAPS[-1],
    }


def config_continuation_budget(args: Any, inputs: CheckpointInputs, selection: CheckpointSelection, plan: ContinuationPlan) -> dict[str, Any]:
    """Retain the ordered continuation budget fields of checkpoint_de_config."""
    material_variable_contract = inputs.material_variable_contract
    items = plan.items
    return {
        "continuous_material_de": material_variable_contract,
        "population": int(args.checkpoint_de_population),
        "generations": int(args.checkpoint_de_generations),
        "selected_count": int(args.checkpoint_selected_count),
        "s400_preview_count": int(args.checkpoint_selected_count),
        "handoff_policy": "merit_potential_diversity_to_material_common_diagnostic",
        "candidate_count": len(items),
        "requested_handoff_count": int(
            getattr(args, "checkpoint_handoff_count", 0)
        ),
        "handoff_count": None,
        "handoff_priority_quotas": {
            "merit": int(getattr(args, "checkpoint_handoff_merit_count", 4)),
            "potential": int(
                getattr(args, "checkpoint_handoff_potential_count", 2)
            ),
            "diversity": int(
                getattr(args, "checkpoint_handoff_diversity_count", 1)
            ),
        },
        "population_scale": str(args.checkpoint_population_scale),
    }


def config_score_and_execution(args: Any, inputs: CheckpointInputs, selection: CheckpointSelection, plan: ContinuationPlan) -> dict[str, Any]:
    """Retain the ordered score and execution fields of checkpoint_de_config."""
    multi_island = plan.multi_island
    use_multi_island = plan.use_multi_island
    return {
        "score": (
            "S = log(R_cpu/M_final_cpu) + "
            "log(M_checkpoint_cuda/M_final_cuda)"
        ),
        "score_contract": _module_policy.CHECKPOINT_SCORE_CONTRACT,
        "quality_cap": _module_policy.QUALITY_CAP,
        "core_quality_cap": _module_policy.CORE_QUALITY_CAP,
        "final_rank1_core_quality_cap": _module_policy.FINAL_RANK1_CORE_QUALITY_CAP,
        "final_rank1_potential_quality_cap": _module_policy.FINAL_RANK1_POTENTIAL_QUALITY_CAP,
        "final_rank1_potential_min_improvement": _module_policy.FINAL_RANK1_POTENTIAL_MIN_IMPROVEMENT,
        "selection": (
            "fixed Outer anchors plus checkpoints admitted by the same strict "
            "topology/structure predicate and distance policy as the downstream handoff; "
            "finite merit admission up to 100x, with no distance relaxation"
        ),
        "outer_basin_policy": "preserved upstream; DE continuation is basin-free",
        "execution_mode": (
            "batched_multi_island_experimental"
            if use_multi_island
            else "serial_process"
        ),
        "multi_island": multi_island,
        "production_default_changed": True,
    }


def write_continuation_config(args: Any, inputs: CheckpointInputs, selection: CheckpointSelection, plan: ContinuationPlan, layer_dir: Path) -> None:
    """Assemble schema families in the exact original dictionary insertion order."""
    config: dict[str, Any] = {}
    config.update(config_start_inventory(args, inputs, selection, plan))
    config.update(config_source_admission(args, inputs, selection, plan))
    config.update(config_handoff_admission(args, inputs, selection, plan))
    config.update(config_continuation_budget(args, inputs, selection, plan))
    config.update(config_score_and_execution(args, inputs, selection, plan))
    _module_files.write_json(layer_dir / "checkpoint_de_config.json", config)
