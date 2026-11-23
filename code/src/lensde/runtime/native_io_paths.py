"""Budget native paths at creation; never relocate or copy back native output."""
from __future__ import annotations

from contextlib import contextmanager
import json
import hashlib
from functools import lru_cache
import os
from pathlib import Path
import re

MAX_NATIVE_PATH = 240
# Includes filenames, nested diagnostics and temporary output suffixes.
OUTPUT_RESERVE = 96
INPUT_FLAGS = ("--system", "--merit", "--variables", "--initial-population",
               "--glass-search", "--catalog")

# Logical stage labels in manifests are unchanged. Only new-run directory
# components use these names; existing experiments keep their original paths.
COMPACT_MARKER = "output_names.json"
COMPACT_NAMES = {
    "de_dls_two_layer_work": "_de", "layer1_checkpoint_de400": "checkpoint",
    "stage_boundaries": "handoff", "scout_to_formation": "s2f",
    "formation_to_periodic": "f2p", "periodic_to_final": "p2f",
    "periodic_refresh_optimization": "period", "formation_round_01": "form01",
    "direction_scouts": "scouts", "direction_scout_batch": "scout_batch",
    "serialized_aiming_audit": "aim_audit", "illumination_trim": "trim",
    "measurement": "meas", "set_vignetting": "sv",
    "field_measurement": "field_meas", "vignetting_field_bridge": "vig_bridge",
    "calibrated_paraxial_retrace": "cp_trace", "sampling_audit": "sampling",
    "screening_contract": "screen", "main_field_probe": "fields",
    "handoff_rechecks": "rechecks", "terminal_zemax_vignetting": "terminal_vig",
    "fixed_validation_parent": "parent", "experiment": "exp",
    "layer2_material5_bridge": "mat5", "diagnostics": "diag",
    "layer2_glass_choice_scout": "glass_scout",
    "layer2_direct_glass_de": "glass_de", "layer2_locked_glass_de": "locked_de",
    "fixed_glass_feedback_repair": "feedback",
    "layer2_final_locked_glass_dls": "final_dls",
    "layer2_material_chromatic_repair": "chroma",
    "layer2_terminal_zemax_vignetting": "terminal_vig",
    "layer2_stage_boundary_material_to_scout_vignetting": "m2s",
    "layer2_stage_boundary_scout_to_locked_de_vignetting": "s2l",
    "layer2_stage_boundary_locked_de_to_final_dls_vignetting": "l2d",
    "layer2_final_locked_glass_dls_vignetting": "dls_vig",
    "funnel_global_explore_seed_pools": "seed_pools",
    "funnel_global_explore_outers": "outers",
    "apg_retrace": "apg_trace",
    "layer2_material_glass_funnel": "materials",
    "layer2_stage_boundary_chromatic_repair_to_material_funnel_vignetting": "c2m",
    "layer2_stage_boundary_material_bridge_to_material_funnel_vignetting": "b2m",
    "refine_like": "rfn", "locked_parent_cpu_score": "parent_cpu",
}


@lru_cache(maxsize=4096)
def compact_names_enabled(parent: Path) -> bool:
    parent = native_path(parent).absolute()
    return any((path / COMPACT_MARKER).is_file() for path in (parent, *parent.parents))


def enable_compact_names(root: Path) -> None:
    """Opt a fresh run into stable, in-place names before creating its stages."""
    root = native_path(root).resolve()
    if os.name == "nt" and path_units(root) + 62 + OUTPUT_RESERVE > MAX_NATIVE_PATH:
        raise OSError(f"Experiment root leaves insufficient native path headroom: {root}. Choose a shorter run name; no temporary output relocation is allowed.")
    root.mkdir(parents=True, exist_ok=True)
    marker = root / COMPACT_MARKER
    contract = {"contract": "in_place_compact_names_v2", "root": str(root),
                "directory_names": COMPACT_NAMES, "temporary_output_relocation": False,
                "logical_labels_preserved": True}
    if marker.exists():
        if json.loads(marker.read_text(encoding="utf-8")).get("contract") != contract["contract"]:
            raise ValueError(f"Incompatible output naming contract: {marker}")
    else:
        marker.write_text(json.dumps(contract, indent=2), encoding="utf-8")
    compact_names_enabled.cache_clear()


