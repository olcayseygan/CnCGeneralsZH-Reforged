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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: ReplayMenu.cpp /////////////////////////////////////////////////////////////////////
// Author: Chris The masta Huybregts, December 2001
// Description: Replay Menus
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/Image.h"
#include "GameClient/TankSkin.h"
#include "GameClient/UIStyle.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"

#include <algorithm>
#include <chrono>
#include <math.h>

// window ids -------------------------------------------------------------------------------------
static NameKeyType parentWindowID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType windowLadderID = NAMEKEY_INVALID;


// window pointers --------------------------------------------------------------------------------
static GameWindow *parentWindow = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *windowLadder = NULL;

/// the screen a tab asked for, pushed over the hub once this one is popped; NULL for Back and Home
static const char *nextScreen = NULL;

GameWindow *WOLMakeButtonLike( GameWindow *parent, GameWindow *model, Int x, Int y, Int w, Int h, const char *label )
{
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	WinInstanceData *look = model ? model->winGetInstanceData() : NULL;
	if (look)
	{
		for (Int i = 0; i < MAX_DRAW_DATA; ++i)
		{
			inst.m_enabledDrawData[i] = look->m_enabledDrawData[i];
			inst.m_disabledDrawData[i] = look->m_disabledDrawData[i];
			inst.m_hiliteDrawData[i] = look->m_hiliteDrawData[i];
		}
		inst.m_enabledText = look->m_enabledText;
		inst.m_disabledText = look->m_disabledText;
		inst.m_hiliteText = look->m_hiliteText;
	}
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( parent,
		WIN_STATUS_ENABLED | (look ? WIN_STATUS_IMAGE : 0), x, y, w, h, &inst,
		model ? model->winGetFont() : NULL, look == NULL );
	if (button)
		GadgetButtonSetText( button, TheGameText->fetch( label ) );
	return button;
}

/// a length across the content: by the height's scale, so a box holds its words (UIStyle::tallPx)
static Int across( Real at1080 )
{
	return UIStyle::tallPx( at1080 );
}

/// a length down the content: by the board's scale, as the band itself is laid out, so rows and
/// boxes fit it on every screen shape; the words keep their size and rowFont fits them to a row
static Int down( Real at1080 )
{
	return UIStyle::px( at1080 );
}

/// Arial at body size, or smaller down to the readable floor, so a row band holds it: 4:3 and 5:4
/// squeeze the band while the type keeps 1080's size
static GameFont *rowFont( Int band, Bool bold = FALSE )
{
	GameFont *font = UIStyle::readableFont( UIStyle::TEXT_BODY, bold );
	for (Int points = UIStyle::TEXT_BODY - 1; font && font->height > band && points >= UIStyle::TEXT_SMALL; --points)
	{
		GameFont *smaller = UIStyle::readableFont( points, bold );
		if (smaller == font)
			break;		// the readable floor
		font = smaller;
	}
	return font;
}

static AsciiString localNick( void )
{
	return TheGameSpyInfo ? TheGameSpyInfo->getLocalName() : AsciiString::TheEmptyString;
}

static UnicodeString number( Int value )
{
	UnicodeString text;
	text.format( u"%d", value );
	return text;
}

/// a button on the room's row from board x left, width wide on the board, as tall as Back
static GameWindow *rowButton( Real left, Real width, const char *label )
{
	if (!buttonBack)
		return NULL;
	Int x, y, w, h;
	buttonBack->winGetScreenPosition( &x, &y );
	buttonBack->winGetSize( &w, &h );
	return WOLMakeButtonLike( parentWindow, buttonBack, UIStyle::boardX( left ), y, UIStyle::px( width ), h, label );
}

/// a small button in a side box (Jump to my page, Open zerohour.gg): the mockups' 40 high, its label 21
static GameWindow *smallButton( Int x, Int y, Int width, const char *label )
{
	GameWindow *button = WOLMakeButtonLike( parentWindow, buttonBack, x, y, width, down( 40.0f ), label );
	if (button)
		button->winSetFont( UIStyle::titleFont( 11 ) );
	return button;
}

//-------------------------------------------------------------------------------------------------
// What the panel shows (ONLINE-UI-REDESIGN.md "Ladder" and "Skins"): the boxes, rows, pictures and
// words of the open page, laid out on the online pages' grid whenever its data changes and drawn
// every frame by drawPanel, the master's WindowLadder draw.  The buttons stay gadgets over it.
//-------------------------------------------------------------------------------------------------
enum PieceKind { PIECE_FILL, PIECE_LINE, PIECE_IMAGE, PIECE_WORDS };
struct Piece
{
	PieceKind kind;
	Int x, y, width, height;
	Color color;
	const Image *image;
	DisplayString *words;
};
static std::vector<Piece> s_pieces;

static void clearPieces( void )
{
	for (size_t i = 0; i < s_pieces.size(); ++i)
		if (s_pieces[i].words)
			TheDisplayStringManager->freeDisplayString( s_pieces[i].words );
	s_pieces.clear();
}

static void addPiece( PieceKind kind, Int x, Int y, Int width, Int height, Color color, const Image *image = NULL, DisplayString *words = NULL )
{
	Piece piece = { kind, x, y, width, height, color, image, words };
	s_pieces.push_back( piece );
}

/// a box: the panel's black under a FRAME edge, as the master's own panel
static void addBox( Int x, Int y, Int width, Int height )
{
	addPiece( PIECE_FILL, x, y, width, height, UIStyle::PANEL );
	addPiece( PIECE_LINE, x, y, width, height, UIStyle::FRAME );
}

/// a selected row: EDGE at about a fifth under a one-pixel EDGE line, the mockups' own row
static void addSelection( Int x, Int y, Int width, Int height )
{
	addPiece( PIECE_FILL, x, y, width, height, UIStyle::withAlpha( UIStyle::EDGE, 46 ) );
	addPiece( PIECE_LINE, x, y, width, height, UIStyle::EDGE );
}

/// a picture with the rank portraits' one-pixel FRAME round it; nothing for a missing one
static void addPicture( const Image *image, Int x, Int y, Int width, Int height )
{
	if (!image)
		return;
	addPiece( PIECE_IMAGE, x, y, width, height, 0, image );
	addPiece( PIECE_LINE, x - 1, y - 1, width + 2, height + 2, UIStyle::FRAME );
}

/// UIStyle::track as pieces
static void addTrack( Int x, Int y, Int width, Int height, Real fraction )
{
	addPiece( PIECE_FILL, x, y, width, height, UIStyle::GRID );
	const Int filled = REAL_TO_INT( width * max( 0.0f, min( 1.0f, fraction ) ) + 0.5f );
	if (filled > 0)
		addPiece( PIECE_FILL, x, y, filled, height, UIStyle::EDGE );
}

static DisplayString *newWords( const UnicodeString &text, GameFont *font, Int wrap = 0 )
{
	DisplayString *words = TheDisplayStringManager->newDisplayString();
	words->setFont( font );
	if (wrap > 0)
		words->setWordWrap( wrap );
	words->setText( text );
	return words;
}

static Int measure( const UnicodeString &text, GameFont *font )
{
	DisplayString *words = newWords( text, font );
	Int width, height;
	words->getSize( &width, &height );
	TheDisplayStringManager->freeDisplayString( words );
	return width;
}

enum { ALIGN_LEFT, ALIGN_RIGHT };
/** Words on the panel from x, or ending at x (ALIGN_RIGHT), their top at y, or centred on the band
	* y to y + band when band is not 0; wrapped at wrap when it is not 0.  Returns their width. */
static Int addWords( const UnicodeString &text, GameFont *font, Int x, Int y, Color color, Int align = ALIGN_LEFT, Int band = 0, Int wrap = 0 )
{
	if (text.isEmpty())
		return 0;
	DisplayString *words = newWords( text, font, wrap );
	Int width, height;
	words->getSize( &width, &height );
	if (align == ALIGN_RIGHT)
		x -= width;
	if (band > 0)
		y += ( band - height ) / 2;
	addPiece( PIECE_WORDS, x, y, width, height, color, NULL, words );
	return width;
}

/// a paragraph wrapped at width; returns its height
static Int addParagraph( const UnicodeString &text, GameFont *font, Int x, Int y, Int width, Color color )
{
	addWords( text, font, x, y, color, ALIGN_LEFT, 0, width );
	return s_pieces.empty() || text.isEmpty() ? 0 : s_pieces.back().height;
}

static void drawSkinsHero( void );

static void drawPanel( GameWindow *window, WinInstanceData *instData )
{
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	UIStyle::panel( x, y, width, height, UIStyle::PANEL );
	// the master's StaticTextTitle and HeaderRule carry the title and its rule; the server line follows
	UIStyle::onlineHeader( UnicodeString::TheEmptyString );
	for (size_t i = 0; i < s_pieces.size(); ++i)
	{
		const Piece &p = s_pieces[i];
		switch (p.kind)
		{
			case PIECE_FILL:	TheDisplay->drawFillRect( p.x, p.y, p.width, p.height, p.color ); break;
			case PIECE_LINE:	TheDisplay->drawOpenRect( p.x, p.y, p.width, p.height, 1.0f, p.color ); break;
			case PIECE_IMAGE:	TheDisplay->drawImage( p.image, p.x, p.y, p.x + p.width, p.y + p.height ); break;
			default:					p.words->draw( p.x, p.y, p.color, UIStyle::SHADOW ); break;
		}
	}
	drawSkinsHero();
}

