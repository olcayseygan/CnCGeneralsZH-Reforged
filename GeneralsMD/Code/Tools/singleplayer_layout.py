#!/usr/bin/env python3
"""Lay the single player's screens out in the menu language, authored at 1920x1080.

The design is experiments/proofs/2026-11-24/screens/single-player/README-single-player.md in the
wrapper (docs/plans/MENU-REDESIGN.md, package 12).  Three screens stand in frame A on the board:

    ChallengeMenu   Generals' Challenge: nine general cards in three sides, the dossier, the places
                    map, the ladder tiles, BACK, the DIFFICULTY readout and PLAY GAME
    ReplayMenu      the replay list, its details column, BACK, COPY TO DESKTOP, DELETE, WATCH REPLAY
    SaveLoad        Load Game: the save list, its details column, BACK, DELETE, LOAD GAME, and the
                    save description's question box (unused while the screen only loads)

The two boxes these screens share their rows with, PopupReplay and PopupSaveLoad, are box masters
and live in boxes_layout.py with the rest of the boxes.  The code draws every line, frame, row,
word and picture (ChallengeMenu.cpp, ReplayMenu.cpp, PopupSaveLoad.cpp, SinglePlayerLayout.h holds
the numbers); these masters carry every window the code looks up at the rects the design draws,
the keys lifted out of EA's frames onto the screen's own parent, and EA's art windows hidden so a
lookup still finds them.

    python bigfile.py extract ../../Run/WindowZH.big "*Menus?ChallengeMenu.wnd" -o wnd   (and so on)
    python singleplayer_layout.py wnd/Window/Menus ../Data/Window/Menus

writes the tracked masters, reading a layout from the master when one is tracked and from the
extracted folder when not; the build copies them to Run/Window/Menus/, where a loose file beats
WindowZH.big.  Running it on its own output changes nothing.

    python singleplayer_layout.py selfcheck

reads the masters back and checks that every window the code looks up is there, that every window
is authored at 1920x1080, that the keys, lists and boxes stand on SinglePlayerLayout.h's numbers,
that the script reproduces each master byte for byte and that Patch.str carries the family's keys.
Run by CTest as singleplayer_selfcheck.  Layouts are not in the multiplayer INI checksum.

    python singleplayer_layout.py places <out.tga>

draws the places map's texture (Data/Art/Textures/SPPlacesMap.tga): land as a dot matrix with its
coast one lit line, meridians and parallels faint, Mercator from 62N to 8N over 128W to 128E at the
design's 1216x340, in a 2048x512 sheet.  It needs numpy, scipy, Pillow and datashader's world raster
(the design's own source); the selfcheck needs none of them.
"""

import os
import re
import sys

import wndlayout
from boxes_layout import desc_rects

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")
LAYOUT_H = os.path.join(HERE, "..", "GameEngine", "Include", "GameClient", "SinglePlayerLayout.h")

BOARD = (1920, 1080)
FULL = (0, 0) + BOARD


def header_rects():
    """Every `const IRegion2D NAME = { { a, b }, { c, d } };` in SinglePlayerLayout.h, with the
    constants it names (KEYS_TOP and the like) read from the same file."""
    with open(LAYOUT_H, "r", encoding="utf-8") as fp:
        text = fp.read()

    def value(expression, names):
        # the header's integer arithmetic: + - * and C's / on names already read
        return int(eval(expression.replace("/", "//"), {"__builtins__": {}}, names))
    ints = {}
    # the namespace's own constants, one tab in; a function's locals stand deeper
    for name, expression in re.findall(r"^\tconst Int (\w+) = ([^;]+);", text, re.M):
        ints[name] = value(expression, ints)
    rects = {}
    for name, a, b, c, d in re.findall(
            r"^\tconst IRegion2D (\w+) = \{ \{ ([^,{}]+), ([^,{}]+) \}, \{ ([^,{}]+), ([^,{}]+) \} \};", text, re.M):
        rects[name] = tuple(value(e, ints) for e in (a, b, c, d))
    return rects, ints


def put(window, rect):
    left, top, right, bottom = rect
    window.rect = (left, top, right, bottom, BOARD[0], BOARD[1])


def hide(window, at=(0, 0)):
    put(window, (at[0], at[1], at[0] + 1, at[1] + 1))
    flags = window.prop("STATUS").split("=", 1)[1].strip().rstrip(";").strip()
    if "HIDDEN" not in flags.split("+"):
        window.set_prop("STATUS", flags + "+HIDDEN")


