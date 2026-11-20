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

// ObserverCamera.cpp /////////////////////////////////////////////////////////////////////////////
// The watcher's camera driven for him, and the watched player's fog.  ObserverCamera.h says which
// is which.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/LogicThread.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/ObserverCamera.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/VictoryConditions.h"

#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <string>

ObserverCamera TheObserverCamera;

/// how far round a hit the others count as the same fight: about half a screen at the default zoom
static const Real DIRECTOR_GATHER_RADIUS = 220.0f;
/// how long a hit keeps a place hot
static const UnsignedInt DIRECTOR_HEAT_FRAMES = 5 * LOGICFRAMES_PER_SECOND;
/// how often the hits are counted again
static const UnsignedInt DIRECTOR_SCAN_FRAMES = LOGICFRAMES_PER_SECOND / 2;
/// how long the director stays with a fight that is still going before it looks for a better one
/// at 12 seconds, and 30 before a slightly bigger fight would do, it sat on one spot through
/// whatever started elsewhere
static const UnsignedInt DIRECTOR_HOLD_FRAMES = 8 * LOGICFRAMES_PER_SECOND;
/// how much hotter somewhere else has to be to be worth leaving a fight that is still going
static const Real DIRECTOR_SWITCH_MARGIN = 2.0f;
/// no move sooner than this after the last one, however big the other fight; it keeps two fights
/// of a size from trading the camera back and forth
static const UnsignedInt DIRECTOR_SETTLE_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
/// this much hotter elsewhere and the director goes before its hold is up
static const Real DIRECTOR_BIG_MARGIN = 2.5f;
/// after this long on one fight any clearly hotter one elsewhere will do
static const UnsignedInt DIRECTOR_TIRED_FRAMES = 20 * LOGICFRAMES_PER_SECOND;
static const Real DIRECTOR_TIRED_MARGIN = 1.3f;
/// a fight burnt down below this share of its own peak is fading, and once settled any clearly
/// hotter one elsewhere will do, as if the director were tired of it
static const Real DIRECTOR_FADING_SHARE = 0.5f;
/// a fight's middle drifts as units die and arrive; the camera follows it only once it has gone this far
static const Real DIRECTOR_FOLLOW_SLACK = 80.0f;
/// what a special power counts for against another one; any of them outranks every fight
static const Real EVENT_WEIGHT = 1.0f;
static const Real EVENT_SUPERWEAPON_WEIGHT = 5.0f;
/// how long a special power is watched with nothing hitting the ground there yet: a superweapon's
/// missiles take a while to arrive, a bomber longer to fly in
static const UnsignedInt EVENT_FRAMES = 12 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt EVENT_SUPERWEAPON_FRAMES = 20 * LOGICFRAMES_PER_SECOND;
/// hits at an event's target in its first seconds are the fight that was there before it, not its own
static const UnsignedInt EVENT_LAUNCH_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
/// the camera stays this long after the last hit at an event's target, for the cloud and the fires
static const UnsignedInt EVENT_AFTERMATH_FRAMES = 5 * LOGICFRAMES_PER_SECOND;
/// and never longer than this after it began, however much goes on hitting round the target
static const UnsignedInt EVENT_MOST_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
/// a hit on a thing that cost this much counts twice what a free one does
static const Real DIRECTOR_COST_PER_WEIGHT = 500.0f;
/// a kill counts this many times a hit, and anything on a superweapon this many times again
static const Real DIRECTOR_KILL_FACTOR = 2.0f;
static const Real DIRECTOR_SUPERWEAPON_FACTOR = 3.0f;
/// and a hit on infantry this share of it: a few riflemen trading shots took the camera from tanks
static const Real DIRECTOR_INFANTRY_FACTOR = 0.25f;
/// with no fight on, how long the director looks at one army, base or building site
static const UnsignedInt DIRECTOR_SIGHT_FRAMES = 9 * LOGICFRAMES_PER_SECOND;
/// how many of the last sights the director will not go back to while there is another
static const size_t DIRECTOR_SEEN_COUNT = 3;
/// further apart than this the camera cuts rather than glides: a glide across most of a map is too
/// long to sit through and shows nothing
static const Real CUT_DISTANCE = 1600.0f;
/// the camera found further than this from where it was put was moved by something else, a click on
/// the production strip or a jump to a group.  Where it was put is taken after the view's own
/// constraint, so the edge of the map pulling it back does not count.  The radar
/// hands the camera over itself, since a click near the camera and a drag both move it by less
static const Real HAND_JUMP_DISTANCE = 400.0f;
/// roughly how long the director's glide takes to arrive, easing in and out
static const Real DIRECTOR_PAN_SECONDS = 1.4f;
/// how long the director's height takes to settle on a new place's: the view's own settle took a
/// third of a second, and a camera gliding in over seconds stepped back up in that third
static const Real DIRECTOR_HEIGHT_SECONDS = 2.2f;
/// and the view under it settles on that height and the ground below on this time constant: its own
/// third of a second stepped the zoom in and out over every ridge a glide crossed
static const Real DIRECTOR_SETTLE_SECONDS = 1.0f;
/// every place the director shows is watched from this much over the watcher's own height, so the
/// fight's surroundings are in the picture and not only the unit the hits came from; 300 at the
/// start height framed one tank and missed the fight beside it
static const Real DIRECTOR_WIDE_EXTRA = 200.0f;
/// the score bar sets this many players' cards and more in two rows: eight in one were 6 pixel text
/// at 720p
static const Int CARD_TWO_ROWS_FROM = 5;
/// the longest a second fight stays up, from when it came; a planned one was held 5300 frames on an
/// eight-player map while the director went from fight to fight beside it
static const UnsignedInt SPLIT_MOST_FRAMES = 18 * LOGICFRAMES_PER_SECOND;
/// a zoom moving more than this share of itself between two updates is logged as a jump
static const Real ZOOM_JUMP_SHARE = 0.02f;
/// the director's glide never crosses the ground faster than this, about two screens a second
static const Real DIRECTOR_TOP_SPEED = 900.0f;
/// a fight this tight is watched from the watcher's own height; wider, the camera rises this much
/// for every unit further, up to the most.  A fight's hits lie within DIRECTOR_GATHER_RADIUS of one
/// another, so the most is reached only by the widest
static const Real DIRECTOR_TIGHT_SPREAD = 60.0f;
static const Real DIRECTOR_HEIGHT_PER_SPREAD = 1.5f;
static const Real DIRECTOR_MOST_EXTRA_HEIGHT = 300.0f;
/// a player's camera comes a few times a second, and this smooths the steps between
static const Real PLAYER_PAN_SECONDS = 0.15f;
static const Real PLAYER_TOP_SPEED = 4000.0f;
/// a frame longer than this is a hitch, and is not allowed to throw the camera across the map
static const UnsignedInt LONGEST_STEP_MILLISECONDS = 100;
static const Real MILLISECONDS_PER_SECOND = 1000.0f;
/// -directorrecord: a second fight closer than this to the first shares its ground, and the corner
/// window would show the director's own fight; the least, raised to the ground off the picture
static const Real SPLIT_APART = 3.0f * DIRECTOR_GATHER_RADIUS;
/// a second fight comes only further away than that, and goes once it comes within this share of it
static const Real SPLIT_SAME_GROUND_SHARE = 2.0f / 3.0f;
/// a second fight is taken at least this hot, and at least this share of the first
static const Real SPLIT_ENTER_HEAT = 2.0f;
static const Real SPLIT_ENTER_SHARE = 0.5f;
/// it is held through its least hold, and after that while it keeps this share of the first
static const UnsignedInt SPLIT_HOLD_FRAMES = 6 * LOGICFRAMES_PER_SECOND;
static const Real SPLIT_STAY_SHARE = 0.25f;
/// once one goes, the next waits this long, so the window does not flicker between the two
static const UnsignedInt SPLIT_REST_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
/// a second fight, once on, stays on this long whatever it does, unless it comes to show the
/// director's own ground: the 12, 15 and 21 retail frames the split screen's radar, panes and lines
/// took to come in, then the hold (the director is a viewer's presentation: every one of its frame
/// counts is wall time, as its seconds are)
static const UnsignedInt SPLIT_LEAST_FRAMES = ConvertRetailFramesWall( 12U ) + ConvertRetailFramesWall( 15U ) + ConvertRetailFramesWall( 21U )
	+ SPLIT_HOLD_FRAMES;
/// a second fight the timeline plans comes this long before it begins: at four seconds the window sat
/// on an empty bridge
static const UnsignedInt SPLIT_LEAD_FRAMES = 3 * LOGICFRAMES_PER_SECOND;
/// the score bar's frame is sized off a gold line a pixel wide for every this many rows of the picture
static const Real PANE_LINE_ROWS_A_PIXEL = 120.0f;
/// and off a soft band of the brand's blue this many times that line's width
static const Int PANE_BAND_LINES = 5;
/// the scouting pass: a crowd of hits this near a fight still going is more of that fight
static const Real SCOUT_SAME_FIGHT = 2.0f * DIRECTOR_GATHER_RADIUS;
/// a fight with no crowd near it for this long is over; its hits stay hot DIRECTOR_HEAT_FRAMES on top
static const UnsignedInt SCOUT_FIGHT_GAP = 3 * LOGICFRAMES_PER_SECOND;
/// a fight is worth filming once one scan of it was this hot and it ran this long, hot tail and all;
/// a jeep shot up on its way past is neither
static const Real FIGHT_WORTH_HEAT = 4.0f;
static const UnsignedInt FIGHT_WORTH_FRAMES = 8 * LOGICFRAMES_PER_SECOND;
/// the director goes to wait at a fight this long before it begins: the glide takes about a second
/// and a half of it
static const UnsignedInt DIRECTOR_PREROLL_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
/// both passes take the logic's CRC this often, and a filming pass whose CRC differs from the
/// scouting pass's is not playing the match the timeline describes
static const UnsignedInt SCOUT_CRC_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
/// the broadcast's power flags drop and go back up over the first many logic frames; the defeat and
/// winner banners open and close over the second, the rule, the panel and the words one after another
static const UnsignedInt FLAG_MOVE_FRAMES = ConvertRetailFramesWall( 15U );
static const UnsignedInt BANNER_MOVE_FRAMES = ConvertRetailFramesWall( 24U );
/// a power's flag hangs under its player's card this long when nothing waits behind it, a defeated
/// player's banner holds this long
static const UnsignedInt FLAG_HOLD_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt DEFEAT_BANNER_HOLD_FRAMES = 3 * LOGICFRAMES_PER_SECOND;
/// a defeated player's card flashes red over the first of these, a line is drawn through it from the
/// second over the third, it is seen struck until the fourth after the defeat and then collapses over
/// the last while the others slide together
static const UnsignedInt CARD_FLASH_FRAMES = ConvertRetailFramesWall( 12U );
static const UnsignedInt CARD_STRIKE_FROM = ConvertRetailFramesWall( 6U );
static const UnsignedInt CARD_STRIKE_FRAMES = ConvertRetailFramesWall( 12U );
static const UnsignedInt CARD_STRUCK_FRAMES = ConvertRetailFramesWall( 36U );
static const UnsignedInt CARD_COLLAPSE_FRAMES = ConvertRetailFramesWall( 18U );
/// the winner's banner starts to come in this long after the match is decided, as the last defeated
/// player's banner over it has opened: the film runs 105 frames past the decision, and at 30 the winner's
/// was all in for only 51 of them
static const UnsignedInt WINNER_DELAY_FRAMES = ConvertRetailFramesWall( 24U );

/// a power further than this share of the picture's width of ground from where the director looks is
/// off its picture, or at its very edge
static const Real OFF_PICTURE_SHARE = 0.4f;

/// -directorrecord's corner window: how long it takes to slide in or out, which it does in full before
/// it turns.  Its plate's sizes and its gap to the picture's edges are a 720 row picture's, grown with
/// the picture's height
static const UnsignedInt WINDOW_SLIDE_FRAMES = ConvertRetailFramesWall( 15U );
static const Real WINDOW_ROWS = 720.0f;
static const Real WINDOW_MARGIN = 16.0f;
static const Real WINDOW_BORDER = 4.0f;
static const Real WINDOW_HEADER = 22.0f;
static const Real WINDOW_RULE = 2.0f;
static const Real WINDOW_HIGHLIGHT = 2.0f;
/// the events file's lost: a unit or structure that cost at least this much
static const Int FILM_LOST_LEAST_COST = 1000;

/// an instant replay shows this long before the power hit and this long after
static const UnsignedInt REPLAY_BEFORE_FRAMES = 2 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt REPLAY_AFTER_FRAMES = 6 * LOGICFRAMES_PER_SECOND;
/// it goes into the film on the first calm frame after it, or this long after it whatever goes on
static const UnsignedInt REPLAY_SPLICE_WAIT_FRAMES = 10 * LOGICFRAMES_PER_SECOND;
/// the replay pass draws this long before each replay unrecorded, so the camera, the animations and the
/// smooth motion's blend have settled when its first picture is taken
static const UnsignedInt REPLAY_LEAD_FRAMES = 2 * LOGICFRAMES_PER_SECOND;
/// the replay's camera starts this much over the director's height and comes down to it, a slow push in
static const Real REPLAY_PUSH_HEIGHT = 120.0f;

static Bool within( const Coord2D &a, const Coord2D &b, Real distance );

//-------------------------------------------------------------------------------------------------
static Bool sameFight( const Coord2D &a, const Coord2D &b )
{
	const Real dx = a.x - b.x;
	const Real dy = a.y - b.y;
	return dx * dx + dy * dy <= DIRECTOR_GATHER_RADIUS * DIRECTOR_GATHER_RADIUS;
}

//-------------------------------------------------------------------------------------------------
/** A power the player fires rather than one of his units: a superweapon, a general's power, or one
	* fired from a building, the command centre's spy satellite.  NULL, a warhead no power sent, is none. */
//-------------------------------------------------------------------------------------------------
static Bool playersPower( const SpecialPowerTemplate *power, const ThingTemplate *sourceThing, Bool superweapon )
{
	if( power == NULL )
		return FALSE;
	return superweapon || power->getRequiredScience() != SCIENCE_INVALID || ( sourceThing != NULL && sourceThing->isKindOf( KINDOF_STRUCTURE ) );
}

//-------------------------------------------------------------------------------------------------
/** The name a player reads for a power.  A SpecialPowerTemplate carries none: the name is on the
	* command button that fires it, as its picture is (InGameUI's powerButton), else the name of what
	* fired it.  Empty for a warhead no power sent. */
