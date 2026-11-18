#!/usr/bin/env python3
"""Lay the online screens out on the staging room's grid, authored at 1920x1080.

EA's online screens (the login, the hub, Quick Match, the custom game lobby, the ladder and the two
popups) were drawn at 800x600 and stretched to the screen, and their callbacks moved and refonted
gadgets at init to make them fit. docs/plans/ONLINE-UI-REDESIGN.md in the wrapper puts every one
of them in the room's chrome instead, and this script writes the layouts that design stands on, the
way lobbyroom_layout.py writes the rooms: the MainMenuRuler art as a fitted RulerFrame window, the
room panel on the ruler's bright line at 96,72 to 1824,1008, the title at x 120 and the two pixel
header rule under it, the buttons on the room's row at 932 to 988, and on the four pages (hub,
Quick Match, lobby, ladder) five push buttons on the score screen's tab row, 158 to 190: TabHome,
TabQuickMatch, TabCustom, TabLadder and TabSkins. The two popups are cards of their own, solid
GROUND under a FRAME edge, centred on the board.

Nothing EA named goes. A window the design drops is kept HIDDEN, so every lookup the callbacks make
still finds it; the windows this script adds are boxes the code draws in (Panel*, Card*), static
texts the code writes into, and the buttons and lists the design names (ButtonPlayVsAI,
ButtonSignOut, ListboxRecent, ButtonSelectAllMaps, ButtonSelectNoMaps, the player card's TabQuickMatch
and TabAI). Every gadget keeps EA's
art: new ones are clones of a gadget in the same file, only rectangles, names, labels and the
panels' colours change. The callbacks still carry their own placements from before; they are
rewritten on top of these layouts.

    python online_layout.py <retail Menus dir> <Menus dir out>

reads the seven layouts as WindowZH.big ships them (bigfile.py extract) and writes the masters
under Code/Data/Window/Menus/. Running it on its own output changes nothing.

    python online_layout.py selfcheck

reads the tracked masters back and checks that every window the menu code looks up is there, that
the script reproduces each file byte for byte, that every window is authored at 1920x1080, that no
two visible gadgets overlap or leave the panel, and that every GUI:Online and GUI:QM label the
script puts on a window is in Patch.str. Run by CTest as online_layout_selfcheck. Layouts are not in
the multiplayer INI checksum.
"""

import os
import sys

import wndlayout
import lobbyroom_layout as room
from wndlayout import clone

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")

WIDE, OLD = room.WIDE, room.OLD
FRAME = room.FRAME                     # the room panel, 96,72 to 1824,1008
INSET = room.INNER_LEFT                # content from 124 to 1796

# the board (UIStyle.h ROOM_*): a main column of 1108 from 124 and a side column of 540 from 1256
MAIN, SIDE = (124, 1108), (1256, 540)
TOP, BOTTOM = 206, 916

# the button row: Back from the left, the wide button ending at 1796 as Play Game does
ROW_TOP, ROW_HEIGHT = room.BUTTON_TOP, room.BUTTON_HEIGHT
BACK = (INSET, ROW_TOP, 300, ROW_HEIGHT)
WIDE_BUTTON = (1308, ROW_TOP, 488, ROW_HEIGHT)
BESIDE_WIDE = (992, ROW_TOP, 300, ROW_HEIGHT)
RIGHT = (1496, ROW_TOP, 300, ROW_HEIGHT)
SECOND_RIGHT = (1180, ROW_TOP, 300, ROW_HEIGHT)

# The page tabs. A width is the wider of the English and Turkish label in Arial at 21 px plus 22
# each side, as the mockups draw a tab: HOME 63 / ANA SAYFA 118, QUICK MATCH 147 / HIZLI MAC 105,
# CUSTOM GAMES 173 / OZEL OYUNLAR 162, LADDER 85 / LIG 34, SKINS 63 / SKINLER 90 (PIL, arial.ttf).
TAB_TOP, TAB_HEIGHT, TAB_GAP = room.HEADER_TOP, room.HEADER_HEIGHT, 8
TABS = [("TabHome", "GUI:OnlineTabHome", 162), ("TabQuickMatch", "GUI:OnlineTabQuickMatch", 192),
        ("TabCustom", "GUI:OnlineTabCustom", 218), ("TabLadder", "GUI:OnlineLadderButton", 130),
        ("TabSkins", "GUI:OnlineSkinsButton", 134)]
