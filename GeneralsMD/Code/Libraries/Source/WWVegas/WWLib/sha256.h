/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Olcay Seygan
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

// FILE: sha256.h /////////////////////////////////////////////////////////////////////////////////
// Desc:   SHA-256 (FIPS 180-4), portable, for the install file check online login sends. sha.h is
//         SHA-1 and the only other hash in the tree is WinCrypt, which the Mac and Linux builds lack.
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

class Sha256
{
public:
	Sha256();
	void Update( const void *data, size_t length );
	void Final( unsigned char digest[32] );		///< ends the hash; Update after it starts nothing new

	/// 64 lowercase hex digits and a terminator
	static void Hex( const unsigned char digest[32], char out[65] );

private:
	void Block( const unsigned char *block );

	uint32_t State[8];
	uint64_t Length;
	unsigned char Buffer[64];
	size_t Buffered;
};

#endif
