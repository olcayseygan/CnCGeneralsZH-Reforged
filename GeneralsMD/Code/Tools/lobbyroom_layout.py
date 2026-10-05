#!/usr/bin/env python3
"""Lay the skirmish and LAN staging rooms out on one grid.

EA's two rooms were placed by hand and never lined up with themselves: the Army heading sat 17
pixels right of its column, the seat table started at 45 while the tab page under it started at
78, and the Team column ended five pixels short of the map. The fork's own tab strip and settings
page then went in wherever the old settings row had been, which in the skirmish room meant two
tabs floating in the middle of the panel over nothing.

So both rooms are two tracks on one grid. The seat table on the left shares its left and right edges
with the tab page under it; the map column on the right shares its edges with the career stats and
Play Game. Back goes bottom left, as far from Play Game as the panel allows. One spacing scale, 4 8
16 24 32; a seat row is 24 high on a 28 pitch, a button 32. The open tab's caption is green, because
EA's disabled caption colour measured 2.1:1 on the panel. Nothing is redrawn: every control keeps
its art, only its rectangle moves.

The LAN room also gets a game mode box, cloned from starting cash, at the top right in line with the
title and the map column. The settings page has no free row wide enough for "Apocalypse:
continuous", and the header band right of the title is empty. It is hidden in the file and the menu
code shows it. The skirmish room has none: single player Apocalypse is a room of its own, written
by apocalypseroom_layout.py from this one's output. Peace time is the last row of its column,
because an Apocalypse game hides it and a hidden last row leaves no hole.

    python lobbyroom_layout.py <Menus dir in> <Menus dir out>

The input is what lobbysettings_layout.py wrote plus the unit limit check box; run that one first
when regenerating from the shipped files.

    python lobbyroom_layout.py selfcheck

reads the tracked layouts back and checks every window the menu code names is still there, no two
visible controls in one container overlap, and the shared edges are still shared. Run by CTest as
lobbyroom_selfcheck. Layouts are not in the multiplayer INI checksum.
"""

import os
import sys

import wndlayout

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")

FRAME = (40, 40, 720, 520)
INNER_LEFT, INNER_RIGHT = 56, 744
# A static text draws its first glyph 6 pixels inside its rectangle, so text boxes start 6 pixels
# left of the edge the controls share and the letters land on it.
TEXT_NUDGE = 6
TITLE = (INNER_LEFT - TEXT_NUDGE, 48, 400, 32)
RULE_TOP = 88
HEADER_TOP = 96
ROW_TOP, ROW_PITCH, ROW_HEIGHT = 120, 28, 24
SEAT_COUNT = 8
ACCEPT_LIGHT = 16

MAP_WIDTH = 176
MAP_LEFT = INNER_RIGHT - MAP_WIDTH
MAP_WINDOW_HEIGHT = 148
SEAT_RIGHT = MAP_LEFT - 16
COLUMN_GAP = 4

TAB_TOP, TAB_WIDTH, TAB_GAP = 356, 136, 8
PAGE_TOP = TAB_TOP + ROW_HEIGHT + 8
BUTTON_TOP, BUTTON_HEIGHT = 512, 32
PAGE_BOTTOM = BUTTON_TOP - 16

SETTING_ROW_PITCH = 32
SETTING_PADDING = 16
SLIDER_HEIGHT = 20
SLIDER_WIDTH = 72
EMOTE_WIDTH = 36

STAT_INSET = 8
# "Current Win Streak" wraps at 800x600 in anything under 128; the values are one or two digits
STAT_VALUE_WIDTH = 32

TAB_CAPTION_COLOR = ("ENABLED:  255 255 255 255, ENABLEDBORDER:  0 0 0 255, "
                     "DISABLED: 186 255 12 255, DISABLEDBORDER: 0 2 0 255, "
                     "HILITE:   186 255 12 255, HILITEBORDER:   0 2 0 255")

# (heading, control prefix, width). "USA Super Weapon General" clips at 800x600 in anything under
# 208; a player name scrolls inside its box, so the player column is the one that gives.
SKIRMISH_COLUMNS = [
    ("StaticTextPlayers", "ComboBoxPlayer", 116),
    ("StaticTextColor", "ComboBoxColor", 80),
    ("StaticTextFaction", "ComboBoxPlayerTemplate", 208),
    ("StaticTextTeam", "ComboBoxTeam", 80),
]
LAN_COLUMNS = [
    (None, "ButtonAccept", ACCEPT_LIGHT),
    ("StaticTextPlayers", "ComboBoxPlayer", 104),
    ("StaticTextColor", "ComboBoxColor", 80),
    ("StaticTextFaction", "ComboBoxPlayerTemplate", 208),
    ("StaticTextTeam", "ComboBoxTeam", 72),
]

