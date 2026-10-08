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
# Builds one source file twice - arm64 and x86_64 - runs both, and compares the two outputs row by
# row.  Rosetta 2 runs the x86_64 binary, so one Mac does the whole job in about a second.
#
# =============================================================================================
# WHAT THIS PROVES, AND WHAT IT DOES NOT
#
# It proves: the same C++ source, compiled by ONE compiler for TWO architectures, computes the
# same values - or it names the rows where it does not.  That is the architecture half of the
# determinism question, and it is the half where x86 and arm64 are genuinely free to differ:
# rounding of float-to-integer conversions, fused multiply-add contraction, and the width of
# `long`.
#
# It does NOT prove anything whatsoever about Windows.  Specifically, it says nothing about:
#
#   - MSVC's code generation.  Both binaries here are clang's.  A difference between clang/x86_64
#     and MSVC/x86_64 is invisible to this harness by construction.
#   - MSVC's optimiser, which reassociates and contracts under its own rules.  /fp:precise is not
#     -ffp-contract=off and the two have never been compared.
#   - MSVC's `long`, which is 32 bits where every target here has 64.  A row that agrees between
#     these two binaries can still disagree with Windows for that reason alone, and the `convert`
#     section contains real examples of exactly that.
#   - The x87 unit, the MXCSR state a Windows process actually runs in, or anything setFPMode()
#     is there to pin.
#
# It is also blind to anything the two targets share, and they share more than the architecture
# name suggests. BOTH define __APPLE__ and BOTH are little-endian. So a bug keyed on either -
# `#if defined(__APPLE__)` taking a path meant for PowerPC, or code that assumes a byte order -
# takes the SAME branch in both builds, they agree, and this harness reports green. B3 hit exactly
# that: gimex.h's ggetm/gputm used a native load for big-endian fields under `#if defined(__APPLE__)`,
# a 2003 shorthand for PowerPC, and it silently byte-swapped every RefPack header field on Apple
# Silicon. Adding a probe for it here would not have caught it, for the reason above.
#
# That is the same shape as the mistake this harness exists because of: a reference that shares the
# property under test proves nothing about it. A differential harness can only see the axis it
# varies. Bugs keyed on the OS or on endianness need a test that asserts the intended value -
# a round-trip in a selfcheck - not a comparison against a twin that shares the assumption.
#
# It is also blind to any probe whose input the compiler can see.  clang folds arithmetic on a
# constant at compile time, on the build machine, and prints the same answer into both binaries.
# B17 hit this twice: an inf*0 probe read 0x7FC00000 on both targets until its input became
# volatile, and then x86_64 read 0xFFC00000.  A `static` array that nothing writes counts as a
# constant too.  Feed a probe from a volatile, or from a global the compiler cannot prove unchanged.
#
# So: a green run here is NOT a green run against Windows.  E1's standing item in
# docs/porting/windows-impact.md is reduced by this task, not discharged by it.  If you find yourself about to
# write "determinism verified" because this passed, read this paragraph again.
# =============================================================================================
#
# It reports differences; it does not decide which architecture is right.  That question is
# "what does the Windows build do", which is a different question and one this machine cannot
# answer.  Differences that have been looked at are recorded in known_differences.txt with a note,
# and anything not in that file fails the run.
#
# Skips rather than fails where the cross-toolchain or Rosetta is missing: this is a capability of
# a developer's machine, not a requirement of the build.
#
#   run_arch_diff.sh <source-dir> <work-dir>

set -uo pipefail

SKIP=77

here="$(cd "$(dirname "$0")" && pwd)"
code_root="${1:-$(cd "$here/../.." && pwd)}"
work="${2:-${TMPDIR:-/tmp}/zhr-arch-diff}"
known="$here/known_differences.txt"

say()  { echo "[arch-diff] $1"; }
skip() { echo "[arch-diff] SKIP: $1"; exit $SKIP; }

mkdir -p "$work"

# --- can this machine do the job at all? -----------------------------------------------------
command -v clang++ >/dev/null 2>&1 || skip "no clang++ on PATH"

probe="$here/arch_probe.cpp"
[ -e "$probe" ] || { echo "[arch-diff] ERROR: $probe is missing"; exit 1; }

# Each canary refuses to compile for any other architecture: off Darwin an older clang (the Steam
# Runtime's 11) accepts -arch, ignores it and builds for the host, and then both sides are arm64
# and agree on everything, the known differences included.
printf '#ifndef __x86_64__\n#error not x86_64\n#endif\nint main(void){return 0;}\n' > "$work/canary_x86.c"
printf '#ifndef __aarch64__\n#error not arm64\n#endif\nint main(void){return 0;}\n' > "$work/canary_arm.c"
clang -arch x86_64 "$work/canary_x86.c" -o "$work/canary_x86" 2>/dev/null \
  || skip "this clang cannot target x86_64 (no cross toolchain)"
clang -arch arm64  "$work/canary_arm.c" -o "$work/canary_arm" 2>/dev/null \
  || skip "this clang cannot target arm64"
"$work/canary_x86" >/dev/null 2>&1 \
  || skip "x86_64 binaries do not run here (Rosetta 2 is not installed)"
