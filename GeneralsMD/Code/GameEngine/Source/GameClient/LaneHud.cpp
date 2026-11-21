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

// FILE: LaneHud.cpp //////////////////////////////////////////////////////////////////////////////
//
// The Lane War HUD, the owner's card hand of 2026-11-21.  Where its boxes stand is LaneHudLayout.h;
// what is in them is read from TheLaneWarDirector, the players and their money.  The HUD carries no
// words: money, income, prices and countdowns are numbers, everything else a picture, a colour or a
// shape, and a name shows only in the tooltip under the pointer.
//
// Thread: LOGIC-THREAD.md.  LaneHud_draw runs from InGameUI::postDraw and LaneHud_preDraw from
// InGameUI::preDraw, both at the render's sync point, and the presses come on the input path, which
// runs there too; so the director, the objects and the money are read here as GroupPanel reads its
// squads.  Nothing here writes the logic: a purchase, a sale, a tier, an upgrade, a move and an
// ability switch go out as MSG_LANEWAR_ORDER and LaneWarDirector checks each again (whyNotPlace is
// the same test the ghost is tinted by).  The one thing written outside the HUD is a client flag on
// each plot unit's drawable, so the board draws no health bar or pips over its pieces.
//
// Art: hud-artist's v4 pieces (experiments/proofs/2026-11-20/lane-hud-art/README-v4.md in the
// wrapper), and the look every Direct Strike screen shares, come from DirectStrikeTheme: the pieces by
// name, the flat stand-ins where one is missing, the type and the shapes.  A piece not in the data yet
// is a flat fill, so the art drops in without a code change.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/LaneHud.h"
#include "GameClient/LaneHudLayout.h"
#include "GameClient/DirectStrikeTheme.h"

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LaneWarEndScreen.h"
#include "GameClient/Line2D.h"
#include "GameClient/Mouse.h"
#include "GameClient/UIStyle.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LaneWarDirector.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/BodyModule.h"

#include <vector>

using namespace DirectStrikeTheme;

namespace
{
	// the HUD's own colours; the flat stand-ins and the shared ones are DirectStrikeTheme's
	const Color DIM							= GameMakeColor( 140, 140, 140, 255 );	///< an image drawn through it: about the README's luminance times 0.55
	const Color LANE_GROUND			= GameMakeColor( 128, 112, 78, 255 );
	const Color LANE_EDGE				= GameMakeColor( 58, 52, 40, 255 );
	const Color PLOT_FILL				= GameMakeColor( 40, 46, 40, 255 );
	const Color VIEW_BOX				= GameMakeColor( 240, 240, 240, 230 );
	const Color BLOCKED					= GameMakeColor( 240, 60, 45, 255 );
	const Color INCOME					= GameMakeColor( 110, 230, 110, 255 );
	const Color AMBER						= GameMakeColor( 240, 170, 50, 255 );
	const Color DOWN						= GameMakeColor( 70, 70, 70, 255 );

	enum { ABILITY_ART_ROWS = 6 };		///< rows the AbilitiesPanel piece's glass holds
	enum { NO_CARD = -1, HERO_CARD = -2 };
	enum { LANEHUD_STRIP_JETS = 64 };		///< jets in the air the strip map marks, six seats' squadrons with room
	Int s_jetsShown = 0;										///< the last draw's airborne jets, for LaneHud_describe

	LaneHudLayout s_layout;
	Bool s_showing = FALSE;
	Bool s_seated = FALSE;
	Int s_team = 0, s_seat = 0;
	Player *s_player = NULL;
	Int s_tab = 0;													///< the tier the hand shows, 0 to 2
	Bool s_upgradesOpen = FALSE;
	Bool s_abilitiesOpen = FALSE;
	Bool s_scoreboard = FALSE;							///< Tab held
	Bool s_homed = FALSE;										///< the camera went to the plot when the match opened

	// what the last draw showed, for the press that comes after it
	const ThingTemplate *s_cardThing[ LANEHUD_CARDS ];
	const ThingTemplate *s_hero = NULL;
	Int s_upgradeCount = 0;									///< cards in the open deck, the income tier first
	Int s_upgradeIndex[ LANEHUD_UPGRADE_CELLS ];	///< each card's entry of LaneWar_upgrades, -1 for the income tier
	struct AbilityRow
	{
		const ThingTemplate *thing;
		LaneWarAbility ability;
	};
	AbilityRow s_rows[ LANEHUD_ABILITY_ROWS ];
	Int s_rowCount = 0;

	// held: a card armed (by a click, or riding the pointer while the button is down), a plot unit picked up
	const ThingTemplate *s_armed = NULL;
	Bool s_dragging = FALSE;								///< the card's press is still down
	Int s_dragCard = NO_CARD;								///< the hand slot it came from, HERO_CARD for the hero
	LaneHudBox s_pressBox;									///< let go inside it, the press was a click: the card stays armed
	ObjectID s_moving = INVALID_ID;

	// the plot unit the pointer rests on, and its row of chips floating over it: kept while the pointer is in the row
	ObjectID s_clusterUnit = INVALID_ID;
	LaneHudBox s_clusterBox;						///< the row and the gap down to the unit
	LaneHudBox s_clusterRow;						///< the chips alone
	enum ChipKind { CHIP_RANK, CHIP_UPGRADE, CHIP_PASSENGER, CHIP_ROOM };
	struct Chip
	{
		Int kind;
		LaneHudBox box;
		Int value;												///< the upgrade's command set slot, the passenger's index
	};
	enum { CLUSTER_CHIPS = 16 };
	Chip s_chips[ CLUSTER_CHIPS ];
	Int s_chipCount = 0;

	// a passenger chip pressed and riding the pointer: let go on the hand, that passenger is sold
	ObjectID s_passengerOf = INVALID_ID;
	Int s_passenger = -1;

	// an infantry card held over a plot unit that carries men: in it, if it has room
	Int s_garrisonIndex = -1;
	Bool s_garrisonRoom = FALSE;
	Int s_moveGrid = 0, s_moveCol = 0, s_moveRow = 0;

	/// where the armed or picked up unit would go, worked out from the pointer at the sync point
	struct Target
	{
		Bool on;					///< the pointer is over the world with something to place
		const ThingTemplate *thing;
		Int grid, col, row, size;
		Bool legal;
		Coord3D at;				///< the block's middle, or the pointer's ground when far off the plot
		Bool onPlot;
	};
	Target s_target;
	DrawableID s_ghost = INVALID_DRAWABLE_ID;
	const ThingTemplate *s_ghostOf = NULL;

	// where the last draw put each seat's plot on the strip map, for the hover
	LaneHudBox s_stripPlot[ LANEWAR_TEAMS ][ LANEWAR_SEATS ];

	// the tooltip the draw found under the pointer, drawn last
	UnicodeString s_tip;
}

//-------------------------------------------------------------------------------------------------
// Small helpers
//-------------------------------------------------------------------------------------------------

/// a 1080 length at this screen's height, one pixel at least
static Int pxs( Real at1080 )
{
	return LaneHud_px( at1080, s_layout.scale );
}

static Bool inside( const LaneHudBox &b, const ICoord2D *p )
{
	return b.contains( p->x, p->y );
}

static const ICoord2D *mousePos( void )
{
	return &TheMouse->getMouseStatus()->pos;
}

static LaneHudBox grow( const LaneHudBox &b, Int by )
{
	return LaneHud_box( b.x - by, b.y - by, b.w + 2 * by, b.h + 2 * by );
}

static GameFont *smallFont( void ) { return hudFont( UIStyle::TEXT_SMALL ); }
static GameFont *bodyFont( void ) { return hudFont( UIStyle::TEXT_BODY ); }
static GameFont *priceFont( void ) { return hudFont( UIStyle::TEXT_STRONG ); }
static GameFont *bigFont( void ) { return hudFont( UIStyle::TEXT_TITLE ); }

static UnicodeString label( const char *key )
{
	return TheGameText->fetch( key );
}

static UnicodeString money( Int amount )
{
	UnicodeString text;
	text.format( u"$%s", UIStyle::grouped( amount ).str() );
	return text;
}

static UnicodeString clockText( Int seconds )
{
	UnicodeString text;
	text.format( u"%d:%02d", seconds / 60, seconds % 60 );
	return text;
}

/// the tooltip for this draw, when the pointer is over the box
static void tip( const LaneHudBox &b, const UnicodeString &text )
{
	if( !inside( b, mousePos() ) || text.isEmpty() )
		return;
	// a command button's label carries its hotkey as '&'
	s_tip.clear();
	for( const WideChar *c = text.str(); *c != 0; ++c )
		if( *c != u'&' )
			s_tip.concat( *c );
}

/// the side whose art the HUD wears: the seated player's, America's for a watcher
static Side hudSide( void )
{
	return sideOf( s_player != NULL ? s_player->getSide() : AsciiString( "America" ) );
}

//-------------------------------------------------------------------------------------------------
// Glyphs drawn in lines where a piece is missing
//-------------------------------------------------------------------------------------------------

static void line( Real x0, Real y0, Real x1, Real y1, const LaneHudBox &b, Color c, Real width = 2.0f )
{
	TheDisplay->drawLine( b.x + REAL_TO_INT( x0 * b.w ), b.y + REAL_TO_INT( y0 * b.h ), b.x + REAL_TO_INT( x1 * b.w ),
		b.y + REAL_TO_INT( y1 * b.h ), width, c );
}

/// the padlock: TabLock, or a body and a shackle
static void lockMark( const LaneHudBox &b )
{
	const Image *image = art( LANE_TAB_LOCK );
	if( image != NULL )
	{
		drawImage( image, b );
		return;
	}
	TheDisplay->drawFillRect( b.x, b.y + b.h / 2, b.w, b.h - b.h / 2, WHITE );
	TheDisplay->drawOpenRect( b.x + b.w / 4, b.y, b.w / 2, b.h / 2 + 1, 2.0f, WHITE );
}

/// the green check: Done, or two strokes
static void doneMark( const LaneHudBox &b )
{
	const Image *image = art( LANE_DONE );
	if( image != NULL )
	{
		drawImage( image, b );
		return;
	}
	line( 0.15f, 0.55f, 0.4f, 0.8f, b, LEGAL, 3.0f );
	line( 0.4f, 0.8f, 0.85f, 0.2f, b, LEGAL, 3.0f );
}

/// a jet: AirMark, or a flat arrowhead
static void airMark( const LaneHudBox &b )
{
	const Image *image = art( LANE_AIR_MARK );
	if( image != NULL )
	{
		drawImage( image, b );
		return;
	}
	line( 0.5f, 0.05f, 0.1f, 0.9f, b, WHITE );
	line( 0.5f, 0.05f, 0.9f, 0.9f, b, WHITE );
	line( 0.1f, 0.6f, 0.9f, 0.6f, b, WHITE );
}

static void boltGlyph( const LaneHudBox &b )
{
	line( 0.6f, 0.15f, 0.35f, 0.55f, b, PRICE, 3.0f );
	line( 0.35f, 0.55f, 0.62f, 0.5f, b, PRICE, 3.0f );
	line( 0.62f, 0.5f, 0.4f, 0.88f, b, PRICE, 3.0f );
}

static void incomeGlyph( const LaneHudBox &b )
{
	const Image *image = art( LANE_INCOME_ICON );
	if( image != NULL )
	{
		drawImage( image, b );
		return;
	}
	for( Int i = 0; i < 3; ++i )
		TheDisplay->drawFillRect( b.x + b.w / 8, b.y + b.h * ( 5 - i ) / 7, b.w * 3 / 8, b.h / 8, PRICE );
	line( 0.72f, 0.85f, 0.72f, 0.2f, b, INCOME, 3.0f );
	line( 0.72f, 0.2f, 0.56f, 0.4f, b, INCOME, 3.0f );
	line( 0.72f, 0.2f, 0.88f, 0.4f, b, INCOME, 3.0f );
}

static Color playerColor( Int team, Int seat )
{
	const Int index = TheLaneWarDirector->getSeatPlayer( team, seat );
	const Player *p = index >= 0 ? ThePlayerList->getNthPlayer( index ) : NULL;
	return p ? UIStyle::readable( p->getPlayerColor() ) : UIStyle::MUTED;
}

static Player *seatPlayer( Int team, Int seat )
{
	const Int index = TheLaneWarDirector->getSeatPlayer( team, seat );
	return index >= 0 ? ThePlayerList->getNthPlayer( index ) : NULL;
}

/// the team's colour, its first seated player's
static Color teamColor( Int team )
{
	for( Int s = 0; s < LANEWAR_SEATS; ++s )
		if( TheLaneWarDirector->getSeatPlayer( team, s ) >= 0 )
			return playerColor( team, s );
	return UIStyle::MUTED;
}

