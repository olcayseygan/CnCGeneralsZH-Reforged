#!/usr/bin/env python3
"""Pack a staged tree of rebuilt art into one of the fork's art archives.

    python artpack.py <staged Art folder> [<staged Data folder>] <Run\\Reforged<Name>.big> [--base <base game folder>]
    python artpack.py selfcheck

The staged Art folder holds W3D\\ and Textures\\ the way Run\\Art does; every file goes into the
archive under Art\\. A staged Data folder holds <Language>\\Art\\W3D\\ (or Textures\\) and goes in
under Data\\: the game opens Data\\<Language>\\Art\\... before Art\\ (GameFileClass::Set_Name), so a
name a language archive holds there (W3DEnglishZH.big's UITRST_SKNP.W3D) has to ship at that path,
and an Art\\ copy alone is refused, since the language copy would shadow it.
The archive's name has to start with "Reforged": that is what makes it beat every
retail archive (Win32BIGFileSystem::init), what Classic Graphics leaves out, what Tools/vendor.ps1
fetches and what the launcher ships. One archive per batch, ReforgedUSA.big, ReforgedChina.big, ReforgedGLA.big.

A model goes in only when it keeps every bone of the retail model it replaces: same name, same
parent, same place, and no new bone that extends a numbered retail run (FIREPOINT03 after
FIREPOINT01-02). The logic reads those bones for fire points and turrets, and Classic still draws
and simulates retail's copy, so the two looks only play the same game while they agree. A model
that fails is left out and named; the rest of the batch still ships. A model under a name retail
does not have goes in as it is: only an INI line can name it, and that is checksummed.

Refused outright: a staged name another Reforged*.big beside the output already holds (the later
archive would win silently) and one a loose file under Run\\Art shadows (the archive would never be
read). The retail archives are read from the output's folder (Patch*.big first, then the rest in
name order) and from the base game folder, ZH_Generals beside it by default.
"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigfile  # noqa: E402

SEP = "\\"
W3D_HIERARCHY, W3D_PIVOTS, PIVOT_SIZE = 0x100, 0x102, 60
EPSILON = 1e-3


def chunks(data, start, end):
    while start + 8 <= end:
        kind, size = struct.unpack_from("<II", data, start)
        size &= 0x7FFFFFFF
        yield kind, start + 8, min(start + 8 + size, end)
        start += 8 + size


def pivots(data):
    """{NAME: (parent NAME or None, translation, rotation)} of the model's hierarchy, {} without one."""
    raw = []
    for kind, a, b in chunks(data, 0, len(data)):
        if kind != W3D_HIERARCHY:
            continue
        for kind2, a2, b2 in chunks(data, a, b):
            if kind2 == W3D_PIVOTS:
                for at in range(a2, b2 - PIVOT_SIZE + 1, PIVOT_SIZE):
                    name = data[at:at + 16].split(b"\0")[0].decode("latin-1").upper()
                    parent, = struct.unpack_from("<i", data, at + 16)
                    raw.append((name, parent, struct.unpack_from("<3f", data, at + 20), struct.unpack_from("<4f", data, at + 44)))
    return {name: (raw[parent][0] if 0 <= parent < len(raw) else None, t, q) for name, parent, t, q in raw}


def close(a, b):
    return all(abs(x - y) <= EPSILON for x, y in zip(a, b))


def bone_problems(new, old):
    """Why the rebuilt model would not play like the retail one, empty when it would."""
    pn, po = pivots(new), pivots(old)
    problems = []
    missing = sorted(set(po) - set(pn))
    if missing:
        problems.append("drops %s" % ", ".join(missing))
    for name in sorted(set(po) & set(pn)):
        (pp, pt, pq), (np_, nt, nq) = po[name], pn[name]
        same_turn = close(pq, nq) or close(pq, [-x for x in nq])
        if pp != np_ or not close(pt, nt) or not same_turn:
            problems.append("moves %s" % name)
    runs = {re.sub(r"\d+$", "", n) for n in po if re.search(r"\d+$", n)}
    extends = sorted(n for n in set(pn) - set(po) if re.search(r"\d+$", n) and re.sub(r"\d+$", "", n) in runs)
    if extends:
        problems.append("extends a retail run with %s" % ", ".join(extends))
    return problems


