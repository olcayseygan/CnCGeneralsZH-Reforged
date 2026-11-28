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

// FILE: DirectStrikeLobby.cpp ////////////////////////////////////////////////////////////////////
//
// The Direct Strike room, DirectStrikeLobby.h, in the menu language (the wrapper's
// experiments/proofs/2026-11-24/lobbies/direct-strike/README-ds-lobby.md).  Every rect is at 1920x1080 in
// DirectStrikeLayout.h, put on the screen by DirectStrike_frame; every line, fill and glow is drawn here or by
// UIStyle, with no piece of the Direct Strike sheets.  Gold marks the local player and nothing else.
//
// A seat is a start, not a slot (DirectStrikeLayout.h): a player moves by clicking an open seat, a
// computer's seat or the other team's head, which presses the room's own hidden start spot for that
// seat.  The host and a skirmish take it (DirectStrikeTakeSeat: a computer there swaps starts with the
// player), a joiner asks the host for it ("StartPos=" in LANAPI's and the online room's requests, which
// the online server only relays), the host refuses a closed seat or a human's, resets everyone's ready
// and sends the slot list, and FitDirectStrikeRoom gives the team that goes with the seat.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/DirectStrikeLobby.h"

#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/DirectStrikeLayout.h"
#include "GameClient/DirectStrikeTheme.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/UIStyle.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/Online/OnlineQueues.h"

#include <algorithm>
#include <vector>

using namespace DirectStrikeTheme;
namespace U = UIStyle;

namespace
{
	enum Room { ROOM_SKIRMISH, ROOM_LAN, ROOM_ONLINE, ROOM_COUNT };
	enum List { LIST_NONE, LIST_SEAT, LIST_ARMY, LIST_COLOR, LIST_SIZE, LIST_SPEED };
	static_assert( DSLOBBY_SPEEDS == PLAYER_GAME_SPEED_COUNT, "the speed list has a row for every player speed" );

	/// each room's layout and its chat log; the skirmish room has none
	struct RoomNames
	{
		const char *layout;
		const char *chatLog;
	};
	const RoomNames ROOMS[ ROOM_COUNT ] =
	{
		{ "SkirmishGameOptionsMenu.wnd", NULL },
		{ "LanGameOptionsMenu.wnd", "ListboxChatWindowLanGame" },
		{ "GameSpyGameOptionsMenu.wnd", "ListboxChatWindowGameSpyGameSetup" },
	};
	const char *const CHAT_ENTRY = "TextEntryChat";

	/// the picker's columns: the side first, then its generals; Direct Strike has no Turkiye
	const char *const ARMIES[ DSLOBBY_ARMY_COLUMNS ][ DSLOBBY_ARMY_TILES ] =
	{
		{ "FactionAmerica", "FactionAmericaAirForceGeneral", "FactionAmericaLaserGeneral", "FactionAmericaSuperWeaponGeneral" },
		{ "FactionChina", "FactionChinaTankGeneral", "FactionChinaInfantryGeneral", "FactionChinaNukeGeneral" },
		{ "FactionGLA", "FactionGLAToxinGeneral", "FactionGLAStealthGeneral", "FactionGLADemolitionGeneral" },
	};
	const char *const ARMY_LABELS[ DSLOBBY_ARMY_COLUMNS ][ DSLOBBY_ARMY_TILES ] =
	{
		{ "SIDE:America", "GUI:OnlineGeneralAir", "GUI:OnlineGeneralLaser", "GUI:OnlineGeneralSuper" },
		{ "SIDE:China", "GUI:OnlineGeneralTank", "GUI:OnlineGeneralInf", "GUI:OnlineGeneralNuke" },
		{ "SIDE:GLA", "GUI:OnlineGeneralToxin", "GUI:OnlineGeneralStealth", "GUI:OnlineGeneralDemo" },
	};

	/// an open seat's level keys, the short words (the label above them says a computer is added)
	const char *const LEVEL_LABELS[ DSLOBBY_LEVELS ] = { "GUI:DirectStrikeEasy", "GUI:DirectStrikeMedium", "GUI:DirectStrikeHard", "GUI:DirectStrikeBrutal" };
	enum { SEAT_ROW_FIRST_AI = 2 };

	// the design's ink beside UIStyle's tokens
	const Color DIM = GameMakeColor( 110, 116, 128, 255 );
	const Color FIG = GameMakeColor( 214, 218, 228, 255 );			///< a chat line's words, softer than a name
	const Color AMBER = GameMakeColor( 232, 168, 40, 255 );
	const Color DARK = GameMakeColor( 14, 16, 22, 255 );				///< words on a lit face
	const Color LIGHT_KEY = GameMakeColor( 170, 176, 186, 255 );	///< the keyline round a colour too dark to see
	const Color PING_RED = GameMakeColor( 230, 70, 50, 255 );
	const Color UNPICKED = GameMakeColor( 70, 72, 80, 255 );

	// the type at 1080, README-ds-lobby.md
	enum
	{
		TYPE_TITLE = 40, TYPE_FACTS = 20, TYPE_TEAM = 22, TYPE_HERO = 34, TYPE_NAME_PX = 22, TYPE_ARMY = 20, TYPE_BODY_PX = 18,
		TYPE_SMALL_PX = 16, TYPE_CAPS = 14, TYPE_START = 36, TYPE_SEND = 15, TYPE_TILE_SIDE = 15,
	};
	const Real CONTENT_IN = 14;		///< a dropdown's words from its left
	const Real CARET_CELL = 40;		///< a dropdown's caret cell at its right
	const Int PING_GOOD = 80, PING_FAIR = 150, PING_POOR = 250;
	enum { CHAT_LINES = 24, COLOR_LIMIT = 16 };

	GameWindow *s_parent = NULL;
	const GameInfo *s_game = NULL;
	Room s_room = ROOM_SKIRMISH;
	GameWinDrawFunc s_roomDraw = NULL;
	GameWinInputFunc s_roomInput = NULL;
	List s_list = LIST_NONE;
	Int s_listSlot = -1;
	DirectStrikeFrame s_frame;
	Int s_seatOf[ DSLOBBY_SEATS ];		///< where each of the first six slots shows, DirectStrikeSeats at the draw or the click
	Int s_slotAt[ DSLOBBY_SEATS ];

	/// the chat's strings, each clipped to the chat box, kept apart from the theme's pool
	DisplayString *s_chatNames[ CHAT_LINES ];
	DisplayString *s_chatTexts[ CHAT_LINES ];
	DisplayString *s_inputText = NULL;
	Int s_chatScroll = 0;	///< entries the log is scrolled back from the newest

	/// one string a letter and font for the spaced capitals, kept until the detach: the theme's pool would spend
	/// two strings a letter every frame
	struct Letter
	{
		GameFont *font;
		WideChar c;
		DisplayString *s;
	};
	std::vector<Letter> s_letters;

	/// what hideRoom changed in the room, so the detach gives the room back as it was found.  A window is
	/// kept by its id under its room, never by pointer: a room's gadgets can go and come back between the
	/// detach and the restore, and a freed window's memory is soon another window (crash 5057, a seat's
	/// static text drawn as the old chat entry).  The room itself is safe to hold: roomSystem drops
	/// everything about it when it is destroyed
	struct Drawn
	{
		GameWindow *root;
		Int id;
		GameWinDrawFunc draw;
	};
	struct Hidden
	{
		GameWindow *root;
		Int id;
	};
	std::vector<Hidden> s_hidden;
	std::vector<Drawn> s_blanked;

	/// a room the lobby has been on, and the system function it had before roomSystem took it
	struct Hook
	{
		GameWindow *root;
		GameWinSystemFunc system;
	};
	std::vector<Hook> s_hooks;

	//-----------------------------------------------------------------------------------------------
	GameWindow *named( const char *what, Int seat = -1 )
	{
		AsciiString full;
		if( seat < 0 )
			full.format( "%s:%s", ROOMS[ s_room ].layout, what );
		else
			full.format( "%s:%s%d", ROOMS[ s_room ].layout, what, seat );
		return TheWindowManager->winGetWindowFromId( s_parent, NAMEKEY( full ) );
	}

	Bool isNetwork( void ) { return s_room != ROOM_SKIRMISH; }
	Bool amHost( void ) { return !isNetwork() || s_game->amIHost(); }

	/// the room's game as it is now, NULL once it went.  The lobby asks before every draw and click: a LAN
	/// host's leave pops the room, which fades out still drawing, and deletes the game at once (crashes
	/// 4776, 4797, 4798 read it freed)
	const GameInfo *liveGame( void )
	{
		if( s_room == ROOM_LAN )
			return TheLAN != NULL ? TheLAN->GetMyGame() : NULL;
		if( s_room == ROOM_ONLINE )
			return TheGameSpyInfo != NULL ? TheGameSpyInfo->getCurrentStagingRoom() : NULL;
		return TheSkirmishGameInfo;
	}

	Int localSlot( void )
	{
		const Int local = s_game->getLocalSlotNum();
		return local >= 0 || isNetwork() ? local : 0;
	}

	const GameSlot *seat( Int slot ) { return s_game->getConstSlot( slot ); }

	/// which slot shows at which seat, read again from the slot list
	void placeSeats( void ) { DirectStrikeSeats( s_game, s_seatOf, s_slotAt ); }

	/// the seat a slot shows at, the place its parts are drawn and hit
	Int seatOfSlot( Int slot ) { return slot >= 0 && slot < DSLOBBY_SEATS ? s_seatOf[ slot ] : 0; }
	DirectStrikeRect seatPart( Int slot, DirectStrikeSeatPart part ) { return DirectStrikeLobby_part( seatOfSlot( slot ), part ); }
	Color teamColor( Int team ) { return team == 0 ? TEAM_US : TEAM_THEM; }
	Color alpha( Color c, Int a ) { return U::withAlpha( c, a ); }

	//-----------------------------------------------------------------------------------------------
	Int screenX( Real x ) { return s_frame.ox + REAL_TO_INT( x * s_frame.k + 0.5f ); }
	Int screenY( Real y ) { return s_frame.oy + REAL_TO_INT( y * s_frame.k + 0.5f ); }
	Real middleOf( const DirectStrikeRect &r ) { return ( r.y0 + r.y1 ) * 0.5f; }
	/// a screen width back in 1080 pixels
	Real design( Int w ) { return (Real)w / s_frame.k; }

	IRegion2D regionOf( const DirectStrikeRect &r )
	{
		IRegion2D g;
		g.lo.x = screenX( r.x0 );
		g.lo.y = screenY( r.y0 );
		g.hi.x = screenX( r.x1 );
		g.hi.y = screenY( r.y1 );
		return g;
	}

	void fill( const DirectStrikeRect &r, Color c )
	{
		const IRegion2D g = regionOf( r );
		if( g.hi.x > g.lo.x && g.hi.y > g.lo.y )
			TheDisplay->drawFillRect( g.lo.x, g.lo.y, g.hi.x - g.lo.x, g.hi.y - g.lo.y, c );
	}

	/// a 1 px outline
	void box( const DirectStrikeRect &r, Color c )
	{
		const IRegion2D g = regionOf( r );
		TheDisplay->drawOpenRect( g.lo.x, g.lo.y, g.hi.x - g.lo.x, g.hi.y - g.lo.y, 1.0f, c );
	}

	/// a 1 px outline in its soft light
	void lit( const DirectStrikeRect &r, Color c ) { U::litEdge( regionOf( r ), c ); }

	/// a filled rect in its soft light (a team bar, a lit segment, the header rule)
	void litFill( const DirectStrikeRect &r, Color c )
	{
		const IRegion2D g = regionOf( r );
		U::litLine( g.lo.x, g.lo.y, max( 1, g.hi.x - g.lo.x ), max( 1, g.hi.y - g.lo.y ), c );
	}

	DirectStrikeRect grown( const DirectStrikeRect &r, Real by ) { return DirectStrike_rect( r.x0 - by, r.y0 - by, r.x1 + by, r.y1 + by ); }

	Bool pointerAt( Real *x, Real *y )
	{
		const MouseIO *mouse = TheMouse->getMouseStatus();
		DirectStrike_toDesign( s_frame, mouse->pos.x, mouse->pos.y, x, y );
		return TRUE;
	}

	Bool pointerIn( const DirectStrikeRect &r )
	{
		Real x, y;
		pointerAt( &x, &y );
		return r.contains( x, y );
	}

	/// under the pointer while no list is open
	Bool hot( const DirectStrikeRect &r ) { return s_list == LIST_NONE && pointerIn( r ); }

	/// the left button held down on r
	Bool held( const DirectStrikeRect &r ) { return pointerIn( r ) && TheMouse->getMouseStatus()->leftState != MBS_Up; }

	//-----------------------------------------------------------------------------------------------
	GameFont *menuFont( Int px1080, Bool bold ) { return U::menuFont( px1080, bold ); }

	/// words of px1080 in a band y0..y1, x their left (align -1), middle (0) or right end (+1); their width at 1080
	Real words( const UnicodeString &s, Int px1080, Bool bold, Real x, Real y0, Real y1, Color color, Int align = -1, Bool shadow = TRUE )
	{
		const Int top = screenY( y0 );
		return design( drawText( s, menuFont( px1080, bold ), screenX( x ), top, screenY( y1 ) - top, color, align, shadow ) );
	}