/// a seat's name and what it is paid, for a tooltip
static UnicodeString seatTip( Int team, Int seat )
{
	Player *p = seatPlayer( team, seat );
	if( p == NULL )
		return UnicodeString::TheEmptyString;
	UnicodeString text;
	text.format( u"%s  +%u", p->getPlayerDisplayName().str(), TheLaneWarDirector->getIncome( team, seat ) );
	return text;
}

static void sendOrder( Int kind, Int a, Int b = -1, Int c = -1, Int d = -1, Int e = -1 )
{
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_LANEWAR_ORDER );
	msg->appendIntegerArgument( kind );
	const Int args[] = { a, b, c, d, e };
	// a sale or a rank takes a fourth integer only for a passenger
	Int count = kind == LANEWAR_ORDER_UPGRADE ? 1 : kind == LANEWAR_ORDER_TIER ? 2 : kind == LANEWAR_ORDER_MOVE ? 5 : 3;
	if( kind == LANEWAR_ORDER_GARRISON || kind == LANEWAR_ORDER_UNIT_UPGRADE
			|| ( ( kind == LANEWAR_ORDER_SELL || kind == LANEWAR_ORDER_VETERANCY ) && d >= 0 ) )
		count = 4;
	for( Int i = 0; i < count; ++i )
		msg->appendIntegerArgument( args[ i ] );
}

static void dropHeld( void )
{
	s_armed = NULL;
	s_dragging = FALSE;
	s_dragCard = NO_CARD;
	s_moving = INVALID_ID;
	s_passengerOf = INVALID_ID;
	s_passenger = -1;
}

static Int localMoney( void )
{
	return s_player ? (Int)s_player->getMoney()->countMoney() : 0;
}

static Int techLevel( void )
{
	return TheLaneWarDirector->getTechLevel( s_team, s_seat );
}

//-------------------------------------------------------------------------------------------------
// The plot under the pointer
//-------------------------------------------------------------------------------------------------

/** The block of `size` slots of the player's grid the world point would put a unit on: its front
	* left slot, the inverse of LaneWar_slotPlace with the block centred on the point.  A point on the
	* grid snaps the block inside it.  FALSE when the grid does not exist. */
static Bool slotUnder( const Coord3D &world, Int grid, Int size, Int *col, Int *row, Bool *onGrid )
{
	Coord2D lo, hi;
	if( !TheLaneWarDirector->getGridBounds( s_team, s_seat, grid, &lo, &hi ) )
		return FALSE;
	const Real front = TheLaneWarDirector->towardEnemy( s_team );
	const Real frontX = front > 0.0f ? hi.x : lo.x;
	const Real leftY = front > 0.0f ? hi.y : lo.y;
	const Real slot = (Real)LANEWAR_SLOT_SIZE;
	*col = (Int)floor( ( leftY - world.y ) / ( front * slot ) - 0.5f * size + 0.5f );
	*row = (Int)floor( ( frontX - world.x ) / ( front * slot ) - 0.5f * size + 0.5f );
	*onGrid = world.x >= lo.x && world.x <= hi.x && world.y >= lo.y && world.y <= hi.y;
	if( *onGrid )
	{
		const Int cols = TheLaneWarDirector->getGridCols( s_team, s_seat, grid );
		const Int rows = TheLaneWarDirector->getGridRows( s_team, s_seat, grid );
		*col = max( 0, min( *col, cols - size ) );
		*row = max( 0, min( *row, rows - size ) );
	}
	return TRUE;
}

/// the player's plot unit under a world point; -1 for none
static Int plotUnitUnder( const Coord3D &world )
{
	for( Int grid = 0; grid < LANEWAR_GRIDS; ++grid )
	{
		Int col, row;
		Bool onGrid;
		if( slotUnder( world, grid, 1, &col, &row, &onGrid ) && onGrid )
		{
			const Int index = TheLaneWarDirector->plotUnitAt( s_player, grid, col, row );
			if( index >= 0 )
				return index;
		}
	}
	return -1;
}

/// the abilities panel as the last draw sized it
static LaneHudBox abilitiesPanel( void )
{
	return LaneHud_abilitiesPanel( s_layout, max( (Int)ABILITY_ART_ROWS, s_rowCount ) );
}

static Bool overHud( const ICoord2D *mouse )
{
	const LaneHudLayout &l = s_layout;
	if( inside( l.map, mouse ) || inside( l.waveBar, mouse ) || inside( l.countdown, mouse ) || inside( l.matchClock, mouse ) || inside( l.menu, mouse )
			|| inside( l.diplomacy, mouse ) )
		return TRUE;
	if( s_seated && ( inside( l.money, mouse ) || inside( l.yourWave, mouse ) ) )
		return TRUE;
	if( !s_seated )
		return FALSE;
	for( Int t = 0; t < LANEHUD_TIERS; ++t )
		if( inside( l.tiers[ t ], mouse ) )
			return TRUE;
	if( inside( l.hand, mouse ) || inside( l.deck, mouse ) || inside( l.abilitiesKey, mouse ) )
		return TRUE;
	if( s_upgradesOpen )
		for( Int c = 0; c < s_upgradeCount; ++c )
			if( inside( LaneHud_upgradeCell( l, s_upgradeCount, c ), mouse ) )
				return TRUE;
	if( s_abilitiesOpen && inside( abilitiesPanel(), mouse ) )
		return TRUE;
	return s_clusterUnit != INVALID_ID && s_chipCount > 0 && inside( s_clusterRow, mouse );
}

/// the four corners of a block of slots on screen, FALSE when one is off it
static Bool blockOnScreen( Int grid, Int col, Int row, Int size, ICoord2D corner[ 4 ] )
{
	Coord2D lo, hi;
	if( !TheLaneWarDirector->getGridBounds( s_team, s_seat, grid, &lo, &hi ) )
		return FALSE;
	const Real front = TheLaneWarDirector->towardEnemy( s_team );
	const Coord2D centre = LaneWar_slotPlace( lo.x, lo.y, hi.x, hi.y, front, col, row, size );
	const Real half = 0.5f * size * LANEWAR_SLOT_SIZE;
	const Real dx[ 4 ] = { -half, half, half, -half }, dy[ 4 ] = { -half, -half, half, half };
	for( Int i = 0; i < 4; ++i )
	{
		Coord3D w;
		w.x = centre.x + dx[ i ];
		w.y = centre.y + dy[ i ];
		w.z = TheTerrainLogic->getGroundHeight( w.x, w.y );
		if( !TheTacticalView->worldToScreen( &w, &corner[ i ] ) )
			return FALSE;
	}
	return TRUE;
}

static void outline( const ICoord2D corner[ 4 ], Color color )
{
	for( Int i = 0; i < 4; ++i )
	{
		const ICoord2D &a = corner[ i ], &b = corner[ ( i + 1 ) % 4 ];
		TheDisplay->drawLine( a.x, a.y, b.x, b.y, 2.0f, color );
	}
}

//-------------------------------------------------------------------------------------------------
/** Who sits where and whether the HUD is up: a lane map, a match, not the shell, not under the end
	* screen.  The local player seated on a plot gets the whole HUD and the command bar goes; a watcher
	* gets the top plate. */
//-------------------------------------------------------------------------------------------------
static Bool refreshShowing( void )
{
	s_showing = FALSE;
	s_seated = FALSE;
	s_player = NULL;
	if( TheLaneWarDirector == NULL || !TheLaneWarDirector->isActive() || !TheGameLogic->isInGame()
			|| TheGameLogic->isInShellGame() || TheGameLogic->getFrame() == 0 || LaneWarEndScreen_isShowing() )
		return FALSE;

	LaneHud_layout( (Int)TheDisplay->getWidth(), (Int)TheDisplay->getHeight(), &s_layout );
	s_player = ThePlayerList->getLocalPlayer();
	s_team = 0;
	s_seat = 0;
	s_seated = s_player != NULL && TheLaneWarDirector->seatOf( s_player, &s_team, &s_seat ) && !TheGameLogic->isInReplayGame();
	s_showing = TRUE;
	return TRUE;
}

/// the classic bar's parts stay hidden under the HUD: radar, command grid, money, General button, powers tray
static void keepClassicBarDown( void )
{
	static NameKeyType barKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
	GameWindow *bar = TheWindowManager->winGetWindowFromId( NULL, barKey );
	if( bar != NULL && !bar->winIsHidden() )
		HideControlBar( TRUE );
}

/// every plot unit's drawable shows no health bar, pips, rank or status icons: the board's pieces
static void hidePlotUnitIcons( void )
{
	const std::vector<LaneWarDirector::PlotUnit> &units = TheLaneWarDirector->getPlotUnits();
	for( size_t i = 0; i < units.size(); ++i )
	{
		Object *obj = TheGameLogic->findObjectByID( units[ i ].id );
		Drawable *draw = obj != NULL ? obj->getDrawable() : NULL;
		if( draw != NULL )
			draw->setIconUIHidden( TRUE );
	}
}

//-------------------------------------------------------------------------------------------------
// The top plate: money and income, the strip map, the wave clock
//-------------------------------------------------------------------------------------------------

/// the seat of a team whose wave goes out on that tick, -1 for none
static Int seatOnTick( Int team, Int tick )
{
	if( tick < 0 )
		return -1;
	Bool occupied[ LANEWAR_SEATS ];
	for( Int s = 0; s < LANEWAR_SEATS; ++s )
		occupied[ s ] = TheLaneWarDirector->getSeatPlayer( team, s ) >= 0;
	return LaneWar_seatForTick( occupied, (UnsignedInt)tick );
}

/// wall seconds to the next spawn tick, rounded up
static Int secondsToTick( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	const UnsignedInt next = TheLaneWarDirector->getNextTickFrame();
	return next > frame ? (Int)( ( next - frame + LOGICFRAMES_PER_SECOND - 1 ) / LOGICFRAMES_PER_SECOND ) : 0;
}

struct StripFrame
{
	Real x0, x1, y0, y1;	///< the world it shows
	Bool flip;						///< the player's own end lies towards +x: turned half round
};

static Bool stripFrame( StripFrame *f )
{
	Bool any = FALSE;
	f->x0 = f->x1 = 0.0f;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			Coord2D lo, hi;
			if( !TheLaneWarDirector->getPlotBounds( t, s, &lo, &hi ) )
				continue;
			f->x0 = any ? min( f->x0, lo.x ) : lo.x;
			f->x1 = any ? max( f->x1, hi.x ) : hi.x;
			f->y0 = any ? min( f->y0, lo.y ) : lo.y;
			f->y1 = any ? max( f->y1, hi.y ) : hi.y;
			any = TRUE;
		}
	if( !any || f->x1 <= f->x0 || f->y1 <= f->y0 )
		return FALSE;
	f->flip = TheLaneWarDirector->towardEnemy( s_team ) < 0.0f;
	return TRUE;
}

static ICoord2D toStrip( const StripFrame &f, Real x, Real y )
{
	const LaneHudBox &lane = s_layout.lane;
	Real u = ( x - f.x0 ) / ( f.x1 - f.x0 ), v = ( y - f.y0 ) / ( f.y1 - f.y0 );
	if( f.flip )
	{
		u = 1.0f - u;
		v = 1.0f - v;
	}
	ICoord2D p;
	p.x = lane.x + REAL_TO_INT( u * lane.w );
	p.y = lane.y + REAL_TO_INT( ( 1.0f - v ) * lane.h );
	return p;
}

static Coord3D fromStrip( const StripFrame &f, const ICoord2D *p )
{
	const LaneHudBox &lane = s_layout.lane;
	Real u = (Real)( p->x - lane.x ) / lane.w, v = 1.0f - (Real)( p->y - lane.y ) / lane.h;
	if( f.flip )
	{
		u = 1.0f - u;
		v = 1.0f - v;
	}
	Coord3D w;
	w.x = f.x0 + u * ( f.x1 - f.x0 );
	w.y = f.y0 + v * ( f.y1 - f.y0 );
	w.z = TheTerrainLogic->getGroundHeight( w.x, w.y );
	return w;
}

/// a rectangle of the world on the strip map, cut to the lane's box; where it went, empty when off it
static LaneHudBox stripRect( const StripFrame &f, Real lx, Real ly, Real hx, Real hy, Color fill, Color edge, Int least = 0 )
{
	const LaneHudBox &lane = s_layout.lane;
	ICoord2D a = toStrip( f, lx, ly ), b = toStrip( f, hx, hy );
	Int x0 = min( a.x, b.x ), x1 = max( a.x, b.x ), y0 = min( a.y, b.y ), y1 = max( a.y, b.y );
	if( x1 - x0 < least )
	{
		const Int c = ( x0 + x1 ) / 2;
		x0 = c - least / 2;
		x1 = x0 + least;
	}
	if( y1 - y0 < least )
	{
		const Int c = ( y0 + y1 ) / 2;
		y0 = c - least / 2;
		y1 = y0 + least;
	}
	x0 = max( x0, lane.x );
	y0 = max( y0, lane.y );
	x1 = min( x1, lane.x + lane.w );
	y1 = min( y1, lane.y + lane.h );
	if( x1 <= x0 || y1 <= y0 )
		return LaneHud_box( 0, 0, 0, 0 );
	if( fill != 0 )
		TheDisplay->drawFillRect( x0, y0, x1 - x0, y1 - y0, fill );
	if( edge != 0 )
		TheDisplay->drawOpenRect( x0, y0, x1 - x0, y1 - y0, 1.0f, edge );
	return LaneHud_box( x0, y0, x1 - x0, y1 - y0 );
}

