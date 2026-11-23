#!/usr/bin/env python3
"""Prepare the LensDE benchmark material database from the published AGF files."""

from __future__ import annotations

import argparse
import csv
import math
import re
from dataclasses import dataclass, field
from pathlib import Path
from tempfile import TemporaryDirectory


CSV_HEADER = [
    "group",
    "category_name",
    "category_name_full",
    "reference",
    "name",
    "filename",
    "min_wavelength",
    "max_wavelength",
    "filename_no_ext",
]


@dataclass
class AgfGlass:
    name: str
    formula: int
    nd: float
    vd: float
    comments: list[str] = field(default_factory=list)
    coefficients: list[float] = field(default_factory=list)
    wavelength_min: float | None = None
    wavelength_max: float | None = None


def read_agf_text(path: Path) -> str:
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig")
    for encoding in ("utf-8", "cp1252", "latin-1"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def parse_float(token: str) -> float:
    return float(token.replace("D", "E").replace("d", "e"))


def parse_agf(path: Path) -> list[AgfGlass]:
    glasses: list[AgfGlass] = []
    current: AgfGlass | None = None
    for raw_line in read_agf_text(path).splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split()
        tag = parts[0].upper()
        if tag == "NM":
            if current is not None:
                glasses.append(current)
            if len(parts) < 6:
                raise ValueError(f"Malformed NM line: {line}")
            current = AgfGlass(
                name=parts[1],
                formula=int(parse_float(parts[2])),
                nd=parse_float(parts[4]),
                vd=parse_float(parts[5]),
            )
        elif current is None:
            continue
        elif tag == "GC":
            comment = line[2:].strip()
            if comment:
                current.comments.append(comment)
        elif tag == "CD":
            current.coefficients = [parse_float(item) for item in parts[1:]]
        elif tag == "LD" and len(parts) >= 3:
            current.wavelength_min = parse_float(parts[1])
            current.wavelength_max = parse_float(parts[2])
    if current is not None:
        glasses.append(current)
    return glasses


def zemax_formula_1_index(glass: AgfGlass, wavelength_um: float) -> float:
    """Zemax Schott polynomial: n^2 = c0 + c1*l^2 + c2*l^-2 + ..."""
    if not glass.coefficients:
        return glass.nd
    n2 = glass.coefficients[0]
    if len(glass.coefficients) > 1:
        n2 += glass.coefficients[1] * wavelength_um * wavelength_um
    for idx in range(2, len(glass.coefficients)):
        coeff = glass.coefficients[idx]
        if abs(coeff) < 1.0e-30:
            continue
        exponent = -2.0 * (idx - 1)
        n2 += coeff * (wavelength_um ** exponent)
    if n2 <= 0.0 or not math.isfinite(n2):
        raise ValueError(f"{glass.name}: non-positive n^2 at {wavelength_um:g} um")
    return math.sqrt(n2)


def zemax_formula_2_index(glass: AgfGlass, wavelength_um: float) -> float:
    """Zemax Sellmeier form, matching refractiveindex.info formula 2."""
    n2 = 1.0
    coeffs = glass.coefficients
    for idx in range(0, len(coeffs) - 1, 2):
        b = coeffs[idx]
        c = coeffs[idx + 1]
        if abs(b) < 1.0e-30 and abs(c) < 1.0e-30:
            continue
        denom = wavelength_um * wavelength_um - c
        if abs(denom) < 1.0e-30:
            raise ValueError(f"{glass.name}: Sellmeier pole at {wavelength_um:g} um")
        n2 += b * wavelength_um * wavelength_um / denom
    if n2 <= 0.0 or not math.isfinite(n2):
        raise ValueError(f"{glass.name}: non-positive n^2 at {wavelength_um:g} um")
    return math.sqrt(n2)


def zemax_formula_5_index(glass: AgfGlass, wavelength_um: float) -> float:
    """Zemax Conrady form: n = n0 + a/lambda + b/lambda**3.5."""
    coeffs = glass.coefficients
    if len(coeffs) < 3:
        raise ValueError(f"{glass.name}: Conrady formula requires three coefficients")
    n = coeffs[0] + coeffs[1] / wavelength_um + coeffs[2] / wavelength_um**3.5
    if n <= 0.0 or not math.isfinite(n):
        raise ValueError(f"{glass.name}: non-positive Conrady index at {wavelength_um:g} um")
    return n


def sampled_wavelengths(lo: float, hi: float, step: float, max_samples: int) -> list[float]:
    if not (math.isfinite(lo) and math.isfinite(hi)) or lo <= 0.0 or hi <= lo:
        lo, hi = 0.365, 1.014
    count = max(2, int(math.ceil((hi - lo) / max(step, 1.0e-6))) + 1)
    count = min(max_samples, count)
    return [lo + (hi - lo) * idx / (count - 1) for idx in range(count)]


def sampled_formula_1_table(
    glass: AgfGlass,
    lo: float,
    hi: float,
    step: float,
    max_samples: int,
) -> list[tuple[float, float]]:
    """Return the continuous valid Schott interval that contains the d line.

    Some legacy AGF records declare a wavelength endpoint at which their own
    Schott polynomial yields a non-positive n^2.  Such points cannot be safely
    represented in Optiland's tabulated-index YAML.  Prefer the valid run that
    contains the Fraunhofer d line; otherwise use the longest valid run.
    """
    runs: list[list[tuple[float, float]]] = []
    current: list[tuple[float, float]] = []
    for wave in sampled_wavelengths(lo, hi, step, max_samples):
        try:
            index = zemax_formula_1_index(glass, wave)
        except ValueError:
            if current:
                runs.append(current)
                current = []
            continue
        current.append((wave, index))
    if current:
        runs.append(current)
    runs = [run for run in runs if len(run) >= 2]
    if not runs:
        raise ValueError(f"{glass.name}: no continuous valid Schott interval")
    d_line = 0.5875618
    containing_d = [run for run in runs if run[0][0] <= d_line <= run[-1][0]]
    return max(containing_d or runs, key=len)


def yaml_number(value: float) -> str:
    return f"{value:.12g}"


def yaml_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def safe_filename(name: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._+-]+", "_", name).strip("._")
    return cleaned or "glass"


def ri_formula_2_coefficients(glass: AgfGlass) -> list[float]:
    out = [0.0]
    for idx in range(0, len(glass.coefficients) - 1, 2):
        b = glass.coefficients[idx]
        c = glass.coefficients[idx + 1]
        if abs(b) < 1.0e-30 and abs(c) < 1.0e-30:
            continue
        out.extend([b, c])
    return out


def ri_formula_5_coefficients(glass: AgfGlass) -> list[float]:
    """Map Zemax Conrady coefficients to refractiveindex.info Cauchy form."""
    if len(glass.coefficients) < 3:
        raise ValueError(f"{glass.name}: Conrady formula requires three coefficients")
    n0, a, b = glass.coefficients[:3]
    return [n0, a, -1.0, b, -3.5]


def plastic_yaml(
    glass: AgfGlass,
    agf_path: Path,
    library: str,
    sample_step: float,
    max_samples: int,
) -> tuple[str, float, float]:
    declared_lo = glass.wavelength_min or 0.365
    declared_hi = glass.wavelength_max or 1.014
    lo = declared_lo
    hi = declared_hi
    comments = " ".join(glass.comments).strip()
    if not comments:
        comments = f"Converted from {agf_path.name}"
    lines = [
        f"# Converted from Zemax AGF catalog {agf_path.name}",
        f'REFERENCES: {yaml_string(f"Zemax AGF catalog {library}: {agf_path.name}")}',
        f"COMMENTS: {yaml_string(comments)}",
        "DATA:",
    ]
    if glass.formula == 2:
        coeffs = " ".join(yaml_number(value) for value in ri_formula_2_coefficients(glass))
        lines.extend(
            [
                "  - type: formula 2",
                f"    wavelength_range: {yaml_number(lo)} {yaml_number(hi)}",
                f"    coefficients: {coeffs}",
            ]
        )
    elif glass.formula == 1:
        table = sampled_formula_1_table(
            glass,
            declared_lo,
            declared_hi,
            sample_step,
            max_samples,
        )
        lo = table[0][0]
        hi = table[-1][0]
        if lo != declared_lo or hi != declared_hi:
            lines[2] = (
                lines[2][:-1]
                + f"; declared wavelength range {declared_lo:.12g}-{declared_hi:.12g} um "
                + f"trimmed to continuous valid Schott range {lo:.12g}-{hi:.12g} um\""
            )
        lines.extend(
            [
                "  - type: tabulated n",
                "    data: |",
            ]
        )
        for wave, index in table:
            lines.append(f"        {wave:.9f} {index:.12g}")
    elif glass.formula == 5:
        coeffs = " ".join(yaml_number(value) for value in ri_formula_5_coefficients(glass))
        lines.extend(
            [
                "  - type: formula 5",
                f"    wavelength_range: {yaml_number(lo)} {yaml_number(hi)}",
                f"    coefficients: {coeffs}",
            ]
        )
    else:
        raise ValueError(f"{glass.name}: unsupported Zemax formula {glass.formula}")

    lines.extend(
        [
            "SPECS:",
            "    n_is_absolute: false",
            "    wavelength_is_vacuum: false",
            f"    nd: {yaml_number(glass.nd)}",
            f"    Vd: {yaml_number(glass.vd)}",
        ]
    )
    return "\n".join(lines) + "\n", lo, hi


def cdgm_yaml(glass: AgfGlass) -> str:
    """Convert the historical AGF record to the engine's optical model."""
    if glass.wavelength_min is None or glass.wavelength_max is None:
        raise ValueError(f"{glass.name}: missing historical wavelength range")
    if len(glass.coefficients) < 6:
        raise ValueError(f"{glass.name}: incomplete historical dispersion coefficients")
    if glass.formula == 1:
        c0, c1, c2, c3, c4, c5 = glass.coefficients[:6]
        model, coefficients = 3, (c0, c1, 2, c2, -2, c3, -4, c4, -6, c5, -8)
    elif glass.formula == 2:
        model, coefficients = 2, (0, *glass.coefficients[:6])
    else:
        raise ValueError(f"{glass.name}: unsupported historical CDGM formula {glass.formula}")
    return (
        "DATA:\n"
        f"  - type: formula {model}\n"
        f"    wavelength_range: {glass.wavelength_min!r} {glass.wavelength_max!r}\n"
        f"    coefficients: {' '.join(map(str, coefficients))}\n"
        "SPECS:\n"
        f"    nd: {glass.nd!r}\n"
        f"    Vd: {glass.vd!r}\n"
    )


def prepare_database(catalog_dir: Path, destination: Path) -> tuple[int, int]:
    """Create the benchmark database from the two published AGF catalogs."""
    destination = destination.resolve()
    if destination.exists():
        raise FileExistsError(f"Database already exists: {destination}. Use a fresh --database-root.")

    cdgm = []
    ordered_rows: dict[int, list[str]] = {}
    for glass in parse_agf(catalog_dir / "CDGM.AGF"):
        filenames = [comment.removeprefix("LENSDE_FILE ") for comment in glass.comments
                     if comment.startswith("LENSDE_FILE ")]
        if len(filenames) != 1 or not re.fullmatch(r"[A-Za-z0-9_+-][A-Za-z0-9._+-]*\.yml", filenames[0]):
            raise ValueError(f"{glass.name}: invalid LensDE material filename")
        filename = filenames[0]
        cdgm.append((filename, cdgm_yaml(glass)))
        row_count = 0
        for comment in glass.comments:
            if not comment.startswith("LENSDE_ROW "):
                continue
            number, values = comment.removeprefix("LENSDE_ROW ").split(" ", 1)
            ordinal = int(number)
            row = next(csv.reader([values], strict=True))
            if (len(row) != len(CSV_HEADER) or row[0] != "glass"
                    or row[5] != f"glass/cdgm/{filename}" or ordinal in ordered_rows):
                raise ValueError(f"{glass.name}: invalid LensDE index row")
            ordered_rows[ordinal] = row
            row_count += 1
        if not row_count:
            raise ValueError(f"{glass.name}: missing LensDE index rows")
    if (not cdgm or len({filename.lower() for filename, _ in cdgm}) != len(cdgm)
            or set(ordered_rows) != set(range(len(ordered_rows)))):
        raise ValueError("The CDGM AGF is missing benchmark models or index rows.")

    plastic_agf = catalog_dir / "PLASTIC2026.AGF"
    plastic = []
    plastic_rows = []
    names = set()
    for glass in parse_agf(plastic_agf):
        filename = safe_filename(glass.name)
        if filename.lower() in names:
            raise ValueError(f"{glass.name}: duplicate PLASTIC2026 model filename")
        names.add(filename.lower())
        content, lo, hi = plastic_yaml(glass, plastic_agf, "PLASTIC2026", 0.001, 2000)
        plastic.append((f"{filename}.yml", content))
        plastic_rows.append([
            "glass", "PLASTIC2026", "PLASTIC2026 (Zemax AGF)", "PLASTIC2026",
            f"{glass.name} (PLASTIC2026)", f"glass/plastic2026/{filename}.yml",
            yaml_number(lo), yaml_number(hi), filename,
        ])
    if not plastic:
        raise ValueError("The PLASTIC2026 AGF contains no glass records.")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix=".lensde-materials-", dir=destination.parent) as temporary:
        temporary_path = Path(temporary).resolve()
        if temporary_path.parent != destination.parent:
            raise ValueError("Temporary database directory escaped the output parent.")
        stage = temporary_path / "database"
        for library, models in (("cdgm", cdgm), ("plastic2026", plastic)):
            folder = stage / "data-nk" / "glass" / library
            folder.mkdir(parents=True)
            for filename, content in models:
                (folder / filename).write_text(content, encoding="utf-8", newline="\r\n")
        with (stage / "catalog_nk.csv").open("w", encoding="utf-8", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(CSV_HEADER)
            writer.writerows(ordered_rows[i] for i in range(len(ordered_rows)))
            writer.writerows(plastic_rows)
        if destination.exists():
            raise FileExistsError(f"Database already exists: {destination}")
        stage.rename(destination)
    return len(cdgm), len(plastic)


def main() -> int:
    catalogs = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog-dir", type=Path, default=catalogs,
                        help="directory containing CDGM.AGF and PLASTIC2026.AGF")
    parser.add_argument("--database-root", type=Path, default=catalogs / "database",
                        help="directory to create for the generated LensDE database")
    args = parser.parse_args()
    try:
        cdgm, plastic = prepare_database(args.catalog_dir, args.database_root)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"Material preparation failed: {exc}\n")
    print(f"Prepared {cdgm} CDGM and {plastic} PLASTIC2026 models in {args.database_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
