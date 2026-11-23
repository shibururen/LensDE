"""Production helpers extracted from run_iterative_hammer_dls_probe; unrelated historical entry points omitted."""

from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import math

from pathlib import Path

from typing import Any

from lensde.helpers.probe_dls_mutation_polish import finite_float

DEFAULT_DATABASE = PROJECT_ROOT / "catalogs/database"

def parse_glass_specs(path: Path) -> tuple[float, float, float | None, float | None] | None:
    nd: float | None = None
    vd: float | None = None
    wavelength_min: float | None = None
    wavelength_max: float | None = None
    table_min = math.inf
    table_max = -math.inf
    in_tabulated_data = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        if stripped.startswith("- type:") or stripped.startswith("type:"):
            in_tabulated_data = "tabulated n" in stripped.lower()
        elif in_tabulated_data and stripped and stripped[0] in "+-.0123456789":
            values = stripped.split()
            if len(values) >= 2:
                wavelength = finite_float(values[0], math.nan)
                if math.isfinite(wavelength):
                    table_min = min(table_min, wavelength)
                    table_max = max(table_max, wavelength)
        if stripped.startswith("nd:"):
            nd = finite_float(stripped.split(":", 1)[1].strip(), math.nan)
        elif stripped.startswith("Vd:"):
            vd = finite_float(stripped.split(":", 1)[1].strip(), math.nan)
        elif stripped.startswith("wavelength_range:"):
            values = stripped.split(":", 1)[1].strip().split()
            if len(values) >= 2:
                wavelength_min = finite_float(values[0], math.nan)
                wavelength_max = finite_float(values[1], math.nan)
    if nd is None or vd is None or not math.isfinite(nd) or not math.isfinite(vd):
        return None
    if wavelength_min is not None and not math.isfinite(wavelength_min):
        wavelength_min = None
    if wavelength_max is not None and not math.isfinite(wavelength_max):
        wavelength_max = None
    if wavelength_min is None and math.isfinite(table_min):
        wavelength_min = table_min
    if wavelength_max is None and math.isfinite(table_max):
        wavelength_max = table_max
    return nd, vd, wavelength_min, wavelength_max

def normalized_glass_libraries(libraries: Any) -> list[str]:
    if isinstance(libraries, str):
        values = libraries.replace(";", ",").split(",")
    else:
        values = list(libraries or [])
    out: list[str] = []
    seen: set[str] = set()
    for value in values:
        name = str(value or "").strip()
        key = name.upper()
        if not name or key in seen:
            continue
        out.append(name)
        seen.add(key)
    return out

def resolve_glass_library_directory(database: Path, library: str) -> Path:
    glass_root = database / "data-nk" / "glass"
    requested = str(library or "").strip()
    if not requested:
        raise ValueError("glass library name must not be empty")
    direct = glass_root / requested.lower()
    if direct.is_dir():
        return direct
    if glass_root.is_dir():
        for child in glass_root.iterdir():
            if child.is_dir() and child.name.casefold() == requested.casefold():
                return child
    raise FileNotFoundError(
        f"Glass library {requested!r} was not found below {glass_root}"
    )

def normalized_required_wavelengths(required_wavelengths_um: Any) -> list[float]:
    values = {
        finite_float(value, math.nan)
        for value in required_wavelengths_um or []
    }
    return sorted(value for value in values if math.isfinite(value) and value > 0.0)

def load_glasses(
    database: Path,
    libraries: Any,
    required_wavelengths_um: Any = None,
    *,
    audit: dict[str, Any] | None = None,
) -> list[dict[str, Any]]:
    requested = normalized_glass_libraries(libraries)
    required = normalized_required_wavelengths(required_wavelengths_um)
    if not requested:
        raise ValueError("at least one glass library must be requested")
    glasses: list[dict[str, Any]] = []
    loaded_directories: set[Path] = set()
    for requested_library in requested:
        glass_root = database / "data-nk" / "glass"
        roots = (
            sorted(path for path in glass_root.iterdir() if path.is_dir())
            if requested_library.upper() == "ALL" and glass_root.is_dir()
            else [resolve_glass_library_directory(database, requested_library)]
        )
        requested_count = 0
        already_loaded = False
        for root in roots:
            resolved_root = root.resolve()
            if resolved_root in loaded_directories:
                already_loaded = True
                continue
            loaded_directories.add(resolved_root)
            library = root.name.upper()
            for path in sorted(root.glob("*.yml")):
                specs = parse_glass_specs(path)
                if specs is None:
                    continue
                nd, vd, wavelength_min, wavelength_max = specs
                covers_required = (
                    not required
                    or (
                        wavelength_min is not None
                        and wavelength_max is not None
                        and wavelength_min <= required[0] + 1.0e-9
                        and wavelength_max + 1.0e-9 >= required[-1]
                    )
                )
                if not covers_required:
                    if audit is not None:
                        audit.setdefault("rejections", []).append({
                            "name": path.stem,
                            "library": library,
                            "min_wavelength_um": wavelength_min,
                            "max_wavelength_um": wavelength_max,
                            "required_wavelengths_um": required,
                            "reason": (
                                "missing_declared_wavelength_range"
                                if wavelength_min is None or wavelength_max is None
                                else "wavelength_range_not_covering_system"
                            ),
                        })
                    continue
                glasses.append(
                    {
                        "name": path.stem,
                        "reference": path.stem,
                        "library": library,
                        "nd": nd,
                        "vd": vd,
                        "path": str(path),
                        "min_wavelength": wavelength_min,
                        "max_wavelength": wavelength_max,
                    }
                )
                requested_count += 1
        if (
            requested_count == 0
            and not already_loaded
            and requested_library.upper() != "ALL"
        ):
            raise RuntimeError(
                f"Glass library {requested_library!r} contains no usable nd/Vd records"
            )
    if not glasses:
        raise RuntimeError(
            f"Requested glass libraries contain no usable nd/Vd records covering "
            f"system wavelengths {required}: {requested}"
        )
    if audit is not None:
        audit.update({
            "policy": "declared_range_must_cover_all_system_wavelengths",
            "required_wavelengths_um": required,
            "accepted_count": len(glasses),
            "rejected_count": len(audit.get("rejections", [])),
        })
    return glasses
