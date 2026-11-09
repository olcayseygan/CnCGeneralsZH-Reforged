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


#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/NetworkUtil.h"
#include "TARGA.H"

/* Frame counts, sized for the time they cover: 128 and 4 frames were EA's at 30 Hz, the same
	 4.3 s and 133 ms at LOGICFRAMES_PER_SECOND are twice the frames. */
Int MAX_FRAMES_AHEAD = ConvertRetailFrames(128);

/* The shortest run-ahead the room is ever commanded to use: 133 ms of input delay.  EA
	 shipped ten frames - a third of a second on every link, LAN included - and because the formula
	 in computeRunAhead does not clear ten until the two worst average round trips add up to about
	 600 ms, ten frames is what every game anyone actually played ran at.  The window can come down
	 this far because computeRunAhead now rounds the trip up rather than down and carries a fixed
	 jitter allowance on top (RUNAHEAD_JITTER_FRAMES), so a link that genuinely needs a wide window
	 is given a wider one than the old floor ever gave it, and a link that does not stops paying. */
Int MIN_RUNAHEAD = ConvertRetailFrames(4);
Int FRAME_DATA_LENGTH = (MAX_FRAMES_AHEAD+1)*2;
Int FRAMES_TO_KEEP = (MAX_FRAMES_AHEAD/2) + 1;

#ifdef DEBUG_LOGGING

void dumpBufferToLog(const void *vBuf, Int len, const char *fname, Int line)
{
	DEBUG_LOG(("======= dumpBufferToLog() %d bytes =======\n", len));
	DEBUG_LOG(("Source: %s:%d\n", fname, line));
	const char *buf = (const char *)vBuf;
	Int numLines = len / 8;
	if ((len % 8) != 0)
	{
		++numLines;
	}
	for (Int dumpindex = 0; dumpindex < numLines; ++dumpindex)
	{
		Int offset = dumpindex*8;
		DEBUG_LOG(("\t%5.5d\t", offset));
		Int dumpindex2;
		Int numBytesThisLine = min(8, len - offset);
		for (dumpindex2 = 0; dumpindex2 < numBytesThisLine; ++dumpindex2)
		{
			Int c = (buf[offset + dumpindex2] & 0xff);
			DEBUG_LOG(("%02X ", c));
		}
		for (; dumpindex2 < 8; ++dumpindex2)
		{
			DEBUG_LOG(("   "));
		}
		DEBUG_LOG((" | "));
		for (dumpindex2 = 0; dumpindex2 < numBytesThisLine; ++dumpindex2)
		{
			char c = buf[offset + dumpindex2];
			DEBUG_LOG(("%c", (isprint(c)?c:'.')));
		}
		DEBUG_LOG(("\n"));
	}
	DEBUG_LOG(("End of packet dump\n"));
}

#endif // DEBUG_LOGGING

/**
 * ResolveIP turns a string ("games2.westwood.com", or "192.168.0.1") into
 * a 32-bit unsigned integer.
 */
UnsignedInt ResolveIP(AsciiString host)
{
  struct hostent *hostStruct;
  struct in_addr *hostNode;

  if (host.getLength() == 0)
  {
	  DEBUG_LOG(("ResolveIP(): Can't resolve NULL\n"));
	  return 0;
  }

  // String such as "127.0.0.1"
  if (isdigit(host.getCharAt(0)))
  {
    return ( ntohl(inet_addr(host.str())) );
  }

  // String such as "localhost"
  hostStruct = gethostbyname(host.str());
  if (hostStruct == NULL)
  {
	  DEBUG_LOG(("ResolveIP(): Can't resolve %s\n", host.str()));
	  return 0;
  }
  hostNode = (struct in_addr *) hostStruct->h_addr;
  return ( ntohl(hostNode->s_addr) );
}

/**
 * Returns the next network command ID.
 */
