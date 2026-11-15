// The airlift panel (AirliftPanel.h).  The side's build card just above the spot the power was aimed
// at, with a pointer down to a gold mark on it, following the spot as the camera moves, drawn the way
// the trainer draws its card (GameConsole.cpp): the tooltip card's nine pieces with its head band
// (<TooltipImage>TipHead and Tip), the title in the bar's "Generals" face and who the load is for in the
// band.  Under it the units the power carries as the command bar's build buttons, four a row: each the
// unit's cameo in the side's tray cell piece (<S>ProTrayCell) inside the build buttons' blue rim, its
// price and the count picked on HUD readouts over the cameo's corners (<S>ProHudCellPlate), its name
// under the cell, every name at one size; a picked one wears a gold ring, the one under the pointer the
// bar's lit overlay (Cameo_hilited).  A press on a cell adds one, a right or ctrl press takes one off,
// the wheel over it does the same up and down, and shift makes it five, as a build button queues.  A
// type the player could not build right now (no factory, a missing prerequisite, its cap) is grey and
// darkened, the bar's own disabled cameo, its count kept at 0, and with the pointer on it the line
// under the grid says why; a cell stops adding where the order would need more planes than the power
// flies.  Then the planes and the fee, the total against the bank in the money readout's own writing,
// and OK and Cancel on the Tab screen's Send pieces; OK is dark while the total is more than the bank.
// A piece the scheme lacks is a flat steel fill.  The rules are the logic's own
// (OCLSpecialPower::canAirlift, checkAirlift, findAirliftRecipient), read again once a logic frame.
//
// Thread: LOGIC-THREAD.md.  AirliftPanel_draw runs from InGameUI::postDraw at the render's sync
// point and the clicks and the wheel on the input path, which runs there too.  The logic hears of it
// only through the one message OK sends.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/AirliftPanel.h"

#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/OCLSpecialPower.h"

#include <stdio.h>
#include <vector>

namespace
{
	// lengths at 1080, put through the screen's height
	const Real CARD_PAD		= 14.0f;	///< the card's side margins inside its frame, the trainer's
	const Real HEAD_H			= 38.0f;	///< the head band, TipHead's height
	const Real SLOT_TOP		= 5.0f;		///< the name slot in TipHead's art, rows 5 to 27
	const Real SLOT_BOTTOM	= 27.0f;
	const Real HEAD_RIGHT	= 58.0f;	///< the band's text ends this far from the right, clear of its 48-wide corner (TIP_EDGE) and its bevel
	const Real TOP				= 46.0f;	///< the first row of cells, under the band
	const Real CELL_W			= 84.0f;	///< a unit's cell: the tray's cell piece round its build button
	const Real CELL_H			= 70.0f;
	const Real CAMEO_X		= 4.0f;		///< the button in it, cell-local, 76 by 60: the bar's 61 by 48 grown
	const Real CAMEO_Y		= 5.0f;
	const Real NAME_H			= 32.0f;	///< the unit's name under its cell, two rows of it
	const Real PICKED_RING	= 2.0f;		///< the gold ring round a picked unit's cameo
	const Real CELL_GAP		= 6.0f;		///< between cells, both ways
	const Real INFO_H			= 34.0f;	///< the line under the grid, two rows of text
	const Real INFO_GAP		= 8.0f;		///< between it and the planes line
	const Real LINE_H			= 20.0f;
	const Real BUTTON_W		= 120.0f;
	const Real BUTTON_H		= 30.0f;
	const Real BOTTOM_PAD	= 16.0f;
	const Real POINTER_H	= 22.0f;	///< the pointer from the panel's foot down to the spot
	const Real POINTER_W	= 20.0f;	///< its base
	const Real MARK				= 10.0f;	///< the diamond on the ground, half its width

	// type in points at 1080, Arial bold as the bar's markings and the build card, the title in the
	// "Generals" face of the bar's player name
	const Real TITLE_POINTS		= 15.0f;
	const Real HEAD_POINTS		= 11.0f;
	const Real NAME_POINTS		= 10.0f;
	const Real PRICE_POINTS		= 9.0f;
	const Real COUNT_POINTS		= 11.0f;
	const Real LINE_POINTS		= 11.0f;
	const Real TOTAL_POINTS		= 13.0f;
	const Real BUTTON_POINTS	= 13.0f;

	enum { MAX_ROWS = 12, COLUMNS = 4, LEAST_POINTS = 6 };

	// what no piece draws, and the stand-ins for a scheme without the card
	const Color STEEL			= GameMakeColor( 44, 52, 60, 240 );
	const Color STEEL_LIT	= GameMakeColor( 140, 152, 166, 255 );
	const Color HEAD			= GameMakeColor( 30, 36, 42, 245 );
	const Color KEYLINE		= GameMakeColor( 5, 7, 9, 255 );
	const Color WELL			= GameMakeColor( 6, 8, 10, 255 );
	const Color KEY_FACE	= GameMakeColor( 58, 66, 74, 255 );
	const Color KEY_DEAD	= GameMakeColor( 34, 38, 42, 255 );
	const Color TEXT			= GameMakeColor( 240, 240, 240, 255 );
	const Color TEXT_DEAD	= GameMakeColor( 120, 120, 116, 255 );
	const Color MUTED			= GameMakeColor( 200, 198, 192, 255 );		///< the talents' rejected text
	const Color GOLD			= GameMakeColor( 242, 194, 48, 255 );			///< the talents' waiting gold
	const Color RED				= GameMakeColor( 226, 76, 60, 255 );
	const Color SHADOW		= GameMakeColor( 0, 0, 0, 255 );
	const Color GOLD_LIT	= GameMakeColor( 255, 230, 130, 255 );		///< a picked cell's ring under the pointer
	const Color DEAD_VEIL	= GameMakeColor( 0, 0, 0, 120 );		///< over a grey cameo, so it reads darker than an idle one

