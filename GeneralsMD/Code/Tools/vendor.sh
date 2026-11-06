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
# Portions adapted from GeneralsMD/Code/Tools/vendor.ps1 by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.
#
# Fetches the third-party sources the build needs and this repository does not carry.
#
# This is the POSIX half of Tools/vendor.ps1 and deliberately a second implementation rather than
# a shared one: a Python dependency costs more here than the duplication does. Behaviour, paths and
# output are meant to match vendor.ps1 line for line, so read that one too before changing this one.
#
# EA stripped these from the source release and they are not ours to commit, so every fresh clone
# has to get them once. build-posix.sh runs this before it configures; running it again when everything
# is in place costs one directory check per library and nothing else.
#
#   --force   re-fetch even what is already there
#
# What it cannot get is the game itself: the .big files from a Zero Hour install go next to the
# executable in GeneralsMD/Run, and the base game's in Run/ZH_Generals. The game says so on startup
# when they are missing.

set -euo pipefail

force=

while [ $# -gt 0 ]; do
  case "$1" in
    --force|-f) force=1 ;;
    -h|--help) sed -n '3,$p' "$0" | sed -n '/^[^#]/q; s/^#\{1\} \{0,1\}//p'; exit 0 ;;
    *) echo "[vendor] ERROR: unknown argument '$1' (try --force)" >&2; exit 1 ;;
  esac
  shift
done

tools_dir=$(cd "$(dirname "$0")" && pwd)
code_root=$(dirname "$tools_dir")
libraries="$code_root/Libraries"
run_folder="$(dirname "$code_root")/Run"
work="${TMPDIR:-/tmp}/zhr-vendor"
work="${work%/}"

# Progress goes to stderr, not stdout. vendor.ps1 can call Write-Host freely because PowerShell
# keeps the host and the pipeline apart; here a function that both logs and returns a value would
# hand its caller the log line as part of the value, and the first run of this copied an archive
# named "[vendor] downloading lzhl.zip" before the streams were separated.
step() { echo "[vendor] $1" >&2; }

# --- the same three helpers vendor.ps1 has, in the same order ------------------------------------

# Both of these run inside $( ), where bash does not carry set -e, so a failed curl or unzip used to
# be ignored: the function printed its result anyway, the caller copied an empty folder over the
# library and the run ended with "everything the build needs is in place" (measured with a bogus
# commit, 2026-09-25). They return failure explicitly now, which the caller's assignment turns into
# an exit under set -e - the way vendor.ps1's $ErrorActionPreference = 'Stop' already behaved.
# The download goes to a .part file and is renamed only once complete, because an existing file is
# taken as already downloaded, and an interrupted transfer would otherwise be reused on every run.
get_file() { # url destination -> prints destination
  local url="$1" destination="$2"
  if [ -e "$destination" ]; then printf '%s\n' "$destination"; return 0; fi
  mkdir -p "$(dirname "$destination")"
  step "downloading $(basename "$destination")"
  if ! curl -fsSL "$url" -o "$destination.part.$$"; then
    rm -f "$destination.part.$$"
    echo "[vendor] ERROR: could not download $url" >&2
    return 1
  fi
  mv -f "$destination.part.$$" "$destination"
  printf '%s\n' "$destination"
}