# the open tab is the disabled button, its label ACTIVE as the options screen's (optionsmenu_layout.py)
TAB_CAPTION_COLOR = ("ENABLED:  255 255 255 255, ENABLEDBORDER:  0 0 0 255, "
                     "DISABLED: 186 255 12 255, DISABLEDBORDER: 0 2 0 255, "
                     "HILITE:   186 255 12 255, HILITEBORDER:   0 2 0 255")
BODY_FONT = 'NAME: "Arial", SIZE: 10, BOLD: 0'

# UIStyle's tokens
PANEL, FRAME_COLOR = room.PANEL, room.FRAME_COLOR
GROUND = (5, 7, 12, 255)
CLEAR = (0, 0, 0, 0)

# the popups: the player card as mocked (card.html), the host popup sized to its two fields
CARD = (360, 96, 1200, 896)
HOST_CARD = (600, 330, 720, 420)


def text(name, template, rect, body=True, label=None):
    """A static text cloned from a label, so it keeps the label's colours: a fixed label, or empty for
    the code to write into."""
    puts = {"FONT": BODY_FONT, "HEADERTEMPLATE": '"[NONE]"'} if body else {}
    if label:
        puts["TEXT"] = '"%s"' % label
    return dict(name=name, template=template, rect=rect, puts=puts,
                drops=("TOOLTIPTEXT",) if label else ("TEXT", "TOOLTIPTEXT"), align=True, beside=False)


def button(name, template, rect, label, tooltip=None):
    puts = {"TEXT": '"%s"' % label}
    if tooltip:
        puts["TOOLTIPTEXT"] = '"%s"' % tooltip
    return dict(name=name, template=template, rect=rect, puts=puts, drops=() if tooltip else ("TOOLTIPTEXT",),
                align=False, beside=True)


def listbox(name, template, rect):
    return dict(name=name, template=template, rect=rect, puts={}, drops=("TOOLTIPTEXT",), align=False, beside=True)


def inset(box, top=56, side=14):
    """A list inside a box: the box's heading band over it, the box's padding round it."""
    left, y, width, height = box
    return (left + side, y + top, width - 2 * side, height - top - side)


# Per screen, in the order build() applies them:
#   names     an unnamed window EA left, named so it can be found again: (name, (how, what))
#   reparent  a window moved under another, keeping its name: (name, new parent)
#   panel     the window drawn as the room panel; home, where added windows go (None: the root)
#   boxes     plain windows at the front of home, PANEL with a FRAME edge, the code draws inside them
#   framed    existing windows given a fill and an edge: (name, rect, fill, edge)
#   adds      gadgets cloned from one in the same file: a button or list appended beside it, a text
#             to home
#   above     a window moved to draw (and take clicks) over a sibling: (name, sibling); a later
#             sibling draws over an earlier one
#   place     every visible window's rectangle, (left, top, width, height) at 1080
#   labels    a kept gadget's new label: (name, string key)
#   hide      what the design drops, kept for the lookups
LOGIN_SIGN_IN = (1100, TOP, 696, BOTTOM - TOP)
LOGIN_SERVER = (MAIN[0], TOP, 952, BOTTOM - TOP)
HUB_CARDS = [(124, TOP, 544, 398), (692, TOP, 540, 398), (1256, TOP, 540, 398)]
HUB_RECENT = (MAIN[0], 622, MAIN[1], BOTTOM - 622)
HUB_MOTD = (SIDE[0], 622, SIDE[1], BOTTOM - 622)
QM_MAPS = (SIDE[0], TOP, SIDE[1], BOTTOM - TOP)
LOBBY_GAMES = (MAIN[0], TOP, MAIN[1], 404)
LOBBY_PLAYERS = (SIDE[0], TOP, SIDE[1], BOTTOM - TOP)


def card_button(box):
    """A hub card's button across its foot, 24 in from its edges as the mockup's."""
    left, top, width, height = box
    return (left + 24, top + 326, width - 48, ROW_HEIGHT)


# The player card's two tabs under its head, a page tab's look: QUICK MATCH 147 / HIZLI MAC 105 and
# VS AI 54 / BILGISAYARA KARSI 208 in Arial at 21 px, plus 22 each side
CARD_TABS = [(CARD[0] + 24, CARD[1] + 158, 192, TAB_HEIGHT), (CARD[0] + 224, CARD[1] + 158, 252, TAB_HEIGHT)]


def card_tab(name, label, rect):
    return dict(name=name, template="ButtonClose", rect=rect,
                puts={"TEXT": '"%s"' % label, "FONT": BODY_FONT, "HEADERTEMPLATE": '"[NONE]"',
                      "TEXTCOLOR": TAB_CAPTION_COLOR},
                drops=("TOOLTIPTEXT",), align=False, beside=True)


