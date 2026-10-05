#!/usr/bin/env python3
"""Give the three lobby screens a Lobby Settings tab and put every host setting on it.

The settings a host picks - starting money, peace time, the superweapon rule, and in the skirmish
lobby the game speed - shipped as a single row of controls squeezed between the player list and the
chat window.  That row was already full: the LAN lobby had three controls on it and no gap wide
enough for a fourth, which is why the peace time box went in at the far right edge with no label
beside it.  Anything else would have to go under the chat window, and there is nothing under the
chat window.

So the row becomes two tabs.  One shows what the screen already showed there - the chat log, or in
the skirmish lobby the map info list - and the other shows a page of settings, laid out as label and
control in two columns with room left over.  Nothing is redrawn: every control keeps the images,
font and colours it shipped with, and the new ones are cloned from a control that was already on the
screen.

The superweapon checkbox is the one control that does not survive.  It could say two things and the
rule underneath it can say three, so it is dropped and ComboBoxSuperweapons takes its place:

    allow    no limit at all
    limit    four of each superweapon per player
    no       one, which is what the old checkbox meant when it was ticked

That number goes down the wire as it always did.  GameInfo carries the restriction as an
UnsignedShort in the options string (SR=%u) and ThingTemplate::getMaxSimultaneousOfType hands it
straight back for every template whose MaxSimultaneousOfType is DeterminedBySuperweaponRestriction,
which is every superweapon building in the game.  So 0, 4 and 1 need no protocol change; a 1.04
client reading a 4 gets four, because that is what the field has always meant.

    python bigfile.py extract ../../Run/WindowZH.big "*options*" -o wnd
    python lobbysettings_layout.py wnd/Window/Menus ../Data/Window/Menus

writes all three masters from the shipped files.  The two network layouts also want the peace time
box, so this calls peacetime_layout.build for it rather than keeping a second copy of the knowledge
of where that box comes from; run this one, not that one, when regenerating the lobbies.  The
skirmish and LAN rooms then go through lobbyroom_layout.py, which puts them on their grid.

    python lobbysettings_layout.py selfcheck

reads the tracked files back and checks they still agree with the menu code: each layout carries the
page, both tabs and every control the code names, and every string the tab and the superweapon box
fetch is in Patch.str.  Run by CTest as lobbysettings_selfcheck.
"""

import os
import re
import sys

import peacetime_layout
import wndlayout
from wndlayout import clone


HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")

# The tab strip and the page, in each layout's own 800x600 creation resolution.  The page sits
# exactly where the window it replaces sits, so switching tabs does not move the eye.
LOBBIES = {
    "LanGameOptionsMenu": dict(
        container="GadgetParent",
        page=(78, 364, 636, 117),
        tabs=(78, 333, 150, 24),
        # the tab that shows what was there before, and the window it shows
        other=("TabChat", "GUI:LobbyTabChat", "ListboxChatWindowLanGame"),
        rename=[("GUI:StartingMoney", "StartingCashLabel")],
        move=[
            ("StartingCashLabel",     98, 376, 170, 24),
            ("ComboBoxStartingCash", 274, 376, 180, 24),
            ("LabelPeaceTime",        98, 408, 170, 24),
            ("ComboBoxPeaceTime",    274, 408, 180, 24),
            ("LabelSuperweapons",     98, 440, 170, 24),
            ("ComboBoxSuperweapons", 274, 440, 220, 24),
        ],
    ),
    "GameSpyGameOptionsMenu": dict(
        container="FadeParent",
        page=(77, 344, 636, 124),
        tabs=(77, 311, 150, 24),
        other=("TabChat", "GUI:LobbyTabChat", "ListboxChatWindowGameSpyGameSetup"),
        rename=[],
        move=[
            ("StartingCashLabel",     97, 356, 170, 24),
            ("ComboBoxStartingCash", 273, 356, 180, 24),
            ("LabelPeaceTime",        97, 388, 170, 24),
            ("ComboBoxPeaceTime",    273, 388, 180, 24),
            ("LabelSuperweapons",     97, 420, 170, 24),
            ("ComboBoxSuperweapons", 273, 420, 220, 24),
            # 28 apart rather than 32: the unit limit and Pro Rules boxes go under these two by
            # hand, and four rows at 32 do not fit in a page 124 high
            ("CheckBoxUseStats",     500, 352, 160, 24),
            ("CheckBoxLimitArmies",  500, 380, 160, 24),
        ],
    ),
    "SkirmishGameOptionsMenu": dict(
        container="SubParent",
        page=(300, 380, 446, 119),
        tabs=(303, 336, 130, 24),
        other=("TabInfo", "GUI:LobbyTabInfo", "ListboxInfo"),
        # EA's name for the game speed label is StaticTextBattleHonors1, which it has never been;
        # nothing in the code asks for it by that name
        rename=[("GUI:StartingMoney", "StartingCashLabel"),
                ("GUI:GameSpeed", "LabelGameSpeed")],
        move=[
            ("StartingCashLabel",    316, 392, 150, 24),
            ("ComboBoxStartingCash", 472, 392, 150, 24),
            ("LabelGameSpeed",       316, 424, 150, 24),
            ("SliderGameSpeed",      472, 424, 140, 27),
            ("StaticTextGameSpeed",  620, 424,  32, 21),
            ("LabelSuperweapons",    316, 456, 150, 24),
            ("ComboBoxSuperweapons", 472, 456, 180, 24),
        ],
    ),
}

