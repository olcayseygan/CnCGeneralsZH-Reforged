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
// The Lane War HUD.  Where its boxes stand is LaneHudLayout.h; what is in them is read from
// TheLaneWarDirector, the players and their money.
//
// Thread: LOGIC-THREAD.md.  LaneHud_draw runs from InGameUI::postDraw and LaneHud_preDraw from
// InGameUI::preDraw, both at the render's sync point, and the presses come on the input path, which
// runs there too; so the director, the objects and the money are read here as GroupPanel reads its
// squads.  Nothing here writes the logic: a purchase, a sale, a tier, an upgrade and a move go out as
// MSG_LANEWAR_ORDER and LaneWarDirector checks each again (whyNotPlace is the same test the ghost
// is tinted by).
//
// Plates: the side's art is looked up as <Side>ProLane<Piece> (AmericaProLaneBuy, GlaProLaneTop) and
// a piece the sheets do not have yet is a flat fill, so the art drops in without a code change.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/LaneHud.h"
#include "GameClient/LaneHudLayout.h"

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

namespace
{
	// flat stand-ins until the side's plates exist, and what no plate draws
	const Color PLATE						= GameMakeColor( 22, 27, 33, 228 );
	const Color PLATE_EDGE			= GameMakeColor( 92, 106, 120, 255 );
	const Color PLATE_LIT				= GameMakeColor( 140, 152, 166, 255 );
	const Color KEY_FACE				= GameMakeColor( 36, 44, 54, 240 );
	const Color KEY_FACE_HOT		= GameMakeColor( 52, 64, 78, 245 );
	const Color KEY_FACE_LIT		= GameMakeColor( 40, 96, 74, 250 );
	const Color GREYED					= GameMakeColor( 0, 0, 0, 150 );
	const Color LANE_GROUND			= GameMakeColor( 128, 112, 78, 255 );
	const Color LANE_EDGE				= GameMakeColor( 58, 52, 40, 255 );
	const Color MIDDLE_BAND			= GameMakeColor( 62, 98, 54, 255 );
	const Color PLOT_FILL				= GameMakeColor( 40, 46, 40, 255 );
	const Color VIEW_BOX				= GameMakeColor( 240, 240, 240, 230 );
	const Color LEGAL						= GameMakeColor( 80, 230, 110, 255 );
	const Color BLOCKED					= GameMakeColor( 240, 60, 45, 255 );
	const Color PRICE						= GameMakeColor( 255, 214, 90, 255 );
	const Color DOWN						= GameMakeColor( 70, 70, 70, 255 );

	enum { TAB_UPGRADES = 3 };
	enum { TEXT_POOL = 256 };

	LaneHudLayout s_layout;
	Bool s_showing = FALSE;
	Bool s_seated = FALSE;
	Int s_team = 0, s_seat = 0;
	Player *s_player = NULL;
	Int s_tab = 0;

	// what the cells held at the last draw, for the press that comes after it
	const ThingTemplate *s_cellThing[ LANEHUD_CELLS ];
	const UpgradeTemplate *s_cellUpgrade[ LANEHUD_CELLS ];
	Int s_cellUpgradeIndex[ LANEHUD_CELLS ];
	const ThingTemplate *s_hero = NULL;

	// the modes: a unit armed for placing, a plot unit picked up, sell, the abilities panel
	const ThingTemplate *s_armed = NULL;
	ObjectID s_moving = INVALID_ID;
	Int s_moveGrid = 0, s_moveCol = 0, s_moveRow = 0;
	Bool s_sellMode = FALSE;
	Bool s_abilitiesOpen = FALSE;

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

	// the plot unit under the pointer in sell mode, -1 for none
	Int s_sellHover = -1;

	DisplayString *s_pool[ TEXT_POOL ];
	Int s_poolUsed = 0;

	enum Piece { PIECE_TOP, PIECE_MIDDLE, PIECE_ENEMY, PIECE_MAP, PIECE_BUY, PIECE_ECONOMY, PIECE_PANEL, PIECE_TAB, PIECE_TAB_ON,
		PIECE_CELL, PIECE_KEY, PIECE_KEY_ON, PIECE_COUNT };
	const char *const PIECE_NAME[ PIECE_COUNT ] = { "Top", "Middle", "Enemy", "Map", "Buy", "Economy", "Panel", "Tab", "TabOn",
		"Cell", "Key", "KeyOn" };
	AsciiString s_piecePrefix;
	const Image *s_pieces[ PIECE_COUNT ];
}

//-------------------------------------------------------------------------------------------------
// Small helpers
//-------------------------------------------------------------------------------------------------

/// a 1080 length at this screen's height, one pixel at least
static Int pxs( Real at1080 )
{
	const Int v = REAL_TO_INT( at1080 * s_layout.scale + 0.5f );
	return v < 1 ? 1 : v;
}

static Bool inside( const LaneHudBox &b, const ICoord2D *p )
{
	return b.contains( p->x, p->y );
}

/// a string of the pool, set to text in font; the pool starts over every draw
static DisplayString *pooled( const UnicodeString &text, GameFont *font )
{
	if( s_poolUsed >= TEXT_POOL )
		return NULL;
	DisplayString *&s = s_pool[ s_poolUsed++ ];
	if( s == NULL )
		s = TheDisplayStringManager->newDisplayString();
	if( s->getFont() != font )
		s->setFont( font );
	if( s->getText().compare( text ) != 0 )
		s->setText( text );
	return s;
}

/// text at x (align -1 from x, 0 centred on x, +1 ending at x), centred down the band y..y+h; its width
static Int drawText( const UnicodeString &text, GameFont *font, Int x, Int y, Int h, Color color, Int align = -1 )
{
	DisplayString *s = pooled( text, font );
	if( s == NULL )
		return 0;
	Int w = 0, th = 0;
	s->getSize( &w, &th );
	const Int lx = align < 0 ? x : align == 0 ? x - w / 2 : x - w;
	s->draw( lx, y + ( h - th ) / 2, color, UIStyle::SHADOW );
	return w;
}

static GameFont *smallFont( void ) { return UIStyle::readableFont( UIStyle::TEXT_SMALL ); }
static GameFont *bodyFont( void ) { return UIStyle::readableFont( UIStyle::TEXT_BODY ); }
static GameFont *strongFont( void ) { return UIStyle::readableFont( UIStyle::TEXT_STRONG, TRUE ); }
static GameFont *bigFont( void ) { return UIStyle::readableFont( UIStyle::TEXT_TITLE, TRUE ); }

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

