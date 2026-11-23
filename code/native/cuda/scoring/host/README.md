# Host execution for CUDA scoring

This directory uses the lens system, candidate variables, and merit settings
to run a CUDA population evaluation. It prepares and uploads inputs, launches
the scoring kernels, and reads back candidate summaries and merit values.

## Entry points and call state

- [`api_entrypoints.cuh`](api_entrypoints.cuh) routes score and merit calls
  through the same mutex, held for the full evaluation.
- [`backend_selection.cuh`](backend_selection.cuh) checks whether a scoring
  mode is enabled by the build and runtime options.
- [`execution_context.cuh`](execution_context.cuh) holds input references and
  call-owned state. [`execution_validate_inputs.cuh`](execution_validate_inputs.cuh)
  checks population dimensions and support for the requested merit settings.
- [`certificate_context.cuh`](certificate_context.cuh) holds thread-local
  requests and defines the shared device symbols used for certificate checks.

## Setup, upload, and kernel launches

- [`execution_prepare_host_configuration.cuh`](execution_prepare_host_configuration.cuh)
  prepares the scoring configuration and packs the host inputs.
  [`execution_prepare_device_context.cuh`](execution_prepare_device_context.cuh)
  computes buffer sizes and initializes device-side settings.
- [`execution_allocate_device_buffers.cuh`](execution_allocate_device_buffers.cuh)
  allocates device buffers. [`execution_upload_inputs.cuh`](execution_upload_inputs.cuh)
  passes them to [`upload.cuh`](upload.cuh) to copy inputs and initialize scratch
  buffers.
- [`execution_launch_kernels.cuh`](execution_launch_kernels.cuh) calls
  [`kernel_pipeline.cuh`](kernel_pipeline.cuh), which schedules geometry, aiming,
  aperture, ray tracing, and merit kernels.

## Results and cleanup

- [`execution_readback_results.cuh`](execution_readback_results.cuh) copies
  scores and requested diagnostics back to the host.
  [`execution_report_diagnostics.cuh`](execution_report_diagnostics.cuh)
  dispatches the configured reports.
- [`execution_callbacks.cuh`](execution_callbacks.cuh) records the first CUDA
  error passed to its status callback.
- [`execution_release_device_resources.cuh`](execution_release_device_resources.cuh)
  frees device buffers and clears shared device pointers.
  [`execution_finish_call.cuh`](execution_finish_call.cuh) closes the timing
  events and publishes scores after a successful call.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these helpers.
[`execution_run.cuh`](execution_run.cuh) defines the order of the call phases.
