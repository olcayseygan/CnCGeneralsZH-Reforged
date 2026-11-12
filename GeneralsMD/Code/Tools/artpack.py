#!/usr/bin/env python3
"""Pack a staged tree of rebuilt art into one of the fork's art archives.

    python artpack.py <staged Art folder> [<staged Data folder>] <Run\\Reforged<Name>.big> [--base <base game folder>]
                      [--override-base <folder, list file or comma list>] [--fork-masters <folder>]
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

--fork-masters <folder> (Code\\Data\\Art\\W3D) names the fork's own models, which the build copies
loose into Run\\Art\\W3D, where every player has them and the INI checksum hashes them. A batch may
carry a copy of one (ReforgedTurkiye.big: the Turkiye models in levels of detail): the master plays
retail's part in the checks above, and its hierarchy and animation chunks have to be the master's
byte for byte, which the game compares before it draws the archive's copy over the loose master,
along with the sub-object bindings fork_master_problems names. Its full level may be a high-resolution
rebuild with meshes of its own.
The loose copy in Run has to be the master's bytes (build first); any other loose clash is refused.

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
# The Strategy Center's hidden prop meshes need a static bone retail's root, FAN01, FAN02 lack. As the
# last pivot under the root with no channel it moves no other bone and no INI or code asks for it.
PROP_PIVOT = "PWPROPS"


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
    odd = [x for i, x in added if not (ADDED_BONE.fullmatch(x) or x == PROP_PIVOT and i == len(pn) - 1 and pn[i][1] == 0)
           or (i > 0 and lookup_name(x, lookups))]
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


W3D_HMODEL = 0x300


def logic_chunks(data):
    """The top-level chunks of a model the logic reads besides its hierarchy, whole and in file order:
    every animation and an HModel."""
    return [data[a - 8:b] for kind, a, b in chunks(data, 0, len(data)) if kind == W3D_HMODEL or kind in W3D_ANIMATIONS]


W3D_PIVOT_FIXUPS, FIXUP_SIZE = 0x103, 48
WHEEL_PIVOT = re.compile(r"WHEEL_[LR]\d\d")


def appended_wheels(new, master):
    """(the pivots a copy's hierarchy adds after the master's, why it may not, empty when it may).
    A tracked or wheeled rebuild hangs road wheels the client turns off the master's skeleton: every
    hierarchy sub-chunk the master's bytes, but the header's pivot count, the pivots and their fixups,
    which may run on past the master's with WHEEL_[LR]<two digits> pivots parented to a master pivot.
    An animation's channels address the master's pivots by index and the loader sizes its motion to
    the tree, so an added pivot stands still and moves nothing before it."""
    n = [(a, b) for kind, a, b in chunks(new, 0, len(new)) if kind == W3D_HIERARCHY]
    m = [(a, b) for kind, a, b in chunks(master, 0, len(master)) if kind == W3D_HIERARCHY]
    if len(n) != len(m):
        return [], ["hierarchy is not the fork master's"]
    added, problems = [], []
    for (na, nb), (ma, mb) in zip(n, m):
        ns = [(kind, new[a:b]) for kind, a, b in chunks(new, na, nb)]
        ms = [(kind, master[a:b]) for kind, a, b in chunks(master, ma, mb)]
        if [k for k, _ in ns] != [k for k, _ in ms]:
            return [], ["hierarchy is not the fork master's"]
        extra = 0
        for (kind, nd), (_, md) in zip(ns, ms):
            if kind == W3D_PIVOTS:
                if not nd.startswith(md) or len(md) % PIVOT_SIZE or (len(nd) - len(md)) % PIVOT_SIZE:
                    problems.append("hierarchy pivots are not the fork master's")
                    continue
                extra, count = (len(nd) - len(md)) // PIVOT_SIZE, len(md) // PIVOT_SIZE
                old = [cstr(md[at:at + 16]) for at in range(0, len(md), PIVOT_SIZE)]
                for at in range(len(md), len(nd), PIVOT_SIZE):
                    name, parent = cstr(nd[at:at + 16]), struct.unpack_from("<i", nd, at + 16)[0]
                    if not WHEEL_PIVOT.fullmatch(name) or not 0 <= parent < count or name in old + added:
                        problems.append("adds pivot %s, not a WHEEL_[LR]<nn> under a master pivot" % name)
                    added.append(name)
            elif kind not in (W3D_HIERARCHY_HEADER, W3D_PIVOT_FIXUPS) and nd != md:
                problems.append("hierarchy is not the fork master's")
        for (kind, nd), (_, md) in zip(ns, ms):
            if kind == W3D_HIERARCHY_HEADER and (len(nd) != len(md) or len(nd) < 24 or nd[:20] != md[:20] or nd[24:] != md[24:]
                                                 or struct.unpack_from("<I", nd, 20)[0] != struct.unpack_from("<I", md, 20)[0] + extra):
                problems.append("hierarchy header is not the fork master's but for its pivot count")
            if kind == W3D_PIVOT_FIXUPS and (not nd.startswith(md) or len(nd) - len(md) != extra * FIXUP_SIZE):
                problems.append("pivot fixups are not the fork master's")
    return added, problems


def wheel_problems(added, master_names, pivots, public=frozenset()):
    """Why the added wheel pivots could answer a name the master answers otherwise, empty when they
    could not: doSingleBoneName asks for NAME and NAME01 on as bones before any sub-object, so no
    added name, nor its stem, may be one the master answers or a numbered run of one, nor a public bone."""
    known = set(master_names) | set(pivots)
    bad = sorted(q for q in added if q in known or q in public or q[:-2] in public or q[:-2] in known
                 or any(len(k) == len(q) and k[:-2] == q[:-2] and k[-2:].isdigit() for k in known))
    return ["adds pivot %s, a name or run the master answers or a public bone" % ", ".join(bad)] if bad else []


def hlod_parts(data):
    """Every HLOD in the file as (its header's name and skeleton, its aggregate and proxy arrays whole,
    its sub-objects as (LOD array index, name, bone) in the order Get_Sub_Object_By_Name walks them:
    the LOD arrays coarsest first as the file holds them, then the aggregates, whose index is None).
    None for an HLOD the engine would not read: no single header or no LOD array."""
    hlods = []
    for kind, a, b in chunks(data, 0, len(data)):
        if kind != W3D_HLOD:
            continue
        heads, extras, subs, aggregates, arrays = [], [], [], [], 0
        for kind2, a2, b2 in chunks(data, a, b):
            if kind2 == W3D_HLOD_HEADER:
                heads.append((cstr(data[a2 + 8:a2 + 24]), cstr(data[a2 + 24:a2 + 40])))
            elif kind2 in W3D_HLOD_LISTS:
                if kind2 != 0x702:
                    extras.append(data[a2 - 8:b2])
                records = [(cstr(data[a3 + 4:a3 + 36]), struct.unpack_from("<I", data, a3)[0])
                           for kind3, a3, b3 in chunks(data, a2, b2) if kind3 == W3D_HLOD_SUB_OBJECT and b3 - a3 >= 36]
                if kind2 == 0x702:
                    subs += [(arrays, name, bone) for name, bone in records]
                    arrays += 1
                elif kind2 == 0x705:
                    aggregates += [(None, name, bone) for name, bone in records]
        hlods.append((heads[0], extras, subs + aggregates) if len(heads) == 1 and arrays else None)
    return hlods


W3D_MESH, W3D_MESH_HEADER3, W3D_BOX, W3D_NULL_OBJECT = 0x000, 0x01F, 0x740, 0x750


def prototypes(data):
    """The render objects the file itself defines, upper case: every mesh as <container>.<mesh> (or the
    mesh alone with no container), every box and null object by its name."""
    names = set()
    for kind, a, b in chunks(data, 0, len(data)):
        if kind == W3D_MESH:
            for kind2, a2, b2 in chunks(data, a, b):
                if kind2 == W3D_MESH_HEADER3 and b2 - a2 >= 40:
                    mesh, container = cstr(data[a2 + 8:a2 + 24]), cstr(data[a2 + 24:a2 + 40])
                    names.add(container + "." + mesh if container else mesh)
        elif kind == W3D_BOX and b - a >= 40:
            names.add(cstr(data[a + 8:a + 40]))
        elif kind == W3D_NULL_OBJECT and b - a >= 48:
            names.add(cstr(data[a + 16:a + 48]))
    return names


def answers(subs):
    """Every name Get_Sub_Object_By_Name finds in these sub-objects: a whole name, or the part after
    its first '.'."""
    return {part for _, full, _ in subs for part in (full, full.partition(".")[2] or full)}


def own_name(name, master_answers):
    """A name a copy may answer that the master does not: a coarse level's copy of a master mesh,
    <name>_L<digit> (what lodtr writes, the form Is_Part_Or_LOD_Copy reads), or a rebuilt detail mesh
    whose part after the container's '.' starts with the reserved HD_."""
    if (name.partition(".")[2] if "." in name else name).startswith("HD_"):
        return True
    lod = re.fullmatch(r"(.+)_L\d", name)
    return lod is not None and lod.group(1) in master_answers


