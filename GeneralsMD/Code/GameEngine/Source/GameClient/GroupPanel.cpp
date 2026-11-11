// The group panel (GroupPanel.h), as the owner picked it from the hud-artist's mockups on 2026-11-09
// (experiments/group-panel/mockups: list A2+, tray A2).
//
// On the left, the production tray's own body against the screen's left edge, one cell for every
// hotkey squad the local player has made, top to bottom in key order: the squad's most numerous unit
// as its picture in the build buttons' blue rim, a keycap with the squad's key over the picture's top
// left corner (pressed and lit while the squad is the one the tray is up for), "xN" in its bottom
// right, a soft "+N" in its top right for the units still being built for it or walking to it, and
// the squad's health in the power bar's colours along the cell's foot.  A squad whose units shot or
// were hit in the last few seconds has its picture blink red inside the rim, a translucent fill.
//
// On the right, while a squad picked by its number is still the selection, a tray between the
// superweapons and the general's powers: the powers tray's own stepped body at 1.2 (four buttons a
// row, flush right, a short last row stepping in at the foot) holding one build button for every
// unit any of the player's factories offers, whose units come out into that squad.  Over it a long
// ledge (<S>ProBuildLedge) ending in a point on the left, the point being the fold button
// (<S>ProBuildFold, folded <S>ProBuildUnfold); the ledge's right end carries the squad's keycap and
// "xN".  Folded, the ledge alone stays.  A piece the sheet does not have yet is drawn as a flat fill.
//
// Thread: LOGIC-THREAD.md.  Everything here that reads the logic runs at the render's sync point:
// GroupPanel_draw from InGameUI::postDraw, which W3DInGameUI::recordDraw runs before the overlap
// window opens with the drawing recorded for the render to replay, and the clicks on the input path,
// which runs at the sync point too.  The build buttons are windows whose state is set there and only
// drawn inside the window.  Squad members are read through Squad::getObjectIDs and findObjectByID,
// never getLiveObjects or getAllObjects: those prune the logic's list, and doing it on one machine
// only would make the squads differ between machines.  The combat blink is the one part worked out
// inside the window, from the wall clock alone, so a picture drawn again beside the tick keeps it
// moving.
//
// The stepped tray drawing, radarFrameTop and controlBarTop are copies of InGameUI.cpp's statics
// (drawHudTray and the rest).
// ponytail: copies, not shared; fold them into one header when InGameUI.cpp is free to change.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GroupPanel.h"

#include "Common/GameCommon.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Squad.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "Lib/Clock.h"

#include <string.h>
#include <vector>

namespace
{
	// Lengths in pixels at 1080 from experiments/group-panel/pieces.md, put through the screen's height
	// (and the list's shrink).  Where a piece of the sheet exists its own size wins over these.
	const Real LIST_CELL_W		= 77.0f;	///< a squad's cell: the tray's cell piece drawn 77 by 59
	const Real LIST_CELL_H		= 59.0f;
	const Real LIST_EDGE_W		= 8.0f;		///< the tray's edges, when the scheme has no tray
	const Real LIST_FOOT_H		= 7.0f;
	const Real LIST_CLEARANCE	= 6.0f;		///< kept clear over the production list's "+N" tab
	const Real PIC_X					= 3.0f;		///< the picture, cell-local 3,4 to 74,51
	const Real PIC_Y					= 4.0f;
	const Real PIC_W					= 71.0f;
	const Real PIC_H					= 47.0f;
	const Real KEYCAP_X				= 1.0f;		///< the key, picture-local
	const Real KEYCAP_Y				= 1.0f;
	const Real KEYCAP_W				= 20.0f;
	const Real KEYCAP_H				= 18.0f;
	const Real HEALTH_Y				= 52.0f;	///< the health bar, cell-local, as wide as the picture
	const Real HEALTH_H				= 4.0f;
	const Real KEY_POINTS			= 9.0f;		///< the key's digit, about 12 px
	const Real CHIP_POINTS		= 9.0f;		///< "xN" and "+N" on the pictures
	const Real LEDGE_POINTS		= 8.0f;		///< "xN" on the ledge, about 11 px

	const Real TRAY_SCALE			= 1.2f;		///< the powers tray's pieces at 1.2: 96x84 cells
	const Real CELL_W					= 96.0f;	///< when the scheme has no tray
	const Real CELL_H					= 84.0f;
	const Real BUTTON_INSET_X	= 3.0f;		///< a 90x72 build button inside its cell
	const Real BUTTON_INSET_Y	= 6.0f;
	const Real LEDGE_LENGTH		= 292.0f;	///< L, the run, R: 1628..1920 in the mockup
	const Real LEDGE_H				= 44.0f;	///< the plate's 40 rows and its 4-row shadow
	const Real LEDGE_L_W			= 92.0f;
	const Real LEDGE_R_W			= 64.0f;
	const Real LEDGE_BODY_Y		= 36.0f;	///< the body's top edge starts here, under the ledge's foot
	const Real FOLD_X					= 13.0f;	///< the fold button's face, L-local
	const Real FOLD_Y					= 6.0f;
	const Real FOLD_W					= 28.0f;
	const Real LEDGE_KEY_X		= 7.0f;		///< the key in R's well, R-local
	const Real LEDGE_KEY_Y		= 11.0f;
	const Real LEDGE_COUNT_X	= 31.0f;	///< "xN" left-aligned from here, R-local, centred on LEDGE_COUNT_Y
	const Real LEDGE_COUNT_Y	= 20.0f;

	const Real PRODUCTION_ROOM	= 39.0f;	///< the production list's row over the radar, at 800x600 (InGameUI.cpp)
	const Real PRODUCTION_LIFT	= 2.0f;
	const Real PRODUCTION_TAB_H	= 28.0f;	///< its "+N" tab, at 1080
	const Real FLOOR_GAP			= 8.0f;		///< kept clear over the general's powers, at 800x600

	enum
	{
		TRAY_COLUMNS			= 4,		///< the tray's width in buttons, widened only when its rows would not fit
		TRAY_MAX_COLUMNS	= 6,
		MAX_BUTTONS				= 40,		///< build buttons the tray holds
	};

	/// the list's room starts this far down the screen, under the six message lines in the top left
	const Real LIST_TOP_SHARE = 0.22f;

	/// how long a squad picked by its key keeps the tray open before its selection is checked against
	/// it: a squad a key made a moment ago reaches the logic a frame, or a network delay, later
	const UnsignedInt CHOSEN_GRACE = LOGICFRAMES_PER_SECOND;

	/// a squad is fighting while one of its units fired or was hit this recently
	const UnsignedInt COMBAT_WINDOW = 3 * LOGICFRAMES_PER_SECOND;
	/// a fighting squad's cell is red this long, then plain this long, on the wall clock, so it keeps
	/// blinking while paused
	const UnsignedInt COMBAT_BLINK_MS = 400;