def short_name(label: str) -> str:
    """Bound labels without losing ordinal identity or merging arbitrary names."""
    if label in COMPACT_NAMES:
        return COMPACT_NAMES[label]
    match = re.fullmatch(r"cand_(\d+)_rank_(\d+)", label)
    if match:
        return f"c{match[1]}r{match[2]}"
    match = re.fullmatch(r"outer_(\d+)_seed_trace_gate_v2", label)
    if match:
        return f"o{match[1]}_gate"
    match = re.fullmatch(r"(\d+)_(.+)", label)
    if match and len(label) > 24:
        return match[1] + '_' + hashlib.sha256(label.encode('utf-8')).hexdigest()[:12]
    for pattern, prefix in ((r"candidate_(\d+)_.*", "c"),
                            (r"source_(\d+)_.*", "src"),
                            (r"direction_scout_(\d+)", "sc"),
                            (r"trial_(\d+)", "t"), (r"feedback_(\d+)", "fb")):
        match = re.fullmatch(pattern, label)
        if match:
            # Source-bearing labels also carry a digest: indices alone are not
            # a collision-safe identity when reusing a candidate root.
            suffix = "_" + hashlib.sha256(label.encode("utf-8")).hexdigest()[:6] if ".*" in pattern else ""
            return prefix + match[1] + suffix
    if len(label) > 24:
        readable = re.sub(r"[^A-Za-z0-9_-]", "_", label)[:8]
        return readable + "_" + hashlib.sha256(label.encode("utf-8")).hexdigest()[:12]
    return label


def named_child(parent: Path, label: str) -> Path:
    """Join a logical directory at its producer, so all consumers see its path."""
    parent = Path(parent)
    if not compact_names_enabled(parent):
        return parent / label
    if Path(label).name != label or label in (".", ".."):
        raise ValueError(f"Expected one directory component, got {label!r}")
    return parent / short_name(label)


def named_path(parent: Path, relative: str) -> Path:
    """Compact each component of an explicitly relative, nested stage label."""
    path = Path(relative)
    if path.is_absolute() or path.drive or ".." in path.parts:
        raise ValueError(f"Expected confined relative output path: {relative!r}")
    for part in path.parts:
        parent = named_child(parent, part)
    return parent


def path_units(path: Path) -> int:
    return len(str(path).encode("utf-16-le")) // 2


def python_path(path: Path) -> Path:
    """Use extended Win32 paths for deep Python I/O, even without registry opt-in."""
    path = Path(path)
    if os.name != "nt" or str(path).startswith("\\\\?\\"):
        return path
    absolute = str(path.resolve())
    if path_units(Path(absolute)) < 200:
        return path
    if absolute.startswith("\\\\"):
        return Path("\\\\?\\UNC\\" + absolute[2:])
    return Path("\\\\?\\" + absolute)


def native_path(path: str | Path) -> Path:
    """Return a Win32 path accepted by the native narrow-path CLI.

    ``python_path`` deliberately adds the extended-length prefix for Python
    filesystem operations.  That prefix must not cross the subprocess
    boundary: the MinGW filesystem implementation used by the Optiland
    executables can otherwise reduce ``\\\\?\\D:\\...`` to a root-relative
    path. Long arguments are rejected by ``native_command``; output producers
    must choose compact directory names before creating commands.
    """
    value = str(path)
    if os.name != "nt":
        return Path(value)
    if value.startswith("\\\\?\\UNC\\"):
        return Path("\\\\" + value[8:])
    if value.startswith("\\\\?\\"):
        return Path(value[4:])
    return Path(value)


@contextmanager
def native_command(command: list[str], cwd: Path, *, force_native: bool = False):
    """Normalize and validate the real command, without changing its I/O tree."""
    original = list(command)
    if os.name != "nt":
        yield original
        return
    # Normalize every extended path argument, including less common native
    # flags such as --jobs/--broker-stats.
    normalized = [
        str(native_path(value))
        if str(value).startswith("\\\\?\\")
        else str(value)
        for value in original
    ]
    if (
        "--out-dir" not in normalized
        or (not force_native and not Path(normalized[0]).name.lower().startswith("optiland"))
    ):
        yield normalized
        return
    for flag in (*INPUT_FLAGS, "--out-dir"):
        if flag in normalized:
            index = normalized.index(flag) + 1
            normalized[index] = str(native_path(normalized[index]))
    out_index = normalized.index("--out-dir") + 1
    requested = Path(normalized[out_index])
    if not requested.is_absolute():
        requested = cwd / requested
    requested = requested.resolve()
    inputs = {}
    for flag in INPUT_FLAGS:
        if flag in normalized:
            index = normalized.index(flag) + 1
            path = Path(normalized[index])
            inputs[index] = (path if path.is_absolute() else cwd / path).resolve()
    if path_units(requested) + OUTPUT_RESERVE <= MAX_NATIVE_PATH and all(
        path_units(path) <= MAX_NATIVE_PATH for path in inputs.values()
    ):
        yield normalized
        return
    too_long_inputs = [str(path) for path in inputs.values() if path_units(path) > MAX_NATIVE_PATH]
    raise OSError(
        f"Native path budget exceeded: output={requested} "
        f"({path_units(requested)} + reserve {OUTPUT_RESERVE} > {MAX_NATIVE_PATH}), "
        f"long_inputs={too_long_inputs}. Shorten stage names at their producer; "
        "temporary output relocation is disabled."
    )
