# Random seeds for optimization

This package sets the random seeds used by LensDE's optimization stages in benchmark runs and workbench projects.

- **Apply a benchmark seed profile.** `projects.py` calls `configure_seed_only()` to set the master seed and stage seeds in the project options. The ten reference profiles are stored in `profiles/`. The `python -m lensde.seeds` command can also write a copy of a project with a selected profile. [Project preparation](../projects.py) · [Benchmark inputs](../../../benchmarks/README.md)
- **Resolve stage and candidate seeds.** `protocol.py` uses the profile's recorded integers for workflow stages, fixed-glass candidates, and asphere refinement. `legacy.py` supports older fixed-glass seed policies used by existing projects.
