#!/usr/bin/env python3
"""Add the campaign pick and campaign mission panels to the main menu layout.

Picking USA, GLA or China in the single player menu opens a panel with a button for each of that
campaign's missions, so a campaign can be started at any mission; the difficulty panel follows the
pick as it always did.  The panel is the single player menu's own (MapBorder: six buttons in a
frame) cloned whole and renamed, which is how it keeps EA's frame, button art, font and sounds, and
then grown by two buttons, since the original 2003 campaigns run to seven missions:

    MapBorder5 / EarthMap5
        ButtonMission1 .. ButtonMission7     the campaign's missions, top to bottom
        ButtonMissionBack                    back to the pick panel or the single player menu

MainMenu.cpp shrinks the frame to the campaign it shows and moves Back up under its last mission.

When the side's original Generals campaign is installed, a pick panel comes first, the multiplayer
menu's (MapBorder1: two buttons and Back) cloned the same way:

    MapBorder6 / EarthMap6
        ButtonCampaignGenerals               the original 2003 campaign
        ButtonCampaignZH                     Zero Hour's campaign
        ButtonCampaignBack                   back to the single player menu

It is PICK_WIDEN wider than the other panels, to the left, so "GENERALS ZERO HOUR" fits its button
at 800x600 (211 pixels of Arial at 15 points against 208); the right edges stay in line.  The
recent-save buttons under the extra strip are never shown.

Both sit where the single player menu does, which is hidden while either shows.  The labels
are set from GUI:CampaignMission, GUI:CampaignZeroHour and GUI:CampaignGenerals; the transitions
that flash them in and out are in Data/INI/WindowTransitionsReforged.ini.

The single player menu itself gains ButtonMods under Skirmish, a copy of Skirmish one step down
with Back and the frame pushed down after it; GameWindowTransitions.cpp copies Skirmish's flash into
every transition group for it.

The menu redesign (docs/plans/MENU-REDESIGN.md, package 4) then turns the drop-downs into one rail on
the right with the panels it opens beside it.  MainMenu.cpp places every window in code at one scale
from the screen's right edge and draws them, so the rects this tool writes only have to hold the
windows; the transitions are no longer played.  For that the layout gains:

    MapBorder7 / EarthMap7                 the Mods panel, a clone of the multiplayer menu
        ButtonModeDirectStrike, ButtonModeApocalypse   the two mode cards
    ButtonOnline                           moved onto the main rail (EarthMap2); shown only in a
                                           game started with -online, which the launcher's ONLINE
                                           key does
    LabelVersion                           a static text for the version at the rail's top right

Multiplayer opens the LAN lobby straight, so the rest of the multiplayer menu (ButtonNetwork,
ButtonMultiBack) stays only for the transitions that name it.  Every button's TOOLTIPTEXT goes: the
mouse would draw it beside the card MainMenu.cpp draws, whose keys are TOOLTIPS below.

The credits screen comes out of the same run: EA's CreditsMenu.wnd with a Back key (a clone of the
single player menu's Back), which CreditsMenu.cpp places at the foot of the credits column.

    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\MainMenu.wnd" -o wnd
    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\CreditsMenu.wnd" -o wnd
    python mainmenu_layout.py wnd/Window/Menus ../Data/Window/Menus

The outputs are tracked masters under Code/Data; the build copies them to Run/Window/Menus/, where a
loose file beats WindowZH.big.

    python mainmenu_layout.py selfcheck

reads the tracked files back and checks they agree: the layout carries every window the code and
the Reforged transitions name, and Patch.str carries the labels.  Run by CTest as
mainmenu_selfcheck.
"""

import os
import re
import sys

import wndlayout
from wndlayout import clone


HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")
TURKISH_STR = os.path.join(HERE, "..", "Data", "Turkish", "Generals.str")
TRANSITIONS = os.path.join(HERE, "..", "Data", "INI", "WindowTransitionsReforged.ini")

MENU = "MainMenu"

# the single player menu's windows -> their names in the clone, top button first
MISSION_RENAME = [
    ("MapBorder", "MapBorder5"),
    ("EarthMap", "EarthMap5"),
    ("ButtonUSA", "ButtonMission1"),
    ("ButtonGLA", "ButtonMission2"),
    ("ButtonChina", "ButtonMission3"),
    ("ButtonChallenge", "ButtonMission4"),
    ("ButtonSkirmish", "ButtonMission5"),
    ("ButtonSingleBack", "ButtonMissionBack"),
]

# MISSION_BUTTONS in MainMenu.cpp; the clone brings five, the rest are copies of the fifth
MISSION_BUTTONS = 7

# the multiplayer menu's windows -> the pick panel's, with the label each button gets
PICK_RENAME = [
    ("MapBorder1", "MapBorder6", None),
    ("EarthMap1", "EarthMap6", None),
    ("ButtonOnline", "ButtonCampaignGenerals", "GUI:CampaignGenerals"),
    ("ButtonNetwork", "ButtonCampaignZH", "GUI:CampaignZeroHour"),
    ("ButtonMultiBack", "ButtonCampaignBack", None),
]

