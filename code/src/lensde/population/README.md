# Local population for ordinary DE

The workbench uses these modules to generate local variants of the current optical prescription when **Local variants of current prescription** is selected. The backend keeps the current prescription in the first row and reads the remaining variants from a CSV file.

- Generates local population rows within the variable bounds. [Local population](local.py) · [Sampling](sampling.py)
- Writes the population CSV consumed by the native optimizer. [CSV writer](io.py)

[Ordinary DE entry point](../engine/ordinary_de.py) · [Benchmark seed population](../engine/seed_population/README.md)
