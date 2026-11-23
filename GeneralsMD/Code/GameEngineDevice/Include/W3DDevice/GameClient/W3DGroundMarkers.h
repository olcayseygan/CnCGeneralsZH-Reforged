// The Machined Reticle ground markers: the radius cursors, the delivery planes' lanes and drops, the
// superweapon countdown, the Spectre's orbit and the move and attack click markers, laid on the ground
// as textured quads out of one atlas (ReforgedGroundMarkers.dds, MappedImages/HandCreated/
// ReforgedGroundMarkers.ini).  Client only: logic state is read at the sync point and never written.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __W3DGROUNDMARKERS_H_
#define __W3DGROUNDMARKERS_H_

#include "Lib/BaseType.h"

class InGameUI;

/// recordDraw's start: nothing of the last picture's is drawn until take runs again (-cinema never takes)
void W3DGroundMarkers_clearDraw( void );
/// at the sync point, after preDraw: the aimed cursor, the clicks, the planes, missiles and gunships
/// the clicks sent, the selected guards and the click markers, with the ground under each of them
void W3DGroundMarkers_take( InGameUI *ui );
/// in the terrain pass, after the shroud (W3DInGameUI::drawOrderRings), on both devices: the quads on land
void W3DGroundMarkers_draw( Bool shadeByShroud );
/// after the water (RTS3DScene::Flush, once the static sort lists drew it): the quads on water, at its surface
void W3DGroundMarkers_drawOnWater( void );
/// a new match, or the shell: every marker goes
void W3DGroundMarkers_reset( void );
/// the click marker of this order kind (InGameUI::OrderHintKind) is drawn here, not by drawOrderRings
Bool W3DGroundMarkers_drawsClickMarker( Int orderHintKind );

#endif // __W3DGROUNDMARKERS_H_
