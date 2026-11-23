"""Checkpoint geometry matrices, distance indices and topology identities."""
from __future__ import annotations

import math
from collections import defaultdict
from typing import Any, Iterable
import numpy as np
from lensde.workflow import selection as strict_topology
from . import files as _module_files
from . import handoff as _module_handoff
from . import policy as _module_policy
from . import snapshots as _module_snapshots


def variable_bounds(variable: dict[str, Any]) -> tuple[float, float]:
    bounds = variable.get("bounds")
    if isinstance(bounds, list) and len(bounds) >= 2:
        return _module_files.finite(bounds[0], 0.0), _module_files.finite(bounds[1], 0.0)
    return _module_files.finite(variable.get("lower"), 0.0), _module_files.finite(variable.get("upper"), 0.0)


def normalized_geometry_matrix(
    rows: list[dict[str, Any]],
    variables: list[dict[str, Any]],
    roles: list[str],
    kinds: set[str],
) -> np.ndarray:
    indices = [
        index
        for index, variable in enumerate(variables)
        if str(variable.get("kind") or "").lower() in kinds
    ]
    if not indices:
        return np.zeros((len(rows), 0), dtype=np.float32)
    values = np.asarray([_module_files.parse_values(row) for row in rows], dtype=np.float64)
    output = np.empty((len(rows), len(indices)), dtype=np.float32)
    for column, index in enumerate(indices):
        variable = variables[index]
        kind = str(variable.get("kind") or "").lower()
        lower, upper = variable_bounds(variable)
        span = upper - lower
        transformed = values[:, index]
        if kind == "thickness" and index < len(roles) and roles[index] == "air":
            span = min(span, 25.0)
        if kind == "asphere_coeff":
            term = max(0, int(variable.get("term") or 0))
            order = 2 * (term + 1)
            radius = max(_module_files.finite(variable.get("asphere_reference_radius"), 1.0), 1.0e-12)
            radial_power = radius**order
            transformed = transformed * radial_power
            span = _module_files.finite(variable.get("edge_sag_upper"), upper * radial_power) - _module_files.finite(
                variable.get("edge_sag_lower"), lower * radial_power
            )
        if not math.isfinite(span) or span <= 1.0e-12:
            span = max(float(np.max(np.abs(transformed))), 1.0)
        output[:, column] = (transformed / span).astype(np.float32)
    return output


def rms_distance_matrix(left: np.ndarray, right: np.ndarray) -> np.ndarray:
    if left.shape[1] == 0:
        return np.zeros((left.shape[0], right.shape[0]), dtype=np.float32)
    squared = (
        np.sum(left * left, axis=1, dtype=np.float32)[:, None]
        + np.sum(right * right, axis=1, dtype=np.float32)[None, :]
        - 2.0 * (left @ right.T)
    )
    np.maximum(squared, 0.0, out=squared)
    squared /= float(left.shape[1])
    np.sqrt(squared, out=squared)
    return squared


class DistanceIndex:
    def __init__(
        self,
        rows: list[dict[str, Any]],
        variables: list[dict[str, Any]],
        roles: list[str],
    ) -> None:
        unique = {str(row["state_id"]): row for row in rows}
        self.rows = list(unique.values())
        self.index = {str(row["state_id"]): index for index, row in enumerate(self.rows)}
        skeleton = normalized_geometry_matrix(self.rows, variables, roles, _module_policy.SKELETON_KINDS)
        asphere = normalized_geometry_matrix(self.rows, variables, roles, _module_policy.ASPHERE_KINDS)
        full = np.concatenate([skeleton, asphere], axis=1)
        # Keep feature matrices only. A full candidate-by-candidate matrix is
        # quadratic in archive size even though selection only queries the few
        # rank-1/selected references. Distances are calculated in vectorized
        # candidate-to-reference batches below.
        self.skeleton = skeleton
        self.asphere = asphere
        self.full = full

    def minimum_many(
        self,
        candidates: list[dict[str, Any]],
        references: Iterable[dict[str, Any]],
    ) -> list[dict[str, float]]:
        reference_rows = list(references)
        if not candidates:
            return []
        if not reference_rows:
            return [
                {key: math.inf for key in ("full", "skeleton", "asphere", "effective")}
                for _ in candidates
            ]
        left = np.asarray(
            [self.index[str(row["state_id"])] for row in candidates], dtype=np.int64
        )
        right = np.asarray(
            [self.index[str(row["state_id"])] for row in reference_rows], dtype=np.int64
        )

        def minimum(features: np.ndarray) -> np.ndarray:
            return np.min(rms_distance_matrix(features[left], features[right]), axis=1)

        full = minimum(self.full)
        skeleton = minimum(self.skeleton)
        asphere = minimum(self.asphere)
        effective = np.maximum(
            full / _module_policy.FULL_DISTANCE_FLOOR,
            skeleton / _module_policy.SKELETON_DISTANCE_FLOOR,
        )
        if self.asphere.shape[1]:
            effective = np.maximum(effective, asphere / _module_policy.ASPHERE_DISTANCE_FLOOR)
        return [
            {
                "full": float(full[index]),
                "skeleton": float(skeleton[index]),
                "asphere": float(asphere[index]),
                "effective": float(effective[index]),
            }
            for index in range(len(candidates))
        ]

    def minimum(
        self,
        candidate: dict[str, Any],
        references: Iterable[dict[str, Any]],
    ) -> dict[str, float]:
        return self.minimum_many([candidate], references)[0]


