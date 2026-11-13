// Cosmetic unit skins (TankSkin.h): the table SkinsReforged.ini fills, the loadout field in the slot
// string, and this player's own loadout.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/TankSkin.h"

#include "Common/INI.h"
#include "Common/PlayerList.h"
#include "GameNetwork/GameInfo.h"

#include <atomic>
#include <vector>

namespace
{
	struct SkinDef
	{
		Int id;
		AsciiString unit;									///< one skin a unit in a loadout
		std::vector<AsciiString> models;		///< the models it dresses, damaged states included
		std::vector<AsciiString> sheets;		///< pairs: the model's sheet, the one drawn in its place
	};

	const FieldParse theSkinFieldParse[] =
	{
		{ "Unit",			INI::parseAsciiString,							NULL, offsetof( SkinDef, unit ) },
		{ "Model",		INI::parseAsciiStringVectorAppend,	NULL, offsetof( SkinDef, models ) },
		{ "Texture",	INI::parseAsciiStringVectorAppend,	NULL, offsetof( SkinDef, sheets ) },
		{ NULL,				NULL,																NULL, 0 }
	};

	std::vector<SkinDef> theSkins;
	std::atomic<SkinLoadout> theLocalLoadout( 0 );		// set by the online session's thread, read by the lobby

	const SkinDef *findSkin( Int id )
	{
		for( size_t i = 0; i < theSkins.size(); ++i )
			if( theSkins[i].id == id )
				return &theSkins[i];
		return NULL;
	}

	// "AVCrusader_R.tga" and "avcrusader_r.dds" name one sheet
	Bool sameSheet( const char *a, const char *b )
	{
		const char *dotA = strrchr( a, '.' );
		const char *dotB = strrchr( b, '.' );
		const size_t lenA = dotA ? (size_t)( dotA - a ) : strlen( a );
		const size_t lenB = dotB ? (size_t)( dotB - b ) : strlen( b );
		return lenA == lenB && strncasecmp( a, b, lenA ) == 0;
	}

	SkinLoadout bit( Int id ) { return (SkinLoadout)1 << id; }
}

//-------------------------------------------------------------------------------------------------
/** Skin <id> ... End.  A second block for an id replaces the first. */
//-------------------------------------------------------------------------------------------------
void INI::parseSkinDefinition( INI *ini )
{
	const Int id = INI::scanInt( ini->getNextToken() );
	if( id < 1 || id > TANK_SKIN_MAX_ID )
		throw INI_INVALID_DATA;

	SkinDef skin;
	skin.id = id;
	ini->initFromINI( &skin, theSkinFieldParse );
	if( skin.unit.isEmpty() || skin.models.empty() || skin.sheets.empty() || ( skin.sheets.size() & 1 ) )
		throw INI_INVALID_DATA;

	for( size_t i = 0; i < theSkins.size(); ++i )
		if( theSkins[i].id == id )
		{
			theSkins[i] = skin;
			return;
		}
	theSkins.push_back( skin );
}

void TankSkin_clearTable( void )
{
	theSkins.clear();
}

Int TankSkin_forModel( SkinLoadout loadout, const char *modelName )
{
	if( loadout == 0 || modelName == NULL )
		return 0;
	for( Int id = 1; id <= TANK_SKIN_MAX_ID; ++id )
	{
		if( ( loadout & bit( id ) ) == 0 )
			continue;
		const SkinDef *skin = findSkin( id );
		if( skin == NULL )
			continue;		// a newer build's skin
		for( size_t m = 0; m < skin->models.size(); ++m )
			if( skin->models[m].compareNoCase( modelName ) == 0 )
				return id;
	}
	return 0;
}

const char *TankSkin_replacementSheet( Int id, const char *sheet )
{
	const SkinDef *skin = findSkin( id );
	if( skin == NULL || sheet == NULL )
		return NULL;
	for( size_t i = 0; i + 1 < skin->sheets.size(); i += 2 )
		if( sameSheet( skin->sheets[i].str(), sheet ) )
			return skin->sheets[i + 1].str();
	return NULL;
}

AsciiString TankSkin_slotField( SkinLoadout loadout )
{
	AsciiString field;
	for( Int id = 1; id <= TANK_SKIN_MAX_ID; ++id )
	{
		if( ( loadout & bit( id ) ) == 0 )
			continue;
		AsciiString one;
		one.format( field.isEmpty() ? ",K%d" : ".%d", id );
		field.concat( one );
	}
	return field;
}

SkinLoadout TankSkin_fromSlotField( const char *field )
{
	if( field == NULL || field[0] != 'K' )
		return 0;
	SkinLoadout loadout = 0;
	const char *p = field + 1;
	while( *p )
	{
		char *end = NULL;
		const long id = strtol( p, &end, 10 );
		if( end == p || id < 1 || id > TANK_SKIN_MAX_ID || ( *end != '.' && *end != 0 ) || ( *end == '.' && end[1] == 0 ) )
			return 0;		// a field no build wrote: none rather than half
		loadout |= bit( (Int)id );
		p = *end ? end + 1 : end;
	}
	return loadout;
}

SkinLoadout TankSkin_setLocalLoadout( const char *ids )
{
	SkinLoadout kept = 0;
	std::vector<AsciiString> unitsDressed;
	const char *p = ids ? ids : "";
	while( *p )
	{
		char *end = NULL;
		const long id = strtol( p, &end, 10 );
		if( end == p )
		{
			++p;		// a separator
			continue;
		}
		p = end;
		if( id < 1 || id > TANK_SKIN_MAX_ID )
			continue;
		// a skin this build's table lacks rides along and dresses nothing here
		if( const SkinDef *skin = findSkin( (Int)id ) )
		{
			Bool dressed = FALSE;
			for( size_t u = 0; u < unitsDressed.size(); ++u )
				dressed |= ( unitsDressed[u].compareNoCase( skin->unit ) == 0 );
			if( dressed )
				continue;
			unitsDressed.push_back( skin->unit );
		}
		kept |= bit( (Int)id );
	}
	theLocalLoadout.store( kept );
	return kept;
}

SkinLoadout TankSkin_localLoadout( void )
{
	return theLocalLoadout;
}

SkinLoadout TankSkin_ofPlayer( Int playerIndex )
{
	if( TheGameInfo == NULL || ThePlayerList == NULL )
		return 0;
	// set as the match starts and never changed in it, so the overlap window may read it too
	const Int slot = ThePlayerList->getSlotIndex( playerIndex );
	const GameSlot *seat = slot >= 0 ? TheGameInfo->getConstSlot( slot ) : NULL;
	return ( seat && seat->isHuman() ) ? seat->getSkins() : 0;
}
