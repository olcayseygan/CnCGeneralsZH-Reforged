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
# Writes GeneralsMD/Code/BuildFingerprint.manifest: the tracked files the build fingerprint hashes (N1,
# decision 5), from `git ls-files`, relative to GeneralsMD/Code, '/' separators, sorted bytewise.
#
#   fingerprint-manifest.sh            rewrite the manifest (run after adding, removing or renaming a file)
#   fingerprint-manifest.sh --check    exit 1 if the committed manifest is out of date, 77 without git
#
# Left out, because a vendoring or build run rewrites their bytes and two checkouts of one commit would
# then disagree: Libraries/Source/FFmpeg/dist/ and dist-arm64/ (Windows' FFmpeg, x64 and ARM64, rebuilt by
# Tools/ffmpeg-build.sh).
# The manifest itself is left out too (it would hash its own list).
#
# Refused while the index has unmerged entries (a merge or rebase stopped on a conflict): `git ls-files`
# lists an unmerged path once per stage, so a manifest written then names it up to three times, and a
# check run then compares against a list that is not a commit's.  The list is deduplicated anyway, and
# --check names any line the committed manifest holds more than once.

set -euo pipefail

CODE="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$CODE/BuildFingerprint.manifest"

if ! command -v git >/dev/null 2>&1 || ! git -C "$CODE" rev-parse --git-dir >/dev/null 2>&1; then
	echo "skip: no git here, so the manifest cannot be checked (it is used as committed)"
	exit 77
fi

if [ -n "$(git -C "$CODE" ls-files -u . 2>/dev/null | head -1)" ]; then
	echo "FAIL: the index has unmerged entries (a merge or rebase stopped on a conflict); resolve and add them, then run this again:"
	git -C "$CODE" ls-files -u . | awk '{print $4}' | LC_ALL=C sort -u | head -10
	exit 1
fi

list() {
	git -C "$CODE" ls-files -z . | tr '\0' '\n' \
		| grep -v '^Libraries/Source/FFmpeg/dist/' \
		| grep -v '^Libraries/Source/FFmpeg/dist-arm64/' \
		| grep -v '^BuildFingerprint\.manifest$' \
		| LC_ALL=C sort -u
}

if [ "${1:-}" = "--check" ]; then
	twice="$(LC_ALL=C sort "$MANIFEST" | uniq -d)"
	if [ -n "$twice" ]; then
		echo "FAIL: BuildFingerprint.manifest lists these more than once; run Tools/fingerprint-manifest.sh and commit it:"
		printf '%s\n' "$twice" | head -10
		exit 1
	fi
	if list | cmp -s - "$MANIFEST"; then
		echo "ok: BuildFingerprint.manifest lists exactly the tracked files ($(wc -l < "$MANIFEST" | tr -d ' '))"
		exit 0
	fi
	echo "FAIL: BuildFingerprint.manifest is out of date; run Tools/fingerprint-manifest.sh and commit it:"
	diff <(list) "$MANIFEST" | head -10
	exit 1
fi
list > "$MANIFEST"
echo "wrote $MANIFEST ($(wc -l < "$MANIFEST" | tr -d ' ') files)"