Int ResolveHostList(AsciiString hosts, UnsignedInt *addresses, Int maxAddresses)
{
	Int count = 0;
	AsciiString host;
	while (hosts.nextToken( &host, "," ))
	{
		if (count >= maxAddresses)
			return -1;

		UnsignedInt ip = ResolveIP( host );
		if (ip == 0 || ip == 0xffffffff)
			return -1;

		addresses[ count++ ] = ip;
	}
	return count;
}

UnsignedShort GenerateNextCommandID() {
	// The counter used to start at 64000, so the first wrap arrived about 1500 commands into the
	// very first match.  It still wraps - it is sixteen bits wide - but IsCommandIdNewer, which is
	// what NetCommandList sorts by, survives the wrap.
	static UnsignedShort commandID = 0;
	return commandID++;
}

/**
 * Returns true if this type of command requires a unique command ID.
 */
Bool DoesCommandRequireACommandID(NetCommandType type) {
	if ((type == NETCOMMANDTYPE_GAMECOMMAND) ||
			(type == NETCOMMANDTYPE_FRAMEINFO) ||
			(type == NETCOMMANDTYPE_PLAYERLEAVE) ||
			(type == NETCOMMANDTYPE_DESTROYPLAYER) ||
			(type == NETCOMMANDTYPE_RUNAHEADMETRICS) ||
			(type == NETCOMMANDTYPE_RUNAHEAD) ||
			(type == NETCOMMANDTYPE_CHAT) ||
			(type == NETCOMMANDTYPE_DISCONNECTVOTE) ||
			(type == NETCOMMANDTYPE_LOADCOMPLETE) ||
			(type == NETCOMMANDTYPE_TIMEOUTSTART) ||
			(type == NETCOMMANDTYPE_WRAPPER) ||
			(type == NETCOMMANDTYPE_FILE) ||
			(type == NETCOMMANDTYPE_FILEANNOUNCE) ||
			(type == NETCOMMANDTYPE_FILEPROGRESS) ||
			(type == NETCOMMANDTYPE_DISCONNECTPLAYER) ||
			(type == NETCOMMANDTYPE_DISCONNECTFRAME) ||
			(type == NETCOMMANDTYPE_DISCONNECTSCREENOFF) ||
			(type == NETCOMMANDTYPE_FRAMERESENDREQUEST))
	{
		return TRUE;
	}
	return FALSE;
}

/**
 * Returns true if this type of network command requires an ack.
 */
Bool CommandRequiresAck(NetCommandMsg *msg) {
	if ((msg->getNetCommandType() == NETCOMMANDTYPE_GAMECOMMAND) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_PLAYERLEAVE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DESTROYPLAYER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_RUNAHEADMETRICS) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_RUNAHEAD) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_CHAT) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTVOTE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTPLAYER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_LOADCOMPLETE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_TIMEOUTSTART) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_WRAPPER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILEANNOUNCE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILEPROGRESS) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTPLAYER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTSCREENOFF) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FRAMERESENDREQUEST))
	{
		return TRUE;
	}
	return FALSE;
}

Bool IsCommandSynchronized(NetCommandType type) {
	if ((type == NETCOMMANDTYPE_GAMECOMMAND) ||
			(type == NETCOMMANDTYPE_FRAMEINFO) ||
			(type == NETCOMMANDTYPE_PLAYERLEAVE) ||
			(type == NETCOMMANDTYPE_DESTROYPLAYER) ||
			(type == NETCOMMANDTYPE_RUNAHEAD))
	{
		return TRUE;
	}
	return FALSE;
}

