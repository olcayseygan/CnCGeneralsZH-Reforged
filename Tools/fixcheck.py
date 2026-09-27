"""Count the floating point left in GameLogic, and fail if it grew.

The fixed-point conversion (Q47.16) takes GameLogic off Real one phase at a time.  This is the
ratchet: it counts float-ness tokens in GameEngine/Source/GameLogic and GameEngine/Include/GameLogic,
comments and string literals stripped, and exits 1 if the total is above the number in
fixcheck.limit beside it.  A phase that removes floats lowers the limit with --record.

    python fixcheck.py            # per-token counts, total against the limit
    python fixcheck.py --record   # write today's total as the new limit
    python fixcheck.py --files    # also the twenty files with the most tokens
"""

import os
import re
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.join(HERE, "..", "GeneralsMD", "Code", "GameEngine")
ROOTS = [os.path.join(ENGINE, "Source", "GameLogic"), os.path.join(ENGINE, "Include", "GameLogic")]
LIMIT = os.path.join(HERE, "fixcheck.limit")

# comments and string/char literals in one pass, so a // inside a string is not a comment
STRIP = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)

TOKENS = {
    "Real": r"\bReal\b",
    "float": r"\bfloat\b",
    "double": r"\bdouble\b",
    "sqrt": r"\b(?:sqrt|sqrtf|Sqrt)\b",
    "Vector3": r"\bVector3\b",
    "Matrix3D": r"\bMatrix3D\b",
    "Coord3D": r"\bCoord3D\b",
    # 1.0f  0.5  .25f  1e-6  3f ; not 0x1F, not an identifier's digits, not a.b.c
    "literal": r"(?<![\w.])(?:\d+\.\d*(?:[eE][+-]?\d+)?[fF]?|\.\d+(?:[eE][+-]?\d+)?[fF]?"
               r"|\d+[eE][+-]?\d+[fF]?|\d+[fF])(?![\w.])",
}
PATTERNS = {k: re.compile(v) for k, v in TOKENS.items()}


def strip(text):
    return STRIP.sub(lambda m: "\n" * m.group(0).count("\n") if m.group(0)[0] == "/" else '""', text)


def count():
    total, per_file = Counter(), Counter()
    for root in ROOTS:
        for dirpath, _, names in os.walk(root):
            for name in names:
                if not name.lower().endswith((".cpp", ".h", ".inl")):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, encoding="latin-1") as f:
                    text = strip(f.read())
                for key, pat in PATTERNS.items():
                    n = len(pat.findall(text))
                    total[key] += n
                    per_file[os.path.relpath(path, ENGINE)] += n
    return total, per_file


def main(argv):
    total, per_file = count()
    n = sum(total.values())
    for key in TOKENS:
        print("%-9s %7d" % (key, total[key]))
    print("%-9s %7d" % ("total", n))
    if "--files" in argv:
        for path, c in per_file.most_common(20):
            print("%7d  %s" % (c, path))
    if "--record" in argv:
        with open(LIMIT, "w") as f:
            f.write("%d\n" % n)
        print("limit recorded: %d" % n)
        return 0
    with open(LIMIT) as f:
        limit = int(f.read().split()[0])
    if n > limit:
        print("FAIL: %d float tokens, limit %d (+%d)" % (n, limit, n - limit))
        return 1
    print("ok: limit %d%s" % (limit, "; %d under it, lower it with --record" % (limit - n) if n < limit else ""))
    return 0


if __name__ == "__main__":
    assert strip('a // 1.0f\n"2.5" 3.0f') == 'a \n"" 3.0f'
    assert PATTERNS["literal"].findall("x=1.0f+.5-0x1F+v2+1e-6+a.b") == ["1.0f", ".5", "1e-6"]
    sys.exit(main(sys.argv[1:]))