//-------------------------------------------------------------------------------------------------
// -online: EA's ladder was a web page on its dead servers.  Here it is a table on the panel, 20 rows
// a page as the service pages them, the player's own row in the page or pinned under it, and a side
// column with his place and the ranks.
//-------------------------------------------------------------------------------------------------
enum { LADDER_PAGE = 20 };
static GameWindow *buttonPrev = NULL;
static GameWindow *buttonNext = NULL;
static GameWindow *buttonJump = NULL;
static Int s_ladderOffset = 0;
static Int s_ladderShown = -1;			///< the OnlineLadderState last laid out, -1 to lay out again
static OnlineLadderRow s_me;				///< the player's own row, from the last page that carried it
static Bool s_hasMe = FALSE;

enum LadderColumn
{
	COLUMN_RANK,
	COLUMN_PLAYER,
	COLUMN_TIER,
	COLUMN_RATING,
	COLUMN_WINS,
	COLUMN_LOSSES,
	COLUMN_WIN_PCT,
	LADDER_COLUMNS
};
/// widths at 1080; the player takes what the others leave
static const Real COLUMN_WIDTH[ LADDER_COLUMNS ] = { 70.0f, 0.0f, 170.0f, 120.0f, 110.0f, 130.0f, 110.0f };
static const Bool COLUMN_RIGHT[ LADDER_COLUMNS ] = { TRUE, FALSE, FALSE, TRUE, TRUE, TRUE, TRUE };
static Int s_columnX[ LADDER_COLUMNS ];
static Int s_columnWidth[ LADDER_COLUMNS ];		///< 0 for a column the screen is too narrow for

/// rated games: a ranked player has his five, whatever the record says
static Int ladderGames( const OnlineLadderRow &r )
{
	const Int games = r.wins + r.losses;
	return r.rank > 0 ? max( games, (Int)UIStyle::RANKED_GAMES ) : games;
}

/// a row's tier as the site sent it, by its rating where the row carries none (UIStyle::playerTier)
static UIStyle::RatingTier rowTier( const OnlineLadderRow &r )
{
	OnlinePlayerInfo info;
	info.tier = r.tier;
	info.hasRating = r.hasRating;
	info.exactRating = r.exactRating;
	info.games = ladderGames( r );
	return UIStyle::playerTier( info );
}

/// the player's own tier, the header's: what the server said of him, else his ladder row
static UIStyle::RatingTier myTier( void )
{
	OnlinePlayerInfo info;
	const AsciiString self = localNick();
	if (self.isNotEmpty() && OnlineGetPlayerInfo( self, info ))
		return UIStyle::playerTier( info );
	return s_hasMe ? rowTier( s_me ) : UIStyle::TIER_UNRANKED;
}

static UnicodeString tierName( UIStyle::RatingTier tier )
{
	return OnlineTierName( AsciiString( UIStyle::tierKey( tier ) ) );
}

/// the first offset of the page that holds the player
static Int myPage( void )
{
	return ( s_me.rank - 1 ) / LADDER_PAGE * LADDER_PAGE;
}

/// the side column's two boxes: Your place over Ranks
static void ladderSideBoxes( IRegion2D &place, IRegion2D &ranks )
{
	IRegion2D table, side;
	UIStyle::onlineColumns( table, side );
	place = side;
	place.hi.y = side.lo.y + down( 340.0f );
	ranks = side;
	ranks.lo.y = place.hi.y + down( 16.0f );
}

/// one cell's words fitted to its column, a name only ever cut, never shortened
static void addCell( Int column, const UnicodeString &text, GameFont *font, Int y, Int band, Color color )
{
	const Int pad = across( 10.0f );
	const Int room = s_columnWidth[ column ] - 2 * pad;
	if (room <= 0 || text.isEmpty())
		return;
	const UnicodeString fitted = column == COLUMN_PLAYER ? UIStyle::fitName( text, font, room ) : UIStyle::fitText( text, font, room );
	if (COLUMN_RIGHT[ column ])
		addWords( fitted, font, s_columnX[ column ] + s_columnWidth[ column ] - pad, y, color, ALIGN_RIGHT, band );
	else
		addWords( fitted, font, s_columnX[ column ] + pad, y, color, ALIGN_LEFT, band );
}

static void addLadderRow( const OnlineLadderRow &r, Int y, Int band, Bool mine )
{
	GameFont *body = rowFont( band );
	const UnicodeString none( u"\x2013" );
	addCell( COLUMN_RANK, r.rank > 0 ? number( r.rank ) : none, body, y, band, UIStyle::MUTED );
	UnicodeString nick;
	nick.translate( r.nick );
	addCell( COLUMN_PLAYER, nick, mine ? rowFont( band, TRUE ) : body, y, band, UIStyle::INK );
	addCell( COLUMN_TIER, tierName( rowTier( r ) ), body, y, band, UIStyle::MUTED );
	addCell( COLUMN_RATING, r.hasRating ? UIStyle::ratingText( r.exactRating ) : none, body, y, band, UIStyle::INK );
	addCell( COLUMN_WINS, number( r.wins ), body, y, band, UIStyle::INK );
	addCell( COLUMN_LOSSES, number( r.losses ), body, y, band, UIStyle::INK );
	const Int games = r.wins + r.losses;
	addCell( COLUMN_WIN_PCT, games > 0 ? UIStyle::percentText( UIStyle::percent( r.wins, games ) ) : none, body, y, band, UIStyle::MUTED );
}

/// a tier's range as the Ranks box lists it: "25.00 to 29.99"
static UnicodeString tierRange( UIStyle::RatingTier tier )
{
	UnicodeString range;
	if (tier == UIStyle::TIER_UNRANKED)
		range = TheGameText->fetch( "GUI:OnlineLadderTierUnranked" );
	else if (tier == UIStyle::TIER_GENERAL)
		range.format( TheGameText->fetch( "GUI:OnlineLadderTierUp" ), UIStyle::ratingText( UIStyle::tierStart( tier ) ).str() );
	else if (tier == UIStyle::TIER_PRIVATE)
		range.format( TheGameText->fetch( "GUI:OnlineLadderTierUnder" ), UIStyle::ratingText( UIStyle::tierStart( UIStyle::TIER_SERGEANT ) ).str() );
	else
		range.format( TheGameText->fetch( "GUI:OnlineLadderTierRange" ), UIStyle::ratingText( UIStyle::tierStart( tier ) ).str(),
			UIStyle::ratingText( UIStyle::tierStart( (UIStyle::RatingTier)( tier + 1 ) ) - 0.01f ).str() );
	return range;
}

/// the table: heads, the page or the line that stands for it, and the player's row pinned under it
static void layoutLadderTable( OnlineLadderState state, Int offset, const std::vector<OnlineLadderRow> &rows, Bool answerHasMe )
{
	IRegion2D table, side;
	UIStyle::onlineColumns( table, side );
	const Int inset = across( 24.0f );
	const Int tableWidth = table.hi.x - table.lo.x;
	GameFont *body = UIStyle::readableFont( UIStyle::TEXT_BODY );
	addBox( table.lo.x, table.lo.y, tableWidth, table.hi.y - table.lo.y );
	const Int shown = min( (Int)rows.size(), (Int)LADDER_PAGE );
	if (state == ONLINE_LADDER_READY && shown > 0)
	{
		UnicodeString page;
		page.format( TheGameText->fetch( "GUI:OnlineLadderPage" ), offset + 1, offset + shown );
		addWords( page, body, table.hi.x - inset, table.lo.y + down( 22.0f ), UIStyle::MUTED, ALIGN_RIGHT );
	}

	// the columns, 14 in from the box's left and ending under the page label's right edge (the
	// mockup's 1070), so every rule and selected row stops where the Win % column does; 4:3 narrows
	// the main column, and Losses go, then Win %, before the names get too little room
	const Int left = table.lo.x + across( 14.0f );
	const Int width = tableWidth - across( 14.0f ) - inset;
	Bool open[ LADDER_COLUMNS ];
	Int fixed = 0;
	for (Int c = 0; c < LADDER_COLUMNS; ++c)
	{
		open[ c ] = TRUE;
		fixed += across( COLUMN_WIDTH[ c ] );
	}
	static const Int DROPPED[] = { COLUMN_LOSSES, COLUMN_WIN_PCT };
	for (Int d = 0; d < (Int)ARRAY_SIZE( DROPPED ) && width - fixed < across( 220.0f ); ++d)
	{
		open[ DROPPED[ d ] ] = FALSE;
		fixed -= across( COLUMN_WIDTH[ DROPPED[ d ] ] );
	}
	Int x = left;
	for (Int c = 0; c < LADDER_COLUMNS; ++c)
	{
		s_columnX[ c ] = x;
		s_columnWidth[ c ] = !open[ c ] ? 0 : c == COLUMN_PLAYER ? width - fixed : across( COLUMN_WIDTH[ c ] );
		x += s_columnWidth[ c ];
	}

	// heads in INK over a FRAME rule; 56 + 36 + 20 rows of 27 + 10 + 40 is 682 of the band's 710
	const Int headTop = table.lo.y + down( 56.0f );
	const Int headHeight = down( 36.0f );
	static const char *const HEAD[ LADDER_COLUMNS ] = { "GUI:OnlineLadderRank", "GUI:OnlineLadderPlayer", "GUI:OnlineLadderTier",
		"GUI:OnlineLadderRating", "GUI:OnlineLadderWins", "GUI:OnlineLadderLosses", "GUI:OnlineLadderWinPct" };
	for (Int c = 0; c < LADDER_COLUMNS; ++c)
	{
		UnicodeString head;
		head.format( TheGameText->fetch( HEAD[ c ] ) );		// Win %% is a format
		addCell( c, head, rowFont( headHeight ), headTop, headHeight, UIStyle::INK );
	}
	addPiece( PIECE_FILL, left, headTop + headHeight - 1, width, 1, UIStyle::FRAME );

	// the page, GRID between the rows, the player's own selected
	const Int rowHeight = down( 27.0f );
	body = rowFont( rowHeight );
	Int y = headTop + headHeight;
	Bool meInPage = FALSE;
	const char *line = NULL;
	if (state == ONLINE_LADDER_WAITING)
		line = "GUI:OnlineLadderLoading";
	else if (state == ONLINE_LADDER_FAILED)
		line = "GUI:OnlineLadderUnavailable";
	else if (shown == 0)
		line = "GUI:OnlineLadderEmpty";
	if (line)
		addWords( UIStyle::fitText( TheGameText->fetch( line ), body, width - across( 20.0f ) ), body, left + across( 10.0f ), y, UIStyle::MUTED, ALIGN_LEFT, rowHeight );
	const AsciiString self = localNick();
	for (Int i = 0; state == ONLINE_LADDER_READY && i < shown; ++i, y += rowHeight)
	{
		const Bool mine = self.isNotEmpty() && rows[ i ].nick.compareNoCase( self ) == 0;
		if (mine)
			addSelection( left, y, width, rowHeight );
		else
			addPiece( PIECE_FILL, left, y + rowHeight - 1, width, 1, UIStyle::GRID );
		addLadderRow( rows[ i ], y, rowHeight, mine );
		meInPage = meInPage || mine;
	}
	if (state == ONLINE_LADDER_READY && answerHasMe && !meInPage)
	{
		const Int pinned = headTop + headHeight + LADDER_PAGE * rowHeight + down( 10.0f );
		addSelection( left, pinned, width, down( 40.0f ) );
		addLadderRow( s_me, pinned, down( 40.0f ), TRUE );
	}
}

