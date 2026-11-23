"""Checkpoint pool admission and stable rank-one/start selection."""
from __future__ import annotations

import math
from collections import defaultdict
from typing import Any, Iterable
from lensde.workflow import selection as strict_topology
from . import diversity as _module_diversity
from . import files as _module_files
from . import geometry as _module_geometry
from . import manifest as _module_manifest
from . import policy as _module_policy
from . import snapshots as _module_snapshots
from .policy import FINAL_RANK1_POTENTIAL_QUALITY_CAP, OUTER_SHORTLIST_LIMIT, QUALITY_CAP


def potential_score(row: dict[str, Any], final_generation: int) -> float:
    ratio = max(
        1.0,
        _module_files.finite(
            row.get("quality_ratio_to_final_rank1"),
            _module_policy.FINAL_RANK1_POTENTIAL_QUALITY_CAP,
        ),
    )
    quality = 1.0 - min(
        1.0,
        math.log(ratio) / math.log(_module_policy.FINAL_RANK1_POTENTIAL_QUALITY_CAP),
    )
    presence = min(1.0, math.log1p(int(row.get("exact_snapshot_count") or 0)) / math.log(6.0))
    persistence = min(1.0, _module_files.finite(row.get("topology_snapshot_count"), 0.0) / 5.0)
    improvement = _module_files.finite(row.get("topology_improvement_ratio"), 1.0)
    improvement_score = min(1.0, max(0.0, math.log(max(1.0, improvement))) / math.log(1.5))
    rank = max(2, int(row.get("merit_rank") or 10**6))
    rank_score = 1.0 / (1.0 + math.log1p(rank - 1))
    generation = int(row.get("snapshot_generation") or final_generation)
    early = 1.0 - min(1.0, generation / max(1, final_generation))
    return 0.50 * quality + 0.15 * presence + 0.15 * (0.5 * persistence + 0.5 * improvement_score) + 0.10 * rank_score + 0.10 * early


def build_outer_pool(
    rows: list[dict[str, Any]],
    rank1: dict[str, Any],
    variables: list[dict[str, Any]],
    quality_cap: float = QUALITY_CAP,
    allow_unproven_potential: bool = False,
    final_quality_cap: float = FINAL_RANK1_POTENTIAL_QUALITY_CAP,
    shortlist_limit: int | None = OUTER_SHORTLIST_LIMIT,
) -> list[dict[str, Any]]:
    final_generation = max(int(row["snapshot_generation"]) for row in rows)
    stats = _module_geometry.topology_statistics(rows, rank1)
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        if not _module_snapshots.valid_nonrank(row, rank1):
            continue
        ratio = _module_files.finite(row.get("merit")) / _module_files.finite(row.get("rank1_merit"))
        if ratio <= quality_cap:
            grouped[str(row["state_id"])].append(row)
    candidates: list[dict[str, Any]] = []
    for state_rows in grouped.values():
        representative = min(
            state_rows,
            key=lambda row: (
                _module_files.finite(row.get("merit")) / _module_files.finite(row.get("rank1_merit")),
                -int(row.get("snapshot_generation") or 0),
                0 if row.get("record") == "population" else 1,
            ),
        )
        candidate = dict(representative)
        source_rank1 = _module_files.finite(candidate.get("rank1_merit"), _module_files.finite(rank1.get("merit")))
        candidate["quality_ratio_to_source_rank1"] = _module_files.finite(candidate.get("merit")) / source_rank1
        candidate["quality_ratio_to_final_rank1"] = _module_files.finite(candidate.get("merit")) / _module_files.finite(rank1.get("merit"))
        candidate["exact_snapshot_count"] = len({int(row["snapshot_generation"]) for row in state_rows})
        topology = stats.get(str(candidate.get("topology_key") or ""), {})
        candidate["topology_snapshot_count"] = int(topology.get("snapshot_count", 0.0))
        candidate["topology_improvement_ratio"] = topology.get("improvement_ratio", 1.0)
        candidate["asphere_layout_key"], candidate["asphere_shape_key"] = _module_geometry.asphere_keys(
            _module_files.parse_values(candidate), variables
        )
        final_ratio = candidate["quality_ratio_to_final_rank1"]
        improvement_ratio = _module_files.finite(candidate.get("topology_improvement_ratio"), 1.0)
        candidate["source_core_quality"] = int(
            candidate["quality_ratio_to_source_rank1"] <= _module_policy.CORE_QUALITY_CAP
        )
        candidate["core_quality"] = int(final_ratio <= _module_policy.FINAL_RANK1_CORE_QUALITY_CAP)
        candidate["quality_band"] = (
            "core"
            if final_ratio <= _module_policy.FINAL_RANK1_CORE_QUALITY_CAP
            else (
                "improving_potential"
                if improvement_ratio >= _module_policy.FINAL_RANK1_POTENTIAL_MIN_IMPROVEMENT
                else "relaxed_merit_potential"
            )
        )
        candidate["potential_score"] = potential_score(candidate, final_generation)
        if final_ratio <= _module_policy.FINAL_RANK1_CORE_QUALITY_CAP or (
            final_ratio <= final_quality_cap
            and (
                improvement_ratio >= _module_policy.FINAL_RANK1_POTENTIAL_MIN_IMPROVEMENT
                or allow_unproven_potential
            )
        ):
            candidates.append(candidate)
    ordered = sorted(
        candidates,
        key=lambda row: (-_module_files.finite(row.get("potential_score"), 0.0), _module_files.finite(row.get("quality_ratio_to_source_rank1"))),
    )
    if shortlist_limit is None:
        return ordered
    return ordered[:max(0, int(shortlist_limit))]


