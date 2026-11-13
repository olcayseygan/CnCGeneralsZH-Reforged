// Cosmetic tank skins (TankSkin.h).  The table is code rather than an INI: a new skin needs a new
// choice in the options and the lobby anyway, and a loose file could not shadow it.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/TankSkin.h"

#include "Common/GlobalData.h"
#include "Common/PlayerList.h"
#include "GameNetwork/GameInfo.h"

// The models each skin dresses, damaged states included.  The general's copies have models of their
// own (the Laser General's Crusader, the Nuke General's Battlemaster); the other generals' share these.
static const char *const SKIN_1K_MODELS[] =
{
	"AVLeopard", "AVLeopard_D",				// Crusader
	"AVLaserTnk", "AVLaserTnk_D",			// Laser General's Crusader
	"NVBtMstr", "NVBtMstr_D",					// Battlemaster
	"NVBtMstrNG", "NVBtMstrNG_D",			// Nuke General's Battlemaster
	"UVLiteTank", "UVLiteTank_D",			// Scorpion
	NULL
};

const char *TankSkin_texturePrefix( Int skin, const char *modelName )
{
	if( skin != TANK_SKIN_1K || modelName == NULL )
		return NULL;
	for( const char *const *model = SKIN_1K_MODELS; *model; ++model )
		if( strcasecmp( *model, modelName ) == 0 )
			return "Skin1K_";
	return NULL;
}

Int TankSkin_ofPlayer( Int playerIndex )
{
	if( TheGameInfo == NULL || ThePlayerList == NULL )
		return TANK_SKIN_NONE;
	// set as the match starts and never changed in it, so the overlap window may read it too
	const Int slot = ThePlayerList->getSlotIndex( playerIndex );
	const GameSlot *seat = slot >= 0 ? TheGameInfo->getConstSlot( slot ) : NULL;
	return ( seat && seat->isHuman() ) ? seat->getSkin() : TANK_SKIN_NONE;
}

Int TankSkin_localPick( void )
{
	return ( TheGlobalData && TheGlobalData->m_tankSkin1K ) ? TANK_SKIN_1K : TANK_SKIN_NONE;
}
