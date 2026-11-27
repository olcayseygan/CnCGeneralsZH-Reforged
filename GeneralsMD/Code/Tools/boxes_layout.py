#!/usr/bin/env python3
"""Lay the game's boxes out in the menu language, authored at 1920x1080.

The design is the boxes README under experiments/proofs/2026-11-24/screens/boxes in the wrapper
(docs/plans/MENU-REDESIGN.md, packages 2 and 5).  Every box is UIStyle's box: frame A round a
glass, an 82 px title band, the content, an 84 px foot with its keys at the right, 170 wide and 16
apart (UIStyle's BOX_KEY_GAP, the 8 px grid), Enter's key rightmost.  The code draws everything that is a line, a frame, a state or a word
and lays the windows out again from UIStyle's numbers when a box's height follows what it holds;
these masters carry every window the code looks up, at the rects the design draws, and hide the
windows the language drops (EA's title art, logos, rules and fixed player rows) so lookups still
resolve.

    MessageBox, QuitMessageBox      the question box, 600,415 to 1320,665 at one line (package 2)
    QuitMenu, QuitNoSave            the in-game menu, a 560 box over the match, the rail's keys
    DisconnectScreen                the 1040 list over the match; rows, votes and chat are code
    EstablishConnectionsScreen      the 720 status box over the menu; rows are code
    DownloadMenu                    the 720 progress box
    WOLBuddyOverlay                 the 1040 friends box: two tabs, the list, the chat, the foot
    RC*Menu (six)                   the right-click menus, 240 wide, 40 an entry under a name line
    Victorious, Defeat, LocalDefeat, ObserverQuit
                                    the band over the match, 600,318 to 1320,486, drawn by
                                    MatchBandDraw (QuitMenu.cpp) in place of EA's pictures
    PopupReplay                     Save Replay, the 1040 box over the end screen (package 12)
    PopupSaveLoad                   Save / Load, the 1040 box over the match, and the save
                                    description's question box inside it (package 12)

    python bigfile.py extract ../../Run/WindowZH.big "*Menus?QuitMenu.wnd" -o wnd   (and so on for
        every retail-only layout above)
    python boxes_layout.py wnd/Window/Menus ../Data/Window/Menus

writes the tracked masters under Code/Data, reading a layout from the master when one is tracked
and from the extracted folder when not; the build copies them to Run/Window/Menus/, where a loose
file beats WindowZH.big.  Running it on its own output changes nothing.

    python boxes_layout.py selfcheck

reads the masters back and checks that every window the code looks up is there, that every window
is authored at 1920x1080, that each box stands on UIStyle's numbers, that the script reproduces each
master byte for byte from itself, and that Patch.str carries the family's keys.  Run by CTest as
boxes_selfcheck.  Layouts are not in the multiplayer INI checksum.
"""

import os
import sys

import wndlayout

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")

BOARD = (1920, 1080)

# UIStyle.h's box numbers
BOX_MENU, BOX_QUESTION, BOX_LIST = 560, 720, 1040
BOX_PAD, BOX_HEAD, BOX_FOOT, BOX_LINE = 32, 82, 84, 28
KEY_WIDTH, KEY_GAP, KEY_HEIGHT = 170, 16, 48
BAR_TOP, TOP_MIN = 832, 56

# the rail's keys in the in-game menu (the main menu's 62 px buttons, 10 apart, 24 more before the
# keys that leave the match), the Esc hint's line under them
RAIL_KEY, RAIL_GAP, RAIL_SPLIT, RAIL_HINT = 62, 10, 24, 60

# the right-click menus
RC_WIDTH, RC_HEAD, RC_ENTRY, RC_TAIL = 240, 36, 40, 6


