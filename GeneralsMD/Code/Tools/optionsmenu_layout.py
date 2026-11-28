#!/usr/bin/env python3
"""Lay the options screen out in the menu language, authored at 1920x1080.

The design is experiments/proofs/2026-11-24/screens/settings/README-settings.md in the wrapper
(menu package 8 of docs/plans/MENU-REDESIGN.md): frame A's panel on 32,28 to 1888,1052, a rail of
eight tabs down the left with Look and Quality under them, one column of rows in the middle and the
pane on the right that explains whatever row the pointer or the focus is on.

Eight pages: System, Graphics, World, Effects, Audio, Controls, Interface, Hotkeys.  Each is a list of
groups, each group a head and its rows, a group's first rows before its second-tier ones, the way the
plan orders them.  A row is a container window named Row<Stem> holding its label and its control, so the code moves, hides and lights a row as one; its TEXT is the
label key the pane names it by, and OPTIONTIP:<Stem> in Patch.str is the pane's two sentences.  The
Audio page's five volumes are faders, Fader<Stem>, drawn by the code over the five sliders it keeps
hidden.  Everything that is a line, a frame, a state or a figure is drawn by OptionsMenu.cpp; this
file holds where the windows stand and which ones exist.

Every row of a page stands in the column; there is no fold (the owner, 2026-11-27: scroll, not More).
The rows of this file stand as the screen opens, a long page's last rows past the body's foot.
OptionsMenu.cpp lays a page out again whenever a row goes (a second monitor unplugged, a borderless
window owning edge scrolling) or the page scrolls under the wheel, taking the rows in the order of
their top edges here.

    python optionsmenu_layout.py <OptionsMenuClassic.wnd in> <out>

The input is the tracked master (Shell.cpp opens OptionsMenuClassic.wnd), at 800x600 before package
8 and at 1920x1080 since; a layout still at 800x600 is first scaled to the 1920x1080 grid.  Running
it on its own output changes nothing.  The retail OptionsMenu.wnd in WindowZH.big was the input of
the tabbed layout before this one (git history of this file).

    python optionsmenu_layout.py selfcheck

reads the tracked files back and checks they agree: every widget TheOptionCatalog names is in the
layout, every row has its pane sentences in English and Turkish, every window is authored at
1920x1080, no two visible windows in one container overlap, and the script reproduces the tracked
file byte for byte.  Run by CTest as
optionsmenu_selfcheck.  Layouts are not in the multiplayer INI checksum.
"""

import os
import re
import sys

import wndlayout
from wndlayout import clone

WIDE = (1920, 1080)

# frame A and the rects of README-settings.md, "Rects at 1920x1080"
PANEL = (32, 28, 1888, 1052)
TITLE = (60, 46, 660, 94)
VERSION = (1260, 46, 1860, 94)
RULE = (34, 110, 1886, 112)
BODY_TOP, BODY_BOTTOM = 132, 954
RAIL_LEFT, RAIL_RIGHT = 60, 340
TAB_HEIGHT, TAB_PITCH = 72, 80
# 40 under the last tab and 40 again to the quality track, Look's and Quality's captions in that air,
# so every gap in the rail is on the 8 px grid
LOOK_TOP, LOOK_BOTTOM = 804, 840
LOOK_BOXES = ((60, 196), (204, 340))
QUALITY_SLIDER = (60, 880, 340, 904)
ROWS_LEFT, ROWS_RIGHT = 372, 1124
LABEL_LEFT, LABEL_RIGHT = 388, 816
CONTROL_LEFT, CONTROL_RIGHT = 824, 1114
SLIDER_LEFT, SLIDER_RIGHT = 828, 1028
READOUT_LEFT = 1044
ROW_HEIGHT, HEAD_HEIGHT, GROUP_GAP = 34, 34, 4
BUTTON_ROW_HEIGHT = 48
FADER_MIN = 330
PANE = (1156, 132, 1860, 954)
KEYS_TOP, KEYS_BOTTOM = 980, 1032
DEFAULTS_KEY = (60, 340)
BACK_KEY = (1296, 1536)
ACCEPT_KEY = (1560, 1860)

#   (page, tab, tab caption)
TABS = [
    ("PageSystem",    "TabSystem",    "GUI:OptionsTabSystem"),
    ("PageGraphics",  "TabGraphics",  "GUI:OptionsTabGraphics"),
    ("PageWorld",     "TabWorld",     "GUI:OptionsTabWorld"),
    ("PageEffects",   "TabEffects",   "GUI:OptionsTabEffects"),
    ("PageAudio",     "TabAudio",     "GUI:OptionsTabAudio"),
    ("PageControls",  "TabControls",  "GUI:OptionsTabControls"),
    ("PageInterface", "TabInterface", "GUI:OptionsTabInterface"),
    ("PageHotkeys",   "TabHotkeys",   "GUI:OptionsTabHotkeys"),
]
# The rail's line under each tab's name is its group heads joined, which the code shortens (Frame
# rate to FPS, GUI:OptionsHeadFrameRate_Short) and then drops the last of; it needs no key of its own.


# A row, by kind:
#   check   (stem, "check", check box)                     the box is the whole row; its caption the label
#   combo   (stem, "combo", label, combo box)
#   entry   (stem, "entry", label, text entry)
#   slider  (stem, "slider", label, slider, readout)
#   button  (stem, "button", push button)                  the connection test
#   fader   (stem, "fader", label, slider, readout)        Audio's volumes, drawn upright by the code
# with ("more",) appended for a row of the plan's second tier, which stood behind the More fold until
# 2026-11-27 and now only marks the order: it stands after its group's first rows.
def check(stem, name, *more):
    return (stem, "check", name) + more


def combo(stem, label, name, *more):
    return (stem, "combo", label, name) + more


def entry(stem, label, name, *more):
    return (stem, "entry", label, name) + more


def slider(stem, label, name, readout, *more):
    return (stem, "slider", label, name, readout) + more


MORE = "more"