/// the side column: his place, rating and tier, the next tier, his record; then the ranks
static void layoutLadderSide( void )
{
	IRegion2D place, ranks;
	ladderSideBoxes( place, ranks );
	const Int inset = across( 24.0f );
	const Int sideWidth = place.hi.x - place.lo.x;
	const Int inner = sideWidth - 2 * inset;
	const Int x = place.lo.x + inset;
	GameFont *body = rowFont( down( 30.0f ) );
	const UnicodeString none( u"\x2013" );
	const UIStyle::RatingTier tier = myTier();
	const Int games = s_hasMe ? s_me.wins + s_me.losses : 0;

	addBox( place.lo.x, place.lo.y, sideWidth, place.hi.y - place.lo.y );
	addWords( TheGameText->fetch( "GUI:OnlineLadderYourPlace" ), UIStyle::sectionFont(), x, place.lo.y + down( 20.0f ), UIStyle::ACTIVE );
	addPicture( UIStyle::tierImage( tier, AsciiString::TheEmptyString ), x, place.lo.y + down( 60.0f ), down( 96.0f ), down( 96.0f ) );

	// the place as a display figure, the title's size where the box is too short or narrow for it; an
	// unranked player's games toward his five in its stead
	UnicodeString figure = none;
	if (s_hasMe && s_me.rank > 0)
		figure.format( TheGameText->fetch( "GUI:OnlinePlaceValue" ), s_me.rank );
	else if (s_hasMe)
		figure.format( TheGameText->fetch( "GUI:OnlineLadderGamesOf" ), min( games, (Int)UIStyle::RANKED_GAMES ), (Int)UIStyle::RANKED_GAMES );
	figure = UIStyle::capitals( figure );
	const Int figureX = place.lo.x + across( 140.0f );
	const Int figureRoom = place.hi.x - inset - figureX;
	GameFont *display = UIStyle::titleFont( UIStyle::TEXT_DISPLAY );
	if (measure( figure, display ) > figureRoom || display->height > down( 130.0f ) - down( 60.0f ))
		display = UIStyle::titleFont( UIStyle::TEXT_TITLE );
	addWords( UIStyle::fitText( figure, display, figureRoom ), display, figureX, place.lo.y + down( 60.0f ), UIStyle::INK );
	UnicodeString standing = s_hasMe && s_me.hasRating ? UIStyle::ratingText( s_me.exactRating ) : none;
	standing.concat( UIStyle::HEADER_JOINER );
	standing.concat( tierName( tier ) );
	addWords( UIStyle::fitText( standing, body, figureRoom ), body, figureX, place.lo.y + down( 130.0f ), UIStyle::INK );
	addPiece( PIECE_FILL, x, place.lo.y + down( 172.0f ), inner, 1, UIStyle::GRID );

	// the next tier and how far, or the games still owed before the first
	UnicodeString next;
	Real progress = 1.0f;
	if (tier == UIStyle::TIER_UNRANKED)
		progress = (Real)games / UIStyle::RANKED_GAMES;
	else if (tier < UIStyle::TIER_GENERAL)
	{
		const UIStyle::RatingTier up = (UIStyle::RatingTier)( tier + 1 );
		const Real from = UIStyle::tierStart( tier );
		const Real to = UIStyle::tierStart( up );
		next.format( TheGameText->fetch( "GUI:OnlineNextTier" ), tierName( up ).str(), UIStyle::ratingText( to ).str(),
			UIStyle::ratingText( to - s_me.exactRating ).str() );
		progress = ( s_me.exactRating - from ) / ( to - from );
	}
	addWords( UIStyle::fitText( next, body, inner ), body, x, place.lo.y + down( 186.0f ), UIStyle::MUTED );
	addTrack( x, place.lo.y + down( 220.0f ), inner, down( 10.0f ), progress );
	UnicodeString record;
	record.format( TheGameText->fetch( "GUI:OnlineLadderRecord" ), s_hasMe ? s_me.wins : 0, s_hasMe ? s_me.losses : 0 );
	addWords( UIStyle::fitText( record, body, inner ), body, x, place.lo.y + down( 242.0f ), UIStyle::INK );

	// the ranks with their portraits and ranges, his own selected; 56 + 8 rows of 36 is 344 of 354
	addBox( ranks.lo.x, ranks.lo.y, sideWidth, ranks.hi.y - ranks.lo.y );
	addWords( TheGameText->fetch( "GUI:OnlineLadderTiers" ), UIStyle::sectionFont(), x, ranks.lo.y + down( 20.0f ), UIStyle::ACTIVE );
	const Int rowLeft = ranks.lo.x + across( 14.0f );
	const Int rowWidth = sideWidth - across( 28.0f );
	const Int band = down( 36.0f );
	const Int picture = down( 28.0f );
	body = rowFont( band );
	Int rowTop = ranks.lo.y + down( 56.0f );
	for (Int t = UIStyle::TIER_GENERAL; t >= UIStyle::TIER_UNRANKED; --t, rowTop += band)
	{
		const UIStyle::RatingTier each = (UIStyle::RatingTier)t;
		if (s_hasMe && each == tier)
			addSelection( rowLeft, rowTop, rowWidth, band );
		addPicture( UIStyle::tierImage( each, AsciiString::TheEmptyString ), rowLeft + across( 10.0f ), rowTop + ( band - picture ) / 2, picture, picture );
		const Int rangeRight = rowLeft + rowWidth - across( 10.0f );
		const Int rangeWidth = addWords( tierRange( each ), body, rangeRight, rowTop, UIStyle::MUTED, ALIGN_RIGHT, band );
		const Int nameX = rowLeft + across( 50.0f );
		const Int nameRoom = rangeRight - rangeWidth - across( 12.0f ) - nameX;
		if (nameRoom > 0)
			addWords( UIStyle::fitText( tierName( each ), body, nameRoom ), body, nameX, rowTop, UIStyle::INK, ALIGN_LEFT, band );
	}
}

static void layoutLadder( OnlineLadderState state, Int offset, const std::vector<OnlineLadderRow> &rows, Bool answerHasMe )
{
	clearPieces();
	layoutLadderTable( state, offset, rows, answerHasMe );
	layoutLadderSide();
	if (buttonPrev)
		buttonPrev->winEnable( state != ONLINE_LADDER_WAITING && s_ladderOffset > 0 );
	if (buttonNext)
		buttonNext->winEnable( state == ONLINE_LADDER_READY && (Int)rows.size() >= LADDER_PAGE );
	if (buttonJump)
		buttonJump->winEnable( state != ONLINE_LADDER_WAITING && s_hasMe && s_me.rank > 0 && myPage() != s_ladderOffset );
}

static void createLadderGadgets( void )
{
	if (!parentWindow || !windowLadder)
		return;
	buttonPrev = rowButton( 1180.0f, UIStyle::ROOM_BUTTON_WIDTH, "GUI:OnlineLadderPrev" );
	buttonNext = rowButton( 1496.0f, UIStyle::ROOM_BUTTON_WIDTH, "GUI:OnlineLadderNext" );
	IRegion2D place, ranks;
	ladderSideBoxes( place, ranks );
	const Int inset = across( 24.0f );
	buttonJump = smallButton( place.lo.x + inset, place.lo.y + down( 284.0f ), place.hi.x - place.lo.x - 2 * inset, "GUI:OnlineLadderJump" );

	// his row until a page carries it: what the server said of him at sign-in
	s_me = OnlineLadderRow();
	s_hasMe = FALSE;
	const AsciiString self = localNick();
	OnlinePlayerInfo info;
	if (self.isNotEmpty() && OnlineGetPlayerInfo( self, info ))
	{
		s_me.nick = self;
		s_me.rank = max( 0, info.rank );
		s_me.hasRating = info.hasRating;
		s_me.exactRating = info.exactRating;
		s_me.rating = info.rating;
		s_me.tier = info.tier;
		s_me.wins = max( 0, info.wins );
		s_me.losses = max( 0, info.losses );
		s_hasMe = TRUE;
	}
}

