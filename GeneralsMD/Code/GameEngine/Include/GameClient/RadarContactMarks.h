// The Turkiye radar's contacts: moving enemies hidden in the fog inside a player's radar coverage,
// published by the logic once a sweep and drawn on the ground (RadarContactMarks.cpp) and on the
// minimap.  The units themselves stay hidden and the fog stays down; only the paint shows.  Nothing
// here goes back into the logic.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __RADARCONTACTMARKS_H__
#define __RADARCONTACTMARKS_H__

#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include <vector>

/** One painted contact: whose radar saw it, what it is (for a pick, never drawn) and where it stood. */
struct RadarContact
{
	Int player;			///< the player index whose radar painted it
	ObjectID id;
	Coord3D pos;
};

/** A sweep's contacts for every player.  Main thread: the logic calls it through callAfterTick. */
void RadarContactMarks_publish( UnsignedInt sweepFrame, const std::vector<RadarContact> &all );
/// InGameUI::update: reads the logic (outside the logic thread's window), refills the lists below and
/// lays and animates the ground marks.  The three getters after reset only read what it left, so a
/// draw beside the tick may call them
void RadarContactMarks_update( void );
void RadarContactMarks_reset( void );			///< InGameUI::reset: forgets both sweeps, releases the decals
/// the shown player's contacts (TheObserverCamera.getShroudPlayerIndex()), this sweep, less any
/// unit that has since come into his sight
const std::vector<RadarContact> &RadarContactMarks_fresh( void );
/// the shown player's contacts, the sweep before, less the same
const std::vector<RadarContact> &RadarContactMarks_echoes( void );
/// 0 at a publish, rising to 1 by the next sweep is due
Real RadarContactMarks_sweepAge( void );
/// the local player's fresh contact nearest pos within RadarContactLook::PickRadius, else INVALID_ID
ObjectID RadarContactMarks_pick( const Coord3D &pos );

/** The look, in world units, seconds, alphas and minimap pixels. */
namespace RadarContactLook
{
	const Real SweepSeconds = 2.0f;			///< the logic's sweep period, what sweepAge counts to
	const Real RingSize = 20.0f;				///< across, the contact's ring with its centre pip
	const Real RingAlpha = 0.95f;
	const Real RippleFrom = 20.0f;			///< across, a ping ripple as it leaves the ring (RingSize)
	const Real RippleTo = 42.0f;				///< across, as it is gone: about twice the ring, as in the approved still
	const Real RippleSeconds = 0.9f;		///< one ripple's run
	const Real RippleGap = 0.25f;				///< the second ripple leaves this long after the first
	const Real RippleAlpha = 0.7f;			///< at its start, fading as the square root of what is left of the run
	const Real EchoAlpha = 0.35f;				///< the previous sweep's ring, fading over the sweep
	const Real PickRadius = 40.0f;			///< world units from a click to a contact it picks
	const UnsignedInt Colour = 0xFFF04024;	///< hostile red, the attack cursor's
	const Int MinimapPip = 3;						///< pixels across, the fresh contact's square
	const Int MinimapRing = 9;					///< pixels across, the faint ring round it
	const Int MinimapEcho = 2;					///< pixels across, the previous sweep's square
}

#endif
