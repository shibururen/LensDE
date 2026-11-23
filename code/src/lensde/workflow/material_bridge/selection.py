"""Material entry quotas and strict ordered structural admission."""
from __future__ import annotations

import argparse
import math
from typing import Any
from lensde.workflow import selection as strict_topology
from . import files as _module_files
from . import ranking as _module_ranking


def lane_quotas(args: argparse.Namespace, limit: int) -> tuple[int, int, int]:
    stable = int(getattr(args, "material_bridge_stable_count", -1))
    repair = int(getattr(args, "material_bridge_repair_count", -1))
    diversity = int(getattr(args, "material_bridge_diversity_count", -1))
    if stable < 0:
        stable = int(math.ceil(limit * 0.5))
    if repair < 0:
        repair = max(1, int(round(limit / 3.0))) if limit >= 3 else 0
    if diversity < 0:
        diversity = max(0, limit - stable - repair)
    return max(0, stable), max(0, repair), max(0, diversity)


def legacy_lane_override_enabled(args: argparse.Namespace) -> bool:
    return any(
        int(getattr(args, name, -1)) >= 0
        for name in (
            "material_bridge_stable_count",
            "material_bridge_repair_count",
            "material_bridge_diversity_count",
        )
    )


def pure_merit_entry_enabled(args: argparse.Namespace, limit: int) -> bool:
    return (
        limit > 0
        and str(getattr(args, "material_entry_selection_mode", "hybrid") or "hybrid")
        .strip()
        .lower()
        == "merit"
        and int(getattr(args, "material_entry_merit_count", 0)) >= limit
        and int(getattr(args, "material_entry_potential_count", 0)) == 0
        and int(getattr(args, "material_entry_diversity_count", 0)) == 0
    )


def add_lane(
    selected: list[dict[str, Any]],
    seen_ranks: set[int],
    seen_topologies: set[str],
    candidates: list[dict[str, Any]],
    count: int,
    bucket: str,
    *,
    prefer_new_topology: bool = False,
    structure_distance_policy: str = strict_topology.LEGACY_DISTANCE_POLICY,
) -> None:
    if count <= 0:
        return
    ordered = list(candidates)
    if prefer_new_topology:
        original_position = {id(row): position for position, row in enumerate(ordered)}
        ordered.sort(
            key=lambda row: (
                _module_ranking.material_topology_family_key(row) in seen_topologies,
                original_position[id(row)],
            )
        )
    added = 0
    for row in ordered:
        rank = int(_module_files.finite_float(row.get("basin_rank"), -1))
        key = _module_ranking.material_topology_family_key(row)
        if rank < 0 or rank in seen_ranks:
            continue
        admission = strict_topology.assess_candidate(
            row,
            selected,
            topology_identity=_module_ranking.material_topology_family_key,
            distance_policy=structure_distance_policy,
        )
        if not admission.accepted:
            continue
        item = dict(row)
        item["material_entry_bucket"] = bucket
        item["material_topology_family_key"] = key
        item = strict_topology.annotate_admission(item, admission)
        selected.append(item)
        seen_ranks.add(rank)
        seen_topologies.add(key)
        added += 1
        if added >= count:
            break


