"""Logical material-bridge entry paths independent of implementation location."""
from __future__ import annotations

from lensde.resources import ProjectPaths
from pathlib import Path


PROJECT_ROOT = ProjectPaths().root
SCRIPT_PATH = Path(__file__).resolve().parents[1] / 'run_material5_bridge_selection.py'
GUI_DIR = PROJECT_ROOT / "web"
CPP_DIR = PROJECT_ROOT / "native"
SCRIPTS_DIR = SCRIPT_PATH.parents[1] / "helpers"

# Every imported responsibility module participates in the existing bridge_code identity.
IMPLEMENTATION_FILES = (SCRIPT_PATH, *(Path(__file__).with_name(name) for name in ('__init__.py', 'broker.py', 'cache.py', 'calibration.py', 'commands.py', 'continuation.py', 'contracts.py', 'deepening.py', 'diagnostics.py', 'execution.py', 'files.py', 'identity.py', 'inputs.py', 'paths.py', 'policy.py', 'ranking.py', 'reporting.py', 'runner.py', 'scoring.py', 'selection.py', 'state.py')))