AsciiString GetAsciiNetCommandType(NetCommandType type) {
	AsciiString s;
	if (type == NETCOMMANDTYPE_FRAMEINFO) {
		s.set("NETCOMMANDTYPE_FRAMEINFO");
	} else if (type == NETCOMMANDTYPE_GAMECOMMAND) {
		s.set("NETCOMMANDTYPE_GAMECOMMAND");
	} else if (type == NETCOMMANDTYPE_PLAYERLEAVE) {
		s.set("NETCOMMANDTYPE_PLAYERLEAVE");
	} else if (type == NETCOMMANDTYPE_RUNAHEADMETRICS) {
		s.set("NETCOMMANDTYPE_RUNAHEADMETRICS");
	} else if (type == NETCOMMANDTYPE_RUNAHEAD) {
		s.set("NETCOMMANDTYPE_RUNAHEAD");
	} else if (type == NETCOMMANDTYPE_DESTROYPLAYER) {
		s.set("NETCOMMANDTYPE_DESTROYPLAYER");
	} else if (type == NETCOMMANDTYPE_ACKBOTH) {
		s.set("NETCOMMANDTYPE_ACKBOTH");
	} else if (type == NETCOMMANDTYPE_ACKSTAGE1) {
		s.set("NETCOMMANDTYPE_ACKSTAGE1");
	} else if (type == NETCOMMANDTYPE_ACKSTAGE2) {
		s.set("NETCOMMANDTYPE_ACKSTAGE2");
	} else if (type == NETCOMMANDTYPE_FRAMEINFO) {
		s.set("NETCOMMANDTYPE_FRAMEINFO");
	} else if (type == NETCOMMANDTYPE_KEEPALIVE) {
		s.set("NETCOMMANDTYPE_KEEPALIVE");
	} else if (type == NETCOMMANDTYPE_DISCONNECTCHAT) {
		s.set("NETCOMMANDTYPE_DISCONNECTCHAT");
	} else if (type == NETCOMMANDTYPE_CHAT) {
		s.set("NETCOMMANDTYPE_CHAT");
	} else if (type == NETCOMMANDTYPE_MANGLERQUERY) {
		s.set("NETCOMMANDTYPE_MANGLERQUERY");
	} else if (type == NETCOMMANDTYPE_MANGLERRESPONSE) {
		s.set("NETCOMMANDTYPE_MANGLERRESPONSE");
	} else if (type == NETCOMMANDTYPE_DISCONNECTKEEPALIVE) {
		s.set("NETCOMMANDTYPE_DISCONNECTKEEPALIVE");
	} else if (type == NETCOMMANDTYPE_DISCONNECTPLAYER) {
		s.set("NETCOMMANDTYPE_DISCONNECTPLAYER");
	} else if (type == NETCOMMANDTYPE_PACKETROUTERQUERY) {
		s.set("NETCOMMANDTYPE_PACKETROUTERQUERY");
	} else if (type == NETCOMMANDTYPE_PACKETROUTERACK) {
		s.set("NETCOMMANDTYPE_PACKETROUTERACK");
	} else if (type == NETCOMMANDTYPE_DISCONNECTVOTE) {
		s.set("NETCOMMANDTYPE_DISCONNECTVOTE");
	} else if (type == NETCOMMANDTYPE_PROGRESS) {
		s.set("NETCOMMANDTYPE_PROGRESS");
	} else if (type == NETCOMMANDTYPE_LOADCOMPLETE) {
		s.set("NETCOMMANDTYPE_LOADCOMPLETE");
	} else if (type == NETCOMMANDTYPE_TIMEOUTSTART) {
		s.set("NETCOMMANDTYPE_TIMEOUTSTART");
	} else if (type == NETCOMMANDTYPE_WRAPPER) {
		s.set("NETCOMMANDTYPE_WRAPPER");
	} else if (type == NETCOMMANDTYPE_FILE) {
		s.set("NETCOMMANDTYPE_FILE");
	} else if (type == NETCOMMANDTYPE_FILEANNOUNCE) {
		s.set("NETCOMMANDTYPE_FILEANNOUNCE");
	} else if (type == NETCOMMANDTYPE_FILEPROGRESS) {
		s.set("NETCOMMANDTYPE_FILEPROGRESS");
	} else if (type == NETCOMMANDTYPE_DISCONNECTFRAME) {
		s.set("NETCOMMANDTYPE_DISCONNECTFRAME");
	} else if (type == NETCOMMANDTYPE_DISCONNECTSCREENOFF) {
		s.set("NETCOMMANDTYPE_DISCONNECTSCREENOFF");
	} else if (type == NETCOMMANDTYPE_FRAMERESENDREQUEST) {
		s.set("NETCOMMANDTYPE_FRAMERESENDREQUEST");
	} else {
		s.set("UNKNOWN");
	}
	return s;
}

