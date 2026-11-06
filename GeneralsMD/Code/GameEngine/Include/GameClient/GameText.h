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
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:    RTS 3
//
// File name:  GameClient/GameText.h
//
// Created:    11/07/01
//
//----------------------------------------------------------------------------

#pragma once

#ifndef __GAMECLIENT_GAMETEXT_H_
#define __GAMECLIENT_GAMETEXT_H_


//----------------------------------------------------------------------------
//           Includes                                                      
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------

class AsciiString;
class UnicodeString;

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------
typedef std::vector<AsciiString> AsciiStringVec;

//===============================
// GameTextInterface 
//===============================
/** Game text interface object for localised text.
	*/
//===============================

class GameTextInterface : public SubsystemInterface
{

	public:

		virtual ~GameTextInterface() {};

		virtual UnicodeString fetch( const Char *label, Bool *exists = NULL ) = 0;		///< Returns the associated labeled unicode text
		virtual UnicodeString fetch( AsciiString label, Bool *exists = NULL ) = 0;		///< Returns the associated labeled unicode text

		// This function is not performance tuned.. Its really only for Worldbuilder. jkmcd
		virtual AsciiStringVec& getStringsWithLabelPrefix(AsciiString label) = 0;

		virtual void					initMapStringFile( const AsciiString& filename ) = 0;

		/// the letter after '&' in the English text of this label (the game's own table and Patch.str,
		/// before any translation), or 0: Classic's command keys are the English ones in every language
		virtual WideChar			fetchEnglishHotKey( const AsciiString& label ) { return 0; }
};


extern GameTextInterface *TheGameText;
extern GameTextInterface* CreateGameTextInterface( void );

//----------------------------------------------------------------------------
//           Inlining
//----------------------------------------------------------------------------

enum
{
	UTF8_CONTINUATION_MASK	= 0xC0,
	UTF8_CONTINUATION				= 0x80,
	UTF8_PAYLOAD_BITS				= 0x3F,
	UTF8_TWO_BYTE_FIRST			= 0xC2,	///< C0 and C1 would spell ASCII the long way round
	UTF8_TWO_BYTE_LAST			= 0xDF,
	UTF8_THREE_BYTE_FIRST		= 0xE0,
	UTF8_THREE_BYTE_LAST		= 0xEF,
	UTF8_THREE_BYTE_LOWEST	= 0x800,	///< anything below fits in two bytes, so three is not UTF-8
};

/** One character of a .str file.  The file is bytes and a WideChar is not: EA's own .str files are
	* ASCII with the odd Latin-1 byte, and a translation is UTF-8.  A well-formed two or three byte
	* UTF-8 sequence becomes the character it spells and any other byte stays the Latin-1 character
	* it always was.  `bytes` is NUL-terminated, and a NUL is never a continuation byte, so nothing
	* past the string is read.  Returns how many bytes the character took. */
inline Int decodeStringFileCharacter( const unsigned char *bytes, WideChar *out )
{
	const unsigned char lead = bytes[ 0 ];
	const Bool secondContinues = ( bytes[ 1 ] & UTF8_CONTINUATION_MASK ) == UTF8_CONTINUATION;

	if( lead >= UTF8_TWO_BYTE_FIRST && lead <= UTF8_TWO_BYTE_LAST && secondContinues )
	{
		*out = (WideChar)( ( ( lead & 0x1F ) << 6 ) | ( bytes[ 1 ] & UTF8_PAYLOAD_BITS ) );
		return 2;
	}

	if( lead >= UTF8_THREE_BYTE_FIRST && lead <= UTF8_THREE_BYTE_LAST && secondContinues
			&& ( bytes[ 2 ] & UTF8_CONTINUATION_MASK ) == UTF8_CONTINUATION )
	{
		const WideChar character = (WideChar)( ( ( lead & 0x0F ) << 12 )
			| ( ( bytes[ 1 ] & UTF8_PAYLOAD_BITS ) << 6 ) | ( bytes[ 2 ] & UTF8_PAYLOAD_BITS ) );
		if( character >= UTF8_THREE_BYTE_LOWEST )
		{
			*out = character;
			return 3;
		}
	}

	*out = lead;
	return 1;
}


#endif // __GAMECLIENT_GAMETEXT_H_
