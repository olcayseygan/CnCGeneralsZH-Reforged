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
# P1 step 2's proof that the game cannot write its install: the roots are read-only, so every write the
# engine addresses relative to the install is refused (posixpath's refuses_write) rather than done.
#
# One short skirmish, rooted at a farm of the install (its folders made anew, every file a link) with
# the shipped overlay passed as -overlay, as the package runs. Into the farm goes one real file the
# engine is known to try to delete on every start, Data/INI/INIZH.big (GameEngine::init's patch-1.01
# clean-up). Afterwards:
#   - the planted file is still there, and the run reports (stderr) that the deletion was refused;
#   - the farm's listing (every name, and every link's target) is what it was before the run;
#   - the install's listing (every file's size, modification time and BLAKE2 hash, taken before this
#     script writes anything and again after both runs) is what it was. That is the rule-9 listing
#     diff, and the one that matters: a write through a farm link would land in the install.
# The armed control: a run with -writableRoot must delete the planted file, so the check can see a
# write when one happens. It runs in a root holding only that file, never in a farm of the install.
#
# RULE 9 still holds while this runs: the root is a farm, never the install.
#
# Usage: root-readonly-check.sh --generals <path> [--data <dir>] [--maxframes <n>, default 20 s of game time] [--keep]
# Exit status: 0 on a pass; 1 otherwise; 77 without game data.

set -u

GENERALS=""
DATA="${ZH_DATA_DIR:-}"
# Logic frames are seconds of game time times the rate the engine's own header names.
LOGIC_FPS="$(sed -n 's/^[[:space:]]*LOGICFRAMES_PER_SECOND[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' \
	"$(dirname "$0")/../GameEngine/Include/Common/GameCommon.h")"
[ -n "$LOGIC_FPS" ] || { echo "root-readonly-check: no LOGICFRAMES_PER_SECOND in GameCommon.h" >&2; exit 2; }
MAXFRAMES=$((20 * LOGIC_FPS))
KEEP=0
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--maxframes) MAXFRAMES="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "root-readonly-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "root-readonly-check: --generals must name the POSIX generals executable" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

CODE="$(cd "$(dirname "$0")/.." && pwd)"
RUNDIR="$(cd "$CODE/../Run" 2>/dev/null && pwd || true)"
INSTALL="$(cd "$DATA/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/root-readonly-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "root-readonly-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
TAG="ro$$_"

