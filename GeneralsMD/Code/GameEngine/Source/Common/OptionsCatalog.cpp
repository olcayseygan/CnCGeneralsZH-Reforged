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

// OptionsCatalog.cpp
//
// The table described in OptionsCatalog.h, and the four passes over it.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/OptionsCatalog.h"
#include "Common/GlobalData.h"
#include "Common/UserPreferences.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/View.h"

//-----------------------------------------------------------------------------
// The accessors.  Each is two lines and exists only because a member pointer cannot span Bool and
// Int fields without a cast, and a cast through a struct offset is the kind of thing that keeps
// working right up until somebody reorders GlobalData.
//-----------------------------------------------------------------------------

#define OPTION_BOOL_ACCESSORS( field )																												\
	static Int get_##field( void ) { return TheGlobalData->field ? 1 : 0; }											\
	static void set_##field( Int value ) { TheWritableGlobalData->field = (value != 0); }

#define OPTION_INT_ACCESSORS( field )																													\
	static Int get_##field( void ) { return TheGlobalData->field; }															\
	static void set_##field( Int value ) { TheWritableGlobalData->field = value; }

OPTION_BOOL_ACCESSORS( m_useCameraConstraints )

static Int get_m_cameraBoundaryMargin( void ) { return TheGlobalData->m_cameraBoundaryMargin; }
static void set_m_cameraBoundaryMargin( Int value )
{
	TheWritableGlobalData->m_cameraBoundaryMargin = value;
	if (TheTacticalView)
	{
		TheTacticalView->forceCameraConstraintRecalc();
		TheTacticalView->forceRedraw();
	}
}
OPTION_BOOL_ACCESSORS( m_edgeScrollInWindowedMode )
OPTION_BOOL_ACCESSORS( m_snapCameraRotateTo45 )
OPTION_BOOL_ACCESSORS( m_zoomToCursor )
OPTION_BOOL_ACCESSORS( m_isometricCamera )
OPTION_BOOL_ACCESSORS( m_formationDrag )
OPTION_BOOL_ACCESSORS( m_showAllyCursors )
OPTION_BOOL_ACCESSORS( m_chromaLighting )
OPTION_INT_ACCESSORS( m_bloomIntensity )
OPTION_INT_ACCESSORS( m_bloomThreshold )
OPTION_INT_ACCESSORS( m_menuTransitionSpeed )
OPTION_INT_ACCESSORS( m_textureFilterMode )
OPTION_INT_ACCESSORS( m_anisotropyLevel )
OPTION_INT_ACCESSORS( m_windowMode )
OPTION_INT_ACCESSORS( m_msaaLevel )
OPTION_BOOL_ACCESSORS( m_vsync )
OPTION_BOOL_ACCESSORS( m_classicGraphics )
OPTION_INT_ACCESSORS( m_healthBarMode )
OPTION_INT_ACCESSORS( m_playerColorScheme )
OPTION_INT_ACCESSORS( m_textLanguage )
OPTION_INT_ACCESSORS( m_inputScheme )
OPTION_BOOL_ACCESSORS( m_wasdCamera )
OPTION_BOOL_ACCESSORS( m_showOrderLines )
OPTION_BOOL_ACCESSORS( m_useShadowVolumesForSkins )
OPTION_BOOL_ACCESSORS( m_shadowsForProjectiles )
OPTION_BOOL_ACCESSORS( m_shadowsForProps )
OPTION_BOOL_ACCESSORS( m_shadowsForParticles )
OPTION_BOOL_ACCESSORS( m_particleGroundBounce )
OPTION_BOOL_ACCESSORS( m_showSkillStrip )
OPTION_BOOL_ACCESSORS( m_showSuperweaponStrip )

//-----------------------------------------------------------------------------
static const unsigned TheMsaaSamples[ OPTION_MSAA_LEVEL_COUNT ] = { 0, 2, 4, 8, 16 };

unsigned msaaSamplesForLevel( Int level )
{
	if( level <= 0 || level >= OPTION_MSAA_LEVEL_COUNT )
		return 0;
	return TheMsaaSamples[ level ];
}

