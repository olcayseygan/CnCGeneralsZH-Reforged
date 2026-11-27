#!/usr/bin/env python3
"""Lay the skirmish, LAN and online map pickers into their room, in place of the seat table.

EA drew the three pickers at 800x600 as a see-through strip over the top of the room. On the rooms'
1920x1080 grid (lobbyroom_layout.py) that strip landed over half the seat table and half the map
column at once. Here the picker swaps panels with the room instead, in the menu language of
experiments/proofs/2026-11-24/screens/pickers/README-pickers.md: three tabs over the seat column,
the map list under them (the code draws it as a grid of map pictures, MapPickerInit in GUIUtil.cpp,
and keeps the hidden listbox as the model), the preview on the room's own map preview's rectangle,
and Cancel and Accept in the room's foot, inside the rectangle Play Game stands on, Accept at its
right end. The room hides those while the picker is open (the menus' ShowUnderlyingGUIElements
lists), so nothing on the right moves or changes size; the match rules page under it stays where
it is. EA's unnamed "Select Map" heading and the panel's border stay in the file, hidden: the tabs
and Accept name the screen, and frame A is the room's.

The geometry is read from the tracked room layouts, so a room laid out again by lobbyroom_layout.py
only needs this script run after it. Every control keeps its name; only rectangles and the hidden
flags change. The Official / Unofficial radio buttons are drawn as the menu's tabs by the menu code
(DrawMapListTab in GUIUtil.cpp). A third tab, Random, is cloned in from them, with the panel it
opens in the list's place: MAP SIZE over a row of five size boxes, MAP TYPE over the type list
(drawn as the same grid), RandomMapTab in GUIUtil.cpp. There is no player row, the map takes its
player count from the room, and no New Map button, since every Accept rolls a fresh seed.

    python mapselect_layout.py <retail Menus dir> <Menus dir out>

reads SkirmishMapSelectMenu.wnd, LanMapSelectMenu.wnd and WOLMapSelectMenu.wnd as WindowZH.big
ships them. Running it on its own output changes nothing.

    python mapselect_layout.py selfcheck

checks the tracked pickers: every window the menu code names is there, the script reproduces the
file byte for byte, everything is authored at 1920x1080, the list stays inside the seat table, the
preview sits exactly where the room's own preview does, and Cancel and Accept stand inside the
room's Play Game rectangle, Cancel at its left end and Accept at its right.
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

TAB_HEIGHT = 40          # the tabs, 126 to 166
CONTENT_TOP = 182        # the grid, or the Random tab's MAP SIZE head
HEAD_HEIGHT = 24         # MAP SIZE, MAP TYPE; each 8 over what it heads, the type head 16 under the sizes
SIZE_TOP = 214           # the five size boxes, 44 high
SIZE_HEIGHT = 44
TYPE_HEAD_TOP = 274
TYPE_TOP = 306           # the map type grid (MapPicker.cpp's PICK_TYPES_TOP)
CANCEL_WIDTH = 200
KEY_GAP = 16             # Cancel to Accept, UIStyle::MENU_KEY_GAP
MAP_ROWS = 4096


def box(window):
    """(left, top, width, height) of a window's rectangle."""
    left, top, right, bottom = window.rect[:4]
    return left, top, right - left, bottom - top


def room_geometry(room_layout):
    """The seat table, the map preview and the Play Game button, as the room lays them out."""
    # the seat column as frame A's room has it (the code lays the rows out, so no row window marks its foot)
    table = (room.INNER_LEFT, room.HEADER_TOP, room.SEAT_RIGHT - room.INNER_LEFT, room.SEATS_BOTTOM - room.HEADER_TOP)
    return table, box(room.need(room_layout, "MapWindow")), box(room.need(room_layout, "ButtonStart"))


def foot_keys(play):
    """Cancel at the left end of Play Game's rectangle, Accept the rest of it, KEY_GAP apart."""
    left, top, width, height = play
    cancel = (left, top, CANCEL_WIDTH, height)
    accept = (left + CANCEL_WIDTH + KEY_GAP, top, width - CANCEL_WIDTH - KEY_GAP, height)
    return cancel, accept


def title_of(layout):
    """EA left the picker's heading unnamed; it is the one unnamed window."""
    unnamed = [child for child in layout.root.children if not room.short(child)]
    if len(unnamed) != 1:
        raise ValueError("expected one unnamed heading in %s, found %d" % (room.short(layout.root), len(unnamed)))
    return unnamed[0]


def place_row(windows, left, top, width, height):
    """Side by side across the width, GAP apart, the last taking what the division left."""
    each = (width - room.GAP * (len(windows) - 1)) // len(windows)
    for at, window in enumerate(windows):
        x = left + at * (each + room.GAP)
        window.place(x, top, left + width - x if at == len(windows) - 1 else each, height)


