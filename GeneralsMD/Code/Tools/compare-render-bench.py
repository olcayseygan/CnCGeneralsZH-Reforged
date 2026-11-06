#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
"""Compare two render benchmark binaries with alternating, paired runs."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command_output(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=10)
    return (result.stdout or result.stderr).strip()


def run_benchmark(binary, arguments, environment):
    result = subprocess.run(
        [str(binary), *arguments], env=environment, capture_output=True,
        text=True, timeout=120,
    )
    if result.returncode:
        raise RuntimeError(
            f"{binary.name} exited {result.returncode}:\n{result.stderr}\n{result.stdout}"
        )
    payload = json.loads(result.stdout)
    if not isinstance(payload, dict):
        raise ValueError("benchmark output must be a JSON object")
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=9)
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    if not 3 <= options.repeats <= 99:
        parser.error("--repeats must be between 3 and 99")
    arguments = options.arguments
    if arguments[:1] == ["--"]:
        arguments = arguments[1:]
    baseline = options.baseline.resolve(strict=True)
    candidate = options.candidate.resolve(strict=True)
    environment = os.environ.copy()
    # Validation changes CPU/GPU costs. Correctness is checked in a separate run.
    for name in list(environment):
        if (name.startswith("MTL_DEBUG_LAYER") or name.startswith("MTL_SHADER_VALIDATION")
                or name.startswith("ZH_GPU_")):
            environment.pop(name)
    environment.pop("MTL_HUD_ENABLED", None)
    environment["LC_ALL"] = "C"
    if sys.platform == "darwin":
        environment["SDL_VIDEO_DRIVER"] = "dummy"
        environment["ZH_SDL_GPU_METAL_WINDOWLESS"] = "1"
    elif sys.platform.startswith("linux"):
        environment["SDL_VIDEO_DRIVER"] = "offscreen"

    report = {
        "schema": 1, "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "host": {"platform": platform.platform(), "machine": platform.machine()},
        "baseline": {"path": str(baseline), "sha256": sha256(baseline)},
        "candidate": {"path": str(candidate), "sha256": sha256(candidate)},
        "environment": {name: environment.get(name) for name in (
            "SDL_VIDEO_DRIVER", "ZH_SDL_GPU_METAL_WINDOWLESS", "SDL_GPU_DRIVER")},
        "instrumentation": "ZH_GPU_*, Metal validation and Metal HUD disabled",
        "arguments": arguments, "runs": [],
    }
    if sys.platform == "darwin":
        report["host"]["cpu"] = command_output(["/usr/sbin/sysctl", "-n", "machdep.cpu.brand_string"])
        report["host"]["thermal_before"] = command_output(["/usr/bin/pmset", "-g", "therm"])
    try:
        report["working_tree_head"] = command_output(["git", "rev-parse", "HEAD"])
        patch = subprocess.check_output(["git", "diff", "--binary", "HEAD"])
        report["working_patch_sha256"] = hashlib.sha256(patch).hexdigest()
    except (OSError, subprocess.CalledProcessError):
        pass

    for pair in range(options.repeats):
        order = ("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline")
        run = {"pair": pair + 1, "order": list(order)}
        for label in order:
            binary = baseline if label == "baseline" else candidate
            run[label] = run_benchmark(binary, arguments, environment)
        for field in ("benchmark", "workload", "backend", "mode", "frames", "warmup", "draws",
                      "lights", "qos", "width", "height", "level", "checksum"):
            if run["baseline"].get(field) != run["candidate"].get(field):
                raise ValueError(f"baseline/candidate disagree on {field}")
        if sys.platform == "darwin" and run["baseline"].get("backend") != "metal":
            raise ValueError("this macOS comparison requires the Metal backend")
        report["runs"].append(run)
        print(f"pair {pair + 1}/{options.repeats} complete", file=sys.stderr, flush=True)

    # The tools expose times explicitly; counts and byte totals remain raw evidence.
    summary = {}
    first = report["runs"][0]["baseline"]
    for metric, value in first.items():
        if not metric.endswith("_ns_per_frame") or not isinstance(value, (float, int)):
            continue
        before = [run["baseline"][metric] for run in report["runs"]]
        after = [run["candidate"][metric] for run in report["runs"]]
        if any(not isinstance(x, (float, int)) or not math.isfinite(x) or x <= 0 for x in before + after):
            raise ValueError(f"invalid timing metric: {metric}")
        relative = [100 * (1 - b / a) for a, b in zip(before, after)]
        summary[metric] = {
            "baseline_median": statistics.median(before),
            "candidate_median": statistics.median(after),
            "baseline_range": [min(before), max(before)],
            "candidate_range": [min(after), max(after)],
            "paired_reduction_percent_median": statistics.median(relative),
            "paired_reduction_percent_range": [min(relative), max(relative)],
        }
    if not summary:
        raise ValueError("benchmark exposed no *_ns_per_frame metrics")
    report["summary"] = summary
    if sys.platform == "darwin":
        report["host"]["thermal_after"] = command_output(["/usr/bin/pmset", "-g", "therm"])
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(summary, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
