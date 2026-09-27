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

// FILE: Thing.cpp ////////////////////////////////////////////////////////////
// Created:   Colin Day, May 2001
//
// Desc:      Things are the base class for objects and drawables, objects
//						are logic side representations while drawables are client
//						side.  Common data will be held in the Thing defined here
//						and systems that need to work with both of them will work with
//						"Things".  The transform is not common any more: Object keeps a
//						fixed point one (Object.cpp), Drawable a float one (Drawable.cpp).
//
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Thing.h"
#include "Common/ThingTemplate.h"

//=============================================================================
/** Constructor */
//=============================================================================
Thing::Thing( const ThingTemplate *thingTemplate )
{
	// sanity
	if( thingTemplate == NULL )
	{

		// cannot create thing without template
		DEBUG_CRASH(( "no template" ));
		return;

	}  // end if

	m_template = thingTemplate;
#if defined(_DEBUG) || defined(_INTERNAL)
	m_templateName = thingTemplate->getName();
#endif

}

//=============================================================================
/** Destructor */
//=============================================================================
Thing::~Thing()
{
}

//=============================================================================
const ThingTemplate *Thing::getTemplate() const
{
	return m_template;
}

//-------------------------------------------------------------------------------------------------
Bool Thing::isKindOf(KindOfType t) const
{
	return getTemplate()->isKindOf(t);
}

//-------------------------------------------------------------------------------------------------
Bool Thing::isKindOfMulti(const KindOfMaskType& mustBeSet, const KindOfMaskType& mustBeClear) const
{
	return getTemplate()->isKindOfMulti(mustBeSet, mustBeClear);
}

// ------------------------------------------------------------------------------------------------
Bool Thing::isAnyKindOf( const KindOfMaskType& anyKindOf ) const
{
	return getTemplate()->isAnyKindOf( anyKindOf );
}
