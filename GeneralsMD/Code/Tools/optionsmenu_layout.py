#!/usr/bin/env python3
"""Rebuild OptionsMenu.wnd as seven tabbed pages on one grid.

EA's options screen is one 800x600 panel with everything on it at once, and it was already full
when it shipped: the language filter, the keyboard button and the four camera checkboxes are all
still in the file, parked off the right edge with HIDDEN set, because there was nowhere left to put
them.  Seventeen settings later there is no version of "find room" that works.

So the screen becomes seven pages behind seven buttons: Display, Graphics, Effects, Audio, Controls,
Gameplay and Network.  Every graphics setting is on Graphics or Effects, including the ones EA hid in
a popup that only opened when Custom was picked from a combo box on another page.  Every page is laid out on the same
grid of three titled groups, so a heading, a label and a box start in the same place whichever tab
is open, and every slider has a readout beside it.  Nothing is redrawn: the controls keep the images and tooltips they
shipped with, a label or a check box takes the lettering of the one next to it, and what is new is
cloned from a control that is already there.

    python optionsmenu_layout.py <shipped OptionsMenu.wnd> <output .wnd>

The input is the file out of WindowZH.big:

    python bigfile.py extract ../../Run/WindowZH.big "*/OptionsMenu.wnd" -o wnd
    python optionsmenu_layout.py wnd/Window/Menus/OptionsMenu.wnd ../Data/Window/Menus/OptionsMenu.wnd

The output is the tracked master under Code/Data; the build copies it to Run/Window/Menus/, where a
loose file beats the archive.  Layouts are not in the multiplayer INI checksum, so this one does not
have to match across a network game.

    python optionsmenu_layout.py selfcheck

reads the three tracked files back and checks they still agree: every widget TheOptionCatalog names
exists in the layout, and every label, tooltip and combo box entry it needs is in Patch.str.  Run by
CTest as optionsmenu_selfcheck.
"""

import os
import re
import sys

import wndlayout
from wndlayout import clone


# The panel and everything on it, in the layout's own 800x600 creation resolution.  One inner edge,
# 16 pixels in from the panel on both sides, is where the title, the tabs, the pages, the buttons
# and the version line all start and stop.  The frame is as tall as the tallest group column needs -
# Graphics' Effects column ends 308 pixels below its heading, its Image column 304 - plus the title,
# the tabs and the buttons, and it is centred on the 600 line: 58 above it, 58 below.  Seven tabs
# still fit the inner width at 100 pixels each.
PANEL = (20, 58, 760, 484)          # left, top, width, height
INNER_LEFT, INNER_WIDTH = 36, 728
TITLE = (INNER_LEFT, 64, 400, 32)
RULE = (20, 100, 760, 1)
TAB_TOP, TAB_HEIGHT, TAB_GAP = 108, 28, 4
PAGE = (INNER_LEFT, 142, INNER_WIDTH, 320)
BUTTON_TOP, BUTTON_HEIGHT, BUTTON_WIDTH = 478, 32, 180
VERSION = (INNER_LEFT, 518, INNER_WIDTH, 16)

TABS = [
    ("PageDisplay",  "TabDisplay",  "GUI:OptionsTabDisplay"),
    ("PageGraphics", "TabGraphics", "GUI:OptionsTabGraphics"),
    ("PageEffects",  "TabEffects",  "GUI:OptionsTabEffects"),
    ("PageAudio",    "TabAudio",    "GUI:OptionsTabAudio"),
    ("PageControls", "TabControls", "GUI:OptionsTabControls"),
    ("PageGameplay", "TabGameplay", "GUI:OptionsTabGameplay"),
    ("PageNetwork",  "TabNetwork",  "GUI:OptionsTabNetwork"),
]

# EA's four group panels.  Each drew a framed box of its own inside the page, sized for a heading
# and a row that are gone, so no two pages had their first control in the same place.  The panels
# go and their controls stand on the page; the two volume sliders EA left outside the audio panel
# come along with the rest.
GROUPS = ["VideoParent", "AudioParent", "ScrollParent", "NetworkParent"]
LOOSE = ["SliderMusicVolume", "SliderSFXVolume"]