	Real wordsWidth( const UnicodeString &s, Int px1080, Bool bold ) { return design( textWidth( s, menuFont( px1080, bold ) ) ); }

	DisplayString *letter( GameFont *f, WideChar c )
	{
		for( size_t i = 0; i < s_letters.size(); ++i )
			if( s_letters[ i ].font == f && s_letters[ i ].c == c )
				return s_letters[ i ].s;
		Letter made = { f, c, TheDisplayStringManager->newDisplayString() };
		UnicodeString one;
		one.concat( c );
		made.s->setFont( f );
		made.s->setText( one );
		s_letters.push_back( made );
		return made.s;
	}

	/// bold capitals spaced `spacing` 1080 pixels apart in a band y0..y1, placed as words(); their width at 1080
	Real caps( const UnicodeString &s, Int px1080, Real spacing, Real x, Real y0, Real y1, Color color, Int align = -1, Bool shadow = TRUE )
	{
		GameFont *f = menuFont( px1080, TRUE );
		const Int gap = REAL_TO_INT( spacing * s_frame.k + 0.5f );
		Int total = 0, h = 0;
		for( Int i = 0; i < s.getLength(); ++i )
		{
			DisplayString *one = letter( f, s.getCharAt( i ) );
			Int w = 0, lh = 0;
			one->getSize( &w, &lh );
			total += w + ( i > 0 ? gap : 0 );
			h = max( h, lh );
		}
		Int at = align < 0 ? screenX( x ) : align == 0 ? screenX( x ) - total / 2 : screenX( x ) - total;
		const Int top = screenY( y0 ) + ( screenY( y1 ) - screenY( y0 ) - h ) / 2;
		for( Int i = 0; i < s.getLength(); ++i )
		{
			DisplayString *one = letter( f, s.getCharAt( i ) );
			if( s.getCharAt( i ) != u' ' )
			{
				if( shadow )
					one->draw( at, top, color, TEXT_SHADOW );
				else
					one->draw( at, top, color, color, 0, 0 );
			}
			at += one->getWidth( -1 ) + gap;
		}
		return design( total );
	}

	Real capsWidth( const UnicodeString &s, Int px1080, Real spacing )
	{
		GameFont *f = menuFont( px1080, TRUE );
		Real total = 0;
		for( Int i = 0; i < s.getLength(); ++i )
			total += design( letter( f, s.getCharAt( i ) )->getWidth( -1 ) ) + ( i > 0 ? spacing : 0 );
		return total;
	}

	//-----------------------------------------------------------------------------------------------
	// marks drawn in spans: a dot, a ring, a diamond, a chevron, a caret and a tick

	/// a disc of radius r (1080) centred at cx, cy, row by row; with `hole` a ring of that inner radius
	void disc( Real cx, Real cy, Real r, Color c, Real hole = 0 )
	{
		const Int rr = max( 1, REAL_TO_INT( r * s_frame.k + 0.5f ) ), hh = REAL_TO_INT( hole * s_frame.k + 0.5f );
		const Int x = screenX( cx ), y = screenY( cy );
		for( Int dy = -rr; dy <= rr; ++dy )
		{
			const Int w = REAL_TO_INT( sqrtf( (Real)( rr * rr - dy * dy ) ) + 0.5f );
			if( hh > 0 && dy * dy < hh * hh )
			{
				const Int inner = REAL_TO_INT( sqrtf( (Real)( hh * hh - dy * dy ) ) + 0.5f );
				TheDisplay->drawFillRect( x - w, y + dy, w - inner, 1, c );
				TheDisplay->drawFillRect( x + inner, y + dy, w - inner, 1, c );
			}
			else
				TheDisplay->drawFillRect( x - w, y + dy, 2 * w, 1, c );
		}
	}

	/// the fortress diamond, its white edge and, with `glow`, its light
	void diamond( Real cx, Real cy, Real r, Color c, Bool glow = TRUE )
	{
		const Int x = screenX( cx ), y = screenY( cy );
		Int rr = max( 3, REAL_TO_INT( r * s_frame.k + 0.5f ) );
		const Int halo = glow ? max( 2, rr / 3 ) : 0;
		for( Int pass = 0; pass < 3; ++pass )
		{
			const Int span = rr + ( pass == 0 ? halo : pass == 1 ? 0 : -1 );
			const Color colour = pass == 0 ? alpha( c, 60 ) : pass == 1 ? alpha( U::INK, 230 ) : c;
			if( pass == 0 && halo == 0 )
				continue;
			for( Int dy = -span; dy <= span; ++dy )
			{
				const Int w = span - abs( dy );
				TheDisplay->drawFillRect( x - w, y + dy, 2 * w + 1, 1, colour );
			}
		}
	}

	/// `count` chevrons of height h, pointing right or left, from x
	Real chevrons( Real x, Real cy, Color c, Int count, Bool right, Real h )
	{
		const Real w = h * 0.5f, step = h * 0.6f;
		const Real width = (Real)max( 2, REAL_TO_INT( 2.0f * s_frame.k + 0.5f ) );
		for( Int i = 0; i < count; ++i )
		{
			const Real x0 = x + step * (Real)i;
			const Real tip = right ? x0 + w : x0, back = right ? x0 : x0 + w;
			TheDisplay->drawLine( screenX( back ), screenY( cy - h * 0.5f ), screenX( tip ), screenY( cy ), width, c );
			TheDisplay->drawLine( screenX( tip ), screenY( cy ), screenX( back ), screenY( cy + h * 0.5f ), width, c );
		}
		return step * (Real)( count - 1 ) + w;
	}

	/// a dropdown's caret, a two-pixel V (pointing up while its list is open)
	void caret( Real cx, Real cy, Color c, Bool up )
	{
		const Real w = 6, h = 4, dir = up ? -1.0f : 1.0f;
		const Real width = (Real)max( 1, REAL_TO_INT( 2.0f * s_frame.k + 0.5f ) );
		TheDisplay->drawLine( screenX( cx - w ), screenY( cy - h * dir ), screenX( cx ), screenY( cy + h * dir ), width, c );
		TheDisplay->drawLine( screenX( cx ), screenY( cy + h * dir ), screenX( cx + w ), screenY( cy - h * dir ), width, c );
	}

	/// the current choice's tick, its right end at x1
	void tick( Real x1, Real cy, Color c )
	{
		const Real width = (Real)max( 2, REAL_TO_INT( 3.0f * s_frame.k + 0.5f ) );
		TheDisplay->drawLine( screenX( x1 - 18 ), screenY( cy ), screenX( x1 - 12 ), screenY( cy + 6 ), width, c );
		TheDisplay->drawLine( screenX( x1 - 12 ), screenY( cy + 6 ), screenX( x1 - 2 ), screenY( cy - 6 ), width, c );
	}

	/// a computer's chip mark, `size` square at x, centred on cy; its width
	Real chipMark( Real x, Real cy, Real size, Color c )
	{
		const Real body = size * 0.6f, in = ( size - body ) * 0.5f;
		const DirectStrikeRect core = DirectStrike_rect( x + in, cy - body * 0.5f, x + in + body, cy + body * 0.5f );
		box( core, c );
		box( grown( core, -3 ), c );
		for( Int i = 0; i < 3; ++i )
		{
			const Real at = core.x0 + body * ( 0.25f + 0.25f * (Real)i );
			fill( DirectStrike_rect( at, core.y0 - in, at + 1.5f, core.y0 ), c );
			fill( DirectStrike_rect( at, core.y1, at + 1.5f, core.y1 + in ), c );
			const Real ay = core.y0 + body * ( 0.25f + 0.25f * (Real)i );
			fill( DirectStrike_rect( core.x0 - in, ay, core.x0, ay + 1.5f ), c );
			fill( DirectStrike_rect( core.x1, ay, core.x1 + in, ay + 1.5f ), c );
		}
		return size;
	}

	/// a colour too dark to see on the glass without a light keyline
	Bool tooDark( Color c )
	{
		UnsignedByte red, green, blue, a;
		GameGetColorComponents( c, &red, &green, &blue, &a );
		return red * 299 + green * 587 + blue * 114 < 60 * 1000;
	}

	/// a colour's swatch, keylined when the colour is too dark; Random a "?" in a grey well
	void swatchAt( const DirectStrikeRect &r, Color c, Bool random )
	{
		if( random )
		{
			fill( r, GameMakeColor( 40, 42, 50, 255 ) );
			box( r, LIGHT_KEY );
			words( UnicodeString( u"?" ), TYPE_SMALL_PX, TRUE, ( r.x0 + r.x1 ) * 0.5f, r.y0, r.y1, U::INK, 0 );
			return;
		}
		if( tooDark( c ) )
			box( grown( r, 1 ), LIGHT_KEY );
		fill( r, c );
	}

	//-----------------------------------------------------------------------------------------------
	/// where a pick stands in the picker, FALSE for Random or anything the picker does not list
	Bool armyCell( Int pick, Int *column, Int *tile )
	{
		const PlayerTemplate *army = pick >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( pick ) : NULL;
		if( army == NULL )
			return FALSE;
		for( Int c = 0; c < DSLOBBY_ARMY_COLUMNS; ++c )
			for( Int t = 0; t < DSLOBBY_ARMY_TILES; ++t )
				if( army->getName().compare( ARMIES[ c ][ t ] ) == 0 )
				{
					*column = c;
					*tile = t;
					return TRUE;
				}
		return FALSE;
	}

	Bool isRandom( Int pick ) { return pick < 0 || ThePlayerTemplateStore->getNthPlayerTemplate( pick ) == NULL; }

	UnicodeString sideName( Int pick )
	{
		AsciiString side;
		side.format( "SIDE:%s", ThePlayerTemplateStore->getNthPlayerTemplate( pick )->getSide().str() );
		return TheGameText->fetch( side );
	}

	/// the army's name, and for a general his side's name in `side`
	UnicodeString armyName( Int pick, UnicodeString *side )
	{
		Int column, tile;
		if( armyCell( pick, &column, &tile ) )
		{
			if( tile > 0 && side != NULL )
				*side = TheGameText->fetch( ARMY_LABELS[ column ][ 0 ] );
			return TheGameText->fetch( ARMY_LABELS[ column ][ tile ] );
		}
		if( isRandom( pick ) )
			return TheGameText->fetch( "GUI:Random" );
		return sideName( pick );
	}

	/// the seat control's pick in a row; Hard is EA's Brutal state, Brutal the first level above it
	Int seatPick( Int row )
	{
		const Int picks[ DSLOBBY_SEAT_ROWS ] =
		{
			SLOT_OPEN, SLOT_CLOSED, SLOT_EASY_AI, SLOT_MED_AI, SLOT_BRUTAL_AI, AILevelAboveHardRating[ 0 ],
		};
		return picks[ row ];
	}

	/// a seat control row's words, the seat box's own (GUI:SlotEasyAI to GUI:SlotBrutalAI)
	UnicodeString seatRowLabel( Int row )
	{
		if( row == 0 )
			return TheGameText->fetch( "GUI:Open" );
		if( row == 1 )
			return TheGameText->fetch( "GUI:Closed" );
		if( row == DSLOBBY_SEAT_ROWS - 1 )
			return SlotStateName( SLOT_BRUTAL_AI, AILevelAboveHardRating[ 0 ] );
		return SlotStateName( (SlotState)seatPick( row ) );
	}

	/// the colours a seat may take: every colour no other seat holds, in the settings' order
	Int freeColors( Int slot, Int *colors )
	{
		Int count = 0;
		for( Int c = 0; c < TheMultiplayerSettings->getNumColors() && count < COLOR_LIMIT - 1; ++c )
		{
			Bool taken = FALSE;
			for( Int i = 0; i < MAX_SLOTS && !taken; ++i )
				taken = i != slot && seat( i ) != NULL && seat( i )->getColor() == c;
			if( !taken )
				colors[ count++ ] = c;
		}
		return count;
	}

	UnicodeString colorName( Int color )
	{
		MultiplayerColorDefinition *def = color >= 0 ? TheMultiplayerSettings->getColor( color ) : NULL;
		return def != NULL ? TheGameText->fetch( def->getTooltipName() ) : TheGameText->fetch( "GUI:Random" );
	}

	/// a remote human's round trip in ms, -1 while nobody has measured one
	Int seatPing( Int slot )
	{
		if( s_room == ROOM_LAN )
			return TheLAN != NULL ? TheLAN->getPeerPing( seat( slot )->getIP() ) : -1;
		GameSpyStagingRoom *room = TheGameSpyInfo != NULL ? TheGameSpyInfo->getCurrentStagingRoom() : NULL;
		GameSpyGameSlot *player = room != NULL ? room->getGameSpySlot( slot ) : NULL;
		if( player == NULL )
			return -1;
		if( !OnlineIsEnabled() )
			return player->getPingAsInt();
		// -online routes every game through the relay; the light is the player's own trip to the server
		AsciiString nick;
		nick.translate( player->getName() );
		OnlinePlayerInfo card;
		return OnlineGetPlayerInfo( nick, card ) ? card.ping : -1;
	}

	/// the humans START waits for: the host's own seat is never asked
	Int humansWaiting( void )
	{
		Int waiting = 0;
		for( Int i = 1; i < MAX_SLOTS; ++i )
			if( seat( i ) != NULL && seat( i )->isHuman() && !seat( i )->isAccepted() )
				++waiting;
		return waiting;
	}

