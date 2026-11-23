# CPU ray tracing and aperture-stop aiming

This directory generates CPU rays from a lens prescription, field, wavelength, and pupil sample. It aims rays at the requested pupil location according to the selected mode, then propagates them through the optical surfaces. Tracing records the final ray state, optical path length, and optional surface-by-surface history. Aiming solves can trace with clipping disabled. Physical tracing applies the requested aperture policy. [Public ray tracing API](../../include/optiland/raytrace.hpp)

## Propagation through optical surfaces

- Evaluates surface sag, normals, and the valid radial domain of the surface profile. [`surface_geometry.cpp`](surface_geometry.cpp)
- Selects the surface intersection routine and applies refraction or reflection at the accepted hit. [`surface_intersection.cpp`](surface_intersection.cpp)
- Propagates the ray in surface order, applies aperture checks, and records optical path length, trace history, and failure state. [`sequential_trace.cpp`](sequential_trace.cpp)
- Prepares a reusable optical layout and uses it to trace batches in input order, with histories for selected rays. [`prepared_trace.cpp`](prepared_trace.cpp)
- Provides plane and sphere intersection routines and aperture checks for prepared traces. [`prepared_geometry_inline.hpp`](prepared_geometry_inline.hpp)
- Records where secondary-wavelength rays cross the aperture envelope. [`spectral_aperture_inline.hpp`](spectral_aperture_inline.hpp)

## Asphere intersection

- Determines the valid search domain for even aspheres using the surface profile and radial slope limits. [`asphere_domain_inline.hpp`](asphere_domain_inline.hpp)
- Generates and checks candidate starting points for difficult asphere intersections. [`asphere_topology_inline.hpp`](asphere_topology_inline.hpp)
- Evaluates the intersection residual and refines trial roots with local iterations or bracketed solves. [`asphere_refinement_inline.hpp`](asphere_refinement_inline.hpp)
- Uses interval bounds to search for and certify the first forward root. [`asphere_certification_inline.hpp`](asphere_certification_inline.hpp)
- Supplies the interval operations used to bound the surface and intersection residual. [`interval_arithmetic_inline.hpp`](interval_arithmetic_inline.hpp)

## Pupil geometry and stop solving

- Computes entrance and exit pupil locations and the aperture-stop radius used to scale normalized pupil coordinates. [`pupil_geometry.cpp`](pupil_geometry.cpp)
- Propagates paraxial rays in both directions and computes mappings between the aperture stop and the first surface. [`paraxial_geometry.cpp`](paraxial_geometry.cpp)
- Generates direct or paraxial rays for finite and infinite object distances and routes requests to the selected aiming mode. [`direct_ray_generation.cpp`](direct_ray_generation.cpp)
- Iterates on the ray launch parameters to hit an aperture-stop target, using paired traces to estimate the Jacobian. [`stop_aiming.cpp`](stop_aiming.cpp)
- Holds the worker state used for the parallel trace in the Jacobian calculation. [`stop_aim_worker.hpp`](stop_aim_worker.hpp)
- Provides field direction, launch geometry, and aperture-scaled finite-difference steps used by the aiming solvers. [`aiming_geometry_inline.hpp`](aiming_geometry_inline.hpp)

## Aiming models and ray generation

- Selects the session aiming strategy for each requested pupil sample or chief ray. [`session_generation.cpp`](session_generation.cpp)
- Uses first-order estimates and a bounded search to solve chief rays for model calibration. [`budget_chief.cpp`](budget_chief.cpp)
- Prepares the on-axis footprint and builds the per-field and wavelength calibrated paraxial models. [`calibrated_model.cpp`](calibrated_model.cpp)
- Uses the calibrated paraxial model to generate rays, then applies the configured stop checks and recovery steps. [`calibrated_generation.cpp`](calibrated_generation.cpp)
- Traces in reverse from the aperture stop to build the mapping used by three-step aiming. [`three_step_reverse_map.cpp`](three_step_reverse_map.cpp)
- Builds the three-step aiming model from center and edge solves for a field and wavelength. [`three_step_model.cpp`](three_step_model.cpp)
- Uses the reverse mapping and fitted model to generate three-step rays and secondary-wavelength recovery rays. [`three_step_generation.cpp`](three_step_generation.cpp)
- Uses the chief ray Jacobian and nearby solved rays as starting points for adaptive pilot aiming. [`adaptive_pilot.cpp`](adaptive_pilot.cpp)
- Implements the adaptation of Optiland robust pupil aiming for centered sequential systems, used as a selected mode or a fallback in some aiming paths. [`robust_aiming.cpp`](robust_aiming.cpp)

The calibrated paraxial and three-step models use the fitting and solver routines in the [aiming calibration directory](calibration/README.md).

## Session reuse and diagnostics

- Initializes aiming state for a lens system and stores successful solutions for reuse by field, wavelength, and pupil sample. [`aiming_session.cpp`](aiming_session.cpp)
- Prepares and caches chief ray and pilot ray data for later requests in the session. [`session_anchor_cache.cpp`](session_anchor_cache.cpp)
- Collects and deduplicates reusable starting points and runs stop solves from those points. [`session_seed_reuse.cpp`](session_seed_reuse.cpp)
- Owns the process-wide asphere root policy and exposes aiming diagnostic resets and snapshots. [`diagnostics.cpp`](diagnostics.cpp)
- Updates the shared aiming counters, residual statistics, and timing measurements. [`diagnostics_inline.hpp`](diagnostics_inline.hpp)
- Records ray failures and warnings for backward propagation or overlapping surfaces. [`trace_diagnostics_inline.hpp`](trace_diagnostics_inline.hpp)
- Runs diagnostic probes for chief ray and calibrated edge aiming and returns the solver residuals and statuses. [`aiming_probes.cpp`](aiming_probes.cpp)
- Records the solver steps in the center and edge aiming diagnostic. [`center_edge_probe.cpp`](center_edge_probe.cpp)
- Declares the types and helpers shared by the ray tracing implementation files. [`detail.hpp`](detail.hpp)
