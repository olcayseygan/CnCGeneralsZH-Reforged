# Build and play on macOS

The native port uses SDL3 and Metal. The deployment target is macOS 13 or later;
build on the Mac you intend to use (Apple silicon or Intel). A universal app
requires separate builds of both architectures; the normal command builds one.

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

Before contributing, run the Release tests and
`python3 GeneralsMD/Code/Tools/check_game_assets.py`. The latter detects common
retail archive/media containers in the Git index. It is a guard against accidental
inclusion, not a rights audit of inherited icons, textures, screenshots or scripts.
Windows and Linux remain supported; shared parser changes must preserve their
formats and behavior too.

The security review found remaining work in legacy BTree/Huffman decoding and
UDP sender authentication. These paths remain available for compatibility; the
bounded RefPack reader does not make the other codecs safe. The optional
`-control` WebSocket also retains its browser interface and has no Origin or
authentication check. Leave it disabled outside a controlled development session.
