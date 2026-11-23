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

// FILE: W3DRagdoll.cpp
// See W3DRagdoll.h.  How the skeleton is read, generically, so another infantry rig needs no code:
//
// The root and any bone under it shorter than MIN_BONE_LENGTH (the mover dummy, "DUM - MVR" on the
// Ranger) stay kinematic: not captured, posed by the base pose from the drawable's transform.  The
// child of those with the most descendants is the body; every bone in it is a particle, except a
// near zero length one, which rides on its parent.  Whatever else hangs off the mover is carried:
// each bone off it, with everything under it, rides on the particle nearest any of its bones at death,
// so the Ranger's BONE-GUN and MUZZLE01 stay in his hand and the Missile Defender's BONE_PACK on his
// back.  A carried thing LONG_CARRIED or longer is drawn level and turned only about the vertical,
// held where the hand held it and kept off the ground: following the hand's whole turn, a launcher
// on the shoulder of a man who fell on his back stood upright out of him.  Such a thing is held by
// an arm, never a toe; a bone of it far off its length (a ghillie flap under the Pathfinder's rifle)
// rides along but only keeps itself out of the ground; and a thing 20 units or more from the body at
// death is put away out of sight (the Angry Mob's spare guns) and stays exactly where it was.
// A particle's parent in the simulation is its nearest particle ancestor: the body bone sits on a
// zero length PELVIS that rides on it.
//
// The constraints, all lengths taken from the pose at the moment of death:
//  - every particle to its parent particle, fixed;
//  - particle siblings to each other, fixed: the hips with the spine, the shoulders with the head;
//  - at a bone with two or more particle children (the spine), or whose one child has (the chest
//    under the neck), the joint is stiff: each child to the bone's parent and to the bone's siblings
//    within STIFF_MIN to STIFF_MAX of its length;
//  - at any other bone with one particle child (shoulder, hip) the joint folds no tighter than
//    JOINT_MIN_ANGLE_DEG, or than it was folded at death;
//  - a bone an artist added to the body (no space in its name: PARA_MAN, a ghillie flap) only to its
//    parent, so it hangs loose and lies down by itself; its parent's joint ignores it.
// The Ranger's rig has no elbow and no knee: upper arm to hand and thigh to foot are one rigid link
// each, and his limbs swing as sticks.  Nothing stops a hip bending backward or a shoulder twisting.
// Vehicles and buildings near the body at death push its particles out of their template's box or
// cylinder, read off the client's drawables and never written.
//
// A bone's rotation is the rotation that best carries the offsets to its neighbours at death onto
// the offsets now (Muller, Bender, Chentanez and Macklin, "A Robust Method to Extract the Rotational
// Part of Deformations", 2016), warm started from the last one.  A leaf (hand, foot, head) borrows
// its parent's neighbours, so the head turns with the chest.  The HTree's capture multiplies its
// matrix after the base pose (PivotClass::Capture_Update), so each frame the animation is held at
// the base pose and every captured bone gets the matrix that lands it on the simulated transform.
//
// The logic moves the dead body on its own: the slide AIDeadState scrubs out, the fling and the
// shockwave through PhysicsBehavior, the sink.  Every particle rides that movement (the carry);
// while the body is in the air the carry is its fall, so the particles take no gravity of their own.

#include "Common/EarlyCommandLine.h"
#include "Common/Geometry.h"
#include "Common/GlobalData.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DRagdoll.h"
#include "WW3D2/htree.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/ww3d.h"

#include <chrono>
#include <math.h>
#include <set>
#include <stdio.h>
#include <string>
#include <string.h>

/// live ragdolls at once; a death past it plays its animation
static const Int MAX_ACTIVE_RAGDOLLS = 24;

static const Real STEP_MS = 1000.0f / 60.0f;			///< fixed simulation step, in W3D sync milliseconds
static const Int MAX_STEPS_PER_UPDATE = 8;				///< a gap longer than 133 ms (off screen, a hitch) is dropped, not caught up
static const Int CONSTRAINT_ITERATIONS = 8;
static const Int ROTATION_ITERATIONS = 6;
static const Real GRAVITY = 200.0f;								///< units/s^2; the Ranger stands about 12 units tall
static const Real DAMPING = 0.02f;								///< share of the velocity lost a step
static const Real GROUND_FRICTION = 0.5f;					///< share of the sideways step a particle on the ground loses
static const Real PARTICLE_RADIUS = 0.7f;					///< how far a bone's pivot stays above the ground
static const Real MIN_BONE_LENGTH = 0.3f;					///< shorter than this to its parent, a bone rides on it
static const Real LONG_CARRIED = 2.5f;						///< a carried thing this long or longer (a rifle, a launcher) is drawn level
static const Real CARRIED_CLEARANCE = 0.5f;				///< how far its bones stay above the ground
static const Real CARRIED_WIDTH = 5.0f;						///< a bone further than this off a carried thing's length is carried on its own
static const Real STOWED_DISTANCE = 20.0f;				///< a carried thing this far from every particle at death is put away, not carried
static const Real CARRIED_DROP_REACH = 2.0f;			///< its hand this far over where it lies or closer, it is let down onto the ground
static const Real LEVEL_HEADING_MIN = 0.25f;			///< the level of its length in the hand under which the hand no longer turns it
static const Real OBSTACLE_REACH = 30.0f;						///< a vehicle or building this far past its own size from the body at death can be fallen into
static const Real LEVEL_MS = 600.0f;							///< how long it takes to come level from its tilt at death, in sync milliseconds
static const Real STIFF_MIN = 0.92f;
static const Real STIFF_MAX = 1.04f;
static const Real JOINT_MIN_ANGLE_DEG = 35.0f;
static const Real AIRBORNE_HEIGHT = 2.0f;					///< the drawable this far over the drawn ground is falling on the logic's account
static const Real BLAST_SPEED = 70.0f;						///< units/s at the blast's middle
static const Real BLAST_LIFT = 4.0f;							///< the blast centre taken this far lower, so the push lifts
static const Real BLAST_REACH_PAD = 10.0f;
static const Real BLAST_MIN_FALLOFF = 0.25f;
static const Real BULLET_SPEED = 30.0f;						///< units/s at the top of the body, nothing at the hips
static const Real IDLE_PUSH_SPEED = 8.0f;					///< a death with no hit to go by still tips over
static const Real JITTER_SPEED = 3.0f;
static const UnsignedInt IMPACT_MAX_AGE_MS = 1500;
static const Real SLEEP_SPEED = 0.5f;							///< units/s; slower than this everywhere for SLEEP_STEPS and it stops
static const Int SLEEP_STEPS = 30;
static const Real SLEEP_MAX_STRETCH = 0.05f;				///< and no constraint further out than this
static const Int MAX_HELD_AWAKE = 4;							///< times a still body is opened up before it sleeps as it is

static Int s_activeRagdolls = 0;

/// the RAGDOLL log line's counters: what the ragdolls cost the render, written every REPORT_MS of sync time
static const UnsignedInt REPORT_MS = 5000;
static UnsignedInt s_reportStart = 0;
static Int s_updates = 0;
static Int s_steps = 0;
static Int s_maxLive = 0;
static double s_updateUs = 0.0;

