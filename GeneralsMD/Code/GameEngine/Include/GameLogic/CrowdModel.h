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

// FILE: CrowdModel.h //////////////////////////////////////////////////////////////////////////
// The route as a band of measured width, and the rules a unit uses to decide where across it to
// drive.
//
// The band is the part retail has no equivalent of.  A route is a line, every unit handed one
// steers at the same metre of it, and a group of twenty crossing open ground therefore drives in
// single file for no reason anybody chose.  A corridor measures how much ground there actually is
// either side of that line, sample by sample, and then a lane is a distance across it - not a
// share of it.  The distinction is not cosmetic: a fraction of the width pulls units into every
// wide bay the route passes and swings them backwards round the outside of corners, which is what
// the first version of this did.
//
// Author: fork, 2026
///////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _CROWD_MODEL_H_
#define _CROWD_MODEL_H_

#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "GameLogic/LocomotorSet.h"

#include <vector>

class Object;
class Path;

enum
{
	/* How far sideways the corridor looks for ground, in cells.  Sixteen, which is what
		 PF_LANE_PROBE_CELLS uses and therefore the widest half-band anything else in the engine can
		 report; the two have to agree or the group is handed lanes the band will not hold.

		 It used to be the sandbox's ten, on the argument that a wider probe is not a wider road and
		 finds the far side of open country that no lane is ever going to use.  That was true while the
		 number of lanes was decided once, at the group's feet: nothing could grow into the extra room.
		 Now the lane count is re-fitted to this band every frame, so the room is exactly what the group
		 spreads into, and stopping at a hundred feet was a cap on how wide a group could ever get. */
	CROWD_PROBE_CELLS			= 16,

	/// samples are one cell apart until the route is longer than this, then they stretch
	CROWD_MAX_SAMPLES			= 160,

	/// a lane taken to get round somebody is held this long, so it cannot flap side to side
	CROWD_HOLD_FRAMES			= 3 * LOGICFRAMES_PER_GAME_SECOND / 2,

	/* How many samples either side of a bridge lose their band as well, so the approach funnels in.
		 Six cells and not four: a unit that is still a lane wide of the centre when the band shuts is
		 a unit arriving at the abutment sideways, and the sideways step is rate-limited to a quarter
		 of its speed.  The funnel has to be longer than the time that step takes. */
	CROWD_BRIDGE_SEAL			= 6,

	/// how far ahead of itself a unit steers, in cells, before its own body length is added
	CROWD_LOOKAHEAD_CELLS	= 2,

	/* The most lanes a group is ever laid out in, whatever the ground says.  Fitting the count to
		 the band every frame took the old fixed cap off, and on open country that is a group of
		 twenty arriving as a line twenty abreast: the front is wider than anything it is walking
		 into, the flanks take the whole map's worth of fire before the middle is in range, and a
		 rank that wide cannot get through the first gap without renumbering itself completely.
		 Five is the widest that still fits through a base entrance without a re-fit, so the group
		 keeps its shape from the field to the door. */
	CROWD_MAX_LANES				= 5
};

/** How many lanes to lay across `span` of drivable ground, `spacing` apart: what the ground holds,
		capped at CROWD_MAX_LANES, and never more lanes than there are bodies to put in them
		(`members` of 0 means the caller counts them itself). */
extern Int Crowd_laneCount( Real span, Real spacing, Int members );

/// one corner of a route, and the deck it is on
struct CrowdRoutePoint
{
	Coord3D						pos;
	PathfindLayerEnum	layer;
};
typedef std::vector<CrowdRoutePoint> CrowdRoute;

/// the optimized corners of `path`, first to last
extern void Crowd_routeFromPath( Path *path, CrowdRoute *out );

/** Where a lane `offset` left of a route turns at the corner `c` between `a` and `b`: along the
		bisector of the two legs' left normals, as far as keeps both legs `offset` away, and never
		more than twice `offset` however sharp the turn.  FALSE for a leg of no length. */
extern Bool Crowd_laneCorner( const Coord3D& a, const Coord3D& c, const Coord3D& b, Real offset, Coord2D *shift );

/**
 * A route, sampled, with the width of the drivable ground either side of every sample.
 *
 * Built once when a unit is handed a path and thrown away with it.  The probing is the expensive
 * part (twenty cell tests a sample) which is why it is not done per frame, and why the sample
 * count is capped rather than the step being fixed.
 */
class CrowdCorridor
{
public:
	struct Sample
	{
		Coord3D		pos;			///< the point on the route itself
		Coord2D		tan;			///< unit direction of travel here
		Real			left;			///< drivable ground to the left, past this body's own radius
		Real			right;		///< and to the right
		Real			along;		///< distance from the start of the route
		PathfindLayerEnum layer;	///< which deck this sample is on: anything above ground is a bridge or a wall
	};

	CrowdCorridor( void );

	/** Measure the route.  Returns false if the path is too short to have a direction, in which
			case the corridor stays empty and the caller drives the centre line the way retail does. */
	Bool build( const Object *obj, const LocomotorSet& locomotorSet, Path *path );

