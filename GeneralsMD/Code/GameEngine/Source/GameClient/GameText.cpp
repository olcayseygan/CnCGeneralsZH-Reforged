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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: GameText.cpp
//
// Created:   11/07/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/WideCharFns.h"

#include "GameClient/GameText.h"
#include "GameClient/ApplicationWindowTitle.h"
#include "Common/Language.h"
#include "Common/Registry.h"
#include "GameClient/LanguageFilter.h"
#include "Common/Debug.h"
#include "Common/UnicodeString.h"
#include "Common/AsciiString.h"
#include "Common/GlobalData.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include <map>
#include <string>


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------

#if defined(_DEBUG) || defined(_INTERNAL)
Bool g_useStringFile = TRUE;
#endif


//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------

#define CSF_ID ( ('C'<<24) | ('S'<<16) | ('F'<<8) | (' ') )
#define CSF_LABEL ( ('L'<<24) | ('B'<<16) | ('L'<<8) | (' ') )
#define CSF_STRING ( ('S'<<24) | ('T'<<16) | ('R'<<8) | (' ') )
#define CSF_STRINGWITHWAVE ( ('S'<<24) | ('T'<<16) | ('R'<<8) | ('W') )
#define CSF_VERSION 3

#define STRING_FILE 0
#define CSF_FILE 1
#define MAX_UITEXT_LENGTH (10*1024)
//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------

//===============================
// StringInfo 
//===============================

struct StringInfo
{
	AsciiString			label;
	UnicodeString		text;
	AsciiString			speech;
};

struct StringLookUp
{
	AsciiString		*label;
	StringInfo		*info;
};

/// one -modfactions mod's own labels: info owns the strings, lut is sorted by compareLUT
struct ModStringTable
{
	StringInfo		*info;
	StringLookUp	*lut;
	Int						count;
};

//===============================
// CSFHeader 
//===============================

struct CSFHeader
{
	Int id;
	Int version;
	Int num_labels;
	Int num_strings;
	Int skip;
	Int langid;

};

//===============================
// struct NoString 
//===============================

struct NoString
{
	struct NoString *next;
	UnicodeString text;
};


//===============================
// GameTextManager 
//===============================

class GameTextManager : public GameTextInterface
{
	public:

		GameTextManager();
		virtual ~GameTextManager();

		virtual void					init( void );						///< Initlaizes the text system
		virtual void					deinit( void );					///< De-initlaizes the text system
		virtual void					update( void ) {};			///< update text manager
		virtual void					reset( void );					///< Resets the text system

		virtual UnicodeString fetch( const Char *label, Bool *exists = NULL );		///< Returns the associated labeled unicode text
		virtual UnicodeString fetch( AsciiString label, Bool *exists = NULL );		///< Returns the associated labeled unicode text
		virtual AsciiStringVec& getStringsWithLabelPrefix(AsciiString label);

		virtual void					initMapStringFile( const AsciiString& filename );
		virtual WideChar			fetchEnglishHotKey( const AsciiString& label );
		virtual void					loadModStrings( Int mod );
		virtual Bool					hasModLabel( Int mod, const char *label ) const;

	protected:

		/// -modfactions: each mod's own labels (English CSF and STR), by mod index; empty with no full -mod
		std::vector< ModStringTable > m_modStrings;
		/// each mod's CSF in the player's language, text only: never consulted for the label set
		std::vector< ModStringTable > m_modLangStrings;
		/// the mod index of a "M<n>~" label with a loaded table, or -1
		Int							modLabelIndex( const char *label ) const;
		void						readModTable( ModStringTable &table, const char *csf, const char *str );
		/// the mod's text for label: its language's CSF first, then its English CSF and STR
		StringLookUp		*findModText( Int mod, const char *label ) const;

		/// label (lower case) -> its English hotkey letter, filled once the table is read
		std::map< std::string, WideChar > m_englishHotKeys;

		Int							m_textCount;
		Int							m_maxLabelLen;
		Char						m_buffer[MAX_UITEXT_LENGTH];
		Char						m_buffer2[MAX_UITEXT_LENGTH];
		Char						m_buffer3[MAX_UITEXT_LENGTH];
		WideChar				m_tbuffer[MAX_UITEXT_LENGTH*2];
		
		StringInfo			*m_stringInfo;
		StringLookUp		*m_stringLUT;
		Bool						m_initialized;
#if defined(_DEBUG) || defined(_INTERNAL)
		Bool						m_jabberWockie;
		Bool						m_munkee;
#endif
		NoString				*m_noStringList;
		Int							m_useStringFile;
		LanguageID			m_language;
		UnicodeString		m_failed;

		StringInfo			*m_mapStringInfo;
		StringLookUp		*m_mapStringLUT;
		Int							m_mapTextCount;

		/// m_asciiStringVec will be altered every time that getStringsWithLabelPrefix is called,
		/// so don't simply store a pointer to it.
		AsciiStringVec			m_asciiStringVec;

		void						stripSpaces ( WideChar *string );
		void						removeLeadingAndTrailing ( Char *m_buffer );
		void						readToEndOfQuote( File *file, Char *in, Char *out, Char *wavefile, Int maxBufLen );
		void						reverseWord ( Char *file, Char *lp );
		void						translateCopy( WideChar *outbuf, Char *inbuf );
		Bool						getStringCount( const Char *filename, Int& textCount );
		Bool						getCSFInfo ( const Char *filename );
		Bool						parseCSF(  const Char *filename );
		Bool						parseStringFile( const char *filename );
		Bool						parseMapStringFile( const char *filename );
		Bool						readLine( char *buffer, Int max, File *file );
		Char						readChar( File *file );
};

