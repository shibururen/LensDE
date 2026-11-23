# Benchmark inputs

This directory contains the saved inputs and seed settings for reproducing
the ten LensDE benchmarks. The tasks are S1, S2, S3, S4, 6A, A1, A2, A3, A4,
and DF1.

## Directory contents

- Each task directory contains the starting optical system, variables, and
  merit settings under `inputs/`, along with saved run options and seed data.
- [`catalog.json`](catalog.json) lists the tasks and their relative catalog
  paths. LensDE uses this index to list and load benchmarks in the CLI and
  the Workbench.

## Run a benchmark

Prepare the material catalogs and build the native executables, then run the
saved workflow from `code/`. The command below runs S1.

```powershell
& .\.venv\Scripts\python.exe -m lensde run --benchmark S1 --kind workflow
```

Replace `S1` with another task ID to run that benchmark. LensDE applies the
saved configuration and seed profile automatically.

[Setup](../README.md) · [Published Zemax prescriptions](../../data/README.md)
