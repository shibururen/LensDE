"""Checkpoint preparation, selection, continuation and publication coordination."""
from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any, Iterable
from lensde.helpers import run_single_parent_local_population_ablation as local_population
from lensde.engine import core as server
from lensde.workflow import selection as strict_topology
from . import contracts as _module_contracts
from . import files as _module_files
from . import handoff as _module_handoff
from . import manifest as _module_manifest
from . import policy as _module_policy
from . import selection as _module_selection
from . import snapshots as _module_snapshots
from .state import CheckpointInputs, CheckpointSelection, ContinuationPlan, CheckpointHandoff, CheckpointActions
from .reporting import write_continuation_config


def prepare_inputs(args: Any):
    """Load the optical inputs and resolve an optional replay manifest."""
    args.basin_run_dir = Path(args.basin_run_dir).resolve()
    args.out_dir = Path(args.out_dir).resolve()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    system_path, variables_path, merit_path = (
        next(path for path in (args.basin_run_dir / "system_input.json", args.basin_run_dir / "system.json") if path.is_file()),
        args.basin_run_dir / "variables.json",
        args.basin_run_dir / "merit.json",
    )
    base_system = _module_files.read_json(system_path)
    variables_doc = _module_files.read_json(variables_path)
    merit = _module_files.read_json(merit_path)
    server.dynamic_aperture_contract.apply_merit(merit)
    glass_search_path = args.basin_run_dir / "glass_search.json"
    glass_search_config = (
        _module_files.read_json(glass_search_path) if glass_search_path.is_file() else None
    )
    variables = list(variables_doc.get("variables") or [])
    material_variable_contract = _module_contracts.continuous_material_variable_contract(variables_doc)
    if not variables:
        raise ValueError(f"no variables in {variables_path}")
    roles = [local_population.thickness_role(base_system, variable) for variable in variables]
    fallback_command = _module_files.load_command(args.basin_run_dir, [str(args.checkpoint_de_exe)])

    reuse_manifest_value = getattr(args, "checkpoint_reuse_run_manifest", None)
    reuse_manifest = (
        Path(reuse_manifest_value).resolve() if reuse_manifest_value else None
    )
    return CheckpointInputs(base_system, variables_doc, merit, glass_search_config, variables, material_variable_contract, roles, fallback_command, reuse_manifest)