//-------------------------------------------------------------------------------------------------
/** The side's prefix for its plates, as GroupPanel's: America, China, Gla. */
//-------------------------------------------------------------------------------------------------
static AsciiString sidePrefix( void )
{
	const AsciiString side = s_player != NULL ? s_player->getSide() : AsciiString( "America" );
	if( side.startsWith( "China" ) )
		return AsciiString( "China" );
	if( side.startsWith( "GLA" ) )
		return AsciiString( "Gla" );
	return AsciiString( "America" );
}

static const Image *piece( Piece which )
{
	const AsciiString prefix = sidePrefix();
	if( prefix != s_piecePrefix )
	{
		s_piecePrefix = prefix;
		for( Int i = 0; i < PIECE_COUNT; ++i )
		{
			AsciiString name;
			name.format( "%sProLane%s", prefix.str(), PIECE_NAME[ i ] );
			s_pieces[ i ] = TheMappedImageCollection->findImageByName( name );
		}
	}
	return s_pieces[ which ];
}

/// a box in its side's plate, or the flat stand-in
static void plate( const LaneHudBox &b, Piece which )
{
	const Image *art = piece( which );
	if( art != NULL )
	{
		TheDisplay->drawImage( art, b.x, b.y, b.x + b.w, b.y + b.h );
		return;
	}
	TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, PLATE );
	TheDisplay->drawFillRect( b.x + 1, b.y + 1, b.w - 2, 1, PLATE_LIT );
	TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, PLATE_EDGE );
}

/// a key: lit when its mode is on, brighter under the pointer
static void keyFace( const LaneHudBox &b, Bool lit, Bool enabled = TRUE )
{
	const Image *art = piece( lit ? PIECE_KEY_ON : PIECE_KEY );
	const ICoord2D *mouse = &TheMouse->getMouseStatus()->pos;
	const Bool hot = enabled && inside( b, mouse );
	if( art != NULL )
		TheDisplay->drawImage( art, b.x, b.y, b.x + b.w, b.y + b.h );
	else
	{
		TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, lit ? KEY_FACE_LIT : hot ? KEY_FACE_HOT : KEY_FACE );
		TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, hot || lit ? UIStyle::EDGE_LIT : PLATE_EDGE );
	}
	if( !enabled )
		TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, GREYED );
}

static void keyLabel( const LaneHudBox &b, const UnicodeString &text, Color color = UIStyle::INK )
{
	GameFont *font = bodyFont();
	drawText( UIStyle::fitText( text, font, b.w - pxs( 8 ) ), font, b.x + b.w / 2, b.y, b.h, color, 0 );
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

static void sendOrder( Int kind, Int a, Int b = -1, Int c = -1, Int d = -1, Int e = -1 )
{
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_LANEWAR_ORDER );
	msg->appendIntegerArgument( kind );
	const Int args[] = { a, b, c, d, e };
	const Int count = kind == LANEWAR_ORDER_UPGRADE ? 1 : kind == LANEWAR_ORDER_TIER ? 2 : kind == LANEWAR_ORDER_MOVE ? 5 : 3;
	for( Int i = 0; i < count; ++i )
		msg->appendIntegerArgument( args[ i ] );
}

