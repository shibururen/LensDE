# Benchmark reproduction inputs

This package supplies the inputs for reproducing the ten published benchmarks with the standard LensDE workflow. The CLI and workbench use `BenchmarkCatalog` to load the selected task. [Benchmark index](../../../benchmarks/catalog.json)

For each benchmark, `system_input.json` defines the flat starting lens layout, along with its fields and wavelengths. The loader reads this file together with the optimization variables, merit function, and run options.

`projects.py` uses the same project preparation as a custom run and applies the seed profile for the selected benchmark. [Project preparation](../projects.py) · [Benchmark inputs and commands](../../../benchmarks/README.md)
