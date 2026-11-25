#!/usr/bin/env python3
"""Lay EA's two message boxes out as the menu language's question box, authored at 1920x1080.

The box (docs/plans/MENU-REDESIGN.md, package 2; the design is the boxes README under
experiments/proofs/2026-11-24/screens/boxes in the wrapper) is 720 wide, as tall as its text: an 82 px
title band, a line of air, 28 px a body line, a line, an 84 px foot with the keys at its right, 170
wide and 12 apart, Enter's key rightmost. MessageBox.cpp lays the windows out again from UIStyle's
numbers on every box it opens, since the height follows the text, and draws everything that is a
line, a frame or a word; this layout only has to carry the windows the code looks up, at the
one-line box's rects, so a box opened past MessageBox.cpp (MainMenu.cpp calls the window manager
directly for its resolution confirm) still stands where the design puts it.

    MessageBox.wnd, QuitMessageBox.wnd
        MessageBoxParent            the box, 600,415 to 1320,665 at one line
            StaticTextTitle         kept for lookups; the code hides it and draws the title
            StaticTextMessage       the same, for the body
            ButtonOk, ButtonYes     Enter's slot, 1118,597 to 1288,645
            ButtonNo                the next slot left
            ButtonCancel            the third
        (QuitMessageBox's rule and Logo are hidden: the language takes no art)

    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\MessageBox.wnd" -o wnd
    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\QuitMessageBox.wnd" -o wnd
    python boxes_layout.py wnd/Window/Menus ../Data/Window/Menus

writes the tracked masters under Code/Data; the build copies them to Run/Window/Menus/, where a
loose file beats WindowZH.big. Running it on its own output changes nothing.

    python boxes_layout.py selfcheck

reads the masters back and checks that every window the code looks up is there, that every window
is authored at 1920x1080, that the box and its keys stand on UIStyle's numbers, that the script
reproduces each master byte for byte from itself, and that Patch.str carries the box's hint keys.
Run by CTest as boxes_selfcheck. Layouts are not in the multiplayer INI checksum.
"""

import os
import sys

import wndlayout

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")

LAYOUTS = ["MessageBox", "QuitMessageBox"]
BOARD = (1920, 1080)

# UIStyle.h's box numbers; questionBoxHeight(1) is 82 + 28 + 28 + 28 + 84
BOX_WIDTH, BOX_PAD, BOX_HEAD, BOX_FOOT, BOX_LINE = 720, 32, 82, 84, 28
KEY_WIDTH, KEY_GAP, KEY_HEIGHT = 170, 12, 48
BOX_HEIGHT = BOX_HEAD + BOX_LINE + BOX_LINE + BOX_LINE + BOX_FOOT
BOX = ((BOARD[0] - BOX_WIDTH) // 2, (BOARD[1] - BOX_HEIGHT) // 2)
BOX = (BOX[0], BOX[1], BOX[0] + BOX_WIDTH, BOX[1] + BOX_HEIGHT)

# the key slot each button stands in, 0 being Enter's at the right (boxKeyRect1080)
KEY_SLOTS = [("ButtonOk", 0), ("ButtonYes", 0), ("ButtonNo", 1), ("ButtonCancel", 2)]
LOOKED_UP = ["MessageBoxParent", "StaticTextTitle", "StaticTextMessage"] + [k for k, _ in KEY_SLOTS]

# the windows the language drops: QuitMessageBox's rule and logo, and MessageBox's title band
DROPPED_UNNAMED = True
DROPPED = ["Logo"]

HINTS = ["GUI:HintEscNoEnterYes", "GUI:HintEscEnterNo", "GUI:HintEscCancelEnterYes",
         "GUI:HintEscCancelEnterNo", "GUI:HintEscCancelEnterOk", "GUI:HintEscEnterOk", "GUI:HintEscCancel"]


def key_rect(slot):
    right = BOX[2] - BOX_PAD - slot * (KEY_WIDTH + KEY_GAP)
    bottom = BOX[3] - 20
    return (right - KEY_WIDTH, bottom - KEY_HEIGHT, right, bottom)


def put(window, rect):
    left, top, right, bottom = rect
    window.rect = (left, top, right, bottom, BOARD[0], BOARD[1])


def hide(window):
    statement = window.prop("STATUS")
    flags = statement.split("=", 1)[1].strip().rstrip(";").strip()
    if "HIDDEN" not in flags.split("+"):
        window.set_prop("STATUS", flags + "+HIDDEN")


def build(layout, menu):
    put(layout.root, (0, 0, BOARD[0] - 1, BOARD[1] - 1))
    parent = layout.find("MessageBoxParent")
    if parent is None:
        raise SystemExit("%s has no MessageBoxParent" % menu)
    put(parent, BOX)
    put(layout.find("StaticTextTitle"), (BOX[0] + BOX_PAD, BOX[1] + 24, BOX[2] - BOX_PAD, BOX[1] + 64))
    top = BOX[1] + BOX_HEAD + BOX_LINE
    put(layout.find("StaticTextMessage"), (BOX[0] + BOX_PAD, top, BOX[2] - BOX_PAD, top + BOX_LINE))
    for name, slot in KEY_SLOTS:
        put(layout.find(name), key_rect(slot))
    for window in parent.children:
        short = (window.name or "").split(":")[-1]
        if short in LOOKED_UP:
            continue
        if short in DROPPED or (DROPPED_UNNAMED and short == ""):
            put(window, (BOX[0], BOX[1], BOX[0] + 1, BOX[1] + 1))
            hide(window)
        else:
            raise SystemExit("%s.wnd has a window this tool does not place: %r" % (menu, window.name))
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
    for menu in LAYOUTS:
        path = os.path.join(MENUS, menu + ".wnd")
        if not os.path.exists(path):
            problems.append("%s.wnd is not tracked under Data/Window/Menus" % menu)
            continue
        with open(path, "rb") as fp:
            text = fp.read().decode("latin-1")
        layout = wndlayout.parse(text)
        for name in LOOKED_UP:
            if layout.find(name) is None:
                problems.append("%s.wnd has no %s" % (menu, name))
        stale = [w.name for w in layout.root.walk() if tuple(w.rect[4:]) != BOARD]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first" % (menu, len(stale), stale[0]))
        parent = layout.find("MessageBoxParent")
        if parent is not None and tuple(parent.rect[:4]) != BOX:
            problems.append("%s.wnd: the box is at %s, UIStyle's one-line box is %s" % (menu, parent.rect[:4], BOX))
        for name, slot in KEY_SLOTS:
            window = layout.find(name)
            if window is not None and tuple(window.rect[:4]) != key_rect(slot):
                problems.append("%s.wnd: %s at %s, slot %d is %s" % (menu, name, window.rect[:4], slot, key_rect(slot)))
        if not problems and build(wndlayout.parse(text), menu).text() != text:
            problems.append("%s.wnd is not what this script writes from it" % menu)
    keys = read_strings()
    for key in HINTS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)

    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1
    print("boxes: %d layouts, box %s, %d hint strings" % (len(LAYOUTS), BOX, len(HINTS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    indir, outdir = argv[1], argv[2]
    for menu in LAYOUTS:
        layout = build(wndlayout.load(os.path.join(indir, menu + ".wnd")), menu)
        out = os.path.join(outdir, menu + ".wnd")
        wndlayout.save(layout, out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
