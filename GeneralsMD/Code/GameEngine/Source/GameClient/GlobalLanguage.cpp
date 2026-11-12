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

// FILE: GlobalLanguage.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Aug 2002
//
//	Filename: 	GlobalLanguage.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Contains the member functions for the language munkee
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"
#include "Platform/IsWindows9x.h"

#include "Common/INI.h"
#include "Common/Registry.h"
#include "GameClient/GlobalLanguage.h"
#if !defined(_WIN32)
#include "glyphrasteriser.h"
#include "posixpath.h"
#include <string>
#endif
#include "Common/FileSystem.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
GlobalLanguage *TheGlobalLanguageData = NULL;				///< The global language singalton

static const FieldParse TheGlobalLanguageDataFieldParseTable[] = 
{
	{ "UnicodeFontName",									INI::parseAsciiString,NULL,									offsetof( GlobalLanguage, m_unicodeFontName ) },
	//{	"UnicodeFontFileName",							INI::parseAsciiString,NULL,									offsetof( GlobalLanguage, m_unicodeFontFileName ) },
	{ "LocalFontFile",										GlobalLanguage::parseFontFileName,					NULL,			0},
	{ "MilitaryCaptionSpeed",						SPEED_FIELD( GlobalLanguage, m_militaryCaptionSpeed ) },
	{ "UseHardWordWrap",						INI::parseBool,					NULL,		offsetof( GlobalLanguage, m_useHardWrap) },
	{ "ResolutionFontAdjustment",						INI::parseReal,					NULL,		offsetof( GlobalLanguage, m_resolutionFontSizeAdjustment) },
	
	{ "CopyrightFont",					GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_copyrightFont ) },
	{ "MessageFont",					GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_messageFont) },					
	{ "MilitaryCaptionTitleFont",		GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_militaryCaptionTitleFont) },
	{ "MilitaryCaptionDelayMS",					INI::parseInt,					NULL,		offsetof( GlobalLanguage, m_militaryCaptionDelayMS ) },
	{ "MilitaryCaptionFont",			GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_militaryCaptionFont) },
	{ "SuperweaponCountdownNormalFont",	GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_superweaponCountdownNormalFont) },
	{ "SuperweaponCountdownReadyFont",	GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_superweaponCountdownReadyFont) },
	{ "NamedTimerCountdownNormalFont",	GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_namedTimerCountdownNormalFont) },
	{ "NamedTimerCountdownReadyFont",	GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_namedTimerCountdownReadyFont) },
	{ "DrawableCaptionFont",			GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_drawableCaptionFont) },
	{ "DefaultWindowFont",				GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_defaultWindowFont) },
	{ "DefaultDisplayStringFont",		GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_defaultDisplayStringFont) },
	{ "TooltipFontName",				GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_tooltipFontName) },
	{ "NativeDebugDisplay",				GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_nativeDebugDisplay) },
	{ "DrawGroupInfoFont",				GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_drawGroupInfoFont) },
	{ "CreditsTitleFont",				GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_creditsTitleFont) },
	{ "CreditsMinorTitleFont",				GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_creditsPositionFont) },
	{ "CreditsNormalFont",				GlobalLanguage::parseFontDesc,	NULL,	offsetof( GlobalLanguage, m_creditsNormalFont) },

	{ NULL,					NULL,						NULL,						0 }  // keep this last
};

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
void INI::parseLanguageDefinition( INI *ini )
{
	if( !TheGlobalLanguageData )
	{
		DEBUG_ASSERTCRASH(TheGlobalLanguageData, ("INI::parseLanguageDefinition - TheGlobalLanguage Data is not around, please create it before trying to parse the ini file."));
		return;
	}  // end if
	// parse the ini weapon definition
	ini->initFromINI( TheGlobalLanguageData, TheGlobalLanguageDataFieldParseTable );
}

/* The font files a language ships next to the game (Language.ini's LocalFontFile entries, kept in
	 m_localFonts).  On Windows they are installed for this process with AddFontResource and removed on
	 shutdown, and GDI draws every glyph from them.  Elsewhere FreeType draws them (D6): the file is
	 registered with the glyph rasteriser, which finds it by its family name before its substitution
	 table, as GDI finds an installed font before its font mapper does.  The name is the engine's spelling,
	 relative to the working directory as AddFontResource took it, and is resolved to the file on disk. */
static Bool installLocalFont( const AsciiString &font )
{
#if defined(_WIN32)
	return AddFontResource(font.str()) != 0;
#else
	std::string path;
	return PosixPath_Resolve( font.str(), POSIX_PATH_EXISTING, path ) && GlyphRasteriserClass::Register_Font_File( path.c_str() );
#endif
}

static void removeLocalFont( const AsciiString &font )
{
#if defined(_WIN32)
	RemoveFontResource(font.str());
#else
	std::string path;
	if (PosixPath_Resolve( font.str(), POSIX_PATH_EXISTING, path ))
		GlyphRasteriserClass::Unregister_Font_File( path.c_str() );
#endif
}

