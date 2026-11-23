"""Register discrete GlassChoice Scout budgets, sampling and batched execution options."""
from __future__ import annotations
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from pathlib import Path
import argparse
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles


def add_scout_options(parser, *, cpp_dir):
    parser.add_argument("--material-bridge-timeout", type=int, default=0)
    parser.add_argument("--glass-scout-round-mode", choices=["auto", "fixed"], default="auto")
    parser.add_argument("--glass-scout-fixed-rounds", type=int, default=3)
    parser.add_argument("--glass-scout-population", type=int, default=240)
    parser.add_argument("--glass-scout-generations", type=int, default=200)
    parser.add_argument(
        "--glass-scout-population-scale", choices=["tight", "medium", "wide"], default="medium"
    )
    parser.add_argument("--glass-scout-field-hy-fractions", default="0,0.25,0.5,0.75,1")
    parser.add_argument("--glass-scout-trace-rays", type=int, default=3)
    parser.add_argument("--glass-scout-trace-arms", type=int, default=6)
    parser.add_argument("--glass-scout-trace-distribution", default="gaussian_ring_arm")
    parser.add_argument(
        "--glass-scout-candidates",
        type=int,
        default=14,
        help="Nearest real-glass candidates retained independently per island and lens group.",
    )
    parser.add_argument(
        "--glass-scout-glass-database", default=str(feedback_cycles.DEFAULT_DATABASE)
    )
    parser.add_argument("--glass-scout-mutation-rate", type=float, default=0.3)
    parser.add_argument("--glass-scout-random-rate", type=float, default=0.06)
    parser.add_argument("--glass-scout-pbest-inherit-rate", type=float, default=0.3)
    parser.add_argument("--glass-scout-neighbor-count", type=int, default=5)
    parser.add_argument("--glass-scout-neighbor-nd-scale", type=float, default=0.012)
    parser.add_argument("--glass-scout-neighbor-vd-scale", type=float, default=4.0)
    parser.add_argument("--glass-scout-seed-glass-keep-rate", type=float, default=0.55)
    parser.add_argument("--glass-scout-seed-glass-neighbor-rate", type=float, default=0.35)
    parser.add_argument("--glass-scout-seed", type=int, default=20260726)
    parser.add_argument("--glass-scout-rng-seed", type=int, default=104729)
    parser.add_argument(
        "--glass-scout-min-trace-coverage", type=float, default=DE_DLS_TRACE_COVERAGE_BASELINE
    )
    parser.add_argument(
        "--glass-scout-min-field-coverage", type=float, default=DE_DLS_TRACE_COVERAGE_BASELINE
    )
    parser.add_argument(
        "--material-continuous-dls-guard-enabled",
        action=argparse.BooleanOptionalAction,
        default=True,
    )
    parser.add_argument(
        "--material-continuous-dls-constraint-growth-ratio", type=float, default=4.0
    )
    parser.add_argument(
        "--material-continuous-dls-constraint-growth-absolute", type=float, default=0.01
    )
    parser.add_argument("--glass-scout-timeout", type=int, default=0)
    parser.add_argument(
        "--reuse-glass-choice-scout",
        action="store_true",
        help="Reuse already completed GlassChoice Scout island outputs in out-dir. Every island must contain system.json, score.json, and result.json, and must not contain error.txt.",
    )
    parser.add_argument(
        "--glass-scout-multi-island",
        dest="glass_scout_multi_island_enabled",
        action=argparse.BooleanOptionalAction,
        default=True,
    )
    parser.add_argument(
        "--glass-scout-multi-island-exe",
        type=Path,
        default=cpp_dir / "build" / "lensde_multi_island.exe",
    )
    parser.add_argument("--glass-scout-multi-island-coalesce-us", type=int, default=1000)
