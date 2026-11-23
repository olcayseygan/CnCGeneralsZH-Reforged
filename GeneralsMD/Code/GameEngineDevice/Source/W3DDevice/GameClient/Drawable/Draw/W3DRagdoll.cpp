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
// near zero length one, which rides on its parent.  Whatever else hangs off the mover (the Ranger's
// BONE-GUN and its MUZZLE01) rides on the right hand, so the rifle stays in it.
//
// The constraints, all lengths taken from the pose at the moment of death:
//  - every particle to its parent particle, fixed;
//  - particle siblings to each other, fixed: the hips with the spine, the shoulders with the head;
//  - at a bone with two or more particle children (the spine) the joint is stiff: each child to the
//    bone's parent and to the bone's siblings within STIFF_MIN to STIFF_MAX of its length;
//  - at a bone with one particle child (shoulder, hip) the joint folds no tighter than
//    JOINT_MIN_ANGLE_DEG.
// The Ranger's rig has no elbow and no knee: upper arm to hand and thigh to foot are one rigid link
// each, and his limbs swing as sticks.  Nothing stops a hip bending backward or a shoulder twisting.
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
#include "Common/ThingTemplate.h"
#include "GameClient/ClientRandomValue.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DRagdoll.h"
#include "WW3D2/htree.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/ww3d.h"

#include <chrono>
#include <math.h>
#include <stdio.h>
#include <string.h>

/// live ragdolls at once; a death past it plays its animation
static const Int MAX_ACTIVE_RAGDOLLS = 24;
/// ponytail: suffix match on the template name, so the generals' AirF_, Lazr_ and SupW_ copies come
/// along; a per-template INI flag when more than a handful of soldiers join
static const char *const RAGDOLL_TEMPLATES[] = { "AmericaInfantryRanger" };

static const Real STEP_MS = 1000.0f / 60.0f;			///< fixed simulation step, in W3D sync milliseconds
static const Int MAX_STEPS_PER_UPDATE = 4;				///< a long gap (off screen, a hitch) is dropped, not caught up
static const Int CONSTRAINT_ITERATIONS = 8;
static const Int ROTATION_ITERATIONS = 6;
static const Real GRAVITY = 200.0f;								///< units/s^2; the Ranger stands about 12 units tall
static const Real DAMPING = 0.02f;								///< share of the velocity lost a step
static const Real GROUND_FRICTION = 0.5f;					///< share of the sideways step a particle on the ground loses
static const Real PARTICLE_RADIUS = 0.7f;					///< how far a bone's pivot stays above the ground
static const Real MIN_BONE_LENGTH = 0.3f;					///< shorter than this to its parent, a bone rides on it
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
static Bool endsWithNoCase( const char *name, const char *suffix )
{
	const size_t n = strlen( name );
	const size_t m = strlen( suffix );
	return n >= m && strcasecmp( name + n - m, suffix ) == 0;
}