"$work/canary_arm" >/dev/null 2>&1 \
  || skip "arm64 binaries do not run here"

# --- what the probe can reach today ----------------------------------------------------------
# dettrig.cpp still needs two spellings the Microsoft CRT provides and clang does not.  Both are
# B5's to remove; until then they are supplied here so that this harness covers DetTrig now rather
# than after that merge.  When B5 lands, delete the two -D flags and nothing else changes.
WW="$code_root/Libraries/Source/WWVegas"
shims=(-D__cdecl= "-D__int64=long long" -include cstddef)
incs=(-I"$WW/WWMath" -I"$WW/WWLib" -I"$WW/Wwutil" -I"$WW" -I"$code_root/Libraries/Include")
# d3dxportable.h and the sweep it shares with Tests/d3dx_oracle.  Last, so that nothing in WW3D2
# can shadow a header the probe already reaches.
incs+=(-I"$WW/WW3D2" -I"$code_root/Tests")

dettrig_src="$WW/WWMath/dettrig.cpp"
dettrig_flags=()
if [ -e "$dettrig_src" ] \
   && clang++ -arch arm64 -c -O2 -std=c++17 "${shims[@]}" "${incs[@]}" \
        "$dettrig_src" -o "$work/dettrig_probe.o" 2>/dev/null; then
  dettrig_flags=(-DPROBE_WITH_DETTRIG "$dettrig_src")
else
  say "note: DetTrig is not compilable here yet, so that section is not covered"
fi

build_and_run() { # arch -> writes $work/out.<arch>
  local arch="$1"
  clang++ -arch "$arch" -O2 -std=c++17 -ffp-contract=off "${shims[@]}" "${incs[@]}" \
    "${dettrig_flags[@]}" "$probe" -o "$work/probe.$arch" 2>"$work/build.$arch.log" || {
      echo "[arch-diff] ERROR: building the probe for $arch failed:"
      sed 's/^/    /' "$work/build.$arch.log" | head -20
      return 1
    }
  "$work/probe.$arch" > "$work/out.$arch" || {
    echo "[arch-diff] ERROR: the $arch probe did not run"
    return 1
  }
}

build_and_run arm64  || exit 1
build_and_run x86_64 || exit 1

say "arm64:  $(grep -c . "$work/out.arm64") rows"
say "x86_64: $(grep -c . "$work/out.x86_64") rows"

python3 - "$work/out.arm64" "$work/out.x86_64" "$known" <<'PY'
import sys

def load(path):
    rows = {}
    for line in open(path):
        parts = line.rstrip("\n").split("\t")
        if len(parts) == 3 and parts[0] != "meta":
            rows[(parts[0], parts[1])] = parts[2]
    return rows

arm, x86 = load(sys.argv[1]), load(sys.argv[2])

known = {}
try:
    for line in open(sys.argv[3]):
        line = line.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        p = line.split("\t")
        if len(p) >= 4:
            known[(p[0], p[1])] = (p[2], p[3], p[4] if len(p) > 4 else "")
except FileNotFoundError:
    pass

only_arm = sorted(set(arm) - set(x86))
only_x86 = sorted(set(x86) - set(arm))
differ   = sorted(k for k in set(arm) & set(x86) if arm[k] != x86[k])
same     = len(set(arm) & set(x86)) - len(differ)

new_diffs, recorded = [], []
for k in differ:
    if k in known and known[k][0] == arm[k] and known[k][1] == x86[k]:
        recorded.append(k)
    else:
        new_diffs.append(k)

# A recorded difference that has stopped differing is a stale row, and a stale ledger is worse
# than none - it makes the next reader trust a line that is no longer true.
stale = [k for k in known if k not in differ and k in arm and k in x86]

print(f"[arch-diff] {same} rows identical, {len(differ)} differ "
      f"({len(recorded)} recorded, {len(new_diffs)} new)")

if recorded:
    print("[arch-diff] recorded differences (which one matches Windows is a separate question):")
    for k in recorded:
        print(f"    {k[0]:9} {k[1]:24} arm64={arm[k]:>22} x86_64={x86[k]:>22}  {known[k][2]}")

fail = False
if new_diffs:
    fail = True
    print("[arch-diff] FAIL: differences that are not recorded in known_differences.txt:")
    for k in new_diffs:
        print(f"    {k[0]:9} {k[1]:24} arm64={arm[k]:>22} x86_64={x86[k]:>22}")
    print("[arch-diff] If one of these is expected, add it to known_differences.txt with a note")
    print("[arch-diff] saying why. Do not add one to silence it - the note is the point.")

if stale:
    fail = True
    print("[arch-diff] FAIL: known_differences.txt lists rows that no longer differ. Remove them:")
    for k in stale:
        print(f"    {k[0]}\t{k[1]}")

if only_arm or only_x86:
    fail = True
    print("[arch-diff] FAIL: the two builds did not emit the same set of rows.")
    for k in only_arm[:10]: print(f"    arm64 only:  {k[0]}\t{k[1]}")
    for k in only_x86[:10]: print(f"    x86_64 only: {k[0]}\t{k[1]}")

sys.exit(1 if fail else 0)
PY
status=$?
[ $status -eq 0 ] && say "ok - and read the header of this file before calling it determinism"
exit $status