	/// the rows in key order, 1 to 9 and then 0
	const Int KEY_ORDER[ NUM_HOTKEY_SQUADS ] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };

	// flat stand-ins until the sheet has the pieces, and what no piece draws
	const Color STEEL					= GameMakeColor( 44, 52, 60, 240 );
	const Color STEEL_LIT			= GameMakeColor( 140, 152, 166, 255 );
	const Color KEYLINE				= GameMakeColor( 5, 7, 9, 255 );
	const Color WELL					= GameMakeColor( 6, 8, 10, 255 );
	const Color KEY_FACE			= GameMakeColor( 20, 26, 32, 255 );
	const Color KEY_FACE_LIT	= GameMakeColor( 70, 92, 150, 255 );
	const Color FOLD_FACE			= GameMakeColor( 40, 120, 92, 255 );
	const Color FOLD_GLYPH		= GameMakeColor( 14, 40, 32, 255 );
	const Color KEY_DIGIT			= GameMakeColor( 255, 210, 60, 255 );	///< the bar's hotkey letters' yellow
	const Color KEY_DIGIT_LIT	= GameMakeColor( 8, 14, 30, 255 );		///< dark on the pressed, lit key
	const Color HEALTH_TRACK	= GameMakeColor( 4, 8, 10, 255 );
	const Color COMBAT_FILL		= GameMakeColor( 225, 20, 10, 120 );	///< over a fighting squad's picture, on its blink
	const Color CHIP_TEXT			= GameMakeColor( 255, 255, 255, 255 );
	const Color SOFT_TEXT			= GameMakeColor( 165, 172, 180, 255 );

	enum { STATE_UP, STATE_HOVER, STATE_PUSHED, STATE_COUNT };

	/// the side's pieces, looked up once a prefix
	struct Pieces
	{
		AsciiString prefix;
		const Image *edgeRight, *cornerTopRight, *cornerBottomRight;	///< the production tray's, <S>ProTray*
		const Image *key, *keyPressed;
		const Image *ledgeL, *ledgeT, *ledgeR;
		const Image *fold[ STATE_COUNT ], *unfold[ STATE_COUNT ];
		const Image *stepDown;	///< <S>ProTrayStepDown, the tray's short row
	};

	/// the powers tray's outline, as InGameUI.cpp's HudTray
	struct Tray
	{
		const Image *cell, *open, *foot, *uncovered, *cornerFoot, *cornerUncovered, *step;
		Int openW, footH, uncoveredH;
	};

	struct SquadRow
	{
		Int squad;
		const ThingTemplate *main;	///< the most numerous unit type, the squad's picture
		Int members;
		Real health, maxHealth;
		Int coming;									///< Player::countGroupReinforcements
		Bool fighting;							///< a member fired or was hit inside COMBAT_WINDOW
		IRegion2D box;							///< the row, set by the draw, taken by a click
	};

	typedef GroupPanelBuildable Buildable;

	Pieces s_pieces;
	SquadRow s_rows[ NUM_HOTKEY_SQUADS ];
	Int s_rowCount = 0;
	Int s_rowsBoxed = 0;					///< rows the last draw put a box on
	IRegion2D s_listBox;					///< the whole box, so a click between rows stays off the ground
	std::vector< Buildable > s_buildables;
	Bool s_gathered = FALSE;
	UnsignedInt s_gatherFrame = 0;
	Player *s_gatherPlayer = NULL;

	Int s_chosenSquad = -1;				///< the squad a number key picked, -1 once the selection left it
	UnsignedInt s_chosenFrame = 0;
	Int s_shownSquad = -1;				///< the squad the tray is up for
	Bool s_trayOpen = TRUE;				///< folded by the point; the match remembers it
	Bool s_ledgeShown = FALSE;
	IRegion2D s_ledgeBox;					///< the whole ledge, point included
	IRegion2D s_foldBox;					///< the point

	GameWindow *s_panel = NULL;
	GameWindow *s_buttons[ MAX_BUTTONS ];

	DisplayString *s_keyText[ NUM_HOTKEY_SQUADS ];
	DisplayString *s_sizeText[ NUM_HOTKEY_SQUADS ];
	DisplayString *s_comingText[ NUM_HOTKEY_SQUADS ];
	DisplayString *s_ledgeKeyText = NULL;
	DisplayString *s_ledgeSizeText = NULL;
}

//-------------------------------------------------------------------------------------------------
/** An 800x600 length at the command bar's scale, InGameUI.cpp's stripPixels. */
//-------------------------------------------------------------------------------------------------
static Int stripPixels( Real nominal )
{
	return REAL_TO_INT_CEIL( nominal * ControlBarHudPageScale() );
}

/// a length at 1080 on this screen, times `scale`; never under `least`
static Int px( Real at1080, Real scale = 1.0f, Int least = 1 )
{
	return max( REAL_TO_INT( at1080 * scale * TheDisplay->getHeight() / 1080.0f + 0.5f ), least );
}

/// an image's own width or height at 1080 on this screen, times `scale`, or `standIn` without one
static Int pieceW( const Image *image, Real standIn, Real scale = 1.0f )
{
	return px( image != NULL ? (Real)image->getImageWidth() : standIn, scale );
}

static Int pieceH( const Image *image, Real standIn, Real scale = 1.0f )
{
	return px( image != NULL ? (Real)image->getImageHeight() : standIn, scale );
}

//-------------------------------------------------------------------------------------------------
/** The side's prefix for the "Pro" pieces, the one the scheme's power tray was found under
	* (ControlBarProReforged.ini's PowerTrayImage, "AmericaProTray"): the bar's own pick for this
	* player's template.  With no tray, the side's name as InGameUI.cpp's hudCellStyle reads it. */
//-------------------------------------------------------------------------------------------------
static AsciiString sidePrefix( void )
{
	const Image *const *tray = TheControlBar->getControlBarSchemeManager()->getPowerTray();
	if( tray != NULL && tray[ ControlBarScheme::TRAY_CELL ] != NULL )
	{
		const AsciiString name = tray[ ControlBarScheme::TRAY_CELL ]->getName();
		const char *at = strstr( name.str(), "ProTray" );
		if( at != NULL && at > name.str() )
		{
			char prefix[ 64 ];
			const size_t length = min( (size_t)( at - name.str() ), sizeof( prefix ) - 1 );
			memcpy( prefix, name.str(), length );
			prefix[ length ] = 0;
			return AsciiString( prefix );
		}
	}

	const AsciiString side = ThePlayerList->getLocalPlayer()->getSide();
	if( side.startsWith( "China" ) || side.startsWith( "Boss" ) )
		return AsciiString( "China" );
	if( side.startsWith( "GLA" ) )
		return AsciiString( "Gla" );
	if( side.startsWith( "Turkiye" ) )
		return AsciiString( "Turkiye" );
	return AsciiString( "America" );
}

static const Image *findPiece( const AsciiString &prefix, const char *piece )
{
	AsciiString name;
	name.format( "%s%s", prefix.str(), piece );
	return TheMappedImageCollection->findImageByName( name );
}

static const Pieces &pieces( void )
{
	const AsciiString prefix = sidePrefix();
	if( s_pieces.prefix == prefix && s_pieces.prefix.isNotEmpty() )
		return s_pieces;

	static const char *const STATES[ STATE_COUNT ] = { "", "H", "P" };
	s_pieces.prefix = prefix;
	AsciiString name;
	s_pieces.edgeRight = findPiece( prefix, "ProTrayEdgeRight" );
	s_pieces.cornerTopRight = findPiece( prefix, "ProTrayCornerTopRight" );
	s_pieces.cornerBottomRight = findPiece( prefix, "ProTrayCornerBottomRight" );
	s_pieces.key = findPiece( prefix, "ProSquadKey" );
	s_pieces.keyPressed = findPiece( prefix, "ProSquadKeyP" );
	s_pieces.ledgeL = findPiece( prefix, "ProBuildLedgeL" );
	s_pieces.ledgeT = findPiece( prefix, "ProBuildLedgeT" );
	s_pieces.ledgeR = findPiece( prefix, "ProBuildLedgeR" );
	for( Int i = 0; i < STATE_COUNT; i++ )
	{
		name.format( "ProBuildFold%s", STATES[ i ] );
		s_pieces.fold[ i ] = findPiece( prefix, name.str() );
		name.format( "ProBuildUnfold%s", STATES[ i ] );
		s_pieces.unfold[ i ] = findPiece( prefix, name.str() );
	}
	s_pieces.stepDown = findPiece( prefix, "ProTrayStepDown" );
	return s_pieces;
}

