# C++ and CUDA backends

This directory contains the programs that trace rays and optimize lens designs. They read the optical system, design variables, and merit settings prepared by Python and write ray-trace results, merit scores, and optimized lens designs.

## Build targets

| Target | Executable | Responsibility |
| --- | --- | --- |
| `cpu` | `lensde_cpu.exe` | Traces rays, evaluates the merit function, and runs differential evolution (DE) and damped least squares (DLS) on the CPU. |
| `cuda` | `lensde_de_cuda.exe` | Runs the optimizer with GPU batch scoring, including ray intersections with aspheric surfaces. |
| `broker` | `lensde_multi_island.exe` | Runs material searches across multiple populations and batches their candidate evaluations on the GPU. |

`sources.json` lists the ordered groups of C++ and CUDA source files. Each target selects which groups to compile as host or device code. The build checks for missing, duplicate, and unregistered source files. [`sources.json`](sources.json)

## Source layout

- `include/` defines the optical data types and the interfaces for ray tracing, analysis, and optimization. [Public headers](include/README.md)
- `src/` reads the command-line inputs, implements CPU tracing and optimization, and runs the material-search broker. [C++ implementation](src/README.md)
- `cuda/` evaluates candidate designs in GPU batches and implements ray intersections with aspheric surfaces. [CUDA implementation](cuda/README.md)
- `scripts/` provides the Windows build entry points. [Build scripts](scripts/README.md)

From `code/`, run `python -m lensde build --backend all` to build all three executables under `native/build/`. Run `python scripts/native_sources.py --target cpu` to print the ordered CPU source list. Replace `cpu` with `cuda` or `broker` to inspect those targets. [Installation and build](../README.md)
