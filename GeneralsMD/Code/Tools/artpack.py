#!/usr/bin/env python3
"""Pack a staged tree of rebuilt art into one of the fork's art archives.

    python artpack.py <staged Art folder> [<staged Data folder>] <Run\\Reforged<Name>.big> [--base <base game folder>]
                      [--override-base <folder, list file or comma list>]
    python artpack.py selfcheck

The staged Art folder holds W3D\\ and Textures\\ the way Run\\Art does; every file goes into the
archive under Art\\. A staged Data folder holds <Language>\\Art\\W3D\\ (or Textures\\) and goes in
under Data\\: the game opens Data\\<Language>\\Art\\... before Art\\ (GameFileClass::Set_Name), so a
name a language archive holds there (W3DEnglishZH.big's UITRST_SKNP.W3D) has to ship at that path,
and an Art\\ copy alone is refused, since the language copy would shadow it.
The archive's name has to start with "Reforged": that is what makes it beat every
retail archive (Win32BIGFileSystem::init), what Classic Graphics leaves out, what Tools/vendor.ps1
fetches and what the launcher ships. One archive per batch, ReforgedUSA.big, ReforgedChina.big, ReforgedGLA.big.

A batch is optional to the network: the INI checksum hashes the retail copy of every model it covers
(ArchiveFileSystem::mountOverRetail), so a player with it and one without play one match, and
nothing but this script keeps the logic's reading of the two copies equal. The logic reads a model's
pristine bones: fire points, barrels, turrets, docks, exits, garrison points (W3DModelDraw's
validateCachedBones). So a model goes in only when, against the retail model of its name:
  - the retail pivots come first, in retail's order, with the same names, parents and the same bits
    of translation and rotation (a rotation negated whole counts as the same);
  - no pivot name appears twice, and an added pivot is ROOTTRANSFORM or WHEEL_L/R<n>;
  - the hierarchy and the HLOD's skeleton keep retail's names (a bare retail mesh may gain an HLOD
    over a hierarchy of its own name);
  - no sub-object the bone lookup falls back to changes under a name an INI or the code asks for
    (the words of every bone, turret, runway and muzzle key and every Bone:<name> in INIZH.big,
    the patch archives and the fork's INI masters, or a two-digit run of one);
  - every animation chunk is retail's byte for byte, a NaN for a NaN in a plain animation's floats,
    which covers an animation-only file too.
A model retail does not have is refused: a player without the batch would have none. A model that
fails is left out and named; the rest of the batch still ships.

Refused outright: a staged name another Reforged*.big beside the output already holds (the later
archive would win silently) and one a loose file under Run\\Art shadows (the archive would never be
read). The retail archives are read from the output's folder (Patch*.big first, then the rest in
name order) and from the base game folder, ZH_Generals beside it by default.

The one exception is the base art, ReforgedTextures, ReforgedNormals and ReforgedTerrain: the game
mounts those first and every batch over them (ArchiveFileSystem::reforgedMountsBefore), so a batch
may replace a name they hold when it says so. --override-base names those paths: a folder (each file
by its path from the Art or Data folder above it, so the staged Art\\Textures folder itself will do),
a text file with one path a line, or paths separated by commas. A clash with the base art that is not
named is still refused, and every name overridden is printed.
"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigfile  # noqa: E402

SEP = "\\"
W3D_HIERARCHY, W3D_HIERARCHY_HEADER, W3D_PIVOTS, PIVOT_SIZE = 0x100, 0x101, 0x102, 60
W3D_HLOD, W3D_HLOD_HEADER, W3D_HLOD_SUB_OBJECT = 0x700, 0x701, 0x704
W3D_HLOD_LISTS = (0x702, 0x705, 0x706)                     # LOD arrays, aggregates, proxies
W3D_ANIMATIONS = (0x200, 0x280, 0x2C0)                     # plain, compressed, morph
# The only bones a rebuild may add: the root an HLOD needs over a model that was a bare mesh, and road
# wheels the client turns. Neither is a name any INI or the logic asks for (the lookup check below
# refuses one that is).
ADDED_BONE = re.compile(r"ROOTTRANSFORM|WHEEL_[LR]\d+")


def chunks(data, start, end):
    while start + 8 <= end:
        kind, size = struct.unpack_from("<II", data, start)
        size &= 0x7FFFFFFF
        yield kind, start + 8, min(start + 8 + size, end)
        start += 8 + size


def cstr(raw):
    return raw.split(b"\0")[0].decode("latin-1").upper()


def parse(data):
    """What the logic can read of a model: its hierarchy (name, header version and pivot count, pivots
    in file order as name, parent index, translation bytes, rotation bytes), the HLOD's name, skeleton
    and sub-objects (full name, bone index, in file order), and its animation chunks whole."""
    w3d = {"hierarchy": None, "header": None, "pivots": [], "hlod": None, "subs": [], "anims": []}
    for kind, a, b in chunks(data, 0, len(data)):
        if kind == W3D_HIERARCHY:
            for kind2, a2, b2 in chunks(data, a, b):
                if kind2 == W3D_HIERARCHY_HEADER:
                    w3d["hierarchy"] = cstr(data[a2 + 4:a2 + 20])
                    w3d["header"] = struct.unpack_from("<I", data, a2)[0], struct.unpack_from("<I", data, a2 + 20)[0]
                elif kind2 == W3D_PIVOTS:
                    for at in range(a2, b2 - PIVOT_SIZE + 1, PIVOT_SIZE):
                        w3d["pivots"].append((cstr(data[at:at + 16]), struct.unpack_from("<i", data, at + 16)[0],
                                              data[at + 20:at + 32], data[at + 44:at + 60]))
            w3d["hierarchy"] = w3d["hierarchy"] or ""
        elif kind == W3D_HLOD:
            for kind2, a2, b2 in chunks(data, a, b):
                if kind2 == W3D_HLOD_HEADER:
                    w3d["hlod"] = (cstr(data[a2 + 8:a2 + 24]), cstr(data[a2 + 24:a2 + 40]))
                elif kind2 in W3D_HLOD_LISTS:
                    for kind3, a3, b3 in chunks(data, a2, b2):
                        if kind3 == W3D_HLOD_SUB_OBJECT:
                            bone, = struct.unpack_from("<I", data, a3)
                            w3d["subs"].append((cstr(data[a3 + 4:a3 + 36]), bone))
        elif kind in W3D_ANIMATIONS:
            w3d["anims"].append((kind, data[a:b]))
    return w3d


W3D_ANIMATION_CHANNEL, CHANNEL_HEADER = 0x202, 12


def nan_equal(a, b):
    """Two runs of floats alike, except that a NaN in one may be any NaN in the other: a writer quiets
    a signalling NaN, and a NaN channel value reads the same either way."""
    if a == b:
        return True
    if len(a) != len(b) or len(a) % 4:
        return False
    for x, y in zip(struct.unpack("<%df" % (len(a) // 4), a), struct.unpack("<%df" % (len(b) // 4), b)):
        if struct.pack("<f", x) != struct.pack("<f", y) and not (x != x and y != y):
            return False
    return True


def same_animation(kind, a, b):
    """Retail's animation chunk byte for byte, but for a NaN in a plain animation's float channels.
    A compressed or morph animation is packed bits and has to match exactly."""
    if a == b:
        return True
    if kind != 0x200:
        return False
    ca, cb = list(chunks(a, 0, len(a))), list(chunks(b, 0, len(b)))
    if len(ca) != len(cb) or ca[-1][2] != len(a) or cb[-1][2] != len(b):
        return False
    for (k1, a1, b1), (k2, a2, b2) in zip(ca, cb):
        x, y = a[a1 - 8:b1], b[a2 - 8:b2]
        if k1 != k2 or len(x) != len(y):
            return False
        if x == y:
            continue
        head = 8 + CHANNEL_HEADER
        if k1 != W3D_ANIMATION_CHANNEL or x[:head] != y[:head] or not nan_equal(x[head:], y[head:]):
            return False
    return True


def same_turn(new, old):
    """One rotation: the same bytes, or every component negated (q and -q turn alike)."""
    return new == old or struct.pack("<4f", *[-x for x in struct.unpack("<4f", new)]) == old


def sub_object(subs, name):
    """The bone index of the sub-object RenderObjClass::Get_Sub_Object_By_Name finds for a name: a
    whole name first, then the part after the first '.', each in order; None when none."""
    for full, bone in subs:
        if full == name:
            return bone
    for full, bone in subs:
        if full.partition(".")[2 if "." in full else 0] == name:
            return bone
    return None


W3D_VERSION_3 = 3 << 16                                    # W3D_MAKE_VERSION(3,0): older ones gain a root


def lookup_name(name, lookups):
    """The logic can ask for this name: an INI or the code names it, alone or as a numbered run."""
    return name in lookups or (re.fullmatch(r".*\d\d", name) is not None and name[:-2] in lookups)


def model_problems(new, old, stem, lookups=frozenset()):
    """Why the rebuilt model would not play like the retail one, empty when it would. The logic reads
    a model's pristine bones (W3DModelDraw's validateCachedBones): the hierarchy's pivots, posed by an
    animation at its first or last frame, found by name, and a sub-object's bone when no pivot has the
    name. So the retail pivots have to come first in the same order with the same bits, the skeleton
    and the animations have to be retail's, and nothing new may answer a name the logic asks for."""
    n, o = parse(new), parse(old)
    problems = []
    if o["hierarchy"] is not None and n["hierarchy"] != o["hierarchy"]:
        problems.append("hierarchy %s is not retail's %s" % (n["hierarchy"] or "missing", o["hierarchy"]))
    elif o["hierarchy"] is None and n["hierarchy"] is not None and n["hierarchy"] != stem:
        problems.append("adds hierarchy %s under another model's name" % n["hierarchy"])
    if o["hlod"] is not None and (n["hlod"] is None or n["hlod"][1] != o["hlod"][1]):
        problems.append("skeleton %s is not retail's %s" % (n["hlod"][1] if n["hlod"] else "missing", o["hlod"][1]))
    elif o["hlod"] is None and n["hlod"] is not None and n["hlod"][1] != (n["hierarchy"] or "") and n["hlod"][1] != stem:
        problems.append("points at skeleton %s, which a bare mesh did not have" % n["hlod"][1])

    # HTreeClass::Load_W3D reads as many pivots as the header says, and before 3.0 puts a root in front
    # of them, which moves every bone by one
    if n["header"] is not None and (n["header"][0] < W3D_VERSION_3 or n["header"][1] != len(n["pivots"])):
        problems.append("hierarchy header version %#x with %d pivots for %d" % (n["header"][0], n["header"][1], len(n["pivots"])))

    po, pn = o["pivots"], n["pivots"]
    names = [p[0] for p in pn]
    dupes = sorted({x for x in names if names.count(x) > 1})
    if dupes:
        problems.append("names twice %s" % ", ".join(dupes))
    old_names = [p[0] for p in po]
    missing = [x for x in old_names if x not in names]
    if missing:
        problems.append("drops %s" % ", ".join(missing))
    elif names[:len(po)] != old_names:
        problems.append("reorders the retail bones")
    else:
        moved = [p[0] for p, q in zip(po, pn) if p[1] != q[1] or p[2] != q[2] or not same_turn(q[3], p[3])]
        if moved:
            problems.append("moves %s" % ", ".join(moved))
    # pivot 0 is never found by name (findSingleBone takes index 0 for none), so the ROOTTRANSFORM an
    # HLOD puts over a bare retail mesh answers nothing, though the drop and particle bones of some
    # INIs are spelled RootTransform
    added = [(i, x) for i, x in enumerate(names) if i >= len(po) and x not in old_names]
    odd = [x for i, x in added if not ADDED_BONE.fullmatch(x) or (i > 0 and lookup_name(x, lookups))]
    if odd:
        problems.append("adds bone %s" % ", ".join(odd))

    # A name falls through to the sub-objects when no pivot past the first answers it (pivot 0 never
    # does), so a retail pivot from the second on shields its name and nothing else does.
    def bone_of(w3d, piv, name):
        i = sub_object(w3d["subs"], name)
        return None if i is None else piv[i][0] if i < len(piv) else i
    asked = {part for full, _ in o["subs"] + n["subs"] for part in (full, full.partition(".")[2] or full)}
    changed = sorted(s for s in asked if s not in old_names[1:] and lookup_name(s, lookups)
                     and bone_of(o, po, s) != bone_of(n, pn, s))
    if changed:
        problems.append("changes sub-object %s, a name the logic asks for" % ", ".join(changed))

    if len(n["anims"]) != len(o["anims"]) or not all(k1 == k2 and same_animation(k1, a1, a2)
                                                       for (k1, a1), (k2, a2) in zip(n["anims"], o["anims"])):
        problems.append("animation is not retail's")
    return problems


