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
# replay-check.sh: replay-check.ps1's POSIX twin (E1).  For each seed, plays a headless skirmish, plays
# its own replay back, plays the same seed a second time, and compares the three runs' CRC of every
# object in the world at the frame they stopped on: the last "HEADLESS CRC: 0x... at frame N" line,
# which any build writes at the end of an unattended run.
#
# WHAT THIS PROVES, AND WHAT IT DOES NOT.  Same-machine determinism only: a recording and its playback
# walk the same frames, and two runs from one seed agree, on this machine and this build.  That catches
# uninitialised memory, iteration order that depends on addresses, and anything else that differs
# between two runs of one binary.  It says NOTHING about whether this build agrees with a Windows build:
# that needs a replay recorded on Windows, played back here, and none exists yet.  Do not call a pass
# "parity" (E1).
#
# A mismatch names the frame the CRC was taken at, not the frame the runs parted: it says a divergence
# happened, not where.  -DebugCRCFromFrame narrows it down afterwards.
#
# RULE 9: the game never runs with the real install as its root.  The root is a farm in a temporary
# folder: the install's directories made anew and every file a symbolic link, and nothing is written
# into it.  The fork's own data is the overlay the app bundle carries (P1): staged by
# Tools/stage-overlay.sh, the zh_overlay target's script, and passed with -overlay, so these runs
# resolve every file as the shipped game does (packaging-resolution-check.sh proves that layout equal
# to Windows' one folder).  The user data
# (ZH_USER_DATA_DIR) is in the same folder, and the folder is removed at the end.  The logs go next to
# the executable, as on Windows, each under its own -logPrefix, and are removed once read.
#
# Usage: replay-check.sh --generals <path> | --app <.app> | --package <folder> [--data <dir>] [--seeds "0 1"] [--players 2]
#          [--aidiff brutal] [--seconds 400 | --maxframes <n>] [--cells <n>] [--extra "<args>"]
#          [--control] [--extended] [--keep] [--log-lines <regex>]
#   --seconds         match length in seconds of game time (default 400), turned into logic frames at
#                     LOGICFRAMES_PER_SECOND from GameCommon.h; --maxframes gives the frames outright
#   --data            a folder holding zerohour/ (with the base game in zerohour/ZH_Generals, as the
#                     install has it); default $ZH_DATA_DIR
#   --cells           playable cells a side of the generated map.  Default: none given, so the
#                     generator sizes the map for the player count (normal).  replay-check.ps1 passes
#                     128, below even the generator's small size for two players, and at 128 seed 1
#                     puts the two starts 124 units apart: both AIs judge every build site unsafe and
#                     the match stays idle, so a CRC over it proves little
#   --extra           switches for every run of every seed.  One that reaches the match has to be on
#                     for the recording and the playback alike
#   --control         the harness's own armed control: before the playback, the kept replay's game
#                     seed (SD= in its header) has its last digit changed, so the playback plays a
#                     different world from the same commands, and every seed must be reported DIVERGED
#   --extended        the wider backstop, not in ctest: seeds 2 to 5, each at 2 and at 4 players (eight
#                     matches), at --maxframes (default 400 s of game time).  --seeds and --players are ignored
#   --keep            leave the temporary folder and the logs, and say where
#   --log-lines <re>  print each live run's log lines matching the extended regex, as "  log: <line>",
#                     before the log is removed (Tests/run_locomotor_check.sh reads port defect 33's check)
#   --package <folder>  the same for a Linux package (P3, Tools/linux-portable.sh): its launcher
#                     zero-hour-reforged.sh runs with no -root and no -overlay; the overlay it must report is
#                     <folder>/share/zero-hour-reforged/overlay, the install is found through Registry.ini
#   --appimage <file>  the same for the package as one AppImage: the file itself runs, mounted through FUSE; the
#                     overlay it must report is under its own mount point, which is new every run
#   --installed <command> [--overlay-pattern <glob>]
#                     the same for an installed package (a .deb, .rpm, Arch package or Flatpak): <command> exists
#                     only where REPLAY_CHECK_RUNNER runs it (a container the package is installed in, or
#                     "flatpak run" with the app's ID as <command>), so nothing of it is looked for here.  It
#                     runs with no -root and no -overlay, as --package; the overlay it must report matches
#                     <glob> (default */share/zero-hour-reforged/overlay).  Needs REPLAY_CHECK_RUNNER
#   REPLAY_CHECK_RUNNER  (environment) a command every run is started through, e.g. a script that runs
#                     its arguments inside a container (P3's clean-environment E1); unset, none
#   --app <.app>      E1 on the bundle itself, "tests what ships" (P1 step 5): its Contents/MacOS/generals
#                     runs with NO -root and NO -overlay, so the bundle's own discovery finds both - its
#                     overlay in Contents/Resources, and the install through Registry.ini's InstallPath,
#                     which names the farm (rule 9 as ever; HOME is an empty folder, so the known places
#                     cannot find anything).  Its logs are the bundle's, in the user data's Logs/.  After
#                     every run the bundle's signature must still verify --deep --strict: nothing may be
#                     written into it.  Replaces --generals
# Exit status: the number of matches that failed (0 when all agree), plus one for a broken seal (--app).
# 77 when there is no game data.
# 99 when the install changed during the run, or when that could not be checked: every file of it is
# listed with its hash before the farm is built and again at the end (Tools/install-guard.sh). A
# difference is "THE INSTALL CHANGED"; a listing that is missing or could not be made is "COULD NOT
# VERIFY", never a change. Either fails the run whatever the matches said.

