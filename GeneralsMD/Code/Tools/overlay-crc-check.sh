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
# P1 step 1's multiplayer check: the fork's overlay searched as a read root (decision 9) must give the
# game exactly what Windows' one folder gives it. Two layouts are built from the same install and the
# same overlay files:
#
#   W  the Windows shape: one folder, the install with the overlay's files copied into it, as
#      generals' post-build copies Code/Data over Run/ on Windows (replay-check.sh's farm);
#   P  the package shape: the install alone as the root, and the overlay as a separate folder passed
#      with -overlay, the code path the app bundle takes.
#
# One short skirmish runs in each, and three values from each run's log must be identical:
#   - "EXE CRC"   GlobalData's executable CRC, which off Windows is the version and the two script files
#                 (SkirmishScripts.scb, MultiplayerScripts.scb): decision 5's inputs;
#   - "INI CRC"   GameEngine::init's checksum over the INI set as loaded: the multiplayer INI check;
#   - the last "HEADLESS CRC", the world state at the last frame.
# Two armed controls, each of which must NOT match W:
#   - P without -overlay: the EXE CRC must differ. That run stops before its INI CRC, since the fork's
#     code needs the fork's INIs (decision 9 records that first stop), so this control shows only that
#     the overlay is seen, not that an INI difference would be;
#   - P with one value changed in the overlay (BalanceReforged.ini's first BuildCost, 1000 to 1001),
#     which runs through: its INI CRC must differ. That is the INI check being able to fail.
#
# The overlay is what ships (P1): Data/INI, Data/Patch.str, Data/Scripts, Data/Turkish,
# Data/German, Data/Ukrainian, Data/Russian, Data/French, Install_Final.bmp, Art/Textures and Window from
# Code/Data, and
# the fork's Reforged*.big from
# GeneralsMD/Run when vendor.sh has fetched them (linked, not copied: 1.65 GB). Both layouts get the
# same files, so what differs is only where they sit.
#
# RULE 9: neither layout's root is the install itself. The install is linked file by file into a farm
# (and, for W, the overlay copied over the links, each link removed before its copy is written).
# The install is listed (sizes, times, BLAKE2) before this script writes anything and again at the end,
# and must be unchanged (Tools/install-guard.sh).
#
# Usage: overlay-crc-check.sh --generals <path> [--data <dir>] [--maxframes <n>, default 20 s of game time] [--keep]
# Exit status: 0 when W and P agree and the control differs; 1 otherwise; 77 without game data.

set -u

GENERALS=""
DATA="${ZH_DATA_DIR:-}"
# Logic frames are seconds of game time times the rate the engine's own header names.
LOGIC_FPS="$(sed -n 's/^[[:space:]]*LOGICFRAMES_PER_SECOND[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' \
	"$(dirname "$0")/../GameEngine/Include/Common/GameCommon.h")"
[ -n "$LOGIC_FPS" ] || { echo "overlay-crc-check: no LOGICFRAMES_PER_SECOND in GameCommon.h" >&2; exit 2; }
MAXFRAMES=$((20 * LOGIC_FPS))
KEEP=0
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--maxframes) MAXFRAMES="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "overlay-crc-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "overlay-crc-check: --generals must name the POSIX generals executable" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

CODE="$(cd "$(dirname "$0")/.." && pwd)"		# GeneralsMD/Code: its Data/ holds the overlay's masters
RUNDIR="$(cd "$CODE/../Run" 2>/dev/null && pwd || true)"	# the fork's art archives, when fetched
INSTALL="$(cd "$DATA/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/overlay-crc-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "overlay-crc-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
TAG="oc$$_"

