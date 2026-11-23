# Continuing lens optimization from saved checkpoints

This package selects candidate lens designs from saved differential evolution (DE) checkpoints and uses their variable values to start a new DE stage. It re-scores the top-ranked reference designs, continues the search from the selected starting points, and writes ranked results for material optimization. The workflow calls this package through `run_checkpoint_de400_layer.run_once()`. [Checkpoint entry point](../run_checkpoint_de400_layer.py)

## Run order and stage data

- Runs the steps in order, from loading saved candidates through selecting starting points, continuing DE, and handing off the results. [`runner.py`](runner.py)
- Defines the records passed between steps, including the search inputs, selected starting points, continuation plan, and handoff rows. [`state.py`](state.py)

## Reading checkpoints and choosing starting designs

- Finds saved runs and reads candidate snapshots, event logs, and basin archives. [`snapshots.py`](snapshots.py)
- Assigns stable candidate IDs and records each selected checkpoint with its top-ranked reference design. [`manifest.py`](manifest.py)
- Normalizes geometry variables and computes distances and optical layout identifiers for comparing designs. [`geometry.py`](geometry.py)
- Chooses top-ranked reference designs and additional checkpoints based on merit and estimated potential for improvement. [`selection.py`](selection.py)
- Selects designs far enough apart to cover different optical layouts. [`diversity.py`](diversity.py)
- Defines shared merit thresholds, distance limits, and numerical tolerances used in selection and scoring. [`policy.py`](policy.py)

## Continuing the search and checking results

- Reconstructs each top-ranked reference from saved variable values and re-scores it with the CPU backend. [`rescoring.py`](rescoring.py)
- Builds the backend commands and variable files needed to start DE from a saved design. [`commands.py`](commands.py)
- Runs and collects results for individual candidates, reusing completed outputs when the required files and generations are present. [`execution.py`](execution.py)
- Checks which continuation runs can share the multi-island backend and collects results in candidate order. [`broker.py`](broker.py)
- Defines trace requirements and checks that reconstructed designs retain the continuous material variables needed for later optimization. [`contracts.py`](contracts.py)

## Handoff and run files

- Checks which continued designs qualify for the next stage, ranks them, and builds the rows passed to material optimization. [`handoff.py`](handoff.py)
- Writes the continuation settings to `checkpoint_de_config.json`, including the chosen starts, budgets, and scoring settings. [`reporting.py`](reporting.py)
- Reads and writes the JSON and CSV records and parses saved candidate values. [`files.py`](files.py)
- Resolves the locations of the checkpoint entry point, native backend directory, and helper modules relative to the checkout. [`paths.py`](paths.py)