#   (page, [(head key, [rows])]) in the plan's order: SETTINGS-PLAN.md, "The new order", with the
#   eight tabs and groups of settings.py's TABS
GROUP_LAYOUT = [
    ("PageSystem", [
        ("GUI:OptionsHeadScreen", [
            combo("Resolution", "ResolutionLabel", "ComboBoxResolution"),
            combo("WindowMode", "LabelWindowMode", "ComboBoxWindowMode"),
            combo("Monitor", "LabelMonitor", "ComboBoxMonitor"),
            slider("Gamma", "GammaLabel", "SliderGamma", "ValueGamma"),
            combo("FullscreenScaling", "LabelFullscreenScaling", "ComboBoxFullscreenScaling", MORE)]),
        ("GUI:OptionsHeadFrameRate", [
            combo("FrameRateLimit", "LabelFrameRateLimit", "ComboBoxFrameRateLimit"),
            check("VSync", "CheckVSync"),
            check("SmoothMotion", "CheckSmoothMotion", MORE)]),
        ("GUI:OptionsHeadNetwork", [
            combo("LANIP", "StaticTextLANIpAddresses", "ComboBoxIP"),
            entry("FirewallPortOverride", "StaticTextFirewallPortOverride", "TextEntryFirewallPortOverride"),
            combo("OnlineIP", "StaticTextOnlineIpAddresses", "ComboBoxOnlineIP", MORE),
            check("SendDelay", "CheckSendDelay", MORE),
            entry("HTTPProxy", "StaticTextHTTPProxy", "TextEntryHTTPProxy", MORE),
            ("ConnectionTest", "button", "ButtonFirewallRefresh")])]),

    ("PageGraphics", [
        ("GUI:OptionsHeadShadows", [
            check("CastShadows", "Check3DShadows"),
            combo("ShadowQuality", "LabelShadowQuality", "ComboBoxShadowQuality"),
            check("FlatShadows", "Check2DShadows", MORE),
            check("InfantryShadows", "CheckInfantryShadows", MORE),
            check("ProjectileShadows", "CheckProjectileShadows", MORE),
            check("PropShadows", "CheckPropShadows", MORE),
            check("ParticleShadows", "CheckParticleShadows", MORE),
            check("VolumetricSmoke", "CheckVolumetricSmoke", MORE),
            check("CloudShadows", "CheckCloudShadows", MORE)]),
        ("GUI:OptionsHeadDetail", [
            slider("TextureResolution", "LabelTextureResolution", "LowResSlider", "ValueTextureResolution"),
            combo("MSAA", "LabelMSAA", "ComboBoxMSAA"),
            check("HDTextures", "CheckHDTextures"),
            check("DetailedModels", "CheckDetailedModels"),
            combo("TextureFiltering", "LabelTextureFiltering", "ComboBoxTextureFiltering", MORE),
            check("KeepDetail", "CheckNoDynamicLOD", MORE),
            check("FullDetailAtDistance", "CheckFullDetailAtDistance", MORE)])]),

    ("PageWorld", [
        ("GUI:OptionsHeadLight", [
            combo("LightingStyle", "LabelLightingStyle", "ComboBoxLightingStyle"),
            combo("Bloom", "LabelBloom", "ComboBoxBloom"),
            check("MapLight", "CheckMapLight"),
            combo("SunShafts", "LabelSunShafts", "ComboBoxSunShafts"),
            check("AmbientOcclusion", "CheckAmbientOcclusion", MORE),
            check("HemiAmbient", "CheckHemiAmbient", MORE),
            check("InternalHdr", "CheckInternalHdr", MORE),
            check("TeamGlow", "CheckTeamGlow", MORE)]),
        ("GUI:OptionsHeadSky", [
            check("Atmosphere", "CheckAtmosphere"),
            check("Weather", "CheckWeather")]),
        ("GUI:OptionsHeadGround", [
            check("ShowProps", "CheckShowProps"),
            check("GroundFoliage", "CheckGroundFoliage"),
            check("TerrainRelief", "CheckTerrainRelief"),
            check("GroundLighting", "CheckGroundLighting", MORE),
            check("TreeSway", "CheckTreeSway", MORE)]),
        ("GUI:OptionsHeadWater", [
            combo("WaterStyle", "LabelWaterStyle", "ComboBoxWaterStyle"),
            check("SmoothWater", "CheckSmoothWater", MORE),
            check("WaterFoam", "CheckWaterFoam", MORE),
            check("WaterSplashes", "CheckWaterSplashes", MORE),
            # Modern water's tiers, which only the Linux renderer draws: shown there, gone elsewhere
            combo("WaterFftResolution", "LabelWaterFftResolution", "ComboBoxWaterFftResolution", MORE),
            combo("WaterRefractionQuality", "LabelWaterRefractionQuality", "ComboBoxWaterRefractionQuality", MORE),
            check("MapAwareWaveHeading", "CheckMapAwareWaveHeading", MORE),
            check("UnderwaterClarity", "CheckUnderwaterClarity", MORE),
            combo("Reflections", "LabelReflections", "ComboBoxReflections", MORE),
            combo("WaterReflectionDetail", "LabelWaterReflectionDetail", "ComboBoxWaterReflectionDetail", MORE)])]),

    ("PageEffects", [
        ("GUI:OptionsHeadParticles", [
            slider("ParticleCap", "LabelParticleCap", "ParticleCapSlider", "ValueParticleCap"),
            combo("Smoke", "LabelSmoke", "ComboBoxSmoke"),
            check("DebrisTrails", "CheckDebrisTrails"),
            check("ParticleBounce", "CheckParticleBounce", MORE)]),
        ("GUI:OptionsHeadFire", [
            check("Firelight", "CheckFirelight"),
            check("HeatHaze", "CheckHeatHaze"),
            check("SmokeFireLighting", "CheckSmokeFireLighting", MORE),
            combo("CloudStyle", "LabelCloudStyle", "ComboBoxCloudStyle", MORE)]),
        ("GUI:OptionsHeadDestruction", [
            check("GroundMarks", "CheckGroundMarks"),
            check("StructureCollapse", "CheckStructureCollapse", MORE),
            check("Ragdoll", "CheckRagdoll", MORE),
            check("ExtraAnimations", "CheckExtraAnimations", MORE)])]),

    ("PageAudio", [
        ("GUI:OptionsHeadVolume", [
            ("MasterVolume", "fader", "LabelMasterVolume", "SliderMasterVolume", "ValueMasterVolume"),
            ("MusicVolume", "fader", "MusicVolumeLabel", "SliderMusicVolume", "ValueMusicVolume"),
            ("SFXVolume", "fader", "SFXVolumeLabel", "SliderSFXVolume", "ValueSFXVolume"),
            ("VoiceVolume", "fader", "VoiceVolumeLabel", "SliderVoiceVolume", "ValueVoiceVolume"),
            ("AmbientVolume", "fader", "LabelAmbientVolume", "SliderAmbientVolume", "ValueAmbientVolume")]),
        ("GUI:OptionsHeadOutput", [
            combo("AudioDevice", "LabelAudioDevice", "ComboBoxAudioDevice"),
            combo("SpeakerType", "LabelSpeakerType", "ComboBoxSpeakerType"),
            check("MuteInBackground", "CheckMuteInBackground")]),
        ("GUI:OptionsHeadMix", [
            check("IncomeSound", "CheckIncomeSound"),
            check("LegacySound", "CheckLegacySound", MORE),
            check("DynamicMusic", "CheckDynamicMusic", MORE)])]),

    ("PageControls", [
        ("GUI:OptionsHeadCamera", [
            slider("ScrollSpeed", "ScrollSpeedLabel", "SliderScrollSpeed", "ValueScrollSpeed"),
            slider("ZoomSpeed", "LabelZoomSpeed", "SliderZoomSpeed", "ValueZoomSpeed"),
            check("ZoomToCursor", "CheckZoomToCursor"),
            check("StartAtMaxZoom", "CheckStartAtMaxZoom"),
            check("CameraShake", "CheckCameraShake"),
            check("IdleWorkerCamera", "CheckIdleWorkerCamera"),
            check("EdgeScrollWindowed", "CheckEdgeScrollWindowed", MORE),
            check("IsometricCamera", "CheckIsometricCamera", MORE),
            check("SnapCamera45", "CheckSnapCamera45", MORE),
            check("DrawAnchor", "CheckBoxDrawAnchor", MORE),
            check("MoveAnchor", "CheckBoxMoveAnchor", MORE)]),
        ("GUI:OptionsHeadOrders", [
            check("Retaliation", "Retaliation"),
            check("FormationDrag", "CheckFormationDrag"),
            check("DoubleClickAttackMove", "CheckDoubleClickAttackMove", MORE),
            slider("DragTolerance", "LabelDragTolerance", "SliderDragTolerance", "ValueDragTolerance", MORE)]),
        ("GUI:OptionsHeadBuilding", [
            check("BuildWithoutWorker", "CheckBuildWithoutWorker"),
            check("GridBuild", "CheckGridBuild"),
            check("SnapBuildNeighbour", "CheckSnapBuildNeighbour"),
            check("NudgeBuild", "CheckNudgeBuild", MORE),
            check("SnapBuild45", "CheckSnapBuild45", MORE)]),
        ("GUI:OptionsHeadDevices", [
            check("ChromaLighting", "CheckChromaLighting", MORE)])]),

    ("PageInterface", [
        ("GUI:OptionsHeadBar", [
            combo("HudLayout", "LabelHudLayout", "ComboBoxHudLayout"),
            check("GridKeys", "CheckGridKeys"),
            slider("MinimapSize", "LabelMinimapSize", "SliderMinimapSize", "ValueMinimapSize"),
            check("NetBox", "CheckNetBox"),
            combo("HudWidth", "LabelHudWidth", "ComboBoxHudWidth", MORE),
            slider("MenuTransitionSpeed", "LabelMenuTransitionSpeed", "SliderMenuTransitionSpeed",
                   "ValueMenuTransitionSpeed", MORE)]),
        ("GUI:OptionsHeadMarkers", [
            combo("HealthBars", "LabelHealthBars", "ComboBoxHealthBars"),
            combo("PlayerColors", "LabelPlayerColors", "ComboBoxPlayerColors"),
            check("SelectionOutlines", "CheckSelectionOutlines"),
            check("OrderLines", "CheckOrderLines"),
            check("HitFlash", "CheckHitFlash"),
            check("DamageNumbers", "CheckDamageNumbers"),
            check("BehindBuilding", "CheckBehindBuilding"),
            combo("HealthBarColors", "LabelHealthBarColors", "ComboBoxHealthBarColors", MORE),
            check("AircraftAltitudeLine", "CheckAircraftAltitudeLine", MORE),
            check("EmptyBuildingPips", "CheckEmptyBuildingPips", MORE)]),
        ("GUI:OptionsHeadGame", [
            combo("Language", "LabelLanguage", "ComboBoxLanguage"),
            combo("GameSpeedMissions", "LabelGameSpeedMissions", "ComboGameSpeedMissions"),
            check("SaveCamera", "CheckBoxSaveCamera", MORE),
            check("UseCamera", "CheckBoxUseCamera", MORE)])]),
]

