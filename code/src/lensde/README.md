# LensDE Python package

The Python package loads benchmark configurations, prepares populations, and serves the browser workbench. It starts the backend programs for ray tracing and optimization, tracks job status, and collects results. The C++ and CUDA backends perform the optical evaluation and optimizer inner loops. [Native backends](../../native/README.md)

## Locate an implementation

- `config/` reads and validates the ten saved benchmark configurations. `resources/` resolves checkout-local paths. [Configuration](config/README.md) · [Resources](resources/README.md)
- `seeds/` supplies the benchmark seed profiles. `engine/seed_population/` builds and filters the initial populations used by the workflow. [Seeds](seeds/README.md) · [Seed population preparation](engine/seed_population/README.md)
- `engine/` turns an optical project into populations, native commands, and results. `workflow/` implements checkpoint continuation, material assignment, and later refinement. [Engine](engine/README.md) · [Workflow modules](workflow/README.md)
- `optics/` and `helpers/` support aperture and illumination handoffs and other shared workflow operations. [Optical handoffs](optics/README.md) · [Helpers](helpers/README.md)
- `runtime/` manages processes, build receipts, and job state. `api/` exposes these services over the local HTTP interface. [Runtime](runtime/README.md) · [HTTP API](api/README.md)

Follow `python -m lensde run --benchmark S1 --kind workflow` from the CLI into project preparation and `JobManager`. The browser workbench uses the same job manager. [CLI](__main__.py) · [Project preparation](projects.py) · [Job manager](runtime/jobs.py)
