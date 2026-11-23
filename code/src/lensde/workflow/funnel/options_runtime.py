"""Register shared local-solver, execution and worker arguments."""
from __future__ import annotations
from pathlib import Path


def add_runtime_options(parser, *, cpp_dir):
    parser.add_argument("--local-method", default="trf")
    parser.add_argument(
        "--local-bounds-mode",
        choices=["safety", "safety_material_strict", "variable"],
        default="safety_material_strict",
    )
    parser.add_argument("--local-residual-mode", default="expanded")
    parser.add_argument("--optimizer-internal-scaling", default="edge_sag_normalized")
    parser.add_argument("--timeout", type=float, default=0.0)
    parser.add_argument("--force-rerun", action="store_true")
    parser.add_argument("--exe", type=Path, default=None)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--child-cpu-workers", type=int, default=0)