def sub_binding_problems(n_subs, m_subs, pivots, public=frozenset()):
    """Why the copy's sub-objects would hand the logic another bone than the master's, empty when they
    would not. The logic falls back to a sub-object for a name no pivot from the second on answers
    (W3DModelDraw's findSingleSubObj), so: every name the master answers that way is found in the copy
    on the master's bone, every level of the copy holds a master sub-object's name only on a bone the
    master gives it, and a name only the copy answers is own_name's, no pivot's and, whole or less a
    two-digit tail, no public bone's (StandardPublicBone and the names the code asks for)."""
    shield = set(pivots[1:])
    am, ac = answers(m_subs), answers(n_subs)
    flat = lambda subs: [(full, bone) for _, full, bone in subs]  # noqa: E731
    problems = []
    rebound = sorted(q for q in am - shield if sub_object(flat(n_subs), q) != sub_object(flat(m_subs), q))
    if rebound:
        problems.append("finds the master's %s on another bone" % ", ".join(rebound))
    bones = {}
    for _, full, bone in m_subs:
        bones.setdefault(full, set()).add(bone)
    held = sorted({"%s on bone %d" % (full, bone) for level, full, bone in n_subs
                   if level is not None and full in bones and full not in shield and bone not in bones[full]})
    if held:
        problems.append("a level holds the master's %s" % ", ".join(held))
    def refused(q):
        stem = q[:-2] if len(q) > 2 and q[-2:].isdigit() else None
        return not own_name(q, am) or q in shield or q in public or stem in public
    added = sorted(q for q in ac - am if refused(q))
    if added:
        problems.append("adds sub-object name %s, not a _L<digit> copy or HD_ mesh, or a bone's" % ", ".join(added))
    return problems


