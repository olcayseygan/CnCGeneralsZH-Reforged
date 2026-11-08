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

// FILE: CrowdModel.cpp ////////////////////////////////////////////////////////////////////////
// The measured band, and the small pieces of arithmetic the steering rules are built out of.
///////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "Common/GlobalData.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CrowdModel.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"

//-------------------------------------------------------------------------------------------------
CrowdCorridor::CrowdCorridor( void )
{
}

//-------------------------------------------------------------------------------------------------
/** How much drivable ground there is one way from a point, stopping at the first place the body
		would not fit.  Half-cell steps, so a doorway one cell wide is not reported as two.

		Deliberately not the clearance map: that is a distance to the nearest obstacle in any
		direction, so a route hugging a wall reads as narrow even when the whole field is open on the
		other side - and that asymmetry is the only thing the band is for. */
//-------------------------------------------------------------------------------------------------
static Real crowdExtent( const Object *obj, const LocomotorSet& locomotorSet, PathfindLayerEnum layer,
												 const Coord3D *from, const Coord2D *dir )
{
	const Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
	const Real step = PATHFIND_CELL_SIZE_F * 0.5f;
	const Real maxDist = PATHFIND_CELL_SIZE_F * (Real)CROWD_PROBE_CELLS;

	Real room = 0.0f;
	for (Real s = step; s <= maxDist; s += step)
	{
		Coord3D probe;
		probe.x = from->x + dir->x * s;
		probe.y = from->y + dir->y * s;
		probe.z = from->z;
		if (!TheAI->pathfinder()->validMovementPosition( isCrusher, layer, locomotorSet, &probe ))
			break;
		room = s;
	}

	// the body has to fit in whatever was found, so the lane only owns the part past its own radius
	if (obj)
		room -= obj->getGeometryInfo().getBoundingCircleRadius();

	return room > 0.0f ? room : 0.0f;
}

//-------------------------------------------------------------------------------------------------
void Crowd_routeFromPath( Path *path, CrowdRoute *out )
{
	out->clear();
	for (const PathNode *n = path->getFirstNode(); n; n = n->getNextOptimized())
	{
		CrowdRoutePoint p;
		p.pos = *n->getPosition();
		p.layer = n->getLayer();
		out->push_back( p );
	}
}

static const Real CROWD_LANE_MIN_COS = 0.5f;		///< a hairpin stretches a corner's offset no more than twice

