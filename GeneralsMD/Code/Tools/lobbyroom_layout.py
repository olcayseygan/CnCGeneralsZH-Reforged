#!/usr/bin/env python3
"""Lay the skirmish, LAN and online staging rooms out on one grid, authored at 1920x1080.

EA drew the three rooms at 800x600 and the shell stretched them to the screen, x and y apart, so on
a 16:9 monitor every emblem, start spot and map preview came out a third wider than tall. These
rooms are authored at 1920x1080 instead, and the window loader fits a shell screen authored wider
than 4:3 at one scale (GameWindowManagerScript.cpp, parseScreenRect): exact on 16:9, twice that on
4K, centred between bands of its own backdrop on 4:3 and 16:10.

The grid is frame A's (docs/plans/MENU-REDESIGN.md, package 3a; the design is
experiments/proofs/2026-11-24/lobbies/classic/README-classic-room.md in the wrapper): the panel on
32,28 to 1888,1052, the seat column 60 to 1294 and the right column 1318 to 1860, the foot rule at
962 and the keys under it. Everything that is a line, a frame, a head, a portrait or a figure is
drawn by GUIUtil.cpp (InitLobbySeatVisuals and the room look under it), never by this layout.

The seat rows here are only where each row's controls start. The code lays the rows out itself on
every update, grouped by team under a head a team, with a scroll past what fits, so the rows of this
file are the visible window of that list: 16 in the skirmish room (more than the 792 pixels of the
seat column ever show), 8 in the network rooms. Every row is one line of 40 tall boxes, 56 apart,
seated or open. A row's cells are the same at every count: player 152 to 444 (a computer's level to
292, its personality beside it from 300), army 460 to 780, colour 796 to 976, team 992 to 1104,
handicap 1120 to 1262. The windows an earlier grid drew a row with (SeatCard, SeatBar,
SeatSwatch, SeatTeam) stay, hidden, so a lookup still finds them.

The right column is the map, its name and Select Map, then the rules from y 506. The network rooms
put the chat on a tab beside the rules (TabLobbySettings and TabChat, the names InitLobbyTabs looks
up); the skirmish room has no chat, and its record and presets open as boxes over the room. Back
and the room's second key sit bottom left, Play / Start is the one lit key bottom right; the
skirmish room's presets key moves into the header.

Every control keeps its name; only its rectangle moves. A layout still at 800x600 is first scaled
to the 1920x1080 grid the way the stretch drew it, so a window this script does not place stays
where it appeared before.

    python lobbyroom_layout.py <Menus dir in> <Menus dir out>

Running it on its own output changes nothing.

    python lobbyroom_layout.py selfcheck

reads the tracked layouts back and checks that every window the menu code names is there, that the
script reproduces the tracked file byte for byte, that every window is authored at 1920x1080 and
that no two visible controls in one container overlap. Run by CTest as lobbyroom_selfcheck. Layouts
are not in the multiplayer INI checksum.
"""

import os
import sys

import wndlayout
from wndlayout import clone

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")

WIDE = (1920, 1080)
OLD = (800, 600)
SEAT_COUNT = 8

# The ruler frame an earlier grid drew the room in (MappedImage MainMenuRuler); frame A is drawn by
# the code now, so the window stays only so a second run finds it, hidden and imageless.
RULER_WINDOW = "RulerFrame"
# frame A's panel and the room's columns (README-classic-room.md, "The room's rects")
FRAME = (32, 28, 1856, 1024)
INNER_LEFT, INNER_RIGHT = 60, 1860
SEAT_RIGHT = 1294
COLUMN_LEFT = 1318   # the map and rules column
TITLE = (INNER_LEFT, 46, 520, 48)
RULE_TOP = 112
HEADER_TOP, HEADER_HEIGHT = 126, 32
ROW_TOP = 160
SEATS_BOTTOM = 952
GAP = 8
# a static text draws its first glyph a few pixels inside its rectangle; the boxes' inner rows
TEXT_NUDGE = 10
CONTROL_HEIGHT = 40

# The skirmish room holds every seat the game has (MAX_SLOTS, NetworkDefs.h); its 16 rows are more
# than the seat column ever shows (15 open seats of 52 and a head), cloned from the eighth. The
# network rooms keep their eight. The rows are placed by the code (GUIUtil.cpp, the room look);
# here each row stands at its eight-seat place so the file reads as a room.
SKIRMISH_SEAT_ROWS = 16
SEAT_ROW_PREFIXES = ("ComboBoxPlayer", "ComboBoxColor", "ComboBoxPlayerTemplate", "ComboBoxTeam",
                     "ComboBoxAIPersona", "ComboBoxHandicap")
SEAT_SCROLL_UP, SEAT_SCROLL_DOWN, SEAT_SCROLL_SLIDER = "ButtonSeatScrollUp", "ButtonSeatScrollDown", "SliderSeatScroll"
SEAT_SCROLL = (SEAT_SCROLL_UP, SEAT_SCROLL_DOWN, SEAT_SCROLL_SLIDER)
SEAT_SCROLL_ART = "ComboBoxTeam1"
SLIDER_TEMPLATE = ("WOLCustomLobby", "SliderChatAdjust")
# the track the code draws: 1280 to 1294, beside the cells
TRACK = (1280, ROW_TOP, 14, SEATS_BOTTOM - ROW_TOP)

# a seat row, seated, open or closed alike (GUIUtil.cpp's LOBBY_ROW_SEAT and placeRoomRow): 56 tall,
# one line of 40 tall boxes from +8; a computer's level (152 to 292) beside its personality (300 to
# 444); the online ping light 18 square in the portrait's top right corner (the portrait 96 to 140)
ROW_PITCH = 56
ROW_BOX_TOP, ROW_BOX_HEIGHT = 8, 40
BOX_HEIGHT = 44
LEVEL_RIGHT = 292
CELLS = {"player": (152, 444), "army": (460, 780), "colour": (796, 976), "team": (992, 1104),
         "handicap": (1120, 1262)}
PING, PING_RIGHT, PING_TOP = 18, 139, 7

