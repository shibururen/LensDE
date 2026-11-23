from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

"""Shared strict structural-family checks for production handoff selectors.

The optical topology key remains a useful categorical identity, but it is not
stable enough to be the only duplicate gate: weak-power and nearly-planar
buckets can change after small continuous moves.  The legacy policy adds a
bound-normalized skeleton distance.  The topology-aware v2 policy keeps that
compatibility path isolated while using grouped, value-scaled structural
distances only for adjacent categorical topology keys.  The convergence-aware
v3 policy additionally collapses checkpoint families whose categorical
differences are confined to weak-power elements and whose continuous skeletons
are already close enough to share a likely downstream attraction basin.
The layout v4 policy compares curvature and spacing directly across categorical
boundaries. Aspheric corrections and optimizer bounds do not define new layouts.
"""

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable

from lensde.engine import core as server  # type: ignore


SKELETON_KINDS = {"curvature", "radius", "thickness"}
ASPHERE_KINDS = {"conic", "asphere_coeff"}
STRUCTURAL_KINDS = SKELETON_KINDS | ASPHERE_KINDS
CURVATURE_KINDS = {"curvature", "radius"}

# Keep this aligned with the production checkpoint skeleton-distance floor.
STRICT_SKELETON_DISTANCE_FLOOR = 0.05
CURVATURE_ZERO_ABSOLUTE = 1.0e-5
CURVATURE_ZERO_SPAN_FRACTION = 1.0e-4

LEGACY_DISTANCE_POLICY = "legacy_bound_rms"
GROUPED_DISTANCE_POLICY = "topology_aware_grouped_v2"
CONVERGENCE_DISTANCE_POLICY = "checkpoint_convergence_family_v3"
LAYOUT_DISTANCE_POLICY = "layout_skeleton_v4"
DISTANCE_POLICIES = (
    LEGACY_DISTANCE_POLICY,
    GROUPED_DISTANCE_POLICY,
    CONVERGENCE_DISTANCE_POLICY,
    LAYOUT_DISTANCE_POLICY,
)

# A layout must be close both in aggregate and at every individual surface.
# The peak cap prevents a local change from disappearing in a long lens train.
LAYOUT_RELATIVE_DISTANCE_FLOOR = 0.20
LAYOUT_PEAK_DISTANCE_FLOOR = 1.0

CONVERGENCE_SKELETON_DISTANCE_FLOOR = 0.08
CONVERGENCE_MAX_WEAK_SHAPE_DIFFERENCES = 2

# These are physical/local comparison floors, not optimizer or merit bounds.
# In particular, a deliberately non-binding 100 mm air-thickness limit must
# not make several-millimetre spacing changes look negligible.
GROUP_ABSOLUTE_SCALE_FLOORS = {
    "curvature": 0.02,
    "radius": 1.0,
    "thickness": 1.0,
    "conic": 1.0,
}
GROUP_TOP_TERM_COUNT = 3


@dataclass(frozen=True)
class StructuralDescriptor:
    schema: tuple[tuple[str, int, int], ...]
    values: tuple[float, ...]
    spans: tuple[float, ...]
    kinds: tuple[str, ...]
    curvature_sign_mask: str


@dataclass(frozen=True)
class StrictAdmission:
    accepted: bool
    reason: str
    minimum_skeleton_distance: float
    curvature_sign_mask: str
    distance_policy: str = LEGACY_DISTANCE_POLICY
    minimum_group_distances: tuple[tuple[str, float], ...] = ()
    minimum_convergence_distance: float = math.inf
    convergence_shape_difference: int = -1


def _finite(value: Any, default: float = 0.0) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return default
    return result if math.isfinite(result) else default


def _variable_bounds(variable: dict[str, Any]) -> tuple[float, float]:
    bounds = variable.get("bounds")
    if isinstance(bounds, list) and len(bounds) >= 2:
        lower = _finite(bounds[0])
        upper = _finite(bounds[1], lower)
    else:
        lower = _finite(variable.get("lower"), _finite(variable.get("initial")))
        upper = _finite(variable.get("upper"), lower)
    return (lower, upper) if lower <= upper else (upper, lower)