def fork_master_problems(new, master, stem, lookups=frozenset()):
    """Why a batch's copy of one of the fork's own models may not stand in for it, empty when it may.
    The master is loose under Code/Data/Art/W3D, every player has it and the INI CRC hashes it, so it
    plays the part retail plays for the other models: model_problems against it, and on top what the
    game compares before it draws a batch's copy over a loose master (FileSystem::takesArchivedModel;
    any other copy and it draws the master): the animation and HModel chunks byte for byte, the
    hierarchy too but for the road wheels appended_wheels and wheel_problems let a rebuild add;
    per HLOD its name and skeleton and its aggregate and proxy arrays; every LOD array record naming a
    render object its own file defines, in the master and the copy (HLodClass skips one it cannot make,
    so the prediction would lie); and sub_binding_problems, the public bones being every name an INI or
    the code asks for. Mesh geometry, sub-object counts, materials, textures and MaxScreenSize are the
    copy's own, so its full level may be a high-resolution rebuild."""
    problems = model_problems(new, master, stem, lookups)
    if logic_chunks(new) != logic_chunks(master):
        problems.append("animation or HModel bytes are not the fork master's")
    added, hierarchy = appended_wheels(new, master)
    problems += hierarchy
    n_hlods, m_hlods = hlod_parts(new), hlod_parts(master)
    if None in n_hlods or None in m_hlods or len(n_hlods) != len(m_hlods):
        problems.append("HLOD count or layout is not the fork master's")
        return problems
    pivots = [p[0] for p in parse(master)["pivots"]]
    problems += wheel_problems(added, {q for hlod in m_hlods for q in answers(hlod[2])}, pivots, lookups)
    for data, hlods, whose in ((new, n_hlods, "copy"), (master, m_hlods, "master")):
        defined = prototypes(data)
        ghosts = sorted({full for hlod in hlods for level, full, _ in hlod[2] if level is not None and full not in defined})
        if ghosts:
            problems.append("the %s's levels name %s, which it does not define" % (whose, ", ".join(ghosts)))
    for (n_names, n_extras, n_subs), (m_names, m_extras, m_subs) in zip(n_hlods, m_hlods):
        if n_names != m_names:
            problems.append("HLOD name or skeleton is not the fork master's")
        if n_extras != m_extras:
            problems.append("aggregates or proxies are not the fork master's")
        problems += sub_binding_problems(n_subs, m_subs, pivots, lookups)
    return problems