set -u
# Every game this starts is unattended: no box, chooser or crash report may wait on a person (EarlyCommandLine.h).
export ZH_UNATTENDED=1

GENERALS=""
APP=""
PACKAGED_OVERLAY=""
INSTALLED=0
REPLAY_CHECK_RUNNER="${REPLAY_CHECK_RUNNER:-}"
DATA="${ZH_DATA_DIR:-}"
SEEDS="0 1"
PLAYERS=2
AIDIFF=brutal
# The logic rate, read from the engine's own header so this script never carries a copy of it.
LOGIC_FPS="$(sed -n 's/^[[:space:]]*LOGICFRAMES_PER_SECOND[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' \
	"$(dirname "$0")/../GameEngine/Include/Common/GameCommon.h")"
[ -n "$LOGIC_FPS" ] || { echo "replay-check: no LOGICFRAMES_PER_SECOND in GameCommon.h" >&2; exit 2; }
MAXFRAMES=$((400 * LOGIC_FPS))		# 400 s, 6 min 40 s of game time
CELLS=""
EXTRA=""
CONTROL=0
EXTENDED=0
KEEP=0
LOG_LINES=""
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--app) APP="$(cd "$2" && pwd)"; GENERALS="$APP/Contents/MacOS/generals"; PACKAGED_OVERLAY="$APP/Contents/Resources/Overlay"; shift 2;;
		--package) APP="$(cd "$2" && pwd -P)"; GENERALS="$APP/zero-hour-reforged.sh"; PACKAGED_OVERLAY="$APP/share/zero-hour-reforged/overlay"; shift 2;;
		--appimage) APP="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"; GENERALS="$APP"; PACKAGED_OVERLAY="*/share/zero-hour-reforged/overlay"; shift 2;;
		--installed) APP="$2"; GENERALS="$2"; INSTALLED=1
			[ -n "$PACKAGED_OVERLAY" ] || PACKAGED_OVERLAY="*/share/zero-hour-reforged/overlay"; shift 2;;
		--overlay-pattern) PACKAGED_OVERLAY="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--seeds) SEEDS="$2"; shift 2;;
		--players) PLAYERS="$2"; shift 2;;
		--aidiff) AIDIFF="$2"; shift 2;;
		--maxframes) MAXFRAMES="$2"; shift 2;;
		--seconds) MAXFRAMES=$(($2 * LOGIC_FPS)); shift 2;;
		--cells) CELLS="$2"; shift 2;;
		--extra) EXTRA="$2"; shift 2;;
		--control) CONTROL=1; shift;;
		--extended) EXTENDED=1; shift;;
		--keep) KEEP=1; shift;;
		--log-lines) LOG_LINES="$2"; shift 2;;
		*) echo "replay-check: unknown argument $1" >&2; exit 2;;
	esac
done

if [ $INSTALLED -eq 1 ]; then
	if [ -z "$REPLAY_CHECK_RUNNER" ]; then
		echo "replay-check: --installed needs REPLAY_CHECK_RUNNER, where the command exists" >&2
		exit 2
	fi
