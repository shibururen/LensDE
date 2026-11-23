# Browser workbench

This directory provides the LensDE browser interface for manually configuring
optical systems and running ray-tracing and optimization experiments.

## Frontend entry points

- [`index.html`](index.html) defines the panels and toolbar.
  [`styles.css`](styles.css) supplies the layout and display styles.
- [`app.js`](app.js) connects the controls to project state, job actions, and
  results. [`api.js`](api.js) sends requests to the [local job API](../src/lensde/api/README.md).
- [`workbench-view.js`](workbench-view.js) renders the controls and updates
  their visibility for ordinary optimization and initial structure search.

## Trace and optimization

**Run trace** evaluates the current project. **Run optimization** follows
the selected mode, either ordinary DE or DLS, or initial structure search.
The ray aiming panel provides the aiming mode and stop-radius source.

[`plots.js`](plots.js) reads traced rays and draws the per-field spot diagrams
in **Analysis / output**. Each plot shows the red, green, and blue wavelength
bands, an RMS circle centered on the weighted green-band centroid when
available, and a physical scale bar. Select **Shared across fields** or **Fit
each field** to change the display scale. Run a new trace after changing spot
sampling.

## Saved results and scope

Trace and optimization jobs save output files in `code/w/` and job records
and logs in `code/work/jobs/`. These files accumulate on disk. The Workbench
exposes a subset of LensDE's features for experiments, and bugs may remain.
You can also use the CLI for experiments. An AI coding assistant can help
prepare commands and project inputs.

[Workbench guide](../docs/workbench_en.txt) · [Language resources](locales/README.md)