static void dropModes( void )
{
	s_armed = NULL;
	s_moving = INVALID_ID;
	s_sellMode = FALSE;
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

/// the player's plot unit under a world point, on either grid; -1 for none
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

static Bool overHud( const ICoord2D *mouse )
{
	const LaneHudLayout &l = s_layout;
	if( inside( l.top, mouse ) || inside( l.middle, mouse ) || inside( l.map, mouse ) )
		return TRUE;
	if( s_seated && ( inside( l.buy, mouse ) || inside( l.economy, mouse ) || ( s_abilitiesOpen && inside( l.abilities, mouse ) ) ) )
		return TRUE;
	for( Int i = 0; i < LANEHUD_ENEMY_BOXES; ++i )
		if( inside( l.enemy[ i ], mouse ) )
			return TRUE;
	return FALSE;
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
/** Who sits where and whether the HUD is up: a lane map, a match, not the shell.  The local player
	* seated on a plot gets the whole HUD and the command bar goes; a watcher gets the read-only half. */
//-------------------------------------------------------------------------------------------------
static Bool refreshShowing( void )
{
	s_showing = FALSE;
	s_seated = FALSE;
	s_player = NULL;
	if( TheLaneWarDirector == NULL || !TheLaneWarDirector->isActive() || !TheGameLogic->isInGame()
			|| TheGameLogic->isInShellGame() || TheGameLogic->getFrame() == 0 )
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

//-------------------------------------------------------------------------------------------------
// The top band: each team's wave order, the countdown, the player's own turn
//-------------------------------------------------------------------------------------------------

/// the seat of a team whose wave went or goes on that tick, -1 for none
static Int seatOnTick( Int team, Int tick )
{
	if( tick < 0 )
		return -1;
	Bool occupied[ LANEWAR_SEATS ];
	for( Int s = 0; s < LANEWAR_SEATS; ++s )
		occupied[ s ] = TheLaneWarDirector->getSeatPlayer( team, s ) >= 0;
	return LaneWar_seatForTick( occupied, (UnsignedInt)tick );
}

/** A seat's chip: its colour down the left and its name, then under it the chip's word (Last, Now,
	* Next) on the left and the seat's income on the right. */
static void seatChip( const LaneHudBox &b, Int labelY, Int labelH, const char *word, Int team, Int seat, Bool now )
{
	TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, KEY_FACE );
	TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, now ? 2.0f : 1.0f, now ? UIStyle::ACTIVE : PLATE_EDGE );
	GameFont *font = smallFont();
	Player *p = seat >= 0 ? seatPlayer( team, seat ) : NULL;
	if( p == NULL )
	{
		drawText( UnicodeString( u"-" ), font, b.x + b.w / 2, b.y, b.h, UIStyle::MUTED, 0 );
		drawText( label( word ), font, b.x + b.w / 2, labelY, labelH, UIStyle::MUTED, 0 );
		return;
	}
	const Int stripe = pxs( 5 );
	TheDisplay->drawFillRect( b.x + 1, b.y + 1, stripe, b.h - 2, playerColor( team, seat ) );
	drawText( UIStyle::fitName( p->getPlayerDisplayName(), font, b.w - stripe - pxs( 8 ) ), font, b.x + stripe + pxs( 4 ), b.y, b.h, UIStyle::INK );
	UnicodeString pay;
	pay.format( u"+%u", TheLaneWarDirector->getIncome( team, seat ) );
	const Int payW = drawText( pay, font, b.x + b.w - pxs( 2 ), labelY, labelH, PRICE, 1 );
	drawText( UIStyle::fitText( label( word ), font, b.w - payW - pxs( 8 ) ), font, b.x + pxs( 2 ), labelY, labelH, UIStyle::MUTED );
}

/// wall seconds to the next spawn tick, rounded up
static Int secondsToTick( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	const UnsignedInt next = TheLaneWarDirector->getNextTickFrame();
	return next > frame ? (Int)( ( next - frame + LOGICFRAMES_PER_SECOND - 1 ) / LOGICFRAMES_PER_SECOND ) : 0;
}

/// three chips a team (the wave before, the wave out now, the next) and the countdown between them
static void drawTop( void )
{
	const LaneHudLayout &l = s_layout;
	plate( l.top, PIECE_TOP );

	const Int tick = (Int)TheLaneWarDirector->getTickCount();
	const Int ownTeam = s_team, enemyTeam = 1 - s_team;
	const Int chipW = pxs( 104 ), chipH = pxs( 28 ), gap = pxs( 4 );
	const Int chipY = l.top.y + pxs( 3 );
	const Int labelY = chipY + chipH;
	const Int labelH = l.top.y + l.top.h - labelY;
	const char *const ORDER[ 3 ] = { "GUI:LaneLast", "GUI:LaneNow", "GUI:LaneNext" };
	for( Int side = 0; side < 2; ++side )
	{
		const Int team = side == 0 ? ownTeam : enemyTeam;
		const Int x0 = side == 0 ? l.top.x + pxs( 6 ) : l.top.x + l.top.w - pxs( 6 ) - 3 * chipW - 2 * gap;
		for( Int k = 0; k < 3; ++k )
		{
			LaneHudBox chip = LaneHud_box( x0 + k * ( chipW + gap ), chipY, chipW, chipH );
			seatChip( chip, labelY, labelH, ORDER[ k ], team, seatOnTick( team, tick - 2 + k ), k == 1 );
		}
	}

	UnicodeString clock;
	clock.format( u"%d:%02d", secondsToTick() / 60, secondsToTick() % 60 );
	drawText( clock, bigFont(), l.top.x + l.top.w / 2, l.top.y, l.top.h, UIStyle::INK, 0 );
}

/// the player's own turn on the left half, who holds the middle on the right
static void drawMiddle( void )
{
	const LaneHudLayout &l = s_layout;
	plate( l.middle, PIECE_MIDDLE );
	const Int half = l.middle.w / 2;
	if( s_seated )
	{
		const Int tick = (Int)TheLaneWarDirector->getTickCount();
		Int ahead = -1;
		for( Int k = 0; k < LANEWAR_SEATS && ahead < 0; ++k )
			if( seatOnTick( s_team, tick + k ) == s_seat )
				ahead = k;
		UnicodeString turn;
		if( ahead == 0 )
			turn = label( "GUI:LaneYourWaveNext" );
		else if( ahead > 0 )
			turn.format( label( "GUI:LaneYourWaveIn" ).str(), secondsToTick() + ahead * LANEWAR_TICK_SECONDS );
		drawText( UIStyle::fitText( turn, smallFont(), half - pxs( 12 ) ), smallFont(), l.middle.x + half / 2, l.middle.y, l.middle.h,
			ahead == 0 ? UIStyle::ACTIVE : UIStyle::INK, 0 );
	}

	const Int holder = TheLaneWarDirector->getMiddleHolder();
	UnicodeString text;
	Color color = UIStyle::MUTED;
	if( holder < 0 )
		text = label( "GUI:LaneMiddleNobody" );
	else
	{
		text.format( label( holder == s_team ? "GUI:LaneMiddleYours" : "GUI:LaneMiddleTheirs" ).str(), (Int)LANEWAR_MIDDLE_BONUS_PERCENT );
		color = holder == s_team ? UIStyle::ACTIVE : BLOCKED;
	}
	TheDisplay->drawFillRect( l.middle.x + half, l.middle.y + 1, pxs( 5 ), l.middle.h - 2, holder < 0 ? UIStyle::MUTED : teamColor( holder ) );
	drawText( UIStyle::fitText( text, smallFont(), half - pxs( 16 ) ), smallFont(), l.middle.x + half + half / 2, l.middle.y, l.middle.h, color, 0 );
}

//-------------------------------------------------------------------------------------------------
// What a seat's next wave holds: one picture per kind of unit on its plot, in the order bought, with
// the count in its corner.  A grid to scale would give a unit four pixels in a box this short.
//-------------------------------------------------------------------------------------------------
static void drawMiniUnits( const LaneHudBox &area, Int team, Int seat )
{
	enum { MINI_ROWS = 2 };
	const Int side = ( area.h - pxs( 2 ) ) / MINI_ROWS;
	const Int perRow = max( 1, area.w / side );
	const ThingTemplate *kinds[ MINI_ROWS * 16 ];
	Int counts[ MINI_ROWS * 16 ];
	const Int most = min( perRow * MINI_ROWS, (Int)ARRAY_SIZE( kinds ) );
	Int used = 0;
	const std::vector<LaneWarDirector::PlotUnit> &units = TheLaneWarDirector->getPlotUnits();
	for( size_t i = 0; i < units.size(); ++i )
	{
		const LaneWarDirector::PlotUnit &u = units[ i ];
		const Object *obj = u.team == team && u.seat == seat ? TheGameLogic->findObjectByID( u.id ) : NULL;
		if( obj == NULL )
			continue;
		Int k = 0;
		while( k < used && kinds[ k ] != obj->getTemplate() )
			++k;
		if( k == used )
		{
			if( used == most )
				continue;
			kinds[ used ] = obj->getTemplate();
			counts[ used++ ] = 0;
		}
		++counts[ k ];
	}

	const Color owner = playerColor( team, seat );
	for( Int k = 0; k < used; ++k )
	{
		const Int x = area.x + ( k % perRow ) * side, y = area.y + ( k / perRow ) * ( side + pxs( 2 ) );
		const Image *picture = kinds[ k ]->getButtonImage();
		if( picture != NULL )
			TheDisplay->drawImage( picture, x + 1, y + 1, x + side - 1, y + side - 1 );
		else
			TheDisplay->drawFillRect( x + 1, y + 1, side - 2, side - 2, PLOT_FILL );
		TheDisplay->drawOpenRect( x, y, side, side, 1.0f, owner );
		if( counts[ k ] > 1 )
		{
			UnicodeString count;
			count.format( u"%d", counts[ k ] );
			drawText( count, smallFont(), x + side - pxs( 2 ), y + side / 2, side / 2, UIStyle::INK, 1 );
		}
	}
}

static void drawEnemies( void )
{
	const LaneHudLayout &l = s_layout;
	const Int team = 1 - s_team;
	Int box = 0;
	for( Int seat = 0; seat < LANEWAR_SEATS && box < LANEHUD_ENEMY_BOXES; ++seat )
	{
		Player *p = seatPlayer( team, seat );
		if( p == NULL )
			continue;
		const LaneHudBox &b = l.enemy[ box++ ];
		plate( b, PIECE_ENEMY );
		const Int headH = pxs( 20 );
		TheDisplay->drawFillRect( b.x + 1, b.y + 1, pxs( 5 ), headH, playerColor( team, seat ) );
		GameFont *font = smallFont();
		UnicodeString pay;
		pay.format( u"+%u  T%d", TheLaneWarDirector->getIncome( team, seat ), TheLaneWarDirector->getTechLevel( team, seat ) );
		const Int payW = drawText( pay, font, b.x + b.w - pxs( 4 ), b.y + 1, headH, PRICE, 1 );
		drawText( UIStyle::fitName( p->getPlayerDisplayName(), font, b.w - payW - pxs( 16 ) ), font, b.x + pxs( 9 ), b.y + 1, headH, UIStyle::INK );
		drawMiniUnits( LaneHud_box( b.x + pxs( 4 ), b.y + headH + pxs( 2 ), b.w - pxs( 8 ), b.h - headH - pxs( 4 ) ), team, seat );
	}
}

//-------------------------------------------------------------------------------------------------
// The strip map: the lane and the plots behind each end, squeezed across to fit, own end on the left
//-------------------------------------------------------------------------------------------------
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

/// a rectangle of the world on the strip map, cut to the lane's box
static void stripRect( const StripFrame &f, Real lx, Real ly, Real hx, Real hy, Color fill, Color edge, Int least = 0 )
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
		return;
	if( fill != 0 )
		TheDisplay->drawFillRect( x0, y0, x1 - x0, y1 - y0, fill );
	if( edge != 0 )
		TheDisplay->drawOpenRect( x0, y0, x1 - x0, y1 - y0, 1.0f, edge );
}

