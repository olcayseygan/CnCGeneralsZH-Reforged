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

// FILE: ChallengeGenerals.h //////////////////////////////////////////////////////////////////////
// Author: Steve Copeland, 6/24/2003
// Desc:   This is a manager for data pertaining to the Generals' Challenge personas and related GUI.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/ChallengeGenerals.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

ChallengeGenerals *TheChallengeGenerals = NULL;

ChallengeGenerals *createChallengeGenerals( void )
{
	return MSGNEW("ChallengeGenerals") ChallengeGenerals;
}


ChallengeGenerals::ChallengeGenerals()
{
	//ctor
}


ChallengeGenerals::~ChallengeGenerals()
{
	//dtor
}


void ChallengeGenerals::init( void )
{
	INI ini;
	ini.load( AsciiString( "Data\\INI\\ChallengeMode.ini" ), INI_LOAD_OVERWRITE, NULL );
}


void ChallengeGenerals::parseGeneralPersona(INI *ini, void *instance, void *store, const void *userData)
{
	static const FieldParse dataFieldParse[] = 
	{
		{ "StartsEnabled",			INI::parseBool, NULL, offsetof( GeneralPersona, m_bStartsEnabled ) },
		{ "BioNameString",			INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strBioName ) },
		{ "BioDOBString",			INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strBioDOB ) },
		{ "BioBirthplaceString",	INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strBioBirthplace ) },
		{ "BioStrategyString",		INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strBioStrategy ) },
		{ "BioRankString",			INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strBioRank ) },
		{ "BioBranchString",		INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strBioBranch ) },
		{ "BioClassNumberString",	INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strBioClassNumber ) },
		{ "BioPortraitSmall",		INI::parseMappedImage, NULL, offsetof( GeneralPersona, m_imageBioPortraitSmall ) },
		{ "BioPortraitLarge",		INI::parseMappedImage, NULL, offsetof( GeneralPersona, m_imageBioPortraitLarge ) },
		{ "Campaign",				INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strCampaign ) },
		{ "PlayerTemplate",			INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strPlayerTemplateName ) },
		{ "PortraitMovieLeftName",		INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strPortraitMovieLeftName ) },
		{ "PortraitMovieRightName",		INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strPortraitMovieRightName ) },
		{ "DefeatedImage",			INI::parseMappedImage, NULL, offsetof( GeneralPersona, m_imageDefeated ) },
		{ "VictoriousImage",		INI::parseMappedImage, NULL, offsetof( GeneralPersona, m_imageVictorious ) },
		{ "DefeatedString",			INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strDefeated ) },
		{ "VictoriousString",		INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strVictorious ) },
		{ "SelectionSound",			INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strSelectionSound ) },
		{ "TauntSound1",				INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strTauntSound1 ) },
		{ "TauntSound2",				INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strTauntSound2 ) },
		{ "TauntSound3",				INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strTauntSound3 ) },
		{ "WinSound",						INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strWinSound ) },
		{ "LossSound",					INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strLossSound ) },
		{ "PreviewSound",					INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strPreviewSound ) },
		{ "NameSound",					INI::parseAsciiString, NULL, offsetof( GeneralPersona, m_strNameSound ) },
		
		{ 0, 0, 0, 0 }
	};
	ini->initFromINI(store, dataFieldParse);
}

// The table's catch-all: a persona whose name the file misspells.  A mod's ChallengeMode.ini
// (Project Raptor) names one GeneraIPersona7, capital I, and stopped the game at start (v4.0.0
// reports 3897-3899, 4096, 4097).  Its trailing number still says which general it is; a name with
// none is read into a persona nobody keeps, so the parse stays in step either way.  A field with no
// "persona" in its name is not one, and is refused as EA refused it.
void ChallengeGenerals::parseMisnamedPersona(INI *ini, void *instance, void * /*store*/, const void *userData)
{
	const char *name = (const char *)userData;
	Int end = (Int)strlen( name );
	Bool namesPersona = FALSE;
	for( Int at = 0; at + 7 <= end && !namesPersona; ++at )
		namesPersona = strncasecmp( name + at, "persona", 7 ) == 0;
	if( !namesPersona )
		throw INI_UNKNOWN_TOKEN;
	Int digits = end;
	while( digits > 0 && name[ digits - 1 ] >= '0' && name[ digits - 1 ] <= '9' )
		--digits;
	const Int index = digits < end ? atoi( name + digits ) : -1;
	GeneralPersona unused;
	GeneralPersona *persona = ( index >= 0 && index < NUM_GENERALS ) ? &((ChallengeGenerals *)instance)->m_position[ index ] : &unused;
	DEBUG_LOG(( "[LINE: %d in '%s'] unknown persona '%s' read as GeneralPersona%d\n",
		ini->getLineNum(), ini->getFilename().str(), name, index ));
	parseGeneralPersona( ini, instance, persona, NULL );
}


const FieldParse ChallengeGenerals::s_fieldParseTable[] = 
{
	{ "GeneralPersona0", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[0] ) },
	{ "GeneralPersona1", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[1] ) },
	{ "GeneralPersona2", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[2] ) },
	{ "GeneralPersona3", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[3] ) },
	{ "GeneralPersona4", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[4] ) },
	{ "GeneralPersona5", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[5] ) },
	{ "GeneralPersona6", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[6] ) },
	{ "GeneralPersona7", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[7] ) },
	{ "GeneralPersona8", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[8] ) },
	{ "GeneralPersona9", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[9] ) },
	{ "GeneralPersona10", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[10] ) },
	{ "GeneralPersona11", ChallengeGenerals::parseGeneralPersona, NULL, offsetof( ChallengeGenerals, m_position[11] ) },
	{ 0, ChallengeGenerals::parseMisnamedPersona, NULL, 0 }	// catch-all, and the end of the table
};


//-------------------------------------------------------------------------------------------------
/** Parse Gen Challenge entries */
//-------------------------------------------------------------------------------------------------
void INI::parseChallengeModeDefinition( INI* ini )
{
	if( TheChallengeGenerals )
	{
		ini->initFromINI( TheChallengeGenerals, TheChallengeGenerals->getFieldParse() );
	}
}

const GeneralPersona* ChallengeGenerals::getPlayerGeneralByCampaignName( AsciiString name ) const 
{
	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		AsciiString campaignName = m_position[i].getCampaign();
		if (campaignName.compareNoCase( name.str() ) == 0)
			return &m_position[i];
	}
	DEBUG_ASSERTCRASH(NULL, ("Can't find General by Campaign Name"));
	return NULL;
}

const GeneralPersona* ChallengeGenerals::getGeneralByGeneralName( AsciiString name ) const
{
	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		AsciiString generalName = m_position[i].getBioName();
		if (generalName.compareNoCase( name.str() ) == 0)
			return &m_position[i];
	}
	return NULL;
}

const GeneralPersona* ChallengeGenerals::getGeneralByTemplateName( AsciiString name ) const
{
	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		AsciiString templateName = m_position[i].getPlayerTemplateName();
		if (templateName.compareNoCase( name.str() ) == 0)
			return &m_position[i];
	}
	return NULL;
}
