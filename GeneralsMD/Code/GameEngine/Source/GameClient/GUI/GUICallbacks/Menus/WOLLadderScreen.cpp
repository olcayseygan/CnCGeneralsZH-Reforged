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
#include "GameClient/OnlineSelfLayout.h"
#include "GameClient/TankSkin.h"
#include "GameClient/UIStyle.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
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

/** A push button made in code with model's look, or none to be drawn by its own draw function; name (the
	* layout's "WOLLadderScreen.wnd:..." form) lets a script find it (ui_walk.py, the -control socket's
	* uitree), NULL for none. */
static GameWindow *makeButton( GameWindow *parent, GameWindow *model, Int x, Int y, Int w, Int h, const char *label,
	const char *name )
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
	if (name)
	{
		inst.m_decoratedNameString = name;
		inst.m_id = (Int)TheNameKeyGenerator->nameToKey( inst.m_decoratedNameString );
	}
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( parent,
		WIN_STATUS_ENABLED | (look ? WIN_STATUS_IMAGE : 0), x, y, w, h, &inst,
		model ? model->winGetFont() : NULL, look == NULL );
	if (button)
		GadgetButtonSetText( button, TheGameText->fetch( label ) );
	return button;
}

GameWindow *WOLMakeButtonLike( GameWindow *parent, GameWindow *model, Int x, Int y, Int w, Int h, const char *label )
{
	return makeButton( parent, model, x, y, w, h, label, NULL );
}

/// a master's window under the panel by its short name
static GameWindow *findChild( const char *name )
{
	AsciiString full( "WOLLadderScreen.wnd:" );
	full.concat( name );
	return TheWindowManager->winGetWindowFromId( parentWindow, TheNameKeyGenerator->nameToKey( full ) );
}

/// the board (README-online-self.md, every number at 1080), letterboxed as the online header is
static Int bx( Real at1080 ) { return UIStyle::boardX( at1080 ); }
static Int by( Real at1080 ) { return UIStyle::boardY( at1080 ); }
static Int bp( Real at1080 ) { return max( 1, UIStyle::px( at1080 ) ); }

static GameFont *body( Bool bold = FALSE ) { return UIStyle::menuFont( UIStyle::MENU_TEXT_BODY, bold ); }
static GameFont *figureFont( void ) { return UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE ); }
static GameFont *labelFont( void ) { return UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE ); }

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

/// a key's words; a key that is a format with no arguments ("Win %%") read as the words it prints
static UnicodeString words( const char *key )
{
	UnicodeString text;
	text.format( TheGameText->fetch( key ) );
	return text;
}

/// a label's capitals fitted to room as tracked 14 px capitals: the key's short form when the whole does not fit
static UnicodeString capsFit( const char *key, Int room )
{
	UnicodeString text = UIStyle::capitals( words( key ) );
	if (UIStyle::trackedWidth( text, labelFont(), UIStyle::MENU_LABEL_TRACKING ) <= room)
		return text;
	AsciiString shortKey = key;
	shortKey.concat( "_Short" );
	if (UIStyle::hasText( shortKey.str() ))
		return UIStyle::capitals( words( shortKey.str() ) );
	return text;
}

//-------------------------------------------------------------------------------------------------
// What the panel shows: the boxes, rows, pictures and words of the open page, laid out whenever its
// data changes and drawn every frame by drawPanel, the master's WindowLadder draw, after the head.
// The keys, the ladder's rows and the skins page's units, looks and filters are buttons over it.
//-------------------------------------------------------------------------------------------------
enum PieceKind { PIECE_FILL, PIECE_LINE, PIECE_IMAGE, PIECE_WORDS, PIECE_CAPS, PIECE_BOX, PIECE_ROW, PIECE_LIT, PIECE_CARET };
struct Piece
{
	PieceKind kind;
	Int x, y, width, height;
	Color color;
	const Image *image;
	DisplayString *words;
	Int state;								///< PIECE_ROW: a UIStyle::RowState
	Bool zebra;
	UnicodeString text;				///< PIECE_CAPS
};
static std::vector<Piece> s_pieces;

static void clearPieces( void )
{
	for (size_t i = 0; i < s_pieces.size(); ++i)
		if (s_pieces[i].words)
			TheDisplayStringManager->freeDisplayString( s_pieces[i].words );
	s_pieces.clear();
}

static Piece &addPiece( PieceKind kind, Int x, Int y, Int width, Int height, Color color = 0, const Image *image = NULL,
	DisplayString *words = NULL )
{
	Piece piece;
	piece.kind = kind;
	piece.x = x;
	piece.y = y;
	piece.width = width;
	piece.height = height;
	piece.color = color;
	piece.image = image;
	piece.words = words;
	piece.state = UIStyle::ROW_PLAIN;
	piece.zebra = FALSE;
	s_pieces.push_back( piece );
	return s_pieces.back();
}

/// a box on the board: black at 70 in a 1 px FRAME edge (UIStyle::onlineBox)
static void addBox( Real left, Real top, Real right, Real bottom )
{
	addPiece( PIECE_BOX, bx( left ), by( top ), bx( right ) - bx( left ), by( bottom ) - by( top ) );
}

/// a row's state (UIStyle::menuRow) on the screen
static void addRow( Int x, Int y, Int width, Int height, UIStyle::RowState state, Bool zebra )
{
	Piece &row = addPiece( PIECE_ROW, x, y, width, height );
	row.state = state;
	row.zebra = zebra;
}

/// a picture, framed in FRAME when framed; nothing for a missing one
static void addPicture( const Image *image, Int x, Int y, Int width, Int height, Bool framed = TRUE )
{
	if (!image)
		return;
	addPiece( PIECE_IMAGE, x, y, width, height, 0, image );
	if (framed)
		addPiece( PIECE_LINE, x - 1, y - 1, width + 2, height + 2, UIStyle::withAlpha( UIStyle::FRAME, 220 ) );
}

