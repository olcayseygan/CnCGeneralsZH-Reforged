// The Machined Reticle rally point: the side's frame round a production building's rally point with
// the side's flag planted in it, and a rail from the building's door to it, laid on the ground as
// textured quads out of one atlas of its own (ReforgedRallyMarkers.dds, MappedImages/HandCreated/
// ReforgedRallyMarkers.ini).  Client only: what it draws is handed over at the sync point by
// W3DWaypointBuffer, which reads the buildings, and nothing here is read by logic.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __W3DRALLYMARKERS_H_
#define __W3DRALLYMARKERS_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/GameType.h"
#include "WWMath/vector3.h"

/// drawn here and not as EA's flag, laser and puck: Reforged graphics, the sheet loaded (tried once,
/// on the first call), in a match, icons shown
Bool W3DRallyMarkers_on( void );
/// the flag and frame a building of this side plants (its template's Side: America, China, GLA, Turkiye
/// and their generals); anything else is America's
Int W3DRallyMarkers_finishOf( const AsciiString &side );
/// at the sync point, before the buildings are handed over
void W3DRallyMarkers_beginTake( void );
/// a selected building that can take a rally point: its path from the door (points[0]) through its
/// natural rally point and round its footprint to the rally point, the last point when hasRally
void W3DRallyMarkers_addPath( ObjectID building, const Vector3 *points, Int count, Bool hasRally, Int finish,
															const Vector3 &color );
/// a building riding the cursor to be placed: where its units will come out and gather
void W3DRallyMarkers_addPlacement( const Vector3 &door, const Vector3 &natural, Int finish, const Vector3 &color );
/// after the last building: the clicks, the cursor's ghost and the ground under all of it
void W3DRallyMarkers_endTake( void );
/// in the terrain pass, after the shroud (W3DInGameUI::drawOrderRings): the quads on land
void W3DRallyMarkers_draw( void );
/// after the water (RTS3DScene::Flush): the quads on water, at its surface
void W3DRallyMarkers_drawOnWater( void );
/// a new match, or the shell
void W3DRallyMarkers_reset( void );

#endif // __W3DRALLYMARKERS_H_
