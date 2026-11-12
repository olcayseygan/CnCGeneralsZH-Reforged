// The ground a defence can hit, worked out with the logic's own tests and handed to the renderer
// as a signed distance field (ReachOverlay.h).  Client side: it reads the logic and writes nothing
// back, and it draws no logic random number.  ReachOverlay_prove is the check that the picture
// does not lie; a scenario's reachproof line runs it.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/ReachOverlay.h"

#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"

#include <chrono>
#include <map>
#include <random>

//-------------------------------------------------------------------------------------------------
// What is shown, and why it can be trusted.
//
// A ground cell is lit for a defence only if every test the logic puts an idle defence through
// before it fires at a ground target there passes, asked of the same functions with the defence's
// own Object:
//
//   - getNextMoodTarget's search: the defence's adjusted vision range (TheAI's, with the mood and
//     owner factors), stretched by the high ground the way PartitionFilterElevatedReach stretches it;
//   - its line of sight, for a defence the INI marks ATTACK_NEEDS_LINE_OF_SIGHT: the terrain line
//     from the top of the defence to the top of the target (isClearLineOfSightTerrain), and the
//     structure walk of Pathfinder::isAttackViewBlockedByObstacle;
//   - the fog: the target's cell has to be clear for the defence's player (PartitionFilterFreeOfFog);
//   - the weapon: the set's chooser (WeaponSet::chooseBestWeaponForTarget) may pick any slot whose
//     AutoChooseSources lets an AI-picked target choose it and that hits ground, and the turret fires
//     only once the chosen one is in range (TurretAIAimTurretState), so EVERY such slot has to have
//     the point inside Weapon::isWithinAttackRange.  A Patriot's ground missile qualifies; its 450
//     assist missile (AutoChooseSources NONE, fired only when another Patriot asks) and its anti-air
//     missile do not, so the Patriot shows the ground it hits on its own.  Air targets are not shown;
//   - a pitch band (the Fire Base's howitzer): only the ground within the height Weapon::isWithinTargetPitch
//     waves through whatever the band.
//
// The target is the worst one of its kind, test by test: a range is measured to its centre (a wider
// target is reached sooner), a minimum range to the edge of the widest ground unit the game can build,
// and a sight line to the top of the lowest one.  A cell under a building or a bridge deck, where no
// ground unit stands, is left out.
//
// The tests are taken on a grid, a quarter of a pathfind cell apart, and a grid point is lit only if
// it and its eight neighbours all pass.  The renderer paints where the field, taken bilinearly, is
// above nought, which needs one lit corner (shadeField smooths the field but holds every unlit point
// below nought), so every painted spot lies in a grid cell whose four corners all passed.
// ReachOverlay_prove measures what is left: points between the grid points.
//-------------------------------------------------------------------------------------------------

static const Real LIVE_STEP = PATHFIND_CELL_SIZE_F * 0.25f;		///< a standing defence's grid
static const Real GHOST_STEP = PATHFIND_CELL_SIZE_F * 0.5f;		///< the placement cursor's, worked out as it moves
static const Real DISTANCE_LIMIT = 16.0f;											///< the field is clamped to this either side
static const UnsignedInt FOG_REFRESH_FRAMES = LOGICFRAMES_PER_SECOND / 4;	///< client frames between two looks at the fog, a quarter second
static const UnsignedInt FORGET_FRAMES = 5 * LOGICFRAMES_PER_SECOND;			///< a field not wanted for five seconds is dropped
static const Real BUILD_BUDGET_MS = 4.0f;											///< grids worked out in one frame, past the first

//-------------------------------------------------------------------------------------------------
/** The worst ground target the game can field: the lowest top and the widest body of every unit a
	* player can build that drives or walks. */
//-------------------------------------------------------------------------------------------------
struct GroundProbe
{
	Real lowestTop;
	Real widestRadius;
};

static GroundProbe theProbe = { 0.0f, 0.0f };
static const ThingTemplate *theProbeFrom = NULL;

static const GroundProbe &groundProbe( void )
{
	const ThingTemplate *first = TheThingFactory ? TheThingFactory->firstTemplate() : NULL;
	if( first == theProbeFrom )
		return theProbe;
	theProbeFrom = first;

	theProbe.lowestTop = FLT_MAX;
	theProbe.widestRadius = 0.0f;
	for( const ThingTemplate *tmpl = first; tmpl; tmpl = tmpl->friend_getNextTemplate() )
	{
		if( !tmpl->isKindOf( KINDOF_VEHICLE ) && !tmpl->isKindOf( KINDOF_INFANTRY ) )
			continue;
		// a player's: it costs something.  Ships, trains and film props cost nothing
		// and something to see on the ground: an angry mob's nexus is the mob's invisible one-unit-high
		// leader, and the mob itself is what gets shot
		if( tmpl->isKindOf( KINDOF_AIRCRAFT ) || tmpl->isKindOf( KINDOF_PROJECTILE ) || tmpl->isKindOf( KINDOF_MOB_NEXUS )
				|| tmpl->getBuildable() != BSTATUS_YES || tmpl->friend_getBuildCost() <= 0 )
			continue;
		// no Geometry of its own (ThingTemplate's default, a sphere of one): it never stands on the field
		// as itself, the way a Technical comes out as one of its chassis
		const GeometryInfo &geom = tmpl->getTemplateGeometryInfo();
		if( geom.getGeomType() == GEOMETRY_SPHERE && geom.getMajorRadius() == 1.0f && geom.getMaxHeightAbovePosition() <= 1.0f )
			continue;
		theProbe.lowestTop = min( theProbe.lowestTop, geom.getMaxHeightAbovePosition() );
		theProbe.widestRadius = max( theProbe.widestRadius, geom.getBoundingCircleRadius() );
	}
	if( theProbe.lowestTop == FLT_MAX )
		theProbe.lowestTop = 0.0f;
	theProbe.lowestTop = max( theProbe.lowestTop, 0.0f );
	DEBUG_LOG(( "REACHOVERLAY: ground targets judged by a top %.2f high and a body %.2f wide\n",
		theProbe.lowestTop, theProbe.widestRadius ));
	return theProbe;
}