	/// Start's state: the host's I while a human is not ready, a ready client's P, H under the pointer
	const char *startState( void )
	{
		if( isNetwork() && !amHost() )
		{
			const GameSlot *me = localSlot() >= 0 ? seat( localSlot() ) : NULL;
			if( me != NULL && me->isAccepted() )
				return "P";
		}
		else if( isNetwork() && humansWaiting() > 0 )
			return "I";
		return hot( DirectStrikeLobby_start( isNetwork() ) ) ? "H" : "";
	}

	Bool editsSeat( Int slot ) { return amHost() && slot != localSlot(); }

	Bool editsArmy( Int slot )
	{
		const GameSlot *s = seat( slot );
		return s->isOccupied() && ( slot == localSlot() || ( amHost() && s->isAI() ) );
	}

	/// ready as the tag says: an AI, the host (a skirmish's player too) or a joiner who accepted
	Bool isReady( Int slot )
	{
		const GameSlot *s = seat( slot );
		return s != NULL && s->isOccupied() && ( s->isAI() || !isNetwork() || slot == 0 || s->isAccepted() );
	}

	/// the seats a team has that are not closed, as the team size chip reads them, and how many of them are ready
	Int teamSize( Int team )
	{
		Int size = 0;
		for( Int row = 0; row < DSLOBBY_ROWS; ++row )
		{
			const GameSlot *s = seat( s_slotAt[ DirectStrikeLobby_seat( team, row ) ] );
			if( s != NULL && s->getState() != SLOT_CLOSED )
				++size;
		}
		return size;
	}

	Int teamReady( Int team )
	{
		Int ready = 0;
		for( Int row = 0; row < DSLOBBY_ROWS; ++row )
			if( isReady( s_slotAt[ DirectStrikeLobby_seat( team, row ) ] ) )
				++ready;
		return ready;
	}

	/// whether the local player can move to `at`: he holds a seat of the six and that seat's slot is open or a
	/// computer's, who takes the seat he leaves (DirectStrikeCanTakeSeat).  A closed seat, a human's and his own do
	/// nothing.
	Bool canMoveTo( Int at ) { return DirectStrikeCanTakeSeat( s_game, localSlot(), at ); }

	/// the seat a click on team `team`'s head moves the local player to: that team's first open seat down its rows,
	/// else its first computer's, -1 for a team of humans or the player's own (the head of his team takes him nowhere)
	Int headMove( Int team )
	{
		const Int local = localSlot();
		if( local < 0 || local >= DSLOBBY_SEATS || DirectStrikeLobby_team( s_seatOf[ local ] ) == team )
			return -1;
		for( Int pass = 0; pass < 2; ++pass )
			for( Int row = 0; row < DSLOBBY_ROWS; ++row )
			{
				const Int at = DirectStrikeLobby_seat( team, row );
				if( canMoveTo( at ) && ( pass == 1 || seat( s_slotAt[ at ] )->getState() == SLOT_OPEN ) )
					return at;
			}
		return -1;
	}

	/// the words' widths of an open seat's level keys, in 1080 pixels, for DirectStrikeLobby_levelKey
	void levelWords( Real widths[ DSLOBBY_LEVELS ] )
	{
		for( Int i = 0; i < DSLOBBY_LEVELS; ++i )
			widths[ i ] = wordsWidth( TheGameText->fetch( LEVEL_LABELS[ i ] ), TYPE_SMALL_PX, FALSE );
	}

	/// the level key of an open or closed seat under (x, y) for the host, -1 for none
	Int levelAt( Int at, Real x, Real y )
	{
		const GameSlot *s = seat( s_slotAt[ at ] );
		if( !amHost() || s == NULL || s->isOccupied() )
			return -1;
		Real widths[ DSLOBBY_LEVELS ];
		levelWords( widths );
		for( Int i = 0; i < DSLOBBY_LEVELS; ++i )
			if( DirectStrikeLobby_levelKey( at, i, widths ).contains( x, y ) )
				return i;
		return -1;
	}

	/// what a click moves the player by: an open or computer seat's card, but not the host's controls on it (the
	/// seat line, which is the seat control; a computer's army and colour; an empty seat's level keys and label)
	Bool overMove( Int at, Real x, Real y )
	{
		if( !canMoveTo( at ) || !DirectStrikeLobby_card( at ).contains( x, y ) )
			return FALSE;
		const Int slot = s_slotAt[ at ];
		if( editsSeat( slot ) && DirectStrikeLobby_part( at, DSSEAT_LINE ).contains( x, y ) )
			return FALSE;
		if( amHost() && !seat( slot )->isOccupied() && ( DirectStrikeLobby_line( at, 1 ).contains( x, y ) || DirectStrikeLobby_line( at, 2 ).contains( x, y ) ) )
			return FALSE;
		return !( editsArmy( slot ) && ( DirectStrikeLobby_part( at, DSSEAT_ARMY ).contains( x, y )
			|| DirectStrikeLobby_part( at, DSSEAT_COLOR ).contains( x, y ) ) );
	}

	Bool pointerOverMove( Int at )
	{
		Real x, y;
		pointerAt( &x, &y );
		return s_list == LIST_NONE && overMove( at, x, y );
	}

	//-----------------------------------------------------------------------------------------------
	void drawNothing( GameWindow *window, WinInstanceData *instData )
	{
	}

	Bool holds( GameWindow *ancestor, GameWindow *window )
	{
		for( GameWindow *up = window != NULL ? window->winGetParent() : NULL; up != NULL; up = up->winGetParent() )
			if( up == ancestor )
				return TRUE;
		return FALSE;
	}

	/// every window of the room hidden but the chat entry; the windows that hold it stay, drawing nothing.
	/// A window with no id cannot be found again at the restore, so it is hidden without a record
	void hideUnder( GameWindow *root, GameWindow *window, GameWindow *keep )
	{
		for( GameWindow *child = window->winGetChild(); child != NULL; child = child->winGetNext() )
		{
			const Int id = child->winGetWindowId();
			if( child == keep || holds( child, keep ) )
			{
				if( child->winIsHidden() )
					child->winHide( FALSE );
				if( child->winGetDrawFunc() != drawNothing )
				{
					if( id != 0 )
					{
						const Drawn was = { root, id, child->winGetDrawFunc() };
						s_blanked.push_back( was );
					}
					child->winSetDrawFunc( drawNothing );
				}
				if( child != keep )
					hideUnder( root, child, keep );
			}
			else if( !child->winIsHidden() )
			{
				Bool known = id == 0;
				for( size_t i = 0; i < s_hidden.size() && !known; ++i )
					known = s_hidden[ i ].root == root && s_hidden[ i ].id == id;
				if( !known )
				{
					const Hidden was = { root, id };
					s_hidden.push_back( was );
				}
				child->winHide( TRUE );
			}
		}
	}

	void hideRoom( GameWindow *room, GameWindow *keep )
	{
		hideUnder( room, room, keep );
	}

	GameWindow *s_fading = NULL;				///< the room the lobby left as its layout fades out
	GameWinDrawFunc s_fadingDraw = NULL;		///< that room's own draw, back at DirectStrikeLobby_restoreRoom

	/// the room's own system function, and when the room is destroyed everything the lobby kept about it goes
	WindowMsgHandledType roomSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
	{
		GameWinSystemFunc system = NULL;
		for( size_t i = 0; i < s_hooks.size(); ++i )
			if( s_hooks[ i ].root == window )
			{
				system = s_hooks[ i ].system;
				if( msg == GWM_DESTROY )
					s_hooks.erase( s_hooks.begin() + i );
				break;
			}
		if( msg == GWM_DESTROY )
		{
			if( window == s_parent )
				DirectStrikeLobby_detach();
			if( window == s_fading )
			{
				s_fading = NULL;
				s_fadingDraw = NULL;
			}
			for( size_t i = s_blanked.size(); i-- > 0; )
				if( s_blanked[ i ].root == window )
					s_blanked.erase( s_blanked.begin() + i );
			for( size_t i = s_hidden.size(); i-- > 0; )
				if( s_hidden[ i ].root == window )
					s_hidden.erase( s_hidden.begin() + i );
		}
		return system != NULL ? system( window, msg, mData1, mData2 ) : MSG_IGNORED;
	}

	/// the window of this id inside the room only: a NULL start would search every window there is
	GameWindow *inRoom( GameWindow *room, Int id )
	{
		GameWindow *first = room->winGetChild();
		return first != NULL ? TheWindowManager->winGetWindowFromId( first, id ) : NULL;
	}

	void hookRoom( GameWindow *room )
	{
		if( room->winGetSystemFunc() == roomSystem )
			return;
		const Hook hook = { room, room->winGetSystemFunc() };
		s_hooks.push_back( hook );
		room->winSetSystemFunc( roomSystem );
	}

	/// the left room's draw through the fade-out: the shell's transition shows the stock gadgets again, so they go every frame
	void fadingDraw( GameWindow *window, WinInstanceData * )
	{
		hideRoom( window, NULL );
	}

	/// the chat entry on the chat input's rect, so a click there gives it the keyboard
	void placeEntry( GameWindow *entry )
	{
		const DirectStrikeHudBox b = DirectStrike_box( s_frame, DirectStrikeLobby_chatInput() );
		Int upX = 0, upY = 0, x = 0, y = 0, w = 0, h = 0;
		if( entry->winGetParent() != NULL )
			entry->winGetParent()->winGetScreenPosition( &upX, &upY );
		entry->winGetScreenPosition( &x, &y );
		entry->winGetSize( &w, &h );
		if( x != b.x || y != b.y )
			entry->winSetPosition( b.x - upX, b.y - upY );
		if( w != b.w || h != b.h )
			entry->winSetSize( b.w, b.h );
	}

	//-----------------------------------------------------------------------------------------------
	/// a dropdown: H lights the caret, P (its list open) lights the edge and washes the face; a field (a choice the
	/// player does not own) has no caret.  Returns where its words go.
	DirectStrikeRect dropdown( const DirectStrikeRect &r, const char *state, Bool editable )
	{
		fill( r, GameMakeColor( 0, 0, 0, 150 ) );
		if( *state == 'P' )
		{
			fill( r, alpha( U::EDGE, 54 ) );
			lit( r, U::EDGE_LIT );
		}
		else
			box( r, editable ? alpha( U::EDGE, 170 ) : alpha( U::FRAME, 150 ) );
		if( !editable )
			return DirectStrike_rect( r.x0 + CONTENT_IN, r.y0, r.x1 - CONTENT_IN, r.y1 );
		fill( DirectStrike_rect( r.x1 - CARET_CELL, r.y0 + 8, r.x1 - CARET_CELL + 1, r.y1 - 8 ), alpha( U::FRAME, 200 ) );
		caret( r.x1 - CARET_CELL * 0.5f, middleOf( r ), *state != 0 ? U::EDGE_LIT : U::INK, *state == 'P' );
		return DirectStrike_rect( r.x0 + CONTENT_IN, r.y0, r.x1 - CARET_CELL - 12, r.y1 );
	}

	/// a dropdown chip's state: P while its list is open, H under the pointer while no list is
	const char *chipState( const DirectStrikeRect &r, List list, Int slot )
	{
		if( s_list == list && s_listSlot == slot )
			return "P";
		return hot( r ) ? "H" : "";
	}

	/// an outline key with words: black, a blue edge, or under the pointer a white lift and a cyan edge
	void outlineKey( const DirectStrikeRect &r, const UnicodeString &label, Int px1080, Bool spaced, Bool lifted )
	{
		fill( r, GameMakeColor( 0, 0, 0, 160 ) );
		if( lifted )
		{
			fill( r, GameMakeColor( 255, 255, 255, 14 ) );
			lit( r, U::EDGE_LIT );
		}
		else
			box( r, alpha( U::EDGE, 200 ) );
		if( spaced )
			caps( label, px1080, 1.4f, ( r.x0 + r.x1 ) * 0.5f, r.y0, r.y1, U::INK, 0 );
		else
			words( label, px1080, FALSE, ( r.x0 + r.x1 ) * 0.5f, r.y0, r.y1, U::INK, 0 );
	}

	/// the glass of an open list: a soft shadow, near opaque black, a lit blue edge
	void listPanel( const DirectStrikeRect &r )
	{
		fill( DirectStrike_rect( r.x0 - 8, r.y0 + 4, r.x1 + 8, r.y1 + 16 ), GameMakeColor( 0, 0, 0, 90 ) );
		fill( DirectStrike_rect( r.x0 - 3, r.y0 + 2, r.x1 + 3, r.y1 + 8 ), GameMakeColor( 0, 0, 0, 110 ) );
		fill( r, GameMakeColor( 6, 8, 14, 244 ) );
		lit( r, U::EDGE );
	}

	/// a list's row: under the pointer a white lift and a cyan edge, the current choice a blue wash and a tick
	void listRow( const DirectStrikeRect &r, const UnicodeString &label, Bool current, Int px1080, Bool computer )
	{
		const Bool over = pointerIn( r );
		if( over )
		{
			fill( r, GameMakeColor( 255, 255, 255, 26 ) );
			lit( r, U::EDGE_LIT );
		}
		else if( current )
			fill( r, alpha( U::EDGE, 70 ) );
		Real x = r.x0 + 14;
		if( computer )
			x += chipMark( x, middleOf( r ), 20, U::INK ) + 12;
		words( label, px1080, FALSE, x, r.y0, r.y1, U::INK );
		if( current )
			tick( r.x1 - 12, middleOf( r ), U::EDGE_LIT );
	}