/// a track: a faint white channel and its share filled, the fill a lit line
static void addTrack( Int x, Int y, Int width, Int height, Real fraction, Color fill = UIStyle::EDGE )
{
	addPiece( PIECE_FILL, x, y, width, height, GameMakeColor( 255, 255, 255, 24 ) );
	const Int filled = REAL_TO_INT( width * max( 0.0f, min( 1.0f, fraction ) ) + 0.5f );
	if (filled > 0)
		addPiece( PIECE_LIT, x, y, max( 2, filled ), height, fill );
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
/** Words on the panel from x, or ending at x (ALIGN_RIGHT), centred on the band y to y + band (their
	* top at y when band is 0).  Returns their width. */
static Int addWords( const UnicodeString &text, GameFont *font, Int x, Int y, Color color, Int align = ALIGN_LEFT, Int band = 0 )
{
	if (text.isEmpty())
		return 0;
	DisplayString *words = newWords( text, font );
	Int width, height;
	words->getSize( &width, &height );
	if (align == ALIGN_RIGHT)
		x -= width;
	if (band > 0)
		y += ( band - height ) / 2;
	addPiece( PIECE_WORDS, x, y, width, height, color, NULL, words );
	return width;
}

/// tracked 14 px capitals (the labels, the heads), centred on the band; returns their width
static Int addCaps( const UnicodeString &text, Int x, Int y, Int band, Color color, Int align = ALIGN_LEFT )
{
	if (text.isEmpty())
		return 0;
	GameFont *font = labelFont();
	const Int width = UIStyle::trackedWidth( text, font, UIStyle::MENU_LABEL_TRACKING );
	Piece &caps = addPiece( PIECE_CAPS, align == ALIGN_RIGHT ? x - width : x, y + ( band - font->height ) / 2, width, font->height, color );
	caps.text = text;
	return width;
}

/// a section head in a box: ACTIVE capitals 16 in and 16 down
static void addSection( const char *key, Real left, Real top, Real right )
{
	addCaps( capsFit( key, bx( right ) - bx( left + 16.0f ) ), bx( left + 16.0f ), by( top + 16.0f ), bp( 18.0f ), UIStyle::ACTIVE );
}

static void drawSkinsHero( void );

static void drawPanel( GameWindow *window, WinInstanceData *instData )
{
	// glass, frame A, the title, the player line, the tabs, the server line and the foot rule; then the page
	UIStyle::onlineHeader( UnicodeString::TheEmptyString );
	GameFont *label = labelFont();
	for (size_t i = 0; i < s_pieces.size(); ++i)
	{
		const Piece &p = s_pieces[i];
		IRegion2D r;
		r.lo.x = p.x;
		r.lo.y = p.y;
		r.hi.x = p.x + p.width;
		r.hi.y = p.y + p.height;
		switch (p.kind)
		{
			case PIECE_FILL:	TheDisplay->drawFillRect( p.x, p.y, p.width, p.height, p.color ); break;
			case PIECE_LINE:	TheDisplay->drawOpenRect( p.x, p.y, p.width, p.height, 1.0f, p.color ); break;
			case PIECE_IMAGE:	TheDisplay->drawImage( p.image, p.x, p.y, p.x + p.width, p.y + p.height ); break;
			case PIECE_CAPS:	UIStyle::drawTracked( p.text, label, p.x, p.y, p.color, UIStyle::MENU_LABEL_TRACKING ); break;
			case PIECE_BOX:		UIStyle::onlineBox( r ); break;
			case PIECE_ROW:		UIStyle::menuRow( r, (UIStyle::RowState)p.state, p.zebra ); break;
			case PIECE_LIT:		UIStyle::litLine( p.x, p.y, p.width, p.height, p.color ); break;
			case PIECE_CARET:
				for (Int row = 0; row < p.height; ++row)
					TheDisplay->drawFillRect( p.x + row, p.y + row, p.width - 2 * row, 1, p.color );
				break;
			default:					p.words->draw( p.x, p.y, p.color, UIStyle::SHADOW ); break;
		}
	}
	drawSkinsHero();
}

/// a key drawn in the menu language, lit when primary; shown or hidden
static void showKey( GameWindow *key, Bool shown, Bool primary = FALSE )
{
	if (!key)
		return;
	UIStyle::setMenuKey( key, primary );
	key->winHide( !shown );
}

//-------------------------------------------------------------------------------------------------
// -online: EA's ladder was a web page on its dead servers.  Here it is a table on the panel, 20 rows
// a page as the service pages them, the player's own row in the page or pinned under it, and a side
// column with his place and the ranks (README-online-self.md, "Ladder").  A row opens its player's card.
//-------------------------------------------------------------------------------------------------
enum { LADDER_PAGE = 20, LADDER_ROWS = LADDER_PAGE + 1 };		///< the page, and the pinned row last
static GameWindow *buttonPrev = NULL;
static GameWindow *buttonNext = NULL;
static GameWindow *buttonJump = NULL;
static GameWindow *s_rowButton[ LADDER_ROWS ];
static AsciiString s_rowNick[ LADDER_ROWS ];
static Bool s_rowMine[ LADDER_ROWS ];
static Int s_ladderOffset = 0;
static Int s_ladderShown = -1;			///< the OnlineLadderState last laid out, -1 to lay out again
static OnlineLadderRow s_me;				///< the player's own row, from the last page that carried it
static Bool s_hasMe = FALSE;

/// the table's box and the side column's, at 1080
static const Real TABLE_LEFT = 60.0f, TABLE_RIGHT = 1296.0f, SIDE_LEFT = 1320.0f, SIDE_RIGHT = 1860.0f;
static const Real BOX_TOP = 182.0f, BOX_BOTTOM = 946.0f, PLACE_BOTTOM = 516.0f, RANKS_TOP = 532.0f;
/// the columns at 1080 (README-online-self.md): right edges of the figures, the left of the words
static const Real COL_RANK_RIGHT = 112.0f, COL_PLAYER = 128.0f, COL_PLAYER_END = 520.0f, COL_TIER = 540.0f,
	COL_TIER_END = 760.0f, COL_ELO = 970.0f, COL_WINS = 1080.0f, COL_LOSSES = 1190.0f, COL_PCT = 1280.0f;
static const Real HEAD_TOP = 226.0f, HEAD_RULE = 250.0f, ROWS_TOP = ONLINE_LADDER_ROWS_TOP;

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
	info.hasElo = r.hasElo;
	info.elo = r.elo;
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

/// the side column's scale: the Elo once the server sent the player's, else the old two decimals
static Bool ladderOnElo( void )
{
	return s_hasMe && s_me.hasElo;
}

/** One row on the screen band top to top + height: place, nick (white bold for his own), the rank's
	* portrait and word, the ELO at 24 with its inline bar (gold on his own) toward the page's best, the
	* record.  best is the page's highest ELO, which fills the bar. */
static void addLadderRow( const OnlineLadderRow &r, Int top, Int height, Bool mine, Real best )
{
	GameFont *plain = body();
	const UnicodeString none( u"\x2013" );
	addWords( r.rank > 0 ? number( r.rank ) : none, plain, bx( COL_RANK_RIGHT ), top, UIStyle::MUTED, ALIGN_RIGHT, height );
	UnicodeString nick;
	nick.translate( r.nick );
	GameFont *nickFont = mine ? UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE - 2, TRUE ) : plain;
	addWords( UIStyle::fitName( nick, nickFont, bx( COL_PLAYER_END ) - bx( COL_PLAYER ) ), nickFont, bx( COL_PLAYER ), top, UIStyle::INK,
		ALIGN_LEFT, height );
	const Int picture = bp( 24.0f );
	const UIStyle::RatingTier tier = rowTier( r );
	addPicture( UIStyle::tierImage( tier, AsciiString::TheEmptyString ), bx( COL_TIER ), top + ( height - picture ) / 2, picture, picture, FALSE );
	const Int tierLeft = bx( COL_TIER + 34.0f );
	addWords( UIStyle::fitText( tierName( tier ), plain, bx( COL_TIER_END ) - tierLeft ), plain, tierLeft, top, UIStyle::MUTED, ALIGN_LEFT, height );
	const Int eloWidth = addWords( r.hasRating ? UIStyle::ladderText( r.shown(), r.hasElo ) : none, figureFont(), bx( COL_ELO ), top,
		UIStyle::INK, ALIGN_RIGHT, height );
	if (r.hasRating && r.hasElo && best > 1000.0f)
	{
		// the bar from the rank column's end to 14 short of the figure, filled from the figure's side
		const Int left = bx( COL_TIER_END + 10.0f );
		const Int right = bx( COL_ELO ) - eloWidth - bp( 14.0f );
		const Int middle = top + height / 2;
		if (right > left)
		{
			addPiece( PIECE_FILL, left, middle - 1, right - left, max( 2, bp( 3.0f ) ), GameMakeColor( 255, 255, 255, 18 ) );
			const Real share = max( 0.0f, min( 1.0f, ( r.elo - 1000.0f ) / ( best - 1000.0f ) ) );
			const Int filled = REAL_TO_INT( ( right - left ) * share + 0.5f );
			if (filled > 0)
				addPiece( PIECE_FILL, right - filled, middle - 1, filled, max( 2, bp( 3.0f ) ),
					UIStyle::withAlpha( mine ? UIStyle::GOLD : UIStyle::EDGE, 170 ) );
		}
	}
	addWords( number( r.wins ), plain, bx( COL_WINS ), top, UIStyle::INK, ALIGN_RIGHT, height );
	addWords( number( r.losses ), plain, bx( COL_LOSSES ), top, UIStyle::INK, ALIGN_RIGHT, height );
	const Int games = r.wins + r.losses;
	addWords( games > 0 ? UIStyle::percentText( UIStyle::percent( r.wins, games ) ) : none, plain, bx( COL_PCT ), top, UIStyle::MUTED,
		ALIGN_RIGHT, height );
}

