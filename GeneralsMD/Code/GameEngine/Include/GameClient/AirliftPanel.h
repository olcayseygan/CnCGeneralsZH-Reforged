// The airlift panel: an airlift power (OCLSpecialPower's Airlift fields, Turkiye's Ally Aid) aimed at
// the ground opens a small build card at the spot instead of firing.  The player picks how many of
// each unit the power carries, sees the planes, the fee and the total, and OK sends the load as one
// MSG_DO_AIRLIFT_ORDER, which the logic checks again.  Client side: every read of the logic happens
// at the render's sync point, inside InGameUI::postDraw's recording, or on the input path.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __AIRLIFTPANEL_H__
#define __AIRLIFTPANEL_H__

#include "Lib/BaseType.h"

class Object;
class SpecialPowerTemplate;

/** Open the panel for `source`'s airlift `power`, aimed at `target`.  Every count starts at 0. */
void AirliftPanel_open( const Object *source, const SpecialPowerTemplate *power, const Coord3D *target );

/** Open it for the local player's first object that carries an airlift power; FALSE with none. */
Bool AirliftPanel_openForLocalPlayer( const Coord3D *target );

void AirliftPanel_close( void );
Bool AirliftPanel_isOpen( void );

/** Draws the panel; called from InGameUI::postDraw. */
void AirliftPanel_draw( void );

/** A press at `mouse` on the panel; TRUE when the press was the panel's.  `cancel` is a right or ctrl
	* press: on a unit's cell it takes one off, anywhere else on the panel it closes it. */
Bool AirliftPanel_click( const ICoord2D *mouse, Bool cancel );

/** The wheel at `mouse`: over a unit's cell `up` adds one and down takes one off, five with shift.
	* TRUE anywhere over the panel, so the camera does not zoom under it. */
Bool AirliftPanel_wheel( const ICoord2D *mouse, Bool up );

/** The panel's state and the centre of every button on screen as JSON members, for the -control
	* socket's "airliftpanel" verb, so a script can click them. */
void AirliftPanel_describe( char *out, Int size );

#endif // __AIRLIFTPANEL_H__
