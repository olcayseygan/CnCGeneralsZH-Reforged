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

// PlayerColorScheme.cpp
//
// The client's answer to "what colour is that player".  See PlayerColorScheme.h for why this is not
// a field on Player.

#include "PreRTS.h"

#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/GameClient.h"
#include "GameClient/PlayerColorScheme.h"

// ------------------------------------------------------------------------------------------------
// The palette.
//
// Four shades a family, picked by eye rather than by rotating a value in HSV: a hue walked down in
// brightness alone goes muddy at the third step, and the third and fourth entries here lean the
// other way instead - one lighter and desaturated, one pushed towards the neighbouring hue.  All
// four still read as "the blue one" at the distance a minimap is looked at.
//
// The order the families are handed out in matters more than which colours they are.  Blue is
// always yours; red is always the first side that is not.
// ------------------------------------------------------------------------------------------------
static const Color TheFamilyShades[ PLAYER_COLOR_FAMILY_COUNT ][ PLAYER_COLOR_SHADE_COUNT ] =
{
	{ 0x4C8CFF, 0x1E46B4, 0x9EC4FF, 0x0096C8 },		// blue
	{ 0xFF3232, 0xA51414, 0xFF9678, 0xD25000 },		// red
	{ 0x3CD23C, 0x1E7D28, 0xA0F08C, 0x6EB400 },		// green
	{ 0xFFD22D, 0xBE8C00, 0xFFF096, 0xD2A03C },		// yellow
	{ 0xB45AFF, 0x6E1EC8, 0xDCA0FF, 0x9646D2 },		// purple
	{ 0xFF8219, 0xBE5000, 0xFFB478, 0xD26E2D },		// orange
	{ 0x2DD2D2, 0x0F7D7D, 0x96F0F0, 0x3CA5B4 },		// cyan
	{ 0xE1E1E1, 0x969696, 0xFAFAFA, 0xBEBEBE },		// grey
};

// Blue is spoken for - it is the side the person holding the mouse is on - so the other alliances
// are dealt from here, in an order that keeps two adjacent teams far apart on the wheel.
static const Int TheTeamFamilyOrder[] =
{
	PLAYER_COLOR_FAMILY_RED,
	PLAYER_COLOR_FAMILY_GREEN,
	PLAYER_COLOR_FAMILY_YELLOW,
	PLAYER_COLOR_FAMILY_PURPLE,
	PLAYER_COLOR_FAMILY_ORANGE,
	PLAYER_COLOR_FAMILY_CYAN,
	PLAYER_COLOR_FAMILY_GREY,
};

static const Int TheTeamFamilyCount = sizeof( TheTeamFamilyOrder ) / sizeof( TheTeamFamilyOrder[ 0 ] );

// ------------------------------------------------------------------------------------------------
// The table, and what it was built from.
// ------------------------------------------------------------------------------------------------
struct PlayerColorEntry
{
	Color trueDay;			///< what the lobby agreed on, which is what the simulation reads
	Color trueNight;
	Color day;					///< what this screen draws
	Color night;
};

static PlayerColorEntry	theEntries[ MAX_PLAYER_COUNT ];
static Int							theEntryCount = 0;
static Bool							theTableValid = FALSE;
static Bool							theTableChanged = FALSE;
static UnsignedInt			theTableStamp = 0;
static UnsignedInt			theTableFrame = 0;
static Bool							theTableHasFrame = FALSE;
static Int							theTableScheme = PLAYER_COLORS_ORIGINAL;

// ------------------------------------------------------------------------------------------------
static Color opaque( Color rgb )
{
	return (Color)(0xFF000000 | (rgb & 0x00FFFFFF));
}