/// a tier's range as the Ranks box lists it: "1800 to 1999", or the old scale's "25.00 to 29.99"
static UnicodeString tierRange( UIStyle::RatingTier tier )
{
	const Bool elo = ladderOnElo();
	UnicodeString range;
	if (tier == UIStyle::TIER_UNRANKED)
		range = TheGameText->fetch( "GUI:OnlineLadderTierUnranked" );
	else if (tier == UIStyle::TIER_GENERAL)
		range.format( TheGameText->fetch( "GUI:OnlineLadderTierUp" ), UIStyle::ladderText( UIStyle::tierStart( tier, elo ), elo ).str() );
	else if (tier == UIStyle::TIER_PRIVATE)
		range.format( TheGameText->fetch( "GUI:OnlineLadderTierUnder" ),
			UIStyle::ladderText( UIStyle::tierStart( UIStyle::TIER_SERGEANT, elo ), elo ).str() );
	else
		range.format( TheGameText->fetch( "GUI:OnlineLadderTierRange" ), UIStyle::ladderText( UIStyle::tierStart( tier, elo ), elo ).str(),
			UIStyle::ladderText( UIStyle::tierStart( (UIStyle::RatingTier)( tier + 1 ), elo ) - ( elo ? 1.0f : 0.01f ), elo ).str() );
	return range;
}

/// a row button's place, or hidden
static void placeRow( Int index, Int x, Int y, Int width, Int height, const AsciiString &nick, Bool mine )
{
	GameWindow *row = s_rowButton[ index ];
	if (!row)
		return;
	s_rowNick[ index ] = nick;
	s_rowMine[ index ] = mine;
	row->winSetPosition( x, y );
	row->winSetSize( width, height );
	row->winHide( nick.isEmpty() );
}

/// the table: section head and page, the column heads, the page or the line that stands for it, the zebra,
/// and the player's row pinned under a dotted gap when the page does not hold him
static void layoutLadderTable( OnlineLadderState state, Int offset, const std::vector<OnlineLadderRow> &rows, Bool answerHasMe )
{
	addBox( TABLE_LEFT, BOX_TOP, TABLE_RIGHT, BOX_BOTTOM );
	addSection( "GUI:OnlineLadderButton", TABLE_LEFT, BOX_TOP, TABLE_LEFT + 300.0f );
	const Int shown = state == ONLINE_LADDER_READY ? min( (Int)rows.size(), (Int)LADDER_PAGE ) : 0;
	if (shown > 0)
	{
		UnicodeString page;
		page.format( TheGameText->fetch( "GUI:OnlineLadderPage" ), offset + 1, offset + shown );
		addWords( page, body(), bx( TABLE_RIGHT - 16.0f ), by( BOX_TOP + 13.0f ), UIStyle::MUTED, ALIGN_RIGHT, bp( 24.0f ) );
	}

	// the heads: words over their figures, ELO green with its caret, being the order the server pages by
	const Int headTop = by( HEAD_TOP );
	const Int headBand = bp( 18.0f );
	addCaps( capsFit( "GUI:OnlineLadderRank", bp( 52.0f ) ), bx( COL_RANK_RIGHT ), headTop, headBand, UIStyle::MUTED, ALIGN_RIGHT );
	addCaps( capsFit( "GUI:OnlineLadderPlayer", bp( 392.0f ) ), bx( COL_PLAYER ), headTop, headBand, UIStyle::MUTED );
	addCaps( capsFit( "GUI:OnlineLadderTier", bp( 220.0f ) ), bx( COL_TIER ), headTop, headBand, UIStyle::MUTED );
	const Int eloHead = addCaps( capsFit( "GUI:OnlineLadderRating", bp( 110.0f ) ), bx( COL_ELO ), headTop, headBand, UIStyle::ACTIVE, ALIGN_RIGHT );
	const Int caret = bp( 8.0f );
	addPiece( PIECE_CARET, bx( COL_ELO ) - eloHead - bp( 10.0f ) - caret, headTop + ( headBand - caret / 2 ) / 2, caret, caret / 2, UIStyle::ACTIVE );
	addCaps( capsFit( "GUI:OnlineLadderWins", bp( 92.0f ) ), bx( COL_WINS ), headTop, headBand, UIStyle::MUTED, ALIGN_RIGHT );
	addCaps( capsFit( "GUI:OnlineLadderLosses", bp( 102.0f ) ), bx( COL_LOSSES ), headTop, headBand, UIStyle::MUTED, ALIGN_RIGHT );
	addCaps( capsFit( "GUI:OnlineLadderWinPct", bp( 82.0f ) ), bx( COL_PCT ), headTop, headBand, UIStyle::MUTED, ALIGN_RIGHT );
	addPiece( PIECE_LIT, bx( TABLE_LEFT + 8.0f ), by( HEAD_RULE ), bx( TABLE_RIGHT - 8.0f ) - bx( TABLE_LEFT + 8.0f ), 1, UIStyle::FRAME );

	// the rows: 20 of 32, his row pinned under them when the page lacks him; zebra on every second, no hairlines
	const AsciiString self = localNick();
	Bool meInPage = FALSE;
	for (Int i = 0; i < shown; ++i)
		meInPage = meInPage || ( self.isNotEmpty() && rows[ i ].nick.compareNoCase( self ) == 0 );
	const Bool pinned = shown > 0 && answerHasMe && !meInPage && s_me.rank > 0;
	const Real rowHeight = ONLINE_LADDER_ROW_HEIGHT;
	Real best = 1001.0f;
	for (Int i = 0; i < shown; ++i)
		if (rows[ i ].hasElo)
			best = max( best, rows[ i ].elo );
	if (pinned && s_me.hasElo)
		best = max( best, s_me.elo );
	const Int rowLeft = bx( TABLE_LEFT + 8.0f );
	const Int rowWidth = bx( TABLE_RIGHT - 8.0f ) - rowLeft;
	for (Int i = 0; i < LADDER_PAGE; ++i)
	{
		const Int top = by( ROWS_TOP + i * rowHeight );
		const Int height = by( ROWS_TOP + ( i + 1 ) * rowHeight ) - top;
		const Bool mine = i < shown && self.isNotEmpty() && rows[ i ].nick.compareNoCase( self ) == 0;
		addRow( rowLeft, top, rowWidth, height, mine ? UIStyle::ROW_LOCAL : UIStyle::ROW_PLAIN, i % 2 == 1 );
		if (i < shown)
			addLadderRow( rows[ i ], top, height, mine, best );
		placeRow( i, rowLeft, top, rowWidth, height, i < shown ? rows[ i ].nick : AsciiString::TheEmptyString, mine );
	}
	const char *line = NULL;
	if (state == ONLINE_LADDER_WAITING)
		line = "GUI:OnlineLadderLoading";
	else if (state == ONLINE_LADDER_FAILED)
		line = "GUI:OnlineLadderUnavailable";
	else if (shown == 0)
		line = "GUI:OnlineLadderEmpty";
	if (line)
		addWords( UIStyle::fitText( TheGameText->fetch( line ), body(), bx( TABLE_RIGHT - 26.0f ) - bx( TABLE_LEFT + 26.0f ) ), body(),
			bx( TABLE_LEFT + 26.0f ), by( ROWS_TOP ), UIStyle::MUTED, ALIGN_LEFT, bp( rowHeight ) );

	if (pinned)
	{
		// the ranks skipped between the page and his row, a dotted rule in the 8 gap; then his row
		const Real gap = ROWS_TOP + LADDER_PAGE * rowHeight;
		for (Real x = TABLE_LEFT + 28.0f; x < TABLE_RIGHT - 28.0f; x += 12.0f)
			addPiece( PIECE_FILL, bx( x ), by( gap + ONLINE_LADDER_PINNED_GAP / 2.0f ), bp( 4.0f ), 1, UIStyle::withAlpha( UIStyle::FRAME, 200 ) );
		const Int top = by( gap + ONLINE_LADDER_PINNED_GAP );
		const Int height = by( gap + ONLINE_LADDER_PINNED_GAP + ONLINE_LADDER_PINNED_HEIGHT ) - top;
		addRow( rowLeft, top, rowWidth, height, UIStyle::ROW_LOCAL, FALSE );
		addLadderRow( s_me, top, height, TRUE, best );
		placeRow( LADDER_PAGE, rowLeft, top, rowWidth, height, s_me.nick, TRUE );
	}
	else
		placeRow( LADDER_PAGE, 0, 0, 1, 1, AsciiString::TheEmptyString, FALSE );
}