//-------------------------------------------------------------------------------------------------
/** A defence as the tests below see it: a standing one through its Object, or one on the
	* placement cursor through its template. */
//-------------------------------------------------------------------------------------------------
struct ReachGun
{
	const Weapon *live;						///< the standing defence's weapon, NULL for the cursor's
	const WeaponTemplate *tmpl;
};

struct ReachShooter
{
	const Object *obj;						///< NULL on the placement cursor
	const ThingTemplate *tmpl;
	Coord3D center;
	Real radius;
	Real eyeZ;
	Real acquireRange;						///< getNextMoodTarget's rangeToFindWithin before the high ground
	Bool lineOfSight;
	Int playerIndex;
	const Player *owner;
	std::vector< ReachGun > guns;
	Real reachBound;							///< nothing further than this from the centre passes
	ObjectID standingIn;					///< the cursor's twin in the proof stands in this building's cells
};

/// a slot the weapon set's chooser can pick for a ground target the defence found itself
static Bool picksGround( UnsignedInt sources, Int antiMask )
{
	if( ( antiMask & WEAPON_ANTI_GROUND ) == 0 )
		return FALSE;
	return ( sources & ( 1 << CMD_FROM_AI ) ) != 0 || ( sources & CMD_DEFAULT_SWITCH_WEAPON ) != 0;
}

/// a weapon that refuses targets outside a pitch band, the Fire Base's howitzer (-15 to 15 degrees)
static Bool pitchLimited( const WeaponTemplate *wt )
{
	return wt->getMinTargetPitch() > -PI || wt->getMaxTargetPitch() < PI;
}

/// Weapon::isWithinTargetPitch lets anything through whose height is within this of the shooter's,
/// whatever the band; past it the answer turns on the target's own shape, so the overlay stops there
static const Real PITCH_FREE_HEIGHT = 10.0f;

static void boundShooter( ReachShooter &s, const WeaponBonus *bonusless )
{
	Real bound = s.acquireRange + Weapon_elevationRangeBonus( s.acquireRange, FLT_MAX );
	for( size_t g = 0; g < s.guns.size(); ++g )
	{
		const Real range = s.guns[ g ].live ? s.guns[ g ].live->getAttackRange( s.obj ) : s.guns[ g ].tmpl->getAttackRange( *bonusless );
		bound = min( bound, range + Weapon_elevationRangeBonus( range, FLT_MAX ) );
	}
	s.reachBound = bound + s.radius;
}

static const char *theRefusal = NULL;		///< why liveShooter last said no, for the proof's log

static Bool refuse( const char *why )
{
	theRefusal = why;
	return FALSE;
}

static Bool liveShooter( const Object *obj, ReachShooter &s )
{
	if( obj == NULL || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) )
		return refuse( "not a standing structure" );
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) )
		return refuse( "under construction or sold" );
	// unpowered, hacked, EMP'd: it fires at nothing, so it shows nothing
	if( obj->isDisabled() || !obj->isAbleToAttack() )
		return refuse( "disabled or unable to attack" );
	// a jammed defence's sight shrinks on a clock the drawing does not follow.  A Stinger Site's
	// shots come from soldiers who walk about it, and it has no gun of its own to show; a Fire Base
	// carries the same SPAWNS_ARE_THE_WEAPONS kind and its own howitzer, which is what is shown
	if( obj->getContainedBy() != NULL || obj->isSightJammed() )
		return refuse( "contained or jammed" );

	const AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL || !ai->canAutoAcquire() )
		return refuse( "no idle auto-acquire" );
	if( ai->getMoodMatrixActionAdjustment( MM_Action_Idle ) & MAA_Affect_Range_IgnoreAll )
		return refuse( "mood ignores all" );
	if( obj->testStatus( OBJECT_STATUS_STEALTHED ) && !ai->canAutoAcquireWhileStealthed() )
		return refuse( "stealthed" );

	s.guns.clear();
	if( obj->isCurWeaponLocked() )
	{
		const Weapon *w = obj->getCurrentWeapon();
		if( w && ( w->getAntiMask() & WEAPON_ANTI_GROUND ) )
		{
			ReachGun gun = { w, w->getTemplate() };
			s.guns.push_back( gun );
		}
	}
	else
	{
		for( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT; ++slot )
		{
			const Weapon *w = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
			if( w == NULL || !picksGround( obj->getWeaponInWeaponSlotCommandSourceMask( (WeaponSlotType)slot ), w->getAntiMask() ) )
				continue;
			ReachGun gun = { w, w->getTemplate() };
			s.guns.push_back( gun );
		}
	}
	if( s.guns.empty() )
		return refuse( "no gun an idle scan picks for the ground" );
	for( size_t g = 0; g < s.guns.size(); ++g )
	{
		if( s.guns[ g ].live->isContactWeapon() )
			return refuse( "contact weapon" );
	}

	s.obj = obj;
	s.tmpl = obj->getTemplate();
	s.center = *obj->getPosition();
	s.radius = obj->getGeometryInfo().getBoundingCircleRadius();
	s.eyeZ = s.center.z + obj->getGeometryInfo().getMaxHeightAbovePosition();
	s.acquireRange = TheAI->getAdjustedVisionRangeForObject( obj, AI_VISIONFACTOR_OWNERTYPE | AI_VISIONFACTOR_MOOD );
	if( s.tmpl->acquiresBeyondVision() )
		s.acquireRange = max( s.acquireRange, obj->getLargestWeaponRange() );
	s.lineOfSight = TheAI->getAiData()->m_attackUsesLineOfSight && obj->isKindOf( KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT );
	s.owner = obj->getControllingPlayer();
	s.playerIndex = s.owner ? s.owner->getPlayerIndex() : 0;
	s.standingIn = INVALID_ID;
	boundShooter( s, NULL );
	return s.acquireRange > 0.0f;
}

