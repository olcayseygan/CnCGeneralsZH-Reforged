#!/usr/bin/env python3
"""Write GameModeBox.wnd, the Create Game and Host buttons' mode question, from EA's MessageBox.wnd.

The question used to be MessageBox.wnd's Yes, No and Cancel relabelled, three buttons for two modes
and a way out.  A third mode needs a fourth button, and MessageBox.wnd is every message box in the
game, so the question has a layout of its own: EA's box cloned whole, keeping its frame, header band,
fonts and button art, every window renamed GameModeBox.wnd:..., and its buttons

    ButtonClassic       ButtonDirectStrike         the first row, where EA's Yes stood
    ButtonApocalypse    ButtonCancel               the second, where its No and Cancel stood

with Ok gone.  The body text stops above the first row.  GUIUtil.cpp's AskGameMode sizes the four to
the widest label and widens the box about its centre to hold two of them; GameModeBoxSystem
(FunctionLexicon) answers the picked mode and closes the box.

    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\MessageBox.wnd" -o wnd
    python gamemode_layout.py wnd/Window/Menus ../Data/Window/Menus

The output is a tracked master under Code/Data; the build copies it to Run/Window/Menus/.

    python gamemode_layout.py selfcheck

reads the tracked file back and checks it carries the four buttons, its system callback, and that
Patch.str carries every label it names.
"""

import os
import re
import sys

import wndlayout
from wndlayout import clone


HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")

SOURCE = "MessageBox"
MENU = "GameModeBox"
SYSTEM = "GameModeBoxSystem"

# EA's button -> ours, with its label; a label EA's CSF has needs no Patch.str line
BUTTONS = [
    ("ButtonYes", "ButtonClassic", "GUI:GameModeClassic"),
    ("ButtonNo", "ButtonDirectStrike", "GUI:GameModeDirectStrike"),
    ("ButtonCancel", "ButtonCancel", "GUI:Cancel"),
]
APOCALYPSE = ("ButtonApocalypse", "GUI:GameModeApocalypse")
FORK_STRINGS = ["GUI:GameModeTitle", "GUI:GameModeBody", "GUI:GameModeClassic", "GUI:GameModeDirectStrike",
                APOCALYPSE[1]]
# the body text ends this far above the first row of buttons
BODY_GAP = 8


def need(layout, name):
    window = layout.find(name)
    if window is None:
        raise SystemExit("%s.wnd has no %s" % (SOURCE, name))
    return window


def build(layout):
    for window in layout.root.walk():
        window.name = (window.name or "").replace(SOURCE + ".wnd:", MENU + ".wnd:")
    layout.root.set_prop("SYSTEMCALLBACK", '"%s"' % SYSTEM)

    first_row = need(layout, "ButtonYes").rect[1]
    second_row = need(layout, "ButtonCancel").rect[1]
    for old, new, label in BUTTONS:
        button = need(layout, old)
        button.name = "%s.wnd:%s" % (MENU, new)
        button.set_prop("TEXT", '"%s"' % label)
    panel = need(layout, "MessageBoxParent")
    ok = need(layout, "ButtonOk")
    panel.children.remove(ok)

    classic = need(layout, "ButtonClassic")
    cancel = need(layout, "ButtonCancel")
    apocalypse = clone(cancel, "%s.wnd:%s" % (MENU, APOCALYPSE[0]))
    apocalypse.set_prop("TEXT", '"%s"' % APOCALYPSE[1])
    panel.children.insert(panel.children.index(cancel), apocalypse)

    # two rows of two; AskGameMode sets the widths and the spacing at run time
    l, t, r, b = classic.rect[:4]
    need(layout, "ButtonDirectStrike").move_to(r + (l - panel.rect[0]), first_row)
    apocalypse.move_to(l, second_row)
    cancel.move_to(r + (l - panel.rect[0]), second_row)

    body = need(layout, "StaticTextMessage")
    bl, bt, br, bb, cw, ch = body.rect
    body.rect = (bl, bt, br, min(bb, first_row - BODY_GAP), cw, ch)
    return layout


def read_strings():
    keys = set()
    with open(PATCH_STR, "r", encoding="utf-8-sig") as fp:
        for line in fp:
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def selfcheck():
    problems = []
    keys = read_strings()
    for key in FORK_STRINGS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)
    path = os.path.join(MENUS, MENU + ".wnd")
    if not os.path.exists(path):
        problems.append("%s.wnd is not tracked under Data/Window/Menus" % MENU)
    else:
        layout = wndlayout.load(path)
        names = set((w.name or "").split(":")[-1] for w in layout.root.walk())
        for name in [new for _, new, _ in BUTTONS] + [APOCALYPSE[0], "MessageBoxParent", "StaticTextTitle",
                                                       "StaticTextMessage"]:
            if name not in names:
                problems.append("%s.wnd has no %s" % (MENU, name))
        if SYSTEM not in (layout.root.prop("SYSTEMCALLBACK") or ""):
            problems.append("%s.wnd's root does not call %s" % (MENU, SYSTEM))
        if any(SOURCE + ".wnd:" in (w.name or "") for w in layout.root.walk()):
            problems.append("%s.wnd still names a %s.wnd window" % (MENU, SOURCE))
    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1
    print("game mode box: four buttons, %d strings" % len(FORK_STRINGS))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    indir, outdir = argv[1], argv[2]
    layout = build(wndlayout.load(os.path.join(indir, SOURCE + ".wnd")))
    out = os.path.join(outdir, MENU + ".wnd")
    wndlayout.save(layout, out)
    print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