// ------------------------------------------------------------------------------------------------
Color playerSchemeColor( Int family, Int shade )
{
	if( family < 0 )
		family = 0;
	family %= PLAYER_COLOR_FAMILY_COUNT;

	if( shade < 0 )
		shade = 0;

	// past the fourth member of one alliance the same four come round again, a quarter darker each
	// time.  Seven allies is not a case anybody plays, but it is a case the lobby allows.
	const Int ring = shade / PLAYER_COLOR_SHADE_COUNT;
	Color base = TheFamilyShades[ family ][ shade % PLAYER_COLOR_SHADE_COUNT ];

	Int red   = (base >> 16) & 0xFF;
	Int green = (base >>  8) & 0xFF;
	Int blue  =  base        & 0xFF;

	for( Int i = 0; i < ring; ++i )
	{
		red   = red   * 3 / 4;
		green = green * 3 / 4;
		blue  = blue  * 3 / 4;
	}

	// a unit nobody can see on the ground is worse than two units the same colour
	if( red < 32 && green < 32 && blue < 32 )
	{
		red += 32;
		green += 32;
		blue += 32;
	}

	return opaque( (red << 16) | (green << 8) | blue );
}

// ------------------------------------------------------------------------------------------------
Color playerSchemeNightColor( Color dayColor )
{
	const Int red   = (dayColor >> 16) & 0xFF;
	const Int green = (dayColor >>  8) & 0xFF;
	const Int blue  =  dayColor        & 0xFF;

	// a quarter of the way to white.  The shipped palette does this by hand, per slot, and the
	// reason is dark blue: at night the terrain is already dark and a dark unit on it is a hole.
	const Int lit_r = red   + (255 - red)   / 4;
	const Int lit_g = green + (255 - green) / 4;
	const Int lit_b = blue  + (255 - blue)  / 4;

	return opaque( (lit_r << 16) | (lit_g << 8) | lit_b );
}

// ------------------------------------------------------------------------------------------------
/** The playable players, in roster order.  Civilians and the neutral player are not on anybody's
	* side, so they keep the colour the map gave them. */
static Int collectPlayableSides( Player **out )
{
	Int count = 0;

	if( ThePlayerList == NULL )
		return 0;

	const Int rosterSize = ThePlayerList->getPlayerCount();
	for( Int i = 0; i < rosterSize && count < MAX_PLAYER_COUNT; ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( player != NULL && player->isPlayableSide() )
			out[ count++ ] = player;
	}

	return count;
}

// ------------------------------------------------------------------------------------------------
/** Everything the table is derived from, in one number.  Rebuilding costs nothing, but asking the
	* relationship map eight times squared for every health bar on the screen would, so the answer is
	* kept until one of these moves: the setting, the roster, the local player, an alliance. */
static UnsignedInt computeStamp( void )
{
	UnsignedInt stamp = (UnsignedInt)TheGlobalData->m_playerColorScheme * 2654435761u;

	Player *playable[ MAX_PLAYER_COUNT ];
	const Int count = collectPlayableSides( playable );

	stamp = stamp * 31 + (UnsignedInt)count;

	Player *local = (ThePlayerList != NULL) ? ThePlayerList->getLocalPlayer() : NULL;
	stamp = stamp * 31 + (UnsignedInt)(local != NULL ? local->getPlayerIndex() + 1 : 0);

	for( Int a = 0; a < count; ++a )
	{
		stamp = stamp * 31 + (UnsignedInt)playable[ a ]->getPlayerIndex();
		stamp = stamp * 31 + (UnsignedInt)playable[ a ]->getPlayerColor();

		// the whole relationship matrix, not just the row the local player is in: the team scheme
		// groups players by who is allied with whom, and two of them making peace with each other
		// changes the grouping without changing anything about the local player at all
		for( Int b = 0; b < count; ++b )
			stamp = stamp * 3 + (UnsignedInt)playable[ a ]->getRelationship( playable[ b ]->getDefaultTeam() );
	}

	return stamp;
}

// ------------------------------------------------------------------------------------------------
static void assignScheme( Player *player, Int family, Int shade )
{
	const Int index = player->getPlayerIndex();
	if( index < 0 || index >= MAX_PLAYER_COUNT )
		return;

	theEntries[ index ].day = playerSchemeColor( family, shade );
	theEntries[ index ].night = playerSchemeNightColor( theEntries[ index ].day );
}

// ------------------------------------------------------------------------------------------------
/** You, your allies, and everybody shooting at you.  Anyone the local player is neutral towards
	* keeps their own colour: a truce is not a side. */