MAP_TOP = ROW_TOP
MAP_WIDTH, MAP_HEIGHT = 542, 278
SELECT_MAP_WIDTH, SELECT_MAP_HEIGHT = 200, 46
MAP_CAPTION_TOP = MAP_TOP + MAP_HEIGHT + 10
RULES_Y = 506                  # the RULES head or the tabs, whatever the map (the pickers hold it too)
RULES_TOP = RULES_Y + 36
RULE_PITCH_MOST = 60          # a 44 box and a 16 gap: on the 8 px grid (62 left 18)
SETTING_HEIGHT = BOX_HEIGHT
SETTING_LABEL_WIDTH = 160
CHECK_LINE = 34
SPOT = 28
TAB_HEIGHT = 30
TABS_LAYOUT = (("TabLobbySettings", "GUI:RoomRules", 96), ("TabChat", "GUI:RoomChat", 96))
CHAT_ENTRY_HEIGHT, SEND_WIDTH = 40, 122

FOOT_RULE = 962
BUTTON_TOP, BUTTON_HEIGHT = 976, 52
BACK_WIDTH, BUDDIES_WIDTH = 240, 240
KEY_PITCH = 256               # 16 between the two left keys, on the grid (252 left 12)
PRESETS_RECT = (1500, 50, 360, 42)
STAT_PITCH, STAT_VALUE_WIDTH = 40, 64
STATS_WIDTH = 372

# the skirmish room's record and presets, boxes centred on the room (mode.py's RECORD 1200x578 and
# PRESETS 1200x668); the veil dims whatever they cover
POPOVER = (360, 251, 1200, 578)
POPOVER_PADDING = 32
POPOVER_TITLE_HEIGHT = 50
CLOSE_WIDTH = 220
RECORD_WINDOWS = ("RecordVeil", "RecordPopover", "StaticTextRecordTitle", "ButtonRecordClose")
# the open record dims the whole room behind it
RECORD_VEIL = (0, 0, 0, 160)
# the button's label is in capitals like every layout button's; GUI:LobbyRecord titles the popover
RECORD_TEXT, RECORD_BUTTON_TEXT, CLOSE_TEXT = "GUI:LobbyRecord", "GUI:LobbyRecordButton", "GUI:LobbyRecordClose"

# The skirmish room's presets: a Presets button after Record opens a popover over the seats' foot, a
# title, the list of big games (filled by the menu code) and Close, under the record's veil. The list
# wears a seat dropdown's open-list art; the record's own list is a table of medals.
PRESET_SLOT_BUTTONS = (("ButtonPresetLoad", "GUI:SkirmishPresetLoad"), ("ButtonPresetSave", "GUI:SkirmishPresetSave"),
                       ("ButtonPresetClear", "GUI:SkirmishPresetClear"))
PRESET_SLOT_BUTTON_WIDTH = 150
PRESET_WINDOWS = ("ButtonPresets", "PresetPopover", "StaticTextPresetTitle", "ListboxPresets",
                  "ButtonPresetsClose") + tuple(name for name, _text in PRESET_SLOT_BUTTONS)
PRESET_TEXT, PRESET_BUTTON_TEXT, PRESET_TOOLTIP = "GUI:LobbyPresets", "GUI:LobbyPresetsButton", "TOOLTIP:LobbyPresets"
# every preset at once, its presets grouped under a heading a seat count
PRESET_POPOVER = (360, 206, 1200, 668)
PRESET_LIST_DATA = ("LENGTH: 64, AUTOSCROLL: 0, SCROLLIFATEND: 0, AUTOPURGE: 0, SCROLLBAR: 1, MULTISELECT: 0, "
                    "COLUMNS: 1, FORCESELECT: 0")

# UIStyle's tokens (UIStyle.cpp, UI-STYLE.md in the wrapper's docs/reference)
PANEL = (0, 0, 0, 200)
CARD = (10, 12, 22, 255)
FRAME_COLOR = (47, 55, 168, 255)
ACTIVE = (186, 255, 12, 255)

POPOVER_COLOR = CARD
POPOVER_BORDER = FRAME_COLOR
# GUIUtil.cpp gives a network room's cards the same, and turns them green or gold
CARD_COLOR = PANEL
CARD_BORDER = FRAME_COLOR
EMPTY_COLOR = (48, 54, 76, 255)
VEIL = (2, 4, 12, 120)

# The personality column: its combo boxes are cloned from the faction ones, so they keep EA's art.
# The heading an earlier grid gave it comes out again.
PERSONA_PREFIX, PERSONA_TOOLTIP = "ComboBoxAIPersona", "GUI:AIPersonaTooltip"
RETIRED_PERSONA_HEADING = "StaticTextAIPersona"

# The handicap column, on every row since a human takes a handicap too. Combo boxes and heading are
# cloned from the team's; the menu code fills the boxes.
HANDICAP_PREFIX, HANDICAP_TOOLTIP = "ComboBoxHandicap", "TOOLTIP:Handicap"
HANDICAP_HEADING, HANDICAP_TEXT = "StaticTextHandicap", "GUI:Handicap"

# The game speed, a label and a combo cloned from the tech respawn row's, so they inherit that row's
# art and lettering; the menu code fills the combo, and in the network rooms only the host may change
# it. All three rooms take the turbo box out that an earlier grid had cloned into the network ones.
GAME_SPEED_ROW = (("LabelGameSpeed", "LabelTechRespawn"), ("ComboBoxGameSpeed", "ComboBoxTechRespawn"))
GAME_SPEED_TEXT, GAME_SPEED_TOOLTIP = "GUI:GameSpeed", "TOOLTIP:GameSpeed"
# the network rooms' tooltip says the host picks it; the skirmish one talks about a skirmish
ROOM_GAME_SPEED_TOOLTIP = "TOOLTIP:RoomGameSpeed"
GAME_SPEED_DROPDOWN = tuple(name for name, _template in GAME_SPEED_ROW)
RETIRED_TURBO_BOX = "CheckBoxTurbo"
TABS = ("TabInfo", "TabChat", "TabLobbySettings")
SEAT_WINDOWS = ("SeatCard", "SeatBar", "SeatSwatch", "SeatTeam")
# what earlier versions of this grid added and this one takes out again
RETIRED_SEAT_WINDOWS = ("SeatBadge", "SeatStatus", "SeatEmblem")

STAT_ROWS = [
    ("StaticTextBestStreak", "StaticTextBestStreakValue"),
    ("StaticTextStreak", "StaticTextStreakValue"),
    ("StaticTextWins", "StaticTextWinsValue"),
    ("StaticTextLosses", "StaticTextLossesValue"),
]

