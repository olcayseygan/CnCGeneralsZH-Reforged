// Cosmetic unit skins, an online player's account entitlement.  Each skin has a fixed small id, the
// one zerohour.gg uses, and dresses one unit's models (Data\INI\SkinsReforged.ini, outside the INI
// checksum).  A player's loadout is a set of ids, at most one a unit, fed in by TankSkin_setLocalLoadout;
// in an online room it rides his seat (GameSlot::getSkins, a ",K1.2.3" field in the slot string) and
// every machine in the match, and the replay, draws his units in it.  Drawing only: no logic decision,
// no INI checksum and no model checksum reads any of it.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __TANKSKIN_H__
#define __TANKSKIN_H__

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

/// A loadout: bit n set for skin id n.
// ponytail: ids 1 to 63 fit; a 64th skin needs a list in its place, in the slot field as well.
typedef UnsignedInt64 SkinLoadout;
enum { TANK_SKIN_MAX_ID = 63 };

/// the skin id in this loadout that dresses this model (case ignored), 0 for none
Int TankSkin_forModel( SkinLoadout loadout, const char *modelName );

/// the sheet skin `id` draws in place of `sheet` (extension and case ignored), NULL for none
const char *TankSkin_replacementSheet( Int id, const char *sheet );

/// ",K1.2.3" for a loadout, nothing for an empty one; the dots keep it short for the LAN packet
AsciiString TankSkin_slotField( SkinLoadout loadout );
/// back from "K1.2.3"; NULL, empty, anything else and ids past TANK_SKIN_MAX_ID read as nothing
SkinLoadout TankSkin_fromSlotField( const char *field );

/** The one way in for this player's own loadout, the ids his zerohour.gg account wears, which the
	* online server sends in auth.ok ("1 2 3", any of space, comma or dot between).  Ids past
	* TANK_SKIN_MAX_ID are dropped, and so is a second skin for a unit already dressed; an id the table
	* lacks is kept and dresses nothing.  Safe from the online session's thread.  Returns what it kept. */
SkinLoadout TankSkin_setLocalLoadout( const char *ids );
SkinLoadout TankSkin_localLoadout( void );

/** The loadout the seat behind this player index wore; none outside a lobby game, for an AI seat,
	* the neutral player and -1.  An index rather than a Player, so the render's overlap window can ask
	* with Drawable::getObjectForDraw's copy and touch nothing the tick is changing. */
SkinLoadout TankSkin_ofPlayer( Int playerIndex );

/// forget every table entry, for a test that loads its own
void TankSkin_clearTable( void );

#endif
