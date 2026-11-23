"""Prepare a seed-only project locally, without importing the workbench server."""

import argparse
import json
from pathlib import Path

from . import configure_seed_only, validate_reference_profiles


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-profiles", action="store_true")
    parser.add_argument("--project", type=Path)
    parser.add_argument("--profile")
    parser.add_argument("--master-seed", type=int)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--strict", action="store_true",
                        help="reject generic profiles or incomplete historical seed tables")
    args = parser.parse_args(argv)
    if args.check_profiles:
        if any(value is not None for value in (args.project, args.profile, args.output, args.master_seed)):
            parser.error("--check-profiles cannot be combined with project conversion")
        print(json.dumps(validate_reference_profiles(), ensure_ascii=False, indent=2))
        return 0
    if args.project is None or args.profile is None or args.output is None:
        parser.error("--project, --profile and --output are required")
    if args.output.resolve() == args.project.resolve():
        parser.error("output must differ from the original project")
    document = json.loads(args.project.read_text(encoding="utf-8-sig"))
    project = document.get("project", document)
    if not isinstance(project, dict) or "system" not in project:
        parser.error("input must contain a project system")
    configured = configure_seed_only(project, args.profile, args.master_seed, strict=args.strict)
    # No directory creation or old-workspace fallback. The caller chooses an
    # existing destination directory; exclusive creation protects old evidence.
    with args.output.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(configured, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps(configured["options"]["initial_search_resolved_seed_contract"],
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
