"""C++ runtime linkage shared by production builds and native diagnostics."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path

from . import processes as subprocess


def cpu_runtime_linkage(compiler: str) -> dict:
    """Keep compilation unchanged and bind MinGW's matching C++ runtime.

    The Windows MinGW 8 shared libstdc++ used by the original CPU toolchain
    crashes during process shutdown after nontrivial thread-local objects are
    used. Mixing static libstdc++ with dynamic libgcc also corrupts concurrent
    thread teardown. Bind both matching runtimes; compilation is unchanged.
    CUDA/MSVC and non-Windows builds do not use this link-only workaround.
    """
    if os.name != "nt":
        return {"link_flags": [], "static_runtime": None}
    target = subprocess.check_output([compiler, "-dumpmachine"], text=True).strip()
    if "mingw" not in target.lower():
        return {"link_flags": [], "static_runtime": None}
    library = subprocess.check_output(
        [compiler, "-print-file-name=libstdc++.a"], text=True).strip()
    archive = Path(library).resolve()
    if not archive.is_file():
        raise FileNotFoundError("Matching MinGW static C++ runtime required: " + library)
    support = []
    for name in ("libgcc.a", "libgcc_eh.a"):
        library = subprocess.check_output(
            [compiler, "-print-file-name=" + name], text=True).strip()
        path = Path(library).resolve()
        if not path.is_file():
            raise FileNotFoundError("Matching MinGW static support runtime required: " + library)
        support.append({"archive": str(path),
                        "archive_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "archive_bytes": path.stat().st_size})
    return {
        "link_flags": ["-static-libstdc++", "-static-libgcc"],
        "static_runtime": {
            "kind": "MinGW libstdc++",
            "compiler_target": target,
            "archive": str(archive),
            "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "archive_bytes": archive.stat().st_size,
            "support_archives": support,
            "reason": "nontrivial thread-local object shutdown compatibility",
        },
    }