	struct Row
	{
		const ThingTemplate *thing;
		Int count;
		Bool can;			///< the player could build one now
		Bool canMore;	///< one more would still fit
		Bool atLimit;	///< buildable, but one more would pass its cap
		IRegion2D cell;	///< the tray cell, set by the draw
		IRegion2D hit;	///< the cell and its name, what a press or the wheel takes
		Int nameTextH;	///< its name's height at the names' one size, namesPoints
	};

	Bool s_open = FALSE;
	ObjectID s_source = INVALID_ID;
	const SpecialPowerTemplate *s_power = NULL;
	Coord3D s_target;
	std::vector< Row > s_rows;
	IRegion2D s_box;
	IRegion2D s_ok;
	IRegion2D s_cancel;
	Bool s_okEnabled = FALSE;
	Int s_planes = 0;
	Int s_cost = 0;
	Int s_recipient = -1;
	Bool s_drawn = FALSE;		///< the boxes are from a draw since the panel opened
	UnsignedInt s_refreshed = 0;		///< one past the logic frame refresh last read, 0 for never
	Int s_namePoints = 0;				///< the names' one size, namesPoints
	Int s_namePointsFor = 0;		///< the well it was worked out for, 0 for none

	DisplayString *s_title = NULL;
	DisplayString *s_price[ MAX_ROWS ];
	DisplayString *s_count[ MAX_ROWS ];
	FittedLabel s_forWhom;
	FittedLabel s_names[ MAX_ROWS ];
	FittedLabel s_info;
	FittedLabel s_planesLine;
	FittedLabel s_feeLine;
	FittedLabel s_totalLine;
	FittedLabel s_okLabel;
	FittedLabel s_cancelLabel;
}

static Int px( Real at1080 )
{
	return max( REAL_TO_INT( at1080 * TheDisplay->getHeight() / 1080.0f + 0.5f ), 1 );
}

static Int points( Real at1080 )
{
	return max( REAL_TO_INT( at1080 * TheDisplay->getHeight() / 1080.0f + 0.5f ), (Int)LEAST_POINTS );
}

static DisplayString *keptString( DisplayString *&string, const UnicodeString &text, GameFont *font )
{
	if( string == NULL )
		string = TheDisplayStringManager->newDisplayString();
	if( string->getFont() != font )
		string->setFont( font );
	if( string->getText().compare( text ) != 0 )
		string->setText( text );
	return string;
}

static Bool inside( const ICoord2D *at, const IRegion2D &box )
{
	return at->x >= box.lo.x && at->x < box.hi.x && at->y >= box.lo.y && at->y < box.hi.y;
}

static void setBox( IRegion2D *box, Int x, Int y, Int w, Int h )
{
	box->lo.x = x;
	box->lo.y = y;
	box->hi.x = x + w;
	box->hi.y = y + h;
}

static void drawSteel( const IRegion2D &r, Color face )
{
	const Int w = r.hi.x - r.lo.x, h = r.hi.y - r.lo.y;
	if( w <= 2 || h <= 2 )
		return;
	TheDisplay->drawFillRect( r.lo.x, r.lo.y, w, h, face );
	TheDisplay->drawFillRect( r.lo.x + 1, r.lo.y + 1, w - 2, 1, STEEL_LIT );
	TheDisplay->drawOpenRect( r.lo.x, r.lo.y, w, h, 1.0f, KEYLINE );
}

/// <side><piece> from the side's button sheet, the side's name as InGameUI.cpp's hudCellStyle reads it
static const Image *sidePiece( Player *player, const char *piece )
{
	const AsciiString side = player->getSide();
	const char *prefix = "America";
	if( side.startsWith( "China" ) || side.startsWith( "Boss" ) )
		prefix = "China";
	else if( side.startsWith( "GLA" ) )
		prefix = "Gla";
	else if( side.startsWith( "Turkiye" ) )
		prefix = "Turkiye";
	AsciiString name;
	name.format( "%s%s", prefix, piece );
	return TheMappedImageCollection->findImageByName( name );
}

/// the Tab screen's button <TooltipImage>Tab<piece>, the trainer's; NULL under a scheme without it
static const Image *tabPiece( const char *piece )
{
	const ControlBarSchemeManager *schemes = TheControlBar ? TheControlBar->getControlBarSchemeManager() : NULL;
	return schemes ? schemes->getTabImage( piece ) : NULL;
}

/// the highest top of the command bar's three panels on screen, or the screen's foot while it is hidden
/// (InGameUI.cpp's controlBarTop, over every panel): the card stays above it, where clicks reach it
static Int commandBarTop( void )
{
	static NameKeyType barKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
	GameWindow *bar = TheWindowManager ? TheWindowManager->winGetWindowFromId( NULL, barKey ) : NULL;
	if( bar == NULL || bar->winIsHidden() || TheControlBar == NULL )
		return (Int)TheDisplay->getHeight();
	ICoord2D barPos;
	bar->winGetScreenPosition( &barPos.x, &barPos.y );
	Int top = (Int)TheDisplay->getHeight();
	for( Int panel = 0; panel < ControlBar::CB_PANEL_COUNT; ++panel )
		top = min( top, TheControlBar->getPanelRect( panel )->lo.y + barPos.y - TheControlBar->getPanelOrigin()->y );
	return max( top, 0 );
}