# The advanced display popup.  Every setting in it is a graphics setting and it opened only when
# Custom was picked in a combo box on the display page, so a player choosing High never saw what
# High turns on.  Its controls move onto the Graphics page; the popup, its two buttons, its heading,
# its rules and its captions go.
ADVANCED = "WinAdvancedDisplayOptions"
GRAPHICS_CHECKS = [
    "Check3DShadows", "Check2DShadows", "CheckCloudShadows", "CheckGroundLighting",
    "CheckSmoothWater", "CheckShowProps", "CheckExtraAnimations", "CheckHeatEffects",
    "CheckBehindBuilding", "CheckNoDynamicLOD",
]
ADVANCED_KEEP = GRAPHICS_CHECKS + ["CheckUnlockFPS", "LowResSlider", "ParticleCapSlider",
                                   "LabelTextureResolution", "LabelParticleCap"]

# Check boxes of the fork's that OptionsMenu.cpp fills in by name rather than through the catalog:
# swaying trees are a GameLOD.ini field like the popup's boxes, so they save under Custom with them.
MENU_CHECKS = ["CheckTreeSway"]

# Combo boxes OptionsMenu.cpp fills in by name for the same reason: the monitors on the desktop are
# not a range a catalog row can describe.
MENU_COMBOS = ["ComboBoxMonitor"]

# The catalog's shadow rows, which stood in GameData.ini with no control until the Effects page.
SHADOW_CHECKS = ["Check3DShadows", "Check2DShadows", "CheckInfantryShadows",
                 "CheckProjectileShadows", "CheckPropShadows", "CheckParticleShadows"]

# The tab a page opens is already captioned with the page's name, so EA's caption inside the panel
# says the same word a second time, and the rule under it then divides nothing from nothing.  Both
# go: the headings are unnamed statics inside the four group panels, found by the string they draw,
# and the four rules are loose children of the old parent.
HEADINGS = ["GUI:DisplayOptions", "GUI:AudioOptions", "GUI:ControlOptions", "GUI:NetworkOptions"]
RULES = ["Line1", "Line2", "Line3", "Line4"]

# The keyboard button opens a screen that no longer exists, so it goes out here rather than being
# deleted from the output by hand every time this runs.  It shipped HIDDEN and off the right edge
# with nothing behind it: the layout its code wanted was never in any .big.
DROP = ["ButtonKeyboardOptions"]

# Controls EA left unnamed that still have to be positioned, found by what they draw and given a
# name on the way through.
NAME_THE_UNNAMED = [
    ("GUI:AntiAliasing", "AntiAliasingLabel"),
    ("GUI:LowResSlider", "LabelTextureResolution"),
    ("GUI:ParticleCap", "LabelParticleCap"),
    ("GUI:Options", "LabelTitle"),
]

# Controls that are in the shipped file and are not wanted at all.  CheckAlternateMouse chose
# between the classic mouse and the alternate one; ComboBoxInputScheme is that choice now, and it
# takes the keyboard along with the mouse.
DELETE = ["CheckAlternateMouse"]

# The templates new controls are cloned from, and whose lettering the moved ones take.
CHECK, LABEL, COMBO, SLIDER = "Retaliation", "DetailLabel", "ComboBoxDetail", "SliderGamma"

