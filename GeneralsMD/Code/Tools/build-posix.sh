#!/usr/bin/env bash
#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# The build behind build-macos.sh and build-linux.sh at the repository root, which are the commands to
# run; this is the POSIX half of build.bat, with the same arguments after the platform:
#
#   build-posix.sh <macos|linux> [clean] [Release|RelWithDebInfo|Debug] [test|<target>]
#
# From a fresh clone it checks the toolchain and names what to install when something is missing,
# fetches the third-party dependency sources (vendor.sh), configures, builds,
# runs ctest when asked, and stages a game that runs:
#
#   macOS   build-mac/Zero Hour Reforged.app (the macos_app target)
#   Linux   build-linux/ZeroHourReforged/, the layout of the Linux package: bin/generals, and the staged
#           overlay at share/zero-hour-reforged/overlay, a link into the build tree
#
# Either one finds the player's Zero Hour by itself, or asks for its folder on the first start. The
# *.big stay where the install has them.
#
# The knobs below are overrides and every one of them empty is the supported path. A machine that
# does need one puts its own lines in build.local.sh at the repository root, which is git-ignored and
# read right after these defaults, rather than editing a tracked file.

set -euo pipefail

# Explicit environment overrides take precedence over build.local.sh defaults.
zh_environment_cmake=${CMAKE:-}
zh_environment_build=${BUILD:-}
CMAKE=${CMAKE:-}
GENERATOR=${GENERATOR:-Ninja}
DEFAULT_CONFIG=Release
BUILD=${BUILD:-}

PLATFORM="${1:-}"
[ $# -eq 0 ] || shift
ROOT=$(CDPATH= cd "$(dirname "$0")/../../.." && pwd)

fail() { echo "[build] ERROR: $1" >&2; exit 1; }
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

case "$PLATFORM" in
  macos) [ "$(uname -s)" = Darwin ] || fail "build-macos.sh builds on macOS; on Linux run build-linux.sh." ;;
  linux) [ "$(uname -s)" = Linux ] || fail "build-linux.sh builds on Linux; on macOS run build-macos.sh." ;;
  *) fail "usage: $0 <macos|linux> [clean] [Release|RelWithDebInfo|Debug] [test|<target>]" ;;
esac

if [ -e "$ROOT/build.local.sh" ]; then
  echo "[build] reading build.local.sh"
  # shellcheck source=/dev/null
  . "$ROOT/build.local.sh"
fi

[ -z "$zh_environment_cmake" ] || CMAKE=$zh_environment_cmake
[ -z "$zh_environment_build" ] || BUILD=$zh_environment_build

SRC="$ROOT/GeneralsMD/Code"
if [ "$PLATFORM" = macos ]; then : "${BUILD:=$ROOT/build-mac}"; else : "${BUILD:=$ROOT/build-linux}"; fi
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
TEST_ONLY=${ZH_TEST_ONLY:-0}
case "$TEST_ONLY" in 0|1) ;; *) fail "ZH_TEST_ONLY must be 0 or 1." ;; esac

CONFIG="${1:-}"
ARG2="${2:-}"
CLEAN_BUILD=0
if [ "$(lower "$CONFIG")" = clean ]; then
  [ "$TEST_ONLY" = 0 ] || fail "test-only cannot clean the build tree."
  CLEAN_BUILD=1
  CONFIG="$ARG2"
  ARG2="${3:-}"
fi
[ -n "$CONFIG" ] || CONFIG="$DEFAULT_CONFIG"

RUNTESTS=
TARGET=
if [ "$(lower "$ARG2")" = test ]; then
  RUNTESTS=1
elif [ -n "$ARG2" ]; then
  TARGET="$ARG2"
fi