static void destroyLadderGadgets( void )
{
	GameWindow *made[] = { buttonPrev, buttonNext, buttonJump };
	for (size_t i = 0; i < ARRAY_SIZE( made ); ++i)
		if (made[i])
			TheWindowManager->winDestroy( made[i] );
	buttonPrev = buttonNext = buttonJump = NULL;
}

static void drawLadder( void )
{
	Int offset = 0;
	std::vector<OnlineLadderRow> rows;
	OnlineLadderRow me;
	Bool hasMe = FALSE;
	const OnlineLadderState state = OnlineGetLadder( offset, rows, me, hasMe );
	if ((Int)state == s_ladderShown)
		return;
	s_ladderShown = (Int)state;
	if (hasMe)
	{
		s_me = me;
		s_hasMe = TRUE;
	}
	layoutLadder( state, offset, rows, hasMe );
}

static void requestLadderPage( Int offset )
{
	s_ladderOffset = max( 0, offset );
	s_ladderShown = -1;
	OnlineRequestLadder( s_ladderOffset );
	drawLadder();
}

//-------------------------------------------------------------------------------------------------
// The skins page, the Skins tab (ONLINE-UI-REDESIGN.md "Skins", experiments/online-redesign/
// skinpick.html), three panels.  Left, the units SkinsReforged.ini dresses by side, each with its
// cameo and how many of its looks the account owns.  Middle, the picked unit's looks as cards, three a
// line, Default first, each its model drawn live at one fixed angle and its name, a tick on the worn
// one and a padlock on a locked one, under the owned count's bar and an All, Owned, Locked filter.
// Right, the look in focus turning on its plate over the whole panel, its name and one button: Wear,
// Worn (Picked until saved), or for a look the account lacks what earns it, which opens zerohour.gg.
// Save sends the whole loadout;
// once the site took it the next room and Quick Match seat it (OnlineSaveSkins).  The ladder's
// layout, no layout of its own to ship, and no card pictures: the models are the cards.
//-------------------------------------------------------------------------------------------------
struct SkinChoice
{
	GameWindow *button;			///< the card, a push button
	Int id;									///< 0 for Default
	Bool owned;
};
struct SkinRow
{
	AsciiString unit;
	UnicodeString name;
	std::vector<SkinChoice> choices;
	Int pick;								///< picked on this page
	Int worn;								///< what the account wears, TankSkin_localLoadout
	GameWindow *tile;				///< the unit's row in the list, a push button
	Int known;							///< its line in SKIN_UNITS; SKIN_UNKNOWN for a unit missing there
	DisplayString *count;		///< "owned / looks" at the row's right end
};
enum { FILTER_ALL, FILTER_OWNED, FILTER_LOCKED, FILTER_COUNT };
static Bool s_skinsPage = FALSE;
static std::vector<SkinRow> s_skinRows;
static size_t s_skinUnit = 0;					///< the row whose looks show
static Int s_skinFocus = 0;						///< the look on the platform, a skin id of that row, 0 Default
static Int s_skinFilter = FILTER_ALL;
static GameWindow *buttonSave = NULL;
static GameWindow *buttonAction = NULL;	///< Wear, Worn, Picked or Open zerohour.gg
static GameWindow *buttonFilter[ FILTER_COUNT ] = { NULL, NULL, NULL };
static Int s_skinsState = -1;					///< OnlineSkinsState last laid out
static UnsignedInt64 s_skinsOwnedShown = 0;
static Bool s_skinsDirty = TRUE;				///< lay out again even with the state unchanged
static Bool s_skinsSaved = FALSE;			///< the last READY answers this page's Save
static IRegion2D s_hero;								///< where the look in focus turns; empty for none
static Bool s_heroOwned = TRUE;

enum { SKIN_SIDE_USA, SKIN_SIDE_CHINA, SKIN_SIDE_GLA, SKIN_SIDE_COUNT };
static const char *const SKIN_SIDE_KEY[ SKIN_SIDE_COUNT ] = { "GUI:OnlineSkinsSideUsa", "GUI:OnlineSkinsSideChina", "GUI:OnlineSkinsSideGla" };
/// each unit SkinsReforged.ini dresses, in the list's order: the object template whose model is drawn,
/// its cameo and side, and the model condition it is shown in (NULL its default; the Worker hauls his
/// crate, which his stance hides); a unit missing here goes last with its name alone and no model
static const struct { const char *unit; const char *templateName; const char *cameo; Int side; const char *state; } SKIN_UNITS[] =
{
	{ "Crusader", "AmericaTankCrusader", "SACLeopard", SKIN_SIDE_USA, NULL },
	{ "Chinook", "AmericaVehicleChinook", "SAChinook", SKIN_SIDE_USA, NULL },
	{ "USADozer", "AmericaVehicleDozer", "SACDozer", SKIN_SIDE_USA, NULL },
	{ "Battlemaster", "ChinaTankBattleMaster", "SNBattlemaster", SKIN_SIDE_CHINA, NULL },
	{ "SupplyTruck", "ChinaVehicleSupplyTruck", "SNSupplyTruck", SKIN_SIDE_CHINA, NULL },
	{ "ChinaDozer", "ChinaVehicleDozer", "SNDozer", SKIN_SIDE_CHINA, NULL },
	{ "Scorpion", "GLATankScorpion", "SUScorpion", SKIN_SIDE_GLA, NULL },
	{ "Worker", "GLAInfantryWorker", "SUWorker", SKIN_SIDE_GLA, "CARRYING" },
};
static const Int SKIN_UNKNOWN = (Int)ARRAY_SIZE( SKIN_UNITS );

/// the house colour the models wear, a player's blue
static const Color SKIN_HOUSE = GameMakeColor( 0x2a, 0x6e, 0xd8, 0xff );
/// a card's model at a fixed turn: the front quarter toward the eye, so the side and the roof show
static const Real SKIN_CARD_TURN = 0.06f;
/// a card's ground, near black, so the model carries it
static const Color SKIN_CARD_GROUND = GameMakeColor( 0x09, 0x0b, 0x13, 0xff );
/// a rare look's gold, the score screen's winner (UIStyle::GOLD) between two darker lines, and the
/// gold a locked one wears
static const Color SKIN_RARE_DARK = GameMakeColor( 0x8a, 0x66, 0x14, 0xff );
static const Color SKIN_RARE_LOCKED = GameMakeColor( 0xa8, 0x8a, 0x3a, 0xff );

/// the button edge's three lines in gold round a rare look
static void rareEdge( Int x, Int y, Int width, Int height )
{
	TheDisplay->drawOpenRect( x, y, width, height, 1.0f, SKIN_RARE_DARK );
	TheDisplay->drawOpenRect( x + 1, y + 1, width - 2, height - 2, 1.0f, UIStyle::GOLD );
	TheDisplay->drawOpenRect( x + 2, y + 2, width - 4, height - 4, 1.0f, SKIN_RARE_DARK );
}

/// a five-pointed star, its points on a circle of size across centred in the size-square box at x, y
static void drawRareStar( Int x, Int y, Int size, Color color )
{
	const Real cx = x + size * 0.5f, cy = y + size * 0.52f, r = size * 0.5f;
	const Real stroke = (Real)max( 2, size / 8 );
	Real px[5], py[5];
	for (Int i = 0; i < 5; ++i)
	{
		const Real a = -1.5707963f + i * 1.2566371f;		// from the top, a fifth of a turn apart
		px[i] = cx + r * cosf( a );
		py[i] = cy + r * sinf( a );
	}
	for (Int i = 0; i < 5; ++i)
	{
		const Int j = ( i + 2 ) % 5;
		TheDisplay->drawLine( (Int)px[i], (Int)py[i], (Int)px[j], (Int)py[j], stroke, color );
	}
}

void WOLSkinsScreenOpen( void )
{
	s_skinsPage = TRUE;
	TheShell->push( AsciiString( "Menus/WOLLadderScreen.wnd" ) );
}

/// the three panels across the content band: the list 360 and the platform 520 wide at 16:9,
/// smaller shares of a squarer band (275 and 388 at 4:3), the cards between
struct SkinsFrame
{
	Int top, bottom;
	Int listX, listWidth, midX, midWidth, rightX, rightWidth;
};
static SkinsFrame skinsFrame( void )
{
	IRegion2D main, side;
	UIStyle::onlineColumns( main, side );
	SkinsFrame f;
	const Int gap = across( 24.0f );
	const Int all = side.hi.x - main.lo.x;
	f.top = main.lo.y;
	f.bottom = main.hi.y;
	f.listX = main.lo.x;
	f.listWidth = min( across( 360.0f ), all * 22 / 100 );
	f.rightWidth = min( across( 520.0f ), all * 31 / 100 );
	f.rightX = side.hi.x - f.rightWidth;
	f.midX = f.listX + f.listWidth + gap;
	f.midWidth = f.rightX - gap - f.midX;
	return f;
}

/// the cards a line: three, so a card is wide enough for a long hull (227 at 16:9, 159 at 4:3), or two
/// where three would be narrower than 120
static Int cardColumns( const SkinsFrame &f )
{
	const Int inner = f.midWidth - 2 * across( 20.0f );
	return inner >= 3 * across( 120.0f ) + 2 * across( 12.0f ) ? 3 : 2;
}