/// a fortress or tower mark: its piece tinted, or a square
static void stripMark( const StripFrame &f, const Coord3D &at, Piece piece, Int size, Color tint )
{
	const ICoord2D p = toStrip( f, at.x, at.y );
	const LaneHudBox b = LaneHud_box( p.x - size / 2, p.y - size / 2, size, size );
	const Image *image = art( piece );
	if( image != NULL )
		drawImage( image, b, tint );
	else
	{
		TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, tint );
		TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, GameMakeColor( 0, 0, 0, 255 ) );
	}
}

static Real healthShare( const Object *obj )
{
	const BodyModuleInterface *body = obj ? obj->getBodyModule() : NULL;
	if( body == NULL || obj->isEffectivelyDead() || body->getMaxHealth() <= 0.0f )
		return 0.0f;
	return body->getHealth() / body->getMaxHealth();
}

static void drawStripMap( void )
{
	const LaneHudLayout &l = s_layout;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			s_stripPlot[ t ][ s ] = LaneHud_box( 0, 0, 0, 0 );
	StripFrame f;
	if( !stripFrame( &f ) )
		return;
	const LaneHudBox &lane = l.lane;
	TheDisplay->drawFillRect( lane.x, lane.y, lane.w, lane.h, LANE_GROUND );

	// the plots at the ends, each in its player's colour
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			Coord2D lo, hi;
			if( TheLaneWarDirector->getSeatPlayer( t, s ) >= 0 && TheLaneWarDirector->getPlotBounds( t, s, &lo, &hi ) )
				s_stripPlot[ t ][ s ] = stripRect( f, lo.x, lo.y, hi.x, hi.y, PLOT_FILL, UIStyle::withAlpha( playerColor( t, s ), 220 ) );
		}
	TheDisplay->drawOpenRect( lane.x, lane.y, lane.w, lane.h, 1.0f, LANE_EDGE );

	// the centre line, dashed: gold while nobody holds the middle, the holder's colour while one does
	const Coord3D *middle = TheLaneWarDirector->getMiddle();
	const Int holder = TheLaneWarDirector->getMiddleHolder();
	const Int mx = toStrip( f, middle->x, middle->y ).x;
	const Int dash = pxs( 3 );
	for( Int y = lane.y + pxs( 6 ); y < lane.y + lane.h; y += 2 * dash )
		TheDisplay->drawFillRect( mx - 1, y, holder < 0 ? 2 : 3, min( dash, lane.y + lane.h - y ), holder < 0 ? PRICE : teamColor( holder ) );

	// fortresses and towers in their team's colour; a tower down is grey where it last stood
	static Coord3D s_towerAt[ LANEWAR_TEAMS ][ 2 ];
	static Bool s_towerKnown[ LANEWAR_TEAMS ][ 2 ];
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		const Color tint = teamColor( t );
		for( Int k = 0; k < 2; ++k )
		{
			const ObjectID id = TheLaneWarDirector->getTower( t, k );
			const Object *tower = id != INVALID_ID ? TheGameLogic->findObjectByID( id ) : NULL;
			if( tower != NULL && !tower->isEffectivelyDead() )
			{
				s_towerAt[ t ][ k ] = *tower->getPosition();
				s_towerKnown[ t ][ k ] = TRUE;
				stripMark( f, s_towerAt[ t ][ k ], LANE_MAP_TOWER, pxs( 10 ), tint );
			}
			else if( id != INVALID_ID && s_towerKnown[ t ][ k ] )
				stripMark( f, s_towerAt[ t ][ k ], LANE_MAP_TOWER, pxs( 10 ), DOWN );
		}
		const Object *fortress = TheGameLogic->findObjectByID( TheLaneWarDirector->getFortress( t ) );
		if( fortress != NULL )
		{
			const Bool down = fortress->isEffectivelyDead() || TheLaneWarDirector->isFortressDown( t );
			stripMark( f, *fortress->getPosition(), LANE_MAP_FORTRESS, pxs( 16 ), down ? DOWN : tint );
		}
	}

	// the wave copies, a dot each in their owner's colour
	const Int dot = max( 2, pxs( 4 ) );
	for( Int i = 0; i < TheLaneWarDirector->getCopyCount(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( TheLaneWarDirector->getCopyID( i ) );
		if( obj == NULL || obj->isEffectivelyDead() )
			continue;
		const ICoord2D p = toStrip( f, obj->getPosition()->x, obj->getPosition()->y );
		if( !inside( lane, &p ) )
			continue;
		const Player *owner = obj->getControllingPlayer();
		TheDisplay->drawFillRect( p.x - dot / 2, p.y - dot / 2, dot, dot, owner ? UIStyle::readable( owner->getPlayerColor() ) : UIStyle::INK );
	}

	// the jets in the air, out on a strike or flying home, as small jet marks in their owner's colour;
	// a parked jet is off the map and nowhere on the strip
	Coord3D jetAt[ LANEHUD_STRIP_JETS ];
	Int jetOwner[ LANEHUD_STRIP_JETS ];
	const Int jets = TheLaneWarDirector->getAirborneJets( jetAt, jetOwner, LANEHUD_STRIP_JETS );
	const Int mark = pxs( 11 );
	for( Int j = 0; j < jets; ++j )
	{
		const ICoord2D p = toStrip( f, jetAt[ j ].x, jetAt[ j ].y );
		if( !inside( lane, &p ) )
			continue;
		const Player *owner = jetOwner[ j ] >= 0 ? ThePlayerList->getNthPlayer( jetOwner[ j ] ) : NULL;
		const Color tint = owner ? UIStyle::readable( owner->getPlayerColor() ) : UIStyle::INK;
		const LaneHudBox b = LaneHud_box( p.x - mark / 2, p.y - mark / 2, mark, mark );
		const Image *image = art( LANE_AIR_MARK );
		if( image != NULL )
			drawImage( image, b, tint );
		else
			TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, tint );
	}
	s_jetsShown = jets;

	// where the camera looks; View names its third and fourth corners wrongly, they come round the screen in order
	Coord3D corner[ 4 ];
	TheTacticalView->getScreenCornerWorldPointsAtZ( &corner[ 0 ], &corner[ 1 ], &corner[ 2 ], &corner[ 3 ], middle->z );
	IRegion2D clip;
	clip.lo.x = lane.x;
	clip.lo.y = lane.y;
	clip.hi.x = lane.x + lane.w;
	clip.hi.y = lane.y + lane.h;
	for( Int i = 0; i < 4; ++i )
	{
		const Coord3D &a = corner[ i ], &b = corner[ ( i + 1 ) % 4 ];
		ICoord2D pa = toStrip( f, a.x, a.y ), pb = toStrip( f, b.x, b.y ), ca, cb;
		if( ClipLine2D( &pa, &pb, &ca, &cb, &clip ) )
			TheDisplay->drawLine( ca.x, ca.y, cb.x, cb.y, 1.0f, VIEW_BOX );
	}

	// the fortresses' health along the well's top, each from its own end: ours green, amber when low; theirs amber
	const Int barY = lane.y + pxs( 2 ), barH = pxs( 3 ), most = lane.w / 2 - pxs( 4 );
	for( Int side = 0; side < 2; ++side )
	{
		const Int team = side == 0 ? s_team : 1 - s_team;
		const Real share = healthShare( TheGameLogic->findObjectByID( TheLaneWarDirector->getFortress( team ) ) );
		const Int w = REAL_TO_INT( most * share );
		const Color color = side == 0 && share > 0.6f ? INCOME : AMBER;
		TheDisplay->drawFillRect( side == 0 ? lane.x + pxs( 2 ) : lane.x + lane.w - pxs( 2 ) - w, barY, w, barH, color );
	}
}

/** A ring cut by angle where the YourRing piece is missing: the arc of mean radius r and the width
	* given, from `from` degrees clockwise from 12 o'clock through `sweep`, drawn in short thick strokes. */
static void arc( Int cx, Int cy, Real r, Real width, Real from, Real sweep, Color color )
{
	const Int steps = max( 1, REAL_TO_INT( fabs( sweep ) / 4.0f + 0.5f ) );
	const Real toRad = PI / 180.0f;
	Real a0 = from * toRad;
	for( Int i = 1; i <= steps; ++i )
	{
		const Real a1 = ( from + sweep * i / steps ) * toRad;
		TheDisplay->drawLine( cx + REAL_TO_INT( r * sinf( a0 ) ), cy - REAL_TO_INT( r * cosf( a0 ) ),
			cx + REAL_TO_INT( r * sinf( a1 ) ), cy - REAL_TO_INT( r * cosf( a1 ) ), width, color );
		a0 = a1;
	}
}

static void disc( Int cx, Int cy, Int r, Color color )
{
	for( Int dy = -r; dy <= r; ++dy )
	{
		const Int half = REAL_TO_INT( sqrtf( (Real)( r * r - dy * dy ) ) );
		TheDisplay->drawFillRect( cx - half, cy + dy, 2 * half + 1, 1, color );
	}
}

/// the money chip: the side's coin, the money, and what the next tick pays behind a rising chevron
static void drawMoney( void )
{
	const LaneHudBox &m = s_layout.money;
	chip( m, LANE_MONEY_CHIP );
	const LaneHudBox coin = LaneHud_box( m.x + pxs( 11 ), m.y + pxs( 19 ), pxs( 28 ), pxs( 28 ) );
	const Image *coinArt = art( LANE_COIN );
	if( coinArt != NULL )
		drawImage( coinArt, coin );
	else
	{
		disc( coin.x + coin.w / 2, coin.y + coin.h / 2, coin.w / 2, PRICE );
		drawText( UnicodeString( u"$" ), bodyFont(), coin.x + coin.w / 2, coin.y, coin.h, GameMakeColor( 90, 64, 10, 255 ), 0 );
	}
	const Int moneyW = drawText( UIStyle::grouped( localMoney() ), bigFont(), m.x + pxs( 45 ), m.y + pxs( 18 ), pxs( 28 ), WHITE );

	// the chevron is the plus; it keeps clear of a long figure
	const Int cx = max( m.x + pxs( 133 ), m.x + pxs( 45 ) + moneyW + pxs( 10 ) ), cy = m.y + pxs( 24 );
	const Color green = GameMakeColor( 110, 220, 90, 255 );
	TheDisplay->drawLine( cx - pxs( 4 ), cy + pxs( 2 ), cx, cy - pxs( 3 ), 2.0f, green );
	TheDisplay->drawLine( cx, cy - pxs( 3 ), cx + pxs( 4 ), cy + pxs( 2 ), 2.0f, green );
	UnicodeString pay;
	pay.format( u"%u", TheLaneWarDirector->getIncome( s_team, s_seat ) );
	drawText( pay, bodyFont(), cx + pxs( 7 ), m.y + pxs( 17 ), pxs( 16 ), green );

	UnicodeString level;
	level.format( label( "GUI:LaneIncome" ).str(), TheLaneWarDirector->getIncomeLevel( s_team, s_seat ), (Int)LANEWAR_INCOME_TIERS );
	tip( m, level );
}

/// the your-wave chip: a ring in the player's colour running down his wait, his dot, and the countdown
static void drawYourWave( void )
{
	const LaneHudBox &b = s_layout.yourWave;
	chip( b, LANE_YOUR_CHIP );
	const Int tick = (Int)TheLaneWarDirector->getTickCount();
	Int ahead = -1, seats = 0;
	for( Int k = 0; k < LANEWAR_SEATS; ++k )
	{
		if( TheLaneWarDirector->getSeatPlayer( s_team, k ) >= 0 )
			++seats;
		if( ahead < 0 && seatOnTick( s_team, tick + k ) == s_seat )
			ahead = k;
	}
	if( ahead < 0 || seats == 0 )
		return;
	const Int left = secondsToTick() + ahead * LANEWAR_TICK_SECONDS;
	const Real share = min( 1.0f, (Real)left / (Real)( seats * LANEWAR_TICK_SECONDS ) );
	const Int cx = b.x + pxs( 22 ), cy = b.y + pxs( 33 );
	const Color own = playerColor( s_team, s_seat );
	const Real r = 9.5f * s_layout.scale, width = max( 2.0f, 3.0f * s_layout.scale );
	const Color track = GameMakeColor( 34, 40, 48, 255 );
	// README-v6: the whole piece as the track, then the piece tinted and cut to the wedge still to wait
	const Image *ring = art( LANE_YOUR_RING );
	const LaneHudBox rb = LaneHud_box( cx - pxs( 12 ), cy - pxs( 12 ), pxs( 24 ), pxs( 24 ) );
	if( ring != NULL )
	{
		drawImage( ring, rb, track );
		if( share > 0.0f )
			TheDisplay->drawImageWedge( ring, rb.x, rb.y, rb.x + rb.w, rb.y + rb.h, 0.0f, 360.0f * share, own );
	}
	else
	{
		arc( cx, cy, r, width, 0.0f, 360.0f, track );
		if( share > 0.0f )
			arc( cx, cy, r, width, 0.0f, 360.0f * share, own );
	}
	disc( cx, cy, pxs( 4 ), own );
	drawText( clockText( left ), priceFont(), b.x + pxs( 40 ), cy - pxs( 10 ), pxs( 20 ), GameMakeColor( 242, 194, 48, 255 ) );
}

