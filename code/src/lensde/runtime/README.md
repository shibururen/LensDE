# How LensDE runs ray tracing and optimization

This package starts the programs that trace rays, measure vignetting, and optimize lens designs. The CLI and workbench both use it to track progress, save results, and stop or resume a run.

- **Start the backend programs.** `native.py` checks that an executable matches the source files in this checkout before launching it. `native_sources.py` reads the list of files to compile, and `native_toolchain.py` supplies the CPU linker settings. [Native runner](native.py) · [Source registry](native_sources.py)
- **Reuse the verified trace build.** The workbench checks its native trace executables at startup. A trace checks the executable and build record for replacement before using that build. Restart the workbench after rebuilding. [Workbench job manager](jobs.py)
- **Track and stop a run.** `jobs.py` records each run's status, logs, results, and output directory. `processes.py` and `concurrency.py` track its child processes, including those started by parallel workers. A stop request targets those processes. [Run tracking](jobs.py) · [Process control](processes.py)
- **Resolve paths and detect source changes.** `native_io_paths.py` maps command and output paths to fit Windows path limits. `source_guard.py` detects changes to source code or saved inputs and asks the user to restart the workbench before starting another optimization or benchmark workflow. [Native paths](native_io_paths.py) · [Source guard](source_guard.py)

When a workflow resumes, completed stages are reused after their inputs are checked. Unfinished stages restart from their saved inputs.