def _current_value(system: dict[str, Any], variable: dict[str, Any]) -> float:
    kind = str(variable.get("kind") or "").strip().lower()
    if kind == "radius":
        surfaces = list(system.get("surfaces") or [])
        index = int(_finite(variable.get("index"), -1.0))
        if 0 <= index < len(surfaces):
            return _finite(surfaces[index].get("radius"), _finite(variable.get("initial")))
    return _finite(
        server.variable_current_value_for_system(
            system,
            variable,
            _finite(variable.get("initial")),
        )
    )


def describe_system(
    system: dict[str, Any], variables_doc: dict[str, Any] | list[dict[str, Any]]
) -> StructuralDescriptor | None:
    variables = (
        list(variables_doc.get("variables") or [])
        if isinstance(variables_doc, dict)
        else list(variables_doc or [])
    )
    return describe_values(variables, [
        _current_value(system, v) if str(v.get("kind") or "").strip().lower() in STRUCTURAL_KINDS else 0.0
        for v in variables
    ])


def describe_values(
    variables_doc: dict[str, Any] | list[dict[str, Any]], values: list[float],
) -> StructuralDescriptor | None:
    """Describe physical checkpoint values with the same rules as a system."""
    variables = (
        list(variables_doc.get("variables") or [])
        if isinstance(variables_doc, dict)
        else list(variables_doc or [])
    )
    if len(values) != len(variables):
        raise ValueError("structural descriptor variable/value count mismatch")
    physical_values = values
    schema: list[tuple[str, int, int]] = []
    values: list[float] = []
    spans: list[float] = []
    kinds: list[str] = []
    signs: list[str] = []
    for variable, value in zip(variables, physical_values):
        kind = str(variable.get("kind") or "").strip().lower()
        if kind not in STRUCTURAL_KINDS:
            continue
        value = _finite(value)
        index = int(_finite(variable.get("index"), -1.0))
        term = int(_finite(variable.get("term"), 0.0))
        lower, upper = _variable_bounds(variable)
        span = upper - lower
        if not math.isfinite(span) or span <= 1.0e-12:
            span = max(abs(value), 1.0)
        schema.append((kind, index, term))
        values.append(value)
        spans.append(span)
        kinds.append(kind)
        if kind in CURVATURE_KINDS:
            epsilon = max(
                CURVATURE_ZERO_ABSOLUTE,
                CURVATURE_ZERO_SPAN_FRACTION * span,
            )
            signs.append("z" if abs(value) < epsilon else ("p" if value > 0.0 else "n"))
    if not schema:
        return None
    return StructuralDescriptor(
        schema=tuple(schema),
        values=tuple(values),
        spans=tuple(spans),
        kinds=tuple(kinds),
        curvature_sign_mask="".join(signs),
    )


def _read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def _find_artifact(starts: Iterable[Path], names: tuple[str, ...]) -> Path | None:
    for start in starts:
        current = start.resolve()
        for _ in range(6):
            for name in names:
                candidate = current / name
                if candidate.is_file():
                    return candidate
            if current.parent == current:
                break
            current = current.parent
    return None


def descriptor_from_row(row: dict[str, Any]) -> StructuralDescriptor | None:
    injected = row.get("_strict_structure_descriptor")
    if isinstance(injected, StructuralDescriptor):
        return injected
    starts: list[Path] = []
    for key in ("out_dir", "run_dir", "result_dir", "case_dir", "source_dir"):
        text = str(row.get(key) or "").strip()
        if text:
            starts.append(Path(text))
    if not starts:
        return None
    system_path = _find_artifact(
        starts,
        ("system.json", "checkpoint_system.json", "system_input.json", "input_system.json"),
    )
    variables_path = _find_artifact(starts, ("variables.json", "best_variables.json"))
    if system_path is None or variables_path is None:
        return None
    try:
        system = _read_json(system_path)
        variables_doc = _read_json(variables_path)
        if not isinstance(system, dict):
            return None
        return describe_system(system, variables_doc)
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        return None