/** The wave bar under the lane: each team's next wave in its seat's colour, ours filling from the
	* left and theirs from the right, each (1 - f) of its half, meeting at the gold middle tick as the
	* waves go out; f is the share of the tick still to run.  The pointer over a half names that seat. */
static void drawWaveBar( void )
{
	const LaneHudBox &b = s_layout.waveBar;
	chip( b, LANE_BAR_CHIP );
	// the groove 4 in, the fills 1 inside its rim
	const Int in = pxs( 4 );
	const LaneHudBox groove = LaneHud_box( b.x + in, b.y + in, b.w - 2 * in, b.h - 2 * in );
	if( art( LANE_BAR_CHIP ) == NULL )
		TheDisplay->drawFillRect( groove.x, groove.y, groove.w, groove.h, GameMakeColor( 34, 40, 48, 255 ) );
	const LaneHudBox fills = LaneHud_box( groove.x + 1, groove.y + 1, groove.w - 2, groove.h - 2 );
	const Real f = min( 1.0f, (Real)secondsToTick() / (Real)LANEWAR_TICK_SECONDS );
	const Int tick = (Int)TheLaneWarDirector->getTickCount();
	const Int half = fills.w / 2;
	for( Int side = 0; side < 2; ++side )
	{
		const Int team = side == 0 ? s_team : 1 - s_team;
		const Int next = seatOnTick( team, tick );
		if( next < 0 )
			continue;
		const Int w = REAL_TO_INT( half * ( 1.0f - f ) );
		TheDisplay->drawFillRect( side == 0 ? fills.x : fills.x + fills.w - w, fills.y, w, fills.h, playerColor( team, next ) );
		tip( LaneHud_box( side == 0 ? b.x : b.x + b.w / 2, b.y, b.w / 2, b.h ), seatTip( team, next ) );
	}
	TheDisplay->drawFillRect( fills.x + half - 1, b.y + pxs( 3 ), 2, pxs( 8 ), GameMakeColor( 242, 194, 48, 255 ) );
}

/// the countdown to the next spawn tick, in its own chip under the bar
static void drawCountdown( void )
{
	const LaneHudBox &c = s_layout.countdown;
	chip( c, LANE_COUNT_CHIP );
	// the chip's 36 px is 24 at 720, short of the title face the readable floor grows there
	GameFont *font = bigFont()->height <= c.h - pxs( 6 ) ? bigFont() : priceFont();
	drawText( clockText( secondsToTick() ), font, c.x + c.w / 2, c.y, c.h, WHITE, 0 );
}

/// the match clock under the your-wave chip: wall time since the start, in the secondary ink
static void drawMatchClock( void )
{
	const LaneHudBox &b = s_layout.matchClock;
	chip( b, LANE_COUNT_CHIP );
	drawText( clockText( (Int)( TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND ) ), priceFont(), b.x + b.w / 2, b.y, b.h, TEXT_GREY, 0 );
	tip( b, label( "GUI:LaneMatchClock" ) );
}

