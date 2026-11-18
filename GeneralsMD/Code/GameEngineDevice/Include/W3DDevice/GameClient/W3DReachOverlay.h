// The ground a defence can hit (GameClient/ReachOverlay.h), drawn on the terrain.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __W3DREACHOVERLAY_H__
#define __W3DREACHOVERLAY_H__

#include "Lib/BaseType.h"
#include <vector>

class CameraClass;
struct ReachOverlayField;

/** The look, in world units and alphas, read by both renderers.  The Direct3D 11 program takes them
	* through its constants, so tuning one needs no new program in the shader cache. */
namespace ReachLook
{
	const Real BorderWidth = 1.5f;		///< the line on the inside of a range edge
	const Real BandWidth = 10.0f;			///< the stronger wash just inside the edge fades out over this
	const Real FillAlpha = 0.08f;			///< the whole area
	const Real BandAlpha = 0.14f;			///< added at the edge, nought BandWidth in
	const Real LineAlpha = 0.75f;
	const Real LineWhite = 0.40f;			///< how far the line goes from the owner's colour toward white
	const Real ShadeWidth = 0.75f;		///< a dark hairline just inside the line, so it reads on pale ground
	const Real ShadeAlpha = 0.30f;
	const Real SoftWidth = 12.0f;			///< a blocked edge (cliff, building, fog) fades in over this, no line
	const Real SoftLine = 0.15f;			///< what is left of the line along a blocked edge
	const Real RippleAlpha = 0.16f;		///< a faint band that leaves the range line and runs inward (Direct3D 11)
	const Real RipplePeriod = 3.0f;		///< seconds between two
	/// world units it runs before it is gone.  The field is flat past 13 in (its 16 limit less the
	/// margin), so a ripple that ran further would light the whole area at once; and the hardness is
	/// only meaningful about 20 in from a blocked edge
	const Real RippleSpan = 8.0f;
	const Real RippleWidth = 3.0f;
}

/** This frame's fields on the ground.  Called from the terrain pass once the ground, the roads, the
	* bridges and the tracks are down and before the shroud and any model: the shroud darkens it with
	* the ground, every unit and building stands on it, and the interface is drawn long after. */
void W3DReachOverlay_Draw( CameraClass &camera );

/** Lets go of every texture and Direct3D 11 object the overlay holds, for the terrain going away or a
	* device reset; the next draw makes them again. */
void W3DReachOverlay_Release( void );

/** The Direct3D 11 pass (DX11ReachOverlay.cpp, Windows only).  False when it cannot draw at all, and
	* the fixed-function path draws instead.  Makes its program on the first terrain frame, fields or
	* not, so the first defence selected does not pay for the compile. */
Bool DX11ReachOverlay_Draw( CameraClass &camera, const std::vector< const ReachOverlayField * > &fields );
void DX11ReachOverlay_Release( void );

#if !defined(_WIN32)
/** Native distance fields on the explicit terrain-phase depth owner. False retains tiled drawing. */
Bool SdlReachOverlay_Draw(CameraClass &, const std::vector<const ReachOverlayField *> &fields);
/** False retains ownership until queued readers submit. */
Bool SdlReachOverlay_Release();
#endif

#endif
