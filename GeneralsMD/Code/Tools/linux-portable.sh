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
# linux-portable.sh: the portable Linux build (P3) - one folder that runs
# on the Steam Deck's SteamOS and any desktop Linux of the last five years, with nothing installed.
#
#   <out>/zero-hour-reforged.sh                   the launcher: runs bin/generals with the player's arguments (a .sh:
#                                                 Steam's Add a Non-Steam Game picker takes .sh, .exe, .application
#                                                 or an AppImage, never a bare executable)
#   <out>/bin/generals                            stripped; bin/generals.debug beside it (a GNU debuglink)
#   <out>/share/zero-hour-reforged/overlay/       the staged overlay (zh_overlay), its art copied in
#   <out>/share/zero-hour-reforged/licenses/      from macos-app-licenses.txt, checked against the link line
#   <out>/share/applications/, share/icons/       a .desktop file and the icon (Main/Generals.ico's 48 px, and 128 px)
#   <out>/VERSION, <out>/README.txt               which build this is; how to install it and point it at Zero Hour
#   <out>.tar.zst                                 the folder, packed (not with --no-tar)
#   <out>.AppImage                                the same folder as one AppImage (with --appimage), without the debug
#                                                 file: AppRun is the launcher, the .desktop file and icon at its root,
#                                                 a type-2 runtime
#
# THE BUILD runs inside Valve's Steam Runtime 3 "sniper" SDK container (Debian 11, glibc 2.31, g++-14, mold),
# as the user running this, with the repository, the build folder and CMake mounted at their own paths.
# libstdc++ and libgcc are linked statically, so the Steam Deck's C++ runtime version does not matter.
#
# Refused, before the folder is written:
#   - generals needing a glibc symbol newer than GLIBC_2.31, or any libstdc++ symbol (GLIBCXX_, CXXABI_);
#   - generals needing a shared library outside the ones every SteamOS and desktop Linux has (glibc's own,
#     and fontconfig); everything else is static or loaded at run time by SDL and miniaudio;
#   - a static library on generals' link line that macos-app-licenses.txt does not name;
#   - a file in the folder that forces the HUD overlay on (off by default in Release: a project rule).
#
# Usage: linux-portable.sh --build <folder> --out <folder> --cmake <cmake>
#          [--image <sdk image>] [--jobs <n>] [--no-art] [--no-tar] [--no-build]
#          [--appimage <appimagetool> --runtime <type-2 runtime>]
#   --build     the build folder (made if missing); kept between runs, so a second build is incremental
#   --out       the folder to make; its name is the package's (e.g. .../ZeroHourReforged-linux-x86_64)
#   --cmake     a Linux CMake of 3.29 or later that runs inside the container: the SDK's own 3.25 cannot
#               configure this project, and a distribution's CMake needs its distribution's glibc.
#               Kitware's release tarball (cmake-3.31.6-linux-x86_64, or -aarch64 on arm64) is what P3 uses
#   --image     default registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest, or sniper/sdk/arm64:latest
#               on an arm64 host (the Steam Frame's architecture); the folder is built for the image's
#   --jobs      the build's parallelism (default: ZH_BUILD_JOBS, else the CPU count)
#   --no-art    leaves the 1.6 GB of Reforged*.big art out (the game then looks as ClassicGraphics does)
#   --no-build  stages from what <build> already holds
#   --appimage  also makes <out>.AppImage with that appimagetool (its extracted AppRun is fine), from a copy of
#               the folder made of hard links; --runtime names the type-2 runtime file it embeds, so the build
#               fetches nothing.  The AppImage mounts through the host's FUSE (fusermount3), or runs from
#               --appimage-extract-and-run where there is none
# Needs docker (the user in its group), python3, rsync, and GNU tar with zstd for the archive.
# Exit status: 0 made and checked; 1 refused or failed (the partial folder is removed).

set -u