//-------------------------------------------------------------------------------------------------
static Bool ragdollSwitch( void )
{
	static const Bool on = findEarlyCommandLineOption( L"-ragdoll" ) != NULL;
	return on;
}

//-------------------------------------------------------------------------------------------------
Bool W3DRagdoll::wants( const ThingTemplate *tmpl )
{
	return tmpl != NULL && tmpl->isKindOf( KINDOF_INFANTRY );
}

//-------------------------------------------------------------------------------------------------
Bool W3DRagdoll::enabled( void )
{
	return ragdollSwitch() || ( TheGlobalData->m_ragdoll && !TheGlobalData->m_classicGraphics );
}

//-------------------------------------------------------------------------------------------------
/** The RAGDOLL line for a skeleton build refused, once per template a run. */
static void logRefusal( const char *templateName, RenderObjClass *robj, const char *why )
{
	static std::set<std::string> s_logged;
	if( !s_logged.insert( templateName ).second )
		return;
	const HTreeClass *tree = robj->Get_HTree();
	DEBUG_LOG(( "RAGDOLL: %s keeps its death animation: %s (model %s, skeleton %s, %d pivots)\n",
		templateName, why, robj->Get_Name(), tree ? tree->Get_Name() : "none", tree ? tree->Num_Pivots() : 0 ));
}

//-------------------------------------------------------------------------------------------------
static Matrix3D orthoInverse( const Matrix3D &m )
{
	Matrix3D inv;
	m.Get_Orthogonal_Inverse( inv );
	return inv;
}

//-------------------------------------------------------------------------------------------------
/// a b; this build has no Matrix3D operator* (ALLOW_TEMPORARIES is off)
static Matrix3D mul( const Matrix3D &a, const Matrix3D &b )
{
	Matrix3D r;
	Matrix3D::Multiply( a, b, &r );
	return r;
}

//-------------------------------------------------------------------------------------------------
/** The rotation q that best carries the rest offsets onto the current ones, A = sum of p q^T. */
static void extractRotation( const Matrix3x3 &A, Quaternion &q )
{
	for( Int iter = 0; iter < ROTATION_ITERATIONS; ++iter )
	{
		const Matrix3x3 R( q );
		Vector3 omega( 0.0f, 0.0f, 0.0f );
		Real dot = 0.0f;
		for( Int c = 0; c < 3; ++c )
		{
			const Vector3 r( R[ 0 ][ c ], R[ 1 ][ c ], R[ 2 ][ c ] );
			const Vector3 a( A[ 0 ][ c ], A[ 1 ][ c ], A[ 2 ][ c ] );
			Vector3 cross;
			Vector3::Cross_Product( r, a, &cross );
			omega += cross;
			dot += Vector3::Dot_Product( r, a );
		}
		omega /= ( (Real)fabs( dot ) + 1.0e-9f );
		const Real w = omega.Length();
		if( w < 1.0e-9f )
			break;
		const Real s = sinf( 0.5f * w ) / w;
		q = Quaternion( omega.X * s, omega.Y * s, omega.Z * s, cosf( 0.5f * w ) ) * q;
		q.Normalize();
	}
}

//-------------------------------------------------------------------------------------------------
static Real groundUnder( Real x, Real y, Real fallback )
{
	return TheTerrainRenderObject ? TheTerrainRenderObject->getDrawHeight( x, y ) : fallback;
}

//-------------------------------------------------------------------------------------------------
W3DRagdoll::W3DRagdoll() :
	m_robj( NULL ),
	m_name( "?" ),
	m_gun( -1 ),
	m_gunHandTilt( 0.0f ),
	m_numBones( 0 ),
	m_numConstraints( 0 ),
	m_numObstacles( 0 ),
	m_pushes( 0 ),
	m_trunkQ( true ),
	m_lastPoseMs( 0.0f ),
	m_lastRoot( 0.0f, 0.0f, 0.0f ),
	m_lastSync( 0 ),
	m_startSync( 0 ),
	m_accumMs( 0.0f ),
	m_height0( 0.0f ),
	m_stillSteps( 0 ),
	m_heldAwake( 0 ),
	m_asleep( FALSE )
{
}

//-------------------------------------------------------------------------------------------------
W3DRagdoll::~W3DRagdoll()
{
	// the render object may already be gone with the drawable, so its bones are left captured
	--s_activeRagdolls;
}

//-------------------------------------------------------------------------------------------------
W3DRagdoll *W3DRagdoll::create( RenderObjClass *robj, const RagdollImpact &impact, const ThingTemplate *tmpl )
{
	if( robj == NULL || s_activeRagdolls >= MAX_ACTIVE_RAGDOLLS )
		return NULL;
	const char *templateName = tmpl ? tmpl->getName().str() : "?";
	W3DRagdoll *doll = new W3DRagdoll;
	++s_activeRagdolls;
	const char *refusal = doll->build( robj, impact, templateName );
	if( refusal != NULL )
	{
		logRefusal( templateName, robj, refusal );
		delete doll;
		return NULL;
	}
	return doll;
}

//-------------------------------------------------------------------------------------------------
void W3DRagdoll::addConstraint( Int a, Int b, Real minD, Real maxD )
{
	if( m_numConstraints >= MAX_CONSTRAINTS )
		return;
	Constraint &c = m_constraint[ m_numConstraints++ ];
	c.a = a;
	c.b = b;
	c.minD = minD;
	c.maxD = maxD;
}

