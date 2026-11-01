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

// OptionsCatalog.cpp
//
// The table described in OptionsCatalog.h, and the four passes over it.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/OptionsCatalog.h"
#include "Common/AudioAffect.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "Common/UserPreferences.h"
#include "GameClient/Mouse.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"

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
OPTION_BOOL_ACCESSORS( m_gridBuildPlacement )
OPTION_BOOL_ACCESSORS( m_snapBuildPlacementTo45 )
OPTION_BOOL_ACCESSORS( m_snapBuildToNeighbour )
OPTION_BOOL_ACCESSORS( m_nudgeBuildPlacement )
OPTION_BOOL_ACCESSORS( m_zoomToCursor )
OPTION_INT_ACCESSORS( m_zoomSpeed )
OPTION_BOOL_ACCESSORS( m_isometricCamera )
OPTION_BOOL_ACCESSORS( m_cameraShake )
OPTION_BOOL_ACCESSORS( m_startAtMaxZoom )

// This catalog loads before there is a mouse, so the value waits in GlobalData and Mouse::parseIni
// takes it from there.  The menu changes it with the mouse up, which is the branch below.
static Int get_m_dragTolerance( void ) { return TheGlobalData->m_dragTolerance; }
static void set_m_dragTolerance( Int value )
{
	TheWritableGlobalData->m_dragTolerance = value;
	if (TheMouse)
		TheMouse->m_dragTolerance = (UnsignedInt)value;
}
// The catalog loads before there is an audio manager, which takes the value from GlobalData in its
// init; the menu's Accept pushes it in from here.
static Int get_m_ambientVolume( void ) { return TheGlobalData->m_ambientVolume; }
static void set_m_ambientVolume( Int value )
{
	TheWritableGlobalData->m_ambientVolume = value;
	if (TheAudio)
		TheAudio->setVolume( value / 100.0f, (AudioAffect)(AudioAffect_Ambient | AudioAffect_SystemSetting) );
}
static Int get_m_masterVolume( void ) { return TheGlobalData->m_masterVolume; }
static void set_m_masterVolume( Int value )
{
	TheWritableGlobalData->m_masterVolume = value;
	if (TheAudio)
		TheAudio->setVolume( value / 100.0f, (AudioAffect)(AudioAffect_Master | AudioAffect_SystemSetting) );
}
OPTION_BOOL_ACCESSORS( m_dynamicMusic )
OPTION_BOOL_ACCESSORS( m_legacySound )
OPTION_BOOL_ACCESSORS( m_muteInBackground )
// The catalog loads before there is a game; the menu's Accept during a mission changes its speed
// on the spot, which only the pacing reads.
static Int get_m_turboMode( void ) { return TheGlobalData->m_turboMode ? 1 : 0; }
static void set_m_turboMode( Int value )
{
	TheWritableGlobalData->m_turboMode = (value != 0);
	if (TheGameLogic && TheGameLogic->isInSinglePlayerGame())
		TheGameEngine->setFramesPerSecondLimit( GameEngine_matchFramesPerSecond( value != 0 ) );
}
OPTION_BOOL_ACCESSORS( m_formationDrag )
OPTION_BOOL_ACCESSORS( m_buildWithoutWorker )
OPTION_BOOL_ACCESSORS( m_chromaLighting )
OPTION_INT_ACCESSORS( m_bloomIntensity )
OPTION_INT_ACCESSORS( m_menuTransitionSpeed )
OPTION_INT_ACCESSORS( m_textureFilterMode )
OPTION_INT_ACCESSORS( m_anisotropyLevel )
OPTION_INT_ACCESSORS( m_windowMode )
OPTION_INT_ACCESSORS( m_fullscreenScaling )
OPTION_INT_ACCESSORS( m_msaaLevel )
OPTION_BOOL_ACCESSORS( m_vsync )
OPTION_INT_ACCESSORS( m_waterStyle )
OPTION_INT_ACCESSORS( m_cloudStyle )
OPTION_BOOL_ACCESSORS( m_classicGraphics )
OPTION_INT_ACCESSORS( m_lightingStyle )
OPTION_INT_ACCESSORS( m_healthBarMode )
OPTION_INT_ACCESSORS( m_healthBarColors )
OPTION_INT_ACCESSORS( m_playerColorScheme )
OPTION_INT_ACCESSORS( m_textLanguage )
OPTION_BOOL_ACCESSORS( m_showOrderLines )
OPTION_BOOL_ACCESSORS( m_showNetBox )
OPTION_BOOL_ACCESSORS( m_showEmptyBuildingPips )
OPTION_BOOL_ACCESSORS( m_useShadowVolumesForSkins )
OPTION_BOOL_ACCESSORS( m_shadowsForProjectiles )
OPTION_BOOL_ACCESSORS( m_shadowsForProps )
OPTION_BOOL_ACCESSORS( m_shadowsForParticles )
OPTION_BOOL_ACCESSORS( m_particleGroundBounce )
OPTION_BOOL_ACCESSORS( m_volumetricSmokeShadows )
OPTION_BOOL_ACCESSORS( m_useCloudMap )
OPTION_BOOL_ACCESSORS( m_smokeFireLighting )
OPTION_BOOL_ACCESSORS( m_explosionDebrisTrails )
OPTION_BOOL_ACCESSORS( m_mapLight )
OPTION_BOOL_ACCESSORS( m_teamGlow )
OPTION_BOOL_ACCESSORS( m_waterFoam )
OPTION_BOOL_ACCESSORS( m_groundMarks )
OPTION_BOOL_ACCESSORS( m_firelight )
OPTION_BOOL_ACCESSORS( m_hemiAmbient )
OPTION_BOOL_ACCESSORS( m_ambientOcclusion )
OPTION_BOOL_ACCESSORS( m_weatherEffects )
OPTION_BOOL_ACCESSORS( m_waterSplashes )
OPTION_BOOL_ACCESSORS( m_atmosphere )
OPTION_BOOL_ACCESSORS( m_structureCollapse )
OPTION_BOOL_ACCESSORS( m_smoothMotion )
OPTION_BOOL_ACCESSORS( m_internalHdr )
OPTION_INT_ACCESSORS( m_shadowQuality )
OPTION_INT_ACCESSORS( m_graphicsQuality )

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
// Glow, as levels.  GlobalData keeps the percentage the renderer reads and GameData.ini can still
// set it directly, so nothing downstream of here knows the levels exist; Options.ini and the menu
// hold the index of one of these entries.  The index kept its meaning when Ultra was added: an
// Options.ini saved with the four old levels (off, subtle, normal, strong) reads 0 to 3 as Off,
// Low, Medium and High.  The renderer scales every one of its numbers off the percentage.
static const Int TheBloomPercents[ BLOOM_LEVEL_COUNT ] = { 0, 25, 50, 75, 100 };

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

	// No control: the margin's size is read nowhere, only whether it is above 0, which lets a manual
	// pan pull the camera back inside limits a scripted path widened.  An empty widgetName makes the
	// menu passes skip the row, so Options.ini still loads and clamps it.  UseCameraConstraints left
	// the catalog: the camera keeps the map's own limits as the game shipped, and only -noViewLimit
	// lifts them.

	{ "CameraBoundaryMargin", "", "",
		OPTION_INT, APPLY_LIVE, 0, 1000,
		get_m_cameraBoundaryMargin, set_m_cameraBoundaryMargin },

	// Retail refuses to edge-scroll in a window because the cursor can legitimately sit on the
	// border while you reach for something else; on a second monitor, or borderless, that is
	// exactly the behaviour you want back.  Borderless turns it on whatever this says
	// (applyWindowMode).  On Options > Controls.
	{ "EdgeScrollInWindowedMode",	OPT_WND( "CheckEdgeScrollWindowed" ), "GUI:EdgeScrollWindowed",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_edgeScrollInWindowedMode, set_m_edgeScrollInWindowedMode },

	// On Options > Controls in both interfaces, with the three building placement rows below it, and
	// all four off until ticked.  Their keys are new: the old ones (SnapCameraRotateTo45,
	// GridBuildPlacement, SnapBuildPlacementTo45) sit in every Options.ini saved while they defaulted
	// on, as a "yes" nobody chose, and would have kept the snaps on.  GameData.ini keeps the old names.
	{ "CameraSnapTo45",						OPT_WND( "CheckSnapCamera45" ), "GUI:SnapCamera45",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_snapCameraRotateTo45, set_m_snapCameraRotateTo45 },

	// Off, a structure goes wherever the cursor is and the white grid lines under the ghost go
	// with it; the red wash over cells nothing can stand on stays (W3DInGameUI::drawBuildGrid).
	{ "BuildGrid",								OPT_WND( "CheckGridBuild" ), "GUI:GridBuild",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_gridBuildPlacement, set_m_gridBuildPlacement },

	{ "BuildSnapTo45",						OPT_WND( "CheckSnapBuild45" ), "GUI:SnapBuild45",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_snapBuildPlacementTo45, set_m_snapBuildPlacementTo45 },

	// InGameUI::snapPlacementToNeighbour: a structure dropped within a few cells of another lands
	// flush against its edge.
	{ "BuildSnapToNeighbour",			OPT_WND( "CheckSnapBuildNeighbour" ), "GUI:SnapBuildNeighbour",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_snapBuildToNeighbour, set_m_snapBuildToNeighbour },

	// InGameUI::nudgePlacementToLegal: a structure whose spot is blocked slides to the nearest one it
	// fits.  A key of its own: NudgeBuildPlacement, the one it had before it was forced on, can still
	// be a "yes" in an Options.ini saved back then.
	{ "BuildNudge",								OPT_WND( "CheckNudgeBuild" ), "GUI:NudgeBuild",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_nudgeBuildPlacement, set_m_nudgeBuildPlacement },

	// MiddleMousePans used to sit here.  There is nothing left to choose: a right drag pans and a
	// middle drag turns the camera.

	// On Options > Controls in both interfaces, off until ticked: players split on whether the wheel
	// should chase the cursor.  A new key for the snaps' reason above: ZoomToCursor is a "yes" in
	// every Options.ini saved while it defaulted on.
	{ "WheelZoomToCursor",				OPT_WND( "CheckZoomToCursor" ), "GUI:ZoomToCursor",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_zoomToCursor, set_m_zoomToCursor },

	// How far one wheel notch moves the camera, in percent of the 60 units it always moved.  The
	// scroll speed above never touched the wheel; this is the wheel's own.  On Options > Controls.
	{ "ZoomSpeed",								OPT_WND( "SliderZoomSpeed" ), "GUI:ZoomSpeed",
		OPTION_INT, APPLY_LIVE, 25, 300,
		get_m_zoomSpeed, set_m_zoomSpeed },

	// The battlefield from far off down a narrow cone, so a unit is the same size wherever it
	// stands on the screen.  The heading stays the player's.  On Options > Controls.
	{ "IsometricCamera",					OPT_WND( "CheckIsometricCamera" ), "GUI:IsometricCamera",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_isometricCamera, set_m_isometricCamera },

	// Whether blasts and scripts may shake the view; W3DView asks at each shake, and one under way
	// settles by itself.  On Options > Controls.  It costs no frame time, so no quality row.
	{ "CameraShake",							OPT_WND( "CheckCameraShake" ), "GUI:CameraShake",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_cameraShake, set_m_cameraShake },

	// A match opens as far out as the wheel goes, or 300 over the ground (View::setZoomToStart).
	// Read when the map loads, so it counts from the next match.  On Options > Controls in both
	// interfaces.  A new key for the snaps' reason above: StartAtMaxZoom is a "yes" in old files.
	{ "OpenAtMaxZoom",						OPT_WND( "CheckStartAtMaxZoom" ), "GUI:StartAtMaxZoom",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_startAtMaxZoom, set_m_startAtMaxZoom },

	// Turbo mode for the campaign and the generals' challenge: twice the logic frames a second, each
	// the same as at normal speed.  A skirmish or network lobby has its own box, the host's for the
	// whole room, so this one never touches a skirmish or a multiplayer match.  On Options > Interface.
	{ "TurboMode",								OPT_WND( "CheckTurboModeMissions" ), "GUI:TurboModeMissions",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_turboMode, set_m_turboMode },

	// Pixels the pointer may travel with a button held before the press stops being a click and
	// starts a selection box, a camera drag or a formation line.  Mouse.ini says 25, which is the
	// default here; a high-DPI mouse wants more and a small screen less.
	{ "DragTolerance",						OPT_WND( "SliderDragTolerance" ), "GUI:DragTolerance",
		OPTION_INT, APPLY_LIVE, 2, 50,
		get_m_dragTolerance, set_m_dragTolerance },

	// Looping world ambience - birds, wind, water, a town - on a slider of its own beside Sound FX,
	// which no longer reaches it.  AudioManager::isAmbientSound says what counts.  On Options > Audio.
	{ "AmbientVolume",						OPT_WND( "SliderAmbientVolume" ), "GUI:AmbientVolume",
		OPTION_INT, APPLY_LIVE, 0, 100,
		get_m_ambientVolume, set_m_ambientVolume },

	// One slider over the other four: music, effects, voices and ambience are each multiplied by it,
	// a unit's reply with its boost included.  Movies follow it through the voice volume.
	{ "MasterVolume",							OPT_WND( "SliderMasterVolume" ), "GUI:MasterVolume",
		OPTION_INT, APPLY_LIVE, 0, 100,
		get_m_masterVolume, set_m_masterVolume },

	// A skirmish or multiplayer match's music answers the fight: a calmer track of the side's own
	// playlist while nothing happens, a harder one when guns fire near the camera or at your forces
	// (MusicManager::update).  Off leaves the music to the map's scripts, as retail had it.
	{ "DynamicMusic",							OPT_WND( "CheckDynamicMusic" ), "GUI:DynamicMusic",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_dynamicMusic, set_m_dynamicMusic },

	// Retail's mix: no sound travel delay, air, doppler, room, occlusion, duck, limiter, battle voice
	// budget, shot folding, reply limits, zoom mix, ambience bed or music following the fight.  On by
	// default, since off is what the fork added.  The audio manager takes it up on its next pass
	// (MilesAudioManager::applyLegacySound), the room and the bed included.  Master volume, the device
	// and the speakers stay as they are.  On Options > Audio.
	{ "LegacySound",							OPT_WND( "CheckLegacySound" ), "GUI:LegacySound",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_legacySound, set_m_legacySound },

	// Silent while another window has the focus, as retail always was; off keeps the game heard
	// behind a browser or a second monitor's window.  Read at the next focus change.  On Options > Audio.
	{ "MuteInBackground",					OPT_WND( "CheckMuteInBackground" ), "GUI:MuteInBackground",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_muteInBackground, set_m_muteInBackground },

	// With the move, attack move or guard key armed, a left drag over the ground spreads the
	// selection along the line drawn instead of sending everyone to one point.  On Options > Controls.
	{ "FormationDrag",						OPT_WND( "CheckFormationDrag" ), "GUI:FormationDrag",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_formationDrag, set_m_formationDrag },

	// With nothing selected the command bar shows one of your builders' buildings, and a structure put
	// down from it goes to the idle builder nearest the site.  Client only: the logic already hands a
	// placement with no qualifying selected builder to one (MSG_DOZER_CONSTRUCT), so off only takes
	// the bar away, as 1.04 had it.  The bar picks it up on its next evaluation.  On Options > Controls.
	{ "BuildWithoutWorker",				OPT_WND( "CheckBuildWithoutWorker" ), "GUI:BuildWithoutWorker",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_buildWithoutWorker, set_m_buildWithoutWorker },

	// The match on Razer hardware: the command bar on the letter keys, power on the digits,
	// superweapons on the numpad, money on the mousepad.  Only during a match; off, and in every
	// menu, the keyboard goes back to whatever Synapse wants to do with it.  On Options > Controls.
	{ "ChromaLighting",						OPT_WND( "CheckChromaLighting" ), "GUI:ChromaLighting",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_chromaLighting, set_m_chromaLighting },

	// Percent of the speed the menu slides and fades were authored at. 100 is what the artists
	// drew; higher gets you through the shell faster, and nothing about a menu animation is worth
	// waiting for on the four hundredth launch.  On Options > Interface.
	{ "MenuTransitionSpeed",			OPT_WND( "SliderMenuTransitionSpeed" ), "GUI:MenuTransitionSpeed",
		OPTION_INT, APPLY_LIVE, 25, 400,
		get_m_menuTransitionSpeed, set_m_menuTransitionSpeed },

	// 0 bilinear, 1 trilinear, 2 anisotropic. Retail shipped bilinear with point mip selection,
	// which is a 2003 fill-rate budget and is why distant ground shimmers; 0 is the default, and 2 is
	// what the presets from Medium up fill in. The filter table is built when the device is made, and W3DDisplay::init is the only place
	// that hands this value to WW3D2 - a reset from the display page does not push it again - so it
	// waits for the next launch.  A combo box on the Graphics page; stored as the same decimal the
	// hand-edited key always was.
	{ "TextureFilter",						OPT_WND( "ComboBoxTextureFilter" ), "GUI:TextureFilter",
		OPTION_ENUM, APPLY_RESTART, 0, TEXTURE_FILTER_MODE_COUNT - 1,
		get_m_textureFilterMode, set_m_textureFilterMode },

	// Samples anisotropic filtering may take. 0 means whatever the card offers, capped at 16, and
	// asking for more than the card has still gets you the card's answer. Only read when the filter
	// above is anisotropic.  The slider beside the filter box; pushed in by W3DDisplay::init with it.
	{ "Anisotropy",								OPT_WND( "SliderAnisotropy" ), "GUI:Anisotropy",
		OPTION_INT, APPLY_RESTART, 0, 16,
		get_m_anisotropyLevel, set_m_anisotropyLevel },

	// Five rows used to sit here: the placement range ring, workers returning to supply, detailed
	// build tooltips, the HUD overlay and replay archiving.  Every one of them is now on for
	// everybody, decided in GlobalData's constructor, so there is nothing left to load or save.
	// Grid placement, snap-to-45 building rotation and the nudge left with them and came back to
	// Options > Controls, above, off until ticked.

	// Glow: off, low, medium, high, ultra.  Off is the default, set in GlobalData's
	// constructor.  The key is "Bloom", not "Glow" or "BloomLevel" - it predates the levels and an
	// Options.ini in the wild already spells it this way.  The "What Glows" threshold row that sat
	// under it is gone: a lower threshold only ever bloomed the buildings and the sand, so the
	// level decides it, and a BloomThreshold line an older Options.ini kept is ignored.
	{ "Bloom",										OPT_WND( "ComboBoxBloom" ), "GUI:Bloom",
		OPTION_ENUM, APPLY_LIVE, 0, BLOOM_LEVEL_COUNT - 1,
		get_bloomLevel, set_bloomLevel },

	// Fullscreen, borderless or windowed.  The old Windowed flag in GameData.ini seeds this and is
	// then derived back from it, so the device layer keeps reading the boolean it always read.
	// The window's style at startup is still settled by CreateWindow in WinMain, which runs before
	// the engine exists and reads this key itself through EarlyOptions.h; changing it while the game
	// is up restyles that window and rebuilds the device (W3DDisplay::setDisplayMode).
	{ "WindowMode",								OPT_WND( "ComboBoxWindowMode" ), "GUI:WindowMode",
		OPTION_ENUM, APPLY_DEVICE_RESET, 0, WINDOW_MODE_COUNT - 1,
		get_m_windowMode, set_m_windowMode },

	// How fullscreen fills a monitor whose shape the picture does not have: stretched, or the
	// picture's own shape with black bars.  The monitor keeps its mode and DX8Wrapper places the
	// window (Apply_Fullscreen_Display); W3DDisplay::setDisplayMode pushes the choice in.
	{ "FullscreenScaling",				OPT_WND( "ComboBoxFullscreenScaling" ), "GUI:FullscreenScaling",
		OPTION_ENUM, APPLY_DEVICE_RESET, 0, FULLSCREEN_SCALING_COUNT - 1,
		get_m_fullscreenScaling, set_m_fullscreenScaling },

	// Multisampling, as an index into 0/2/4/8/16 rather than a sample count - the device offers
	// those and nothing between them, so a slider would spend most of its travel on values that
	// silently round down.  W3DDisplay::init hands it to whichever device presents, once.  The
	// Direct3D 11 scene takes it on its post-process target and drops its FXAA while it has samples.
	{ "MSAA",											OPT_WND( "ComboBoxMSAA" ), "GUI:MSAA",
		OPTION_ENUM, APPLY_RESTART, 0, OPTION_MSAA_LEVEL_COUNT - 1,
		get_m_msaaLevel, set_m_msaaLevel },

	// Wait for the monitor.  Off is what the uncapped picture shipped as: D3D9 honours the
	// presentation interval in a window, so leaving this on would pin a windowed game to the
	// refresh the way fullscreen used to be pinned.  The device has to be reset; Accept does that.
	{ "VSync",										OPT_WND( "CheckVSync" ), "GUI:VSync",
		OPTION_BOOL, APPLY_DEVICE_RESET, 0, 1,
		get_m_vsync, set_m_vsync },

	// Water selects a complete surface path, including geometry and reflections. The water
	// object applies it at the next frame boundary, including while the options menu is paused.
	{ "WaterStyle", OPT_WND( "ComboBoxWaterStyle" ), "GUI:WaterStyle",
		OPTION_ENUM, APPLY_LIVE, 0, WATER_STYLE_COUNT - 1,
		get_m_waterStyle, set_m_waterStyle },

	// Client appearance only; hazard area, lifetime and damage remain unchanged.
	{ "CloudStyle", OPT_WND( "ComboBoxCloudStyle" ), "GUI:CloudStyle",
		OPTION_ENUM, APPLY_LIVE, 0, CLOUD_STYLE_COUNT - 1,
		get_m_cloudStyle, set_m_cloudStyle },

	// Classic or Reforged.  Classic is the picture the game shipped with: its own textures, EA's
	// ground tile, no normal maps, the stencil shadows and no post effects, and it is what a key-less
	// file gets.  The upscaled archives are mounted before GlobalData exists, so Win32BIGFileSystem
	// reads this key out of Options.ini itself, with the same default, and everything else takes it
	// once while the device starts.  Unticking the box in the menu fills in the High preset, which is
	// the Reforged look in one click; ticking it takes the fork's rows of the slider back to their first step.
	{ "ClassicGraphics",					OPT_WND( "CheckClassicGraphics" ), "GUI:ClassicGraphics",
		OPTION_BOOL, APPLY_RESTART, 0, 1,
		get_m_classicGraphics, set_m_classicGraphics },

	// Who wears a health bar: everyone, everyone hurt, only the selection, or nobody.  Read every
	// frame by the drawable that is about to draw one, so changing it shows immediately.
	{ "HealthBars",								OPT_WND( "ComboBoxHealthBars" ), "GUI:HealthBars",
		OPTION_ENUM, APPLY_LIVE, 0, HEALTH_BAR_MODE_COUNT - 1,
		get_m_healthBarMode, set_m_healthBarMode },

	// What a health bar is coloured by: 0 the health, green through yellow to red as retail drew it,
	// 1 the owner's player colour.  On Options > Interface under Health Bars.  Read every frame by
	// Drawable::drawHealthBar, and a disabled unit keeps its blue either way.
	{ "HealthBarColors",					OPT_WND( "ComboBoxHealthBarColors" ), "GUI:HealthBarColors",
		OPTION_ENUM, APPLY_LIVE, 0, HEALTH_BAR_COLORS_COUNT - 1,
		get_m_healthBarColors, set_m_healthBarColors },

	// MenuLayout (stretch or fit), HudScale and InterfaceStyle used to sit here.  Every menu is
	// fitted now, and there is one interface at one size.

	// Whose colour a player is drawn in.  Purely local: the match still agrees on the lobby's
	// colours and this only changes what this screen puts on top of them, so two people in the same
	// game can run different schemes.
	{ "PlayerColors",							OPT_WND( "ComboBoxPlayerColors" ), "GUI:PlayerColors",
		OPTION_ENUM, APPLY_LIVE, 0, PLAYER_COLOR_SCHEME_COUNT - 1,
		get_m_playerColorScheme, set_m_playerColorScheme },

	// The line from each selected unit to where it is going, with every point of a shift queue after
	// it.  The list is rebuilt from the units every frame, so turning it off takes the lines away at
	// once and turning it back on shows the orders already given.
	{ "OrderLines",								OPT_WND( "CheckOrderLines" ), "GUI:OrderLines",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_showOrderLines, set_m_showOrderLines },

	// The box in the top right corner: the match clock, the frame rates and, in a network game, the
	// connection.  Off, the superweapon timers move up into the corner it leaves.  A key of its own
	// and not ShowHudOverlay, which left this catalog on purpose: an Options.ini from before that
	// still says "no" under the old name.
	{ "ShowNetBox",								OPT_WND( "CheckNetBox" ), "GUI:NetBox",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_showNetBox, set_m_showNetBox },

	// The row of slots over a building that holds troops is drawn empty too: ten grey boxes over a
	// Barracks nobody is healing in.  Off, a building wears its slots only while somebody is inside.
	// Vehicles keep theirs either way, where an empty row is how much the transport carries.
	{ "EmptyBuildingPips",				OPT_WND( "CheckEmptyBuildingPips" ), "GUI:EmptyBuildingPips",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_showEmptyBuildingPips, set_m_showEmptyBuildingPips },

	// Which language the words are in.  English is the string table the game shipped with, and every
	// other entry is a translation laid over it, so a line the translation lacks stays English.  The
	// table is built once while the game starts: a new language is on screen from the next launch.
	// Purely local, like the colours above - two players in one match can read it in two languages.
	{ "TextLanguage",							OPT_WND( "ComboBoxLanguage" ), "GUI:Language",
		OPTION_ENUM, APPLY_RESTART, 0, TEXT_LANGUAGE_COUNT - 1,
		get_m_textLanguage, set_m_textLanguage },

	// The Effects page.  These four sat in GameData.ini with no control, all off.  Each is read when the
	// thing that casts the shadow is made (fillShadowInfoFromTemplate, promoteSkinShadowToVolume), so a
	// change shows on the next map rather than on the units already standing there - except smoke
	// clouds, which ask every frame.
	{ "UseShadowVolumesForSkins",	OPT_WND( "CheckInfantryShadows" ), "GUI:InfantryShadows",
		OPTION_BOOL, APPLY_NEXT_MAP, 0, 1,
		get_m_useShadowVolumesForSkins, set_m_useShadowVolumesForSkins },

	{ "ShadowsForProjectiles",		OPT_WND( "CheckProjectileShadows" ), "GUI:ProjectileShadows",
		OPTION_BOOL, APPLY_NEXT_MAP, 0, 1,
		get_m_shadowsForProjectiles, set_m_shadowsForProjectiles },

	{ "ShadowsForProps",					OPT_WND( "CheckPropShadows" ), "GUI:PropShadows",
		OPTION_BOOL, APPLY_NEXT_MAP, 0, 1,
		get_m_shadowsForProps, set_m_shadowsForProps },

	{ "ShadowsForParticles",			OPT_WND( "CheckParticleShadows" ), "GUI:ParticleShadows",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_shadowsForParticles, set_m_shadowsForParticles },

	// Smoke and dust in the sun's map: a cloud shades the ground, the units and the smoke behind it
	// by how thick it is, and darkens on its own far side.  Read by the shadow pass every frame.  On
	// Options > Effects; -novolumetricsmoke turns it off for a run.
	{ "VolumetricSmokeShadows",		OPT_WND( "CheckVolumetricSmoke" ), "GUI:VolumetricSmoke",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_volumetricSmokeShadows, set_m_volumetricSmokeShadows },

	// The shadows of passing clouds scrolled over the terrain.  It used to belong to the detail
	// preset, which turned it off at Low and gave no other way to; now applying a preset leaves it
	// alone and GameData.ini's Yes holds until Options.ini says otherwise.  The terrain asks every
	// frame.  Its control is EA's CheckCloudShadows: the quality slider sets it like any other box, and
	// the menu reads it by hand and saves it at every detail level.
	{ "UseCloudMap",							"", "",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_useCloudMap, set_m_useCloudMap },

	// Classic or Reforged light.  Reforged rebalances each map's lights as the map loads (so a change
	// shows on the next map), deepens the sun's shadow to match and grades the Direct3D 11 picture,
	// which follows the setting every frame.  ClassicGraphics forces Classic.  On Options > Graphics;
	// -lighting picks it for one run.
	{ "LightingStyle",						OPT_WND( "ComboBoxLightingStyle" ), "GUI:LightingStyle",
		OPTION_ENUM, APPLY_NEXT_MAP, 0, LIGHTING_STYLE_COUNT - 1,
		get_m_lightingStyle, set_m_lightingStyle },

	// Smoke lit by the fire beside it.  On Options > Effects; -nosmokefirelight turns it off for a run.
	{ "SmokeFireLighting",				OPT_WND( "CheckSmokeFireLighting" ), "GUI:SmokeFireLighting",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_smokeFireLighting, set_m_smokeFireLighting },

	// The fork's smoke-trailing blast fragments.  Off, every system marked ExplosionDebrisTrail in
	// ParticleSystemReforged.ini is destroyed as it is made, which leaves the retail explosions.  On
	// Options > Effects.
	{ "DebrisTrails",							OPT_WND( "CheckDebrisTrails" ), "GUI:DebrisTrails",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_explosionDebrisTrails, set_m_explosionDebrisTrails },

	// Three Direct3D 11 looks, all off by default; the menu greys them under -d3d9.  Warm light is a
	// low late afternoon sun put into each map's lights as the map loads (WorldHeightMap), so it waits
	// for the next map, and a light still haze over low ground, pushed every frame
	// (W3DShaderManager).  Team glow marks a house colour mesh as it is recoloured for its side,
	// which happens as a map's models are made.  On Options > Graphics.
	{ "MapLight",									OPT_WND( "CheckMapLight" ), "GUI:MapLight",
		OPTION_BOOL, APPLY_NEXT_MAP, 0, 1,
		get_m_mapLight, set_m_mapLight },

	{ "TeamGlow",									OPT_WND( "CheckTeamGlow" ), "GUI:TeamGlow",
		OPTION_BOOL, APPLY_NEXT_MAP, 0, 1,
		get_m_teamGlow, set_m_teamGlow },

	// Foam, a shore band and the sun's shadow on rivers and lakes; the water asks every frame.  Off
	// under Classic graphics too.  On Options > Effects beside Soft Shorelines.
	{ "WaterFoam",								OPT_WND( "CheckWaterFoam" ), "GUI:WaterFoam",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_waterFoam, set_m_waterFoam },

	// Craters and a building's stain on the terrain, fading over minutes, and darker treads.  The
	// scorch buffer asks every draw.  Off under Classic graphics and -d3d9.  On Options > Effects.
	{ "GroundMarks",							OPT_WND( "CheckGroundMarks" ), "GUI:GroundMarks",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_groundMarks, set_m_groundMarks },

	// Burning buildings, wrecks and blasts as flickering point lights on the ground, walls and units.
	// The particle draw asks every frame.  Off under Classic graphics and -d3d9, which light nothing
	// per pixel.  On Options > Effects.
	{ "Firelight",								OPT_WND( "CheckFirelight" ), "GUI:Firelight",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_firelight, set_m_firelight },

	// Models take the map's ambient from the sky above and the ground below instead of evenly.  The
	// scene hands it to the backend every frame.  Off under Classic graphics and -d3d9.  On Options > Effects.
	{ "HemiAmbient",							OPT_WND( "CheckHemiAmbient" ), "GUI:HemiAmbient",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_hemiAmbient, set_m_hemiAmbient },

	// Corners and the ground under what stands on it darken.  The scene reads it every frame.  Off under
	// Classic graphics and -d3d9.  On Options > Effects.
	{ "AmbientOcclusion",					OPT_WND( "CheckAmbientOcclusion" ), "GUI:AmbientOcclusion",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_ambientOcclusion, set_m_ambientOcclusion },

	// Rain, blowing sand or snow by the map's ground, and a wind on trees and smoke.  Read every frame, and
	// no sim reads it, so two players may differ.  Off under Classic graphics and -d3d9.  On Options > Effects.
	{ "Weather",							OPT_WND( "CheckWeather" ), "GUI:Weather",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_weatherEffects, set_m_weatherEffects },

	// A splash and rings where a blast or a falling body meets water: W3DGameClient::splashInWater makes
	// particle systems, and no sim reads them.  Off under Classic graphics, -d3d9 and Modern water.  On
	// Options > Effects.
	{ "WaterSplashes",						OPT_WND( "CheckWaterSplashes" ), "GUI:WaterSplashes",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_waterSplashes, set_m_waterSplashes },

	// The far ground fades toward the horizon and what the frame left empty takes a sky.  Read every frame,
	// and no sim reads it.  Off under Classic graphics and -d3d9.  On Options > Graphics, under Warm Light:
	// Effects is full.
	{ "Atmosphere",								OPT_WND( "CheckAtmosphere" ), "GUI:Atmosphere",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_atmosphere, set_m_atmosphere },

	// A building's last model goes into the ground: StructureCollapse.cpp moves a copy no drawable owns,
	// and no sim reads it.  Off under Classic graphics and -d3d9.  On Options > Display, under Picture:
	// every column of Graphics and Effects is full.
	{ "StructureCollapse",				OPT_WND( "CheckStructureCollapse" ), "GUI:StructureCollapse",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_structureCollapse, set_m_structureCollapse },

	// Models drawn between their last two logic ticks on a display faster than the logic; a 60 Hz screen
	// shows no change.  Read every picture by W3DDisplay's smoothMotionBegin, and no sim reads it.  The
	// key is the one the launcher offered once; a line it left behind is the player's choice.
	// -nosmoothmotion and -smoothmotion beat it for a run.  On Options > Display, under Picture.
	{ "SmoothMotion",							OPT_WND( "CheckSmoothMotion" ), "GUI:SmoothMotion",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_smoothMotion, set_m_smoothMotion },

	// What passes white keeps its colour on the way to the eight bit screen; no HDR monitor output.  The
	// post chain is told every frame.  Classic graphics has no half float scene and -d3d9 no chain.  On
	// Options > Effects.
	{ "InternalHdr",							OPT_WND( "CheckInternalHdr" ), "GUI:InternalHdr",
		OPTION_BOOL, APPLY_LIVE, 0, 1,
		get_m_internalHdr, set_m_internalHdr },

	// The size of the sun's depth map.  The shadow pass asks for it every frame and the backend makes
	// the map again when the size changes, so a new level shows on the next frame.  On Options >
	// Effects.  Classic graphics and -d3d9 draw no map at all.
	{ "ShadowQuality",						OPT_WND( "ComboBoxShadowQuality" ), "GUI:ShadowQuality",
		OPTION_ENUM, APPLY_LIVE, 0, SHADOW_QUALITY_COUNT - 1,
		get_m_shadowQuality, set_m_shadowQuality },

	// -smoke and -particlebounce as settings.  Both are spent on the particle system templates while
	// the particle manager starts, so they wait for the next launch.  The command line is parsed after
	// this catalog loads, so either switch still wins for the one run it is given.
	{ "Smoke",										OPT_WND( "ComboBoxSmoke" ), "GUI:Smoke",
		OPTION_ENUM, APPLY_RESTART, 0, SMOKE_LEVEL_COUNT - 1,
		get_smokeLevel, set_smokeLevel },

	{ "ParticleBounce",						OPT_WND( "CheckParticleBounce" ), "GUI:ParticleBounce",
		OPTION_BOOL, APPLY_RESTART, 0, 1,
		get_m_particleGroundBounce, set_m_particleGroundBounce },

	// Where the Performance / Quality slider stands.  The slider fills in the other controls and each
	// of them saves itself; a detail preset puts it on the preset's step whatever this says.  Under
	// Custom, GameLODManager takes the Direct3D 11 particle ring, which has no control, from the
	// preset at or below this step.
	{ "GraphicsQuality",					OPT_WND( "SliderGraphicsQuality" ), "GUI:GraphicsQuality",
		OPTION_INT, APPLY_LIVE, 0, GRAPHICS_QUALITY_STEPS - 1,
		get_m_graphicsQuality, set_m_graphicsQuality },

	{ NULL, NULL, NULL, OPTION_BOOL, APPLY_LIVE, 0, 0, NULL, NULL }
};

