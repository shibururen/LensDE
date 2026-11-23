"""Register checkpoint continuation and completed-stage reuse arguments in their original order."""
from __future__ import annotations
from pathlib import Path
import argparse
from lensde.workflow import run_checkpoint_de400_layer as checkpoint_de400


def add_checkpoint_options(parser, *, cpp_dir):
    parser.add_argument("--basin-run-dir", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument(
        "--checkpoint-outer-root",
        type=Path,
        default=None,
        help="Root containing completed Outer checkpoint snapshots; enables the production checkpoint route.",
    )
    parser.add_argument(
        "--checkpoint-reuse-run-manifest",
        type=Path,
        default=None,
        help="Replay the archived checkpoint run manifest when compact output cleanup removed the original Outer snapshot streams.",
    )
    parser.add_argument("--checkpoint-nonrank-count", type=int, default=15)
    parser.add_argument(
        "--checkpoint-total-count",
        type=int,
        default=20,
        help="Fixed checkpoint continuation compute budget. Rank1 anchors are selected independently of the number of completed Outers and the remaining seats are filled by checkpoint candidates.",
    )
    parser.add_argument(
        "--checkpoint-merit-relaxation",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="If strict merit admission leaves seats empty, admit candidates up to the relaxed merit cap without changing parameter-distance thresholds.",
    )
    parser.add_argument(
        "--checkpoint-basin-fill",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Allow merit-qualified basin archive rows to fill checkpoint shortages.",
    )
    parser.add_argument("--checkpoint-de-population", type=int, default=100)
    parser.add_argument("--checkpoint-de-generations", type=int, default=400)
    parser.add_argument("--checkpoint-selected-count", type=int, default=7)
    parser.add_argument(
        "--checkpoint-handoff-count",
        type=int,
        default=0,
        help="Select this many checkpoint candidates for the material bridge. Zero passes every viable checkpoint result.",
    )
    parser.add_argument("--checkpoint-handoff-merit-count", type=int, default=7)
    parser.add_argument("--checkpoint-handoff-potential-count", type=int, default=0)
    parser.add_argument("--checkpoint-handoff-diversity-count", type=int, default=0)
    parser.add_argument(
        "--strict-structure-selection-policy",
        choices=checkpoint_de400.strict_topology.DISTANCE_POLICIES,
        default=checkpoint_de400.strict_topology.LEGACY_DISTANCE_POLICY,
        help="Versioned checkpoint/material-entry structural duplicate policy. The legacy default preserves old and manually resumed experiments.",
    )
    parser.add_argument(
        "--checkpoint-population-scale", choices=["tight", "medium", "wide"], default="medium"
    )
    parser.add_argument("--checkpoint-optimizer-seed", type=int, default=20261729)
    parser.add_argument("--checkpoint-population-seed", type=int, default=2026072400)
    parser.add_argument(
        "--checkpoint-de-use-cuda", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument(
        "--checkpoint-de-exe", type=Path, default=cpp_dir / "build" / "lensde_de_cuda.exe"
    )
    parser.add_argument(
        "--checkpoint-multi-island",
        "--checkpoint-multi-island-experimental",
        dest="checkpoint_multi_island_experimental",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Use the E2-validated batched multi-island executable for the checkpoint continuation layer. Enabled by default for N20/P100/G400; incompatible custom contracts safely fall back to serial.",
    )
    parser.add_argument(
        "--checkpoint-multi-island-exe",
        type=Path,
        default=cpp_dir / "build" / "lensde_multi_island.exe",
    )
    parser.add_argument("--checkpoint-multi-island-coalesce-us", type=int, default=1000)
    parser.add_argument("--checkpoint-de-timeout", type=int, default=0)
    parser.add_argument("--reference-dls-run", type=Path, default=None)
    parser.add_argument(
        "--reuse-restart-ranked",
        type=Path,
        default=None,
        help="Skip the potential sweep/restart and reuse a completed restart_latest_ranked.csv.",
    )
    parser.add_argument(
        "--reuse-material-funnel",
        type=Path,
        default=None,
        help="Skip layer 1 and the material funnel, then resume from a completed material funnel_final_ranked.csv.",
    )
