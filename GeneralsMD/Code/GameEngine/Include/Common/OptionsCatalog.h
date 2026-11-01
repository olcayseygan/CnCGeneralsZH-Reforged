/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

#pragma once

// OptionsCatalog.h
//
// One table describing every scalar setting the player can change.
//
// EA spread a setting over five files: a GlobalData field, a default in GlobalData's constructor,
// a hand-written OptionPreferences::getXxx that parses one Options.ini key, a copy line in
// GameLODManager::setOptionPreferences, a widget in OptionsMenu.wnd, and three more places in
// OptionsMenu.cpp - the NameKey, the code that fills the widget on the way in and the code that
// reads it back on the way out.  Ten edits for one checkbox, and every one of them a chance to
// forget the fourth.  The FPSLimit key is what forgetting looks like: OptionsMenu.cpp still
// carries the commented-out write, so the checkbox exists and nothing persists.
//
// So the description of a setting lives here instead, one row per setting, and the four passes
// that used to be hand-written per setting are loops over this table: read Options.ini, write
// Options.ini, fill the menu, read the menu back.  Adding a setting is a row, a GlobalData field
// and whatever code actually reads that field.
//
// What is deliberately NOT here: resolution, the detail preset, the LAN and online IP combo boxes
// and the firewall entries.  None of them is a scalar with a range - picking a resolution rebuilds
// the shell, the detail preset writes eleven other settings - and forcing them through a generic
// table would mean a table row carrying a function that does something entirely unlike the others.
// They keep their bespoke code in OptionsMenu.cpp.

#ifndef __OPTIONSCATALOG_H_
#define __OPTIONSCATALOG_H_

#include "Lib/BaseType.h"

// UserPreferences, not OptionPreferences: the catalog only ever does map lookups, and taking the
// base class keeps it clear of OptionsMenu.cpp - which is where OptionPreferences' constructor
// lives, and which drags in the shell, the audio manager and GameSpy behind it.
class UserPreferences;
class AsciiString;

//-----------------------------------------------------------------------------
/** How a setting is stored in Options.ini and what kind of control shows it. */
enum OptionKind
{
	OPTION_BOOL,		///< "yes" / "no" in Options.ini, a check box in the menu
	OPTION_INT,			///< a decimal clamped to [lo,hi], a slider in the menu
	OPTION_ENUM,		///< a decimal in [0,hi], a combo box whose entries are labelKey with 0..hi appended
};

//-----------------------------------------------------------------------------
/** When a changed value actually reaches the player.
	*
	* The menu shows this: a setting the device has to be rebuilt for cannot pretend it took effect
	* the moment Accept was pressed, and one that is only read while the process starts up has to say
	* so or the player changes it, sees nothing happen and changes it back. */
enum OptionApply
{
	APPLY_LIVE,					///< whoever reads the GlobalData field reads it every frame; nothing else to do
	APPLY_DEVICE_RESET,	///< the D3D device has to be recreated before it shows
	APPLY_RESTART,			///< only read once, before the engine exists - see EarlyOptions.h
	APPLY_SHELL_REBUILD,	///< read as the menus are laid out: Accept builds the shell again, as a resolution change does
	APPLY_NEXT_MAP,			///< read when a drawable is made, so what already stands keeps the old value until the next map
};

//-----------------------------------------------------------------------------
/** One setting.
	*
	* `get` and `set` are the whole coupling to GlobalData.  A member pointer would be tidier but the
	* fields are a mix of Bool, Int and enum, and two one-line functions cost less than the cast that
	* would take. */
struct OptionDef
{
	const char*	iniKey;			///< the Options.ini key, and the name the tests match on
	const char*	widgetName;	///< "OptionsMenu.wnd:CheckXxx", or "" while the setting has no control
	const char*	labelKey;		///< CSF label; OPTION_ENUM appends "0".."N" to it for the combo entries
	OptionKind	kind;
	OptionApply	apply;
	Int					lo;					///< inclusive low bound; 0 for BOOL and ENUM
	Int					hi;					///< inclusive high bound; 1 for BOOL, the last index for ENUM
	Int					(*get)( void );				///< the live value out of TheGlobalData
	void				(*set)( Int value );	///< write it back into TheWritableGlobalData
};

//-----------------------------------------------------------------------------
// The table itself, and its length.  Terminated by a row whose iniKey is NULL as well, so a walk
// can use either.
extern const OptionDef TheOptionCatalog[];
extern const Int TheOptionCatalogCount;

/** The row with this Options.ini key, or NULL. */
extern const OptionDef *findOptionDef( const char *iniKey );

/** Clamp a raw value the way the row says. */
extern Int clampOptionValue( const OptionDef& def, Int value );

/** Typed text -> a value for this row, for the console's "set".  Stricter than reading Options.ini:
	* a bool takes yes/no, true/false, on/off, y/n, t/f or 1/0, a number has to be whole and inside
	* [lo,hi], and anything else is FALSE with `value` untouched rather than clamped or read as "no". */
extern Bool parseOptionText( const OptionDef& def, const char *text, Int *value );

/** A value as Options.ini stores it: "yes"/"no" for a bool, the clamped decimal otherwise. */
extern AsciiString formatOptionValue( const OptionDef& def, Int value );