# the rail's two choices, not rows: their pane sentences are OPTIONTIP:Look and OPTIONTIP:Quality
RAIL_STEMS = ["Look", "Quality"]

# Controls a merge or the rail took off the screen.  They stay, hidden, as the model the code reads
# and writes: a catalog row keeps its widget and its Options.ini key, so nobody's choice changes
# (README-settings.md and the plan's "Merge, rename, drop").  The detail preset and its slider are
# Quality in the rail; Classic Graphics is Look's two boxes; Texture Filtering and Anisotropic Samples
# are one list; EA's Heat Effects is the second half of Heat Haze; Scene reflections and Reflection
# mode are one list; Alternate Mouse leaves the screen; EA's hidden leftovers stay hidden.
#   (page, name)
HIDDEN_MODELS = [
    ("PageSystem", "ResolutionNote"),
    ("PageSystem", "AntiAliasingLabel"),
    ("PageSystem", "ComboBoxAntiAliasing"),
    ("PageGraphics", "DetailLabel"),
    ("PageGraphics", "ComboBoxDetail"),
    ("PageGraphics", "DetailNote"),
    ("PageGraphics", "LabelGraphicsQuality"),
    ("PageGraphics", "ValueGraphicsQuality"),
    ("PageGraphics", "CheckClassicGraphics"),
    ("PageGraphics", "CheckUnlockFPS"),
    ("PageGraphics", "LabelTextureFilter"),
    ("PageGraphics", "ComboBoxTextureFilter"),
    ("PageGraphics", "LabelAnisotropy"),
    ("PageGraphics", "SliderAnisotropy"),
    ("PageGraphics", "ValueAnisotropy"),
    ("PageWorld", "CheckWaterReflectionEnabled"),
    ("PageWorld", "LabelWaterReflection"),
    ("PageWorld", "ComboBoxWaterReflection"),
    ("PageEffects", "CheckHeatEffects"),
    ("PageControls", "CheckAlternateMouse"),
]
# merged models the quality slider still sets though hidden (OptionsMenu.cpp, isMergedModel)
MERGED_MODELS = ["ComboBoxTextureFilter", "SliderAnisotropy", "CheckHeatEffects",
                 "CheckWaterReflectionEnabled", "ComboBoxWaterReflection"]