/// a readout on a corner of the cameo, set a point smaller until it fits, as a button's markings are
static void drawReadout( DisplayString *&string, const UnicodeString &text, Real at1080, const IRegion2D &cameo,
												 HudReadoutCorner corner, Color color, const Image *plate )
{
	Int p = points( at1080 );
	DisplayString *s = keptString( string, text, TheFontLibrary->getFont( "Arial", p, TRUE ) );
	while( p > LEAST_POINTS && !HudReadout_fits( s, cameo ) )
		s = keptString( string, text, TheFontLibrary->getFont( "Arial", --p, TRUE ) );
	HudReadout_draw( s, cameo, corner, color, plate );
}

/// OK or Cancel: the Tab screen's Send piece in its off, hover (H), pushed (P) or dead (D) state, the
/// label gold under the pointer, as the trainer's buttons
static void drawButton( const IRegion2D &r, FittedLabel &label, const UnicodeString &text, Bool enabled, Bool hover, Bool down )
{
	const Image *image = tabPiece( !enabled ? "SendD" : ( hover && down ? "SendP" : ( hover ? "SendH" : "Send" ) ) );
	if( image == NULL && enabled && hover )
		image = tabPiece( "SendH" );
	Color tint = 0xFFFFFFFF;
	if( image == NULL && ( image = tabPiece( "Send" ) ) != NULL && !enabled )
		tint = GameMakeColor( 110, 110, 110, 255 );
	if( image != NULL )
		TheDisplay->drawImage( image, r.lo.x, r.lo.y, r.hi.x, r.hi.y, tint );
	else
	{
		drawSteel( r, enabled ? KEY_FACE : KEY_DEAD );
		if( hover && enabled )
			TheDisplay->drawOpenRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, 1.0f, GOLD );
	}
	TheDisplay->flushBatch2D();
	IRegion2D well = r;
	well.lo.x += px( 4 );
	well.hi.x -= px( 4 );
	drawFittedLabel( label, text, points( BUTTON_POINTS ), well, !enabled ? TEXT_DEAD : ( hover ? GOLD : TEXT ), FALSE );
}

/// one size for every name under the cells: the largest at which each fits its well, wrapped to two
/// rows, so no name stands out smaller than its neighbours.  Worked out again only for a new panel
/// or a new screen
/// `text` as drawn in Arial bold at `p`, wrapped to `wrapW` (0 for one line): its width and height
static void measureText( const UnicodeString &text, Int p, Int wrapW, Int *w, Int *h )
{
	static DisplayString *measure = NULL;
	if( measure == NULL )
		measure = TheDisplayStringManager->newDisplayString();
	measure->setWordWrap( wrapW );
	measure->setWordWrapCentered( wrapW > 0 );
	measure->setFont( TheFontLibrary->getFont( "Arial", p, TRUE ) );
	measure->setText( text );
	*w = *h = 0;
	measure->getSize( w, h );
}

/// The names' one size, and each name's height at it (Row::nameTextH), so every name hangs from the
/// top of its well and a one-row name lines up with the first row of a two-row neighbour
static Int namesPoints( Int wellW, Int wellH )
{
	if( s_namePointsFor == wellW * 10000 + wellH )
		return s_namePoints;
	Int p = points( NAME_POINTS );
	for( ; p > LEAST_POINTS; --p )
	{
		Bool fits = TRUE;
		for( size_t i = 0; i < s_rows.size() && fits; ++i )
		{
			Int w, h;
			measureText( s_rows[ i ].thing->getDisplayName(), p, wellW, &w, &h );
			fits = w <= wellW && h <= wellH;
		}
		if( fits )
			break;
	}
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		Int w, h;
		measureText( s_rows[ i ].thing->getDisplayName(), p, wellW, &w, &h );
		s_rows[ i ].nameTextH = max( min( h, wellH ), 1 );
	}
	s_namePoints = p;
	s_namePointsFor = wellW * 10000 + wellH;
	return p;
}

/// `text` broken into lines no wider than `width` at `p`, a line feed where each break falls, so a
/// line never starts on the space it broke at.  The last answer is kept, as the hint comes every frame
static UnicodeString brokenLines( const UnicodeString &text, Int p, Int width )
{
	static UnicodeString lastIn, lastOut;
	static Int lastP = 0, lastWidth = 0;
	if( p == lastP && width == lastWidth && text.compare( lastIn ) == 0 )
		return lastOut;

	UnicodeString out, line, word;
	const Int length = text.getLength();
	for( Int i = 0; i <= length; ++i )
	{
		const WideChar c = i < length ? text.getCharAt( i ) : 0;
		if( c != 0 && c != u' ' && c != u'\n' )
		{
			word.concat( (WideChar)c );
			continue;
		}
		if( !word.isEmpty() )
		{
			UnicodeString tried = line;
			if( !tried.isEmpty() )
				tried.concat( (WideChar)u' ' );
			tried.concat( word );
			Int w, h;
			measureText( tried, p, 0, &w, &h );
			if( w > width && !line.isEmpty() )
			{
				out.concat( line );
				out.concat( (WideChar)u'\n' );
				line = word;
			}
			else
				line = tried;
			word.clear();
		}
		if( c == u'\n' && !line.isEmpty() )
		{
			out.concat( line );
			out.concat( (WideChar)u'\n' );
			line.clear();
		}
	}
	out.concat( line );

	lastIn = text;
	lastOut = out;
	lastP = p;
	lastWidth = width;
	return out;
}

