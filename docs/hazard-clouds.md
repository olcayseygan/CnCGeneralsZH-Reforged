# Volumetric hazard clouds

**Options → Graphics → Cloud style** selects **Classic** or **Volumetric**. Classic, the original
toxin and radiation ground effects, is the default; Volumetric is an opt-in. Switching applies live
and persists as `CloudStyle = 0` or `CloudStyle = 1` in Options.ini. The choice is independent of
the particle and smoke-shadow settings. The Classic graphics setting overrides it: that is the
game's own picture, puddles included. The heat effects option takes the nuclear shimmer off.

## How it is split

- **Neutral core**, built on every platform: `Platform/HazardCloud.h` (the renderer API: field and
  body types, palettes, the camera a cloud is drawn from, the abstract `HazardCloud::Layer`,
  `Supported()`, `CreateLayer()` and `ReleaseDevice()`), `Platform/HazardCloudPolicy.h` (which
  templates are which hazard, build-up, validity), `HazardCloudFlow` (the planar smoke-transport
  simulation), `HazardCloud.cpp` (approved palettes, cache estimate), and the field tracking in
  `W3DParticleSys.cpp`.
- **Device-free half of a layer**, also on every platform: `Platform/HazardCloudCore.h` with
  `HazardCloudCore.cpp` (`HazardCloud::LayerCore`: fields, wakes, palettes, terrain grids, the
  shader constants and when the atlases rebake) and `HazardCloudShader.cpp` (the HLSL text of the
  cloud passes and the nuclear embers). A backend derives from `LayerCore` and supplies the device
  half: `initialize`, `render`, `estimatedCacheBytes` and `shutdown`.
- **SDL GPU backend**, built where the SDL GPU renderer is (Linux and macOS):
  `PosixDevice/Render/SdlHazardCloud*.cpp`. `Supported()` is true when the device in use is the SDL
  GPU device and it provides a sampleable scene depth plus the R16F/RGBA16F/RGBA32F formats the
  clouds use. Its shaders go through `SDL3_Create_Shader`, so Metal gets them transpiled from SPIR-V.
- **Direct3D 11 backend**, Windows: `W3DDevice/GameClient/DX11HazardCloud.cpp`. `Supported()` is
  true while the Direct3D 11 frame presents (not under `-d3d9` or `-headless`) on a feature level
  11 device that renders and samples R16F and RGBA16F and samples RGBA32F and R32F. The same HLSL
  is compiled at shader model 5 without SDL's register spaces, through
  `DX11BackendClass::Compile_Program`, so the programs land in `Run/dx11shaders.cache` and a second
  run compiles nothing. `test_dx11backend` compiles every pass and fails if FXC refuses one.
- **No backend**: `HazardCloudUnsupported.cpp`, where `Supported()` is false and `CreateLayer()`
  returns null, linked only into the tests that carry no device.

The Direct3D 11 passes run on the context between two of the backend's draws, from
`W3DParticleSystemManager::doParticles`, while the draws go into the scene's own target or into a
screen filter's texture over the scene's depth (black and white, motion blur, cross fade:
`DX11BackendClass::Scene_Target`); a mirror and the sun's map keep Classic. The scene's depth is
read through `DX11DeviceClass::Get_Depth_Texture`, so the composite binds that target with no depth
buffer. Afterwards the scene target, its depth and the viewport are
bound again and `Forget_Bindings` makes the backend's next draw bind everything it needs. The heat
haze bends a `CopyResource` of the scene taken after the smoke and embers. The embers are drawn
with the backend's half-pixel viewport offset, so they sit where Direct3D 9 would put them.

Classic, or no backend, tracks the ground fields as retail did. The extended tracking (fields keyed
by height and owner, owner binding for nuclear pulses, the sparse-emitter grace rule, skipping
systems still in their initial delay) runs only with a backend and the Volumetric style. The device
is asked once and the answer cached; the mode is decided at the sync point only, and when it changes
the field tables are cleared before their key ordering changes. The first sync point with Volumetric
on (or a new device) also initializes a throwaway layer, which builds the programs every layer
shares: built by the first hazard, a program missing from the shader cache froze that frame for 3.6
to 5.5 s on Direct3D 11.

A field's object fires its weapon on the frame it dies, so one last system turns up with no living
owner. It joins the bound field already standing at the same place and palette, the dead owner's,
and fades with it. Without that it started a new unbound cloud at full strength, about ten seconds
of smoke after the hazard ended (a Toxin Tractor's contamination: owner gone at frame 3850, the
second cloud drawn until 4436).

## What Volumetric draws

Approved green toxin, blue beta, purple gamma and amber nuclear palettes use transparency defaults
of 86%, 86%, 86% and 0%. Effects read their actual emission radius and hazard lifetime. Toxin
billows form outward; nuclear haze fades into its full footprint. Formation is capped to one quarter
of short hazard lifetimes. Visible infantry stir the smoke; hidden, stealthed or shrouded infantry
leave no wakes. Nothing here changes damage, weapons, movement, gameplay randomness or simulation
state: Classic and Volumetric runs of the same scenario give the same game-state CRC.

Everything the clouds read from the game (field owners and their remaining lifetime, infantry
positions, visibility and shroud) is taken in `updateGroundFields` at the sync point, through
`W3DParticleSystemManager::takeReadsForRender`; the draw reads only those copies, the render
terrain heights and the scene camera, which the caller hands over from `rinfo.Camera` rather than
the device's transforms (those can be the last draw's until the next state flush).

Each field's density and light live in two atlases baked from the field's state. A camera move
never rebakes them. A structural change does at once: a palette, the terrain, the field's size, a
new field. A cloud that only ages or is stirred rebakes at most `BakesPerSecond` (20) times a
second, and a wake grid with no infantry moving and nothing left to heal is put at rest and not
uploaded again. `test_hazard_cloud_policy` holds both rules.

The client admits at most eight visible cloud layers within a 192 MiB atlas-cache budget.
Over-budget fields, unknown effects and missing GPU resources keep the Classic effect. Clouds draw
after opaque geometry and water in the main scene; reflection and custom passes leave the
main-view caches and wakes alone. A map's end frees the layers; the programs and screen-sized
targets they share stay until `ReleaseDevice`, which `releaseCloudResources` calls at the display's
device sites (teardown, alt-tab reset, a resolution or window change).

The support model follows height-map terrain or an elevated plane. Cliffs and crossing decks have no
dedicated fixture or automatic geometry fallback: one height per XY sample cannot represent
several support surfaces within a field. Exactly coincident owners of the same palette cannot be
told apart by position.

`HAZARDCLOUD:` in the Windows log says when a layer first draws on Direct3D 11, and why the clouds
stopped if a program or a texture was refused.

## Tests

```sh
cmake --build build-linux --target generals test_hazard_cloud_policy test_gameengine sdl_gpu_frame_selfcheck
ctest --test-dir build-linux --output-on-failure -R '^(test_hazard_cloud_policy|test_gameengine|sdl_gpu_frame_selfcheck)$'
```

On Windows `build.bat Release test` runs `test_hazard_cloud_policy` and `test_dx11backend`.
`Scenarios/toxinslope.txt` puts a Scud Storm field on the mound at 1075, 3775 on Twilight Flame;
with `CloudStyle = 1` in the run's Options.ini, `-camera 1075 3775 -turbo -screenshot 2200` shows
it.