/// the defence on the placement cursor, as it will stand there: its template's default weapon set,
/// no bonus, the sight a human player's turret gets (the cursor is always the local player's)
static Bool ghostShooter( const ThingTemplate *tmpl, Real x, Real y, const Player *owner, ReachShooter &s )
{
	if( tmpl == NULL || owner == NULL || !tmpl->isKindOf( KINDOF_STRUCTURE ) )
		return FALSE;
	const WeaponTemplateSet *set = tmpl->findWeaponTemplateSet( WeaponSetFlags() );
	if( set == NULL )
		return FALSE;

	s.guns.clear();
	Real longest = 0.0f;
	const WeaponBonus bonusless;
	for( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT; ++slot )
	{
		const WeaponTemplate *wt = set->getNth( (WeaponSlotType)slot );
		if( wt == NULL )
			continue;
		longest = max( longest, wt->getAttackRange( bonusless ) );
		if( !picksGround( set->getNthCommandSourceMask( (WeaponSlotType)slot ), wt->getAntiMask() ) )
			continue;
		if( wt->isContactWeapon() )
			return FALSE;
		ReachGun gun = { NULL, wt };
		s.guns.push_back( gun );
	}
	if( s.guns.empty() )
		return FALSE;

	s.obj = NULL;
	s.tmpl = tmpl;
	s.center.x = x;
	s.center.y = y;
	s.center.z = TheTerrainLogic->getGroundHeight( x, y );
	s.radius = tmpl->getTemplateGeometryInfo().getBoundingCircleRadius();
	s.eyeZ = s.center.z + tmpl->getTemplateGeometryInfo().getMaxHeightAbovePosition();
	s.acquireRange = tmpl->friend_calcVisionRange() * TheAI->getAiData()->m_guardOuterModifierHuman;
	if( tmpl->acquiresBeyondVision() )
		s.acquireRange = max( s.acquireRange, longest );
	s.lineOfSight = TheAI->getAiData()->m_attackUsesLineOfSight && tmpl->isKindOf( KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT );
	s.owner = owner;
	s.playerIndex = owner->getPlayerIndex();
	s.standingIn = INVALID_ID;
	boundShooter( s, &bonusless );
	return s.acquireRange > 0.0f;
}

/// where a ground unit at (x, y) stands: on the ground, or on the water over it
static Real standHeight( Real x, Real y )
{
	Real z = TheTerrainLogic->getGroundHeight( x, y );
	Real waterZ = 0.0f;
	if( TheTerrainLogic->isUnderwater( x, y, &waterZ ) )
		z = max( z, waterZ );
	return z;
}

/// what a grid point's tests said: out of the guns' reach (a range edge, drawn as a line), passed, or
/// in reach but blocked (a cliff, a building, the sight line, the fog: an edge that fades in)
enum { REACH_OUT = 0, REACH_PASS = 1, REACH_BLOCKED = 2 };

//-------------------------------------------------------------------------------------------------
/** Every test but the fog, for the worst ground target standing at (x, y). */
//-------------------------------------------------------------------------------------------------
static UnsignedByte hitsGroundAt( const ReachShooter &s, const GroundProbe &probe, Real x, Real y )
{
	const Real dx = x - s.center.x;
	const Real dy = y - s.center.y;
	if( dx * dx + dy * dy > s.reachBound * s.reachBound )
		return REACH_OUT;

	Coord3D at;
	at.set( x, y, standHeight( x, y ) );

	const PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, &at );
	if( cell == NULL || cell->getType() == PathfindCell::CELL_OBSTACLE )
		return REACH_BLOCKED;
	if( TheTerrainLogic->findBridgeAt( &at ) != NULL )
		return REACH_BLOCKED;

	// FROM_BOUNDINGSPHERE_2D from the defence's edge to the target's middle
	Real distSqr;
	if( s.obj )
		distSqr = ThePartitionManager->getDistanceSquared( s.obj, &at, FROM_BOUNDINGSPHERE_2D );
	else
	{
		const Real d = sqrtf( dx * dx + dy * dy ) - s.radius;
		distSqr = d > 0.0f ? d * d : 0.0f;
	}

	// getNextMoodTarget's search and PartitionFilterElevatedReach
	const Real look = s.obj ? Weapon_elevatedRange( s.obj, s.acquireRange, at.z )
		: s.acquireRange + Weapon_elevationRangeBonus( s.acquireRange, s.center.z - at.z );
	if( distSqr > look * look )
		return REACH_OUT;

	// the widest target's edge is this much nearer than its middle (distCalcProc_BoundaryAndBoundary_2D)
	const Real edge = max( 0.0f, sqrtf( distSqr ) - probe.widestRadius );
	const WeaponBonus bonusless;
	for( size_t g = 0; g < s.guns.size(); ++g )
	{
		const ReachGun &gun = s.guns[ g ];
		if( gun.live )
		{
			if( !gun.live->isWithinAttackRange( s.obj, &at ) )
				return REACH_OUT;
		}
		else
		{
			const Real range = gun.tmpl->getAttackRange( bonusless );
			const Real reach = range + Weapon_elevationRangeBonus( range, s.center.z - at.z );
			const Real minimum = gun.tmpl->getMinimumAttackRange();
			if( distSqr > reach * reach || distSqr < minimum * minimum )
				return REACH_OUT;
		}
		const Real minimum = gun.tmpl->getMinimumAttackRange();
		if( minimum > 0.0f && edge * edge < minimum * minimum )
			return REACH_OUT;
		if( pitchLimited( gun.tmpl ) && fabs( at.z - s.center.z ) >= PITCH_FREE_HEIGHT )
			return REACH_BLOCKED;
	}

	if( s.lineOfSight )
	{
		Coord3D top = at;
		top.z += probe.lowestTop;
		if( s.obj )
		{
			if( !ThePartitionManager->isClearLineOfSightTerrain( s.obj, *s.obj->getPosition(), NULL, top ) )
				return REACH_BLOCKED;
			if( TheAI->pathfinder()->isAttackViewBlockedByObstacle( s.obj, *s.obj->getPosition(), NULL, at ) )
				return REACH_BLOCKED;
		}
		else
		{
			Coord3D eye = s.center;
			eye.z = s.eyeZ;
			if( !ThePartitionManager->isClearLineOfSightTerrain( NULL, eye, NULL, top ) )
				return REACH_BLOCKED;
			if( TheAI->pathfinder()->isGroundViewBlockedByObstacle( s.center, at, s.standingIn ) )
				return REACH_BLOCKED;
		}
	}
	return REACH_PASS;
}

