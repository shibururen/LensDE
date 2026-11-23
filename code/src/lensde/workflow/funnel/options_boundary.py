"""Register stage-boundary and terminal vignetting contracts."""
from __future__ import annotations
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .settings import STAGE_BOUNDARY_COVERAGE_ABSOLUTE_TOLERANCE
from .settings import STAGE_BOUNDARY_MODE_APERTURE_RELATIVE_TOLERANCE
from .settings import STAGE_BOUNDARY_SOURCE_APERTURE_RELATIVE_TOLERANCE
import argparse


def add_boundary_options(parser, *, cpp_dir):
    parser.add_argument(
        "--fixed-glass-feedback-min-field-coverage",
        type=float,
        default=DE_DLS_TRACE_COVERAGE_BASELINE,
    )
    parser.add_argument(
        "--stage-boundary-vignetting-recalibration",
        dest="stage_boundary_vignetting_recalibration_enabled",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Only with a positive minimum-relative-illumination objective, at each changed field/sampling handoff calibrate the fixed pupil with strict APG, then require APG/CP aperture and coverage parity after aperture-updating retraces.",
    )
    parser.add_argument(
        "--stage-boundary-source-aperture-relative-tolerance",
        type=float,
        default=STAGE_BOUNDARY_SOURCE_APERTURE_RELATIVE_TOLERANCE,
        help="Diagnostic threshold for clear-aperture change from the incoming system; it is reported but does not reject a bridge.",
    )
    parser.add_argument(
        "--stage-boundary-mode-aperture-relative-tolerance",
        type=float,
        default=STAGE_BOUNDARY_MODE_APERTURE_RELATIVE_TOLERANCE,
        help="Maximum APG/CP clear-aperture relative disagreement.",
    )
    parser.add_argument(
        "--stage-boundary-coverage-absolute-tolerance",
        type=float,
        default=STAGE_BOUNDARY_COVERAGE_ABSOLUTE_TOLERANCE,
        help="Maximum APG/CP trace and minimum-field coverage disagreement.",
    )
    parser.add_argument(
        "--terminal-zemax-vignetting-finalization",
        dest="terminal_zemax_vignetting_finalization_enabled",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Only with a positive minimum-relative-illumination objective, after final geometry/asphere optimization publish a terminal strict-APG Set Vignetting system without changing candidate order.",
    )
