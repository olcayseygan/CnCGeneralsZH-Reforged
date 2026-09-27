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

// FILE: Thing.h //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					         
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  Thing.h
//
// Created:    Colin Day, May 2001
//
// Desc:       Things are the base class for objects and drawables, objects
//						 are logic side representations while drawables are client
//						 side.  Common data will be held in the Thing defined here
//						 and systems that need to work with both of them will work with
//						 "Things"
//
//-----------------------------------------------------------------------------

#pragma once

#ifndef __THING_H_
#define __THING_H_

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//           Includes                                                      
//-----------------------------------------------------------------------------
#include "Common/GameMemory.h"
#include "Common/KindOf.h"
#include "Common/OVERRIDE.h"
#include "WWMath/Matrix3D.h"							///< @todo Decide if we're keeping the WWMath libs (MSB)

//-----------------------------------------------------------------------------
//           Forward References
//-----------------------------------------------------------------------------

class Object;
class AIObject;
class Drawable;
class Team;
class ThingTemplate;

//-----------------------------------------------------------------------------
//           Type Defines
//-----------------------------------------------------------------------------

//=====================================
// class Thing
//=====================================
/** A thing is the common base class for objects and drawables.  It will
	* hold common information to both and systems that need to work with both
	* objects and drawables should work with things instead.  You can not
	* instantiate things, they are purely virtual */
//=====================================
class Thing : public MemoryPoolObject
{
	// note, it is explicitly OK to pass null for 'thing' here;
	// they will check for null and return null in these cases.
	friend inline Object *AsObject(Thing *thing) { return thing ? thing->asObjectMeth() : NULL; }
	friend inline Drawable *AsDrawable(Thing *thing) { return thing ? thing->asDrawableMeth() : NULL; }
	friend inline const Object *AsObject(const Thing *thing) { return thing ? thing->asObjectMeth() : NULL; }
	friend inline const Drawable *AsDrawable(const Thing *thing) { return thing ? thing->asDrawableMeth() : NULL; }

	MEMORY_POOL_GLUE_ABC(Thing)

public:

	Thing( const ThingTemplate *thingTemplate );

	/** 
		return the thing template for this thing.
	*/
	const ThingTemplate *getTemplate() const;

	// convenience method for patching isKindOf thru to template.
	Bool isKindOf(KindOfType t) const;
	Bool isKindOfMulti(const KindOfMaskType& mustBeSet, const KindOfMaskType& mustBeClear) const;
	Bool isAnyKindOf(const KindOfMaskType& anyKindOf) const;

	/* Where a Thing is lives in the subclasses, not here.  An Object holds a fixed point transform,
		 which is what the simulation runs on (GameLogic/Object.h); a Drawable holds a float Matrix3D,
		 which is what the renderer wants (GameClient/Drawable.h).  The Object pushes its transform to
		 its Drawable every time it changes, and nothing goes the other way. */

protected:

	virtual Object *asObjectMeth() { return NULL; }
	virtual Drawable *asDrawableMeth() { return NULL; }
	virtual const Object *asObjectMeth() const { return NULL; }
	virtual const Drawable *asDrawableMeth() const { return NULL; }

private:

	// note that it is declared 'const' -- the assumption being that
	// since ThingTemplates are shared between many, many Things, the Thing
	// should never be able to change it.
	OVERRIDE<ThingTemplate> m_template;	///< reference back to template database
#if defined(_DEBUG) || defined(_INTERNAL)
	AsciiString m_templateName;
#endif

};


//-----------------------------------------------------------------------------
//           Externals                                                     
//-----------------------------------------------------------------------------

#endif // $label