/// the floating top: each chip stands on its own rect
static void drawTop( void )
{
	const LaneHudLayout &l = s_layout;
	if( s_seated )
		drawMoney();
	chip( l.map, LANE_LANE_CHIP );
	drawStripMap();
	if( s_seated )
		drawYourWave();
	drawWaveBar();
	drawCountdown();
	drawMatchClock();

	// the menu keys
	const struct { const LaneHudBox *box; Piece piece; const char *tipKey; } KEYS[ 2 ] =
	{
		{ &l.menu, SIDEBAR_KEY_MENU, "GUI:LaneMenu" },
		{ &l.diplomacy, SIDEBAR_KEY_DIPLOMACY, "GUI:LaneDiplomacy" },
	};
	for( Int k = 0; k < 2; ++k )
	{
		const LaneHudBox &b = *KEYS[ k ].box;
		const Bool flat = art( KEYS[ k ].piece ) == NULL;
		key( b, KEYS[ k ].piece, FALSE );
		if( flat && k == 0 )
			for( Int i = 0; i < 3; ++i )
				line( 0.38f, 0.3f + 0.2f * i, 0.62f, 0.3f + 0.2f * i, b, WHITE );
		else if( flat )
		{
			const LaneHudBox bubble = LaneHud_box( b.x + b.w * 3 / 8, b.y + b.h / 4, b.w / 4, b.h * 2 / 5 );
			TheDisplay->drawOpenRect( bubble.x, bubble.y, bubble.w, bubble.h, 2.0f, WHITE );
			line( 0.44f, 0.65f, 0.42f, 0.82f, b, WHITE );
		}
		tip( b, label( KEYS[ k ].tipKey ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** A seat's frame: its colour, what it is paid, what its plot is worth, and the plot itself small.
	* Held Tab lays these out for every other seat; the pointer over a plot on
	* the strip map raises one under the plate. */
//-------------------------------------------------------------------------------------------------
static void drawPlotFrame( const LaneHudBox &b, Int team, Int seat )
{
	const Image *frame = art( LANE_ENEMY_PLOT_FRAME );
	if( frame != NULL )
		drawImage( frame, b );
	else
		plate( b );
	const LaneHudBox glass = LaneHud_box( b.x + pxs( 5 ), b.y + pxs( 5 ), b.w - pxs( 10 ), b.h - pxs( 10 ) );
	TheDisplay->drawFillRect( glass.x, glass.y, pxs( 3 ), glass.h, playerColor( team, seat ) );

	Int army = 0;
	const std::vector<LaneWarDirector::PlotUnit> &units = TheLaneWarDirector->getPlotUnits();
	for( size_t i = 0; i < units.size(); ++i )
		if( units[ i ].team == team && units[ i ].seat == seat )
			army += units[ i ].cost;
	UnicodeString pay;
	pay.format( u"+%u", TheLaneWarDirector->getIncome( team, seat ) );
	drawText( pay, priceFont(), glass.x + pxs( 9 ), glass.y + pxs( 4 ), pxs( 24 ), INCOME );
	drawText( money( army ), bodyFont(), glass.x + pxs( 9 ), glass.y + pxs( 30 ), pxs( 20 ), PRICE );

	// the plot to scale, front at the bottom
	const Int cols = TheLaneWarDirector->getGridCols( team, seat, LANEWAR_GRID_GROUND );
	const Int rows = TheLaneWarDirector->getGridRows( team, seat, LANEWAR_GRID_GROUND );
	if( cols <= 0 || rows <= 0 )
		return;
	const Int roomW = glass.w / 2, roomH = glass.h - pxs( 6 );
	const Int cell = max( 1, min( pxs( 3 ), min( roomW / cols, roomH / rows ) ) );
	const Int right = glass.x + glass.w - pxs( 4 );
	const Int bottom = glass.y + glass.h - pxs( 3 );
	const Color owner = playerColor( team, seat );
	const LaneHudBox g = LaneHud_box( right - cols * cell, bottom - rows * cell, cols * cell, rows * cell );
	TheDisplay->drawFillRect( g.x, g.y, g.w, g.h, PLOT_FILL );
	TheDisplay->drawOpenRect( g.x - 1, g.y - 1, g.w + 2, g.h + 2, 1.0f, UIStyle::withAlpha( owner, 160 ) );
	for( size_t i = 0; i < units.size(); ++i )
	{
		const LaneWarDirector::PlotUnit &u = units[ i ];
		if( u.team != team || u.seat != seat )
			continue;
		const Int x = g.x + u.col * cell, y = g.y + g.h - ( u.row + u.size ) * cell;
		TheDisplay->drawFillRect( x, y, u.size * cell, u.size * cell, owner );
	}
}

static void drawScoreboard( void )
{
	const LaneHudLayout &l = s_layout;
	Int frame = 0;
	for( Int pass = 0; pass < 2; ++pass )
	{
		const Int team = pass == 0 ? 1 - s_team : s_team;
		for( Int seat = 0; seat < LANEWAR_SEATS && frame < LANEHUD_PLOT_FRAMES; ++seat )
		{
			if( TheLaneWarDirector->getSeatPlayer( team, seat ) < 0 || ( s_seated && team == s_team && seat == s_seat ) )
				continue;
			drawPlotFrame( l.plotFrame[ frame ], team, seat );
			tip( l.plotFrame[ frame ], seatTip( team, seat ) );
			++frame;
		}
	}
}

/// the pointer over a seat's plot on the strip map: its frame under the wave bar, beside the countdown
static void drawStripHover( void )
{
	const ICoord2D *mouse = mousePos();
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			if( s_stripPlot[ t ][ s ].w <= 0 || !inside( grow( s_stripPlot[ t ][ s ], 1 ), mouse ) )
				continue;
			const LaneHudBox &bar = s_layout.waveBar, &count = s_layout.countdown;
			const Int w = s_layout.plotFrame[ 0 ].w, h = s_layout.plotFrame[ 0 ].h;
			const Int x = mouse->x < count.x + count.w / 2 ? max( bar.x, count.x - pxs( 10 ) - w ) : count.x + count.w + pxs( 10 );
			drawPlotFrame( LaneHud_box( x, bar.y + bar.h + pxs( 8 ), w, h ), t, s );
			s_tip = seatTip( t, s );
			return;
		}
}

//-------------------------------------------------------------------------------------------------
// The hand: tier keys, the hero's card, the tier's cards, the upgrades deck, the abilities key
//-------------------------------------------------------------------------------------------------

/// a part of a card, in the art's 100x116 frame
static LaneHudBox cardPart( const LaneHudBox &card, Int x0, Int y0, Int x1, Int y1 )
{
	const Int lx = card.x + card.w * x0 / 100, ly = card.y + card.h * y0 / 116;
	return LaneHud_box( lx, ly, card.x + card.w * x1 / 100 - lx, card.y + card.h * y1 / 116 - ly );
}

/** A card: its frame (gold for the hero), the picture, and in the band under it the price, or the
	* check when it is done.  `dim` greys the picture; `lit` rings the card that is armed. */
static void drawCard( const LaneHudBox &c, const Image *picture, Int price, Bool dim, Bool done, Bool hero, Bool lit, Bool air )
{
	const Image *frame = art( hero ? LANE_CARD_HERO : LANE_CARD );
	const LaneHudBox well = cardPart( c, 5, 5, 95, 77 ), band = cardPart( c, 8, 84, 92, 108 );
	if( frame != NULL )
		drawImage( frame, c );
	else
	{
		TheDisplay->drawFillRect( c.x, c.y, c.w, c.h, CARD_FACE );
		TheDisplay->drawFillRect( well.x, well.y, well.w, well.h, WELL );
		TheDisplay->drawFillRect( band.x, band.y, band.w, band.h, WELL );
		TheDisplay->drawOpenRect( c.x, c.y, c.w, c.h, hero ? 2.0f : 1.0f, hero ? PRICE : PLATE_EDGE );
	}
	if( picture != NULL )
	{
		const LaneHudBox p = cardPart( c, 7, 7, 93, 76 );
		drawImage( picture, p, dim ? DIM : 0xFFFFFFFF );
	}
	if( air )
		airMark( cardPart( c, 8, 56, 26, 74 ) );
	if( done )
		doneMark( cardPart( c, 40, 86, 60, 106 ) );
	else if( price >= 0 )
	{
		// at 720 the readable face overruns a $1,500 band; the dollar goes first
		UnicodeString text = money( price );
		Int w = 0, h = 0;
		DisplayString *measure = pooled( text, priceFont() );
		if( measure != NULL )
			measure->getSize( &w, &h );
		if( w > band.w )
			text = UIStyle::grouped( price );
		drawText( text, priceFont(), band.x + band.w / 2, band.y, band.h, dim ? UIStyle::MUTED : PRICE, 0 );
	}
	if( lit )
		TheDisplay->drawOpenRect( c.x - 1, c.y - 1, c.w + 2, c.h + 2, 2.0f, UIStyle::ACTIVE );
}

/// the card standing under the pointer rises; nothing rises while a card, a unit or a passenger is held
static LaneHudBox risen( const LaneHudBox &c )
{
	const ICoord2D *mouse = mousePos();
	const Int rise = pxs( LANEHUD_CARD_RISE );
	if( s_dragging || s_moving != INVALID_ID || s_passengerOf != INVALID_ID || !inside( LaneHud_box( c.x, c.y - rise, c.w, c.h + rise ), mouse ) )
		return c;
	return LaneHud_box( c.x, c.y - rise, c.w, c.h );
}

/// the tier the hand shows: the one picked, or a locked one under the pointer, greyed
static Int shownTier( void )
{
	for( Int t = 0; t < LANEHUD_TIERS; ++t )
		if( t + 1 > techLevel() && inside( s_layout.tiers[ t ], mousePos() ) )
			return t;
	return s_tab;
}

static void drawTierKeys( void )
{
	const Int tech = techLevel(), cash = localMoney();
	for( Int t = 0; t < LANEHUD_TIERS; ++t )
	{
		const LaneHudBox &b = s_layout.tiers[ t ];
		const Piece piece = (Piece)( LANE_TIER1 + t );
		const Bool locked = t + 1 > tech;
		key( b, piece, !locked && s_tab == t, locked );
		const Bool flat = art( piece ) == NULL;
		if( locked )
		{
			const Int price = LaneWar_tierPrice( LANEWAR_TRACK_TECH, t + 1 );
			const Bool next = t + 1 == tech + 1 && cash >= price;
			// the lock in the key and its price beside it, outside: a key at 720 is narrower than "$3,000"
			lockMark( LaneHud_box( b.x + ( b.w - pxs( 14 ) ) / 2, b.y + ( b.h - pxs( 16 ) ) / 2, pxs( 14 ), pxs( 16 ) ) );
			drawText( money( price ), smallFont(), b.x - pxs( 6 ), b.y, b.h, next ? PRICE : UIStyle::MUTED, 1 );
			if( next && inside( b, mousePos() ) )
				TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, PRICE );
		}
		else if( flat )
		{
			const WideChar *const MARKS[ 3 ] = { u"I", u"II", u"III" };
			drawText( UnicodeString( MARKS[ t ] ), priceFont(), b.x + b.w / 2, b.y, b.h, s_tab == t ? UIStyle::ACTIVE : WHITE, 0 );
		}
		UnicodeString name;
		name.format( label( "GUI:LaneTier" ).str(), t + 1 );
		tip( b, name );
	}
}

/// the hero's card: bought once a match, then greyed with the check
static void drawHero( void )
{
	s_hero = NULL;
	LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
	const Int count = LaneWar_roster( s_player, roster, LANEWAR_ROSTER_MAX );
	for( Int i = 0; i < count && s_hero == NULL; ++i )
		if( roster[ i ].hero )
			s_hero = roster[ i ].thing;
	if( s_hero == NULL || ( s_dragging && s_dragCard == HERO_CARD ) )
		return;
	const Bool bought = TheLaneWarDirector->isHeroBought( s_team, s_seat );
	const Int price = s_hero->calcCostToBuild( s_player );
	const LaneHudBox c = bought ? s_layout.hero : risen( s_layout.hero );
	drawCard( c, s_hero->getButtonImage(), price, bought || localMoney() < price, bought, TRUE, s_armed == s_hero, FALSE );
	tip( c, s_hero->getDisplayName() );
}

static void drawCards( void )
{
	const Int tier = shownTier();
	const Bool locked = tier + 1 > techLevel();
	const Int cash = localMoney();
	for( Int c = 0; c < LANEHUD_CARDS; ++c )
		s_cardThing[ c ] = NULL;
	LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
	const Int count = LaneWar_roster( s_player, roster, LANEWAR_ROSTER_MAX );
	Int card = 0;
	for( Int i = 0; i < count && card < LANEHUD_CARDS; ++i )
	{
		if( roster[ i ].hero || roster[ i ].tier != tier + 1 )
			continue;
		const ThingTemplate *thing = roster[ i ].thing;
		s_cardThing[ card ] = thing;
		if( !( s_dragging && s_dragCard == card ) )
		{
			const Int price = thing->calcCostToBuild( s_player );
			const LaneHudBox box = locked ? s_layout.cards[ card ] : risen( s_layout.cards[ card ] );
			drawCard( box, thing->getButtonImage(), price, locked || cash < price, FALSE, FALSE, s_armed == thing && !s_dragging,
				LaneWarDirector::isJet( thing ) );
			tip( box, thing->getDisplayName() );
		}
		++card;
	}
}

/// the deck: three backs, the top one showing the next income tier and its price
static void drawDeck( void )
{
	const LaneHudBox &d = s_layout.deck;
	const Image *back = art( LANE_CARD_BACK );
	LaneHudBox top = d;
	for( Int k = 0; k < 3; ++k )
	{
		// README-v4: the copies at +0,+6, +3,+3 and +6,+0, the top one last
		const Int step = REAL_TO_INT( 3.0f * s_layout.scale + 0.5f );
		top = LaneHud_box( d.x + step * k, d.y + step * ( 2 - k ), d.w, d.h );
		if( back != NULL )
			drawImage( back, top );
		else
		{
			TheDisplay->drawFillRect( top.x, top.y, top.w, top.h, CARD_FACE );
			TheDisplay->drawOpenRect( top.x, top.y, top.w, top.h, 1.0f, k == 2 && ( s_upgradesOpen || inside( d, mousePos() ) ) ? UIStyle::EDGE_LIT : PLATE_EDGE );
		}
	}
	incomeGlyph( cardPart( top, 18, 14, 82, 68 ) );
	const Int level = TheLaneWarDirector->getIncomeLevel( s_team, s_seat );
	const LaneHudBox band = cardPart( top, 8, 84, 92, 108 );
	if( level >= LANEWAR_INCOME_TIERS )
		doneMark( cardPart( top, 40, 86, 60, 106 ) );
	else
	{
		const Int price = LaneWar_tierPrice( LANEWAR_TRACK_INCOME, level + 1 );
		drawText( money( price ), priceFont(), band.x + band.w / 2, band.y, band.h, localMoney() >= price ? PRICE : UIStyle::MUTED, 0 );
	}
	if( s_upgradesOpen )
		TheDisplay->drawOpenRect( top.x - 1, top.y - 1, top.w + 2, top.h + 2, 2.0f, UIStyle::ACTIVE );
	tip( d, label( "GUI:LaneUpgrades" ) );
}

/// the open deck: the income tier first, then the side's upgrades, each a card with its price or the check
static void drawUpgrades( void )
{
	s_upgradeCount = 0;
	if( !s_upgradesOpen )
		return;
	const UpgradeTemplate *list[ LANEWAR_ROSTER_MAX ];
	const Int listed = LaneWar_upgrades( s_player, list, LANEWAR_ROSTER_MAX );
	s_upgradeCount = min( 1 + listed, (Int)LANEHUD_UPGRADE_CELLS );
	const Int cash = localMoney();
	for( Int i = 0; i < s_upgradeCount; ++i )
	{
		const LaneHudBox c = LaneHud_upgradeCell( s_layout, s_upgradeCount, i );
		if( i == 0 )
		{
			s_upgradeIndex[ 0 ] = -1;
			const Int level = TheLaneWarDirector->getIncomeLevel( s_team, s_seat );
			const Bool maxed = level >= LANEWAR_INCOME_TIERS;
			const Int price = LaneWar_tierPrice( LANEWAR_TRACK_INCOME, level + 1 );
			drawCard( c, NULL, maxed ? -1 : price, !maxed && cash < price, maxed, FALSE, FALSE, FALSE );
			incomeGlyph( cardPart( c, 18, 14, 82, 68 ) );
			UnicodeString pays;
			pays.format( u"+%d", (Int)LANEWAR_INCOME_TIER_PAYS );
			// the pay sits on the picture well's floor: the readable face does not shrink with the screen,
			// so at 720 the body face is taller than the well's last 15 rows and ran into the price band
			const LaneHudBox well = cardPart( c, 5, 5, 95, 77 );
			GameFont *font = bodyFont()->height <= pxs( 18 ) ? bodyFont() : smallFont();
			const Int th = font->height;
			if( !maxed )
				drawText( pays, font, well.x + well.w - pxs( 3 ), well.y + well.h - th - pxs( 1 ), th, INCOME, 1 );
			UnicodeString name;
			name.format( label( "GUI:LaneIncome" ).str(), level, (Int)LANEWAR_INCOME_TIERS );
			tip( c, name );
			continue;
		}
		const UpgradeTemplate *u = list[ i - 1 ];
		s_upgradeIndex[ i ] = i - 1;
		const Bool done = s_player->hasUpgradeComplete( u );
		const Int price = u->calcCostToBuild( s_player );
		drawCard( c, u->getButtonImage(), price, done || cash < price, done, FALSE, FALSE, FALSE );
		tip( c, TheGameText->fetch( u->getDisplayNameLabel() ) );
	}
}

static void drawAbilitiesKey( void )
{
	const LaneHudBox &b = s_layout.abilitiesKey;
	key( b, LANE_ABILITIES_KEY, s_abilitiesOpen );
	if( art( LANE_ABILITIES_KEY ) == NULL )
		boltGlyph( b );
	tip( b, label( "GUI:LaneAbilities" ) );
}

//-------------------------------------------------------------------------------------------------
/** The abilities panel: one row a roster unit's button the director presses, the unit's picture, the
	* button's and its switch.  A click on the row turns it; the order goes out as LANEWAR_ORDER_ABILITY
	* and the switch shows what the logic holds, read here at the sync point. */
//-------------------------------------------------------------------------------------------------
static void gatherAbilities( void )
{
	s_rowCount = 0;
	Int wanted = 0;
	LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
	const Int count = LaneWar_roster( s_player, roster, LANEWAR_ROSTER_MAX );
	for( Int i = 0; i < count; ++i )
	{
		LaneWarAbility list[ LANEWAR_ABILITIES_MAX ];
		const Int listed = LaneWar_abilitiesOf( roster[ i ].thing, list, LANEWAR_ABILITIES_MAX );
		for( Int k = 0; k < listed; ++k, ++wanted )
		{
			if( s_rowCount >= LANEHUD_ABILITY_ROWS )
				continue;
			s_rows[ s_rowCount ].thing = roster[ i ].thing;
			s_rows[ s_rowCount ].ability = list[ k ];
			++s_rowCount;
		}
	}
	// the panel grows to LANEHUD_ABILITY_ROWS, all a 16:9 screen holds under the top chips; say so once if a roster outgrows it
	static AsciiString s_warned;
	if( wanted > s_rowCount && s_warned != s_player->getSide() )
	{
		s_warned = s_player->getSide();
		DEBUG_LOG(( "LANEHUD: %s has %d ability rows, the panel shows %d\n", s_warned.str(), wanted, s_rowCount ));
	}
}

/** The AbilitiesPanel piece cut across in three, the vertical 3-slice README-v4 asks for past six rows:
	* 0 its head (the plate's top edge and the glass's first row), 1 one row from the glass's middle, 2
	* its foot (the last row and the bottom edge).  The glass's scanlines repeat every two pixels, so a
	* 48 pixel row laid end to end carries them on unbroken where a stretch would smear them.  Each is a
	* mapped image of its own on the piece's sheet, <Prefix>LaneAbilitiesPanel{Head,Row,Foot}, handed to
	* the image collection the first time a side asks.  NULL while the piece is missing. */
static const Image *panelSlice( Int part )
{
	const Piece piece = (Piece)( LANE_ABILITIES_PANEL_HEAD + part );
	// rows of the piece at 1080: an 8 pixel edge, six glass rows of 48, an 8 pixel edge
	static const Int FROM[ 3 ] = { 0, 104, 248 }, TO[ 3 ] = { 56, 152, 304 }, HEIGHT = 304;
	const Image *made = art( piece );
	if( made != NULL )
		return made;
	const Image *whole = art( LANE_ABILITIES_PANEL );
	if( whole == NULL || BitTest( whole->getStatus(), IMAGE_STATUS_ROTATED_90_CLOCKWISE ) )
		return NULL;
	Image *slice = newInstance( Image );
	slice->setName( pieceName( piece ) );
	slice->setFilename( whole->getFilename() );
	slice->setTextureWidth( whole->getTextureSize()->x );
	slice->setTextureHeight( whole->getTextureSize()->y );
	slice->setStatus( whole->getStatus() );
	const Region2D *uv = whole->getUV();
	Region2D cut = *uv;
	cut.lo.y = uv->lo.y + ( uv->hi.y - uv->lo.y ) * FROM[ part ] / HEIGHT;
	cut.hi.y = uv->lo.y + ( uv->hi.y - uv->lo.y ) * TO[ part ] / HEIGHT;
	slice->setUV( &cut );
	ICoord2D size;
	size.x = whole->getImageWidth();
	size.y = whole->getImageHeight() * ( TO[ part ] - FROM[ part ] ) / HEIGHT;
	slice->setImageSize( &size );
	TheMappedImageCollection->addImage( slice );
	forgetPieces();		// the theme kept the name as missing
	return slice;
}

static void drawToggle( const LaneHudBox &b, Bool on )
{
	const Image *image = art( on ? LANE_TOGGLE_ON : LANE_TOGGLE_OFF );
	if( image != NULL )
	{
		drawImage( image, b );
		return;
	}
	TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, on ? KEY_FACE_LIT : WELL );
	TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, on ? UIStyle::ACTIVE : PLATE_EDGE );
	const Int knob = b.h - pxs( 6 );
	const Int kx = on ? b.x + b.w - pxs( 3 ) - knob : b.x + pxs( 3 );
	TheDisplay->drawFillRect( kx, b.y + pxs( 3 ), knob, knob, on ? WHITE : UIStyle::MUTED );
}