# 800x600 pixels the pick panel and its buttons grow by, leftwards
PICK_WIDEN = 32

# the single player menu's Mods button, a copy of Skirmish one step under it: it asks Direct Strike or
# Apocalypse and opens that mode's skirmish room (the owner, 2026-11-24, in place of a button a mode)
MODS = "ButtonMods"
MODS_LABEL = "GUI:Mods"
MODS_TOOLTIP = "TOOLTIP:Mods"
# the mode buttons under Skirmish, top first
MODE_BUTTONS = [(MODS, MODS_LABEL, MODS_TOOLTIP)]

# the Mods panel: the multiplayer menu cloned, its two first buttons the cards, Back dropped
MODS_PANEL_RENAME = [
    ("MapBorder1", "MapBorder7", None),
    ("EarthMap1", "EarthMap7", None),
    ("ButtonOnline", "ButtonModeDirectStrike", "GUI:GameModeDirectStrike"),
    ("ButtonNetwork", "ButtonModeApocalypse", "GUI:GameModeApocalypse"),
    ("ButtonMultiBack", "ButtonModeBack", None),
]

CREDITS = "CreditsMenu"
CREDITS_BACK = "ButtonBack"

# the card MainMenu.cpp draws beside a button (railTip there), one line each
TOOLTIPS = ["TOOLTIP:MainMenu" + key for key in (
    "Solo", "Multi", "Load", "Options", "Credits", "Exit", "Online", "USA", "GLA", "China", "Challenge",
    "Skirmish", "Mods", "LoadGame", "Replay", "Generals", "ZH", "Mission", "Easy", "Medium", "Hard",
    "Back")]

# the main menu's own words (MainMenu.cpp, CreditsMenu.cpp); EA's exit question is QuitMenu's too
MENU_STRINGS = ["GUI:MainMenuMission", "GUI:MainMenuDifficulty", "GUI:MainMenuExitTitle",
                "GUI:MainMenuExitMessage", "GUI:CreditsReforged", "GUI:CreditsLead", "GUI:CreditsPort",
                "GUI:HintEscBack"]

STRINGS = ["GUI:CampaignMission", "GUI:CampaignZeroHour", "GUI:CampaignGenerals", MODS_LABEL, MODS_TOOLTIP] \
    + TOOLTIPS + MENU_STRINGS


def drop_prop(window, key):
    """Remove a one-line statement, if the window carries it."""
    i = window.prop_index(key)
    if i >= 0:
        assert ";" in window.props[i], (window.name, key)
        del window.props[i]


def full(name):
    return "%s.wnd:%s" % (MENU, name)


def clone_panel(layout, renames):
    """Clone the frame the first rename names, renaming every window in it and relabelling the
    buttons given a label.  A window the list leaves out would keep a name the code already uses."""
    template = layout.find(renames[0][0])
    if template is None:
        raise SystemExit("%s has no %s to clone" % (MENU, renames[0][0]))

    panel = clone(template)
    for old, new, label in renames:
        window = panel.find(old)
        if window is None:
            raise SystemExit("%s's %s has no %s" % (MENU, renames[0][0], old))
        window.name = full(new)
        if label:
            window.set_prop("TEXT", '"%s"' % label)
            drop_prop(window, "TOOLTIPTEXT")

    leftover = [w.name for w in panel.walk() if w.name not in [full(new) for _, new, _ in renames]]
    if leftover:
        raise SystemExit("%s has windows this tool does not rename: %s" % (renames[0][0], leftover))
    return panel


def grow_missions(panel):
    """Add ButtonMission6 .. on the fifth's step below it, and push Back and the frame down."""
    earth = panel.find("EarthMap5")
    fifth = panel.find("ButtonMission5")
    step = fifth.rect[1] - panel.find("ButtonMission4").rect[1]
    extra = MISSION_BUTTONS - 5
    for n in range(6, MISSION_BUTTONS + 1):
        button = clone(fifth, full("ButtonMission%d" % n))
        button.move_by(0, step * (n - 5))
        earth.children.append(button)
    panel.find("ButtonMissionBack").move_by(0, step * extra)
    for frame in (panel, earth):
        l, t, r, b, cw, ch = frame.rect
        frame.rect = (l, t, r, b + step * extra, cw, ch)


def mission_names():
    return [new for _, new in MISSION_RENAME] + [
        "ButtonMission%d" % n for n in range(6, MISSION_BUTTONS + 1)]


def add_mode_buttons(layout):
    """Clone Skirmish a step below itself for each mode button, and push Back and the frame down as
    many steps.  Run after the mission panel is cloned, which must not carry the extra buttons."""
    earth = layout.find("EarthMap")
    skirmish = layout.find("ButtonSkirmish")
    step = skirmish.rect[1] - layout.find("ButtonChallenge").rect[1]
    for n, (name, label, tooltip) in enumerate(MODE_BUTTONS, 1):
        button = clone(skirmish, full(name))
        button.move_by(0, step * n)
        button.set_prop("TEXT", '"%s"' % label)
        button.set_prop("TOOLTIPTEXT", '"%s"' % tooltip)
        earth.children.append(button)
    grow = step * len(MODE_BUTTONS)
    layout.find("ButtonSingleBack").move_by(0, grow)
    for frame in (layout.find("MapBorder"), earth):
        l, t, r, b, cw, ch = frame.rect
        frame.rect = (l, t, r, b + grow, cw, ch)