static Int cardWidth( const SkinsFrame &f )
{
	const Int columns = cardColumns( f );
	return ( f.midWidth - 2 * across( 20.0f ) - ( columns - 1 ) * across( 12.0f ) ) / columns;
}

/// card slot of shown under the bar: two lines fill the column, more where the shown looks need them
static void cardPlace( Int slot, Int shown, const SkinsFrame &f, Int &x, Int &y, Int &width, Int &height )
{
	const Int columns = cardColumns( f );
	const Int lines = max( 2, ( shown + columns - 1 ) / columns );
	const Int top = f.top + down( 88.0f );
	const Int room = f.bottom - down( 20.0f ) - top;
	const Int gap = down( 12.0f );
	width = cardWidth( f );
	height = ( room - ( lines - 1 ) * gap ) / lines;
	x = f.midX + across( 20.0f ) + ( slot % columns ) * ( width + across( 12.0f ) );
	y = top + ( slot / columns ) * ( height + gap );
}

/// the list's order, SKIN_UNITS', the table's own order within a unit kept
static bool skinRowBefore( const SkinRow &a, const SkinRow &b )
{
	return a.known < b.known;
}

static Int rowSide( size_t r )
{
	return s_skinRows[ r ].known < SKIN_UNKNOWN ? SKIN_UNITS[ s_skinRows[ r ].known ].side : -1;
}

/// row r's place in the list, a side's heading over its first unit
static void unitPlace( size_t r, const SkinsFrame &f, Int &x, Int &y, Int &width, Int &height )
{
	Int top = f.top + down( 16.0f );
	Int side = -1;
	for (size_t i = 0; i <= r && i < s_skinRows.size(); ++i)
	{
		const Int its = rowSide( i );
		if (its != side && its >= 0)
			top += down( 40.0f );
		side = its;
		if (i < r)
			top += down( 68.0f );
	}
	x = f.listX + across( 12.0f );
	y = top;
	width = f.listWidth - across( 24.0f );
	height = down( 60.0f );
}

/// the look in focus on the shown unit's row, its first when the focus is not one of its own
static SkinChoice *focusChoice( SkinRow &row )
{
	for (size_t c = 0; c < row.choices.size(); ++c)
		if (row.choices[ c ].id == s_skinFocus)
			return &row.choices[ c ];
	return row.choices.empty() ? NULL : &row.choices[ 0 ];
}

/** The look in focus turning on its plate, once a 12 s by the wall clock, from the panel's draw so
	* the buttons go over it; dimmed for a look the account lacks. */
static void drawSkinsHero( void )
{
	if (!s_skinsPage || s_skinUnit >= s_skinRows.size() || s_hero.hi.x <= s_hero.lo.x || s_hero.hi.y <= s_hero.lo.y)
		return;
	const SkinRow &row = s_skinRows[ s_skinUnit ];
	if (row.known >= SKIN_UNKNOWN)
		return;
	const double seconds = std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
	const Real turns = (Real)fmod( seconds / 12.0, 1.0 );
	TheDisplay->drawModelPreview( AsciiString( SKIN_UNITS[ row.known ].templateName ), s_skinFocus, SKIN_HOUSE,
		s_hero.lo.x, s_hero.lo.y, s_hero.hi.x - s_hero.lo.x, s_hero.hi.y - s_hero.lo.y, turns, !s_heroOwned, FALSE,
		SKIN_UNITS[ row.known ].state );
	if (TankSkin_isRare( s_skinFocus ))
	{
		rareEdge( s_hero.lo.x, s_hero.lo.y, s_hero.hi.x - s_hero.lo.x, s_hero.hi.y - s_hero.lo.y );
		const Int mark = across( 26.0f );
		drawRareStar( s_hero.lo.x + across( 12.0f ), s_hero.lo.y + down( 12.0f ), mark, s_heroOwned ? UIStyle::GOLD : SKIN_RARE_LOCKED );
	}
}

static Bool hovered( GameWindow *window, WinInstanceData *instData )
{
	return BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) && BitTest( instData->getState(), WIN_STATE_HILITED );
}

/// a button's own label in the button's font
static DisplayString *buttonWords( GameWindow *window, WinInstanceData *instData )
{
	DisplayString *words = instData->getTextDisplayString();
	if (words->getFont() != window->winGetFont())
		words->setFont( window->winGetFont() );
	return words;
}

/** A unit's row in the list, drawn over its push button: the selected row's EDGE wash and line, a
	* FRAME line under the mouse; its cameo, its name (ACTIVE under the mouse) and its owned count. */
