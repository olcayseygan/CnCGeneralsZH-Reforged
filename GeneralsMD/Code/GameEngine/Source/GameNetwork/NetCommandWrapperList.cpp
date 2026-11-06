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

////// NetCommandWrapperList.cpp ////////////////////////////////
// Bryan Cleveland

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/NetCommandWrapperList.h"
#include "GameNetwork/NetPacket.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
////// NetCommandWrapperListNode ///////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

NetCommandWrapperListNode::NetCommandWrapperListNode(NetWrapperCommandMsg *msg) 
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_sourceMask = 0;
	m_next = NULL;

	//

	m_numChunks = msg->getNumChunks();
	m_chunksPresent = NEW Bool[m_numChunks];	// pool[]ify
	m_numChunksPresent = 0;

	for (Int i = 0; i < m_numChunks; ++i) {
		m_chunksPresent[i] = FALSE;
	}

	m_dataLength = msg->getTotalDataLength();
	m_data = NEW UnsignedByte[m_dataLength];	// pool[]ify

	m_commandID = msg->getWrappedCommandID();
	m_playerID = msg->getPlayerID();
}

NetCommandWrapperListNode::~NetCommandWrapperListNode() {
	if (m_chunksPresent != NULL) {
		delete[] m_chunksPresent;
		m_chunksPresent = NULL;
	}

	if (m_data != NULL) {
		delete[] m_data;
		m_data = NULL;
	}
}

Bool NetCommandWrapperListNode::isComplete() {
	return m_numChunksPresent == m_numChunks;
}

Int NetCommandWrapperListNode::getPercentComplete(void) {
	if (isComplete())
		return 100;
	else
		return min(99, REAL_TO_INT( ((Real)m_numChunksPresent)/((Real)m_numChunks)*100.0f ));
}

UnsignedShort NetCommandWrapperListNode::getCommandID() {
	return m_commandID;
}

UnsignedInt NetCommandWrapperListNode::getRawDataLength() {
	return m_dataLength;
}

void NetCommandWrapperListNode::copyChunkData(NetWrapperCommandMsg *msg) {
	if (msg == NULL) {
		DEBUG_CRASH(("Trying to copy data from a non-existent wrapper command message"));
		return;
	}

	UnsignedInt chunkNumber = msg->getChunkNumber();

	if (chunkNumber >= m_numChunks) {
		DEBUG_CRASH(("Data chunk %d exceeds the expected maximum of %d chunks\n",
			chunkNumber, m_numChunks));
		return;
	}

	if (m_chunksPresent[chunkNumber] == TRUE) {
		// we already received this chunk, no need to recopy it.
		return;
	}

	// The offset and the length both come off the wire and neither was checked.  A wrapped command
	// whose chunk claims an offset or a size beyond the buffer this node allocated for the whole
	// command wrote straight past the end of that heap block - and the sender is a peer, not us.
	UnsignedInt chunkDataOffset = msg->getDataOffset();
	UnsignedInt chunkDataLength = msg->getDataLength();

	if (chunkDataOffset >= m_dataLength) {
		DEBUG_CRASH(("Data chunk offset %d exceeds the total data length %d\n",
			chunkDataOffset, m_dataLength));
		return;
	}

	if (chunkDataLength > MAX_PACKET_SIZE) {
		DEBUG_CRASH(("Data chunk size %d is greater than the maximum packet size %d\n",
			chunkDataLength, MAX_PACKET_SIZE));
		return;
	}

	if (chunkDataOffset + chunkDataLength > m_dataLength) {
		DEBUG_CRASH(("Data chunk at %d for %d bytes runs past the data array\n",
			chunkDataOffset, chunkDataLength));
		return;
	}

	DEBUG_LOG(("NetCommandWrapperListNode::copyChunkData() - copying chunk %d\n", chunkNumber));

	memcpy(m_data + chunkDataOffset, msg->getData(), chunkDataLength);

	// only a chunk that was really copied counts as present, or the command completes with a hole
	m_chunksPresent[chunkNumber] = TRUE;
	++m_numChunksPresent;
}

