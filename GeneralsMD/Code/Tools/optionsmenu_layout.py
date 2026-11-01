#!/usr/bin/env python3
"""Rebuild OptionsMenu.wnd as eight tabbed pages on one grid.

EA's options screen is one 800x600 panel with everything on it at once, and it was already full
when it shipped: the language filter, the keyboard button and the four camera checkboxes are all
still in the file, parked off the right edge with HIDDEN set, because there was nowhere left to put
them.  Seventeen settings later there is no version of "find room" that works.

So the screen becomes eight pages behind eight buttons: Display, Graphics, Effects, Audio, Controls,
Interface, Network and Hotkeys, the last two lists rather than settings (build_hotkeys_page).  Every graphics setting is on Graphics or Effects, including the ones EA hid in
a popup that only opened when Custom was picked from a combo box on another page.  Each page is a list
of titled groups, sorted by what a setting does, flowed into three columns on one grid, so a heading,
a label and a box start in the same place whichever tab is open, and every slider has a readout
beside it.  Nothing is redrawn: the controls keep the images and tooltips they shipped with, a label
or a check box takes the lettering of the one next to it, and what is new is cloned from a control
that is already there.

The result is OptionsMenuClassic.wnd, which Shell.cpp opens.

    python optionsmenu_layout.py <shipped OptionsMenu.wnd> <output .wnd>

The input is the file out of WindowZH.big:

    python bigfile.py extract ../../Run/WindowZH.big "*/OptionsMenu.wnd" -o wnd
    python optionsmenu_layout.py wnd/Window/Menus/OptionsMenu.wnd ../Data/Window/Menus/OptionsMenuClassic.wnd

The output is the tracked master under Code/Data; the build copies it to Run/Window/Menus/, where a
loose file beats the archive.  Layouts are not in the multiplayer INI checksum, so it does not have to
match across a network game.

    python optionsmenu_layout.py selfcheck

reads the tracked files back and checks they still agree: every widget TheOptionCatalog names exists
in the layout, and every label, tooltip and combo box entry it needs is in Patch.str.  Run by CTest as
optionsmenu_selfcheck.
"""

import os
import re
import sys

import wndlayout
from wndlayout import clone


# The panel and everything on it, in the layout's own 800x600 creation resolution.  One inner edge,
# 16 pixels in from the panel on both sides, is where the title, the tabs, the pages, the buttons
# and the version line all start and stop.  The frame is as tall as the tallest group column needs -
# Effects' Shadows column and Graphics' Quality column both end 312 pixels below their heading, on the
# page's last pixel - plus the title, the tabs and the buttons, and it is centred on the 600 line: 58
# above it, 58 below.  A column that runs past the page is refused when the layout is built.  Eight tabs
# still fit the inner width at 87 pixels each.
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
    ("PageInterface", "TabInterface", "GUI:OptionsTabInterface"),
    ("PageNetwork",  "TabNetwork",  "GUI:OptionsTabNetwork"),
    ("PageHotkeys",  "TabHotkeys",  "GUI:OptionsTabHotkeys"),
]

# The Hotkeys page is two lists rather than groups of settings: every command on the left, and on the
# right the command bar of one building or unit, picked by general and object.  Its lists are cloned
# from the LAN lobby's chat list, the one scrolling list box with EA's art that is two layouts away,
# and its buttons from the Network page's Refresh.  OptionsMenuHotkeys.cpp fills all of it in.
#   (name, left, top, width, height)
HOTKEY_LIST_SOURCE = "ListboxChatWindowLanGame"
HOTKEY_COMMANDS = ("ListboxHotkeyCommands", 40, 182, 344, 220)
HOTKEY_FACTION = ("ComboBoxHotkeyFaction", 408, 182, 352, 24)
HOTKEY_OBJECT = ("ComboBoxHotkeyObject", 408, 214, 352, 24)
HOTKEY_BUTTONS = ("ListboxHotkeyButtons", 408, 246, 352, 156)
# four lines of the notes' 10 point lettering: a clash names every button on the letter
HOTKEY_NOTE = ("HotkeyNote", 34, 408, 398, 54)
HOTKEY_HEADINGS = [("GUI:HotkeysCommands", 40), ("GUI:HotkeysCommandBar", 408)]
#   (name, caption key, left)
HOTKEY_BUTTON_ROW = [("ButtonHotkeyChange", "GUI:HotkeysChange", 440),
                     ("ButtonHotkeyClear", "GUI:HotkeysClear", 548),
                     ("ButtonHotkeyDefault", "GUI:HotkeysDefault", 656)]