def select_starts(args: Any, inputs: CheckpointInputs, actions: CheckpointActions):
    """Select checkpoint starts from the current run or a saved replay manifest."""
    base_system = inputs.base_system
    variables_doc = inputs.variables_doc
    variables = inputs.variables
    roles = inputs.roles
    reuse_manifest = inputs.reuse_manifest
    outer_dirs: list[Path] = []
    events_by_outer: dict[int, list[dict[str, Any]]] = {}
    rank1_by_outer: dict[int, dict[str, Any]] = {}
    basin_rows_by_outer: dict[int, list[dict[str, Any]]] = {}
    requested_total_count = max(
        1,
        int(getattr(args, "checkpoint_total_count", _module_policy.CHECKPOINT_TOTAL_COUNT)),
    )
    rank1_anchor_outers: list[int] = []
    if reuse_manifest is not None:
        if not reuse_manifest.is_file():
            raise FileNotFoundError(
                f"checkpoint replay manifest does not exist: {reuse_manifest}"
            )
        raw_checkpoints = _module_files.read_json(reuse_manifest)
        if not isinstance(raw_checkpoints, list) or not raw_checkpoints:
            raise ValueError(
                f"checkpoint replay manifest has no candidate rows: {reuse_manifest}"
            )
        manifest_rows = [dict(row) for row in raw_checkpoints if isinstance(row, dict)]
        if not manifest_rows:
            raise ValueError(
                f"checkpoint replay manifest has no object rows: {reuse_manifest}"
            )
        for row in manifest_rows:
            row_values = _module_files.parse_values(row)
            if len(row_values) != len(variables):
                raise ValueError(
                    f"checkpoint replay row {row.get('run_id', '<unknown>')} has "
                    f"{len(row_values)} values for {len(variables)} variables"
                )
            if str(row.get("run_role") or "").strip().lower() == "rank1":
                outer = int(row.get("outer") or 0)
                if outer > 0:
                    rank1_by_outer[outer] = row
        checkpoints = [
            row
            for row in manifest_rows
            if str(row.get("run_role") or "").strip().lower() != "rank1"
        ]
        if not checkpoints:
            raise ValueError(
                f"checkpoint replay manifest has no non-rank1 rows: {reuse_manifest}"
            )
        if not rank1_by_outer:
            raise ValueError(
                f"checkpoint replay manifest has no rank1 rows: {reuse_manifest}"
            )
        requested_total_count = len(manifest_rows)
        rank1_anchor_outers = sorted(rank1_by_outer)
        print(
            "[checkpoint DE] replaying archived run manifest "
            f"({len(checkpoints)} non-rank1 candidates + "
            f"{len(rank1_by_outer)} rank1 anchors)",
            flush=True,
        )
    else:
        outer_dirs = actions.discover_outer_dirs(Path(args.checkpoint_outer_root))
        for outer, outer_dir in enumerate(outer_dirs, 1):
            events = _module_snapshots.event_rows_for_outer(outer_dir, outer, len(variables))
            _, rank1 = _module_snapshots.final_context(events)
            events_by_outer[outer] = events
            rank1_by_outer[outer] = rank1
            basin_rows_by_outer[outer] = _module_snapshots.basin_rows_for_outer(
                outer_dir,
                outer,
                len(variables),
                rank1,
            )

        rank1_anchor_outers = _module_selection.select_rank1_anchor_outers(
            rank1_by_outer,
            min(_module_policy.RANK1_ANCHOR_LIMIT, requested_total_count),
        )
        checkpoint_budget = max(
            0,
            requested_total_count - len(rank1_anchor_outers),
        )
        checkpoints = actions.select_checkpoint_candidates(
            events_by_outer,
            rank1_by_outer,
            variables,
            roles,
            checkpoint_budget,
            basin_rows_by_outer,
            allow_merit_relaxation=bool(
                getattr(args, "checkpoint_merit_relaxation", True)
            ),
            allow_basin_fill=bool(getattr(args, "checkpoint_basin_fill", True)),
            structure_distance_policy=str(getattr(
                args, "strict_structure_selection_policy", strict_topology.LEGACY_DISTANCE_POLICY
            )),
        )
    return CheckpointSelection(outer_dirs, events_by_outer, rank1_by_outer, basin_rows_by_outer, requested_total_count, rank1_anchor_outers, checkpoints)


def rescore_anchors(args: Any, inputs: CheckpointInputs, selection: CheckpointSelection, layer_dir: Path, actions: CheckpointActions):
    """Require exact reconstructed rank-one anchors before publishing a continuation plan."""
    base_system = inputs.base_system
    variables_doc = inputs.variables_doc
    merit = inputs.merit
    glass_search_config = inputs.glass_search_config
    fallback_command = inputs.fallback_command
    rank1_by_outer = selection.rank1_by_outer
    rank1_rescore_merits: dict[int, float] = {}
    for outer, row in sorted(rank1_by_outer.items()):
        print(f"[checkpoint DE] rescore Outer {outer:02d} rank1", flush=True)
        try:
            rank1_rescore_merits[outer] = actions.rescore_rank1(
                outer=outer,
                row=row,
                args=args,
                base_system=base_system,
                variables_doc=variables_doc,
                merit=merit,
                glass_search_config=glass_search_config,
                fallback_command=fallback_command,
            )
        except RuntimeError as exc:
            result_path = (
                layer_dir / "rank1_rescore" / f"outer_{outer:02d}"
                / "rescore_result.json"
            )
            result = _module_files.read_json(result_path) if result_path.is_file() else {}
            failure = {
                "outer": outer,
                "state_id": row.get("state_id"),
                "source_merit": _module_files.finite(row.get("merit")),
                "error": str(exc),
                "rescore_result": result,
            }
            _module_files.write_json(layer_dir / "rank1_rescore_summary.json", {
                "status": "failed",
                "accepted_outers": sorted(rank1_rescore_merits),
                "failed_outer": failure,
                "policy": "exact_rank1_reconstruction_required",
            })
            raise RuntimeError(
                f"Outer {outer} rank1 reconstruction disagrees with the "
                "search record; checkpoint selection is stopped"
            ) from exc

    _module_files.write_json(layer_dir / "rank1_rescore_summary.json", {
        "status": "complete",
        "accepted_outers": sorted(rank1_rescore_merits),
        "accepted_count": len(rank1_rescore_merits),
        "policy": "exact_rank1_reconstruction_required",
    })
    return rank1_rescore_merits