//-------------------------------------------------------------------------------------------------
// One defence's field and the grid behind it.
//-------------------------------------------------------------------------------------------------
struct ReachEntry
{
	ReachOverlayField field;
	std::vector< UnsignedByte > geometry;		///< REACH_PASS where every test but the fog passed at this grid point
	UnsignedInt signature;
	UnsignedInt fogFrame;
	UnsignedInt lastWanted;
	Int playerIndex;
	Bool built;
	Bool regridded;		///< the grid moved or changed size since the last field, so the version must change
};

static UnsignedInt theVersion = 0;

static void layGrid( ReachOverlayField &field, const ReachShooter &s, Real step )
{
	field.step = step;
	field.originX = floorf( ( s.center.x - s.reachBound ) / step ) * step - step;
	field.originY = floorf( ( s.center.y - s.reachBound ) / step ) * step - step;
	field.width = (Int)ceilf( ( s.center.x + s.reachBound - field.originX ) / step ) + 2;
	field.height = (Int)ceilf( ( s.center.y + s.reachBound - field.originY ) / step ) + 2;
	field.distanceLimit = DISTANCE_LIMIT;
	field.color = clientPlayerColor( s.owner );
	field.opacity = 1.0f;
}

static void traceGeometry( ReachEntry &entry, const ReachShooter &s, Real step )
{
	layGrid( entry.field, s, step );
	const GroundProbe &probe = groundProbe();
	const Int w = entry.field.width;
	const Int h = entry.field.height;
	entry.geometry.assign( w * h, 0 );
	for( Int j = 0; j < h; ++j )
	{
		const Real y = entry.field.originY + j * step;
		for( Int i = 0; i < w; ++i )
			entry.geometry[ j * w + i ] = hitsGroundAt( s, probe, entry.field.originX + i * step, y );
	}

	// the bound allows for all the high ground a range can gain; cut the grid down to what passed,
	// two points of nothing round it, so the fog pass, the field and the texture stay small
	Int loI = w, hiI = -1, loJ = h, hiJ = -1;
	for( Int j = 0; j < h; ++j )
	{
		for( Int i = 0; i < w; ++i )
		{
			if( entry.geometry[ j * w + i ] != REACH_PASS )
				continue;
			loI = min( loI, i );
			hiI = max( hiI, i );
			loJ = min( loJ, j );
			hiJ = max( hiJ, j );
		}
	}
	if( hiI < 0 )
	{
		loI = loJ = 0;
		hiI = hiJ = 1;
	}
	loI = max( loI - 2, 0 );
	loJ = max( loJ - 2, 0 );
	hiI = min( hiI + 2, w - 1 );
	hiJ = min( hiJ + 2, h - 1 );
	const Int cw = hiI - loI + 1;
	const Int ch = hiJ - loJ + 1;
	std::vector< UnsignedByte > cropped( cw * ch );
	for( Int j = 0; j < ch; ++j )
		memcpy( &cropped[ j * cw ], &entry.geometry[ ( j + loJ ) * w + loI ], cw );
	entry.geometry.swap( cropped );
	entry.field.originX += loI * step;
	entry.field.originY += loJ * step;
	entry.field.width = cw;
	entry.field.height = ch;

	entry.playerIndex = s.playerIndex;
	entry.built = TRUE;
	entry.regridded = TRUE;
	entry.fogFrame = 0;
}

/// a separable Gaussian over the grid, sigma in grid steps; past the border the edge sample holds,
/// and the border is two points of nothing (traceGeometry's crop)
static void blurGrid( std::vector< Real > &v, Int w, Int h, Real sigma )
{
	const Int radius = (Int)ceilf( 2.5f * sigma );
	std::vector< Real > weight( radius + 1 );
	Real sum = 0.0f;
	for( Int r = 0; r <= radius; ++r )
	{
		weight[ r ] = expf( -0.5f * (Real)( r * r ) / ( sigma * sigma ) );
		sum += r == 0 ? weight[ r ] : 2.0f * weight[ r ];
	}
	for( Int r = 0; r <= radius; ++r )
		weight[ r ] /= sum;

	// one line of samples, stride apart, through a padded copy so the inner loop has no clamp
	std::vector< Real > line( max( w, h ) + 2 * radius );
	auto pass = [ & ]( Real *first, Int count, Int stride )
	{
		for( Int n = 0; n < count; ++n )
			line[ radius + n ] = first[ n * stride ];
		for( Int r = 0; r < radius; ++r )
		{
			line[ r ] = first[ 0 ];
			line[ radius + count + r ] = first[ ( count - 1 ) * stride ];
		}
		for( Int n = 0; n < count; ++n )
		{
			const Real *at = &line[ radius + n ];
			Real total = weight[ 0 ] * at[ 0 ];
			for( Int r = 1; r <= radius; ++r )
				total += weight[ r ] * ( at[ -r ] + at[ r ] );
			first[ n * stride ] = total;
		}
	};
	for( Int j = 0; j < h; ++j )
		pass( &v[ j * w ], w, 1 );
	for( Int i = 0; i < w; ++i )
		pass( &v[ i ], h, w );
}