SCREENS = {
    "GameSpyLoginProfile": dict(
        panel="GadgetParent", home="GadgetParent", title="StaticTextTitle", title_text="GUI:OnlineLabel", rule="Line",
        tabs=False,
        labels={"StaticTextEmail": "GUI:OnlineLoginName", "ButtonLogin": "GUI:OnlineSignIn",
                "ButtonTOS": "GUI:OnlineForgotPassword"},
        names=[("PanelSignIn", ("parent", "ButtonLogin"))],
        # EA's sign-in box is a box here, and the gadgets it held stand in the panel beside it: a gadget
        # placed outside its parent takes no click
        reparent=[("Line", "GadgetParent"), ("ButtonBack", "GadgetParent"), ("StaticTextEmail", "GadgetParent"),
                  ("StaticTextPassword", "GadgetParent"), ("TextEntryPassword", "GadgetParent"),
                  ("CheckBoxRememberInfo", "GadgetParent"), ("ButtonLogin", "GadgetParent"),
                  ("ButtonCreateAccount", "GadgetParent"), ("ButtonTOS", "GadgetParent")],
        # the account field's list drops over the fields under it, and EA's terms popup over all of them
        above=[("ComboBoxEmail", "StaticTextBrowser"), ("ParentTOS", "ComboBoxEmail")],
        boxes=[("PanelServer", LOGIN_SERVER, False)],
        framed=[("PanelSignIn", LOGIN_SIGN_IN, PANEL, FRAME_COLOR)],
        # the server's counts are drawn in code between its heading and the message of the day
        adds=[text("StaticTextServer", "StaticTextPassword", (138, 226, 900, 32), body=False),
              text("StaticTextMOTD", "StaticTextPassword", (138, 412, 900, 300)),
              text("StaticTextLauncher", "StaticTextPassword", (138, 860, 900, 32), label="GUI:OnlineLoginLauncher"),
              text("StaticTextSignIn", "StaticTextPassword", (1114, 226, 668, 32), body=False,
                   label="GUI:OnlineLoginHeading"),
              text("StaticTextHelp", "StaticTextPassword", (1114, 266, 668, 80)),
              text("StaticTextKeepHint", "StaticTextPassword", (1152, 590, 620, 28), label="GUI:OnlineLoginKeepHint"),
              text("StaticTextStatus", "StaticTextPassword", (1114, 704, 668, 28)),
              text("StaticTextBrowser", "StaticTextPassword", (1114, 822, 668, 28), label="GUI:OnlineLoginBrowser")],
        place={"StaticTextEmail": (1114, 356, 668, 28), "ComboBoxEmail": (1124, 386, 648, 44),
               "StaticTextPassword": (1114, 456, 668, 28), "TextEntryPassword": (1124, 486, 648, 44),
               "CheckBoxRememberInfo": (1124, 556, 648, 32), "ButtonLogin": (1124, 642, 648, ROW_HEIGHT),
               "ButtonCreateAccount": (1124, 762, 316, 40), "ButtonTOS": (1456, 762, 316, 40),
               "ButtonBack": BACK},
        aligned=["StaticTextEmail", "StaticTextPassword"],
        hide=["ParentESRB", "ComboBoxLoginName", "StaticTextLoginName", "WinAge"]),

    "WOLWelcomeMenu": dict(
        panel="GadgetParent", home="GadgetParent", title="StaticTextTitle", title_text="GUI:OnlineLabel",
        rule="HeaderRule", tabs=True,
        labels={"ButtonQuickMatch": "GUI:OnlineHubFindMatch", "ButtonCustomMatch": "GUI:OnlineHubBrowse",
                "ButtonMyInfo": "GUI:OnlineHubMyCard"},
        names=[("HeaderRule", ("thin", "GadgetParent")), ("ButtonHolder", ("parent", "ButtonQuickMatch"))],
        reparent=[],
        boxes=[("CardPlayVsAI", HUB_CARDS[0], False), ("CardQuickMatch", HUB_CARDS[1], False),
               ("CardCustom", HUB_CARDS[2], False), ("CardRecent", HUB_RECENT, False),
               ("CardMOTD", HUB_MOTD, False)],
        framed=[("ButtonHolder", FRAME, CLEAR, CLEAR)],
        adds=[button("ButtonPlayVsAI", "ButtonQuickMatch", card_button(HUB_CARDS[0]), "GUI:OnlineHubPlay"),
              button("ButtonSignOut", "ButtonBack", RIGHT, "GUI:OnlineHubSignOut"),
              listbox("ListboxRecent", "InfoListbox", inset(HUB_RECENT))],
        place={"ButtonQuickMatch": card_button(HUB_CARDS[1]), "ButtonCustomMatch": card_button(HUB_CARDS[2]),
               "InfoListbox": inset(HUB_MOTD), "ButtonBack": BACK, "ButtonMyInfo": SECOND_RIGHT},
        aligned=[],
        hide=["ButtonOptions", "ButtonBuddies", "PlayerInfoParent"]),

    "WOLQuickMatchMenu": dict(
        panel="SubParent", home="SubParent", title="StaticTextTitle", title_text="GUI:OnlineTabQuickMatch",
        rule="Line", tabs=True,
        labels={"ButtonStart": "GUI:QMSearch", "ButtonStop": "GUI:QMStop"},
        names=[],
        reparent=[("ListBoxMapSelect", "SubParent")],
        boxes=[("PanelArmy", (MAIN[0], TOP, MAIN[1], 494), False), ("PanelColor", (MAIN[0], 716, MAIN[1], 200), False),
               ("PanelMaps", QM_MAPS, False),
               ("PanelSearch", (MAIN[0], TOP, MAIN[1], BOTTOM - TOP), True), ("PanelPicks", QM_MAPS, True)],
        framed=[],
        # EA's own tooltips for these two, which its ZH layout had dropped with the buttons
        adds=[button("ButtonSelectAllMaps", "ButtonBack", (1280, 846, 238, 40), "GUI:QMAll", "TOOLTIP:SelectAll"),
              button("ButtonSelectNoMaps", "ButtonBack", (1534, 846, 238, 40), "GUI:QMNone", "TOOLTIP:SelectNone")],
        place={"ListBoxMapSelect": (1280, 334, 492, 480), "ButtonBack": BACK, "ButtonWiden": BESIDE_WIDE,
               "ButtonStart": WIDE_BUTTON, "ButtonStop": WIDE_BUTTON},
        aligned=[],
        # Stop shares Search's place and shows while a search runs
        hide=["ButtonStop", "ButtonOptions", "ButtonBuddies", "ListboxQuickMatch", "StaticTextMatchingProgress",
              "ParentOptions", "ParentStats", "ComboBoxLadder", "ComboBoxMaxDisconnects", "ComboBoxMaxPing",
              "ComboBoxNumPlayers", "ComboBoxSide", "ComboBoxColor"]),

    "WOLCustomLobby": dict(
        panel="GadgetParent", home="GadgetParent", title="StaticTextTitle", title_text="GUI:OnlineTabCustom",
        rule="HeaderRule", tabs=True,
        labels={"ButtonJoin": "GUI:OnlineLobbyJoin", "ButtonHost": "GUI:OnlineLobbyHost"},
        names=[],
        reparent=[],
        boxes=[("PanelGames", LOBBY_GAMES, False), ("PanelPlayers", LOBBY_PLAYERS, False)],
        framed=[],
        adds=[],
        # ListboxGamesLarge is the game list: ZH dropped the small one (LobbyUtils.cpp GrabWindowInfo)
        place={"ParentGameListLarge": LOBBY_GAMES, "ListboxGamesLarge": inset(LOBBY_GAMES),
               "ComboBoxGroupRooms": (MAIN[0], 626, MAIN[1], 34),
               "ListboxChat": (MAIN[0], 668, MAIN[1], 198), "TextEntryChat": (MAIN[0], 874, MAIN[1], 42),
               "ListboxPlayers": inset(LOBBY_PLAYERS),
               "ButtonBack": BACK, "ButtonJoin": BESIDE_WIDE, "ButtonHost": WIDE_BUTTON},
        aligned=[],
        hide=["ButtonGameListToggle", "SliderChatAdjust", "ButtonBuddy", "ButtonEmote", "ParentSortButtons",
              "ComboBoxGroupRooms"]),

    # the ladder and the skins page are drawn in code over this layout's panel (WOLLadderScreen.cpp)
    "WOLLadderScreen": dict(
        panel="WindowLadder", home=None, title="StaticTextTitle", title_text="GUI:OnlineLadderButton",
        rule="HeaderRule", tabs=True,
        names=[], boxes=[], framed=[], adds=[],
        # EA's Back came before the panel, which drew over it once it covered the screen
        above=[("ButtonBack", "WindowLadder")],
        place={"ButtonBack": BACK},
        aligned=[],
        hide=[]),

    # the card's head, figures, boxes and last matches are drawn in code over PopupParentOld
    # (PopupPlayerInfo.cpp), so EA's nick, rank picture and rating line go with its statistics
    "PopupPlayerInfo": dict(
        panel=None, home="PopupParentOld", title=None, rule=None, tabs=False,
        labels={"ButtonCommunicator": "GUI:OnlineCardProfile"},
        names=[("StaticTextBattleHonors", ("text", "GUI:BattleHonors"))],
        reparent=[], boxes=[],
        framed=[("PopupParentOld", CARD, GROUND, FRAME_COLOR)],
        adds=[card_tab("TabQuickMatch", "GUI:OnlineTabQuickMatch", CARD_TABS[0]),
              card_tab("TabAI", "GUI:OnlineCardTabAI", CARD_TABS[1])],
        place={"PopupParent": CARD,
               "ButtonClose": (384, 912, 300, ROW_HEIGHT), "ButtonCommunicator": (1096, 912, 440, ROW_HEIGHT)},
        aligned=[],
        hide=["PlayerInfoParent", "ListboxInfo", "StaticTextBattleHonors", "CheckBoxAsianText",
              "CheckBoxNonAsianText", "ButtonDeleteAccount", "ButtonSetLocale", "StaticTextPlayerStatisticsLabel",
              "Line"]),

    "PopupHostGame": dict(
        panel=None, home="ParentHostPopUp", title=None, rule=None, tabs=False,
        names=[("StaticTextGameName", ("text", "GUI:GameName")), ("StaticTextPassword", ("text", "GUI:Password"))],
        reparent=[], boxes=[],
        framed=[("ParentHostPopUp", HOST_CARD, GROUND, FRAME_COLOR)],
        adds=[],
        place={"StaticTextGameName": (614, 354, 682, 28), "TextEntryGameName": (624, 386, 672, 44),
               "StaticTextPassword": (614, 446, 682, 28), "TextEntryGamePassword": (624, 478, 672, 44),
               "CheckBoxLimitArmies": (624, 542, 672, 32), "CheckBoxUseStats": (624, 586, 672, 32),
               "ButtonCancel": (624, 670, 300, ROW_HEIGHT), "ButtonCreateGame": (996, 670, 300, ROW_HEIGHT)},
        aligned=["StaticTextGameName", "StaticTextPassword"],
        hide=[]),
}

