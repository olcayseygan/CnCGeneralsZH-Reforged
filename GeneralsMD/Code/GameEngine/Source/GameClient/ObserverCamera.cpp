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

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
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
#include <math.h>

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
/// how long a superweapon is shown at its silo before the camera goes to where it will land
static const UnsignedInt EVENT_LAUNCH_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
/// the camera stays this long after the last hit at an event's target, for the cloud and the fires
static const UnsignedInt EVENT_AFTERMATH_FRAMES = 5 * LOGICFRAMES_PER_SECOND;
/// a hit on a thing that cost this much counts twice what a free one does
static const Real DIRECTOR_COST_PER_WEIGHT = 500.0f;
/// a kill counts this many times a hit, and anything on a superweapon this many times again
static const Real DIRECTOR_KILL_FACTOR = 2.0f;
static const Real DIRECTOR_SUPERWEAPON_FACTOR = 3.0f;
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
/// the longest a split stays up, from when it opened; a planned one was held 5300 frames on an
/// eight-player map while pane 0 went from fight to fight beside it
static const UnsignedInt SPLIT_MOST_FRAMES = 18 * LOGICFRAMES_PER_SECOND;
/// how long after the panes are gone the hand-over is logged a line a frame
static const UnsignedInt HANDOVER_LOG_FRAMES = 45;
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
/// -directorrecord: a second fight closer than this to the first shares its ground, and the split
/// would show one fight twice; the least, raised to the ground a pane's whole picture spans
static const Real SPLIT_APART = 3.0f * DIRECTOR_GATHER_RADIUS;
/// a split opens only for two fights further apart than either pane's picture is wide, so no ground
/// shows in both, and closes once they come within this share of that.  A pane zooms out until its
/// fight fills its circle, one and a half to two and a half times the director's zoom: at 660 apart,
/// and at one and a half times the director's own picture, about 700, the two panes of a 1v1 showed
/// one bridge from neighbouring views
static const Real SPLIT_SAME_GROUND_SHARE = 2.0f / 3.0f;
/// the picture splits for a second fight at least this hot, and at least this share of the first
static const Real SPLIT_ENTER_HEAT = 2.0f;
static const Real SPLIT_ENTER_SHARE = 0.5f;
/// a split is held this long once its panes are all in, and after that while the second fight keeps
/// this share
static const UnsignedInt SPLIT_HOLD_FRAMES = 6 * LOGICFRAMES_PER_SECOND;
static const Real SPLIT_STAY_SHARE = 0.25f;
/// once the picture is whole again it stays whole this long, so it does not flicker between the two
static const UnsignedInt SPLIT_REST_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
/// the corner radar takes this long to slide out to the left before the panes come, and back after
static const UnsignedInt PANE_RADAR_FRAMES = 12;
/// the panes take this long to slide in along the rays, and out again
static const UnsignedInt PANE_SLIDE_FRAMES = 15;
/// then the gold draws out along the settled lines from where they meet, and the radar's frame round
/// the map, for this long; it goes back in as long before the panes leave.  Drawn while the panes
/// slid, the lines were whole before the meeting point was on the screen and nothing was seen to draw
static const UnsignedInt PANE_DRAW_FRAMES = 21;
static const UnsignedInt PANE_UNDRAW_FRAMES = 15;
/// so a split, once on, stays on this long whatever its fights do, unless its panes come to show
/// the same ground: a split that went off before its panes were in flashed a pane in and out
static const UnsignedInt SPLIT_LEAST_FRAMES = PANE_RADAR_FRAMES + PANE_SLIDE_FRAMES + PANE_DRAW_FRAMES + SPLIT_HOLD_FRAMES;
/// a split the timeline plans opens this long before its fight: at four seconds pane 1 sat on an
/// empty bridge
static const UnsignedInt SPLIT_LEAD_FRAMES = 3 * LOGICFRAMES_PER_SECOND;
/// the match opens on every player's base, one pane each, for this long
static const UnsignedInt PANE_INTRO_FRAMES = 7 * LOGICFRAMES_PER_SECOND;
/// the gold of a line between panes is a pixel for every this many rows of the picture
static const Real PANE_LINE_ROWS_A_PIXEL = 120.0f;
/// the soft band of the brand's blue under a line is this many times the gold's width
static const Int PANE_BAND_LINES = 5;
/// once the panes are in, a line grows out from the meeting point over this share of the draw, its
/// band fades in from this share of it, a beat behind, and the radar's frame traces its gold round
/// the map over this share; going back runs it all backwards
static const Real PANE_LINE_DRAWN_BY = 0.75f;
static const Real PANE_BAND_FROM = 0.35f;
static const Real PANE_FRAME_TRACED_BY = 0.6f;
/// while the panes are held a light runs out along every gold line once in this many logic frames,
/// taking this many to reach the end
static const UnsignedInt PANE_SHIMMER_PERIOD = 5 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt PANE_SHIMMER_FRAMES = 45;
/// a pane's circle is looked for on a grid this fine, a centre every 10 pixels at 1280x720
static const Int PANE_CIRCLE_COLUMNS = 128;
static const Int PANE_CIRCLE_ROWS = 72;
/// a subject fitted into its pane's circle leaves this much of the radius round it
static const Real PANE_FIT_MARGIN = 0.15f;
/// a pane never comes closer than the director's own zoom, and never goes further out than this times it
static const Real PANE_FIT_FARTHEST = 3.0f;
/// a fight is never fitted as smaller than this across, so two tanks are not filled into a pane
static const Real PANE_FIT_LEAST_EXTENT = 150.0f;
/// the opening fits what a player has within this of the middle of it, his base and the army beside it
static const Real PANE_INTRO_REACH = 500.0f;
/// how much of the way to a new fit a pane's zoom goes each logic frame, about a second and a half
static const Real PANE_FIT_FOLLOW = 0.06f;
/// how far over a command centre's own height its player's plate hangs: the flags on a GLA palace
/// stand past it, and zoomed in they ran into the plate
static const Real PANE_MARK_LIFT = 15.0f;
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
static const UnsignedInt FLAG_MOVE_FRAMES = 15;
static const UnsignedInt BANNER_MOVE_FRAMES = 24;
/// a power's flag hangs under its player's card this long when nothing waits behind it, a defeated
/// player's banner holds this long
static const UnsignedInt FLAG_HOLD_FRAMES = 4 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt DEFEAT_BANNER_HOLD_FRAMES = 3 * LOGICFRAMES_PER_SECOND;
/// a defeated player's card flashes red over the first of these, a line is drawn through it from the
/// second over the third, it is seen struck until the fourth after the defeat and then collapses over
/// the last while the others slide together
static const UnsignedInt CARD_FLASH_FRAMES = 12;
static const UnsignedInt CARD_STRIKE_FROM = 6;
static const UnsignedInt CARD_STRIKE_FRAMES = 12;
static const UnsignedInt CARD_STRUCK_FRAMES = 36;
static const UnsignedInt CARD_COLLAPSE_FRAMES = 18;
/// the winner's banner starts to come in this long after the match is decided, as the last defeated
/// player's banner over it has opened: the film runs 105 frames past the decision, and at 30 the winner's
/// was all in for only 51 of them
static const UnsignedInt WINNER_DELAY_FRAMES = 24;