def add_random_tab(layout):
    """Clone the Random tab and its panel from the picker's own controls, so they wear EA's art.
    They are removed and cloned again on every run, which is what makes a run on this script's own
    output come out byte for byte the same as one on the retail file."""
    room.remove(layout, [RANDOM_TAB] + RANDOM_PANEL)
    menu = layout.root.name.split(":")[0]
    named = lambda name: "%s:%s" % (menu, name)

    def hidden(window):
        room.hide(window)
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
    """MAP SIZE over the five size boxes across the seat column, MAP TYPE over the type list under
    them, down to the list's foot. The code draws the heads in its capitals and the list as tiles."""
    left, top, width, height = area
    find = lambda name: room.need(layout, name)

    find("StaticTextRandomSize").place(left, CONTENT_TOP, width, HEAD_HEIGHT)
    place_row([find("RadioButtonRandomSize%d" % size) for size in range(len(RANDOM_SIZES))],
              left, SIZE_TOP, width, SIZE_HEIGHT)
    find("StaticTextRandomTemplate").place(left, TYPE_HEAD_TOP, width, HEAD_HEIGHT)
    find("ListboxRandomTemplate").place(left, TYPE_TOP, width, top + height - TYPE_TOP)


def build(layout, room_layout):
    table, preview, play = room_geometry(room_layout)
    left, top, width, height = table
    cancel, accept = foot_keys(play)

    # every window is placed below; this only moves them onto the 1920x1080 grid first
    sx, sy = room.WIDE[0] / room.OLD[0], room.WIDE[1] / room.OLD[1]
    for node in layout.root.walk():
        l, t, r, b, cw, ch = node.rect
        if (cw, ch) == room.OLD:
            node.rect = (round(l * sx), round(t * sy), round(r * sx), round(b * sy)) + room.WIDE

    # the parent spans the seat table and the map column down to the foot's keys; it stays see-through
    root = layout.root
    play_left, play_top, play_width, play_height = play
    root.place(left, top, room.INNER_RIGHT - left, max(top + height, play_top + play_height) - top)

    # frame A and the glass are the room's; the border and EA's heading stay for the code, hidden
    border = room.need(layout, "WinGadgetBorder")
    border.place(*table)
    border.set_prop("ENABLEDDRAWDATA", room.draw_data(room.CARD, room.FRAME_COLOR))
    room.hide(border)
    title = title_of(layout)
    title.place(left, top, width // 2, TAB_HEIGHT)
    room.left_align(title)
    room.hide(title)

    listbox = room.need(layout, "ListboxMap")
    listbox.place(left, CONTENT_TOP, width, top + height - CONTENT_TOP)
    # EA's 100 rows (300 in skirmish) with AUTOPURGE 0 dropped every map past them without a word;
    # the list fills 2-player maps first, so a big Maps folder lost every 6 to 8 player map
    at = listbox.prop_index("LISTBOXDATA")
    listbox.props[at] = re.sub(r"LENGTH: \d+", "LENGTH: %d" % MAP_ROWS, listbox.props[at])

    map_window = room.need(layout, "WinMapPreview")
    map_window.place(*preview)
    # the menu code places the start spots on the map; only their size is the layout's
    for spot in map_window.children:
        spot.place(preview[0], preview[1], room.SPOT, room.SPOT)

    room.need(layout, "ButtonOK").place(*accept)
    room.need(layout, "ButtonBack").place(*cancel)
    if accept[2] < CANCEL_WIDTH:
        raise ValueError("Accept is %d wide beside Cancel in Play Game's %d" % (accept[2], play_width))

    # cloned last, from controls whose art is final by now; the Random panel takes the list's place
    add_random_tab(layout)
    place_row([room.need(layout, name) for name in ("RadioButtonSystemMaps", "RadioButtonUserMaps", RANDOM_TAB)],
              left, top, width, TAB_HEIGHT)
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
        table, preview, play = room_geometry(load_room(menu))
        for name in ["ListboxMap", "RadioButtonSystemMaps", "RadioButtonUserMaps", RANDOM_TAB] + RANDOM_PANEL:
            if not inside(box(layout.find(name)), table):
                problems.append("%s.wnd: %s runs out of the seat table" % (menu, name))
        if box(layout.find("WinMapPreview")) != preview:
            problems.append("%s.wnd: the preview is not where %s.wnd draws its map" % (menu, PICKERS[menu]))
        # the room's lit key and the picker's stand on one rectangle, so opening the picker moves no key
        accept, cancel = box(layout.find("ButtonOK")), box(layout.find("ButtonBack"))
        if not inside(accept, play) or accept[0] + accept[2] != play[0] + play[2]:
            problems.append("%s.wnd: Accept does not end where %s.wnd's Play Game does" % (menu, PICKERS[menu]))
        if not inside(cancel, play) or cancel[0] != play[0] or cancel[0] + cancel[2] + KEY_GAP != accept[0]:
            problems.append("%s.wnd: Cancel is not at the left of Play Game, %d before Accept" % (menu, KEY_GAP))
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
