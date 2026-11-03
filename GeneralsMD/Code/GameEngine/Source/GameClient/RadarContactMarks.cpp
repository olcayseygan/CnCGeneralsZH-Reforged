// The Turkiye radar's contacts on the ground (RadarContactMarks.h): per contact a red ring with a
// bright centre pip lying on the terrain and two ping ripples running out of it, and where the
// sweep before saw something, a faint thin ring fading out.  The decals are the smoke-signal marks'
// kind (InGameUI::addSignalMark), so every renderer draws them through the shared decal path.
// Client side: it reads the logic (the water, the bridges) and the drawables, and writes nothing
// back.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/RadarContactMarks.h"

#include "Common/GameEngine.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/CinemaDirector.h"
#include "GameClient/Drawable.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/Shadow.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

namespace
{
	// ponytail: a hard cap on the contacts drawn per sweep; a radar painting more than this is a
	// screen of red already, and the pool would want to grow without bound
	const size_t MAX_CONTACTS = 64;

	struct Sweep
	{
		UnsignedInt frame;
		std::vector< RadarContact > all;
	};

	Sweep s_sweeps[ 2 ];							///< [0] the last publication, [1] the one before
	Int s_publications = 0;						///< how many s_sweeps hold, 0..2

	// what RadarContactMarks_update worked out this pass, for the minimap draw to read without
	// touching the logic or a drawable while it may run beside the tick (logic thread)
	std::vector< RadarContact > s_fresh;
	std::vector< RadarContact > s_echoes;
	Real s_sweepAge = 0.0f;

	std::vector< Shadow * > s_contactDecals;	///< ReforgedRadarContact, one per fresh contact
	std::vector< Shadow * > s_ringDecals;			///< ReforgedRadarRing, two ripples per fresh contact and one per echo

	/** The player's contacts in a sweep, less any whose unit is drawn now: a red ring left under a unit
		* he can see would only say what he sees already.  It asks the drawable, which GameClient::update's
		* walk has just set for this pass and for this same shown player, so the ring goes on the very
		* frame the unit appears (and stays off through the walk's two seconds of keeping a unit that
		* slipped back into the fog drawn). */
	void pickOut( const Sweep &sweep, Int player, std::vector< RadarContact > &out )
	{
		out.clear();
		for( size_t index = 0; index < sweep.all.size() && out.size() < MAX_CONTACTS; index++ )
		{
			const RadarContact &contact = sweep.all[ index ];
			if( contact.player != player )
				continue;
			const Object *obj = TheGameLogic->findObjectByID( contact.id );
			Drawable *draw = obj != NULL ? obj->getDrawable() : NULL;
			if( draw != NULL && !draw->getFullyObscuredByShroud() )
				continue;
			out.push_back( contact );
		}
	}

	/// the shown player's two lists, refilled every pass: what is drawn changes with the pass, not the sweep
	void refreshShown( void )
	{
		const Int player = TheObserverCamera.getShroudPlayerIndex();
		s_fresh.clear();
		s_echoes.clear();
		if( s_publications > 0 )
			pickOut( s_sweeps[ 0 ], player, s_fresh );
		if( s_publications > 1 )
			pickOut( s_sweeps[ 1 ], player, s_echoes );
	}

	/// seconds since the last publication, by the logic's clock and the way into the next tick, so a
	/// ripple runs smooth at a frame rate over the logic's
	Real secondsSinceSweep( void )
	{
		if( s_publications == 0 )
			return 0.0f;
		const UnsignedInt now = TheGameLogic->getFrame();
		const UnsignedInt since = now > s_sweeps[ 0 ].frame ? now - s_sweeps[ 0 ].frame : 0;
		return ( (Real)since + GameEngine_logicTickFraction() ) / LOGICFRAMES_PER_SECOND;
	}

	/** Whether a mark at pos lies where the ground under it is drawn.  A decal without a render object
		* is laid on the terrain heightmap whatever its height, so over water it would sit on the seabed
		* and on a bridge under the deck; those contacts keep only their minimap pip. */
	Bool onOpenGround( const Coord3D &pos )
	{
		return !TheTerrainLogic->isUnderwater( pos.x, pos.y ) && TheTerrainLogic->getLayerForDestination( &pos ) == LAYER_GROUND;
	}

	Shadow *newDecal( const char *texture )
	{
		Shadow::ShadowTypeInfo decalInfo;
		decalInfo.allowUpdates = FALSE;
		decalInfo.allowWorldAlign = TRUE;		// wrapped over the terrain it lands on
		decalInfo.m_type = SHADOW_ALPHA_DECAL;
		strcpy( decalInfo.m_ShadowName, texture );
		decalInfo.m_sizeX = RadarContactLook::RingSize;
		decalInfo.m_sizeY = RadarContactLook::RingSize;
		decalInfo.m_offsetX = 0.0f;
		decalInfo.m_offsetY = 0.0f;
		Shadow *decal = TheProjectedShadowManager->addDecal( &decalInfo );
		decal->setColor( (Color)RadarContactLook::Colour );
		decal->setOpacity( 0 );
		decal->enableShadowRender( FALSE );
		return decal;
	}

	/// the next decal from a pool, made when the pool has run out
	Shadow *take( std::vector< Shadow * > &pool, size_t &used, const char *texture )
	{
		if( used == pool.size() )
			pool.push_back( newDecal( texture ) );
		return pool[ used++ ];
	}