# (heading, control prefixes, cell); a heading of None for a control under another's. The heads
# are drawn by the code; their windows are placed on them and hidden, so a lookup still finds them.
SEAT_COLUMNS = [
    ("StaticTextPlayers", ("ComboBoxPlayer",), "player"),
    (None, (PERSONA_PREFIX,), "player"),
    ("StaticTextFaction", ("ComboBoxPlayerTemplate",), "army"),
    ("StaticTextColor", ("ComboBoxColor",), "colour"),
    ("StaticTextTeam", ("ComboBoxTeam",), "team"),
    (HANDICAP_HEADING, (HANDICAP_PREFIX,), "handicap"),
]
# A seat's player name in the online room is a static text over its combo, as EA drew it; the code
# parks it. The ping light stands on the name's second line.
GAMESPY_COLUMNS = [(None, ("GenericPing",), "player"),
                   ("StaticTextPlayers", ("ComboBoxPlayer", "StaticTextPlayer"), "player")] + SEAT_COLUMNS[1:]

# The rules, one column: (label, combo) rows, then the check boxes up to three to a line.
MATCH_RULES = [("StartingCashLabel", "ComboBoxStartingCash"), ("LabelSuperweapons", "ComboBoxSuperweapons"),
               ("LabelIncomeSharing", "ComboBoxIncomeSharing"), ("LabelSupplyPileLimit", "ComboBoxSupplyPileLimit"),
               ("LabelTechRespawn", "ComboBoxTechRespawn")]
SKIRMISH_RULES = [GAME_SPEED_DROPDOWN] + MATCH_RULES
NETWORK_RULES = [GAME_SPEED_DROPDOWN] + MATCH_RULES[:1] + [("LabelPeaceTime", "ComboBoxPeaceTime")] + MATCH_RULES[1:]
# The day and night cycle's box (DN in the options string) in all three rooms, cloned from the unit
# limit box so it wears that box's art, and put after it. It shares the unit limit's row in the
# skirmish room and opens a row of its own in the network rooms.
DAY_NIGHT = ("CheckBoxDayNight", "GUI:DayNight", "TOOLTIP:DayNight")
# The Hidden Map box sat between the two until the owner took it out of every room on 2026-11-25; a
# ranked Quick Match still starts black on its own (SH in the options string).
RETIRED_HIDDEN_MAP_BOX = "CheckBoxStartShrouded"
# the check boxes by line, the column's foot last; the online room's first line is the two rules it
# pins (Record Stats, Original Armies Only)
SKIRMISH_CHECKS = [["CheckBoxUnitLimit", DAY_NIGHT[0]]]
LAN_CHECKS = [["CheckBoxProRules", "CheckBoxUnitLimit", DAY_NIGHT[0]]]
GAMESPY_CHECKS = [["CheckBoxUseStats", "CheckBoxLimitArmies"]] + LAN_CHECKS

ROOMS = {
    "SkirmishGameOptionsMenu": dict(container="SubParent", rule="Line", title="StaticTextTitle",
                                    columns=SEAT_COLUMNS, rules=SKIRMISH_RULES, checks=SKIRMISH_CHECKS,
                                    seat_rows=SKIRMISH_SEAT_ROWS),
    "LanGameOptionsMenu": dict(container="GadgetParent", rule="LINE", title="StaticTextTitle",
                               columns=SEAT_COLUMNS, rules=NETWORK_RULES, checks=LAN_CHECKS,
                               chat="ListboxChatWindowLanGame"),
    "GameSpyGameOptionsMenu": dict(container="FadeParent", rule="LINE", title="StaticTextGameName",
                                   columns=GAMESPY_COLUMNS, rules=NETWORK_RULES, checks=GAMESPY_CHECKS,
                                   chat="ListboxChatWindowGameSpyGameSetup"),
}

# every window the menu code looks up, besides the per-seat ones in the columns
NAMED = ["MapWindow", "ButtonSelectMap", "TextEntryMapDisplay", "StaticTextMapPreview", "ButtonBack",
         "ButtonStart", "PageLobbySettings"]
NAMED_PER_SEAT = ["ComboBoxColor", "ComboBoxPlayerTemplate", "ComboBoxTeam", PERSONA_PREFIX,
                  "ButtonMapStartPosition"] + list(SEAT_WINDOWS)
NAMED_SKIRMISH = ["ButtonRecord", "ListboxInfo"] + list(RECORD_WINDOWS) + list(SEAT_SCROLL) + list(PRESET_WINDOWS)
NAMED_NETWORK_PER_SEAT = ["ButtonAccept"]


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


def draw_data(color, border, image="NoImage"):
    first = "IMAGE: %s, COLOR: %d %d %d %d, BORDERCOLOR: %d %d %d %d" % ((image,) + color + border)
    rest = "IMAGE: NoImage, COLOR: 255 255 255 0, BORDERCOLOR: 255 255 255 0"
    return ", ".join([first] + [rest] * 8)


# ---------------------------------------------------------------------------
# what the rooms carried in before this grid, kept idempotent
# ---------------------------------------------------------------------------

def add_game_speed_row(layout, menu):
    """Clone the tech respawn row into the game speed row, once; a second run finds it there and
    only sets the room's tooltip again."""
    tooltip = GAME_SPEED_TOOLTIP if menu == "SkirmishGameOptionsMenu" else ROOM_GAME_SPEED_TOOLTIP
    for name, template in GAME_SPEED_ROW:
        control = layout.find(name)
        if control is None:
            source = need(layout, template)
            control = clone(source, "%s.wnd:%s" % (menu, name))
            if name.startswith("Label"):
                control.put_prop("TEXT", '"%s"' % GAME_SPEED_TEXT)
            layout.root.parent_of(source).children.append(control)
        control.put_prop("TOOLTIPTEXT", '"%s"' % tooltip)


def add_check_box(layout, menu, spec, after="CheckBoxUnitLimit"):
    """Clone the `after` box into another box of that kind, put after it, once; a second run finds
    it there."""
    name, text, tooltip = spec
    if layout.find(name) is not None:
        return
    source = need(layout, after)
    box = clone(source, "%s.wnd:%s" % (menu, name))
    box.put_prop("TEXT", '"%s"' % text)
    box.put_prop("TOOLTIPTEXT", '"%s"' % tooltip)
    parent = layout.root.parent_of(source)
    parent.children.insert(parent.children.index(source) + 1, box)