//----------------------------------------------------------------------------------------------
// What another machine is allowed to put on this disk.
//
// A map transfer arrives as a filename and a block of bytes, and both come from the other end.
// Nothing checked either one: whatever it named was written wherever the name resolved to, with
// whatever was in it.  A host could hand a joining player any file it liked.
//
// Three gates, cheapest first: the name may not climb out of the directory it belongs in, the
// extension has to be one of the six a map transfer ever carries, and the bytes have to look like
// what the extension claims - with a size ceiling per kind, so a "map" cannot be a gigabyte.
//----------------------------------------------------------------------------------------------

enum TransferFileType
{
	TransferFileType_Invalid = -1,
	TransferFileType_Map,
	TransferFileType_Ini,
	TransferFileType_Str,
	TransferFileType_Txt,
	TransferFileType_Tga,
	TransferFileType_Wak,
	TransferFileType_Count
};

struct TransferFileRule
{
	const char *ext;
	UnsignedInt maxSize;
};

static const TransferFileRule transferFileRules[TransferFileType_Count] =
{
	{ ".map", 5 * 1024 * 1024 },
	{ ".ini", 2 * 1024 * 1024 },
	{ ".str",      512 * 1024 },
	{ ".txt", 1 * 1024 * 1024 },
	{ ".tga", 2 * 1024 * 1024 },
	{ ".wak",      128 * 1024 },
};

static TransferFileType getTransferFileType(const char *extension)
{
	for (Int i = 0; i < TransferFileType_Count; ++i)
	{
		if (strcasecmp(extension, transferFileRules[i].ext) == 0)
			return (TransferFileType)i;
	}
	return TransferFileType_Invalid;
}

// The portable path keeps its last two components, so a ".." among them walks out of the map
// directory.  Nothing legitimate needs one.
Bool IsSafeTransferPath(const AsciiString &filePath)
{
	const char *p = filePath.str();
	if (strstr(p, "..") != NULL)
	{
		DEBUG_LOG(("Transfer path '%s' tries to climb out of its directory\n", p));
		return FALSE;
	}
	return TRUE;
}

