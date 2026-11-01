#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
"""Link the same benchmark object against a saved renderer archive or object."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--target", choices=("sdl_draw_bench", "sdl_mip_bench"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--archive", type=Path)
    source.add_argument("--object", type=Path)
    options = parser.parse_args()
    build = options.build.resolve(strict=True)
    if not (build / "build.ninja").is_file():
        parser.error("this helper requires a configured Ninja build")
    replacement = (options.archive or options.object).resolve(strict=True)
    commands = subprocess.check_output(
        ["ninja", "-t", "commands", options.target], cwd=build, text=True,
    )
    tokens = shlex.split(commands.strip().splitlines()[-1])
    # Accept only CMake's single link command. Execute an argv array, never shell text.
    if tokens[:2] != [":", "&&"] or tokens[-2:] != ["&&", ":"]:
        parser.error("unrecognized CMake link rule")
    command = tokens[2:-2]
    if any(token in ("&&", ";", "|", "||") for token in command):
        parser.error("compound link rules are not supported")
    archives = [i for i, token in enumerate(command) if Path(token).name == "libposixd3d9.a"]
    if len(archives) != 1 or "-o" not in command:
        parser.error("renderer archive or output missing from link rule")
    output = options.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output == replacement:
        parser.error("output must differ from the saved baseline input")
    if output == (build / options.target).resolve():
        parser.error("choose a separate output; retain the candidate executable")
    command[command.index("-o") + 1] = str(output)
    if options.archive:
        command[archives[0]] = str(replacement)
    else:
        command.insert(archives[0], str(replacement))
    environment = os.environ.copy()
    environment["TMPDIR"] = environment.get("TMPDIR", "/tmp/")
    subprocess.run(command, cwd=build, env=environment, check=True)
    manifest = {
        "target": options.target, "replacement": str(replacement),
        "replacement_sha256": sha256(replacement), "command": command,
        "output_sha256": sha256(output),
    }
    output.with_suffix(output.suffix + ".build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(output)


if __name__ == "__main__":
    main()