def short(window):
    return (window.name or "").split(":")[-1]


def lift(root, names, to):
    """Move the named windows to be children of `to`, in this order, after its own children.  The
    rects in a .wnd are absolute, so nothing moves on screen."""
    for name in names:
        window = root.find(name)
        parent = root.parent_of(window)
        if parent is not to:
            parent.children.remove(window)
            to.children.append(window)


def place_tree(root, rects, keep_hidden=()):
    """Every window under root: to its rect when the table names it, else hidden at the corner.
    Windows in keep_hidden take their rect and stay hidden (code shows them)."""
    for window in list(root.walk())[1:]:
        name = short(window)
        if name in rects:
            put(window, rects[name])
            if name in keep_hidden:
                hide_flag(window)
        else:
            hide(window)


def hide_flag(window):
    flags = window.prop("STATUS").split("=", 1)[1].strip().rstrip(";").strip()
    if "HIDDEN" not in flags.split("+"):
        window.set_prop("STATUS", flags + "+HIDDEN")


def set_text(window, key):
    if window.prop("TEXT") is not None:
        window.set_prop("TEXT", '"%s"' % key)


# ---------------------------------------------------------------------------------------------- the layouts
CARD_W, CARD_H, CARD_GAP, SIDE_GAP, CARDS_LEFT, CARDS_TOP = 184, 240, 10, 36, 66, 160
# the design's sides, as ChallengeMode.ini numbers its personas; the code places the cards again
# from each persona's own side, so this is only where they stand until it does
SIDE_PERSONAS = [[0, 3, 5], [2, 4, 7], [1, 6, 8]]


def card_rect(side, slot):
    x0 = CARDS_LEFT + side * (3 * CARD_W + 2 * CARD_GAP + SIDE_GAP) + slot * (CARD_W + CARD_GAP)
    return (x0, CARDS_TOP, x0 + CARD_W, CARDS_TOP + CARD_H)


def build_challenge(layout):
    rects, _ = header_rects()
    root = layout.root
    gadgets = layout.find("GadgetParent")
    put(root, FULL)
    lift(root, ["ButtonBack", "ButtonPlay", "GeneralsBioParent"] + ["GeneralPosition%d" % i for i in range(12)],
         gadgets)
    table = {"GadgetParent": FULL, "ButtonBack": rects["CHALLENGE_BACK"], "ButtonPlay": rects["CHALLENGE_PLAY"]}
    for side, personas in enumerate(SIDE_PERSONAS):
        for slot, persona in enumerate(personas):
            table["GeneralPosition%d" % persona] = card_rect(side, slot)
    place_tree(root, table)
    set_text(layout.find("ButtonBack"), "GUI:Back")


def build_replays(layout):
    rects, _ = header_rects()
    root = layout.root
    gadgets = layout.find("GadgetParent")
    put(root, FULL)
    keys = ["ListboxReplayFiles", "ButtonBack", "ButtonCopyReplay", "ButtonDeleteReplay", "ButtonLoadReplay"]
    lift(root, keys, gadgets)
    place_tree(root, {"GadgetParent": FULL, "ListboxReplayFiles": rects["REPLAY_LIST"],
                      "ButtonBack": rects["REPLAY_BACK"], "ButtonCopyReplay": rects["REPLAY_COPY"],
                      "ButtonDeleteReplay": rects["REPLAY_DELETE"], "ButtonLoadReplay": rects["REPLAY_WATCH"]})
    set_text(layout.find("ButtonBack"), "GUI:Back")
    set_text(layout.find("ButtonCopyReplay"), "GUI:SPCopyToDesktop")
    set_text(layout.find("ButtonDeleteReplay"), "GUI:SPDelete")
    set_text(layout.find("ButtonLoadReplay"), "GUI:SPWatchReplay")


def build_saveload(layout):
    rects, _ = header_rects()
    root = layout.root
    menu = layout.find("SaveLoadMenu")
    put(root, FULL)
    lift(root, ["ListboxGames", "ButtonBack", "ButtonSave", "ButtonLoad", "ButtonDelete", "SaveDescParent"], menu)
    _, desc = desc_rects(False)
    table = {"SaveLoadMenu": FULL, "ListboxGames": rects["SAVE_LIST"], "ButtonBack": rects["SAVE_BACK"],
             "ButtonDelete": rects["SAVE_DELETE"], "ButtonLoad": rects["SAVE_LOAD"]}
    table.update(desc)
    # the confirm parents stay, hidden: Load, Overwrite and Delete ask through the message box now
    place_tree(root, table, keep_hidden=("SaveDescParent",))
    set_text(layout.find("ButtonDelete"), "GUI:SPDelete")
    set_text(layout.find("ButtonSaveDescCancel"), "GUI:Cancel")