//-----------------------------------------------------------------------------
Int msaaLevelForSamples( unsigned samples )
{
	Int level = 0;
	for( Int i = 1; i < OPTION_MSAA_LEVEL_COUNT; ++i )
	{
		if( TheMsaaSamples[ i ] <= samples )
			level = i;
	}
	return level;
}

//-----------------------------------------------------------------------------
// Bloom, as levels.  GlobalData keeps the two percentages the shader reads and GameData.ini keeps
// setting them directly, so nothing downstream of here knows the levels exist; what changed is
// Options.ini and the menu, which now hold the index of one of these entries.
//
// The strengths are spread over the useful half of the range: past about 85 the whole picture
// washes out, and below 30 the effect is only visible on the muzzle flashes.  The thresholds run
// the other way round on purpose - a low number means more of the picture is bright enough to
// glow - so the entry a player picks reads as "how much glows", which is the thing they can see.
static const Int TheBloomPercents[ BLOOM_LEVEL_COUNT ] = { 0, 35, 60, 85 };
static const Int TheBloomThresholdPercents[ BLOOM_THRESHOLD_LEVEL_COUNT ] = { 85, 65, 45 };

static Int clampLevel( Int level, Int count )
{
	if( level < 0 )
		return 0;
	if( level >= count )
		return count - 1;
	return level;
}

/** The level whose percentage is nearest the one GlobalData is holding.  It has to be nearest
	* rather than exact: GameData.ini sets these fields to any number it likes, and the combo box
	* still has to show something rather than falling back to the first entry and reading as off. */
static Int nearestLevel( const Int *percents, Int count, Int percent )
{
	Int best = 0;
	for( Int i = 1; i < count; ++i )
	{
		const Int here = percents[ i ] > percent ? percents[ i ] - percent : percent - percents[ i ];
		const Int sofar = percents[ best ] > percent ? percents[ best ] - percent : percent - percents[ best ];
		if( here < sofar )
			best = i;
	}
	return best;
}

// Bare -smoke is 2, and 4 is where the thicker smoke was measured at 0.7 ms a frame over a burning
// column.  The switch goes to 8; a level for that would be a slider position nobody could see
// through.
static const Int TheSmokeThicknesses[ SMOKE_LEVEL_COUNT ] = { 0, 2, 4 };

static Int get_smokeLevel( void )
{
	return nearestLevel( TheSmokeThicknesses, SMOKE_LEVEL_COUNT, REAL_TO_INT( TheGlobalData->m_smokeThickness ) );
}

static void set_smokeLevel( Int level )
{
	TheWritableGlobalData->m_smokeThickness = (Real)TheSmokeThicknesses[ clampLevel( level, SMOKE_LEVEL_COUNT ) ];
}

static Int get_bloomLevel( void )
{
	return nearestLevel( TheBloomPercents, BLOOM_LEVEL_COUNT, TheGlobalData->m_bloomIntensity );
}

static void set_bloomLevel( Int level )
{
	TheWritableGlobalData->m_bloomIntensity = TheBloomPercents[ clampLevel( level, BLOOM_LEVEL_COUNT ) ];
}

static Int get_bloomThresholdLevel( void )
{
	return nearestLevel( TheBloomThresholdPercents, BLOOM_THRESHOLD_LEVEL_COUNT,
											 TheGlobalData->m_bloomThreshold );
}

static void set_bloomThresholdLevel( Int level )
{
	TheWritableGlobalData->m_bloomThreshold =
		TheBloomThresholdPercents[ clampLevel( level, BLOOM_THRESHOLD_LEVEL_COUNT ) ];
}

//-----------------------------------------------------------------------------
// The catalog.
//
// widgetName and labelKey are empty for every row that has no control in OptionsMenu.wnd yet.
// The menu passes skip those rows, so a setting can live here - loaded, saved, clamped - before it
// has anywhere to be clicked.
//
// A widget name is the layout file plus the control name, which is what nameToKey wants; OPT_WND
// spells the layout once instead of seventeen times.
//-----------------------------------------------------------------------------
#define OPT_WND( control )	"OptionsMenu.wnd:" control