static int _cdecl			compareLUT ( const void *,  const void*);

static StringLookUp *findModLabel( const ModStringTable &table, const char *label )
{
	AsciiString lb = label;
	StringLookUp key;
	key.info = NULL;
	key.label = &lb;
	return (StringLookUp *) bsearch( &key, (void*) table.lut, table.count, sizeof(StringLookUp), compareLUT );
}
//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------

GameTextInterface *TheGameText = NULL;

// The translation each TextLanguageType lays over the CSF.  English is the CSF itself.  Loose
// files, which the build copies from Code/Data into Run/Data.
static const char *const TheTextLanguageOverlays[ TEXT_LANGUAGE_COUNT ] =
{
	NULL,
	"Data\\Turkish\\Generals.str",
	"Data\\German\\Generals.str",
	"Data\\Ukrainian\\Generals.str",
	"Data\\Russian\\Generals.str",
	"Data\\French\\Generals.str",
};

//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------

//============================================================================
// CreateGameTextInterface
//============================================================================

GameTextInterface* CreateGameTextInterface( void )
{
	return NEW GameTextManager;
}


//============================================================================
// GameTextManager::GameTextManager
//============================================================================

GameTextManager::GameTextManager()
:	m_textCount(0),
	m_maxLabelLen(0),
	m_stringInfo(NULL),
	m_stringLUT(NULL),
	m_initialized(FALSE),
	m_noStringList(NULL),
#if defined(_DEBUG) || defined(_INTERNAL)
	m_jabberWockie(FALSE),
	m_munkee(FALSE),
	m_useStringFile(g_useStringFile),
#else
	m_useStringFile(TRUE),
#endif
	m_mapStringInfo(NULL),
	m_mapStringLUT(NULL),
	m_failed(u"***FATAL*** String Manager failed to initilaize properly")
{
	// Added By Sadullah Nader
	// Initializations missing and needed

	for(Int i=0; i < MAX_UITEXT_LENGTH; i++)
	{
		m_buffer[i] = 0; 
		m_buffer2[i] = 0; 
		m_buffer3[i] = 0; 
	}
	//
}

//============================================================================
// GameTextManager::~GameTextManager
//============================================================================

GameTextManager::~GameTextManager()
{
	deinit();
}							

//============================================================================
// GameTextManager::init
//============================================================================

extern const Char *g_strFile;
extern const Char *g_csfFile;

#if !defined(_WIN32)
ApplicationWindowTitleHook TheApplicationWindowTitleHook = NULL;
#endif

/* Names the game's window.  On Windows that is ApplicationHWnd, set here as it always was.  Elsewhere
	 the window is the platform layer's (C2's SdlGameEngine), which names it through the hook it sets;
	 with no window, as in a headless run, there is nothing to name. */
static void setApplicationWindowTitle( const UnicodeString &ourName, const AsciiString &ourNameA )
{
#if defined(_WIN32)
	extern HWND ApplicationHWnd;  ///< our application window handle
	if (ApplicationHWnd) {
		//Set it twice because Win 9x does not support SetWindowTextW.
		::SetWindowText(ApplicationHWnd, ourNameA.str());
		// Win32's W API: WideChar and WCHAR are the same two bytes on Windows, which makes the cast honest.
		::SetWindowTextW(ApplicationHWnd, reinterpret_cast<LPCWSTR>(ourName.str()));
	}
#else
	if (TheApplicationWindowTitleHook != NULL)
	{
		char title[ 512 ];
		WideCharToUtf8( ourName.str(), title, sizeof( title ) );
		TheApplicationWindowTitleHook( title );
	}
	(void)ourNameA;
#endif
}

