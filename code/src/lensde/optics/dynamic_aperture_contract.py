"""Production aperture sampling, independent of image-quality sampling."""

CONTRACT = "dynamic_aperture_4x8_v1"


def apply_merit(merit: dict) -> dict:
    """Normalize in place, including stale explicit values from saved runs."""
    merit["dynamic_apertures"] = True
    merit["clear_aperture_rings"] = 4
    merit["clear_aperture_arms"] = 8
    merit["vignetting_merit_aperture_envelope_experimental"] = False
    return merit


def apply_command(command: list[str], *, materialized_input: bool = False) -> list[str]:
    """Normalize in place without changing --rays/--arms (image sampling)."""
    result = []
    index = 0
    while index < len(command):
        token = command[index]
        if token == "--clear-rings":
            index += 2
            continue
        if token in {
            "--no-clear-aperture-update", "--clear-aperture-update",
            "--sync-stop-aperture", "--input-clear-apertures-materialized",
            "--vignetting-merit-aperture-envelope-experimental",
            "--no-vignetting-merit-aperture-envelope-experimental",
        }:
            index += 1
            continue
        result.append(token)
        index += 1
    result.extend([
        "--clear-aperture-update", "--sync-stop-aperture", "--clear-rings", "4",
        "--no-vignetting-merit-aperture-envelope-experimental",
    ])
    if materialized_input:
        result.append("--input-clear-apertures-materialized")
    command[:] = result
    return command
