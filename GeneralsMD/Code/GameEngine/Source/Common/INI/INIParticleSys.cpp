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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: INIParticleSys.cpp ///////////////////////////////////////////////////////////////////////////
// Author: Michael S. Booth, November 2001
// Desc:   Parsing Particle System INI entries
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "GameClient/ParticleSys.h"


/** 
 * Parse entry 
 */
void INI::parseParticleSystemDefinition( INI* ini )
{
	AsciiString name;

	// read the name
	const char* c = ini->getNextToken();
	name.set( c );	

	// find existing item if present
	ParticleSystemTemplate *sysTemplate = const_cast<ParticleSystemTemplate*>(TheParticleSystemManager->findTemplate( name ));
	const Bool classic = TheParticleSystemManager->isClassicReadingForkSystems();
	const Bool added = sysTemplate == NULL;
	if (added)
	{
		// no item is present, create a new one
		sysTemplate = TheParticleSystemManager->newTemplate( name );
	}
	else if (classic)
	{
		// an edit of one of EA's systems: it goes into a template nothing is made from
		sysTemplate = TheParticleSystemManager->newTemplate( "ClassicDiscard" );
	}

	// a map's rules file edits the template every system of that name is made from
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
		TheParticleSystemManager->rememberTemplateBeforeMapINI( sysTemplate );

	// parse the ini definition
	ini->initFromINI( sysTemplate, sysTemplate->getFieldParse() );

	// a system only the fork has stays a name a weapon or a saved game can hold, and emits nothing, unless
	// it is a Superweapon General's copy of one of EA's: then it is that system, as he had it
	if (classic && added)
	{
		const ParticleSystemTemplate *ea = name.startsWith( "SupW_" ) ? TheParticleSystemManager->findTemplate( name.str() + 5 ) : NULL;
		if (ea)
			sysTemplate->copyLookOf( ea );
		else
			sysTemplate->emitNothing();
	}
}