# Unpacks into a folder of its own and hands back whatever single directory the archive contained,
# which for a GitHub source zip is the repository at that commit.
expand_source() { # archive name -> prints the unpacked root
  local archive="$1" name="$2" target="$run/$2"
  rm -rf "$target"
  mkdir -p "$target"
  step "unpacking $name"
  case "$archive" in
    *.tar.gz|*.tar.bz2) tar -xf "$archive" -C "$target" || { echo "[vendor] ERROR: could not unpack $archive" >&2; return 1; } ;;
    *)        unzip -qo "$archive" -d "$target" || { echo "[vendor] ERROR: could not unpack $archive" >&2; return 1; } ;;
  esac
  local entry count=0 only=
  for entry in "$target"/*; do
    [ -e "$entry" ] || continue
    count=$((count + 1))
    only="$entry"
  done
  if [ "$count" -eq 1 ] && [ -d "$only" ]; then printf '%s\n' "$only"; else printf '%s\n' "$target"; fi
}

copy_files() { # destination file... 
  local destination="$1"; shift
  mkdir -p "$destination"
  local file
  for file in "$@"; do cp -f "$file" "$destination/$(basename "$file")"; done
}

# The files directly in a folder with one of these extensions, minus the named exceptions. Both
# lists are matched case-insensitively, the way PowerShell's -contains and -in are, because the
# DirectX drop spells its headers in a different case from the script that asks for them.
#
#   list_top_level <folder> <extensions, comma separated> [<excluded names, comma separated>]
list_top_level() {
  local folder="$1" extensions=",$(lower "$2")," excluded=",$(lower "${3:-}"),"
  local file base
  for file in "$folder"/*; do
    [ -f "$file" ] || continue
    base=$(lower "$(basename "$file")")
    case "$base" in *.*) ;; *) continue ;; esac
    case "$extensions" in *",.${base##*.},"*) ;; *) continue ;; esac
    case "$excluded" in *",$base,"*) continue ;; esac
    printf '%s\n' "$file"
  done
}

lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

# shasum is Perl's, and some Linux installs keep it off the PATH (Arch: /usr/bin/core_perl); coreutils'
# sha256sum gives the same digest.  Without either the checks below would fail and delete what they check.
sha256_of() {
  if command -v shasum > /dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}'
  else sha256sum "$1" | awk '{print $1}'; fi
}

# --- zlib 1.1.4, flat. maketree.c is a generator with its own main() and does not belong in the lib.
install_zlib() {
  local destination="$libraries/Source/Compression/ZLib"
  if [ -e "$destination/deflate.c" ] && [ -z "$force" ]; then
    # Still checked on a second run, and on a tree vendor.ps1 filled: zlib that arrived from the
    # PowerShell script is unpatched, and a shared checkout is exactly where that happens.
    patch_zlib_for_apple "$destination"
    patch_zlib_zutil_for_apple "$destination"
    return 0
  fi
  local archive source
  archive=$(get_file 'https://zlib.net/fossils/zlib-1.1.4.tar.gz' "$work/zlib-1.1.4.tar.gz")
  source=$(expand_source "$archive" 'zlib')
  local IFS=$'\n'
  copy_files "$destination" $(list_top_level "$source" '.c,.h' 'maketree.c')
  unset IFS
  patch_zlib_for_apple "$destination"
  patch_zlib_zutil_for_apple "$destination"
  step "zlib 1.1.4 -> Libraries/Source/Compression/ZLib"
}

# zlib 1.1.4 guards its own Byte typedef:
#
#     #if !defined(MACOS) && !defined(TARGET_OS_MAC)
#     typedef unsigned char  Byte;  /* 8 bits */
#     #endif
#
# In 2002 that deferred to Classic Mac OS's MacTypes.h, which defined Byte itself. On a modern Mac
# TARGET_OS_MAC still arrives transitively through Apple's system headers, so the typedef is
# skipped - and MacTypes.h is not what zlib.h ends up including, so nothing supplies it. Every
# translation unit then dies at zconf.h:223, "unknown type name 'Byte'".
#
# No compile definition fixes this: the guard tests defined(), so defining either name skips the
# typedef harder. The file has to change. zlib upstream reached the same conclusion and deleted the
# guard outright in 1.2.0, which is exactly what this does.
#
# Windows is unaffected either way - neither name is ever defined there, so the typedef happens
# before and after. It does mean vendor.ps1 and vendor.sh now leave different bytes in zconf.h;
# that is in docs/porting/windows-impact.md.
# zlib's licence, clause 2: "Altered source versions must be plainly marked as such".  Each of the two
# patches below leaves a comment on the line before the one it changed, and a tree patched before the
# comment existed gets it on the next run: mark_zlib_altered <file> <awk regex of the changed line> <text>.
ZLIB_ALTERED='Altered for Zero Hour Reforged by GeneralsMD/Code/Tools/vendor.sh'
mark_zlib_altered() {
  local file="$1" line="$2" text="$3"
  [ -e "$file" ] || return 0
  grep -qF "$ZLIB_ALTERED" "$file" && return 0
  grep -qE "$line" "$file" || return 0
  awk -v re="$line" -v mark="/* $ZLIB_ALTERED: $text */" '
    !done && $0 ~ re { print mark; done = 1 }
    { print }
  ' "$file" > "$file.marked" && mv -f "$file.marked" "$file"
  grep -qF "$ZLIB_ALTERED" "$file" || { echo "[vendor] ERROR: could not mark $file as altered" >&2; exit 1; }
}

patch_zlib_for_apple() {
  local zconf="$1/zconf.h"
  [ -e "$zconf" ] || return 0
  if ! grep -q 'TARGET_OS_MAC' "$zconf"; then   # already patched, or a zlib that dropped it
    mark_zlib_altered "$zconf" '^typedef unsigned char  Byte;' 'the Classic Mac OS guard around this typedef removed, as zlib 1.2.0 did'
    return 0
  fi

  awk '
    /^#if !defined\(MACOS\) && !defined\(TARGET_OS_MAC\)$/ { dropping = 1; next }
    dropping && /^#endif$/                                    { dropping = 0; next }
    { print }
  ' "$zconf" > "$zconf.patched"

  # Loudly if it did not take. A silently unpatched zconf.h is a wall of "unknown type name 'Byte'"
  # with nothing pointing back at this script.
  if grep -q 'TARGET_OS_MAC' "$zconf.patched" || ! grep -q '^typedef unsigned char  Byte;' "$zconf.patched"; then
    rm -f "$zconf.patched"
    echo "[vendor] ERROR: could not unguard the Byte typedef in $zconf - the guard has moved" >&2
    exit 1
  fi
  mv -f "$zconf.patched" "$zconf"
  mark_zlib_altered "$zconf" '^typedef unsigned char  Byte;' 'the Classic Mac OS guard around this typedef removed, as zlib 1.2.0 did'
  step 'unguarded zlib Byte typedef for Apple (see the comment in this script)'
}