def remove(layout, names):
    for name in names:
        window = layout.find(name)
        if window is not None:
            layout.root.parent_of(window).children.remove(window)


def add_combo_column(layout, menu, prefix, template, tooltip):
    """One combo box a seat, cloned from the template's and in once, ahead of the team heading, seat
    7 first like EA's columns. A layout that already has them is left alone."""
    if layout.find(prefix + "0") is not None:
        return
    named = lambda name: "%s.wnd:%s" % (menu, name)
    team = need(layout, "StaticTextTeam")
    container = layout.root.parent_of(team)
    added = []
    for seat in reversed(range(SEAT_COUNT)):
        combo = clone(need(layout, "%s%d" % (template, seat)), named("%s%d" % (prefix, seat)))
        combo.put_prop("TOOLTIPTEXT", '"%s"' % tooltip)
        added.append(combo)
    at = container.children.index(team)
    container.children[at:at] = added


def add_handicap_column(layout, menu):
    add_combo_column(layout, menu, HANDICAP_PREFIX, "ComboBoxTeam", HANDICAP_TOOLTIP)
    if layout.find(HANDICAP_HEADING) is not None:
        return
    team = need(layout, "StaticTextTeam")
    heading = clone(team, "%s.wnd:%s" % (menu, HANDICAP_HEADING))
    heading.put_prop("TEXT", '"%s"' % HANDICAP_TEXT)
    container = layout.root.parent_of(team)
    container.children.insert(container.children.index(team), heading)


def widen(layout):
    """Every rectangle still at 800x600 moves to where the stretch drew it on a 1920x1080 screen."""
    sx, sy = WIDE[0] / OLD[0], WIDE[1] / OLD[1]
    for node in layout.root.walk():
        left, top, right, bottom, cw, ch = node.rect
        if (cw, ch) == OLD:
            node.rect = (round(left * sx), round(top * sy), round(right * sx), round(bottom * sy)) + WIDE
    # EA's LAN root stops a pixel short; the loader only spreads a root that covers its whole
    # resolution over a wider screen, so all three are made exact
    node = layout.root
    node.rect = (0, 0) + WIDE + WIDE
    # EA's root draws the shell's ruler art, a 4:3 frame of its own: spread over a wide screen behind
    # the fitted panel it left a second outline and a stray title line. A plain veil darkens the
    # shell map instead, and the ruler comes back as a fitted window of its own (add_ruler).
    node.set_prop("STATUS", "ENABLED+NOFOCUS")
    node.set_prop("ENABLEDDRAWDATA", draw_data(VEIL, (0, 0, 0, 0)))


# ---------------------------------------------------------------------------
# the grid
# ---------------------------------------------------------------------------

def plain_window(template, name, image, color, border):
    """An empty window cloned from the room's container: no input, a colour fill or an image."""
    window = clone(template, name)
    window.children = []
    window.set_prop("STATUS", "ENABLED+NOINPUT+NOFOCUS" + ("+IMAGE" if image else ""))
    window.put_prop("SYSTEMCALLBACK", '"[None]"')
    window.set_prop("ENABLEDDRAWDATA", draw_data(color, border))
    return window


def add_ruler(layout, menu, container):
    """The ruler frame an earlier grid added, kept hidden and without its art: frame A is the code's."""
    if layout.find(RULER_WINDOW) is None:
        ruler = plain_window(container, "%s.wnd:%s" % (menu, RULER_WINDOW), False, (0, 0, 0, 0), (0, 0, 0, 0))
        layout.root.children.insert(0, ruler)
    ruler = need(layout, RULER_WINDOW)
    ruler.place(0, 0, WIDE[0], WIDE[1])
    ruler.set_prop("STATUS", "ENABLED+HIDDEN+NOINPUT+NOFOCUS")
    ruler.set_prop("ENABLEDDRAWDATA", draw_data((0, 0, 0, 0), (0, 0, 0, 0)))


def hide(window):
    status = window.prop("STATUS")
    if status is not None and "HIDDEN" not in status:
        window.set_prop("STATUS", statement(window, "STATUS") + "+HIDDEN")


def seat_rows(spec):
    return spec.get("seat_rows", SEAT_COUNT)


def add_seat_rows(layout, menu, spec):
    """The skirmish room's rows past the eighth: each seat control cloned from the eighth row's, once,
    next to it in its container; place_seats puts them on their rows."""
    for prefix in SEAT_ROW_PREFIXES:
        source = need(layout, "%s%d" % (prefix, SEAT_COUNT - 1))
        container = layout.root.parent_of(source)
        for seat in range(SEAT_COUNT, seat_rows(spec)):
            if layout.find("%s%d" % (prefix, seat)) is None:
                container.children.insert(container.children.index(source),
                                          clone(source, "%s.wnd:%s%d" % (menu, prefix, seat)))


def add_seat_windows(layout, menu, container, spec):
    """One card, bar and two swatches a seat, at the front of the container, hidden: the code draws
    a row's wash, colour and team itself now, and only a lookup still wants them."""
    remove(layout, ["%s%d" % (kind, seat) for kind in SEAT_WINDOWS + RETIRED_SEAT_WINDOWS
                    for seat in range(seat_rows(spec))])
    template = need(layout, spec["container"])
    named = lambda kind, seat: "%s.wnd:%s%d" % (menu, kind, seat)
    added = {kind: [] for kind in SEAT_WINDOWS}
    for seat in range(seat_rows(spec)):
        for kind in SEAT_WINDOWS:
            card = kind == "SeatCard"
            added[kind].append(plain_window(template, named(kind, seat), False,
                                            CARD_COLOR if card else EMPTY_COLOR,
                                            CARD_BORDER if card else (0, 0, 0, 0)))
    for window in (window for kind in SEAT_WINDOWS for window in added[kind]):
        window.place(INNER_LEFT, ROW_TOP, 0, 0)
        hide(window)
    container.children[0:0] = [window for kind in SEAT_WINDOWS for window in added[kind]]


def row_top(seat):
    """Where a row's controls start in the file: the eight-seat place, the rows past it over them."""
    return ROW_TOP + (seat % SEAT_COUNT) * ROW_PITCH


