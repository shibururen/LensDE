"""Content identity for the complete registered material-bridge implementation."""
from __future__ import annotations

import hashlib
import json
from .paths import IMPLEMENTATION_FILES, SCRIPT_PATH


def implementation_identity() -> str | None:
    """Invalidate saved work when any explicitly registered implementation module changes."""
    records: dict[str, str] = {}
    for path in IMPLEMENTATION_FILES:
        try:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError:
            return None
        records[path.relative_to(SCRIPT_PATH.parent).as_posix()] = digest
    return hashlib.sha256(json.dumps(records, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