# One validated worker limit is shared by CMake and its nested FFmpeg build.
# shellcheck source=build-jobs.sh
. "$SRC/Tools/build-jobs.sh"
BUILD_JOBS=$(zh_resolve_build_jobs "${ZH_BUILD_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-auto}}") || exit 2
TEST_JOBS=$(zh_resolve_test_jobs "${ZH_TEST_JOBS:-${CTEST_PARALLEL_LEVEL:-auto}}" "$BUILD_JOBS") || exit 2
export ZH_BUILD_JOBS="$BUILD_JOBS" CMAKE_BUILD_PARALLEL_LEVEL="$BUILD_JOBS"
export ZH_TEST_JOBS="$TEST_JOBS" CTEST_PARALLEL_LEVEL="$TEST_JOBS"
desired_personnel=${ZH_TEXT_PERSONNEL:-unchanged}
desired_factions=${ZH_TEXT_FACTIONS:-unchanged}
case "$desired_personnel" in unchanged|humans|robots) ;; *) fail "ZH_TEXT_PERSONNEL must be unchanged, humans or robots." ;; esac
case "$desired_factions" in unchanged|original|adapted) ;; *) fail "ZH_TEXT_FACTIONS must be unchanged, original or adapted." ;; esac
if [ "$CLEAN_BUILD" = 1 ]; then
  echo "[build] removing $BUILD"
  rm -rf "$BUILD"
fi