class CheckpointStructureIndex(DistanceIndex):
    """Use the downstream admission function on physical snapshot parameters.

    Bound-normalized distances remain diagnostics only. They must not veto a
    candidate that passes the configured downstream structural policy.
    """

    def __init__(self, rows, variables, roles, *, distance_policy):
        super().__init__(rows, variables, roles)
        self.distance_policy = strict_topology.normalize_distance_policy(distance_policy)
        self.structural_rows = {
            str(row["state_id"]): {
                **row,
                "_strict_structure_descriptor": strict_topology.describe_values(
                    variables, _module_files.parse_values(row)
                ),
            }
            for row in self.rows
        }
        self._prefix_admissions = {}

    def _assess(self, candidate, references, reference_ids):
        # Greedy selection appends references. Cache only each candidate's last
        # prefix/result, not a quadratic candidate-to-candidate distance table.
        # For v4 the accepted result is the nearest pair; rejected prefixes
        # stay rejected. Other policies retain their complete shared evaluation.
        state = str(candidate["state_id"])
        cached = self._prefix_admissions.get(state)
        if self.distance_policy == strict_topology.LAYOUT_DISTANCE_POLICY and cached:
            previous_ids, previous = cached
            if reference_ids[:len(previous_ids)] == previous_ids:
                if not previous.accepted or reference_ids == previous_ids:
                    return previous
                extra = strict_topology.assess_candidate(
                    candidate, references[len(previous_ids):],
                    topology_identity=_module_handoff.checkpoint_handoff_topology_identity,
                    distance_policy=self.distance_policy,
                )
                result = (extra if not extra.accepted else min(
                    (previous, extra), key=lambda a: a.minimum_skeleton_distance
                ))
                self._prefix_admissions[state] = (reference_ids, result)
                return result
        result = strict_topology.assess_candidate(
            candidate, references, topology_identity=_module_handoff.checkpoint_handoff_topology_identity,
            distance_policy=self.distance_policy,
        )
        self._prefix_admissions[state] = (reference_ids, result)
        return result

    def minimum_many(self, candidates, references):
        references = list(references)
        diagnostics = super().minimum_many(candidates, references)
        reference_views = [self.structural_rows[str(row["state_id"])] for row in references]
        reference_ids = tuple(str(row["state_id"]) for row in references)
        for row, diagnostic in zip(candidates, diagnostics):
            admission = self._assess(
                self.structural_rows[str(row["state_id"])], reference_views, reference_ids,
            )
            diagnostic["legacy_effective"] = diagnostic["effective"]
            diagnostic["admission"] = admission
            scale = (1.0 if self.distance_policy == strict_topology.LAYOUT_DISTANCE_POLICY
                     else strict_topology.STRICT_SKELETON_DISTANCE_FLOOR)
            diagnostic["effective"] = (
                max(1.0, admission.minimum_skeleton_distance / scale)
                if admission.accepted else 0.0
            )
        return diagnostics


def asphere_keys(values: list[float], variables: list[dict[str, Any]]) -> tuple[str, str]:
    by_surface: dict[int, list[tuple[dict[str, Any], float]]] = defaultdict(list)
    for variable, value in zip(variables, values, strict=False):
        if str(variable.get("kind") or "").lower() in _module_policy.ASPHERE_KINDS:
            by_surface[int(variable.get("index") or 0)].append((variable, float(value)))
    if not by_surface:
        return "sphere", "sphere"

    def bucket(value: float) -> str:
        magnitude = abs(value)
        level = 0 if magnitude < 0.02 else 1 if magnitude < 0.10 else 2 if magnitude < 0.30 else 3
        return ("p" if value > 0.0 else "n" if value < 0.0 else "z") + str(level)

    layout_parts: list[str] = []
    shape_parts: list[str] = []
    fractions = (0.25, 0.50, 0.75, 1.00)
    for surface in sorted(by_surface):
        items = by_surface[surface]
        orders = sorted(
            2 * (max(0, int(variable.get("term") or 0)) + 1)
            for variable, _ in items
            if str(variable.get("kind") or "").lower() == "asphere_coeff"
        )
        has_conic = any(str(variable.get("kind") or "").lower() == "conic" for variable, _ in items)
        layout_parts.append(f"S{surface}:" + ("K" if has_conic else "") + ",".join(f"A{o}" for o in orders))
        departures = [0.0 for _ in fractions]
        conic_bins: list[str] = []
        for variable, value in items:
            kind = str(variable.get("kind") or "").lower()
            lower, upper = variable_bounds(variable)
            if kind == "conic":
                conic_bins.append(bucket(value / max(abs(lower), abs(upper), 1.0e-12)))
                continue
            term = max(0, int(variable.get("term") or 0))
            order = 2 * (term + 1)
            radius = max(_module_files.finite(variable.get("asphere_reference_radius"), 1.0), 1.0e-12)
            radial_power = radius**order
            q = value * radial_power
            q_bound = max(
                abs(_module_files.finite(variable.get("edge_sag_lower"), lower * radial_power)),
                abs(_module_files.finite(variable.get("edge_sag_upper"), upper * radial_power)),
                1.0e-12,
            )
            for index, fraction in enumerate(fractions):
                departures[index] += (q / q_bound) * fraction**order
        shape_parts.append(
            f"S{surface}:K{''.join(conic_bins) or 'z0'}:D"
            + ".".join(bucket(value) for value in departures)
        )
    return "|".join(layout_parts), "|".join(shape_parts)