# The fork's own controls.  Where they stand is decided below with everything else.
#   (template, name, text key)
NEW_CONTROLS = [
    (LABEL,  "LabelMonitor",           "GUI:Monitor"),
    (COMBO,  "ComboBoxMonitor",        None),
    (LABEL,  "LabelWindowMode",        "GUI:WindowMode"),
    (COMBO,  "ComboBoxWindowMode",     None),
    (CHECK,  "CheckVSync",             "GUI:VSync"),
    (CHECK,  "CheckClassicGraphics",   "GUI:ClassicGraphics"),
    (LABEL,  "LabelMSAA",              "GUI:MSAA"),
    (COMBO,  "ComboBoxMSAA",           None),
    (LABEL,  "LabelBloom",             "GUI:Bloom"),
    (COMBO,  "ComboBoxBloom",          None),
    (LABEL,  "LabelBloomThreshold",    "GUI:BloomThreshold"),
    (COMBO,  "ComboBoxBloomThreshold", None),
    (LABEL,  "LabelTextureFilter",     "GUI:TextureFilter"),
    (COMBO,  "ComboBoxTextureFilter",  None),
    (LABEL,  "LabelAnisotropy",        "GUI:Anisotropy"),
    (SLIDER, "SliderAnisotropy",       None),
    (LABEL,  "LabelHealthBars",        "GUI:HealthBars"),
    (COMBO,  "ComboBoxHealthBars",     None),
    (LABEL,  "LabelPlayerColors",      "GUI:PlayerColors"),
    (COMBO,  "ComboBoxPlayerColors",   None),
    (LABEL,  "LabelLanguage",          "GUI:Language"),
    (COMBO,  "ComboBoxLanguage",       None),
    (CHECK,  "CheckOrderLines",        "GUI:OrderLines"),
    (CHECK,  "CheckZoomToCursor",      "GUI:ZoomToCursor"),
    (CHECK,  "CheckIsometricCamera",   "GUI:IsometricCamera"),
    (CHECK,  "CheckTreeSway",          "GUI:TreeSway"),
    (CHECK,  "CheckInfantryShadows",   "GUI:InfantryShadows"),
    (CHECK,  "CheckProjectileShadows", "GUI:ProjectileShadows"),
    (CHECK,  "CheckPropShadows",       "GUI:PropShadows"),
    (CHECK,  "CheckParticleShadows",   "GUI:ParticleShadows"),
    (LABEL,  "LabelSmoke",             "GUI:Smoke"),
    (COMBO,  "ComboBoxSmoke",          None),
    (CHECK,  "CheckParticleBounce",    "GUI:ParticleBounce"),
    (LABEL,  "LabelInputScheme",       "GUI:InputScheme"),
    (COMBO,  "ComboBoxInputScheme",    None),
    (CHECK,  "CheckWasdCamera",        "GUI:WasdCamera"),
    (CHECK,  "CheckChromaLighting",    "GUI:ChromaLighting"),
]

# A slider on its own says nothing about where it stands, so each one has a readout beside it that
# OptionsMenu.cpp writes.  Cloned from a label with its caption and tooltip taken off.
READOUTS = [
    "ValueGamma", "ValueTextureResolution", "ValueParticleCap", "ValueAnisotropy",
    "ValueMusicVolume", "ValueSFXVolume", "ValueVoiceVolume", "ValueScrollSpeed",
]

# a cloned slider keeps its template's range unless it is given one
SLIDER_RANGES = [("SliderAnisotropy", 0, 16)]

# EA's captions that do not fit the page: two popup headings written in capitals, and a check box
# caption that ran 20 pixels past the panel's right edge once it stood in a 268 pixel column.
#   (control, text key)
TEXT_OVERRIDES = [
    ("LabelTextureResolution", "GUI:TextureResolution"),
    ("LabelParticleCap", "GUI:ParticleLimit"),
    ("CheckNoDynamicLOD", "GUI:NeverLowerDetail"),
]

# Three titled groups across every page, one spacing scale: 4 8 16 24 32.  Columns 224 wide, 24
# apart, 4 in from the page edge; a group heading on 150 and its content from 182.  A setting is its
# label over its control, pitch 56, and a slider stops at 136 with its readout beside it on the same
# row; a check box takes 28, a button 32.  Grouping follows what a setting does rather than where EA
# put it: on Graphics the preset and the two sliders it sets, the picture settings, then the terrain;
# on Effects the shadows, the extra touches, then smoke and particles.
COLUMNS = (40, 288, 536)
COLUMN_WIDTH = 224
HEADING_TOP, CONTENT_TOP = 150, 182
ROW_HEIGHT, SETTING_PITCH, CHECK_PITCH, BUTTON_PITCH = 24, 56, 28, 32
READOUT_LEFT, READOUT_WIDTH = 144, 80
# A static text draws its first glyph 6px inside its rectangle, so labels, headings and readouts start
# 6px left of the edge the boxes and check glyphs share, and their text lands on it.  A readout still
# sits a few pixels below its slider's bar - the bar is drawn in the top of the slider's rectangle -
# and lifting it would push its rectangle into the label above, which the overlap check refuses.
TEXT_NUDGE = 6
GROUP_HEADING_COLOR = ("ENABLED:  186 255 12 255, ENABLEDBORDER:  0 2 0 255, "
                       "DISABLED: 186 255 12 255, DISABLEDBORDER: 0 2 0 255, "
                       "HILITE:   186 255 12 255, HILITEBORDER:   0 2 0 255")
