/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// AIIdentityChina.cpp
// The China generals' own styles: China, Tank, Infantry and Nuke.  AIIdentity.h has the hooks; none yet.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "GameLogic/AIIdentity.h"
#include "GameLogic/AIPlayer.h"

class AIIdentityChina : public AIIdentity
{
public:
	AIIdentityChina( AIPlayer *ai, AIGeneral general ) : AIIdentity( ai, general ) {}
};

AIIdentity *createChinaIdentity( AIPlayer *ai, AIGeneral general )
{
	return new AIIdentityChina( ai, general );
}
