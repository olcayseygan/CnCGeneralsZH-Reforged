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
# asan-check.sh: the whole game under AddressSanitizer, in a ZH_SANITIZE=address build
# (docs/porting/sanitizers.md).  Two runs, and each must exit 0 with no ASan report:
#   mobstress  headless, the mobstress scenario on Alpine Assault, seed 1, --mob-frames (default 400 s): the
#              simulation, the allocator and its locks, the job threads
#   skirmish   -offscreen and -noaudio at 1920x1080, the seed-1234 generated skirmish, --skirmish-frames
#              (default 60 s): the same plus the GPU device (Metal on macOS, Vulkan on Linux) and the
#              system frameworks under it
# Each run ends with the game's reset after the match, where the use-after-free ZH_SANITIZE first found was.
#
# WHAT A PASS SAYS: ASan saw no invalid access on these two paths in these frames.  Not that there is none
# elsewhere (the network, the shell, saves), and nothing about leaks: detect_leaks is off, because the
# engine frees much of its memory only at exit, or never.
#
# RULE 9, as replay-check.sh has it: the root is a farm of links in a temporary folder, the install is
# listed before and after (install-guard.sh), and the user data and logs stay in that folder.  The
# overlays are staged from this tree by stage-overlay.sh, the shipped one and the dev one (the scenarios).
#
# Usage: asan-check.sh --generals <path> [--data <dir>] [--mob-frames <n>] [--skirmish-frames <n>] [--keep]
#   --generals  a ZH_SANITIZE=address build's generals (refused if ASan is not in it)
#   --data      a folder holding zerohour/; default $ZH_DATA_DIR
#   --keep      leave the temporary folder, with the runs' output and any ASan report, and say where
# Prints one line per run and then "asan-check: PASS" or "asan-check: FAIL".
# Exit status: 0 both runs clean; 1 a run failed (non-zero exit, an ASan report, or no frame limit
# reached); 2 usage, or not an ASan build; 77 no game data; 99 the install changed, or could not be checked.
set -u

GENERALS=""
DATA="${ZH_DATA_DIR:-}"
# Logic frames are seconds of game time times the rate the engine's own header names.
LOGIC_FPS="$(sed -n 's/^[[:space:]]*LOGICFRAMES_PER_SECOND[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' \
	"$(dirname "$0")/../GameEngine/Include/Common/GameCommon.h")"
[ -n "$LOGIC_FPS" ] || { echo "asan-check: no LOGICFRAMES_PER_SECOND in GameCommon.h" >&2; exit 2; }
MOB_FRAMES=$((400 * LOGIC_FPS))
SKIRMISH_FRAMES=$((60 * LOGIC_FPS))
KEEP=0
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--mob-frames) MOB_FRAMES="$2"; shift 2;;
		--skirmish-frames) SKIRMISH_FRAMES="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "asan-check: unknown argument $1" >&2; exit 2;;
	esac
done

if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "asan-check: --generals must name a generals executable" >&2
	exit 2
fi
# An ordinary build would pass trivially: no runtime, no reports.
if ! nm "$GENERALS" 2>/dev/null | grep -q "__asan_init"; then
	echo "asan-check: $GENERALS is not an ASan build (configure with -DZH_SANITIZE=address)" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

CODE="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL="$(cd "$DATA/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"
# A work folder that could not be made is the end of the run: going on with WORK empty would put
# "$WORK/..." at the file system's root.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/asan-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "asan-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
ROOT="$WORK/root"
TAG="ac$$_"

. "$(dirname "$0")/install-guard.sh"	# install_snapshot, install_verify
INSTALL_VERIFIED=0
verify_install() {
	[ "$INSTALL_VERIFIED" -eq 1 ] && return 0
	INSTALL_VERIFIED=1
	install_verify "$INSTALL" "$WORK/install-before.list" "$WORK/install-after.list" > "$WORK/install-verdict.txt" 2>&1
	[ $? -eq 0 ] && return 0
	cat "$WORK/install-verdict.txt" >&2 2>/dev/null || echo "FAIL: COULD NOT VERIFY the install: its work folder $WORK is gone" >&2
	return 1
}
cleanup() {
	if ! verify_install; then
		trap - EXIT
		echo "kept for inspection: $WORK" >&2
		exit 99
	fi
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

install_snapshot "$INSTALL" "$WORK/install-before.list"

mkdir -p "$ROOT"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done
"$(dirname "$0")/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$WORK/overlay" > /dev/null
"$(dirname "$0")/stage-overlay.sh" "$CODE/Data" "" "$WORK/overlay-dev" --dev > /dev/null

FAILED=0
run() {	# run <name> <frames> <switches...>
	local name="$1" frames="$2"; shift 2
	local prefix="$TAG$name" user="$WORK/user-$name"
	mkdir -p "$user"
	# halt_on_error: the first report ends the run.  abort_on_error=0: macOS's default aborts at exit once
	# anything was reported, which would turn every exit code into 134.
	( cd "$ROOT" && ASAN_OPTIONS="halt_on_error=1:abort_on_error=0:detect_leaks=0:log_path=$WORK/asan-$name" \
		ZH_OFFSCREEN_HZ=120 ZH_USER_DATA_DIR="$user" \
		perl -e 'alarm shift; exec @ARGV or exit 127' 3600 "$GENERALS" -root "$ROOT" -overlay "$WORK/overlay" \
		-overlay "$WORK/overlay-dev" -multiInstance -maxframes "$frames" -logPrefix "$prefix" "$@" \
		> "$WORK/$name.out" 2> "$WORK/$name.err" )
	local status=$?
	local reports; reports=$(cat "$WORK/asan-$name".* 2>/dev/null | grep -c "ERROR: AddressSanitizer")
	local result; result=$(grep -m1 -o "HEADLESS RESULT: [^(]*" "$EXEDIR/${prefix}DebugLogFile.txt" 2>/dev/null)
	local first; first=$(cat "$WORK/asan-$name".* 2>/dev/null | grep -m1 -o "ERROR: AddressSanitizer: [a-z-]*")
	local verdict=ok
	if [ "$status" -ne 0 ] || [ "$reports" -ne 0 ] || ! printf '%s' "$result" | grep -q "frame limit reached"; then
		verdict=FAILED; FAILED=1
	fi
	echo "asan-check: $name: $verdict (exit $status, $reports ASan reports${first:+, $first}; ${result:-no HEADLESS RESULT line})"
}
run mobstress "$MOB_FRAMES" -headless -noFPSLimit -quickstart -noshellmap -observer \
	-map "Maps\\Alpine Assault\\Alpine Assault.map" -autoskirmish 2 -takeover -side 0 FactionGLA -side 1 FactionAmerica \
	-seed 1 -scenario mobstress
run skirmish "$SKIRMISH_FRAMES" -offscreen -noaudio -xres 1920 -yres 1080 -randommap 1234 2 small -autoskirmish 2 \
	-seed 1234 -observer

if [ "$FAILED" -ne 0 ]; then
	echo "asan-check: FAIL"
	KEEP=1
	exit 1
fi
echo "asan-check: PASS"
exit 0