# An INI key that names a bone: ExitBone, WeaponFireFXBone, BladeBoneName, YardBonePrefix, the turret
# keys, the runway lists, the muzzle flashes. Every word after it counts, and so does every Bone:<name>
# a BoneFXUpdate or TransitionDamageFX value carries under any key.
BONE_KEY = re.compile(r"\s*\w*(bone|turret|runway|muzzle)\w*\s*=", re.I)
BONE_WORD = re.compile(r"bone\s*:\s*(\w+)", re.I)


def ini_lookups(archives, folders):
    """Every name an INI or the logic's own code can ask a model for, upper case: the words of every
    bone key in every .ini the retail archives and the fork's masters hold, and the names the logic
    writes in the code."""
    words = set(CODE_BONES)
    texts = [read_member(w) for a in archives for p, w in index_of(a).items() if p.endswith(".ini")]
    for folder in folders:
        for root, _dirs, files in os.walk(folder):
            texts += [open(os.path.join(root, f), "rb").read() for f in files if f.lower().endswith(".ini")]
    for text in texts:
        for line in text.decode("latin-1").splitlines():
            line = line.split(";")[0].split("//")[0]
            words.update(w.upper() for w in BONE_WORD.findall(line))
            if BONE_KEY.match(line):
                words.update(w.upper() for w in line.partition("=")[2].split())
    return frozenset(words)