GlobalLanguage::GlobalLanguage()
{
	m_unicodeFontName.clear();
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_unicodeFontFileName.clear();
	m_unicodeFontName.clear();
	m_militaryCaptionSpeed.setRaw( 0 );
	m_useHardWrap = FALSE;
	m_resolutionFontSizeAdjustment = 0.7f;
	m_militaryCaptionDelayMS = 750;
	//End Add
}

GlobalLanguage::~GlobalLanguage()
{
	StringListIt it = m_localFonts.begin();
	while( it != m_localFonts.end())
	{
		AsciiString font = *it;
		removeLocalFont(font);
		//SendMessage( HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
		++it;
	}
}

void GlobalLanguage::init( void ) 
{

	INI ini;
	AsciiString fname;
	fname.format("Data\\%s\\Language.ini", GetRegistryLanguage().str());

	//GS NOTE: Must call doesFileExist in either case so that NameKeyGenerator will stay in sync
	AsciiString tempName;
	tempName.format("Data\\%s\\Language9x.ini", GetRegistryLanguage().str());
	bool isExist = TheFileSystem->doesFileExist(tempName.str());
	if (isWindows9x()  && isExist)
	{	//check if we're running Win9x variant since they may need different fonts
		fname = tempName;
	}


	ini.load( fname, INI_LOAD_OVERWRITE, NULL );

	StringListIt it = m_localFonts.begin();
	while( it != m_localFonts.end())
	{
		AsciiString font = *it;
		if(!installLocalFont(font))
		{
			DEBUG_ASSERTCRASH(FALSE,("GlobalLanguage::init Failed to add font %s", font.str()));
		}
		else
		{
			//SendMessage( HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
		}
		++it;
	}

	
}
void GlobalLanguage::reset( void ) {}


void GlobalLanguage::parseFontDesc(INI *ini, void *instance, void *store, const void* userData)
{
	FontDesc *fontDesc = (FontDesc *)store;
	fontDesc->name = ini->getNextQuotedAsciiString();
	fontDesc->size = ini->scanInt(ini->getNextToken());
	fontDesc->bold = ini->scanBool(ini->getNextToken());
}

void GlobalLanguage::parseFontFileName( INI *ini, void * instance, void *store, const void* userData )
{
	GlobalLanguage *monkey = (GlobalLanguage *)instance;
	AsciiString asciiString = ini->getNextAsciiString();
	monkey->m_localFonts.push_front(asciiString);
}

Int GlobalLanguage::adjustFontSize(Int theFontSize)
{
	const Int size = adjustFontSizeForScreen( theFontSize, TheGlobalData->m_xResolution,
																						TheGlobalData->m_yResolution,
																						m_resolutionFontSizeAdjustment );
	// EA's rate (Language.ini's damping) and EA's 2x cap
	if( size > theFontSize * 2 )
		return theFontSize * 2;
	return size;
}

Int GlobalLanguage::adjustFontSizeForScreen(Int theFontSize, Int screenWidth, Int screenHeight,
																						Real damping)
{
	//
	// The screen's size over the 800x600 everything was authored at, the smaller of the two axes -
	// the same number ControlBarUniformScale() hands the command bar, and for the same reason.  It
	// used to be the width alone, which is only the same thing at 4:3: on a 5120x1440 screen the
	// width says 6.4 where the height says 2.4, and the lettering came out twice the size the panel
	// under it had grown to.  A 1920x1080 shot and a 5120x1440 shot of the same bar have to
	// overlay, and they only do if the text is measured off the axis the layout was measured off.
	//
	Real adjustFactor = screenWidth/800.0f;
	if (screenHeight > 0)
	{
		const Real byHeight = screenHeight/600.0f;
		if (byHeight < adjustFactor) adjustFactor = byHeight;
	}
	adjustFactor = 1.0f + (adjustFactor-1.0f) * damping;
	if (adjustFactor<1.0f) adjustFactor = 1.0f;
	//
	// the window layouts themselves are stretched by the full resolution ratio, so a font ceiling
	// of 2x meant that from about 1950 pixels wide up the text stopped growing while the panel it
	// sits in kept going - at 2560 a command bar three times the size of its 800x600 original wore
	// 8pt letters.  The damping factor keeps the text at or below the layout's own scale (init
	// sets 1, the layout's), so the ceiling only has to be high enough not to bite before 4K.
	//
	if (adjustFactor>4.0f) adjustFactor = 4.0f;
	Int pointSize = REAL_TO_INT_FLOOR(theFontSize*adjustFactor);
	return pointSize;
}

FontDesc::FontDesc(void)
{
	name = "Arial Unicode MS";	///<name of font
	size = 12;			///<point size
	bold = FALSE;			///<is bold?
}
//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