# The views new to this layout: the two merged lists, cloned beside the models they speak for.
#   (template, name, text key)
NEW_VIEWS = [
    ("LABEL", "LabelTextureFiltering", "GUI:OptionsTextureFiltering"),
    ("COMBO", "ComboBoxTextureFiltering", None),
    ("LABEL", "LabelReflections", "GUI:OptionsReflections"),
    ("COMBO", "ComboBoxReflections", None),
    # Key Grid's Grid Keys box (2026-11-27), the catalog's GridKeys row
    ("CHECK", "CheckGridKeys", "GUI:GridKeys"),
    # the catalog's TerrainRelief row, Ground Relief (2026-11-27)
    ("CHECK", "CheckTerrainRelief", "GUI:TerrainRelief"),
    # the catalog's IncomeSound row, the cash ding on a deposit (2026-11-28)
    ("CHECK", "CheckIncomeSound", "GUI:IncomeSound"),
]

# Renamed captions (the plan's renames and the two Turkish words the writing rule forced), and the
# keys under the panel.  A catalog row keeps its own label key; the screen shows these.
#   (control, text key)
TEXT_OVERRIDES = [
    ("CheckNoDynamicLOD", "GUI:OptionsKeepDetail"),
    ("CheckInternalHdr", "GUI:OptionsFireColors"),
    ("CheckDebrisTrails", "GUI:OptionsExplosionDebris"),
    ("CheckParticleBounce", "GUI:OptionsBouncingDebris"),
    ("LabelCloudStyle", "GUI:OptionsToxinClouds"),
    ("CheckHeatHaze", "GUI:OptionsHeatHaze"),
    ("CheckAmbientOcclusion", "GUI:OptionsAmbientOcclusion"),
    ("CheckHDTextures", "GUI:OptionsHDTextures"),
    ("CheckDynamicMusic", "GUI:OptionsDynamicMusic"),
    ("ButtonFirewallRefresh", "GUI:OptionsTest"),
    ("ButtonBack", "GUI:OptionsCancel"),
]
# the pane's name for the test row, which has no label of its own
BUTTON_ROW_KEYS = {"ConnectionTest": "GUI:OptionsConnectionTest"}

# The Hotkeys page: the commands on the left over the rows column, the bar of one building or unit on
# the right over the pane's, the picked command under the bar's three buttons, the note at the foot.
# Its lists are cloned from the LAN lobby's chat list.  OptionsMenuHotkeys.cpp fills all of it in.
#   (name, left, top, right, bottom)
HOTKEY_LIST_SOURCE = "ListboxChatWindowLanGame"
# 8 under the heads (BODY_TOP + HEAD_HEIGHT), 16 between the right column's parts, 8 between the keys
HOTKEY_COMMANDS = ("ListboxHotkeyCommands", 372, 174, 1100, 906)
HOTKEY_FACTION = ("ComboBoxHotkeyFaction", 1132, 174, 1860, 210)
HOTKEY_OBJECT = ("ComboBoxHotkeyObject", 1132, 226, 1860, 262)
HOTKEY_BUTTONS = ("ListboxHotkeyButtons", 1132, 278, 1860, 652)
HOTKEY_PICKED = ("HotkeyPicked", 1132, 730, 1860, 906)
HOTKEY_NOTE = ("HotkeyNote", 372, 914, 1860, 950)
HOTKEY_HEADINGS = [("GUI:HotkeysCommands", 372, 1100), ("GUI:HotkeysCommandBar", 1132, 1860)]
#   (name, caption key, left, right)
HOTKEY_BUTTON_ROW = [("ButtonHotkeyChange", "GUI:HotkeysChange", 1132, 1369),
                     ("ButtonHotkeyClear", "GUI:HotkeysClear", 1377, 1615),
                     ("ButtonHotkeyDefault", "GUI:HotkeysDefault", 1623, 1860)]
HOTKEY_BUTTON_TOP, HOTKEY_BUTTON_BOTTOM = 668, 714
HOTKEY_COMMAND_COLUMNS = (68, 32)
HOTKEY_BUTTON_COLUMNS = (8, 70, 22)
HOTKEY_STRINGS = ["GUI:HotkeysNone", "GUI:HotkeysPressKey", "GUI:HotkeysPressLetter",
                  "GUI:HotkeysClash", "GUI:HotkeysAlsoOn", "GUI:HotkeysFixed", "GUI:HotkeysGameKey",
                  "GUI:HotkeysMoved", "GUI:HotkeysPick", "GUI:HotkeysOnFixedKey",
                  "GUI:HotkeysButtonMoved", "GUI:HotkeysPicked", "GUI:HotkeysAlsoOnKey",
                  "TOOLTIP:HotkeyChange", "TOOLTIP:HotkeyClear", "TOOLTIP:HotkeyDefault"]

# Slider ranges, held to the catalog rows' own bounds by selfcheck.
SLIDER_RANGES = [("SliderAnisotropy", 0, 16), ("SliderDragTolerance", 2, 50),
                 ("SliderAmbientVolume", 0, 100), ("SliderZoomSpeed", 25, 300),
                 ("SliderMenuTransitionSpeed", 25, 400), ("SliderMasterVolume", 0, 100),
                 ("SliderGraphicsQuality", 0, 10), ("SliderMinimapSize", 100, 200)]

