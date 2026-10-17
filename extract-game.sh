#!/usr/bin/env bash
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
# Lays out Zero Hour and Generals from an install medium or an existing install, without installing or Wine.
#
#   ./extract-game.sh <source> [target]
#
#   source  an .iso, a mounted disc, or a folder holding an install (a copy of a Windows one)
#   target  the Zero Hour folder to fill; default ~/Games/Command & Conquer Generals Zero Hour, after asking
#
# Zero Hour's files go into target, Generals' into target/ZH_Generals, where the game looks (README.md,
# "Where it looks for Zero Hour"). Run it once per disc when the games are on separate media.
# InstallShield media (data1.hdr + data1.cab) need unshield; an .iso needs 7z or bsdtar.

set -euo pipefail

DEFAULT_TARGET="$HOME/Games/Command & Conquer Generals Zero Hour"

say() { echo "[extract] $*"; }
fail() { echo "[extract] ERROR: $*" >&2; exit 1; }
confirm() {
  local answer
  read -r -p "[extract] $1 [y/N] " answer || true
  [[ $answer == [yY] || $answer == [yY][eE][sS] ]]
}

usage() { echo "usage: $0 <iso|folder> [target]   (default target: $DEFAULT_TARGET)" >&2; exit 2; }
[ $# -ge 1 ] && [ $# -le 2 ] || usage
source=$1
[ -e "$source" ] || fail "\"$source\" does not exist."

if [ $# -eq 2 ]; then
  target=$2
else
  target=$DEFAULT_TARGET
  confirm "Extract to \"$target\"?" || fail "cancelled; name a target as the second argument."
fi
mkdir -p "$target"
target=$(cd "$target" && pwd)

# Scratch space beside the target: a DVD's worth of files would not fit a RAM-backed /tmp.
work=$(mktemp -d "$(dirname "$target")/.extract-game.XXXXXX")
trap 'rm -rf "$work"' EXIT

# --- the medium's files ---
if [ -d "$source" ]; then
  tree=$(cd "$source" && pwd)
else
  say "unpacking $source"
  tree="$work/iso"
  mkdir -p "$tree"
  if command -v 7z >/dev/null 2>&1; then
    7z x -y -bd -o"$tree" "$source" >/dev/null || fail "7z could not unpack \"$source\"."
  elif command -v bsdtar >/dev/null 2>&1; then
    bsdtar -xf "$source" -C "$tree" || fail "bsdtar could not unpack \"$source\"."
  else
    fail "unpacking an image needs 7z or bsdtar: sudo apt-get install 7zip (or p7zip-full), or mount it and pass the folder."
  fi
fi

# InstallShield keeps the files in cabinets, one folder per component group; merged, the groups give back
# the installed tree.
mapfile -t headers < <(find "$tree" -type f -iname 'data1.hdr')
if [ ${#headers[@]} -gt 0 ]; then
  command -v unshield >/dev/null 2>&1 || fail "this is an InstallShield medium; install unshield (sudo apt-get install unshield)."
  merged="$work/merged"
  mkdir -p "$merged"
  for header in "${headers[@]}"; do
    say "unpacking the InstallShield cabinets beside $header"
    groups="$work/groups"
    rm -rf "$groups"
    unshield -d "$groups" x "$header" >/dev/null || fail "unshield could not unpack \"$header\"."
    for group in "$groups"/*/; do
      cp -a "$group." "$merged/"
    done
  done
  tree=$merged
fi

# --- find each game by the archive it is known by ---
find_root() {
  local hit
  hit=$(find "$tree" -type f -iname "$1" -print -quit)
  [ -n "$hit" ] && dirname "$hit"
}
zero_hour=$(find_root 'INIZH.big' || true)
generals=$(find_root 'Textures.big' || true)
# an install whose Generals sits beside it, where the game itself would look
if [ -z "$generals" ] && [ -n "$zero_hour" ] && [ -d "$source" ]; then
  for beside in "$zero_hour/../Command & Conquer Generals" "$zero_hour/../Command & Conquer(tm) Generals"; do
    if [ -d "$beside" ] && [ -n "$(find "$beside" -maxdepth 1 -type f -iname 'Textures.big' -print -quit)" ]; then
      generals=$(cd "$beside" && pwd)
      break
    fi
  done
fi
# both in one folder: the game reads Generals from the Zero Hour folder itself, nothing to split
[ "$generals" != "$zero_hour" ] || generals=
[ -n "$zero_hour" ] || [ -n "$generals" ] \
  || fail "no INIZH.big (Zero Hour) or Textures.big (Generals) under \"$source\". Is it the install medium?"

copy_into() {
  local from=$1 to=$2
  mkdir -p "$to"
  if [ -n "$(ls -A "$to" 2>/dev/null | grep -v -x ZH_Generals || true)" ]; then
    confirm "\"$to\" already has files. Overwrite the ones the medium also has?" || fail "cancelled."
  fi
  say "copying $(basename "$from") -> $to"
  # the Zero Hour folder's own copy of Generals stays out of the Zero Hour copy
  find "$from" -mindepth 1 -maxdepth 1 ! -iname ZH_Generals -exec cp -a {} "$to/" \;
}

[ -z "$zero_hour" ] || copy_into "$zero_hour" "$target"
[ -z "$generals" ] || copy_into "$generals" "$target/ZH_Generals"

# --- what the game will find ---
has() { [ -n "$(find "$1" -maxdepth 1 -type f -iname "$2" -print -quit 2>/dev/null)" ]; }
missing=
has "$target" 'INIZH.big' || missing="$missing Zero Hour (INIZH.big)"
has "$target" 'Textures.big' || has "$target/ZH_Generals" 'Textures.big' || missing="$missing${missing:+,} Generals (Textures.big)"
if [ -n "$missing" ]; then
  say "done, but \"$target\" still lacks$missing: run this again with that game's medium and the same target."
  exit 0
fi
say "done: Zero Hour in \"$target\", Generals in ZH_Generals inside it."
say "a disc carries version 1.00; the engine is Zero Hour 1.04's, so a 1.04 install's files are the safer source."
if [ "$target" = "$DEFAULT_TARGET" ]; then
  say "the game finds this folder by itself."
else
  say "start the game with -root \"$target\", or choose this folder when it asks."
fi