	//-----------------------------------------------------------------------------------------------
	/// the room's facts after the title: what room, the size, then the version or the room's name
	UnicodeString facts( void )
	{
		UnicodeString room, extra;
		if( s_room == ROOM_SKIRMISH )
		{
			room = TheGameText->fetch( "GUI:DirectStrikeSkirmish" );
			extra = TheGameText->fetch( "GUI:DirectStrikeVersion" );
		}
		else if( s_room == ROOM_LAN )
		{
			room = TheGameText->fetch( "GUI:DirectStrikeLan" );
			extra = const_cast<LANGameInfo *>( static_cast<const LANGameInfo *>( s_game ) )->getName();
		}
		else
		{
			room = TheGameText->fetch( "GUI:DirectStrikeOnline" );
			GameSpyStagingRoom *staging = TheGameSpyInfo != NULL ? TheGameSpyInfo->getCurrentStagingRoom() : NULL;
			if( staging != NULL )
				extra = staging->getGameName();
		}
		UnicodeString size, line;
		size.format( TheGameText->fetch( "GUI:DirectStrikeTeamSize" ), teamSize( 0 ), teamSize( 1 ) );
		// the middle dot by its number: the file is read in the machine's code page, where a typed dot
		// becomes its two UTF-8 bytes
		static const WideChar DOT[] = { ' ', ' ', 0x00B7, ' ', ' ', 0 };
		line.format( u"%ls%ls%ls", room.str(), DOT, size.str() );
		if( !extra.isEmpty() )
		{
			line.concat( DOT );
			line.concat( extra );
		}
		return line;
	}

	/// the end screen's header: the title, the facts, BACK, the rule and under it each team's ready share from its
	/// own end toward the white middle tick
	void drawHeader( void )
	{
		const DirectStrikeRect title = DirectStrikeLobby_title();
		const Real w = caps( TheGameText->fetch( "GUI:DirectStrike" ), TYPE_TITLE, TYPE_TITLE * 0.12f, title.x0, title.y0, title.y1, U::INK );
		words( facts(), TYPE_FACTS, FALSE, title.x0 + w + 24, title.y0 + 10, title.y1 - 2, U::MUTED );

		const DirectStrikeRect back = DirectStrikeLobby_back();
		const U::KeyState state = s_list != LIST_NONE ? U::KEY_NORMAL : held( back ) ? U::KEY_PRESSED : pointerIn( back ) ? U::KEY_HOVER : U::KEY_NORMAL;
		U::menuKey( regionOf( back ), pooled( TheGameText->fetch( "GUI:Back" ), menuFont( U::MENU_TEXT_KEY, TRUE ) ), state, FALSE );

		const Real x0 = DSLOBBY_PANEL_X0 + 2, x1 = DSLOBBY_PANEL_X1 - 2, mid = ( x0 + x1 ) * 0.5f;
		litFill( DirectStrike_rect( x0, DSLOBBY_RULE_Y, x1, DSLOBBY_RULE_Y + 2 ), U::FRAME );
		for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
		{
			const Real n = ( mid - x0 ) * (Real)teamReady( team ) / (Real)max( 1, teamSize( team ) );
			if( n >= 1 )
				litFill( team == 0 ? DirectStrike_rect( x0, DSLOBBY_RULE_Y + 3, x0 + n, DSLOBBY_RULE_Y + 5 )
					: DirectStrike_rect( x1 - n, DSLOBBY_RULE_Y + 3, x1, DSLOBBY_RULE_Y + 5 ), teamColor( team ) );
		}
		fill( DirectStrike_rect( mid, DSLOBBY_RULE_Y - 3, mid + 1, DSLOBBY_RULE_Y + 9 ), U::INK );
	}

	/// a team's head as the end screen's: the lit bar, TEAM n, two chevrons toward the lane, READY and the count at
	/// the right.  A click takes the player to the team's first open seat, so under the pointer it lifts and says so.
	void drawHead( Int team )
	{
		const DirectStrikeRect head = DirectStrikeLobby_teamHead( team );
		const Bool over = headMove( team ) >= 0 && hot( head );
		if( over )
		{
			const DirectStrikeRect lift = DirectStrike_rect( head.x0, head.y0 - 2, head.x1, head.y1 + 2 );
			fill( lift, GameMakeColor( 255, 255, 255, 16 ) );
			lit( lift, U::EDGE_LIT );
		}
		const Color colour = teamColor( team );
		litFill( DirectStrike_rect( head.x0, head.y0 + 4, head.x0 + 4, head.y1 - 4 ), colour );
		UnicodeString label;
		label.format( TheGameText->fetch( "GUI:DirectStrikeTeam" ), team + 1 );
		Real x = head.x0 + 16;
		x += caps( label, TYPE_TEAM, 2.4f, x, head.y0, head.y1, U::INK ) + 14;
		if( over )
			x += caps( TheGameText->fetch( "GUI:DirectStrikeMoveHere" ), TYPE_CAPS, 2.0f, x, head.y0 + 2, head.y1, U::EDGE_LIT ) + 14;
		chevrons( x, middleOf( head ), colour, 2, team == 0, 12 );

		const Int size = teamSize( team ), ready = teamReady( team );
		UnicodeString count;
		count.format( u"%d/%d", ready, size );
		const Real fw = words( count, TYPE_HERO, FALSE, head.x1, head.y0 - 2, head.y1, U::INK, 1 );
		caps( TheGameText->fetch( "GUI:DirectStrikeReady" ), TYPE_CAPS, 1.4f, head.x1 - fw - 12, head.y0 + 10, head.y1 - 4,
			ready == size ? U::ACTIVE : U::MUTED, 1 );
		litFill( DirectStrike_rect( head.x0, head.y1 + 6, head.x1, head.y1 + 7 ), U::FRAME );
	}

	/// a seat's picture: the general's face or the side's command centre, a "?" for Random; the colour along its
	/// foot, keylined when dark; FRAME round it
	void drawPicture( Int slot )
	{
		const GameSlot *s = seat( slot );
		const DirectStrikeRect r = seatPart( slot, DSSEAT_PORTRAIT );
		const IRegion2D g = regionOf( r );
		const Int pick = s->getPlayerTemplate();
		const Image *picture = isRandom( pick ) ? NULL : U::armyPortrait( ThePlayerTemplateStore->getNthPlayerTemplate( pick ) );
		if( picture != NULL )
			U::coverImage( picture, g.lo.x, g.lo.y, g.hi.x - g.lo.x, g.hi.y - g.lo.y );
		else
		{
			fill( r, GameMakeColor( 24, 26, 34, 255 ) );
			words( UnicodeString( u"?" ), 64, TRUE, ( r.x0 + r.x1 ) * 0.5f, r.y0, r.y1, U::MUTED, 0 );
		}
		const Color colour = s->getColor() >= 0 ? LobbySeatColor( s ) : UNPICKED;
		const DirectStrikeRect band = DirectStrike_rect( r.x0, r.y1 - 5, r.x1, r.y1 );
		if( tooDark( colour ) )
			fill( DirectStrike_rect( band.x0, band.y0 - 1, band.x1, band.y0 ), LIGHT_KEY );
		fill( band, colour );
		box( r, alpha( U::FRAME, 220 ) );
	}

	/// the + that takes the player to a seat, over its picture: cyan and lit under the pointer
	void drawTakeMark( const DirectStrikeRect &picture, Bool over, Bool can )
	{
		const Real cx = ( picture.x0 + picture.x1 ) * 0.5f, cy = ( picture.y0 + picture.y1 ) * 0.5f - 14;
		const Color c = over ? U::EDGE_LIT : can ? U::MUTED : DIM;
		const DirectStrikeRect across = DirectStrike_rect( cx - 24, cy - 1, cx + 24, cy + 2 ), down = DirectStrike_rect( cx - 1, cy - 24, cx + 2, cy + 24 );
		if( over )
		{
			fill( picture, GameMakeColor( 255, 255, 255, 12 ) );
			litFill( across, c );
			litFill( down, c );
		}
		else
		{
			fill( across, c );
			fill( down, c );
		}
		caps( TheGameText->fetch( "GUI:DirectStrikeTakeSeat" ), TYPE_CAPS, 1.6f, cx, cy + 36, cy + 56, c, 0 );
	}

	/// a network seat's readiness on its picture's top corner toward the lane: the lamp and its word
	void drawTag( Int at, Int slot )
	{
		const GameSlot *s = seat( slot );
		const Bool host = s->isHuman() && slot == 0;
		const Bool ready = isReady( slot );
		const UnicodeString word = TheGameText->fetch( host ? "GUI:DirectStrikeHost" : ready ? "GUI:DirectStrikeReady" : "GUI:DirectStrikeWaiting" );
		const Color c = host ? U::EDGE_LIT : ready ? U::ACTIVE : AMBER;
		const DirectStrikeRect strip = DirectStrikeLobby_part( at, DSSEAT_TAG );
		const Real w = 10 + 12 + 8 + capsWidth( word, TYPE_CAPS, 1.6f ) + 12;
		const Real x0 = DirectStrikeLobby_team( at ) == 0 ? strip.x1 - w : strip.x0;
		const DirectStrikeRect badge = DirectStrike_rect( x0, strip.y0, x0 + w, strip.y1 );
		fill( badge, GameMakeColor( 0, 0, 0, 200 ) );
		box( badge, alpha( c, 200 ) );
		const Real cy = middleOf( badge );
		if( host || ready )
			disc( badge.x0 + 16, cy, 6, c );
		else
			disc( badge.x0 + 16, cy, 6, c, 4 );
		caps( word, TYPE_CAPS, 1.6f, badge.x0 + 30, badge.y0, badge.y1, c );
	}

	/// the ping in a seat line: its figure and four bars, lit by the round trip; returns where the name must end
	Real drawPing( const DirectStrikeRect &content, Int ping )
	{
		const Int n = ping < PING_GOOD ? 4 : ping < PING_FAIR ? 3 : ping < PING_POOR ? 2 : 1;
		const Color c = n >= 3 ? U::ACTIVE : n == 2 ? AMBER : PING_RED;
		const Real cy = middleOf( content );
		for( Int i = 0; i < 4; ++i )
		{
			const Real h = 6.0f + 4.0f * (Real)i, x = content.x1 - 26 + 7 * (Real)i;
			fill( DirectStrike_rect( x, cy + 9 - h, x + 5, cy + 9 ), i < n ? c : GameMakeColor( 255, 255, 255, 40 ) );
		}
		UnicodeString figure;
		figure.format( TheGameText->fetch( "GUI:DirectStrikePing" ), ping );
		const Real tw = words( figure, TYPE_CAPS, FALSE, content.x1 - 30 - 8, content.y0, content.y1, U::MUTED, 1 );
		return content.x1 - 30 - 16 - tw;
	}

	/// an open or closed seat: the dashed square takes the player there; the host gets the seat control, ADD
	/// COMPUTER and the four level keys; a client one TAKE SEAT key.  A closed seat is the same card darkened.
	void drawEmptySeat( Int at, Int slot )
	{
		const GameSlot *s = seat( slot );
		const Bool closed = s->getState() == SLOT_CLOSED;
		const DirectStrikeRect picture = DirectStrikeLobby_part( at, DSSEAT_PORTRAIT );
		fill( picture, GameMakeColor( 0, 0, 0, 90 ) );
		const Color dash = alpha( U::FRAME, 220 );
		for( Real x = picture.x0; x < picture.x1; x += 8 )
		{
			fill( DirectStrike_rect( x, picture.y0, min( x + 3, picture.x1 ), picture.y0 + 1 ), dash );
			fill( DirectStrike_rect( x, picture.y1 - 1, min( x + 3, picture.x1 ), picture.y1 ), dash );
		}
		for( Real y = picture.y0; y < picture.y1; y += 8 )
		{
			fill( DirectStrike_rect( picture.x0, y, picture.x0 + 1, min( y + 3, picture.y1 ) ), dash );
			fill( DirectStrike_rect( picture.x1 - 1, y, picture.x1, min( y + 3, picture.y1 ) ), dash );
		}
		const Real cx = ( picture.x0 + picture.x1 ) * 0.5f, cy = middleOf( picture ) - 14;
		if( closed )
		{
			disc( cx, cy, 24, DIM, 21 );
			const Real width = (Real)max( 2, REAL_TO_INT( 3.0f * s_frame.k + 0.5f ) );
			TheDisplay->drawLine( screenX( cx - 17 ), screenY( cy + 17 ), screenX( cx + 17 ), screenY( cy - 17 ), width, DIM );
			words( TheGameText->fetch( "GUI:Closed" ), TYPE_CAPS, TRUE, cx, cy + 36, cy + 56, DIM, 0 );
		}
		else
			drawTakeMark( picture, pointerOverMove( at ), canMoveTo( at ) );

		const DirectStrikeRect line = DirectStrikeLobby_part( at, DSSEAT_LINE );
		const DirectStrikeRect content = dropdown( line, amHost() ? chipState( line, LIST_SEAT, slot ) : "", amHost() );
		words( TheGameText->fetch( closed ? "GUI:Closed" : "GUI:Open" ), TYPE_BODY_PX, FALSE, content.x0, content.y0, content.y1, closed ? DIM : U::MUTED );
		if( amHost() )
		{
			const DirectStrikeRect label = DirectStrikeLobby_line( at, 1 );
			const Real iw = chipMark( label.x0, label.y1 - 12, 18, U::MUTED );
			caps( TheGameText->fetch( "GUI:DirectStrikeAddComputer" ), TYPE_CAPS, 1.6f, label.x0 + iw + 10, label.y1 - 24, label.y1, U::MUTED );
			Real widths[ DSLOBBY_LEVELS ];
			levelWords( widths );
			for( Int i = 0; i < DSLOBBY_LEVELS; ++i )
			{
				const DirectStrikeRect key = DirectStrikeLobby_levelKey( at, i, widths );
				outlineKey( key, TheGameText->fetch( LEVEL_LABELS[ i ] ), TYPE_SMALL_PX, FALSE, hot( key ) );
			}
		}
		else if( !closed )
		{
			const DirectStrikeRect key = DirectStrikeLobby_takeSeat( at );
			outlineKey( key, TheGameText->fetch( "GUI:DirectStrikeTakeSeat" ), TYPE_BODY_PX, TRUE, pointerOverMove( at ) && pointerIn( key ) );
		}
		if( closed )
			fill( DirectStrikeLobby_card( at ), GameMakeColor( 0, 0, 0, 80 ) );
	}

