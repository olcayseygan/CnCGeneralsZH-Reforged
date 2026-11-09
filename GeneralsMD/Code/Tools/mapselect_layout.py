#!/usr/bin/env python3
"""Lay the skirmish, LAN and online map pickers into their room, in place of the seat table.

EA drew the three pickers at 800x600 as a see-through strip over the top of the room. On the rooms'
1920x1080 grid (lobbyroom_layout.py) that strip landed over half the seat table and half the map
column at once. Here the picker swaps panels with the room instead: the map list takes the seat
table's rectangle, its preview takes the room's own map preview's rectangle, and Accept and Cancel
take the row the map's name and Select Map stand on. The room hides those while the picker is open
(the menus' ShowUnderlyingGUIElements lists), so nothing on the right moves or changes size; the
match rules page under it stays where it is.

The geometry is read from the tracked room layouts, so a room laid out again by lobbyroom_layout.py
only needs this script run after it. Every control keeps its name and EA's art; only rectangles
move, plus the panel's colours. The Official / Unofficial radio buttons are drawn as UIStyle tabs by
the menu code (DrawMapListTab in GUIUtil.cpp). A third tab, Random, is cloned in from them, with
the panel it opens in the list's place: a map type list and a size row (RandomMapTab in
GUIUtil.cpp). There is no player row, the map takes its player count from the room, and no New Map
button, since every Accept rolls a fresh seed.

    python mapselect_layout.py <retail Menus dir> <Menus dir out>

reads SkirmishMapSelectMenu.wnd, LanMapSelectMenu.wnd and WOLMapSelectMenu.wnd as WindowZH.big
ships them. Running it on its own output changes nothing.

    python mapselect_layout.py selfcheck

checks the tracked pickers: every window the menu code names is there, the script reproduces the
file byte for byte, everything is authored at 1920x1080, the list stays inside the seat table, and
the preview and the two buttons sit exactly where the room's own preview and Select Map do.
Run by CTest as mapselect_selfcheck. Layouts are not in the multiplayer INI checksum.
"""

import os
import re
import sys

import wndlayout
import lobbyroom_layout as room
from wndlayout import clone

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")

PICKERS = {
    "SkirmishMapSelectMenu": "SkirmishGameOptionsMenu",
    "LanMapSelectMenu": "LanGameOptionsMenu",
    "WOLMapSelectMenu": "GameSpyGameOptionsMenu",
}
# The Random tab: a map type list and a size row, the row being radio buttons drawn as tabs (the menu
# code gives them DrawMapListTab). RandomMapTab in GUIUtil.cpp reads these names.
RANDOM_TAB = "RadioButtonRandomMaps"
RANDOM_SIZES = ["Tiny", "Small", "Medium", "Large", "Huge"]
RANDOM_PANEL = (["StaticTextRandomTemplate", "ListboxRandomTemplate", "StaticTextRandomSize"]
                + ["RadioButtonRandomSize%d" % size for size in range(len(RANDOM_SIZES))])

NAMED = ["ButtonOK", "ButtonBack", "ListboxMap", "RadioButtonSystemMaps", "RadioButtonUserMaps",
         "WinMapPreview", "WinGadgetBorder", RANDOM_TAB] + RANDOM_PANEL + [
         "ButtonMapStartPosition%d" % seat for seat in range(room.SEAT_COUNT)]

PADDING = 12
TITLE_HEIGHT = 32
TAB_HEIGHT = 36
MAP_ROWS = 4096
LABEL_HEIGHT = 32


def box(window):
    """(left, top, width, height) of a window's rectangle."""
    left, top, right, bottom = window.rect[:4]
    return left, top, right - left, bottom - top


def room_geometry(room_layout):
    """The seat table, the map preview and the Select Map button, as the room lays them out."""
    last_card = room.need(room_layout, "SeatCard%d" % (room.SEAT_COUNT - 1))
    table_bottom = last_card.rect[3]
    table = (room.INNER_LEFT, room.HEADER_TOP, room.SEAT_RIGHT - room.INNER_LEFT, table_bottom - room.HEADER_TOP)
    return table, box(room.need(room_layout, "MapWindow")), box(room.need(room_layout, "ButtonSelectMap"))


def title_of(layout):
    """EA left the picker's heading unnamed; it is the one unnamed window."""
    unnamed = [child for child in layout.root.children if not room.short(child)]
    if len(unnamed) != 1:
        raise ValueError("expected one unnamed heading in %s, found %d" % (room.short(layout.root), len(unnamed)))
    return unnamed[0]


