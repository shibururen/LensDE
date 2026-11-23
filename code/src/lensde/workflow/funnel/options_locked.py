"""Register final locked-glass DLS schedules, audits and tail-only resume arguments."""
from __future__ import annotations
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from pathlib import Path
import argparse


def add_locked_tail_options(parser, *, cpp_dir):
    parser.add_argument(
        "--final-locked-glass-dls-enabled",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Run the parent-retaining CPU DLS tail after locked-glass pure DE.",
    )
    parser.add_argument("--final-locked-glass-dls-source-count", type=int, default=4)
    parser.add_argument("--final-locked-glass-dls-iter-schedule", default="400,400,400")
    parser.add_argument("--final-locked-glass-dls-lambda-schedule", default="0.01,0.001,0.001")
    parser.add_argument("--final-locked-glass-dls-tol-schedule", default="0.0001,0.00001,0.00001")
    parser.add_argument("--final-locked-glass-dls-max-reject", type=int, default=200)
    parser.add_argument("--final-locked-glass-dls-fd", type=float, default=0.0001)
    parser.add_argument("--final-locked-glass-dls-method", default="trf")
    parser.add_argument("--final-locked-glass-dls-bounds-mode", default="safety")
    parser.add_argument("--final-locked-glass-dls-residual-mode", default="expanded")
    parser.add_argument("--final-locked-glass-dls-scaling", default="legacy_optical")
    parser.add_argument("--final-locked-glass-dls-jobs", type=int, default=2)
    parser.add_argument("--final-locked-glass-dls-child-cpu-workers", type=int, default=0)
    parser.add_argument(
        "--final-locked-glass-dls-min-trace-coverage",
        type=float,
        default=DE_DLS_TRACE_COVERAGE_BASELINE,
    )
    parser.add_argument(
        "--final-locked-glass-dls-min-field-coverage",
        type=float,
        default=DE_DLS_TRACE_COVERAGE_BASELINE,
    )
    parser.add_argument("--final-locked-glass-dls-timeout", type=int, default=0)
    parser.add_argument(
        "--resume-final-locked-glass-dls-from",
        type=Path,
        default=None,
        help="Run only the final locked-glass DLS tail from an existing fixed-glass feedback directory. Exact completed passes are reused only after current CPU and CUDA rescoring agree.",
    )