def select_lanes(args: argparse.Namespace, rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    limit = int(getattr(args, "material_entry_count", 0))
    structure_distance_policy = str(
        getattr(
            args,
            "strict_structure_selection_policy",
            strict_topology.LEGACY_DISTANCE_POLICY,
        )
    )

    def add(*lane_args: Any, **lane_kwargs: Any) -> None:
        add_lane(
            *lane_args,
            structure_distance_policy=structure_distance_policy,
            **lane_kwargs,
        )

    forced_ranks = set(_module_files.parse_int_list(getattr(args, "selection_ranks", ""))) | set(
        _module_files.parse_int_list(getattr(args, "material_entry_ranks", ""))
    )
    viable = [
        row
        for row in rows
        if bool(row.get("final_materialization_valid", False))
        and (
            row.get("bridge_classification") == "stable_common"
            or bool(row.get("repair_success", False))
            or bool(row.get("bridge_deepen_accepted", False))
        )
    ]
    if limit <= 0:
        selected: list[dict[str, Any]] = []
        seen_ranks: set[int] = set()
        seen_topologies: set[str] = set()
        add(
            selected,
            seen_ranks,
            seen_topologies,
            sorted(viable, key=_module_ranking.effective_key),
            len(viable),
            "all_viable",
        )
        return selected

    selected: list[dict[str, Any]] = []
    seen_ranks: set[int] = set()
    seen_topologies: set[str] = set()
    forced = [
        row
        for row in viable
        if int(_module_files.finite_float(row.get("basin_rank"), -1)) in forced_ranks
    ]
    add(
        selected,
        seen_ranks,
        seen_topologies,
        sorted(forced, key=_module_ranking.effective_key),
        len(forced),
        "forced_rank",
    )

    if legacy_lane_override_enabled(args):
        stable_count, repair_count, diversity_count = lane_quotas(args, limit)
        stable_rows = sorted(
            [
                row
                for row in viable
                if row.get("bridge_classification") == "stable_common"
            ],
            key=_module_ranking.effective_key,
        )
        repair_rows = sorted(
            [row for row in viable if bool(row.get("repair_success", False))],
            key=lambda row: (
                not bool(row.get("repair_strict_success", False)),
                _module_ranking.effective_key(row),
                -_module_files.finite_float(row.get("repair_gain_per_second"), -math.inf),
            ),
        )
        remaining = max(0, limit - len(selected))
        add(
            selected,
            seen_ranks,
            seen_topologies,
            stable_rows,
            min(stable_count, remaining),
            "stable_common_override",
        )
        remaining = max(0, limit - len(selected))
        add(
            selected,
            seen_ranks,
            seen_topologies,
            repair_rows,
            min(repair_count, remaining),
            "aperture_repair_override",
        )
        remaining = max(0, limit - len(selected))
        add(
            selected,
            seen_ranks,
            seen_topologies,
            sorted(viable, key=_module_ranking.effective_key),
            min(diversity_count, remaining),
            "diversity_override",
            prefer_new_topology=True,
        )
        remaining = max(0, limit - len(selected))
        add(
            selected,
            seen_ranks,
            seen_topologies,
            sorted(viable, key=_module_ranking.effective_key),
            remaining,
            "override_fill",
        )
        return selected[: max(limit, len(forced))]

    merit_count = max(
        0,
        int(getattr(args, "material_entry_merit_count", int(math.ceil(limit / 2.0)))),
    )
    potential_count = max(
        0,
        int(getattr(args, "material_entry_potential_count", limit - merit_count)),
    )
    diversity_count = max(
        0,
        int(getattr(args, "material_entry_diversity_count", 0)),
    )
    quality_guard_ratio = max(
        1.0,
        _module_files.finite_float(getattr(args, "material_entry_quality_guard_ratio", 1.10), 1.10),
    )
    by_merit = sorted(viable, key=lambda row: _module_ranking.bridge_selection_key(row, "merit"))
    by_potential = sorted(
        _module_ranking.potential_lane_rows(viable), key=lambda row: _module_ranking.bridge_selection_key(row, "potential")
    )
    selection_mode = str(
        getattr(args, "material_entry_selection_mode", "hybrid") or "hybrid"
    )
    by_mode = sorted(
        viable, key=lambda row: _module_ranking.bridge_selection_key(row, selection_mode)
    )
    if pure_merit_entry_enabled(args, limit):
        remaining = max(0, limit - len(selected))
        add(
            selected,
            seen_ranks,
            seen_topologies,
            by_merit,
            remaining,
            "merit",
        )
        # Merit changes ordering, never the structural admission requirement.
        # A shortage is reported by the caller rather than filled with copies
        # of a layout that the strict pass already rejected.
        return selected[: max(limit, len(forced))]

    quality_start = len(selected)
    best_merit = _module_ranking.selection_reference_merit(by_merit[0]) if by_merit else math.inf
    guarded = [
        row
        for row in by_merit
        if _module_ranking.selection_reference_merit(row) <= quality_guard_ratio * best_merit
    ]
    remaining = max(0, limit - len(selected))
    add(
        selected,
        seen_ranks,
        seen_topologies,
        guarded,
        min(len(guarded), remaining),
        "quality_guard",
    )
    quality_used = len(selected) - quality_start
    remaining = max(0, limit - len(selected))
    add(
        selected,
        seen_ranks,
        seen_topologies,
        by_merit,
        min(max(0, merit_count - quality_used), remaining),
        "merit",
    )
    remaining = max(0, limit - len(selected))
    add(
        selected,
        seen_ranks,
        seen_topologies,
        by_potential,
        min(potential_count, remaining),
        "potential",
    )
    remaining = max(0, limit - len(selected))
    add(
        selected,
        seen_ranks,
        seen_topologies,
        _module_ranking.quality_potential_pareto_order(viable),
        min(diversity_count, remaining),
        "diversity_pareto",
        prefer_new_topology=True,
    )
    remaining = max(0, limit - len(selected))
    add(
        selected,
        seen_ranks,
        seen_topologies,
        by_mode,
        remaining,
        "priority_fill",
    )
    # A topology shortage is reported by the caller; it is never repaired by
    # launching another expensive arm from a duplicate geometry family.
    return selected[: max(limit, len(forced))]