void GameTextManager::init( void )
{
	AsciiString csfFile;
	csfFile.format(g_csfFile, GetRegistryLanguage().str());
	Int format;

	if ( m_initialized )
	{
		return;
	}

	m_initialized = TRUE;

	m_maxLabelLen = 0;
#if defined(_DEBUG) || defined(_INTERNAL)
	if(TheGlobalData)
	{
		m_jabberWockie = TheGlobalData->m_jabberOn;
		m_munkee = 	TheGlobalData->m_munkeeOn;
	}
#endif

	if ( m_useStringFile && getStringCount( g_strFile, m_textCount ) )
	{
		format = STRING_FILE;
	}
	else if ( getCSFInfo ( csfFile.str() ) )
	{
		format = CSF_FILE;
	}
	else
	{
		return;
	}

	if( (m_textCount == 0) )
	{
		return;
	}

	// The 1.04 patch ships its two new menu strings as a text overlay - Data\Patch.str
	// inside PatchData.big - instead of an updated CSF. The retail 1.04 exe reads it;
	// this source drop predates that, so without it the patched MainMenu.wnd shows
	// MISSING: 'GUI:CustomMission'. Parse it into the tail of the string table.
	//
	// The player's text language is a second overlay after it, so a translation overrules the
	// CSF and this fork's own English lines in Patch.str alike, and a line it does not carry
	// stays English rather than turning into a key name.
	enum { OVERLAY_COUNT = 2 };
	const Int language = TheGlobalData ? TheGlobalData->m_textLanguage : TEXT_LANGUAGE_ENGLISH;
	const char *overlays[ OVERLAY_COUNT ] =
	{
		"Data\\Patch.str",
		( language > TEXT_LANGUAGE_ENGLISH && language < TEXT_LANGUAGE_COUNT ) ? TheTextLanguageOverlays[ language ] : NULL,
	};
	Int overlayCounts[ OVERLAY_COUNT ] = { 0, 0 };
	Int overlayTotal = 0;
	for ( Int overlay = 0; overlay < OVERLAY_COUNT; ++overlay )
	{
		if ( overlays[ overlay ] == NULL || !getStringCount( overlays[ overlay ], overlayCounts[ overlay ] ) )
		{
			overlayCounts[ overlay ] = 0;
		}
		overlayTotal += overlayCounts[ overlay ];
	}
	Int mainCount = m_textCount;

	//Allocate StringInfo Array

	m_stringInfo = NEW StringInfo[m_textCount + overlayTotal];

	if( m_stringInfo == NULL )
	{
		deinit();
		return;
	}

	if ( format == STRING_FILE )
	{
		if( parseStringFile( g_strFile ) == FALSE )
		{
			deinit();
			return;
		}
	}
	else
	{
		if ( !parseCSF ( csfFile.str() ) )
		{
			deinit();
			return;
		}
	}

	// parseStringFile fills m_stringInfo from index 0, so point it at each overlay's slice of
	// the tail in turn and restore afterwards.
	StringInfo *wholeTable = m_stringInfo;
	Int filled = mainCount;
	Int englishEnd = mainCount;		// the CSF and Patch.str: everything before the translation
	for ( Int overlay = 0; overlay < OVERLAY_COUNT; ++overlay )
	{
		if ( overlayCounts[ overlay ] == 0 )
		{
			continue;
		}
		m_stringInfo = wholeTable + filled;
		if ( parseStringFile( overlays[ overlay ] ) )
		{
			filled += overlayCounts[ overlay ];
		}
		if ( overlay == 0 )
			englishEnd = filled;
	}
	m_stringInfo = wholeTable;
	m_textCount = filled;

	// Classic's command keys are the English labels' letters whatever language the text is in, so
	// every player presses the same key.  Read in table order, so Patch.str overrules the CSF.
	m_englishHotKeys.clear();
	for ( Int i = 0; i < englishEnd; i++ )
	{
		const WideChar *amp = m_stringInfo[ i ].text.str();
		while ( amp && *amp && *amp != u'&' )
			amp++;
		if ( amp == NULL || *amp == 0 || amp[ 1 ] <= u' ' )
			continue;
		AsciiString label = m_stringInfo[ i ].label;
		label.toLower();
		m_englishHotKeys[ label.str() ] = amp[ 1 ];
	}

	m_stringLUT = NEW StringLookUp[m_textCount];

	StringLookUp *lut = m_stringLUT;
	StringInfo *info = m_stringInfo;

	for ( Int i = 0; i < m_textCount; i++ )
	{
		lut->info = info;
		lut->label = &info->label;
		lut++;
		info++;
	}

	qsort( m_stringLUT, m_textCount, sizeof(StringLookUp), compareLUT  );

	// An overlay that cannot overrule what came before it is half an overlay: a label two files
	// carry has two entries here and bsearch takes whichever qsort happened to leave first. The
	// table was filled CSF first and each overlay after it, so the entry further along the array
	// is the one to keep.
	if ( filled > mainCount && m_textCount > 1 )
	{
		Int kept = 1;
		for ( Int i = 1; i < m_textCount; i++ )
		{
			// strcasecmp, the same comparison compareLUT sorts and bsearch searches with
			if ( m_stringLUT[i].label->compareNoCase( m_stringLUT[kept - 1].label->str() ) == 0 )
			{
				if ( m_stringLUT[i].info > m_stringLUT[kept - 1].info )
					m_stringLUT[kept - 1] = m_stringLUT[i];
				continue;
			}
			m_stringLUT[kept] = m_stringLUT[i];
			kept++;
		}
		m_textCount = kept;
	}

	UnicodeString ourName = fetch("GUI:Command&ConquerGenerals");
	AsciiString ourNameA;
	ourNameA.translate(ourName);	//get ASCII version for Win 9x

	setApplicationWindowTitle( ourName, ourNameA );

}

//============================================================================
// GameTextManager::deinit
//============================================================================

void GameTextManager::deinit( void )
{

	if( m_stringInfo != NULL )
	{
		delete [] m_stringInfo;
		m_stringInfo = NULL;
	}

	if( m_stringLUT != NULL )
	{
		delete [] m_stringLUT;
		m_stringLUT = NULL;
	}

	m_textCount = 0;

	for ( size_t mod = 0; mod < m_modStrings.size(); ++mod )
	{
		delete [] m_modStrings[ mod ].info;
		delete [] m_modStrings[ mod ].lut;
		delete [] m_modLangStrings[ mod ].info;
		delete [] m_modLangStrings[ mod ].lut;
	}
	m_modStrings.clear();
	m_modLangStrings.clear();

	NoString *noString = m_noStringList;

	DEBUG_LOG(("\n*** Missing strings ***\n"));
	while ( noString )
	{
		DEBUG_LOG(("*** %s ***\n", WideCharAsUtf8( noString->text.str() ).str()));
		NoString *next = noString->next;
		delete noString;
		noString = next;
	}
	DEBUG_LOG(("*** End missing strings ***\n\n"));

	m_noStringList = NULL;

	m_initialized = FALSE;
}