	void place( Shadow *decal, const Coord3D &pos, Real size, Real alpha )
	{
		const Int opacity = REAL_TO_INT( clamp( 0.0f, alpha, 1.0f ) * 255.0f );
		decal->setPosition( pos.x, pos.y, pos.z );
		decal->setSize( size, size );
		decal->setOpacity( opacity );
		decal->enableShadowRender( opacity > 0 );
	}

	void hideFrom( std::vector< Shadow * > &pool, size_t used )
	{
		for( size_t index = used; index < pool.size(); index++ )
		{
			pool[ index ]->setOpacity( 0 );
			pool[ index ]->enableShadowRender( FALSE );
		}
	}
}

void RadarContactMarks_publish( UnsignedInt sweepFrame, const std::vector<RadarContact> &all )
{
	std::swap( s_sweeps[ 0 ], s_sweeps[ 1 ] );
	s_sweeps[ 0 ].frame = sweepFrame;
	s_sweeps[ 0 ].all = all;
	s_publications = min( s_publications + 1, 2 );
}

//-------------------------------------------------------------------------------------------------
/** Lays this frame's marks.  Ripples leave the ring at the sweep and run out to RippleTo, fading;
	* the ring holds through its sweep and fades over the next if no sweep follows, so a radar that
	* stops publishing does not leave its last paint on the ground.  Hidden while scripts or -cinema
	* hide the icons, like the signal marks. */
//-------------------------------------------------------------------------------------------------
void RadarContactMarks_update( void )
{
	refreshShown();
	const Real seconds = secondsSinceSweep();
	const Real age = seconds / RadarContactLook::SweepSeconds;
	s_sweepAge = min( age, 1.0f );

	// no decal manager without a renderer (headless): nothing to lay
	if( TheProjectedShadowManager == NULL )
		return;

	size_t contactsUsed = 0;
	size_t ringsUsed = 0;
	if( TheGameLogic->getDrawIconUI() && !CinemaDirector_hidesHud() )
	{
		using namespace RadarContactLook;
		const Real ringAlpha = RingAlpha * clamp( 0.0f, 2.0f - age, 1.0f );
		for( size_t index = 0; index < s_fresh.size(); index++ )
		{
			const Coord3D &pos = s_fresh[ index ].pos;
			if( !onOpenGround( pos ) )
				continue;
			place( take( s_contactDecals, contactsUsed, "ReforgedRadarContact" ), pos, RingSize, ringAlpha );
			for( Int ripple = 0; ripple < 2; ripple++ )
			{
				// a square-root fade keeps a ripple seen until it is nearly out, as the approved still has it
				const Real t = ( seconds - ripple * RippleGap ) / RippleSeconds;
				const Bool running = t >= 0.0f && t < 1.0f;
				place( take( s_ringDecals, ringsUsed, "ReforgedRadarRing" ), pos,
					RippleFrom + ( RippleTo - RippleFrom ) * clamp( 0.0f, t, 1.0f ), running ? RippleAlpha * sqrtf( 1.0f - t ) : 0.0f );
			}
		}
		const Real echoAlpha = EchoAlpha * ( 1.0f - s_sweepAge );
		for( size_t index = 0; index < s_echoes.size(); index++ )
		{
			if( onOpenGround( s_echoes[ index ].pos ) )
				place( take( s_ringDecals, ringsUsed, "ReforgedRadarRing" ), s_echoes[ index ].pos, RingSize, echoAlpha );
		}
	}
	hideFrom( s_contactDecals, contactsUsed );
	hideFrom( s_ringDecals, ringsUsed );
}

void RadarContactMarks_reset( void )
{
	for( size_t index = 0; index < s_contactDecals.size(); index++ )
		s_contactDecals[ index ]->release();
	for( size_t index = 0; index < s_ringDecals.size(); index++ )
		s_ringDecals[ index ]->release();
	s_contactDecals.clear();
	s_ringDecals.clear();
	s_sweeps[ 0 ].all.clear();
	s_sweeps[ 1 ].all.clear();
	s_publications = 0;
	s_fresh.clear();
	s_echoes.clear();
	s_sweepAge = 0.0f;
}

const std::vector<RadarContact> &RadarContactMarks_fresh( void )
{
	return s_fresh;
}

const std::vector<RadarContact> &RadarContactMarks_echoes( void )
{
	return s_echoes;
}

Real RadarContactMarks_sweepAge( void )
{
	return s_sweepAge;
}

ObjectID RadarContactMarks_pick( const Coord3D &pos )
{
	const Int player = ThePlayerList->getLocalPlayer()->getPlayerIndex();
	ObjectID best = INVALID_ID;
	Real bestSquared = RadarContactLook::PickRadius * RadarContactLook::PickRadius;
	if( s_publications == 0 )
		return best;
	const std::vector< RadarContact > &all = s_sweeps[ 0 ].all;
	for( size_t index = 0; index < all.size(); index++ )
	{
		if( all[ index ].player != player )
			continue;
		const Real dx = all[ index ].pos.x - pos.x;
		const Real dy = all[ index ].pos.y - pos.y;
		const Real squared = dx * dx + dy * dy;
		if( squared <= bestSquared )
		{
			bestSquared = squared;
			best = all[ index ].id;
		}
	}
	return best;
}
