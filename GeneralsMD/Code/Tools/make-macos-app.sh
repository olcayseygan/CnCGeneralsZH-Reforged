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
# make-macos-app.sh: builds "Zero Hour Reforged.app" (P1 step 5), the
# macos_app CMake target's one step.
#
#   Contents/Info.plist, PkgInfo       with ZHReforgedCommit and ZHReforgedBuildDate: which build this is
#   Contents/MacOS/generals            stripped; its dSYM beside the bundle (<out>.dSYM), not in it, for C5
#   Contents/Resources/AppIcon.icns    from Main/Generals.ico's 48 px image (soft on Retina: an open item)
#   Contents/Resources/Overlay/        the staged overlay (zh_overlay), without the art archives
#   Contents/Resources/Licenses/       from macos-app-licenses.txt, checked against the link line
#
# Refused, before anything is written:
#   - a static library on generals' link line (build.ninja) that macos-app-licenses.txt does not name;
#   - an object in any static library on the link line, or a Mach-O in the bundle, built for a newer
#     macOS than --min-macos (P2): the final executable's own stamp would hide a vendored library built
#     for the build machine's version, and the player's Mac would find it at the first call.
# Last, an ad-hoc signature (codesign --sign - --timestamp=none) over the final contents, and
# codesign --verify --deep --strict of it.  Anything written into the bundle afterwards breaks that seal.
#
# THE ART is not in the app (a project rule, as on Linux and upstream's Windows): the Reforged*.big the
# staged overlay links are left out.  The game reads them from ReforgedArt in the user data folder
# (~/Library/Application Support/...) when something has put them there; it does not download them.
# --no-art is still taken, and changes nothing.
#
# Usage: make-macos-app.sh --generals <exe> --overlay <staged overlay> --build <build dir>
#          --out <.../Zero Hour Reforged.app> --bundle-id <id> --min-macos <version> [--no-art]
#          [--link-ninja <build.ninja>]
#   --min-macos   LSMinimumSystemVersion, and the newest minimum any shipped object may carry (the build's
#                 CMAKE_OSX_DEPLOYMENT_TARGET)
#   --x86-64-generals <exe>
#                 universal2 (P2, option A, a release-time step): an x86_64 build of generals, lipo'd with
#                 --generals into one executable before dsymutil; the minimum-macOS check reads every slice,
#                 and the x86_64 build's own libraries too when its folder holds a build.ninja
#   --link-ninja  the build.ninja whose generals link line is checked (default <build>/build.ninja)
# Exit status: 0 built, signed and verified; 1 refused or failed (the partial bundle is removed).

set -u

GENERALS="" OVERLAY="" BUILD="" OUT="" BUNDLE_ID="" NINJA="" MIN_MACOS="" X86=""
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--overlay) OVERLAY="$2"; shift 2;;
		--build) BUILD="$2"; shift 2;;
		--out) OUT="$2"; shift 2;;
		--bundle-id) BUNDLE_ID="$2"; shift 2;;
		--min-macos) MIN_MACOS="$2"; shift 2;;
		--x86-64-generals) X86="$2"; shift 2;;
		--no-art) shift;;		# the art is never in the app now: taken, and nothing changes
		--link-ninja) NINJA="$2"; shift 2;;
		*) echo "make-macos-app: unknown argument $1" >&2; exit 2;;
	esac
done
fail() { echo "make-macos-app: $*" >&2; [ -n "${STARTED:-}" ] && rm -rf -- "${OUT:?}"; exit 1; }
[ -x "$GENERALS" ] && [ -d "$OVERLAY" ] && [ -d "$BUILD" ] && [ -n "$BUNDLE_ID" ] && [ -n "$MIN_MACOS" ] || fail "--generals, --overlay, --build, --bundle-id and --min-macos are required"
case "$OUT" in *.app) ;; *) fail "--out must name a .app";; esac
[ -n "$NINJA" ] || NINJA="$BUILD/build.ninja"
CODE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$CODE/../.." && pwd)"
TABLE="$CODE/Tools/macos-app-licenses.txt"

