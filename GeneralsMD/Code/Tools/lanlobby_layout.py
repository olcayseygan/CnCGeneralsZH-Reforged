#!/usr/bin/env python3
"""Lay the LAN lobby out on frame A's board, authored at 1920x1080.

The design is experiments/proofs/2026-11-24/screens/game-lists/README-game-lists.md in the wrapper
(MENU-REDESIGN.md package 10): the panel on frame A at 32,28 to 1888,1052, the title row with the
player's name box and CLEAR at its right end, the games' list in the left column 60 to 1310 from
130 with the chat under it, the picked game and the players in the right column 1334 to 1860, the
foot's keys MAIN MENU, DIRECT CONNECT, JOIN GAME and CREATE GAME at 976 to 1028, 300 wide and 16
apart. Create Game asks the three modes (AskGameMode); the Mods key an earlier layout added stays in
the file, hidden. The code draws every line, head, row and the picked game (LanLobbyMenu.cpp,
GameInfoWindow.cpp, LobbyUtils.cpp); EA's labels stay, hidden and named, so nothing they held is lost.

    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\LanLobbyMenu.wnd" -o wnd
    python lanlobby_layout.py wnd/Window/Menus ../Data/Window/Menus

The output is a tracked master under Code/Data; the build copies it to Run/Window/Menus/, where a
loose file beats WindowZH.big. Running it on its own output changes nothing, and

    python lanlobby_layout.py rebuild

does that to the tracked master, for a change to this script.

    python lanlobby_layout.py selfcheck

reads the tracked master back: it carries every window LanLobbyMenu.cpp looks up, the script
reproduces it byte for byte, every window is at 1920x1080, no two visible gadgets overlap or leave
the panel, and the foot's keys stand on the key row's pitch. Run by CTest as lanlobby_layout_selfcheck.
"""

import os
import sys

import wndlayout
import lobbyroom_layout as room
from wndlayout import clone


HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
MENU = "LanLobbyMenu"
WIDE = room.WIDE
CLEAR = (0, 0, 0, 0)

# frame A's board, every number at 1080
FRAME = (32, 28, 1856, 1024)
X0, X1 = 60, 1860
LEFT_RIGHT, RIGHT_LEFT = 1310, 1334
ROW_TOP, ROW_HEIGHT, KEY, KEY_GAP = 976, 52, 300, 16
SEND_W = 120

# the list: its head from 132, the column words at 168, rows of 40 from 198 (the listbox draws them 4 px in),
# six of them; the chat's head at 454, its log from 480 to 896, the entry and SEND at 908
GAMES = (X0, 194, LEFT_RIGHT - X0, 6 * 40 + 8)
CHAT = (X0, 480, LEFT_RIGHT - X0, 896 - 480)
ENTRY = (X0, 908, LEFT_RIGHT - X0 - SEND_W - 12, 44)
SEND = (LEFT_RIGHT - SEND_W, 908, SEND_W, 44)
# the right column: the picked game from 130 to 742, the players' head at 752, their rows from 786
PICKED = (RIGHT_LEFT, 130, X1 - RIGHT_LEFT, 742 - 130)
PLAYERS = (RIGHT_LEFT, 786, X1 - RIGHT_LEFT, 946 - 786)
# the title row's right end: PLAYER NAME's box and CLEAR
NAME = (1358, 46, 340, 48)
CLEAR_KEY = (1710, 46, 150, 48)


def key(index, from_right):
    left = X1 - (index + 1) * KEY - index * KEY_GAP if from_right else X0 + index * (KEY + KEY_GAP)
    return (left, ROW_TOP, KEY, ROW_HEIGHT)


PLACE = {
    "GadgetParent": FRAME,
    "TextEntryPlayerName": NAME, "ButtonClear": CLEAR_KEY,
    "ListboxGames": GAMES, "StaticTextGameInfo": PICKED, "ListboxPlayers": PLAYERS,
    "ListboxChatWindowLanLobby": CHAT, "TextEntryChat": ENTRY, "ButtonEmote": SEND,
    "ButtonBack": key(0, False), "ButtonDirectConnect": key(1, False),
    "ButtonJoin": key(1, True), "ButtonHost": key(0, True), "ButtonMods": key(2, True),
}
KEYS = ["ButtonBack", "ButtonDirectConnect", "ButtonJoin", "ButtonHost"]
LABELS = {"ButtonBack": "GUI:GotoMainMenu", "ButtonHost": "GUI:CreateGame", "ButtonJoin": "GUI:LobbyJoinGame",
          "ButtonEmote": "GUI:RoomSend", "ButtonClear": "GUI:Clear"}
# EA's unnamed labels and rule, named so the code and this script can find them, all drawn by code now
NAMES = [("StaticTextTitle", "GUI:LANLobby"), ("StaticTextPlayerName", "GUI:PlayerName"),
         ("StaticTextPlayers", "GUI:Players"), ("StaticTextGames", "GUI:Games"), ("StaticTextInfo", "GUI:GameInfo")]