/// the side column: his place, rating and tier, the next rank, his record, Jump to my page; then the ranks
static void layoutLadderSide( OnlineLadderState state )
{
	const UnicodeString none( u"\x2013" );
	const UIStyle::RatingTier tier = myTier();
	const Int games = s_hasMe ? s_me.wins + s_me.losses : 0;
	const Bool ranked = s_hasMe && s_me.rank > 0;
	const Bool elo = ladderOnElo();
	const Real left = SIDE_LEFT + 24.0f, right = SIDE_RIGHT - 24.0f;

	addBox( SIDE_LEFT, BOX_TOP, SIDE_RIGHT, PLACE_BOTTOM );
	addSection( "GUI:OnlineLadderYourPlace", SIDE_LEFT + 8.0f, BOX_TOP, SIDE_RIGHT - 24.0f );
	const Int portrait = bp( 104.0f );
	addPicture( UIStyle::tierImage( tier, AsciiString::TheEmptyString ), bx( left ), by( 230.0f ), portrait, portrait );

	// the place at 40, an unranked player's games toward his five in its stead; the ELO at 24, its label, the tier
	const Real figureX = left + 104.0f + 22.0f;
	const Int figureRoom = bx( right ) - bx( figureX );
	if (ranked)
	{
		UnicodeString place;
		place.format( TheGameText->fetch( "GUI:OnlinePlaceValue" ), s_me.rank );
		GameFont *big = UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE, TRUE );
		addWords( UIStyle::fitText( place, big, figureRoom ), big, bx( figureX ), by( 226.0f ), UIStyle::INK, ALIGN_LEFT, bp( 52.0f ) );
	}
	else
	{
		UnicodeString count;
		count.format( TheGameText->fetch( "GUI:OnlineLadderGamesOf" ), min( games, (Int)UIStyle::RANKED_GAMES ), (Int)UIStyle::RANKED_GAMES );
		GameFont *big = UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE + 4 );
		addWords( UIStyle::fitText( count, big, figureRoom ), big, bx( figureX ), by( 234.0f ), UIStyle::INK, ALIGN_LEFT, bp( 38.0f ) );
	}
	const Int eloTop = by( 284.0f ), eloBand = bp( 32.0f );
	Int x = bx( figureX );
	x += addWords( ranked && s_me.hasRating ? UIStyle::ladderText( s_me.shown(), elo ) : none, figureFont(), x, eloTop, UIStyle::INK,
		ALIGN_LEFT, eloBand ) + bp( 8.0f );
	x += addCaps( UIStyle::capitals( TheGameText->fetch( "GUI:OnlineLadderShort" ) ), x, eloTop, eloBand, UIStyle::MUTED ) + bp( 14.0f );
	addWords( UIStyle::fitText( tierName( tier ), body(), bx( right ) - x ), body(), x, eloTop, UIStyle::MUTED, ALIGN_LEFT, eloBand );

	// the next rank and how far, or the games still owed before the first; its track
	const Int nextTop = by( 350.0f ), nextBand = bp( 26.0f );
	Real progress = 1.0f;
	if (!ranked || tier == UIStyle::TIER_UNRANKED)
	{
		addCaps( capsFit( "GUI:OnlineLadderPlacement", bp( 200.0f ) ), bx( left ), nextTop, nextBand, UIStyle::MUTED );
		UnicodeString owed;
		owed.format( TheGameText->fetch( "GUI:OnlineLadderGamesToGo" ), max( 0, (Int)UIStyle::RANKED_GAMES - games ) );
		addWords( owed, body(), bx( right ), nextTop, UIStyle::INK, ALIGN_RIGHT, nextBand );
		progress = (Real)min( games, (Int)UIStyle::RANKED_GAMES ) / UIStyle::RANKED_GAMES;
	}
	else if (tier < UIStyle::TIER_GENERAL)
	{
		const UIStyle::RatingTier up = (UIStyle::RatingTier)( tier + 1 );
		const Real from = UIStyle::tierStart( tier, elo );
		const Real to = UIStyle::tierStart( up, elo );
		addCaps( capsFit( "GUI:OnlineLadderNextRank", bp( 140.0f ) ), bx( left ), nextTop, nextBand, UIStyle::MUTED );
		UnicodeString togo;
		togo.format( TheGameText->fetch( "GUI:OnlineLadderToGo" ), UIStyle::ladderText( max( 0.0f, to - s_me.shown() ), elo ).str() );
		const Int togoWidth = addWords( togo, body(), bx( right ), nextTop, UIStyle::MUTED, ALIGN_RIGHT, nextBand );
		UnicodeString at;
		at.format( TheGameText->fetch( "GUI:OnlineLadderNextAt" ), tierName( up ).str(), UIStyle::ladderText( to, elo ).str() );
		const Int atLeft = bx( left + 150.0f );
		addWords( UIStyle::fitText( at, body(), bx( right ) - togoWidth - bp( 12.0f ) - atLeft ), body(), atLeft, nextTop, UIStyle::INK,
			ALIGN_LEFT, nextBand );
		// private's Elo track starts where sergeant does (the site's meter): empty, never a divide by nothing
		progress = to > from ? ( s_me.shown() - from ) / ( to - from ) : 0.0f;
	}
	addTrack( bx( left ), by( 384.0f ), bx( right ) - bx( left ), bp( 6.0f ), progress );

	// the record: wins, losses and win rate, a figure and its label in each third
	const Int cell = ( bx( right ) - bx( left ) ) / 3;
	const Int recordTop = by( 404.0f ), recordBand = bp( 32.0f );
	const Int wins = s_hasMe ? s_me.wins : 0, losses = s_hasMe ? s_me.losses : 0;
	const UnicodeString figures[ 3 ] = { number( wins ), number( losses ),
		wins + losses > 0 ? UIStyle::percentText( UIStyle::percent( wins, wins + losses ) ) : none };
	static const char *const RECORD[ 3 ] = { "GUI:OnlineLadderWins", "GUI:OnlineLadderLosses", "GUI:OnlineWinRateLabel" };
	for (Int c = 0; c < 3; ++c)
	{
		const Int cellLeft = bx( left ) + c * cell;
		const Int figure = addWords( figures[ c ], figureFont(), cellLeft, recordTop, UIStyle::INK, ALIGN_LEFT, recordBand );
		const Int labelLeft = cellLeft + figure + bp( 10.0f );
		addCaps( capsFit( RECORD[ c ], cellLeft + cell - bp( 6.0f ) - labelLeft ), labelLeft, recordTop, recordBand, UIStyle::MUTED );
	}

	// the ranks with their portraits and ranges, his own on the gold edge
	addBox( SIDE_LEFT, RANKS_TOP, SIDE_RIGHT, BOX_BOTTOM );
	addSection( "GUI:OnlineLadderTiers", SIDE_LEFT + 8.0f, RANKS_TOP, SIDE_RIGHT - 220.0f );
	addCaps( UIStyle::capitals( TheGameText->fetch( "GUI:OnlineLadderShort" ) ), bx( right ), by( RANKS_TOP + 16.0f ), bp( 18.0f ), UIStyle::MUTED,
		ALIGN_RIGHT );
	const Real ranksTop = RANKS_TOP + 44.0f;
	const Real band = 45.0f;
	Real rowTop = ranksTop;
	for (Int t = UIStyle::TIER_GENERAL; t >= UIStyle::TIER_UNRANKED; --t, rowTop += band)
	{
		const UIStyle::RatingTier each = (UIStyle::RatingTier)t;
		const Int top = by( rowTop );
		const Int height = by( rowTop + band ) - top;
		if (s_hasMe && each == tier)
			addRow( bx( SIDE_LEFT + 10.0f ), top, bx( SIDE_RIGHT - 10.0f ) - bx( SIDE_LEFT + 10.0f ), height, UIStyle::ROW_LOCAL, FALSE );
		const Int picture = bp( band - 12.0f );
		addPicture( UIStyle::tierImage( each, AsciiString::TheEmptyString ), bx( left ), top + bp( 6.0f ), picture, picture, FALSE );
		const Int middle = bx( right - 200.0f );
		addWords( tierRange( each ), body(), bx( right ), top, UIStyle::MUTED, ALIGN_RIGHT, height );
		const Int nameLeft = bx( left ) + picture + bp( 16.0f );
		addWords( UIStyle::fitText( tierName( each ), body(), middle - bp( 8.0f ) - nameLeft ), body(), nameLeft, top, UIStyle::INK,
			ALIGN_LEFT, height );
	}
}