static void healthBar( Int x, Int y, Int w, Int h, const Object *obj, Color color )
{
	TheDisplay->drawFillRect( x, y, w, h, GameMakeColor( 0, 0, 0, 200 ) );
	const BodyModuleInterface *body = obj ? obj->getBodyModule() : NULL;
	if( body == NULL || obj->isEffectivelyDead() || body->getMaxHealth() <= 0.0f )
		return;
	const Real share = body->getHealth() / body->getMaxHealth();
	TheDisplay->drawFillRect( x + 1, y + 1, REAL_TO_INT( ( w - 2 ) * share ), h - 2, color );
}

static void drawStripMap( void )
{
	const LaneHudLayout &l = s_layout;
	plate( l.map, PIECE_MAP );
	StripFrame f;
	if( !stripFrame( &f ) )
		return;
	const LaneHudBox &lane = l.lane;
	TheDisplay->drawFillRect( lane.x, lane.y, lane.w, lane.h, LANE_GROUND );

	// the plots at the ends, the middle band
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			Coord2D lo, hi;
			if( TheLaneWarDirector->getPlotBounds( t, s, &lo, &hi ) )
				stripRect( f, lo.x, lo.y, hi.x, hi.y, PLOT_FILL, UIStyle::withAlpha( playerColor( t, s ), 200 ) );
		}
	const Real band = ( f.x1 - f.x0 ) * 0.02f;
	const Coord3D *middle = TheLaneWarDirector->getMiddle();
	stripRect( f, middle->x - band, f.y0, middle->x + band, f.y1, MIDDLE_BAND, 0 );
	TheDisplay->drawOpenRect( lane.x, lane.y, lane.w, lane.h, 1.0f, LANE_EDGE );

	// fortresses and towers in their team's colour; a tower down is a dark square, a fortress down crossed
	const Int towerSize = pxs( 7 );
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		const Color tint = teamColor( t );
		for( Int k = 0; k < 2; ++k )
		{
			const ObjectID id = TheLaneWarDirector->getTower( t, k );
			if( id == INVALID_ID )
				continue;
			const Object *tower = TheGameLogic->findObjectByID( id );
			const Bool up = tower != NULL && !tower->isEffectivelyDead();
			if( up )
			{
				const Coord3D *at = tower->getPosition();
				stripRect( f, at->x - 16, at->y - 16, at->x + 16, at->y + 16, tint, GameMakeColor( 0, 0, 0, 255 ), towerSize );
			}
		}
		const Object *fortress = TheGameLogic->findObjectByID( TheLaneWarDirector->getFortress( t ) );
		if( fortress != NULL )
		{
			const Coord3D *at = fortress->getPosition();
			const Bool down = fortress->isEffectivelyDead() || TheLaneWarDirector->isFortressDown( t );
			stripRect( f, at->x - 70, at->y - 70, at->x + 70, at->y + 70, down ? DOWN : tint, GameMakeColor( 0, 0, 0, 255 ), pxs( 12 ) );
		}
	}
	// a tower that is gone, where the map put it, as a dark outline: found by its last place the
	// director knows nothing of, so the outline stands where the live one stood at the last draw
	static Coord3D s_towerAt[ LANEWAR_TEAMS ][ 2 ];
	static Bool s_towerKnown[ LANEWAR_TEAMS ][ 2 ];
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int k = 0; k < 2; ++k )
		{
			const ObjectID id = TheLaneWarDirector->getTower( t, k );
			const Object *tower = id != INVALID_ID ? TheGameLogic->findObjectByID( id ) : NULL;
			if( tower != NULL && !tower->isEffectivelyDead() )
			{
				s_towerAt[ t ][ k ] = *tower->getPosition();
				s_towerKnown[ t ][ k ] = TRUE;
			}
			else if( id != INVALID_ID && s_towerKnown[ t ][ k ] )
			{
				const Coord3D &at = s_towerAt[ t ][ k ];
				stripRect( f, at.x - 16, at.y - 16, at.x + 16, at.y + 16, DOWN, BLOCKED, towerSize );
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

	// the two fortresses' health over the lane, the player's own on the left
	const Int barY = l.map.y + pxs( 5 ), barH = max( 4, lane.y - l.map.y - pxs( 9 ) ), barW = lane.w / 3;
	for( Int side = 0; side < 2; ++side )
	{
		const Int team = side == 0 ? s_team : 1 - s_team;
		const Object *fortress = TheGameLogic->findObjectByID( TheLaneWarDirector->getFortress( team ) );
		healthBar( side == 0 ? lane.x : lane.x + lane.w - barW, barY, barW, barH, fortress, teamColor( team ) );
	}
}

//-------------------------------------------------------------------------------------------------
// The buy panel: the tier tabs and the upgrades, fifteen cells, the hero beside them
//-------------------------------------------------------------------------------------------------
static Int localMoney( void )
{
	return s_player ? (Int)s_player->getMoney()->countMoney() : 0;
}

static Int techLevel( void )
{
	return TheLaneWarDirector->getTechLevel( s_team, s_seat );
}

/// a cell: the picture, its price at the foot, greyed when it cannot be bought now
static void drawCell( const LaneHudBox &c, const Image *picture, Int price, Bool can, Bool lit, const UnicodeString &stamp )
{
	keyFace( c, lit );
	const Int pad = pxs( 3 );
	const Int footH = pxs( 20 );
	if( picture != NULL )
		TheDisplay->drawImage( picture, c.x + pad, c.y + pad, c.x + c.w - pad, c.y + c.h - footH );
	TheDisplay->drawFillRect( c.x + 1, c.y + c.h - footH, c.w - 2, footH - 1, GameMakeColor( 0, 0, 0, 170 ) );
	if( price >= 0 )
		drawText( UIStyle::fitText( money( price ), smallFont(), c.w - 4, UIStyle::grouped( price ) ), smallFont(), c.x + c.w - 2,
			c.y + c.h - footH, footH, can ? PRICE : UIStyle::MUTED, 1 );
	if( !can )
		TheDisplay->drawFillRect( c.x, c.y, c.w, c.h - footH, GREYED );
	if( !stamp.isEmpty() )
		drawText( stamp, smallFont(), c.x + c.w / 2, c.y, c.h - footH, UIStyle::INK, 0 );
}

static void drawBuy( void )
{
	const LaneHudLayout &l = s_layout;
	plate( l.buy, PIECE_BUY );
	const Int cash = localMoney();
	const Int tech = techLevel();

	// the tabs: a locked tier says so with its price
	for( Int t = 0; t < LANEHUD_TABS; ++t )
	{
		const LaneHudBox &b = l.tabs[ t ];
		const Image *art = piece( s_tab == t ? PIECE_TAB_ON : PIECE_TAB );
		if( art != NULL )
			TheDisplay->drawImage( art, b.x, b.y, b.x + b.w, b.y + b.h );
		else
		{
			const Color text = UIStyle::tab( b.x, b.y, b.w, b.h, TRUE, s_tab == t, inside( b, &TheMouse->getMouseStatus()->pos ) );
			(void)text;
		}
		UnicodeString name;
		if( t == TAB_UPGRADES )
			name = label( "GUI:LaneUpgrades" );
		else
			name.format( label( "GUI:LaneTier" ).str(), t + 1 );
		const Bool locked = t != TAB_UPGRADES && t + 1 > tech;
		keyLabel( b, name, locked ? UIStyle::MUTED : s_tab == t ? UIStyle::ACTIVE : UIStyle::INK );
	}

	for( Int c = 0; c < LANEHUD_CELLS; ++c )
	{
		s_cellThing[ c ] = NULL;
		s_cellUpgrade[ c ] = NULL;
		s_cellUpgradeIndex[ c ] = -1;
	}

	UnicodeString info;
	Color infoColor = UIStyle::INK;
	const ICoord2D *mouse = &TheMouse->getMouseStatus()->pos;

	if( s_tab == TAB_UPGRADES )
	{
		const UpgradeTemplate *list[ LANEWAR_ROSTER_MAX ];
		const Int count = LaneWar_upgrades( s_player, list, LANEWAR_ROSTER_MAX );
		for( Int i = 0; i < count && i < LANEHUD_CELLS; ++i )
		{
			const UpgradeTemplate *u = list[ i ];
			const Int price = u->calcCostToBuild( s_player );
			const Bool done = s_player->hasUpgradeComplete( u );
			s_cellUpgrade[ i ] = u;
			s_cellUpgradeIndex[ i ] = i;
			drawCell( l.cells[ i ], u->getButtonImage(), done ? -1 : price, !done && cash >= price, FALSE,
								done ? label( "GUI:LaneOwned" ) : UnicodeString::TheEmptyString );
			if( inside( l.cells[ i ], mouse ) )
			{
				info = TheGameText->fetch( u->getDisplayNameLabel() );
				if( !done )
				{
					info.concat( u"  " );
					info.concat( money( price ) );
				}
			}
		}
	}
	else
	{
		LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
		const Int count = LaneWar_roster( s_player, roster, LANEWAR_ROSTER_MAX );
		Int cell = 0;
		const Bool locked = s_tab + 1 > tech;
		for( Int i = 0; i < count; ++i )
		{
			if( roster[ i ].hero || roster[ i ].tier != s_tab + 1 || cell >= LANEHUD_CELLS )
				continue;
			const ThingTemplate *thing = roster[ i ].thing;
			const Int price = thing->calcCostToBuild( s_player );
			s_cellThing[ cell ] = thing;
			drawCell( l.cells[ cell ], thing->getButtonImage(), price, !locked && cash >= price, s_armed == thing, UnicodeString::TheEmptyString );
			if( inside( l.cells[ cell ], mouse ) )
			{
				info = thing->getDisplayName();
				info.concat( u"  " );
				info.concat( money( price ) );
			}
			++cell;
		}

		// a locked tier: the foot is the key that buys it, when it is the next one
		if( locked )
		{
			const Int price = LaneWar_tierPrice( LANEWAR_TRACK_TECH, tech + 1 );
			const Bool next = s_tab + 1 == tech + 1;
			keyFace( l.info, FALSE, next && cash >= price );
			UnicodeString unlock;
			unlock.format( label( "GUI:LaneUnlock" ).str(), tech + 1, price );
			keyLabel( l.info, next ? unlock : label( "GUI:LaneLocked" ), next ? PRICE : UIStyle::MUTED );
			info.clear();
		}
	}

	// the hero, once a match
	s_hero = NULL;
	{
		LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
		const Int count = LaneWar_roster( s_player, roster, LANEWAR_ROSTER_MAX );
		for( Int i = 0; i < count && s_hero == NULL; ++i )
			if( roster[ i ].hero )
				s_hero = roster[ i ].thing;
	}
	const LaneHudBox &h = l.hero;
	if( s_hero != NULL )
	{
		const Bool bought = TheLaneWarDirector->isHeroBought( s_team, s_seat );
		const Int price = s_hero->calcCostToBuild( s_player );
		keyFace( h, s_armed == s_hero );
		const Int headH = pxs( 22 ), footH = pxs( 22 );
		drawText( label( "GUI:LaneHero" ), smallFont(), h.x + h.w / 2, h.y + 1, headH, UIStyle::ACTIVE, 0 );
		const Image *picture = s_hero->getButtonImage();
		if( picture != NULL )
		{
			const Int pw = h.w - pxs( 8 ), ph = pw * picture->getImageHeight() / max( 1, (Int)picture->getImageWidth() );
			const Int py = h.y + headH + ( h.h - headH - footH - ph ) / 2;
			TheDisplay->drawImage( picture, h.x + pxs( 4 ), py, h.x + pxs( 4 ) + pw, py + ph );
			if( bought || cash < price )
				TheDisplay->drawFillRect( h.x + pxs( 4 ), py, pw, ph, GREYED );
		}
		drawText( bought ? label( "GUI:LaneHeroPlaced" ) : money( price ), smallFont(), h.x + h.w / 2, h.y + h.h - footH, footH,
							bought ? UIStyle::ACTIVE : cash >= price ? PRICE : UIStyle::MUTED, 0 );
		if( inside( h, mouse ) )
			info = s_hero->getDisplayName();
	}

	// the foot: the unit under the pointer, or what the mode does
	if( !( s_tab != TAB_UPGRADES && s_tab + 1 > tech ) )
	{
		if( info.isEmpty() && s_armed != NULL )
		{
			info = label( "GUI:LanePlaceHint" );
			infoColor = UIStyle::MUTED;
		}
		else if( info.isEmpty() && s_sellMode )
		{
			info = label( "GUI:LaneSellHint" );
			infoColor = UIStyle::MUTED;
		}
		drawText( UIStyle::fitText( info, bodyFont(), l.info.w - pxs( 8 ) ), bodyFont(), l.info.x + pxs( 4 ), l.info.y, l.info.h, infoColor );
	}
}

//-------------------------------------------------------------------------------------------------
// The economy box and its keys
//-------------------------------------------------------------------------------------------------
static void drawEconomy( void )
{
	const LaneHudLayout &l = s_layout;
	plate( l.economy, PIECE_ECONOMY );
	const Int cash = localMoney();
	drawText( money( cash ), bigFont(), l.money.x + pxs( 6 ), l.money.y, l.money.h, PRICE );
	UnicodeString pay;
	pay.format( label( "GUI:LanePerWave" ).str(), TheLaneWarDirector->getIncome( s_team, s_seat ) );
	drawText( pay, bodyFont(), l.money.x + l.money.w - pxs( 6 ), l.money.y, l.money.h, UIStyle::INK, 1 );

	// income tier: level, the next one's price and what it adds
	const Int level = TheLaneWarDirector->getIncomeLevel( s_team, s_seat );
	const LaneHudBox &income = l.keys[ LANEHUD_KEY_INCOME ];
	const Bool maxed = level >= LANEWAR_INCOME_TIERS;
	const Int price = LaneWar_tierPrice( LANEWAR_TRACK_INCOME, level + 1 );
	keyFace( income, FALSE, !maxed && cash >= price );
	UnicodeString line1, line2;
	line1.format( label( "GUI:LaneIncome" ).str(), level, (Int)LANEWAR_INCOME_TIERS );
	if( maxed )
		line2 = label( "GUI:LaneMax" );
	else
		line2.format( u"%s  +%d", money( price ).str(), (Int)LANEWAR_INCOME_TIER_PAYS );
	const Int half = income.h / 2;
	drawText( line1, bodyFont(), income.x + income.w / 2, income.y + pxs( 2 ), half, UIStyle::INK, 0 );
	drawText( line2, smallFont(), income.x + income.w / 2, income.y + half - pxs( 2 ), half, maxed ? UIStyle::MUTED : PRICE, 0 );

	keyFace( l.keys[ LANEHUD_KEY_SELL ], s_sellMode );
	keyLabel( l.keys[ LANEHUD_KEY_SELL ], label( "GUI:LaneSell" ) );
	keyFace( l.keys[ LANEHUD_KEY_ABILITIES ], s_abilitiesOpen );
	keyLabel( l.keys[ LANEHUD_KEY_ABILITIES ], label( "GUI:LaneAbilities" ) );
	const char *const KEYS[ 4 ] = { "GUI:LanePlot", "GUI:LaneLane", "GUI:LaneMenu", "GUI:LaneDiplomacy" };
	for( Int k = 0; k < 4; ++k )
	{
		keyFace( l.keys[ LANEHUD_KEY_PLOT + k ], FALSE );
		keyLabel( l.keys[ LANEHUD_KEY_PLOT + k ], label( KEYS[ k ] ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** The abilities panel.  Empty on purpose: the job that fills it puts each roster unit's abilities
	* here as on/off toggles the wave units obey.  `inner` is the panel under its title; a click inside
	* it reaches abilitiesPanelClick, which returns TRUE when it used the click. */
//-------------------------------------------------------------------------------------------------
static void abilitiesPanelDraw( const LaneHudBox &inner )
{
	(void)inner;
}

static Bool abilitiesPanelClick( const LaneHudBox &inner, const ICoord2D *mouse )
{
	(void)inner;
	(void)mouse;
	return FALSE;
}

static LaneHudBox abilitiesInner( void )
{
	const LaneHudBox &b = s_layout.abilities;
	const Int headH = pxs( 34 );
	return LaneHud_box( b.x + pxs( 8 ), b.y + headH, b.w - pxs( 16 ), b.h - headH - pxs( 8 ) );
}

static void drawAbilities( void )
{
	if( !s_abilitiesOpen )
		return;
	const LaneHudBox &b = s_layout.abilities;
	plate( b, PIECE_PANEL );
	drawText( label( "GUI:LaneAbilities" ), strongFont(), b.x + pxs( 10 ), b.y + pxs( 2 ), pxs( 30 ), UIStyle::ACTIVE );
	UIStyle::rule( b.x + pxs( 8 ), b.y + pxs( 32 ), b.w - pxs( 16 ) );
	abilitiesPanelDraw( abilitiesInner() );
}

Bool LaneHud_abilitiesPanelIsOpen( void )
{
	return s_abilitiesOpen;
}

void LaneHud_abilitiesPanelSetOpen( Bool open )
{
	s_abilitiesOpen = open;
}

//-------------------------------------------------------------------------------------------------
// Over the world: the target block's outline, green or red, and in sell mode the unit under the
// pointer with what it would bring back
//-------------------------------------------------------------------------------------------------
static void drawWorldMarks( void )
{
	ICoord2D corner[ 4 ];
	if( s_target.on && s_target.onPlot && blockOnScreen( s_target.grid, s_target.col, s_target.row, s_target.size, corner ) )
		outline( corner, s_target.legal ? LEGAL : BLOCKED );

	if( s_sellMode && s_sellHover >= 0 && s_sellHover < (Int)TheLaneWarDirector->getPlotUnits().size() )
	{
		const LaneWarDirector::PlotUnit &u = TheLaneWarDirector->getPlotUnits()[ s_sellHover ];
		const Object *obj = TheGameLogic->findObjectByID( u.id );
		if( obj != NULL && blockOnScreen( u.grid, u.col, u.row, u.size, corner ) )
		{
			const Bool hero = LaneWar_isHero( s_player, obj->getTemplate() );
			outline( corner, hero ? BLOCKED : PRICE );
			UnicodeString text;
			if( !hero )
				text.format( label( "GUI:LaneRefund" ).str(), LaneWar_refund( u.cost, u.sent ) );
			const ICoord2D *mouse = &TheMouse->getMouseStatus()->pos;
			if( !text.isEmpty() )
				drawText( text, bodyFont(), mouse->x + pxs( 18 ), mouse->y - pxs( 26 ), pxs( 24 ), PRICE );
		}
	}
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
	s_sellHover = -1;
	if( !refreshShowing() || !s_seated )
	{
		if( !s_showing )
		{
			dropModes();
			s_abilitiesOpen = FALSE;
			s_tab = 0;
		}
		destroyGhost();
		return;
	}
	keepClassicBarDown();

	// a picked up unit that has gone, an armed unit the player can no longer pay for or a hero now bought
	const Object *moving = s_moving != INVALID_ID ? TheGameLogic->findObjectByID( s_moving ) : NULL;
	if( s_moving != INVALID_ID && moving == NULL )
		s_moving = INVALID_ID;
	if( s_armed != NULL && ( localMoney() < s_armed->calcCostToBuild( s_player )
			|| ( LaneWar_isHero( s_player, s_armed ) && TheLaneWarDirector->isHeroBought( s_team, s_seat ) ) ) )
		s_armed = NULL;

	const ICoord2D *mouse = &TheMouse->getMouseStatus()->pos;
	Coord3D world;
	const ThingTemplate *thing = s_armed != NULL ? s_armed : moving != NULL ? moving->getTemplate() : NULL;
	if( thing != NULL && !overHud( mouse ) && TheTacticalView->screenToTerrain( mouse, &world ) )
	{
		Target &t = s_target;
		t.thing = thing;
		t.grid = LaneWarDirector::gridFor( thing );
		t.size = LaneWarDirector::sizeFor( thing );
		if( slotUnder( world, t.grid, t.size, &t.col, &t.row, &t.onPlot ) )
		{
			t.on = TRUE;
			t.legal = TheLaneWarDirector->whyNotPlace( s_player, thing, t.col, t.row, moving ? moving->getID() : INVALID_ID ) == NULL;
			t.at = t.onPlot ? TheLaneWarDirector->slotCentre( s_team, s_seat, t.grid, t.col, t.row, t.size ) : world;
		}
	}
	if( s_sellMode && !overHud( mouse ) && TheTacticalView->screenToTerrain( mouse, &world ) )
		s_sellHover = plotUnitUnder( world );

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
	s_poolUsed = 0;
	if( !s_showing )
		return;
	drawTop();
	drawMiddle();
	drawEnemies();
	drawStripMap();
	if( !s_seated )
		return;
	drawBuy();
	drawEconomy();
	drawAbilities();
	drawWorldMarks();
}

//-------------------------------------------------------------------------------------------------
// Presses
//-------------------------------------------------------------------------------------------------

/// the camera to the middle of the player's own ground grid
static void lookAtPlot( void )
{
	Coord2D lo, hi;
	if( !TheLaneWarDirector->getPlotBounds( s_team, s_seat, &lo, &hi ) )
		return;
	Coord3D at;
	at.x = 0.5f * ( lo.x + hi.x );
	at.y = 0.5f * ( lo.y + hi.y );
	at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );
	TheTacticalView->lookAt( &at );
}

static Bool pressBuy( const ICoord2D *mouse )
{
	const LaneHudLayout &l = s_layout;
	for( Int t = 0; t < LANEHUD_TABS; ++t )
		if( inside( l.tabs[ t ], mouse ) )
		{
			s_tab = t;
			return TRUE;
		}

	const Int tech = techLevel();
	if( s_tab != TAB_UPGRADES && s_tab + 1 > tech && inside( l.info, mouse ) )
	{
		if( s_tab + 1 == tech + 1 )
			sendOrder( LANEWAR_ORDER_TIER, LANEWAR_TRACK_TECH, tech + 1 );
		return TRUE;
	}

	for( Int c = 0; c < LANEHUD_CELLS; ++c )
	{
		if( !inside( l.cells[ c ], mouse ) )
			continue;
		if( s_cellUpgrade[ c ] != NULL )
			sendOrder( LANEWAR_ORDER_UPGRADE, s_cellUpgradeIndex[ c ] );
		else if( s_cellThing[ c ] != NULL && s_tab + 1 <= tech )
		{
			s_armed = s_armed == s_cellThing[ c ] ? NULL : s_cellThing[ c ];
			s_moving = INVALID_ID;
			s_sellMode = FALSE;
		}
		return TRUE;
	}

	if( inside( l.hero, mouse ) && s_hero != NULL && !TheLaneWarDirector->isHeroBought( s_team, s_seat ) )
	{
		s_armed = s_armed == s_hero ? NULL : s_hero;
		s_moving = INVALID_ID;
		s_sellMode = FALSE;
	}
	return TRUE;
}

static Bool pressEconomy( const ICoord2D *mouse )
{
	const LaneHudLayout &l = s_layout;
	if( inside( l.keys[ LANEHUD_KEY_INCOME ], mouse ) )
	{
		const Int level = TheLaneWarDirector->getIncomeLevel( s_team, s_seat );
		if( level < LANEWAR_INCOME_TIERS )
			sendOrder( LANEWAR_ORDER_TIER, LANEWAR_TRACK_INCOME, level + 1 );
	}
	else if( inside( l.keys[ LANEHUD_KEY_SELL ], mouse ) )
	{
		const Bool on = !s_sellMode;
		dropModes();
		s_sellMode = on;
	}
	else if( inside( l.keys[ LANEHUD_KEY_ABILITIES ], mouse ) )
		s_abilitiesOpen = !s_abilitiesOpen;
	else if( inside( l.keys[ LANEHUD_KEY_PLOT ], mouse ) )
		lookAtPlot();
	else if( inside( l.keys[ LANEHUD_KEY_LANE ], mouse ) )
	{
		Coord3D at = *TheLaneWarDirector->getMiddle();
		at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );
		TheTacticalView->lookAt( &at );
	}
	else if( inside( l.keys[ LANEHUD_KEY_MENU ], mouse ) )
		TheMessageStream->appendMessage( GameMessage::MSG_META_OPTIONS );
	else if( inside( l.keys[ LANEHUD_KEY_DIPLOMACY ], mouse ) )
		TheMessageStream->appendMessage( GameMessage::MSG_META_DIPLOMACY );
	return TRUE;
}

Bool LaneHud_press( const ICoord2D *mouse, Bool right )
{
	if( !s_showing )
		return FALSE;
	const LaneHudLayout &l = s_layout;

	if( right )
	{
		// a right press takes back whatever is armed, and is the HUD's over any of its boxes
		const Bool armed = s_armed != NULL || s_moving != INVALID_ID || s_sellMode || s_abilitiesOpen;
		dropModes();
		s_abilitiesOpen = FALSE;
		return armed || overHud( mouse );
	}

	if( inside( l.map, mouse ) )
	{
		StripFrame f;
		if( inside( l.lane, mouse ) && stripFrame( &f ) )
		{
			const Coord3D at = fromStrip( f, mouse );
			TheTacticalView->lookAt( &at );
		}
		return TRUE;
	}
	if( !s_seated )
		return overHud( mouse );

	if( s_abilitiesOpen && inside( l.abilities, mouse ) )
	{
		abilitiesPanelClick( abilitiesInner(), mouse );
		return TRUE;
	}
	if( inside( l.buy, mouse ) )
		return pressBuy( mouse );
	if( inside( l.economy, mouse ) )
		return pressEconomy( mouse );
	if( overHud( mouse ) )
		return TRUE;

	// the world
	Coord3D world;
	if( !TheTacticalView->screenToTerrain( mouse, &world ) )
		return s_armed != NULL || s_moving != INVALID_ID || s_sellMode;

	if( s_armed != NULL )
	{
		Int col, row;
		Bool onGrid;
		const Int grid = LaneWarDirector::gridFor( s_armed );
		if( slotUnder( world, grid, LaneWarDirector::sizeFor( s_armed ), &col, &row, &onGrid ) && onGrid )
		{
			sendOrder( LANEWAR_ORDER_PLACE, s_armed->getTemplateID(), col, row );
			// the hero goes once; anything else stays armed while the money lasts (preDraw drops it)
			if( LaneWar_isHero( s_player, s_armed ) )
				s_armed = NULL;
		}
		return TRUE;
	}

	if( s_moving != INVALID_ID )
	{
		LaneHud_release( mouse );
		return TRUE;
	}

	const Int index = plotUnitUnder( world );
	if( s_sellMode )
	{
		if( index >= 0 )
		{
			const LaneWarDirector::PlotUnit &u = TheLaneWarDirector->getPlotUnits()[ index ];
			sendOrder( LANEWAR_ORDER_SELL, u.grid, u.col, u.row );
		}
		return TRUE;
	}

	// a press on one of the player's own plot units picks it up; letting go elsewhere puts it there
	if( index >= 0 )
	{
		const LaneWarDirector::PlotUnit &u = TheLaneWarDirector->getPlotUnits()[ index ];
		s_moving = u.id;
		s_moveGrid = u.grid;
		s_moveCol = u.col;
		s_moveRow = u.row;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void LaneHud_release( const ICoord2D *mouse )
{
	if( !s_showing || !s_seated || s_moving == INVALID_ID )
		return;
	const Object *obj = TheGameLogic->findObjectByID( s_moving );
	Coord3D world;
	if( obj == NULL || overHud( mouse ) || !TheTacticalView->screenToTerrain( mouse, &world ) )
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
	const Bool any = s_armed != NULL || s_moving != INVALID_ID || s_sellMode || s_abilitiesOpen;
	dropModes();
	s_abilitiesOpen = FALSE;
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