HIDE = ["StaticTextTitle", "StaticTextPlayerName", "StaticTextPlayers", "StaticTextGames", "StaticTextInfo", "Line",
        "ButtonMods"]
MODS = "ButtonMods"

LOOKUPS = ["LanLobbyMenuParent", "GadgetParent", "ButtonBack", "ButtonClear", "ButtonHost", "ButtonJoin", MODS,
           "ButtonDirectConnect", "ButtonEmote", "TextEntryPlayerName", "TextEntryChat", "ListboxPlayers", "ListboxGames",
           "StaticTextGameInfo", "ListboxChatWindowLanLobby"]


def full(name):
    return "%s.wnd:%s" % (MENU, name)


def hide(window):
    status = window.prop("STATUS").split("=", 1)[1].strip().rstrip(";").strip()
    if "HIDDEN" not in status.split("+"):
        window.set_prop("STATUS", status + "+HIDDEN")


def hidden(window):
    return "HIDDEN" in (window.prop("STATUS") or "")


def build(layout):
    room.widen(layout)
    for name, text in NAMES:
        if layout.find(name) is None:
            for node in layout.root.walk():
                if not room.short(node) and node.prop("TEXT") == 'TEXT = "%s";' % text:
                    node.name = full(name)
                    break
    if layout.find("Line") is None:
        raise KeyError("LanLobbyMenu.wnd has no Line")
    if layout.find(MODS) is None:
        host = room.need(layout, "ButtonHost")
        mods = clone(host, full(MODS))
        mods.set_prop("TEXT", '"GUI:Mods"')
        parent = layout.find("GadgetParent")
        parent.children.insert(parent.children.index(host) + 1, mods)
    # frame A, the glass and every line are the code's
    room.need(layout, "GadgetParent").set_prop("ENABLEDDRAWDATA", room.draw_data(CLEAR, CLEAR))
    for name, rect in PLACE.items():
        room.need(layout, name).place(*rect)
    for name, label in sorted(LABELS.items()):
        room.need(layout, name).put_prop("TEXT", '"%s"' % label)
    for name in HIDE:
        hide(room.need(layout, name))
    return layout


def visible_gadgets(window, gone=False):
    gone = gone or hidden(window)
    if not gone and window.prop("WINDOWTYPE") != "WINDOWTYPE = USER;":
        yield window
    for child in window.children:
        for node in visible_gadgets(child, gone):
            yield node


def meets(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def selfcheck():
    problems = []
    path = os.path.join(MENUS, MENU + ".wnd")
    if not os.path.exists(path):
        print("  %s.wnd is not tracked under Data/Window/Menus" % MENU)
        return 1
    with open(path, "rb") as fp:
        tracked = fp.read().decode("latin-1")
    lf = tracked.replace("\r\n", "\n")
    layout = wndlayout.parse(tracked)
    for name in LOOKUPS:
        if layout.find(name) is None:
            problems.append("%s.wnd has no %s" % (MENU, name))
    try:
        if build(wndlayout.parse(tracked)).text().replace("\r\n", "\n") != lf:
            problems.append("%s.wnd is not what lanlobby_layout.py writes; run it with rebuild" % MENU)
    except KeyError as error:
        problems.append("%s.wnd no longer builds: %s" % (MENU, error))
    stale = [room.short(node) or "(unnamed)" for node in layout.root.walk() if tuple(node.rect[4:]) != WIDE]
    if stale:
        problems.append("%d windows not authored at 1920x1080, %s first" % (len(stale), stale[0]))
    gadgets = list(visible_gadgets(layout.root))
    for index, a in enumerate(gadgets):
        for b in gadgets[index + 1:]:
            if meets(a.rect, b.rect):
                problems.append("%s and %s overlap" % (room.short(a), room.short(b)))
    left, top, width, height = FRAME
    for node in gadgets:
        l, t, r, b = node.rect[:4]
        if l < left or t < top or r > left + width or b > top + height:
            problems.append("%s runs out of the panel" % (room.short(node) or "(unnamed)"))
    for name in KEYS:
        l, t, r, b = room.need(layout, name).rect[:4]
        pitch = KEY + KEY_GAP
        if (r - l, b - t) != (KEY, ROW_HEIGHT) or t != ROW_TOP:
            problems.append("%s is not a key on the foot row" % name)
        elif (l - X0) % pitch and (X1 - r) % pitch:
            problems.append("%s is off the keys' pitch" % name)
    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1
    print("lan lobby: %d windows checked, %d gadgets shown" % (len(LOOKUPS), len(gadgets)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) == 2 and argv[1] == "rebuild":
        path = os.path.join(MENUS, MENU + ".wnd")
        wndlayout.save(build(wndlayout.load(path)), path)
        print("%s rebuilt" % path)
        return 0
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