static void drawAbilities( void )
{
	if( !s_abilitiesOpen )
		return;
	gatherAbilities();
	const LaneHudLayout &l = s_layout;
	const Int rows = max( (Int)ABILITY_ART_ROWS, s_rowCount );
	const LaneHudBox panel = LaneHud_abilitiesPanel( l, rows );
	const Image *panelArt = art( LANE_ABILITIES_PANEL );
	const Image *head = panelSlice( 0 ), *middle = panelSlice( 1 ), *foot = panelSlice( 2 );
	if( panelArt != NULL && rows == ABILITY_ART_ROWS )
		drawImage( panelArt, panel );
	else if( head != NULL && middle != NULL && foot != NULL )
	{
		// more rows than the art's six: its head, one glass row repeated, its foot
		const Int end = l.abilityPad + l.abilityRowH;
		drawImage( head, LaneHud_box( panel.x, panel.y, panel.w, end ) );
		for( Int r = 1; r < rows - 1; ++r )
			drawImage( middle, LaneHud_box( panel.x, panel.y + l.abilityPad + r * l.abilityRowH, panel.w, l.abilityRowH ) );
		drawImage( foot, LaneHud_box( panel.x, panel.y + panel.h - end, panel.w, end ) );
	}
	else
	{
		plate( panel );
		TheDisplay->drawFillRect( panel.x + l.abilityPad, panel.y + l.abilityPad, panel.w - 2 * l.abilityPad, panel.h - 2 * l.abilityPad, WELL );
	}
	const Int playerIndex = s_player->getPlayerIndex();
	for( Int r = 0; r < s_rowCount; ++r )
	{
		const AbilityRow &row = s_rows[ r ];
		const LaneHudBox box = LaneHud_abilityRow( l, rows, r );
		const Bool on = TheLaneWarDirector->isAbilityOn( playerIndex, row.thing, row.ability.slot );
		if( inside( box, mousePos() ) )
			TheDisplay->drawFillRect( box.x, box.y, box.w, box.h, GameMakeColor( 255, 255, 255, 18 ) );
		const LaneHudBox cameo = LaneHud_at( box, l.abilityCameo ), icon = LaneHud_at( box, l.abilityIcon );
		if( row.thing->getButtonImage() != NULL )
			drawImage( row.thing->getButtonImage(), cameo );
		if( row.ability.button->getButtonImage() != NULL )
			drawImage( row.ability.button->getButtonImage(), icon, on ? 0xFFFFFFFF : DIM );
		drawToggle( LaneHud_at( box, l.abilityToggle ), on );
		tip( cameo, row.thing->getDisplayName() );
		tip( LaneHud_box( icon.x, box.y, box.x + box.w - icon.x, box.h ), TheGameText->fetch( row.ability.button->getTextLabel() ) );
	}
}

Bool LaneHud_abilitiesPanelIsOpen( void )
{
	return s_abilitiesOpen;
}

void LaneHud_abilitiesPanelSetOpen( Bool open )
{
	s_abilitiesOpen = open;
	if( open )
		s_upgradesOpen = FALSE;
}

//-------------------------------------------------------------------------------------------------
// What rides the pointer: a dragged card, the target block over the world, and a plot unit carried
// over the hand with what selling it brings back
//-------------------------------------------------------------------------------------------------
static void drawHeld( void )
{
	const ICoord2D *mouse = mousePos();
	ICoord2D corner[ 4 ];
	if( s_target.on && s_target.onPlot && blockOnScreen( s_target.grid, s_target.col, s_target.row, s_target.size, corner ) )
		outline( corner, s_target.legal ? LEGAL : BLOCKED );

	if( s_dragging && s_armed != NULL )
	{
		// over the plot the card shrinks below and right of the pointer, so the ghost and its outline stay in sight
		const Bool overPlot = ( s_target.on && s_target.onPlot ) || s_garrisonIndex >= 0;
		const LaneHudBox c = overPlot
			? LaneHud_box( mouse->x + pxs( 20 ), mouse->y + pxs( 20 ), pxs( 60 ), pxs( 70 ) )
			: LaneHud_box( mouse->x + pxs( 24 ), mouse->y - pxs( 44 ), pxs( 115 ), pxs( 133 ) );
		drawCard( c, s_armed->getButtonImage(), overPlot ? -1 : s_armed->calcCostToBuild( s_player ), FALSE, FALSE, s_dragCard == HERO_CARD, FALSE,
			LaneWarDirector::isJet( s_armed ) );
	}

	// an infantry card over a plot unit that carries men: the unit green with room, red without
	const std::vector<LaneWarDirector::PlotUnit> &units = TheLaneWarDirector->getPlotUnits();
	if( s_garrisonIndex >= 0 && s_garrisonIndex < (Int)units.size() )
	{
		const LaneWarDirector::PlotUnit &u = units[ s_garrisonIndex ];
		if( blockOnScreen( u.grid, u.col, u.row, u.size, corner ) )
			outline( corner, s_garrisonRoom ? LEGAL : BLOCKED );
	}

	// a passenger carried off his unit: his picture at the pointer, and over the hand what he sells for
	if( s_passengerOf != INVALID_ID )
	{
		for( size_t i = 0; i < units.size(); ++i )
		{
			if( units[ i ].id != s_passengerOf || s_passenger < 0 || s_passenger >= (Int)units[ i ].passengers.size() )
				continue;
			const LaneWarDirector::PlotUnit::Passenger &p = units[ i ].passengers[ s_passenger ];
			const LaneHudBox c = LaneHud_box( mouse->x + pxs( 12 ), mouse->y - pxs( 30 ), pxs( 44 ), pxs( 36 ) );
			if( p.thing->getButtonImage() != NULL )
				drawImage( p.thing->getButtonImage(), c );
			if( inside( s_layout.hand, mouse ) )
			{
				const LaneHudBox &h = s_layout.hand;
				TheDisplay->drawOpenRect( h.x, h.y, h.w, h.h, 2.0f, PRICE );
				UnicodeString refund;
				refund.format( u"+%s", money( LaneWar_refund( p.cost, units[ i ].sent ) + LaneWar_refund( p.veterancyPaid, units[ i ].sent ) ).str() );
				drawText( refund, bigFont(), mouse->x + pxs( 60 ), mouse->y - pxs( 34 ), pxs( 30 ), PRICE );
			}
		}
		return;
	}

	if( s_moving == INVALID_ID || !inside( s_layout.hand, mouse ) )
		return;
	for( size_t i = 0; i < units.size(); ++i )
	{
		if( units[ i ].id != s_moving )
			continue;
		const Object *obj = TheGameLogic->findObjectByID( s_moving );
		const Bool hero = obj != NULL && LaneWar_isHero( s_player, obj->getTemplate() );
		const LaneHudBox &h = s_layout.hand;
		TheDisplay->drawOpenRect( h.x, h.y, h.w, h.h, 2.0f, hero ? BLOCKED : PRICE );
		if( !hero )
		{
			// everything selling it pays back: the unit, its ranks, its upgrades and its passengers
			UnicodeString refund;
			refund.format( u"+%s", money( TheLaneWarDirector->getSellValue( (Int)i ) ).str() );
			drawText( refund, bigFont(), mouse->x + pxs( 18 ), mouse->y - pxs( 34 ), pxs( 30 ), PRICE );
		}
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/** The row of chips over the plot unit the pointer rests on: for now the rank chip, the next rank's
	* chevrons and its price, a click buying it (LANEWAR_ORDER_VETERANCY), the check at Heroic.  The
	* unit's own upgrades and its passengers join this row as the director gains them. */
//-------------------------------------------------------------------------------------------------
static Int clusterIndex( void )
{
	const std::vector<LaneWarDirector::PlotUnit> &units = TheLaneWarDirector->getPlotUnits();
	for( size_t i = 0; i < units.size(); ++i )
		if( units[ i ].id == s_clusterUnit )
			return (Int)i;
	return -1;
}

/// which unit the row stands over: the one under the pointer, or the last one while the pointer is in its row
static void refreshCluster( const ICoord2D *mouse )
{
	if( s_armed != NULL || s_moving != INVALID_ID )
	{
		s_clusterUnit = INVALID_ID;
		return;
	}
	if( s_clusterUnit != INVALID_ID && clusterIndex() >= 0 && inside( s_clusterBox, mouse ) )
		return;
	s_clusterUnit = INVALID_ID;
	Coord3D world;
	if( overHud( mouse ) || !TheTacticalView->screenToTerrain( mouse, &world ) )
		return;
	const Int index = plotUnitUnder( world );
	if( index >= 0 )
		s_clusterUnit = TheLaneWarDirector->getPlotUnits()[ index ].id;
}

/// the screen point over a plot unit's block: its middle across and its top edge
static Bool overUnit( const LaneWarDirector::PlotUnit &u, Int *cx, Int *top )
{
	ICoord2D corner[ 4 ];
	if( !blockOnScreen( u.grid, u.col, u.row, u.size, corner ) )
		return FALSE;
	*top = corner[ 0 ].y;
	*cx = 0;
	for( Int i = 0; i < 4; ++i )
	{
		*top = min( *top, (Int)corner[ i ].y );
		*cx += corner[ i ].x;
	}
	*cx /= 4;
	return TRUE;
}

/// a chip's face, as a card's: lit under the pointer when it can be pressed
static void chipFace( const LaneHudBox &b, Bool live )
{
	TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, CARD_FACE );
	TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, live && inside( b, mousePos() ) ? UIStyle::EDGE_LIT : PLATE_EDGE );
}

/// a chip's price band, or the check when there is nothing left to buy
static void chipPrice( const LaneHudBox &b, Int price, Bool can )
{
	const LaneHudBox band = LaneHud_box( b.x, b.y + b.h * 3 / 5, b.w, b.h * 2 / 5 );
	if( price <= 0 )
		doneMark( LaneHud_box( b.x + b.w / 2 - pxs( 8 ), band.y, pxs( 16 ), pxs( 16 ) ) );
	else
		drawText( UIStyle::grouped( price ), smallFont(), b.x + b.w / 2, band.y, band.h, can ? PRICE : UIStyle::MUTED, 0 );
}

/// rank chevrons, `count` of them stacked in the chip's top
static void chevrons( const LaneHudBox &b, Int count, Color color )
{
	const LaneHudBox glyph = LaneHud_box( b.x + b.w / 4, b.y + pxs( 3 ), b.w / 2, b.h / 2 );
	for( Int k = 0; k < count; ++k )
	{
		const Real y = 0.15f + 0.25f * k;
		line( 0.1f, y, 0.5f, y + 0.25f, glyph, color );
		line( 0.5f, y + 0.25f, 0.9f, y, glyph, color );
	}
}

/** The row over the plot unit the pointer rests on, calm and wordless: its rank chip (the next rank's
	* chevrons and price, the check at Heroic), a chip for each upgrade on its own command set (price,
	* the check once owned, greyed where another one rules it out), then a chip a passenger (his picture
	* and his next rank's price) and an empty outline for each place still free. */
