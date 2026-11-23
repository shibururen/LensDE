"""Fixed-aperture APG calibration and strict APG/CP bridge realization."""
from __future__ import annotations

import argparse
import copy
import shutil
from lensde.runtime import processes as subprocess
import sys
import time
from pathlib import Path
from typing import Any
from lensde.engine import core as server
from . import cache as _module_cache
from . import contracts as _module_contracts
from . import files as _module_files
from . import paths as _module_paths
from . import policy as _module_policy


def prepare_aperture_realized_bridge_source(
    *,
    source: Path,
    bridge_root: Path,
    merit: dict[str, Any],
    nodes: tuple[float, ...],
    args: argparse.Namespace,
) -> tuple[Path, dict[str, Any]]:
    """Calibrate target fields and validate APG/CP retraces on one pupil."""

    merit = server.dynamic_aperture_contract.apply_merit(copy.deepcopy(merit))
    source_system_path = _module_files.source_file(source, "system.json")
    bridge_root = server.native_io_paths.python_path(bridge_root)
    source_variables_path = _module_files.source_file(source, "variables.json")
    source_system = _module_files.read_json(source_system_path)
    minimum = _module_contracts.minimum_relative_illumination(merit)
    if minimum <= 0.0:
        return source, {
            "contract": _module_policy.MATERIAL_BRIDGE_VIGNETTING_CONTRACT,
            "performed": False,
            "cache_hit": False,
            "reason": "vignetting_disabled",
            "source": str(source.resolve()),
            "target_nodes": list(nodes),
        }

    bridge_root.mkdir(parents=True, exist_ok=True)
    merit_path = bridge_root / "bridge_merit.json"
    _module_files.write_json(merit_path, merit)
    cpu_exe = _module_paths.CPP_DIR / "build" / "lensde_cpu.exe"
    if not cpu_exe.is_file():
        raise FileNotFoundError(f"vignetting bridge CPU executable not found: {cpu_exe}")
    # This branch is active only for a vignetted system. Its incoming physical
    # apertures are already established, so the field-grid bridge must not apply
    # the clear-aperture margin a second time.
    aperture_margin = 1.0

    def make_command(experiment_path: Path, aiming_policy: str) -> list[str]:
        source_aperture_tolerance = _module_files.finite_float(
            getattr(
                args,
                "stage_boundary_source_aperture_relative_tolerance",
                0.05,
            ),
            0.05,
        )
        mode_aperture_tolerance = _module_files.finite_float(
            getattr(
                args,
                "stage_boundary_mode_aperture_relative_tolerance",
                0.03,
            ),
            0.03,
        )
        coverage_tolerance = _module_files.finite_float(
            getattr(
                args,
                "stage_boundary_coverage_absolute_tolerance",
                0.01,
            ),
            0.01,
        )
        return [
            sys.executable,
            "-m", 'lensde.helpers.run_vignetting_aperture_realized_bridge_experiment',
            "--source-system", str(source_system_path),
            "--source-merit", str(merit_path),
            "--source-variables", str(source_variables_path),
            "--executable", str(cpu_exe),
            "--output-dir", str(experiment_path),
            "--field-nodes", ",".join(f"{node:.17g}" for node in nodes),
            "--minimum-relative-illumination", f"{minimum:.17g}",
            "--aperture-margin", f"{aperture_margin:.17g}",
            "--source-aperture-relative-tolerance",
            f"{source_aperture_tolerance:.17g}",
            "--mode-aperture-relative-tolerance",
            f"{mode_aperture_tolerance:.17g}",
            "--coverage-absolute-tolerance",
            f"{coverage_tolerance:.17g}",
            "--minimum-trace-coverage",
            f"{_module_contracts.minimum_trace_coverage(args):.17g}",
            "--minimum-field-coverage",
            f"{_module_contracts.minimum_trace_coverage(args):.17g}",
            "--set-vignetting-aiming", aiming_policy,
        ]

    experiment_dir = server.native_io_paths.named_child(bridge_root, 'experiment')
    command = make_command(experiment_dir, "gui_default")
    command_path = bridge_root / "command.json"
    cached_command = _module_files.read_json(command_path) if command_path.is_file() else None
    binding_path = bridge_root / "completed_input_binding.json"
    try:
        cached_binding = _module_files.read_json(binding_path) if binding_path.is_file() else None
    except (OSError, ValueError):
        cached_binding = None
    input_binding = _module_cache.vignetting_bridge_input_binding(command)
    _module_files.write_json(bridge_root / "command.json", command)
    started = time.perf_counter()
    attempts: list[dict[str, Any]] = []

    def run_attempt(
        experiment_path: Path,
        aiming_policy: str,
        log_suffix: str,
    ) -> tuple[subprocess.CompletedProcess[str], Path, dict[str, Any]]:
        attempt_command = make_command(experiment_path, aiming_policy)
        attempt_started = time.perf_counter()
        result = subprocess.run(
            attempt_command,
            cwd=_module_paths.CPP_DIR,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=(
                int(getattr(args, "material_bridge_timeout", 0))
                if int(getattr(args, "material_bridge_timeout", 0)) > 0
                else None
            ),
        )
        (bridge_root / f"stdout{log_suffix}.log").write_text(
            result.stdout or "", encoding="utf-8", errors="replace"
        )
        (bridge_root / f"stderr{log_suffix}.log").write_text(
            result.stderr or "", encoding="utf-8", errors="replace"
        )
        attempt_comparison_path = experiment_path / "bridge_comparison.json"
        attempt_comparison = (
            _module_files.read_json(attempt_comparison_path)
            if attempt_comparison_path.is_file()
            else {}
        )
        attempts.append({
            "aiming_policy": aiming_policy,
            "command": attempt_command,
            "experiment_dir": str(experiment_path.resolve()),
            "returncode": result.returncode,
            "passed": bool(attempt_comparison.get("passed", False)),
            "missing_pupil_samples": _module_contracts.vignetting_bridge_missing_pupil_samples(
                experiment_path
            ),
            "elapsed_seconds": time.perf_counter() - attempt_started,
        })
        return result, attempt_comparison_path, attempt_comparison

    comparison_path = experiment_dir / "bridge_comparison.json"
    comparison = _module_files.read_json(comparison_path) if comparison_path.is_file() else {}
    realized_path = experiment_dir / "aperture_realized_y_only_system.json"
    cache_hit = _module_cache.cached_vignetting_bridge_is_usable(
        command=command,
        cached_command=cached_command,
        comparison=comparison,
        nodes=nodes,
        minimum=minimum,
        aperture_margin=aperture_margin,
        realized_path=realized_path,
        input_binding=input_binding,
        cached_input_binding=cached_binding,
    )
    if cache_hit:
        completed_returncode = 0
        attempts.append({
            "aiming_policy": "gui_default",
            "command": command,
            "experiment_dir": str(experiment_dir.resolve()),
            "returncode": 0,
            "passed": True,
            "missing_pupil_samples": False,
            "elapsed_seconds": 0.0,
            "cache_hit": True,
        })
    else:
        # Invalidate completion BEFORE starting. A timeout or failed rerun must
        # not associate yesterday's passed comparison with today's inputs.
        binding_path.unlink(missing_ok=True)
        comparison_path.unlink(missing_ok=True)
        completed, comparison_path, comparison = run_attempt(
            experiment_dir,
            "gui_default",
            "",
        )
        completed_returncode = completed.returncode
    # Set Vignetting coefficients are measured with the exact GUI/APG contract.
    # The experiment itself then performs strict APG and calibrated-paraxial
    # retraces with aperture updates; its calibrated output is the handoff.
    fallback_used = False
    _module_files.write_json(bridge_root / "bridge_attempts.json", attempts)
    if completed_returncode != 0 or not bool(comparison.get("passed", False)):
        raise RuntimeError(
            "aperture-realized vignetting field bridge failed: "
            f"rc={completed_returncode}, output={bridge_root}"
        )

    if not realized_path.is_file():
        raise FileNotFoundError(realized_path)
    realized = _module_files.read_json(realized_path)
    realized["fields"] = _module_contracts.fields_on_exact_nodes(
        realized, nodes, allow_zero_for_missing=False
    )
    handoff_dir = bridge_root / "handoff"
    handoff_dir.mkdir(parents=True, exist_ok=True)
    _module_files.write_json(handoff_dir / "system.json", realized)
    shutil.copy2(_module_files.source_file(source, "variables.json"), handoff_dir / "variables.json")
    _module_files.write_json(handoff_dir / "merit.json", merit)
    retrace_system_path = Path(comparison["outputs"]["calibrated_retrace_system"])
    retrace_score = retrace_system_path.parent / "score.json"
    if not retrace_score.is_file():
        raise FileNotFoundError(retrace_score)
    shutil.copy2(retrace_score, handoff_dir / "score.json")
    if input_binding and _module_cache.vignetting_bridge_input_binding(command) == input_binding:
        pending_binding_path = binding_path.with_suffix(".tmp")
        _module_files.write_json(pending_binding_path, input_binding)
        pending_binding_path.replace(binding_path)
    acceptance = comparison.get("acceptance", {})
    aperture_stability = comparison.get("aperture_stability", {})
    report = {
        "contract": _module_policy.MATERIAL_BRIDGE_VIGNETTING_CONTRACT,
        "performed": True,
        "illumination_margin_trim": comparison.get("illumination_margin_trim"),
        "handoff_advisory_contract": server.vignetting_handoff_policy.CONTRACT,
        "calibration_diagnostics_passed": comparison.get("calibration_diagnostics_passed"),
        "fallback_used": bool(comparison.get("fallback_used")),
        "source_pupil_retained": bool(comparison.get("source_pupil_retained")),
        "apg_retrace_is_diagnostic_not_admission": True,
        "cp_per_ray_convergence_gate": False,
        "cache_hit": cache_hit,
        "source": str(source.resolve()),
        "handoff": str(handoff_dir.resolve()),
        "target_nodes": list(nodes),
        "target_field_hy_interpolation": False,
        "source_aiming_mode": _module_contracts.system_aiming_mode(source_system),
        "set_vignetting_aiming_mode": comparison.get("bridge_policy", {}).get(
            "set_vignetting_aiming_mode"
        ),
        "set_vignetting_aiming_fallback_used": fallback_used,
        "handoff_aiming_mode": _module_contracts.system_aiming_mode(realized),
        "attempts": attempts,
        "minimum_relative_illumination": minimum,
        "apg_trace_coverage": acceptance.get("apg_trace_coverage"),
        "calibrated_trace_coverage": acceptance.get(
            "calibrated_trace_coverage"
        ),
        "trace_coverage_absolute_difference": acceptance.get(
            "trace_coverage_absolute_difference"
        ),
        "apg_min_field_coverage": acceptance.get("apg_min_field_coverage"),
        "calibrated_min_field_coverage": acceptance.get(
            "calibrated_min_field_coverage"
        ),
        "min_field_coverage_absolute_difference": acceptance.get(
            "min_field_coverage_absolute_difference"
        ),
        "source_to_apg_max_aperture_relative_change": aperture_stability.get(
            "source_to_apg_max_relative_change"
        ),
        "source_to_calibrated_max_aperture_relative_change": (
            aperture_stability.get("source_to_calibrated_max_relative_change")
        ),
        "apg_to_calibrated_max_aperture_relative_difference": (
            aperture_stability.get("apg_to_calibrated_max_relative_difference")
        ),
        "elapsed_seconds": time.perf_counter() - started,
        "comparison": str(comparison_path.resolve()),
    }
    _module_files.write_json(bridge_root / "vignetting_bridge_report.json", report)
    return handoff_dir, report