# zutil.h:113 is the same predicate a second time, and it was missed the first time round for a
# reason worth keeping: the compiler stopped at zconf.h, so this one never got a chance to fail.
# Fixing the error the compiler reports is not the same as fixing the file.
#
#     #if defined(MACOS) || defined(TARGET_OS_MAC)
#     #  define OS_CODE  0x07
#     #  ...
#     #    ifndef fdopen
#     #      define fdopen(fd,mode) NULL      /* No fdopen() */
#
# TARGET_OS_MAC arrives transitively here too, so on macOS this branch is live: OS_CODE becomes
# 0x07 and fdopen becomes a macro expanding to NULL - `#ifndef fdopen` is true because fdopen is a
# function, not a macro.  It was written for Classic Mac OS, where there genuinely was no fdopen
# and MWERKS was the compiler; on Darwin, which is Unix, both statements are simply false.
#
# Nothing calls it today: OS_CODE is read only at gzio.c:165 and fdopen only at gzio.c:156, both
# inside the gzip wrapper, and the game calls z_compress2/z_uncompress, which are zlib format.
# gzio.c is compiled and never called.  So this is a landmine rather than a fire - and that is the
# argument for spending six lines on it, not against.  The first caller of gzopen on a Mac would
# get a FILE* built from NULL.
#
# Narrowed rather than deleted, because the Classic Mac branch is still correct for Classic Mac:
# __APPLE__ is defined on Darwin and was not on Mac OS 9 or MWERKS.  Skipping the branch lets
# zutil.h fall through to its own `#ifndef OS_CODE / #define OS_CODE 0x03 /* assume Unix */`,
# which is what Darwin is, and leaves fdopen as the real function.
#
# Windows is untouched: neither MACOS nor TARGET_OS_MAC is ever defined there, so the branch was
# already dead and the condition it is now guarded by is never evaluated.
patch_zlib_zutil_for_apple() {
  local zutil="$1/zutil.h"
  [ -e "$zutil" ] || return 0

  # Matched on meaning rather than on spacing: any #if that tests TARGET_OS_MAC and does not
  # already exclude __APPLE__.  An exact-text match would silently do nothing if the upstream line
  # were ever respelled, which is the same quiet failure this whole patch exists to avoid.
  if ! grep -nE '^[[:space:]]*#[[:space:]]*if.*TARGET_OS_MAC' "$zutil" | grep -qv '__APPLE__'; then
    mark_zlib_altered "$zutil" 'TARGET_OS_MAC.*__APPLE__' '&& !defined(__APPLE__) added, so Darwin is not taken for Classic Mac OS'
    return 0
  fi

  awk '
    /^[[:space:]]*#[[:space:]]*if/ && /TARGET_OS_MAC/ && !/__APPLE__/ {
      sub(/^[[:space:]]*#[[:space:]]*if[[:space:]]*/, "")
      print "#if (" $0 ") && !defined(__APPLE__)"
      next
    }
    { print }
  ' "$zutil" > "$zutil.patched"

  if grep -nE '^[[:space:]]*#[[:space:]]*if.*TARGET_OS_MAC' "$zutil.patched" | grep -qv '__APPLE__'; then
    rm -f "$zutil.patched"
    echo "[vendor] ERROR: could not narrow the Classic Mac branch in $zutil - it has moved" >&2
    exit 1
  fi
  mv -f "$zutil.patched" "$zutil"
  mark_zlib_altered "$zutil" 'TARGET_OS_MAC.*__APPLE__' '&& !defined(__APPLE__) added, so Darwin is not taken for Classic Mac OS'
  step 'narrowed zlib Classic Mac branch to Classic Mac (see the comment in this script)'
}

