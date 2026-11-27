#!/usr/bin/env python3
"""Lay the online screens out on frame A's board, authored at 1920x1080.

EA's online screens (the login, the hub, Quick Match, the custom game lobby, the ladder and the two
popups) were drawn at 800x600 and stretched to the screen. This script writes the masters the menu
language stands on (docs/plans/MENU-REDESIGN.md, packages 9 to 11; the online family's design is
experiments/proofs/2026-11-24/screens/online-entry/README-online-entry.md and header.py in the
wrapper): the panel on frame A at 32,28 to 1888,1052, content from x 60 to 1860, the title row 46 to
94 and the header rule at 110 (both hidden here, UIStyle::onlineHeader draws them from the master's
title), five tabs on the row 126 to 162 (TabHome, TabQuickMatch, TabCustom, TabLadder, TabSkins,
which the code sizes to their words), content from 182, the foot rule at 962 and the keys at 976 to
1028, 300 wide and 16 apart. The login is a dialog on 340,241 to 1580,839 with the same head offset
from its corner and no tabs. The popups are boxes centred on the board, clear here: the code draws
their glass and frame A.

Package 9 (login, hub, Quick Match) authored the board and its own three screens; package 10 the
Custom games lobby (README-game-lists.md in screens/game-lists) and the Host Game and Join Game boxes,
PopupJoinGame a master of its own since; package 11 the ladder's and the skins page's keys and the
player card (README-online-self.md in screens/online-self). The ladder's rows, the skins page's units,
looks and filters are buttons the code makes, as many as the data asks for.

Nothing EA named goes. A window the design drops is kept HIDDEN, so every lookup the callbacks make
still finds it; the windows this script adds are boxes the code draws in (Panel*, Card*), static
texts the code writes into, and the buttons and lists the design names (ButtonPlayVsAI,
ButtonSignOut, ListboxRecent, ButtonSelectAllMaps, ButtonSelectNoMaps, the player card's TabQuickMatch
and TabAI, the two ComboBoxGameSpeed of Play vs AI and Quick Match). New gadgets are clones of a
gadget in the same file (the hub's combo, on a screen with none, of Quick Match's ComboBoxSide); the
code draws every line and frame (UIStyle's menu language), so the panels and the hub's and Quick
Match's boxes are clear here.

    python online_layout.py <retail Menus dir> <Menus dir out>

reads the seven layouts as WindowZH.big ships them (bigfile.py extract) and writes the masters
under Code/Data/Window/Menus/. Running it on its own output changes nothing, and

    python online_layout.py rebuild

does exactly that to the tracked masters, for a change to this script.

    python online_layout.py selfcheck

reads the tracked masters back and checks that every window the menu code looks up is there, that
the script reproduces each file byte for byte, that every window is authored at 1920x1080, that no
two visible gadgets overlap or leave the panel, that the online pages' keys stand on the foot row and
inside the content edges, and that every GUI:Online and GUI:QM label the script puts on a window is in
Patch.str. Run by CTest as online_layout_selfcheck. Layouts are not in the multiplayer INI checksum.
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

# Frame A's board (UIStyle.h MENU_* and ONLINE_*), every number at 1080: the panel as (left, top,
# width, height), the content edges 28 in from its sides
FRAME = (32, 28, 1856, 1024)
INSET, INSET_RIGHT = 60, 1860
TITLE_TOP, TITLE_HEIGHT = 46, 48       # the title row, from the panel's top 18 down
RULE_TOP = 110                         # the header rule, 82 under the panel's top
TOP, BOTTOM = 182, 946                 # content: 20 under the tabs, 16 over the foot rule
# the columns: main 1176 from 60, side 600 ending at 1860, 24 apart
MAIN, SIDE = (INSET, 1176), (1260, 600)

# the foot: keys 300 wide and 16 apart, leaving keys from the left, the rest ending at 1860
ROW_TOP, ROW_HEIGHT = 976, 52
KEY, KEY_GAP = 300, 16
BACK = (INSET, ROW_TOP, KEY, ROW_HEIGHT)
RIGHT = (INSET_RIGHT - KEY, ROW_TOP, KEY, ROW_HEIGHT)
SECOND_RIGHT = (INSET_RIGHT - 2 * KEY - KEY_GAP, ROW_TOP, KEY, ROW_HEIGHT)
THIRD_RIGHT = (INSET_RIGHT - 3 * KEY - 2 * KEY_GAP, ROW_TOP, KEY, ROW_HEIGHT)

# The login's dialog (README-online-entry.md, "Login"): the head keeps its offsets from the corner,
# content from 102 under its top (no tabs), the foot rule 90 over its foot
LOGIN = (340, 241, 1240, 598)
LOGIN_X0, LOGIN_X1 = LOGIN[0] + 28, LOGIN[0] + LOGIN[2] - 28        # 368 to 1552
LOGIN_TOP = LOGIN[1] + 102                                         # 343
LOGIN_ROW_TOP = LOGIN[1] + LOGIN[3] - 90 + 14                      # 763

# The page tabs, from x 60, 8 apart. The code sizes each to its words in 16 px bold capitals plus 28
# a side (UIStyle::onlinePageInit); these widths only stand them roughly where they will be.
TAB_TOP, TAB_HEIGHT, TAB_GAP = 126, 36, 8
TABS = [("TabHome", "GUI:OnlineTabHome", 108), ("TabQuickMatch", "GUI:OnlineTabQuickMatch", 176),
        ("TabCustom", "GUI:OnlineTabCustom", 196), ("TabLadder", "GUI:OnlineLadderButton", 124),
        ("TabSkins", "GUI:OnlineSkinsButton", 112)]

# the open tab is the disabled button, its label ACTIVE as the options screen's (optionsmenu_layout.py)
TAB_CAPTION_COLOR = ("ENABLED:  255 255 255 255, ENABLEDBORDER:  0 0 0 255, "
                     "DISABLED: 186 255 12 255, DISABLEDBORDER: 0 2 0 255, "
                     "HILITE:   186 255 12 255, HILITEBORDER:   0 2 0 255")
BODY_FONT = 'NAME: "Arial", SIZE: 10, BOLD: 0'

# UIStyle's tokens
FRAME_COLOR = room.FRAME_COLOR
CLEAR = (0, 0, 0, 0)
# the boxes the code draws in, clear
BOX_DRAWN = (CLEAR, CLEAR)

# the popups: the player card (README-online-self.md, "Player card": 1240 by 900, centred, the code draws its
# glass and frame A and shrinks it while the history waits); Host Game and Join Game are boxes of the message
# box's pattern, 760 wide and centred (README-game-lists.md: 454 and 320 high), fields 32 in, keys 220 by 48,
# 16 apart (UIStyle's BOX_KEY_GAP, the 8 px grid) at the foot's right, 20 over its foot
CARD = (340, 90, 1240, 900)
HOST_CARD = (580, 313, 760, 454)
JOIN_CARD = (580, 380, 760, 320)
BOX_X0, BOX_W = 612, 696
BOX_KEY_W, BOX_KEY_H, BOX_KEY_GAP = 220, 48, 16


def box_key(box, index):
    """A box's key: index 0 is the rightmost, Enter's."""
    left, top, width, height = box
    right = left + width - 32 - index * (BOX_KEY_W + BOX_KEY_GAP)
    return (right - BOX_KEY_W, top + height - 20 - BOX_KEY_H, BOX_KEY_W, BOX_KEY_H)


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


def combo(name, rect, donor=None):
    """A shown combo box cloned from Quick Match's ComboBoxSide, last in home so its list drops over the
    gadgets under it. A screen without a combo of its own (the hub) takes the tracked Quick Match master's."""
    return dict(name=name, template="ComboBoxSide", donor=donor, rect=rect, puts={"STATUS": "ENABLED+IMAGE"},
                drops=("TOOLTIPTEXT",), align=False, beside=False)


