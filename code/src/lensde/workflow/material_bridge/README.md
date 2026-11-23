# Preparing lens candidates for material optimization

This package takes candidate designs from the geometry search and prepares them for material optimization. It keeps refractive index and Abbe number available as design variables, calibrates vignetting where enabled, and scores candidates under shared optical settings. It repairs or continues selected designs and writes the material-stage entries to `selected_material_entries.csv`. [Material preparation entry point](../run_material5_bridge_selection.py)

## Run order and stage data

- Loads the input candidates, checks for completed results, and runs calibration, repair, continuation, and selection in order. [`runner.py`](runner.py)
- Defines the data passed between steps, including the input files, diagnostic scores, repair results, and selected candidates. [`state.py`](state.py)

## Preparing and calibrating candidates

- Builds the system and variable files around each starting design and maps the saved population to the variables used in continuation. [`inputs.py`](inputs.py)
- Measures vignetting with fixed clear apertures and retraces the calibrated system before passing it to the next stage. [`calibration.py`](calibration.py)
- Defines the field positions, aiming settings, and trace requirements and checks that continuous material variables are retained. [`contracts.py`](contracts.py)
- Defines shared trace coverage thresholds and tolerances for comparing CPU and CUDA results. [`policy.py`](policy.py)

## Scoring and continuing optimization

- Builds backend commands for scoring and continuing differential evolution (DE) with the selected optical settings. [`commands.py`](commands.py)
- Scores starting designs under the same field settings and compares CPU and CUDA merit and coverage. [`scoring.py`](scoring.py)
- Runs backend commands and reads the resulting scores and optimized designs. [`execution.py`](execution.py)
- Groups compatible DE runs for multi-island execution and reconstructs their optimized systems from the returned results. [`broker.py`](broker.py)
- Runs groups of repair or continuation passes and assesses whether repaired designs meet the trace and merit requirements. [`continuation.py`](continuation.py)
- Continues candidates under common scoring settings and extends the search for the designs selected for more iterations. [`deepening.py`](deepening.py)

## Ranking candidates and recording results

- Labels candidates using their trace and score diagnostics and carries those measurements forward for comparison. [`diagnostics.py`](diagnostics.py)
- Groups candidates by optical layout and orders them by merit and potential for further improvement. [`ranking.py`](ranking.py)
- Applies selection limits and checks whether candidates are distinct enough to pass to the material stage. [`selection.py`](selection.py)
- Writes `bridge_report.json` with calibration, repair, continuation, and selection results. [`reporting.py`](reporting.py)

## Saved files and result reuse

- Reuses completed calibration, scoring, or stage outputs when they match the current inputs. [`cache.py`](cache.py)
- Identifies the implementation that produced a saved result so its reuse is checked after source changes. [`identity.py`](identity.py)
- Reads and writes JSON and CSV files and locates the saved inputs for each candidate. [`files.py`](files.py)
- Locates the workflow entry point, backend directory, and source files used to identify the implementation. [`paths.py`](paths.py)
