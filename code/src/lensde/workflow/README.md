# Continuing and refining lens designs

This package takes the candidate lens designs and stage files produced by the initial structure search and runs the subsequent optimization stages. It selects starting points from saved checkpoints, compares how much candidates improve under local optimization, and passes selected designs through glass selection and final refinement. The stages write updated optical systems, merit scores, and summaries for the workflow shared by the command line and workbench.

## Run setup and workflow entry points

- Runs the stages after global search, including checkpoint continuation, glass selection, fixed-glass refinement, and final vignetting. [`run_two_layer_basin_refine_funnel.py`](run_two_layer_basin_refine_funnel.py) · [Stage implementations](funnel/README.md)
- Writes a new project JSON with a named seed profile, using an existing project or the saved input for a benchmark. [`configure_seed_profile.py`](configure_seed_profile.py)

## Continuing the search and selecting candidates

- Reads saved optimizer checkpoints, selects starting candidates, continues differential evolution, and saves candidates for the next stage. [`run_checkpoint_de400_layer.py`](run_checkpoint_de400_layer.py) · [Checkpoint implementation](checkpoint/README.md)
- Runs a damped least-squares (DLS) pass on representatives from several search basins and records their merit improvements. [`run_basin_dls_potential_sweep.py`](run_basin_dls_potential_sweep.py)
- Ranks the tested candidates by merit improvement, with extra weight on improvement in the last iterations. [`select_basin_potential_candidates.py`](select_basin_potential_candidates.py)
- Continues DLS refinement on selected designs from the earlier sweep and records their new merit scores. [`run_selected_basin_deep_refine.py`](run_selected_basin_deep_refine.py)
- Compares optical layouts and variable values to check whether candidates belong to the same design family and whether they meet the rules for passing to another stage. [`selection.py`](selection.py)

## Material optimization and glass selection

- Calibrates candidates before glass selection, runs repair and continuation, and saves the designs selected for material optimization. [`run_material5_bridge_selection.py`](run_material5_bridge_selection.py) · [Material preparation stages](material_bridge/README.md)
- Corrects chromatic errors while limiting changes to lens geometry before the main material search. [`run_material_chromatic_weak_geometry_stage.py`](run_material_chromatic_weak_geometry_stage.py)
- Refines candidates from differential evolution through local optimization, continuous material adjustment, and catalog glass selection. [`run_post_de_basin_refine_funnel.py`](run_post_de_basin_refine_funnel.py)

The benchmark guide lists the ten tasks and shows how to run their saved inputs. [Benchmark inputs and commands](../../../benchmarks/README.md)