# Every window the callbacks look up that the retail layout has (WOLLoginMenu.cpp, WOLWelcomeMenu.cpp,
# WOLQuickMatchMenu.cpp, WOLLobbyMenu.cpp, LobbyUtils.cpp, WOLLadderScreen.cpp, PopupPlayerInfo.cpp,
# PopupHostGame.cpp, and PopulatePlayerInfoWindows over the hub and Quick Match). Names the code asks
# for that EA's ZH layouts never had (TextEntryLoginName, ButtonLadder, ListboxGames and the rest)
# stay absent, as they were.
STATS = ["StaticTextGamesPlayedValue", "StaticTextWinsValue", "StaticTextLossesValue", "StaticTextDisconnectsValue",
         "StaticTextBestStreakValue", "StaticTextStreak", "StaticTextStreakValue", "StaticTextWinPercentValue",
         "ProgressBarRank", "WinRank", "FactionImage", "StaticTextRank"]
ARMIES = ["USA", "AmericaSuperWeaponGeneral", "AmericaLaserGeneral", "AmericaAirForceGeneral", "China",
          "ChinaTankGeneral", "ChinaInfantryGeneral", "ChinaNukeGeneral", "GLA", "GLAToxinGeneral",
          "GLADemolitionGeneral", "GLAStealthGeneral"]
