"""Resolve resources without importing the workbench or rewriting prescriptions."""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class MaterialResourceError(FileNotFoundError):
    """A requested material is absent from this project's database."""


def discover_project_root(start: Path | None = None) -> Path:
    """Find a LensDE checkout; installed clients can pass an explicit root."""
    location = (start or Path(__file__)).resolve()
    if location.is_file():
        location = location.parent
    for candidate in (location, *location.parents):
        marker = candidate / "pyproject.toml"
        if marker.is_file() and re.search(r'^name\s*=\s*[\'"]lensde[\'"]\s*$', marker.read_text(encoding="utf-8"), re.MULTILINE):
            return candidate
    raise FileNotFoundError(f"Cannot locate a LensDE project above {location}; pass ProjectPaths(root=...) explicitly")


def confined(path: Path, parent: Path, *, role: str) -> Path:
    """Resolve existing links and require a strict descendant of parent."""
    resolved, boundary = path.resolve(), parent.resolve()
    if resolved == boundary or not resolved.is_relative_to(boundary):
        raise ValueError(f"{role} must be inside {boundary}: {resolved}")
    return resolved


@dataclass(frozen=True)
class ProjectPaths:
    root: Path | str | None = None

    def __post_init__(self) -> None:
        root = discover_project_root() if self.root is None else Path(self.root).expanduser().resolve()
        if not root.is_dir():
            raise FileNotFoundError(f"LensDE project directory does not exist: {root}")
        object.__setattr__(self, "root", root)

    @property
    def database(self) -> Path:
        return confined(self.root / "catalogs/database", self.root, role="Material database")

    @property
    def catalog(self) -> Path:
        path = self.database / "catalog_nk.csv"
        if not path.is_file():
            raise MaterialResourceError(f"Missing LensDE material catalog: {path}; expected this project's catalogs/database/catalog_nk.csv")
        return path

    def material_source(self, source_path: str | Path) -> Path:
        """Rebase a database reference to this project, without changing its JSON.

        Accepts data-nk-relative references and old absolute references containing
        a database path component. The old absolute file is never opened.
        """
        raw = str(source_path)
        if not raw or not raw.strip():
            raise ValueError("Material source_path must be a nonempty database reference")
        parts = tuple(part for part in raw.replace("\\", "/").split("/") if part and part != ".")
        if ".." in parts:
            raise ValueError(f"Material source_path cannot traverse parent directories: {raw!r}")
        database_indices = [index for index, part in enumerate(parts) if part.casefold() == "database"]
        if database_indices:
            parts = parts[database_indices[-1] + 1:]
        elif raw.startswith(("/", "\\")) or re.match(r"^[A-Za-z]:", raw):
            raise ValueError(f"Material source_path is outside the project database and contains no database component: {raw!r}")
        if not parts or any(":" in part for part in parts):
            raise ValueError(f"Invalid material database reference: {raw!r}")
        candidate = confined(self.database.joinpath(*parts), self.database, role="Material source_path")
        if not candidate.is_file():
            raise MaterialResourceError(f"Material source_path {raw!r} resolves to missing project file {candidate}; no old-workspace fallback is used")
        return candidate

    def material_sources(self, document: Any) -> dict[tuple[str | int, ...], Path]:
        """Return source_path locations without modifying any document field."""
        resolved = {}

        def visit(value: Any, location: tuple[str | int, ...]) -> None:
            if isinstance(value, dict):
                for key, child in value.items():
                    if key == "source_path" and child:
                        resolved[location + (key,)] = self.material_source(child)
                    else:
                        visit(child, location + (key,))
            elif isinstance(value, list):
                for index, child in enumerate(value):
                    visit(child, location + (index,))

        visit(document, ())
        return resolved