//-------------------------------------------------------------------------------------------------
// Drawing helpers.  A missing piece is a flat fill, so the panel runs before its art lands.
//-------------------------------------------------------------------------------------------------
static void drawPiece( const Image *image, Int lx, Int ly, Int hx, Int hy )
{
	if( image != NULL && hx > lx && hy > ly )
		TheDisplay->drawImage( image, lx, ly, hx, hy );
}

static void drawSteel( const IRegion2D &r )
{
	const Int w = r.hi.x - r.lo.x, h = r.hi.y - r.lo.y;
	if( w <= 2 || h <= 2 )
		return;
	TheDisplay->drawFillRect( r.lo.x, r.lo.y, w, h, STEEL );
	TheDisplay->drawFillRect( r.lo.x + 1, r.lo.y + 1, w - 2, 1, STEEL_LIT );
	TheDisplay->drawOpenRect( r.lo.x, r.lo.y, w, h, 1.0f, KEYLINE );
}

static void drawWell( const IRegion2D &r )
{
	TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, WELL );
}

/// `draw` run where the picture is drawn: recorded as one call while the sync point records, so what
/// it works out from the wall clock is worked out again by every replay
static void atDrawTime( const std::function<void()> &draw )
{
	if( TheDisplay->isRecording() )
		TheDisplay->record( draw );
	else
		draw();
}


//-------------------------------------------------------------------------------------------------
/** InGameUI.cpp's drawHudTray: cell i is row i / columns, place i % columns in from the screen edge;
	* `openSide` +1 puts the open side on the right, `footSide` +1 puts row 0's outer edge below. */