static const Real SMOOTH_SIGMA = 4.0f;		///< world units the edge is smoothed over, at least a grid step
static const Real SMOOTH_MARGIN = 0.6f;		///< grid steps the smoothed edge is pulled in, past the steps' corners
static const Real UNLIT_HOLD = 0.05f;			///< grid steps an unlit point is held under nought whatever the blur says

//-------------------------------------------------------------------------------------------------
/** The fog on top of the geometry, then the erosion, then the signed distance (a chamfer walk, a
	* grid step straight and step root two across), smoothed.  Only the sign is load-bearing; the size
	* shapes the border the renderer draws.
	*
	* The lit points are the grid's own staircase, and so is a field taken straight from them: a range
	* ring drawn in steps of a grid point, a cliff's edge in steps of a pathfind cell.  The distance is
	* blurred, which lays the edge along the middle of the steps, and pulled in by a margin so it passes
	* inside their corners.  Whatever the blur did, an unlit point is then held below nought, so the
	* edge the renderer draws still lies only in grid cells with a lit corner, whose four corners all
	* passed: the area can shrink by the margin, never grow.
	*
	* The hardness is what the nearest failed point failed on, out of reach (a ring, hard) or blocked
	* (soft), blurred as the distance is so a ring meeting a cliff turns from line to fade smoothly. */
//-------------------------------------------------------------------------------------------------
static void shadeField( ReachEntry &entry )
{
	ReachOverlayField &field = entry.field;
	const Int w = field.width;
	const Int h = field.height;
	const Real step = field.step;

	// hard: what the nearest failed point failed on, 1 out of reach, 0 blocked (a point in reach that
	// the fog hides counts as blocked); gap: how far that point is, in grid steps, a city-block walk
	std::vector< UnsignedByte > seen( w * h, 0 );
	std::vector< Real > hard( w * h, 1.0f );
	std::vector< Int > gap( w * h, w + h );
	for( Int j = 0; j < h; ++j )
	{
		for( Int i = 0; i < w; ++i )
		{
			const Int k = j * w + i;
			if( entry.geometry[ k ] == REACH_PASS )
			{
				Coord3D at;
				at.set( field.originX + i * step, field.originY + j * step, 0.0f );
				seen[ k ] = ThePartitionManager->getShroudStatusForPlayer( entry.playerIndex, &at ) == CELLSHROUD_CLEAR ? 1 : 0;
			}
			if( !seen[ k ] )
			{
				gap[ k ] = 0;
				hard[ k ] = entry.geometry[ k ] == REACH_OUT ? 1.0f : 0.0f;
			}
		}
	}
	auto take = [ & ]( Int k, Int from )
	{
		if( gap[ from ] + 1 < gap[ k ] )
		{
			gap[ k ] = gap[ from ] + 1;
			hard[ k ] = hard[ from ];
		}
	};
	for( Int j = 0; j < h; ++j )
		for( Int i = 0; i < w; ++i )
		{
			if( i > 0 ) take( j * w + i, j * w + i - 1 );
			if( j > 0 ) take( j * w + i, ( j - 1 ) * w + i );
		}
	for( Int j = h - 1; j >= 0; --j )
		for( Int i = w - 1; i >= 0; --i )
		{
			if( i < w - 1 ) take( j * w + i, j * w + i + 1 );
			if( j < h - 1 ) take( j * w + i, ( j + 1 ) * w + i );
		}

	// lit: it and its eight neighbours passed
	std::vector< UnsignedByte > lit( w * h, 0 );
	for( Int j = 1; j < h - 1; ++j )
	{
		for( Int i = 1; i < w - 1; ++i )
		{
			Bool all = TRUE;
			for( Int dj = -1; dj <= 1 && all; ++dj )
				for( Int di = -1; di <= 1 && all; ++di )
					all = seen[ ( j + dj ) * w + i + di ] != 0;
			lit[ j * w + i ] = all ? 1 : 0;
		}
	}

	const Real across = step * 1.41421356f;
	const Real outOfReach = DISTANCE_LIMIT + step;
	std::vector< Real > d( w * h, outOfReach );
	for( Int j = 0; j < h; ++j )
	{
		for( Int i = 0; i < w; ++i )
		{
			const UnsignedByte mine = lit[ j * w + i ];
			for( Int dj = -1; dj <= 1; ++dj )
			{
				for( Int di = -1; di <= 1; ++di )
				{
					const Int ni = i + di;
					const Int nj = j + dj;
					if( ( di == 0 && dj == 0 ) || ni < 0 || nj < 0 || ni >= w || nj >= h || lit[ nj * w + ni ] == mine )
						continue;
					d[ j * w + i ] = min( d[ j * w + i ], ( di != 0 && dj != 0 ) ? across : step );
				}
			}
		}
	}
	for( Int j = 0; j < h; ++j )
	{
		for( Int i = 0; i < w; ++i )
		{
			Real &here = d[ j * w + i ];
			if( i > 0 ) here = min( here, d[ j * w + i - 1 ] + step );
			if( j > 0 ) here = min( here, d[ ( j - 1 ) * w + i ] + step );
			if( i > 0 && j > 0 ) here = min( here, d[ ( j - 1 ) * w + i - 1 ] + across );
			if( i < w - 1 && j > 0 ) here = min( here, d[ ( j - 1 ) * w + i + 1 ] + across );
		}
	}
	for( Int j = h - 1; j >= 0; --j )
	{
		for( Int i = w - 1; i >= 0; --i )
		{
			Real &here = d[ j * w + i ];
			if( i < w - 1 ) here = min( here, d[ j * w + i + 1 ] + step );
			if( j < h - 1 ) here = min( here, d[ ( j + 1 ) * w + i ] + step );
			if( i < w - 1 && j < h - 1 ) here = min( here, d[ ( j + 1 ) * w + i + 1 ] + across );
			if( i > 0 && j < h - 1 ) here = min( here, d[ ( j + 1 ) * w + i - 1 ] + across );
		}
	}

	for( Int k = 0; k < w * h; ++k )
	{
		const Real magnitude = min( d[ k ] - 0.5f * step, DISTANCE_LIMIT );
		d[ k ] = lit[ k ] ? magnitude : -magnitude;
	}
	const Real sigma = max( SMOOTH_SIGMA / step, 1.0f );
	blurGrid( d, w, h, sigma );
	blurGrid( hard, w, h, sigma );

	// a fog refresh that changed nothing keeps the version, so the renderer keeps its texture
	Bool changed = (Int)field.distance.size() != w * h || (Int)field.hardness.size() != w * h;
	field.distance.resize( w * h );
	field.hardness.resize( w * h );
	for( Int k = 0; k < w * h; ++k )
	{
		Real value = d[ k ] - SMOOTH_MARGIN * step;
		if( !lit[ k ] )
			value = min( value, -UNLIT_HOLD * step );
		value = max( -DISTANCE_LIMIT, min( value, DISTANCE_LIMIT ) );
		const Real hardness = hard[ k ];
		changed = changed || field.distance[ k ] != value || field.hardness[ k ] != hardness;
		field.distance[ k ] = value;
		field.hardness[ k ] = hardness;
	}
	if( changed || entry.regridded )
		field.version = ++theVersion;
	entry.regridded = FALSE;
}