def select_checkpoint_candidates(
    events_by_outer: dict[int, list[dict[str, Any]]],
    rank1_by_outer: dict[int, dict[str, Any]],
    variables: list[dict[str, Any]],
    roles: list[str],
    budget: int,
    basin_rows_by_outer: dict[int, list[dict[str, Any]]] | None = None,
    *,
    allow_merit_relaxation: bool = True,
    allow_basin_fill: bool = True,
    structure_distance_policy: str = strict_topology.LAYOUT_DISTANCE_POLICY,
) -> list[dict[str, Any]]:
    rank1_rows = [rank1_by_outer[outer] for outer in sorted(rank1_by_outer)]
    pools = {
        outer: build_outer_pool(events, rank1_by_outer[outer], variables)
        for outer, events in events_by_outer.items()
    }
    all_rows = [*rank1_rows, *(row for pool in pools.values() for row in pool)]
    distance_index = _module_geometry.CheckpointStructureIndex(
        all_rows, variables, roles, distance_policy=structure_distance_policy
    )
    independent_pools: dict[int, list[dict[str, Any]]] = {}
    for outer, pool in pools.items():
        kept: list[dict[str, Any]] = []
        for row in pool:
            distance = distance_index.minimum(row, rank1_rows)
            row.update(
                {
                    "min_full_distance_to_any_rank1": distance["full"],
                    "min_skeleton_distance_to_any_rank1": distance["skeleton"],
                    "min_asphere_distance_to_any_rank1": distance["asphere"],
                    "min_effective_distance_to_any_rank1": distance["effective"],
                }
            )
            if distance["effective"] >= 1.0:
                kept.append(row)
        independent_pools[outer] = kept

    preselected: list[dict[str, Any]] = []
    per_outer_prefilter_limit = max(_module_policy.OUTER_PREFILTER_LIMIT, max(0, budget))
    for outer in sorted(independent_pools):
        chosen = _module_diversity.greedy_select(
            independent_pools[outer],
            per_outer_prefilter_limit,
            rank1_rows,
            distance_index,
            reason="outer_prefilter",
        )
        preselected.extend({**row, "outer_selection_rank": rank} for rank, row in enumerate(chosen, 1))

    selected: list[dict[str, Any]] = []
    for outer in sorted(independent_pools):
        outer_rows = [row for row in preselected if int(row["outer"]) == outer]
        selected = _module_diversity.greedy_select(
            outer_rows,
            min(budget, len(selected) + _module_policy.GLOBAL_OUTER_FLOOR),
            rank1_rows,
            distance_index,
            selected,
            reason="global_outer_floor",
        )
    selected = _module_diversity.greedy_select(
        preselected,
        min(budget, len(selected) + _module_policy.GLOBAL_KEY_SEAT_LIMIT),
        rank1_rows,
        distance_index,
        selected,
        require_key_novelty=True,
        reason="global_key_coverage",
    )
    selected = _module_diversity.greedy_select(
        preselected,
        budget,
        rank1_rows,
        distance_index,
        selected,
        reason="global_potential_geometry_fill",
    )
    if len(selected) < budget and allow_basin_fill and basin_rows_by_outer:
        basin_candidates = [
            row
            for outer, rows in basin_rows_by_outer.items()
            for row in build_outer_pool(
                rows,
                rank1_by_outer[outer],
                variables,
                quality_cap=_module_policy.BASIN_FILL_QUALITY_CAP,
            )
        ]
        if basin_candidates:
            fill_index = _module_geometry.CheckpointStructureIndex(
                [*rank1_rows, *preselected, *selected, *basin_candidates],
                variables,
                roles,
                distance_policy=structure_distance_policy,
            )
            for row in basin_candidates:
                distance = fill_index.minimum(row, rank1_rows)
                row.update(
                    {
                        "min_full_distance_to_any_rank1": distance["full"],
                        "min_skeleton_distance_to_any_rank1": distance["skeleton"],
                        "min_asphere_distance_to_any_rank1": distance["asphere"],
                        "min_effective_distance_to_any_rank1": distance["effective"],
                    }
                )
            selected = _module_diversity.greedy_select(
                basin_candidates,
                budget,
                rank1_rows,
                fill_index,
                selected,
                require_key_novelty=False,
                reason="basin_archive_fill",
            )
    if len(selected) < budget and allow_merit_relaxation:
        relaxed_snapshot_candidates = [
            row
            for outer, events in events_by_outer.items()
            for row in build_outer_pool(
                events,
                rank1_by_outer[outer],
                variables,
                quality_cap=_module_policy.BASIN_FILL_QUALITY_CAP,
                allow_unproven_potential=True,
                final_quality_cap=_module_policy.BASIN_FILL_QUALITY_CAP,
                shortlist_limit=None,
            )
        ]
        relaxed_basin_candidates = (
            [
                row
                for outer, rows in (basin_rows_by_outer or {}).items()
                for row in build_outer_pool(
                    rows,
                    rank1_by_outer[outer],
                    variables,
                    quality_cap=_module_policy.BASIN_FILL_QUALITY_CAP,
                    allow_unproven_potential=True,
                    final_quality_cap=_module_policy.BASIN_FILL_QUALITY_CAP,
                    shortlist_limit=None,
                )
            ]
            if allow_basin_fill
            else []
        )
        relaxed_by_state: dict[str, dict[str, Any]] = {}
        for row in [*relaxed_snapshot_candidates, *relaxed_basin_candidates]:
            state = str(row["state_id"])
            current = relaxed_by_state.get(state)
            if current is None or (
                _module_files.finite(row.get("potential_score"), 0.0),
                -_module_files.finite(row.get("quality_ratio_to_final_rank1")),
                row.get("record") != "basin_archive",
            ) > (
                _module_files.finite(current.get("potential_score"), 0.0),
                -_module_files.finite(current.get("quality_ratio_to_final_rank1")),
                current.get("record") != "basin_archive",
            ):
                relaxed_by_state[state] = row
        relaxed_candidates = list(relaxed_by_state.values())
        if relaxed_candidates:
            relaxed_index = _module_geometry.CheckpointStructureIndex(
                [*rank1_rows, *selected, *relaxed_candidates],
                variables,
                roles,
                distance_policy=structure_distance_policy,
            )
            # Merit is relaxed while shared structural admission stays hard.
            # Snapshot rows retain priority over the basin archive.
            selected = _module_diversity.greedy_select(
                relaxed_snapshot_candidates,
                budget,
                rank1_rows,
                relaxed_index,
                selected,
                minimum_effective_distance=1.0,
                distance_admission="strict_distance",
                reason="relaxed_snapshot_merit_fill",
                prefer_merit=True,
            )
            if len(selected) < budget and relaxed_basin_candidates:
                selected = _module_diversity.greedy_select(
                    relaxed_basin_candidates,
                    budget,
                    rank1_rows,
                    relaxed_index,
                    selected,
                    minimum_effective_distance=1.0,
                    distance_admission="strict_distance",
                    reason="relaxed_basin_merit_fill",
                    prefer_merit=True,
                )
        # Broaden quality admission before sacrificing useful compute to
        # near-identical starts. Geometry remains a hard gate at every tier.
        for quality_cap in _module_policy.CHECKPOINT_MERIT_RELAXATION_CAPS[1:]:
            if len(selected) >= budget:
                break
            diverse_candidates = []
            for outer, events in events_by_outer.items():
                sources = [events]
                if allow_basin_fill:
                    sources.append((basin_rows_by_outer or {}).get(outer, []))
                for source_rows in sources:
                    if source_rows:
                        diverse_candidates.extend(build_outer_pool(
                            source_rows, rank1_by_outer[outer], variables,
                            quality_cap=quality_cap, final_quality_cap=quality_cap,
                            allow_unproven_potential=True, shortlist_limit=None,
                        ))
            diverse_index = _module_geometry.CheckpointStructureIndex(
                [*rank1_rows, *selected, *diverse_candidates], variables, roles,
                distance_policy=structure_distance_policy,
            )
            selected = _module_diversity.greedy_select(
                diverse_candidates, budget, rank1_rows, diverse_index, selected,
                minimum_effective_distance=1.0, distance_admission="strict_distance",
                reason=f"diverse_merit_fill_{quality_cap:g}", prefer_merit=True,
            )
    return [
        {
            **row,
            "selection_topology_shape_layout": _module_geometry.checkpoint_topology_shape_layout(row),
            "global_selection_rank": rank,
        }
        for rank, row in enumerate(selected, 1)
    ]


def select_rank1_anchor_outers(
    rank1_by_outer: dict[int, dict[str, Any]],
    limit: int,
) -> list[int]:
    """Choose bounded rank-1 controls independent of Outer count or topology."""
    ordered = sorted(
        rank1_by_outer.items(),
        key=lambda item: (_module_files.finite(item[1].get("merit")), int(item[0])),
    )
    selected: list[int] = []
    seen_states: set[str] = set()
    for outer, row in ordered:
        state = _module_manifest.checkpoint_state_identity(row)
        if state in seen_states:
            continue
        selected.append(outer)
        seen_states.add(state)
        if len(selected) >= limit:
            return sorted(selected)
    return sorted(selected)