def build(layout):
    label = "GUI:CampaignMission"
    missions = clone_panel(layout, [
        (old, new, label if new.startswith("ButtonMission") and new != "ButtonMissionBack" else None)
        for old, new in MISSION_RENAME])
    grow_missions(missions)
    layout.root.children.append(missions)
    pick = clone_panel(layout, PICK_RENAME)
    for window in pick.walk():
        l, t, r, b, cw, ch = window.rect
        window.rect = (l - PICK_WIDEN, t, r, b, cw, ch)
    layout.root.children.append(pick)
    add_mode_buttons(layout)
    add_rail_windows(layout)
    return layout


def add_rail_windows(layout):
    """The menu redesign's windows: the Mods panel, ButtonOnline on the main rail, LabelVersion; and
    no TOOLTIPTEXT anywhere, since the code draws the tooltips."""
    mods = clone_panel(layout, MODS_PANEL_RENAME)
    earth = mods.find("EarthMap7")
    earth.children.remove(mods.find("ButtonModeBack"))
    layout.root.children.append(mods)

    online = layout.find("ButtonOnline")
    layout.root.parent_of(online).children.remove(online)
    layout.find("EarthMap2").children.append(online)

    version = clone(layout.find("StaticTextSelectDifficulty"), full("LabelVersion"))
    version.set_prop("STATUS", "ENABLED")
    version.set_prop("TEXT", '""')
    layout.root.children.append(version)

    for window in layout.root.walk():
        drop_prop(window, "TOOLTIPTEXT")


def build_credits(layout, main):
    """EA's credits screen with a Back key, cloned from the main menu's single player Back."""
    back = clone(main.find("ButtonSingleBack"), "%s.wnd:%s" % (CREDITS, CREDITS_BACK))
    drop_prop(back, "TOOLTIPTEXT")
    layout.root.children.append(back)
    return layout


def read_strings(path=PATCH_STR):
    keys = set()
    with open(path, "r", encoding="utf-8") as fp:
        for line in fp:
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def transition_windows():
    with open(TRANSITIONS, "r") as fp:
        return set(re.findall(r"WinName\s*=\s*%s\.wnd:(\w+)" % MENU, fp.read()))


def selfcheck():
    problems = []

    keys = read_strings()
    for key in STRINGS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)

    path = os.path.join(MENUS, MENU + ".wnd")
    names = set()
    if not os.path.exists(path):
        problems.append("%s.wnd is not tracked under Data/Window/Menus" % MENU)
    else:
        layout = wndlayout.load(path)
        names = set((w.name or "").split(":")[-1] for w in layout.root.walk())

    wanted = set(mission_names()) | set(new for _, new, _ in PICK_RENAME) | transition_windows() | {name for name, _, _ in MODE_BUTTONS}
    wanted |= set(new for _, new, _ in MODS_PANEL_RENAME if new != "ButtonModeBack") | {"LabelVersion", "ButtonOnline"}
    for name in sorted(wanted - names):
        problems.append("%s.wnd has no %s" % (MENU, name))
    if os.path.exists(path):
        if layout.find("ButtonOnline") not in layout.find("EarthMap2").children:
            problems.append("ButtonOnline is not on the main rail (EarthMap2)")
        tipped = [w.name for w in layout.root.walk() if w.prop("TOOLTIPTEXT")]
        if tipped:
            problems.append("TOOLTIPTEXT left on %s: the code draws the main menu's tooltips" % tipped)

    credits = os.path.join(MENUS, CREDITS + ".wnd")
    if not os.path.exists(credits):
        problems.append("%s.wnd is not tracked under Data/Window/Menus" % CREDITS)
    else:
        for name in ("ParentCreditsWindow", "BackgroundWindow", "WinTextDraw", CREDITS_BACK):
            if wndlayout.load(credits).find(name) is None:
                problems.append("%s.wnd has no %s" % (CREDITS, name))

    turkish = read_strings(TURKISH_STR)
    for key in TOOLTIPS + MENU_STRINGS:
        if key not in turkish:
            problems.append("%s is not in Data/Turkish/Generals.str" % key)

    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1

    print("main menu: %d windows checked, %d strings" % (len(wanted), len(STRINGS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()

    if len(argv) != 3:
        print(__doc__)
        return 2

    indir, outdir = argv[1], argv[2]
    layout = build(wndlayout.load(os.path.join(indir, MENU + ".wnd")))
    out = os.path.join(outdir, MENU + ".wnd")
    wndlayout.save(layout, out)
    print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    credits = build_credits(wndlayout.load(os.path.join(indir, CREDITS + ".wnd")), layout)
    out = os.path.join(outdir, CREDITS + ".wnd")
    wndlayout.save(credits, out)
    print("%s: %d windows" % (out, sum(1 for _ in credits.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