# The open tab is the disabled button, and EA drew its caption 62,64,92 on the dark panel - about
# 2:1, and it read as a tab that could not be pressed.  It takes the heading green; the others stay white.
TAB_CAPTION_COLOR = ("ENABLED:  255 255 255 255, ENABLEDBORDER:  0 0 0 255, "
                     "DISABLED: 186 255 12 255, DISABLEDBORDER: 0 2 0 255, "
                     "HILITE:   186 255 12 255, HILITEBORDER:   0 2 0 255")


def setting(label, control, readout=None):
    return ("setting", label, control, readout)


#   (page, column, heading key, items)
GROUP_LAYOUT = [
    ("PageDisplay",  0, "GUI:OptionsGroupScreen", [
        setting("LabelMonitor", "ComboBoxMonitor"),
        setting("ResolutionLabel", "ComboBoxResolution"),
        setting("LabelWindowMode", "ComboBoxWindowMode"),
        ("check", "CheckVSync")]),
    ("PageDisplay",  1, "GUI:OptionsGroupPicture", [
        setting("GammaLabel", "SliderGamma", "ValueGamma")]),

    ("PageGraphics", 0, "GUI:OptionsGroupDetail", [
        ("check", "CheckClassicGraphics"),
        setting("DetailLabel", "ComboBoxDetail"),
        setting("LabelTextureResolution", "LowResSlider", "ValueTextureResolution"),
        setting("LabelParticleCap", "ParticleCapSlider", "ValueParticleCap")]),
    ("PageGraphics", 1, "GUI:OptionsGroupImage", [
        setting("LabelMSAA", "ComboBoxMSAA"),
        setting("LabelBloom", "ComboBoxBloom"),
        setting("LabelBloomThreshold", "ComboBoxBloomThreshold"),
        setting("LabelTextureFilter", "ComboBoxTextureFilter"),
        setting("LabelAnisotropy", "SliderAnisotropy", "ValueAnisotropy")]),
    ("PageGraphics", 2, "GUI:OptionsGroupTerrain", [
        ("check", "CheckCloudShadows"),
        ("check", "CheckGroundLighting"),
        ("check", "CheckSmoothWater"),
        ("check", "CheckShowProps")]),

    ("PageEffects",  0, "GUI:OptionsGroupShadows", [("check", name) for name in SHADOW_CHECKS]),
    ("PageEffects",  1, "GUI:OptionsGroupExtras", [
        ("check", "CheckExtraAnimations"),
        ("check", "CheckTreeSway"),
        ("check", "CheckHeatEffects"),
        ("check", "CheckBehindBuilding")]),
    ("PageEffects",  2, "GUI:OptionsGroupParticles", [
        setting("LabelSmoke", "ComboBoxSmoke"),
        ("check", "CheckParticleBounce"),
        ("check", "CheckNoDynamicLOD")]),

    ("PageAudio",    0, "GUI:OptionsGroupVolume", [
        setting("MusicVolumeLabel", "SliderMusicVolume", "ValueMusicVolume"),
        setting("SFXVolumeLabel", "SliderSFXVolume", "ValueSFXVolume"),
        setting("VoiceVolumeLabel", "SliderVoiceVolume", "ValueVoiceVolume")]),

    ("PageControls", 0, "GUI:OptionsGroupScrolling", [
        setting("ScrollSpeedLabel", "SliderScrollSpeed", "ValueScrollSpeed"),
        ("check", "CheckZoomToCursor"),
        ("check", "CheckIsometricCamera")]),
    ("PageControls", 1, "GUI:OptionsGroupOrders", [
        ("check", "Retaliation"),
        ("check", "CheckDoubleClickAttackMove")]),
    ("PageControls", 2, "GUI:OptionsGroupInput", [
        setting("LabelInputScheme", "ComboBoxInputScheme"),
        ("check", "CheckWasdCamera"),
        ("check", "CheckChromaLighting")]),

    ("PageGameplay", 0, "GUI:OptionsGroupBattlefield", [
        setting("LabelHealthBars", "ComboBoxHealthBars"),
        setting("LabelPlayerColors", "ComboBoxPlayerColors"),
        ("check", "CheckOrderLines")]),
    ("PageGameplay", 1, "GUI:OptionsGroupLanguage", [
        setting("LabelLanguage", "ComboBoxLanguage")]),

    ("PageNetwork",  0, "GUI:OptionsGroupAddresses", [
        setting("StaticTextOnlineIpAddresses", "ComboBoxOnlineIP"),
        setting("StaticTextLANIpAddresses", "ComboBoxIP")]),
    ("PageNetwork",  1, "GUI:OptionsGroupFirewall", [
        setting("StaticTextFirewallPortOverride", "TextEntryFirewallPortOverride"),
        ("button", "ButtonFirewallRefresh"),
        ("check", "CheckSendDelay")]),
    ("PageNetwork",  2, "GUI:OptionsGroupProxy", [
        setting("StaticTextHTTPProxy", "TextEntryHTTPProxy")]),
]