# getPristineBonePositions and friends called with a literal in GameLogic (DockUpdate, ChinookAIUpdate,
# ParachuteContain, GarrisonContain, ParkingPlaceBehavior, RailedTransportDockUpdate)
CODE_BONES = ("FIREPOINT", "STATION", "HELIPARK01", "ROPESTART", "ROPEEND", "PARA_MAN", "PARA_COG", "PARA_ATTCH",
              "DOCKSTART", "DOCKACTION", "DOCKEND", "DOCKWAITING", "DOCKWAITING07")


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


BASE_ART = ("reforgedtextures.big", "reforgednormals.big", "reforgedterrain.big")


def override_paths(spec):
    """The archive paths --override-base names, lower case: a folder's files by their path from the
    Art or Data folder above them, a list file's lines, or a comma list."""
    if os.path.isdir(spec):
        parts = os.path.normpath(os.path.abspath(spec)).split(os.sep)
        tops = [i for i, p in enumerate(parts) if p.lower() in ("art", "data")]
        if not tops:
            sys.exit("%s: no Art or Data folder above it to name its files from" % spec)
        top = tops[-1]
        if top >= 2 and parts[top].lower() == "art" and parts[top - 2].lower() == "data":
            top -= 2                                       # Data\<Language>\Art\...
        if top < 2:
            sys.exit("%s: its Art or Data folder sits at the top of the drive; stage it one folder down" % spec)
        root = os.sep.join(parts[:top])
        return {os.path.relpath(os.path.join(folder, f), root).replace(os.sep, SEP).lower()
                for folder, _dirs, files in os.walk(spec) for f in files}
    items = open(spec).read().splitlines() if os.path.isfile(spec) else spec.split(",")
    return {i.strip().replace("/", SEP).lower() for i in items if i.strip()}