def publish_start_manifests(args: Any, inputs: CheckpointInputs, selection: CheckpointSelection, layer_dir: Path, rank1_rescore_merits: dict[int, float], actions: CheckpointActions):
    """Publish selected starts, anchors and the unchanged native execution policy."""
    rank1_by_outer = selection.rank1_by_outer
    rank1_anchor_outers = selection.rank1_anchor_outers
    checkpoints = selection.checkpoints
    _module_files.write_json(layer_dir / "checkpoint_selection.json", checkpoints)
    _module_files.write_csv(
        layer_dir / "checkpoint_selection.csv",
        [
            {
                **{key: value for key, value in row.items() if key != "values"},
                "values_json": json.dumps(_module_files.parse_values(row), separators=(",", ":")),
            }
            for row in checkpoints
        ],
    )
    controls = [
        {
            "outer": outer,
            "state_id": row["state_id"],
            "source_generation": row["snapshot_generation"],
            "source_merit": row["merit"],
            "rescore_merit": rank1_rescore_merits[outer],
            "topology_key": row.get("topology_key", ""),
            "values": _module_files.parse_values(row),
        }
        for outer, row in sorted(rank1_by_outer.items())
    ]
    _module_files.write_json(layer_dir / "rank1_reference_manifest.json", controls)
    items = _module_manifest.make_run_items(
        checkpoints,
        rank1_by_outer,
        rank1_anchor_outers,
    )
    for item in items:
        item["matched_rank1_rescore_merit"] = rank1_rescore_merits[int(item["outer"])]
    _module_files.write_json(layer_dir / "run_manifest.json", items)
    multi_island = actions.checkpoint_multi_island_policy(args, len(items))
    use_multi_island = bool(multi_island["used"])
    return ContinuationPlan(items, multi_island, use_multi_island)


def continue_candidates(args: Any, inputs: CheckpointInputs, plan: ContinuationPlan, layer_dir: Path, actions: CheckpointActions):
    """Execute the planned serial or broker wave and retain partial result publication."""
    base_system = inputs.base_system
    variables_doc = inputs.variables_doc
    merit = inputs.merit
    glass_search_config = inputs.glass_search_config
    fallback_command = inputs.fallback_command
    items = plan.items
    multi_island = plan.multi_island
    use_multi_island = plan.use_multi_island
    if use_multi_island:
        print(
            f"[checkpoint DE] default batched multi-island mode: {len(items)} islands",
            flush=True,
        )
        results = actions.run_candidates_batched_experimental(
            items,
            args,
            base_system,
            variables_doc,
            merit,
            glass_search_config,
            fallback_command,
        )
        _module_files.write_csv(layer_dir / "checkpoint_de_results.csv", results)
        _module_files.write_json(layer_dir / "checkpoint_de_results.json", results)
    else:
        if multi_island["requested"] and multi_island["fallback_reasons"]:
            print(
                "[checkpoint DE] multi-island fallback to serial: "
                + "; ".join(multi_island["fallback_reasons"]),
                flush=True,
            )
        results = []
        for index, item in enumerate(items, 1):
            result = actions.run_candidate(
                item,
                index,
                args,
                base_system,
                variables_doc,
                merit,
                glass_search_config,
                fallback_command,
            )
            results.append(result)
            _module_files.write_csv(layer_dir / "checkpoint_de_results.csv", results)
            _module_files.write_json(layer_dir / "checkpoint_de_results.json", results)
    return results