def skeleton_distance(
    left: StructuralDescriptor | None,
    right: StructuralDescriptor | None,
) -> float:
    if left is None or right is None or left.schema != right.schema:
        return math.inf
    terms: list[float] = []
    for kind, left_value, right_value, left_span, right_span in zip(
        left.kinds,
        left.values,
        right.values,
        left.spans,
        right.spans,
        strict=True,
    ):
        if kind not in SKELETON_KINDS:
            continue
        span = max(left_span, right_span, 1.0e-12)
        delta = (left_value - right_value) / span
        terms.append(delta * delta)
    return math.sqrt(sum(terms) / len(terms)) if terms else math.inf


def normalize_distance_policy(value: Any) -> str:
    text = str(value or LEGACY_DISTANCE_POLICY).strip().lower()
    return text if text in DISTANCE_POLICIES else LEGACY_DISTANCE_POLICY


def _topology_components(key: str) -> dict[str, str] | None:
    parts: dict[str, str] = {}
    for token in str(key or "").split("|"):
        name, separator, value = token.partition("=")
        if separator:
            parts[name.strip().lower()] = value.strip()
    if "pwr" not in parts or "shape" not in parts:
        return None
    parts.pop("bend", None)
    return parts


def topology_keys_need_parameter_gate(left_key: str, right_key: str) -> bool:
    """Return whether v2 should use a continuous near-duplicate gate.

    A continuous power move can change a P/N/W bucket without changing the
    optical family.  That ambiguity is credible only while the remaining
    categorical structure (especially shape and gap pattern) is unchanged.
    Unknown/legacy key schemas retain the conservative parameter gate.
    """

    left = _topology_components(left_key)
    right = _topology_components(right_key)
    if left is None or right is None:
        return True
    left_power = left.pop("pwr", "")
    right_power = right.pop("pwr", "")
    return left == right and len(left_power) == len(right_power)


def weak_shape_convergence_family(
    left_key: str,
    right_key: str,
    *,
    max_shape_differences: int = CONVERGENCE_MAX_WEAK_SHAPE_DIFFERENCES,
) -> tuple[bool, int]:
    """Identify coarse-key variants likely to collapse under continuation.

    Shape buckets are least stable on weak-power elements.  Two candidates are
    treated as one predictive convergence family only when power and gap
    signatures agree exactly and every shape mismatch occurs at a ``W``
    element.  Strong-element shape changes and different gap layouts remain
    distinct regardless of numerical proximity.
    """

    left = _topology_components(left_key)
    right = _topology_components(right_key)
    if left is None or right is None:
        return False, -1
    left_power = left.get("pwr", "").upper()
    right_power = right.get("pwr", "").upper()
    if not left_power or left_power != right_power:
        return False, -1
    if left.get("gap", "") != right.get("gap", ""):
        return False, -1
    left_shape = left.get("shape", "").split(".")
    right_shape = right.get("shape", "").split(".")
    if (
        len(left_shape) != len(right_shape)
        or len(left_shape) != len(left_power)
    ):
        return False, -1
    differences = [
        index
        for index, (left_token, right_token) in enumerate(
            zip(left_shape, right_shape, strict=True)
        )
        if left_token != right_token
    ]
    limit = max(0, int(max_shape_differences))
    if not differences or len(differences) > limit:
        return False, len(differences)
    return all(left_power[index] == "W" for index in differences), len(differences)


def _distance_group(kind: str) -> str:
    if kind in CURVATURE_KINDS:
        return "power"
    if kind == "thickness":
        return "spacing"
    return "asphere"


def _local_parameter_scale(
    kind: str,
    left_value: float,
    right_value: float,
    left_span: float,
    right_span: float,
) -> float:
    magnitude = max(abs(left_value), abs(right_value))
    if kind == "asphere_coeff":
        # Coefficient units depend on radial order.  The local magnitude is
        # primary; a tiny fraction of the declared span is only a zero floor.
        coefficient_floor = max(left_span, right_span, 0.0) * 1.0e-6
        return max(magnitude, coefficient_floor, 1.0e-18)
    return max(
        magnitude,
        GROUP_ABSOLUTE_SCALE_FLOORS.get(kind, 1.0),
        1.0e-18,
    )


