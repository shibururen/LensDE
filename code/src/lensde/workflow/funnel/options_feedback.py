"""Register fixed-glass feedback, integer seed and acceptance arguments."""
from __future__ import annotations
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .settings import FIXED_GLASS_SEED_POLICY_LEGACY
from .settings import FIXED_GLASS_SEED_POLICY_NUMERIC
from .settings import FIXED_GLASS_SEED_POLICY_STABLE
from pathlib import Path
import argparse
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from lensde.engine import core as server


def add_feedback_options(parser, *, cpp_dir):
    parser.add_argument("--fixed-glass-feedback-enabled", action="store_true")
    parser.add_argument(
        "--fixed-glass-feedback-out-dir",
        type=Path,
        default=None,
        help="Optional shorter output root for fixed-glass feedback repair.",
    )
    parser.add_argument(
        "--fixed-glass-feedback-mode", choices=["cycle", "refine_like"], default="refine_like"
    )
    parser.add_argument("--fixed-glass-feedback-source-count", type=int, default=2)
    parser.add_argument("--fixed-glass-feedback-cycles", type=int, default=2)
    parser.add_argument("--fixed-glass-feedback-elites-per-cycle", type=int, default=2)
    parser.add_argument("--fixed-glass-feedback-algorithms", default="adaptive_jade")
    parser.add_argument("--fixed-glass-feedback-material-modes", default="continuous")
    parser.add_argument("--fixed-glass-feedback-population", type=int, default=240)
    parser.add_argument("--fixed-glass-feedback-trace-rays", type=int, default=6)
    parser.add_argument("--fixed-glass-feedback-trace-arms", type=int, default=8)
    parser.add_argument("--fixed-glass-feedback-trace-distribution", default="gaussian_ring_arm")
    parser.add_argument("--fixed-glass-feedback-field-hy-fractions", default="")
    parser.add_argument("--fixed-glass-feedback-de-use-cuda", action="store_true")
    parser.add_argument(
        "--fixed-glass-feedback-multi-island",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Use the one-process shared CUDA broker for compatible locked-glass pure-DE candidates; incompatible contracts fall back to serial.",
    )
    parser.add_argument("--fixed-glass-feedback-multi-island-coalesce-us", type=int, default=1000)
    parser.add_argument("--fixed-glass-feedback-multi-island-exe", type=Path)
    parser.add_argument("--fixed-glass-feedback-de-workers", type=int, default=1)
    parser.add_argument("--fixed-glass-feedback-de-child-cpu-workers", type=int, default=0)
    parser.add_argument("--fixed-glass-feedback-de-cuda-min-batch", type=int, default=1)
    parser.add_argument("--fixed-glass-feedback-de-cuda-exe", default="")
    parser.add_argument(
        "--fixed-glass-feedback-constraint-handling",
        choices=["feasibility_rules", "scalar_merit"],
        default="feasibility_rules",
    )
    parser.add_argument(
        "--fixed-glass-feedback-population-scale",
        choices=["tight", "medium", "wide"],
        default="medium",
    )
    parser.add_argument("--fixed-glass-feedback-generations", type=int, default=420)
    parser.add_argument("--fixed-glass-feedback-hybrid-parents", type=int, default=10)
    parser.add_argument("--fixed-glass-feedback-polish-candidates", type=int, default=0)
    parser.add_argument("--fixed-glass-feedback-polish-refill-max-extra", type=int, default=0)
    parser.add_argument("--fixed-glass-feedback-refine-outer", type=int, default=80)
    parser.add_argument("--fixed-glass-feedback-refine-de-gen", type=int, default=400)
    parser.add_argument(
        "--fixed-glass-feedback-refine-early-stop",
        action=argparse.BooleanOptionalAction,
        default=True,
    )
    parser.add_argument("--fixed-glass-feedback-refine-early-stop-min-epochs", type=int, default=4)
    parser.add_argument("--fixed-glass-feedback-refine-early-stop-window", type=int, default=3)
    parser.add_argument(
        "--fixed-glass-feedback-refine-early-stop-min-rel-improve", type=float, default=0.01
    )
    parser.add_argument(
        "--fixed-glass-feedback-refine-early-stop-min-abs-improve", type=float, default=0.01
    )
    parser.add_argument(
        "--fixed-glass-feedback-refine-early-stop-min-lpsr-progress", type=float, default=0.35
    )
    parser.add_argument("--fixed-glass-feedback-refine-early-stop-tail-epochs", type=int, default=0)
    parser.add_argument("--fixed-glass-feedback-refine-lpsr-horizon-epochs", type=int, default=24)
    parser.add_argument("--fixed-glass-feedback-local-iter", type=int, default=0)
    parser.add_argument("--fixed-glass-feedback-local-lambda", default="0.03")
    parser.add_argument("--fixed-glass-feedback-local-fd", default="0.0001")
    parser.add_argument("--fixed-glass-feedback-local-tol", default="0.00008")
    parser.add_argument("--fixed-glass-feedback-local-max-reject", type=int, default=100)
    parser.add_argument("--fixed-glass-feedback-method", choices=["trf", "dls"], default="trf")
    parser.add_argument("--fixed-glass-feedback-local-bounds-mode", default="safety")
    parser.add_argument("--fixed-glass-feedback-local-residual-mode", default="expanded")
    parser.add_argument("--fixed-glass-feedback-refresh", default="0.25")
    parser.add_argument("--fixed-glass-feedback-de-variant", default="jade_pbest_archive_lpsr_feas")
    parser.add_argument("--fixed-glass-feedback-de-strategy", default="rand1bin")
    parser.add_argument("--fixed-glass-feedback-seed", type=int, default=20260704)
    parser.add_argument("--fixed-glass-feedback-rng-seed", type=int, default=104729)
    parser.add_argument(
        "--fixed-glass-feedback-seed-policy",
        choices=[
            FIXED_GLASS_SEED_POLICY_STABLE,
            FIXED_GLASS_SEED_POLICY_LEGACY,
            FIXED_GLASS_SEED_POLICY_NUMERIC,
        ],
        default=FIXED_GLASS_SEED_POLICY_NUMERIC,
    )
    parser.add_argument("--master-seed", type=int, default=None)
    parser.add_argument("--seed-profile", default=server.seed_protocol.GENERIC_PROFILE)
    parser.add_argument("--fixed-glass-feedback-timeout", type=int, default=0)
    parser.add_argument(
        "--fixed-glass-feedback-glass-database", default=str(feedback_cycles.DEFAULT_DATABASE)
    )
    parser.add_argument("--fixed-glass-feedback-glass-candidates", type=int, default=14)
    parser.add_argument("--fixed-glass-feedback-glass-mutation-rate", type=float, default=0.3)
    parser.add_argument("--fixed-glass-feedback-glass-random-rate", type=float, default=0.06)
    parser.add_argument("--fixed-glass-feedback-glass-pbest-inherit-rate", type=float, default=0.3)
    parser.add_argument("--fixed-glass-feedback-glass-neighbor-count", type=int, default=5)
    parser.add_argument("--fixed-glass-feedback-glass-neighbor-nd-scale", type=float, default=0.012)
    parser.add_argument("--fixed-glass-feedback-glass-neighbor-vd-scale", type=float, default=4.0)
    parser.add_argument("--fixed-glass-feedback-seed-glass-keep-rate", type=float, default=0.55)
    parser.add_argument("--fixed-glass-feedback-seed-glass-neighbor-rate", type=float, default=0.35)
    parser.add_argument(
        "--fixed-glass-feedback-continuous-glass-neighbor-rate", type=float, default=0.45
    )
    parser.add_argument(
        "--fixed-glass-feedback-continuous-glass-neighbor-topk", type=int, default=8
    )
    parser.add_argument(
        "--fixed-glass-feedback-continuous-glass-neighbor-pick", type=int, default=5
    )
    parser.add_argument(
        "--fixed-glass-feedback-continuous-glass-nd-noise", type=float, default=0.0008
    )
    parser.add_argument(
        "--fixed-glass-feedback-continuous-glass-vd-noise", type=float, default=0.18
    )
    parser.add_argument("--fixed-glass-feedback-edge-weight", type=float, default=-1.0)
    parser.add_argument("--fixed-glass-feedback-min-edge-thickness", type=float, default=-1.0)
    parser.add_argument("--fixed-glass-feedback-spot-weight", type=float, default=3.0)
    parser.add_argument(
        "--fixed-glass-feedback-rms-mode",
        choices=["rms_spot", "rms_wavefront", "rms_spot_wavefront"],
        default="rms_spot",
    )
    parser.add_argument("--fixed-glass-feedback-quality-spot-scale", type=float, default=0.01)
    parser.add_argument("--fixed-glass-feedback-quality-wavefront-scale", type=float, default=0.25)
    parser.add_argument(
        "--fixed-glass-feedback-wavefront-reference",
        choices=["chief_ray", "centroid_sphere"],
        default="centroid_sphere",
    )
    parser.add_argument("--fixed-glass-feedback-distortion-upper", type=float, default=0.03)
    parser.add_argument("--fixed-glass-feedback-sa-step-start", default="0.08")
    parser.add_argument("--fixed-glass-feedback-sa-step-end", default="0.01")
    parser.add_argument("--fixed-glass-feedback-sa-temp-start", default="0.08")
    parser.add_argument("--fixed-glass-feedback-sa-temp-end", default="0.002")
    parser.add_argument("--fixed-glass-feedback-sa-temp-power", default="1.6")
    parser.add_argument("--fixed-glass-feedback-target-mean-spot", type=float, default=0.008)
    parser.add_argument("--fixed-glass-feedback-max-distortion", type=float, default=0.03)
    parser.add_argument("--fixed-glass-feedback-max-constraint-error", type=float, default=2.5)
    parser.add_argument("--fixed-glass-feedback-min-edge", type=float, default=-1.0)
    parser.add_argument(
        "--fixed-glass-feedback-min-trace-coverage",
        type=float,
        default=DE_DLS_TRACE_COVERAGE_BASELINE,
    )