/// the field taken bilinearly, as the renderer takes it; outside the grid it is the limit below nought
static Real fieldAt( const ReachOverlayField &field, Real x, Real y )
{
	const Real fx = ( x - field.originX ) / field.step;
	const Real fy = ( y - field.originY ) / field.step;
	const Int i = (Int)floorf( fx );
	const Int j = (Int)floorf( fy );
	if( i < 0 || j < 0 || i + 1 >= field.width || j + 1 >= field.height )
		return -field.distanceLimit;
	const Real tx = fx - i;
	const Real ty = fy - j;
	const Real *row = &field.distance[ j * field.width + i ];
	const Real top = row[ 0 ] * ( 1.0f - tx ) + row[ 1 ] * tx;
	const Real bottom = row[ field.width ] * ( 1.0f - tx ) + row[ field.width + 1 ] * tx;
	return top * ( 1.0f - ty ) + bottom * ty;
}

//-------------------------------------------------------------------------------------------------
// What is on the map this frame, and which fields are wanted.
//-------------------------------------------------------------------------------------------------
struct StructureMark
{
	ObjectID id;
	Real x;
	Real y;
};

static std::vector< StructureMark > theStructures;
static std::map< ObjectID, ReachEntry > theEntries;
static ReachEntry theGhost;
static const ThingTemplate *theGhostTemplate = NULL;
static Coord3D theGhostAt;
static Bool theGhostShown = FALSE;
static std::vector< const ReachOverlayField * > theShown;

static UnsignedInt mix( UnsignedInt hash, UnsignedInt value )
{
	return ( hash ^ value ) * 16777619u;
}

static UnsignedInt mixReal( UnsignedInt hash, Real value )
{
	UnsignedInt bits;
	memcpy( &bits, &value, sizeof( bits ) );
	return mix( hash, bits );
}

/// what the grid depends on: the defence and its guns, and every structure standing near it
static UnsignedInt shooterSignature( const ReachShooter &s )
{
	UnsignedInt hash = 2166136261u;
	hash = mixReal( hash, s.center.x );
	hash = mixReal( hash, s.center.y );
	hash = mixReal( hash, s.center.z );
	hash = mixReal( hash, s.eyeZ );
	hash = mixReal( hash, s.acquireRange );
	hash = mix( hash, s.lineOfSight ? 1 : 0 );
	hash = mix( hash, (UnsignedInt)s.playerIndex );
	for( size_t g = 0; g < s.guns.size(); ++g )
	{
		hash = mix( hash, (UnsignedInt)(uintptr_t)s.guns[ g ].tmpl );
		if( s.guns[ g ].live )
			hash = mixReal( hash, s.guns[ g ].live->getAttackRange( s.obj ) );
	}
	const Real nearSqr = sqr( s.reachBound + PATHFIND_CELL_SIZE_F * 10.0f );
	for( size_t k = 0; k < theStructures.size(); ++k )
	{
		const StructureMark &mark = theStructures[ k ];
		if( sqr( mark.x - s.center.x ) + sqr( mark.y - s.center.y ) > nearSqr )
			continue;
		hash = mix( hash, mark.id );
		hash = mixReal( hash, mark.x );
		hash = mixReal( hash, mark.y );
	}
	return hash;
}

/// a defence whose reach the local player is shown: their own, an ally's, or any to an observer
static Bool revealedToLocal( const Object *obj )
{
	const Player *local = ThePlayerList->getLocalPlayer();
	if( local == NULL || obj == NULL )
		return FALSE;
	if( obj->getControllingPlayer() == local )
		return TRUE;
	if( !local->isPlayerObserver() && local->getRelationship( obj->getTeam() ) != ALLIES )
		return FALSE;
	return obj->getShroudedStatus( local->getPlayerIndex() ) < OBJECTSHROUD_FOGGED;
}

static void clearAll( void )
{
	theEntries.clear();
	theShown.clear();
	theGhostShown = FALSE;
	theGhostTemplate = NULL;
	theGhost.built = FALSE;
}

static Real millisecondsSince( const std::chrono::steady_clock::time_point &start )
{
	return std::chrono::duration< Real, std::milli >( std::chrono::steady_clock::now() - start ).count();
}