	/// the seat `at`: the local player's gold edge and wash, the picture, the seat line, the army and the colour,
	/// and in a network room the readiness tag and a human's ping
	void drawSeat( Int at )
	{
		const Int slot = s_slotAt[ at ];
		const GameSlot *s = seat( slot );
		if( s == NULL )
			return;
		if( slot == localSlot() )
			U::menuRow( regionOf( DirectStrikeLobby_card( at ) ), U::ROW_LOCAL );
		if( !s->isOccupied() )
		{
			drawEmptySeat( at, slot );
			return;
		}
		drawPicture( slot );
		// a computer's card takes the player too, the computer going to the seat he leaves: its + while it would
		if( pointerOverMove( at ) )
			drawTakeMark( seatPart( slot, DSSEAT_PORTRAIT ), TRUE, TRUE );

		const DirectStrikeRect line = seatPart( slot, DSSEAT_LINE );
		const Bool editSeat = editsSeat( slot );
		DirectStrikeRect content = dropdown( line, editSeat ? chipState( line, LIST_SEAT, slot ) : "", editSeat );
		// online the light is the player's own trip to the server, so your own seat has one; a LAN room has no ping of your own
		const Bool ownLight = s_room == ROOM_ONLINE && OnlineIsEnabled();
		const Int ping = isNetwork() && s->isHuman() && ( slot != localSlot() || ownLight ) ? seatPing( slot ) : -1;
		if( ping >= 0 )
			content.x1 = drawPing( content, ping );
		if( s->isAI() )
		{
			const Real iw = chipMark( content.x0, middleOf( content ), 22, U::INK );
			words( SlotStateName( s->getState(), s->getAIRating() ), TYPE_NAME_PX, FALSE, content.x0 + iw + 10, content.y0, content.y1, U::INK );
		}
		else
			words( s->getName(), TYPE_NAME_PX, FALSE, content.x0, content.y0, content.y1, slot == localSlot() ? U::GOLD : U::INK );

		const Bool editArmy = editsArmy( slot );
		const DirectStrikeRect army = seatPart( slot, DSSEAT_ARMY );
		content = dropdown( army, editArmy ? chipState( army, LIST_ARMY, slot ) : "", editArmy );
		UnicodeString side;
		const Real aw = words( armyName( s->getPlayerTemplate(), &side ), TYPE_ARMY, FALSE, content.x0, content.y0, content.y1, U::INK );
		if( !side.isEmpty() )
			words( side, TYPE_SMALL_PX, FALSE, content.x0 + aw + 10, content.y0 + 2, content.y1, U::MUTED );

		if( isNetwork() )
			drawTag( at, slot );

		const DirectStrikeRect color = seatPart( slot, DSSEAT_COLOR );
		content = dropdown( color, editArmy ? chipState( color, LIST_COLOR, slot ) : "", editArmy );
		const Bool random = s->getColor() < 0;
		const Real cy = middleOf( content );
		swatchAt( DirectStrike_rect( content.x0, cy - 11, content.x0 + 34, cy + 11 ), random ? UNPICKED : LobbySeatColor( s ), random );
		words( colorName( s->getColor() ), TYPE_BODY_PX, FALSE, content.x0 + 34 + 12, content.y0, content.y1, random ? U::MUTED : U::INK );
	}

	//-----------------------------------------------------------------------------------------------
	/// the map's own preview with the lane drawn over it: each team's plots at the map's starts in their
	/// players' colours (yours ringed gold, an open one hollow, a closed one gone), the fortresses, the lane between
	/// them and its middle; the key under it names the marks
	void drawIsland( void )
	{
		const Bool network = isNetwork();
		const DirectStrikeRect island = DirectStrikeLobby_island( network ), key = DirectStrikeLobby_islandKey( network );
		const DirectStrikeRect area = DirectStrike_rect( island.x0 + 1, island.y0 + 1, island.x1 - 1, key.y0 );
		fill( island, GameMakeColor( 4, 10, 26, 170 ) );

		AsciiString mapName = s_game->getMap();
		mapName.toLower();
		const MapMetaData *md = TheMapCache != NULL ? TheMapCache->findMap( mapName ) : NULL;
		const Real ew = md != NULL ? md->m_extent.hi.x - md->m_extent.lo.x : 0.0f, eh = md != NULL ? md->m_extent.hi.y - md->m_extent.lo.y : 0.0f;
		if( md != NULL && ew > 0.0f && eh > 0.0f )
		{
			// the map at its own shape, as large as the area holds
			const Real ratio = ew / eh, aw = area.x1 - area.x0, ah = area.y1 - area.y0;
			const Real mw = min( aw, ah * ratio ), mh = mw / ratio;
			const DirectStrikeRect map = DirectStrike_rect( area.x0 + ( aw - mw ) * 0.5f, area.y0 + ( ah - mh ) * 0.5f,
				area.x0 + ( aw + mw ) * 0.5f, area.y0 + ( ah + mh ) * 0.5f );
			// the preview is square, the map's longer side across it from its bottom left (generatePreview), black
			// past the shorter
			const Image *preview = getMapPreviewImage( s_game->getMap() );
			if( preview != NULL )
			{
				IRegion2D clip = regionOf( map );
				const Real side = max( mw, mh );
				const IRegion2D at = regionOf( DirectStrike_rect( map.x0, map.y1 - side, map.x0 + side, map.y1 ) );
				TheDisplay->setClipRegion( &clip );
				TheDisplay->drawImage( preview, at.lo.x, at.lo.y, at.hi.x, at.hi.y );
				TheDisplay->enableClipping( FALSE );
			}
			const Real perX = mw / ew, perY = mh / eh;
			Real startX[ DSLOBBY_SEATS ], startY[ DSLOBBY_SEATS ];
			Bool found = TRUE;
			for( Int at = 0; at < DSLOBBY_SEATS; ++at )
			{
				AsciiString waypoint;
				waypoint.format( "Player_%d_Start", at + 1 );
				WaypointMap::const_iterator it = md->m_waypoints.find( waypoint );
				found = found && it != md->m_waypoints.end();
				startX[ at ] = it != md->m_waypoints.end() ? map.x0 + ( it->second.x - md->m_extent.lo.x ) * perX : 0.0f;
				startY[ at ] = it != md->m_waypoints.end() ? map.y1 - ( it->second.y - md->m_extent.lo.y ) * perY : 0.0f;
			}
			if( found )
			{
				// the lane: from fortress to fortress on the middle line, 45 cells (450 world) across
				Real teamX[ DSLOBBY_TEAMS ] = { 0, 0 }, middleY = 0;
				for( Int at = 0; at < DSLOBBY_SEATS; ++at )
				{
					teamX[ DirectStrikeLobby_team( at ) ] += startX[ at ] / DSLOBBY_ROWS;
					middleY += startY[ at ] / DSLOBBY_SEATS;
				}
				const Real middleX = ( teamX[ 0 ] + teamX[ 1 ] ) * 0.5f;
				Real fortX[ DSLOBBY_TEAMS ];
				for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
					fortX[ team ] = middleX + ( teamX[ team ] - middleX ) * DirectStrikeLobby_fortressShare();
				const Real laneH = max( 12.0f, 450.0f * perY );
				const DirectStrikeRect lane = DirectStrike_rect( fortX[ 0 ], middleY - laneH * 0.5f, fortX[ 1 ], middleY + laneH * 0.5f );
				fill( lane, GameMakeColor( 255, 255, 255, 22 ) );
				fill( DirectStrike_rect( lane.x0, lane.y0, lane.x1, lane.y0 + 1 ), alpha( U::FRAME, 230 ) );
				fill( DirectStrike_rect( lane.x0, lane.y1 - 1, lane.x1, lane.y1 ), alpha( U::FRAME, 230 ) );
				for( Real y = lane.y0 - 14; y < lane.y1 + 14; y += 6 )
					fill( DirectStrike_rect( middleX, y, middleX + 1, y + 3 ), alpha( U::INK, 200 ) );
				const Real mark = max( 8.0f, min( 16.0f, mw / 40.0f ) );
				for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
				{
					diamond( fortX[ team ], middleY, mark, teamColor( team ) );
					const Real h = mark * 0.9f;
					chevrons( team == 0 ? fortX[ team ] + mark + 6 : fortX[ team ] - mark - 6 - ( h * 0.6f + h * 0.5f ), middleY, teamColor( team ), 2, team == 0, h );
				}
				// a plot: 24 cells along the lane by 32 across (RMG_LANE_PLOT_ALONG, _ACROSS)
				const Real plotW = max( 8.0f, 240.0f * perX ), plotH = max( 10.0f, 320.0f * perY );
				for( Int at = 0; at < DSLOBBY_SEATS; ++at )
				{
					const Int slot = s_slotAt[ at ];
					const GameSlot *s = seat( slot );
					if( s == NULL || s->getState() == SLOT_CLOSED )
						continue;
					const DirectStrikeRect plot = DirectStrike_rect( startX[ at ] - plotW * 0.5f, startY[ at ] - plotH * 0.5f,
						startX[ at ] + plotW * 0.5f, startY[ at ] + plotH * 0.5f );
					if( !s->isOccupied() )
					{
						box( plot, alpha( U::MUTED, 200 ) );
						continue;
					}
					const Bool random = s->getColor() < 0;
					swatchAt( plot, random ? UNPICKED : LobbySeatColor( s ), FALSE );
					if( slot == localSlot() )
					{
						box( grown( plot, 3 ), U::GOLD );
						box( grown( plot, 2 ), U::GOLD );
					}
				}
			}
		}
		box( island, alpha( U::FRAME, 220 ) );

		// the key: the fortress, a player's plot, an open plot, you
		fill( DirectStrike_rect( key.x0 + 1, key.y0, key.x1 - 1, key.y0 + 1 ), alpha( U::FRAME, 140 ) );
		fill( DirectStrike_rect( key.x0 + 1, key.y0 + 1, key.x1 - 1, key.y1 - 1 ), GameMakeColor( 0, 0, 0, 90 ) );
		const char *const labels[ 4 ] = { "GUI:DirectStrikeFortress", "GUI:DirectStrikePlot", "GUI:DirectStrikePlotOpen", "GUI:DirectStrikeYou" };
		Real x = key.x0 + 16;
		const Real cy = middleOf( key );
		for( Int i = 0; i < 4; ++i )
		{
			if( i == 0 )
				diamond( x + 8, cy, 8, TEAM_US, FALSE );
			else if( i == 1 )
				fill( DirectStrike_rect( x + 1, cy - 7, x + 15, cy + 7 ), GameMakeColor( 30, 160, 50, 255 ) );
			else if( i == 2 )
				box( DirectStrike_rect( x + 1, cy - 7, x + 15, cy + 7 ), alpha( U::MUTED, 220 ) );
			else
			{
				fill( DirectStrike_rect( x + 2, cy - 6, x + 14, cy + 6 ), GameMakeColor( 40, 80, 220, 255 ) );
				box( DirectStrike_rect( x - 1, cy - 9, x + 17, cy + 9 ), U::GOLD );
				box( DirectStrike_rect( x, cy - 8, x + 16, cy + 8 ), U::GOLD );
			}
			x += 26 + words( TheGameText->fetch( labels[ i ] ), TYPE_SMALL_PX, FALSE, x + 26, key.y0, key.y1, U::MUTED ) + 30;
		}
	}