# name: (builder, windows the code looks up)
SAVELOAD_LOOKUPS = ["SaveLoadMenu", "MenuButtonFrame", "ButtonBack", "ButtonSave", "ButtonLoad", "ButtonDelete",
                    "ListboxGames", "LoadConfirmParent", "OverwriteConfirmParent", "SaveDescParent",
                    "DeleteConfirmParent", "EntryDesc", "ButtonLoadConfirm", "ButtonLoadCancel",
                    "ButtonOverwriteConfirm", "ButtonOverwriteCancel", "ButtonSaveDescConfirm",
                    "ButtonSaveDescCancel", "ButtonDeleteConfirm", "ButtonDeleteCancel"]
LAYOUTS = {
    "ChallengeMenu": (build_challenge, ["ParentChallengeMenu", "GadgetParent", "ButtonPlay", "ButtonBack",
                                        "GeneralsBioParent", "BioPortrait", "BioNameEntry", "BioDOBEntry",
                                        "BioBirthplaceEntry", "BioStrategyEntry"]
                      + ["GeneralPosition%d" % i for i in range(12)]),
    "ReplayMenu": (build_replays, ["ParentReplayMenu", "GadgetParent", "ListboxReplayFiles", "ButtonBack",
                                   "ButtonCopyReplay", "ButtonDeleteReplay", "ButtonLoadReplay"]),
    "SaveLoad": (build_saveload, ["SaveLoadParent"] + SAVELOAD_LOOKUPS),
}

# where the selfcheck holds each master to the header: (layout, window, SinglePlayerLayout.h rect)
HELD = [("ChallengeMenu", "ButtonBack", "CHALLENGE_BACK"), ("ChallengeMenu", "ButtonPlay", "CHALLENGE_PLAY"),
        ("ReplayMenu", "ListboxReplayFiles", "REPLAY_LIST"), ("ReplayMenu", "ButtonBack", "REPLAY_BACK"),
        ("ReplayMenu", "ButtonCopyReplay", "REPLAY_COPY"), ("ReplayMenu", "ButtonDeleteReplay", "REPLAY_DELETE"),
        ("ReplayMenu", "ButtonLoadReplay", "REPLAY_WATCH"), ("SaveLoad", "ListboxGames", "SAVE_LIST"),
        ("SaveLoad", "ButtonBack", "SAVE_BACK"), ("SaveLoad", "ButtonDelete", "SAVE_DELETE"),
        ("SaveLoad", "ButtonLoad", "SAVE_LOAD")]

# keys the family's code fetches; Patch.str has to carry each
KEYS = ["GUI:SPChallengeTitle", "GUI:SPChooseGeneral", "GUI:SPSideUSA", "GUI:SPSideChina", "GUI:SPSideGLA",
        "GUI:SPTactics", "GUI:SPStationed", "GUI:SPTheChallenge", "GUI:SPBattles", "GUI:SPLocationUnknown",
        "GUI:SPYourGeneral", "GUI:SPThisGeneral", "GUI:SPBattleInOrder", "GUI:SPDifficulty", "GUI:SPPickedOnMenu",
        "GUI:SPPickedOnMenu_Short", "GUI:SPFinalBattle", "GUI:SPFinalBattle_Short", "GUI:SPArmyAirForce",
        "GUI:SPArmyAirForce_Short", "GUI:SPArmyToxin", "GUI:SPArmyNuke", "GUI:SPArmySuperweapon", "GUI:SPArmyTank",
        "GUI:SPArmyLaser", "GUI:SPArmyStealth", "GUI:SPArmyInfantry", "GUI:SPArmyDemolition",
        "GUI:SPReplays", "GUI:SPReplayCount", "GUI:SPName", "GUI:SPRecorded", "GUI:SPMap",
        "GUI:SPPlayers", "GUI:SPLength", "GUI:SPVersion", "GUI:SPOlder", "GUI:SPOlderLine", "GUI:SPSpeed",
        "GUI:SPGame", "GUI:SPEnded", "GUI:SPThisBuild", "GUI:SPOlderBuild", "GUI:SPMultiplayer",
        "GUI:SPSinglePlayer", "GUI:SPPlayedOut", "GUI:SPQuitEarly", "GUI:SPOutOfSync", "GUI:SPLeft", "GUI:SPTeam",
        "GUI:SPCopyToDesktop", "GUI:SPCopyToDesktop_Short", "GUI:SPDelete", "GUI:SPWatchReplay",
        "GUI:SPWatchReplay_Short", "GUI:SPSaveCount", "GUI:SPCampaign",
        "GUI:SPMission", "GUI:SPKind", "GUI:SPSaved", "GUI:SPKindSaved", "GUI:SPKindMissionStart",
        "GUI:SPLegendMissionStart", "GUI:SPLegendOlder", "GUI:SPDone", "GUI:SPThisSave", "GUI:SPAhead",
        "GUI:SPMissionOf", "GUI:SPSavedByYou", "GUI:SPLoads", "GUI:SPLoadsYes", "GUI:SPLoadsNo", "GUI:SPPlayedFor",
        "GUI:SPFile", "GUI:SPCampaignZH", "GUI:SPCampaignGenerals", "GUI:SPCampaignChallenge", "GUI:SPSkirmish",
        "GUI:SPWontLoad", "GUI:SPNoPreview", "GUI:SPNewSave", "GUI:SPSaveLoad", "GUI:SPHintEscEnterSaves"]


