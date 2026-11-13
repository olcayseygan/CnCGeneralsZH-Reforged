// Cosmetic tank skins.  A player's pick rides his lobby seat (GameSlot::getSkin, the slot string's
// last field) and every machine in the match draws his tanks in it.  Drawing only: no logic decision,
// no INI checksum and no model checksum reads any of it, so a seat with a skin plays the same game.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __TANKSKIN_H__
#define __TANKSKIN_H__

#include "Lib/BaseType.h"

/// The values travel in slot strings, replays and saves, so only append.
enum TankSkin
{
	TANK_SKIN_NONE,
	TANK_SKIN_1K,		///< gold "1K": Crusader, Battlemaster, Scorpion and their generals' copies
	TANK_SKIN_COUNT
};

/** The prefix a skin's sheets carry, "Skin1K_" before the model's own sheet name, or NULL when this
	* skin does not dress this model (case ignored).  A sheet missing on disk is simply not swapped. */
const char *TankSkin_texturePrefix( Int skin, const char *modelName );

/** the skin the seat behind this player index picked; none outside a lobby game, for an AI seat, for
	* the neutral player and for -1.  An index rather than a Player, so the render's overlap window can
	* ask with Drawable::getObjectForDraw's copy and touch nothing the tick is changing. */
Int TankSkin_ofPlayer( Int playerIndex );

/// what this machine's options give its own seat ("TankSkin1K")
Int TankSkin_localPick( void );

#endif