const OptionDef TheOptionCatalog[] =
{
	// iniKey, widgetName, labelKey, kind, apply, lo, hi, get, set

	// The camera and mouse habits below have no control in OptionsMenu.wnd any more.  They are
	// not gone: an empty widgetName only makes the menu passes skip the row, so Options.ini still
	// loads, clamps and saves each one and a player who wants the old behaviour can put the key
	// back by hand.  The defaults in GlobalData are what everybody else gets.

	{ "UseCameraConstraints", "", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_useCameraConstraints, set_m_useCameraConstraints },

	{ "CameraBoundaryMargin", "", "",
		OPTION_INT, APPLY_LIVE, 0, 1000,
		get_m_cameraBoundaryMargin, set_m_cameraBoundaryMargin },

	// Retail refuses to edge-scroll in a window because the cursor can legitimately sit on the
	// border while you reach for something else; on a second monitor, or borderless, that is
	// exactly the behaviour you want back.
	{ "EdgeScrollInWindowedMode",	"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_edgeScrollInWindowedMode, set_m_edgeScrollInWindowedMode },

	{ "SnapCameraRotateTo45",			"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_snapCameraRotateTo45, set_m_snapCameraRotateTo45 },

	// MiddleMousePans used to sit here.  The middle button is the only camera drag there is now, so
	// there is nothing left to choose: it pans, and Ctrl turns the same drag into a rotate.

	// Back on Options > Controls: players split on whether the wheel should chase the cursor, and
	// Legacy zooms on the middle of the screen whatever this says.
	{ "ZoomToCursor",							OPT_WND( "CheckZoomToCursor" ), "GUI:ZoomToCursor",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_zoomToCursor, set_m_zoomToCursor },

	// The battlefield from far off down a narrow cone, so a unit is the same size wherever it
	// stands on the screen.  The heading stays the player's.  On Options > Controls.
	{ "IsometricCamera",					OPT_WND( "CheckIsometricCamera" ), "GUI:IsometricCamera",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_isometricCamera, set_m_isometricCamera },

	// A right drag over the ground spreads the selection along the line drawn instead of sending
	// everyone to one point.  On by default - the right button stopped scrolling, so the drag was
	// free - and here for anyone who would rather a slipped click did nothing at all.
	{ "FormationDrag",						"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_formationDrag, set_m_formationDrag },

	// In a network game, each ally's mouse is drawn on the map with their name over it and a patch
	// of their colour under it.  Off and neither end of it happens: nothing is sent and nothing is
	// drawn, so a player who does not want to be watched turns it off on their own machine.
	{ "ShowAllyCursors",					"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_showAllyCursors, set_m_showAllyCursors },

	// The match on Razer hardware: the command bar on the letter keys, power on the digits,
	// superweapons on the numpad, money on the mousepad.  Only during a match; off, and in every
	// menu, the keyboard goes back to whatever Synapse wants to do with it.  On Options > Controls.
	{ "ChromaLighting",						OPT_WND( "CheckChromaLighting" ), "GUI:ChromaLighting",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_chromaLighting, set_m_chromaLighting },

	// Percent of the speed the menu slides and fades were authored at. 100 is what the artists
	// drew; higher gets you through the shell faster, and nothing about a menu animation is worth
	// waiting for on the four hundredth launch.
	{ "MenuTransitionSpeed",			"", "",
		OPTION_INT, APPLY_LIVE, 25, 400,
		get_m_menuTransitionSpeed, set_m_menuTransitionSpeed },

	// 0 bilinear, 1 trilinear, 2 anisotropic. Retail shipped bilinear with point mip selection,
	// which is a 2003 fill-rate budget and is why distant ground used to shimmer; 2 is the default
	// here. The filter table is built when the device is made, so this needs a device reset.  A combo
	// box on the Graphics page; stored as the same decimal the hand-edited key always was.
	{ "TextureFilter",						OPT_WND( "ComboBoxTextureFilter" ), "GUI:TextureFilter",
		OPTION_ENUM, APPLY_DEVICE_RESET, 0, TEXTURE_FILTER_MODE_COUNT - 1,
		get_m_textureFilterMode, set_m_textureFilterMode },

	// Samples anisotropic filtering may take. 0 means whatever the card offers, capped at 16, and
	// asking for more than the card has still gets you the card's answer. Only read when the filter
	// above is anisotropic.  The slider beside the filter box.
	{ "Anisotropy",								OPT_WND( "SliderAnisotropy" ), "GUI:Anisotropy",
		OPTION_INT, APPLY_DEVICE_RESET, 0, 16,
		get_m_anisotropyLevel, set_m_anisotropyLevel },

	// Eight rows used to sit here: grid and nudge build placement, snap-to-45 building rotation, the
	// placement range ring, workers returning to supply, detailed build tooltips, the HUD overlay
	// and replay archiving.  Every one of them is now on for everybody, decided in GlobalData's
	// constructor, so there is nothing left to load or save.  Gameplay is health bars and nothing
	// else.

	// Off, subtle, normal, strong.  Off is the default, which is what GameData.ini says: the game's
	// artwork has no HDR range in it, so how much glow looks right is a matter of taste rather than
	// something to pick on the player's behalf.  The key is "Bloom", not "BloomLevel" - it predates
	// the levels and an Options.ini in the wild already spells it this way.
	{ "Bloom",										OPT_WND( "ComboBoxBloom" ), "GUI:Bloom",
		OPTION_ENUM, APPLY_LIVE, 0, BLOOM_LEVEL_COUNT - 1,
		get_bloomLevel, set_bloomLevel },

	// How much of the picture is bright enough to glow at all.  Three answers, because the number
	// underneath is a brightness that runs backwards and nobody could be expected to guess that.
	{ "BloomThreshold",						OPT_WND( "ComboBoxBloomThreshold" ), "GUI:BloomThreshold",
		OPTION_ENUM, APPLY_LIVE, 0, BLOOM_THRESHOLD_LEVEL_COUNT - 1,
		get_bloomThresholdLevel, set_bloomThresholdLevel },

	// Fullscreen, borderless or windowed.  The old Windowed flag in GameData.ini seeds this and is
	// then derived back from it, so the device layer keeps reading the boolean it always read.
	// The window's style at startup is still settled by CreateWindow in WinMain, which runs before
	// the engine exists and reads this key itself through EarlyOptions.h; changing it while the game
	// is up restyles that window and rebuilds the device (W3DDisplay::setDisplayMode).
	{ "WindowMode",								OPT_WND( "ComboBoxWindowMode" ), "GUI:WindowMode",
		OPTION_ENUM, APPLY_DEVICE_RESET, 0, WINDOW_MODE_COUNT - 1,
		get_m_windowMode, set_m_windowMode },

	// Multisampling, as an index into 0/2/4/8/16 rather than a sample count - the device offers
	// those and nothing between them, so a slider would spend most of its travel on values that
	// silently round down.
	{ "MSAA",											OPT_WND( "ComboBoxMSAA" ), "GUI:MSAA",
		OPTION_ENUM, APPLY_DEVICE_RESET, 0, OPTION_MSAA_LEVEL_COUNT - 1,
		get_m_msaaLevel, set_m_msaaLevel },

	// Wait for the monitor.  Off is what the uncapped picture shipped as: D3D9 honours the
	// presentation interval in a window, so leaving this on would pin a windowed game to the
	// refresh the way fullscreen used to be pinned.  The device has to be reset; Accept does that.
	{ "VSync",										OPT_WND( "CheckVSync" ), "GUI:VSync",
		OPTION_BOOL, APPLY_DEVICE_RESET, 0, 1,
		get_m_vsync, set_m_vsync },

	// Classic or Reforged.  Classic is the picture the game shipped with: its own textures, EA's
	// ground tile, no normal maps, the stencil shadows and no post effects.  The upscaled archives
	// are mounted before GlobalData exists, so Win32BIGFileSystem reads this key out of Options.ini
	// itself, and everything else takes it once while the device starts.
	{ "ClassicGraphics",					OPT_WND( "CheckClassicGraphics" ), "GUI:ClassicGraphics",
		OPTION_BOOL, APPLY_RESTART, 0, 1,
		get_m_classicGraphics, set_m_classicGraphics },

	// Who wears a health bar: everyone, everyone hurt, only the selection, or nobody.  Read every
	// frame by the drawable that is about to draw one, so changing it shows immediately.
	{ "HealthBars",								OPT_WND( "ComboBoxHealthBars" ), "GUI:HealthBars",
		OPTION_ENUM, APPLY_LIVE, 0, HEALTH_BAR_MODE_COUNT - 1,
		get_m_healthBarMode, set_m_healthBarMode },

	// Whose colour a player is drawn in.  Purely local: the match still agrees on the lobby's
	// colours and this only changes what this screen puts on top of them, so two people in the same
	// game can run different schemes.
	{ "PlayerColors",							OPT_WND( "ComboBoxPlayerColors" ), "GUI:PlayerColors",
		OPTION_ENUM, APPLY_LIVE, 0, PLAYER_COLOR_SCHEME_COUNT - 1,
		get_m_playerColorScheme, set_m_playerColorScheme },

	// Modern or Legacy.  Legacy is the mouse and the keys the game shipped with and switches off what
	// this fork added to both.  Every click and key asks, so it changes the moment Accept is pressed,
	// and it is local: two players in one match can give orders two different ways.
	// The line from each selected unit to where it is going, with every point of a shift queue after
	// it.  The list is rebuilt from the units every frame, so turning it off takes the lines away at
	// once and turning it back on shows the orders already given.
	{ "OrderLines",								OPT_WND( "CheckOrderLines" ), "GUI:OrderLines",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_showOrderLines, set_m_showOrderLines },

	{ "InputScheme",							OPT_WND( "ComboBoxInputScheme" ), "GUI:InputScheme",
		OPTION_ENUM, APPLY_LIVE, 0, INPUT_SCHEME_COUNT - 1,
		get_m_inputScheme, set_m_inputScheme },

	// W A S D on the camera, and the keys they held moved to F G H J K.  Modern only: the box greys
	// while Legacy is picked, and a tick saved under Modern is kept but ignored until Modern is back.
	// Every key asks, and Accept rebuilds the command bar's letters, so it changes at once.
	{ "WasdCamera",								OPT_WND( "CheckWasdCamera" ), "GUI:WasdCamera",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_wasdCamera, set_m_wasdCamera },

	// Which language the words are in.  English is the string table the game shipped with, and every
	// other entry is a translation laid over it, so a line the translation lacks stays English.  The
	// table is built once while the game starts: a new language is on screen from the next launch.
	// Purely local, like the colours above - two players in one match can read it in two languages.
	{ "TextLanguage",							OPT_WND( "ComboBoxLanguage" ), "GUI:Language",
		OPTION_ENUM, APPLY_RESTART, 0, TEXT_LANGUAGE_COUNT - 1,
		get_m_textLanguage, set_m_textLanguage },

	// The Effects page.  These four sat in GameData.ini with no control, all on.  Each is read when the
	// thing that casts the shadow is made, so a change shows on the next map rather than on the units
	// already standing there - except smoke clouds, which ask every frame.
	{ "UseShadowVolumesForSkins",	OPT_WND( "CheckInfantryShadows" ), "GUI:InfantryShadows",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_useShadowVolumesForSkins, set_m_useShadowVolumesForSkins },

	{ "ShadowsForProjectiles",		OPT_WND( "CheckProjectileShadows" ), "GUI:ProjectileShadows",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_shadowsForProjectiles, set_m_shadowsForProjectiles },

	{ "ShadowsForProps",					OPT_WND( "CheckPropShadows" ), "GUI:PropShadows",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_shadowsForProps, set_m_shadowsForProps },

	{ "ShadowsForParticles",			OPT_WND( "CheckParticleShadows" ), "GUI:ParticleShadows",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_shadowsForParticles, set_m_shadowsForParticles },

	// -smoke and -particlebounce as settings.  Both are spent on the particle system templates while
	// the particle manager starts, so they wait for the next launch.  The command line is parsed after
	// this catalog loads, so either switch still wins for the one run it is given.
	{ "Smoke",										OPT_WND( "ComboBoxSmoke" ), "GUI:Smoke",
		OPTION_ENUM, APPLY_RESTART, 0, SMOKE_LEVEL_COUNT - 1,
		get_smokeLevel, set_smokeLevel },

	{ "ParticleBounce",						OPT_WND( "CheckParticleBounce" ), "GUI:ParticleBounce",
		OPTION_BOOL, APPLY_RESTART, 0, 1,
		get_m_particleGroundBounce, set_m_particleGroundBounce },

	// The strips over the battlefield while watching a match.  They have no control in the options
	// menu: a spectator switches them from the drop-down in the top left corner, which writes them
	// back itself.  Playing, every strip is drawn whatever these say.  The production queues have no
	// switch: watching, they are on the Tab scoreboard, and playing, they are always drawn.
	{ "ShowSkillStrip",						NULL, "GUI:HudSkillStrip",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_showSkillStrip, set_m_showSkillStrip },

	{ "ShowSuperweaponStrip",			NULL, "GUI:HudSuperweaponStrip",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_showSuperweaponStrip, set_m_showSuperweaponStrip },

	{ NULL, NULL, NULL, OPTION_BOOL, APPLY_LIVE, 0, 0, NULL, NULL }
};

const Int TheOptionCatalogCount = (sizeof( TheOptionCatalog ) / sizeof( TheOptionCatalog[ 0 ] )) - 1;

//-----------------------------------------------------------------------------
const OptionDef *findOptionDef( const char *iniKey )
{
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
		if( stricmp( TheOptionCatalog[ i ].iniKey, iniKey ) == 0 )
			return &TheOptionCatalog[ i ];

	return NULL;
}

//-----------------------------------------------------------------------------
Int clampOptionValue( const OptionDef& def, Int value )
{
	if( value < def.lo )
		return def.lo;
	if( value > def.hi )
		return def.hi;
	return value;
}

//-----------------------------------------------------------------------------
/** Read one stored string.
	*
	* The option getters this replaces tested `stricmp(s, "yes") == 0` and called everything else
	* false, so a hand-edited `ZoomToCursor = true` silently did nothing.  UserPreferences::getBool
	* has always been the lenient one; the catalog follows it.  Writing still produces "yes"/"no". */
static Int parseOptionValue( const OptionDef& def, const AsciiString& stored )
{
	if( def.kind == OPTION_BOOL )
	{
		const char *s = stored.str();
		const Bool on = stricmp( s, "yes" ) == 0
									|| stricmp( s, "true" ) == 0
									|| stricmp( s, "on" ) == 0
									|| stricmp( s, "y" ) == 0
									|| stricmp( s, "t" ) == 0
									|| stricmp( s, "1" ) == 0;
		return on ? 1 : 0;
	}

	return clampOptionValue( def, atoi( stored.str() ) );
}

//-----------------------------------------------------------------------------
static AsciiString formatOptionValue( const OptionDef& def, Int value )
{
	if( def.kind == OPTION_BOOL )
		return AsciiString( value ? "yes" : "no" );

	AsciiString out;
	out.format( "%d", clampOptionValue( def, value ) );
	return out;
}

//-----------------------------------------------------------------------------
void loadOptionsFromPreferences( UserPreferences& pref )
{
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];

		UserPreferences::const_iterator it = pref.find( AsciiString( def.iniKey ) );
		if( it == pref.end() )
			continue;	// no key, so keep whatever GameData.ini's default put in GlobalData

		def.set( parseOptionValue( def, it->second ) );
	}
}

//-----------------------------------------------------------------------------
void saveOptionsToPreferences( UserPreferences& pref )
{
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];
		pref[ AsciiString( def.iniKey ) ] = formatOptionValue( def, def.get() );
	}
}
