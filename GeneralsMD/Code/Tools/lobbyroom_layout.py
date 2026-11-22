#!/usr/bin/env python3
"""Lay the skirmish, LAN and online staging rooms out on one grid, authored at 1920x1080.

EA drew the three rooms at 800x600 and the shell stretched them to the screen, x and y apart, so on
a 16:9 monitor every emblem, start spot and map preview came out a third wider than tall. These
rooms are authored at 1920x1080 instead, and the window loader fits a shell screen authored wider
than 4:3 at one scale (GameWindowManagerScript.cpp, parseScreenRect): exact on 16:9, twice that on
4K, centred between bands of its own backdrop on 4:3 and 16:10.

One grid drives all three rooms. The seat table takes the left of the panel, one row a seat: a
colour bar, the player (or the computer's level) and the computer's personality, the army, a colour
swatch and the colour, a team swatch and the team, the handicap; the swatches are the rules page's
check boxes' size, framed like them. The personality column has no heading: only a computer's row
fills it, and on every other row the menu code widens the player box over it (InitAIPersonaComboBoxes in
GUIUtil.cpp). A player who is
ready turns his row green, so the network rooms need no status column; their ready lights are kept
at no size, since the menu code still sets them. The online room puts a ping light ahead of the row.
The right column is the map preview, the map's name and Select Map, and the match rules page, always
open: the Info / Chat / Lobby Settings tabs are gone. In the skirmish room the seats run down to the
buttons, and the battle honours and career record open over them from the Record button beside Back,
in a window of their own that starts hidden, over a veil that dims the room and takes its clicks;
the network rooms keep their chat under the seats. Back
sits bottom left, Play / Start bottom right; the online room's buddies button beside Back.

Every control keeps its name and art; only its rectangle moves, and the record's three windows move
into the popover. Windows this script adds (SeatCard, SeatBar, SeatSwatch, SeatTeam, one a seat)
take no input and are filled by UpdateLobbySeatVisuals in GUIUtil.cpp. A layout still at 800x600 is first scaled to the
1920x1080 grid the way the stretch drew it, so a window this script does not place stays where it
appeared before.

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

# The score screen's blue ruler frame (MappedImage MainMenuRuler, the art EA's rooms and score screen
# stood in) drawn over the whole 1920x1080 grid: its bright line runs at 96,72 to 1824,1008 and the
# art is clear inside it. The panel sits on that line, as the score screen's does.
RULER_IMAGE = "MainMenuRuler"
RULER_WINDOW = "RulerFrame"
# the panel and its two tracks
FRAME = (96, 72, 1728, 936)
INNER_LEFT, INNER_RIGHT = 124, 1796
SEAT_RIGHT = 1284
COLUMN_LEFT = INNER_RIGHT - 488   # the map and rules column
# a static text draws its first glyph a few pixels inside its rectangle
TEXT_NUDGE = 10
TITLE = (INNER_LEFT - TEXT_NUDGE, 86, 900, 52)
RULE_TOP = 148
HEADER_TOP, HEADER_HEIGHT = 158, 32
ROW_TOP = 196
CONTROL_HEIGHT = 40
# a seat card three pixels taller than its combo boxes top and bottom, six between cards
ROW_HEIGHT = CONTROL_HEIGHT + 6
ROW_PITCH = ROW_HEIGHT + 6
# the last column stops as far inside the card's right edge as the combos sit inside its top
COLUMNS_RIGHT = SEAT_RIGHT - (ROW_HEIGHT - CONTROL_HEIGHT) // 2
GAP = 8

# The skirmish room holds every seat the game has (MAX_SLOTS, NetworkDefs.h). It has no chat, so its
# rows run down to the foot of the rules page, as many as fit (SKIRMISH_SEAT_ROWS, cloned from the
# eighth); the first row is always the local player's, and the rows under it scroll over the rest
# with a scroll bar at the cards' right, the buttons and track of the room's own list boxes (a seat's
# dropdown lists, whose art the layout copies). Its cards stop short of the bar; the network rooms
# keep eight rows and no bar.
SCROLL_WIDTH, SCROLL_BUTTON_HEIGHT = 21, 22
SEAT_ROW_PREFIXES = ("ComboBoxPlayer", "ComboBoxColor", "ComboBoxPlayerTemplate", "ComboBoxTeam",
                     "ComboBoxAIPersona", "ComboBoxHandicap")
SEAT_SCROLL_UP, SEAT_SCROLL_DOWN, SEAT_SCROLL_SLIDER = "ButtonSeatScrollUp", "ButtonSeatScrollDown", "SliderSeatScroll"
SEAT_SCROLL = (SEAT_SCROLL_UP, SEAT_SCROLL_DOWN, SEAT_SCROLL_SLIDER)
SEAT_SCROLL_ART = "ComboBoxTeam1"
SLIDER_TEMPLATE = ("WOLCustomLobby", "SliderChatAdjust")
SCROLL_SEAT_RIGHT = SEAT_RIGHT - SCROLL_WIDTH - 6

# a seat row: colour bar, then the columns; the bar's fill is drawn a pixel inside each edge, so
# eight here is UIStyle's six pixel stripe on screen
BAR_WIDTH = 8
SEAT_COLUMNS_LEFT = INNER_LEFT + BAR_WIDTH + 16
# the rules page's check boxes draw a 26 pixel box at 1080; the swatches match it
SWATCH = 26
PING = 24

MAP_TOP = ROW_TOP
# "HARITA SEC" in the button's own face needs about 220 at 1080
SELECT_MAP_WIDTH = 240
PAGE_BOTTOM = 920
PAGE_PADDING = 12
SETTING_PITCH, SETTING_HEIGHT = 38, 34
SETTING_LABEL_WIDTH = 170
CHECK_WIDTH = 228
SPOT = 28

UNDER_SEATS_BOTTOM = 916
SKIRMISH_SEAT_ROWS = (PAGE_BOTTOM - ROW_TOP - ROW_HEIGHT) // ROW_PITCH + 1
BUTTON_TOP, BUTTON_HEIGHT = 932, 56
BACK_WIDTH, BUDDIES_WIDTH = 300, 260
EMOTE_WIDTH = 64
STATS_WIDTH = 372
STAT_PITCH, STAT_VALUE_WIDTH = 40, 64

# the skirmish room's record, over the seats
# as tall as two rows of medals and the four career lines need; the veil dims whatever it covers
POPOVER = (INNER_LEFT, HEADER_TOP, SEAT_RIGHT - INNER_LEFT, 416)
POPOVER_PADDING = 24
POPOVER_TITLE_HEIGHT = 44
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
# as tall as the seat table, its presets grouped under a heading a seat count
PRESET_POPOVER_WIDTH, PRESET_POPOVER_HEIGHT = 760, UNDER_SEATS_BOTTOM - HEADER_TOP
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

# (heading, control prefixes, width); None for a column without a heading, a width of None for the
# column that takes what the others leave. The swatches and the ping light are fixed-size squares
# centred in the row.
# Widths measured, not guessed: the longest English, Turkish or German entry each box can show,
# in Arial at the 19 pixels the rooms draw at 1080p, plus 10 of inset and the 40 pixel arrow.
# Player "Medium AI" 92: the box is this narrow only on a computer's row, where the levels are not
# translated (SlotStateName), and its open list has no arrow beside "Geschlossen" 114. Personality
# "Hava Kuvvetleri" 136, army "ABD Hava Kuvvetleri Generali" 259, colour "Camgöbeği" 100, team
# "Keins" 49, handicap "100%" 50. The army's rest is 342 here and 310 online. The handicap box is
# wider than its text so that its heading, 115 at the 27 pixels column heads measure, ends about
# over the seat card's edge.
PLAYER_WIDTH = 142
SEAT_COLUMNS = [
    ("StaticTextPlayers", ("ComboBoxPlayer",), PLAYER_WIDTH),
    (None, (PERSONA_PREFIX,), 186),
    ("StaticTextFaction", ("ComboBoxPlayerTemplate",), None),
    ("StaticTextColor", ("SeatSwatch",), SWATCH),
    (None, ("ComboBoxColor",), 150),
    ("StaticTextTeam", ("SeatTeam",), SWATCH),
    (None, ("ComboBoxTeam",), 99),
    (HANDICAP_HEADING, (HANDICAP_PREFIX,), 110),
]
# A seat's player name in the online room is a static text over its combo, as EA drew it.
GAMESPY_COLUMNS = [(None, ("GenericPing",), PING),
                   ("StaticTextPlayers", ("ComboBoxPlayer", "StaticTextPlayer"), PLAYER_WIDTH)] + SEAT_COLUMNS[1:]
SQUARES = {"SeatSwatch": SWATCH, "SeatTeam": SWATCH, "GenericPing": PING}
# a swatch and the box after it share one heading
SWATCHES = ("SeatSwatch", "SeatTeam")

# The rules page, one column: (label, combo) rows, then the check boxes two to a row.
MATCH_RULES = [("StartingCashLabel", "ComboBoxStartingCash"), ("LabelSuperweapons", "ComboBoxSuperweapons"),
               ("LabelIncomeSharing", "ComboBoxIncomeSharing"), ("LabelSupplyPileLimit", "ComboBoxSupplyPileLimit"),
               ("LabelTechRespawn", "ComboBoxTechRespawn")]
SKIRMISH_RULES = [GAME_SPEED_DROPDOWN] + MATCH_RULES
NETWORK_RULES = [GAME_SPEED_DROPDOWN] + MATCH_RULES[:1] + [("LabelPeaceTime", "ComboBoxPeaceTime")] + MATCH_RULES[1:]
# The Hidden Map box in all three rooms, cloned from the unit limit box so it wears that box's art;
# the menu code ticks it from the game's start shroud (SH in the options string). It shares the unit
# limit's row in the skirmish room and opens a row of its own in the network rooms.
START_SHROUDED = ("CheckBoxStartShrouded", "GUI:StartShrouded", "TOOLTIP:StartShrouded")
# The day and night cycle's box (DN in the options string), cloned the same way, after Hidden Map.
DAY_NIGHT = ("CheckBoxDayNight", "GUI:DayNight", "TOOLTIP:DayNight")
SKIRMISH_CHECKS = ["CheckBoxUnitLimit", START_SHROUDED[0], DAY_NIGHT[0]]
LAN_CHECKS = ["CheckBoxProRules", "CheckBoxUnitLimit", START_SHROUDED[0], DAY_NIGHT[0]]
GAMESPY_CHECKS = ["CheckBoxUseStats", "CheckBoxLimitArmies"] + LAN_CHECKS

ROOMS = {
    "SkirmishGameOptionsMenu": dict(container="SubParent", rule="Line", title="StaticTextTitle",
                                    columns=SEAT_COLUMNS, rules=SKIRMISH_RULES, checks=SKIRMISH_CHECKS,
                                    checks_per_row=3,  # three boxes in one row, so the map keeps its size
                                    row_pitch=ROW_PITCH, row_height=ROW_HEIGHT, seat_right=SCROLL_SEAT_RIGHT,
                                    seat_rows=SKIRMISH_SEAT_ROWS),
    "LanGameOptionsMenu": dict(container="GadgetParent", rule="LINE", title="StaticTextTitle",
                               columns=SEAT_COLUMNS, rules=NETWORK_RULES, checks=LAN_CHECKS,
                               row_pitch=ROW_PITCH, row_height=ROW_HEIGHT, chat="ListboxChatWindowLanGame"),
    "GameSpyGameOptionsMenu": dict(container="FadeParent", rule="LINE", title="StaticTextGameName",
                                   columns=GAMESPY_COLUMNS, rules=NETWORK_RULES, checks=GAMESPY_CHECKS,
                                   row_pitch=ROW_PITCH, row_height=ROW_HEIGHT,
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


def add_start_shrouded_box(layout, menu, spec=START_SHROUDED, after="CheckBoxUnitLimit"):
    """Clone the unit limit box into the Hidden Map box (or another box of that kind, put after
    `after`), once; a second run finds it there."""
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
    """The score screen's ruler frame over the whole grid, first under the root so it draws beneath
    the panel; fitted with the rest, so it hugs the panel on any screen. A second run finds it."""
    if layout.find(RULER_WINDOW) is None:
        ruler = plain_window(container, "%s.wnd:%s" % (menu, RULER_WINDOW), True, (0, 0, 0, 255), (0, 0, 0, 255))
        layout.root.children.insert(0, ruler)
    ruler = need(layout, RULER_WINDOW)
    ruler.place(0, 0, WIDE[0], WIDE[1])
    ruler.set_prop("ENABLEDDRAWDATA", draw_data((0, 0, 0, 255), (0, 0, 0, 255), RULER_IMAGE))


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
    """One card, bar and two swatches a seat, put at the front of the container so they draw under
    the seat's controls. The game draws the swatches' check box frame itself."""
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
    container.children[0:0] = [window for kind in SEAT_WINDOWS for window in added[kind]]


def columns_right(spec):
    """Where a room's last seat column stops, as far inside its cards' right edge as COLUMNS_RIGHT."""
    return spec.get("seat_right", SEAT_RIGHT) - (SEAT_RIGHT - COLUMNS_RIGHT)


def column_widths(columns, right=COLUMNS_RIGHT):
    """The one column without a width takes what the fixed ones and their gaps leave."""
    fixed = sum(width for _h, _p, width in columns if width is not None)
    gaps = sum(6 if prefixes[0] in SWATCHES else GAP for _h, prefixes, _w in columns[:-1])
    rest = right - SEAT_COLUMNS_LEFT - fixed - gaps
    return [rest if width is None else width for _h, _p, width in columns]


def place_seats(layout, spec, seat_zero_player):
    pitch, height = spec["row_pitch"], spec["row_height"]
    seat_right = spec.get("seat_right", SEAT_RIGHT)
    right = columns_right(spec)
    rows = seat_rows(spec)
    for seat in range(rows):
        top = ROW_TOP + seat * pitch
        need(layout, "SeatCard%d" % seat).place(INNER_LEFT, top, seat_right - INNER_LEFT, height)
        need(layout, "SeatBar%d" % seat).place(INNER_LEFT, top, BAR_WIDTH, height)
        accept = layout.find("ButtonAccept%d" % seat)
        if accept is not None:
            # the menu code still shows and colours it; the row's own colour says ready now
            accept.place(INNER_LEFT, top, 0, 0)

    left = SEAT_COLUMNS_LEFT
    columns = spec["columns"]
    widths = column_widths(columns, right)
    for index, (heading_name, prefixes, _width) in enumerate(columns):
        width = widths[index]
        if heading_name is not None:
            heading = need(layout, heading_name)
            span = width + (6 + widths[index + 1] if prefixes[0] in SWATCHES else 0)
            if index == len(columns) - 1:
                # the word is wider than its box, and a static text wraps and clips to its rectangle;
                # nothing stands between it and the map's heading
                span = COLUMN_LEFT - left
            heading.place(left - TEXT_NUDGE, HEADER_TOP, span, HEADER_HEIGHT)
            left_align(heading)
        for seat in range(rows):
            top = ROW_TOP + seat * pitch
            for prefix in prefixes:
                name = "%s%d" % (prefix, seat)
                if seat == 0 and prefix == "ComboBoxPlayer" and seat_zero_player:
                    # in the skirmish room the local player's seat is a text entry, not a combo box
                    name = seat_zero_player
                control = need(layout, name)
                if prefix in SQUARES:
                    size = SQUARES[prefix]
                    control.place(left + (width - size) // 2, top + (height - size) // 2, size, size)
                else:
                    control.place(left, top + (height - CONTROL_HEIGHT) // 2, width, CONTROL_HEIGHT)
        left += width + (6 if prefixes[0] in SWATCHES else GAP)
    if min(widths) <= 0 or left - GAP != right:
        raise ValueError("seat columns %s do not fill the seat table to %d" % (widths, right))
    return ROW_TOP + (rows - 1) * pitch + height


def statement(window, key):
    """A property's value as one line, without its keyword and semicolon."""
    text = window.prop(key)
    if text is None:
        raise KeyError("%s has no %s" % (short(window), key))
    return text.split("=", 1)[1].strip().rstrip(";").strip()