# EA controls that ship hidden and stay hidden, parked on a page rather than off the panel
#   (page, name, column, top)
HIDDEN_PARKED = [
    ("PageDisplay",  "AntiAliasingLabel",    2, CONTENT_TOP),
    ("PageDisplay",  "ComboBoxAntiAliasing", 2, CONTENT_TOP + ROW_HEIGHT),
    ("PageGraphics", "CheckUnlockFPS",       0, CONTENT_TOP + 3 * SETTING_PITCH),
]


def _named(name):
    return "OptionsMenu.wnd:%s" % name


def detach(parent, name):
    """Take one child out of parent's list and return it."""
    for i, child in enumerate(parent.children):
        if (child.name or "").split(":")[-1] == name:
            return parent.children.pop(i)
    raise KeyError(name)


def short(window):
    return (window.name or "").split(":")[-1]


def statement_end(props, start):
    end = start
    while ";" not in props[end]:
        end += 1
    return end


def drop_prop(window, key):
    """Take a statement out of a window, if it has one."""
    start = window.prop_index(key)
    if start >= 0:
        del window.props[start:statement_end(window.props, start) + 1]


def restyle(window, template, keys=("FONT", "HEADERTEMPLATE", "TEXTCOLOR")):
    """Give a control the lettering of the template.  The popup's check boxes were 10 point and the
    page's 14, and the network labels were 10 point beside 14 point labels on every other page; a
    column of settings in two type sizes does not read as one column."""
    for key in keys:
        source, target = template.prop_index(key), window.prop_index(key)
        if source < 0 or target < 0:
            continue
        lines = template.props[source:statement_end(template.props, source) + 1]
        moved = [window.indent + line[len(template.indent):] if line.startswith(template.indent)
                 else line for line in lines]
        window.props[target:statement_end(window.props, target) + 1] = moved


def make_page(video_parent, name):
    """An empty container the size of the page area.

    Cloned from VideoParent for one property: SYSTEMCALLBACK is PassMessagesToParentSystem, without
    which a click on anything inside the page stops at the page and never reaches OptionsMenuSystem.
    SEE_THRU then keeps it from drawing over the panel art behind it."""
    page = clone(video_parent, _named(name))
    page.children = []
    page.place(PAGE[0], PAGE[1], PAGE[2], PAGE[3])
    page.set_prop("STATUS", "ENABLED+NOFOCUS+SEE_THRU")
    return page


def make_tab(button_template, name, text, index):
    """The tabs share the inner width; the last takes the pixels the division leaves over, so the
    strip ends exactly where the pages and the buttons end."""
    width = (INNER_WIDTH - (len(TABS) - 1) * TAB_GAP) // len(TABS)
    left = INNER_LEFT + index * (width + TAB_GAP)
    if index == len(TABS) - 1:
        width = INNER_LEFT + INNER_WIDTH - left
    tab = clone(button_template, _named(name))
    tab.place(left, TAB_TOP, width, TAB_HEIGHT)
    tab.put_prop("TEXT", '"%s"' % text)
    # the Defaults button's tooltip came along with the clone and said "reset" over every tab
    drop_prop(tab, "TOOLTIPTEXT")
    tab.set_prop("TEXTCOLOR", TAB_CAPTION_COLOR)
    return tab


def make_readout(label_template, name):
    readout = clone(label_template, _named(name))
    readout.children = []
    drop_prop(readout, "TEXT")
    drop_prop(readout, "TOOLTIPTEXT")
    readout.put_prop("STATICTEXTDATA", "CENTERED: 0")
    return readout


def setting_of(name):
    """The setting a control belongs to, which is its name without the kind in front.  A label and
    the combo box under it are two controls for one setting and share one tooltip string."""
    for prefix in ("ComboBox", "Slider", "Check", "Label"):
        if name.startswith(prefix):
            return name[len(prefix):]
    return name


