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
	addWords( TheGameText->fetch( "GUI:OnlineLadderSub" ), UIStyle::sectionFont(), table.lo.x + inset, table.lo.y + down( 20.0f ), UIStyle::ACTIVE );
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
// The skins page, the Skins tab: a card per unit SkinsReforged.ini dresses, its cameo, name and side,
// and a choice card for Default and each skin for it.  A skin the account lacks stays on its card,
// disabled.  Save sends the whole loadout; once the site took it the next room and Quick Match seat
// it (OnlineSaveSkins).  The same layout as the ladder, no layout of its own to ship.
//-------------------------------------------------------------------------------------------------
struct SkinChoice
{
	GameWindow *button;
	Int id;									///< 0 for Default
	Bool owned;
	DisplayString *chip;		///< Worn, Picked not saved, Owned or Not owned
	Color chipColor;
	DisplayString *hint;		///< under a skin the account lacks: where to get it; NULL otherwise
};
struct SkinRow
{
	AsciiString unit;
	UnicodeString name;
	std::vector<SkinChoice> choices;
	Int pick;								///< picked on this page
	Int worn;								///< what the account wears, TankSkin_localLoadout
};
static Bool s_skinsPage = FALSE;
static std::vector<SkinRow> s_skinRows;
static GameWindow *buttonSave = NULL;
static GameWindow *buttonSite = NULL;
static Int s_skinsState = -1;					///< OnlineSkinsState last laid out
static UnsignedInt64 s_skinsOwnedShown = 0;
static Bool s_skinsDirty = TRUE;				///< lay out again even with the state unchanged
static Bool s_skinsSaved = FALSE;			///< the last READY answers this page's Save

/// the cameo and side of each unit SkinsReforged.ini dresses; a unit missing here shows its name alone
static const struct { const char *unit; const char *cameo; const char *side; } SKIN_UNITS[] =
{
	{ "Crusader", "SACLeopard", "GUI:OnlineSkinsUsaTank" },
	{ "Battlemaster", "SNBattlemaster", "GUI:OnlineSkinsChinaTank" },
	{ "Scorpion", "SUScorpion", "GUI:OnlineSkinsGlaTank" },
};

void WOLSkinsScreenOpen( void )
{
	s_skinsPage = TRUE;
	TheShell->push( AsciiString( "Menus/WOLLadderScreen.wnd" ) );
}

/// unit r's card in the units box; ponytail: three fit the box, a fourth wants a scrolling list
static void unitCard( Int r, Int &x, Int &y, Int &width, Int &height )
{
	IRegion2D units, side;
	UIStyle::onlineColumns( units, side );
	x = units.lo.x + across( 20.0f );
	width = units.hi.x - units.lo.x - across( 40.0f );
	y = units.lo.y + down( 56.0f + 212.0f * r );		// three cards end at 692 of the band's 710
	height = down( 200.0f );
}

/// choice c of count on unit r's card: 370 by 152 beside the cameo, narrower where the card is
static void choiceCard( Int r, Int c, Int count, Int &x, Int &y, Int &width, Int &height )
{
	Int cardX, cardY, cardWidth, cardHeight;
	unitCard( r, cardX, cardY, cardWidth, cardHeight );
	const Int first = cardX + across( 290.0f );
	const Int gap = across( 20.0f );
	const Int room = cardX + cardWidth - across( 18.0f ) - first;
	width = min( across( 370.0f ), ( room - ( count - 1 ) * gap ) / max( 1, count ) );
	x = first + c * ( width + gap );
	y = cardY + down( 24.0f );
	height = down( 152.0f );
}

static void freeChoiceWords( SkinChoice &choice )
{
	if (choice.chip)
		TheDisplayStringManager->freeDisplayString( choice.chip );
	if (choice.hint)
		TheDisplayStringManager->freeDisplayString( choice.hint );
	choice.chip = choice.hint = NULL;
}

/** A choice card, drawn over its push button: the panel's black under a FRAME edge, the three-line
	* edge on the pick; its name in the Generals face (MUTED when the account lacks it, ACTIVE under
	* the mouse), the chip under it and, for a skin the account lacks, where to get it. */