LOOKUPS = {
    "GameSpyLoginProfile": ["WOLLoginMenuParent", "GadgetParent", "ButtonBack", "ButtonCreateAccount", "ButtonLogin",
                            "ButtonTOS", "ButtonTOSOK", "CheckBoxRememberInfo", "ComboBoxEmail", "ComboBoxLoginName",
                            "ListboxTOS", "ParentESRB", "ParentTOS", "StaticTextESRBTop", "StaticTextEmail",
                            "StaticTextLoginName", "StaticTextPassword", "StaticTextTitle", "TextEntryDay",
                            "TextEntryMonth", "TextEntryPassword", "TextEntryYear", "WinAge"],
    "WOLWelcomeMenu": ["WOLWelcomeMenuParent", "ButtonBack", "ButtonBuddies", "ButtonCustomMatch", "ButtonMyInfo",
                       "ButtonOptions", "ButtonQuickMatch", "CommunityInfoParent", "InfoListbox", "StaticTextCommunity",
                       "StaticTextRank", "StaticTextTitle", "ProgressBarRank", "WinRank", "FactionImage"]
                      + ["Percent" + army for army in ARMIES],
    "WOLQuickMatchMenu": ["WOLQuickMatchMenuParent", "ButtonBack", "ButtonBuddies", "ButtonOptions", "ButtonStart",
                          "ButtonStop", "ButtonWiden", "ComboBoxColor", "ComboBoxLadder", "ComboBoxMaxDisconnects",
                          "ComboBoxMaxPing", "ComboBoxNumPlayers", "ComboBoxSide", "ListBoxMapSelect",
                          "ListboxQuickMatch", "ParentOptions", "ParentStats", "StaticTextAllowableMaps",
                          "StaticTextLadder", "StaticTextMatchingProgress", "StaticTextMaxDisconnects",
                          "StaticTextMaxPing", "StaticTextNumPlayers", "StaticTextTitle"] + STATS,
    "WOLCustomLobby": ["WOLLobbyMenuParent", "GadgetParent", "ButtonBack", "ButtonBuddy", "ButtonEmote",
                       "ButtonGameListToggle", "ButtonHost", "ButtonJoin", "ButtonRefresh", "ComboBoxGroupRooms",
                       "ListboxChat", "ListboxPlayers", "SliderChatAdjust", "TextEntryChat", "ParentGameListLarge",
                       "ListboxGamesLarge", "ButtonSortAlpha", "ButtonSortPing", "ButtonSortBuddies",
                       "WindowSortAlpha", "WindowSortPing", "WindowSortBuddies"],
    "WOLLadderScreen": ["LadderParent", "ButtonBack", "WindowLadder"],
    "PopupPlayerInfo": ["PopupParent", "ButtonClose", "ButtonCommunicator", "ListboxInfo", "ButtonSetLocale",
                        "ButtonDeleteAccount", "CheckBoxAsianText", "CheckBoxNonAsianText",
                        "StaticTextPlayerStatisticsLabel", "StaticTextTotalKillsValue", "StaticTextTotalDeathsValue",
                        "StaticTextTotalBuiltValue", "StaticTextBuildingsKilledValue", "StaticTextBuildingsLostValue",
                        "StaticTextBuildingsBuiltValue", "StaticTextInProgress"] + STATS,
    "PopupHostGame": ["ParentHostPopUp", "ButtonCancel", "ButtonCreateGame", "CheckBoxAllowObservers",
                      "CheckBoxLimitArmies", "CheckBoxUseStats", "TextEntryGameDescription", "TextEntryGameName",
                      "TextEntryGamePassword"],
}
# the hub's title and Back lend their lettering to the two pages that have neither
KIT = "WOLWelcomeMenu"


