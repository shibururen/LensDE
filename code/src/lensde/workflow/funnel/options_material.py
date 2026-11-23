"""Register material entry, bridge, repair and continuous-refinement arguments."""
from __future__ import annotations
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from pathlib import Path
import argparse
import math


def add_material_stage_options(parser, *, cpp_dir):
    parser.add_argument("--skip-material-funnel", action="store_true")
    parser.add_argument(
        "--material-terminal-mode",
        choices=["funnel", "direct_glass_de", "scout_lock_fixed_de"],
        default="funnel",
        help="scout_lock_fixed_de runs continuous-material DLS, a short multi-island GlassChoice Scout, locked-glass pure DE, and an independent final DLS tail; direct_glass_de retains the joint mixed search for A/B.",
    )
    parser.add_argument("--material-reference-run", type=Path, default=None)
    parser.add_argument("--material-merit-template", type=Path, default=None)
    parser.add_argument("--material-stages-json", type=Path, default=None)
    parser.add_argument("--material-entry-count", type=int, default=4)
    parser.add_argument("--material-entry-ranks", default="")
    parser.add_argument("--material-entry-merit-count", type=int, default=4)
    parser.add_argument("--material-entry-potential-count", type=int, default=0)
    parser.add_argument("--material-entry-diversity-count", type=int, default=0)
    parser.add_argument("--material-entry-quality-guard-ratio", type=float, default=1.1)
    parser.add_argument(
        "--material-entry-selection-mode", choices=["hybrid", "merit", "potential"], default="merit"
    )
    parser.add_argument("--material-entry-max-final-merit", type=float, default=math.inf)
    parser.add_argument(
        "--material-chromatic-repair-enabled",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Before Material Funnel or direct glass locking, repair chromatic merit while inheriting the complete preceding-stage variable contract.",
    )
    parser.add_argument("--material-chromatic-repair-population", type=int, default=320)
    parser.add_argument("--material-chromatic-repair-generations", type=int, default=1200)
    parser.add_argument(
        "--material-chromatic-repair-population-scale",
        choices=["tight", "medium", "wide"],
        default="wide",
    )
    parser.add_argument("--material-chromatic-repair-seed", type=int, default=20260804)
    parser.add_argument(
        "--material-chromatic-repair-field-hy-fractions", default="0,0.25,0.5,0.75,1"
    )
    parser.add_argument("--material-chromatic-repair-trace-rays", type=int, default=3)
    parser.add_argument("--material-chromatic-repair-trace-arms", type=int, default=6)
    parser.add_argument(
        "--material-chromatic-repair-trace-distribution", default="gaussian_ring_arm"
    )
    parser.add_argument("--material-chromatic-repair-glass-database", default="")
    parser.add_argument("--material-chromatic-repair-glass-neighbor-rate", type=float, default=0.85)
    parser.add_argument("--material-chromatic-repair-glass-neighbor-topk", type=int, default=69)
    parser.add_argument("--material-chromatic-repair-glass-neighbor-pick", type=int, default=69)
    parser.add_argument("--material-chromatic-repair-glass-nd-noise", type=float, default=0.0005)
    parser.add_argument("--material-chromatic-repair-glass-vd-noise", type=float, default=0.12)
    parser.add_argument(
        "--material-bridge-enabled", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument("--material-bridge-field-hy-fractions", default="0,0.25,0.5,0.75,1")
    parser.add_argument("--material-bridge-trace-rays", type=int, default=3)
    parser.add_argument("--material-bridge-trace-arms", type=int, default=6)
    parser.add_argument("--material-bridge-trace-distribution", default="gaussian_ring_arm")
    parser.add_argument("--material-bridge-stable-count", type=int, default=-1)
    parser.add_argument("--material-bridge-repair-count", type=int, default=-1)
    parser.add_argument("--material-bridge-diversity-count", type=int, default=-1)
    parser.add_argument("--material-bridge-max-image-growth", type=float, default=0.15)
    parser.add_argument("--material-bridge-constraint-threshold", type=float, default=2.5)
    parser.add_argument("--material-bridge-constraint-growth-ratio", type=float, default=2.0)
    parser.add_argument("--material-bridge-constraint-growth-absolute", type=float, default=1.0)
    parser.add_argument("--material-bridge-repair-probe-count", type=int, default=4)
    parser.add_argument("--material-bridge-repair-population", type=int, default=240)
    parser.add_argument("--material-bridge-repair-generations", type=int, default=150)
    parser.add_argument("--material-bridge-repair-epoch-generations", type=int, default=50)
    parser.add_argument(
        "--material-bridge-repair-population-scale",
        choices=["tight", "medium", "wide"],
        default="tight",
    )
    parser.add_argument(
        "--material-bridge-repair-constraint-handling",
        choices=["feasibility_rules", "scalar_merit"],
        default="scalar_merit",
    )
    parser.add_argument(
        "--material-bridge-repair-min-constraint-reduction", type=float, default=0.8
    )
    parser.add_argument("--material-bridge-repair-max-image-damage", type=float, default=0.15)
    parser.add_argument("--material-bridge-repair-max-max-image-damage", type=float, default=0.25)
    parser.add_argument(
        "--material-bridge-repair-rescue-max-image-damage", type=float, default=0.35
    )
    parser.add_argument(
        "--material-bridge-repair-rescue-max-max-image-damage", type=float, default=0.5
    )
    parser.add_argument("--material-bridge-repair-min-edge", type=float, default=0.0)
    parser.add_argument(
        "--material-bridge-repair-min-trace-coverage",
        type=float,
        default=DE_DLS_TRACE_COVERAGE_BASELINE,
    )
    parser.add_argument(
        "--material-bridge-deepen-enabled", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument("--material-bridge-deepen-population", type=int, default=400)
    parser.add_argument("--material-bridge-deepen-generations", type=int, default=250)
    parser.add_argument(
        "--material-bridge-deepen-population-scale",
        choices=["tight", "medium", "wide"],
        default="medium",
    )
    parser.add_argument(
        "--material-bridge-deepen-constraint-handling",
        choices=["feasibility_rules", "scalar_merit"],
        default="feasibility_rules",
    )
    parser.add_argument("--material-bridge-extension-count", type=int, default=2)
    parser.add_argument("--material-bridge-extension-target-generations", type=int, default=400)
    parser.add_argument(
        "--material-bridge-de-use-cuda", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument(
        "--material-bridge-de-cuda-exe", type=Path, default=cpp_dir / "build" / "lensde_de_cuda.exe"
    )
    parser.add_argument("--material-bridge-de-cuda-min-batch", type=int, default=1)
    parser.add_argument(
        "--material-bridge-multi-island",
        dest="material_bridge_multi_island_enabled",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Batch independent repair, deepen, and extension candidates within each Material Bridge wave. Enabled by default; incompatible contracts safely fall back to the serial DE executable.",
    )
    parser.add_argument(
        "--material-bridge-multi-island-exe",
        type=Path,
        default=cpp_dir / "build" / "lensde_multi_island.exe",
    )
    parser.add_argument("--material-bridge-multi-island-coalesce-us", type=int, default=1000)
    parser.add_argument("--material-bridge-de-variant", default="jade_pbest_archive_lpsr_feas")
    parser.add_argument("--material-bridge-de-strategy", default="rand1bin")
    parser.add_argument("--material-bridge-seed", type=int, default=20260719)


def add_material_refine_options(parser, *, cpp_dir):
    parser.add_argument(
        "--material-selection-mode", choices=["hybrid", "merit", "potential"], default="hybrid"
    )
    parser.add_argument("--material-keep", type=int, default=8)
    parser.add_argument("--material-max-per-topology", type=int, default=1)
    parser.add_argument("--material-min-field-count", type=int, default=5)
    parser.add_argument("--material-local-iter", type=int, default=400)
    parser.add_argument("--material-local-lambda", type=float, default=0.01)
    parser.add_argument("--material-local-fd", type=float, default=0.0001)
    parser.add_argument("--material-local-tol", type=float, default=0.0001)
    parser.add_argument("--material-local-max-reject", type=int, default=40)
    parser.add_argument("--material-local-plateau-window", type=int, default=50)
    parser.add_argument("--material-local-plateau-min-iterations", type=int, default=80)
    parser.add_argument("--material-local-plateau-min-abs-improvement", type=float, default=0.02)
    parser.add_argument("--material-local-plateau-min-rel-improvement", type=float, default=0.01)
    parser.add_argument(
        "--material-restart-early-stop", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument("--material-restart-early-stop-max-iterations", type=int, default=20)
    parser.add_argument(
        "--material-restart-early-stop-min-abs-improvement", type=float, default=0.002
    )
    parser.add_argument(
        "--material-restart-early-stop-min-rel-improvement", type=float, default=0.001
    )
