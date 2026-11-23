# Diagnostics for CUDA scoring

This directory records asphere intersection and aiming results during CUDA
population scoring. It runs diagnostic comparisons on candidate models and
reports the collected counters, residuals, and timings.

## Device records and comparisons

- [`asphere_profile_records.cuh`](asphere_profile_records.cuh) defines the
  feasibility, deep comparison, and UC5 profile records.
- [`builder_instrumentation.cuh`](builder_instrumentation.cuh) records work
  counts and device cycles for calibrated model construction.
- [`intersection_shadow.cuh`](intersection_shadow.cuh) compares asphere root
  outcomes. [`model_shadow.cuh`](model_shadow.cuh) compares model validity and
  calibrated anchors across aiming paths.

## Calibrated aiming probes

- [`calibrated_probe_records.cuh`](calibrated_probe_records.cuh) defines
  result records and device kernels for stop-trace comparisons.
- [`calibrated_probe_run.cuh`](calibrated_probe_run.cuh) runs and times
  serial and cooperative probes, single- and double-precision stop traces,
  and edge-anchor replay.
- [`chief_seed_probe.cuh`](chief_seed_probe.cuh) compares the first chief-ray
  seed solve with and without convergence tracking.

## Host reports

- [`root_profile_report.cuh`](root_profile_report.cuh) reports asphere
  intersection outcomes and root-solver work.
- [`feasibility_report.cuh`](feasibility_report.cuh) reports feasibility
  counters, recovery outcomes, and timings by scoring phase.
- [`model_builder_report.cuh`](model_builder_report.cuh) writes model
  construction records to CSV and releases their device buffer.
- [`trace_report.cuh`](trace_report.cuh) reports field and ray details for a
  selected candidate. [`uc5_profile_report.cuh`](uc5_profile_report.cuh)
  reports UC5 counters by build type, wavelength role, and edge anchor.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these helpers.
The [host execution path](../host/README.md) manages diagnostic settings,
buffers, and readback within the locked scoring call.