def inset(box, top=56, side=14):
    """A list inside a box: the box's heading band over it, the box's padding round it."""
    left, y, width, height = box
    return (left + side, y + top, width - 2 * side, height - top - side)


# Per screen, in the order build() applies them:
#   names     an unnamed window EA left, named so it can be found again: (name, (how, what))
#   reparent  a window moved under another, keeping its name: (name, new parent)
#   frame     the panel's rect, FRAME unless given (the login's dialog)
#   panel     the window drawn as the panel (clear: the code draws frame A); home, where added windows
#             go (None: the root)
#   boxes     plain windows at the front of home, painted box_fill (clear by default), the code draws
#             inside them
#   framed    existing windows given a fill and an edge: (name, rect, fill, edge)
#   adds      gadgets cloned from one in the same file: a button or list appended beside it, a text
#             to home
#   above     a window moved to draw (and take clicks) over a sibling: (name, sibling); a later
#             sibling draws over an earlier one
#   place     every visible window's rectangle, (left, top, width, height) at 1080
#   labels    a kept gadget's new label: (name, string key)
#   hide      what the design drops, kept for the lookups
#   keys      the push buttons that stand on the foot row, checked by selfcheck

# The login (README-online-entry.md): the server column 368 to 928, the sign-in column 976 to 1552, both
# from 343; the section head over a lit rule, then a label 16 under it and its entry 48 high
LOGIN_SERVER = (LOGIN_X0, LOGIN_TOP, 560, 400)
LOGIN_SIGN_X, LOGIN_SIGN_W = 976, LOGIN_X1 - 976
LOGIN_SIGN_IN = (LOGIN_SIGN_X, LOGIN_TOP, LOGIN_SIGN_W, 400)
LOGIN_FIELDS = LOGIN_TOP + 26          # under the section head and its rule
LOGIN_HALF = (LOGIN_SIGN_W - 16) // 2

