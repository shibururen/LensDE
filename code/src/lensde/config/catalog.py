"""Read published benchmark inputs from the project checkout."""
from __future__ import annotations

from dataclasses import asdict, dataclass
import json
from pathlib import Path, PurePosixPath
from typing import Any

from lensde.resources import ProjectPaths


TASK_ORDER = ("S1", "S2", "S3", "S4", "6A", "A1", "A2", "A3", "A4", "DF1")
ROLES = ("system", "variables", "merit", "options")
FILENAMES = {"system": "inputs/system_input.json", "variables": "inputs/variables.json",
             "merit": "inputs/merit.json", "options": "options.saved.json"}
CONTROL_KEYS = frozenset(("locale", "language", "ui_language", "output_path", "output_dir", "out_dir"))
LABELS = {
    "zh-CN": {"configuration_kind": "保存配置", "normalization": "未应用运行时归一化", "task": "任务"},
    "en": {"configuration_kind": "Saved configuration", "normalization": "Runtime normalization not applied", "task": "Task"},
}


class ConfigurationError(ValueError):
    """Configuration is missing, damaged or outside the supported saved schema."""


class MissingConfigurationError(ConfigurationError, FileNotFoundError):
    """A requested task or required local artifact does not exist."""


class ConfigurationSchemaError(ConfigurationError):
    """Saved data cannot be read under the explicit catalog contract."""


class SeedCoordinateError(ConfigurationError, KeyError):
    """No unique recorded seed exists for the exact requested coordinates."""


def _read(root: Path, relative: str) -> bytes:
    parts = PurePosixPath(relative)
    if (not relative or "\\" in relative or ":" in relative or parts.is_absolute()
            or ".." in parts.parts):
        raise ConfigurationSchemaError(f"Artifact path is not project-relative: {relative!r}")
    path = (root / relative).resolve()
    if path == root or not path.is_relative_to(root):
        raise ConfigurationSchemaError(f"Artifact path escapes the project: {relative!r}")
    try:
        return path.read_bytes()
    except FileNotFoundError as error:
        raise MissingConfigurationError(f"Required local configuration artifact is missing: {path}") from error


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ConfigurationSchemaError(f"Duplicate saved JSON key: {key!r}")
        result[key] = value
    return result


def _nonfinite(value):
    raise ConfigurationSchemaError(f"Non-finite saved JSON value: {value}")


def _decode(data: bytes, role: str) -> dict[str, Any]:
    try:
        value = json.loads(data.decode("utf-8-sig"), object_pairs_hook=_pairs, parse_constant=_nonfinite)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ConfigurationSchemaError(f"Invalid UTF-8 JSON in {role}: {error}") from error
    if not isinstance(value, dict):
        raise ConfigurationSchemaError(f"Expected a saved JSON object for {role}")
    return value


def _int(value, field):
    if type(value) is not int:
        raise ConfigurationSchemaError(f"Expected a recorded integer for {field}; got {value!r}")
    return value


