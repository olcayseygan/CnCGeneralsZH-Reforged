# Native heat haze and blast distortion

This ports the existing DX11 4.1.0 formulas and producer records to the native SDL
main-scene route. It adds no new look, tuning, command-line switch or environment
setting. The existing Heat Haze option and a valid, explicitly requested post chain
admit heat; a post chain admits existing ScreenDistortion FX. Classic graphics,
post-off/invalid chains, unsupported renderers and excluded views do not admit them.
An internal neutral COPY used solely for world AO does not count as a requested chain.

The exact slot-03 scene identity owns every operation: target, device/frame generation,
view key and capture phase must match. Heat consumes fresh independent `BeforePost`
normalized R32 depth and an independent colour snapshot before normal post processing.
A foreground surface blocks the heat column using the original clearance/softness;
visible flame, exhaust and beam selection, fading and paused client time are unchanged.
Blast snapshots the completed normal post output and warps it before health bars and
HUD drawing. Recovery COPY never proves normal completion, so it cannot admit blast.
No water, reflection, sun or earlier AO depth capture substitutes for BeforePost depth.

One device-owned distortion module shares fullscreen programs, sampler and teardown.
Its heat field is half-resolution RGBA16F, with a full-size same-format heat colour copy;
blast has an independent same-format final-output copy. Both composites write RGB only,
preserving destination alpha and depth. All programs and images prepare before visible
work. Missing allocation, shader, pipeline, capture or recording bypasses that effect
and leaves the normal post route usable. Repeated/stale calls consume pending records
without replaying them. Release submits queued borrowers or retains resources on failure;
teardown can discard pending readers before releasing them. Warm dimensions/formats
reuse resources and replacement images retire through the existing frame owner.

The heat layout and 2048-record limit remain four float4s per record (128 KiB). This
baseline predates the fork's graphics-storage subsystem: native instance vertex
attributes transport those exact records instead. One six-vertex draw uses the copied
record count; existing recorded draws retain one instance. The persistent instance
buffer submits previous borrowers before the existing frame staging path updates it.
This avoids importing the later storage/particle migration; it does not claim matching
performance on every driver. Blast retains the four-record limit and original projected
radii, pull, dome shape, border settling and client-frame aging. Windows still uses its
original producer entry points and shared shader text.

## In-game capture recipe

Use normal packaged data, a fixed camera and a visible main battlefield. No custom
fixture is required. Start with the existing `-dx11post copy` chain to isolate distortion
from grading/FXAA, then repeat with the normal bloom/FXAA/sharpen chain. Keep resolution,
viewport and camera constant. Include an offset or reduced world viewport above the
command bar: the existing producers already project into full-texture coordinates.

For heat, enable the existing Heat Haze option and observe a Dragon Tank flame, a
missile exhaust and a laser beam. Include a building or vehicle between the camera and
the flame and a textured background behind it: the foreground outline should remain
stable while the background shimmers. Compare Heat Haze off/on, then pause: the existing
client clock and beam/exhaust records must remain stable. Check overlapping columns,
near-screen-edge emitters and a hidden/shrouded fire; visibility follows the existing
producer. Repeat under both eight-bit COPY and float bloom scenes.

For blast, trigger a Scud Storm impact (`ScudStormMissileDetonation`) or a Nuke Cannon
impact (`WeaponFX_NukeCannon` / `Nuke_WeaponFX_NukeCannon`) from the existing shipped FX
lists. Capture pull, expanding ring and settled frames with terrain behind the centre.
Health bars and HUD text should remain straight. Repeat near a screen edge and during
pause. Do not change camera shake or add the optional severe-explosion kick.

For both, repeat with post off, malformed chain, Classic graphics, mirror/sun/custom
passes and menu/loading views. Effects must not leak from a prior admitted main view.
Turn off the user chain while world AO stays enabled: its internal COPY must not make
heat/blast reappear. Resize while effects are live, then return to an ordinary frame.
Windows comparison remains the existing DX11 effect; no new Windows path is introduced.

## Focused verification

`test_sdl_distortion` and `test_sdl_heat` compile the mechanically adapted shared source
to SPIR-V and Metal text. Their `--gpu` variants compare actual native output to independent
CPU pixel oracles, including original alpha/depth, zero/count limits, pause-equivalent
records, overlap, edges, float scenes, resize/reuse, stale phase/frame/generation/view,
post off, completed-post proof and injected preparation/recording failures. The heat
fixture exercises 2048 instances and rejects the 2049th record. Replacing each native
composite with COPY must fail its corresponding nonzero-effect pixel comparisons.
`test_sdl_post` and `test_sdl_ao` cover the prerequisite route after the recorded-draw
transport change. CTest registration precedes profile collection and uses explicit
render include paths because `ZH_POSIX_RENDER_DIR` is initialized later.

These fixtures do not prove an in-game capture, a window swapchain or runtime cost.
Those remain integration checks using the recipes above.