def place_seats(layout, spec, seat_zero_player):
    """Every row's controls in their cells, a human's row: the player box centred, the personality
    under where a computer's level would stand. The code moves and sizes them per seat."""
    rows = seat_rows(spec)
    for seat in range(rows):
        accept = layout.find("ButtonAccept%d" % seat)
        if accept is not None:
            # the menu code still shows and colours it; the row says READY in words now
            accept.place(INNER_LEFT, row_top(seat), 0, 0)
    for heading_name, prefixes, cell in spec["columns"]:
        left, right = CELLS[cell]
        if heading_name is not None:
            heading = need(layout, heading_name)
            heading.place(left, HEADER_TOP, right - left, HEADER_HEIGHT)
            left_align(heading)
            hide(heading)
        for seat in range(rows):
            top = row_top(seat)
            for prefix in prefixes:
                name = "%s%d" % (prefix, seat)
                if seat == 0 and prefix == "ComboBoxPlayer" and seat_zero_player:
                    # in the skirmish room the local player's seat is a text entry, not a combo box
                    name = seat_zero_player
                control = need(layout, name)
                if prefix == "GenericPing":
                    control.place(PING_RIGHT - PING, top + PING_TOP, PING, PING)
                elif prefix == PERSONA_PREFIX:
                    control.place(LEVEL_RIGHT + GAP, top + ROW_BOX_TOP, right - LEVEL_RIGHT - GAP, ROW_BOX_HEIGHT)
                elif prefix in ("ComboBoxPlayer", "StaticTextPlayer"):
                    control.place(left, top + ROW_BOX_TOP, LEVEL_RIGHT - left, ROW_BOX_HEIGHT)
                else:
                    control.place(left, top + ROW_BOX_TOP, right - left, ROW_BOX_HEIGHT)
    return SEATS_BOTTOM


def statement(window, key):
    """A property's value as one line, without its keyword and semicolon."""
    text = window.prop(key)
    if text is None:
        raise KeyError("%s has no %s" % (short(window), key))
    return text.split("=", 1)[1].strip().rstrip(";").strip()


def place_seat_scroll(layout, menu, container, spec):
    """The skirmish room's scroll gadgets, an up and a down button and a slider wearing the art of
    a seat's dropdown list. The buttons start from Back's and the track from the online lobby's chat
    slider, once; every run sets their art and place."""
    named = lambda name: "%s.wnd:%s" % (menu, name)
    art = need(layout, SEAT_SCROLL_ART)
    for name in (SEAT_SCROLL_UP, SEAT_SCROLL_DOWN):
        if layout.find(name) is None:
            button = clone(need(layout, "ButtonBack"), named(name))
            container.children.append(button)
    if layout.find(SEAT_SCROLL_SLIDER) is None:
        menu_name, template_name = SLIDER_TEMPLATE
        template = wndlayout.load(os.path.join(MENUS, menu_name + ".wnd")).find(template_name)
        container.children.append(clone(template, named(SEAT_SCROLL_SLIDER)))

    for name, part in ((SEAT_SCROLL_UP, "UP"), (SEAT_SCROLL_DOWN, "DOWN")):
        button = need(layout, name)
        button.set_prop("STATUS", "ENABLED+IMAGE+NOFOCUS")
        button.set_prop("HEADERTEMPLATE", '"[None]"')
        button.set_prop("TEXT", '""')
        for state in ("ENABLED", "DISABLED", "HILITE"):
            button.set_prop(state + "DRAWDATA", statement(art, "LISTBOX%s%sBUTTONDRAWDATA" % (state, part)))
    slider = need(layout, SEAT_SCROLL_SLIDER)
    slider.set_prop("STATUS", "ENABLED+IMAGE")
    for state in ("ENABLED", "DISABLED", "HILITE"):
        slider.set_prop(state + "DRAWDATA", statement(art, "LISTBOX%sSLIDERDRAWDATA" % state))
        slider.set_prop("SLIDERTHUMB%sDRAWDATA" % state, statement(art, "SLIDERTHUMB%sDRAWDATA" % state))

    # the code draws the track and moves the seats by the wheel and a click on it; the gadgets stay
    # on it, hidden, for the lookups
    left, top, width, height = TRACK
    for name in SEAT_SCROLL:
        need(layout, name).place(left, top, width, height)
        hide(need(layout, name))


def place_map(layout):
    """The map at the column's top at the pickers' wide shape (542 by 278); the code fits a squarer
    map into it. Its name under it with Select Map at the right; the MAP head is the code's."""
    heading = need(layout, "StaticTextMapPreview")
    heading.place(COLUMN_LEFT, HEADER_TOP, INNER_RIGHT - COLUMN_LEFT, HEADER_HEIGHT)
    left_align(heading)
    hide(heading)
    map_window = need(layout, "MapWindow")
    map_window.place(COLUMN_LEFT, MAP_TOP, MAP_WIDTH, MAP_HEIGHT)
    # the start spots are placed by the menu code from the map; only their size is the layout's
    for spot in map_window.children:
        spot.place(COLUMN_LEFT, MAP_TOP, SPOT, SPOT)
    select = need(layout, "ButtonSelectMap")
    select.place(INNER_RIGHT - SELECT_MAP_WIDTH, MAP_CAPTION_TOP, SELECT_MAP_WIDTH, SELECT_MAP_HEIGHT)
    name = need(layout, "TextEntryMapDisplay")
    name.place(COLUMN_LEFT, MAP_CAPTION_TOP, INNER_RIGHT - SELECT_MAP_WIDTH - GAP - COLUMN_LEFT, 30)
    left_align(name)


def place_rules(layout, rules, checks):
    """The rules from RULES_TOP down, the check boxes' lines on the column's foot; the pitch is what
    the rows leave over the count, at most 62. The page covers the column under the RULES head."""
    page = need(layout, "PageLobbySettings")
    page.place(COLUMN_LEFT, RULES_TOP, INNER_RIGHT - COLUMN_LEFT, SEATS_BOTTOM - RULES_TOP)
    page.set_prop("STATUS", "ENABLED+NOFOCUS")
    checks_top = SEATS_BOTTOM - len(checks) * CHECK_LINE
    # the gap between two boxes stays on the 8 px grid: 16 where the rows have room, else 8 with the
    # boxes as tall as what is left (the online room's seven rules over two lines of check boxes)
    room = (checks_top - 4 - RULES_TOP) // len(rules)
    gap = 16 if room >= RULE_PITCH_MOST else 8
    height = min(SETTING_HEIGHT, room - gap)
    pitch = height + gap
    top = RULES_TOP
    for label_name, combo_name in rules:
        label = need(layout, label_name)
        label.place(COLUMN_LEFT, top, SETTING_LABEL_WIDTH, height)
        left_align(label)
        need(layout, combo_name).place(COLUMN_LEFT + SETTING_LABEL_WIDTH, top,
                                       INNER_RIGHT - COLUMN_LEFT - SETTING_LABEL_WIDTH, height)
        top += pitch
    width = (INNER_RIGHT - COLUMN_LEFT - 2 * GAP) // 3
    for line, names in enumerate(checks):
        for index, name in enumerate(names):
            need(layout, name).place(COLUMN_LEFT + index * (width + GAP), checks_top + line * CHECK_LINE,
                                     width, CHECK_LINE)