def hierarchy_trees(data):
    """(name, bytes) of every hierarchy chunk a model defines, the name upper case."""
    trees = []
    for kind, a, b in chunks(data, 0, len(data)):
        if kind == W3D_HIERARCHY:
            trees += [(cstr(data[a2 + 4:a2 + 20]), data[a - 8:b]) for kind2, a2, b2 in chunks(data, a, b)
                      if kind2 == W3D_HIERARCHY_HEADER and b2 - a2 >= 20]
    return trees


def tree_clashes(models):
    """Hierarchy names the given models (path -> bytes) define two ways, one line each. The game keeps
    the first tree loaded under a name (HTreeManagerClass::Load_Tree), so a family sharing a tree has
    to bring the same one in every file a player can end up with, wheels in all or in none."""
    trees = {}
    for path, data in models.items():
        for name, raw in hierarchy_trees(data):
            trees.setdefault(name, {}).setdefault(raw, set()).add(path)
    return ["hierarchy %s differs between %s" % (name, " and ".join(", ".join(sorted(paths)) for paths in forms.values()))
            for name, forms in sorted(trees.items()) if len(forms) > 1]


def fork_masters(folder):
    """art\\w3d\\<name>.w3d, lower case, -> the master's path, for every .w3d in folder."""
    return {("art" + SEP + "w3d" + SEP + f).lower(): os.path.join(folder, f)
            for f in os.listdir(folder) if f.lower().endswith(".w3d")}


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


def pack(staged, output, base, allowed=frozenset(), masters=None):
    masters = masters or {}
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
    for p in sorted(members):
        loose = os.path.join(run, p.replace(SEP, os.sep))
        if not os.path.exists(loose):
            continue
        if p not in masters:
            clashes.append("%s (loose in Run)" % p)
        elif open(loose, "rb").read() != open(masters[p], "rb").read():
            # the batch's copy is checked against the master, and the game against the loose copy
            clashes.append("%s (loose in Run is not its fork master: build first)" % p)
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
    for path, full in members.items():
        if not path.endswith(".w3d"):
            continue
        stem = path.split(SEP)[-1][:-4].upper()
        with open(full, "rb") as f:
            data = f.read()
        if path in masters:
            # a fork master is what every player has and what the CRC hashes, retail copy or none
            with open(masters[path], "rb") as f:
                problems = fork_master_problems(data, f.read(), stem, lookups)
        elif path not in retail:
            # the batch is optional to the network: a model only it has would be missing for a player
            # without it while his INI names the same object
            problems = ["a model retail does not have"]
        else:
            problems = model_problems(data, read_member(retail[path]), stem, lookups)
        if problems:
            refused[path] = problems

    # the fork's models as a player with this batch has them: the batch's copy where it ships, the loose
    # master everywhere else
    fork = {}
    for path, master in masters.items():
        source = members[path] if path in members and path not in refused else master
        with open(source, "rb") as f:
            fork[path] = f.read()
    trees = tree_clashes(fork)
    if trees:
        sys.exit("refused, a tree name is defined two ways and the game keeps whichever loads first:\n  " + "\n  ".join(trees))

    kept = [(spelled[path], full) for path, full in members.items() if path not in refused]
    bigfile.write_archive(output, kept)
    w3d = sum(p.lower().endswith(".w3d") for p, _ in kept)
    forked = sum(p.lower() in masters for p, _ in kept)
    print("%d models (%d replace a retail one, %d a fork master), %d other files" % (
        w3d, w3d - forked, forked, len(kept) - w3d))
    for path in sorted(refused):
        print("left out %s: %s" % (path, "; ".join(refused[path])))
    for line in overridden:
        print("overrides %s" % line)


