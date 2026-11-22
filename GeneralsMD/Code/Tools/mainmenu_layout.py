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

Last, the single player menu itself gains ButtonDirectStrike and ButtonApocalypse under Skirmish,
copies of Skirmish one and two steps down with Back and the frame pushed down after them;
GameWindowTransitions.cpp copies Skirmish's flash into every transition group for each.

    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\MainMenu.wnd" -o wnd
    python mainmenu_layout.py wnd/Window/Menus ../Data/Window/Menus

The output is a tracked master under Code/Data; the build copies it to Run/Window/Menus/, where a
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

# the single player menu's Direct Strike button, a copy of Skirmish one step under it
DIRECT_STRIKE = "ButtonDirectStrike"
DIRECT_STRIKE_LABEL = "GUI:DirectStrike"
DIRECT_STRIKE_TOOLTIP = "TOOLTIP:DirectStrike"
# and the Apocalypse button a step under that
APOCALYPSE = "ButtonApocalypse"
APOCALYPSE_LABEL = "GUI:Apocalypse"
APOCALYPSE_TOOLTIP = "TOOLTIP:Apocalypse"
# the mode buttons under Skirmish, top first
MODE_BUTTONS = [(DIRECT_STRIKE, DIRECT_STRIKE_LABEL, DIRECT_STRIKE_TOOLTIP),
                (APOCALYPSE, APOCALYPSE_LABEL, APOCALYPSE_TOOLTIP)]

STRINGS = ["GUI:CampaignMission", "GUI:CampaignZeroHour", "GUI:CampaignGenerals",
           DIRECT_STRIKE_LABEL, DIRECT_STRIKE_TOOLTIP, APOCALYPSE_LABEL, APOCALYPSE_TOOLTIP]


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
    return layout


def read_strings():
    keys = set()
    with open(PATCH_STR, "r", encoding="utf-8") as fp:
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
    for name in sorted(wanted - names):
        problems.append("%s.wnd has no %s" % (MENU, name))

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
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