BUILD="" OUT="" CMAKE="" IMAGE="" JOBS="" ART=1 TAR=1 DOBUILD=1
APPIMAGETOOL="" RUNTIME=""
while [ $# -gt 0 ]; do
	case "$1" in
		--build) BUILD="$2"; shift 2;;
		--out) OUT="$2"; shift 2;;
		--cmake) CMAKE="$2"; shift 2;;
		--image) IMAGE="$2"; shift 2;;
		--jobs) JOBS="$2"; shift 2;;
		--no-art) ART=0; shift;;
		--no-tar) TAR=0; shift;;
		--no-build) DOBUILD=0; shift;;
		--appimage) APPIMAGETOOL="$2"; shift 2;;
		--runtime) RUNTIME="$2"; shift 2;;
		*) echo "linux-portable: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$IMAGE" ]; then
	case "$(uname -m)" in
		aarch64|arm64) IMAGE="registry.gitlab.steamos.cloud/steamrt/sniper/sdk/arm64:latest";;
		*) IMAGE="registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest";;
	esac
fi
fail() { echo "linux-portable: $*" >&2; [ -n "${STARTED:-}" ] && rm -rf -- "${OUT:?}"; exit 1; }
[ -n "$BUILD" ] && [ -n "$OUT" ] || fail "--build and --out are required"
[ "$DOBUILD" -eq 0 ] || [ -x "$CMAKE" ] || fail "--cmake must name a Linux CMake of 3.29 or later (Kitware's release tarball)"
command -v docker >/dev/null || fail "no docker"
CODE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$CODE/../.." && pwd)"
TABLE="$CODE/Tools/macos-app-licenses.txt"
mkdir -p "$BUILD" && BUILD="$(cd "$BUILD" && pwd)" || fail "cannot make $BUILD"
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT";; esac
[ -n "$JOBS" ] || JOBS="${ZH_BUILD_JOBS:-$(nproc)}"
. "$CODE/Tools/package-common.sh"

# everything the container reads or writes, mounted at its own path
mounts=( -v "$REPO:$REPO" -v "$BUILD:$BUILD" )
[ -n "$CMAKE" ] && mounts+=( -v "$(cd "$(dirname "$CMAKE")/.." && pwd):$(cd "$(dirname "$CMAKE")/.." && pwd)" )
in_sdk() {	# in_sdk <command>: runs a shell command inside the SDK as this user
	docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp "${mounts[@]}" -w "$BUILD" "$IMAGE" bash -c "$1"
}
ARCH="$(in_sdk "uname -m")" || fail "cannot run $IMAGE"
case "$ARCH" in x86_64|aarch64) ;; *) fail "$IMAGE is $ARCH: only x86_64 and aarch64 are built";; esac

# ---- the build ---------------------------------------------------------------------------------------------
if [ "$DOBUILD" -eq 1 ]; then
	echo "linux-portable: building generals and the overlay in $IMAGE (-j$JOBS; the log: $BUILD/linux-portable.log)"
	in_sdk "'$CMAKE' -S '$CODE' -B '$BUILD' -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-14 \
		-DCMAKE_CXX_COMPILER=g++-14 '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc' && \
		ninja -C '$BUILD' -j$JOBS generals zh_overlay" > "$BUILD/linux-portable.log" 2>&1 \
		|| { grep -E "error|FAILED|undefined reference" "$BUILD/linux-portable.log" | head -20 >&2; fail "the build failed"; }
fi
GENERALS="$BUILD/generals"
[ -x "$GENERALS" ] && [ -d "$BUILD/overlay" ] || fail "$BUILD holds no generals or no staged overlay"

# ---- the licences, against the link line -----------------------------------------------------------------
LIBPATHS="$(mktemp "${TMPDIR:-/tmp}/zh-link-libs.XXXXXX")"
trap 'rm -f -- "$LIBPATHS"' EXIT
linked="$(package_link_libraries "$BUILD/build.ninja" "$LIBPATHS")" || fail "cannot read generals' link line"
[ -n "$linked" ] || fail "generals' link line names no static library"
ENTRIES="$(package_license_entries "$TABLE" $linked)" || fail "refused: generals links libraries macos-app-licenses.txt does not cover:$ENTRIES"

# ---- the art, unless it is left out: the staged overlay links it from GeneralsMD/Run ----------------------
if [ "$ART" -eq 1 ] && ! ls "$BUILD/overlay"/Reforged*.big >/dev/null 2>&1; then
	fail "the staged overlay holds no Reforged*.big art: put the archives in GeneralsMD/Run and build again, or pass --no-art"
fi