cleanup() {
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

farm() {	# farm <root>: the install's folders made anew, every file a link
	mkdir -p "$1"
	( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$1/$d"; done
	( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$1/$f"; done
}

stage_overlay() {	# stage_overlay <folder>: the shipped overlay, by the zh_overlay target's own script
	"$CODE/Tools/stage-overlay.sh" "$CODE/Data" "$RUNDIR" "$1"
}

lay_over() {	# lay_over <overlay> <farm>: the overlay's entries into the farm, each farm link removed first
	( cd "$1" && find . \( -type f -o -type l \) ) | while IFS= read -r f; do
		mkdir -p "$(dirname "$2/$f")"
		rm -f -- "$2/$f"
		cp -P -- "$1/$f" "$2/$f"
	done
}

. "$CODE/Tools/install-guard.sh"	# install_snapshot, install_verify
if ! install_snapshot "$INSTALL" "$WORK/install.before"; then
	echo "FAIL: COULD NOT VERIFY the install: its listing before the run could not be made; nothing was run"
	exit 1
fi
stage_overlay "$WORK/overlay"
farm "$WORK/W"
lay_over "$WORK/overlay" "$WORK/W"
farm "$WORK/P"
ARTS=$(ls "$WORK/overlay"/Reforged*.big 2>/dev/null | wc -l | tr -d ' ')
echo "overlay: $(cd "$WORK/overlay" && find . \( -type f -o -type l \) | wc -l | tr -d ' ') files, $ARTS of them the fork's art archives"

# run <name> <root> <switches...>: sets EXE, INI and WORLD from the run's log
run() {
	local name="$1" root="$2"; shift 2
	local prefix="$TAG$name" log="$EXEDIR/${TAG}${name}DebugLogFile.txt"
	mkdir -p "$WORK/user_$name"
	( cd "$root" && ZH_USER_DATA_DIR="$WORK/user_$name" "$GENERALS" -headless -root "$root" "$@" -quickstart -noshellmap \
		-multiInstance -noFPSLimit -maxframes "$MAXFRAMES" -logPrefix "$prefix" \
		-randommap 0 2 128 -autoskirmish 2 -aidiff brutal -seed 0 -observer \
		> "$WORK/$name.out" 2> "$WORK/$name.err" )
	EXE=""; INI=""; WORLD=""
	[ -f "$log" ] || return
	EXE="$(grep -a 'EXE CRC: 0x' "$log" | tail -1 | sed 's/.*EXE CRC: \(0x[0-9A-Fa-f]*\).*/\1/')"
	INI="$(grep -a 'INI CRC is 0x' "$log" | tail -1 | sed 's/.*INI CRC is \(0x[0-9A-Fa-f]*\).*/\1/')"
	WORLD="$(grep -a 'HEADLESS CRC: 0x' "$log" | tail -1 | sed 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\1 at frame \2/')"
}

run W "$WORK/W"
W_EXE="$EXE"; W_INI="$INI"; W_WORLD="$WORLD"
echo "W (one folder):         EXE CRC $W_EXE, INI CRC $W_INI, world $W_WORLD"
run P "$WORK/P" -overlay "$WORK/overlay"
P_EXE="$EXE"; P_INI="$INI"; P_WORLD="$WORLD"
echo "P (root + overlay):     EXE CRC $P_EXE, INI CRC $P_INI, world $P_WORLD"
run C "$WORK/P"
C_EXE="$EXE"; C_INI="$INI"
echo "control (no overlay):   EXE CRC $C_EXE, INI CRC ${C_INI:-(none: stopped before it)}"
cp -R "$WORK/overlay" "$WORK/overlay-edited"
perl -0777 -pi -e 's/BuildCost = 1000/BuildCost = 1001/' "$WORK/overlay-edited/Data/INI/BalanceReforged.ini"
run E "$WORK/P" -overlay "$WORK/overlay-edited"
E_EXE="$EXE"; E_INI="$INI"
echo "control (one INI value): EXE CRC $E_EXE, INI CRC $E_INI"

status=0
if [ -z "$W_EXE" ] || [ -z "$W_INI" ] || [ -z "$W_WORLD" ] || [ -z "$P_EXE" ] || [ -z "$P_INI" ] || [ -z "$P_WORLD" ]; then
	echo "FAIL: a run logged no CRC (see $WORK, kept)"; KEEP=1; status=1
elif [ "$W_EXE" != "$P_EXE" ] || [ "$W_INI" != "$P_INI" ] || [ "$W_WORLD" != "$P_WORLD" ]; then
	echo "FAIL: the overlay layout does not give the game what the one folder gives it"; status=1
else
	echo "ok: the EXE CRC, the INI CRC and the world state agree between W and P"
fi
if ! grep -q "generals: overlay .*searched before the install" "$WORK/P.err"; then
	echo "FAIL: P's run did not report its overlay"; status=1
fi
if [ -z "$C_EXE" ] || [ "$C_EXE" = "$W_EXE" ]; then
	echo "FAIL: the control without the overlay has W's EXE CRC, so this check cannot see the overlay"; status=1
else
	echo "ok: the control without the overlay has another EXE CRC"
fi
if [ -z "$E_INI" ] || [ "$E_INI" = "$W_INI" ] || [ "$E_EXE" != "$W_EXE" ]; then
	echo "FAIL: one changed INI value did not change the INI CRC alone, so the INI comparison cannot fail"; status=1
else
	echo "ok: one changed INI value changes the INI CRC, and only it"
fi
install_verify "$INSTALL" "$WORK/install.before" "$WORK/install.after" || status=1
exit $status
