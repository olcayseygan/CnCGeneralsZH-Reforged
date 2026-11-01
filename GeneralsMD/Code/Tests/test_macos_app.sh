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
# Tools/make-macos-app.sh checked on itself (P1 step 5), with a bundle built from the real inputs (this
# build's generals, staged overlay and link line) into a temporary folder, without the art:
#   1. it builds, and the bundle holds what P1 specifies: Info.plist (lints; the executable, the
#      id, this build's version, the executable's minimum macOS, high resolution, the category), PkgInfo,
#      a stripped arm64/x86_64 executable with its dSYM beside the bundle, the icon, the overlay, one
#      licence file per entry the link line needs; its ad-hoc signature verifies --deep --strict;
#   2. the licence check's control: a link line with a library the table does not cover is refused,
#      naming it, and leaves no bundle;
#   3. the seal's control: a file written into the signed bundle makes codesign --verify --deep --strict
#      fail, so a pass after a run means nothing was written;
#   4. the oldest macOS (P2): the bundle's LSMinimumSystemVersion is the build's deployment target, and
#      every object it ships is built for no newer one.  Two controls: a lower --min-macos than the
#      build's is refused, naming libraries; a Mach-O built for a newer macOS planted in the overlay is
#      refused by the bundle-side check, naming it;
#   5. universal2 (option A's path, tested with tiny stand-ins, no second build): an x86_64 executable
#      for the target lipo'd in makes a bundle whose executable holds both slices and still verifies;
#      one built for a newer macOS is refused, and so is an arm64 file passed as the x86_64 one;
#   6. the art, never bundled (a project rule: the app carries none): a fresh bundle built without --no-art
#      holds no Reforged*.big, whatever the staged overlay links; and the bundle's game, started headless on an
#      empty root with a ReforgedArt folder in its user data, reads that folder as an overlay and writes
#      nothing into the bundle (its seal still verifies).
# What it cannot see: a quarantined download and Gatekeeper (E2), and the dialog of the root chooser (by hand).
# Usage: test_macos_app.sh <generals> <staged overlay> <build dir> <deployment target>.
# Exit 0, 1, or 77 off macOS.
set -u
GENERALS="$1" OVERLAY="$2" BUILD="$3" TARGET="$4"
if [ "$(uname -s)" != "Darwin" ] || ! command -v codesign >/dev/null 2>&1; then
	echo "skip: not macOS, or no codesign"
	exit 77
fi
SCRIPT="$(cd "$(dirname "$0")/../Tools" && pwd)/make-macos-app.sh"
T="$(mktemp -d "${TMPDIR:-/tmp}/macos-app-check.XXXXXX")" || T=""
if [ -z "$T" ] || [ ! -d "$T" ]; then
	echo "test_macos_app: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
trap 'rm -rf -- "${T:?}"' EXIT
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }
ID=io.github.example.check
APP="$T/one/Zero Hour Reforged.app"

# 1. the bundle
out="$(bash "$SCRIPT" --generals "$GENERALS" --overlay "$OVERLAY" --build "$BUILD" --out "$APP" --bundle-id "$ID" --min-macos "$TARGET" --no-art 2>&1)"; status=$?
check '[ $status -eq 0 ]' "it builds a bundle (exit $status: $out)"
P="$APP/Contents/Info.plist"
key() { plutil -extract "$1" raw -o - "$P" 2>/dev/null; }
version="$(awk '/#define VERSION_MAJOR/ {a=$3} /#define VERSION_MINOR/ {b=$3} /#define VERSION_BUILDNUM/ {c=$3} END {print a"."b"."c}' "$BUILD/generated/BuildVersion.h")"
minos="$TARGET"
check 'plutil -lint "$P" >/dev/null' "Info.plist lints"
check '[ "$(key CFBundleExecutable)" = generals ] && [ "$(key CFBundleIdentifier)" = "$ID" ] && [ "$(key CFBundlePackageType)" = APPL ]' \
	"Info.plist names the executable, the id and APPL"
check '[ "$(key CFBundleShortVersionString)" = "$version" ] && [ "$(key LSMinimumSystemVersion)" = "$minos" ]' \
	"and this build's version ($version) and its deployment target ($minos) as the minimum macOS"
check '[ "$(key NSHighResolutionCapable)" = true ] && [ "$(key LSApplicationCategoryType)" = public.app-category.strategy-games ] && [ "$(key CFBundleIconFile)" = AppIcon ]' \
	"high resolution, the strategy games category, and the icon"
check '[ "$(key LSSupportsGameMode)" = true ]' "the bundle declares Game Mode support"
network_usage_en='Zero Hour Reforged uses the local network to find and host LAN games and connect to other players.'
network_usage_de='Zero Hour Reforged verwendet das lokale Netzwerk, um LAN-Spiele zu finden, zu hosten und mit anderen Spielern zu spielen.'
check '[ "$(key NSLocalNetworkUsageDescription)" = "$network_usage_en" ]' "the local-network purpose explains LAN discovery, hosting and multiplayer"
for locale in en de; do
	case "$locale" in en) description="$network_usage_en";; de) description="$network_usage_de";; esac
	strings="$APP/Contents/Resources/$locale.lproj/InfoPlist.strings"
	check 'plutil -lint "$strings" >/dev/null && [ "$(plutil -extract NSLocalNetworkUsageDescription raw -o - "$strings")" = "$description" ]' \
		"the $locale local-network purpose is a valid localized property list"