# ---- the HUD rule, over the overlay that will be copied in ------------------------------------------
found="$(hud_check_files "$BUILD/overlay")"
[ -z "$found" ] || fail "refused: nothing shipped may force the HUD overlay on (ShowHudOverlay = Yes in: $(printf '%s ' $found))"

# ---- what generals needs of the system ---------------------------------------------------------------------
needs="$(in_sdk "readelf -d '$GENERALS' | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; echo '--'; objdump -T '$GENERALS'")" \
	|| fail "cannot read generals' dynamic section"
libs="$(sed '/^--$/q' <<< "$needs" | grep -v '^--$')"
glibc="$(printf '%s\n' "$needs" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
[ -n "$glibc" ] || fail "cannot read generals' glibc symbols"
[ "$(printf '%s\n%s\n' "$glibc" GLIBC_2.31 | sort -V | tail -1)" = GLIBC_2.31 ] || fail "refused: generals needs $glibc, newer than GLIBC_2.31"
! grep -qE 'GLIBCXX_|CXXABI_' <<< "$needs" || fail "refused: generals needs libstdc++ symbols (it must link it statically)"
allowed='^(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|ld-linux-x86-64\.so\.2|ld-linux-aarch64\.so\.1|libfontconfig\.so\.1)$'
extra="$(printf '%s\n' "$libs" | grep -vE "$allowed")"
[ -z "$extra" ] || fail "refused: generals needs shared libraries a SteamOS may lack: $(printf '%s ' $extra)"

# ---- the folder --------------------------------------------------------------------------------------------
rm -rf -- "$OUT" "$OUT.tar.zst"
STARTED=1
mkdir -p "$OUT" || fail "cannot create $OUT"
OUT="$(cd "$OUT" && pwd)"
mounts+=( -v "$(dirname "$OUT"):$(dirname "$OUT")" )
S="$OUT/share/zero-hour-reforged"
mkdir -p "$OUT/bin" "$S/overlay" "$S/licenses" "$OUT/share/applications" "$OUT/share/icons/hicolor/48x48/apps" \
	"$OUT/share/icons/hicolor/128x128/apps" \
	|| fail "cannot create $OUT"
in_sdk "objcopy --only-keep-debug '$GENERALS' '$OUT/bin/generals.debug' && strip -S -x -o '$OUT/bin/generals' '$GENERALS' && \
	cd '$OUT/bin' && objcopy --add-gnu-debuglink=generals.debug generals" || fail "cannot strip generals"
# the overlay, its links followed: the art is copied (a reflink where the file system has them)
if [ "$ART" -eq 1 ]; then
	cp -R -L --reflink=auto "$BUILD/overlay/." "$S/overlay/" || fail "cannot copy the overlay"
else
	rsync -aL --exclude 'Reforged*.big' "$BUILD/overlay/" "$S/overlay/" || fail "cannot copy the overlay"
fi
for e in $ENTRIES; do
	license_entry "$e" "$S/licenses" || fail "cannot write the licence entry '$e'"
done

cat > "$OUT/zero-hour-reforged.sh" <<'LAUNCHER'
#!/bin/sh
# Zero Hour Reforged's launcher (P3): the game finds its overlay beside it and the player's Zero Hour by itself
# (Registry.ini, then ~/Games, the Steam libraries and the rest; see README.txt).  Arguments pass through,
# so Steam's launch options reach the game: -root "<your Zero Hour folder>" names the install.
here="$(dirname "$(readlink -f "$0")")"
exec "$here/bin/generals" "$@"
LAUNCHER
chmod +x "$OUT/zero-hour-reforged.sh"

python3 - "$CODE/Main/Generals.ico" "$OUT/share/icons/hicolor/48x48/apps/zero-hour-reforged.png" \
	"$OUT/share/icons/hicolor/128x128/apps/zero-hour-reforged.png" <<'ICON_EOF' || fail "cannot make the icon"
# the .ico's largest 24-bit image (48 px), its AND mask as alpha, written as a PNG; and the same scaled to 128 px
# (bilinear, over premultiplied alpha), because AppStream, which the Flatpak's metadata goes through, wants 64 or more
import struct, sys, zlib
d = open(sys.argv[1], "rb").read()
count = struct.unpack("<H", d[4:6])[0]
best = None
for i in range(count):
    w, h, c, r, planes, bpp, size, off = struct.unpack("<BBBBHHII", d[6 + 16 * i:22 + 16 * i])
    w, h = w or 256, h or 256
    if d[off:off + 4] == b"\x89PNG":
        continue
    header = struct.unpack("<IiiHHIIiiII", d[off:off + 40])
    if header[4] == 24 and (best is None or w > best[0]):
        best = (w, h, off + header[0])
