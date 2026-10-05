#!/usr/bin/env python3
"""Write single player Apocalypse's room from the skirmish room.

Apocalypse alone is one human against the zombies: no computer seats, no teams. The skirmish room
is an eight seat table with a team column, career stats and a battle honours list, and none of that
means anything here. So this room is a form for one player on the left and the map on the right.

    form (the settings page, its border kept)      map preview
      Player Name   [ name entry              ]    [                 ]
      Army          [ ComboBoxPlayerTemplate0 ]    [   MapWindow     ]
      Color         [ ComboBoxColor0          ]    [                 ]
                                                   [                 ]
      Zombie Flow   [ Waves / Continuous      ]    map name
      Starting Cash, Superweapons, Tech Respawn,   [ SELECT MAP      ]
      Pile Limit, Game Speed
      [x] Unit Limit
    BACK                                           PLAY GAME

The menu code is the skirmish room's own (SkirmishGameOptionsMenu.cpp), so every window keeps its
"SkirmishGameOptionsMenu.wnd:" name, and the windows that code looks up and this room has no use for
(seats one to seven, the team column, the column headings, the stats reset button) are moved into one
hidden window. The code may show any of them again, as the map list does when it closes, and they
still stay out of sight. What no code needs is left out: the honours list, the stats panel, the tab
strip and income sharing, which with one player shares nothing. The settings page becomes the form:
the player's own seat controls move onto it, so the map list hides the whole form by hiding the page,
which is what lies under it.

The form is the room grid's (lobbyroom_layout.py): rows 24 high on a 32 pitch, 24 more between the
three groups, label and control columns as on the settings pages. Every control keeps EA's art; only
rectangles move, and the four new labels and the mode box are clones of Game Speed's label and of
starting cash.

    python apocalypseroom_layout.py <SkirmishGameOptionsMenu.wnd in> <ApocalypseGameOptionsMenu.wnd out>

The input is the tracked skirmish master, lobbyroom_layout.py's output.

    python apocalypseroom_layout.py selfcheck

rebuilds the room from the tracked skirmish master and wants the tracked Apocalypse master back byte
for byte, then checks every window the menu code names is there, no two visible controls overlap,
the form stays inside its page and Patch.str carries the new labels. Run by CTest as
apocalypseroom_selfcheck. Layouts are not in the multiplayer INI checksum.
"""

import os
import re
import sys

import lobbyroom_layout as room
import wndlayout

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
SOURCE = os.path.join(MENUS, "SkirmishGameOptionsMenu.wnd")
TARGET = os.path.join(MENUS, "ApocalypseGameOptionsMenu.wnd")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")
PREFIX = "SkirmishGameOptionsMenu.wnd:"

FORM_LEFT, FORM_TOP, FORM_RIGHT = room.INNER_LEFT, room.HEADER_TOP, 408
COLUMN_BOTTOM = room.PAGE_BOTTOM
LABEL_WIDTH, CONTROL_WIDTH = 112, 208
ROW_PITCH, GROUP_GAP = 32, 24
MAP_LEFT = FORM_RIGHT + 16
MAP_WIDTH = room.INNER_RIGHT - MAP_LEFT

# (label, control); a label is an existing window or a (new name, string label) clone of Game Speed's
GROUPS = [
    [(("LabelPlayerName", "GUI:PlayerName"), "TextEntryPlayerName"),
     (("LabelArmy", "GUI:Faction"), "ComboBoxPlayerTemplate0"),
     (("LabelColor", "GUI:Color"), "ComboBoxColor0")],
    [(("LabelGameMode", "GUI:ApocalypseFlow"), "ComboBoxGameMode"),
     ("StartingCashLabel", "ComboBoxStartingCash"),
     ("LabelSuperweapons", "ComboBoxSuperweapons"),
     ("LabelTechRespawn", "ComboBoxTechRespawn"),
     ("LabelSupplyPileLimit", "ComboBoxSupplyPileLimit"),
     ("LabelGameSpeed", "ComboBoxGameSpeed")],
    [("CheckBoxUnitLimit",)],
]
LABEL_MODEL = "LabelGameSpeed"
GAME_MODE_MODEL = "ComboBoxStartingCash"
# the mode box's entries are short here ("Waves"), but its look is the LAN room's box
GAME_MODE_DATA = room.GAME_MODE_DATA