@dataclass(frozen=True)
class BenchmarkSummary:
    task: str
    saved_option_count: int
    copied_core_inputs: int
    configuration_kind: str = "saved"
    runtime_normalization_applied: bool = False
    production_launch_supported: bool = False

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class RecordedCandidateSeeds:
    task: str
    source_rank: int
    cycle: int
    candidate_archive_id: str
    optimizer_seed: int
    population_seed: int
    optimizer_seed_base: int
    population_seed_base: int
    seed_policy: str
    seed_key: str

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class SavedBenchmark:
    """Immutable saved bytes; every parsed property returns a separate value.

    Editing a returned dict leaves the saved inputs and other callers' views intact.
    """
    task: str
    _documents: tuple[tuple[str, bytes], ...]
    _local_paths: tuple[tuple[str, str], ...]
    _project_root: Path

    @property
    def configuration_kind(self) -> str:
        return "saved"

    @property
    def runtime_normalization_applied(self) -> bool:
        return False

    @property
    def production_launch_supported(self) -> bool:
        return False

    def raw_document(self, role: str) -> bytes:
        try:
            return dict(self._documents)[role]
        except KeyError as error:
            raise ConfigurationSchemaError(f"Unknown saved document role: {role!r}") from error

    def document(self, role: str) -> dict[str, Any]:
        return _decode(self.raw_document(role), f"{self.task}:{role}")

    @property
    def system(self) -> dict[str, Any]:
        return self.document("system")

    @property
    def variables(self) -> dict[str, Any]:
        return self.document("variables")

    @property
    def merit(self) -> dict[str, Any]:
        return self.document("merit")

    @property
    def options(self) -> dict[str, Any]:
        return self.document("options")

    @property
    def scientific_configuration(self) -> dict[str, Any]:
        """Exact saved scientific documents; excludes locale and run location."""
        return {role: self.document(role) for role in ROLES}

    @property
    def provenance(self) -> dict[str, Any]:
        data = _read(self._project_root, f"benchmarks/{self.task}/catalog.json")
        catalog = _decode(data, f"{self.task}:catalog")
        if catalog.get("task") != self.task:
            raise ConfigurationSchemaError(f"Unexpected saved catalog identity for {self.task}")
        baseline = catalog["baseline_record"]
        return {"artifact_paths": dict(self._local_paths),
                "original_inputs": baseline["input_files"],
                "original_options": baseline["options_source"],
                "original_formal_root": catalog["formal_root_provenance"],
                "meaning": "Original absolute paths are source attribution only; the loader never opens them."}

    @property
    def seed_recipe(self) -> dict[str, Any]:
        data = _read(self._project_root, f"benchmarks/{self.task}/seed_recipe.json")
        recipe = _decode(data, f"{self.task}:seed_recipe")
        if recipe.get("task") != self.task or not isinstance(recipe.get("fixed_material_candidate_mapping"), list):
            raise ConfigurationSchemaError(f"{self.task}: missing or invalid saved seed coordinate table")
        return recipe

    def fixed_material_seeds(self, *, source_rank: int, cycle: int,
                             candidate_archive_id: str | None = None) -> RecordedCandidateSeeds:
        """Look up an exact saved coordinate, never derive an unseen seed.

        source_rank is the recorded summary rank, not the repair directory index.
        Both source_rank and cycle are required, with no implicit default.
        """
        _int(source_rank, "source_rank")
        _int(cycle, "cycle")
        found = []
        for row in self.seed_recipe.get("fixed_material_candidate_mapping", []):
            values = row.get("saved_values", {})
            if values.get("source_rank") != source_rank or values.get("cycle") != cycle:
                continue
            if candidate_archive_id is not None and row.get("candidate_archive_id") != candidate_archive_id:
                continue
            found.append(row)
        if len(found) != 1:
            raise SeedCoordinateError(f"{self.task}: expected one recorded fixed-material seed for source_rank={source_rank}, cycle={cycle}, candidate_archive_id={candidate_archive_id!r}; found {len(found)}. No seed was synthesized.")
        row, values = found[0], found[0]["saved_values"]
        required = ("optimizer_seed", "population_seed", "optimizer_seed_base", "population_seed_base")
        missing = [field for field in (*required, "seed_policy", "seed_key") if field not in values]
        if missing:
            raise SeedCoordinateError(f"{self.task}: recorded candidate lacks {missing}; no fallback is available")
        return RecordedCandidateSeeds(self.task, source_rank, cycle, row["candidate_archive_id"],
            *(_int(values[field], field) for field in required), values["seed_policy"], values["seed_key"])

    def command_seed_plan(self, command_id: str) -> dict[str, Any]:
        rows = [row for row in self.seed_recipe.get("command_seed_plans", []) if row.get("command_id") == command_id]
        if len(rows) != 1:
            raise SeedCoordinateError(f"{self.task}: no unique recorded command seed plan for {command_id!r}")
        return rows[0]

    def material_sources(self) -> dict[tuple[str | int, ...], Path]:
        """Resolve local material files separately; leave saved JSON unchanged."""
        return ProjectPaths(self._project_root).material_sources(self.system)

    def inspection(self, *, locale: str = "en") -> dict[str, Any]:
        if locale not in LABELS:
            raise ConfigurationSchemaError(f"Unsupported presentation locale: {locale!r}")
        return {"task": self.task, "configuration_kind": "saved",
                "runtime_normalization_applied": False, "production_launch_supported": False,
                "saved_option_count": len(self.options),
                "variable_count": len(self.variables["variables"]),
                "presentation": {"locale": locale, "labels": dict(LABELS[locale])}}


class BenchmarkCatalog:
    """List the published benchmarks and read the inputs for a selected task."""
    def __init__(self, root: Path | str | None = None):
        self.paths = ProjectPaths(root)
        self.root = self.paths.root
        index = _decode(_read(self.root, "benchmarks/catalog.json"), "benchmark index")
        rows = index.get("tasks")
        if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
            raise ConfigurationSchemaError("Benchmark index requires task records")
        self._tasks = {}
        for row in rows:
            task = row.get("task")
            if task not in TASK_ORDER or task in self._tasks:
                raise ConfigurationSchemaError(f"Unknown or duplicate benchmark task: {task!r}")
            if row.get("catalog") != f"benchmarks/{task}/catalog.json":
                raise ConfigurationSchemaError(f"Unexpected catalog path for {task}")
            self._tasks[task] = row
        if set(self._tasks) != set(TASK_ORDER):
            raise ConfigurationSchemaError("Benchmark index must contain all ten published tasks")

    def list_benchmarks(self) -> tuple[BenchmarkSummary, ...]:
        return tuple(BenchmarkSummary(task, _int(self._tasks[task]["saved_option_count"], "saved_option_count"),
                                      _int(self._tasks[task]["copied_core_inputs"], "copied_core_inputs")) for task in TASK_ORDER)

    def load(self, task: str) -> SavedBenchmark:
        if task not in self._tasks:
            raise MissingConfigurationError(f"Unknown formal benchmark: {task!r}; no task substitution")
        base = f"benchmarks/{task}/"
        documents, local_paths = [], []
        for role in ROLES:
            relative = base + FILENAMES[role]
            data = _read(self.root, relative)
            value = _decode(data, task + ":" + role)
            if role == "variables" and (not isinstance(value.get("variables"), list) or not value["variables"]):
                raise ConfigurationSchemaError(f"{task}: saved variable order must be a nonempty list")
            if role == "system" and any(not isinstance(value.get(key), list) or not value[key]
                                        for key in ("surfaces", "fields", "wavelengths")):
                raise ConfigurationSchemaError(f"{task}: saved system requires surfaces, fields and wavelengths")
            if role == "options":
                if CONTROL_KEYS.intersection(value):
                    raise ConfigurationSchemaError(f"{task}: UI/output controls cannot be scientific options: {sorted(CONTROL_KEYS.intersection(value))}")
            documents.append((role, data))
            local_paths.append((role, relative))
        return SavedBenchmark(task, tuple(documents), tuple(local_paths), self.root)

    def inspect(self, task: str, *, locale: str = "en") -> dict[str, Any]:
        return self.load(task).inspection(locale=locale)