static void layoutLadder( OnlineLadderState state, Int offset, const std::vector<OnlineLadderRow> &rows, Bool answerHasMe )
{
	clearPieces();
	layoutLadderTable( state, offset, rows, answerHasMe );
	layoutLadderSide( state );
	const Bool nextLive = state == ONLINE_LADDER_READY && (Int)rows.size() >= LADDER_PAGE;
	if (buttonPrev)
		buttonPrev->winEnable( state != ONLINE_LADDER_WAITING && s_ladderOffset > 0 );
	if (buttonNext)
	{
		buttonNext->winEnable( nextLive );
		UIStyle::setMenuKey( buttonNext, nextLive );		// the lit key while there is a next page
	}
	if (buttonJump)
		buttonJump->winEnable( state != ONLINE_LADDER_WAITING && s_hasMe && s_me.rank > 0 && myPage() != s_ladderOffset );
}

/// a ladder row's button: the white lift and cyan edge under the mouse (his own keeps its gold)
static void drawLadderRow( GameWindow *window, WinInstanceData *instData )
{
	Int index = 0;
	while (index < LADDER_ROWS && s_rowButton[ index ] != window)
		++index;
	if (index == LADDER_ROWS || s_rowMine[ index ] || !BitTest( instData->getState(), WIN_STATE_HILITED ))
		return;
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	IRegion2D r;
	r.lo.x = x;
	r.lo.y = y;
	r.hi.x = x + width;
	r.hi.y = y + height;
	UIStyle::menuRow( r, UIStyle::ROW_HOVER );
}

static void createLadderGadgets( void )
{
	if (!parentWindow || !windowLadder)
		return;
	buttonPrev = findChild( "ButtonPrev" );
	buttonNext = findChild( "ButtonNext" );
	buttonJump = findChild( "ButtonJump" );
	showKey( buttonPrev, TRUE );
	showKey( buttonNext, TRUE, TRUE );
	showKey( buttonJump, TRUE );
	showKey( findChild( "ButtonSave" ), FALSE );
	showKey( findChild( "ButtonAction" ), FALSE );
	for (Int i = 0; i < LADDER_ROWS; ++i)
	{
		char name[ 64 ];
		if (i < LADDER_PAGE)
			sprintf( name, "WOLLadderScreen.wnd:LadderRow%d", i );
		else
			strcpy( name, "WOLLadderScreen.wnd:LadderRowMine" );
		s_rowButton[ i ] = makeButton( parentWindow, NULL, 0, 0, 1, 1, "GUI:OnlineLadderPlayer", name );
		if (s_rowButton[ i ])
		{
			GadgetButtonSetText( s_rowButton[ i ], UnicodeString::TheEmptyString );
			s_rowButton[ i ]->winSetDrawFunc( drawLadderRow );
			s_rowButton[ i ]->winHide( TRUE );
		}
		s_rowNick[ i ].clear();
		s_rowMine[ i ] = FALSE;
	}

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
		s_me.hasElo = info.hasElo;
		s_me.elo = info.elo;
		s_me.rating = info.rating;
		s_me.tier = info.tier;
		s_me.wins = max( 0, info.wins );
		s_me.losses = max( 0, info.losses );
		s_hasMe = TRUE;
	}
}

static void destroyLadderGadgets( void )
{
	for (Int i = 0; i < LADDER_ROWS; ++i)
	{
		if (s_rowButton[ i ])
			TheWindowManager->winDestroy( s_rowButton[ i ] );
		s_rowButton[ i ] = NULL;
		s_rowNick[ i ].clear();
	}
	buttonPrev = buttonNext = buttonJump = NULL;		// the master's
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
	DEBUG_LOG(( "LADDER PAGE state=%d offset=%d rows=%d me=%d rank=%d\n", (Int)state, offset, (Int)rows.size(), (Int)hasMe,
		s_hasMe ? s_me.rank : 0 ));
}

static void requestLadderPage( Int offset )
{
	s_ladderOffset = max( 0, offset );
	s_ladderShown = -1;
	OnlineRequestLadder( s_ladderOffset );
	drawLadder();
}

/** A row clicked: its player's card over the ladder.  The ladder carries no account ids, so another
	* player's card shows his ratings and record (stats{nick}) and no match history; his own has both. */
static Bool openRowCard( GameWindow *control )
{
	for (Int i = 0; i < LADDER_ROWS; ++i)
	{
		if (control != s_rowButton[ i ] || s_rowNick[ i ].isEmpty())
			continue;
		Int id = -1;
		if (s_rowMine[ i ] && TheGameSpyInfo)
			id = TheGameSpyInfo->getLocalProfileID();
		else if (TheGameSpyInfo && TheGameSpyInfo->getPlayerInfoMap())
		{
			PlayerInfoMap::iterator found = TheGameSpyInfo->getPlayerInfoMap()->find( s_rowNick[ i ] );
			if (found != TheGameSpyInfo->getPlayerInfoMap()->end() && found->second.m_profileID > 0)
				id = found->second.m_profileID;
		}
		DEBUG_LOG(( "LADDER CARD nick=%s id=%d\n", s_rowNick[ i ].str(), id ));
		GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );
		SetLookAtPlayer( id, s_rowNick[ i ] );
		GameSpyOpenOverlay( GSOVERLAY_PLAYERINFO );
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// The skins page, the Skins tab (README-online-self.md, "Skins"), three panels.  Left, the units
// SkinsReforged.ini dresses by side, each with its cameo, how many of its looks the account owns and
// that count as a bar.  Middle, the picked unit's looks as cards sized to their count, each its model
// drawn live at one fixed angle and its name, a tick on the worn and the picked one and a padlock on a
// locked one, under the owned bar and an All, Owned, Locked filter.  Right, the look in focus turning
// on its plate, its name and one key: Wear, Worn (Picked until saved), or for a look the account lacks
// what earns it, which opens zerohour.gg.  Save sends the whole loadout; once the site took it the next
// room and Quick Match seat it (OnlineSaveSkins).
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
	Int have;								///< owned looks, Default among them
};
enum { FILTER_ALL, FILTER_OWNED, FILTER_LOCKED, FILTER_COUNT };
static Bool s_skinsPage = FALSE;
static std::vector<SkinRow> s_skinRows;
static size_t s_skinUnit = 0;					///< the row whose looks show
static Int s_skinFocus = 0;						///< the look on the platform, a skin id of that row, 0 Default
static Int s_skinFilter = FILTER_ALL;
static GameWindow *buttonSave = NULL;
static GameWindow *buttonAction = NULL;	///< Wear, Worn, Picked or what earns a locked look
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

/// the three panels at 1080: the list 360 wide, the platform 520, the cards between, 24 apart
static const Real LIST_LEFT = 60.0f, LIST_RIGHT = 420.0f, MID_LEFT = 444.0f, MID_RIGHT = 1316.0f,
	PLATFORM_LEFT = 1340.0f, PLATFORM_RIGHT = 1860.0f;
static const Real CARDS_TOP = ONLINE_SKINS_CARDS_TOP, CARDS_BOTTOM = ONLINE_SKINS_CARDS_BOTTOM;

/// a five-pointed star, its points on a circle of size across centred in the size-square box at x, y:
/// a rare look's mark, white on the cyan edge (gold is the local player's alone)
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

static IRegion2D region( Int x, Int y, Int width, Int height )
{
	IRegion2D r;
	r.lo.x = x;
	r.lo.y = y;
	r.hi.x = x + width;
	r.hi.y = y + height;
	return r;
}