done
head_commit="$(git -C "$(dirname "$SCRIPT")/.." rev-parse --short=10 HEAD 2>/dev/null || echo unknown)"
check '[ "$(key ZHReforgedCommit | sed "s/-dirty\$//")" = "$head_commit" ] && key ZHReforgedBuildDate | grep -Eq "^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z\$"' \
	"which build: the source commit ($head_commit) and the build time, in UTC"
check '[ "$(cat "$APP/Contents/PkgInfo")" = "APPL????" ]' "PkgInfo"
check 'file "$APP/Contents/MacOS/generals" | grep -q "Mach-O 64-bit executable" && [ "$(stat -f %z "$APP/Contents/MacOS/generals")" -lt "$(stat -f %z "$GENERALS")" ]' \
	"a Mach-O executable, smaller than the unstripped one"
check '[ -d "$APP.dSYM" ] && [ ! -e "$APP/Contents/MacOS/generals.dSYM" ]' "its dSYM beside the bundle, not in it"
check 'file "$APP/Contents/Resources/AppIcon.icns" | grep -q "icon"' "the icon"
check '[ -f "$APP/Contents/Resources/Overlay/Install_Final.bmp" ] && [ -d "$APP/Contents/Resources/Overlay/Data/INI" ] && [ -z "$(find "$APP" -type l)" ]' \
	"the overlay, with no symbolic link anywhere in the bundle"
nlic=$(ls "$APP/Contents/Resources/Licenses" | wc -l | tr -d ' ')
check '[ "$nlic" -ge 13 ] && [ -f "$APP/Contents/Resources/Licenses/Zero-Hour-Reforged-LICENSE.md" ] && [ -f "$APP/Contents/Resources/Licenses/FFmpeg-SOURCE.txt" ] && grep -q "may not be removed" "$APP/Contents/Resources/Licenses/zlib-LICENSE.txt"' \
	"the licences ($nlic files: the game, FFmpeg with its source pointer, zlib from its header, ...)"
check 'grep -q "this notice may not be removed or altered" "$APP/Contents/Resources/Licenses/LZH-Light-LICENSE.txt" && grep -q "GNU GPL option, version 2 or later" "$APP/Contents/Resources/Licenses/FreeType-OPTION.txt" && [ -f "$APP/Contents/Resources/Licenses/FreeType-GPLv2.txt" ]' \
	"LZH-Light's notice, and FreeType's GPLv2-or-later option with its GPLv2 text"
check 'codesign --verify --deep --strict "$APP" 2>/dev/null && codesign -dv "$APP" 2>&1 | grep -q "Signature=adhoc"' "signed ad hoc, and it verifies --deep --strict"

# 3. the seal's control, on that bundle
touch "$APP/Contents/Resources/written-after-signing"
check '! codesign --verify --deep --strict "$APP" 2>/dev/null' "a file written after signing breaks the seal (so a clean verify after a run means nothing was written)"

# 2. the licence check's control
python3 - "$BUILD/build.ninja" "$T/build.ninja" <<'NINJA_EOF'
import re, sys
text = open(sys.argv[1]).read()
text = re.sub(r"(^build generals: CXX_EXECUTABLE_LINKER.*?\n  LINK_LIBRARIES = )", r"\1Libraries/libnolicence.a ", text, count=1, flags=re.S | re.M)
open(sys.argv[2], "w").write(text)
NINJA_EOF
out="$(bash "$SCRIPT" --generals "$GENERALS" --overlay "$OVERLAY" --build "$BUILD" --out "$T/three/Zero Hour Reforged.app" --bundle-id "$ID" --min-macos "$TARGET" --no-art --link-ninja "$T/build.ninja" 2>&1)"; status=$?
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "does not cover: nolicence" && [ ! -e "$T/three/Zero Hour Reforged.app" ]' \
	"a linked library without a licence entry is refused, naming it, and leaves no bundle (exit $status)"

# 4. the oldest macOS
check 'otool -l "$APP/Contents/MacOS/generals" | awk "/LC_BUILD_VERSION/ {f=1} f && \$1 == \"minos\" {print \$2; exit}" | grep -qx "$TARGET"' \
	"the executable is stamped for $TARGET"
out="$(bash "$SCRIPT" --generals "$GENERALS" --overlay "$OVERLAY" --build "$BUILD" --out "$T/four/Zero Hour Reforged.app" --bundle-id "$ID" --min-macos 11.0 --no-art 2>&1)"; status=$?
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "linked libraries built for a newer macOS than 11.0" && printf "%s" "$out" | grep -q "libgameengine.a: minimum macOS $TARGET" && [ ! -e "$T/four/Zero Hour Reforged.app" ]' \
	"a minimum below the build's is refused, naming the libraries, and leaves no bundle (exit $status)"