//-------------------------------------------------------------------------------------------------
const char *W3DRagdoll::build( RenderObjClass *robj, const RagdollImpact &impact, const char *templateName )
{
	const HTreeClass *tree = robj->Get_HTree();
	if( tree == NULL )
		return "no skeleton";
	const Int n = tree->Num_Pivots();
	if( n < 3 )
		return "too few pivots";
	if( n > MAX_BONES )
		return "more pivots than it holds";
	if( robj->Get_Num_Bones() != n )
		return "the model's bones are not its skeleton's";
	// a scaled drawable puts the scale in the root, and the captures below assume rigid matrices
	const Matrix3D &rootTm = robj->Get_Transform();
	if( fabs( rootTm.Get_X_Vector().Length() - 1.0f ) > 0.01f )
		return "drawn scaled";

	m_robj = robj;
	m_numBones = n;

	Matrix3D baseWorld[ MAX_BONES ];	// the base pose from an identity root
	Matrix3D w0[ MAX_BONES ];					// the pose now, world space
	Int descendants[ MAX_BONES ];
	for( Int i = 0; i < n; ++i )
	{
		tree->Simple_Evaluate_Pivot( i, Matrix3D( true ), &baseWorld[ i ] );
		w0[ i ] = robj->Get_Bone_Transform( i );
		m_bone[ i ].parent = ( i == 0 ) ? -1 : tree->Get_Parent_Index( i );
		if( m_bone[ i ].parent >= i )
			return "a bone before its parent";		// Base_Update poses in index order, parents first; anything else is not a skeleton this reads
		descendants[ i ] = 0;
	}
	for( Int i = n - 1; i > 0; --i )
		descendants[ m_bone[ i ].parent ] += descendants[ i ] + 1;

	// kinematic root chain, then the body under it
	Real length[ MAX_BONES ];
	Int bodyRoot = -1;
	for( Int i = 0; i < n; ++i )
	{
		Bone &b = m_bone[ i ];
		length[ i ] = ( i == 0 ) ? 0.0f : ( w0[ i ].Get_Translation() - w0[ b.parent ].Get_Translation() ).Length();
		b.role = ( i == 0 || ( m_bone[ b.parent ].role == ROLE_KINEMATIC && length[ i ] < MIN_BONE_LENGTH ) ) ? ROLE_KINEMATIC : ROLE_PARTICLE;
		if( b.role != ROLE_KINEMATIC && m_bone[ b.parent ].role == ROLE_KINEMATIC
				&& ( bodyRoot < 0 || descendants[ i ] > descendants[ bodyRoot ] ) )
			bodyRoot = i;
	}
	if( bodyRoot < 0 )
		return "no body under the root";

	Bool inBody[ MAX_BONES ];
	for( Int i = 0; i < n; ++i )
		inBody[ i ] = ( i == bodyRoot ) || ( i > 0 && inBody[ m_bone[ i ].parent ] );

	// what hangs off the mover outside the body is carried: each thing (a bone off the mover and all
	// under it) by the particle nearest any of its bones at death, the gun by a hand, a pack by the back
	Int carried[ MAX_BONES ];
	Int numParticles = 0;
	for( Int i = 1; i < n; ++i )
	{
		Bone &b = m_bone[ i ];
		carried[ i ] = -1;
		b.carrier = -1;
		if( b.role == ROLE_KINEMATIC )
			continue;
		if( inBody[ i ] && ( i == bodyRoot || length[ i ] >= MIN_BONE_LENGTH ) )
		{
			b.role = ROLE_PARTICLE;
			++numParticles;
		}
		else
		{
			b.role = ROLE_FOLLOW;
			b.anchor = b.parent;
			if( !inBody[ i ] )
				carried[ i ] = ( m_bone[ b.parent ].role == ROLE_KINEMATIC ) ? i : carried[ b.parent ];
		}
	}
	if( numParticles < 3 )
		return "fewer than three bones to swing";

	// The rig's own bones are named "<model> <part>" (the biped's "AIPFDR L HAND"); what an artist added
	// to the body is named without a space (PARA_MAN, the ghillie's DUM_CAMO flaps).  Such a bone hangs
	// loose on its parent alone: tied rigidly to its siblings it gave the torso a back the ground
	// pressed flat (squashed chests, constraints left 0.05 to 0.26 out) and stood the flaps up.
	Bool attach[ MAX_BONES ];
	Int numFrame = 0;
	for( Int i = 0; i < n; ++i )
	{
		attach[ i ] = inBody[ i ] && i != bodyRoot && strchr( tree->Get_Bone_Name( i ), ' ' ) == NULL;
		if( m_bone[ i ].role == ROLE_PARTICLE && !attach[ i ] )
			++numFrame;
	}
	if( numFrame < 3 )
		for( Int i = 0; i < n; ++i )
			attach[ i ] = FALSE;
	// A particle's parent in the simulation is its nearest particle ancestor: the body bone sits on a
	// zero length PELVIS, and read as "the parent" it left the body bone tied to nothing, so a buggy's
	// tail could hold the hips up while the rest of the soldier lay on the ground.
	Int depth[ MAX_BONES ];
	for( Int i = 0; i < n; ++i )
	{
		Bone &b = m_bone[ i ];
		b.simParent = -1;
		depth[ i ] = 0;
		if( b.role != ROLE_PARTICLE || i == bodyRoot )
			continue;
		Int p = b.parent;
		while( p > 0 && m_bone[ p ].role == ROLE_FOLLOW && inBody[ p ] )
			p = m_bone[ p ].parent;
		if( p > 0 && m_bone[ p ].role == ROLE_PARTICLE )
		{
			b.simParent = p;
			depth[ i ] = depth[ p ] + 1;
			length[ i ] = ( w0[ i ].Get_Translation() - w0[ p ].Get_Translation() ).Length();
		}
	}
	Int frameChildren[ MAX_BONES ];
	for( Int i = 0; i < n; ++i )
		frameChildren[ i ] = 0;
	for( Int i = 1; i < n; ++i )
		if( m_bone[ i ].role == ROLE_PARTICLE && !attach[ i ] && m_bone[ i ].simParent >= 0 )
			++frameChildren[ m_bone[ i ].simParent ];
	// the arms: the deepest fork of the trunk (the neck) and what hangs two or more bones below it (upper
	// arm, forearm, hand; not the head)
	Int shoulders = -1;
	for( Int i = 1; i < n; ++i )
		if( m_bone[ i ].role == ROLE_PARTICLE && !attach[ i ] && frameChildren[ i ] >= 2 && ( shoulders < 0 || depth[ i ] > depth[ shoulders ] ) )
			shoulders = i;
	Bool arm[ MAX_BONES ];
	Int numArm = 0;
	for( Int i = 0; i < n; ++i )
	{
		arm[ i ] = FALSE;
		if( shoulders < 0 || m_bone[ i ].role != ROLE_PARTICLE || attach[ i ] || depth[ i ] < depth[ shoulders ] + 2 )
			continue;
		Int up = i;
		while( up >= 0 && depth[ up ] > depth[ shoulders ] )
			up = m_bone[ up ].simParent;
		arm[ i ] = up == shoulders;
		numArm += arm[ i ] ? 1 : 0;
	}

	// a bone of a carried thing far off its first bone's length is something hanging from it (the
	// Pathfinder's rifle holds a ghillie flap ten units below it): it rides along, but the thing is
	// laid on the ground by its own length, and the flap only kept out of the ground
	m_bone[ 0 ].offAxis = FALSE;
	for( Int k = 1; k < n; ++k )
	{
		m_bone[ k ].offAxis = FALSE;
		if( carried[ k ] < 0 || carried[ k ] == k )
			continue;
		const Int r = carried[ k ];
		const Vector3 local = Matrix3x3( w0[ r ] ).Transpose() * ( w0[ k ].Get_Translation() - w0[ r ].Get_Translation() );
		m_bone[ k ].offAxis = local.Y * local.Y + local.Z * local.Z > CARRIED_WIDTH * CARRIED_WIDTH;
	}

	char carriedLog[ 640 ] = "";
	m_gun = -1;
	for( Int r = 1; r < n; ++r )
	{
		if( carried[ r ] != r )
			continue;
		Bone &br = m_bone[ r ];
		const Vector3 at = w0[ r ].Get_Translation();
		const Matrix3x3 rot0( w0[ r ] );
		Real extent = 0.0f;
		for( Int k = r; k < n; ++k )
			if( carried[ k ] == r && !m_bone[ k ].offAxis )	// its length runs along its first bone's X, as the muzzle does
				extent = WWMath::Max( extent, (Real)fabs( ( rot0.Transpose() * ( w0[ k ].Get_Translation() - at ) ).X ) );
		// a long thing (a rifle, a launcher) is drawn level, held where the hand held it: riding the hand's
		// turn, a launcher shouldered by a soldier lying on his back points at the sky
		Bool level = extent >= LONG_CARRIED;
		// held by the particle nearest any of its bones at death; a long one by an arm
		Real nearest = 1.0e9f;
		for( Int k = r; k < n; ++k )
		{
			if( carried[ k ] != r || m_bone[ k ].offAxis )
				continue;
			const Vector3 xk = w0[ k ].Get_Translation();
			for( Int p = 1; p < n; ++p )
				if( m_bone[ p ].role == ROLE_PARTICLE && !attach[ p ] && ( !level || numArm == 0 || arm[ p ] )
						&& ( xk - w0[ p ].Get_Translation() ).Length() < nearest )
				{
					nearest = ( xk - w0[ p ].Get_Translation() ).Length();
					br.anchor = p;
				}
		}
		// a thing put away out of sight (the Angry Mob's unused AK 2000 units off, the Hacker's case 400
		// under the ground) stays exactly where the death left it: carried, it swung into view
		const Bool stowed = nearest > STOWED_DISTANCE;
		if( stowed )
		{
			br.anchor = 0;
			level = FALSE;
			for( Int k = r; k < n; ++k )
				if( carried[ k ] == r )
					m_bone[ k ].carrier = STOWED;
		}
		if( level )
		{
			const Vector3 axis = rot0.Get_X_Vector();
			br.axisLocal.Set( 1.0f, 0.0f, 0.0f );
			br.grip = rot0.Transpose() * ( w0[ br.anchor ].Get_Translation() - at );
			Vector3 flat( axis.X, axis.Y, 0.0f );
			br.levelAxis.Set( 0.0f, 0.0f, 1.0f );
			br.levelAngle = 0.0f;
			if( flat.Length() > 1.0e-3f )
			{
				flat.Normalize();
				Vector3 turn;
				Vector3::Cross_Product( axis, flat, &turn );
				const Real s = turn.Length();
				if( s > 1.0e-6f )
				{
					br.levelAxis = turn / s;
					br.levelAngle = atan2f( s, Vector3::Dot_Product( axis, flat ) );
				}
			}
			else
				flat.Set( 1.0f, 0.0f, 0.0f );
			br.yaw0 = atan2f( flat.Y, flat.X );
			br.yaw = br.yaw0;
			for( Int k = r; k < n; ++k )
				if( carried[ k ] == r )
					m_bone[ k ].carrier = r;
			if( m_gun < 0 )
				m_gun = r;
		}
		const size_t used = strlen( carriedLog );
		snprintf( carriedLog + used, sizeof( carriedLog ) - used, "%s%s %.1f long on %s%s", used ? ", " : "",
			tree->Get_Bone_Name( r ), extent, stowed ? "nothing, stowed" : tree->Get_Bone_Name( br.anchor ), level ? " level" : "" );
	}

	for( Int i = 0; i < n; ++i )
	{
		Bone &b = m_bone[ i ];
		b.q = Quaternion( true );
		b.numNeighbors = 0;
		b.trunk = FALSE;
		b.rot0 = Matrix3x3( w0[ i ] );
		b.x = w0[ i ].Get_Translation();
		b.x0 = b.x;
		b.restLength = ( i == 0 ) ? 0.0f : ( b.x - w0[ b.parent ].Get_Translation() ).Length();
		b.xPrev = b.x;
		if( b.role == ROLE_KINEMATIC )
			b.followLocal = baseWorld[ i ];	// for a kinematic bone, its base pose under the root
		else
		{
			// Base_i = baseWorld[parent]^-1 baseWorld[i], so its inverse is baseWorld[i]^-1 baseWorld[parent]
			b.baseLocalInv = mul( orthoInverse( baseWorld[ i ] ), baseWorld[ b.parent ] );
			if( b.role == ROLE_FOLLOW )
				b.followLocal = mul( orthoInverse( w0[ b.anchor ] ), w0[ i ] );
		}
	}

	// neighbours for the rotation solve, and the constraints
	const Real cosMin = cosf( JOINT_MIN_ANGLE_DEG * 3.14159265f / 180.0f );
	for( Int j = 1; j < n; ++j )
	{
		Bone &bj = m_bone[ j ];
		if( bj.role != ROLE_PARTICLE )
			continue;
		const Int p = bj.simParent;
		const Bool parentIsParticle = p >= 0;
		if( parentIsParticle )
		{
			bj.neighbor[ bj.numNeighbors++ ] = p;
			addConstraint( j, p, length[ j ], length[ j ] );
		}
		if( attach[ j ] )
			continue;	// loose: its parent alone holds it and turns it
		Int children[ MAX_BONES ];
		Int numChildren = 0;
		for( Int c = j + 1; c < n; ++c )
			if( m_bone[ c ].simParent == j && !attach[ c ] )
			{
				children[ numChildren++ ] = c;
				if( bj.numNeighbors < MAX_NEIGHBORS )
					bj.neighbor[ bj.numNeighbors++ ] = c;
			}
		// siblings, once a pair
		for( Int s = j + 1; s < n; ++s )
			if( p >= 0 && m_bone[ s ].simParent == p && !attach[ s ] )
			{
				const Real d = ( bj.x - m_bone[ s ].x ).Length();
				addConstraint( j, s, d, d );
			}
		if( !parentIsParticle )
			continue;
		// the trunk is stiff: a bone that forks (the spine at the hips, the neck at the shoulders), and one
		// whose only child forks (the chest under the neck)
		const Bool stiff = numChildren >= 2 || ( numChildren == 1 && frameChildren[ children[ 0 ] ] >= 2 );
		for( Int k = 0; k < numChildren; ++k )
		{
			const Int c = children[ k ];
			if( stiff )
			{
				m_bone[ c ].trunk = m_bone[ p ].trunk = bj.trunk = TRUE;
				const Real d = ( m_bone[ c ].x - m_bone[ p ].x ).Length();
				addConstraint( c, p, d * STIFF_MIN, d * STIFF_MAX );
				for( Int s = 1; s < n; ++s )
					if( s != j && m_bone[ s ].simParent == p && !attach[ s ] )
					{
						m_bone[ s ].trunk = TRUE;
						const Real ds = ( m_bone[ c ].x - m_bone[ s ].x ).Length();
						addConstraint( c, s, ds * STIFF_MIN, ds * STIFF_MAX );
					}
			}
			else
			{
				const Real a = length[ j ];
				const Real b = length[ c ];
				// a joint already folded tighter at death (a launcher's arm) starts there, not snapped open
				const Real dMin = (Real)sqrt( WWMath::Max( 0.0f, a * a + b * b - 2.0f * a * b * cosMin ) );
				addConstraint( c, p, WWMath::Min( dMin, ( m_bone[ c ].x - m_bone[ p ].x ).Length() ), a + b );
			}
		}
	}
	// a leaf turns with its parent's neighbours, or a hand, a foot and the head would only swing
	for( Int j = 1; j < n; ++j )
	{
		Bone &bj = m_bone[ j ];
		if( bj.role != ROLE_PARTICLE || bj.numNeighbors != 1 || attach[ j ] )
			continue;
		if( bj.simParent < 0 )
			continue;
		const Bone &bp = m_bone[ bj.simParent ];
		for( Int k = 0; k < bp.numNeighbors && bj.numNeighbors < MAX_NEIGHBORS; ++k )
			if( bp.neighbor[ k ] != j )
				bj.neighbor[ bj.numNeighbors++ ] = bp.neighbor[ k ];
	}
	for( Int j = 1; j < n; ++j )
	{
		Bone &bj = m_bone[ j ];
		for( Int k = 0; k < bj.numNeighbors; ++k )
			bj.rest[ k ] = m_bone[ bj.neighbor[ k ] ].x - bj.x;
	}

	// the push, as a velocity a particle starts with
	const Real dt = STEP_MS / 1000.0f;
	const Real pelvisZ = m_bone[ bodyRoot ].x.Z;
	Real topZ = pelvisZ;
	for( Int i = 1; i < n; ++i )
		if( m_bone[ i ].role == ROLE_PARTICLE && m_bone[ i ].x.Z > topZ )
			topZ = m_bone[ i ].x.Z;
	const Real span = WWMath::Max( topZ - pelvisZ, 1.0f );

	const Bool fresh = impact.valid && WW3D::Get_Sync_Time() - impact.syncTime <= IMPACT_MAX_AGE_MS;
	const Vector3 impactPos( impact.pos.x, impact.pos.y, impact.pos.z );
	Vector3 shotDir( 0.0f, 0.0f, 0.0f );
	if( fresh && impact.radius <= 0.0f )
	{
		const Vector3 from = impact.hasSource ? Vector3( impact.source.x, impact.source.y, impact.source.z ) : impactPos;
		shotDir = impact.hasSource ? impactPos - from : m_bone[ bodyRoot ].x - impactPos;
		shotDir.Z = 0.0f;
	}
	if( !fresh || ( impact.radius <= 0.0f && shotDir.Length2() < 1.0e-6f ) )
	{
		const Real angle = GameClientRandomValueReal( 0.0f, 6.2831853f );
		shotDir.Set( cosf( angle ), sinf( angle ), 0.0f );
	}
	if( shotDir.Length2() > 0.0f )
		shotDir.Normalize();
	const Real shotSpeed = fresh ? BULLET_SPEED : IDLE_PUSH_SPEED;

	for( Int i = 1; i < n; ++i )
	{
		Bone &b = m_bone[ i ];
		if( b.role != ROLE_PARTICLE )
			continue;
		Vector3 v( GameClientRandomValueReal( -JITTER_SPEED, JITTER_SPEED ),
							 GameClientRandomValueReal( -JITTER_SPEED, JITTER_SPEED ),
							 GameClientRandomValueReal( -JITTER_SPEED, JITTER_SPEED ) );
		if( fresh && impact.radius > 0.0f )
		{
			Vector3 away = b.x - impactPos;
			const Real dist = away.Length();
			away.Z += BLAST_LIFT;
			away.Normalize();
			const Real falloff = WWMath::Clamp( 1.0f - dist / ( impact.radius + BLAST_REACH_PAD ), BLAST_MIN_FALLOFF, 1.0f );
			v += away * ( BLAST_SPEED * falloff );
		}
		else
		{
			const Real h = WWMath::Clamp( ( b.x.Z - pelvisZ ) / span, 0.0f, 1.0f );
			v += shotDir * ( shotSpeed * h );
		}
		b.xPrev = b.x - v * dt;
	}

	// a foot planted on a bulge of the drawn ground starts under it; the whole body is lifted so no
	// particle starts below the ground, its shape kept
	Real lowest = 0.0f;
	for( Int i = 1; i < n; ++i )
		if( m_bone[ i ].role == ROLE_PARTICLE )
			lowest = WWMath::Min( lowest, m_bone[ i ].x.Z - groundUnder( m_bone[ i ].x.X, m_bone[ i ].x.Y, m_bone[ i ].x.Z ) );
	if( lowest < 0.0f )
		for( Int i = 1; i < n; ++i )
			if( m_bone[ i ].role == ROLE_PARTICLE )
			{
				m_bone[ i ].x.Z -= lowest;
				m_bone[ i ].xPrev.Z -= lowest;
			}

	const Vector3 rootPos = rootTm.Get_Translation();
	m_lastRoot = rootPos;
	m_height0 = WWMath::Min( 0.0f, rootPos.Z - groundUnder( rootPos.X, rootPos.Y, rootPos.Z ) );
	m_lastSync = WW3D::Get_Sync_Time();
	m_startSync = m_lastSync;

	// the animation stops here: base pose, every bone the ragdoll moves captured
	if( robj->Peek_Animation() != NULL )
		robj->Set_Animation();
	for( Int i = 1; i < n; ++i )
		if( m_bone[ i ].role != ROLE_KINEMATIC )
			robj->Capture_Bone( i );
	m_name = templateName;
	const std::chrono::steady_clock::time_point gatherStart = std::chrono::steady_clock::now();
	gatherObstacles( rootPos );
	const double gatherUs = std::chrono::duration<double, std::micro>( std::chrono::steady_clock::now() - gatherStart ).count();
	pose( robj, 0.0f );
	DEBUG_LOG(( "RAGDOLL: start %s, skeleton %s, %d bones, %d particles (%d loose), %d constraints, body %s, push %s, lifted %.2f, %d vehicles and buildings near (%.1f us), carried: %s\n",
		templateName, tree->Get_Name(), n, numParticles, numParticles - numFrame, m_numConstraints, tree->Get_Bone_Name( bodyRoot ),
		!fresh ? "none" : impact.radius > 0.0f ? "blast" : "shot", -lowest, m_numObstacles, gatherUs, carriedLog[ 0 ] ? carriedLog : "nothing" ));
	logPose( robj, "start", 0.0f );
	return NULL;
}

