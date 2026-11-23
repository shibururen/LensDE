# Workflow helpers

This package helps LensDE continue optimization from checkpoints, refine material choices, and transfer aperture and vignetting settings between stages.

- **Prepare local candidates and feedback runs.** `run_explore_dls_feedback_cycles.py` builds local populations and prepares native commands for checkpoint continuation and material refinement. Its helper modules sample within variable bounds, choose nearby glasses from the material catalog, and read or write run data. [Checkpoint continuation](../workflow/checkpoint/README.md) · [Material refinement](../workflow/funnel/README.md)
- **Transfer aperture and vignetting settings.** The material bridge invokes `run_vignetting_aperture_realized_bridge_experiment.py` for vignetted systems. It measures vignetting on the target field grid, retraces the resulting system, and checks trace coverage before the handoff. [Material bridge](../workflow/material_bridge/README.md)