//============================================================================
// GameTextManager::reset
//============================================================================

void GameTextManager::reset( void )
{
	if( m_mapStringInfo != NULL )
	{
		delete [] m_mapStringInfo;
		m_mapStringInfo = NULL;
	}

	if( m_mapStringLUT != NULL )
	{
		delete [] m_mapStringLUT;
		m_mapStringLUT = NULL;
	}
}


//============================================================================
// GameTextManager::stripSpaces 
//============================================================================

void GameTextManager::stripSpaces ( WideChar *string )
{
	WideChar *str, *ptr;
	WideChar ch, last = 0;
	Int skipall = TRUE;

	str = ptr = string;

	while ( (ch = *ptr++) != 0 )
	{
		if ( ch == ' '  )
		{
			if ( last == ' ' || skipall )
			{
				continue;
			}
		}

		if ( ch == '\n' || ch == '\t' )
		{
				// remove last space
				if ( last == ' ' )
				{
					str--;
				}

				skipall = TRUE;		// skip all spaces
				last = *str++ = ch;
				continue;
		}

		last = *str++ = ch;
		skipall = FALSE;
	}

	if ( last == ' ' )
	{
		str--;
	}

	*str = 0;
}

//============================================================================
// GameTextManager::removeLeadingAndTrailing 
//============================================================================

void GameTextManager::removeLeadingAndTrailing ( Char *buffer )
{
	Char *first, *ptr;
	Char ch;

	ptr = first = buffer;

	while ( (ch = *first) != 0 && WideCharIsSpace( ch ))
	{
			first++;
	}

	while ( (*ptr++ = *first++) != 0 );

	ptr -= 2;;

	while ( (ptr > buffer) && (ch = *ptr) != 0 && WideCharIsSpace( ch ) )
	{
		ptr--;
	}

	ptr++;
	*ptr = 0;
}

//============================================================================
// GameTextManager::readToEndOfQuote
//============================================================================

void GameTextManager::readToEndOfQuote( File *file, Char *in, Char *out, Char *wavefile, Int maxBufLen )
{
	Int slash = FALSE;
	Int state = 0;
	Int line_start = FALSE;
	Char ch;
	Int ccount = 0;
	Int len = 0;
	Int done = FALSE;

	while ( maxBufLen )
	{
		// get next Char

		if ( in )
		{
			if ( (ch = *in++) == 0 )
			{
				in = NULL; // have exhausted the input m_buffer
				ch = readChar ( file );
			}
		}
		else
		{
			ch = readChar ( file );
		}

		if ( ch == EOF )
		{
			return ;
		}

		if ( ch == '\n' )
		{
			line_start = TRUE;
			slash = FALSE;
			ccount = 0;
			ch = ' ';
		}
		else if ( ch == '\\' && !slash)
		{
			slash = TRUE;
		}
		else if ( ch == '\\' && slash)
		{
			slash = FALSE;
		}
		else if ( ch == '"' && !slash )
		{
			break; // done
		}
		else
		{
			slash = FALSE;
		}

		if ( WideCharIsSpace( ch ))
		{
			ch = ' ';
		}

		*out++ = ch;
		maxBufLen--;
	}

	*out = 0;

	while ( !done )
	{
		// get next Char

		if ( in )
		{
			if ( (ch = *in++) == 0 )
			{
				in = NULL; // have exhausted the input m_buffer
				ch = readChar ( file );
			}
		}
		else
		{
			ch = readChar ( file );
		}

		if ( ch == '\n' || ch == EOF )
		{
			break;
		}

		switch ( state )
		{

			case 0:
				if ( WideCharIsSpace( ch ) || ch == '=' )
				{
					break;
				}

				state = 1;
			case 1:
				if ( ( ch >= 'a' && ch <= 'z') || ( ch >= 'A' && ch <='Z') || (ch >= '0' && ch <= '9') || ch == '_' )
				{
					*wavefile++ = ch;
					len++;
					break;
				}
				state = 2;
			case 2:
				break;
		}
	}

	*wavefile = 0;

	if ( len )
	{
		if ( ( ch = *(wavefile-1)) >= '0' && ch <= '9' )
		{
			*wavefile++ = 'e';
			*wavefile = 0;
		}
	}

}


//============================================================================
// GameTextManager::reverseWord 
//============================================================================

void GameTextManager::reverseWord ( Char *file, Char *lp )
{
	Int first = TRUE;
	Char f, l;
	Int ok = TRUE	;

	while ( ok )
	{
		if ( file >= lp )
		{
			return;
		}

		f = *file;
		l = *lp;

		if ( first )
		{
			if ( f >= 'A' && f <= 'Z' )
			{
				if ( l >= 'a' && l <= 'z' )
				{
					f = (f - 'A') + 'a';
					l = (l - 'a') + 'A';
				}
			}

			first = FALSE;
		}

		*lp-- = f;
		*file++ = l;

	}

}

//============================================================================
// GameTextManager::translateCopy
//============================================================================