def base_clashes(members, held, allowed):
    """(overridden, refused) of the staged paths a base art archive holds, split by --override-base."""
    clash = sorted(set(held) & set(members))
    return [p for p in clash if p in allowed], [p for p in clash if p not in allowed]


def pack(staged, output, base, allowed=frozenset()):
    run = os.path.dirname(os.path.abspath(output))
    name = os.path.basename(output)
    if not name.lower().startswith("reforged") or name.lower() in BASE_ART:
        sys.exit("%s: the archive's name has to start with Reforged (and not name the base art, %s)" % (name, ", ".join(BASE_ART)))

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

    clashes, overridden = [], []
    for n in bigs(run):
        if not n.lower().startswith("reforged") or n.lower() == name.lower():
            continue
        if n.lower() in BASE_ART:
            over, refused_here = base_clashes(members, index_of(os.path.join(run, n)), allowed)
            overridden += ["%s (over %s)" % (p, n) for p in over]
            clashes += ["%s (in %s; name it in --override-base to replace it)" % (p, n) for p in refused_here]
        else:
            clashes += ["%s (in %s)" % (p, n) for p in sorted(set(index_of(os.path.join(run, n))) & set(members))]
    clashes += ["%s (loose in Run)" % p for p in sorted(members) if os.path.exists(os.path.join(run, p.replace(SEP, os.sep)))]
    clashes += ["%s (%s is read first: stage it at that path)" % (p, o) for p, o in shadowed(members, retail)]
    if clashes:
        sys.exit("refused, these names are already somewhere that wins or loses against this archive:\n  " + "\n  ".join(clashes))

    if not any(n.lower() == "inizh.big" for n in root):
        sys.exit("no INIZH.big beside %s: the bone names the INIs ask for come from it" % output)
    # a user's own map.ini can name a bone too; that is the one source left out
    lookups = ini_lookups([os.path.join(run, n) for n in root] + [os.path.join(base, n) for n in base_bigs],
                          [os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "INI"),
                           os.path.join(run, "Data", "INI")])
    refused = {}
    replaced = 0
    for path, full in members.items():
        if not path.endswith(".w3d"):
            continue
        if path not in retail:
            # the batch is optional to the network: a model only it has would be missing for a player
            # without it while his INI names the same object
            refused[path] = ["a model retail does not have"]
            continue
        replaced += 1
        with open(full, "rb") as f:
            problems = model_problems(f.read(), read_member(retail[path]), path.split(SEP)[-1][:-4].upper(), lookups)
        if problems:
            refused[path] = problems

    kept = [(spelled[path], full) for path, full in members.items() if path not in refused]
    bigfile.write_archive(output, kept)
    w3d = sum(p.lower().endswith(".w3d") for p, _ in kept)
    print("%d models (%d replace a retail one, %d new), %d other files" % (
        w3d, w3d, 0, len(kept) - w3d))
    for path in sorted(refused):
        print("left out %s: %s" % (path, "; ".join(refused[path])))
    for line in overridden:
        print("overrides %s" % line)