printf 'int main(void) { return 0; }\n' > "$T/newer.c"
mkdir -p "$T/overlay"
for e in "$OVERLAY"/*; do case "$e" in *.big) ;; *) cp -R -L "$e" "$T/overlay/";; esac; done
if cc -mmacosx-version-min=26.0 -o "$T/overlay/newer-tool" "$T/newer.c" 2>/dev/null; then
	out="$(bash "$SCRIPT" --generals "$GENERALS" --overlay "$T/overlay" --build "$BUILD" --out "$T/five/Zero Hour Reforged.app" --bundle-id "$ID" --min-macos "$TARGET" --no-art 2>&1)"; status=$?
	check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "the bundle.s executables built for a newer macOS" && printf "%s" "$out" | grep -q "newer-tool: minimum macOS 26.0" && [ ! -e "$T/five/Zero Hour Reforged.app" ]' \
		"a Mach-O for a newer macOS inside the bundle is refused, naming it (exit $status)"
else
	check 'false' "cc could not build the control's newer-macOS Mach-O"
fi

# 5. universal2
printf 'int main(void) { return 0; }\n' > "$T/x86.c"
cc -arch x86_64 -mmacosx-version-min="$TARGET" -o "$T/x86-ok" "$T/x86.c" 2>/dev/null
cc -arch x86_64 -mmacosx-version-min=26.0 -o "$T/x86-newer" "$T/x86.c" 2>/dev/null
cc -arch arm64 -mmacosx-version-min="$TARGET" -o "$T/not-x86" "$T/x86.c" 2>/dev/null
U="$T/six/Zero Hour Reforged.app"
out="$(bash "$SCRIPT" --generals "$GENERALS" --overlay "$OVERLAY" --build "$BUILD" --out "$U" --bundle-id "$ID" --min-macos "$TARGET" --no-art --x86-64-generals "$T/x86-ok" 2>&1)"; status=$?
archs="$(lipo -archs "$U/Contents/MacOS/generals" 2>/dev/null | tr ' ' '\n' | sort | tr '\n' ' ')"
check '[ $status -eq 0 ] && [ "$archs" = "arm64 x86_64 " ] && codesign --verify --deep --strict "$U" 2>/dev/null' \
	"an x86_64 slice for $TARGET makes a universal executable ($archs) that verifies (exit $status)"
check '[ "$(otool -arch all -l "$U/Contents/MacOS/generals" | awk "\$1 == \"minos\" {print \$2}" | sort -u | tr "\n" " ")" = "$TARGET " ]' \
	"and both slices are stamped for $TARGET"
out="$(bash "$SCRIPT" --generals "$GENERALS" --overlay "$OVERLAY" --build "$BUILD" --out "$T/seven/Zero Hour Reforged.app" --bundle-id "$ID" --min-macos "$TARGET" --no-art --x86-64-generals "$T/x86-newer" 2>&1)"; status=$?
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "the x86_64 executable built for a newer macOS" && [ ! -e "$T/seven/Zero Hour Reforged.app" ]' \
	"an x86_64 slice for macOS 26 is refused before a bundle exists (exit $status)"
out="$(bash "$SCRIPT" --generals "$GENERALS" --overlay "$OVERLAY" --build "$BUILD" --out "$T/eight/Zero Hour Reforged.app" --bundle-id "$ID" --min-macos "$TARGET" --no-art --x86-64-generals "$T/not-x86" 2>&1)"; status=$?
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "is not an x86_64 executable"' "an arm64 file passed as the x86_64 slice is refused (exit $status)"

# 6. the art, never bundled, read from the user data folder
N="$T/nine/Zero Hour Reforged.app"
out="$(bash "$SCRIPT" --generals "$GENERALS" --overlay "$OVERLAY" --build "$BUILD" --out "$N" --bundle-id "$ID" --min-macos "$TARGET" 2>&1)"; status=$?
check '[ $status -eq 0 ] && [ -z "$(find "$N" -name "Reforged*.big")" ]' \
	"a bundle holds no Reforged*.big (the staged overlay links $(find -L "$OVERLAY" -maxdepth 1 -name 'Reforged*.big' | wc -l | tr -d ' ')) (exit $status)"
mkdir -p "$T/artuser/ReforgedArt" "$T/emptyroot"
head -c 200000 /dev/urandom > "$T/artuser/ReforgedArt/ReforgedTest.big"
( ZH_UNATTENDED=1 ZH_USER_DATA_DIR="$T/artuser/" perl -e 'alarm shift; exec @ARGV' 120 "$N/Contents/MacOS/generals" -headless \
	-root "$T/emptyroot" -maxframes 1 > "$T/start.out" 2> "$T/start.err" )
art_real="$(cd "$T/artuser/ReforgedArt" && pwd -P)"
check 'grep -q "^generals: overlay $art_real, searched before the install" "$T/start.err" && codesign --verify --deep --strict "$N" 2>/dev/null' \
	"the bundle's game reads <user data>/ReforgedArt as an overlay, and writes nothing into the bundle"

exit $failed