# ---- the link line against the licence table ----------------------------------------------------------
. "$CODE/Tools/package-common.sh"		# the link-line reader and the licence entries, shared with P3
LIBPATHS="$(mktemp "${TMPDIR:-/tmp}/zh-link-libs.XXXXXX")"
trap 'rm -f -- "$LIBPATHS"' EXIT
linked="$(package_link_libraries "$NINJA" "$LIBPATHS")" || fail "cannot read generals' link line from $NINJA"
[ -n "$linked" ] || fail "generals' link line in $NINJA names no static library"
ENTRIES="$(package_license_entries "$TABLE" $linked)" || fail "refused: generals links libraries macos-app-licenses.txt does not cover:$ENTRIES"

# ---- the oldest macOS, over everything that ships ----------------------------------------------------------
minos_check() {	# minos_check <what> <files...>: fails naming every object built for a newer macOS than MIN_MACOS
	local what="$1"; shift
	local newer
	newer="$(for f in "$@"; do printf '== %s\n' "$f"; otool -arch all -l "$f" 2>/dev/null; done | python3 -c '
import re, sys
limit = tuple(int(x) for x in sys.argv[1].split("."))
current, member, pending, bad = None, None, None, {}
for line in sys.stdin:
    if line.startswith("== "):
        current = line[3:].strip(); continue
    m = re.match(r"^(\S.*\.a)\((.*)\):$", line.strip())
    if m:
        member = m.group(2); continue
    t = line.split()
    if not t: continue
    if t[0] == "cmd": pending = t[1]
    elif (pending == "LC_BUILD_VERSION" and t[0] == "minos") or (pending == "LC_VERSION_MIN_MACOSX" and t[0] == "version"):
        v = tuple(int(x) for x in t[1].split("."))
        if v > limit:
            bad.setdefault("%s: minimum macOS %s" % (current, t[1]), []).append(member or "")
for k, members in sorted(bad.items()):
    print("%s (%d object%s%s)" % (k, len(members), "" if len(members) == 1 else "s",
        ": " + ", ".join(sorted(set(x for x in members if x))[:3]) if any(members) else ""))
' "$MIN_MACOS")"
	[ -z "$newer" ] || fail "refused: $what built for a newer macOS than $MIN_MACOS:
$newer"
}
# every static library on generals' link line, object by object (build.ninja names them relative to the build)
libs=()
while IFS= read -r a; do
	[ -n "$a" ] || continue
	case "$a" in /*) libs+=("$a");; *) libs+=("$BUILD/$a");; esac
done < "$LIBPATHS"
minos_check "linked libraries" "${libs[@]}"

# ---- universal2: the x86_64 slice, checked before anything is written ---------------------------------------
if [ -n "$X86" ]; then
	[ -f "$X86" ] || fail "--x86-64-generals: no file at $X86"
	[ "$(lipo -archs "$X86" 2>/dev/null)" = x86_64 ] || fail "--x86-64-generals: $X86 is not an x86_64 executable ($(lipo -archs "$X86" 2>&1))"
	case " $(lipo -archs "$GENERALS" 2>/dev/null) " in *" x86_64 "*) fail "--generals already holds an x86_64 slice";; esac
	minos_check "the x86_64 executable" "$X86"
	x86build="$(cd "$(dirname "$X86")" && pwd)"
	if [ -f "$x86build/build.ninja" ]; then		# its own link line's libraries, as the arm64 ones above
		python3 - "$x86build/build.ninja" "$LIBPATHS.x86" <<'LINK86_EOF' || fail "cannot read the x86_64 build's link line"
import re, sys
text = open(sys.argv[1]).read()
m = re.search(r"^build generals: CXX_EXECUTABLE_LINKER.*?\n  LINK_LIBRARIES = (.*?)\n", text, re.S | re.M)
open(sys.argv[2], "w").write("\n".join(sorted(set(t for t in m.group(1).split() if t.endswith(".a")))) + "\n")
LINK86_EOF
		libs86=()
		while IFS= read -r a; do [ -n "$a" ] || continue; case "$a" in /*) libs86+=("$a");; *) libs86+=("$x86build/$a");; esac; done < "$LIBPATHS.x86"
		rm -f -- "$LIBPATHS.x86"
		minos_check "the x86_64 build's linked libraries" "${libs86[@]}"
	fi
fi

# ---- the bundle ---------------------------------------------------------------------------------------------
rm -rf -- "${OUT:?}"
STARTED=1
C="$OUT/Contents"
mkdir -p "$C/MacOS" "$C/Resources/Overlay" "$C/Resources/Licenses" || fail "cannot create $OUT"

# the executable, stripped, and its symbols beside the bundle
rm -rf -- "${OUT:?}.dSYM"
EXE="$GENERALS"
if [ -n "$X86" ]; then
	universal="$(mktemp -d "${TMPDIR:-/tmp}/zh-universal.XXXXXX")" || universal=""
	[ -n "$universal" ] && [ -d "$universal" ] || fail "cannot make a work folder under ${TMPDIR:-/tmp}"
	EXE="$universal/generals"
	lipo -create "$GENERALS" "$X86" -output "$EXE" || fail "lipo -create failed"
fi
dsymutil "$EXE" -o "$OUT.dSYM" >/dev/null 2>&1 || fail "dsymutil failed"
cp "$EXE" "$C/MacOS/generals" && strip -S -x "$C/MacOS/generals" || fail "cannot place the executable"
[ "$EXE" = "$GENERALS" ] || rm -rf -- "$(dirname "$EXE")"

# Info.plist, from the build's version header and the executable's own minimum macOS
version="$(awk '/#define VERSION_MAJOR/ {a=$3} /#define VERSION_MINOR/ {b=$3} /#define VERSION_BUILDNUM/ {c=$3} END {print a"."b"."c}' "$BUILD/generated/BuildVersion.h")"
minos="$MIN_MACOS"
[ "$version" != ".." ] || fail "cannot read the version from $BUILD/generated/BuildVersion.h"
# which build this is, for the player and for tools that update an installed copy: the source commit (with -dirty when
# tracked files differ from it) and the time the bundle was made, in UTC; "unknown" outside a git checkout
source_dir="$(cd "$(dirname "$0")/.." && pwd)"
commit="$(git -C "$source_dir" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)"
if [ "$commit" != unknown ] && ! git -C "$source_dir" diff --quiet HEAD -- . 2>/dev/null; then commit="$commit-dirty"; fi
built="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
network_usage_en='Zero Hour Reforged uses the local network to find and host LAN games and connect to other players.'
network_usage_de='Zero Hour Reforged verwendet das lokale Netzwerk, um LAN-Spiele zu finden, zu hosten und mit anderen Spielern zu spielen.'
cat > "$C/Info.plist" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>		<string>en</string>
	<key>CFBundleDisplayName</key>			<string>Zero Hour Reforged</string>
	<key>CFBundleExecutable</key>			<string>generals</string>
	<key>CFBundleGetInfoString</key>		<string>Zero Hour Reforged $version, $commit, built $built</string>
	<key>CFBundleIconFile</key>			<string>AppIcon</string>
	<key>CFBundleIdentifier</key>			<string>$BUNDLE_ID</string>
	<key>CFBundleInfoDictionaryVersion</key>	<string>6.0</string>
	<key>CFBundleName</key>				<string>Zero Hour Reforged</string>
	<key>CFBundlePackageType</key>			<string>APPL</string>
	<key>CFBundleShortVersionString</key>		<string>$version</string>
	<key>CFBundleVersion</key>			<string>$version</string>
	<key>LSApplicationCategoryType</key>		<string>public.app-category.strategy-games</string>
	<key>LSMinimumSystemVersion</key>		<string>$minos</string>
	<key>NSHighResolutionCapable</key>		<true/>
	<key>NSLocalNetworkUsageDescription</key>	<string>$network_usage_en</string>
	<key>ZHReforgedBuildDate</key>			<string>$built</string>
	<key>ZHReforgedCommit</key>			<string>$commit</string>
</dict>
</plist>
PLIST_EOF
plutil -lint "$C/Info.plist" >/dev/null || fail "Info.plist does not lint"
# macOS 15 and later ask for local-network access when the LAN lobby uses UDP.
# Localize that system-owned prompt; older supported macOS versions ignore the key.
for locale in en de; do
	case "$locale" in en) description="$network_usage_en";; de) description="$network_usage_de";; esac
	mkdir -p "$C/Resources/$locale.lproj" || fail "cannot create the $locale localization"
	strings="$C/Resources/$locale.lproj/InfoPlist.strings"
	printf '"NSLocalNetworkUsageDescription" = "%s";\n' "$description" > "$strings" || fail "cannot write $strings"
	plutil -lint "$strings" >/dev/null || fail "$strings does not lint"
done
printf 'APPL????' > "$C/PkgInfo"

# the icon: the .ico's 48 px image scaled to every size an .icns holds
icons="$(mktemp -d "${TMPDIR:-/tmp}/zh-icon.XXXXXX")" || icons=""
[ -n "$icons" ] && [ -d "$icons" ] || fail "cannot make a work folder under ${TMPDIR:-/tmp}"
iconset="$icons/AppIcon.iconset"
mkdir -p "$iconset"
sips -s format png "$CODE/Main/Generals.ico" --out "$iconset/base.png" >/dev/null 2>&1 || fail "cannot read Main/Generals.ico"
for s in 16 32 128 256 512; do
	sips -z $s $s "$iconset/base.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null 2>&1 &&
	sips -z $((s * 2)) $((s * 2)) "$iconset/base.png" --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null 2>&1 || fail "sips failed at $s px"
done
rm -f "$iconset/base.png"
iconutil -c icns "$iconset" -o "$C/Resources/AppIcon.icns" || fail "iconutil failed"
rm -rf -- "$(dirname "$iconset")"

# the overlay: everything as staged but the art
for entry in "$OVERLAY"/*; do
	name="$(basename "$entry")"
	case "$name" in
		Reforged*.big) continue;;		# the art: read from the user data folder, never in the app
		*) cp -R -L "$entry" "$C/Resources/Overlay/$name" || fail "cannot copy $name";;
	esac
done

# the licences
L="$C/Resources/Licenses"
for e in $ENTRIES; do
	license_entry "$e" "$L" || fail "cannot write the licence entry '$e'"
done

# the oldest macOS, over every Mach-O the bundle holds
machos=()
while IFS= read -r f; do machos+=("$f"); done < <(find "$C" -type f -exec sh -c 'file -b "$1" | grep -q "^Mach-O" && echo "$1"' _ {} \;)
[ "${#machos[@]}" -gt 0 ] || fail "no Mach-O in the bundle"
minos_check "the bundle's executables" "${machos[@]}"

# ---- the signature, last, over the final contents ------------------------------------------------------------
codesign --force --sign - --timestamp=none "$OUT" 2>/dev/null || fail "codesign failed"
codesign --verify --deep --strict "$OUT" 2>/dev/null || fail "the fresh signature does not verify"
echo "make-macos-app: $OUT: $(lipo -archs "$C/MacOS/generals"), version $version, minimum macOS $minos, $(printf '%s\n' $ENTRIES | wc -l | tr -d ' ') licence entries for $(printf '%s\n' $linked | wc -l | tr -d ' ') linked libraries, no art; signed ad hoc and verified"