UnsignedByte * NetCommandWrapperListNode::getRawData() {
	return m_data;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////// NetCommandWrapperList ///////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

NetCommandWrapperList::NetCommandWrapperList() {
	m_list = NULL;
}

NetCommandWrapperList::~NetCommandWrapperList() {
	NetCommandWrapperListNode *temp;
	while (m_list != NULL) {
		temp = m_list->m_next;
		m_list->deleteInstance();
		m_list = temp;
	}
}

void NetCommandWrapperList::init() {
	m_list = NULL;
}

void NetCommandWrapperList::reset() {
	NetCommandWrapperListNode *temp;
	while (m_list != NULL) {
		temp = m_list->m_next;
		m_list->deleteInstance();
		m_list = temp;
	}
}

Int NetCommandWrapperList::getPercentComplete(UnsignedShort wrappedCommandID)
{
	NetCommandWrapperListNode *temp = m_list;

	while ((temp != NULL) && (temp->getCommandID() != wrappedCommandID)) {
		temp = temp->m_next;
	}

	if (!temp)
		return 0;

	return temp->getPercentComplete();
}

// The first chunk of a wrapped command makes us allocate the whole command's buffer at once, at a
// size the sender names, and an unfinished command is never let go.  So one peer could make us
// hold any amount of memory by opening commands it never completes.  A sender gets this many open
// at a time and this many bytes across them.  A 65 KB map transfer is one command; normal play
// never comes near either figure.
static const Int MAX_WRAPPED_COMMANDS_PER_SENDER = 64;
static const UnsignedInt MAX_WRAPPED_BYTES_PER_SENDER = 16 * 1024 * 1024;

Bool NetCommandWrapperList::isWithinSenderCap(NetWrapperCommandMsg *msg) {
	const UnsignedInt totalLength = msg->getTotalDataLength();
	const UnsignedInt numChunks = msg->getNumChunks();

	// every chunk carries at least one byte, and an empty command has nothing to rebuild
	if (numChunks == 0 || totalLength == 0 || numChunks > totalLength || totalLength > MAX_WRAPPED_BYTES_PER_SENDER) {
		DEBUG_LOG(("NetCommandWrapperList - refused a wrapped command of %u bytes in %u chunks from player %d\n",
			totalLength, numChunks, msg->getPlayerID()));
		return FALSE;
	}

	Int openCommands = 0;
	UnsignedInt openBytes = 0;
	for (NetCommandWrapperListNode *node = m_list; node != NULL; node = node->m_next) {
		if (node->getPlayerID() == msg->getPlayerID()) {
			++openCommands;
			openBytes += node->getRawDataLength();
		}
	}

	if (openCommands >= MAX_WRAPPED_COMMANDS_PER_SENDER || totalLength > MAX_WRAPPED_BYTES_PER_SENDER - openBytes) {
		DEBUG_LOG(("NetCommandWrapperList - player %d already has %d wrapped commands of %u bytes open, refused one more of %u\n",
			msg->getPlayerID(), openCommands, openBytes, totalLength));
		return FALSE;
	}
	return TRUE;
}

void NetCommandWrapperList::processWrapper(NetCommandRef *ref) {
	NetCommandWrapperListNode *temp = m_list;
	NetWrapperCommandMsg *msg = (NetWrapperCommandMsg *)(ref->getCommand());

	while ((temp != NULL) && ((temp->getCommandID() != msg->getWrappedCommandID()) || (temp->getPlayerID() != msg->getPlayerID()))) {
		temp = temp->m_next;
	}

	if (temp == NULL) {
		if (!isWithinSenderCap(msg)) {
			return;
		}
		temp = newInstance(NetCommandWrapperListNode)(msg);
		temp->m_next = m_list;
		m_list = temp;
	}

	temp->m_sourceMask |= ref->getSourceMask();
	temp->copyChunkData(msg);
}

NetCommandList * NetCommandWrapperList::getReadyCommands() 
{
	NetCommandList *retlist = newInstance(NetCommandList);
	retlist->init();

	NetCommandWrapperListNode *temp = m_list;
	NetCommandWrapperListNode *next = NULL;

	while (temp != NULL) {
		next = temp->m_next;
		if (temp->isComplete()) {
			NetCommandRef *msg = NetPacket::ConstructNetCommandMsgFromRawData(temp->getRawData(), temp->getRawDataLength());
			// NULL when the rebuilt bytes do not read as a command; the sender's problem, not ours.  And
			// the chunks carry their sender, so the command inside cannot claim to be somebody else's.
			if (msg != NULL && msg->getCommand()->getPlayerID() != temp->getPlayerID()) {
				DEBUG_LOG(("NetCommandWrapperList - player %d wrapped a command from player %d, dropped\n",
					temp->getPlayerID(), msg->getCommand()->getPlayerID()));
				msg->deleteInstance();
				msg = NULL;
			}
			if (msg != NULL) {
				NetCommandRef *ret = retlist->addMessage(msg->getCommand());
				ret->setRelay(msg->getRelay());
				ret->setSourceMask(temp->m_sourceMask);

				msg->deleteInstance();
				msg = NULL;
			}

			removeFromList(temp);
			temp = NULL;
		}
		temp = next;
	}

	return retlist;
}

void NetCommandWrapperList::removeFromList(NetCommandWrapperListNode *node) {
	if (node == NULL) {
		return;
	}

	NetCommandWrapperListNode *temp = m_list;
	NetCommandWrapperListNode *prev = NULL;

	while ((temp != NULL) && (temp != node)) {
		prev = temp;
		temp = temp->m_next;
	}

	if (temp == NULL) {
		return;
	}

	if (prev == NULL) {
		m_list = temp->m_next;
		temp->deleteInstance();
		temp = NULL;
	} else {
		prev->m_next = temp->m_next;
		temp->deleteInstance();
		temp = NULL;
	}
}
