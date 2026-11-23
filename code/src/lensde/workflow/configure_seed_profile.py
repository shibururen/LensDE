"""Create a versioned project for GUI/CLI execution without changing its source."""

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root
from pathlib import Path
import argparse
import json
import sys

GUI = PROJECT_ROOT / "web"

from lensde.seeds import protocol as seed_protocol
from lensde.engine import core as server

def formal_project(profile_name: str) -> dict:
    from lensde.config import BenchmarkCatalog
    profile = seed_protocol.load_profile(profile_name)
    if "source_archive" not in profile:
        raise ValueError("generic_v2 has no historical formal input")
    prefix = profile_name.split("_", 1)[0]
    task = profile.get("benchmark_task") or ("DF1" if prefix == "GGPGPPPP" else prefix[1:] if prefix.startswith("9A") else prefix)
    case = BenchmarkCatalog(PROJECT_ROOT).load(task)
    return {"system": case.system, "variables": case.variables["variables"],
            "merit": case.merit, "options": case.options}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--project", type=Path, help="project JSON or a manifest containing project; only seed configuration is upgraded")
    inputs.add_argument("--from-formal", action="store_true", help="use the selected profile's actual saved initial input and effective options")
    parser.add_argument("--profile", required=True, help="generic_v2 or a saved formal seed profile")
    parser.add_argument("--seed", type=int, help="master seed; defaults to the profile's historical reference")
    parser.add_argument("--output", type=Path, required=True, help="new project JSON, never the original archive")
    args = parser.parse_args()
    if args.output.exists() or args.project is not None and args.output.resolve() == args.project.resolve():
        parser.error("output must be a new file")
    if args.from_formal:
        project = formal_project(args.profile)
    else:
        document = json.loads(args.project.read_text(encoding="utf-8-sig"))
        project = document.get("project", document)
    if "system" not in project:
        parser.error("input does not contain a project system")
    configured = server.normalise_project(seed_protocol.configure_seed_only(project, args.profile, args.seed))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(configured, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(configured["options"]["initial_search_resolved_seed_contract"], ensure_ascii=False, indent=2))
    print("Saved project:", args.output.resolve())

if __name__ == "__main__": main()
