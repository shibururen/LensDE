"""Stage-boundary decisions only; never used in the optimizer hot path."""
from __future__ import annotations

import math
from pathlib import Path

CONTRACT = "vignetting_handoff_advisory_v1"
CHIEF_REFERENCE_CONTRACT = "vignetted_pupil_chief_v1"


def chief_ray_usable(
    score: dict,
    *,
    ok_key: str = "chief_ray_ok",
    failed_fields_key: str = "chief_ray_failed_fields",
    progress_key: str = "min_chief_progress",
) -> bool:
    """Consume the scorer's configured-pupil chief status, never a principal ray.

    Untagged historical scores remain readable as legacy data; they must be
    rescored before comparing their Merit with the new production contract.
    Do not infer principal validity from, or merge it into, these fields.
    """
    reference_contract = score.get("chief_reference_contract")
    if reference_contract not in (None, "", CHIEF_REFERENCE_CONTRACT):
        return False
    chief_status = score.get(ok_key)
    failed_fields = score.get(failed_fields_key)
    if chief_status not in (None, "") or failed_fields not in (None, ""):
        try:
            if isinstance(chief_status, str):
                chief_ok = chief_status.strip().lower() in {"1", "true", "yes", "y"}
            else:
                chief_ok = bool(chief_status)
            return chief_ok and int(float(failed_fields)) == 0
        except (TypeError, ValueError, OverflowError):
            return False
    try:
        progress = float(score.get(progress_key, 1.0))
    except (TypeError, ValueError):
        return False
    return math.isfinite(progress) and progress >= 1.0 - 1.0e-12


def score_usable(score: dict, *, min_trace: float = 0.0, min_field: float = 0.0) -> bool:
    """Use the existing score/trace contract, not a new ray-aiming test.

    In particular CP's interpolated rays have no per-ray convergence test.
    Neither aim_residual, aim_converged nor a failure-code count is inspected.
    The caller may retain its existing coverage floors; no floor is relaxed
    inside the optimizer, and no physical penalty is added here.
    """
    try:
        merit = float(score["merit"])
        coverage = float(score["trace_coverage"])
        field_coverage = float(score["min_field_coverage"])
    except (KeyError, TypeError, ValueError):
        return False
    return bool(
        score.get("trace_ok")
        and all(math.isfinite(value) for value in (merit, coverage, field_coverage))
        and coverage > 0.0 and coverage + 1.0e-12 >= min_trace
        and field_coverage > 0.0 and field_coverage + 1.0e-12 >= min_field
        and chief_ray_usable(score)
    )


def optical_measurement_failure(directory: Path, *, log_paths=()) -> bool:
    """Recognize solver-declared calibration failure, not crashes or I/O errors."""
    markers = (
        "automatic vignetting failed; the input system was left unchanged",
        "no traceable pupil samples were found in the pupil grid",
        "self-consistent pupil center did not converge",
    )
    for path in (directory / "stdout.log", directory / "stderr.log", *log_paths):
        if path.is_file():
            message = path.read_text(encoding="utf-8", errors="replace").lower()
            if any(marker in message for marker in markers):
                return True
    return False