def grouped_structural_distance(
    left: StructuralDescriptor | None,
    right: StructuralDescriptor | None,
) -> tuple[float, dict[str, float]]:
    """Compare power, spacing and asphere groups without bound/RMS dilution."""

    if left is None or right is None or left.schema != right.schema:
        return math.inf, {}
    terms: dict[str, list[float]] = {
        "power": [],
        "spacing": [],
        "asphere": [],
    }
    for kind, left_value, right_value, left_span, right_span in zip(
        left.kinds,
        left.values,
        right.values,
        left.spans,
        right.spans,
        strict=True,
    ):
        scale = _local_parameter_scale(
            kind,
            left_value,
            right_value,
            left_span,
            right_span,
        )
        terms[_distance_group(kind)].append(abs(left_value - right_value) / scale)

    distances: dict[str, float] = {}
    for group, values in terms.items():
        if not values:
            continue
        strongest = sorted(values, reverse=True)[:GROUP_TOP_TERM_COUNT]
        distances[group] = math.sqrt(
            sum(value * value for value in strongest) / len(strongest)
        )
    return (max(distances.values()), distances) if distances else (math.inf, {})


def assess_candidate(
    candidate: dict[str, Any],
    selected: list[dict[str, Any]],
    *,
    topology_identity: Callable[[dict[str, Any]], str],
    distance_floor: float = STRICT_SKELETON_DISTANCE_FLOOR,
    distance_policy: str = LEGACY_DISTANCE_POLICY,
) -> StrictAdmission:
    policy = normalize_distance_policy(distance_policy)
    descriptor = descriptor_from_row(candidate)
    candidate_key = topology_identity(candidate)
    minimum_distance = math.inf
    minimum_groups: dict[str, float] = {}
    minimum_convergence_distance = math.inf
    minimum_convergence_shape_difference = -1
    for existing in selected:
        existing_key = topology_identity(existing)
        if candidate_key == existing_key:
            return StrictAdmission(
                accepted=False,
                reason="duplicate_topology_key",
                minimum_skeleton_distance=0.0,
                curvature_sign_mask=(
                    descriptor.curvature_sign_mask if descriptor is not None else ""
                ),
                distance_policy=policy,
            )
        existing_descriptor = descriptor_from_row(existing)
        if policy == LAYOUT_DISTANCE_POLICY:
            distance, groups = layout_skeleton_distance(descriptor, existing_descriptor)
            if distance < minimum_distance:
                minimum_distance, minimum_groups = distance, groups
            if distance + 1.0e-12 < 1.0:
                return StrictAdmission(
                    accepted=False,
                    reason="duplicate_layout_skeleton",
                    minimum_skeleton_distance=distance,
                    curvature_sign_mask=descriptor.curvature_sign_mask,
                    distance_policy=policy,
                    minimum_group_distances=tuple(sorted(groups.items())),
                )
            continue
        if policy in {GROUPED_DISTANCE_POLICY, CONVERGENCE_DISTANCE_POLICY}:
            distance, groups = grouped_structural_distance(
                descriptor, existing_descriptor
            )
            use_parameter_gate = topology_keys_need_parameter_gate(
                candidate_key, existing_key
            )
        else:
            distance = skeleton_distance(descriptor, existing_descriptor)
            groups = {}
            use_parameter_gate = True
        if policy == CONVERGENCE_DISTANCE_POLICY:
            same_convergence_family, shape_difference = (
                weak_shape_convergence_family(candidate_key, existing_key)
            )
            convergence_distance = skeleton_distance(
                descriptor, existing_descriptor
            )
            if (
                same_convergence_family
                and convergence_distance < minimum_convergence_distance
            ):
                minimum_convergence_distance = convergence_distance
                minimum_convergence_shape_difference = shape_difference
            if (
                same_convergence_family
                and convergence_distance + 1.0e-12
                < CONVERGENCE_SKELETON_DISTANCE_FLOOR
            ):
                return StrictAdmission(
                    accepted=False,
                    reason="duplicate_predicted_convergence_family",
                    minimum_skeleton_distance=convergence_distance,
                    curvature_sign_mask=descriptor.curvature_sign_mask,
                    distance_policy=policy,
                    minimum_group_distances=tuple(sorted(groups.items())),
                    minimum_convergence_distance=convergence_distance,
                    convergence_shape_difference=shape_difference,
                )
        if distance < minimum_distance:
            minimum_distance = distance
            minimum_groups = groups
        if (
            use_parameter_gate
            and distance + 1.0e-12 < max(0.0, distance_floor)
        ):
            return StrictAdmission(
                accepted=False,
                reason=(
                    "duplicate_adjacent_topology_parameter_near"
                    if policy in {
                        GROUPED_DISTANCE_POLICY,
                        CONVERGENCE_DISTANCE_POLICY,
                    }
                    else "duplicate_structural_parameter_near"
                ),
                minimum_skeleton_distance=distance,
                curvature_sign_mask=descriptor.curvature_sign_mask,
                distance_policy=policy,
                minimum_group_distances=tuple(sorted(groups.items())),
                minimum_convergence_distance=minimum_convergence_distance,
                convergence_shape_difference=minimum_convergence_shape_difference,
            )
    return StrictAdmission(
        accepted=True,
        reason="strict_topology_parameter_admitted",
        minimum_skeleton_distance=minimum_distance,
        curvature_sign_mask=(descriptor.curvature_sign_mask if descriptor is not None else ""),
        distance_policy=policy,
        minimum_group_distances=tuple(sorted(minimum_groups.items())),
        minimum_convergence_distance=minimum_convergence_distance,
        convergence_shape_difference=minimum_convergence_shape_difference,
    )