def build(layout, menu):
    LAYOUTS[menu][0](layout)
    return layout


def read_strings():
    keys = set()
    with open(PATCH_STR, "r", encoding="utf-8") as fp:
        for line in fp:
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def selfcheck():
    problems = []
    rects, ints = header_rects()
    masters = {}
    for menu, (_, looked_up) in sorted(LAYOUTS.items()):
        path = os.path.join(MENUS, menu + ".wnd")
        if not os.path.exists(path):
            problems.append("%s.wnd is not tracked under Data/Window/Menus" % menu)
            continue
        with open(path, "rb") as fp:
            text = fp.read().decode("latin-1")
        layout = wndlayout.parse(text)
        masters[menu] = layout
        before = len(problems)
        for name in looked_up:
            if layout.find(name) is None:
                problems.append("%s.wnd has no %s" % (menu, name))
        stale = [w.name for w in layout.root.walk() if tuple(w.rect[4:]) != BOARD]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first" % (menu, len(stale), stale[0]))
        if len(problems) == before and build(wndlayout.parse(text), menu).text() != text:
            problems.append("%s.wnd is not what this script writes from it" % menu)
    for menu, window, rect in HELD:
        if menu in masters:
            w = masters[menu].find(window)
            if w is not None and tuple(w.rect[:4]) != rects[rect]:
                problems.append("%s.wnd: %s at %s, SinglePlayerLayout.h %s says %s"
                                % (menu, window, w.rect[:4], rect, rects[rect]))
    # the nine cards the design draws: three sides of three, inside the panel, none over another
    if "ChallengeMenu" in masters:
        seen = []
        for personas in SIDE_PERSONAS:
            for persona in personas:
                r = masters["ChallengeMenu"].find("GeneralPosition%d" % persona).rect[:4]
                if r[2] - r[0] != ints["CARD_WIDTH"] or r[3] - r[1] != ints["CARD_HEIGHT"]:
                    problems.append("ChallengeMenu.wnd: GeneralPosition%d is not a card's size" % persona)
                if any(r[0] < s[2] and s[0] < r[2] and r[1] < s[3] and s[1] < r[3] for s in seen):
                    problems.append("ChallengeMenu.wnd: GeneralPosition%d stands over another card" % persona)
                if r[2] > ints["RIGHT"]:
                    problems.append("ChallengeMenu.wnd: GeneralPosition%d runs past the content" % persona)
                seen.append(r)
    # every key on the foot row, inside the content, and the one primary at the right edge
    for menu, keys, primary in (("ChallengeMenu", ["ButtonBack", "ButtonPlay"], "ButtonPlay"),
                                ("ReplayMenu", ["ButtonBack", "ButtonCopyReplay", "ButtonDeleteReplay",
                                                "ButtonLoadReplay"], "ButtonLoadReplay"),
                                ("SaveLoad", ["ButtonBack", "ButtonDelete", "ButtonLoad"], "ButtonLoad")):
        if menu not in masters:
            continue
        for name in keys:
            r = masters[menu].find(name).rect[:4]
            if (r[1], r[3]) != (ints["KEYS_TOP"], ints["KEYS_BOTTOM"]) or r[0] < ints["LEFT"] or r[2] > ints["RIGHT"]:
                problems.append("%s.wnd: %s is off the key row" % (menu, name))
        if masters[menu].find(primary).rect[2] != ints["RIGHT"]:
            problems.append("%s.wnd: the primary key %s does not end at the content's right" % (menu, primary))
    # the list boxes hold whole rows
    for name, rows, height in (("REPLAY_LIST", "REPLAY_ROWS", "REPLAY_ROW_HEIGHT"),
                               ("SAVE_LIST", "SAVE_ROWS", "SAVE_ROW_HEIGHT")):
        r = rects[name]
        if r[3] - r[1] != ints[rows] * ints[height]:
            problems.append("%s holds %d px, not %s rows of %s" % (name, r[3] - r[1], rows, height))
    keys = read_strings()
    for key in KEYS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)

    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1
    print("single player: %d layouts, %d strings" % (len(LAYOUTS), len(KEYS)))
    return 0


