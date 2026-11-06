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

/** A contact's class, drawn as a small glyph once the logic knows it (a SIHA recon source, or a
	* radar whose owner has Hedef Siniflandirma).  A value is its cell in the glyph sheet, less one. */
enum RadarGlyph
{
	RADAR_GLYPH_NONE = 0,
	RADAR_GLYPH_INFANTRY,
	RADAR_GLYPH_VEHICLE,
	RADAR_GLYPH_ANTIAIR,
	RADAR_GLYPH_ARTILLERY,
	RADAR_GLYPH_AIRCRAFT,
	RADAR_GLYPH_STRUCTURE_DEFENCE,

	RADAR_GLYPH_COUNT
};

/** One painted contact: whose radar saw it, what it is and where it stood. */
struct RadarContact
{
	Int player;			///< the player index whose radar painted it
	ObjectID id;		///< for a pick and a heading, never drawn
	Coord3D pos;
	UnsignedByte glyph;	///< a RadarGlyph; RADAR_GLYPH_NONE when the class is not known
	Bool recon;			///< seen by a SeesStationary source (SIHA recon): drawn as a chip, not a red ring
};

/** A live SeesStationary source, for its area ring and its countdown tray. */
struct RadarReconArea
{
	Int player;			///< the player index the area reports to
	Coord3D pos;
	Real radius;
	UnsignedInt endFrame;	///< the logic frame the source runs out on; 0 for a source with no end (ring, no countdown)
};

/** A sweep's contacts and recon areas for every player.  Main thread: the logic calls it through callAfterTick. */
void RadarContactMarks_publish( UnsignedInt sweepFrame, const std::vector<RadarContact> &contacts, const std::vector<RadarReconArea> &areas );
/// InGameUI::update: reads the logic (outside the logic thread's window), refills the lists below and
/// lays and animates the ground marks.  The getters and the draw after reset only read what it left,
/// so a draw beside the tick may call them
void RadarContactMarks_update( void );
void RadarContactMarks_reset( void );			///< InGameUI::reset: forgets both sweeps, releases the decals
/// InGameUI::preDraw, in the 2D pass: the chips over recon contacts, the glyphs beside classified red
/// rings and each recon area's tray, kept above bottom (the command bar's top on the screen)
void RadarContactMarks_draw( Int bottom );
/// the shown player's contacts (TheObserverCamera.getShroudPlayerIndex()), this sweep, less any
/// unit that has since come into his sight
const std::vector<RadarContact> &RadarContactMarks_fresh( void );
/// the shown player's contacts, the sweep before, less the same
const std::vector<RadarContact> &RadarContactMarks_echoes( void );
/// the shown player's recon areas still running (an endFrame of 0 never runs out)
const std::vector<RadarReconArea> &RadarContactMarks_areas( void );
/// 0 at a publish, rising to 1 by the next sweep is due
Real RadarContactMarks_sweepAge( void );
/// the local player's fresh contact nearest pos within RadarContactLook::PickRadius, else INVALID_ID
ObjectID RadarContactMarks_pick( const Coord3D &pos );

/** The look, in world units, seconds, alphas, pixels and colours. */
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

	// SIHA recon, as recon_v1 has it
	const UnsignedInt Amber = 0xFFFFB428;		///< a contact whose type is known: ground ring, glyph, chevron, minimap dot
	const UnsignedInt AreaGreen = 0xFF5AD278;	///< the recon area's dashed ring and its tray's label
	const UnsignedInt TrayText = 0xFFCDFFCD;	///< the tray's time left
	const Real AreaAlpha = 0.8f;				///< the area ring's decal
	const Real AreaDecalSpan = 256.0f / 240.0f;	///< the area decal across over the area's diameter: its ring sits at 240 of 256 (Tools/recon_marks.py)
	const Real ReconRingSize = 14.0f;		///< across, the amber ground ring under a recon contact
	const Real ChevronAhead = 12.0f;		///< world units from the contact to the heading chevron's tip
	const Real MoverDistance = 3.0f;		///< world units between two sweeps that make a contact a mover
	const Real ChipHeight = 26.0f;			///< world units over the ground the chip floats, where a health bar sits
	const Real TrayPixels = 10.0f;			///< the tray's lettering at 1920x1080, scaled with the interface
	const Int MinimapReconDot = 3;			///< pixels across, the recon contact's dot on its dark keyline
}

#endif
