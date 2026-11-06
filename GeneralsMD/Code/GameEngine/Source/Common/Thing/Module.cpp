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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Module.cpp ///////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, September 2001
// Desc:	 Object and drawable modules and actions.  These are simply just class
//				 instances that we can assign to objects, drawables, and things to contain
//				 data and code for specific events, or just to hold data
///////////////////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/Module.h"
#include "Common/Thing.h"
#include "Common/INI.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma message("************************************** WARNING, optimization disabled for debugging purposes")
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// this method should NEVER be overridden by user code, only via the MAKE_STANDARD_MODULE_xxx macros!
// it should also NEVER be called directly; it's only for use by ModuleFactory!
/*static*/ ModuleData* Module::friend_newModuleData(INI* ini) 
{ 
	ModuleData* data = MSGNEW("Module::friend_newModuleData") ModuleData;	// no need to memorypool these since we never allocate more than one of each
	if (ini)
		ini->initFromINI(data, 0);	// this is just so that an "end" token is required
	return data;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Module::~Module()
{

}  // end ~Module

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Module::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Module::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** load post process */
// ------------------------------------------------------------------------------------------------
void Module::loadPostProcess( void )
{

}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ObjectModule::ObjectModule( Thing *thing, const ModuleData* moduleData ) : Module(moduleData)
{ 
	if (!moduleData)
	{
		DEBUG_CRASH(("module data may not be null\n"));
		throw INI_INVALID_DATA;
	}

	DEBUG_ASSERTCRASH( thing, ("Thing passed to ObjectModule is NULL!\n") );
	m_object = AsObject(thing);
	DEBUG_ASSERTCRASH( m_object, ("Thing passed to ObjectModule is not an Object!\n") );

}  // end ObjectModule

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ObjectModule::~ObjectModule( void )
{

}  // end ~ObjectModule

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ObjectModule::crc( Xfer *xfer )
{

	// extend base class
	Module::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ObjectModule::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	Module::xfer( xfer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** load post process */
// ------------------------------------------------------------------------------------------------
void ObjectModule::loadPostProcess( void )
{

	// extend base class
	Module::loadPostProcess();

}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DrawableModule::DrawableModule( Thing *thing, const ModuleData* moduleData ) : Module(moduleData)
{ 
	if (!moduleData)
	{
		DEBUG_CRASH(("module data may not be null\n"));
		throw INI_INVALID_DATA;
	}

	DEBUG_ASSERTCRASH( thing, ("Thing passed to DrawableModule is NULL!\n") );
	m_drawable = AsDrawable(thing);
	DEBUG_ASSERTCRASH( m_drawable, ("Thing passed to DrawableModule is not a Drawable!\n") );

}  // end ~DrawableModule

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DrawableModule::~DrawableModule( void )
{

}  // end ~DrawableModule

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DrawableModule::crc( Xfer *xfer )
{

	// extend base class
	Module::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void DrawableModule::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	Module::xfer( xfer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** load post process */
// ------------------------------------------------------------------------------------------------
void DrawableModule::loadPostProcess( void )
{

	// extend base class
	Module::loadPostProcess();

}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
void UpgradeMuxData::performUpgradeFX(Object* obj) const
{
	if (m_fxListUpgrade)
	{
		FXList::doFXObj(m_fxListUpgrade, obj);
	}
}

//-------------------------------------------------------------------------------------------------
/** The upgrade a TriggeredBy, ConflictsWith or RemovesUpgrades line names, or NULL when there is no
	* such upgrade.  EA threw INI_INVALID_DATA here, lazily, on the first object of the template, so a
	* data naming an upgrade it never defines killed the map load with nothing in the crash report
	* (reports 1893-1899).  The name is said in the log and skipped instead, and looked up again the
	* next time: a map.ini can define the upgrade for one match, so a miss is never kept in the shared
	* module data, or a machine that played a match without that map would go on without the upgrade
	* that a fresh machine has. */
//-------------------------------------------------------------------------------------------------
static const UpgradeTemplate *findMuxUpgrade( const AsciiString &name, const char *field, const Module *module, const Object *obj )
{
	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( name );
	if( !upgrade && !name.isEmpty() && !name.isNone() )
	{
		// said once per object, module and name; the set only quiets the log and decides nothing
		static std::set< std::string > said;
		AsciiString key;
		key.format( "%s %s %s %s", obj->getTemplate()->getName().str(),
			KEYNAME( module->getModuleTagNameKey() ).str(), field, name.str() );
		if( said.insert( key.str() ).second )
			DEBUG_LOG(( "%s %s of %s: %s names %s, which is not an Upgrade; ignored\n",
				KEYNAME( module->getModuleNameKey() ).str(), KEYNAME( module->getModuleTagNameKey() ).str(),
				obj->getTemplate()->getName().str(), field, name.str() ));
	}
	return upgrade;
}

//-------------------------------------------------------------------------------------------------
void UpgradeMuxData::muxDataProcessUpgradeRemoval(ObjectModule* module) const
{
	std::vector<AsciiString>::const_iterator it;
	for( it = m_removalUpgradeNames.begin(); it != m_removalUpgradeNames.end(); ++it )
	{
		const UpgradeTemplate* theTemplate = findMuxUpgrade( *it, "RemovesUpgrades", module, module->getObject() );
		if( theTemplate )
			module->getObject()->removeUpgrade(theTemplate);
	}
}

//-------------------------------------------------------------------------------------------------
Bool UpgradeMuxData::isTriggeredBy(const std::string &upgrade) const
{
	std::vector<AsciiString>::const_iterator it;
	for( it = m_triggerUpgradeNames.begin(); it != m_triggerUpgradeNames.end();	++it)
	{
		AsciiString trigger = *it;
		if (strcasecmp(trigger.str(), upgrade.c_str()) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void UpgradeMuxData::getUpgradeActivationMasks(UpgradeMaskType& activation, UpgradeMaskType& conflicting, const ObjectModule* module) const
{
	// already computed.
	if (!m_activationUpgradeNames.empty() || !m_conflictingUpgradeNames.empty())
	{
		m_activationMask.clear();
		m_conflictingMask.clear();

		// An upgrade that does not exist adds nothing to either mask.  The masks are only cached once
		// every name was found: a miss is looked up again on the next call (see findMuxUpgrade).
		Bool allFound = TRUE;
		std::vector<AsciiString>::const_iterator it;
		for( it = m_activationUpgradeNames.begin();
					it != m_activationUpgradeNames.end();
					it++)
		{
			const UpgradeTemplate* theTemplate = findMuxUpgrade( *it, "TriggeredBy", module, module->getObject() );
			if( theTemplate )
				m_activationMask.set( theTemplate->getUpgradeMask() );
			else if( !it->isEmpty() && !it->isNone() )
				allFound = FALSE;
		}

		for( it = m_conflictingUpgradeNames.begin();
					it != m_conflictingUpgradeNames.end();
					it++)
		{
			const UpgradeTemplate* theTemplate = findMuxUpgrade( *it, "ConflictsWith", module, module->getObject() );
			if( theTemplate )
				m_conflictingMask.set( theTemplate->getUpgradeMask() );
			else if( !it->isEmpty() && !it->isNone() )
				allFound = FALSE;
		}

		// We set the trigger upgrade names with the activationUpgradeNames entries to be used later.
		// We have to do this because the activationUpgradeNames are toasted just below.
		m_triggerUpgradeNames = m_activationUpgradeNames;

		//Clear the names now that we've cached the values!
		if( allFound )
		{
			m_activationUpgradeNames.clear();
			m_conflictingUpgradeNames.clear();
		}
	}
	activation = m_activationMask;
	conflicting = m_conflictingMask;
}