def place_frame(layout, spec):
    """The panel on frame A's rect, its fill and edge the code's; the title and the header rule stay
    for the code to read and are hidden. Back bottom left, Play / Start the lit key bottom right."""
    container = need(layout, spec["container"])
    container.place(*FRAME)
    container.set_prop("ENABLEDDRAWDATA", draw_data((0, 0, 0, 0), (0, 0, 0, 0)))
    title = need(layout, spec["title"])
    title.place(*TITLE)
    left_align(title)
    hide(title)
    rule = need(layout, spec["rule"])
    rule.place(FRAME[0], RULE_TOP, FRAME[2], 0)
    rule.set_prop("ENABLEDDRAWDATA", draw_data((0, 0, 0, 0), (0, 0, 0, 0)))
    hide(rule)
    need(layout, "ButtonBack").place(INNER_LEFT, BUTTON_TOP, BACK_WIDTH, BUTTON_HEIGHT)
    need(layout, "ButtonStart").place(COLUMN_LEFT, BUTTON_TOP, INNER_RIGHT - COLUMN_LEFT, BUTTON_HEIGHT)


def add_tabs(layout, menu, container):
    """RULES and CHAT where the RULES head stands, cloned from Back once; the code draws them, sizes
    them to their words and swaps the page (InitLobbyTabs)."""
    left = COLUMN_LEFT
    for name, text, width in TABS_LAYOUT:
        tab = layout.find(name)
        if tab is None:
            tab = clone(need(layout, "ButtonBack"), "%s.wnd:%s" % (menu, name))
            container.children.append(tab)
        tab.set_prop("TEXT", '"%s"' % text)
        drop_prop(tab, "TOOLTIPTEXT")
        tab.place(left, RULES_Y, width, TAB_HEIGHT)
        left += width + GAP


def place_chat(layout, listbox):
    """The chat under the tabs, over the rules page it swaps with; the line to write in and SEND at
    the column's foot."""
    entry_top = SEATS_BOTTOM - 6 - CHAT_ENTRY_HEIGHT
    need(layout, listbox).place(COLUMN_LEFT, RULES_TOP, INNER_RIGHT - COLUMN_LEFT, entry_top - GAP - RULES_TOP)
    need(layout, "TextEntryChat").place(COLUMN_LEFT + 8, entry_top, INNER_RIGHT - COLUMN_LEFT - SEND_WIDTH - 24,
                                        CHAT_ENTRY_HEIGHT)
    need(layout, "ButtonEmote").place(INNER_RIGHT - 8 - SEND_WIDTH, entry_top, SEND_WIDTH, CHAT_ENTRY_HEIGHT)
    need(layout, "ButtonEmote").put_prop("TEXT", '"GUI:RoomSend"')


def skirmish_panels(parent):
    """EA left the honours frame and the career stats panel unnamed; the stats panel is the one
    with children."""
    unnamed = [child for child in parent.children if not short(child)]
    info_frames = [child for child in unnamed if not child.children]
    stats_panels = [child for child in unnamed if child.children]
    if len(info_frames) != 1 or len(stats_panels) != 1:
        raise ValueError("expected one info frame and one stats panel in %s, found %d and %d"
                         % (short(parent), len(info_frames), len(stats_panels)))
    return info_frames[0], stats_panels[0]


def add_record_popover(layout, menu, container):
    """The honours frame, its list and the stats panel move into a hidden window of their own, last
    in the container so it draws over the seats, with a title and a close button; Record beside Back
    opens it. Once there, a second run finds them and moves nothing."""
    named = lambda name: "%s.wnd:%s" % (menu, name)
    back = need(layout, "ButtonBack")
    if layout.find("ButtonRecord") is None:
        record = clone(back, named("ButtonRecord"))
        record.put_prop("TEXT", '"%s"' % RECORD_TEXT)
        container.children.insert(container.children.index(back) + 1, record)
    popover = layout.find("RecordPopover")
    if popover is None:
        popover = plain_window(container, named("RecordPopover"), False, POPOVER_COLOR, POPOVER_BORDER)
        popover.set_prop("STATUS", "ENABLED+HIDDEN+NOFOCUS")
        info_frame, stats_panel = skirmish_panels(container)
        listbox = need(layout, "ListboxInfo")
        for window in (info_frame, listbox, stats_panel):
            container.children.remove(window)
        title = clone(need(layout, "StaticTextMapPreview"), named("StaticTextRecordTitle"))
        title.put_prop("TEXT", '"%s"' % RECORD_TEXT)
        close = clone(back, named("ButtonRecordClose"))
        close.put_prop("TEXT", '"%s"' % CLOSE_TEXT)
        popover.children = [title, info_frame, listbox, stats_panel, close]
        container.children.append(popover)
    if layout.find("RecordVeil") is None:
        # the menu code gives it the input that closes the record
        veil = plain_window(container, named("RecordVeil"), False, RECORD_VEIL, (0, 0, 0, 0))
        veil.set_prop("STATUS", "ENABLED+HIDDEN+NOFOCUS")
        container.children.insert(container.children.index(popover), veil)
    return popover


