# Refining lens geometry and selecting catalog glass

This package takes the candidate designs and saved run files from global search and runs the two-layer refinement workflow. The first layer continues from checkpoints and can restart geometry optimization. The second optimizes material properties, selects catalog glasses, and refines the lens with those glasses held fixed. It writes the ranked designs, final optical systems and scores, and run settings. [Two-layer workflow entry point](../run_two_layer_basin_refine_funnel.py)

## Run order and shared settings

- Runs the two layers, resumes completed work when eligible, and records the final ranking in `funnel_final_ranked.csv`. [`runner.py`](runner.py)
- Defines the operations passed to the runner and the input data needed for final damped least-squares (DLS) passes. [`contracts.py`](contracts.py)
- Reads and writes run files and resolves common ray sampling and restart settings. [`common.py`](common.py)
- Defines shared scoring tolerances, trace coverage thresholds, and the geometry variables allowed in final DLS. [`settings.py`](settings.py)

## Geometry search and preparation for material optimization

- Builds a three-field reference system and its variable list for geometry refinement. [`reference.py`](reference.py)
- Selects geometry designs for local refinement or material search and filters out candidates with duplicate optical layouts. [`selection.py`](selection.py)
- Runs geometry restart cycles from selected designs and records the results for each starting point. [`restarts.py`](restarts.py)
- Sends selected designs through material preparation, optional chromatic repair, and continuous material optimization before catalog glass selection. [`material.py`](material.py) · [Material preparation](../material_bridge/README.md)
- Determines when a change in fields or sampling requires vignetting recalibration and defines the aperture and trace requirements. [`calibration_policy.py`](calibration_policy.py)
- Recalibrates vignetting and verifies the aperture and ray coverage through independent retraces. [`calibration.py`](calibration.py)

## Catalog glass selection and fixed-glass refinement

- Tests discrete catalog glass assignments and checks the resulting systems before passing them to refinement. [`glass_scout.py`](glass_scout.py)
- Carries forward the edge-thickness constraints, keeps the selected glasses fixed, and chooses the refinement route. [`fixed_glass.py`](fixed_glass.py)
- Resolves the source system, backend command, parent score, and acceptance rules for fixed-glass refinement. [`feedback_contracts.py`](feedback_contracts.py)
- Prepares and runs one fixed-glass refinement case and assesses its result against the parent design. [`feedback_case.py`](feedback_case.py)
- Groups compatible fixed-glass runs for multi-island execution. [`feedback_batch.py`](feedback_batch.py)
- Chooses batch or serial execution and records fixed-glass results in candidate order. [`feedback.py`](feedback.py)

## Final local refinement and vignetting

- Builds the final DLS commands with material variables fixed and defines when a pass can be reused or accepted. [`locked_contracts.py`](locked_contracts.py)
- Cross-checks final DLS results between CPU and CUDA and re-scores saved passes when resuming. [`locked_audit.py`](locked_audit.py)
- Runs DLS passes for each candidate and keeps the preceding design if a pass fails the acceptance checks. [`locked_glass_tail.py`](locked_glass_tail.py)
- Recomputes and scores vignetting for final designs where enabled and writes the resulting optical systems. [`terminal_vignetting.py`](terminal_vignetting.py)

## Command-line options

- Assembles the argument parser for the two-layer workflow. [`cli.py`](cli.py)
- Registers checkpoint selection, continuation, and completed-stage reuse options. [`options_checkpoint.py`](options_checkpoint.py)
- Registers the reference geometry, candidate selection, and restart options. [`options_geometry.py`](options_geometry.py)
- Registers entry selection, candidate repair, and continuous material optimization options. [`options_material.py`](options_material.py)
- Registers catalog glass search budgets, ray sampling, and batch execution options. [`options_scout.py`](options_scout.py)
- Registers fixed-glass refinement budgets, seeds, and acceptance thresholds. [`options_feedback.py`](options_feedback.py)
- Registers stage-boundary and final vignetting options. [`options_boundary.py`](options_boundary.py)
- Registers final DLS schedules, scoring checks, and options for resuming only final refinement. [`options_locked.py`](options_locked.py)
- Registers shared backend, solver, time limit, and worker options. [`options_runtime.py`](options_runtime.py)