# Strings the code draws besides the rows' own: the rail, the fold, the badges, the pane's blocks,
# the reasons a row is greyed, the merged lists' entries, the connection test.
CODE_STRINGS = (
    ["GUI:OptionsTabSystem", "GUI:OptionsTabWorld",
     "GUI:OptionsLook", "GUI:OptionsLookName", "GUI:OptionsLookClassic", "GUI:OptionsLookReforged",
     "GUI:OptionsQuality", "GUI:OptionsQualityName", "GUI:OptionsLocked", "GUI:OptionsHeadFrameRate_Short",
     "GUI:OptionsBadgeRestart", "GUI:OptionsBadgeNextMap", "GUI:OptionsLegendRestart",
     "GUI:OptionsLegendNextMap", "GUI:OptionsNeeds", "GUI:OptionsNeededBy", "GUI:OptionsChoices",
     "GUI:OptionsGroupAsSet", "GUI:OptionsNot",
     "GUI:OptionsValueOn", "GUI:OptionsValueOff", "GUI:OptionsNeedD3D11", "GUI:OptionsTestNotRun",
     "GUI:OptionsTestDone", "GUI:OptionsReasonReforged", "GUI:OptionsReasonD3D11",
     "GUI:OptionsReasonClassicWater", "GUI:OptionsReasonCastShadows", "GUI:OptionsReasonLegacySound",
     "GUI:OptionsReasonFullscreen"]
    + ["GUI:OptionsQualityStep%d" % step for step in range(4)]
    + ["GUI:OptionsFiltering%d" % entry for entry in range(5)]
    + ["GUI:OptionsReflections%d" % entry for entry in range(4)]
    + [key for _control, key in TEXT_OVERRIDES] + list(BUTTON_ROW_KEYS.values())
    + [key for _t, _n, key in NEW_VIEWS if key])

TEMPLATES = {"CHECK": "Retaliation", "LABEL": "DetailLabel", "COMBO": "ComboBoxDetail",
             "BUTTON": "ButtonDefaults"}


def _named(name):
    return "OptionsMenu.wnd:%s" % name


def short(window):
    return (window.name or "").split(":")[-1]


def statement_end(props, start):
    end = start
    while ";" not in props[end]:
        end += 1
    return end


def drop_prop(window, key):
    start = window.prop_index(key)
    if start >= 0:
        del window.props[start:statement_end(window.props, start) + 1]


def text_key(window):
    statement = window.prop("TEXT")
    if statement is None:
        return None
    match = re.search(r'"([^"]*)"', statement)
    return match.group(1) if match else None


def set_status(window, hidden):
    """The window's STATUS with HIDDEN put in or taken out, the rest as it was."""
    statement = window.prop("STATUS") or "STATUS = ENABLED;"
    flags = [flag for flag in re.sub(r"^\s*STATUS\s*=\s*|;\s*$", "", statement).split("+") if flag.strip()]
    flags = [flag.strip() for flag in flags if flag.strip() != "HIDDEN"]
    if hidden:
        flags.append("HIDDEN")
    window.put_prop("STATUS", "+".join(flags) or "ENABLED")


def place(window, left, top, right, bottom):
    window.rect = (left, top, right, bottom) + WIDE


def to_wide(layout):
    """A layout still at 800x600, scaled to the 1920x1080 grid the way the shell stretched it."""
    for node in layout.root.walk():
        left, top, right, bottom, cw, ch = node.rect
        if (cw, ch) == WIDE:
            continue
        sx, sy = WIDE[0] / float(cw), WIDE[1] / float(ch)
        node.rect = (int(round(left * sx)), int(round(top * sy)), int(round(right * sx)),
                     int(round(bottom * sy))) + WIDE


def container(template, name):
    """A USER window that passes its children's messages up, see-through until the code draws it."""
    window = clone(template, _named(name))
    window.children = []
    window.put_prop("STATUS", "ENABLED+NOFOCUS+SEE_THRU")
    window.put_prop("SYSTEMCALLBACK", '"PassMessagesToParentSystem"')
    drop_prop(window, "TEXT")
    drop_prop(window, "TOOLTIPTEXT")
    return window


def make_list(template, spec, columns):
    name, left, top, right, bottom = spec
    listbox = clone(template, _named(name))
    place(listbox, left, top, right, bottom)
    listbox.set_prop("STATUS", "ENABLED")
    widths = "".join("COLUMNSWIDTH: %d, " % share for share in columns)
    listbox.set_prop("LISTBOXDATA", "LENGTH: 300, AUTOSCROLL: 0, SCROLLIFATEND: 0, AUTOPURGE: 0, SCROLLBAR: 1, "
                     "MULTISELECT: 0, COLUMNS: %d, %sFORCESELECT: 0" % (len(columns), widths))
    return listbox


def row_names(row):
    """The windows a row holds, label first."""
    kind = row[1]
    if kind in ("check", "button"):
        return [row[2]]
    if kind in ("combo", "entry"):
        return [row[2], row[3]]
    return [row[2], row[3], row[4]]


