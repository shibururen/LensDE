"""Greedy distance admission and topology-layout diversification."""
from __future__ import annotations

from collections import defaultdict
from typing import Any, Iterable
from lensde.workflow import selection as strict_topology
from . import files as _module_files
from . import geometry as _module_geometry
from .policy import CHECKPOINT_TOPOLOGY_EFFECTIVE_DISTANCE_FLOOR, CHECKPOINT_TOPOLOGY_POWER_DISTANCE_FLOOR, CHECKPOINT_TOPOLOGY_REPLACEMENT_LIMIT


def greedy_select(
    candidates: list[dict[str, Any]],
    budget: int,
    references: list[dict[str, Any]],
    distance_index: DistanceIndex,
    selected: list[dict[str, Any]] | None = None,
    *,
    require_key_novelty: bool = False,
    minimum_effective_distance: float = 1.0,
    distance_admission: str = "strict_distance",
    reason: str,
    prefer_merit: bool = False,
) -> list[dict[str, Any]]:
    chosen = list(selected or [])
    chosen_ids = {str(row["state_id"]) for row in chosen}
    reference_ids = {str(row["state_id"]) for row in references}
    remaining = [
        row
        for row in candidates
        if str(row["state_id"]) not in chosen_ids | reference_ids
    ]
    while remaining and len(chosen) < budget:
        selection_references = [*references, *chosen]
        novelties = distance_index.minimum_many(remaining, selection_references)
        scored: list[tuple[float, dict[str, Any], dict[str, float], float]] = []
        for row, novelty in zip(remaining, novelties, strict=True):
            if novelty["effective"] + 1.0e-12 < minimum_effective_distance:
                continue
            # Structural admission has already used the downstream predicate.
            # Novelty only ranks candidates that passed that same hard gate.
            key_bonus = _module_geometry.key_novelty(row, selection_references)
            if require_key_novelty and key_bonus <= 0.0:
                continue
            geometry_score = min(1.0, novelty["effective"] / 4.0)
            score = (
                0.75 * _module_files.finite(row.get("potential_score"), 0.0)
                + 0.25 * geometry_score
                + 0.08 * key_bonus
            )
            scored.append((score, row, novelty, key_bonus))
        if not scored:
            break
        _, row, novelty, key_bonus = max(
            scored,
            key=lambda item: (
                -_module_files.finite(item[1].get("merit")) if prefer_merit else 0.0,
                item[0],
                -_module_files.finite(item[1].get("quality_ratio_to_source_rank1")),
                item[2]["effective"],
            ),
        )
        chosen_row = {
            **row,
            "selection_reason": reason,
            "selection_distance_admission": distance_admission,
            "selection_distance_multiplier": minimum_effective_distance,
            "selection_min_full_distance": novelty["full"],
            "selection_min_skeleton_distance": novelty["skeleton"],
            "selection_min_asphere_distance": novelty["asphere"],
            "selection_min_effective_distance": novelty["effective"],
            "selection_key_novelty": key_bonus,
        }
        if "admission" in novelty:
            chosen_row = strict_topology.annotate_admission(chosen_row, novelty["admission"])
            chosen_row["selection_structure_distance_policy"] = distance_index.distance_policy
            chosen_row["selection_legacy_effective_distance"] = novelty["legacy_effective"]
        chosen.append(chosen_row)
        chosen_id = str(row["state_id"])
        remaining = [
            item for item in remaining if str(item["state_id"]) != chosen_id
        ]
    return chosen