def place_record(layout, menu, container):
    """The battle honours and the career record, side by side in the popover over the seats, the
    veil under it over the whole room."""
    popover = add_record_popover(layout, menu, container)
    need(layout, "RecordVeil").place(*FRAME)
    popover.place(*POPOVER)
    popover.set_prop("ENABLEDDRAWDATA", draw_data(POPOVER_COLOR, POPOVER_BORDER))
    left, top, width, height = POPOVER
    right, bottom = left + width - POPOVER_PADDING, top + height - POPOVER_PADDING
    title = need(layout, "StaticTextRecordTitle")
    title.place(left + POPOVER_PADDING - TEXT_NUDGE, top + POPOVER_PADDING, width // 2, POPOVER_TITLE_HEIGHT)
    left_align(title)
    # a heading inside a panel is ACTIVE, as the options pages' are
    title.set_prop("TEXTCOLOR", "ENABLED: %d %d %d %d, ENABLEDBORDER: 0 0 0 255, DISABLED: 192 192 192 255, "
                   "DISABLEDBORDER: 64 64 64 255, HILITE: 128 128 255 255, HILITEBORDER: 0 0 128 255" % ACTIVE)
    close_top = bottom - BUTTON_HEIGHT
    need(layout, "ButtonRecordClose").place(right - CLOSE_WIDTH, close_top, CLOSE_WIDTH, BUTTON_HEIGHT)

    info_frame, stats_panel = skirmish_panels(popover)
    body_top = top + POPOVER_PADDING + POPOVER_TITLE_HEIGHT + GAP
    body_height = close_top - GAP - body_top
    stats_left = right - STATS_WIDTH
    honours_width = stats_left - 16 - left - POPOVER_PADDING
    for window in (info_frame, need(layout, "ListboxInfo")):
        window.place(left + POPOVER_PADDING, body_top, honours_width, body_height)
    stats_panel.place(stats_left, body_top, STATS_WIDTH, body_height)
    value_left = right - 16 - STAT_VALUE_WIDTH
    for row, (label_name, value_name) in enumerate(STAT_ROWS):
        row_top = body_top + 16 + row * STAT_PITCH
        label = need(layout, label_name)
        # the word ends 8 before its figure (it ended 10 before: the nudge came off its width too)
        label_left = stats_left + 16 - TEXT_NUDGE
        label.place(label_left, row_top, value_left - GAP - label_left, CONTROL_HEIGHT)
        left_align(label)
        need(layout, value_name).place(value_left, row_top, STAT_VALUE_WIDTH, CONTROL_HEIGHT)
    record = need(layout, "ButtonRecord")
    record.place(INNER_LEFT + KEY_PITCH, BUTTON_TOP, BUDDIES_WIDTH, BUTTON_HEIGHT)
    record.set_prop("TEXT", '"%s"' % RECORD_BUTTON_TEXT)


def drop_prop(window, key):
    at = window.prop_index(key)
    if at >= 0:
        end = at
        while ";" not in window.props[end]:
            end += 1
        del window.props[at:end + 1]


def place_presets(layout, menu, container):
    """The Presets button after Record and its popover, standing on the seats' foot over the Presets
    button's corner, last in the container so it draws over the seats; once there, a second run finds
    them and only places them and sets their art."""
    named = lambda name: "%s.wnd:%s" % (menu, name)
    record = need(layout, "ButtonRecord")
    if layout.find("ButtonPresets") is None:
        container.children.insert(container.children.index(record) + 1, clone(record, named("ButtonPresets")))
    popover = layout.find("PresetPopover")
    if popover is None:
        popover = plain_window(container, named("PresetPopover"), False, POPOVER_COLOR, POPOVER_BORDER)
        popover.set_prop("STATUS", "ENABLED+HIDDEN+NOFOCUS")
        popover.children = [clone(need(layout, "StaticTextRecordTitle"), named("StaticTextPresetTitle")),
                            clone(need(layout, "ListboxInfo"), named("ListboxPresets")),
                            clone(need(layout, "ButtonRecordClose"), named("ButtonPresetsClose"))]
        container.children.append(popover)

    # in the header now, drawn by the code as the PRESET field
    button = need(layout, "ButtonPresets")
    button.place(*PRESETS_RECT)
    button.set_prop("TEXT", '"%s"' % PRESET_BUTTON_TEXT)
    button.put_prop("TOOLTIPTEXT", '"%s"' % PRESET_TOOLTIP)

    left, top, width, height = PRESET_POPOVER
    popover.place(left, top, width, height)
    popover.set_prop("ENABLEDDRAWDATA", draw_data(POPOVER_COLOR, POPOVER_BORDER))
    right, bottom = left + width - POPOVER_PADDING, top + height - POPOVER_PADDING
    title = need(layout, "StaticTextPresetTitle")
    title.place(left + POPOVER_PADDING - TEXT_NUDGE, top + POPOVER_PADDING, width // 2, POPOVER_TITLE_HEIGHT)
    title.set_prop("TEXT", '"%s"' % PRESET_TEXT)
    close_top = bottom - BUTTON_HEIGHT
    need(layout, "ButtonPresetsClose").place(right - CLOSE_WIDTH, close_top, CLOSE_WIDTH, BUTTON_HEIGHT)
    # the custom slots' Load, Save and Clear, Close's twins, left to right ahead of it
    for index, (name, text) in enumerate(PRESET_SLOT_BUTTONS):
        button = layout.find(name)
        if button is None:
            button = clone(need(layout, "ButtonPresetsClose"), named(name))
            popover.children.append(button)
        button.set_prop("TEXT", '"%s"' % text)
        button.place(left + POPOVER_PADDING + index * (PRESET_SLOT_BUTTON_WIDTH + GAP), close_top,
                     PRESET_SLOT_BUTTON_WIDTH, BUTTON_HEIGHT)

    # the list in a seat dropdown's open-list art, lettering and scroll bar, one column of text
    art = need(layout, SEAT_SCROLL_ART)
    listbox = need(layout, "ListboxPresets")
    body_top = top + POPOVER_PADDING + POPOVER_TITLE_HEIGHT + GAP
    listbox.place(left + POPOVER_PADDING, body_top, width - 2 * POPOVER_PADDING, close_top - GAP - body_top)
    drop_prop(listbox, "TOOLTIPTEXT")
    for key in ("FONT", "HEADERTEMPLATE", "TEXTCOLOR"):
        listbox.set_prop(key, statement(art, key))
    for state in ("ENABLED", "DISABLED", "HILITE"):
        listbox.set_prop(state + "DRAWDATA", statement(art, "COMBOBOXLISTBOX%sDRAWDATA" % state))
        for part in ("UPBUTTON", "DOWNBUTTON", "SLIDER"):
            key = "LISTBOX%s%sDRAWDATA" % (state, part)
            listbox.put_prop(key, statement(art, key))
        listbox.put_prop("SLIDERTHUMB%sDRAWDATA" % state, statement(art, "SLIDERTHUMB%sDRAWDATA" % state))
    listbox.set_prop("LISTBOXDATA", PRESET_LIST_DATA)


def build(layout, menu):
    spec = ROOMS[menu]
    widen(layout)
    add_combo_column(layout, menu, PERSONA_PREFIX, "ComboBoxPlayerTemplate", PERSONA_TOOLTIP)
    add_handicap_column(layout, menu)
    add_seat_rows(layout, menu, spec)
    # the network rooms take their RULES and CHAT tabs back (add_tabs); the info tab is gone for good
    remove(layout, (TABS if "chat" not in spec else ("TabInfo",))
           + (RETIRED_PERSONA_HEADING, RETIRED_TURBO_BOX, RETIRED_HIDDEN_MAP_BOX))
    add_game_speed_row(layout, menu)
    add_check_box(layout, menu, DAY_NIGHT)
    container = need(layout, spec["container"])
    add_ruler(layout, menu, container)
    add_seat_windows(layout, menu, container, spec)
    place_frame(layout, spec)
    place_seats(layout, spec, "TextEntryPlayerName" if menu == "SkirmishGameOptionsMenu" else None)
    if "chat" not in spec:
        place_seat_scroll(layout, menu, container, spec)
    place_rules(layout, spec["rules"], spec["checks"])
    place_map(layout)
    if "chat" in spec:
        add_tabs(layout, menu, container)
        place_chat(layout, spec["chat"])
    else:
        place_record(layout, menu, container)
        place_presets(layout, menu, container)
    if menu == "GameSpyGameOptionsMenu":
        need(layout, "ButtonCommunicator").place(INNER_LEFT + KEY_PITCH, BUTTON_TOP, BUDDIES_WIDTH,
                                                 BUTTON_HEIGHT)
    return layout


# ---------------------------------------------------------------------------
# selfcheck
# ---------------------------------------------------------------------------

def overlaps(container):
    """Visible controls in one container drawn over each other. The seat rows are placed by the code
    on every update, so their file places are left out; the chat and the rules page swap places by
    their tabs."""
    seat_controls = SEAT_ROW_PREFIXES + ("StaticTextPlayer", "GenericPing", "ButtonAccept", "TextEntryPlayerName")
    visible = [(short(child), child.rect[:4]) for child in container.children
               if short(child) and "HIDDEN" not in (child.prop("STATUS") or "")
               and not short(child).startswith(SEAT_WINDOWS + seat_controls)]
    swapping = {"PageLobbySettings", "ListboxChatWindowLanGame", "ListboxChatWindowGameSpyGameSetup",
                "TextEntryChat", "ButtonEmote"}
    found = []
    for index, (name, (left, top, right, bottom)) in enumerate(visible):
        for other, (other_left, other_top, other_right, other_bottom) in visible[index + 1:]:
            if name in swapping and other in swapping:
                continue
            if left < other_right and other_left < right and top < other_bottom and other_top < bottom:
                found.append("%s and %s overlap" % (name, other))
    return found


def selfcheck():
    problems = []
    for menu, spec in sorted(ROOMS.items()):
        path = os.path.join(MENUS, menu + ".wnd")
        with open(path, "rb") as fp:
            tracked = fp.read().decode("latin-1")
        layout = wndlayout.parse(tracked)
        names = list(NAMED) + [spec["container"], spec["title"]]
        for _heading, prefixes, _width in spec["columns"]:
            names += ["%s%d" % (prefix, seat) for prefix in prefixes for seat in range(seat_rows(spec))
                      if not (seat == 0 and prefix == "ComboBoxPlayer" and menu == "SkirmishGameOptionsMenu")]
        per_seat = NAMED_PER_SEAT + (NAMED_NETWORK_PER_SEAT if "chat" in spec else [])
        # a map has at most eight start spots, however many rows the room has
        names += ["%s%d" % (prefix, seat) for prefix in per_seat for seat in range(SEAT_COUNT)]
        names += ["%s%d" % (prefix, seat) for prefix in NAMED_PER_SEAT if prefix != "ButtonMapStartPosition"
                  for seat in range(SEAT_COUNT, seat_rows(spec))]
        if "chat" not in spec:
            names += NAMED_SKIRMISH
        for rule in spec["rules"]:
            names += list(rule)
        names += [name for line in spec["checks"] for name in line]
        if "chat" in spec:
            names += [name for name, _text, _width in TABS_LAYOUT] + ["TextEntryChat", "ButtonEmote", spec["chat"]]
        for name in names:
            if layout.find(name) is None:
                problems.append("%s.wnd has no %s" % (menu, name))
        unwanted = ("TabInfo",) if "chat" in spec else TABS
        for tab in unwanted:
            if layout.find(tab) is not None:
                problems.append("%s.wnd still has %s; the room has no such tab" % (menu, tab))
        if layout.find(RETIRED_TURBO_BOX) is not None:
            problems.append("%s.wnd still has %s; turbo is gone" % (menu, RETIRED_TURBO_BOX))
        if layout.find(RETIRED_HIDDEN_MAP_BOX) is not None:
            problems.append("%s.wnd still has %s; no room offers Hidden Map" % (menu, RETIRED_HIDDEN_MAP_BOX))
        stale = [short(node) or "(unnamed)" for node in layout.root.walk() if tuple(node.rect[4:]) != WIDE]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first"
                            % (menu, len(stale), stale[0]))
        try:
            rebuilt = build(wndlayout.parse(tracked), menu).text()
            # WND writers use CRLF; Git checkouts may use LF. Compare all content, including final newlines.
            if rebuilt.replace("\r\n", "\n") != tracked.replace("\r\n", "\n"):
                problems.append("%s.wnd is not what lobbyroom_layout.py writes; regenerate it" % menu)
        except (KeyError, ValueError) as error:
            problems.append("%s.wnd no longer builds: %s" % (menu, error))
        container = layout.find(spec["container"])
        if container is not None:
            problems.extend("%s.wnd: %s" % (menu, problem) for problem in overlaps(container))

    for problem in problems:
        print("lobbyroom: %s" % problem)
    if problems:
        return 1
    print("lobbyroom: %d rooms on the 1920x1080 grid" % len(ROOMS))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    source, target = argv[1], argv[2]
    for menu in sorted(ROOMS):
        layout = wndlayout.load(os.path.join(source, menu + ".wnd"))
        out = os.path.join(target, menu + ".wnd")
        wndlayout.save(build(layout, menu), out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
