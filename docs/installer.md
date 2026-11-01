# Guided setup on macOS and Linux

English · [Deutsch](installer.de.md)

Run this in the repository:

```sh
make install
```

`./GeneralsMD/Code/Tools/install-posix.sh` does the same without Make. Python 3.8
or newer is required. On a fresh Mac, install Apple's Command Line Tools first
(`xcode-select --install`); if Python is missing, the entry point says so before
doing anything else. Plain `make` lists the available commands.

The installer asks before every step. **Enter means no**, Ctrl+C cancels. A
terminal is required; there is deliberately no automatic yes. Steps that already
finished are kept if you cancel.

Messages follow `LC_ALL`, `LC_MESSAGES` or `LANG`; override them with
`ZH_INSTALL_LANG=en` (or `de`). Untranslated languages fall back to English.

1. Check the existing tools locally. Missing tools can be installed through an
   existing Homebrew, apt, dnf or pacman. The exact command is shown before the
   question; system packages may need your administrator password. Homebrew
   itself is never installed automatically.
2. Hardware-specific optimizations (for example CPU scheduling on Apple Silicon)
   are detected automatically and never asked about. The instruction baseline
   stays compatible with other machines of the same architecture. Opt out with
   `CPU_TUNING=generic`.
3. Download missing third-party sources, configure CMake, compile, run the tests
   and package the application. **Each of these actions has its own question.**
   If a required build step is declined, that build ends; setup can continue with
   existing game data.
4. Optionally search for your already installed original games. Read: Steam
   libraries including `libraryfolders.vdf`, personal game folders and known Wine,
   CrossOver, Whisky and Bottles directories. The search follows no subdirectory
   links and has a limit; further folders can be entered manually. No whole disk
   is scanned and no game is downloaded from Steam or GOG.
5. Pick the found folders or enter paths manually. Zero Hour with `INIZH.big` and
   Generals with `Textures.big` are required. The selected installations can be
   used in place or copied completely into a **new** personal data folder. Both
   sources and the destination are shown again before copying. Generals goes into
   `ZH_Generals/`. Existing destinations are never overwritten. Symbolic links in
   the data to copy are rejected; use a complete local copy.
6. Optionally copy the Mac app to `~/Applications/`, or add a personal menu entry
   on Linux. An existing Mac app is not replaced; the package in the build folder
   stays usable.
7. Optionally launch the game with the chosen Zero Hour path. If Generals lives
   elsewhere, the application can ask for its folder at start.

Original files must not be copied into this repository. The installer also checks
for paths that reach it through symbolic links. Sources are never modified. After a
copy error the original installations are untouched and temporary working copies
are cleaned up. An interruption during the final move can leave an incomplete new
destination; pick a new folder when repeating, or use your unchanged sources.

For your own Steam data and current GOG availability see
[the guide to the original games](macos.md#obtain-your-own-generals-and-zero-hour-data).
The installer never asks for Steam/GOG passwords and downloads no EA archives,
movies, music or voice files. The GPL release of the existing code and its
copyright notices remain separate from this.

The direct build entry points remain available:

```sh
make build                       # build and package Release
make test                        # build, run every test, then package
make test-only                   # repeat tests in an existing configured build
make install CONFIG=Debug        # the guided build also honors CONFIG
make install JOBS=8 TEST_JOBS=4
make test-only TEST_JOBS=1        # run sequentially if needed
make build CPU_TUNING=generic     # portable scheduling instead of automatic CPU tuning
```

Build workers are chosen from CPU count and memory, capped at eight by default.
Tests use at most four workers automatically; `JOBS` and `TEST_JOBS` accept `auto`
or a positive integer. A job count controls the backend, while `make -j` does not
start two workflows against the same build tree or terminal simultaneously.
Existing CMake/CTest parallel-level environment settings are honored unless the
corresponding Make variable is explicitly set. The nested FFmpeg build uses the
same build-worker limit.

`BUILD=/path/to/build` selects a different build directory, including for the
application the installer offers to launch. `CMAKE=/path/to/cmake` selects the
CMake executable. Both also work as environment overrides of `build.local.sh`.
For example:

```sh
make install CONFIG=Debug BUILD="$HOME/Builds/Zero Hour Debug"
make test-only CONFIG=Debug BUILD="$HOME/Builds/Zero Hour Debug"
```

`test-only` runs the entire existing suite without fetching dependencies,
configuring, compiling or packaging. The directory must already be configured
for the requested `CONFIG`; after changing source files use `make test` to rebuild
before testing. The test timeout and resource locks are retained. Large engine
suites can take several minutes even when no output appears from that one test.
A `Skipped` result usually means original game data or an external reference tool
is missing; it is not a failed test or proof that this case was exercised. See
[the test guide in README](../README.md#how-it-is-tested) for data-dependent checks.

These direct commands do not prompt before each build step, which suits developers
and CI. `make install` keeps every confirmation. Questions are printed as complete,
flushed lines before reading an answer; child diagnostics share the same output
stream so the question cannot lag behind a later build message. `build.bat` and
the Windows builds are unchanged.

## Adding a language

Messages are English in the source. A translation is one JSON file,
`GeneralsMD/Code/Tools/installer_locales/<language>.json`, with the yes-words, the
`[y/N]` hint and a `messages` table that maps each English message to its
translation. Keep every `%s`/`%d` placeholder, in the same order. `de.json` is a
complete example; `Tests/test_installer.py` fails if a catalog misses a message or
changes a placeholder. Answers `y`/`yes` are always accepted in addition to the
language's own.

Missing or malformed catalogs fall back to English. An invalid translated format
also uses the English message with its original arguments. Enter, `n` and `no`
never authorize a step, even if a catalog accidentally lists them as yes-words.
