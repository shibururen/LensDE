"""Physical materialization, continuous-material and trace-admission contracts."""
from __future__ import annotations

import math
from typing import Any, Iterable
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from lensde.engine import core as server
from . import files as _module_files
from . import policy as _module_policy


def materialization_contract(
    score: dict[str, Any],
    merit: dict[str, Any],
    *,
    require_nonnegative_edge: bool,
    trace_coverage_epsilon: float = 0.0,
) -> tuple[bool, list[str]]:
    """Mirror the C++ static-materialization gate for workflow decisions."""
    reasons: list[str] = []
    score_merit = _module_files.finite(score.get("merit"))
    fail_merit = _module_files.finite(merit.get("fail_merit"), 1.0e12)
    trace_coverage = _module_files.finite(score.get("trace_coverage"), -math.inf)
    min_field_coverage = _module_files.finite(score.get("min_field_coverage"), -math.inf)
    min_system = _module_files.finite(merit.get("min_system_coverage"), 1.0)
    min_field = _module_files.finite(merit.get("min_field_coverage"), 1.0)
    coverage_epsilon = max(0.0, _module_files.finite(trace_coverage_epsilon, 0.0))
    if not bool(score.get("trace_ok", False)):
        reasons.append("trace_not_ok")
    if not math.isfinite(score_merit) or score_merit <= 0.0 or score_merit >= fail_merit:
        reasons.append("invalid_or_fail_merit")
    if trace_coverage + coverage_epsilon + _module_policy.MATERIALIZATION_TOLERANCE < min_system:
        reasons.append("trace_coverage_below_contract")
    if min_field_coverage + coverage_epsilon + _module_policy.MATERIALIZATION_TOLERANCE < min_field:
        reasons.append("field_coverage_below_contract")
    if bool(merit.get("require_chief_ray", False)):
        if not server.vignetting_handoff_policy.chief_ray_usable(score):
            reasons.append("chief_ray_below_contract")
    if bool(merit.get("require_off_axis_field_coverage", False)):
        if int(_module_files.finite(score.get("zero_coverage_off_axis_fields"), 1.0)) != 0:
            reasons.append("off_axis_field_below_contract")
    if require_nonnegative_edge:
        min_edge = _module_files.finite(score.get("min_edge"), -math.inf)
        if min_edge < -_module_policy.MATERIALIZATION_TOLERANCE:
            reasons.append("negative_or_missing_edge")
    return not reasons, reasons


def continuous_material_variable_contract(
    variables_doc: dict[str, Any] | list[dict[str, Any]],
) -> dict[str, Any]:
    """Summarize whether continuous n/Vd variables are active DE dimensions."""
    variables = (
        list(variables_doc.get("variables") or [])
        if isinstance(variables_doc, dict)
        else list(variables_doc or [])
    )
    material_variables = [
        variable
        for variable in variables
        if str(variable.get("kind") or "") in _module_policy.CONTINUOUS_MATERIAL_KINDS
    ]
    kind_counts = {
        kind: sum(str(variable.get("kind") or "") == kind for variable in material_variables)
        for kind in sorted(_module_policy.CONTINUOUS_MATERIAL_KINDS)
    }
    optimizable = 0
    surfaces_by_kind: dict[str, set[int]] = {
        kind: set() for kind in _module_policy.CONTINUOUS_MATERIAL_KINDS
    }
    variable_keys: list[str] = []
    for variable in material_variables:
        kind = str(variable.get("kind") or "")
        try:
            index = int(variable.get("index", -1))
        except (TypeError, ValueError):
            index = -1
        surfaces_by_kind[kind].add(index)
        variable_keys.append(
            f"{kind}:{index}:{int(variable.get('term', 0) or 0)}"
        )
        lower = _module_files.finite(variable.get("lower"))
        upper = _module_files.finite(variable.get("upper"))
        if math.isfinite(lower) and math.isfinite(upper) and upper > lower:
            optimizable += 1
    paired_surfaces = surfaces_by_kind["material_nd"] & surfaces_by_kind["material_vd"]
    all_material_surfaces = surfaces_by_kind["material_nd"] | surfaces_by_kind["material_vd"]
    return {
        "mode": "continuous",
        "material_variable_count": len(material_variables),
        "material_nd_count": kind_counts["material_nd"],
        "material_vd_count": kind_counts["material_vd"],
        "optimizable_material_variable_count": optimizable,
        "paired_material_surface_count": len(paired_surfaces),
        "unpaired_material_surface_indices": sorted(all_material_surfaces - paired_surfaces),
        "variable_keys": sorted(variable_keys),
        "optimization_enabled": bool(material_variables)
        and optimizable == len(material_variables)
        and not (all_material_surfaces - paired_surfaces),
    }


def assert_continuous_material_variables_preserved(
    source_doc: dict[str, Any] | list[dict[str, Any]],
    prepared_doc: dict[str, Any] | list[dict[str, Any]],
    *,
    stage: str,
) -> None:
    source = continuous_material_variable_contract(source_doc)
    prepared = continuous_material_variable_contract(prepared_doc)
    missing = sorted(set(source["variable_keys"]) - set(prepared["variable_keys"]))
    if missing:
        raise ValueError(
            f"{stage} removed continuous material DE variables: {', '.join(missing)}"
        )


def command_materialization_trace_epsilon(command: list[str]) -> float:
    """Mirror the near-trace materialization contract carried by a command."""
    adaptive = "--adaptive-allow-near-trace-infeasible" in command
    local = "--local-allow-near-trace-infeasible" in command
    if not adaptive and not local:
        return 0.0
    flag = "--trace-coverage-epsilon" if adaptive else "--local-trace-coverage-epsilon"
    fallback = _module_files.flag_value(command, "--trace-coverage-epsilon", "1e-9")
    return max(0.0, _module_files.finite(_module_files.flag_value(command, flag, fallback), 1.0e-9))


def checkpoint_adaptive_trace_contract(command: list[str]) -> tuple[bool, float]:
    """Return the adaptive trace admission contract inherited from an Outer run.

    Checkpoint DE must rank its initial anchor under the same coverage contract
    that admitted the source state.  ``base_command_prefix()`` intentionally
    keeps only evaluator flags, so this optimizer-specific contract has to be
    carried explicitly by both the single-process and multi-island routes.
    """
    allow_near = "--adaptive-allow-near-trace-infeasible" in command
    return allow_near, (
        command_materialization_trace_epsilon(command) if allow_near else 0.0
    )


def append_checkpoint_adaptive_trace_contract(
    command: list[str],
    source_command: list[str],
) -> None:
    allow_near, epsilon = checkpoint_adaptive_trace_contract(source_command)
    if not allow_near:
        return
    if "--adaptive-allow-near-trace-infeasible" not in command:
        command.append("--adaptive-allow-near-trace-infeasible")
    feedback_cycles.set_command_arg(
        command,
        "--trace-coverage-epsilon",
        f"{epsilon:.17g}",
    )