void ReachOverlay_update( void )
{
	if( TheGlobalData == NULL || TheGlobalData->m_headless || TheGameLogic == NULL || TheInGameUI == NULL
			|| !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || ThePlayerList == NULL
			|| ThePlayerList->getLocalPlayer() == NULL || TheAI == NULL || TheAI->pathfinder() == NULL )
	{
		clearAll();
		return;
	}

	// the tick may be running beside this frame: keep what was worked out at the last sync point
	if( LogicThread::isInsideWindow() )
		return;

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const UnsignedInt now = TheGameClient ? TheGameClient->getFrame() : 0;

	theStructures.clear();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead() )
			continue;
		StructureMark mark = { obj->getID(), obj->getPosition()->x, obj->getPosition()->y };
		theStructures.push_back( mark );
	}

	// which defences are wanted: the selected ones, or while a building is on the cursor none but
	// that one; every defence at once buries the ground in colour
	std::vector< const Object * > wanted;
	const ThingTemplate *pending = TheInGameUI->getPendingPlaceType();
	if( pending == NULL )
	{
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			const Object *obj = (*it)->getObject();
			if( revealedToLocal( obj ) )
				wanted.push_back( obj );
		}
	}

	theShown.clear();
	Bool builtOne = FALSE;

	// the cursor's first: it is what the player is looking at
	theGhostShown = FALSE;
	const Drawable *icon = TheInGameUI->getPendingPlaceDrawable();
	ReachShooter ghost;
	if( pending != NULL && icon != NULL
			&& ghostShooter( pending, icon->getPosition()->x, icon->getPosition()->y, ThePlayerList->getLocalPlayer(), ghost ) )
	{
		const Bool moved = pending != theGhostTemplate || !theGhost.built
			|| sqr( ghost.center.x - theGhostAt.x ) + sqr( ghost.center.y - theGhostAt.y ) > sqr( 0.5f * GHOST_STEP );
		if( moved )
		{
			theGhostTemplate = pending;
			theGhostAt = ghost.center;
			traceGeometry( theGhost, ghost, GHOST_STEP );
			builtOne = TRUE;
		}
		if( moved || now - theGhost.fogFrame >= FOG_REFRESH_FRAMES )
		{
			shadeField( theGhost );
			theGhost.fogFrame = now;
		}
		theGhost.field.opacity = 1.0f;
		theShown.push_back( &theGhost.field );
		theGhostShown = TRUE;
	}

	for( size_t k = 0; k < wanted.size(); ++k )
	{
		ReachShooter s;
		if( !liveShooter( wanted[ k ], s ) )
			continue;

		ReachEntry &entry = theEntries[ s.obj->getID() ];
		entry.lastWanted = now;
		const UnsignedInt signature = shooterSignature( s );
		if( !entry.built || entry.signature != signature )
		{
			// a grid that no longer matches what stands round it is not drawn until it is worked out again
			if( builtOne && millisecondsSince( start ) > BUILD_BUDGET_MS )
			{
				entry.built = FALSE;
				continue;
			}
			traceGeometry( entry, s, LIVE_STEP );
			entry.signature = signature;
			shadeField( entry );
			entry.fogFrame = now;
			builtOne = TRUE;
		}
		else if( now - entry.fogFrame >= FOG_REFRESH_FRAMES )
		{
			shadeField( entry );
			entry.fogFrame = now;
		}
		entry.field.color = clientPlayerColor( s.owner );
		theShown.push_back( &entry.field );
	}

	for( std::map< ObjectID, ReachEntry >::iterator it = theEntries.begin(); it != theEntries.end(); )
	{
		if( now - it->second.lastWanted > FORGET_FRAMES )
			theEntries.erase( it++ );
		else
			++it;
	}
}

void ReachOverlay_getFields( std::vector< const ReachOverlayField * > &out )
{
	out = theShown;
}

//-------------------------------------------------------------------------------------------------
/** The check that the picture does not lie.  For a standing defence and a probe of the enemy's, a
	* real Object: the field the defence would show, and the one its template would show on the cursor
	* at the same spot, are sampled at random points, inside, outside and near the edge; the probe is
	* put on each point and the logic is asked itself, the way an idle defence asks it: getNextMoodTarget
	* as the turret's idle scan calls it, the fog filter it applies for this owner, then the weapon the
	* chooser takes and the range and attack tests the turret's aim state makes before it fires.  A lit
	* point the logic refuses is a lie; an unlit one it accepts is ground the picture leaves out.
	*
	* Diagnostic: it moves the probe, runs the partition and sets the defence's current weapon, so only
	* a scenario calls it.  The sample points come from a generator of their own, not the logic's. */
//-------------------------------------------------------------------------------------------------
static Bool logicHits( Object *defence, Object *probe )
{
	AIUpdateInterface *ai = defence->getAI();
	if( ai == NULL || ai->getNextMoodTarget( FALSE, TRUE, FALSE, TRUE ) != probe )
		return FALSE;

	const Player *owner = defence->getControllingPlayer();
	if( owner && ( owner->getPlayerType() == PLAYER_HUMAN || owner->isForkMapComputer() || defence->getTemplate()->acquiresBeyondVision() ) )
	{
		const ObjectShroudStatus status = probe->getShroudedStatus( owner->getPlayerIndex() );
		if( status != OBJECTSHROUD_CLEAR && status != OBJECTSHROUD_PARTIAL_CLEAR )
			return FALSE;
	}

	defence->chooseBestWeaponForTarget( probe, PREFER_MOST_DAMAGE, CMD_FROM_AI );
	const Weapon *weapon = defence->getCurrentWeapon();
	if( weapon == NULL || !weapon->isWithinAttackRange( defence, probe ) )
		return FALSE;
	const CanAttackResult result = defence->getAbleToAttackSpecificObject( ATTACK_CONTINUED_TARGET, probe, CMD_FROM_AI );
	return result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING;
}