# looked up by the menu code or by the map list's, and of no use in this room
HIDDEN = (["StaticTextPlayers", "StaticTextColor", "StaticTextFaction", "StaticTextTeam",
           "StaticTextMapPreview", "ButtonReset"]
          + ["ComboBoxPlayer%d" % seat for seat in range(1, room.SEAT_COUNT)]
          + ["ComboBox%s%d" % (column, seat) for column in ("Color", "PlayerTemplate")
             for seat in range(1, room.SEAT_COUNT)]
          + ["ComboBoxTeam%d" % seat for seat in range(room.SEAT_COUNT)])
HOLDER = "ApocalypseUnused"
DROPPED = ["ListboxInfo", "TabInfo", "TabLobbySettings", "ButtonResetFPS",
           "LabelIncomeSharing", "ComboBoxIncomeSharing"]

# every window SkirmishGameOptionsMenu.cpp and SkirmishMapSelectMenu.cpp look up without a null check
CODE_NAMES = (["SkirmishGameOptionsMenuParent", "SubParent", "StaticTextTitle", "ButtonBack", "ButtonStart",
               "ButtonSelectMap", "ButtonReset", "MapWindow", "TextEntryMapDisplay", "TextEntryPlayerName",
               "ComboBoxStartingCash", "ComboBoxGameSpeed", "ComboBoxGameMode", "PageLobbySettings",
               "StaticTextTeam", "StaticTextFaction", "StaticTextColor", "StaticTextMapPreview"]
              + ["ComboBox%s%d" % (column, seat) for column in ("Color", "PlayerTemplate", "Team")
                 for seat in range(room.SEAT_COUNT)]
              + ["ComboBoxPlayer%d" % seat for seat in range(1, room.SEAT_COUNT)]
              + ["ButtonMapStartPosition%d" % seat for seat in range(room.SEAT_COUNT)])
# the fork's own labels this room shows; Player Name, Army and Color are EA's
STRINGS = ["GUI:Apocalypse", "GUI:ApocalypseFlow", "GUI:ApocalypseFlowWaves", "GUI:ApocalypseFlowContinuous"]


def take(layout, name):
    """Lift a window out of wherever it is; its rectangle is absolute, so it can go anywhere."""
    window = room.need(layout, name)
    layout.root.parent_of(window).children.remove(window)
    return window


def clone_label(layout, name, text):
    label = layout.find(name)
    if label is None:
        label = wndlayout.clone(room.need(layout, LABEL_MODEL), PREFIX + name)
        label.set_prop("TEXT", '"%s"' % text)
    return label


def clone_game_mode(layout):
    mode = layout.find(room.GAME_MODE)
    if mode is None:
        model = room.need(layout, GAME_MODE_MODEL)
        mode = wndlayout.clone(model, PREFIX + room.GAME_MODE)
        mode.children = []
        # starting cash's tooltip would explain the wrong thing, and the entries say what they are
        del mode.props[mode.prop_index("TOOLTIPTEXT")]
        mode.set_prop("COMBOBOXDATA", GAME_MODE_DATA)
    return mode


