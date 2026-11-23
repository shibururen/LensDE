# Windows native build scripts

This directory provides PowerShell entry points for the CPU, CUDA DE, and
multi-island executables. The scripts use the [native source registry](../sources.json)
and write their outputs under `native/build/`.

## Build entry points

- [`build_cpu.ps1`](build_cpu.ps1) builds the CPU executable with MinGW
  `g++`. Its default output name is `optiland_cpu.exe`.
- [`build_cuda_de.ps1`](build_cuda_de.ps1) builds `optiland_de_cuda.exe` with
  MSVC and NVCC. It also builds the multi-island companion by default.
  Use `-ProductionOnly` to build just the CUDA DE executable, or `-HostOnly`
  to prepare host objects. Set `-CudaArch` or `OPTILAND_CUDA_ARCH` when the
  script cannot detect the GPU architecture.
- [`build_multi_island.ps1`](build_multi_island.ps1) uses the same CUDA
  build path to produce `optiland_multi_island_experiment.exe`.

## Project build

From `code/`, run `python -m lensde build --backend all` to build
`lensde_cpu.exe`, `lensde_de_cuda.exe`, and `lensde_multi_island.exe` with
source and executable receipts. Use `cpu`, `cuda`, or `broker` to build one
target. [`build.py`](../../scripts/build.py)

The [build environment](../../README.md) lists the compiler requirements.
