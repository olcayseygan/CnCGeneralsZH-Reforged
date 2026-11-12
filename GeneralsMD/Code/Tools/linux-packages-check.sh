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
# linux-packages-check.sh: proves the release files linux-packages.sh made, each on a clean container of its
# distribution.  Per distribution, two containers:
#
#   minimal   the package installed with only what it Depends/Requires (no recommended or weak dependencies),
#             then cut off the network: E1 (Tools/replay-check.sh --installed) through /usr/bin/zero-hour-reforged,
#             its launcher and its own overlay discovery, at the pins: seed 0 at 40 s of game time, seeds 0
#             and 1 at 400 s.  The game data is mounted read-only (rule 9 as ever: the runs root at a farm of it);
#   drawn     the package installed as a player gets it (recommended dependencies on; on Arch, the Vulkan and
#             X11 optdepends), Xvfb, and Mesa's software Vulkan (lavapipe): one windowed skirmish frame, written
#             with -screenshot, which must be a picture (many colours), not a blank.
#
#   deb       debian:12 and ubuntu:24.04            rpm   fedora:latest and opensuse/tumbleweed
#   arch      archlinux:latest                      appimage   debian:12, run with APPIMAGE_EXTRACT_AND_RUN
#
# No run needs a person: the Zero Hour folder is given (Registry.ini for E1, -root for the frame), every run is
# unattended (ZH_UNATTENDED=1, which also keeps the launcher from fetching the art), and each has a time limit.
# The containers are removed as each distribution ends, and the images it pulled with --remove-images.
#
# Usage: linux-packages-check.sh --packages <folder> --data <folder> [--formats "deb rpm arch appimage"]
#          [--no-draw] [--remove-images]
#   --packages   linux-packages.sh's --out
#   --data       a folder holding zerohour/ (the base game in zerohour/ZH_Generals), as replay-check.sh takes it
# Exit status: the number of distributions that failed; 77 without docker or data.

set -u
PKGS="" DATA="" FORMATS="deb rpm arch appimage" DRAW=1 REMOVE_IMAGES=0
while [ $# -gt 0 ]; do
	case "$1" in
		--packages) PKGS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--formats) FORMATS="$2"; shift 2;;
		--no-draw) DRAW=0; shift;;
		--remove-images) REMOVE_IMAGES=1; shift;;
		*) echo "linux-packages-check: unknown argument $1" >&2; exit 2;;
	esac
done
command -v docker > /dev/null || { echo "skip: no docker"; exit 77; }
[ -n "$DATA" ] && [ -d "$DATA/zerohour" ] || { echo "skip: no game data (--data)"; exit 77; }
[ -n "$PKGS" ] && [ -d "$PKGS" ] || { echo "linux-packages-check: --packages must name linux-packages.sh's --out" >&2; exit 2; }
CODE="$(cd "$(dirname "$0")/.." && pwd)"
PKGS="$(cd "$PKGS" && pwd -P)"; DATA="$(cd "$DATA" && pwd -P)"
file_of() { ls "$PKGS"/$1 2>/dev/null | head -1; }
DEB="$(file_of 'zero-hour-reforged_*_amd64.deb')"; RPM="$(file_of 'zero-hour-reforged-*.x86_64.rpm')"
ARCHPKG="$(file_of 'zero-hour-reforged-*-x86_64.pkg.tar.zst')"; APPIMAGE="$(file_of 'zero-hour-reforged-*-x86_64.AppImage')"
# E1 runs 40 s and 400 s of game time, in logic frames at the rate the engine's own header names
LOGIC_FPS="$(sed -n 's/^[[:space:]]*LOGICFRAMES_PER_SECOND[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$CODE/GameEngine/Include/Common/GameCommon.h")"
[ -n "$LOGIC_FPS" ] || { echo "linux-packages-check: no LOGICFRAMES_PER_SECOND in GameCommon.h" >&2; exit 2; }
SHORT=$((40 * LOGIC_FPS)) LONG=$((400 * LOGIC_FPS))
# E1's pins, measured on Windows x64 at a 30 Hz logic tick (frames 1200 and 12000): re-measure whenever
# LOGICFRAMES_PER_SECOND changes, or every package fails here
PIN_SHORT=0x87792035 PIN_0=0xAF2D1FDD PIN_1=0x084F009C
failed=0 pulled=""
say() { echo "linux-packages-check: $*"; }
crc() { printf '%s\n' "$1" | sed -n "s/.*seed $2, 2 players: HEADLESS CRC \(0x[0-9A-Fa-f]*\) at frame $3.*/\1/p" | head -1; }	# <E1 output> <seed> <frame>