def index_of(archive):
    try:
        with open(archive, "rb") as f:
            return {path.lower().replace("/", SEP): (archive, offset, size) for path, offset, size in bigfile._read_index(f)}
    except ValueError:
        return {}                                          # a file named .big that is not an archive


def bigs(folder):
    return sorted((n for n in os.listdir(folder) if n.lower().endswith(".big")), key=str.lower) if os.path.isdir(folder) else []


def read_member(where):
    archive, offset, size = where
    with open(archive, "rb") as f:
        f.seek(offset)
        return f.read(size)


def shadowed(members, retail):
    """Staged Art\\W3D or Art\\Textures paths a language folder's copy (Data\\<Language>\\Art\\...) in
    retail shadows, with that copy's path, when the staged set does not hold that path too."""
    lang = {}
    for path in retail:
        p = path.split(SEP)
        if len(p) == 5 and p[0] == "data" and p[2] == "art":
            lang.setdefault(SEP.join(p[2:]), []).append(path)
    return sorted((path, other) for path in members for other in lang.get(path, []) if other not in members)


def pack(staged, output, base):
    run = os.path.dirname(os.path.abspath(output))
    name = os.path.basename(output)
    if not name.lower().startswith("reforged") or name.lower().startswith("reforgednormals"):
        sys.exit("%s: the archive's name has to start with Reforged (and not ReforgedNormals)" % name)

    members, spelled = {}, {}
    for root in staged:
        top = os.path.basename(os.path.normpath(root))
        if top.lower() not in ("art", "data"):
            sys.exit("%s: a staged folder is named Art or Data, as its files go into the archive under that name" % root)
        for folder, _dirs, files in os.walk(root):
            for f in files:
                full = os.path.join(folder, f)
                path = top + SEP + os.path.relpath(full, root).replace(os.sep, SEP)
                members[path.lower()], spelled[path.lower()] = full, path

    # retail as the game resolves it: the patch archives overwrite (last in name order wins), the
    # rest of the root keeps its first claim, the base game fills what is left
    root = [n for n in bigs(run) if not n.lower().startswith("reforged")]
    retail = {}
    order = [n for n in reversed(root) if n.lower().startswith("patch")] + [n for n in root if not n.lower().startswith("patch")]
    for n in order:
        for path, where in index_of(os.path.join(run, n)).items():
            retail.setdefault(path, where)
    base_bigs = bigs(base)
    if not any(n.lower() == "w3d.big" for n in base_bigs):
        sys.exit("no W3D.big in %s: pass the base game folder with --base" % base)
    for n in base_bigs:
        for path, where in index_of(os.path.join(base, n)).items():
            retail.setdefault(path, where)

    clashes = []
    for n in bigs(run):
        if n.lower().startswith("reforged") and n.lower() != name.lower() and not n.lower().startswith("reforgednormals"):
            clashes += ["%s (in %s)" % (p, n) for p in sorted(set(index_of(os.path.join(run, n))) & set(members))]
    clashes += ["%s (loose in Run)" % p for p in sorted(members) if os.path.exists(os.path.join(run, p.replace(SEP, os.sep)))]
    clashes += ["%s (%s is read first: stage it at that path)" % (p, o) for p, o in shadowed(members, retail)]
    if clashes:
        sys.exit("refused, these names are already somewhere that wins or loses against this archive:\n  " + "\n  ".join(clashes))

    refused = {}
    replaced = 0
    for path, full in members.items():
        if not path.endswith(".w3d") or path not in retail:
            continue
        replaced += 1
        with open(full, "rb") as f:
            problems = bone_problems(f.read(), read_member(retail[path]))
        if problems:
            refused[path] = problems

    kept = [(spelled[path], full) for path, full in members.items() if path not in refused]
    bigfile.write_archive(output, kept)
    w3d = sum(p.lower().endswith(".w3d") for p, _ in kept)
    print("%d models (%d replace a retail one, %d new), %d other files" % (
        w3d, replaced - len(refused), w3d - replaced + len(refused), len(kept) - w3d))
    for path in sorted(refused):
        print("left out %s: %s" % (path, "; ".join(refused[path])))