HOTKEY_BUTTON_TOP, HOTKEY_BUTTON_WIDTH = 412, 104
# the share of each list's width a column takes: name and key, and on the bar a cameo first
HOTKEY_COMMAND_COLUMNS = (68, 32)
HOTKEY_BUTTON_COLUMNS = (12, 66, 22)
# strings OptionsMenuHotkeys.cpp shows besides the captions above
HOTKEY_STRINGS = ["GUI:HotkeysNone", "GUI:HotkeysPressKey", "GUI:HotkeysPressLetter", "GUI:HotkeysPressForceAttack",
                  "GUI:HotkeysClash", "GUI:HotkeysAlsoOn", "GUI:HotkeysFixed", "GUI:HotkeysGameKey",
                  "GUI:HotkeysMoved", "GUI:HotkeysForceAttack", "GUI:HotkeysPick", "GUI:HotkeysOnFixedKey",
                  "GUI:HotkeysButtonMoved", "TOOLTIP:HotkeyChange", "TOOLTIP:HotkeyClear",
                  "TOOLTIP:HotkeyDefault"]

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

# Combo boxes OptionsMenu.cpp fills in by name for the same reason: the monitors on the desktop and
# the audio endpoints are not a range a catalog row can describe, and the speaker setup is EA's
# SpeakerType string in Options.ini, which the audio manager reads itself.
MENU_COMBOS = ["ComboBoxMonitor", "ComboBoxSpeakerType", "ComboBoxAudioDevice"]