def build(layout):
    to_wide(layout)
    root = layout.root
    place(root, 0, 0, WIDE[0] - 1, WIDE[1] - 1)
    parent = layout.find("OptionsMenuParent")
    old = layout.find("OptionsMenuParentOld")
    lan = wndlayout.load(LAN_LAYOUT)
    lan_list = lan.find(HOTKEY_LIST_SOURCE)
    # the LAN lobby's list is authored at 1920x1080 or older; it is placed again below either way
    page_template = None

    # Everything on a page or generated by an earlier run comes off the old parent: the controls go
    # into waiting by name, what this script makes (pages, tabs, rows, heads, the pane, the rail's
    # boxes, the merged views) is made again.
    waiting = {}
    generated = ("Row", "Fader", "Head", "GroupHeading", "ButtonMore", "ButtonLook", "OptionsPane", "HotkeyPicked")
    kept = []
    for child in old.children:
        name = short(child)
        if name.startswith("Page"):
            if page_template is None:
                page_template = clone(child)
                page_template.children = []
            for node in child.children:
                inner = short(node)
                if inner.startswith(("Row", "Fader")):
                    for control in node.children:
                        waiting[short(control)] = control
                elif inner.startswith(generated) or inner in [n for _t, n, _k in NEW_VIEWS]:
                    continue
                else:
                    waiting[inner] = node
        elif name.startswith("Tab") or name.startswith(generated):
            continue
        elif name == "SliderGraphicsQuality":
            waiting[name] = child
        else:
            kept.append(child)
    old.children = kept

    templates = {}
    for key, name in TEMPLATES.items():
        source = waiting.get(name) or layout.find(name)
        templates[key] = clone(source)
        # ComboBoxDetail and DetailLabel are hidden models after the first run
        set_status(templates[key], False)
    for template, name, text in NEW_VIEWS:
        control = clone(templates[template], _named(name))
        control.children = []
        if text:
            control.put_prop("TEXT", '"%s"' % text)
        else:
            drop_prop(control, "TEXT")
        waiting[name] = control
    for name, key in TEXT_OVERRIDES:
        target = waiting.get(name) or layout.find(name)
        target.put_prop("TEXT", '"%s"' % key)
    for name, low, high in SLIDER_RANGES:
        waiting[name].put_prop("SLIDERDATA", "MINVALUE: %d, MAXVALUE: %d" % (low, high))

    def take(name):
        control = waiting.pop(name)
        drop_prop(control, "TOOLTIPTEXT")
        set_status(control, False)
        return control

    def make_page(name):
        page = clone(page_template, _named(name))
        page.children = []
        place(page, ROWS_LEFT, BODY_TOP, ROWS_RIGHT if name != "PageHotkeys" else PANE[2], BODY_BOTTOM)
        page.set_prop("STATUS", "ENABLED+NOFOCUS+SEE_THRU")
        return page

    def make_head(page_name, index, key, left, right, top):
        head = clone(templates["LABEL"], _named("Head%s%d" % (page_name[len("Page"):], index)))
        head.children = []
        head.put_prop("TEXT", '"%s"' % key)
        drop_prop(head, "TOOLTIPTEXT")
        head.put_prop("STATICTEXTDATA", "CENTERED: 0")
        place(head, left, top, right, top + HEAD_HEIGHT)
        return head

    pages = []
    for page_name, _tab, _text in TABS:
        page = make_page(page_name)
        pages.append(page)
        if page_name == "PageHotkeys":
            build_hotkeys_page(page, templates, lan_list, take)
            continue
        groups = dict(GROUP_LAYOUT)[page_name]
        fixed = (len(groups) * HEAD_HEIGHT + (len(groups) - 1) * GROUP_GAP
                 + sum(ROW_HEIGHT for _h, rows in groups for row in rows if row[1] != "fader"))
        fader_height = max(FADER_MIN, BODY_BOTTOM - BODY_TOP - fixed)
        y = BODY_TOP
        for index, (head_key, rows) in enumerate(groups):
            if index:
                y += GROUP_GAP
            page.children.append(make_head(page_name, index, head_key, ROWS_LEFT, ROWS_RIGHT, y))
            y += HEAD_HEIGHT
            faders = [row for row in rows if row[1] == "fader"]
            if faders:
                width = (ROWS_RIGHT - ROWS_LEFT) // len(faders)
                for at, row in enumerate(faders):
                    stem, _kind, label, control, readout = row
                    fader = container(page_template, "Fader%s" % stem)
                    fader.put_prop("TEXT", '"%s"' % text_key(waiting[label]))
                    left = ROWS_LEFT + at * width
                    place(fader, left + 2, y, left + width - 2, y + fader_height)
                    # the slider and its words stay inside as the model; the code draws the fader
                    for name in (label, control, readout):
                        part = take(name)
                        place(part, left + 4, y + 4, left + 24, y + 24)
                        set_status(part, True)
                        fader.children.append(part)
                    page.children.append(fader)
                y += fader_height
                continue
            for row in rows:
                stem, kind = row[0], row[1]
                height = BUTTON_ROW_HEIGHT if kind == "button" else ROW_HEIGHT
                box = container(page_template, "Row%s" % stem)
                place(box, ROWS_LEFT, y, ROWS_RIGHT, y + height)
                names = row_names(row)
                if kind == "check":
                    control = take(names[0])
                    place(control, ROWS_LEFT, y, ROWS_RIGHT, y + height)
                    box.children.append(control)
                    key = text_key(control)
                elif kind == "button":
                    control = take(names[0])
                    place(control, LABEL_LEFT, y + 4, LABEL_LEFT + 160, y + height - 4)
                    box.children.append(control)
                    key = BUTTON_ROW_KEYS[stem]
                else:
                    label = take(names[0])
                    label.put_prop("STATICTEXTDATA", "CENTERED: 0")
                    place(label, LABEL_LEFT, y, LABEL_RIGHT, y + height)
                    box.children.append(label)
                    key = text_key(label)
                    control = take(names[1])
                    if kind == "slider":
                        place(control, SLIDER_LEFT, y + 5, SLIDER_RIGHT, y + height - 5)
                        readout = take(names[2])
                        readout.put_prop("STATICTEXTDATA", "CENTERED: 0")
                        place(readout, READOUT_LEFT, y, CONTROL_RIGHT, y + height)
                        box.children.append(control)
                        box.children.append(readout)
                    else:
                        place(control, CONTROL_LEFT, y + 2, CONTROL_RIGHT, y + height - 2)
                        box.children.append(control)
                box.put_prop("TEXT", '"%s"' % key)
                page.children.append(box)
                y += height

    for page_name, name in HIDDEN_MODELS:
        control = waiting.pop(name)
        page = pages[[p for p, _t, _x in TABS].index(page_name)]
        place(control, ROWS_LEFT, BODY_TOP, ROWS_LEFT + 20, BODY_TOP + 20)
        set_status(control, True)
        page.children.append(control)

    # the rail: the quality slider, Look's two boxes, the tabs
    quality = take("SliderGraphicsQuality")
    place(quality, *QUALITY_SLIDER)
    look = []
    for (left, right), name, key in zip(LOOK_BOXES, ("ButtonLookClassic", "ButtonLookReforged"),
                                        ("GUI:OptionsLookClassic", "GUI:OptionsLookReforged")):
        box = clone(templates["BUTTON"], _named(name))
        box.children = []
        box.put_prop("TEXT", '"%s"' % key)
        drop_prop(box, "TOOLTIPTEXT")
        place(box, left, LOOK_TOP, right, LOOK_BOTTOM)
        look.append(box)
    pane = container(page_template, "OptionsPane")
    place(pane, *PANE)

    if waiting:
        raise KeyError("given no place: %s" % ", ".join(sorted(waiting)))

    # the frame, the title, the keys under the foot rule
    place(parent, *PANEL)
    place(old, *PANEL)
    place(layout.find("LabelTitle"), *TITLE)
    place(layout.find("LabelVersion"), *VERSION)
    place(layout.find("Line"), *RULE)
    place(layout.find("ButtonDefaults"), DEFAULTS_KEY[0], KEYS_TOP, DEFAULTS_KEY[1], KEYS_BOTTOM)
    place(layout.find("ButtonBack"), BACK_KEY[0], KEYS_TOP, BACK_KEY[1], KEYS_BOTTOM)
    place(layout.find("ButtonAccept"), ACCEPT_KEY[0], KEYS_TOP, ACCEPT_KEY[1], KEYS_BOTTOM)
    for name in ("ButtonDefaults", "ButtonBack", "ButtonAccept"):
        drop_prop(layout.find(name), "TOOLTIPTEXT")

    # last in the file is drawn first: the pane, the rail and the tabs, then the pages over them
    old.children.append(pane)
    old.children.append(quality)
    old.children.extend(look)
    for index, (_page, tab_name, text) in enumerate(TABS):
        tab = clone(templates["BUTTON"], _named(tab_name))
        tab.children = []
        tab.put_prop("TEXT", '"%s"' % text)
        drop_prop(tab, "TOOLTIPTEXT")
        top = BODY_TOP + index * TAB_PITCH
        place(tab, RAIL_LEFT, top, RAIL_RIGHT, top + TAB_HEIGHT)
        old.children.append(tab)
    old.children.extend(pages)
    return layout


