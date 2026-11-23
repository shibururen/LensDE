# Local server for the LensDE workbench

This package serves the LensDE workbench on localhost. It accepts project settings and run requests from the browser and returns prepared projects, merit previews, run status, and output files. It uses the same project preparation and run manager as the command line.

## HTTP routes and project handling

- Serves the browser files, handles JSON requests for projects and material lookup, and routes run control and file downloads. [`server.py`](server.py)
- Imports full projects or optical systems into the shared project format and computes the merit settings that the engine would use. [`service.py`](service.py)

## Loading projects and running calculations

- `GET /api/default` loads the S3 starting template. `GET /api/benchmarks` lists the ten saved benchmarks, and `GET /api/benchmark?id=S3` loads a specific one by ID.
- `POST /api/jobs` starts ray tracing, optimization, vignetting measurement, or the initial structure search. `GET /api/jobs?id=...` returns progress and links to available results. Stop and resume requests act on runs recorded by LensDE. [Run management](../runtime/jobs.py)
- `POST /api/export` returns a download link for project JSON or an optical system in JSON, Zemax ZMX, or CODE V SEQ format.

The workbench verifies the available native trace builds at startup. Ordinary traces check whether the executable or its build record has changed. Restart the workbench after rebuilding to load the new executable. Optimization and benchmark jobs continue to verify their sources and recipes when submitted.
