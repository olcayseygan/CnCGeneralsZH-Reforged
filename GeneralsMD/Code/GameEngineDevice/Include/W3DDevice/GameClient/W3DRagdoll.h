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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: W3DRagdoll.h
// A soldier's death drawn as a limp body instead of the canned death animation: the Ragdoll option
// (off under Classic graphics), or -ragdoll over it for a run.  Client only; the option is read here
// and nowhere in the logic.  The logic keeps the dead object exactly as it always did (where it lies, the slow
// death's sink and its removal); this only overrides the bone transforms of the drawable's own
// render object, through the HTree's bone capture, and reads nothing back into the logic.  The
// bones are verlet particles held by distance constraints (W3DRagdoll.cpp has the rules), pushed by
// the last hit the logic reported through Drawable::reactToImpact, falling onto the drawn terrain.

#pragma once

#ifndef __W3D_RAGDOLL_H_
#define __W3D_RAGDOLL_H_

#include "Lib/BaseType.h"
#include "WWMath/matrix3.h"
#include "WWMath/matrix3d.h"
#include "WWMath/quat.h"
#include "WWMath/vector3.h"

class RenderObjClass;
class ThingTemplate;

/** The last hit that took health off the soldier, as reactToImpact hands it over. */
struct RagdollImpact
{
	RagdollImpact() : radius( 0.0f ), hasSource( FALSE ), valid( FALSE ), syncTime( 0 ) { pos.zero(); source.zero(); }
	Coord3D pos;					///< where it landed
	Coord3D source;				///< where the shooter stood, when hasSource
	Real radius;					///< the blast's radius, 0 for a direct hit
	Bool hasSource;
	Bool valid;
	UnsignedInt syncTime;	///< WW3D::Get_Sync_Time when it arrived; an old hit pushes nothing
};

class W3DRagdoll
{
public:
	/** The template is infantry, so its drawable listens for hits whether or not the option is on now. */
	static Bool wants( const ThingTemplate *tmpl );

	/** A new death may go limp: the Ragdoll option outside Classic graphics, or -ragdoll.  Asked at
		each death, so a switch mid-match reaches the next one and a live ragdoll plays out. */
	static Bool enabled( void );

	/** Takes over robj's skeleton from the pose it holds now.  NULL when the cap of live ragdolls is
		reached or the skeleton is one it cannot use (logged once per template); the death animation
		then plays as before. */
	static W3DRagdoll *create( RenderObjClass *robj, const RagdollImpact &impact, const ThingTemplate *tmpl );

	~W3DRagdoll();

	/** Every draw, after the drawable's transform reached robj.  FALSE when robj is no longer the
		render object it took over (a model swap): the caller deletes it and the animation takes over. */
	Bool update( RenderObjClass *robj );

private:
	enum { MAX_BONES = 64, MAX_NEIGHBORS = 8, MAX_CONSTRAINTS = 192, MAX_OBSTACLES = 8, STOWED = -2 };

	/** A vehicle or building near the body at death, as its template's geometry stood then. */
	struct Obstacle
	{
		Real x, y, z;				///< its position
		Real cosA, sinA;		///< its facing
		Real major, minor;	///< a box's half length and half width, else the radius in major
		Real top;						///< world height of its top
		Bool box;
	};

	enum Role
	{
		ROLE_KINEMATIC,		///< the root and the mover dummy under it: left to the base pose and the drawable's transform
		ROLE_PARTICLE,		///< simulated
		ROLE_FOLLOW,			///< rides rigidly on m_anchor (a zero length bone, the gun)
	};

	struct Bone
	{
		Role role;
		Int parent;
		Int anchor;							///< ROLE_FOLLOW: the bone it rides on
		Int carrier;						///< a bone of a long carried thing (the gun and what hangs off it): that thing's first bone; STOWED for a thing put away; else -1
		Int simParent;					///< ROLE_PARTICLE: the nearest particle above it, -1 for the body's first
		Bool offAxis;						///< a carried bone well off its thing's length: hangs from it, kept out of the ground on its own
		Bool trunk;							///< ROLE_PARTICLE: held stiff with the spine
		Vector3 x0;							///< world position at the start
		Real restLength;				///< world distance to its parent at the start
		Matrix3D baseLocalInv;	///< the inverse of the HTree's base transform relative to the parent
		Matrix3D followLocal;		///< ROLE_FOLLOW: its transform in the anchor's frame at the start
		Matrix3x3 rot0;					///< world rotation at the start
		Vector3 levelAxis;			///< a long carried thing's first bone: what rot0 turns about to lay its length level
		Real levelAngle;				///< and how far
		Vector3 axisLocal;			///< and its length, in its own frame
		Vector3 grip;						///< and where its anchor held it, in its own frame at the start
		Real yaw0;							///< and the heading of its length at the start
		Real yaw;								///< and the heading it was last given
		Vector3 x;							///< ROLE_PARTICLE: position now
		Vector3 xPrev;					///< and one step ago
		Quaternion q;						///< rotation since the start, warm start for the next solve
		Int numNeighbors;
		Int neighbor[ MAX_NEIGHBORS ];
		Vector3 rest[ MAX_NEIGHBORS ];	///< neighbour minus self at the start, world space
	};

	struct Constraint
	{
		Int a, b;
		Real minD, maxD;
	};

	W3DRagdoll();
	const char *build( RenderObjClass *robj, const RagdollImpact &impact, const char *templateName );	///< NULL, or why the skeleton cannot be used
	void addConstraint( Int a, Int b, Real minD, Real maxD );
	void step( Real dt, Bool gravity, Real sinkOffset );
	Real worstStretch( Int *which ) const;	///< how far the constraint furthest out is, and which one
	void gatherObstacles( const Vector3 &at );	///< the vehicles and buildings the body could fall into
	void collide( Vector3 &x );						///< a particle pushed out of them
	void pose( RenderObjClass *robj, Real sinkOffset );
	void logPose( RenderObjClass *robj, const char *when, Real sinkOffset );	///< the RAGDOLL line: where the bones landed against where they were sent

	RenderObjClass *m_robj;		///< identity only, never dereferenced once it may be gone
	const char *m_name;				///< the template's, for the log
	Int m_gun;								///< the first long carried thing's first bone, -1 when none; the log follows it
	Real m_gunHandTilt;				///< degrees off level its length would be riding the hand, at the last pose
	Int m_numBones;
	Bone m_bone[ MAX_BONES ];
	Int m_numConstraints;
	Constraint m_constraint[ MAX_CONSTRAINTS ];
	Int m_numObstacles;
	Obstacle m_obstacle[ MAX_OBSTACLES ];
	Int m_pushes;							///< particle pushes out of them, for the log
	Quaternion m_trunkQ;			///< the trunk's turn since the start, as last put back in shape
	Real m_lastPoseMs;				///< sync milliseconds from death to the last pose, for the log
	Vector3 m_lastRoot;				///< the drawable's position last update, for the carry
	UnsignedInt m_lastSync;
	UnsignedInt m_startSync;	///< sync time at death, for the log
	Real m_accumMs;
	Real m_height0;						///< the drawable's height over the drawn ground at death, when below it; the sink is measured from there
	Int m_stillSteps;
	Int m_heldAwake;					///< times it came to rest with its constraints still out
	Bool m_asleep;
};

#endif // __W3D_RAGDOLL_H_