static void drawCluster( void )
{
	s_clusterBox = s_clusterRow = LaneHud_box( 0, 0, 0, 0 );
	s_chipCount = 0;
	const Int index = s_clusterUnit != INVALID_ID ? clusterIndex() : -1;
	if( index < 0 )
		return;
	const LaneWarDirector::PlotUnit &u = TheLaneWarDirector->getPlotUnits()[ index ];
	Int cx, top;
	if( !overUnit( u, &cx, &top ) )
		return;

	// what the row holds
	LaneWarDirector::UnitUpgrade upgrades[ 6 ];
	const Int upgradeCount = TheLaneWarDirector->getUnitUpgrades( index, upgrades, 6 );
	Int used = 0, most = 0;
	TheLaneWarDirector->getPassengerRoom( index, &used, &most );
	const Int passengers = (Int)u.passengers.size();
	const Int room = max( 0, min( most - used, (Int)CLUSTER_CHIPS - 1 - upgradeCount - passengers ) );
	s_chips[ s_chipCount++ ].kind = CHIP_RANK;
	for( Int k = 0; k < upgradeCount; ++k, ++s_chipCount )
	{
		s_chips[ s_chipCount ].kind = CHIP_UPGRADE;
		s_chips[ s_chipCount ].value = k;
	}
	for( Int p = 0; p < passengers && s_chipCount < CLUSTER_CHIPS; ++p, ++s_chipCount )
	{
		s_chips[ s_chipCount ].kind = CHIP_PASSENGER;
		s_chips[ s_chipCount ].value = p;
	}
	for( Int r = 0; r < room; ++r, ++s_chipCount )
		s_chips[ s_chipCount ].kind = CHIP_ROOM;

	// a chip holds a four-figure price in the readable face, which does not shrink with the screen
	const Int size = max( pxs( 44 ), smallFont()->height * 5 / 2 ), gap = pxs( 4 );
	const Int rowW = s_chipCount * size + ( s_chipCount - 1 ) * gap;
	s_clusterRow = LaneHud_box( cx - rowW / 2, top - pxs( 8 ) - size, rowW, size );
	s_clusterBox = LaneHud_box( s_clusterRow.x - pxs( 6 ), s_clusterRow.y - pxs( 6 ), s_clusterRow.w + pxs( 12 ), top - s_clusterRow.y + pxs( 10 ) );

	const Int cash = localMoney();
	for( Int c = 0; c < s_chipCount; ++c )
	{
		Chip &chip = s_chips[ c ];
		chip.box = LaneHud_box( s_clusterRow.x + c * ( size + gap ), s_clusterRow.y, size, size );
		const LaneHudBox &b = chip.box;
		const LaneHudBox picture = LaneHud_box( b.x + pxs( 3 ), b.y + pxs( 3 ), b.w - pxs( 6 ), b.h * 3 / 5 - pxs( 3 ) );
		if( chip.kind == CHIP_RANK )
		{
			const Int price = TheLaneWarDirector->getNextVeterancyPrice( index );
			const Bool can = price > 0 && cash >= price;
			chipFace( b, can );
			chevrons( b, min( 3, u.veterancy + ( price > 0 ? 1 : 0 ) ), can ? PRICE : UIStyle::MUTED );
			chipPrice( b, price, can );
		}
		else if( chip.kind == CHIP_UPGRADE )
		{
			const LaneWarDirector::UnitUpgrade &up = upgrades[ chip.value ];
			const Bool can = up.available && !up.owned && cash >= up.price;
			chipFace( b, can );
			if( up.upgrade->getButtonImage() != NULL )
				drawImage( up.upgrade->getButtonImage(), picture, up.owned || can ? 0xFFFFFFFF : DIM );
			chipPrice( b, up.owned ? 0 : up.price, can );
			if( !up.owned && !up.available )
				TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, GREYED );
			tip( b, TheGameText->fetch( up.upgrade->getDisplayNameLabel() ) );
		}
		else if( chip.kind == CHIP_PASSENGER )
		{
			const LaneWarDirector::PlotUnit::Passenger &p = u.passengers[ chip.value ];
			const Int price = TheLaneWarDirector->getNextPassengerVeterancyPrice( index, chip.value );
			const Bool can = price > 0 && cash >= price;
			chipFace( b, TRUE );
			if( !( s_passengerOf == u.id && s_passenger == chip.value ) && p.thing->getButtonImage() != NULL )
				drawImage( p.thing->getButtonImage(), picture );
			if( p.veterancy > 0 )
				chevrons( LaneHud_box( b.x + b.w * 3 / 5, b.y, b.w * 2 / 5, b.h / 2 ), p.veterancy, PRICE );
			chipPrice( b, price, can );
			tip( b, p.thing->getDisplayName() );
		}
		else
			TheDisplay->drawOpenRect( b.x + pxs( 4 ), b.y + pxs( 4 ), b.w - pxs( 8 ), b.h - pxs( 8 ), 1.0f, PLATE_EDGE );
	}
}

/** A press in the row: the rank chip buys the next rank, an upgrade chip its upgrade, a passenger
	* chip his next rank by its price band and otherwise picks him up to be dragged onto the hand. */
static Bool pressCluster( const ICoord2D *mouse )
{
	const Int index = s_clusterUnit != INVALID_ID ? clusterIndex() : -1;
	if( index < 0 || s_chipCount == 0 || !inside( s_clusterRow, mouse ) )
		return FALSE;
	const LaneWarDirector::PlotUnit &u = TheLaneWarDirector->getPlotUnits()[ index ];
	for( Int c = 0; c < s_chipCount; ++c )
	{
		const Chip &chip = s_chips[ c ];
		if( !inside( chip.box, mouse ) )
			continue;
		if( chip.kind == CHIP_RANK && TheLaneWarDirector->getNextVeterancyPrice( index ) > 0 )
			sendOrder( LANEWAR_ORDER_VETERANCY, u.grid, u.col, u.row );
		else if( chip.kind == CHIP_UPGRADE )
		{
			LaneWarDirector::UnitUpgrade upgrades[ 6 ];
			const Int count = TheLaneWarDirector->getUnitUpgrades( index, upgrades, 6 );
			if( chip.value < count && upgrades[ chip.value ].available && !upgrades[ chip.value ].owned )
				sendOrder( LANEWAR_ORDER_UNIT_UPGRADE, u.grid, u.col, u.row, upgrades[ chip.value ].slot );
		}
		else if( chip.kind == CHIP_PASSENGER )
		{
			const Bool onPrice = mouse->y >= chip.box.y + chip.box.h * 3 / 5;
			if( onPrice && TheLaneWarDirector->getNextPassengerVeterancyPrice( index, chip.value ) > 0 )
				sendOrder( LANEWAR_ORDER_VETERANCY, u.grid, u.col, u.row, chip.value );
			else if( !onPrice )
			{
				s_passengerOf = u.id;
				s_passenger = chip.value;
			}
		}
		return TRUE;
	}
	return TRUE;
}

static void drawTip( void )
{
	if( s_tip.isEmpty() || s_dragging || s_moving != INVALID_ID || s_passengerOf != INVALID_ID )
		return;
	GameFont *font = bodyFont();
	DisplayString *s = pooled( s_tip, font );
	if( s == NULL )
		return;
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	const ICoord2D *mouse = mousePos();
	const Int pad = pxs( 5 );
	Int x = mouse->x + pxs( 14 ), y = mouse->y - h - 2 * pad - pxs( 6 );
	x = min( x, (Int)TheDisplay->getWidth() - w - 2 * pad - 1 );
	y = max( y, 0 );
	TheDisplay->drawFillRect( x, y, w + 2 * pad, h + 2 * pad, GameMakeColor( 10, 12, 16, 235 ) );
	TheDisplay->drawOpenRect( x, y, w + 2 * pad, h + 2 * pad, 1.0f, PLATE_EDGE );
	s->draw( x + pad, y + pad, WHITE, UIStyle::SHADOW );
}

//-------------------------------------------------------------------------------------------------
Bool LaneHud_isShowing( void )
{
	return s_showing;
}

//-------------------------------------------------------------------------------------------------
/** The target under the pointer and the ghost riding it: the armed unit, or the plot unit picked up,
	* on the block of slots the pointer centres, red where whyNotPlace says no. */
//-------------------------------------------------------------------------------------------------
static void lookAtPlot( void );

static void destroyGhost( void )
{
	Drawable *ghost = s_ghost != INVALID_DRAWABLE_ID ? TheGameClient->findDrawableByID( s_ghost ) : NULL;
	if( ghost != NULL )
		TheGameClient->destroyDrawable( ghost );
	s_ghost = INVALID_DRAWABLE_ID;
	s_ghostOf = NULL;
}

void LaneHud_preDraw( void )
{
	s_target.on = FALSE;
	s_garrisonIndex = -1;
	if( !refreshShowing() )
	{
		dropHeld();
		s_abilitiesOpen = s_upgradesOpen = s_scoreboard = FALSE;
		s_clusterUnit = INVALID_ID;
		s_tab = 0;
		s_homed = FALSE;
		destroyGhost();
		return;
	}
	hidePlotUnitIcons();
	if( !s_seated )
	{
		destroyGhost();
		return;
	}
	keepClassicBarDown();
	if( !s_homed )
	{
		// the match opens on the player's plot, turned as Home turns it
		s_homed = TRUE;
		lookAtPlot();
	}
	if( s_tab + 1 > techLevel() )
		s_tab = techLevel() - 1;

	// a picked up unit that has gone, an armed unit the player can no longer pay for or a hero now bought
	const Object *moving = s_moving != INVALID_ID ? TheGameLogic->findObjectByID( s_moving ) : NULL;
	if( s_moving != INVALID_ID && moving == NULL )
		s_moving = INVALID_ID;
	if( s_armed != NULL && ( localMoney() < s_armed->calcCostToBuild( s_player )
			|| ( LaneWar_isHero( s_player, s_armed ) && TheLaneWarDirector->isHeroBought( s_team, s_seat ) ) ) )
	{
		s_armed = NULL;
		s_dragging = FALSE;
		s_dragCard = NO_CARD;
	}

	const ICoord2D *mouse = mousePos();
	refreshCluster( mouse );
	Coord3D world;
	const ThingTemplate *thing = s_armed != NULL ? s_armed : moving != NULL ? moving->getTemplate() : NULL;
	if( thing != NULL && !overHud( mouse ) && TheTacticalView->screenToTerrain( mouse, &world ) )
	{
		Target &t = s_target;
		t.thing = thing;
		t.grid = LANEWAR_GRID_GROUND;
		t.size = LaneWarDirector::sizeFor( thing );
		if( slotUnder( world, t.grid, t.size, &t.col, &t.row, &t.onPlot ) )
		{
			t.on = TRUE;
			t.legal = TheLaneWarDirector->whyNotPlace( s_player, thing, t.col, t.row, moving ? moving->getID() : INVALID_ID ) == NULL;
			t.at = t.onPlot ? TheLaneWarDirector->slotCentre( s_team, s_seat, t.grid, t.col, t.row, t.size ) : world;
		}
	}

	// an infantry card over one of the player's units that carries men goes in it, not onto a slot
	s_garrisonIndex = -1;
	if( s_armed != NULL && s_armed->isKindOf( KINDOF_INFANTRY ) && !overHud( mouse ) && TheTacticalView->screenToTerrain( mouse, &world ) )
	{
		const Int index = plotUnitUnder( world );
		Int used = 0, most = 0;
		if( index >= 0 )
			TheLaneWarDirector->getPassengerRoom( index, &used, &most );
		if( most > 0 )
		{
			s_garrisonIndex = index;
			s_garrisonRoom = used < most && localMoney() >= s_armed->calcCostToBuild( s_player );
			s_target.on = FALSE;
		}
	}

	if( !s_target.on )
	{
		destroyGhost();
		return;
	}

	Drawable *ghost = s_ghost != INVALID_DRAWABLE_ID ? TheGameClient->findDrawableByID( s_ghost ) : NULL;
	if( ghost == NULL || s_ghostOf != s_target.thing )
	{
		destroyGhost();
		ghost = TheThingFactory->newDrawable( s_target.thing, DRAWABLE_STATUS_NO_STATE_PARTICLES );
		if( ghost == NULL )
			return;
		ghost->setIndicatorColor( s_player->getPlayerColor() );
		ghost->setDrawableOpacity( TheGlobalData->m_buildPlacementOpacity );
		ghost->setShadowsEnabled( FALSE );
		s_ghost = ghost->getID();
		s_ghostOf = s_target.thing;
	}
	ghost->setPosition( &s_target.at );
	ghost->setOrientation( TheLaneWarDirector->towardEnemy( s_team ) > 0.0f ? 0.0f : PI );
	static const RGBColor illegal = { 1.0f, 0.0f, 0.0f };
	ghost->colorTint( s_target.legal ? NULL : &illegal );
}

//-------------------------------------------------------------------------------------------------
void LaneHud_draw( void )
{
	begin( hudSide(), s_layout.scale );
	s_tip.clear();
	if( !s_showing )
		return;
	drawTop();
	if( s_scoreboard )
		drawScoreboard();
	else
		drawStripHover();
	if( s_seated )
	{
		drawTierKeys();
		drawHero();
		drawCards();
		drawDeck();
		drawAbilitiesKey();
		drawUpgrades();
		drawAbilities();
		drawCluster();
		drawHeld();
	}
	drawTip();
}

//-------------------------------------------------------------------------------------------------
// Presses
//-------------------------------------------------------------------------------------------------

/** The owner's rule of 2026-11-21 for both teams: the camera looks at the lane from the side, never from
	* behind the units, so the lane runs across the screen with the player's own end on the left and the
	* enemy on the right, as the strip map draws it.  The view's angle 0 looks along +y with +x to the
	* right (a world heading h is angle h - 90 degrees, W3DView's rotateCameraTowardPosition), so the team
	* whose enemy lies towards -x is turned half round.  Pitch and zoom stay as they are. */