static void drawSkinUnit( GameWindow *window, WinInstanceData *instData )
{
	const SkinRow *row = NULL;
	size_t r = 0;
	for (; r < s_skinRows.size(); ++r)
		if (s_skinRows[r].tile == window)
		{
			row = &s_skinRows[r];
			break;
		}
	if (!row)
		return;

	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	const Bool lit = hovered( window, instData );
	if (r == s_skinUnit)
	{
		TheDisplay->drawFillRect( x, y, width, height, UIStyle::withAlpha( UIStyle::EDGE, 46 ) );
		TheDisplay->drawOpenRect( x, y, width, height, 1.0f, UIStyle::EDGE );
	}
	else if (lit)
		TheDisplay->drawOpenRect( x, y, width, height, 1.0f, UIStyle::FRAME );

	// the cameo at the retail 5:4, 60 by 48 in a 60 row
	const Int pictureHeight = height - down( 12.0f );
	const Int pictureWidth = pictureHeight * 5 / 4;
	const Int pictureX = x + across( 6.0f );
	const Int pictureY = y + ( height - pictureHeight ) / 2;
	const Image *cameo = row->known < SKIN_UNKNOWN ? TheMappedImageCollection->findImageByName( SKIN_UNITS[ row->known ].cameo ) : NULL;
	if (cameo)
	{
		TheDisplay->drawImage( cameo, pictureX, pictureY, pictureX + pictureWidth, pictureY + pictureHeight );
		TheDisplay->drawOpenRect( pictureX - 1, pictureY - 1, pictureWidth + 2, pictureHeight + 2, 1.0f, UIStyle::FRAME );
	}

	DisplayString *name = buttonWords( window, instData );
	Int wordsWidth, wordsHeight;
	name->getSize( &wordsWidth, &wordsHeight );
	name->draw( pictureX + pictureWidth + across( 14.0f ), y + ( height - wordsHeight ) / 2, lit ? UIStyle::ACTIVE : UIStyle::INK, UIStyle::SHADOW );
	if (row->count)
	{
		row->count->getSize( &wordsWidth, &wordsHeight );
		row->count->draw( x + width - across( 12.0f ) - wordsWidth, y + ( height - wordsHeight ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
	}
}

/// a tick in a size-square box at x, y: the button edge's chrome, a dark plate under a bright stroke
static void drawTick( Int x, Int y, Int size, Color color )
{
	TheDisplay->drawFillRect( x, y, size, size, UIStyle::PANEL );
	TheDisplay->drawOpenRect( x, y, size, size, 1.0f, UIStyle::EDGE );
	const Real stroke = (Real)max( 2, size / 7 );
	TheDisplay->drawLine( x + size * 2 / 10, y + size * 5 / 10, x + size * 4 / 10, y + size * 3 / 4, stroke, color );
	TheDisplay->drawLine( x + size * 4 / 10, y + size * 3 / 4, x + size * 8 / 10, y + size * 1 / 4, stroke, color );
}

/// a padlock in a size-square box at x, y: a shackle over a solid body, in the muted ink
static void drawPadlock( Int x, Int y, Int size, Color color = UIStyle::MUTED )
{
	TheDisplay->drawFillRect( x, y, size, size, UIStyle::PANEL );
	const Int bodyTop = y + size * 9 / 20;
	const Int shackle = size / 2;
	const Real stroke = (Real)max( 2, size / 9 );
	TheDisplay->drawOpenRect( x + ( size - shackle ) / 2, y + size / 8, shackle, bodyTop - y - size / 8 + 2, stroke, color );
	TheDisplay->drawFillRect( x + size / 6, bodyTop, size - size / 3, size - ( bodyTop - y ) - size / 10, color );
}

/** A look's card, drawn over its push button: the model on a near-black ground at a fixed turn,
	* darkened where the account lacks the look; the three-line edge on the look in focus; a tick or a
	* padlock in the corner; its name (MUTED when locked, ACTIVE under the mouse) at the foot. */
static void drawSkinChoice( GameWindow *window, WinInstanceData *instData )
{
	const SkinRow *row = NULL;
	const SkinChoice *choice = NULL;
	size_t r = 0;
	for (; r < s_skinRows.size() && !choice; ++r)
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
			if (s_skinRows[r].choices[c].button == window)
			{
				row = &s_skinRows[r];
				choice = &s_skinRows[r].choices[c];
				break;
			}
	if (!choice)
		return;

	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	DisplayString *name = buttonWords( window, instData );
	Int nameWidth, nameHeight;
	name->getSize( &nameWidth, &nameHeight );
	const Int picture = height - nameHeight - 2 * down( 8.0f );

	TheDisplay->drawFillRect( x, y, width, height, UIStyle::PANEL );
	if (picture > 2)
		TheDisplay->drawFillRect( x + 1, y + 1, width - 2, picture - 1, SKIN_CARD_GROUND );
	if (picture > down( 24.0f ) && row->known < SKIN_UNKNOWN)
		TheDisplay->drawModelPreview( AsciiString( SKIN_UNITS[ row->known ].templateName ), choice->id, SKIN_HOUSE,
			x + 2, y + 2, width - 4, picture - 3, SKIN_CARD_TURN, !choice->owned, TRUE, SKIN_UNITS[ row->known ].state );
	// a rare look in gold, the focus's blue edge inside it
	const Bool rare = TankSkin_isRare( choice->id );
	if (rare)
		rareEdge( x, y, width, height );
	if (choice->id == s_skinFocus)
		UIStyle::edge( x + ( rare ? 3 : 0 ), y + ( rare ? 3 : 0 ), width - ( rare ? 6 : 0 ), height - ( rare ? 6 : 0 ) );
	else if (!rare)
		TheDisplay->drawOpenRect( x, y, width, height, 1.0f, UIStyle::FRAME );

	// the state in the corner: a tick on the look the unit wears (or is picked to), a padlock on one
	// the account lacks; a rare look's star in the other
	const Int mark = across( 18.0f );
	const Int markX = x + width - across( 8.0f ) - mark;
	const Int markY = y + down( 8.0f );
	if (choice->id == row->pick)
		drawTick( markX, markY, mark, row->pick == row->worn ? UIStyle::ACTIVE : UIStyle::EDGE_LIT );
	else if (!choice->owned)
		drawPadlock( markX, markY, mark );
	if (rare)
		drawRareStar( x + across( 8.0f ), markY, mark, choice->owned ? UIStyle::GOLD : SKIN_RARE_LOCKED );

	const Color ink = rare ? ( choice->owned ? UIStyle::GOLD : SKIN_RARE_LOCKED )
		: !choice->owned ? UIStyle::MUTED : hovered( window, instData ) ? UIStyle::ACTIVE : UIStyle::INK;
	name->draw( x + across( 10.0f ), y + picture + down( 8.0f ), ink, UIStyle::SHADOW );
}

/// a filter's box: the three-line edge on the one in force, its label ACTIVE then or under the mouse
static void drawSkinFilter( GameWindow *window, WinInstanceData *instData )
{
	Int filter = 0;
	while (filter < FILTER_COUNT && buttonFilter[ filter ] != window)
		++filter;
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	TheDisplay->drawFillRect( x, y, width, height, UIStyle::PANEL );
	const Bool on = filter == s_skinFilter;
	if (on)
		UIStyle::edge( x, y, width, height );
	else
		TheDisplay->drawOpenRect( x, y, width, height, 1.0f, UIStyle::FRAME );
	DisplayString *label = buttonWords( window, instData );
	Int wordsWidth, wordsHeight;
	label->getSize( &wordsWidth, &wordsHeight );
	label->draw( x + ( width - wordsWidth ) / 2, y + ( height - wordsHeight ) / 2,
		on || hovered( window, instData ) ? UIStyle::ACTIVE : UIStyle::INK, UIStyle::SHADOW );
}

/* Each row's pick and what it wears, from what the account wears. */
static void picksFromLoadout( void )
{
	const SkinLoadout wear = TankSkin_localLoadout();
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		s_skinRows[r].worn = 0;
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
			if (s_skinRows[r].choices[c].id && (wear & ((SkinLoadout)1 << s_skinRows[r].choices[c].id)))
				s_skinRows[r].worn = s_skinRows[r].choices[c].id;
		s_skinRows[r].pick = s_skinRows[r].worn;
	}
}

/// the units whose pick differs from what the account wears
static Int unsavedCount( void )
{
	Int count = 0;
	for (size_t r = 0; r < s_skinRows.size(); ++r)
		if (s_skinRows[r].pick != s_skinRows[r].worn)
			++count;
	return count;
}

static void createSkinsGadgets( void )
{
	if (!parentWindow || !windowLadder)
		return;

	// rows in id order, a unit's skins on the row of its first, Default first
	for (Int id = 1; id <= TANK_SKIN_MAX_ID; ++id)
	{
		AsciiString unit, unitLabel, label;
		if (!TankSkin_describe( id, unit, unitLabel, label ))
			continue;
		size_t r = 0;
		while (r < s_skinRows.size() && s_skinRows[r].unit.compareNoCase( unit ) != 0)
			++r;
		SkinChoice choice = { NULL, id, FALSE };
		if (r == s_skinRows.size())
		{
			SkinRow row;
			row.unit = unit;
			if (unitLabel.isEmpty())
				row.name.translate( unit );
			else
				row.name = TheGameText->fetch( unitLabel );
			row.pick = row.worn = 0;
			row.tile = NULL;
			row.known = SKIN_UNKNOWN;
			for (Int u = 0; u < SKIN_UNKNOWN; ++u)
				if (unit.compareNoCase( SKIN_UNITS[u].unit ) == 0)
					row.known = u;
			row.count = NULL;
			SkinChoice none = { NULL, 0, TRUE };
			row.choices.push_back( none );
			s_skinRows.push_back( row );
		}
		s_skinRows[r].choices.push_back( choice );
	}
	std::stable_sort( s_skinRows.begin(), s_skinRows.end(), skinRowBefore );

	// the list's rows and the cards, push buttons drawn as rows and cards; the cards are placed and
	// shown by layoutSkins, the shown unit's that pass the filter
	const SkinsFrame f = skinsFrame();
	GameFont *body = UIStyle::readableFont( UIStyle::TEXT_BODY );
	GameFont *strong = UIStyle::readableFont( UIStyle::TEXT_BODY, TRUE );
	const Int cardRoom = cardWidth( f ) - 2 * across( 10.0f );
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		SkinRow &row = s_skinRows[r];
		Int x, y, width, height;
		unitPlace( r, f, x, y, width, height );
		row.tile = WOLMakeButtonLike( parentWindow, buttonBack, x, y, width, height, "GUI:OnlineSkinsDefault" );
		if (row.tile)
		{
			// the name between the cameo and the widest count the row can show
			const Int looks = (Int)row.choices.size();
			UnicodeString widest = number( looks );
			widest.concat( u" / " );
			widest.concat( number( looks ) );
			const Int room = width - across( 6.0f ) - ( height - down( 12.0f ) ) * 5 / 4 - across( 14.0f )
				- measure( widest, body ) - across( 22.0f );
			row.tile->winSetFont( body );
			GadgetButtonSetText( row.tile, UIStyle::fitName( row.name, body, room ) );		// a name: an ellipsis, never a cut word
			row.tile->winSetDrawFunc( drawSkinUnit );
		}

		for (size_t c = 0; c < row.choices.size(); ++c)
		{
			AsciiString unit, unitLabel, label = "GUI:OnlineSkinsDefault";
			if (row.choices[c].id)
				TankSkin_describe( row.choices[c].id, unit, unitLabel, label );
			GameWindow *button = WOLMakeButtonLike( parentWindow, buttonBack, f.midX, f.top, cardWidth( f ), down( 100.0f ),
				label.isEmpty() ? "GUI:OnlineSkinsDefault" : label.str() );
			if (button)
			{
				button->winSetFont( strong );
				GadgetButtonSetText( button, UIStyle::fitText( button->winGetText(), strong, cardRoom ) );
				button->winSetDrawFunc( drawSkinChoice );
				button->winHide( TRUE );
			}
			row.choices[c].button = button;
		}
	}

	// the filter at the title line's right end: All, Owned, Locked
	const Int inset = across( 20.0f );
	const Int filterGap = across( 8.0f );
	// 260 kept at the left for the unit's name and its count
	const Int filterWidth = min( across( 130.0f ), ( f.midWidth - 2 * inset - across( 260.0f ) - 2 * filterGap ) / FILTER_COUNT );
	static const char *const FILTER_KEY[ FILTER_COUNT ] = { "GUI:OnlineSkinsAll", "GUI:OnlineSkinsOwned", "GUI:OnlineSkinsFilterLocked" };
	for (Int i = 0; i < FILTER_COUNT; ++i)
	{
		const Int x = f.midX + f.midWidth - inset - ( FILTER_COUNT - i ) * filterWidth - ( FILTER_COUNT - 1 - i ) * filterGap;
		buttonFilter[i] = WOLMakeButtonLike( parentWindow, buttonBack, x, f.top + down( 15.0f ), filterWidth, down( 34.0f ), FILTER_KEY[i] );
		if (buttonFilter[i])
		{
			buttonFilter[i]->winSetFont( body );
			GadgetButtonSetText( buttonFilter[i], UIStyle::fitText( buttonFilter[i]->winGetText(), body, filterWidth - 2 * filterGap ) );
			buttonFilter[i]->winSetDrawFunc( drawSkinFilter );
		}
	}

	// the platform's one button at its foot, its label set by layoutSkins
	const Int pad = across( 24.0f );
	const Int actionHeight = down( 56.0f );
	buttonAction = WOLMakeButtonLike( parentWindow, buttonBack, f.rightX + pad, f.bottom - down( 24.0f ) - actionHeight,
		f.rightWidth - 2 * pad, actionHeight, "GUI:OnlineSkinsWear" );
	buttonSave = rowButton( 1308.0f, UIStyle::ROOM_WIDE_BUTTON_WIDTH, "GUI:OnlineSkinsSave" );

	s_skinsState = -1;
	s_skinsDirty = TRUE;
	s_skinsSaved = FALSE;
	s_skinFilter = FILTER_ALL;
	picksFromLoadout();

	// the first unit and what it wears
	s_skinUnit = 0;
	s_skinFocus = s_skinRows.empty() ? 0 : s_skinRows[0].pick;
	OnlineRequestSkins();		// a code redeemed on the site since the sign-in
}