# The hub: Play vs AI and Quick Match 660 wide, Custom games 432, 24 apart, 182 to 530; the recent
# matches 1256 wide and the server box from 1340, 554 to 946; a card's key 52 high, 24 in, 24 over its foot
HUB_CARDS = [(60, TOP, 660, 348), (744, TOP, 660, 348), (1428, TOP, 432, 348)]
HUB_RECENT = (MAIN[0], 554, 1256, BOTTOM - 554)
HUB_MOTD = (1340, 554, INSET_RIGHT - 1340, BOTTOM - 554)
# Quick Match: the army box to 744, the colour box from 768, the picks box at the side, all to 946
QM_ARMY = (MAIN[0], TOP, MAIN[1], 744 - TOP)
QM_COLOUR = (MAIN[0], 768, MAIN[1], BOTTOM - 768)
QM_PICKS = (SIDE[0], TOP, SIDE[1], BOTTOM - TOP)
QM_SEARCH = (MAIN[0], TOP, MAIN[1], BOTTOM - TOP)
# Custom games (README-game-lists.md, "Rects at 1920x1080"): the left column 60 to 1310 holds the games (the
# head from 184, the column words at 220, rows of 40 from 250, eight of them, the list 4 px in from its top
# as the listbox draws, its scroll bar ending 1310, the lock legend under it), the room tabs and the chat with
# its entry and SEND; the right column 1334 to 1860 the picked game over the players (the head, the column
# words at 599, rows of 34 from 618). LobbyUtils.cpp's GAME_COLUMN_EDGES are the columns inside the list.
LOBBY_LEFT_W, LOBBY_RIGHT_X = 1310 - INSET, 1334
LOBBY_RIGHT_W = INSET_RIGHT - LOBBY_RIGHT_X
LOBBY_GAMES = (INSET, TOP, LOBBY_LEFT_W, 602 - TOP)
LOBBY_LIST = (INSET, 246, LOBBY_LEFT_W, 8 * 40 + 8)
LOBBY_TABS = (INSET, 606, LOBBY_LEFT_W, 30)
# the 8 px grid, as the LAN lobby's: the chat log ends 8 over the entry, the entry 8 before SEND, the players'
# rows 32 over the foot's keys (WOLLobbyMenu.cpp placeSideColumn keeps the same foot)
LOBBY_CHAT = (INSET, 644, LOBBY_LEFT_W, 900 - 644)
SEND_W = 120
LOBBY_ENTRY = (INSET, 908, LOBBY_LEFT_W - SEND_W - 8, 44)
LOBBY_SEND = (1310 - SEND_W, 908, SEND_W, 44)
LOBBY_PICKED = (LOBBY_RIGHT_X, TOP, LOBBY_RIGHT_W, 548 - TOP)
LOBBY_PLAYERS = (LOBBY_RIGHT_X, 558, LOBBY_RIGHT_W, BOTTOM - 558)
LOBBY_PLAYERS_FOOT = 944
LOBBY_PLAYER_LIST = (LOBBY_RIGHT_X, 618, LOBBY_RIGHT_W, LOBBY_PLAYERS_FOOT - 618)


def card_button(box):
    """A hub card's key across its foot: 24 in from its sides, its foot 24 over the card's."""
    left, top, width, height = box
    return (left + 24, top + height - 76, width - 48, ROW_HEIGHT)


# The match speed picked before a server-made match, a combo in a fact's value: Play vs AI's third fact
# (rows of 40 from 70 under the card's top, 236 wide ending 24 in), and the picks box's last row
# (README-online-entry.md "Your picks": the portrait 300 from 246, six rows of 52 from 566)
HUB_AI_SPEED = (HUB_CARDS[0][0] + HUB_CARDS[0][2] - 24 - 110, HUB_CARDS[0][1] + 70 + 2 * 40 + 2, 110, 32)
QM_SPEED = (SIDE[0] + SIDE[1] - 24 - 120, 566 + 5 * 52 + 10, 120, 32)