# --- LZH-Light 1.0. Lzhl_tcp.cpp is a socket layer nothing calls; Test.c has its own main().
install_lzhl() {
  local header="$libraries/Source/Compression/LZHCompress/CompLibHeader"
  local source_folder="$libraries/Source/Compression/LZHCompress/CompLibSource"
  if [ -e "$source_folder/Lzhl.cpp" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/TheSuperHackers/lzhl-1.0/archive/dfd96e2.zip' "$work/lzhl.zip")
  source=$(expand_source "$archive" 'lzhl')
  local IFS=$'\n'
  copy_files "$header" $(list_top_level "$source" '.h')
  copy_files "$source_folder" $(list_top_level "$source" '.cpp,.tbl' 'Lzhl_tcp.cpp,Test.c')
  step "LZH-Light 1.0 -> Libraries/Source/Compression/LZHCompress"
}

# --- The fork's one change to LZH-Light, Libraries/Source/lzhl-clear-history.patch: LZBuffer's history
# starts cleared.  The compressor's backward match reads history it has not written yet (MemorySanitizer,
# Lz.cpp), so the output depended on what the allocator left there.  The game's operator new zero-fills,
# so the game compresses the same bytes as before; anything on another allocator now does too.  The
# licence asks for altered copies to be marked, and the marker is what this checks, as the others do.
install_lzhl_patch() {
  local destination="$libraries/Source/Compression/LZHCompress/CompLibHeader"
  local header="$destination/_lz.h"
  if grep -q 'Zero Hour Reforged: altered' "$header" 2>/dev/null; then return 0; fi
  local patch="$libraries/Source/lzhl-clear-history.patch"
  GIT_CEILING_DIRECTORIES="$libraries/Source" \
    git -C "$destination" -c core.autocrlf=false apply "$patch" || true
  if ! grep -q 'Zero Hour Reforged: altered' "$header" 2>/dev/null; then
    echo "[vendor] lzhl-clear-history.patch did not apply to Libraries/Source/Compression/LZHCompress" >&2
    echo "[vendor] ($header still lacks its marker)" >&2
    exit 1
  fi
  step "lzhl-clear-history.patch -> Libraries/Source/Compression/LZHCompress"
}

# --- The DirectX 8 headers and import libraries are Windows-only and vendor.ps1 keeps them. Nothing
# that compiles on a Mac includes d3d8.h, and the .lib files are MSVC import libraries that no
# toolchain here can link, so fetching them would cost 40 MB to satisfy nobody.
#
# CMakeLists.txt's vendored-sources guard still lists Libraries/DirectX/Include/d3d8.h, so a
# configure on a Mac fails on it until A1 puts that entry behind the same platform guard. That is
# A1's line to move, not this script's.
report_directx() {
  step 'skipping the DirectX 8 SDK: Windows only, and vendor.ps1 is what fetches it'
}

# --- GameSpy SDK, whole repository. It brings its own CMakeLists, which CMakeLists.txt adds.
install_gamespy() {
  local destination="$libraries/Source/GameSpy"
  if [ -e "$destination/CMakeLists.txt" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/TheSuperHackers/GamespySDK/archive/b1b77d8.zip' "$work/gamespy.zip")
  source=$(expand_source "$archive" 'gamespy')
  # Every one of these folders holds a committed .gitignore that keeps the code out of the
  # repository. Emptying the folder first takes that with it, and then the whole SDK shows up as
  # untracked - which is how 780 files of third-party source nearly went into a commit.
  # Moved aside and moved back rather than read and rewritten, so it returns byte for byte. Read
  # into a variable it comes back a trailing newline short, and then the file the whole dance
  # exists to protect shows up as modified in every diff.
  local keep="$destination/.gitignore" kept="$run/gamespy.gitignore"
  rm -f "$kept"
  if [ -e "$keep" ]; then mv "$keep" "$kept"; fi
  rm -rf "$destination"
  mkdir -p "$destination"
  # The trailing /. copies the contents rather than the directory, dot files included.
  cp -Rf "$source/." "$destination/"
  if [ -e "$kept" ]; then mv -f "$kept" "$keep"; fi
  step "GamespySDK -> Libraries/Source/GameSpy"
}

# --- The fork's one change to the GameSpy SDK, Libraries/Source/gamespy-strlwr-c-only.patch.
# gsplatform.h declares _strlwr and _strupr with C linkage for every non-Windows compiler, C++ included.
# Engine C++ already has MSVCCompat.h's inline _strlwr, and a second declaration of the same name
# with a different linkage is ill-formed, so 34 engine files stopped on it.  The patch keeps the
# declarations for GameSpy's own C, which is what gsplatformutil.c defines them for.  Only the
# !_WIN32 half of the header changes, so vendor.ps1 has nothing to apply.
#
# GIT_CEILING_DIRECTORIES is not optional. The SDK sits inside this repository's checkout, and
# without the ceiling git finds the outer repository, treats every file in the patch as outside it,
# skips them all - and says nothing.
#
# And the result is checked by the marker, not by git apply's exit status. The failure above is the
# silent kind; a status check would pass on the one case it exists to catch.
install_gamespy_patch() {
  local destination="$libraries/Source/GameSpy"
  local header="$destination/include/gamespy/gsplatform.h"
  if grep -q 'Zero Hour Reforged: C only' "$header" 2>/dev/null; then return 0; fi
  local patch="$libraries/Source/gamespy-strlwr-c-only.patch"
  GIT_CEILING_DIRECTORIES="$libraries/Source" \
    git -C "$destination" -c core.autocrlf=false apply "$patch" || true
  if ! grep -q 'Zero Hour Reforged: C only' "$header" 2>/dev/null; then
    echo "[vendor] gamespy-strlwr-c-only.patch did not apply to Libraries/Source/GameSpy" >&2
    echo "[vendor] ($header still lacks its marker)" >&2
    exit 1
  fi
  step "gamespy-strlwr-c-only.patch -> Libraries/Source/GameSpy"
}

# --- And its second change, Libraries/Source/gamespy-gsi-unix.patch: the SDK's own platform macro
# renamed from _UNIX to GSI_UNIX, in all 9 files that use it.  gsplatform.h defines it on Linux and
# Apple, and _UNIX is also the switch for Westwood's abandoned port in WWVegas, which the port's
# rules say never to turn on: every engine file that included a GameSpy header had it turned on from
# that point.  After the rename the SDK takes exactly the branches it did - its objects are identical
# - and nothing outside it sees _UNIX.  vendor.ps1 has nothing to apply: Windows never defined it.
install_gamespy_unix_patch() {
  local destination="$libraries/Source/GameSpy"
  local header="$destination/include/gamespy/gsplatform.h"
  if grep -q 'Zero Hour Reforged: GSI_UNIX' "$header" 2>/dev/null; then return 0; fi
  local patch="$libraries/Source/gamespy-gsi-unix.patch"
  GIT_CEILING_DIRECTORIES="$libraries/Source" \
    git -C "$destination" -c core.autocrlf=false apply "$patch" || true
  if ! grep -q 'Zero Hour Reforged: GSI_UNIX' "$header" 2>/dev/null; then
    echo "[vendor] gamespy-gsi-unix.patch did not apply to Libraries/Source/GameSpy" >&2
    echo "[vendor] ($header still lacks its marker)" >&2
    exit 1
  fi
  step "gamespy-gsi-unix.patch -> Libraries/Source/GameSpy"
}

# --- FFmpeg. Not fetched by either script: Libraries/Source/FFmpeg/dist is committed, and it is a
# Windows distribution - .lib import libraries and avcodec-62.dll and friends. A Mac build needs a
# different FFmpeg entirely, and whether that is Homebrew, a vendored dylib or a static build is
# still open.
#
# TODO(C4/D-track): decide where macOS gets FFmpeg from and add the step here. Guessing now would
# pin a choice that C4 has not made, and M1 is a headless build that links neither binkvideo nor
# milesaudio, so nothing before M2 needs it.

# --- The fork's own upscaled art: every 3D texture at twice its size, and the ground. Not in git -
# ReforgedTextures.big alone is a gigabyte, ten times
# what GitHub takes in a file, and LFS in a fork is billed to the parent repository.
#
# It comes from the release channel, the same place a player's launcher takes it from, and the
# channel's own _versions.json carries the sha256 of every file in the newest release. Reading the
# hash from there rather than pinning it here means regenerating the art does not leave this script
# lying.
#
# Two places it can come from, in this order:
#
#   1. the release channel, if this checkout knows one. This repository is public and does not name
#      it: the address comes from ZHR_CHANNEL_URL, or from the launcher checkout beside this one.
#   2. this repository's own art release on GitHub, which is where anyone who just cloned the
#      public repository gets it. art.json there lists each file with its sha256, so the hashes are
#      not pinned in this script and regenerating the art does not leave it lying.
#
# With neither, the step is skipped: the game plays at the textures it shipped with, and
# experiments/doku-upscale is where the art is made.
#
# ReforgedNormals.big, the generated normal maps, is left out: the game stopped reading them on
# 2026-10-06, and a channel release from before that still lists the archive.
art_pattern='Reforged(?!Normals).*\.big$'
art_release='https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/download/art-latest'

# --- SDL3 3.4.16, the whole repository: the window, the events, the entry point and the GPU API on
# every platform that is not Windows (decision 3 in PORTING.md). Windows keeps
# Win32Device, so vendor.ps1 does not fetch it - it says so, the way report_directx does here. Same
# .gitignore dance as GameSpy. Pinned to the release's commit, not its tag, because a tag can move.
install_sdl3() {
  local destination="$libraries/Source/SDL3"
  if [ -e "$destination/CMakeLists.txt" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/libsdl-org/SDL/archive/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c.zip' "$work/SDL3-3.4.16.zip")
  source=$(expand_source "$archive" 'SDL3')
  local keep="$destination/.gitignore" kept="$run/SDL3.gitignore"
  rm -f "$kept"
  if [ -e "$keep" ]; then mv "$keep" "$kept"; fi
  rm -rf "$destination"
  mkdir -p "$destination"
  cp -Rf "$source/." "$destination/"
  if [ -e "$kept" ]; then mv -f "$kept" "$keep"; fi
  if [ ! -e "$destination/include/SDL3/SDL_gpu.h" ]; then
    echo "[vendor] SDL3 unpacked without include/SDL3/SDL_gpu.h - not the tree this build expects" >&2
    exit 1
  fi
  step "SDL3 3.4.16 -> Libraries/Source/SDL3"
}

# --- The fork's first change to SDL3, Libraries/Source/sdl3-metal-windowless.patch: METAL_PrepareDriver
# also accepts ZH_SDL_GPU_METAL_WINDOWLESS, so -offscreen makes a Metal GPU device on a host with no
# window server (the patch's header says why, what upstream has, and when it goes). Hint-gated: every
# run without the hint is upstream's. Checked by the marker, for the reason install_gamespy_patch gives.
install_sdl3_patch() {
  local destination="$libraries/Source/SDL3"
  local source="$destination/src/gpu/metal/SDL_gpu_metal.m"
  if grep -q 'ZH_SDL_GPU_METAL_WINDOWLESS' "$source" 2>/dev/null; then return 0; fi
  local patch="$libraries/Source/sdl3-metal-windowless.patch"
  GIT_CEILING_DIRECTORIES="$libraries/Source" \
    git -C "$destination" -c core.autocrlf=false apply "$patch" || true
  if ! grep -q 'ZH_SDL_GPU_METAL_WINDOWLESS' "$source" 2>/dev/null; then
    echo "[vendor] sdl3-metal-windowless.patch did not apply to Libraries/Source/SDL3" >&2
    echo "[vendor] ($source still lacks ZH_SDL_GPU_METAL_WINDOWLESS)" >&2
    exit 1
  fi
  step "sdl3-metal-windowless.patch -> Libraries/Source/SDL3"
}

# --- miniaudio 0.11.25, the one header and its one implementation file: audio beneath C4's port of
# MilesAudioManager (decision 3). POSIX only, like SDL3. Copied file by file, so its
# committed .gitignore is never disturbed; upstream's CMakeLists builds extras this does not want.
install_miniaudio() {
  local destination="$libraries/Source/miniaudio"
  if [ -e "$destination/miniaudio.c" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/mackron/miniaudio/archive/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d.zip' "$work/miniaudio-0.11.25.zip")
  source=$(expand_source "$archive" 'miniaudio')
  if [ ! -e "$source/miniaudio.h" ] || [ ! -e "$source/miniaudio.c" ]; then
    echo "[vendor] miniaudio 0.11.25 unpacked without miniaudio.h and miniaudio.c" >&2
    exit 1
  fi
  copy_files "$destination" "$source/miniaudio.h" "$source/miniaudio.c" "$source/LICENSE"
  step "miniaudio 0.11.25 -> Libraries/Source/miniaudio"
}

# Replaces a vendored folder's contents with the named entries of an unpacked source, keeping the
# folder's committed .gitignore. For the three shader libraries below, which upstream ship with
# tens of megabytes of test data this build never reads.
#   copy_entries <source> <destination> <entry>...
copy_entries() {
  local source="$1" destination="$2"; shift 2
  local keep="$destination/.gitignore" kept="$run/$(basename "$destination").gitignore"
  rm -f "$kept"
  if [ -e "$keep" ]; then mv "$keep" "$kept"; fi
  rm -rf "$destination"
  mkdir -p "$destination"
  local entry
  for entry in "$@"; do
    if [ ! -e "$source/$entry" ]; then
      echo "[vendor] ERROR: $(basename "$destination") unpacked without $entry" >&2
      exit 1
    fi
    cp -R "$source/$entry" "$destination/"
  done
  if [ -e "$kept" ]; then mv -f "$kept" "$keep"; fi
}

# --- glslang, pinned to vulkan-sdk-1.4.357.0 (168d452a): the HLSL front end that compiles the
# shader generators' SDL3 target to SPIR-V (decision 4 in PORTING.md). POSIX only; the
# Windows build compiles HLSL with d3dcompiler_47.dll and never needs it.
#
# THIS TAG IS PAST THE HLSL FRONT END'S DEPRECATION. Upstream deprecated it in April 2026
# (KhronosGroup/glslang#4210) and will remove it in a future major release, not before about
# October 2027. A bump must check that ENABLE_HLSL still exists at the new commit; decision 4 records
# the way out (DXC through SDL_shadercross, then Slang).
#
# Copied without Test/, gtests/ and the rest of upstream's CI: what the library build reads is the
# sources, the version it takes from CHANGES.md, and StandAlone/ for one header the C interface uses.
install_glslang() {
  local destination="$libraries/Source/glslang"
  if [ -e "$destination/glslang/HLSL/hlslParseHelper.cpp" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/KhronosGroup/glslang/archive/168d452a4f460d24b588fed08477a81c44ee27a1.zip' "$work/glslang-vulkan-sdk-1.4.357.0.zip")
  source=$(expand_source "$archive" 'glslang')
  copy_entries "$source" "$destination" CMakeLists.txt parse_version.cmake CHANGES.md build_info.h.tmpl \
    build_info.py LICENSE.txt LICENSES README.md glslang SPIRV StandAlone
  if [ ! -e "$destination/glslang/HLSL/hlslParseHelper.cpp" ]; then
    echo "[vendor] glslang unpacked without its HLSL front end - not the tree this build expects" >&2
    exit 1
  fi
  step "glslang vulkan-sdk-1.4.357.0 -> Libraries/Source/glslang"
}

# --- SPIRV-Cross at 1a616956, the commit SDL_shadercross 1ff05bec pins as its own submodule, which is
# what the D-spike measured all 49 programs through: SPIR-V to MSL for SDL's Metal backend. POSIX
# only. Copied without reference/, shaders*/ and samples/, which are its test suite.
install_spirv_cross() {
  local destination="$libraries/Source/SPIRV-Cross"
  if [ -e "$destination/spirv_msl.cpp" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/KhronosGroup/SPIRV-Cross/archive/1a6169566c73d3da552748fc372fe2bbb856e46e.zip' "$work/SPIRV-Cross-1a616956.zip")
  source=$(expand_source "$archive" 'SPIRV-Cross')
  local entries=(CMakeLists.txt cmake include pkg-config LICENSE LICENSES README.md GLSL.std.450.h
    NonSemanticShaderDebugInfo100.h)
  local file
  for file in "$source"/spirv*.cpp "$source"/spirv*.hpp "$source"/spirv*.h; do
    entries+=("$(basename "$file")")
  done
  copy_entries "$source" "$destination" "${entries[@]}"
  if [ ! -e "$destination/spirv_msl.cpp" ] || [ ! -e "$destination/spirv_cross_c.h" ]; then
    echo "[vendor] SPIRV-Cross unpacked without spirv_msl.cpp and spirv_cross_c.h" >&2
    exit 1
  fi
  step "SPIRV-Cross 1a616956 -> Libraries/Source/SPIRV-Cross"
}

# --- SDL_shadercross at 1ff05bec (it has no releases): the one C file that turns SPIR-V into an SDL
# GPU shader, MSL on Metal. Only its source, header and licence: CMakeLists.txt builds it as a target
# of its own, without DXC and without upstream's CMake, whose vendored mode insists on DXC's source
# even when DXC is off.
install_shadercross() {
  local destination="$libraries/Source/SDL_shadercross"
  if [ -e "$destination/src/SDL_shadercross.c" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/libsdl-org/SDL_shadercross/archive/1ff05bec573988a98ef9e0260b4da44f512b8367.zip' "$work/SDL_shadercross-1ff05bec.zip")
  source=$(expand_source "$archive" 'SDL_shadercross')
  copy_entries "$source" "$destination" LICENSE.txt README.txt
  mkdir -p "$destination/src" "$destination/include/SDL3_shadercross"
  cp -f "$source/src/SDL_shadercross.c" "$destination/src/"
  cp -f "$source/include/SDL3_shadercross/SDL_shadercross.h" "$destination/include/SDL3_shadercross/"
  step "SDL_shadercross 1ff05bec -> Libraries/Source/SDL_shadercross"
}

# --- FreeType 2.14.3 (released 2026-03-22; the newest stable tag on 2026-09-26), pinned to the
# release's commit 0a0221a1: the glyph rasteriser under render2dsentence off Windows (decision 6, D6).
# POSIX only; Windows draws text with GDI. Its TrueType interpreter v35 is what reproduces GDI's
# advance widths (D6). Copied without tests/, subprojects/ and the other build systems.
install_freetype() {
  local destination="$libraries/Source/freetype"
  if [ -e "$destination/src/truetype/ttinterp.c" ] && [ -z "$force" ]; then return 0; fi
  local archive source
  archive=$(get_file 'https://github.com/freetype/freetype/archive/0a0221a1347e2f1e07c395263540026e9a0aa7c7.zip' "$work/freetype-2.14.3.zip")
  source=$(expand_source "$archive" 'freetype')
  copy_entries "$source" "$destination" CMakeLists.txt builds include src LICENSE.TXT README docs
  if ! grep -q 'define FREETYPE_PATCH  3' "$destination/include/freetype/freetype.h"; then
    echo "[vendor] freetype unpacked, but not as 2.14.3 - not the tree this build expects" >&2
    exit 1
  fi
  step "FreeType 2.14.3 -> Libraries/Source/freetype"
}

# --- FFmpeg 8.1.2, the release Windows' dist/ is built from (Tools/ffmpeg-build.sh): the decoder
# under the Bink API off Windows (V1). Only the tarball is kept, checked against its published hash;
# CMake's POSIX build unpacks it into the build tree, builds the few components the movies need
# (Tools/ffmpeg-build-posix.sh), and deletes the unpacked tree again - 11.7MB here instead of 110MB.
install_ffmpeg() {
  local destination="$libraries/Source/FFmpeg/ffmpeg-8.1.2.tar.xz"
  local sha='464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c'
  if [ -e "$destination" ] && [ -z "$force" ] && [ "$(sha256_of "$destination")" = "$sha" ]; then return 0; fi
  rm -f "$destination"
  local archive hash
  archive=$(get_file 'https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz' "$work/ffmpeg-8.1.2.tar.xz")
  hash=$(sha256_of "$archive")
  if [ "$hash" != "$sha" ]; then
    rm -f "$archive"
    echo "[vendor] ERROR: ffmpeg-8.1.2.tar.xz has hash $hash, and the release was published as $sha" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$destination")"
  cp -f "$archive" "$destination"
  step "FFmpeg 8.1.2 (source tarball) -> Libraries/Source/FFmpeg"
}

# --- mbedTLS 3.6.7 (Apache-2.0), the long-term-support line: the TLS under the online service's lobby
# connection (GameNetwork/Online), the same pin as vendor.ps1, on every platform. The tarball is checked
# against the sha256 the release publishes beside it. Only include/, library/ and the licence are copied;
# CMakeLists.txt compiles library/*.c itself, so upstream's build, programs and tests stay out.
install_mbedtls() {
  local destination="$libraries/Source/mbedtls"
  if [ -e "$destination/include/mbedtls/build_info.h" ] && [ -z "$force" ] \
     && grep -q 'MBEDTLS_VERSION_STRING         "3.6.7"' "$destination/include/mbedtls/build_info.h"; then return 0; fi
  local sha='a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6'
  local archive source hash
  archive=$(get_file 'https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2' "$work/mbedtls-3.6.7.tar.bz2")
  hash=$(sha256_of "$archive")
  if [ "$hash" != "$sha" ]; then
    rm -f "$archive"
    echo "[vendor] ERROR: mbedtls-3.6.7.tar.bz2 has hash $hash, and the release was published as $sha" >&2
    exit 1
  fi
  source=$(expand_source "$archive" 'mbedtls')
  copy_entries "$source" "$destination" include library LICENSE
  step "mbedTLS 3.6.7 -> Libraries/Source/mbedtls"
}

get_channel_url() {
  if [ -n "${ZHR_CHANNEL_URL:-}" ]; then printf '%s/\n' "${ZHR_CHANNEL_URL%/}"; return 0; fi
  local launcher
  launcher="$(dirname "$(dirname "$(dirname "$code_root")")")/launcher/update.js"
  if [ -e "$launcher" ]; then
    local url
    url=$(sed -n "s/.*CHANNEL_URL[[:space:]]*=[[:space:]]*'\([^']*\)'.*/\1/p" "$launcher" | head -n 1)
    if [ -n "$url" ]; then printf '%s/\n' "${url%/}"; return 0; fi
  fi
  return 0
}

# Each source hands back the same shape: one line per file, name, url, size and sha256, tab
# separated. The two manifests are JSON and sh does not read JSON, so python3 does that part - the
# one from the command line tools, not a Homebrew one. Without it the art step is skipped and says
# so, which is the same outcome as an unreachable channel and equally survivable.
have_python() { command -v python3 >/dev/null 2>&1; }

get_art_from_channel() {
  local channel_url
  channel_url=$(get_channel_url)
  [ -n "$channel_url" ] || return 0
  local versions
  if ! versions=$(curl -fsSL "${channel_url}_versions.json" 2>/dev/null); then
    step 'the release channel is not reachable'
    return 0
  fi
  printf '%s' "$versions" | python3 -c '
import json, re, sys
channel, pattern = sys.argv[1], re.compile(sys.argv[2])
release = json.load(sys.stdin)["game"][0]
for f in release["files"]:
    if pattern.search(f["path"]):
        path = f["path"].replace("\\", "/")
        name = path.rsplit("/", 1)[-1]
        print("\t".join([name, channel + release["folder"] + "/" + path,
                         str(f["size"]), f["sha256"]]))
' "$channel_url" "$art_pattern"
}

get_art_from_release() {
  local manifest
  if ! manifest=$(curl -fsSL "$art_release/art.json" 2>/dev/null); then return 0; fi
  printf '%s' "$manifest" | python3 -c '
import json, re, sys
base, pattern = sys.argv[1], re.compile(sys.argv[2])
for f in json.load(sys.stdin)["files"]:
    if pattern.search(f["name"]):
        print("\t".join([f["name"], base + "/" + f["name"], str(f["size"]), f["sha256"]]))
' "$art_release" "$art_pattern"
}

install_art() {
  if ! have_python; then
    step 'no python3, so the art manifest cannot be read - skipping the upscaled art'
    return 0
  fi
  local wanted
  wanted=$(get_art_from_channel)
  if [ -z "$wanted" ]; then wanted=$(get_art_from_release); fi
  if [ -z "$wanted" ]; then
    step 'no upscaled art is published yet, so the game will use the textures it shipped with'
    return 0
  fi

  mkdir -p "$run_folder"
  local name url size sha target partial hash
  while IFS=$'\t' read -r name url size sha; do
    [ -n "$name" ] || continue
    target="$run_folder/$name"
    if [ -e "$target" ] && [ -z "$force" ] && [ "$(sha256_of "$target")" = "$(lower "$sha")" ]; then
      continue
    fi
    partial="$target.part"
    step "downloading $name ($(( (size + 524288) / 1048576 )) MB)"
    curl -fsSL "$url" -o "$partial"
    hash=$(sha256_of "$partial")
    # Loudly, and without leaving the half-file behind: art that is quietly wrong is a game that
    # looks subtly wrong an hour later, with nothing to point at.
    if [ "$hash" != "$(lower "$sha")" ]; then
      rm -f "$partial"
      echo "[vendor] ERROR: $name downloaded with hash $hash, and it was published as $sha" >&2
      exit 1
    fi
    mv -f "$partial" "$target"
    step "$name -> Run"
  done <<EOF
$wanted
EOF
}

mkdir -p "$work"
# Downloads are shared through $work: each is complete once renamed, and a .part carries this run's pid.
# Everything unpacked or set aside goes in a folder of this run's own. Two vendor.sh runs on one
# machine (two worktrees, or a gate's two legs on one Mac) used to unpack into the same
# $work/<name> and delete each other's files halfway.
run=$(mktemp -d "$work/run.XXXXXX")
trap 'rm -rf "$run"' EXIT
install_zlib
install_lzhl
install_lzhl_patch
report_directx
install_gamespy
install_gamespy_patch
install_gamespy_unix_patch
install_sdl3
install_sdl3_patch
install_miniaudio
install_glslang
install_spirv_cross
install_shadercross
install_freetype
install_ffmpeg
install_mbedtls
install_art
step 'everything the build needs is in place'