TAB_SETTINGS = ("TabLobbySettings", "GUI:LobbyTabSettings")
PAGE = "PageLobbySettings"

# The checkbox the combo box replaces.  Two names for one control, because EA spelt it differently
# in the LAN layout than in the other two.
SUPERWEAPON_CHECKBOX = ("CheckboxLimitSuperweapons", "CheckBoxLimitSuperweapons")

# The unit limit and Pro Rules check boxes were put on all three pages by hand after the pages were
# generated, so build() does not make them; the selfcheck still wants them on every page, because the
# menu code names them and a missing control is as silent as the rest.
HAND_PLACED_CHECKBOXES = ("CheckBoxUnitLimit", "CheckBoxProRules")

# Income sharing, tech building respawn and the supply pile limit went on all three pages by hand
# the same way.
HAND_PLACED_COMBOS = ("LabelIncomeSharing", "ComboBoxIncomeSharing", "LabelTechRespawn", "ComboBoxTechRespawn",
                      "LabelSupplyPileLimit", "ComboBoxSupplyPileLimit")

# What build() moves out of EA's skirmish layout and a later hand edit replaced: the game speed
# slider and its number became one dropdown in the slider's place.  None is gone without a successor.
REPLACED_BY_HAND = {"SliderGameSpeed": "ComboBoxGameSpeed", "StaticTextGameSpeed": None}

# Pro Rules are for people playing each other, so the skirmish page lost its box again for v1.0.0 and
# the selfcheck wants it gone there rather than present.
NO_PRO_RULES_MENU = "SkirmishGameOptionsMenu"

# Cloned from a control that is already on the screen, so they inherit its images and font.  A
# layout gets the ones its own page has a row for: the skirmish lobby has no peace time, because a
# skirmish is played against computer players and those do not honour a truce.
NEW_CONTROLS = [
    ("LabelPeaceTime",       "label", "GUI:PeaceTimeLabel",  "TOOLTIP:PeaceTime"),
    ("LabelSuperweapons",    "label", "GUI:Superweapons",    "TOOLTIP:Superweapons"),
    ("ComboBoxSuperweapons", "combo", None,                  "TOOLTIP:Superweapons"),
]

# every string the tabs and the superweapon box fetch
STRINGS = [
    "GUI:LobbyTabChat",
    "GUI:LobbyTabInfo",
    "GUI:LobbyTabSettings",
    "GUI:PeaceTimeLabel",
    "GUI:Superweapons",
    "GUI:SuperweaponsAllow",
    "GUI:SuperweaponsLimit",
    "GUI:SuperweaponsNone",
    "TOOLTIP:Superweapons",
    "GUI:UnitLimit",
    "TOOLTIP:UnitLimit",
    "GUI:ProRules",
    "TOOLTIP:ProRules",
    "GUI:IncomeSharing",
    "TOOLTIP:IncomeSharing",
    "GUI:IncomeSharingOff",
    "GUI:IncomeSharingTech",
    "GUI:IncomeSharingAll",
    "GUI:TechRespawn",
    "TOOLTIP:TechRespawn",
    "GUI:TechRespawnOff",
    "GUI:TechRespawnFormat",
    "GUI:SupplyPileLimit",
    "TOOLTIP:SupplyPileLimit",
    "GUI:SupplyPileLimitOff",
    "GUI:SupplyPileLimitFormat",
    # both rooms' game mode box, which lobbyroom_layout.py puts at the top right
    "GUI:GameModeStandard",
    "GUI:ApocalypseWaves",
    "GUI:ApocalypseContinuous",
]


def drawn_text(window):
    statement = window.prop("TEXT")
    if statement is None:
        return None
    match = re.search(r'"([^"]*)"', statement)
    return match.group(1) if match else None


def detach(parent, name):
    for i, child in enumerate(parent.children):
        if (child.name or "").split(":")[-1] == name:
            return parent.children.pop(i)
    raise KeyError(name)