def topology_statistics(
    rows: list[dict[str, Any]], rank1: dict[str, Any]
) -> dict[str, dict[str, float]]:
    by_key_generation: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        key = str(row.get("topology_key") or "")
        if key and _module_snapshots.valid_nonrank(row, rank1):
            by_key_generation[(key, int(row["snapshot_generation"]))].append(row)
    trajectories: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for (key, _), items in by_key_generation.items():
        trajectories[key].append(min(items, key=lambda item: _module_files.finite(item.get("merit"))))
    output: dict[str, dict[str, float]] = {}
    for key, items in trajectories.items():
        ordered = sorted(items, key=lambda item: int(item["snapshot_generation"]))
        start = _module_files.finite(ordered[0].get("merit"))
        end = _module_files.finite(ordered[-1].get("merit"))
        output[key] = {
            "snapshot_count": float(len(ordered)),
            "first_generation": float(ordered[0]["snapshot_generation"]),
            "last_generation": float(ordered[-1]["snapshot_generation"]),
            "improvement_ratio": start / end if end > 0.0 else math.inf,
        }
    return output


def hierarchy_keys(row: dict[str, Any]) -> tuple[str, ...]:
    return (
        str(row.get("topology_key") or ""),
        str(row.get("semantic_key") or ""),
        str(row.get("asphere_layout_key") or ""),
        str(row.get("asphere_shape_key") or ""),
        str(row.get("descriptor_key") or ""),
    )


def checkpoint_topology_identity(row: dict[str, Any]) -> str:
    """Return a structural identity for diagnostics and later-stage selection."""
    for key in ("topology_key", "semantic_key", "descriptor_key"):
        value = str(row.get(key) or "").strip()
        if value:
            return value
    return f"state:{row.get('state_id') or _module_handoff.checkpoint_handoff_identity(row)}"


def checkpoint_topology_shape_layout(row: dict[str, Any]) -> str:
    """Return the ordered element-shape portion of the existing topology key.

    This is a selector view of ``topology_key``, not another persisted key
    schema.  The full key retains element power and surface-strength buckets;
    the layout view ignores those boundary-sensitive suffixes when deciding
    whether a checkpoint adds an as-yet-unrepresented lens-group arrangement.
    """
    topology = str(row.get("topology_key") or "").strip()
    parts = topology.split("|")
    if (
        len(parts) < 3
        or not parts[0].startswith("e")
        or not parts[1].startswith("s")
    ):
        return topology or checkpoint_topology_identity(row)
    lens_part = next((part for part in parts[2:] if part.startswith("l")), "")
    tokens = [token for token in lens_part[1:].split(".") if token]
    if not tokens:
        return topology
    shape_prefixes = ("pl", "bv", "bc", "mp", "mn", "mw", "x")
    shapes: list[str] = []
    for token in tokens:
        shape = next(
            (prefix for prefix in shape_prefixes if token.startswith(prefix)),
            "",
        )
        if not shape:
            return topology
        shapes.append(shape)
    return f"{parts[0]}|{parts[1]}|shape={'.'.join(shapes)}"


def checkpoint_power_parameter_distance(
    left: dict[str, Any],
    right: dict[str, Any],
    variables: list[dict[str, Any]],
) -> float:
    """Compare only curvature/radius variables on physical local scales."""
    left_values = _module_files.parse_values(left)
    right_values = _module_files.parse_values(right)
    terms: list[float] = []
    for variable, left_value, right_value in zip(
        variables,
        left_values,
        right_values,
        strict=False,
    ):
        kind = str(variable.get("kind") or "").strip().lower()
        if kind not in {"curvature", "radius"}:
            continue
        absolute_floor = 0.02 if kind == "curvature" else 1.0
        scale = max(abs(left_value), abs(right_value), absolute_floor)
        terms.append(abs(left_value - right_value) / scale)
    if not terms:
        return math.inf
    strongest = sorted(terms, reverse=True)[:3]
    return math.sqrt(
        sum(value * value for value in strongest) / len(strongest)
    )


def key_novelty(row: dict[str, Any], selected: list[dict[str, Any]]) -> float:
    keys = hierarchy_keys(row)
    selected_keys = [hierarchy_keys(item) for item in selected]
    novel = [
        all(item[index] != key for item in selected_keys)
        for index, key in enumerate(keys)
        if key
    ]
    return sum(int(value) for value in novel) / len(novel) if novel else 0.0
