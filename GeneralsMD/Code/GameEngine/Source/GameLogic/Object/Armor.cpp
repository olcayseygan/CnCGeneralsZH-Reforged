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

// FILE: ArmorTemplate.cpp ///////////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, November 2001
// Desc:   ArmorTemplate descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "Common/INI.h"
#include "Common/ThingFactory.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Damage.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
ArmorStore* TheArmorStore = NULL;					///< the ArmorTemplate store definition

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
ArmorTemplate::ArmorTemplate()
{
	clear();
}

//-------------------------------------------------------------------------------------------------
void ArmorTemplate::clear()
{
	for (int i = 0; i < DAMAGE_NUM_TYPES; i++)
	{
		m_damageCoefficient[i] = 1.0f;
	}
}

//-------------------------------------------------------------------------------------------------
Real ArmorTemplate::adjustDamage(DamageType t, Real damage) const 
{ 
	if (t == DAMAGE_UNRESISTABLE)
		return damage;
	if (t == DAMAGE_SUBDUAL_UNRESISTABLE)
		return damage;

	damage *= m_damageCoefficient[t];

	if (damage < 0.0f)
		damage = 0.0f;
	
	return damage;
}

//-------------------------------------------------------------------------------------------Static
/*static*/ void ArmorTemplate::parseArmorCoefficients( INI* ini, void *instance, void* /* store */, const void* userData )
{
	ArmorTemplate* self = (ArmorTemplate*) instance;

	const char* damageName = ini->getNextToken();
	Real pct = INI::scanPercentToReal(ini->getNextToken());

	if (strcasecmp(damageName, "Default") == 0)
	{
		for (Int i = 0; i < DAMAGE_NUM_TYPES; i++)
		{
			self->m_damageCoefficient[i] = pct;
		}
		return;
	}

	// EA's parse let a misspelt type through (The Power of the West has INFANTRY_MISSIL) and wrote it at
	// index -1; the line is skipped instead, as data written against retail expects
	Int dt = DamageTypeFlags::getSingleBitFromName(damageName);
	if (dt < 0)
	{
		DEBUG_LOG(("[LINE: %d in '%s'] damage type '%s' does not exist; the armor line is skipped.\n", ini->getLineNum(), ini->getFilename().str(), damageName));
		return;
	}
	self->m_damageCoefficient[dt] = pct;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ArmorStore::ArmorStore()
{
	m_armorTemplates.clear();
}

//-------------------------------------------------------------------------------------------------
ArmorStore::~ArmorStore()
{
	m_armorTemplates.clear();
}

//-------------------------------------------------------------------------------------------------
const ArmorTemplate* ArmorStore::findArmorTemplate(AsciiString name) const
{
	NameKeyType namekey = TheNameKeyGenerator->nameToKey(name);
  ArmorTemplateMap::const_iterator it = m_armorTemplates.find(namekey);
  if (it == m_armorTemplates.end())
	{
		return NULL;
	}
	else
	{
		return &(*it).second;
	}
}

//-------------------------------------------------------------------------------------------------
/** A map.ini changes armour in place and the next match in this process would play with it, where a
	* machine that never loaded that map plays the stock values.  Backwards, so an armour both map.ini and
	* solo.ini changed goes back to what it was before either.  In place, because every ArmorSet holds a
	* pointer to its template. */
//-------------------------------------------------------------------------------------------------
void ArmorStore::reset()
{
	for (Int i = (Int)m_beforeMapOverrides.size() - 1; i >= 0; --i)
		m_armorTemplates[m_beforeMapOverrides[i].first] = m_beforeMapOverrides[i].second;
	m_beforeMapOverrides.clear();
}

//-------------------------------------------------------------------------------------------------
/*static */ void ArmorStore::parseArmorDefinition(INI *ini)
{
	static const FieldParse myFieldParse[] = 
	{
		{ "Armor", ArmorTemplate::parseArmorCoefficients, NULL, 0 },
		{ NULL, NULL, NULL, 0 }
	};

	const char *c = ini->getNextToken();
	NameKeyType key = TheNameKeyGenerator->nameToKey(c);
	ArmorTemplateMap::const_iterator existing = TheArmorStore->m_armorTemplates.find(key);
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES && existing != TheArmorStore->m_armorTemplates.end())
		TheArmorStore->m_beforeMapOverrides.push_back(std::make_pair(key, existing->second));
	if (ini->getLoadType() == INI_LOAD_MULTIFILE && existing == TheArmorStore->m_armorTemplates.end())
	{
		// BalanceReforged.ini rewrites armors objects already wear; a new name would reach nothing.
		// An Armor.ini without it: the block is read and dropped, as for a missing object.
		DEBUG_LOG(("[LINE: %d in '%s'] Armor '%s' is patched but was never defined; the block is skipped.\n", ini->getLineNum(), ini->getFilename().str(), c));
		ArmorTemplate scratch;
		ini->initFromINI(&scratch, myFieldParse);
		return;
	}
	ArmorTemplate& armorTmpl = TheArmorStore->m_armorTemplates[key];
	armorTmpl.clear();
	armorTmpl.m_name = c;
	ini->initFromINI(&armorTmpl, myFieldParse);
}

//-------------------------------------------------------------------------------------------------
/*static*/ void INI::parseArmorDefinition(INI *ini)
{
	ArmorStore::parseArmorDefinition(ini);
}

