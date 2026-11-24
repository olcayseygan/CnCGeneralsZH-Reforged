#!/usr/bin/env python3
"""Add the Mods button to the LAN lobby layout.

Create Game makes a classic room without asking; Mods asks Direct Strike or Apocalypse
(AskGameMode) and makes that mode's room (the owner, 2026-11-24).  ButtonMods is Create Game
cloned whole, so it keeps EA's art, font and sounds, and stands one row above it: the chat log
gives up that row and the chat line and its emote key move up with it.

    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\LanLobbyMenu.wnd" -o wnd
    python lanlobby_layout.py wnd/Window/Menus ../Data/Window/Menus

The output is a tracked master under Code/Data; the build copies it to Run/Window/Menus/, where a
loose file beats WindowZH.big.

    python lanlobby_layout.py selfcheck

reads the tracked master back: it carries every window LanLobbyMenu.cpp looks up, and ButtonMods
meets no other button, the chat line or the chat log.
"""

import os
import sys

import wndlayout
from wndlayout import clone


HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
MENU = "LanLobbyMenu"
MODS = "ButtonMods"
MODS_LABEL = "GUI:Mods"
MODS_TOOLTIP = "TOOLTIP:Mods"
# the row the chat log gives up, in the layout's 800x600 pixels
ROW = 40
LOOKUPS = ["LanLobbyMenuParent", "ButtonBack", "ButtonClear", "ButtonHost", "ButtonJoin", MODS, "ButtonDirectConnect",
           "ButtonEmote", "TextEntryPlayerName", "TextEntryChat", "ListboxPlayers", "ListboxGames",
           "StaticTextGameInfo", "ListboxChatWindowLanLobby"]


def full(name):
    return "%s.wnd:%s" % (MENU, name)


def build(layout):
    host = layout.find("ButtonHost")
    chat = layout.find("ListboxChatWindowLanLobby")
    l, t, r, b, cw, ch = chat.rect
    chat.rect = (l, t, r, b - ROW, cw, ch)
    for name in ("TextEntryChat", "ButtonEmote"):
        layout.find(name).move_by(0, -ROW)
    mods = clone(host, full(MODS))
    mods.move_by(0, -ROW)
    mods.set_prop("TEXT", '"%s"' % MODS_LABEL)
    if mods.prop_index("TOOLTIPTEXT") >= 0:
        mods.set_prop("TOOLTIPTEXT", '"%s"' % MODS_TOOLTIP)
    parent = layout.find("GadgetParent")
    parent.children.insert(parent.children.index(host) + 1, mods)
    return layout


def meets(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def selfcheck():
    problems = []
    path = os.path.join(MENUS, MENU + ".wnd")
    if not os.path.exists(path):
        print("  %s.wnd is not tracked under Data/Window/Menus" % MENU)
        return 1
    layout = wndlayout.load(path)
    names = set((w.name or "").split(":")[-1] for w in layout.root.walk())
    for name in LOOKUPS:
        if name not in names:
            problems.append("%s.wnd has no %s" % (MENU, name))
    mods = layout.find(MODS)
    if mods is not None:
        for name in ("ButtonHost", "ButtonJoin", "ButtonDirectConnect", "ButtonBack", "TextEntryChat", "ButtonEmote",
                     "ListboxChatWindowLanLobby"):
            other = layout.find(name)
            if other is not None and meets(mods.rect, other.rect):
                problems.append("%s meets %s" % (MODS, name))
    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1
    print("lan lobby: %d windows checked" % len(LOOKUPS))
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
