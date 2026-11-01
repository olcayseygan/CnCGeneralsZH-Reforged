# Metal measurements on M2 Max

Measured on 1 November 2026, using the native arm64 Release build, Apple M2 Max,
macOS 27.2 and the `apple-m2` scheduling profile. The baseline is
`bf6fcd62cbfe67c9bf0e061f9d42556ac511a630`. All inputs are synthetic; no retail
game data is required or included.

These are targeted measurements of the production renderer, not match FPS.
Rendering, shader features, simulation rules and supported operating systems
are retained. The GPU tests compare rendered pixels as well as upload sizes.

## Results

Each value below is the median of nine runs. Each run first warms the renderer,
then averages the per-frame time. Baseline and candidate run sequentially in
alternating AB/BA order. Percentages in the paired column are the median and
observed range of the nine paired reductions; a negative value means slower.

| Workload | Baseline CPU ms | Candidate CPU ms | Paired CPU reduction |
| --- | ---: | ---: | ---: |
| 1,024 tree-shader draws, four carried light states | 0.377 | 0.298 | 21.1% (17.2–22.6%) |
| Same draws, no enabled lights | 0.344 | 0.300 | 12.7% (11.5–13.4%) |
| Generated fixed-function shader control | 0.360 | 0.357 | 1.3% (−5.8–14.3%): effectively unchanged |
| Update mip 6 (32×32) of a 2048×2048 texture | 1.083 | 0.015 | 98.6% (97.2–98.9%) |
| Update only the 2048×2048 base mip | 1.387 | 1.141 | 17.1% (4.9–30.6%) |

The draw workloads use 60 warmup frames and 300 measured frames per run. Both
shader modes draw the same unlit quad. The light states are carried device state
which the engine shader does not consume; they quantify work previously done
unnecessarily. The no-light case verifies that the gain does not depend on
those four states. Generated shaders retain the original combined builder and
its uniform-binding path.

The mip workloads use 30 warmup frames and 120 measured frames per run. Updating
mip 6 stages **4,096 bytes instead of 22,369,620 bytes** per frame. Including CPU
work, submission and GPU completion, its median falls from **2.061 to 0.221 ms**;
the paired reduction is **88.2%**, with every pair improving by 83.4–92.0%.
Updating the base mip stages 16,777,216 bytes and preserves all sibling levels.
Initial uploads and complete-chain replacements still upload every level.

CPU time uses `CLOCK_THREAD_CPUTIME_ID`, excluding descheduled and waiting time.
Draw CPU time covers the production draw-recording calls; upload CPU time also
covers the CPU mip write, staging and submission. Wall-clock completion time is
recorded separately and includes `SDL_WaitForGPUIdle`. Most draw completion
ranges overlap substantially: these results do **not** establish a display-FPS
improvement or a reduction in GPU shader execution time.

Both benchmark processes request the same macOS `user_initiated` thread QoS.
The runner removes validation, tracing, capture and GPU tuning overrides.
No system-wide power setting is changed. `pmset` reported no recorded thermal
or performance warning; CPU frequencies and background apps were not locked.
See Apple's [QoS guidance](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/PrioritizeWorkAtTheTaskLevel.html).

The draw benchmark checks draw counts and framebuffer pixels every frame,
outside the timing spans. The mip benchmark validates every pixel of every mip
after the measured loop. Baseline and candidate checksums match in all 45 pairs.
The regression tests also hold an encoded GPU reference across a base-mip
update: restoring incorrect cycling loses four sibling levels. The old renderer
additionally fails the staged-byte and rendered-base preservation checks.

## Reproduce

Build the opt-in tools; they are excluded from normal builds and from CTest:

```sh
cmake --build build-mac --target sdl_draw_bench sdl_mip_bench --parallel 8
SDL_VIDEO_DRIVER=dummy ZH_SDL_GPU_METAL_WINDOWLESS=1 \
  ./build-mac/sdl_draw_bench --mode engine --lights 4 --draws 1024 --frames 300 --warmup 60
SDL_VIDEO_DRIVER=dummy ZH_SDL_GPU_METAL_WINDOWLESS=1 \
  ./build-mac/sdl_mip_bench --size 2048 --level 6 --frames 120 --warmup 30
```

For a comparison, first save `libposixd3d9.a` from the baseline build outside the
checkout. Build the new tools, then link the *same benchmark object* against the
saved archive. The helper reads the actual Ninja link rule and keeps all other
dependencies and flags. Saved archives must match the target architecture and
the public API used by the benchmark.

```sh
python3 GeneralsMD/Code/Tools/relink-render-bench.py \
  --build build-mac --target sdl_mip_bench \
  --archive /tmp/baseline-libposixd3d9.a --output /tmp/mip-baseline
python3 GeneralsMD/Code/Tools/compare-render-bench.py \
  --baseline /tmp/mip-baseline --candidate build-mac/sdl_mip_bench \
  --output /tmp/mip-comparison.json --repeats 9 -- \
  --size 2048 --level 6 --frames 120 --warmup 30
```

For the recorded draw comparison, only the saved `PosixDevice9Draw.cpp.o` was
placed before the candidate archive. This isolates the constants change; the
class layout did not change. The helper's `--object` option reproduces that
link. The mip comparison uses the complete saved archive so its private upload
layout remains consistent. Measurement should run with no parallel build or GPU
test. Correctness validation is a separate run using the command in
[the macOS guide](macos.md).

[Raw paired samples](benchmarks/m2-max-2026-11-01.csv) contain CPU, wall-clock,
completion, staged-byte and checksum values. The accompanying
[metadata](benchmarks/m2-max-2026-11-01.json) records source, object, archive and
executable hashes, exact link commands, configuration and host information.