Bool IsValidTransferFileContent(const AsciiString &filePath, const UnsignedByte *data, UnsignedInt dataSize)
{
	const char *fileExt = strrchr(filePath.str(), '.');
	if (fileExt == NULL)
	{
		DEBUG_LOG(("File '%s' has no extension\n", filePath.str()));
		return FALSE;
	}

	const TransferFileType fileType = getTransferFileType(fileExt);
	if (fileType == TransferFileType_Invalid)
	{
		DEBUG_LOG(("File '%s' has an extension a transfer never carries\n", filePath.str()));
		return FALSE;
	}

	const TransferFileRule &rule = transferFileRules[fileType];
	if (dataSize > rule.maxSize)
	{
		DEBUG_LOG(("File '%s' is %d bytes, over the %d byte limit for its kind\n",
			filePath.str(), dataSize, rule.maxSize));
		return FALSE;
	}

	switch (fileType)
	{
		case TransferFileType_Ini:
		{
			// an INI is text; a null byte in one means it is something else wearing the name
			for (UnsignedInt i = 0; i < dataSize; ++i)
			{
				if (data[i] == 0)
				{
					DEBUG_LOG(("INI file '%s' holds null bytes, so it is not an INI\n", filePath.str()));
					return FALSE;
				}
			}
			break;
		}

		case TransferFileType_Tga:
		{
			// the Targa 2 footer is the last 26 bytes, and its signature the 18 before the end.
			// Read it by offset rather than as a struct: the struct carries a constructor and the
			// padding that comes with it.
			static const Int TGA2_FOOTER_SIZE = 26;
			static const Int TGA2_SIGNATURE_LEN = 16;
			if (dataSize < (UnsignedInt)(sizeof(TGAHeader) + TGA2_FOOTER_SIZE))
			{
				DEBUG_LOG(("TGA file '%s' is too small to be one\n", filePath.str()));
				return FALSE;
			}
			const UnsignedByte *sig = data + dataSize - (TGA2_SIGNATURE_LEN + 2);
			if (memcmp(sig, TGA2_SIGNATURE, TGA2_SIGNATURE_LEN) == 0
				&& sig[TGA2_SIGNATURE_LEN] == '.'
				&& sig[TGA2_SIGNATURE_LEN + 1] == '\0')
			{
				break;
			}

			// The footer is Targa 2.0 and a 1.0 file has none, which is every preview a paint program
			// saves that way.  Refusing those sent no progress back, and the host sat out the whole
			// two minute timeout and called the map transfer failed.  Without a footer the header has
			// to hold up instead: a real image type, a size, a pixel depth, and for an uncompressed
			// image the pixels it promises.  Read by offset, the struct is not packed.
			const UnsignedByte imageType = data[2];
			const UnsignedInt width = data[12] | (data[13] << 8);
			const UnsignedInt height = data[14] | (data[15] << 8);
			const UnsignedByte pixelDepth = data[16];
			const Bool knownType = imageType == TGA_CMAPPED || imageType == TGA_TRUECOLOR || imageType == TGA_MONO
				|| imageType == TGA_CMAPPED_ENCODED || imageType == TGA_TRUECOLOR_ENCODED || imageType == TGA_MONO_ENCODED;
			const Bool knownDepth = pixelDepth == 8 || pixelDepth == 15 || pixelDepth == 16 || pixelDepth == 24 || pixelDepth == 32;
			const Bool encoded = imageType >= TGA_CMAPPED_ENCODED;
			if (!knownType || !knownDepth || width == 0 || height == 0
				|| (!encoded && 18 + data[0] + (Int64)width * height * ((pixelDepth + 7) / 8) > (Int64)dataSize))
			{
				DEBUG_LOG(("TGA file '%s' has no TRUEVISION-XFILE footer and no header that holds up\n", filePath.str()));
				return FALSE;
			}
			break;
		}

		default:
			break;
	}

	return TRUE;
}

//----------------------------------------------------------------------------------------------
// A player name another machine sends us.
//
// The name goes into the game state string the lobby passes around, and that string is parsed on
// commas, colons and semicolons - so a name holding one of those rewrites everyone's idea of who
// is in the room.  Control characters and half a surrogate pair do their own damage in a text
// field, and a name made only of spaces reads as an empty seat.
//----------------------------------------------------------------------------------------------

static Bool isCharacterANameMayNotHold(const WideChar c)
{
	return c < u' '														// C0 controls
		|| c == u',' || c == u':' || c == u';'							// the game state separators
		|| (c >= u'\x007f' && c <= u'\x009f')							// DEL and the C1 controls
		|| c == u'\x2028' || c == u'\x2029'								// line and paragraph separators
		|| (c >= u'\xd800' && c <= u'\xdfff');							// lone surrogates
}

static Bool isSpaceCharacter(const WideChar c)
{
	return c == u' '
		|| c == u'\xa0'													// no-break space
		|| c == u'\x1680'												// ogham space mark
		|| (c >= u'\x2000' && c <= u'\x200a')							// en/em, figure, thin, hair
		|| c == u'\x202f'												// narrow no-break space
		|| c == u'\x205f'												// medium mathematical space
		|| c == u'\x3000';												// ideographic space
}

Bool IsUsablePlayerName(const WideChar *playerName)
{
	if (playerName == NULL)
		return FALSE;

	Bool sawSomethingReadable = FALSE;
	for (const WideChar *c = playerName; *c; ++c)
	{
		if (isCharacterANameMayNotHold(*c))
			return FALSE;
		if (!isSpaceCharacter(*c))
			sawSomethingReadable = TRUE;
	}

	return sawSomethingReadable;
}