def build(layout):
    sub_parent = room.need(layout, "SubParent")
    page = room.need(layout, "PageLobbySettings")
    room.need(layout, "StaticTextTitle").set_prop("TEXT", '"GUI:Apocalypse"')

    info_frame, stats_panel = room.skirmish_panels(sub_parent)
    sub_parent.children.remove(info_frame)
    sub_parent.children.remove(stats_panel)
    for name in DROPPED:
        take(layout, name)

    holder = wndlayout.clone(info_frame, PREFIX + HOLDER)
    holder.children = [take(layout, name) for name in HIDDEN]
    holder.set_prop("STATUS", "ENABLED+HIDDEN")
    holder.place(room.FRAME[0], room.FRAME[1], room.FRAME[2], room.FRAME[3])
    sub_parent.children.append(holder)

    # the form: the page gets the player's own seat controls and the new labels, in reading order
    rows = []
    for group in GROUPS:
        for row in group:
            names = []
            for item in row:
                if isinstance(item, tuple):
                    page_window = clone_label(layout, *item)
                elif item == room.GAME_MODE:
                    page_window = clone_game_mode(layout)
                else:
                    page_window = take(layout, item)
                names.append(room.short(page_window))
                page.children.append(page_window)
            rows.append(tuple(names))
    page.place(FORM_LEFT, FORM_TOP, FORM_RIGHT - FORM_LEFT, COLUMN_BOTTOM - FORM_TOP)

    content = sum(len(group) for group in GROUPS) * ROW_PITCH - (ROW_PITCH - room.ROW_HEIGHT) \
        + (len(GROUPS) - 1) * GROUP_GAP
    top = FORM_TOP + (COLUMN_BOTTOM - FORM_TOP - content) // 2
    at = 0
    for group in GROUPS:
        group_rows = rows[at:at + len(group)]
        room.place_settings(layout, [(LABEL_WIDTH, CONTROL_WIDTH, group_rows)], FORM_LEFT, FORM_RIGHT,
                            top, COLUMN_BOTTOM, ROW_PITCH, 0)
        top += len(group) * ROW_PITCH + GROUP_GAP
        at += len(group)

    # the map column: the preview as tall as the form leaves room for, its name, the button
    button_top = COLUMN_BOTTOM - room.BUTTON_HEIGHT
    name_top = button_top - 8 - room.ROW_HEIGHT
    room.need(layout, "MapWindow").place(MAP_LEFT, FORM_TOP, MAP_WIDTH, name_top - 8 - FORM_TOP)
    room.need(layout, "TextEntryMapDisplay").place(MAP_LEFT, name_top, MAP_WIDTH, room.ROW_HEIGHT)
    room.need(layout, "ButtonSelectMap").place(MAP_LEFT, button_top, MAP_WIDTH, room.BUTTON_HEIGHT)
    return layout


# ---------------------------------------------------------------------------
# selfcheck
# ---------------------------------------------------------------------------

def read_strings():
    keys = set()
    with open(PATCH_STR, "r", encoding="utf-8") as fp:
        for line in fp:
            line = line.strip()
            if re.match(r"^[A-Za-z]+:[A-Za-z0-9_]+$", line):
                keys.add(line)
    return keys


def visible(container):
    return [child for child in container.children
            if room.short(child) and "HIDDEN" not in (child.prop("STATUS") or "")]


def selfcheck():
    problems = []
    if not os.path.exists(TARGET):
        print("apocalypseroom: %s is not tracked" % TARGET)
        return 1
    with open(TARGET, "rb") as fp:
        tracked = fp.read().decode("latin-1")
    try:
        built = build(wndlayout.load(SOURCE)).text()
        if built != tracked:
            problems.append("ApocalypseGameOptionsMenu.wnd is not what the skirmish master builds; "
                            "re-run this script")
    except (KeyError, ValueError) as error:
        problems.append("the skirmish master no longer builds: %s" % error)

    layout = wndlayout.load(TARGET)
    for name in CODE_NAMES:
        if layout.find(name) is None:
            problems.append("no %s" % name)
    holder = layout.find(HOLDER)
    if holder is None or "HIDDEN" not in (holder.prop("STATUS") or ""):
        problems.append("%s is missing or shown" % HOLDER)
    for container in ("SubParent", "PageLobbySettings"):
        problems.extend(room.overlaps(room.need(layout, container), None))

    page = room.need(layout, "PageLobbySettings")
    left, top, right, bottom = page.rect[:4]
    for child in visible(page):
        l, t, r, b = child.rect[:4]
        if l < left or t < top or r > right or b > bottom:
            problems.append("%s runs off the form" % room.short(child))
    for name in ("TextEntryPlayerName", "ComboBoxPlayerTemplate0", "ComboBoxColor0", room.GAME_MODE):
        if layout.find(name) not in page.children:
            problems.append("%s is not on the form, so the map list would not hide it" % name)

    keys = read_strings()
    problems.extend("%s is not in Patch.str" % key for key in STRINGS if key not in keys)

    for problem in problems:
        print("apocalypseroom: %s" % problem)
    if problems:
        return 1
    print("apocalypseroom: %d windows, %d on the form" % (sum(1 for _ in layout.root.walk()), len(visible(page))))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    layout = build(wndlayout.load(argv[1]))
    wndlayout.save(layout, argv[2])
    print("%s: %d windows" % (argv[2], sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