def selfcheck():
    def chunk(kind, body, sub=False):
        return struct.pack("<II", kind, len(body) | (0x80000000 if sub else 0)) + body

    def model(bones, hierarchy="AVTANK", skeleton="AVTANK", subs=(), anim=None, q=(0, 0, 0, 1), version=0x40001, count=None,
              coarse=(), lists=b"", hlod="AVTANK", tail=b"", undefined=()):
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
            def full(s):
                return s[1:] if s[0] == "=" else s if "." in s else "AVTANK." + s

            def array(level):
                return chunk(0x702, b"".join(chunk(W3D_HLOD_SUB_OBJECT, struct.pack("<I", names.index(bone))
                                                   + full(s).encode().ljust(32, b"\0"))
                                             for s, bone in level), True)
            # every level's mesh defined in the file, but for those named undefined
            for s in sorted({full(s) for level in list(coarse) + [subs] for s, _ in level} - set(undefined)):
                container, _, mesh = s.rpartition(".")
                out += chunk(W3D_MESH, chunk(W3D_MESH_HEADER3, struct.pack("<II", 0, 0) + mesh.encode().ljust(16, b"\0")
                                             + container.encode().ljust(16, b"\0")), True)
            # coarse: the LOD arrays before the full one (subs), coarsest first as hlod.cpp reads them;
            # lists: aggregate or proxy arrays after them
            out += chunk(W3D_HLOD, chunk(W3D_HLOD_HEADER, struct.pack("<II", 0, 1 + len(coarse)) + hlod.encode().ljust(16, b"\0")
                                         + skeleton.encode().ljust(16, b"\0"))
                         + b"".join(array(level) for level in coarse) + array(subs) + lists, True)
        if anim is not None:
            # an odd-length header first, so the channel's floats sit off a four-byte boundary as in
            # ABSupplyCt_S, whose quieted NaNs a word-by-word compare missed
            channel = chunk(W3D_ANIMATION_CHANNEL, struct.pack("<5H2x", 0, len(anim) // 4 - 1, 1, 6, 1) + anim)
            out += chunk(0x200, chunk(0x201, b"abc") + channel, True)
        return out + tail

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
    # the prop pivot: last and under the root, nowhere else
    assert check(model(bones + [("PWPROPS", "ROOT", (0, 0, 0))], subs=[("BARREL", "TURRET")], anim=curve + nan)) == []
    assert check(model(bones + [("PWPROPS", "TURRET", (0, 0, 0))], subs=[("BARREL", "TURRET")], anim=curve + nan)) == \
        ["adds bone PWPROPS"]
    assert check(model(bones + [("PWPROPS", "ROOT", (0, 0, 0)), ("WHEEL_L01", "ROOT", (0, 1, 0))], subs=[("BARREL", "TURRET")],
                       anim=curve + nan)) == ["adds bone PWPROPS"]
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
    # a fork master's levelled copy: coarser levels in front of the master's own, nothing else of it changed
    master = model(bones, subs=[("BARREL", "TURRET")], anim=curve + nan)
    logic = "animation or HModel bytes are not the fork master's"
    own = "not a _L<digit> copy or HD_ mesh, or a bone's"

    def fork(new_bones=bones, **kw):
        kw.setdefault("subs", [("BARREL", "TURRET")])
        kw.setdefault("anim", curve + nan)
        return fork_master_problems(model(new_bones, **kw), master, "AVTANK", lookups)
    assert fork(coarse=[[("BARREL_L2", "TURRET")], [("BARREL_L1", "TURRET"), ("BARREL", "TURRET")]]) == []
    assert fork(anim=curve + quiet) == [logic]
    assert fork(bones[:2]) == ["drops FIREPOINT01", "hierarchy pivots are not the fork master's",
                               "hierarchy header is not the fork master's but for its pivot count"]
    # the rest of what the game compares before it draws the batch's copy over the master
    assert fork(tail=chunk(W3D_HMODEL, b"hm")) == [logic]
    assert fork(hlod="AVTANK2") == ["HLOD name or skeleton is not the fork master's"]
    # road wheels hung off the master's skeleton, and nothing else added to it
    assert fork(bones + [("WHEEL_L01", "ROOT", (0, 1, 0)), ("WHEEL_R01", "TURRET", (0, -1, 0))]) == []
    assert fork(bones + [("TURRET2", "ROOT", (0, 1, 0))]) == \
        ["adds bone TURRET2", "adds pivot TURRET2, not a WHEEL_[LR]<nn> under a master pivot"]
    assert fork(bones + [("WHEEL_L01", "ROOT", (0, 1, 0)), ("WHEEL_L02", "WHEEL_L01", (0, 1, 0))]) == \
        ["adds pivot WHEEL_L02, not a WHEEL_[LR]<nn> under a master pivot"]
    assert fork(bones + [("WHEEL_L01", "ROOT", (0, 1, 0))], count=3) == \
        ["hierarchy header version 0x40001 with 3 pivots for 4", "hierarchy header is not the fork master's but for its pivot count"]
    wheeled = model(bones, subs=[("BARREL", "TURRET"), ("WHEEL_L01", "ROOT")], anim=curve + nan)
    assert fork_master_problems(model(bones + [("WHEEL_L01", "ROOT", (0, 1, 0))], subs=[("BARREL", "TURRET"), ("WHEEL_L01", "ROOT")],
                                      anim=curve + nan), wheeled, "AVTANK", lookups) == \
        ["adds pivot WHEEL_L01, a name or run the master answers or a public bone"]
    # the full level rebuilt at high resolution: more meshes of its own, the master's on its bone
    assert fork(subs=[("BARREL", "TURRET"), ("HD_HULL", "ROOT"), ("HD_TRACKS", "ROOT")],
                coarse=[[("BARREL", "TURRET"), ("HD_HULL_L1", "ROOT")], [("BARREL_L1", "TURRET")]]) == []
    assert fork(subs=[("HD_HULL", "ROOT")]) == ["changes sub-object BARREL, a name the logic asks for",
                                                "finds the master's AVTANK.BARREL, BARREL on another bone"]
    assert fork(lists=chunk(0x705, b"", True)) == ["aggregates or proxies are not the fork master's"]
    assert fork(coarse=[[("BARREL", "ROOT")]]) == ["changes sub-object BARREL, a name the logic asks for",
                                                   "finds the master's AVTANK.BARREL, BARREL on another bone",
                                                   "a level holds the master's AVTANK.BARREL on bone 0"]
    assert fork(coarse=[[("OTHER.BARREL", "ROOT")]]) == ["changes sub-object BARREL, a name the logic asks for",
                                                         "finds the master's BARREL on another bone",
                                                         "adds sub-object name OTHER.BARREL, " + own]
    # a record has to name a mesh the file defines, or HLodClass leaves it out and the prediction lies
    assert fork(coarse=[[("BARREL_L1", "TURRET")]], undefined=["AVTANK.BARREL_L1"]) == \
        ["the copy's levels name AVTANK.BARREL_L1, which it does not define"]
    # a name only the copy answers is a master mesh's _L<digit> copy or an HD_ mesh, and no bone's
    assert fork(coarse=[[("TURRET", "TURRET")]]) == ["adds sub-object name AVTANK.TURRET, TURRET, " + own]
    assert fork(coarse=[[("DOCKSTART", "TURRET")]]) == ["adds sub-object name AVTANK.DOCKSTART, DOCKSTART, " + own]
    assert fork(coarse=[[("HD_FIREPOINT01", "TURRET")]]) == []
    assert fork(coarse=[[("HD_MUZZLE01", "TURRET")]], subs=[("BARREL", "TURRET")]) == []
    assert fork(coarse=[[("BARREL01", "TURRET")]]) == ["changes sub-object BARREL01, a name the logic asks for",
                                                       "adds sub-object name AVTANK.BARREL01, BARREL01, " + own]
    assert fork(coarse=[[("FIREPOINT02", "TURRET")]]) == \
        ["changes sub-object FIREPOINT02, a name the logic asks for", "adds sub-object name AVTANK.FIREPOINT02, FIREPOINT02, " + own]
    assert fork(coarse=[[("=MUZZLE_L1", "TURRET")]], subs=[("BARREL", "TURRET")]) == ["adds sub-object name MUZZLE_L1, " + own]
    # a family sharing a tree name brings one tree: wheels in every file or in none
    wheels = model(bones + [("WHEEL_L01", "ROOT", (0, 1, 0))], subs=[("BARREL", "TURRET")], anim=curve + nan)
    damaged = model(bones, subs=[("BARREL", "TURRET")], hlod="AVTANK_D")
    assert tree_clashes({"avtank": master, "avtank_d": damaged}) == []
    assert tree_clashes({"avtank": wheels, "avtank_d": damaged}) == ["hierarchy AVTANK differs between avtank and avtank_d"]
    assert tree_clashes({"avtank": wheels, "avtank_d": model(bones + [("WHEEL_L01", "ROOT", (0, 1, 0))], hlod="AVTANK_D")}) == []
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
        masters = os.path.join(tmp, "W3D")
        os.makedirs(masters)
        open(os.path.join(masters, "TRAltay.W3D"), "wb").close()
        open(os.path.join(masters, "notes.txt"), "wb").close()
        assert fork_masters(masters) == {"art\\w3d\\traltay.w3d": os.path.join(masters, "TRAltay.W3D")}
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
    masters = {}
    if "--fork-masters" in args:
        i = args.index("--fork-masters")
        masters = fork_masters(args[i + 1])
        del args[i:i + 2]
    if len(args) not in (2, 3):
        print(__doc__.strip())
        return 2
    staged, output = args[:-1], args[-1]
    pack(staged, output, base or os.path.join(os.path.dirname(os.path.abspath(output)), "ZH_Generals"), allowed, masters)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
