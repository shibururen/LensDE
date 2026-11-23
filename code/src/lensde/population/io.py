"""Production CSV serialization with the retained historical .15g precision."""

import csv
from pathlib import Path


def write_population_csv(path: str | Path, rows: list[list[float]], *, overwrite: bool = False) -> None:
    """Write the old population writer's exact number and newline format.

    The destination directory must exist. Exclusive creation is the default;
    replacing a file requires an explicit overwrite=True.
    """
    with Path(path).open("w" if overwrite else "x", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        for row in rows:
            writer.writerow([f"{value:.15g}" for value in row])