def build_hotkeys_page(page, templates, lan_list, take):
    for index, (key, left, right) in enumerate(HOTKEY_HEADINGS):
        head = clone(templates["LABEL"], _named("HeadHotkeys%d" % index))
        head.children = []
        head.put_prop("TEXT", '"%s"' % key)
        drop_prop(head, "TOOLTIPTEXT")
        head.put_prop("STATICTEXTDATA", "CENTERED: 0")
        place(head, left, BODY_TOP, right, BODY_TOP + HEAD_HEIGHT)
        page.children.append(head)
    # the lists are cloned afresh from the LAN lobby's, which the page's old copies came from
    for spec, columns in ((HOTKEY_COMMANDS, HOTKEY_COMMAND_COLUMNS), (HOTKEY_BUTTONS, HOTKEY_BUTTON_COLUMNS)):
        take(spec[0])
        page.children.append(make_list(lan_list, spec, columns))
    for name, left, top, right, bottom in (HOTKEY_FACTION, HOTKEY_OBJECT):
        combo_box = take(name)
        place(combo_box, left, top, right, bottom)
        page.children.append(combo_box)
    for name, key, left, right in HOTKEY_BUTTON_ROW:
        button = take(name)
        button.put_prop("TEXT", '"%s"' % key)
        button.put_prop("TOOLTIPTEXT", '"TOOLTIP:%s"' % name[len("Button"):])
        place(button, left, HOTKEY_BUTTON_TOP, right, HOTKEY_BUTTON_BOTTOM)
        page.children.append(button)
    name, left, top, right, bottom = HOTKEY_NOTE
    note = take(name)
    place(note, left, top, right, bottom)
    page.children.append(note)
    name, left, top, right, bottom = HOTKEY_PICKED
    picked = clone(templates["LABEL"], _named(name))
    picked.children = []
    drop_prop(picked, "TEXT")
    drop_prop(picked, "TOOLTIPTEXT")
    place(picked, left, top, right, bottom)
    page.children.append(picked)


# ---------------------------------------------------------------------------
# selfcheck: the files have to agree
# ---------------------------------------------------------------------------

_HERE = os.path.dirname(os.path.abspath(__file__))
_CODE = os.path.dirname(_HERE)

CATALOG = os.path.join(_CODE, "GameEngine", "Source", "Common", "OptionsCatalog.cpp")
STRINGS = os.path.join(_CODE, "Data", "Patch.str")
TURKISH = os.path.join(_CODE, "Data", "Turkish", "Generals.str")
LAYOUT = os.path.join(_CODE, "Data", "Window", "Menus", "OptionsMenuClassic.wnd")
LAN_LAYOUT = os.path.join(_CODE, "Data", "Window", "Menus", "LanGameOptionsMenu.wnd")
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


_QUALITY_ROW = re.compile(r'\{\s*"(?P<control>\w+)",\s*(?P<cost>-?[\d.]+)f?,\s*\{(?P<values>[^}]*)\}\s*\}')


def read_quality_table():
    with open(CATALOG, "rb") as fp:
        text = fp.read().decode("latin-1")
    table = text[text.index("TheGraphicsQualityTable[] ="):]
    table = table[:table.index("};")]
    return [(match.group("control"), [int(value) for value in match.group("values").split(",")])
            for match in _QUALITY_ROW.finditer(table)]


def bound(text):
    text = text.strip()
    return int(text) if text.lstrip("-").isdigit() else enum_count(text.split()[0]) - 1


def read_strings(path=STRINGS):
    """The keys a .str file defines.  A key is a line of its own, the value the quoted line under it,
    and '//' the only comment GameText.cpp's parseStringFile knows."""
    keys = set()
    with open(path, "rb") as fp:
        for line in fp.read().decode("utf-8", "replace").splitlines():
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def enum_count(name):
    pattern = re.compile(r"\b%s\s*=\s*(\d+)" % re.escape(name))
    listed = re.compile(r"\benum\s*\{([^{}=]*?)\b%s\s*[,}]" % re.escape(name))
    for root, _dirs, files in os.walk(INCLUDE):
        for filename in files:
            if not filename.endswith(".h"):
                continue
            with open(os.path.join(root, filename), "rb") as fp:
                text = fp.read().decode("latin-1")
            match = pattern.search(text)
            if match:
                return int(match.group(1))
            match = listed.search(text)
            if match:
                return len([entry for entry in match.group(1).split(",") if entry.strip()])
    return None


def overlaps(container_window):
    """Two visible windows in one container drawn over each other, through the whole tree."""
    found = []
    # the pages stand on one another and the code shows one at a time, the pane hidden under Hotkeys
    visible = [(short(child), child.rect[:4]) for child in container_window.children
               if "HIDDEN" not in (child.prop("STATUS") or "")
               and not short(child).startswith(("Page", "OptionsPane"))]
    for index, (name, (left, top, right, bottom)) in enumerate(visible):
        for other, (o_left, o_top, o_right, o_bottom) in visible[index + 1:]:
            if left < o_right and o_left < right and top < o_bottom and o_top < bottom:
                found.append("%s: %s and %s overlap" % (short(container_window), name, other))
    for child in container_window.children:
        found.extend(overlaps(child))
    return found


