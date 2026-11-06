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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// RadiusDecal.cpp ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_SHADOW_NAMES

#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameClient/CinemaDirector.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/RadiusDecal.h"
#include "GameClient/Shadow.h"
#include "GameLogic/GameLogic.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// ------------------------------------------------------------------------------------------------
RadiusDecalTemplate::RadiusDecalTemplate() : 
	m_shadowType(SHADOW_ALPHA_DECAL), 
	m_minOpacity(1.0f),
	m_maxOpacity(1.0f),
	m_opacityThrobTime(LOGICFRAMES_PER_SECOND),
	m_color(0),
	m_onlyVisibleToOwningPlayer(true),
	m_name(AsciiString::TheEmptyString)  // Added By Sadullah Nader for Init purposes
{
}

// ------------------------------------------------------------------------------------------------
void RadiusDecalTemplate::createRadiusDecal(const Coord3D& pos, Real radius, const Player* owningPlayer, RadiusDecal& result, const char* texture) const
{
	result.clear();
	
	if (owningPlayer == NULL)
	{
		DEBUG_CRASH(("You MUST specify a non-NULL owningPlayer to createRadiusDecal. (srj)\n"));
		return;
	}

	if (m_name.isEmpty() || radius <= 0.0f)
		return;

	// it is now considered nonEmpty, regardless of the state of m_decal, etc
	result.m_empty = false;

	// No decal manager without a renderer (headless, or off Windows until the D track): no decal.
	if (TheProjectedShadowManager != NULL &&
			(!m_onlyVisibleToOwningPlayer ||
			 owningPlayer->getPlayerIndex() == ThePlayerList->getLocalPlayer()->getPlayerIndex()))
	{
		// Logic modules call this inside the tick, which runs beside the render: addDecal links the decal
		// into the list the render is walking and loads its texture, so it waits for the tick's end.
		std::shared_ptr<RadiusDecalCell> cell = std::make_shared<RadiusDecalCell>();
		cell->setPosition(pos);
		cell->setAngle(0.0f);
		result.m_decal = cell;
		result.m_template = this;
		// By value, not through this: a template can be a module's own copy (DeliverPayloadAIUpdate's),
		// gone before the call runs when its object is made and destroyed in the same tick.
		const AsciiString name = texture ? AsciiString(texture) : m_name;
		const ShadowType shadowType = m_shadowType;
		const Real size = radius*2;
		const Color color = m_color == 0 ? clientPlayerColor(owningPlayer) : m_color;
		LogicThread::callAfterTick( [cell, name, shadowType, size, color]()
		{
			Shadow::ShadowTypeInfo decalInfo;
			decalInfo.allowUpdates = FALSE;										// shadow texture will never update
			decalInfo.allowWorldAlign = TRUE;									// shadow image will wrap around world objects
			decalInfo.m_type = shadowType;
			strcpy(decalInfo.m_ShadowName, name.str());		// name of your texture
			decalInfo.m_sizeX = size;											// world space dimensions
			decalInfo.m_sizeY = size;											// world space dimensions

			Shadow *decal = TheProjectedShadowManager->addDecal(&decalInfo);
			if (decal == NULL)
			{
				DEBUG_CRASH(("Unable to add decal %s\n",decalInfo.m_ShadowName));
				return;
			}
			decal->setColor(color);
			// what the owner set while the add waited, the creation's own position and angle among it
			if (cell->hasPosition)
				decal->setPosition(cell->position.x, cell->position.y, cell->position.z);
			if (cell->hasAngle)
				decal->setAngle(cell->angle);
			if (cell->hasOpacity)
				decal->setOpacity(cell->opacity);
			cell->decal = decal;
		} );
	}
}

// ------------------------------------------------------------------------------------------------
void RadiusDecalTemplate::xferRadiusDecalTemplate( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferAsciiString(&m_name);	
	xfer->xferUser(&m_shadowType, sizeof(m_shadowType));	
	xfer->xferReal(&m_minOpacity);
  xfer->xferReal(&m_maxOpacity);
	xfer->xferUnsignedInt(&m_opacityThrobTime);
	xfer->xferColor(&m_color);
	xfer->xferBool(&m_onlyVisibleToOwningPlayer);
}

static void parseRetiredField( INI *, void *, void *, const void * )
{
}