const Int TheOptionCatalogCount = (sizeof( TheOptionCatalog ) / sizeof( TheOptionCatalog[ 0 ] )) - 1;

//-----------------------------------------------------------------------------
// The Performance / Quality slider, described in OptionsCatalog.h.
//
// Rows run from the cheapest setting to the dearest, and a row's values climb from step 0 to step
// 10: the column a row first rises in is where the slider starts paying for it.  A column is what the
// menu fills in, and saves: a preset named in Options.ini or picked for a first run sets only the
// GameLOD fields (applyGraphicsQualityStep), so a row with no line stays at GlobalData's off.  costMs is the one
// place the price is written down: Direct3D 11 GPU milliseconds a frame saved by turning that one
// setting from its best to its cheapest with everything else at its best, in a 900 unit four-army
// fight at 1920x1080 on a GeForce RTX 2060 (2026-10-25).  Anything under the 0.15 ms the baselines
// wandered by is written 0.  The detail presets sit on steps 0, 4, 7 and 10, so the column under
// Low, Medium, High or Ultra is that preset.
//
// The free rows all come on by step 3, except those that draw into the sun's shadow map: they come
// on with cast shadows at step 5, since the map is not drawn without them.  Never Lower Detail is
// free on a card that holds its frame rate and is what saves one that does not, so it waits for
// Ultra.  Multisampling cost nothing measurable under Direct3D 9 at 8x on this card; its Direct3D
// 11 price was never swept, so the row still rises through the middle.  Under Direct3D 9
// MapLight, TeamGlow and WaterFoam are greyed and the slider leaves them; the shadow map and the
// volumetric clouds are the other renderers' alone.
//
// The nine looks of 2026-10-31 all rise above High, so High stays the picture the Classic box's
// untick has always given.  Their costs are GPU means past frame 600, on against off in two pairs on
// trailer chaos (water splashes on constant shelling), rows under 0.15 ms again written 0.  Weather
// and Atmosphere change a map's mood, and Atmosphere, Firelight and StructureCollapse still carry
// faults listed in docs/plans/TODO.md, so those three wait for Ultra.
//
// Trees have no row: GameLogic reads the setting as a map loads, so it stays with the memory test
// as EA had it and a preset never turns them off.  The behind-buildings marker has none either; it
// is there to read the battlefield, not to cost frame time.  Water style has none: the fight above
// had no sea, so Modern water read as free, and v3.4.0 gave it to every preset from Medium up and
// to every Options.ini without the key.  The main menu's shell map is mostly sea, and there the
// same card spent 11 to 45 ms a pass against 3.3 to 6 ms with Classic water.  It stays the opt-in
// GlobalData's constructor makes it.
//-----------------------------------------------------------------------------
const Int TheGraphicsQualityAnchors[] = { 0, 4, 7, 10 };