def select_handoff(args: Any, results: list[dict[str, Any]], actions: CheckpointActions):
    """Rank valid completed candidates and apply the existing strict downstream admission."""
    viable = [
        row
        for row in results
        if int(row.get("returncode", -1)) == 0
        and not bool(row.get("timed_out"))
        and str(row.get("score_contract") or "") == _module_policy.CHECKPOINT_SCORE_CONTRACT
        and bool(row.get("materialization_valid", False))
        and bool(row.get("trace_ok", False))
        and math.isfinite(_module_files.finite(row.get("S_final")))
        and math.isfinite(_module_files.finite(row.get("M_final")))
    ]
    viable.sort(
        key=lambda row: (
            -_module_files.finite(row.get("S_final"), -math.inf),
            _module_files.finite(row.get("M_final")),
        )
    )
    all_viable = [
        _module_handoff.material_row(row, rank)
        for rank, row in enumerate(viable, 1)
    ]
    handoff = actions.select_checkpoint_handoff_rows(
        all_viable,
        limit=max(0, int(getattr(args, "checkpoint_handoff_count", 0) or 0)),
        merit_count=max(
            0, int(getattr(args, "checkpoint_handoff_merit_count", 4))
        ),
        potential_count=max(
            0, int(getattr(args, "checkpoint_handoff_potential_count", 2))
        ),
        diversity_count=max(
            0, int(getattr(args, "checkpoint_handoff_diversity_count", 1))
        ),
        structure_distance_policy=str(
            getattr(
                args,
                "strict_structure_selection_policy",
                strict_topology.LEGACY_DISTANCE_POLICY,
            )
        ),
    )
    if not handoff:
        raise RuntimeError("checkpoint DE layer produced no material-funnel candidates")
    return CheckpointHandoff(all_viable, handoff)


def publish_handoff(args: Any, layer_dir: Path, selected: CheckpointHandoff):
    """Publish the final handoff, preview and completion counts without altering rows."""
    all_viable = selected.all_viable
    handoff = selected.handoff
    preview_count = max(1, int(args.checkpoint_selected_count))
    preview = handoff[:preview_count]
    _module_files.write_csv(layer_dir / "checkpoint_de_all_viable.csv", all_viable)
    _module_files.write_json(layer_dir / "checkpoint_de_all_viable.json", all_viable)
    _module_files.write_csv(layer_dir / "checkpoint_handoff_selection.csv", handoff)
    _module_files.write_json(layer_dir / "checkpoint_handoff_selection.json", handoff)
    _module_files.write_csv(layer_dir / "checkpoint_de_s400_preview.csv", preview)
    _module_files.write_json(layer_dir / "checkpoint_de_s400_preview.json", preview)
    _module_files.write_csv(layer_dir / "checkpoint_de_latest_ranked.csv", handoff)
    _module_files.write_json(layer_dir / "checkpoint_de_latest_ranked.json", handoff)
    config_path = layer_dir / "checkpoint_de_config.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    config["all_viable_count"] = len(all_viable)
    config["handoff_count"] = len(handoff)
    config["handoff_unique_topology_count"] = len(
        {_module_handoff.checkpoint_handoff_topology_identity(row) for row in handoff}
    )
    config["handoff_unique_curvature_sign_mask_count"] = len(
        {
            str(row.get("strict_curvature_sign_mask") or "")
            for row in handoff
            if str(row.get("strict_curvature_sign_mask") or "")
        }
    )
    requested_handoff = max(
        0, int(getattr(args, "checkpoint_handoff_count", 0) or 0)
    )
    config["handoff_topology_shortfall"] = (
        max(0, requested_handoff - len(handoff)) if requested_handoff > 0 else 0
    )
    _module_files.write_json(config_path, config)
    return handoff


def run_once(args: Any, *, actions: CheckpointActions) -> list[dict[str, Any]]:
    """Coordinate explicit input, selection, execution and handoff phases."""
    inputs = prepare_inputs(args)
    selection = select_starts(args, inputs, actions)
    layer_dir = server.native_io_paths.named_child(args.out_dir, 'layer1_checkpoint_de400')
    rank1_merits = rescore_anchors(args, inputs, selection, layer_dir, actions)
    plan = publish_start_manifests(args, inputs, selection, layer_dir, rank1_merits, actions)
    write_continuation_config(args, inputs, selection, plan, layer_dir)
    results = continue_candidates(args, inputs, plan, layer_dir, actions)
    selected = select_handoff(args, results, actions)
    return publish_handoff(args, layer_dir, selected)