// ------------------------------------------------------------------------------------------------
/*static*/ void RadiusDecalTemplate::parseRadiusDecalTemplate(INI* ini, void *instance, void * store, const void* /*userData*/)
{
	static const FieldParse dataFieldParse[] = 
	{
		{ "Texture",										INI::parseAsciiString,				NULL,							offsetof( RadiusDecalTemplate, m_name ) },
		{ "Style",											INI::parseBitString32,				TheShadowNames,		offsetof( RadiusDecalTemplate, m_shadowType ) },
		{ "OpacityMin",									INI::parsePercentToReal,			NULL,							offsetof( RadiusDecalTemplate, m_minOpacity ) },
		{ "OpacityMax",									INI::parsePercentToReal,			NULL,							offsetof( RadiusDecalTemplate, m_maxOpacity) },
		{ "OpacityThrobTime",						INI::parseDurationUnsignedInt,NULL,							offsetof( RadiusDecalTemplate, m_opacityThrobTime ) },
		{ "Color",											INI::parseColorInt,						NULL,							offsetof( RadiusDecalTemplate, m_color ) },
		{ "OnlyVisibleToOwningPlayer",	INI::parseBool,								NULL,							offsetof( RadiusDecalTemplate, m_onlyVisibleToOwningPlayer ) },
		// the retired aura rings' fields; data written for them may still name them, and an unknown field throws
		{ "SpinTime",										parseRetiredField,						NULL,							0 },
		{ "LightReference",							parseRetiredField,						NULL,							0 },
		{ "LightScaleMin",							parseRetiredField,						NULL,							0 },
		{ "LightScaleMax",							parseRetiredField,						NULL,							0 },
		{ 0, 0, 0, 0 }
	};

	ini->initFromINI(store, dataFieldParse);
}

// ------------------------------------------------------------------------------------------------
RadiusDecal::RadiusDecal() :
	m_template(NULL),
	m_empty(true)
{
}

// ------------------------------------------------------------------------------------------------
RadiusDecal::RadiusDecal(const RadiusDecal& that) :
	m_template(NULL),
	m_empty(true)
{
	DEBUG_CRASH(("not fully implemented"));
}

// ------------------------------------------------------------------------------------------------
RadiusDecal& RadiusDecal::operator=(const RadiusDecal& that)
{
	if (this != &that)
	{
		clear();
		DEBUG_CRASH(("not fully implemented"));
	}
	return *this;
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::xferRadiusDecal( Xfer *xfer )
{
	/// @todo implement me
	if (xfer->getXferMode() == XFER_LOAD)
	{
		clear();
	}
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::clear()
{
	m_template = NULL;
	if (m_decal)
	{
		// after the tick, like the add: the cell outlives this RadiusDecal and its module
		std::shared_ptr<RadiusDecalCell> cell = m_decal;
		LogicThread::callAfterTick( [cell]()
		{
			if (cell->decal)
				cell->decal->release();
			cell->decal = NULL;
		} );
	}
	m_decal.reset();
	m_empty = true;
}

// ------------------------------------------------------------------------------------------------
RadiusDecal::~RadiusDecal()
{
	clear();
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::update()
{
	// the decal itself turns up after the tick that created it; until then the cell keeps what is set
	RadiusDecalCell *decal = m_decal.get();
	if (decal && m_template)
	{
		UnsignedInt now = TheGameLogic->getFrame();
		Real theta = (2*PI) * (Real)(now % m_template->m_opacityThrobTime) / (Real)m_template->m_opacityThrobTime;
		Real percent = 0.5f * (Sin(theta) + 1.0f);
		Int opac;
		if( TheGameLogic->getDrawIconUI() && !CinemaDirector_hidesHud() )
		{
			Real opacity = m_template->m_minOpacity + percent * (m_template->m_maxOpacity - m_template->m_minOpacity);
			opac = REAL_TO_INT(opacity * 255.0f);
		}
		else
		{
			//Scripts turned this off, or -cinema did, so don't show them!
			opac = 0;
		}
		decal->setOpacity(opac);
	}
}



void RadiusDecal::setOpacity( Real o )
{
	if (m_decal)
	{
		m_decal->setOpacity(REAL_TO_INT(255.0f * o));
	}
}

// ------------------------------------------------------------------------------------------------
void RadiusDecal::setPosition(const Coord3D& pos)
{
	if (m_decal)
	{
		m_decal->setPosition(pos);	//world space position of center of decal
	}
}

// ------------------------------------------------------------------------------------------------
// Straight to the decal once the add has run, into the cell until then.  The two never overlap: the
// add runs after the tick, on the main thread, while no tick is writing.
void RadiusDecalCell::setPosition(const Coord3D& pos)
{
	if (decal)
		decal->setPosition(pos.x, pos.y, pos.z);
	else
	{
		position = pos;
		hasPosition = TRUE;
	}
}

void RadiusDecalCell::setOpacity(Int o)
{
	if (decal)
		decal->setOpacity(o);
	else
	{
		opacity = o;
		hasOpacity = TRUE;
	}
}

void RadiusDecalCell::setAngle(Real a)
{
	if (decal)
		decal->setAngle(a);
	else
	{
		angle = a;
		hasAngle = TRUE;
	}
}