void WOLSkinsScreenOpen( void )
{
	s_skinsPage = TRUE;
	TheShell->push( AsciiString( "Menus/WOLLadderScreen.wnd" ) );
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

/// the side heads in the list, one over each side's first unit
static Int sideHeads( void )
{
	Int heads = 0, side = -1;
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		if (rowSide( r ) != side && rowSide( r ) >= 0)
			++heads;
		side = rowSide( r );
	}
	return heads;
}

/** Row r's place in the list at 1080 (OnlineSelfLayout.h): 8 apart inside a side, 40 between sides,
	* 70 high or less where more units must fit; a side's head 30 over its first unit, its top in headTop
	* when r is a side's first, else -1. */
static void unitPlace( size_t r, Real &x, Real &y, Real &width, Real &height, Real &headTop )
{
	const Int heads = sideHeads();
	const Int units = max( 1, (Int)s_skinRows.size() );
	const Real gaps = ONLINE_SKINS_UNIT_GAP * max( 0, units - max( 1, heads ) ) + ONLINE_SKINS_SIDE_GAP * max( 0, heads - 1 );
	height = min( ONLINE_SKINS_UNIT_HEIGHT, (Real)(Int)( ( ONLINE_SKINS_UNITS_BOTTOM - ONLINE_SKINS_UNITS_TOP - gaps ) / units ) );
	Real top = ONLINE_SKINS_UNITS_TOP;
	Int side = -1;
	headTop = -1.0f;
	for (size_t i = 0; i <= r && i < s_skinRows.size(); ++i)
	{
		const Int its = rowSide( i );
		const Bool newSide = its != side && its >= 0;
		if (i > 0)
			top += height + ( newSide ? ONLINE_SKINS_SIDE_GAP : ONLINE_SKINS_UNIT_GAP );
		if (i == r && newSide)
			headTop = top - 30.0f;
		side = its;
	}
	x = LIST_LEFT + 8.0f;
	y = top;
	width = LIST_RIGHT - 8.0f - x;
}

/// the look in focus on the shown unit's row, its first when the focus is not one of its own
static SkinChoice *focusChoice( SkinRow &row )
{
	for (size_t c = 0; c < row.choices.size(); ++c)
		if (row.choices[ c ].id == s_skinFocus)
			return &row.choices[ c ];
	return row.choices.empty() ? NULL : &row.choices[ 0 ];
}

/// a tick in a size-square box at x, y: a dark well under a box and stroke in color
static void drawTick( Int x, Int y, Int size, Color color )
{
	TheDisplay->drawFillRect( x, y, size, size, GameMakeColor( 0, 0, 0, 170 ) );
	TheDisplay->drawOpenRect( x, y, size, size, 1.0f, color );
	const Real stroke = (Real)max( 2, size / 9 );
	TheDisplay->drawLine( x + size * 22 / 100, y + size * 52 / 100, x + size * 42 / 100, y + size * 74 / 100, stroke, color );
	TheDisplay->drawLine( x + size * 42 / 100, y + size * 74 / 100, x + size * 80 / 100, y + size * 28 / 100, stroke, color );
}

/// a padlock in a size-square box at x, y: a shackle over a solid body
static void drawPadlock( Int x, Int y, Int size, Color color = UIStyle::MUTED )
{
	const Real stroke = (Real)max( 2, size / 10 );
	TheDisplay->drawOpenRect( x + size * 28 / 100, y + size * 8 / 100, size * 44 / 100, size * 52 / 100, stroke, color );
	TheDisplay->drawFillRect( x + size * 15 / 100, y + size * 45 / 100, size * 70 / 100, size * 50 / 100, color );
}

/** The look in focus turning on its plate, once a 12 s by the wall clock, from the panel's draw so
	* the keys go over it; dimmed with a padlock for a look the account lacks, a rare one on the cyan
	* edge with its white star. */
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
		UIStyle::litEdge( s_hero, UIStyle::EDGE_LIT );
		drawRareStar( s_hero.lo.x + bp( 14.0f ), s_hero.lo.y + bp( 14.0f ), bp( 26.0f ), UIStyle::INK );
	}
	if (!s_heroOwned)
		drawPadlock( s_hero.hi.x - bp( 44.0f ), s_hero.lo.y + bp( 14.0f ), bp( 28.0f ) );
}

static Bool hovered( GameWindow *window, WinInstanceData *instData )
{
	return BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) && BitTest( instData->getState(), WIN_STATE_HILITED );
}

/// a button's own label in the font asked for
static DisplayString *buttonWords( WinInstanceData *instData, GameFont *font )
{
	DisplayString *words = instData->getTextDisplayString();
	if (words->getFont() != font)
		words->setFont( font );
	return words;
}

/** A unit's row in the list, drawn over its push button: the open one on the EDGE wash with its lit
	* left edge, a hovered one the white lift; its cameo framed, its name, its owned count and that count
	* as a 3 px bar under the name. */
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
	const IRegion2D box = region( x, y, width, height );
	if (r == s_skinUnit)
	{
		TheDisplay->drawFillRect( x, y, width, height, UIStyle::withAlpha( UIStyle::EDGE, 70 ) );
		UIStyle::litLine( x, y, max( 2, bp( 3.0f ) ), height, UIStyle::EDGE );
	}
	else if (hovered( window, instData ))
		UIStyle::menuRow( box, UIStyle::ROW_HOVER );

	// the cameo at 70 by 56, centred on the row
	const Int pictureWidth = bp( 70.0f ), pictureHeight = bp( 56.0f );
	const Int pictureX = x + bp( 12.0f );
	const Int pictureY = y + ( height - pictureHeight ) / 2;
	const Image *cameo = row->known < SKIN_UNKNOWN ? TheMappedImageCollection->findImageByName( SKIN_UNITS[ row->known ].cameo ) : NULL;
	if (cameo)
	{
		TheDisplay->drawImage( cameo, pictureX, pictureY, pictureX + pictureWidth, pictureY + pictureHeight );
		TheDisplay->drawOpenRect( pictureX - 1, pictureY - 1, pictureWidth + 2, pictureHeight + 2, 1.0f, UIStyle::withAlpha( UIStyle::FRAME, 220 ) );
	}

	const Int textLeft = pictureX + pictureWidth + bp( 14.0f );
	const Int right = x + width - bp( 10.0f );
	const Int textTop = y + bp( 8.0f ), textBand = bp( 28.0f );
	DisplayString *name = buttonWords( instData, body() );
	Int wordsWidth, wordsHeight;
	name->getSize( &wordsWidth, &wordsHeight );
	name->draw( textLeft, textTop + ( textBand - wordsHeight ) / 2, UIStyle::INK, UIStyle::SHADOW );
	if (row->count)
	{
		row->count->getSize( &wordsWidth, &wordsHeight );
		row->count->draw( right - wordsWidth, textTop + ( textBand - wordsHeight ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
	}
	const Int looks = max( 1, (Int)row->choices.size() );
	const Int track = right - textLeft;
	const Int trackTop = y + bp( 44.0f );
	TheDisplay->drawFillRect( textLeft, trackTop, track, bp( 3.0f ), GameMakeColor( 255, 255, 255, 24 ) );
	const Int filled = track * row->have / looks;
	if (filled > 0)
		UIStyle::litLine( textLeft, trackTop, filled, bp( 3.0f ), UIStyle::EDGE );
}

/** A look's card, drawn over its push button: the model on a near-black ground at a fixed turn,
	* dimmed where the account lacks the look; the three-line edge on the look in focus, the cyan edge and
	* white star on a rare one, the white lift under the mouse; a tick or a padlock top right; its name
	* at the foot, MUTED when locked. */
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
	const IRegion2D box = region( x, y, width, height );
	const Int picture = height - bp( 40.0f );

	TheDisplay->drawFillRect( x, y, width, height, GameMakeColor( 0, 0, 0, 150 ) );
	if (picture > 2)
		TheDisplay->drawFillRect( x + 2, y + 2, width - 4, picture - 2, SKIN_CARD_GROUND );
	if (picture > bp( 24.0f ) && row->known < SKIN_UNKNOWN)
		TheDisplay->drawModelPreview( AsciiString( SKIN_UNITS[ row->known ].templateName ), choice->id, SKIN_HOUSE,
			x + 2, y + 2, width - 4, picture - 2, SKIN_CARD_TURN, !choice->owned, TRUE, SKIN_UNITS[ row->known ].state );
	const Bool rare = TankSkin_isRare( choice->id );
	if (choice->id == s_skinFocus)
		UIStyle::edge( x, y, width, height );
	else if (rare)
		UIStyle::litEdge( box, UIStyle::EDGE_LIT );
	else if (hovered( window, instData ))
	{
		TheDisplay->drawFillRect( x, y, width, height, GameMakeColor( 255, 255, 255, 14 ) );
		UIStyle::litEdge( box, UIStyle::EDGE );
	}
	else
		TheDisplay->drawOpenRect( x, y, width, height, 1.0f, UIStyle::withAlpha( UIStyle::FRAME, 230 ) );

	const Int mark = bp( 20.0f );
	const Int markX = x + width - bp( 30.0f );
	const Int markY = y + bp( 10.0f );
	if (rare)
		drawRareStar( x + bp( 10.0f ), markY, mark, UIStyle::INK );
	if (choice->id == row->pick)
		drawTick( markX, markY, mark, row->pick == row->worn ? UIStyle::ACTIVE : UIStyle::EDGE_LIT );
	else if (choice->id == row->worn)
		drawTick( markX, markY, mark, UIStyle::MUTED );
	else if (!choice->owned)
		drawPadlock( markX, markY, mark );

	DisplayString *name = buttonWords( instData, body() );
	Int nameWidth, nameHeight;
	name->getSize( &nameWidth, &nameHeight );
	name->draw( x + bp( 12.0f ), y + picture + ( height - bp( 4.0f ) - picture - nameHeight ) / 2, choice->owned ? UIStyle::INK : UIStyle::MUTED,
		UIStyle::SHADOW );
}