def place_seat_scroll(layout, menu, container, spec):
    """The skirmish room's scroll bar beside the rows under the local player's: an up and a
    down button and the track between them where a list box puts its own, wearing the art of a
    seat's dropdown list. The buttons start from Back's and the track from the online lobby's chat
    slider, once; every run sets their art and place. The menu code sets the range."""
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

    pitch, height = spec["row_pitch"], spec["row_height"]
    left = spec["seat_right"] + 6
    top = ROW_TOP + pitch
    bottom = ROW_TOP + (seat_rows(spec) - 1) * pitch + height
    need(layout, SEAT_SCROLL_UP).place(left, top, SCROLL_WIDTH, SCROLL_BUTTON_HEIGHT)
    need(layout, SEAT_SCROLL_DOWN).place(left, bottom - SCROLL_BUTTON_HEIGHT, SCROLL_WIDTH, SCROLL_BUTTON_HEIGHT)
    slider.place(left, top + SCROLL_BUTTON_HEIGHT + 1, SCROLL_WIDTH, bottom - top - 2 * SCROLL_BUTTON_HEIGHT - 2)


def place_map(layout, caption_top):
    """The map takes whatever the rules page leaves of the column, square, up to the column's width."""
    heading = need(layout, "StaticTextMapPreview")
    heading.place(COLUMN_LEFT - TEXT_NUDGE, HEADER_TOP, INNER_RIGHT - COLUMN_LEFT + TEXT_NUDGE, HEADER_HEIGHT)
    left_align(heading)
    size = min(INNER_RIGHT - COLUMN_LEFT, caption_top - 12 - MAP_TOP)
    map_left = COLUMN_LEFT + (INNER_RIGHT - COLUMN_LEFT - size) // 2
    map_window = need(layout, "MapWindow")
    map_window.place(map_left, MAP_TOP, size, size)
    # the start spots are placed by the menu code from the map; only their size is the layout's
    for spot in map_window.children:
        spot.place(map_left, MAP_TOP, SPOT, SPOT)
    select = need(layout, "ButtonSelectMap")
    select.place(INNER_RIGHT - SELECT_MAP_WIDTH, caption_top - 2, SELECT_MAP_WIDTH, 44)
    name = need(layout, "TextEntryMapDisplay")
    name.place(COLUMN_LEFT - TEXT_NUDGE, caption_top, INNER_RIGHT - SELECT_MAP_WIDTH - GAP - COLUMN_LEFT, CONTROL_HEIGHT)
    left_align(name)