//-------------------------------------------------------------------------------------------------
Bool Crowd_laneCorner( const Coord3D& a, const Coord3D& c, const Coord3D& b, Real offset, Coord2D *shift )
{
	Coord2D in, onward;
	in.x = c.x - a.x;				in.y = c.y - a.y;
	onward.x = b.x - c.x;		onward.y = b.y - c.y;
	if (in.length() < 0.01f || onward.length() < 0.01f)
		return FALSE;
	in.normalize();
	onward.normalize();

	Coord2D miter;
	miter.x = -in.y - onward.y;
	miter.y = in.x + onward.x;
	Real stretch = 1.0f;
	if (miter.length() < 0.01f)
	{
		// the route doubles straight back: the lane keeps the side it was on
		miter.x = -in.y;
		miter.y = in.x;
	}
	else
	{
		miter.normalize();
		const Real cosHalf = miter.x * -in.y + miter.y * in.x;
		stretch = 1.0f / ((cosHalf > CROWD_LANE_MIN_COS) ? cosHalf : CROWD_LANE_MIN_COS);
	}
	const Real reach = offset * stretch;
	shift->x = miter.x * reach;
	shift->y = miter.y * reach;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool CrowdCorridor::build( const Object *obj, const LocomotorSet& locomotorSet, Path *path )
{
	m_samples.clear();

	if (path == NULL)
		return FALSE;

	const PathNode *node = path->getFirstNode();
	if (node == NULL || node->getNextOptimized() == NULL)
		return FALSE;

	// length first, so the step can be chosen to fit the sample budget
	Real total = 0.0f;
	for (const PathNode *n = node; n && n->getNextOptimized(); n = n->getNextOptimized())
	{
		Real segLen = 0.0f;
		n->getNextOptimized( NULL, &segLen );
		total += segLen;
	}
	if (total < PATHFIND_CELL_SIZE_F)
		return FALSE;

	Real step = PATHFIND_CELL_SIZE_F;
	if (total / step > (Real)(CROWD_MAX_SAMPLES - 1))
		step = total / (Real)(CROWD_MAX_SAMPLES - 1);

	Real walked = 0.0f;			// distance already covered by emitted samples
	Real base = 0.0f;				// distance to the start of the segment being walked
	for (const PathNode *n = node; n && n->getNextOptimized(); n = n->getNextOptimized())
	{
		Coord2D dir;
		Real segLen = 0.0f;
		const PathNode *next = n->getNextOptimized( &dir, &segLen );
		if (segLen < 0.001f)
			continue;

		const Coord3D *a = n->getPosition();
		const Coord3D *b = next->getPosition();

		/* The layer this piece of route is on, not the one the unit is standing on.  A route over a
			 bridge changes deck halfway along, and probing a bridge sample against the ground layer asks
			 how much room there is on the riverbank underneath - which is where a lane measured that way
			 sends the unit.  That is the walking off the side of a bridge. */
		const PathfindLayerEnum layer = n->getLayer();

		while (walked <= base + segLen && (Int)m_samples.size() < CROWD_MAX_SAMPLES)
		{
			const Real t = walked - base;

			Sample s;
			s.pos.x = a->x + dir.x * t;
			s.pos.y = a->y + dir.y * t;
			s.pos.z = a->z + (b->z - a->z) * (t / segLen);
			s.tan = dir;
			s.along = walked;
			s.layer = layer;

			Coord2D left, right;
			left.x = -dir.y;	left.y = dir.x;
			right.x = dir.y;	right.y = -dir.x;
			s.left = crowdExtent( obj, locomotorSet, layer, &s.pos, &left );
			s.right = crowdExtent( obj, locomotorSet, layer, &s.pos, &right );

			m_samples.push_back( s );
			walked += step;
		}

		base += segLen;
	}

	if (m_samples.size() < 2)
	{
		m_samples.clear();
		return FALSE;
	}

	/* Smooth the two width curves.  A single sample that happened to land beside a rock reports a
		 metre of road in the middle of a field, and a lane clamped to that one sample jerks sideways
		 and back again over two frames.  Two [1 2 1] passes, which is what the sandbox settled on. */
	const Int n = (Int)m_samples.size();
	std::vector<Real> tmpL( n ), tmpR( n );
	for (Int pass = 0; pass < 2; pass++)
	{
		for (Int i = 0; i < n; i++)
		{
			const Int p = (i > 0) ? i - 1 : 0;
			const Int q = (i < n - 1) ? i + 1 : n - 1;
			tmpL[ i ] = (m_samples[ p ].left + 2.0f * m_samples[ i ].left + m_samples[ q ].left) * 0.25f;
			tmpR[ i ] = (m_samples[ p ].right + 2.0f * m_samples[ i ].right + m_samples[ q ].right) * 0.25f;
		}
		for (Int i = 0; i < n; i++)
		{
			m_samples[ i ].left = tmpL[ i ];
			m_samples[ i ].right = tmpR[ i ];
		}
	}

	sealBridges();

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A bridge has no band, and neither does the ground each side of one.

		Probing the right deck already reports a bridge as narrow, but narrow is not the same as
		single file: half a lane of drift is enough to put a tank over the railing, and the roadway is
		a different width from the pathfind cells that carry it.  The approach matters as much as the
		deck.  A unit holding a lane a body width off centre while it rolls up to a bridge arrives
		beside the entrance rather than at it, stops against the abutment, and every unit behind it
		queues on the bank - which is the not getting onto the bridge at all.  So the band closes
		CROWD_BRIDGE_SEAL samples before the deck starts and opens the same distance after it ends,
		and inside that stretch every unit drives the centre line exactly the way retail does.

		Letting the deck keep the width its own layer measures, with the approach held to that width
		instead of to nothing, was tried and shipped for a while, and it lost on both counts.  At the
		same vehicle speed twenty Crusaders over the Kandahar bridge on Golden Oasis got their last tank
		across 7.5 seconds later on seed 1 (44.1 against 36.6), and over the long bridge the two tanks
		on the outside lane came down the bank beside the ramp instead of onto it, pressed against its
		side and stood there for the rest of the match, on every seed tried. */
//-------------------------------------------------------------------------------------------------
void CrowdCorridor::sealBridges( void )
{
	const Int n = (Int)m_samples.size();

	for (Int i = 0; i < n; i++)
	{
		if (m_samples[ i ].layer <= LAYER_GROUND)
			continue;

		Int lo = i - CROWD_BRIDGE_SEAL;
		Int hi = i + CROWD_BRIDGE_SEAL;
		if (lo < 0) lo = 0;
		if (hi > n - 1) hi = n - 1;
		for (Int j = lo; j <= hi; j++)
		{
			m_samples[ j ].left = 0.0f;
			m_samples[ j ].right = 0.0f;
		}
	}
}

//-------------------------------------------------------------------------------------------------
Int CrowdCorridor::nearest( const Coord3D& pos, Int hint ) const
{
	const Int n = (Int)m_samples.size();
	if (n == 0)
		return 0;

	Int lo = hint - 4;
	Int hi = hint + 12;
	if (lo < 0) lo = 0;
	if (hi > n - 1) hi = n - 1;

	Int best = lo;
	Real bestSqr = 1e30f;
	for (Int i = lo; i <= hi; i++)
	{
		const Real dx = pos.x - m_samples[ i ].pos.x;
		const Real dy = pos.y - m_samples[ i ].pos.y;
		const Real d = dx * dx + dy * dy;
		if (d < bestSqr) { bestSqr = d; best = i; }
	}

	/* The window is right while the unit is following the route and wrong the moment it is not -
		 shoved off it, retreating, or handed a new one - so a miss falls back to the whole route
		 rather than steering at a sample fifty cells behind. */
	const Real close = PATHFIND_CELL_SIZE_F * 6.0f;
	if (bestSqr > close * close && (lo > 0 || hi < n - 1))
	{
		for (Int i = 0; i < n; i++)
		{
			const Real dx = pos.x - m_samples[ i ].pos.x;
			const Real dy = pos.y - m_samples[ i ].pos.y;
			const Real d = dx * dx + dy * dy;
			if (d < bestSqr) { bestSqr = d; best = i; }
		}
	}

	return best;
}

//-------------------------------------------------------------------------------------------------
Real CrowdCorridor::curvature( Int i ) const
{
	const Int n = (Int)m_samples.size();
	if (n < 2)
		return 0.0f;
	if (i < 0) i = 0;
	if (i > n - 1) i = n - 1;
	Int j = i + 4;
	if (j > n - 1) j = n - 1;
	return m_samples[ i ].tan.x * m_samples[ j ].tan.y - m_samples[ i ].tan.y * m_samples[ j ].tan.x;
}

//-------------------------------------------------------------------------------------------------
void CrowdCorridor::point( Int i, Real lat, Coord3D *out ) const
{
	const Sample& s = m_samples[ i ];
	out->x = s.pos.x - s.tan.y * lat;
	out->y = s.pos.y + s.tan.x * lat;
	out->z = s.pos.z;
}

//-------------------------------------------------------------------------------------------------
Real CrowdCorridor::clampLat( Int i, Real lat ) const
{
	const Sample& s = m_samples[ i ];
	if (lat > s.left) lat = s.left;
	if (lat < -s.right) lat = -s.right;
	return lat;
}

//-------------------------------------------------------------------------------------------------
Real CrowdCorridor::latOf( Int i, const Coord3D& pos ) const
{
	const Sample& s = m_samples[ i ];
	return (pos.x - s.pos.x) * -s.tan.y + (pos.y - s.pos.y) * s.tan.x;
}

//-------------------------------------------------------------------------------------------------
Real CrowdCorridor::alongOf( Int i, const Coord3D& pos ) const
{
	const Sample& s = m_samples[ i ];
	Real a = s.along + (pos.x - s.pos.x) * s.tan.x + (pos.y - s.pos.y) * s.tan.y;
	if (a < 0.0f)
		a = 0.0f;
	const Real len = length();
	if (a > len)
		a = len;
	return a;
}

//-------------------------------------------------------------------------------------------------
Int CrowdCorridor::bracket( Real along, Real *frac ) const
{
	const Int n = (Int)m_samples.size();
	*frac = 0.0f;
	if (n < 2)
		return 0;

	Int lo = 0;
	Int hi = n - 1;
	while (lo + 1 < hi)
	{
		const Int mid = (lo + hi) / 2;
		if (m_samples[ mid ].along <= along)
			lo = mid;
		else
			hi = mid;
	}

	const Real span = m_samples[ lo + 1 ].along - m_samples[ lo ].along;
	Real f = (span > 0.001f) ? (along - m_samples[ lo ].along) / span : 0.0f;
	if (f < 0.0f) f = 0.0f;
	if (f > 1.0f) f = 1.0f;
	*frac = f;
	return lo;
}

//-------------------------------------------------------------------------------------------------
/** The same point `point` returns, but at a distance rather than at a sample.

		Samples are a cell apart, so a steering point taken from one jumps ten feet forward every time
		the unit crosses a sample boundary, and the direction handed to the locomotor steps with it.  A
		heavy tank's steering absorbs that; a light one turns it into a wobble down the whole march. */
//-------------------------------------------------------------------------------------------------
void CrowdCorridor::pointAt( Real along, Real lat, Coord3D *out ) const
{
	Real f = 0.0f;
	const Int i = bracket( along, &f );
	const Int j = (i + 1 < (Int)m_samples.size()) ? i + 1 : i;
	const Sample& a = m_samples[ i ];
	const Sample& b = m_samples[ j ];

	Real tx = a.tan.x + (b.tan.x - a.tan.x) * f;
	Real ty = a.tan.y + (b.tan.y - a.tan.y) * f;
	const Real tl = (Real)sqrt( tx * tx + ty * ty );
	if (tl > 0.001f)
	{
		tx /= tl;
		ty /= tl;
	}
	else
	{
		// two samples pointing exactly opposite each other: keep the one we are leaving
		tx = a.tan.x;
		ty = a.tan.y;
	}

	out->x = a.pos.x + (b.pos.x - a.pos.x) * f - ty * lat;
	out->y = a.pos.y + (b.pos.y - a.pos.y) * f + tx * lat;
	out->z = a.pos.z + (b.pos.z - a.pos.z) * f;
}

//-------------------------------------------------------------------------------------------------
Real CrowdCorridor::clampLatNarrowest( Real from, Real to, Real lat ) const
{
	if (to < from)
	{
		const Real swap = from;
		from = to;
		to = swap;
	}

	lat = clampLatAt( from, lat );
	lat = clampLatAt( to, lat );

	// and every whole sample in between, which is where the pinch that neither end can see lives
	const Int n = (Int)m_samples.size();
	for (Int k = 0; k < n; k++)
	{
		const Sample& s = m_samples[ k ];
		if (s.along <= from)
			continue;
		if (s.along >= to)
			break;
		if (lat > s.left) lat = s.left;
		if (lat < -s.right) lat = -s.right;
	}
	return lat;
}

//-------------------------------------------------------------------------------------------------
Real CrowdCorridor::clampLatAt( Real along, Real lat ) const
{
	Real f = 0.0f;
	const Int i = bracket( along, &f );
	const Int j = (i + 1 < (Int)m_samples.size()) ? i + 1 : i;

	const Real left  = m_samples[ i ].left  + (m_samples[ j ].left  - m_samples[ i ].left ) * f;
	const Real right = m_samples[ i ].right + (m_samples[ j ].right - m_samples[ i ].right) * f;

	if (lat > left) lat = left;
	if (lat < -right) lat = -right;
	return lat;
}

//-------------------------------------------------------------------------------------------------
void CrowdCorridor::buildForTest( const Coord3D *pts, Int count, Real halfWidth,
																	const PathfindLayerEnum *layers, const Real *halfWidths )
{
	m_samples.clear();
	if (pts == NULL || count < 2)
		return;

	Real along = 0.0f;
	for (Int k = 0; k < count; k++)
	{
		const Int ahead = (k + 1 < count) ? k + 1 : k;
		const Int behind = (k + 1 < count) ? k : k - 1;

		Real dx = pts[ ahead ].x - pts[ behind ].x;
		Real dy = pts[ ahead ].y - pts[ behind ].y;
		const Real d = (Real)sqrt( dx * dx + dy * dy );
		if (d > 0.001f)
		{
			dx /= d;
			dy /= d;
		}

		if (k > 0)
		{
			const Real sx = pts[ k ].x - pts[ k - 1 ].x;
			const Real sy = pts[ k ].y - pts[ k - 1 ].y;
			along += (Real)sqrt( sx * sx + sy * sy );
		}

		Sample s;
		s.pos = pts[ k ];
		s.tan.x = dx;
		s.tan.y = dy;
		s.left = halfWidths ? halfWidths[ k ] : halfWidth;
		s.right = halfWidths ? halfWidths[ k ] : halfWidth;
		s.along = along;
		s.layer = layers ? layers[ k ] : LAYER_GROUND;
		m_samples.push_back( s );
	}
}

//-------------------------------------------------------------------------------------------------
Int Crowd_laneCount( Real span, Real spacing, Int members )
{
	if (spacing < 0.001f)
		return 1;

	// centres, so a span exactly one spacing wide is two lanes and a span of nothing is still one
	Int lanes = (Int)(span / spacing) + 1;
	if (lanes > CROWD_MAX_LANES)
		lanes = CROWD_MAX_LANES;
	if (members > 0 && lanes > members)
		lanes = members;
	if (lanes < 1)
		lanes = 1;
	return lanes;
}

//-------------------------------------------------------------------------------------------------
Bool Crowd_outranks( const Object *big, const Object *little )
{
	if (big == NULL || little == NULL)
		return FALSE;
	const Real rb = big->getGeometryInfo().getBoundingCircleRadius();
	const Real rs = little->getGeometryInfo().getBoundingCircleRadius();
	return rb > rs + 2.0f;
}

//-------------------------------------------------------------------------------------------------
Real Crowd_gap( const Object *a, const Object *b )
{
	const Coord3D *pa = a->getPosition();
	const Coord3D *pb = b->getPosition();
	const Real dx = pb->x - pa->x;
	const Real dy = pb->y - pa->y;
	const Real d = (Real)sqrt( dx * dx + dy * dy );
	return d - a->getGeometryInfo().getBoundingCircleRadius()
					 - b->getGeometryInfo().getBoundingCircleRadius();
}

//-------------------------------------------------------------------------------------------------
Real Crowd_remaining( const Object *obj )
{
	const AIUpdateInterface *ai = obj ? obj->getAIUpdateInterface() : NULL;
	if (ai == NULL)
		return 0.0f;

	/* Along the route, not across the map.  Two units the same distance from the goal are not the
		 same distance from it when one of them has a hill in the way, and priority handed to the one
		 with the longer drive is priority handed to the wrong unit. */
	const Path *path = ai->getPath();
	if (path != NULL && path->hasCachedPointOnPath())
		return path->peekCachedDistToGoal();

	const Coord3D *goal = ai->getGoalPosition();
	if (goal == NULL)
		return 0.0f;
	const Coord3D *pos = obj->getPosition();
	const Real dx = goal->x - pos->x;
	const Real dy = goal->y - pos->y;
	return (Real)sqrt( dx * dx + dy * dy );
}

//-------------------------------------------------------------------------------------------------
Real Crowd_brakeSpeed( Real speed, Real blockerSpeed, Real gap, Int frames )
{
	if (frames <= 0)
		return speed;

	/* His speed, plus enough to close the gap over `frames` frames rather than this one.  The two
		 cases that need no brake fall out of the same line and are not worth testing for: a blocker
		 going as fast as us or faster puts `want` above `speed`, and so does a gap wider than the
		 closing speed can cross in the window. */
	Real want = blockerSpeed + gap / (Real)frames;
	if (want < 0.0f) want = 0.0f;
	return (want < speed) ? want : speed;
}

//-------------------------------------------------------------------------------------------------
Real Crowd_releaseCap( Real held, Real want, Real filter )
{
	if (want <= held)
		return want;
	return held + (want - held) * filter;
}

//-------------------------------------------------------------------------------------------------
Real Crowd_bendBonus( const CrowdCorridor *corr, Int sample, Real lat )
{
	if (corr == NULL || corr->isEmpty())
		return 0.0f;

	const Real curv = corr->curvature( sample );
	if (fabs( curv ) < 0.08f)
		return 0.0f;

	// positive curvature is a left turn, and a unit on the left of a left turn is on the inside
	const Real inside = (curv > 0.0f) ? lat : -lat;
	if (inside <= 0.0f)
		return 0.0f;

	Real strength = fabs( curv ) * 2.0f;
	if (strength > 1.0f) strength = 1.0f;
	return inside * strength;
}