def annotate_admission(
    row: dict[str, Any], admission: StrictAdmission
) -> dict[str, Any]:
    annotated = {
        **row,
        "strict_topology_admission": admission.reason,
        "strict_curvature_sign_mask": admission.curvature_sign_mask,
        "strict_structure_distance_policy": admission.distance_policy,
        "strict_min_skeleton_parameter_distance": (
            admission.minimum_skeleton_distance
            if math.isfinite(admission.minimum_skeleton_distance)
            else None
        ),
        "strict_min_convergence_family_distance": (
            admission.minimum_convergence_distance
            if math.isfinite(admission.minimum_convergence_distance)
            else None
        ),
        "strict_convergence_shape_difference": (
            admission.convergence_shape_difference
            if admission.convergence_shape_difference >= 0
            else None
        ),
    }
    for group in ("power", "spacing", "asphere", "power_peak", "spacing_peak"):
        value = dict(admission.minimum_group_distances).get(group)
        annotated[f"strict_min_{group}_parameter_distance"] = (
            value if value is not None and math.isfinite(value) else None
        )
    return annotated


def layout_skeleton_distance(
    left: StructuralDescriptor | None,
    right: StructuralDescriptor | None,
) -> tuple[float, dict[str, float]]:
    """Value-scaled layout distance; <1 means close under both physical gates.

    Compare aligned geometry only, so different asphere terms do not disable
    the check. Radius variables are converted to curvature. Squared physical
    scales normalize the group energy independently of search bounds, while
    a per-surface cap prevents isolated strong changes from being averaged out.
    """
    def geometry(descriptor):
        if descriptor is None:
            return {}
        values = {}
        for (kind, index, _term), value in zip(descriptor.schema, descriptor.values):
            if kind not in SKELETON_KINDS:
                continue
            if kind == "radius":
                value = 1.0 / value if abs(value) > 1.0e-12 else 0.0
                kind = "curvature"
            values[(kind, index)] = value
        return values

    a, b = geometry(left), geometry(right)
    if not a or a.keys() != b.keys():
        return math.inf, {}
    groups = {}
    for kind, name in (("curvature", "power"), ("thickness", "spacing")):
        pairs = [(value, b[key]) for key, value in a.items() if key[0] == kind]
        if not pairs:
            continue
        floor = GROUP_ABSOLUTE_SCALE_FLOORS[kind]
        scales = [max(abs(x), abs(y), floor) for x, y in pairs]
        groups[name] = math.sqrt(
            sum((x-y)**2 for x, y in pairs) / sum(scale**2 for scale in scales)
        )
        groups[name + "_peak"] = max(
            abs(x-y) / scale for (x, y), scale in zip(pairs, scales)
        )
    distance = max(
        value / (LAYOUT_PEAK_DISTANCE_FLOOR if name.endswith("_peak")
                 else LAYOUT_RELATIVE_DISTANCE_FLOOR)
        for name, value in groups.items()
    )
    return distance, groups
