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
// The Direct Strike room, DirectStrikeLobby.h.  Every rect is the Spearhead console's at 1920x1080, in
// DirectStrikeLayout.h, put on the screen by DirectStrike_frame; every piece and colour is the theme's,
// on the common frame every player of the room sees alike (SIDE_COMMON).
//
// A seat is a start, not a slot (DirectStrikeLayout.h): a player moves by clicking an open seat or the
// other team's head, which presses the room's own hidden start spot for that seat, so the move takes
// the path a start spot click always took: the host and a skirmish set the start, a joiner asks the
// host for it ("StartPos=" in LANAPI's and the online room's requests), the host refuses a seat that is
// not open (DirectStrikeSeatIsOpen), resets everyone's ready and sends the slot list, and
// FitDirectStrikeRoom gives the team that goes with the seat.
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
#include "GameClient/Mouse.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/Online/OnlineQueues.h"

#include <algorithm>
#include <vector>

using namespace DirectStrikeTheme;

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

	/// the picker's columns, README-lobby.md "Army picker": the side first, then its generals; Lane War has no Turkiye
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

	/// the seat control's rows, in README-lobby.md's order
	const char *const SEAT_LABELS[ DSLOBBY_SEAT_ROWS ] =
	{
		"GUI:Open", "GUI:Closed", "GUI:DirectStrikeEasy", "GUI:DirectStrikeMedium", "GUI:DirectStrikeHard", "GUI:DirectStrikeBrutal",
	};
	enum { SEAT_ROW_FIRST_AI = 2 };

	// the text in the theme's type roles; README-lobby.md's insets, in 1080 pixels
	const Real TITLE_EM = TYPE_TITLE, TITLE_SPACING = TYPE_TITLE * TRACK_TITLE;
	const Real TEAM_EM = TYPE_LABEL, TEAM_SPACING = TYPE_LABEL * TRACK_TEAM, TEAM_INSET = 20;
	const Real NAME_EM = TYPE_NAME, CHIP_EM = TYPE_LABEL, CHAT_EM = TYPE_BODY, TILE_EM = TYPE_BODY;
	const Real START_EM = TYPE_FIGURE, START_SPACING = TYPE_FIGURE * TRACK_KEY;
	const Real CONTENT_INSET = 16, ICON = 28, ICON_GAP = 10, CHAT_NAME_GAP = 8;
	const Real CHAT_TEXT_IN = 6, CHAT_FOOT = 5;		///< the log's text from the glass's left, its newest line's foot above the glass's
	const Real CHAT_INPUT_IN = 18;								///< the typed text and its caret from the input's left
	const Real SWATCH_W = 36, SWATCH_H = 24, CHECK = 20, CHECK_INSET = 36;
	const Real ISLAND_EM = TYPE_NAME, ISLAND_NOTE_EM = TYPE_BODY, ISLAND_BAND_EM = TYPE_LABEL, ISLAND_BAND_NOTE_EM = TYPE_BODY;
	const Real ISLAND_NAME_BELOW = 20, ISLAND_NOTE_BELOW = 46;		///< the caption's middles under the island picture
	const Real ISLAND_BAND_NAME_X = 800, ISLAND_BAND_NOTE_X = 1204;	///< the map band's name from here, its note ending here
	const Real ISLAND_WELL_OUT = 4;			///< the offline island's well round the picture
	const Real PLUS_IN = 28;						///< an open seat's + key in its portrait well
	const Real HEAD_GLASS = 5;					///< a team head's glass, where its hover band lies
	const Real VERSION_X = 1896, VERSION_Y = 904, VERSION_EM = TYPE_SMALL;	///< the version tag under the console, right-aligned
	const Real GOLD_LINE_X0 = 958, GOLD_LINE_Y0 = 830, GOLD_LINE_X1 = 962, GOLD_LINE_Y1 = 872;	///< the tug bar's middle
	const Real LIST_TEXT = 20, LIST_AI_TEXT = 58;
	const Real TILE_PICTURE_X = 32, TILE_PICTURE_Y = 6, TILE_PICTURE = 72;	///< the 64 picture with the well's 4 round it
	const Real TILE_NAME_X = 68, TILE_NAME_Y = 92;
	const Real CARET_W = 2;
	const Color CLOSED_TINT = GameMakeColor( 255, 255, 255, 178 );	///< a closed card at alpha 0.7
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

	/// the chat's strings, each clipped to the panel's glass, kept apart from the theme's pool
	DisplayString *s_chatNames[ CHAT_LINES ];
	DisplayString *s_chatTexts[ CHAT_LINES ];
	DisplayString *s_inputText = NULL;
	Int s_chatScroll = 0;	///< entries the log is scrolled back from the newest

	/// what hideRoom changed in the room, so the detach gives the room back as it was found
	struct Drawn
	{
		GameWindow *window;
		GameWinDrawFunc draw;
	};
	std::vector<GameWindow *> s_hidden;
	std::vector<Drawn> s_blanked;

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
	DirectStrikeRect seatPart( Int slot, DirectStrikeSeatPart part ) { return DirectStrikeLobby_part( seatOfSlot( slot ), part, isNetwork() ); }
	Color teamColor( Int team ) { return team == 0 ? TEAM_US : TEAM_THEM; }

	//-----------------------------------------------------------------------------------------------
	Int screenX( Real x ) { return s_frame.ox + REAL_TO_INT( x * s_frame.k + 0.5f ); }
	Int screenY( Real y ) { return s_frame.oy + REAL_TO_INT( y * s_frame.k + 0.5f ); }
	LaneHudBox boxOf( const DirectStrikeRect &r ) { return DirectStrike_box( s_frame, r ); }
	Real middleOf( const DirectStrikeRect &r ) { return ( r.y0 + r.y1 ) * 0.5f; }

	/// a square of `size` whose left is x, centred down r
	LaneHudBox iconAt( Real x, const DirectStrikeRect &r, Real w, Real h )
	{
		const Real y = middleOf( r ) - h * 0.5f;
		return boxOf( DirectStrike_rect( x, y, x + w, y + h ) );
	}

	Bool pointerIn( const DirectStrikeRect &r )
	{
		const MouseIO *mouse = TheMouse->getMouseStatus();
		Real x, y;
		DirectStrike_toDesign( s_frame, mouse->pos.x, mouse->pos.y, &x, &y );
		return r.contains( x, y );
	}

	/// the left button held down on r: a key shows pressed (P) until it is let go
	Bool held( const DirectStrikeRect &r ) { return pointerIn( r ) && TheMouse->getMouseStatus()->leftState != MBS_Up; }

	/// a Riveted Clamp key: "" up, H under the pointer, P while the button is held on it, I when it does nothing
	void clampKey( const DirectStrikeRect &r, Piece piece, Bool disabled = FALSE )
	{
		key( boxOf( r ), piece, !disabled && held( r ), disabled );
	}

	/// text of em pixels at 1080, its band centred on y; its width
	Int text( const UnicodeString &s, Real em, Real x, Real y, Color color, Int align = -1, Bool shadow = TRUE )
	{
		const Int h = px( em * 2 );
		return drawText( s, font( em ), screenX( x ), screenY( y ) - h / 2, h, color, align, shadow );
	}

	/// a chip's state: P while its list is open, H under the pointer while no list is
	const char *chipState( const DirectStrikeRect &r, List list, Int slot )
	{
		if( s_list == list && s_listSlot == slot )
			return "P";
		return s_list == LIST_NONE && pointerIn( r ) ? "H" : "";
	}

	Color faceText( const char *state ) { return *state == 'P' ? TEXT_DARK : TEXT_WHITE; }
	Bool faceShadow( const char *state ) { return *state != 'P'; }

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

	Side armySide( Int pick )
	{
		const PlayerTemplate *army = pick >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( pick ) : NULL;
		return army != NULL ? sideOf( army->getSide() ) : SIDE_AMERICA;
	}

	Bool isRandom( Int pick ) { return pick < 0 || ThePlayerTemplateStore->getNthPlayerTemplate( pick ) == NULL; }

	UnicodeString armyName( Int pick )
	{
		Int column, tile;
		if( armyCell( pick, &column, &tile ) )
			return TheGameText->fetch( ARMY_LABELS[ column ][ tile ] );
		if( isRandom( pick ) )
			return TheGameText->fetch( "GUI:Random" );
		AsciiString side;
		side.format( "SIDE:%s", ThePlayerTemplateStore->getNthPlayerTemplate( pick )->getSide().str() );
		return TheGameText->fetch( side );
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

	UnicodeString seatLabel( const GameSlot *s )
	{
		const Int pick = SlotPickOf( s );
		for( Int row = 0; row < DSLOBBY_SEAT_ROWS; ++row )
			if( seatPick( row ) == pick )
				return TheGameText->fetch( SEAT_LABELS[ row ] );
		return SlotStateName( s->getState(), s->getAIRating() );
	}

	/// the colours a seat may take: every colour no other seat holds, in the settings' order
	Int freeColors( Int slot, Int *colors )
	{
		Int count = 0;
		for( Int c = 0; c < TheMultiplayerSettings->getNumColors() && count < COLOR_LIMIT; ++c )
		{
			Bool held = FALSE;
			for( Int i = 0; i < MAX_SLOTS && !held; ++i )
				held = i != slot && seat( i ) != NULL && seat( i )->getColor() == c;
			if( !held )
				colors[ count++ ] = c;
		}
		return count;
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

	Piece pingPiece( Int ping )
	{
		return ping < PING_GOOD ? LOBBY_PING4 : ping < PING_FAIR ? LOBBY_PING3 : ping < PING_POOR ? LOBBY_PING2 : LOBBY_PING1;
	}

	/// Start's state: the host's I while a human is not ready, a ready client's P
	const char *startState( void )
	{
		if( isNetwork() && !amHost() )
		{
			const GameSlot *me = localSlot() >= 0 ? seat( localSlot() ) : NULL;
			if( me != NULL && me->isAccepted() )
				return "P";
		}
		else if( isNetwork() )
		{
			// the host's own seat is never asked
			for( Int i = 1; i < MAX_SLOTS; ++i )
				if( seat( i ) != NULL && seat( i )->isHuman() && !seat( i )->isAccepted() )
					return "I";
		}
		return pointerIn( DirectStrikeLobby_start() ) ? "H" : "";
	}

	Bool editsSeat( Int slot ) { return amHost() && slot != localSlot(); }

	Bool editsArmy( Int slot )
	{
		const GameSlot *s = seat( slot );
		return s->isOccupied() && ( slot == localSlot() || ( amHost() && s->isAI() ) );
	}

	/// ready as the lamp says: an AI, the host (a skirmish's player too) or a joiner who accepted
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

	/// whether the local player can move to `at`: he holds a seat of the six and that seat's slot is open.  A
	/// closed seat, a taken one and his own do nothing.
	Bool canMoveTo( Int at )
	{
		const Int local = localSlot();
		if( local < 0 || local >= DSLOBBY_SEATS || seat( local ) == NULL || !seat( local )->isOccupied() )
			return FALSE;
		const GameSlot *there = seat( s_slotAt[ at ] );
		return s_slotAt[ at ] != local && there != NULL && there->getState() == SLOT_OPEN;
	}

	/// the seat a click on team `team`'s head moves the local player to: that team's first open seat down its rows,
	/// -1 for a full team or the player's own (the head of his team takes him nowhere)
	Int headMove( Int team )
	{
		const Int local = localSlot();
		if( local < 0 || local >= DSLOBBY_SEATS || DirectStrikeLobby_team( s_seatOf[ local ] ) == team )
			return -1;
		for( Int row = 0; row < DSLOBBY_ROWS; ++row )
			if( canMoveTo( DirectStrikeLobby_seat( team, row ) ) )
				return DirectStrikeLobby_seat( team, row );
		return -1;
	}

	/// the plate a click moves the player by: an open seat's whole card, but for the host its seat line, which is the
	/// seat control
	Bool overMove( Int at, Real x, Real y )
	{
		if( !canMoveTo( at ) || !DirectStrikeLobby_card( at ).contains( x, y ) )
			return FALSE;
		return !( editsSeat( s_slotAt[ at ] ) && DirectStrikeLobby_part( at, DSSEAT_LINE, isNetwork() ).contains( x, y ) );
	}

	Bool pointerOverMove( Int at )
	{
		const MouseIO *mouse = TheMouse->getMouseStatus();
		Real x, y;
		DirectStrike_toDesign( s_frame, mouse->pos.x, mouse->pos.y, &x, &y );
		return s_list == LIST_NONE && overMove( at, x, y );
	}

	/// a team head's plate, its point toward the lane
	DirectStrikeRect headPlate( Int team ) { return DirectStrikeLobby_pointed( DirectStrikeLobby_teamHead( team ), team, DSLOBBY_HEAD_SPEAR ); }

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

	/// every window of the room hidden but the chat entry; the windows that hold it stay, drawing nothing
	void hideRoom( GameWindow *window, GameWindow *keep )
	{
		for( GameWindow *child = window->winGetChild(); child != NULL; child = child->winGetNext() )
		{
			if( child == keep || holds( child, keep ) )
			{
				if( child->winIsHidden() )
					child->winHide( FALSE );
				if( child->winGetDrawFunc() != drawNothing )
				{
					const Drawn was = { child, child->winGetDrawFunc() };
					s_blanked.push_back( was );
					child->winSetDrawFunc( drawNothing );
				}
				if( child != keep )
					hideRoom( child, keep );
			}
			else if( !child->winIsHidden() )
			{
				if( std::find( s_hidden.begin(), s_hidden.end(), child ) == s_hidden.end() )
					s_hidden.push_back( child );
				child->winHide( TRUE );
			}
		}
	}

	GameWindow *s_fading = NULL;				///< the room the lobby left as its layout fades out
	GameWinDrawFunc s_fadingDraw = NULL;		///< that room's own draw, back at DirectStrikeLobby_restoreRoom

	/// the left room's draw through the fade-out: the shell's transition shows the stock gadgets again, so they go every frame
	void fadingDraw( GameWindow *window, WinInstanceData * )
	{
		hideRoom( window, NULL );
	}

	/// the chat entry on the chat input's rect, so a click there gives it the keyboard
	void placeEntry( GameWindow *entry )
	{
		const LaneHudBox b = boxOf( DirectStrikeLobby_chatInput() );
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
	/// a pointed plate, team 1's piece pointing right and team 2's left, drawn at r grown by `by` toward the lane
	void pointedPlate( const DirectStrikeRect &r, Int team, Real by, Piece rightL, Piece leftL, Color tint = 0xFFFFFFFF )
	{
		const Piece l = team == 0 ? rightL : leftL;
		threeSlice( boxOf( DirectStrikeLobby_pointed( r, team, by ) ), l, (Piece)( l + 1 ), (Piece)( l + 2 ), "", tint );
	}

	/// the console, the title over its top rim with each team's chevrons at its ends, the version under its foot
	void drawConsole( void )
	{
		nineSlice( boxOf( DirectStrikeLobby_console() ), CONSOLE_TL );
		const LaneHudBox title = boxOf( DirectStrikeLobby_title() );
		threeSlice( title, HEX_TITLE_L, HEX_TITLE_C, HEX_TITLE_R );
		drawSpaced( TheGameText->fetch( "GUI:DirectStrike" ), font( TITLE_EM ), title.x + title.w / 2, title.y, title.h, TITLE_SPACING, TEXT_WHITE, 0 );
		for( Int i = 0; i < 2; ++i )
		{
			glow( CHEVRON_R, boxOf( DirectStrikeLobby_titleChevron( 0, i ) ), teamColor( 0 ) );
			glow( CHEVRON_L, boxOf( DirectStrikeLobby_titleChevron( 1, i ) ), teamColor( 1 ) );
		}
		// the chevrons leave the title the name alone: the version stands under the console's right foot
		Bool tagged = FALSE;
		const UnicodeString version = TheGameText->fetch( "GUI:DirectStrikeVersion", &tagged );
		if( tagged && !version.isEmpty() )
			text( version, VERSION_EM, VERSION_X, VERSION_Y, TEXT_GREY, 1 );
	}

	/// each team's head: its plate pointing at the lane, the name at its outer end, a light a seat in play lit for each
	/// ready one from the outer end, and its chevrons.  A click takes the player to the team's first open seat, so
	/// the head shows the list's hover band and a gold name while it would.
	void drawHeads( void )
	{
		for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
		{
			const DirectStrikeRect head = DirectStrikeLobby_teamHead( team );
			const LaneHudBox b = boxOf( head );
			pointedPlate( head, team, DSLOBBY_HEAD_SPEAR, SPEAR_HEAD_RIGHT_L, SPEAR_HEAD_LEFT_L );
			const Bool hot = s_list == LIST_NONE && headMove( team ) >= 0 && pointerIn( headPlate( team ) );
			if( hot )
				listRow( boxOf( DirectStrike_rect( head.x0 + HEAD_GLASS, head.y0 + HEAD_GLASS, head.x1 - HEAD_GLASS, head.y1 - HEAD_GLASS ) ), FALSE, TRUE );
			UnicodeString label;
			label.format( TheGameText->fetch( "GUI:DirectStrikeTeam" ), team + 1 );
			const Int x = team == 0 ? screenX( head.x0 + TEAM_INSET ) : screenX( head.x1 - TEAM_INSET );
			drawSpaced( label, font( TEAM_EM ), x, b.y, b.h, TEAM_SPACING, hot ? TEXT_GOLD : TEXT_GREY, team == 0 ? -1 : 1 );

			const Int size = min( teamSize( team ), (Int)DSLOBBY_HEAD_LEDS ), ready = teamReady( team );
			for( Int i = 0; i < size; ++i )
			{
				const LaneHudBox led = boxOf( DirectStrikeLobby_headLed( team, i ) );
				if( i < ready )
					glow( LED_LIT, led, teamColor( team ) );
				else
					drawImage( art( LED ), led );
			}
			for( Int i = 0; i < DSLOBBY_HEAD_CHEVRONS; ++i )
				glow( team == 0 ? CHEVRON_R : CHEVRON_L, boxOf( DirectStrikeLobby_headChevron( team, i ) ), teamColor( team ) );
		}
	}

	void drawPortrait( Int slot )
	{
		const GameSlot *s = seat( slot );
		const DirectStrikeRect wellAt = seatPart( slot, DSSEAT_PORTRAIT );
		const LaneHudBox well = boxOf( wellAt );
		if( s->getState() == SLOT_OPEN )
		{
			// the + key, 28 in: the whole open card is its target (overMove), so it lights for the card
			const Int at = seatOfSlot( slot );
			const DirectStrikeRect mark = DirectStrike_rect( wellAt.x0 + PLUS_IN, wellAt.y0 + PLUS_IN, wellAt.x1 - PLUS_IN, wellAt.y1 - PLUS_IN );
			const Bool hot = pointerOverMove( at );
			const char *state = canMoveTo( at ) ? stateOf( hot, hot && TheMouse->getMouseStatus()->leftState != MBS_Up, FALSE ) : "I";
			drawImage( art( CLAMP_PLUS, state ), boxOf( mark ) );
			return;
		}
		if( s->getState() == SLOT_CLOSED )
		{
			drawCentred( art( LOBBY_CLOSED ), well, CLOSED_TINT );
			return;
		}
		const Int pick = s->getPlayerTemplate();
		if( isRandom( pick ) )
		{
			portraitWell( well, art( LOBBY_RANDOM ), LobbySeatColor( s ), FALSE );
			return;
		}
		const Image *general = generalPortrait( ThePlayerTemplateStore->getNthPlayerTemplate( pick ) );
		portraitWell( well, general != NULL ? general : headquartersPicture( armySide( pick ) ), LobbySeatColor( s ), TRUE );
	}

	void drawSeatLine( Int slot )
	{
		const GameSlot *s = seat( slot );
		const DirectStrikeRect line = seatPart( slot, DSSEAT_LINE );
		const Bool drop = editsSeat( slot );
		const char *state = drop ? chipState( line, LIST_SEAT, slot ) : "";
		dropdown( boxOf( line ), state, !drop );
		const Real x = line.x0 + CONTENT_INSET, y = middleOf( line );
		if( s->getState() == SLOT_OPEN )
			text( TheGameText->fetch( "GUI:Open" ), CHIP_EM, x, y, *state == 'P' ? TEXT_DARK : TEXT_GREY, -1, faceShadow( state ) );
		else if( s->getState() == SLOT_CLOSED )
			text( TheGameText->fetch( "GUI:Closed" ), CHIP_EM, x, y, *state == 'P' ? TEXT_DARK : TEXT_DIM, -1, faceShadow( state ) );
		else if( s->isAI() )
		{
			drawImage( art( LOBBY_AI ), iconAt( x, line, ICON, ICON ) );
			text( seatLabel( s ), NAME_EM, x + ICON + ICON_GAP, y, faceText( state ), -1, faceShadow( state ) );
		}
		else
			text( s->getName(), NAME_EM, x, y, faceText( state ), -1, faceShadow( state ) );
	}

	void drawSeatChips( Int slot )
	{
		const GameSlot *s = seat( slot );
		const Bool drop = editsArmy( slot );
		const Int pick = s->getPlayerTemplate();

		const DirectStrikeRect army = seatPart( slot, DSSEAT_ARMY );
		const char *armyState = drop ? chipState( army, LIST_ARMY, slot ) : "";
		dropdown( boxOf( army ), armyState, !drop );
		// the name alone: the portrait well beside the chip already shows the army
		text( armyName( pick ), CHIP_EM, army.x0 + CONTENT_INSET, middleOf( army ), faceText( armyState ), -1, faceShadow( armyState ) );

		const DirectStrikeRect color = seatPart( slot, DSSEAT_COLOR );
		dropdown( boxOf( color ), drop ? chipState( color, LIST_COLOR, slot ) : "", !drop );
		swatch( iconAt( color.x0 + CONTENT_INSET, color, SWATCH_W, SWATCH_H ), LobbySeatColor( s ) );
	}

	void drawSeatLights( Int slot )
	{
		const GameSlot *s = seat( slot );
		Piece lamp = LAMP_READY;
		if( s->isHuman() )
			lamp = slot == 0 ? LAMP_HOST : s->isAccepted() ? LAMP_READY : LAMP_WAITING;
		drawImage( art( lamp ), boxOf( seatPart( slot, DSSEAT_LAMP ) ) );
		// online the light is the player's own trip to the server, so your own seat has one as in the classic
		// room; a LAN or skirmish room has no ping of your own.  -1 (nothing drawn) until the card's ping arrives
		const Bool ownLight = s_room == ROOM_ONLINE && OnlineIsEnabled();
		const Int ping = s->isHuman() && ( slot != localSlot() || ownLight ) ? seatPing( slot ) : -1;
		if( ping >= 0 )
			drawImage( art( pingPiece( ping ) ), boxOf( seatPart( slot, DSSEAT_PING ) ) );
	}

	/// the seat `at`: its plate pointing at the lane (the local player's with the bright lip, an open or closed seat a
	/// bay, a closed one faded), the portrait well, the line, and for a player the chips, the lights strip in a
	/// network room and the lip's segments, lit in the team's colour while he is ready
	void drawSeat( Int at )
	{
		const Int slot = s_slotAt[ at ];
		const GameSlot *s = seat( slot );
		if( s == NULL )
			return;
		const Int team = DirectStrikeLobby_team( at );
		const Bool occupied = s->isOccupied();
		const Color tint = s->getState() == SLOT_CLOSED ? CLOSED_TINT : 0xFFFFFFFF;
		const Bool mine = slot == localSlot();
		pointedPlate( DirectStrikeLobby_card( at ), team, DSLOBBY_SPEAR,
			!occupied ? BAY_RIGHT_L : mine ? SPEAR_YOU_RIGHT_L : SPEAR_RIGHT_L,
			!occupied ? BAY_LEFT_L : mine ? SPEAR_YOU_LEFT_L : SPEAR_LEFT_L, tint );
		nineSlice( boxOf( seatPart( slot, DSSEAT_PORTRAIT ) ), WELL_TL, tint );
		drawPortrait( slot );
		drawSeatLine( slot );
		if( !occupied )
			return;
		drawSeatChips( slot );
		if( isNetwork() )
		{
			nineSlice( boxOf( DirectStrikeLobby_strip( at ) ), WELL_TL );
			drawSeatLights( slot );
		}
		const Bool ready = isReady( slot );
		for( Int k = 0; k < DSLOBBY_LIP_SEGS; ++k )
		{
			const LaneHudBox seg = boxOf( DirectStrikeLobby_lipSeg( at, k ) );
			if( ready )
				glow( SEG_LIT, seg, teamColor( team ) );
			else
				drawImage( art( SEG ), seg );
		}
	}

	//-----------------------------------------------------------------------------------------------
	UnicodeString sizeLabel( Int first, Int second )
	{
		UnicodeString label;
		label.format( TheGameText->fetch( "GUI:DirectStrikeTeamSize" ), first, second );
		return label;
	}

	/// the team size chip: the host's dropdown, a field for everyone else
	void drawSize( void )
	{
		const DirectStrikeRect r = DirectStrikeLobby_size();
		const char *state = amHost() ? chipState( r, LIST_SIZE, -1 ) : "";
		clampChip( boxOf( r ), state, !amHost() );
		text( sizeLabel( teamSize( 0 ), teamSize( 1 ) ), CHIP_EM, r.x0 + CONTENT_INSET, middleOf( r ), faceText( state ), -1, faceShadow( state ) );
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

	/// the game speed chip: the host's dropdown, a field for everyone else
	void drawSpeed( void )
	{
		const DirectStrikeRect r = DirectStrikeLobby_speed();
		const char *state = amHost() ? chipState( r, LIST_SPEED, -1 ) : "";
		clampChip( boxOf( r ), state, !amHost() );
		text( GameSpeedText( roomSpeed() ), CHIP_EM, r.x0 + CONTENT_INSET, middleOf( r ), faceText( state ), -1, faceShadow( state ) );
	}

	/// the Riveted Clamp box of the chat, the offline island panel and every open list
	void clampBox( const DirectStrikeRect &r ) { nineSlice( boxOf( r ), CLAMP_BOX_TL ); }

	void drawIsland( void )
	{
		const UnicodeString name = TheGameText->fetch( "GUI:DirectStrikeMap" );
		const UnicodeString note = TheGameText->fetch( "GUI:DirectStrikeIslandNote" );
		if( !isNetwork() )
		{
			clampBox( DirectStrikeLobby_islandPanel() );
			const DirectStrikeRect island = DirectStrikeLobby_island();
			nineSlice( boxOf( DirectStrike_rect( island.x0 - ISLAND_WELL_OUT, island.y0 - ISLAND_WELL_OUT, island.x1 + ISLAND_WELL_OUT,
				island.y1 + ISLAND_WELL_OUT ) ), WELL_TL );
			drawImage( art( LOBBY_ISLAND ), boxOf( island ) );
			text( name, ISLAND_EM, 960, island.y1 + ISLAND_NAME_BELOW, TEXT_WHITE, 0 );
			text( note, ISLAND_NOTE_EM, 960, island.y1 + ISLAND_NOTE_BELOW, TEXT_GREY, 0 );
			return;
		}
		// the map band on the head row: its island in a well, the name on the glass and the note at its right end
		const DirectStrikeRect band = DirectStrikeLobby_islandChip();
		threeSlice( boxOf( band ), CLAMP_BAND_L, CLAMP_BAND_C, CLAMP_BAND_R );
		nineSlice( boxOf( DirectStrikeLobby_islandWell() ), WELL_TL );
		drawImage( art( LOBBY_ISLAND_BAND ), boxOf( DirectStrikeLobby_islandSmall() ) );
		text( name, ISLAND_BAND_EM, ISLAND_BAND_NAME_X, middleOf( band ), TEXT_WHITE );
		text( note, ISLAND_BAND_NOTE_EM, ISLAND_BAND_NOTE_X, middleOf( band ), TEXT_GREY, 1 );
	}

	/// a chat string of the line `line`, wrapped at `wrap` pixels and clipped to the panel's glass
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

	/// the log's entries, newest at the foot unless scrolled back: "[name] message" in the sender's
	/// colour and white, the message wrapped beside the name; anything else dim, wrapped to the glass
	void drawChatLog( void )
	{
		clampBox( DirectStrikeLobby_chat() );
		GameWindow *log = named( ROOMS[ s_room ].chatLog );
		if( log == NULL )
			return;
		const DirectStrikeRect at = DirectStrikeLobby_chatGlass();
		IRegion2D glass;
		glass.lo.x = screenX( at.x0 );
		glass.lo.y = screenY( at.y0 );
		glass.hi.x = screenX( at.x1 );
		glass.hi.y = screenY( at.y1 );
		const Int left = screenX( at.x0 + CHAT_TEXT_IN );
		GameFont *f = font( CHAT_EM );
		const Int gap = px( CHAT_NAME_GAP ), step = px( DSLOBBY_CHAT_LINE ), width = glass.hi.x - left;
		const Int entries = GadgetListBoxGetNumEntries( log );
		s_chatScroll = max( 0, min( s_chatScroll, entries - 1 ) );
		Int bottom = screenY( at.y1 - CHAT_FOOT );
		Int line = 0;
		for( Int row = entries - 1 - s_chatScroll; row >= 0 && line < CHAT_LINES && bottom > glass.lo.y; --row, ++line )
		{
			Color color = TEXT_WHITE;
			const UnicodeString entry = GadgetListBoxGetTextAndColor( log, &color, row, 0 );
			const Int close = senderEnd( entry );
			Int nameW = 0, nameH = 0, textW = 0, textH = 0;
			UnicodeString message = entry;
			DisplayString *who = NULL;
			if( close >= 0 )
			{
				UnicodeString name;
				for( Int i = 1; i < close; ++i )
					name.concat( entry.getCharAt( i ) );
				message.set( entry.str() + close + 2 );
				who = chatString( s_chatNames, line, name, f, &glass, 0 );
				who->getSize( &nameW, &nameH );
				nameW += gap;
			}
			DisplayString *what = chatString( s_chatTexts, line, message, f, &glass, width - nameW );
			what->getSize( &textW, &textH );
			// one line takes `step`, every wrapped line under it the font's own height
			const Int lineH = f->height;
			const Int top = bottom - textH - ( step - lineH ) / 2;
			if( who != NULL )
				who->draw( left, top, color, TEXT_SHADOW );
			what->draw( left + nameW, top, who != NULL ? TEXT_WHITE : TEXT_DIM, TEXT_SHADOW );
			bottom -= textH + step - lineH;
		}
	}

	void drawChatInput( void )
	{
		const DirectStrikeRect input = DirectStrikeLobby_chatInput();
		const LaneHudBox field = boxOf( input );
		dropdown( field, "", TRUE );
		clampKey( DirectStrikeLobby_send(), CLAMP_SEND );
		GameWindow *entry = named( CHAT_ENTRY );
		if( entry == NULL )
			return;
		placeEntry( entry );
		IRegion2D inside;
		inside.lo.x = screenX( input.x0 + CHAT_INPUT_IN );
		inside.lo.y = field.y;
		inside.hi.x = field.x + field.w - px( CHAT_INPUT_IN );
		inside.hi.y = field.y + field.h;
		if( s_inputText == NULL )
			s_inputText = TheDisplayStringManager->newDisplayString();
		GameFont *f = font( CHAT_EM );
		if( s_inputText->getFont() != f )
			s_inputText->setFont( f );
		const UnicodeString typed = GadgetTextEntryGetText( entry );
		if( s_inputText->getText().compare( typed ) != 0 )
			s_inputText->setText( typed );
		s_inputText->setClipRegion( &inside );
		Int w = 0, h = 0;
		s_inputText->getSize( &w, &h );
		if( h == 0 )
			h = px( CHAT_EM );
		const Int top = field.y + ( field.h - h ) / 2;
		// the field shows the end of a line too long for it
		const Int x = min( inside.lo.x, inside.hi.x - w - px( CARET_W ) );
		s_inputText->draw( x, top, TEXT_WHITE, TEXT_SHADOW );
		if( TheWindowManager->winGetFocus() == entry )
			TheDisplay->drawFillRect( x + w + 1, top, px( CARET_W ), h, TEXT_WHITE );
	}

	void drawKeys( void )
	{
		clampKey( DirectStrikeLobby_back(), CLAMP_BACK );
		drawSize();
		drawSpeed();
		// startState's H under the pointer turns P while the button is held there, as every key does
		const char *state = startState();
		if( *state == 'H' && held( DirectStrikeLobby_start() ) )
			state = "P";
		threeSlice( boxOf( DirectStrikeLobby_start() ), CLAMP_START_L, CLAMP_START_C, CLAMP_START_R, state );
		const UnicodeString label = TheGameText->fetch( isNetwork() && !amHost() ? "GUI:DirectStrikeReady" : "GUI:DirectStrikeStart" );
		const LaneHudBox b = boxOf( DirectStrikeLobby_start() );
		const Color color = *state == 'P' ? TEXT_DARK : *state == 'I' ? TEXT_DIM : TEXT_WHITE;
		drawSpaced( label, font( START_EM ), b.x + b.w / 2, b.y, b.h, START_SPACING, color, 0, *state != 'P' );
	}

	/// the tug bar: each team's ready seats over its seats in play, filling from its own end toward the gold line in
	/// the middle; both halves full is a room the host can start
	void drawTug( void )
	{
		threeSlice( boxOf( DirectStrikeLobby_tug() ), TUG_L, TUG_C, TUG_R );
		for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
		{
			const Int size = teamSize( team );
			const Int lit = size > 0 ? ( 2 * DSLOBBY_TUG_SEGS * teamReady( team ) + size ) / ( 2 * size ) : 0;
			for( Int i = 0; i < DSLOBBY_TUG_SEGS; ++i )
			{
				const LaneHudBox seg = boxOf( DirectStrikeLobby_tugSeg( team, i ) );
				if( i < lit )
					glow( TUG_SEG_LIT, seg, teamColor( team ) );
				else
					drawImage( art( TUG_SEG ), seg );
			}
		}
		const LaneHudBox line = boxOf( DirectStrike_rect( GOLD_LINE_X0, GOLD_LINE_Y0, GOLD_LINE_X1, GOLD_LINE_Y1 ) );
		TheDisplay->drawFillRect( line.x, line.y, line.w, line.h, TEXT_GOLD );
	}

	//-----------------------------------------------------------------------------------------------
	/// a list's row in the theme's one rule, its text from LIST_TEXT in, after an icon when it has one, and the
	/// check at the right on the current choice
	void drawListRow( const DirectStrikeRect &list, const DirectStrikeRect &r, const UnicodeString &label, Bool current, Piece icon )
	{
		const Bool hot = !current && pointerIn( r );
		listRow( boxOf( r ), current, hot );
		Real x = list.x0 + LIST_TEXT;
		if( icon != PIECE_COUNT )
		{
			drawImage( art( icon ), iconAt( x, r, ICON, ICON ) );
			x = list.x0 + LIST_AI_TEXT;
		}
		text( label, CHIP_EM, x, middleOf( r ), listText( current, hot ) );
		if( current )
			drawImage( art( LOBBY_CHECK ), iconAt( list.x1 - CHECK_INSET, r, CHECK, CHECK ) );
	}

	void drawSeatList( Int slot )
	{
		const DirectStrikeRect list = DirectStrikeLobby_seatList( seatOfSlot( slot ), isNetwork() );
		clampBox( list );
		const Int current = SlotPickOf( seat( slot ) );
		for( Int row = 0; row < DSLOBBY_SEAT_ROWS; ++row )
			drawListRow( list, DirectStrikeLobby_seatRow( seatOfSlot( slot ), row, isNetwork() ), TheGameText->fetch( SEAT_LABELS[ row ] ),
				seatPick( row ) == current, row >= SEAT_ROW_FIRST_AI ? LOBBY_AI : PIECE_COUNT );
	}

	void drawSizeList( void )
	{
		const DirectStrikeRect list = DirectStrikeLobby_sizeList();
		clampBox( list );
		for( Int row = 0; row < DSLOBBY_SIZES; ++row )
			drawListRow( list, DirectStrikeLobby_sizeRow( row ), sizeLabel( row + 1, row + 1 ),
				teamSize( 0 ) == row + 1 && teamSize( 1 ) == row + 1, PIECE_COUNT );
	}

	void drawSpeedList( void )
	{
		const DirectStrikeRect list = DirectStrikeLobby_speedList();
		clampBox( list );
		for( Int row = 0; row < DSLOBBY_SPEEDS; ++row )
			drawListRow( list, DirectStrikeLobby_speedRow( row ), GameSpeedText( PLAYER_GAME_SPEED_PERCENTS[ row ] ),
				roomSpeed() == PLAYER_GAME_SPEED_PERCENTS[ row ], PIECE_COUNT );
	}

	void drawPicker( Int slot )
	{
		const Int at = seatOfSlot( slot );
		clampBox( DirectStrikeLobby_picker( at, isNetwork() ) );
		const Int pick = seat( slot )->getPlayerTemplate();

		const DirectStrikeRect random = DirectStrikeLobby_pickerRandom( at, isNetwork() );
		const char *randomState = isRandom( pick ) ? "P" : pointerIn( random ) ? "H" : "";
		threeSlice( boxOf( random ), DROP_L, DROP_C, FIELD_R, randomState );
		const Real x = random.x0 + CONTENT_INSET;
		drawImage( art( COIN_RANDOM ), iconAt( x, random, ICON, ICON ) );
		text( TheGameText->fetch( "GUI:Random" ), CHIP_EM, x + ICON + ICON_GAP, middleOf( random ), faceText( randomState ), -1, faceShadow( randomState ) );

		Int pickColumn = -1, pickTile = -1;
		armyCell( pick, &pickColumn, &pickTile );
		for( Int column = 0; column < DSLOBBY_ARMY_COLUMNS; ++column )
			for( Int tile = 0; tile < DSLOBBY_ARMY_TILES; ++tile )
			{
				const DirectStrikeRect r = DirectStrikeLobby_pickerTile( at, column, tile, isNetwork() );
				const Bool current = column == pickColumn && tile == pickTile;
				const Bool hot = !current && pointerIn( r );
				const char *state = current ? "P" : hot ? "H" : "";
				drawImage( art( LOBBY_PICK_TILE, state ), boxOf( r ) );
				if( current )
					currentFrame( boxOf( r ) );
				const Int num = ThePlayerTemplateStore->getTemplateNumByName( ARMIES[ column ][ tile ] );
				const PlayerTemplate *army = num >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( num ) : NULL;
				const Image *general = generalPortrait( army );
				const DirectStrikeRect picture = DirectStrike_rect( r.x0 + TILE_PICTURE_X, r.y0 + TILE_PICTURE_Y,
					r.x0 + TILE_PICTURE_X + TILE_PICTURE, r.y0 + TILE_PICTURE_Y + TILE_PICTURE );
				portraitWell( boxOf( picture ), general != NULL ? general : headquartersPicture( (Side)column ), 0, TRUE );
				const Color color = current ? TEXT_DARK : hot ? TEXT_GOLD : TEXT_WHITE;
				text( TheGameText->fetch( ARMY_LABELS[ column ][ tile ] ), TILE_EM, r.x0 + TILE_NAME_X, r.y0 + TILE_NAME_Y, color, 0, !current );
			}
	}

	void drawColorList( Int slot )
	{
		Int colors[ COLOR_LIMIT ];
		const Int count = freeColors( slot, colors );
		clampBox( DirectStrikeLobby_colorList( seatOfSlot( slot ), count, isNetwork() ) );
		for( Int i = 0; i < count; ++i )
		{
			const DirectStrikeRect cell = DirectStrikeLobby_colorCell( seatOfSlot( slot ), count, i, isNetwork() );
			const Real x = ( cell.x0 + cell.x1 - SWATCH_W ) * 0.5f;
			const LaneHudBox box = iconAt( x, cell, SWATCH_W, SWATCH_H );
			const Bool current = colors[ i ] == seat( slot )->getColor();
			listRow( boxOf( cell ), FALSE, !current && pointerIn( cell ) );
			swatch( box, TheMultiplayerSettings->getColor( colors[ i ] )->getColor() );
			if( current )
			{
				currentFrame( box );
				drawCentred( art( LOBBY_CHECK ), box );
			}
		}
	}

	//-----------------------------------------------------------------------------------------------
	void lobbyDraw( GameWindow *window, WinInstanceData *instData )
	{
		if( s_game == NULL )
			return;
		s_frame = DirectStrike_frame( (Int)TheDisplay->getWidth(), (Int)TheDisplay->getHeight() );
		// the common frame, the same for every player of the room whatever his side (the owner, 2026-11-23)
		begin( SIDE_COMMON, s_frame.k );
		placeSeats();
		hideRoom( window, isNetwork() ? named( CHAT_ENTRY ) : NULL );

		TheDisplay->drawFillRect( 0, 0, (Int)TheDisplay->getWidth(), (Int)TheDisplay->getHeight(), VEIL );
		drawConsole();
		drawHeads();
		for( Int at = 0; at < DSLOBBY_SEATS; ++at )
			drawSeat( at );
		drawIsland();
		if( isNetwork() )
		{
			drawChatLog();
			drawChatInput();
		}
		drawKeys();
		drawTug();

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
		if( list == LIST_SPEED )
		{
			// the hidden box's own pick, so the room's handler sets the game and sends the options
			GameWindow *box = speedBox();
			for( Int row = 0; row < DSLOBBY_SPEEDS && box != NULL; ++row )
				if( DirectStrikeLobby_speedRow( row ).contains( x, y ) )
				{
					choose( box, PLAYER_GAME_SPEED_PERCENTS[ row ] );
					return;
				}
			return;
		}
		if( list == LIST_SIZE )
		{
			for( Int row = 0; row < DSLOBBY_SIZES; ++row )
				if( DirectStrikeLobby_sizeRow( row ).contains( x, y ) )
				{
					chooseSize( row + 1 );
					return;
				}
			return;
		}
		if( list == LIST_SEAT )
		{
			for( Int row = 0; row < DSLOBBY_SEAT_ROWS; ++row )
				if( DirectStrikeLobby_seatRow( seatOfSlot( slot ), row, isNetwork() ).contains( x, y ) )
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
			if( DirectStrikeLobby_pickerRandom( seatOfSlot( slot ), isNetwork() ).contains( x, y ) )
			{
				choose( combo, PLAYERTEMPLATE_RANDOM );
				return;
			}
			for( Int column = 0; column < DSLOBBY_ARMY_COLUMNS; ++column )
				for( Int tile = 0; tile < DSLOBBY_ARMY_TILES; ++tile )
					if( DirectStrikeLobby_pickerTile( seatOfSlot( slot ), column, tile, isNetwork() ).contains( x, y ) )
					{
						choose( combo, ThePlayerTemplateStore->getTemplateNumByName( ARMIES[ column ][ tile ] ) );
						return;
					}
			return;
		}
		Int colors[ COLOR_LIMIT ];
		const Int count = freeColors( slot, colors );
		GameWindow *combo = named( "ComboBoxColor", slot );
		for( Int i = 0; i < count && combo != NULL; ++i )
			if( DirectStrikeLobby_colorCell( seatOfSlot( slot ), count, i, isNetwork() ).contains( x, y ) )
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
		if( DirectStrikeLobby_start().contains( x, y ) && *startState() != 'I' )
		{
			press( "ButtonStart" );
			return;
		}
		if( isNetwork() && DirectStrikeLobby_send().contains( x, y ) )
		{
			sendChat();
			return;
		}
		if( amHost() && DirectStrikeLobby_size().contains( x, y ) )
		{
			s_list = LIST_SIZE;
			return;
		}
		if( amHost() && DirectStrikeLobby_speed().contains( x, y ) )
		{
			s_list = LIST_SPEED;
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
		// an open seat's card takes the player there, and so does the other team's head, to its first open seat
		for( Int at = 0; at < DSLOBBY_SEATS; ++at )
			if( overMove( at, x, y ) )
			{
				moveTo( at );
				return;
			}
		for( Int team = 0; team < DSLOBBY_TEAMS; ++team )
			if( headPlate( team ).contains( x, y ) && headMove( team ) >= 0 )
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
	for( size_t i = 0; i < s_blanked.size(); ++i )
		s_blanked[ i ].window->winSetDrawFunc( s_blanked[ i ].draw );
	for( size_t i = 0; i < s_hidden.size(); ++i )
		s_hidden[ i ]->winHide( FALSE );
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
	s_chatScroll = 0;
	s_parent = NULL;
	s_game = NULL;
	s_roomDraw = NULL;
	s_roomInput = NULL;
	s_list = LIST_NONE;
	s_listSlot = -1;
}