def place_row(windows, left, top, width, height):
    """Side by side across the width, GAP apart, the last flush with the right edge."""
    each = (width - room.GAP * (len(windows) - 1)) // len(windows)
    for at, window in enumerate(windows):
        x = left + width - each if at == len(windows) - 1 else left + at * (each + room.GAP)
        window.place(x, top, each, height)


def add_random_tab(layout):
    """Clone the Random tab and its panel from the picker's own controls, so they wear EA's art.
    They are removed and cloned again on every run, which is what makes a run on this script's own
    output come out byte for byte the same as one on the retail file."""
    room.remove(layout, [RANDOM_TAB] + RANDOM_PANEL)
    menu = layout.root.name.split(":")[0]
    named = lambda name: "%s:%s" % (menu, name)

    def hidden(window):
        status = window.prop("STATUS").split("=", 1)[1].strip().rstrip(";")
        window.set_prop("STATUS", status + "+HIDDEN")
        return window

    def label(name, text):
        window = hidden(clone(title_of(layout), named(name)))
        window.set_prop("TEXT", '"%s"' % text)
        return window

    def radio(name, text, group):
        window = hidden(clone(room.need(layout, "RadioButtonUserMaps"), named(name)))
        window.set_prop("TEXT", '"%s"' % text)
        window.set_prop("RADIOBUTTONDATA", "GROUP: %d" % group)
        return window

    tab = clone(room.need(layout, "RadioButtonUserMaps"), named(RANDOM_TAB))
    tab.set_prop("TEXT", '"GUI:Random"')
    templates = hidden(clone(room.need(layout, "ListboxMap"), named("ListboxRandomTemplate")))
    windows = ([tab, label("StaticTextRandomTemplate", "GUI:RandomMapTemplate"), templates,
                label("StaticTextRandomSize", "GUI:RandomMapSize")]
               + [radio("RadioButtonRandomSize%d" % size, "GUI:RandomMapSize%s" % name, 2)
                  for size, name in enumerate(RANDOM_SIZES)])
    layout.root.children.extend(windows)


def place_random_panel(layout, area):
    """The map type list down the left half of the list's rectangle; the size row at the top of the
    right half."""
    left, top, width, height = area
    column = (width - 2 * room.GAP) // 2
    right = left + width - column
    find = lambda name: room.need(layout, name)

    find("StaticTextRandomTemplate").place(left - room.TEXT_NUDGE, top, column, LABEL_HEIGHT)
    list_top = top + LABEL_HEIGHT + room.GAP
    find("ListboxRandomTemplate").place(left, list_top, column, top + height - list_top)

    find("StaticTextRandomSize").place(right - room.TEXT_NUDGE, top, column, LABEL_HEIGHT)
    place_row([find("RadioButtonRandomSize%d" % size) for size in range(len(RANDOM_SIZES))],
              right, list_top, column, TAB_HEIGHT)