	/// the island's name and the host's Mixed box at the caption's right end
	void drawCaption( void )
	{
		const Bool network = isNetwork();
		const DirectStrikeRect caption = DirectStrikeLobby_caption( network );
		words( TheGameText->fetch( "GUI:DirectStrikeMap" ), network ? TYPE_ARMY : TYPE_NAME_PX, FALSE, caption.x0, caption.y0, caption.y1, U::INK );
		const UnicodeString mixed = TheGameText->fetch( "GUI:DirectStrikeMixed" );
		const Real nw = wordsWidth( mixed, TYPE_BODY_PX, FALSE );
		const Real bx = caption.x1 - nw - 12 - 20, my = middleOf( caption );
		const Bool over = amHost() && hot( DirectStrikeLobby_mixed( network ) );
		const DirectStrikeRect check = DirectStrike_rect( bx, my - 10, bx + 20, my + 10 );
		fill( grown( check, -1 ), GameMakeColor( 0, 0, 0, 150 ) );
		box( check, over ? U::EDGE_LIT : amHost() ? alpha( U::EDGE, 230 ) : alpha( U::FRAME, 150 ) );
		if( s_game->getDirectStrikeMixed() )
		{
			const Real width = (Real)max( 2, REAL_TO_INT( 3.0f * s_frame.k + 0.5f ) );
			TheDisplay->drawLine( screenX( bx + 4 ), screenY( my ), screenX( bx + 8 ), screenY( my + 5 ), width, U::ACTIVE );
			TheDisplay->drawLine( screenX( bx + 8 ), screenY( my + 5 ), screenX( bx + 16 ), screenY( my - 6 ), width, U::ACTIVE );
		}
		words( mixed, TYPE_BODY_PX, FALSE, bx + 32, caption.y0, caption.y1, over ? U::INK : U::MUTED );
	}

	UnicodeString sizeLabel( Int first, Int second )
	{
		UnicodeString label;
		label.format( TheGameText->fetch( "GUI:DirectStrikeTeamSize" ), first, second );
		return label;
	}

	/// the room's own hidden speed box: every room's layout has one, its item data the percent
	GameWindow *speedBox( void ) { return named( "ComboBoxGameSpeed" ); }

	/// the speed the room will play: the box's pick, which the network rooms keep on the host's
	Int roomSpeed( void )
	{
		GameWindow *box = speedBox();
		Int selected = -1;
		if( box != NULL )
			GadgetComboBoxGetSelectedPos( box, &selected );
		if( selected < 0 )
			return s_game->getGameSpeedPercent();
		return ClampPlayerGameSpeedPercent( (Int)(intptr_t)GadgetComboBoxGetItemData( box, selected ) );
	}

	/// TEAMS and SPEED: the host's dropdowns, fields for everyone else
	void drawSetup( void )
	{
		const Bool network = isNetwork();
		for( Int i = 0; i < 2; ++i )
		{
			const DirectStrikeRect r = i == 0 ? DirectStrikeLobby_size( network ) : DirectStrikeLobby_speed( network );
			const DirectStrikeRect content = dropdown( r, amHost() ? chipState( r, i == 0 ? LIST_SIZE : LIST_SPEED, -1 ) : "", amHost() );
			const Real lw = caps( TheGameText->fetch( i == 0 ? "GUI:DirectStrikeTeamsLabel" : "GUI:DirectStrikeSpeedLabel" ), TYPE_CAPS, 1.6f,
				content.x0, content.y0, content.y1, U::MUTED );
			words( i == 0 ? sizeLabel( teamSize( 0 ), teamSize( 1 ) ) : GameSpeedText( roomSpeed() ), TYPE_NAME_PX, FALSE, content.x0 + lw + 16,
				content.y0, content.y1, U::INK );
		}
	}

	/// START (READY for a client), the room's one lit key: blue under the glass with four cyan edges and light from
	/// its foot; lit cyan with the word dark while held or for a ready client; dim while a human is not ready
	void drawStart( void )
	{
		const DirectStrikeRect r = DirectStrikeLobby_start( isNetwork() );
		const char *state = startState();
		if( *state == 'H' && held( r ) )
			state = "P";
		const UnicodeString label = TheGameText->fetch( isNetwork() && !amHost() ? "GUI:DirectStrikeReady" : "GUI:DirectStrikeStart" );
		const Real cx = ( r.x0 + r.x1 ) * 0.5f;
		fill( r, GameMakeColor( 0, 0, 0, 170 ) );
		if( *state == 'I' )
		{
			box( r, alpha( U::FRAME, 200 ) );
			caps( label, TYPE_START, 10, cx, r.y0, r.y1, DIM, 0 );
			return;
		}
		const Real line = 2;
		if( *state == 'P' )
		{
			fill( r, alpha( U::EDGE_LIT, 190 ) );
			litFill( DirectStrike_rect( r.x0, r.y0, r.x1, r.y0 + line ), U::EDGE_LIT );
			litFill( DirectStrike_rect( r.x0, r.y1 - line, r.x1, r.y1 ), U::EDGE_LIT );
			litFill( DirectStrike_rect( r.x0, r.y0, r.x0 + line, r.y1 ), U::EDGE_LIT );
			litFill( DirectStrike_rect( r.x1 - line, r.y0, r.x1, r.y1 ), U::EDGE_LIT );
			fill( grown( r, -4 ), GameMakeColor( 255, 255, 255, 36 ) );
			caps( label, TYPE_START, 10, cx, r.y0, r.y1, DARK, 0, FALSE );
			return;
		}
		fill( r, alpha( U::EDGE, 78 ) );
		// the cyan light rising from the foot, in bands
		enum { BANDS = 12 };
		for( Int i = 0; i < BANDS; ++i )
		{
			const Real t = (Real)( i + 1 ) / BANDS;
			const Real y0 = r.y0 + ( r.y1 - r.y0 ) * (Real)i / BANDS, y1 = r.y0 + ( r.y1 - r.y0 ) * (Real)( i + 1 ) / BANDS;
			fill( DirectStrike_rect( r.x0, y0, r.x1, y1 ), alpha( U::EDGE_LIT, REAL_TO_INT( powf( t, 1.3f ) * 42.0f ) ) );
		}
		if( *state == 'H' )
			fill( r, GameMakeColor( 255, 255, 255, 18 ) );
		litFill( DirectStrike_rect( r.x0, r.y0, r.x1, r.y0 + line ), U::EDGE_LIT );
		litFill( DirectStrike_rect( r.x0, r.y1 - line, r.x1, r.y1 ), U::EDGE_LIT );
		litFill( DirectStrike_rect( r.x0, r.y0, r.x0 + line, r.y1 ), U::EDGE_LIT );
		litFill( DirectStrike_rect( r.x1 - line, r.y0, r.x1, r.y1 ), U::EDGE_LIT );
		caps( label, TYPE_START, 10, cx, r.y0, r.y1, U::INK, 0 );
	}

	/// a chat string of the line `line`, wrapped at `wrap` pixels and clipped to the chat box
	DisplayString *chatString( DisplayString **strings, Int line, const UnicodeString &s, GameFont *f, IRegion2D *glass, Int wrap )
	{
		DisplayString *&string = strings[ line ];
		if( string == NULL )
			string = TheDisplayStringManager->newDisplayString();
		if( string->getFont() != f )
			string->setFont( f );
		string->setWordWrap( wrap );
		if( string->getText().compare( s ) != 0 )
			string->setText( s );
		string->setClipRegion( glass );
		return string;
	}

	/// where "] " closes the sender's name of a "[name] message" line, -1 for a line without one
	Int senderEnd( const UnicodeString &entry )
	{
		if( entry.getLength() == 0 || entry.getCharAt( 0 ) != u'[' )
			return -1;
		for( Int i = 1; i + 1 < entry.getLength(); ++i )
			if( entry.getCharAt( i ) == u']' && entry.getCharAt( i + 1 ) == u' ' )
				return i;
		return -1;
	}

	/// the log's entries, newest at the foot unless scrolled back: "[name] message" as the sender's colour in a
	/// stripe, the name white bold and the words beside it wrapped; anything else dim, wrapped to the box
	void drawChatLog( void )
	{
		const DirectStrikeRect chat = DirectStrikeLobby_chat();
		fill( chat, GameMakeColor( 0, 0, 0, 110 ) );
		box( chat, alpha( U::FRAME, 160 ) );
		GameWindow *log = named( ROOMS[ s_room ].chatLog );
		if( log == NULL )
			return;
		IRegion2D glass = regionOf( DirectStrike_rect( chat.x0 + 6, chat.y0 + 6, chat.x1 - 6, chat.y1 - 6 ) );
		GameFont *bold = menuFont( TYPE_SMALL_PX, TRUE ), *plain = menuFont( TYPE_SMALL_PX, FALSE );
		const Int stripeX = screenX( chat.x0 + 14 ), nameX = screenX( chat.x0 + 26 ), gap = DirectStrike_px( s_frame, 10 );
		const Int step = DirectStrike_px( s_frame, DSLOBBY_CHAT_LINE ), right = screenX( chat.x1 - 14 );
		const Int entries = GadgetListBoxGetNumEntries( log );
		s_chatScroll = max( 0, min( s_chatScroll, entries - 1 ) );
		Int bottom = screenY( chat.y1 - 16 );
		Int line = 0;
		for( Int row = entries - 1 - s_chatScroll; row >= 0 && line < CHAT_LINES && bottom > glass.lo.y; --row, ++line )
		{
			Color color = U::INK;
			const UnicodeString entry = GadgetListBoxGetTextAndColor( log, &color, row, 0 );
			const Int close = senderEnd( entry );
			Int nameW = 0, nameH = 0, textW = 0, textH = 0;
			UnicodeString message = entry;
			DisplayString *who = NULL;
			const Int left = close >= 0 ? nameX : stripeX;
			if( close >= 0 )
			{
				UnicodeString name;
				for( Int i = 1; i < close; ++i )
					name.concat( entry.getCharAt( i ) );
				message.set( entry.str() + close + 2 );
				who = chatString( s_chatNames, line, name, bold, &glass, 0 );
				who->getSize( &nameW, &nameH );
				nameW += gap;
			}
			DisplayString *what = chatString( s_chatTexts, line, message, plain, &glass, right - left - nameW );
			what->getSize( &textW, &textH );
			// one line takes `step`, every wrapped line under it the font's own height
			const Int lineH = plain->height;
			const Int top = bottom - textH - ( step - lineH ) / 2;
			if( who != NULL )
			{
				const Int stripeH = DirectStrike_px( s_frame, 16 );
				const Int stripeTop = top + ( lineH - stripeH ) / 2;
				if( tooDark( color ) )
					TheDisplay->drawOpenRect( stripeX - 1, stripeTop - 1, DirectStrike_px( s_frame, 4 ) + 2, stripeH + 2, 1.0f, LIGHT_KEY );
				TheDisplay->drawFillRect( stripeX, stripeTop, DirectStrike_px( s_frame, 4 ), stripeH, color );
				who->draw( left, top, U::INK, TEXT_SHADOW );
			}
			what->draw( left + nameW, top, who != NULL ? FIG : DIM, TEXT_SHADOW );
			bottom -= textH + step - lineH;
		}
	}

	void drawChatInput( void )
	{
		const DirectStrikeRect input = DirectStrikeLobby_chatInput();
		fill( input, GameMakeColor( 0, 0, 0, 150 ) );
		box( input, alpha( U::EDGE, 170 ) );
		const DirectStrikeRect send = DirectStrikeLobby_send();
		outlineKey( send, TheGameText->fetch( "GUI:DirectStrikeSend" ), TYPE_SEND, TRUE, hot( send ) );
		GameWindow *entry = named( CHAT_ENTRY );
		if( entry == NULL )
			return;
		placeEntry( entry );
		const UnicodeString typed = GadgetTextEntryGetText( entry );
		const Bool focused = TheWindowManager->winGetFocus() == entry;
		if( typed.isEmpty() && !focused )
		{
			words( TheGameText->fetch( "GUI:DirectStrikeTypeToChat" ), TYPE_SMALL_PX, FALSE, input.x0 + CONTENT_IN, input.y0, input.y1, DIM );
			return;
		}
		IRegion2D inside = regionOf( DirectStrike_rect( input.x0 + CONTENT_IN, input.y0, input.x1 - CONTENT_IN, input.y1 ) );
		if( s_inputText == NULL )
			s_inputText = TheDisplayStringManager->newDisplayString();
		GameFont *f = menuFont( TYPE_SMALL_PX, FALSE );
		if( s_inputText->getFont() != f )
			s_inputText->setFont( f );
		if( s_inputText->getText().compare( typed ) != 0 )
			s_inputText->setText( typed );
		s_inputText->setClipRegion( &inside );
		Int w = 0, h = 0;
		s_inputText->getSize( &w, &h );
		if( h == 0 )
			h = f->height;
		const Int top = ( inside.lo.y + inside.hi.y - h ) / 2;
		const Int caretW = DirectStrike_px( s_frame, 2 );
		// the field shows the end of a line too long for it
		const Int x = min( inside.lo.x, inside.hi.x - w - caretW );
		s_inputText->draw( x, top, U::INK, TEXT_SHADOW );
		if( focused )
			TheDisplay->drawFillRect( x + w + 1, top, caretW, h, U::INK );
	}