cleanup() {
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

farm() {
	mkdir -p "$1"
	( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$1/$d"; done
	( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$1/$f"; done
}

# plant <root>: a real Data/INI/INIZH.big in <root>. The farm entry is REMOVED first: the install has
# that file, so the entry is a link to it, and writing the link writes the install (2026-09-26: this
# script's first version overwrote the install's copy exactly so, P1).
plant() {
	mkdir -p "$1/Data/INI"
	rm -f -- "$1/Data/INI/INIZH.big"
	printf 'planted by root-readonly-check\n' > "$1/Data/INI/INIZH.big"
	if [ -L "$1/Data/INI/INIZH.big" ] || [ ! -f "$1/Data/INI/INIZH.big" ]; then
		echo "FAIL: the plant in $1 is not a regular file of its own; stopping before any run"; exit 1
	fi
}

stage_overlay() {	# stage_overlay <folder>: the shipped overlay, by the zh_overlay target's own script
	"$CODE/Tools/stage-overlay.sh" "$CODE/Data" "$RUNDIR" "$1"
}

. "$CODE/Tools/install-guard.sh"	# the install's listing: install_snapshot, install_verify

# snapshot <dir> <out> [hash]: every entry, with a link's target or a file's size and mtime; with
# "hash", each regular file's BLAKE2 as well (read only). The farms' listing: links and folders.
snapshot() {
	python3 - "$1" "${3:-}" > "$2" <<'EOF'
import hashlib, os, sys
root = sys.argv[1]
hashing = len(sys.argv) > 2 and sys.argv[2] == 'hash'
def digest(path):
    h = hashlib.blake2b(digest_size=16)
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()
for base, dirs, files in os.walk(root):
    dirs.sort()
    for name in sorted(dirs + files):
        p = os.path.join(base, name)
        rel = os.path.relpath(p, root)
        if os.path.islink(p):
            print(rel, '->', os.readlink(p))
        else:
            st = os.lstat(p)
            if os.path.isdir(p):
                print(rel, 'dir', int(st.st_mtime_ns))
            else:
                print(rel, st.st_size, int(st.st_mtime_ns), digest(p) if hashing else '')
EOF
}

run() {	# run <name> <root> <switches...>; RUN_EXE overrides the executable
	local name="$1" root="$2"; shift 2
	mkdir -p "$WORK/user_$name"
	( cd "$root" && ZH_USER_DATA_DIR="$WORK/user_$name" "${RUN_EXE:-$GENERALS}" -headless -root "$root" "$@" -quickstart -noshellmap \
		-multiInstance -noFPSLimit -maxframes "$MAXFRAMES" -logPrefix "$TAG$name" \
		-randommap 0 2 128 -autoskirmish 2 -aidiff brutal -seed 0 -observer \
		> "$WORK/$name.out" 2> "$WORK/$name.err" )
	LOG="$EXEDIR/${TAG}${name}DebugLogFile.txt"
}

# The install's listing first, before this script writes anything anywhere, so that a write by the
# harness itself is caught as well as one by the game.
if ! install_snapshot "$INSTALL" "$WORK/install.before"; then
	echo "FAIL: COULD NOT VERIFY the install: its listing before the run could not be made; nothing was run"
	exit 1
fi
mkdir -p "$WORK/overlay"
stage_overlay "$WORK/overlay"
status=0

# ---- the run: read-only (the default) -------------------------------------------------------------
farm "$WORK/R"
plant "$WORK/R"
snapshot "$WORK/R" "$WORK/farm.before"
run R "$WORK/R" -overlay "$WORK/overlay"
snapshot "$WORK/R" "$WORK/farm.after"

if ! grep -aq 'HEADLESS CRC: 0x' "$LOG" 2>/dev/null; then
	echo "FAIL: the read-only run did not reach its match (see $WORK, kept)"; KEEP=1; status=1
fi
if [ -f "$WORK/R/Data/INI/INIZH.big" ]; then
	echo "ok: the planted Data/INI/INIZH.big survived GameEngine::init"
else
	echo "FAIL: the planted Data/INI/INIZH.big was deleted"; status=1
fi
if grep -aq 'refused to delete "Data\\INI\\INIZH.big"' "$WORK/R.err" 2>/dev/null; then
	echo "ok: the run reports the deletion refused"
else
	echo "FAIL: the log does not record the refused deletion"; status=1
fi
if cmp -s "$WORK/farm.before" "$WORK/farm.after"; then
	echo "ok: the farm is as it was ($(wc -l < "$WORK/farm.before" | tr -d ' ') entries)"
else
	echo "FAIL: the farm changed:"; diff "$WORK/farm.before" "$WORK/farm.after" | head -10; status=1
fi
grep -a 'refused to' "$WORK/R.err" 2>/dev/null | sed 's/^/  stderr: /' | head -5

# ---- the armed control: -writableRoot ---------------------------------------------------------------
# In a root with NO links to the install, only the planted file: GameEngine::init deletes it before
# it needs any game data, and then stops for want of GameData.ini. A run that may write can reach
# nothing but this folder.
# It also runs as an app bundle would: the executable hard-linked (no copy) into Fake.app/Contents/MacOS,
# where the debug log must NOT be written (a write into a bundle breaks its signature) but in the
# user data directory's Logs folder instead (getLogDirectory).
mkdir -p "$WORK/W" "$WORK/Fake.app/Contents/MacOS"
plant "$WORK/W"
if ! ln "$GENERALS" "$WORK/Fake.app/Contents/MacOS/generals" 2>/dev/null; then
	cp "$GENERALS" "$WORK/Fake.app/Contents/MacOS/generals"
fi
RUN_EXE="$WORK/Fake.app/Contents/MacOS/generals" run W "$WORK/W" -writableRoot
if [ -f "$WORK/W/Data/INI/INIZH.big" ]; then
	echo "FAIL: with -writableRoot the planted file survived too, so this check cannot see a write"; status=1
else
	echo "ok: the control with -writableRoot deleted it, as a writable root does"
fi
if [ -f "$WORK/user_W/Logs/${TAG}WDebugLogFile.txt" ] && ! ls "$WORK/Fake.app/Contents/MacOS/"*DebugLogFile* >/dev/null 2>&1; then
	echo "ok: run from inside an app bundle, the log went to the user data's Logs, not into the bundle"
else
	echo "FAIL: the bundled run's log is not where it belongs:"; ls "$WORK/Fake.app/Contents/MacOS" "$WORK/user_W" "$WORK/user_W/Logs" 2>&1 | head; status=1
fi
# The install once more, hashed, after both runs: the rule-9 listing diff, by content
install_verify "$INSTALL" "$WORK/install.before" "$WORK/install.after" || status=1
exit $status
