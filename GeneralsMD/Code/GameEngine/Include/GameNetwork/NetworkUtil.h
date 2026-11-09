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


#pragma once
#ifndef __NETWORKUTIL_H
#define __NETWORKUTIL_H

#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/NetCommandMsg.h"

UnsignedInt ResolveIP(AsciiString host);

/** Read a comma separated list of addresses - a -netgame slot list - into host order addresses,
    in the order they were given, since that order is the player order on every machine.  Returns
    how many were read, or -1 if the list is longer than maxAddresses or one entry is not an
    address.  ResolveIP() answers INADDR_NONE for a malformed dotted quad and 0 for a name it
    cannot look up; neither is a player, so both are refused here. */
Int ResolveHostList(AsciiString hosts, UnsignedInt *addresses, Int maxAddresses);

/** True for the whole of 127.0.0.0/8, which Windows routes to loopback and which is therefore the
    range two copies of the game on one machine take an address each out of.  Host order, the same
    order ResolveIP() answers in. */
inline Bool IsLoopbackIP(UnsignedInt hostOrderIP) { return (hostOrderIP & 0xFF000000) == 0x7F000000; }

UnsignedShort GenerateNextCommandID();
Bool DoesCommandRequireACommandID(NetCommandType type);
Bool CommandRequiresAck(NetCommandMsg *msg);
Bool IsCommandSynchronized(NetCommandType type);
AsciiString GetAsciiNetCommandType(NetCommandType type);

/* What another machine is allowed to put on this disk during a map transfer.  IsSafeTransferPath
	 refuses a name that climbs out of the directory it belongs in; IsValidTransferFileContent
	 refuses an extension a transfer never carries, a file over the size ceiling for its kind, and
	 bytes that are not what the extension claims. */
Bool IsSafeTransferPath(const AsciiString &filePath);
Bool IsValidTransferFileContent(const AsciiString &filePath, const UnsignedByte *data, UnsignedInt dataSize);

/* Whether a player name another machine sends us may be shown and put in the lobby's game state
   string.  Refuses the separators that string is parsed on, control characters, lone surrogates,
   and a name made of nothing but spaces. */
Bool IsUsablePlayerName(const WideChar *playerName);

// Command ids are sixteen bits wide and wrap.  Ordering them with < or > puts every command
// issued after a wrap in front of every command issued before it, so the answer comes from the
// distance between them instead: newVal is newer when it sits within half the id space ahead of
// oldVal.  Equal ids are not newer.
inline Bool IsCommandIdNewer( UnsignedShort newVal, UnsignedShort oldVal )
{
	const UnsignedShort diff = newVal - oldVal;
	return diff != 0 && diff < 0x8000;
}

//
// The command list is sorted by command type, then player id, then sort number.  The sort number
// is the command id for everything except the three ack types, which sort by the id of the command
// they acknowledge - an ack's own id is not a key at all: it stays at the constructor's zero when
// the ack is made to be sent, and NetPacket::getCommandList sets it to the packet's running command
// counter when one is read off the wire, which acks do not advance.  Either way it is the same
// number for every ack in a run.  Every comparison in the list has to use the same key, so all of
// them ask these.
//
inline Bool IsCommandFromSamePlayerGroup( NetCommandMsg *first, NetCommandMsg *second )
{
	return first->getNetCommandType() == second->getNetCommandType() &&
				 first->getPlayerID() == second->getPlayerID();
}

inline Bool IsCommandNewerInSamePlayerGroup( NetCommandMsg *newCommand, NetCommandMsg *oldCommand )
{
	return IsCommandFromSamePlayerGroup( newCommand, oldCommand ) &&
				 IsCommandIdNewer( (UnsignedShort)newCommand->getSortNumber(), (UnsignedShort)oldCommand->getSortNumber() );
}

inline Bool IsCommandNewer( NetCommandMsg *newCommand, NetCommandMsg *oldCommand )
{
	if( newCommand->getNetCommandType() != oldCommand->getNetCommandType() )
		return newCommand->getNetCommandType() > oldCommand->getNetCommandType();

	if( newCommand->getPlayerID() != oldCommand->getPlayerID() )
		return newCommand->getPlayerID() > oldCommand->getPlayerID();

	return IsCommandIdNewer( (UnsignedShort)newCommand->getSortNumber(), (UnsignedShort)oldCommand->getSortNumber() );
}

#ifdef DEBUG_LOGGING
extern "C" {
void dumpBufferToLog(const void *vBuf, Int len, const char *fname, Int line);
};
#define LOGBUFFER(buf, len) dumpBufferToLog(buf, len, __FILE__, __LINE__)
#else
#define LOGBUFFER(buf, len) {}
#endif // DEBUG_LOGGING

#endif
