"""Extracted paper engine: execution."""
from __future__ import annotations
import copy
from lensde.runtime import concurrency as concurrent
import heapq
import hashlib
import json
import math
import mimetypes
import os
import queue
import random
import shutil
import statistics
import sys
import threading
import time
import csv
from collections import Counter
from pathlib import Path
from typing import Callable
from urllib.parse import parse_qs, urlparse
from lensde.runtime import processes as subprocess
from lensde.runtime import native_io_paths
from lensde.optics import dynamic_aperture_contract, vignetting_handoff_policy, stage_illumination_trim, post_asphere_handoff
from lensde.seeds import protocol as seed_protocol
from . import state

from . import apertures as _lensde_apertures
from . import asphere_contracts as _lensde_asphere_contracts
from . import asphere_population as _lensde_asphere_population
from . import asphere_workflow as _lensde_asphere_workflow
from . import commands as _lensde_commands
from . import configuration as _lensde_configuration
from . import defaults as _lensde_defaults
from . import fields as _lensde_fields
from . import geometry as _lensde_geometry
from . import initialization as _lensde_initialization
from . import materials as _lensde_materials
from . import orchestration as _lensde_orchestration
from . import populations as _lensde_populations
from . import provenance as _lensde_provenance
from . import tracing as _lensde_tracing

def remaining_timeout_seconds(timeout_seconds: int, started_at: float) -> float | None:
    if timeout_seconds <= 0:
        return None
    return max(0.001, timeout_seconds - (time.time() - started_at))


def run_stage(args: list[str], timeout_seconds: int, started_at: float, env: dict[str, str] | None=None) -> tuple[int, str, str, bool]:
    try:
        with native_io_paths.native_command(args, state.CPP_ROOT, force_native=True) as native_args:
            completed = subprocess.run(native_args, cwd=str(state.CPP_ROOT), text=True, encoding='utf-8', errors='replace', capture_output=True, env=env, timeout=remaining_timeout_seconds(timeout_seconds, started_at))
        return (completed.returncode, completed.stdout, completed.stderr, False)
    except subprocess.TimeoutExpired as exc:
        return (-9, _lensde_materials.process_text(exc.stdout), _lensde_materials.process_text(exc.stderr), True)


class PersistentCppRunner:

    def __init__(self, exe: Path, timeout_seconds: int, started_at: float):
        self.exe = exe
        self.timeout_seconds = timeout_seconds
        self.started_at = started_at
        self.closed = False
        self.lines: queue.Queue[str | None] = queue.Queue()
        self.process = subprocess.Popen([str(exe), '--server'], cwd=str(state.CPP_ROOT), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', bufsize=1)
        self.reader = threading.Thread(target=self._read_stdout, daemon=True)
        self.reader.start()
        self._wait_ready()

    def _read_stdout(self) -> None:
        try:
            assert self.process.stdout is not None
            for line in self.process.stdout:
                self.lines.put(line)
        finally:
            self.lines.put(None)

    def _remaining(self) -> float | None:
        return remaining_timeout_seconds(self.timeout_seconds, self.started_at)

    def _wait_ready(self) -> None:
        output: list[str] = []
        while True:
            remaining = self._remaining()
            wait_seconds = 0.5 if remaining is None else min(0.5, max(0.001, remaining))
            try:
                line = self.lines.get(timeout=wait_seconds)
            except queue.Empty:
                if self.process.poll() is not None:
                    raise RuntimeError('persistent C++ runner exited before ready: ' + ''.join(output))
                continue
            if line is None:
                raise RuntimeError('persistent C++ runner closed before ready: ' + ''.join(output))
            if line.strip() == state.SERVER_READY:
                return
            output.append(line)

    def run(self, args: list[str]) -> tuple[int, str, str, bool]:
        if self.closed:
            return (-8, '', 'persistent C++ runner is already closed', False)
        if self.process.poll() is not None:
            return (self.process.returncode or -8, '', 'persistent C++ runner has exited', False)
        assert self.process.stdin is not None
        command = subprocess.list2cmdline([str(item) for item in args[1:]])
        out_lines: list[str] = []
        try:
            self.process.stdin.write(command + '\n')
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as exc:
            return (self.process.poll() or -8, ''.join(out_lines), str(exc), False)
        while True:
            remaining = self._remaining()
            if remaining is not None and remaining <= 0.001:
                self.close(force=True)
                return (-9, ''.join(out_lines), '', True)
            wait_seconds = 0.5 if remaining is None else min(0.5, max(0.001, remaining))
            try:
                line = self.lines.get(timeout=wait_seconds)
            except queue.Empty:
                if self.process.poll() is not None:
                    return (self.process.returncode or -8, ''.join(out_lines), '', False)
                continue
            if line is None:
                return (self.process.poll() or -8, ''.join(out_lines), '', False)
            stripped = line.strip()
            if stripped.startswith(state.SERVER_DONE):
                parts = stripped.split()
                try:
                    code = int(parts[-1])
                except (ValueError, IndexError):
                    code = 99
                subprocess.mark_stage(args, code)
                return (code, ''.join(out_lines), '', False)
            out_lines.append(line)

    def close(self, force: bool=False) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            if not force and self.process.poll() is None and (self.process.stdin is not None):
                self.process.stdin.write('__quit__\n')
                self.process.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        try:
            self.process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
