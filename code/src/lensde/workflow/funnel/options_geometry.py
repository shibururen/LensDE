"""Register reference geometry, sparse selection and restart arguments."""
from __future__ import annotations
from .settings import DEFAULT_MAX_LOCAL_ITER_PER_RESTART
from pathlib import Path
import math


def add_geometry_options(parser, *, cpp_dir):
    parser.add_argument("--geometry-merit-template", type=Path, default=None)
    parser.add_argument("--geometry-kinds", default="curvature,thickness")
    parser.add_argument("--geometry-field-hy-fractions", default="")
    parser.add_argument("--max-basins", type=int, default=48)
    parser.add_argument("--selection-count", type=int, default=12)
    parser.add_argument("--selection-ranks", default="")
    parser.add_argument("--selection-merit-count", type=int, default=4)
    parser.add_argument("--selection-potential-count", type=int, default=6)
    parser.add_argument("--selection-diversity-count", type=int, default=2)
    parser.add_argument("--selection-max-final-merit", type=float, default=math.inf)
    parser.add_argument("--potential-local-iter", type=int, default=400)
    parser.add_argument("--potential-local-lambda", type=float, default=0.01)
    parser.add_argument("--potential-local-fd", type=float, default=0.0001)
    parser.add_argument("--potential-local-tol", type=float, default=1e-06)
    parser.add_argument("--potential-local-max-reject", type=int, default=200)
    parser.add_argument("--restart-max-cycles", type=int, default=4)
    parser.add_argument("--restart-min-cycles", type=int, default=2)
    parser.add_argument("--restart-local-iter-schedule", default="400,400,400,400")
    parser.add_argument(
        "--max-local-iter-per-restart", type=int, default=DEFAULT_MAX_LOCAL_ITER_PER_RESTART
    )
    parser.add_argument("--restart-local-lambda", type=float, default=0.001)
    parser.add_argument("--restart-local-fd", type=float, default=2e-05)
    parser.add_argument("--restart-local-tol", type=float, default=1e-08)
    parser.add_argument("--restart-local-max-reject", type=int, default=500)
    parser.add_argument("--restart-min-delta", type=float, default=0.05)
    parser.add_argument("--restart-min-rel-delta", type=float, default=0.005)
    parser.add_argument("--restart-patience", type=int, default=1)
