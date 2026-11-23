"""Production helpers extracted from run_minimal_vignetting_only_de; unrelated historical entry points omitted."""

from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import json

from lensde.runtime import processes as subprocess

import time

from pathlib import Path

from lensde.runtime import native_io_paths as native_io_paths

def load_json(path: Path) -> dict:
    path = native_io_paths.python_path(path)
    return json.loads(path.read_text(encoding="utf-8-sig"))

def write_json(path: Path, payload: object) -> None:
    path = native_io_paths.python_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

def run_logged(command: list[str], cwd: Path, output: Path) -> float:
    output = native_io_paths.python_path(output)
    output.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    with native_io_paths.native_command(command, cwd, force_native=True) as native_command:
        completed = subprocess.run(
            native_command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    elapsed = time.perf_counter() - started
    (output / "stdout.log").write_text(completed.stdout, encoding="utf-8")
    (output / "stderr.log").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(
            f"{output.name} failed with return code {completed.returncode}; "
            f"see {output / 'stderr.log'}"
        )
    return elapsed