static void applyRelationScheme( Player *local, Player **playable, Int count )
{
	Int allyShade = 0;
	Int enemyShade = 0;

	for( Int i = 0; i < count; ++i )
	{
		Player *player = playable[ i ];

		if( player == local )
		{
			assignScheme( player, PLAYER_COLOR_FAMILY_BLUE, 0 );
			continue;
		}

		switch( local->getRelationship( player->getDefaultTeam() ) )
		{
			case ALLIES:
				assignScheme( player, PLAYER_COLOR_FAMILY_GREEN, allyShade++ );
				break;

			case ENEMIES:
				assignScheme( player, PLAYER_COLOR_FAMILY_RED, enemyShade++ );
				break;

			default:
				break;
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** A hue per alliance.  Alliances are read off the relationship map rather than the lobby's team
	* numbers, because a script can make and break them mid-match and the lobby cannot. */
static void applyTeamScheme( Player *local, Player **playable, Int count )
{
	Int group[ MAX_PLAYER_COUNT ];
	Int groupSize[ MAX_PLAYER_COUNT ];
	Int groupCount = 0;
	Int i;

	for( i = 0; i < count; ++i )
	{
		group[ i ] = -1;
		groupSize[ i ] = 0;
	}

	for( i = 0; i < count; ++i )
	{
		if( group[ i ] >= 0 )
			continue;

		const Int mine = groupCount++;
		group[ i ] = mine;

		for( Int j = i + 1; j < count; ++j )
		{
			if( group[ j ] < 0 && playable[ i ]->getRelationship( playable[ j ]->getDefaultTeam() ) == ALLIES )
				group[ j ] = mine;
		}
	}

	// which group is drawn blue.  An observer is on nobody's side, so the first group on the roster
	// takes it - the picture still has to distinguish the sides from each other.
	Int localGroup = 0;
	for( i = 0; i < count; ++i )
	{
		if( playable[ i ] == local )
		{
			localGroup = group[ i ];
			break;
		}
	}

	Int familyOfGroup[ MAX_PLAYER_COUNT ];
	Int nextFamily = 0;
	for( i = 0; i < groupCount; ++i )
	{
		if( i == localGroup )
			familyOfGroup[ i ] = PLAYER_COLOR_FAMILY_BLUE;
		else
			familyOfGroup[ i ] = TheTeamFamilyOrder[ nextFamily++ % TheTeamFamilyCount ];
	}

	for( i = 0; i < count; ++i )
	{
		const Int mine = group[ i ];
		assignScheme( playable[ i ], familyOfGroup[ mine ], groupSize[ mine ]++ );
	}
}

// ------------------------------------------------------------------------------------------------
static void rebuildTable( void )
{
	Color wasDay[ MAX_PLAYER_COUNT ];
	Int i;

	for( i = 0; i < MAX_PLAYER_COUNT; ++i )
		wasDay[ i ] = theTableValid ? theEntries[ i ].day : 0;

	theEntryCount = (ThePlayerList != NULL) ? ThePlayerList->getPlayerCount() : 0;
	if( theEntryCount > MAX_PLAYER_COUNT )
		theEntryCount = MAX_PLAYER_COUNT;

	// start from the identity: every player wears the colour the lobby gave them
	for( i = 0; i < MAX_PLAYER_COUNT; ++i )
	{
		Player *player = (ThePlayerList != NULL && i < theEntryCount) ? ThePlayerList->getNthPlayer( i ) : NULL;

		theEntries[ i ].trueDay = (player != NULL) ? player->getPlayerColor() : 0;
		theEntries[ i ].trueNight = (player != NULL) ? player->getPlayerNightColor() : 0;
		theEntries[ i ].day = theEntries[ i ].trueDay;
		theEntries[ i ].night = theEntries[ i ].trueNight;
	}

	const Int scheme = TheGlobalData->m_playerColorScheme;

	if( scheme != PLAYER_COLORS_ORIGINAL && ThePlayerList != NULL )
	{
		Player *playable[ MAX_PLAYER_COUNT ];
		const Int count = collectPlayableSides( playable );
		Player *local = ThePlayerList->getLocalPlayer();

		// an observer has no side, so there is no "you" and no "them"; the team scheme is the one
		// that still says something, and it is what the relation scheme falls back to
		if( scheme == PLAYER_COLORS_RELATION && local != NULL && local->isPlayableSide() )
			applyRelationScheme( local, playable, count );
		else
			applyTeamScheme( local, playable, count );
	}

	theTableChanged = FALSE;
	for( i = 0; i < MAX_PLAYER_COUNT; ++i )
	{
		if( wasDay[ i ] != theEntries[ i ].day )
			theTableChanged = TRUE;
	}

	theTableValid = TRUE;
}

// ------------------------------------------------------------------------------------------------
static void ensureTable( void )
{
	// only the main thread outside the window builds the table, every pass (GameClient::update) before
	// the draw: the tick reads it beside the render, and a rebuild walks the players' relationships the
	// tick may be changing, so a change the tick made shows one pass later
	if( LogicThread::t_isLogicThread || LogicThread::isInsideWindow() )
		return;

	if( TheGlobalData == NULL )
		return;

	// at most one rebuild a frame; the shell has no frame counter running, and does not need one.
	// The frame is the logic frame, which stands still through a whole load and a whole pause, so a
	// new setting or a longer roster gets past the gate on its own. Without that, a setting changed
	// in a paused match waited for the pause to end, and a table built early in the load, before
	// the players existed, handed out black until the first logic frame.
	const Int scheme = TheGlobalData->m_playerColorScheme;
	const Int rosterSize = (ThePlayerList != NULL) ? ThePlayerList->getPlayerCount() : 0;
	if( TheGameClient != NULL )
	{
		const UnsignedInt frame = TheGameClient->getFrame();
		if( theTableValid && theTableHasFrame && theTableFrame == frame
				&& theTableScheme == scheme && theEntryCount == rosterSize )
			return;

		theTableFrame = frame;
		theTableHasFrame = TRUE;
	}
	theTableScheme = scheme;

	const UnsignedInt stamp = computeStamp();
	if( theTableValid && stamp == theTableStamp )
		return;

	theTableStamp = stamp;
	rebuildTable();
}

// ------------------------------------------------------------------------------------------------
void invalidatePlayerColorScheme( void )
{
	theTableValid = FALSE;
	theTableHasFrame = FALSE;
	theTableChanged = FALSE;
	theEntryCount = 0;
}

// ------------------------------------------------------------------------------------------------
Bool updatePlayerColorScheme( void )
{
	ensureTable();

	const Bool changed = theTableChanged;
	theTableChanged = FALSE;
	return changed;
}

// ------------------------------------------------------------------------------------------------
Color clientPlayerColor( const Player *player )
{
	if( player == NULL )
		return 0;

	if( TheGlobalData == NULL || TheGlobalData->m_playerColorScheme == PLAYER_COLORS_ORIGINAL )
		return player->getPlayerColor();

	ensureTable();

	const Int index = player->getPlayerIndex();
	if( index < 0 || index >= MAX_PLAYER_COUNT )
		return player->getPlayerColor();

	return theEntries[ index ].day;
}

// ------------------------------------------------------------------------------------------------
Color clientPlayerNightColor( const Player *player )
{
	if( player == NULL )
		return 0;

	if( TheGlobalData == NULL || TheGlobalData->m_playerColorScheme == PLAYER_COLORS_ORIGINAL )
		return player->getPlayerNightColor();

	ensureTable();

	const Int index = player->getPlayerIndex();
	if( index < 0 || index >= MAX_PLAYER_COUNT )
		return player->getPlayerNightColor();

	return theEntries[ index ].night;
}

// ------------------------------------------------------------------------------------------------
Color clientColor( Color logicColor )
{
	if( TheGlobalData == NULL || TheGlobalData->m_playerColorScheme == PLAYER_COLORS_ORIGINAL )
		return logicColor;

	ensureTable();

	// the alpha is the caller's - a floating text draws its owner's colour at the transparency it
	// chose - so only the three colour bytes are matched and replaced
	const Color rgb = logicColor & 0x00FFFFFF;
	const Color alpha = logicColor & 0xFF000000;

	for( Int i = 0; i < theEntryCount; ++i )
	{
		if( (theEntries[ i ].trueDay & 0x00FFFFFF) == rgb )
			return alpha | (theEntries[ i ].day & 0x00FFFFFF);

		if( (theEntries[ i ].trueNight & 0x00FFFFFF) == rgb )
			return alpha | (theEntries[ i ].night & 0x00FFFFFF);
	}

	return logicColor;
}