	/// the readiness lane: the track, frame A's scale under it, a fortress diamond at each end, a segment a seat in
	/// play filling from each fortress toward the white middle, lit in the team's colour while ready; in a network
	/// room what START waits for over the middle
	void drawLane( void )
	{
		const DirectStrikeRect lane = DirectStrikeLobby_lane(), track = DirectStrikeLobby_track();
		fill( DirectStrike_rect( lane.x0, lane.y0, lane.x1, lane.y0 + 1 ), alpha( U::FRAME, 110 ) );
		fill( track, GameMakeColor( 255, 255, 255, 8 ) );
		box( track, alpha( U::FRAME, 200 ) );
		Int i = 0;
		for( Real x = track.x0; x <= track.x1; x += 8, ++i )
		{
			const Bool longer = i % 5 == 0;
			fill( DirectStrike_rect( x, track.y1, x + 1, track.y1 + ( longer ? 8 : 3 ) ), alpha( U::FRAME, longer ? 200 : 140 ) );
		}
		const Real mid = ( track.x0 + track.x1 ) * 0.5f, cy = middleOf( track );
		for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
		{
			const Int size = teamSize( team ), ready = teamReady( team );
			for( Int seg = 0; seg < size; ++seg )
			{
				const DirectStrikeRect r = DirectStrikeLobby_laneSeg( team, seg, size );
				if( seg < ready )
					litFill( r, teamColor( team ) );
				else
					box( r, alpha( teamColor( team ), 110 ) );
			}
			diamond( team == 0 ? lane.x0 + 16 : lane.x1 - 16, cy, 14, teamColor( team ) );
		}
		fill( DirectStrike_rect( mid - 1, track.y0 - 4, mid + 1, track.y1 + 10 ), U::INK );
		if( !isNetwork() )
			return;
		const Int waiting = humansWaiting();
		UnicodeString label;
		if( waiting == 0 )
			label = TheGameText->fetch( "GUI:DirectStrikeAllReady" );
		else if( waiting == 1 )
			label = TheGameText->fetch( "GUI:DirectStrikeWaitingForOne" );
		else
			label.format( TheGameText->fetch( "GUI:DirectStrikeWaitingFor" ), waiting );
		const DirectStrikeRect status = DirectStrikeLobby_status();
		caps( label, TYPE_CAPS, 2.0f, mid, status.y0, status.y1, waiting == 0 ? U::ACTIVE : AMBER, 0 );
	}

	//-----------------------------------------------------------------------------------------------
	Int seatListRows( Int slot ) { return seat( slot )->isHuman() ? DSLOBBY_HUMAN_SEAT_ROWS : DSLOBBY_SEAT_ROWS; }

	void drawSeatList( Int slot )
	{
		const Int rows = seatListRows( slot );
		const DirectStrikeRect list = DirectStrikeLobby_seatList( seatOfSlot( slot ), rows );
		listPanel( list );
		const Int current = SlotPickOf( seat( slot ) );
		for( Int row = 0; row < rows; ++row )
			listRow( DirectStrikeLobby_listRow( list, row ), seatRowLabel( row ), seatPick( row ) == current, TYPE_BODY_PX, row >= SEAT_ROW_FIRST_AI );
	}

	void drawSizeList( void )
	{
		const DirectStrikeRect list = DirectStrikeLobby_sizeList( isNetwork() );
		listPanel( list );
		for( Int row = 0; row < DSLOBBY_SIZES; ++row )
			listRow( DirectStrikeLobby_listRow( list, row ), sizeLabel( row + 1, row + 1 ), teamSize( 0 ) == row + 1 && teamSize( 1 ) == row + 1,
				TYPE_NAME_PX, FALSE );
	}

	void drawSpeedList( void )
	{
		const DirectStrikeRect list = DirectStrikeLobby_speedList( isNetwork() );
		listPanel( list );
		for( Int row = 0; row < DSLOBBY_SPEEDS; ++row )
			listRow( DirectStrikeLobby_listRow( list, row ), GameSpeedText( PLAYER_GAME_SPEED_PERCENTS[ row ] ),
				roomSpeed() == PLAYER_GAME_SPEED_PERCENTS[ row ], TYPE_NAME_PX, FALSE );
	}

	/// the army picker in its team's wing: Random, then a column a side (the side, then its three generals), the
	/// current one in blue with a lit edge and a tick on its picture, the pointer's tile lifted cyan
	void drawPicker( Int slot )
	{
		const Int at = seatOfSlot( slot );
		listPanel( DirectStrikeLobby_picker( at ) );
		const Int pick = seat( slot )->getPlayerTemplate();

		const DirectStrikeRect random = DirectStrikeLobby_pickerRandom( at );
		const Bool randomPicked = isRandom( pick ), randomOver = pointerIn( random );
		const DirectStrikeRect content = dropdown( random, "", FALSE );
		if( randomOver )
		{
			fill( random, GameMakeColor( 255, 255, 255, 26 ) );
			lit( random, U::EDGE_LIT );
		}
		else if( randomPicked )
			fill( random, alpha( U::EDGE, 70 ) );
		const Real cy = middleOf( random );
		swatchAt( DirectStrike_rect( content.x0, cy - 12, content.x0 + 24, cy + 12 ), 0, TRUE );
		words( TheGameText->fetch( "GUI:Random" ), TYPE_ARMY, FALSE, content.x0 + 36, random.y0, random.y1, U::INK );
		if( randomPicked )
			tick( random.x1 - 12, cy, U::EDGE_LIT );

		Int pickColumn = -1, pickTile = -1;
		armyCell( pick, &pickColumn, &pickTile );
		for( Int column = 0; column < DSLOBBY_ARMY_COLUMNS; ++column )
			for( Int tile = 0; tile < DSLOBBY_ARMY_TILES; ++tile )
			{
				const DirectStrikeRect t = DirectStrikeLobby_pickerTile( at, column, tile );
				const Bool current = column == pickColumn && tile == pickTile;
				const Bool over = !current && pointerIn( t );
				fill( t, GameMakeColor( 255, 255, 255, 8 ) );
				if( current )
				{
					fill( t, alpha( U::EDGE, 110 ) );
					lit( t, U::EDGE_LIT );
				}
				else if( over )
				{
					fill( t, GameMakeColor( 255, 255, 255, 26 ) );
					lit( t, U::EDGE_LIT );
				}
				else
					box( t, alpha( U::FRAME, 170 ) );
				const Real px0 = ( t.x0 + t.x1 - DSLOBBY_TILE_PICTURE ) * 0.5f;
				const DirectStrikeRect picture = DirectStrike_rect( px0, t.y0 + 8, px0 + DSLOBBY_TILE_PICTURE, t.y0 + 8 + DSLOBBY_TILE_PICTURE );
				const Int num = ThePlayerTemplateStore->getTemplateNumByName( ARMIES[ column ][ tile ] );
				const Image *image = num >= 0 ? U::armyPortrait( ThePlayerTemplateStore->getNthPlayerTemplate( num ) ) : NULL;
				const IRegion2D g = regionOf( picture );
				if( image != NULL )
					U::coverImage( image, g.lo.x, g.lo.y, g.hi.x - g.lo.x, g.hi.y - g.lo.y );
				box( grown( picture, 1 ), alpha( U::FRAME, 200 ) );
				const UnicodeString name = TheGameText->fetch( ARMY_LABELS[ column ][ tile ] );
				if( tile == 0 )
					caps( name, TYPE_TILE_SIDE, 1.4f, ( t.x0 + t.x1 ) * 0.5f, picture.y1 + 4, t.y1 - 2, U::INK, 0 );
				else
					words( name, TYPE_SMALL_PX, FALSE, ( t.x0 + t.x1 ) * 0.5f, picture.y1 + 4, t.y1 - 2, U::INK, 0 );
				if( current )
				{
					const DirectStrikeRect mark = DirectStrike_rect( picture.x1 - 22, picture.y0, picture.x1, picture.y0 + 22 );
					fill( mark, U::EDGE_LIT );
					const Real width = (Real)max( 2, REAL_TO_INT( 3.0f * s_frame.k + 0.5f ) );
					const Color ink = GameMakeColor( 8, 12, 24, 255 );
					TheDisplay->drawLine( screenX( mark.x0 + 5 ), screenY( mark.y0 + 11 ), screenX( mark.x0 + 10 ), screenY( mark.y0 + 16 ), width, ink );
					TheDisplay->drawLine( screenX( mark.x0 + 10 ), screenY( mark.y0 + 16 ), screenX( mark.x0 + 18 ), screenY( mark.y0 + 6 ), width, ink );
				}
			}
	}

	/// the colour list: only the colours no other seat holds, then Random; the hovered colour's name under the cells
	void drawColorList( Int slot )
	{
		Int colors[ COLOR_LIMIT ];
		const Int count = freeColors( slot, colors );
		colors[ count ] = -1;	// Random
		const Int at = seatOfSlot( slot );
		const DirectStrikeRect list = DirectStrikeLobby_colorList( at, count + 1 );
		listPanel( list );
		const Int mine = seat( slot )->getColor();
		Int shown = mine;
		for( Int i = 0; i <= count; ++i )
		{
			const DirectStrikeRect cell = DirectStrikeLobby_colorCell( at, count + 1, i );
			const Bool current = colors[ i ] == mine, over = pointerIn( cell );
			if( over )
			{
				fill( cell, GameMakeColor( 255, 255, 255, 26 ) );
				lit( cell, U::EDGE_LIT );
				shown = colors[ i ];
			}
			else if( current )
				fill( cell, alpha( U::EDGE, 70 ) );
			else
				box( cell, alpha( U::FRAME, 140 ) );
			const Real cy = middleOf( cell );
			const Bool random = colors[ i ] < 0;
			swatchAt( DirectStrike_rect( cell.x0 + 12, cy - 13, cell.x1 - 12, cy + 13 ),
				random ? UNPICKED : TheMultiplayerSettings->getColor( colors[ i ] )->getColor(), random );
			if( current )
				tick( cell.x1 - 2, cell.y0 + 12, U::EDGE_LIT );
		}
		words( colorName( shown ), TYPE_SMALL_PX, FALSE, list.x0 + DSLOBBY_COLOR_PAD, list.y1 - 34, list.y1 - 8, U::INK );
	}

	//-----------------------------------------------------------------------------------------------
	void lobbyDraw( GameWindow *window, WinInstanceData *instData )
	{
		s_game = liveGame();
		if( s_game == NULL )
		{
			hideRoom( window, NULL );	// the game went before the detach: the room fades out bare, as after it
			return;
		}
		s_frame = DirectStrike_frame( (Int)TheDisplay->getWidth(), (Int)TheDisplay->getHeight() );
		begin( SIDE_COMMON, s_frame.k );
		placeSeats();
		hideRoom( window, isNetwork() ? named( CHAT_ENTRY ) : NULL );

		const IRegion2D panel = regionOf( DirectStrikeLobby_panel() );
		U::glass( panel );
		U::frameA( panel );
		drawHeader();
		for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
			drawHead( team );
		for( Int at = 0; at < DSLOBBY_SEATS; ++at )
			drawSeat( at );
		drawIsland();
		drawCaption();
		if( isNetwork() )
		{
			drawChatLog();
			drawChatInput();
		}
		drawSetup();
		drawStart();
		drawLane();

		if( s_list == LIST_SEAT )
			drawSeatList( s_listSlot );
		else if( s_list == LIST_ARMY )
			drawPicker( s_listSlot );
		else if( s_list == LIST_COLOR )
			drawColorList( s_listSlot );
		else if( s_list == LIST_SIZE )
			drawSizeList();
		else if( s_list == LIST_SPEED )
			drawSpeedList();
	}

	//-----------------------------------------------------------------------------------------------
	/// the hidden box's line whose item data is `data`, chosen as a click on it would: the room's handler hears it
	void choose( GameWindow *combo, Int data )
	{
		for( Int i = 0; i < GadgetComboBoxGetLength( combo ); ++i )
			if( (Int)(intptr_t)GadgetComboBoxGetItemData( combo, i ) == data )
			{
				GadgetComboBoxSetSelectedPos( combo, i );
				return;
			}
		DEBUG_LOG(( "DIRECTSTRIKE LOBBY no line with data %d in window %d\n", data, combo->winGetWindowId() ));
	}

	/// a team size of `size` a side: rows past it close and rows within it open, on both teams, through each seat's
	/// own box so a network room sends it as a seat change; a seat inside the size keeps whoever holds it, and a
	/// human outside it is left seated, since closing them would be a kick
	void chooseSize( Int size )
	{
		// by seat row, the slot each seat shows read again after every change: an AI closed off a seat it had moved
		// to may show elsewhere, so a second pass settles what the first one moved
		for( Int pass = 0; pass < 2; ++pass )
			for( Int at = 0; at < DSLOBBY_SEATS; ++at )
			{
				placeSeats();
				const Int slot = s_slotAt[ at ];
				const GameSlot *s = seat( slot );
				GameWindow *combo = slot != localSlot() && s != NULL ? named( "ComboBoxPlayer", slot ) : NULL;
				if( combo == NULL )
					continue;
				const Bool within = DirectStrikeLobby_row( at ) < size;
				if( !within && !s->isHuman() && s->getState() != SLOT_CLOSED )
					choose( combo, SLOT_CLOSED );
				else if( within && s->getState() == SLOT_CLOSED )
					choose( combo, SLOT_OPEN );
				// the room heard that change at once and may have left the lobby
				if( s_game == NULL )
					return;
			}
	}

	void press( const char *button, Int index = -1 )
	{
		GameWindow *window = named( button, index );
		if( window != NULL )
			TheWindowManager->winSendSystemMsg( s_parent, GBM_SELECTED, (WindowMsgData)window, window->winGetWindowId() );
	}

	/// the local player to seat `at`: the room's own start spot for that seat, pressed as a click on the map would.
	/// The host and a skirmish take the start at once and send the slot list; a joiner asks the host for it.
	void moveTo( Int at )
	{
		DEBUG_LOG(( "DIRECTSTRIKE LOBBY move slot %d from seat %d to seat %d\n", localSlot(), seatOfSlot( localSlot() ), at ));
		press( "ButtonMapStartPosition", at );
	}

