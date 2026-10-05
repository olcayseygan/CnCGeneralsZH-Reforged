#!/usr/bin/env python3
"""Put an Apocalypse button in the main menu's single player list, under Skirmish.

The button is a clone of ButtonSkirmish, so it keeps EA's images, font and colours.  The list it
joins already had seven buttons on a 40 pixel pitch, ending at 390 in a dropdown that ends at 400,
and the faction logo the single player transition grows in at the bottom right starts at 423.
Eight buttons at 40 would run the dropdown into that logo, so the list closes up to 36, the button
height to 34 (EA's own Back button in this list was already 34), and the dropdown grows by what is
left.

The file this starts from is the 1.04 patch's MainMenu.wnd out of PatchWindow.big, not the one in
WindowZH.big: the patch archives are loaded over the rest, so that is the layout the game shows.
It carries a Custom Mission button between Skirmish and Back, which the fork's code does not
answer; it stays where the list puts it.

The LAN lobby's title is unnamed in EA's LanLobbyMenu.wnd, and the Apocalypse lobby is that screen
with another caption, so the title gets a name here and nothing else changes in that file.

    python bigfile.py extract ../../Run/PatchWindow.big "*MainMenu.wnd" -o wnd
    python bigfile.py extract ../../Run/WindowZH.big "*LanLobbyMenu.wnd" -o wnd
    python mainmenu_layout.py wnd/Window/Menus ../Data/Window/Menus

writes both masters.

    python mainmenu_layout.py selfcheck

reads the tracked files back: the button is there, under Skirmish, inside the dropdown, the list
does not overlap itself or the logo, the lobby title has its name, and Patch.str has the strings.
WindowTransitions.ini names every other button in the list and not this one, so it comes up with
the dropdown instead of flashing in after Skirmish.
"""

import os
import re
import sys

import wndlayout

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")

BUTTON = "ButtonApocalypse"
MODEL = "ButtonSkirmish"
DROPDOWN, LIST = "MapBorder", "EarthMap"
# the logo WindowTransitions.ini grows in under the dropdown while it is open
LOGO = "WinFactionSkirmish"

FIRST_TOP, PITCH, HEIGHT = 116, 36, 34
# EA's margins: the list ends 2 pixels inside its panel, the panel 8 inside the border
LIST_MARGIN, BORDER_MARGIN = 2, 8

LOBBY_TITLE_TEXT, LOBBY_TITLE = "GUI:LANLobby", "StaticTextTitle"

STRINGS = ["GUI:Apocalypse", "GUI:ApocalypseToolTip"]


def short(window):
    return (window.name or "").split(":")[-1]


def need(layout, name):
    window = layout.find(name)
    if window is None:
        raise KeyError("layout has no %s" % name)
    return window


def buttons(layout):
    """The list's buttons, top to bottom."""
    return sorted(need(layout, LIST).children, key=lambda child: child.rect[1])


def build_main_menu(layout):
    panel = need(layout, LIST)
    model = need(layout, MODEL)
    button = wndlayout.clone(model, "MainMenu.wnd:" + BUTTON)
    button.set_prop("TEXT", '"GUI:Apocalypse"')
    button.put_prop("TOOLTIPTEXT", '"GUI:ApocalypseToolTip"')
    panel.children.insert(panel.children.index(model) + 1, button)

    ordered = buttons(layout)
    ordered.remove(button)
    ordered.insert(ordered.index(model) + 1, button)
    for index, child in enumerate(ordered):
        left, _top, right = child.rect[:3]
        child.place(left, FIRST_TOP + index * PITCH, right - left, HEIGHT)

    list_bottom = FIRST_TOP + (len(ordered) - 1) * PITCH + HEIGHT + LIST_MARGIN
    for window, bottom in ((panel, list_bottom), (need(layout, DROPDOWN), list_bottom + BORDER_MARGIN)):
        left, top, right = window.rect[:3]
        window.place(left, top, right - left, bottom - top)
    return layout


def build_lan_lobby(layout):
    for node in layout.root.walk():
        if re.search(r'"%s"' % LOBBY_TITLE_TEXT, node.prop("TEXT") or ""):
            node.put_prop("NAME", '"LanLobbyMenu.wnd:%s"' % LOBBY_TITLE)
            return layout
    raise KeyError("LanLobbyMenu.wnd has no %s caption" % LOBBY_TITLE_TEXT)


BUILDERS = {"MainMenu": build_main_menu, "LanLobbyMenu": build_lan_lobby}


def read_strings():
    with open(PATCH_STR, "r") as fp:
        return set(line.strip() for line in fp)


def check_main_menu(layout):
    found = []
    button = layout.find(BUTTON)
    if button is None:
        return ["MainMenu.wnd has no %s" % BUTTON]
    ordered = buttons(layout)
    if button not in ordered:
        return ["%s is not in %s" % (BUTTON, LIST)]
    if ordered.index(button) != ordered.index(need(layout, MODEL)) + 1:
        found.append("%s is not right under %s" % (BUTTON, MODEL))
    for upper, lower in zip(ordered, ordered[1:]):
        if upper.rect[3] > lower.rect[1]:
            found.append("%s runs into %s" % (short(upper), short(lower)))
    panel, dropdown = need(layout, LIST), need(layout, DROPDOWN)
    if ordered[-1].rect[3] > panel.rect[3] or panel.rect[3] > dropdown.rect[3]:
        found.append("the list runs out of its dropdown")
    if dropdown.rect[3] >= need(layout, LOGO).rect[1]:
        found.append("%s runs into %s" % (DROPDOWN, LOGO))
    return found


def selfcheck():
    problems = []
    keys = read_strings()
    problems.extend("%s is not in Patch.str" % key for key in STRINGS if key not in keys)

    main_menu = wndlayout.load(os.path.join(MENUS, "MainMenu.wnd"))
    problems.extend(check_main_menu(main_menu))
    if wndlayout.load(os.path.join(MENUS, "LanLobbyMenu.wnd")).find(LOBBY_TITLE) is None:
        problems.append("LanLobbyMenu.wnd has no %s" % LOBBY_TITLE)

    for problem in problems:
        print("mainmenu: %s" % problem)
    if problems:
        return 1
    print("mainmenu: %d buttons in the single player list" % len(buttons(main_menu)))
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