static void drawSkinChoice( GameWindow *window, WinInstanceData *instData )
{
	const SkinRow *row = NULL;
	const SkinChoice *choice = NULL;
	for (size_t r = 0; r < s_skinRows.size() && !choice; ++r)
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
	TheDisplay->drawFillRect( x, y, width, height, UIStyle::PANEL );
	if (choice->id == row->pick)
		UIStyle::edge( x, y, width, height );
	else
		TheDisplay->drawOpenRect( x, y, width, height, 1.0f, UIStyle::FRAME );

	const Int inset = across( 24.0f );
	const Bool hovered = BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) && BitTest( instData->getState(), WIN_STATE_HILITED );
	DisplayString *name = instData->getTextDisplayString();
	if (name->getFont() != window->winGetFont())
		name->setFont( window->winGetFont() );
	name->draw( x + inset, y + down( 26.0f ), !choice->owned ? UIStyle::MUTED : hovered ? UIStyle::ACTIVE : UIStyle::INK, UIStyle::SHADOW );
	if (choice->chip)
	{
		UIStyle::chip( choice->chip, x + inset, y + down( 80.0f ), choice->chipColor, UIStyle::FRAME );
		Int chipWidth, chipHeight;
		choice->chip->getSize( &chipWidth, &chipHeight );
		if (choice->hint)
		{
			// under the chip (its label and its own 5 over and 4 under), kept inside a squeezed card
			Int hintWidth, hintHeight;
			choice->hint->getSize( &hintWidth, &hintHeight );
			const Int under = y + down( 80.0f ) + chipHeight + UIStyle::tallPx( 9.0f ) + down( 12.0f );
			choice->hint->draw( x + inset, min( under, y + height - down( 8.0f ) - hintHeight ), UIStyle::MUTED, UIStyle::SHADOW );
		}
	}
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
		SkinChoice choice = { NULL, id, FALSE, NULL, UIStyle::MUTED, NULL };
		if (r == s_skinRows.size())
		{
			SkinRow row;
			row.unit = unit;
			if (unitLabel.isEmpty())
				row.name.translate( unit );
			else
				row.name = TheGameText->fetch( unitLabel );
			row.pick = row.worn = 0;
			SkinChoice none = { NULL, 0, TRUE, NULL, UIStyle::MUTED, NULL };
			row.choices.push_back( none );
			s_skinRows.push_back( row );
		}
		s_skinRows[r].choices.push_back( choice );
	}

	// the choices, push buttons drawn as the cards
	GameFont *face = UIStyle::titleFont( 15 );
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		const Int count = (Int)s_skinRows[r].choices.size();
		for (Int c = 0; c < count; ++c)
		{
			AsciiString unit, unitLabel, label = "GUI:OnlineSkinsDefault";
			if (s_skinRows[r].choices[c].id)
				TankSkin_describe( s_skinRows[r].choices[c].id, unit, unitLabel, label );
			Int x, y, width, height;
			choiceCard( (Int)r, c, count, x, y, width, height );
			GameWindow *button = WOLMakeButtonLike( parentWindow, buttonBack, x, y, width, height,
				label.isEmpty() ? "GUI:OnlineSkinsDefault" : label.str() );
			if (button)
			{
				button->winSetFont( face );
				GadgetButtonSetText( button, UIStyle::fitText( button->winGetText(), face, width - 2 * across( 24.0f ) ) );
				button->winSetDrawFunc( drawSkinChoice );
			}
			s_skinRows[r].choices[c].button = button;
		}
	}

	IRegion2D units, side;
	UIStyle::onlineColumns( units, side );
	const Int inset = across( 24.0f );
	if (OnlineGetAccountUrl().isNotEmpty())
		buttonSite = smallButton( side.lo.x + inset, side.lo.y + down( 184.0f ), side.hi.x - side.lo.x - 2 * inset, "GUI:OnlineSkinsOpenSite" );
	buttonSave = rowButton( 1308.0f, UIStyle::ROOM_WIDE_BUTTON_WIDTH, "GUI:OnlineSkinsSave" );

	s_skinsState = -1;
	s_skinsDirty = TRUE;
	s_skinsSaved = FALSE;
	picksFromLoadout();
	OnlineRequestSkins();		// a code redeemed on the site since the sign-in
}