//-------------------------------------------------------------------------------------------------
void W3DRagdoll::step( Real dt, Bool gravity, Real sinkOffset )
{
	const Real drop = gravity ? GRAVITY * dt * dt : 0.0f;
	Real floorZ[ MAX_BONES ];
	for( Int i = 1; i < m_numBones; ++i )
	{
		Bone &b = m_bone[ i ];
		if( b.role != ROLE_PARTICLE )
			continue;
		const Vector3 v = ( b.x - b.xPrev ) * ( 1.0f - DAMPING );
		b.xPrev = b.x;
		b.x += v;
		b.x.Z -= drop;
		// ponytail: the floor is read once a step at the predicted spot; per iteration if a slope shows it
		floorZ[ i ] = groundUnder( b.x.X, b.x.Y, b.x.Z ) + sinkOffset + PARTICLE_RADIUS;
	}

	for( Int iter = 0; iter < CONSTRAINT_ITERATIONS; ++iter )
	{
		for( Int k = 0; k < m_numConstraints; ++k )
		{
			const Constraint &c = m_constraint[ k ];
			Vector3 &xa = m_bone[ c.a ].x;
			Vector3 &xb = m_bone[ c.b ].x;
			const Vector3 delta = xb - xa;
			const Real d = delta.Length();
			if( d < 1.0e-6f )
				continue;
			const Real target = WWMath::Clamp( d, c.minD, c.maxD );
			if( target == d )
				continue;
			const Vector3 corr = delta * ( 0.5f * ( d - target ) / d );
			xa += corr;
			xb -= corr;
		}
		for( Int i = 1; i < m_numBones; ++i )
		{
			if( m_bone[ i ].role != ROLE_PARTICLE )
				continue;
			if( m_numObstacles > 0 )
				collide( m_bone[ i ].x );
			if( m_bone[ i ].x.Z < floorZ[ i ] )
				m_bone[ i ].x.Z = floorZ[ i ];
		}
	}

	Real maxMove = 0.0f;
	for( Int i = 1; i < m_numBones; ++i )
	{
		Bone &b = m_bone[ i ];
		if( b.role != ROLE_PARTICLE )
			continue;
		if( b.x.Z <= floorZ[ i ] + 0.01f )
		{
			b.xPrev.X += ( b.x.X - b.xPrev.X ) * GROUND_FRICTION;
			b.xPrev.Y += ( b.x.Y - b.xPrev.Y ) * GROUND_FRICTION;
		}
		maxMove = WWMath::Max( maxMove, ( b.x - b.xPrev ).Length() );
	}
	m_stillSteps = ( gravity && maxMove < SLEEP_SPEED * dt ) ? m_stillSteps + 1 : 0;
	if( m_stillSteps < SLEEP_STEPS )
		return;
	Int worst;
	const Real stretch = worstStretch( &worst );
	if( stretch <= SLEEP_MAX_STRETCH || m_heldAwake >= MAX_HELD_AWAKE )
	{
		m_asleep = TRUE;
		return;
	}
	// Still but wrong: a body that landed flat is pressed into a plane its own trunk does not fit in,
	// and every correction then runs along the ground, where it cannot help.  The trunk is put back in
	// its shape at death, turned the way that best fits where it lies, lifted clear of the ground, and
	// left to settle again from there.
	const Constraint &w = m_constraint[ worst ];
	DEBUG_LOG(( "RAGDOLL: %s held awake, constraints %.3f out at worst, %.2f and %.2f over the floor\n", m_name, stretch,
		m_bone[ w.a ].x.Z - floorZ[ w.a ], m_bone[ w.b ].x.Z - floorZ[ w.b ] ));
	++m_heldAwake;
	m_stillSteps = 0;
	Vector3 centre( 0.0f, 0.0f, 0.0f );
	Vector3 centre0( 0.0f, 0.0f, 0.0f );
	Int count = 0;
	for( Int i = 1; i < m_numBones; ++i )
		if( m_bone[ i ].role == ROLE_PARTICLE && m_bone[ i ].trunk )
		{
			if( count == 0 )
				m_trunkQ = m_bone[ i ].q;	// the warm start: how the trunk's first bone has turned
			centre += m_bone[ i ].x;
			centre0 += m_bone[ i ].x0;
			++count;
		}
	if( count < 3 )
		return;
	centre /= (Real)count;
	centre0 /= (Real)count;
	Matrix3x3 A( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
	for( Int i = 1; i < m_numBones; ++i )
		if( m_bone[ i ].role == ROLE_PARTICLE && m_bone[ i ].trunk )
		{
			const Vector3 p = m_bone[ i ].x - centre;
			const Vector3 q = m_bone[ i ].x0 - centre0;
			for( Int r = 0; r < 3; ++r )
				for( Int c = 0; c < 3; ++c )
					A[ r ][ c ] += p[ r ] * q[ c ];
		}
	for( Int pass = 0; pass < 4; ++pass )
		extractRotation( A, m_trunkQ );
	const Matrix3x3 R( m_trunkQ );
	Real under = 0.0f;
	for( Int i = 1; i < m_numBones; ++i )
	{
		Bone &b = m_bone[ i ];
		if( b.role != ROLE_PARTICLE || !b.trunk )
			continue;
		const Vector3 move = centre + R * ( b.x0 - centre0 ) - b.x;
		b.x += move;
		b.xPrev += move;
		under = WWMath::Max( under, floorZ[ i ] - b.x.Z );
	}
	if( under > 0.0f )
		for( Int i = 1; i < m_numBones; ++i )
			if( m_bone[ i ].role == ROLE_PARTICLE && m_bone[ i ].trunk )
			{
				m_bone[ i ].x.Z += under;
				m_bone[ i ].xPrev.Z += under;
			}
}

//-------------------------------------------------------------------------------------------------
/** The client's own drawables only, read and never written, so the logic never sees any of it. Taken
	once at death: a body lives seconds, and what it falls into is what stood there. */
void W3DRagdoll::gatherObstacles( const Vector3 &at )
{
	m_numObstacles = 0;
	if( TheGameClient == NULL )
		return;
	for( Drawable *draw = TheGameClient->firstDrawable(); draw != NULL && m_numObstacles < MAX_OBSTACLES; draw = draw->getNextDrawable() )
	{
		const ThingTemplate *tmpl = draw->getTemplate();
		if( tmpl == NULL || !( tmpl->isKindOf( KINDOF_VEHICLE ) || tmpl->isKindOf( KINDOF_STRUCTURE ) ) || draw->isDrawableEffectivelyHidden() )
			continue;
		const GeometryInfo &geom = tmpl->getTemplateGeometryInfo();
		const Coord3D *pos = draw->getPosition();
		const Real dx = pos->x - at.X;
		const Real dy = pos->y - at.Y;
		const Real reach = OBSTACLE_REACH + geom.getBoundingCircleRadius();
		if( dx * dx + dy * dy > reach * reach )
			continue;
		Obstacle &o = m_obstacle[ m_numObstacles++ ];
		o.x = pos->x;
		o.y = pos->y;
		o.z = pos->z;
		o.cosA = cosf( draw->getOrientation() );
		o.sinA = sinf( draw->getOrientation() );
		o.box = geom.getGeomType() == GEOMETRY_BOX;
		o.major = geom.getMajorRadius();
		o.minor = o.box ? geom.getMinorRadius() : o.major;
		o.top = pos->z + geom.getMaxHeightAbovePosition();
	}
}

//-------------------------------------------------------------------------------------------------
void W3DRagdoll::collide( Vector3 &x )
{
	for( Int k = 0; k < m_numObstacles; ++k )
	{
		const Obstacle &o = m_obstacle[ k ];
		if( x.Z > o.top + PARTICLE_RADIUS || x.Z < o.z - PARTICLE_RADIUS )
			continue;
		const Real wx = x.X - o.x;
		const Real wy = x.Y - o.y;
		// out by the shortest way: over the top, or out of the side
		const Real up = o.top + PARTICLE_RADIUS - x.Z;
		if( o.box )
		{
			const Real lx = wx * o.cosA + wy * o.sinA;
			const Real ly = -wx * o.sinA + wy * o.cosA;
			const Real outX = o.major + PARTICLE_RADIUS - (Real)fabs( lx );
			const Real outY = o.minor + PARTICLE_RADIUS - (Real)fabs( ly );
			if( outX <= 0.0f || outY <= 0.0f )
				continue;
			++m_pushes;
			if( up <= outX && up <= outY )
				x.Z += up;
			else if( outX <= outY )
			{
				const Real s = lx < 0.0f ? -outX : outX;
				x.X += s * o.cosA;
				x.Y += s * o.sinA;
			}
			else
			{
				const Real s = ly < 0.0f ? -outY : outY;
				x.X -= s * o.sinA;
				x.Y += s * o.cosA;
			}
		}
		else
		{
			const Real d = (Real)sqrt( wx * wx + wy * wy );
			const Real out = o.major + PARTICLE_RADIUS - d;
			if( out <= 0.0f )
				continue;
			++m_pushes;
			if( up <= out || d < 1.0e-4f )
				x.Z += up;
			else
			{
				x.X += wx / d * out;
				x.Y += wy / d * out;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
Real W3DRagdoll::worstStretch( Int *which ) const
{
	Real stretch = 0.0f;
	*which = 0;
	for( Int k = 0; k < m_numConstraints; ++k )
	{
		const Constraint &c = m_constraint[ k ];
		const Real d = ( m_bone[ c.a ].x - m_bone[ c.b ].x ).Length();
		const Real out = WWMath::Max( c.minD - d, d - c.maxD );
		if( out > stretch )
		{
			stretch = out;
			*which = k;
		}
	}
	return stretch;
}

//-------------------------------------------------------------------------------------------------
void W3DRagdoll::logPose( RenderObjClass *robj, const char *when, Real sinkOffset )
{
	Real worst = 0.0f;
	Real top = -1.0e9f;
	Real low = 1.0e9f;
	Int highest = 1;
	for( Int i = 1; i < m_numBones; ++i )
	{
		const Bone &b = m_bone[ i ];
		if( b.role != ROLE_PARTICLE )
			continue;
		worst = WWMath::Max( worst, ( robj->Get_Bone_Transform( i ).Get_Translation() - b.x ).Length() );
		const Real over = b.x.Z - ( groundUnder( b.x.X, b.x.Y, b.x.Z ) + sinkOffset );
		if( over > top )
		{
			top = over;
			highest = i;
		}
		low = WWMath::Min( low, over );
	}
	Int worstConstraint;
	const Real stretch = worstStretch( &worstConstraint );
	const HTreeClass *tree = robj->Get_HTree();

	// what the render object draws, read back from it after the capture: every bone the ragdoll moves
	// against its length to its parent at death (a bone left behind stretches the skin between them),
	// and how low and high they sit
	Real drawnStretch = 0.0f;
	Int drawnWorst = 1;
	Real drawnLow = 1.0e9f;
	Real drawnTop = -1.0e9f;
	for( Int i = 1; i < m_numBones; ++i )
	{
		const Bone &b = m_bone[ i ];
		if( b.role == ROLE_KINEMATIC || b.carrier == STOWED )
			continue;
		const Vector3 at = robj->Get_Bone_Transform( i ).Get_Translation();
		const Real over = at.Z - ( groundUnder( at.X, at.Y, at.Z ) + sinkOffset );
		drawnLow = WWMath::Min( drawnLow, over );
		drawnTop = WWMath::Max( drawnTop, over );
		if( m_bone[ b.parent ].role == ROLE_KINEMATIC || b.offAxis )
			continue;
		const Real off =(Real)fabs( ( at - robj->Get_Bone_Transform( b.parent ).Get_Translation() ).Length() - b.restLength );
		if( off > drawnStretch )
		{
			drawnStretch = off;
			drawnWorst = i;
		}
	}
	char gun[ 256 ] = "";
	if( m_gun >= 0 )
	{
		const Bone &g = m_bone[ m_gun ];
		const Matrix3D &gt = robj->Get_Bone_Transform( m_gun );
		const Vector3 drawnAxis = Matrix3x3( gt ) * g.axisLocal;
		Vector3 grip;
		Matrix3D::Transform_Vector( gt, g.grip, &grip );
		const Vector3 hand = robj->Get_Bone_Transform( g.anchor ).Get_Translation();
		Real gunLow = 1.0e9f;
		for( Int k = m_gun; k < m_numBones; ++k )
			if( m_bone[ k ].carrier == m_gun && !m_bone[ k ].offAxis )
			{
				const Vector3 at = robj->Get_Bone_Transform( k ).Get_Translation();
				gunLow = WWMath::Min( gunLow, at.Z - ( groundUnder( at.X, at.Y, at.Z ) + sinkOffset ) );
			}
		snprintf( gun, sizeof( gun ), ", %s %.0f deg off level in the hand, drawn %.0f deg, lowest bone %.2f over the ground, grip %.2f from %s",
			tree->Get_Bone_Name( m_gun ), m_gunHandTilt, (Real)asin( WWMath::Clamp( (Real)fabs( drawnAxis.Z ), 0.0f, 1.0f ) ) * 57.29578f,
			gunLow, ( grip - hand ).Length(), tree->Get_Bone_Name( g.anchor ) );
	}
	DEBUG_LOG(( "RAGDOLL: %s %s: particles %.2f to %.2f (%s) over the ground, constraints %.3f out at worst (%s to %s); drawn bones %.3f off their particles, %.2f to %.2f over the ground, lengths %.3f off at worst (%s); %d pushes out of vehicles and buildings%s\n",
		m_name, when, low, top, tree->Get_Bone_Name( highest ), stretch, tree->Get_Bone_Name( m_constraint[ worstConstraint ].a ),
		tree->Get_Bone_Name( m_constraint[ worstConstraint ].b ), worst, drawnLow, drawnTop, drawnStretch, tree->Get_Bone_Name( drawnWorst ),
		m_pushes, gun ));
}

//-------------------------------------------------------------------------------------------------
void W3DRagdoll::pose( RenderObjClass *robj, Real sinkOffset )
{
	Matrix3D world[ MAX_BONES ];
	world[ 0 ] = robj->Get_Transform();
	for( Int i = 1; i < m_numBones; ++i )
	{
		Bone &b = m_bone[ i ];
		if( b.role == ROLE_KINEMATIC )
			world[ i ] = mul( world[ 0 ], b.followLocal );
		else if( b.role == ROLE_PARTICLE )
		{
			if( !m_asleep )
			{
				Matrix3x3 A( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
				for( Int k = 0; k < b.numNeighbors; ++k )
				{
					const Vector3 p = m_bone[ b.neighbor[ k ] ].x - b.x;
					const Vector3 &q = b.rest[ k ];
					for( Int r = 0; r < 3; ++r )
						for( Int c = 0; c < 3; ++c )
							A[ r ][ c ] += p[ r ] * q[ c ];
				}
				extractRotation( A, b.q );
			}
			world[ i ] = Matrix3D( Matrix3x3( b.q ) * b.rot0, b.x );
		}
	}
	for( Int i = 1; i < m_numBones; ++i )
	{
		Bone &b = m_bone[ i ];
		if( b.role != ROLE_FOLLOW )
			continue;
		world[ i ] = mul( world[ b.anchor ], b.followLocal );
		if( b.carrier != i )
			continue;
		// a long carried thing: level, turned to the heading the hand gives it (kept while the hand holds
		// it near upright), held at its grip
		const Vector3 carriedAxis = Matrix3x3( world[ i ] ) * b.axisLocal;
		if( carriedAxis.X * carriedAxis.X + carriedAxis.Y * carriedAxis.Y > LEVEL_HEADING_MIN * LEVEL_HEADING_MIN )
			b.yaw = atan2f( carriedAxis.Y, carriedAxis.X );
		const Real laid = b.levelAngle * WWMath::Min( 1.0f, (Real)( m_lastSync - m_startSync ) / LEVEL_MS );
		const Matrix3x3 rot = Matrix3x3( Vector3( 0.0f, 0.0f, 1.0f ), sinf( b.yaw - b.yaw0 ), cosf( b.yaw - b.yaw0 ) )
			* Matrix3x3( b.levelAxis, sinf( laid ), cosf( laid ) ) * b.rot0;
		world[ i ] = Matrix3D( rot, world[ b.anchor ].Get_Translation() - rot * b.grip );
		if( i == m_gun )
			m_gunHandTilt = (Real)asin( WWMath::Clamp( (Real)fabs( carriedAxis.Z ), 0.0f, 1.0f ) ) * 57.29578f;
	}
	// and lifted whole off the ground where it would go into it, or let down onto it as the hand that
	// holds it comes down to lie there
	for( Int r = 1; r < m_numBones; ++r )
	{
		if( m_bone[ r ].carrier != r )
			continue;
		Real low = 1.0e9f;
		for( Int k = r; k < m_numBones; ++k )
			if( m_bone[ k ].carrier == r && !m_bone[ k ].offAxis )
			{
				const Vector3 xk = world[ k ].Get_Translation();
				low = WWMath::Min( low, xk.Z - ( groundUnder( xk.X, xk.Y, xk.Z ) + sinkOffset ) );
			}
		const Vector3 &held = m_bone[ m_bone[ r ].anchor ].x;
		const Real handUp = held.Z - ( groundUnder( held.X, held.Y, held.Z ) + sinkOffset + PARTICLE_RADIUS );
		const Real down = WWMath::Clamp( 1.0f - handUp / CARRIED_DROP_REACH, 0.0f, 1.0f );
		const Real shift = low < CARRIED_CLEARANCE ? CARRIED_CLEARANCE - low : ( CARRIED_CLEARANCE - low ) * down;
		for( Int k = r; k < m_numBones; ++k )
		{
			if( m_bone[ k ].carrier != r )
				continue;
			world[ k ][ 2 ][ 3 ] += shift;
			if( m_bone[ k ].offAxis )
			{
				const Vector3 xk = world[ k ].Get_Translation();
				world[ k ][ 2 ][ 3 ] = WWMath::Max( xk.Z, groundUnder( xk.X, xk.Y, xk.Z ) + sinkOffset + CARRIED_CLEARANCE );
			}
		}
	}

	// HTree: T_i = T_parent Base_i Cap_i, so Cap_i = Base_i^-1 T_parent^-1 W_i
	for( Int i = 1; i < m_numBones; ++i )
	{
		const Bone &b = m_bone[ i ];
		if( b.role == ROLE_KINEMATIC )
			continue;
		const Matrix3D cap = mul( b.baseLocalInv, mul( orthoInverse( world[ b.parent ] ), world[ i ] ) );
		robj->Control_Bone( i, cap );
	}
}

//-------------------------------------------------------------------------------------------------
Bool W3DRagdoll::update( RenderObjClass *robj )
{
	if( robj != m_robj || robj->Get_Num_Bones() != m_numBones )
	{
		DEBUG_LOG(( "RAGDOLL: the model changed under it, the animation takes over\n" ));
		return FALSE;
	}
	const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
	// the module sets the death clip again on every state change; the captures hold only over the base pose
	if( robj->Peek_Animation() != NULL )
		robj->Set_Animation();

	const Vector3 rootPos = robj->Get_Transform().Get_Translation();
	const Vector3 carry = rootPos - m_lastRoot;
	m_lastRoot = rootPos;
	const Real height = rootPos.Z - groundUnder( rootPos.X, rootPos.Y, rootPos.Z );
	const Bool airborne = height > AIRBORNE_HEIGHT;
	const Real sinkOffset = WWMath::Min( 0.0f, height - m_height0 );
	if( carry.Length2() > 0.0f )
	{
		for( Int i = 1; i < m_numBones; ++i )
			if( m_bone[ i ].role == ROLE_PARTICLE )
			{
				m_bone[ i ].x += carry;
				m_bone[ i ].xPrev += carry;
			}
		if( airborne || carry.X != 0.0f || carry.Y != 0.0f )
		{
			m_asleep = FALSE;
			m_stillSteps = 0;
		}
	}

	const UnsignedInt now = WW3D::Get_Sync_Time();
	m_accumMs = WWMath::Min( m_accumMs + (Real)( now - m_lastSync ), MAX_STEPS_PER_UPDATE * STEP_MS );
	m_lastSync = now;
	const Bool wasAsleep = m_asleep;
	while( m_accumMs >= STEP_MS )
	{
		if( !m_asleep )
		{
			step( STEP_MS / 1000.0f, !airborne, sinkOffset );
			++s_steps;
		}
		m_accumMs -= STEP_MS;
	}

	const Bool wasLevel = m_lastPoseMs >= LEVEL_MS;
	m_lastPoseMs = (Real)( now - m_startSync );
	pose( robj, sinkOffset );
	if( m_asleep && !wasAsleep )
	{
		char when[ 48 ];
		snprintf( when, sizeof( when ), "asleep %.2f s after death", ( now - m_startSync ) / 1000.0f );
		logPose( robj, when, sinkOffset );
	}
	else if( m_gun >= 0 && !wasLevel && m_lastPoseMs >= LEVEL_MS )
	{
		char when[ 48 ];
		snprintf( when, sizeof( when ), "%s %.2f s after death", m_asleep ? "asleep" : "awake", ( now - m_startSync ) / 1000.0f );
		logPose( robj, when, sinkOffset );
	}

	s_updateUs += std::chrono::duration<double, std::micro>( std::chrono::steady_clock::now() - started ).count();
	++s_updates;
	if( s_activeRagdolls > s_maxLive )
		s_maxLive = s_activeRagdolls;
	if( now - s_reportStart >= REPORT_MS )
	{
		DEBUG_LOG(( "RAGDOLL: %d live (%d at most), %d updates %.2f us each, %d steps, %.1f us total a second\n",
			s_activeRagdolls, s_maxLive, s_updates, s_updates ? s_updateUs / s_updates : 0.0, s_steps,
			s_updateUs * 1000.0 / (double)( now - s_reportStart ) ));
		s_reportStart = now;
		s_updates = 0;
		s_steps = 0;
		s_maxLive = s_activeRagdolls;
		s_updateUs = 0.0;
	}
	return TRUE;
}