static void freeWords( DisplayString *&words )
{
	if (words)
		TheDisplayStringManager->freeDisplayString( words );
	words = NULL;
}

static void destroySkinsGadgets( void )
{
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		if (s_skinRows[r].tile)
			TheWindowManager->winDestroy( s_skinRows[r].tile );
		freeWords( s_skinRows[r].count );
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
		{
			if (s_skinRows[r].choices[c].button)
				TheWindowManager->winDestroy( s_skinRows[r].choices[c].button );
		}
	}
	const Bool hadRows = !s_skinRows.empty();
	s_skinRows.clear();
	GameWindow *made[] = { buttonSave, buttonAction, buttonFilter[0], buttonFilter[1], buttonFilter[2] };
	for (size_t i = 0; i < ARRAY_SIZE( made ); ++i)
		if (made[i])
			TheWindowManager->winDestroy( made[i] );
	buttonSave = buttonAction = NULL;
	for (Int i = 0; i < FILTER_COUNT; ++i)
		buttonFilter[i] = NULL;
	s_hero.lo.x = s_hero.hi.x = s_hero.lo.y = s_hero.hi.y = 0;
	if (hadRows && TheDisplay)
		TheDisplay->freeModelPreviews();		// nothing of the models stays for a match that follows
}

static void layoutSkins( OnlineSkinsState state, UnsignedInt64 owned, const AsciiString &error )
{
	clearPieces();
	s_hero.lo.x = s_hero.hi.x = s_hero.lo.y = s_hero.hi.y = 0;
	const SkinsFrame f = skinsFrame();
	const Int height = f.bottom - f.top;
	GameFont *body = UIStyle::readableFont( UIStyle::TEXT_BODY );
	GameFont *title = UIStyle::titleFont( 16 );

	addBox( f.listX, f.top, f.listWidth, height );
	addBox( f.midX, f.top, f.midWidth, height );
	addBox( f.rightX, f.top, f.rightWidth, height );

	// every look's state, every unit's count; a side's heading over its first unit
	Int side = -1;
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		SkinRow &row = s_skinRows[r];
		Int have = 0;
		for (size_t c = 0; c < row.choices.size(); ++c)
		{
			SkinChoice &choice = row.choices[c];
			choice.owned = choice.id == 0 || (owned & ((SkinLoadout)1 << choice.id)) != 0;
			have += choice.owned ? 1 : 0;
		}
		UnicodeString count = number( have );
		count.concat( u" / " );
		count.concat( number( (Int)row.choices.size() ) );
		freeWords( row.count );
		row.count = newWords( count, body );

		const Int its = rowSide( r );
		if (its != side && its >= 0)
		{
			Int x, y, width, rowHeight;
			unitPlace( r, f, x, y, width, rowHeight );
			addWords( UIStyle::fitText( TheGameText->fetch( SKIN_SIDE_KEY[ its ] ), UIStyle::sectionFont(), width ),
				UIStyle::sectionFont(), x + across( 4.0f ), y - down( 40.0f ), UIStyle::ACTIVE, ALIGN_LEFT, down( 40.0f ) );
		}
		side = its;
	}

	SkinRow *row = s_skinUnit < s_skinRows.size() ? &s_skinRows[ s_skinUnit ] : NULL;
	SkinChoice *focus = row ? focusChoice( *row ) : NULL;
	if (focus)
		s_skinFocus = focus->id;

	// the middle: the unit's name and its owned count left of the filter, the bar, the cards
	const Int inset = across( 20.0f );
	const Int inner = f.midWidth - 2 * inset;
	const Int midLeft = f.midX + inset;
	if (row)
	{
		Int have = 0;
		for (size_t c = 0; c < row->choices.size(); ++c)
			have += row->choices[c].owned ? 1 : 0;
		const Int looks = (Int)row->choices.size();
		UnicodeString count = number( have );
		count.concat( u" / " );
		count.concat( number( looks ) );
		Int filterLeft = f.midX + f.midWidth - inset, filterTop;
		if (buttonFilter[0])
			buttonFilter[0]->winGetScreenPosition( &filterLeft, &filterTop );
		const Int room = filterLeft - across( 16.0f ) - midLeft - measure( count, body ) - across( 14.0f );
		const Int titleWidth = addWords( UIStyle::fitName( row->name, title, room ), title, midLeft, f.top + down( 10.0f ), UIStyle::INK, ALIGN_LEFT, down( 44.0f ) );
		addWords( count, body, midLeft + titleWidth + across( 14.0f ), f.top + down( 10.0f ), UIStyle::MUTED, ALIGN_LEFT, down( 44.0f ) );

		// the bar: a segment a look, the owned ones lit
		const Int segmentGap = across( 4.0f );
		const Int segment = ( inner - ( looks - 1 ) * segmentGap ) / max( 1, looks );
		for (Int i = 0; i < looks; ++i)
			addPiece( PIECE_FILL, midLeft + i * ( segment + segmentGap ), f.top + down( 64.0f ), segment, max( 2, down( 10.0f ) ),
				i < have ? UIStyle::EDGE : UIStyle::GRID );
	}

	// the cards: the shown unit's looks the filter lets through, Default first, the rest hidden
	Int shown = 0;
	if (row)
		for (size_t c = 0; c < row->choices.size(); ++c)
			if (s_skinFilter == FILTER_ALL || row->choices[c].owned == ( s_skinFilter == FILTER_OWNED ))
				++shown;
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		Int slot = 0;
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
		{
			SkinChoice &choice = s_skinRows[r].choices[c];
			if (!choice.button)
				continue;
			const Bool passes = s_skinFilter == FILTER_ALL || choice.owned == ( s_skinFilter == FILTER_OWNED );
			if (r != s_skinUnit || !passes)
			{
				choice.button->winHide( TRUE );
				continue;
			}
			Int x, y, width, cardHeight;
			cardPlace( slot++, shown, f, x, y, width, cardHeight );
			choice.button->winSetPosition( x, y );
			choice.button->winSetSize( width, cardHeight );
			choice.button->winHide( FALSE );
		}
	}

	// the platform: the button at its foot, the look's name over it, the model in all the rest
	const Int pad = across( 24.0f );
	const Int rightLeft = f.rightX + pad;
	const Int rightInner = f.rightWidth - 2 * pad;
	if (row && focus)
	{
		AsciiString unit, unitLabel, label;
		if (focus->id)
			TankSkin_describe( focus->id, unit, unitLabel, label );
		const UnicodeString name = TheGameText->fetch( label.isEmpty() ? "GUI:OnlineSkinsDefault" : label.str() );
		const Int buttonTop = f.bottom - down( 24.0f ) - down( 56.0f );
		const Int nameBand = down( 48.0f );
		const Int nameTop = buttonTop - down( 8.0f ) - nameBand;
		const Color nameInk = TankSkin_isRare( focus->id ) ? ( focus->owned ? UIStyle::GOLD : SKIN_RARE_LOCKED )
			: focus->owned ? UIStyle::INK : UIStyle::MUTED;
		addWords( UIStyle::fitText( name, title, rightInner ), title, rightLeft, nameTop, nameInk, ALIGN_LEFT, nameBand );
		s_hero.lo.x = f.rightX + across( 8.0f );
		s_hero.lo.y = f.top + down( 8.0f );
		s_hero.hi.x = f.rightX + f.rightWidth - across( 8.0f );
		s_hero.hi.y = nameTop;
		s_heroOwned = focus->owned;

		if (buttonAction)
		{
			// a look the account lacks: what earns it, as the button's label; the button opens the site
			const AsciiString earned = TankSkin_earnedBy( focus->id );
			const char *action;
			Bool live;
			if (!focus->owned)
			{
				action = earned.isEmpty() ? "GUI:OnlineSkinsOpenSite" : earned.str();
				live = OnlineGetAccountUrl().isNotEmpty();
			}
			else if (focus->id == row->pick)
			{
				action = row->pick == row->worn ? "GUI:OnlineSkinsWornButton" : "GUI:OnlineSkinsPickedButton";
				live = FALSE;
			}
			else
			{
				action = "GUI:OnlineSkinsWear";
				live = state != ONLINE_SKINS_WAITING;		// an ask's answer resets the picks to what the site holds
			}
			Int width, buttonHeight;
			buttonAction->winGetSize( &width, &buttonHeight );
			GadgetButtonSetText( buttonAction, UIStyle::fitKey( action, buttonAction->winGetFont(), width - 2 * across( 12.0f ) ) );
			buttonAction->winEnable( live );
			buttonAction->winHide( FALSE );
		}
	}
	else if (buttonAction)
		buttonAction->winHide( TRUE );

	// between Back and Save: the unsaved picks counted, else how the last ask went
	const Int pending = unsavedCount();
	UnicodeString line;
	Color lineColor = UIStyle::INK;
	if (pending == 1)
		line = TheGameText->fetch( "GUI:OnlineSkinsPending1" );
	else if (pending > 1)
		line.format( TheGameText->fetch( "GUI:OnlineSkinsPending" ), pending );
	else
	{
		const char *key = NULL;
		if (state == ONLINE_SKINS_WAITING)
			key = "GUI:OnlineSkinsWaiting";
		else if (state == ONLINE_SKINS_READY)
			key = s_skinsSaved ? "GUI:OnlineSkinsSaved" : owned == 0 ? "GUI:OnlineSkinsNone" : NULL;
		else if (error == "signedout")
			key = "GUI:OnlineSkinsSignedOut";
		else if (error == "throttled")
			key = "GUI:OnlineSkinsThrottled";
		else if (error == "unavailable")
			key = "GUI:OnlineSkinsUnavailable";
		else
			key = "GUI:OnlineSkinsRefused";		// not_owned, two_for_unit, unknown: the site's view changed under us
		if (key)
			line = TheGameText->fetch( key );
		lineColor = state == ONLINE_SKINS_READY && s_skinsSaved ? UIStyle::ACTIVE : state == ONLINE_SKINS_WAITING ? UIStyle::MUTED : UIStyle::INK;
	}
	if (!line.isEmpty() && buttonBack)
	{
		Int backX, backY, backWidth, backHeight;
		buttonBack->winGetScreenPosition( &backX, &backY );
		buttonBack->winGetSize( &backWidth, &backHeight );
		const Int right = UIStyle::boardX( 1308.0f ) - UIStyle::px( 20.0f );
		const Int room = right - ( backX + backWidth + UIStyle::px( UIStyle::ROOM_BUTTON_GAP ) );
		if (room > 0)
			addWords( UIStyle::fitText( line, body, room ), body, right, backY, lineColor, ALIGN_RIGHT, backHeight );
	}
	if (buttonSave)
		buttonSave->winEnable( state != ONLINE_SKINS_WAITING && owned != 0 && pending > 0 );
}