static void destroySkinsGadgets( void )
{
	for (size_t r = 0; r < s_skinRows.size(); ++r)
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
		{
			if (s_skinRows[r].choices[c].button)
				TheWindowManager->winDestroy( s_skinRows[r].choices[c].button );
			freeChoiceWords( s_skinRows[r].choices[c] );
		}
	s_skinRows.clear();
	GameWindow *made[] = { buttonSave, buttonSite };
	for (size_t i = 0; i < ARRAY_SIZE( made ); ++i)
		if (made[i])
			TheWindowManager->winDestroy( made[i] );
	buttonSave = buttonSite = NULL;
}

static void layoutSkins( OnlineSkinsState state, UnsignedInt64 owned, const AsciiString &error )
{
	clearPieces();
	IRegion2D units, side;
	UIStyle::onlineColumns( units, side );
	const Int inset = across( 24.0f );
	GameFont *body = UIStyle::readableFont( UIStyle::TEXT_BODY );
	GameFont *strong = UIStyle::font( UIStyle::TEXT_STRONG, TRUE );

	// the units box, a card per unit: its cameo, its name and side, and each choice's chip
	addBox( units.lo.x, units.lo.y, units.hi.x - units.lo.x, units.hi.y - units.lo.y );
	addWords( TheGameText->fetch( "GUI:OnlineSkinsSub" ), UIStyle::sectionFont(), units.lo.x + inset, units.lo.y + down( 20.0f ), UIStyle::ACTIVE );
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		SkinRow &row = s_skinRows[r];
		Int x, y, width, height;
		unitCard( (Int)r, x, y, width, height );
		addBox( x, y, width, height );
		const char *cameo = NULL;
		const char *sideKey = NULL;
		for (size_t u = 0; u < ARRAY_SIZE( SKIN_UNITS ); ++u)
			if (row.unit.compareNoCase( SKIN_UNITS[u].unit ) == 0)
			{
				cameo = SKIN_UNITS[u].cameo;
				sideKey = SKIN_UNITS[u].side;
			}
		addPicture( cameo ? TheMappedImageCollection->findImageByName( cameo ) : NULL, x + across( 22.0f ), y + down( 26.0f ), down( 150.0f ), down( 120.0f ) );
		const Int nameRoom = across( 260.0f );
		const Int nameBand = down( 30.0f );
		const Int nameWidth = addWords( UIStyle::fitText( row.name, strong, nameRoom ), strong, x + across( 22.0f ), y + down( 156.0f ), UIStyle::INK, ALIGN_LEFT, nameBand );
		const Int sideRoom = nameRoom - nameWidth - across( 10.0f );
		if (sideKey && sideRoom > 0)
			addWords( UIStyle::fitText( TheGameText->fetch( sideKey ), body, sideRoom ), body, x + across( 32.0f ) + nameWidth, y + down( 156.0f ),
				UIStyle::MUTED, ALIGN_LEFT, nameBand );

		for (size_t c = 0; c < row.choices.size(); ++c)
		{
			SkinChoice &choice = row.choices[c];
			choice.owned = choice.id == 0 || (owned & ((SkinLoadout)1 << choice.id)) != 0;
			const Bool picked = choice.id == row.pick;
			const char *chip = picked ? ( choice.id == row.worn ? "GUI:OnlineSkinsWorn" : "GUI:OnlineSkinsUnsaved" )
				: choice.owned ? "GUI:OnlineSkinsOwned" : "GUI:OnlineSkinsLocked";
			Int cx, cy, cw, ch;
			choiceCard( (Int)r, (Int)c, (Int)row.choices.size(), cx, cy, cw, ch );
			const Int room = cw - 2 * inset;
			freeChoiceWords( choice );
			choice.chip = newWords( UIStyle::fitText( TheGameText->fetch( chip ), UIStyle::chipFont(), room - 2 * across( 8.0f ) ), UIStyle::chipFont() );
			choice.chipColor = picked ? UIStyle::ACTIVE : UIStyle::MUTED;
			if (!choice.owned)
				choice.hint = newWords( UIStyle::fitText( TheGameText->fetch( "GUI:OnlineSkinsLockedHint" ), body, room ), body );
			// nothing to pick while an ask is out: its answer resets the picks to what the site holds
			if (choice.button)
				choice.button->winEnable( choice.owned && state != ONLINE_SKINS_WAITING );
		}
	}

	// the side box: whose a skin is, the site's account page, who sees skins, then how the last ask went
	const Int sideWidth = side.hi.x - side.lo.x;
	const Int inner = sideWidth - 2 * inset;
	const Int x = side.lo.x + inset;
	addBox( side.lo.x, side.lo.y, sideWidth, side.hi.y - side.lo.y );
	addWords( TheGameText->fetch( "GUI:OnlineSkinsGet" ), UIStyle::sectionFont(), x, side.lo.y + down( 20.0f ), UIStyle::ACTIVE );
	addParagraph( TheGameText->fetch( "GUI:OnlineSkinsGet1" ), body, x, side.lo.y + down( 60.0f ), inner, UIStyle::INK );
	addPiece( PIECE_FILL, x, side.lo.y + down( 256.0f ), inner, 1, UIStyle::GRID );
	addWords( TheGameText->fetch( "GUI:OnlineSkinsWho" ), UIStyle::sectionFont(), x, side.lo.y + down( 278.0f ), UIStyle::ACTIVE );
	Int y = side.lo.y + down( 318.0f );
	y += addParagraph( TheGameText->fetch( "GUI:OnlineSkinsWho1" ), body, x, y, inner, UIStyle::MUTED ) + down( 24.0f );
	addPiece( PIECE_FILL, x, y, inner, 1, UIStyle::GRID );

	const char *line;
	if (state == ONLINE_SKINS_WAITING)
		line = "GUI:OnlineSkinsWaiting";
	else if (state == ONLINE_SKINS_READY)
		line = s_skinsSaved ? "GUI:OnlineSkinsSaved" : owned == 0 ? "GUI:OnlineSkinsNone" : "GUI:OnlineSkinsHint";
	else if (error == "signedout")
		line = "GUI:OnlineSkinsSignedOut";
	else if (error == "throttled")
		line = "GUI:OnlineSkinsThrottled";
	else if (error == "unavailable")
		line = "GUI:OnlineSkinsUnavailable";
	else
		line = "GUI:OnlineSkinsRefused";		// not_owned, two_for_unit, unknown: the site's view changed under us
	const Bool bad = state == ONLINE_SKINS_FAILED || (state == ONLINE_SKINS_READY && owned == 0);
	const Color lineColor = bad ? UIStyle::INK : s_skinsSaved && state == ONLINE_SKINS_READY ? UIStyle::ACTIVE : UIStyle::MUTED;
	addParagraph( TheGameText->fetch( line ), body, x, y + down( 22.0f ), inner, lineColor );

	// the unsaved picks counted beside Save
	const Int pending = unsavedCount();
	if (pending > 0 && buttonBack)
	{
		UnicodeString count;
		if (pending == 1)
			count = TheGameText->fetch( "GUI:OnlineSkinsPending1" );
		else
			count.format( TheGameText->fetch( "GUI:OnlineSkinsPending" ), pending );
		Int backX, backY, backWidth, backHeight;
		buttonBack->winGetScreenPosition( &backX, &backY );
		buttonBack->winGetSize( &backWidth, &backHeight );
		const Int right = UIStyle::boardX( 1308.0f ) - UIStyle::px( 20.0f );
		const Int room = right - ( backX + backWidth + UIStyle::px( UIStyle::ROOM_BUTTON_GAP ) );
		if (room > 0)
			addWords( UIStyle::fitText( count, body, room ), body, right, backY, UIStyle::INK, ALIGN_RIGHT, backHeight );
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
}

/* A choice clicked: that row's pick.  FALSE when the control is none of them. */
static Bool pickSkin( GameWindow *control )
{
	for (size_t r = 0; r < s_skinRows.size(); ++r)
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
			if (s_skinRows[r].choices[c].button == control)
			{
				s_skinRows[r].pick = s_skinRows[r].choices[c].id;
				s_skinsSaved = FALSE;
				s_skinsDirty = TRUE;
				drawSkins();
				return TRUE;
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
			else if( control == buttonSite )
			{
				OnlineOpenURL( OnlineGetAccountUrl().str() );
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