def build(layout, room_layout):
    table, preview, select = room_geometry(room_layout)
    left, top, width, height = table
    select_left, select_top, select_width, select_height = select
    cancel_left = room.COLUMN_LEFT

    # every window is placed below; this only moves them onto the 1920x1080 grid first
    sx, sy = room.WIDE[0] / room.OLD[0], room.WIDE[1] / room.OLD[1]
    for node in layout.root.walk():
        l, t, r, b, cw, ch = node.rect
        if (cw, ch) == room.OLD:
            node.rect = (round(l * sx), round(t * sy), round(r * sx), round(b * sy)) + room.WIDE

    # the parent spans the seat table and the map column down to the buttons; it stays see-through
    root = layout.root
    root.place(left, top, room.INNER_RIGHT - left, max(top + height, select_top + select_height) - top)

    border = room.need(layout, "WinGadgetBorder")
    border.place(*table)
    border.set_prop("ENABLEDDRAWDATA", room.draw_data(room.CARD, room.FRAME_COLOR))

    inner_left, inner_width = left + PADDING, width - 2 * PADDING
    title = title_of(layout)
    title.place(inner_left - room.TEXT_NUDGE, top + PADDING, inner_width // 2, TITLE_HEIGHT)
    room.left_align(title)
    # a heading inside a panel is ACTIVE, as the record's is
    title.set_prop("TEXTCOLOR", "ENABLED: %d %d %d %d, ENABLEDBORDER: 0 0 0 255, DISABLED: 192 192 192 255, "
                   "DISABLEDBORDER: 64 64 64 255, HILITE: 128 128 255 255, HILITEBORDER: 0 0 128 255" % room.ACTIVE)

    tab_top = top + PADDING + TITLE_HEIGHT + room.GAP
    list_top = tab_top + TAB_HEIGHT + room.GAP
    listbox = room.need(layout, "ListboxMap")
    listbox.place(inner_left, list_top, inner_width, top + height - PADDING - list_top)
    # EA's 100 rows (300 in skirmish) with AUTOPURGE 0 dropped every map past them without a word;
    # the list fills 2-player maps first, so a big Maps folder lost every 6 to 8 player map
    at = listbox.prop_index("LISTBOXDATA")
    listbox.props[at] = re.sub(r"LENGTH: \d+", "LENGTH: %d" % MAP_ROWS, listbox.props[at])

    map_window = room.need(layout, "WinMapPreview")
    map_window.place(*preview)
    # the menu code places the start spots on the map; only their size is the layout's
    for spot in map_window.children:
        spot.place(preview[0], preview[1], room.SPOT, room.SPOT)

    room.need(layout, "ButtonOK").place(*select)
    room.need(layout, "ButtonBack").place(cancel_left, select_top, select_width, select_height)
    if cancel_left + select_width + room.GAP > select_left:
        raise ValueError("Cancel at %d does not fit beside Accept at %d" % (cancel_left, select_left))

    # cloned last, from controls whose art is final by now; the Random panel takes the list's place
    add_random_tab(layout)
    place_row([room.need(layout, name) for name in ("RadioButtonSystemMaps", "RadioButtonUserMaps", RANDOM_TAB)],
              inner_left, tab_top, inner_width, TAB_HEIGHT)
    place_random_panel(layout, box(listbox))
    return layout


def load_room(menu):
    return wndlayout.load(os.path.join(MENUS, PICKERS[menu] + ".wnd"))


# ---------------------------------------------------------------------------
# selfcheck
# ---------------------------------------------------------------------------

def inside(inner, outer):
    left, top, width, height = inner
    o_left, o_top, o_width, o_height = outer
    return left >= o_left and top >= o_top and left + width <= o_left + o_width and top + height <= o_top + o_height


def selfcheck():
    problems = []
    for menu in sorted(PICKERS):
        path = os.path.join(MENUS, menu + ".wnd")
        if not os.path.exists(path):
            problems.append("%s.wnd is not tracked" % menu)
            continue
        with open(path, "rb") as fp:
            tracked = fp.read().decode("latin-1")
        layout = wndlayout.parse(tracked)
        missing = [name for name in NAMED if layout.find(name) is None]
        problems.extend("%s.wnd has no %s" % (menu, name) for name in missing)
        stale = [room.short(node) or "(unnamed)" for node in layout.root.walk() if tuple(node.rect[4:]) != room.WIDE]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first" % (menu, len(stale), stale[0]))
        try:
            rebuilt = build(wndlayout.parse(tracked), load_room(menu)).text()
            # WND writers use CRLF; Git checkouts may use LF. Compare all content, including final newlines.
            if rebuilt.replace("\r\n", "\n") != tracked.replace("\r\n", "\n"):
                problems.append("%s.wnd is not what mapselect_layout.py writes from %s.wnd; regenerate it"
                                % (menu, PICKERS[menu]))
        except (KeyError, ValueError) as error:
            problems.append("%s.wnd no longer builds: %s" % (menu, error))
        if missing:
            continue
        table, preview, select = room_geometry(load_room(menu))
        for name in ["ListboxMap", "RadioButtonSystemMaps", "RadioButtonUserMaps", RANDOM_TAB] + RANDOM_PANEL:
            if not inside(box(layout.find(name)), table):
                problems.append("%s.wnd: %s runs out of the seat table" % (menu, name))
        if box(layout.find("WinMapPreview")) != preview:
            problems.append("%s.wnd: the preview is not where %s.wnd draws its map" % (menu, PICKERS[menu]))
        if box(layout.find("ButtonOK")) != select:
            problems.append("%s.wnd: Accept is not where %s.wnd has Select Map" % (menu, PICKERS[menu]))
        for child in layout.root.children:
            if not inside(box(child), box(layout.root)):
                problems.append("%s.wnd: %s lies outside the picker" % (menu, room.short(child) or "(unnamed)"))

    for problem in problems:
        print("mapselect: %s" % problem)
    if problems:
        return 1
    print("mapselect: %d pickers in their rooms' seat tables" % len(PICKERS))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    source, target = argv[1], argv[2]
    for menu in sorted(PICKERS):
        layout = wndlayout.load(os.path.join(source, menu + ".wnd"))
        out = os.path.join(target, menu + ".wnd")
        wndlayout.save(build(layout, load_room(menu)), out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
