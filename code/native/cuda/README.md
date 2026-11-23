# CUDA population scoring and asphere intersection

This directory evaluates batches of candidate lens designs on the GPU. It applies candidate variables to the input lens, aims and traces pupil rays through the resulting surfaces, and returns a merit score and optical measurements for each population row. [Population evaluation API](../include/optiland/optimizer.hpp)

## Population scoring

- Implements the CUDA population evaluator. It prepares and uploads inputs, runs the aiming, tracing, and reduction kernels, and reads back spot, wavefront, trace coverage, and merit results in the original candidate order. Its implementation headers are organized under `scoring/`. [`de_cuda_scorer.cu`](de_cuda_scorer.cu)

## Asphere intersection

- Builds and refines candidate intersections for conic and even-aspheric surfaces. Under the certified root policy, it verifies the first forward intersection within the allowed surface domain. Its implementation headers are organized under `intersection/`. [`de_cuda_asphere_intersection.cu`](de_cuda_asphere_intersection.cu)

## Shared interfaces and build

- Defines the device surface layout and the intersection function signatures used by both compilation units. [`de_cuda_device_common.cuh`](de_cuda_device_common.cuh)
- Defines the control-flow and return value passed between scoring setup and intersection helper stages. [`phase_control.cuh`](phase_control.cuh)

The native CUDA and multi-island targets compile and link both `.cu` files. The shared headers compile through these two units. [Native source registry](../sources.json)