# The catalog's shadow rows, which stood in GameData.ini with no control until the Effects page.
SHADOW_CHECKS = ["Check3DShadows", "Check2DShadows", "CheckCloudShadows", "CheckInfantryShadows",
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

# The templates new controls are cloned from, and whose lettering the moved ones take.
CHECK, LABEL, COMBO, SLIDER = "Retaliation", "DetailLabel", "ComboBoxDetail", "SliderGamma"

# The fork's own controls.  Where they stand is decided below with everything else.
#   (template, name, text key)
NEW_CONTROLS = [
    (LABEL,  "LabelMonitor",           "GUI:Monitor"),
    (COMBO,  "ComboBoxMonitor",        None),
    (LABEL,  "LabelWindowMode",        "GUI:WindowMode"),
    (COMBO,  "ComboBoxWindowMode",     None),
    (LABEL,  "LabelFullscreenScaling", "GUI:FullscreenScaling"),
    (COMBO,  "ComboBoxFullscreenScaling", None),
    (CHECK,  "CheckVSync",             "GUI:VSync"),
    (CHECK,  "CheckClassicGraphics",   "GUI:ClassicGraphics"),
    (LABEL,  "LabelWaterStyle",        "GUI:WaterStyle"),
    (COMBO,  "ComboBoxWaterStyle",      None),
    (LABEL,  "LabelCloudStyle",        "GUI:CloudStyle"),
    (COMBO,  "ComboBoxCloudStyle",      None),
    (LABEL,  "LabelMSAA",              "GUI:MSAA"),
    (COMBO,  "ComboBoxMSAA",           None),
    (LABEL,  "LabelBloom",             "GUI:Bloom"),
    (COMBO,  "ComboBoxBloom",          None),
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
    (CHECK,  "CheckNetBox",            "GUI:NetBox"),
    (CHECK,  "CheckEmptyBuildingPips", "GUI:EmptyBuildingPips"),
    (CHECK,  "CheckZoomToCursor",      "GUI:ZoomToCursor"),
    (CHECK,  "CheckIsometricCamera",   "GUI:IsometricCamera"),
    (CHECK,  "CheckCameraShake",       "GUI:CameraShake"),
    (CHECK,  "CheckStartAtMaxZoom",    "GUI:StartAtMaxZoom"),
    (CHECK,  "CheckSnapCamera45",      "GUI:SnapCamera45"),
    (CHECK,  "CheckGridBuild",         "GUI:GridBuild"),
    (CHECK,  "CheckSnapBuild45",       "GUI:SnapBuild45"),
    (CHECK,  "CheckSnapBuildNeighbour", "GUI:SnapBuildNeighbour"),
    (CHECK,  "CheckNudgeBuild",        "GUI:NudgeBuild"),
    (LABEL,  "LabelDragTolerance",     "GUI:DragTolerance"),
    (SLIDER, "SliderDragTolerance",    None),
    (CHECK,  "CheckTreeSway",          "GUI:TreeSway"),
    (CHECK,  "CheckInfantryShadows",   "GUI:InfantryShadows"),
    (CHECK,  "CheckProjectileShadows", "GUI:ProjectileShadows"),
    (CHECK,  "CheckPropShadows",       "GUI:PropShadows"),
    (CHECK,  "CheckParticleShadows",   "GUI:ParticleShadows"),
    (LABEL,  "LabelSmoke",             "GUI:Smoke"),
    (COMBO,  "ComboBoxSmoke",          None),
    (CHECK,  "CheckParticleBounce",    "GUI:ParticleBounce"),
    (CHECK,  "CheckChromaLighting",    "GUI:ChromaLighting"),
    (LABEL,  "LabelAmbientVolume",     "GUI:AmbientVolume"),
    (SLIDER, "SliderAmbientVolume",    None),
    (LABEL,  "LabelMasterVolume",      "GUI:MasterVolume"),
    (SLIDER, "SliderMasterVolume",     None),
    (CHECK,  "CheckDynamicMusic",      "GUI:DynamicMusic"),
    (LABEL,  "LabelAudioDevice",       "GUI:AudioDevice"),
    (COMBO,  "ComboBoxAudioDevice",    None),
    (LABEL,  "LabelSpeakerType",       "GUI:SpeakerType"),
    (COMBO,  "ComboBoxSpeakerType",    None),
    (CHECK,  "CheckMuteInBackground",  "GUI:MuteInBackground"),
    (CHECK,  "CheckLegacySound",       "GUI:LegacySound"),
    (LABEL,  "LabelZoomSpeed",         "GUI:ZoomSpeed"),
    (SLIDER, "SliderZoomSpeed",        None),
    (LABEL,  "LabelShadowQuality",     "GUI:ShadowQuality"),
    (COMBO,  "ComboBoxShadowQuality",  None),
    (CHECK,  "CheckVolumetricSmoke",   "GUI:VolumetricSmoke"),
    (LABEL,  "LabelLightingStyle",     "GUI:LightingStyle"),
    (COMBO,  "ComboBoxLightingStyle",  None),
    (CHECK,  "CheckSmokeFireLighting", "GUI:SmokeFireLighting"),
    (CHECK,  "CheckDebrisTrails",      "GUI:DebrisTrails"),
    (CHECK,  "CheckEdgeScrollWindowed", "GUI:EdgeScrollWindowed"),
    (CHECK,  "CheckFormationDrag",     "GUI:FormationDrag"),
    (CHECK,  "CheckBuildWithoutWorker", "GUI:BuildWithoutWorker"),
    (LABEL,  "LabelHealthBarColors",   "GUI:HealthBarColors"),
    (COMBO,  "ComboBoxHealthBarColors", None),
    (LABEL,  "LabelMenuTransitionSpeed", "GUI:MenuTransitionSpeed"),
    (SLIDER, "SliderMenuTransitionSpeed", None),
    (CHECK,  "CheckTurboModeMissions", "GUI:TurboModeMissions"),
    (CHECK,  "CheckMapLight",          "GUI:MapLight"),
    (CHECK,  "CheckTeamGlow",          "GUI:TeamGlow"),
    (CHECK,  "CheckWaterFoam",         "GUI:WaterFoam"),
    (CHECK,  "CheckGroundMarks",       "GUI:GroundMarks"),
    (CHECK,  "CheckFirelight",         "GUI:Firelight"),
    (CHECK,  "CheckHemiAmbient",       "GUI:HemiAmbient"),
    (CHECK,  "CheckAmbientOcclusion",  "GUI:AmbientOcclusion"),
    (CHECK,  "CheckInternalHdr",       "GUI:InternalHdr"),
    (CHECK,  "CheckWeather",           "GUI:Weather"),
    (CHECK,  "CheckWaterSplashes",     "GUI:WaterSplashes"),
    (CHECK,  "CheckAtmosphere",        "GUI:Atmosphere"),
    (CHECK,  "CheckStructureCollapse", "GUI:StructureCollapse"),
    (CHECK,  "CheckSmoothMotion",      "GUI:SmoothMotion"),
    (LABEL,  "LabelGraphicsQuality",   "GUI:GraphicsQuality"),
    (SLIDER, "SliderGraphicsQuality",  None),
]

# EA's check boxes that shipped HIDDEN off the panel's right edge with working code behind them in
# OptionsMenu.cpp, which saves each one the moment it is clicked.  They come onto the pages with a
# tooltip each; their captions are EA's labels, retitled in Patch.str.
#   (name, tooltip key)
EA_PARKED = [
    ("CheckBoxDrawAnchor", "TOOLTIP:DrawScrollAnchor"),
    ("CheckBoxMoveAnchor", "TOOLTIP:MoveScrollAnchor"),
    ("CheckBoxSaveCamera", "TOOLTIP:SaveCamera"),
    ("CheckBoxUseCamera",  "TOOLTIP:UseCamera"),
]

# A slider on its own says nothing about where it stands, so each one has a readout beside it that
# OptionsMenu.cpp writes.  Cloned from a label with its caption and tooltip taken off.
READOUTS = [
    "ValueGamma", "ValueTextureResolution", "ValueParticleCap", "ValueAnisotropy",
    "ValueMusicVolume", "ValueSFXVolume", "ValueVoiceVolume", "ValueScrollSpeed",
    "ValueDragTolerance", "ValueAmbientVolume", "ValueZoomSpeed", "ValueMenuTransitionSpeed",
    "ValueMasterVolume", "ValueGraphicsQuality",
]

# Lines of small grey text under a control that say what its current choice does, written by
# OptionsMenu.cpp.  The detail preset is the one that needs it: picking Ultra only fills in the boxes,
# and nothing on the shipped screen said that Accept is what applies them.
#   (name, label key whose entries 0..count-1 it shows, count)
# The resolution's line is one short sentence, shown only when the monitor lists the picked size.
NOTES = [("DetailNote", "GUI:DetailNote", 5), ("ResolutionNote", "GUI:ResolutionNote", 1)]
NOTE_HEIGHT = 80
NOTE_HEIGHTS = {"ResolutionNote": 20}


def note_height(name):
    return NOTE_HEIGHTS.get(name, NOTE_HEIGHT)
NOTE_FONT = 'NAME: "Arial", SIZE: 10, BOLD: 0'
# UIStyle's MUTED, the front end's colour for a second line
NOTE_COLOR = ("ENABLED:  164 170 190 255, ENABLEDBORDER:  0 0 0 255, "
              "DISABLED: 164 170 190 255, DISABLEDBORDER: 0 0 0 255, "
              "HILITE:   164 170 190 255, HILITEBORDER:   0 0 0 255")

# a cloned slider keeps its template's range unless it is given one; selfcheck holds these to the
# catalog row's own bounds
SLIDER_RANGES = [("SliderAnisotropy", 0, 16), ("SliderDragTolerance", 2, 50),
                 ("SliderAmbientVolume", 0, 100), ("SliderZoomSpeed", 25, 300),
                 ("SliderMenuTransitionSpeed", 25, 400), ("SliderMasterVolume", 0, 100),
                 ("SliderGraphicsQuality", 0, 10)]

# EA's captions that do not fit the page: two popup headings written in capitals, and a check box
# caption that ran 20 pixels past the panel's right edge once it stood in a 268 pixel column.
#   (control, text key)
TEXT_OVERRIDES = [
    ("LabelTextureResolution", "GUI:TextureResolution"),
    ("LabelParticleCap", "GUI:ParticleLimit"),
    ("CheckNoDynamicLOD", "GUI:NeverLowerDetail"),
]

# Three columns on every page, one spacing scale: 4 8 16 24 32.  Columns 224 wide, 24 apart, 4 in
# from the page edge; a group's heading on its top and its content 32 below, the first heading on
# 150.  A setting is its label over its control, pitch 56, and a slider stops at 136 with its readout
# beside it on the same row; a check box takes 28, a button 32.  A column holds one group, or two
# stacked 16 apart when a page has more groups than columns.
COLUMNS = (40, 288, 536)
COLUMN_WIDTH = 224
HEADING_TOP, CONTENT_TOP = 150, 182
HEADING_PITCH, GROUP_GAP = CONTENT_TOP - HEADING_TOP, 16
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


# Each page's groups in reading order, sorted by what a setting does rather than where EA put it.
# Display is the screen itself; Graphics what the picture costs and how sharp it is, with the glow;
# Effects what stands in the world - shadows, scenery, smoke; Controls the camera, the orders, the
# placement of a building and the input devices; Interface the HUD, what is drawn over the
# battlefield and the language of the words.
#   (page, [(heading key, items)])
GROUP_LAYOUT = [
    ("PageDisplay", [
        ("GUI:OptionsGroupScreen", [
            setting("LabelMonitor", "ComboBoxMonitor"),
            setting("ResolutionLabel", "ComboBoxResolution"),
            ("note", "ResolutionNote"),
            setting("LabelWindowMode", "ComboBoxWindowMode"),
            setting("LabelFullscreenScaling", "ComboBoxFullscreenScaling"),
            ("check", "CheckVSync")]),
        ("GUI:OptionsGroupPicture", [
            setting("GammaLabel", "SliderGamma", "ValueGamma"),
            # every column of Graphics and Effects is full
            ("check", "CheckStructureCollapse"),
            ("check", "CheckSmoothMotion")])]),

    ("PageGraphics", [
        # the particle limit went to Effects' Particles group to make room for the quality slider
        ("GUI:OptionsGroupDetail", [
            setting("DetailLabel", "ComboBoxDetail"),
            setting("LabelGraphicsQuality", "SliderGraphicsQuality", "ValueGraphicsQuality"),
            ("note", "DetailNote"),
            setting("LabelTextureResolution", "LowResSlider", "ValueTextureResolution"),
            ("check", "CheckNoDynamicLOD")]),
        ("GUI:OptionsGroupImage", [
            ("check", "CheckClassicGraphics"),
            setting("LabelMSAA", "ComboBoxMSAA"),
            setting("LabelTextureFilter", "ComboBoxTextureFilter"),
            setting("LabelAnisotropy", "SliderAnisotropy", "ValueAnisotropy"),
            # the Lighting column is full with the water and cloud styles
            ("check", "CheckMapLight"),
            ("check", "CheckTeamGlow"),
            ("check", "CheckAtmosphere")]),
        ("GUI:OptionsGroupLighting", [
            setting("LabelLightingStyle", "ComboBoxLightingStyle"),
            setting("LabelBloom", "ComboBoxBloom"),
            ("check", "CheckGroundLighting"),
            ("check", "CheckHeatEffects"),
            setting("LabelWaterStyle", "ComboBoxWaterStyle"),
            setting("LabelCloudStyle", "ComboBoxCloudStyle")])]),

    ("PageEffects", [
        ("GUI:OptionsGroupShadows",
            [setting("LabelShadowQuality", "ComboBoxShadowQuality")]
            + [("check", name) for name in SHADOW_CHECKS]
            + [("check", "CheckVolumetricSmoke")]),
        ("GUI:OptionsGroupExtras", [
            ("check", "CheckShowProps"),
            ("check", "CheckSmoothWater"),
            ("check", "CheckWaterFoam"),
            ("check", "CheckGroundMarks"),
            ("check", "CheckHemiAmbient"),
            ("check", "CheckAmbientOcclusion"),
            ("check", "CheckInternalHdr"),
            ("check", "CheckExtraAnimations"),
            ("check", "CheckTreeSway"),
            ("check", "CheckBehindBuilding")]),
        ("GUI:OptionsGroupParticles", [
            setting("LabelParticleCap", "ParticleCapSlider", "ValueParticleCap"),
            setting("LabelSmoke", "ComboBoxSmoke"),
            ("check", "CheckParticleBounce"),
            ("check", "CheckSmokeFireLighting"),
            ("check", "CheckFirelight"),
            ("check", "CheckDebrisTrails"),
            ("check", "CheckWeather"),
            ("check", "CheckWaterSplashes")])]),

    ("PageAudio", [
        ("GUI:OptionsGroupVolume", [
            setting("LabelMasterVolume", "SliderMasterVolume", "ValueMasterVolume"),
            setting("SFXVolumeLabel", "SliderSFXVolume", "ValueSFXVolume"),
            setting("VoiceVolumeLabel", "SliderVoiceVolume", "ValueVoiceVolume"),
            setting("LabelAmbientVolume", "SliderAmbientVolume", "ValueAmbientVolume")]),
        ("GUI:OptionsGroupMusic", [
            setting("MusicVolumeLabel", "SliderMusicVolume", "ValueMusicVolume"),
            ("check", "CheckDynamicMusic")]),
        ("GUI:OptionsGroupOutput", [
            setting("LabelAudioDevice", "ComboBoxAudioDevice"),
            setting("LabelSpeakerType", "ComboBoxSpeakerType"),
            ("check", "CheckMuteInBackground"),
            ("check", "CheckLegacySound")])]),

    ("PageControls", [
        ("GUI:OptionsGroupCamera", [
            setting("ScrollSpeedLabel", "SliderScrollSpeed", "ValueScrollSpeed"),
            setting("LabelZoomSpeed", "SliderZoomSpeed", "ValueZoomSpeed"),
            ("check", "CheckIsometricCamera"),
            ("check", "CheckZoomToCursor"),
            ("check", "CheckStartAtMaxZoom"),
            ("check", "CheckSnapCamera45"),
            ("check", "CheckEdgeScrollWindowed"),
            ("check", "CheckCameraShake")]),
        ("GUI:OptionsGroupOrders", [
            setting("LabelDragTolerance", "SliderDragTolerance", "ValueDragTolerance"),
            ("check", "CheckAlternateMouse"),
            ("check", "Retaliation"),
            ("check", "CheckDoubleClickAttackMove"),
            ("check", "CheckFormationDrag"),
            ("check", "CheckBoxDrawAnchor"),
            ("check", "CheckBoxMoveAnchor")]),
        ("GUI:OptionsGroupPlacement", [
            ("check", "CheckGridBuild"),
            ("check", "CheckSnapBuild45"),
            ("check", "CheckSnapBuildNeighbour"),
            ("check", "CheckNudgeBuild"),
            ("check", "CheckBuildWithoutWorker")]),
        ("GUI:OptionsGroupInput", [
            ("check", "CheckChromaLighting")])]),

    ("PageInterface", [
        ("GUI:OptionsGroupHud", [
            ("check", "CheckNetBox")]),
        ("GUI:OptionsGroupMenus", [
            setting("LabelMenuTransitionSpeed", "SliderMenuTransitionSpeed", "ValueMenuTransitionSpeed")]),
        ("GUI:OptionsGroupSpeed", [
            ("check", "CheckTurboModeMissions")]),
        ("GUI:OptionsGroupBattlefield", [
            setting("LabelHealthBars", "ComboBoxHealthBars"),
            setting("LabelHealthBarColors", "ComboBoxHealthBarColors"),
            setting("LabelPlayerColors", "ComboBoxPlayerColors"),
            ("check", "CheckOrderLines"),
            ("check", "CheckEmptyBuildingPips")]),
        ("GUI:OptionsGroupLanguage", [
            setting("LabelLanguage", "ComboBoxLanguage")]),
        ("GUI:OptionsGroupReplays", [
            ("check", "CheckBoxSaveCamera"),
            ("check", "CheckBoxUseCamera")])]),

    ("PageNetwork", [
        ("GUI:OptionsGroupAddresses", [
            setting("StaticTextOnlineIpAddresses", "ComboBoxOnlineIP"),
            setting("StaticTextLANIpAddresses", "ComboBoxIP")]),
        ("GUI:OptionsGroupFirewall", [
            setting("StaticTextFirewallPortOverride", "TextEntryFirewallPortOverride"),
            ("button", "ButtonFirewallRefresh"),
            ("check", "CheckSendDelay")]),
        ("GUI:OptionsGroupProxy", [
            setting("StaticTextHTTPProxy", "TextEntryHTTPProxy")])]),
]

def item_height(item):
    if item[0] == "note":
        return note_height(item[1]) + 4
    return {"setting": SETTING_PITCH, "check": CHECK_PITCH, "button": BUTTON_PITCH}[item[0]]


def group_height(group):
    return HEADING_PITCH + sum(item_height(item) for item in group[1])


def flow(groups):
    """Groups into columns, in order: one a column while they fit three, otherwise the split into
    three runs whose tallest column is shortest.  A group never breaks across two columns."""
    if len(groups) <= len(COLUMNS):
        return [[group] for group in groups]
    best = None
    for first in range(1, len(groups) - 1):
        for second in range(first + 1, len(groups)):
            columns = [groups[:first], groups[first:second], groups[second:]]
            tallest = max(sum(group_height(g) for g in column) + GROUP_GAP * (len(column) - 1)
                          for column in columns)
            if best is None or tallest < best[0]:
                best = (tallest, columns)
    return best[1]


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


def make_note(label_template, name):
    """A readout in the combo boxes' 10 point lettering and a quieter grey, tall enough to wrap."""
    note = make_readout(label_template, name)
    note.set_prop("FONT", NOTE_FONT)
    note.set_prop("HEADERTEMPLATE", '"LabelRegular"')
    note.set_prop("TEXTCOLOR", NOTE_COLOR)
    return note


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


def drop_by_text(root, texts):
    """Delete every window drawing one of these strings, wherever it sits."""
    wanted = set(texts)
    for node in list(root.walk()):
        node.children = [child for child in node.children
                         if drawn_text(child) not in wanted]


def make_heading(templates, name, key):
    """A group's heading: a label in the buttons' lettering and the heading green."""
    heading = clone(templates[LABEL], _named(name))
    heading.children = []
    heading.put_prop("TEXT", '"%s"' % key)
    drop_prop(heading, "TOOLTIPTEXT")
    restyle(heading, templates["ButtonDefaults"], keys=("FONT", "HEADERTEMPLATE"))
    heading.set_prop("TEXTCOLOR", GROUP_HEADING_COLOR)
    heading.put_prop("STATICTEXTDATA", "CENTERED: 0")
    return heading


def make_list(templates, spec, columns):
    """A scrolling list cloned from the LAN lobby's chat, which takes the focus so the arrow keys
    walk it, and keeps no more rows than it is given."""
    name, left, top, width, height = spec
    listbox = clone(templates[HOTKEY_LIST_SOURCE], _named(name))
    # the lobby room is laid out at 1920x1080 now, and this page is 800x600
    listbox.rect = (left, top, left + width, top + height, 800, 600)
    listbox.set_prop("STATUS", "ENABLED")
    widths = "".join("COLUMNSWIDTH: %d, " % share for share in columns)
    listbox.set_prop("LISTBOXDATA", "LENGTH: 300, AUTOSCROLL: 0, SCROLLIFATEND: 0, AUTOPURGE: 0, SCROLLBAR: 1, "
                     "MULTISELECT: 0, COLUMNS: %d, %sFORCESELECT: 0" % (len(columns), widths))
    return listbox


def build_hotkeys_page(page, templates):
    for index, (key, left) in enumerate(HOTKEY_HEADINGS):
        heading = make_heading(templates, "GroupHeadingHotkeys%d" % index, key)
        heading.place(left - TEXT_NUDGE, HEADING_TOP, 344, ROW_HEIGHT)
        page.children.append(heading)
    page.children.append(make_list(templates, HOTKEY_COMMANDS, HOTKEY_COMMAND_COLUMNS))
    for name, left, top, width, height in (HOTKEY_FACTION, HOTKEY_OBJECT):
        combo = clone(templates[COMBO], _named(name))
        combo.place(left, top, width, height)
        drop_prop(combo, "TOOLTIPTEXT")
        combo.set_prop("COMBOBOXDATA", "ISEDITABLE: 0, MAXCHARS: 64, MAXDISPLAY: 12, ASCIIONLY: 0, LETTERSANDNUMBERS: 0")
        page.children.append(combo)
    page.children.append(make_list(templates, HOTKEY_BUTTONS, HOTKEY_BUTTON_COLUMNS))
    name, left, top, width, height = HOTKEY_NOTE
    note = make_note(templates[LABEL], name)
    note.place(left, top, width, height)
    page.children.append(note)
    for name, key, left in HOTKEY_BUTTON_ROW:
        button = clone(templates["ButtonFirewallRefresh"], _named(name))
        button.children = []
        button.place(left, HOTKEY_BUTTON_TOP, HOTKEY_BUTTON_WIDTH, ROW_HEIGHT)
        button.put_prop("TEXT", '"%s"' % key)
        button.put_prop("TOOLTIPTEXT", '"TOOLTIP:%s"' % name[len("Button"):])
        restyle(button, templates["ButtonDefaults"])
        page.children.append(button)


def build(layout):
    old = layout.find("OptionsMenuParentOld")
    templates = dict((name, layout.find(name))
                     for name in (CHECK, LABEL, COMBO, SLIDER, "VideoParent", "ButtonDefaults",
                                  "ButtonFirewallRefresh"))
    templates[HOTKEY_LIST_SOURCE] = wndlayout.load(LAN_LAYOUT).find(HOTKEY_LIST_SOURCE)

    for text, name in NAME_THE_UNNAMED:
        for node in layout.root.walk():
            # "unnamed" in this file means NAME = "OptionsMenu.wnd:", the layout and nothing after it
            if drawn_text(node) == text and not (node.name or "").split(":")[-1]:
                node.name = _named(name)

    drop_by_text(layout.root, HEADINGS)
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
    for name, tooltip in EA_PARKED:
        control = detach(old, name)
        control.set_prop("STATUS", "ENABLED+IMAGE+BORDER")   # the page's own check boxes', not HIDDEN
        control.put_prop("TOOLTIPTEXT", '"%s"' % tooltip)
        waiting[name] = control
    for node in detach(old, ADVANCED).walk():
        if short(node) in ADVANCED_KEEP:
            waiting[short(node)] = node
    for template, name, text in NEW_CONTROLS:
        waiting[name] = make_control(templates[template], name, text, 0, 0, 1, 1)
    for name in READOUTS:
        waiting[name] = make_readout(templates[LABEL], name)
    for name, _key, _count in NOTES:
        waiting[name] = make_note(templates[LABEL], name)
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

    page_bottom = PAGE[1] + PAGE[3]
    for page_name, groups in GROUP_LAYOUT:
        index = 0
        for column, stack in enumerate(flow(groups)):
            left = COLUMNS[column]
            heading_top = HEADING_TOP
            for heading_key, items in stack:
                heading_name = "GroupHeading%s%d" % (page_name[len("Page"):], index)
                index += 1
                waiting[heading_name] = make_heading(templates, heading_name, heading_key)
                put(page_name, heading_name, left - TEXT_NUDGE, heading_top, COLUMN_WIDTH, ROW_HEIGHT)

                top = heading_top + HEADING_PITCH
                for item in items:
                    if item[0] == "setting":
                        _kind, label, control, readout = item
                        put(page_name, label, left - TEXT_NUDGE, top, COLUMN_WIDTH, ROW_HEIGHT, LABEL)
                        if readout:
                            put(page_name, readout, left + READOUT_LEFT - TEXT_NUDGE, top + ROW_HEIGHT,
                                READOUT_WIDTH, ROW_HEIGHT, LABEL)
                        put(page_name, control, left, top + ROW_HEIGHT,
                            READOUT_LEFT - 8 if readout else COLUMN_WIDTH, ROW_HEIGHT)
                    elif item[0] == "check":
                        put(page_name, item[1], left, top, COLUMN_WIDTH, ROW_HEIGHT, CHECK)
                    elif item[0] == "note":
                        put(page_name, item[1], left - TEXT_NUDGE, top, COLUMN_WIDTH, note_height(item[1]))
                    else:
                        # a button on a page is lettered like the ones under the pages, the
                        # Generals face in capitals, not EA's 10 point Arial
                        put(page_name, item[1], left, top, 160, ROW_HEIGHT, "ButtonDefaults")
                    top += item_height(item)
                if top > page_bottom:
                    raise ValueError("%s column %d runs to %d, past the page's %d"
                                     % (page_name, column, top, page_bottom))
                heading_top = top + GROUP_GAP
    for page_name, name, column, top in HIDDEN_PARKED:
        put(page_name, name, COLUMNS[column], top, COLUMN_WIDTH, ROW_HEIGHT)
    build_hotkeys_page(pages["PageHotkeys"], templates)
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


# a row of TheGraphicsQualityTable: the control, its cost and its value at every step of the slider
_QUALITY_ROW = re.compile(r'\{\s*"(?P<control>\w+)",\s*(?P<cost>-?[\d.]+)f?,\s*\{(?P<values>[^}]*)\}\s*\}')


def read_quality_table():
    with open(CATALOG, "rb") as fp:
        text = fp.read().decode("latin-1")
    table = text[text.index("TheGraphicsQualityTable[] ="):]
    table = table[:table.index("};")]
    return [(match.group("control"), [int(value) for value in match.group("values").split(",")])
            for match in _QUALITY_ROW.finditer(table)]


def bound(text):
    """A catalog row's bound as written: a number, or an enum constant less one."""
    text = text.strip()
    return int(text) if text.lstrip("-").isdigit() else enum_count(text.split()[0]) - 1


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
            problems.append("%s names %s, which is not in %s" % (row["ini"], widget, os.path.basename(LAYOUT)))
        if not label:
            problems.append("%s has a widget and no label key" % row["ini"])
            continue
        if label not in keys:
            problems.append("%s label %s is not in Patch.str" % (row["ini"], label))

        tooltip = "TOOLTIP:%s" % setting_of(widget)
        if tooltip not in keys:
            problems.append("%s tooltip %s is not in Patch.str" % (row["ini"], tooltip))

        # a slider narrower than its row never reaches the row's ends, and says nothing about it
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
            for entry in range(int(row["lo"]), count):
                if "%s%d" % (label, entry) not in keys:
                    problems.append("%s entry %s%d is not in Patch.str" % (row["ini"], label, entry))

    for _control, key in TEXT_OVERRIDES:
        if key not in keys:
            problems.append("caption %s is not in Patch.str" % key)

    for name, tooltip in EA_PARKED:
        if tooltip not in keys:
            problems.append("%s tooltip %s is not in Patch.str" % (name, tooltip))
    for name in (READOUTS + GRAPHICS_CHECKS + MENU_CHECKS + MENU_COMBOS + [n for n, _k, _c in NOTES]
                 + [n for n, _t in EA_PARKED]):
        if name not in controls:
            problems.append("%s has no %s, which OptionsMenu.cpp fills in" % (os.path.basename(LAYOUT), name))
    for group in [g for _page, groups in GROUP_LAYOUT for g in groups]:
        if group[0] not in keys:
            problems.append("group heading %s is not in Patch.str" % group[0])
    hotkey_controls = [spec[0] for spec in (HOTKEY_COMMANDS, HOTKEY_FACTION, HOTKEY_OBJECT, HOTKEY_BUTTONS, HOTKEY_NOTE)]
    for name in hotkey_controls + [name for name, _key, _left in HOTKEY_BUTTON_ROW]:
        if name not in controls:
            problems.append("%s has no %s, which OptionsMenuHotkeys.cpp fills in" % (os.path.basename(LAYOUT), name))
    for key in [key for key, _left in HOTKEY_HEADINGS] + [key for _n, key, _l in HOTKEY_BUTTON_ROW] + HOTKEY_STRINGS:
        if key not in keys:
            problems.append("the Hotkeys page needs %s in Patch.str" % key)
    for name, key, count in NOTES:
        for entry in range(count):
            if "%s%d" % (key, entry) not in keys:
                problems.append("%s needs %s%d in Patch.str" % (name, key, entry))
    for name in MENU_COMBOS:
        for key in ("GUI:%s" % setting_of(name), "TOOLTIP:%s" % setting_of(name)):
            if key not in keys:
                problems.append("%s needs %s in Patch.str" % (name, key))
    # every row of the quality slider names a control on Graphics, Effects or Display, where the full
    # columns pushed Collapsing Buildings, and has a value for every step; a row one value short still
    # compiles, with a 0 in the last step
    steps = enum_count("GRAPHICS_QUALITY_STEPS")
    quality = read_quality_table()
    if len(quality) < 9:
        problems.append("only %d quality rows parsed, the regex has stopped matching" % len(quality))
    on_pages = set(short(child) for page in ("PageGraphics", "PageEffects", "PageDisplay")
                   for child in layout.find(page).children)
    for control, values in quality:
        if control not in on_pages:
            problems.append("quality row %s is not a control on the Graphics, Effects or Display page" % control)
        if len(values) != steps:
            problems.append("quality row %s has %d values for %d steps" % (control, len(values), steps))

    if ADVANCED in controls:
        problems.append("%s still carries %s; its controls are on the Graphics page"
                        % (os.path.basename(LAYOUT), ADVANCED))
    problems.extend(overlaps(layout))

    # Hazard appearance is a Graphics setting, independent of particle thickness.
    graphics = layout.find("PageGraphics")
    cloud_label = layout.find("LabelCloudStyle")
    cloud = layout.find("ComboBoxCloudStyle")
    if graphics and cloud_label and cloud:
        if cloud not in graphics.children or cloud_label not in graphics.children:
            problems.append("CloudStyle must be on the Graphics page")
        if cloud_label.rect[1] != cloud.rect[1] - ROW_HEIGHT:
            problems.append("CloudStyle label must sit above its combo box")

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