w, h, pixels = best
row = (w * 3 + 3) & ~3
mask_row = ((w + 31) // 32) * 4
mask = pixels + row * h
rgba = []		# rows of (r, g, b, a), top first
for y in range(h):
    src = pixels + row * (h - 1 - y)
    msrc = mask + mask_row * (h - 1 - y)
    line = []
    for x in range(w):
        b, g, r = d[src + 3 * x:src + 3 * x + 3]
        transparent = (d[msrc + x // 8] >> (7 - x % 8)) & 1
        line.append((r, g, b, 0 if transparent else 255))
    rgba.append(line)
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
def write_png(path, image):
    size = len(image)
    raw = b"".join(bytes([0]) + bytes(v for p in line for v in p) for line in image)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
def scaled(image, size):
    n = len(image)
    def at(x, y):
        r, g, b, a = image[min(max(y, 0), n - 1)][min(max(x, 0), n - 1)]
        return (r * a / 255.0, g * a / 255.0, b * a / 255.0, float(a))
    out = []
    for y in range(size):
        fy = (y + 0.5) * n / size - 0.5; y0 = int(fy // 1); ty = fy - y0
        line = []
        for x in range(size):
            fx = (x + 0.5) * n / size - 0.5; x0 = int(fx // 1); tx = fx - x0
            p = [at(x0, y0)[i] * (1 - tx) * (1 - ty) + at(x0 + 1, y0)[i] * tx * (1 - ty)
                 + at(x0, y0 + 1)[i] * (1 - tx) * ty + at(x0 + 1, y0 + 1)[i] * tx * ty for i in range(4)]
            a = p[3]
            line.append(tuple(int(round(min(255, c * 255.0 / a))) if a > 0 else 0 for c in p[:3]) + (int(round(a)),))
        out.append(line)
    return out
write_png(sys.argv[2], rgba)
write_png(sys.argv[3], scaled(rgba, 128))
ICON_EOF

cat > "$OUT/share/applications/zero-hour-reforged.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Zero Hour Reforged
Comment=Command & Conquer Generals Zero Hour, reforged
Exec=zero-hour-reforged.sh
Icon=zero-hour-reforged
# a template: to use it, put the folder's path in Exec and copy this file to ~/.local/share/applications
Categories=Game;StrategyGame;
Terminal=false
DESKTOP

commit="$(git -C "$CODE" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)"
if [ "$commit" != unknown ] && ! git -C "$CODE" diff --quiet HEAD -- . 2>/dev/null; then commit="$commit-dirty"; fi
built="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
image_id="$(docker image inspect --format '{{index .RepoDigests 0}}' "$IMAGE" 2>/dev/null || echo "$IMAGE")"
compiler="$(in_sdk "g++-14 --version | head -1")"
version="$(awk '/#define VERSION_MAJOR/ {a=$3} /#define VERSION_MINOR/ {b=$3} /#define VERSION_BUILDNUM/ {c=$3} END {print a"."b"."c}' "$BUILD/generated/BuildVersion.h")"
cat > "$OUT/VERSION" <<VERSION_EOF
Zero Hour Reforged $version for Linux ($ARCH)
commit $commit
built $built
built in $image_id
compiler $compiler
needs glibc $glibc or newer, and fontconfig; art $( [ "$ART" -eq 1 ] && echo included || echo left out)
VERSION_EOF

cat > "$OUT/README.txt" <<'README_EOF'
Zero Hour Reforged for Linux and the Steam Deck
===============================================

This folder is the whole game engine; nothing is installed. It needs your own copy of Command & Conquer
Generals Zero Hour (Steam, EA App, CD or First Decade), with the original Generals it builds on.

INSTALL
  Unpack the folder anywhere you can write, e.g. ~/Games/ZeroHourReforged. To start it, run
  zero-hour-reforged.sh in the folder.

  In Steam: Games > Add a Non-Steam Game to My Library > Browse, choose zero-hour-reforged.sh.

WHERE YOUR ZERO HOUR IS
  The game finds it by itself when it is in one of these places:
    ~/Games/Command & Conquer Generals Zero Hour (or "Zero Hour", or the other usual names)
    any Steam library, the SD card's included, as Steam installs it
  with the original Generals in a ZH_Generals folder inside it, or in a folder named "Command & Conquer
  Generals" beside it.

  If it is elsewhere, the first start asks for the folder (in Desktop Mode) and remembers it. In the
  Steam Deck's Game Mode there is no folder dialog: give the folder in Steam's launch options instead.
  Select the game in Steam, then Properties > General > Launch Options, and enter:

    -root "/home/you/Games/Command & Conquer Generals Zero Hour"

  with the path of your own Zero Hour folder (the one with INIZH.big in it) between the quotes.

THE UPSCALED ART
  A folder built without the art (every Linux package) does not download it. The game reads the
  Reforged*.big files from ReforgedArt/ in the settings folder below when they are there, and plays at
  the original textures when they are not.

FILES
  Settings, saves, replays and logs:  ~/.local/share/Command and Conquer Generals Zero Hour Data/
                                     ($XDG_DATA_HOME's, when that is set)
  Which build this is:               VERSION
  Licences:                          share/zero-hour-reforged/licenses/

UNINSTALL
  Delete this folder. Your settings and saves stay in ~/.local/share/Command and Conquer Generals Zero
  Hour Data until you delete them too.
README_EOF

# the HUD rule once more, over the finished folder
found="$(hud_check_files "$OUT")"
[ -z "$found" ] || fail "refused: nothing shipped may force the HUD overlay on (ShowHudOverlay = Yes in: $(printf '%s ' $found))"

size="$(du -sh "$OUT" | cut -f1)"
if [ "$TAR" -eq 1 ]; then
	tar -C "$(dirname "$OUT")" -I 'zstd -T0 -10' -cf "$OUT.tar.zst" "$(basename "$OUT")" || fail "cannot pack $OUT.tar.zst"
fi
if [ -n "$APPIMAGETOOL" ]; then
	[ -x "$APPIMAGETOOL" ] && [ -f "$RUNTIME" ] || fail "--appimage needs an appimagetool and --runtime a type-2 runtime file"
	A="$OUT.AppDir"
	rm -rf -- "$A" "$OUT.AppImage"
	cp -al "$OUT" "$A" 2>/dev/null || cp -R "$OUT" "$A" || fail "cannot make $A"
	rm -f "$A/bin/generals.debug"		# the AppImage is for playing; the debug file stays in the folder and its tarball
	cp "$OUT/zero-hour-reforged.sh" "$A/AppRun" && cp "$OUT/share/applications/zero-hour-reforged.desktop" "$A/" \
		&& cp "$OUT/share/icons/hicolor/128x128/apps/zero-hour-reforged.png" "$A/zero-hour-reforged.png" \
		&& ln -s zero-hour-reforged.png "$A/.DirIcon" || fail "cannot complete $A"
	ARCH="$ARCH" "$APPIMAGETOOL" --no-appstream --runtime-file "$RUNTIME" "$A" "$OUT.AppImage" > "$OUT.appimagetool.log" 2>&1 \
		|| { tail -5 "$OUT.appimagetool.log" >&2; rm -rf -- "$A"; fail "appimagetool failed"; }
	rm -rf -- "$A"
	# a type-2 runtime answers --appimage-offset with where its squashfs starts, without mounting anything
	offset="$("$OUT.AppImage" --appimage-offset 2>/dev/null)"
	case "$offset" in ''|*[!0-9]*) fail "the AppImage does not answer --appimage-offset: its runtime is not a type-2 runtime";; esac
fi
STARTED=""
echo "linux-portable: $OUT ($size$( [ "$TAR" -eq 1 ] && echo ", packed $(du -sh "$OUT.tar.zst" | cut -f1)")$( [ -n "$APPIMAGETOOL" ] && echo ", AppImage $(du -sh "$OUT.AppImage" | cut -f1)")): commit $commit, needs $glibc, $(printf '%s\n' $ENTRIES | wc -l) licence entries for $(printf '%s\n' $linked | wc -l) linked libraries, art $( [ "$ART" -eq 1 ] && echo included || echo left out)"
