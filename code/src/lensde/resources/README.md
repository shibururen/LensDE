# Project and material paths

This package finds the LensDE project root, resolves material file references, and checks paths used by the native runner.

- **Locate the project.** `discover_project_root()` searches upward for LensDE's `pyproject.toml`, which marks the `code/` directory. The benchmark loader uses this root to find the saved inputs. [Path implementation](paths.py) · [Benchmark loader](../config/catalog.py)
- **Resolve material files.** `ProjectPaths` locates `catalog_nk.csv` and the material models under `code/catalogs/database/`. It resolves the material `source_path` values in a saved project for use during project preparation. For an archived path that includes `database/`, it uses the portion after that directory to find the corresponding local file. [Material database setup](../../../catalogs/README.md)
- **Check native paths.** The native runner uses `confined()` to keep build and output directories within the project and each run's output within the configured output directory. [Native runner](../runtime/native.py)