	/// the host ticks Mixed: the room's game takes it and a network room sends its options, as its own boxes do
	void toggleMixed( void )
	{
		GameInfo *game = const_cast<GameInfo *>( s_game );
		game->setDirectStrikeMixed( !game->getDirectStrikeMixed() );
		DEBUG_LOG(( "DIRECTSTRIKE LOBBY mixed %d\n", (Int)game->getDirectStrikeMixed() ));
		if( !isNetwork() )
			return;
		game->resetAccepted();
		if( s_room == ROOM_LAN )
		{
			TheLAN->RequestGameOptions( GenerateGameOptionsString(), true );
			lanUpdateSlotList();
		}
		else if( TheGameSpyInfo != NULL )
		{
			TheGameSpyInfo->setGameOptions();
			WOLDisplaySlotList();
		}
	}

	void sendChat( void )
	{
		GameWindow *entry = named( CHAT_ENTRY );
		if( entry != NULL )
			TheWindowManager->winSendSystemMsg( s_parent, GEM_EDIT_DONE, (WindowMsgData)entry, 0 );
	}

	/// every choose(), press() and sendChat() below is a synchronous message to the room, whose handler can leave
	/// the lobby and detach it (s_game NULL); each is therefore the last thing its click does
	void pickFromList( List list, Int slot, Real x, Real y )
	{
		const Bool network = isNetwork();
		if( list == LIST_SPEED )
		{
			// the hidden box's own pick, so the room's handler sets the game and sends the options
			GameWindow *box = speedBox();
			for( Int row = 0; row < DSLOBBY_SPEEDS && box != NULL; ++row )
				if( DirectStrikeLobby_listRow( DirectStrikeLobby_speedList( network ), row ).contains( x, y ) )
				{
					choose( box, PLAYER_GAME_SPEED_PERCENTS[ row ] );
					return;
				}
			return;
		}
		if( list == LIST_SIZE )
		{
			for( Int row = 0; row < DSLOBBY_SIZES; ++row )
				if( DirectStrikeLobby_listRow( DirectStrikeLobby_sizeList( network ), row ).contains( x, y ) )
				{
					chooseSize( row + 1 );
					return;
				}
			return;
		}
		if( list == LIST_SEAT )
		{
			const Int rows = seatListRows( slot );
			const DirectStrikeRect seats = DirectStrikeLobby_seatList( seatOfSlot( slot ), rows );
			for( Int row = 0; row < rows; ++row )
				if( DirectStrikeLobby_listRow( seats, row ).contains( x, y ) )
				{
					if( GameWindow *combo = named( "ComboBoxPlayer", slot ) )
						choose( combo, seatPick( row ) );
					return;
				}
			return;
		}
		if( list == LIST_ARMY )
		{
			GameWindow *combo = named( "ComboBoxPlayerTemplate", slot );
			if( combo == NULL )
				return;
			if( DirectStrikeLobby_pickerRandom( seatOfSlot( slot ) ).contains( x, y ) )
			{
				choose( combo, PLAYERTEMPLATE_RANDOM );
				return;
			}
			for( Int column = 0; column < DSLOBBY_ARMY_COLUMNS; ++column )
				for( Int tile = 0; tile < DSLOBBY_ARMY_TILES; ++tile )
					if( DirectStrikeLobby_pickerTile( seatOfSlot( slot ), column, tile ).contains( x, y ) )
					{
						choose( combo, ThePlayerTemplateStore->getTemplateNumByName( ARMIES[ column ][ tile ] ) );
						return;
					}
			return;
		}
		Int colors[ COLOR_LIMIT ];
		const Int count = freeColors( slot, colors );
		colors[ count ] = -1;	// Random
		GameWindow *combo = named( "ComboBoxColor", slot );
		for( Int i = 0; i <= count && combo != NULL; ++i )
			if( DirectStrikeLobby_colorCell( seatOfSlot( slot ), count + 1, i ).contains( x, y ) )
			{
				choose( combo, colors[ i ] );
				return;
			}
	}

	/// a press at a screen pixel, the message's own (a scripted click does not move the cursor)
	void click( Int screenX, Int screenY )
	{
		Real x, y;
		DirectStrike_toDesign( s_frame, screenX, screenY, &x, &y );
		placeSeats();
		const Bool network = isNetwork();
		const List open = s_list;
		const Int openSlot = s_listSlot;
		s_list = LIST_NONE;
		s_listSlot = -1;
		// a click with a list open is that list's, or closes it
		if( open != LIST_NONE )
		{
			pickFromList( open, openSlot, x, y );
			return;
		}
		// Back pops the shell and detaches this lobby before press() returns: a key press is never also a seat pick
		if( DirectStrikeLobby_back().contains( x, y ) )
		{
			press( "ButtonBack" );
			return;
		}
		if( DirectStrikeLobby_start( network ).contains( x, y ) && *startState() != 'I' )
		{
			press( "ButtonStart" );
			return;
		}
		if( network && DirectStrikeLobby_send().contains( x, y ) )
		{
			sendChat();
			return;
		}
		if( amHost() && DirectStrikeLobby_size( network ).contains( x, y ) )
		{
			s_list = LIST_SIZE;
			return;
		}
		if( amHost() && DirectStrikeLobby_speed( network ).contains( x, y ) )
		{
			s_list = LIST_SPEED;
			return;
		}
		if( amHost() && DirectStrikeLobby_mixed( network ).contains( x, y ) )
		{
			toggleMixed();
			return;
		}
		for( Int slot = 0; slot < DSLOBBY_SEATS; ++slot )
		{
			if( seat( slot ) == NULL )
				continue;
			List list = LIST_NONE;
			if( editsSeat( slot ) && seatPart( slot, DSSEAT_LINE ).contains( x, y ) )
				list = LIST_SEAT;
			else if( editsArmy( slot ) && seatPart( slot, DSSEAT_ARMY ).contains( x, y ) )
				list = LIST_ARMY;
			else if( editsArmy( slot ) && seatPart( slot, DSSEAT_COLOR ).contains( x, y ) )
				list = LIST_COLOR;
			if( list != LIST_NONE )
			{
				s_list = list;
				s_listSlot = slot;
				return;
			}
		}
		// the host's level keys on an open or closed seat make it a computer of that level, through its own box
		for( Int at = 0; at < DSLOBBY_SEATS; ++at )
		{
			const Int level = levelAt( at, x, y );
			if( level >= 0 )
			{
				if( GameWindow *combo = named( "ComboBoxPlayer", s_slotAt[ at ] ) )
					choose( combo, seatPick( SEAT_ROW_FIRST_AI + level ) );
				return;
			}
		}
		// an open seat's card takes the player there, and so does the other team's head, to its first open seat
		for( Int at = 0; at < DSLOBBY_SEATS; ++at )
			if( overMove( at, x, y ) )
			{
				moveTo( at );
				return;
			}
		for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
			if( DirectStrikeLobby_teamHead( team ).contains( x, y ) && headMove( team ) >= 0 )
			{
				moveTo( headMove( team ) );
				return;
			}
	}

	/// every seat's pick and the open list, one line, so a report's log tail says what the room held
	void logSeats( const char *why )
	{
		AsciiString line;
		placeSeats();
		for( Int slot = 0; slot < DSLOBBY_SEATS; ++slot )
		{
			const GameSlot *s = seat( slot );
			AsciiString one;
			if( s != NULL )
				one.format( " %d=%d/%d/%d@%d", slot, SlotPickOf( s ), s->getPlayerTemplate(), s->getColor(), s_seatOf[ slot ] );
			else
				one.format( " %d=none", slot );
			line.concat( one );
		}
		DEBUG_LOG(( "DIRECTSTRIKE LOBBY %s seats%s list=%d/%d speed=%d\n", why, line.str(), (Int)s_list, s_listSlot, roomSpeed() ));
	}

	WindowMsgHandledType lobbyInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
	{
		s_game = liveGame();
		if( msg == GWM_LEFT_DOWN && s_game != NULL )
		{
			// SHORTTOLONG's halves
			click( (Short)( mData1 & 0xFFFF ), (Short)( ( mData1 >> 16 ) & 0xFFFF ) );
			if( s_game != NULL )
				logSeats( "click" );
			return MSG_HANDLED;
		}
		if( msg == GWM_LEFT_UP )
			return MSG_HANDLED;
		// Esc closes an open list first, on its release as the rooms' own Esc goes; the next Esc is the room's Back
		if( msg == GWM_CHAR && (UnsignedByte)mData1 == KEY_ESC && s_list != LIST_NONE )
		{
			if( BitTest( mData2, KEY_STATE_UP ) )
			{
				s_list = LIST_NONE;
				s_listSlot = -1;
			}
			return MSG_HANDLED;
		}
		if( ( msg == GWM_WHEEL_UP || msg == GWM_WHEEL_DOWN ) && s_game != NULL && isNetwork() )
		{
			Real x, y;
			DirectStrike_toDesign( s_frame, (Short)( mData1 & 0xFFFF ), (Short)( ( mData1 >> 16 ) & 0xFFFF ), &x, &y );
			if( DirectStrikeLobby_chat().contains( x, y ) )
				s_chatScroll += msg == GWM_WHEEL_UP ? 1 : -1;
			return MSG_HANDLED;
		}
		return s_roomInput != NULL ? s_roomInput( window, msg, mData1, mData2 ) : MSG_IGNORED;
	}
}

//-------------------------------------------------------------------------------------------------
void DirectStrikeLobby_attach( GameWindow *parent, const char *layoutFilename, const GameInfo *game )
{
	if( parent == NULL || game == NULL )
		return;
	Int room = 0;
	while( room < ROOM_COUNT && strcmp( ROOMS[ room ].layout, layoutFilename ) != 0 )
		++room;
	DEBUG_ASSERTCRASH( room < ROOM_COUNT, ( "DirectStrikeLobby_attach: no room is laid out by '%s'", layoutFilename ) );
	if( room == ROOM_COUNT )
		return;
	s_game = game;
	if( s_parent == parent )
		return;
	DirectStrikeLobby_detach();
	if( s_fading == parent )
		DirectStrikeLobby_restoreRoom();	// its own draw, never the fade's, is the one to keep
	s_game = game;
	s_parent = parent;
	s_room = (Room)room;
	s_roomDraw = parent->winGetDrawFunc();
	s_roomInput = parent->winGetInputFunc();
	hookRoom( parent );
	parent->winSetDrawFunc( lobbyDraw );
	parent->winSetInputFunc( lobbyInput );
	parent->winClearStatus( WIN_STATUS_NO_INPUT );
	DEBUG_LOG(( "DIRECTSTRIKE LOBBY attach %s local=%d host=%d\n", layoutFilename, localSlot(), (Int)amHost() ));
	logSeats( "attach" );
}

/** The room's windows back as the lobby found them, so a later show of the layout is the stock room and
	* never a bare frame.  Called when a room opens, not at the detach, which runs as the room starts to fade
	* out: the stock gadgets would flash up over the fade. */
void DirectStrikeLobby_restoreRoom( void )
{
	if( s_fading != NULL && s_fading->winGetDrawFunc() == fadingDraw )
		s_fading->winSetDrawFunc( s_fadingDraw );
	s_fading = NULL;
	s_fadingDraw = NULL;
	// each window found again by id under its room, which roomSystem guarantees is still alive; one that
	// went with its gadget since is skipped
	for( size_t i = 0; i < s_blanked.size(); ++i )
	{
		GameWindow *window = inRoom( s_blanked[ i ].root, s_blanked[ i ].id );
		if( window != NULL && window->winGetDrawFunc() == drawNothing )
			window->winSetDrawFunc( s_blanked[ i ].draw );
	}
	for( size_t i = 0; i < s_hidden.size(); ++i )
	{
		GameWindow *window = inRoom( s_hidden[ i ].root, s_hidden[ i ].id );
		if( window != NULL )
			window->winHide( FALSE );
	}
	s_blanked.clear();
	s_hidden.clear();
}

void DirectStrikeLobby_detach( void )
{
	if( s_parent != NULL )
	{
		// the room's windows stay hidden through its fade-out; DirectStrikeLobby_restoreRoom gives them back
		s_fading = s_parent;
		s_fadingDraw = s_roomDraw;
		s_parent->winSetDrawFunc( fadingDraw );
		s_parent->winSetInputFunc( s_roomInput );
	}
	for( Int i = 0; i < CHAT_LINES; ++i )
	{
		if( s_chatNames[ i ] != NULL )
			TheDisplayStringManager->freeDisplayString( s_chatNames[ i ] );
		if( s_chatTexts[ i ] != NULL )
			TheDisplayStringManager->freeDisplayString( s_chatTexts[ i ] );
		s_chatNames[ i ] = s_chatTexts[ i ] = NULL;
	}
	if( s_inputText != NULL )
		TheDisplayStringManager->freeDisplayString( s_inputText );
	s_inputText = NULL;
	for( size_t i = 0; i < s_letters.size(); ++i )
		TheDisplayStringManager->freeDisplayString( s_letters[ i ].s );
	s_letters.clear();
	s_chatScroll = 0;
	s_parent = NULL;
	s_game = NULL;
	s_roomDraw = NULL;
	s_roomInput = NULL;
	s_list = LIST_NONE;
	s_listSlot = -1;
}