/// a filter, a tab: the one in force open and green
static void drawSkinFilter( GameWindow *window, WinInstanceData *instData )
{
	Int filter = 0;
	while (filter < FILTER_COUNT && buttonFilter[ filter ] != window)
		++filter;
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	UIStyle::menuTab( region( x, y, width, height ), instData->getTextDisplayString(), filter == s_skinFilter,
		filter != s_skinFilter && hovered( window, instData ) );
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
			row.have = 1;
			SkinChoice none = { NULL, 0, TRUE };
			row.choices.push_back( none );
			s_skinRows.push_back( row );
		}
		s_skinRows[r].choices.push_back( choice );
	}
	std::stable_sort( s_skinRows.begin(), s_skinRows.end(), skinRowBefore );

	// the list's rows and the cards, push buttons drawn as rows and cards; the cards are placed and
	// shown by layoutSkins, the shown unit's that pass the filter
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		SkinRow &row = s_skinRows[r];
		Real x, y, width, height, headTop;
		unitPlace( r, x, y, width, height, headTop );
		char name[ 80 ];
		sprintf( name, "WOLLadderScreen.wnd:SkinUnit%s", row.unit.str() );
		row.tile = makeButton( parentWindow, NULL, bx( x ), by( y ), bx( x + width ) - bx( x ), by( y + height ) - by( y ),
			"GUI:OnlineSkinsDefault", name );
		if (row.tile)
		{
			// the name between the cameo and the widest count the row can show
			const Int looks = (Int)row.choices.size();
			UnicodeString widest = number( looks );
			widest.concat( u" / " );
			widest.concat( number( looks ) );
			const Int room = bx( x + width - 10.0f ) - bx( x + 12.0f + 70.0f + 14.0f ) - measure( widest, body() ) - bp( 12.0f );
			GadgetButtonSetText( row.tile, UIStyle::fitName( row.name, body(), room ) );		// a name: an ellipsis, never a cut word
			row.tile->winSetDrawFunc( drawSkinUnit );
		}

		for (size_t c = 0; c < row.choices.size(); ++c)
		{
			AsciiString unit, unitLabel, label = "GUI:OnlineSkinsDefault";
			if (row.choices[c].id)
				TankSkin_describe( row.choices[c].id, unit, unitLabel, label );
			sprintf( name, "WOLLadderScreen.wnd:SkinLook%s%d", row.unit.str(), row.choices[c].id );
			GameWindow *button = makeButton( parentWindow, NULL, bx( MID_LEFT ), by( CARDS_TOP ), bp( 200.0f ), bp( 100.0f ),
				label.isEmpty() ? "GUI:OnlineSkinsDefault" : label.str(), name );
			if (button)
			{
				button->winSetDrawFunc( drawSkinChoice );
				button->winHide( TRUE );
			}
			row.choices[c].button = button;
		}
	}

	// the filter at the title line's right end: All, Owned, Locked, tabs sized to their words
	static const char *const FILTER_KEY[ FILTER_COUNT ] = { "GUI:OnlineSkinsAll", "GUI:OnlineSkinsOwned", "GUI:OnlineSkinsFilterLocked" };
	static const char *const FILTER_NAME[ FILTER_COUNT ] = { "WOLLadderScreen.wnd:SkinFilterAll", "WOLLadderScreen.wnd:SkinFilterOwned",
		"WOLLadderScreen.wnd:SkinFilterLocked" };
	GameFont *tabFont = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE );
	Int right = bx( ONLINE_SKINS_CARDS_RIGHT );
	for (Int i = FILTER_COUNT - 1; i >= 0; --i)
	{
		const UnicodeString label = UIStyle::capitals( TheGameText->fetch( FILTER_KEY[i] ) );
		const Int width = measure( label, tabFont ) + bp( 32.0f );
		buttonFilter[i] = makeButton( parentWindow, NULL, right - width, by( BOX_TOP + 12.0f ), width, by( BOX_TOP + 44.0f ) - by( BOX_TOP + 12.0f ),
			FILTER_KEY[i], FILTER_NAME[i] );
		if (buttonFilter[i])
		{
			GadgetButtonSetText( buttonFilter[i], label );
			buttonFilter[i]->winSetDrawFunc( drawSkinFilter );
		}
		right -= width + bp( 8.0f );
	}

	// the master's keys: Save where Next stands, the platform's one key; the ladder's go
	buttonAction = findChild( "ButtonAction" );
	buttonSave = findChild( "ButtonSave" );
	showKey( findChild( "ButtonPrev" ), FALSE );
	showKey( findChild( "ButtonNext" ), FALSE );
	showKey( findChild( "ButtonJump" ), FALSE );
	showKey( buttonSave, TRUE );
	showKey( buttonAction, TRUE );

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
	for (Int i = 0; i < FILTER_COUNT; ++i)
	{
		if (buttonFilter[i])
			TheWindowManager->winDestroy( buttonFilter[i] );
		buttonFilter[i] = NULL;
	}
	buttonSave = buttonAction = NULL;		// the master's
	s_hero.lo.x = s_hero.hi.x = s_hero.lo.y = s_hero.hi.y = 0;
	if (hadRows && TheDisplay)
		TheDisplay->freeModelPreviews();		// nothing of the models stays for a match that follows
}

static Bool passesFilter( const SkinChoice &choice )
{
	return s_skinFilter == FILTER_ALL || choice.owned == ( s_skinFilter == FILTER_OWNED );
}