# ---------------------------------------------------------------------------
# small edits
# ---------------------------------------------------------------------------

def named(menu, name):
    return "%s.wnd:%s" % (menu, name)


def flags(window):
    return window.prop("STATUS").split("=", 1)[1].strip().rstrip(";").strip()


def hide(window):
    if "HIDDEN" not in flags(window).split("+"):
        window.set_prop("STATUS", flags(window) + "+HIDDEN")


def hidden(window):
    return "HIDDEN" in flags(window).split("+")


def drop_prop(window, key):
    start = window.prop_index(key)
    if start >= 0:
        end = start
        while ";" not in window.props[end]:
            end += 1
        del window.props[start:end + 1]


def paint(window, fill, edge):
    window.set_prop("ENABLEDDRAWDATA", room.draw_data(fill, edge))


def pick(layout, how, what):
    """An unnamed window by where EA left it: the parent of a named one, the label carrying a text, or
    the one pixel rule in a container."""
    if how == "parent":
        return layout.root.parent_of(room.need(layout, what))
    for node in layout.root.walk():
        if room.short(node) or node.children:
            continue
        if how == "text" and node.prop("TEXT") == 'TEXT = "%s";' % what:
            return node
        if how == "thin" and layout.root.parent_of(node) is room.need(layout, what) and node.rect[3] - node.rect[1] <= 2:
            return node
    raise KeyError("no unnamed window for %s %s" % (how, what))


def scale_old(layout):
    """A popup's rectangles move to where the stretch drew them on a 1920x1080 screen, its root too:
    room.widen would spread a popup's root over the whole screen."""
    sx, sy = WIDE[0] / OLD[0], WIDE[1] / OLD[1]
    for node in layout.root.walk():
        left, top, right, bottom, cw, ch = node.rect
        if (cw, ch) == OLD:
            node.rect = (round(left * sx), round(top * sy), round(right * sx), round(bottom * sy)) + WIDE


# ---------------------------------------------------------------------------
# the build
# ---------------------------------------------------------------------------