# the frame run's farm, the way replay-check.sh makes its own (links into the read-only data)
# a work folder that could not be made is the end of the run: going on with WORKROOT empty would write under /
WORKROOT="$(mktemp -d "${TMPDIR:-/tmp}/zhr-pkgcheck.XXXXXX")" || WORKROOT=""
if [ -z "$WORKROOT" ] || [ ! -d "$WORKROOT" ]; then echo "linux-packages-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2; exit 2; fi
trap 'docker rm -f $(docker ps -aq --filter "label=zhr-pkgcheck=$$") > /dev/null 2>&1; rm -rf -- "$WORKROOT"' EXIT
FARM="$WORKROOT/farm"
( cd "$DATA/zerohour" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$FARM/$d"; done
( cd "$DATA/zerohour" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$DATA/zerohour/${f#./}" "$FARM/$f"; done

start_container() {	# start_container <name> <image>: a clean container, the packages, data and work folder mounted
	docker image inspect "$2" > /dev/null 2>&1 || pulled="$pulled $2"
	docker run -d --name "$1" --label "zhr-pkgcheck=$$" -v "$PKGS:$PKGS:ro" -v "$DATA:$DATA:ro" -v "$WORKROOT:$WORKROOT" \
		"$2" sleep 7200 > /dev/null
}
as_root() { docker exec "$1" sh -c "$2"; }

# install_cmd <distribution> <minimal|drawn>: the package manager's own command, as a player would type it
install_cmd() {
	local weak
	case "$1:$2" in
		debian*:minimal|ubuntu*:minimal) echo "apt-get -q update && DEBIAN_FRONTEND=noninteractive apt-get -q -y install --no-install-recommends '$DEB'";;
		debian*:drawn|ubuntu*:drawn) echo "apt-get -q update && DEBIAN_FRONTEND=noninteractive apt-get -q -y install '$DEB' xvfb";;
		fedora*:minimal) echo "dnf -q -y --setopt=install_weak_deps=False install '$RPM'";;
		fedora*:drawn) echo "dnf -q -y install '$RPM' xorg-x11-server-Xvfb";;
		opensuse*:minimal) echo "zypper -q -n install --allow-unsigned-rpm --no-recommends '$RPM'";;
		opensuse*:drawn) echo "zypper -q -n install --allow-unsigned-rpm '$RPM' xvfb-run xorg-x11-server-Xvfb";;
		arch*:minimal) echo "pacman -Sy --noconfirm && pacman -U --noconfirm '$ARCHPKG'";;
		arch*:drawn) echo "pacman -Sy --noconfirm && pacman -U --noconfirm '$ARCHPKG' && pacman -S --noconfirm vulkan-icd-loader vulkan-swrast libx11 libxkbcommon libxcursor libxrandr libxi alsa-lib dbus xorg-server-xvfb";;
		appimage:minimal) echo "true";;		# nothing: the AppImage brings the engine, the image glibc and fontconfig
		appimage:drawn) echo "apt-get -q update && DEBIAN_FRONTEND=noninteractive apt-get -q -y install libfontconfig1 libvulkan1 mesa-vulkan-drivers libx11-6 libxext6 libxcursor1 libxi6 libxrandr2 libxkbcommon0 xvfb";;
	esac
}

check_distribution() {	# check_distribution <label> <image> <format>
	local label="$1" image="$2" format="$3" name="zhr-pkgcheck-$$-$1" T="$WORKROOT/$1" bad=0
	mkdir -p "$T/tmp"
	say "== $label ($image, $format)"

	# minimal: E1 at the pins
	start_container "$name" "$image" || { say "$label: cannot start $image"; return 1; }
	case "$format" in appimage) as_root "$name" "apt-get -q update > /dev/null && DEBIAN_FRONTEND=noninteractive apt-get -q -y install libfontconfig1 > /dev/null";; esac
	if ! as_root "$name" "$(install_cmd "$label" minimal)" > "$T/install-minimal.log" 2>&1; then
		say "$label: the minimal install failed:"; tail -5 "$T/install-minimal.log"; docker rm -f "$name" > /dev/null; return 1
	fi
	docker network disconnect bridge "$name" > /dev/null 2>&1		# E1 runs offline
	cat > "$T/runner" <<RUNNER
