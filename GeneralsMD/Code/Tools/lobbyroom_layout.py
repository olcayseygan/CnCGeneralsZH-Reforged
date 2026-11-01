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

# Turbo mode replaced the skirmish room's game speed dropdown: a box cloned from the unit limit's,
# which every room has, so it inherits that box's art and lettering.
TURBO_BOX = ("CheckBoxTurbo", "GUI:TurboMode", "TOOLTIP:TurboMode")
GAME_SPEED_DROPDOWN = ("LabelGameSpeed", "ComboBoxGameSpeed")
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
SKIRMISH_RULES = [("StartingCashLabel", "ComboBoxStartingCash"), ("LabelSuperweapons", "ComboBoxSuperweapons"),
                  ("LabelIncomeSharing", "ComboBoxIncomeSharing"), ("LabelSupplyPileLimit", "ComboBoxSupplyPileLimit"),
                  ("LabelTechRespawn", "ComboBoxTechRespawn")]
NETWORK_RULES = SKIRMISH_RULES[:1] + [("LabelPeaceTime", "ComboBoxPeaceTime")] + SKIRMISH_RULES[1:]
SKIRMISH_CHECKS = ["CheckBoxTurbo", "CheckBoxUnitLimit"]
LAN_CHECKS = ["CheckBoxProRules", "CheckBoxUnitLimit", "CheckBoxTurbo"]
GAMESPY_CHECKS = ["CheckBoxUseStats", "CheckBoxLimitArmies"] + LAN_CHECKS

ROOMS = {
    "SkirmishGameOptionsMenu": dict(container="SubParent", rule="Line", title="StaticTextTitle",
                                    columns=SEAT_COLUMNS, rules=SKIRMISH_RULES, checks=SKIRMISH_CHECKS,
                                    row_pitch=ROW_PITCH, row_height=ROW_HEIGHT),
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
NAMED_SKIRMISH = ["ButtonRecord", "ListboxInfo"] + list(RECORD_WINDOWS)
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

def add_turbo_box(layout):
    """Clone the unit limit box into the turbo box beside it, once; a second run finds it there."""
    name, text, tooltip = TURBO_BOX
    if layout.find(name) is not None:
        return
    unit_limit = need(layout, "CheckBoxUnitLimit")
    prefix = unit_limit.name.split(":")[0]
    box = clone(unit_limit, "%s:%s" % (prefix, name))
    box.put_prop("TEXT", '"%s"' % text)
    box.put_prop("TOOLTIPTEXT", '"%s"' % tooltip)
    layout.root.parent_of(unit_limit).children.append(box)


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


def add_seat_windows(layout, menu, container, spec):
    """One card, bar and two swatches a seat, put at the front of the container so they draw under
    the seat's controls. The game draws the swatches' check box frame itself."""
    remove(layout, ["%s%d" % (kind, seat) for kind in SEAT_WINDOWS + RETIRED_SEAT_WINDOWS
                    for seat in range(SEAT_COUNT)])
    template = need(layout, spec["container"])
    named = lambda kind, seat: "%s.wnd:%s%d" % (menu, kind, seat)
    added = {kind: [] for kind in SEAT_WINDOWS}
    for seat in range(SEAT_COUNT):
        for kind in SEAT_WINDOWS:
            card = kind == "SeatCard"
            added[kind].append(plain_window(template, named(kind, seat), False,
                                            CARD_COLOR if card else EMPTY_COLOR,
                                            CARD_BORDER if card else (0, 0, 0, 0)))
    container.children[0:0] = [window for kind in SEAT_WINDOWS for window in added[kind]]


def column_widths(columns):
    """The one column without a width takes what the fixed ones and their gaps leave."""
    fixed = sum(width for _h, _p, width in columns if width is not None)
    gaps = sum(6 if prefixes[0] in SWATCHES else GAP for _h, prefixes, _w in columns[:-1])
    rest = COLUMNS_RIGHT - SEAT_COLUMNS_LEFT - fixed - gaps
    return [rest if width is None else width for _h, _p, width in columns]


def place_seats(layout, spec, seat_zero_player):
    pitch, height = spec["row_pitch"], spec["row_height"]
    for seat in range(SEAT_COUNT):
        top = ROW_TOP + seat * pitch
        need(layout, "SeatCard%d" % seat).place(INNER_LEFT, top, SEAT_RIGHT - INNER_LEFT, height)
        need(layout, "SeatBar%d" % seat).place(INNER_LEFT, top, BAR_WIDTH, height)
        accept = layout.find("ButtonAccept%d" % seat)
        if accept is not None:
            # the menu code still shows and colours it; the row's own colour says ready now
            accept.place(INNER_LEFT, top, 0, 0)

    left = SEAT_COLUMNS_LEFT
    columns = spec["columns"]
    widths = column_widths(columns)
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
        for seat in range(SEAT_COUNT):
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
    if min(widths) <= 0 or left - GAP != COLUMNS_RIGHT:
        raise ValueError("seat columns %s do not fill the seat table to %d" % (widths, COLUMNS_RIGHT))
    return ROW_TOP + (SEAT_COUNT - 1) * pitch + height


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


def place_rules(layout, rules, checks):
    """The rules page stands on the column's foot, as tall as its rows; returns its top."""
    rows = len(rules) + (len(checks) + 1) // 2
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
        need(layout, name).place(left + (index % 2) * (CHECK_WIDTH + GAP), top + (index // 2) * SETTING_PITCH,
                                 CHECK_WIDTH, SETTING_HEIGHT)
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


def build(layout, menu):
    spec = ROOMS[menu]
    widen(layout)
    add_combo_column(layout, menu, PERSONA_PREFIX, "ComboBoxPlayerTemplate", PERSONA_TOOLTIP)
    add_handicap_column(layout, menu)
    remove(layout, TABS + GAME_SPEED_DROPDOWN + (RETIRED_PERSONA_HEADING,))
    add_turbo_box(layout)
    container = need(layout, spec["container"])
    add_ruler(layout, menu, container)
    add_seat_windows(layout, menu, container, spec)
    place_frame(layout, spec)
    seats_bottom = place_seats(layout, spec, "TextEntryPlayerName" if menu == "SkirmishGameOptionsMenu" else None)
    page_top = place_rules(layout, spec["rules"], spec["checks"])
    place_map(layout, page_top - 14 - CONTROL_HEIGHT)
    if "chat" in spec:
        place_chat(layout, spec["chat"], seats_bottom + 16)
    else:
        place_record(layout, menu, container)
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
            pair = sorted((name.rstrip("01234567"), other.rstrip("01234567")))
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
            names += ["%s%d" % (prefix, seat) for prefix in prefixes for seat in range(SEAT_COUNT)
                      if not (seat == 0 and prefix == "ComboBoxPlayer" and menu == "SkirmishGameOptionsMenu")]
        per_seat = NAMED_PER_SEAT + (NAMED_NETWORK_PER_SEAT if "chat" in spec else [])
        names += ["%s%d" % (prefix, seat) for prefix in per_seat for seat in range(SEAT_COUNT)]
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
        stale = [short(node) or "(unnamed)" for node in layout.root.walk() if tuple(node.rect[4:]) != WIDE]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first"
                            % (menu, len(stale), stale[0]))
        try:
            rebuilt = build(wndlayout.parse(tracked), menu).text()
            if rebuilt != tracked:
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