def selfcheck():
    rows = read_catalog()
    keys = read_strings()
    turkish = read_strings(TURKISH)
    layout = wndlayout.load(LAYOUT)
    controls = set(short(node) for node in layout.root.walk())
    problems = []
    if len(rows) < 9:
        problems.append("only %d catalog rows parsed, the regex has stopped matching" % len(rows))

    for row in rows:
        widget, label = row["widget"], row["label"]
        if widget is None:
            if label:
                problems.append("%s has a label key and no widget" % row["ini"])
            continue
        if widget not in controls:
            problems.append("%s names %s, which is not in %s" % (row["ini"], widget, os.path.basename(LAYOUT)))
        if not label:
            problems.append("%s has a widget and no label key" % row["ini"])
            continue
        if label not in keys:
            problems.append("%s label %s is not in Patch.str" % (row["ini"], label))
        if row["kind"] == "OPTION_INT":
            bounds = (bound(row["lo"]), bound(row["hi"]))
            if dict((name, (low, high)) for name, low, high in SLIDER_RANGES).get(widget) != bounds:
                problems.append("%s runs %d to %d and SLIDER_RANGES does not give %s that range"
                                % ((row["ini"],) + bounds + (widget,)))
        if row["kind"] == "OPTION_ENUM":
            constant = row["hi"].split()[0]
            count = enum_count(constant)
            if count is None:
                problems.append("%s: no header declares %s" % (row["ini"], constant))
                continue
            for entry_index in range(int(row["lo"]), count):
                if "%s%d" % (label, entry_index) not in keys:
                    problems.append("%s entry %s%d is not in Patch.str" % (row["ini"], label, entry_index))

    # every row, fader and rail choice has its pane sentences in both languages, and its name
    stems = [row[0] for _page, groups in GROUP_LAYOUT for _h, rs in groups for row in rs] + RAIL_STEMS
    # the plan's 118, Key Grid's Grid Keys and Ground Relief (2026-11-27), Income Sound (2026-11-28)
    if len(stems) != 121:
        problems.append("%d settings on the screen, the plan's tooltip table holds 118, Grid Keys, Ground Relief and Income Sound" % len(stems))
    for stem in stems:
        for table, language in ((keys, "Patch.str"), (turkish, "Turkish/Generals.str")):
            if "OPTIONTIP:%s" % stem not in table:
                problems.append("OPTIONTIP:%s is not in %s" % (stem, language))
    for key in CODE_STRINGS + [head for _p, groups in GROUP_LAYOUT for head, _r in groups] \
            + [text for _p, _t, text in TABS] + [key for key, _l, _r in HOTKEY_HEADINGS] \
            + [key for _n, key, _l, _r in HOTKEY_BUTTON_ROW] + HOTKEY_STRINGS:
        for table, language in ((keys, "Patch.str"), (turkish, "Turkish/Generals.str")):
            if key not in table and not key.startswith("TOOLTIP:") and not key.startswith("GUI:Hotkeys"):
                problems.append("%s is not in %s" % (key, language))
        if key not in keys and key not in turkish:
            problems.append("%s is in neither string table" % key)

    # every window the code looks up
    wanted = ([page for page, _t, _x in TABS] + [tab for _p, tab, _x in TABS]
              + [name for _p, name in HIDDEN_MODELS] + ["OptionsPane", "ButtonLookClassic", "ButtonLookReforged",
                                                       "SliderGraphicsQuality", HOTKEY_PICKED[0], HOTKEY_NOTE[0]]
              + [spec[0] for spec in (HOTKEY_COMMANDS, HOTKEY_FACTION, HOTKEY_OBJECT, HOTKEY_BUTTONS)]
              + [name for name, _k, _l, _r in HOTKEY_BUTTON_ROW])
    for page, groups in GROUP_LAYOUT:
        for _h, rs in groups:
            for row in rs:
                prefix = "Fader" if row[1] == "fader" else "Row"
                wanted.append(prefix + row[0])
                wanted.extend(row_names(row))
    for name in wanted:
        if name not in controls:
            problems.append("%s has no %s" % (os.path.basename(LAYOUT), name))
    for name in MERGED_MODELS:
        if name not in [n for _p, n in HIDDEN_MODELS]:
            problems.append("merged model %s is not kept hidden" % name)

    # every row of the quality slider names a control on Graphics, World, Effects or System
    steps = enum_count("GRAPHICS_QUALITY_STEPS")
    quality = read_quality_table()
    if len(quality) < 9:
        problems.append("only %d quality rows parsed, the regex has stopped matching" % len(quality))
    on_pages = set()
    for page in ("PageSystem", "PageGraphics", "PageWorld", "PageEffects"):
        window = layout.find(page)
        if window is not None:
            on_pages.update(short(node) for node in window.walk())
    for control, values in quality:
        if control not in on_pages:
            problems.append("quality row %s is not a control on System, Graphics, World or Effects" % control)
        if len(values) != steps:
            problems.append("quality row %s has %d values for %d steps" % (control, len(values), steps))

    # authored at 1920x1080, nothing drawn over anything, no fold left (a page too long scrolls)
    for node in layout.root.walk():
        if tuple(node.rect[4:]) != WIDE:
            problems.append("%s is authored at %dx%d" % (short(node) or "the root", node.rect[4], node.rect[5]))
        if short(node).startswith(("RowMore", "ButtonMore")):
            problems.append("%s is a fold's window; every row stands in the column" % short(node))
    problems.extend(overlaps(layout.root))
    for _page, tab, _text in TABS:
        control = layout.find(tab)
        if control is None or control.rect[0] < RAIL_LEFT or control.rect[2] > RAIL_RIGHT:
            problems.append("%s is not in the rail" % tab)

    # the script reproduces the tracked file
    with open(LAYOUT, "rb") as fp:
        tracked = fp.read().decode("latin-1").replace("\r\n", "\n")
    again = build(wndlayout.load(LAYOUT)).text().replace("\r\n", "\n")
    if again != tracked:
        problems.append("running the script on %s changes it; regenerate it" % os.path.basename(LAYOUT))

    for problem in problems:
        print("optionsmenu: %s" % problem)
    if problems:
        return 1
    print("optionsmenu: %d catalog rows and %d settings agree with %s, Patch.str and the Turkish table"
          % (len(rows), len(stems), os.path.basename(LAYOUT)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    layout = wndlayout.load(argv[1])
    result = build(layout).text().replace("\r\n", "\n").encode("latin-1")
    with open(argv[2], "wb") as output:
        output.write(result)
    print("%s: %d windows" % (argv[2], sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