elif [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "replay-check: --generals must name the POSIX generals executable" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

CODE="$(cd "$(dirname "$0")/.." && pwd)"		# GeneralsMD/Code: its Data/ is the overlay
INSTALL="$(cd "$DATA/zerohour" && pwd)"
if [ $INSTALLED -eq 0 ]; then
	EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
	GENERALS="$EXEDIR/$(basename "$GENERALS")"		# absolute: every run starts in the root
fi
# A work folder that could not be made is the end of the run: going on with WORK empty would put
# "$WORK/..." at the file system's root.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/replay-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "replay-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
ROOT="$WORK/root"
USERDATA="$WORK/user"
TAG="rc$$_"		# every log this run writes starts with it
[ -n "$APP" ] && EXEDIR="$USERDATA/Logs"		# a bundle or package writes its logs there (ExecutableDirectory.cpp)

# The install, listed before anything else happens (RULE 9: a farm entry is a link into it, so a write
# through one would change it).  Checked again on the way out, however the run ends.
. "$(dirname "$0")/install-guard.sh"	# install_snapshot, install_verify
INSTALL_VERIFIED=0
verify_install() {
	[ "$INSTALL_VERIFIED" -eq 1 ] && return 0
	INSTALL_VERIFIED=1
	install_verify "$INSTALL" "$WORK/install-before.list" "$WORK/install-after.list" > "$WORK/install-verdict.txt" 2>&1
	local verdict=$?
	[ "$verdict" -eq 0 ] && return 0
	cat "$WORK/install-verdict.txt" >&2 2>/dev/null || echo "FAIL: COULD NOT VERIFY the install: its work folder $WORK is gone" >&2
	return 1
}

cleanup() {
	if ! verify_install; then
		trap - EXIT
		# the install controls fail on purpose, and leave nothing behind; a real failure keeps the folder
		if [ -n "${REPLAY_CHECK_CONTROL_INSTALL:-}" ]; then
			rm -rf -- "${WORK:?}"
			rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
		else
			echo "kept for inspection: $WORK" >&2
		fi
		exit 99
	fi
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

install_snapshot "$INSTALL" "$WORK/install-before.list"	# a failure here is found by verify_install
# The check's own armed controls, which never touch the install (used by Tests/run_replay_check.sh):
#   REPLAY_CHECK_CONTROL_INSTALL=changed  spoils the saved listing, so the end must report a change;
#   REPLAY_CHECK_CONTROL_INSTALL=missing  removes it, so the end must say it COULD NOT VERIFY.
case "${REPLAY_CHECK_CONTROL_INSTALL:-}" in
	"") ;;
	missing) rm -f -- "$WORK/install-before.list" ;;
	*) echo "replay-check-control 0 0 00000000000000000000000000000000" >> "$WORK/install-before.list" ;;
esac

# ---- the farm ------------------------------------------------------------------------------------
mkdir -p "$ROOT" "$USERDATA"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done

# The overlay as it ships (P1 step 3): its own folder, never written into the farm.  With --app it is
# the bundle's own, found by the game; the install is the farm, through Registry.ini.
OVERLAY="$WORK/overlay"
if [ -n "$APP" ]; then
	mkdir -p "$WORK/home"
	printf 'InstallPath = %s\n' "$ROOT" > "$USERDATA/Registry.ini"
else
	"$(dirname "$0")/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$OVERLAY"
fi