def make_control(template, name, text, left, top, width, height):
    control = clone(template, _named(name))
    control.children = []
    control.place(left, top, width, height)
    if text is not None:
        control.put_prop("TEXT", '"%s"' % text)
    control.put_prop("TOOLTIPTEXT", '"TOOLTIP:%s"' % setting_of(name))
    # a label wide enough to read is a label that starts at the left, not one centred in 230 pixels
    if control.prop("STATICTEXTDATA") is not None:
        control.set_prop("STATICTEXTDATA", "CENTERED: 0")
    return control


def drawn_text(window):
    """The TEXT a window draws, or None.  Used to reach the controls EA left unnamed."""
    statement = window.prop("TEXT")
    if statement is None:
        return None
    match = re.search(r'"([^"]*)"', statement)
    return match.group(1) if match else None


def drop_by_name(root, names):
    """Delete every window with one of these names, wherever it sits."""
    wanted = set(names)
    for node in list(root.walk()):
        node.children = [child for child in node.children
                         if (child.name or "").split(":")[-1] not in wanted]


def drop_by_text(root, texts):
    """Delete every window drawing one of these strings, wherever it sits."""
    wanted = set(texts)
    for node in list(root.walk()):
        node.children = [child for child in node.children
                         if drawn_text(child) not in wanted]


def build(layout):
    old = layout.find("OptionsMenuParentOld")
    templates = dict((name, layout.find(name))
                     for name in (CHECK, LABEL, COMBO, SLIDER, "VideoParent", "ButtonDefaults"))

    for text, name in NAME_THE_UNNAMED:
        for node in layout.root.walk():
            # "unnamed" in this file means NAME = "OptionsMenu.wnd:", the layout and nothing after it
            if drawn_text(node) == text and not (node.name or "").split(":")[-1]:
                node.name = _named(name)

    drop_by_text(layout.root, HEADINGS)
    drop_by_name(layout.root, DELETE)
    for name in RULES + DROP:
        detach(old, name)

    # every control that is going onto a page, by name: out of EA's group panels, off the old
    # parent, out of the advanced popup, and new
    waiting = {}
    for group_name in GROUPS:
        for child in detach(old, group_name).children:
            waiting[short(child)] = child
    for name in LOOSE:
        waiting[name] = detach(old, name)
    for node in detach(old, ADVANCED).walk():
        if short(node) in ADVANCED_KEEP:
            waiting[short(node)] = node
    for template, name, text in NEW_CONTROLS:
        waiting[name] = make_control(templates[template], name, text, 0, 0, 1, 1)
    for name in READOUTS:
        waiting[name] = make_readout(templates[LABEL], name)
    for name, low, high in SLIDER_RANGES:
        waiting[name].put_prop("SLIDERDATA", "MINVALUE: %d, MAXVALUE: %d" % (low, high))
    for name, key in TEXT_OVERRIDES:
        waiting[name].put_prop("TEXT", '"%s"' % key)

    pages = dict((page_name, make_page(templates["VideoParent"], page_name))
                 for page_name, _tab, _text in TABS)

    def put(page_name, name, left, top, width, height, template=None):
        control = waiting.pop(name)
        control.place(left, top, width, height)
        if template is not None:
            restyle(control, templates[template])
        if template == LABEL:
            # a label reads from the left edge its control starts at, not from the middle of 268 pixels
            control.put_prop("STATICTEXTDATA", "CENTERED: 0")
        pages[page_name].children.append(control)

    for page_name, column, heading_key, items in GROUP_LAYOUT:
        left = COLUMNS[column]
        heading_name = "GroupHeading%s%d" % (page_name[len("Page"):], column)
        heading = clone(templates[LABEL], _named(heading_name))
        heading.children = []
        heading.put_prop("TEXT", '"%s"' % heading_key)
        drop_prop(heading, "TOOLTIPTEXT")
        restyle(heading, templates["ButtonDefaults"], keys=("FONT", "HEADERTEMPLATE"))
        heading.set_prop("TEXTCOLOR", GROUP_HEADING_COLOR)
        heading.put_prop("STATICTEXTDATA", "CENTERED: 0")
        waiting[heading_name] = heading
        put(page_name, heading_name, left - TEXT_NUDGE, HEADING_TOP, COLUMN_WIDTH, ROW_HEIGHT)

        top = CONTENT_TOP
        for item in items:
            if item[0] == "setting":
                _kind, label, control, readout = item
                put(page_name, label, left - TEXT_NUDGE, top, COLUMN_WIDTH, ROW_HEIGHT, LABEL)
                if readout:
                    put(page_name, readout, left + READOUT_LEFT - TEXT_NUDGE, top + ROW_HEIGHT,
                        READOUT_WIDTH, ROW_HEIGHT, LABEL)
                put(page_name, control, left, top + ROW_HEIGHT,
                    READOUT_LEFT - 8 if readout else COLUMN_WIDTH, ROW_HEIGHT)
                top += SETTING_PITCH
            elif item[0] == "check":
                put(page_name, item[1], left, top, COLUMN_WIDTH, ROW_HEIGHT, CHECK)
                top += CHECK_PITCH
            else:
                put(page_name, item[1], left, top, 160, ROW_HEIGHT)
                top += BUTTON_PITCH
    for page_name, name, column, top in HIDDEN_PARKED:
        put(page_name, name, COLUMNS[column], top, COLUMN_WIDTH, ROW_HEIGHT)
    if waiting:
        raise KeyError("given no place on a page: %s" % ", ".join(sorted(waiting)))

    # the frame round the pages, on the same inner edge; the routine pair on the right, Defaults
    # alone on the left where a slip of the pointer cannot reach it from Accept
    layout.find("OptionsMenuParent").place(*PANEL)
    old.place(*PANEL)
    layout.find("LabelTitle").place(*TITLE)
    layout.find("Line").place(*RULE)
    right = INNER_LEFT + INNER_WIDTH
    layout.find("ButtonDefaults").place(INNER_LEFT, BUTTON_TOP, BUTTON_WIDTH, BUTTON_HEIGHT)
    layout.find("ButtonAccept").place(right - BUTTON_WIDTH, BUTTON_TOP, BUTTON_WIDTH, BUTTON_HEIGHT)
    layout.find("ButtonBack").place(right - 2 * BUTTON_WIDTH - 16, BUTTON_TOP, BUTTON_WIDTH, BUTTON_HEIGHT)
    layout.find("LabelVersion").place(*VERSION)

    # last in the file is topmost: drawWindow walks the child list from the tail back to the head,
    # so the tabs go on after the old parent's own children and the pages after the tabs
    for index, (page_name, tab_name, text) in enumerate(TABS):
        old.children.append(make_tab(templates["ButtonDefaults"], tab_name, text, index))
    for page_name, _tab, _text in TABS:
        old.children.append(pages[page_name])

    return layout