//-----------------------------------------------------------------------------
// Multisampling is stored as an index rather than a sample count, because that is what a combo box
// hands back and because the counts are not a range: 3x and 5x do not exist.
enum { OPTION_MSAA_LEVEL_COUNT = 5 };

/** Samples per pixel for a stored MSAA level.  0 is off. */
extern unsigned msaaSamplesForLevel( Int level );

/** The level that asks for this many samples, rounding a number in between downwards. */
extern Int msaaLevelForSamples( unsigned samples );

//-----------------------------------------------------------------------------
// Glow (the Bloom key) is a strength in percent the renderer wants, and "Bloom = 60" is not a
// question a player can answer.  So Options.ini stores a level, the menu offers those levels by
// name, and the percentage each level stands for is in OptionsCatalog.cpp.  What glows is not a
// setting: fire, explosions, muzzle flashes and lasers do and the painted world does not, at every
// level (W3DShaderManager.cpp, glowTuning).
enum { BLOOM_LEVEL_COUNT = 5 };						///< off, low, medium, high, ultra

// Texture filtering is a mode too - bilinear, trilinear, anisotropic - and the combo box offers it
// by those names.  The number in Options.ini is the same 0..2 it was when the key had no control.
enum { TEXTURE_FILTER_MODE_COUNT = 3 };

// Smoke is the -smoke switch's thickness offered as original, thick and very thick.  Options.ini
// stores the level; the thickness each level stands for is in OptionsCatalog.cpp.
enum { SMOKE_LEVEL_COUNT = 3 };

// Classic is the original surface; Modern is the SDL wave and optical surface.
enum { WATER_STYLE_COUNT = 2 };
// Classic is the original hazard effect; Volumetric is the Linux toxin/radiation cloud.
enum { CLOUD_STYLE_COUNT = 2 };

// Shadow quality is the size of the sun's depth map, low, medium and high: 1024, 2048 and 4096
// texels a side (W3DVolumetricShadow.cpp, renderShadowMap).  Medium is the size the map always had.
enum { SHADOW_QUALITY_COUNT = 3 };

// What colours a health bar: 0 the health, green through red, 1 the owner's player colour.
enum { HEALTH_BAR_COLORS_COUNT = 2 };

//-----------------------------------------------------------------------------
// The Performance / Quality slider on the Graphics page.  One row per graphics setting the Graphics
// and Effects pages carry, holding what that setting's control shows at each step of the slider:
// step 0 is the fastest picture, the last step the best looking, and a setting that costs little
// frame time turns up at an earlier step than one that costs much.  The four detail presets are
// steps on it (TheGraphicsQualityAnchors), so GameLODManager::init takes the presets' values for the
// GameLOD.ini fields from here too.  A new graphics setting is one more row.
//
// Not in it: the resolution, the window, the monitor, VSync, gamma and ClassicGraphics, which are
// the screen and a choice of art rather than a price paid in frame time, the behind-buildings
// marker, which is there to read the battlefield, and trees, which GameLogic reads as a map loads.
// A setting the simulation reads stays out of the slider.
enum { GRAPHICS_QUALITY_STEPS = 11 };

struct GraphicsQualityRow
{
	const char	*control;		///< the control's name in OptionsMenu.wnd, without "OptionsMenu.wnd:"
	Real				costMs;			///< Direct3D 11 GPU milliseconds a frame the row's last step costs over its first, 0 under the noise
	Int					values[ GRAPHICS_QUALITY_STEPS ];	///< per step: a check box 0 or 1, a combo box its entry, a slider its position
};

extern const GraphicsQualityRow TheGraphicsQualityTable[];
extern const Int TheGraphicsQualityTableCount;

/** The slider's step for each detail preset, indexed by StaticGameLODLevel: Low, Medium, High, Ultra. */
extern const Int TheGraphicsQualityAnchors[];

/** The row for this control, or NULL. */
extern const GraphicsQualityRow *findGraphicsQualityRow( const char *control );

/** A row the fork added to the picture, as against one of EA's detail settings.  While Classic
	* Graphics is ticked the slider and the presets move only EA's, and unticking it fills these in. */
extern Bool graphicsQualityRowIsReforged( const GraphicsQualityRow &row );

/** Options.ini -> TheWritableGlobalData, for every row.  A key that is absent leaves the field at
	* whatever GlobalData's constructor put there, which is off for everything the fork added: a detail
	* preset named in the file, or picked for a first run, sets only the GameLOD fields.  The rest of
	* its column reaches a row through the menu, which saves what it shows. */
extern void loadOptionsFromPreferences( UserPreferences& pref );

/** What this row holds with no line in Options.ini, the value a first run has.  The menu's Defaults. */
extern Int optionDefaultValue( const OptionDef& def );

/** One log line with every row's value as it stands, for a run's log to say which setup it drew. */
extern void logOptionCatalog( void );

/** TheGlobalData -> Options.ini, for every row.  The options menu's Accept writes only the rows the
	* player changed instead, so a value the command line or ClassicGraphics put in GlobalData for one
	* run does not end up on disk. */
extern void saveOptionsToPreferences( UserPreferences& pref );

#endif // __OPTIONSCATALOG_H_
