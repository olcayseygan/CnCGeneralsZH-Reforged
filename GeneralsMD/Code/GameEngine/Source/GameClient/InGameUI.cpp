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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// InGameUI.cpp ///////////////////////////////////////////////////////////////////////////////////
// Implementation of in-game user interface singleton inteface
// Author: Michael S. Booth, March 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicThread.h"
#if !defined(_WIN32)
#include <unistd.h>		// getpid, for the replay checkpoint folder
#endif
#include "Lib/Clock.h"
#include "Lib/WideCharFns.h"

#define DEFINE_SHADOW_NAMES

#include "Common/LocalFileSystem.h"
#include "Common/ActionManager.h"
#include "Common/DrawnPath.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/RandomValue.h"
#include "Common/GameType.h"
#include "Common/MessageStream.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/Upgrade.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/TunnelTracker.h"
#include "Common/BuildAssistant.h"
#include "Common/Recorder.h"
#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"
#include "Common/OptionsCatalog.h"

#include "GameClient/Anim2D.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/ControlBarSidebar.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/Eva.h"
#include "GameClient/GameText.h"
#include "Common/UserPreferences.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameConsole.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/GameWindowID.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/ScoreScreenLayout.h"
#include "GameClient/Shell.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/Mouse.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/View.h"
#include "GameClient/TerrainVisual.h"	
#include "GameClient/CinemaDirector.h"
#include "GameClient/ControlBar.h"
#include "Common/MessageBoxFlags.h"	// MessageBoxWrapper and its flags
#include "GameClient/Display.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/RadarContactMarks.h"
#include "GameClient/ReachOverlay.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/Shadow.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/GroupPanel.h"
#include "GameClient/DirectStrikeHud.h"
#include "GameClient/MatchClock.h"
#include "GameClient/DirectStrikeEndScreen.h"
#include "GameClient/AirliftPanel.h"
#include "GameClient/UIStyle.h"
#include "GameClient/DirectStrikeTheme.h"
#include "GameClient/ObserverHudLayout.h"
#include "GameClient/MapUtil.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/Online/OnlineQueues.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIGuard.h"
#include "GameLogic/ApocalypseDirector.h"
#include "GameLogic/AodDirector.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/ParticleUplinkCannonUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Object.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/MobMemberSlavedUpdate.h"//ML
#include "GameLogic/Module/SpawnBehavior.h"

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


// ------------------------------------------------------------------------------------------------
/** How the structure riding the cursor is drawn. `BuildPlacementOpacity` and
	* `BuildPlacementShadows` in GameData.ini; the defaults are what the game always did. */
static void dressPlacementPreview( Drawable *draw )
{
	if( draw == NULL )
		return;

	draw->setDrawableOpacity( TheGlobalData->m_buildPlacementOpacity );
	draw->setShadowsEnabled( TheGlobalData->m_buildPlacementShadows );
}
static const RGBColor illegalBuildColor = { 1.0, 0.0, 0.0 };

// ------------------------------------------------------------------------------------------------
// Layout of the production list; drawProductionList() further down is where it is used.
// ------------------------------------------------------------------------------------------------
enum
{
	PRODUCTION_LIST_LIFT		= 2,		///< clearance between the queue tray's foot and the radar frame's top
	HUD_CELL_CAMEO_W				= 39,		///< a production or superweapon cameo: the general's power shortcut's
	HUD_CELL_CAMEO_H				= 27,
	HUD_CELL_POINT_SIZE			= 7,		///< the seconds and the "xN" written in a cameo
	HUD_CELL_TRAY_CELL_W		= 80,		///< the general's powers tray's cell piece at 1080, round its
	HUD_CELL_TRAY_CELL_H		= 70,		///< power button; a cameo's cell keeps the same margins to scale
	HUD_CELL_TRAY_BUTTON_W	= 75,
	HUD_CELL_TRAY_BUTTON_H	= 60,
	HUD_TRAY_TAB_W					= 40,		///< the "+N" tab's picture at 1080, see hudTab
	HUD_TRAY_TAB_H					= 28,
	HUD_TRAY_TAB_JOIN				= 6,		///< its rows lying over the tray's outer edge
	HUD_TRAY_TAB_TEXT_INSET_X	= 4,	///< the text's room inside the rest of it
	HUD_TRAY_TAB_TEXT_INSET_Y	= 2,
	HUD_HERO_BAR_W					= 3,		///< the hero cell's upright health bar, beside the cameo
	SUPERWEAPON_TRAY_DROP		= 4,		///< between the info strip's foot and the superweapon tray's top
	SIDEBAR_SUPERWEAPON_ROWS	= 6,	///< the Red Alert style layout's named superweapon rows before the "+N" tab
	HUD_READY_GLOW_PERIOD_MS	= 1400,	///< one breath of a charged superweapon's glow
	HUD_READY_SHIMMER_RISE	= 85		///< percent of the cameo the shimmer's top edge climbs in a breath
};

//-------------------------------------------------------------------------------------------------
/** Every number above is an 800x600 one, the resolution the command bar was drawn at, so every one
	* of them goes through here to become pixels.  The scale is the command bar's own, one factor for
	* both axes, and not the display over 800 wide. */
//-------------------------------------------------------------------------------------------------
static Int stripPixels( Int nominal )
{
	return REAL_TO_INT_CEIL( nominal * ControlBarHudPageScale() );
}

//-------------------------------------------------------------------------------------------------
/** The Red Alert style layout is on screen (docs/plans/HUD-SIDEBAR-LAYOUT.md); observers, replays and
	* a defeated player keep Classic, which isSidebarLayout says. */
//-------------------------------------------------------------------------------------------------
static Bool sidebarLayoutOn( void )
{
	return TheGlobalData->m_hudLayout == HUD_LAYOUT_SIDEBAR && TheControlBar != NULL && TheControlBar->isSidebarLayout();
}

/// every rectangle of the layout on screen, in the HudWidth area
static HudLayoutRects hudRects( void )
{
	HudLayoutRects r;
	HudLayoutForScreen( TheDisplay->getWidth(), TheDisplay->getHeight(),
											sidebarLayoutOn() ? HUD_LAYOUT_SIDEBAR : HUD_LAYOUT_CLASSIC, TheGlobalData->m_hudWidth, &r );
	return r;
}

/// the world the interface leaves open, whose sides the pieces EA pinned to the screen's edges stand on:
/// the HudWidth area under Classic, so a 16:9 choice on a 32:9 screen keeps them in the middle, and left
/// of the sidebar and above the strip under Red Alert style
static IRegion2D hudPlay( void )
{
	return hudRects().play;
}

//-------------------------------------------------------------------------------------------------
/** One strip's tray, the general's powers tray's outline laid round a group of cells that stands
	* against another edge of the screen or grows the other way.  The roles are the powers tray's own,
	* named from the screen edge out: the edge down the open side of each row's innermost cell, the
	* foot outside row 0, the edge over a cell no row beyond covers, the open side's two corners, and
	* the step filling the notch where a short row ends on a longer one. */
//-------------------------------------------------------------------------------------------------
struct HudTray
{
	const Image *cell;							///< under every cell, NULL with no tray: then nothing is drawn round them
	const Image *open;							///< down the open side
	const Image *foot;							///< outside row 0
	const Image *uncovered;					///< over the far side of a cell no row covers
	const Image *cornerFoot;				///< the open side's corner at the foot
	const Image *cornerUncovered;		///< the open side's corner at the far side
	const Image *step;							///< open side by far side, in a short row's cell where it meets the longer
	Int openW, footH, uncoveredH;		///< each edge's thickness at the cells' scale
	const Image *close;							///< down the screen side, openW thick, where a tray closes there too (the superweapons')
	const Image *closeFoot;					///< its corner at the foot
	const Image *closeUncovered;		///< and at the far side
};

//-------------------------------------------------------------------------------------------------
/** The local player's build of the command bar for the cameos drawn over the battlefield, the
	* production ones and the superweapon ones.  The cameos sit together in one tray the way the
	* general's powers do, each in the tray's cell piece with the margins round a power button scaled
	* to the cameo, and inside the button's 1-pixel border.  A scheme with no tray, Classic's, leaves
	* the border alone, as its shipped column does.  What else is the side's is the plate under every
	* readout, a piece of its own button sheet (<side>ProHudCellPlate in ReforgedHud<side>Powers).  A
	* sheet without the plate yet gets HudReadout_draw's solid one.  An observer wears his own bar's
	* (ObserverPro, ReforgedHudObsPieces); a side no build is named for wears America's. */
//-------------------------------------------------------------------------------------------------
struct HudCellStyle
{
	const Image *plate;			///< under a readout, stretched to its text
	Int cameoW, cameoH;			///< the cameo, the size of the general's power shortcut's
	Int marginX, marginY;		///< from the cell's edge to the cameo's, each side
	Int cellW, cellH;				///< a cell, and the step from one to the next: cells touch, as the tray's do
	const Image *tabUp;			///< the queue's "+N" tab, NULL until the sheet has one
	const Image *tabDown;		///< the superweapons' "+N" tab
	const Image *readyGlow;	///< a charged superweapon's light, NULL until the sheet has one
	const Image *readyShimmer;	///< the band climbing through it
	HudTray production;			///< against the screen's left edge, one row
	HudTray superweapon;		///< against the right edge, rows going down
};

/// a tray piece the powers tray has no use for, the mirror of one it has; until a sheet draws it the
/// unmirrored one stands in, its light on the wrong side
static const Image *mirroredTrayPiece( const char *prefix, const char *piece, const Image *standIn )
{
	AsciiString name;
	name.format( "%sProTray%s", prefix, piece );
	const Image *image = TheMappedImageCollection->findImageByName( name );
	return image != NULL ? image : standIn;
}

/// a house colour as the player's units show it: the colour is the texture's tint, darker than what a
/// lit model gives back (Chocolate is 90,50,20 and its units read about 234,121,43), so its hue and
/// saturation are kept and its value raised to 0.9, opaque
static Color readableHouseColor( Color house )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( house, &r, &g, &b, &a );
	const Int brightest = max( (Int)r, max( (Int)g, (Int)b ) );
	const Real lift = brightest > 0 ? 0.9f * 255.0f / brightest : 1.0f;
	return GameMakeColor( min( REAL_TO_INT( r * lift + 0.5f ), 255 ), min( REAL_TO_INT( g * lift + 0.5f ), 255 ),
												min( REAL_TO_INT( b * lift + 0.5f ), 255 ), 255 );
}

/// the edges as thick, against the cell, as the powers tray's are against its cell
static void sizeHudTray( HudTray *tray, Real scaleX, Real scaleY )
{
	tray->openW = tray->open ? REAL_TO_INT_CEIL( tray->open->getImageWidth() * scaleX ) : 0;
	tray->footH = tray->foot ? REAL_TO_INT_CEIL( tray->foot->getImageHeight() * scaleY ) : 0;
	tray->uncoveredH = tray->uncovered ? REAL_TO_INT_CEIL( tray->uncovered->getImageHeight() * scaleY ) : 0;
}

static Bool isAsciiDigit( WideChar c )
{
	return c >= u'0' && c <= u'9';
}

/** Watching a player, the money readout stands in the box right of his name on the observer's panel:
	* right-aligned to the box's inner edge, a point smaller at a time while the figure is wider than the
	* box (digits are never shortened).  The panel's frame took the readout's middle before, where the
	* player's own bar has room for it.  The box is the panel's top row from the name's right edge to the
	* panel's frame, 8 of its 503 pixels at 1920, plus as much again for air.  The figure reads as the
	* HUD's money does, "$ 10,000", on one line: the window is made as wide as the figure unwrapped,
	* measured apart from the gadget, and marked one-line, since the gadget's own string wraps ten pixels
	* inside the window's width ("$" over "10,000"). */
static UnicodeString groupThousands( const UnicodeString& text );
static void fitObserverMoney( GameWindow *moneyWin, Int amount, Bool unlimited )
{
	GameWindow *info = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "ControlBar.wnd:ObserverPlayerInfoWindow" ) );
	GameWindow *name = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "ControlBar.wnd:StaticTextPlayerName" ) );
	TextData *data = (TextData *)moneyWin->winGetUserData();
	if( info == NULL || name == NULL || info->winIsHidden() || data == NULL || data->text == NULL )
		return;
	UnicodeString figure;
	if( unlimited )
		figure = TheGameText->fetch( "GUI:StartingMoneyUnlimited" );
	else
	{
		figure.format( TheGameText->fetch( "GUI:ControlBarMoneyDisplay" ), amount );
		figure = groupThousands( figure );
	}
	if( data->text->getText().compare( figure ) != 0 )
		GadgetStaticTextSetText( moneyWin, figure );
	Int infoX, infoY, infoW, infoH, nameX, nameY, nameW, nameH;
	info->winGetScreenPosition( &infoX, &infoY );
	info->winGetSize( &infoW, &infoH );
	name->winGetScreenPosition( &nameX, &nameY );
	name->winGetSize( &nameW, &nameH );
	const Int inset = max( infoW * 16 / 503, 2 );
	const Int boxLeft = nameX + nameW + inset / 2;
	const Int boxRight = infoX + infoW - inset;
	if( boxRight - boxLeft < 8 )
		return;
	// the figure's width on one line, in the window's font and then a point smaller at a time
	GameFont *font = moneyWin->winGetFont();
	DisplayString *measure = TheDisplayStringManager->newDisplayString();
	measure->setText( figure );
	Int textW = 0, textH = 0;
	for( ;; )
	{
		measure->setFont( font );
		measure->getSize( &textW, &textH );
		if( font == NULL || textW <= boxRight - boxLeft || font->pointSize <= 6 )
			break;
		font = TheFontLibrary->getFont( font->nameString, font->pointSize - 1, font->bold );
	}
	TheDisplayStringManager->freeDisplayString( measure );
	if( font != NULL && font != moneyWin->winGetFont() )
		GadgetStaticTextSetFont( moneyWin, font );
	Int parentX = 0, parentY = 0;
	if( moneyWin->winGetParent() )
		moneyWin->winGetParent()->winGetScreenPosition( &parentX, &parentY );
	data->centered = TRUE;
	// the static text's draw wraps ten pixels inside the window's width, so a window six wider than
	// the figure broke "$ 10,000" at its space and the box cut the second line off; a one-line window
	// is never wrapped
	moneyWin->winSetStatus( WIN_STATUS_ONE_LINE );
	const Int slack = 6;
	moneyWin->winSetPosition( boxRight - textW - slack - parentX, nameY - parentY );
	moneyWin->winSetSize( textW + slack, nameH );
}

/// the amount without the currency sign the language's text puts before it: "$ 10000" reads "10000"
static UnicodeString amountOnly( const UnicodeString& text )
{
	const Int length = text.getLength();
	Int first = 0;
	while( first < length && !isAsciiDigit( text.getCharAt( first ) ) && text.getCharAt( first ) != u'-' )
		first++;
	UnicodeString out;
	for( Int i = first; i < length; i++ )
		out.concat( text.getCharAt( i ) );
	return out;
}

/// text with a comma every three digits in each run of digits: "$ 10000" reads "$ 10,000"
static UnicodeString groupThousands( const UnicodeString& text )
{
	UnicodeString out;
	const Int length = text.getLength();
	for( Int i = 0; i < length; i++ )
	{
		out.concat( text.getCharAt( i ) );
		Int digitsAfter = 0;
		while( i + 1 + digitsAfter < length && isAsciiDigit( text.getCharAt( i + 1 + digitsAfter ) ) )
			digitsAfter++;
		if( isAsciiDigit( text.getCharAt( i ) ) && digitsAfter > 0 && digitsAfter % 3 == 0 )
			out.concat( (WideChar)u',' );
	}
	return out;
}

/** The IncomeReadout option's readings: the bar's own player's money earned, once a second, read on
	* every pass whether the option is on or not so that switching it on mid-match shows a figure at
	* once.  The score keeper is only read; nothing here reaches the logic.  Another player, or a frame
	* behind the last reading (the next match), starts them again. */
static IncomeReadings s_incomeReadings;
static const Player *s_incomePlayer = NULL;
static Int s_incomeShown = -1;			///< the figure drawIncomeReadout puts beside the money
static Int s_incomeModeShown = 0;		///< and how, INCOME_READOUT_OFF for not at all
static DisplayString *s_incomeText = NULL;	///< its words, made on first use

/// the income a second or a minute for `player` (NULL: none, which also forgets the readings), and in
/// `mode` which of the two, INCOME_READOUT_OFF when the option is off
static Int incomeReadoutRate( const Player *player, Int *mode )
{
	*mode = INCOME_READOUT_OFF;
	if( player != s_incomePlayer )
	{
		s_incomeReadings.clear();
		s_incomePlayer = player;
	}
	if( player == NULL )
		return -1;
	s_incomeReadings.sample( TheGameLogic->getFrameForClient(),
		const_cast< Player * >( player )->getScoreKeeper()->getTotalMoneyEarned(), LOGICFRAMES_PER_SECOND );
	const Int option = TheGlobalData->m_incomeReadout;
	if( option != INCOME_READOUT_PER_SECOND && option != INCOME_READOUT_PER_MINUTE )
		return -1;
	*mode = option;
	const UnsignedInt unit = ( option == INCOME_READOUT_PER_MINUTE ? 60 : 1 ) * LOGICFRAMES_PER_SECOND;
	return s_incomeReadings.rate( unit, LOGICFRAMES_PER_SECOND );
}

UnicodeString HudMoney_format( Int amount )
{
	UnicodeString text;
	text.format( TheGameText->fetch( "GUI:ControlBarMoneyDisplay" ), amount );
	if( TheControlBar && TheControlBar->getControlBarSchemeManager()->isMoneyGrouped() )
		text = groupThousands( text );
	return text;
}

static HudCellStyle hudCellStyle( void )
{
	const AsciiString side = ThePlayerList->getLocalPlayer()->getSide();
	HudCellStyle style;
	style.cameoW = stripPixels( HUD_CELL_CAMEO_W );
	style.cameoH = stripPixels( HUD_CELL_CAMEO_H );
	style.marginX = REAL_TO_INT_CEIL( style.cameoW * ( HUD_CELL_TRAY_CELL_W - HUD_CELL_TRAY_BUTTON_W ) / ( 2.0f * HUD_CELL_TRAY_BUTTON_W ) );
	style.marginY = REAL_TO_INT_CEIL( style.cameoH * ( HUD_CELL_TRAY_CELL_H - HUD_CELL_TRAY_BUTTON_H ) / ( 2.0f * HUD_CELL_TRAY_BUTTON_H ) );
	style.cellW = style.cameoW + 2 * style.marginX;
	style.cellH = style.cameoH + 2 * style.marginY;

	const char *prefix = "America";
	if( side.startsWith( "China" ) || side.startsWith( "Boss" ) )
		prefix = "China";
	else if( side.startsWith( "GLA" ) )
		prefix = "Gla";
	else if( side.startsWith( "Turkiye" ) )
		prefix = "Turkiye";
	else if( side.startsWith( "Observer" ) )
		prefix = "Observer";

	AsciiString name;
	name.format( "%sProHudCellPlate", prefix );
	style.plate = TheMappedImageCollection->findImageByName( name );
	name.format( "%sProTrayTabUp", prefix );
	style.tabUp = TheMappedImageCollection->findImageByName( name );
	name.format( "%sProTrayTabDown", prefix );
	style.tabDown = TheMappedImageCollection->findImageByName( name );
	name.format( "%sProHudReadyGlow", prefix );
	style.readyGlow = TheMappedImageCollection->findImageByName( name );
	name.format( "%sProHudReadyShimmer", prefix );
	style.readyShimmer = TheMappedImageCollection->findImageByName( name );

	// the production tray is the powers tray mirrored left for right, the superweapon tray the same
	// tray turned top for bottom, which leaves every one of its pieces the right way up but the step
	const Image *const *tray = TheControlBar->getControlBarSchemeManager()->getPowerTray();
	const HudTray none = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0 };
	style.production = none;
	style.superweapon = none;
	if( tray == NULL )
		return style;

	const Image *cell = tray[ ControlBarScheme::TRAY_CELL ];
	const HudTray production =
	{
		cell,
		mirroredTrayPiece( prefix, "EdgeRight", tray[ ControlBarScheme::TRAY_EDGE_LEFT ] ),
		tray[ ControlBarScheme::TRAY_EDGE_BOTTOM ],
		tray[ ControlBarScheme::TRAY_EDGE_TOP ],
		mirroredTrayPiece( prefix, "CornerBottomRight", tray[ ControlBarScheme::TRAY_CORNER_BL ] ),
		mirroredTrayPiece( prefix, "CornerTopRight", tray[ ControlBarScheme::TRAY_CORNER_TL ] ),
		mirroredTrayPiece( prefix, "StepRight", tray[ ControlBarScheme::TRAY_STEP ] ),
		0, 0, 0
	};
	const HudTray superweapon =
	{
		cell,
		tray[ ControlBarScheme::TRAY_EDGE_LEFT ],
		tray[ ControlBarScheme::TRAY_EDGE_TOP ],
		tray[ ControlBarScheme::TRAY_EDGE_BOTTOM ],
		tray[ ControlBarScheme::TRAY_CORNER_TL ],
		tray[ ControlBarScheme::TRAY_CORNER_BL ],
		mirroredTrayPiece( prefix, "StepDown", tray[ ControlBarScheme::TRAY_STEP ] ),
		0, 0, 0,
		// closed on the screen side as well, the left edge and its corners mirrored, so its steel stands
		// whole against the screen's edge rather than running off it
		mirroredTrayPiece( prefix, "EdgeRight", tray[ ControlBarScheme::TRAY_EDGE_LEFT ] ),
		mirroredTrayPiece( prefix, "CornerTopRight", tray[ ControlBarScheme::TRAY_CORNER_TL ] ),
		mirroredTrayPiece( prefix, "CornerBottomRight", tray[ ControlBarScheme::TRAY_CORNER_BL ] )
	};
	style.production = production;
	style.superweapon = superweapon;
	const Real scaleX = (Real)style.cellW / cell->getImageWidth();
	const Real scaleY = (Real)style.cellH / cell->getImageHeight();
	sizeHudTray( &style.production, scaleX, scaleY );
	sizeHudTray( &style.superweapon, scaleX, scaleY );
	return style;
}

//-------------------------------------------------------------------------------------------------
/** A strip's cells and the tray round them, drawn piece by piece as drawPowerTraySlot draws a
	* power's slot.  Cell i is row i / columns, place i % columns counted in from the screen edge;
	* `openSide` is +1 when the open side, away from the screen edge, is the right one and -1 when it
	* is the left, `footSide` +1 when row 0's outer edge is below the cells and -1 above.  The screen
	* edge's side gets no edge, as the powers tray's right side has none. */
//-------------------------------------------------------------------------------------------------
static void drawHudTray( const HudTray &tray, const IRegion2D *cells, Int count, Int columns, Int openSide, Int footSide )
{
	if( tray.cell == NULL || count == 0 )
		return;

	const Int rows = ( count + columns - 1 ) / columns;
	auto rowLength = [ & ]( Int row ) { return row >= 0 && row < rows ? MIN( columns, count - row * columns ) : 0; };
	auto piece = []( const Image *image, Int lx, Int ly, Int hx, Int hy )
	{
		// a piece the art leaves out leaves the battlefield showing there
		if( image != NULL && hx > lx && hy > ly )
			TheDisplay->drawImage( image, lx, ly, hx, hy );
	};

	for( Int each = 0; each < count; each++ )
	{
		const IRegion2D &c = cells[ each ];
		const Int row = each / columns, place = each % columns;
		const Bool innermost = place == rowLength( row ) - 1;
		const Bool covered = place < rowLength( row + 1 );
		// this short row's innermost cell stands on a longer row: the step fills the notch
		const Bool stepHere = innermost && row > 0 && place + 1 < rowLength( row - 1 );
		// the short row beyond ends over this cell's screen-side neighbour: the step closes that corner
		const Bool stepBeside = !covered && place > 0 && place - 1 < rowLength( row + 1 );

		const Int openLo = openSide > 0 ? c.hi.x : c.lo.x - tray.openW;
		const Int openHi = openLo + tray.openW;
		const Int footLo = footSide > 0 ? c.hi.y : c.lo.y - tray.footH;
		const Int footHi = footLo + tray.footH;
		const Int farLo = footSide > 0 ? c.lo.y - tray.uncoveredH : c.hi.y;
		const Int farHi = farLo + tray.uncoveredH;
		const Int stepLo = footSide > 0 ? c.hi.y - tray.uncoveredH : c.lo.y;
		const Int stepHi = stepLo + tray.uncoveredH;
		const Int notch = stepHere ? tray.uncoveredH : 0;
		const Int shorten = stepBeside ? tray.openW : 0;

		piece( tray.cell, c.lo.x, c.lo.y, c.hi.x, c.hi.y );
		if( !covered )
			piece( tray.uncovered, c.lo.x + ( openSide > 0 ? shorten : 0 ), farLo, c.hi.x - ( openSide > 0 ? 0 : shorten ), farHi );
		if( innermost )
		{
			piece( tray.open, openLo, c.lo.y + ( footSide > 0 ? 0 : notch ), openHi, c.hi.y - ( footSide > 0 ? notch : 0 ) );
			if( stepHere )
				piece( tray.step, openLo, stepLo, openHi, stepHi );
			if( !covered )
				piece( tray.cornerUncovered, openLo, farLo, openHi, farHi );
		}
		if( row == 0 )
		{
			piece( tray.foot, c.lo.x, footLo, c.hi.x, footHi );
			if( innermost )
				piece( tray.cornerFoot, openLo, footLo, openHi, footHi );
		}
		// a tray closed on the screen side too: its edge down every row's outermost cell, cornered at the
		// foot and the far side.  The caller leaves openW free between the cells and the screen's edge
		if( place == 0 && tray.close != NULL )
		{
			const Int closeLo = openSide > 0 ? c.lo.x - tray.openW : c.hi.x;
			const Int closeHi = closeLo + tray.openW;
			piece( tray.close, closeLo, c.lo.y, closeHi, c.hi.y );
			if( row == 0 )
				piece( tray.closeFoot, closeLo, footLo, closeHi, footHi );
			if( !covered )
				piece( tray.closeUncovered, closeLo, farLo, closeHi, farHi );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A charged superweapon's light: its owner's colour cast up off the foot of its ring over the
	* cameo, strongest at the foot and gone by the top, breathing once every HUD_READY_GLOW_PERIOD_MS on the
	* logic clock, so it holds still while the game is paused.  The piece, <side>ProHudReadyGlow, is
	* grey light on black drawn additively: the vertex colour, the owner's scaled by the breath,
	* multiplies it (Render2DClass draws with GRADIENT_MODULATE) and the sum brightens the cameo
	* without hiding it.  Over it <side>ProHudReadyShimmer, a band of light as tall as the cameo and
	* brightest along its top edge, climbs from the foot to HUD_READY_SHIMMER_RISE of the cameo
	* once a breath and fades as it goes, clipped to the cameo: Tools/hud_command_bar.py shimmer_rise,
	* the owner's pick from the round 8 preview.  A piece the sheet lacks is left out. */
//-------------------------------------------------------------------------------------------------
static void drawReadyGlow( const Image *glow, const Image *shimmer, const IRegion2D &cameo, Color owner )
{
	const Int period = LOGICFRAMES_PER_SECOND * HUD_READY_GLOW_PERIOD_MS / 1000;
	const Real phase = (Real)( TheGameLogic->getFrame() % period ) / period;
	const Real breath = 0.55f + 0.45f * ( 0.5f - 0.5f * Cos( 2.0f * PI * phase ) );

	UnsignedByte r, g, b, a;
	GameGetColorComponents( owner, &r, &g, &b, &a );
	auto tint = [ r, g, b ]( Real light )
	{
		return GameMakeColor( REAL_TO_INT( r * light ), REAL_TO_INT( g * light ), REAL_TO_INT( b * light ), 255 );
	};

	if( glow != NULL )
		TheDisplay->drawImage( glow, cameo.lo.x, cameo.lo.y, cameo.hi.x, cameo.hi.y, tint( breath ),
													 Display::DRAW_IMAGE_ADDITIVE );
	if( shimmer == NULL )
		return;

	// the band's top edge stands rise of the cameo up off the foot; below the cameo it is cut
	const Real rise = HUD_READY_SHIMMER_RISE / 100.0f * phase;
	const Real fade = (Real)pow( 1.0f - phase, 0.7 );
	const Int height = cameo.hi.y - cameo.lo.y;
	const Int top = cameo.lo.y + REAL_TO_INT( floor( height * ( 1.0f - rise ) + 0.5f ) ) - 1;
	IRegion2D clip = cameo;
	TheDisplay->setClipRegion( &clip );
	TheDisplay->drawImage( shimmer, cameo.lo.x, top, cameo.hi.x, top + height, tint( fade * breath ),
												 Display::DRAW_IMAGE_ADDITIVE );
	TheDisplay->enableClipping( FALSE );
}

/// no tab: the cells all fit
static IRegion2D noHudTab( void )
{
	IRegion2D none;
	none.lo.x = none.lo.y = none.hi.x = none.hi.y = 0;
	return none;
}

//-------------------------------------------------------------------------------------------------
/** A full tray's "+N" tab, a tag of its own pinned to the tray's far side at the screen edge: the
	* superweapons' hangs from the tray's bottom against the right edge (<side>ProTrayTabDown), the
	* queue's sticks up from its top against the left edge (<side>ProTrayTabUp).  Each is one fixed
	* picture, HUD_TRAY_TAB_W by HUD_TRAY_TAB_H at 1080 and scaled as the powers tray is, whose
	* HUD_TRAY_TAB_JOIN rows nearest the tray lie over the tray's own outer edge so the art can draw
	* the pin there.  `screenSide` is +1 for the right edge and -1 for the left, `edge` that edge's x
	* (hudPlay's side, or wherever the tray stands), `tray` the tray's outer line on its far side,
	* `hangs` +1 for a tab below that line and -1 above.  The text is
	* centred in the rest, inset HUD_TRAY_TAB_TEXT_INSET_X and HUD_TRAY_TAB_TEXT_INSET_Y.  A sheet
	* without the piece yet gets a dark slab. */
//-------------------------------------------------------------------------------------------------
static IRegion2D hudTab( Int screenSide, Int edge, Int tray, Int hangs )
{
	const Real k = TheDisplay->getHeight() / 1080.0f;
	const Int width = REAL_TO_INT_CEIL( HUD_TRAY_TAB_W * k );
	const Int height = REAL_TO_INT_CEIL( HUD_TRAY_TAB_H * k );
	const Int join = REAL_TO_INT_CEIL( HUD_TRAY_TAB_JOIN * k );
	IRegion2D tab;
	tab.lo.x = screenSide > 0 ? edge - width : edge;
	tab.hi.x = tab.lo.x + width;
	tab.lo.y = hangs > 0 ? tray - join : tray + join - height;
	tab.hi.y = tab.lo.y + height;
	return tab;
}

static void drawHudTab( const Image *art, const IRegion2D &tab, Int hangs, DisplayString *text )
{
	if( art != NULL )
		TheDisplay->drawImage( art, tab.lo.x, tab.lo.y, tab.hi.x, tab.hi.y );
	else
		TheDisplay->drawFillRect( tab.lo.x, tab.lo.y, tab.hi.x - tab.lo.x, tab.hi.y - tab.lo.y, GameMakeColor( 2, 4, 5, 230 ) );

	const Real k = TheDisplay->getHeight() / 1080.0f;
	const Int join = REAL_TO_INT_CEIL( HUD_TRAY_TAB_JOIN * k );
	const Int insetX = REAL_TO_INT_CEIL( HUD_TRAY_TAB_TEXT_INSET_X * k );
	const Int insetY = REAL_TO_INT_CEIL( HUD_TRAY_TAB_TEXT_INSET_Y * k );
	IRegion2D room;
	room.lo.x = tab.lo.x + insetX;
	room.hi.x = tab.hi.x - insetX;
	room.lo.y = tab.lo.y + insetY + ( hangs > 0 ? join : 0 );
	room.hi.y = tab.hi.y - insetY - ( hangs > 0 ? 0 : join );

	Int textWidth = 0, textHeight = 0;
	text->getSize( &textWidth, &textHeight );
	text->setClipRegion( &room );
	text->draw( ( room.lo.x + room.hi.x - textWidth ) / 2, ( room.lo.y + room.hi.y - textHeight ) / 2,
							GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
/** The cameo a special power is fired from.  A SpecialPowerTemplate carries no art of its own -
	* the picture lives on whichever command button launches it - so the button list is what has to
	* be asked.  A power with no button anywhere (a scripted one) draws as an empty box
	* with its countdown in it, which is still the timer it was asked for. */
//-------------------------------------------------------------------------------------------------
static const CommandButton *powerButton( const SpecialPowerTemplate *powerTemplate )
{
	if( powerTemplate == NULL || TheControlBar == NULL )
		return NULL;

	for( const CommandButton *button = TheControlBar->getCommandButtons(); button;
			 button = button->getNext() )
	{
		if( button->getSpecialPowerTemplate() == powerTemplate && button->getButtonImage() )
			return button;
	}

	return NULL;
}

static const Image *superweaponCameo( const SpecialPowerTemplate *powerTemplate )
{
	const CommandButton *button = powerButton( powerTemplate );
	return button ? button->getButtonImage() : NULL;
}

//-------------------------------------------------------------------------------------------------
/** The border a command button of `type` wears on the bar and in the tray, the scheme's
	* ButtonBorder* colour through setCommandBarBorder; GAME_COLOR_UNDEFINED draws none, as
	* W3DPushButton draws none. */
//-------------------------------------------------------------------------------------------------
static Color hudCellBorder( CommandButtonMappedBorderType type )
{
	if( type == COMMAND_BUTTON_BORDER_BUILD )
		return TheControlBar->getBuildBorderColor();
	if( type == COMMAND_BUTTON_BORDER_UPGRADE )
		return TheControlBar->getUpgradeBorderColor();
	if( type == COMMAND_BUTTON_BORDER_ACTION )
		return TheControlBar->getActionBorderColor();
	if( type == COMMAND_BUTTON_BORDER_SYSTEM )
		return TheControlBar->getSystemBorderColor();
	return GAME_COLOR_UNDEFINED;
}

/// the cell round a cameo's box: the tray's cell piece, and what a click on the cameo takes
static IRegion2D hudCellBox( const HudCellStyle &style, const IRegion2D &cameo )
{
	IRegion2D box;
	box.lo.x = cameo.lo.x - style.marginX;
	box.lo.y = cameo.lo.y - style.marginY;
	box.hi.x = cameo.hi.x + style.marginX;
	box.hi.y = cameo.hi.y + style.marginY;
	return box;
}

/// the button's own 1-pixel border, just outside the cameo, the way W3DPushButton draws it
static void drawHudCellBorder( const IRegion2D &cameo, Color border )
{
	if( border != GAME_COLOR_UNDEFINED )
		TheDisplay->drawOpenRect( cameo.lo.x - 1, cameo.lo.y - 1, cameo.hi.x - cameo.lo.x + 2, cameo.hi.y - cameo.lo.y + 2,
															1, border );
}

/// a band `width` thick just inside the box lo.x, lo.y to hi.x, hi.y, on all four sides
static void fillFrame( Int loX, Int loY, Int hiX, Int hiY, Int width, Color color )
{
	TheDisplay->drawFillRect( loX, loY, hiX - loX, width, color );
	TheDisplay->drawFillRect( loX, hiY - width, hiX - loX, width, color );
	TheDisplay->drawFillRect( loX, loY + width, width, hiY - loY - 2 * width, color );
	TheDisplay->drawFillRect( hiX - width, loY + width, width, hiY - loY - 2 * width, color );
}

//-------------------------------------------------------------------------------------------------
/** Whose a superweapon is, charged or not: a thick ring in its owner's colour on all four sides of
	* the cameo, from one pixel over the picture's edge out to a keyline at the cell's side edge, light
	* under a dark colour and dark under a light one, so a navy, an olive or a black owner reads on
	* every side's tray.  At 1080 that is 3 pixels of colour and 1 of keyline, the owner's pick from
	* round 19 (experiments/hud-sides-modern/superweapon/sw.py, variant A); `margin` is the cell's side
	* margin, so it keeps its proportion at every HUD Size.  The readout stands inside it: its frame is
	* never under a pixel. */
//-------------------------------------------------------------------------------------------------
static void drawOwnerRing( const IRegion2D &cameo, Color owner, Int margin )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( owner, &r, &g, &b, &a );
	const Bool dark = 299 * r + 587 * g + 114 * b < 90 * 1000;
	const Color keyline = dark ? GameMakeColor( 214, 216, 220, 255 ) : GameMakeColor( 10, 10, 12, 255 );
	const Int keylineWidth = max( margin / 3, 1 );
	const Int out = max( margin - keylineWidth, 1 );
	fillFrame( cameo.lo.x - out - keylineWidth, cameo.lo.y - out - keylineWidth,
						 cameo.hi.x + out + keylineWidth, cameo.hi.y + out + keylineWidth, keylineWidth, keyline );
	fillFrame( cameo.lo.x - out, cameo.lo.y - out, cameo.hi.x + out, cameo.hi.y + out, out + 1,
						 GameMakeColor( r, g, b, 255 ) );
}

//-------------------------------------------------------------------------------------------------
/** Pointer to show while a structure rides the cursor.  Mouse::BUILD_PLACEMENT and
	* INVALID_BUILD_PLACEMENT are two names in CursorININames[] that no shipped Mouse.ini ever
	* defines, so asking for them left m_cursorInfo empty and drew nothing at all - the pointer
	* simply vanished for as long as something was being placed.  Honour the build cursors when art
	* for them does exist, and otherwise fall back to cursors that are always
	* there: the crosshair for a spot that can be built on, the no-go pointer for one that cannot. */
//-------------------------------------------------------------------------------------------------
static Mouse::MouseCursor placementCursor( Bool legal )
{
	Mouse::MouseCursor wanted = legal ? Mouse::BUILD_PLACEMENT : Mouse::INVALID_BUILD_PLACEMENT;

	if( TheMouse->m_cursorInfo[ wanted ].cursorName.isEmpty() == FALSE )
		return wanted;

	return legal ? Mouse::CROSS : Mouse::GENERIC_INVALID;

}  // end placementCursor

//-------------------------------------------------------------------------------------------------
/** The friendly enter hint names the object under the pointer, and the cursor follows what will
	* hold the unit: a building that takes a garrison gets the garrison cursor, anything else that is
	* not a structure the board one (a transport), and the rest, tunnels and the like, the plain enter.
	* A target that holds nothing is a passenger the selection is about to pick up (CommandXlat's
	* boardingContainer), so the selection's first container decides instead. */
//-------------------------------------------------------------------------------------------------
static Mouse::MouseCursor enterCursor( const Object *target )
{
	const Object *holder = target;
	if( holder != NULL && holder->getContain() == NULL )
	{
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		holder = selected->empty() ? NULL : selected->front()->getObject();
	}
	if( holder == NULL || holder->getContain() == NULL )
		return Mouse::ENTER_FRIENDLY;
	if( holder->getContain()->isGarrisonable() )
		return Mouse::GARRISON;
	if( !holder->isKindOf( KINDOF_STRUCTURE ) )
		return Mouse::BOARD;
	return Mouse::ENTER_FRIENDLY;

}  // end enterCursor

//-------------------------------------------------------------------------------------------------
/// The InGameUI singleton instance.
InGameUI *TheInGameUI = NULL;

GameWindow *m_replayWindow = NULL;

// ------------------------------------------------------------------------------------------------
struct KindOfSelectionData
{
	KindOfMaskType m_mustbeSet;
	KindOfMaskType m_mustbeClear;
	Bool m_idleOnly;

	DrawableList newlySelectedDrawables;
};
// ------------------------------------------------------------------------------------------------
static Bool kindOfUnitSelection( Drawable *test, void *userData )
{
	KindOfSelectionData *data = (KindOfSelectionData *) userData;

	if( test )
	{
		const Object *object = test->getObject();
		// Only things with objects can be selected, and the code below isn't 
		// safe unless you've verified that there is a valid object.
		if (!object)
			return FALSE;

		Bool isKindOfMatch = object->isKindOfMulti(data->m_mustbeSet, data->m_mustbeClear)
			&& ( !data->m_idleOnly || InGameUI::isIdleCombatUnit( object ) );

		// only select objects if not already selected
		if( object && isKindOfMatch 
					&& object->isLocallyControlled() 
					&& !object->isContained() 
					&& !object->getDrawable()->isSelected() 
					&& !object->isEffectivelyDead()
					&& object->getDrawable()->isMassSelectable()
					&& !object->isOffMap()
				)
		{
			// enforce optional unit cap
			if (TheInGameUI->getMaxSelectCount() > 0 && TheInGameUI->getSelectCount() >= TheInGameUI->getMaxSelectCount())
			{
				if ( !TheInGameUI->getDisplayedMaxWarning() )
				{
					TheInGameUI->setDisplayedMaxWarning( TRUE );
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:MaxSelectionSize").str(), TheInGameUI->getMaxSelectCount());
					TheInGameUI->message(msg);
				}
			}
			else
			{
				TheInGameUI->selectDrawable( test );
				TheInGameUI->setDisplayedMaxWarning( FALSE );
				data->newlySelectedDrawables.push_back(test);
				return TRUE;
			}	
		}
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
struct MatchingUnitSelectionData
{
	const ThingTemplate *templateToSelect;
	DrawableList newlySelectedDrawables;
	Bool isCarBomb;
};
// ------------------------------------------------------------------------------------------------
static Bool similarUnitSelection( Drawable *test, void *userData )
{
	MatchingUnitSelectionData *data = (MatchingUnitSelectionData *) userData;
	const ThingTemplate *selectedType = data->templateToSelect;

	if( test )
	{
		const Object *object = test->getObject();
		// Only things with objects can be selected, and the code below isn't 
		// safe unless you've verified that there is a valid object.
		if (!object)
			return FALSE;

		Bool isEquivalent = object->getTemplate()->isEquivalentTo( selectedType );
		if( data->isCarBomb && !isEquivalent && object->testStatus( OBJECT_STATUS_IS_CARBOMB ) )
		{
			isEquivalent = TRUE;
		}

		// only select objects if not already selected
		if( object && isEquivalent 
			  && object->isLocallyControlled() 
				&& !object->isContained()
				&& !( object->getDrawable()->isSelected() ) 
				&& object->getDrawable()->isMassSelectable() // And only if they can be multiply selected. (otherwise the drawable will be, but the object will not be)
				&& !object->isOffMap()
				)
		{
			// enforce optional unit cap
			if (TheInGameUI->getMaxSelectCount() > 0 && TheInGameUI->getSelectCount() >= TheInGameUI->getMaxSelectCount())
			{
				if ( !TheInGameUI->getDisplayedMaxWarning() )
				{
					TheInGameUI->setDisplayedMaxWarning( TRUE );
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:MaxSelectionSize").str(), TheInGameUI->getMaxSelectCount());
					TheInGameUI->message(msg);
				}
			}
			else
			{
				TheInGameUI->selectDrawable( test );
				TheInGameUI->setDisplayedMaxWarning( FALSE );
				data->newlySelectedDrawables.push_back(test);
				return TRUE;
			}	
		}
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void showReplayControls( void )
{
	if (m_replayWindow)
	{
		Bool show = TheGameLogic->isInReplayGame();
		m_replayWindow->winHide(!show);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void hideReplayControls( void )
{
	if (m_replayWindow)
	{
		m_replayWindow->winHide(TRUE);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void toggleReplayControls( void )
{
	if (m_replayWindow)
	{
		Bool show = TheGameLogic->isInReplayGame() && m_replayWindow->winIsHidden();
		m_replayWindow->winHide(!show);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo::SuperweaponInfo(
	ObjectID id,
	UnsignedInt timestamp,
	Bool hiddenByScript,
	Bool hiddenByScience,
	Bool ready,
  Bool evaReadyPlayed,
	const AsciiString& superweaponNormalFont, 
	Int superweaponNormalPointSize, 
	Bool superweaponNormalBold,
	Color c, 
	const SpecialPowerTemplate* spt
) :
	m_id(id),
	m_timestamp(timestamp),
	m_hiddenByScript(hiddenByScript),
	m_hiddenByScience(hiddenByScience),
	m_ready(ready),
  m_evaReadyPlayed( evaReadyPlayed ),
	m_forceUpdateText(false),
	m_nameDisplayString(NULL),
	m_timeDisplayString(NULL),
	m_color(c),
	m_powerTemplate(spt)
{
	m_nameDisplayString = TheDisplayStringManager->newDisplayString();
	m_nameDisplayString->reset();
	m_nameDisplayString->setText( UnicodeString::TheEmptyString );

	m_timeDisplayString = TheDisplayStringManager->newDisplayString();
	m_timeDisplayString->reset();
	m_timeDisplayString->setText( UnicodeString::TheEmptyString );

	setFont( superweaponNormalFont, superweaponNormalPointSize, superweaponNormalBold );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo::~SuperweaponInfo()
{
	if (m_nameDisplayString)
		TheDisplayStringManager->freeDisplayString( m_nameDisplayString );
	m_nameDisplayString = NULL;

	if (m_timeDisplayString)
		TheDisplayStringManager->freeDisplayString( m_timeDisplayString );
	m_timeDisplayString = NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::setFont(const AsciiString& superweaponNormalFont, Int superweaponNormalPointSize, Bool superweaponNormalBold)
{
	m_nameDisplayString->setFont( TheFontLibrary->getFont( superweaponNormalFont, 
		TheGlobalLanguageData->adjustFontSize(superweaponNormalPointSize), superweaponNormalBold ) );
	m_timeDisplayString->setFont( TheFontLibrary->getFont( superweaponNormalFont, 
		TheGlobalLanguageData->adjustFontSize(superweaponNormalPointSize), superweaponNormalBold ) );
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::setText(const UnicodeString& name, const UnicodeString& time)
{
	m_nameDisplayString->setText(name);
	m_timeDisplayString->setText(time);
}

// ------------------------------------------------------------------------------------------------
/** The countdowns sit straight on the battlefield, where light terrain swallows them. Put a
	* translucent plate under the whole line - the name is right-aligned to x, the time starts at
	* it - before either half is drawn. */
// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::drawBackdrop(Int x, Int y)
{
	if( m_nameDisplayString == NULL || m_timeDisplayString == NULL )
		return;

	Int nameW = m_nameDisplayString->getWidth();
	Int timeW = m_timeDisplayString->getWidth();
	Int h = (Int)getHeight();

	const Int pad = 3;
	TheDisplay->drawFillRect( x - nameW - pad, y - 1, nameW + timeW + pad*2, h + 2,
														GameMakeColor( 0, 0, 0, 130 ) );
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::drawName(Int x, Int y, Color color, Color dropColor)
{
	if (color == 0)
		color = clientColor( m_color );	// the owner's colour was stored when the timer started
 	m_nameDisplayString->draw(x - m_nameDisplayString->getWidth(), y, color, dropColor);
}

// ------------------------------------------------------------------------------------------------
void SuperweaponInfo::drawTime(Int x, Int y, Color color, Color dropColor)
{
	if (color == 0)
		color = clientColor( m_color );
 	m_timeDisplayString->draw(x, y, color, dropColor);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Real SuperweaponInfo::getHeight() const
{
	return m_nameDisplayString->getFont()->height;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void InGameUI::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version 
	* 2: Save NamedTimers, but not specifically their Info structs.  We'll recreate them.
  * 3: Added m_evaReadyPlayed boolean to transfer
*/
// ------------------------------------------------------------------------------------------------
void InGameUI::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	if( version >= 2 )
	{
		// Saving the named timer infos and their friends so we get script timers back after we load
		xfer->xferInt(&m_namedTimerLastFlashFrame);
		xfer->xferBool(&m_namedTimerUsedFlashColor);
		xfer->xferBool(&m_showNamedTimers);

		// For the timers themselves, all I need to save is the things that are used in the call to addNamedTimer.
		// It is okay to do this, because SuperweaponInfos pushes things on to a map; addNamedTimer is just a more
		// organized way to push things on the namedTimer Map.
		// addNamedTimer needs (const AsciiString& timerName, const UnicodeString& text, Bool isCountdown)
		if (xfer->getXferMode() == XFER_SAVE)
		{
			Int timerCount = m_namedTimers.size();
			xfer->xferInt( &timerCount );
			for( NamedTimerMapIt timerIter = m_namedTimers.begin(); timerIter != m_namedTimers.end(); ++timerIter )
			{
				xfer->xferAsciiString( &(timerIter->second->m_timerName) );
				xfer->xferUnicodeString( &(timerIter->second->timerText) );
				xfer->xferBool( &(timerIter->second->isCountdown) );
			}
		}
		else // iz a Load
		{
			Int timerCount;
			xfer->xferInt( &timerCount );
			for( Int timerIndex = 0; timerIndex < timerCount; ++timerIndex )
			{
				AsciiString timerName;
				UnicodeString timerText;
				Bool isCountdown;
				xfer->xferAsciiString( &timerName );
				xfer->xferUnicodeString( &timerText );
				xfer->xferBool( &isCountdown );

				addNamedTimer( timerName, timerText, isCountdown );
			}
		}
	}

	xfer->xferBool(&m_superweaponHiddenByScript);
	//xfer->xferBool(&m_inputEnabled);	// no, don't save this yet. somewhat problematic.

	if (xfer->getXferMode() == XFER_SAVE)
	{
		for (Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex)
		{
			for (SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].begin(); mapIt != m_superweapons[playerIndex].end(); ++mapIt)
			{
				AsciiString powerName = mapIt->first;
				SuperweaponList& swList = mapIt->second;
				for (SuperweaponList::iterator listIt = swList.begin(); listIt != swList.end(); ++listIt)
				{
					SuperweaponInfo* swInfo = *listIt;

					// since this list tends to be somewhat sparse, we write stuff out pretty explicitly.
					xfer->xferInt(&playerIndex);
					
					AsciiString templateName = swInfo->getSpecialPowerTemplate()->getName();

					xfer->xferAsciiString(&templateName);
					xfer->xferAsciiString(&powerName);
					xfer->xferObjectID(&swInfo->m_id);
					xfer->xferUnsignedInt(&swInfo->m_timestamp);
					xfer->xferBool(&swInfo->m_hiddenByScript);
					xfer->xferBool(&swInfo->m_hiddenByScience);
					xfer->xferBool(&swInfo->m_ready);
          if ( version >= 3 )
          {
            xfer->xferBool( &swInfo->m_evaReadyPlayed );
          }
				}
			}
		}
		Int noMorePlayers = -1;		// our "done" sentinel
		xfer->xferInt(&noMorePlayers);
	}
	else if (xfer->getXferMode() == XFER_LOAD)
	{
		for (;;)
		{
			Int playerIndex;
			xfer->xferInt(&playerIndex);

			if (playerIndex == -1)
			{
				break;	// our "done" sentinel
			}
			else if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
			{
				DEBUG_CRASH(("SWInfo bad plyrindex\n"));
				throw INI_INVALID_DATA;
			}

			AsciiString templateName;
			xfer->xferAsciiString(&templateName);
			const SpecialPowerTemplate* powerTemplate = TheSpecialPowerStore->findSpecialPowerTemplate(templateName);
			if (powerTemplate == NULL)
			{
				DEBUG_CRASH(("power %s not found\n",templateName.str()));
				throw INI_INVALID_DATA;
			}

			AsciiString powerName;
			ObjectID id;
			UnsignedInt timestamp;
			Bool hiddenByScript, hiddenByScience, ready, evaReadyPlayed;

			xfer->xferAsciiString(&powerName);
			xfer->xferObjectID(&id);
			xfer->xferUnsignedInt(&timestamp);
			xfer->xferBool(&hiddenByScript);
			xfer->xferBool(&hiddenByScience);
			xfer->xferBool(&ready);
      if ( version >= 3 )
      {
        xfer->xferBool( &evaReadyPlayed );
      }
      else
      {
        evaReadyPlayed = ready;
      }

			// srj sez: due to order-of-operation stuff, sometimes these will already exist,
			// sometimes not. not sure why. so handle both cases. 
			SuperweaponInfo* swInfo = findSWInfo(playerIndex, powerName, id, powerTemplate);
			if (swInfo == NULL)
			{
				const Player* player = ThePlayerList->getNthPlayer(playerIndex);
				swInfo = newInstance(SuperweaponInfo)(
					id,
					timestamp,
					hiddenByScript,
					hiddenByScience,
					ready,
          evaReadyPlayed,
					m_superweaponNormalFont, 
					m_superweaponNormalPointSize, 
					TRUE,		// the countdown list is white and bold
					player->getPlayerColor(),
					powerTemplate);
				m_superweapons[playerIndex][powerName].push_back(swInfo);
			}
			else
			{
				// swInfo->m_id = id;	// redundant, already matches
				swInfo->m_timestamp = timestamp;
				swInfo->m_hiddenByScript = hiddenByScript;
				swInfo->m_hiddenByScience = hiddenByScience;
				swInfo->m_ready = ready;
        swInfo->m_evaReadyPlayed = evaReadyPlayed;
			}
			swInfo->m_forceUpdateText = true;
		
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void InGameUI::loadPostProcess( void )
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::setMouseCursor(Mouse::MouseCursor c)
{
	if (!TheMouse)
		return;

	TheMouse->setCursor(c);

	if (m_mouseMode == MOUSEMODE_GUI_COMMAND && c != Mouse::ARROW && c != Mouse::SCROLL)
		m_mouseModeCursor = c;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SuperweaponInfo* InGameUI::findSWInfo(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].find(powerName);
	if (mapIt != m_superweapons[playerIndex].end())
	{
		for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
		{
			if ((*listIt)->m_id == id)
			{
				return *listIt;
			}
		}
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::addSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	LOGIC_AFTER_TICK( [=]() { addSuperweapon( playerIndex, powerName, id, powerTemplate ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	if (powerTemplate == NULL)
		return;

	// Pro Rules or No Superweapons: a silo nobody may fire gets no countdown on everybody's screen and no "missile ready"
	if (ProRulesRefuseSpecialPower(ThePlayerList->getNthPlayer(playerIndex), powerTemplate->getSpecialPowerType()))
		return;

	// srj sez: don't allow adding the same superweapon more than once. it can happen. not sure how. (srj)
	SuperweaponInfo* swInfo = findSWInfo(playerIndex, powerName, id, powerTemplate);
	if (swInfo != NULL)
		return;

	const Player* player = ThePlayerList->getNthPlayer(playerIndex);
	Bool hiddenByScience = (powerTemplate->getRequiredScience() != SCIENCE_INVALID) && (player->hasScience(powerTemplate->getRequiredScience()) == false);

#ifndef DO_UNIT_TIMINGS
  DEBUG_LOG(("Adding superweapon UI timer\n"));
#endif
	SuperweaponInfo *info = newInstance(SuperweaponInfo)(
					id,
					-1,			// timestamp
					FALSE,	// hiddenByScript
					hiddenByScience,//Aaayeeee! This is meaningless and just clogs up the works, sez srj, nuke or repair or SHIP WITH(tm), ASAP
													// THe trouble is: There is no mechanism to clear this bit when the science is granted, thus,
													// the timer never, ever, ever get drawn.... unless the owning object is post-science constructed.
					FALSE,	// ready
          FALSE,  // evaReadyPlayed
					m_superweaponNormalFont, 
					m_superweaponNormalPointSize, 
					TRUE,		// the countdown list is white and bold
					player->getPlayerColor(),
					powerTemplate);

	m_superweapons[playerIndex][powerName].push_back(info);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::removeSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate)
{
	// logic never reads the answer, which after the tick is not known yet
	if( LogicThread::defersEffects() )
	{
		LogicThread::callAfterTick( [=]() { removeSuperweapon( playerIndex, powerName, id, powerTemplate ); } );
		return FALSE;
	}
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	DEBUG_LOG(("Removing superweapon UI timer\n"));
	SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].find(powerName);
	if (mapIt != m_superweapons[playerIndex].end())
	{
		SuperweaponList& swList = mapIt->second;
		for (SuperweaponList::iterator listIt = swList.begin(); listIt != swList.end(); ++listIt)
		{
			if ((*listIt)->m_id == id)
			{
				SuperweaponInfo *info = *listIt;
				swList.erase(listIt);
				info->deleteInstance();
				if (swList.size() == 0)
				{
					m_superweapons[playerIndex].erase(mapIt);
				}
				return TRUE;
			}
		}
	}

	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::objectChangedTeam(const Object *obj, Int oldPlayerIndex, Int newPlayerIndex)
{
	if( LogicThread::defersEffects() )
	{
		// by ID: the tick may destroy it before its end, and a dead object's timers went with it
		const ObjectID id = obj->getID();
		LogicThread::callAfterTick( [=]() {
			const Object *live = TheGameLogic->findObjectByID( id );
			if( live )
				objectChangedTeam( live, oldPlayerIndex, newPlayerIndex );
		} );
		return;
	}
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	// if we already had it listed, remove and re-add it
	if (obj && oldPlayerIndex >= 0 && newPlayerIndex >= 0)
	{
		ObjectID id = obj->getID();
		AsciiString powerName;
		for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
		{
			SpecialPowerModuleInterface* sp = (*m)->getSpecialPower();
			if (!sp)
				continue;

			const SpecialPowerTemplate *powerTemplate = sp->getSpecialPowerTemplate();
			powerName = powerTemplate->getName();

			SuperweaponMap::iterator mapIt = m_superweapons[oldPlayerIndex].find(powerName);
			Bool found = false;
			if (mapIt != m_superweapons[oldPlayerIndex].end())
			{
				for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
				{
					if ((*listIt)->m_id == id)
					{
						removeSuperweapon(oldPlayerIndex, powerName, id, powerTemplate);
						addSuperweapon(newPlayerIndex, powerName, id, powerTemplate);
						found = true;
						break;
					}
				}
			}
			if (!found)
			{
				if( TheGameLogic->getFrame() == 0 && !obj->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) &&
					obj->isKindOf( KINDOF_COMMANDCENTER ) == FALSE )
					addSuperweapon(newPlayerIndex, powerName, id, powerTemplate);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::hideObjectSuperweaponDisplayByScript(const Object *obj)
{
	setObjectSuperweaponHiddenByScript( obj->getID(), TRUE );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::showObjectSuperweaponDisplayByScript(const Object *obj)
{
	setObjectSuperweaponHiddenByScript( obj->getID(), FALSE );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::setObjectSuperweaponHiddenByScript( ObjectID objID, Bool hidden )
{
	LOGIC_AFTER_TICK( [=]() { setObjectSuperweaponHiddenByScript( objID, hidden ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	for (Int playerIndex = 0; playerIndex < MAX_PLAYER_COUNT; ++playerIndex)
	{
		for (SuperweaponMap::iterator mapIt = m_superweapons[playerIndex].begin(); mapIt != m_superweapons[playerIndex].end(); ++mapIt)
		{
			for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
			{
				if ((*listIt)->m_id == objID)
				{
					(*listIt)->m_hiddenByScript = hidden;
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::setSuperweaponDisplayEnabledByScript(Bool enable)
{
	LOGIC_AFTER_TICK( [=]() { setSuperweaponDisplayEnabledByScript( enable ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	m_superweaponHiddenByScript = !enable;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::getSuperweaponDisplayEnabledByScript(void) const
{
	// The getter asks whether the display is enabled, so it is the negation of the hidden flag.
	// Nothing calls it yet, which is why the inversion went unnoticed.
	return !m_superweaponHiddenByScript;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::addNamedTimer( const AsciiString& timerName, const UnicodeString& text, Bool isCountdown )
{
	LOGIC_AFTER_TICK( [=]() { addNamedTimer( timerName, text, isCountdown ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	NamedTimerInfo *info = newInstance( NamedTimerInfo );	
	info->m_timerName = timerName;
	info->color = m_namedTimerNormalColor;
	info->timerText = text;
	info->displayString = TheDisplayStringManager->newDisplayString();
	info->displayString->reset();
	info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerNormalFont, 
		TheGlobalLanguageData->adjustFontSize(m_namedTimerNormalPointSize), m_namedTimerNormalBold ) );
	info->displayString->setText( UnicodeString::TheEmptyString );
	info->timestamp = -1;
	info->isCountdown = isCountdown;

//	GameFont *font = info->displayString->getFont();

	removeNamedTimer(timerName);
	m_namedTimers[timerName] = info;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::removeNamedTimer( const AsciiString& timerName )
{
	LOGIC_AFTER_TICK( [=]() { removeNamedTimer( timerName ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	NamedTimerMapIt mapIt = m_namedTimers.find(timerName);
	if (mapIt != m_namedTimers.end())
	{
		TheDisplayStringManager->freeDisplayString( mapIt->second->displayString );
		mapIt->second->deleteInstance();
		m_namedTimers.erase(mapIt);
		return;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::showNamedTimerDisplay( Bool show )
{
	LOGIC_AFTER_TICK( [=]() { showNamedTimerDisplay( show ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	m_showNamedTimers = show;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const FieldParse InGameUI::s_fieldParseTable[] = 
{
	{ "MaxSelectionSize",								INI::parseInt,					NULL,		offsetof( InGameUI, m_maxSelectCount ) },

	{ "MessageColor1",									INI::parseColorInt,			NULL,		offsetof( InGameUI, m_messageColor1 ) },
	{ "MessageColor2",									INI::parseColorInt,			NULL,		offsetof( InGameUI, m_messageColor2 ) },
	{ "MessagePosition",								INI::parseICoord2D,			NULL,		offsetof( InGameUI, m_messagePosition ) },
	{ "MessageFont",										INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_messageFont ) },
	{ "MessagePointSize",								INI::parseInt,					NULL,		offsetof( InGameUI, m_messagePointSize ) },
	{ "MessageBold",										INI::parseBool,					NULL,		offsetof( InGameUI, m_messageBold ) },
	{ "MessageDelayMS",									INI::parseInt,					NULL,		offsetof( InGameUI, m_messageDelayMS ) },

	{ "MilitaryCaptionColor",						INI::parseRGBAColorInt,	NULL,		offsetof( InGameUI, m_militaryCaptionColor ) },
	{ "MilitaryCaptionPosition",				INI::parseICoord2D,			NULL,		offsetof( InGameUI, m_militaryCaptionPosition ) },

	{ "MilitaryCaptionTitleFont",				INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_militaryCaptionTitleFont ) },
	{ "MilitaryCaptionTitlePointSize",	INI::parseInt,					NULL,		offsetof( InGameUI, m_militaryCaptionTitlePointSize ) },
	{ "MilitaryCaptionTitleBold",				INI::parseBool,					NULL,		offsetof( InGameUI, m_militaryCaptionTitleBold ) },

	{ "MilitaryCaptionFont",						INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_militaryCaptionFont ) },
	{ "MilitaryCaptionPointSize",				INI::parseInt,					NULL,		offsetof( InGameUI, m_militaryCaptionPointSize ) },
	{ "MilitaryCaptionBold",						INI::parseBool,					NULL,		offsetof( InGameUI, m_militaryCaptionBold ) },

	{ "MilitaryCaptionRandomizeTyping",	INI::parseBool,					NULL,		offsetof( InGameUI, m_militaryCaptionRandomizeTyping ) },
	{ "MilitaryCaptionSpeed",						SPEED_FIELD( InGameUI, m_militaryCaptionSpeed ) },

	{ "MilitaryCaptionPosition",				INI::parseICoord2D,			NULL,		offsetof( InGameUI, m_militaryCaptionPosition ) },

	{ "SuperweaponCountdownPosition",					INI::parseCoord2D,			NULL,		offsetof( InGameUI, m_superweaponPosition ) },
	{ "SuperweaponCountdownFlashDuration",		SPEED_FIELD( InGameUI, m_superweaponFlashDuration ) },
	{ "SuperweaponCountdownFlashColor",				INI::parseColorInt,			NULL,		offsetof( InGameUI, m_superweaponFlashColor ) },

	{ "SuperweaponCountdownNormalFont",				INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_superweaponNormalFont ) },
	{ "SuperweaponCountdownNormalPointSize",	INI::parseInt,					NULL,		offsetof( InGameUI, m_superweaponNormalPointSize ) },
	{ "SuperweaponCountdownNormalBold",				INI::parseBool,					NULL,		offsetof( InGameUI, m_superweaponNormalBold ) },

	{ "SuperweaponCountdownReadyFont",				INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_superweaponReadyFont ) },
	{ "SuperweaponCountdownReadyPointSize",		INI::parseInt,					NULL,		offsetof( InGameUI, m_superweaponReadyPointSize ) },
	{ "SuperweaponCountdownReadyBold",				INI::parseBool,					NULL,		offsetof( InGameUI, m_superweaponReadyBold ) },

	{ "NamedTimerCountdownPosition",					INI::parseCoord2D,			NULL,		offsetof( InGameUI, m_namedTimerPosition ) },
	{ "NamedTimerCountdownFlashDuration",			SPEED_FIELD( InGameUI, m_namedTimerFlashDuration ) },
	{ "NamedTimerCountdownFlashColor",				INI::parseColorInt,			NULL,		offsetof( InGameUI, m_namedTimerFlashColor ) },

	{ "NamedTimerCountdownNormalFont",				INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_namedTimerNormalFont ) },
	{ "NamedTimerCountdownNormalPointSize",		INI::parseInt,					NULL,		offsetof( InGameUI, m_namedTimerNormalPointSize ) },
	{ "NamedTimerCountdownNormalBold",				INI::parseBool,					NULL,		offsetof( InGameUI, m_namedTimerNormalBold ) },
	{ "NamedTimerCountdownNormalColor",				INI::parseColorInt,			NULL,		offsetof( InGameUI, m_namedTimerNormalColor ) },

	{ "NamedTimerCountdownReadyFont",					INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_namedTimerReadyFont ) },
	{ "NamedTimerCountdownReadyPointSize",		INI::parseInt,					NULL,		offsetof( InGameUI, m_namedTimerReadyPointSize ) },
	{ "NamedTimerCountdownReadyBold",					INI::parseBool,					NULL,		offsetof( InGameUI, m_namedTimerReadyBold ) },
	{ "NamedTimerCountdownReadyColor",				INI::parseColorInt,			NULL,		offsetof( InGameUI, m_namedTimerReadyColor ) },

	{ "FloatingTextTimeOut",									SPEED_FIELD( InGameUI, m_floatingTextTimeOut ) },
	{ "FloatingTextMoveUpSpeed",							SPEED_FIELD( InGameUI, m_floatingTextMoveUpSpeed ) },
	{ "FloatingTextVanishRate",								SPEED_FIELD( InGameUI, m_floatingTextMoveVanishRate ) },

	{ "PopupMessageColor",								INI::parseColorInt,					NULL,		offsetof( InGameUI, m_popupMessageColor ) },
	
	{ "DrawableCaptionFont",									INI::parseAsciiString,	NULL,		offsetof( InGameUI, m_drawableCaptionFont ) },
	{ "DrawableCaptionPointSize",							INI::parseInt,					NULL,		offsetof( InGameUI, m_drawableCaptionPointSize ) },
	{ "DrawableCaptionBold",									INI::parseBool,					NULL,		offsetof( InGameUI, m_drawableCaptionBold ) },
	{ "DrawableCaptionColor",									INI::parseColorInt,			NULL,		offsetof( InGameUI, m_drawableCaptionColor ) },

	{ "DrawRMBScrollAnchor",									INI::parseBool,					NULL,		offsetof( InGameUI, m_drawRMBScrollAnchor ) },
	{ "MoveRMBScrollAnchor",									INI::parseBool,					NULL,		offsetof( InGameUI, m_moveRMBScrollAnchor ) },

	{ "AttackDamageAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_DAMAGE_AREA] ) },
	{ "AttackScatterAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_SCATTER_AREA] ) },
	{ "AttackContinueAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_ATTACK_CONTINUE_AREA] ) },
	{ "FriendlySpecialPowerRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_FRIENDLY_SPECIALPOWER] ) },
	{ "OffensiveSpecialPowerRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_OFFENSIVE_SPECIALPOWER] ) },
	{ "SuperweaponScatterAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA] ) },

	{ "GuardAreaRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_GUARD_AREA] ) },
	{ "EmergencyRepairRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[RADIUSCURSOR_EMERGENCY_REPAIR] ) },

	{ "ParticleCannonRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_PARTICLECANNON] ) },
	{ "A10StrikeRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_A10STRIKE] ) },
	{ "CarpetBombRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_CARPETBOMB] ) },
	{ "DaisyCutterRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_DAISYCUTTER] ) },
	{ "ParadropRadiusCursor",				RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_PARADROP] ) },
	{ "SpySatelliteRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SPYSATELLITE] ) },
	{ "SpectreGunshipRadiusCursor",	RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SPECTREGUNSHIP] ) },
	{ "HelixNapalmBombRadiusCursor",RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_HELIX_NAPALM_BOMB] ) },
	
	{ "NuclearMissileRadiusCursor", RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_NUCLEARMISSILE] ) }, 
	{ "EMPPulseRadiusCursor",		  	RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_EMPPULSE] ) },
	{ "ArtilleryRadiusCursor",		  RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_ARTILLERYBARRAGE] ) },
	{ "FrenzyRadiusCursor",				  RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_FRENZY] ) },
	{ "NapalmStrikeRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_NAPALMSTRIKE] ) },
	{ "ClusterMinesRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_CLUSTERMINES] ) },
	
	{ "ScudStormRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_SCUDSTORM] ) }, 
	{ "AnthraxBombRadiusCursor",		RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_ANTHRAXBOMB] ) },
	{ "AmbushRadiusCursor",					RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_AMBUSH] ) }, 
	{ "RadarRadiusCursor",					RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[	RADIUSCURSOR_RADAR] ) },
	{ "SpyDroneRadiusCursor",				RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_SPYDRONE] ) },

	{ "ClearMinesRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_CLEARMINES] ) },
	{ "AmbulanceRadiusCursor",			RadiusDecalTemplate::parseRadiusDecalTemplate, NULL, offsetof( InGameUI, m_radiusCursors[ RADIUSCURSOR_AMBULANCE] ) },

	{ NULL,													NULL,										NULL,		0 }  // keep this last
};

//-------------------------------------------------------------------------------------------------
/** Parse MouseCursor entry */
//-------------------------------------------------------------------------------------------------
void INI::parseInGameUIDefinition( INI* ini )
{
	if( TheInGameUI )
	{
		// parse the ini weapon definition
		ini->initFromINI( TheInGameUI, TheInGameUI->getFieldParse() );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
InGameUI::InGameUI()
{
	Int i;

	for( i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		m_moveHint[ i ].pos.zero();
		m_moveHint[ i ].frame = 0;
		m_moveHint[ i ].bornMs = 0;
		m_moveHint[ i ].kind = ORDER_HINT_MOVE;
		m_moveHint[ i ].hull = 0.0f;
	}
	m_nextMoveHint = 0;

  m_inputEnabled = true;
	m_isDragSelecting = false;
	m_isFormationDragging = FALSE;
	m_formationDragSpacing = FORMATION_DRAG_MIN_SPACING;
	m_selectCount = 0;
	m_frameSelectionChanged = 0;
  m_duringDoubleClickAttackMoveGuardHintTimer = 0;
  m_duringDoubleClickAttackMoveGuardHintStashedPosition.zero();
	m_maxSelectCount = -1;
	m_isScrolling = FALSE;
	m_isSelecting = FALSE;
	m_mouseMode = MOUSEMODE_DEFAULT;
	m_mouseModeCursor = Mouse::ARROW;
	m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
	
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_currentlyPlayingMovie.clear();
	m_militarySubtitle = NULL;
	m_popupMessageData = NULL;
	m_waypointMode = FALSE;
	m_clientQuiet = FALSE;
	
	m_messageColor1 = GameMakeColor( 255, 255, 255, 255 );
	m_messageColor2 = GameMakeColor( 180, 180, 180, 255 );
	m_messagePosition.x = 10;
	m_messagePosition.y = 10;
	m_messageFont = "Arial";
	m_messagePointSize = 10;
	m_messageBold = FALSE;
	m_messageDelayMS = 5000;
	for( i = 0; i < CLASSIC_MESSAGES; i++ )
	{
		m_classicMessages[ i ].text = NULL;
		m_classicMessages[ i ].frame = 0;
		m_classicMessages[ i ].color = 0;
	}
	m_classicMessageFadeFrame = 0;

	m_militaryCaptionColor.red   = 200;
	m_militaryCaptionColor.green = 200;
	m_militaryCaptionColor.blue  = 30;
	m_militaryCaptionColor.alpha = 255;
	m_militaryCaptionPosition.x = 10;
	m_militaryCaptionPosition.y = 380;

	m_militaryCaptionTitleFont = "Courier";
	m_militaryCaptionTitlePointSize = 12;
	m_militaryCaptionTitleBold = TRUE;

	m_militaryCaptionFont = "Courier";
	m_militaryCaptionPointSize = 12;
	m_militaryCaptionBold = FALSE;

	m_militaryCaptionRandomizeTyping = FALSE;
	m_militaryCaptionSpeed.setRaw( 1 );
	m_popupMessageColor = GameMakeColor(255,255,255,255);

	m_tooltipsDisabledUntil = 0;

	for( i = 0; i < MAX_BUILD_PROGRESS; i++ )
	{

		m_buildProgress[ i ].m_thingTemplate = NULL;
		m_buildProgress[ i ].m_percentComplete = 0.0f;
		m_buildProgress[ i ].m_control = NULL;

	}  // end for i

	m_pendingGUICommand = NULL;

	// allocate an array for the placement icons
	m_placeIcon = NEW Drawable* [ TheGlobalData->m_maxLineBuildObjects ];
	for( i = 0; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		m_placeIcon[ i ] = NULL;
	m_pendingPlaceType = NULL;
	m_pendingPlaceSourceObjectID = INVALID_ID;
	m_preventLeftClickDeselectionInAlternateMouseModeForOneClick = FALSE;
	m_placeAnchorStart.x = m_placeAnchorStart.y = 0;
	m_placeAnchorEnd.x = m_placeAnchorEnd.y = 0;
	m_placeAnchorInProgress = FALSE;
	m_placeAngleOffset = 0.0f;
	m_placeAngleType = NULL;
	m_placementLegal = TRUE;
	m_placementNudge.zero();
	m_placementRowGap = 0.0f;
	m_placementRowGapWheel = 0.0f;

	m_videoStream = NULL;
	m_videoBuffer = NULL;
	m_cameoVideoStream = NULL;
	m_cameoVideoBuffer = NULL;

	m_armedSignal = SIGNAL_KIND_COUNT;
	m_dozerCheckFrame = 0;
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
		m_hadDozer[ index ] = FALSE;

	m_replayWindow = NULL;
	m_messagesOn = TRUE;

	forgetPendingPlacements();
	m_hudDisplayString = NULL;
	m_peaceTimeDisplayString = NULL;
	m_lastMoneyDisplayed = -1;
	m_moneyShownPlayer = NULL;
	m_moneyShownMs = 0;
	m_hudDrawCount = 0;
	m_noticeTop = 0;
	m_hudLastSampleFrame = 0;
	m_hudLastSampleMs = 0;
	m_cameraKeyLastMs = 0;
	m_cameraSnapRepeatMs = 0;
	m_subtitleFreezeStartMs = 0;
	m_subtitleFreezeSteps = 0;
	m_hudLastSampleLogicFrame = 0;
	m_hudRealClockBaseMs = 0;
	m_hudLastDrawMs = 0;
	m_productionListCount = 0;
	m_productionListTotal = 0;
	for( Int text = 0; text < 2 * PRODUCTION_LIST_CELLS; text++ )
		m_productionListText[ text ] = NULL;
	for( Int text = 0; text < SUPERWEAPON_LIST_CELLS; text++ )
		m_superweaponCellText[ text ] = NULL;
	m_superweaponMoreText = NULL;
	m_productionListMoreText = NULL;
	m_productionListCells = 0;
	m_productionListTab = noHudTab();
	m_hero = INVALID_ID;
	m_heroCell = noHudTab();
	m_superweaponCells = 0;
	m_superweaponTab = noHudTab();
	m_hudCellPass = 0;
	m_hudCellDrawnPass = 0;
	m_rackStat = OBSHUD_STAT_ARMY;	// the watcher's standings open on the armies, as the design shows them

	m_superweaponPosition.x = 0.7f;
	m_superweaponPosition.y = 0.7f;
	m_superweaponFlashDuration.setRaw( 50.0f );	// msec: 1.0f frames at the native speed
	m_superweaponNormalFont = "Arial";
	m_superweaponNormalPointSize = 10;
	m_superweaponNormalBold = FALSE;
	m_superweaponReadyFont = "Arial";
	m_superweaponReadyPointSize = 10;
	m_superweaponReadyBold = FALSE;

	m_superweaponFlashColor = GameMakeColor(255, 255, 255, 255);
	m_superweaponLastFlashFrame = 0;
	m_superweaponUsedFlashColor = TRUE; // so next one is false
	m_superweaponHiddenByScript = FALSE;

	m_namedTimerPosition.x = 0.05f;
	m_namedTimerPosition.y = 0.7f;
	m_namedTimerFlashDuration.setRaw( 50.0f );	// msec: 1.0f frames at the native speed
	m_namedTimerNormalFont = "Arial";
	m_namedTimerNormalPointSize = 10;
	m_namedTimerNormalBold = FALSE;
	m_namedTimerReadyFont = "Arial";
	m_namedTimerReadyPointSize = 10;
	m_namedTimerReadyBold = FALSE;


	m_namedTimerNormalColor	= GameMakeColor(255, 255,   0, 255);
	m_namedTimerReadyColor	= GameMakeColor(255,   0, 255, 255);
	m_namedTimerFlashColor	= GameMakeColor(  0, 255, 255, 255);
	m_namedTimerLastFlashFrame = 0;
	m_namedTimerUsedFlashColor = TRUE; // so next one is false
	m_showNamedTimers = TRUE;

	// retail InGameUI.ini's own numbers, so a pop without the file moves as it does with it
	m_floatingTextTimeOut.setRaw( 333 );
	m_floatingTextMoveUpSpeed.setRaw( 30.0f );
	m_floatingTextMoveVanishRate.setRaw( 3.0f );

	m_drawableCaptionFont = "Arial";
	m_drawableCaptionPointSize = 10;
	m_drawableCaptionBold = FALSE;
	m_drawableCaptionColor = GameMakeColor(255, 255, 255, 255);

	m_drawRMBScrollAnchor = FALSE;
	m_moveRMBScrollAnchor = FALSE;
	m_displayedMaxWarning = FALSE; 

	m_waypointMode			= false;
	m_forceAttackMode		= false;
	m_forceMoveToMode		= false;
	m_attackMoveToMode	= false;
	m_forceAttackArmed	= false;
	m_guardArmed				= false;
	m_guardArmedMode		= GUARDMODE_GUARD_WITHOUT_PURSUIT;
	m_guardRadiusScale	= 1.0f;
	m_moveArmed					= false;
	m_orderKeyKeptByShift	= false;
	m_preferSelection		= false;
	m_isAttackCircling	= FALSE;

	m_curRcType = RADIUSCURSOR_NONE;
	memset( &m_markerAim, 0, sizeof( m_markerAim ) );
	m_markerAim.type = RADIUSCURSOR_NONE;

	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;

}  // end InGameUI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
InGameUI::~InGameUI()
{
	delete TheControlBar;
	TheControlBar = NULL;

	// free all the display strings if we're
	removeMilitarySubtitle();

	stopMovie();
	stopCameoMovie();

	// remove any build available status
	placeBuildAvailable( NULL, NULL );
	setRadiusCursorNone();

	// delete the message resources
	freeMessageResources();

	// delete the array for the drawbles
	delete [] m_placeIcon;
	m_placeIcon = NULL;

	// clear floating text
	clearFloatingText();

	// clear world animations
	clearWorldAnimations();
	resetIdleWorker();

	for( std::map< std::string, DisplayString * >::iterator text = m_broadcastTexts.begin(); text != m_broadcastTexts.end(); ++text )
		TheDisplayStringManager->freeDisplayString( text->second );
	m_broadcastTexts.clear();
	for( Int text = 0; text < 2 * PRODUCTION_LIST_CELLS; text++ )
		TheDisplayStringManager->freeDisplayString( m_productionListText[ text ] );
	for( Int text = 0; text < SUPERWEAPON_LIST_CELLS; text++ )
		TheDisplayStringManager->freeDisplayString( m_superweaponCellText[ text ] );
	TheDisplayStringManager->freeDisplayString( m_superweaponMoreText );
	TheDisplayStringManager->freeDisplayString( m_productionListMoreText );
}

//-------------------------------------------------------------------------------------------------
/** Initialize the in game user interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::init( void )
{
	INI ini;
	ini.load( AsciiString( "Data\\INI\\InGameUI.ini" ), INI_LOAD_OVERWRITE, NULL );

	//override INI values with language localized values:
	if (TheGlobalLanguageData)
	{
		if (TheGlobalLanguageData->m_drawableCaptionFont.name.isNotEmpty())
		{	m_drawableCaptionFont = TheGlobalLanguageData->m_drawableCaptionFont.name;
			m_drawableCaptionPointSize = TheGlobalLanguageData->m_drawableCaptionFont.size;
			m_drawableCaptionBold = TheGlobalLanguageData->m_drawableCaptionFont.bold;
		}

		if (TheGlobalLanguageData->m_messageFont.name.isNotEmpty())
		{	m_messageFont = TheGlobalLanguageData->m_messageFont.name;
			m_messagePointSize = TheGlobalLanguageData->m_messageFont.size;
			m_messageBold = TheGlobalLanguageData->m_messageFont.bold;
		}

		if (TheGlobalLanguageData->m_militaryCaptionTitleFont.name.isNotEmpty())
		{	m_militaryCaptionTitleFont = TheGlobalLanguageData->m_militaryCaptionTitleFont.name;
			m_militaryCaptionTitlePointSize = TheGlobalLanguageData->m_militaryCaptionTitleFont.size;
			m_militaryCaptionTitleBold = TheGlobalLanguageData->m_militaryCaptionTitleFont.bold;
		}

		if (TheGlobalLanguageData->m_militaryCaptionFont.name.isNotEmpty())
		{	m_militaryCaptionFont = TheGlobalLanguageData->m_militaryCaptionFont.name;
			m_militaryCaptionPointSize = TheGlobalLanguageData->m_militaryCaptionFont.size;
			m_militaryCaptionBold = TheGlobalLanguageData->m_militaryCaptionFont.bold;
		}

		if (TheGlobalLanguageData->m_superweaponCountdownNormalFont.name.isNotEmpty())
		{	m_superweaponNormalFont = TheGlobalLanguageData->m_superweaponCountdownNormalFont.name;
			m_superweaponNormalPointSize = TheGlobalLanguageData->m_superweaponCountdownNormalFont.size;
			m_superweaponNormalBold = TheGlobalLanguageData->m_superweaponCountdownNormalFont.bold;
		}

		if (TheGlobalLanguageData->m_superweaponCountdownReadyFont.name.isNotEmpty())
		{	m_superweaponReadyFont = TheGlobalLanguageData->m_superweaponCountdownReadyFont.name;
			m_superweaponReadyPointSize = TheGlobalLanguageData->m_superweaponCountdownReadyFont.size;
			m_superweaponReadyBold = TheGlobalLanguageData->m_superweaponCountdownReadyFont.bold;
		}

		if (TheGlobalLanguageData->m_namedTimerCountdownNormalFont.name.isNotEmpty())
		{	m_namedTimerNormalFont = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.name;
			m_namedTimerNormalPointSize = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.size;
			m_namedTimerNormalBold = TheGlobalLanguageData->m_namedTimerCountdownNormalFont.bold;
		}

		if (TheGlobalLanguageData->m_namedTimerCountdownReadyFont.name.isNotEmpty())
		{	m_namedTimerReadyFont = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.name;
			m_namedTimerReadyPointSize = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.size;
			m_namedTimerReadyBold = TheGlobalLanguageData->m_namedTimerCountdownReadyFont.bold;
		}
	}

	/**@ todo we used to put in the hint spy translator, but it's difficult
	to order the translators when the code is not centralized so it has
	been moved to where all the other translators are attached in game client */

	// create the tactical view
	if (TheDisplay)
	{
		TheTacticalView = createView();
		TheTacticalView->init();
		TheDisplay->attachView( TheTacticalView );

		// make the tactical display the full screen width for now
		TheTacticalView->setWidth( TheDisplay->getWidth());
		TheTacticalView->setHeight( TheDisplay->getHeight() * 0.77f);
	}
	TheTacticalView->setDefaultView(0.0f, 0.0f, 1.0f);

	/** @todo this may be the wrong place to create the sidebar, but for now
	this is where it lives */
	createControlBar();

	/** @todo This may be the wrong place to create the replay menu, but for now
	this is where it lives */
	createReplayControl();

	// create the command bar
	TheControlBar = NEW ControlBar;
	TheControlBar->init();

	m_windowLayouts.clear();

	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;


}  // end init

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void InGameUI::setRadiusCursor(RadiusCursorType cursorType, const SpecialPowerTemplate* specPowTempl, WeaponSlotType weaponSlot)
{
	// EA aimed three powers with no radius cursor at all: the Particle Cannon, the Laser General's
	// cannon and the supply crate drop.  The ground markers give them one; Classic keeps them bare
	if (cursorType == RADIUSCURSOR_NONE && specPowTempl != NULL && groundMarkersOn())
	{
		switch (specPowTempl->getSpecialPowerType())
		{
			case SPECIAL_PARTICLE_UPLINK_CANNON:
			case SUPW_SPECIAL_PARTICLE_UPLINK_CANNON:
			case LAZR_SPECIAL_PARTICLE_UPLINK_CANNON:
				cursorType = RADIUSCURSOR_PARTICLECANNON;
				break;
			case SPECIAL_CRATE_DROP:
				cursorType = RADIUSCURSOR_PARADROP;
				break;
			default:
				break;
		}
	}

	if (cursorType == m_curRcType)
		return;

	m_curRadiusCursor.clear();
	m_curRadiusCursorCentre.clear();
	m_curRcType = RADIUSCURSOR_NONE;
	// the ground marker's aim goes down with it, remembered a moment in case the same cursor comes
	// straight back up (a mouse hint takes it down and puts it up again on every move)
	if( m_markerAim.type != RADIUSCURSOR_NONE && m_markerAim.endMs == 0 )
		m_markerAim.endMs = MAX( 1u, (UnsignedInt)Clock_Milliseconds() );

	if (cursorType == RADIUSCURSOR_NONE)
		return;

	Object* obj = NULL;
	if( m_pendingGUICommand && m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT )
	{
		if( ThePlayerList && ThePlayerList->getLocalPlayer() && specPowTempl != NULL )
		{
			obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( specPowTempl->getSpecialPowerType() );
		}
	}
	else
	{
		if (getSelectCount() == 0)
			return;

		Drawable *draw = getFirstSelectedDrawable();
		if (draw == NULL)
			return;

		obj = draw->getObject();
	}

	if (obj == NULL)
		return;
	
	Player* controller = obj->getControllingPlayer();
	if (controller == NULL)
		return;

	Real radius = 0.0f;
	const Weapon* w = NULL;
	switch (cursorType)
	{
		// already handled
		//case RADIUSCURSOR_NONE:
		//	return;
		case RADIUSCURSOR_ATTACK_DAMAGE_AREA:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? w->getPrimaryDamageRadius(obj) : 0.0f;
			break;
		case RADIUSCURSOR_ATTACK_SCATTER_AREA:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? (w->getScatterRadius() + w->getScatterTargetScalar()) : 0.0f;
			break;
		case RADIUSCURSOR_ATTACK_CONTINUE_AREA:
		case RADIUSCURSOR_CLEARMINES:
			w = obj->getWeaponInWeaponSlot(weaponSlot);
			radius = w ? w->getContinueAttackRange() : 0.0f;
			break;
		case RADIUSCURSOR_GUARD_AREA:
			radius = AIGuardMachine::getStdGuardRange(obj);
			break;
		case RADIUSCURSOR_FRIENDLY_SPECIALPOWER:
		case RADIUSCURSOR_OFFENSIVE_SPECIALPOWER:
		case RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA:
		case RADIUSCURSOR_EMERGENCY_REPAIR:
		case RADIUSCURSOR_PARTICLECANNON: 
		case RADIUSCURSOR_A10STRIKE:
		case RADIUSCURSOR_SPECTREGUNSHIP:
    case RADIUSCURSOR_HELIX_NAPALM_BOMB:
		case RADIUSCURSOR_DAISYCUTTER:
		case RADIUSCURSOR_CARPETBOMB:
		case RADIUSCURSOR_PARADROP:
		case RADIUSCURSOR_SPYSATELLITE: 
		case RADIUSCURSOR_NUCLEARMISSILE: 
		case RADIUSCURSOR_EMPPULSE:
		case RADIUSCURSOR_ARTILLERYBARRAGE:
		case RADIUSCURSOR_FRENZY:
		case RADIUSCURSOR_NAPALMSTRIKE:
		case RADIUSCURSOR_CLUSTERMINES:
		case RADIUSCURSOR_SCUDSTORM: 
		case RADIUSCURSOR_ANTHRAXBOMB:
		case RADIUSCURSOR_AMBUSH: 
		case RADIUSCURSOR_RADAR:
		case RADIUSCURSOR_SPYDRONE:
		case RADIUSCURSOR_AMBULANCE:
			radius = specPowTempl ? specPowTempl->getRadiusCursorRadius( controller ) : 0.0f;
			break;

	}

	// the cannon's power names no radius: its beam sweeps an S along the line from the uplink, half
	// its swath either side of the target and its amplitude across (ParticleUplinkCannonUpdate)
	if (radius <= 0.0f && cursorType == RADIUSCURSOR_PARTICLECANNON)
	{
		const ModuleInfo &info = obj->getTemplate()->getBehaviorModuleInfo();
		for (Int i = 0; i < info.getCount(); ++i)
		{
			if (info.getNthName( i ).compareNoCase( "ParticleUplinkCannonUpdate" ) != 0)
				continue;
			const ParticleUplinkCannonUpdateModuleData *data = (const ParticleUplinkCannonUpdateModuleData *)info.getNthData( i );
			radius = MAX( 0.5f * data->m_swathOfDeathDistance, data->m_swathOfDeathAmplitude ) + 20.0f;
			break;
		}
	}

	// a weapon that does not scatter (the Maroon Beret's Kargu drones dive on the point) names no scatter
	// area: under the Reforged markers its aim is drawn as wide as the blast, so the button has a marker
	if (radius <= 0.0f && cursorType == RADIUSCURSOR_ATTACK_SCATTER_AREA && w != NULL && groundMarkersOn())
		radius = MAX( w->getPrimaryDamageRadius( obj ), 40.0f );

	if (radius <= 0.0f)
		return;

	Coord3D pos = { 0, 0, 0 };	// will be updated right away
	const Real unwheeled = radius;
	if (cursorType == RADIUSCURSOR_GUARD_AREA)
		radius *= m_guardRadiusScale;

	// The Machined Reticle markers draw the cursor themselves (W3DGroundMarkers), from what is kept
	// here, and no decal is made.  The same cursor put back up within a moment (a mouse hint, the
	// guard wheel remaking it at its new size) keeps its press, so the timeline does not restart, and
	// a new size is eased to from the one on the ground
	if (groundMarkersOn())
	{
		const UnsignedInt MARKER_AIM_KEEP_MS = 250;
		const UnsignedInt MARKER_WHEEL_EASE_MS = 400;		// W3DGroundMarkers eases over the same
		const UnsignedInt now = MAX( 1u, (UnsignedInt)Clock_Milliseconds() );
		GroundMarkerAim &aim = m_markerAim;
		const Bool same = aim.type == cursorType && aim.power == specPowTempl && aim.caster == obj->getID()
										&& aim.endMs != 0 && now - aim.endMs < MARKER_AIM_KEEP_MS;
		if (!same)
		{
			aim.pressMs = now;
			aim.radiusFrom = radius;
			aim.radiusMs = 0;
			aim.hasPos = FALSE;
		}
		else if (radius != aim.radius)
		{
			// from wherever the last step's easing has got to
			Real shown = aim.radius;
			if (aim.radiusMs != 0 && now - aim.radiusMs < MARKER_WHEEL_EASE_MS)
			{
				const Real f = 1.0f - (Real)( now - aim.radiusMs ) / (Real)MARKER_WHEEL_EASE_MS;
				shown = aim.radius + ( aim.radiusFrom - aim.radius ) * f * f * f;
			}
			aim.radiusFrom = shown;
			aim.radiusMs = now;
		}
		aim.type = cursorType;
		aim.power = specPowTempl;
		aim.caster = obj->getID();
		aim.radius = radius;
		aim.unwheeled = unwheeled;
		aim.endMs = 0;
		m_curRcType = cursorType;
		handleRadiusCursor();
		return;
	}

	if (cursorType == RADIUSCURSOR_GUARD_AREA && m_radiusCursors[cursorType].getTexture().compareNoCase("SCCGuard_Generic") == 0)
	{
		// EA's guard marker is one picture, the ring with the shield in its middle, so sizing it to
		// the wheeled radius stretched the shield too.  The two are drawn apart (Tools/guard_marker.py):
		// the ring at the radius, the shield, half the picture's width, at the size it has unwheeled.
		// any other marker texture stays one picture
		m_radiusCursors[cursorType].createRadiusDecal(pos, radius, controller, m_curRadiusCursor, "ReforgedGuardRing");
		m_radiusCursors[cursorType].createRadiusDecal(pos, unwheeled * 0.5f, controller, m_curRadiusCursorCentre, "ReforgedGuardCentre");
	}
	else
		m_radiusCursors[cursorType].createRadiusDecal(pos, radius, controller, m_curRadiusCursor);
	m_curRcType = cursorType;

	handleRadiusCursor();
}

//-------------------------------------------------------------------------------------------------
/** GameLogic::update's freeze, where the scripts run and the frame stays: a camera move freezes
	* nothing in a multiplayer session. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isTimeFrozenWithoutTick( void ) const
{
	return ( !TheGameEngine->isMultiplayerSession() && TheTacticalView->isTimeFrozen()
					 && !TheTacticalView->isCameraMovementFinished() )
				 || TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript();
}

//-------------------------------------------------------------------------------------------------
/** handle updating of "radius cursors" that follow the mouse pos */
//-------------------------------------------------------------------------------------------------
void InGameUI::handleRadiusCursor()
{
	// the ground markers' aim has no decal, only the point (W3DGroundMarkers reads it at the sync
	// point, where this runs from preDraw)
	if (m_curRcType != RADIUSCURSOR_NONE && m_markerAim.type == m_curRcType && m_markerAim.endMs == 0)
	{
		const MouseIO* mouseIO = TheMouse->getMouseStatus();
		Coord3D pos;
		const Bool radarOn = TheRadar->isRadarForced()
									|| ( !TheRadar->isRadarHidden()
												&& ThePlayerList->getLocalPlayer()
												&& ThePlayerList->getLocalPlayer()->hasRadar() );
		Bool hasPos = radarOn && TheRadar->screenPixelToWorld( &mouseIO->pos, &pos );
		if( !hasPos )
			hasPos = TheTacticalView->screenToTerrain( &mouseIO->pos, &pos );
		if ( TheGlobalData->m_doubleClickAttackMove && m_duringDoubleClickAttackMoveGuardHintTimer > 0 )
		{
			m_markerAim.pos = m_duringDoubleClickAttackMoveGuardHintStashedPosition;
			m_markerAim.hasPos = TRUE;
		}
		else if ( hasPos )
		{
			m_markerAim.pos = pos;
			m_markerAim.hasPos = TRUE;
		}
		return;
	}

	if (!m_curRadiusCursor.isEmpty())
	{
		const MouseIO* mouseIO = TheMouse->getMouseStatus();
		Coord3D pos;

		//
		// if the mouse is in the radar window, the position in the world is that which is
		// represented by the radar, otherwise we use the mouse position itself transformed
		// from screen to world
		// But only if the radar is on.
		//
		Bool radarOn = TheRadar->isRadarForced() 
									|| ( !TheRadar->isRadarHidden() 
												&& ThePlayerList->getLocalPlayer() 
												&& ThePlayerList->getLocalPlayer()->hasRadar()
											);

		Bool hasPos = radarOn && TheRadar->screenPixelToWorld( &mouseIO->pos, &pos );
		if( !hasPos )// if radar off, or point not on radar
			hasPos = TheTacticalView->screenToTerrain( &mouseIO->pos, &pos );


    if ( TheGlobalData->m_doubleClickAttackMove && m_duringDoubleClickAttackMoveGuardHintTimer > 0 )
    {
      m_curRadiusCursor.setOpacity( m_duringDoubleClickAttackMoveGuardHintTimer * 0.1f );
  		m_curRadiusCursor.setPosition( m_duringDoubleClickAttackMoveGuardHintStashedPosition );	//world space position of center of decal
      m_curRadiusCursorCentre.setOpacity( m_duringDoubleClickAttackMoveGuardHintTimer * 0.1f );
      m_curRadiusCursorCentre.setPosition( m_duringDoubleClickAttackMoveGuardHintStashedPosition );

    }
    else if ( hasPos )
    {
  		m_curRadiusCursor.setPosition(pos);	//world space position of center of decal
      m_curRadiusCursor.update();
      m_curRadiusCursorCentre.setPosition(pos);
      m_curRadiusCursorCentre.update();
    }

  }
}

//-------------------------------------------------------------------------------------------------
/** The Machined Reticle markers are drawn under the Reforged graphics once their atlas loaded: every
	* MappedImage of ReforgedGroundMarkers.ini found, and the sheet itself on disk and not the missing
	* texture (W3DGroundMarkers' loadAtlas, at the sync point, says so here once).  Without it everything
	* stays on the retail decals, so a build that lacks the sheet still marks the ground.  The delivery
	* modules ask from the logic thread, so this only reads: a lookup there could add a name key beside
	* the main thread. */
//-------------------------------------------------------------------------------------------------
static Bool theGroundMarkersLoaded = FALSE;

void InGameUI::setGroundMarkersLoaded( Bool loaded )
{
	theGroundMarkersLoaded = loaded;
}

Bool InGameUI::groundMarkersOn( void )
{
	return theGroundMarkersLoaded && TheGlobalData != NULL && !TheGlobalData->m_classicGraphics;
}

static Bool theRallyMarkersLoaded = FALSE;

void InGameUI::setRallyMarkersLoaded( Bool loaded )
{
	theRallyMarkersLoaded = loaded;
}

Bool InGameUI::rallyMarkersOn( void )
{
	return theRallyMarkersLoaded && TheGlobalData != NULL && !TheGlobalData->m_classicGraphics;
}

//-------------------------------------------------------------------------------------------------
/** A ground order or a power at a location went out on this click: the cursor that aimed it, if one
	* is still up or went down a moment ago, becomes a marker on the clicked point. */
//-------------------------------------------------------------------------------------------------
void InGameUI::noteGroundMarkerClick( const Coord3D &pos, const SpecialPowerTemplate *power )
{
	const UnsignedInt MARKER_AIM_KEEP_MS = 250;
	if( !groundMarkersOn() || m_markerAim.type == RADIUSCURSOR_NONE )
		return;
	// only the cursor that aimed this order: the guard's for a guard, the power's own for a power, a
	// weapon's area (the Comanche's rocket pods, clear mines, the Maroon Beret's Kargu) for a weapon
	// fired at a location; this is the client's own click and sends nothing
	const Bool weaponCursor = m_markerAim.power == NULL
		&& ( m_markerAim.type == RADIUSCURSOR_ATTACK_SCATTER_AREA || m_markerAim.type == RADIUSCURSOR_ATTACK_DAMAGE_AREA
				|| m_markerAim.type == RADIUSCURSOR_ATTACK_CONTINUE_AREA || m_markerAim.type == RADIUSCURSOR_CLEARMINES );
	if( power != NULL ? m_markerAim.power != power : ( m_markerAim.type != RADIUSCURSOR_GUARD_AREA && !weaponCursor ) )
		return;
	const UnsignedInt now = MAX( 1u, (UnsignedInt)Clock_Milliseconds() );
	if( m_markerAim.endMs != 0 && now - m_markerAim.endMs >= MARKER_AIM_KEEP_MS )
		return;
	GroundMarkerShot shot;
	shot.aim = m_markerAim;
	shot.aim.pos = pos;
	shot.aim.hasPos = TRUE;
	shot.clickMs = now;
	m_markerShots.push_back( shot );
	// a burst of clicks with nothing taking them (a hidden interface) stays bounded
	if( m_markerShots.size() > 16 )
		m_markerShots.erase( m_markerShots.begin() );
}


void InGameUI::triggerDoubleClickAttackMoveGuardHint( void ) 
{
	const MouseIO* mouseIO = TheMouse->getMouseStatus();
	// No ground under the cursor means there is nothing to stash and nothing to hint at, so the
	// timer stays down rather than parking the hint on the corner of the map for eleven frames.
	if( TheTacticalView->screenToTerrain( &mouseIO->pos, &m_duringDoubleClickAttackMoveGuardHintStashedPosition ) )
		m_duringDoubleClickAttackMoveGuardHintTimer = 11;
}


//-------------------------------------------------------------------------------------------------
/** Handle the placement "icons" that appear at the cursor when we're putting down a 
	* structure to build.  Note that this has additional logic to also show a line
	* of objects because when we build "walls" we want to draw a line of repeating
	* wall pieces on the map where we want to put all of them */
//-------------------------------------------------------------------------------------------------


void InGameUI::evaluateSoloNexus( Drawable *newlyAddedDrawable )
{

	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;//failsafe...

	// short test: If the thing just added is a nonmobster, bail with NULL
	if ( newlyAddedDrawable )
	{
		const Object *newObj = newlyAddedDrawable->getObject();
		if ( newObj && ! ( newObj->isKindOf(KINDOF_MOB_NEXUS) || newObj->isKindOf(KINDOF_IGNORED_IN_GUI) ) )
			return;
	}

	//LoopAllSelectedDrawables
	UnsignedShort nexaeFound = 0;
	for( DrawableListCIt it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it ) 
	{

		Drawable *draw = (*it);
		const Object *obj = draw->getObject();


		if ( ! obj )
			continue;
			
		if ( obj->isKindOf( KINDOF_MOB_NEXUS ) )
		{
			++nexaeFound;
			if ( nexaeFound == 1 )
			{
				m_soloNexusSelectedDrawableID = draw->getID();
			}
			else // darn! more than one!
			{
				m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;
				return;
			}
		}
		else if ( ! obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )// darn! a non-angrymobster!
		{
			m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;
			return;
		}

	}  // end for


}


//-------------------------------------------------------------------------------------------------
// The spectator actions the control socket sends (runSpectatorAction): "camera:free",
// "camera:director" or "camera:player" picks who drives the camera (ObserverCamera.h), the player
// stop taking the top of the standings when nobody is followed; "follow:N" or "follow:none" picks the
// player it follows and leaves the mode; "fog" turns the followed player's fog on and off.
//-------------------------------------------------------------------------------------------------
static const std::string CAMERA_ACTION = "camera:";
static const std::string CAMERA_DIRECTOR = "director";
static const std::string CAMERA_FREE = "free";
static const std::string CAMERA_PLAYER = "player";
static const std::string FOLLOW_ACTION = "follow:";
static const std::string FOLLOW_NOBODY = "none";
static const std::string FOG_ACTION = "fog";

enum
{
	FRAMES_PER_MINUTE					= LOGICFRAMES_PER_SECOND * 60,
	SECONDS_IN_MINUTE					= 60,
	SECONDS_PER_HOUR					= 60 * 60,
	PERCENT										= 100
};

//-------------------------------------------------------------------------------------------------
/** Is the local player watching rather than playing - an observer, or knocked out and stayed? */
//-------------------------------------------------------------------------------------------------
static Bool localPlayerWatching( void )
{
	const Player *local = ThePlayerList->getLocalPlayer();
	return local != NULL && !local->isPlayerActive();
}

/** Everything a player can be ranked by, counted in one walk over what he owns. */
struct SpectatorStats
{
	Player *player;
	Int team;
	Int networth;		///< cash plus the build cost of everything standing
	Int cash;
	Bool unlimited;	///< Unlimited starting cash: cash is the fixed purse, shown as the word and kept out of networth
	Int income;			///< money earned per minute of match, averaged over the whole of it
	Int army;				///< the build cost of everything standing that is not a structure
	Int kills;			///< units and buildings destroyed
	Int losses;
	Int rank;
	Int power;			///< produced less consumed, negative when his base is browned out
	Int produced;		///< the power his plants make
	Int unitCount;	///< what the score counts: units, and the buildings EA's observer panel counted
	Int buildingCount;
	Bool underAttack;	///< a building of his took damage in the last few seconds
	std::map< const ThingTemplate *, Int > units;	///< how many of each unit with a cameo he has standing
};

/** One of the rack's stat keys: the label it goes by and the number the list is ranked by when it is
	* picked, in the order of the keys that pick them (pickSpectatorStat).  The number says its own unit:
	* "$14,700", "$1,200/min", "Rank 3", a power surplus green or red. */
struct SpectatorStat
{
	const char *label;
	Int SpectatorStats::*value;
};

static const SpectatorStat SPECTATOR_STATS[] =
{
	{ "GUI:HudNetWorth",		&SpectatorStats::networth },
	{ "GUI:HudStatCash",		&SpectatorStats::cash },
	{ "GUI:HudStatIncome",	&SpectatorStats::income },
	{ "GUI:HudStatArmy",		&SpectatorStats::army },
	{ "GUI:HudStatKills",		&SpectatorStats::kills },
	{ "GUI:HudStatRank",		&SpectatorStats::rank },
	{ "GUI:HudStatPower",		&SpectatorStats::power }
};
enum { SPECTATOR_STAT_COUNT = ARRAY_SIZE( SPECTATOR_STATS ) };

static void addObjectStats( Object *obj, void *userData )
{
	SpectatorStats *stats = (SpectatorStats *)userData;
	if( obj->isEffectivelyDead() )
		return;

	const ThingTemplate *thing = obj->getTemplate();
	const Int cost = thing->calcCostToBuild( stats->player );
	stats->networth += cost;
	if( obj->isKindOf( KINDOF_STRUCTURE ) )
	{
		if( obj->isKindOf( KINDOF_SCORE ) || obj->isKindOf( KINDOF_SCORE_CREATE ) || obj->isKindOf( KINDOF_SCORE_DESTROY ) )
			stats->buildingCount++;
		// his base under attack: a building hit by an enemy in the last three seconds (not its own fire,
		// a crush or the sell-off)
		const BodyModuleInterface *body = obj->getBodyModule();
		const UnsignedInt hit = body != NULL ? body->getLastDamageTimestamp() : 0;
		if( hit > 0 && hit <= TheGameLogic->getFrame() && hit + 3 * LOGICFRAMES_PER_SECOND >= TheGameLogic->getFrame() )
		{
			const DamageInfo *damage = body->getLastDamageInfo();
			const Object *source = damage != NULL ? TheGameLogic->findObjectByID( damage->in.m_sourceID ) : NULL;
			if( source != NULL && source->getRelationship( obj ) == ENEMIES )
				stats->underAttack = TRUE;
		}
		return;
	}
	if( obj->isKindOf( KINDOF_SCORE ) )
		stats->unitCount++;

	stats->army += cost;
	if( cost > 0 && thing->getButtonImage() != NULL )
		stats->units[ thing ]++;
}

//-------------------------------------------------------------------------------------------------
/** A lobby seat the observer bar lists, player0 to player7 and not an observer: the Boss general sits
	* in one but is no playable side.  Outside a multiplayer match the playable sides stand in. */
//-------------------------------------------------------------------------------------------------
static Bool isObserverSeat( const Player *player )
{
	if( !TheRecorder->isMultiplayer() )
		return player->isPlayableSide();
	if( player->isPlayerObserver() )
		return FALSE;
	for( Int slot = 0; slot < MAX_SLOTS; slot++ )
	{
		AsciiString name;
		name.format( "player%d", slot );
		if( TheNameKeyGenerator->nameToKey( name ) == player->getPlayerNameKey() )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Everyone still in the match but leftOut with his numbers, ranked by `stat`, and the players in kept
	* who have lost.  Allies both ways share a team number, counted in the order the teams first appear
	* in the player list.  seats takes the observer bar's players instead of the playable sides. */
//-------------------------------------------------------------------------------------------------
static std::vector< SpectatorStats > gatherSpectatorStats( const SpectatorStat &stat, const Player *leftOut, PlayerMaskType kept = 0, Bool seats = FALSE )
{
	std::vector< SpectatorStats > players;
	const UnsignedInt frame = TheGameLogic->getFrame();
	Int teams = 0;
	for( Int index = 0; index < ThePlayerList->getPlayerCount(); index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( player == NULL || player == leftOut || !( seats ? isObserverSeat( player ) : player->isPlayableSide() ) )
			continue;
		if( !player->isPlayerActive() && ( player->getPlayerMask() & kept ) == 0 )
			continue;

		ScoreKeeper *score = player->getScoreKeeper();
		SpectatorStats stats;
		stats.player = player;
		stats.team = -1;
		stats.cash = player->getMoney()->countMoney();
		stats.unlimited = player->getMoney()->isUnlimited();
		stats.networth = stats.unlimited ? 0 : stats.cash;
		stats.income = frame > 0 ? (Int)( (Int64)score->getTotalMoneyEarned() * FRAMES_PER_MINUTE / frame ) : 0;
		stats.army = 0;
		stats.kills = score->getTotalUnitsDestroyed() + score->getTotalBuildingsDestroyed();
		stats.losses = score->getTotalUnitsLost() + score->getTotalBuildingsLost();
		stats.rank = player->getRankLevel();
		stats.power = player->getEnergy()->getProduction() - player->getEnergy()->getConsumption();
		stats.produced = player->getEnergy()->getProduction();
		stats.unitCount = 0;
		stats.buildingCount = 0;
		stats.underAttack = FALSE;
		player->iterateObjects( addObjectStats, &stats );

		for( size_t earlier = 0; earlier < players.size() && stats.team < 0; earlier++ )
		{
			const Player *other = players[ earlier ].player;
			if( player->getRelationship( other->getDefaultTeam() ) == ALLIES && other->getRelationship( player->getDefaultTeam() ) == ALLIES )
				stats.team = players[ earlier ].team;
		}
		if( stats.team < 0 )
			stats.team = teams++;
		players.push_back( stats );
	}

	const Int SpectatorStats::*value = stat.value;
	std::stable_sort( players.begin(), players.end(),
										[ value ]( const SpectatorStats &a, const SpectatorStats &b ) { return a.*value > b.*value; } );
	return players;
}

/** m:ss of match time, or h:mm:ss once it runs past the hour. */
static std::string spectatorClock( UnsignedInt frame )
{
	const UnsignedInt seconds = frame / LOGICFRAMES_PER_SECOND;
	char text[ sizeof( "000:00:00" ) ];
	if( seconds >= SECONDS_PER_HOUR )
		sprintf( text, "%u:%02u:%02u", seconds / SECONDS_PER_HOUR, seconds / SECONDS_IN_MINUTE % SECONDS_IN_MINUTE, seconds % SECONDS_IN_MINUTE );
	else
		sprintf( text, "%u:%02u", seconds / SECONDS_IN_MINUTE, seconds % SECONDS_IN_MINUTE );
	return text;
}

//-------------------------------------------------------------------------------------------------
// Replay playback from the control socket: "replay:pause" pauses and resumes, "replay:seek:N" jumps
// to frame N, and "replay:speed:N" plays at N percent of the logic rate the game was played at.
// A seek back loads the last checkpoint in front of the frame and runs forward from there: the
// simulation keeps no history to step back through, so the replay saves the whole world every half
// minute of it as it plays.  A seek forward fast-forwards, one picture in thirty drawn, until the
// frame is reached; with REPLAY_FORWARD_FROM_CHECKPOINT on, it loads a checkpoint already taken
// between here and there instead when that beats simulating up to it (after a seek back, say).  The
// load puts no loading screen up; the picture holds.
//-------------------------------------------------------------------------------------------------
static const std::string REPLAY_ACTION = "replay:";
static const std::string REPLAY_SEEK_TO = REPLAY_ACTION + "seek:";
static const std::string REPLAY_PAUSE = REPLAY_ACTION + "pause";
static const std::string REPLAY_SPEED = REPLAY_ACTION + "speed:";

/** The frame a seek runs to, 0 for none.  Not a member: a seek back resets the whole interface when
	* it starts the replay over, and the frame has to outlive that. */
static UnsignedInt TheReplaySeekFrame = 0;

/** 1x: the rate the game was played at.  A skirmish on its fast setting recorded 60. */
static Int replayNormalFramesPerSecond( void )
{
	const Int recorded = TheRecorder->getPlaybackFramesPerSecond();
	return recorded > 0 ? recorded : LOGICFRAMES_PER_SECOND;
}

/** A rewind checkpoint: the world saved between two logic frames, where playback stood in the
	* replay, and the CRCs the last logic frame posted that the next one has yet to compare.  Those
	* are messages on their way, and the load's reset empties the stream they are in. */
struct ReplayCheckpoint
{
	AsciiString path;
	RecorderClass::PlaybackCursor cursor;
	GameLogicRandomState random;			///< a save game does not carry the logic's random stream
	std::vector< std::pair< Int, Bool > > postedCRCs;
	UnsignedInt line;									///< TheReplayLine when it was taken
	Bool parted;											///< the line it was taken on had already gone out of sync
};

static const UnsignedInt REPLAY_CHECKPOINT_FRAMES = LOGICFRAMES_PER_SECOND * 30;
static const char *const REPLAY_CHECKPOINT_FOLDER = "ReplayRewind";

/** The checkpoints of the replay TheReplayCheckpointsOf names, by frame.  Not members, for the same
	* reason as the seek frame: loading one resets the interface. */
static std::map< UnsignedInt, ReplayCheckpoint > TheReplayCheckpoints;
static AsciiString TheReplayCheckpointsOf;
/// a checkpoint of this replay failed to save, so no more are tried: each try is a whole save
static Bool TheReplayCheckpointsFailed = FALSE;

/** A seek back is carried out on the next client pass rather than in the click: the load resets the
	* message stream, and the click is a message the stream is in the middle of handing round. */
static Bool TheReplayRewindWaiting = FALSE;

/// when the seek running now was asked for, for the log line when it lands
static UnsignedInt TheReplaySeekStartMs = 0;

/** What the last checkpoint load took and how many frames a second the last seek simulated, which
	* decide whether a seek forward loads the checkpoint in front of its target.  Seeded with the
	* slowest load measured, a late 8-player Twilight Flame (3.8 s), and a simulation rate above any
	* measured there (43 to 55 a second headless), so until a seek has measured both the checkpoint
	* has to be 400 frames ahead before it is taken. */
static UnsignedInt TheReplayLoadMs = 4000;
static UnsignedInt TheReplaySeekFramesPerSecond = 100;
/// where the simulated part of the seek running now started, frame and clock
static UnsignedInt TheReplaySimulatedFrom = 0;
static UnsignedInt TheReplaySimulatedFromMs = 0;

/** The line of play the replay is on: 0 is the replay played straight from its start, and every
	* checkpoint load starts a new one.  Saving and loading do not yet carry every bit of the world, so
	* a load can play on into a different game; the recorded CRCs catch it, and then the checkpoint
	* loaded and those taken since on its line are dropped (recoverFromPartedReplayLine). */
static UnsignedInt TheReplayLine = 0;
static UnsignedInt TheReplayLineLoadedFrom = 0;	///< the checkpoint the line was loaded from
static Bool TheReplayLineParted = FALSE;				///< it has gone out of sync, and been dealt with
/** Loads of this replay that parted.  After one, a seek forward no longer trades the line it is on
	* for a load; after two, loads have shown they cannot be trusted here and none is retried. */
static Int TheReplayPartedLoads = 0;

/** A seek forward may load a checkpoint in front of its target instead of simulating up to it.  Off
	* until save/load parity holds, the condition REJOIN_FROM_CHECKPOINT (Network.cpp) waits on too: on
	* an 8-player Twilight Flame replay of 2026-11-24 the load cut a 24,143 frame jump from 791 s to 38 s,
	* and every load parted from the recording 100 frames later, so the fast jump ended on another game. */
static const Bool REPLAY_FORWARD_FROM_CHECKPOINT = FALSE;

static void seekReplay( UnsignedInt target )
{
	TheReplaySeekStartMs = Clock_Milliseconds();
	TheGameLogic->setGamePaused( FALSE );
	const UnsignedInt frame = TheGameLogic->getFrame();
	// the first checkpoint is taken on the first frame, so only frame 0 has none behind it
	Bool load = target <= frame && !TheReplayCheckpoints.empty();
	if( REPLAY_FORWARD_FROM_CHECKPOINT && target > frame && TheReplayPartedLoads == 0 )
	{
		// forward: the checkpoint at or before the target, if loading it beats simulating up to it
		std::map< UnsignedInt, ReplayCheckpoint >::const_iterator at = TheReplayCheckpoints.upper_bound( target );
		if( at != TheReplayCheckpoints.begin() && ( --at )->first > frame )
			load = (UnsignedInt64)( at->first - frame ) * 1000 > (UnsignedInt64)TheReplayLoadMs * TheReplaySeekFramesPerSecond;
	}
	TheReplayRewindWaiting = load;
	TheReplaySeekFrame = target;
	TheReplaySimulatedFrom = frame;
	TheReplaySimulatedFromMs = TheReplaySeekStartMs;
}

/** A folder of this process's own: two copies of the game watching replays at once each keep their
	* checkpoints apart, and a copy that died leaves nothing another will take for its own. */
static AsciiString replayCheckpointFolder( void )
{
	AsciiString leaf;
#if defined(_WIN32)
	leaf.format( "%s\\%u", REPLAY_CHECKPOINT_FOLDER, (UnsignedInt)GetCurrentProcessId() );
#else
	leaf.format( "%s\\%u", REPLAY_CHECKPOINT_FOLDER, (UnsignedInt)getpid() );
#endif
	return TheGameState->getFilePathInSaveDirectory( leaf );
}

/** Every checkpoint file in the folder, not only the ones in the map, so nothing outlives the replay. */
static void forgetReplayCheckpoints( void )
{
	const AsciiString folder = replayCheckpointFolder();
	std::vector< AsciiString > files;
	TheLocalFileSystem->getFilesInDirectory( folder, AsciiString( "*.sav" ), files );
	for( size_t i = 0; i < files.size(); ++i )
	{
		AsciiString path;
		path.format( "%s\\%s", folder.str(), files[ i ].str() );
		TheLocalFileSystem->deleteFile( path.str() );
	}
	TheReplayCheckpoints.clear();
	TheReplayCheckpointsFailed = FALSE;
	TheReplayLine = 0;
	TheReplayLineParted = FALSE;
	TheReplayPartedLoads = 0;
}

static void collectPostedCRCs( GameMessageList *list, std::vector< std::pair< Int, Bool > > &crcs )
{
	for( GameMessage *message = list->getFirstMessage(); message; message = message->next() )
		if( message->getType() == GameMessage::MSG_LOGIC_CRC )
			crcs.push_back( std::make_pair( message->getArgument( 0 )->integer, message->getArgument( 1 )->boolean ) );
}

static void takeReplayCheckpoint( UnsignedInt frame )
{
	const UnsignedInt startMs = Clock_Milliseconds();
	TheLocalFileSystem->createDirectory( TheGameState->getSaveDirectory() );
	TheLocalFileSystem->createDirectory( TheGameState->getFilePathInSaveDirectory( REPLAY_CHECKPOINT_FOLDER ) );
	const AsciiString folder = replayCheckpointFolder();
	TheLocalFileSystem->createDirectory( folder );

	ReplayCheckpoint &checkpoint = TheReplayCheckpoints[ frame ];
	checkpoint.path.format( "%s\\%u.sav", folder.str(), frame );
	checkpoint.cursor = TheRecorder->getPlaybackCursor();
	checkpoint.random = GetGameLogicRandomState();
	checkpoint.line = TheReplayLine;
	checkpoint.parted = checkpoint.cursor.crcInfo.sawCRCMismatch();
	// the command list first: what it holds reaches the logic ahead of what the stream still holds
	collectPostedCRCs( TheCommandList, checkpoint.postedCRCs );
	collectPostedCRCs( TheMessageStream, checkpoint.postedCRCs );
	if( !TheGameState->saveCheckpoint( checkpoint.path ) )
	{
		// the rewind keeps the checkpoints before this one
		DEBUG_LOG(( "REPLAY CHECKPOINT frame %u failed, no more checkpoints for this replay\n", frame ));
		TheReplayCheckpoints.erase( frame );
		TheReplayCheckpointsFailed = TRUE;
		return;
	}
	DEBUG_LOG(( "REPLAY CHECKPOINT frame %u in %u ms\n", frame, (UnsignedInt)( Clock_Milliseconds() - startMs ) ));
}

/** Load the last checkpoint at or before the frame, or the first one if the frame is before it, for a
	* seek back or a seek forward past a checkpoint.  The
	* camera, the speed and the observer's view stay as they were, the observer camera's mode, followed
	* player and fog included: the checkpoint carries the camera it was taken with, which is not where
	* the person watching is looking now. */
static void rewindReplay( UnsignedInt target )
{
	std::map< UnsignedInt, ReplayCheckpoint >::const_iterator at = TheReplayCheckpoints.upper_bound( target );
	if( at != TheReplayCheckpoints.begin() )
		--at;
	const ReplayCheckpoint &checkpoint = at->second;
	const Bool forward = at->first > TheGameLogic->getFrame();

	const AsciiString replayFile = TheRecorder->getCurrentReplayFilename();
	// the reset forgets that the director raised the view over a fight, and the view keeps the raise;
	// given back first, so rewinds do not stack it
	TheObserverCamera.releaseHeight();
	Coord3D lookingAt;
	TheTacticalView->getPosition( &lookingAt );
	const Real angle = TheTacticalView->getAngle();
	const Real pitch = TheTacticalView->getPitch();
	const Real zoom = TheTacticalView->getZoom();
	const Int framesPerSecond = TheGameEngine->getFramesPerSecondLimit();
	const UnsignedInt startMs = Clock_Milliseconds();
	// the load resets the client, the observer camera with it: who drives it, who is followed and
	// whose fog is drawn are the watcher's choices, not the checkpoint's
	const ObserverCameraMode cameraMode = TheObserverCamera.getMode();
	const Int followed = TheObserverCamera.getFollowedPlayerIndex();
	const Bool fog = TheObserverCamera.isFogOn();
	const Bool looking = TheObserverCamera.isLooking();
	const Bool lookTookFog = TheObserverCamera.lookTookFog();

	// a failed load has cleared the match and said so; nothing is left to seek in or look at
	if( !TheGameState->loadCheckpoint( checkpoint.path,
		[ & ]() { return TheRecorder->resumePlayback( replayFile, checkpoint.cursor ); } ) )
	{
		TheReplaySeekFrame = 0;
		TheWritableGlobalData->m_TiVOFastMode = FALSE;
		return;
	}
	SetGameLogicRandomState( checkpoint.random );

	TheObserverCamera.setMode( cameraMode );
	TheObserverCamera.followPlayer( followed );
	TheObserverCamera.setFog( fog );
	TheObserverCamera.keepLooking( looking, lookTookFog );
	if( followed != ObserverCamera::NO_PLAYER )
		TheControlBar->watchPlayer( ThePlayerList->getNthPlayer( followed ) );

	for( size_t each = 0; each < checkpoint.postedCRCs.size(); each++ )
	{
		GameMessage *crc = TheMessageStream->appendMessage( GameMessage::MSG_LOGIC_CRC );
		crc->appendIntegerArgument( checkpoint.postedCRCs[ each ].first );
		crc->appendBooleanArgument( checkpoint.postedCRCs[ each ].second );
	}

	TheTacticalView->lookAt( &lookingAt );
	TheTacticalView->setAngle( angle );
	TheTacticalView->setPitch( pitch );
	TheTacticalView->setZoom( zoom );
	TheGameEngine->setFramesPerSecondLimit( framesPerSecond );

	TheReplayLine++;
	TheReplayLineLoadedFrom = at->first;
	TheReplayLineParted = checkpoint.parted;	// a line already out of sync has nothing left to catch
	const UnsignedInt nowMs = Clock_Milliseconds();
	TheReplayLoadMs = nowMs - startMs;
	TheReplaySimulatedFrom = TheGameLogic->getFrame();
	TheReplaySimulatedFromMs = nowMs;
	DEBUG_LOG(( "REPLAY %s to frame %u from the checkpoint at %u, loaded in %u ms\n",
		forward ? "FORWARD" : "REWIND", target, at->first, TheReplayLoadMs ));
}

/** The line a checkpoint load started has gone out of sync with the recording.  The load is what
	* parted it, and loading the same file plays the same different game again, so that checkpoint and
	* every one taken since on its line are dropped and the seek runs again once from an earlier one.  A
	* second parted load ends it there: on an 8-player Twilight Flame replay of 2026-11-24 every load
	* parted, the one at frame 1 by 9701, so stepping further back cost minutes and still ended out of
	* sync.  The log says so either way.  The watcher sees the picture jump back and race forward again,
	* as a seek. */
static void recoverFromPartedReplayLine( UnsignedInt frame )
{
	TheReplayLineParted = TRUE;
	TheReplayPartedLoads++;
	Int dropped = 0;
	for( std::map< UnsignedInt, ReplayCheckpoint >::iterator it = TheReplayCheckpoints.begin(); it != TheReplayCheckpoints.end(); )
	{
		if( it->first == TheReplayLineLoadedFrom || it->second.line == TheReplayLine )
		{
			TheLocalFileSystem->deleteFile( it->second.path.str() );
			it = TheReplayCheckpoints.erase( it );
			dropped++;
		}
		else
			++it;
	}
	// a seek still running keeps its target; a line that parted after it landed goes back to where it is
	const UnsignedInt target = TheReplaySeekFrame != 0 ? TheReplaySeekFrame : frame;
	const Bool again = TheReplayPartedLoads < 2 && !TheReplayCheckpoints.empty() && TheReplayCheckpoints.begin()->first <= target;
	DEBUG_LOG(( "REPLAY CHECKPOINT at %u parted from the recording by frame %u (parted load %d): %d checkpoints dropped, %s\n",
		TheReplayLineLoadedFrom, frame, TheReplayPartedLoads, dropped,
		again ? "seeking again from an earlier one" : "no load is retried on this replay, playback stays out of sync" ));
	if( !again )
		return;
	if( TheReplaySeekFrame == 0 )
		TheReplaySeekStartMs = Clock_Milliseconds();
	TheReplaySeekFrame = target;
	TheReplayRewindWaiting = TRUE;
}

/** Every client pass, between two logic frames: the checkpoints are taken here, a seek back is carried
	* out here, and a seek stops on its frame rather than on the next picture drawn. */
static void updateReplaySeek( void )
{
	if( !TheGameLogic->isInGame() || TheGameLogic->isLoadingMap() )
		return;
	if( !TheGameLogic->isInReplayGame() )
	{
		TheReplaySeekFrame = 0;
		TheReplayRewindWaiting = FALSE;
		if( TheReplayCheckpointsOf.isNotEmpty() )
		{
			forgetReplayCheckpoints();
			TheReplayCheckpointsOf.clear();
		}
		return;
	}

	if( TheReplayCheckpointsOf != TheRecorder->getCurrentReplayFilename() )
	{
		forgetReplayCheckpoints();
		TheReplayCheckpointsOf = TheRecorder->getCurrentReplayFilename();
	}

	if( TheReplayRewindWaiting )
	{
		TheReplayRewindWaiting = FALSE;
		rewindReplay( TheReplaySeekFrame );
	}

	const UnsignedInt frame = TheGameLogic->getFrame();
	// a load never lands on frame 0, so this is the replay started over from the shell: the straight line
	if( frame == 0 )
		TheReplayLine = 0;
	if( TheReplayLine != 0 && !TheReplayLineParted && TheRecorder->getPlaybackCursor().crcInfo.sawCRCMismatch() )
	{
		recoverFromPartedReplayLine( frame );
		return;
	}

	if( TheRecorder->hasPlaybackLeft() && frame > 0 && !TheReplayCheckpointsFailed
			&& ( TheReplayCheckpoints.empty() || frame >= TheReplayCheckpoints.rbegin()->first + REPLAY_CHECKPOINT_FRAMES ) )
		takeReplayCheckpoint( frame );

	if( TheReplaySeekFrame == 0 )
		return;
	const Bool seeking = frame < TheReplaySeekFrame;
	TheWritableGlobalData->m_TiVOFastMode = seeking;
	if( !seeking )
	{
		const UnsignedInt nowMs = Clock_Milliseconds();
		const UnsignedInt simulated = frame > TheReplaySimulatedFrom ? frame - TheReplaySimulatedFrom : 0;
		const UnsignedInt simulatedMs = nowMs - TheReplaySimulatedFromMs;
		// a short tail measures the pass around it more than the simulation
		if( simulated >= LOGICFRAMES_PER_SECOND && simulatedMs > 0 )
			TheReplaySeekFramesPerSecond = (UnsignedInt)( (UnsignedInt64)simulated * 1000 / simulatedMs ) + 1;
		DEBUG_LOG(( "REPLAY SEEK to frame %u landed on %u in %u ms at %d fps, %u frames simulated in %u ms\n", TheReplaySeekFrame,
			frame, nowMs - TheReplaySeekStartMs, TheGameEngine->getFramesPerSecondLimit(), simulated, simulatedMs ));
		TheReplaySeekFrame = 0;
	}
}

//-------------------------------------------------------------------------------------------------
/// the top seat of the standings by `stat` still in the game, NO_PLAYER with nobody left: who a key that wants a
/// seat takes when nobody is followed
static Int topActiveSeat( Int stat )
{
	const std::vector< SpectatorStats > ranked = gatherSpectatorStats( SPECTATOR_STATS[ stat ], ThePlayerList->getLocalPlayer(),
		PLAYERMASK_ALL, TRUE );
	for( size_t row = 0; row < ranked.size(); row++ )
		if( ranked[ row ].player->isPlayerActive() )
			return ranked[ row ].player->getPlayerIndex();
	return ObserverCamera::NO_PLAYER;
}

//-------------------------------------------------------------------------------------------------
void InGameUI::runSpectatorAction( const std::string &action )
{
	if( action.compare( 0, CAMERA_ACTION.size(), CAMERA_ACTION ) == 0 )
	{
		const std::string choice = action.substr( CAMERA_ACTION.size() );
		if( choice == CAMERA_DIRECTOR )
			TheObserverCamera.setMode( OBSERVER_CAMERA_DIRECTOR );
		else if( choice == CAMERA_PLAYER )
		{
			// nobody followed is no dead stop: the top of the standings still in the game is taken, and
			// with nobody left in it the camera stays as it is
			Int player = TheObserverCamera.getFollowedPlayerIndex();
			if( player == ObserverCamera::NO_PLAYER )
				player = topActiveSeat( m_rackStat );
			if( player != ObserverCamera::NO_PLAYER )
				takePlayerCamera( player );
		}
		else if( choice == CAMERA_FREE )
			TheObserverCamera.setMode( OBSERVER_CAMERA_FREE );
		else
			DEBUG_LOG(( "Spectator action \"%s\" names no camera\n", action.c_str() ));
	}
	else if( action.compare( 0, FOLLOW_ACTION.size(), FOLLOW_ACTION ) == 0 )
	{
		// following a player is watching him too: his side on the bar
		const std::string choice = action.substr( FOLLOW_ACTION.size() );
		Player *player = choice == FOLLOW_NOBODY ? NULL : ThePlayerList->getNthPlayer( atoi( choice.c_str() ) );
		// nobody during a look ends it as a second click on the row does
		if( player == NULL && TheObserverCamera.isLooking() )
		{
			letGoOfPlayerCamera();
			return;
		}
		deselectAllDrawables();
		TheControlBar->watchPlayer( player );
		TheObserverCamera.followPlayer( player != NULL ? player->getPlayerIndex() : ObserverCamera::NO_PLAYER );
	}
	else if( action == FOG_ACTION )
		TheObserverCamera.setFog( !TheObserverCamera.isFogOn() );
	// the pause key's own message, so the key and the button are one path
	else if( action == REPLAY_PAUSE && TheGameLogic->isInReplayGame() )
		TheMessageStream->appendMessage( GameMessage::MSG_META_TOGGLE_PAUSE );
	// replay:seek:N, a frame to jump to, for a script that has no pointer to put on the timeline
	else if( action.compare( 0, REPLAY_SEEK_TO.size(), REPLAY_SEEK_TO ) == 0 && TheGameLogic->isInReplayGame() )
		seekReplay( (UnsignedInt)atoi( action.substr( REPLAY_SEEK_TO.size() ).c_str() ) );
	else if( action.compare( 0, REPLAY_SPEED.size(), REPLAY_SPEED ) == 0 && TheGameLogic->isInReplayGame() )
		TheGameEngine->setFramesPerSecondLimit( atoi( action.substr( REPLAY_SPEED.size() ).c_str() ) * replayNormalFramesPerSecond() / PERCENT );
}

//-------------------------------------------------------------------------------------------------
/** Following him is watching him too, his side on the bar, as "follow:N" is.  The screen's picture
	* only; nothing reaches the logic. */
//-------------------------------------------------------------------------------------------------
void InGameUI::takePlayerCamera( Int playerIndex )
{
	deselectAllDrawables();
	TheControlBar->watchPlayer( ThePlayerList->getNthPlayer( playerIndex ) );
	TheObserverCamera.lookThrough( playerIndex, FALSE );
}

void InGameUI::letGoOfPlayerCamera( void )
{
	TheObserverCamera.stopLooking();
	TheControlBar->watchPlayer( NULL );
}

//-------------------------------------------------------------------------------------------------
// What a watcher's console ranks the seats by, and where the squad panel hangs under the superweapon
// tray (the rack that used to hang there gave way to the observer console, below)
//-------------------------------------------------------------------------------------------------
enum
{
	RACK_TRAY_GAP					= 8,		///< the superweapon tray's foot to the squad panel's plate
	RACK_NO_TRAY_DROP			= 4,		///< the info strip's foot to the plate, no superweapons up
	RACK_REFRESH_FRAMES		= LOGICFRAMES_PER_SECOND / 2
};

static std::vector< SpectatorStats > theRackStats;		///< every seat's standings, gathered every RACK_REFRESH_FRAMES
static UnsignedInt theRackStatsFrame = 0;
static Int theRackStatsStat = -1;
static Bool theConsoleShown = FALSE;		///< the observer console's last draw put it up (drawObserverConsole)

/// what a press on the observer HUD, or one of its hotkeys, does (InGameUI::runConsoleAction)
enum ConsoleAction
{
	CON_PICK,				///< arg: the player index; the picked one again lets go
	CON_STAT_STEP,	///< arg: +1 or -1
	CON_SEAT_STEP,	///< arg: +1 or -1 round the board's order
	CON_CAMERA,			///< arg: 0 free, 1 follow, 2 director
	CON_VISION,			///< arg: 0 all, 1 the picked seat's team, 2 the picked seat
	CON_HIDE,				///< arg: CON_MODULE_*
	CON_HIDE_ALL,
	CON_OPTIONS, CON_CHAT, CON_QUIT,
	CON_LOOK,				///< arg: an object to look at
	CON_STANDINGS,	///< the right box shows the standings in the roster's place, or the roster again
	CON_VISION_CYCLE,	///< the vision key: all, the followed seat's team, the followed seat, round again
	CON_DROP,				///< arg: the crown's dropdown (0 the camera, 1 the stat) opened or closed
	CON_STAT_PICK		///< arg: the stat
};

/// the modules a watcher hides on his own, for the match
enum { CON_MODULE_RADAR, CON_MODULE_BOARD, CON_MODULE_DOSSIER, CON_MODULES };
static Bool theConsoleHidden[ CON_MODULES ];
/// what the vision keys last chose: all, the picked seat's team, the picked seat
static Int theConsoleVision = 0;

static UnicodeString broadcastNumber( const char *prefix, Int value );

/// a length at 1080, on this screen: the HUD's own scale, the one stripPixels uses
static Int rackPx( Real at1080 )
{
	return REAL_TO_INT_FLOOR( at1080 * ControlBarHudPageScale() / 1.8f + 0.5f );
}

/// the player's colour as his units show it, opaque: the tint the console's stripes, lamps and bars take.
/// The house colour itself is the texture's tint, and a dark red or navy one was lost on the steel
static Color rackColour( const Player *player )
{
	return readableHouseColor( clientPlayerColor( player ) );
}

/// under the cells' pass stamp, as a click on the panel is (handleHudCellClick)
Bool InGameUI::handleAirliftWheel( const ICoord2D *mouse, Real notches )
{
	if( m_hudCellDrawnPass + 1 != m_hudCellPass )
		return FALSE;
	return AirliftPanel_wheel( mouse, notches > 0.0f );
}

//-------------------------------------------------------------------------------------------------
/** The command grid's first keys while the console is up, Q W E R T Y A as shipped, pick the stat
	* keys' stats in their order.  A watcher has no commands to press; anything past the last stat is left to
	* the command bar. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::pickSpectatorStat( Int commandSlot )
{
	// the last draw's flag outlives a pass that drew nothing, a load screen's; the watch is asked again
	if( !theConsoleShown || !localPlayerWatching() || commandSlot >= SPECTATOR_STAT_COUNT )
		return FALSE;
	m_rackStat = commandSlot;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The same keys as letters, so the stat keys answer Q W E R T Y A with no CommandMap binding: the
	* shipped one binds no command slot to a letter.  A plain press only, while the console is up and the
	* local player watches; a player's letters never come here.  The observer HUD's other keys are
	* CommandMap commands a player rebinds (MSG_META_OBSERVER_*, runObserverCommand). */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::pickSpectatorStatKey( Int key, Int keyState )
{
	static const Int KEYS[ SPECTATOR_STAT_COUNT ] = { KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_A };
	if( !theConsoleShown || !localPlayerWatching() || ( keyState & ( KEY_STATE_CONTROL | KEY_STATE_SHIFT | KEY_STATE_ALT ) ) )
		return FALSE;
	for( Int stat = 0; stat < SPECTATOR_STAT_COUNT; stat++ )
	{
		if( KEYS[ stat ] != key )
			continue;
		if( keyState & KEY_STATE_DOWN )
			m_rackStat = stat;
		return TRUE;
	}
	return FALSE;
}

void InGameUI::handleBuildPlacements( void )
{

	//
	// if we're in the process of placing something we need up update one or more drawables
	// based on the position of the mouse
	//
	if( m_pendingPlaceType )
	{
		ICoord2D loc;
		Coord3D world;
		Real angle = m_placeIcon[ 0 ]->getOrientation();

		// update the angle of the icon to match any placement angle and pick the
		// location the icon will be at (anchored is the start, otherwise it's the mouse)
		Bool row = FALSE;
		if( isPlacementAnchored() )
		{
			ICoord2D start, end;

			// get the placement arrow points
			getPlacementPoints( &start, &end );

			// set icon to anchor point
			loc = start;

			// only adjust angle if we've actually moved the mouse, and not into a row: that drag
			// lays structures, the heading stays the one they had
			const Bool dragged = start.x != end.x || start.y != end.y;
			row = dragged && placesRow();			if( dragged && !row )
				angle = computePlacementAngle( &start, &end );

		}  // end if
		else
		{
			const MouseIO *mouseIO = TheMouse->getMouseStatus();

			// location is the mouse position
			loc = mouseIO->pos;

		}  // end else

		// set the location and angle of the place icon
		/**@todo this whole orientation vector thing is LAME! Must replace, all I want to
		to do is set a simple angle and have it automatically change, ug! */
		// Above the horizon the ray reaches no ground and the answer is the corner of the map, which
		// threw the ghost across the world and back.  Hold it where it was instead.
		if( !TheTacticalView->screenToTerrain( &loc, &world ) )
			world = *m_placeIcon[ 0 ]->getPosition();
		snapPlacementToGrid( &world, m_pendingPlaceType, angle );
		snapPlacementToNeighbour( &world, m_pendingPlaceType, angle );

		//
		// NudgeBuildPlacement: the ghost sits where the last legality check found room, which is
		// the cursor itself whenever the cursor works.  The check below runs every other frame and
		// always measures from the cursor, never from an already-nudged spot, so the offset cannot
		// walk away from the mouse; the ghost is at most one frame behind it.
		//
		const Coord3D cursorWorld = world;

		// a row starts where it was anchored; each piece is judged where it stands, not nudged
		if( row )
			m_placementNudge.zero();

		if( m_placementNudge.x != 0.0f || m_placementNudge.y != 0.0f )
		{
			world.x += m_placementNudge.x;
			world.y += m_placementNudge.y;
			world.z = TheTerrainLogic->getGroundHeight( world.x, world.y );
		}

		m_placeIcon[ 0 ]->setPosition( &world );
		m_placeIcon[ 0 ]->setOrientation( angle );


		//
		// check to see if this is a legal location to build something at and tint or "un-tint"
		// the cursor icons as appropriate.  This involves a pathfind which could be
		// expensive so we don't want to do it on every frame (althought that would be ideal)
		// If we discover there are cases that this is just too slow we should increase the
		// delay time between checks or we need to come up with a way of recording what is
		// valid and what isn't or "fudge" the results to feel "ok"
		//
		if( ( TheGameClient->getFrame() & 0x1 ) && !row )
		{
			TheTerrainVisual->removeAllBibs();

			Object *builderObject = TheGameLogic->findObjectByID( getPendingPlaceSourceObjectID() );

			const UnsignedInt checkOptions = placementCheckOptions();
			Coord3D spot = cursorWorld;
			LegalBuildCode lbc;
			lbc = TheBuildAssistant->isLocationLegalToBuild( &spot, m_pendingPlaceType, angle,
																											 checkOptions, builderObject, NULL );

			//
			// and the ground an order of your own has already been placed on but has not landed on
			// yet - see recordPendingPlacement.  Without this the ghost is green over a structure
			// that is already paid for, which is what a shift-held run of clicks sees on a laggy link.
			//
			if( lbc == LBC_OK && overlapsPendingPlacement( &spot, m_pendingPlaceType, angle ) )
				lbc = LBC_OBJECTS_IN_THE_WAY;

			//
			// Blocked: look for the nearest spot that is not, and move the ghost there - the click
			// does the same search (see PlaceEventTranslator), so what you see is where it lands.
			// Shroud is left alone on purpose: unscouted ground is not a placement mistake to fix,
			// and hunting around in it would answer questions about ground you cannot see.
			//
			m_placementNudge.zero();
			if( lbc != LBC_OK && lbc != LBC_SHROUD )
			{
				const Bool moved = nudgePlacementToLegal( &spot, m_pendingPlaceType, angle,
																									builderObject );

				//
				// Either way the search has just painted a red bib on everything it bumped into on
				// the way out, which is a report on spots nobody is proposing.  Clear them, and if
				// there was nowhere to go, ask about the spot under the cursor once more so the one
				// bib that does explain the refusal comes back.
				//
				TheTerrainVisual->removeAllBibs();

				if( moved )
				{
					m_placementNudge.x = spot.x - cursorWorld.x;
					m_placementNudge.y = spot.y - cursorWorld.y;
					lbc = LBC_OK;
				}
				else
				{
					TheBuildAssistant->isLocationLegalToBuild( &cursorWorld, m_pendingPlaceType, angle,
																										 checkOptions, builderObject, NULL );
				}
			}

			// the cursor reads this too - see createCommandHint's MOUSEMODE_BUILD_PLACE case
			m_placementLegal = ( lbc == LBC_OK );

			if( lbc != LBC_OK )
				m_placeIcon[ 0 ]->colorTint( &illegalBuildColor );
			else
				m_placeIcon[ 0 ]->colorTint( NULL );

			


			// Add the bibs around the structure.
			if (lbc != LBC_OK) 
			{
				TheTerrainVisual->addFactionBibDrawable(m_placeIcon[0], lbc != LBC_OK);
			} else {
				TheTerrainVisual->removeFactionBibDrawable(m_placeIcon[0]);
			}
		}  // end if



		//
		// we have additional place icons when we're placing down a line of walls or other
		// similarly placed object, or a shift-dragged row of structures ... for those we will
		// have them be oriented the same way as the first one, but we'll set their positions so
		// that they "tile" end to end
		//
		const Bool lineBuild = isPlacementAnchored() && TheBuildAssistant->isLineBuildTemplate( m_pendingPlaceType );
		Int iconsUsed = 1;
		if( lineBuild || row )
		{
			Int i;

			// get our line placement points
			ICoord2D screenStart, screenEnd;
			getPlacementPoints( &screenStart, &screenEnd );

			// project the start and the end points of the line anchor into the 3D world
			Coord3D worldStart, worldEnd;
			// An end that leaves the ground - the cursor dragged above the horizon - has no place on
			// the map, and the corner is not it.  Leave the line as it was last drawn.  This block is
			// the last thing handleBuildPlacements does.
			if( !TheTacticalView->screenToTerrain( &screenStart, &worldStart ) ||
					!TheTacticalView->screenToTerrain( &screenEnd, &worldEnd ) )
				return;

			// both ends, so a wall lands on the grid and tiles from a grid square; a row starts where
			// the ghost did, against its neighbour, and runs toward the cursor itself, since a snapped
			// end turned a short drag a few degrees off the line the player drew
			snapPlacementToGrid( &worldStart, m_pendingPlaceType, angle );
			if( lineBuild )
				snapPlacementToGrid( &worldEnd, m_pendingPlaceType, angle );
			snapPlacementToNeighbour( &worldStart, m_pendingPlaceType, angle );

			// get the builder object that will be constructing things
			Object *builderObject = TheGameLogic->findObjectByID( TheInGameUI->getPendingPlaceSourceObjectID() );

			const Coord3D *positions;
			std::vector<Coord3D> rowPositions;
			if( lineBuild )
			{
				// how big are each of our objects
				Real objectSize = m_pendingPlaceType->getTemplateGeometryInfo().getMajorRadius() * 2.0f;

				//
				// given the start/end points in the world and the the angle of the wall, fill
				// out an array of positions that "tile" this wall across the landscape
				//
				BuildAssistant::TileBuildInfo *tileBuildInfo;
				tileBuildInfo = TheBuildAssistant->buildTiledLocations( m_pendingPlaceType, angle,
																																&worldStart, &worldEnd,
																																objectSize,
																																TheGlobalData->m_maxLineBuildObjects,
																																builderObject,
																																BuildAssistant::IGNORE_UNSEEN );
				positions = tileBuildInfo->positions;
				iconsUsed = tileBuildInfo->tilesUsed;
			}
			else
			{
				computePlacementRow( m_pendingPlaceType, angle, &worldStart, &worldEnd, &rowPositions );
				positions = &rowPositions[ 0 ];
				iconsUsed = (Int)rowPositions.size();
			}

			// create any necessary drawables we need to "fill out" the line
			for( i = 0; i < iconsUsed; i++ )
			{

				if( m_placeIcon[ i ] == NULL )
					m_placeIcon[ i ] = TheThingFactory->newDrawable( m_pendingPlaceType,
																													 DRAWABLE_STATUS_NO_STATE_PARTICLES );

			}  // end for i

			//
			// A row is judged piece by piece, on the frames the single ghost would have been: red
			// where the click will leave a gap, and the cursor says yes while any piece can go up.
			//
			const Bool judgeRow = row && ( TheGameClient->getFrame() & 0x1 );
			if( judgeRow )
			{
				TheTerrainVisual->removeAllBibs();
				m_placementLegal = FALSE;
			}

			//
			// march down each drawable and set the position based on its position in the
			// line and set their angles all the same
			//
			for( i = 0; i < iconsUsed; i++ )
			{

				// set the drawble position
				m_placeIcon[ i ]->setPosition( &positions[ i ] );

				// set opacity and shadowing for the drawble
				dressPlacementPreview( m_placeIcon[ i ] );

				// set the drawable angle
				m_placeIcon[ i ]->setOrientation( angle );

				if( judgeRow )
				{
					const Bool legal =
						TheBuildAssistant->isLocationLegalToBuild( &positions[ i ], m_pendingPlaceType, angle,
																											 placementCheckOptions(), builderObject,
																											 NULL ) == LBC_OK &&
						!overlapsPendingPlacement( &positions[ i ], m_pendingPlaceType, angle );
					m_placeIcon[ i ]->colorTint( legal ? NULL : &illegalBuildColor );
					if( legal )
						m_placementLegal = TRUE;
				}

			}  // end for i

		}  // end if

		//
		// destroy any drawables that we're not using anymore because a previous line length was
		// longer, or the row was let go of
		//
		for( Int i = iconsUsed; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		{

			if( m_placeIcon[ i ] != NULL )
				TheGameClient->destroyDrawable( m_placeIcon[ i ] );
			m_placeIcon[ i ] = NULL;

		}  // end for i

	}  // end if

}  // end handleBuildPlacements

static Int radarFrameTop( void );

//-------------------------------------------------------------------------------------------------
/** Pre-draw phase of the in game ui */
//-------------------------------------------------------------------------------------------------
void InGameUI::preDraw( void )
{

	// handle any "icons" for the act of building things and placing them in the world
	handleBuildPlacements();

	// a Direct Strike unit riding the pointer to its plot slot
	DirectStrikeHud_preDraw();
	// and a decided Direct Strike match's end, read here at the sync point
	DirectStrikeEndScreen_preDraw();

	// handle radius-cursors, if any
	handleRadiusCursor();

	// where the selection is headed, read fresh from the units themselves
	updateOrderHints();

	// the build grid under a structure waiting to be placed is not drawn here: it is terrain
	// geometry now, and HeightMapRenderObjClass::Render puts it down with the ground itself

	// draw the floating text first;
	drawFloatingText();

	// the SIHA recon chips and trays, and the glyphs beside classified radar rings; a tray stays
	// clear of the command bar and the radar's frame, the highest of it
	RadarContactMarks_draw( radarFrameTop() );

	// draw world animations
	updateAndDrawWorldAnimations();

}  // end preDraw

//-------------------------------------------------------------------------------------------------
/** Update the in game user interface */
//-------------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(InGameUI_update)
void InGameUI::update( void )
{
	USE_PERF_TIMER(InGameUI_update)

	// a client pass draws (GameClient::update's DRAW) before it gets here; the clicks after it may use
	// the HUD cells' boxes only if that draw put them there.  See handleHudCellClick
	m_hudCellPass++;

	updateReplaySeek();

	// the wheeled radius belongs to the guard it was wheeled for; a shift-kept key keeps it
	if( !m_guardArmed )
		m_guardRadiusScale = 1.0f;

	/// @todo make sure this code gets called even when the UI is not being drawn
	if ( m_videoStream && m_videoBuffer )
	{
		if ( m_videoStream->isFrameReady())
		{
			m_videoStream->frameDecompress();
			m_videoStream->frameRender( m_videoBuffer );
			m_videoStream->frameNext();
			if ( m_videoStream->frameIndex() == 0 )
			{
				stopMovie();
			}
		}
	}

	if ( m_cameoVideoStream && m_cameoVideoBuffer )
	{
		if ( m_cameoVideoStream->isFrameReady())
		{
			m_cameoVideoStream->frameDecompress();
			m_cameoVideoStream->frameRender( m_cameoVideoBuffer );
			m_cameoVideoStream->frameNext();
//			if ( m_cameoVideoStream->frameIndex() == 0 )
//			{
//				stopMovie();
//			}
		}
	}

	// the messages are lines of the event feed now, which drops them at drawing by their frame
	UnsignedInt currLogicFrame = TheGameLogic->getFrame();
	if( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
		watchDozers();
	updateSignalMarks();
	ReachOverlay_update();		// the ground a selected or sited defence hits, drawn on the terrain
	UnsignedByte r, g, b, a;
	Int amount;

	//
	// Update the Military Subtitle display
	//
	if( m_militarySubtitle )		// if we have a subtitle, work on it
	{
		// if the timeis frozen by a script, then we still want the text to display
		if(TheScriptEngine->isTimeFrozenScript())
		{
			//
			// These are LOGIC-frame counters walked down by hand to fake time passing while the
			// logic clock is frozen.  They cannot be gated on the logic frame - that is exactly
			// what is not advancing - and one step per call was one step per RENDER frame, so
			// with the renderer uncapped a cutscene's subtitles ran down (fps/30) times too fast.
			// The wall clock supplies the missing 30Hz: count the frames the freeze has lasted
			// so far and apply only the ones not applied yet, so nothing drifts however long it
			// runs.  See FINDINGS.md 7.2.
			//
			const UnsignedInt nowMs = Clock_Milliseconds();
			if( m_subtitleFreezeStartMs == 0 )
			{
				m_subtitleFreezeStartMs = nowMs;
				m_subtitleFreezeSteps = 0;
			}
			const UnsignedInt elapsedFrames = (nowMs - m_subtitleFreezeStartMs) * LOGICFRAMES_PER_SECOND / 1000;
			const UnsignedInt steps = elapsedFrames - m_subtitleFreezeSteps;
			m_subtitleFreezeSteps = elapsedFrames;
			m_militarySubtitle->lifetime -= min( steps, m_militarySubtitle->lifetime );
			m_militarySubtitle->blockBeginFrame -= min( steps, m_militarySubtitle->blockBeginFrame );
			m_militarySubtitle->incrementOnFrame -= min( steps, m_militarySubtitle->incrementOnFrame );
		}
		else
		{
			m_subtitleFreezeStartMs = 0;
		}
		// if it's time to remove the subtitle, Then remove it
		if((Int)m_militarySubtitle->lifetime < (Int)currLogicFrame)
		{
			// the fade takes 0.1 more alpha each step than the last, so it is stepped once a retail
			// frame of the subtitle's age, the logic frame and render passes in between holding the
			// colour; a script freeze ages it through lifetime, so the age is the clock, not the frame
			const UnsignedInt fadeStep = (currLogicFrame - m_militarySubtitle->lifetime) / LOGICFRAMES_PER_RETAIL_FRAME;
			if( fadeStep != m_militarySubtitle->fadeStep )
			{
				m_militarySubtitle->fadeStep = fadeStep;
				//steal colins fade from above :)
				GameGetColorComponents( m_militarySubtitle->color, &r, &g, &b, &a );
				// start fading the alpha on this color down
				amount = REAL_TO_INT( fadeStep * 0.1f );
				if( a - amount < 0 )
				{
					removeMilitarySubtitle();
				}
				else
				{
					a -= amount;
					m_militarySubtitle->color = GameMakeColor(r, g, b, a);
				}
			}
		}
		else
		{
			// trigger whether or not we should draw the block	
			// retail toggled every 10 frames, its 9 plus the strict <'s one; the 10 is doubled, not the 9
			if( m_militarySubtitle->blockBeginFrame + ConvertRetailFramesWall( 10 ) - 1 < currLogicFrame )
			{
				m_militarySubtitle->blockBeginFrame = currLogicFrame;
				m_militarySubtitle->blockDrawn = !m_militarySubtitle->blockDrawn;
			}

			// If it's time to add another letter to the display string, lets do that.
			if( m_militarySubtitle->incrementOnFrame < currLogicFrame )
			{
				// first grab the letter we want to add
				WideChar tempWChar = m_militarySubtitle->subtitle.getCharAt(m_militarySubtitle->index);
				// if that letter is a return, add a new line
				if(tempWChar == u'\n')
				{
					// increment the Block position's Y value to draw it on the next line
					Int height;
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getSize(NULL, &height);
					m_militarySubtitle->blockPos.y = m_militarySubtitle->blockPos.y + height;

					// Now add a new display string
					m_militarySubtitle->currentDisplayString++;
					if(!(m_militarySubtitle->currentDisplayString >= MAX_SUBTITLE_LINES) )
					{	
						m_militarySubtitle->blockPos.x = m_militarySubtitle->position.x;
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString] = TheDisplayStringManager->newDisplayString();
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->reset();
						m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->setFont(	TheFontLibrary->getFont( m_militaryCaptionFont, TheGlobalLanguageData->adjustFontSize(m_militaryCaptionPointSize), m_militaryCaptionBold ) )	;

						m_militarySubtitle->blockDrawn = TRUE;
						m_militarySubtitle->incrementOnFrame = currLogicFrame + (Int)(((Real)LOGICFRAMES_PER_SECOND * TheGlobalLanguageData->m_militaryCaptionDelayMS)/1000.0f);
					}
					else
					{
						// if we've exceeded the allocated number of display strings, this will force us to essentially truncate the remaining text
						//
						// roll the index back to the last real line. It used to be left at
						// MAX_SUBTITLE_LINES, and displayStrings only has that many entries, so the
						// draw loops (which run i <= currentDisplayString) read displayStrings[4]
						// and removeMilitarySubtitle then freed that out-of-bounds pointer.
						//
						m_militarySubtitle->currentDisplayString = MAX_SUBTITLE_LINES - 1;
						m_militarySubtitle->index = m_militarySubtitle->subtitle.getLength();
						DEBUG_CRASH(("You're Only Allowed to use %d lines of subtitle text\n",MAX_SUBTITLE_LINES));
					}
				}
				else
				{
					// okay, we're not a \n, lets append this character to the display string
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->appendChar(tempWChar);
					// increment the draw position of the block
					Int width;
					m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getSize(&width,NULL);
					m_militarySubtitle->blockPos.x = m_militarySubtitle->position.x + width;

					// lets make a sound
					static AudioEventRTS click("MilitarySubtitlesTyping");
					TheAudio->addAudioEvent(&click);
					// a letter every speed + 1 retail frames, the strict < above being the + 1: the speed is
					// parsed doubled, so the + 1 is doubled here too
					if(TheGlobalLanguageData)
						m_militarySubtitle->incrementOnFrame = currLogicFrame + TheGlobalLanguageData->m_militaryCaptionSpeed + LOGICFRAMES_PER_RETAIL_FRAME - 1;
					else
						m_militarySubtitle->incrementOnFrame = currLogicFrame + m_militaryCaptionSpeed + LOGICFRAMES_PER_RETAIL_FRAME - 1;

				}
				// increment the index			
				m_militarySubtitle->index++;
				if(m_militarySubtitle->index >= m_militarySubtitle->subtitle.getLength())
				{
					// We're at the end of the subtitle, set everything to persist till the subtitle has expired
					m_militarySubtitle->incrementOnFrame = m_militarySubtitle->lifetime + 1;
				}
	/*
							else
								{
									// randomize the space between printing of characters
									if(GameClientRandomValueReal(0,1) < 0.95f)
									{
										m_militarySubtitle->incrementOnFrame = GameClientRandomValue(2, 5) + currLogicFrame;
									}
									else
									{
										m_militarySubtitle->incrementOnFrame = GameClientRandomValue(10, 13) + currLogicFrame;
									}
								}*/
				
			}
		}
	}

	//
	// Update the player money window if the money amount has changed.  The amount last written is
	// a member and not a static inside this function: the gadget is thrown away and built again
	// whenever the command bar is, and a cache that outlives the window it was filled for left the
	// new one showing the dollar signs its .wnd ships with until the player's money next moved.
	//
	// This is also as good a place as any to do the power hide/show.
	//
	static NameKeyType moneyWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:MoneyDisplay" );	
	static NameKeyType powerWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:PowerWindow" );	

	GameWindow *moneyWin = TheWindowManager->winGetWindowFromId( NULL, moneyWindowKey );
	GameWindow *powerWin = TheWindowManager->winGetWindowFromId( NULL, powerWindowKey );
//	if( moneyWin == NULL )
//	{
//		NameKeyType moneyWindowKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:MoneyDisplay" );	
//
//		moneyWin = TheWindowManager->winGetWindowFromId( NULL, moneyWindowKey );
//
//	}  // end if
	Player *moneyPlayer = NULL;
	if( TheControlBar->isObserverControlBarOn())
		moneyPlayer = TheControlBar->getObserverLookAtPlayer();
	else
		moneyPlayer = ThePlayerList->getLocalPlayer();
	if( moneyPlayer)
	{
		// -2 stands for Unlimited, apart from the -1 that a reset leaves, so switching the observed
		// player between an unlimited purse and a counted one still rewrites the gadget
		const Bool unlimited = moneyPlayer->getMoney()->isUnlimited();
		const Int purse = unlimited ? -2 : (Int)moneyPlayer->getMoney()->countMoney();
		// the figure counts up and down to the purse on the wall clock, as Red Alert 2's did; it snaps
		// when the bar first shows it, for another player's purse, and to or from Unlimited
		const UnsignedInt nowMs = Clock_Milliseconds();
		Int currentMoney = purse;
		if( !unlimited && m_lastMoneyDisplayed >= 0 && moneyPlayer == m_moneyShownPlayer )
			currentMoney = stepMoneyShown( m_lastMoneyDisplayed, purse, nowMs - m_moneyShownMs );
		m_moneyShownPlayer = moneyPlayer;
		m_moneyShownMs = nowMs;
		// the IncomeReadout option, which drawIncomeReadout puts on a plate of its own at the money box:
		// the player's own bar only (an observer's box is fitted to the figure) and never by Unlimited
		s_incomeShown = incomeReadoutRate( TheControlBar->isObserverControlBarOn() ? NULL : moneyPlayer, &s_incomeModeShown );
		if( unlimited )
			s_incomeModeShown = INCOME_READOUT_OFF;
		if( m_lastMoneyDisplayed != currentMoney )
		{
			UnicodeString buffer;
			if( unlimited )
				buffer = TheGameText->fetch( "GUI:StartingMoneyUnlimited" );
			else
				buffer.format( TheGameText->fetch( "GUI:ControlBarMoneyDisplay" ), currentMoney );
			if( !unlimited && TheControlBar->getControlBarSchemeManager()->isMoneySignPainted() )
				buffer = amountOnly( buffer );
			if( !unlimited && TheControlBar->getControlBarSchemeManager()->isMoneyGrouped() )
				buffer = groupThousands( buffer );
			GadgetStaticTextSetText( moneyWin, buffer );
			m_lastMoneyDisplayed = currentMoney;

		}  // end if

		// put back on screen every frame, which is how they survive the context switching that hides
		// everything else on the bar
		moneyWin->winHide( FALSE );
		powerWin->winHide( FALSE );
		if( TheControlBar->isObserverControlBarOn() )
			fitObserverMoney( moneyWin, currentMoney, unlimited );
	}
	else
	{
		moneyWin->winHide(TRUE);
		powerWin->winHide(TRUE);
		m_moneyShownPlayer = NULL;
		s_incomeModeShown = INCOME_READOUT_OFF;
	}
	
	// Update the floating Text;
	updateFloatingText();

	// update the control bar
	TheControlBar->update();

	updateIdleUnitButtons();

	// update any random window layout that so requests
	for (std::list<WindowLayout *>::iterator it = m_windowLayouts.begin(); it != m_windowLayouts.end(); ++it)
	{
		WindowLayout *layout = *it;
		layout->runUpdate();
	}

	//
	// Handle keyboard camera rotations.  These used to apply one whole step per call and this
	// update runs once per RENDER frame, so with the renderer uncapped the camera swung as fast
	// as the machine happened to draw - the same key held for the same time went a different
	// distance on every PC, and on a fast one a tap threw the view right past what you wanted.
	// The step sizes are written for the 30Hz logic rate, so scale them by how many of those
	// frames the wall clock says went by.  Capped at four, so coming back from a hitch or an
	// alt-tab does not fling the camera across the map on the first frame.  See FINDINGS.md 7.4.
	//
	const UnsignedInt cameraNowMs = Clock_Milliseconds();

	// a watcher's camera is driven for him while it is not in his own hands (ObserverCamera.h)
	if( TheGameLogic->isInGame() && localPlayerWatching() )
	{
		// -directorrecord: the director has the camera all match, no interface and no radar is over the
		// picture, and the glide runs on the logic clock, which is what each recorded picture is
		UnsignedInt observerNowMs = cameraNowMs;
		if( TheGlobalData->m_directorRecord && !TheGameLogic->isInShellGame() )
		{
			if( TheObserverCamera.getMode() != OBSERVER_CAMERA_DIRECTOR )
				TheObserverCamera.setMode( OBSERVER_CAMERA_DIRECTOR );
			if( !CinemaDirector_isHudHidden() )
				CinemaDirector_setHudHidden( TRUE, FALSE );
			TheMouse->setVisibility( FALSE );
			// the picture's own time, a share of a frame before the logic frame for a -recordfps
			// picture between two of them
			observerNowMs = GameEngine_pictureMilliseconds();
		}
		// a looked-through player who lost or left sends no camera any more, and his last one would
		// hold the screen still: the look ends, back to the watcher's own
		const Int looked = TheObserverCamera.getFollowedPlayerIndex();
		if( TheObserverCamera.isLooking() && looked != ObserverCamera::NO_PLAYER && !ThePlayerList->getNthPlayer( looked )->isPlayerActive() )
			letGoOfPlayerCamera();
		TheObserverCamera.update( observerNowMs );
	}

	Real cameraSteps = 1.0f;
	if( m_cameraKeyLastMs != 0 )
		cameraSteps = (Real)(cameraNowMs - m_cameraKeyLastMs) * (RETAIL_LOGICFRAMES_PER_SECOND / 1000.0f);
	m_cameraKeyLastMs = cameraNowMs;
	if( cameraSteps > 4.0f )
		cameraSteps = 4.0f;

	//
	// SnapCameraRotateTo45 makes the heading discrete rather than smoothed-then-settled: the camera
	// stands on an eighth and jumps to the next one, so there is no in-between heading to look at
	// and nothing left to settle when the key comes up.  The angle is therefore not accumulated per
	// frame at all - a press turns the view one eighth and holding the key repeats that on a fixed
	// interval, which is what the rotate keys do in Stronghold.
	//
	const UnsignedInt CAMERA_SNAP_REPEAT_MS = 250;
	// Shift flips it while held, the same as on the middle-button drag (LookAtXlat).
	if( TheGlobalData->m_snapCameraRotateTo45 != TheKeyboard->isShift() )
	{
		if( (m_cameraRotatingLeft || m_cameraRotatingRight) && m_cameraRotatingLeft != m_cameraRotatingRight
				&& cameraNowMs - m_cameraSnapRepeatMs >= CAMERA_SNAP_REPEAT_MS )
		{
			m_cameraSnapRepeatMs = cameraNowMs;
			TheTacticalView->setAngle( View_stepAngleByEighths( TheTacticalView->getAngle(),
																													m_cameraRotatingRight ? 1 : -1 ) );
		}
	}
	else
	{
		if( m_cameraRotatingLeft && !m_cameraRotatingRight )
		{
			//Keyboard rotate left
			TheTacticalView->setAngle( TheTacticalView->getAngle() - TheGlobalData->m_keyboardCameraRotateSpeed * cameraSteps );
		}
		if( m_cameraRotatingRight && !m_cameraRotatingLeft )
		{
			//Keyboard rotate right
			TheTacticalView->setAngle( TheTacticalView->getAngle() + TheGlobalData->m_keyboardCameraRotateSpeed * cameraSteps );
		}
	}
	if( m_cameraZoomingIn && !m_cameraZoomingOut )
	{
		//Keyboard zoom in
		TheTacticalView->zoomIn( cameraSteps );
	}
	if( m_cameraZoomingOut && !m_cameraZoomingIn )
	{
		//Keyboard zoom out
		TheTacticalView->zoomOut( cameraSteps );
	}

}  // end update

//-------------------------------------------------------------------------------------------------
void InGameUI::registerWindowLayout( WindowLayout *layout )
{
	unregisterWindowLayout(layout); // sanity
	m_windowLayouts.push_back(layout);
}

//-------------------------------------------------------------------------------------------------
void InGameUI::unregisterWindowLayout( WindowLayout *layout )
{
	for (std::list<WindowLayout *>::iterator it = m_windowLayouts.begin(); it != m_windowLayouts.end(); ++it)
	{
		if (*it == layout)
		{
			m_windowLayouts.erase(it);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Reset the in game user interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::reset( void )
{
	for( Int i = 0; i < MAX_MOVE_HINTS; i++ )
	{
		m_moveHint[ i ].pos.zero();
		m_moveHint[ i ].frame = 0;
		m_moveHint[ i ].bornMs = 0;
		m_moveHint[ i ].hull = 0.0f;
	}
	m_nextMoveHint = 0;
	m_commandedPoints.clear();
	m_queuedClicks.clear();
	m_orderRings.clear();
	m_pendingOrders.clear();
	m_pendingRallies.clear();

	m_isQuitMenuVisible = FALSE;
	TheObserverCamera.reset();
	CinemaDirector_forgetHudHidden();
	clearSignalMarks();
	RadarContactMarks_reset();
	// a load half picked does not outlive its match
	AirliftPanel_close();
	m_armedSignal = SIGNAL_KIND_COUNT;
	m_dozerCheckFrame = 0;
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
		m_hadDozer[ index ] = FALSE;
	m_inputEnabled = true;
	// reset the command bar
	TheControlBar->reset();

	TheTacticalView->setDefaultView(0.0f, 0.0f, 1.0f);

	ResetInGameChat();

	// stop any movie currently playing
	stopMovie();

	// remove any pending GUI command
	setGUICommand( NULL );

	// remove any build available status
	placeBuildAvailable( NULL, NULL );
	m_placeAngleOffset = 0.0f;
	m_placeAngleType = NULL;
	forgetPendingPlacements();

	// free any message resources allocated
	freeMessageResources();

	Int i;
	for (i=0; i<MAX_PLAYER_COUNT; ++i)
	{
		for (SuperweaponMap::iterator mapIt = m_superweapons[i].begin(); mapIt != m_superweapons[i].end(); ++mapIt)
		{
			for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
			{
				SuperweaponInfo *info = *listIt;
				info->deleteInstance();
			}
			mapIt->second.clear();
		}
		m_superweapons[i].clear();
	}

	for (NamedTimerMapIt timerIt = m_namedTimers.begin(); timerIt != m_namedTimers.end(); ++timerIt)
	{
		NamedTimerInfo *info = timerIt->second;
		TheDisplayStringManager->freeDisplayString(info->displayString);
		info->deleteInstance();
	}
	m_namedTimers.clear();
	m_namedTimerLastFlashFrame = 0;
	m_namedTimerUsedFlashColor = TRUE; // so next one is false
	m_showNamedTimers = TRUE;

	removeMilitarySubtitle();
	clearPopupMessageData();
	m_superweaponLastFlashFrame = 0;
	m_superweaponUsedFlashColor = TRUE; // so next one is false
	m_superweaponHiddenByScript = FALSE;

	clearFloatingText();
	clearWorldAnimations();
	resetIdleWorker();
	m_moneyShownPlayer = NULL;		// the next match's purse shows at once, not counted to from this one's

	m_waypointMode			= false;
	m_forceAttackMode		= false;
	m_forceMoveToMode		= false;
	m_attackMoveToMode	= false;
	m_forceAttackArmed	= false;
	m_guardArmed				= false;
	m_guardArmedMode		= GUARDMODE_GUARD_WITHOUT_PURSUIT;
	m_guardRadiusScale	= 1.0f;
	m_moveArmed					= false;
	m_orderKeyKeptByShift	= false;
	m_preferSelection		= false;
	m_isAttackCircling	= FALSE;
	m_clientQuiet    = false;
	m_markerShots.clear();
	m_markerAim.type = RADIUSCURSOR_NONE;
	m_markerAim.endMs = 0;

	// TheSuperHackers @bugfix A key or button still held when the game ended left its camera
	// interaction latched on, and the shell then scrolled or spun on its own.
	setScrolling(false);
	setSelecting(false);
	setCameraRotateLeft(false);
	setCameraRotateRight(false);
	setCameraZoomIn(false);
	setCameraZoomOut(false);
	setCameraTrackingDrawable(false);

	m_windowLayouts.clear();

	m_tooltipsDisabledUntil = 0;

	UpdateDiplomacyBriefingText(AsciiString::TheEmptyString, TRUE);
}  // end reset

//-------------------------------------------------------------------------------------------------
/** Empty the message list: a new match, and the score screen, start without the last one's lines */
//-------------------------------------------------------------------------------------------------
void InGameUI::freeMessageResources( void )
{
	freeClassicMessages();
}  // end freeMessageResources

//-------------------------------------------------------------------------------------------------
void InGameUI::freeClassicMessages( void )
{
	for( Int i = 0; i < CLASSIC_MESSAGES; i++ )
	{
		if( m_classicMessages[ i ].text )
			TheDisplayStringManager->freeDisplayString( m_classicMessages[ i ].text );
		m_classicMessages[ i ].text = NULL;
		m_classicMessages[ i ].frame = 0;
	}
}

//-------------------------------------------------------------------------------------------------
/** EA's InGameUI::addMessageText: the oldest line falls off the end, the new one goes in at the top
	* in white or grey by turns, or in the colour it was given. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addClassicMessage( const UnicodeString &text, const Color *color )
{
	// a tick's line (message, playerMessage, chatMessage) is formatted at the call and added after it
	if( LogicThread::defersEffects() )
	{
		const Bool hasColor = color != NULL;
		const Color colorValue = hasColor ? *color : 0;
		LogicThread::callAfterTick( [=]() { addClassicMessage( text, hasColor ? &colorValue : NULL ); } );
		return;
	}
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	const Color color1 = color ? ( *color | GameMakeColor( 0, 0, 0, 255 ) ) : m_messageColor1;
	const Color color2 = color ? ( *color | GameMakeColor( 0, 0, 0, 255 ) ) : m_messageColor2;

	if( m_classicMessages[ CLASSIC_MESSAGES - 1 ].text )
		TheDisplayStringManager->freeDisplayString( m_classicMessages[ CLASSIC_MESSAGES - 1 ].text );
	for( Int i = CLASSIC_MESSAGES - 1; i >= 1; i-- )
		m_classicMessages[ i ] = m_classicMessages[ i - 1 ];

	ClassicMessage &added = m_classicMessages[ 0 ];
	added.frame = TheGameLogic->getFrame();
	added.text = TheDisplayStringManager->newDisplayString();
	added.text->setFont( TheFontLibrary->getFont( m_messageFont,
		TheGlobalLanguageData->adjustFontSize( m_messagePointSize ), m_messageBold ) );
	added.text->setText( text );
	added.color = ( m_classicMessages[ 1 ].text == NULL || m_classicMessages[ 1 ].color == color2 ) ? color1 : color2;
}

//-------------------------------------------------------------------------------------------------
/** EA's fade, once a logic frame: a line loses alpha by a hundredth of its age every frame from the
	* start (MessageDelayMS divided by the frame rate and by a thousand is 0 in EA's arithmetic), so a
	* line is gone about seven seconds after it came. */
//-------------------------------------------------------------------------------------------------
void InGameUI::fadeClassicMessages( void )
{
	const UnsignedInt now = TheGameLogic->getFrame() / LOGICFRAMES_PER_RETAIL_FRAME;	// EA's steps are retail 30 Hz frames
	if( now == m_classicMessageFadeFrame )
		return;
	m_classicMessageFadeFrame = now;

	const UnsignedInt timeout = m_messageDelayMS / LOGICFRAMES_PER_SECOND / 1000;
	for( Int i = CLASSIC_MESSAGES - 1; i >= 0; i-- )
	{
		ClassicMessage &line = m_classicMessages[ i ];
		if( line.text == NULL || now - line.frame / LOGICFRAMES_PER_RETAIL_FRAME <= timeout )
			continue;
		UnsignedByte r, g, b, a;
		GameGetColorComponents( line.color, &r, &g, &b, &a );
		const Int amount = REAL_TO_INT( ( now - line.frame / LOGICFRAMES_PER_RETAIL_FRAME ) * 0.01f );
		a = ( a - amount < 0 ) ? 0 : a - amount;
		line.color = GameMakeColor( r, g, b, a );
		if( a == 0 )
		{
			TheDisplayStringManager->freeDisplayString( line.text );
			line.text = NULL;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Where an x EA gave as a fraction of its 800 wide screen lands at the HUD's one scale: as far from
	* the nearer side edge as EA's was, so a column EA put on the right stays on the right of a 32:9
	* screen.  The edges are hudPlay's: the HudWidth area's, and the sidebar's left under Red Alert
	* style.  At 4:3 this is EA's own position. */
//-------------------------------------------------------------------------------------------------
static Int hudEdgeX( Real fraction )
{
	const Real s = ControlBarUniformScale();
	const IRegion2D play = hudPlay();
	if( fraction < 0.5f )
		return play.lo.x + REAL_TO_INT( fraction * 800.0f * s );
	return play.hi.x - REAL_TO_INT( ( 1.0f - fraction ) * 800.0f * s );
}

//-------------------------------------------------------------------------------------------------
void InGameUI::drawClassicMessages( void )
{
	fadeClassicMessages();
	if( !m_messagesOn )
		return;

	// the screen's own top left corner, not the menus' 4:3 box; HudWidth's centred area's on a screen
	// wider than the choice
	const Int x = hudPlay().lo.x + m_messagePosition.x;
	Int y = m_messagePosition.y;
	for( Int i = CLASSIC_MESSAGES - 1; i >= 0; i-- )
	{
		ClassicMessage &line = m_classicMessages[ i ];
		if( line.text == NULL )
			continue;
		UnsignedByte r, g, b, a;
		GameGetColorComponents( line.color, &r, &g, &b, &a );
		line.text->draw( x, y, line.color, GameMakeColor( 0, 0, 0, a ) );
		y += line.text->getFont()->height;
	}
}

//-------------------------------------------------------------------------------------------------
/** The foot of the message lines as drawClassicMessages stacks them when all six are up:
	* MessagePosition's y and six lines of the message font, the language's when it names one.  All
	* six whether or not they show now, so whatever sits under them does not jump as lines come and
	* fade (the squad list). */
//-------------------------------------------------------------------------------------------------
Int InGameUI::getMessageLinesBottom( void ) const
{
	GameFont *font = TheFontLibrary->getFont( m_messageFont,
		TheGlobalLanguageData->adjustFontSize( m_messagePointSize ), m_messageBold );
	return m_messagePosition.y + ( font != NULL ? CLASSIC_MESSAGES * font->height : 0 );
}

//-------------------------------------------------------------------------------------------------
/** Same as the unicode message method, but this takes an ascii string which is assumed
	* to me a string manager label */
//-------------------------------------------------------------------------------------------------
// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
void InGameUI::message( AsciiString stringManagerLabel, ... )
{
	UnicodeString stringManagerString;
	UnicodeString formattedMessage;

	// fetch the string from the string manger
	stringManagerString = TheGameText->fetch( stringManagerLabel.str() );

	// construct the final text after formatting
	va_list args;
	// C4840 here is va_start naming a class-typed last parameter, which both compilers handle;
	// it is kept on everywhere else to catch a string object passed through the "...".
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4840)
#endif
  va_start( args, stringManagerLabel );
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	WideChar buf[ UnicodeString::MAX_FORMAT_BUF_LEN ];
  // truncate rather than throw: an uncaught engine exception aborts with 0xC0000409 and no log
  // at all, so an over-long chat or script message used to be a silent hard crash.
  // WideCharFormatV, not _vsnwprintf: buf is WideChar and there is no char16_t printf anywhere.
  // It keeps _vsnwprintf's contract, so the negative test below still means "it did not fit".
  if( WideCharFormatV(buf, sizeof( buf )/sizeof( WideChar ) - 1, stringManagerString.str(), args ) < 0 )
			DEBUG_LOG(("InGameUI::message - text truncated to %d characters\n", (Int)(sizeof( buf )/sizeof( WideChar ) - 1)));
	buf[ sizeof( buf )/sizeof( WideChar ) - 1 ] = 0;
	formattedMessage.set( buf );
  va_end(args);

	// add the text to the ui
	addMessageText( formattedMessage );

}  // end 

//-------------------------------------------------------------------------------------------------
/** Interface for display text messages to the user */
//-------------------------------------------------------------------------------------------------
// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
void InGameUI::message( UnicodeString format, ... )
{
	UnicodeString formattedMessage;

	// construct the final text after formatting
	va_list args;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4840)
#endif
  va_start( args, format );
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	WideChar buf[ UnicodeString::MAX_FORMAT_BUF_LEN ];
  // truncate rather than throw: an uncaught engine exception aborts with 0xC0000409 and no log
  // at all, so an over-long chat or script message used to be a silent hard crash.
  if( WideCharFormatV(buf, sizeof( buf )/sizeof( WideChar ) - 1, format.str(), args ) < 0 )
			DEBUG_LOG(("InGameUI::message - text truncated to %d characters\n", (Int)(sizeof( buf )/sizeof( WideChar ) - 1)));
	buf[ sizeof( buf )/sizeof( WideChar ) - 1 ] = 0;
	formattedMessage.set( buf );
  va_end(args);

	// add the text to the ui
	addMessageText( formattedMessage );

}  // end message

//-------------------------------------------------------------------------------------------------
/** A message is a line of EA's list over the battlefield's top left. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addMessageText( const UnicodeString& formattedMessage )
{
	addClassicMessage( formattedMessage, NULL );
}

//-------------------------------------------------------------------------------------------------
/** A message about one player, in his colour as EA wrote it: a player beaten, a player gone. */
//-------------------------------------------------------------------------------------------------
void InGameUI::playerMessage( Player *player, const UnicodeString &text )
{
	const Color color = player->getPlayerColor();
	addClassicMessage( text, &color );
}

//-------------------------------------------------------------------------------------------------
/** Is the local screen told what `player` did where nobody else is?  A watcher is told everything,
	* a player what he or a mutual ally did: an enemy's promotion or his power going up is not his to
	* know. */
//-------------------------------------------------------------------------------------------------
static Bool feedShows( const Player *player )
{
	const Player *local = ThePlayerList->getLocalPlayer();
	if( !local->isPlayerActive() || player == local )
		return TRUE;
	return player->getRelationship( local->getDefaultTeam() ) == ALLIES &&
				 local->getRelationship( player->getDefaultTeam() ) == ALLIES;
}

//-------------------------------------------------------------------------------------------------
/** Once a second: a player whose last dozer, or last worker for the GLA, has just gone gets a line,
	* since without one he can build nothing more.  Only where feedShows says, and not for a player
	* already beaten, whose last dozer went with everything else. */
//-------------------------------------------------------------------------------------------------
void InGameUI::watchDozers( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame < m_dozerCheckFrame + LOGICFRAMES_PER_SECOND && frame >= m_dozerCheckFrame )
		return;
	m_dozerCheckFrame = frame;

	KindOfMaskType dozers;
	dozers.set( KINDOF_DOZER );
	for( Int index = 0; index < ThePlayerList->getPlayerCount() && index < MAX_PLAYER_COUNT; index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		const Bool has = player->isPlayableSide() && player->countObjects( dozers, KINDOFMASK_NONE ) > 0;
		const Bool lost = m_hadDozer[ index ] && !has;
		m_hadDozer[ index ] = has;
		if( !lost || TheVictoryConditions->hasSinglePlayerBeenDefeated( player ) || !feedShows( player ) )
			continue;

		const Bool workers = player->getPlayerTemplate()->getSide().startsWith( "GLA" );
		UnicodeString text;
		text.format( TheGameText->fetch( workers ? "GUI:HudNoWorkerLeft" : "GUI:HudNoDozerLeft" ), player->getPlayerDisplayName().str() );
		playerMessage( player, text );
	}
}

//-------------------------------------------------------------------------------------------------
/** Where the command bar's left piece, the radar, starts on screen, or the screen's bottom while the
	* bar is hidden: it is hidden while the game loads and in the observer views.  ControlBar Pro's bar
	* is three pieces with the world between them, and what stands over the bar on the left stands on
	* the radar's piece; it travels with the frame when the bar drops. */
//-------------------------------------------------------------------------------------------------
static Int controlBarTop( void )
{
	static NameKeyType controlBarKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
	GameWindow *bar = TheWindowManager->winGetWindowFromId( NULL, controlBarKey );
	if( bar == NULL || bar->winIsHidden() )
		return TheDisplay->getHeight();

	ICoord2D barPos;
	bar->winGetScreenPosition( &barPos.x, &barPos.y );
	return TheControlBar->getPanelRect( ControlBar::CB_PANEL_LEFT )->lo.y + barPos.y - TheControlBar->getPanelOrigin()->y;
}

//-------------------------------------------------------------------------------------------------
/** The top of the radar's frame as it stands.  The map's shape only widens or narrows it, so this is
	* the scheme's radar block for every map.  A scheme with no frame gives the top of its painting's
	* radar box and ledge (its BlockRadar) where it names one, and controlBarTop otherwise, as does a
	* hidden radar. */
//-------------------------------------------------------------------------------------------------
static Int radarFrameTop( void )
{
	// the Red Alert style radar is up in the sidebar: what stood on the radar's frame on the left
	// stands on the selection card instead (every caller stands at the left edge, where the card is)
	if( sidebarLayoutOn() )
		return hudRects().card.lo.y;

	static NameKeyType radarKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:LeftHUD" );
	const Image *const *frame = TheControlBar->getControlBarSchemeManager()->getRadarFrame();
	GameWindow *radar = TheWindowManager->winGetWindowFromId( NULL, radarKey );
	if( radar == NULL || radar->winIsHidden() || controlBarTop() >= TheDisplay->getHeight() )
		return controlBarTop();
	if( frame == NULL )
		return TheControlBar->getControlBarSchemeManager()->radarArtTop( controlBarTop() );

	IRegion2D inner, outer;
	radar->winGetScreenPosition( &inner.lo.x, &inner.lo.y );
	radar->winGetSize( &inner.hi.x, &inner.hi.y );
	inner.hi.x += inner.lo.x;
	inner.hi.y += inner.lo.y;
	ControlBarSchemeManager::radarFrameRect( frame, inner, &outer );
	return outer.lo.y;
}

//-------------------------------------------------------------------------------------------------
/** A line of chat from `player`, EA's way: "[name] text" in the message list, in his colour. */
//-------------------------------------------------------------------------------------------------
void InGameUI::chatMessage( Player *player, const UnicodeString &text )
{
	UnicodeString said;
	said.format( u"[%ls] %ls", player->getPlayerDisplayName().str(), text.str() );
	const Color color = player->getPlayerColor();
	addClassicMessage( said, &color );
}

//-------------------------------------------------------------------------------------------------
/** An area selection is occurring, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::beginAreaSelectHint( const GameMessage *msg )
{
	m_isDragSelecting = true;
	m_dragSelectRegion = msg->getArgument( 0 )->pixelRegion;
}

//-------------------------------------------------------------------------------------------------
/** An area selection has occurred, finish graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::endAreaSelectHint( const GameMessage *msg )
{
	m_isDragSelecting = false;
}

//-------------------------------------------------------------------------------------------------
/** The player is dragging out the line a formation move will spread along. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addFormationDragPoint( const ICoord2D& pt )
{
	m_isFormationDragging = TRUE;

	if( m_formationDragPoints.empty() )
	{
		m_formationDragSpacing = FORMATION_DRAG_MIN_SPACING;
		m_formationDragPoints.push_back( pt );
		updateFormationHints();
		return;
	}

	// The last point always follows the cursor, so the end of the line sits exactly where the hand
	// is.  A new point is only kept once the cursor has moved far enough for the curve to be saying
	// something; without that a slow drag stores one point a frame and fills the message with noise.
	Bool keep = TRUE;
	const Int keptIndex = (Int)m_formationDragPoints.size() - 2;
	if( keptIndex >= 0 )
	{
		const Int dx = pt.x - m_formationDragPoints[ keptIndex ].x;
		const Int dy = pt.y - m_formationDragPoints[ keptIndex ].y;
		keep = ( dx * dx + dy * dy >= m_formationDragSpacing * m_formationDragSpacing );
	}

	if( keep )
	{
		// A long drag would otherwise run out of points, and everything after that would collapse
		// into one straight rubber band from the last kept point to the cursor - which is the kink
		// that showed up in play.  Halve the resolution instead: throw away every second point and
		// double the spacing.  The shape survives at a coarser step and the line keeps growing for
		// as long as the player keeps dragging.
		if( (Int)m_formationDragPoints.size() >= MAX_FORMATION_DRAG_POINTS )
		{
			std::vector<ICoord2D> thinned;
			for( Int i = 0; i < (Int)m_formationDragPoints.size(); i += 2 )
				thinned.push_back( m_formationDragPoints[ i ] );
			m_formationDragPoints = thinned;
			m_formationDragSpacing *= 2;
		}

		m_formationDragPoints.push_back( pt );
	}
	else
	{
		m_formationDragPoints.back() = pt;
	}

	updateFormationHints();
}

//-------------------------------------------------------------------------------------------------
/** Work out who is going where, from the curve as it stands.  This is the same arithmetic the logic
	* runs when the button comes up - same curve, same filter, same total order - so what the player
	* is shown is what the units will do. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateFormationHints( void )
{
	// last frame's markers, kept only so this frame's can inherit their age (see addOrderHint)
	std::vector<OrderHint> previous;
	previous.swap( m_orderHints );

	if( m_formationDragPoints.size() < 2 )
		return;

	std::vector<Coord3D> path;
	for( std::vector<ICoord2D>::const_iterator pt = m_formationDragPoints.begin();
			 pt != m_formationDragPoints.end(); ++pt )
	{
		// past the horizon there is no ground, the same points finishFormationDrag leaves out
		Coord3D world;
		if( TheTacticalView->screenToTerrain( &(*pt), &world ) )
			path.push_back( world );
	}

	if( path.size() < 2 )
		return;

	std::vector<Real> arc;
	buildPathArcLengths( path, arc );

	const Real span = arc.back();
	if( span < 1.0f )
		return;

	std::vector<Object *> movers;
	for( DrawableList::const_iterator it = m_selectedDrawables.begin();
			 it != m_selectedDrawables.end(); ++it )
	{
		Object *obj = (*it)->getObject();
		if( !obj || obj->getControllingPlayer() != ThePlayerList->getLocalPlayer() )
			continue;
		if( obj->isKindOf( KINDOF_IMMOBILE ) || obj->getAIUpdateInterface() == NULL )
			continue;
		// the same riders the logic leaves out of a formation move (GameLogicDispatch)
		if( obj->isDisabledByType( DISABLED_HELD ) && !isForceAttackArmed() && !isForceLineHeld() && !isGuardArmed() )
			continue;
		movers.push_back( obj );
	}

	if( movers.empty() )
		return;

	orderAlongPath( movers, path, arc );

	const Int count = movers.size();
	for( Int i = 0; i < count; i++ )
	{
		OrderHint hint;
		hint.kind = isInAttackMoveToMode() ? ORDER_HINT_ATTACK_MOVE
							: ( isForceAttackArmed() || isForceLineHeld() ) ? ORDER_HINT_ATTACK
							: isGuardArmed() ? ORDER_HINT_GUARD
							: ORDER_HINT_MOVE;
		hint.owner = movers[ i ]->getID();
		hint.from = *movers[ i ]->getPosition();
		pointAlongPath( path, arc, span * ((count == 1) ? 1.0f : ((Real)i / (Real)(count - 1))),
										&hint.to );
		hint.to.z = TheTerrainLogic->getGroundHeight( hint.to.x, hint.to.y );
		addOrderHint( hint, previous );
	}
}

//-------------------------------------------------------------------------------------------------
/** The order an aircraft has taken but not started.  A plane on the ground answers a move or an
	* attack by handing its state machine to the takeoff sequence and putting the order aside, so
	* neither the state id nor the goal names the thing the player pointed at until it is flying. */
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** Where an order aimed at a unit or building ends: just over the top of it.  The joint EA's rally
	* line puts on the ground sits inside the model there and the model hides it, and at the top of
	* the geometry a turret still covers it. */
//-------------------------------------------------------------------------------------------------
static Coord3D orderTargetSpot( const Object *target )
{
	const Real CLEARANCE = 8.0f;
	Coord3D spot = *target->getPosition();
	spot.z += target->getGeometryInfo().getMaxHeightAbovePosition() + CLEARANCE;
	return spot;
}

//-------------------------------------------------------------------------------------------------
/** A building's outline is its footprint grown by a margin, so it clears the walls, and it lies on
	* the ground rather than at the building's base height: each point takes the terrain under it, so
	* the line rides over a hill instead of cutting into it.  The lift keeps the ribbon, which faces
	* the camera, out of the ground it lies on. */
//-------------------------------------------------------------------------------------------------
static const Real OUTLINE_MARGIN = 6.0f;
static const Real OUTLINE_LIFT = 2.0f;
static const Real OUTLINE_STEP = 8.0f;		///< longest stretch between two points that take the ground's height

static Real outlineHeight( Real x, Real y )
{
	return TheTerrainLogic->getGroundHeight( x, y ) + OUTLINE_LIFT;
}

/** Where an order aimed at an outlined building ends: on the outline, at its point nearest 'from'. */
static Coord3D nearestOutlinePoint( const Object *obj, const Coord3D& from )
{
	const GeometryInfo& geom = obj->getGeometryInfo();
	const Coord3D *ctr = obj->getPosition();
	const Real dx = from.x - ctr->x;
	const Real dy = from.y - ctr->y;

	Coord3D spot;
	if( geom.getGeomType() == GEOMETRY_BOX )
	{
		// into the building's own frame, x along its major axis, clamped onto the rectangle
		const Real c = (Real)Cos( obj->getOrientation() );
		const Real s = (Real)Sin( obj->getOrientation() );
		const Real a = geom.getMajorRadius() + OUTLINE_MARGIN;
		const Real b = geom.getMinorRadius() + OUTLINE_MARGIN;
		Real lx = clamp( -a, dx * c + dy * s, a );
		Real ly = clamp( -b, dy * c - dx * s, b );

		// from inside it, out to the nearest side
		if( fabs( lx ) < a && fabs( ly ) < b )
		{
			if( a - fabs( lx ) < b - fabs( ly ) )
				lx = ( lx < 0.0f ) ? -a : a;
			else
				ly = ( ly < 0.0f ) ? -b : b;
		}
		spot.x = ctr->x + lx * c - ly * s;
		spot.y = ctr->y + lx * s + ly * c;
	}
	else
	{
		const Real r = geom.getMajorRadius() + OUTLINE_MARGIN;
		const Real len = (Real)sqrt( dx * dx + dy * dy );
		spot.x = ctr->x + ( ( len > 0.01f ) ? dx * r / len : r );
		spot.y = ctr->y + ( ( len > 0.01f ) ? dy * r / len : 0.0f );
	}
	spot.z = outlineHeight( spot.x, spot.y );
	return spot;
}

void InGameUI::traceStructureOutline( const Object *obj, std::vector<Coord3D>& points )
{
	points.clear();
	const GeometryInfo& geom = obj->getGeometryInfo();
	const Coord3D *ctr = obj->getPosition();

	if( geom.getGeomType() == GEOMETRY_BOX )
	{
		// the rally line's corner arithmetic (W3DWaypointBuffer), on the grown rectangle.  The loop
		// starts and ends halfway along the first side: a ribbon's two ends meeting on a corner leave
		// a notch there, and on a straight run they close up
		const Real c = (Real)Cos( obj->getOrientation() );
		const Real s = (Real)Sin( obj->getOrientation() );
		const Real a = geom.getMajorRadius() + OUTLINE_MARGIN;
		const Real b = geom.getMinorRadius() + OUTLINE_MARGIN;
		const Real signX[ 4 ] = { -1.0f, 1.0f, 1.0f, -1.0f };
		const Real signY[ 4 ] = { 1.0f, 1.0f, -1.0f, -1.0f };
		Coord2D corners[ 4 ];
		for( Int i = 0; i < 4; ++i )
		{
			corners[ i ].x = ctr->x + signX[ i ] * a * c - signY[ i ] * b * s;
			corners[ i ].y = ctr->y + signX[ i ] * a * s + signY[ i ] * b * c;
		}

		Coord2D path[ 6 ];
		path[ 0 ].x = ( corners[ 0 ].x + corners[ 1 ].x ) * 0.5f;
		path[ 0 ].y = ( corners[ 0 ].y + corners[ 1 ].y ) * 0.5f;
		path[ 1 ] = corners[ 1 ];
		path[ 2 ] = corners[ 2 ];
		path[ 3 ] = corners[ 3 ];
		path[ 4 ] = corners[ 0 ];
		path[ 5 ] = path[ 0 ];

		for( Int i = 0; i < 5; ++i )
		{
			const Coord2D& p = path[ i ];
			const Coord2D& q = path[ i + 1 ];
			const Real ex = q.x - p.x;
			const Real ey = q.y - p.y;
			const Int pieces = max( 1, (Int)ceil( sqrt( ex * ex + ey * ey ) / OUTLINE_STEP ) );
			for( Int k = 0; k < pieces; ++k )
			{
				Coord3D pt;
				pt.x = p.x + ex * k / pieces;
				pt.y = p.y + ey * k / pieces;
				pt.z = outlineHeight( pt.x, pt.y );
				points.push_back( pt );
			}
		}
	}
	else
	{
		const Real r = geom.getMajorRadius() + OUTLINE_MARGIN;
		const Int pieces = max( 16, (Int)ceil( 2.0f * PI * r / OUTLINE_STEP ) );
		for( Int k = 0; k < pieces; ++k )
		{
			const Real angle = 2.0f * PI * k / pieces;
			Coord3D pt;
			pt.x = ctr->x + r * (Real)Cos( angle );
			pt.y = ctr->y + r * (Real)Sin( angle );
			pt.z = outlineHeight( pt.x, pt.y );
			points.push_back( pt );
		}
	}

	// closed
	if( !points.empty() )
		points.push_back( points.front() );
}

static Bool isAttackOrderHint( InGameUI::OrderHintKind kind )
{
	return kind == InGameUI::ORDER_HINT_ATTACK || kind == InGameUI::ORDER_HINT_FORCE_ATTACK;
}

//-------------------------------------------------------------------------------------------------
/** A drawn hint that ends on an outlined building ends on the outline, nearest where it comes from,
	* and a later leg of a shift list that starts from one leaves the outline nearest where it goes.
	* Run after the bunching, so a knot's averaged end is put back on the line too. */
//-------------------------------------------------------------------------------------------------
void InGameUI::snapOrderHintsToOutlines( void )
{
	if( m_structureOutlines.empty() )
		return;

	auto outlined = [this]( ObjectID id ) -> const Object *
	{
		if( id == INVALID_ID )
			return NULL;
		// a red outline in the dark half of a blink has no line to end on, so the hint keeps its
		// spot over the roof for those frames: from and to stay together across the blink
		for( std::vector<StructureOutline>::const_iterator it = m_structureOutlines.begin(); it != m_structureOutlines.end(); ++it )
			if( it->id == id )
				return ( it->color == OUTLINE_HIDDEN ) ? NULL : TheGameLogic->findObjectByID( id );
		return NULL;
	};

	for( std::vector<OrderHint>::iterator hint = m_drawnOrderHints.begin(); hint != m_drawnOrderHints.end(); ++hint )
	{
		if( const Object *from = outlined( hint->fromTarget ) )
			hint->from = nearestOutlinePoint( from, hint->to );
		if( const Object *to = outlined( hint->target ) )
			hint->to = nearestOutlinePoint( to, hint->from );
	}
}

//-------------------------------------------------------------------------------------------------
/** Rebuilt every frame from the selection and its order hints.  An attack keeps the time it was first
	* seen across rebuilds, so the blink runs once per attack rather than once per frame. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateStructureOutlines( void )
{
	const UnsignedInt BLINK_HALF_MS = 150;	// lit, then dark, for this long each
	const UnsignedInt BLINKS = 3;
	const UnsignedInt now = Clock_Milliseconds();

	m_previousStructureOutlines.swap( m_structureOutlines );
	m_structureOutlines.clear();
	const std::vector<StructureOutline>& previous = m_previousStructureOutlines;

	auto outlineFor = [this]( ObjectID id ) -> StructureOutline&
	{
		for( std::vector<StructureOutline>::iterator it = m_structureOutlines.begin(); it != m_structureOutlines.end(); ++it )
			if( it->id == id )
				return *it;
		StructureOutline fresh;
		fresh.id = id;
		fresh.selected = FALSE;
		fresh.attacked = FALSE;
		fresh.attackedSinceMs = 0;
		fresh.color = OUTLINE_HIDDEN;
		m_structureOutlines.push_back( fresh );
		return m_structureOutlines.back();
	};

	for( std::vector<OrderHint>::const_iterator hint = m_orderHints.begin(); hint != m_orderHints.end(); ++hint )
	{
		if( !isAttackOrderHint( hint->kind ) || hint->target == INVALID_ID )
			continue;
		const Object *target = TheGameLogic->findObjectByID( hint->target );
		if( target == NULL || !target->isKindOf( KINDOF_STRUCTURE ) )
			continue;

		StructureOutline& outline = outlineFor( hint->target );
		if( outline.attacked )
			continue;
		outline.attacked = TRUE;
		outline.attackedSinceMs = now;
		for( std::vector<StructureOutline>::const_iterator old = previous.begin(); old != previous.end(); ++old )
			if( old->id == hint->target && old->attacked )
				outline.attackedSinceMs = old->attackedSinceMs;
	}

	for( DrawableList::const_iterator it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		// an enemy building selected and then fogged over would trace its live footprint in the fog
		if( obj && obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isEffectivelyDead() && !isHiddenByShroud( obj ) )
			outlineFor( obj->getID() ).selected = TRUE;
	}

	for( std::vector<StructureOutline>::iterator it = m_structureOutlines.begin(); it != m_structureOutlines.end(); ++it )
	{
		const UnsignedInt age = now - it->attackedSinceMs;
		const Bool dark = it->attacked && age < 2 * BLINKS * BLINK_HALF_MS && ( age / BLINK_HALF_MS ) % 2 == 1;
		it->color = ( it->attacked && !dark ) ? OUTLINE_RED : it->selected ? OUTLINE_WHITE : OUTLINE_HIDDEN;
	}
}

Bool InGameUI::getHeldAircraftOrder( const Object *obj, OrderHintKind& kind, Coord3D& to, Bool& onObject, ObjectID& target ) const
{
	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	const JetAIUpdate *jet = ai->getJetAIUpdate();
	if( jet == NULL )
		return FALSE;

	ObjectID targetID = INVALID_ID;
	Coord3D targetPos;
	targetPos.zero();

	switch( jet->friend_getHeldOrder( targetID, targetPos ) )
	{
		case AICMD_ATTACK_OBJECT:
			kind = ORDER_HINT_ATTACK;
			break;

		case AICMD_FORCE_ATTACK_OBJECT:
			kind = ORDER_HINT_FORCE_ATTACK;
			break;

		case AICMD_ATTACK_POSITION:
		case AICMD_ATTACK_AREA:
			kind = ORDER_HINT_ATTACK_GROUND;
			break;

		case AICMD_ATTACKMOVE_TO_POSITION:
			kind = ORDER_HINT_ATTACK_MOVE;
			break;

		case AICMD_ENTER:
		case AICMD_GET_REPAIRED:
			kind = ORDER_HINT_ENTER;
			break;

		// a queued path is held as a coordinate list the storage does not hand back, so a shift move
		// given to a parked plane stays invisible until it flies.  Single-point orders are the ones
		// worth drawing here
		case AICMD_MOVE_TO_POSITION:
		case AICMD_MOVE_TO_OBJECT:
			kind = ORDER_HINT_MOVE;
			break;

		default:
			// everything else the aircraft holds is its own housekeeping, not a player order
			return FALSE;
	}

	onObject = ( targetID != INVALID_ID );
	target = targetID;
	if( onObject )
	{
		// a target under the shroud draws nothing, as in getQueuedOrderHint
		const Object *targetObj = TheGameLogic->findObjectByID( targetID );
		if( targetObj == NULL || isHiddenByShroud( targetObj ) )
			return FALSE;
		to = orderTargetSpot( targetObj );
	}
	else
	{
		to = targetPos;
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The spot a guarding unit is holding.  A guard order clears the state machine before it starts,
	* so the machine's goal position is the origin and reading it drew every guard marker in the
	* bottom left corner of the map.  What the unit is guarding is kept on the AI itself. */
//-------------------------------------------------------------------------------------------------
static Bool getGuardedSpot( const AIUpdateInterface *ai, Coord3D& spot, Bool& onObject, ObjectID& target )
{
	onObject = ( ai->getGuardTargetType() == GUARDTARGET_OBJECT );
	target = onObject ? ai->getGuardObject() : INVALID_ID;
	switch( ai->getGuardTargetType() )
	{
		case GUARDTARGET_LOCATION:
			spot = *ai->getGuardLocation();
			return TRUE;

		case GUARDTARGET_OBJECT:
		{
			const Object *guarded = TheGameLogic->findObjectByID( ai->getGuardObject() );
			if( guarded == NULL )
				return FALSE;
			spot = orderTargetSpot( guarded );
			return TRUE;
		}

		case GUARDTARGET_AREA:
		{
			const PolygonTrigger *area = ai->getAreaToGuard();
			if( area == NULL )
				return FALSE;
			area->getCenterPoint( &spot );
			return TRUE;
		}

		default:
			return FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
/** Where each selected unit is going, asked of the unit rather than remembered from the order.  A
	* line therefore lives exactly as long as the order behind it: it appears the frame the unit
	* accepts the command and goes when the unit arrives, changes its mind or leaves the selection. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateOrderHints( void )
{
	expirePendingOrders();

	// a formation line being drawn shows every unit's own station, which is the whole point of it
	if( m_isFormationDragging )
	{
		updateFormationHints();
		markOrderHints();
		m_drawnOrderHints = m_orderHints;
	}
	else
	{
		collectOrderHints();
		applyCommandedPoints();
		numberOrderHints();
		markOrderHints();
		bunchOrderHints();
	}
	updateStructureOutlines();
	snapOrderHintsToOutlines();
	updateOrderRings();
}

//-------------------------------------------------------------------------------------------------
/** The frame an order sent now runs on: in a network game the execution frame, run-ahead later,
	* and in a game on this machine the next tick.  Logic has run it once its frame is past this. */
//-------------------------------------------------------------------------------------------------
static UnsignedInt orderDueFrame( void )
{
	UnsignedInt due = TheGameLogic->getFrame();
	if( TheNetwork != NULL && TheNetwork->getExecutionFrame() > (Int)due )
		due = (UnsignedInt)TheNetwork->getExecutionFrame();
	return due;
}

//-------------------------------------------------------------------------------------------------
/** The local player has just given the selection a move-type order (HintSpy).  Each unit it will
	* move gets a ring waiting for logic to run the order (orderDueFrame). */
//-------------------------------------------------------------------------------------------------
void InGameUI::armOrderRings( OrderHintKind kind )
{
	const UnsignedInt due = orderDueFrame();

	const Player *local = ThePlayerList->getLocalPlayer();
	for( DrawableList::const_iterator it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( !obj || obj->getControllingPlayer() != local || obj->getAI() == NULL )
			continue;
		if( obj->isKindOf( KINDOF_IMMOBILE ) || obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )
			continue;

		// the new order replaces whatever ring the last one left on this unit
		Bool replaced = FALSE;
		for( size_t i = 0; i < m_orderRings.size(); ++i )
		{
			if( m_orderRings[ i ].owner == obj->getID() )
			{
				m_orderRings.erase( m_orderRings.begin() + i );
				replaced = TRUE;
				break;
			}
		}

		OrderRing ring;
		ring.replaced = replaced;
		ring.owner = obj->getID();
		ring.kind = kind;
		ring.at.zero();
		// the body, not the bounding circle: a box's corner and a gun barrel sweep one half again as
		// wide as the hull, and a ring that size under every tank of a group ran into one band
		const GeometryInfo& geom = obj->getGeometryInfo();
		const Real body = ( geom.getGeomType() == GEOMETRY_BOX ) ? ( geom.getMajorRadius() + geom.getMinorRadius() ) * 0.5f
																														: geom.getMajorRadius();
		ring.radius = max( 3.0f, body );
		ring.onGround = TRUE;
		ring.settled = FALSE;
		ring.dueFrame = due;
		ring.armedMs = Clock_Milliseconds();
		ring.bornMs = 0;
		m_orderRings.push_back( ring );
	}
}

//-------------------------------------------------------------------------------------------------
/** Once a frame at the sync point, beside the order hints.  A ring is placed the first frame logic
	* is past its order and keeps following the unit's destination until that is final, so a group
	* move shows the goal logic's flood gave each member at once and moves to the path's real end the
	* frame the path comes back. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateOrderRings( void )
{
	const UnsignedInt now = Clock_Milliseconds();
	const UnsignedInt frame = TheGameLogic->getFrame();
	size_t kept = 0;
	for( size_t i = 0; i < m_orderRings.size(); ++i )
	{
		OrderRing ring = m_orderRings[ i ];
		Bool keep = ( ring.bornMs != 0 ) ? ( now - ring.bornMs < (UnsignedInt)ORDER_RING_MS ) : ( now - ring.armedMs < (UnsignedInt)ORDER_RING_WAIT_MS );
		if( keep && !ring.settled && frame > ring.dueFrame )
			keep = placeOrderRing( ring );
		if( keep )
			m_orderRings[ kept++ ] = ring;
	}
	m_orderRings.resize( kept );
}

//-------------------------------------------------------------------------------------------------
/** Where logic is sending the ring's unit.  A path planned since the order ran ends where the unit
	* will stand; before it comes back, a move's goal position is already the unit's own (the flood's
	* cell for a click, the station on a formation line), while an attack move's is the click shared by
	* the whole group, so that ring waits for the path.  FALSE drops the ring. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::placeOrderRing( OrderRing& ring )
{
	Object *obj = TheGameLogic->findObjectByID( ring.owner );
	AIUpdateInterface *ai = obj ? obj->getAIUpdateInterface() : NULL;
	if( ai == NULL || obj->isEffectivelyDead() )
		return FALSE;

	Coord3D to;
	Bool found = FALSE;
	const Int pathSize = ai->friend_getWaypointGoalPathSize();
	const Int pathIndex = ai->friend_getCurrentGoalPathIndex();
	Path *path = ai->getPath();
	const Bool freshPath = path != NULL && !ai->isWaitingForPath() &&
												 TheGameLogic->getFrame() - ai->getPathAge() >= ring.dueFrame;

	if( ring.kind == ORDER_HINT_WAYPOINT && pathSize > 0 && pathIndex >= 0 && pathIndex < pathSize )
	{
		to = *ai->friend_getGoalPathPosition( pathSize - 1 );		// the point just added ends the list
		ring.settled = found = TRUE;
	}
	else if( ai->hasTunnelTrip() )
	{
		to = *ai->getTunnelTripGoal();
		ring.settled = found = TRUE;
	}
	else if( freshPath )
	{
		to = *path->getLastNode()->getPosition();
		ring.settled = found = TRUE;
	}
	else if( ring.kind == ORDER_HINT_GUARD )
	{
		Bool onObject;
		ObjectID target;
		found = getGuardedSpot( ai, to, onObject, target ) && !onObject;
	}
	else if( ring.replaced && ( path != NULL || ai->isWaitingForPath() ) )
	{
		// a unit still on its last order's way: its goal can still be that order's spot, and the dot
		// stood there as if the new order had not taken.  It waits for the new path; a unit with no
		// path and none coming is already on its cell and falls through to its goal
	}
	else if( ring.kind != ORDER_HINT_ATTACK_MOVE )
	{
		const StateID state = ai->getCurrentStateID();
		if( state == AI_MOVE_TO || state == AI_FOLLOW_PATH || state == AI_MOVE_AND_TIGHTEN )
		{
			to = *ai->getGoalPosition();
			found = TRUE;
		}
	}

	if( !found )
	{
		// placed once and now unreadable (arrived, or stopped to fight): it stays where it was
		ring.settled = ( ring.bornMs != 0 );
		return TRUE;
	}

	const PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination( &to );
	ring.onGround = ( layer == LAYER_GROUND );
	ring.at = to;
	ring.at.z = ring.onGround ? TheTerrainLogic->getGroundHeight( to.x, to.y ) : TheTerrainLogic->getLayerHeight( to.x, to.y, layer );
	if( ring.bornMs == 0 )
		ring.bornMs = Clock_Milliseconds();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A unit's new order replaces the one it was still waiting on. */
//-------------------------------------------------------------------------------------------------
static void replacePendingOrder( std::vector<InGameUI::PendingOrder>& list, const InGameUI::PendingOrder& order )
{
	for( size_t i = 0; i < list.size(); ++i )
	{
		if( list[ i ].owner == order.owner )
		{
			list[ i ] = order;
			return;
		}
	}
	list.push_back( order );
}

//-------------------------------------------------------------------------------------------------
/** The selection was just given an order that is not queued (HintSpy).  Every unit that would take
	* it draws its line from the order until logic has run it: a move sends whatever can move, an
	* attack whatever can shoot. */
//-------------------------------------------------------------------------------------------------
void InGameUI::predictOrderHints( OrderHintKind kind, const Coord3D *to, ObjectID target )
{
	const Bool attack = ( kind == ORDER_HINT_ATTACK || kind == ORDER_HINT_FORCE_ATTACK || kind == ORDER_HINT_ATTACK_GROUND );

	PendingOrder order;
	order.kind = kind;
	order.target = target;
	order.dueFrame = orderDueFrame();
	if( to )
		order.to = *to;
	else
		order.to.zero();

	const Player *local = ThePlayerList->getLocalPlayer();
	for( DrawableList::const_iterator it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( !obj || obj->getControllingPlayer() != local || obj->getAI() == NULL || obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )
			continue;
		if( attack ? !obj->isAbleToAttack() : obj->isKindOf( KINDOF_IMMOBILE ) )
			continue;
		if( obj->getID() == target )
			continue;
		order.owner = obj->getID();
		replacePendingOrder( m_pendingOrders, order );
		rememberCommandedPoint( order.owner, kind, ( to && target == INVALID_ID ) ? to : NULL );
	}
}

//-------------------------------------------------------------------------------------------------
/** The click a unit's ground order went to, for applyCommandedPoints; NULL forgets it (an order at a
	* unit or building, or a formation line, whose stations are the units' own). */
//-------------------------------------------------------------------------------------------------
void InGameUI::rememberCommandedPoint( ObjectID owner, OrderHintKind kind, const Coord3D *at )
{
	// an order that is not queued empties the unit's list, and the clicks queued on it with it
	for( size_t q = 0; q < m_queuedClicks.size(); )
	{
		if( m_queuedClicks[ q ].owner == owner )
			m_queuedClicks.erase( m_queuedClicks.begin() + q );
		else
			++q;
	}
	for( size_t c = 0; c < m_commandedPoints.size(); ++c )
	{
		if( m_commandedPoints[ c ].owner != owner )
			continue;
		m_commandedPoints.erase( m_commandedPoints.begin() + c );
		break;
	}
	if( at == NULL )
		return;
	CommandedPoint point;
	point.owner = owner;
	point.kind = kind;
	point.at = *at;
	point.goal = *at;
	point.settled = FALSE;
	point.legs = 0;
	point.seen = TRUE;
	point.dueFrame = orderDueFrame();
	// a unit that died or left the selection before its line moved off keeps its entry; the oldest go
	if( m_commandedPoints.size() >= 512 )
		m_commandedPoints.erase( m_commandedPoints.begin() );
	m_commandedPoints.push_back( point );
}

//-------------------------------------------------------------------------------------------------
/** A ground order went on the end of the selection's lists (HintSpy, shift held): each unit that
	* would take it keeps the click, which its queued leg ends on (applyCommandedPoints).  Logic spreads
	* a group's queued point as it spreads a plain move, and the joints of a big group's list landed on
	* four or more spots round each click. */
//-------------------------------------------------------------------------------------------------
void InGameUI::rememberQueuedClicks( OrderHintKind kind, const Coord3D& at )
{
	const Bool attack = ( kind == ORDER_HINT_ATTACK_GROUND );
	const Player *local = ThePlayerList->getLocalPlayer();
	for( DrawableList::const_iterator it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( !obj || obj->getControllingPlayer() != local || obj->getAI() == NULL || obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )
			continue;
		if( attack ? !obj->isAbleToAttack() : obj->isKindOf( KINDOF_IMMOBILE ) )
			continue;
		QueuedClick click;
		click.owner = obj->getID();
		click.kind = kind;
		click.at = at;
		click.bornMs = Clock_Milliseconds();
		click.dueFrame = orderDueFrame();
		click.matched = FALSE;
		if( m_queuedClicks.size() >= 1024 )
			m_queuedClicks.erase( m_queuedClicks.begin() );
		m_queuedClicks.push_back( click );
	}
}

//-------------------------------------------------------------------------------------------------
/** A formation line was just sent (HintSpy).  The drag is over but its markers are still in the
	* list, one per mover at the station logic is about to give it, so the line the player drew stays
	* on the ground until the units take their stations over. */
//-------------------------------------------------------------------------------------------------
void InGameUI::predictFormationOrderHints( void )
{
	PendingOrder order;
	order.target = INVALID_ID;
	order.dueFrame = orderDueFrame();
	for( std::vector<OrderHint>::const_iterator it = m_orderHints.begin(); it != m_orderHints.end(); ++it )
	{
		order.owner = it->owner;
		order.kind = it->kind;
		order.to = it->to;
		replacePendingOrder( m_pendingOrders, order );
		rememberCommandedPoint( order.owner, order.kind, NULL );
	}
}

//-------------------------------------------------------------------------------------------------
/** A rally point was just sent (HintSpy).  The control bar is told to look again, so the flag moves
	* on this frame too. */
//-------------------------------------------------------------------------------------------------
void InGameUI::predictRallyPoint( ObjectID building, const Coord3D& at )
{
	PendingRally rally;
	rally.building = building;
	rally.at = at;
	rally.dueFrame = orderDueFrame();

	Bool replaced = FALSE;
	for( size_t i = 0; i < m_pendingRallies.size() && !replaced; ++i )
	{
		if( m_pendingRallies[ i ].building == building )
		{
			m_pendingRallies[ i ] = rally;
			replaced = TRUE;
		}
	}
	if( !replaced )
		m_pendingRallies.push_back( rally );

	TheControlBar->markUIDirty();
}

//-------------------------------------------------------------------------------------------------
const Coord3D *InGameUI::getPredictedRallyPoint( ObjectID building ) const
{
	for( size_t i = 0; i < m_pendingRallies.size(); ++i )
		if( m_pendingRallies[ i ].building == building )
			return &m_pendingRallies[ i ].at;
	return NULL;
}

//-------------------------------------------------------------------------------------------------
/** At the sync point, before the lines are read: whatever logic has run is its own again.  A rally
	* point going back to the building's own makes the control bar look again for the flag. */
//-------------------------------------------------------------------------------------------------
void InGameUI::expirePendingOrders( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();

	size_t kept = 0;
	for( size_t i = 0; i < m_pendingOrders.size(); ++i )
		if( frame <= m_pendingOrders[ i ].dueFrame )
			m_pendingOrders[ kept++ ] = m_pendingOrders[ i ];
	m_pendingOrders.resize( kept );

	kept = 0;
	for( size_t i = 0; i < m_pendingRallies.size(); ++i )
		if( frame <= m_pendingRallies[ i ].dueFrame )
			m_pendingRallies[ kept++ ] = m_pendingRallies[ i ];
	if( kept != m_pendingRallies.size() )
	{
		m_pendingRallies.resize( kept );
		TheControlBar->markUIDirty();
	}
}

//-------------------------------------------------------------------------------------------------
/** The line of an order this machine sent and logic has not run yet, drawn from the unit to where
	* the order points.  A target that is gone or under the shroud draws nothing of its own, and the
	* unit is read as usual. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::addPendingOrderHint( const Object *obj, const std::vector<OrderHint>& previous )
{
	for( std::vector<PendingOrder>::const_iterator it = m_pendingOrders.begin(); it != m_pendingOrders.end(); ++it )
	{
		if( it->owner != obj->getID() )
			continue;

		OrderHint hint;
		hint.owner = it->owner;
		hint.kind = it->kind;
		hint.from = *obj->getPosition();
		hint.to = it->to;
		if( it->target != INVALID_ID )
		{
			const Object *target = TheGameLogic->findObjectByID( it->target );
			if( target == NULL || target->isEffectivelyDead() || isHiddenByShroud( target ) )
				return FALSE;
			hint.to = orderTargetSpot( target );
			hint.onObject = TRUE;
			hint.target = it->target;		// its ring and outline, as once logic has run the order
		}
		addOrderHint( hint, previous );
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** EA's rally point says "go here" with a flag and marks every bend of its line with a joint.  An
	* order reads the same way: the flag goes on the last bit of ground a unit is sent to, and a unit
	* or building it is sent at gets the joint instead, so a flag never stands on a tank. */
//-------------------------------------------------------------------------------------------------
InGameUI::OrderHintMark InGameUI::markForOrderHint( OrderHintKind kind, Bool onObject, Bool lastPoint )
{
	if( kind == ORDER_HINT_ABILITY || kind == ORDER_HINT_UPGRADE )
		return ORDER_MARK_NONE;
	return ( lastPoint && !onObject ) ? ORDER_MARK_FLAG : ORDER_MARK_JOINT;
}

//-------------------------------------------------------------------------------------------------
/** Five colours, one per thing the player has to tell apart at a glance: going, killing, killing on
	* the way, holding, and doing something to a thing.  Picked to stay apart from each other and from
	* the seat colours' pure hues once a dark rim is round them, on grass, sand and snow alike. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isOrderMarkOnWater( Real x, Real y, Real *waterZ )
{
	Real z = 0.0f;
	if( !TheTerrainLogic->isUnderwater( x, y, &z ) || z <= TheTerrainLogic->getGroundHeight( x, y ) )
		return FALSE;
	if( waterZ )
		*waterZ = z;
	return TRUE;
}

UnsignedInt InGameUI::orderKindColor( OrderHintKind kind )
{
	// only the local player's own orders are drawn, so his seat's colour is the one to step aside from
	const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	return orderKindColor( kind, local ? ( (UnsignedInt)clientPlayerColor( local ) & 0x00FFFFFF ) : 0 );
}

/** Hue in degrees and saturation (max - min over max) of an 0x00RRGGBB colour; brightening a colour,
	* as the selection ring does, changes neither. */
static Real colorHue( UnsignedInt rgb, Real *saturation )
{
	const Real r = (Real)( ( rgb >> 16 ) & 0xFF ), g = (Real)( ( rgb >> 8 ) & 0xFF ), b = (Real)( rgb & 0xFF );
	const Real hi = MAX( r, MAX( g, b ) ), lo = MIN( r, MIN( g, b ) );
	*saturation = ( hi > 0.0f ) ? ( hi - lo ) / hi : 0.0f;
	if( hi - lo <= 0.0f )
		return 0.0f;
	Real hue = ( hi == r ) ? 60.0f * ( g - b ) / ( hi - lo ) : ( hi == g ) ? 60.0f * ( b - r ) / ( hi - lo ) + 120.0f : 60.0f * ( r - g ) / ( hi - lo ) + 240.0f;
	return ( hue < 0.0f ) ? hue + 360.0f : hue;
}

static Real hueApart( Real a, Real b )
{
	const Real apart = (Real)fabs( a - b );
	return MIN( apart, 360.0f - apart );
}

// Move, attack, attack move, guard, everything else: each kind's colour and the one it steps to when
// it sits on the seat's own hue (orderKindPalette).  Hues in degrees:
//   green 131 -> chartreuse 93, a lime that still reads green: 27 off a green seat, 42 off the gold
//     and 87 off the cyan
//   red 3 -> pink-red 333: 27 off a red seat, still red enough to read as an attack
//   orange 29 -> magenta 313
//   gold 51 -> pale yellow-white, apart by being nearly white
//   cyan 180 -> periwinkle 254: 74 off a cyan seat and 36 off the Reforged blue (218), the colour
//     the player holding the mouse always has, which a sky blue would have matched.  The cyan sits
//     at 180 for the same blue: at 196 it was 22 off it
static const UnsignedInt TheOrderColors[ 5 ]			= { 0x0040E85C, 0x00FF3A2E, 0x00FF8A1C, 0x00FFDC46, 0x003CE6E6 };
static const UnsignedInt TheOrderAlternates[ 5 ]	= { 0x008CF03C, 0x00FF2E8C, 0x00FF4AD8, 0x00FFF5C8, 0x009A7CFF };

/** The five colours a seat's orders wear.  A kind steps aside when its hue is within 25 degrees of
	* the seat's and it is the kind nearest the seat, or within 15: an orange seat (0xFF8219, hue 27)
	* moves the attack move (2 off) and leaves the attack (24 off) red, where stepping both aside put a
	* pink-red beside the magenta.  An alternate is taken only where it lands 25 or more off the seat and
	* 40 or more off every other colour in use; otherwise the kind keeps its own.  Grey and white take
	* part in none of it. */
static void orderKindPalette( UnsignedInt seat, UnsignedInt out[ 5 ] )
{
	const Real SATURATED = 0.35f;
	const Real NEAR_HUE = 25.0f;
	const Real CLOSE_HUE = 15.0f;
	const Real KEEP_APART = 40.0f;
	for( Int i = 0; i < 5; ++i )
		out[ i ] = TheOrderColors[ i ];
	Real seatSaturation;
	const Real seatHue = colorHue( seat, &seatSaturation );
	if( seatSaturation < SATURATED )
		return;

	Real apart[ 5 ];
	Int order[ 5 ];
	for( Int i = 0; i < 5; ++i )
	{
		Real saturation;
		const Real hue = colorHue( TheOrderColors[ i ], &saturation );
		apart[ i ] = ( saturation >= SATURATED ) ? hueApart( hue, seatHue ) : 360.0f;
		order[ i ] = i;
	}
	std::sort( order, order + 5, [&]( Int a, Int b ) { return apart[ a ] < apart[ b ]; } );

	for( Int n = 0; n < 5; ++n )
	{
		const Int k = order[ n ];
		if( apart[ k ] >= NEAR_HUE )
			break;
		if( n > 0 && apart[ k ] >= CLOSE_HUE )
			continue;
		Real altSaturation;
		const Real altHue = colorHue( TheOrderAlternates[ k ], &altSaturation );
		Bool fits = altSaturation < SATURATED || hueApart( altHue, seatHue ) >= NEAR_HUE;
		for( Int j = 0; j < 5 && fits && altSaturation >= SATURATED; ++j )
		{
			Real saturation;
			const Real hue = colorHue( out[ j ], &saturation );
			if( j != k && saturation >= SATURATED && hueApart( altHue, hue ) < KEEP_APART )
				fits = FALSE;
		}
		if( fits )
			out[ k ] = TheOrderAlternates[ k ];
	}
}

UnsignedInt InGameUI::orderKindColor( OrderHintKind kind, UnsignedInt seat )
{
	// worked out once a seat colour, the local player's being the only one asked for in a match
	static UnsignedInt cachedSeat = 0xFFFFFFFF;
	static UnsignedInt cached[ 5 ];
	if( seat != cachedSeat )
	{
		orderKindPalette( seat, cached );
		cachedSeat = seat;
		Real saturation;
		const Real hue = colorHue( seat, &saturation );
		DEBUG_LOG(( "ORDERCOLOR seat %06X hue %.0f saturation %.2f: move %06X attack %06X attack move %06X guard %06X other %06X\n",
			seat, hue, saturation, cached[ 0 ], cached[ 1 ], cached[ 2 ], cached[ 3 ], cached[ 4 ] ));
	}
	switch( kind )
	{
		case ORDER_HINT_MOVE:
		case ORDER_HINT_WAYPOINT:				return cached[ 0 ];
		case ORDER_HINT_ATTACK:
		case ORDER_HINT_FORCE_ATTACK:
		case ORDER_HINT_ATTACK_GROUND:	return cached[ 1 ];
		case ORDER_HINT_ATTACK_MOVE:		return cached[ 2 ];
		case ORDER_HINT_GUARD:					return cached[ 3 ];
		default:												return cached[ 4 ];
	}
}

//-------------------------------------------------------------------------------------------------
/** A unit's hints sit together, in the order it gets to them.  The flag goes on the last of them
	* that is ground, even when an attack on something is queued after it. */
//-------------------------------------------------------------------------------------------------
void InGameUI::markOrderHints( void )
{
	size_t first = 0;
	while( first < m_orderHints.size() )
	{
		size_t end = first + 1;
		while( end < m_orderHints.size() && m_orderHints[ end ].owner == m_orderHints[ first ].owner )
			++end;

		size_t last = end;
		for( size_t i = first; i < end; ++i )
			if( markForOrderHint( m_orderHints[ i ].kind, m_orderHints[ i ].onObject, TRUE ) == ORDER_MARK_FLAG )
				last = i;

		for( size_t i = first; i < end; ++i )
			m_orderHints[ i ].mark = markForOrderHint( m_orderHints[ i ].kind, m_orderHints[ i ].onObject, i == last );

		first = end;
	}
}

//-------------------------------------------------------------------------------------------------
/** One hint per selected unit, and one more per queued point it still owes. */
//-------------------------------------------------------------------------------------------------
void InGameUI::collectOrderHints( void )
{
	// last frame's markers, kept only so this frame's can inherit their age (see addOrderHint)
	std::vector<OrderHint> previous;
	previous.swap( m_orderHints );

	Player *local = ThePlayerList->getLocalPlayer();
	for( DrawableList::const_iterator it = m_selectedDrawables.begin();
			 it != m_selectedDrawables.end(); ++it )
	{
		Object *obj = (*it)->getObject();
		if( !obj || obj->getControllingPlayer() != local )
			continue;

		// an angry mob's members each chase a spot round the nexus, which holds the real order, so
		// only the nexus draws: one line for the mob rather than one per rioter
		if( obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )
			continue;

		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if( ai == NULL )
			continue;

		// an order sent from here that logic has not run yet is drawn from the order, on the click
		if( addPendingOrderHint( obj, previous ) )
			continue;

		OrderHint hint;
		hint.owner = obj->getID();
		Coord3D resolvedGoal;
		Bool goalResolved = FALSE;
		Bool resolvedOnObject = FALSE;		// the resolved goal is a unit or building, not ground
		ObjectID resolvedTarget = INVALID_ID;
		Bool legsDrawn = FALSE;		// the order drew its own threads, and the shift list follows on from them

		// a capture walks its man to the door with a plain move of its own and then stands him idle
		// while the building turns, so the state machine reads as a move and then as nothing at all.
		// The ability knows better, and the thread is the capture's for as long as it runs
		ObjectID capturedID = INVALID_ID;
		const Object *captured = obj->getCaptureTarget( &capturedID ) ? TheGameLogic->findObjectByID( capturedID ) : NULL;
		if( captured && isHiddenByShroud( captured ) )
			captured = NULL;

		switch( ai->getCurrentStateID() )
		{
			case AI_MOVE_TO:
			case AI_FOLLOW_PATH:
			case AI_FOLLOW_EXITPRODUCTION_PATH:
			case AI_MOVE_AND_EVACUATE:
			case AI_MOVE_AND_EVACUATE_AND_EXIT:
			case AI_MOVE_AND_DELETE:
			case AI_MOVE_AND_TIGHTEN:
			case AI_PICK_UP_CRATE:
				hint.kind = ORDER_HINT_MOVE;
				break;

			case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM:
			case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS:
			case AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT:
			case AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT:
				hint.kind = ORDER_HINT_WAYPOINT;
				break;

			// An attack move and a shot at the ground both end on the point that was clicked, which the
			// order keeps as its goal position.  The end of the unit's path is somewhere else: a free
			// cell next to it for a mover, the centre of a cell in range for a gun.  The attack move's
			// path is also thrown away every time the unit stops to fight, so reading the path left the
			// thread pointing at whatever the fight was about
			case AI_ATTACK_MOVE_TO:
				hint.kind = ORDER_HINT_ATTACK_MOVE;
				resolvedGoal = *ai->getGoalPosition();
				goalResolved = TRUE;
				break;

			case AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS:
			case AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM:
			case AI_HUNT:
				hint.kind = ORDER_HINT_ATTACK_MOVE;
				break;

			case AI_ATTACK_OBJECT:
			case AI_ATTACK_AND_FOLLOW_OBJECT:
			case AI_ATTACK_SQUAD:
				hint.kind = ORDER_HINT_ATTACK;
				break;

			case AI_FORCE_ATTACK_OBJECT:
				hint.kind = ORDER_HINT_FORCE_ATTACK;
				break;

			case AI_ATTACK_POSITION:
				hint.kind = ORDER_HINT_ATTACK_GROUND;
				resolvedGoal = *ai->getGoalPosition();
				goalResolved = TRUE;
				break;

			case AI_ATTACK_AREA:
				hint.kind = ORDER_HINT_ATTACK_GROUND;
				break;

			case AI_ENTER:
			{
				// An order that is going through the tunnels is still the order it was: one thread in its
				// colour to the mouth it goes down and one from the mouth it comes up at to where it was
				// sent.  The far mouth is worked out the way the unit will work it out when it gets there.
				const Object *entrance = ai->getGoalObject();
				if( ai->hasTunnelTrip() && entrance != NULL )
				{
					hint.kind = ( ai->getTunnelTripEnd() == TUNNEL_TRIP_ATTACK_MOVE ) ? ORDER_HINT_ATTACK_MOVE : ORDER_HINT_MOVE;
					hint.from = *obj->getPosition();
					hint.to = orderTargetSpot( entrance );
					hint.onObject = TRUE;
					hint.target = entrance->getID();
					addOrderHint( hint, previous );

					const Object *exit = local->getTunnelSystem()->findTunnelNear( ai->getTunnelTripGoal(), obj );
					hint.from = ( exit != NULL ) ? *exit->getPosition() : hint.to;
					hint.to = *ai->getTunnelTripGoal();
					hint.onObject = FALSE;
					hint.target = INVALID_ID;
					addOrderHint( hint, previous );
					legsDrawn = TRUE;
					break;
				}
				hint.kind = ORDER_HINT_ENTER;
				break;
			}

			case AI_RAPPEL_INTO:
			case AI_COMBATDROP:
				hint.kind = ORDER_HINT_ENTER;
				break;

			case AI_DOCK:
				hint.kind = ORDER_HINT_DOCK;
				break;

			case AI_GET_REPAIRED:
				hint.kind = ORDER_HINT_GET_REPAIRED;
				break;

			case AI_HACK_INTERNET:
				hint.kind = ORDER_HINT_HACK;
				break;

			case AI_GUARD:
			case AI_GUARD_RETALIATE:
			case AI_GUARD_TUNNEL_NETWORK:
			{
				// A guard order clears the state machine on its way in, so the goal position it leaves
				// behind is the origin - and the marker landed in the bottom left corner of the map
				// every time.  The spot being guarded lives on the AI itself, so ask it there.
				hint.kind = ORDER_HINT_GUARD;
				if( !getGuardedSpot( ai, resolvedGoal, resolvedOnObject, resolvedTarget ) )
					continue;
				goalResolved = TRUE;
				break;
			}

			default:
				// a parked aircraft is running its own takeoff state machine, not the order the player
				// gave it, and that order is held out of reach of the goal until the wheels are up.
				// Ask for it, or an air strike shows nothing at all during the seconds the plane spends
				// taxiing, which is exactly when the player wants to see where it is going
				if( captured == NULL && !getHeldAircraftOrder( obj, hint.kind, resolvedGoal, resolvedOnObject, resolvedTarget ) )
					continue;
				goalResolved = TRUE;
				break;
		}

		if( captured )
		{
			hint.kind = ORDER_HINT_CAPTURE;
			resolvedGoal = orderTargetSpot( captured );
			goalResolved = TRUE;
			resolvedOnObject = TRUE;
			resolvedTarget = captured->getID();
		}

		if( !legsDrawn )
		{
			hint.from = *obj->getPosition();

			// a queued path is shown point by point: one thread from the unit to its next point and one
			// from each point to the one after it, so the whole shift queue is on the ground at once and
			// stays there after the key is let go
			const Int pathSize = ai->friend_getWaypointGoalPathSize();
			const Int pathIndex = ai->friend_getCurrentGoalPathIndex();
			hint.onObject = FALSE;
			hint.target = INVALID_ID;
			Bool goalHidden = FALSE;
			if( goalResolved )
			{
				hint.to = resolvedGoal;
				hint.onObject = resolvedOnObject;
				hint.target = resolvedOnObject ? resolvedTarget : INVALID_ID;
			}
			else if( pathSize > 0 && pathIndex >= 0 && pathIndex < pathSize )
			{
				for( Int i = pathIndex; i < pathSize - 1; i++ )
				{
					hint.to = *ai->friend_getGoalPathPosition( i );
					addOrderHint( hint, previous );
					hint.from = hint.to;
				}
				hint.to = *ai->friend_getGoalPathPosition( pathSize - 1 );
			}
			else
			{
				// a goal object outranks the goal position: a unit chasing something is headed wherever that
				// thing is standing now, not where it stood when the order was given.  Without one the end of
				// the unit's own path is where it is really going: a group sent to one spot is spread over the
				// free cells round it, and the order's point is the same for every member.  A unit still
				// waiting for its path has only the order's point to show
				Object *goalObj = ai->getGoalObject();
				Path *path = ai->getPath();
				hint.onObject = ( goalObj != NULL );
				if( goalObj && isHiddenByShroud( goalObj ) )
					goalHidden = TRUE;
				else if( goalObj )
				{
					hint.to = orderTargetSpot( goalObj );
					hint.target = goalObj->getID();
				}
				else if( path )
					hint.to = *path->getLastNode()->getPosition();
				else
					hint.to = *ai->getGoalPosition();
			}

			// a goal under the shroud - a radar contact, or a target that has driven into the fog - draws
			// no thread: its end, or the end of the path chasing it, would trace the unit through the fog.
			// The shift list that follows starts from the unit itself
			if( goalHidden )
				hint.to = hint.from;
			else
				addOrderHint( hint, previous );
		}

		// the rest of the unit's shift queue: every order still owed after this one, each drawn on from
		// where the last leaves off.  The list belongs to the whole chain rather than to one unit, so
		// each member's tail starts wherever that unit's own current order ends
		const OrderChain *chain = local->getOrderQueue()->findChain( obj->getID() );
		if( chain )
		{
			hint.from = hint.to;
			addQueuedOrderTail( hint, *chain, previous );
		}
	}

	//
	// A unit holding a list that drew nothing above has to draw it anyway.  One waiting for the rest
	// of its chain to catch up is idle, an aircraft on its way home for ammo has no goal the switch
	// recognises, and one parked on its airfield is not even in the selection - so the list went
	// blank for exactly the time the player most wanted to see it.  A chain with anything selected in
	// it draws for every member.
	//
	const OrderChainList& chains = local->getOrderQueue()->getChains();
	for( OrderChainList::const_iterator chain = chains.begin(); chain != chains.end(); ++chain )
	{
		Bool anySelected = FALSE;
		for( std::vector<ObjectID>::const_iterator id = chain->m_members.begin(); id != chain->m_members.end(); ++id )
		{
			const Object *member = TheGameLogic->findObjectByID( *id );
			const Drawable *draw = member ? member->getDrawable() : NULL;
			if( draw && draw->isSelected() )
			{
				anySelected = TRUE;
				break;
			}
		}

		if( !anySelected )
			continue;

		for( std::vector<ObjectID>::const_iterator id = chain->m_members.begin(); id != chain->m_members.end(); ++id )
		{
			Bool alreadyDrawn = FALSE;
			for( std::vector<OrderHint>::const_iterator drawn = m_orderHints.begin();
					 drawn != m_orderHints.end(); ++drawn )
			{
				if( drawn->owner == *id )
				{
					alreadyDrawn = TRUE;
					break;
				}
			}

			if( alreadyDrawn )
				continue;

			const Object *member = TheGameLogic->findObjectByID( *id );
			if( member == NULL || member->isEffectivelyDead() )
				continue;

			OrderHint hint;
			hint.owner = *id;
			hint.from = *member->getPosition();
			hint.to = hint.from;

			// the order the chain is on now is no longer in its list, so it is drawn from here
			if( getQueuedOrderHint( chain->m_active, hint ) )
			{
				addOrderHint( hint, previous );
				hint.from = hint.to;
			}

			addQueuedOrderTail( hint, *chain, previous );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Units on the same kind of order that stand together and are headed to the same place draw one
	* thread between them, from their average position to their average destination.  Twenty tanks
	* sent across the map were twenty threads laid almost on top of each other; a selection spread
	* over the map still draws one thread per knot of units, so a flank sent separately stays
	* visible. */
//-------------------------------------------------------------------------------------------------
void InGameUI::bunchOrderHints( void )
{
	// about ten pathfinding cells: a knot of four or five tanks, or a flood of destinations round
	// one click
	const Real BUNCH_RADIUS = 100.0f;
	const Real bunchRadiusSqr = BUNCH_RADIUS * BUNCH_RADIUS;

	m_drawnOrderHints.clear();
	std::vector<Int> memberCounts;

	for( std::vector<OrderHint>::const_iterator hint = m_orderHints.begin();
			 hint != m_orderHints.end(); ++hint )
	{
		Bool joined = FALSE;
		for( size_t i = 0; i < m_drawnOrderHints.size(); ++i )
		{
			OrderHint& bunch = m_drawnOrderHints[ i ];
			if( bunch.kind != hint->kind || bunch.step != hint->step || bunch.icon != hint->icon || bunch.mark != hint->mark )
				continue;
			const Real fromX = bunch.from.x - hint->from.x;
			const Real fromY = bunch.from.y - hint->from.y;
			if( fromX * fromX + fromY * fromY > bunchRadiusSqr )
				continue;
			const Real toX = bunch.to.x - hint->to.x;
			const Real toY = bunch.to.y - hint->to.y;
			if( toX * toX + toY * toY > bunchRadiusSqr )
				continue;

			// the bunch's end is a click only while every member's is that same click
			bunch.clicked = bunch.clicked && hint->clicked && toX * toX + toY * toY < 1.0f;

			// running averages, so the bunch is compared against its middle rather than its first unit
			const Real share = 1.0f / (Real)( ++memberCounts[ i ] );
			bunch.from.x += ( hint->from.x - bunch.from.x ) * share;
			bunch.from.y += ( hint->from.y - bunch.from.y ) * share;
			bunch.from.z += ( hint->from.z - bunch.from.z ) * share;
			bunch.to.x += ( hint->to.x - bunch.to.x ) * share;
			bunch.to.y += ( hint->to.y - bunch.to.y ) * share;
			bunch.to.z += ( hint->to.z - bunch.to.z ) * share;

			// the oldest member's age, so a unit joining a standing order does not slide the marker in again
			bunch.bornMs = min( bunch.bornMs, hint->bornMs );
			joined = TRUE;
			break;
		}

		if( !joined )
		{
			m_drawnOrderHints.push_back( *hint );
			memberCounts.push_back( 1 );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Every order still owed, one thread from the last point to the next, starting wherever the hint
	* handed in leaves off. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addQueuedOrderTail( OrderHint& hint, const OrderChain& chain, const std::vector<OrderHint>& previous )
{
	// what each leg starts from, so snapOrderHintsToOutlines can put that end on an outline too.  An
	// upgrade or ability is used where the step before ends and passes that step's target on
	ObjectID fromTarget = hint.target;
	for( std::vector<QueuedOrder>::const_iterator order = chain.m_pending.begin();
			 order != chain.m_pending.end(); ++order )
	{
		if( !getQueuedOrderHint( *order, hint ) )
			continue;

		hint.fromTarget = fromTarget;
		addOrderHint( hint, previous );
		hint.from = hint.to;
		if( hint.kind != ORDER_HINT_ABILITY && hint.kind != ORDER_HINT_UPGRADE )
			fromTarget = hint.target;
	}
	hint.fromTarget = INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
/** Which marker a queued order draws, and where.  A queued victim is drawn where it stands now rather
	* than where it stood when the player picked it, so the thread follows a target that is driving
	* away.  One that died while it waited its turn is drawn nowhere: the order will be skipped.  Nor
	* is one that has driven into the shroud since it was picked - the thread would otherwise trace it
	* through the fog, which is a look at the map you have not earned.
	*
	* An upgrade, or an ability that needs no target, is used wherever the step before it ends, which
	* is the hint.to handed in; it is left there, and the marker sits on that spot. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::getQueuedOrderHint( const QueuedOrder& order, OrderHint& hint ) const
{
	hint.icon = NULL;
	hint.onObject = FALSE;
	hint.target = INVALID_ID;

	switch( order.getType() )
	{
		case GameMessage::MSG_DO_MOVETO:
		case GameMessage::MSG_DO_FORCEMOVETO:
		case GameMessage::MSG_DO_FORMATION_MOVETO:
		case GameMessage::MSG_DO_SALVAGE:
			hint.kind = ORDER_HINT_MOVE;
			break;

		case GameMessage::MSG_DO_ATTACKMOVETO:
		case GameMessage::MSG_DO_FORMATION_ATTACKMOVETO:
			hint.kind = ORDER_HINT_ATTACK_MOVE;
			break;

		case GameMessage::MSG_DO_ATTACK_OBJECT:
		case GameMessage::MSG_DO_WEAPON_AT_OBJECT:
			hint.kind = ORDER_HINT_ATTACK;
			break;

		case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
			hint.kind = ORDER_HINT_FORCE_ATTACK;
			break;

		case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
		case GameMessage::MSG_DO_FORMATION_FORCEATTACK:
		case GameMessage::MSG_DO_WEAPON_AT_LOCATION:
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:
			hint.kind = ORDER_HINT_ATTACK_GROUND;
			break;

		case GameMessage::MSG_DO_GUARD_POSITION:
		case GameMessage::MSG_DO_GUARD_OBJECT:
		case GameMessage::MSG_DO_FORMATION_GUARD:
			hint.kind = ORDER_HINT_GUARD;
			break;

		case GameMessage::MSG_ENTER:
		case GameMessage::MSG_COMBATDROP_AT_OBJECT:
		case GameMessage::MSG_COMBATDROP_AT_LOCATION:
			hint.kind = ORDER_HINT_ENTER;
			break;

		case GameMessage::MSG_DOCK:
			hint.kind = ORDER_HINT_DOCK;
			break;

		case GameMessage::MSG_GET_REPAIRED:
			hint.kind = ORDER_HINT_GET_REPAIRED;
			break;

		case GameMessage::MSG_GET_HEALED:
			hint.kind = ORDER_HINT_GET_HEALED;
			break;

		case GameMessage::MSG_DO_REPAIR:
		case GameMessage::MSG_RESUME_CONSTRUCTION:		// a building placed with shift, or a right click on one standing unfinished
			hint.kind = ORDER_HINT_DO_REPAIR;
			break;

		case GameMessage::MSG_EVACUATE:
			// pointed at a spot, the transport drives there to unload; otherwise it unloads where the
			// step before leaves it, like an ability
			hint.kind = ORDER_HINT_MOVE;
			if( order.getDestination( &hint.to ) )
				return TRUE;
			hint.kind = ORDER_HINT_ABILITY;
			return TRUE;

		case GameMessage::MSG_EXIT:
			// the object it names is the passenger, inside: one climbs out where the step before ends
			hint.kind = ORDER_HINT_ABILITY;
			return TRUE;

		case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:
		{
			// a capture and a hack have cursors of their own; the rest of what a unit does to one object
			// with an ability - a charge, a sniper round, a satchel - is an attack as far as the marker goes
			const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplateByID( order.getArgument( 0 ).integer );
			switch( power ? power->getSpecialPowerType() : SPECIAL_INVALID )
			{
				case SPECIAL_INFANTRY_CAPTURE_BUILDING:
				case SPECIAL_BLACKLOTUS_CAPTURE_BUILDING:
					hint.kind = ORDER_HINT_CAPTURE;
					break;

				case SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK:
				case SPECIAL_BLACKLOTUS_STEAL_CASH_HACK:
				case SPECIAL_HACKER_DISABLE_BUILDING:
					hint.kind = ORDER_HINT_HACK;
					break;

				default:
					hint.kind = ORDER_HINT_ATTACK;
					break;
			}
			break;
		}

		case GameMessage::MSG_DO_SPECIAL_POWER:
			hint.kind = ORDER_HINT_ABILITY;
			return TRUE;

		case GameMessage::MSG_QUEUE_UPGRADE:
		{
			const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgradeByKey( (NameKeyType)order.getArgument( 1 ).integer );
			hint.kind = ORDER_HINT_UPGRADE;
			hint.icon = upgrade ? upgrade->getButtonImage() : NULL;
			return TRUE;
		}

		default:
			// a hold is wherever each unit happens to be standing, and a list that began behind an
			// unqueued order has nothing of its own to show for that order
			return FALSE;
	}

	const ObjectID targetID = order.getTargetID();
	if( targetID == INVALID_ID )
		return order.getDestination( &hint.to );

	const Object *target = TheGameLogic->findObjectByID( targetID );
	if( target == NULL || target->isEffectivelyDead() || isHiddenByShroud( target ) )
		return FALSE;

	hint.to = orderTargetSpot( target );
	hint.onObject = TRUE;
	hint.target = targetID;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The hints of one unit sit together in the list, in the order it will get to them.  A unit with
	* more than one place to go carries a number on each, so a shift list reads first to last on the
	* ground; a unit with one does not, since a lone "1" says nothing. */
//-------------------------------------------------------------------------------------------------
void InGameUI::numberOrderHints( void )
{
	size_t first = 0;
	while( first < m_orderHints.size() )
	{
		size_t end = first + 1;
		while( end < m_orderHints.size() && m_orderHints[ end ].owner == m_orderHints[ first ].owner )
			++end;

		if( end - first > 1 )
		{
			for( size_t i = first; i < end; ++i )
				m_orderHints[ i ].step = (Int)( i - first ) + 1;
		}
		first = end;
	}
}

//-------------------------------------------------------------------------------------------------
/** Add a marker to this frame's list, carrying over when it first appeared.  The list is thrown
	* away and rebuilt from the units every frame, so without this every marker would be newborn on
	* every frame and none of them would ever finish sliding in.
	*
	* A marker is last frame's marker when it is the n-th one belonging to the same unit.  Matching
	* on the destination instead would restart the slide on every frame of a chase, and matching on
	* the unit alone would give a whole queue one age. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addOrderHint( OrderHint& hint, const std::vector<OrderHint>& previous )
{
	Int ordinal = 0;
	for( std::vector<OrderHint>::const_iterator it = m_orderHints.begin();
			 it != m_orderHints.end(); ++it )
	{
		if( it->owner == hint.owner )
			ordinal++;
	}

	hint.bornMs = Clock_Milliseconds();

	Int seen = 0;
	for( std::vector<OrderHint>::const_iterator it = previous.begin();
			 it != previous.end(); ++it )
	{
		if( it->owner != hint.owner )
			continue;
		if( seen++ == ordinal )
		{
			hint.bornMs = it->bornMs;
			break;
		}
	}

	m_orderHints.push_back( hint );
}

//-------------------------------------------------------------------------------------------------
/** The attack key is armed and the player has pressed the left button: from here a drag draws a
	* circle rather than a selection box. */
//-------------------------------------------------------------------------------------------------
void InGameUI::beginAttackCircle( const ICoord2D& pt )
{
	m_isAttackCircling = TRUE;
	m_attackCircleAnchor = pt;
	m_attackCircleCursor = pt;
	TheTacticalView->screenToTerrain( &pt, &m_attackCircleCenter );
	DEBUG_LOG(("attack circle: begun at %d,%d with %d selected\n", pt.x, pt.y, getSelectCount()));
}

//-------------------------------------------------------------------------------------------------
void InGameUI::updateAttackCircle( const ICoord2D& pt )
{
	m_attackCircleCursor = pt;
}

//-------------------------------------------------------------------------------------------------
/** The circle the player is dragging, in world terms: its centre is the ground the press landed on,
	* and the cursor sits on its rim in that same plane.  The rim is not rayed onto the terrain: a
	* hill or a building under the cursor used to jump the radius every frame.  Both the rim drawn on
	* the screen and the wash laid on the ground ask this, so they cannot disagree about where the
	* circle is.  The centre is kept as ground, not as the pixel: a camera that moves during the drag
	* used to slide the whole circle over the map. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::getAttackCircleGround( Coord3D& center, Real& radius ) const
{
	if( !m_isAttackCircling )
		return FALSE;

	Coord3D rim;
	center = m_attackCircleCenter;
	TheTacticalView->screenToWorldAtZ( &m_attackCircleCursor, &rim, center.z );

	const Real dx = rim.x - center.x;
	const Real dy = rim.y - center.y;
	radius = (Real)sqrt( dx * dx + dy * dy );

	// a press that has not been dragged anywhere is a click, not a circle
	return radius >= 1.0f;
}

//-------------------------------------------------------------------------------------------------
/** The enemies standing in the circle are shared across the units that can shoot.  Both sides are
	* stood in the order they sit around the centre the player drew, the same order a move line uses
	* along its curve, and the shots are then dealt so the counts differ by one at most.  With enough
	* guns each takes one target.  With more targets than guns, each gun queues its own run of them
	* instead of the whole selection walking one list.  The pair is two ids on the attack message, the
	* target first, so every other machine fires what this one decided and nobody recomputes the
	* circle.  Without shift, a unit's first shot replaces whatever it was doing; with shift, the run
	* goes on the end of that unit's own queue.  Shroud decides which enemies count. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::issueAttackCircle( void )
{
	Coord3D center;
	Real radius;
	const Bool wasDragged = getAttackCircleGround( center, radius );

	m_isAttackCircling = FALSE;

	// the button went down and came back up without going anywhere, so this was a plain attack
	// click and the order belongs to the command translator, not here
	if( !wasDragged )
		return FALSE;

	const Player *local = ThePlayerList->getLocalPlayer();

	// who can actually shoot. a dozer standing in the selection is not given somebody else's target.
	// a passenger whose transport is also selected is left out too: the transport's own order already
	// tells everyone inside it to fire.
	const DrawableList *selected = getAllSelectedLocalDrawables();
	std::vector<const Object *> shooters;
	shooters.reserve( selected->size() );
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = (*it) ? (*it)->getObject() : NULL;
		if( obj == NULL || obj->isEffectivelyDead() || !obj->isAbleToAttack() )
			continue;
		shooters.push_back( obj );
	}

	std::vector<AttackAssignSlot> attackers;
	attackers.reserve( shooters.size() );
	for( std::vector<const Object *>::const_iterator it = shooters.begin(); it != shooters.end(); ++it )
	{
		const Object *obj = *it;
		const Object *holder = obj->getContainedBy();
		if( holder != NULL )
		{
			Bool holderSelected = FALSE;
			for( std::vector<const Object *>::const_iterator other = shooters.begin(); other != shooters.end(); ++other )
			{
				if( *other == holder )
				{
					holderSelected = TRUE;
					break;
				}
			}
			if( holderSelected )
				continue;
		}

		AttackAssignSlot slot;
		slot.id = obj->getID();
		slot.x = obj->getPosition()->x;
		slot.y = obj->getPosition()->y;
		attackers.push_back( slot );
	}

	std::vector<AttackAssignSlot> targets;
	// a radar ring is measured where it was painted, up to a sweep behind its unit, so the query
	// reaches past the circle and each spot is held to the circle itself.
	// ponytail: a unit that ran further than RING_LAG in one sweep drops out of its ring's circle
	const Real RING_LAG = 150.0f;
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &center, radius + RING_LAG, FROM_CENTER_2D, NULL );
	MemoryPoolObjectHolder holder( iter );
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		const Coord3D *spot = attackListSpot( obj, local );
		if( spot == NULL )
			continue;
		const Real dx = spot->x - center.x;
		const Real dy = spot->y - center.y;
		if( dx * dx + dy * dy > radius * radius )
			continue;

		AttackAssignSlot slot;
		slot.id = obj->getID();
		slot.x = spot->x;
		slot.y = spot->y;
		targets.push_back( slot );
	}

	orderAroundPoint( attackers, center.x, center.y );
	orderAroundPoint( targets, center.x, center.y );

	std::vector<AttackAssignPair> pairs;
	assignAttacks( (Int)attackers.size(), (Int)targets.size(), pairs );

	ObjectID previousAttacker = INVALID_ID;
	for( std::vector<AttackAssignPair>::const_iterator pair = pairs.begin(); pair != pairs.end(); ++pair )
	{
		const AttackAssignSlot& attacker = attackers[ pair->attacker ];
		const AttackAssignSlot& target = targets[ pair->target ];
		if( attacker.id == target.id )
			continue;

		const Bool firstForThisAttacker = attacker.id != previousAttacker;
		previousAttacker = attacker.id;

		// each unit's first shot starts that unit's list. the rest of its run is queued behind it.
		// shift appends the whole run onto whatever that one unit was already doing.
		const Bool startsList = firstForThisAttacker && !isInWaypointMode();
		markNextOrderQueued( startsList ? ORDER_QUEUE_FRESH : ORDER_QUEUE_APPEND );
		GameMessage *attack = TheMessageStream->appendMessage( GameMessage::MSG_DO_ATTACK_OBJECT );
		attack->appendObjectIDArgument( target.id );
		attack->appendObjectIDArgument( attacker.id );
	}

	DEBUG_LOG(("attack circle: radius %.0f, %d targets, %d attackers, %d shots\n", radius,
						 (Int)targets.size(), (Int)attackers.size(), (Int)pairs.size()));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Where the circle or the attack line measures this, or NULL when it does not go on the target
	* list.  A unit under the shroud goes on it only as a radar ring the player sees, and is measured
	* at the ring: its own spot would tell the circle's edge what the fog hides. */
//-------------------------------------------------------------------------------------------------
const Coord3D *InGameUI::attackListSpot( const Object *obj, const Player *local ) const
{
	if( obj->isEffectivelyDead() )
		return NULL;
	if( local->getRelationship( obj->getTeam() ) != ENEMIES )
		return NULL;
	const Coord3D *spot = obj->getPosition();
	if( isHiddenByShroud( obj ) )
	{
		spot = NULL;
		const std::vector<RadarContact> &rings = RadarContactMarks_fresh();
		for( size_t i = 0; i < rings.size() && spot == NULL; ++i )
			if( rings[ i ].id == obj->getID() )
				spot = &rings[ i ].pos;
		if( spot == NULL )
			return NULL;
	}

	//
	// Shroud is only half of invisible. A stealthed tank sitting in ground the player has
	// cleared passes the test above, so a circle swept over open country used to pick out every
	// hidden unit in it and open fire: a detector nobody built, and a way to read the map for
	// stealth by dragging a circle over it. Undetected means not in the circle.
	//
	if( obj->testStatus( OBJECT_STATUS_STEALTHED ) && !obj->testStatus( OBJECT_STATUS_DETECTED ) )
		return NULL;

	//
	// Not everything hostile standing in the circle is something to shoot at.  A shell or a
	// missile in flight is an object on the enemy's team like any other, and the range query hands
	// them over the same way it hands over tanks; the rest of the engine drops them by kind
	// wherever it scans.  Each one that reached the list cost the group a two second stall on a
	// target that was about to stop existing.
	//
	if( obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_UNATTACKABLE ) )
		return NULL;
	return spot;
}

//-------------------------------------------------------------------------------------------------
/** The line drawn with the attack key: every enemy it runs across is shared across the units that
	* can shoot, in the order both sides stand along the stroke, nearest the start first.  The shots
	* are dealt the same way as the circle, so the counts differ by one at most, and each unit works
	* down its own share.  "Across" is the line passing over the object's own footprint or near it,
	* within LINE_TOLERANCE of its edge.  Returns how many enemies the line crossed.  With
	* none, the caller fires on the ground along the line instead.  Enemies that nobody selected can
	* shoot still count, so the ground is not fired in their place. */
//-------------------------------------------------------------------------------------------------
Int InGameUI::issueAttackLine( const std::vector<Coord3D>& line )
{
	// how far off the line a unit's edge may be and still count: the line is drawn near the enemies
	// it means, not threaded through each one, so a tank's length either side of it counts
	const Real LINE_TOLERANCE = 40.0f;

	// the range query measures to centres, so it reaches this much further for a building whose
	// centre is off the line and whose walls are under it
	const Real LARGEST_FOOTPRINT = 150.0f;

	if( line.size() < 2 )
		return 0;

	// one query over the circle round the whole line, then each object is measured against it
	Region2D box;
	box.lo.x = box.hi.x = line[ 0 ].x;
	box.lo.y = box.hi.y = line[ 0 ].y;
	for( std::vector<Coord3D>::const_iterator point = line.begin(); point != line.end(); ++point )
	{
		box.lo.x = min( box.lo.x, point->x );
		box.lo.y = min( box.lo.y, point->y );
		box.hi.x = max( box.hi.x, point->x );
		box.hi.y = max( box.hi.y, point->y );
	}
	Coord3D center;
	center.x = ( box.lo.x + box.hi.x ) * 0.5f;
	center.y = ( box.lo.y + box.hi.y ) * 0.5f;
	center.z = 0.0f;
	const Real halfWidth = ( box.hi.x - box.lo.x ) * 0.5f;
	const Real halfHeight = ( box.hi.y - box.lo.y ) * 0.5f;
	const Real reach = sqrt( halfWidth * halfWidth + halfHeight * halfHeight ) + LINE_TOLERANCE + LARGEST_FOOTPRINT;

	const Player *local = ThePlayerList->getLocalPlayer();
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &center, reach, FROM_CENTER_2D, NULL );
	MemoryPoolObjectHolder holder( iter );

	// enemies the stroke actually crosses. who stands where along it is sorted afterwards, guns and
	// enemies the same way, so the deal does not depend on the order the range query handed them over
	std::vector<AttackAssignSlot> targets;
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		// the reach's footprint slack also covers a radar ring left up to a sweep behind its unit
		const Coord3D *pos = attackListSpot( obj, local );
		if( pos == NULL )
			continue;

		const Real allowed = obj->getGeometryInfo().getBoundingCircleRadius() + LINE_TOLERANCE;
		Real bestDistanceSqr = allowed * allowed;
		Real along = -1.0f;
		Real walked = 0.0f;
		for( size_t i = 1; i < line.size(); ++i )
		{
			const Real dx = line[ i ].x - line[ i - 1 ].x;
			const Real dy = line[ i ].y - line[ i - 1 ].y;
			const Real lengthSqr = dx * dx + dy * dy;
			const Real length = sqrt( lengthSqr );
			Real t = 0.0f;
			if( lengthSqr > 0.0f )
				t = clamp( 0.0f, ( ( pos->x - line[ i - 1 ].x ) * dx + ( pos->y - line[ i - 1 ].y ) * dy ) / lengthSqr, 1.0f );
			const Real offX = line[ i - 1 ].x + dx * t - pos->x;
			const Real offY = line[ i - 1 ].y + dy * t - pos->y;
			const Real distanceSqr = offX * offX + offY * offY;
			if( distanceSqr <= bestDistanceSqr )
			{
				bestDistanceSqr = distanceSqr;
				along = walked + length * t;
			}
			walked += length;
		}

		if( along >= 0.0f )
		{
			AttackAssignSlot slot;
			slot.id = obj->getID();
			slot.x = pos->x;
			slot.y = pos->y;
			targets.push_back( slot );
		}
	}

	// who can actually shoot. a dozer standing in the selection is not given somebody else's target.
	// a passenger whose transport is also selected is left out too: the transport's own order already
	// tells everyone inside it to fire. the circle collects the same list.
	std::vector<AttackAssignSlot> attackers;
	std::vector<AttackAssignPair> pairs;
	if( !targets.empty() )
	{
		const DrawableList *selected = getAllSelectedLocalDrawables();
		std::vector<const Object *> shooters;
		shooters.reserve( selected->size() );
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			const Object *obj = (*it) ? (*it)->getObject() : NULL;
			if( obj == NULL || obj->isEffectivelyDead() || !obj->isAbleToAttack() )
				continue;
			shooters.push_back( obj );
		}

		attackers.reserve( shooters.size() );
		for( std::vector<const Object *>::const_iterator it = shooters.begin(); it != shooters.end(); ++it )
		{
			const Object *obj = *it;
			const Object *container = obj->getContainedBy();
			if( container != NULL )
			{
				Bool containerSelected = FALSE;
				for( std::vector<const Object *>::const_iterator other = shooters.begin(); other != shooters.end(); ++other )
				{
					if( *other == container )
					{
						containerSelected = TRUE;
						break;
					}
				}
				if( containerSelected )
					continue;
			}

			AttackAssignSlot slot;
			slot.id = obj->getID();
			slot.x = obj->getPosition()->x;
			slot.y = obj->getPosition()->y;
			attackers.push_back( slot );
		}

		std::vector<Real> arc;
		buildPathArcLengths( line, arc );
		orderAlongPath( attackers, line, arc );
		orderAlongPath( targets, line, arc );
		assignAttacks( (Int)attackers.size(), (Int)targets.size(), pairs );

		ObjectID previousAttacker = INVALID_ID;
		for( std::vector<AttackAssignPair>::const_iterator pair = pairs.begin(); pair != pairs.end(); ++pair )
		{
			const AttackAssignSlot& attacker = attackers[ pair->attacker ];
			const AttackAssignSlot& target = targets[ pair->target ];
			if( attacker.id == target.id )
				continue;

			const Bool firstForThisAttacker = attacker.id != previousAttacker;
			previousAttacker = attacker.id;

			// each unit's first shot starts that unit's list. the rest of its run is queued behind it.
			// shift appends the whole run onto whatever that one unit was already doing.
			const Bool startsList = firstForThisAttacker && !isInWaypointMode();
			markNextOrderQueued( startsList ? ORDER_QUEUE_FRESH : ORDER_QUEUE_APPEND );
			GameMessage *attack = TheMessageStream->appendMessage( GameMessage::MSG_DO_ATTACK_OBJECT );
			attack->appendObjectIDArgument( target.id );
			attack->appendObjectIDArgument( attacker.id );
		}
	}

	DEBUG_LOG(("attack line: %d points, %d targets, %d attackers, %d shots\n", (Int)line.size(),
						 (Int)targets.size(), (Int)attackers.size(), (Int)pairs.size()));
	return (Int)targets.size();
}

//-------------------------------------------------------------------------------------------------
/** Whether the shroud is over this object as far as the player at this machine is concerned, or the
	* player a watcher with fog on is looking through. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isHiddenByShroud( const Object *obj ) const
{
	return obj->getShroudedStatus( TheObserverCamera.getShroudPlayerIndex() ) > OBJECTSHROUD_PARTIAL_CLEAR;
}

//-------------------------------------------------------------------------------------------------
void InGameUI::markNextOrderQueued( OrderQueueMode mode )
{
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_QUEUE_NEXT_ORDER );
	msg->appendIntegerArgument( mode );
}

//-------------------------------------------------------------------------------------------------
/** An order landed on the ground: a marker where it did.  Classic graphics start EA's ring
	* (W3DInGameUI::drawMoveHints), and only for the moves EA gave it to; otherwise the order's own
	* coloured marker closes in on the point (W3DInGameUI::drawOrderRings). */
//-------------------------------------------------------------------------------------------------
void InGameUI::createMoveHint( const Coord3D& at, OrderHintKind kind, Real hull )
{
	if( TheGlobalData->m_classicGraphics && kind != ORDER_HINT_MOVE && kind != ORDER_HINT_ATTACK_MOVE && kind != ORDER_HINT_WAYPOINT )
		return;

	// a lone tower shooting the ground or a unit still marks the spot
	if( kind != ORDER_HINT_ATTACK_GROUND && !isAttackOrderHint( kind ) && getSelectCount() == 1 )
	{
		Drawable *draw = getFirstSelectedDrawable();
		Object *obj = draw ? draw->getObject() : NULL;
		if( obj && obj->isKindOf( KINDOF_IMMOBILE ) )
		{
			//Don't allow move hints to be created if our selected object can't move!
			return;
		}
	}

	m_moveHint[ m_nextMoveHint ].frame = TheGameClient->getFrame();
	m_moveHint[ m_nextMoveHint ].bornMs = MAX( 1u, (UnsignedInt)Clock_Milliseconds() );
	m_moveHint[ m_nextMoveHint ].pos = at;
	m_moveHint[ m_nextMoveHint ].kind = kind;
	m_moveHint[ m_nextMoveHint ].hull = hull;

	m_nextMoveHint++;
	if (m_nextMoveHint == InGameUI::MAX_MOVE_HINTS)
		m_nextMoveHint = 0;
}

//-------------------------------------------------------------------------------------------------
/** A formation line was just sent: the stations it was worked out into are still the hints, so each
	* gets the marker a plain move puts on its one point.  EA had no formation line, so Classic none. */
//-------------------------------------------------------------------------------------------------
void InGameUI::createFormationMoveHints( void )
{
	if( TheGlobalData->m_classicGraphics )
		return;
	for( std::vector<OrderHint>::const_iterator it = m_orderHints.begin(); it != m_orderHints.end(); ++it )
		if( !it->onObject && it->mark != ORDER_MARK_NONE )
			createMoveHint( it->to, it->kind );
}

//-------------------------------------------------------------------------------------------------
/** A unit's first line ends where the player clicked, not where logic parks it.  A group move
	* spreads the units' stopping places round the click, and the thread they share, the middle of
	* those places, landed a tank's length or more off the marker the player saw.  Each unit carries
	* the click of its last ground order from here (predictOrderHints) for as long as its first line
	* is that order's.  It is while the line keeps the order's kind (an attack move's unit stopping
	* to fight is still on it); while it stays within reach of the click until a path planned for this
	* order comes back, and within reach of that path's end after; and while the unit has no fewer
	* lines than it had, so reaching the click with a shift list behind it ends the order.  A line that
	* moves off is another order and the click is forgotten, as is the click of a unit that draws no
	* line and waits on no order (deselected, arrived, dead).  A shift-queued leg ends on its own click
	* the same way (rememberQueuedClicks), the unit's queued clicks laid on its ground legs from the
	* last back.  Each next leg starts on the click the leg before ended on, so the joint and the next
	* thread never part.  The unit's own dot stays on its own place.  ORDERPOINT in the log says when
	* a click settles and why one is forgotten. */
//-------------------------------------------------------------------------------------------------
void InGameUI::applyCommandedPoints( void )
{
	const Real REACH = 100.0f;		// bunchOrderHints' radius: a flood of stopping places round one click
	// a settled unit's path end may be replanned a cell or two off its first one; further is a new order
	const Real SETTLED_SLACK = 3.0f * PATHFIND_CELL_SIZE_F;
	// a queued leg is paired with its click by order, so the reach only guards against a stray pairing
	const Real QUEUED_REACH = 3.0f * REACH;
	// a queued click waits this long for its leg before it is called finished or orphaned: queued
	// orders are not drawn before logic runs them, a few frames of run-ahead in a network game
	const UnsignedInt QUEUED_CLICK_GRACE_MS = 2000;
	const UnsignedInt now = Clock_Milliseconds();
	const UnsignedInt frame = TheGameLogic->getFrame();
	auto apartSq = []( const Coord3D& a, const Coord3D& b ) { return ( a.x - b.x ) * ( a.x - b.x ) + ( a.y - b.y ) * ( a.y - b.y ); };
	auto hasPending = [this]( ObjectID owner )
	{
		for( size_t p = 0; p < m_pendingOrders.size(); ++p )
			if( m_pendingOrders[ p ].owner == owner )
				return TRUE;
		return FALSE;
	};
	// a leg ends on 'at': it and the next leg's start
	auto snapLeg = [this]( size_t leg, const Coord3D& at )
	{
		m_orderHints[ leg ].to = at;
		m_orderHints[ leg ].clicked = TRUE;
		if( leg + 1 < m_orderHints.size() && m_orderHints[ leg + 1 ].owner == m_orderHints[ leg ].owner && m_orderHints[ leg + 1 ].fromTarget == INVALID_ID )
			m_orderHints[ leg + 1 ].from = at;
	};

	for( size_t c = 0; c < m_commandedPoints.size(); ++c )
		m_commandedPoints[ c ].seen = FALSE;

	for( size_t i = 0; i < m_orderHints.size(); ++i )
	{
		if( i > 0 && m_orderHints[ i - 1 ].owner == m_orderHints[ i ].owner )
			continue;		// once a unit, from its first line
		const ObjectID owner = m_orderHints[ i ].owner;
		Int legs = 1;
		while( i + legs < m_orderHints.size() && m_orderHints[ i + legs ].owner == owner )
			++legs;

		// the first line and the order that is not queued
		Bool firstSnapped = FALSE;
		size_t c = 0;
		while( c < m_commandedPoints.size() && m_commandedPoints[ c ].owner != owner )
			++c;
		if( c < m_commandedPoints.size() )
		{
			OrderHint& hint = m_orderHints[ i ];
			CommandedPoint& point = m_commandedPoints[ c ];
			point.seen = TRUE;

			const char *forgot = NULL;
			// an attack move's unit stopping to fight draws the fight; the click waits for it to go on
			const Bool fighting = point.kind == ORDER_HINT_ATTACK_MOVE && ( hint.kind == ORDER_HINT_ATTACK || hint.kind == ORDER_HINT_FORCE_ATTACK );
			if( fighting )
			{
			}
			else if( hint.onObject || hint.kind != point.kind )
				forgot = "another kind of order";
			else if( point.legs > 0 && legs < point.legs )
				forgot = "a leg was finished";
			else if( !point.settled && apartSq( hint.to, point.at ) >= REACH * REACH )
				forgot = "out of reach of the click";
			else if( point.settled && apartSq( hint.to, point.goal ) >= SETTLED_SLACK * SETTLED_SLACK )
				forgot = "the stopping place moved";		// a leg finished and the next queued in the same frame, or another order
			else if( !point.settled && !hasPending( owner ) )
			{
				// The path's end is the stopping place, but only a path planned for this order: a unit
				// nudged on its way still holds the last order's path for a few frames, and settling on
				// that put sixteen of twenty tanks on their old spots (InGameUI::placeOrderRing's test)
				const Object *obj = TheGameLogic->findObjectByID( owner );
				const AIUpdateInterface *ai = obj ? obj->getAI() : NULL;
				if( ai && ai->getPath() && !ai->isWaitingForPath() && TheGameLogic->getFrame() - ai->getPathAge() >= point.dueFrame )
				{
					point.settled = TRUE;
					point.goal = hint.to;
					DEBUG_LOG(( "ORDERPOINT unit %d settled on (%.0f, %.0f), click (%.0f, %.0f)\n", (Int)owner, hint.to.x, hint.to.y, point.at.x, point.at.y ));
				}
			}
			if( forgot )
			{
				DEBUG_LOG(( "ORDERPOINT unit %d forgot click (%.0f, %.0f): %s\n", (Int)owner, point.at.x, point.at.y, forgot ));
				m_commandedPoints.erase( m_commandedPoints.begin() + c );
			}
			else if( !fighting )
			{
				point.legs = legs;
				snapLeg( i, point.at );
				firstSnapped = TRUE;
			}
		}

		// The queued legs: the unit's ground legs after the first (or from the first, when that is a
		// queued one already) take its queued clicks, the last leg the last click and back from there,
		// as long as each pair is the same colour of order and within reach. Only clicks logic has run:
		// a queued order draws no leg before then, and the newest click laid on the last drawn leg put
		// every joint one click ahead
		m_pairLegs.clear();
		for( Int k = firstSnapped ? 1 : 0; k < legs; ++k )
		{
			const OrderHint& leg = m_orderHints[ i + k ];
			if( !leg.onObject && leg.kind != ORDER_HINT_ABILITY && leg.kind != ORDER_HINT_UPGRADE )
				m_pairLegs.push_back( i + k );
		}
		m_pairClicks.clear();
		for( size_t q = 0; q < m_queuedClicks.size(); ++q )
			if( m_queuedClicks[ q ].owner == owner && frame > m_queuedClicks[ q ].dueFrame )
				m_pairClicks.push_back( q );
		size_t g = m_pairLegs.size(), n = m_pairClicks.size();
		// A pair that does not fit (another colour, or a far member of a big group spread further than
		// QUEUED_REACH round its point) is left unsnapped and the rest still pair: stopping there left
		// every older click unpaired, dropped a moment later, and the unit's later legs had nothing to
		// end on once its first leg was done. A click with a leg to pair with is still owed
		while( g > 0 && n > 0 )
		{
			--g;
			--n;
			QueuedClick& click = m_queuedClicks[ m_pairClicks[ n ] ];
			const OrderHint& leg = m_orderHints[ m_pairLegs[ g ] ];
			click.matched = TRUE;
			if( orderKindColor( leg.kind ) == orderKindColor( click.kind ) && apartSq( leg.to, click.at ) < QUEUED_REACH * QUEUED_REACH )
				snapLeg( m_pairLegs[ g ], click.at );
		}
	}

	// no line and no order waiting: deselected, arrived or dead
	for( size_t c = 0; c < m_commandedPoints.size(); )
	{
		if( !m_commandedPoints[ c ].seen && !hasPending( m_commandedPoints[ c ].owner ) )
			m_commandedPoints.erase( m_commandedPoints.begin() + c );
		else
			++c;
	}
	// A click logic has run and no leg paired with this frame is done with: its leg finished, logic
	// refused the order, or the unit is gone from the selection. Given a moment first
	for( size_t q = 0; q < m_queuedClicks.size(); )
	{
		QueuedClick& click = m_queuedClicks[ q ];
		if( !click.matched && frame > click.dueFrame && now - click.bornMs > QUEUED_CLICK_GRACE_MS )
			m_queuedClicks.erase( m_queuedClicks.begin() + q );
		else
		{
			click.matched = FALSE;
			++q;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** An attack command has occurred: the target flashes on the click, as a building being garrisoned
	* does, instead of waiting for logic to run the order and the first shot, and the order's marker
	* closes on the ground under it (an aircraft's too, a unit on a bridge on the deck). */
//-------------------------------------------------------------------------------------------------
void InGameUI::createAttackHint( const GameMessage *msg )
{
	markAttackTarget( msg, ORDER_HINT_ATTACK );
}

//-------------------------------------------------------------------------------------------------
/** A force attack command has occurred: a unit or building aimed at flashes, as with an attack.
	* Aimed at the ground the message holds a place, not an object, and the ground's marker is
	* HintSpy's createMoveHint. */
//-------------------------------------------------------------------------------------------------
void InGameUI::createForceAttackHint( const GameMessage *msg )
{
	markAttackTarget( msg, ORDER_HINT_FORCE_ATTACK );
}

//-------------------------------------------------------------------------------------------------
void InGameUI::markAttackTarget( const GameMessage *msg, OrderHintKind kind )
{
	if( msg->getArgumentCount() < 1 || msg->getArgumentDataType( 0 ) != ARGUMENTDATATYPE_OBJECTID )
		return;
	const Object *target = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );
	if( target == NULL )
		return;
	Drawable *draw = target->getDrawable();
	if( draw )
		draw->flashAsSelected();
	Coord3D under = *target->getPosition();
	under.z = TheTerrainLogic->getLayerHeight( under.x, under.y, target->getLayer() );
	createMoveHint( under, kind, target->getGeometryInfo().getBoundingCircleRadius() );
}

//-------------------------------------------------------------------------------------------------
/** An garrison command has occurred, start graphical "hint". */
//-------------------------------------------------------------------------------------------------
void InGameUI::createGarrisonHint( const GameMessage *msg )
{
	Drawable *draw = TheGameClient->findDrawableByID( msg->getArgument(0)->drawableID );
	if( draw )
	{
		draw->onSelected();
	}
}

#if defined(_DEBUG) || defined(_INTERNAL)
#define AI_DEBUG_TOOLTIPS		1

#ifdef AI_DEBUG_TOOLTIPS
#include "Common/StateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/AIPathfind.h"
#endif // AI_DEBUG_TOOLTIPS

#endif // defined(_DEBUG) || defined(_INTERNAL)

//-------------------------------------------------------------------------------------------------
/** Details of what is mouse hovered over right now are in this message.  Terrain might result
	* in just a tooltip.  An object might get a tooltip and show its hit points.
 */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isOverSidebarHud( const ICoord2D &pixel ) const
{
	if( !sidebarLayoutOn() )
		return FALSE;
	const HudLayoutRects r = hudRects();
	auto inside = [ &pixel ]( const IRegion2D &box )
	{
		return pixel.x >= box.lo.x && pixel.x < box.hi.x && pixel.y >= box.lo.y && pixel.y < box.hi.y;
	};
	return inside( r.sidebar ) || inside( r.strip ) || inside( r.card );
}

void InGameUI::createMouseoverHint( const GameMessage *msg )
{
	if (m_isScrolling || m_isSelecting)
		return; // no mouseover for you

	// The Red Alert style layout's parent window is full screen and see-through, so the world's pick
	// ran under its steel and an object's tooltip and highlight showed through the sidebar.
	// SelectionXlat sends one location hint as the cursor comes onto the steel and none while it stays:
	// that one lets go of whatever was under the cursor in the world
	if( TheMouse && isOverSidebarHud( TheMouse->getMouseStatus()->pos ) )
	{
		if( m_mousedOverDrawableID != INVALID_DRAWABLE_ID )
		{
			m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
			TheMouse->resetTooltipDelay();
		}
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		setMouseCursor( Mouse::ARROW );
		return;
	}

	// a signal armed off the bar is where the next click goes, over a unit, the ground or the radar
	if( isSignalArmed() )
	{
		setMouseCursor( Mouse::CROSS );
		return;
	}

	GameWindow *window = NULL;
	const MouseIO *io = TheMouse->getMouseStatus();
	Bool underWindow = false;
	if (io && TheWindowManager)
		window = TheWindowManager->getWindowUnderCursor(io->pos.x, io->pos.y);

	while (window)
	{
		if (window->winGetInputFunc() == LeftHUDInput) {
			underWindow = false;
			break;
		}
		
		// check to see if it or any of its parents are opaque.  If so, we can't select anything.
		if (!BitTest( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
		{
			underWindow = true;
			break;
		}

		window = window->winGetParent();
	}
	if (underWindow)
	{
		setMouseCursor(Mouse::ARROW); // regardless of m_mouseMode
		return;
	}

  



	DrawableID oldID = m_mousedOverDrawableID;

	if (msg->getType() == GameMessage::MSG_MOUSEOVER_DRAWABLE_HINT)
	{
		TheMouse->setCursorTooltip(UnicodeString::TheEmptyString );
		m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
		const Drawable *draw = TheGameClient->findDrawableByID(msg->getArgument(0)->drawableID);
		const Object *obj = draw ? draw->getObject() : NULL;
		if( obj )
		{
			
 			//Ahh, here is a wierd exception: if the moused-over drawable is a mob-member
			//(e.g. AngryMob), Lets fool the UI into creating the hint for the NEXUS instead...
 			if (obj->isKindOf( KINDOF_IGNORED_IN_GUI ))
 			{
 				static NameKeyType key_MobMemberSlavedUpdate = NAMEKEY( "MobMemberSlavedUpdate" );
 				MobMemberSlavedUpdate *MMSUpdate = (MobMemberSlavedUpdate*)obj->findUpdateModule( key_MobMemberSlavedUpdate );
 				if( MMSUpdate )
 				{
 					Object *slaver = TheGameLogic->findObjectByID(MMSUpdate->getSlaverID());
 					if ( slaver )
 					{
 						Drawable *slaverDraw = slaver->getDrawable();
 						if ( slaverDraw )
 							m_mousedOverDrawableID = slaverDraw->getID();
 							// if this fails, not to worry... it has already defaulted to INVALID_DRAWABLE_ID, above
 					}
 				}
 			}
 			else
 				m_mousedOverDrawableID = draw->getID();

#if defined(_DEBUG) || defined(_INTERNAL) //Extra hacky, sorry, but I need to use this in constantdebug report
			if ( TheGlobalData->m_constantDebugUpdate == TRUE )
				m_mousedOverDrawableID = draw->getID();
#endif


			const Player* player = NULL;
			const ThingTemplate *thingTemplate = obj->getTemplate();

			ContainModuleInterface* contain = obj->getContain();
			if( contain )
				player = contain->getApparentControllingPlayer(ThePlayerList->getLocalPlayer());

			if (player == NULL)
				player = obj->getControllingPlayer();

			Bool disguised = false;
			if( obj->isKindOf( KINDOF_DISGUISER ) )
			{
				//Because we have support for disguised units pretending to be units from another
				//team, we need to intercept it here and make sure it's rendered appropriately
				//based on which client is rendering it.
        StealthUpdate *update = obj->getStealth();
				if( update )
				{
					if( update->isDisguised() )
					{
						Player *clientPlayer = ThePlayerList->getLocalPlayer();
						Player *disguisedPlayer = ThePlayerList->getNthPlayer( update->getDisguisedPlayerIndex() );
						if( player->getRelationship( clientPlayer->getDefaultTeam() ) != ALLIES && clientPlayer->isPlayerActive() )
						{
							//Neutrals and enemies will see this disguised unit as the team it's disguised as.
							player = disguisedPlayer;
							const ThingTemplate *disguisedTemplate = update->getDisguisedTemplate();
							if( disguisedTemplate )
							{
								thingTemplate = disguisedTemplate;
								disguised = true;
							}
						}
						//Otherwise, the color will show up as the team it really belongs to (already set above).
					}
				}
			}


			UnicodeString str = thingTemplate->getDisplayName();
			UnicodeString displayName = thingTemplate->getDisplayName();
			if( str.isEmpty() )
			{
				AsciiString txtTemp;
				txtTemp.format("ThingTemplate:%s", obj->getTemplate()->getName().str());
				str = TheGameText->fetch(txtTemp);
				//str.format(L"ThingTemplate:'%hs'", obj->getTemplate()->getName().str());
			}

#ifdef AI_DEBUG_TOOLTIPS
			if (TheGlobalData->m_debugAI) {
				const Team *team = obj->getTeam();
				AsciiString objName = obj->getName();
				AsciiString teamName;
				AsciiString stateName;
				
				AIUpdateInterface *ai = (AIUpdateInterface*)obj->getAI();
				if (ai) {
					if (ai->getPath()) {
						TheAI->pathfinder()->setDebugPath(ai->getPath());
					}
#ifdef STATE_MACHINE_DEBUG	
					stateName = ai->getCurrentStateName();
					if (ai->getAttackInfo()) {
						stateName.concat(" AttackPriority=");
						stateName.concat(ai->getAttackInfo()->getName());
					}
#endif
				}
				if( team )
				{
					teamName = team->getName();
				}
				if (!objName.isEmpty())
				{
					if (!teamName.isEmpty())
					{
						str.format(u"%hs(%hs): %s", teamName.str(), objName.str(), str.str());
					}
					else
					{
						str.format(u"%hs: %s", objName.str(), str.str());
					}
				}
				else
				{
					if (!teamName.isEmpty())
					{
						str.format(u"%hs: %s", teamName.str(), str.str());
					}
				}
				str.format(u"%s - %hs", str.str(), stateName.str());

			}
#endif
			UnicodeString warehouseFeedback;
			// Add on dollar amount of warehouse contents so people don't freak out until the art is hooked up
			static const NameKeyType warehouseModuleKey = TheNameKeyGenerator->nameToKey( "SupplyWarehouseDockUpdate" );
			SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate *)obj->findUpdateModule( warehouseModuleKey );
			if( warehouseModule != NULL )
			{
				Int boxes = warehouseModule->getBoxesStored();
				Int value = boxes * TheGlobalData->m_baseValuePerSupplyBox;
				warehouseFeedback.format(TheGameText->fetch("TOOLTIP:SupplyWarehouse"), value);
				str.concat(warehouseFeedback);
				// the lobby's supply pile limit: say so when this player's gatherers would be turned away
				if( warehouseModule->isClosedToPlayer( ThePlayerList->getLocalPlayer() ) )
					str.concat( TheGameText->fetch( "TOOLTIP:SupplyPileFull" ) );
			}

      if (player)
			{
				UnicodeString tooltip;
				//if (TheRecorder->isMultiplayer() && player->getPlayerType() == PLAYER_HUMAN)
				if (TheRecorder->isMultiplayer() && player->isPlayableSide())
					tooltip.format(u"%s\n%s", str.str(), ((Player *)player)->getPlayerDisplayName().str());
				else
					tooltip = str;

				Int localPlayerIndex = ThePlayerList ? TheObserverCamera.getShroudPlayerIndex() : 0;

				Int x, y;
				ThePartitionManager->worldToCell(obj->getPosition()->x, obj->getPosition()->y, &x, &y);
				if( ThePartitionManager->getShroudStatusForPlayer(localPlayerIndex, x, y) == CELLSHROUD_CLEAR )
				{
					RGBColor rgb;
					if( disguised )
					{
						rgb.setFromInt( clientPlayerColor( player ) );
					}
					else
					{
						rgb.setFromInt( clientColor( draw->getObject()->getIndicatorColor() ) );

						// Unless this is a stealth garrisoned building, 
						// Let's not use the contained's housecolor
						const Object *obj = draw->getObject();
						if ( obj )
						{
							ContainModuleInterface *contain = obj->getContain();
							if ( contain && contain->isGarrisonable() )
							{
								const Player *play = contain->getApparentControllingPlayer( ThePlayerList->getLocalPlayer() );
								if ( play )
									rgb.setFromInt( clientPlayerColor( play ) );
							}
						}

					}

					//Object:Prop is a blank string... but we don't want to show
					//any popup box at all if that is the case!
					if( displayName.compare( TheGameText->fetch( "OBJECT:Prop" ) ) )
					{
	  				TheMouse->setCursorTooltip(tooltip, -1, &rgb );
					}
				}
			}
		}

	}
	else
	{
		m_mousedOverDrawableID = INVALID_DRAWABLE_ID;
	}

	if (oldID != m_mousedOverDrawableID)
	{
		//DEBUG_LOG(("Resetting tooltip delay\n"));
		TheMouse->resetTooltipDelay();
	}

	if (m_mouseMode == MOUSEMODE_DEFAULT && !m_isScrolling && !m_isSelecting && !TheInGameUI->getSelectCount() && (TheRecorder->getMode() != RECORDERMODETYPE_PLAYBACK || TheLookAtTranslator->hasMouseMovedRecently()))
	{
		if( m_mousedOverDrawableID != INVALID_DRAWABLE_ID )
		{
			Drawable *draw = TheGameClient->findDrawableByID(m_mousedOverDrawableID);
			
			//Add basic logic to determine if we can select a unit (or hint)
			const Object *obj = draw ? draw->getObject() : NULL;
			Bool drawSelectable = CanSelectDrawable(draw, FALSE);
			if( !obj )
			{
				drawSelectable = false;
			}

			if( drawSelectable && obj->isLocallyControlled() )
			{
				setMouseCursor(Mouse::SELECTING);
			}
			else
			{
				setMouseCursor(Mouse::ARROW);
			}
		}
		else
		{
			setMouseCursor(Mouse::ARROW);
		}
	}
	else if( m_mouseMode == MOUSEMODE_BUILD_PLACE )
	{
		// Over something no order can be given to (a dozer over an enemy power plant) no command
		// hint comes, only this one, so the pointer kept the build cursor the last spot of open
		// ground left while the ghost had turned red.  It follows the ghost's legality here as well.
		setMouseCursor( placementCursor( m_placementLegal ) );
	}
	else if (m_mouseMode != MOUSEMODE_DEFAULT)
	{
		setMouseCursor((Mouse::MouseCursor)m_mouseModeCursor);
	}
}

//-------------------------------------------------------------------------------------------------
/** A command would be given if a click were to happen, so give a preview hint of what it would be.
	* Changing the mouse cursor is an example
	*/
void InGameUI::createCommandHint( const GameMessage *msg )
{
	if (m_isScrolling || m_isSelecting || TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
		return;

	if( isSignalArmed() )
	{
		setMouseCursor( Mouse::CROSS );
		return;
	}

	const Drawable *draw = TheGameClient->findDrawableByID(m_mousedOverDrawableID);
	GameMessage::Type t = msg->getType();
//#ifdef DO_SHROUD_PROJECTION
	if( draw && (t == GameMessage::MSG_DO_ATTACK_OBJECT_HINT || t == GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT) )
	{
		const Object* obj = draw->getObject();
		Int localPlayerIndex = ThePlayerList ? ThePlayerList->getLocalPlayer()->getPlayerIndex() : 0;
#if defined(_DEBUG) || defined(_INTERNAL)
		ObjectShroudStatus ss = (!obj || !TheGlobalData->m_shroudOn) ? OBJECTSHROUD_CLEAR : obj->getShroudedStatus(localPlayerIndex);
#else
		ObjectShroudStatus ss = (!obj) ? OBJECTSHROUD_CLEAR : obj->getShroudedStatus(localPlayerIndex);
#endif
		if (ss == OBJECTSHROUD_SHROUDED)
		{
			t = GameMessage::MSG_DO_MOVETO_HINT;	// if the object is hidden, switch to something innocuous
		}
	}
//#endif


	//
	// While a structure is on the cursor, handleBuildPlacements owns the radius cursor - it is the
	// range ring for the thing being placed. Wiping it here (this runs on every mouse hint) is what
	// made that ring flicker or never appear at all.
	//
	if( m_pendingPlaceType == NULL )
		setRadiusCursorNone();

  if ( TheGlobalData->m_doubleClickAttackMove )
  {
    if ( --m_duringDoubleClickAttackMoveGuardHintTimer > 0 )
    {
      setMouseCursor(Mouse::FORCE_ATTACK_GROUND);
		  setRadiusCursor(RADIUSCURSOR_GUARD_AREA, 
										  NULL,
										  PRIMARY_WEAPON);
      return;
    }
  }





	// set cursor to normal if there is a window under the cursor
	GameWindow *window = NULL;
	const MouseIO *io = TheMouse->getMouseStatus();
	Bool underWindow = false;
	if (io && TheWindowManager)
		window = TheWindowManager->getWindowUnderCursor(io->pos.x, io->pos.y);


	while (window)
	{
		if (window->winGetInputFunc() == LeftHUDInput) {
			underWindow = false;
			break;
		}
		
		// check to see if it or any of its parents are opaque.  If so, we can't select anything.
		if (!BitTest( window->winGetStatus(), WIN_STATUS_SEE_THRU ))
		{
			underWindow = true;
			break;
		}

		window = window->winGetParent();
	}

	//Add basic logic to determine if we can select a unit (or hint)
	const Object *obj = draw ? draw->getObject() : NULL;
	Bool drawSelectable = CanSelectDrawable(draw, FALSE);
	if( !obj )
	{
		drawSelectable = false;
	}

	// Note: These are only non-NULL if there is exactly one thing selected.
	const Drawable *srcDraw = NULL;
	const Object *srcObj = NULL;
	if (getSelectCount() == 1) {
		srcDraw = getAllSelectedDrawables()->front();
		srcObj = (srcDraw ? srcDraw->getObject() : NULL);
	}

	switch (m_mouseMode)
	{
		case MOUSEMODE_DEFAULT:
			{ 
				// This section of code only gets called when there is no specific cursor mode happening.
				if (underWindow || (srcObj && !srcObj->isLocallyControlled()))
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				// the guard key looks like EA's guard button armed: the guard range decal under the cursor
				if( isGuardArmed() )
					setRadiusCursor( RADIUSCURSOR_GUARD_AREA, NULL, PRIMARY_WEAPON );
				switch (t)
				{
					case GameMessage::MSG_DO_MOVETO_HINT:
					{
						if( !drawSelectable && srcObj && srcObj->isLocallyControlled() && srcObj->isKindOf(KINDOF_STRUCTURE))
							setMouseCursor( Mouse::GENERIC_INVALID );
						else if( drawSelectable && obj->isLocallyControlled() && !obj->isKindOf(KINDOF_MINE))
							setMouseCursor( Mouse::SELECTING );
						else if( TheRadar->isRadarWindow( window ) &&
										 TheRadar->isRadarForced() == FALSE &&
										 (TheRadar->isRadarHidden() || 
										 ThePlayerList->getLocalPlayer()->hasRadar() == FALSE) )
							setMouseCursor( Mouse::ARROW );
						else if( isGuardArmed() )
							setMouseCursor( Mouse::GUARD );	// the guard buttons' own cursor (CommandSetReforged.ini)
						else
							setMouseCursor( Mouse::MOVETO );
						break;
					}
					case GameMessage::MSG_DO_ATTACKMOVETO_HINT:
						if( drawSelectable && obj->isLocallyControlled()  )
							setMouseCursor( Mouse::SELECTING );
						else
							setMouseCursor( Mouse::ATTACKMOVETO );
						break;
					case GameMessage::MSG_ADD_WAYPOINT_HINT:
						setMouseCursor( Mouse::WAYPOINT );
						break;
					case GameMessage::MSG_DO_ATTACK_OBJECT_HINT:
						setMouseCursor( Mouse::ATTACK_OBJECT );
						break;
					case GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT:
						setMouseCursor( Mouse::OUTRANGE );
						break;
					case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT_HINT:
						setMouseCursor( Mouse::FORCE_ATTACK_OBJECT );
						break;
					case GameMessage::MSG_DO_FORCE_ATTACK_GROUND_HINT:
						setMouseCursor( Mouse::FORCE_ATTACK_GROUND );
						break;
					case GameMessage::MSG_GET_REPAIRED_HINT:
						setMouseCursor( Mouse::GET_REPAIRED );
						break;
					case GameMessage::MSG_DOCK_HINT:
						setMouseCursor( Mouse::DOCK );
						break;
					case GameMessage::MSG_GET_HEALED_HINT:
						setMouseCursor( Mouse::GET_HEALED );
						break;
					case GameMessage::MSG_DO_REPAIR_HINT:
						setMouseCursor( Mouse::DO_REPAIR );
						break;
					case GameMessage::MSG_RESUME_CONSTRUCTION_HINT:
						setMouseCursor( Mouse::RESUME_CONSTRUCTION );
						break;				
					case GameMessage::MSG_ENTER_HINT:
						setMouseCursor( enterCursor( obj ) );
						break;
					case GameMessage::MSG_CONVERT_TO_CARBOMB_HINT:
						setMouseCursor( Mouse::CAR_BOMB );
						break;
					case GameMessage::MSG_HIJACK_HINT:
						setMouseCursor( Mouse::HIJACK );
						break;
					case GameMessage::MSG_SABOTAGE_HINT:
						setMouseCursor( Mouse::SABOTAGE );
						break;
					case GameMessage::MSG_DEFECTOR_HINT:
						setMouseCursor( Mouse::DEFECTOR );
						break;
#ifdef ALLOW_SURRENDER
					case GameMessage::MSG_PICK_UP_PRISONER_HINT:
						setMouseCursor( Mouse::PICK_UP_PRISONER );
						break;
#endif
					case GameMessage::MSG_CAPTUREBUILDING_HINT:
						setMouseCursor( Mouse::CAPTUREBUILDING );
						break;
					case GameMessage::MSG_HACK_HINT:
						setMouseCursor( Mouse::HACK );
						break;
					case GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT:
						setMouseCursor( Mouse::GENERIC_INVALID );
						break;
					case GameMessage::MSG_SET_RALLY_POINT_HINT:
						if ( !drawSelectable )
							setMouseCursor( Mouse::SET_RALLY_POINT );
						else
							setMouseCursor( Mouse::SELECTING );
						break;
					case GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION_HINT:
						setMouseCursor( Mouse::PARTICLE_UPLINK_CANNON );
						break;
					case GameMessage::MSG_DO_SALVAGE_HINT:
						setMouseCursor( Mouse::MOVETO );
						break;
					case GameMessage::MSG_DO_INVALID_HINT:
						setMouseCursor( Mouse::GENERIC_INVALID );
						break;
				}
			}
			break;
		case MOUSEMODE_BUILD_PLACE:
			{
				if (underWindow)
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				//
				// what is under the cursor does not matter while a structure is on it - whether the
				// spot can be built on does, and handleBuildPlacements already worked that out for
				// the tint on the ghost.  Anything the hint said would have left the pointer with no
				// art at all: see placementCursor.
				//
				setMouseCursor( placementCursor( m_placementLegal ) );
			}
			break;
		case MOUSEMODE_GUI_COMMAND:
			{
				if (underWindow)
				{
					setMouseCursor(Mouse::ARROW);
					return;
				}
				// set the mouse cursor for commands that need a targeting or to normal with no command
				if( m_pendingGUICommand )
				{
					if( m_pendingGUICommand->isContextCommand() || 
							m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER ||
							m_pendingGUICommand->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT )
					{
						//Here is the hook for when we are in a context sensitive command mode. We can
						//either do the specified command mode command or nothing! Whether or not the 
						//command is valid or not was determined in evaluateContextCommand which is
						//called first, and posts the appropriate message.
						AsciiString cursorName;	// empty by default
						switch( t )
						{
							case GameMessage::MSG_VALID_GUICOMMAND_HINT:
								cursorName = m_pendingGUICommand->getCursorName();
								break;
							case GameMessage::MSG_INVALID_GUICOMMAND_HINT:
							default:
								cursorName = m_pendingGUICommand->getInvalidCursorName();
								break;
						}

						Int index = TheMouse->getCursorIndex(cursorName);
						if( index != Mouse::INVALID_MOUSE_CURSOR )
						{
							setMouseCursor( (Mouse::MouseCursor)index );
						}
						else
						{
							setMouseCursor( Mouse::CROSS );
						}
						setRadiusCursor(m_pendingGUICommand->getRadiusCursorType(), //*****************************************************************
														m_pendingGUICommand->getSpecialPowerTemplate(),
														m_pendingGUICommand->getWeaponSlot());
					}
					else if( BitTest( m_pendingGUICommand->getOptions(), COMMAND_OPTION_NEED_TARGET ) )
					{
						Int index = TheMouse->getCursorIndex(m_pendingGUICommand->getCursorName());
						if (index != Mouse::INVALID_MOUSE_CURSOR)
							setMouseCursor( (Mouse::MouseCursor)index );
						else
							setMouseCursor( Mouse::CROSS );
						setRadiusCursor(m_pendingGUICommand->getRadiusCursorType(), //*****************************************************************
														m_pendingGUICommand->getSpecialPowerTemplate(),
														m_pendingGUICommand->getWeaponSlot());
					}
					else
					{
						setRadiusCursorNone();
					}
				}
			}
			break;
	}
}

//-------------------------------------------------------------------------------------------------
/** Force fire is the attack key armed, or Ctrl held the way the game as shipped force fired. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isForceFireOn( void ) const
{
	return m_forceAttackArmed || m_forceAttackMode;
}

//-------------------------------------------------------------------------------------------------
/** Shift on top of the Force Attack hold turns its left drag from the circle into the attack line.
	* An armed move, attack move or guard key keeps its own line. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isForceLineHeld( void ) const
{
	return m_forceAttackMode && !isLineOrderArmed() && TheKeyboard->isShift();
}

//-------------------------------------------------------------------------------------------------
Bool InGameUI::wantsDragLine( void ) const
{
	return isLineOrderArmed() || isForceLineHeld() || isMoveLineHeld();
}

//-------------------------------------------------------------------------------------------------
Bool InGameUI::wantsDragCircle( void ) const
{
	return isForceFireOn() && !isLineOrderArmed() && !isForceLineHeld() && m_selectCount > 0
		&& getGUICommand() == NULL && m_pendingPlaceType == NULL;
}

//-------------------------------------------------------------------------------------------------
/** An armed key's line (move, attack move, guard), the attack key's and Ctrl's circle and Ctrl+Shift's
	* attack line are drawn with the left button in every mouse, the button their clicks are aimed with;
	* the right one pans then, and its click puts the key down.  Shift's move line is a plain move and is
	* drawn with the button a click on the ground moves with: the left in the default Classic mouse, the
	* right under the Alternate Mouse Setup and the Reforged mouse, where the left drag stays the box. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isOrderDragButton( Bool rightButton ) const
{
	if( isOrderKeyArmed() || m_forceAttackMode )
		return !rightButton;
	return rightButton ? TheGlobalData->rightButtonOrders() : TheGlobalData->leftButtonOrders();
}

//-------------------------------------------------------------------------------------------------
/** Shift with no key armed draws the move line, the one V draws, when something selected of yours
	* can walk it.  With nothing that walks selected there is no line to draw and shift's drag stays
	* the box that adds to the selection.  Ctrl and Alt on top keep their own meanings, and a GUI
	* command or a structure on the cursor (shift drags a row of them) owns the drag. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isMoveLineHeld( void ) const
{
	if( !TheKeyboard->isShift() || TheKeyboard->isCtrl() || TheKeyboard->isAlt() )
		return FALSE;
	if( !TheGlobalData->m_formationDrag || isOrderKeyArmed() || m_forceAttackMode
			|| getGUICommand() != NULL || m_pendingPlaceType != NULL )
		return FALSE;

	for( DrawableList::const_iterator it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( obj && obj->isLocallyControlled() && !obj->isKindOf( KINDOF_IMMOBILE ) && obj->getAIUpdateInterface() != NULL )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/// Get drawable ID under cursor
//-------------------------------------------------------------------------------------------------
DrawableID InGameUI::getMousedOverDrawableID( void ) const
{

	return m_mousedOverDrawableID;

}

//-------------------------------------------------------------------------------------------------
/// set right-click scroll mode
//-------------------------------------------------------------------------------------------------
void InGameUI::setScrolling( Bool isScrolling, Bool moveCursor )
{
	if (m_isScrolling == isScrolling)
	{
		return;
	}

	if (isScrolling)
	{
		TheMouse->capture();
		if (moveCursor)
			setMouseCursor( Mouse::SCROLL );

		// break any camera locks
		TheTacticalView->setCameraLock( INVALID_ID );
		TheTacticalView->setCameraLockDrawable( NULL );
	}
	else
	{
		if (moveCursor)
			setMouseCursor( Mouse::ARROW );
		TheMouse->releaseCapture();
	}

	m_isScrolling = isScrolling;

}

//-------------------------------------------------------------------------------------------------
/// are we scrolling?
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isScrolling( void )
{
	return m_isScrolling;
}

//-------------------------------------------------------------------------------------------------
/// set drag select mode
//-------------------------------------------------------------------------------------------------
void InGameUI::setSelecting( Bool isSelecting )
{
	if (m_isSelecting == isSelecting)
	{
		return;
	}

	//setMouseCursor( Mouse::SELECTING );
	m_isSelecting = isSelecting;
}

//-------------------------------------------------------------------------------------------------
/// are we selecting?
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isSelecting( void )
{
	return m_isSelecting;
}

//-------------------------------------------------------------------------------------------------
/// get scroll amount
//-------------------------------------------------------------------------------------------------
void InGameUI::setScrollAmount( Coord2D amt )
{
	m_scrollAmt = amt;
}

//-------------------------------------------------------------------------------------------------
/// get scroll amount
//-------------------------------------------------------------------------------------------------
Coord2D InGameUI::getScrollAmount( void )
{
	return m_scrollAmt;
}

//-------------------------------------------------------------------------------------------------
/** Like the building "placement" mode, clicking on some buttons in the UI require us to
	* provide additional data by clicking on a target object/location in the world.  This
	* is where we enable that "mode" so that we can get the additional data needed for a
	* command from the user */
//-------------------------------------------------------------------------------------------------
void InGameUI::setGUICommand( const CommandButton *command )
{
	if (TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
		return;

	// an armed command and an armed smoke signal both want the next click: the newer wins
	if( command != NULL )
		disarmSignal();

	// EA's guard buttons, and their hotkey, arm the guard key with the button's own mode instead of
	// waiting as a pending command, so a button press and the key click and drag the same way
	if( command && ( command->getCommandType() == GUI_COMMAND_GUARD ||
									 command->getCommandType() == GUI_COMMAND_GUARD_WITHOUT_PURSUIT ||
									 command->getCommandType() == GUI_COMMAND_GUARD_FLYING_UNITS_ONLY ) )
	{
		const GuardMode mode = command->getCommandType() == GUI_COMMAND_GUARD ? GUARDMODE_NORMAL
			: command->getCommandType() == GUI_COMMAND_GUARD_WITHOUT_PURSUIT ? GUARDMODE_GUARD_WITHOUT_PURSUIT
			: GUARDMODE_GUARD_FLYING_UNITS_ONLY;
		setGUICommand( NULL );
		clearAttackMoveToMode();
		m_guardArmed = TRUE;
		m_guardArmedMode = mode;
		return;
	}

	// sanity
	if( command )
	{

		if( BitTest( command->getOptions(), COMMAND_OPTION_NEED_TARGET ) == FALSE )
		{

			DEBUG_ASSERTCRASH( 0, ("setGUICommand: Command '%s' does not need additional user interaction\n",	
														command->getName().str()) );
			m_pendingGUICommand = NULL;
			m_mouseMode = MOUSEMODE_DEFAULT;
			return;

		}  // end if

		m_mouseMode = MOUSEMODE_GUI_COMMAND;

	}  // end if
	else
	{
		m_mouseMode = MOUSEMODE_DEFAULT;
	}

	// set the command
	m_pendingGUICommand = command;

	// set the mouse cursor for commands that need a targeting or to normal with no command
	if( command && BitTest( command->getOptions(), COMMAND_OPTION_NEED_TARGET ) && !command->isContextCommand() )
	{
		setMouseCursor( Mouse::ARROW );// This occurs on the mouse-up of a panel button, so make an arrow
		// the mouseoverhint code will take care of the cursor context, once the mouse leaves the panel
		// but we will set the radius cursor here, so you can see it bleeding out from beneath the panel

		setRadiusCursor(command->getRadiusCursorType(), //*****************************************************************
										command->getSpecialPowerTemplate(),
										command->getWeaponSlot());
	}
	else
	{
		if (TheMouse)
		{
			setMouseCursor( Mouse::ARROW );
		}
		setRadiusCursorNone();
	}

	m_mouseModeCursor = TheMouse->getMouseCursor();

}  // end setGUICommand

//-------------------------------------------------------------------------------------------------
/** Get the pending gui command */
//-------------------------------------------------------------------------------------------------
const CommandButton *InGameUI::getGUICommand( void ) const
{

	return m_pendingGUICommand;

} 

//-------------------------------------------------------------------------------------------------
/** Destroy any drawables we have in our placement icon array and set to NULL */
//-------------------------------------------------------------------------------------------------
void InGameUI::destroyPlacementIcons( void )
{
	Int i;

	for( i = 0; i < TheGlobalData->m_maxLineBuildObjects; ++i )
	{

		if( m_placeIcon[ i ] ) 
		{
			TheTerrainVisual->removeFactionBibDrawable(m_placeIcon[ i ]);
			TheGameClient->destroyDrawable( m_placeIcon[ i ] );
		}
		m_placeIcon[ i ] = NULL;

	}  // end for i
	TheTerrainVisual->removeAllBibs();

}  // end destroyPlacementIcons

//-------------------------------------------------------------------------------------------------
/** User has clicked on a built item that requires placement in the world.  We will 
	* record what that thing is so that the we can catch the next click in the world
	* and try to place the object there */
//-------------------------------------------------------------------------------------------------
void InGameUI::placeBuildAvailable( const ThingTemplate *build, Drawable *buildDrawable )
{
	//
	// Each placement starts packed. The wheel opens the row while its line is being drawn, and that
	// gap belongs to the line, not to the next building.
	//
	m_placementRowGap = 0.0f;
	m_placementRowGapWheel = 0.0f;

	// if building something, no radius cursor, thankew
	if (build != NULL)
		setRadiusCursorNone();

	// a structure on the cursor and an armed smoke signal both want the next left click: the newer wins
	if( build != NULL )
		disarmSignal();

	//
	// if we're setting another place available, but we're somehow already in the placement
	// mode, get out of it before we start a new one
	//
	if( m_pendingPlaceType != NULL && build != NULL )
		placeBuildAvailable( NULL, NULL );

	//
	// The wheeled/dragged heading is carried from one placement to the next so a row of walls or
	// bunkers goes down facing the same way. It belongs to that one structure though: turning a
	// bunker used to leave every later building - a supply centre, a war factory - wearing the same
	// offset off its own designed view angle, which reads as the building coming out backwards. A
	// different type starts square again.
	//
	if( build != NULL && build != m_placeAngleType )
	{
		m_placeAngleOffset = 0.0f;
		m_placeAngleType = build;
	}

	//
	// keep a record of what we are trying to place, if we are already trying to
	// place something, it is overwritten
	//
	m_pendingPlaceType = build;

	//Keep the prev pending place for left click deselection prevention in alternate mouse mode.
	//We want to keep our dozer selected after initiating construction.
	setPreventLeftClickDeselectionInAlternateMouseModeForOneClick( m_pendingPlaceSourceObjectID != INVALID_ID );
	m_pendingPlaceSourceObjectID = INVALID_ID;

	Object *sourceObject = NULL;
	if( buildDrawable )
		sourceObject = buildDrawable->getObject();
	if( sourceObject )
		m_pendingPlaceSourceObjectID = sourceObject->getID();

	//
	// hack, change our cursor to at least something different ... also note that it's
	// possible to not have the mouse yet, as some UI systems as part of initialization
	// make sure that there isn't anything valid for to "place build"
	//
	if( TheMouse )
	{

		if( build )
		{
			m_mouseMode = MOUSEMODE_BUILD_PLACE;
			m_placementLegal = TRUE;
			m_placementNudge.zero();
			m_mouseModeCursor = placementCursor( TRUE );

			Drawable *draw;

			// capture the mouse for our window, windows is lame and changes it if we don't
			TheMouse->capture();

			// hack for changing cursor
			setMouseCursor( (Mouse::MouseCursor)m_mouseModeCursor );

			// deselect all drawables, otherwise they move to the place we click
			///@ todo when message stream order more formalized eliminate this
//			TheInGameUI->deselectAllDrawables();

			// create a drawble of what we are building to be "attached" at the cursor
			draw = TheThingFactory->newDrawable( build, DRAWABLE_STATUS_NO_STATE_PARTICLES );
			if (sourceObject)
			{
				if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
					draw->setIndicatorColor(sourceObject->getControllingPlayer()->getPlayerNightColor());
				else
					draw->setIndicatorColor(sourceObject->getControllingPlayer()->getPlayerColor());
			}
			DEBUG_ASSERTCRASH( draw, ("Unable to create icon at cursor for placement '%s'\n",
												 build->getName().str()) );

			//
			// set the initial angle of the free floating building to the property from INI
			// we have this so we can have the "cool" face the user until they click and
			// pick an actual direction for placement
			//
			Real angle = build->getPlacementViewAngle();

			// don't forget to take into account the current view angle
			// angle += TheTacticalView->getAngle();	Don't do this - makes odd angled building placements.  jba.

			// carry over whatever heading the player wheeled to last time, so a row of walls or
			// bunkers can be laid down all facing the same way (see rotatePendingPlacement)
			angle = normalizeAngle( angle + m_placeAngleOffset );

			// set the angle in the icon we just created
			draw->setOrientation( angle );

			// set the build icon attached to the cursor to be "see-thru"
			dressPlacementPreview( draw );

			// set the "icon" in the icon array at the first index
			DEBUG_ASSERTCRASH( m_placeIcon[ 0 ] == NULL, ("placeBuildAvailable, build icon array is not empty!") );
			m_placeIcon[ 0 ] = draw;	

		}  // end if
		else
		{
			if (m_mouseMode == MOUSEMODE_BUILD_PLACE)
			{
				m_mouseMode = MOUSEMODE_DEFAULT;
				m_mouseModeCursor = Mouse::ARROW;
			}

			TheMouse->releaseCapture();
			setMouseCursor( Mouse::ARROW );
			setPlacementStart( NULL );

			// if we have a place icons destroy them
			destroyPlacementIcons();

			if( sourceObject )
			{
				ProductionUpdateInterface *puInterface = sourceObject->getProductionUpdateInterface();
				if( puInterface )
				{
					//Clear the special power mode for construction if we set it. Actually call it everytime
					//rather than checking if it's set before clearing (cheaper).
					puInterface->setSpecialPowerConstructionCommandButton( NULL );
				}
			}

		}  // end else

	}  // end if

}  // end placeBuildAvailable

//-------------------------------------------------------------------------------------------------
/** Return the thing we're attempting to place */
//-------------------------------------------------------------------------------------------------
const ThingTemplate *InGameUI::getPendingPlaceType( void )
{
	return m_pendingPlaceType;
}

//-------------------------------------------------------------------------------------------------
/** The see-thru structure the cursor is carrying, so the renderer can hang decoration off it -
	* the exit line, for one.  NULL whenever we are not in placement mode. */
//-------------------------------------------------------------------------------------------------
const Drawable *InGameUI::getPendingPlaceDrawable( void ) const
{
	if( m_pendingPlaceType == NULL )
		return NULL;

	return m_placeIcon[ 0 ];
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const ObjectID InGameUI::getPendingPlaceSourceObjectID( void )
{

	return m_pendingPlaceSourceObjectID;

}  // end getPendingPlaceSourceObjectID

//-------------------------------------------------------------------------------------------------
/** Start the angle selection interface for selecting building angles when placing them */
//-------------------------------------------------------------------------------------------------
void InGameUI::setPlacementStart( const ICoord2D *start )
{

	// if we have a start point we turn "on" the interface, otherwise we turn it "off"
	if( start )
	{

		m_placeAnchorStart = *start;
		m_placeAnchorEnd = *start;
		m_placeAnchorInProgress = TRUE;

	}  // end if
	else
		m_placeAnchorInProgress = FALSE;

}  // end setPlacementStart

//-------------------------------------------------------------------------------------------------
/** Set the end anchor for the angle build interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::setPlacementEnd( const ICoord2D *end )
{

	if( end )
		m_placeAnchorEnd = *end;

}  // end setPlacementEnd

//-------------------------------------------------------------------------------------------------
/** Is the angle selection interface for placing building at angles up? */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isPlacementAnchored( void )
{

	return m_placeAnchorInProgress;

}  // end isPlacementAnchored

//-------------------------------------------------------------------------------------------------
/** Get the start and end anchor points for the building angle selection interface */
//-------------------------------------------------------------------------------------------------
void InGameUI::getPlacementPoints( ICoord2D *start, ICoord2D *end )
{

	if( start )
		*start = m_placeAnchorStart;
	if( end )
		*end = m_placeAnchorEnd;

}  // end getPlacementPoints

//-------------------------------------------------------------------------------------------------
/** Return the angle of the drawable at the cursor if any */
//-------------------------------------------------------------------------------------------------
Real InGameUI::getPlacementAngle( void )
{

	if( m_placeIcon[ 0 ] )
		return m_placeIcon[ 0 ]->getOrientation();

	return 0.0f;

}  // end getPlacementAngle

//-------------------------------------------------------------------------------------------------
/** The heading a drag from 'start' to 'end' (both in screen pixels) aims a structure at.  The two
	* points are projected onto the terrain first, so the answer is a world heading and not a screen
	* one: dragging "up" the screen faces the building away from the camera whatever the camera is
	* turned to. */
//-------------------------------------------------------------------------------------------------
Real InGameUI::computePlacementAngle( const ICoord2D *start, const ICoord2D *end )
{
	Coord3D worldStart, worldEnd;

	TheTacticalView->screenToTerrain( start, &worldStart );
	TheTacticalView->screenToTerrain( end, &worldEnd );

	Coord2D v;
	v.x = worldEnd.x - worldStart.x;
	v.y = worldEnd.y - worldStart.y;

	Real angle = v.toAngle();

	// optional 45 degree snap (BuildSnapTo45 in Options.ini) - lines walls and
	// defenses up with the base instead of leaving them at whatever the drag produced.
	if( TheGlobalData->m_snapBuildPlacementTo45 )
		angle = snapAngleTo45( angle );

	return angle;

}  // end computePlacementAngle

//-------------------------------------------------------------------------------------------------
/** Experimental GridBuildPlacement (Options.ini): put the structure on the pathfinder's build grid
	* instead of wherever the cursor happened to be to the pixel.  Everything the pathfinder does is
	* in 10-unit cells (PATHFIND_CELL_SIZE), and a structure dropped a couple of units off that grid
	* blocks a strip of a cell it does not fill, which is what leaves the gaps you cannot walk a
	* soldier through between two buildings that look flush.  Snapping the footprint's edges to the
	* grid lines makes neighbours share an edge exactly, and a row of them come out straight.
	*
	* The footprint is the template's own geometry, turned by the heading it is being placed at: a
	* box is major along its facing and minor across it, and anything round is its bounding circle.
	* At 45 degrees the axis-aligned extents grow, which is right - that is the ground it covers. */
//-------------------------------------------------------------------------------------------------
static void placementHalfSizes( const ThingTemplate *what, Real *major, Real *minor )
{
	const GeometryInfo &geom = what->getTemplateGeometryInfo();
	*major = geom.getMajorRadius();
	*minor = geom.getMinorRadius();
	if( geom.getGeomType() != GEOMETRY_BOX )
		*major = *minor = geom.getBoundingCircleRadius();

	// The ground a structure really takes is not its collision box: BuildAssistant's own clearance
	// check grows both radii by the factory bib (isLocationClearOfObjects' myBounds), and the bib is
	// the concrete apron you can see under it.  Snapping the bare box left that apron hanging off
	// the grid by the bib's width, which is what makes a placed building look like it did not
	// quite sit down on its squares.
	*major += what->getFactoryExtraBibWidth();
	*minor += what->getFactoryExtraBibWidth();
}

static void placementHalfExtents( const ThingTemplate *what, Real angle, Real *halfX, Real *halfY )
{
	Real major, minor;
	placementHalfSizes( what, &major, &minor );

	const Real c = (Real)fabs( Cos( angle ) );
	const Real sn = (Real)fabs( Sin( angle ) );

	*halfX = major * c + minor * sn;
	*halfY = major * sn + minor * c;
}

void InGameUI::snapPlacementToGrid( Coord3D *world, const ThingTemplate *what, Real angle ) const
{
	if( world == NULL || what == NULL || gridPlacementOn() == FALSE )
		return;

	Real halfX, halfY;
	placementHalfExtents( what, angle, &halfX, &halfY );

	world->x = snapPlacementAxis( world->x, halfX );
	world->y = snapPlacementAxis( world->y, halfY );

}  // end snapPlacementToGrid

//-------------------------------------------------------------------------------------------------
/** Ctrl held while placing turns GridBuildPlacement the other way for as long as it is down: off,
	* it snaps; on, it places freely.  The ghost, the lines and the order the click sends all ask
	* this, so they cannot disagree.  Classic too: the snap is not one of 1.04's rules, it is an
	* option on top of them, and the key that turns it is no different there. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::gridPlacementOn( void ) const
{
	const Bool flip = TheKeyboard && TheKeyboard->isCtrl();
	return ( TheGlobalData->m_gridBuildPlacement != FALSE ) != flip;
}

//-------------------------------------------------------------------------------------------------
/** Shift or alt held on the drag: a wall already tiles from any drag, so it is left to do that.
	* Everything that asks this wants to know whether the drag lays pieces instead of aiming one, and
	* that is the same answer for a row and a grid; computePlacementRow is where the two part. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::placesRow( void )
{
	return m_pendingPlaceType != NULL && TheKeyboard &&
				 ( TheKeyboard->isShift() || TheKeyboard->isAlt() ) &&
				 !TheBuildAssistant->isLineBuildTemplate( m_pendingPlaceType );
}

//-------------------------------------------------------------------------------------------------
/** Alt makes it a grid, and wins over shift when both are down: a grid dragged along the
	* structure's own line is already a row.  Let go of alt with shift still held and the same drag
	* is the shift row again; let go of both and it aims one structure.  Asked only once placesRow
	* has said yes. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::placesGrid( void ) const
{
	return TheKeyboard->isAlt();
}

//-------------------------------------------------------------------------------------------------
/** One grid square a notch, added to the packed step.  A notch toward the user closes it back,
	* and it stops at the buildings touching: tighter than that is two structures on one footprint.
	* Fractions pile up, so a touchpad swipe that arrives as halves still moves the row. */
//-------------------------------------------------------------------------------------------------
void InGameUI::adjustPlacementRowGap( Real spin )
{
	const Real cell = (Real)PLACEMENT_CELL;
	const Real cap = 20.0f * cell;

	m_placementRowGapWheel += spin;
	const Int steps = (Int)m_placementRowGapWheel;
	if( steps == 0 )
		return;

	m_placementRowGapWheel -= (Real)steps;
	m_placementRowGap += (Real)steps * cell;

	if( m_placementRowGap < 0.0f )
	{
		m_placementRowGap = 0.0f;
		m_placementRowGapWheel = 0.0f;
	}
	else if( m_placementRowGap > cap )
	{
		m_placementRowGap = cap;
		m_placementRowGapWheel = 0.0f;
	}

}  // end adjustPlacementRowGap

static const Real GUARD_PICK_RADIUS_MIN = 50.0f;
static const Real GUARD_PICK_STEP = 1.15f;		///< what one notch of the wheel multiplies the radius by
// ponytail: under logic's GUARD_RADIUS_MAX of 800 because one ground decal draws at most 104 terrain
// cells across (W3DProjectedShadow's index buffer clip), and a ring past about 515 came out squared
// off.  Raise both together if the decal buffers grow
static const Real GUARD_PICK_RADIUS_MAX = 500.0f;

//-------------------------------------------------------------------------------------------------
/** The range the guard marker is drawn at before the wheel touches it, the one setRadiusCursor
	* takes: the first selected unit's own guard range. */
//-------------------------------------------------------------------------------------------------
static Real guardMarkerBaseRadius( Drawable *draw )
{
	const Object *obj = draw ? draw->getObject() : NULL;
	return obj ? AIGuardMachine::getStdGuardRange( obj ) : 0.0f;
}

//-------------------------------------------------------------------------------------------------
Real InGameUI::getArmedGuardRadius( void )
{
	if( !m_guardArmed || m_guardRadiusScale == 1.0f )
		return 0.0f;
	return guardMarkerBaseRadius( getFirstSelectedDrawable() ) * m_guardRadiusScale;
}

//-------------------------------------------------------------------------------------------------
/** A notch away from the player grows the circle by a fixed share, so the wheel feels the same on a
	* small circle as on a big one.  The scale stops where the radius does, so the wheel never winds
	* up travel that has to be undone first. */
//-------------------------------------------------------------------------------------------------
void InGameUI::adjustArmedGuardRadius( Real notches )
{
	const Real base = guardMarkerBaseRadius( getFirstSelectedDrawable() );
	if( base <= 0.0f )
		return;

	// half the unwheeled size at the least: the ring's inside edge is then still clear of the shield,
	// which keeps its own size
	m_guardRadiusScale *= (Real)pow( GUARD_PICK_STEP, notches );
	m_guardRadiusScale = min( max( m_guardRadiusScale, max( 0.5f, GUARD_PICK_RADIUS_MIN / base ) ), max( 1.0f, GUARD_PICK_RADIUS_MAX / base ) );

	// the ring is made at its size, so it is made again
	if( m_curRcType == RADIUSCURSOR_GUARD_AREA )
	{
		setRadiusCursorNone();
		setRadiusCursor( RADIUSCURSOR_GUARD_AREA, NULL, PRIMARY_WEAPON );
	}
}

//-------------------------------------------------------------------------------------------------
/** The first unit of the player's in the selection with a stance to read, for the stance key's face
	* and for which stance a press switches the selection to. */
//-------------------------------------------------------------------------------------------------
static Bool isSelectionAggressive( void )
{
	const DrawableList *selected = TheInGameUI->getAllSelectedLocalDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( obj && obj->getAI() )
			return obj->getAI()->hasAggressiveStance();
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void InGameUI::pressOrderKey( Int key )
{
	if( key == ORDER_KEY_STANCE && getSelectCount() > 0 )
	{
		GameMessage *stance = TheMessageStream->appendMessage( GameMessage::MSG_SET_STANCE );
		stance->appendIntegerArgument( isSelectionAggressive() ? 0 : 1 );
	}
}

//-------------------------------------------------------------------------------------------------
/** Every piece faces 'angle', the heading on the ghost before the drag began: the drag is spent on
	* the row, so it cannot aim as well.  The row stops where the money does, which is what the logic
	* would do to the orders past it anyway (canMakeUnit per MSG_DOZER_CONSTRUCT); the player sees the
	* row that will go up.  Legality is not asked here - the ghost and the click each ask it per piece. */
//-------------------------------------------------------------------------------------------------
void InGameUI::computePlacementRow( const ThingTemplate *what, Real angle, const Coord3D *start,
																		const Coord3D *end, std::vector<Coord3D> *positions ) const
{
	//
	// A factory's door needs its lane clear of the next structure (isLocationClearOfObjects' exit
	// check), so a row running out of the door or into it leaves the lane between each pair.
	// Half the lane on each half-length: the pair's shared box grows by the whole of it.
	//
	Real major, minor;
	placementHalfSizes( what, &major, &minor );
	const Real halfFacing = major + what->getFactoryExitWidth() * 0.5f;

	Int most = TheGlobalData->m_maxLineBuildObjects;
	Player *player = ThePlayerList->getLocalPlayer();
	const Int cost = what->calcCostToBuild( player );
	if( cost > 0 )
	{
		const Int affordable = (Int)( player->getMoney()->countMoney() / cost );
		if( affordable < most )
			most = affordable;
	}

	// a row is one line of a grid: everything on line 0, and no step to a next one
	Coord2D step;
	Coord2D lineStep;
	lineStep.x = 0.0f;
	lineStep.y = 0.0f;
	Int count;
	Int perLine;
	if( placesGrid() )
	{
		count = placementGrid( end->x - start->x, end->y - start->y, (Real)Cos( angle ),
													 (Real)Sin( angle ), halfFacing, minor, most, &step, &lineStep, &perLine,
													 m_placementRowGap );
	}
	else
	{
		count = placementRow( end->x - start->x, end->y - start->y, (Real)Cos( angle ),
													(Real)Sin( angle ), halfFacing, minor, most, &step,
													m_placementRowGap );
		perLine = count;
	}

	positions->clear();
	for( Int i = 0; i < count; i++ )
	{
		Coord3D pos;
		pos.x = start->x + step.x * ( i % perLine ) + lineStep.x * ( i / perLine );
		pos.y = start->y + step.y * ( i % perLine ) + lineStep.y * ( i / perLine );
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		positions->push_back( pos );
	}

}  // end computePlacementRow

//-------------------------------------------------------------------------------------------------
/** The legality question asked of the spot under the ghost, in one place - the nudge search asks it
	* of every candidate too.  IGNORE_STEALTHED is deliberate: a structure you cannot put down is a
	* place a stealthed unit is standing, and the placement cursor is not a detector.  The click
	* re-asks with FAIL_STEALTHED_WITHOUT_FEEDBACK, which is where that actually gets decided.
	* IGNORE_UNSEEN is the same for the fog: the ghost, its bib, the nudge and the click know only what
	* this player has seen, and the logic's own check turns down an order on a building he has not. */
//-------------------------------------------------------------------------------------------------
UnsignedInt InGameUI::placementCheckOptions( void )
{
	// the trainer's build anywhere: the logic asks no more than this (MSG_DOZER_CONSTRUCT)
	const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( local && local->hasCheat( CHEAT_BUILD_ANYWHERE ) )
		return BuildAssistant::NO_OBJECT_OVERLAP | BuildAssistant::IGNORE_STEALTHED | BuildAssistant::IGNORE_UNSEEN;

	return BuildAssistant::USE_QUICK_PATHFIND |
				 BuildAssistant::TERRAIN_RESTRICTIONS |
				 BuildAssistant::CLEAR_PATH |
				 BuildAssistant::NO_OBJECT_OVERLAP |
				 BuildAssistant::SHROUD_REVEALED |
				 BuildAssistant::IGNORE_STEALTHED |
				 BuildAssistant::IGNORE_UNSEEN;

}  // end placementCheckOptions

//-------------------------------------------------------------------------------------------------
/** The ground a structure of this template would stand on, put down here at this heading: the box
	* isLocationClearOfObjects compares, bib and all. */
//-------------------------------------------------------------------------------------------------
void InGameUI::placementFootprint( const ThingTemplate *what, const Coord3D *world, Real angle,
																	 PlacementBox *footprint )
{
	placementHalfSizes( what, &footprint->halfMajor, &footprint->halfMinor );
	footprint->x = world->x;
	footprint->y = world->y;
	footprint->c = (Real)Cos( angle );
	footprint->s = (Real)Sin( angle );
}

//-------------------------------------------------------------------------------------------------
/** Remember a structure just ordered, so the click after it knows the ground is spoken for.  Round
	* a ring: the oldest goes, which is the one most likely to be standing by now. */
//-------------------------------------------------------------------------------------------------
void InGameUI::recordPendingPlacement( const ThingTemplate *what, const Coord3D *world, Real angle )
{
	if( what == NULL || world == NULL || TheGameLogic == NULL )
		return;

	PendingPlacement *pending = &m_pendingPlacement[ m_pendingPlacementAt ];
	m_pendingPlacementAt = ( m_pendingPlacementAt + 1 ) % PENDING_PLACEMENTS;

	placementFootprint( what, world, angle, &pending->footprint );
	pending->frame = TheGameLogic->getFrame();

	//
	// frame zero is a real frame and zero is how an empty slot is spelt, so an order placed on it
	// is remembered from frame one.  It costs one frame of one order in the first thirtieth of a
	// second of a match, when nothing is built yet.
	//
	if( pending->frame == 0 )
		pending->frame = 1;

}  // end recordPendingPlacement

//-------------------------------------------------------------------------------------------------
/** Would a structure put down here land on one already ordered?  Orders older than
	* PENDING_PLACEMENT_FRAMES are ignored rather than cleared: by then the structure is either
	* standing, where the ordinary check sees it, or the logic refused the order and the ground is
	* free again. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::overlapsPendingPlacement( const Coord3D *world, const ThingTemplate *what,
																				 Real angle ) const
{
	if( what == NULL || world == NULL || TheGameLogic == NULL )
		return FALSE;

	PlacementBox mine;
	placementFootprint( what, world, angle, &mine );

	const UnsignedInt now = TheGameLogic->getFrame();

	for( Int i = 0; i < PENDING_PLACEMENTS; i++ )
	{
		const PendingPlacement *pending = &m_pendingPlacement[ i ];

		if( pending->frame == 0 || now < pending->frame ||
				now - pending->frame > PENDING_PLACEMENT_FRAMES )
			continue;

		if( footprintsOverlap( &mine, &pending->footprint ) )
			return TRUE;
	}

	return FALSE;

}  // end overlapsPendingPlacement

//-------------------------------------------------------------------------------------------------
/** Of every structure it could be pulled against, the one that moves it least wins - the one the
	* click was plainly meant for.  Standing structures come off the partition manager, ordered ones
	* off the pending ring, and one that has since gone up is in both, the same box twice. */
//-------------------------------------------------------------------------------------------------
void InGameUI::snapPlacementToNeighbour( Coord3D *world, const ThingTemplate *what, Real angle ) const
{
	if( !TheGlobalData->m_snapBuildToNeighbour || TheBuildAssistant->isLineBuildTemplate( what ) )
		return;

	const Real largestFootprint = 150.0f;		// half-diagonal of the biggest structure it may lean on

	PlacementBox mine;
	placementFootprint( what, world, angle, &mine );

	Coord2D best;
	best.x = world->x;
	best.y = world->y;
	Real bestMoveSqr = -1.0f;

	std::vector<PlacementBox> neighbours;
	const UnsignedInt now = TheGameLogic->getFrame();
	for( Int i = 0; i < PENDING_PLACEMENTS; i++ )
	{
		const PendingPlacement *pending = &m_pendingPlacement[ i ];
		if( pending->frame != 0 && now >= pending->frame && now - pending->frame <= PENDING_PLACEMENT_FRAMES )
			neighbours.push_back( pending->footprint );
	}

	const Real searchRadius = (Real)sqrt( mine.halfMajor * mine.halfMajor + mine.halfMinor * mine.halfMinor ) +
														(Real)PLACEMENT_FLUSH_REACH + largestFootprint;
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( world, searchRadius, FROM_CENTER_2D, NULL );
	MemoryPoolObjectHolder holder( iter );
	for( Object *obj = iter->first(); obj; obj = iter->next() )
	{
		// a ghost that jumps flush against bare ground says where an unseen building stands
		if( !obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead() ||
				BuildAssistant_isHiddenFromLocalPlayer( obj ) )
			continue;

		PlacementBox standing;
		placementFootprint( obj->getTemplate(), obj->getPosition(), obj->getOrientation(), &standing );
		neighbours.push_back( standing );
	}

	// and one that died in his fog, which he still sees standing in its ghost.  The ghost keeps no
	// template, so its box is the bare geometry, without a factory's bib.
	enum { MAX_GHOSTS = 16 };
	const GhostObject *ghosts[ MAX_GHOSTS ];
	const Int ghostCount = BuildAssistant_findDeadGhostsInFog( world, searchRadius, ghosts, MAX_GHOSTS );
	for( Int g = 0; g < ghostCount; g++ )
	{
		PlacementBox remembered;
		remembered.x = ghosts[ g ]->getParentPosition()->x;
		remembered.y = ghosts[ g ]->getParentPosition()->y;
		remembered.c = (Real)Cos( ghosts[ g ]->getParentAngle() );
		remembered.s = (Real)Sin( ghosts[ g ]->getParentAngle() );
		remembered.halfMajor = ghosts[ g ]->getGeometryMajorRadius();
		remembered.halfMinor = ghosts[ g ]->getGeometryType() == GEOMETRY_BOX ?
													 ghosts[ g ]->getGeometryMinorRadius() : remembered.halfMajor;
		neighbours.push_back( remembered );
	}

	for( size_t i = 0; i < neighbours.size(); i++ )
	{
		PlacementBox moved = mine;
		if( !flushAgainst( &neighbours[ i ], &moved ) )
			continue;

		// flush against this one and into the next is no help: the nudge would throw it off again
		Bool blocked = FALSE;
		for( size_t j = 0; j < neighbours.size() && !blocked; j++ )
			blocked = j != i && footprintsOverlap( &moved, &neighbours[ j ] );
		if( blocked )
			continue;

		const Real moveSqr = ( moved.x - mine.x ) * ( moved.x - mine.x ) + ( moved.y - mine.y ) * ( moved.y - mine.y );
		if( bestMoveSqr < 0.0f || moveSqr < bestMoveSqr )
		{
			bestMoveSqr = moveSqr;
			best.x = moved.x;
			best.y = moved.y;
		}
	}

	world->x = best.x;
	world->y = best.y;
	world->z = TheTerrainLogic->getGroundHeight( best.x, best.y );

}  // end snapPlacementToNeighbour

//-------------------------------------------------------------------------------------------------
void InGameUI::forgetPendingPlacements( void )
{
	for( Int i = 0; i < PENDING_PLACEMENTS; i++ )
		m_pendingPlacement[ i ].frame = 0;

	m_pendingPlacementAt = 0;

}  // end forgetPendingPlacements

//-------------------------------------------------------------------------------------------------
/** NudgeBuildPlacement (Options.ini): the spot under the cursor is blocked - a rock, a neighbour's
	* bib, ground too steep by a hair - so find the nearest one that is not and put the structure
	* there instead of turning it red and leaving the player to hunt for the legal pixel by hand.
	*
	* Nearest is meant literally: candidates come out of placementNudgeOffset in distance order, so
	* the first one that fits is the closest one that fits.  A step is one pathfinder cell, which is
	* also what GridBuildPlacement snaps to, so a nudged structure stays on the same grid as the ones
	* already down.
	*
	* The cost is in the pathfind, not in the ground: EA's own comment on the caller says the check
	* is too expensive to run every frame, and that is the CLEAR_PATH half of it.  So the sweep asks
	* the cheap questions first - shroud, overlap, terrain - across all PLACEMENT_NUDGE_TRIES cells,
	* and only spends a pathfind on a cell that already passed those.
	*
	* ponytail: reaches PLACEMENT_NUDGE_RINGS cells (100 units) and gives up, and gives up sooner
	* after PLACEMENT_NUDGE_PATHFINDS unreachable candidates.  Both are flat numbers; scale them off
	* the structure's own footprint if a supply centre still ends up hunting too far. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::nudgePlacementToLegal( Coord3D *world, const ThingTemplate *what, Real angle,
																			Object *builderObject ) const
{
	if( world == NULL || what == NULL || TheGlobalData->m_nudgeBuildPlacement == FALSE )
		return FALSE;

	// a wall tiles from the two points you dragged between; sliding one end off that line is not help
	if( TheBuildAssistant->isLineBuildTemplate( what ) )
		return FALSE;

	// CLEAR_PATH is the pathfind, and USE_QUICK_PATHFIND only says which one it uses
	const UnsignedInt fullOptions = placementCheckOptions();
	const UnsignedInt groundOptions = fullOptions & ~BuildAssistant::CLEAR_PATH;
	Int pathfinds = 0;

	for( Int i = 0; i < PLACEMENT_NUDGE_TRIES; i++ )
	{
		Real dx, dy;
		placementNudgeOffset( i, (Real)PLACEMENT_CELL, &dx, &dy );

		Coord3D test = *world;
		test.x += dx;
		test.y += dy;
		test.z = TheTerrainLogic->getGroundHeight( test.x, test.y );

		// is there room at all?  cheap, and it throws out all but a handful of the candidates
		if( TheBuildAssistant->isLocationLegalToBuild( &test, what, angle, groundOptions,
																									 builderObject, NULL ) != LBC_OK )
			continue;

		// room a structure you have already ordered is on its way to is not room
		if( overlapsPendingPlacement( &test, what, angle ) )
			continue;

		// there is - can the builder actually walk to it?
		if( TheBuildAssistant->isLocationLegalToBuild( &test, what, angle, fullOptions,
																									 builderObject, NULL ) == LBC_OK )
		{
			*world = test;
			return TRUE;
		}

		//
		// Room the builder cannot reach - across a cliff, behind a wall.  A few of those are
		// normal near the edge of a base; a lot of them means the whole neighbourhood is cut off,
		// and there is no sense pathfinding to every cell of it twice a frame.
		//
		if( ++pathfinds >= PLACEMENT_NUDGE_PATHFINDS )
			break;

	}  // end for i

	return FALSE;

}  // end nudgePlacementToLegal


//-------------------------------------------------------------------------------------------------
/** Aim the structure sitting on the cursor at 'angle', and keep that heading for the placements
	* that follow - the same offset the wheel writes (see rotatePendingPlacement), so a wall aimed by
	* dragging carries on in the direction it was aimed instead of snapping back to the template's own
	* view angle on the next piece. */
//-------------------------------------------------------------------------------------------------
void InGameUI::setPlacementAngle( Real angle )
{
	if( m_pendingPlaceType == NULL )
		return;

	m_placeAngleOffset = normalizeAngle( angle - m_pendingPlaceType->getPlacementViewAngle() );

	for( Int i = 0; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		if( m_placeIcon[ i ] )
			m_placeIcon[ i ]->setOrientation( angle );

}  // end setPlacementAngle

//-------------------------------------------------------------------------------------------------
/** Turn the structure sitting on the cursor by 'steps' eighths of a turn.  The chosen heading is
	* kept for the placements that follow, so a row of walls or a line of bunkers can be laid down
	* facing the same way without re-aiming each one - it is an offset rather than an absolute angle
	* so that each structure still starts from its own designed view angle.  Does nothing while the
	* drag-to-aim anchor is down: that interface recomputes the angle from the drag every frame and
	* would throw this away on the next one. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::rotatePendingPlacement( Int steps )
{
	const Real step = PI / 4.0f;

	if( steps == 0 || m_pendingPlaceType == NULL || m_placeIcon[ 0 ] == NULL || isPlacementAnchored() )
		return FALSE;

	m_placeAngleOffset = normalizeAngle( m_placeAngleOffset + steps * step );

	for( Int i = 0; i < TheGlobalData->m_maxLineBuildObjects; i++ )
		if( m_placeIcon[ i ] )
			m_placeIcon[ i ]->setOrientation( normalizeAngle( m_placeIcon[ i ]->getOrientation() + steps * step ) );

	return TRUE;

}  // end rotatePendingPlacement

//-------------------------------------------------------------------------------------------------
/** Mark given Drawable as "selected". */
//-------------------------------------------------------------------------------------------------
void InGameUI::selectDrawable( Drawable *draw )
{
	// a drawable the tick destroys leaves the selection in this queue too, before it is deleted
	if( LogicThread::defersEffects() )
		LogicThread::noteSelected( draw->getID(), draw->getObject()->getID(), TRUE );	// what the tick reads back (M7)
	LOGIC_AFTER_TICK( [=]() { selectDrawable( draw ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );

	if( draw->isSelected() == FALSE )
	{

		m_frameSelectionChanged = TheGameLogic->getFrame();
		// set the selection in the drawable
		draw->friend_setSelected();

		// add to our selected list
		m_selectedDrawables.push_front( draw );

		// we now have one more selected drawable
		incrementSelectCount(); 


		// evaluate whether our selection consists of exactly one angry mob
		evaluateSoloNexus( draw );

		// the control needs to update its context sensitive display now
		TheControlBar->onDrawableSelected( draw );

	}  // end if

}  // end selectDrawable

//-------------------------------------------------------------------------------------------------
void InGameUI::setDisplayedMaxWarning( Bool selected )
{
	// in order with the selectDrawable the tick queued before it
	LOGIC_AFTER_TICK( [=]() { setDisplayedMaxWarning( selected ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	m_displayedMaxWarning = selected;
}

//-------------------------------------------------------------------------------------------------
/** Clear "selected" status of Drawable. */
//-------------------------------------------------------------------------------------------------
void InGameUI::deselectDrawable( Drawable *draw )
{
	if( LogicThread::defersEffects() )
		LogicThread::noteSelected( draw->getID(), INVALID_ID, FALSE );	// what the tick reads back (M7)
	LOGIC_AFTER_TICK( [=]() { deselectDrawable( draw ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );

	if( draw->isSelected() )
	{
		m_frameSelectionChanged = TheGameLogic->getFrame();
		// clear the selected bit out of the drawable
		draw->friend_clearSelected();

		// find the drawable entry in our list
		DrawableListIt findIt = std::find( m_selectedDrawables.begin(), 
																			 m_selectedDrawables.end(), 
																			 draw );

		// sanity
		DEBUG_ASSERTCRASH( findIt != m_selectedDrawables.end(),
											 ("deselectDrawable: Drawable not found in the selected drawable list '%s'\n",
											 draw->getTemplate()->getName().str()) );

		// remove it from the selected drawable list		
		m_selectedDrawables.erase( findIt );

		// keep out own internal count happy
		decrementSelectCount(); 

		// evaluate whether our selection consists of exactly one angry mob
		evaluateSoloNexus();

		// the control needs to update its context sensitive display now
		TheControlBar->onDrawableDeselected( draw );

	}  // end if

}  // end deselectDrawable

//-------------------------------------------------------------------------------------------------
/** Clear all drawables' "select" status */
//-------------------------------------------------------------------------------------------------
void InGameUI::deselectAllDrawables( void )
{
	if( LogicThread::defersEffects() )
		LogicThread::noteSelectionCleared();	// what the tick reads back (M7)
	LOGIC_AFTER_TICK( [=]() { deselectAllDrawables(); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	const Bool hadSelectedDrawables = !selected->empty();

	// loop through all the selected drawables
	for ( DrawableListCIt it = selected->begin(); it != selected->end(); )
	{

		// get drawable and increment iterator, we will invalidate it as we deselect
		Drawable* draw = *it++;

		// do the deselection
		TheInGameUI->deselectDrawable( draw );

	}  // end while

	// keep our list all tidy
	m_selectedDrawables.clear();

	// a fresh selection is the player's choice, and a unit still underground is not in it
	m_tunnelTripRiders.clear();


	// our selection can no longer consist of exactly one angry mob
	m_soloNexusSelectedDrawableID = INVALID_DRAWABLE_ID;


	// TheSuperHackers @tweak Only send this when something actually was selected. The rest of the
	// spam is dropped in the stream, where a clear followed by a fresh selection is recognised.
	if( hadSelectedDrawables )
	{
		// The message carried one boolean that nothing on the receiving side ever read: the handler
		// deselects the whole group and always did.
		TheMessageStream->appendMessage( GameMessage::MSG_DESTROY_SELECTED_GROUP );
	}
}



//-------------------------------------------------------------------------------------------------
/** Return the list of all the currently selected Drawable pointers. */
//-------------------------------------------------------------------------------------------------
const DrawableList *InGameUI::getAllSelectedDrawables( void ) const
{
	LOGIC_CENSUS( CENSUS_CLIENT_READ );
	return &m_selectedDrawables;
}

//-------------------------------------------------------------------------------------------------
/** A tunnel hides its passengers, and hiding a drawable deselects it. A player who never asked for
	* the tunnel, because a move order took it on its own, lost the unit from the selection on the way
	* through; remember it here so it comes back out selected. A trip ordered by clicking the tunnel
	* still deselects, as it always did. */
//-------------------------------------------------------------------------------------------------
void InGameUI::holdSelectionThroughTunnel( Drawable *draw )
{
	const Object *obj = draw->getObject();
	if( obj == NULL || obj->getAI() == NULL || !obj->getAI()->hasTunnelTrip() )
		return;
	if( obj->getControllingPlayer() != ThePlayerList->getLocalPlayer() )
		return;

	m_tunnelTripRiders.push_back( obj->getID() );
}

//-------------------------------------------------------------------------------------------------
void InGameUI::restoreSelectionAfterTunnel( Drawable *draw )
{
	const Object *obj = draw->getObject();
	if( obj == NULL )
		return;

	std::vector<ObjectID>::iterator rider = std::find( m_tunnelTripRiders.begin(), m_tunnelTripRiders.end(), obj->getID() );
	if( rider == m_tunnelTripRiders.end() )
		return;
	m_tunnelTripRiders.erase( rider );

	// the tunnel took it out of the logic side's group too, so it goes back in the way a click adds it
	GameMessage *groupMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
	groupMsg->appendBooleanArgument( FALSE );
	groupMsg->appendObjectIDArgument( obj->getID() );
	selectDrawable( draw );
}

//-------------------------------------------------------------------------------------------------
/** Return the list of all the currently selected Drawable pointers. */
//-------------------------------------------------------------------------------------------------
const DrawableList *InGameUI::getAllSelectedLocalDrawables( void )
{
	m_selectedLocalDrawables.clear();
	for (DrawableList::const_iterator it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it)
	{
		Drawable *draw = (*it);
		if (draw && draw->getObject() && draw->getObject()->isLocallyControlled())
			m_selectedLocalDrawables.push_back( draw );
	}
	return &m_selectedLocalDrawables;
}

//-------------------------------------------------------------------------------------------------
/** Return poiner to the first selected drawable, if any */
//-------------------------------------------------------------------------------------------------
Drawable *InGameUI::getFirstSelectedDrawable( void )
{
	LOGIC_CENSUS( CENSUS_CLIENT_READ );

	// sanity
	if( m_selectedDrawables.empty() )
		return NULL;  // this is valid, nothing is selected

	return m_selectedDrawables.front();

}  // end getFirstSelectedDrawable

//-------------------------------------------------------------------------------------------------
/** Return true if the selected ID is in the drawable list */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::isDrawableSelected( DrawableID idToCheck ) const
{

	for( DrawableListCIt it = m_selectedDrawables.begin(); it != m_selectedDrawables.end(); ++it ) 
	{

		if( (*it)->getID() == idToCheck )
			return TRUE;

	}  // end for

	return FALSE;

}  // end isDrawableSelected

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::isAnySelectedKindOf( KindOfType kindOf ) const
{
	Drawable *draw;

	for( DrawableListCIt it = m_selectedDrawables.begin();
			 it != m_selectedDrawables.end();
			 ++it )
	{

		/** @todo, it seems like we might want to keep a list of drawable pointers so we
		don't have to do this lookup ... it seems "tightly coupled" to me (CBD) */
		// get the drawable from the ID
		draw = *it;
		if( draw && draw->isKindOf( kindOf ) )
			return TRUE;

	}  // end for, it

	return FALSE;  // no selected objects are of the kind of type

}  // end isAnySelectedKindOf

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::isAllSelectedKindOf( KindOfType kindOf ) const
{
	Drawable *draw;

	for( DrawableListCIt it = m_selectedDrawables.begin();
			 it != m_selectedDrawables.end();
			 ++it )
	{

		/** @todo, it seems like we might want to keep a list of drawable pointers so we
		don't have to do this lookup ... it seems "tightly coupled" to me (CBD) */
		// get the drawable from the ID
		draw = *it;
		if( draw && draw->isKindOf( kindOf ) == FALSE )
			return FALSE;  // not all objects are of the kind of type

	}  // end for, it

	return TRUE;  // all objects have this kindof bit set in them

}  // end isAllSelectedKindOf

//-------------------------------------------------------------------------------------------------
/** Set the input enabled/disabled */
//-------------------------------------------------------------------------------------------------
void InGameUI::setInputEnabled( Bool enable )
{
	LOGIC_AFTER_TICK( [=]() { setInputEnabled( enable ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	if(!enable)
		setSelecting( FALSE );
	
	Bool wasEnabled = m_inputEnabled;

	m_inputEnabled = enable;
	
	if (wasEnabled && !enable)
	{
		clearModifierModes();
	}
}

//-------------------------------------------------------------------------------------------------
/** Forget every "mode" a held key puts us in.
	*
	* Two things can eat the key release that would normally end one of these.  A cinematic disables
	* input, so a ctrl held as it starts and let go during it is never seen released, and the game
	* still thinks you are force-attacking when it ends.  Losing the window's focus does the same
	* thing and is the one a player meets: alt-tab out with alt held - which is how you alt-tab -
	* and you come back in waypoint mode, laying a route with every click, until you press and
	* release alt once more.  WinMain calls this from both focus messages, next to the keyboard's
	* own reset. */
//-------------------------------------------------------------------------------------------------
void InGameUI::clearModifierModes( void )
{
	setForceAttackMode( false );			// CTRL
	setForceMoveMode( false );				// apparently unmapped in current CommandMap.ini
	setWaypointMode( false );					// ALT
	setPreferSelectionMode( false );	// SHIFT
	setCameraRotateLeft( false );			// KP4
	setCameraRotateRight( false );		// KP6
	setCameraZoomIn( false );					// KP8
	setCameraZoomOut( false );				// KP2
}

//-------------------------------------------------------------------------------------------------
/** Drawable is being destroyed, clean up any UI elements associated with it. */
//-------------------------------------------------------------------------------------------------
void InGameUI::disregardDrawable( Drawable *draw )
{

	// make sure drawable is no longer selected
	deselectDrawable( draw );		

}

//-------------------------------------------------------------------------------------------------
/** The IncomeReadout option's figure, "+2,400/min", on a readout plate of the side's own at the money
	* box's right end: over the box's top edge on a bar along the bottom of the screen, under its foot
	* on the sidebar, whose box stands at the top.  Its own plate and not the box's text, because the
	* box is sized for the money: at 1920x1080 the Reforged bar's glass is about 151 pixels and
	* "$ 15,400" already takes 100 of them, so "+2,400/min" beside it never fitted. */
//-------------------------------------------------------------------------------------------------
static Int hudReadoutFrame( void );
static void drawIncomeReadout( void )
{
	if( s_incomeModeShown != INCOME_READOUT_PER_SECOND && s_incomeModeShown != INCOME_READOUT_PER_MINUTE )
		return;
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;
	static const NameKeyType moneyKey = NAMEKEY( "ControlBar.wnd:MoneyDisplay" );
	GameWindow *money = TheWindowManager->winGetWindowFromId( NULL, moneyKey );
	for( GameWindow *up = money; up != NULL; up = up->winGetParent() )
		if( up->winIsHidden() )
			return;
	if( money == NULL )
		return;

	UnicodeString text;
	text.format( TheGameText->fetch( s_incomeModeShown == INCOME_READOUT_PER_MINUTE ? "GUI:IncomeReadoutMinute" : "GUI:IncomeReadoutSecond" ),
							 s_incomeShown );
	text = groupThousands( text );
	if( s_incomeText == NULL )
		s_incomeText = TheDisplayStringManager->newDisplayString();
	GameFont *font = TheFontLibrary->getFont( "Arial", max( REAL_TO_INT( 9.0f * TheDisplay->getHeight() / 1080.0f ), 7 ), TRUE );
	if( s_incomeText->getFont() != font )
		s_incomeText->setFont( font );
	if( s_incomeText->getText().compare( text ) != 0 )
		s_incomeText->setText( text );
	Int textW = 0, textH = 0;
	s_incomeText->getSize( &textW, &textH );

	Int x, y, w, h;
	money->winGetScreenPosition( &x, &y );
	money->winGetSize( &w, &h );
	// clear of the box's frame above it, which stands a third of the glass's height over the glass;
	// the sidebar's box has a 16 pixel gap at 1080 to the radar under it, so a hair under its foot
	const Bool above = y > TheDisplay->getHeight() / 2;
	const Int air = above ? h / 3 : 1;
	const Int tall = textH + 2 * hudReadoutFrame();
	IRegion2D cell;
	cell.lo.x = x;
	cell.hi.x = x + w;
	cell.lo.y = above ? y - air - tall : y + h + air;
	cell.hi.y = cell.lo.y + tall;
	HudReadout_draw( s_incomeText, cell, above ? HUD_READOUT_BOTTOM_RIGHT : HUD_READOUT_TOP_RIGHT,
									 GameMakeColor( 120, 230, 90, 255 ), hudCellStyle().plate );
}

//-------------------------------------------------------------------------------------------------
/** This is called after the UI has been drawn. */
//-------------------------------------------------------------------------------------------------
void InGameUI::postDraw( void )
{
	// drawHudOverlay is NOT called here - it goes on top of everything, see W3DInGameUI::draw
	m_hudCellDrawnPass = m_hudCellPass;
	drawIncomeReadout();
	drawRadarAlarmPointers();
	drawProductionList();
	drawClassicMessages();

	if( m_militarySubtitle )
	{
		ICoord2D pos;
		pos.x = m_militarySubtitle->position.x;
		pos.y = m_militarySubtitle->position.y;
		Color dropColor;
		UnsignedByte r, g, b, a;
		GameGetColorComponents( m_militarySubtitle->color, &r, &g, &b, &a );
		dropColor = GameMakeColor( 0, 0, 0, a );
		for(Int i = 0; i <= m_militarySubtitle->currentDisplayString; i++)
		{
			m_militarySubtitle->displayStrings[i]->draw(pos.x,pos.y, m_militarySubtitle->color,dropColor );
			Int height;
			m_militarySubtitle->displayStrings[i]->getSize(NULL, &height);
			pos.y += height;
		}
		if( m_militarySubtitle->blockDrawn )
		{
			ICoord2D size;
			size.y = m_militarySubtitle->displayStrings[m_militarySubtitle->currentDisplayString]->getFont()->height;
			size.x = size.y * 0.8f;
			TheDisplay->drawFillRect(m_militarySubtitle->blockPos.x, m_militarySubtitle->blockPos.y, size.x, size.y, m_militarySubtitle->color);
		}

	}

	// draw superweapon timers
  // Also responsible for Eva saying "Superweapon is ready for launch"
  //  IMPORTANT: Don't bail out of this block early just because you don't 
  //  want to display the timers -- Eva still needs to be checked
	m_superweaponCells = 0;		// nothing drawn is nothing to click
	m_classicSuperweaponLines.clear();
	m_superweaponTab = noHudTab();
	// the spectator's rack hangs under the superweapon tray when there is one
	Int rackPlateTop = max( hudStripBottom(), TheUIRect().y ) + stripPixels( RACK_NO_TRAY_DROP );
	if (TheGameLogic->getFrame() > 0 )
	{
		//
		// The cameos in the top right, everybody's as EA listed them, bare, each ringed in its
		// owner's colour.  Everything the loop below finds is gathered first
		// and drawn at the end of it, the charged ones and then the soonest ready at the right hand
		// end, so the one that matters next is always in the same place however many are behind it.
		//
		struct ListedSuperweapon
		{
			const Image *cameo;
			Color owner;
			Bool ready;
			Int seconds;
			Int percent;				///< of the charge done, for the sweep
			ObjectID jump;			///< the local player's own building, which a click looks at; INVALID_ID otherwise
			SuperweaponInfo *info;	///< its timer, whose strings the Red Alert style rows write into
			Bool enemy;					///< the local player's enemy's: a red row
			AsciiString power;	///< the special power's name, "GUI:<power>" its label
		};
		std::vector< ListedSuperweapon > listed;

		for (Int i=0; i<MAX_PLAYER_COUNT; ++i)
		{
			for (SuperweaponMap::iterator mapIt = m_superweapons[i].begin(); mapIt != m_superweapons[i].end(); ++mapIt)
			{
				for (SuperweaponList::iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt)
				{
					SuperweaponInfo *info = *listIt;
					DEBUG_ASSERTCRASH(info, ("No superweapon info!"));
					if (info && !info->m_hiddenByScript && !info->m_hiddenByScience)
					{
						Object * owningObject = TheGameLogic->findObjectByID(info->m_id);
						if (owningObject)
						{
							
							// We don't draw our timers until we are finished with construction.
							// It is important that let the SpecialPowerUpdate is add its timer in its contructor,,
							// since the science for it could be added before construction is finished,
							// And thus the timer set to READY before the timer is first drawn, here
							if ( owningObject->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ))
								continue;

							SpecialPowerModuleInterface *module = owningObject->getSpecialPowerModule(info->getSpecialPowerTemplate());
							if (module)
							{
								// found one - draw it
 								Bool isReady = module->isReady();
 								Int readySecs;
 
								// IsReady includes disabledness, so if you have a 0 timer disabled super, you don't want 
 								// the UnsignedInt to wrap around to hundreds of millions of seconds.
 								if( module->getReadyFrame() < TheGameLogic->getFrame() )
									readySecs = 0;
 								else 
 									readySecs = ControlBar_secondsFromFrames( (Real)(module->getReadyFrame() - TheGameLogic->getFrame()) );
								// Yes, integer math.  We can't have float imprecision display 4:01 on a disabled superweapon.
 
                // Only if we actually changed the ready status do we want to play an Eva event.
                if ( isReady && !info->m_evaReadyPlayed )
                {
                  if ( TheGameLogic->getFrame() > 0 )
                  {
                    SpecialPowerType type = module->getSpecialPowerTemplate()->getSpecialPowerType();
                  
                    Player *localPlayer = ThePlayerList->getLocalPlayer();
                  
                    if( type == SPECIAL_PARTICLE_UPLINK_CANNON || type == SUPW_SPECIAL_PARTICLE_UPLINK_CANNON || type == LAZR_SPECIAL_PARTICLE_UPLINK_CANNON )
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_ParticleCannon);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_ParticleCannon);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_ParticleCannon);
                      }
                    }
                    else if( type == SPECIAL_NEUTRON_MISSILE || type == NUKE_SPECIAL_NEUTRON_MISSILE || type == SUPW_SPECIAL_NEUTRON_MISSILE )
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_Nuke);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_Nuke);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_Nuke);
                      }
                    }
                    else if (type == SPECIAL_SCUD_STORM)
                    {
                      if ( localPlayer == owningObject->getControllingPlayer() )
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Own_ScudStorm);
                      }
                      else if ( localPlayer->getRelationship(owningObject->getTeam()) != ENEMIES )
                      {
                        // Note: counting relationship NEUTRAL as ally. Not sure if this makes a difference???
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Ally_ScudStorm);
                      }
                      else
                      {
                        TheEva->setShouldPlay(EVA_SuperweaponReady_Enemy_ScudStorm);
                      }
                    }
                  }
                  info->m_evaReadyPlayed = true;
                }
                else
                {
                  if ( !isReady )
                    info->m_evaReadyPlayed = false; // Reset Eva for next time
                }
              
                // how far through the charge it is: the frame it comes ready on against a whole charge
                const UnsignedInt reload = info->getSpecialPowerTemplate()->getReloadTime();
                const UnsignedInt left = module->getReadyFrame() > TheGameLogic->getFrame()
                                         ? module->getReadyFrame() - TheGameLogic->getFrame() : 0;
                Int percent = 100;
                if( !isReady && reload > 0 )
                  percent = left >= reload ? 0 : REAL_TO_INT( 100.0f * (Real)( reload - left ) / (Real)reload );

                const ObjectID jump = owningObject->getControllingPlayer() == ThePlayerList->getLocalPlayer()
                                      ? owningObject->getID() : INVALID_ID;
                const CommandButton *shortcut = powerButton( info->getSpecialPowerTemplate() );
                const Bool enemy = ThePlayerList->getLocalPlayer()->getRelationship( owningObject->getTeam() ) == ENEMIES;
                ListedSuperweapon line = { shortcut ? shortcut->getButtonImage() : NULL,
                                           clientColor( info->getColor() ), isReady, readySecs, percent, jump,
                                           info, enemy, mapIt->first };
                listed.push_back( line );
                if (info->getSpecialPowerTemplate()->isSharedNSync())
                  break; // Wow, it is almost too easy!
                // This prevents redundant timers for shared powers/superweapons
                // No matter how many specialpowermodules register their timers with me,
                // I will only draw the timer of the first valid one in my list,
                // since they all have the same template, ans they all
                // use the Player::getReadyFrame() functions to stay in sync.
              }
						}
					}
				}
			}
		}

		// a watcher reads every clock on the observer console's tiles and dossier: the top of his screen stays clear
		if( !m_superweaponHiddenByScript && !listed.empty() && !localPlayerWatching() )
		{
			// the local player's own first, then everyone else's, each soonest first: twelve cells and a
			// "+N" must never leave your own nuke behind the tag.  The Classic layout keeps EA's order
			if( TheGlobalData->m_hudLayout != HUD_LAYOUT_RETAIL )
				std::stable_sort( listed.begin(), listed.end(), []( const ListedSuperweapon &a, const ListedSuperweapon &b )
					{
						const Bool ownA = a.jump != INVALID_ID, ownB = b.jump != INVALID_ID;
						if( ownA != ownB )
							return ownA;
						return a.ready != b.ready ? a.ready : a.seconds < b.seconds;
					} );

			const HudCellStyle style = hudCellStyle();
			const HudLayoutRects rects = hudRects();
			if( rects.layout == HUD_LAYOUT_SIDEBAR )
			{
				//
				// The Red Alert style layout writes them as named rows (the wireframe's bottom right): from
				// superweaponRow up, superweaponRowPitch apart, the right edge on the sidebar's left.  Each
				// row is the side's SuperweaponOwn or SuperweaponEnemy plate (SW_ROW_W x SW_ROW_H at 1080,
				// the sidebar art): the power's cameo in its icon well, its name (EA's own "GUI:<power>"
				// countdown label) and the time left or "Ready" in its glass, the charge filling its groove,
				// teal for your own and your allies', red for an enemy's.  Without the plate a flat one with
				// the same seats stands in.  Past SIDEBAR_SUPERWEAPON_ROWS a "+N" tab sticks up from the top
				// row.  A click on a row looks at your own building, as a cell's does.
				//
				// the art's own pixels, which the seats below are read in; the row on screen is superweaponRow
				enum { SW_ROW_W = 288, SW_ROW_H = 40 };
				const Bool more = (Int)listed.size() > SIDEBAR_SUPERWEAPON_ROWS;
				const Int rows = more ? SIDEBAR_SUPERWEAPON_ROWS : (Int)listed.size();
				const Int edge = rects.sidebar.lo.x;
				for( Int each = 0; each < rows; each++ )
				{
					IRegion2D &row = m_superweaponPlace[ each ];
					row.hi.x = edge;
					row.lo.x = edge - rects.superweaponRow.width();
					row.hi.y = rects.superweaponRow.hi.y - each * rects.superweaponRowPitch;
					row.lo.y = row.hi.y - rects.superweaponRow.height();
					m_superweaponJump[ each ] = listed[ each ].jump;
				}
				m_superweaponCells = rows;
				m_superweaponTab = more ? hudTab( 1, edge, m_superweaponPlace[ rows - 1 ].lo.y, -1 ) : noHudTab();

				// a seat on the plate, in its art pixels, on screen
				auto seat = []( const IRegion2D &row, Int x0, Int y0, Int x1, Int y1 )
				{
					const Real sx = row.width() / (Real)SW_ROW_W, sy = row.height() / (Real)SW_ROW_H;
					IRegion2D at;
					at.lo.x = row.lo.x + REAL_TO_INT_FLOOR( x0 * sx + 0.5f );
					at.lo.y = row.lo.y + REAL_TO_INT_FLOOR( y0 * sy + 0.5f );
					at.hi.x = row.lo.x + REAL_TO_INT_FLOOR( x1 * sx + 0.5f );
					at.hi.y = row.lo.y + REAL_TO_INT_FLOOR( y1 * sy + 0.5f );
					return at;
				};
				// icon well 4..46 x 4..36, glass 52..270 x 4..26, groove 52..270 x 29..35, lamp 274..284 x 14..26
				auto iconWell = [ & ]( const IRegion2D &row ) { return seat( row, 4, 4, 46, 36 ); };
				auto textGlass = [ & ]( const IRegion2D &row ) { return seat( row, 52, 4, 270, 26 ); };
				auto groove = [ & ]( const IRegion2D &row ) { return seat( row, 52, 29, 270, 35 ); };

				const Image *ownPlate = Sidebar_image( "SuperweaponOwn" );
				const Image *enemyPlate = Sidebar_image( "SuperweaponEnemy" );
				const Color teal = GameMakeColor( 40, 190, 160, 255 );
				const Color red = GameMakeColor( 214, 72, 52, 255 );
				TheDisplay->beginBatch2D();
				for( Int each = 0; each < rows; each++ )
				{
					const ListedSuperweapon &line = listed[ each ];
					const IRegion2D &row = m_superweaponPlace[ each ];
					const IRegion2D icon = iconWell( row );
					const IRegion2D glass = textGlass( row );
					const IRegion2D charge = groove( row );
					const Image *plate = line.enemy ? enemyPlate : ownPlate;
					if( plate != NULL )
						TheDisplay->drawImage( plate, row.lo.x, row.lo.y, row.hi.x, row.hi.y );
					else
					{
						const Color tint = line.enemy ? red : teal;
						TheDisplay->drawFillRect( row.lo.x, row.lo.y, row.width(), row.height(), GameMakeColor( 6, 9, 11, 200 ) );
						TheDisplay->drawOpenRect( row.lo.x, row.lo.y, row.width(), row.height(), 1, tint );
						TheDisplay->drawOpenRect( icon.lo.x - 1, icon.lo.y - 1, icon.width() + 2, icon.height() + 2, 1, tint );
						TheDisplay->drawFillRect( glass.lo.x, glass.lo.y, glass.width(), glass.height(), GameMakeColor( 0, 0, 0, 120 ) );
						TheDisplay->drawFillRect( charge.lo.x, charge.lo.y, charge.width(), charge.height(), GameMakeColor( 0, 0, 0, 170 ) );
					}
					if( line.cameo != NULL )
						TheDisplay->drawImage( line.cameo, icon.lo.x + 1, icon.lo.y + 1, icon.hi.x - 1, icon.hi.y - 1 );
					// the charge fills the groove, teal for yours and your allies', red for an enemy's, all of it
					// once it is ready
					TheDisplay->drawFillRect( charge.lo.x, charge.lo.y, charge.width() * ( line.ready ? 100 : line.percent ) / 100,
																		charge.height(), line.enemy ? red : teal );
				}
				TheDisplay->endBatch2D();

				// the name and the clock in the glass, bold: the largest of the superweapon font's sizes
				// whose line fits it with 3 rows to spare, found once for a glass height.  A 22 pixel glass
				// at 1080 takes a 19 pixel line, a cap of about 12
				const Int glassH = textGlass( m_superweaponPlace[ 0 ] ).height();
				static Int s_fitGlass = -1;
				static Int s_fitPoint = 4;
				if( s_fitGlass != glassH )
				{
					s_fitGlass = glassH;
					s_fitPoint = 4;
					for( Int point = 40; point > 4; point-- )
					{
						const GameFont *font = TheFontLibrary->getFont( m_superweaponNormalFont,
							TheGlobalLanguageData->adjustFontSize( point ), TRUE );
						if( font != NULL && font->height <= glassH - 3 )
						{
							s_fitPoint = point;
							break;
						}
					}
				}

				const UnicodeString readyText = TheGameText->fetch( "GUI:SidebarSuperweaponReady" );
				const Color drop = GameMakeColor( 0, 0, 0, 255 );
				for( Int each = 0; each < rows; each++ )
				{
					const ListedSuperweapon &line = listed[ each ];
					const IRegion2D &row = m_superweaponPlace[ each ];
					const IRegion2D glass = textGlass( row );
					AsciiString label;
					label.format( "GUI:%s", line.power.str() );
					UnicodeString time;
					if( line.ready )
						time = readyText;
					else
						time.format( u"%d:%2.2d", line.seconds / 60, line.seconds % 60 );
					line.info->setFont( m_superweaponNormalFont, s_fitPoint, TRUE );
					const UnicodeString full = TheGameText->fetch( label );
					DisplayString *name = line.info->getNameString();
					DisplayString *clock = line.info->getTimeString();
					clock->setText( time );

					const Int gap = MAX( 2, glass.height() / 3 );
					const Int textY = glass.lo.y + ( glass.height() - name->getFont()->height ) / 2;
					const Int clockX = glass.hi.x - gap - clock->getWidth();
					// the name gives way to the time, held steady: a quarter of the glass for the clock, more
					// only when the clock itself is wider
					const Int clockRoom = MAX( clock->getWidth(), glass.width() / 4 );
					IRegion2D room;
					room.lo.x = glass.lo.x + gap;
					room.lo.y = glass.lo.y;
					room.hi.x = glass.hi.x - gap - clockRoom - gap;
					room.hi.y = glass.hi.y;

					// a name wider than its room loses letters off its end for an ellipsis, measured in the
					// row's font ("Particle Uplink C…").  Measuring needs the text in the string, and every
					// change rebuilds its quads, so the count kept is remembered per row against the name,
					// the room and the font, and worked out again only when one of them changes
					static UnsignedInt s_nameKey[ SIDEBAR_SUPERWEAPON_ROWS ];		// 0 is never a key: nothing worked out yet
					static Int s_nameChars[ SIDEBAR_SUPERWEAPON_ROWS ];
					const WideChar ellipsis = (WideChar)0x2026;
					const Int roomW = room.hi.x - room.lo.x;
					UnsignedInt key = (UnsignedInt)roomW * 131u + (UnsignedInt)s_fitPoint * 7919u;
					for( Int c = 0; c < full.getLength(); c++ )
						key = key * 31u + (UnsignedInt)full.getCharAt( c );
					key |= 1u;
					if( key != s_nameKey[ each ] )
					{
						s_nameKey[ each ] = key;
						s_nameChars[ each ] = -1;
						name->setText( full );
						if( name->getWidth() > roomW )
						{
							UnicodeString marked = full;
							marked.concat( ellipsis );
							name->setText( marked );
							const Int ellipsisW = name->getWidth() - name->getWidth( full.getLength() );
							Int keep = full.getLength();
							while( keep > 0 && name->getWidth( keep ) + ellipsisW > roomW )
								keep--;
							while( keep > 0 && full.getCharAt( keep - 1 ) == (WideChar)' ' )
								keep--;
							s_nameChars[ each ] = keep;
						}
					}
					UnicodeString shown = full;
					if( s_nameChars[ each ] >= 0 )
					{
						shown.clear();
						for( Int c = 0; c < s_nameChars[ each ]; c++ )
							shown.concat( full.getCharAt( c ) );
						shown.concat( ellipsis );
					}
					name->setText( shown );

					// the name in the row's own colour, the clock white
					const Color ink = line.enemy ? GameMakeColor( 255, 128, 108, 255 ) : GameMakeColor( 110, 232, 204, 255 );
					clock->draw( clockX, textY, GameMakeColor( 240, 244, 246, 255 ), drop );
					name->setClipRegion( &room );
					name->draw( room.lo.x, textY, ink, drop );
				}
				if( more )
				{
					UnicodeString rest;
					rest.format( u"+%d", (Int)listed.size() - rows );
					drawHudTab( style.tabUp, m_superweaponTab, -1, cellString( m_superweaponMoreText, rest ) );
				}
			}
			else if( TheGlobalData->m_hudLayout == HUD_LAYOUT_RETAIL )
			{
				//
				// The Classic layout draws them as EA's game did, the 1.04 code with only its loop over
				// the players read off `listed` (gathered in EA's order, players then powers, and left
				// unsorted for this layout): from SuperweaponCountdownPosition in InGameUI.ini down, a line
				// each, "Name: " right-aligned to that x and "m:ss" after it, in the owner's colour on a
				// black drop.  A charged one goes to the ready font and, with a flash duration, blinks
				// between its owner's colour and SuperweaponCountdownFlashColor; past 82% of the view's
				// height a single "..." stands for the rest.  What EA did not have: a click on a line
				// looks at your own building, as a cell's does; the owner's colour is the one his units
				// show (readableHouseColor), a dark house colour being lost on sand; and the lines are
				// drawn by drawHudOverlay, after the world's health bars, which went over them.
				//
				Int startX = (Int)( m_superweaponPosition.x * TheDisplay->getWidth() );
				// never under the info strip at the top (the clock, the rates, the net box): while it is up the
				// list starts below its foot, as the icon tray does, and from InGameUI.ini's place otherwise
				Int startY = (Int)( m_superweaponPosition.y * TheDisplay->getHeight() );
				if( hudStripBottom() > 0 )
					startY = max( startY, hudStripBottom() + stripPixels( SUPERWEAPON_TRAY_DROP ) );
				const Int bottomMargin = (Int)( (Real)TheTacticalView->getHeight() * 0.82f );
				const Real flashFrames = m_superweaponFlashDuration;
				Int rows = 0;
				for( Int each = 0; each < (Int)listed.size(); each++ )
				{
					const ListedSuperweapon &line = listed[ each ];
					SuperweaponInfo *info = line.info;
					if( startY >= bottomMargin )
					{
						UnicodeString ellipsis;
						ellipsis.format( u"..." );
						info->setText( ellipsis, ellipsis );
						info->setFont( m_superweaponReadyFont, m_superweaponNormalPointSize, m_superweaponNormalBold );
						const ClassicSuperweaponLine more = { info, startX, startY, m_superweaponFlashColor, TRUE };
						m_classicSuperweaponLines.push_back( more );
						break;
					}

					if( line.ready )
						info->setFont( m_superweaponReadyFont, m_superweaponReadyPointSize, m_superweaponReadyBold );
					else
						info->setFont( m_superweaponNormalFont, m_superweaponNormalPointSize, m_superweaponNormalBold );
					AsciiString label;
					label.format( "GUI:%s", line.power.str() );
					UnicodeString name, time;
					name.format( u"%ls: ", TheGameText->fetch( label ).str() );
					time.format( u"%d:%2.2d", line.seconds / 60, line.seconds % 60 );
					info->setText( name, time );

					Color color = readableHouseColor( clientColor( info->getColor() ) );
					if( line.ready && flashFrames != 0.0f )
					{
						if( TheGameLogic->getFrame() >= m_superweaponLastFlashFrame + (Int)flashFrames )
						{
							m_superweaponUsedFlashColor = !m_superweaponUsedFlashColor;
							m_superweaponLastFlashFrame = TheGameLogic->getFrame();
						}
						if( !m_superweaponUsedFlashColor )
							color = m_superweaponFlashColor;
					}
					const ClassicSuperweaponLine drawn = { info, startX, startY, color, FALSE };
					m_classicSuperweaponLines.push_back( drawn );

					const Int h = (Int)info->getHeight();
					if( rows < SUPERWEAPON_LIST_CELLS )
					{
						IRegion2D &box = m_superweaponPlace[ rows ];
						box.lo.x = startX - info->getNameString()->getWidth();
						box.hi.x = startX + info->getTimeString()->getWidth();
						box.lo.y = startY;
						box.hi.y = startY + h;
						m_superweaponJump[ rows ] = line.jump;
						rows++;
					}
					startY += h;
				}
				m_superweaponCells = rows;
				rackPlateTop = startY + rackPx( RACK_TRAY_GAP );
			}
			else
			{
				// rows of SUPERWEAPON_LIST_COLUMNS in one tray hard against the right edge of the HUD's
				// area, a row under a row, the tray's top a little under the info strip's foot whenever it
				// is up (the strip is drawn last and would cover them), the top of the screen otherwise.
				// Two rows at most, the last the short one; past SUPERWEAPON_LIST_CELLS a "+N" tab hangs
				// from the tray
				const Int cameoW = style.cameoW;
				const Int cameoH = style.cameoH;
				// the cells stand the tray's closing edge in from the screen's edge (drawHudTray)
				const Int right = rects.play.hi.x - ( style.superweapon.close != NULL ? style.superweapon.openW : 0 );
				const Int top = max( hudStripBottom(), TheUIRect().y ) + stripPixels( SUPERWEAPON_TRAY_DROP ) + style.superweapon.footH;
				const Bool more = (Int)listed.size() > SUPERWEAPON_LIST_CELLS;
				const Int cameos = more ? SUPERWEAPON_LIST_CELLS : (Int)listed.size();
				IRegion2D place[ SUPERWEAPON_LIST_CELLS ];
				for( Int each = 0; each < cameos; each++ )
				{
					place[ each ].hi.x = right - each % SUPERWEAPON_LIST_COLUMNS * style.cellW - style.marginX;
					place[ each ].lo.y = top + each / SUPERWEAPON_LIST_COLUMNS * style.cellH + style.marginY;
					place[ each ].lo.x = place[ each ].hi.x - cameoW;
					place[ each ].hi.y = place[ each ].lo.y + cameoH;
					m_superweaponPlace[ each ] = hudCellBox( style, place[ each ] );
					m_superweaponJump[ each ] = listed[ each ].jump;
				}
				m_superweaponCells = cameos;
				// under the last row's cell at the edge, below the tray's own bottom edge
				const IRegion2D &lastRow = m_superweaponPlace[ ( cameos - 1 ) / SUPERWEAPON_LIST_COLUMNS * SUPERWEAPON_LIST_COLUMNS ];
				m_superweaponTab = more ? hudTab( 1, right, lastRow.hi.y + style.superweapon.uncoveredH, 1 ) : noHudTab();
				rackPlateTop = max( lastRow.hi.y + style.superweapon.uncoveredH, m_superweaponTab.hi.y ) + rackPx( RACK_TRAY_GAP );

				TheDisplay->beginBatch2D();
				drawHudTray( style.superweapon, m_superweaponPlace, cameos, SUPERWEAPON_LIST_COLUMNS, -1, -1 );
				for( Int each = 0; each < cameos; each++ )
					if( listed[ each ].cameo != NULL )
						TheDisplay->drawImage( listed[ each ].cameo, place[ each ].lo.x, place[ each ].lo.y,
																	 place[ each ].hi.x, place[ each ].hi.y );
				// charging, the scrim swept off as the charge runs; charged, its owner's light rising off the foot
				for( Int each = 0; each < cameos; each++ )
				{
					if( !listed[ each ].ready )
						TheDisplay->drawRemainingRectClock( place[ each ].lo.x, place[ each ].lo.y, cameoW, cameoH,
																								listed[ each ].percent, GameMakeColor( 0, 0, 0, 130 ) );
					else
						drawReadyGlow( style.readyGlow, style.readyShimmer, place[ each ], listed[ each ].owner );
				}
				// a ring in its owner's colour says whose it is, charged or not: a tint on the readout's plate
				// would leave a charged one, which wears no readout, with no owner at all
				for( Int each = 0; each < cameos; each++ )
					drawOwnerRing( place[ each ], listed[ each ].owner, style.marginX );
				TheDisplay->endBatch2D();

				//
				// Each cameo says which power it is and how long it has left: the power's name across its top
				// ("GUI:<power>", its _Short form where the whole one is wider than the picture), and under it
				// the seconds left, large, in the owner's colour as his units show it, lifted where a dark house
				// colour would sink into the picture.  Both stand on a soft shadow, a faint dark ring and a drop
				// to the bottom right, so a light picture or the charge's scrim never swallows them.  A charged
				// one keeps its name and wears no number: zero seconds says nothing the glow does not.  The type
				// follows the cameo, so every HUD Size keeps it in proportion: a 49 pixel cameo at 1080 takes a
				// 16 pixel name line and a 27 pixel clock line.  The strings are the timer's own two, which only
				// the Red Alert style rows use otherwise; the fonts are worked out once per cameo size, and a
				// name is fitted again only when its string holds none in that font or one too wide
				//
				static Int s_fitCameoW = -1;
				static Int s_fitCameoH = -1;
				static GameFont *s_nameFont = NULL;
				static GameFont *s_clockFont = NULL;
				const Int pad = max( cameoH / 16, 1 );
				if( s_fitCameoW != cameoW || s_fitCameoH != cameoH )
				{
					s_fitCameoW = cameoW;
					s_fitCameoH = cameoH;
					// the largest bold size whose line fits, from the line's own height down: a font's line is
					// taller than its size, so the search is a step or three
					auto fitLine = [ this ]( Int line )
					{
						GameFont *fitted = NULL;
						for( Int point = max( line, 5 ); point > 4 && fitted == NULL; point-- )
						{
							GameFont *font = TheFontLibrary->getFont( m_superweaponNormalFont,
								TheGlobalLanguageData->adjustFontSize( point ), TRUE );
							if( font != NULL && font->height <= line )
								fitted = font;
						}
						return fitted;
					};
					s_nameFont = fitLine( cameoH * 16 / 49 );
					s_clockFont = fitLine( cameoH * 27 / 49 );
				}

				const Color halo = GameMakeColor( 0, 0, 0, 90 );
				const Color drop = GameMakeColor( 0, 0, 0, 200 );
				const Color clear = GameMakeColor( 0, 0, 0, 0 );
				const Int step = max( cameoH / 40, 1 );
				auto shadowed = [ & ]( DisplayString *text, Int x, Int y, Color ink )
				{
					text->draw( x - step, y, halo, clear, 0, 0 );
					text->draw( x + step, y, halo, clear, 0, 0 );
					text->draw( x, y - step, halo, clear, 0, 0 );
					text->draw( x, y + step, halo, clear, 0, 0 );
					text->draw( x, y, ink, drop, step, step );
				};

				for( Int each = 0; each < cameos && s_nameFont != NULL && s_clockFont != NULL; each++ )
				{
					const ListedSuperweapon &line = listed[ each ];
					const IRegion2D &cameo = place[ each ];
					IRegion2D cell = m_superweaponPlace[ each ];
					DisplayString *name = line.info->getNameString();
					DisplayString *clock = line.info->getTimeString();

					const Int nameRoom = cameoW - 2 * pad;
					if( name->getFont() != s_nameFont || name->getTextLength() == 0 || name->getWidth() > nameRoom )
					{
						AsciiString label;
						label.format( "GUI:%s", line.power.str() );
						name->setFont( s_nameFont );
						name->setText( UIStyle::fitKey( label.str(), s_nameFont, nameRoom ) );
					}
					name->setClipRegion( &cell );
					const Int nameY = cameo.lo.y + pad / 2;
					shadowed( name, cameo.lo.x + ( cameoW - name->getWidth() ) / 2, nameY, GameMakeColor( 236, 238, 242, 255 ) );

					if( line.ready )
						continue;
					UnicodeString seconds;
					seconds.format( u"%d", line.seconds > 0 ? line.seconds : 0 );
					clock->setFont( s_clockFont );
					clock->setText( seconds );
					clock->setClipRegion( &cell );
					// centred under the name; a figure has no descender, so the line sits a tenth of itself low
					const Int roomTop = nameY + s_nameFont->height;
					const Int clockY = roomTop + ( cameo.hi.y - roomTop - s_clockFont->height ) / 2 + s_clockFont->height / 10;
					shadowed( clock, cameo.lo.x + ( cameoW - clock->getWidth() ) / 2, clockY,
										UIStyle::readable( readableHouseColor( line.owner ) ) );
				}
				if( more )
				{
					UnicodeString rest;
					rest.format( u"+%d", (Int)listed.size() - cameos );
					drawHudTab( style.tabDown, m_superweaponTab, 1, cellString( m_superweaponMoreText, rest ) );
				}
			}
		}
	}

	// the watcher's radar box, only while watching: under the windows, so the radar paints on its glass; the
	// rest of his HUD is drawn over everything in drawHudOverlay
	if( TheGameLogic->getFrame() > 0 && !TheGameLogic->isInShellGame() && localPlayerWatching() )
		drawObserverConsoleBay();

	// a player's squad list on the left, and the picked squad's build tray where a watcher's rack hangs
	GroupPanel_draw( rackPlateTop );

	// a Direct Strike map's own HUD, in the command bar's place, and over it the match's end
	DirectStrikeHud_draw();
	DirectStrikeEndScreen_draw();

	// an airlift power's load being picked, over everything else at its spot
	AirliftPanel_draw();

	// draw named timers
	if (TheGameLogic->getFrame() > 0 && m_showNamedTimers)
	{
//		Int namedTimerCount = 0;
		Bool reverseXDir = (m_namedTimerPosition.x >= 0.5f);
		// EA's column as far in from the right edge as EA had it, at the HUD's scale
		Int startX = hudEdgeX( m_namedTimerPosition.x );
		Int startY = max( (Int)(m_namedTimerPosition.y * TheDisplay->getHeight()), hudStripBottom() );
		Color bgColor = GameMakeColor( 0, 0, 0, 255 );
		for (NamedTimerMapIt mapIt = m_namedTimers.begin(); mapIt != m_namedTimers.end(); ++mapIt)
		{
			AsciiString timerName = mapIt->first;
			NamedTimerInfo *info = mapIt->second;
			DEBUG_ASSERTCRASH(info, ("No namedTimer info!"));
			if (info)
			{
				// found one - draw it
				UnicodeString line;
				// a map may show a counter its scripts never set; such a counter reads 0, as an unset one does in a condition
				const TCounter *counter = TheScriptEngine->getCounter(timerName);
				Int framesLeft = counter ? counter->value : 0;
				UnsignedInt readyFrame = TheGameLogic->getFrame();
				if (framesLeft > 0)
					readyFrame += ConvertRetailFrames( framesLeft );	// script timers count retail frames of game time
				// at the rate the match is paced to, not the one measured over the last second: that one
				// wanders a frame or two either way (and far down after a load), and a countdown divided by
				// it jumps a second or runs backwards.  The pacer's own rate, a cinematic's slower one included
				// (GameEngine_missionTimeRate's), so the clock keeps real seconds under held input too
				Int readySecs = ControlBar_secondsFromFramesAt( (Real)(readyFrame - TheGameLogic->getFrame()),
						GameEngine_cinematicFramesPerSecond( TheGameEngine->getFramesPerSecondLimit(), GameEngine_isPacingCinematic() ) );
				if ( (info->isCountdown && readySecs != info->timestamp) || (!info->isCountdown && framesLeft != info->timestamp) )
				{
					if (!readySecs && info->isCountdown)
					{
						// go bold - we're good to go
						info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerReadyFont, 
							TheGlobalLanguageData->adjustFontSize(m_namedTimerReadyPointSize), m_namedTimerReadyBold ) );
					}
					else
					{
						// if we were at 0, we've just fired - kill the bold
						if (info->timestamp == 0 || info->isCountdown)
						{
							info->displayString->setFont( TheFontLibrary->getFont( m_namedTimerNormalFont, 
								TheGlobalLanguageData->adjustFontSize(m_namedTimerNormalPointSize), m_namedTimerNormalBold ) );
						}
					}

					info->timestamp = readySecs;
					Int min = readySecs/60;
					Int sec = readySecs - min*60;
					
					if (!info->isCountdown)
						line.format(u"%s %d", info->timerText.str(), framesLeft);
					else
					{
						if (sec >= 10)
							line.format(u"%s %d:%d", info->timerText.str(), min, sec);
						else
							line.format(u"%s %d:0%d", info->timerText.str(), min, sec);
					}
					info->displayString->setText(line);
				}

				// draw the text
				Int drawX = startX;
				if (reverseXDir)
					drawX -= info->displayString->getWidth();
				if (!readySecs && info->isCountdown)
				{
					if ( m_namedTimerFlashDuration != 0.0f )
					{
						if ( TheGameLogic->getFrame() >= m_namedTimerLastFlashFrame + (Int)(m_namedTimerFlashDuration) )
						{
							m_namedTimerUsedFlashColor = !m_namedTimerUsedFlashColor;
							m_namedTimerLastFlashFrame = TheGameLogic->getFrame();
						}
						info->displayString->draw( drawX, startY, (m_namedTimerUsedFlashColor)?info->color:m_namedTimerFlashColor, bgColor );
					}
					else
					{
						info->displayString->draw( drawX, startY, info->color, bgColor );
					}
				}
				else
				{
					info->displayString->draw( drawX, startY, info->color, bgColor );
				}

				// increment text spot to next location
				startY -= info->displayString->getFont()->height;
			}
		}
	}
	
	// draw the anchor of a right-drag pan
	if (TheLookAtTranslator && m_drawRMBScrollAnchor)
	{
		const ICoord2D* anchor = TheLookAtTranslator->getScrollAnchor();
		if (anchor)
		{
			static const Int w = 2;
			static const Int h = 2;
			static const Int r = 4; // ratio
			static const Color mainColor = GameMakeColor(0, 255, 0, 255);
			static const Color dropColor = GameMakeColor(0, 0, 0, 255);
			TheDisplay->drawFillRect( anchor->x-w*r-1, anchor->y-h-1, w*2*r+3, h*2+3, dropColor );
			TheDisplay->drawFillRect( anchor->x-w-1, anchor->y-h*r-1, w*2+3, h*2*r+3, dropColor );
			TheDisplay->drawFillRect( anchor->x-w*r, anchor->y-h, w*2*r+1, h*2+1, mainColor );
			TheDisplay->drawFillRect( anchor->x-w, anchor->y-h*r, w*2+1, h*2*r+1, mainColor );
		}
	}

	//draw superweapon ready multipliers
	TheControlBar->drawSpecialPowerShortcutMultiplierText();

}  // end postDraw

//-------------------------------------------------------------------------------------------------
/** Create the control user interface GUI */
//-------------------------------------------------------------------------------------------------
void InGameUI::createControlBar( void )
{

	TheWindowManager->winCreateFromScript( AsciiString("ControlBar.wnd") );

	// winCreateFromScript hands back nothing for a missing file and destroys the whole tree when one
	// child fails to parse, and everything after this (ControlBar::init first) stands on the bar's
	// parent.  v4.1.0 died on that null in HideControlBar at every launch of one install, with no line
	// saying which; say it, and stop with a message instead of an access violation.
	if( TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" ) ) == NULL )
	{
		DEBUG_LOG(( "InGameUI::createControlBar - Window\\ControlBar.wnd built no ControlBar.wnd:ControlBarParent (missing, or a window in it failed to parse)\n" ));
		MessageBoxWrapper( "The command bar's layout (Window\\ControlBar.wnd) could not be read.\n\nRepair the game in the launcher, or remove any mod files from the game folder.", "Command & Conquer Generals Zero Hour", MSGBOX_OK | MSGBOX_TASKMODAL | MSGBOX_ICONERROR );
		_exit(1);
	}

	HideControlBar();
/*
	// hide all windows created from this layout
	GameWindow *window = TheWindowManager->winGetWindowList();
	for( ; window; window = window->winGetPrev() )
		window->winHide( TRUE );
*/

}  // end createControlBar

//-------------------------------------------------------------------------------------------------
/** Create the replay control GUI */
//-------------------------------------------------------------------------------------------------
void InGameUI::createReplayControl( void )
{

	m_replayWindow = TheWindowManager->winCreateFromScript( AsciiString("ReplayControl.wnd") );
	// the parent draws the replay strip itself (ReplayControls.cpp); the layout has it see-through
	m_replayWindow->winClearStatus( WIN_STATUS_SEE_THRU | WIN_STATUS_BORDER );
	m_replayWindow->winSetDrawFunc( ReplayControlDraw );

/*	
	// hide all windows created from this layout
	GameWindow *window = TheWindowManager->winGetWindowList();
	for( ; window; window = window->winGetPrev() )
		window->winHide( TRUE );
*/

}  // end createReplayControl

// ------------------------------------------------------------------------------------------------
// InGameUI::playMovie
// ------------------------------------------------------------------------------------------------
void InGameUI::playMovie( const AsciiString& movieName )
{
	LOGIC_AFTER_TICK( [=]() { playMovie( movieName ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );

	stopMovie();

	m_videoStream = TheVideoPlayer->open( movieName );

	if ( m_videoStream == NULL )
	{
		return;
	}

	m_currentlyPlayingMovie = movieName;
	m_videoBuffer = TheDisplay->createVideoBuffer();

	if (	m_videoBuffer == NULL || 
				!m_videoBuffer->allocate(	m_videoStream->width(), 
													m_videoStream->height())
		)
	{
		stopMovie();
		return;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::stopMovie( void )
{
	delete m_videoBuffer;
	m_videoBuffer = NULL;

	if ( m_videoStream )
	{
		m_videoStream->close();
		m_videoStream = NULL;
	}

	if (!m_currentlyPlayingMovie.isEmpty()) {
		//TheScriptEngine->notifyOfCompletedVideo(m_currentlyPlayingMovie); // removing sync error source -MDC
		m_currentlyPlayingMovie = AsciiString::TheEmptyString;
	}
}

// ------------------------------------------------------------------------------------------------
// InGameUI::videoBuffer
// ------------------------------------------------------------------------------------------------
VideoBuffer* InGameUI::videoBuffer( void )
{
	return m_videoBuffer;
}

// ------------------------------------------------------------------------------------------------
// InGameUI::playMovie
// ------------------------------------------------------------------------------------------------
void InGameUI::playCameoMovie( const AsciiString& movieName )
{

	stopCameoMovie();

	m_cameoVideoStream = TheVideoPlayer->open( movieName );

	if ( m_cameoVideoStream == NULL )
	{
		return;
	}

	m_cameoVideoBuffer = TheDisplay->createVideoBuffer();

	if (	m_cameoVideoBuffer == NULL || 
				!m_cameoVideoBuffer->allocate(	m_cameoVideoStream->width(), 
													m_cameoVideoStream->height())
		)
	{
		stopCameoMovie();
		return;
	}
	GameWindow *window = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:RightHUD") ));
	WinInstanceData *winData = window->winGetInstanceData();
	winData->setVideoBuffer(m_cameoVideoBuffer);
//	window->winHide(FALSE);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void InGameUI::stopCameoMovie( void )
{
//RightHUD
	//GameWindow *window = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:CameoMovieWindow") ));
	GameWindow *window = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString("ControlBar.wnd:RightHUD") ));
//	window->winHide(FALSE);
	WinInstanceData *winData = window->winGetInstanceData();
	winData->setVideoBuffer(NULL);
	
	delete m_cameoVideoBuffer;
	m_cameoVideoBuffer = NULL;

	if ( m_cameoVideoStream )
	{
		m_cameoVideoStream->close();
		m_cameoVideoStream = NULL;
	}
	
}

// ------------------------------------------------------------------------------------------------
// InGameUI::videoBuffer
// ------------------------------------------------------------------------------------------------
VideoBuffer* InGameUI::cameoVideoBuffer( void )
{
	return m_cameoVideoBuffer;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void InGameUI::displayCantBuildMessage( LegalBuildCode lbc )
{

	switch( lbc )
	{

		//---------------------------------------------------------------------------------------------
		case LBC_RESTRICTED_TERRAIN:
			TheInGameUI->message( "GUI:CantBuildRestrictedTerrain" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_NOT_FLAT_ENOUGH:
			TheInGameUI->message( "GUI:CantBuildNotFlatEnough" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_OBJECTS_IN_THE_WAY:
			TheInGameUI->message( "GUI:CantBuildObjectsInTheWay" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_TOO_CLOSE_TO_SUPPLIES:
			TheInGameUI->message( "GUI:CantBuildTooCloseToSupplies" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_TOO_MANY_DERRICK_DEFENSES:
			TheInGameUI->message( "GUI:CantBuildTooManyDerrickDefenses" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_TOO_CLOSE_TO_ENEMY:
			TheInGameUI->message( "GUI:CantBuildTooCloseToEnemy" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_NO_CLEAR_PATH:
		  TheInGameUI->message( "GUI:CantBuildNoClearPath" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_SHROUD:
			TheInGameUI->message( "GUI:CantBuildShroud" );
			break;

		//---------------------------------------------------------------------------------------------
		case LBC_GENERIC_FAILURE:
		default:

			TheInGameUI->message( "GUI:CantBuildThere" );
			break;

	}  // end switch

}  // end displayCantBuildMessage

// ------------------------------------------------------------------------------------------------
// InGameUI::militarySubtitle
// ------------------------------------------------------------------------------------------------
void InGameUI::militarySubtitle( const AsciiString& label, Int duration )
{
	LOGIC_AFTER_TICK( [=]() { militarySubtitle( label, duration ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	// make sure we don't already have a subtitle up there
	removeMilitarySubtitle();

	// update our history
	UpdateDiplomacyBriefingText(label, FALSE);

	UnicodeString title = TheGameText->fetch(label);

	// make sure we actually will be displaying something
	if( title.isEmpty() || duration <= 0)
	{
		DEBUG_CRASH(("Trying to create a military subtitle but either title is empty (%s) or duration is <= 0 (%d)",WideCharAsUtf8( title.str() ).str(), duration));
		return;
	}

	// we need some frame info to set our timings
	UnsignedInt currLogicFrame = TheGameLogic->getFrame();
	const int messageTimeout = currLogicFrame + (Int)(((Real)LOGICFRAMES_PER_SECOND * duration)/1000.0f);	// wall time: it goes with the speech

	// disable tooltips until this frame, cause we don't want to collide with the military subtitles.
	TheInGameUI->disableTooltipsUntil(messageTimeout);
	
	// the position passed in is an 800x600 one: at the HUD's one scale from the top left of hudPlay
	// (the screen's, or HudWidth's centred area), as the messages are
	Coord2D multiplier;
	const IRegion2D play = hudPlay();
	const UIRect box = { play.lo.x, 0, play.hi.x - play.lo.x, (Int)TheDisplay->getHeight() };
	multiplier.x = multiplier.y = ControlBarUniformScale();

	// lets bring out the data structure!
	m_militarySubtitle = NEW MilitarySubtitleData;

	m_militarySubtitle->subtitle.set(title);
	m_militarySubtitle->blockDrawn = TRUE;
	m_militarySubtitle->blockBeginFrame = currLogicFrame;
	m_militarySubtitle->lifetime = messageTimeout;
	m_militarySubtitle->blockPos.x =  m_militarySubtitle->position.x = box.x + m_militaryCaptionPosition.x * multiplier.x;
	m_militarySubtitle->blockPos.y =  m_militarySubtitle->position.y = box.y + m_militaryCaptionPosition.y * multiplier.y;
	m_militarySubtitle->incrementOnFrame = currLogicFrame + (Int)(((Real)LOGICFRAMES_PER_SECOND * TheGlobalLanguageData->m_militaryCaptionDelayMS)/1000.0f);
	m_militarySubtitle->fadeStep = 0;
	m_militarySubtitle->index = 0;
	for (int i = 1; i < MAX_SUBTITLE_LINES; i ++)
		m_militarySubtitle->displayStrings[i] = NULL;

	m_militarySubtitle->currentDisplayString = 0;
	m_militarySubtitle->displayStrings[0] = TheDisplayStringManager->newDisplayString();
	m_militarySubtitle->displayStrings[0]->reset();
	m_militarySubtitle->displayStrings[0]->setFont(	TheFontLibrary->getFont( m_militaryCaptionTitleFont, 
		TheGlobalLanguageData->adjustFontSize(m_militaryCaptionTitlePointSize), m_militaryCaptionTitleBold ) );
	m_militarySubtitle->color = GameMakeColor(m_militaryCaptionColor.red, m_militaryCaptionColor.green, m_militaryCaptionColor.blue, m_militaryCaptionColor.alpha);
}

// ------------------------------------------------------------------------------------------------
// InGameUI::removeMilitarySubtitle
// ------------------------------------------------------------------------------------------------
void InGameUI::removeMilitarySubtitle( void )
{
	// sanity (is there really such a thing in this world?)
	if(!m_militarySubtitle)
		return;

	TheInGameUI->clearTooltipsDisabled();

	// loop through and free up the display strings
	for(Int i = 0; i <= m_militarySubtitle->currentDisplayString; i ++)
	{
		TheDisplayStringManager->freeDisplayString(m_militarySubtitle->displayStrings[i]);
		m_militarySubtitle->displayStrings[i] = NULL;
	}

	//delete it man!
	delete m_militarySubtitle;
	m_militarySubtitle= NULL;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool InGameUI::areSelectedObjectsControllable() const
{
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// loop through all the selected drawables
	const Drawable *draw;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		// get this drawable
		draw = *it;

		// All selected objects will have the same local controller, so 
		// simply return the first one.
		return draw->getObject()->isLocallyControlled();
	}

	// Nothing selected...
	return FALSE;
}

//------------------------------------------------------------------------------
//Resets the camera to default zoom and orientation.
//------------------------------------------------------------------------------
void InGameUI::resetCamera()
{
	ViewLocation currentView;
	TheTacticalView->getLocation( &currentView ); 
	TheTacticalView->resetCamera( &currentView.getPosition(), 1, 0.0f, 0.0f );
}

//------------------------------------------------------------------------------
//Checks to see if an object can interact with an object in a non-hostile manner. This is currently used by the selection 
//translator to determine whether to do something to an object or select it instead based on the context of what is currently
//selected.
//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsNonAttackInteractWithObject( const Object *objectToInteractWith, SelectionRules rule ) const
{
	for( int i = 1; i < NUM_ACTIONTYPES; i++ )
	{
		if( i != ACTIONTYPE_ATTACK_OBJECT )
		{
			if( canSelectedObjectsDoAction( (ActionType)i, objectToInteractWith, rule ) )
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

CanAttackResult InGameUI::getCanSelectedObjectsAttack( ActionType action, const Object *objectToInteractWith, SelectionRules rule, Bool additionalChecking ) const
{
	//Kris: Aug 16, 2003
	//John McDonald added this code back in Oct 09, 2002. 
	//Replaced it with palatable code.
	//if( (objectToInteractWith == NULL) != (action == ACTIONTYPE_SET_RALLY_POINT)) <---BAD CODE
	if( !objectToInteractWith && action != ACTIONTYPE_SET_RALLY_POINT || //No object to interact with (and not rally point mode)
			 objectToInteractWith && action == ACTIONTYPE_SET_RALLY_POINT )  //Object to interact with (and rally point mode)
	{
		//Sanity check OR can't set a rally point over an object.
		return ATTACKRESULT_NOT_POSSIBLE;
	}

	// get selected list of drawables
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// set up counters for rule checking
	Int count = 0;
	CanAttackResult bestResult = ATTACKRESULT_NOT_POSSIBLE;
	CanAttackResult worstResult = ATTACKRESULT_POSSIBLE;

	// loop through all the selected drawables
	Drawable *other;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		other = *it;
		count++;

		switch( action )
		{
			case ACTIONTYPE_ATTACK_OBJECT:
			{
				//additionalChecking is TRUE only if force attack mode is on.
				CanAttackResult result = 	TheActionManager->getCanAttackObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, 
									additionalChecking ? ATTACK_NEW_TARGET_FORCED : ATTACK_NEW_TARGET );

				if( result > bestResult )
				{
					//Best result is used for the rule: SELECTION_ANY
					bestResult = result;
				}
				if( result < worstResult )
				{
					//Worst result is used for the rule: SELECTION_ALL
					worstResult = result;
				}
				break;
			}

			case ACTIONTYPE_NONE:
			case ACTIONTYPE_GET_REPAIRED_AT:
			case ACTIONTYPE_DOCK_AT:
			case ACTIONTYPE_GET_HEALED_AT:
			case ACTIONTYPE_REPAIR_OBJECT:
			case ACTIONTYPE_RESUME_CONSTRUCTION:
			case ACTIONTYPE_COMBATDROP_INTO:
			case ACTIONTYPE_ENTER_OBJECT:
			case ACTIONTYPE_HIJACK_VEHICLE:
			case ACTIONTYPE_SABOTAGE_BUILDING:
			case ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB:
			case ACTIONTYPE_CAPTURE_BUILDING:
			case ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING:
#ifdef ALLOW_SURRENDER
			case ACTIONTYPE_PICK_UP_PRISONER:
#endif
			case ACTIONTYPE_STEAL_CASH_VIA_HACKING:
			case ACTIONTYPE_DISABLE_BUILDING_VIA_HACKING:
			case ACTIONTYPE_MAKE_DEFECTOR:
			case ACTIONTYPE_SET_RALLY_POINT:
			default:
				DEBUG_CRASH( ("Called InGameUI::getCanSelectedObjectsAttack() with actiontype %d. Only accepts attack types! Should you be calling InGameUI::canSelectedObjectsDoAction() instead?") );
				return ATTACKRESULT_INVALID_SHOT;

		}

	}  // end for

	if( count > 0 )
	{
		if( rule == SELECTION_ANY )
		{
			return bestResult;
		}
		return worstResult;
	}

	// no can do!
	return ATTACKRESULT_NOT_POSSIBLE;
}

//------------------------------------------------------------------------------
//Wrapper function that checks a specific action.
//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsDoAction( ActionType action, const Object *objectToInteractWith, SelectionRules rule, Bool additionalChecking ) const
{

	//Kris: Aug 16, 2003
	//John McDonald added this code back in Oct 09, 2002. This code is SO wrong that it should
	//be a firing offense. Strangely enough, this code has gone unnoticed for nearly a year
	//and nearly two projects. I'm fixing this now by moving it to the rally point code...
	//because it would be nice if a saboteur could actually sabotage a building via a 
	//commandbutton.
	//if( (objectToInteractWith == NULL) != (action == ACTIONTYPE_SET_RALLY_POINT))
	if( !objectToInteractWith && action != ACTIONTYPE_SET_RALLY_POINT || //No object to interact with (and not rally point mode)
			 objectToInteractWith && action == ACTIONTYPE_SET_RALLY_POINT )  //Object to interact with (and rally point mode)
	{
		//Sanity check OR can't set a rally point over an object.
		return FALSE;
	}

	// get selected list of drawables
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// set up counters for rule checking
	Int count = 0;
	Int qualify = 0;

	// loop through all the selected drawables
	Drawable *other;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		other = *it;
		count++;
		Bool success = FALSE;

		switch( action )
		{
			case ACTIONTYPE_NONE:
				//However strange this might be, it is always possible to do "nothing"
				//although I can't think of why this would be needed...
				return TRUE;
			case ACTIONTYPE_GET_REPAIRED_AT:
				success = TheActionManager->canGetRepairedAt( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_DOCK_AT:
				success = TheActionManager->canDockAt( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_GET_HEALED_AT:
				success = TheActionManager->canGetHealedAt( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				if( success )
				{
					ContainModuleInterface *contain = objectToInteractWith->getContain();
					if( contain && contain->isHealContain() )
					{
						//This container is only used for the purposes of healing and we cannot 
						//enter it normally -- this is NOT a transport!
						success = false;
					}
				}
				break;
			case ACTIONTYPE_REPAIR_OBJECT:
			{
				ObjectID currentRepairer = objectToInteractWith->getSoleHealingBenefactor(); 
				success = ( TheActionManager->canRepairObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER ) 
										&& ( currentRepairer == INVALID_ID || currentRepairer == other->getObject()->getID() ) );
											// unless someone else is already healing it...
											// please note that this add'l test is left out of canRepairObject() since canRepairObject 
											// gets called from within the Dozer/WorkerAIUpdates' stateMachines as they continue the repair process.
											// This remains true.
				break;
			}
			case ACTIONTYPE_RESUME_CONSTRUCTION:
				success = TheActionManager->canResumeConstructionOf( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_COMBATDROP_INTO:
				success = TheActionManager->canEnterObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, COMBATDROP_INTO );
				break;
			case ACTIONTYPE_ENTER_OBJECT:
				//additionalChecking is TRUE only if we want to check if transport is full first.
				success = TheActionManager->canEnterObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, additionalChecking ? CHECK_CAPACITY : DONT_CHECK_CAPACITY );
				break;
			case ACTIONTYPE_ATTACK_OBJECT:
				DEBUG_CRASH( ("Called InGameUI::canSelectedObjectsDoAction() with ACTIONTYPE_ATTACK_OBJECT. You must use InGameUI::getCanSelectedObjectsAttack() instead.") );
				return FALSE;
			case ACTIONTYPE_HIJACK_VEHICLE:
				success = TheActionManager->canHijackVehicle( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_SABOTAGE_BUILDING:
				success = TheActionManager->canSabotageBuilding( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB:
				success = TheActionManager->canConvertObjectToCarBomb( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_CAPTURE_BUILDING:
				success = TheActionManager->canCaptureBuilding( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING:
				success = TheActionManager->canDisableVehicleViaHacking( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
#ifdef ALLOW_SURRENDER
			case ACTIONTYPE_PICK_UP_PRISONER:
				success = TheActionManager->canPickUpPrisoner( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
#endif
			case ACTIONTYPE_STEAL_CASH_VIA_HACKING:
				success = TheActionManager->canStealCashViaHacking( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_DISABLE_BUILDING_VIA_HACKING:
				success = TheActionManager->canDisableBuildingViaHacking( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_MAKE_DEFECTOR:
				success = TheActionManager->canMakeObjectDefector( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER );
				break;
			case ACTIONTYPE_SET_RALLY_POINT:
			{
				Object *obj = other->getObject();
				if (!obj) {
					success = false;
					break;
				}
				success = (obj->isKindOf(KINDOF_AUTO_RALLYPOINT) && obj->isLocallyControlled());
				break;
			}
		}

		if( success )
		{
			if( rule == SELECTION_ANY )
			{
				return TRUE;
			}

			++qualify;
		}
	}  // end for

	//If the rule is all must qualify, do the check now and return success
	//only if all the selected units qualified.
	if( rule == SELECTION_ALL && count > 0 && qualify == count )
	{
		return TRUE;
	}

	// no can do!
	return FALSE;
}

//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsDoSpecialPower( const CommandButton *command, const Object *objectToInteractWith, const Coord3D *position, SelectionRules rule, UnsignedInt commandOptions, Object* ignoreSelObj ) const
{
	//Get the special power template.
	const SpecialPowerTemplate *spTemplate = command->getSpecialPowerTemplate();

	//Order of precendence:
	//1) NO TARGET OR POS
	//2) COMMAND_OPTION_NEED_OBJECT_TARGET
	//3) NEED_TARGET_POS
	Bool doAtPosition = BitTest( command->getOptions(), NEED_TARGET_POS );
	Bool doAtObject = BitTest( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET );

	//Sanity checks
	if( doAtObject && !objectToInteractWith )
	{
		return false;
	}
	if( doAtPosition && !position )
	{
		return false;		
	}

	// get selected list of drawables
	Drawable* ignoreSelDraw = ignoreSelObj ? ignoreSelObj->getDrawable() : NULL;

	DrawableList tmpList;
	if (ignoreSelDraw)
		tmpList.push_back(ignoreSelDraw);

	const DrawableList* selected = (tmpList.size() > 0) ? &tmpList : TheInGameUI->getAllSelectedDrawables();

	// set up counters for rule checking
	Int count = 0;
	Int qualify = 0;

	// loop through all the selected drawables
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		Drawable* other = *it;
		count++;

		if( !doAtObject && !doAtPosition )
		{
			if( TheActionManager->canDoSpecialPower( other->getObject(), spTemplate, CMD_FROM_PLAYER, commandOptions ) )
			{
				//This is the no target version
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
		else if( doAtObject )
		{
			if( TheActionManager->canDoSpecialPowerAtObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, spTemplate, commandOptions ) )
			{
				//This requires a object target
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
		else if( doAtPosition )
		{
			if( TheActionManager->canDoSpecialPowerAtLocation( other->getObject(), position, CMD_FROM_PLAYER, spTemplate, objectToInteractWith, commandOptions ) )
			{
				//This requires a valid location.
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
	}
	if( rule == SELECTION_ALL && count > 0 && qualify == count )
	{
		return true;
	}
	return false;
}

//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsOverrideSpecialPowerDestination( const Coord3D *loc, SelectionRules rule, SpecialPowerType spType ) const
{
	// set up counters for rule checking
	Int count = 0;
	Int qualify = 0;

	// get selected list of drawables
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// loop through all the selected drawables
	Drawable *other;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		other = *it;
		count++;

		if( TheActionManager->canOverrideSpecialPowerDestination( other->getObject(), loc, spType, CMD_FROM_PLAYER ) )
		{
			if( rule == SELECTION_ANY )
			{
				return true;
			}
			qualify++;
		}
	}
	if( rule == SELECTION_ALL && count > 0 && qualify == count )
	{
		return true;
	}
	return false;
}


//------------------------------------------------------------------------------
Bool InGameUI::canSelectedObjectsEffectivelyUseWeapon( const CommandButton *command, const Object *objectToInteractWith, const Coord3D *position, SelectionRules rule ) const
{
	//Get the special power template.
	WeaponSlotType slot = command->getWeaponSlot();

	//Order of precendence:
	//1) NO TARGET OR POS
	//2) COMMAND_OPTION_NEED_OBJECT_TARGET
	//3) NEED_TARGET_POS
	Bool doAtPosition = BitTest( command->getOptions(), NEED_TARGET_POS );
	Bool doAtObject = BitTest( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET );

	//Sanity checks
	if( doAtObject && !objectToInteractWith )
	{
		return false;
	}
	if( doAtPosition && !position )
	{
		return false;		
	}

	// get selected list of drawables
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

	// set up counters for rule checking
	Int count = 0;
	Int qualify = 0;

	// loop through all the selected drawables
	Drawable *other;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
	
		// get this drawable
		other = *it;
		count++;

		if( !doAtObject && !doAtPosition )
		{
			if( TheActionManager->canFireWeapon( other->getObject(), slot, CMD_FROM_PLAYER ) )
			{
				//This is the no target version
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
		else if( doAtObject )
		{
			if( TheActionManager->canFireWeaponAtObject( other->getObject(), objectToInteractWith, CMD_FROM_PLAYER, slot ) )
			{
				//This requires a object target
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
		else if( doAtPosition )
		{
			if( TheActionManager->canFireWeaponAtLocation( other->getObject(), position, CMD_FROM_PLAYER, slot, objectToInteractWith ) )
			{
				//This requires a valid location.
				if( rule == SELECTION_ANY )
				{
					return true;
				}
				qualify++;
			}
		}
	}
	if( rule == SELECTION_ALL && count > 0 && qualify == count )
	{
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
Int InGameUI::selectAllUnitsByTypeAcrossRegion( IRegion2D *region, KindOfMaskType mustBeSet, KindOfMaskType mustBeClear, Bool idleOnly )
{
	KindOfSelectionData data;
	Int newSelectionCount = 0;
	Int oldSelectionCount = getAllSelectedDrawables()->size();

	data.m_mustbeSet = mustBeSet;
	data.m_mustbeClear = mustBeClear;
	data.m_idleOnly = idleOnly;

	if (region)
	{
		TheTacticalView->iterateDrawablesInRegion(region, kindOfUnitSelection, (void *)&data);
		newSelectionCount += data.newlySelectedDrawables.size();
	}
	else
	{
		// loop over the map
		Drawable *temp = TheGameClient->firstDrawable();
		while( temp )
		{
			if( kindOfUnitSelection( temp, (void *)&data) )
			{
				newSelectionCount ++;
			}

			temp = temp->getNextDrawable();
		}
	}
	setDisplayedMaxWarning( FALSE );

	if (newSelectionCount > 0)
	{
		// create selected message
		GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

		teamMsg->appendBooleanArgument( (oldSelectionCount == 0) ? TRUE : FALSE );

		const Drawable *draw;

		//Loop through each drawable add append it's objectID to the event.
		for( DrawableListCIt it = data.newlySelectedDrawables.begin(); it != data.newlySelectedDrawables.end(); ++it )
		{
			draw = *it;
			if( draw && draw->getObject() )
			{
				teamMsg->appendObjectIDArgument( draw->getObject()->getID() );
			}
		}
	}

	return newSelectionCount;
}

// ------------------------------------------------------------------------------------------------
/** Selects maching units on the screen */
// ------------------------------------------------------------------------------------------------
Int InGameUI::selectMatchingAcrossRegion( IRegion2D *region )
{
	const DrawableList *selected = getAllSelectedDrawables();

	/* loop through all the selected drawables and create a set of all the objects,
	   so that you only iterate once through each type of object
	*/

	const Drawable *draw;

	//std::set<AsciiString> drawableList;
	std::set<const ThingTemplate*> drawableList;
	Bool carBomb = FALSE;
	
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		// get this drawable
		draw = *it;
		if( draw && draw->getObject() && draw->getObject()->isLocallyControlled() )
		{
			// Use the Object's thing template, doing so will prevent wierdness for disguised vehicles.
			drawableList.insert( draw->getObject()->getTemplate() );
			if( draw->getObject()->testStatus( OBJECT_STATUS_IS_CARBOMB ) )
			{
				carBomb = TRUE;
			}
		}
	}

	if (drawableList.size() == 0)
		return -1; // nothing useful selected to begin with - don't bother iterating

	std::set<const ThingTemplate*>::iterator iter;
	const ThingTemplate *templateName;

	// now use the list to select across screen
	MatchingUnitSelectionData data;
	Int newSelectionCount = 0;

	for( iter = drawableList.begin(); iter != drawableList.end(); ++iter )
	{
		// get this drawable
		templateName = *iter;

		data.templateToSelect = templateName;
		data.isCarBomb        = carBomb;
		if (region)
			newSelectionCount +=TheTacticalView->iterateDrawablesInRegion(region, similarUnitSelection, (void *)&data);
		else
		{
			// loop over the map
			Drawable *temp = TheGameClient->firstDrawable();
			while( temp )
			{
				newSelectionCount += similarUnitSelection( temp, (void *)&data);
				temp = temp->getNextDrawable();
			}
		}
		setDisplayedMaxWarning( FALSE );
	}

	if (newSelectionCount > 0)
	{
		// create selected message
		GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND );
		// not creating a new team so pass in false
		teamMsg->appendBooleanArgument( FALSE );

		//Loop through each drawable add append it's objectID to the event.
		for( DrawableListCIt it = data.newlySelectedDrawables.begin(); it != data.newlySelectedDrawables.end(); ++it )
		{
			draw = *it;
			if( draw && draw->getObject() )
			{
				teamMsg->appendObjectIDArgument( draw->getObject()->getID() );
			}
		}
	}

	return newSelectionCount;

}

// ------------------------------------------------------------------------------------------------
Int InGameUI::selectAllUnitsByTypeAcrossScreen(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear, Bool idleOnly)
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
			
	IRegion2D region;
	ICoord2D origin;
	ICoord2D size;
 
	TheTacticalView->getOrigin( &origin.x, &origin.y );
	size.x = TheTacticalView->getWidth();
	size.y = TheTacticalView->getHeight();
 
	buildRegion( &origin, &size, &region );

	Int numSelected = selectAllUnitsByTypeAcrossRegion(&region, mustBeSet, mustBeClear, idleOnly);
	if (numSelected == -1)
	{
		UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
		TheInGameUI->message( message );
	}
	else if (numSelected == 0)
	{
	}
	else
	{
		UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossScreen" );
		TheInGameUI->message( message );
	}
	return numSelected;
}

// ------------------------------------------------------------------------------------------------
/** Selects maching units on the screen */
// ------------------------------------------------------------------------------------------------
Int InGameUI::selectMatchingAcrossScreen( void )
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
			
	IRegion2D region;
	ICoord2D origin;
	ICoord2D size;
 
	TheTacticalView->getOrigin( &origin.x, &origin.y );
	size.x = TheTacticalView->getWidth();
	size.y = TheTacticalView->getHeight();
 
	buildRegion( &origin, &size, &region );

	Int numSelected = selectMatchingAcrossRegion(&region);
	if (numSelected == -1)
	{
		UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
		TheInGameUI->message( message );
	}
	else if (numSelected == 0)
	{
	}
	else
	{
		UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossScreen" );
		TheInGameUI->message( message );
	}
	return numSelected;
}

//-------------------------------------------------------------------------------------------------
Int InGameUI::selectAllUnitsByTypeAcrossMap(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear, Bool idleOnly)
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
	Int numSelected = selectAllUnitsByTypeAcrossRegion(NULL, mustBeSet, mustBeClear, idleOnly);
	if (numSelected == -1)
	{
		UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
		TheInGameUI->message( message );
	}
	else if (numSelected == 0)
	{
		Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
		if( !draw || !draw->getObject() || !draw->getObject()->isKindOf( KINDOF_STRUCTURE ) )
		{
			UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossMap" );
			TheInGameUI->message( message );
		}
	}
	else
	{
		UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossMap" );
		TheInGameUI->message( message );
	}
	return numSelected;
}

//-------------------------------------------------------------------------------------------------
/** Selects matching units across map */
//-------------------------------------------------------------------------------------------------
Int InGameUI::selectMatchingAcrossMap()
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
	Int numSelected = selectMatchingAcrossRegion(NULL);
	if (numSelected == -1)
	{
		UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
		TheInGameUI->message( message );
	}
	else if (numSelected == 0)
	{
		Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
		if( !draw || !draw->getObject() || !draw->getObject()->isKindOf( KINDOF_STRUCTURE ) )
		{
			UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossMap" );
			TheInGameUI->message( message );
		}
	}
	else
	{
		UnicodeString message = TheGameText->fetch( "GUI:SelectedAcrossMap" );
		TheInGameUI->message( message );
	}
	return numSelected;
}

//-------------------------------------------------------------------------------------------------
Int InGameUI::selectAllUnitsByType(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear)
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
	Int numSelected = selectAllUnitsByTypeAcrossScreen(mustBeSet, mustBeClear, FALSE);
	if (numSelected == -1)
	{
		return numSelected;
	}

	if (numSelected == 0)
	{
		Int numSelectedAcrossMap = selectAllUnitsByTypeAcrossMap(mustBeSet, mustBeClear, FALSE);
		return numSelectedAcrossMap;
	}
	return numSelected;
}

//-------------------------------------------------------------------------------------------------
/** Selects matching units, either on screen or across map.  When called by pressing 'T',
    their is not a way to tell if the game is supposed to select across the screen, or
    across the map.  For mouse clicks, i.e. Alt + click or double click, we can directly call
    selectMatchingAcrossScreen or selectMatchingAcrossMap */
//-------------------------------------------------------------------------------------------------
Int InGameUI::selectUnitsMatchingCurrentSelection()
{
	/// When implementing this, obey TheInGameUI->getMaxSelectCount() if it is > 0
	Int numSelected = selectMatchingAcrossScreen();
	if (numSelected == -1)
		return numSelected;
	if (numSelected == 0)
	{
		Int numSelectedAcrossMap = selectMatchingAcrossMap();
		//if (numSelectedAcrossMap < 1)
		//{
			//UnicodeString message = TheGameText->fetch( "GUI:NothingSelected" );
			//TheInGameUI->message( message );
		//}
		return numSelectedAcrossMap;
	}
	return numSelected;

}

//-----------------------------------------------------------------------------
/**
 * Given an "anchor" point and the current mouse position (dest),
 * construct a valid 2D bounding region.
 */
//-----------------------------------------------------------------------------------
void InGameUI::buildRegion( const ICoord2D *anchor, const ICoord2D *dest, IRegion2D *region )
{
	// build rectangular region defined by the drag selection
	if (anchor->x < dest->x)
	{
		region->lo.x = anchor->x;
		region->hi.x = dest->x;
	}
	else
	{
		region->lo.x = dest->x;
		region->hi.x = anchor->x;
	}

	if (anchor->y < dest->y)
	{
		region->lo.y = anchor->y;
		region->hi.y = dest->y;
	}
	else
	{
		region->lo.y = dest->y;
		region->hi.y = anchor->y;
	}
}

//-------------------------------------------------------------------------------------------------
/** How opaque a floating text is ageMs after it appeared, 0 once it is gone.  Retail's pop: solid
	* for the INI's hold, then each logic frame took floor(n * rate) more alpha on the n-th frame after
	* it, a fade that starts slow and ends fast, rate * 30 * t^2 / 2 lost by t seconds.  Here it runs on
	* the wall clock, so neither the tick rate nor the game speed changes how long a figure stays; with
	* InGameUI.ini's 333 ms and 3 that is 2.7 seconds in all, as in 2003. */
//-------------------------------------------------------------------------------------------------
static Int floatingTextAlpha( UnsignedInt ageMs, UnsignedInt holdMs, Real vanishRate )
{
	if( ageMs <= holdMs )
		return 255;
	const Real t = INT_TO_REAL( (Int)( ageMs - holdMs ) ) / 1000.0f;
	const Real lost = 0.5f * max( 0.1f, vanishRate ) * RETAIL_LOGICFRAMES_PER_SECOND * t * t;
	return lost >= 255.0f ? 0 : 255 - REAL_TO_INT_CEIL( lost );
}

//-------------------------------------------------------------------------------------------------
/** Add a new floating text to our list */
//-------------------------------------------------------------------------------------------------
FloatingTextData *InGameUI::addFloatingText(const UnicodeString& text,const Coord3D *pos, Color color)
{
	// nobody in logic keeps the answer
	if( LogicThread::defersEffects() )
	{
		const Coord3D at = *pos;
		LogicThread::callAfterTick( [=]() { addFloatingText( text, &at, color ); } );
		return NULL;
	}
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	if( !TheGameLogic->getDrawIconUI() )
		return NULL;

	{
		FloatingTextData *newFTD = newInstance( FloatingTextData );
		newFTD->m_color = color;
		newFTD->m_pos3D.x = pos->x;
		newFTD->m_pos3D.z = pos->z;
		newFTD->m_pos3D.y = pos->y;
		newFTD->m_text = text;
		// the caption face at the caption size, a unit's name's lettering, as retail's line was
		newFTD->m_dString->setFont( TheFontLibrary->getFont( m_drawableCaptionFont,
			TheGlobalLanguageData->adjustFontSize( m_drawableCaptionPointSize ), FALSE ) );	// retail's regular weight
		newFTD->m_dString->setText(text);
		newFTD->m_bornMs = Clock_Milliseconds();

		m_floatingTextList.push_front( newFTD ); // add to the list
		return newFTD;
	}
}

//-------------------------------------------------------------------------------------------------
/** The money figure.  Who sees it is the scoreboard's rule: your own purse and your allies', and
	* everybody's for an observer, a defeated player or a replay.  An enemy's income never shows, so
	* the old per-caller stealth checks have nothing left to hide: a stealthed hacker or a cloaked
	* killer is only ever seen by his own side.  Money made is in its owner's colour, as retail's was,
	* so a team game says whose truck that was, and money taken is red whoever lost it. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addMoneyText( Int amount, const Coord3D *pos, const Player *owner )
{
	if( LogicThread::defersEffects() )
	{
		const Coord3D at = *pos;
		LogicThread::callAfterTick( [=]() { addMoneyText( amount, &at, owner ); } );
		return;
	}
	const Player *local = ThePlayerList->getLocalPlayer();
	if( amount == 0 || owner == NULL || local == NULL )
		return;
	if( owner != local && local->isPlayerActive() && local->getRelationship( owner->getDefaultTeam() ) != ALLIES )
		return;

	// a gain in the owner's colour, as retail drew it, lifted only where it is too dark to read
	const Color LOSS_RED = GameMakeColor( 255, 48, 48, 255 );			// the red the truce is written in
	const Color color = amount > 0 ? UIStyle::readable( clientPlayerColor( owner ) ) : LOSS_RED;

	UnicodeString text;
	text.format( TheGameText->fetch( amount > 0 ? "GUI:AddCash" : "GUI:LoseCash" ), amount > 0 ? amount : -amount );
	addFloatingText( text, pos, color );
}

/** Each signal's mark on the ground, by SignalKind: Art/Textures/<name>.tga, drawn by
	* Tools/signal_marks.py, white where the sender's colour goes. */
static const char *const SIGNAL_MARK_TEXTURES[ SIGNAL_KIND_COUNT ] =
{
	"ReforgedSignalAttack", "ReforgedSignalDefend", "ReforgedSignalLook"
};

enum
{
	SIGNAL_MARK_SIZE						= 160,	///< across, in world units: 90 read as a coin from the default camera
	SIGNAL_MARK_OPACITY					= 230	///< out of 255, so the ground under it still shows a little
};
#define SIGNAL_MARK_FADE_OUT_FRAMES ((Int)(LOGICFRAMES_PER_GAME_SECOND * 2))	///< the smoke's last steps, over which the mark fades

//-------------------------------------------------------------------------------------------------
/** The mark a signal lays on the ground: crossed swords, a shield or an eye in a ring, in `color`,
	* turned to this viewer's camera as it stands now so it reads upright. */
//-------------------------------------------------------------------------------------------------
void InGameUI::addSignalMark( SignalKind kind, const Coord3D &pos, Color color, ParticleSystemID smoke )
{
	LOGIC_AFTER_TICK( [=]() { addSignalMark( kind, pos, color, smoke ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	// No decal manager without a renderer (headless, or off Windows until the D track): no mark.
	// GameLogicDispatch calls this for every signal a player sends, replays included.
	if( TheProjectedShadowManager == NULL )
		return;

	Shadow::ShadowTypeInfo decalInfo;
	decalInfo.allowUpdates = FALSE;
	decalInfo.allowWorldAlign = TRUE;		// wrapped over the terrain it lands on
	// SHADOW_ALPHA_DECAL is the kind setColor and setOpacity reach, and removeShadow takes off
	decalInfo.m_type = SHADOW_ALPHA_DECAL;
	strcpy( decalInfo.m_ShadowName, SIGNAL_MARK_TEXTURES[ kind ] );
	decalInfo.m_sizeX = SIGNAL_MARK_SIZE;
	decalInfo.m_sizeY = SIGNAL_MARK_SIZE;
	decalInfo.m_offsetX = 0.0f;
	decalInfo.m_offsetY = 0.0f;

	SignalMark mark;
	mark.decal = TheProjectedShadowManager->addDecal( &decalInfo );
	mark.decal->setAngle( TheTacticalView->getAngle() );
	mark.decal->setColor( color );
	mark.decal->setOpacity( 0 );
	mark.decal->setPosition( pos.x, pos.y, pos.z );
	mark.smoke = smoke;
	m_signalMarks.push_back( mark );
}

//-------------------------------------------------------------------------------------------------
/** Each mark lasts as long as its smoke and fades out over the smoke's last steps.  Both can't
	* simply count logic frames: ParticleSystemManager::update steps once per pass that sees a new
	* logic frame, so a network game catching up several frames in one pass keeps its smoke longer
	* than the frame count says, and a mark on its own clock went a second early or late.  The smoke
	* has left in it what it still has to emit plus the life of its youngest puff.  Hidden while
	* scripts or -cinema hide the icons, like a radius decal. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateSignalMarks( void )
{
	const Bool shown = TheGameLogic->getDrawIconUI() && !CinemaDirector_hidesHud();
	for( size_t index = 0; index < m_signalMarks.size(); )
	{
		SignalMark &mark = m_signalMarks[ index ];
		const ParticleSystem *smoke = TheParticleSystemManager->findParticleSystem( mark.smoke );
		if( smoke == NULL )
		{
			mark.decal->release();
			m_signalMarks.erase( m_signalMarks.begin() + index );
			continue;
		}

		// puffs the GPU took have no Particle, and their system says how long the last of them lives
		const Particle *youngest = smoke->getLastParticle();
		const UnsignedInt puffLeft = max( youngest ? youngest->getLifetimeLeft() : 0, smoke->getGpuLifetimeLeft() );
		const UnsignedInt left = smoke->getSystemLifetimeLeft() + puffLeft;
		const UnsignedInt opacity = min( (UnsignedInt)SIGNAL_MARK_OPACITY, left * SIGNAL_MARK_OPACITY / SIGNAL_MARK_FADE_OUT_FRAMES );
		mark.decal->setOpacity( shown ? (Int)opacity : 0 );
		index++;
	}
}

void InGameUI::clearSignalMarks( void )
{
	for( size_t index = 0; index < m_signalMarks.size(); index++ )
		m_signalMarks[ index ].decal->release();
	m_signalMarks.clear();
}

//-------------------------------------------------------------------------------------------------
/** The same logic message as the Alt+Z/X/C keys, so the same throttle and the same allies see it. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::placeArmedSignal( const Coord3D &world )
{
	if( !isSignalArmed() )
		return FALSE;

	GameMessage *message = TheMessageStream->appendMessage( GameMessage::MSG_PLACE_SIGNAL );
	message->appendLocationArgument( world );
	message->appendIntegerArgument( m_armedSignal );
	disarmSignal();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
inline Bool isClose(Real a, Real b) { return fabs(a-b) <= 1.0f; }
inline Bool isClose(const Coord3D& a, const Coord3D& b) 
{
		return	isClose(a.x, b.x) && 
			isClose(a.y, b.y) && 
			isClose(a.z, b.z);
}
void InGameUI::DEBUG_addFloatingText(const AsciiString& text, const Coord3D * pos, Color color)
{
	if( LogicThread::defersEffects() )
	{
		const Coord3D at = *pos;
		LogicThread::callAfterTick( [=]() { DEBUG_addFloatingText( text, &at, color ); } );
		return;
	}
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	const Int POINTSIZE = 8;
	const Int LEADING = 0;

	Coord3D posToUse = *pos;

try_again:
	for (FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end(); ++it)
	{
		if (isClose((*it)->m_pos3D, posToUse))
		{
			posToUse.z -= (POINTSIZE + LEADING);
			goto try_again;
		}
	}

	FloatingTextData *newFTD = newInstance( FloatingTextData );
	newFTD->m_color = clientColor( color );
	newFTD->m_pos3D.x = posToUse.x;
	newFTD->m_pos3D.y = posToUse.y;
	newFTD->m_pos3D.z = posToUse.z;
	UnicodeString translate;
	translate.translate(text);
	newFTD->m_text = translate;
	newFTD->m_dString->setText(translate);
	newFTD->m_dString->setFont(TheWindowManager->winFindFont( AsciiString("Arial"), POINTSIZE, FALSE ));
	newFTD->m_bornMs = Clock_Milliseconds();

	m_floatingTextList.push_front( newFTD ); // add to the list

	//DEBUG_LOG(("%s\n",text.str()));
}
#endif

//-------------------------------------------------------------------------------------------------
/** modify the position of our floating text */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateFloatingText( void )
{
	const UnsignedInt now = Clock_Milliseconds();
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end();)
	{
		FloatingTextData *ftd = *it;
		if( floatingTextAlpha( now - ftd->m_bornMs, m_floatingTextTimeOut.raw(), m_floatingTextMoveVanishRate.raw() ) <= 0 )
		{
			it = m_floatingTextList.erase(it);
			ftd->deleteInstance();
			continue;
		}
		++it;
	}
}

//-------------------------------------------------------------------------------------------------
/** Itterates through and draws each floating text */
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** The one red the truce is written in, wherever it is written. */
//-------------------------------------------------------------------------------------------------
static Color peaceTimeColor( Int alpha )
{
	return GameMakeColor( 255, 48, 48, alpha );
}

// -directorrecord's broadcast is drawn in zerohour.gg's colours: the ground, its panel, ink, muted
// text, the line and the gold the pane lines are drawn in.  The ground is opaque: at the site's veil
// the gold of the pane lines under it showed through the score bar's corners.  The site sets words in
// Chivo and numbers in JetBrains Mono; Windows has neither, and these are the site's own fallbacks
static const Color BROADCAST_GROUND = GameMakeColor( 0x0c, 0x12, 0x20, 255 );
static const Color BROADCAST_PANEL = GameMakeColor( 0x12, 0x1a, 0x2b, 255 );
static const Color BROADCAST_LINE = GameMakeColor( 0x1f, 0x29, 0x40, 255 );
static const Color BROADCAST_INK = GameMakeColor( 0xe8, 0xea, 0xf0, 255 );
static const Color BROADCAST_MUTED = GameMakeColor( 0x8e, 0x97, 0xad, 255 );
static const Color BROADCAST_GOLD = GameMakeColor( 0xf2, 0xc2, 0x30, 255 );
/// the score bar's frame is cut to match the split screen's lines it outlived: the brand's blue band
/// as a halo at these strengths, a pixel of the ground a 720 rows and half the lines' gold, on the
/// three sides that are on the screen
static const UnsignedByte BROADCAST_BAND_RGB[ 3 ] = { 0x80, 0x95, 0xea };
static const Real BROADCAST_BAND_OUTER_ALPHA = 0.3f;
static const Real BROADCAST_BAND_INNER_ALPHA = 0.55f;
static const Real BROADCAST_FRAME_EDGE_ROWS_A_PIXEL = 720.0f;
static const char *const BROADCAST_WORDS = "Segoe UI";
static const char *const BROADCAST_NUMBERS = "Consolas";
/// the broadcast's sizes are a 720 row picture's, grown with the picture's height
static const Real BROADCAST_ROWS = 720.0f;
static const Int BROADCAST_NAME_POINTS = 11;
static const Int BROADCAST_SIDE_POINTS = 9;
static const Int BROADCAST_HEAD_POINTS = 7;
/// the clock was 16 and the armies' bar 6 rows; both gave up height when the bar was thinned
static const Int BROADCAST_CLOCK_POINTS = 13;
static const Real BROADCAST_PAD = 8.0f;
static const Real BROADCAST_GAP = 10.0f;
static const Real BROADCAST_RULE = 2.0f;
static const Real BROADCAST_TUG = 4.0f;
static const Int THOUSANDS = 3;
static const Int BROADCAST_WIDEST = 888888;	///< the number the cash and army columns are measured on

/** A whole number with its thousands parted by commas, after prefix. */
static UnicodeString broadcastNumber( const char *prefix, Int value )
{
	std::string digits = std::to_string( value );
	for( Int at = (Int)digits.size() - THOUSANDS; at > 0; at -= THOUSANDS )
		digits.insert( at, "," );
	UnicodeString text;
	text.translate( AsciiString( ( prefix + digits ).c_str() ) );
	return text;
}

/** color with its opacity taken down to share of what it was. */
static Color broadcastFade( Color color, Real share )
{
	UnsignedByte red, green, blue, alpha;
	GameGetColorComponents( color, &red, &green, &blue, &alpha );
	return GameMakeColor( red, green, blue, (UnsignedByte)REAL_TO_INT( alpha * share ) );
}

/** A size given for the 720 row picture, in this picture's pixels and font points. */
static Int broadcastPixels( Real at720 )
{
	return max( REAL_TO_INT( at720 * ( TheDisplay->getHeight() / BROADCAST_ROWS ) ), 1 );
}

static Int broadcastPoints( Int at720 )
{
	return REAL_TO_INT( at720 * ( TheDisplay->getHeight() / BROADCAST_ROWS ) );
}

/** What the broadcast calls a player and what goes beside it.  Every AI's name is its difficulty, so
	* a plate read "Hard AI vs Hard AI": an AI is called by its general, the difficulty beside it.  A
	* computer seat given a name of its own (-seatname) is called by it, as a player is. */
static Bool broadcastByDifficulty( Player *player )
{
	const UnicodeString &name = player->getPlayerDisplayName();
	if( player->getPlayerType() != PLAYER_COMPUTER )
		return FALSE;
	if( name.compare( SlotStateName( SLOT_EASY_AI ) ) == 0 || name.compare( SlotStateName( SLOT_MED_AI ) ) == 0
		|| name.compare( SlotStateName( SLOT_BRUTAL_AI ) ) == 0 )
		return TRUE;
	for( Int i = 0; i < AI_LEVELS_ABOVE_HARD; ++i )
	{
		if( name.compare( SlotStateName( SLOT_BRUTAL_AI, AILevelAboveHardRating[i] ) ) == 0 )
			return TRUE;
	}
	return FALSE;
}

static UnicodeString broadcastName( Player *player )
{
	return broadcastByDifficulty( player ) ? player->getPlayerTemplate()->getDisplayName() : player->getPlayerDisplayName();
}

static UnicodeString broadcastSide( Player *player )
{
	return broadcastByDifficulty( player ) ? player->getPlayerDisplayName() : player->getPlayerTemplate()->getDisplayName();
}

/// U+2026 by number: written as a character in a u"" literal, MSVC read this file in the machine's
/// code page and the bar drew the three characters of its UTF-8 bytes
static const WideChar BROADCAST_ELLIPSIS[] = { 0x2026, 0 };
static const WideChar HIGH_SURROGATE_FIRST = 0xD800;
static const WideChar HIGH_SURROGATE_LAST = 0xDBFF;

/** full's text as it fits widest pixels: whole, or as many of its first characters as fit with
	* ellipsis, a string holding the ellipsis in full's font, after them.  Spaces before the ellipsis
	* go, and a character outside the 16 bit range is never cut in half. */
static UnicodeString broadcastFitted( DisplayString *full, DisplayString *ellipsis, Int widest )
{
	UnicodeString text = full->getText();
	if( full->getWidth() <= widest )
		return text;
	std::vector< Int > prefixWidths( text.getLength() + 1, 0 );
	for( Int count = 1; count <= text.getLength(); count++ )
		prefixWidths[ count ] = full->getWidth( count );
	const Int kept = ObserverCamera_fitCount( prefixWidths, ellipsis->getWidth(), widest );
	while( text.getLength() > kept )
		text.removeLastChar();
	auto dangles = [ &text ]() -> Bool
	{
		const WideChar last = text.getCharAt( text.getLength() - 1 );
		return last == u' ' || ( last >= HIGH_SURROGATE_FIRST && last <= HIGH_SURROGATE_LAST );
	};
	while( text.getLength() > 0 && dangles() )
		text.removeLastChar();
	text.concat( BROADCAST_ELLIPSIS );
	return text;
}

/** How many of players are on team. */
static Int broadcastTeamSize( const std::vector< SpectatorStats > &players, Int team )
{
	Int size = 0;
	for( size_t index = 0; index < players.size(); index++ )
		size += players[ index ].team == team ? 1 : 0;
	return size;
}

/** A team of two or more by its letter, Team A the first such team in team order; a player alone has
	* no team name. */
static UnicodeString broadcastTeamName( const std::vector< SpectatorStats > &players, Int team )
{
	Int letter = 'A';
	for( Int earlier = 0; earlier < team; earlier++ )
		letter += broadcastTeamSize( players, earlier ) >= 2 ? 1 : 0;
	AsciiString name;
	name.format( "Team %c", letter );
	UnicodeString text;
	text.translate( name );
	return text;
}

/// the ground's edge round a gold player's swatch, in 720 line pixels: one pixel was lost at 720p
static const Real BROADCAST_GOLD_EDGE = 2.0f;

/** A player's colour as a filled rectangle; one near the lines' gold gets an edge of the ground round
	* it, outside so a swatch three pixels wide keeps its colour, and over its neighbours' ends in the
	* army bar so it does not run into them. */
static void drawBroadcastSwatch( Int left, Int top, Int width, Int height, Color color )
{
	if( width <= 0 )
		return;
	if( ObserverCamera_nearBrandGold( color ) )
	{
		const Int edge = max( broadcastPixels( BROADCAST_GOLD_EDGE ), 2 );
		TheDisplay->drawFillRect( left - edge, top - edge, width + edge * 2, height + edge * 2, BROADCAST_GROUND );
	}
	TheDisplay->drawFillRect( left, top, width, height, color );
}

/// a label on the picture: pieces of text side by side, each its colour, on the ground with the gold
/// rule along its bottom
struct BroadcastPlate
{
	std::vector< DisplayString * > pieces;
	std::vector< Color > colors;
};

/** The plate's text alone, its pieces half a gap apart: their width and the tallest's height. */
static void broadcastPiecesSize( const BroadcastPlate &plate, Int *width, Int *height )
{
	*width = *height = 0;
	for( size_t piece = 0; piece < plate.pieces.size(); piece++ )
	{
		Int pieceWidth = 0, pieceHeight = 0;
		plate.pieces[ piece ]->getSize( &pieceWidth, &pieceHeight );
		*width += pieceWidth + ( piece > 0 ? broadcastPixels( BROADCAST_GAP ) / 2 : 0 );
		*height = max( *height, pieceHeight );
	}
}

/** The plate's text from left, each piece centred down a line height rows high from top, faded to
	* shown, its shadow in ground. */
static void drawBroadcastPieces( const BroadcastPlate &plate, Int left, Int top, Int height, Real shown, Color ground )
{
	Int x = left;
	for( size_t piece = 0; piece < plate.pieces.size(); piece++ )
	{
		Int pieceWidth = 0, pieceHeight = 0;
		plate.pieces[ piece ]->getSize( &pieceWidth, &pieceHeight );
		plate.pieces[ piece ]->draw( x, top + ( height - pieceHeight ) / 2, broadcastFade( plate.colors[ piece ], shown ),
			broadcastFade( ground, shown ) );
		x += pieceWidth + broadcastPixels( BROADCAST_GAP ) / 2;
	}
}

static void broadcastPlateSize( const BroadcastPlate &plate, Int *width, Int *height )
{
	Int textWidth = 0, textHeight = 0;
	broadcastPiecesSize( plate, &textWidth, &textHeight );
	*width = textWidth + 2 * broadcastPixels( BROADCAST_PAD );
	*height = textHeight + broadcastPixels( BROADCAST_PAD ) + broadcastPixels( BROADCAST_RULE );
}

/** The plate with its top left at left, top, faded to shown. */
static void drawBroadcastPlate( const BroadcastPlate &plate, Int left, Int top, Real shown )
{
	const Int pad = broadcastPixels( BROADCAST_PAD );
	const Int rule = broadcastPixels( BROADCAST_RULE );
	Int width = 0, height = 0;
	broadcastPlateSize( plate, &width, &height );
	TheDisplay->drawFillRect( left, top, width, height, broadcastFade( BROADCAST_GROUND, shown ) );
	TheDisplay->drawFillRect( left, top + height - rule, width, rule, broadcastFade( BROADCAST_GOLD, shown ) );
	drawBroadcastPieces( plate, left + pad, top + pad / 2, height - pad - rule, shown, BROADCAST_GROUND );
}

/// the broadcast's red, for a defeat and nothing else: a struck card and the defeat banner's tab
static const Color BROADCAST_DEFEAT = GameMakeColor( 0xb8, 0x26, 0x1f, 255 );
/// the defeat and winner banners: the name's points, the tab's word's, and where the defeat banner's
/// foot sits, a share of the picture's height down; the winner's hangs under it
static const Int BROADCAST_BANNER_POINTS = 16;
static const Int BROADCAST_BANNER_WORD_POINTS = 12;
static const Real BROADCAST_BANNER_FOOT = 0.7f;

/** A banner's height with title over under, its rule included. */
static Int broadcastBannerHeight( const BroadcastPlate &title, const BroadcastPlate &under )
{
	Int width = 0, titleHeight = 0, underHeight = 0;
	broadcastPiecesSize( title, &width, &titleHeight );
	broadcastPiecesSize( under, &width, &underHeight );
	return titleHeight + underHeight + broadcastPixels( BROADCAST_PAD ) + broadcastPixels( BROADCAST_RULE );
}

/** A banner across the lower picture for a defeat or the winner, centred, its foot on the row foot: a
	* tab of colour holding word, beside it title over the smaller under, on the ground with a rule of the
	* tab's colour along its foot.  It opens as the rule grows out from its middle, the panel rises off the
	* rule and the words come last; shown runs it from 0 to 1, and going back down closes it the same way.
	* The rectangle it takes on the picture. */
static IRegion2D drawBroadcastBanner( DisplayString *word, Color tab, Color wordColor, const BroadcastPlate &title,
	const BroadcastPlate &under, Int foot, Real shown )
{
	const Int pad = broadcastPixels( BROADCAST_PAD );
	const Int rule = broadcastPixels( BROADCAST_RULE );
	Int wordWidth = 0, wordHeight = 0, titleWidth = 0, titleHeight = 0, underWidth = 0, underHeight = 0;
	word->getSize( &wordWidth, &wordHeight );
	broadcastPiecesSize( title, &titleWidth, &titleHeight );
	broadcastPiecesSize( under, &underWidth, &underHeight );
	const Int tabWidth = wordWidth + 2 * pad;
	const Int width = tabWidth + pad + max( titleWidth, underWidth ) + pad;
	const Int height = broadcastBannerHeight( title, under ) - rule;
	const Int left = ( (Int)TheDisplay->getWidth() - width ) / 2;
	const Int top = foot - rule - height;

	const Int ruleWidth = REAL_TO_INT( width * ObserverCamera_easeBetween( shown, 0.0f, 0.45f ) );
	TheDisplay->drawFillRect( left + ( width - ruleWidth ) / 2, foot - rule, ruleWidth, rule, tab );
	const Int panelHeight = REAL_TO_INT( height * ObserverCamera_easeBetween( shown, 0.3f, 0.85f ) );
	if( panelHeight > 0 )
	{
		TheDisplay->drawFillRect( left, foot - rule - panelHeight, width, panelHeight, BROADCAST_GROUND );
		TheDisplay->drawFillRect( left, foot - rule - panelHeight, tabWidth, panelHeight, tab );
	}
	const Real words = ObserverCamera_easeBetween( shown, 0.8f, 1.0f );
	if( words > 0.0f )
	{
		word->draw( left + pad, top + ( height - wordHeight ) / 2, broadcastFade( wordColor, words ), broadcastFade( tab, words ) );
		drawBroadcastPieces( title, left + tabWidth + pad, top + pad / 2, titleHeight, words, BROADCAST_GROUND );
		drawBroadcastPieces( under, left + tabWidth + pad, top + pad / 2 + titleHeight, underHeight, words, BROADCAST_GROUND );
	}
	IRegion2D drawn;
	drawn.lo.x = left;
	drawn.lo.y = top;
	drawn.hi.x = left + width;
	drawn.hi.y = foot;
	return drawn;
}

//-------------------------------------------------------------------------------------------------
DisplayString *InGameUI::broadcastText( const std::string &key, const UnicodeString &text, const char *font, Int points, Bool bold )
{
	DisplayString *&string = m_broadcastTexts[ key ];
	if( string == NULL )
	{
		string = TheDisplayStringManager->newDisplayString();
		string->setFont( TheFontLibrary->getFont( AsciiString( font ), points, bold ) );
	}
	string->setText( text );
	return string;
}

/// one player's card of the score bar
struct BroadcastRow
{
	const SpectatorStats *stats;
	Color color;
	DisplayString *name;
	DisplayString *side;
	DisplayString *cash;
	DisplayString *army;
};

/// the score bar's sizes, largest first, in 720 line points and pixels: a row of cards takes the first
/// that fits the screen's width
static const Int BROADCAST_CARD_STEPS = 4;
static const Int BROADCAST_CARD_NAME_POINTS[ BROADCAST_CARD_STEPS ] = { 11, 10, 9, 8 };
static const Int BROADCAST_CARD_DETAIL_POINTS[ BROADCAST_CARD_STEPS ] = { 9, 8, 8, 7 };
static const Real BROADCAST_CARD_PAD[ BROADCAST_CARD_STEPS ] = { 6.0f, 5.0f, 4.0f, 3.0f };
/// the widest a player's own name may be where the broadcast writes it, in 720 line pixels for a name of
/// BROADCAST_NAME_POINTS and in proportion at other sizes: a longer one ends in an ellipsis, so one long
/// name neither widens every card nor runs into the general beside it or across the picture.  A
/// computer player's name is his general and is never cut: "GLA Demolition General" is 171 at 11
/// points.  A banner has the picture's middle
static const Real BROADCAST_CARD_NAME_WIDEST = 140.0f;
static const Real BROADCAST_BANNER_NAME_WIDEST = 210.0f;
/// the house colour along a card's top
static const Real BROADCAST_CARD_STRIP = 2.0f;
/// the line through a defeated player's name, in 720 line pixels, with a pixel of the ground each side
static const Real BROADCAST_STRIKE = 2.0f;
/// a power's flag under a card: its icon this many 720 rows high, with this much of the panel round it
static const Real BROADCAST_FLAG_ICON = 30.0f;
static const Real BROADCAST_FLAG_INSET = 3.0f;

//-------------------------------------------------------------------------------------------------
DisplayString *InGameUI::broadcastNameText( const std::string &key, Player *player, Int points, Real widest )
{
	const Int screenPoints = broadcastPoints( points );
	UnicodeString name = broadcastName( player );
	if( !broadcastByDifficulty( player ) )
		name = broadcastFitted( broadcastText( "full" + key, name, BROADCAST_WORDS, screenPoints, TRUE ),
			broadcastText( "ellipsis" + std::to_string( screenPoints ), UnicodeString( BROADCAST_ELLIPSIS ), BROADCAST_WORDS, screenPoints, TRUE ),
			broadcastPixels( widest * points / BROADCAST_NAME_POINTS ) );
	return broadcastText( key, name, BROADCAST_WORDS, screenPoints, TRUE );
}

/// the score bar laid out for one set of players: their cards' strings, every block's and card's place on
/// the screen and the bar's measures.  While a defeated player's card closes the bar is drawn between
/// two, with that card and without it
struct BroadcastLayout
{
	std::vector< SpectatorStats > players;
	std::vector< BroadcastRow > rows;
	std::vector< Int > blockFirst, blockSizes, blockArmies, lineOf;
	std::vector< DisplayString * > teamNames, teamTotals;
	std::vector< Int > blockLefts, blockWidths, blockHeaderTops, blockCardsTops;
	std::vector< Int > cardLefts, cardTops, cardLines;
	DisplayString *armyHead = NULL;
	DisplayString *versusText = NULL;
	Bool compact = FALSE;
	Int step = 0, lines = 0, cardWidth = 0, fullCardWidth = 0, cardHeight = 0, cardPad = 0, halfPad = 0, versusWidth = 0;
	Int nameHeight = 0, sideHeight = 0, numberHeight = 0, labelHeight = 0, textHeight = 0, strip = 0;
	Int left = 0, barWidth = 0, height = 0, rowLeft = 0, rowWidth = 0, tugTop = 0;
};

/// a card's rectangle on the screen
struct BroadcastPlace
{
	Int left, top, width, height;
};

//-------------------------------------------------------------------------------------------------
/** The score bar laid out for players into l: every side in one row (two rows of one-line cards from
	* five players), its cards sized to the largest step that fits the screen, under the clock's tab of
	* clockBox by clockBoxHeight.  Team letters come from named, everyone who played; tag keeps this
	* layout's team totals apart from another's drawn in the same frame. */
//-------------------------------------------------------------------------------------------------
void InGameUI::layOutBroadcast( const std::vector< SpectatorStats > &players, const std::vector< SpectatorStats > &named,
	const std::string &tag, Int clockBox, Int clockBoxHeight, BroadcastLayout &l )
{
	const Int pad = broadcastPixels( BROADCAST_PAD );
	const Int tug = broadcastPixels( BROADCAST_TUG );
	l.players = players;
	// a block is one side: a team, or a player on his own, its players' cards side by side and a "vs"
	// between two blocks
	for( size_t index = 0; index < l.players.size(); index++ )
	{
		if( index == 0 || l.players[ index ].team != l.players[ index - 1 ].team )
		{
			l.blockFirst.push_back( (Int)index );
			l.blockSizes.push_back( 0 );
			l.blockArmies.push_back( 0 );
		}
		l.blockSizes.back()++;
		l.blockArmies.back() += l.players[ index ].army;
	}
	const Int blocks = (Int)l.blockSizes.size();

	// five players or more stand in two rows, whole blocks to a row, and their cards keep one line each:
	// the name at the left, the army's value at the right, no general and no cash.  One row of eight
	// two-line cards fell to 7 point text at 720, and one line with the cash had to be cut to fit
	l.lineOf = ObserverCamera_cardRows( l.blockSizes );
	l.lines = l.lineOf.back() + 1;
	l.compact = l.lines > 1;
	std::vector< std::vector< Int > > lineSizes( l.lines );
	for( Int block = 0; block < blocks; block++ )
		lineSizes[ l.lineOf[ block ] ].push_back( l.blockSizes[ block ] );

	// every card's text at a size, and the card width it needs: the widest of the name, the general or
	// difficulty, the money line (cash, then the army's label and value) at six digits each, and a team
	// header spread over its block's cards.  Each size keeps strings of its own, since a string keeps
	// the font it was made with
	l.rows.assign( l.players.size(), BroadcastRow() );
	l.teamNames.assign( blocks, NULL );
	l.teamTotals.assign( blocks, NULL );
	Int widestWidth = 0;
	auto buildCards = [ & ]( Int step ) -> Int
	{
		const std::string size = "card" + std::to_string( step ) + ":";
		const Int cardPad = broadcastPixels( BROADCAST_CARD_PAD[ step ] );
		const Int detailPoints = broadcastPoints( BROADCAST_CARD_DETAIL_POINTS[ step ] );
		l.armyHead = broadcastText( size + "armyhead", TheGameText->fetch( "GUI:HudStatArmy" ), BROADCAST_WORDS,
			broadcastPoints( BROADCAST_HEAD_POINTS ), FALSE );
		l.versusText = broadcastText( size + "versus", UnicodeString( u"vs" ), BROADCAST_WORDS, detailPoints, FALSE );
		DisplayString *widest = broadcastText( size + "widest", broadcastNumber( "$", BROADCAST_WIDEST ), BROADCAST_NUMBERS, detailPoints, TRUE );
		Int width = 0, height = 0, labelWidth = 0;
		l.armyHead->getSize( &labelWidth, &height );
		widest->getSize( &widestWidth, &height );
		Int content = l.compact ? 0 : widestWidth + cardPad + labelWidth + cardPad / 2 + widestWidth;
		for( size_t index = 0; index < l.players.size(); index++ )
		{
			const SpectatorStats &stats = l.players[ index ];
			const std::string seat = size + std::to_string( stats.player->getPlayerIndex() );
			BroadcastRow &row = l.rows[ index ];
			row.stats = &stats;
			row.color = clientPlayerColor( stats.player );
			row.name = broadcastNameText( "name" + seat, stats.player, BROADCAST_CARD_NAME_POINTS[ step ], BROADCAST_CARD_NAME_WIDEST );
			row.side = broadcastText( "side" + seat, broadcastSide( stats.player ), BROADCAST_WORDS, detailPoints, FALSE );
			row.cash = broadcastText( "cash" + seat, stats.unlimited ? TheGameText->fetch( "GUI:StartingMoneyUnlimited" ) : broadcastNumber( "$", stats.cash ), BROADCAST_NUMBERS, detailPoints, TRUE );
			row.army = broadcastText( "army" + seat, broadcastNumber( "$", stats.army ), BROADCAST_NUMBERS, detailPoints, TRUE );
			row.name->getSize( &width, &height );
			if( l.compact )
			{
				content = max( content, width + cardPad + widestWidth );
				continue;
			}
			// the general or difficulty stands at the right end of the name's line
			Int sideWidth = 0;
			row.side->getSize( &sideWidth, &height );
			content = max( content, width + cardPad + sideWidth );
		}
		for( Int block = 0; block < blocks; block++ )
		{
			if( l.blockSizes[ block ] < 2 )
				continue;
			// keyed by the team and the layout, not the block's place: while a card closes both layouts are
			// up, and a team's total is not the same in the two
			const Int team = l.players[ l.blockFirst[ block ] ].team;
			const std::string key = size + tag + std::to_string( team );
			l.teamNames[ block ] = broadcastText( "teamname" + key, broadcastTeamName( named, team ), BROADCAST_WORDS, detailPoints, TRUE );
			l.teamTotals[ block ] = broadcastText( "teamtotal" + key, broadcastNumber( "$", l.blockArmies[ block ] ), BROADCAST_NUMBERS, detailPoints, TRUE );
			// the total measured as six digits, as the cards' numbers are, so the width holds still
			Int nameWidth = 0;
			l.teamNames[ block ]->getSize( &nameWidth, &height );
			const Int header = nameWidth + cardPad + widestWidth;
			const Int cards = l.blockSizes[ block ];
			content = max( content, ( header - ( cards - 1 ) * cardPad + cards - 1 ) / cards - 2 * cardPad );
		}
		return content + 2 * cardPad;
	};

	// the largest size whose row fits the screen less the bar's padding; past the smallest the cards are
	// cut to fit, so the bar never runs off the picture
	Int cardWidths[ BROADCAST_CARD_STEPS ], cardGaps[ BROADCAST_CARD_STEPS ], versusWidths[ BROADCAST_CARD_STEPS ];
	for( Int step = 0; step < BROADCAST_CARD_STEPS; step++ )
	{
		cardWidths[ step ] = buildCards( step );
		cardGaps[ step ] = broadcastPixels( BROADCAST_CARD_PAD[ step ] );
		Int versusWidth = 0, versusHeight = 0;
		l.versusText->getSize( &versusWidth, &versusHeight );
		versusWidths[ step ] = versusWidth + 4 * cardGaps[ step ];
	}
	const Int room = (Int)TheDisplay->getWidth() - 4 * pad;
	l.step = 0;
	for( Int line = 0; line < l.lines; line++ )
		l.step = max( l.step, ObserverCamera_cardStep( lineSizes[ line ], cardWidths, cardGaps, versusWidths, BROADCAST_CARD_STEPS, room ) );
	buildCards( l.step );
	l.cardPad = cardGaps[ l.step ];
	l.versusWidth = versusWidths[ l.step ];
	std::vector< Int > lineCards, lineBlocks;
	l.cardWidth = cardWidths[ l.step ];
	l.fullCardWidth = l.cardWidth;
	std::vector< Int > lineWidths( l.lines, 0 );
	for( Int line = 0; line < l.lines; line++ )
		lineWidths[ line ] = ObserverCamera_cardRow( lineSizes[ line ], l.cardWidth, l.cardPad, l.versusWidth, &lineCards, &lineBlocks );
	l.rowWidth = *std::max_element( lineWidths.begin(), lineWidths.end() );
	if( l.rowWidth > room )
	{
		for( Int line = 0; line < l.lines; line++ )
			l.cardWidth = min( l.cardWidth, ObserverCamera_cardWidthIn( lineSizes[ line ], l.cardPad, l.versusWidth, room ) );
		for( Int line = 0; line < l.lines; line++ )
			lineWidths[ line ] = ObserverCamera_cardRow( lineSizes[ line ], l.cardWidth, l.cardPad, l.versusWidth, &lineCards, &lineBlocks );
		l.rowWidth = *std::max_element( lineWidths.begin(), lineWidths.end() );
	}
	// every block's and every card's left across the bar, a shorter line in the middle under the longer
	std::vector< Int > cardLefts( l.players.size(), 0 ), blockLefts( blocks, 0 );
	for( Int line = 0; line < l.lines; line++ )
	{
		ObserverCamera_cardRow( lineSizes[ line ], l.cardWidth, l.cardPad, l.versusWidth, &lineCards, &lineBlocks );
		const Int shift = ( l.rowWidth - lineWidths[ line ] ) / 2;
		Int blockInLine = 0, cardInLine = 0;
		for( Int block = 0; block < blocks; block++ )
		{
			if( l.lineOf[ block ] != line )
				continue;
			blockLefts[ block ] = shift + lineBlocks[ blockInLine++ ];
			for( Int index = l.blockFirst[ block ]; index < l.blockFirst[ block ] + l.blockSizes[ block ]; index++ )
				cardLefts[ index ] = shift + lineCards[ cardInLine++ ];
		}
	}

	// the bar's measures: the clock in a tab at the top in the middle, under it a line of team headers
	// when there are teams, the cards, a header line and a card line again for a second row, and the
	// armies' bar under all of it
	Int headerHeight = 0, width = 0;
	l.rows[ 0 ].name->getSize( &width, &l.nameHeight );
	l.rows[ 0 ].side->getSize( &width, &l.sideHeight );
	l.rows[ 0 ].cash->getSize( &width, &l.numberHeight );
	l.armyHead->getSize( &width, &l.labelHeight );
	l.numberHeight = max( l.numberHeight, l.labelHeight );
	for( Int block = 0; block < blocks; block++ )
		if( l.teamNames[ block ] != NULL )
			l.teamNames[ block ]->getSize( &width, &headerHeight );
	// half a card pad round the card's text and under the team names, and a pad's half under the
	// armies' bar: with a pad each and the money on two lines the bar took 125 rows of 720
	l.halfPad = max( l.cardPad / 2, 1 );
	if( headerHeight > 0 )
		headerHeight += l.halfPad;
	l.barWidth = max( l.rowWidth, clockBox ) + 2 * pad;
	l.left = ( (Int)TheDisplay->getWidth() - l.barWidth ) / 2;
	l.rowLeft = l.left + ( l.barWidth - l.rowWidth ) / 2;
	const Int headerTop = clockBoxHeight + l.halfPad;
	l.strip = broadcastPixels( BROADCAST_CARD_STRIP );
	// a card is two lines, the name with the general or difficulty at its right, then the money; the
	// general on a line of its own made the bar 100 rows of 720 tall for a 1v1
	l.textHeight = l.compact ? max( l.nameHeight, l.numberHeight ) : max( l.nameHeight, l.sideHeight ) + l.numberHeight;
	l.cardHeight = l.strip + l.halfPad + l.textHeight + l.halfPad;
	// a row's team headers take a line over it only when the row has a team
	std::vector< Int > lineHeaders( l.lines, 0 ), lineTops( l.lines, 0 );
	for( Int block = 0; block < blocks; block++ )
		if( l.teamNames[ block ] != NULL )
			lineHeaders[ l.lineOf[ block ] ] = headerHeight;
	l.tugTop = headerTop;
	for( Int line = 0; line < l.lines; line++ )
	{
		lineTops[ line ] = l.tugTop;
		l.tugTop += lineHeaders[ line ] + l.cardHeight + l.halfPad;
	}
	l.height = l.tugTop + tug + l.halfPad;

	// and every block and card where it stands on the screen
	l.blockLefts.assign( blocks, 0 );
	l.blockWidths.assign( blocks, 0 );
	l.blockHeaderTops.assign( blocks, 0 );
	l.blockCardsTops.assign( blocks, 0 );
	l.cardLefts.assign( l.players.size(), 0 );
	l.cardTops.assign( l.players.size(), 0 );
	l.cardLines.assign( l.players.size(), 0 );
	for( Int block = 0; block < blocks; block++ )
	{
		l.blockLefts[ block ] = l.rowLeft + blockLefts[ block ];
		l.blockWidths[ block ] = l.blockSizes[ block ] * l.cardWidth + ( l.blockSizes[ block ] - 1 ) * l.cardPad;
		l.blockHeaderTops[ block ] = lineTops[ l.lineOf[ block ] ];
		l.blockCardsTops[ block ] = l.blockHeaderTops[ block ] + lineHeaders[ l.lineOf[ block ] ];
		for( Int index = l.blockFirst[ block ]; index < l.blockFirst[ block ] + l.blockSizes[ block ]; index++ )
		{
			l.cardLefts[ index ] = l.rowLeft + cardLefts[ index ];
			l.cardTops[ index ] = l.blockCardsTops[ block ];
			l.cardLines[ index ] = l.lineOf[ block ];
		}
	}
}

// -directorrecord's corner window is drawn in the brand's video tokens (docs/reference/BRAND.md, Video):
// the plate, the recess behind the picture, the trim along the top, the chip's steel and the
// highlight round the picture.  No gold
static const Color WINDOW_PLATE = GameMakeColor( 0x17, 0x18, 0x1a, 255 );
static const Color WINDOW_SUNK = GameMakeColor( 0x0b, 0x0c, 0x0d, 255 );
static const Color WINDOW_TRIM = GameMakeColor( 0x5b, 0x7b, 0xe0, 255 );
static const Color WINDOW_STEEL = GameMakeColor( 0xc3, 0xc8, 0xce, 255 );
static const Color WINDOW_ACCENT = GameMakeColor( 0x6c, 0xc0, 0xea, 255 );
/// the chip's mono capitals, in 720 line points, and its hairline and the gap inside it in 720 pixels
static const Int WINDOW_CHIP_POINTS = 7;
static const Real WINDOW_CHIP_LINE = 1.0f;
static const Real WINDOW_CHIP_PAD = 3.0f;
/// U+00B7 by number, for the same reason as the ellipsis
static const WideChar WINDOW_CHIP_DOT[] = { ' ', 0x00B7, ' ', 0 };

/** text in capitals, the chip's mono labels. */
static UnicodeString windowCapitals( const UnicodeString &text )
{
	UnicodeString capitals;
	for( Int index = 0; index < text.getLength(); index++ )
		capitals.concat( (WideChar)WideCharToUpper( text.getCharAt( index ) ) );
	return capitals;
}

//-------------------------------------------------------------------------------------------------
/** -directorrecord's corner window at the lower right: a plate round the picture the recording
	* shrinks into it (W3DDisplay's joinVideoWindow), its top right corner cut at 45 degrees and a trim
	* rule along its top, a chip over the picture naming whose power or fight it is, and the highlight
	* round the picture.  The main draw alone, like the rest of the broadcast; its rectangles are the
	* broadcast's, so the recording keeps them, and the picture's own rectangle is left to the window. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawDirectorWindow( void )
{
	const IRegion2D window = TheObserverCamera.getWindowRect();
	if( window.hi.x <= window.lo.x )
		return;
	const DirectorWindowFrame frame = ObserverCamera_windowFrame( TheDisplay->getHeight() );
	const Int left = window.lo.x - frame.border;
	const Int right = window.hi.x + frame.border;
	const Int top = window.lo.y - frame.header;
	const Int bottom = window.hi.y + frame.border;
	const Int width = right - left;

	// the plate, a row at a time where the corner is cut, then the trim along the top up to the cut
	for( Int row = 0; row < frame.cut; row++ )
		TheDisplay->drawFillRect( left, top + row, width - ( frame.cut - row ), 1, WINDOW_PLATE );
	TheDisplay->drawFillRect( left, top + frame.cut, width, bottom - top - frame.cut, WINDOW_PLATE );
	TheDisplay->drawFillRect( left, top, width - frame.cut, frame.rule, WINDOW_TRIM );
	// the recess the picture goes into, which a frame with no picture of the window keeps
	TheDisplay->drawFillRect( window.lo.x, window.lo.y, window.hi.x - window.lo.x, window.hi.y - window.lo.y, WINDOW_SUNK );
	const Int line = frame.highlight;
	TheDisplay->drawFillRect( window.lo.x - line, window.lo.y - line, window.hi.x - window.lo.x + 2 * line, line, WINDOW_ACCENT );
	TheDisplay->drawFillRect( window.lo.x - line, window.hi.y, window.hi.x - window.lo.x + 2 * line, line, WINDOW_ACCENT );
	TheDisplay->drawFillRect( window.lo.x - line, window.lo.y, line, window.hi.y - window.lo.y, WINDOW_ACCENT );
	TheDisplay->drawFillRect( window.hi.x, window.lo.y, line, window.hi.y - window.lo.y, WINDOW_ACCENT );

	// the chip: the power's user, his name and his side; a fight's players, by name
	const PlayerMaskType sides = TheObserverCamera.getWindowSides();
	std::vector< Player * > players;
	for( Int index = 0; index < ThePlayerList->getPlayerCount(); index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( ( player->getPlayerMask() & sides ) != 0 && player->isPlayableSide() && !player->isPlayerObserver() )
			players.push_back( player );
	}
	if( !players.empty() )
	{
		UnicodeString text;
		if( players.size() == 1 )
		{
			text = broadcastName( players[ 0 ] );
			text.concat( WINDOW_CHIP_DOT );
			text.concat( broadcastSide( players[ 0 ] ) );
		}
		else
		{
			for( size_t index = 0; index < players.size(); index++ )
			{
				if( index > 0 )
					text.concat( UnicodeString( u" vs " ) );
				text.concat( broadcastName( players[ index ] ) );
			}
		}
		const Int points = broadcastPoints( WINDOW_CHIP_POINTS );
		const Int hairline = max( broadcastPixels( WINDOW_CHIP_LINE ), 1 );
		const Int pad = broadcastPixels( WINDOW_CHIP_PAD );
		const Int room = width - frame.cut - 2 * frame.border - 2 * ( pad + hairline );
		const UnicodeString capitals = windowCapitals( text );
		DisplayString *chip = broadcastText( "windowchip", broadcastFitted( broadcastText( "windowchipfull", capitals, BROADCAST_NUMBERS, points, TRUE ),
			broadcastText( "windowchipellipsis", UnicodeString( BROADCAST_ELLIPSIS ), BROADCAST_NUMBERS, points, TRUE ), room ),
			BROADCAST_NUMBERS, points, TRUE );
		Int chipWidth = 0, chipHeight = 0;
		chip->getSize( &chipWidth, &chipHeight );
		const Int boxWidth = chipWidth + 2 * ( pad + hairline );
		const Int boxHeight = min( chipHeight + 2 * hairline, frame.header - frame.rule - 2 * line );
		const Int boxLeft = left + frame.border;
		const Int boxTop = top + frame.rule + ( frame.header - frame.rule - line - boxHeight ) / 2;
		TheDisplay->drawOpenRect( boxLeft, boxTop, boxWidth, boxHeight, (Real)hairline, WINDOW_STEEL );
		chip->draw( boxLeft + hairline + pad, boxTop + ( boxHeight - chipHeight ) / 2, WINDOW_STEEL, WINDOW_PLATE );
	}

	// the plate's four sides are the broadcast's; the picture between them is the window's
	IRegion2D strip;
	strip.lo.x = left;
	strip.lo.y = top;
	strip.hi.x = right;
	strip.hi.y = window.lo.y;
	TheObserverCamera.addBroadcast( strip );
	strip.lo.y = window.hi.y;
	strip.hi.y = bottom;
	TheObserverCamera.addBroadcast( strip );
	strip.lo.y = window.lo.y;
	strip.hi.y = window.hi.y;
	strip.hi.x = window.lo.x;
	TheObserverCamera.addBroadcast( strip );
	strip.lo.x = window.hi.x;
	strip.hi.x = right;
	TheObserverCamera.addBroadcast( strip );
}

//-------------------------------------------------------------------------------------------------
/** The score bar hangs from the top edge in the middle: the match clock in a tab of its own, and under
	* it every side in one row (two rows of one-line cards from five players), "Team A vs Team B vs a player alone", a card a player with his name in
	* his colour, his general or difficulty, his cash and the cost of everything he has standing that is
	* not a building.  A team's cards sit together under its name and its armies' total.  Under the row
	* the armies pull on one bar, each player his colour in the cards' order, gold at the middle when
	* two sides play so the side ahead is the one past it.  A special power a player used, or an upgrade
	* he bought, drops its icon in a flag from under his card.  A player who loses has his card flash red and struck through, then
	* it closes while the others slide to where they stand without it, and his banner comes up across
	* the lower picture; once the match is decided the winner's comes up under the last.
	* Everything is read off the players and their objects; nothing here writes the logic. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawDirectorBroadcast( void )
{
	TheObserverCamera.clearBroadcast();
	// the corner window first, so a banner across the lower picture lies over it
	drawDirectorWindow();
	// everyone who played, the ones who lost included, so a team keeps its letter after a player of it is
	// gone; the bar holds the ones still in, and a defeated player's card until it has closed
	std::vector< SpectatorStats > named = gatherSpectatorStats( SPECTATOR_STATS[ 0 ], NULL, TheObserverCamera.getPlayedMask() );
	std::stable_sort( named.begin(), named.end(), []( const SpectatorStats &a, const SpectatorStats &b )
		{ return a.team != b.team ? a.team < b.team : a.player->getPlayerIndex() < b.player->getPlayerIndex(); } );
	const UnsignedInt frame = TheGameLogic->getFrame();
	std::vector< SpectatorStats > shown, settled;
	std::vector< Real > flashes, strikes;
	Real slide = 0.0f;
	for( size_t index = 0; index < named.size(); index++ )
	{
		const Int seat = named[ index ].player->getPlayerIndex();
		Real flash = 0.0f, struck = 0.0f, collapse = 0.0f;
		if( TheObserverCamera.getDefeatFrame( seat ) != 0 )
			ObserverCamera_cardExit( GameEngine_pictureFrame(), TheObserverCamera.getDefeatFrame( seat ), TheObserverCamera.getCollapseFrame( seat ),
				&flash, &struck, &collapse );
		if( collapse >= 1.0f )
			continue;
		shown.push_back( named[ index ] );
		flashes.push_back( flash );
		strikes.push_back( struck );
		if( collapse > 0.0f )
			slide = collapse;
		else
			settled.push_back( named[ index ] );
	}
	// the match is over: nobody is left to score
	if( shown.empty() )
		return;
	// cards close one at a time, so the bar is between two layouts at most: with that card and without it
	const Bool sliding = slide > 0.0f && !settled.empty();

	const Int rule = broadcastPixels( BROADCAST_RULE );
	const Int tug = broadcastPixels( BROADCAST_TUG );
	UnicodeString clockText;
	clockText.translate( AsciiString( spectatorClock( frame ).c_str() ) );
	DisplayString *clock = broadcastText( "clock", clockText, BROADCAST_NUMBERS, broadcastPoints( BROADCAST_CLOCK_POINTS ), TRUE );
	Int clockWidth = 0, clockHeight = 0;
	clock->getSize( &clockWidth, &clockHeight );
	const Int clockBox = clockWidth + 2 * broadcastPixels( BROADCAST_GAP );
	const Int clockBoxHeight = clockHeight + rule;

	BroadcastLayout from, to;
	layOutBroadcast( shown, named, "", clockBox, clockBoxHeight, from );
	if( sliding )
		layOutBroadcast( settled, named, "to", clockBox, clockBoxHeight, to );

	// the frame round the bar
	const Int screenRows = TheDisplay->getHeight();
	const Int lineGold = ObserverCamera_paneLineWidth( screenRows );
	const Int frameGold = max( lineGold / 2, 1 );
	const Int frameEdge = frameGold + max( REAL_TO_INT( screenRows / BROADCAST_FRAME_EDGE_ROWS_A_PIXEL ), 1 );
	const Int frameHalo = max( ( ObserverCamera_paneBandWidth( screenRows ) - lineGold ) / 2 - OBSERVER_PANE_LINE_EDGE, 0 ) / 2;
	const Int frameReach = frameEdge + frameHalo;
	// a card is drawn with the first layout's strings and measures over the first half of a slide and the
	// second's after: the same strings unless the size or the rows change
	const BroadcastLayout &face = sliding && slide >= 0.5f ? to : from;
	auto mix = [ & ]( Int first, Int second ) { return sliding ? first + REAL_TO_INT( ( second - first ) * slide ) : first; };
	auto seatIn = []( const BroadcastLayout &l, const Player *player ) -> Int
	{
		for( size_t index = 0; index < l.players.size(); index++ )
			if( l.players[ index ].player == player )
				return (Int)index;
		return -1;
	};
	auto teamIn = []( const BroadcastLayout &l, Int team ) -> Int
	{
		for( size_t block = 0; block < l.blockFirst.size(); block++ )
			if( l.players[ l.blockFirst[ block ] ].team == team )
				return (Int)block;
		return -1;
	};
	// a card in both layouts moves from its first place to its second; the closing one narrows on its own
	// middle and the others slide over it
	auto cardPlace = [ & ]( Int index ) -> BroadcastPlace
	{
		BroadcastPlace place = { from.cardLefts[ index ], from.cardTops[ index ], from.cardWidth, from.cardHeight };
		if( !sliding )
			return place;
		const Int other = seatIn( to, from.players[ index ].player );
		if( other < 0 )
		{
			const Int open = REAL_TO_INT( from.cardWidth * ( 1.0f - slide ) );
			place.left += ( from.cardWidth - open ) / 2;
			place.width = open;
			return place;
		}
		place.left = mix( from.cardLefts[ index ], to.cardLefts[ other ] );
		place.top = mix( from.cardTops[ index ], to.cardTops[ other ] );
		place.width = mix( from.cardWidth, to.cardWidth );
		place.height = mix( from.cardHeight, to.cardHeight );
		return place;
	};
	const Int left = mix( from.left, to.left );
	const Int barWidth = mix( from.barWidth, to.barWidth );
	const Int height = mix( from.height, to.height );
	const Int rowLeft = mix( from.rowLeft, to.rowLeft );
	const Int rowWidth = mix( from.rowWidth, to.rowWidth );
	const Int tugTop = mix( from.tugTop, to.tugTop );
	const Int clockLeft = left + ( barWidth - clockBox ) / 2;

	// a special power's flag, its button's icon on the panel, drops from the bar's foot under its
	// player's card; a superweapon's is edged in gold.  Drawn before the bar, which covers it until it is
	// clear.  In two rows the top row's flags hang a quarter card left and the bottom's a quarter right,
	// so a card and the one under it do not hang theirs on top of each other
	std::vector< IRegion2D > flagsDrawn;
	for( size_t index = 0; index < from.players.size(); index++ )
	{
		Real drop = 0.0f;
		const DirectorShowing *flag = TheObserverCamera.getPowerFlag( from.players[ index ].player->getPlayerIndex(), &drop );
		if( flag == NULL || drop <= 0.0f )
			continue;
		const Image *icon = flag->upgrade != NULL ? flag->upgrade->getButtonImage() : superweaponCameo( flag->power );
		if( icon == NULL && flag->sourceThing != NULL )
			icon = flag->sourceThing->getButtonImage();
		if( icon == NULL || icon->getImageHeight() <= 0 )
			continue;
		const BroadcastPlace place = cardPlace( (Int)index );
		const Int faceIndex = &face == &from ? (Int)index : seatIn( to, from.players[ index ].player );
		const Int quarter = face.lines > 1 ? ( face.cardLines[ faceIndex ] == 0 ? -place.width / 4 : place.width / 4 ) : 0;
		const Int iconHeight = broadcastPixels( BROADCAST_FLAG_ICON );
		const Int iconWidth = iconHeight * icon->getImageWidth() / icon->getImageHeight();
		const Int inset = broadcastPixels( BROADCAST_FLAG_INSET );
		const Int edge = flag->superweapon ? max( broadcastPixels( BROADCAST_GOLD_EDGE ), 2 ) : 0;
		const Int accent = from.strip;
		const Int flagWidth = iconWidth + 2 * inset + 2 * edge;
		const Int flagHeight = iconHeight + 2 * inset + accent;
		const Int flagLeft = place.left + place.width / 2 + quarter - flagWidth / 2;
		const Int flagTop = height + frameReach - flagHeight + REAL_TO_INT( flagHeight * drop );
		if( edge > 0 )
			TheDisplay->drawFillRect( flagLeft, flagTop, flagWidth, flagHeight, BROADCAST_GOLD );
		TheDisplay->drawFillRect( flagLeft + edge, flagTop, flagWidth - 2 * edge, flagHeight - accent, BROADCAST_PANEL );
		TheDisplay->drawImage( icon, flagLeft + edge + inset, flagTop + inset, flagLeft + edge + inset + iconWidth, flagTop + inset + iconHeight );
		if( edge == 0 )
			drawBroadcastSwatch( flagLeft, flagTop + flagHeight - accent, flagWidth, accent, from.rows[ index ].color );
		IRegion2D drawn;
		drawn.lo.x = flagLeft - max( broadcastPixels( BROADCAST_GOLD_EDGE ), 2 );
		drawn.lo.y = height;
		drawn.hi.x = flagLeft + flagWidth + max( broadcastPixels( BROADCAST_GOLD_EDGE ), 2 );
		drawn.hi.y = flagTop + flagHeight + max( broadcastPixels( BROADCAST_GOLD_EDGE ), 2 );
		flagsDrawn.push_back( drawn );
	}

	// the frame under the bar, squares one inside the other from the top edge down: the halo's two
	// strengths of blue, the ground's edge, then the gold up to the bar, so the bar sits on the gold.
	// Its top runs off the screen, which leaves the sides and the foot.  Over the flags, which drop
	// from under it
	auto frameRect = [ & ]( Int outside, Color color )
	{
		if( height + outside > 0 )
			TheDisplay->drawFillRect( left - outside, 0, barWidth + 2 * outside, height + outside, color );
	};
	frameRect( frameReach, GameMakeColor( BROADCAST_BAND_RGB[ 0 ], BROADCAST_BAND_RGB[ 1 ], BROADCAST_BAND_RGB[ 2 ],
		(UnsignedByte)REAL_TO_INT( 255.0f * BROADCAST_BAND_OUTER_ALPHA ) ) );
	frameRect( frameEdge + frameHalo / 2, GameMakeColor( BROADCAST_BAND_RGB[ 0 ], BROADCAST_BAND_RGB[ 1 ], BROADCAST_BAND_RGB[ 2 ],
		(UnsignedByte)REAL_TO_INT( 255.0f * BROADCAST_BAND_INNER_ALPHA ) ) );
	frameRect( frameEdge, BROADCAST_GROUND );
	frameRect( frameGold, BROADCAST_GOLD );

	if( height > 0 )
		TheDisplay->drawFillRect( left, 0, barWidth, height, BROADCAST_GROUND );
	TheDisplay->drawFillRect( clockLeft, 0, clockBox, clockBoxHeight, BROADCAST_PANEL );
	TheDisplay->drawFillRect( clockLeft, clockBoxHeight - rule, clockBox, rule, BROADCAST_GOLD );
	clock->draw( clockLeft + ( clockBox - clockWidth ) / 2, 0, BROADCAST_INK, BROADCAST_GROUND );

	// block by block: a "vs" before every block but the first of its row, and a team's name and its
	// armies' total over its cards.  A block in both layouts moves with its cards; one in the first alone,
	// the closing player's own, or a team header gone with him, fades out where it stood
	auto drawMoving = [ & ]( DisplayString *text, Color color, Bool first, Int firstX, Int firstY, Bool second, Int secondX, Int secondY )
	{
		if( first && second )
			text->draw( mix( firstX, secondX ), mix( firstY, secondY ), color, BROADCAST_GROUND );
		else if( first )
			text->draw( firstX, firstY, broadcastFade( color, 1.0f - slide ), broadcastFade( BROADCAST_GROUND, 1.0f - slide ) );
		else if( second )
			text->draw( secondX, secondY, broadcastFade( color, slide ), broadcastFade( BROADCAST_GROUND, slide ) );
	};
	auto versusAt = []( const BroadcastLayout &l, Int block, Int *x, Int *y ) -> Bool
	{
		if( block <= 0 || l.lineOf[ block ] != l.lineOf[ block - 1 ] )
			return FALSE;
		Int width = 0, tall = 0;
		l.versusText->getSize( &width, &tall );
		*x = l.blockLefts[ block ] - ( l.versusWidth + width ) / 2;
		*y = l.blockCardsTops[ block ] + ( l.cardHeight - tall ) / 2;
		return TRUE;
	};
	for( size_t block = 0; block < from.blockFirst.size(); block++ )
	{
		const Int other = sliding ? teamIn( to, from.players[ from.blockFirst[ block ] ].team ) : -1;
		Int firstX = 0, firstY = 0, secondX = 0, secondY = 0;
		const Bool versusFirst = versusAt( from, (Int)block, &firstX, &firstY );
		const Bool versusSecond = other >= 0 && versusAt( to, other, &secondX, &secondY );
		drawMoving( versusFirst && versusSecond ? face.versusText : versusFirst ? from.versusText : to.versusText, BROADCAST_MUTED,
			versusFirst, firstX, firstY, versusSecond, secondX, secondY );

		const Bool headerFirst = from.teamNames[ block ] != NULL;
		const Bool headerSecond = other >= 0 && to.teamNames[ other ] != NULL;
		if( !headerFirst && !headerSecond )
			continue;
		const BroadcastLayout &look = headerFirst && headerSecond ? face : headerFirst ? from : to;
		const Int lookBlock = &look == &from ? (Int)block : other;
		Int totalWidth = 0, totalHeight = 0;
		look.teamTotals[ lookBlock ]->getSize( &totalWidth, &totalHeight );
		const Int secondLeft = other >= 0 ? to.blockLefts[ other ] : 0;
		const Int secondRight = other >= 0 ? to.blockLefts[ other ] + to.blockWidths[ other ] : 0;
		const Int secondTop = other >= 0 ? to.blockHeaderTops[ other ] : 0;
		drawMoving( look.teamNames[ lookBlock ], BROADCAST_GOLD, headerFirst, from.blockLefts[ block ], from.blockHeaderTops[ block ],
			headerSecond, secondLeft, secondTop );
		drawMoving( look.teamTotals[ lookBlock ], BROADCAST_INK, headerFirst, from.blockLefts[ block ] + from.blockWidths[ block ] - totalWidth,
			from.blockHeaderTops[ block ], headerSecond, secondRight - totalWidth, secondTop );
	}

	// a card a player, the house colour along its top, the name, the general or the difficulty, then one
	// money line: the cash at the left, the army's label and value at the right.  A defeated player's card
	// flashes red, takes a red veil and has a line drawn through his name; closing, its words go first
	auto drawCard = [ & ]( const BroadcastLayout &l, Int index, const BroadcastPlace &place, Real faded, Real words, Real flash, Real struck )
	{
		const BroadcastRow &row = l.rows[ index ];
		const Int textLeft = place.left + l.cardPad;
		const Int numberEnd = place.left + place.width - l.cardPad;
		TheDisplay->drawFillRect( place.left, place.top, place.width, place.height, broadcastFade( BROADCAST_PANEL, faded ) );
		if( struck > 0.0f )
			TheDisplay->drawFillRect( place.left, place.top, place.width, place.height, broadcastFade( BROADCAST_DEFEAT, 0.2f * struck * faded ) );
		drawBroadcastSwatch( place.left, place.top, place.width, l.strip, broadcastFade( row.color, faded ) );
		if( words > 0.0f )
		{
			const Color ground = broadcastFade( BROADCAST_GROUND, words );
			Int top = place.top + l.strip + l.halfPad;
			Int numberWidth = 0, numberTall = 0;
			row.name->getSize( &numberWidth, &numberTall );
			const Int nameTop = l.compact ? top + l.textHeight - numberTall : top;
			const Int nameTall = numberTall;
			row.name->draw( textLeft, nameTop, broadcastFade( ObserverCamera_readableColor( row.color ), words * ( 1.0f - 0.4f * struck ) ), ground );
			if( !l.compact )
			{
				// the general or difficulty at the right of the name's line, on the name's foot
				const Int lineHeight = max( l.nameHeight, l.sideHeight );
				Int sideWidth = 0, sideTall = 0;
				row.side->getSize( &sideWidth, &sideTall );
				row.side->draw( numberEnd - sideWidth, top + lineHeight - sideTall, broadcastFade( BROADCAST_MUTED, words ), ground );
				top += lineHeight;
			}
			const Int moneyBottom = l.compact ? top + l.textHeight : top + l.numberHeight;
			row.army->getSize( &numberWidth, &numberTall );
			row.army->draw( numberEnd - numberWidth, moneyBottom - numberTall, broadcastFade( BROADCAST_INK, words ), ground );
			if( !l.compact )
			{
				Int armyLabelWidth = 0, armyLabelTall = 0;
				l.armyHead->getSize( &armyLabelWidth, &armyLabelTall );
				l.armyHead->draw( numberEnd - numberWidth - l.halfPad - armyLabelWidth, moneyBottom - l.labelHeight, broadcastFade( BROADCAST_MUTED, words ), ground );
				row.cash->getSize( &numberWidth, &numberTall );
				row.cash->draw( textLeft, moneyBottom - numberTall, broadcastFade( BROADCAST_INK, words ), ground );
			}
			// the line runs left to right across the card through the name, edged in the ground so it reads
			// over a red player's name as well
			if( struck > 0.0f )
			{
				const Int thick = max( broadcastPixels( BROADCAST_STRIKE ), 2 );
				const Int lineTop = nameTop + ( nameTall - thick ) / 2;
				const Int length = REAL_TO_INT( ( place.width - 2 * l.cardPad ) * struck );
				TheDisplay->drawFillRect( textLeft, lineTop - 1, length, thick + 2, ground );
				TheDisplay->drawFillRect( textLeft, lineTop, length, thick, broadcastFade( BROADCAST_DEFEAT, words ) );
			}
		}
		if( flash > 0.0f )
			TheDisplay->drawFillRect( place.left, place.top, place.width, place.height, broadcastFade( BROADCAST_DEFEAT, 0.85f * flash * faded ) );
	};
	// the closing card first, so the others slide over it
	for( Int pass = 0; pass < 2; pass++ )
		for( size_t index = 0; index < from.players.size(); index++ )
		{
			const Int other = sliding ? seatIn( to, from.players[ index ].player ) : (Int)index;
			const Bool closing = sliding && other < 0;
			if( closing != ( pass == 0 ) )
				continue;
			const BroadcastLayout &look = closing ? from : face;
			drawCard( look, &look == &from ? (Int)index : other, cardPlace( (Int)index ), closing ? 1.0f - slide : 1.0f,
				closing ? max( 1.0f - slide * 4.0f, 0.0f ) : 1.0f, flashes[ index ], strikes[ index ] );
		}

	// the armies' bar under the whole row, every player's share in the cards' order, so with two sides
	// each pulls from its own end and they meet at a gold mark in the middle; more sides are parted by
	// the ground's colour
	const Int faceBlocks = (Int)face.blockFirst.size();
	std::vector< Int > barArmies, barBlocks;
	for( Int block = 0; block < faceBlocks; block++ )
		for( Int index = face.blockFirst[ block ]; index < face.blockFirst[ block ] + face.blockSizes[ block ]; index++ )
		{
			barArmies.push_back( face.players[ index ].army );
			barBlocks.push_back( block );
		}
	TheDisplay->drawFillRect( rowLeft, tugTop, rowWidth, tug, BROADCAST_LINE );
	const std::vector< Int > shares = ObserverCamera_barShares( barArmies, rowWidth );
	Int barAt = rowLeft;
	for( size_t piece = 0; piece < shares.size(); piece++ )
	{
		drawBroadcastSwatch( barAt, tugTop, shares[ piece ], tug, face.rows[ piece ].color );
		if( faceBlocks > 2 && piece > 0 && barBlocks[ piece ] != barBlocks[ piece - 1 ] )
			TheDisplay->drawFillRect( barAt, tugTop - rule, rule, tug + 2 * rule, BROADCAST_GROUND );
		barAt += shares[ piece ];
	}
	if( faceBlocks == 2 )
		TheDisplay->drawFillRect( rowLeft + ( rowWidth - rule ) / 2, tugTop - rule, rule, tug + 2 * rule, BROADCAST_GOLD );

	IRegion2D bar;
	bar.lo.x = left - frameReach;
	bar.lo.y = 0;
	bar.hi.x = left + barWidth + frameReach;
	bar.hi.y = max( height + frameReach, 0 );
	TheObserverCamera.addBroadcast( bar );
	for( size_t flag = 0; flag < flagsDrawn.size(); flag++ )
		TheObserverCamera.addBroadcast( flagsDrawn[ flag ] );
	static Int loggedHeight = -1;
	if( !sliding && height != loggedHeight )
	{
		loggedHeight = height;
		DEBUG_LOG(( "OBSCAM frame %u score bar %d x %d, %d row(s), size %d (name %d, detail %d points), card %d of %d\n", frame,
			barWidth, height, from.lines, from.step, BROADCAST_CARD_NAME_POINTS[ from.step ], BROADCAST_CARD_DETAIL_POINTS[ from.step ],
			from.cardWidth, from.fullCardWidth ));
	}

	// a defeated player's banner across the lower picture, the broadcast's red in its tab, and once the
	// match is decided the winner's under it in gold.  A banner names the player in his colour, under it
	// his general or difficulty and his team
	auto bannerLines = [ & ]( Player *player, BroadcastPlate *title, BroadcastPlate *under )
	{
		const std::string seat = std::to_string( player->getPlayerIndex() );
		title->pieces.push_back( broadcastNameText( "bannername" + seat, player, BROADCAST_BANNER_POINTS, BROADCAST_BANNER_NAME_WIDEST ) );
		title->colors.push_back( ObserverCamera_readableColor( clientPlayerColor( player ) ) );
		under->pieces.push_back( broadcastText( "bannerside" + seat, broadcastSide( player ), BROADCAST_WORDS, broadcastPoints( BROADCAST_SIDE_POINTS ), FALSE ) );
		under->colors.push_back( BROADCAST_MUTED );
		for( size_t index = 0; index < named.size(); index++ )
		{
			if( named[ index ].player != player || broadcastTeamSize( named, named[ index ].team ) < 2 )
				continue;
			under->pieces.push_back( broadcastText( "bannerteam" + seat, broadcastTeamName( named, named[ index ].team ), BROADCAST_WORDS,
				broadcastPoints( BROADCAST_SIDE_POINTS ), TRUE ) );
			under->colors.push_back( BROADCAST_GOLD );
		}
	};
	const Int foot = REAL_TO_INT( TheDisplay->getHeight() * BROADCAST_BANNER_FOOT );
	Real defeatShown = 0.0f;
	const DirectorShowing *defeat = TheObserverCamera.getDefeatBanner( &defeatShown );
	if( defeat != NULL && defeatShown > 0.0f )
	{
		BroadcastPlate title, under;
		bannerLines( ThePlayerList->getNthPlayer( defeat->player ), &title, &under );
		UnicodeString defeatWord( u"Defeated" );
		if( TheApocalypseDirector != NULL && TheApocalypseDirector->isActive() )
		{
			// Apocalypse has no defeat (the owner, 2026-11-23): the tab says how long the seat held
			const UnsignedInt seconds = TheObserverCamera.getDefeatFrame( defeat->player ) / LOGICFRAMES_PER_SECOND;
			UnicodeString held;
			held.format( u"%d:%02d", seconds / 60, seconds % 60 );
			defeatWord.format( TheGameText->fetch( "GUI:ApocalypseSurvived" ), held.str() );
		}
		DisplayString *word = broadcastText( "bannerdefeated", defeatWord, BROADCAST_WORDS,
			broadcastPoints( BROADCAST_BANNER_WORD_POINTS ), TRUE );
		TheObserverCamera.addBroadcast( drawBroadcastBanner( word, BROADCAST_DEFEAT, BROADCAST_INK, title, under, foot, defeatShown ) );
	}
	// a team that won is named once, its players under it
	const Real winnerShown = TheObserverCamera.getWinnerShown();
	std::vector< const SpectatorStats * > winners;
	for( size_t index = 0; index < named.size(); index++ )
		if( named[ index ].player->isPlayerActive() )
			winners.push_back( &named[ index ] );
	if( winnerShown > 0.0f && !winners.empty() )
	{
		BroadcastPlate title, under;
		const Int team = winners[ 0 ]->team;
		if( broadcastTeamSize( named, team ) >= 2 )
		{
			title.pieces.push_back( broadcastText( "bannerwinteam", broadcastTeamName( named, team ), BROADCAST_WORDS,
				broadcastPoints( BROADCAST_BANNER_POINTS ), TRUE ) );
			title.colors.push_back( BROADCAST_INK );
			for( size_t index = 0; index < winners.size(); index++ )
			{
				Player *player = winners[ index ]->player;
				under.pieces.push_back( broadcastNameText( "bannerwinner" + std::to_string( player->getPlayerIndex() ), player,
					BROADCAST_SIDE_POINTS, BROADCAST_BANNER_NAME_WIDEST ) );
				under.colors.push_back( ObserverCamera_readableColor( clientPlayerColor( player ) ) );
			}
		}
		else
			bannerLines( winners[ 0 ]->player, &title, &under );
		DisplayString *word = broadcastText( "bannervictory", UnicodeString( u"Victory" ), BROADCAST_WORDS,
			broadcastPoints( BROADCAST_BANNER_WORD_POINTS ), TRUE );
		const Int winnerFoot = foot + broadcastPixels( BROADCAST_GAP ) + broadcastBannerHeight( title, under );
		TheObserverCamera.addBroadcast( drawBroadcastBanner( word, BROADCAST_GOLD, BROADCAST_GROUND, title, under, winnerFoot, winnerShown ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** The Art of Defense panel at the top middle of the world (the owner, 2026-11-28: what is coming now, when the
	* next comes, what it is).  Two of the side's trays, one under the other, cells as the production queue's:
	* the first the lives the seats share, the wave under way and its units still to come down the local seat's
	* lane or walking it, a cameo a kind with its count; the second the next wave, its seconds to go once the last
	* of this one is out, and its units a lane.  Each cameo wears its rank's chevron, its health as it comes
	* (rank and the boss's share in), a flying mark on an aircraft and a boss mark on the boss.  Three words,
	* Lives, Wave and Next, the rest numbers and pictures.  It reads TheAodDirector at the sync point and writes
	* nothing back; the next wave is drawn a wave ahead in the logic, so what it shows is what comes. */
//-------------------------------------------------------------------------------------------------
enum
{
	AOD_CAMEO_W					= 46,		///< a kind's cameo, 800x600 units through stripPixels: half again the queue's
	AOD_CAMEO_H					= 36,
	AOD_SMALL_POINTS		= 7,		///< the corners' readouts
	AOD_BIG_POINTS			= 14,		///< the lives, the wave and the next wave's number
	AOD_ROW_GAP					= 2,		///< between the two trays
	AOD_PLAY_SHARE			= 70		///< percent of the world's width a tray may take before its cells shrink
};

static IRegion2D s_aodPanel = { { 0, 0 }, { 0, 0 } };		///< the last draw's whole panel, which a click on is the panel's
static std::vector<DisplayString *> s_aodStrings;				///< kept from draw to draw, used in the same order
static UnsignedInt s_aodStringsUsed = 0;

static DisplayString *aodString( const AsciiString &fontName, Int points, const UnicodeString &text )
{
	if( s_aodStringsUsed == s_aodStrings.size() )
		s_aodStrings.push_back( TheDisplayStringManager->newDisplayString() );
	DisplayString *string = s_aodStrings[ s_aodStringsUsed++ ];
	GameFont *font = TheFontLibrary->getFont( fontName, max( stripPixels( points ), 6 ), TRUE );
	if( string->getFont() != font )
		string->setFont( font );
	if( string->getText().compare( text ) != 0 )
		string->setText( text );
	// HudReadout_draw clips a string to its plate: one drawn bare is clipped to the screen alone
	IRegion2D screen = { { 0, 0 }, { (Int)TheDisplay->getWidth(), (Int)TheDisplay->getHeight() } };
	string->setClipRegion( &screen );
	return string;
}

/// one cell of the panel: a kind's cameo, or a number with its word
struct AodCell
{
	const ThingTemplate *thing;		///< the kind, NULL for a number cell
	Int count;										///< units of it, or the cell's number
	Int wave;											///< the wave whose rank and boss share it shows
	Bool boss;
	UnicodeString word;						///< a number cell's word
	UnicodeString big;						///< and its number as written
	UnicodeString corner;					///< a number cell's bottom right readout (the seconds to go)
	Color tint;										///< under a number cell's cameo box, 0 for none
};

/// the side's tray hanging from a line, open on the right and closed on the left, its foot along the top
static HudTray aodTray( Int cellW, Int cellH )
{
	HudTray tray = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0 };
	const Image *const *pieces = TheControlBar != NULL ? TheControlBar->getControlBarSchemeManager()->getPowerTray() : NULL;
	if( pieces == NULL || pieces[ ControlBarScheme::TRAY_CELL ] == NULL )
		return tray;
	const AsciiString side = ThePlayerList->getLocalPlayer()->getSide();
	const char *prefix = "America";
	if( side.startsWith( "China" ) || side.startsWith( "Boss" ) )
		prefix = "China";
	else if( side.startsWith( "GLA" ) )
		prefix = "Gla";
	else if( side.startsWith( "Turkiye" ) )
		prefix = "Turkiye";
	tray.cell = pieces[ ControlBarScheme::TRAY_CELL ];
	tray.open = mirroredTrayPiece( prefix, "EdgeRight", pieces[ ControlBarScheme::TRAY_EDGE_LEFT ] );
	tray.foot = pieces[ ControlBarScheme::TRAY_EDGE_TOP ];
	tray.uncovered = pieces[ ControlBarScheme::TRAY_EDGE_BOTTOM ];
	tray.cornerFoot = mirroredTrayPiece( prefix, "CornerTopRight", pieces[ ControlBarScheme::TRAY_CORNER_TL ] );
	tray.cornerUncovered = mirroredTrayPiece( prefix, "CornerBottomRight", pieces[ ControlBarScheme::TRAY_CORNER_BL ] );
	tray.step = pieces[ ControlBarScheme::TRAY_STEP ];
	tray.close = pieces[ ControlBarScheme::TRAY_EDGE_LEFT ];
	tray.closeFoot = pieces[ ControlBarScheme::TRAY_CORNER_TL ];
	tray.closeUncovered = pieces[ ControlBarScheme::TRAY_CORNER_BL ];
	sizeHudTray( &tray, (Real)cellW / tray.cell->getImageWidth(), (Real)cellH / tray.cell->getImageHeight() );
	return tray;
}

/// the kinds of a wave a cell each, the boss first and then in the table's order, with how many of each
static void aodKindCells( std::vector<AodCell> &cells, Int wave, const std::vector<Int> *kinds, Int lane )
{
	for( Int kind = -1; kind < Aod_kindCount(); ++kind )
	{
		Int count = 0;
		if( kinds != NULL )
		{
			for( size_t i = 0; i < kinds->size(); ++i )
				count += ( *kinds )[ i ] == kind ? 1 : 0;
		}
		else
			count = TheAodDirector->countLeft( lane, kind );
		const ThingTemplate *thing = TheAodDirector->kindTemplate( kind );
		if( count <= 0 || thing == NULL )
			continue;
		AodCell cell;
		cell.thing = thing;
		cell.count = count;
		cell.wave = wave;
		cell.boss = kind < 0;
		cell.tint = 0;
		cells.push_back( cell );
	}
}

/// one row of cells in its tray, its top at `top`, centred on `middle`; the row's bottom
static Int drawAodRow( const std::vector<AodCell> &cells, Int middle, Int top, Int widest, const HudCellStyle &style,
											 const AsciiString &fontName )
{
	if( cells.empty() )
		return top;

	// the queue's cell margins round a bigger cameo, shrunk as a whole when the row is wider than its share
	Int cameoW = stripPixels( AOD_CAMEO_W ), cameoH = stripPixels( AOD_CAMEO_H );
	Int marginX = REAL_TO_INT_CEIL( cameoW * ( HUD_CELL_TRAY_CELL_W - HUD_CELL_TRAY_BUTTON_W ) / ( 2.0f * HUD_CELL_TRAY_BUTTON_W ) );
	Int marginY = REAL_TO_INT_CEIL( cameoH * ( HUD_CELL_TRAY_CELL_H - HUD_CELL_TRAY_BUTTON_H ) / ( 2.0f * HUD_CELL_TRAY_BUTTON_H ) );
	const Int count = (Int)cells.size();
	if( ( cameoW + 2 * marginX ) * count > widest && count > 0 )
	{
		const Real shrink = (Real)widest / ( ( cameoW + 2 * marginX ) * count );
		cameoW = max( REAL_TO_INT( cameoW * shrink ), 8 );
		cameoH = max( REAL_TO_INT( cameoH * shrink ), 6 );
		marginX = max( REAL_TO_INT( marginX * shrink ), 1 );
		marginY = max( REAL_TO_INT( marginY * shrink ), 1 );
	}
	const Int cellW = cameoW + 2 * marginX, cellH = cameoH + 2 * marginY;
	const HudTray tray = aodTray( cellW, cellH );
	const Int left = middle - cellW * count / 2;
	const Int cellTop = top + tray.footH;

	std::vector<IRegion2D> boxes( count );
	for( Int i = 0; i < count; ++i )
	{
		boxes[ i ].lo.x = left + i * cellW;
		boxes[ i ].hi.x = boxes[ i ].lo.x + cellW;
		boxes[ i ].lo.y = cellTop;
		boxes[ i ].hi.y = cellTop + cellH;
	}
	const Int bottom = cellTop + cellH + tray.uncoveredH;
	if( tray.cell != NULL )
		drawHudTray( tray, &boxes[ 0 ], count, count, 1, -1 );
	else
	{
		// a scheme with no tray (Classic's): a plate of the bar's steel under the row
		const Int pad = max( marginX, 2 );
		TheDisplay->drawFillRect( left - pad, top, cellW * count + 2 * pad, bottom - top + pad, GameMakeColor( 20, 28, 36, 225 ) );
		TheDisplay->drawOpenRect( left - pad, top, cellW * count + 2 * pad, bottom - top + pad, 1.0f, GameMakeColor( 112, 132, 150, 235 ) );
	}

	const Color white = GameMakeColor( 245, 245, 245, 255 );
	const Color health = GameMakeColor( 120, 230, 110, 255 );
	const Color red = GameMakeColor( 240, 70, 50, 255 );
	const Color air = GameMakeColor( 110, 200, 255, 255 );
	const Color hole = GameMakeColor( 2, 4, 5, 255 );
	for( Int i = 0; i < count; ++i )
	{
		const AodCell &c = cells[ i ];
		IRegion2D cameo;
		cameo.lo.x = boxes[ i ].lo.x + marginX;
		cameo.lo.y = boxes[ i ].lo.y + marginY;
		cameo.hi.x = cameo.lo.x + cameoW;
		cameo.hi.y = cameo.lo.y + cameoH;
		if( c.thing == NULL )
		{
			// a number cell: its word over its number, the seconds to go in the corner
			TheDisplay->drawFillRect( cameo.lo.x, cameo.lo.y, cameoW, cameoH, c.tint != 0 ? c.tint : hole );
			DisplayString *word = aodString( fontName, AOD_SMALL_POINTS, c.word );
			HudReadout_draw( word, cameo, HUD_READOUT_TOP_LEFT, GameMakeColor( 200, 210, 220, 255 ), style.plate );
			DisplayString *big = aodString( fontName, AOD_BIG_POINTS, c.big );
			Int w = 0, h = 0, wordH = 0;
			big->getSize( &w, &h );
			word->getSize( NULL, &wordH );
			const Int below = cameo.lo.y + wordH;
			big->draw( ( cameo.lo.x + cameo.hi.x - w ) / 2, below + ( cameo.hi.y - below - h ) / 2, white, GameMakeColor( 0, 0, 0, 255 ) );
			if( !c.corner.isEmpty() )
				HudReadout_draw( aodString( fontName, AOD_SMALL_POINTS, c.corner ), cameo, HUD_READOUT_BOTTOM_RIGHT, white, style.plate );
			drawHudCellBorder( cameo, c.tint != 0 ? red : hudCellBorder( COMMAND_BUTTON_BORDER_SYSTEM ) );
			continue;
		}

		if( c.thing->getButtonImage() != NULL )
			TheDisplay->drawImage( c.thing->getButtonImage(), cameo.lo.x, cameo.lo.y, cameo.hi.x, cameo.hi.y );
		drawHudCellBorder( cameo, c.boss ? red : hudCellBorder( COMMAND_BUTTON_BORDER_BUILD ) );

		// the rank it comes in at, the bar's own chevrons
		const Int rank = Aod_waveRank( c.wave );
		static const char *const CHEVRONS[ LEVEL_COUNT ] = { NULL, "SCVeter1", "SCVeter2", "SCVeter3" };
		const Image *chevron = rank > LEVEL_REGULAR && rank < LEVEL_COUNT ? TheMappedImageCollection->findImageByName( CHEVRONS[ rank ] ) : NULL;
		if( chevron != NULL )
		{
			const Int size = max( cameoH / 3, 6 );
			TheDisplay->drawImage( chevron, cameo.lo.x + 1, cameo.lo.y + 1, cameo.lo.x + 1 + size, cameo.lo.y + 1 + size );
		}

		// what it flies or that it is the boss, its health as it comes, how many
		if( c.boss )
			HudReadout_draw( aodString( fontName, AOD_SMALL_POINTS, TheGameText->fetch( "GUI:AodBoss" ) ), cameo, HUD_READOUT_TOP_RIGHT, red, style.plate );
		else if( c.thing->isKindOf( KINDOF_AIRCRAFT ) )
			HudReadout_draw( aodString( fontName, AOD_SMALL_POINTS, TheGameText->fetch( "GUI:AodAir" ) ), cameo, HUD_READOUT_TOP_RIGHT, air, style.plate );
		Real hp = c.thing->calcMaxHealth( TRUE ) * TheGlobalData->m_healthBonus[ rank >= 0 && rank < LEVEL_COUNT ? rank : 0 ];
		if( c.boss )
			hp = hp * (Real)Aod_bossHealthPercent( c.wave ) / 100.0f;
		UnicodeString text;
		if( hp > 0.0f )
		{
			text.format( u"%d", REAL_TO_INT( hp + 0.5f ) );
			HudReadout_draw( aodString( fontName, AOD_SMALL_POINTS, text ), cameo, HUD_READOUT_BOTTOM_LEFT, health, style.plate );
		}
		text.format( u"x%d", c.count );
		HudReadout_draw( aodString( fontName, AOD_SMALL_POINTS, text ), cameo, HUD_READOUT_BOTTOM_RIGHT, white, style.plate );
	}

	s_aodPanel.lo.x = s_aodPanel.hi.x > s_aodPanel.lo.x ? min( s_aodPanel.lo.x, left - tray.openW ) : left - tray.openW;
	s_aodPanel.lo.y = s_aodPanel.hi.y > s_aodPanel.lo.y ? min( s_aodPanel.lo.y, top ) : top;
	s_aodPanel.hi.x = max( s_aodPanel.hi.x, left + cellW * count + tray.openW );
	s_aodPanel.hi.y = max( s_aodPanel.hi.y, bottom );
	return bottom;
}

/// the whole panel under `top`; its bottom, or `top` when there is none
static Int drawAodPanel( Int top, const AsciiString &fontName )
{
	s_aodPanel.lo.x = s_aodPanel.lo.y = s_aodPanel.hi.x = s_aodPanel.hi.y = 0;
	s_aodStringsUsed = 0;
	Player *local = ThePlayerList->getLocalPlayer();
	if( TheAodDirector == NULL || !TheAodDirector->isActive() || TheAodDirector->isOver() || local == NULL || TheControlBar == NULL )
		return top;

	const IRegion2D play = hudPlay();
	const Int middle = ( play.lo.x + play.hi.x ) / 2;
	const Int widest = ( play.hi.x - play.lo.x ) * AOD_PLAY_SHARE / 100;
	const HudCellStyle style = hudCellStyle();
	const Int lanes = TheAodDirector->getLaneCount();
	const Int lane = lanes > 0 ? max( 0, min( local->getMpStartIndex(), lanes - 1 ) ) : 0;
	const Int wave = TheAodDirector->getWave();

	// the lives the seats share and the wave under way, with what of it is left down this seat's lane
	std::vector<AodCell> now;
	AodCell number;
	number.thing = NULL;
	number.boss = FALSE;
	number.wave = wave;
	number.tint = 0;
	number.word = TheGameText->fetch( "GUI:AodLives" );
	number.count = TheAodDirector->getLives();
	number.big.format( u"%d", number.count );
	now.push_back( number );
	if( wave > 0 )
	{
		number.word = TheGameText->fetch( "GUI:AodWave" );
		number.big.format( u"%d/%d", wave, (Int)AOD_WAVES );
		number.tint = Aod_isBossWave( wave ) ? GameMakeColor( 90, 14, 10, 255 ) : 0;
		now.push_back( number );
		aodKindCells( now, wave, NULL, lane );
	}

	// the next wave, the seconds to it once this one is all out, and its units a lane
	std::vector<AodCell> next;
	const Int coming = TheAodDirector->getNextWave();
	if( coming > 0 )
	{
		number.word = TheGameText->fetch( "GUI:AodNext" );
		number.big.format( u"%d", coming );
		number.tint = Aod_isBossWave( coming ) ? GameMakeColor( 90, 14, 10, 255 ) : 0;
		const UnsignedInt at = TheAodDirector->getNextWaveFrame(), frame = TheGameLogic->getFrame();
		if( at > frame )
			number.corner.format( u"%ds", (Int)( ( at - frame + LOGICFRAMES_PER_SECOND - 1 ) / LOGICFRAMES_PER_SECOND ) );
		next.push_back( number );
		aodKindCells( next, coming, &TheAodDirector->getNextKinds(), lane );
	}

	TheDisplay->beginBatch2D();
	Int bottom = drawAodRow( now, middle, top, widest, style, fontName );
	bottom = drawAodRow( next, middle, bottom + stripPixels( AOD_ROW_GAP ), widest, style, fontName );
	TheDisplay->endBatch2D();
	return bottom;
}

//-------------------------------------------------------------------------------------------------
/** Where the info strip ends, for the timers that start in the same corner.  The strip draws after
	* them, on top of everything, so its height cannot be read from the last draw; it is one line of
	* the strip's font at the strip's own offsets, which is what drawHudOverlay lays out. */
//-------------------------------------------------------------------------------------------------
Int InGameUI::hudStripBottom( void ) const
{
	if( !TheGlobalData->m_showNetBox && !TheGameLogic->isPeaceTime() )
		return 0;
	const GameFont *font = TheFontLibrary->getFont( m_superweaponNormalFont,
		TheGlobalLanguageData->adjustFontSize( HUD_CLOCK_POINT_SIZE ), TRUE );
	return TheUIRect().y + stripPixels( 2 ) + font->height + 1 + stripPixels( 4 );
}

//-------------------------------------------------------------------------------------------------
void InGameUI::drawHudOverlay( void )
{
	// the online notice stands under whatever of this the frame shows; laid out here, at the sync point,
	// because the notice draws inside the overlap window, where peace time and the vote are not to be read
	m_noticeTop = 0;
	s_aodPanel.lo.x = s_aodPanel.lo.y = s_aodPanel.hi.x = s_aodPanel.hi.y = 0;		// drawn again below, or nothing to click

	// the Classic layout's superweapon lines postDraw laid out, here so the health bars go under them
	const Color drop = GameMakeColor( 0, 0, 0, 255 );
	for( size_t each = 0; each < m_classicSuperweaponLines.size(); each++ )
	{
		const ClassicSuperweaponLine &line = m_classicSuperweaponLines[ each ];
		if( !line.timeOnly )
			line.info->drawName( line.x, line.y, line.color, drop );
		line.info->drawTime( line.x, line.y, line.color, drop );
	}

	// only once a real game is under way - not in the shell, and not on the menu's background map
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	if( TheGameLogic->getFrame() == 0 )
		return;

	// the observer console, here so the health bars and the cash floating off a refinery go under it
	if( localPlayerWatching() )
		drawObserverConsole();

	// the match clock at the top middle, under the readout strip while that is up; a watcher's is on his
	// console, and the top of his screen stays clear
	if( MatchClock_wanted() && !localPlayerWatching() )
	{
		MatchClockState clock;
		MatchClock_gather( TheGameLogic->getFrame(), clock );
		MatchClock_draw( clock, hudStripBottom() );
	}

	// Art of Defense: the waves at the top middle, the online notice under them
	if( !localPlayerWatching() )
		m_noticeTop = max( m_noticeTop, drawAodPanel( hudStripBottom(), m_superweaponNormalFont ) );

	// on top of the battlefield like this strip, but never behind the ShowNetBox switch
	drawKickVoteBanner();

	// ShowNetBox is the player's switch for the readout.  Peace time is a rule of the match rather
	// than a readout, so while it runs the strip stays up with only that segment in it.
	const Bool showInfo = TheGlobalData->m_showNetBox;
	const Bool peace = TheGameLogic->isPeaceTime();
	if( !showInfo && !peace )
		return;
	m_noticeTop = max( m_noticeTop, hudStripBottom() );

	//
	// Two rates, both sampled over half a second of wall clock so the numbers are readable, and
	// they are not the same thing. Client frames are the render rate. Logic frames are the
	// simulation, which is *supposed* to run at LOGICFRAMES_PER_SECOND and drops below it exactly
	// when a frame overruns its budget - so a reading under the nominal rate is the stutter,
	// measured, rather than something the render rate can be blamed for.
	//
	// NOT GameClient::getFrame() - that is the simulation frame number from the server, so the
	// "fps" reading was a second copy of the logic rate and moved with it. postDraw runs once per
	// rendered frame, so count the draws here instead.
	++m_hudDrawCount;
	const UnsignedInt clientFrame = m_hudDrawCount;
	const UnsignedInt logicFrame = TheGameLogic->getFrame();
	UnsignedInt nowMs = Clock_Milliseconds();
	// a replay wound back runs the frame number backwards, and the unsigned difference read as tens
	// of millions of logic frames a second; the reading starts over from there instead
	if( m_hudLastSampleFrame == 0 || logicFrame < m_hudLastSampleLogicFrame )
	{
		m_hudLastSampleMs = nowMs;
		m_hudLastSampleFrame = clientFrame;
		m_hudLastSampleLogicFrame = logicFrame;
	}
	else if( nowMs - m_hudLastSampleMs >= 500 )
	{
		const Real elapsed = (Real)(nowMs - m_hudLastSampleMs);
		m_hudFps.add( (clientFrame - m_hudLastSampleFrame) * 1000.0f / elapsed );
		m_hudLogicHz.add( (logicFrame - m_hudLastSampleLogicFrame) * 1000.0f / elapsed );
		m_hudLastSampleMs = nowMs;
		m_hudLastSampleFrame = clientFrame;
		m_hudLastSampleLogicFrame = logicFrame;
	}

	//
	// Game time is logic frames; real time is the wall clock. They start together and drift apart
	// by however much the simulation has fallen behind - which is the same stutter the two rates
	// above show, totalled. The base is set the first time the overlay draws rather than at frame
	// zero, so switching it on mid-game does not invent an hour of lag.
	//
	// A pause stops the logic clock and not the wall clock, and the gap between the two readings
	// is meant to be simulation lag - time spent in the menu is not lag. So the base is walked
	// forward by every paused frame's worth of wall clock and both readouts stand still.
	//
	UnsignedInt gameSecs = logicFrame / LOGICFRAMES_PER_SECOND;
	if( m_hudRealClockBaseMs == 0 )
		m_hudRealClockBaseMs = nowMs - gameSecs * 1000;
	else if( TheGameLogic->isGamePaused() )
		m_hudRealClockBaseMs += nowMs - m_hudLastDrawMs;
	m_hudLastDrawMs = nowMs;
	UnsignedInt realSecs = (nowMs - m_hudRealClockBaseMs) / 1000;

	// the machine's own clock first, for a player who wants to know when to stop
	WallClockTime wallClock;
	getLocalWallClock( &wallClock );

	// the logic ticks a wall second as measured, whatever game speed the match was started at
	UnicodeString rateText;
	rateText.format( u"%dhz(%dfps)", m_hudLogicHz.shown, m_hudFps.shown );

	UnicodeString text;
	text.format( u"%02d:%02d   %02d:%02d:%02d(%02d:%02d:%02d)   %s %s",
							 wallClock.wHour, wallClock.wMinute,
							 gameSecs / 3600, (gameSecs / 60) % 60, gameSecs % 60,
							 realSecs / 3600, (realSecs / 60) % 60, realSecs % 60,
							 rateText.str(), TheDisplay->getRendererName() );

	UnicodeString frameText;
	frameText.format( u"   frame %d", (Int)logicFrame );
	text.concat( frameText );

	// in a network game, how far ahead the room can play without waiting on anybody, out of the
	// input delay it is running with, and the frame rate the slowest machine has set for everyone:
	// ready falling to 0 is the stall, seen before it is felt
	if( TheNetwork != NULL )
	{
		UnicodeString netText;
		netText.format( u"   ready %d/%d   room %dfps", (Int)TheNetwork->getFramesReady(),
										(Int)TheNetwork->getRunAhead(), (Int)TheNetwork->getFrameRate() );
		text.concat( netText );
	}

	// the lobby's unit limit, as this player's own share and not the match total: what stands and
	// what is queued, against the number the production queue refuses at
	const UnsignedInt unitCap = TheGameLogic->getUnitCap();
	const Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( unitCap > 0 && localPlayer && !localPlayer->isPlayerObserver() )
	{
		UnicodeString units;
		units.format( u"   %d/%d units", localPlayer->countUnitsTowardCap(), unitCap );
		text.concat( units );
	}

	if( m_hudDisplayString == NULL )
	{
		// bold at a small point size, because this plate is read at a glance rather than read
		m_hudDisplayString = TheDisplayStringManager->newDisplayString();
		m_hudDisplayString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
										TheGlobalLanguageData->adjustFontSize( HUD_CLOCK_POINT_SIZE ),
										TRUE ) );
	}
	Int textWidth = 0, textHeight = 0;
	if( showInfo )
	{
		m_hudDisplayString->setText( text );
		m_hudDisplayString->getSize( &textWidth, &textHeight );
	}

	// the truce goes last on the line, in red, so it sits in the same spot whether the readout to
	// its left is there or not
	Int peaceWidth = 0;
	if( peace )
	{
		const UnsignedInt secs = ControlBar_secondsFromFrames(
										(Real)(TheGameLogic->getPeaceTimeEndFrame() - logicFrame) );
		UnicodeString clock;
		clock.format( TheGameText->fetch( "GUI:PeaceTimeHud" ), secs / 60, secs % 60 );
		UnicodeString peaceText = TheGameText->fetch( "GUI:PeaceTimeHudLabel" );
		peaceText.concat( u' ' );
		peaceText.concat( clock );

		if( m_peaceTimeDisplayString == NULL )
		{
			m_peaceTimeDisplayString = TheDisplayStringManager->newDisplayString();
			m_peaceTimeDisplayString->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
											TheGlobalLanguageData->adjustFontSize( HUD_CLOCK_POINT_SIZE ),
											TRUE ) );
		}
		m_peaceTimeDisplayString->setText( peaceText );

		Int peaceHeight = 0;
		m_peaceTimeDisplayString->getSize( &peaceWidth, &peaceHeight );
		if( peaceHeight > textHeight )
			textHeight = peaceHeight;
	}

	const Int gap = (showInfo && peace) ? stripPixels( 6 ) : 0;
	const Int stripWidth = textWidth + gap + peaceWidth;

	// top right, clear of the radar and the superweapon timers, and first in that corner.
	//
	// The three gaps are 800x600 numbers put through the command bar's own scale, like everything
	// else on this overlay.  Held at a flat pixel count the plate crept into the corner as the
	// screen grew: the lettering inside it scales and the margin around it did not, so a 1440-tall
	// shot and a 1080-tall one did not overlay however the text was sized.
	const Int pad = stripPixels( 2 );
	// the box's right edge, or hudPlay's where that is nearer: the sidebar's left under Red Alert style
	const UIRect box = TheUIRect();
	const Int right = min( box.x + box.w, (Int)hudPlay().hi.x );
	Int x = right - stripWidth - pad - stripPixels( 4 );
	Int y = box.y + stripPixels( 2 );

	// a plate behind it, so it stays legible over bright terrain
	TheDisplay->drawFillRect( x - pad, y - 1, stripWidth + pad*2, textHeight + 2,
														GameMakeColor( 0, 0, 0, 140 ) );

	if( showInfo )
		m_hudDisplayString->draw( x, y, GameMakeColor( 235, 235, 235, 255 ), GameMakeColor( 0, 0, 0, 255 ) );

	if( peace )
		m_peaceTimeDisplayString->draw( x + textWidth + gap, y, peaceTimeColor( 255 ), GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
/** The open vote kick, a plate at the top middle of the battlefield: who is on the block, the yes
	* votes against the number needed, the seconds left and how to answer.  The vote is the logic's
	* (GameLogic::processKickVote); this only shows it. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawKickVoteBanner( void )
{
	const Int target = TheGameLogic->getKickVoteTarget();
	if( target < 0 )
		return;

	const UnsignedInt secs = ControlBar_secondsFromFrames(
									(Real)(TheGameLogic->getKickVoteEndFrame() - TheGameLogic->getFrame()) );
	UnicodeString text;
	text.format( TheGameText->fetch( "GUI:KickVoteBanner" ), ThePlayerList->getNthPlayer( target )->getPlayerDisplayName().str(),
							 TheGameLogic->countKickVotes(), TheGameLogic->countKickVoters(), (Int)secs );
	DisplayString *banner = broadcastText( "kickvote", text, m_superweaponNormalFont.str(),
																				 TheGlobalLanguageData->adjustFontSize( HUD_OVERLAY_POINT_SIZE ), TRUE );

	Int width = 0, height = 0;
	banner->getSize( &width, &height );
	const UIRect box = TheUIRect();
	const Int pad = stripPixels( 3 );
	const Int x = box.x + ( box.w - width ) / 2;
	const Int y = box.y + stripPixels( 40 );
	TheDisplay->drawFillRect( x - pad, y - pad, width + pad*2, height + pad*2, GameMakeColor( 0, 0, 0, 170 ) );
	banner->draw( x, y, GameMakeColor( 255, 210, 90, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
	m_noticeTop = max( m_noticeTop, y + height + pad );		// the online notice goes under the vote
}

/// the maintenance banner's warning: the hotkey page's clash colour, the one error colour the front end has
static const Color NOTICE_WARNING = GameMakeColor( 255, 104, 72, 255 );
/// the info banner's trim: Generals blue, the HUD's (BRAND.md, "The in-game HUD"), and its chip's label
static const Color NOTICE_BLUE = GameMakeColor( 68, 89, 182, 255 );
static const Color NOTICE_BLUE_LABEL = GameMakeColor( 150, 170, 240, 255 );
/// Generals steel, brushed and lit from the top left: the plate's lit and shaded halves, its bevel's light
/// and shade, and the sunk tray the words sit in
static const Color STEEL_LIT = GameMakeColor( 72, 77, 85, 245 );
static const Color STEEL_DARK = GameMakeColor( 50, 54, 60, 245 );
static const Color STEEL_LIGHT = GameMakeColor( 138, 145, 155, 255 );
static const Color STEEL_SHADE = GameMakeColor( 18, 20, 23, 255 );
static const Color STEEL_TRAY = GameMakeColor( 13, 15, 18, 235 );

/// a steel plate hanging from y, its two lower corners cut at 45 degrees (the emblem's language), a trim of
/// trimSize along its foot and up the cuts
static void drawSteelPlate( Int x, Int y, Int width, Int height, Int cut, Color trim, Int trimSize )
{
	const Int body = height - cut;
	TheDisplay->drawFillRect( x, y, width, body / 2, STEEL_LIT );
	TheDisplay->drawFillRect( x, y + body / 2, width, body - body / 2, STEEL_DARK );
	TheDisplay->drawFillRect( x, y, 1, body, STEEL_LIGHT );
	TheDisplay->drawFillRect( x + width - 1, y, 1, body, STEEL_SHADE );
	// the cuts a row at a time, each row one in from the last, its ends in the trim
	for( Int i = 0; i < cut; ++i )
	{
		const Int left = x + i + 1, rowWidth = width - 2 * ( i + 1 );
		if( rowWidth <= 0 )
			break;
		TheDisplay->drawFillRect( left, y + body + i, rowWidth, 1, STEEL_DARK );
		TheDisplay->drawFillRect( left, y + body + i, min( trimSize, rowWidth ), 1, trim );
		TheDisplay->drawFillRect( left + rowWidth - min( trimSize, rowWidth ), y + body + i, min( trimSize, rowWidth ), 1, trim );
	}
	TheDisplay->drawFillRect( x + cut, y + height - trimSize, width - 2 * cut, trimSize, trim );
}

/// a tray sunk into the plate: shade along its top and left, a lit lip along its foot and right
static void drawSteelTray( Int x, Int y, Int width, Int height )
{
	TheDisplay->drawFillRect( x, y, width, height, STEEL_TRAY );
	TheDisplay->drawFillRect( x, y, width, 1, STEEL_SHADE );
	TheDisplay->drawFillRect( x, y, 1, height, STEEL_SHADE );
	TheDisplay->drawFillRect( x, y + height - 1, width, 1, STEEL_LIGHT );
	TheDisplay->drawFillRect( x + width - 1, y, 1, height, STEEL_LIGHT );
}

//-------------------------------------------------------------------------------------------------
/** The online service's notice (OnlineGetNotice), on every screen: a plate hanging from the top edge
	* as the general's powers tray hangs from its edge, in the HUD's Generals steel, the same in the
	* shell and a match.  A bevelled plate with its lower corners cut at 45 degrees and a trim along its
	* foot, the words in a sunk tray after a chip.  Info is trimmed in Generals blue; maintenance in the
	* warning colour, with a warning stripe down the tray and its words bold.  The plate is as wide as its
	* words, and the words wrap to more lines rather than lose any.  In the shell it stays left of the
	* main menu's logo (board x 1240); in a match it keeps to the open world (left of the sidebar, inside
	* the HudWidth area) and stands under the top right info strip, peace time and a kick vote's plate
	* when they show (m_noticeTop).  Drawn, not a window: it takes no click. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawOnlineNotice( void )
{
	OnlineNotice notice;
	if( !OnlineGetNotice( notice ) || !TheDisplay || !TheGameText )
		return;

	const Bool inMatch = TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isLoadingMap();
	if( !inMatch && UIStyle::onlinePageShowing() )
		return;		// the online pages carry it in their head (menu package 9)
	const Int margin = UIStyle::px( 16.0f );
	Int left = 0, right = TheDisplay->getWidth(), top = 0;
	Int limit = right;		// the plate's right edge goes no further
	if( inMatch && TheControlBar )
	{
		const IRegion2D play = hudPlay();
		left = play.lo.x;
		right = limit = play.hi.x;
		top = play.lo.y;
		// under the info strip, peace time and an open kick vote, as the sync point laid them out
		if( m_noticeTop > 0 )
			top = max( top, m_noticeTop + UIStyle::px( 4.0f ) );
	}
	else
		limit = min( right, UIStyle::boardX( 1240.0f ) );		// clear of the main menu's COMMAND & CONQUER logo
	const Bool maintenance = notice.maintenance;
	const Int widest = min( limit - left - 2 * margin, UIStyle::px( 1200.0f ) );
	if( widest <= 0 )
		return;

	// the words: the server's in the player's language, the game's own for a maintenance that sent none
	UnicodeString words;
	const AsciiString utf8 = OnlineNoticeWords( notice, TheGlobalData->m_textLanguage );
	if( utf8.isNotEmpty() )
		words = UnicodeString( MultiByteToWideCharSingleLine( utf8.str() ).c_str() );
	else
		words = TheGameText->fetch( "GUI:OnlineNoticeMaintenanceWords" );
	UnicodeString until;
	const UnicodeString when = OnlineUntilText( notice.until );
	if( !when.isEmpty() )
		until.format( TheGameText->fetch( maintenance ? "GUI:OnlineNoticeBack" : "GUI:OnlineNoticeUntil" ), when.str() );

	GameFont *wordFont = UIStyle::readableFont( UIStyle::TEXT_BODY, maintenance );
	GameFont *untilFont = UIStyle::readableFont( UIStyle::TEXT_BODY );
	struct Piece { const char *key; GameFont *font; };
	DisplayString *strings[ 3 ];
	const Piece pieces[ 3 ] = { { "notice:chip", UIStyle::chipFont() }, { "notice:words", wordFont }, { "notice:until", untilFont } };
	for( Int i = 0; i < 3; ++i )
	{
		DisplayString *&s = m_broadcastTexts[ pieces[ i ].key ];
		if( s == NULL )
			s = TheDisplayStringManager->newDisplayString();
		if( s->getFont() != pieces[ i ].font )
			s->setFont( pieces[ i ].font );
		strings[ i ] = s;
	}
	DisplayString *chip = strings[ 0 ], *wordText = strings[ 1 ], *untilText = strings[ 2 ];
	chip->setText( TheGameText->fetch( maintenance ? "GUI:OnlineNoticeMaintenance" : "GUI:OnlineNoticeInfo" ) );
	untilText->setText( until );

	// the plate round a tray round [stripe] chip words until, all sized to what they hold
	const Int cut = UIStyle::px( 6.0f );						// the 45 degree corners, and the plate's side margin
	const Int rim = UIStyle::px( 3.0f );						// plate over and under the tray
	const Int trim = max( 2, UIStyle::px( 3.0f ) );
	const Int trayPadX = UIStyle::px( 10.0f ), trayPadY = UIStyle::px( 4.0f );
	const Int gap = UIStyle::px( 12.0f );
	const Int stripe = maintenance ? UIStyle::stripeWidth() : 0;
	Int chipW = 0, chipH = 0, untilW = 0, untilH = 0, wordW = 0, wordH = 0;
	chip->getSize( &chipW, &chipH );
	untilText->getSize( &untilW, &untilH );
	chipW += 2 * UIStyle::px( 8.0f );		// UIStyle::chip's own margins
	chipH += UIStyle::px( 9.0f );
	const Int fixed = stripe + 2 * trayPadX + chipW + gap + ( until.isEmpty() ? 0 : gap + untilW );
	const Int wordRoom = max( UIStyle::px( 120.0f ), widest - 2 * cut - fixed );
	wordText->setWordWrap( wordRoom );		// more lines before any word is lost
	wordText->setText( words );
	wordText->getSize( &wordW, &wordH );
	wordW = min( wordW, wordRoom );

	const Int trayW = fixed + wordW;
	const Int trayH = max( chipH, max( wordH, untilH ) ) + 2 * trayPadY;
	const Int width = trayW + 2 * cut;
	const Int height = rim + trayH + rim + cut;
	// centred on the open world, and in the shell pushed left of the logo when the centre would reach it
	Int x = left + ( right - left - width ) / 2;
	if( x + width > limit - margin )
		x = limit - margin - width;
	x = max( x, left + margin );

	const Color trimColor = maintenance ? NOTICE_WARNING : NOTICE_BLUE;
	drawSteelPlate( x, top, width, height, cut, trimColor, trim );
	const Int trayX = x + cut, trayY = top + rim;
	drawSteelTray( trayX, trayY, trayW, trayH );
	if( stripe )
		TheDisplay->drawFillRect( trayX + 1, trayY + 1, stripe, trayH - 2, NOTICE_WARNING );

	const Int middle = trayY + trayH / 2;
	const Int chipX = trayX + stripe + trayPadX;
	UIStyle::chip( chip, chipX, middle - chipH / 2, maintenance ? NOTICE_WARNING : NOTICE_BLUE_LABEL, trimColor );
	const Int wordX = chipX + chipW + gap;
	wordText->draw( wordX, middle - wordH / 2, UIStyle::INK, UIStyle::SHADOW );
	if( !until.isEmpty() )
		untilText->draw( wordX + wordW + gap, middle - untilH / 2, UIStyle::MUTED, UIStyle::SHADOW );
}

//-------------------------------------------------------------------------------------------------
/** Where an item goes in the list: the soonest to finish first, ties keeping the order they were
	* met in so a base of identical barracks does not shuffle frame to frame. */
//-------------------------------------------------------------------------------------------------
static Int productionListPlace( const InGameUI::ProductionListSlot *slots, Int count, Int remaining )
{
	Int at = count;
	while( at > 0 && slots[ at - 1 ].remaining > remaining )
		at--;
	return at;
}

//-------------------------------------------------------------------------------------------------
/** Append everything this object has queued, units and upgrades alike, into a list kept sorted by
	* how long each item still has to wait - soonest first. Everything is counted in *total; only
	* the `cells` items that finish first get a cell, whatever order they were met in.
	*
	* The wait is cumulative down a producer's queue: an entry only starts once everything ahead of
	* it has popped, so its remaining time is the time left on the entry in front plus its own build
	* time. Only the head of a queue carries progress, so this is what separates "arrives next" from
	* "arrives eventually" across a whole base's worth of buildings. A quantity modifier (the China
	* barracks' Red Guard pairs) does not multiply the wait - the entry overbuilds in place and all
	* of its units pop in the same handful of frames. */
//-------------------------------------------------------------------------------------------------
static void appendProducerQueue( Object *obj, InGameUI::ProductionListSlot *slots, Int *count, Int *total, Int cells )
{
	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if( pu == NULL )
		return;

	Player *owner = obj->getControllingPlayer();
	Int ahead = 0;					///< frames the queue in front of the current entry still needs
	Int runAt = -1;					///< where this producer's last item went, so a run of the same thing folds into it

	for( const ProductionEntry *p = pu->firstProduction(); p; p = pu->nextProduction( p ) )
	{
		Int typeKey = 0;
		Bool isUpgrade = FALSE;
		Int buildFrames = 0;
		const Image *cameo = NULL;

		if( p->getProductionType() == PRODUCTION_UNIT && p->getProductionObject() )
		{
			buildFrames = p->getProductionObject()->calcTimeToBuild( owner );
			typeKey = (Int)p->getProductionObject()->getTemplateID();
			cameo = p->getProductionObject()->getButtonImage();
		}
		else if( p->getProductionType() == PRODUCTION_UPGRADE && p->getProductionUpgrade() )
		{
			isUpgrade = TRUE;
			typeKey = (Int)p->getProductionUpgrade()->getUpgradeNameKey();
			buildFrames = p->getProductionUpgrade()->calcTimeToBuild( owner );
			cameo = p->getProductionUpgrade()->getButtonImage();
		}
		else
			continue;

		Int left = buildFrames - REAL_TO_INT( buildFrames * p->getPercentComplete() / 100.0f );
		if( left < 0 )
			left = 0;					// an item that is overbuilding while it waits for a door
		const Int remaining = ahead + left;
		ahead = remaining;

		// one item per queue entry, not per unit that entry will deliver: the China barracks' Red
		// Guard pair is one order, paid for once and cancelled once
		(*total)++;

		// a run of the same thing queued back to back in one producer folds into the cell the run
		// started, which wears an "xN" and the time the next of them arrives
		const Int id = isUpgrade ? typeKey : (Int)p->getProductionID();
		if( runAt >= 0 && runAt < *count && slots[ runAt ].typeKey == typeKey
				&& slots[ runAt ].isUpgrade == isUpgrade && slots[ runAt ].producer == obj->getID() )
		{
			// a cancel takes the newest of the run off, as the command bar's does
			slots[ runAt ].quantity++;
			slots[ runAt ].id = id;
			continue;
		}

		const Int at = productionListPlace( slots, *count, remaining );

		// something else has come between: whatever this producer queues next starts a new run
		runAt = -1;

		if( at >= cells )
			continue;						// everything already kept finishes sooner: counted, but not drawn

		if( *count < cells )
			(*count)++;

		for( Int j = *count - 1; j > at; j-- )
			slots[ j ] = slots[ j - 1 ];

		runAt = at;

		InGameUI::ProductionListSlot *slot = &slots[ at ];
		slot->producer = obj->getID();
		slot->typeKey = typeKey;
		slot->isUpgrade = isUpgrade;
		slot->remaining = remaining;
		slot->quantity = 1;
		// only the head of a queue is being built; the rest wait at 0
		slot->percent = REAL_TO_INT( p->getPercentComplete() );
		slot->cameo = cameo;
		slot->id = id;
		slot->isStructure = FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
/** A building going up on the map, for the same list the queues go in.  What a dozer or a worker is
	* putting up is nowhere in a queue, it is an object on the ground with a construction percentage on
	* it, sorted in among the queued items on the one thing they share - how long it still has. */
//-------------------------------------------------------------------------------------------------
static void appendStructureUnderConstruction( Object *obj, InGameUI::ProductionListSlot *slots, Int *count, Int *total, Int cells )
{
	// a rebuild hole wears the same status while it puts a GLA structure back up
	if( !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->isKindOf( KINDOF_REBUILD_HOLE ) )
		return;

	const Real percent = obj->getConstructionPercent();
	const Int buildFrames = obj->getTemplate()->calcTimeToBuild( obj->getControllingPlayer() );

	Int remaining = REAL_TO_INT( buildFrames * ( 1.0f - percent / 100.0f ) );
	if( remaining < 0 )
		remaining = 0;

	(*total)++;

	const Int at = productionListPlace( slots, *count, remaining );
	if( at >= cells )
		return;

	if( *count < cells )
		(*count)++;

	for( Int j = *count - 1; j > at; j-- )
		slots[ j ] = slots[ j - 1 ];

	// each site is its own place on the map, so two of the same building stay two cells
	InGameUI::ProductionListSlot *slot = &slots[ at ];
	slot->producer = obj->getID();
	slot->typeKey = 0;
	slot->isUpgrade = FALSE;
	slot->remaining = remaining;
	slot->quantity = 1;
	slot->percent = REAL_TO_INT( percent );
	slot->cameo = obj->getTemplate()->getButtonImage();
	slot->id = 0;
	slot->isStructure = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** What one Player::iterateObjects sweep fills in. */
//-------------------------------------------------------------------------------------------------
struct ProductionListGather
{
	InGameUI::ProductionListSlot *slot;
	Int *count;
	Int *total;
	ObjectID *hero;		///< the first living hero met, for the hero's cell
	Int cells;				///< how many slot has room for
};

/** What drawProductionList's last sweep was of. */
static struct
{
	Bool valid;
	UnsignedInt frame;
	const Player *player;
} s_listGather;

//-------------------------------------------------------------------------------------------------
/** Player::iterateObjects callback: everything one player has coming, queued in a factory or going
	* up on the ground, sorted against each other in the one list.  Nothing is counted twice: a
	* building under construction produces nothing until it is finished. */
//-------------------------------------------------------------------------------------------------
static void gatherProductionList( Object *obj, void *userData )
{
	ProductionListGather *g = (ProductionListGather *)userData;
	// the drawable knows whether its object has a production module, where asking the object walked
	// every module of everything the player owns
	Drawable *draw = obj->getDrawable();
	if( draw == NULL || draw->hasProductionModule() )
		appendProducerQueue( obj, g->slot, g->count, g->total, g->cells );
	appendStructureUnderConstruction( obj, g->slot, g->count, g->total, g->cells );
	// the hero the SELECT_HERO key would pick: a body still on the list while it dies is not one
	if( *g->hero == INVALID_ID && obj->isKindOf( KINDOF_HERO ) && !obj->isEffectivelyDead() )
		*g->hero = obj->getID();
}

//-------------------------------------------------------------------------------------------------
// What every seated player has coming, for the observer console's tiles (drawObserverConsole).
//-------------------------------------------------------------------------------------------------
enum
{
	OBS_QUEUE_CELLS		= 6		///< a player's soonest six
};

/// one player's queue
struct ObserverQueue
{
	Player *player;
	InGameUI::ProductionListSlot slot[ OBS_QUEUE_CELLS ];
	Int count;
	Int total;
};

/// the last sweep, once a logic frame, of everybody or of the picked player alone
static std::vector< ObserverQueue > theObserverQueues;
static Bool theObserverQueuesValid = FALSE;
static UnsignedInt theObserverQueuesFrame = 0;
static const Player *theObserverQueuesOf = NULL;

static void forgetConsole( void );

static void forgetObserverPanels( void )
{
	forgetConsole();
}

/// the player the watcher has picked, by his button, a standings row or a unit of his; NULL for none
static Player *observerPickedPlayer( void )
{
	Player *player = TheControlBar != NULL ? TheControlBar->getObserverLookAtPlayer() : NULL;
	const Int followed = TheObserverCamera.getFollowedPlayerIndex();
	if( player == NULL && followed != ObserverCamera::NO_PLAYER )
		player = ThePlayerList->getNthPlayer( followed );
	if( player == NULL || player == ThePlayerList->getLocalPlayer() || !isObserverSeat( player ) )
		return NULL;
	return player;
}

static void gatherObserverQueues( const Player *picked )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( theObserverQueuesValid && theObserverQueuesFrame == frame && theObserverQueuesOf == picked )
		return;
	theObserverQueuesValid = TRUE;
	theObserverQueuesFrame = frame;
	theObserverQueuesOf = picked;
	theObserverQueues.clear();
	const Player *local = ThePlayerList->getLocalPlayer();
	for( Int index = 0; index < ThePlayerList->getPlayerCount(); index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( player == NULL || player == local || !player->isPlayerActive() || !isObserverSeat( player ) )
			continue;
		if( picked != NULL && player != picked )
			continue;
		ObserverQueue queue;
		queue.player = player;
		queue.count = 0;
		queue.total = 0;
		ObjectID hero = INVALID_ID;
		ProductionListGather gather = { queue.slot, &queue.count, &queue.total, &hero, OBS_QUEUE_CELLS };
		player->iterateObjects( gatherProductionList, &gather );
		theObserverQueues.push_back( queue );
	}
}

//-------------------------------------------------------------------------------------------------
/** A frame drawn over a cell's edge, in screen pixels: one 800x600 pixel at the HUD's scale. */
//-------------------------------------------------------------------------------------------------
static Int hudReadoutFrame( void )
{
	const Int frame = REAL_TO_INT_FLOOR( ControlBarHudScale() + 0.5f );
	return frame > 1 ? frame : 1;
}

enum
{
	HUD_READOUT_PAD						= 2		///< the plate's margin each side of its text, screen pixels
};

//-------------------------------------------------------------------------------------------------
/** How wide a readout's text may be on `cell`: the cell less its frame and the plate's margins. */
static Int readoutRoom( const IRegion2D &cell )
{
	return cell.hi.x - cell.lo.x - 2 * hudReadoutFrame() - 2 * HUD_READOUT_PAD;
}

//-------------------------------------------------------------------------------------------------
/** Measured the way the plate is: by the string's drawn size.  getWidth() adds up the letters'
	* advances, which leaves out the last letter's overhang, so a "103s" that fitted by that sum had a
	* plate one pixel wider than its room and stood on the first column of the frame. */
Bool HudReadout_fits( DisplayString *text, const IRegion2D &cell )
{
	Int textWidth = 0;
	text->getSize( &textWidth, NULL );
	return textWidth <= readoutRoom( cell );
}

//-------------------------------------------------------------------------------------------------
void HudReadout_draw( DisplayString *text, const IRegion2D &cell, HudReadoutCorner corner, Color textColor,
											const Image *plate )
{
	Int textWidth = 0, textHeight = 0;
	text->getSize( &textWidth, &textHeight );
	if( textWidth <= 0 || textHeight <= 0 )
		return;

	// text still too wide at the smallest size loses its end to the plate's edge: the plate is never
	// wider than the cell inside its frame
	const Int frame = hudReadoutFrame();
	const Int plateWidth = min( textWidth, readoutRoom( cell ) ) + 2 * HUD_READOUT_PAD;
	const Bool atLeft = corner == HUD_READOUT_TOP_LEFT || corner == HUD_READOUT_BOTTOM_LEFT;
	const Bool atTop = corner == HUD_READOUT_TOP_LEFT || corner == HUD_READOUT_TOP_RIGHT;

	Int left = atLeft ? cell.lo.x + frame : cell.hi.x - frame - plateWidth;
	Int top = atTop ? cell.lo.y + frame : cell.hi.y - frame - textHeight;
	if( corner == HUD_READOUT_CENTRE )
	{
		left = ( cell.lo.x + cell.hi.x - plateWidth ) / 2;
		top = ( cell.lo.y + cell.hi.y - textHeight ) / 2;
	}

	// the side's plate when the sheet has one, or solid, the colour of a cell's own hole: the text is
	// read against the plate at every HUD Size, whatever picture is under it
	if( plate != NULL )
		TheDisplay->drawImage( plate, left, top, left + plateWidth, top + textHeight );
	else
		TheDisplay->drawFillRect( left, top, plateWidth, textHeight, GameMakeColor( 2, 4, 5, 255 ) );
	IRegion2D plateBox = { { left, top }, { left + plateWidth, top + textHeight } };
	text->setClipRegion( &plateBox );
	text->draw( left + HUD_READOUT_PAD, top, textColor, GameMakeColor( 0, 0, 0, 255 ) );
}

static bool buildPlanComesFirst( const BuildPlanNumber& a, const BuildPlanNumber& b )
{
	if( a.builder != b.builder )
		return a.builder < b.builder;
	if( a.current != b.current )
		return a.current != FALSE;
	return a.plan < b.plan;
}

void InGameUI_numberBuildPlans( std::vector<BuildPlanNumber>& plans )
{
	std::sort( plans.begin(), plans.end(), buildPlanComesFirst );

	size_t first = 0;
	while( first < plans.size() )
	{
		size_t end = first + 1;
		while( end < plans.size() && plans[ end ].builder == plans[ first ].builder )
			++end;

		// a lone "1" says nothing, as on a unit's order markers
		for( size_t i = first; i < end; ++i )
			plans[ i ].step = end - first > 1 ? (Int)( i - first ) + 1 : 0;
		first = end;
	}
}

//-------------------------------------------------------------------------------------------------
/** The local player's hero in a cell of the queue's own tray, against the screen's left edge over
	* the queue row whose top is `queueTop`.  It leaves the room the queue's "+N" tab takes whether the
	* tab is up or not, so the cell never jumps.  Beside the cameo, inside the cell, an upright bar fills
	* from the foot with the hero's health in the colours of the bar over his head.  Returns the cell,
	* what a click on it takes (handleHudCellClick), or nothing when there is no living hero. */
//-------------------------------------------------------------------------------------------------
static IRegion2D drawHeroCell( const HudCellStyle &style, Int queueTop, ObjectID heroID )
{
	Object *hero = TheGameLogic->findObjectByID( heroID );
	if( hero == NULL || hero->isEffectivelyDead() )
		return noHudTab();
	BodyModuleInterface *body = hero->getBodyModule();
	const Real maxHealth = body != NULL ? body->getMaxHealth() : 0.0f;
	if( maxHealth <= 0.0f )
		return noHudTab();
	const Real ratio = MAX( 0.0f, MIN( 1.0f, body->getHealth() / maxHealth ) );

	const Int left = hudPlay().lo.x;
	const Int tabTop = hudTab( -1, left, queueTop - style.production.uncoveredH, -1 ).lo.y;
	const Int barW = stripPixels( HUD_HERO_BAR_W );
	IRegion2D cell;
	cell.lo.x = left;
	cell.hi.x = left + style.cellW + barW + style.marginX;
	cell.hi.y = tabTop - stripPixels( PRODUCTION_LIST_LIFT ) - style.production.footH;
	cell.lo.y = cell.hi.y - style.cellH;
	IRegion2D cameo;
	cameo.lo.x = cell.lo.x + style.marginX;
	cameo.hi.x = cameo.lo.x + style.cameoW;
	cameo.lo.y = cell.lo.y + style.marginY;
	cameo.hi.y = cameo.lo.y + style.cameoH;
	const Int barX = cameo.hi.x + style.marginX;
	const Int barH = style.cameoH;

	Color fill = GameMakeColor( 0, 255, 0, 255 ), outline = GameMakeColor( 0, 128, 0, 255 );
	if( Drawable *draw = hero->getDrawable() )
		draw->healthBarColors( ratio, &fill, &outline );
	const Int inside = barH - 2;
	const Int filled = REAL_TO_INT( inside * ratio );

	TheDisplay->beginBatch2D();
	drawHudTray( style.production, &cell, 1, 1, 1, 1 );
	if( const Image *picture = hero->getTemplate()->getButtonImage() )
		TheDisplay->drawImage( picture, cameo.lo.x, cameo.lo.y, cameo.hi.x, cameo.hi.y );
	drawHudCellBorder( cameo, hudCellBorder( COMMAND_BUTTON_BORDER_BUILD ) );
	// the bar over his head stood up: outline, health from the foot, the health lost a dark slot
	TheDisplay->drawOpenRect( barX, cameo.lo.y, barW, barH, 1, outline );
	TheDisplay->drawFillRect( barX + 1, cameo.lo.y + 1 + inside - filled, barW - 2, filled, fill );
	TheDisplay->drawFillRect( barX + 1, cameo.lo.y + 1, barW - 2, inside - filled, GameMakeColor( 10, 10, 10, 190 ) );
	TheDisplay->endBatch2D();
	return cell;
}

//-------------------------------------------------------------------------------------------------
/** Everything the local player has coming, as bare cameos on the left over the
	* control bar: every producer's queue and every building a dozer or a worker has going up, soonest
	* to finish at the left, in one row.  Each wears its seconds in the bottom left corner, an "xN" in
	* the top right for a run of the same thing, and a scrim swept off as its own build runs.
	* PRODUCTION_LIST_CELLS cells, and a "+N" tab sticking up from the tray when more is coming than
	* they show.  The sweep reads the logic, which only moves on a logic frame, so
	* it is taken once a frame and the render passes between two frames draw what it found; a script can
	* change a queue while time is frozen, so a frozen pass sweeps every time. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawProductionList( void )
{
	// nothing drawn is nothing to click
	m_productionListCells = 0;
	m_productionListTab = noHudTab();
	m_heroCell = noHudTab();
	forgetObserverPanels();
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

	// the Classic layout (HUD_LAYOUT_RETAIL) has neither the list nor the hero's cell over it
	if( TheGlobalData->m_hudLayout == HUD_LAYOUT_RETAIL )
		return;

	// a watcher has no queue of his own
	Player *player = ThePlayerList->getLocalPlayer();
	if( !player->isPlayerActive() )
		return;

	const Bool timeFrozen = ( !TheGameEngine->isMultiplayerSession() && TheTacticalView->isTimeFrozen()
														&& !TheTacticalView->isCameraMovementFinished() )
													|| TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript();
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( timeFrozen || !s_listGather.valid || s_listGather.frame != frame || s_listGather.player != player )
	{
		m_productionListCount = 0;
		m_productionListTotal = 0;
		m_hero = INVALID_ID;
		ProductionListGather gather = { m_productionList, &m_productionListCount, &m_productionListTotal, &m_hero, PRODUCTION_LIST_CELLS };
		player->iterateObjects( gatherProductionList, &gather );
		s_listGather.valid = TRUE;
		s_listGather.frame = frame;
		s_listGather.player = player;
	}

	// one row in a tray hard against the screen's left edge, the powers tray mirrored, its foot
	// standing just clear of the radar's frame; the hero's cell stands over it, queue or none
	const HudCellStyle style = hudCellStyle();
	const Int foot = radarFrameTop() - stripPixels( PRODUCTION_LIST_LIFT ) - style.production.footH;
	// the Direct Strike HUD sells and shows its own hero card, so the cell would be a second hero at the
	// screen's edge; Apocalypse has no such HUD and keeps it
	if( !DirectStrikeHud_isShowing() )
		m_heroCell = drawHeroCell( style, foot - style.cellH, m_hero );
	// the Red Alert style sidebar's cameos wear their own queue's clocks and counts; the hero's cell
	// stays, over the selection card (radarFrameTop)
	if( m_productionListCount == 0 || sidebarLayoutOn() )
		return;

	// a cell wearing an "x5" stands for five.  Only a full list leaves anything out, and the tab
	// counts everything no cameo shows
	Int shown = 0;
	for( Int each = 0; each < m_productionListCount; each++ )
		shown += m_productionList[ each ].quantity;
	const Bool more = m_productionListTotal > shown;
	const Int cameos = m_productionListCount;

	const Int cameoW = style.cameoW;
	const Int cameoH = style.cameoH;
	const Int left = hudPlay().lo.x;
	IRegion2D place[ PRODUCTION_LIST_CELLS ];
	for( Int each = 0; each < cameos; each++ )
	{
		place[ each ].lo.x = left + each * style.cellW + style.marginX;
		place[ each ].hi.y = foot - style.marginY;
		place[ each ].hi.x = place[ each ].lo.x + cameoW;
		place[ each ].lo.y = place[ each ].hi.y - cameoH;
		m_productionListPlace[ each ] = hudCellBox( style, place[ each ] );
	}
	m_productionListCells = cameos;
	// over the first cell, above the tray's own top edge
	if( more )
		m_productionListTab = hudTab( -1, left, m_productionListPlace[ 0 ].lo.y - style.production.uncoveredH, -1 );

	// ctrl is the cells' cancel key, so while it is held the cameo a click would cancel says so: it goes
	// red and wears a minus.  An accidental cancel costs the whole item
	Int cancelHover = -1;
	if( TheKeyboard->isCtrl() )
	{
		const MouseIO *io = TheMouse->getMouseStatus();
		for( Int each = 0; each < cameos; each++ )
		{
			const Object *producer = TheGameLogic->findObjectByID( m_productionList[ each ].producer );
			const IRegion2D &box = m_productionListPlace[ each ];
			if( !m_productionList[ each ].isStructure && producer != NULL && producer->isLocallyControlled()
					&& io->pos.x >= box.lo.x && io->pos.x < box.hi.x && io->pos.y >= box.lo.y && io->pos.y < box.hi.y )
				cancelHover = each;
		}
	}

	// a piece at a time over every cell, so a run of the same piece is one draw call; see beginBatch2D
	TheDisplay->beginBatch2D();
	drawHudTray( style.production, m_productionListPlace, cameos, PRODUCTION_LIST_COLUMNS, 1, 1 );
	for( Int each = 0; each < cameos; each++ )
		if( m_productionList[ each ].cameo != NULL )
			TheDisplay->drawImage( m_productionList[ each ].cameo, place[ each ].lo.x, place[ each ].lo.y,
														 place[ each ].hi.x, place[ each ].hi.y );
	// swept off the way the command bar's own clock is; an item still waiting wears all of it
	for( Int each = 0; each < cameos; each++ )
		TheDisplay->drawRemainingRectClock( place[ each ].lo.x, place[ each ].lo.y, cameoW, cameoH,
																				m_productionList[ each ].percent, GameMakeColor( 0, 0, 0, 100 ) );
	// the command bar's own: build for a unit or a structure, upgrade for an upgrade
	for( Int each = 0; each < cameos; each++ )
		drawHudCellBorder( place[ each ], hudCellBorder( m_productionList[ each ].isUpgrade ? COMMAND_BUTTON_BORDER_UPGRADE
																																										: COMMAND_BUTTON_BORDER_BUILD ) );
	if( cancelHover >= 0 )
	{
		const IRegion2D &box = place[ cancelHover ];
		const Int barInset = cameoW / 4;
		const Int barHeight = stripPixels( 4 );
		TheDisplay->drawFillRect( box.lo.x, box.lo.y, cameoW, cameoH, GameMakeColor( 190, 0, 0, 120 ) );
		TheDisplay->drawFillRect( box.lo.x + barInset, box.lo.y + ( cameoH - barHeight ) / 2, cameoW - 2 * barInset, barHeight,
															GameMakeColor( 255, 255, 255, 255 ) );
	}
	TheDisplay->endBatch2D();

	const Color white = GameMakeColor( 255, 255, 255, 255 );
	for( Int each = 0; each < cameos; each++ )
	{
		const ProductionListSlot &slot = m_productionList[ each ];
		drawCellSeconds( m_productionListText[ 2 * each ], ControlBar_secondsFromFrames( (Real)slot.remaining ),
										 place[ each ], style.plate );
		if( slot.quantity > 1 )
		{
			UnicodeString count;
			count.format( u"x%d", slot.quantity );
			HudReadout_draw( cellString( m_productionListText[ 2 * each + 1 ], count ), place[ each ],
											 HUD_READOUT_TOP_RIGHT, white, style.plate );
		}
	}
	if( more )
	{
		UnicodeString rest;
		rest.format( u"+%d", m_productionListTotal - shown );
		drawHudTab( style.tabUp, m_productionListTab, -1, cellString( m_productionListMoreText, rest ) );
	}
}

/// a number on a dark plate in a cameo's corner, the theme's small face
static void drawObserverReadout( const UnicodeString &text, const IRegion2D &cameo, Bool top, Bool right, Color colour )
{
	// the broadcast floor: 14 px at 1080, on a dark (8,9,10) chip (PIECES.md)
	DisplayString *string = DirectStrikeTheme::pooled( text, DirectStrikeTheme::font( 14 ) );
	if( string == NULL )
		return;
	Int width = 0, height = 0;
	string->getSize( &width, &height );
	const Int x = right ? cameo.hi.x - width - 2 : cameo.lo.x + 2;
	const Int y = top ? cameo.lo.y + 1 : cameo.hi.y - height - 1;
	TheDisplay->drawFillRect( x - 2, y, width + 4, height, GameMakeColor( 8, 9, 10, 230 ) );
	string->draw( x, y, colour, GameMakeColor( 0, 0, 0, 255 ) );
}


//=================================================================================================
// The watcher's HUD, round 24: the owner's pick of 2026-11-24, the HUD as one more side bar (the
// wrapper's experiments/proofs/2026-11-24/spirit/b-bar/README-spec.md).  America's radar box bottom left,
// its square glass the radar and five keys beside it (standings, vision, chat, options, quit); China's
// centre panel for the followed player where the command grid stands; Turkiye's box bottom right, as tall
// as its list, holding the roster or, with the standings key down, the ranked standings.  The paintings
// are Tools/hud_command_bar.py's (paint_obs_*), the rects GameClient/ObserverHudLayout.h's at 1920x1080
// (obsMap puts them on the screen); the rows, rulings, words and pictures are drawn here.  Everything
// reads the logic and writes nothing back.  The stock observer bar's windows are hidden but the radar's,
// which stands in the radar box's glass (keepConsoleWindows).
//=================================================================================================

enum
{
	CON_STATS					= OBSHUD_STATS		///< Money, Income, Army, Kills, Power, Builds, Super, as the keys Q W E R T Y A
};

/** ObserverHudLayout.h's numbers on this screen: k turns a length into the screen's, the radar box hangs from
	* the left edge (L), the right box from the right (R), the followed player's panel from the middle (C, moved
	* by shift at 1080 to clear a wide roster), all on the screen's foot (Y). */
struct ObsMap
{
	Real k;
	Int left, right, bottom, shift;
	Int S( Real at1080 ) const { return REAL_TO_INT_FLOOR( at1080 * k + 0.5f ); }
	Int L( Real x ) const { return left + S( x ); }
	Int R( Real x ) const { return right - S( 1920 - x ); }
	Int C( Real x ) const { return ( left + right ) / 2 + S( x + shift - 960 ); }
	Int Y( Real y ) const { return bottom - S( 1080 - y ); }
	static IRegion2D box( Int x0, Int y0, Int x1, Int y1 ) { IRegion2D r; r.lo.x = x0; r.lo.y = y0; r.hi.x = x1; r.hi.y = y1; return r; }
	IRegion2D atL( const ObsRect &r ) const { return box( L( r.x0 ), Y( r.y0 ), L( r.x1 ), Y( r.y1 ) ); }
	IRegion2D atR( const ObsRect &r ) const { return box( R( r.x0 ), Y( r.y0 ), R( r.x1 ), Y( r.y1 ) ); }
	IRegion2D atC( const ObsRect &r ) const { return box( C( r.x0 ), Y( r.y0 ), C( r.x1 ), Y( r.y1 ) ); }
};

static ObsMap obsMap( void )
{
	ObsMap m;
	const IRegion2D play = hudPlay();
	const Int screenW = play.hi.x - play.lo.x, screenH = TheDisplay->getHeight();
	m.k = min( screenH / 1080.0f, screenW / 1920.0f );
	m.left = play.lo.x;
	m.right = play.hi.x;
	m.bottom = screenH;
	m.shift = 0;
	return m;
}

/// a team's fixed colour, the same every match so a viewer can say "blue team"; gold is the followed seat's alone
static Color consoleTeamColour( Int team )
{
	return DirectStrikeTheme::teamColour( team );
}

/// a pulse on the wall clock, 0 to 1 and back once a period: the motion cues run in real time, the same
/// on a paused replay, a fast-forward and a slow machine
static Real consolePulse( UnsignedInt periodMs )
{
	const UnsignedInt now = Clock_Milliseconds();
	return 0.5f - 0.5f * Cos( 2.0f * PI * ( now % periodMs ) / (Real)periodMs );
}

/// dollars in one format in every row and every sum: thousands to one decimal, "$4.1k", "$206.6k"
static UnicodeString consoleMoney( Int value )
{
	const Int tenths = ( max( value, 0 ) + 50 ) / 100;
	UnicodeString text;
	text.format( u"$%d.%dk", tenths / 10, tenths % 10 );
	return text;
}

/// What the console knows of one seat, gathered each draw from the standings the rack's refresh keeps.
struct ConsoleSeat
{
	const SpectatorStats *stats;
	Player *player;
	Int index;									///< the player's index
	Bool out;										///< beaten: the dark tile with the skull
	Int value[ CON_STATS ];
	const ObserverQueue *queue;	///< NULL with nothing coming
	Bool hasSuperweapon, superweaponReady;
	Int superweaponPercent, superweaponSeconds;
	const Image *superweaponCameo;	///< the soonest one's picture
	Int power;									///< 0 enough, 1 short, 2 none
	Int seat;										///< the lobby seat's number, 1 up: the roster's #
	UnicodeString label;				///< the name, with the seat's number after it when another seat shares it
};

struct ConsoleHit
{
	IRegion2D box;
	Int action;		///< InGameUI::ConsoleAction
	Int arg;
};
static std::vector< ConsoleHit > theConsoleHits;
static IRegion2D theConsoleBlock[ 4 ];			///< radar, board, card: a press on any is the HUD's
static Int theConsoleOrder[ MAX_PLAYER_COUNT ];	///< the player indices in the board's order, for prev and next
static Int theConsoleOrderCount = 0;

static void forgetConsole( void )
{
	theConsoleHits.clear();
	for( Int each = 0; each < 4; each++ )
		theConsoleBlock[ each ] = noHudTab();
	theConsoleShown = FALSE;
}

static Bool consoleHot( const IRegion2D &r )
{
	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	return mouse.x >= r.lo.x && mouse.x < r.hi.x && mouse.y >= r.lo.y && mouse.y < r.hi.y;
}

/// one of the watcher's pieces (ReforgedHudObsPieces, ReforgedHudObsSpec), ObserverPro<name><state>
static const Image *obsPiece( const char *name, const char *state = "" )
{
	AsciiString full;
	full.format( "ObserverPro%s%s", name, state );
	return TheMappedImageCollection != NULL ? TheMappedImageCollection->findImageByName( full ) : NULL;
}

/// a key of the watcher's sheet: its piece in the state the pointer and `lit` say (P lit or held down, H under
/// the pointer), and a click that does `action`; TRUE while the pointer is on it.  A key that cannot act draws its
/// I piece and takes no click (the radar box's steel still swallows it)
static Bool consoleKey( const char *piece, const IRegion2D &r, Bool lit, Int action, Int arg, Bool usable = TRUE )
{
	const Bool hot = consoleHot( r );
	if( !usable )
	{
		if( const Image *image = obsPiece( piece, "I" ) )
			TheDisplay->drawImage( image, r.lo.x, r.lo.y, r.hi.x, r.hi.y );
		return hot;
	}
	const Bool held = hot && TheMouse->getMouseStatus()->leftState != MBS_Up;
	const Image *image = obsPiece( piece, lit || held ? "P" : hot ? "H" : "" );
	if( image != NULL )
		TheDisplay->drawImage( image, r.lo.x, r.lo.y, r.hi.x, r.hi.y );
	else
		TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, DirectStrikeTheme::KEY_FACE );
	const ConsoleHit hit = { r, action, arg };
	theConsoleHits.push_back( hit );
	return hot;
}

/// Keep the stock observer bar's windows out from under the HUD: every child of the bar's frame down but
/// the radar, which stands in the radar box's glass (or is down too while the watcher hides it).
static void keepConsoleWindows( const IRegion2D &glass )
{
	static NameKeyType parentKey = NAMEKEY( "ControlBar.wnd:ControlBarParent" );
	static NameKeyType radarKey = NAMEKEY( "ControlBar.wnd:LeftHUD" );
	GameWindow *parent = TheWindowManager->winGetWindowFromId( NULL, parentKey );
	if( parent == NULL )
		return;
	for( GameWindow *child = parent->winGetChild(); child != NULL; child = child->winGetNext() )
	{
		const Bool radar = child->winGetWindowId() == radarKey;
		if( !radar )
		{
			if( !child->winIsHidden() )
				child->winHide( TRUE );
			continue;
		}
		if( child->winIsHidden() != theConsoleHidden[ CON_MODULE_RADAR ] )
			child->winHide( theConsoleHidden[ CON_MODULE_RADAR ] );
		Int px = 0, py = 0, x = 0, y = 0, w = 0, h = 0;
		parent->winGetScreenPosition( &px, &py );
		child->winGetScreenPosition( &x, &y );
		child->winGetSize( &w, &h );
		// the window is RadarUL/LR, the glass's rect, as Observer8x6 names it
		const Int wantX = glass.lo.x, wantY = glass.lo.y;
		const Int wantW = glass.hi.x - glass.lo.x, wantH = glass.hi.y - glass.lo.y;
		if( x != wantX || y != wantY )
			child->winSetPosition( wantX - px, wantY - py );
		if( w != wantW || h != wantH )
			child->winSetSize( wantW, wantH );
	}
}

/// the soonest of a player's superweapon clocks: ready ones first
static void consoleSuperweapon( const SuperweaponMap &powers, ConsoleSeat *seat )
{
	seat->hasSuperweapon = FALSE;
	seat->superweaponReady = FALSE;
	seat->superweaponPercent = 0;
	seat->superweaponSeconds = 0;
	seat->superweaponCameo = NULL;
	Int best = -1;
	for( SuperweaponMap::const_iterator mapIt = powers.begin(); mapIt != powers.end(); ++mapIt )
	{
		for( SuperweaponList::const_iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt )
		{
			const SuperweaponInfo *info = *listIt;
			if( info == NULL || info->m_hiddenByScript || info->m_hiddenByScience )
				continue;
			Object *building = TheGameLogic->findObjectByID( info->m_id );
			if( building == NULL || building->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
				continue;
			SpecialPowerModuleInterface *module = building->getSpecialPowerModule( info->getSpecialPowerTemplate() );
			if( module == NULL )
				continue;
			const UnsignedInt now = TheGameLogic->getFrame();
			const UnsignedInt wait = module->getReadyFrame() > now ? module->getReadyFrame() - now : 0;
			const UnsignedInt reload = info->getSpecialPowerTemplate()->getReloadTime();
			const Int rank = module->isReady() ? 0 : (Int)wait + 1;
			if( best >= 0 && rank >= best )
				continue;
			best = rank;
			seat->hasSuperweapon = TRUE;
			seat->superweaponReady = module->isReady();
			seat->superweaponCameo = superweaponCameo( info->getSpecialPowerTemplate() );
			seat->superweaponSeconds = ControlBar_secondsFromFrames( (Real)wait );
			seat->superweaponPercent = seat->superweaponReady || reload == 0 ? 100 : ( wait >= reload ? 0 : REAL_TO_INT( 100.0f * ( reload - wait ) / reload ) );
		}
	}
}

/// a seat's value of a console stat as the tile and the tab's scale write it
static UnicodeString consoleValueText( Int stat, const ConsoleSeat &seat )
{
	UnicodeString text;
	const Int v = seat.value[ stat ];
	switch( stat )
	{
		case 0: return seat.stats->unlimited ? TheGameText->fetch( "GUI:StartingMoneyUnlimited" ) : consoleMoney( v );
		case 1: return broadcastNumber( "+", max( v, 0 ) );
		case 2: return consoleMoney( v );
		case 4: text.format( u"%+d", v ); return text;
		case 6:
			if( !seat.hasSuperweapon )
				return UnicodeString( u"-" );
			if( seat.superweaponReady )
				return TheGameText->fetch( "GUI:ObsConReady" );
			text.format( u"%d:%02d", seat.superweaponSeconds / 60, seat.superweaponSeconds % 60 );
			return text;
		default: text.format( u"%d", v ); return text;
	}
}

/// one of a player's general's powers and superweapons, its clock
struct ConsolePower { const Image *cameo; Bool ready; Int seconds; Int percent; ObjectID building; };

static std::vector< ConsolePower > consolePowers( const SuperweaponMap &powers )
{
	std::vector< ConsolePower > list;
	for( SuperweaponMap::const_iterator mapIt = powers.begin(); mapIt != powers.end(); ++mapIt )
		for( SuperweaponList::const_iterator listIt = mapIt->second.begin(); listIt != mapIt->second.end(); ++listIt )
		{
			const SuperweaponInfo *info = *listIt;
			if( info == NULL || info->m_hiddenByScript || info->m_hiddenByScience )
				continue;
			Object *building = TheGameLogic->findObjectByID( info->m_id );
			if( building == NULL || building->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
				continue;
			SpecialPowerModuleInterface *module = building->getSpecialPowerModule( info->getSpecialPowerTemplate() );
			if( module == NULL )
				continue;
			const UnsignedInt now = TheGameLogic->getFrame();
			const UnsignedInt wait = module->getReadyFrame() > now ? module->getReadyFrame() - now : 0;
			const UnsignedInt reload = info->getSpecialPowerTemplate()->getReloadTime();
			ConsolePower power;
			power.cameo = superweaponCameo( info->getSpecialPowerTemplate() );
			power.ready = module->isReady();
			power.seconds = ControlBar_secondsFromFrames( (Real)wait );
			power.percent = power.ready || reload == 0 ? 100 : ( wait >= reload ? 0 : REAL_TO_INT( 100.0f * ( reload - wait ) / reload ) );
			power.building = building->getID();
			list.push_back( power );
			if( info->getSpecialPowerTemplate()->isSharedNSync() )
				break;
		}
	std::stable_sort( list.begin(), list.end(), []( const ConsolePower &a, const ConsolePower &b ) { return a.ready != b.ready ? a.ready : a.seconds < b.seconds; } );
	return list;
}

//-------------------------------------------------------------------------------------------------
// The watcher's HUD's small drawing: the inks README-spec.md names, a hairline, a player's colour in its light
// keyline, a picture set in a well as the bar sets a cameo, the power bar's flat fill, a caret, the best's rule
//-------------------------------------------------------------------------------------------------
static const Color OBS_PAPER		= GameMakeColor( 232, 234, 240, 255 );
static const Color OBS_HAIR			= GameMakeColor( 66, 70, 78, 255 );
static const Color OBS_HAIR_LIT	= GameMakeColor( 104, 108, 116, 255 );
static const Color OBS_KEYLINE	= GameMakeColor( 170, 176, 186, 255 );
static const Color OBS_CAMEO_RIM	= GameMakeColor( 92, 98, 108, 255 );

static void obsFill( Int x0, Int y0, Int x1, Int y1, Color c )
{
	if( x1 > x0 && y1 > y0 )
		TheDisplay->drawFillRect( x0, y0, x1 - x0, y1 - y0, c );
}

static void obsHair( Int x0, Int x1, Int y, Color c = OBS_HAIR )
{
	obsFill( x0, y, x1, y + 1, c );
}

static void obsSwatch( Int x0, Int y0, Int x1, Int y1, Color c )
{
	obsFill( x0 - 1, y0 - 1, x1 + 1, y1 + 1, OBS_KEYLINE );
	obsFill( x0, y0, x1, y1, GameMakeColor( 8, 9, 10, 255 ) );
	obsFill( x0 + 1, y0 + 1, x1 - 1, y1 - 1, c );
}

static void obsSlot( const IRegion2D &r, const Image *picture, Bool dim, Color rim = OBS_CAMEO_RIM )
{
	const Int w = r.hi.x - r.lo.x, h = r.hi.y - r.lo.y;
	obsFill( r.lo.x, r.lo.y, r.hi.x, r.hi.y, GameMakeColor( 6, 7, 8, 255 ) );
	if( picture != NULL )
	{
		const IRegion2D inner = ObsMap::box( r.lo.x + 1, r.lo.y + 1, r.hi.x - 1, r.hi.y - 1 );
		UIStyle::coverImage( picture, inner.lo.x, inner.lo.y, w - 2, h - 2, &inner );
		TheDisplay->enableClipping( FALSE );		// coverImage leaves its clip on for a caller that sets one
		if( dim )
			obsFill( inner.lo.x, inner.lo.y, inner.hi.x, inner.hi.y, GameMakeColor( 6, 7, 8, 150 ) );
	}
	TheDisplay->drawOpenRect( r.lo.x, r.lo.y, w, h, 1.0f, rim );
	obsHair( r.lo.x + 1, r.hi.x - 1, r.lo.y + 1, GameMakeColor( 255, 255, 255, 60 ) );
	obsHair( r.lo.x + 1, r.hi.x - 1, r.hi.y - 2, GameMakeColor( 0, 0, 0, 120 ) );
}

/// what the code pours into a power bar's channel: a flat fill with a lighter top line and a darker foot
static void obsBar( Int x0, Int y0, Int x1, Int y1, Color c )
{
	if( x1 <= x0 )
		return;
	UnsignedByte r, g, b, a;
	GameGetColorComponents( c, &r, &g, &b, &a );
	obsFill( x0, y0, x1, y1, c );
	obsHair( x0, x1, y0, GameMakeColor( min( 255, r * 135 / 100 + 30 ), min( 255, g * 135 / 100 + 30 ), min( 255, b * 135 / 100 + 30 ), 255 ) );
	obsHair( x0, x1, y1 - 1, GameMakeColor( r * 55 / 100, g * 55 / 100, b * 55 / 100, 255 ) );
}

static void obsCaret( Int cx, Int cy, Int half, Color c )
{
	for( Int row = 0; row <= half; row++ )
		obsFill( cx - half + row, cy - half / 2 + row, cx + half - row + 1, cy - half / 2 + row + 1, c );
}

/// the picture a seat goes by: his army's general, or his side's Command Center (UIStyle::armyPortrait)
static const Image *obsPortrait( const Player *player )
{
	return UIStyle::armyPortrait( player->getPlayerTemplate() );
}

/// a word of the string table, or its language's short form (<key>_Short) when the long one is wider than room
static UnicodeString obsWord( const char *key, GameFont *font, Int room )
{
	UnicodeString word = TheGameText->fetch( key );
	if( room <= 0 || DirectStrikeTheme::textWidth( word, font ) <= room )
		return word;
	AsciiString shortKey;
	shortKey.format( "%s_Short", key );
	Bool exists = FALSE;
	const UnicodeString shorter = TheGameText->fetch( shortKey, &exists );
	return exists ? shorter : word;
}

/// the lobby seat a player sits in, 1 up, as the roster's # shows it
static Int obsSeatNumber( const Player *player )
{
	for( Int slot = 0; slot < MAX_SLOTS; slot++ )
	{
		AsciiString name;
		name.format( "player%d", slot );
		if( TheNameKeyGenerator->nameToKey( name ) == player->getPlayerNameKey() )
			return slot + 1;
	}
	return player->getPlayerIndex() + 1;
}

/// the stat words in the order of the stat keys
static const char *const OBS_STAT_WORDS[ CON_STATS ] = { "GUI:ObsConStatMoney", "GUI:ObsConStatIncome", "GUI:ObsConStatArmy",
	"GUI:ObsConStatKills", "GUI:ObsConStatPower", "GUI:ObsConStatBuilds", "GUI:ObsConStatSuper" };
/// the camera dropdown's entries, in CON_CAMERA's order
static const char *const OBS_CAMERA_WORDS[ 3 ] = { "GUI:ObsConCamFree", "GUI:ObsConCamFollow", "GUI:ObsConCamDirector" };

/// the open dropdown (0 the camera, 1 the stat) or none, the standings key, and the pass the Tab screen last drew in
static Int theDropOpen = -1;
static Bool theStandingsOpen = FALSE;
static UnsignedInt theTabDrawnPass = 0;
/// the Tab screen's mute keys, drawn by drawObserverTab and pressed through clickObserverTab
struct ObsTabMute { IRegion2D box; Int slot; };
static std::vector< ObsTabMute > theTabMutes;

/// every seat with its numbers, in the roster's order (team by team, each team in its seats' order)
static void gatherConsoleSeats( std::vector< ConsoleSeat > &seats, const SuperweaponMap *superweapons )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( theRackStatsStat != 0 || frame < theRackStatsFrame || frame >= theRackStatsFrame + RACK_REFRESH_FRAMES || theRackStats.empty() )
	{
		theRackStats = gatherSpectatorStats( SPECTATOR_STATS[ 0 ], ThePlayerList->getLocalPlayer(), PLAYERMASK_ALL, TRUE );
		theRackStatsFrame = frame;
		theRackStatsStat = 0;
	}
	gatherObserverQueues( NULL );
	seats.clear();
	for( size_t row = 0; row < theRackStats.size() && seats.size() < (size_t)MAX_SLOTS; row++ )
	{
		ConsoleSeat seat;
		seat.stats = &theRackStats[ row ];
		seat.player = theRackStats[ row ].player;
		seat.index = seat.player->getPlayerIndex();
		seat.out = !seat.player->isPlayerActive();
		seat.queue = NULL;
		for( size_t q = 0; q < theObserverQueues.size(); q++ )
			if( theObserverQueues[ q ].player == seat.player && theObserverQueues[ q ].count > 0 )
				seat.queue = &theObserverQueues[ q ];
		consoleSuperweapon( superweapons[ seat.index ], &seat );
		const Energy *energy = seat.player->getEnergy();
		seat.power = energy->getProduction() == 0 && energy->getConsumption() > 0 ? 2 : ( energy->hasSufficientPower() ? 0 : 1 );
		seat.value[ 0 ] = seat.stats->unlimited ? 0 : seat.stats->cash;
		seat.value[ 1 ] = seat.stats->income;
		seat.value[ 2 ] = seat.stats->army;
		seat.value[ 3 ] = seat.stats->kills;
		seat.value[ 4 ] = seat.stats->power;
		seat.value[ 5 ] = seat.queue != NULL ? seat.queue->total : 0;
		seat.value[ 6 ] = seat.hasSuperweapon ? ( seat.superweaponReady ? 1000000 : seat.superweaponPercent ) : -1;
		seat.seat = obsSeatNumber( seat.player );
		seat.label = seat.player->getPlayerDisplayName();
		seats.push_back( seat );
	}
	// two seats with one name each carry their seat's number where a list sorts them away from the roster's #
	for( size_t each = 0; each < seats.size(); each++ )
		for( size_t other = 0; other < seats.size(); other++ )
			if( other != each && seats[ other ].player->getPlayerDisplayName().compare( seats[ each ].player->getPlayerDisplayName() ) == 0 )
			{
				UnicodeString number;
				number.format( u" %d", seats[ each ].seat );
				seats[ each ].label = seats[ each ].player->getPlayerDisplayName();
				seats[ each ].label.concat( number );
				break;
			}
	std::stable_sort( seats.begin(), seats.end(), []( const ConsoleSeat &a, const ConsoleSeat &b )
		{
			return a.stats->team != b.stats->team ? a.stats->team < b.stats->team : a.seat < b.seat;
		} );
	theConsoleOrderCount = (Int)seats.size();
	for( Int each = 0; each < theConsoleOrderCount; each++ )
		theConsoleOrder[ each ] = seats[ each ].index;
}

/// the best of a stat among the seats still standing, when one seat alone holds it; -1 otherwise
static Int obsBest( const std::vector< ConsoleSeat > &seats, Int stat )
{
	Int best = -1, holders = 0;
	for( size_t each = 0; each < seats.size(); each++ )
	{
		if( seats[ each ].out )
			continue;
		const Int v = seats[ each ].value[ stat ];
		if( holders == 0 || v > best )
		{
			best = v;
			holders = 1;
		}
		else if( v == best )
			holders++;
	}
	return holders == 1 ? best : -1;
}

/// The right box's painting at box (from its top to the screen's foot) and its mount: the box's four crop
/// slices (ReforgedHudObsSpec), Big from the box's top left cut to its size, Right down the screen's edge, Bottom
/// along its foot, the Corner in the screen's corner, each scaled by k
static void drawObsBox( const ObsMap &m, const ObsRightBox &rb )
{
	const IRegion2D box = m.atR( rb.box );
	const Int c = m.S( OBSHUD_BOX_CUT );
	const Image *big = obsPiece( "BoxBig" ), *right = obsPiece( "BoxRight" ), *bottom = obsPiece( "BoxBottom" ), *corner = obsPiece( "BoxCorner" );
	if( big == NULL || right == NULL || bottom == NULL || corner == NULL )
	{
		obsFill( box.lo.x, box.lo.y, box.hi.x, box.hi.y, DirectStrikeTheme::PLATE );
		return;
	}
	const Int bigW = m.S( (Real)big->getImageWidth() ), bigH = m.S( (Real)big->getImageHeight() );
	auto clipped = [ & ]( const Image *image, Int x, Int y, Int w, Int h, Int cx0, Int cy0, Int cx1, Int cy1 )
	{
		if( cx1 <= cx0 || cy1 <= cy0 )
			return;
		IRegion2D clip = ObsMap::box( cx0, cy0, cx1, cy1 );
		TheDisplay->setClipRegion( &clip );
		TheDisplay->enableClipping( TRUE );
		TheDisplay->drawImage( image, x, y, x + w, y + h );
		TheDisplay->enableClipping( FALSE );
	};
	clipped( big, box.lo.x, box.lo.y, bigW, bigH, box.lo.x, box.lo.y, box.hi.x - c, box.hi.y - c );
	clipped( right, box.hi.x - c, box.lo.y, c, bigH, box.hi.x - c, box.lo.y, box.hi.x, box.hi.y - c );
	clipped( bottom, box.lo.x, box.hi.y - c, bigW, c, box.lo.x, box.hi.y - c, box.hi.x - c, box.hi.y );
	TheDisplay->drawImage( corner, box.hi.x - c, box.hi.y - c, box.hi.x, box.hi.y );
	if( const Image *mount = obsPiece( "Mount" ) )
	{
		const IRegion2D r = m.atR( rb.mount );
		TheDisplay->drawImage( mount, r.lo.x, r.lo.y, r.hi.x, r.hi.y );
	}
}

/// "$1.0k HIGHEST" at the foot of a list: the key to the white rule under a column's best
static void drawObsLegend( const ObsMap &m, Int x, Int y )
{
	using namespace DirectStrikeTheme;
	GameFont *labelFont= font( OBSHUD_FLOOR );
	const UnicodeString sample = consoleMoney( 1000 );
	const Int w = drawText( sample, labelFont,x, y, m.S( 22 ), TEXT_WHITE );
	obsFill( x, y + m.S( 19 ), x + w, y + m.S( 19 ) + max( 1, m.S( 2 ) ), TEXT_WHITE );
	drawText( TheGameText->fetch( "GUI:ObsConHighest" ), labelFont,x + w + m.S( 8 ), y, m.S( 22 ), TEXT_GREY );
}

/// a seat's name struck through in red, for a seat that is out
static void drawObsStruck( const UnicodeString &name, GameFont *f, Int x, Int y, Int h, Int room )
{
	using namespace DirectStrikeTheme;
	const UnicodeString shown = UIStyle::fitName( name, f, max( room, 1 ) );
	const Int w = drawText( shown, f, x, y, h, TEXT_DIM );
	obsFill( x, y + h / 2, x + w, y + h / 2 + max( 1, REAL_TO_INT( scale() * 2 ) ), TEXT_RED );
}

/// the parts of a seat's row the roster and the standings share: the gold rim of the followed seat, the red
/// one of a seat under attack, the place or seat number, the team's tick, the picture, the colour; returns where
/// the name starts
static Int drawObsSeatHead( const ObsMap &m, const ConsoleSeat &seat, const IRegion2D &r, Int number, Int tickAt, Bool followed, Bool hot )
{
	using namespace DirectStrikeTheme;
	const Int h = r.hi.y - r.lo.y, ym = ( r.lo.y + r.hi.y ) / 2;
	if( hot )
		obsHair( r.lo.x, r.hi.x, r.lo.y, OBS_HAIR_LIT );
	if( followed )
		TheDisplay->drawOpenRect( r.lo.x + 1, r.lo.y + 1, r.hi.x - r.lo.x - 2, h - 2, 1.0f, TEXT_GOLD );
	else if( seat.stats->underAttack && !seat.out )
		TheDisplay->drawOpenRect( r.lo.x + 1, r.lo.y + 1, r.hi.x - r.lo.x - 2, h - 2, max( 1.0f, m.k * 2 ),
			UIStyle::withAlpha( TEXT_RED, REAL_TO_INT( 255 * ( 0.35f + 0.65f * consolePulse( 500 ) ) ) ) );
	UnicodeString n;
	n.format( u"%d", number );
	drawText( n, font( OBSHUD_FLOOR ), r.lo.x + m.S( 13 ), r.lo.y, h, followed ? TEXT_GOLD : TEXT_GREY, 0 );
	obsFill( r.lo.x + m.S( tickAt ), r.lo.y + m.S( 3 ), r.lo.x + m.S( tickAt + 3 ), r.hi.y - m.S( 4 ), consoleTeamColour( seat.stats->team ) );
	const Int ps = h - m.S( 6 ), px = r.lo.x + m.S( tickAt + 5 );
	obsSlot( ObsMap::box( px, r.lo.y + m.S( 3 ), px + ps, r.lo.y + m.S( 3 ) + ps ), obsPortrait( seat.player ), seat.out );
	const Int sx = px + ps + m.S( 8 ), half = m.S( h > m.S( 28 ) ? 7.0f : 5.0f );
	obsSwatch( sx, ym - half, sx + m.S( 9 ), ym + half, rackColour( seat.player ) );
	return sx + m.S( 16 );
}

//-------------------------------------------------------------------------------------------------
/** The followed player's panel, China's centre panel where the command grid stands: his money in the tab with
	* the painted gold sign, his power poured into the rail's channel, his picture with a star a rank, his name,
	* income, kills and losses and power in the four fact wells, and in the cells' well his army (two rows, most
	* first, each with its count), what he has coming (three, the first with its build sweep) and his two soonest
	* powers with their clocks on readout plates.  Returns its blocker. */
//-------------------------------------------------------------------------------------------------
static IRegion2D drawObserverPanel( const ObsMap &m, const ConsoleSeat &seat, const SuperweaponMap &powerMap )
{
	using namespace DirectStrikeTheme;
	const SpectatorStats &stats = *seat.stats;
	auto at = [ & ]( const ObsRect &r ) { return m.atC( r ); };
	const IRegion2D panel = at( ObsHud_panel() );
	if( const Image *painting = obsPiece( "PlayerWindow" ) )
		TheDisplay->drawImage( painting, panel.lo.x, panel.lo.y, panel.hi.x, panel.hi.y );

	// the money tab: the gold sign at the well's left, the figure after it
	const IRegion2D money = at( ObsHud_panelMoney() );
	const Int mh = money.hi.y - money.lo.y;
	drawText( UnicodeString( u"$" ), font( 20 ), money.lo.x + m.S( 14 ), money.lo.y, mh, TEXT_GOLD, 0 );
	drawText( stats.unlimited ? TheGameText->fetch( "GUI:StartingMoneyUnlimited" ) : broadcastNumber( "", stats.cash ), font( 18 ),
		money.lo.x + m.S( 26 ), money.lo.y, mh, TEXT_WHITE );

	// the power poured into the rail's channel, green while it holds, red when the base is short
	const Int used = max( stats.produced - stats.power, 0 );
	const IRegion2D power = at( ObsHud_panelPower() );
	if( stats.produced > 0 || used > 0 )
	{
		const Real share = stats.produced > 0 ? min( 1.0f, (Real)used / stats.produced ) : 1.0f;
		obsBar( power.lo.x, power.lo.y, power.lo.x + REAL_TO_INT( ( power.hi.x - power.lo.x ) * share ), power.hi.y,
			stats.power < 0 ? TEXT_RED : TEXT_GREEN );
	}

	// his picture in a gold keyline, a star a rank along its foot
	const IRegion2D picture = at( ObsHud_panelPortrait() );
	obsSlot( picture, obsPortrait( seat.player ), FALSE, TEXT_GOLD );
	const Image *star = art( OBS_ICON_STAR );
	const Int stars = max( 0, min( stats.rank, 6 ) );
	for( Int each = 0; each < stars && star != NULL; each++ )
	{
		const Int s = m.S( 14 ), sx = picture.lo.x + m.S( 14 + 18 * each ) - s / 2, sy = picture.hi.y - m.S( 12 ) - s / 2;
		TheDisplay->drawImage( star, sx, sy, sx + s, sy + s, GameMakeColor( 226, 230, 236, 255 ) );
	}

	// the four facts
	GameFont *label = font( OBSHUD_FLOOR ), *figure = font( 16 );
	const IRegion2D name = at( ObsHud_panelFact( 0 ) ), income = at( ObsHud_panelFact( 1 ) ), fight = at( ObsHud_panelFact( 2 ) ),
		powerFact = at( ObsHud_panelFact( 3 ) );
	const Int ix0 = name.lo.x + m.S( 10 ), ix1 = name.hi.x - m.S( 10 );
	auto rowH = []( const IRegion2D &r ) { return r.hi.y - r.lo.y; };
	drawText( UIStyle::fitName( seat.player->getPlayerDisplayName(), font( 18 ), ix1 - ix0 ), font( 18 ), ix0, name.lo.y, rowH( name ), TEXT_GOLD );
	drawText( TheGameText->fetch( "GUI:ObsConStatIncome" ), label, ix0, income.lo.y, rowH( income ), TEXT_GREY );
	UnicodeString incomeText = broadcastNumber( "+$", stats.income );
	incomeText.concat( TheGameText->fetch( "GUI:ObsConPerMinute" ) );
	drawText( incomeText, figure, ix1, income.lo.y, rowH( income ), TEXT_GREEN, 1 );
	UnicodeString n;
	drawText( TheGameText->fetch( "GUI:ObsConStatKills" ), label, ix0, fight.lo.y, rowH( fight ), TEXT_GREY );
	n.format( u"%d", stats.kills );
	drawText( n, figure, ix0 + m.S( 94 ), fight.lo.y, rowH( fight ), TEXT_WHITE, 1 );
	drawText( TheGameText->fetch( "GUI:ObsConLost" ), label, ix0 + m.S( 104 ), fight.lo.y, rowH( fight ), TEXT_GREY );
	n.format( u"%d", stats.losses );
	drawText( n, figure, ix1, fight.lo.y, rowH( fight ), TEXT_WHITE, 1 );
	drawText( TheGameText->fetch( "GUI:ObsConStatPower" ), label, ix0, powerFact.lo.y, rowH( powerFact ), TEXT_GREY );
	n.format( u"%d / %d", used, stats.produced );
	drawText( n, figure, ix1, powerFact.lo.y, rowH( powerFact ), stats.power < 0 ? TEXT_RED : TEXT_WHITE, 1 );

	// the cells' well: the army in two rows, most first; the queue and the powers on the third
	const IRegion2D cells = at( ObsHud_panelCells() );
	auto cellRowLabel = [ & ]( const char *key, Int row )
	{
		const IRegion2D c = at( ObsHud_panelCell( row, 0 ) );
		drawText( obsWord( key, label, c.lo.x - cells.lo.x - m.S( 12 ) ), label, cells.lo.x + m.S( 8 ), c.lo.y, c.hi.y - c.lo.y, TEXT_GREY );
	};
	typedef std::pair< const ThingTemplate *, Int > UnitCount;
	std::vector< UnitCount > units( stats.units.begin(), stats.units.end() );
	std::stable_sort( units.begin(), units.end(), []( const UnitCount &a, const UnitCount &b ) { return a.second > b.second; } );
	cellRowLabel( "GUI:ObsConStatArmy", 0 );
	for( Int each = 0; each < (Int)units.size() && each < 14; each++ )
	{
		const IRegion2D c = at( ObsHud_panelCell( each / 7, each % 7 ) );
		obsSlot( c, units[ each ].first->getButtonImage(), FALSE );
		n.format( u"%d", units[ each ].second );
		drawObserverReadout( n, c, FALSE, TRUE, TEXT_WHITE );
	}
	cellRowLabel( "GUI:ObsConQueue", 2 );
	if( seat.queue != NULL )
		for( Int each = 0; each < seat.queue->count && each < 3; each++ )
		{
			const InGameUI::ProductionListSlot &slot = seat.queue->slot[ each ];
			const IRegion2D c = at( ObsHud_panelCell( 2, each ) );
			obsSlot( c, slot.cameo, FALSE );
			TheDisplay->drawRemainingRectClock( c.lo.x + 1, c.lo.y + 1, c.hi.x - c.lo.x - 2, c.hi.y - c.lo.y - 2, slot.percent, GameMakeColor( 0, 0, 0, 150 ) );
			if( slot.quantity > 1 )
			{
				n.format( u"%d", slot.quantity );
				drawObserverReadout( n, c, FALSE, TRUE, TEXT_WHITE );
			}
		}
	const IRegion2D word = at( ObsHud_panelPowersWord() );
	drawText( obsWord( "GUI:ObsConPowers", label, word.hi.x - word.lo.x ), label, word.lo.x, word.lo.y, word.hi.y - word.lo.y, TEXT_GREY );
	const std::vector< ConsolePower > powers = consolePowers( powerMap );
	for( Int each = 0; each < (Int)powers.size() && each < 2; each++ )
	{
		const ConsolePower &p = powers[ each ];
		const IRegion2D c = at( ObsHud_panelPower( each ) );
		obsSlot( c, p.cameo, FALSE );
		const Int plate = m.S( 19 );
		obsFill( c.lo.x + 1, c.hi.y - plate, c.hi.x - 1, c.hi.y - 2, GameMakeColor( 6, 7, 8, 225 ) );
		UnicodeString clock;
		if( p.ready )
			clock = TheGameText->fetch( "GUI:ObsConReady" );
		else
			clock.format( u"%d:%02d", p.seconds / 60, p.seconds % 60 );
		drawText( clock, label, ( c.lo.x + c.hi.x ) / 2, c.hi.y - plate, plate - 2, p.ready ? TEXT_GREEN : TEXT_WHITE, 0 );
		const ConsoleHit look = { c, CON_LOOK, (Int)p.building };
		theConsoleHits.push_back( look );
	}
	obsHair( cells.lo.x + m.S( 8 ), cells.hi.x - m.S( 8 ), at( ObsHud_panelCell( 2, 0 ) ).lo.y - m.S( 4 ) );
	obsFill( word.lo.x - m.S( 6 ), word.lo.y + m.S( 2 ), word.lo.x - m.S( 6 ) + 1, word.hi.y - m.S( 2 ), OBS_HAIR );
	return at( ObsHud_panelBlock() );
}

//-------------------------------------------------------------------------------------------------
/** The watcher's radar box, the Observer8x6 scheme's painting (ReforgedHudObsCommandBar), drawn before the
	* windows so the radar paints on its glass.  The scheme's own draw stands on a window of the bar that
	* keepConsoleWindows hides with the rest of EA's observer bar. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawObserverConsoleBay( void )
{
	ControlBarSchemeManager *schemes = TheControlBar != NULL ? TheControlBar->getControlBarSchemeManager() : NULL;
	if( schemes == NULL || !schemes->isCurrentSchemePainted() || controlBarTop() >= TheDisplay->getHeight() )
		return;
	ICoord2D offset;
	offset.x = offset.y = 0;
	schemes->drawPainting( offset, FALSE );
}

//-------------------------------------------------------------------------------------------------
/** The roster in the right box's well: a column to 16 seats and two past (the teams split by half), the column
	* heads, a head a team when any team has two seats, a row a seat with its number, team tick, picture, colour,
	* name and army, the best army ruled white, the key to the rule at the foot. */
//-------------------------------------------------------------------------------------------------
static void drawObsRoster( const ObsMap &m, const IRegion2D &well, const std::vector< std::vector< const ConsoleSeat * > > &columns,
	const ObsRosterTier &tier, Int head, const std::vector< ConsoleSeat > &seats, const Player *picked )
{
	using namespace DirectStrikeTheme;
	const Int nc = (Int)columns.size(), gap = m.S( 10 );
	const Int cw = ( well.hi.x - well.lo.x - m.S( 16 ) - gap * ( nc - 1 ) ) / max( nc, 1 );
	const Int best = obsBest( seats, OBSHUD_STAT_ARMY );
	const Int rowH = m.S( tier.row ), headH = m.S( head );
	GameFont *labelFont= font( OBSHUD_FLOOR ), *nameFont = font( tier.nameSize );
	const Int teamSize = max( (Int)OBSHUD_FLOOR, tier.nameSize - 1 );
	GameFont *teamFont = font( teamSize );
	Int teamArmy[ MAX_PLAYER_COUNT ] = { 0 };
	for( size_t each = 0; each < seats.size(); each++ )
		teamArmy[ max( 0, min( seats[ each ].stats->team, (Int)MAX_PLAYER_COUNT - 1 ) ) ] += seats[ each ].stats->army;
	for( Int c = 0; c < nc; c++ )
	{
		const Int cx0 = well.lo.x + m.S( 8 ) + c * ( cw + gap ), cx1 = cx0 + cw, vx1 = cx1 - m.S( 6 );
		// a row's place is the well's top plus its offset at 1080, scaled once, so rounding never adds up down a list
		Int at = 4;
		Int y = well.lo.y + m.S( at );
		drawText( UnicodeString( u"#" ), labelFont,cx0 + m.S( 14 ), y, m.S( 20 ), TEXT_GREY, 0 );
		drawText( TheGameText->fetch( "GUI:ObsConPlayers" ), labelFont,cx0 + m.S( 30 ), y, m.S( 20 ), TEXT_GREY );
		drawText( TheGameText->fetch( "GUI:ObsConStatArmy" ), labelFont,vx1, y, m.S( 20 ), TEXT_GREY, 1 );
		at += 22;
		y = well.lo.y + m.S( at );
		obsHair( cx0, cx1, y, OBS_HAIR_LIT );
		Int lastTeam = -1;
		for( size_t each = 0; each < columns[ c ].size(); each++ )
		{
			const ConsoleSeat &seat = *columns[ c ][ each ];
			const Int t = max( 0, min( seat.stats->team, (Int)MAX_PLAYER_COUNT - 1 ) );
			if( head > 0 && t != lastTeam )
			{
				if( lastTeam >= 0 )
					at += 6;
				y = well.lo.y + m.S( at );
				lastTeam = t;
				obsSwatch( cx0 + m.S( 8 ), y + headH / 2 - m.S( 6 ), cx0 + m.S( 20 ), y + headH / 2 + m.S( 6 ), consoleTeamColour( t ) );
				UnicodeString team;
				team.format( TheGameText->fetch( "GUI:ObsConTeam" ), t + 1 );
				drawSpaced( team, teamFont, cx0 + m.S( 30 ), y, headH, teamSize * 0.16f, TEXT_WHITE );
				drawText( consoleMoney( teamArmy[ t ] ), teamFont, vx1, y, headH, TEXT_GREY, 1 );
				at += head;
				y = well.lo.y + m.S( at );
			}
			const IRegion2D r = ObsMap::box( cx0, y, cx1, y + rowH );
			obsHair( cx0, cx1, y );
			const Bool followed = seat.player == picked;
			const Int nameX = drawObsSeatHead( m, seat, r, seat.seat, 25, followed, consoleHot( r ) );
			const Int room = vx1 - m.S( 70 ) - nameX;
			if( seat.out )
			{
				drawObsStruck( seat.player->getPlayerDisplayName(), nameFont, nameX, y, rowH, room );
				drawText( TheGameText->fetch( "GUI:ObsConOut" ), nameFont, vx1, y, rowH, TEXT_RED, 1 );
			}
			else
			{
				drawText( UIStyle::fitName( seat.player->getPlayerDisplayName(), nameFont, max( room, 1 ) ), nameFont, nameX, y, rowH,
					followed ? TEXT_GOLD : TEXT_WHITE );
				const Int w = drawText( consoleMoney( seat.stats->army ), nameFont, vx1, y, rowH, TEXT_WHITE, 1 );
				if( seat.stats->army == best )
					obsFill( vx1 - w, y + rowH / 2 + m.S( tier.nameSize * 0.55f ), vx1, y + rowH / 2 + m.S( tier.nameSize * 0.55f ) + max( 1, m.S( 2 ) ), TEXT_WHITE );
			}
			const ConsoleHit hit = { r, CON_PICK, seat.index };
			theConsoleHits.push_back( hit );
			at += tier.row;
			y = well.lo.y + m.S( at );
		}
		obsHair( cx0, cx1, y, OBS_HAIR_LIT );
		if( c == nc - 1 )
			drawObsLegend( m, cx0 + m.S( 8 ), y + m.S( 4 ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** The standings in the right box's well: one list ranked by the picked stat, seven columns (the picked one
	* wider, lit and ruled), a break every eight places, the seats that are out at its foot, a column's best ruled
	* white.  A column's word picks its stat. */
//-------------------------------------------------------------------------------------------------
static void drawObsStandings( const ObsMap &m, const IRegion2D &well, const std::vector< ConsoleSeat > &seats, Int stat, const Player *picked )
{
	using namespace DirectStrikeTheme;
	const Int n = (Int)seats.size();
	const Int rowH = m.S( ObsHud_standRow( n ) ), nameSize = ObsHud_standNameSize( n );
	GameFont *labelFont= font( OBSHUD_FLOOR ), *f = font( nameSize );
	const Int lx0 = well.lo.x + m.S( 8 ), lx1 = well.hi.x - m.S( 8 );
	Int colX0[ CON_STATS ], colX1[ CON_STATS ];
	Int x = lx1 - m.S( 4 );
	for( Int k = CON_STATS - 1; k >= 0; k-- )
	{
		colX1[ k ] = x;
		x -= m.S( k == stat ? OBSHUD_STAND_PICKED : OBSHUD_STAND_COL );
		colX0[ k ] = x;
	}
	const Int nameX1 = x - m.S( 10 );
	std::vector< const ConsoleSeat * > ranked;
	for( Int each = 0; each < n; each++ )
		ranked.push_back( &seats[ each ] );
	std::stable_sort( ranked.begin(), ranked.end(), [ stat ]( const ConsoleSeat *a, const ConsoleSeat *b )
		{
			return a->out != b->out ? !a->out : a->value[ stat ] > b->value[ stat ];
		} );
	Int best[ CON_STATS ];
	for( Int k = 0; k < CON_STATS; k++ )
		best[ k ] = k == CON_STATS - 1 ? -1 : obsBest( seats, k );

	// a row's place is the well's top plus its offset at 1080, scaled once, so rounding never adds up down a list
	const Int row1080 = ObsHud_standRow( n );
	Int at = 4;
	Int y = well.lo.y + m.S( at );
	const Int listFoot = well.lo.y + m.S( 4 + 30 + n * row1080 + 4 * ( n > 0 ? ( n - 1 ) / 8 : 0 ) );
	obsFill( colX0[ stat ] - m.S( 2 ), y, colX1[ stat ] + m.S( 2 ), listFoot, GameMakeColor( 255, 255, 255, 12 ) );
	drawText( UnicodeString( u"#" ), labelFont,lx0 + m.S( 13 ), y, m.S( 22 ), TEXT_GREY, 0 );
	drawText( TheGameText->fetch( "GUI:ObsConPlayers" ), labelFont,lx0 + m.S( 34 ), y, m.S( 22 ), TEXT_GREY );
	for( Int k = 0; k < CON_STATS; k++ )
	{
		const UnicodeString word = obsWord( OBS_STAT_WORDS[ k ], labelFont,colX1[ k ] - colX0[ k ] - m.S( 6 ) );
		drawText( word, labelFont,colX1[ k ] - m.S( 4 ), y, m.S( 22 ), k == stat ? TEXT_WHITE : TEXT_GREY, 1 );
		const ConsoleHit hit = { ObsMap::box( colX0[ k ], y, colX1[ k ], y + m.S( 22 ) ), CON_STAT_PICK, k };
		theConsoleHits.push_back( hit );
	}
	obsHair( colX0[ stat ] + m.S( 6 ), colX1[ stat ] - m.S( 4 ), y + m.S( 21 ), TEXT_WHITE );
	at += 26;
	y = well.lo.y + m.S( at );
	obsHair( lx0, lx1, y, OBS_HAIR_LIT );
	for( Int place = 1; place <= n; place++ )
	{
		const ConsoleSeat &seat = *ranked[ place - 1 ];
		if( place > 1 && ( place - 1 ) % 8 == 0 )
		{
			at += 4;
			y = well.lo.y + m.S( at );
			obsHair( lx0, lx1, y - m.S( 2 ), OBS_HAIR_LIT );
		}
		else if( place > 1 )
			obsHair( lx0, lx1, y );
		const IRegion2D r = ObsMap::box( lx0, y, lx1, y + rowH );
		const Bool followed = seat.player == picked;
		const Int nameX = drawObsSeatHead( m, seat, r, place, 28, followed, consoleHot( r ) );
		if( seat.out )
		{
			drawObsStruck( seat.label, f, nameX, y, rowH, nameX1 - nameX );
			drawText( TheGameText->fetch( "GUI:ObsConOut" ), f, colX1[ 0 ] - m.S( 4 ), y, rowH, TEXT_RED, 1 );
		}
		else
		{
			drawText( UIStyle::fitName( seat.label, f, max( nameX1 - nameX, 1 ) ), f, nameX, y, rowH, followed ? TEXT_GOLD : TEXT_WHITE );
			for( Int k = 0; k < CON_STATS; k++ )
			{
				const Bool ready = k == CON_STATS - 1 && seat.superweaponReady;
				const Color ink = ready ? TEXT_GREEN : ( k == 4 && seat.value[ k ] < 0 ) ? TEXT_RED : k == stat ? TEXT_WHITE : TEXT_GREY;
				const Int w = drawText( consoleValueText( k, seat ), f, colX1[ k ] - m.S( 4 ), y, rowH, ink, 1 );
				if( best[ k ] >= 0 && seat.value[ k ] == best[ k ] )
				{
					const Int uy = y + rowH / 2 + m.S( nameSize * 0.55f );
					obsFill( colX1[ k ] - m.S( 4 ) - w, uy, colX1[ k ] - m.S( 4 ), uy + max( 1, m.S( 2 ) ), TEXT_WHITE );
				}
			}
		}
		const ConsoleHit hit = { r, CON_PICK, seat.index };
		theConsoleHits.push_back( hit );
		at += row1080;
		y = well.lo.y + m.S( at );
	}
	obsHair( lx0, lx1, y, OBS_HAIR_LIT );
	drawObsLegend( m, lx0 + m.S( 8 ), y + m.S( 4 ) );
}

//-------------------------------------------------------------------------------------------------
/** The watcher's HUD, over the windows and the world's bars: the five keys in the radar box (its painting is
	* the scheme's, drawn with the bar), the right box with the roster or the standings, the followed player's
	* panel, an open dropdown's list over all of it and the hovered key's tooltip on the radar's ledge.  See the
	* section's head.  The right box steps away while the Tab screen is up: that lists every seat with more. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawObserverConsole( void )
{
	forgetConsole();
	// the HUD stands where the bar stands, so not while the bar is hidden (hidehud, a script)
	if( controlBarTop() >= TheDisplay->getHeight() )
		return;
	// nor while a menu is over the match: drawn after the windows (drawHudOverlay), it covered the Esc menu, the
	// options' DEFAULTS and CANCEL, and any box.  Forgotten above, so its keys and rows take no click and its
	// hotkeys wait until the menu goes (TODO Overlays and Seat Pips 5)
	WindowLayout *optionsOver = TheShell != NULL ? TheShell->getOptionsLayout( FALSE ) : NULL;
	if( isQuitMenuVisible() || TheWindowManager->winGetModalWindow() != NULL || ( optionsOver != NULL && !optionsOver->isHidden() ) )
		return;
	theConsoleShown = TRUE;
	ObsMap m = obsMap();
	keepConsoleWindows( m.atL( ObsHud_radarGlass() ) );
	using namespace DirectStrikeTheme;
	begin( SIDE_COMMON, m.k );

	std::vector< ConsoleSeat > seats;
	gatherConsoleSeats( seats, m_superweapons );
	const Int n = (Int)seats.size();
	const Int stat = max( 0, min( m_rackStat, (Int)CON_STATS - 1 ) );
	const Player *picked = observerPickedPlayer();
	const ConsoleSeat *pickedSeat = NULL;
	for( size_t each = 0; each < seats.size(); each++ )
		if( seats[ each ].player == picked )
			pickedSeat = &seats[ each ];
	const Bool tabOpen = theTabDrawnPass != 0 && theTabDrawnPass + 1 >= m_hudCellPass;

	// the teams, and whether any holds two seats (a head each in the roster, a team's share in the channel)
	Int teamSize[ MAX_PLAYER_COUNT ] = { 0 }, teamArmy[ MAX_PLAYER_COUNT ] = { 0 };
	Int teamCount = 0;
	Bool teamed = FALSE;
	for( size_t each = 0; each < seats.size(); each++ )
	{
		const Int t = max( 0, min( seats[ each ].stats->team, (Int)MAX_PLAYER_COUNT - 1 ) );
		teamCount = max( teamCount, t + 1 );
		teamSize[ t ]++;
		teamArmy[ t ] += seats[ each ].stats->army;
		teamed = teamed || teamSize[ t ] >= 2;
	}

	// the radar box's steel is the HUD's, and its five keys
	theConsoleBlock[ 0 ] = m.atL( ObsHud_radarBox() );
	static const char *const KEY_PIECES[ OBSHUD_KEYS ] = { "Standings", "Vision", "Chat", "Options", "Back" };
	static const Int KEY_ACTION[ OBSHUD_KEYS ] = { CON_STANDINGS, CON_VISION_CYCLE, CON_CHAT, CON_OPTIONS, CON_QUIT };
	WindowLayout *options = TheShell != NULL ? TheShell->getOptionsLayout( FALSE ) : NULL;
	const Bool lit[ OBSHUD_KEYS ] = { theStandingsOpen, FALSE, IsInGameChatActive(), options != NULL && !options->isHidden(), isQuitMenuVisible() };
	// chat is dimmed where there is nobody to talk to (a skirmish watched alone, a replay)
	const Bool usable[ OBSHUD_KEYS ] = { TRUE, TRUE, IsInGameChatAvailable(), TRUE, TRUE };
	Int tip = -1;
	for( Int each = 0; each < OBSHUD_KEYS; each++ )
		if( consoleKey( KEY_PIECES[ each ], m.atL( ObsHud_key( each ) ), lit[ each ], KEY_ACTION[ each ], 0, usable[ each ] ) )
			tip = each;

	// the right box, as tall as its list
	Int rightX0 = 1920;
	IRegion2D dropAt[ 2 ];
	Bool boxShown = FALSE;
	if( !tabOpen && !theConsoleHidden[ CON_MODULE_BOARD ] && n > 0 )
	{
		boxShown = TRUE;
		ObsRosterTier tier = ObsHud_rosterTier( n );
		const Int head = teamed ? tier.head : 0;
		std::vector< std::vector< const ConsoleSeat * > > columns( tier.columns );
		Int x0 = 0, body = 0;
		if( theStandingsOpen )
		{
			// as wide as the longest name needs, at 1080, measured when the standings are gathered again (twice a
			// second) and before anything is drawn: the measure takes the theme's strings, which begin gives back
			static UnsignedInt measuredAt = 0xFFFFFFFF;
			static Int nameW1080 = 0;
			if( measuredAt != theRackStatsFrame )
			{
				Int nameW = 0;
				GameFont *f = font( ObsHud_standNameSize( n ) );
				for( Int each = 0; each < n; each++ )
					nameW = max( nameW, textWidth( seats[ each ].label, f ) );
				begin( SIDE_COMMON, m.k );
				nameW1080 = REAL_TO_INT( nameW / m.k ) + 12;
				measuredAt = theRackStatsFrame;
			}
			x0 = ObsHud_standX0( n, nameW1080 );
			body = ObsHud_standBody( n );
		}
		else
		{
			// past 16 seats two columns, the teams split by half
			std::vector< Int > teams;
			for( Int each = 0; each < n; each++ )
				if( teams.empty() || teams.back() != seats[ each ].stats->team )
					teams.push_back( seats[ each ].stats->team );
			const Int half = ( (Int)teams.size() + 1 ) / 2;
			Int teamsIn = 0, rowsIn = 0;
			for( Int each = 0; each < n; each++ )
			{
				Int at = 0;
				for( Int t = 0; t < (Int)teams.size(); t++ )
					if( teams[ t ] == seats[ each ].stats->team )
						at = t;
				columns[ tier.columns == 2 && at >= half ? 1 : 0 ].push_back( &seats[ each ] );
			}
			for( size_t c = 0; c < columns.size(); c++ )
			{
				Int distinct = 0, last = -1;
				for( size_t each = 0; each < columns[ c ].size(); each++ )
					if( columns[ c ][ each ]->stats->team != last || each == 0 )
					{
						distinct++;
						last = columns[ c ][ each ]->stats->team;
					}
				teamsIn = max( teamsIn, distinct );
				rowsIn = max( rowsIn, (Int)columns[ c ].size() );
			}
			x0 = tier.columns == 2 ? OBSHUD_ROSTER_X2 : OBSHUD_ROSTER_X1;
			body = ObsHud_rosterBody( teamsIn, rowsIn, tier.row, head );
		}
		const ObsRightBox rb = ObsHud_rightBox( x0, body );
		rightX0 = x0;
		drawObsBox( m, rb );
		theConsoleBlock[ 1 ] = m.atR( rb.block );

		// the crown: the clock in the mount's well, the teams' (or the players') shares of the armies poured into
		// the channel with a mark at its middle, the camera's and the stat's dropdowns
		const IRegion2D clock = m.atR( rb.clock );
		UnicodeString clockText;
		clockText.translate( AsciiString( spectatorClock( TheGameLogic->getFrame() ).c_str() ) );
		drawText( clockText, font( 24 ), ( clock.lo.x + clock.hi.x ) / 2, clock.lo.y, clock.hi.y - clock.lo.y, TEXT_WHITE, 0 );
		const IRegion2D channel = m.atR( rb.channel );
		std::vector< std::pair< Int, Color > > shares;
		if( teamed )
		{
			for( Int t = 0; t < teamCount; t++ )
				if( teamSize[ t ] > 0 )
					shares.push_back( std::make_pair( teamArmy[ t ], consoleTeamColour( t ) ) );
		}
		else
			for( Int each = 0; each < n; each++ )
				if( !seats[ each ].out )
					shares.push_back( std::make_pair( seats[ each ].stats->army, rackColour( seats[ each ].player ) ) );
		Int total = 0;
		for( size_t each = 0; each < shares.size(); each++ )
			total += max( shares[ each ].first, 0 );
		Int at = channel.lo.x;
		for( size_t each = 0; each < shares.size() && total > 0; each++ )
		{
			const Int end = each + 1 == shares.size() ? channel.hi.x : at + ( channel.hi.x - channel.lo.x ) * max( shares[ each ].first, 0 ) / total;
			obsBar( at, channel.lo.y, end, channel.hi.y, shares[ each ].second );
			at = end;
		}
		const Int mid = ( channel.lo.x + channel.hi.x ) / 2;
		obsFill( mid - max( 1, m.S( 1 ) ), channel.lo.y - m.S( 4 ), mid + max( 1, m.S( 1 ) ), channel.hi.y + m.S( 3 ), TEXT_WHITE );
		const ObserverCameraMode mode = TheObserverCamera.getMode();
		const Int camera = mode == OBSERVER_CAMERA_FREE ? 0 : mode == OBSERVER_CAMERA_PLAYER ? 1 : 2;
		const char *const dropWords[ 2 ] = { OBS_CAMERA_WORDS[ camera ], OBS_STAT_WORDS[ stat ] };
		GameFont *dropFont = font( 15 );
		for( Int d = 0; d < 2; d++ )
		{
			const IRegion2D r = m.atR( rb.drop[ d ] );
			dropAt[ d ] = r;
			const Bool hot = consoleHot( r );
			if( const Image *face = obsPiece( "Drop", theDropOpen == d ? "P" : hot ? "H" : "" ) )
				TheDisplay->drawImage( face, r.lo.x, r.lo.y, r.hi.x, r.hi.y );
			drawText( obsWord( dropWords[ d ], dropFont, r.hi.x - r.lo.x - m.S( 38 ) ), dropFont, r.lo.x + m.S( 12 ), r.lo.y, r.hi.y - r.lo.y,
				hot || theDropOpen == d ? TEXT_WHITE : OBS_PAPER );
			obsCaret( r.hi.x - m.S( 15 ), ( r.lo.y + r.hi.y ) / 2, max( 2, m.S( 5 ) ), hot || theDropOpen == d ? TEXT_WHITE : OBS_PAPER );
			const ConsoleHit hit = { r, CON_DROP, d };
			theConsoleHits.push_back( hit );
		}

		if( theStandingsOpen )
			drawObsStandings( m, m.atR( rb.well ), seats, stat, picked );
		else
			drawObsRoster( m, m.atR( rb.well ), columns, tier, head, seats, picked );
	}

	// the followed player's panel, beside the roster; the standings show his row and leave the middle clear
	if( pickedSeat != NULL && !theStandingsOpen && !theConsoleHidden[ CON_MODULE_DOSSIER ] )
	{
		m.shift = boxShown ? ObsHud_panelShift( rightX0 ) : 0;
		theConsoleBlock[ 2 ] = drawObserverPanel( m, *pickedSeat, m_superweapons[ pickedSeat->index ] );
		m.shift = 0;
	}

	// an open dropdown's list over everything, the current entry lit, the one under the pointer brighter
	if( boxShown && theDropOpen >= 0 && theDropOpen < 2 )
	{
		const IRegion2D r = dropAt[ theDropOpen ];
		const Int h = r.hi.y - r.lo.y;
		const Int entries = theDropOpen == 0 ? 3 : CON_STATS;
		const ObserverCameraMode mode = TheObserverCamera.getMode();
		const Int current = theDropOpen == 0 ? ( mode == OBSERVER_CAMERA_FREE ? 0 : mode == OBSERVER_CAMERA_PLAYER ? 1 : 2 ) : stat;
		GameFont *dropFont = font( 15 );
		for( Int e = 0; e < entries; e++ )
		{
			const IRegion2D er = ObsMap::box( r.lo.x, r.hi.y + e * h, r.hi.x, r.hi.y + ( e + 1 ) * h );
			const Bool hot = consoleHot( er );
			if( const Image *face = obsPiece( "Drop", e == current ? "P" : hot ? "H" : "" ) )
				TheDisplay->drawImage( face, er.lo.x, er.lo.y, er.hi.x, er.hi.y );
			const char *key = theDropOpen == 0 ? OBS_CAMERA_WORDS[ e ] : OBS_STAT_WORDS[ e ];
			drawText( obsWord( key, dropFont, er.hi.x - er.lo.x - m.S( 24 ) ), dropFont, er.lo.x + m.S( 12 ), er.lo.y, h,
				e == current || hot ? TEXT_WHITE : OBS_PAPER );
			const ConsoleHit hit = { er, theDropOpen == 0 ? CON_CAMERA : CON_STAT_PICK, e };
			theConsoleHits.push_back( hit );
		}
		theConsoleBlock[ 3 ] = ObsMap::box( r.lo.x, r.hi.y, r.hi.x, r.hi.y + entries * h );
	}

	// the key under the pointer: its name and hotkey, and one line of what it does, on the radar's ledge
	if( tip >= 0 )
	{
		static const char *const TIP_NAME[ OBSHUD_KEYS ] = { "GUI:ObsConTipStandings", NULL, "GUI:ObsConTipChat", "GUI:ObsConTipOptions", "GUI:ObsConTipQuit" };
		static const char *const TIP_LINE[ OBSHUD_KEYS ] = { "GUI:ObsConTipStandingsLine", NULL, "GUI:ObsConTipChatLine", "GUI:ObsConTipOptionsLine",
			"GUI:ObsConTipQuitLine" };
		static const char *const VISION_NAME[ 3 ] = { "GUI:ObsConTipVisionAll", "GUI:ObsConTipVisionTeam", "GUI:ObsConTipVisionPlayer" };
		static const char *const VISION_LINE[ 3 ] = { "GUI:ObsConTipVisionAllLine", "GUI:ObsConTipVisionTeamLine", "GUI:ObsConTipVisionPlayerLine" };
		const Int vision = TheObserverCamera.isFogOn() ? ( theConsoleVision == 1 ? 1 : 2 ) : 0;
		const UnicodeString name = TheGameText->fetch( tip == OBSHUD_KEY_VISION ? VISION_NAME[ vision ] : TIP_NAME[ tip ] );
		const UnicodeString line = TheGameText->fetch( tip == OBSHUD_KEY_VISION ? VISION_LINE[ vision ] : TIP_LINE[ tip ] );
		UnicodeString chord;
		if( tip == OBSHUD_KEY_STANDINGS )
			chord = MetaMap_chordLabel( GameMessage::MSG_META_OBSERVER_TOGGLE_STANDINGS );
		else if( tip == OBSHUD_KEY_VISION )
			chord = MetaMap_chordLabel( GameMessage::MSG_META_OBSERVER_VISION_CYCLE );
		GameFont *nameFont = font( 16 ), *labelFont= font( OBSHUD_FLOOR );
		const Int nameW = textWidth( name, nameFont ), chordW = textWidth( chord, labelFont ), lineW = textWidth( line, labelFont );
		const Int need = max( REAL_TO_INT( ( nameW + chordW ) / m.k ) + 56, REAL_TO_INT( lineW / m.k ) + 40 ) + 8;
		const IRegion2D card = m.atL( ObsHud_tip( need ) );
		const ControlBarTooltipImages &pieces = TheControlBar->getControlBarSchemeManager()->getTooltipImages();
		if( !ControlBarDrawNinePieceBox( pieces.card, card, m.k ) )
			obsFill( card.lo.x, card.lo.y, card.hi.x, card.hi.y, DirectStrikeTheme::PLATE );
		const Int headY = card.lo.y + m.S( 6 ), headH = m.S( 26 );
		drawText( name, nameFont, card.lo.x + m.S( 18 ), headY, headH, TEXT_WHITE );
		if( !chord.isEmpty() )
			drawText( chord, labelFont,card.hi.x - m.S( 20 ), headY, headH, TEXT_GREY, 1 );
		drawText( line, labelFont,card.lo.x + m.S( 18 ), card.lo.y + m.S( 44 ), m.S( 30 ), TEXT_GREY );
	}
}

//-------------------------------------------------------------------------------------------------
/** The watcher's Tab screen over its painting (Diplomacy.cpp draws the painting and calls this with the
	* window's rect, 1379x714 at 1080): the teams' titles, a column a team in the table (four columns of eight
	* at most, past which the seats run on down four columns), a row a seat with his picture, colour, name,
	* money, kills and, for another human, the mute key, and the match's facts at the foot.  The right box
	* steps away while this is up. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawObserverTab( const IRegion2D &panel )
{
	theTabDrawnPass = m_hudCellPass;
	theTabMutes.clear();
	if( !localPlayerWatching() )
		return;
	using namespace DirectStrikeTheme;
	const Real k = (Real)( panel.hi.x - panel.lo.x ) / 1379.0f;
	begin( SIDE_COMMON, k );
	auto X = [ & ]( Int x ) { return panel.lo.x + REAL_TO_INT_FLOOR( x * k + 0.5f ); };
	auto Y = [ & ]( Int y ) { return panel.lo.y + REAL_TO_INT_FLOOR( y * k + 0.5f ); };
	auto S = [ & ]( Real v ) { return REAL_TO_INT_FLOOR( v * k + 0.5f ); };
	auto at = [ & ]( const ObsRect &r ) { return ObsMap::box( X( r.x0 ), Y( r.y0 ), X( r.x1 ), Y( r.y1 ) ); };

	std::vector< ConsoleSeat > seats;
	gatherConsoleSeats( seats, m_superweapons );
	const Int n = (Int)seats.size();
	std::vector< Int > teams;
	for( Int each = 0; each < n; each++ )
		if( teams.empty() || teams.back() != seats[ each ].stats->team )
			teams.push_back( seats[ each ].stats->team );
	Int needed = 0, tallest = 0;
	for( size_t t = 0; t < teams.size(); t++ )
	{
		Int size = 0;
		for( Int each = 0; each < n; each++ )
			size += seats[ each ].stats->team == teams[ t ] ? 1 : 0;
		needed += ( size + OBSHUD_TAB_ROWS - 1 ) / OBSHUD_TAB_ROWS;
		tallest = max( tallest, min( size, (Int)OBSHUD_TAB_ROWS ) );
	}
	// a team a column (or two, past eight seats) while that fits; else the seats run on down four columns
	const Bool byTeam = needed <= OBSHUD_TAB_COLUMNS;
	const Int columns = byTeam ? max( needed, 1 ) : OBSHUD_TAB_COLUMNS;
	const Int rows = byTeam ? max( tallest, 1 ) : ( n + OBSHUD_TAB_COLUMNS - 1 ) / OBSHUD_TAB_COLUMNS;
	std::vector< std::vector< const ConsoleSeat * > > cells( columns );
	std::vector< Int > columnTeam( columns, -1 );
	{
		Int column = 0;
		for( size_t t = 0; t < teams.size() && byTeam; t++ )
		{
			Int inColumn = 0;
			columnTeam[ column ] = teams[ t ];
			for( Int each = 0; each < n; each++ )
			{
				if( seats[ each ].stats->team != teams[ t ] )
					continue;
				if( inColumn == OBSHUD_TAB_ROWS )
				{
					column++;
					columnTeam[ column ] = teams[ t ];
					inColumn = 0;
				}
				cells[ column ].push_back( &seats[ each ] );
				inColumn++;
			}
			column++;
		}
		for( Int each = 0; each < n && !byTeam; each++ )
			cells[ min( each / max( rows, 1 ), columns - 1 ) ].push_back( &seats[ each ] );
	}

	const ObsRect titles = ObsHud_tabTitles(), table = ObsHud_tabTable();
	GameFont *nameFont = font( 16 ), *labelFont= font( OBSHUD_FLOOR ), *teamFont = font( 17 ), *sumFont = font( 15 );
	const UnsignedInt frame = TheGameLogic->getFrame();
	for( Int c = 0; c < columns; c++ )
	{
		const IRegion2D head = at( ObsHud_tabColumn( c, columns, titles ) );
		const IRegion2D col = at( ObsHud_tabColumn( c, columns, table ) );
		if( c > 0 )
		{
			obsFill( head.lo.x - S( 4 ), head.lo.y + S( 6 ), head.lo.x - S( 4 ) + 1, head.hi.y - S( 6 ), OBS_HAIR );
			obsFill( col.lo.x - S( 4 ), col.lo.y + S( 6 ), col.lo.x - S( 4 ) + 1, col.hi.y - S( 6 ), OBS_HAIR );
		}
		const Int hh = head.hi.y - head.lo.y;
		if( byTeam )
		{
			const Int t = columnTeam[ c ];
			obsSwatch( head.lo.x + S( 8 ), head.lo.y + hh / 2 - S( 6 ), head.lo.x + S( 20 ), head.lo.y + hh / 2 + S( 6 ), consoleTeamColour( t ) );
			UnicodeString team;
			team.format( TheGameText->fetch( "GUI:ObsConTeam" ), t + 1 );
			drawSpaced( team, teamFont, head.lo.x + S( 30 ), head.lo.y, hh, 2.6f, TEXT_WHITE );
			Int money = 0;
			for( Int each = 0; each < n; each++ )
				money += seats[ each ].stats->team == t ? seats[ each ].value[ 0 ] : 0;
			drawText( consoleMoney( money ), sumFont, head.hi.x - S( 8 ), head.lo.y, hh, TEXT_GREY, 1 );
		}
		else if( c == 0 )
			drawText( TheGameText->fetch( "GUI:ObsConPlayers" ), teamFont, head.lo.x + S( 8 ), head.lo.y, hh, TEXT_WHITE );

		for( size_t j = 0; j < cells[ c ].size(); j++ )
		{
			const ConsoleSeat &seat = *cells[ c ][ j ];
			const ObsRect rowRect = ObsHud_tabRow( ObsHud_tabColumn( c, columns, table ), (Int)j, rows );
			const IRegion2D r = at( rowRect );
			if( j % 2 )
				obsFill( r.lo.x - S( 4 ), r.lo.y, r.hi.x + S( 4 ), r.hi.y, GameMakeColor( 255, 255, 255, 10 ) );
			else if( j > 0 )
				obsHair( r.lo.x, r.hi.x, r.lo.y );
			const Int ps = r.hi.y - r.lo.y - S( 14 );
			obsSlot( ObsMap::box( r.lo.x + S( 4 ), r.lo.y + S( 7 ), r.lo.x + S( 4 ) + ps, r.lo.y + S( 7 ) + ps ), obsPortrait( seat.player ), seat.out );
			const Int sx = r.lo.x + S( 4 ) + ps + S( 10 ), ym = ( r.lo.y + r.hi.y ) / 2;
			obsSwatch( sx, ym - S( 18 ), sx + S( 10 ), ym - S( 4 ), rackColour( seat.player ) );
			// another human still in the game: his mute key, on this machine's chat alone
			Int slot = seat.seat - 1;
			const GameSlot *gameSlot = TheGameInfo != NULL && slot >= 0 && slot < MAX_SLOTS ? TheGameInfo->getConstSlot( slot ) : NULL;
			const Bool mutable_ = gameSlot != NULL && gameSlot->isHuman() && slot != TheGameInfo->getLocalSlotNum()
				&& ( TheNetwork == NULL || TheNetwork->isPlayerConnected( slot ) );
			const Int end = r.hi.x - S( mutable_ ? 48 : 8 );
			// the name and the line under it stand round the row's middle, whatever the row's height
			const Int nameY = ym - S( 30 ), nameH = S( 30 );
			if( seat.out )
			{
				drawObsStruck( seat.label, nameFont, sx + S( 18 ), nameY, nameH, end - sx - S( 18 ) );
				drawText( TheGameText->fetch( "GUI:ObsConOut" ), labelFont,sx, ym + S( 2 ), S( 26 ), TEXT_RED );
			}
			else
			{
				drawText( UIStyle::fitName( seat.label, nameFont, max( end - sx - S( 18 ), 1 ) ), nameFont, sx + S( 18 ), nameY, nameH, TEXT_WHITE );
				const Int lineY = ym + S( 2 ), lineH = S( 26 );
				drawText( seat.stats->unlimited ? TheGameText->fetch( "GUI:StartingMoneyUnlimited" ) : consoleMoney( seat.value[ 0 ] ), labelFont,sx, lineY, lineH, TEXT_WHITE );
				drawText( TheGameText->fetch( "GUI:ObsConStatKills" ), labelFont,sx + S( 78 ), lineY, lineH, TEXT_GREY );
				UnicodeString kills;
				kills.format( u"%d", seat.stats->kills );
				drawText( kills, labelFont,sx + S( 138 ), lineY, lineH, TEXT_WHITE );
			}
			if( mutable_ )
			{
				const IRegion2D key = at( ObsHud_tabMute( rowRect ) );
				const Bool hot = consoleHot( key );
				const Bool held = hot && TheMouse->getMouseStatus()->leftState != MBS_Up;
				AsciiString piece;
				piece.format( "%s%s", gameSlot->isMuted() ? "Unmute" : "Mute", held ? "P" : hot ? "H" : "" );
				if( const Image *image = TheControlBar->getControlBarSchemeManager()->getTabImage( piece.str() ) )
					TheDisplay->drawImage( image, key.lo.x, key.lo.y, key.hi.x, key.hi.y );
				const ObsTabMute mute = { key, slot };
				theTabMutes.push_back( mute );
			}
		}
	}

	// the foot: what a watcher wants of the match that the HUD shows nowhere else but the clock
	const IRegion2D facts = at( ObsHud_tabFacts() );
	UnicodeString values[ 4 ];
	const MapMetaData *map = TheMapCache != NULL ? TheMapCache->findMap( TheGlobalData->m_mapName ) : NULL;
	if( map != NULL )
		values[ 0 ] = map->m_displayName;
	else
	{
		AsciiString file = TheGlobalData->m_mapName;
		const char *slash = strrchr( file.str(), '\\' );
		values[ 0 ].translate( AsciiString( slash != NULL ? slash + 1 : file.str() ) );
	}
	values[ 1 ].translate( AsciiString( spectatorClock( frame ).c_str() ) );
	const Real pace = (Real)TheGameEngine->getFramesPerSecondLimit() / LOGICFRAMES_PER_SECOND * GAME_SPEED_PERCENT / 100.0f;
	const Int hundredths = REAL_TO_INT( pace * 100.0f + 0.5f );
	if( hundredths % 10 == 0 )
		values[ 2 ].format( u"%d.%dx", hundredths / 100, hundredths / 10 % 10 );
	else
		values[ 2 ].format( u"%d.%02dx", hundredths / 100, hundredths % 100 );
	Int watchers = 0;
	for( Int slot = 0; slot < MAX_SLOTS && TheGameInfo != NULL; slot++ )
	{
		const GameSlot *s = TheGameInfo->getConstSlot( slot );
		watchers += s != NULL && s->isOccupied() && s->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER ? 1 : 0;
	}
	values[ 3 ].format( u"%d", max( watchers, 1 ) );
	static const char *const FACT_WORDS[ 4 ] = { "GUI:ObsConMap", "GUI:ObsConLength", "GUI:ObsConSpeed", "GUI:ObsConObservers" };
	const Int fw = ( facts.hi.x - facts.lo.x - S( 16 ) ) / 4, fh = facts.hi.y - facts.lo.y;
	for( Int each = 0; each < 4; each++ )
	{
		const Int fx = facts.lo.x + S( 8 ) + each * fw;
		if( each > 0 )
			obsFill( fx - S( 4 ), facts.lo.y + S( 10 ), fx - S( 4 ) + 1, facts.hi.y - S( 10 ), OBS_HAIR );
		const Int lw = drawText( TheGameText->fetch( FACT_WORDS[ each ] ), labelFont,fx + S( 10 ), facts.lo.y, fh, TEXT_GREY );
		drawText( UIStyle::fitName( values[ each ], nameFont, max( fw - lw - S( 28 ), 1 ) ), nameFont, fx + S( 18 ) + lw, facts.lo.y, fh, TEXT_WHITE );
	}
}

//-------------------------------------------------------------------------------------------------
/** A click on the Tab screen at x, y: a mute key mutes or hears that human again, on this machine's chat alone,
	* as Diplomacy.wnd's mute buttons do.  TRUE when a key took it. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::clickObserverTab( Int x, Int y )
{
	for( size_t each = 0; each < theTabMutes.size(); each++ )
	{
		const IRegion2D &r = theTabMutes[ each ].box;
		if( x < r.lo.x || x >= r.hi.x || y < r.lo.y || y >= r.hi.y || TheGameInfo == NULL )
			continue;
		if( GameSlot *slot = TheGameInfo->getSlot( theTabMutes[ each ].slot ) )
			slot->mute( !slot->isMuted() );
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** What a press on the observer HUD or one of its hotkeys does.  The camera and the fog go through the
	* spectator actions the control socket also drives, so a script and a click do the same thing. */
//-------------------------------------------------------------------------------------------------
void InGameUI::runConsoleAction( Int action, Int arg )
{
	const Player *picked = observerPickedPlayer();
	auto follow = [ this ]( Int index )
	{
		AsciiString name;
		if( index < 0 )
			name.format( "%s%s", FOLLOW_ACTION.c_str(), FOLLOW_NOBODY.c_str() );
		else
			name.format( "%s%d", FOLLOW_ACTION.c_str(), index );
		runSpectatorAction( name.str() );
	};
	// any press but a dropdown's own closes its list
	if( action != CON_DROP )
		theDropOpen = -1;
	switch( action )
	{
		case CON_PICK:
			follow( picked != NULL && picked->getPlayerIndex() == arg ? -1 : arg );
			break;
		case CON_STANDINGS:
			theStandingsOpen = !theStandingsOpen;
			break;
		case CON_VISION_CYCLE:
		{
			// all, the team, the seat, round again.  The fog drawn is the followed seat's (ObserverCamera), so a
			// press with nobody followed follows the seat picked on the bar, or the top of the standings, first,
			// as the camera's follow key does; it used to stay on all and change nothing
			Int seat = TheObserverCamera.getFollowedPlayerIndex();
			if( seat == ObserverCamera::NO_PLAYER && picked != NULL )
				seat = picked->getPlayerIndex();
			if( seat == ObserverCamera::NO_PLAYER )
				seat = topActiveSeat( m_rackStat );
			if( seat == ObserverCamera::NO_PLAYER )
				break;
			if( seat != TheObserverCamera.getFollowedPlayerIndex() )
				follow( seat );
			runConsoleAction( CON_VISION, ( theConsoleVision + 1 ) % 3 );
			break;
		}
		case CON_DROP:
			theDropOpen = theDropOpen == arg ? -1 : arg;
			break;
		case CON_STAT_PICK:
			m_rackStat = max( 0, min( arg, (Int)CON_STATS - 1 ) );
			break;
		case CON_STAT_STEP:
			m_rackStat = ( m_rackStat + ( arg > 0 ? 1 : CON_STATS - 1 ) ) % CON_STATS;
			break;
		case CON_SEAT_STEP:
		{
			// the seat after or before the picked one, round the board's order
			Int at = arg > 0 ? -1 : 0;
			for( Int i = 0; i < theConsoleOrderCount; i++ )
				if( picked != NULL && theConsoleOrder[ i ] == picked->getPlayerIndex() )
					at = i;
			if( theConsoleOrderCount > 0 )
				follow( theConsoleOrder[ ( at + ( arg > 0 ? 1 : theConsoleOrderCount - 1 ) + theConsoleOrderCount ) % theConsoleOrderCount ] );
			break;
		}
		case CON_CAMERA:
			// follow takes the picked seat, or with nobody picked the top of the standings (runSpectatorAction)
			runSpectatorAction( CAMERA_ACTION + ( arg == 0 ? CAMERA_FREE : arg == 1 ? CAMERA_PLAYER : CAMERA_DIRECTOR ) );
			break;
		case CON_VISION:
			// a seat's sight wants a seat; with nobody picked the picture stays whole
			theConsoleVision = picked != NULL ? arg : 0;
			if( ( theConsoleVision != 0 ) != TheObserverCamera.isFogOn() )
				runSpectatorAction( FOG_ACTION );
			break;
		case CON_HIDE:
			if( arg >= 0 && arg < CON_MODULES )
				theConsoleHidden[ arg ] = !theConsoleHidden[ arg ];
			break;
		case CON_HIDE_ALL:
		{
			const Bool hide = !( theConsoleHidden[ CON_MODULE_RADAR ] && theConsoleHidden[ CON_MODULE_BOARD ] && theConsoleHidden[ CON_MODULE_DOSSIER ] );
			for( Int each = 0; each < CON_MODULES; each++ )
				theConsoleHidden[ each ] = hide;
			break;
		}
		case CON_OPTIONS:
			if( WindowLayout *options = TheShell->getOptionsLayout( TRUE ) )
			{
				options->runInit();
				options->hide( FALSE );
				options->bringForward();
			}
			break;
		case CON_CHAT:
			ToggleInGameChat();
			break;
		case CON_QUIT:
			ToggleQuitMenu();
			break;
		case CON_LOOK:
			if( Object *thing = TheGameLogic->findObjectByID( (ObjectID)arg ) )
				TheTacticalView->lookAt( thing->getPosition() );
			break;
	}
}

/// the observer HUD's CommandMap commands, each the press of one of its keys; nobody but a watcher's
Bool InGameUI::runObserverCommand( Int meta )
{
	if( !localPlayerWatching() )
		return FALSE;
	struct Command { Int meta; Int action; Int arg; };
	static const Command COMMANDS[] =
	{
		{ GameMessage::MSG_META_OBSERVER_CAMERA_FREE, CON_CAMERA, 0 },
		{ GameMessage::MSG_META_OBSERVER_CAMERA_FOLLOW, CON_CAMERA, 1 },
		{ GameMessage::MSG_META_OBSERVER_CAMERA_DIRECTOR, CON_CAMERA, 2 },
		{ GameMessage::MSG_META_OBSERVER_VISION_ALL, CON_VISION, 0 },
		{ GameMessage::MSG_META_OBSERVER_VISION_TEAM, CON_VISION, 1 },
		{ GameMessage::MSG_META_OBSERVER_VISION_PLAYER, CON_VISION, 2 },
		{ GameMessage::MSG_META_OBSERVER_TOGGLE_RADAR, CON_HIDE, CON_MODULE_RADAR },
		{ GameMessage::MSG_META_OBSERVER_TOGGLE_BOARD, CON_HIDE, CON_MODULE_BOARD },
		{ GameMessage::MSG_META_OBSERVER_TOGGLE_DOSSIER, CON_HIDE, CON_MODULE_DOSSIER },
		{ GameMessage::MSG_META_OBSERVER_TOGGLE_HUD, CON_HIDE_ALL, 0 },
		{ GameMessage::MSG_META_OBSERVER_NEXT_SEAT, CON_SEAT_STEP, 1 },
		{ GameMessage::MSG_META_OBSERVER_PREV_SEAT, CON_SEAT_STEP, -1 },
		{ GameMessage::MSG_META_OBSERVER_NEXT_STAT, CON_STAT_STEP, 1 },
		{ GameMessage::MSG_META_OBSERVER_PREV_STAT, CON_STAT_STEP, -1 },
		{ GameMessage::MSG_META_OBSERVER_TOGGLE_STANDINGS, CON_STANDINGS, 0 },
		{ GameMessage::MSG_META_OBSERVER_VISION_CYCLE, CON_VISION_CYCLE, 0 }
	};
	for( Int each = 0; each < ARRAY_SIZE( COMMANDS ); each++ )
		if( COMMANDS[ each ].meta == meta )
		{
			runConsoleAction( COMMANDS[ each ].action, COMMANDS[ each ].arg );
			return TRUE;
		}
	return FALSE;
}

/// the wheel over the board steps the picked stat; TRUE when the board took it
Bool InGameUI::handleConsoleWheel( const ICoord2D *mouse, Real notches )
{
	if( !theConsoleShown || m_hudCellDrawnPass + 1 != m_hudCellPass || theConsoleHidden[ CON_MODULE_BOARD ] )
		return FALSE;
	const IRegion2D &board = theConsoleBlock[ 1 ];
	if( mouse->x < board.lo.x || mouse->x >= board.hi.x || mouse->y < board.lo.y || mouse->y >= board.hi.y )
		return FALSE;
	if( notches != 0.0f )
		runConsoleAction( CON_STAT_STEP, notches < 0.0f ? 1 : -1 );
	return TRUE;
}


//-------------------------------------------------------------------------------------------------
/** A chevron at the edge of the view pointing at each alarm that is off the screen, in the alarm's
	* colour on a dark keyline as the minimap's marks are: an attack, a building lost, an infiltration
	* or one of your stealth units revealed.  Built, upgraded and the rest are news, not a reason to
	* look away.  An attack's chevron breathes once a second while the fight lasts.  The edge is the
	* view's above the command bar; the chevrons go with the HUD, so not while the bar is hidden, a
	* script hides the interface or -cinema films. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawRadarAlarmPointers( void )
{
	if( TheRadar == NULL || TheTacticalView == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;
	if( !TheGameLogic->getDrawIconUI() || CinemaDirector_hidesHud() )
		return;
	const Int barTop = controlBarTop();
	if( barTop >= TheDisplay->getHeight() )
		return;

	// the alarms are the minimap's: none to point at for a player whose radar draws none
	// (W3DLeftHUDDraw's test; postDraw runs at the sync point, so the player may be asked)
	Player *local = ThePlayerList->getLocalPlayer();
	if( !TheRadar->isRadarForced() && ( TheRadar->isRadarHidden() || local == NULL || !local->hasRadar() ) )
		return;

	// sized against a 1080 line screen, as the HUD's cells are
	const Real scale = max( 1.0f, TheDisplay->getHeight() / 1080.0f );
	const Real size = 10.0f * scale;
	const Real margin = 14.0f * scale;

	Int originX, originY;
	TheTacticalView->getOrigin( &originX, &originY );
	const Real left = originX + margin;
	const Real top = max( originY, hudStripBottom() ) + margin;		// under the info strip, which paints after
	// the Red Alert style sidebar stands over the view's right side and holds the radar itself
	const Bool sidebar = sidebarLayoutOn();
	const Int viewRight = originX + TheTacticalView->getWidth();
	const Real right = ( sidebar ? min( viewRight, (Int)hudPlay().hi.x ) : viewRight ) - margin;
	const Real bottom = min( originY + TheTacticalView->getHeight(), barTop ) - margin;
	if( right <= left || bottom <= top )
		return;
	const Real centreX = ( left + right ) * 0.5f;
	const Real centreY = ( top + bottom ) * 0.5f;

	// The radar's frame stands above the bar on the left and rises with Minimap Size, and the windows
	// paint after postDraw: over the frame's width the edge is the frame's top.  The frame sits on the
	// screen's left edge, so its right side is all that is needed of its width.
	Real radarRight = 0.0f;
	{
		static NameKeyType radarKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:LeftHUD" );
		GameWindow *radar = TheWindowManager->winGetWindowFromId( NULL, radarKey );
		if( !sidebar && radar && !radar->winIsHidden() )
		{
			IRegion2D inner, outer;
			radar->winGetScreenPosition( &inner.lo.x, &inner.lo.y );
			radar->winGetSize( &inner.hi.x, &inner.hi.y );
			inner.hi.x += inner.lo.x;
			inner.hi.y += inner.lo.y;
			outer = inner;
			const Image *const *radarFrame = TheControlBar->getControlBarSchemeManager()->getRadarFrame();
			if( radarFrame )
				ControlBarSchemeManager::radarFrameRect( radarFrame, inner, &outer );
			radarRight = outer.hi.x + margin;
		}
	}
	const Real radarBottom = min( bottom, (Real)radarFrameTop() - margin );

	const UnsignedInt frame = TheGameLogic->getFrame();
	const Real pulsePhase = (Real)( frame % LOGICFRAMES_PER_SECOND ) / LOGICFRAMES_PER_SECOND;
	const Real breathe = 0.6f + 0.4f * ( 1.0f - fabs( 2.0f * pulsePhase - 1.0f ) );

	// all lines, no image: one batch holds every chevron (postDraw replays outside any batch)
	TheDisplay->beginBatch2D();

	for( Int slot = 0; slot < TheRadar->getEventSlotCount(); ++slot )
	{
		RadarEventType type;
		Coord3D world;
		RGBAColorInt colour;
		Real fade;
		if( !TheRadar->getShownEvent( slot, &type, &world, &colour, &fade ) )
			continue;
		if( type != RADAR_EVENT_UNDER_ATTACK && type != RADAR_EVENT_STRUCTURE_LOST &&
				type != RADAR_EVENT_INFILTRATION && type != RADAR_EVENT_STEALTH_NEUTRALIZED )
			continue;

		// which way on the screen: toward the alarm's own pixel while the camera can project it, else
		// toward a point a step from the view's middle along the ground, which it always can
		ICoord2D pixel;
		const View::WorldToScreenReturn seen = TheTacticalView->worldToScreenTriReturn( &world, &pixel );
		Real dirX, dirY;
		if( seen != View::WTS_INVALID )
		{
			if( seen == View::WTS_INSIDE_FRUSTUM && pixel.x >= left && pixel.x <= right && pixel.y >= top &&
					pixel.y <= ( pixel.x <= radarRight ? radarBottom : bottom ) )
				continue;		// on the screen, where the alarm can be seen for itself
			dirX = pixel.x - centreX;
			dirY = pixel.y - centreY;
		}
		else
		{
			Coord3D look;
			TheTacticalView->getPosition( &look );
			look.z = world.z;
			const Real dx = world.x - look.x;
			const Real dy = world.y - look.y;
			const Real length = sqrtf( dx * dx + dy * dy );
			if( length < 1.0f )
				continue;
			ICoord2D from, to;
			if( TheTacticalView->worldToScreenTriReturn( &look, &from ) == View::WTS_INVALID )
				continue;
			// a long step, so the whole pixels it lands on still give the angle; shorter only when a step
			// toward the camera would pass behind it
			Bool projected = FALSE;
			for( Real stepLength = 400.0f; stepLength >= 50.0f && !projected; stepLength *= 0.5f )
			{
				Coord3D step = look;
				step.x += dx / length * stepLength;
				step.y += dy / length * stepLength;
				projected = TheTacticalView->worldToScreenTriReturn( &step, &to ) != View::WTS_INVALID;
			}
			if( !projected )
				continue;
			dirX = (Real)( to.x - from.x );
			dirY = (Real)( to.y - from.y );
		}
		const Real dirLength = sqrtf( dirX * dirX + dirY * dirY );
		if( dirLength < 0.001f )
			continue;
		dirX /= dirLength;
		dirY /= dirLength;

		// where the ray from the middle leaves the box
		Real reach = 1.0e9f;
		if( fabs( dirX ) > 0.0001f )
			reach = min( reach, ( right - left ) * 0.5f / (Real)fabs( dirX ) );
		if( fabs( dirY ) > 0.0001f )
			reach = min( reach, ( bottom - top ) * 0.5f / (Real)fabs( dirY ) );
		Real tipX = centreX + dirX * reach;
		Real tipY = centreY + dirY * reach;
		// past the radar frame's top, it stops at the frame
		if( tipX <= radarRight + size && tipY > radarBottom && dirY > 0.0001f )
		{
			reach = ( radarBottom - centreY ) / dirY;
			tipX = centreX + dirX * reach;
			tipY = centreY + dirY * reach;
		}

		Real alpha = fade;
		if( type == RADAR_EVENT_UNDER_ATTACK )
			alpha *= breathe;
		const Color stroke = GameMakeColor( (UnsignedByte)colour.red, (UnsignedByte)colour.green, (UnsignedByte)colour.blue,
																				REAL_TO_UNSIGNEDBYTE( colour.alpha * alpha ) );
		const Color keyline = GameMakeColor( 8, 10, 8, REAL_TO_UNSIGNEDBYTE( 200.0f * alpha ) );

		// two chevrons, the one behind smaller, pointing out of the screen
		for( Int pass = 0; pass < 2; ++pass )
		{
			for( Int chevron = 0; chevron < 2; ++chevron )
			{
				const Real back = chevron * size * 0.7f;
				const Real wing = size * ( chevron == 0 ? 0.8f : 0.6f );
				const Real pointX = tipX - dirX * back;
				const Real pointY = tipY - dirY * back;
				const Real baseX = pointX - dirX * size * 0.8f;
				const Real baseY = pointY - dirY * size * 0.8f;
				const Int px = REAL_TO_INT_FLOOR( pointX + 0.5f );
				const Int py = REAL_TO_INT_FLOOR( pointY + 0.5f );
				const Int ax = REAL_TO_INT_FLOOR( baseX - dirY * wing + 0.5f );
				const Int ay = REAL_TO_INT_FLOOR( baseY + dirX * wing + 0.5f );
				const Int bx = REAL_TO_INT_FLOOR( baseX + dirY * wing + 0.5f );
				const Int by = REAL_TO_INT_FLOOR( baseY - dirX * wing + 0.5f );
				const Real width = ( chevron == 0 ? 3.0f : 2.0f ) * scale + ( pass == 0 ? 2.0f : 0.0f );
				const Color c = pass == 0 ? keyline : stroke;
				TheDisplay->drawLine( ax, ay, px, py, width, c );
				TheDisplay->drawLine( bx, by, px, py, width, c );
			}
		}
	}

	TheDisplay->endBatch2D();
}

//-------------------------------------------------------------------------------------------------
/** A cell's readout string, made the first time in the superweapon countdown's face, bold, and set
	* to `text` only when it holds something else: a string laid out again builds its texture again. */
//-------------------------------------------------------------------------------------------------
DisplayString *InGameUI::cellString( DisplayString *&string, const UnicodeString &text )
{
	if( string == NULL )
	{
		string = TheDisplayStringManager->newDisplayString();
		string->setFont( TheFontLibrary->getFont( m_superweaponNormalFont,
			TheGlobalLanguageData->adjustFontSize( HUD_CELL_POINT_SIZE ), TRUE ) );
	}
	if( string->getText().compare( text ) != 0 )
		string->setText( text );
	return string;
}

//-------------------------------------------------------------------------------------------------
/** A countdown in a cameo's bottom left corner, in bare seconds with the unit on it, "45s": the
	* picture's middle is what it is there to show, and m:ss is a number to convert first.  A number
	* too wide for the cameo with its s goes without it, and stays without until the number changes,
	* so the string is not laid out twice a frame. */
//-------------------------------------------------------------------------------------------------
void InGameUI::drawCellSeconds( DisplayString *&string, Int seconds, const IRegion2D &cameo, const Image *plate )
{
	UnicodeString bare, unit;
	bare.format( u"%d", seconds > 0 ? seconds : 0 );
	unit.format( u"%ds", seconds > 0 ? seconds : 0 );
	DisplayString *text = NULL;
	if( string != NULL && string->getText().compare( bare ) == 0 )
		text = string;
	else
	{
		text = cellString( string, unit );
		if( !HudReadout_fits( text, cameo ) )
			text = cellString( string, bare );
	}
	HudReadout_draw( text, cameo, HUD_READOUT_BOTTOM_LEFT, GameMakeColor( 245, 245, 245, 255 ), plate );
}

//-------------------------------------------------------------------------------------------------
/** The cells lie over the world rather than in a window, so nothing ahead of PlaceEventTranslator has
	* claimed a click on them; it asks here first, before placement and selection turn the click into
	* an order.  A cell is its cell piece, cameo, border and margin, and a "+N" tab is its picture; the
	* tray's edges round the group stay the world's.  A cancel is the command bar's own
	* message with the producer on it, which the logic checks is the sender's. */
//-------------------------------------------------------------------------------------------------
Bool InGameUI::handleHudCellClick( const ICoord2D *mouse, Bool cancel )
{
	// the boxes are the last pass's draw, and only if that pass drew: a click comes after the pass's
	// update (logic thread 0) or at the start of the next pass ahead of its draw (1 and 2), and either
	// way update() has counted the drawing pass once since.  A pass that drew nothing over the world
	// (-cinema, the load screen, -headless, TiVO's skipped frames) leaves the stamp behind
	if( m_hudCellDrawnPass + 1 != m_hudCellPass )
		return FALSE;

	auto inside = [ mouse ]( const IRegion2D &box )
	{
		return mouse->x >= box.lo.x && mouse->x < box.hi.x && mouse->y >= box.lo.y && mouse->y < box.hi.y;
	};

	// the airlift panel lies over everything else
	if( AirliftPanel_click( mouse, cancel ) )
		return TRUE;

	// a "+N" tab stands for no one item, and the Art of Defense panel for none at all
	if( inside( m_productionListTab ) || inside( m_superweaponTab ) || inside( s_aodPanel ) )
		return TRUE;

	// the hero's cell is the SELECT_HERO key: select him, or the transport he rides, and look at him.
	// A cancel has nothing to take off
	if( inside( m_heroCell ) )
	{
		if( !cancel )
			TheMessageStream->appendMessage( GameMessage::MSG_META_SELECT_HERO );
		return TRUE;
	}

	if( GroupPanel_click( mouse, cancel ) )
		return TRUE;

	// the observer HUD: its keys, rows and cells, the last drawn on top (an open list over the rows), and the
	// rest of its steel is its own; a click anywhere else closes an open list and goes no further
	for( size_t each = theConsoleHits.size(); each > 0; each-- )
	{
		const ConsoleHit &hit = theConsoleHits[ each - 1 ];
		if( !inside( hit.box ) )
			continue;
		if( !cancel )
			runConsoleAction( hit.action, hit.arg );
		return TRUE;
	}
	const Bool listWasOpen = theDropOpen >= 0;
	theDropOpen = -1;
	for( Int each = 0; each < 4; each++ )
		if( inside( theConsoleBlock[ each ] ) )
			return TRUE;
	if( listWasOpen )
		return TRUE;


	for( Int each = 0; each < m_productionListCells; each++ )
	{
		if( !inside( m_productionListPlace[ each ] ) )
			continue;
		const ProductionListSlot &slot = m_productionList[ each ];
		Object *producer = TheGameLogic->findObjectByID( slot.producer );
		if( producer == NULL )
			return TRUE;				// gone since the draw: the click is still the cell's
		if( cancel && !slot.isStructure && producer->isLocallyControlled() )
		{
			GameMessage *msg = TheMessageStream->appendMessage( slot.isUpgrade ? GameMessage::MSG_CANCEL_UPGRADE
																																				 : GameMessage::MSG_CANCEL_UNIT_CREATE );
			msg->appendIntegerArgument( slot.id );
			msg->appendObjectIDArgument( slot.producer );
		}
		else
			TheTacticalView->lookAt( producer->getPosition() );
		return TRUE;
	}

	for( Int each = 0; each < m_superweaponCells; each++ )
	{
		if( !inside( m_superweaponPlace[ each ] ) )
			continue;
		Object *building = TheGameLogic->findObjectByID( m_superweaponJump[ each ] );
		if( building != NULL )
			TheTacticalView->lookAt( building->getPosition() );
		return TRUE;
	}

	return FALSE;
}

void InGameUI::drawFloatingText( void )
{
	FloatingTextData *ftd;
	const UnsignedInt now = Clock_Milliseconds();
	// loop through and draw all the texts
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end(); ++it)
	{
		ftd = *it;
		ICoord2D pos;
		// whose fog the screen is drawn in
		Int playerNdx = TheObserverCamera.getShroudPlayerIndex();

		// which PartitionManager cells are we looking at?
		Int pCX, pCY;
		ThePartitionManager->worldToCell(ftd->m_pos3D.x, ftd->m_pos3D.y, &pCX, &pCY);

		// translate it's 3d pos into a 2d screen pos
		if( TheTacticalView->worldToScreen(&ftd->m_pos3D, &pos)
			&& ftd->m_dString
			&& ThePartitionManager->getShroudStatusForPlayer(playerNdx, pCX, pCY) == CELLSHROUD_CLEAR )
		{
			// screen pixels throughout, never the camera's: the pop is one size at every zoom
			const UnsignedInt ageMs = now - ftd->m_bornMs;
			const Int alpha = floatingTextAlpha( ageMs, m_floatingTextTimeOut.raw(), m_floatingTextMoveVanishRate.raw() );

			// retail's steady drift, its pixels a second at 800x600 grown with the screen as the bar is
			const Int rise = REAL_TO_INT( m_floatingTextMoveUpSpeed.raw() * TheUIScale() * ageMs / 1000.0f );

			UnsignedByte r, g, b, a;
			GameGetColorComponents( ftd->m_color, &r, &g, &b, &a );
			const Color fill = GameMakeColor( r, g, b, alpha );
			// one drop shadow, as retail's: solid black a screen pixel down and right (two from 4K up), or a
			// light grey or olive seat's figure is lost on rails, gravel and sand; 2/5 black read as none
			const Color shade = GameMakeColor( 0, 0, 0, alpha );
			const Int k = Drawable_pipPixel( TheUIScale() );

			Int width, height;
			ftd->m_dString->getSize(&width, &height);
			ftd->m_dString->draw( pos.x - width / 2, pos.y - height - rise, fill, shade, k, k );
		}

	}
}

//-------------------------------------------------------------------------------------------------
/** ittereate through and clear out the list of floating text */
//-------------------------------------------------------------------------------------------------
void InGameUI::clearFloatingText( void )
{
	FloatingTextData *ftd;
	// loop through and draw all the texts
	for(FloatingTextListIt it = m_floatingTextList.begin(); it != m_floatingTextList.end();)
	{
		ftd = *it;
		it = m_floatingTextList.erase(it);
		ftd->deleteInstance();
	}
	
}

//-------------------------------------------------------------------------------------------------
/** If we want to use the default text color, then we call this function */
//-------------------------------------------------------------------------------------------------
void InGameUI::popupMessage( const AsciiString& message, Int x, Int y, Int width, Bool pause, Bool pauseMusic)
{
	popupMessage( message, x, y, width, m_popupMessageColor, pause, pauseMusic);
}

//-------------------------------------------------------------------------------------------------
/** initialize, and popup a message box to the user */
//-------------------------------------------------------------------------------------------------
void InGameUI::popupMessage( const AsciiString& identifier, Int x, Int y, Int width, Color textColor, Bool pause, Bool pauseMusic)
{
	// the pause it may ask for stops the next tick either way
	LOGIC_AFTER_TICK( [=]() { popupMessage( identifier, x, y, width, textColor, pause, pauseMusic ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	if(m_popupMessageData)
		clearPopupMessageData();

	UpdateDiplomacyBriefingText(identifier, FALSE);

	UnicodeString message = TheGameText->fetch(identifier);

	m_popupMessageData = newInstance( PopupMessageData );	
	m_popupMessageData->message = message;
	// x and why are passed in as a percentage of the screen, convert to screen coords
	if( x > 100 )
		x = 100;
	if( x < 0 )
		x = 0;
	
	if( y > 100 )
		y = 100;
	if( y < 0 )
		y = 0;

	const UIRect box = TheUIRect();
	m_popupMessageData->x = box.x + box.w * (INT_TO_REAL(x) / 100);
	m_popupMessageData->y = box.y + box.h * (INT_TO_REAL(y) / 100);
	// cap the lower limit of the width
	if(width < 50)
		width = 50;
	m_popupMessageData->width = width;
	m_popupMessageData->textColor = textColor;
	m_popupMessageData->pause = pause;
	m_popupMessageData->pauseMusic = pauseMusic;

	if( pause )
		TheGameLogic->setGamePaused(TRUE, pauseMusic);

	m_popupMessageData->layout = TheWindowManager->winCreateLayout(AsciiString("InGamePopupMessage.wnd"));
	m_popupMessageData->layout->runInit();
}

//-------------------------------------------------------------------------------------------------
/** take care of the logic of clearing the popupMessageData */
//-------------------------------------------------------------------------------------------------
void InGameUI::clearPopupMessageData( void )
{
	LOGIC_AFTER_TICK( [=]() { clearPopupMessageData(); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	if(!m_popupMessageData)
		return;
	if(m_popupMessageData->layout)
	{
		m_popupMessageData->layout->destroyWindows();
		m_popupMessageData->layout->deleteInstance();
		m_popupMessageData->layout = NULL;
	}
	if( m_popupMessageData->pause )
		TheGameLogic->setGamePaused(FALSE, m_popupMessageData->pauseMusic);
	m_popupMessageData->deleteInstance();
	m_popupMessageData = NULL;
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------
/** Floating Text Constructor */
//-------------------------------------------------------------------------------------------------
FloatingTextData::FloatingTextData(void)
{
	// Added By Sadullah Nader
	// Initializations missing and needed
	m_color = 0;
	m_bornMs = 0;
	m_pos3D.zero();
	m_text.clear();
	//
	m_dString = TheDisplayStringManager->newDisplayString();
}

//-------------------------------------------------------------------------------------------------
/** Floating Text Destructor */
//-------------------------------------------------------------------------------------------------
FloatingTextData::~FloatingTextData(void)
{
	if(m_dString)
		TheDisplayStringManager->freeDisplayString( m_dString );
	m_dString = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
// WORLD ANIMATION DATA ///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WorldAnimationData::WorldAnimationData( void )
{

	m_anim = NULL;
	m_worldPos.zero();
	m_expireFrame = 0;
	m_options = WORLD_ANIM_NO_OPTIONS;
	m_zRisePerSecond = 0.0f;

}  // end WorldAnimationData

// ------------------------------------------------------------------------------------------------
/** Add a 2D animation at a spot in the world */
// ------------------------------------------------------------------------------------------------
void InGameUI::addWorldAnimation( Anim2DTemplate *animTemplate,
																	const Coord3D *pos,
																	WorldAnimationOptions options,
																	Real durationInSeconds,
																	Real zRisePerSecond )
{
	if( LogicThread::defersEffects() && pos != NULL )
	{
		const Coord3D at = *pos;
		LogicThread::callAfterTick( [=]() { addWorldAnimation( animTemplate, &at, options, durationInSeconds, zRisePerSecond ); } );
		return;
	}
	LOGIC_CENSUS( CENSUS_INGAMEUI );

	// sanity
	if( animTemplate == NULL || pos == NULL || durationInSeconds <= 0.0f )
		return;

	// allocate a new world animation data struct
	// (huh huh, he said "wad")
	WorldAnimationData *wad = NEW WorldAnimationData;
	if( wad == NULL )
		return;		

	// allocate a new animation instance
	Anim2D *anim = newInstance(Anim2D)( animTemplate, TheAnim2DCollection );

	// assign all data
	wad->m_anim = anim;
	wad->m_expireFrame = TheGameLogic->getFrame() + (durationInSeconds * LOGICFRAMES_PER_SECOND);
	wad->m_options = options;
	wad->m_worldPos = *pos;
	wad->m_zRisePerSecond = zRisePerSecond;

	// add to list
	m_worldAnimationList.push_front( wad );

}  // end addWorldAnimation

// ------------------------------------------------------------------------------------------------
/** Delete all world animations */
// ------------------------------------------------------------------------------------------------
void InGameUI::clearWorldAnimations( void )
{
	WorldAnimationData *wad;

	// iterate through all entries and delete the animation data
	for( WorldAnimationListIterator it = m_worldAnimationList.begin();	
			 it != m_worldAnimationList.end(); /*empty*/ )
	{

		wad = *it;
		if( wad )
		{

			// delete the animation instance
			wad->m_anim->deleteInstance();

			// delete the world animation data
			delete wad;

		}  // end if

		it = m_worldAnimationList.erase( it );

	}  // end for
	
}  // end clearWorldAnimations

static const UnsignedInt FRAMES_BEFORE_EXPIRE_TO_FADE = LOGICFRAMES_PER_SECOND * 1;
// ------------------------------------------------------------------------------------------------
/** Update all world animations and draw the visible ones */
// ------------------------------------------------------------------------------------------------
void InGameUI::updateAndDrawWorldAnimations( void )
{
	WorldAnimationData *wad;

	// go through all animations
	for( WorldAnimationListIterator it = m_worldAnimationList.begin();
			 it != m_worldAnimationList.end(); /*empty*/ )
	{

		// get data
		wad = *it;

		// update portion ... only when the game is in motion
		if( wad && TheGameLogic->isGamePaused() == FALSE )
		{

			//
			// see if it's time to expire this animation based on animation type and options or
			// the expire frame
			//
			if( TheGameLogic->getFrame() >= wad->m_expireFrame ||
					(BitTest( wad->m_options, WORLD_ANIM_PLAY_ONCE_AND_DESTROY ) &&
					 BitTest( wad->m_anim->getStatus(), ANIM_2D_STATUS_COMPLETE )) )
			{

				// delete this element and continue
				wad->m_anim->deleteInstance();
				delete wad;
				it = m_worldAnimationList.erase( it );
				continue;

			}  // end if

			// update the Z value
			if( wad->m_zRisePerSecond )
				wad->m_worldPos.z += wad->m_zRisePerSecond / RETAIL_LOGICFRAMES_PER_SECOND;	// a render pass, not a logic frame: kept as it was

		}  // end if

		//
		// don't bother going forward with the draw process if this location is shrouded for
		// the local player
		//
		Int playerIndex = TheObserverCamera.getShroudPlayerIndex();
		if( ThePartitionManager->getShroudStatusForPlayer( playerIndex, &wad->m_worldPos ) != CELLSHROUD_CLEAR )
		{

			++it;
			continue;

		}  // end if

		// update translucency value
		Real alpha = 1.0f;
		if( BitTest( wad->m_options, WORLD_ANIM_FADE_ON_EXPIRE ) )
		{

			// see if we should be setting the translucency value
			UnsignedInt framesTillExpire = wad->m_expireFrame - TheGameLogic->getFrame();
			if( framesTillExpire < FRAMES_BEFORE_EXPIRE_TO_FADE )
			{
				
				// compute alpha level so that we're totally gone by the expire frame
				alpha = INT_TO_REAL( framesTillExpire ) / INT_TO_REAL( FRAMES_BEFORE_EXPIRE_TO_FADE );
				wad->m_anim->setAlpha( alpha );
				
			}  // end if

		}  // end if

		// project the point to screen space
		ICoord2D screen;
		if( TheTacticalView->worldToScreen( &wad->m_worldPos, &screen ) == TRUE )
		{
			// The artwork's own size at every zoom. It was scaled by 1.3 / zoom (1.3 the old furthest
			// zoom the art was drawn against), about 1 at the opening camera, so the heal cross, the
			// crate's dollar sign and the promotion chevrons swelled over a close-up unit and shrank
			// to specks far out. A sign reads at one size.
			const UnsignedInt width = wad->m_anim->getCurrentFrameWidth();
			const UnsignedInt height = wad->m_anim->getCurrentFrameHeight();

			// adjust the screen position to draw so the image is centered at the location
			screen.x -= width / 2;
			screen.y -= height / 2;

			// draw the animation
			wad->m_anim->draw( screen.x, screen.y, width, height );

		}  // end if

		// go to the next element in the list
		++it;

	}  // end for

}  // end updateAndDrawWorldAnimations


void InGameUI::addIdleWorker( Object *obj )
{
	if(!obj)
		return;
	addIdleWorkerFor( obj, obj->getControllingPlayer()->getPlayerIndex() );
}

/* The list keeps the pointer and never reads through it here, so after a tick it may name an object
	 that tick destroyed: the destroy queued its removeIdleWorker behind this add. */
void InGameUI::addIdleWorkerFor( Object *obj, Int index )
{
	LOGIC_AFTER_TICK( [=]() { addIdleWorkerFor( obj, index ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	if( std::find( m_idleWorkers[index].begin(), m_idleWorkers[index].end(), obj ) != m_idleWorkers[index].end() )
		return;

	m_idleWorkers[index].push_back(obj);
}

void InGameUI::removeIdleWorker( Object *obj, Int playerNumber )
{
	if(!obj)
		return;
	// compares the pointer only, so the object may be gone by the time this runs
	LOGIC_AFTER_TICK( [=]() { removeIdleWorker( obj, playerNumber ); } );
	LOGIC_CENSUS( CENSUS_INGAMEUI );
	if(playerNumber < 0 || playerNumber >= MAX_PLAYER_COUNT)  // we're leaving the game, so this is all screwed
		return;
	
	if(m_idleWorkers[playerNumber].empty())
		return;

	
	ObjectListIt it = m_idleWorkers[playerNumber].begin();
	while(it != m_idleWorkers[playerNumber].end())
	{
		Object *itObj = *it;
		if(itObj == obj)
		{
			m_idleWorkers[playerNumber].erase(it);
			return;
		}
		++it;
	}
	return;
}

// The worker key and the worker button.  With IdleWorkerCamera on (the default) every layout centres
// the camera on the worker picked, or on what carries it, each press, as the original game's
// selectNextIdleWorker did; off, only Classic, the original game's bar, does (issue #232) and Reforged
// and Red Alert Style leave the camera where the player has it.
Object *InGameUI::selectNextIdleWorker( void )
{
	// the same walk the button's light comes from, so a lit button always has a worker to give
	KindOfMaskType mustBeSet, mustBeClear;
	idleUnitKinds( GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER, &mustBeSet, &mustBeClear );
	Object *picked = selectNextIdleUnit( mustBeSet, mustBeClear );
	if( picked && ( TheGlobalData->m_idleWorkerCamera || TheGlobalData->m_hudLayout == HUD_LAYOUT_RETAIL ) && TheTacticalView )
		TheTacticalView->lookAt( picked->getPosition() );
	return picked;
}

// A unit of the local player's that is standing on the field with no order: not a dozer, a worker
// or a supply truck (those have the idle-worker key), not a structure, not riding in anything.
Bool InGameUI::isIdleCombatUnit( const Object *obj )
{
	if( !obj->isLocallyControlled() || obj->isEffectivelyDead() || obj->isContained() || !obj->isMobile() )
		return FALSE;

	if( !obj->isKindOf( KINDOF_SELECTABLE ) || obj->isKindOf( KINDOF_NO_SELECT ) || obj->isKindOf( KINDOF_IGNORED_IN_GUI ) )
		return FALSE;

	if( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) )
		return FALSE;

	// a player owns things with no AI module at all
	const AIUpdateInterface *ai = obj->getAI();
	return ai && ai->isIdle();
}

// A dozer or worker of the local player's with no order, riding in something or not: the idle
// worker button selects what carries it.  A Worker out on a supply run is busy even on the frames
// its AI stands idle between two docks.  Read here every pass, where the list the dozers keep
// themselves fills only when their sleeping idle state wakes, seconds late.
static Bool isIdleWorker( const Object *obj )
{
	if( !obj->isLocallyControlled() || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_DOZER ) )
		return FALSE;

	const AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || !ai->isIdle() )
		return FALSE;

	const SupplyTruckAIInterface *truck = ai->getSupplyTruckAIInterface();
	return truck == NULL || !truck->isCurrentlyFerryingSupplies();
}

// what an idle key of the kinds idleUnitKinds gave cycles over
static Bool isIdleOfKinds( const Object *obj, const KindOfMaskType &mustBeSet, const KindOfMaskType &mustBeClear )
{
	if( mustBeSet.test( KINDOF_DOZER ) )
		return isIdleWorker( obj );
	return InGameUI::isIdleCombatUnit( obj ) && obj->isKindOfMulti( mustBeSet, mustBeClear );
}

void InGameUI::idleUnitKinds( GameMessage::Type t, KindOfMaskType *mustBeSet, KindOfMaskType *mustBeClear )
{
	if( t == GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER )
		mustBeSet->set( KINDOF_DOZER );
	else if( t == GameMessage::MSG_META_SELECT_NEXT_IDLE_AIRCRAFT )
		mustBeSet->set( KINDOF_AIRCRAFT );
	else if( t == GameMessage::MSG_META_SELECT_NEXT_IDLE_VEHICLE )
	{
		mustBeSet->set( KINDOF_VEHICLE );
		mustBeClear->set( KINDOF_AIRCRAFT );
	}
	else if( t == GameMessage::MSG_META_SELECT_NEXT_IDLE_INFANTRY )
		mustBeSet->set( KINDOF_INFANTRY );
}

// what a right click on an idle button takes (the select all key of the kind, kindOfUnitSelection
// across the map), save the locally controlled test its caller has made
static Bool isSelectAllOfKinds( const Object *obj, const KindOfMaskType &mustBeSet, const KindOfMaskType &mustBeClear )
{
	if( obj->isEffectivelyDead() || obj->isContained() || obj->isOffMap() )
		return FALSE;
	if( obj->isKindOf( KINDOF_IGNORES_SELECT_ALL ) || obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_STRUCTURE ) )
		return FALSE;
	const Drawable *draw = obj->getDrawable();
	return draw && draw->isMassSelectable() && obj->isKindOfMulti( mustBeSet, mustBeClear );
}

//-------------------------------------------------------------------------------------------------
/** One walk of the object list a frame sets each idle button in one of three looks.  Something of
	* its kind standing idle: the highlighted art, which a click cycles over.  Some of the kind but
	* none idle: the plain art, live, since a right click on the infantry, vehicle and aircraft
	* buttons still takes them all and a left click finds nothing and leaves the selection alone.
	* None of the kind at all: dark, and dead to both clicks.  The worker button takes no right
	* click, so it has only the first and the last.  The four light on the same pass, and each
	* cycles over what lit it. */
//-------------------------------------------------------------------------------------------------
void InGameUI::updateIdleUnitButtons( void )
{
	enum { IDLE_BUTTON_COUNT = 4 };
	static const GameMessage::Type keys[ IDLE_BUTTON_COUNT ] =
	{
		GameMessage::MSG_META_SELECT_NEXT_IDLE_INFANTRY,
		GameMessage::MSG_META_SELECT_NEXT_IDLE_VEHICLE,
		GameMessage::MSG_META_SELECT_NEXT_IDLE_AIRCRAFT,
		GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER,
	};
	static const NameKeyType ids[ IDLE_BUTTON_COUNT ] =
	{
		NAMEKEY( "ControlBar.wnd:ButtonIdleInfantry" ),
		NAMEKEY( "ControlBar.wnd:ButtonIdleVehicle" ),
		NAMEKEY( "ControlBar.wnd:ButtonIdleAircraft" ),
		NAMEKEY( "ControlBar.wnd:ButtonIdleWorker" ),
	};
	// the worker's right click goes nowhere, so owning a busy one leaves its button dark
	static const Bool rightClick[ IDLE_BUTTON_COUNT ] = { TRUE, TRUE, TRUE, FALSE };
	// the scheme's plain art per button, kept while the highlighted art stands in its place; taken
	// again whenever the scheme puts its own back (a side or resolution change)
	static const Image *plain[ IDLE_BUTTON_COUNT ] = { NULL, NULL, NULL, NULL };
	GameWindow *win[ IDLE_BUTTON_COUNT ];
	KindOfMaskType mustBeSet[ IDLE_BUTTON_COUNT ], mustBeClear[ IDLE_BUTTON_COUNT ];
	Bool idle[ IDLE_BUTTON_COUNT ] = { FALSE, FALSE, FALSE, FALSE };
	Bool owned[ IDLE_BUTTON_COUNT ] = { FALSE, FALSE, FALSE, FALSE };
	Int i;
	for( i = 0; i < IDLE_BUTTON_COUNT; i++ )
	{
		win[ i ] = TheWindowManager->winGetWindowFromId( NULL, ids[ i ] );
		idleUnitKinds( keys[ i ], &mustBeSet[ i ], &mustBeClear[ i ] );
	}
	if( win[ 0 ] == NULL || !getInputEnabled() || !TheGameLogic->isInGame() )
	{
		for( i = 0; i < IDLE_BUTTON_COUNT; i++ )
			if( win[ i ] )
				win[ i ]->winEnable( FALSE );
		return;
	}

	// ponytail: the whole object list every render frame; keep a list per kind if it shows in a profile.
	// The owned test rides the same pass and drops out per kind at its first find.
	Int lit = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj && lit < IDLE_BUTTON_COUNT; obj = obj->getNextObject() )
	{
		if( !obj->isLocallyControlled() )
			continue;
		for( i = 0; i < IDLE_BUTTON_COUNT; i++ )
		{
			if( !idle[ i ] && isIdleOfKinds( obj, mustBeSet[ i ], mustBeClear[ i ] ) )
			{
				idle[ i ] = TRUE;
				lit++;
			}
			if( rightClick[ i ] && !owned[ i ] && isSelectAllOfKinds( obj, mustBeSet[ i ], mustBeClear[ i ] ) )
				owned[ i ] = TRUE;
		}
	}
	for( i = 0; i < IDLE_BUTTON_COUNT; i++ )
	{
		if( win[ i ] == NULL )
			continue;
		win[ i ]->winEnable( idle[ i ] || owned[ i ] );
		const Image *hilite = GadgetButtonGetHiliteImage( win[ i ] );
		const Image *shown = GadgetButtonGetEnabledImage( win[ i ] );
		if( shown != hilite )
			plain[ i ] = shown;
		const Image *want = ( idle[ i ] && hilite ) || plain[ i ] == NULL ? hilite : plain[ i ];
		if( shown != want )
			GadgetButtonSetEnabledImage( win[ i ], want );
	}
}

Object *InGameUI::selectNextIdleUnit( KindOfMaskType mustBeSet, KindOfMaskType mustBeClear )
{
	// The key walks the object list, the one updateIdleUnitButtons lights the buttons from, which is
	// in creation order and stays put between presses.  A lone selected unit is the place to carry
	// on from; with nothing or a group selected the walk starts over.
	const Object *current = getSelectCount() == 1 ? getFirstSelectedDrawable()->getObject() : NULL;
	Object *first = NULL;
	Object *selectThisObject = NULL;
	Bool passedCurrent = FALSE;

	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( isIdleOfKinds( obj, mustBeSet, mustBeClear ) )
		{
			if( passedCurrent )
			{
				selectThisObject = obj;
				break;
			}
			if( !first )
				first = obj;
		}
		if( obj == current )
			passedCurrent = TRUE;
	}

	// past the last one the cycle wraps to the first
	if( !selectThisObject )
		selectThisObject = first;
	if( !selectThisObject )
		return NULL;

	// a worker riding in a tunnel or a transport is selected by what carries it
	if( selectThisObject->getContainedBy() )
		selectThisObject = selectThisObject->getContainedBy();

	deselectAllDrawables();
	GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

	//New group or add to group? Passed in value is true if we are creating a new group.
	teamMsg->appendBooleanArgument( TRUE );
	teamMsg->appendObjectIDArgument( selectThisObject->getID() );

	// the camera stays where the player has it
	selectDrawable( selectThisObject->getDrawable() );
	return selectThisObject;
}

// Shift on an idle button: every one the button's cycle would visit, as one new selection, across
// the map and with the camera left alone.  A worker riding in something brings what carries it.
Int InGameUI::selectAllIdleUnits( KindOfMaskType mustBeSet, KindOfMaskType mustBeClear )
{
	GameMessage *teamMsg = NULL;
	Int count = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !isIdleOfKinds( obj, mustBeSet, mustBeClear ) )
			continue;
		Object *pick = obj->getContainedBy() ? obj->getContainedBy() : obj;
		Drawable *draw = pick->getDrawable();
		if( draw == NULL || draw->isSelected() )
			continue;
		if( getMaxSelectCount() > 0 && getSelectCount() >= getMaxSelectCount() )
			break;
		if( teamMsg == NULL )
		{
			deselectAllDrawables();
			teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
			teamMsg->appendBooleanArgument( TRUE );
		}
		teamMsg->appendObjectIDArgument( pick->getID() );
		selectDrawable( draw );
		count++;
	}
	return count;
}

void InGameUI::resetIdleWorker( void )
{
	for(Int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		m_idleWorkers[i].clear();
	}

}

void InGameUI::recreateControlBar( void )
{
	//
	// deleteInstance() hands the block back to the pool without taking the window off the manager's
	// list and without touching its children, so the freed root stayed in m_windowList and the pool
	// then handed the same block to the root created two lines later.  The list was a ring of
	// half-freed windows after that, and the first winRepaint that reached one drew a push button
	// whose overlay image was whatever the block now held.  Out of a match nothing showed because
	// the bar is hidden and winRepaint skips hidden windows; in one it was the crash on changing
	// resolution.  winDestroy unlinks the whole tree and defers the free to the window manager's own
	// pass, which is also what keeps the pointers below readable until then.
	//
	// A window's id is the key of its full decorated name, and the bar's root is called
	// ControlBar.wnd:ControlBarParent - so a lookup on "ControlBar.wnd" matched nothing and the old
	// bar was never destroyed at all.  Every rebuild left its predecessor on the window list, drawn
	// and clickable at the size of the screen it was built for, and a resolution change makes two of
	// them in a row: the command bar stacked two and three deep over the battlefield.  Every root
	// with that id goes, not the first, so a run that already collected some is cleaned out.
	//
	// winDestroy rather than deleteInstance: deleteInstance hands the block back to the pool without
	// taking the window off the manager's list and without touching its children, so the freed root
	// stayed in m_windowList and the pool then handed the same block to the root created two lines
	// later.  The list was a ring of half-freed windows after that, and the first winRepaint that
	// reached one drew a push button whose overlay image was whatever the block now held.  winDestroy
	// unlinks the whole tree and defers the free to the window manager's own pass, which is also what
	// keeps the pointers below readable until then - and it relinks m_next into the destroy list, so
	// the walk has to take the next window before it destroys this one.
	//
	const NameKeyType controlBarRootID =
		TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:ControlBarParent" ) );

	GameWindow *nextWindow = NULL;
	for( GameWindow *window = TheWindowManager->winGetWindowList(); window; window = nextWindow )
	{
		nextWindow = window->winGetNext();
		if( window->winGetWindowId() == controlBarRootID )
			TheWindowManager->winDestroy( window );
	}

	// the money gadget goes with those windows, and the new one carries whatever text its .wnd
	// ships with until update() is told the amount it is showing is not the amount the player has
	m_lastMoneyDisplayed = -1;

	createControlBar();

	// the radar kept a pointer to the old bar's radar window, and every minimap click went through it
	if( TheRadar )
		TheRadar->bindRadarWindow();

	//
	// The bar is rebuilt from its windows rather than replaced.  Replacing it re-parsed
	// CommandButton.ini and CommandSet.ini and handed out new CommandButton addresses, while every
	// const CommandButton * already held by something in the running game - a production queue, a
	// hunt update, the partition manager, the academy - went on pointing at the freed ones.
	//
	if(TheControlBar)
	{
		TheControlBar->initWindows();

		//
		// The two things init() cannot do for itself: the shortcut strip belongs to one general, and
		// the scheme is that general's artwork.  Both were applied to windows that no longer exist.
		//
		if(TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		{
			Player *localPlayer = ThePlayerList->getLocalPlayer();
			TheControlBar->initSpecialPowershortcutBar(localPlayer);
			TheControlBar->setControlBarSchemeByPlayer(localPlayer);
		}
	}

}

//
// Everything a running match has on screen that is not the shell and not the command bar.  Each of
// these was stretched to the resolution it was created at and none of them is rebuilt by the shell
// going away, so after a mode change the diplomacy panel, the chat line and the replay controls
// were still wearing the old screen's geometry - drawn and clicked a proportion of a screen away
// from where they belong, or off the edge of a smaller one.  Diplomacy and chat are thrown away and
// come back the next time the player asks for them; the replay controls are put back here because
// nothing else builds them.
//
void InGameUI::notifyResolutionChange( void )
{
	recreateControlBar();

	ResetDiplomacy();
	ResetInGameChat();
	if( TheGameConsole )
		TheGameConsole->resetCheatWindow();

	if( m_replayWindow )
	{
		const Bool wasHidden = m_replayWindow->winIsHidden();

		TheWindowManager->winDestroy( m_replayWindow );
		createReplayControl();
		m_replayWindow->winHide( wasHidden );
	}

	RecreateQuitMenu();
}

void InGameUI::disableTooltipsUntil(UnsignedInt frameNum)
{
	if (frameNum > m_tooltipsDisabledUntil) 
		m_tooltipsDisabledUntil = frameNum;
}

void InGameUI::clearTooltipsDisabled()
{
	m_tooltipsDisabledUntil = 0;
}

Bool InGameUI::areTooltipsDisabled() const
{
	return (TheGameLogic->getFrame() < m_tooltipsDisabledUntil);
}


WindowMsgHandledType IdleWorkerSystem( GameWindow *window, UnsignedInt msg, 
																				WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{
		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{	
			// if we're givin the opportunity to take the keyboard focus we must say we don't want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = FALSE;
		}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			static NameKeyType buttonSelectID = NAMEKEY( "IdleWorker.wnd:ButtonSelectNextIdleWorker" );
			if (control && control->winGetWindowId() == buttonSelectID)
			{
				// the same as the control bar's worker button: the worker key's cycle, Shift for every idle worker
				KindOfMaskType mustBeSet, mustBeClear;
				InGameUI::idleUnitKinds( GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER, &mustBeSet, &mustBeClear );
				if( TheKeyboard->isShift() )
					TheInGameUI->selectAllIdleUnits( mustBeSet, mustBeClear );
				else
					TheInGameUI->selectNextIdleWorker();
			}
			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}