resolve_executable() {
  local candidate=$1
  case "$candidate" in
    */*) [ -x "$candidate" ] && printf '%s\n' "$candidate" ;;
    *) command -v "$candidate" 2>/dev/null ;;
  esac
}

# The interactive installer opts in. Existing build/CI entry points remain unattended.
# Prompt text and yes-words come from install_i18n.py (English, plus installer_locales/*.json).
guided_step() {
  [ "${ZH_GUIDED_BUILD:-0}" = 1 ] || return 0
  local status=0
  python3 "$SRC/Tools/install_i18n.py" confirm "$@" || status=$?
  case "$status" in
    0|1) return "$status" ;;
    *) exit "$status" ;;
  esac
}

run_tests() {
  local cached_ctest= resolved_cmake=
  CTEST=${CTEST:-}
  if [ -z "$CTEST" ] && [ -n "$CMAKE" ]; then
    resolved_cmake=$(resolve_executable "$CMAKE" || true)
    if [ -n "$resolved_cmake" ] && [ -x "$(dirname "$resolved_cmake")/ctest" ]; then
      CTEST="$(dirname "$resolved_cmake")/ctest"
    fi
  fi
  if [ -z "$CTEST" ] && [ -f "$BUILD/CMakeCache.txt" ]; then
    cached_ctest=$(sed -n 's/^CMAKE_CTEST_COMMAND:[^=]*=\(.*\)$/\1/p' "$BUILD/CMakeCache.txt" | head -n 1)
    [ ! -x "$cached_ctest" ] || CTEST=$cached_ctest
  fi
  [ -n "$CTEST" ] || CTEST=ctest
  CTEST=$(resolve_executable "$CTEST") || fail "ctest not found; install CMake or set CTEST to its executable."
  if guided_step "Run the full test suite (some tests need a GPU or your own game data)?"; then
    echo "[build] running $CTEST with $TEST_JOBS workers"
    echo "[build] Large engine and packaging checks can take several minutes."
    "$CTEST" --test-dir "$BUILD" --parallel "$TEST_JOBS" --output-on-failure --no-tests=error || fail "tests failed."
  fi
}

echo "[build] config: $CONFIG"
echo "[build] tree:   $BUILD ($GENERATOR)"
echo "[build] workers: build=$BUILD_JOBS, tests=$TEST_JOBS"
if [ "$TEST_ONLY" = 1 ]; then
  [ -n "$RUNTESTS" ] && [ -z "$TARGET" ] || fail "test-only requires a configuration followed by test."
  [ -f "$BUILD/CMakeCache.txt" ] || fail "no configured build at $BUILD; run make build first."
  cached_config=$(sed -n 's/^CMAKE_BUILD_TYPE:[^=]*=\(.*\)$/\1/p' "$BUILD/CMakeCache.txt" | head -n 1)
  [ "$cached_config" = "$CONFIG" ] || fail "build is configured for ${cached_config:-an unknown configuration}, not $CONFIG; build that configuration first."
  run_tests
  exit 0
fi

# --- the toolchain. cmake's own failure ("No CMAKE_CXX_COMPILER could be found", "could not find X11
# or Wayland development libraries") sends people looking in the wrong place, so what is missing is
# named here, with the command that installs it. ---
missing=
need() { for tool in "$@"; do command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"; done; }

if [ "$PLATFORM" = macos ]; then
  xcode-select -p >/dev/null 2>&1 || fail "the Xcode command line tools are not installed. Run: xcode-select --install"
  need git curl unzip tar make ninja
  [ -n "$CMAKE" ] || command -v cmake >/dev/null 2>&1 || [ -x /Applications/CMake.app/Contents/bin/cmake ] \
    || missing="$missing cmake"
  [ -z "$missing" ] || fail "missing:$missing
[build]        Install them with Homebrew (https://brew.sh): brew install cmake ninja"
else
  need cc c++ git curl unzip tar xz make ninja pkg-config
  [ -n "$CMAKE" ] || need cmake
  # GNU ld cannot link the engine's cyclic static libraries; CMakeLists.txt picks LLD, else mold.
  command -v ld.lld >/dev/null 2>&1 || command -v ld.mold >/dev/null 2>&1 || command -v mold >/dev/null 2>&1 \
    || missing="$missing lld"
  # Tools/widechar_check.py reads the engine as POSIX compiles it through unifdef.
  [ -z "$RUNTESTS" ] || need unifdef
  # The X11, Wayland, EGL, DRM and Vulkan headers are SDL3's (the list Tools/linux-check.sh builds with);
  # fontconfig is the glyph rasteriser's.
  if command -v pkg-config >/dev/null 2>&1; then
    for module in x11 xext xcursor xi xfixes xrandr xscrnsaver xtst wayland-client xkbcommon egl libdrm gbm vulkan fontconfig; do
      pkg-config --exists "$module" || missing="$missing $module"
    done
  fi
  if [ -n "$missing" ]; then
    apt="sudo apt-get install cmake ninja-build g++ git curl unzip xz-utils make pkg-config python3 libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev wayland-protocols libegl-dev libdrm-dev libgbm-dev libvulkan-dev libfontconfig-dev lld unifdef"
    dnf="sudo dnf install cmake ninja-build gcc-c++ git curl unzip xz make pkgconf-pkg-config python3 libX11-devel libXext-devel libXcursor-devel libXi-devel libXfixes-devel libXrandr-devel libXScrnSaver-devel libXtst-devel wayland-devel libxkbcommon-devel wayland-protocols-devel mesa-libEGL-devel libdrm-devel mesa-libgbm-devel vulkan-loader-devel fontconfig-devel lld unifdef"
    pacman="sudo pacman -S --needed cmake ninja gcc git curl unzip xz make pkgconf python libx11 libxext libxcursor libxi libxfixes libxrandr libxss libxtst wayland libxkbcommon wayland-protocols mesa libdrm vulkan-icd-loader vulkan-headers fontconfig lld unifdef"
    distro=$( (. /etc/os-release 2>/dev/null && echo "${ID:-} ${ID_LIKE:-}") || true)
    case " $distro " in
      *" debian "*|*" ubuntu "*) how="$apt" ;;
      *" fedora "*|*" rhel "*) how="$dnf" ;;
      *" arch "*) how="$pacman" ;;
      *) how="Debian or Ubuntu: $apt
[build]        Fedora: $dnf
[build]        Arch: $pacman" ;;
    esac
    fail "missing:$missing
[build]        $how"
  fi
fi

# --- locate cmake: the override, then PATH, then common macOS installers ---
if [ -n "$CMAKE" ]; then
  requested_cmake=$CMAKE
  CMAKE=$(resolve_executable "$CMAKE") || fail "CMAKE is set to \"$requested_cmake\", but no executable was found."
else
  CMAKE=$(command -v cmake 2>/dev/null || true)
  for try in /Applications/CMake.app/Contents/bin/cmake /opt/homebrew/bin/cmake /usr/local/bin/cmake; do
    if [ -z "$CMAKE" ] && [ -x "$try" ]; then CMAKE=$try; fi
  done
fi
[ -n "$CMAKE" ] || fail "cmake not found."
echo "[build] cmake:  $CMAKE"

# --- third-party sources the repository does not carry. Fetches whatever is
# missing and is a no-op once it is there. ---
guided_step "Download missing third-party sources and unpack them locally (no original game data)?" || exit 0
"$SRC/Tools/vendor.sh" || fail "fetching the third-party sources failed."

# --- configure ---
# Ninja is single-config, so unlike build.bat the configuration is baked in at configure time and
# --config is ignored at build time. Switching from Release to Debug in an existing tree therefore has
# to configure again, or it silently rebuilds the configuration that is already there.
cached_config=
cached_tuning=
cached_personnel=
cached_factions=
desired_tuning=${ZH_MACOS_CPU_TUNING:-auto}
if [ -e "$BUILD/CMakeCache.txt" ]; then
  cached_tuning=$(sed -n 's/^ZH_MACOS_CPU_TUNING:[^=]*=\(.*\)$/\1/p' "$BUILD/CMakeCache.txt" | head -n 1)
  cached_config=$(sed -n 's/^CMAKE_BUILD_TYPE:[^=]*=\(.*\)$/\1/p' "$BUILD/CMakeCache.txt" | head -n 1)
  cached_personnel=$(sed -n 's/^ZH_TEXT_PERSONNEL:[^=]*=\(.*\)$/\1/p' "$BUILD/CMakeCache.txt" | head -n 1)
  cached_factions=$(sed -n 's/^ZH_TEXT_FACTIONS:[^=]*=\(.*\)$/\1/p' "$BUILD/CMakeCache.txt" | head -n 1)
fi
if [ ! -e "$BUILD/CMakeCache.txt" ] || [ ! -e "$BUILD/build.ninja" ] || [ "$cached_config" != "$CONFIG" ] || [ "$cached_tuning" != "$desired_tuning" ] || [ "$cached_personnel" != "$desired_personnel" ] || [ "$cached_factions" != "$desired_factions" ]; then
  guided_step "Configure CMake and write build files to %s?" "$BUILD" || exit 0
  echo "[build] configuring $SRC -> $BUILD"
  "$CMAKE" -S "$SRC" -B "$BUILD" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$CONFIG" -DZH_MACOS_CPU_TUNING="$desired_tuning" \
    -DZH_TEXT_PERSONNEL="$desired_personnel" -DZH_TEXT_FACTIONS="$desired_factions" || fail "configure failed."
fi

# --- build ---
guided_step "Build the %s code with the native compiler in %s?" "$CONFIG" "$BUILD" || exit 0
if [ -n "$TARGET" ]; then
  echo "[build] building target $TARGET"
  "$CMAKE" --build "$BUILD" --parallel "$BUILD_JOBS" --target "$TARGET" || fail "build failed."
  echo "[build] done: $TARGET in $BUILD"
  exit 0
fi
"$CMAKE" --build "$BUILD" --parallel "$BUILD_JOBS" || fail "build failed."

# --- tests ---
if [ -n "$RUNTESTS" ]; then run_tests; fi

# --- the game, staged where it runs ---
guided_step "Create/refresh the application package in %s (on Linux only the previous package in this build folder is replaced)?" "$BUILD" || exit 0
if [ "$PLATFORM" = macos ]; then
  "$CMAKE" --build "$BUILD" --parallel "$BUILD_JOBS" --target macos_app || fail "the app bundle failed."
  echo "[build] done: open \"$BUILD/Zero Hour Reforged.app\""
else
  package="$BUILD/ZeroHourReforged"
  rm -rf "$package"
  mkdir -p "$package/bin" "$package/share/zero-hour-reforged"
  # Generals' icon and add-to-menu.sh, which puts the folder in the app menu with it
  for size in 48 128; do
    mkdir -p "$package/share/icons/hicolor/${size}x${size}/apps"
    python3 "$SRC/Tools/generals-icon.py" png "$SRC/Main/Generals.ico" \
      "$package/share/icons/hicolor/${size}x${size}/apps/zero-hour-reforged.png" $size || fail "cannot make the icon."
  done
  cp "$SRC/Tools/linux-menu-entry.sh" "$package/add-to-menu.sh"
  cp "$BUILD/generals" "$package/bin/generals"
  # a copy, not a link into the build tree, so the folder runs from wherever it is moved
  cp -R "$BUILD/overlay" "$package/share/zero-hour-reforged/overlay"
  echo "[build] done: run $package/bin/generals"
fi
echo "[build] It finds your Zero Hour in the usual places, or asks for its folder on the first start: the one"
echo "[build] holding INIZH.big, with the original Generals in ZH_Generals inside it or beside it."
echo "[build] Nothing is copied: the game reads the *.big where your install keeps them."