/// why the player could not build `thing` now: the build card's "Requires:" line, its cap, or no factory
static UnicodeString whyNot( const ThingTemplate *thing, Player *player )
{
	UnicodeString needs;
	for( Int i = 0; i < thing->getPrereqCount(); ++i )
	{
		const UnicodeString missing = thing->getNthPrereq( i )->getRequiresList( player );
		if( missing.isEmpty() )
			continue;
		if( !needs.isEmpty() )
			needs.concat( u", " );
		needs.concat( missing );
	}
	UnicodeString text;
	if( !needs.isEmpty() )
		text.format( TheGameText->fetch( "CONTROLBAR:Requirements" ).str(), needs.str() );
	else if( !player->canBuildMoreOfType( thing, 1 ) )
		text = TheGameText->fetch( "GUI:AirliftAtLimit" );
	else
		text = TheGameText->fetch( "GUI:AirliftNoFactory" );
	return text;
}

//-------------------------------------------------------------------------------------------------
/** The power behind the panel, or NULL once it is gone: the source dead or handed over, the power
	* fired from elsewhere, the match over. */
//-------------------------------------------------------------------------------------------------
static OCLSpecialPower *airlift( Player **player )
{
	*player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( !s_open || TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || *player == NULL )
		return NULL;
	Object *source = TheGameLogic->findObjectByID( s_source );
	if( source == NULL || source->isEffectivelyDead() || source->getControllingPlayer() != *player )
		return NULL;
	OCLSpecialPower *sp = OCLSpecialPower::findAirlift( source, s_power );
	if( sp == NULL || !sp->isReady() )
		return NULL;
	return sp;
}

static OCLSpecialPower::AirliftOrder orderOf( Int bump = -1 )
{
	OCLSpecialPower::AirliftOrder order;
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		const Int count = s_rows[ i ].count + ( (Int)i == bump ? 1 : 0 );
		if( count <= 0 )
			continue;
		OCLSpecialPower::AirliftLine line;
		line.thing = s_rows[ i ].thing;
		line.count = count;
		order.push_back( line );
	}
	return order;
}

/// the rows' availability and the order's planes, cost and recipient, from the logic as it stands
static void refresh( OCLSpecialPower *sp, Player *player )
{
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		Row &row = s_rows[ i ];
		row.can = sp->canAirlift( player, row.thing, 1 );
		if( !row.can || !sp->canAirlift( player, row.thing, max( row.count, 1 ) ) )
			row.count = 0;
	}

	OCLSpecialPower::AirliftOrder order = orderOf();
	const OCLSpecialPower::AirliftCheck check = sp->checkAirlift( player, order, &s_planes, &s_cost, NULL );
	s_okEnabled = check == OCLSpecialPower::AIRLIFT_OK;
	Player *recipient = sp->findAirliftRecipient( &s_target );
	s_recipient = recipient ? recipient->getPlayerIndex() : -1;

	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		Row &row = s_rows[ i ];
		Int planes = 0, cost = 0;
		const OCLSpecialPower::AirliftCheck more = sp->checkAirlift( player, orderOf( (Int)i ), &planes, &cost, NULL );
		row.canMore = row.can && more != OCLSpecialPower::AIRLIFT_TOO_MANY_PLANES && more != OCLSpecialPower::AIRLIFT_NOT_BUILDABLE;
		row.atLimit = row.can && !sp->canAirlift( player, row.thing, row.count + 1 );
	}
	s_refreshed = TheGameLogic->getFrame() + 1;
}

/// `steps` more of row `i`, as far as the planes go, or fewer down to none
static void bump( size_t i, Int steps, OCLSpecialPower *sp, Player *player )
{
	Row &row = s_rows[ i ];
	if( steps < 0 )
		row.count = max( 0, row.count + steps );
	for( Int n = 0; n < steps && row.canMore; ++n )
	{
		++row.count;
		refresh( sp, player );
	}
	refresh( sp, player );
}

/// the row under the pointer, or -1
static Int rowAt( const ICoord2D *at )
{
	for( size_t i = 0; i < s_rows.size(); ++i )
		if( inside( at, s_rows[ i ].hit ) )
			return (Int)i;
	return -1;
}

//-------------------------------------------------------------------------------------------------
void AirliftPanel_open( const Object *source, const SpecialPowerTemplate *power, const Coord3D *target )
{
	if( source == NULL || power == NULL || target == NULL )
		return;
	OCLSpecialPower *sp = OCLSpecialPower::findAirlift( const_cast< Object * >( source ), power );
	if( sp == NULL )
		return;

	s_open = TRUE;
	s_drawn = FALSE;
	s_source = source->getID();
	s_power = power;
	s_target = *target;
	s_refreshed = 0;
	s_namePointsFor = 0;
	s_rows.clear();
	const std::vector< AsciiString > &units = sp->getAirliftData()->m_airliftUnits;
	for( std::vector< AsciiString >::const_iterator it = units.begin(); it != units.end() && s_rows.size() < MAX_ROWS; ++it )
	{
		const ThingTemplate *thing = TheThingFactory->findTemplate( *it );
		if( thing == NULL )
			continue;
		Row row;
		row.thing = thing;
		row.count = 0;
		row.can = FALSE;
		row.canMore = FALSE;
		row.atLimit = FALSE;
		row.nameTextH = 1;
		row.cell.lo.x = row.cell.lo.y = row.cell.hi.x = row.cell.hi.y = 0;
		row.hit = row.cell;
		s_rows.push_back( row );
	}
}