# Rejoin, at the recent matches' foot on the right: the card is the match still running while the server
# offers one (WOLWelcomeMenu.cpp), and the key shows only then
HUB_REJOIN = (HUB_RECENT[0] + HUB_RECENT[2] - 24 - KEY, HUB_RECENT[1] + HUB_RECENT[3] - 76, KEY, ROW_HEIGHT)


# The player card's two tabs under its head, 164 to 198 down from its top and 28 in; the code sizes each to
# its words (16 px bold capitals plus 22 a side, 8 apart, PopupPlayerInfo.cpp), these stand them roughly there.
# Its foot: CLOSE 220 wide at the right (Enter's, lit), PROFILE 380 wide 16 before it (UIStyle's BOX_KEY_GAP, the
# 8 px grid; ADD FRIEND is made in code 16 before that), 48 high, 20 over the foot
CARD_X0, CARD_X1, CARD_Y1 = CARD[0] + 28, CARD[0] + CARD[2] - 28, CARD[1] + CARD[3]
CARD_KEY_GAP = BOX_KEY_GAP
CARD_TABS = [(CARD_X0, CARD[1] + 164, 200, 34), (CARD_X0 + 208, CARD[1] + 164, 120, 34)]
CARD_CLOSE = (CARD_X1 - 220, CARD_Y1 - 68, 220, 48)
CARD_PROFILE = (CARD_X1 - 220 - CARD_KEY_GAP - 380, CARD_Y1 - 68, 380, 48)

# The ladder's and the skins page's keys (README-online-self.md): Previous and Next at the foot's right, Save
# where Next stands (the skins page shows it, the ladder hides it), Jump to my page in Your place, the
# platform's one key under the look's name
LADDER_JUMP = (1344, 448, 492, 48)         # top on the table's seventh row (256 + 6 x 32), no near-miss edge
SKINS_ACTION = (1364, 860, 472, 52)        # 64 over the foot's keys, the 8 px grid


def card_tab(name, label, rect):
    return dict(name=name, template="ButtonClose", rect=rect,
                puts={"TEXT": '"%s"' % label, "FONT": BODY_FONT, "HEADERTEMPLATE": '"[NONE]"',
                      "TEXTCOLOR": TAB_CAPTION_COLOR},
                drops=("TOOLTIPTEXT",), align=False, beside=True)