struct ProofCount
{
	Int drawn;
	Int drawnMissed;			///< lies
	Int undrawn;
	Int undrawnHit;				///< ground left out
};

static void tally( ProofCount &count, Bool drawn, Bool hit )
{
	if( drawn )
	{
		++count.drawn;
		if( !hit )
			++count.drawnMissed;
	}
	else
	{
		++count.undrawn;
		if( hit )
			++count.undrawnHit;
	}
}

Bool ReachOverlay_prove( Object *defence, Object *probe, Int samples )
{
	ReachShooter live;
	if( defence == NULL || probe == NULL || !liveShooter( defence, live ) )
	{
		DEBUG_LOG(( "HEADLESS REACHPROOF: no field for %s: %s\n", defence ? defence->getTemplate()->getName().str() : "(none)",
			theRefusal ? theRefusal : "no probe" ));
		return FALSE;
	}
	ReachShooter ghost;
	const Bool hasGhost = ghostShooter( defence->getTemplate(), live.center.x, live.center.y, live.owner, ghost );
	ghost.standingIn = defence->getID();

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	ReachEntry liveEntry;
	traceGeometry( liveEntry, live, LIVE_STEP );
	shadeField( liveEntry );
	const Real liveMS = millisecondsSince( start );
	ReachEntry ghostEntry;
	if( hasGhost )
	{
		traceGeometry( ghostEntry, ghost, GHOST_STEP );
		ghostEntry.playerIndex = live.playerIndex;
		shadeField( ghostEntry );
	}

	Int litPoints = 0;
	for( size_t k = 0; k < liveEntry.field.distance.size(); ++k )
		litPoints += liveEntry.field.distance[ k ] > 0.0f ? 1 : 0;

	const Coord3D home = *probe->getPosition();
	// mt19937's raw output is the same on every platform; its distributions are not
	std::mt19937 pick( (UnsignedInt)defence->getID() * 2654435761u + (UnsignedInt)samples );
	const Real span = live.reachBound + 10.0f;
	auto across = [ &pick, span ]( void ) { return ( (Real)( pick() >> 8 ) * ( 1.0f / 16777216.0f ) * 2.0f - 1.0f ) * span; };

	enum { ANYWHERE, INSIDE, EDGE, KINDS };
	static const char *const kindNames[ KINDS ] = { "anywhere", "inside", "edge" };
	ProofCount liveCount[ KINDS ];
	ProofCount ghostCount[ KINDS ];
	memset( liveCount, 0, sizeof( liveCount ) );
	memset( ghostCount, 0, sizeof( ghostCount ) );
	Int unstandable = 0;
	Real lowest = FLT_MAX;
	Real highest = -FLT_MAX;

	for( Int kind = 0; kind < KINDS; ++kind )
	{
		Int taken = 0;
		for( Int tries = 0; taken < samples && tries < samples * 200; ++tries )
		{
			const Real x = live.center.x + across();
			const Real y = live.center.y + across();
			const Real d = fieldAt( liveEntry.field, x, y );
			if( kind == INSIDE && d <= 0.0f )
				continue;
			if( kind == EDGE && fabs( d ) > 6.0f )
				continue;

			Coord3D at;
			at.set( x, y, standHeight( x, y ) );
			const PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, &at );
			if( cell == NULL || cell->getType() == PathfindCell::CELL_OBSTACLE || TheTerrainLogic->findBridgeAt( &at ) != NULL )
			{
				++unstandable;
				continue;
			}
			++taken;
			lowest = min( lowest, at.z );
			highest = max( highest, at.z );

			probe->setPosition( &at );
			ThePartitionManager->update();
			const Bool hit = logicHits( defence, probe );
			tally( liveCount[ kind ], d > 0.0f, hit );
			if( hasGhost )
				tally( ghostCount[ kind ], fieldAt( ghostEntry.field, x, y ) > 0.0f, hit );
		}
	}

	probe->setPosition( &home );
	ThePartitionManager->update();

	const GroundProbe &worst = groundProbe();
	DEBUG_LOG(( "HEADLESS REACHPROOF: %s at (%.0f,%.0f,%.1f) against %s: reach bound %.1f, %d lit grid points, grid in %.1f ms, worst target top %.2f body %.2f, ground sampled from %.1f to %.1f high, %d unstandable points skipped\n",
		defence->getTemplate()->getName().str(), live.center.x, live.center.y, live.center.z,
		probe->getTemplate()->getName().str(), live.reachBound, litPoints, liveMS, worst.lowestTop, worst.widestRadius,
		lowest, highest, unstandable ));
	Int lies = 0;
	for( Int kind = 0; kind < KINDS; ++kind )
	{
		DEBUG_LOG(( "HEADLESS REACHPROOF:   %-8s live  drawn %d, %d not hit; undrawn %d, %d hit\n", kindNames[ kind ],
			liveCount[ kind ].drawn, liveCount[ kind ].drawnMissed, liveCount[ kind ].undrawn, liveCount[ kind ].undrawnHit ));
		if( hasGhost )
			DEBUG_LOG(( "HEADLESS REACHPROOF:   %-8s ghost drawn %d, %d not hit; undrawn %d, %d hit\n", kindNames[ kind ],
				ghostCount[ kind ].drawn, ghostCount[ kind ].drawnMissed, ghostCount[ kind ].undrawn, ghostCount[ kind ].undrawnHit ));
		lies += liveCount[ kind ].drawnMissed + ghostCount[ kind ].drawnMissed;
	}
	DEBUG_LOG(( "HEADLESS REACHPROOF: %s %s, %d lies\n", defence->getTemplate()->getName().str(), lies == 0 ? "PASS" : "FAIL", lies ));
	return lies == 0;
}
