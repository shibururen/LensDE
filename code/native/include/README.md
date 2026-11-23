# Public C++ and CUDA headers

This directory contains the C++ headers used by the LensDE native executables. The `optiland/` path follows the C++ namespace carried over from the optical engine prototype. Its types, including `optiland::LensSystem`, are used to load, trace, score, and optimize lens designs. The `lensde/cli/` path follows the `lensde::cli` namespace for native command entry points.

LensDE builds the optical engine from the C++ and CUDA sources in this repository. The CPU implementation of robust pupil aiming adapts the algorithm from the upstream Optiland project. [Optiland MIT license](../../third_party/optiland_LICENSE.txt)

## Optical data and algorithms

- Collects the types for optical systems, surfaces, fields, rays, glasses, design variables, and merit settings. [`optiland/core.hpp`](optiland/core.hpp)
- Collects options, progress and result types, and optimizer interfaces for differential evolution (DE) and damped least squares (DLS). [`optiland/optimizer.hpp`](optiland/optimizer.hpp)
- Points to the individual headers for ray tracing, analysis, lens solves, file I/O, and GPU evaluation. [Optical header index](optiland/README.md)

## Native command entry points

- Declares the functions for running one command or handling repeated commands in a persistent native process. [`lensde/cli/command.hpp`](lensde/cli/command.hpp) · [Application headers](lensde/README.md)