SCREENS = {
    "GameSpyLoginProfile": dict(
        frame=LOGIN,
        panel="GadgetParent", home="GadgetParent", title="StaticTextTitle", title_text="GUI:OnlineLabel", rule="Line",
        tabs=False,
        labels={"ButtonLogin": "GUI:OnlineSignIn", "ButtonTOS": "GUI:OnlineForgotPassword"},
        # the field's label is written by the code (WOLLoginMenu.cpp, GUI:OnlineLoginName in 14 px capitals
        # across the 576 wide column), not a one-line box of the master's font
        untext=["StaticTextEmail"],
        names=[("PanelSignIn", ("parent", "ButtonLogin"))],
        # EA's sign-in box is a box here, and the gadgets it held stand in the panel beside it: a gadget
        # placed outside its parent takes no click
        reparent=[("Line", "GadgetParent"), ("ButtonBack", "GadgetParent"), ("StaticTextEmail", "GadgetParent"),
                  ("StaticTextPassword", "GadgetParent"), ("TextEntryPassword", "GadgetParent"),
                  ("CheckBoxRememberInfo", "GadgetParent"), ("ButtonLogin", "GadgetParent"),
                  ("ButtonCreateAccount", "GadgetParent"), ("ButtonTOS", "GadgetParent")],
        # the account field's list drops over the fields under it, and EA's terms popup over all of them
        above=[("ComboBoxEmail", "StaticTextStatus"), ("ParentTOS", "ComboBoxEmail")],
        boxes=[("PanelServer", LOGIN_SERVER, False)],
        framed=[("PanelSignIn", LOGIN_SIGN_IN, CLEAR, CLEAR)],
        # the server column (heads, the server and its ping, the counts, the notice, the launcher line) and
        # the sign-in head are drawn in code; the message of the day is a text under the counts
        adds=[text("StaticTextServer", "StaticTextPassword", (LOGIN_X0, LOGIN_FIELDS + 12, 560, 34), body=False),
              text("StaticTextMOTD", "StaticTextPassword", (LOGIN_X0, LOGIN_FIELDS + 162, 560, 70)),
              text("StaticTextSignIn", "StaticTextPassword", (LOGIN_SIGN_X, LOGIN_TOP, LOGIN_SIGN_W, 20), body=False,
                   label="GUI:OnlineLoginHeading"),
              # 8 under the two half keys, so more than the grid's 64 under Sign In, its sibling
              text("StaticTextStatus", "StaticTextPassword", (LOGIN_SIGN_X, LOGIN_FIELDS + 356, LOGIN_SIGN_W, 22))],
        # a label 8 over its field, 16 between a field and the next label, the keep check 8 under the password
        place={"StaticTextEmail": (LOGIN_SIGN_X, LOGIN_FIELDS + 16, LOGIN_SIGN_W, 18),
               "ComboBoxEmail": (LOGIN_SIGN_X, LOGIN_FIELDS + 42, LOGIN_SIGN_W, 48),
               "StaticTextPassword": (LOGIN_SIGN_X, LOGIN_FIELDS + 106, LOGIN_SIGN_W, 18),
               "TextEntryPassword": (LOGIN_SIGN_X, LOGIN_FIELDS + 132, LOGIN_SIGN_W, 48),
               "CheckBoxRememberInfo": (LOGIN_SIGN_X, LOGIN_FIELDS + 188, LOGIN_SIGN_W, 32),
               "ButtonLogin": (LOGIN_SIGN_X, LOGIN_FIELDS + 236, LOGIN_SIGN_W, ROW_HEIGHT),
               "ButtonCreateAccount": (LOGIN_SIGN_X, LOGIN_FIELDS + 304, LOGIN_HALF, 44),
               "ButtonTOS": (LOGIN_X1 - LOGIN_HALF, LOGIN_FIELDS + 304, LOGIN_HALF, 44),
               "ButtonBack": (LOGIN_X0, LOGIN_ROW_TOP, KEY, ROW_HEIGHT)},
        aligned=["StaticTextEmail", "StaticTextPassword"],
        keys=["ButtonBack"],
        hide=["ParentESRB", "ComboBoxLoginName", "StaticTextLoginName", "WinAge", "StaticTextServer",
              "StaticTextSignIn"]),

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
              listbox("ListboxRecent", "InfoListbox", inset(HUB_RECENT)),
              button("ButtonRejoin", "ButtonQuickMatch", HUB_REJOIN, "GUI:OnlineRejoin"),
              combo("ComboBoxGameSpeed", HUB_AI_SPEED, donor="WOLQuickMatchMenu")],
        # the message of the day in the server box, under its name and over its facts
        place={"ButtonQuickMatch": card_button(HUB_CARDS[1]), "ButtonCustomMatch": card_button(HUB_CARDS[2]),
               "InfoListbox": (HUB_MOTD[0] + 24, HUB_MOTD[1] + 62, HUB_MOTD[2] - 48, 172),
               "ButtonBack": BACK, "ButtonMyInfo": SECOND_RIGHT},
        aligned=[],
        keys=["ButtonBack", "ButtonMyInfo", "ButtonSignOut"],
        hide=["ButtonOptions", "ButtonBuddies", "PlayerInfoParent", "ButtonRejoin", "ListboxRecent"]),

    "WOLQuickMatchMenu": dict(
        panel="SubParent", home="SubParent", title="StaticTextTitle", title_text="GUI:OnlineTabQuickMatch",
        rule="Line", tabs=True,
        labels={"ButtonStart": "GUI:QMSearch", "ButtonStop": "GUI:QMStop"},
        names=[],
        reparent=[("ListBoxMapSelect", "SubParent")],
        boxes=[("PanelArmy", QM_ARMY, False), ("PanelColor", QM_COLOUR, False), ("PanelMaps", QM_PICKS, False),
               ("PanelSearch", QM_SEARCH, True), ("PanelPicks", QM_PICKS, True)],
        framed=[],
        # EA's own tooltips for these two, which its ZH layout had dropped with the buttons; ranked maps are
        # generated, so they and the map list stay hidden
        adds=[button("ButtonSelectAllMaps", "ButtonBack", (1284, 880, 238, 40), "GUI:QMAll", "TOOLTIP:SelectAll"),
              button("ButtonSelectNoMaps", "ButtonBack", (1534, 880, 238, 40), "GUI:QMNone", "TOOLTIP:SelectNone"),
              combo("ComboBoxGameSpeed", QM_SPEED)],
        place={"ListBoxMapSelect": (1284, 334, 552, 480), "ButtonBack": BACK, "ButtonWiden": SECOND_RIGHT,
               "ButtonStart": RIGHT, "ButtonStop": RIGHT},
        aligned=[],
        keys=["ButtonBack", "ButtonWiden", "ButtonStart", "ButtonStop"],
        # Stop shares Search's place and shows while a search runs
        hide=["ButtonStop", "ListBoxMapSelect", "ButtonSelectAllMaps", "ButtonSelectNoMaps", "ButtonOptions",
              "ButtonBuddies", "ListboxQuickMatch", "StaticTextMatchingProgress", "ParentOptions", "ParentStats",
              "ComboBoxLadder", "ComboBoxMaxDisconnects", "ComboBoxMaxPing", "ComboBoxNumPlayers", "ComboBoxSide",
              "ComboBoxColor"]),

    "WOLCustomLobby": dict(
        panel="GadgetParent", home="GadgetParent", title="StaticTextTitle", title_text="GUI:OnlineTabCustom",
        rule="HeaderRule", tabs=True,
        labels={"ButtonJoin": "GUI:OnlineLobbyJoin", "ButtonHost": "GUI:OnlineLobbyHost", "ButtonEmote": "GUI:RoomSend"},
        names=[],
        reparent=[],
        boxes=[("PanelGames", LOBBY_GAMES, False), ("PanelPicked", LOBBY_PICKED, False),
               ("PanelPlayers", LOBBY_PLAYERS, False)],
        framed=[],
        # Host Game asks the three modes (AskGameMode); the Mods key an earlier layout added stays, hidden, for
        # anything that still looks it up
        adds=[button("ButtonMods", "ButtonHost", THIRD_RIGHT, "GUI:Mods", "TOOLTIP:Mods")],
        # ListboxGamesLarge is the game list: ZH dropped the small one (LobbyUtils.cpp GrabWindowInfo); the
        # hidden room combo keeps the strip the room tabs are drawn in; ButtonEmote is SEND
        place={"ParentGameListLarge": LOBBY_GAMES, "ListboxGamesLarge": LOBBY_LIST,
               "ComboBoxGroupRooms": LOBBY_TABS, "ListboxChat": LOBBY_CHAT, "TextEntryChat": LOBBY_ENTRY,
               "ButtonEmote": LOBBY_SEND, "ListboxPlayers": LOBBY_PLAYER_LIST,
               "ButtonBack": BACK, "ButtonJoin": SECOND_RIGHT, "ButtonHost": RIGHT},
        aligned=[],
        keys=["ButtonBack", "ButtonJoin", "ButtonHost"],
        hide=["ButtonGameListToggle", "SliderChatAdjust", "ButtonBuddy", "ButtonMods", "ParentSortButtons",
              "ComboBoxGroupRooms"]),

    # the ladder and the skins page are drawn in code over this layout's panel (WOLLadderScreen.cpp)
    "WOLLadderScreen": dict(
        panel="WindowLadder", home=None, title="StaticTextTitle", title_text="GUI:OnlineLadderButton",
        rule="HeaderRule", tabs=True,
        names=[], boxes=[], framed=[],
        adds=[button("ButtonPrev", "ButtonBack", SECOND_RIGHT, "GUI:OnlineLadderPrev"),
              button("ButtonNext", "ButtonBack", RIGHT, "GUI:OnlineLadderNext"),
              button("ButtonSave", "ButtonBack", RIGHT, "GUI:OnlineSkinsSave"),
              button("ButtonJump", "ButtonBack", LADDER_JUMP, "GUI:OnlineLadderJump"),
              button("ButtonAction", "ButtonBack", SKINS_ACTION, "GUI:OnlineSkinsWear")],
        # EA's Back came before the panel, which drew over it once it covered the screen
        above=[("ButtonBack", "WindowLadder")],
        place={"ButtonBack": BACK},
        aligned=[],
        keys=["ButtonBack", "ButtonPrev", "ButtonNext", "ButtonSave"],
        # the master opens as the ladder; the skins page swaps the keys over (WOLLadderScreen.cpp)
        hide=["ButtonSave", "ButtonAction"]),

    # the card's head, figures, boxes and last matches are drawn in code over PopupParentOld
    # (PopupPlayerInfo.cpp), so EA's nick, rank picture and rating line go with its statistics
    "PopupPlayerInfo": dict(
        panel=None, home="PopupParentOld", title=None, rule=None, tabs=False,
        labels={"ButtonCommunicator": "GUI:OnlineCardProfile"},
        names=[("StaticTextBattleHonors", ("text", "GUI:BattleHonors"))],
        reparent=[], boxes=[],
        framed=[("PopupParentOld", CARD, CLEAR, CLEAR)],
        adds=[card_tab("TabQuickMatch", "GUI:OnlineTabQuickMatch", CARD_TABS[0]),
              card_tab("TabAI", "GUI:OnlineCardTabAI", CARD_TABS[1])],
        place={"PopupParent": CARD, "ButtonClose": CARD_CLOSE, "ButtonCommunicator": CARD_PROFILE},
        aligned=[],
        hide=["PlayerInfoParent", "ListboxInfo", "StaticTextBattleHonors", "CheckBoxAsianText",
              "CheckBoxNonAsianText", "ButtonDeleteAccount", "ButtonSetLocale", "StaticTextPlayerStatisticsLabel",
              "Line"]),

    # Host Game, a box of the message box's pattern (README-game-lists.md, "lists-host-popup"): the code draws
    # the shell, the title with the mode it answered, the field labels and the armies' line; the fields and
    # keys stand here. Use Stats is a GameSpy box an online game never asks (PopupHostGame.cpp hides it)
    "PopupHostGame": dict(
        panel=None, home="ParentHostPopUp", title=None, rule=None, tabs=False,
        names=[("StaticTextGameName", ("text", "GUI:GameName")), ("StaticTextPassword", ("text", "GUI:Password"))],
        reparent=[], boxes=[],
        framed=[("ParentHostPopUp", HOST_CARD, CLEAR, CLEAR)],
        adds=[],
        place={"StaticTextGameName": (BOX_X0, 416, BOX_W, 26), "TextEntryGameName": (BOX_X0, 445, BOX_W, 48),
               "StaticTextPassword": (BOX_X0, 512, BOX_W, 26), "TextEntryGamePassword": (BOX_X0, 541, BOX_W, 48),
               "CheckBoxLimitArmies": (BOX_X0, 620, BOX_W, 28), "CheckBoxUseStats": (BOX_X0, 620, BOX_W, 28),
               "ButtonCancel": box_key(HOST_CARD, 1), "ButtonCreateGame": box_key(HOST_CARD, 0)},
        aligned=[],
        hide=["StaticTextGameName", "StaticTextPassword", "CheckBoxUseStats"]),

    # Join Game, for a password game (README-game-lists.md, "lists-join-password"): EA's popup had no join key,
    # Enter in the field joined; JOIN is Cancel cloned. The title, the game's name and the label are the code's
    "PopupJoinGame": dict(
        panel=None, home="ParentJoinPopUp", title=None, rule=None, tabs=False,
        names=[("StaticTextGameLabel", ("text", "GUI:GameName")), ("StaticTextPassword", ("text", "GUI:Password"))],
        reparent=[], boxes=[],
        framed=[("ParentJoinPopUp", JOIN_CARD, CLEAR, CLEAR)],
        adds=[button("ButtonJoin", "ButtonCancel", box_key(JOIN_CARD, 0), "GUI:OnlineLobbyJoin")],
        place={"TextEntryGamePassword": (BOX_X0, JOIN_CARD[1] + 140, BOX_W, 48), "ButtonCancel": box_key(JOIN_CARD, 1),
               "StaticTextGameName": (BOX_X0, JOIN_CARD[1] + 46, BOX_W, 26),
               "StaticTextGameLabel": (BOX_X0, JOIN_CARD[1] + 110, BOX_W, 26),
               "StaticTextPassword": (BOX_X0, JOIN_CARD[1] + 110, BOX_W, 26)},
        labels={"ButtonCancel": "GUI:Cancel"},
        aligned=[],
        hide=["StaticTextGameName", "StaticTextGameLabel", "StaticTextPassword"]),
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
                       "ButtonGameListToggle", "ButtonHost", "ButtonMods", "ButtonJoin", "ButtonRefresh", "ComboBoxGroupRooms",
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
    "PopupJoinGame": ["ParentJoinPopUp", "TextEntryGamePassword", "StaticTextGameName", "ButtonCancel", "ButtonJoin"],
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