def selfcheck():
    def chunk(kind, body, sub=False):
        return struct.pack("<II", kind, len(body) | (0x80000000 if sub else 0)) + body

    def model(bones, hierarchy="AVTANK", skeleton="AVTANK", subs=(), anim=None, q=(0, 0, 0, 1), version=0x40001, count=None):
        names = [b[0] for b in bones]
        records = b"".join(name.encode().ljust(16, b"\0") + struct.pack("<i", names.index(parent) if parent else -1)
                           + struct.pack("<3f", *t) + struct.pack("<3f", 0, 0, 0) + struct.pack("<4f", *q)
                           for name, parent, t in bones)
        out = b""
        if hierarchy is not None:
            header = struct.pack("<I", version) + hierarchy.encode().ljust(16, b"\0") \
                + struct.pack("<I3f", len(bones) if count is None else count, 0, 0, 0)
            out += chunk(W3D_HIERARCHY, chunk(W3D_HIERARCHY_HEADER, header) + chunk(W3D_PIVOTS, records), True)
        if skeleton is not None:
            entries = b"".join(chunk(W3D_HLOD_SUB_OBJECT, struct.pack("<I", names.index(bone))
                                     + (s[1:] if s[0] == "=" else s if "." in s else "AVTANK." + s).encode().ljust(32, b"\0"))
                               for s, bone in subs)
            out += chunk(W3D_HLOD, chunk(W3D_HLOD_HEADER, struct.pack("<II", 0, 1) + b"AVTANK".ljust(16, b"\0")
                                         + skeleton.encode().ljust(16, b"\0")) + chunk(0x702, entries, True), True)
        if anim is not None:
            # an odd-length header first, so the channel's floats sit off a four-byte boundary as in
            # ABSupplyCt_S, whose quieted NaNs a word-by-word compare missed
            channel = chunk(W3D_ANIMATION_CHANNEL, struct.pack("<5H2x", 0, len(anim) // 4 - 1, 1, 6, 1) + anim)
            out += chunk(0x200, chunk(0x201, b"abc") + channel, True)
        return out

    bones = [("ROOT", None, (0, 0, 0)), ("TURRET", "ROOT", (1, 0, 2)), ("FIREPOINT01", "TURRET", (3, 0, 0))]
    nan, quiet = struct.pack("<I", 0x7F800001), struct.pack("<I", 0x7FC00001)
    curve = struct.pack("<2f", 1.0, 2.0)
    retail = model(bones, subs=[("BARREL", "TURRET")], anim=curve + nan)
    lookups = frozenset(["FIREPOINT", "MUZZLE", "BARREL", "EXITSTART", "ROOTTRANSFORM"])
    check = lambda new, old=retail: model_problems(new, old, "AVTANK", lookups)  # noqa: E731
    assert check(retail) == []
    # what today's batches do: road wheels, a quieted NaN in a channel, a new LOD mesh, a renamed one
    assert check(model(bones + [("WHEEL_L01", "ROOT", (0, 1, 0))], subs=[("BARREL", "TURRET"), ("HULL_L1", "ROOT")],
                       anim=curve + quiet)) == []
    assert check(model(bones + [("WHEEL_L01", "ROOT", (0, 1, 0))], subs=[("BARREL", "TURRET")], anim=curve + nan, q=(0, 0, 0, 1)),
                 model(bones, subs=[("BARREL", "TURRET")], anim=curve + nan, q=(-0.0, -0.0, -0.0, -1))) == []
    # a bare mesh made an HLOD over its own root
    assert model_problems(model([("ROOTTRANSFORM", None, (0, 0, 0))], "PROP", "PROP", subs=[("PROP", "ROOTTRANSFORM")]),
                          b"", "PROP", lookups) == []
    # and every way of failing
    assert check(model(bones[:2], subs=[("BARREL", "TURRET")], anim=curve + nan)) == ["drops FIREPOINT01"]
    assert check(model([bones[0], (bones[1][0], "ROOT", (1, 0, 2.0000002)), bones[2]], subs=[("BARREL", "TURRET")],
                       anim=curve + nan)) == ["moves TURRET"]
    assert check(model([bones[0], bones[2], bones[1]], subs=[("BARREL", "TURRET")], anim=curve + nan)) == ["reorders the retail bones"]
    assert check(model(bones + [("TURRET", "ROOT", (0, 0, 0))], subs=[("BARREL", "TURRET")], anim=curve + nan)) == \
        ["names twice TURRET"]
    assert check(model(bones + [("FIREPOINT02", "TURRET", (3, 1, 0))], subs=[("BARREL", "TURRET")], anim=curve + nan)) == \
        ["adds bone FIREPOINT02"]
    assert check(model(bones + [("MUZZLE", "TURRET", (3, 1, 0))], subs=[("BARREL", "TURRET")], anim=curve + nan)) == \
        ["adds bone MUZZLE"]
    # the header the engine reads: before 3.0 a root goes in front, and only the counted pivots load
    assert check(model(bones, subs=[("BARREL", "TURRET")], anim=curve + nan, version=0x20000)) == \
        ["hierarchy header version 0x20000 with 3 pivots for 3"]
    assert check(model(bones, subs=[("BARREL", "TURRET")], anim=curve + nan, count=2)) == \
        ["hierarchy header version 0x40001 with 2 pivots for 3"]
    # pivot 0 answers no name, so a sub-object under its name is what the logic finds
    assert check(model(bones, subs=[("BARREL", "TURRET"), ("ROOT", "TURRET")], anim=curve + nan),
                 model(bones, subs=[("BARREL", "TURRET")], anim=curve + nan)) == []
    assert model_problems(model(bones, subs=[("BARREL", "TURRET"), ("ROOTTRANSFORM", "TURRET")], anim=curve + nan), retail,
                          "AVTANK", lookups) == ["changes sub-object ROOTTRANSFORM, a name the logic asks for"]
    # a whole name beats a suffix, as Get_Sub_Object_By_Name looks
    assert check(model(bones, subs=[("BARREL", "TURRET"), ("=BARREL", "ROOT")], anim=curve + nan)) == \
        ["changes sub-object BARREL, a name the logic asks for"]
    assert check(model(bones, subs=[("BARREL", "TURRET"), ("OTHER.BARREL", "ROOT")], anim=curve + nan)) == []
    # an INI asks for RootTransform: as pivot 0 it answers nothing, anywhere else it would
    assert check(model(bones + [("ROOTTRANSFORM", "ROOT", (0, 0, 0))], subs=[("BARREL", "TURRET")], anim=curve + nan)) == \
        ["adds bone ROOTTRANSFORM"]
    assert check(model(bones, "AVTANK2", subs=[("BARREL", "TURRET")], anim=curve + nan)) == ["hierarchy AVTANK2 is not retail's AVTANK"]
    assert check(model(bones, skeleton="AVTANK_SKL", subs=[("BARREL", "TURRET")], anim=curve + nan)) == \
        ["skeleton AVTANK_SKL is not retail's AVTANK"]
    assert check(model(bones, subs=[("BARREL", "ROOT")], anim=curve + nan)) == \
        ["changes sub-object BARREL, a name the logic asks for"]
    assert check(model(bones, subs=[("BARREL", "TURRET"), ("EXITSTART", "ROOT")], anim=curve + nan)) == \
        ["changes sub-object EXITSTART, a name the logic asks for"]
    assert check(model(bones, subs=[("BARREL", "TURRET")], anim=struct.pack("<2f", 1.0, 2.5) + nan)) == ["animation is not retail's"]
    assert check(model(bones, subs=[("BARREL", "TURRET")])) == ["animation is not retail's"]
    # an animation-only file is its animation
    assert model_problems(model([], None, None, anim=curve), model([], None, None, anim=curve), "AVTANK_A") == []
    assert model_problems(model([], None, None, anim=curve + nan), model([], None, None, anim=curve), "AVTANK_A") == \
        ["animation is not retail's"]
    assert model_problems(model([("ROOTTRANSFORM", None, (0, 0, 0))], "AVTANK", "AVTANK"), b"", "PROP") == \
        ["adds hierarchy AVTANK under another model's name"]
    assert lookup_name("FIREPOINT03", lookups) and not lookup_name("WHEEL_L01", lookups)
    assert nan_equal(nan, quiet) and not nan_equal(nan, struct.pack("<f", 1.0))

    english = "data\\english\\art\\w3d\\uitrst_sknp.w3d"
    retail_paths = {english: None, "art\\w3d\\uirtoxf_skn.w3d": None}
    assert shadowed({"art\\w3d\\uitrst_sknp.w3d": ""}, retail_paths) == [("art\\w3d\\uitrst_sknp.w3d", english)]
    assert shadowed({"art\\w3d\\uitrst_sknp.w3d": "", english: ""}, retail_paths) == []
    assert shadowed({english: "", "art\\w3d\\uirtoxf_skn.w3d": ""}, retail_paths) == []

    bridge, wall = "art\\textures\\cbbridge.dds", "art\\textures\\cbwall.dds"
    held = {bridge: None, wall: None, "art\\textures\\other.dds": None}
    assert base_clashes({bridge: "", wall: ""}, held, set()) == ([], [bridge, wall])
    assert base_clashes({bridge: "", wall: ""}, held, {bridge}) == ([bridge], [wall])
    assert override_paths("Art/Textures/CBBridge.dds, art\\textures\\cbwall.dds") == {bridge, wall}
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "Object.ini"), "w") as f:
            f.write("Object X\n  Geometry = BOX\n  ExitBone = ExitStart ; the door\n  WeaponFireFXBone = PRIMARY Muzzle\n"
                    "  PristineParticleSystem1 = bone:Smoke01 OnlyOnce:Yes\n  Turret = TURRET01\nEnd\n")
        found = ini_lookups([], [tmp])
        assert {"EXITSTART", "PRIMARY", "MUZZLE", "SMOKE01", "TURRET01", "DOCKSTART"} <= found and "BOX" not in found
        folder = os.path.join(tmp, "stage", "Art", "Textures")
        os.makedirs(folder)
        open(os.path.join(folder, "CBBridge.dds"), "wb").close()
        assert override_paths(folder) == {bridge}
        lang = os.path.join(tmp, "stage", "Data", "English", "Art", "Textures")
        os.makedirs(lang)
        open(os.path.join(lang, "CBBridge.dds"), "wb").close()
        assert override_paths(lang) == {"data\\english\\" + bridge}
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
    allowed = frozenset()
    if "--override-base" in args:
        i = args.index("--override-base")
        allowed = frozenset(override_paths(args[i + 1]))
        del args[i:i + 2]
    if len(args) not in (2, 3):
        print(__doc__.strip())
        return 2
    staged, output = args[:-1], args[-1]
    pack(staged, output, base or os.path.join(os.path.dirname(os.path.abspath(output)), "ZH_Generals"), allowed)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