# ---------------------------------------------------------------------------
# selfcheck: the three files have to agree
#
# TheOptionCatalog names a widget, the layout has to carry a control with that name, and both the
# label and the tooltip have to be in Patch.str or the screen draws a row of raw key names.  Nothing
# at build time notices any of that - a typo in a widget name just means the control is silently
# never filled in - so it is checked here, against the tracked files.
# ---------------------------------------------------------------------------

_HERE = os.path.dirname(os.path.abspath(__file__))
_CODE = os.path.dirname(_HERE)

CATALOG = os.path.join(_CODE, "GameEngine", "Source", "Common", "OptionsCatalog.cpp")
STRINGS = os.path.join(_CODE, "Data", "Patch.str")
LAYOUT = os.path.join(_CODE, "Data", "Window", "Menus", "OptionsMenu.wnd")
INCLUDE = os.path.join(_CODE, "GameEngine", "Include")

_ROW = re.compile(
    r'\{\s*"(?P<ini>[^"]+)",\s*'
    r'(?:OPT_WND\(\s*"(?P<widget>[^"]+)"\s*\)|"")\s*,\s*'
    r'"(?P<label>[^"]*)",\s*'
    r'(?P<kind>OPTION_\w+),\s*APPLY_\w+,\s*(?P<lo>[^,]+?),\s*(?P<hi>[^,]+?),')


def read_catalog():
    with open(CATALOG, "rb") as fp:
        text = fp.read().decode("latin-1")
    return [match.groupdict() for match in _ROW.finditer(text)]