//-------------------------------------------------------------------------------------------------
static Bool sameFight( const Coord2D &a, const Coord2D &b )
{
	const Real dx = a.x - b.x;
	const Real dy = a.y - b.y;
	return dx * dx + dy * dy <= DIRECTOR_GATHER_RADIUS * DIRECTOR_GATHER_RADIUS;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_hitWeight( Int cost, Bool killed, Bool superweapon )
{
	Real weight = 1.0f + cost / DIRECTOR_COST_PER_WEIGHT;
	if( killed )
		weight *= DIRECTOR_KILL_FACTOR;
	if( superweapon )
		weight *= DIRECTOR_SUPERWEAPON_FACTOR;
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
Coord2D ObserverCamera_eventPlace( const DirectorEvent &event, UnsignedInt frame )
{
	if( event.superweapon && frame < event.since + EVENT_LAUNCH_FRAMES && !sameFight( event.source, event.target ) )
		return event.source;
	return event.target;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_sameUse( const DirectorEvent &event, const Player *owner, const SpecialPowerTemplate *power, const Coord2D &target,
	UnsignedInt frame )
{
	return power != NULL && event.power == power && event.owner == owner && frame < event.until && sameFight( event.target, target );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_stayOnEvent( const DirectorEvent *current, const DirectorEvent *best, UnsignedInt held )
{
	if( current == NULL )
		return FALSE;
	return best == NULL || best == current || !ObserverCamera_shouldMove( current->weight, best->weight, held, current->weight );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_eventCutsIn( const DirectorEvent &event, UnsignedInt frame, UnsignedInt held )
{
	return held >= DIRECTOR_SETTLE_FRAMES || ( event.superweapon && frame < event.since + EVENT_LAUNCH_FRAMES );
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
Int ObserverCamera_groundOf( const Coord2D &place, const Coord2D *homes, Int count )
{
	Int nearest = 0;
	Real nearestSquared = 0.0f;
	for( Int home = 0; home < count; home++ )
	{
		const Real dx = homes[ home ].x - place.x;
		const Real dy = homes[ home ].y - place.y;
		const Real squared = dx * dx + dy * dy;
		if( home == 0 || squared < nearestSquared )
		{
			nearest = home;
			nearestSquared = squared;
		}
	}
	return nearest;
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_secondPlace( const std::vector< DirectorHeat > &hits, const Coord2D &first, Real needed, const Coord2D *homes,
	Int homeCount, Coord2D *place, Real *heat )
{
	const Int firstGround = ObserverCamera_groundOf( first, homes, homeCount );
	std::vector< DirectorHeat > apart;
	for( size_t index = 0; index < hits.size(); index++ )
	{
		const Real dx = hits[ index ].position.x - first.x;
		const Real dy = hits[ index ].position.y - first.y;
		if( homeCount >= 2 && ObserverCamera_groundOf( hits[ index ].position, homes, homeCount ) == firstGround )
			continue;
		if( dx * dx + dy * dy > needed * needed )
			apart.push_back( hits[ index ] );
	}
	return ObserverCamera_hottestPlace( apart, place, heat );
}

//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_holdSplit( Bool split, Real firstHeat, Real secondHeat, Real apart, Real needed, UnsignedInt framesSince )
{
	// two halves showing one fight's ground is no split at all, however new
	if( apart <= needed * SPLIT_SAME_GROUND_SHARE )
		return FALSE;
	if( split )
		return framesSince < SPLIT_LEAST_FRAMES || ( secondHeat > 0.0f && secondHeat >= firstHeat * SPLIT_STAY_SHARE );
	return apart > needed && framesSince >= SPLIT_REST_FRAMES && firstHeat > 0.0f && secondHeat >= SPLIT_ENTER_HEAT
		&& secondHeat >= firstHeat * SPLIT_ENTER_SHARE;
}

//-------------------------------------------------------------------------------------------------
/** How much of a picture half w wide and half h high round its middle a ray sweeping counterclockwise
	* from 0 has covered by angle degrees.  In each quarter the swept part is a triangle against the near
	* side until the ray reaches the corner, and the whole quarter less a triangle against the far side
	* after; the quarters that start upright are the same with the sides swapped. */
//-------------------------------------------------------------------------------------------------
static Real sweptArea( Real angle, Real halfWidth, Real halfHeight )
{
	const Int quarter = min( (Int)( angle / 90.0f ), 3 );
	const Real within = ( angle - quarter * 90.0f ) * PI / 180.0f;
	const Real along = quarter % 2 == 0 ? halfWidth : halfHeight;
	const Real across = quarter % 2 == 0 ? halfHeight : halfWidth;
	Real part;
	if( within <= atan2f( across, along ) )
		part = 0.5f * along * along * tanf( within );
	else
		part = along * across - 0.5f * across * across * tanf( PI * 0.5f - within );
	return quarter * halfWidth * halfHeight + part;
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_angleForShare( Real share, Int width, Int height )
{
	const Real halfWidth = width * 0.5f;
	const Real halfHeight = height * 0.5f;
	const Real wanted = share * 4.0f * halfWidth * halfHeight;
	Real low = 0.0f;
	Real high = 360.0f;
	for( Int step = 0; step < 40; step++ )
	{
		const Real tried = ( low + high ) * 0.5f;
		if( sweptArea( tried, halfWidth, halfHeight ) < wanted )
			low = tried;
		else
			high = tried;
	}
	return ( low + high ) * 0.5f;
}

//-------------------------------------------------------------------------------------------------
/** Each layout is where its rays would stand on a round picture, where angle and area are one; on the
	* real picture each ray goes where it covers the same share of it.  Two panes are the diagonal
	* corner to corner, its upper left half the radar's; four are both diagonals.  Three is a Y, the top
	* wedge and two below; the odd counts keep a ray straight down, the even ones a pair of opposite
	* rays.  At a fixed 45 degrees a 16:9 Y gave its top pane 14% of the picture and each of the others
	* 43%; every pane now has its share, a third of it for three. */
//-------------------------------------------------------------------------------------------------
Int ObserverCamera_paneLayout( Int count, Real *rays, Int width, Int height )
{
	static const Real two[] = { 45, 225 };
	static const Real three[] = { 30, 150, 270 };
	static const Real four[] = { 45, 135, 225, 315 };
	static const Real five[] = { 54, 126, 198, 270, 342 };
	static const Real six[] = { 30, 90, 150, 210, 270, 330 };
	static const Real seven[] = { 12.857f, 64.286f, 115.714f, 167.143f, 218.571f, 270, 321.429f };
	static const Real eight[] = { 0, 45, 90, 135, 180, 225, 270, 315 };
	static const Real *layouts[] = { two, three, four, five, six, seven, eight };
	if( count < 2 )
		return 0;
	count = min( count, (Int)OBSERVER_MOST_PANES );
	for( Int ray = 0; ray < count; ray++ )
		rays[ ray ] = ObserverCamera_angleForShare( layouts[ count - 2 ][ ray ] / 360.0f, width, height );
	return count;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_paneOf( Real x, Real y, Real originX, Real originY, const Real *rays, Int count )
{
	Real angle = atan2f( originY - y, x - originX ) * 180.0f / PI;
	if( angle < 0.0f )
		angle += 360.0f;
	// below the first ray is the far side of the last pane, which wraps round through 0
	Int pane = count - 1;
	for( Int ray = 0; ray < count; ray++ )
	{
		if( angle >= rays[ ray ] )
			pane = ray;
	}
	return pane;
}

//-------------------------------------------------------------------------------------------------
/* paneOf's float angle strays less than two hundredths of a degree from the true one, even on an 8K
	 picture, for a pixel at least PANE_RUN_CLEARANCE pixels from the meeting point.  A stretch of a row whose true
	 angle stays more than PANE_RUN_MARGIN degrees off every ray and off the turn's ends all the way along
	 can therefore take one pixel's pane for all of it.  A stretch that cannot is halved, and one no longer
	 than PANE_RUN_SHORTEST is asked pixel by pixel. */
static const double PANE_RUN_MARGIN = 0.05;
static const double PANE_RUN_CLEARANCE = 2.0;
static const Int PANE_RUN_SHORTEST = 4;

static double paneRunAngle( double x, double towardsTop, double originX )
{
	const double angle = atan2( towardsTop, x - originX ) * 180.0 / 3.14159265358979323846;
	return angle < 0.0 ? angle + 360.0 : angle;
}

static Bool paneRunIsOnePane( Int first, Int last, double towardsTop, double originX, const Real *rays, Int count )
{
	const double left = first + 0.5;
	const double right = last + 0.5;
	if( fabs( towardsTop ) < PANE_RUN_CLEARANCE && right > originX - PANE_RUN_CLEARANCE
			&& left < originX + PANE_RUN_CLEARANCE )
		return FALSE;
	// along a row the angle only ever turns one way, so its ends bound it
	const double leftAngle = paneRunAngle( left, towardsTop, originX );
	const double rightAngle = paneRunAngle( right, towardsTop, originX );
	const double lowest = min( leftAngle, rightAngle ) - PANE_RUN_MARGIN;
	const double highest = max( leftAngle, rightAngle ) + PANE_RUN_MARGIN;
	if( lowest <= 0.0 || highest >= 360.0 )
		return FALSE;
	for( Int ray = 0; ray < count; ray++ )
	{
		if( rays[ ray ] >= lowest && rays[ ray ] <= highest )
			return FALSE;
	}
	return TRUE;
}

static void addPaneRun( std::vector< ObserverPaneRun > &runs, Int y, Int x0, Int x1, Int pane )
{
	if( !runs.empty() && runs.back().y == y && runs.back().x1 == x0 && runs.back().pane == pane )
	{
		runs.back().x1 = x1;
		return;
	}
	const ObserverPaneRun run = { y, x0, x1, pane };
	runs.push_back( run );
}

static void addPaneRuns( std::vector< ObserverPaneRun > &runs, Int y, Int first, Int last, Real originX, Real originY,
	const Real *rays, Int count )
{
	const Real centreY = (Real)y + 0.5f;
	const double towardsTop = (double)originY - centreY;
	if( paneRunIsOnePane( first, last, towardsTop, originX, rays, count ) )
	{
		addPaneRun( runs, y, first, last + 1, ObserverCamera_paneOf( (Real)first + 0.5f, centreY, originX, originY, rays, count ) );
		return;
	}
	if( last - first < PANE_RUN_SHORTEST )
	{
		for( Int x = first; x <= last; x++ )
			addPaneRun( runs, y, x, x + 1, ObserverCamera_paneOf( (Real)x + 0.5f, centreY, originX, originY, rays, count ) );
		return;
	}
	const Int middle = ( first + last ) / 2;
	addPaneRuns( runs, y, first, middle, originX, originY, rays, count );
	addPaneRuns( runs, y, middle + 1, last, originX, originY, rays, count );
}

void ObserverCamera_paneRuns( Int width, Int height, Real originX, Real originY, const Real *rays, Int count,
	std::vector< ObserverPaneRun > &runs )
{
	runs.clear();
	for( Int y = 0; y < height; y++ )
		addPaneRuns( runs, y, 0, width - 1, originX, originY, rays, count );
}

//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera_paneExitDirection( const Real *rays )
{
	const Real away = ( ( rays[ 0 ] + rays[ 1 ] ) * 0.5f + 180.0f ) * PI / 180.0f;
	Coord2D direction;
	direction.x = cosf( away );
	direction.y = -sinf( away );
	return direction;
}

//-------------------------------------------------------------------------------------------------
/** Pane 0 is a wedge of 180 degrees or less, so it holds the whole picture once it holds the four
	* corners. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera_paneExit( const Real *rays, Int count, Int width, Int height )
{
	const Coord2D direction = ObserverCamera_paneExitDirection( rays );
	const Real cornerX[ 4 ] = { 0.5f, width - 0.5f, 0.5f, width - 0.5f };
	const Real cornerY[ 4 ] = { 0.5f, 0.5f, height - 0.5f, height - 0.5f };
	Real inside = 2.0f * ( width + height );
	Real outside = 0.0f;
	for( Int step = 0; step < 32; step++ )
	{
		const Real tried = ( inside + outside ) * 0.5f;
		const Real originX = width * 0.5f + direction.x * tried;
		const Real originY = height * 0.5f + direction.y * tried;
		Bool whole = TRUE;
		for( Int corner = 0; corner < 4 && whole; corner++ )
			whole = ObserverCamera_paneOf( cornerX[ corner ], cornerY[ corner ], originX, originY, rays, count ) == 0;
		if( whole )
			inside = tried;
		else
			outside = tried;
	}
	return inside;
}

//-------------------------------------------------------------------------------------------------
/** How far a point is from a ray out of the middle: across to the ray's line, or back to where the
	* ray starts when the point is behind it. */
//-------------------------------------------------------------------------------------------------
static Real distanceToRay( Real x, Real y, Real middleX, Real middleY, Real degrees )
{
	const Real angle = degrees * PI / 180.0f;
	const Real alongX = cosf( angle );
	const Real alongY = -sinf( angle );
	const Real fromX = x - middleX;
	const Real fromY = y - middleY;
	if( fromX * alongX + fromY * alongY <= 0.0f )
		return sqrtf( fromX * fromX + fromY * fromY );
	return fabs( fromX * alongY - fromY * alongX );
}

//-------------------------------------------------------------------------------------------------
/** Every pane is a wedge of 180 degrees or less, so the nearest of its edges is one of its two rays,
	* the screen's four sides or the radar's frame; the grid point furthest from all of them is the
	* circle's centre. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera_paneCircles( const Real *rays, Int count, Int width, Int height, const Coord2D &radarHalf, Real top,
	Coord2D *centres, Real *radii )
{
	const Real middleX = width * 0.5f;
	const Real middleY = height * 0.5f;
	for( Int pane = 0; pane < count; pane++ )
	{
		radii[ pane ] = 0.0f;
		centres[ pane ].x = middleX;
		centres[ pane ].y = middleY;
	}
	for( Int row = 0; row < PANE_CIRCLE_ROWS; row++ )
	{
		for( Int column = 0; column < PANE_CIRCLE_COLUMNS; column++ )
		{
			const Real x = ( column + 0.5f ) * width / PANE_CIRCLE_COLUMNS;
			const Real y = ( row + 0.5f ) * height / PANE_CIRCLE_ROWS;
			const Int pane = ObserverCamera_paneOf( x, y, middleX, middleY, rays, count );
			Real clear = min( min( x, width - x ), min( y - top, height - y ) );
			clear = min( clear, distanceToRay( x, y, middleX, middleY, rays[ pane ] ) );
			clear = min( clear, distanceToRay( x, y, middleX, middleY, rays[ ( pane + 1 ) % count ] ) );
			const Real outX = max( fabs( x - middleX ) - radarHalf.x, 0.0f );
			const Real outY = max( fabs( y - middleY ) - radarHalf.y, 0.0f );
			clear = min( clear, sqrtf( outX * outX + outY * outY ) );
			if( clear > radii[ pane ] )
			{
				radii[ pane ] = clear;
				centres[ pane ].x = x;
				centres[ pane ].y = y;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The label's top corners are the first of it to leave the circle going up: they meet it where its
	* chord is the label's width, and the label hangs from there. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera_paneLabelTop( const Coord2D &centre, Real radius, Real width, Real height )
{
	const Real halfWidth = width * 0.5f;
	if( halfWidth >= radius )
		return centre.y - height * 0.5f;
	const Real above = sqrtf( radius * radius - halfWidth * halfWidth );
	return centre.y - max( above, height * 0.5f );
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
std::vector< Int > ObserverCamera_teamOrder( const std::vector< Int > &teams )
{
	std::vector< Int > order;
	std::vector< Bool > taken( teams.size(), FALSE );
	for( size_t first = 0; first < teams.size(); first++ )
	{
		if( taken[ first ] )
			continue;
		for( size_t index = first; index < teams.size(); index++ )
		{
			if( teams[ index ] != teams[ first ] )
				continue;
			order.push_back( (Int)index );
			taken[ index ] = TRUE;
		}
	}
	return order;
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
/// zerohour.gg's --gold, the pane lines' colour, and how far from it a player's colour has to be to
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
Real ObserverCamera_extentAround( const std::vector< DirectorHeat > &things, const Coord2D &around, Real reach, Real least )
{
	Real farthest = least;
	for( size_t index = 0; index < things.size(); index++ )
	{
		const Real dx = things[ index ].position.x - around.x;
		const Real dy = things[ index ].position.y - around.y;
		const Real distance = sqrtf( dx * dx + dy * dy );
		if( distance <= reach )
			farthest = max( farthest, distance );
	}
	return farthest;
}

//-------------------------------------------------------------------------------------------------
/** The eye stands back from what it looks at in a straight line with the zoom, and the ground a
	* circle on the screen covers with it, so the two measures give the line and the reach wanted is
	* read off it. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera_zoomForReach( Real zoomA, Real reachA, Real zoomB, Real reachB, Real reach, Real nearest, Real farthest )
{
	const Real zoom = zoomA + ( reach - reachA ) * ( zoomB - zoomA ) / ( reachB - reachA );
	return min( max( zoom, nearest ), farthest );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_paneGround( Real screenGround, Real circleGround, Real reach )
{
	return screenGround * min( max( reach / circleGround, 1.0f ), PANE_FIT_FARTHEST );
}

//-------------------------------------------------------------------------------------------------
/** The wedge from origin between the pane's two rays, cut by the screen: where the rays meet if that
	* is on the screen, the screen's corners inside the wedge, and where each ray leaves the screen.
	* A wedge of 180 degrees or less and the screen are both convex, so these are all its corners. */
//-------------------------------------------------------------------------------------------------
Int ObserverCamera_panePolygon( const Real *rays, Int count, Int pane, const Coord2D &origin, Int width, Int height,
	Coord2D *vertices )
{
	const Coord2D screen[ 4 ] = { { 0.0f, 0.0f }, { (Real)width, 0.0f }, { (Real)width, (Real)height }, { 0.0f, (Real)height } };
	Int found = 0;
	if( count < 2 )
	{
		for( Int corner = 0; corner < 4; corner++ )
			vertices[ found++ ] = screen[ corner ];
		return found;
	}

	const Bool originOnScreen = origin.x >= 0.0f && origin.x <= width && origin.y >= 0.0f && origin.y <= height;
	if( originOnScreen )
		vertices[ found++ ] = origin;
	for( Int corner = 0; corner < 4; corner++ )
	{
		if( ObserverCamera_paneOf( screen[ corner ].x, screen[ corner ].y, origin.x, origin.y, rays, count ) == pane )
			vertices[ found++ ] = screen[ corner ];
	}
	const Int bounds[ 2 ] = { pane, ( pane + 1 ) % count };
	for( Int side = 0; side < 2; side++ )
	{
		const Real angle = rays[ bounds[ side ] ] * PI / 180.0f;
		const Real alongX = cosf( angle );
		const Real alongY = -sinf( angle );
		// the stretch of the ray inside the screen, as distances along it
		Real enter = 0.0f;
		Real leave = 1.0e9f;
		const Real start[ 2 ] = { origin.x, origin.y };
		const Real along[ 2 ] = { alongX, alongY };
		const Real high[ 2 ] = { (Real)width, (Real)height };
		for( Int axis = 0; axis < 2; axis++ )
		{
			if( fabs( along[ axis ] ) < 1.0e-6f )
			{
				if( start[ axis ] < 0.0f || start[ axis ] > high[ axis ] )
					enter = leave + 1.0f;
				continue;
			}
			const Real atLow = ( 0.0f - start[ axis ] ) / along[ axis ];
			const Real atHigh = ( high[ axis ] - start[ axis ] ) / along[ axis ];
			enter = max( enter, min( atLow, atHigh ) );
			leave = min( leave, max( atLow, atHigh ) );
		}
		if( enter > leave )
			continue;
		if( !originOnScreen )
		{
			vertices[ found ].x = origin.x + alongX * enter;
			vertices[ found ].y = origin.y + alongY * enter;
			found++;
		}
		vertices[ found ].x = origin.x + alongX * leave;
		vertices[ found ].y = origin.y + alongY * leave;
		found++;
	}
	return found;
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
		if( !power.power || power.superweapon || power.peak > 0.0f || frame > power.start + EVENT_FRAMES )
			continue;
		for( size_t hit = 0; hit < hits.size() && power.peak <= 0.0f; hit++ )
			if( within( hits[ hit ].position, power.target, DIRECTOR_GATHER_RADIUS ) )
				power.peak = EVENT_WEIGHT;
	}
}

//-------------------------------------------------------------------------------------------------
/** The timeline is written with no decimals, so the scouting pass's target is the one fired on frame
	* within a unit of at. */
//-------------------------------------------------------------------------------------------------
Bool ObserverCamera_powerLands( const std::vector< DirectorMoment > &timeline, UnsignedInt frame, const Coord2D &at )
{
	for( size_t index = 0; index < timeline.size(); index++ )
	{
		const DirectorMoment &power = timeline[ index ];
		if( power.power && power.start == frame && within( power.target, at, 1.0f ) )
			return ObserverCamera_worthFilming( power );
	}
	return FALSE;
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
		if( moment.start <= frame || moment.start > frame + DIRECTOR_PREROLL_FRAMES )
			continue;
		if( moment.power ? !moment.superweapon : !ObserverCamera_worthFilming( moment ) )
			continue;
		if( taken != NULL && within( moment.place, *taken, apart ) )
			continue;
		if( best >= 0 )
		{
			const DirectorMoment &held = timeline[ best ];
			const Bool better = moment.superweapon != held.superweapon ? moment.superweapon : moment.peak > held.peak;
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
	const Coord2D *besides )
{
	for( size_t index = 0; index < timeline.size(); index++ )
	{
		const DirectorMoment &fight = timeline[ index ];
		if( fight.power || !ObserverCamera_worthFilming( fight ) || frame + DIRECTOR_PREROLL_FRAMES < fight.start || fight.last < until )
			continue;
		if( scoutedNear( fight, place ) && ( besides == NULL || !scoutedNear( fight, *besides ) ) )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera_plannedSecond( const std::vector< DirectorMoment > &timeline, const Coord2D &first, UnsignedInt frame, Real apart,
	Bool keeping, const Coord2D *homes, Int homeCount )
{
	const Int firstGround = ObserverCamera_groundOf( first, homes, homeCount );
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
		if( homeCount >= 2 && ObserverCamera_groundOf( fight.place, homes, homeCount ) == firstGround )
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
Real ObserverCamera_easeFrames( UnsignedInt frame, UnsignedInt start, UnsignedInt length )
{
	if( frame <= start )
		return 0.0f;
	const Real t = min( (Real)( frame - start ) / length, 1.0f );
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
Real ObserverCamera_lineDrawn( Real progress )
{
	return ObserverCamera_easeBetween( progress, 0.0f, PANE_LINE_DRAWN_BY );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_bandShown( Real progress )
{
	return ObserverCamera_easeBetween( progress, PANE_BAND_FROM, 1.0f );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_frameTraced( Real progress )
{
	return ObserverCamera_easeBetween( progress, 0.0f, PANE_FRAME_TRACED_BY );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera_shimmerAt( UnsignedInt frame )
{
	const UnsignedInt into = frame % PANE_SHIMMER_PERIOD;
	if( into >= PANE_SHIMMER_FRAMES )
		return -1.0f;
	return ObserverCamera_easeBetween( (Real)into, 0.0f, (Real)PANE_SHIMMER_FRAMES );
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
Real ObserverCamera_showingShown( const DirectorShowing &showing, UnsignedInt frame, UnsignedInt moveFrames )
{
	if( showing.start == 0 )
		return 0.0f;
	const Real in = ObserverCamera_easeFrames( frame, showing.start, moveFrames );
	if( showing.leaving == 0 )
		return in;
	return in * ( 1.0f - ObserverCamera_easeFrames( frame, showing.leaving, moveFrames ) );
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera_cardExit( UnsignedInt frame, UnsignedInt defeated, UnsignedInt collapseFrom, Real *flash, Real *struck, Real *collapse )
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
	reset();
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::reset( void )
{
	m_mode = OBSERVER_CAMERA_FREE;
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
	m_panesHeldZoom = FALSE;
	m_lastZoom = 0.0f;
	m_lastCut = FALSE;
	m_handoverLogUntil = 0;
	m_paneLookFrame = 0;
	m_paneLookStepMost[ 0 ] = m_paneLookStepMost[ 1 ] = 0.0f;
	m_paneSurvivor = 0;
	m_spentMoment = -1;
	m_survivorHandover = FALSE;
	m_drivenTo.zero();
	m_lastUpdate = 0;
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
	m_broadcastTop = 0.0f;
	m_screenGround = 0.0f;
	m_circleGround = 0.0f;
	m_splitApart = SPLIT_APART;
	m_split = FALSE;
	m_splitChanged = 0;
	m_secondPlace.x = m_secondPlace.y = 0.0f;
	m_panePhase = PANES_NONE;
	m_panePhaseStart = 0;
	m_intro = FALSE;
	m_introDone = FALSE;
	m_introGlide = FALSE;
	m_panesLeftPlace.x = m_panesLeftPlace.y = 0.0f;
	m_homeCount = 0;
	m_paneCount = 0;
	m_paneProgress = 0.0f;
	m_lineProgress = 0.0f;
	m_paneExit = 0.0f;
	m_paneBaseZoom = 1.0f;
	m_paneFitValid = FALSE;
	m_paneFitFrame = 0;
	m_radarHalf.x = m_radarHalf.y = 0.0f;
	m_aimFrom = ViewLocation();
	m_paneOrigin.x = m_paneOrigin.y = 0.0f;
	m_cornerRadarSlide = 0.0f;
	m_radarFrame.lo.x = m_radarFrame.lo.y = m_radarFrame.hi.x = m_radarFrame.hi.y = 0;
	for( Int pane = 0; pane < OBSERVER_MOST_PANES; pane++ )
	{
		m_paneRays[ pane ] = 0.0f;
		m_panePlayers[ pane ] = NULL;
		m_homes[ pane ].x = m_homes[ pane ].y = 0.0f;
		m_paneSubject[ pane ].x = m_paneSubject[ pane ].y = 0.0f;
		m_paneMark[ pane ].x = m_paneMark[ pane ].y = m_paneMark[ pane ].z = 0.0f;
		m_paneCentres[ pane ].x = m_paneCentres[ pane ].y = 0.0f;
		m_paneRadii[ pane ] = 0.0f;
		m_paneExtent[ pane ] = PANE_FIT_LEAST_EXTENT;
		m_paneFit[ pane ] = 1.0f;
		m_paneShifted[ pane ] = 0.0f;
		m_paneError[ pane ] = 0.0f;
		m_paneShown[ pane ].x = m_paneShown[ pane ].y = 0.0f;
		m_paneWorst[ pane ].x = m_paneWorst[ pane ].y = 0.0f;
		m_paneOutside[ pane ] = 0.0f;
		m_paneGlide[ pane ] = ViewLocation();
		m_paneVelocity[ pane ].x = m_paneVelocity[ pane ].y = m_paneVelocity[ pane ].z = 0.0f;
		m_paneVelocity[ pane ].angle = m_paneVelocity[ pane ].pitch = m_paneVelocity[ pane ].zoom = 0.0f;
		m_paneView[ pane ] = ViewLocation();
	}
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
			m_events[ index ].until = max( m_events[ index ].until, frame + ( superweapon ? EVENT_SUPERWEAPON_FRAMES : EVENT_FRAMES ) );
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
	event.until = frame + ( superweapon ? EVENT_SUPERWEAPON_FRAMES : EVENT_FRAMES );
	event.weight = superweapon ? EVENT_SUPERWEAPON_WEIGHT : EVENT_WEIGHT;
	event.superweapon = superweapon;
	// a power that only looks, a spy satellite or a radar scan, hurts nobody and is not filmed: the
	// director cut to empty ground for every one an Air Force general fired.  The scouting pass knows
	// which ones land; without it a power waits for its first hit
	event.landed = superweapon || ObserverCamera_powerLands( m_timeline, frame, event.target );
	event.power = power;
	event.sourceThing = sourceThing;
	m_events.push_back( event );
	if( event.landed )
		noteFlag( event );

	if( !TheGlobalData->m_directorScoutFile.isEmpty() )
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
		event.until = max( event.until, frame + EVENT_AFTERMATH_FRAMES );
		return;
	}

	// a warhead nobody's special power sent, a script's or a map's, is still worth seeing land
	noteSpecialPower( owner, at, at, TRUE, NULL, NULL );
	m_events.back().until = frame + EVENT_AFTERMATH_FRAMES;
}

//-------------------------------------------------------------------------------------------------
/** A power that landed hangs its flag under its player's card: a superweapon, or a general's power,
	* one a promotion bought.  A unit's own ability, a sniper's shot or a hacker's, is neither. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::noteFlag( const DirectorEvent &event )
{
	if( !TheGlobalData->m_directorRecord || event.power == NULL )
		return;
	if( !event.superweapon && event.power->getRequiredScience() == SCIENCE_INVALID )
		return;
	const Int index = event.owner->getPlayerIndex();
	const DirectorShowing flag = { index, event.power, event.sourceThing, event.superweapon, 0, 0 };
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
		const DirectorShowing banner = { index, NULL, NULL, FALSE, 0, 0 };
		m_defeatBanners.push_back( banner );
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
		DEBUG_LOG(( "OBSCAM frame %u power flag under player %d's card: %s%s, %d waiting\n", frame, index, flag.power->getName().str(),
			flag.superweapon ? " (superweapon)" : "", (Int)m_flags[ index ].size() - 1 ));
	}
}

//-------------------------------------------------------------------------------------------------
const DirectorShowing *ObserverCamera::getPowerFlag( Int playerIndex, Real *drop ) const
{
	const std::vector< DirectorShowing > &flags = m_flags[ playerIndex ];
	if( flags.empty() || flags.front().start == 0 )
		return NULL;
	*drop = ObserverCamera_showingShown( flags.front(), TheGameLogic->getFrame(), FLAG_MOVE_FRAMES );
	return &flags.front();
}

//-------------------------------------------------------------------------------------------------
const DirectorShowing *ObserverCamera::getDefeatBanner( Real *shown ) const
{
	if( m_defeatBanners.empty() || m_defeatBanners.front().start == 0 )
		return NULL;
	*shown = ObserverCamera_showingShown( m_defeatBanners.front(), TheGameLogic->getFrame(), BANNER_MOVE_FRAMES );
	return &m_defeatBanners.front();
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera::getWinnerShown( void ) const
{
	return m_winnerFrame == 0 ? 0.0f : ObserverCamera_easeFrames( TheGameLogic->getFrame(), m_winnerFrame, BANNER_MOVE_FRAMES );
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::dropOldEvents( UnsignedInt frame )
{
	size_t kept = 0;
	for( size_t index = 0; index < m_events.size(); index++ )
	{
		if( frame < m_events[ index ].until )
			m_events[ kept++ ] = m_events[ index ];
	}
	m_events.resize( kept );
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
	* now, so the view's own picks and projections answer for that camera before anything draws.  Every
	* pane camera is placed by asking the view: two cuts that predicted its projection both drew the
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
/** How far from pixel the view, as it is aimed now, draws subject. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera::pixelError( const Coord2D &subject, const Coord2D &pixel )
{
	Coord3D world;
	world.x = subject.x;
	world.y = subject.y;
	world.z = TheTerrainLogic->getGroundHeight( subject.x, subject.y );
	ICoord2D drawn;
	TheTacticalView->worldToScreenTriReturn( &world, &drawn );
	const Real dx = drawn.x - pixel.x;
	const Real dy = drawn.y - pixel.y;
	return sqrtf( dx * dx + dy * dy );
}

//-------------------------------------------------------------------------------------------------
/** What the pane shows from the view as it is aimed now, its wedge round origin cut by the screen,
	* picked onto the terrain at every corner: the move of look that brings all of it inside the map.
	* worst is the ground under the corner furthest past the map and past how far, below zero inside.
	* A ray that meets no terrain, over the horizon or past the heightmap's overhang, is taken on the
	* look point's height. */
//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera::mapShift( Int pane, const Coord2D &origin, const Coord2D &look, Coord2D *worst, Real *past )
{
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	Coord2D vertices[ 8 ];
	const Int vertexCount = ObserverCamera_panePolygon( m_paneRays, m_paneCount, pane, origin, width, height, vertices );
	const Region2D map = mapRegion();
	const Real lookHeight = TheTerrainLogic->getGroundHeight( look.x, look.y );
	Coord2D grounds[ 8 ];
	for( Int vertex = 0; vertex < vertexCount; vertex++ )
	{
		ICoord2D at;
		at.x = min( max( REAL_TO_INT( vertices[ vertex ].x ), 0 ), width - 1 );
		at.y = min( max( REAL_TO_INT( vertices[ vertex ].y ), 0 ), height - 1 );
		Coord3D hit;
		if( !TheTacticalView->screenToTerrain( &at, &hit ) )
			TheTacticalView->screenToWorldAtZ( &at, &hit, lookHeight );
		grounds[ vertex ].x = hit.x - look.x;
		grounds[ vertex ].y = hit.y - look.y;
		const Real beyond = max( max( map.lo.x - hit.x, hit.x - map.hi.x ), max( map.lo.y - hit.y, hit.y - map.hi.y ) );
		if( vertex == 0 || beyond > *past )
		{
			*past = beyond;
			worst->x = hit.x;
			worst->y = hit.y;
		}
	}
	const Coord2D kept = ObserverCamera_keepInMap( look, grounds, vertexCount, map );
	Coord2D shift;
	shift.x = kept.x - look.x;
	shift.y = kept.y - look.y;
	return shift;
}

//-------------------------------------------------------------------------------------------------
/** How far a circle of radius pixels round centre reaches on the ground from subject, the point under
	* centre, through the view as it is aimed: the nearest of eight points round it, picked on ground. */
//-------------------------------------------------------------------------------------------------
static Real reachRound( const Coord2D &subject, const Coord2D &centre, Real radius, Real ground )
{
	enum { REACH_POINTS = 8 };
	Real nearest = 0.0f;
	for( Int point = 0; point < REACH_POINTS; point++ )
	{
		const Real angle = point * 2.0f * PI / REACH_POINTS;
		ICoord2D at;
		at.x = REAL_TO_INT( centre.x + radius * cosf( angle ) );
		at.y = REAL_TO_INT( centre.y + radius * sinf( angle ) );
		Coord3D hit;
		TheTacticalView->screenToWorldAtZ( &at, &hit, ground );
		const Real dx = hit.x - subject.x;
		const Real dy = hit.y - subject.y;
		const Real reach = sqrtf( dx * dx + dy * dy );
		nearest = point == 0 ? reach : min( nearest, reach );
	}
	return nearest;
}

//-------------------------------------------------------------------------------------------------
/** How far a circle of radius pixels round centre reaches on the ground at zoom, subject put on
	* centre. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera::groundReach( const Coord2D &subject, const Coord2D &centre, Real radius, Real zoom )
{
	placeOnPixel( subject, centre, zoom );
	return reachRound( subject, centre, radius, TheTerrainLogic->getGroundHeight( subject.x, subject.y ) );
}

//-------------------------------------------------------------------------------------------------
/** The highest zoom from nearest up to wanted at which the pane, all in and its subject on its
	* circle's centre, shows no ground past the map at any corner; nearest when none does.  More ground
	* shows the higher the camera stands, so it is found by halving. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera::zoomInMap( Int pane, const Coord2D &subject, Real nearest, Real wanted )
{
	enum { HALVINGS = 6 };
	Coord2D middle;
	middle.x = TheDisplay->getWidth() * 0.5f;
	middle.y = TheDisplay->getHeight() * 0.5f;
	Real fits = nearest;
	Real tried = wanted;
	Real lowest = nearest;
	Real highest = wanted;
	for( Int halving = 0; halving <= HALVINGS; halving++ )
	{
		const Coord2D look = placeOnPixel( subject, m_paneCentres[ pane ], tried );
		Coord2D worst;
		Real past = 0.0f;
		mapShift( pane, middle, look, &worst, &past );
		if( past <= 0.0f )
		{
			fits = tried;
			if( halving == 0 )
				break;
			lowest = tried;
		}
		else
		{
			highest = tried;
		}
		tried = ( lowest + highest ) * 0.5f;
	}
	return fits;
}

//-------------------------------------------------------------------------------------------------
/** The look point for a pane's camera this frame: subject on pixel at zoom, then moved the least that
	* keeps what the pane shows, its wedge round the meeting point where it is now, inside the map.
	* What it measured is kept for the log.  The view is left aimed there. */
//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera::placePane( Int pane, const Coord2D &subject, const Coord2D &pixel, Real zoom )
{
	Coord2D look = placeOnPixel( subject, pixel, zoom );
	m_paneShown[ pane ] = subject;
	m_paneError[ pane ] = pixelError( subject, pixel );
	const Coord2D shift = mapShift( pane, m_paneOrigin, look, &m_paneWorst[ pane ], &m_paneOutside[ pane ] );
	m_paneShifted[ pane ] = sqrtf( shift.x * shift.x + shift.y * shift.y );
	if( m_paneOutside[ pane ] > 0.0f )
	{
		look.x += shift.x;
		look.y += shift.y;
		aimView( look, zoom );
		mapShift( pane, m_paneOrigin, look, &m_paneWorst[ pane ], &m_paneOutside[ pane ] );
	}
	return look;
}

//-------------------------------------------------------------------------------------------------
/** The ground the whole picture spans across its middle row, and the least a split's pane circle
	* reaches round its centre, at the height of the ground the view looks at, through the camera the
	* last draw built.  Taken while there are no panes, when the view is the director's own at the zoom
	* the panes would start from. */
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

	Real rays[ OBSERVER_MOST_PANES ];
	const Int count = ObserverCamera_paneLayout( 2, rays, width, height );
	Coord2D centres[ OBSERVER_MOST_PANES ];
	Real radii[ OBSERVER_MOST_PANES ];
	ObserverCamera_paneCircles( rays, count, width, height, m_radarHalf, m_broadcastTop, centres, radii );
	for( Int pane = 0; pane < count; pane++ )
	{
		ICoord2D at;
		at.x = REAL_TO_INT( centres[ pane ].x );
		at.y = REAL_TO_INT( centres[ pane ].y );
		Coord3D under;
		TheTacticalView->screenToWorldAtZ( &at, &under, ground );
		const Coord2D subject = { under.x, under.y };
		const Real reach = reachRound( subject, centres[ pane ], radii[ pane ], ground );
		m_circleGround = pane == 0 ? reach : min( m_circleGround, reach );
	}
}

//-------------------------------------------------------------------------------------------------
/** The ground a split's pane would span across its middle row with the fight round subject fitted
	* into its circle, the way fitPanes fits it. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera::paneGround( const Coord2D &subject ) const
{
	const Real extent = ObserverCamera_extentAround( m_fights, subject, DIRECTOR_GATHER_RADIUS, PANE_FIT_LEAST_EXTENT );
	return ObserverCamera_paneGround( m_screenGround, m_circleGround, extent * ( 1.0f + PANE_FIT_MARGIN ) );
}

//-------------------------------------------------------------------------------------------------
Real ObserverCamera::splitApart( const Coord2D &second ) const
{
	return max( SPLIT_APART, max( paneGround( m_place ), paneGround( second ) ) );
}

//-------------------------------------------------------------------------------------------------
/** -directorrecord: whether the picture is split, and the second fight it shows.  Asked again only
	* when the hits were counted again.  Only real fighting counts, on both sides of the line: a base
	* going up, or a dozer clearing trees, is no reason to split.  A special power the director is
	* showing counts as a fight beside it. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateSplit( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( m_paneCount < 2 )
		measureScreenGround();
	Coord2D middle;
	Real firstHeat = ObserverCamera_heatAround( m_fights, m_place, &middle );
	if( m_placeKind == PLACE_EVENT )
		firstHeat = max( firstHeat, SPLIT_ENTER_HEAT );
	// a second fight is looked for beyond what pane 0's own picture would span; a split up keeps the
	// distance it opened at, so a fight that grows and zooms its pane out does not end it
	const Real searched = m_split ? m_splitApart : splitApart( m_place );
	Coord2D second = { 0.0f, 0.0f };
	Real secondHeat = 0.0f;
	ObserverCamera_secondPlace( m_fights, m_place, searched, m_homes, m_homeCount, &second, &secondHeat );
	if( secondHeat > 0.0f && ObserverCamera_fizzles( m_timeline, second, frame ) )
		secondHeat = 0.0f;
	// with a timeline a split is decided before it opens: pane 0 needs a fight the scouting pass saw
	// last through the least hold, or a special power, or it is about to leave for something else and
	// the split goes off before its panes are in
	const Bool timelineKnown = !m_timeline.empty();
	const UnsignedInt heldTo = frame + SPLIT_LEAST_FRAMES;
	const Bool firstLasts = !timelineKnown || m_placeKind == PLACE_EVENT || ObserverCamera_fightLasts( m_timeline, m_place, frame, heldTo, NULL );
	// the scouting pass knows the pairs: a second fight worth filming about to begin far enough away
	// splits the picture before its first shot, pane 1 waiting where it begins, and keeps it split
	// while it lasts.  Once it is going it is wherever the hits are, and in a lull pane 1 stays put;
	// sent back to where it began, pane 1 sat on empty ground while the fight went on elsewhere.  The
	// director that moved onto pane 1's fight has been handed it, and the split ends there: kept on
	// by the timeline, pane 1 went over to the fight pane 0 had just left
	const Real sameGround = searched * SPLIT_SAME_GROUND_SHARE;
	const Bool handedOver = m_split && within( m_place, m_secondPlace, sameGround );
	const Bool mayPlan = m_placeKind != PLACE_SIGHT && ( m_split || firstLasts );
	const Int planned = mayPlan ? ObserverCamera_plannedSecond( m_timeline, m_place, frame, searched, m_split, m_homes, m_homeCount ) : -1;
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

	// with the second fight known, both panes' pictures say how far apart is far enough
	const Real needed = m_split ? m_splitApart : splitApart( shown );
	const UnsignedInt since = frame >= m_splitChanged ? frame - m_splitChanged : 0;
	// a planned split rests after the last one like any other: opened 45 frames after one went off,
	// the radar slid back into its corner and straight out again
	const Bool plannedOpens = plannedSplit && ( m_split || since >= SPLIT_REST_FRAMES );
	Bool split = plannedOpens || ObserverCamera_holdSplit( m_split, firstHeat, secondHeat, apart, needed, since );
	// a live split opens, with a timeline, only on two different fights the scouting pass saw last: a
	// crowd beside pane 0's that the pass saw as part of it was pane 0's fight spreading, and the split
	// went off again two seconds later, once the director had followed it there
	if( split && !m_split && !plannedSplit && timelineKnown )
		split = firstLasts && ObserverCamera_fightLasts( m_timeline, shown, frame, heldTo, &m_place );
	split = split && !handedOver && apart > needed * SPLIT_SAME_GROUND_SHARE;
	// a split goes out the usual way when it has been up long enough, when the director has taken pane
	// 0 to another fight (pane 0 keeps the one it had until the panes are gone, see chooseTarget), and
	// when the second fight is now somewhere else.  Pane 0 retargeted under a split it never left, and a
	// pre-roll fifteen frames into one moved it 2900 units while it slid in
	const Bool upTooLong = m_split && since >= SPLIT_MOST_FRAMES;
	const Bool paneZeroLeft = m_split && !handedOver && !within( m_place, m_panesLeftPlace, sameGround );
	const Bool paneOneLeft = m_split && !within( shown, m_secondPlace, sameGround );
	if( split && ( upTooLong || paneZeroLeft || paneOneLeft ) )
	{
		DEBUG_LOG(( "OBSCAM frame %u split ends:%s%s%s\n", frame, upTooLong ? " up too long" : "",
			paneZeroLeft ? " the director went elsewhere" : "", paneOneLeft ? " the second fight is elsewhere" : "" ));
		split = FALSE;
		if( planned >= 0 )
			m_spentMoment = planned;
	}
	if( split != m_split )
	{
		DEBUG_LOG(( "OBSCAM frame %u split %s, first (%.0f,%.0f) heat %.1f on home %d's ground, second (%.0f,%.0f) heat %.1f on home %d's, %.0f apart of %.0f needed, screen %.0f%s\n",
			frame, split ? "on" : "off", m_place.x, m_place.y, firstHeat, ObserverCamera_groundOf( m_place, m_homes, m_homeCount ), shown.x, shown.y,
			secondHeat, ObserverCamera_groundOf( shown, m_homes, m_homeCount ), apart, needed, m_screenGround, plannedSplit ? ", planned" : "" ));
		m_split = split;
		m_splitChanged = frame;
		m_splitApart = needed;
		// the director has gone over to pane 1's fight: pane 1 is the picture that stays, and the panes
		// go out towards pane 0's side, where pane 0 used to fill the screen and the camera then cut
		// across the map to the fight pane 1 had been showing
		m_paneSurvivor = !split && handedOver && !m_intro ? 1 : 0;
	}
	if( m_split )
		m_secondPlace = shown;
}

//-------------------------------------------------------------------------------------------------
/** The panes' timeline, on logic frames, which is what each recorded picture is.  A split: the corner
	* radar slides out, the second pane slides in along the diagonal with the framed radar on its
	* corner, and it all goes back the same way when the split ends.  The match opens with a pane a
	* player, held a few seconds and then slid away to leave pane 0. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::advancePanes( UnsignedInt frame )
{
	static const char *const phaseNames[] = { "none", "radar out", "in", "draw", "held", "undraw", "out", "radar in" };
	PanePhase next = m_panePhase;
	const UnsignedInt elapsed = frame >= m_panePhaseStart ? frame - m_panePhaseStart : 0;
	switch( m_panePhase )
	{
		case PANES_NONE:
			if( !m_introDone )
			{
				m_introDone = TRUE;
				Int players = 0;
				for( Int index = 0; index < ThePlayerList->getPlayerCount() && players < OBSERVER_MOST_PANES; index++ )
				{
					const Player *player = ThePlayerList->getNthPlayer( index );
					if( player->isPlayableSide() && !player->isPlayerObserver() && player->isPlayerActive() )
						m_panePlayers[ players++ ] = player;
				}
				// teammates side by side round the meeting point: a player's team is the first one
				// before him allied with him both ways
				std::vector< Int > teams;
				for( Int pane = 0; pane < players; pane++ )
				{
					Int team = pane;
					for( Int earlier = 0; earlier < pane && team == pane; earlier++ )
					{
						const Player *one = m_panePlayers[ pane ];
						const Player *other = m_panePlayers[ earlier ];
						if( one->getRelationship( other->getDefaultTeam() ) == ALLIES && other->getRelationship( one->getDefaultTeam() ) == ALLIES )
							team = teams[ earlier ];
					}
					teams.push_back( team );
				}
				const std::vector< Int > order = ObserverCamera_teamOrder( teams );
				const Player *found[ OBSERVER_MOST_PANES ];
				for( Int pane = 0; pane < players; pane++ )
					found[ pane ] = m_panePlayers[ order[ pane ] ];
				for( Int pane = 0; pane < players; pane++ )
					m_panePlayers[ pane ] = found[ pane ];
				m_paneCount = ObserverCamera_paneLayout( players, m_paneRays, TheDisplay->getWidth(), TheDisplay->getHeight() );
				// the opening starts on the first view whole and opens into its panes like a split
				if( m_paneCount >= 2 )
				{
					m_intro = TRUE;
					next = PANES_RADAR_OUT;
				}
			}
			else if( m_split )
			{
				m_paneCount = ObserverCamera_paneLayout( 2, m_paneRays, TheDisplay->getWidth(), TheDisplay->getHeight() );
				next = PANES_RADAR_OUT;
			}
			if( next != PANES_NONE )
			{
				m_paneExit = ObserverCamera_paneExit( m_paneRays, m_paneCount, TheDisplay->getWidth(), TheDisplay->getHeight() );
				m_paneBaseZoom = TheTacticalView->getZoom();
				m_paneFitValid = FALSE;
				for( Int pane = 0; pane < OBSERVER_MOST_PANES; pane++ )
				{
					m_paneGlide[ pane ] = ViewLocation();
					m_paneExtent[ pane ] = PANE_FIT_LEAST_EXTENT;
				}
			}
			break;
		case PANES_RADAR_OUT:
			// a split that ended before its panes came takes the radar back without bringing them in
			if( !m_intro && !m_split )
				next = PANES_RADAR_IN;
			else if( elapsed >= PANE_RADAR_FRAMES )
				next = PANES_IN;
			break;
		case PANES_IN:
			if( elapsed >= PANE_SLIDE_FRAMES )
				next = PANES_DRAW;
			break;
		case PANES_DRAW:
			if( elapsed >= PANE_DRAW_FRAMES )
				next = PANES_HELD;
			break;
		case PANES_HELD:
			if( m_intro ? elapsed >= PANE_INTRO_FRAMES : !m_split )
				next = PANES_UNDRAW;
			// the opening ends on pane 0's base and the director stays there until something happens;
			// handed whatever sight it had come to meanwhile, it glided over empty ground to another base
			if( next == PANES_UNDRAW && m_intro && ( !m_placeValid || m_placeKind == PLACE_SIGHT ) )
			{
				m_place = m_paneSubject[ 0 ];
				m_placeKind = PLACE_SIGHT;
				m_placeHeight = 0.0f;
				m_placeSince = frame;
				m_placeValid = TRUE;
			}
			break;
		case PANES_UNDRAW:
			if( elapsed >= PANE_UNDRAW_FRAMES )
				next = PANES_OUT;
			break;
		case PANES_OUT:
			if( elapsed >= PANE_SLIDE_FRAMES )
			{
				next = PANES_RADAR_IN;
				m_introGlide = m_intro;
				// pane 1 fills the screen: from here pane 0 does, with pane 1's camera, which update hands it
				m_survivorHandover = m_paneSurvivor == 1;
				m_paneSurvivor = 0;
			}
			break;
		case PANES_RADAR_IN:
			if( elapsed >= PANE_RADAR_FRAMES )
			{
				next = PANES_NONE;
				m_paneCount = 0;
				m_intro = FALSE;
			}
			break;
	}
	if( next != m_panePhase )
	{
		DEBUG_LOG(( "OBSCAM frame %u panes %s, %d of them%s\n", frame, phaseNames[ next ], m_paneCount, m_intro ? ", the opening" : "" ));
		m_panePhase = next;
		m_panePhaseStart = frame;
	}

	switch( m_panePhase )
	{
		case PANES_NONE:
			m_paneProgress = 0.0f;
			m_cornerRadarSlide = 0.0f;
			break;
		case PANES_RADAR_OUT:
			m_paneProgress = 0.0f;
			m_cornerRadarSlide = ObserverCamera_easeFrames( frame, m_panePhaseStart, PANE_RADAR_FRAMES );
			break;
		case PANES_IN:
			m_paneProgress = ObserverCamera_easeFrames( frame, m_panePhaseStart, PANE_SLIDE_FRAMES );
			m_cornerRadarSlide = 1.0f;
			break;
		case PANES_DRAW:
			m_paneProgress = 1.0f;
			m_cornerRadarSlide = 1.0f;
			break;
		case PANES_HELD:
			m_paneProgress = 1.0f;
			m_cornerRadarSlide = 1.0f;
			break;
		case PANES_UNDRAW:
			m_paneProgress = 1.0f;
			m_cornerRadarSlide = 1.0f;
			break;
		case PANES_OUT:
			m_paneProgress = 1.0f - ObserverCamera_easeFrames( frame, m_panePhaseStart, PANE_SLIDE_FRAMES );
			m_cornerRadarSlide = 1.0f;
			break;
		case PANES_RADAR_IN:
			m_paneProgress = 0.0f;
			m_cornerRadarSlide = 1.0f - ObserverCamera_easeFrames( frame, m_panePhaseStart, PANE_RADAR_FRAMES );
			break;
	}

	// the gold draws on the panes once they have settled and goes back in before they leave
	const UnsignedInt phaseFrames = frame >= m_panePhaseStart ? frame - m_panePhaseStart : 0;
	m_lineProgress = m_panePhase == PANES_HELD ? 1.0f : 0.0f;
	if( m_panePhase == PANES_DRAW )
		m_lineProgress = min( (Real)phaseFrames / PANE_DRAW_FRAMES, 1.0f );
	if( m_panePhase == PANES_UNDRAW )
		m_lineProgress = 1.0f - min( (Real)phaseFrames / PANE_UNDRAW_FRAMES, 1.0f );
	m_paneOrigin.x = TheDisplay->getWidth() * 0.5f;
	m_paneOrigin.y = TheDisplay->getHeight() * 0.5f;
	if( m_paneCount >= 2 )
	{
		// pane 1 staying, the meeting point leaves the other way, and a split's two wedges are mirror
		// images, so the same distance leaves pane 1 holding the whole screen
		const Coord2D away = ObserverCamera_paneExitDirection( m_paneRays );
		const Real side = m_paneSurvivor == 1 ? -1.0f : 1.0f;
		m_paneOrigin.x += side * away.x * ( 1.0f - m_paneProgress ) * m_paneExit;
		m_paneOrigin.y += side * away.y * ( 1.0f - m_paneProgress ) * m_paneExit;
	}
}

//-------------------------------------------------------------------------------------------------
/** The opening's panes: how far each player's things spread round his fixed subject, his base and
	* the army beside it. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::updateIntroPlaces( void )
{
	std::vector< DirectorHeat > sights[ OBSERVER_MOST_PANES ];
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		const Int cost = obj->getTemplate()->friend_getBuildCost();
		if( cost <= 0 || obj->isEffectivelyDead() )
			continue;
		for( Int pane = 0; pane < m_paneCount; pane++ )
		{
			if( obj->getControllingPlayer() != m_panePlayers[ pane ] )
				continue;
			DirectorHeat heat;
			heat.position.x = obj->getPosition()->x;
			heat.position.y = obj->getPosition()->y;
			heat.weight = ObserverCamera_sightWeight( cost, obj->isKindOf( KINDOF_STRUCTURE ), FALSE, FALSE ) + 1.0f;
			sights[ pane ].push_back( heat );
		}
	}
	for( Int pane = 0; pane < m_paneCount; pane++ )
		m_paneExtent[ pane ] = ObserverCamera_extentAround( sights[ pane ], m_paneSubject[ pane ], PANE_INTRO_REACH, PANE_FIT_LEAST_EXTENT );
}

//-------------------------------------------------------------------------------------------------
/** The opening's subjects, fixed for the whole of it: each player's command centre, his start, or
	* where his things crowd when he has none.  Following the crowd let a pane wander off after the
	* first units to leave the base.  His plate hangs from the top of the command centre. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::pickIntroBases( void )
{
	std::vector< DirectorHeat > sights[ OBSERVER_MOST_PANES ];
	Bool based[ OBSERVER_MOST_PANES ];
	for( Int pane = 0; pane < m_paneCount; pane++ )
		based[ pane ] = FALSE;
	for( Object *obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject() )
	{
		const Int cost = obj->getTemplate()->friend_getBuildCost();
		if( obj->isEffectivelyDead() )
			continue;
		for( Int pane = 0; pane < m_paneCount; pane++ )
		{
			if( obj->getControllingPlayer() != m_panePlayers[ pane ] )
				continue;
			if( !based[ pane ] && obj->isKindOf( KINDOF_COMMANDCENTER ) )
			{
				based[ pane ] = TRUE;
				m_paneSubject[ pane ].x = obj->getPosition()->x;
				m_paneSubject[ pane ].y = obj->getPosition()->y;
				m_paneMark[ pane ] = *obj->getPosition();
				m_paneMark[ pane ].z += obj->getGeometryInfo().getMaxHeightAbovePosition() + PANE_MARK_LIFT;
			}
			if( cost <= 0 )
				continue;
			DirectorHeat heat;
			heat.position.x = obj->getPosition()->x;
			heat.position.y = obj->getPosition()->y;
			heat.weight = ObserverCamera_sightWeight( cost, obj->isKindOf( KINDOF_STRUCTURE ), FALSE, FALSE ) + 1.0f;
			sights[ pane ].push_back( heat );
		}
	}
	for( Int pane = 0; pane < m_paneCount; pane++ )
	{
		if( based[ pane ] )
			continue;
		Real heat = 0.0f;
		ObserverCamera_hottestPlace( sights[ pane ], &m_paneSubject[ pane ], &heat );
		m_paneMark[ pane ].x = m_paneSubject[ pane ].x;
		m_paneMark[ pane ].y = m_paneSubject[ pane ].y;
		m_paneMark[ pane ].z = TheTerrainLogic->getGroundHeight( m_paneSubject[ pane ].x, m_paneSubject[ pane ].y );
	}
	for( Int pane = 0; pane < m_paneCount; pane++ )
		m_homes[ pane ] = m_paneSubject[ pane ];
	m_homeCount = m_paneCount;
	DEBUG_LOG(( "OBSCAM frame %u %d homes, a split's second fight on another's ground\n", TheGameLogic->getFrame(), m_homeCount ));
}

//-------------------------------------------------------------------------------------------------
/** Every 30 logic frames while there are panes: each one's subject, its zoom now and with the panes
	* all in, and how far the map moved its look point off the subject, so a recording that drifts
	* says whether the subject moved or the map pushed the camera. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::logPanes( UnsignedInt frame ) const
{
	enum { PANE_LOG_FRAMES = 30 };
	if( m_paneCount < 2 || frame % PANE_LOG_FRAMES != 0 )
		return;
	for( Int pane = 0; pane < m_paneCount; pane++ )
	{
		DEBUG_LOG(( "OBSCAM frame %u pane %d subject (%.0f,%.0f) error %.1f px extent %.0f zoom %.2f fit %.2f base %.2f map shift %.0f worst (%.0f,%.0f) past %.0f\n",
			frame, pane, m_paneShown[ pane ].x, m_paneShown[ pane ].y, m_paneError[ pane ], m_paneExtent[ pane ], paneZoom( pane ),
			m_paneFit[ pane ], m_paneBaseZoom, m_paneShifted[ pane ], m_paneWorst[ pane ].x, m_paneWorst[ pane ].y, m_paneOutside[ pane ] ));
	}
}

//-------------------------------------------------------------------------------------------------
/** Each pane's zoom with the panes all in: its subject's spread, fitted into the largest circle its
	* wedge holds clear of the rays, the screen's edges and the radar's frame.  Never closer than the
	* director was when the panes came, never more than PANE_FIT_FARTHEST times further out, and never
	* so far out that the pane shows ground past the map.  It moves towards a new fit a share a logic
	* frame, and while the panes are held only outwards, so a fight that grows is let out and one that
	* shrinks does not pump the zoom; the map's limit brings it down whatever the phase. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::fitPanes( UnsignedInt frame )
{
	const IRegion2D &radar = m_radarFrame;
	if( radar.hi.x > radar.lo.x )
	{
		m_radarHalf.x = ( radar.hi.x - radar.lo.x ) * 0.5f;
		m_radarHalf.y = ( radar.hi.y - radar.lo.y ) * 0.5f;
	}
	ObserverCamera_paneCircles( m_paneRays, m_paneCount, TheDisplay->getWidth(), TheDisplay->getHeight(), m_radarHalf,
		m_broadcastTop, m_paneCentres, m_paneRadii );

	Coord2D subjects[ OBSERVER_MOST_PANES ];
	for( Int pane = 0; pane < m_paneCount; pane++ )
		subjects[ pane ] = m_paneSubject[ pane ];
	if( !m_intro )
	{
		subjects[ 0 ] = m_panesLeftPlace;
		subjects[ 1 ] = m_secondPlace;
		m_paneExtent[ 0 ] = ObserverCamera_extentAround( m_fights, subjects[ 0 ], DIRECTOR_GATHER_RADIUS, PANE_FIT_LEAST_EXTENT );
		m_paneExtent[ 1 ] = ObserverCamera_extentAround( m_fights, subjects[ 1 ], DIRECTOR_GATHER_RADIUS, PANE_FIT_LEAST_EXTENT );
	}

	// measured once a logic frame, through the view itself: the circle's reach at the nearest and the
	// farthest zoom gives the zoom that fits, and a fit is lowered to where the pane shows nothing past
	// the map, the subject still on its centre; one that has to come down goes down whatever the phase
	const Bool moves = frame != m_paneFitFrame;
	if( m_paneFitValid && !moves )
		return;
	m_paneFitFrame = frame;
	const Real farthest = m_paneBaseZoom * PANE_FIT_FARTHEST;
	for( Int pane = 0; pane < m_paneCount; pane++ )
	{
		const Real reach = m_paneExtent[ pane ] * ( 1.0f + PANE_FIT_MARGIN );
		const Real nearReach = groundReach( subjects[ pane ], m_paneCentres[ pane ], m_paneRadii[ pane ], m_paneBaseZoom );
		const Real farReach = groundReach( subjects[ pane ], m_paneCentres[ pane ], m_paneRadii[ pane ], farthest );
		const Real fitted = ObserverCamera_zoomForReach( m_paneBaseZoom, nearReach, farthest, farReach, reach, m_paneBaseZoom, farthest );		const Real wanted = zoomInMap( pane, subjects[ pane ], m_paneBaseZoom, fitted );
		const Bool mapLowers = wanted < fitted && wanted < m_paneFit[ pane ];
		if( !m_paneFitValid )
			m_paneFit[ pane ] = wanted;
		else if( moves && ( !panesSettled() || wanted > m_paneFit[ pane ] || mapLowers ) )
			m_paneFit[ pane ] += ( wanted - m_paneFit[ pane ] ) * PANE_FIT_FOLLOW;
	}
	m_paneFitValid = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A pane's zoom now: the director's own with no panes, its fit with them all in. */
//-------------------------------------------------------------------------------------------------
Real ObserverCamera::paneZoom( Int pane ) const
{
	return m_paneBaseZoom + ( m_paneFit[ pane ] - m_paneBaseZoom ) * m_paneProgress;
}

//-------------------------------------------------------------------------------------------------
/** Each pane past the first glides to its subject on its own, and its camera sits back from the
	* subject by the centre of the pane's circle, at the pane's own zoom, so the subject fills the
	* circle.  While the rays' meeting point slides, the camera sits back by that much more, so the
	* pane's picture slides with it and is the world on the whole screen, with no edge of a moved
	* picture to show.  Angle and pitch are the main camera's. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::stepPaneCameras( const ViewLocation &step, Real elapsedSeconds )
{
	if( m_paneCount < 2 )
		return;
	if( !m_intro )
		m_paneSubject[ 1 ] = m_secondPlace;

	const Coord3D &at = step.getPosition();
	for( Int pane = 1; pane < m_paneCount; pane++ )
	{
		const Real zoom = paneZoom( pane );
		ViewLocation subject;
		subject.init( m_paneSubject[ pane ].x, m_paneSubject[ pane ].y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
		if( !m_paneGlide[ pane ].isValid() )
		{
			m_paneVelocity[ pane ].x = m_paneVelocity[ pane ].y = m_paneVelocity[ pane ].z = 0.0f;
			m_paneVelocity[ pane ].angle = m_paneVelocity[ pane ].pitch = m_paneVelocity[ pane ].zoom = 0.0f;
			m_paneGlide[ pane ] = subject;
		}
		else
		{
			const ViewLocation glide = ObserverCamera_approach( m_paneGlide[ pane ], subject, elapsedSeconds,
				DIRECTOR_PAN_SECONDS, DIRECTOR_TOP_SPEED, &m_paneVelocity[ pane ] );
			m_paneGlide[ pane ].init( glide.getPosition().x, glide.getPosition().y, at.z, step.getAngle(), step.getPitch(), step.getZoom() );
		}

		// the circle's centre moved with the meeting point; it only ever slides down the screen, away
		// from the horizon, so the projection holds off the screen too
		Coord2D pixel;
		Real radius = 0.0f;
		getPaneCircle( pane, &pixel, &radius );
		Coord2D glided;
		glided.x = m_paneGlide[ pane ].getPosition().x;
		glided.y = m_paneGlide[ pane ].getPosition().y;
		const Coord2D camera = placePane( pane, glided, pixel, zoom );
		m_paneView[ pane ].init( camera.x, camera.y, at.z, step.getAngle(), step.getPitch(), zoom );
	}
}

//-------------------------------------------------------------------------------------------------
/** On the meeting point when the panes are all in, and far enough out along its way off the screen,
	* the radar's own size further, that none of it shows when they have gone. */
//-------------------------------------------------------------------------------------------------
Coord2D ObserverCamera::getFramedRadarMiddle( Real radarDiagonal ) const
{
	const Coord2D away = ObserverCamera_paneExitDirection( m_paneRays );
	const Real side = m_paneSurvivor == 1 ? -1.0f : 1.0f;
	const Real out = side * ( 1.0f - m_paneProgress ) * ( m_paneExit + radarDiagonal );
	Coord2D middle;
	middle.x = TheDisplay->getWidth() * 0.5f + away.x * out;
	middle.y = TheDisplay->getHeight() * 0.5f + away.y * out;
	return middle;
}

//-------------------------------------------------------------------------------------------------
/** Where each pane's subject is drawn, the way update and stepPaneCameras put it: pane 0's moves from
	* the middle of the screen to its circle as the panes come in, every other pane's circle slides
	* with the meeting point.  Moved with the meeting point as well, pane 0's label hung under its
	* subject while the panes slid, over the fight it was naming. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::getPaneCircle( Int pane, Coord2D *centre, Real *radius ) const
{
	const Real middleX = TheDisplay->getWidth() * 0.5f;
	const Real middleY = TheDisplay->getHeight() * 0.5f;
	if( pane == 0 || pane == m_paneSurvivor )
	{
		centre->x = middleX + ( m_paneCentres[ pane ].x - middleX ) * m_paneProgress;
		centre->y = middleY + ( m_paneCentres[ pane ].y - middleY ) * m_paneProgress;
	}
	else
	{
		centre->x = m_paneCentres[ pane ].x + m_paneOrigin.x - middleX;
		centre->y = m_paneCentres[ pane ].y + m_paneOrigin.y - middleY;
	}
	*radius = m_paneRadii[ pane ];
}

//-------------------------------------------------------------------------------------------------
/** A split's two panes show the fights at the director's place and at the second one, and their
	* sides are every player in the hits within the gather radius of each. */
//-------------------------------------------------------------------------------------------------
PlayerMaskType ObserverCamera::getPaneSides( Int pane ) const
{
	const Coord2D &subject = pane == 0 ? m_panesLeftPlace : m_secondPlace;
	PlayerMaskType sides = 0;
	for( size_t index = 0; index < m_fights.size(); index++ )
		if( sameFight( m_fights[ index ].position, subject ) )
			sides |= m_fightSides[ index ];
	return sides;
}

//-------------------------------------------------------------------------------------------------
Int ObserverCamera::getIntroPlayerIndex( Int pane ) const
{
	return m_panePlayers[ pane ]->getPlayerIndex();
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::beginPanePass( Int pane )
{
	TheTacticalView->getLocation( &m_firstView );
	TheTacticalView->setLocation( &m_paneView[ pane ] );
	m_drawingPane = pane;
}

//-------------------------------------------------------------------------------------------------
void ObserverCamera::endPanePass( void )
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
			obj->isKindOf( KINDOF_FS_SUPERWEAPON ) );
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
		const Bool superweapon = obj->isKindOf( KINDOF_FS_SUPERWEAPON );
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

		heat.weight = ObserverCamera_hitWeight( cost, obj->isEffectivelyDead(), superweapon );
		hits.push_back( heat );

		// -directorrecord's split counts only fights, and its labels name who is in them
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
		if( frame >= event.since + EVENT_LAUNCH_FRAMES && ObserverCamera_heatAround( hits, event.target, &middle ) > 0.0f )
			event.until = max( event.until, frame + EVENT_AFTERMATH_FRAMES );
		if( !event.landed && ObserverCamera_heatAround( fights, event.target, &middle ) > 0.0f )
		{
			event.landed = TRUE;
			noteFlag( event );
		}
		if( !event.landed )
			continue;
		if( best == NULL || event.weight >= best->weight )
			best = &event;
	}
	DirectorEvent *current = m_placeValid && m_placeKind == PLACE_EVENT ? findEvent( m_placeEvent ) : NULL;
	if( ObserverCamera_stayOnEvent( current, best, held ) )
	{
		m_place = ObserverCamera_eventPlace( *current, frame );
		*place = m_place;
		return TRUE;
	}
	// an event that just ended hands straight over to the next; anything else is given its settle
	// first, except a superweapon still leaving its silo
	if( best != NULL && ( !m_placeValid || m_placeKind == PLACE_EVENT || ObserverCamera_eventCutsIn( *best, frame, held ) ) )
	{
		DEBUG_LOG(( "OBSCAM frame %u director to special power %u at (%.0f,%.0f)%s\n", frame, best->id,
			best->target.x, best->target.y, best->superweapon ? " superweapon" : "" ));
		m_place = ObserverCamera_eventPlace( *best, frame );
		m_placeKind = PLACE_EVENT;
		m_placeHeight = 0.0f;
		m_placeEvent = best->id;
		m_placeSince = frame;
		m_placeValid = TRUE;
		*place = m_place;
		return TRUE;
	}

	// a fight the scouting pass saw begin is waited for where it begins, so the picture has it from
	// the first shot; it becomes an ordinary fight when it starts.  It is gone to from nothing, from a
	// fight that has settled when it is clearly bigger, and from anything for a superweapon's silo
	if( m_placeValid && m_placeKind == PLACE_UPCOMING )
	{
		if( frame < m_timeline[ m_placeMoment ].start )
		{
			*place = m_place;
			return TRUE;
		}
		m_placeKind = PLACE_FIGHT;
		m_placePeak = 0.0f;
		m_placeSince = frame;
	}
	// the fight pane 1 holds stays pane 1's: pane 0 going there swapped the two panes' subjects
	const Int upcoming = timeline ? ObserverCamera_prerollMoment( m_timeline, frame, m_split ? &m_secondPlace : NULL, m_splitApart ) : -1;
	if( upcoming >= 0 && upcoming != m_placeMoment )
	{
		const DirectorMoment &moment = m_timeline[ upcoming ];
		Coord2D middle;
		const Real heatHere = m_placeValid && m_placeKind == PLACE_FIGHT ? ObserverCamera_heatAround( hits, m_place, &middle ) : 0.0f;
		if( moment.superweapon || heatHere <= 0.0f || ( held >= DIRECTOR_SETTLE_FRAMES && moment.peak > heatHere * DIRECTOR_SWITCH_MARGIN ) )
		{
			DEBUG_LOG(( "OBSCAM frame %u pre-roll to %s at frame %u (%.0f,%.0f) peak %.1f\n", frame,
				moment.superweapon ? "superweapon" : "fight", moment.start, moment.place.x, moment.place.y, moment.peak ));
			m_place = moment.place;
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
		// a split's pane 0 keeps its own fight while it goes on rather than take pane 1's; once it is
		// over the director moves there and the split ends, pane 1's fight handed to pane 0
		const Bool heldByPaneOne = m_split && within( hottest, m_secondPlace, m_splitApart );
		m_placePeak = max( m_placePeak, heatHere );
		if( heatHere > 0.0f && ( sameFight( hottest, followed ) || heldByPaneOne || !ObserverCamera_shouldMove( heatHere, hottestHeat, held, m_placePeak ) ) )
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
	if( TheGlobalData->m_directorRecord && m_placeScanned == TheGameLogic->getFrame() )
		updateSplit();
	// the opening shows the first player's base in pane 0.  Pane 0 keeps its place until the panes
	// have gone: a split ends when the director moves onto the second fight, and pane 0 going there at
	// once showed that fight twice while pane 1 slid out
	if( m_intro && m_panePhase != PANES_OUT && m_panePhase != PANES_RADAR_IN )
		place = m_paneSubject[ 0 ];
	const Bool leaving = m_panePhase == PANES_UNDRAW || m_panePhase == PANES_OUT;
	const Bool panesUp = m_panePhase == PANES_IN || panesSettled() || m_panePhase == PANES_OUT;
	if( panesUp && ( leaving || ( !m_split && !m_intro ) ) )
		place = m_panesLeftPlace;
	else if( panesUp && m_split && !m_intro && !within( place, m_panesLeftPlace, m_splitApart * SPLIT_SAME_GROUND_SHARE ) )
	{
		// updateSplit ends a split whose pane 0 the director takes elsewhere before this is reached; a
		// place that still moves this far under panes would be a jump, so pane 0 stays and says so
		DEBUG_LOG(( "OBSCAM frame %u pane 0 would jump to (%.0f,%.0f) under a split, held at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
			place.x, place.y, m_panesLeftPlace.x, m_panesLeftPlace.y ));
		place = m_panesLeftPlace;
	}
	else
		m_panesLeftPlace = place;
	// while there are panes each one keeps what it shows inside the map itself; the whole screen's
	// corners, wider than pane 0 and at a zoom the panes do not draw at, pushed its subject off its
	// circle and out of the picture
	if( m_paneCount < 2 )
		place = keepInMap( place, current );
	target->init( place.x, place.y, at.z, current.getAngle(), current.getPitch(), current.getZoom() );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** While panes are up and for HANDOVER_LOG_FRAMES after, a line a frame with pane 0's look point,
	* zoom, subject and the pixel the view, aimed as it will draw, puts the subject on: a hand-over
	* between the panes and the single view that jumps shows as a step in those numbers. */
//-------------------------------------------------------------------------------------------------
void ObserverCamera::logHandover( const ViewLocation &step, const ViewLocation &placed )
{
	// the panes moving, and HANDOVER_LOG_FRAMES after; held still, once a second.  A split held 5300
	// frames wrote a line every one of them
	const UnsignedInt frame = TheGameLogic->getFrame();
	// inside a split no pane jumps: each pane's largest subject step a logic frame, written when the
	// panes are gone, says whether one did.  The subject and not the look point: a pane sliding off the
	// screen's edge keeps its subject on its circle's centre, and the look under that perspective swung
	// 89 units in a frame while the picture moved evenly.  The radar sliding is not the panes
	const Bool panesShown = m_paneCount >= 2 && m_panePhase != PANES_RADAR_IN && m_panePhase != PANES_RADAR_OUT;
	if( panesShown && frame != m_paneLookFrame )
	{
		const Coord2D looks[ 2 ] = { { step.getPosition().x, step.getPosition().y },
			{ m_paneGlide[ 1 ].getPosition().x, m_paneGlide[ 1 ].getPosition().y } };
		for( Int pane = 0; pane < 2; pane++ )
		{
			if( m_paneLookFrame != 0 )
			{
				const Real dx = looks[ pane ].x - m_paneLookLast[ pane ].x;
				const Real dy = looks[ pane ].y - m_paneLookLast[ pane ].y;
				m_paneLookStepMost[ pane ] = max( m_paneLookStepMost[ pane ], sqrtf( dx * dx + dy * dy ) / ( frame - m_paneLookFrame ) );
			}
			m_paneLookLast[ pane ] = looks[ pane ];
		}
		m_paneLookFrame = frame;
	}
	else if( !panesShown && m_paneLookFrame != 0 )
	{
		DEBUG_LOG(( "OBSCAM frame %u panes down, largest subject step a frame: pane 0 %.1f, pane 1 %.1f\n", frame,
			m_paneLookStepMost[ 0 ], m_paneLookStepMost[ 1 ] ));
		m_paneLookFrame = 0;
		m_paneLookStepMost[ 0 ] = m_paneLookStepMost[ 1 ] = 0.0f;
	}
	if( m_panePhase != PANES_NONE && m_panePhase != PANES_HELD )
		m_handoverLogUntil = frame + HANDOVER_LOG_FRAMES;
	if( frame >= m_handoverLogUntil && ( m_panePhase != PANES_HELD || frame % LOGICFRAMES_PER_SECOND != 0 ) )
		return;
	TheTacticalView->aimCamera();
	Coord3D world = step.getPosition();
	world.z = TheTerrainLogic->getGroundHeight( world.x, world.y );
	ICoord2D pixel;
	pixel.x = pixel.y = -1;
	TheTacticalView->worldToScreenTriReturn( &world, &pixel );
	DEBUG_LOG(( "OBSCAM frame %u hand look (%.1f,%.1f) zoom %.3f subject (%.1f,%.1f) at (%d,%d)\n", frame,
		placed.getPosition().x, placed.getPosition().y, TheTacticalView->getZoom(), world.x, world.y, pixel.x, pixel.y ));
	if( m_paneCount < 2 )
		return;
	Coord2D circle;
	Real radius = 0.0f;
	getPaneCircle( 1, &circle, &radius );
	DEBUG_LOG(( "OBSCAM frame %u hand pane 1 look (%.1f,%.1f) zoom %.3f subject (%.1f,%.1f) at (%.0f,%.0f)\n", frame,
		m_paneView[ 1 ].getPosition().x, m_paneView[ 1 ].getPosition().y, m_paneView[ 1 ].getZoom(),
		m_paneGlide[ 1 ].getPosition().x, m_paneGlide[ 1 ].getPosition().y, circle.x, circle.y ));
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

	// -directorrecord's panes run on the logic clock, on the split the last scan decided
	if( TheGlobalData->m_directorRecord )
	{
		const UnsignedInt frame = TheGameLogic->getFrame();
		if( !m_timelineLoaded )
			loadTimeline();
		checkTimeline( frame );
		updateBroadcastMoments( frame );
		const Bool introStarting = !m_introDone;
		advancePanes( frame );
		if( m_intro && introStarting )
			pickIntroBases();
		if( m_intro && ( introStarting || frame % DIRECTOR_SCAN_FRAMES == 0 ) )
			updateIntroPlaces();
		// pane 1 went out holding the whole screen: the single view takes its camera, its subject and
		// its glide as they stand, and goes on from there
		if( m_survivorHandover )
		{
			m_survivorHandover = FALSE;
			TheTacticalView->setLocation( &m_paneView[ 1 ] );
			TheTacticalView->getLocation( &current );
			m_mainOffset.x = m_paneGlide[ 1 ].getPosition().x - current.getPosition().x;
			m_mainOffset.y = m_paneGlide[ 1 ].getPosition().y - current.getPosition().y;
			m_velocity = m_paneVelocity[ 1 ];
			m_panesLeftPlace.x = m_paneGlide[ 1 ].getPosition().x;
			m_panesLeftPlace.y = m_paneGlide[ 1 ].getPosition().y;
			DEBUG_LOG(( "OBSCAM frame %u panes went out on pane 1, the single view takes its camera\n", frame ));
		}
	}

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
	// glide, takes the height at once with it, so it is one clean cut and not a cut and a step.
	// Panes set the zoom outright; when they let go the spring starts from the height the panes left
	// the camera at, so the view does not settle from there to an old height in one go
	const Bool panesZoom = m_paneCount >= 2 && !isShowingPlayerView();
	const Coord3D &from = current.getPosition();
	const Real gapX = target.getPosition().x - from.x - ( m_driving ? m_mainOffset.x : 0.0f );
	const Real gapY = target.getPosition().y - from.y - ( m_driving ? m_mainOffset.y : 0.0f );
	const Bool cut = !m_introGlide && !panesZoom && gapX * gapX + gapY * gapY > CUT_DISTANCE * CUT_DISTANCE;
	if( !m_heightDriven )
	{
		m_heightExtra = 0.0f;
		m_heightExtraVelocity = 0.0f;
	}
	if( m_panesHeldZoom && !panesZoom && m_heightDriven )
	{
		m_heightExtra = TheTacticalView->getCurrentHeightAboveGround() - m_handHeight;
		m_heightExtraVelocity = 0.0f;
	}
	// a step back or in that no glide made: the zoom moved more than ZOOM_JUMP_SHARE between two
	// updates without a cut, panes aside, which set their zoom outright as they slide
	if( TheGlobalData->m_directorRecord && m_lastZoom > 0.0f && !m_lastCut && !panesZoom && !m_panesHeldZoom
		&& fabsf( current.getZoom() - m_lastZoom ) > m_lastZoom * ZOOM_JUMP_SHARE )
		DEBUG_LOG(( "OBSCAM frame %u zoom jump %.3f -> %.3f\n", TheGameLogic->getFrame(), m_lastZoom, current.getZoom() ));
	m_lastZoom = current.getZoom();
	m_lastCut = cut;
	m_panesHeldZoom = panesZoom;
	const Real extraBefore = m_heightExtra;
	m_heightExtra = ObserverCamera_easeHeight( m_heightExtra, DIRECTOR_WIDE_EXTRA + m_placeHeight, &m_heightExtraVelocity, elapsed / MILLISECONDS_PER_SECOND, cut );
	// panes hold the view's height and set the zoom outright from the zoom they opened at, so the
	// spring's steps go into that zoom while they are up.  Held at the zoom of the moment they opened,
	// a special power's split went back out to a picture far below where the director was heading,
	// and the single view then climbed the whole way back: a descent and a climb for one use
	if( panesZoom && m_heightDriven )
		m_paneBaseZoom += ( m_heightExtra - extraBefore ) * ( TheTacticalView->getZoomForHeight( 1.0f ) - TheTacticalView->getZoomForHeight( 0.0f ) );
	if( isShowingPlayerView() )
		releaseHeight();
	else
		driveHeight( m_heightExtra );
	// while there are panes the zoom is set outright from how far in they are, with the view's own
	// settling held off: it eases on every draw, and a frame has a draw a pane
	holdHeight( isShowingPlayerView() || panesZoom );
	if( !m_driving )
	{
		m_velocity.x = m_velocity.y = m_velocity.z = m_velocity.angle = m_velocity.pitch = m_velocity.zoom = 0.0f;
		m_mainOffset.x = m_mainOffset.y = 0.0f;
	}

	// the glide is of the subject; the camera sits back from it so the subject is in pane 0's circle,
	// moving there as the panes come in and back to the middle of the screen as they go
	Coord2D fromMiddle = { 0.0f, 0.0f };
	if( panesZoom )
	{
		m_aimFrom = current;
		fitPanes( TheGameLogic->getFrame() );
		fromMiddle.x = ( m_paneCentres[ 0 ].x - TheDisplay->getWidth() * 0.5f ) * m_paneProgress;
		fromMiddle.y = ( m_paneCentres[ 0 ].y - TheDisplay->getHeight() * 0.5f ) * m_paneProgress;
	}
	const Coord3D &camera = current.getPosition();
	ViewLocation subject;
	subject.init( camera.x + m_mainOffset.x, camera.y + m_mainOffset.y, camera.z, current.getAngle(), current.getPitch(), current.getZoom() );
	// from the opening's last player to the director's first place is a glide whatever the distance,
	// on a target kept just inside the cut distance until the real one is
	if( m_introGlide )
	{
		const Real dx = target.getPosition().x - subject.getPosition().x;
		const Real dy = target.getPosition().y - subject.getPosition().y;
		const Real distance = sqrtf( dx * dx + dy * dy );
		const Real glideReach = CUT_DISTANCE * 0.9f;
		if( distance > glideReach )
		{
			const Real share = glideReach / distance;
			target.init( subject.getPosition().x + dx * share, subject.getPosition().y + dy * share, target.getPosition().z,
				target.getAngle(), target.getPitch(), target.getZoom() );
		}
		else
			m_introGlide = FALSE;
	}
	const Bool player = m_mode == OBSERVER_CAMERA_PLAYER;
	ViewLocation step = ObserverCamera_approach( subject, target, elapsed / MILLISECONDS_PER_SECOND,
		player ? PLAYER_PAN_SECONDS : DIRECTOR_PAN_SECONDS, player ? PLAYER_TOP_SPEED : DIRECTOR_TOP_SPEED, &m_velocity );
	Coord2D sitBack;
	sitBack.x = step.getPosition().x;
	sitBack.y = step.getPosition().y;
	if( panesZoom )
	{
		const Coord3D &stepAt = step.getPosition();
		step.init( stepAt.x, stepAt.y, stepAt.z, step.getAngle(), step.getPitch(), paneZoom( 0 ) );
		Coord2D pixel;
		pixel.x = TheDisplay->getWidth() * 0.5f + fromMiddle.x;
		pixel.y = TheDisplay->getHeight() * 0.5f + fromMiddle.y;
		m_aimFrom = step;
		sitBack = placePane( 0, sitBack, pixel, step.getZoom() );
	}
	else if( TheGlobalData->m_directorRecord && !player )
	{
		// the single view puts the subject on the screen's middle pixel the way pane 0 does at the end of
		// its exit and the start of its entry.  Looking straight at the subject drew it off the middle by
		// the view's lift over the ground, and the picture jumped sideways by that much the frame the
		// panes let go and again the frame they came
		Coord2D middle;
		middle.x = TheDisplay->getWidth() * 0.5f;
		middle.y = TheDisplay->getHeight() * 0.5f;
		m_aimFrom = step;
		sitBack = placeOnPixel( sitBack, middle, step.getZoom() );
	}
	// the pane cameras are placed by aiming the view, so before the view is put where it draws
	stepPaneCameras( step, elapsed / MILLISECONDS_PER_SECOND );
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

	if( TheGlobalData->m_directorRecord )
	{
		logPanes( TheGameLogic->getFrame() );
		logHandover( step, placed );
	}
}
