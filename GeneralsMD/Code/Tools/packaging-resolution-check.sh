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
# P1 step 3's proof that the package layout resolves every file exactly as Windows' one folder does
# (decision 9), path by path, not only in the CRCs that overlay-crc-check.sh compares.
#
#   W  the Windows shape: one folder, a farm of the install with the staged overlay copied into it;
#   P  the package shape: the farm of the install as the root, the staged overlay as -overlay.
#
# The overlay is staged by Tools/stage-overlay.sh, the script the build's zh_overlay target and the app
# bundle use. Each layout is started with -dumpFileResolution, which after GameEngine::init writes, for
# every path the game can open (the loose files under its roots and every file in the mounted
# archives), "loose" with the size and hash of its bytes, or the archive that won it with the member's
# size, then the INI and EXE CRCs (PosixFileResolutionDump.cpp). The two dumps must be identical.
#
# And B, the bundle shape (P1 step 4): the executable hard-linked into Fake.app/Contents/MacOS with the
# staged overlay copied to Contents/Resources/Overlay, run with NO -root and NO -overlay, so the game finds
# both itself: the overlay beside it, and the install through Registry.ini's InstallPath (naming a farm of
# the install) with an empty home, no known places. Its dump must equal W's too. With no InstallPath at
# all, the same bundle must stop and say how to give it one (headless: no chooser).
#
# The armed control: P again with the overlay's ReforgedTextures.big renamed ZReforgedTextures.big. It
# then mounts after TexturesZH.big instead of before, so every path both archives hold must change
# hands, and the dump must differ in exactly such lines. That proves the comparison sees archive order,
# not only names.
#
# Rule 9, and P1 step 2's standard: the install is hashed before this script writes anything and again
# at the end, and must be unchanged; every write into a farm path removes the farm's link first.
#
# Usage: packaging-resolution-check.sh --generals <path> [--data <dir>] [--keep]
# Exit status: 0 on a pass; 1 otherwise; 77 without game data.

set -u

GENERALS=""
DATA="${ZH_DATA_DIR:-}"
KEEP=0
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "packaging-resolution-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "packaging-resolution-check: --generals must name the POSIX generals executable" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

TOOLS="$(cd "$(dirname "$0")" && pwd)"
CODE="$(cd "$TOOLS/.." && pwd)"
INSTALL="$(cd "$DATA/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"
# A work folder that could not be made is the end of the run: going on with WORK empty would put
# "$WORK/..." at the file system's root.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/packaging-resolution-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "packaging-resolution-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
TAG="pr$$_"