const GraphicsQualityRow TheGraphicsQualityTable[] =
{
	// control, cost, step:			 0     1     2     3     4     5     6     7     8     9    10
	{ "ComboBoxLightingStyle",	0.0f,  {    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckGroundLighting",		0.0f,  {    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckSmoothWater",				0.0f,  {    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckCloudShadows",			0.0f,  {    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckExtraAnimations",		0.0f,  {    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckHeatEffects",				0.0f,  {    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckTreeSway",					0.0f,  {    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "Check2DShadows",					0.0f,  {    0,    0,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckProjectileShadows",	0.0f,  {    0,    0,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckDebrisTrails",			0.0f,  {    0,    0,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckSmokeFireLighting",	0.0f,  {    0,    0,    1,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckTeamGlow",					0.0f,  {    0,    0,    0,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckWaterFoam",					0.0f,  {    0,    0,    0,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "ComboBoxCloudStyle",			0.0f,  {    0,    0,    0,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckParticleBounce",		0.0f,  {    0,    0,    0,    1,    1,    1,    1,    1,    1,    1,    1 } },
	{ "CheckInfantryShadows",		0.0f,  {    0,    0,    0,    0,    0,    1,    1,    1,    1,    1,    1 } },
	{ "CheckVolumetricSmoke",		0.0f,  {    0,    0,    0,    0,    0,    1,    1,    1,    1,    1,    1 } },
	{ "ComboBoxMSAA",						0.0f,  {    0,    0,    0,    0,    0,    1,    1,    2,    2,    3,    3 } },
	{ "CheckNoDynamicLOD",			0.0f,  {    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    1 } },
	{ "CheckInternalHdr",				0.0f,  {    0,    0,    0,    0,    0,    0,    0,    0,    1,    1,    1 } },
	{ "CheckHemiAmbient",				0.0f,  {    0,    0,    0,    0,    0,    0,    0,    0,    1,    1,    1 } },
	{ "CheckStructureCollapse",	0.0f,  {    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    1 } },
	{ "ComboBoxTextureFilter",	0.1f,  {    0,    1,    2,    2,    2,    2,    2,    2,    2,    2,    2 } },
	{ "ComboBoxBloom",					0.1f,  {    0,    1,    2,    2,    2,    2,    2,    2,    2,    2,    2 } },
	{ "SliderAnisotropy",				0.1f,  {    2,    4,    8,   16,   16,   16,   16,   16,   16,   16,   16 } },
	{ "CheckParticleShadows",		0.15f, {    0,    0,    0,    0,    0,    0,    0,    1,    1,    1,    1 } },
	{ "LowResSlider",						0.2f,  {    1,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2 } },
	{ "CheckPropShadows",				0.2f,  {    0,    0,    0,    0,    0,    0,    1,    1,    1,    1,    1 } },
	{ "CheckWaterSplashes",			0.2f,  {    0,    0,    0,    0,    0,    0,    0,    0,    1,    1,    1 } },
	{ "CheckAtmosphere",				0.2f,  {    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    1 } },
	{ "ComboBoxShadowQuality",	0.25f, {    0,    0,    0,    0,    0,    0,    1,    1,    1,    2,    2 } },
	{ "CheckAmbientOcclusion",	0.3f,  {    0,    0,    0,    0,    0,    0,    0,    0,    1,    1,    1 } },
	{ "CheckMapLight",					0.35f, {    0,    0,    0,    0,    0,    0,    0,    0,    1,    1,    1 } },
	{ "CheckFirelight",					0.4f,  {    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    1 } },
	// 0.79 and 0.33 ms in the two pairs on 2026-11-01, the widest spread in the table
	{ "CheckGroundMarks",				0.55f, {    0,    0,    0,    0,    0,    0,    0,    0,    0,    1,    1 } },
	{ "CheckWeather",						0.65f, {    0,    0,    0,    0,    0,    0,    0,    0,    0,    1,    1 } },
	{ "ComboBoxSmoke",					0.75f, {    0,    0,    0,    0,    0,    0,    0,    0,    0,    1,    1 } },
	// measured 5000 against the slider's floor of 100 (1.6 ms) and against 1000 (0.5 ms)
	{ "ParticleCapSlider",			1.6f,  {  500, 1000, 1500, 1500, 1500, 2000, 2500, 3000, 3500, 4000, 5000 } },
	// the shadow map's whole cost: the scene 4.06 to 2.14 ms and the shadow pass 0.6 to 0
	{ "Check3DShadows",					2.5f,  {    0,    0,    0,    0,    0,    1,    1,    1,    1,    1,    1 } },
};

const Int TheGraphicsQualityTableCount = sizeof( TheGraphicsQualityTable ) / sizeof( TheGraphicsQualityTable[ 0 ] );

//-----------------------------------------------------------------------------
const GraphicsQualityRow *findGraphicsQualityRow( const char *control )
{
	for( Int i = 0; i < TheGraphicsQualityTableCount; ++i )
		if( strcmp( TheGraphicsQualityTable[ i ].control, control ) == 0 )
			return &TheGraphicsQualityTable[ i ];

	return NULL;
}

//-----------------------------------------------------------------------------
const OptionDef *findOptionDef( const char *iniKey )
{
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
		if( strcasecmp( TheOptionCatalog[ i ].iniKey, iniKey ) == 0 )
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
	* The option getters this replaces tested `strcasecmp(s, "yes") == 0` and called everything else
	* false, so a hand-edited `ZoomToCursor = true` silently did nothing.  UserPreferences::getBool
	* has always been the lenient one; the catalog follows it.  Writing still produces "yes"/"no". */
static Int parseOptionValue( const OptionDef& def, const AsciiString& stored )
{
	if( def.kind == OPTION_BOOL )
	{
		const char *s = stored.str();
		const Bool on = strcasecmp( s, "yes" ) == 0
									|| strcasecmp( s, "true" ) == 0
									|| strcasecmp( s, "on" ) == 0
									|| strcasecmp( s, "y" ) == 0
									|| strcasecmp( s, "t" ) == 0
									|| strcasecmp( s, "1" ) == 0;
		return on ? 1 : 0;
	}

	return clampOptionValue( def, atoi( stored.str() ) );
}

//-----------------------------------------------------------------------------
Bool parseOptionText( const OptionDef& def, const char *text, Int *value )
{
	if( def.kind == OPTION_BOOL )
	{
		// off and on in pairs, so a word's index modulo two is its value
		static const char *const TheBoolWords[] = { "no", "yes", "false", "true", "off", "on", "n", "y", "f", "t", "0", "1" };
		for( Int i = 0; i < (Int)( sizeof( TheBoolWords ) / sizeof( TheBoolWords[ 0 ] ) ); ++i )
		{
			if( strcasecmp( text, TheBoolWords[ i ] ) == 0 )
			{
				*value = i % 2;
				return TRUE;
			}
		}
		return FALSE;
	}

	char *end;
	const long number = strtol( text, &end, 10 );
	if( end == text || *end != '\0' || number < def.lo || number > def.hi )
		return FALSE;

	*value = (Int)number;
	return TRUE;
}

//-----------------------------------------------------------------------------
AsciiString formatOptionValue( const OptionDef& def, Int value )
{
	if( def.kind == OPTION_BOOL )
		return AsciiString( value ? "yes" : "no" );

	AsciiString out;
	out.format( "%d", clampOptionValue( def, value ) );
	return out;
}

//-----------------------------------------------------------------------------
/** The catalog row a quality table row sets, or -1 for a GameLOD field, which the preset sets itself. */
static Int graphicsQualityOptionIndex( const GraphicsQualityRow &row )
{
	AsciiString widget;
	widget.format( OPT_WND( "%s" ), row.control );
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		if( strcmp( TheOptionCatalog[ i ].widgetName, widget.str() ) == 0 )
			return i;

		// EA's cloud shadow box is read by the menu by hand, so its catalog row names no control
		if( strcmp( row.control, "CheckCloudShadows" ) == 0 && strcmp( TheOptionCatalog[ i ].iniKey, "UseCloudMap" ) == 0 )
			return i;
	}

	return -1;
}

//-----------------------------------------------------------------------------
Bool graphicsQualityRowIsReforged( const GraphicsQualityRow &row )
{
	return graphicsQualityOptionIndex( row ) >= 0 && strcmp( row.control, "CheckCloudShadows" ) != 0;
}

//-----------------------------------------------------------------------------
// What each row held before Options.ini reached it: GlobalData's constructor and GameData.ini, the
// values a first run plays with.  Taken once per GlobalData, because GameData blocks parse again later
// and by then a row holds the player's value.
static Int TheOptionDefaults[ sizeof( TheOptionCatalog ) / sizeof( TheOptionCatalog[ 0 ] ) ];
static const GlobalData *TheOptionDefaultsOwner = NULL;

Int optionDefaultValue( const OptionDef& def )
{
	return TheOptionDefaults[ &def - TheOptionCatalog ];
}

void loadOptionsFromPreferences( UserPreferences& pref )
{
	if( TheOptionDefaultsOwner != TheGlobalData )
	{
		TheOptionDefaultsOwner = TheGlobalData;
		for( Int i = 0; i < TheOptionCatalogCount; ++i )
			TheOptionDefaults[ i ] = TheOptionCatalog[ i ].get();
	}

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
void logOptionCatalog( void )
{
	AsciiString line;
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		AsciiString entry;
		entry.format( " %s=%d", TheOptionCatalog[ i ].iniKey, TheOptionCatalog[ i ].get() );
		line.concat( entry );
	}
	DEBUG_LOG(( "OPTIONS%s\n", line.str() ));
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
