// Aircraft effects the data cannot key (AircraftEffects.cpp): wingtip vortices in a hard turn, vapour
// on a pull-out and at the supersonic crossing, the sonic boom's dust and a low pass's spray, runway
// smoke and dust, the grey smoke of a hurt jet, the fall of a shot-down one, rotor wash by the ground
// under it, the light under a decoy flare, a hot gun barrel's shimmer, a falling bomb's trail and the
// Aurora's contrail.  Client only, drawn under the Reforged look.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#pragma once

#ifndef __AIRCRAFTEFFECTS_H_
#define __AIRCRAFTEFFECTS_H_

/// once a client pass, after the drawables' own update; does its work once a logic frame
extern void AircraftEffects_update( void );

#endif