def frame_of(spec):
    return spec.get("frame", FRAME)


def add_header(layout, menu, spec, home, kit):
    """The title and its rule, at their offsets from the panel's top left, hidden: UIStyle::onlineHeader
    draws both and reads the title's words from here."""
    left, top, width, _height = frame_of(spec)
    dy = top - FRAME[1]
    title = layout.find(spec["title"])
    if title is None:
        title = clone(room.need(kit, "StaticTextTitle"), named(menu, spec["title"]))
        title.rect = title.rect[:4] + WIDE   # the kit can be the retail file, still at 800x600
        home.children.append(title)
    title.place(left + 28, TITLE_TOP + dy, 640, TITLE_HEIGHT)
    room.left_align(title)
    if spec["title_text"]:
        title.put_prop("TEXT", '"%s"' % spec["title_text"])
    hide(title)
    rule = layout.find(spec["rule"])
    if rule is None:
        rule = room.plain_window(room.need(layout, spec["panel"]), named(menu, spec["rule"]), False, CLEAR, FRAME_COLOR)
        home.children.append(rule)
    rule.place(left, RULE_TOP + dy, width, 0)
    paint(rule, CLEAR, CLEAR)
    hide(rule)


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
        panel.place(*frame_of(spec))
        paint(panel, CLEAR, CLEAR)		# glass and frame A are the code's (UIStyle::onlineHeader)
        add_header(layout, menu, spec, home, kit)
        if spec["tabs"]:
            add_tabs(layout, menu, home)
        if menu == "WOLLadderScreen":
            # EA set this Back in Times New Roman; the room's buttons are the Generals face
            back, model = room.need(layout, "ButtonBack"), room.need(kit, "ButtonBack")
            for key in ("FONT", "HEADERTEMPLATE"):
                back.put_prop(key, model.prop(key).split("=", 1)[1].strip().rstrip(";"))

    fresh = []
    fill, edge = spec.get("box_fill", BOX_DRAWN)
    for name, rect, starts_hidden in spec["boxes"]:
        box = layout.find(name)
        if box is None:
            box = room.plain_window(room.need(layout, spec["panel"]), named(menu, name), False, fill, edge)
            fresh.append(box)
        box.place(*rect)
        paint(box, fill, edge)
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
            donor = wndlayout.load(os.path.join(MENUS, add["donor"] + ".wnd")) if add.get("donor") else layout
            template = room.need(donor, add["template"])
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
    for name in spec.get("untext", []):
        drop_prop(room.need(layout, name), "TEXT")
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
        # WND writers use CRLF; Git checkouts may use LF. Compare all content, including final newlines.
        lf = tracked.replace("\r\n", "\n")
        if layout.text().replace("\r\n", "\n") != lf:
            problems.append("%s.wnd does not round trip through wndlayout.py" % menu)
        for name in LOOKUPS[menu] + new_names(spec):
            if layout.find(name) is None:
                problems.append("%s.wnd has no %s" % (menu, name))
        stale = [room.short(node) or "(unnamed)" for node in layout.root.walk() if tuple(node.rect[4:]) != WIDE]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first" % (menu, len(stale), stale[0]))
        try:
            if build(wndlayout.parse(tracked), menu, kit).text().replace("\r\n", "\n") != lf:
                problems.append("%s.wnd is not what online_layout.py writes; regenerate it" % menu)
        except (KeyError, ValueError) as error:
            problems.append("%s.wnd no longer builds: %s" % (menu, error))
        gadgets = list(visible_gadgets(layout.root))
        problems.extend("%s.wnd: %s" % (menu, problem) for problem in overlaps(gadgets))
        if spec["panel"] is not None:
            frame = frame_of(spec)
            left, top, right, bottom = frame[0], frame[1], frame[0] + frame[2], frame[1] + frame[3]
        else:
            left, top, right, bottom = layout.root.rect[:4]
        for node in gadgets:
            l, t, r, b = node.rect[:4]
            if l < left or t < top or r > right or b > bottom:
                problems.append("%s.wnd: %s runs out of the panel" % (menu, room.short(node) or "(unnamed)"))
        problems.extend("%s.wnd: %s" % (menu, problem) for problem in key_row(layout, spec))
        for key in labels(spec):
            if key.split(":")[1].startswith(("Online", "QM")) and key not in known:
                problems.append("%s.wnd: %s is not in Patch.str" % (menu, key))

    for problem in problems:
        print("online: %s" % problem)
    if problems:
        return 1
    print("online: %d layouts on frame A's board" % len(SCREENS))
    return 0


def key_row(layout, spec):
    """The foot's keys: 300 by 52 on the row 14 under the foot rule, inside the content edges, on the
    16 px pitch from the left or from the right edge."""
    left, top, width, height = frame_of(spec)
    x0, x1, row = left + 28, left + width - 28, top + height - 90 + 14
    found = []
    for name in spec.get("keys", []):
        l, t, r, b = room.need(layout, name).rect[:4]
        pitch = KEY + KEY_GAP
        if (r - l, b - t) != (KEY, ROW_HEIGHT) or t != row:
            found.append("%s is not a key on the foot row" % name)
        elif l < x0 or r > x1 or ((l - x0) % pitch and (x1 - r) % pitch):
            found.append("%s is off the keys' pitch" % name)
    return found


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) == 2 and argv[1] == "rebuild":
        kit = wndlayout.load(os.path.join(MENUS, KIT + ".wnd"))
        for menu in sorted(SCREENS):
            path = os.path.join(MENUS, menu + ".wnd")
            wndlayout.save(build(wndlayout.load(path), menu, kit), path)
            print("%s rebuilt" % path)
        return 0
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