def diversify_checkpoint_topology_layouts(
    selected: list[dict[str, Any]],
    rank1_by_outer: dict[int, dict[str, Any]],
    candidates: list[dict[str, Any]],
    variables: list[dict[str, Any]],
    roles: list[str],
    *,
    replacement_limit: int = CHECKPOINT_TOPOLOGY_REPLACEMENT_LIMIT,
    power_distance_floor: float = CHECKPOINT_TOPOLOGY_POWER_DISTANCE_FLOOR,
    effective_distance_floor: float = CHECKPOINT_TOPOLOGY_EFFECTIVE_DISTANCE_FLOOR,
) -> list[dict[str, Any]]:
    """Legacy replacement used for historical replays, not current selection.

    The regular selector remains authoritative and fills the reservoir first.
    This conservative pass can remove only a repeated full ``topology_key``.
    Its replacement must expose a previously unrepresented ordered element
    shape layout, be sufficiently far in curvature/radius space from its own
    Outer rank-1 state, and retain the existing relaxed geometry admission.
    """
    diversified = [dict(row) for row in selected]
    limit = max(0, int(replacement_limit))
    if limit == 0 or not diversified or not candidates:
        return diversified

    rank1_rows = [rank1_by_outer[outer] for outer in sorted(rank1_by_outer)]
    ranked_candidates: dict[str, dict[str, Any]] = {}
    for row in candidates:
        state_id = str(row.get("state_id") or "")
        outer = int(row.get("outer") or 0)
        if not state_id or outer not in rank1_by_outer:
            continue
        current = ranked_candidates.get(state_id)
        if current is None or (
            _module_files.finite(row.get("potential_score"), 0.0),
            -_module_files.finite(row.get("quality_ratio_to_final_rank1")),
            row.get("record") != "basin_archive",
        ) > (
            _module_files.finite(current.get("potential_score"), 0.0),
            -_module_files.finite(current.get("quality_ratio_to_final_rank1")),
            current.get("record") != "basin_archive",
        ):
            ranked_candidates[state_id] = row
    candidate_rows = list(ranked_candidates.values())
    if not candidate_rows:
        return diversified

    distance_index = _module_geometry.DistanceIndex(
        [*rank1_rows, *diversified, *candidate_rows],
        variables,
        roles,
    )
    replacements = 0
    while replacements < limit:
        topology_counts: dict[tuple[int, str], int] = defaultdict(int)
        for row in diversified:
            topology_counts[
                (
                    int(row.get("outer") or 0),
                    str(row.get("topology_key") or ""),
                )
            ] += 1
        removable = [
            row
            for row in diversified
            if str(row.get("topology_key") or "")
            and topology_counts[
                (
                    int(row.get("outer") or 0),
                    str(row.get("topology_key") or ""),
                )
            ]
            > 1
        ]
        if not removable:
            break
        remove_row = max(
            removable,
            key=lambda row: (
                _module_files.finite(row.get("quality_ratio_to_final_rank1"), 0.0),
                -_module_files.finite(row.get("potential_score"), 0.0),
                int(row.get("global_selection_rank") or 0),
            ),
        )
        references = [
            *rank1_rows,
            *(row for row in diversified if row is not remove_row),
        ]
        remove_outer = int(remove_row.get("outer") or 0)
        represented_layouts = {
            _module_geometry.checkpoint_topology_shape_layout(row)
            for row in references
            if int(row.get("outer") or 0) == remove_outer
        }
        selected_states = {str(row.get("state_id") or "") for row in diversified}
        scored: list[
            tuple[float, dict[str, Any], dict[str, float], float]
        ] = []
        for candidate in candidate_rows:
            if str(candidate.get("state_id") or "") in selected_states:
                continue
            if int(candidate.get("outer") or 0) != remove_outer:
                continue
            if _module_geometry.checkpoint_topology_shape_layout(candidate) in represented_layouts:
                continue
            outer = int(candidate.get("outer") or 0)
            matched_rank1 = rank1_by_outer.get(outer)
            if matched_rank1 is None:
                continue
            rank1_power_distance = _module_geometry.checkpoint_power_parameter_distance(
                candidate,
                matched_rank1,
                variables,
            )
            if rank1_power_distance + 1.0e-12 < power_distance_floor:
                continue
            distance = distance_index.minimum(candidate, references)
            if distance["effective"] + 1.0e-12 < effective_distance_floor:
                continue
            geometry_score = min(1.0, distance["effective"] / 4.0)
            score = (
                0.75 * _module_files.finite(candidate.get("potential_score"), 0.0)
                + 0.25 * geometry_score
            )
            scored.append((score, candidate, distance, rank1_power_distance))
        if not scored:
            break
        _, replacement, distance, rank1_power_distance = max(
            scored,
            key=lambda item: (
                item[0],
                -_module_files.finite(item[1].get("quality_ratio_to_final_rank1")),
                item[3],
            ),
        )
        rank1_distance = distance_index.minimum(replacement, rank1_rows)
        replacement_row = {
            **replacement,
            "selection_reason": "topology_power_swap",
            "selection_distance_admission": (
                f"distance_relaxed_{int(round(effective_distance_floor * 100)):03d}"
            ),
            "selection_distance_multiplier": effective_distance_floor,
            "selection_min_full_distance": distance["full"],
            "selection_min_skeleton_distance": distance["skeleton"],
            "selection_min_asphere_distance": distance["asphere"],
            "selection_min_effective_distance": distance["effective"],
            "selection_key_novelty": 1.0,
            "selection_topology_shape_layout": _module_geometry.checkpoint_topology_shape_layout(
                replacement
            ),
            "selection_rank1_power_distance": rank1_power_distance,
            "selection_replaced_state_id": str(remove_row.get("state_id") or ""),
            "selection_replaced_topology_key": str(
                remove_row.get("topology_key") or ""
            ),
            "min_full_distance_to_any_rank1": rank1_distance["full"],
            "min_skeleton_distance_to_any_rank1": rank1_distance["skeleton"],
            "min_asphere_distance_to_any_rank1": rank1_distance["asphere"],
            "min_effective_distance_to_any_rank1": rank1_distance["effective"],
        }
        diversified[diversified.index(remove_row)] = replacement_row
        replacements += 1
    return diversified