def add_tabs(layout, menu, home):
    template = room.need(layout, "ButtonBack")
    left = INSET
    for name, label, width in TABS:
        tab = layout.find(name)
        if tab is None:
            tab = clone(template, named(menu, name))
            home.children.append(tab)
        tab.place(left, TAB_TOP, width, TAB_HEIGHT)
        tab.put_prop("TEXT", '"%s"' % label)
        drop_prop(tab, "TOOLTIPTEXT")
        tab.put_prop("FONT", BODY_FONT)
        tab.put_prop("HEADERTEMPLATE", '"[NONE]"')
        tab.put_prop("TEXTCOLOR", TAB_CAPTION_COLOR)
        left += width + TAB_GAP


def add_header(layout, menu, spec, home, kit):
    title = layout.find(spec["title"])
    if title is None:
        title = clone(room.need(kit, "StaticTextTitle"), named(menu, spec["title"]))
        title.rect = title.rect[:4] + WIDE   # the kit can be the retail file, still at 800x600
        home.children.append(title)
    title.place(*room.TITLE)
    room.left_align(title)
    if spec["title_text"]:
        title.put_prop("TEXT", '"%s"' % spec["title_text"])
    rule = layout.find(spec["rule"])
    if rule is None:
        rule = room.plain_window(room.need(layout, spec["panel"]), named(menu, spec["rule"]), False, CLEAR, FRAME_COLOR)
        home.children.append(rule)
    rule.place(FRAME[0], room.RULE_TOP, FRAME[2], 0)
    paint(rule, CLEAR, FRAME_COLOR)


def build(layout, menu, kit):
    spec = SCREENS[menu]
    screen = spec["panel"] is not None
    if screen:
        room.widen(layout)
    else:
        scale_old(layout)

    for name, (how, what) in spec["names"]:
        if layout.find(name) is None:
            pick(layout, how, what).name = named(menu, name)
    for name, parent_name in spec.get("reparent", []):
        window, parent = room.need(layout, name), room.need(layout, parent_name)
        if window not in parent.children:
            layout.root.parent_of(window).children.remove(window)
            parent.children.append(window)

    home = room.need(layout, spec["home"]) if spec["home"] else layout.root
    if screen:
        panel = room.need(layout, spec["panel"])
        room.add_ruler(layout, menu, panel)
        panel.place(*FRAME)
        paint(panel, PANEL, FRAME_COLOR)
        add_header(layout, menu, spec, home, kit)
        if spec["tabs"]:
            add_tabs(layout, menu, home)
        if menu == "WOLLadderScreen":
            # EA set this Back in Times New Roman; the room's buttons are the Generals face
            back, model = room.need(layout, "ButtonBack"), room.need(kit, "ButtonBack")
            for key in ("FONT", "HEADERTEMPLATE"):
                back.put_prop(key, model.prop(key).split("=", 1)[1].strip().rstrip(";"))

    fresh = []
    for name, rect, starts_hidden in spec["boxes"]:
        box = layout.find(name)
        if box is None:
            box = room.plain_window(room.need(layout, spec["panel"]), named(menu, name), False, PANEL, FRAME_COLOR)
            fresh.append(box)
        box.place(*rect)
        paint(box, PANEL, FRAME_COLOR)
        if starts_hidden:
            hide(box)
    home.children[0:0] = fresh
    for name, rect, fill, edge in spec["framed"]:
        window = room.need(layout, name)
        window.place(*rect)
        paint(window, fill, edge)

    for add in spec["adds"]:
        window = layout.find(add["name"])
        if window is None:
            template = room.need(layout, add["template"])
            window = clone(template, named(menu, add["name"]))
            window.children = []
            (layout.root.parent_of(template) if add["beside"] else home).children.append(window)
        window.place(*add["rect"])
        for key, statement in sorted(add["puts"].items()):
            window.put_prop(key, statement)
        for key in add["drops"]:
            drop_prop(window, key)
        if add["align"]:
            room.left_align(window)
    for name, below in spec.get("above", []):
        window, under = room.need(layout, name), room.need(layout, below)
        siblings = layout.root.parent_of(window).children
        if siblings.index(window) < siblings.index(under):
            siblings.remove(window)
            siblings.insert(siblings.index(under) + 1, window)

    for name, rect in spec["place"].items():
        room.need(layout, name).place(*rect)
    for name in spec["aligned"]:
        room.left_align(room.need(layout, name))
    for name, label in sorted(spec.get("labels", {}).items()):
        room.need(layout, name).put_prop("TEXT", '"%s"' % label)
    if "Line" in spec["place"]:
        paint(room.need(layout, "Line"), CLEAR, FRAME_COLOR)
    for name in spec["hide"]:
        hide(room.need(layout, name))
    return layout


