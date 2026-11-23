from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import argparse
import csv
import json
import math
import re
from pathlib import Path
from typing import Any


POLISH_RE = re.compile(r"polish\s+(\d+)\s+best merit\s+([0-9.eE+-]+)")


def finite_float(value: Any, default: float = math.nan) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default


def parse_polish_curve(stdout_path: Path) -> list[tuple[int, float]]:
    if not stdout_path.exists():
        return []
    text = stdout_path.read_text(encoding="utf-8", errors="replace")
    return [(int(match.group(1)), float(match.group(2))) for match in POLISH_RE.finditer(text)]


def value_before(curve: list[tuple[int, float]], last_iter: int, window: int, fallback: float) -> float:
    if not curve:
        return fallback
    cutoff = max(0, last_iter - window)
    values = [value for iteration, value in curve if iteration <= cutoff]
    return values[-1] if values else curve[0][1]


def score_row(row: dict[str, str]) -> dict[str, Any]:
    out_dir = Path(row["out_dir"])
    curve = parse_polish_curve(out_dir / "stdout.log")
    last_iter = curve[-1][0] if curve else 0
    initial = finite_float(row.get("initial_merit"))
    final = finite_float(row.get("final_merit"))
    archive = finite_float(row.get("archive_merit"))
    total = initial - final if math.isfinite(initial) and math.isfinite(final) else math.nan
    archive_total = archive - final if math.isfinite(archive) and math.isfinite(final) else math.nan
    rel = total / max(abs(initial), 1.0e-9) if math.isfinite(total) and math.isfinite(initial) else math.nan
    tail100 = value_before(curve, last_iter, 100, initial) - final if math.isfinite(final) else math.nan
    tail50 = value_before(curve, last_iter, 50, initial) - final if math.isfinite(final) else math.nan
    tail25 = value_before(curve, last_iter, 25, initial) - final if math.isfinite(final) else math.nan

    # Deliberately emphasizes "still moving" behavior over final merit.
    potential_score = (
        math.log1p(max(total, 0.0))
        + 2.0 * math.log1p(max(tail50, 0.0))
        + math.log1p(max(tail25, 0.0))
        + 0.5 * max(rel, 0.0)
    )
    out = dict(row)
    out.update(
        {
            "last_polish_iter": last_iter,
            "total_improve_initial_to_final": total,
            "archive_improve_to_final": archive_total,
            "relative_improve_initial_to_final": rel,
            "tail100_improve": tail100,
            "tail50_improve": tail50,
            "tail25_improve": tail25,
            "potential_score": potential_score,
        }
    )
    return out


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prior-sweep", type=Path, required=True)
    parser.add_argument("--summary-csv", type=Path, default=None)
    parser.add_argument("--out-csv", type=Path, default=None)
    parser.add_argument("--top-n", type=int, default=6)
    parser.add_argument("--max-final-merit", type=float, default=math.inf)
    args = parser.parse_args()

    prior = args.prior_sweep.resolve()
    summary_csv = args.summary_csv.resolve() if args.summary_csv else prior / "dls_potential_summary.csv"
    rows = [score_row(row) for row in read_rows(summary_csv)]
    rows = [
        row
        for row in rows
        if str(row.get("final_trace_ok", "")).lower() == "true"
        and finite_float(row.get("final_merit"), math.inf) <= args.max_final_merit
    ]
    rows.sort(key=lambda row: finite_float(row.get("potential_score"), -math.inf), reverse=True)

    unique: list[dict[str, Any]] = []
    seen: set[str] = set()
    for row in rows:
        key = str(row.get("structural_topology_key") or row.get("basin_rank"))
        if key in seen:
            continue
        seen.add(key)
        unique.append(row)

    out_csv = args.out_csv or (prior / "potential_candidate_ranked.csv")
    write_csv(out_csv, unique)
    print(json.dumps(
        {
            "prior_sweep": str(prior),
            "summary_csv": str(summary_csv),
            "out_csv": str(out_csv),
            "candidate_count": len(unique),
            "selected_ranks": [row.get("basin_rank") for row in unique[: args.top_n]],
        },
        indent=2,
        ensure_ascii=False,
    ))
    for row in unique[: args.top_n]:
        print(
            "rank",
            row.get("basin_rank"),
            "score",
            f"{finite_float(row.get('potential_score')):.6g}",
            "archive",
            row.get("archive_merit"),
            "dls500",
            row.get("final_merit"),
            "total",
            f"{finite_float(row.get('total_improve_initial_to_final')):.6g}",
            "tail50",
            f"{finite_float(row.get('tail50_improve')):.6g}",
            "tail25",
            f"{finite_float(row.get('tail25_improve')):.6g}",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