def read_strings():
    """The keys defined in Patch.str.  A key is a line of its own, the value is the quoted line
    under it, and '//' is the only comment GameText.cpp's parseStringFile knows."""
    keys = set()
    with open(STRINGS, "rb") as fp:
        for line in fp.read().decode("latin-1").splitlines():
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def enum_count(name):
    """The value of an enum constant like WINDOW_MODE_COUNT, out of whichever header declares it."""
    pattern = re.compile(r"\b%s\s*=\s*(\d+)" % re.escape(name))
    for root, _dirs, files in os.walk(INCLUDE):
        for filename in files:
            if not filename.endswith(".h"):
                continue
            with open(os.path.join(root, filename), "rb") as fp:
                match = pattern.search(fp.read().decode("latin-1"))
            if match:
                return int(match.group(1))
    return None


def overlaps(layout):
    """Two visible controls on one page drawn over each other.  Nothing at build time notices that,
    and on a page laid out by arithmetic it is the one mistake arithmetic makes."""
    found = []
    for page_name, _tab, _text in TABS:
        page = layout.find(page_name)
        if page is None:
            found.append("OptionsMenu.wnd has no %s" % page_name)
            continue
        visible = [(short(child), child.rect[:4]) for child in page.children
                   if "HIDDEN" not in (child.prop("STATUS") or "")]
        for index, (name, (left, top, right, bottom)) in enumerate(visible):
            for other, (other_left, other_top, other_right, other_bottom) in visible[index + 1:]:
                if left < other_right and other_left < right and top < other_bottom and other_top < bottom:
                    found.append("%s: %s and %s overlap" % (page_name, name, other))
    return found


def selfcheck():
    rows = read_catalog()
    keys = read_strings()
    layout = wndlayout.load(LAYOUT)
    controls = set((node.name or "").split(":")[-1] for node in layout.root.walk())

    problems = []

    if len(rows) < 9:
        problems.append("only %d catalog rows parsed, the regex has stopped matching" % len(rows))

    for _page, _tab, text in TABS:
        if text not in keys:
            problems.append("tab caption %s is not in Patch.str" % text)

    for row in rows:
        widget, label = row["widget"], row["label"]
        if widget is None:
            # a setting with no control yet is allowed, but then it has no label either
            if label:
                problems.append("%s has a label key and no widget" % row["ini"])
            continue

        if widget not in controls:
            problems.append("%s names %s, which is not in OptionsMenu.wnd" % (row["ini"], widget))
        if not label:
            problems.append("%s has a widget and no label key" % row["ini"])
            continue
        if label not in keys:
            problems.append("%s label %s is not in Patch.str" % (row["ini"], label))

        tooltip = "TOOLTIP:%s" % setting_of(widget)
        if tooltip not in keys:
            problems.append("%s tooltip %s is not in Patch.str" % (row["ini"], tooltip))

        if row["kind"] == "OPTION_ENUM":
            constant = row["hi"].split()[0]
            count = enum_count(constant)
            if count is None:
                problems.append("%s: no header declares %s" % (row["ini"], constant))
                continue
            for entry in range(int(row["lo"]), count):
                if "%s%d" % (label, entry) not in keys:
                    problems.append("%s entry %s%d is not in Patch.str" % (row["ini"], label, entry))

    for _control, key in TEXT_OVERRIDES:
        if key not in keys:
            problems.append("caption %s is not in Patch.str" % key)

    for name in READOUTS + GRAPHICS_CHECKS + MENU_CHECKS + MENU_COMBOS:
        if name not in controls:
            problems.append("OptionsMenu.wnd has no %s, which OptionsMenu.cpp fills in" % name)
    for name in MENU_COMBOS:
        for key in ("GUI:%s" % setting_of(name), "TOOLTIP:%s" % setting_of(name)):
            if key not in keys:
                problems.append("%s needs %s in Patch.str" % (name, key))
    if ADVANCED in controls:
        problems.append("OptionsMenu.wnd still carries %s; its controls are on the Graphics page"
                        % ADVANCED)
    problems.extend(overlaps(layout))

    for problem in problems:
        print("optionsmenu: %s" % problem)
    if problems:
        return 1

    print("optionsmenu: %d catalog rows agree with %s and %s"
          % (len(rows), os.path.basename(LAYOUT), os.path.basename(STRINGS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()

    if len(argv) != 3:
        print(__doc__)
        return 2

    layout = wndlayout.load(argv[1])
    wndlayout.save(build(layout), argv[2])
    print("%s: %d windows" % (argv[2], sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