def box_rect(width, height, over_match):
    """UIStyle::boxRect1080: centred over a menu; over a match centred above the bar, clear of it."""
    x0 = (BOARD[0] - width) // 2
    if over_match:
        lowest = BAR_TOP - 44
        y0 = max(TOP_MIN, (lowest - height) // 2 + 8)
        if y0 + height > lowest:
            y0 = max(TOP_MIN, lowest - height)
    else:
        y0 = (BOARD[1] - height) // 2
    return (x0, y0, x0 + width, y0 + height)


def key_rect(box, slot):
    """UIStyle::boxKeyRect1080: slot 0 is Enter's, at the right."""
    right = box[2] - BOX_PAD - slot * (KEY_WIDTH + KEY_GAP)
    bottom = box[3] - 20
    return (right - KEY_WIDTH, bottom - KEY_HEIGHT, right, bottom)


def foot_entry(box, left):
    """A text entry in the foot, left of its keys (the chat line)."""
    return (box[0] + BOX_PAD, box[3] - 68, left - 16, box[3] - 20)


def question_height(lines):
    return BOX_HEAD + BOX_LINE + lines * BOX_LINE + BOX_LINE + BOX_FOOT


def quit_menu_height(keys):
    """QuitMenu.cpp's quitMenuHeight: the head, 24 of air, the keys, the split, the hint."""
    return BOX_HEAD + 24 + keys * RAIL_KEY + (keys - 1) * RAIL_GAP + RAIL_SPLIT + RAIL_HINT


def put(window, rect):
    left, top, right, bottom = rect
    window.rect = (left, top, right, bottom, BOARD[0], BOARD[1])


def hide(window, at):
    """Out of the way: 1 px at the box's corner, hidden."""
    put(window, (at[0], at[1], at[0] + 1, at[1] + 1))
    statement = window.prop("STATUS")
    flags = statement.split("=", 1)[1].strip().rstrip(";").strip()
    if "HIDDEN" not in flags.split("+"):
        window.set_prop("STATUS", flags + "+HIDDEN")


def short(window):
    return (window.name or "").split(":")[-1]


def place_tree(root, rects, box):
    """Every window under root: to its rect when the table names it, else hidden at the box's corner."""
    for window in list(root.walk())[1:]:
        if short(window) in rects:
            put(window, rects[short(window)])
        else:
            hide(window, box)


def set_text(window, key):
    if window.prop("TEXT") is not None:
        window.set_prop("TEXT", '"%s"' % key)


def set_draw(window, func):
    window.set_prop("DRAWCALLBACK", '"%s"' % func)


# ---------------------------------------------------------------------------------------------- the layouts
# Package 2's question box.
QUESTION_BOX = box_rect(BOX_QUESTION, question_height(1), False)
QUESTION_KEYS = [("ButtonOk", 0), ("ButtonYes", 0), ("ButtonNo", 1), ("ButtonCancel", 2)]


def build_question(layout):
    put(layout.root, (0, 0, BOARD[0] - 1, BOARD[1] - 1))
    parent = layout.find("MessageBoxParent")
    box = QUESTION_BOX
    rects = {"MessageBoxParent": box,
             "StaticTextTitle": (box[0] + BOX_PAD, box[1] + 24, box[2] - BOX_PAD, box[1] + 64),
             "StaticTextMessage": (box[0] + BOX_PAD, box[1] + BOX_HEAD + BOX_LINE, box[2] - BOX_PAD,
                                   box[1] + BOX_HEAD + 2 * BOX_LINE)}
    for name, slot in QUESTION_KEYS:
        rects[name] = key_rect(box, slot)
    put(parent, box)
    for window in parent.children:
        if short(window) in rects:
            put(window, rects[short(window)])
        elif short(window) in ("", "Logo"):
            hide(window, box)
        else:
            raise SystemExit("a message box has a window this tool does not place: %r" % window.name)


def quit_rects(net):
    """The in-game menu: Return lit on top, Save/Load (single player), Options; Restart or Surrender
    and Exit set apart under a faint rule."""
    keep = ["ButtonReturn"] + ([] if net else ["ButtonSaveLoad"]) + ["ButtonOptions"]
    leave = ["ButtonRestart", "ButtonExit"]
    box = box_rect(BOX_MENU, quit_menu_height(len(keep) + len(leave)), True)
    x0, x1 = box[0] + BOX_PAD, box[2] - BOX_PAD
    y = box[1] + BOX_HEAD + 24
    rects = {"QuitMenuParent": box}
    for name in keep:
        rects[name] = (x0, y, x1, y + RAIL_KEY)
        y += RAIL_KEY + RAIL_GAP
    y += RAIL_SPLIT - RAIL_GAP
    for name in leave:
        rects[name] = (x0, y, x1, y + RAIL_KEY)
        y += RAIL_KEY + RAIL_GAP
    return box, rects


def build_quit(layout, net):
    box, rects = quit_rects(net)
    put(layout.root, box)
    place_tree(layout.root, rects, box)


# the disconnect box at its design size (two waiting, five connected on one line, four chat lines);
# DisconnectMenu.cpp lays it out again on every draw from what it holds
DISCONNECT_BOX = box_rect(BOX_LIST, 498, True)


def build_disconnect(layout):
    box = DISCONNECT_BOX
    quit_key = key_rect(box, 0)
    put(layout.root, box)
    place_tree(layout.root, {"ButtonQuitGame": quit_key, "TextEntry": foot_entry(box, quit_key[0])}, box)


# six players, 46 a row
CONNECTIONS_BOX = box_rect(BOX_QUESTION, BOX_HEAD + 16 + 6 * 46 + 16 + BOX_FOOT, False)


def build_connections(layout):
    box = CONNECTIONS_BOX
    put(layout.root, box)
    place_tree(layout.root, {"ButtonQuit": key_rect(box, 0)}, box)
    set_text(layout.find("ButtonQuit"), "Network:QuitGame")       # EA's GUI:Quit is in no string table


DOWNLOAD_BOX = box_rect(BOX_QUESTION, BOX_HEAD + 24 + 34 + 18 + 30 + 20 + BOX_FOOT, False)


def build_download(layout):
    box = DOWNLOAD_BOX
    put(layout.root, box)
    # the static texts and the bar stay where the code reads them, hidden: the code draws their words
    place_tree(layout.root, {"ButtonCancel": key_rect(box, 0)}, box)


# seven rows of 46 at the design's size
FRIENDS_LIST = 7 * 46
FRIENDS_BOX = box_rect(BOX_LIST, BOX_HEAD + 20 + 36 + 16 + FRIENDS_LIST + 20 + BOX_FOOT, False)


def friends_rects():
    box = FRIENDS_BOX
    x0, x1 = box[0] + BOX_PAD, box[2] - BOX_PAD
    tabs = box[1] + BOX_HEAD + 20
    top = tabs + 36 + 16
    content = (x0, top, x1, box[3] - 20)		# down to the foot: the chat line is a child of it
    lx1 = x0 + 520
    close = key_rect(box, 0)
    return box, {"RadioButtonBuddies": (x0, tabs, x0 + 190, tabs + 36),
                 "RadioButtonIgnore": (x0 + 202, tabs, x0 + 362, tabs + 36),
                 "BuddiesParent": content, "IgnoreParent": content,
                 "ListboxBuddies": (x0, top, lx1, top + FRIENDS_LIST),
                 "ListboxBuddyChat": (lx1 + 24, top, x1, top + FRIENDS_LIST),
                 "ListboxIgnore": (x0, top, lx1, top + FRIENDS_LIST),
                 "TextEntryChat": foot_entry(box, close[0]),
                 "ButtonHide": close}


def build_friends(layout):
    box, rects = friends_rects()
    put(layout.root, box)
    place_tree(layout.root, rects, box)
    set_text(layout.find("ButtonHide"), "GUI:Close")
    set_text(layout.find("RadioButtonBuddies"), "GUI:Friends")
    set_text(layout.find("RadioButtonIgnore"), "GUI:IgnoreSm")    # EA's GUI:IgnoreSM is in no string table


# the right-click menus: (layout, entries top to bottom, the new word for an entry)
RC_MENUS = {
    "RCBuddiesMenu": (["ButtonStats", "ButtonDelete", "ButtonIgnore"],
                      {"ButtonStats": "GUI:PlayerCard", "ButtonDelete": "GUI:RemoveFriend"}),
    "RCBuddyRequestMenu": (["ButtonAdd", "ButtonDelete"], {}),
    "RCNonBuddiesMenu": (["ButtonStats", "ButtonAdd", "ButtonIgnore"],
                         {"ButtonStats": "GUI:PlayerCard", "ButtonAdd": "GUI:AddFriend"}),
    "RCLocalPlayerMenu": (["ButtonStats"], {"ButtonStats": "GUI:PlayerCard"}),
    "RCNoProfileMenu": (["ButtonIgnore"], {}),
    "RCGameDetailsMenu": (["ButtonLadderDetails"], {}),
}


def rc_rect(entries):
    """The menu at the board's corner: the caller puts it at the pointer."""
    return (0, 0, RC_WIDTH, RC_HEAD + RC_ENTRY * len(entries) + RC_TAIL)


def build_rc(layout, menu):
    entries, words = RC_MENUS[menu]
    box = rc_rect(entries)
    put(layout.root, box)
    set_draw(layout.root, "RightClickMenuDraw")
    rects = {}
    for i, name in enumerate(entries):
        rects[name] = (box[0] + 1, RC_HEAD + RC_ENTRY * i, box[2] - 1, RC_HEAD + RC_ENTRY * (i + 1))
    place_tree(layout.root, rects, box)
    for name in entries:
        window = layout.find(name)
        set_draw(window, "RightClickEntryDraw")
        if name in words:
            set_text(window, words[name])


BAND_BOX = (600, 318, 1320, 486)


def build_band(layout):
    put(layout.root, BAND_BOX)
    set_draw(layout.root, "MatchBandDraw")
    place_tree(layout.root, {}, BAND_BOX)


def lift(root, names, to):
    """Move the named windows to be children of `to`, out of EA's button frames (rects in a .wnd are
    absolute, so nothing moves on screen)."""
    for name in names:
        window = root.find(name)
        parent = root.parent_of(window)
        if parent is not to:
            parent.children.remove(window)
            to.children.append(window)


# Package 12's boxes (the single player README, SinglePlayerLayout.h has the same numbers).  The save
# description: the question box with a text box in it, CANCEL and SAVE GAME 190 wide.
DESC_HEIGHT = BOX_HEAD + 24 + BOX_LINE + 12 + 48 + BOX_LINE + BOX_FOOT
DESC_KEY = 190


def wide_key(box, slot, width):
    right = box[2] - BOX_PAD - slot * (width + KEY_GAP)
    return (right - width, box[3] - 20 - KEY_HEIGHT, right, box[3] - 20)


def desc_rects(over_match):
    box = box_rect(BOX_QUESTION, DESC_HEIGHT, over_match)
    y = box[1] + BOX_HEAD + 24
    return box, {"SaveDescParent": box, "EntryDesc": (box[0] + BOX_PAD, y + 40, box[2] - BOX_PAD, y + 88),
                 "ButtonSaveDescConfirm": wide_key(box, 0, DESC_KEY), "ButtonSaveDescCancel": wide_key(box, 1, DESC_KEY)}


# Save Replay over the end screen: NAME and its text box, the saved replays (seven rows of 38), CANCEL
# and SAVE; "replay saved" is the one-line question box over it.  The column heads stand 22 under the
# text box and 34 over the list, so the text box and the list are 56 apart, on the 8 px grid
REPLAY_POPUP = box_rect(BOX_LIST, BOX_HEAD + 24 + 48 + 22 + 34 + 7 * 38 + 16 + BOX_FOOT, False)


def popup_replay_rects():
    box = REPLAY_POPUP
    x0, x1 = box[0] + BOX_PAD, box[2] - BOX_PAD
    y = box[1] + BOX_HEAD + 24
    rows = y + 48 + 22 + 34
    return {"PopupReplaySubMenu": box, "TextEntryReplayName": (x0 + 100, y, x1, y + 48),
            "ListboxGames": (x0, rows, x1, rows + 7 * 38), "ButtonSave": key_rect(box, 0),
            "ButtonBack": key_rect(box, 1), "PopupReplaySaved": QUESTION_BOX}


def build_popup_replay(layout):
    put(layout.root, (0, 0, BOARD[0], BOARD[1]))
    sub = layout.find("PopupReplaySubMenu")
    lift(layout.root, ["ButtonBack", "ButtonSave"], sub)
    place_tree(layout.root, popup_replay_rects(), REPLAY_POPUP)
    set_text(layout.find("ButtonBack"), "GUI:Cancel")
    set_text(layout.find("ButtonSave"), "GUI:Save")


# Save / Load over a match: the new save and the saves (eight rows of 40), DELETE, LOAD GAME and SAVE
# GAME 190 wide; the confirms ask through the message box, only the description keeps a box here
SAVE_POPUP = box_rect(BOX_LIST, BOX_HEAD + 20 + 34 + 8 * 40 + 16 + BOX_FOOT, True)


def popup_saveload_rects():
    box = SAVE_POPUP
    top = box[1] + BOX_HEAD + 20 + 34
    rects = {"ListboxGames": (box[0] + BOX_PAD, top, box[2] - BOX_PAD, top + 8 * 40),
             "ButtonSave": wide_key(box, 0, DESC_KEY), "ButtonLoad": wide_key(box, 1, DESC_KEY),
             "ButtonDelete": wide_key(box, 2, DESC_KEY)}
    rects.update(desc_rects(True)[1])
    return rects


def build_popup_saveload(layout):
    box = SAVE_POPUP
    put(layout.root, box)
    lift(layout.root, ["ButtonSave", "ButtonLoad", "ButtonDelete"], layout.root)
    rects = popup_saveload_rects()
    for window in list(layout.root.walk())[1:]:
        if short(window) in rects:
            put(window, rects[short(window)])
            if short(window) == "SaveDescParent":
                status = window.prop("STATUS").split("=", 1)[1].strip().rstrip(";").strip()
                if "HIDDEN" not in status.split("+"):
                    window.set_prop("STATUS", status + "+HIDDEN")
        else:
            hide(window, box)
    set_text(layout.find("ButtonDelete"), "GUI:SPDelete")
    set_text(layout.find("ButtonSaveDescCancel"), "GUI:Cancel")


# name: (builder, windows the code looks up, the box's own window and where it stands)
LAYOUTS = {
    "MessageBox": (build_question, ["MessageBoxParent", "StaticTextTitle", "StaticTextMessage"]
                   + [k for k, _ in QUESTION_KEYS], ("MessageBoxParent", QUESTION_BOX)),
    "QuitMessageBox": (build_question, ["MessageBoxParent", "StaticTextTitle", "StaticTextMessage"]
                       + [k for k, _ in QUESTION_KEYS], ("MessageBoxParent", QUESTION_BOX)),
    "QuitMenu": (lambda l: build_quit(l, False), ["QuitMenuParent", "ButtonReturn", "ButtonSaveLoad",
                                                   "ButtonOptions", "ButtonRestart", "ButtonExit"],
                 ("QuitMenuParent", quit_rects(False)[0])),
    "QuitNoSave": (lambda l: build_quit(l, True), ["QuitMenuParent", "ButtonReturn", "ButtonOptions",
                                                    "ButtonRestart", "ButtonExit"],
                   ("QuitMenuParent", quit_rects(True)[0])),
    "DisconnectScreen": (build_disconnect, ["DisconnectWindow", "ButtonQuitGame", "TextEntry",
                                            "ListboxTextDisplay"]
                         + ["StaticPlayer%dName" % i for i in range(1, 8)]
                         + ["ButtonKickPlayer%d" % i for i in range(1, 8)]
                         + ["StaticPlayer%dVotes" % i for i in range(1, 8)],
                         ("DisconnectWindow", DISCONNECT_BOX)),
    "EstablishConnectionsScreen": (build_connections, ["EstablishConnectionsWindow", "ButtonQuit"]
                                   + ["StaticPlayer%dName" % i for i in range(1, 8)]
                                   + ["StaticPlayer%dStatus" % i for i in range(1, 8)],
                                   ("EstablishConnectionsWindow", CONNECTIONS_BOX)),
    "DownloadMenu": (build_download, ["ParentDownload", "ButtonCancel", "StaticTextSize", "StaticTextTime",
                                      "StaticTextFile", "StaticTextStatus", "ProgressBarMunkee"],
                     ("ParentDownload", DOWNLOAD_BOX)),
    "WOLBuddyOverlay": (build_friends, ["BuddyMenuParent", "ButtonHide", "RadioButtonIgnore",
                                        "RadioButtonBuddies", "BuddiesParent", "IgnoreParent",
                                        "ListboxBuddies", "ListboxBuddyChat", "ListboxIgnore", "TextEntryChat"],
                        ("BuddyMenuParent", FRIENDS_BOX)),
    "Victorious": (build_band, [], (None, BAND_BOX)),
    "Defeat": (build_band, [], (None, BAND_BOX)),
    "LocalDefeat": (build_band, [], (None, BAND_BOX)),
    "ObserverQuit": (build_band, [], (None, BAND_BOX)),
    "PopupReplay": (build_popup_replay, ["PopupReplayMenu", "PopupReplaySubMenu", "MenuButtonFrame", "ButtonBack",
                                         "ButtonSave", "ListboxGames", "TextEntryReplayName", "PopupReplaySaved",
                                         "StaticReplaySaved"], ("PopupReplaySubMenu", REPLAY_POPUP)),
    "PopupSaveLoad": (build_popup_saveload, ["SaveLoadMenu", "MenuButtonFrame", "ButtonBack", "ButtonSave",
                                             "ButtonLoad", "ButtonDelete", "ListboxGames", "LoadConfirmParent",
                                             "OverwriteConfirmParent", "SaveDescParent", "DeleteConfirmParent",
                                             "EntryDesc", "ButtonSaveDescConfirm", "ButtonSaveDescCancel",
                                             "ButtonLoadConfirm", "ButtonLoadCancel", "ButtonOverwriteConfirm",
                                             "ButtonOverwriteCancel", "ButtonDeleteConfirm", "ButtonDeleteCancel"],
                      (None, SAVE_POPUP)),
}
for _menu, (_entries, _) in RC_MENUS.items():
    LAYOUTS[_menu] = ((lambda m: lambda l: build_rc(l, m))(_menu), ["ParentRightClick" if _menu != "RCGameDetailsMenu"
                                                                   else "RCParent"] + _entries,
                      (None, rc_rect(_entries)))

# keys the family's code fetches; Patch.str has to carry each
KEYS = ["GUI:HintEscNoEnterYes", "GUI:HintEscEnterNo", "GUI:HintEscCancelEnterYes",
        "GUI:HintEscCancelEnterNo", "GUI:HintEscCancelEnterOk", "GUI:HintEscEnterOk", "GUI:HintEscCancel",
        "GUI:Paused", "GUI:InGameMenu", "GUI:EscReturns", "GUI:SurrenderConfirmation",
        "GUI:SurrenderConfirmationTitle", "GUI:WaitingForPlayers", "GUI:NotRespondingCount",
        "GUI:DisconnectPlayer", "GUI:DisconnectState", "GUI:DropsIn", "GUI:DropsIn_Short", "GUI:DisconnectVotes",
        "GUI:NotResponding", "GUI:NotResponding_Short", "GUI:Reconnecting", "GUI:Reconnecting_Short",
        "GUI:HostNotResponding", "GUI:ConnectedCaps", "Network:Vote", "Network:Vote_Short", "GUI:Voted",
        "GUI:Voted_Short", "GUI:Connecting", "GUI:ReadyCount", "GUI:WaitingForResponse",
        "GUI:WaitingForMangledPort", "GUI:ConnectionDone", "GUI:ConnectionFailed", "GUI:Downloading",
        "GUI:Friends", "GUI:PlayerCard", "GUI:AddFriend", "GUI:RemoveFriend",
        "GUI:SPNewSave", "GUI:SPSaveLoad", "GUI:SPHintEscEnterSaves", "GUI:SPDelete"]


def build(layout, menu):
    LAYOUTS[menu][0](layout)
    return layout


def read_strings():
    keys = set()
    with open(PATCH_STR, "r", encoding="utf-8") as fp:
        for line in fp:
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def selfcheck():
    problems = []
    for menu, (_, looked_up, (box_name, box)) in sorted(LAYOUTS.items()):
        path = os.path.join(MENUS, menu + ".wnd")
        if not os.path.exists(path):
            problems.append("%s.wnd is not tracked under Data/Window/Menus" % menu)
            continue
        with open(path, "rb") as fp:
            text = fp.read().decode("latin-1")
        layout = wndlayout.parse(text)
        before = len(problems)
        for name in looked_up:
            if layout.find(name) is None:
                problems.append("%s.wnd has no %s" % (menu, name))
        stale = [w.name for w in layout.root.walk() if tuple(w.rect[4:]) != BOARD]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first" % (menu, len(stale), stale[0]))
        window = layout.find(box_name) if box_name else layout.root
        if window is not None and tuple(window.rect[:4]) != box:
            problems.append("%s.wnd: the box is at %s, the design's is %s" % (menu, window.rect[:4], box))
        if box[2] - box[0] in (BOX_MENU, BOX_QUESTION, BOX_LIST) and (box[2] - box[0]) % 8:
            problems.append("%s.wnd: a box width off frame A's 8 px ticks" % menu)
        if len(problems) == before and build(wndlayout.parse(text), menu).text() != text:
            problems.append("%s.wnd is not what this script writes from it" % menu)
    # a box over the match never reaches the bar
    for menu in ("QuitMenu", "QuitNoSave", "DisconnectScreen", "PopupSaveLoad"):
        box = LAYOUTS[menu][2][1]
        if box[3] > BAR_TOP - 44:
            problems.append("%s.wnd reaches the command bar (%d)" % (menu, box[3]))
    keys = read_strings()
    for key in KEYS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)

    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1
    print("boxes: %d layouts, %d strings" % (len(LAYOUTS), len(KEYS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    indir, outdir = argv[1], argv[2]
    for menu in sorted(LAYOUTS):
        source = os.path.join(outdir, menu + ".wnd")
        if not os.path.exists(source):
            source = os.path.join(indir, menu + ".wnd")
        layout = build(wndlayout.load(source), menu)
        out = os.path.join(outdir, menu + ".wnd")
        wndlayout.save(layout, out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