//-------------------------------------------------------------------------------------------------
static void drawStepTray( const Tray &tray, const IRegion2D *cells, Int count, Int columns, Int openSide, Int footSide )
{
	if( tray.cell == NULL || count == 0 )
		return;

	const Int rows = ( count + columns - 1 ) / columns;
	auto rowLength = [ & ]( Int row ) { return row >= 0 && row < rows ? min( columns, count - row * columns ) : 0; };

	for( Int each = 0; each < count; each++ )
	{
		const IRegion2D &c = cells[ each ];
		const Int row = each / columns, place = each % columns;
		const Bool innermost = place == rowLength( row ) - 1;
		const Bool covered = place < rowLength( row + 1 );
		const Bool stepHere = innermost && row > 0 && place + 1 < rowLength( row - 1 );
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

		drawPiece( tray.cell, c.lo.x, c.lo.y, c.hi.x, c.hi.y );
		if( !covered )
			drawPiece( tray.uncovered, c.lo.x + ( openSide > 0 ? shorten : 0 ), farLo, c.hi.x - ( openSide > 0 ? 0 : shorten ), farHi );
		if( innermost )
		{
			drawPiece( tray.open, openLo, c.lo.y + ( footSide > 0 ? 0 : notch ), openHi, c.hi.y - ( footSide > 0 ? notch : 0 ) );
			if( stepHere )
				drawPiece( tray.step, openLo, stepLo, openHi, stepHi );
			if( !covered )
				drawPiece( tray.cornerUncovered, openLo, farLo, openHi, farHi );
		}
		if( row == 0 )
		{
			drawPiece( tray.foot, c.lo.x, footLo, c.hi.x, footHi );
			if( innermost )
				drawPiece( tray.cornerFoot, openLo, footLo, openHi, footHi );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Where one of the command bar's pieces starts on screen, or the screen's foot while the bar is
	* hidden (InGameUI.cpp's controlBarTop, for any piece). */
//-------------------------------------------------------------------------------------------------
static Int controlBarTop( Int panel )
{
	static NameKeyType controlBarKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
	GameWindow *bar = TheWindowManager->winGetWindowFromId( NULL, controlBarKey );
	if( bar == NULL || bar->winIsHidden() )
		return (Int)TheDisplay->getHeight();

	ICoord2D barPos;
	bar->winGetScreenPosition( &barPos.x, &barPos.y );
	return TheControlBar->getPanelRect( panel )->lo.y + barPos.y - TheControlBar->getPanelOrigin()->y;
}

/// the top of the radar's frame, InGameUI.cpp's radarFrameTop
static Int radarFrameTop( void )
{
	static NameKeyType radarKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:LeftHUD" );
	const Int barTop = controlBarTop( ControlBar::CB_PANEL_LEFT );
	const Image *const *frame = TheControlBar->getControlBarSchemeManager()->getRadarFrame();
	GameWindow *radar = TheWindowManager->winGetWindowFromId( NULL, radarKey );
	if( radar == NULL || radar->winIsHidden() || barTop >= (Int)TheDisplay->getHeight() )
		return barTop;
	if( frame == NULL )
		return TheControlBar->getControlBarSchemeManager()->radarArtTop( barTop );

	IRegion2D inner, outer;
	radar->winGetScreenPosition( &inner.lo.x, &inner.lo.y );
	radar->winGetSize( &inner.hi.x, &inner.hi.y );
	inner.hi.x += inner.lo.x;
	inner.hi.y += inner.lo.y;
	ControlBarSchemeManager::radarFrameRect( frame, inner, &outer );
	return outer.lo.y;
}

//-------------------------------------------------------------------------------------------------
/** How far down the right edge the tray may reach: the highest of the general's powers showing
	* (their stepped tray's frame stands a little above the buttons), the general's tab, and the
	* bar's right hand piece. */
//-------------------------------------------------------------------------------------------------
static Int rightTrayFloor( void )
{
	Int floor = controlBarTop( ControlBar::CB_PANEL_RIGHT );
	auto standOn = [ &floor ]( GameWindow *win )
	{
		if( win == NULL || win->winIsHidden() )
			return;
		ICoord2D at;
		win->winGetScreenPosition( &at.x, &at.y );
		floor = min( floor, at.y - stripPixels( FLOOR_GAP ) );
	};

	static NameKeyType generalKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonGeneral" );
	standOn( TheWindowManager->winGetWindowFromId( NULL, generalKey ) );

	// the shortcut layout is the player template's; its slot windows are found by name once a layout
	static AsciiString keyedLayout;
	static NameKeyType barKey = NAMEKEY_INVALID;
	static NameKeyType slotKeys[ MAX_SPECIAL_POWER_SHORTCUTS ];
	static Int slotCount = 0;
	const PlayerTemplate *pt = ThePlayerList->getLocalPlayer()->getPlayerTemplate();
	if( pt == NULL || pt->getSpecialPowerShortcutWinName().isEmpty() )
		return floor;
	if( keyedLayout != pt->getSpecialPowerShortcutWinName() )
	{
		keyedLayout = pt->getSpecialPowerShortcutWinName();
		AsciiString name;
		name.format( "%s:GenPowersShortcutBarParent", keyedLayout.str() );
		barKey = TheNameKeyGenerator->nameToKey( name );
		slotCount = min( pt->getSpecialPowerShortcutButtonCount(), (Int)MAX_SPECIAL_POWER_SHORTCUTS );
		for( Int i = 0; i < slotCount; i++ )
		{
			name.format( "%s:ButtonParent%d", keyedLayout.str(), i + 1 );
			slotKeys[ i ] = TheNameKeyGenerator->nameToKey( name );
		}
	}
	GameWindow *bar = TheWindowManager->winGetWindowFromId( NULL, barKey );
	if( bar == NULL || bar->winIsHidden() )
		return floor;
	for( Int i = 0; i < slotCount; i++ )
		standOn( TheWindowManager->winGetWindowFromId( bar, slotKeys[ i ] ) );
	return floor;
}

//-------------------------------------------------------------------------------------------------
/** A kept string set to `text` in Arial bold at `points`, laid out again only when either changed. */
//-------------------------------------------------------------------------------------------------
static DisplayString *keptString( DisplayString *&string, const UnicodeString &text, Int points )
{
	if( string == NULL )
		string = TheDisplayStringManager->newDisplayString();
	GameFont *font = TheFontLibrary->getFont( "Arial", max( points, 6 ), TRUE );
	if( string->getFont() != font )
		string->setFont( font );
	if( string->getText().compare( text ) != 0 )
		string->setText( text );
	return string;
}

static const Image *pictureOf( const ThingTemplate *thing )
{
	if( thing == NULL )
		return NULL;
	return thing->getButtonImage() ? thing->getButtonImage() : thing->getSelectedPortraitImage();
}

/// a keycap with the squad's key on it at `r`, pressed and lit for the squad the tray is up for
static void drawKeycap( const IRegion2D &r, DisplayString *digit, Bool pressed )
{
	const Pieces &p = pieces();
	const Image *cap = pressed && p.keyPressed != NULL ? p.keyPressed : p.key;
	if( cap != NULL )
		drawPiece( cap, r.lo.x, r.lo.y, r.hi.x, r.hi.y );
	else
	{
		TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, pressed ? KEY_FACE_LIT : KEY_FACE );
		TheDisplay->drawOpenRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, 1.0f, STEEL_LIT );
	}
	TheDisplay->flushBatch2D();

	// yellow with a black shadow at rest, as the bar's build-button letters; dark, no shadow and a pixel
	// right and down on the pressed key
	Int width = 0, height = 0;
	digit->getSize( &width, &height );
	const Int x = ( r.lo.x + r.hi.x - width ) / 2 + ( pressed ? 1 : 0 );
	const Int y = ( r.lo.y + r.hi.y - height ) / 2 + ( pressed ? 1 : 0 );
	if( pressed )
		digit->draw( x, y, KEY_DIGIT_LIT, KEY_DIGIT_LIT, 0, 0 );
	else
		digit->draw( x, y, KEY_DIGIT, GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
/** Each squad's live members, its main type and health, read from the logic's own list. */
//-------------------------------------------------------------------------------------------------
static void gatherSquads( Player *player )
{
	s_rowCount = 0;
	std::vector< std::pair< const ThingTemplate *, Int > > types;

	// a frame inside the combat window; a weapon that never fired says 0, a body never hit the future
	const UnsignedInt now = TheGameLogic->getFrame();
	auto recent = [ now ]( UnsignedInt at ) { return at != 0 && at <= now && now - at < COMBAT_WINDOW; };
	for( Int k = 0; k < NUM_HOTKEY_SQUADS; k++ )
	{
		const Int squad = KEY_ORDER[ k ];
		const Squad *members = player->getHotkeySquad( squad );
		if( members == NULL )
			continue;

		SquadRow row;
		row.squad = squad;
		row.main = NULL;
		row.members = 0;
		row.health = 0.0f;
		row.maxHealth = 0.0f;
		row.coming = 0;
		row.fighting = FALSE;
		row.box.lo.x = row.box.lo.y = row.box.hi.x = row.box.hi.y = 0;

		types.clear();
		const VecObjectID &ids = members->getObjectIDs();
		for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
		{
			const Object *obj = TheGameLogic->findObjectByID( *it );
			// one on its way from the factory is the row's "+N", not one of its members yet
			if( obj == NULL || obj->isEffectivelyDead() || obj->getReinforcementSquad() >= 0 )
				continue;

			row.members++;
			const ThingTemplate *thing = obj->getTemplate();
			size_t at = 0;
			while( at < types.size() && types[ at ].first != thing )
				at++;
			if( at == types.size() )
				types.push_back( std::make_pair( thing, 0 ) );
			types[ at ].second++;

			const BodyModuleInterface *body = obj->getBodyModule();
			if( body != NULL )
			{
				row.health += body->getHealth();
				row.maxHealth += body->getMaxHealth();
			}
			if( !row.fighting )
				row.fighting = recent( obj->getLastShotFiredFrame() ) || ( body != NULL && recent( body->getLastDamageTimestamp() ) );
		}
		// a squad with nobody left standing is not listed
		if( row.members == 0 )
			continue;

		// the most numerous type, the one that joined first on a tie
		Int best = 0;
		for( size_t t = 0; t < types.size(); t++ )
			if( types[ t ].second > best )
			{
				best = types[ t ].second;
				row.main = types[ t ].first;
			}
		row.coming = player->countGroupReinforcements( squad );
		s_rows[ s_rowCount++ ] = row;
	}
}

//-------------------------------------------------------------------------------------------------
Bool GroupPanel_isPlayerBuildButton( Int commandType, UnsignedInt options, Bool onBar )
{
	return commandType == GUI_COMMAND_UNIT_BUILD && onBar && !BitTest( options, SCRIPT_ONLY );
}

/// slot i of a factory's set as its bar has it: a unit build button the player can see there
static Bool GroupPanel_isPlayerBuildSlot( const CommandButton *button, Int i )
{
	return button != NULL && GroupPanel_isPlayerBuildButton( button->getCommandType(), button->getOptions(),
		TheControlBar->hasCommandWindow( i ) );
}

//-------------------------------------------------------------------------------------------------
/** One unit type per entry for every unit build button any of the player's finished factories
	* carries, with the best availability the command bar would give it on any of them. */
//-------------------------------------------------------------------------------------------------
static Bool isWorkingFactory( Object *obj )
{
	return !obj->isEffectivelyDead() && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
		&& !obj->testStatus( OBJECT_STATUS_SOLD ) && obj->getProductionUpdateInterface() != NULL;
}

static void gatherBuildable( Object *obj, void *userData )
{
	std::vector< Buildable > *list = (std::vector< Buildable > *)userData;
	if( !isWorkingFactory( obj ) )
		return;
	const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
	if( set == NULL )
		return;

	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( !GroupPanel_isPlayerBuildSlot( button, i ) )
			continue;
		const ThingTemplate *thing = button->getThingTemplate();
		if( thing == NULL )
			continue;
		const CommandAvailability now = TheControlBar->getButtonAvailability( button, obj );
		if( now == COMMAND_HIDDEN )
			continue;

		size_t at = 0;
		while( at < list->size() && (*list)[ at ].thing != thing )
			at++;
		if( at == list->size() )
		{
			const Buildable added = { thing, button, now };
			list->push_back( added );
		}
		else if( ControlBar::commandAvailabilityRank( now ) > ControlBar::commandAvailabilityRank( (*list)[ at ].best ) )
			(*list)[ at ].best = now;
	}
}

void GroupPanel_gatherBuildable( Player *player, std::vector< GroupPanelBuildable > &out )
{
	out.clear();
	if( player != NULL )
		player->iterateObjects( gatherBuildable, &out );
}

/// the command bar's own reading of an availability (ControlBar::updateContextCommand)
void GroupPanel_showAvailability( GameWindow *button, CommandAvailability availability )
{
	button->winClearStatus( WIN_STATUS_NOT_READY );
	button->winClearStatus( WIN_STATUS_ALWAYS_COLOR );
	switch( availability )
	{
		case COMMAND_AVAILABLE:
		case COMMAND_ACTIVE:
			button->winEnable( TRUE );
			break;
		case COMMAND_NOT_READY:
			button->winEnable( FALSE );
			button->winSetStatus( WIN_STATUS_NOT_READY );
			break;
		case COMMAND_CANT_AFFORD:
			button->winEnable( FALSE );
			button->winSetStatus( WIN_STATUS_ALWAYS_COLOR );
			break;
		default:
			button->winEnable( FALSE );
			break;
	}
}

struct FreeSlots
{
	const ThingTemplate *thing;
	UnsignedInt free;
};

/// the room left in the queues of the factories that can take `thing` now
static void countFreeSlots( Object *obj, void *userData )
{
	FreeSlots *slots = (FreeSlots *)userData;
	if( !isWorkingFactory( obj ) )
		return;
	const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
	if( set == NULL )
		return;

	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( !GroupPanel_isPlayerBuildSlot( button, i ) )
			continue;
		const ThingTemplate *thing = button->getThingTemplate();
		if( thing != slots->thing )
			continue;
		if( TheControlBar->getButtonAvailability( button, obj ) != COMMAND_AVAILABLE )
			return;
		const ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
		const UnsignedInt cap = getQueueCap( pu );
		if( pu->getProductionCount() < cap )
			slots->free += cap - pu->getProductionCount();
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/** A click on a build button: as many MSG_QUEUE_UNIT_FOR_GROUP as the factory buttons' shift and
	* ctrl ask for, cut to what the bank pays for and the queues hold, so a ctrl+shift click does not
	* send a hundred orders the logic would refuse.  The logic picks the factory for each. */
//-------------------------------------------------------------------------------------------------
void GroupPanel_queueUnit( const ThingTemplate *thing, Int squad )
{
	Player *player = ThePlayerList->getLocalPlayer();
	FreeSlots slots = { thing, 0 };
	player->iterateObjects( countFreeSlots, &slots );

	Int wanted = min( getBuildBatchCount(), (Int)slots.free );
	const Int cost = thing->calcCostToBuild( player );
	if( cost > 0 )
		wanted = min( wanted, (Int)( player->getMoney()->countMoney() / (UnsignedInt)cost ) );

	for( Int each = 0; each < wanted; each++ )
	{
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_QUEUE_UNIT_FOR_GROUP );
		msg->appendIntegerArgument( thing->getTemplateID() );
		msg->appendIntegerArgument( squad );
	}
}

//-------------------------------------------------------------------------------------------------
// The tray's windows: a parent over the tray's body that takes the clicks between the buttons and
// hears the buttons' presses, and MAX_BUTTONS push buttons in it drawn as the command bar draws its
// own (the cameo, the build border, greyed when the bar would grey it) with the bar's build tooltip.
// They are made on first use and again whenever the window manager has destroyed them.
//-------------------------------------------------------------------------------------------------
static Int panelId( void )
{
	static const Int id = (Int)TheNameKeyGenerator->nameToKey( "GroupPanel:BuildTray" );
	return id;
}

static Bool panelAlive( void )
{
	return s_panel != NULL && TheWindowManager->winGetWindowFromId( NULL, panelId() ) == s_panel;
}

static void drawNothing( GameWindow *window, WinInstanceData *instData )
{
}

static void buildTooltip( GameWindow *window, WinInstanceData *instData, UnsignedInt mouse )
{
	TheControlBar->showBuildTooltipLayout( window );
}

static WindowMsgHandledType panelSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GBM_SELECTED )
		return GameWinDefaultSystem( window, msg, mData1, mData2 );

	const CommandButton *command = (const CommandButton *)GadgetButtonGetData( (GameWindow *)mData1 );
	if( command != NULL && command->getThingTemplate() != NULL && isValidHotkeySquadIndex( s_shownSquad ) )
		GroupPanel_queueUnit( command->getThingTemplate(), s_shownSquad );
	return MSG_HANDLED;
}

static GameWindow *panelWindow( void )
{
	if( panelAlive() )
		return s_panel;

	s_panel = TheWindowManager->winCreate( NULL, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN, 0, 0, 1, 1, panelSystem );
	if( s_panel == NULL )
		return NULL;
	s_panel->winSetWindowId( panelId() );
	s_panel->winSetDrawFunc( drawNothing );
	s_panel->winSetInputFunc( GameWinBlockInput );

	for( Int i = 0; i < MAX_BUTTONS; i++ )
	{
		WinInstanceData inst;
		inst.init();
		BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
		GameWindow *button = TheWindowManager->gogoGadgetPushButton( s_panel,
			WIN_STATUS_ENABLED | WIN_STATUS_IMAGE | WIN_STATUS_USE_OVERLAY_STATES | WIN_STATUS_HIDDEN,
			0, 0, 1, 1, &inst, NULL, TRUE );
		s_buttons[ i ] = button;
		if( button == NULL )
			continue;
		button->winSetTooltipFunc( buildTooltip );
		GadgetButtonSetData( button, NULL );
		GadgetButtonSetBorder( button, TheControlBar->getBuildBorderColor() );
		GadgetButtonSetAltSound( button, "GUICommandBarClick" );
	}
	return s_panel;
}

static void hideTrayWindows( void )
{
	if( panelAlive() )
		s_panel->winHide( TRUE );
}

static void hidePanel( void )
{
	s_shownSquad = -1;
	s_ledgeShown = FALSE;
	hideTrayWindows();
}

//-------------------------------------------------------------------------------------------------
/** Whether the selection is still the squad a key picked: nothing selected that is not in it. */
//-------------------------------------------------------------------------------------------------
static Bool chosenSquadSelected( Player *player )
{
	if( !isValidHotkeySquadIndex( s_chosenSquad ) )
		return FALSE;
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if( selected == NULL || selected->empty() )
		return FALSE;
	// ponytail: a fixed second of grace for a squad still on its way to the logic; the pending list
	// SelectionTranslator keeps would be exact if a longer network delay ever closes the tray early
	if( TheGameLogic->getFrame() - s_chosenFrame < CHOSEN_GRACE )
		return TRUE;

	const Squad *squad = player->getHotkeySquad( s_chosenSquad );
	if( squad == NULL )
		return FALSE;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( obj == NULL || !squad->isOnSquad( obj ) )
			return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The squad list (mockup A2+): the production tray's body against the left edge, one cell a squad
	* going down, centred in the room between the message lines and the production list over the
	* radar; ten cells that do not fit shrink together.
	*
	*   +-----+--------------------+-+
	*   | key |              "+N"  | |
	*   +-----+     picture        | |   the tray's open edge on the right
	*   |                    "xN"  | |
	*   +--------------------------+ |
	*   |========= health =========| |
	*   +--------------------------+-+
	*
	* A fighting squad's picture takes a translucent red fill inside its rim, on for COMBAT_BLINK_MS and
	* off for as long, timed by the wall clock where the picture is drawn (atDrawTime). */
//-------------------------------------------------------------------------------------------------
static void drawList( void )
{
	s_listBox.lo.x = s_listBox.lo.y = s_listBox.hi.x = s_listBox.hi.y = 0;
	if( s_rowCount == 0 )
		return;

	const Pieces &p = pieces();
	const Int screenH = (Int)TheDisplay->getHeight();
	const Int top = REAL_TO_INT( screenH * LIST_TOP_SHARE );
	const Int bottom = radarFrameTop() - stripPixels( PRODUCTION_LIFT ) - stripPixels( PRODUCTION_ROOM )
										 - px( PRODUCTION_TAB_H ) - px( LIST_CLEARANCE );
	const Int room = max( bottom - top, 1 );

	// the production tray's pieces (InGameUI.cpp hudCellStyle) laid going down, as the superweapons'
	// are: the powers tray's top edge over the first cell, its foot under the last, the open side right
	const Image *const *tray = TheControlBar->getControlBarSchemeManager()->getPowerTray();
	Tray body = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0 };
	if( tray != NULL && tray[ ControlBarScheme::TRAY_CELL ] != NULL )
	{
		const Tray pieced =
		{
			tray[ ControlBarScheme::TRAY_CELL ],
			p.edgeRight != NULL ? p.edgeRight : tray[ ControlBarScheme::TRAY_EDGE_LEFT ],
			tray[ ControlBarScheme::TRAY_EDGE_TOP ],
			tray[ ControlBarScheme::TRAY_EDGE_BOTTOM ],
			p.cornerTopRight != NULL ? p.cornerTopRight : tray[ ControlBarScheme::TRAY_CORNER_TL ],
			p.cornerBottomRight != NULL ? p.cornerBottomRight : tray[ ControlBarScheme::TRAY_CORNER_BL ],
			NULL,	// one column: no row steps in
			0, 0, 0
		};
		body = pieced;
	}

	// the cell at `s`, and the edges as thick against it as the tray's are against its cell piece
	Int cellW = 0, cellH = 0;
	auto measure = [ & ]( Real s )
	{
		cellW = px( LIST_CELL_W, s );
		cellH = px( LIST_CELL_H, s );
		if( body.cell != NULL )
		{
			const Real kx = (Real)cellW / body.cell->getImageWidth(), ky = (Real)cellH / body.cell->getImageHeight();
			body.openW = body.open != NULL ? REAL_TO_INT_CEIL( body.open->getImageWidth() * kx ) : 0;
			body.footH = body.foot != NULL ? REAL_TO_INT_CEIL( body.foot->getImageHeight() * ky ) : 0;
			body.uncoveredH = body.uncovered != NULL ? REAL_TO_INT_CEIL( body.uncovered->getImageHeight() * ky ) : 0;
		}
		else
		{
			body.openW = px( LIST_EDGE_W, s );
			body.footH = body.uncoveredH = px( LIST_FOOT_H, s );
		}
		return body.footH + s_rowCount * cellH + body.uncoveredH;
	};
	Real s = 1.0f;
	while( measure( s ) > room && s > 0.41f )
		s = max( s * 0.95f, 0.4f );

	const Int height = measure( s );
	s_listBox.lo.x = 0;
	s_listBox.hi.x = cellW + body.openW;
	s_listBox.lo.y = top + max( ( room - height ) / 2, 0 );
	s_listBox.hi.y = s_listBox.lo.y + height;

	IRegion2D cells[ NUM_HOTKEY_SQUADS ];
	for( Int r = 0; r < s_rowCount; r++ )
	{
		IRegion2D &c = cells[ r ];
		c.lo.x = 0;
		c.hi.x = cellW;
		c.lo.y = s_listBox.lo.y + body.footH + r * cellH;
		c.hi.y = c.lo.y + cellH;
		s_rows[ r ].box.lo.x = 0;
		s_rows[ r ].box.hi.x = s_listBox.hi.x;
		s_rows[ r ].box.lo.y = c.lo.y;
		s_rows[ r ].box.hi.y = c.hi.y;
	}

	TheDisplay->beginBatch2D();
	if( body.cell != NULL )
		drawStepTray( body, cells, s_rowCount, 1, 1, -1 );
	else
		drawSteel( s_listBox );
	TheDisplay->endBatch2D();

	const Int keyPoints = max( REAL_TO_INT( KEY_POINTS * s * screenH / 1080.0f ), 7 );
	const Int chipPoints = max( REAL_TO_INT( CHIP_POINTS * s * screenH / 1080.0f ), 7 );
	const Int keyW = pieceW( p.key, KEYCAP_W, s ), keyH = pieceH( p.key, KEYCAP_H, s );
	const Color rim = TheControlBar->getBuildBorderColor();

	for( Int r = 0; r < s_rowCount; r++ )
	{
		const SquadRow &row = s_rows[ r ];
		const IRegion2D &c = cells[ r ];
		IRegion2D picture;
		picture.lo.x = c.lo.x + px( PIC_X, s );
		picture.lo.y = c.lo.y + px( PIC_Y, s );
		picture.hi.x = picture.lo.x + px( PIC_W, s );
		picture.hi.y = picture.lo.y + px( PIC_H, s );
		IRegion2D health;
		health.lo.x = picture.lo.x;
		health.hi.x = picture.hi.x;
		health.lo.y = c.lo.y + px( HEALTH_Y, s );
		health.hi.y = min( health.lo.y + px( HEALTH_H, s, 3 ), c.hi.y );
		IRegion2D key;
		key.lo.x = picture.lo.x + px( KEYCAP_X, s );
		key.lo.y = picture.lo.y + px( KEYCAP_Y, s );
		key.hi.x = key.lo.x + keyW;
		key.hi.y = key.lo.y + keyH;

		TheDisplay->beginBatch2D();
		const Image *image = pictureOf( row.main );
		if( image != NULL )
			drawPiece( image, picture.lo.x, picture.lo.y, picture.hi.x, picture.hi.y );
		else
			TheDisplay->drawFillRect( picture.lo.x, picture.lo.y, picture.hi.x - picture.lo.x, picture.hi.y - picture.lo.y, HEALTH_TRACK );
		TheDisplay->endBatch2D();

		// a fighting squad's picture blinks red inside its blue rim, under the key and the counts; the
		// steel round it and the health bar stay as they are
		if( row.fighting )
		{
			IRegion2D at = picture;
			at.lo.x++;
			at.lo.y++;
			at.hi.x--;
			at.hi.y--;
			atDrawTime( [ at ]()
			{
				if( ( Clock_Milliseconds() / COMBAT_BLINK_MS ) % 2 == 0 )
					TheDisplay->drawFillRect( at.lo.x, at.lo.y, at.hi.x - at.lo.x, at.hi.y - at.lo.y, COMBAT_FILL );
			} );
		}

		TheDisplay->beginBatch2D();
		TheDisplay->drawOpenRect( picture.lo.x, picture.lo.y, picture.hi.x - picture.lo.x, picture.hi.y - picture.lo.y, 1.0f, rim );

		// the health bar: the power bar's colours on a black track, its top row lit
		TheDisplay->drawFillRect( health.lo.x, health.lo.y, health.hi.x - health.lo.x, health.hi.y - health.lo.y, HEALTH_TRACK );
		if( row.maxHealth > 0.0f )
		{
			const Real share = max( min( row.health / row.maxHealth, 1.0f ), 0.0f );
			const Int innerW = health.hi.x - health.lo.x - 2, innerH = health.hi.y - health.lo.y - 2;
			const Int filled = REAL_TO_INT_CEIL( innerW * share );
			const Bool high = share > 0.5f, middle = share > 0.25f;
			const Color fill = high ? GameMakeColor( 16, 228, 2, 255 ) : ( middle ? GameMakeColor( 235, 200, 30, 255 ) : GameMakeColor( 230, 40, 30, 255 ) );
			const Color lit = high ? GameMakeColor( 152, 255, 104, 255 ) : ( middle ? GameMakeColor( 255, 240, 120, 255 ) : GameMakeColor( 255, 120, 100, 255 ) );
			if( filled > 0 && innerH > 0 )
			{
				TheDisplay->drawFillRect( health.lo.x + 1, health.lo.y + 1, filled, innerH, fill );
				if( innerH > 1 )
					TheDisplay->drawFillRect( health.lo.x + 1, health.lo.y + 1, filled, 1, lit );
			}
		}
		TheDisplay->endBatch2D();

		UnicodeString text;
		text.format( u"%d", row.squad );
		drawKeycap( key, keptString( s_keyText[ row.squad ], text, keyPoints ), row.squad == s_shownSquad );
		text.format( u"x%d", row.members );
		HudReadout_draw( keptString( s_sizeText[ row.squad ], text, chipPoints ), picture, HUD_READOUT_BOTTOM_RIGHT, CHIP_TEXT );
		if( row.coming > 0 )
		{
			text.format( u"+%d", row.coming );
			HudReadout_draw( keptString( s_comingText[ row.squad ], text, chipPoints ), picture, HUD_READOUT_TOP_RIGHT, SOFT_TEXT );
		}
	}
	s_rowsBoxed = s_rowCount;
}

//-------------------------------------------------------------------------------------------------
/** The ledge over the tray (mockup A2, tray C's ledge), a 3-slice flush with the screen's edge: L holds
	* the point, where the fold button's face sits, the run stretches, and R's well carries the squad's
	* keycap and "xN". */
//-------------------------------------------------------------------------------------------------
static void drawLedge( Int top, const SquadRow *row )
{
	const Pieces &p = pieces();
	const Int right = (Int)TheDisplay->getWidth();
	const Int height = pieceH( p.ledgeT, LEDGE_H );
	const Int length = px( LEDGE_LENGTH );
	const Int leftW = pieceW( p.ledgeL, LEDGE_L_W );
	const Int rightW = pieceW( p.ledgeR, LEDGE_R_W );
	const Int foldSize = pieceW( s_trayOpen ? p.fold[ STATE_UP ] : p.unfold[ STATE_UP ], FOLD_W );

	s_ledgeBox.lo.x = right - length;
	s_ledgeBox.hi.x = right;
	s_ledgeBox.lo.y = top;
	s_ledgeBox.hi.y = top + height;
	s_foldBox.lo.x = s_ledgeBox.lo.x + px( FOLD_X );
	s_foldBox.lo.y = top + px( FOLD_Y );
	s_foldBox.hi.x = s_foldBox.lo.x + foldSize;
	s_foldBox.hi.y = s_foldBox.lo.y + foldSize;
	s_ledgeShown = TRUE;

	// the point's state: lit under the pointer, pushed while the left button holds it
	const MouseIO *mouse = TheMouse->getMouseStatus();
	const Bool over = mouse->pos.x >= s_foldBox.lo.x && mouse->pos.x < s_foldBox.hi.x
		&& mouse->pos.y >= s_foldBox.lo.y && mouse->pos.y < s_foldBox.hi.y;
	const Int state = over ? ( mouse->leftState != MBS_Up ? STATE_PUSHED : STATE_HOVER ) : STATE_UP;
	const Image *const *point = s_trayOpen ? p.fold : p.unfold;
	const Image *button = point[ state ] != NULL ? point[ state ] : point[ STATE_UP ];

	TheDisplay->beginBatch2D();
	const Int runLo = s_ledgeBox.lo.x + leftW, runHi = right - rightW;
	if( p.ledgeL != NULL && p.ledgeT != NULL && p.ledgeR != NULL )
	{
		drawPiece( p.ledgeL, s_ledgeBox.lo.x, top, runLo, top + height );
		drawPiece( p.ledgeT, runLo, top, runHi, top + height );
		drawPiece( p.ledgeR, runHi, top, right, top + height );
	}
	else
	{
		IRegion2D plate = s_ledgeBox;
		plate.hi.y = top + px( LEDGE_BODY_Y + 4.0f );	// the plate's 40 rows, no shadow
		drawSteel( plate );
		IRegion2D well = { { runHi + px( 4.0f ), top + px( 6.0f ) }, { runHi + px( 58.0f ), top + px( 34.0f ) } };
		drawWell( well );
	}
	if( button != NULL )
		drawPiece( button, s_foldBox.lo.x, s_foldBox.lo.y, s_foldBox.hi.x, s_foldBox.hi.y );
	else
	{
		// a chevron on the point: right to fold the tray toward the edge, left to bring it out
		TheDisplay->drawFillRect( s_foldBox.lo.x, s_foldBox.lo.y, foldSize, foldSize, FOLD_FACE );
		TheDisplay->drawOpenRect( s_foldBox.lo.x, s_foldBox.lo.y, foldSize, foldSize, 1.0f, KEYLINE );
		const Int cx = ( s_foldBox.lo.x + s_foldBox.hi.x ) / 2, cy = ( s_foldBox.lo.y + s_foldBox.hi.y ) / 2;
		const Int half = max( foldSize / 6, 2 );
		const Int tip = s_trayOpen ? cx + half : cx - half, back = s_trayOpen ? cx - half : cx + half;
		TheDisplay->drawLine( back, cy - 2 * half, tip, cy, 2.0f, FOLD_GLYPH );
		TheDisplay->drawLine( tip, cy, back, cy + 2 * half, 2.0f, FOLD_GLYPH );
	}
	TheDisplay->endBatch2D();

	// R's well: the squad's key, pressed (the tray is always the chosen squad's), and its size
	if( row == NULL )
		return;
	IRegion2D key;
	key.lo.x = runHi + px( LEDGE_KEY_X );
	key.lo.y = top + px( LEDGE_KEY_Y );
	key.hi.x = key.lo.x + pieceW( p.key, KEYCAP_W );
	key.hi.y = key.lo.y + pieceH( p.key, KEYCAP_H );
	UnicodeString text;
	text.format( u"%d", row->squad );
	drawKeycap( key, keptString( s_ledgeKeyText, text, max( REAL_TO_INT( KEY_POINTS * TheDisplay->getHeight() / 1080.0f ), 7 ) ), TRUE );

	text.format( u"x%d", row->members );
	DisplayString *size = keptString( s_ledgeSizeText, text, max( REAL_TO_INT( LEDGE_POINTS * TheDisplay->getHeight() / 1080.0f ), 7 ) );
	Int sizeW = 0, sizeH = 0;
	size->getSize( &sizeW, &sizeH );
	size->draw( runHi + px( LEDGE_COUNT_X ), top + px( LEDGE_COUNT_Y ) - sizeH / 2, CHIP_TEXT, GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
/** The tray (mockup A2): the powers tray's stepped body at 1.2 under the ledge, four buttons a row
	* flush right, a short last row stepping in at the foot; folded, the ledge alone. */
//-------------------------------------------------------------------------------------------------
static void drawTray( Int top )
{
	s_ledgeShown = FALSE;
	if( !isValidHotkeySquadIndex( s_shownSquad ) || s_buildables.empty() )
	{
		hideTrayWindows();
		return;
	}

	const SquadRow *row = NULL;
	for( Int r = 0; r < s_rowCount; r++ )
		if( s_rows[ r ].squad == s_shownSquad )
			row = &s_rows[ r ];

	if( !s_trayOpen )
	{
		hideTrayWindows();
		drawLedge( top, row );
		return;
	}
	GameWindow *panel = panelWindow();
	if( panel == NULL )
		return;

	// the body: the scheme's powers tray, laid as the superweapons' (foot on top, open side left) with
	// the side's StepDown where the short last row steps in
	const Pieces &p = pieces();
	const Image *const *tray = TheControlBar->getControlBarSchemeManager()->getPowerTray();
	Tray body = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0 };
	if( tray != NULL && tray[ ControlBarScheme::TRAY_CELL ] != NULL )
	{
		const Tray pieced =
		{
			tray[ ControlBarScheme::TRAY_CELL ],
			tray[ ControlBarScheme::TRAY_EDGE_LEFT ],
			tray[ ControlBarScheme::TRAY_EDGE_TOP ],
			tray[ ControlBarScheme::TRAY_EDGE_BOTTOM ],
			tray[ ControlBarScheme::TRAY_CORNER_TL ],
			tray[ ControlBarScheme::TRAY_CORNER_BL ],
			p.stepDown != NULL ? p.stepDown : tray[ ControlBarScheme::TRAY_STEP ],
			0, 0, 0
		};
		body = pieced;
	}

	const Int right = (Int)TheDisplay->getWidth();
	const Int shown = min( (Int)s_buildables.size(), (Int)MAX_BUTTONS );
	const Int floor = rightTrayFloor();
	const Int bodyTop = top + px( LEDGE_BODY_Y );	// the body's top edge, under the ledge's foot

	Int barW = 0, barH = 0;
	static NameKeyType barButtonKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonCommand01" );
	GameWindow *barButton = TheWindowManager->winGetWindowFromId( NULL, barButtonKey );
	if( barButton != NULL )
		barButton->winGetSize( &barW, &barH );

	// rows of four; wider when they would reach the powers, then smaller
	Int columns = min( (Int)TRAY_COLUMNS, shown );
	Real scale = TRAY_SCALE;
	Int cellW = 0, cellH = 0;
	auto measure = [ & ]()
	{
		cellW = pieceW( body.cell, CELL_W / TRAY_SCALE, scale );
		cellH = pieceH( body.cell, CELL_H / TRAY_SCALE, scale );
		body.openW = body.open ? pieceW( body.open, 0, scale ) : 0;
		body.footH = body.foot ? pieceH( body.foot, 0, scale ) : 0;
		body.uncoveredH = body.uncovered ? pieceH( body.uncovered, 0, scale ) : 0;
		const Int rows = ( shown + columns - 1 ) / columns;
		return bodyTop + body.footH + rows * cellH + body.uncoveredH;
	};
	while( measure() > floor && columns < min( (Int)TRAY_MAX_COLUMNS, shown ) )
		columns++;
	while( measure() > floor && scale > TRAY_SCALE * 0.6f )
		scale *= 0.92f;
	const Int bodyBottom = measure();

	// cells in the tray's order (place counted in from the edge); a row reads left to right, flush right
	IRegion2D cells[ MAX_BUTTONS ];
	Int cellOf[ MAX_BUTTONS ];
	for( Int i = 0; i < shown; i++ )
	{
		const Int rowIndex = i / columns, inRow = i % columns;
		const Int rowLength = min( columns, shown - rowIndex * columns );
		const Int each = rowIndex * columns + ( rowLength - 1 - inRow );
		cellOf[ i ] = each;
		cells[ each ].hi.x = right - ( rowLength - 1 - inRow ) * cellW;
		cells[ each ].lo.x = cells[ each ].hi.x - cellW;
		cells[ each ].lo.y = bodyTop + body.footH + rowIndex * cellH;
		cells[ each ].hi.y = cells[ each ].lo.y + cellH;
	}

	TheDisplay->beginBatch2D();
	if( body.cell != NULL )
		drawStepTray( body, cells, shown, columns, -1, -1 );
	else
	{
		IRegion2D plate = { { right - columns * cellW, bodyTop }, { right, bodyBottom } };
		drawSteel( plate );
	}
	TheDisplay->endBatch2D();
	// the ledge over the body's top edge
	drawLedge( top, row );

	// the parent covers the body, so a click between the buttons stays off the ground
	// ponytail: the notch the short row leaves blocks the ground too; a second parent if anyone clicks there
	const Int bodyLeft = right - columns * cellW - body.openW;
	panel->winSetPosition( bodyLeft, bodyTop );
	panel->winSetSize( right - bodyLeft, bodyBottom - bodyTop );

	// a button inside its cell as the mockup has it, no bigger than the bar's own
	Int buttonW = cellW - 2 * px( BUTTON_INSET_X, scale / TRAY_SCALE );
	Int buttonH = cellH - 2 * px( BUTTON_INSET_Y, scale / TRAY_SCALE );
	if( barW > 0 && barH > 0 )
	{
		buttonW = min( buttonW, barW );
		buttonH = min( buttonH, barH );
	}
	for( Int i = 0; i < MAX_BUTTONS; i++ )
	{
		GameWindow *button = s_buttons[ i ];
		if( button == NULL )
			continue;
		if( i >= shown )
		{
			button->winHide( TRUE );
			continue;
		}

		const Buildable &entry = s_buildables[ i ];
		const IRegion2D &cell = cells[ cellOf[ i ] ];
		button->winSetPosition( ( cell.lo.x + cell.hi.x - buttonW ) / 2 - bodyLeft, ( cell.lo.y + cell.hi.y - buttonH ) / 2 - bodyTop );
		button->winSetSize( buttonW, buttonH );
		if( GadgetButtonGetData( button ) != (void *)entry.button )
		{
			GadgetButtonSetEnabledImage( button, entry.button->getButtonImage() );
			GadgetButtonSetData( button, (void *)entry.button );
		}

		GroupPanel_showAvailability( button, entry.best );
		button->winHide( FALSE );
	}
	panel->winHide( FALSE );
}

//-------------------------------------------------------------------------------------------------
void GroupPanel_draw( Int rightTop )
{
	s_rowsBoxed = 0;
	s_listBox.lo.x = s_listBox.lo.y = s_listBox.hi.x = s_listBox.hi.y = 0;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
	{
		s_chosenSquad = -1;
		s_gathered = FALSE;
		s_trayOpen = TRUE;		// a new match starts with the tray out
		hidePanel();
		return;
	}

	// a watcher, a replay and a hidden command bar (cinematics) get neither half
	Player *player = ThePlayerList->getLocalPlayer();
	if( player == NULL || !player->isPlayerActive() || TheGameLogic->isInReplayGame() || TheInGameUI->isQuitMenuVisible()
			|| controlBarTop( ControlBar::CB_PANEL_LEFT ) >= (Int)TheDisplay->getHeight() )
	{
		hidePanel();
		return;
	}

	// once a logic frame: nothing read here changes in between
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( !s_gathered || s_gatherFrame != frame || s_gatherPlayer != player )
	{
		gatherSquads( player );
		s_buildables.clear();
		player->iterateObjects( gatherBuildable, &s_buildables );
		s_gathered = TRUE;
		s_gatherFrame = frame;
		s_gatherPlayer = player;
	}

	// the tray closes for good once the selection leaves the squad, so a box round the same units
	// does not open it again
	s_shownSquad = chosenSquadSelected( player ) ? s_chosenSquad : -1;
	if( s_shownSquad < 0 )
		s_chosenSquad = -1;

	drawList();
	drawTray( rightTop );
}

//-------------------------------------------------------------------------------------------------
Bool GroupPanel_click( const ICoord2D *mouse, Bool cancel )
{
	auto inside = [ mouse ]( const IRegion2D &box )
	{
		return mouse->x >= box.lo.x && mouse->x < box.hi.x && mouse->y >= box.lo.y && mouse->y < box.hi.y;
	};

	// the ledge's point folds the tray away or brings it out; the rest of the ledge is only steel
	if( s_ledgeShown && inside( s_ledgeBox ) )
	{
		if( inside( s_foldBox ) && ( !cancel || TheKeyboard->isCtrl() ) )
			s_trayOpen = !s_trayOpen;
		return TRUE;
	}

	for( Int r = 0; r < s_rowsBoxed; r++ )
	{
		if( !inside( s_rows[ r ].box ) )
			continue;

		// a right press is the list's and does nothing, so no order lands on the ground under it; while
		// the promotion screen is up the number keys are its columns, and a row must not buy a science
		if( ( cancel && !TheKeyboard->isCtrl() ) || TheControlBar->isPurchaseScienceVisible() )
			return TRUE;

		// the number key's own message: SelectionTranslator selects the squad, and a second press inside
		// its double-tap time takes the camera there, exactly as the key does
		TheMessageStream->appendMessage( (GameMessage::Type)( GameMessage::MSG_META_SELECT_TEAM0 + s_rows[ r ].squad ) );
		return TRUE;
	}

	// between the rows, the box is still the list's
	return s_rowsBoxed > 0 && inside( s_listBox );
}

//-------------------------------------------------------------------------------------------------
void GroupPanel_squadChosen( Int squad )
{
	s_chosenSquad = isValidHotkeySquadIndex( squad ) ? squad : -1;
	s_chosenFrame = TheGameLogic->getFrame();
}