# (label width, control width, rows); a row is (label, combo), (label, slider, readout) or (check,).
# "Allow Superweapons" needs 152 at 800x600.
SKIRMISH_SETTINGS = [
    (96, 152, [("StartingCashLabel", "ComboBoxStartingCash"), ("LabelSuperweapons", "ComboBoxSuperweapons"),
               ("LabelIncomeSharing", "ComboBoxIncomeSharing"), ("LabelSupplyPileLimit", "ComboBoxSupplyPileLimit")]),
    (88, 112, [("LabelGameSpeed", "ComboBoxGameSpeed"), ("CheckBoxUnitLimit",),
               ("LabelTechRespawn", "ComboBoxTechRespawn")]),
]
# The skirmish page is 108 high and the pile limit is its fourth row: 26 apart from 4 under the page
# top, the last row ends two pixels above the page bottom.
SKIRMISH_SETTING_ROW_PITCH = 26
SKIRMISH_SETTING_TOP_PADDING = 4
# The LAN page is 84 high over the chat entry and every column is full, so the check boxes take a
# third column and the rows close up to 28 from the page top to fit a third one.
LAN_SETTINGS = [
    (96, 152, [("StartingCashLabel", "ComboBoxStartingCash"), ("LabelSuperweapons", "ComboBoxSuperweapons"),
               ("LabelTechRespawn", "ComboBoxTechRespawn")]),
    (96, 152, [("LabelIncomeSharing", "ComboBoxIncomeSharing"), ("LabelSupplyPileLimit", "ComboBoxSupplyPileLimit"),
               ("LabelPeaceTime", "ComboBoxPeaceTime")]),
    (0, 128, [("CheckBoxProRules",), ("CheckBoxUnitLimit",)]),
]
LAN_SETTING_ROW_PITCH = 28
LAN_SETTING_TOP_PADDING = 4
# the game mode box and the control it is cloned from; it sits on the map column's edges, centred
# in the band between the frame top and the rule
GAME_MODE, GAME_MODE_MODEL = "ComboBoxGameMode", "ComboBoxStartingCash"
GAME_MODE_RECT = (MAP_LEFT, (FRAME[1] + RULE_TOP - ROW_HEIGHT) // 2, MAP_WIDTH, ROW_HEIGHT)
GAME_MODE_DATA = "ISEDITABLE: 0, MAXCHARS: 48, MAXDISPLAY: 5, ASCIIONLY: 0, LETTERSANDNUMBERS: 0"

STAT_ROWS = [
    ("StaticTextBestStreak", "StaticTextBestStreakValue"),
    ("StaticTextStreak", "StaticTextStreakValue"),
    ("StaticTextWins", "StaticTextWinsValue"),
    ("StaticTextLosses", "StaticTextLossesValue"),
]

# the container each room's controls live in, the windows its menu code swaps with the page, and
# whether it carries the game mode box
ROOMS = {
    "SkirmishGameOptionsMenu": dict(container="SubParent", swapped="ListboxInfo", game_mode=False),
    "LanGameOptionsMenu": dict(container="GadgetParent", swapped="ListboxChatWindowLanGame", game_mode=True),
}


def short(window):
    return (window.name or "").split(":")[-1]


def need(layout, name):
    window = layout.find(name)
    if window is None:
        raise KeyError("layout has no %s" % name)
    return window


def left_align(window):
    if window.prop("STATICTEXTDATA") is not None:
        window.set_prop("STATICTEXTDATA", "CENTERED: 0")


def place_seats(layout, columns, seat_zero_player):
    left = INNER_LEFT
    for heading_name, prefix, width in columns:
        if heading_name is not None:
            heading = need(layout, heading_name)
            # no wider than the column: with a 4 pixel gap a nudged, wider box runs into the next heading
            heading.place(left - TEXT_NUDGE, HEADER_TOP, width, ROW_HEIGHT)
            left_align(heading)
        for seat in range(SEAT_COUNT):
            top = ROW_TOP + seat * ROW_PITCH
            name = "%s%d" % (prefix, seat)
            if seat == 0 and prefix == "ComboBoxPlayer" and seat_zero_player:
                # in the skirmish room the local player's seat is a text entry, not a combo box
                name = seat_zero_player
            control = need(layout, name)
            if prefix == "ButtonAccept":
                control.place(left, top + (ROW_HEIGHT - ACCEPT_LIGHT) // 2, ACCEPT_LIGHT, ACCEPT_LIGHT)
            else:
                control.place(left, top, width, ROW_HEIGHT)
        left += width + COLUMN_GAP
    if left - COLUMN_GAP != SEAT_RIGHT:
        raise ValueError("seat columns end at %d, expected %d" % (left - COLUMN_GAP, SEAT_RIGHT))


def place_map(layout):
    last_row_bottom = ROW_TOP + (SEAT_COUNT - 1) * ROW_PITCH + ROW_HEIGHT
    heading = need(layout, "StaticTextMapPreview")
    heading.place(MAP_LEFT - TEXT_NUDGE, HEADER_TOP, MAP_WIDTH + TEXT_NUDGE, ROW_HEIGHT)
    left_align(heading)
    # MapWindow's start position buttons are placed by the menu code from the map, so only the
    # window itself moves
    need(layout, "MapWindow").place(MAP_LEFT, ROW_TOP, MAP_WIDTH, MAP_WINDOW_HEIGHT)
    need(layout, "ButtonSelectMap").place(MAP_LEFT, last_row_bottom - BUTTON_HEIGHT, MAP_WIDTH, BUTTON_HEIGHT)
    entry_top = last_row_bottom - BUTTON_HEIGHT - 8 - ROW_HEIGHT
    need(layout, "TextEntryMapDisplay").place(MAP_LEFT, entry_top, MAP_WIDTH, ROW_HEIGHT)
    if ROW_TOP + MAP_WINDOW_HEIGHT + 8 != entry_top:
        raise ValueError("map window and map name are %d apart, expected 8"
                         % (entry_top - ROW_TOP - MAP_WINDOW_HEIGHT))


def place_settings(layout, columns, page_left, page_right, page_top, page_bottom,
                   row_pitch=SETTING_ROW_PITCH, top_padding=SETTING_PADDING):
    left = page_left + SETTING_PADDING
    for label_width, control_width, rows in columns:
        for row, names in enumerate(rows):
            top = page_top + top_padding + row * row_pitch
            if top + ROW_HEIGHT > page_bottom:
                raise ValueError("%s runs to %d, past the page bottom at %d"
                                 % (names[0], top + ROW_HEIGHT, page_bottom))
            if len(names) == 1:
                need(layout, names[0]).place(left, top, label_width + control_width, ROW_HEIGHT)
                continue
            label = need(layout, names[0])
            label.place(left - TEXT_NUDGE, top, label_width, ROW_HEIGHT)
            left_align(label)
            control_left = left + label_width
            if len(names) == 3:
                need(layout, names[1]).place(control_left, top + (ROW_HEIGHT - SLIDER_HEIGHT) // 2,
                                             SLIDER_WIDTH, SLIDER_HEIGHT)
                need(layout, names[2]).place(control_left + SLIDER_WIDTH, top,
                                             control_width - SLIDER_WIDTH, ROW_HEIGHT)
            else:
                need(layout, names[1]).place(control_left, top, control_width, ROW_HEIGHT)
        left += label_width + control_width + SETTING_PADDING
    if left > page_right:
        raise ValueError("settings run to %d, past the page edge at %d" % (left, page_right))


def place_frame(layout, container_name, rule_name):
    need(layout, container_name).place(*FRAME)
    title = need(layout, "StaticTextTitle")
    title.place(*TITLE)
    left_align(title)
    need(layout, rule_name).place(FRAME[0], RULE_TOP, FRAME[2], 0)
    need(layout, "ButtonBack").place(INNER_LEFT, BUTTON_TOP, MAP_WIDTH, BUTTON_HEIGHT)
    need(layout, "ButtonStart").place(MAP_LEFT, BUTTON_TOP, MAP_WIDTH, BUTTON_HEIGHT)


def place_tabs(layout, other_tab):
    for index, name in enumerate((other_tab, "TabLobbySettings")):
        tab = need(layout, name)
        tab.place(INNER_LEFT + index * (TAB_WIDTH + TAB_GAP), TAB_TOP, TAB_WIDTH, ROW_HEIGHT)
        tab.set_prop("TEXTCOLOR", TAB_CAPTION_COLOR)


def skirmish_panels(sub_parent):
    """EA left the map info frame and the career stats panel unnamed; the stats panel is the one
    with children."""
    unnamed = [child for child in sub_parent.children if not short(child)]
    info_frames = [child for child in unnamed if not child.children]
    stats_panels = [child for child in unnamed if child.children]
    if len(info_frames) != 1 or len(stats_panels) != 1:
        raise ValueError("expected one info frame and one stats panel in SubParent, found %d and %d"
                         % (len(info_frames), len(stats_panels)))
    return info_frames[0], stats_panels[0]


def build_skirmish(layout):
    place_frame(layout, "SubParent", "Line")
    place_seats(layout, SKIRMISH_COLUMNS, "TextEntryPlayerName")
    place_map(layout)
    place_tabs(layout, "TabInfo")

    info_frame, stats_panel = skirmish_panels(need(layout, "SubParent"))
    page_height = PAGE_BOTTOM - PAGE_TOP
    for window in (info_frame, need(layout, "ListboxInfo"), need(layout, "PageLobbySettings")):
        window.place(INNER_LEFT, PAGE_TOP, SEAT_RIGHT - INNER_LEFT, page_height)
    place_settings(layout, SKIRMISH_SETTINGS, INNER_LEFT, SEAT_RIGHT, PAGE_TOP, PAGE_BOTTOM,
                   SKIRMISH_SETTING_ROW_PITCH, SKIRMISH_SETTING_TOP_PADDING)

    stats_panel.place(MAP_LEFT, PAGE_TOP, MAP_WIDTH, page_height)
    value_left = INNER_RIGHT - STAT_INSET - STAT_VALUE_WIDTH
    label_left = MAP_LEFT + STAT_INSET - TEXT_NUDGE
    for row, (label_name, value_name) in enumerate(STAT_ROWS):
        top = PAGE_TOP + STAT_INSET + row * ROW_HEIGHT
        label = need(layout, label_name)
        label.place(label_left, top, value_left - label_left, ROW_HEIGHT)
        left_align(label)
        need(layout, value_name).place(value_left, top, STAT_VALUE_WIDTH, ROW_HEIGHT)
    # an earlier version put the mode box here too; Apocalypse has its own room now
    mode = layout.find(GAME_MODE)
    if mode is not None:
        need(layout, ROOMS["SkirmishGameOptionsMenu"]["container"]).children.remove(mode)
    return layout


def build_lan(layout):
    place_frame(layout, "GadgetParent", "LINE")
    place_seats(layout, LAN_COLUMNS, None)
    place_map(layout)
    place_tabs(layout, "TabChat")

    chat_width = INNER_RIGHT - INNER_LEFT
    entry_top = BUTTON_TOP - 8 - ROW_HEIGHT
    listbox_height = entry_top - 8 - PAGE_TOP
    need(layout, "ListboxChatWindowLanGame").place(INNER_LEFT, PAGE_TOP, chat_width, listbox_height)
    need(layout, "PageLobbySettings").place(INNER_LEFT, PAGE_TOP, chat_width, listbox_height)
    need(layout, "TextEntryChat").place(INNER_LEFT, entry_top, chat_width - EMOTE_WIDTH - COLUMN_GAP, ROW_HEIGHT)
    need(layout, "ButtonEmote").place(INNER_RIGHT - EMOTE_WIDTH, entry_top, EMOTE_WIDTH, ROW_HEIGHT)
    place_settings(layout, LAN_SETTINGS, INNER_LEFT, INNER_RIGHT, PAGE_TOP, PAGE_TOP + listbox_height,
                   LAN_SETTING_ROW_PITCH, LAN_SETTING_TOP_PADDING)
    add_game_mode(layout, "LanGameOptionsMenu")
    return layout


def add_game_mode(layout, menu):
    control = layout.find(GAME_MODE)
    if control is None:
        model = need(layout, GAME_MODE_MODEL)
        control = wndlayout.clone(model, "%s.wnd:%s" % (menu, GAME_MODE))
        control.children = []
        # the starting cash tooltip would explain the wrong thing, and the entries say what they are
        tooltip = control.prop_index("TOOLTIPTEXT")
        if tooltip >= 0:
            del control.props[tooltip]
        control.set_prop("STATUS", model.prop("STATUS").split("=")[1].strip(" ;") + "+HIDDEN")
        need(layout, ROOMS[menu]["container"]).children.append(control)
    # the shown entry is cut to MAXCHARS, and starting cash's 16 would leave "Apocalypse: continuou"
    control.set_prop("COMBOBOXDATA", GAME_MODE_DATA)
    control.place(*GAME_MODE_RECT)


BUILDERS = {"SkirmishGameOptionsMenu": build_skirmish, "LanGameOptionsMenu": build_lan}


# ---------------------------------------------------------------------------
# selfcheck
# ---------------------------------------------------------------------------

def overlaps(container, swapped):
    """Visible controls in one container drawn over each other. The page and the window the tabs
    swap it with share a rectangle on purpose, and only one of them is ever shown."""
    visible = [(short(child), child.rect[:4]) for child in container.children
               if short(child) and "HIDDEN" not in (child.prop("STATUS") or "")]
    exempt = frozenset(("PageLobbySettings", swapped))
    found = []
    for index, (name, (left, top, right, bottom)) in enumerate(visible):
        for other, (other_left, other_top, other_right, other_bottom) in visible[index + 1:]:
            if frozenset((name, other)) == exempt:
                continue
            if left < other_right and other_left < right and top < other_bottom and other_top < bottom:
                found.append("%s and %s overlap" % (name, other))
    return found


def shared_edges(layout):
    """The two tracks: what is meant to stand on one edge, and whether it still does."""
    found = []
    tracks = [
        ("seat table right edge", SEAT_RIGHT, [("ComboBoxTeam0", 2), ("PageLobbySettings", None)]),
        ("map column left edge", MAP_LEFT, [("MapWindow", 0), ("ButtonSelectMap", 0), ("ButtonStart", 0)]),
        ("inner left edge", INNER_LEFT, [("ButtonBack", 0), ("TabLobbySettings", None)]),
    ]
    for label, edge, members in tracks:
        for name, side in members:
            window = layout.find(name)
            if window is None or side is None:
                continue
            value = window.rect[side]
            if value != edge:
                found.append("%s is %s at %d, not %d" % (name, label, value, edge))
    return found


def game_mode(layout, container_name):
    """The mode box the menu code names, in the room's container, hidden, at the top right."""
    control = layout.find(GAME_MODE)
    if control is None:
        return ["no %s" % GAME_MODE]
    found = []
    if control not in need(layout, container_name).children:
        found.append("%s is not in %s" % (GAME_MODE, container_name))
    if "HIDDEN" not in (control.prop("STATUS") or ""):
        found.append("%s is not hidden; the menu code shows it" % GAME_MODE)
    if GAME_MODE_DATA not in (control.prop("COMBOBOXDATA") or ""):
        found.append("%s cuts its entries short; COMBOBOXDATA is not %s" % (GAME_MODE, GAME_MODE_DATA))
    left, top, right, bottom = control.rect[:4]
    if (left, top, right - left, bottom - top) != GAME_MODE_RECT:
        found.append("%s is not at %s" % (GAME_MODE, (GAME_MODE_RECT,)))
    return found


def selfcheck():
    problems = []
    for menu, spec in sorted(ROOMS.items()):
        path = os.path.join(MENUS, menu + ".wnd")
        layout = wndlayout.load(path)
        container = layout.find(spec["container"])
        if container is None:
            problems.append("%s.wnd has no %s" % (menu, spec["container"]))
            continue
        try:
            BUILDERS[menu](wndlayout.load(path))
        except (KeyError, ValueError) as error:
            problems.append("%s.wnd no longer builds: %s" % (menu, error))
        problems.extend("%s.wnd: %s" % (menu, problem) for problem in overlaps(container, spec["swapped"]))
        problems.extend("%s.wnd: %s" % (menu, problem) for problem in shared_edges(layout))
        if spec["game_mode"]:
            problems.extend("%s.wnd: %s" % (menu, problem) for problem in game_mode(layout, spec["container"]))
        elif layout.find(GAME_MODE) is not None:
            problems.append("%s.wnd: carries %s, which only the LAN room has" % (menu, GAME_MODE))

    for problem in problems:
        print("lobbyroom: %s" % problem)
    if problems:
        return 1
    print("lobbyroom: %d rooms on the grid" % len(ROOMS))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    source, target = argv[1], argv[2]
    for menu, build in sorted(BUILDERS.items()):
        layout = wndlayout.load(os.path.join(source, menu + ".wnd"))
        out = os.path.join(target, menu + ".wnd")
        wndlayout.save(build(layout), out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