//-------------------------------------------------------------------------------------------------
Bool W3DRagdoll::wants( const ThingTemplate *tmpl )
{
	if( tmpl == NULL || !ragdollSwitch() )
		return FALSE;
	const char *name = tmpl->getName().str();
	const size_t n = strlen( name );
	for( size_t i = 0; i < sizeof( RAGDOLL_TEMPLATES ) / sizeof( RAGDOLL_TEMPLATES[ 0 ] ); ++i )
	{
		const size_t m = strlen( RAGDOLL_TEMPLATES[ i ] );
		if( n >= m && strcmp( name + n - m, RAGDOLL_TEMPLATES[ i ] ) == 0 && ( n == m || name[ n - m - 1 ] == '_' ) )
			return TRUE;
	}
	return FALSE;
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
	m_numBones( 0 ),
	m_numConstraints( 0 ),
	m_lastRoot( 0.0f, 0.0f, 0.0f ),
	m_lastSync( 0 ),
	m_startSync( 0 ),
	m_accumMs( 0.0f ),
	m_height0( 0.0f ),
	m_stillSteps( 0 ),
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
W3DRagdoll *W3DRagdoll::create( RenderObjClass *robj, const RagdollImpact &impact )
{
	if( robj == NULL || s_activeRagdolls >= MAX_ACTIVE_RAGDOLLS )
		return NULL;
	W3DRagdoll *doll = new W3DRagdoll;
	++s_activeRagdolls;
	if( !doll->build( robj, impact ) )
	{
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
Bool W3DRagdoll::build( RenderObjClass *robj, const RagdollImpact &impact )
{
	const HTreeClass *tree = robj->Get_HTree();
	if( tree == NULL )
		return FALSE;
	const Int n = tree->Num_Pivots();
	if( n < 3 || n > MAX_BONES || robj->Get_Num_Bones() != n )
		return FALSE;
	// a scaled drawable puts the scale in the root, and the captures below assume rigid matrices
	const Matrix3D &rootTm = robj->Get_Transform();
	if( fabs( rootTm.Get_X_Vector().Length() - 1.0f ) > 0.01f )
		return FALSE;

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
			return FALSE;		// Base_Update poses in index order, parents first; anything else is not a skeleton this reads
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
		return FALSE;

	Bool inBody[ MAX_BONES ];
	Int hand = bodyRoot;
	for( Int i = 0; i < n; ++i )
	{
		inBody[ i ] = ( i == bodyRoot ) || ( i > 0 && inBody[ m_bone[ i ].parent ] );
		if( inBody[ i ] && hand == bodyRoot && endsWithNoCase( tree->Get_Bone_Name( i ), " R HAND" ) )
			hand = i;
	}

	Int numParticles = 0;
	for( Int i = 1; i < n; ++i )
	{
		Bone &b = m_bone[ i ];
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
			b.anchor = ( m_bone[ b.parent ].role == ROLE_KINEMATIC ) ? hand : b.parent;
			if( m_bone[ b.anchor ].role == ROLE_FOLLOW && b.anchor > i )
				b.anchor = bodyRoot;
		}
	}
	if( numParticles < 3 )
		return FALSE;

	for( Int i = 0; i < n; ++i )
	{
		Bone &b = m_bone[ i ];
		b.q = Quaternion( true );
		b.numNeighbors = 0;
		b.rot0 = Matrix3x3( w0[ i ] );
		b.x = w0[ i ].Get_Translation();
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
		const Int p = bj.parent;
		const Bool parentIsParticle = m_bone[ p ].role == ROLE_PARTICLE;
		if( parentIsParticle )
		{
			bj.neighbor[ bj.numNeighbors++ ] = p;
			addConstraint( j, p, length[ j ], length[ j ] );
		}
		Int children[ MAX_BONES ];
		Int numChildren = 0;
		for( Int c = j + 1; c < n; ++c )
			if( m_bone[ c ].parent == j && m_bone[ c ].role == ROLE_PARTICLE )
			{
				children[ numChildren++ ] = c;
				if( bj.numNeighbors < MAX_NEIGHBORS )
					bj.neighbor[ bj.numNeighbors++ ] = c;
			}
		// siblings, once a pair
		for( Int s = j + 1; s < n; ++s )
			if( m_bone[ s ].parent == p && m_bone[ s ].role == ROLE_PARTICLE )
			{
				const Real d = ( bj.x - m_bone[ s ].x ).Length();
				addConstraint( j, s, d, d );
			}
		if( !parentIsParticle )
			continue;
		for( Int k = 0; k < numChildren; ++k )
		{
			const Int c = children[ k ];
			if( numChildren >= 2 )
			{
				const Real d = ( m_bone[ c ].x - m_bone[ p ].x ).Length();
				addConstraint( c, p, d * STIFF_MIN, d * STIFF_MAX );
				for( Int s = 1; s < n; ++s )
					if( s != j && m_bone[ s ].parent == p && m_bone[ s ].role == ROLE_PARTICLE )
					{
						const Real ds = ( m_bone[ c ].x - m_bone[ s ].x ).Length();
						addConstraint( c, s, ds * STIFF_MIN, ds * STIFF_MAX );
					}
			}
			else
			{
				const Real a = length[ j ];
				const Real b = length[ c ];
				const Real dMin = (Real)sqrt( WWMath::Max( 0.0f, a * a + b * b - 2.0f * a * b * cosMin ) );
				addConstraint( c, p, dMin, a + b );
			}
		}
	}
	// a leaf turns with its parent's neighbours, or a hand, a foot and the head would only swing
	for( Int j = 1; j < n; ++j )
	{
		Bone &bj = m_bone[ j ];
		if( bj.role != ROLE_PARTICLE || bj.numNeighbors != 1 )
			continue;
		const Bone &bp = m_bone[ bj.parent ];
		if( bp.role != ROLE_PARTICLE )
			continue;
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

	const Vector3 rootPos = rootTm.Get_Translation();
	m_lastRoot = rootPos;
	m_height0 = WWMath::Min( 0.0f, rootPos.Z - groundUnder( rootPos.X, rootPos.Y, rootPos.Z ) );
	m_lastSync = WW3D::Get_Sync_Time();

	// the animation stops here: base pose, every bone the ragdoll moves captured
	if( robj->Peek_Animation() != NULL )
		robj->Set_Animation();
	for( Int i = 1; i < n; ++i )
		if( m_bone[ i ].role != ROLE_KINEMATIC )
			robj->Capture_Bone( i );
	pose( robj );
	DEBUG_LOG(( "RAGDOLL: start %s, %d bones, %d particles, %d constraints, body %s, gun on %s, push %s\n",
		tree->Get_Name(), n, numParticles, m_numConstraints, tree->Get_Bone_Name( bodyRoot ), tree->Get_Bone_Name( hand ),
		!fresh ? "none" : impact.radius > 0.0f ? "blast" : "shot" ));
	logPose( robj, "start", 0.0f );
	m_startSync = m_lastSync;
	return TRUE;
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
			if( m_bone[ i ].role == ROLE_PARTICLE && m_bone[ i ].x.Z < floorZ[ i ] )
				m_bone[ i ].x.Z = floorZ[ i ];
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
	if( m_stillSteps >= SLEEP_STEPS )
		m_asleep = TRUE;
}

//-------------------------------------------------------------------------------------------------
void W3DRagdoll::logPose( RenderObjClass *robj, const char *when, Real sinkOffset )
{
	Real worst = 0.0f;
	Real top = -1.0e9f;
	Real low = 1.0e9f;
	for( Int i = 1; i < m_numBones; ++i )
	{
		const Bone &b = m_bone[ i ];
		if( b.role != ROLE_PARTICLE )
			continue;
		worst = WWMath::Max( worst, ( robj->Get_Bone_Transform( i ).Get_Translation() - b.x ).Length() );
		const Real over = b.x.Z - ( groundUnder( b.x.X, b.x.Y, b.x.Z ) + sinkOffset );
		top = WWMath::Max( top, over );
		low = WWMath::Min( low, over );
	}
	Real stretch = 0.0f;
	for( Int k = 0; k < m_numConstraints; ++k )
	{
		const Constraint &c = m_constraint[ k ];
		const Real d = ( m_bone[ c.a ].x - m_bone[ c.b ].x ).Length();
		stretch = WWMath::Max( stretch, WWMath::Max( c.minD - d, d - c.maxD ) );
	}
	DEBUG_LOG(( "RAGDOLL: %s: bones %.3f off their particles at worst, particles %.2f to %.2f over the ground, constraints %.3f out at worst\n",
		when, worst, low, top, stretch ));
}

//-------------------------------------------------------------------------------------------------
void W3DRagdoll::pose( RenderObjClass *robj )
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
		if( m_bone[ i ].role == ROLE_FOLLOW )
			world[ i ] = mul( world[ m_bone[ i ].anchor ], m_bone[ i ].followLocal );

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

	pose( robj );
	if( m_asleep && !wasAsleep )
	{
		char when[ 48 ];
		snprintf( when, sizeof( when ), "asleep %.2f s after death", ( now - m_startSync ) / 1000.0f );
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