def place_rules(layout, rules, checks, per_row=2):
    """The rules page stands on the column's foot, as tall as its rows; returns its top. The check
    boxes go per_row to a row across the two-box width."""
    rows = len(rules) + (len(checks) + per_row - 1) // per_row
    check_width = (2 * CHECK_WIDTH + GAP - (per_row - 1) * GAP) // per_row
    page_top = PAGE_BOTTOM - 2 * PAGE_PADDING - rows * SETTING_PITCH + 4
    page = need(layout, "PageLobbySettings")
    page.place(COLUMN_LEFT, page_top, INNER_RIGHT - COLUMN_LEFT, PAGE_BOTTOM - page_top)
    page.set_prop("STATUS", "ENABLED+NOFOCUS")
    left = COLUMN_LEFT + PAGE_PADDING
    top = page_top + PAGE_PADDING
    for label_name, combo_name in rules:
        label = need(layout, label_name)
        label.place(left - TEXT_NUDGE, top, SETTING_LABEL_WIDTH, SETTING_HEIGHT)
        left_align(label)
        need(layout, combo_name).place(left + SETTING_LABEL_WIDTH, top,
                                       INNER_RIGHT - PAGE_PADDING - left - SETTING_LABEL_WIDTH, SETTING_HEIGHT)
        top += SETTING_PITCH
    for index, name in enumerate(checks):
        need(layout, name).place(left + (index % per_row) * (check_width + GAP), top + (index // per_row) * SETTING_PITCH,
                                 check_width, SETTING_HEIGHT)
    return page_top


def place_frame(layout, spec):
    """EA framed the skirmish panel and drew its rule in FRAME blue, but left the network rooms'
    panels unframed under a paler rule; all three take the skirmish room's."""
    container = need(layout, spec["container"])
    container.place(*FRAME)
    container.set_prop("ENABLEDDRAWDATA", draw_data(PANEL, FRAME_COLOR))
    title = need(layout, spec["title"])
    title.place(*TITLE)
    left_align(title)
    rule = need(layout, spec["rule"])
    rule.place(FRAME[0], RULE_TOP, FRAME[2], 0)
    rule.set_prop("ENABLEDDRAWDATA", draw_data((0, 0, 0, 0), FRAME_COLOR))
    need(layout, "ButtonBack").place(INNER_LEFT, BUTTON_TOP, BACK_WIDTH, BUTTON_HEIGHT)
    need(layout, "ButtonStart").place(COLUMN_LEFT, BUTTON_TOP, INNER_RIGHT - COLUMN_LEFT, BUTTON_HEIGHT)


def place_chat(layout, listbox, top):
    entry_top = UNDER_SEATS_BOTTOM - 44
    need(layout, listbox).place(INNER_LEFT, top, SEAT_RIGHT - INNER_LEFT, entry_top - GAP - top)
    need(layout, "TextEntryChat").place(INNER_LEFT, entry_top, SEAT_RIGHT - INNER_LEFT - EMOTE_WIDTH - GAP, 44)
    need(layout, "ButtonEmote").place(SEAT_RIGHT - EMOTE_WIDTH, entry_top, EMOTE_WIDTH, 44)


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
        label.place(stats_left + 16 - TEXT_NUDGE, row_top, value_left - stats_left - 16, CONTROL_HEIGHT)
        left_align(label)
        need(layout, value_name).place(value_left, row_top, STAT_VALUE_WIDTH, CONTROL_HEIGHT)
    record = need(layout, "ButtonRecord")
    record.place(INNER_LEFT + BACK_WIDTH + 16, BUTTON_TOP, BUDDIES_WIDTH, BUTTON_HEIGHT)
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

    button = need(layout, "ButtonPresets")
    button.place(INNER_LEFT + BACK_WIDTH + 16 + BUDDIES_WIDTH + 16, BUTTON_TOP, BUDDIES_WIDTH, BUTTON_HEIGHT)
    button.set_prop("TEXT", '"%s"' % PRESET_BUTTON_TEXT)
    button.put_prop("TOOLTIPTEXT", '"%s"' % PRESET_TOOLTIP)

    left, top = INNER_LEFT, UNDER_SEATS_BOTTOM - PRESET_POPOVER_HEIGHT
    width, height = PRESET_POPOVER_WIDTH, PRESET_POPOVER_HEIGHT
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
    remove(layout, TABS + (RETIRED_PERSONA_HEADING, RETIRED_TURBO_BOX))
    add_game_speed_row(layout, menu)
    add_start_shrouded_box(layout, menu)
    add_start_shrouded_box(layout, menu, DAY_NIGHT, START_SHROUDED[0])
    container = need(layout, spec["container"])
    add_ruler(layout, menu, container)
    add_seat_windows(layout, menu, container, spec)
    place_frame(layout, spec)
    seats_bottom = place_seats(layout, spec, "TextEntryPlayerName" if menu == "SkirmishGameOptionsMenu" else None)
    if "seat_right" in spec:
        place_seat_scroll(layout, menu, container, spec)
    page_top = place_rules(layout, spec["rules"], spec["checks"], spec.get("checks_per_row", 2))
    place_map(layout, page_top - 14 - CONTROL_HEIGHT)
    if "chat" in spec:
        place_chat(layout, spec["chat"], seats_bottom + 16)
    else:
        place_record(layout, menu, container)
        place_presets(layout, menu, container)
    if menu == "GameSpyGameOptionsMenu":
        need(layout, "ButtonCommunicator").place(INNER_LEFT + BACK_WIDTH + 16, BUTTON_TOP, BUDDIES_WIDTH,
                                                 BUTTON_HEIGHT)
    return layout


# ---------------------------------------------------------------------------
# selfcheck
# ---------------------------------------------------------------------------

def overlaps(container):
    """Visible controls in one container drawn over each other. The seat windows lie under or inside
    a seat's row on purpose, and the online room's player name is a static text over its combo."""
    visible = [(short(child), child.rect[:4]) for child in container.children
               if short(child) and "HIDDEN" not in (child.prop("STATUS") or "")
               and not short(child).startswith(SEAT_WINDOWS)]
    found = []
    for index, (name, (left, top, right, bottom)) in enumerate(visible):
        for other, (other_left, other_top, other_right, other_bottom) in visible[index + 1:]:
            pair = sorted((name.rstrip("0123456789"), other.rstrip("0123456789")))
            if pair == ["ComboBoxPlayer", "StaticTextPlayer"] and name[-1] == other[-1]:
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
        names += spec["checks"]
        for name in names:
            if layout.find(name) is None:
                problems.append("%s.wnd has no %s" % (menu, name))
        for tab in TABS:
            if layout.find(tab) is not None:
                problems.append("%s.wnd still has %s; the rules page is always open" % (menu, tab))
        if layout.find(RETIRED_TURBO_BOX) is not None:
            problems.append("%s.wnd still has %s; turbo is gone" % (menu, RETIRED_TURBO_BOX))
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
