"""Logical checkpoint entry paths retained independently of implementation location."""
from __future__ import annotations

from lensde.resources import ProjectPaths
from pathlib import Path


PROJECT_ROOT = ProjectPaths().root
SCRIPT_PATH = Path(__file__).resolve().parents[1] / 'run_checkpoint_de400_layer.py'
GUI_DIR = PROJECT_ROOT / "web"
CPP_DIR = PROJECT_ROOT / "native"
SCRIPTS_DIR = SCRIPT_PATH.parents[1] / "helpers"