def build(layout, menu):
    spec = LOBBIES[menu]
    named = lambda name: "%s.wnd:%s" % (menu, name)

    # the peace time box is this file's business too now: the page has a labelled row waiting for it
    if menu in peacetime_layout.PLACEMENT and layout.find("ComboBoxPeaceTime") is None:
        layout = peacetime_layout.build(layout, menu)
        if menu == peacetime_layout.LISTBOX_TOP[0]:
            # peacetime_layout took 22 pixels off the top of the chat window to fit the box on the
            # old settings row.  The row is tabs now and the box is on the page, so give them back.
            listbox = layout.find(peacetime_layout.LISTBOX_TOP[1])
            l, t, r, b, cw, ch = listbox.rect
            listbox.rect = (l, spec["page"][1], r, b, cw, ch)

    container = layout.find(spec["container"])
    if container is None:
        raise SystemExit("%s has no %s" % (menu, spec["container"]))
    if "PassMessagesToParentSystem" not in (container.prop("SYSTEMCALLBACK") or ""):
        raise SystemExit("%s: %s does not pass messages up, so the page would swallow every click"
                         % (menu, spec["container"]))

    for text, name in spec["rename"]:
        for node in container.walk():
            if drawn_text(node) == text:
                node.name = named(name)
                break

    for name in SUPERWEAPON_CHECKBOX:
        try:
            detach(container, name)
        except KeyError:
            pass

    combo = layout.find("ComboBoxStartingCash")
    label = layout.find("StartingCashLabel")
    wanted = set(name for name, _l, _t, _w, _h in spec["move"])
    for name, kind, text, tooltip in NEW_CONTROLS:
        if name not in wanted:
            continue
        control = clone(label if kind == "label" else combo, named(name))
        control.children = []
        if text is not None:
            control.put_prop("TEXT", '"%s"' % text)
        control.put_prop("TOOLTIPTEXT", '"%s"' % tooltip)
        if control.prop("STATICTEXTDATA") is not None:
            # a label reads from the left; centred in 170 pixels it floats away from its control
            control.set_prop("STATICTEXTDATA", "CENTERED: 0")
        container.children.append(control)

    # The page is a clone of the screen's own container, which is a plain dark panel: the settings
    # then sit on the same background the chat window drew on.  It keeps that container's
    # PassMessagesToParentSystem, without which a click on anything inside it never reaches the
    # screen's system callback.
    page = clone(container, named(PAGE))
    page.children = []
    page.place(*spec["page"])
    page.set_prop("STATUS", "ENABLED+NOFOCUS")

    button = layout.find("ButtonSelectMap")
    left, top, width, height = spec["tabs"]
    tabs = []
    for index, (name, text) in enumerate([(spec["other"][0], spec["other"][1]), TAB_SETTINGS]):
        tab = clone(button, named(name))
        tab.children = []
        tab.place(left + index * (width + 4), top, width, height)
        tab.put_prop("TEXT", '"%s"' % text)
        tabs.append(tab)

    for name, l, t, w, h in spec["move"]:
        control = detach(container, name)
        control.place(l, t, w, h)
        page.children.append(control)

    # last, and in this order: a .wnd draws its children in order, so the page has to go on after
    # the window it covers and the tabs after the page
    container.children.append(page)
    container.children.extend(tabs)
    return layout


def read_strings():
    keys = set()
    with open(PATCH_STR, "r") as fp:
        for line in fp:
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def selfcheck():
    problems = []

    keys = read_strings()
    for key in STRINGS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)

    for menu in sorted(LOBBIES):
        spec = LOBBIES[menu]
        path = os.path.join(MENUS, menu + ".wnd")
        if not os.path.exists(path):
            problems.append("%s.wnd is not tracked under Data/Window/Menus" % menu)
            continue
        layout = wndlayout.load(path)

        page = layout.find(PAGE)
        if page is None:
            problems.append("%s.wnd has no %s" % (menu, PAGE))
        for name in (spec["other"][0], TAB_SETTINGS[0], spec["other"][2]):
            if layout.find(name) is None:
                problems.append("%s.wnd has no %s" % (menu, name))

        for moved, _l, _t, _w, _h in spec["move"]:
            name = REPLACED_BY_HAND.get(moved, moved)
            if name is None:
                continue
            control = layout.find(name)
            if control is None:
                problems.append("%s.wnd has no %s" % (menu, name))
            elif page is not None and control not in list(page.walk()):
                problems.append("%s.wnd: %s is not on the settings page" % (menu, name))

        for name in HAND_PLACED_CHECKBOXES:
            checkbox = layout.find(name)
            if menu == NO_PRO_RULES_MENU and name == "CheckBoxProRules":
                if checkbox is not None:
                    problems.append("%s.wnd still carries %s; a skirmish has no Pro Rules" % (menu, name))
            elif checkbox is None:
                problems.append("%s.wnd has no %s" % (menu, name))
            elif page is not None and checkbox not in list(page.walk()):
                problems.append("%s.wnd: %s is not on the settings page" % (menu, name))

        for name in HAND_PLACED_COMBOS:
            control = layout.find(name)
            if control is None:
                problems.append("%s.wnd has no %s" % (menu, name))
            elif page is not None and control not in list(page.walk()):
                problems.append("%s.wnd: %s is not on the settings page" % (menu, name))

        for name in SUPERWEAPON_CHECKBOX:
            if layout.find(name) is not None:
                problems.append("%s.wnd still carries %s; the combo box replaced it"
                                % (menu, name))

    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1

    print("lobby settings: %d layouts, %d strings" % (len(LOBBIES), len(STRINGS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()

    if len(argv) != 3:
        print(__doc__)
        return 2

    indir, outdir = argv[1], argv[2]
    for menu in sorted(LOBBIES):
        layout = wndlayout.load(os.path.join(indir, menu + ".wnd"))
        out = os.path.join(outdir, menu + ".wnd")
        wndlayout.save(build(layout, menu), out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
