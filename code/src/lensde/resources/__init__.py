"""Project-local resources, with no fallback to an earlier workspace."""
from .paths import MaterialResourceError, ProjectPaths, discover_project_root

__all__ = ["MaterialResourceError", "ProjectPaths", "discover_project_root"]
