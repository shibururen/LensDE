"""Build the original ordered native-funnel argument schema without running a stage."""
from __future__ import annotations
from .options_boundary import add_boundary_options
from .options_checkpoint import add_checkpoint_options
from .options_feedback import add_feedback_options
from .options_geometry import add_geometry_options
from .options_locked import add_locked_tail_options
from .options_material import add_material_refine_options
from .options_material import add_material_stage_options
from .options_runtime import add_runtime_options
from .options_scout import add_scout_options
import argparse


def parse_args(*, cpp_dir):
    parser = argparse.ArgumentParser(
        description="Run checkpoint DE continuation followed by the material/glass refinement funnel."
    )
    add_checkpoint_options(parser, cpp_dir=cpp_dir)
    add_geometry_options(parser, cpp_dir=cpp_dir)
    add_material_stage_options(parser, cpp_dir=cpp_dir)
    add_scout_options(parser, cpp_dir=cpp_dir)
    add_material_refine_options(parser, cpp_dir=cpp_dir)
    add_feedback_options(parser, cpp_dir=cpp_dir)
    add_boundary_options(parser, cpp_dir=cpp_dir)
    add_locked_tail_options(parser, cpp_dir=cpp_dir)
    add_runtime_options(parser, cpp_dir=cpp_dir)
    args = parser.parse_args()
    return args