void LaneHud_turnToLane( Int team )
{
	TheTacticalView->setAngle( TheLaneWarDirector->towardEnemy( team ) > 0.0f ? 0.0f : PI );
}

static void turnToLane( void )
{
	LaneHud_turnToLane( s_team );
}

/// the camera to the middle of the player's own plot, turned to the lane
static void lookAtPlot( void )
{
	Coord2D lo, hi;
	if( !TheLaneWarDirector->getPlotBounds( s_team, s_seat, &lo, &hi ) )
		return;
	Coord3D at;
	at.x = 0.5f * ( lo.x + hi.x );
	at.y = 0.5f * ( lo.y + hi.y );
	at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );
	turnToLane();
	TheTacticalView->lookAt( &at );
}

/// a card or the hero pressed: armed and riding the pointer until the button comes up; pressed again, put back
static void pickCard( const ThingTemplate *thing, Int slot, const LaneHudBox &box )
{
	s_moving = INVALID_ID;
	if( s_armed == thing )
	{
		dropHeld();
		return;
	}
	s_armed = thing;
	s_dragging = TRUE;
	s_dragCard = slot;
	s_pressBox = grow( box, pxs( LANEHUD_CARD_RISE ) );
}

/// a press on the bottom row or the panels over it; TRUE when it was on one of them
static Bool pressRow( const ICoord2D *mouse )
{
	const LaneHudLayout &l = s_layout;
	if( s_upgradesOpen )
		for( Int c = 0; c < s_upgradeCount; ++c )
		{
			if( !inside( LaneHud_upgradeCell( l, s_upgradeCount, c ), mouse ) )
				continue;
			if( s_upgradeIndex[ c ] < 0 )
			{
				const Int level = TheLaneWarDirector->getIncomeLevel( s_team, s_seat );
				if( level < LANEWAR_INCOME_TIERS )
					sendOrder( LANEWAR_ORDER_TIER, LANEWAR_TRACK_INCOME, level + 1 );
			}
			else
				sendOrder( LANEWAR_ORDER_UPGRADE, s_upgradeIndex[ c ] );
			return TRUE;
		}
	if( s_abilitiesOpen && inside( abilitiesPanel(), mouse ) )
	{
		const Int rows = max( (Int)ABILITY_ART_ROWS, s_rowCount );
		for( Int r = 0; r < s_rowCount; ++r )
			if( inside( LaneHud_abilityRow( l, rows, r ), mouse ) )
			{
				const AbilityRow &row = s_rows[ r ];
				const Bool on = TheLaneWarDirector->isAbilityOn( s_player->getPlayerIndex(), row.thing, row.ability.slot );
				sendOrder( LANEWAR_ORDER_ABILITY, row.thing->getTemplateID(), row.ability.slot, on ? 0 : 1 );
			}
		return TRUE;
	}

	const Int tech = techLevel();
	for( Int t = 0; t < LANEHUD_TIERS; ++t )
	{
		if( !inside( l.tiers[ t ], mouse ) )
			continue;
		if( t + 1 <= tech )
			s_tab = t;
		else if( t + 1 == tech + 1 && localMoney() >= LaneWar_tierPrice( LANEWAR_TRACK_TECH, t + 1 ) )
		{
			sendOrder( LANEWAR_ORDER_TIER, LANEWAR_TRACK_TECH, t + 1 );
			s_tab = t;
		}
		return TRUE;
	}

	const Int rise = pxs( LANEHUD_CARD_RISE );
	const LaneHudBox heroReach = LaneHud_box( l.hero.x, l.hero.y - rise, l.hero.w, l.hero.h + rise );
	if( inside( heroReach, mouse ) )
	{
		if( s_hero != NULL && !TheLaneWarDirector->isHeroBought( s_team, s_seat ) )
			pickCard( s_hero, HERO_CARD, l.hero );
		return TRUE;
	}
	for( Int c = 0; c < LANEHUD_CARDS; ++c )
	{
		const LaneHudBox reach = LaneHud_box( l.cards[ c ].x, l.cards[ c ].y - rise, l.cards[ c ].w, l.cards[ c ].h + rise );
		if( !inside( reach, mouse ) )
			continue;
		if( s_cardThing[ c ] != NULL && s_tab + 1 <= tech )
			pickCard( s_cardThing[ c ], c, l.cards[ c ] );
		return TRUE;
	}

	if( inside( l.deck, mouse ) )
	{
		s_upgradesOpen = !s_upgradesOpen;
		s_abilitiesOpen = FALSE;
		return TRUE;
	}
	if( inside( l.abilitiesKey, mouse ) )
	{
		LaneHud_abilitiesPanelSetOpen( !s_abilitiesOpen );
		return TRUE;
	}
	return FALSE;
}

Bool LaneHud_press( const ICoord2D *mouse, Bool right )
{
	if( !s_showing )
		return FALSE;
	const LaneHudLayout &l = s_layout;

	if( right )
	{
		// a right press takes back whatever is held or open, and is the HUD's over any of its boxes
		const Bool held = s_armed != NULL || s_moving != INVALID_ID || s_upgradesOpen || s_abilitiesOpen;
		dropHeld();
		s_upgradesOpen = s_abilitiesOpen = FALSE;
		return held || overHud( mouse );
	}

	if( inside( l.menu, mouse ) )
	{
		TheMessageStream->appendMessage( GameMessage::MSG_META_OPTIONS );
		return TRUE;
	}
	if( inside( l.diplomacy, mouse ) )
	{
		TheMessageStream->appendMessage( GameMessage::MSG_META_DIPLOMACY );
		return TRUE;
	}
	if( inside( l.map, mouse ) )
	{
		StripFrame f;
		if( inside( l.lane, mouse ) && stripFrame( &f ) )
		{
			const Coord3D at = fromStrip( f, mouse );
			turnToLane();
			TheTacticalView->lookAt( &at );
		}
		return TRUE;
	}
	if( !s_seated )
		return overHud( mouse );

	// a unit carried by clicks put down on the hand: sold
	if( s_moving != INVALID_ID && inside( l.hand, mouse ) )
	{
		LaneHud_release( mouse );
		return TRUE;
	}
	if( pressCluster( mouse ) || pressRow( mouse ) || overHud( mouse ) )
		return TRUE;

	// the world
	Coord3D world;
	if( !TheTacticalView->screenToTerrain( mouse, &world ) )
		return s_armed != NULL || s_moving != INVALID_ID;

	if( s_armed != NULL && s_garrisonIndex >= 0 )
	{
		// an armed infantry card clicked onto a unit that carries men: one more man in it
		const LaneWarDirector::PlotUnit &u = TheLaneWarDirector->getPlotUnits()[ s_garrisonIndex ];
		if( s_garrisonRoom )
			sendOrder( LANEWAR_ORDER_GARRISON, u.grid, u.col, u.row, s_armed->getTemplateID() );
		return TRUE;
	}
	if( s_armed != NULL )
	{
		Int col, row;
		Bool onGrid;
		if( slotUnder( world, LANEWAR_GRID_GROUND, LaneWarDirector::sizeFor( s_armed ), &col, &row, &onGrid ) && onGrid )
		{
			sendOrder( LANEWAR_ORDER_PLACE, s_armed->getTemplateID(), col, row );
			// the hero goes once; anything else stays armed while the money lasts (preDraw drops it)
			if( LaneWar_isHero( s_player, s_armed ) )
				dropHeld();
		}
		return TRUE;
	}

	if( s_moving != INVALID_ID )
	{
		LaneHud_release( mouse );
		return TRUE;
	}

	// a press on one of the player's own plot units picks it up: let go on a free slot it moves there,
	// on the hand it is sold, where it was it rides the pointer until the next click
	const Int index = plotUnitUnder( world );
	if( index >= 0 )
	{
		const LaneWarDirector::PlotUnit &u = TheLaneWarDirector->getPlotUnits()[ index ];
		s_moving = u.id;
		s_moveGrid = u.grid;
		s_moveCol = u.col;
		s_moveRow = u.row;
		s_upgradesOpen = s_abilitiesOpen = FALSE;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void LaneHud_release( const ICoord2D *mouse )
{
	if( !s_showing || !s_seated )
		return;

	// a card let go: back where it was pressed is a click and it stays armed; on the plot it is bought there
	if( s_dragging )
	{
		s_dragging = FALSE;
		const ThingTemplate *thing = s_armed;
		if( thing == NULL || inside( s_pressBox, mouse ) )
			return;
		if( s_garrisonIndex >= 0 )
		{
			const LaneWarDirector::PlotUnit &u = TheLaneWarDirector->getPlotUnits()[ s_garrisonIndex ];
			if( s_garrisonRoom )
				sendOrder( LANEWAR_ORDER_GARRISON, u.grid, u.col, u.row, thing->getTemplateID() );
			dropHeld();
			return;
		}
		Coord3D world;
		Int col, row;
		Bool onGrid;
		if( !overHud( mouse ) && TheTacticalView->screenToTerrain( mouse, &world )
				&& slotUnder( world, LANEWAR_GRID_GROUND, LaneWarDirector::sizeFor( thing ), &col, &row, &onGrid ) && onGrid )
			sendOrder( LANEWAR_ORDER_PLACE, thing->getTemplateID(), col, row );
		dropHeld();
		return;
	}

	// a passenger let go on the hand is sold, anywhere else he stays where he is
	if( s_passengerOf != INVALID_ID )
	{
		const std::vector<LaneWarDirector::PlotUnit> &units = TheLaneWarDirector->getPlotUnits();
		for( size_t i = 0; i < units.size(); ++i )
			if( units[ i ].id == s_passengerOf && inside( s_layout.hand, mouse ) )
				sendOrder( LANEWAR_ORDER_SELL, units[ i ].grid, units[ i ].col, units[ i ].row, s_passenger );
		s_passengerOf = INVALID_ID;
		s_passenger = -1;
		return;
	}

	if( s_moving == INVALID_ID )
		return;
	const Object *obj = TheGameLogic->findObjectByID( s_moving );
	if( obj == NULL )
	{
		s_moving = INVALID_ID;
		return;
	}

	// on the hand: sold, the hero excepted, who is never for sale
	if( inside( s_layout.hand, mouse ) )
	{
		if( !LaneWar_isHero( s_player, obj->getTemplate() ) )
			sendOrder( LANEWAR_ORDER_SELL, s_moveGrid, s_moveCol, s_moveRow );
		s_moving = INVALID_ID;
		return;
	}

	Coord3D world;
	if( overHud( mouse ) || !TheTacticalView->screenToTerrain( mouse, &world ) )
		return;
	Int col, row;
	Bool onGrid;
	const ThingTemplate *thing = obj->getTemplate();
	if( !slotUnder( world, s_moveGrid, LaneWarDirector::sizeFor( thing ), &col, &row, &onGrid ) || !onGrid )
		return;
	// let go where it was picked up: it rides the pointer until the next press, a click and a click
	if( col == s_moveCol && row == s_moveRow )
		return;
	sendOrder( LANEWAR_ORDER_MOVE, s_moveGrid, s_moveCol, s_moveRow, col, row );
	s_moving = INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
Bool LaneHud_escape( void )
{
	if( !s_showing )
		return FALSE;
	const Bool any = s_armed != NULL || s_moving != INVALID_ID || s_upgradesOpen || s_abilitiesOpen;
	dropHeld();
	s_upgradesOpen = s_abilitiesOpen = FALSE;
	return any;
}

//-------------------------------------------------------------------------------------------------
Bool LaneHud_viewHome( void )
{
	if( !s_showing || !s_seated )
		return FALSE;
	lookAtPlot();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void LaneHud_describe( Int grid, Int col, Int row, char *out, Int size )
{
	Int x = -1, y = -1;
	ICoord2D corner[ 4 ];
	if( s_showing && s_seated && blockOnScreen( grid, col, row, 1, corner ) )
	{
		x = ( corner[ 0 ].x + corner[ 1 ].x + corner[ 2 ].x + corner[ 3 ].x ) / 4;
		y = ( corner[ 0 ].y + corner[ 1 ].y + corner[ 2 ].y + corner[ 3 ].y ) / 4;
	}
	Int used = snprintf( out, size, "\"team\":%d,\"seat\":%d,\"jets\":%d,\"slot\":[%d,%d],\"chips\":[", s_seated ? s_team : -1, s_seated ? s_seat : -1,
		s_jetsShown, x, y );
	for( Int c = 0; c < s_chipCount && used > 0 && used < size; ++c )
	{
		const LaneHudBox &b = s_chips[ c ].box;
		used += snprintf( out + used, size - used, "%s[%d,%d,%d,%d,%d]", c > 0 ? "," : "", s_chips[ c ].kind, b.x, b.y, b.w, b.h );
	}
	if( used > 0 && used < size )
		snprintf( out + used, size - used, "]" );
}

//-------------------------------------------------------------------------------------------------
void LaneHud_holdScoreboard( Bool held )
{
	s_scoreboard = held;
}