void GameTextManager::translateCopy( WideChar *outbuf, Char *inbuf )
{
	Int slash = FALSE;

#if defined(_DEBUG) || defined(_INTERNAL)
	if ( m_jabberWockie )
	{
		static Char buffer[MAX_UITEXT_LENGTH*2];
		Char *firstLetter = NULL, *lastLetter;
		Char *b = buffer;
		Int formatWord = FALSE;
		Char ch;

		while ( (ch = *inbuf++) != 0 )
		{
			if ( ! (( ch >= 'a' && ch <= 'z') || ( ch >= 'A' && ch <= 'Z' )))
			{
				if ( firstLetter )
				{
					if ( !formatWord )
					{
						lastLetter = b-1;
						reverseWord ( firstLetter, lastLetter );
					}
					firstLetter = NULL;
					formatWord = FALSE;
				}
				*b++ = ch;
				if ( ch == '\\' )
				{
					*b++ = *inbuf++;
				}
				if ( ch == '%' )
				{
					while ( (ch = *inbuf++) != 0 && !( (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))
					{
						*b++ = ch;
					}
					*b++ = ch;
				}
			}
			else
			{
				if ( !firstLetter )
				{
					firstLetter = b;
				}

				*b++ = ch;

			}
		}

		if ( firstLetter )
		{
			lastLetter = b-1;
			reverseWord ( firstLetter, lastLetter );
		}

		*b++ = 0;
		inbuf = buffer;
	}
	else if( m_munkee )
	{
		WideCharCpy(outbuf, u"Munkee");
		return;
	}
#endif

	while( *inbuf != '\0' )
	{
		if( slash == TRUE )
		{
			slash = FALSE;

			switch( *inbuf )
			{
				// in case end of string is reached
				// should never happen!!!
				case '\0':
					return;

				case '\\':
					*outbuf++ = '\\';
					break;

				case '\'':
					*outbuf++ = '\'';
					break;

				case '\"':
					*outbuf++ = '\"';
					break;

				case '\?':
					*outbuf++ = '\?';
					break;

				case 't':
					*outbuf++ = '\t';
					break;

				case 'n':
					*outbuf++ = '\n';
					break;

				default:
					*outbuf++ = *inbuf & 0x00FF;
					break;
			}
		}
		else if( *inbuf != '\\' )
		{
			// the loop's own inbuf++ takes the last byte of the character
			inbuf += decodeStringFileCharacter( (const unsigned char *)inbuf, outbuf ) - 1;
			outbuf++;
		}
		else
			slash = TRUE;

		inbuf++;
	}
	*outbuf= 0;
}

//============================================================================
// GameTextManager::getStringCount
//============================================================================

Bool GameTextManager::getStringCount( const char *filename, Int& textCount )
{
	Int ok = TRUE;

	textCount = 0;

	File *file;
	file = TheFileSystem->openFile(filename, File::READ | File::TEXT);
	DEBUG_LOG(("Looking in %s for string file\n", filename));
	
	if ( file == NULL )
	{
		return FALSE;
	}

	while(ok)
	{
		if( !readLine( m_buffer, sizeof( m_buffer) -1, file ) )
			break;
		removeLeadingAndTrailing ( m_buffer );

		if( m_buffer[0] == '"' )
		{
				Int len = strlen(m_buffer);
				m_buffer[ len ] = '\n';
				m_buffer[ len+1] = 0;
			readToEndOfQuote( file, &m_buffer[1], m_buffer2, m_buffer3, MAX_UITEXT_LENGTH );
		}
		else if( !strcasecmp( m_buffer, "END") )
		{
			textCount++;
		}
	}

	textCount += 500;
	file->close();
	file = NULL;
	return TRUE;
}

//============================================================================
// GameTextManager::getCSFInfo 
//============================================================================

Bool GameTextManager::getCSFInfo ( const Char *filename )
{
	CSFHeader header;
	Int ok = FALSE;
	File *file = TheFileSystem->openFile(filename, File::READ | File::BINARY);
	DEBUG_LOG(("Looking in %s for compiled string file\n", filename));

	if ( file != NULL )
	{
		if ( file->read( &header, sizeof ( header )) == sizeof ( header ) )
		{
			if ( header.id == CSF_ID )
			{
				m_textCount = header.num_labels;

				if ( header.version >= 2 )
				{
					m_language = (LanguageID) header.langid;
				}
				else
				{
					m_language = LANGUAGE_ID_US;
				}

				ok = TRUE;
			}
		}

		file->close();
		file = NULL;
	}

	return ok;
}

//============================================================================
// GameTextManager::parseCSF
//============================================================================

Bool GameTextManager::parseCSF( const Char *filename )
{
	File *file;
	Int id;
	Int len;
	Int listCount = 0;
	Bool ok = FALSE;
	CSFHeader header;

	file = TheFileSystem->openFile(filename, File::READ | File::BINARY);

	if ( file == NULL )
	{
		return FALSE;
	}

	if (  file->read ( &header, sizeof ( CSFHeader)) != sizeof ( CSFHeader) )
	{
		return FALSE;
	}

	while( file->read ( &id, sizeof (id)) == sizeof ( id) )
	{
		Int num;
		Int num_strings;

		if ( id != CSF_LABEL )
		{
			goto quit;
		}

		file->read ( &num_strings, sizeof ( Int ));

		file->read ( &len, sizeof ( Int ) );

		if ( len )
		{
			file->read ( m_buffer, len );
		}

		m_buffer[len] = 0;

		m_stringInfo[listCount].label = m_buffer;


		if ( len > m_maxLabelLen )
		{
			m_maxLabelLen = len;
		}

		num = 0;

		while ( num < num_strings )
		{
		 	file->read ( &id, sizeof ( Int ) );

			if ( id != CSF_STRING && id != CSF_STRINGWITHWAVE )
			{
				goto quit;
			}

		 	file->read ( &len, sizeof ( Int ) );

			if ( len )
			{
				file->read ( m_tbuffer, len*sizeof(WideChar) );
			}

			if ( num == 0 )
			{
				// only use the first string found
				m_tbuffer[len] = 0;
				
				{
					WideChar *ptr;
				
					ptr = m_tbuffer;
				
					while ( *ptr )
					{
						*ptr = ~*ptr;
						ptr++;
					}
				}
				
				stripSpaces ( m_tbuffer );
				m_stringInfo[listCount].text = m_tbuffer;
			}

			if ( id == CSF_STRINGWITHWAVE )
			{
			 	file->read ( &len, sizeof ( Int ) );
				if ( len )
				{
					file->read ( m_buffer, len );
				}
				m_buffer[len] = 0;

				if ( num == 0 && len )
				{
					// only use the first string found
					m_stringInfo[listCount].speech = m_buffer;
				}

			}

			num++;
		}

		listCount++;
	}

	ok = TRUE;

quit:

	file->close();
	file = NULL;

	return ok;
}


//============================================================================
// GameTextManager::parseStringFile
//============================================================================

Bool GameTextManager::parseStringFile( const char *filename )
{
	Int listCount = 0;
	Int ok = TRUE;

	File *file = TheFileSystem->openFile(filename, File::READ | File::TEXT);
	
	if ( file == NULL )
	{
		return FALSE;
	}

	while( ok )
	{
		Int len;
		if( !readLine( m_buffer, MAX_UITEXT_LENGTH, file ))
		{
			break;
		}

		removeLeadingAndTrailing ( m_buffer );

		if( ( *(unsigned short *)m_buffer == 0x2F2F) || !m_buffer[0])			//	0x2F2F is Hex for //
			continue;

		// make sure label is unique

		for ( Int i = 0; i < listCount; i++ )
		{
			if ( !strcasecmp ( m_stringInfo[i].label.str(), m_buffer ))
			{
				DEBUG_ASSERTCRASH ( FALSE, ("String label '%s' multiply defined!", m_buffer ));
			}
		}

		m_stringInfo[listCount].label = m_buffer;
		len = strlen ( m_buffer );


		if ( len > m_maxLabelLen )
		{
			m_maxLabelLen = len;
		}

		Bool readString = FALSE;
		while( ok )
		{
			if (!readLine ( m_buffer, sizeof(m_buffer)-1, file ))
			{
				DEBUG_ASSERTCRASH (FALSE, ("Unexpected end of string file"));
				ok = FALSE;
				goto quit;
			}

			removeLeadingAndTrailing ( m_buffer );

			if( m_buffer[0] == '"' )
			{
				len = strlen(m_buffer);
				m_buffer[ len ] = '\n';
				m_buffer[ len+1] = 0;
				readToEndOfQuote( file, &m_buffer[1], m_buffer2, m_buffer3, MAX_UITEXT_LENGTH );


				if ( readString )
				{
					// only one string per label allows
						DEBUG_ASSERTCRASH ( FALSE, ("String label '%s' has more than one string defined!", m_stringInfo[listCount].label.str()));
				}
				else
				{
					// Copy string into new home
					translateCopy( m_tbuffer, m_buffer2 );
					stripSpaces ( m_tbuffer );
					
					m_stringInfo[listCount].text = m_tbuffer ;
					m_stringInfo[listCount].speech = m_buffer3;
					readString = TRUE;
				}
			}
			else if ( !strcasecmp ( m_buffer, "END" ))
			{
				break;
			}
		}

		listCount++;
	}

quit:

	file->close();
	file = NULL;

	return ok;
}

//============================================================================
// GameTextManager::initMapStringFile
//============================================================================

void GameTextManager::initMapStringFile( const AsciiString& filename )
{
	m_mapTextCount = 0;
	getStringCount( filename.str(), m_mapTextCount );

	m_mapStringInfo = NEW StringInfo[m_mapTextCount];

	parseMapStringFile( filename.str() );

	m_mapStringLUT = NEW StringLookUp[m_mapTextCount];

	StringLookUp *lut = m_mapStringLUT;
	StringInfo *info = m_mapStringInfo;

	for ( Int i = 0; i < m_mapTextCount; i++ )
	{
		lut->info = info;
		lut->label = &info->label;
		lut++;
		info++;
	}

	qsort( m_mapStringLUT, m_mapTextCount, sizeof(StringLookUp), compareLUT  );
}

//============================================================================
// GameTextManager::parseMapStringFile
//============================================================================

Bool GameTextManager::parseMapStringFile( const char *filename )
{
	Int listCount = 0;
	Int ok = TRUE;

	File *file;

	file = TheFileSystem->openFile(filename, File::READ | File::TEXT);	
	if ( file == NULL )
	{
		return FALSE;
	}

	while( ok )
	{
		Int len;
		if( !readLine( m_buffer, MAX_UITEXT_LENGTH, file ))
		{
			break;
		}

		removeLeadingAndTrailing ( m_buffer );

		if( ( *(unsigned short *)m_buffer == 0x2F2F) || !m_buffer[0])			//	0x2F2F is Hex for //
			continue;

		// make sure label is unique

		for ( Int i = 0; i < listCount; i++ )
		{
			if ( !strcasecmp ( m_mapStringInfo[i].label.str(), m_buffer ))
			{
				DEBUG_ASSERTCRASH ( FALSE, ("String label '%s' multiply defined!", m_buffer ));
			}
		}

		m_mapStringInfo[listCount].label = m_buffer;
		len = strlen ( m_buffer );


		if ( len > m_maxLabelLen )
		{
			m_maxLabelLen = len;
		}

		Bool readString = FALSE;
		while( ok )
		{
			if (!readLine ( m_buffer, sizeof(m_buffer)-1, file ))
			{
				DEBUG_ASSERTCRASH (FALSE, ("Unexpected end of string file"));
				ok = FALSE;
				goto quit;
			}

			removeLeadingAndTrailing ( m_buffer );

			if( m_buffer[0] == '"' )
			{
				len = strlen(m_buffer);
				m_buffer[ len ] = '\n';
				m_buffer[ len+1] = 0;
				readToEndOfQuote( file, &m_buffer[1], m_buffer2, m_buffer3, MAX_UITEXT_LENGTH );


				if ( readString )
				{
					// only one string per label allowed
						DEBUG_ASSERTCRASH ( FALSE, ("String label '%s' has more than one string defined!", m_stringInfo[listCount].label.str()));
				}
				else
				{
					// Copy string into new home
					translateCopy( m_tbuffer, m_buffer2 );
					stripSpaces ( m_tbuffer );

					UnicodeString text = UnicodeString(m_tbuffer);
					if (TheLanguageFilter)
						TheLanguageFilter->filterLine(text);
					
					m_mapStringInfo[listCount].text = text;
					m_mapStringInfo[listCount].speech = m_buffer3;
					readString = TRUE;
				}
			}
			else if ( !strcasecmp ( m_buffer, "END" ))
			{
				break;
			}
		}

		listCount++;
	}

quit:

	file->close();
	file = NULL;

	return ok;
}

//============================================================================
// *GameTextManager::fetch
//============================================================================

UnicodeString GameTextManager::fetch( const Char *label, Bool *exists )
{
	DEBUG_ASSERTCRASH ( m_initialized, ("String Manager has not been m_initialized") );

	if( m_stringInfo == NULL )
	{
		if( exists )
			*exists = FALSE;
		return m_failed;
	}

	// -modfactions: "M<n>~LABEL" is mod n-1's own LABEL, and ours when that mod has none
	if ( label[0] == 'M' && !m_modStrings.empty() )
	{
		Int mod = modLabelIndex( label );
		if ( mod >= 0 )
		{
			label += 3;
			StringLookUp *own = findModText( mod, label );
			if ( own )
			{
				if( exists )
					*exists = TRUE;
				return own->info->text;
			}
		}
	}

	StringLookUp *lookUp;
	StringLookUp key;
	AsciiString lb;
	lb = label;
	key.info = NULL;
	key.label = &lb;

	lookUp = (StringLookUp *) bsearch( &key, (void*) m_stringLUT, m_textCount, sizeof(StringLookUp), compareLUT );

	if ( lookUp == NULL && m_mapStringLUT && m_mapTextCount )
	{
		lookUp = (StringLookUp *) bsearch( &key, (void*) m_mapStringLUT, m_mapTextCount, sizeof(StringLookUp), compareLUT );
	}

	if( lookUp == NULL )
	{

		// string not found
		if( exists )
			*exists = FALSE;

		// See if we already have the missing string
		UnicodeString missingString;
		missingString.format(u"MISSING: '%hs'", label);

		NoString *noString = m_noStringList;

		while ( noString )
		{
			if (noString->text == missingString)
				return missingString;

			noString = noString->next;
		}

		//DEBUG_LOG(("*** MISSING:'%s' ***\n", label));
		// Remember file could have been altered at this point.
		noString = NEW NoString;
		noString->text = missingString;
		noString->next = m_noStringList;
		m_noStringList = noString;
		return noString->text;
	}
	if( exists )	
		*exists = TRUE;
	return lookUp->info->text;
}

//============================================================================
// *GameTextManager::fetch
//============================================================================

UnicodeString GameTextManager::fetch( AsciiString label, Bool *exists )
{
	return fetch(label.str(), exists);
}

//============================================================================
// GameTextManager::modLabelIndex
//============================================================================

Int GameTextManager::modLabelIndex( const char *label ) const
{
	if ( label[0] != 'M' || label[1] < '1' || label[1] > '9' || label[2] != '~' )
		return -1;
	Int mod = label[1] - '1';
	return mod < (Int)m_modStrings.size() && m_modStrings[ mod ].lut != NULL ? mod : -1;
}

//============================================================================
// GameTextManager::loadModStrings
//============================================================================

void GameTextManager::loadModStrings( Int mod )
{
	if ( (Int)m_modStrings.size() <= mod )
	{
		ModStringTable none = { NULL, NULL, 0 };
		m_modStrings.resize( mod + 1, none );
		m_modLangStrings.resize( mod + 1, none );
	}

	// The label set decides which INI words get "M<n>~", so it may not depend on the player's
	// language: the English CSF and the STR only.  The language's CSF is text alone, read first.
	AsciiString csfFile;
	csfFile.format(g_csfFile, "English");
	readModTable( m_modStrings[ mod ], csfFile.str(), g_strFile );
	AsciiString language = GetRegistryLanguage();
	csfFile.format(g_csfFile, language.str());
	readModTable( m_modLangStrings[ mod ], language.compareNoCase( "English" ) ? csfFile.str() : NULL, NULL );

	DEBUG_LOG(("MODNS M%d~ strings: %d labels\n", mod + 1, m_modStrings[ mod ].count));
}

//============================================================================
// GameTextManager::readModTable
//============================================================================

void GameTextManager::readModTable( ModStringTable &table, const char *csf, const char *str )
{
	// getCSFInfo sizes through our own count and language; keep ours
	Int ourCount = m_textCount;
	LanguageID ourLanguage = m_language;
	Int csfCount = csf && getCSFInfo( csf ) ? m_textCount : 0;
	m_textCount = ourCount;
	m_language = ourLanguage;
	Int strCount = 0;
	if ( str )
		getStringCount( str, strCount );

	table.info = NEW StringInfo[ csfCount + strCount ];

	// the parsers fill m_stringInfo from index 0: point it at the mod's table, the STR after the CSF
	StringInfo *ours = m_stringInfo;
	m_stringInfo = table.info;
	if ( csfCount )
		parseCSF( csf );
	m_stringInfo = table.info + csfCount;
	if ( strCount )
		parseStringFile( str );
	m_stringInfo = ours;

	// the STR's padding leaves empty entries behind; they are no label
	table.lut = NEW StringLookUp[ csfCount + strCount ];
	table.count = 0;
	for ( Int i = 0; i < csfCount + strCount; i++ )
	{
		if ( table.info[ i ].label.isEmpty() )
			continue;
		table.lut[ table.count ].info = &table.info[ i ];
		table.lut[ table.count ].label = &table.info[ i ].label;
		table.count++;
	}
	qsort( table.lut, table.count, sizeof(StringLookUp), compareLUT );

	// a label in both files: the entry further along, the STR's, wins, as the overlays do in init
	if ( table.count > 1 )
	{
		Int kept = 1;
		for ( Int i = 1; i < table.count; i++ )
		{
			if ( table.lut[i].label->compareNoCase( table.lut[kept - 1].label->str() ) == 0 )
			{
				if ( table.lut[i].info > table.lut[kept - 1].info )
					table.lut[kept - 1] = table.lut[i];
				continue;
			}
			table.lut[kept] = table.lut[i];
			kept++;
		}
		table.count = kept;
	}
}

//============================================================================
// GameTextManager::findModText
//============================================================================

StringLookUp *GameTextManager::findModText( Int mod, const char *label ) const
{
	StringLookUp *own = findModLabel( m_modLangStrings[ mod ], label );
	return own ? own : findModLabel( m_modStrings[ mod ], label );
}

//============================================================================
// GameTextManager::hasModLabel
//============================================================================

Bool GameTextManager::hasModLabel( Int mod, const char *label ) const
{
	return findModLabel( m_modStrings[ mod ], label ) != NULL;
}

//============================================================================
// GameTextManager::fetchEnglishHotKey
//============================================================================

WideChar GameTextManager::fetchEnglishHotKey( const AsciiString& label )
{
	// a mod's own label: the letter after '&' in the mod's text, which is the mod's own key
	Int mod = m_modStrings.empty() ? -1 : modLabelIndex( label.str() );
	if ( mod >= 0 )
	{
		StringLookUp *own = findModText( mod, label.str() + 3 );
		if ( own )
		{
			const WideChar *amp = own->info->text.str();
			while ( *amp && *amp != u'&' )
				amp++;
			return ( *amp && amp[ 1 ] > u' ' ) ? amp[ 1 ] : 0;
		}
		return fetchEnglishHotKey( AsciiString( label.str() + 3 ) );
	}

	AsciiString key = label;
	key.toLower();
	std::map< std::string, WideChar >::const_iterator it = m_englishHotKeys.find( key.str() );
	return it == m_englishHotKeys.end() ? 0 : it->second;
}

//============================================================================
// GameTextManager::getStringsWithLabelPrefix
//============================================================================

AsciiStringVec& GameTextManager::getStringsWithLabelPrefix(AsciiString label)
{
	m_asciiStringVec.clear();
	if (m_stringLUT) {
		for (int i = 0; i < m_textCount; ++i) {		
			if (strstr(m_stringLUT[i].label->str(), label.str()) == m_stringLUT[i].label->str()) {
				m_asciiStringVec.push_back(*m_stringLUT[i].label);
			}
		}
	}
	if (m_mapStringLUT) {
		for (int i = 0; i < m_mapTextCount; ++i) {		
			if (strstr(m_mapStringLUT[i].label->str(), label.str()) == m_mapStringLUT[i].label->str()) {
				m_asciiStringVec.push_back(*m_mapStringLUT[i].label);
			}
		}
	}
	return m_asciiStringVec;
}

//============================================================================
// GameTextManager::readLine
//============================================================================

Bool	GameTextManager::readLine( char *buffer, Int max, File *file )
{
	Int ok = FALSE;

	while ( max && file->read( buffer, 1 ) == 1 )
	{
		ok = TRUE;

		if ( *buffer == '\n' )
		{
			break;
		}

		buffer++;
		max--;
	}

	*buffer = 0;

	return ok;
}

//============================================================================
// GameTextManager::readChar
//============================================================================

Char	GameTextManager::readChar( File *file )
{
	Char ch;

	if ( file->read( &ch, 1 ) == 1 )
	{
		return ch;
	}

	return 0;
}

//============================================================================
// GameTextManager::compareLUT 
//============================================================================

static int __cdecl compareLUT ( const void *i1,  const void*i2)
{
	StringLookUp *lut1 = (StringLookUp*) i1;
	StringLookUp *lut2 = (StringLookUp*) i2;

	return strcasecmp( lut1->label->str(), lut2->label->str());
}