#!/bin/sh
exec docker exec -i -u $(id -u):$(id -g) -e HOME -e ZH_USER_DATA_DIR -e ZH_UNATTENDED -e APPIMAGE_EXTRACT_AND_RUN=1 -e TMPDIR=$T/tmp -w "\$PWD" $name timeout 900 "\$@"
RUNNER
	chmod +x "$T/runner"
	local target=( --installed /usr/bin/zero-hour-reforged ) e1a e1b
	[ "$format" = appimage ] && target=( --appimage "$APPIMAGE" )
	e1a="$(TMPDIR="$T/tmp" REPLAY_CHECK_RUNNER="$T/runner" bash "$CODE/Tools/replay-check.sh" "${target[@]}" --data "$DATA" --seeds 0 --maxframes $SHORT 2>&1)"
	e1b="$(TMPDIR="$T/tmp" REPLAY_CHECK_RUNNER="$T/runner" bash "$CODE/Tools/replay-check.sh" "${target[@]}" --data "$DATA" --seeds "0 1" --maxframes $LONG 2>&1)"
	printf '%s\n%s\n' "$e1a" "$e1b" | grep -E "HEADLESS CRC|agree|DIVERGED|no result|INSTALL" | sed 's/^/  /'
	[ "$(crc "$e1a" 0 $SHORT)" = $PIN_SHORT ] && printf '%s' "$e1a" | grep -q "the playback and the second run agree" \
		|| { say "$label: E1 seed 0 at $SHORT is not $PIN_SHORT (or did not agree)"; bad=1; }
	[ "$(crc "$e1b" 0 $LONG)" = $PIN_0 ] && [ "$(crc "$e1b" 1 $LONG)" = $PIN_1 ] \
		&& [ "$(printf '%s' "$e1b" | grep -c "the playback and the second run agree")" -eq 2 ] \
		|| { say "$label: E1 seeds 0 and 1 at $LONG are not $PIN_0 and $PIN_1 (or did not agree)"; bad=1; }
	[ $bad -eq 0 ] && say "$label: E1 at the three pins, through the installed launcher (minimal install, offline)"
	docker rm -f "$name" > /dev/null

	# drawn: one software-rendered frame
	if [ $DRAW -eq 1 ]; then
		start_container "$name" "$image" || { say "$label: cannot start $image"; return 1; }
		if ! as_root "$name" "$(install_cmd "$label" drawn)" > "$T/install-drawn.log" 2>&1; then
			say "$label: the default install failed:"; tail -5 "$T/install-drawn.log"; docker rm -f "$name" > /dev/null; return 1
		fi
		local run=/usr/bin/zero-hour-reforged shot
		[ "$format" = appimage ] && run="$APPIMAGE"
		mkdir -p "$T/draw-user" "$T/draw-home"
		docker exec -u "$(id -u):$(id -g)" -e HOME="$T/draw-home" -e ZH_USER_DATA_DIR="$T/draw-user/" -e ZH_UNATTENDED=1 \
			-e APPIMAGE_EXTRACT_AND_RUN=1 -e TMPDIR="$T/tmp" -e LIBGL_ALWAYS_SOFTWARE=1 -w "$T" "$name" sh -c \
			"Xvfb :77 -screen 0 1024x768x24 -nolisten tcp > '$T/xvfb.log' 2>&1 & sleep 2; DISPLAY=:77 timeout 900 '$run' -root '$FARM' \
				-quickstart -noshellmap -multiInstance -noaudio -xres 800 -yres 600 -randommap 0 2 -autoskirmish 2 -aidiff brutal \
				-seed 0 -observer -maxframes $((3 * LOGIC_FPS)) -screenshot $((8 * LOGIC_FPS / 3)) > '$T/draw.out' 2> '$T/draw.err'; echo \$? > '$T/draw.status'"
		shot="$(find "$T/draw-user" -name 'sshot*.bmp' | head -1)"
		local colours=0
		[ -n "$shot" ] && colours="$(od -An -v -tu1 -j 54 "$shot" | tr -s ' ' '\n' | sort -u | wc -l)"
		if [ -n "$shot" ] && [ "$colours" -gt 64 ]; then
			say "$label: a software-rendered frame ($(wc -c < "$shot") bytes, $colours byte values; exit $(cat "$T/draw.status"))"
		else
			say "$label: no picture from the software-rendered run (screenshot '${shot:-none}', exit $(cat "$T/draw.status" 2>/dev/null))"
			tail -5 "$T/draw.err"; bad=1
		fi
		docker rm -f "$name" > /dev/null
	fi
	[ $bad -eq 0 ]
}

for format in $FORMATS; do
	case "$format" in
		deb) [ -n "$DEB" ] || { say "no .deb in $PKGS"; failed=$((failed + 1)); continue; }
			check_distribution debian-12 debian:12 deb || failed=$((failed + 1))
			check_distribution ubuntu-24.04 ubuntu:24.04 deb || failed=$((failed + 1));;
		rpm) [ -n "$RPM" ] || { say "no .rpm in $PKGS"; failed=$((failed + 1)); continue; }
			check_distribution fedora fedora:latest rpm || failed=$((failed + 1))
			check_distribution opensuse-tumbleweed opensuse/tumbleweed rpm || failed=$((failed + 1));;
		arch) [ -n "$ARCHPKG" ] || { say "no Arch package in $PKGS"; failed=$((failed + 1)); continue; }
			check_distribution archlinux archlinux:latest arch || failed=$((failed + 1));;
		appimage) [ -n "$APPIMAGE" ] || { say "no AppImage in $PKGS"; failed=$((failed + 1)); continue; }
			check_distribution appimage debian:12 appimage || failed=$((failed + 1));;
		*) say "unknown format $format"; failed=$((failed + 1));;
	esac
done
if [ $REMOVE_IMAGES -eq 1 ] && [ -n "$pulled" ]; then
	docker rmi $pulled > /dev/null 2>&1 && say "removed the images it pulled:$pulled"
fi
say "$failed distribution(s) failed"
exit $failed