# ---------------------------------------------------------------------------------------------- the places map
WORLD = r"C:\Users\user\anaconda3\Lib\site-packages\datashader\tests\data\world.rgb.tif"
PLACES_W, PLACES_H = 1216, 340
SHEET = (2048, 512)


def places(out):
    """The design's world() at 1216x340 in the top left of a 2048x512 sheet."""
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter
    from scipy.ndimage import binary_erosion, gaussian_filter

    lon0, lon1, lat_top, lat_bot = -128.0, 128.0, 62.0, 8.0
    merc = lambda lat: np.log(np.tan(np.pi / 4 + np.radians(lat) / 2))       # noqa: E731
    m0, m1 = merc(lat_top), merc(lat_bot)
    w, h = PLACES_W, PLACES_H
    proj = lambda lon, lat: ((lon - lon0) / (lon1 - lon0) * w, (m0 - merc(lat)) / (m0 - m1) * h)  # noqa: E731
    frame = (47, 55, 168)
    lay = Image.new("RGBA", (w, h), (6, 10, 20, 150))
    d = ImageDraw.Draw(lay)
    for lon in range(-120, 129, 30):
        x = proj(lon, 0)[0]
        d.line((x, 0, x, h), fill=frame + (52,))
    for lat in range(15, 61, 15):
        y = proj(0, lat)[1]
        d.line((0, y, w, y), fill=frame + (52,))
    src = np.asarray(Image.open(WORLD).convert("RGB").resize((2048, 1024), Image.BILINEAR)).astype(np.int16)
    r, g, b = src[..., 0], src[..., 1], src[..., 2]
    land = gaussian_filter((~((b > r + 25) & (b >= g - 12))).astype(np.float32), 1.6) > 0.5
    ys = np.arange(h)
    lat = np.degrees(2 * np.arctan(np.exp(m0 - (ys + 0.5) / h * (m0 - m1))) - np.pi / 2)
    lon = lon0 + (np.arange(w) + 0.5) / w * (lon1 - lon0)
    rows = np.clip(((90 - lat) / 180 * 1024).astype(int), 0, 1023)
    cols = np.clip(((lon + 180) / 360 * 2048).astype(int), 0, 2047)
    mask = land[rows][:, cols]
    dots = np.zeros((h, w), bool)
    dots[3::6, 3::6] = True
    a = np.zeros((h, w, 4), np.uint8)
    a[mask & dots] = (118, 134, 222, 210)
    a[mask & ~binary_erosion(mask)] = (70, 92, 235, 230)
    layer = Image.fromarray(a, "RGBA")
    lay.alpha_composite(layer.filter(ImageFilter.GaussianBlur(2)))
    lay.alpha_composite(layer)
    sheet = Image.new("RGBA", SHEET, (0, 0, 0, 0))
    sheet.paste(lay, (0, 0))
    sheet.save(out, compression="tga_rle")
    print("%s: %dx%d sheet, the map at 0,0 to %d,%d" % (out, SHEET[0], SHEET[1], w, h))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) == 3 and argv[1] == "places":
        return places(argv[2])
    if len(argv) != 3:
        print(__doc__)
        return 2
    indir, outdir = argv[1], argv[2]
    for menu in sorted(LAYOUTS):
        source = os.path.join(outdir, menu + ".wnd")
        if not os.path.exists(source):
            source = os.path.join(indir, menu + ".wnd")
        layout = build(wndlayout.load(source), menu)
        out = os.path.join(outdir, menu + ".wnd")
        wndlayout.save(layout, out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
