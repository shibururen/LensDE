"""Isolated native-process execution; importing this package starts nothing."""
from .native import NativeRunner, RunResult

__all__ = ["NativeRunner", "RunResult"]