//-------------------------------------------------------------------------------------------------
static UnicodeString powerShownName( const SpecialPowerTemplate *power, const ThingTemplate *sourceThing )
{
	if( power != NULL && TheControlBar != NULL )
	{
		for( const CommandButton *button = TheControlBar->getCommandButtons(); button != NULL; button = button->getNext() )
		{
			if( button->getSpecialPowerTemplate() == power && !button->getTextLabel().isEmpty() )
				return TheGameText->fetch( button->getTextLabel() );
		}
	}
	return sourceThing != NULL ? sourceThing->getDisplayName() : UnicodeString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_hitWeight( Int cost, Bool killed, Bool superweapon, Bool infantry )
{
	Real weight = 1.0f + cost / DIRECTOR_COST_PER_WEIGHT;
	if( killed )
		weight *= DIRECTOR_KILL_FACTOR;
	if( superweapon )
		weight *= DIRECTOR_SUPERWEAPON_FACTOR;
	if( infantry )
		weight *= DIRECTOR_INFANTRY_FACTOR;
	return weight;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_sightWeight( Int cost, Bool structure, Bool busy, Bool superweapon )
{
	Real weight = cost / DIRECTOR_COST_PER_WEIGHT;
	if( busy )
		weight *= 2.0f;
	else if( structure )
		weight *= 0.5f;
	if( superweapon )
		weight *= DIRECTOR_SUPERWEAPON_FACTOR;
	return weight;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_nextSight( const std::vector< DirectorHeat > &sights, const std::vector< Coord2D > &seen, Coord2D *place )
{
	std::vector< DirectorHeat > fresh;
	for( size_t index = 0; index < sights.size(); index++ )
	{
		Bool wasSeen = FALSE;
		for( size_t look = 0; look < seen.size() && !wasSeen; look++ )
			wasSeen = sameFight( sights[ index ].position, seen[ look ] );
		if( !wasSeen )
			fresh.push_back( sights[ index ] );
	}
	Real heat = 0.0f;
	return ObserverCamera_hottestPlace( fresh, place, &heat );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_heatAround( const std::vector< DirectorHeat > &hits, const Coord2D &around, Coord2D *middle )
{
	Real heat = 0.0f;
	Coord2D sum = { 0.0f, 0.0f };
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const DirectorHeat &hit = hits[ index ];
		if( !sameFight( hit.position, around ) )
			continue;

		heat += hit.weight;
		sum.x += hit.position.x * hit.weight;
		sum.y += hit.position.y * hit.weight;
	}

	*middle = around;
	if( heat > 0.0f )
	{
		middle->x = sum.x / heat;
		middle->y = sum.y / heat;
	}
	return heat;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_hottestPlace( const std::vector< DirectorHeat > &hits, Coord2D *place, Real *heat )
{
	// ponytail: every hit against every other, fine for the few hundred a big fight makes in five
	// seconds; a grid of cells if a match ever gets to thousands
	*heat = 0.0f;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		Coord2D middle;
		const Real around = ObserverCamera_heatAround( hits, hits[ index ].position, &middle );
		if( around > *heat )
		{
			*heat = around;
			*place = middle;
		}
	}
	return *heat > 0.0f;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_shouldMove( Real heatHere, Real heatThere, UnsignedInt framesHere, Real peakHere )
{
	if( heatHere <= 0.0f )
		return heatThere > 0.0f;
	if( framesHere < DIRECTOR_SETTLE_FRAMES )
		return FALSE;
	if( heatThere > heatHere * DIRECTOR_BIG_MARGIN )
		return TRUE;
	const Bool fading = heatHere < peakHere * DIRECTOR_FADING_SHARE;
	if( fading )
		return heatThere > heatHere * DIRECTOR_TIRED_MARGIN;
	if( framesHere < DIRECTOR_HOLD_FRAMES )
		return FALSE;
	const Real margin = framesHere >= DIRECTOR_TIRED_FRAMES ? DIRECTOR_TIRED_MARGIN : DIRECTOR_SWITCH_MARGIN;
	return heatThere > heatHere * margin;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_sameUse( const DirectorEvent &event, const Player *owner, const SpecialPowerTemplate *power, const Coord2D &target,
	UnsignedInt frame )
{
	return power != NULL && event.power == power && event.owner == owner && frame < event.until && sameFight( event.target, target );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_stayOnEvent( const DirectorEvent *current, const DirectorEvent *best )
{
	if( current == NULL )
		return FALSE;
	if( best == NULL || best == current || sameFight( best->target, current->target ) )
		return TRUE;
	return best->weight < current->weight;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt ObserverCamera_eventKeptTo( const DirectorEvent &event, UnsignedInt frame, UnsignedInt more )
{
	return min( max( event.until, frame + more ), event.since + EVENT_MOST_FRAMES );
}

//-------------------------------------------------------------------------------------------------
/** The owner's rule: a scan never cuts.  Nor does any power that hurts nobody where it is aimed: a
	* paradrop or an ambush brings units whose fight takes the camera on its own once it starts. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_powerLooksOnly( SpecialPowerType type )
{
	switch( type )
	{
		case SPECIAL_SPY_SATELLITE:
		case SPECIAL_RADAR_VAN_SCAN:
		case SPECIAL_SPY_DRONE:
		case SPECIAL_CIA_INTELLIGENCE:
		case SPECIAL_COMMUNICATIONS_DOWNLOAD:
		case SPECIAL_CASH_HACK:
		case SPECIAL_BLACKLOTUS_STEAL_CASH_HACK:
		case SPECIAL_CASH_BOUNTY:
		case SPECIAL_LEAFLET_DROP:
		case EARLY_SPECIAL_LEAFLET_DROP:
		case SPECIAL_HACKER_DISABLE_BUILDING:
		case SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK:
		case SPECIAL_BLACKLOTUS_CAPTURE_BUILDING:
		case SPECIAL_INFANTRY_CAPTURE_BUILDING:
		case SPECIAL_DEFECTOR:
		case SPECIAL_GPS_SCRAMBLER:
		case SLTH_SPECIAL_GPS_SCRAMBLER:
		case SPECIAL_DISGUISE_AS_VEHICLE:
		case SPECIAL_BOOBY_TRAP:
		case SPECIAL_REPAIR_VEHICLES:
		case EARLY_SPECIAL_REPAIR_VEHICLES:
		case SPECIAL_FRENZY:
		case EARLY_SPECIAL_FRENZY:
		case SPECIAL_CHANGE_BATTLE_PLANS:
		case SPECIAL_PARADROP_AMERICA:
		case INFA_SPECIAL_PARADROP_AMERICA:
		case SPECIAL_TANK_PARADROP:
		case SPECIAL_AMBUSH:
		case SPECIAL_SNEAK_ATTACK:
		case SPECIAL_CRATE_DROP:
		case SPECIAL_TERROR_CELL:
		case SPECIAL_CLEANUP_AREA:
			return TRUE;
		default:
			return FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_landsNow( const DirectorEvent &event, UnsignedInt frame, Bool timelineKnown, Bool fightNear )
{
	if( event.looksOnly )
		return FALSE;
	if( event.landsAt != 0 )
		return frame + DIRECTOR_PREROLL_FRAMES >= event.landsAt;
	return !timelineKnown && fightNear;
}

//-------------------------------------------------------------------------------------------------
/** The gather radius at the least: before the picture's ground is first measured, screenGround is 0. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_offPicture( const Coord2D &target, const Coord2D &place, Real screenGround )
{
	return !within( target, place, max( DIRECTOR_GATHER_RADIUS, screenGround * OFF_PICTURE_SHARE ) );
}

//-------------------------------------------------------------------------------------------------
/** A power only had a pane once it landed further from the shown one than a split's two panes are
	* apart, which is most of a picture's width again: one fired on a superweapon's frame across the same
	* base was off the superweapon's picture and inside that distance, and got nothing. */
//-------------------------------------------------------------------------------------------------
UnsignedInt ObserverCamera_secondPower( const std::vector< DirectorEvent > &events, UnsignedInt shown, UnsignedInt held,
	const Coord2D &place, Real screenGround )
{
	const DirectorEvent *best = NULL;
	for( size_t index = 0; index < events.size(); index++ )
	{
		const DirectorEvent &event = events[ index ];
		if( !event.landed || event.id == shown || !ObserverCamera_offPicture( event.target, place, screenGround ) )
			continue;
		if( best == NULL || event.weight > best->weight || ( event.weight == best->weight && best->id != held ) )
			best = &event;
	}
	return best != NULL ? best->id : 0;
}

//-------------------------------------------------------------------------------------------------
/** A power outranks a fight in the window as it does on the whole picture. */
//-------------------------------------------------------------------------------------------------
DirectorWindowKind ObserverCamera_pickWindow( UnsignedInt secondPower, Bool secondFight )
{
	if( secondPower != 0 )
		return DIRECTOR_WINDOW_POWER;
	return secondFight ? DIRECTOR_WINDOW_FIGHT : DIRECTOR_WINDOW_NONE;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_holdWindow( Bool on, Bool wanted, UnsignedInt framesSince )
{
	if( on )
		return wanted || framesSince < WINDOW_SLIDE_FRAMES;
	return wanted && framesSince >= WINDOW_SLIDE_FRAMES;
}

//-------------------------------------------------------------------------------------------------
DirectorWindowFrame ObserverCamera_windowFrame( Int height )
{
	const Real scale = height / WINDOW_ROWS;
	DirectorWindowFrame frame;
	frame.border = max( REAL_TO_INT( WINDOW_BORDER * scale ), 2 );
	frame.header = max( REAL_TO_INT( WINDOW_HEADER * scale ), 8 );
	frame.rule = max( REAL_TO_INT( WINDOW_RULE * scale ), 1 );
	frame.cut = frame.header;
	frame.highlight = max( REAL_TO_INT( WINDOW_HIGHLIGHT * scale ), 1 );
	return frame;
}

//-------------------------------------------------------------------------------------------------
/** The lower left is kept clear for the event map that goes there later, so the window takes the
	* lower right, and slides in from past the right edge with its whole plate. */
//-------------------------------------------------------------------------------------------------
IRegion2D ObserverCamera_windowRect( Int width, Int height, Real progress )
{
	IRegion2D rect;
	rect.lo.x = rect.lo.y = rect.hi.x = rect.hi.y = 0;
	if( progress <= 0.0f )
		return rect;
	const DirectorWindowFrame frame = ObserverCamera_windowFrame( height );
	const Int windowWidth = width / OBSERVER_WINDOW_SCALE;
	const Int windowHeight = height / OBSERVER_WINDOW_SCALE;
	const Int margin = max( REAL_TO_INT( WINDOW_MARGIN * height / WINDOW_ROWS ), 1 );
	const Int slide = REAL_TO_INT( ( 1.0f - min( progress, 1.0f ) ) * ( windowWidth + 2 * frame.border + margin ) );
	rect.hi.x = width - margin - frame.border + slide;
	rect.lo.x = rect.hi.x - windowWidth;
	rect.hi.y = height - margin - frame.border;
	rect.lo.y = rect.hi.y - windowHeight;
	return rect;
}

//-------------------------------------------------------------------------------------------------
/** Places are written with no decimals: a cell is ten units and nothing reads finer. */
//-------------------------------------------------------------------------------------------------
AsciiString ObserverCamera_formatFilmEvent( const DirectorFilmEvent &event )
{
	AsciiString line;
	line.format( "event %u %s %d %.0f %.0f %s %s", event.frame, event.type.str(), event.player, event.x, event.y,
		event.thing.isEmpty() ? "-" : event.thing.str(), event.name.isEmpty() ? "-" : event.name.str() );
	return line;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_parseFilmEvent( const char *line, DirectorFilmEvent *event )
{
	char type[ 32 ];
	char thing[ 128 ];
	Int nameAt = 0;
	if( sscanf( line, "event %u %31s %d %f %f %127s %n", &event->frame, type, &event->player, &event->x, &event->y, thing, &nameAt ) != 6
		|| nameAt == 0 )
		return FALSE;
	std::string name( line + nameAt );
	while( !name.empty() && ( name.back() == '\n' || name.back() == '\r' ) )
		name.pop_back();
	if( name.empty() )
		return FALSE;
	event->type = type;
	event->thing = thing;
	event->name = name.c_str();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A name as one field of a line, UTF-8: the hotkey's '&' a button label carries dropped and a line
	* break made a space, "-" for none. */
//-------------------------------------------------------------------------------------------------
AsciiString ObserverCamera_filmName( const UnicodeString &shown )
{
	UnicodeString name;
	for( Int index = 0; index < shown.getLength(); index++ )
	{
		const WideChar letter = shown.getCharAt( index );
		if( letter != u'&' )
			name.concat( letter == u'\n' || letter == u'\r' || letter == u'\t' ? (WideChar)u' ' : letter );
	}
	name.trim();
	return name.isEmpty() ? AsciiString( "-" ) : AsciiString( WideCharAsUtf8( name.str() ).str() );
}

//-------------------------------------------------------------------------------------------------
/** A scan, a paradrop or a unit's own ability is no big moment, and one that hurt nobody has nothing
	* to replay. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_missedBigMoment( Bool superweapon, Bool generalsPower, Bool landed, Bool shown )
{
	return ( superweapon || generalsPower ) && landed && !shown;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera_replayRange( UnsignedInt landed, UnsignedInt *first, UnsignedInt *last )
{
	*first = landed > REPLAY_BEFORE_FRAMES ? landed - REPLAY_BEFORE_FRAMES : 1;
	*last = landed + REPLAY_AFTER_FRAMES;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_spliceNow( UnsignedInt frame, UnsignedInt last, Bool calm )
{
	return frame > last && ( calm || frame >= last + REPLAY_SPLICE_WAIT_FRAMES );
}

//-------------------------------------------------------------------------------------------------
/** The power's name goes last; template names hold no space. */
//-------------------------------------------------------------------------------------------------
AsciiString ObserverCamera_formatReplay( const DirectorReplay &replay )
{
	AsciiString line;
	line.format( "replay %u %u %u %.0f %.0f %d %u %s", replay.first, replay.last, replay.landed, replay.target.x, replay.target.y,
		replay.player, replay.splice, replay.power.isEmpty() ? "-" : replay.power.str() );
	return line;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_parseReplay( const char *line, DirectorReplay *replay )
{
	char power[ 128 ];
	if( sscanf( line, "replay %u %u %u %f %f %d %u %127s", &replay->first, &replay->last, &replay->landed, &replay->target.x,
			&replay->target.y, &replay->player, &replay->splice, power ) != 8 || replay->last < replay->first )
		return FALSE;
	replay->power = power;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Two powers landing at once both missed overlap: one pass of the match plays a frame once, so the
	* later starts where the earlier ends and keeps its own landing.  ponytail: one landing inside the
	* earlier replay's frames is dropped; cutting the earlier one short for it would keep both. */
//-------------------------------------------------------------------------------------------------
static Bool replayStartsSooner( const DirectorReplay &a, const DirectorReplay &b )
{
	return a.first < b.first || ( a.first == b.first && a.landed < b.landed );
}

void ObserverCamera_planReplays( std::vector< DirectorReplay > &replays )
{
	std::stable_sort( replays.begin(), replays.end(), replayStartsSooner );
	size_t kept = 0;
	for( size_t index = 0; index < replays.size(); index++ )
	{
		DirectorReplay replay = replays[ index ];
		if( kept > 0 && replay.first <= replays[ kept - 1 ].last )
			replay.first = replays[ kept - 1 ].last + 1;
		if( replay.first > replay.landed || replay.first > replay.last )
			continue;
		replays[ kept++ ] = replay;
	}
	replays.resize( kept );
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_replayAt( const std::vector< DirectorReplay > &replays, UnsignedInt frame, UnsignedInt lead )
{
	for( size_t index = 0; index < replays.size(); index++ )
	{
		if( frame <= replays[ index ].last && frame + lead >= replays[ index ].first )
			return (Int)index;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt ObserverCamera_filmPicture( UnsignedInt frame, UnsignedInt start, Int pictures )
{
	return frame > start ? ( frame - start ) * (UnsignedInt)pictures : 0;
}

//-------------------------------------------------------------------------------------------------
AsciiString ObserverCamera_formatReplayClip( Int index, UnsignedInt firstPicture, UnsignedInt pictures, UnsignedInt splicePicture,
	const DirectorReplay &replay )
{
	AsciiString line;
	line.format( "clip %d %u %u %u %u %u %d %s", index, firstPicture, pictures, splicePicture, replay.first, replay.last, replay.player,
		replay.power.isEmpty() ? "-" : replay.power.str() );
	return line;
}

/// the path WinMain reads once the engine is gone, so a plain buffer and no AsciiString
static char s_replaysWritten[ 1024 ] = "";

const char *ObserverCamera_replaysWritten( void )
{
	return s_replaysWritten;
}

//-------------------------------------------------------------------------------------------------
AsciiString ObserverCamera_formatFilmMap( const char *mapFile, Real loX, Real loY, Real hiX, Real hiY )
{
	AsciiString line;
	line.format( "map %s %.0f %.0f %.0f %.0f", mapFile, loX, loY, hiX, hiY );
	return line;
}

//-------------------------------------------------------------------------------------------------
/** A map's file name may hold spaces, "Tournament Desert", so the extent is read off the end. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_parseFilmMap( const char *line, AsciiString *mapFile, Real *loX, Real *loY, Real *hiX, Real *hiY )
{
	if( strncmp( line, "map ", 4 ) != 0 )
		return FALSE;
	std::string text( line + 4 );
	while( !text.empty() && isspace( (unsigned char)text.back() ) )
		text.pop_back();
	Real *values[ 4 ] = { hiY, hiX, loY, loX };
	for( Int value = 0; value < 4; value++ )
	{
		const size_t space = text.find_last_of( ' ' );
		if( space == std::string::npos || sscanf( text.c_str() + space + 1, "%f", values[ value ] ) != 1 )
			return FALSE;
		text.erase( space );
	}
	if( text.empty() )
		return FALSE;
	*mapFile = text.c_str();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
AsciiString ObserverCamera_formatFilmPlayer( Int index, const char *playerTemplate, UnsignedInt rgb, const char *displayName )
{
	AsciiString line;
	line.format( "player %d %s %06X %s", index, playerTemplate, rgb & 0xFFFFFFu, displayName );
	return line;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_parseFilmPlayer( const char *line, Int *index, AsciiString *playerTemplate, UnsignedInt *rgb, AsciiString *displayName )
{
	char name[ 128 ];
	Int nameAt = 0;
	if( sscanf( line, "player %d %127s %x %n", index, name, rgb, &nameAt ) != 3 || nameAt == 0 )
		return FALSE;
	*playerTemplate = name;
	std::string rest( line + nameAt );
	while( !rest.empty() && ( rest.back() == '\n' || rest.back() == '\r' ) )
		rest.pop_back();
	*displayName = rest.c_str();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_spreadAround( const std::vector< DirectorHeat > &hits, const Coord2D &around )
{
	Real weight = 0.0f;
	Real squares = 0.0f;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const DirectorHeat &hit = hits[ index ];
		if( !sameFight( hit.position, around ) )
			continue;

		const Real dx = hit.position.x - around.x;
		const Real dy = hit.position.y - around.y;
		weight += hit.weight;
		squares += ( dx * dx + dy * dy ) * hit.weight;
	}
	return weight > 0.0f ? sqrtf( squares / weight ) : 0.0f;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_fightHeight( Real spread )
{
	const Real extra = ( spread - DIRECTOR_TIGHT_SPREAD ) * DIRECTOR_HEIGHT_PER_SPREAD;
	return min( max( extra, 0.0f ), DIRECTOR_MOST_EXTRA_HEIGHT );
}

//-------------------------------------------------------------------------------------------------
/** Each axis on its own: the lowest corner may not go below the map's low edge, nor the highest
	* past its high one.  When the screen is wider than the map the two bounds cross, and anywhere
	* between them shows as little off the map as the screen can. */
//-------------------------------------------------------------------------------------------------
static Real keepAxisInMap( Real place, Real lowestCorner, Real highestCorner, Real mapLow, Real mapHigh )
{
	const Real low = mapLow - lowestCorner;
	const Real high = mapHigh - highestCorner;
	return min( max( place, min( low, high ) ), max( low, high ) );
}

//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera_keepInMap( const Coord2D &place, const Coord2D *corners, Int count, const Region2D &map )
{
	Coord2D lowest = corners[ 0 ];
	Coord2D highest = corners[ 0 ];
	for( Int index = 1; index < count; index++ )
	{
		lowest.x = min( lowest.x, corners[ index ].x );
		lowest.y = min( lowest.y, corners[ index ].y );
		highest.x = max( highest.x, corners[ index ].x );
		highest.y = max( highest.y, corners[ index ].y );
	}

	Coord2D kept;
	kept.x = keepAxisInMap( place.x, lowest.x, highest.x, map.lo.x, map.hi.x );
	kept.y = keepAxisInMap( place.y, lowest.y, highest.y, map.lo.y, map.hi.y );
	return kept;
}

//-------------------------------------------------------------------------------------------------
/** An angle's shortest way round to another, so a camera facing just west of north turns a few
	* degrees to just east of it rather than all the way back round. */
//-------------------------------------------------------------------------------------------------
static Real shortestTurn( Real from, Real to )
{
	Real turn = to - from;
	while( turn > PI )
		turn -= 2.0f * PI;
	while( turn < -PI )
		turn += 2.0f * PI;
	return turn;
}

//-------------------------------------------------------------------------------------------------
/** One axis of a critically damped spring, in the closed form game cameras use (Game Programming
	* Gems 4, 1.10): from rest it gathers speed, then slows into the target without passing it. */
//-------------------------------------------------------------------------------------------------
static Real springTowards( Real from, Real to, Real *velocity, Real smoothSeconds, Real elapsedSeconds )
{
	const Real omega = 2.0f / smoothSeconds;
	const Real x = omega * elapsedSeconds;
	const Real decay = 1.0f / ( 1.0f + x + 0.48f * x * x + 0.235f * x * x * x );
	const Real change = from - to;
	const Real pull = ( *velocity + omega * change ) * elapsedSeconds;
	*velocity = ( *velocity - omega * pull ) * decay;
	Real result = to + ( change + pull ) * decay;
	if( ( to - from > 0.0f ) == ( result > to ) )
	{
		result = to;
		*velocity = 0.0f;
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
ViewLocation ObserverCamera_approach( const ViewLocation &from, const ViewLocation &to, Real elapsedSeconds, Real smoothSeconds,
	Real topSpeed, ObserverCameraVelocity *velocity )
{
	const Coord3D &start = from.getPosition();
	const Coord3D &end = to.getPosition();
	Real dx = end.x - start.x;
	Real dy = end.y - start.y;
	const Real distance = sqrtf( dx * dx + dy * dy );
	if( distance > CUT_DISTANCE )
	{
		velocity->x = velocity->y = velocity->z = velocity->angle = velocity->pitch = velocity->zoom = 0.0f;
		return to;
	}

	// a long way is closed on a point that runs at most this far ahead, which holds the middle of the
	// glide to a steady speed instead of a lunge
	const Real reach = topSpeed * smoothSeconds;
	if( distance > reach )
	{
		dx *= reach / distance;
		dy *= reach / distance;
	}

	const Real angle = from.getAngle();
	ViewLocation step;
	step.init( springTowards( start.x, start.x + dx, &velocity->x, smoothSeconds, elapsedSeconds ),
						 springTowards( start.y, start.y + dy, &velocity->y, smoothSeconds, elapsedSeconds ),
						 springTowards( start.z, end.z, &velocity->z, smoothSeconds, elapsedSeconds ),
						 springTowards( angle, angle + shortestTurn( angle, to.getAngle() ), &velocity->angle, smoothSeconds, elapsedSeconds ),
						 springTowards( from.getPitch(), to.getPitch(), &velocity->pitch, smoothSeconds, elapsedSeconds ),
						 springTowards( from.getZoom(), to.getZoom(), &velocity->zoom, smoothSeconds, elapsedSeconds ) );
	return step;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_easeHeight( Real from, Real to, Real *velocity, Real elapsedSeconds, Bool cut )
{
	if( cut )
	{
		*velocity = 0.0f;
		return to;
	}
	return springTowards( from, to, velocity, DIRECTOR_HEIGHT_SECONDS, elapsedSeconds );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_secondPlace( const std::vector< DirectorHeat > &hits, const Coord2D &first, Real needed, Coord2D *place, Real *heat )
{
	std::vector< DirectorHeat > apart;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const Real dx = hits[ index ].position.x - first.x;
		const Real dy = hits[ index ].position.y - first.y;
		if( dx * dx + dy * dy > needed * needed )
			apart.push_back( hits[ index ] );
	}
	return ObserverCamera_hottestPlace( apart, place, heat );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_holdSplit( Bool split, Real firstHeat, Real secondHeat, Real apart, Real needed, UnsignedInt framesSince )
{
	// a window showing the director's own fight is no second fight at all, however new
	if( apart <= needed * SPLIT_SAME_GROUND_SHARE )
		return FALSE;
	if( split )
		return framesSince < SPLIT_LEAST_FRAMES || ( secondHeat > 0.0f && secondHeat >= firstHeat * SPLIT_STAY_SHARE );
	return apart > needed && framesSince >= SPLIT_REST_FRAMES && firstHeat > 0.0f && secondHeat >= SPLIT_ENTER_HEAT
		&& secondHeat >= firstHeat * SPLIT_ENTER_SHARE;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_cardRow( const std::vector< Int > &sizes, Int cardWidth, Int cardGap, Int versusWidth,
	std::vector< Int > *cardLefts, std::vector< Int > *blockLefts )
{
	cardLefts->clear();
	blockLefts->clear();
	Int at = 0;
	for( size_t block = 0; block < sizes.size(); block++ )
	{
		if( block > 0 )
			at += versusWidth;
		blockLefts->push_back( at );
		for( Int card = 0; card < sizes[ block ]; card++ )
		{
			if( card > 0 )
				at += cardGap;
			cardLefts->push_back( at );
			at += cardWidth;
		}
	}
	return at;
}

//-------------------------------------------------------------------------------------------------
std::vector< Int > ObserverCamera_cardRows( const std::vector< Int > &sizes )
{
	std::vector< Int > rows( sizes.size(), 0 );
	Int cards = 0;
	for( size_t block = 0; block < sizes.size(); block++ )
		cards += sizes[ block ];
	if( cards < CARD_TWO_ROWS_FROM )
		return rows;
	// blocks in order into the first row until it holds half the cards, the rest into the second; a
	// block that would carry the first row further past half than it is short goes down instead
	Int first = 0;
	Bool second = FALSE;
	for( size_t block = 0; block < sizes.size(); block++ )
	{
		if( block > 0 && !second && first + sizes[ block ] - cards / 2 > cards / 2 - first )
			second = TRUE;
		rows[ block ] = second ? 1 : 0;
		if( !second )
			first += sizes[ block ];
	}
	return rows;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_cardStep( const std::vector< Int > &sizes, const Int *cardWidths, const Int *cardGaps,
	const Int *versusWidths, Int steps, Int room )
{
	std::vector< Int > cardLefts, blockLefts;
	for( Int step = 0; step < steps; step++ )
		if( ObserverCamera_cardRow( sizes, cardWidths[ step ], cardGaps[ step ], versusWidths[ step ], &cardLefts, &blockLefts ) <= room )
			return step;
	return steps - 1;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_cardWidthIn( const std::vector< Int > &sizes, Int cardGap, Int versusWidth, Int room )
{
	Int cards = 0;
	for( size_t block = 0; block < sizes.size(); block++ )
		cards += sizes[ block ];
	const Int blocks = (Int)sizes.size();
	return ( room - ( cards - blocks ) * cardGap - ( blocks - 1 ) * versusWidth ) / cards;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_fitCount( const std::vector< Int > &prefixWidths, Int ellipsisWidth, Int widest )
{
	Int count = (Int)prefixWidths.size() - 1;
	if( prefixWidths[ count ] <= widest )
		return count;
	while( count > 0 && prefixWidths[ count ] + ellipsisWidth > widest )
		count--;
	return count;
}

//-------------------------------------------------------------------------------------------------
std::vector< Int > ObserverCamera_barShares( const std::vector< Int > &values, Int width )
{
	std::vector< Int > pixels( values.size(), 0 );
	Int64 total = 0;
	for( size_t index = 0; index < values.size(); index++ )
		total += values[ index ];
	if( total <= 0 )
		return pixels;
	std::vector< Int64 > remainders( values.size(), 0 );
	Int given = 0;
	for( size_t index = 0; index < values.size(); index++ )
	{
		const Int64 scaled = (Int64)values[ index ] * width;
		pixels[ index ] = (Int)( scaled / total );
		remainders[ index ] = scaled % total;
		given += pixels[ index ];
	}
	for( ; given < width; given++ )
	{
		size_t largest = 0;
		for( size_t index = 1; index < values.size(); index++ )
			if( remainders[ index ] > remainders[ largest ] )
				largest = index;
		pixels[ largest ]++;
		remainders[ largest ] = -1;
	}
	return pixels;
}

//-------------------------------------------------------------------------------------------------
/** The relative luminance of a colour, the way the contrast ratio is defined. */
//-------------------------------------------------------------------------------------------------
static Real luminance( Real red, Real green, Real blue )
{
	const Real channels[ 3 ] = { red, green, blue };
	Real linear[ 3 ];
	for( Int channel = 0; channel < 3; channel++ )
	{
		const Real value = channels[ channel ] / 255.0f;
		linear[ channel ] = value <= 0.03928f ? value / 12.92f : powf( ( value + 0.055f ) / 1.055f, 2.4f );
	}
	return 0.2126f * linear[ 0 ] + 0.7152f * linear[ 1 ] + 0.0722f * linear[ 2 ];
}

/// the broadcast's ground, zerohour.gg's --bg, and the contrast its text needs on it
static const UnsignedByte BROADCAST_GROUND_RGB[ 3 ] = { 0x0c, 0x12, 0x20 };
static const Real READABLE_CONTRAST = 4.5f;
static const Real READABLE_STEP = 0.05f;
/// zerohour.gg's --gold, the score bar's frame's colour, and how far from it a player's colour has to be to
/// stand apart from them, summed over the three channels
static const Int BRAND_GOLD_RGB[ 3 ] = { 0xf2, 0xc2, 0x30 };
static const Int BRAND_GOLD_NEAR = 100;

//-------------------------------------------------------------------------------------------------
Color ObserverCamera_readableColor( Color color )
{
	UnsignedByte red, green, blue, alpha;
	GameGetColorComponents( color, &red, &green, &blue, &alpha );
	const Real ground = luminance( BROADCAST_GROUND_RGB[ 0 ], BROADCAST_GROUND_RGB[ 1 ], BROADCAST_GROUND_RGB[ 2 ] );
	for( Real white = 0.0f; white <= 1.0f; white += READABLE_STEP )
	{
		const Real r = red + ( 255.0f - red ) * white;
		const Real g = green + ( 255.0f - green ) * white;
		const Real b = blue + ( 255.0f - blue ) * white;
		if( ( luminance( r, g, b ) + 0.05f ) / ( ground + 0.05f ) >= READABLE_CONTRAST )
			return GameMakeColor( (UnsignedByte)REAL_TO_INT( r ), (UnsignedByte)REAL_TO_INT( g ), (UnsignedByte)REAL_TO_INT( b ), alpha );
	}
	return GameMakeColor( 255, 255, 255, alpha );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_nearBrandGold( Color color )
{
	UnsignedByte red, green, blue, alpha;
	GameGetColorComponents( color, &red, &green, &blue, &alpha );
	return abs( red - BRAND_GOLD_RGB[ 0 ] ) + abs( green - BRAND_GOLD_RGB[ 1 ] ) + abs( blue - BRAND_GOLD_RGB[ 2 ] ) <= BRAND_GOLD_NEAR;
}

//-------------------------------------------------------------------------------------------------
static Real distanceSquared( const Coord2D &a, const Coord2D &b )
{
	const Real dx = a.x - b.x;
	const Real dy = a.y - b.y;
	return dx * dx + dy * dy;
}

static Bool within( const Coord2D &a, const Coord2D &b, Real distance )
{
	return distanceSquared( a, b ) <= distance * distance;
}

//-------------------------------------------------------------------------------------------------
/** A crowd's hits are taken out of the scan before the next hottest place is looked for, so each
	* pass of the loop is another crowd.  The crowd's own first hit lies within the gather radius of
	* the crowd's middle, which is a weighted middle of hits all within that radius of it, so every
	* pass takes at least one hit out. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera_trackFights( std::vector< DirectorMoment > &fights, const std::vector< DirectorFightHit > &hits, UnsignedInt frame )
{
	std::vector< DirectorHeat > left;
	std::vector< UnsignedInt > leftSides;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		DirectorHeat heat;
		heat.position = hits[ index ].position;
		heat.weight = hits[ index ].weight;
		left.push_back( heat );
		leftSides.push_back( hits[ index ].sides );
	}

	Coord2D crowd;
	Real heat = 0.0f;
	while( ObserverCamera_hottestPlace( left, &crowd, &heat ) )
	{
		UnsignedInt sides = 0;
		size_t kept = 0;
		for( size_t index = 0; index < left.size(); index++ )
		{
			if( sameFight( left[ index ].position, crowd ) )
			{
				sides |= leftSides[ index ];
				continue;
			}
			left[ kept ] = left[ index ];
			leftSides[ kept ] = leftSides[ index ];
			kept++;
		}
		left.resize( kept );
		leftSides.resize( kept );

		DirectorMoment *joined = NULL;
		for( size_t index = 0; index < fights.size(); index++ )
		{
			DirectorMoment &fight = fights[ index ];
			if( fight.power || frame > fight.last + SCOUT_FIGHT_GAP || !within( fight.target, crowd, SCOUT_SAME_FIGHT ) )
				continue;
			if( joined == NULL || distanceSquared( fight.target, crowd ) < distanceSquared( joined->target, crowd ) )
				joined = &fight;
		}
		if( joined == NULL )
		{
			DirectorMoment fight;
			fight.start = frame;
			fight.last = frame;
			fight.place = crowd;
			fight.target = crowd;
			fight.peak = heat;
			fight.sides = sides;
			fight.power = FALSE;
			fight.superweapon = FALSE;
			fights.push_back( fight );
			continue;
		}
		joined->last = frame;
		joined->target = crowd;
		joined->peak = max( joined->peak, heat );
		joined->sides |= sides;
	}
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_worthFilming( const DirectorMoment &moment )
{
	if( moment.power )
		return moment.superweapon || moment.peak > 0.0f;
	return moment.peak >= FIGHT_WORTH_HEAT && moment.last - moment.start >= FIGHT_WORTH_FRAMES;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera_markLandings( std::vector< DirectorMoment > &moments, const std::vector< DirectorFightHit > &hits, UnsignedInt frame )
{
	for( size_t index = 0; index < moments.size(); index++ )
	{
		DirectorMoment &power = moments[ index ];
		if( !power.power || power.superweapon || power.peak > 0.0f || frame > power.start + EVENT_MOST_FRAMES )
			continue;
		for( size_t hit = 0; hit < hits.size() && power.peak <= 0.0f; hit++ )
		{
			if( !within( hits[ hit ].position, power.target, DIRECTOR_GATHER_RADIUS ) )
				continue;
			power.peak = EVENT_WEIGHT;
			power.last = frame;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The timeline is written with no decimals, so the scouting pass's target is the one fired on frame
	* within a unit of at.  A superweapon lands from its launch. */
//-------------------------------------------------------------------------------------------------
UnsignedInt ObserverCamera_powerLandsAt( const std::vector< DirectorMoment > &timeline, UnsignedInt frame, const Coord2D &at )
{
	for( size_t index = 0; index < timeline.size(); index++ )
	{
		const DirectorMoment &power = timeline[ index ];
		if( power.power && power.start == frame && within( power.target, at, 1.0f ) )
			return ObserverCamera_worthFilming( power ) ? max( power.last, power.start ) : 0;
	}
	return 0;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera_closeTimeline( std::vector< DirectorMoment > &moments, UnsignedInt end )
{
	for( size_t index = 0; index < moments.size(); index++ )
	{
		DirectorMoment &fight = moments[ index ];
		if( fight.power || fight.last + SCOUT_FIGHT_GAP < end )
			continue;
		fight.last = max( fight.last, fight.start + FIGHT_WORTH_FRAMES );
	}
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_prerollMoment( const std::vector< DirectorMoment > &timeline, UnsignedInt frame, const Coord2D *taken, Real apart )
{
	Int best = -1;
	for( size_t index = 0; index < timeline.size(); index++ )
	{
		const DirectorMoment &moment = timeline[ index ];
		// a power is waited for when it lands, a strike flown in from the map's edge well after its launch
		const UnsignedInt begins = moment.power ? max( moment.last, moment.start ) : moment.start;
		if( begins <= frame || begins > frame + DIRECTOR_PREROLL_FRAMES || !ObserverCamera_worthFilming( moment ) )
			continue;
		if( taken != NULL && within( moment.power ? moment.target : moment.place, *taken, apart ) )
			continue;
		if( best >= 0 )
		{
			const DirectorMoment &held = timeline[ best ];
			const Int rank = ( moment.superweapon ? 2 : 0 ) + ( moment.power ? 1 : 0 );
			const Int heldRank = ( held.superweapon ? 2 : 0 ) + ( held.power ? 1 : 0 );
			const Bool better = rank != heldRank ? rank > heldRank : moment.peak > held.peak;
			if( !better )
				continue;
		}
		best = (Int)index;
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
/** A fight is looked for from a scan before the scouting pass first saw it to the end of its gap,
	* the two passes' scans need not fall on the same frames; near where it began or where it was last
	* seen, since a fight moves. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_fizzles( const std::vector< DirectorMoment > &timeline, const Coord2D &place, UnsignedInt frame )
{
	Bool seen = FALSE;
	for( size_t index = 0; index < timeline.size(); index++ )
	{
		const DirectorMoment &fight = timeline[ index ];
		if( fight.power || frame + DIRECTOR_SCAN_FRAMES < fight.start || frame > fight.last + SCOUT_FIGHT_GAP )
			continue;
		if( !within( fight.place, place, SCOUT_SAME_FIGHT ) && !within( fight.target, place, SCOUT_SAME_FIGHT ) )
			continue;
		if( ObserverCamera_worthFilming( fight ) )
			return FALSE;
		seen = TRUE;
	}
	return seen;
}

//-------------------------------------------------------------------------------------------------
static Bool scoutedNear( const DirectorMoment &fight, const Coord2D &place )
{
	return within( fight.place, place, SCOUT_SAME_FIGHT ) || within( fight.target, place, SCOUT_SAME_FIGHT );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_fightLasts( const std::vector< DirectorMoment > &timeline, const Coord2D &place, UnsignedInt frame, UnsignedInt until,
	const Coord2D *besides, Real leastPeak )
{
	for( size_t index = 0; index < timeline.size(); index++ )
	{
		const DirectorMoment &fight = timeline[ index ];
		if( fight.power || !ObserverCamera_worthFilming( fight ) || fight.peak < leastPeak || frame + DIRECTOR_PREROLL_FRAMES < fight.start
			|| fight.last < until )
			continue;
		if( scoutedNear( fight, place ) && ( besides == NULL || !scoutedNear( fight, *besides ) ) )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_plannedSecond( const std::vector< DirectorMoment > &timeline, const Coord2D &first, UnsignedInt frame, Real apart,
	Bool keeping )
{
	Int best = -1;
	for( size_t index = 0; index < timeline.size(); index++ )
	{
		const DirectorMoment &fight = timeline[ index ];
		if( fight.power || !ObserverCamera_worthFilming( fight ) || frame + SPLIT_LEAD_FRAMES < fight.start || frame > fight.last )
			continue;
		if( !keeping && frame >= fight.start )
			continue;
		if( within( fight.place, first, apart ) )
			continue;
		if( best < 0 || fight.peak > timeline[ best ].peak )
			best = (Int)index;
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
AsciiString ObserverCamera_formatMoment( const DirectorMoment &moment )
{
	AsciiString line;
	line.format( "%s %u %u %.0f %.0f %.0f %.0f %.2f %u %d", moment.power ? "power" : "fight", moment.start, moment.last,
		moment.place.x, moment.place.y, moment.target.x, moment.target.y, moment.peak, moment.sides, moment.superweapon ? 1 : 0 );
	return line;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_parseMoment( const char *line, DirectorMoment *moment )
{
	char kind[ 8 ];
	Int superweapon = 0;
	const Int read = sscanf( line, "%7s %u %u %f %f %f %f %f %u %d", kind, &moment->start, &moment->last, &moment->place.x,
		&moment->place.y, &moment->target.x, &moment->target.y, &moment->peak, &moment->sides, &superweapon );
	if( read != 10 )
		return FALSE;
	const Bool power = strcmp( kind, "power" ) == 0;
	if( !power && strcmp( kind, "fight" ) != 0 )
		return FALSE;
	moment->power = power;
	moment->superweapon = superweapon != 0;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_easeFrames( Real frame, UnsignedInt start, UnsignedInt length )
{
	if( frame <= (Real)start )
		return 0.0f;
	const Real t = min( ( frame - (Real)start ) / length, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_paneLineWidth( Int height )
{
	return max( REAL_TO_INT( height / PANE_LINE_ROWS_A_PIXEL ), 2 );
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_paneBandWidth( Int height )
{
	return PANE_BAND_LINES * ObserverCamera_paneLineWidth( height );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_easeBetween( Real progress, Real from, Real to )
{
	const Real t = min( max( ( progress - from ) / ( to - from ), 0.0f ), 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_advanceShowing( std::vector< DirectorShowing > &queue, UnsignedInt frame, UnsignedInt moveFrames, UnsignedInt holdAlone,
	Bool keepLast )
{
	if( !queue.empty() && queue.front().leaving != 0 && frame >= queue.front().leaving + moveFrames )
		queue.erase( queue.begin() );
	if( queue.empty() )
		return FALSE;
	DirectorShowing &first = queue.front();
	if( first.start == 0 )
	{
		first.start = frame;
		return TRUE;
	}
	const size_t waiting = queue.size() - 1;
	const UnsignedInt hold = holdAlone / (UnsignedInt)min( waiting + 1, (size_t)3 );
	if( first.leaving == 0 && !( keepLast && waiting == 0 ) && frame >= first.start + moveFrames + hold )
		first.leaving = frame;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_showingShown( const DirectorShowing &showing, Real frame, UnsignedInt moveFrames )
{
	if( showing.start == 0 )
		return 0.0f;
	const Real in = ObserverCamera_easeFrames( frame, showing.start, moveFrames );
	if( showing.leaving == 0 )
		return in;
	return in * ( 1.0f - ObserverCamera_easeFrames( frame, showing.leaving, moveFrames ) );
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera_cardExit( Real frame, UnsignedInt defeated, UnsignedInt collapseFrom, Real *flash, Real *struck, Real *collapse )
{
	// up in a third of the flash, down over the rest
	const UnsignedInt up = CARD_FLASH_FRAMES / 3;
	*flash = ObserverCamera_easeFrames( frame, defeated, up ) * ( 1.0f - ObserverCamera_easeFrames( frame, defeated + up, CARD_FLASH_FRAMES - up ) );
	*struck = ObserverCamera_easeFrames( frame, defeated + CARD_STRIKE_FROM, CARD_STRIKE_FRAMES );
	*collapse = ObserverCamera_easeFrames( frame, collapseFrom, CARD_COLLAPSE_FRAMES );
}

//-------------------------------------------------------------------------------------------------
UnsignedInt ObserverCamera_collapseFrom( UnsignedInt defeated, UnsignedInt lastCollapse )
{
	return max( defeated + CARD_STRUCK_FRAMES, lastCollapse + CARD_COLLAPSE_FRAMES );
}

//-------------------------------------------------------------------------------------------------
ObserverCamera::ObserverCamera()
{
	// the timeline is the whole process's, read once and kept across the resets a match start makes
	m_scoutScanned = 0;
	m_timelineLoaded = FALSE;
	m_modeBeforeLook = OBSERVER_CAMERA_FREE;
	m_lookTookFog = FALSE;
	// and so is the events file, which a later reset closes and nothing opens again
	m_filmEvents = NULL;
	m_filmEventsTried = FALSE;
	// and the replays, the filming pass's and the replay pass's
	m_replayCrcsWritten = 0;
	m_replayFile = NULL;
	m_replayFileTried = FALSE;
	m_replaysLoaded = FALSE;
	m_replayGaveUp = FALSE;
	m_replaysDone = FALSE;
	m_replayClip = -1;
	m_replayFrame = 0xFFFFFFFFu;
	reset();
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::reset( void )
{
	m_mode = OBSERVER_CAMERA_FREE;
	m_looking = FALSE;
	m_followed = NO_PLAYER;
	m_fog = FALSE;
	m_shroudViewer = NO_PLAYER;
	m_driving = FALSE;
	m_holdingHeight = FALSE;
	m_heightDriven = FALSE;
	m_handHeight = 0.0f;
	m_drivenHeight = 0.0f;
	m_heightExtra = 0.0f;
	m_heightExtraVelocity = 0.0f;
	m_lastZoom = 0.0f;
	m_lastCut = FALSE;
	m_spentMoment = -1;
	m_drivenTo.zero();
	m_lastUpdate = 0;
	m_directedFrame = 0xFFFFFFFFu;
	m_directing = TRUE;
	m_velocity.x = m_velocity.y = m_velocity.z = m_velocity.angle = m_velocity.pitch = m_velocity.zoom = 0.0f;
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
		m_playerViews[ index ] = ViewLocation();
	m_placeValid = FALSE;
	m_place.x = m_place.y = 0.0f;
	m_placeSince = 0;
	m_placeScanned = 0;
	m_placePeak = 0.0f;
	m_placeFor = NULL;
	m_placeKind = PLACE_SIGHT;
	m_placeHeight = 0.0f;
	m_placeEvent = 0;
	m_eventSecond = 0;
	m_windowKind = DIRECTOR_WINDOW_NONE;
	m_windowOn = FALSE;
	m_windowChanged = 0;
	m_windowSubject.x = m_windowSubject.y = 0.0f;
	m_windowSides = 0;
	m_windowProgress = 0.0f;
	m_windowGlide = ViewLocation();
	m_windowVelocity.x = m_windowVelocity.y = m_windowVelocity.z = 0.0f;
	m_windowVelocity.angle = m_windowVelocity.pitch = m_windowVelocity.zoom = 0.0f;
	m_windowView = ViewLocation();
	flushFilmEvents();
	if( m_filmEvents != NULL )
	{
		fclose( m_filmEvents );
		m_filmEvents = NULL;
	}
	if( m_replayFile != NULL )
	{
		fclose( m_replayFile );
		m_replayFile = NULL;
	}
	m_filmPending.clear();
	m_filmBuilding.clear();
	m_filmLost.clear();
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
		m_filmRank[ index ] = -1;
	m_placeMoment = -1;
	m_skippedFight.x = m_skippedFight.y = 0.0f;
	m_seen.clear();
	m_events.clear();
	m_nextEventId = 1;
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
	{
		m_flags[ index ].clear();
		m_defeatFrame[ index ] = 0;
		m_collapseFrame[ index ] = 0;
	}
	m_defeatBanners.clear();
	m_playedMask = 0;
	m_lastCollapse = 0;
	m_winnerFrame = 0;
	m_fights.clear();
	m_fightSides.clear();
	m_broadcast.clear();
	m_screenGround = 0.0f;
	m_splitApart = SPLIT_APART;
	m_split = FALSE;
	m_splitChanged = 0;
	m_secondPlace.x = m_secondPlace.y = 0.0f;
	m_aimFrom = ViewLocation();
	m_mainOffset.x = m_mainOffset.y = 0.0f;
	m_firstView = ViewLocation();
	m_drawingPane = 0;
}

//-------------------------------------------------------------------------------------------------
/** Called from the logic on every machine, players' included, so it only ever adds to a list the
	* director reads; nothing the logic does depends on it. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteSpecialPower( const Player *owner, const Coord3D *from, const Coord3D *at, Bool superweapon,
	const SpecialPowerTemplate *power, const ThingTemplate *sourceThing )
{
	// a power with no place of its own, one aimed at a unit or at nothing, is shown where it was used
	// from when it is the player's; a unit's own ability, a hacker's or a sniper's, is no event
	if( at == NULL && !playersPower( power, sourceThing, superweapon ) )
		return;
	if( at == NULL )
		at = from;
	const UnsignedInt frame = TheGameLogic->getFrame();
	dropOldEvents( frame );

	// the same power from the same player again on the same spot while the first is still shown is
	// more of that use: it keeps the shot going instead of starting a new one the director cuts to
	Coord2D target;
	target.x = at->x;
	target.y = at->y;
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		if( ObserverCamera_sameUse( m_events[ index ], owner, power, target, frame ) )
		{
			m_events[ index ].until = ObserverCamera_eventKeptTo( m_events[ index ], frame, superweapon ? EVENT_SUPERWEAPON_FRAMES : EVENT_FRAMES );
			return;
		}
	}

	DirectorEvent event;
	event.id = m_nextEventId++;
	event.owner = owner;
	event.source.x = from->x;
	event.source.y = from->y;
	event.target.x = at->x;
	event.target.y = at->y;
	event.since = frame;
	event.weight = superweapon ? EVENT_SUPERWEAPON_WEIGHT : EVENT_WEIGHT;
	event.superweapon = superweapon;
	// a power that only looks, a spy satellite or a radar scan, hurts nobody and is not filmed: the
	// director cut to empty ground for every one an Air Force general fired, and to a spy satellite
	// over the central battle in S1-A1.  The scouting pass knows which of the others land and when;
	// without it a power waits for its first hit
	event.looksOnly = !superweapon && power != NULL && ObserverCamera_powerLooksOnly( power->getSpecialPowerType() );
	event.landsAt = event.looksOnly ? 0 : ObserverCamera_powerLandsAt( m_timeline, frame, event.target );
	event.landed = superweapon || ObserverCamera_landsNow( event, frame, !m_timeline.empty(), FALSE );
	// a superweapon's warhead is seen hitting by noteSuperweaponHit; another power the timeline has landing
	// lands on the frame the scouting pass saw it hit
	event.landedAt = event.landed && !superweapon ? max( event.landsAt, frame ) : 0;
	// one still to land waits for it as long as anything is ever shown
	event.until = frame + ( superweapon ? EVENT_SUPERWEAPON_FRAMES : event.landed || event.looksOnly ? EVENT_FRAMES : EVENT_MOST_FRAMES );
	event.shown = FALSE;
	event.power = power;
	event.sourceThing = sourceThing;
	m_events.push_back( event );
	// its flag goes up as it is used, whether or not it lands: a scan's flag is all there is of it
	noteFlag( event );
	if( TheGlobalData->m_directorRecord && power != NULL )
		DEBUG_LOG(( "OBSCAM frame %u special power %u %s used by player %d at (%.0f,%.0f)%s%s\n", frame, event.id, power->getName().str(),
			owner->getPlayerIndex(), target.x, target.y, superweapon ? ", superweapon" : "",
			event.looksOnly ? ", only looks" : event.landed ? ", lands" : event.landsAt != 0 ? ", lands later" : ", waits for a hit" ));
	// the events file has a general's powers and every superweapon, a warhead no power sent among them,
	// and leaves out a unit's own ability
	if( superweapon || playersPower( power, sourceThing, superweapon ) )
		noteFilmEvent( frame, superweapon ? "superweapon" : event.looksOnly ? "scan" : "power", owner != NULL ? owner->getPlayerIndex() : -1,
			target.x, target.y, power != NULL ? power->getName().str() : "-", powerShownName( power, sourceThing ) );

	if( !TheGlobalData->m_directorScoutFile.isEmpty() && !event.looksOnly )
	{
		DirectorMoment moment;
		moment.start = frame;
		moment.last = frame;
		moment.place = event.source;
		moment.target = event.target;
		moment.peak = superweapon ? event.weight : 0.0f;
		moment.sides = owner->getPlayerMask();
		moment.power = TRUE;
		moment.superweapon = superweapon;
		m_scouted.push_back( moment );
	}
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteSuperweaponHit( const Player *owner, const Coord3D *at, Bool follow )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	Coord2D where;
	where.x = at->x;
	where.y = at->y;
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		DirectorEvent &event = m_events[ index ];
		if( event.owner != owner || !sameFight( event.target, where ) || frame >= event.until )
			continue;

		if( follow )
			event.target = where;
		event.until = ObserverCamera_eventKeptTo( event, frame, EVENT_AFTERMATH_FRAMES );
		if( event.landedAt == 0 )
			event.landedAt = frame;
		return;
	}

	// a warhead nobody's special power sent, a script's or a map's, is still worth seeing land
	noteSpecialPower( owner, at, at, TRUE, NULL, NULL );
	m_events.back().until = frame + EVENT_AFTERMATH_FRAMES;
	m_events.back().landedAt = frame;
}

//-------------------------------------------------------------------------------------------------
/** A power used hangs its flag under its player's card when it is the player's, not a unit's: a
	* superweapon, a general's power one a promotion bought, or one a building fires, the command
	* centre's spy satellite.  A unit's own ability, a sniper's shot or a hacker's, is none of them.  It
	* goes ahead of any upgrade still waiting, which a Hard AI buys a dozen of in a minute.  The logic
	* calls it from the tick; the flags change after it, on the main thread, since the card draw holds
	* a pointer into them (getPowerFlag) while a tick runs beside the render. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteFlag( const DirectorEvent &event )
{
	if( !TheGlobalData->m_directorRecord || !playersPower( event.power, event.sourceThing, event.superweapon ) )
		return;
	LOGIC_AFTER_TICK( [this, event]() { noteFlag( event ); } )
	const Int index = event.owner->getPlayerIndex();
	const DirectorShowing flag = { index, event.power, event.sourceThing, event.superweapon, 0, 0, NULL };
	std::vector< DirectorShowing > &flags = m_flags[ index ];
	std::vector< DirectorShowing >::iterator waiting = flags.begin();
	while( waiting != flags.end() && ( waiting->start != 0 || waiting->upgrade == NULL ) )
		++waiting;
	flags.insert( waiting, flag );
}

//-------------------------------------------------------------------------------------------------
/** Called from the logic on every machine, like noteSpecialPower, and only adds to the flags, after
	* the tick as noteFlag does. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteUpgrade( const Player *owner, const UpgradeTemplate *upgrade )
{
	if( !TheGlobalData->m_directorRecord || upgrade->getButtonImage() == NULL )
		return;
	LOGIC_AFTER_TICK( [this, owner, upgrade]() { noteUpgrade( owner, upgrade ); } )
	const Int index = owner->getPlayerIndex();
	const DirectorShowing flag = { index, NULL, NULL, FALSE, 0, 0, upgrade };
	m_flags[ index ].push_back( flag );
}

//-------------------------------------------------------------------------------------------------
/** The broadcast's moments, one logic frame on: a player seen playing who no longer is has lost, his
	* card struck and his banner queued; the match decided brings the winner's banner; and every queue
	* of flags and banners moves on.  Read off the players, never written to them. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateBroadcastMoments( UnsignedInt frame )
{
	for( Int index = 0; index < ThePlayerList->getPlayerCount() && index < MAX_PLAYER_COUNT; index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( !player->isPlayableSide() || player->isPlayerObserver() )
			continue;
		const PlayerMaskType mask = player->getPlayerMask();
		if( player->isPlayerActive() )
		{
			m_playedMask |= mask;
			continue;
		}
		if( ( m_playedMask & mask ) == 0 || m_defeatFrame[ index ] != 0 )
			continue;
		m_defeatFrame[ index ] = frame;
		m_collapseFrame[ index ] = ObserverCamera_collapseFrom( frame, m_lastCollapse );
		m_lastCollapse = m_collapseFrame[ index ];
		m_flags[ index ].clear();
		const DirectorShowing banner = { index, NULL, NULL, FALSE, 0, 0, NULL };
		m_defeatBanners.push_back( banner );
		noteFilmEvent( frame, "defeated", index, -1.0f, -1.0f, "-", UnicodeString::TheEmptyString );
		DEBUG_LOG(( "OBSCAM frame %u defeat: player %d '%s' (%s), card struck, collapses at frame %u\n", frame, index,
			WideCharAsUtf8( player->getPlayerDisplayName().str() ).str(), WideCharAsUtf8( player->getPlayerTemplate()->getDisplayName().str() ).str(),
			m_collapseFrame[ index ] ));
	}

	const UnsignedInt decidedOn = TheVictoryConditions->getEndFrame();
	const Bool decided = decidedOn > LOGICFRAMES_PER_SECOND;
	if( decided && m_winnerFrame == 0 )
	{
		m_winnerFrame = frame + WINNER_DELAY_FRAMES;
		DEBUG_LOG(( "OBSCAM frame %u match decided on frame %u, winner banner at frame %u\n", frame, decidedOn, m_winnerFrame ));
	}

	if( ObserverCamera_advanceShowing( m_defeatBanners, frame, BANNER_MOVE_FRAMES, DEFEAT_BANNER_HOLD_FRAMES, decided ) )
		DEBUG_LOG(( "OBSCAM frame %u defeat banner: player %d, %d waiting\n", frame, m_defeatBanners.front().player,
			(Int)m_defeatBanners.size() - 1 ));
	for( Int index = 0; index < MAX_PLAYER_COUNT; index++ )
	{
		if( !ObserverCamera_advanceShowing( m_flags[ index ], frame, FLAG_MOVE_FRAMES, FLAG_HOLD_FRAMES, FALSE ) )
			continue;
		const DirectorShowing &flag = m_flags[ index ].front();
		DEBUG_LOG(( "OBSCAM frame %u %s flag under player %d's card: %s%s, %d waiting\n", frame, flag.upgrade != NULL ? "upgrade" : "power", index,
			flag.upgrade != NULL ? flag.upgrade->getUpgradeName().str() : flag.power->getName().str(), flag.superweapon ? " (superweapon)" : "",
			(Int)m_flags[ index ].size() - 1 ));
	}
}

//-------------------------------------------------------------------------------------------------
/** The filming pass alone writes the events file: the scouting pass plays the same match headless and
	* has no film to sit beside.  A line waits here until update writes it, since a power is noted from
	* the logic. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteFilmEvent( UnsignedInt frame, const char *type, Int player, Real x, Real y, const char *thing,
	const UnicodeString &name )
{
	if( !TheGlobalData->m_directorRecord || !TheGlobalData->m_directorScoutFile.isEmpty() || isReplaying()
		|| ( m_filmEventsTried && m_filmEvents == NULL ) )
		return;
	DirectorFilmEvent event;
	event.frame = frame;
	event.type = type;
	event.player = player;
	event.x = x;
	event.y = y;
	event.thing = thing;
	event.name = ObserverCamera_filmName( name );
	m_filmPending.push_back( ObserverCamera_formatFilmEvent( event ) );
}

//-------------------------------------------------------------------------------------------------
/** Videos\<name>.events beside the film W3DDisplay's captureVideoFrame writes as Videos\<name>.mp4,
	* opened on the match's first directed frame with the map and the players at its top. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::openFilmEvents( void )
{
	m_filmEventsTried = TRUE;
	AsciiString directory;
	directory.format( "%sVideos", TheGlobalData->getPath_UserData().str() );
	TheFileSystem->createDirectory( directory );
	AsciiString path;
	path.format( "%s/%s.events", directory.str(), TheGlobalData->m_videoName.str() );
	m_filmEvents = fopen( path.str(), "w" );
	if( m_filmEvents == NULL )
	{
		DEBUG_LOG(( "OBSCAM cannot write the events file '%s', the film goes without one\n", path.str() ));
		m_filmPending.clear();
		return;
	}
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	fprintf( m_filmEvents, "%s\n", ObserverCamera_formatFilmMap( TheGlobalData->m_mapName.str(), extent.lo.x, extent.lo.y,
		extent.hi.x, extent.hi.y ).str() );
	for( Int index = 0; index < ThePlayerList->getPlayerCount(); index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( !player->isPlayableSide() || player->isPlayerObserver() )
			continue;
		fprintf( m_filmEvents, "%s\n", ObserverCamera_formatFilmPlayer( player->getPlayerIndex(), player->getPlayerTemplate()->getName().str(),
			(UnsignedInt)player->getPlayerColor(), WideCharAsUtf8( player->getPlayerDisplayName().str() ).str() ).str() );
	}
	fflush( m_filmEvents );
	DEBUG_LOG(( "OBSCAM events written to '%s'\n", path.str() ));
}

//-------------------------------------------------------------------------------------------------
/** Flushed every frame something was written, so a run cut short keeps what it had. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::flushFilmEvents( void )
{
	if( m_filmEvents == NULL || m_filmPending.empty() )
		return;
	for( size_t index = 0; index < m_filmPending.size(); index++ )
		fprintf( m_filmEvents, "%s\n", m_filmPending[ index ].str() );
	fflush( m_filmEvents );
	m_filmPending.clear();
}

//-------------------------------------------------------------------------------------------------
/** What the events file reads off the objects and the players each logic frame: a structure no longer
	* under construction that was at the last look has been built, a dear thing dead that was not at the
	* last look is lost, and a player's rank going up is a promotion.  Only read, never written. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::scanFilmEvents( UnsignedInt frame )
{
	std::vector< ObjectID > building;
	std::vector< ObjectID > lost;
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		const Bool dead = obj->isEffectivelyDead();
		const Player *owner = obj->getControllingPlayer();
		const Int player = owner != NULL ? owner->getPlayerIndex() : -1;
		const ObjectID id = obj->getID();
		if( obj->isKindOf( KINDOF_STRUCTURE ) && !dead )
		{
			if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
				building.push_back( id );
			else if( std::find( m_filmBuilding.begin(), m_filmBuilding.end(), id ) != m_filmBuilding.end() )
				noteFilmEvent( frame, "built", player, obj->getPosition()->x, obj->getPosition()->y, obj->getTemplate()->getName().str(),
					obj->getTemplate()->getDisplayName() );
		}
		if( !dead || obj->getTemplate()->friend_getBuildCost() < FILM_LOST_LEAST_COST )
			continue;
		lost.push_back( id );
		if( std::find( m_filmLost.begin(), m_filmLost.end(), id ) == m_filmLost.end() )
			noteFilmEvent( frame, "lost", player, obj->getPosition()->x, obj->getPosition()->y, obj->getTemplate()->getName().str(),
				obj->getTemplate()->getDisplayName() );
	}
	m_filmBuilding.swap( building );
	m_filmLost.swap( lost );

	for( Int index = 0; index < ThePlayerList->getPlayerCount() && index < MAX_PLAYER_COUNT; index++ )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( !player->isPlayableSide() || player->isPlayerObserver() )
			continue;
		const Int rank = player->getRankLevel();
		if( m_filmRank[ index ] >= 0 && rank > m_filmRank[ index ] )
			noteFilmEvent( frame, "rank", player->getPlayerIndex(), -1.0f, -1.0f, "-", UnicodeString::TheEmptyString );
		m_filmRank[ index ] = rank;
	}
}

//-------------------------------------------------------------------------------------------------
const DirectorShowing *ObserverCamera::getPowerFlag( Int playerIndex, Real *drop ) const
{
	const std::vector< DirectorShowing > &flags = m_flags[ playerIndex ];
	if( flags.empty() || flags.front().start == 0 )
		return NULL;
	*drop = ObserverCamera_showingShown( flags.front(), GameEngine_pictureFrame(), FLAG_MOVE_FRAMES );
	return &flags.front();
}

//-------------------------------------------------------------------------------------------------
const DirectorShowing *ObserverCamera::getDefeatBanner( Real *shown ) const
{
	if( m_defeatBanners.empty() || m_defeatBanners.front().start == 0 )
		return NULL;
	*shown = ObserverCamera_showingShown( m_defeatBanners.front(), GameEngine_pictureFrame(), BANNER_MOVE_FRAMES );
	return &m_defeatBanners.front();
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera::getWinnerShown( void ) const
{
	return m_winnerFrame == 0 ? 0.0f : ObserverCamera_easeFrames( GameEngine_pictureFrame(), m_winnerFrame, BANNER_MOVE_FRAMES );
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::dropOldEvents( UnsignedInt frame )
{
	size_t kept = 0;
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		const DirectorEvent &event = m_events[ index ];
		if( frame < event.until )
		{
			m_events[ kept++ ] = event;
			continue;
		}
		// what the film made of each power, so a run's log counts the ones it missed
		if( TheGlobalData->m_directorRecord && event.power != NULL && !event.looksOnly )
			DEBUG_LOG(( "OBSCAM frame %u special power %u %s over: %s\n", frame, event.id, event.power->getName().str(),
				event.shown ? "shown" : event.landed ? "landed and missed" : "never landed" ));
		noteMissedMoment( event );
	}
	m_events.resize( kept );
}

//-------------------------------------------------------------------------------------------------
/** The filming pass alone: the scouting pass films nothing and the replay pass is the replays. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteMissedMoment( const DirectorEvent &event )
{
	if( !TheGlobalData->m_directorRecord || !TheGlobalData->m_directorScoutFile.isEmpty() || isReplaying() )
		return;
	const Bool generalsPower = event.power != NULL && event.power->getRequiredScience() != SCIENCE_INVALID;
	if( !ObserverCamera_missedBigMoment( event.superweapon, generalsPower, event.landed, event.shown ) )
		return;
	DirectorReplay replay;
	replay.landed = event.landedAt != 0 ? event.landedAt : event.landsAt != 0 ? event.landsAt : event.since;
	ObserverCamera_replayRange( replay.landed, &replay.first, &replay.last );
	replay.target = event.target;
	replay.player = event.owner != NULL ? event.owner->getPlayerIndex() : -1;
	replay.power = event.power != NULL ? event.power->getName() : AsciiString( "-" );
	replay.splice = 0;
	m_replays.push_back( replay );
	DEBUG_LOG(( "OBSCAM frame %u instant replay of %s by player %d at (%.0f,%.0f), frames %u to %u, landed %u, waits for a calm frame\n",
		TheGameLogic->getFrame(), replay.power.str(), replay.player, replay.target.x, replay.target.y, replay.first, replay.last, replay.landed ));
}

//-------------------------------------------------------------------------------------------------
/** The filming pass, once a logic frame: its own CRC checkpoints, the replays' splices on the first
	* calm frame, and Videos\<name>.replays opened with the first replay and written as things come, so a
	* film cut short keeps what it had.  No replay, no file, and WinMain runs no replay pass. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateReplaysFile( UnsignedInt frame )
{
	if( frame % SCOUT_CRC_FRAMES == 0 && ( m_replayCrcs.empty() || m_replayCrcs.back().frame != frame ) )
	{
		const CrcCheckpoint checkpoint = { frame, TheGameLogic->getCRC( CRC_RECALC ) };
		m_replayCrcs.push_back( checkpoint );
	}

	// calm: on a sight, nothing landing, nothing in the corner window
	const Bool calm = ( !m_placeValid || m_placeKind == PLACE_SIGHT ) && !m_windowOn;
	std::vector< AsciiString > lines;
	size_t kept = 0;
	for( size_t index = 0; index < m_replays.size(); index++ )
	{
		DirectorReplay &replay = m_replays[ index ];
		if( !ObserverCamera_spliceNow( frame, replay.last, calm ) )
		{
			m_replays[ kept++ ] = replay;
			continue;
		}
		replay.splice = frame;
		lines.push_back( ObserverCamera_formatReplay( replay ) );
		DEBUG_LOG(( "OBSCAM frame %u instant replay of %s at frame %u goes in here%s\n", frame, replay.power.str(), replay.landed,
			calm ? ", the director calm" : ", the director still busy" ));
	}
	m_replays.resize( kept );

	if( !lines.empty() && m_replayFile == NULL && !m_replayFileTried )
	{
		m_replayFileTried = TRUE;
		AsciiString directory;
		directory.format( "%sVideos", TheGlobalData->getPath_UserData().str() );
		TheFileSystem->createDirectory( directory );
		AsciiString path;
		path.format( "%s/%s.replays", directory.str(), TheGlobalData->m_videoName.str() );
		m_replayFile = fopen( path.str(), "w" );
		if( m_replayFile == NULL )
			DEBUG_LOG(( "OBSCAM cannot write the replays file '%s', the film goes without replays\n", path.str() ));
		else
		{
			strlcpy( s_replaysWritten, path.str(), sizeof( s_replaysWritten ) );
			DEBUG_LOG(( "OBSCAM replays written to '%s'\n", path.str() ));
		}
	}
	if( m_replayFile == NULL )
		return;
	for( ; m_replayCrcsWritten < m_replayCrcs.size(); m_replayCrcsWritten++ )
		fprintf( m_replayFile, "crc %u %u\n", m_replayCrcs[ m_replayCrcsWritten ].frame, m_replayCrcs[ m_replayCrcsWritten ].crc );
	for( size_t index = 0; index < lines.size(); index++ )
		fprintf( m_replayFile, "%s\n", lines[ index ].str() );
	fflush( m_replayFile );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::isReplaying( void ) const
{
	return TheGlobalData != NULL && !TheGlobalData->m_directorReplaysFile.isEmpty();
}

//-------------------------------------------------------------------------------------------------
/** The replay pass's plan, read once.  A clip list an earlier run left is deleted first, so a pass
	* that gives up leaves none for the cut to splice in. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::loadReplays( void )
{
	if( m_replaysLoaded )
		return;
	m_replaysLoaded = TRUE;
	AsciiString clips;
	clips.format( "%sVideos/%s.txt", TheGlobalData->getPath_UserData().str(), TheGlobalData->m_videoName.str() );
	remove( clips.str() );

	const char *path = TheGlobalData->m_directorReplaysFile.str();
	FILE *file = fopen( path, "r" );
	if( file == NULL )
	{
		DEBUG_LOG(( "-directorreplays: no '%s', nothing to replay\n", path ));
		return;
	}
	char line[ 256 ];
	while( fgets( line, sizeof( line ), file ) != NULL )
	{
		CrcCheckpoint checkpoint;
		DirectorReplay replay;
		if( sscanf( line, "crc %u %u", &checkpoint.frame, &checkpoint.crc ) == 2 )
			m_replayCrcs.push_back( checkpoint );
		else if( ObserverCamera_parseReplay( line, &replay ) )
			m_replays.push_back( replay );
	}
	fclose( file );
	const size_t asked = m_replays.size();
	ObserverCamera_planReplays( m_replays );
	DEBUG_LOG(( "-directorreplays: %d replays, %d played (the rest overlap one before), %d CRC checkpoints\n", (Int)asked,
		(Int)m_replays.size(), (Int)m_replayCrcs.size() ));
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::replayRecords( UnsignedInt frame )
{
	loadReplays();
	if( m_replayGaveUp )
		return FALSE;
	const Int at = ObserverCamera_replayAt( m_replays, frame, 0 );
	return at >= 0;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::replaySkipsDraw( UnsignedInt frame )
{
	if( !isReplaying() || frame == 0 )
		return FALSE;
	loadReplays();
	return m_replayGaveUp || ObserverCamera_replayAt( m_replays, frame, REPLAY_LEAD_FRAMES ) < 0;
}

//-------------------------------------------------------------------------------------------------
/** Past the last replay's last frame the clip list is written: each replay's first picture in the
	* replays movie, its pictures, and the film's picture it goes in before, at -recordfps's pictures a
	* logic frame in both, the film starting on -directorrecord's first recorded frame. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::replaysFinished( UnsignedInt frame )
{
	loadReplays();
	if( m_replaysDone )
		return TRUE;
	if( !m_replayGaveUp && !m_replays.empty() && frame <= m_replays.back().last + 1 )
		return FALSE;
	m_replaysDone = TRUE;
	if( m_replayGaveUp || m_replays.empty() )
		return TRUE;

	AsciiString path;
	path.format( "%sVideos/%s.txt", TheGlobalData->getPath_UserData().str(), TheGlobalData->m_videoName.str() );
	FILE *file = fopen( path.str(), "w" );
	if( file == NULL )
	{
		DEBUG_LOG(( "-directorreplays: cannot write the clip list '%s'\n", path.str() ));
		return TRUE;
	}
	const Int pictures = TheGlobalData->m_videoPictures;
	UnsignedInt firstPicture = 0;
	for( size_t index = 0; index < m_replays.size(); index++ )
	{
		const DirectorReplay &replay = m_replays[ index ];
		const UnsignedInt count = ( replay.last - replay.first + 1 ) * (UnsignedInt)pictures;
		const UnsignedInt splicePicture = ObserverCamera_filmPicture( replay.splice, (UnsignedInt)TheGlobalData->m_videoStartFrame, pictures );
		fprintf( file, "%s\n", ObserverCamera_formatReplayClip( (Int)index, firstPicture, count, splicePicture, replay ).str() );
		firstPicture += count;
	}
	fclose( file );
	DEBUG_LOG(( "-directorreplays: %d clips, %u pictures, listed in '%s'\n", (Int)m_replays.size(), firstPicture, path.str() ));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The replay pass's camera: the filming pass's CRC checked on each of its checkpoints, the first that
	* differs ending the pass, and through each replay and the lead before it the target held on the
	* screen's middle pixel at the director's height, pushing in from a little higher over the replay. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateReplay( void )
{
	loadReplays();
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame != m_replayFrame )
	{
		m_replayFrame = frame;
		while( !m_replayGaveUp && !m_replayCrcs.empty() && m_replayCrcs.front().frame <= frame )
		{
			const CrcCheckpoint checkpoint = m_replayCrcs.front();
			m_replayCrcs.erase( m_replayCrcs.begin() );
			if( checkpoint.frame < frame )
				continue;
			const UnsignedInt crc = TheGameLogic->getCRC( CRC_RECALC );
			DEBUG_LOG(( "OBSCAM frame %u replay CRC: filmed 0x%08X replayed 0x%08X\n", frame, checkpoint.crc, crc ));
			if( crc == checkpoint.crc )
				continue;
			DEBUG_LOG(( "OBSCAM frame %u the match went another way than the film's, no replays\n", frame ));
			m_replayGaveUp = TRUE;
		}
	}

	const Int clip = m_replayGaveUp ? -1 : ObserverCamera_replayAt( m_replays, frame, REPLAY_LEAD_FRAMES );
	if( clip < 0 )
	{
		m_driving = FALSE;
		return;
	}
	const DirectorReplay &replay = m_replays[ clip ];
	const Real from = (Real)replay.first - (Real)REPLAY_LEAD_FRAMES;
	const Real progress = ( GameEngine_pictureFrame() - from ) / max( (Real)replay.last - from, 1.0f );
	const Real extra = DIRECTOR_WIDE_EXTRA + REPLAY_PUSH_HEIGHT * ( 1.0f - ObserverCamera_easeBetween( progress, 0.0f, 1.0f ) );
	driveHeight( extra );
	if( clip != m_replayClip )
	{
		m_replayClip = clip;
		TheTacticalView->setZoomToHeight( m_handHeight + extra );
		DEBUG_LOG(( "OBSCAM frame %u replay %d: %s by player %d at (%.0f,%.0f), frames %u to %u\n", frame, clip, replay.power.str(),
			replay.player, replay.target.x, replay.target.y, replay.first, replay.last ));
	}

	ViewLocation current;
	TheTacticalView->getLocation( &current );
	Coord2D middle;
	middle.x = TheDisplay->getWidth() * 0.5f;
	middle.y = TheDisplay->getHeight() * 0.5f;
	m_aimFrom = current;
	// the map's edge is measured from where placeOnPixel left the view aimed, as the window's camera
	// does: measured from the frame before, a camera far from the target was pushed off the map
	Coord2D look = placeOnPixel( replay.target, middle, current.getZoom() );
	ViewLocation aimed;
	aimed.init( look.x, look.y, current.getPosition().z, current.getAngle(), current.getPitch(), current.getZoom() );
	look = keepInMap( look, aimed );
	ViewLocation placed;
	placed.init( look.x, look.y, current.getPosition().z, current.getAngle(), current.getPitch(), current.getZoom() );
	TheTacticalView->setLocation( &placed );
	TheTacticalView->applyCameraConstraint();
	TheTacticalView->getPosition( &m_drivenTo );
	m_driving = TRUE;
}

//-------------------------------------------------------------------------------------------------
DirectorEvent *ObserverCamera::findEvent( UnsignedInt id )
{
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		if( m_events[ index ].id == id )
			return &m_events[ index ];
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::notePlayerView( Int playerIndex, const ViewLocation &view )
{
	if( !m_playerViews[ playerIndex ].isValid() )
		DEBUG_LOG(( "OBSCAM frame %u first camera from player %d\n", TheGameLogic->getFrame(), playerIndex ));
	m_playerViews[ playerIndex ] = view;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::setMode( ObserverCameraMode mode )
{
	// the lever to Free or Director is the watcher's own choice and ends a look; the watcher scrolling
	// off a player's camera (update) drops the mode without coming here, and the look stays
	if( mode != OBSERVER_CAMERA_PLAYER )
		endLook();
	m_mode = mode;
	m_driving = FALSE;
	m_placeValid = FALSE;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::followPlayer( Int playerIndex )
{
	m_followed = playerIndex;
	m_driving = FALSE;
	m_placeValid = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Picking a second player while looking through the first keeps the camera the watcher had before
	* either, so one Esc always goes back to it. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::lookThrough( Int playerIndex, Bool takeFog )
{
	if( !m_looking )
	{
		m_modeBeforeLook = m_mode == OBSERVER_CAMERA_PLAYER ? OBSERVER_CAMERA_FREE : m_mode;
		m_lookTookFog = FALSE;
	}
	followPlayer( playerIndex );
	if( takeFog && !m_fog )
	{
		m_fog = TRUE;
		m_lookTookFog = TRUE;
	}
	setMode( OBSERVER_CAMERA_PLAYER );
	m_looking = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A follow with no look, the director kept to a player, lets go of him and keeps the mode; only a
	* player's camera with nobody left to show falls back to the watcher's own. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::stopLooking( void )
{
	if( m_followed == NO_PLAYER && !m_looking )
		return FALSE;

	followPlayer( NO_PLAYER );
	const ObserverCameraMode back = m_looking ? m_modeBeforeLook : m_mode;
	endLook();
	setMode( back == OBSERVER_CAMERA_PLAYER ? OBSERVER_CAMERA_FREE : back );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Every road out of a look comes here: the fog the look turned on goes off with it. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::endLook( void )
{
	if( m_looking && m_lookTookFog )
		m_fog = FALSE;
	m_looking = FALSE;
	m_lookTookFog = FALSE;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera::getShroudPlayerIndex( void ) const
{
	return m_shroudViewer != NO_PLAYER ? m_shroudViewer : ThePlayerList->getLocalPlayer()->getPlayerIndex();
}

//-------------------------------------------------------------------------------------------------
/** The fog is the followed player's while it is on.  Swapping it is what the debug key that makes
	* you another player does to the fog: his ghosts of what he last saw in, and the ground redrawn
	* in his shroud.  A knocked-out player's machine kept only his own fog memory until now, so it
	* starts keeping everybody's the first time he asks for somebody else's; what the new viewer saw
	* before that has no snapshot and is not drawn in his fog, shadow included.
	*
	* The frame each drawable was last seen clear belongs to the old viewer and is cleared, or a unit
	* he saw stays drawn two seconds into the new viewer's fog. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateShroudViewer( void )
{
	const Int viewer = m_fog ? m_followed : NO_PLAYER;
	if( viewer == m_shroudViewer )
		return;

	m_shroudViewer = viewer;
	TheGhostObjectManager->setTrackAllPlayers( TRUE );
	TheGhostObjectManager->setLocalPlayerIndex( getShroudPlayerIndex() );
	ThePartitionManager->refreshShroudForLocalPlayer();
	for( Drawable *draw = TheGameClient->firstDrawable(); draw != NULL; draw = draw->getNextDrawable() )
		draw->setShroudClearFrame( 0 );
}

//-------------------------------------------------------------------------------------------------
/** While a player's camera is shown, his zoom is: the view otherwise eases its height back towards
	* the watcher's own every frame and the two meet two thirds of the way.  Only ever let go when
	* this took it, so the cinema's hold on the height is not undone. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::holdHeight( Bool hold )
{
	if( hold == m_holdingHeight )
		return;

	m_holdingHeight = hold;
	TheTacticalView->setOkToAdjustHeight( !hold );
}

//-------------------------------------------------------------------------------------------------
/** The director's height over a fight is the watcher's own plus extra.  It is set as the height
	* the view wants, so the view's own easing carries the camera there rather than a jump.  A turn
	* of the wheel while it drives moves the height the view wants, and that turn is the watcher's:
	* it is added to his own height, so the director never takes it back. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::driveHeight( Real extra )
{
	const Real now = TheTacticalView->getHeightAboveGround();
	if( m_heightDriven )
		m_handHeight += now - m_drivenHeight;
	else
		m_handHeight = now;
	m_heightDriven = TRUE;
	TheTacticalView->setHeightSettleSeconds( DIRECTOR_SETTLE_SECONDS );

	const Real wanted = m_handHeight + extra;
	if( wanted != now )
		TheTacticalView->setHeightAboveGround( wanted );
	m_drivenHeight = TheTacticalView->getHeightAboveGround();
}

//-------------------------------------------------------------------------------------------------
/** The watcher's own height back, with any turn of the wheel since the last frame kept. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::releaseHeight( void )
{
	if( !m_heightDriven )
		return;

	m_heightDriven = FALSE;
	TheTacticalView->setHeightSettleSeconds( 0.0f );
	TheTacticalView->setHeightAboveGround( m_handHeight + TheTacticalView->getHeightAboveGround() - m_drivenHeight );
}

//-------------------------------------------------------------------------------------------------
/** The screen's corners on the ground round the point looked at now, so the place keeps the whole
	* screen over the map.  They are measured at the current zoom: while the camera rises the corners
	* spread and the place is pulled further in, frame by frame. */
//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera::keepInMap( const Coord2D &place, const ViewLocation &current ) const
{
	const Coord3D &at = current.getPosition();
	Coord3D world[ 4 ];
	TheTacticalView->getScreenCornerWorldPointsAtZ( &world[ 0 ], &world[ 1 ], &world[ 2 ], &world[ 3 ],
		TheTerrainLogic->getGroundHeight( at.x, at.y ) );
	Coord2D corners[ 4 ];
	for( Int index = 0; index < 4; index++ )
	{
		corners[ index ].x = world[ index ].x - at.x;
		corners[ index ].y = world[ index ].y - at.y;
	}

	return ObserverCamera_keepInMap( place, corners, 4, mapRegion() );
}

//-------------------------------------------------------------------------------------------------
Region2D ObserverCamera::mapRegion( void ) const
{
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	Region2D map;
	map.lo.x = extent.lo.x;
	map.lo.y = extent.lo.y;
	map.hi.x = extent.hi.x;
	map.hi.y = extent.hi.y;
	return map;
}

//-------------------------------------------------------------------------------------------------
/** The view looking at look from zoom, at this update's angle, pitch and height, its camera built
	* now, so the view's own picks and projections answer for that camera before anything draws.  The
	* director's and the window's cameras are placed by asking the view: two cuts that predicted its projection both drew the
	* subject off its pixel and the map's edge into the picture. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::aimView( const Coord2D &look, Real zoom )
{
	ViewLocation aimed;
	aimed.init( look.x, look.y, m_aimFrom.getPosition().z, m_aimFrom.getAngle(), m_aimFrom.getPitch(), zoom );
	TheTacticalView->setLocation( &aimed );
	TheTacticalView->aimCamera();
}

//-------------------------------------------------------------------------------------------------
/** The look point at zoom that draws subject on pixel.  The camera moves with its look point, so the
	* ground under pixel, picked on the subject's own height, is moved onto the subject; again, for the
	* view's constraint and its lift over a hill, which do not move with it.  The view is left aimed
	* there. */
//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera::placeOnPixel( const Coord2D &subject, const Coord2D &pixel, Real zoom )
{
	enum { PLACE_PASSES = 3 };
	const Real ground = TheTerrainLogic->getGroundHeight( subject.x, subject.y );
	ICoord2D at;
	at.x = REAL_TO_INT( pixel.x );
	at.y = REAL_TO_INT( pixel.y );
	Coord2D look = subject;
	for( Int pass = 0; pass < PLACE_PASSES; pass++ )
	{
		aimView( look, zoom );
		Coord3D hit;
		TheTacticalView->screenToWorldAtZ( &at, &hit, ground );
		look.x += subject.x - hit.x;
		look.y += subject.y - hit.y;
	}
	aimView( look, zoom );
	return look;
}

//-------------------------------------------------------------------------------------------------
/** The ground the whole picture spans across its middle row, at the height of the ground the view
	* looks at, through the camera the last draw built. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::measureScreenGround( void )
{
	Coord3D look;
	TheTacticalView->getPosition( &look );
	const Real ground = TheTerrainLogic->getGroundHeight( look.x, look.y );
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	ICoord2D left, right;
	left.x = 0;
	right.x = width - 1;
	left.y = right.y = height / 2;
	Coord3D leftGround, rightGround;
	TheTacticalView->screenToWorldAtZ( &left, &leftGround, ground );
	TheTacticalView->screenToWorldAtZ( &right, &rightGround, ground );
	const Real dx = rightGround.x - leftGround.x;
	const Real dy = rightGround.y - leftGround.y;
	m_screenGround = sqrtf( dx * dx + dy * dy );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera::splitApart( const Coord2D & ) const
{
	// off the director's picture, the way a second power has to be for the window
	return max( SPLIT_APART, m_screenGround * OFF_PICTURE_SHARE );
}

//-------------------------------------------------------------------------------------------------
/** -directorrecord: whether there is a second fight off the picture for the corner window, and where.
	* It was the split screen's rule and keeps its holds: asked again only when the hits were counted
	* again, only real fighting counts (a base going up, or a dozer clearing trees, is no second
	* fight), and a special power the director is showing counts as a fight.  A second power off the
	* picture outranks it in the window (updateWindow). */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateSecondFight( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	measureScreenGround();
	Coord2D middle;
	Real firstHeat = ObserverCamera_heatAround( m_fights, m_place, &middle );
	if( m_placeKind == PLACE_EVENT )
		firstHeat = max( firstHeat, SPLIT_ENTER_HEAT );
	// a second fight is looked for off the director's picture; one up keeps the distance it came at,
	// so the director zooming out over a growing fight does not end it
	const Real searched = m_split ? m_splitApart : splitApart( m_place );
	Coord2D second = { 0.0f, 0.0f };
	Real secondHeat = 0.0f;
	ObserverCamera_secondPlace( m_fights, m_place, searched, &second, &secondHeat );
	if( secondHeat > 0.0f && ObserverCamera_fizzles( m_timeline, second, frame ) )
		secondHeat = 0.0f;
	// with a timeline a second fight is decided before it comes: the director's place needs a fight the
	// scouting pass saw last through the least hold, or a special power, or it is about to leave for
	// something else, perhaps the second fight itself, and the window would come and go at once
	const Bool timelineKnown = !m_timeline.empty();
	const UnsignedInt heldTo = frame + SPLIT_LEAST_FRAMES;
	const Bool firstLasts = !timelineKnown || m_placeKind == PLACE_EVENT || ObserverCamera_fightLasts( m_timeline, m_place, frame, heldTo, NULL );
	// the scouting pass knows the pairs: a second fight worth filming about to begin far enough away
	// comes before its first shot, the window waiting where it begins, and stays while it lasts.  Once
	// it is going it is wherever the hits are, and in a lull the window stays put; sent back to where
	// it began, the split's pane sat on empty ground while the fight went on elsewhere.  The director
	// that moved onto the second fight has been handed it, and it ends there
	const Real sameGround = searched * SPLIT_SAME_GROUND_SHARE;
	const Bool handedOver = m_split && within( m_place, m_secondPlace, sameGround );
	const Bool mayPlan = m_placeKind != PLACE_SIGHT && ( m_split || firstLasts );
	const Int planned = mayPlan ? ObserverCamera_plannedSecond( m_timeline, m_place, frame, searched, m_split ) : -1;
	// a planned moment whose split was ended early does not open another
	const Bool plannedSplit = planned >= 0 && planned != m_spentMoment;
	if( plannedSplit && frame < m_timeline[ planned ].start && ( secondHeat <= 0.0f || !within( second, m_timeline[ planned ].place, sameGround ) ) )
		second = m_timeline[ planned ].place;
	else if( m_split && secondHeat <= 0.0f )
		second = m_secondPlace;

	// the second fight is followed once it has moved a little, the way the director follows its own
	Coord2D shown = m_secondPlace;
	const Real dx = second.x - m_secondPlace.x;
	const Real dy = second.y - m_secondPlace.y;
	if( !m_split || dx * dx + dy * dy > DIRECTOR_FOLLOW_SLACK * DIRECTOR_FOLLOW_SLACK )
		shown = second;
	const Real ax = shown.x - m_place.x;
	const Real ay = shown.y - m_place.y;
	const Real apart = sqrtf( ax * ax + ay * ay );

	const Real needed = m_split ? m_splitApart : splitApart( shown );
	const UnsignedInt since = frame >= m_splitChanged ? frame - m_splitChanged : 0;
	// a planned one rests after the last one like any other, so the window does not go and come back
	const Bool plannedOpens = plannedSplit && ( m_split || since >= SPLIT_REST_FRAMES );
	Bool split = plannedOpens || ObserverCamera_holdSplit( m_split, firstHeat, secondHeat, apart, needed, since );
	// a live one comes, with a timeline, only on two different fights the scouting pass saw last: a
	// crowd beside the director's that the pass saw as part of it was that fight spreading, and the
	// split it opened went off again two seconds later, once the director had followed it there
	if( split && !m_split && !plannedSplit && timelineKnown )
		split = firstLasts && ObserverCamera_fightLasts( m_timeline, shown, frame, heldTo, &m_place );
	split = split && !handedOver && apart > needed * SPLIT_SAME_GROUND_SHARE;
	// the second fight goes when it has been up long enough and when it is now somewhere else; the
	// director on it ends it above
	const Bool upTooLong = m_split && since >= SPLIT_MOST_FRAMES;
	const Bool secondLeft = m_split && !within( shown, m_secondPlace, sameGround );
	if( split && ( upTooLong || secondLeft ) )
	{
		DEBUG_LOG(( "OBSCAM frame %u second fight ends:%s%s\n", frame, upTooLong ? " up too long" : "",
			secondLeft ? " the second fight is elsewhere" : "" ));
		split = FALSE;
		if( planned >= 0 )
			m_spentMoment = planned;
	}
	if( split != m_split )
	{
		DEBUG_LOG(( "OBSCAM frame %u second fight %s, first heat %.1f, second (%.0f,%.0f) heat %.1f, %.0f apart of %.0f needed, screen %.0f%s%s\n",
			frame, split ? "on" : "off", firstHeat, shown.x, shown.y, secondHeat, apart, needed, m_screenGround, plannedSplit ? ", planned" : "",
			handedOver ? ", the director took it" : "" ));
		m_split = split;
		m_splitChanged = frame;
		m_splitApart = needed;
	}
	if( m_split )
		m_secondPlace = shown;
}

//-------------------------------------------------------------------------------------------------
/** -directorrecord's corner window, on every logic frame from what the last scan decided: a second
	* power off the picture (directorPlace's m_eventSecond) first, else the second fight.  It slides all
	* the way in before it goes and all the way out before it comes again; while up, a new subject takes
	* it at once, its camera gliding there or cutting the way the director's does. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateWindow( UnsignedInt frame )
{
	const DirectorEvent *power = m_eventSecond != 0 ? findEvent( m_eventSecond ) : NULL;
	const DirectorWindowKind wanted = ObserverCamera_pickWindow( power != NULL ? power->id : 0, m_split );
	const UnsignedInt since = frame >= m_windowChanged ? frame - m_windowChanged : 0;
	const Bool on = ObserverCamera_holdWindow( m_windowOn, wanted != DIRECTOR_WINDOW_NONE, since );
	if( on != m_windowOn )
	{
		DEBUG_LOG(( "OBSCAM frame %u window %s\n", frame, on ? "in" : "out" ));
		m_windowOn = on;
		m_windowChanged = frame;
	}
	// going out, or still in with nothing left to show, it keeps what it had
	if( !m_windowOn || wanted == DIRECTOR_WINDOW_NONE )
		return;
	const Coord2D subject = power != NULL ? power->target : m_secondPlace;
	if( wanted != m_windowKind || !within( subject, m_windowSubject, DIRECTOR_GATHER_RADIUS ) )
	{
		if( power != NULL )
			DEBUG_LOG(( "OBSCAM frame %u window on special power %u %s at (%.0f,%.0f), the director at (%.0f,%.0f)\n", frame, power->id,
				power->power != NULL ? power->power->getName().str() : "warhead", subject.x, subject.y, m_place.x, m_place.y ));
		else
			DEBUG_LOG(( "OBSCAM frame %u window on the second fight at (%.0f,%.0f), the director at (%.0f,%.0f)\n", frame,
				subject.x, subject.y, m_place.x, m_place.y ));
	}
	m_windowKind = wanted;
	m_windowSubject = subject;
	const PlayerMaskType sides = power != NULL ? ( power->owner != NULL ? power->owner->getPlayerMask() : 0 ) : sidesAround( subject );
	// a fight's lull names who was in it
	if( sides != 0 || power != NULL )
		m_windowSides = sides;
}

//-------------------------------------------------------------------------------------------------
/** How far the window is in at the picture's time. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::easeWindow( Real at )
{
	const Real slid = ObserverCamera_easeFrames( at, m_windowChanged, WINDOW_SLIDE_FRAMES );
	m_windowProgress = m_windowOn ? slid : m_windowChanged == 0 ? 0.0f : 1.0f - slid;
}

//-------------------------------------------------------------------------------------------------
/** The window's camera: the director's own angle, pitch and zoom, gliding to its subject on a spring of
	* its own and cutting where the director would, the subject on the middle of the picture it shrinks,
	* moved in where that would show ground past the map. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::stepWindowCamera( const ViewLocation &step, Real elapsedSeconds )
{
	if( !isWindowDrawn() )
	{
		m_windowGlide = ViewLocation();
		return;
	}
	const Coord3D &at = step.getPosition();
	ViewLocation subject;
	subject.init( m_windowSubject.x, m_windowSubject.y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
	if( !m_windowGlide.isValid() )
	{
		m_windowVelocity.x = m_windowVelocity.y = m_windowVelocity.z = 0.0f;
		m_windowVelocity.angle = m_windowVelocity.pitch = m_windowVelocity.zoom = 0.0f;
		m_windowGlide = subject;
	}
	else
	{
		const ViewLocation glide = ObserverCamera_approach( m_windowGlide, subject, elapsedSeconds, DIRECTOR_PAN_SECONDS,
			DIRECTOR_TOP_SPEED, &m_windowVelocity );
		m_windowGlide.init( glide.getPosition().x, glide.getPosition().y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
	}
	Coord2D middle;
	middle.x = TheDisplay->getWidth() * 0.5f;
	middle.y = TheDisplay->getHeight() * 0.5f;
	Coord2D glided;
	glided.x = m_windowGlide.getPosition().x;
	glided.y = m_windowGlide.getPosition().y;
	m_aimFrom = step;
	Coord2D look = placeOnPixel( glided, middle, step.getZoom() );
	ViewLocation aimed;
	aimed.init( look.x, look.y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
	look = keepInMap( look, aimed );
	m_windowView.init( look.x, look.y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
}

//-------------------------------------------------------------------------------------------------
IRegion2D ObserverCamera::getWindowRect( void ) const
{
	return ObserverCamera_windowRect( TheDisplay->getWidth(), TheDisplay->getHeight(), m_windowProgress );
}

//-------------------------------------------------------------------------------------------------
PlayerMaskType ObserverCamera::getWindowSides( void ) const
{
	return m_windowSides;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::beginWindowPass( void )
{
	TheTacticalView->getLocation( &m_firstView );
	TheTacticalView->setLocation( &m_windowView );
	m_drawingPane = OBSERVER_WINDOW_PANE;
}

//-------------------------------------------------------------------------------------------------
/** The corner window's second fight: its sides are every player in the hits within the gather
	* radius of it. */
//-------------------------------------------------------------------------------------------------
PlayerMaskType ObserverCamera::sidesAround( const Coord2D &subject ) const
{
	PlayerMaskType sides = 0;
	for( size_t index = 0; index < m_fights.size(); index++ )
		if( sameFight( m_fights[ index ].position, subject ) )
			sides |= m_fightSides[ index ];
	return sides;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::endWindowPass( void )
{
	TheTacticalView->setLocation( &m_firstView );
	m_drawingPane = 0;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::takenByHand( const ViewLocation &current ) const
{
	if( TheLookAtTranslator->isMovingCamera() )
		return TRUE;

	const Real dx = current.getPosition().x - m_drivenTo.x;
	const Real dy = current.getPosition().y - m_drivenTo.y;
	return dx * dx + dy * dy > HAND_JUMP_DISTANCE * HAND_JUMP_DISTANCE;
}

//-------------------------------------------------------------------------------------------------
/** The players a recent hit was between, when one dealt it to another he is at war with; none for a
	* tree a dozer cleared, or a building's own wear, which is no fight. */
//-------------------------------------------------------------------------------------------------
static PlayerMaskType fightSides( const Object *obj, const BodyModuleInterface *body )
{
	const PlayerMaskType sourceMask = body->getLastDamageInfo()->in.m_sourcePlayerMask;
	const Player *victim = obj->getControllingPlayer();
	if( sourceMask == 0 || victim == NULL || ( sourceMask & victim->getPlayerMask() ) != 0 )
		return 0;
	const Player *source = ThePlayerList->getPlayerFromMask( sourceMask );
	if( source == NULL || victim->getRelationship( source->getDefaultTeam() ) != ENEMIES )
		return 0;
	return sourceMask | victim->getPlayerMask();
}

//-------------------------------------------------------------------------------------------------
/** The scouting pass counts the hits the way the director does on the same scan frames, so a fight
	* starts on the scan the filming pass would first have seen it. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::scout( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( frame == m_scoutScanned )
		return;
	m_scoutScanned = frame;
	if( frame % SCOUT_CRC_FRAMES == 0 )
	{
		const CrcCheckpoint checkpoint = { frame, TheGameLogic->getCRC( CRC_RECALC ) };
		m_scoutCrcs.push_back( checkpoint );
	}
	if( frame % DIRECTOR_SCAN_FRAMES != 0 )
		return;

	std::vector< DirectorFightHit > hits;
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		// a thing with no body module takes no damage, so it has no hits to count
		const BodyModuleInterface *body = obj->getBodyModule();
		if( body == NULL )
			continue;
		const UnsignedInt hitAt = body->getLastDamageTimestamp();
		if( hitAt == 0 || frame >= hitAt + DIRECTOR_HEAT_FRAMES )
			continue;
		DirectorFightHit hit;
		hit.sides = fightSides( obj, body );
		if( hit.sides == 0 )
			continue;
		hit.position.x = obj->getPosition()->x;
		hit.position.y = obj->getPosition()->y;
		hit.weight = ObserverCamera_hitWeight( obj->getTemplate()->friend_getBuildCost(), obj->isEffectivelyDead(),
			IsSuperweaponInMatch( obj->getTemplate() ), obj->isKindOf( KINDOF_INFANTRY ) );
		hits.push_back( hit );
	}
	ObserverCamera_markLandings( m_scouted, hits, frame );
	ObserverCamera_trackFights( m_scouted, hits, frame );
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::finishScout( void )
{
	const char *path = TheGlobalData->m_directorScoutFile.str();
	FILE *file = fopen( path, "w" );
	if( file == NULL )
	{
		DEBUG_LOG(( "-directorscout: cannot write '%s', the filming pass films live\n", path ));
		return;
	}
	// the last frame the pass counted on: a replay that ran out has gone back to the shell by now
	ObserverCamera_closeTimeline( m_scouted, m_scoutScanned );
	Int worth = 0;
	for( size_t index = 0; index < m_scouted.size(); index++ )
	{
		const AsciiString line = ObserverCamera_formatMoment( m_scouted[ index ] );
		fprintf( file, "%s\n", line.str() );
		if( !ObserverCamera_worthFilming( m_scouted[ index ] ) )
			continue;
		worth++;
		DEBUG_LOG(( "OBSCAM scouted %s\n", line.str() ));
	}
	for( size_t index = 0; index < m_scoutCrcs.size(); index++ )
		fprintf( file, "crc %u %u\n", m_scoutCrcs[ index ].frame, m_scoutCrcs[ index ].crc );
	fclose( file );
	DEBUG_LOG(( "-directorscout: %d moments, %d worth filming, %d CRC checkpoints, to frame %u, written to '%s'\n",
		(Int)m_scouted.size(), worth, (Int)m_scoutCrcs.size(), m_scoutScanned, path ));
}

//-------------------------------------------------------------------------------------------------
/** The file is the scouting pass's, made for this run alone, and gone once it is read. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::loadTimeline( void )
{
	m_timelineLoaded = TRUE;
	const char *path = TheGlobalData->m_directorTimelineFile.str();
	if( TheGlobalData->m_directorTimelineFile.isEmpty() )
		return;
	FILE *file = fopen( path, "r" );
	if( file == NULL )
	{
		DEBUG_LOG(( "-directortimeline: no '%s', the director films live\n", path ));
		return;
	}
	char line[ 256 ];
	Int worth = 0;
	while( fgets( line, sizeof( line ), file ) != NULL )
	{
		CrcCheckpoint checkpoint;
		DirectorMoment moment;
		if( sscanf( line, "crc %u %u", &checkpoint.frame, &checkpoint.crc ) == 2 )
			m_timelineCrcs.push_back( checkpoint );
		else if( ObserverCamera_parseMoment( line, &moment ) )
		{
			m_timeline.push_back( moment );
			if( ObserverCamera_worthFilming( moment ) )
				worth++;
		}
	}
	fclose( file );
	remove( path );
	DEBUG_LOG(( "-directortimeline: %d moments, %d worth filming, %d CRC checkpoints\n", (Int)m_timeline.size(), worth,
		(Int)m_timelineCrcs.size() ));
}

//-------------------------------------------------------------------------------------------------
/** Same seed and same switches play the same match, and the checkpoints are how that is known rather
	* than hoped: the first that differs throws the timeline away and the rest is filmed live. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::checkTimeline( UnsignedInt frame )
{
	while( !m_timelineCrcs.empty() && m_timelineCrcs.front().frame <= frame )
	{
		const CrcCheckpoint checkpoint = m_timelineCrcs.front();
		m_timelineCrcs.erase( m_timelineCrcs.begin() );
		if( checkpoint.frame < frame )
			continue;
		const UnsignedInt crc = TheGameLogic->getCRC( CRC_RECALC );
		DEBUG_LOG(( "OBSCAM frame %u timeline CRC: scouted 0x%08X filmed 0x%08X\n", frame, checkpoint.crc, crc ));
		if( crc == checkpoint.crc )
			continue;
		DEBUG_LOG(( "OBSCAM frame %u the match went another way than the scouting pass's, the director films live\n", frame ));
		m_timeline.clear();
		m_timelineCrcs.clear();
		m_placeMoment = -1;
		if( m_placeKind == PLACE_UPCOMING )
			m_placeValid = FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
/** Where the director looks: the fight with the most at stake, held for a while, followed as it
	* moves, and left for a clearly bigger one.  With no fight anywhere it goes round what is worth
	* seeing instead, an army on the move, a base going up, a superweapon, a few seconds each and not
	* straight back to one it has just shown.  A special power beats all of it and is held until
	* nothing has hit the ground there for a few seconds.  Narrowed to one player it counts only the
	* hits on his things, the hits his things made, his own sights and the special powers he used or
	* had used on him.  It only reads the logic. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::directorPlace( const Player *narrowTo, Coord2D *place )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( narrowTo != m_placeFor )
	{
		m_placeFor = narrowTo;
		m_placeValid = FALSE;
	}
	if( m_placeValid && frame >= m_placeScanned && frame < m_placeScanned + DIRECTOR_SCAN_FRAMES )
	{
		*place = m_place;
		return TRUE;
	}
	m_placeScanned = frame;

	std::vector< DirectorHeat > hits;
	std::vector< DirectorHeat > fights;
	std::vector< PlayerMaskType > sides;
	std::vector< DirectorHeat > sights;
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		const Int cost = obj->getTemplate()->friend_getBuildCost();
		const Bool superweapon = IsSuperweaponInMatch( obj->getTemplate() );
		const Bool mine = narrowTo == NULL || obj->getControllingPlayer() == narrowTo;
		DirectorHeat heat;
		heat.position.x = obj->getPosition()->x;
		heat.position.y = obj->getPosition()->y;

		if( mine && cost > 0 && !obj->isEffectivelyDead() )
		{
			const AIUpdateInterface *ai = obj->getAIUpdateInterface();
			const Bool marching = ai != NULL && ai->isMoving() && obj->isAbleToAttack();
			const Bool busy = marching || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION );
			heat.weight = ObserverCamera_sightWeight( cost, obj->isKindOf( KINDOF_STRUCTURE ), busy, superweapon );
			sights.push_back( heat );
		}

		// a thing with no body module takes no damage, so it has no hits to count
		const BodyModuleInterface *body = obj->getBodyModule();
		if( body == NULL )
			continue;
		const UnsignedInt hitAt = body->getLastDamageTimestamp();
		if( hitAt == 0 || frame >= hitAt + DIRECTOR_HEAT_FRAMES )
			continue;
		if( !mine && ( body->getLastDamageInfo()->in.m_sourcePlayerMask & narrowTo->getPlayerMask() ) == 0 )
			continue;

		heat.weight = ObserverCamera_hitWeight( cost, obj->isEffectivelyDead(), superweapon, obj->isKindOf( KINDOF_INFANTRY ) );
		hits.push_back( heat );

		// -directorrecord's second fight counts only fights, and the window's chip names who is in them
		const PlayerMaskType between = fightSides( obj, body );
		if( between == 0 )
			continue;
		fights.push_back( heat );
		sides.push_back( between );
	}
	m_fights = fights;
	m_fightSides = sides;

	Coord2D hottest;
	Real hottestHeat = 0.0f;
	ObserverCamera_hottestPlace( hits, &hottest, &hottestHeat );
	// a fight the scouting pass saw come to nothing is not worth the trip
	const Bool timeline = narrowTo == NULL && !m_timeline.empty();
	if( timeline && hottestHeat > 0.0f && ObserverCamera_fizzles( m_timeline, hottest, frame ) )
	{
		if( !sameFight( hottest, m_skippedFight ) )
			DEBUG_LOG(( "OBSCAM frame %u skips a fight at (%.0f,%.0f) heat %.1f the scout saw fizzle\n", frame, hottest.x, hottest.y, hottestHeat ));
		m_skippedFight = hottest;
		hottestHeat = 0.0f;
	}

	const UnsignedInt held = frame >= m_placeSince ? frame - m_placeSince : 0;
	Coord2D followed = m_place;

	// a special power outranks any fight.  Narrowed to one player, only his own and the ones that
	// land on his things count, the way only his fights do
	dropOldEvents( frame );
	DirectorEvent *best = NULL;
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		DirectorEvent &event = m_events[ index ];
		Coord2D middle;
		if( event.owner != narrowTo && narrowTo != NULL && ObserverCamera_heatAround( sights, event.target, &middle ) <= 0.0f )
			continue;
		// hits at the target keep it going: the missiles arriving, the bombs, the fires after
		if( event.landed && frame >= event.since + EVENT_LAUNCH_FRAMES && ObserverCamera_heatAround( hits, event.target, &middle ) > 0.0f )
			event.until = ObserverCamera_eventKeptTo( event, frame, EVENT_AFTERMATH_FRAMES );
		// one landing late, a strike flown in from the map's edge, is shown from when it lands
		if( !event.landed && ObserverCamera_landsNow( event, frame, !m_timeline.empty(), ObserverCamera_heatAround( fights, event.target, &middle ) > 0.0f ) )
		{
			event.landed = TRUE;
			event.since = frame;
			event.until = frame + EVENT_FRAMES;
			if( event.landedAt == 0 )
				event.landedAt = max( event.landsAt, frame );
			if( TheGlobalData->m_directorRecord && event.power != NULL )
				DEBUG_LOG(( "OBSCAM frame %u special power %u %s lands\n", frame, event.id, event.power->getName().str() ));
		}
		if( !event.landed )
			continue;
		if( best == NULL || event.weight >= best->weight )
			best = &event;
	}
	// a power takes the camera from anything the moment it lands, settled or not: waiting out the
	// settle, S1-A1 showed special power 3 three seconds after it hit.  Two at once off each other's
	// picture each keep theirs while both go on, the one shown first on the whole picture and the other
	// in the corner window; the heavier taking the picture would swap the two
	const Bool windows = TheGlobalData->m_directorRecord && narrowTo == NULL;
	DirectorEvent *current = m_placeValid && m_placeKind == PLACE_EVENT ? findEvent( m_placeEvent ) : NULL;
	const Bool bestOffPicture = windows && current != NULL && best != NULL && best != current
		&& ObserverCamera_offPicture( best->target, current->target, m_screenGround );
	DirectorEvent *shown = NULL;
	if( bestOffPicture || ObserverCamera_stayOnEvent( current, best ) )
		shown = current;
	else if( best != NULL )
	{
		DEBUG_LOG(( "OBSCAM frame %u director to special power %u %s at (%.0f,%.0f)%s\n", frame, best->id,
			best->power != NULL ? best->power->getName().str() : "warhead", best->target.x, best->target.y,
			best->superweapon ? " superweapon" : "" ));
		m_placeKind = PLACE_EVENT;
		m_placeHeight = 0.0f;
		m_placeEvent = best->id;
		m_placeSince = frame;
		m_placeValid = TRUE;
		shown = best;
	}
	const UnsignedInt heldSecond = m_eventSecond;
	m_eventSecond = 0;
	if( shown != NULL )
	{
		m_place = shown->target;
		shown->shown = TRUE;
		// -directorrecord: the heaviest other power landing off its picture at the same time goes into the
		// corner window, ahead of any second fight
		if( windows )
			m_eventSecond = ObserverCamera_secondPower( m_events, shown->id, heldSecond, m_place, m_screenGround );
		DirectorEvent *second = findEvent( m_eventSecond );
		if( second != NULL )
			second->shown = TRUE;
		*place = m_place;
		return TRUE;
	}

	// a fight the scouting pass saw begin is waited for where it begins, so the picture has it from
	// the first shot; it becomes an ordinary fight when it starts.  It is gone to from nothing, from a
	// fight that has settled when it is clearly bigger, and from anything for a superweapon's silo
	if( m_placeValid && m_placeKind == PLACE_UPCOMING )
	{
		const DirectorMoment &waited = m_timeline[ m_placeMoment ];
		if( frame < ( waited.power ? max( waited.last, waited.start ) : waited.start ) )
		{
			*place = m_place;
			return TRUE;
		}
		m_placeKind = PLACE_FIGHT;
		m_placePeak = 0.0f;
		m_placeSince = frame;
	}
	// the fight the window holds stays the window's: the director going there swapped the two pictures
	const Int upcoming = timeline ? ObserverCamera_prerollMoment( m_timeline, frame, m_split ? &m_secondPlace : NULL, m_splitApart ) : -1;
	if( upcoming >= 0 && upcoming != m_placeMoment )
	{
		const DirectorMoment &moment = m_timeline[ upcoming ];
		Coord2D middle;
		const Real heatHere = m_placeValid && m_placeKind == PLACE_FIGHT ? ObserverCamera_heatAround( hits, m_place, &middle ) : 0.0f;
		if( moment.power || heatHere <= 0.0f || ( held >= DIRECTOR_SETTLE_FRAMES && moment.peak > heatHere * DIRECTOR_SWITCH_MARGIN ) )
		{
			// a power is waited for where it lands, not where it is fired from
			const Coord2D &waitAt = moment.power ? moment.target : moment.place;
			DEBUG_LOG(( "OBSCAM frame %u pre-roll to %s at frame %u (%.0f,%.0f) peak %.1f\n", frame,
				moment.superweapon ? "superweapon" : moment.power ? "special power" : "fight", moment.power ? max( moment.last, moment.start ) : moment.start,
				waitAt.x, waitAt.y, moment.peak ));
			m_place = waitAt;
			m_placeKind = PLACE_UPCOMING;
			m_placeHeight = 0.0f;
			m_placeMoment = upcoming;
			m_placeSince = frame;
			m_placeValid = TRUE;
			*place = m_place;
			return TRUE;
		}
	}

	if( m_placeValid && m_placeKind == PLACE_FIGHT )
	{
		const Real heatHere = ObserverCamera_heatAround( hits, m_place, &followed );
		// the director keeps its own fight while it goes on rather than take the window's; once it is
		// over the director moves there and the window lets it go
		const Bool heldByWindow = m_split && within( hottest, m_secondPlace, m_splitApart );
		m_placePeak = max( m_placePeak, heatHere );
		if( heatHere > 0.0f && ( sameFight( hottest, followed ) || heldByWindow || !ObserverCamera_shouldMove( heatHere, hottestHeat, held, m_placePeak ) ) )
		{
			const Real dx = followed.x - m_place.x;
			const Real dy = followed.y - m_place.y;
			if( dx * dx + dy * dy > DIRECTOR_FOLLOW_SLACK * DIRECTOR_FOLLOW_SLACK )
				m_place = followed;
			m_placeHeight = ObserverCamera_fightHeight( ObserverCamera_spreadAround( hits, followed ) );
			*place = m_place;
			return TRUE;
		}
	}
	else if( m_placeValid && m_placeKind == PLACE_SIGHT && hottestHeat <= 0.0f && held < DIRECTOR_SIGHT_FRAMES )
	{
		if( ObserverCamera_heatAround( sights, m_place, &followed ) > 0.0f )
			m_place = followed;
		*place = m_place;
		return TRUE;
	}

	if( hottestHeat > 0.0f )
	{
		DEBUG_LOG(( "OBSCAM frame %u director to fight (%.0f,%.0f) heat %.1f\n", frame, hottest.x, hottest.y, hottestHeat ));
		m_place = hottest;
		m_placeKind = PLACE_FIGHT;
		m_placePeak = hottestHeat;
		m_placeHeight = ObserverCamera_fightHeight( ObserverCamera_spreadAround( hits, hottest ) );
	}
	else
	{
		Coord2D sight;
		if( !ObserverCamera_nextSight( sights, m_seen, &sight ) )
		{
			m_seen.clear();
			if( !ObserverCamera_nextSight( sights, m_seen, &sight ) )
			{
				m_placeValid = FALSE;
				return FALSE;
			}
		}
		DEBUG_LOG(( "OBSCAM frame %u director to sight (%.0f,%.0f)\n", frame, sight.x, sight.y ));
		m_place = sight;
		m_placeKind = PLACE_SIGHT;
		m_placeHeight = 0.0f;
		m_seen.push_back( sight );
		if( m_seen.size() > DIRECTOR_SEEN_COUNT )
			m_seen.erase( m_seen.begin() );
	}
	m_placeSince = frame;
	m_placeValid = TRUE;
	*place = m_place;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::isShowingPlayerView( void ) const
{
	return m_mode == OBSERVER_CAMERA_PLAYER && m_followed != NO_PLAYER && m_playerViews[ m_followed ].isValid();
}

//-------------------------------------------------------------------------------------------------
/** The director with a player picked keeps to his fights; a player's camera with nobody picked has
	* nothing to show and leaves the camera where it is. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera::chooseTarget( const ViewLocation &current, ViewLocation *target )
{
	if( m_mode == OBSERVER_CAMERA_PLAYER && m_followed == NO_PLAYER )
		return FALSE;

	if( isShowingPlayerView() )
	{
		m_split = FALSE;
		*target = m_playerViews[ m_followed ];
		return TRUE;
	}

	const Player *narrowTo = m_followed == NO_PLAYER ? NULL : ThePlayerList->getNthPlayer( m_followed );
	const Coord3D &at = current.getPosition();
	Coord2D place;
	if( !directorPlace( narrowTo, &place ) )
		return FALSE;
	// the main camera is always the whole screen; a second action goes into the corner window
	if( TheGlobalData->m_directorRecord && m_directing && m_placeScanned == TheGameLogic->getFrame() && m_followed == NO_PLAYER )
		updateSecondFight();
	place = keepInMap( place, current );
	target->init( place.x, place.y, at.z, current.getAngle(), current.getPitch(), current.getZoom() );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::update( UnsignedInt nowMilliseconds )
{
	const UnsignedInt elapsed = m_lastUpdate == 0 ? 0 : min( nowMilliseconds - m_lastUpdate, LONGEST_STEP_MILLISECONDS );
	m_lastUpdate = nowMilliseconds;

	updateShroudViewer();

	if( m_mode == OBSERVER_CAMERA_FREE )
	{
		m_driving = FALSE;
		m_split = FALSE;
		holdHeight( FALSE );
		releaseHeight();
		return;
	}

	// -directorreplays: the camera is the replays' alone, and none of the director's work runs
	if( TheGlobalData->m_directorRecord && isReplaying() )
	{
		if( !TheGameLogic->isInShellGame() )
			updateReplay();
		return;
	}

	ViewLocation current;
	TheTacticalView->getLocation( &current );
	// the followed player stays followed, for his fog, with the camera back in the watcher's hands
	if( m_driving && takenByHand( current ) )
	{
		DEBUG_LOG(( "OBSCAM frame %u the watcher took the camera\n", TheGameLogic->getFrame() ));
		m_mode = OBSERVER_CAMERA_FREE;
		m_driving = FALSE;
		m_split = FALSE;
		holdHeight( FALSE );
		releaseHeight();
		return;
	}

	// -directorrecord's corner window runs on the logic clock, on what the last scan decided.  What it
	// decides it decides once a logic frame, -recordfps updating each frame once a picture; the glide
	// and the window's slide go on at every picture's own time
	m_directing = !TheGlobalData->m_directorRecord || TheGameLogic->getFrame() != m_directedFrame;
	if( TheGlobalData->m_directorRecord && m_directing )
	{
		const UnsignedInt frame = TheGameLogic->getFrame();
		m_directedFrame = frame;
		if( !m_timelineLoaded )
			loadTimeline();
		checkTimeline( frame );
		updateBroadcastMoments( frame );
		updateWindow( frame );
		// the filming pass's events file, written from here and nowhere else
		if( TheGlobalData->m_directorScoutFile.isEmpty() && !TheGameLogic->isInShellGame() )
		{
			if( !m_filmEventsTried )
				openFilmEvents();
			scanFilmEvents( frame );
			flushFilmEvents();
			// and its instant replays, the missed big moments
			updateReplaysFile( frame );
		}
	}
	if( TheGlobalData->m_directorRecord )
		easeWindow( GameEngine_pictureFrame() );

	ViewLocation target;
	if( !chooseTarget( current, &target ) )
	{
		m_driving = FALSE;
		m_split = FALSE;
		holdHeight( FALSE );
		releaseHeight();
		return;
	}

	// a player's screen brings its own zoom; the director's height is the watcher's own plus what
	// the fight's width asks for, eased there on a spring of its own.  Handed straight to the view,
	// whose settle closes two thirds of a gap in ten frames, every change of place's height made a
	// camera gliding in for seconds step back up in a third of one.  A cut, the place too far for a
	// glide, takes the height at once with it, so it is one clean cut and not a cut and a step
	const Coord3D &from = current.getPosition();
	const Real gapX = target.getPosition().x - from.x - ( m_driving ? m_mainOffset.x : 0.0f );
	const Real gapY = target.getPosition().y - from.y - ( m_driving ? m_mainOffset.y : 0.0f );
	const Bool cut = gapX * gapX + gapY * gapY > CUT_DISTANCE * CUT_DISTANCE;
	if( !m_heightDriven )
	{
		m_heightExtra = 0.0f;
		m_heightExtraVelocity = 0.0f;
	}
	// a step back or in that no glide made: the zoom moved more than ZOOM_JUMP_SHARE between two
	// updates without a cut
	if( TheGlobalData->m_directorRecord && m_lastZoom > 0.0f && !m_lastCut
		&& fabsf( current.getZoom() - m_lastZoom ) > m_lastZoom * ZOOM_JUMP_SHARE )
		DEBUG_LOG(( "OBSCAM frame %u zoom jump %.3f -> %.3f\n", TheGameLogic->getFrame(), m_lastZoom, current.getZoom() ));
	m_lastZoom = current.getZoom();
	m_lastCut = cut;
	m_heightExtra = ObserverCamera_easeHeight( m_heightExtra, DIRECTOR_WIDE_EXTRA + m_placeHeight, &m_heightExtraVelocity, elapsed / MILLISECONDS_PER_SECOND, cut );
	if( isShowingPlayerView() )
		releaseHeight();
	else
		driveHeight( m_heightExtra );
	holdHeight( isShowingPlayerView() );
	if( !m_driving )
	{
		m_velocity.x = m_velocity.y = m_velocity.z = m_velocity.angle = m_velocity.pitch = m_velocity.zoom = 0.0f;
		m_mainOffset.x = m_mainOffset.y = 0.0f;
	}

	const Coord3D &camera = current.getPosition();
	ViewLocation subject;
	subject.init( camera.x + m_mainOffset.x, camera.y + m_mainOffset.y, camera.z, current.getAngle(), current.getPitch(), current.getZoom() );
	const Bool player = m_mode == OBSERVER_CAMERA_PLAYER;
	ViewLocation step = ObserverCamera_approach( subject, target, elapsed / MILLISECONDS_PER_SECOND,
		player ? PLAYER_PAN_SECONDS : DIRECTOR_PAN_SECONDS, player ? PLAYER_TOP_SPEED : DIRECTOR_TOP_SPEED, &m_velocity );
	Coord2D sitBack;
	sitBack.x = step.getPosition().x;
	sitBack.y = step.getPosition().y;
	if( TheGlobalData->m_directorRecord && !player )
	{
		// the view puts the subject on the screen's middle pixel, as the window's camera does.  Looking
		// straight at the subject drew it off the middle by the view's lift over the ground
		Coord2D middle;
		middle.x = TheDisplay->getWidth() * 0.5f;
		middle.y = TheDisplay->getHeight() * 0.5f;
		m_aimFrom = step;
		sitBack = placeOnPixel( sitBack, middle, step.getZoom() );
	}
	// the window's camera is placed by aiming the view, so before the view is put where it draws
	if( TheGlobalData->m_directorRecord )
		stepWindowCamera( step, elapsed / MILLISECONDS_PER_SECOND );
	m_mainOffset.x = step.getPosition().x - sitBack.x;
	m_mainOffset.y = step.getPosition().y - sitBack.y;
	ViewLocation placed;
	placed.init( sitBack.x, sitBack.y, step.getPosition().z, step.getAngle(), step.getPitch(), step.getZoom() );
	TheTacticalView->setLocation( &placed );
	if( cut && !isShowingPlayerView() )
		TheTacticalView->setZoomToHeight( m_handHeight + m_heightExtra );
	// the view keeps its look point inside its constraint when it draws; held there now, a cut to a
	// place past the constraint is not mistaken next frame for the watcher moving the camera
	TheTacticalView->applyCameraConstraint();
	TheTacticalView->getPosition( &m_drivenTo );
	m_driving = TRUE;
}
