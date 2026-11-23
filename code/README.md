# LensDE source and setup

LensDE runs the ten lens-design benchmarks from saved optical inputs and run options. Python prepares the projects and coordinates the stages. The C++ and CUDA backends trace rays, evaluate candidates, and run the optimization.

## Repository layout

```text
data/                  Published lens prescriptions and two Zemax AGF catalogs
image/                 Paper framework figure
code/
  benchmarks/          Saved optical inputs and run options for ten tasks
  catalogs/            Benchmark AGF catalogs, preparation script, and guide
  src/lensde/          Python command line, project setup, and workflows
  native/              C++ tracing and optimization, CUDA, and material broker
  scripts/             Native build scripts
  web/                 Browser workbench for manual experiments
  docs/                Workbench usage guide
```

The benchmark command reads the saved configuration and prepares the project before handing it to the native backend. [CLI](src/lensde/__main__.py) · [Project preparation](src/lensde/projects.py) · [Native backends](native/README.md)

## Before running your own experiments

- **Back working distance (BFL).** To work around known BFL handling issues, we recommend following the S1–S4 benchmark setups, adding an auxiliary surface behind the last powered lens element, and explicitly setting the BFL reference surface and lower limit. In S1, S22 is the auxiliary surface, and BFL is the axial distance from the reference surface S21 to the image plane. [S1 system input](benchmarks/S1/inputs/system_input.json) · [Benchmark inputs](benchmarks/README.md)
- **Disk space for initial structure search.** We recommend checking available disk space before starting. Both the CLI and the Workbench write many intermediate files under `code/w/` during a search, and some remain after it finishes. The `run_directory` field in `code/work/jobs/<job-id>/job.json` gives the output path relative to `code/`.

## Installation

### Requirements

- **Python package**. Windows x64, PowerShell, and Python 3.11. The reference environment uses Python 3.11.4.
- **Native build**. MinGW with `g++` on `PATH`. The CUDA and material-broker builds also require the NVIDIA CUDA Toolkit with `nvcc` on `PATH` and compatible MSVC x64 C++ tools. An NVIDIA GPU compatible with the build is required to run the CUDA backend.

The reference native build uses MinGW g++ 8.1.0, CUDA Toolkit 11.2.67, and MSVC 14.16, targeting `sm_86`. Use a short path for the source checkout and native run outputs.

### Install the Python package

From the repository root, run these commands in PowerShell with Python 3.11 available as `python`.

```powershell
Set-Location code
python -m venv .venv
$py = '.\.venv\Scripts\python.exe'
& $py -m pip install -r requirements-tested.txt wheel
& $py -m pip install --no-build-isolation -e .
```

The first pip command installs the tested dependencies and build tools. The second installs LensDE in editable mode from this checkout. To confirm that the CLI is available, run `& $py -m lensde --help`.

### Build the native executables

From `code/` in the same PowerShell session, build the three executables used by the benchmark workflows.

```powershell
& $py -m lensde build --backend all
```

The CPU optimizer, CUDA optimizer, material broker, and build records are written to `native/build/`. For a CPU-only build, use `--backend cpu`. [Native build details](native/scripts/README.md)

A clean build of all three targets took about 20 minutes on our reference Windows setup, including compilation and linking. Build time varies with hardware. Later builds reuse compatible objects.

## Material catalogs

Prepare the benchmark material database from the two published Zemax AGF files. From `code/`, run this command once in the installation environment.

```powershell
& $py catalogs/prepare_materials.py
```

The script reads the two AGF files next to it in `code/catalogs/` and creates the local index and optical models in `code/catalogs/database/`. Git ignores the generated database.

- **Reproduce a benchmark.** The preparation script builds the material database used for the benchmark optimizations. [Benchmark material catalogs](catalogs/README.md)
- **Use your own catalog.** In a separate working copy, add the YAML model files under `data-nk/glass/`, register each material in `catalog_nk.csv`, then select the library in the workbench or in a custom project. [Custom catalog guide and example](./catalogs/README.md)

## Run a benchmark

After building the native executables and preparing the benchmark material database, run the saved workflow from `code/`. The command below runs S1.

```powershell
& $py -m lensde run --benchmark S1 --kind workflow
```

Replace `S1` with a task ID from the benchmark guide to run that task's saved workflow. To view S1's settings first, run `& $py -m lensde inspect S1 --language en`. [Saved benchmark inputs](benchmarks/README.md) · [Benchmark material catalogs](catalogs/README.md) · [Published Zemax prescriptions](../data/README.md)

## Workbench experiments

Use the Workbench to configure optical systems by hand and run ray-tracing or optimization experiments. After building the native executables and preparing the material catalogs, start it from `code/`.

```powershell
& $py -m lensde serve --port 8767
```

Open `http://127.0.0.1:8767/`. Select **EN** for the English interface. Under **Files**, choose **S1** and select **Load example**. Inspect or edit the surfaces, variables, fields, and merit settings. Select **Run trace** and view the results under **Analysis / output**.

Each trace or optimization saves output files in `code/w/` and a job record and log in `code/work/jobs/`. These files accumulate on disk. The experimental Workbench exposes a subset of LensDE's features, and bugs may remain. You can also use the CLI for experiments, with an AI coding assistant to help prepare commands or project files. [Workbench guide](docs/workbench_en.txt)