static void drawSkins( void )
{
	UnsignedInt64 owned = 0;
	AsciiString error;
	const OnlineSkinsState state = OnlineGetSkins( owned, error );
	if (!s_skinsDirty && (Int)state == s_skinsState && owned == s_skinsOwnedShown)
		return;
	if (state == ONLINE_SKINS_READY && s_skinsState == (Int)ONLINE_SKINS_WAITING)
		picksFromLoadout();		// an answer arrived: what the site holds now, after a save or a fresh read
	s_skinsState = (Int)state;
	s_skinsOwnedShown = owned;
	s_skinsDirty = FALSE;
	DEBUG_LOG(( "ONLINE SKINS state=%d error=%s owned=%s worn=%s rows=%d\n", (Int)state, error.str(),
		TankSkin_slotField( owned ).str(), TankSkin_slotField( TankSkin_localLoadout() ).str(), (Int)s_skinRows.size() ));
	layoutSkins( state, owned, error );
	if (s_skinUnit < s_skinRows.size())
		DEBUG_LOG(( "SKINS PAGE unit=%s focus=%d filter=%d pick=%d worn=%d unsaved=%d\n", s_skinRows[ s_skinUnit ].unit.str(),
			s_skinFocus, s_skinFilter, s_skinRows[ s_skinUnit ].pick, s_skinRows[ s_skinUnit ].worn, unsavedCount() ));
}

/* A unit's row clicked: its looks show, the one it wears in focus.  A card clicked: that look in
	 focus.  A filter: the cards it lets through.  The platform's button: Wear the look in focus, or
	 for a look the account lacks the site's account page.  FALSE when the control is none of them. */
static Bool pickSkin( GameWindow *control )
{
	for (Int i = 0; i < FILTER_COUNT; ++i)
		if (control == buttonFilter[i])
		{
			s_skinFilter = i;
			s_skinsDirty = TRUE;
			drawSkins();
			return TRUE;
		}
	if (control == buttonAction && s_skinUnit < s_skinRows.size())
	{
		SkinRow &row = s_skinRows[ s_skinUnit ];
		SkinChoice *focus = focusChoice( row );
		if (focus && !focus->owned)
		{
			if (OnlineGetAccountUrl().isNotEmpty())
				OnlineOpenURL( OnlineGetAccountUrl().str() );
		}
		else if (focus)
		{
			row.pick = focus->id;
			s_skinsSaved = FALSE;
			s_skinsDirty = TRUE;
			drawSkins();
		}
		return TRUE;
	}
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		if (s_skinRows[r].tile == control)
		{
			s_skinUnit = r;
			s_skinFocus = s_skinRows[r].pick;
			s_skinsDirty = TRUE;
			drawSkins();
			return TRUE;
		}
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
			if (s_skinRows[r].choices[c].button == control)
			{
				s_skinFocus = s_skinRows[r].choices[c].id;
				s_skinsDirty = TRUE;
				drawSkins();
				return TRUE;
			}
	}
	return FALSE;
}

static void saveSkins( void )
{
	SkinLoadout wear = 0;
	for (size_t r = 0; r < s_skinRows.size(); ++r)
		if (s_skinRows[r].pick)
			wear |= (SkinLoadout)1 << s_skinRows[r].pick;
	s_skinsSaved = TRUE;
	OnlineSaveSkins( wear );
	DEBUG_LOG(( "SKINS PAGE save wear=%s\n", TankSkin_slotField( wear ).str() ));
	drawSkins();
}

/* Back, or a tab (UIStyle::onlineTabClicked): Back's body, which holds nothing to ask about here, then
	 the tab's screen as the next one, pushed over the hub once this one is popped; Home only pops. */
static void leaveTo( UIStyle::OnlineTab to )
{
	nextScreen = UIStyle::onlineTabScreen( to );
	TheShell->pop();
}


//-------------------------------------------------------------------------------------------------
/** Initialize the single player menu */
//-------------------------------------------------------------------------------------------------
void WOLLadderScreenInit( WindowLayout *layout, void *userData )
{
	TheShell->showShellMap(TRUE);
	nextScreen = NULL;

	// get ids for our children controls
	parentWindowID = TheNameKeyGenerator->nameToKey( AsciiString("WOLLadderScreen.wnd:LadderParent") );
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString("WOLLadderScreen.wnd:ButtonBack") );
	windowLadderID = TheNameKeyGenerator->nameToKey( AsciiString("WOLLadderScreen.wnd:WindowLadder") );

	parentWindow = TheWindowManager->winGetWindowFromId( NULL, parentWindowID );
	buttonBack = TheWindowManager->winGetWindowFromId( parentWindow, buttonBackID );
	windowLadder = TheWindowManager->winGetWindowFromId( parentWindow, windowLadderID );

	if (OnlineIsEnabled())
	{
		// one layout serves both tabs: Skins when the hub's tab or WOLSkinsScreenOpen asked for it
		s_skinsPage = s_skinsPage || UIStyle::onlineTabAsked() == UIStyle::ONLINE_TAB_SKINS;
		UIStyle::onlinePageInit( parentWindow, "WOLLadderScreen.wnd", s_skinsPage ? UIStyle::ONLINE_TAB_SKINS : UIStyle::ONLINE_TAB_LADDER );
		if (windowLadder)
			windowLadder->winSetDrawFunc( drawPanel );
		if (s_skinsPage)
		{
			GameWindow *title = TheWindowManager->winGetWindowFromId( parentWindow,
				TheNameKeyGenerator->nameToKey( AsciiString( "WOLLadderScreen.wnd:StaticTextTitle" ) ) );
			if (title)
				GadgetStaticTextSetText( title, TheGameText->fetch( "GUI:OnlineSkinsButton" ) );
			createSkinsGadgets();
			drawSkins();
		}
		else
		{
			createLadderGadgets();
			requestLadderPage( 0 );
		}
	}
	else if (TheWebBrowser != NULL)
	{
		TheWebBrowser->createBrowserWindow("MessageBoard", windowLadder);
	}

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentWindow );

}  // end ReplayMenuInit

//-------------------------------------------------------------------------------------------------
/** single player menu shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLLadderScreenShutdown( WindowLayout *layout, void *userData )
{

	destroyLadderGadgets();
	destroySkinsGadgets();
	clearPieces();
	UIStyle::onlinePageShutdown( parentWindow );
	s_skinsPage = FALSE;		// the Ladder tab opens the ladder again
	if (TheWebBrowser != NULL && !OnlineIsEnabled())
	{
		TheWebBrowser->closeBrowserWindow(windowLadder);
	}

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete; a tab's screen goes over the hub this uncovers
	TheShell->shutdownComplete( layout, nextScreen != NULL );
	if (nextScreen != NULL)
		TheShell->push( AsciiString( nextScreen ) );
	nextScreen = NULL;

}  // end ReplayMenuShutdown

//-------------------------------------------------------------------------------------------------
/** single player menu update method */
//-------------------------------------------------------------------------------------------------
void WOLLadderScreenUpdate( WindowLayout *layout, void *userData )
{
	if (OnlineIsEnabled())
	{
		OnlineUpdate();		// the lines this screen waits on, whatever lies under it on the shell stack
		if (s_skinsPage)
			drawSkins();
		else
			drawLadder();
	}
}  // end ReplayMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Replay menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLadderScreenInput( GameWindow *window, UnsignedInt msg,
																						WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{

					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)buttonBack, buttonBackID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}  // end ReplayMenuInput

//-------------------------------------------------------------------------------------------------
/** single player menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLadderScreenSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{


			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}  // end input
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if (UIStyle::onlineTabClicked( control, leaveTo ))
				break;

			if( controlID == buttonBackID )
			{

				// thou art directed to return to thy known solar system immediately!
				leaveTo( UIStyle::ONLINE_TAB_HOME );

			}  // end else if
			else if( control == buttonPrev )
			{
				requestLadderPage( s_ladderOffset - LADDER_PAGE );
			}
			else if( control == buttonNext )
			{
				requestLadderPage( s_ladderOffset + LADDER_PAGE );
			}
			else if( control == buttonJump )
			{
				requestLadderPage( myPage() );
			}
			else if( control == buttonSave )
			{
				saveSkins();
			}
			else
			{
				pickSkin( control );
			}

			break;

		}  // end selected

		default:
			return MSG_IGNORED;
	}  // end switch

	return MSG_HANDLED;
}  // end ReplayMenuSystem