# ---- a run ---------------------------------------------------------------------------------------
# Sets RUN_CRC and RUN_FRAME from the run's last HEADLESS CRC line, RUN_RESULT from its HEADLESS
# RESULT line, RUN_BUILT to the number of structures the AI put up after frame 0, and RUN_BUILT_NAMES
# to their distinct template names, sorted (port defect 33's check reads the Supply Centers there); empty
# when the run wrote none.
run_game() {	# run_game <log prefix> <switches...>
	local prefix="$TAG$1"; shift
	local log="$EXEDIR/${prefix}DebugLogFile.txt"
	rm -f -- "$log"
	# -noFPSLimit as replay-check.ps1 has it: nothing paces a headless run, and it cannot touch the
	# logic.  -multiInstance so that a run does not wait on another copy's lock.
	if [ -n "$APP" ]; then
		( cd "$WORK" && HOME="$WORK/home" ZH_USER_DATA_DIR="$USERDATA" $REPLAY_CHECK_RUNNER "$GENERALS" -headless -quickstart -noshellmap \
			-multiInstance -noFPSLimit -maxframes "$MAXFRAMES" -logPrefix "$prefix" "$@" $EXTRA \
			> "$WORK/${prefix}.out" 2> "$WORK/${prefix}.err" )
	else
		( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA" $REPLAY_CHECK_RUNNER "$GENERALS" -headless -root "$ROOT" -overlay "$OVERLAY" -quickstart -noshellmap \
			-multiInstance -noFPSLimit -maxframes "$MAXFRAMES" -logPrefix "$prefix" "$@" $EXTRA \
			> "$WORK/${prefix}.out" 2> "$WORK/${prefix}.err" )
	fi
	RUN_STATUS=$?
	RUN_CRC=""; RUN_FRAME=""; RUN_RESULT=""; RUN_BUILT=0; RUN_BUILT_NAMES=""; RUN_STATS=""; RUN_LOG_LINES=""
	# --app, --package: the bundle or package must have found its own overlay (PosixMain says so on stderr);
	# a run that did not is reported as having no result
	if [ -n "$APP" ] && ! grep -a "^generals: overlay .*, searched before the install" "$WORK/${prefix}.err" \
			| sed 's/^generals: overlay \(.*\), searched before the install$/\1/' \
			| { while IFS= read -r o; do case "$o" in $PACKAGED_OVERLAY) exit 0;; esac; done; exit 1; }; then
		echo "(the bundle or package did not report its own overlay in $WORK/${prefix}.err) "
		return
	fi
	[ -f "$log" ] || return
	local line
	line="$(grep -a 'HEADLESS CRC: 0x' "$log" | tail -1)"
	RUN_CRC="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\1/p')"
	RUN_FRAME="$(printf '%s' "$line" | sed -n 's/.*HEADLESS CRC: \(0x[0-9A-Fa-f]*\) at frame \([0-9]*\).*/\2/p')"
	RUN_RESULT="$(grep -a 'HEADLESS RESULT: ' "$log" | tail -1 | sed 's/.*HEADLESS RESULT: //')"
	RUN_BUILT="$(grep -a -c 'AI BUILT frame [1-9]' "$log")"
	RUN_BUILT_NAMES="$(grep -a 'AI BUILT frame [1-9]' "$log" | sed -n "s/.*player [0-9]* '\([^']*\)'.*/\1/p" | sort -u | paste -sd ' ' -)"
	RUN_LOG_LINES=""
	if [ -n "$LOG_LINES" ]; then RUN_LOG_LINES="$(grep -a -E -- "$LOG_LINES" "$log" | tr -d '\r' | sed 's/^/  log: /')"; fi
	# each player's fight: units built, units lost, kills, peak units, buildings built, buildings lost
	RUN_STATS="$(grep -a 'HEADLESS PLAYER' "$log" | sed -E 's/.*units ([0-9]+) built ([0-9]+) lost ([0-9]+) killed peak ([0-9]+) \| buildings ([0-9]+) built ([0-9]+) lost.*/units \1 lost \2 kills \3 peak \4 buildings \5 lost \6/' | paste -sd ';' - | sed 's/;/; /g')"
}

REPLAYS="$USERDATA/Replays"
# The matches, as seed:players.
MATCHES=""
if [ "$EXTENDED" -eq 1 ]; then
	for seed in 2 3 4 5; do MATCHES="$MATCHES $seed:2 $seed:4"; done
else
	for seed in $SEEDS; do MATCHES="$MATCHES $seed:$PLAYERS"; done