cleanup() {
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

. "$TOOLS/install-guard.sh"	# install_snapshot, install_verify

farm() {	# the install's folders made anew, every file a link
	mkdir -p "$1"
	( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$1/$d"; done
	( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$1/$f"; done
}

lay_over() {	# lay_over <overlay> <farm>: the overlay's entries into the farm, each farm link removed first
	( cd "$1" && find . \( -type f -o -type l \) ) | while IFS= read -r f; do
		mkdir -p "$(dirname "$2/$f")"
		rm -f -- "$2/$f"
		cp -P -- "$1/$f" "$2/$f"
	done
}

dump() {	# dump <name> <root> <out> <switches...>
	local name="$1" root="$2" out="$3"; shift 3
	mkdir -p "$WORK/user_$name"
	# the Reforged archives are what this checks the order of, and a first run leaves them out
	echo "ClassicGraphics = no" > "$WORK/user_$name/Options.ini"
	( cd "$root" && ZH_USER_DATA_DIR="$WORK/user_$name" "$GENERALS" -headless -root "$root" "$@" -quickstart -noshellmap \
		-multiInstance -logPrefix "$TAG$name" -dumpFileResolution "$out" \
		> "$WORK/$name.out" 2> "$WORK/$name.err" )
}

if ! install_snapshot "$INSTALL" "$WORK/install.before"; then
	echo "FAIL: COULD NOT VERIFY the install: its listing before the run could not be made; nothing was run"
	exit 1
fi
"$TOOLS/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$WORK/overlay"
ARTS=$(ls "$WORK/overlay"/Reforged*.big 2>/dev/null | wc -l | tr -d ' ')

farm "$WORK/W"
lay_over "$WORK/overlay" "$WORK/W"
farm "$WORK/P"
dump W "$WORK/W" "$WORK/W.dump"
dump P "$WORK/P" "$WORK/P.dump" -overlay "$WORK/overlay"

status=0
if [ ! -s "$WORK/W.dump" ] || [ ! -s "$WORK/P.dump" ]; then
	echo "FAIL: a layout wrote no dump (see $WORK, kept)"; KEEP=1; status=1
else
	loose=$(grep -c $'\tloose\t' "$WORK/W.dump"); archived=$(grep -c $'\tarchive ' "$WORK/W.dump")
	echo "W: $loose loose paths and $archived archived; the overlay staged with $ARTS art archives"
	grep -a '^INI CRC\|^EXE CRC' "$WORK/W.dump" | sed 's/^/W: /'
	if cmp -s "$WORK/W.dump" "$WORK/P.dump"; then
		echo "ok: every path resolves the same in W and P ($(wc -l < "$WORK/W.dump" | tr -d ' ') lines, the CRCs included)"
	else
		echo "FAIL: the package layout resolves differently:"; diff "$WORK/W.dump" "$WORK/P.dump" | head -20; status=1
	fi
fi

# ---- B: the bundle finds its overlay and its install itself ----------------------------------------
mkdir -p "$WORK/Fake.app/Contents/MacOS" "$WORK/Fake.app/Contents/Resources" "$WORK/bhome" "$WORK/user_B"
if ! ln "$GENERALS" "$WORK/Fake.app/Contents/MacOS/generals" 2>/dev/null; then
	cp "$GENERALS" "$WORK/Fake.app/Contents/MacOS/generals"
fi
cp -R -P "$WORK/overlay" "$WORK/Fake.app/Contents/Resources/Overlay"
farm "$WORK/B"
( cd "$WORK" && HOME="$WORK/bhome" ZH_USER_DATA_DIR="$WORK/user_B" "$WORK/Fake.app/Contents/MacOS/generals" -headless \
	> "$WORK/B0.out" 2> "$WORK/B0.err" )
if grep -q 'start the game with -root' "$WORK/B0.err"; then
	echo "ok: with nothing registered or known, the bundle stops and says to use -root"
else
	echo "FAIL: the bundle with no install known did not say so:"; head -3 "$WORK/B0.err"; status=1
fi
printf 'InstallPath = %s\n' "$WORK/B" > "$WORK/user_B/Registry.ini"
( cd "$WORK" && HOME="$WORK/bhome" ZH_USER_DATA_DIR="$WORK/user_B" "$WORK/Fake.app/Contents/MacOS/generals" -headless \
	-quickstart -noshellmap -multiInstance -logPrefix "${TAG}B" -dumpFileResolution "$WORK/B.dump" \
	> "$WORK/B.out" 2> "$WORK/B.err" )
if [ -s "$WORK/B.dump" ] && cmp -s "$WORK/W.dump" "$WORK/B.dump" && grep -q 'Resources/Overlay, searched before the install' "$WORK/B.err"; then
	echo "ok: the bundle, rooted by Registry.ini with its own overlay, resolves every path as W does"
else
	echo "FAIL: the bundle layout did not resolve as W:"; head -4 "$WORK/B.err"; [ -s "$WORK/B.dump" ] && diff "$WORK/W.dump" "$WORK/B.dump" | head -5; status=1
fi

# ---- the armed control ----------------------------------------------------------------------------
if [ "$ARTS" -gt 0 ] && [ -L "$WORK/overlay/ReforgedTextures.big" ]; then
	cp -R -P "$WORK/overlay" "$WORK/overlay-renamed"
	mv "$WORK/overlay-renamed/ReforgedTextures.big" "$WORK/overlay-renamed/ZReforgedTextures.big"
	farm "$WORK/C"
	dump C "$WORK/C" "$WORK/C.dump" -overlay "$WORK/overlay-renamed"
	# lines where the winner changed from ReforgedTextures.big to another archive (not only its name)
	moved=$(diff "$WORK/P.dump" "$WORK/C.dump" | grep -a '^> ' | grep -a $'\tarchive ' | grep -av -i 'reforgedtextures.big' | wc -l | tr -d ' ')
	if [ -s "$WORK/C.dump" ] && [ "$moved" -gt 0 ]; then
		echo "ok: the control (ReforgedTextures.big mounted after TexturesZH.big) moves $moved paths to another archive"
	else
		echo "FAIL: the control moved no path to another archive, so this check cannot see archive order"; status=1
	fi
else
	echo "FAIL: no ReforgedTextures.big in the overlay (vendor.sh's art), so the control cannot run"; status=1
fi

install_verify "$INSTALL" "$WORK/install.before" "$WORK/install.after" || status=1
exit $status
