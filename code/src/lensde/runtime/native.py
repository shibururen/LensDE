"""One-shot native runner restricted to fresh, fingerprinted project builds."""
from __future__ import annotations

import hashlib
import json
import math
import os
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence

from lensde.resources.paths import ProjectPaths, confined
from . import native_io_paths
from .native_sources import load_native_registry, native_source_hashes, source_registry_sha256


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _stop_owned_child(process: subprocess.Popen, grace: float) -> None:
    if process.poll() is None:
        try:
            process.terminate()
        except ProcessLookupError:
            pass  # It exited between poll and terminate.
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        try:
            process.kill()
        except ProcessLookupError:
            pass
        process.wait()


@dataclass(frozen=True)
class RunResult:
    command: tuple[str, ...]
    cwd: Path
    outdir: Path
    returncode: int
    timed_out: bool
    elapsed_seconds: float
    pid: int
    stdout_path: Path
    stderr_path: Path

    @property
    def stdout(self) -> str:
        return self.stdout_path.read_text(encoding="utf-8", errors="replace")

    @property
    def stderr(self) -> str:
        return self.stderr_path.read_text(encoding="utf-8", errors="replace")


def _run_child(command: Sequence[str], *, cwd: Path, outdir: Path,
               timeout: float | None, env: Mapping[str, str] | None = None,
               terminate_grace: float = 2.0) -> RunResult:
    """Run only the child this call creates; no PID discovery or global cleanup."""
    if timeout is not None and (not math.isfinite(timeout) or timeout <= 0):
        raise ValueError("timeout must be finite and positive, or None")
    if not math.isfinite(terminate_grace) or terminate_grace <= 0:
        raise ValueError("terminate_grace must be finite and positive")
    stdout_path, stderr_path = outdir / "stdout.log", outdir / "stderr.log"
    child_env = os.environ.copy()
    if env is not None:
        child_env.update(env)
    started = time.monotonic()
    timed_out = False
    with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
        process = subprocess.Popen(
            [str(item) for item in command], cwd=str(cwd), env=child_env,
            stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            _stop_owned_child(process, terminate_grace)
        except BaseException:
            # We own exactly this child, including on interruption.
            _stop_owned_child(process, terminate_grace)
            raise
    return RunResult(tuple(str(item) for item in command), cwd, outdir,
                     process.returncode, timed_out, time.monotonic() - started,
                     process.pid, stdout_path, stderr_path)


class NativeRunner:
    def __init__(self, project: ProjectPaths | Path | str | None = None, *,
                 build_root: Path | str | None = None,
                 output_root: Path | str | None = None,
                 allow_diagnostics: bool = False):
        self.project = project if isinstance(project, ProjectPaths) else ProjectPaths(project)
        root = self.project.root
        self.build_root = confined(self._absolute(build_root or "build"), root, role="Build directory")
        self.output_root = confined(self._absolute(output_root or "runs"), root, role="Output directory")
        if ".baseline" in self.build_root.relative_to(root).parts:
            raise ValueError("Baseline builds are reference-only; NativeRunner requires a candidate build")
        if ".baseline" in self.output_root.relative_to(root).parts:
            raise ValueError("Baseline files are reference-only; outputs require a separate run directory")
        self.allow_diagnostics = allow_diagnostics

    def _absolute(self, path: Path | str) -> Path:
        path = native_io_paths.native_path(path)
        return path if path.is_absolute() else self.project.root / path

    def executable(self, executable: Path | str) -> tuple[Path, dict]:
        """Validate the build receipt, binary and matching local source hashes."""
        path = confined(self._absolute(executable), self.build_root, role="Native executable")
        if not path.is_file():
            raise FileNotFoundError(f"Project native executable is missing: {path}; build it locally first")
        receipt_path = path.with_suffix(".build.json")
        if not receipt_path.is_file():
            raise FileNotFoundError(f"Missing local build receipt {receipt_path}; historical binaries are not accepted")
        receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
        if receipt.get("label") != "candidate":
            raise ValueError(f"NativeRunner requires a candidate build, not a reference/unknown build: {receipt_path}")
        if receipt.get("diagnostics") and not self.allow_diagnostics:
            raise ValueError(f"Diagnostic binary requires allow_diagnostics=True: {path}")
        if receipt.get("executable_sha256") != sha256(path):
            raise ValueError(f"Native executable hash does not match its build receipt: {path}")
        source_hashes = receipt.get("source_sha256")
        if not isinstance(source_hashes, dict) or not source_hashes:
            raise ValueError(f"Build receipt contains no source fingerprints: {receipt_path}")
        native_root = confined(self.project.root / "native", self.project.root, role="Native source tree")
        for relative, expected in source_hashes.items():
            source = confined(native_root / relative, native_root, role="Build source")
            if not source.is_file() or sha256(source) != expected:
                raise ValueError(f"Native source differs from the build receipt: {source}; rebuild before running")
        actual=native_source_hashes(native_root)
        if receipt.get('diagnostics'):
            recorded_parts={relative.replace('\\','/').split('/')[0] for relative in source_hashes}
            actual={relative:value for relative,value in actual.items() if relative.split('/')[0] in recorded_parts}
        normalized={relative.replace('\\','/'):value for relative,value in source_hashes.items()}
        if len(normalized)!=len(source_hashes) or actual!=normalized:
            raise ValueError('Native source file set differs from the build receipt; rebuild before running')
        load_native_registry(native_root)
        if receipt.get('source_registry_sha256')!=source_registry_sha256(native_root):
            raise ValueError('Native source registry differs from the build receipt; rebuild before running')
        return path, receipt

    def run(self, executable: Path | str, args: Sequence[str], *,
            cwd: Path | str, outdir: Path | str, timeout: float | None,
            env: Mapping[str, str] | None = None) -> RunResult:
        path, receipt = self.executable(executable)
        working = self._absolute(cwd).resolve()
        if not working.is_dir() or not working.is_relative_to(self.project.root):
            raise ValueError(f"Native cwd must be an existing directory in {self.project.root}: {working}")
        output = confined(self._absolute(outdir), self.output_root, role="Run output")
        if isinstance(args, (str, bytes)):
            raise TypeError("args must be a sequence of arguments, not a command string")
        arguments = [str(item) for item in args]
        if any(item == "--out-dir" or item.startswith("--out-dir=") for item in arguments):
            raise ValueError("Pass outdir explicitly; NativeRunner owns the --out-dir argument")
        if timeout is not None and (not math.isfinite(timeout) or timeout <= 0):
            raise ValueError("timeout must be finite and positive, or None")
        command = [str(path), *arguments, "--out-dir", str(output)]
        with native_io_paths.native_command(command, working, force_native=True) as normalized:
            # A fresh directory prevents accidental replay into a historical run.
            output.mkdir(parents=True, exist_ok=False)
            metadata = {"schema_version": 1, "command": normalized, "cwd": str(working),
                        "outdir": str(output), "executable_sha256": receipt["executable_sha256"],
                        "diagnostics": bool(receipt.get("diagnostics")), "timeout": timeout}
            (output / "command.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
            result = _run_child(normalized, cwd=working, outdir=output, timeout=timeout, env=env)
            metadata.update(returncode=result.returncode, timed_out=result.timed_out,
                            elapsed_seconds=result.elapsed_seconds, pid=result.pid)
            (output / "run.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
            return result