//-------------------------------------------------------------------------------------------------
Bool AirliftPanel_openForLocalPlayer( const Coord3D *target )
{
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( player == NULL || TheGameLogic == NULL )
		return FALSE;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->getControllingPlayer() != player || obj->isEffectivelyDead() )
			continue;
		for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
		{
			SpecialPowerModuleInterface *sp = (*m)->getSpecialPower();
			if( sp && OCLSpecialPower::findAirlift( obj, sp->getSpecialPowerTemplate() ) )
			{
				AirliftPanel_open( obj, sp->getSpecialPowerTemplate(), target );
				return TRUE;
			}
		}
	}
	return FALSE;
}

void AirliftPanel_close( void )
{
	s_open = FALSE;
	s_drawn = FALSE;
	s_rows.clear();
}

Bool AirliftPanel_isOpen( void )
{
	return s_open;
}

//-------------------------------------------------------------------------------------------------
void AirliftPanel_draw( void )
{
	Player *player = NULL;
	OCLSpecialPower *sp = airlift( &player );
	if( sp == NULL )
	{
		if( s_open )
			AirliftPanel_close();
		return;
	}
	// the logic moves on once a tick; a press or the wheel refreshes on its own change
	if( s_refreshed != TheGameLogic->getFrame() + 1 )
		refresh( sp, player );

	// the card's size: the grid of cells, four a row, then the info, the planes and the total, the buttons
	const Int count = (Int)s_rows.size();
	const Int columns = max( min( count, (Int)COLUMNS ), 1 );
	const Int gridRows = ( count + COLUMNS - 1 ) / COLUMNS;
	const Int pad = px( CARD_PAD ), cellW = px( CELL_W ), cellH = px( CELL_H ), nameH = px( NAME_H ), gap = px( CELL_GAP );
	const Int lineH = px( LINE_H ), infoH = px( INFO_H ), bh = px( BUTTON_H ), bw = px( BUTTON_W );
	const Int w = 2 * pad + COLUMNS * cellW + ( COLUMNS - 1 ) * gap;
	const Int gridH = gridRows > 0 ? gridRows * ( cellH + nameH ) + ( gridRows - 1 ) * gap : 0;
	const Int h = px( TOP ) + gridH + gap + infoH + px( INFO_GAP ) + lineH + lineH + gap + bh + px( BOTTOM_PAD );
	const Int tip = px( POINTER_H ), halfBase = px( POINTER_W ) / 2;

	// just above the spot, with a pointer down to it and a mark on the ground there.  The spot is projected
	// every frame, so the panel rides with the camera; when it leaves the screen the panel stays clamped
	// at the edge, still usable, and the pointer and the mark go until the spot comes back
	const Int screenW = (Int)TheDisplay->getWidth(), screenH = (Int)TheDisplay->getHeight();
	ICoord2D spot;
	spot.x = screenW / 2;
	spot.y = screenH / 2;
	Bool onScreen = FALSE;
	if( TheTacticalView )
	{
		ICoord2D projected;
		const View::WorldToScreenReturn seen = TheTacticalView->worldToScreenTriReturn( &s_target, &projected );
		if( seen != View::WTS_INVALID )
			spot = projected;
		onScreen = seen == View::WTS_INSIDE_FRUSTUM;
	}
	ICoord2D at;
	at.x = max( 0, min( spot.x - w / 2, screenW - w ) );
	at.y = max( 0, min( spot.y - tip - h, commandBarTop() - h ) );
	setBox( &s_box, at.x, at.y, w, h );

	ICoord2D mouse;
	mouse.x = mouse.y = -1;
	if( TheMouse )
		mouse = TheMouse->getMouseStatus()->pos;
	Int hovered = -1;		// the cell under the pointer, found as the cells are laid out

	if( onScreen )
	{
		// the mark: a gold diamond round the spot
		const Int m = px( MARK );
		TheDisplay->drawLine( spot.x - m, spot.y, spot.x, spot.y - m / 2, 2.0f, GOLD );
		TheDisplay->drawLine( spot.x, spot.y - m / 2, spot.x + m, spot.y, 2.0f, GOLD );
		TheDisplay->drawLine( spot.x + m, spot.y, spot.x, spot.y + m / 2, 2.0f, GOLD );
		TheDisplay->drawLine( spot.x, spot.y + m / 2, spot.x - m, spot.y, 2.0f, GOLD );

		// the pointer, from the panel's foot to the spot, while the panel is above it
		if( spot.y > s_box.hi.y )
		{
			const Int baseX = max( s_box.lo.x + halfBase, min( spot.x, s_box.hi.x - halfBase ) );
			for( Int bx = baseX - halfBase; bx <= baseX + halfBase; ++bx )
				TheDisplay->drawLine( bx, s_box.hi.y - 1, spot.x, spot.y - m / 2, 1.0f, STEEL );
			TheDisplay->drawLine( baseX - halfBase, s_box.hi.y - 1, spot.x, spot.y - m / 2, 1.0f, KEYLINE );
			TheDisplay->drawLine( baseX + halfBase, s_box.hi.y - 1, spot.x, spot.y - m / 2, 1.0f, KEYLINE );
		}
	}

	// the card: the tooltip card's nine pieces with the head row that has no figures' well, as the
	// trainer wears it, or a steel plate with a darker band
	const ControlBarSchemeManager *schemes = TheControlBar ? TheControlBar->getControlBarSchemeManager() : NULL;
	Bool framed = FALSE;
	if( schemes )
	{
		const ControlBarTooltipImages &card = schemes->getTooltipImages();
		const Bool head = card.head[ 0 ] && card.head[ 1 ] && card.head[ 2 ];
		const Image *frame[ 9 ] = { head ? card.head[ 0 ] : card.card[ 0 ], head ? card.head[ 1 ] : card.card[ 1 ],
			head ? card.head[ 2 ] : card.card[ 2 ], card.card[ 3 ], card.card[ 4 ], card.card[ 5 ], card.card[ 6 ], card.card[ 7 ], card.card[ 8 ] };
		framed = ControlBarDrawNinePieceBox( frame, s_box, TheDisplay->getHeight() / 1080.0f );
	}
	if( !framed )
	{
		drawSteel( s_box, STEEL );
		TheDisplay->drawFillRect( at.x + 1, at.y + 2, w - 2, px( HEAD_H ) - 2, HEAD );
	}

	// the head band: the power's name, and who the load lands for
	UnicodeString title, description;
	if( s_power->getRequiredScience() == SCIENCE_INVALID || !TheScienceStore->getNameAndDescription( s_power->getRequiredScience(), title, description ) )
		title = TheGameText->fetch( "GUI:AirliftTitle" );
	Player *recipient = s_recipient >= 0 ? ThePlayerList->getNthPlayer( s_recipient ) : NULL;
	UnicodeString forWhom;
	if( recipient )
		forWhom.format( TheGameText->fetch( "GUI:AirliftFor" ).str(), recipient->getPlayerDisplayName().str() );

	TheDisplay->flushBatch2D();
	const Int slotTop = at.y + px( SLOT_TOP ), slotH = px( SLOT_BOTTOM ) - px( SLOT_TOP );
	const Int headRight = at.x + w - px( HEAD_RIGHT );
	const Int titleRoom = ( headRight - at.x - pad ) * 3 / 5;
	Int titlePoints = points( TITLE_POINTS );
	DisplayString *titleString = keptString( s_title, title, TheFontLibrary->getFont( "Generals", titlePoints, TRUE ) );
	Int titleW = 0, titleH = 0;
	titleString->getSize( &titleW, &titleH );
	while( ( titleW > titleRoom || titleH > slotH ) && titlePoints > LEAST_POINTS )
	{
		titleString = keptString( s_title, title, TheFontLibrary->getFont( "Generals", --titlePoints, TRUE ) );
		titleString->getSize( &titleW, &titleH );
	}
	titleString->draw( at.x + pad + px( 4 ), slotTop + ( slotH - titleH ) / 2, GOLD, SHADOW );
	IRegion2D forWell;
	const Int forLeft = at.x + pad + px( 4 ) + titleW + px( 12 );
	setBox( &forWell, forLeft, slotTop, max( headRight - forLeft, 1 ), slotH );
	drawFittedLabel( s_forWhom, forWhom, points( HEAD_POINTS ), forWell, recipient == player ? TEXT : GOLD, FALSE, FITTED_ALIGN_RIGHT );

	// the cells, centred on the card
	const Image *const *tray = schemes ? schemes->getPowerTray() : NULL;
	const Image *trayCell = tray ? tray[ ControlBarScheme::TRAY_CELL ] : NULL;
	const Image *plate = sidePiece( player, "ProHudCellPlate" );
	static const Image *lit = TheMappedImageCollection->findImageByName( "Cameo_hilited" );
	const Int namePoints = namesPoints( cellW, nameH );
	const Color rim = TheControlBar ? TheControlBar->getBuildBorderColor() : GAME_COLOR_UNDEFINED;
	const Int gridLeft = at.x + ( w - ( columns * cellW + ( columns - 1 ) * gap ) ) / 2;
	const Int gridTop = at.y + px( TOP );
	for( Int i = 0; i < count; ++i )
	{
		Row &row = s_rows[ i ];
		const Int x = gridLeft + ( i % COLUMNS ) * ( cellW + gap );
		const Int y = gridTop + ( i / COLUMNS ) * ( cellH + nameH + gap );
		setBox( &row.cell, x, y, cellW, cellH );
		setBox( &row.hit, x, y, cellW, cellH + nameH );
		if( inside( &mouse, row.hit ) )
			hovered = i;
		IRegion2D cameo;
		setBox( &cameo, x + px( CAMEO_X ), y + px( CAMEO_Y ), cellW - 2 * px( CAMEO_X ), cellH - 2 * px( CAMEO_Y ) );

		if( trayCell )
			TheDisplay->drawImage( trayCell, row.cell.lo.x, row.cell.lo.y, row.cell.hi.x, row.cell.hi.y );
		else
		{
			TheDisplay->drawFillRect( x, y, cellW, cellH, WELL );
			TheDisplay->drawOpenRect( x, y, cellW, cellH, 1.0f, STEEL_LIT );
		}

		// the cameo as the bar draws a build button, three ways apart at a glance: picked is the clear
		// picture in a gold ring, idle the build buttons' rim (lit under the pointer), and one that could
		// not be built grey and darkened
		const Int cameoW = cameo.hi.x - cameo.lo.x, cameoH = cameo.hi.y - cameo.lo.y;
		const Image *picture = row.thing->getButtonImage() ? row.thing->getButtonImage() : row.thing->getSelectedPortraitImage();
		if( picture )
			TheDisplay->drawImage( picture, cameo.lo.x, cameo.lo.y, cameo.hi.x, cameo.hi.y, 0xFFFFFFFF,
														 row.can ? Display::DRAW_IMAGE_ALPHA : Display::DRAW_IMAGE_GRAYSCALE );
		else
			TheDisplay->drawFillRect( cameo.lo.x, cameo.lo.y, cameoW, cameoH, KEYLINE );
		// a picked cell under the pointer keeps its picture clear and lights its ring instead
		if( !row.can )
			TheDisplay->drawFillRect( cameo.lo.x, cameo.lo.y, cameoW, cameoH, DEAD_VEIL );
		else if( i == hovered && lit && row.count <= 0 )
			TheDisplay->drawImage( lit, cameo.lo.x, cameo.lo.y, cameo.hi.x, cameo.hi.y );
		if( row.count > 0 )
		{
			const Int ring = px( PICKED_RING );
			const Color gold = i == hovered ? GOLD_LIT : GOLD;
			TheDisplay->drawFillRect( cameo.lo.x - ring, cameo.lo.y - ring, cameoW + 2 * ring, ring, gold );
			TheDisplay->drawFillRect( cameo.lo.x - ring, cameo.hi.y, cameoW + 2 * ring, ring, gold );
			TheDisplay->drawFillRect( cameo.lo.x - ring, cameo.lo.y, ring, cameoH, gold );
			TheDisplay->drawFillRect( cameo.hi.x, cameo.lo.y, ring, cameoH, gold );
		}
		else if( rim != GAME_COLOR_UNDEFINED )
			TheDisplay->drawOpenRect( cameo.lo.x - 1, cameo.lo.y - 1, cameoW + 2, cameoH + 2, 1.0f, row.can ? rim : KEYLINE );
		TheDisplay->flushBatch2D();

		// the price top left, the count picked bottom right, the name under the cell
		UnicodeString text;
		text.format( u"$%d", row.thing->calcCostToBuild( player ) );
		drawReadout( s_price[ i ], text, PRICE_POINTS, cameo, HUD_READOUT_TOP_LEFT, row.can ? GOLD : TEXT_DEAD, plate );
		if( row.count > 0 )
		{
			text.format( u"x%d", row.count );
			drawReadout( s_count[ i ], text, COUNT_POINTS, cameo, HUD_READOUT_BOTTOM_RIGHT, TEXT, plate );
		}
		// as tall as the name itself, so it hangs from the well's top
		IRegion2D nameWell;
		setBox( &nameWell, x, y + cellH + px( 2 ), cellW, row.nameTextH );
		drawFittedLabel( s_names[ i ], row.thing->getDisplayName(), namePoints, nameWell,
										 row.can ? ( i == hovered ? GOLD : TEXT ) : TEXT_DEAD, TRUE );
	}

	// under the grid: why the cell under the pointer is grey, that one more passes its cap or the
	// planes are full, or how to pick
	Int y = gridTop + gridH + gap;
	UnicodeString info;
	Color infoColor = MUTED;
	if( hovered >= 0 && !s_rows[ hovered ].can )
	{
		info = whyNot( s_rows[ hovered ].thing, player );
		infoColor = RED;
	}
	else if( hovered >= 0 && s_rows[ hovered ].atLimit )
	{
		info = TheGameText->fetch( "GUI:AirliftAtLimit" );
		infoColor = RED;
	}
	else if( hovered >= 0 && !s_rows[ hovered ].canMore )
	{
		info = TheGameText->fetch( "GUI:AirliftPlanesFull" );
		infoColor = RED;
	}
	else
		info = TheGameText->fetch( "GUI:AirliftHint" );
	IRegion2D infoWell;
	setBox( &infoWell, at.x + pad, y, w - 2 * pad, infoH );
	drawFittedLabel( s_info, brokenLines( info, points( LINE_POINTS ), w - 2 * pad ), points( LINE_POINTS ), infoWell, infoColor, TRUE );
	y += infoH + px( INFO_GAP );

	// the planes on the left and the fee in its own column on the right, then the total against the
	// bank; the money is written as the bar's money readout writes it
	const Int fee = s_planes * sp->getAirliftData()->m_airliftFeePerPlane;
	UnicodeString planes, feeText, total;
	planes.format( TheGameText->fetch( "GUI:AirliftPlanes" ).str(), s_planes, sp->getAirliftData()->m_airliftMaxPlanes );
	feeText.format( TheGameText->fetch( "GUI:AirliftFee" ).str(), HudMoney_format( fee ).str() );
	total.format( TheGameText->fetch( "GUI:AirliftTotal" ).str(), HudMoney_format( s_cost ).str() );
	const Bool broke = s_cost > (Int)player->getMoney()->countMoney();
	const Int textLeft = at.x + pad + px( 4 ), textRight = at.x + w - pad - px( 4 );
	IRegion2D well;
	setBox( &well, textLeft, y, ( textRight - textLeft ) / 2, lineH );
	drawFittedLabel( s_planesLine, planes, points( LINE_POINTS ), well, TEXT, FALSE, FITTED_ALIGN_LEFT );
	setBox( &well, textLeft + ( textRight - textLeft ) / 2, y, ( textRight - textLeft ) / 2, lineH );
	drawFittedLabel( s_feeLine, feeText, points( LINE_POINTS ), well, TEXT, FALSE, FITTED_ALIGN_RIGHT );
	y += lineH;
	setBox( &well, textLeft, y, textRight - textLeft, lineH );
	drawFittedLabel( s_totalLine, total, points( TOTAL_POINTS ), well, broke ? RED : GOLD, FALSE, FITTED_ALIGN_RIGHT );
	y += lineH + gap;

	// OK and Cancel, flush right
	setBox( &s_cancel, at.x + w - pad - bw, y, bw, bh );
	setBox( &s_ok, s_cancel.lo.x - px( 8 ) - bw, y, bw, bh );
	const Bool down = TheMouse && TheMouse->getMouseStatus()->leftState != MBS_Up;
	drawButton( s_ok, s_okLabel, TheGameText->fetch( "GUI:AirliftOK" ), s_okEnabled, inside( &mouse, s_ok ), down );
	drawButton( s_cancel, s_cancelLabel, TheGameText->fetch( "GUI:AirliftCancel" ), TRUE, inside( &mouse, s_cancel ), down );
	s_drawn = TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool AirliftPanel_click( const ICoord2D *mouse, Bool cancel )
{
	if( !s_open || !s_drawn || !inside( mouse, s_box ) )
		return FALSE;

	Player *player = NULL;
	OCLSpecialPower *sp = airlift( &player );
	if( sp == NULL )
	{
		AirliftPanel_close();
		return TRUE;
	}

	// a press on a cell adds, a right or ctrl press takes off, five with shift, as a build button queues.
	// `cancel` is a right press or a ctrl left one; with ctrl down it is taken for the left
	const Int step = TheKeyboard && TheKeyboard->isShift() ? 5 : 1;
	const Int row = rowAt( mouse );
	if( row >= 0 )
	{
		bump( row, cancel ? -step : step, sp, player );
		return TRUE;
	}

	// a right press anywhere else on the panel closes it, so no order lands on the ground under it
	const Bool rightPress = cancel && !( TheKeyboard && TheKeyboard->isCtrl() );
	if( rightPress || inside( mouse, s_cancel ) )
	{
		AirliftPanel_close();
		return TRUE;
	}

	if( inside( mouse, s_ok ) && s_okEnabled )
	{
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_AIRLIFT_ORDER );
		msg->appendIntegerArgument( s_power->getID() );
		msg->appendLocationArgument( s_target );
		msg->appendObjectIDArgument( s_source );
		for( size_t i = 0; i < s_rows.size(); ++i )
		{
			if( s_rows[ i ].count <= 0 )
				continue;
			msg->appendIntegerArgument( s_rows[ i ].thing->getTemplateID() );
			msg->appendIntegerArgument( s_rows[ i ].count );
		}
		AirliftPanel_close();
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool AirliftPanel_wheel( const ICoord2D *mouse, Bool up )
{
	if( !s_open || !s_drawn || !inside( mouse, s_box ) )
		return FALSE;

	Player *player = NULL;
	OCLSpecialPower *sp = airlift( &player );
	if( sp == NULL )
	{
		AirliftPanel_close();
		return FALSE;
	}

	// over a cell up adds and down takes off, five with shift; anywhere on the panel the camera stays
	const Int row = rowAt( mouse );
	if( row >= 0 )
	{
		const Int step = TheKeyboard && TheKeyboard->isShift() ? 5 : 1;
		bump( row, up ? step : -step, sp, player );
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AirliftPanel_describe( char *out, Int size )
{
	auto centre = []( const IRegion2D &r, char *buf, size_t n )
	{
		snprintf( buf, n, "[%d,%d]", ( r.lo.x + r.hi.x ) / 2, ( r.lo.y + r.hi.y ) / 2 );
	};

	Int used = snprintf( out, size, "\"open\":%s", s_open && s_drawn ? "true" : "false" );
	if( !s_open || !s_drawn || used >= size )
		return;

	char ok[ 32 ], cancelAt[ 32 ];
	centre( s_ok, ok, sizeof( ok ) );
	centre( s_cancel, cancelAt, sizeof( cancelAt ) );
	used += snprintf( out + used, size - used, ",\"forPlayer\":%d,\"planes\":%d,\"cost\":%d,\"okEnabled\":%s,\"okButton\":%s,\"cancelButton\":%s,\"rows\":[",
										s_recipient, s_planes, s_cost, s_okEnabled ? "true" : "false", ok, cancelAt );
	// a left click on a cell adds one, a right click takes one off
	for( size_t i = 0; i < s_rows.size() && used < size; ++i )
	{
		char cell[ 32 ];
		centre( s_rows[ i ].cell, cell, sizeof( cell ) );
		used += snprintf( out + used, size - used, "%s{\"unit\":\"%s\",\"count\":%d,\"buildable\":%s,\"canMore\":%s,\"cell\":%s}",
											i ? "," : "", s_rows[ i ].thing->getName().str(), s_rows[ i ].count, s_rows[ i ].can ? "true" : "false",
											s_rows[ i ].canMore ? "true" : "false", cell );
	}
	if( used < size )
		snprintf( out + used, size - used, "]" );
}
