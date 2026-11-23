# CPU aperture-stop aiming calibration

This directory provides the C++ helpers that the CPU ray tracer uses to build calibrated paraxial and three-step aiming models. For a given field and wavelength, these models use solved chief and edge rays to estimate where a ray must pass through the first lens surface to hit the requested point on the aperture stop. The aiming session reuses the model when generating rays for other pupil samples. [CPU ray tracing](../README.md)

## Chief-ray search

The chief-ray solver tries a bounded set of initial first-surface points and returns a chief-ray solution for model calibration. [Chief-ray solver](../budget_chief.cpp)

- Reuses chief-ray solutions from other wavelengths for the same field. [`budget_chief_append_same_field_cached_chief_seeds.hpp`](budget_chief_append_same_field_cached_chief_seeds.hpp)
- Reuses and extrapolates chief-ray solutions from nearby field points to start a solve at the requested field. [`budget_chief_append_field_continuation_seeds.hpp`](budget_chief_append_field_continuation_seeds.hpp)
- Uses solved rays at nearby field angles to estimate starting points across a gap in the sampled angles. [`budget_chief_append_angle_bridge_seeds.hpp`](budget_chief_append_angle_bridge_seeds.hpp)
- Applies fixed-Jacobian or secant updates to reduce the chief ray's error at the stop. [`budget_chief_solve_budget_chief_fixed_or_secant.hpp`](budget_chief_solve_budget_chief_fixed_or_secant.hpp)
- Tries a selected range of candidate starting points and retains the best eligible chief-ray solution. [`budget_chief_try_trial_seed_range_for_field.hpp`](budget_chief_try_trial_seed_range_for_field.hpp)

## Calibrated paraxial aiming

The model fits or interpolates a mapping between first-surface positions and stop coordinates. The following files supply the samples and apply the acceptance criteria. [Calibrated paraxial model](../calibrated_model.cpp)

- Checks a chief ray at the stop and records its first-surface point when accepted. [`calibration_accept_chief_ray.hpp`](calibration_accept_chief_ray.hpp)
- Fits a local mapping from the chief ray and a grid of traced pupil samples. [`calibration_try_grid_fit.hpp`](calibration_try_grid_fit.hpp)
- Retrieves a cached edge starting point from another wavelength for the same field. [`calibration_cached_same_field_edge_seed.hpp`](calibration_cached_same_field_edge_seed.hpp)
- Estimates a fallback edge starting point using a cached footprint or a shifted chief-ray point. [`calibration_fallback_edge_seed.hpp`](calibration_fallback_edge_seed.hpp)
- Traces a scaled edge trial and measures its error at the stop. [`calibration_probe_scale.hpp`](calibration_probe_scale.hpp)
- Tests scaled edge starting points to obtain an acceptable stop sample. [`calibration_try_scaled_edge_anchor.hpp`](calibration_try_scaled_edge_anchor.hpp)
- Corrects a trial edge ray with secant updates to its first-surface point. [`calibration_solve_secant_edge.hpp`](calibration_solve_secant_edge.hpp)
- Refines an edge ray in first-surface coordinates with the configured solver steps. [`calibration_solve_seed_space_edge.hpp`](calibration_solve_seed_space_edge.hpp)
- Checks the edge sample's stop error and decides whether to record or defer the sample. [`calibration_accept_edge_stop_sample.hpp`](calibration_accept_edge_stop_sample.hpp)
- Generates edge corrections for a calibrated paraxial fit using the selected edge solver. [`calibration_build_edge_calibration.hpp`](calibration_build_edge_calibration.hpp)
- Solves the edge samples needed to build the pupil interpolation. [`calibration_sweep_interpolation_edges.hpp`](calibration_sweep_interpolation_edges.hpp)
- Builds the interpolation model from the chief and edge samples and records its validity. [`build_calibrated_interpolation.hpp`](build_calibrated_interpolation.hpp)

## Three-step aiming

The three-step model uses a central fit and edge corrections with the reverse map from the aperture stop to the first surface. [Three-step model](../three_step_model.cpp)

- Fits the central pupil mapping from traced samples near the chief ray. [`three_step_build_central_fit.hpp`](three_step_build_central_fit.hpp)
- Coordinates the edge solve, from choosing a starting point to accepting the solved stop sample. [`three_step_solve_edge.hpp`](three_step_solve_edge.hpp)
- Refines the first-surface point for a three-step edge ray using the selected correction method. [`three_step_solve_seed_space_edge.hpp`](three_step_solve_seed_space_edge.hpp)
- Tries scaled starting points when the initial three-step edge estimate needs adjustment. [`three_step_try_scaled_edge_anchor.hpp`](three_step_try_scaled_edge_anchor.hpp)

## Asphere intersection

- Finds and checks the first forward intersection with an even asphere under the unified asphere root policy. The surface intersection routine uses this helper during tracing. [`intersect_unified_surface.hpp`](intersect_unified_surface.hpp) · [Surface intersection](../surface_intersection.cpp)
