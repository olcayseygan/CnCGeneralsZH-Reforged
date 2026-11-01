# Build and play on macOS

The native port uses SDL3 and Metal. The deployment target is macOS 13 or later;
build on the Mac you intend to use (Apple silicon or Intel). A universal app
requires separate builds of both architectures; the normal command builds one.

## Guided setup

Run `make install` for a step-by-step installer (English, German; see
[adding a language](installer.md#adding-a-language)). It asks before every
executing step and can optionally discover/copy your owned Steam/GOG installation
outside the checkout. See [the installer guide](installer.md).

## Native fullscreen and system integration

Fullscreen uses a native macOS Space. On Apple silicon with macOS 14 or later,
this allows the system's [Game Mode](https://support.apple.com/105118) to prioritize
the game. The app declares Game Mode support; macOS decides when to activate it,
and you can turn it off in the system's Game Mode menu. macOS 13 and Intel Macs
keep native fullscreen without requiring Game Mode.

The Dock and menu bar stay hidden while the game owns its fullscreen Space, so
moving the pointer to an edge still scrolls the map. Switching to another app
restores normal desktop behavior. Window transitions finish before the renderer
resets its device. If a particular display setup needs the previous fullscreen
behavior, launch the packaged executable with SDL's compatibility override:

```sh
SDL_VIDEO_MAC_FULLSCREEN_SPACES=0 \
  "build-mac/Zero Hour Reforged.app/Contents/MacOS/generals"
```

Only a visible, focused game window prevents idle display sleep. Minimizing it,
switching to another app or running offscreen releases that assertion; simulation
and networking continue as before. `SDL_VIDEO_ALLOW_SCREENSAVER=1` also allows
display sleep while playing.

The VSync setting also controls Metal presentation when the device is created or
reset. When VSync is off, immediate presentation is used if the display backend
supports it; otherwise presentation stays synchronized. The game's frame limit
and simulation timing remain separate.

Native input-method candidates follow an active text field after window,
monitor or Retina-scaling changes, without interrupting an ongoing composition.

Global C++ allocations use system-compatible storage, so AppKit, CoreGraphics
and graphics drivers can safely release it across library boundaries. Explicit
engine object and string pools retain their existing allocation paths.

On macOS 15 and later, the LAN lobby may trigger the system's local-network
permission dialog. The app explains discovery, hosting and multiplayer in English
or German. Allow access to use LAN multiplayer; an existing choice can be changed
in **System Settings → Privacy & Security → Local Network**. This permission is
managed by macOS, and the app does not request it merely to install or copy game
data. A rebuilt, ad-hoc-signed app may be treated as a new identity and prompt
again; a distributed release needs an Apple-issued signing identity for reliable
permission tracking. See Apple's [local-network privacy guidance](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy).

## Apple Silicon

Release builds run natively on arm64. `ZH_MACOS_CPU_TUNING=auto` recognizes the
M1 through M5 families, including Pro, Max and Ultra variants, and uses the
corresponding Clang `-mtune` profile when both C and C++ compilers support it.
On an M2 Max that profile is `apple-m2`. It retains the existing instruction set, so a build can
still run on M1; it does not use `-march=native`, `-mcpu=native` or fast math.
Intel, Windows and Linux builds keep their architecture paths. Explicit selection:

```sh
ZH_MACOS_CPU_TUNING=apple-m2 ./build-macos.sh Release test
ZH_MACOS_CPU_TUNING=generic ./build-macos.sh Release test
```

CMake users can set `-DZH_MACOS_CPU_TUNING=auto|generic|apple-m1|apple-m2|apple-m3|apple-m4|apple-m5`.
Changing the setting through the build script reconfigures the existing tree.
An older compiler without the detected CPU's scheduling profile falls back to generic in automatic mode;
explicit unsupported settings report an error. Build in a native terminal for an
arm64 app; an Intel/Rosetta build remains an Intel build.

FFmpeg uses the same selected target architecture as the engine, including when
`CMAKE_OSX_ARCHITECTURES` is left at CMake's default. An explicit architecture or
`CMAKE_APPLE_SILICON_PROCESSOR` override remains the caller's choice. Universal
apps still use separate arm64 and x86_64 builds before combining the executables.

The Metal renderer uses SDL's native Apple GPU resources and asynchronous command
buffers. The shader loader preserves binding slots for textures read without
samplers; this keeps FFT mips and water/foam fields active on Apple GPUs. Its FP
contract preserves the physical-water invariants across shader variants. The
height-only CPU path uses Darwin's combined sine/cosine evaluation to match the
full model exactly while still omitting derivative calculations.

The scheduling distinction is described in the [Clang driver reference](https://clang.llvm.org/docs/ClangCommandLineReference.html).
For game-specific measurements use [Metal profiling in Instruments](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app).
Scheduling flags alone are not a measured FPS improvement; actual gameplay
profiling requires your original game data. No game assets are needed for the
synthetic GPU and cross-architecture regression tests.

The renderer also avoids unused vertex-constant work for engine shaders and
uploads only changed texture mips while preserving the other levels. See the
[M2 Max measurements](metal-performance.md) for paired timings, controls and
reproduction commands. These targeted render workloads do not establish match
FPS.

Unchanged gamma ramps are cached without allocating or repacking their entries
each frame. Ramp changes cycle the GPU texture, so previously submitted frames
retain their own values. Failed uploads and submissions invalidate the cached
ramp and can be retried. Texture and vertex-buffer updates stop if a required
flush fails, preserving any commands still queued. Shore shaders use the
canonical program cache when the fast lookup misses.

After building, the offscreen GPU checks can run with Apple's
[Metal API Validation](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-api-usage):

```sh
MTL_DEBUG_LAYER=1 ZH_GPU_DEBUG=1 ctest --test-dir build-mac \
  -R '^(sdl_gpu_frame_selfcheck|sdl_program_cache_selfcheck|sdl_pipeline_state_selfcheck|posix_gpu_draw_selfcheck|posix_reset_selfcheck|ffref_gpu_selfcheck)$' \
  --output-on-failure
```

These checks use synthetic data and require no original game files. The reset
check deliberately refuses allocations, uploads and submissions, then checks
that retrying preserves the rendered data. Validation is a development tool
with runtime overhead; leave it off during gameplay measurements.

## Build the engine

Install Apple's command line tools and CMake/Ninja:

```sh
xcode-select --install
brew install cmake ninja
```

From the repository root:

```sh
./build-macos.sh Release test
open "build-mac/Zero Hour Reforged.app"
```

The build downloads third-party dependency sources. It does not download retail
game data or the optional upscaled art. Tests use synthetic inputs unless you
explicitly configure a separately owned installation; tests needing real game
data are skipped when it is absent. GPU tests also need a working Metal device.

The app is signed ad hoc, not notarised. macOS may ask you to approve opening a
locally built app. Use the app's specific Open action/System Settings approval;
do not disable Gatekeeper system-wide. Keep the app and the retail installation
separate. The app can be moved after it has been built.

## Obtain your own Generals and Zero Hour data

Both games are needed: Zero Hour provides `INIZH.big`; the base game provides
`Textures.big` and shared resources. Copy the complete installed game folders,
including language archives and movie/audio subdirectories, rather than just
those two marker files. Use your own legitimately obtained installation.

### Steam

[Generals](https://store.steampowered.com/app/2229870/) and
[Zero Hour](https://store.steampowered.com/app/2732960/) are listed on Steam;
the Zero Hour store page also lists The Ultimate Collection containing both.
You need the **Windows game data**, even though this engine runs natively.

1. Install both games through your Steam account on Windows (a PC or your own
   Windows VM). If you already have Windows Steam installed in a CrossOver
   bottle on the Mac, its installed game folders can be used too.
2. In Steam, use each game's **Manage → Browse local files** to find its folder.
3. Copy the two complete folders to the Mac, outside this checkout. For example:

```text
~/Games/
  Command & Conquer Generals - Zero Hour/
    INIZH.big
    ...all other Zero Hour files...
  Command & Conquer Generals/
    Textures.big
    ...all other Generals files...
```

The native macOS Steam client does not turn a Windows-only title into a macOS
download. The folder search can discover data you have already placed in
`~/Library/Application Support/Steam/steamapps/common/`, additional libraries
listed in `libraryfolders.vdf`, or an existing CrossOver/Whisky bottle. Discovery
does not download or purchase games. A Steam login/password is never needed by
this repository's scripts.

### GOG

As checked on **2026-11-01**, GOG has a
[Generals Deluxe Edition Dreamlist entry](https://www.gog.com/dreamlist/game/command-conquer-generals-deluxe-edition);
this is a request for a release, not a confirmed purchase/download listing.
Do not buy an unrelated GOG title expecting its data to work here.

If a compatible release becomes available in your GOG library, GOG documents
[offline backup installers](https://support.gog.com/hc/en-us/articles/213148105-How-do-I-download-my-purchased-items).
Download every required installer part, install into your own Windows environment,
and copy the resulting complete Generals and Zero Hour folders as above. An
already installed copy under a bottle's `GOG Games/` or `Program Files*/GOG Games/`
is searched too. Compatibility with a future GOG release has not been tested.
`extract-game.sh` handles existing installations and supported disc cabinets;
it does not claim to unpack arbitrary GOG installer executables.

## Select the installation

Open the app and select the **Zero Hour** folder when asked. If Generals is not
beside it or inside `ZH_Generals/`, the chooser asks for that folder separately.
The choice is remembered in `Registry.ini`. You can also select the root explicitly:

```sh
open "build-mac/Zero Hour Reforged.app" --args \
  -root "$HOME/Games/Command & Conquer Generals - Zero Hour"
```

To copy a supplied installation tree or supported disc to the default layout:

```sh
./extract-game.sh "/Volumes/My owned installation" \
  "$HOME/Games/Command & Conquer Generals Zero Hour"
```

The script runs with Apple's system Bash, supports spaces in paths, and refuses
to put the extracted installation inside this checkout. Disc cabinets may need
`unshield`; ISO extraction needs `7z` or `bsdtar`. A current, patched installation
is preferable to an unpatched disc copy.

Settings, saves and replays live separately under:

```text
~/Library/Application Support/Command and Conquer Generals Zero Hour Data/
```

Optional artwork you are entitled to use belongs in that folder's `ReforgedArt/`,
outside the app and checkout. The existing optional art downloader remains available
with an explicit external destination (Windows uses `vendor.ps1 -ArtDirectory`):

```sh
GeneralsMD/Code/Tools/vendor.sh --art-dir \
  "$HOME/Library/Application Support/Command and Conquer Generals Zero Hour Data/ReforgedArt"
```

Its availability depends on the configured release source and your rights to the
artwork. A normal build does not invoke it. No game data should be committed or included in an
engine-only source archive. A code licence does not establish rights to retail
textures, movies, voices or music.

## Troubleshooting and contribution checks

- Missing `INIZH.big`: select the installed Zero Hour directory, not its parent,
  an installer executable, or the base game directory.
- Missing shared textures/audio: copy the complete Generals directory too.
- Case-sensitive APFS is supported; path tests exercise a temporary APFS image.
- Bare `build-mac/generals` does not use the packaged chooser defaults; pass
  `-root` and `-overlay build-mac/overlay` when launching it directly.
- Build/test logs and diagnostics are useful; do not attach retail archives to
  issues. Use synthetic reproductions for parser bugs.

The normal suite checks window policy on SDL's offscreen driver. To exercise
actual Cocoa fullscreen transitions, Retina dimensions, menu overrides and
display-sleep assertions in a desktop session, run the explicit native check:

```sh
cmake --build build-mac --target macos_window_selfcheck
./build-mac/macos_window_selfcheck
```

It briefly opens a test window and switches Spaces, then closes it. It needs no
game data and is excluded from automatic builds and CTest runs. The actual
Game Mode indicator and the local-network permission dialog still need checking
with the packaged game; the check does not change system permission choices.

Before contributing, run the Release tests and
`python3 GeneralsMD/Code/Tools/check_game_assets.py`. The latter detects common
retail archive/media containers in the Git index. It is a guard against accidental
inclusion, not a rights audit of inherited icons, textures, screenshots or scripts.
Windows and Linux remain supported; shared parser changes must preserve their
formats and behavior too.

The architecture comparison first checks whether both runtime architectures can
start. A nonresponsive canary, including a stalled Rosetta launch, times out after
ten seconds and marks that comparison unavailable. Once the runtimes respond,
the arithmetic comparison still reports differences as failures.

BTree, Huffman and RefPack use bounded readers; the legacy formats remain supported.
For browser control capabilities, runtime GameSpy configuration and the remaining
trusted-relay boundary of the UDP protocol, see [network configuration](network-control.md).