static void layoutSkins( OnlineSkinsState state, UnsignedInt64 owned, const AsciiString &error )
{
	clearPieces();
	s_hero.lo.x = s_hero.hi.x = s_hero.lo.y = s_hero.hi.y = 0;

	addBox( LIST_LEFT, BOX_TOP, LIST_RIGHT, BOX_BOTTOM );
	addBox( MID_LEFT, BOX_TOP, MID_RIGHT, BOX_BOTTOM );
	addBox( PLATFORM_LEFT, BOX_TOP, PLATFORM_RIGHT, BOX_BOTTOM );

	// every look's state, every unit's count; a side's head over its first unit
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		SkinRow &row = s_skinRows[r];
		row.have = 0;
		for (size_t c = 0; c < row.choices.size(); ++c)
		{
			SkinChoice &choice = row.choices[c];
			choice.owned = choice.id == 0 || (owned & ((SkinLoadout)1 << choice.id)) != 0;
			row.have += choice.owned ? 1 : 0;
		}
		UnicodeString count = number( row.have );
		count.concat( u" / " );
		count.concat( number( (Int)row.choices.size() ) );
		freeWords( row.count );
		row.count = newWords( count, body() );

		Real x, y, width, height, headTop;
		unitPlace( r, x, y, width, height, headTop );
		if (headTop >= 0.0f)
		{
			const UnicodeString side = UIStyle::capitals( TheGameText->fetch( SKIN_SIDE_KEY[ rowSide( r ) ] ) );
			addCaps( side, bx( LIST_LEFT + 16.0f ), by( headTop + 6.0f ), bp( 18.0f ), UIStyle::ACTIVE );
		}
	}

	SkinRow *row = s_skinUnit < s_skinRows.size() ? &s_skinRows[ s_skinUnit ] : NULL;
	SkinChoice *focus = row ? focusChoice( *row ) : NULL;
	if (focus)
		s_skinFocus = focus->id;

	// the middle: the unit's name at 28 and its owned count left of the filter, the owned bar, the cards
	const Real midLeft = ONLINE_SKINS_CARDS_LEFT, midRight = ONLINE_SKINS_CARDS_RIGHT;
	if (row)
	{
		const Int looks = (Int)row->choices.size();
		UnicodeString count = number( row->have );
		count.concat( u" / " );
		count.concat( number( looks ) );
		Int filterLeft = bx( midRight ), filterTop = 0;
		if (buttonFilter[0])
			buttonFilter[0]->winGetScreenPosition( &filterLeft, &filterTop );
		GameFont *title = UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE + 4 );
		const Int room = filterLeft - bp( 16.0f ) - bx( midLeft ) - measure( count, body() ) - bp( 14.0f );
		const Int titleWidth = addWords( UIStyle::fitName( row->name, title, room ), title, bx( midLeft ), by( BOX_TOP + 8.0f ), UIStyle::INK,
			ALIGN_LEFT, bp( 36.0f ) );
		addWords( count, body(), bx( midLeft ) + titleWidth + bp( 14.0f ), by( BOX_TOP + 8.0f ), UIStyle::MUTED, ALIGN_LEFT, bp( 36.0f ) );
		addTrack( bx( midLeft ), by( BOX_TOP + 58.0f ), bx( midRight ) - bx( midLeft ), bp( 6.0f ), (Real)row->have / max( 1, looks ) );
	}

	// the cards: the shown unit's looks the filter lets through, Default first, the rest hidden
	Int shown = 0;
	if (row)
		for (size_t c = 0; c < row->choices.size(); ++c)
			if (passesFilter( row->choices[c] ))
				++shown;
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		Int slot = 0;
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
		{
			SkinChoice &choice = s_skinRows[r].choices[c];
			if (!choice.button)
				continue;
			if (r != s_skinUnit || !passesFilter( choice ))
			{
				choice.button->winHide( TRUE );
				continue;
			}
			Real x, y, width, height;
			OnlineSkinsCardPlace( slot++, shown, midRight - midLeft, CARDS_BOTTOM - CARDS_TOP, x, y, width, height );
			const Int left = bx( midLeft + x ), top = by( CARDS_TOP + y );
			choice.button->winSetPosition( left, top );
			choice.button->winSetSize( bx( midLeft + x + width ) - left, by( CARDS_TOP + y + height ) - top );
			choice.button->winHide( FALSE );
		}
	}

	// the platform: the model in all the room the name and the key leave, the look's name at 40, the key
	const Int pending = unsavedCount();
	const Bool saveLive = state != ONLINE_SKINS_WAITING && owned != 0 && pending > 0;
	if (row && focus)
	{
		AsciiString unit, unitLabel, label;
		if (focus->id)
			TankSkin_describe( focus->id, unit, unitLabel, label );
		const UnicodeString name = TheGameText->fetch( label.isEmpty() ? "GUI:OnlineSkinsDefault" : label.str() );
		GameFont *big = UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE );
		addWords( UIStyle::fitText( name, big, bx( PLATFORM_RIGHT - 24.0f ) - bx( PLATFORM_LEFT + 24.0f ) ), big, bx( PLATFORM_LEFT + 24.0f ),
			by( 784.0f ), focus->owned ? UIStyle::INK : UIStyle::MUTED, ALIGN_LEFT, bp( 52.0f ) );
		s_hero = UIStyle::boardRect( (Int)PLATFORM_LEFT + 8, (Int)BOX_TOP + 8, (Int)PLATFORM_RIGHT - 8, 774 );
		s_heroOwned = focus->owned;

		if (buttonAction)
		{
			// a look the account lacks: what earns it, as the key's label; the key opens the site
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
			Int width, keyHeight;
			buttonAction->winGetSize( &width, &keyHeight );
			GadgetButtonSetText( buttonAction, UIStyle::fitKey( action, UIStyle::menuFont( UIStyle::MENU_TEXT_KEY, TRUE ), width - bp( 24.0f ) ) );
			buttonAction->winEnable( live );
			// the lit key whenever Save has nothing to do
			showKey( buttonAction, TRUE, live && !saveLive );
		}
	}
	else if (buttonAction)
		buttonAction->winHide( TRUE );

	// between Back and Save, ending 24 before Save: the unsaved picks counted, else how the last ask went
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
	if (!line.isEmpty())
	{
		const Int right = bx( (Real)( UIStyle::ONLINE_RIGHT - UIStyle::MENU_KEY_WIDTH - 24 ) );
		const Int left = bx( (Real)( UIStyle::ONLINE_LEFT + UIStyle::MENU_KEY_WIDTH + 24 ) );
		addWords( UIStyle::fitText( line, body(), right - left ), body(), right, by( (Real)UIStyle::MENU_KEYS_TOP ), lineColor, ALIGN_RIGHT,
			by( (Real)UIStyle::MENU_KEYS_BOTTOM ) - by( (Real)UIStyle::MENU_KEYS_TOP ) );
	}
	if (buttonSave)
	{
		buttonSave->winEnable( saveLive );
		UIStyle::setMenuKey( buttonSave, saveLive );
	}
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
	 focus.  A filter: the cards it lets through.  The platform's key: Wear the look in focus, or
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
		showKey( buttonBack, TRUE );
		if (s_skinsPage)
		{
			GameWindow *title = findChild( "StaticTextTitle" );
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
	else
	{
		// EA's ladder was a web page; the keys the online pages use stay hidden
		static const char *const ONLINE_ONLY[] = { "ButtonPrev", "ButtonNext", "ButtonJump", "ButtonSave", "ButtonAction" };
		for (Int i = 0; i < (Int)ARRAY_SIZE( ONLINE_ONLY ); ++i)
			if (GameWindow *key = findChild( ONLINE_ONLY[i] ))
				key->winHide( TRUE );
		if (TheWebBrowser != NULL)
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

	if (OnlineIsEnabled() && GameSpyIsOverlayOpen( GSOVERLAY_PLAYERINFO ))
		GameSpyCloseOverlay( GSOVERLAY_PLAYERINFO );		// a card a row opened goes with the ladder
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
		// the FRIENDS key and an invite's toast, as the pages that read the buddy queue have them
		if (TheShell->isAnimFinished())
			FriendsCornerUpdate();
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
			else if( control == buttonPrev && buttonPrev )
			{
				requestLadderPage( s_ladderOffset - LADDER_PAGE );
			}
			else if( control == buttonNext && buttonNext )
			{
				requestLadderPage( s_ladderOffset + LADDER_PAGE );
			}
			else if( control == buttonJump && buttonJump )
			{
				requestLadderPage( myPage() );
			}
			else if( control == buttonSave && buttonSave )
			{
				saveSkins();
			}
			else if( !openRowCard( control ) )
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

