#!/usr/bin/env python3
"""Prepare a versioned manifest while keeping source paths relative to its location."""
import argparse
import json
from pathlib import Path
import os
import re


def prepare(version: str, output: Path) -> None:
    if version and not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("Use a numeric release version such as 0.2.0")
    source = Path(__file__).resolve().parents[1] / "packaging/flatpak/io.github.hdijkema.AltPgAdmin.json"
    manifest = json.loads(source.read_text(encoding="utf-8"))
    application = next(module for module in manifest["modules"] if module["name"] == "pgadmin-jr")
    application["config-opts"].append(f"-DPGADM_RELEASE_VERSION={version}")
    for module in manifest["modules"]:
        for item in module.get("sources", []):
            if "path" in item:
                item["path"] = os.path.relpath((source.parent / item["path"]).resolve(), output.parent.resolve())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.version, args.output)
    except ValueError as error:
        parser.error(str(error))