fi
failures=0
nseeds=0
for match in $MATCHES; do
	seed="${match%%:*}"; players="${match##*:}"; name="det${seed}_${players}p"
	nseeds=$((nseeds + 1))
	printf 'seed %s, %s players: recording ... ' "$seed" "$players"
	rm -f -- "$REPLAYS/00000000.rep"
	# -observer, so every side is AI and the command stream is the AI's own decisions
	run_game "${name}_live" -randommap "$seed" "$players" $CELLS -autoskirmish "$players" \
		-aidiff "$AIDIFF" -seed "$seed" -observer
	LIVE_CRC="$RUN_CRC"; LIVE_FRAME="$RUN_FRAME"; LIVE_BUILT="$RUN_BUILT"; LIVE_STATS="$RUN_STATS"; LIVE_BUILT_NAMES="$RUN_BUILT_NAMES"
	if [ -n "$RUN_LOG_LINES" ]; then printf '\n%s\n' "$RUN_LOG_LINES"; fi
	if [ -z "$LIVE_CRC" ]; then echo "no result from the live run (exit $RUN_STATUS)"; failures=$((failures + 1)); continue; fi
	if [ ! -f "$REPLAYS/00000000.rep" ]; then echo "the live run wrote no replay"; failures=$((failures + 1)); continue; fi
	# out of the way of the next recording, and under a name -replay can be given
	mv -- "$REPLAYS/00000000.rep" "$REPLAYS/determinism${seed}_${players}.rep"
	if [ "$CONTROL" -eq 1 ]; then
		# the same length, so nothing after it in the header moves
		perl -0777 -pi -e 's/SD=(\d*)(\d);/"SD=".$1.(($2+1)%10).";"/e' "$REPLAYS/determinism${seed}_${players}.rep"
	fi
	printf 'frame %s, CRC %s, %s structures built after frame 0 (%s); playing back ... ' "$LIVE_FRAME" "$LIVE_CRC" "$LIVE_BUILT" "$RUN_RESULT"

	run_game "${name}_back" -replay "determinism${seed}_${players}"
	BACK_CRC="$RUN_CRC"; BACK_FRAME="$RUN_FRAME"

	printf 'again ... '
	rm -f -- "$REPLAYS/00000000.rep"
	run_game "${name}_again" -randommap "$seed" "$players" $CELLS -autoskirmish "$players" \
		-aidiff "$AIDIFF" -seed "$seed" -observer
	AGAIN_CRC="$RUN_CRC"; AGAIN_FRAME="$RUN_FRAME"

	bad=""
	# An idle match agrees with itself and proves nothing: a divergence in movement, combat or the AI
	# cannot show in a world where nothing happens.
	if [ "$LIVE_BUILT" -eq 0 ]; then
		bad="${bad} IDLE: the AI built nothing after frame 0;"
	fi
	if [ "$BACK_CRC" != "$LIVE_CRC" ] || [ "$BACK_FRAME" != "$LIVE_FRAME" ]; then
		bad="${bad} playback ${BACK_CRC:-none} at frame ${BACK_FRAME:-none};"
	fi
	if [ "$AGAIN_CRC" != "$LIVE_CRC" ] || [ "$AGAIN_FRAME" != "$LIVE_FRAME" ]; then
		bad="${bad} second run ${AGAIN_CRC:-none} at frame ${AGAIN_FRAME:-none};"
	fi
	if [ -z "$bad" ]; then
		echo "the playback and the second run agree at frame $LIVE_FRAME"
		echo "  seed $seed, $players players: HEADLESS CRC $LIVE_CRC at frame $LIVE_FRAME"
		echo "    the fight, per player: $LIVE_STATS"
		echo "    built: ${LIVE_BUILT_NAMES:-nothing}"
	else
		echo "FAILED, live $LIVE_CRC at frame $LIVE_FRAME:$bad"
		failures=$((failures + 1))
	fi
done

echo
if [ -n "$APP" ] && [ "${APP%.app}" != "$APP" ]; then		# a macOS bundle's seal; a Linux package has none
	if codesign --verify --deep --strict "$APP" 2>/dev/null; then
		echo "the bundle's signature still verifies --deep --strict: nothing was written into it"
	else
		echo "FAILED: the bundle's signature no longer verifies: something was written into it"
		SEAL_BROKEN=1
	fi
fi
if [ "$failures" -eq 0 ]; then
	echo "$nseeds of $nseeds matches: each replay played back to the same world, and each seed played the same twice (this machine only)."
else
	echo "$failures of $nseeds matches did not agree with themselves on this machine."
fi
failures=$((failures + ${SEAL_BROKEN:-0}))		# the exit status counts a broken seal as one more failure
if ! verify_install; then
	KEEP=1
	exit 99
fi
exit "$failures"