	Bool isEmpty( void ) const { return m_samples.size() < 2; }
	Int count( void ) const { return (Int)m_samples.size(); }
	const Sample& at( Int i ) const { return m_samples[ i ]; }
	Real length( void ) const { return m_samples.empty() ? 0.0f : m_samples.back().along; }

	/** Index of the sample nearest to pos.  `hint` is where the caller was last frame; the search
			starts there and only falls back to the whole route if that window misses. */
	Int nearest( const Coord3D& pos, Int hint ) const;

	/// how hard the route turns at this sample, as the cross product of two tangents four apart
	Real curvature( Int i ) const;

	/// the world point `lat` to the left of sample i (negative is right)
	void point( Int i, Real lat, Coord3D *out ) const;

	/// `lat`, cut down to what the ground at sample i actually allows
	Real clampLat( Int i, Real lat ) const;

	/// where across the band `pos` sits, measured against sample i
	Real latOf( Int i, const Coord3D& pos ) const;

	/** How far along the route `pos` is, measured against sample i.  The sample's own `along` is
			quantised to the sample step; this is not, which is what lets a steering point advance a
			foot at a time instead of a cell at a time. */
	Real alongOf( Int i, const Coord3D& pos ) const;

	/// the world point `lat` to the left of the route at distance `along`, interpolated between samples
	void pointAt( Real along, Real lat, Coord3D *out ) const;

	/// `lat`, cut down to what the ground allows at distance `along`
	Real clampLatAt( Real along, Real lat ) const;

	/** `lat`, cut down to the tightest the ground gets anywhere between `from` and `to`.

			Clamping at the two ends only is what lets a unit aim across a corner: both the point it is
			standing on and the point it is steering at are comfortably inside the band, and the pinch
			between them is not consulted by either.  The unit then drives the straight line between
			them, which goes through the building on the corner.  This is the cheap version of a line
			of sight test - no cell lookups, just the samples that were measured when the band was
			built. */
	Real clampLatNarrowest( Real from, Real to, Real lat ) const;

	/** Build from a plain polyline, with the same width everywhere: for tests, which have no Object,
			no pathfinder and no ground to probe.  `layers`, when given, is one entry per point, and so
			is `halfWidths`, which overrides the single width where a test needs a pinch in the road. */
	void buildForTest( const Coord3D *pts, Int count, Real halfWidth,
										 const PathfindLayerEnum *layers = NULL, const Real *halfWidths = NULL );

	/// close the band on every bridge deck and on the ground either side of one
	void sealBridges( void );

private:
	/// the sample at or before `along`, with the fraction of the way to the next one
	Int bracket( Real along, Real *frac ) const;

	std::vector<Sample>	m_samples;
};

/** Right of way by body size.  A bigger unit has less room to be squeezed and less patience in a
		bottleneck, so the small one moves.  The margin stops two nearly-equal bodies from each
		deciding the other outranks them. */
// (the second parameter is not called `small`: rpcndr.h, which comes in with windows.h, defines
//  that name as `char`)
extern Bool Crowd_outranks( const Object *big, const Object *little );

/** Distance between two bodies, discs, never negative-infinite: touching is zero and overlapping
		is negative by however much they overlap. */
extern Real Crowd_gap( const Object *a, const Object *b );

/** How far along its own route a unit still has to go, which is what priority is decided on.
		Falls back to straight-line distance to the goal when there is no path. */
extern Real Crowd_remaining( const Object *obj );

/** The speed to hold behind a unit that is going the same way, or `speed` unchanged when there is
		nothing to hold back for.

		Closing speed and the time it leaves, never the raw gap.  Braking by distance slows a whole
		column that was not catching anybody up, and braking behind a unit that is not moving at all
		is worse than either: the collision never happens, so the blocked-frame count never rises and
		none of the engine's own stuck machinery ever fires.  Measured over eight seeds, distance
		braking cost 3518 blocked unit-frames a match and braking behind parked allies 16593, against
		1236 for this. */
extern Real Crowd_brakeSpeed( Real speed, Real blockerSpeed, Real gap, Int frames );

/** The throttle a unit is allowed this frame, given the one it held last frame.

		A brake is taken the moment it is asked for; coming off one is a ramp.  Every input to the
		speed cap is read raw once a frame - who is in front of us, how fast he is going, how much air
		is left - and all three of them flicker: the blocker slips out of the lookahead cone for a
		frame and the cap jumps back to full speed, the unit lunges, the blocker is inside the cone
		again and the brake goes back on.  That is a column pumping the pedal several times a second,
		and it feeds itself, because the unit behind reads our speed and does the same harder. */
extern Real Crowd_releaseCap( Real held, Real want, Real filter );

/** The inside of a bend counts as being further ahead than it is.
		A unit on the inside of a turn has the least room and is the easiest to squeeze, so it is
		let out of the corner first and the outside units, which have room to keep rolling, flow
		round it.  Zero on a straight. */
extern Real Crowd_bendBonus( const CrowdCorridor *corr, Int sample, Real lat );

#endif // _CROWD_MODEL_H_