def selfcheck():
    def model(bones):
        records = []
        names = [b[0] for b in bones]
        for name, parent, t in bones:
            records.append(name.encode().ljust(16, b"\0") + struct.pack("<i", names.index(parent) if parent else -1)
                           + struct.pack("<3f", *t) + struct.pack("<3f", 0, 0, 0) + struct.pack("<4f", 0, 0, 0, 1))
        body = b"".join(records)
        inner = struct.pack("<II", W3D_PIVOTS, len(body)) + body
        return struct.pack("<II", W3D_HIERARCHY, len(inner) | 0x80000000) + inner

    retail = model([("ROOT", None, (0, 0, 0)), ("TURRET", "ROOT", (1, 0, 2)), ("FIREPOINT01", "TURRET", (3, 0, 0))])
    assert bone_problems(retail, retail) == []
    assert bone_problems(model([("ROOT", None, (0, 0, 0)), ("TURRET", "ROOT", (1, 0, 2)), ("FIREPOINT01", "TURRET", (3, 0, 0)),
                                ("WHEEL_L01", "ROOT", (0, 1, 0))]), retail) == []
    assert bone_problems(model([("ROOT", None, (0, 0, 0)), ("TURRET", "ROOT", (1, 0, 2))]), retail) == ["drops FIREPOINT01"]
    assert bone_problems(model([("ROOT", None, (0, 0, 0)), ("TURRET", "ROOT", (1, 0, 2.5)), ("FIREPOINT01", "TURRET", (3, 0, 0))]),
                         retail) == ["moves TURRET"]
    assert bone_problems(model([("ROOT", None, (0, 0, 0)), ("TURRET", "ROOT", (1, 0, 2)), ("FIREPOINT01", "ROOT", (3, 0, 0))]),
                         retail) == ["moves FIREPOINT01"]
    assert bone_problems(model([("ROOT", None, (0, 0, 0)), ("TURRET", "ROOT", (1, 0, 2)), ("FIREPOINT01", "TURRET", (3, 0, 0)),
                                ("FIREPOINT02", "TURRET", (3, 1, 0))]), retail) == ["extends a retail run with FIREPOINT02"]
    assert pivots(b"") == {}

    english = "data\\english\\art\\w3d\\uitrst_sknp.w3d"
    retail_paths = {english: None, "art\\w3d\\uirtoxf_skn.w3d": None}
    assert shadowed({"art\\w3d\\uitrst_sknp.w3d": ""}, retail_paths) == [("art\\w3d\\uitrst_sknp.w3d", english)]
    assert shadowed({"art\\w3d\\uitrst_sknp.w3d": "", english: ""}, retail_paths) == []
    assert shadowed({english: "", "art\\w3d\\uirtoxf_skn.w3d": ""}, retail_paths) == []
    print("artpack selfcheck OK")


def main(argv):
    if argv[1:] == ["selfcheck"]:
        selfcheck()
        return 0
    args = argv[1:]
    base = None
    if "--base" in args:
        i = args.index("--base")
        base = args[i + 1]
        del args[i:i + 2]
    if len(args) not in (2, 3):
        print(__doc__.strip())
        return 2
    staged, output = args[:-1], args[-1]
    pack(staged, output, base or os.path.join(os.path.dirname(os.path.abspath(output)), "ZH_Generals"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
