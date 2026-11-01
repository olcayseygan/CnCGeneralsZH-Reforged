#!/usr/bin/env python3
"""Repeat a sun-shadow scene with isolated settings and an inspectable manifest.

Uses an existing executable, overlay and prepared file-link root. Installs nothing.
Never run concurrent GPU workloads when comparing the recorded timings. The original
timing probe includes loading; its aggregate is diagnostic, not steady-state FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

CASES = {
    "solid": ("Flash Effect", 1200, 945, 800, None),
    "smoke": ("Flash Effect", 1700, 1580, 600, "rf_smoke"),
    "cloak": ("Flash Effect", 1700, 1580, 300, "sun_cloak"),
    "trees": ("Flash Effect", 1700, 1580, 300, "sun_trees"),
    "forest": ("ForgottenForestZH", 1620, 1470, 1200, None),
    "bridge": ("Golden Oasis", 1750, 1500, 600, "bridgecross"),
    "helicopter": ("Fortress Avalanche", 1000, 2100, 300, "s240_heli"),
    "water": ("Tournament Island", 400, 1000, 240, None),
    "river": ("Winding River", 1300, 1700, 240, None),
}

OPTIONS = """Resolution = 1280 720
ClassicGraphics = no
LightingStyle = 1
StaticGameLOD = Custom
IdealStaticGameLOD = High
DynamicLOD = no
TextureReduction = 0
TextureFilter = 2
Anisotropy = 8
ShadowQuality = 1
UseShadowVolumes = yes
UseShadowVolumesForSkins = yes
UseShadowDecals = yes
ShadowsForProps = yes
ShadowsForProjectiles = yes
ShadowsForParticles = yes
ShowTrees = yes
TreeSway = yes
ShowSoftWaterEdge = yes
UseCloudMap = yes
UseLightMap = yes
MaxParticleCount = 3000
ExtraAnimations = yes
SmoothMotion = no
ShowNetBox = no
VSync = no
MSAA = 0
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("generals", "root", "overlay", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--case", choices=CASES, default="solid")
    p.add_argument("--quality", type=int, choices=(0, 1, 2), default=1, help="existing shadow quality: 1024/2048/4096")
    p.add_argument("--trees", choices=("yes", "no"), default="yes", help="ShowTrees preference; multiplayer can still load trees")
    p.add_argument("--geometry-shadows", choices=("yes", "no"), default="yes", help="set both ordinary shadow options")
    p.add_argument("--smoke-shadows", choices=("yes", "no"), help="override the separate volumetric smoke shadow option")
    p.add_argument("--gpu-debug", action="store_true", help="enable GPU validation (not a performance run)")
    p.add_argument("--icd", type=Path)
    p.add_argument("--timeout", type=int, default=240)
    p.add_argument("--frame", type=int, help="override the case's screenshot frame (also the measurement length minus 60)")
    p.add_argument("--replay", type=Path, help="play an existing recording in isolated user data instead of starting a match")
    p.add_argument("--measure", action="store_true", help="omit the screenshot and its settling/pacing window")
    p.add_argument("--report", action="store_true", help="read back sun depth occupancy (excluded from performance runs)")
    args = p.parse_args()
    if (args.report or args.gpu_debug) and args.measure:
        p.error("diagnostic readback/validation cannot be used with --measure")
    replay = args.replay.resolve() if args.replay else None
    if replay and (not replay.is_file() or CASES[args.case][4]):
        p.error("--replay requires an existing file and a case without scenario actions")
    exe, root, overlay, out = (getattr(args, n).resolve() for n in ("generals", "root", "overlay", "out"))
    if not exe.is_file() or not root.is_dir() or not overlay.is_dir():
        p.error("executable, prepared root and overlay must already exist")
    # No mixing fresh pictures with stale results, and no player settings or saves.
    out.mkdir(parents=True, exist_ok=False)
    user = out / "user"
    user.mkdir()
    options = OPTIONS.replace("ShadowQuality = 1", "ShadowQuality = " + str(args.quality))
    options = options.replace("ShowTrees = yes", "ShowTrees = " + args.trees)
    for option in ("UseShadowVolumes", "UseShadowDecals"):
        options = options.replace(option + " = yes", option + " = " + args.geometry_shadows)
    if args.smoke_shadows:
        options += "VolumetricSmokeShadows = " + args.smoke_shadows + "\n"
    (user / "Options.ini").write_text(options)
    name, x, y, shot, scenario = CASES[args.case]
    if replay:
        (user / "Replays").mkdir()
        shutil.copyfile(replay, user / "Replays" / "sun-check.rep")
    if args.frame is not None:
        if args.frame < 1:
            p.error("--frame must be positive")
        shot = args.frame
    scenario_source = None
    if scenario:
        # Development scenarios are deliberately absent from the packaged overlay.
        # Add just the chosen fixture to a private overlay, leaving the build untouched.
        fixture_overlay = out / "overlay"
        fixture_overlay.mkdir()
        for entry in overlay.iterdir():
            if entry.name != "Scenarios":
                (fixture_overlay / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
        (fixture_overlay / "Scenarios").mkdir()
        scenario_source = Path(__file__).resolve().parent.parent / "Data" / "Scenarios" / (scenario + ".txt")
        shutil.copyfile(scenario_source, fixture_overlay / "Scenarios" / scenario_source.name)
        overlay = fixture_overlay
    command = [str(exe), "-root", str(root), "-overlay", str(overlay),
               "-offscreen", "-multiInstance", "-noaudio",
               "-win", "-xres", "1280", "-yres", "720", "-msaa", "0",
               "-quickstart", "-noshellmap", "-nologo", "-novideo", "-noFPSLimit",
               "-camera", str(x), str(y),
               "-turbo", "-screenshot", str(shot), "-maxframes", str(shot + 60),
               "-logPrefix", "shadow_"]
    if replay:
        command += ["-replay", "sun-check"]
    else:
        command += ["-map", f"Maps/{name}/{name}.map", "-autoskirmish", "2",
                    "-seed", "1", "-side", "0", "FactionAmerica",
                    "-side", "1", "FactionChina"]
        # Cloak coverage requires the owner's view; observers do not draw the friendly cloak.
        if args.case != "cloak":
            command.append("-observer")
    if args.report:
        command.append("-shadowmapreport")
    # Replay playback ignores scheduled screenshots. Its gate is the rendered run's
    # limit-frame CRC, with the input recording retained by hash in the manifest.
    if args.measure or replay:
        at = command.index("-screenshot")
        del command[at:at + 2]
    if scenario:
        command += ["-takeover", "-scenario", scenario]
    overrides = {"ZH_USER_DATA_DIR": str(user), "ZH_UNATTENDED": "1",
                 "ZH_AUDIO_BACKEND": "null", "SDL_GPU_DRIVER": "vulkan",
                 "ZH_OFFSCREEN_HZ": "0", "ZH_GPU_TIMING": "0,1000000"}
    if args.gpu_debug:
        overrides["ZH_GPU_DEBUG"] = "1"
    if args.icd:
        overrides["VK_DRIVER_FILES"] = str(args.icd.resolve())
    repository = Path(__file__).resolve().parents[3]
    manifest = {"command": command, "environment": overrides, "case": args.case,
                "quality": args.quality, "exe_sha256": digest(exe),
                "trees": args.trees, "geometry_shadows": args.geometry_shadows, "smoke_shadows": args.smoke_shadows,
                "options_sha256": digest(user / "Options.ini"),
                "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repository, text=True).strip(),
                "git_diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff", "HEAD"], cwd=repository)).hexdigest(),
                "timing_scope": "device probe includes loading; HEADLESS FRAMETIME starts after logic frame 60",
                "measurement": args.measure}
    if scenario_source:
        manifest["scenario_sha256"] = digest(scenario_source)
    if replay:
        manifest["replay"] = {"source": str(replay), "sha256": digest(replay)}
    # New backend files can be untracked while an increment is under review.
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "-z"], cwd=repository, text=True).split("\0")
    manifest["untracked_files_sha256"] = {name: digest(repository / name) for name in untracked
                                           if name and (repository / name).is_file()}
    manifest_path = out / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    environment = dict(os.environ)
    # This switch is presence-based in the backend: "0" would still enable it.
    environment.pop("ZH_GPU_DEBUG", None)
    environment.update(overrides)
    start = time.monotonic()
    with (out / "run.log").open("w") as log:
        process = subprocess.Popen(command, cwd=out, env=environment,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            try:
                manifest["returncode"] = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                manifest["timeout"] = True
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            manifest["returncode"] = process.returncode
    manifest["elapsed_seconds"] = time.monotonic() - start
    engine_log = exe.parent / "shadow_DebugLogFile.txt"
    if engine_log.is_file():
        shutil.copyfile(engine_log, out / "engine.log")
        manifest["match_summary"] = [line for line in engine_log.read_text(errors="replace").splitlines()
                                     if line.startswith(("HEADLESS CRC", "HEADLESS FRAMETIME", "HEADLESS LOGICTIME", "HEADLESS SCENARIO"))]
        manifest["scenario_loaded"] = not scenario or ("SCENARIO: cannot open" not in engine_log.read_text(errors="replace")
                                                       and any(line.startswith("HEADLESS SCENARIO:") and "0 actions," not in line
                                                               for line in manifest["match_summary"]))
    manifest["reached_limit"] = any(line.startswith("HEADLESS CRC AT LIMIT:") and line.endswith(f" at frame {shot + 60}")
                                    for line in manifest.get("match_summary", []))
    shots = sorted(user.rglob("sshot*.bmp"))
    manifest["screenshots"] = [{"path": str(s), "sha256": digest(s)} for s in shots]
    manifest["timings"] = [line for line in (out / "run.log").read_text(errors="replace").splitlines()
                           if "PosixDevice9 timing:" in line and "LONG FRAME" not in line]
    manifest["shadow_diagnostics"] = [line for line in (out / "run.log").read_text(errors="replace").splitlines()
                                     if line.startswith("SunShadow:")]
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"manifest": str(manifest_path), "returncode": process.returncode,
                      "screenshots": len(shots), "elapsed_seconds": manifest["elapsed_seconds"]}))
    return int(process.returncode != 0 or (not args.measure and not replay and len(shots) != 1)
               or manifest.get("timeout", False) or not manifest.get("scenario_loaded", not scenario)
               or not manifest["reached_limit"])


if __name__ == "__main__":
    raise SystemExit(main())
