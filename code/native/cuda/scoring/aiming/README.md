# Calibrated ray aiming for CUDA scoring

This directory uses candidate surfaces, fields, wavelengths, and stop targets
to solve launch seeds for chief rays and pupil-edge rays. It combines those
seeds and the traced stop coordinates into a calibrated model for CUDA ray
generation.

## Chief-ray solves

- [`chief_geometry.cuh`](chief_geometry.cuh) computes paraxial transport to the
  stop and uses traced ray geometry to estimate or shift trial seeds.
- [`chief_optical_context.cuh`](chief_optical_context.cuh) prepares the optical
  context used to estimate chief-ray seeds.
- [`chief_secant_solver.cuh`](chief_secant_solver.cuh) refines seeds against
  the stop target. [`budget_chief.cuh`](budget_chief.cuh) tries the prioritized
  seeds within the chief-ray solve budget.
- [`ordered_chief_tasks.cuh`](ordered_chief_tasks.cuh) prepares the solve
  context and trial seeds for ordered chief-ray tasks.

## Pupil-edge calibration

- [`seed_candidates.cuh`](seed_candidates.cuh) prepares trial seeds within
  the first-surface bounds. [`stop_samples.cuh`](stop_samples.cuh) traces seeds
  to the stop and measures the physical stop radius.
- [`edge_scale_solver.cuh`](edge_scale_solver.cuh) probes edge seeds and
  adjusts their scale toward the stop targets.
- [`edge_candidate_search.cuh`](edge_candidate_search.cuh) tests edge seed
  candidates. [`edge_recovery.cuh`](edge_recovery.cuh) refines or recovers a
  usable anchor when the initial search falls short.
- [`edge_calibration.cuh`](edge_calibration.cuh) coordinates the solves for the
  four pupil-edge anchors.

## Model assembly and reuse

- [`model_chief.cuh`](model_chief.cuh) supplies the chief seed and its traced
  stop coordinates. [`model_builder.cuh`](model_builder.cuh) assembles the
  chief and edge results into a model with residuals and validity flags.
- [`edge_symmetry.cuh`](edge_symmetry.cuh) transforms edge anchors by symmetry,
  and [`vignetted_continuation.cuh`](vignetted_continuation.cuh) transfers edge
  seed estimates between fields with different vignetting.
- [`chromatic_anchor.cuh`](chromatic_anchor.cuh) identifies the center field
  and selects valid same-field models across wavelengths.
  [`secondary_edge_reuse.cuh`](secondary_edge_reuse.cuh) tests edge seeds from
  those models against the current stop targets.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these helpers
and uses the [calibrated model records](../contracts/README.md).