# ---------------------------------------------------------------------------
# selfcheck
# ---------------------------------------------------------------------------

def new_names(spec):
    names = [name for name, _how in spec["names"]] + [name for name, _rect, _hidden in spec["boxes"]]
    names += [add["name"] for add in spec["adds"]]
    if spec["panel"] is not None:
        names += [room.RULER_WINDOW, spec["title"], spec["rule"]]
    if spec["tabs"]:
        names += [name for name, _label, _width in TABS]
    return names


def labels(spec):
    """The labels this script puts on windows, as string keys."""
    keys = [label for _name, label, _width in TABS] if spec["tabs"] else []
    keys += [spec.get("title_text")] if spec.get("title_text") else []
    keys += list(spec.get("labels", {}).values())
    for add in spec["adds"]:
        keys += [statement.strip('"') for key, statement in add["puts"].items() if key in ("TEXT", "TOOLTIPTEXT")]
    return keys


def visible_gadgets(window, hidden_above=False):
    """Every gadget that draws: not a plain USER window, nor under a hidden one."""
    gone = hidden_above or hidden(window)
    if not gone and window.prop("WINDOWTYPE") != "WINDOWTYPE = USER;":
        yield window
    for child in window.children:
        for node in visible_gadgets(child, gone):
            yield node


def overlaps(gadgets):
    found = []
    boxes = [(room.short(node) or "(unnamed)", node.rect[:4]) for node in gadgets]
    for index, (name, (left, top, right, bottom)) in enumerate(boxes):
        for other, (other_left, other_top, other_right, other_bottom) in boxes[index + 1:]:
            if left < other_right and other_left < right and top < other_bottom and other_top < bottom:
                found.append("%s and %s overlap" % (name, other))
    return found


def patch_keys():
    with open(PATCH_STR, "rb") as fp:
        return set(line.strip() for line in fp.read().decode("utf-8", "replace").splitlines())


def selfcheck():
    problems = []
    known = patch_keys()
    kit_path = os.path.join(MENUS, KIT + ".wnd")
    kit = wndlayout.load(kit_path) if os.path.exists(kit_path) else None
    for menu, spec in sorted(SCREENS.items()):
        path = os.path.join(MENUS, menu + ".wnd")
        if not os.path.exists(path):
            problems.append("%s.wnd is not tracked" % menu)
            continue
        with open(path, "rb") as fp:
            tracked = fp.read().decode("latin-1")
        layout = wndlayout.parse(tracked)
        if layout.text() != tracked:
            problems.append("%s.wnd does not round trip through wndlayout.py" % menu)
        for name in LOOKUPS[menu] + new_names(spec):
            if layout.find(name) is None:
                problems.append("%s.wnd has no %s" % (menu, name))
        stale = [room.short(node) or "(unnamed)" for node in layout.root.walk() if tuple(node.rect[4:]) != WIDE]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first" % (menu, len(stale), stale[0]))
        try:
            if build(wndlayout.parse(tracked), menu, kit).text() != tracked:
                problems.append("%s.wnd is not what online_layout.py writes; regenerate it" % menu)
        except (KeyError, ValueError) as error:
            problems.append("%s.wnd no longer builds: %s" % (menu, error))
        gadgets = list(visible_gadgets(layout.root))
        problems.extend("%s.wnd: %s" % (menu, problem) for problem in overlaps(gadgets))
        if spec["panel"] is not None:
            left, top, right, bottom = FRAME[0], FRAME[1], FRAME[0] + FRAME[2], FRAME[1] + FRAME[3]
        else:
            left, top, right, bottom = layout.root.rect[:4]
        for node in gadgets:
            l, t, r, b = node.rect[:4]
            if l < left or t < top or r > right or b > bottom:
                problems.append("%s.wnd: %s runs out of the panel" % (menu, room.short(node) or "(unnamed)"))
        for key in labels(spec):
            if key.split(":")[1].startswith(("Online", "QM")) and key not in known:
                problems.append("%s.wnd: %s is not in Patch.str" % (menu, key))

    for problem in problems:
        print("online: %s" % problem)
    if problems:
        return 1
    print("online: %d layouts on the room's grid" % len(SCREENS))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    source, target = argv[1], argv[2]
    kit = wndlayout.load(os.path.join(source, KIT + ".wnd"))
    for menu in sorted(SCREENS):
        layout = build(wndlayout.load(os.path.join(source, menu + ".wnd")), menu, kit)
        out = os.path.join(target, menu + ".wnd")
        wndlayout.save(layout, out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
