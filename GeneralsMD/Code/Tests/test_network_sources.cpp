// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreRTS.h"
#include "test_harness.h"
#include "Common/GlobalData.h"
#define private public
#include "GameNetwork/ConnectionManager.h"
#undef private
#include "GameNetwork/NetCommandWrapperList.h"
#include <vector>
#include <cstring>

namespace {
struct Writer : NetPacket {
    using NetPacket::GetBufferSizeNeededForCommand;
    using NetPacket::FillBufferWithCommand;
};
struct Fixture {
    GlobalData *saved;
    ConnectionManager *manager;
    Fixture() : saved(TheWritableGlobalData) {
        TheWritableGlobalData = NEW GlobalData;
        manager = NEW ConnectionManager;
        std::memset(manager->m_connections, 0, sizeof(manager->m_connections));
        manager->m_localSlot = 3; manager->m_packetRouterSlot = 2;
        manager->m_transport = NEW Transport;
        std::memset(manager->m_transport->m_inBuffer, 0, sizeof(manager->m_transport->m_inBuffer));
        manager->m_netCommandWrapperList = newInstance(NetCommandWrapperList);
        manager->m_netCommandWrapperList->init();
        manager->m_relayedCommands = newInstance(NetCommandList); manager->m_relayedCommands->init();
        manager->m_pendingCommands = newInstance(NetCommandList); manager->m_pendingCommands->init();
        for (unsigned slot = 0; slot < 3; ++slot) {
            Connection *connection = newInstance(Connection); connection->init();
            User *user = newInstance(User);
            user->SetIPAddr(0x7f000001+slot); user->SetPort(12340+slot);
            connection->setUser(user); manager->m_connections[slot] = connection;
        }
        for (unsigned slot = 0; slot < MAX_SLOTS; ++slot) manager->m_lastHeardFrom[slot] = 123;
    }
    ~Fixture() {
        delete manager;
        delete TheWritableGlobalData; TheWritableGlobalData = saved;
    }
    void deliver(NetCommandMsg *msg, unsigned source) {
        NetCommandRef *ref = NEW_NETCOMMANDREF(msg); ref->setRelay(1u<<3);
        NetPacket *packet = newInstance(NetPacket); packet->init();
        CHECK(packet->addCommand(ref));
        TransportMessage &input = manager->m_transport->m_inBuffer[0];
        input.length = packet->getLength(); std::memcpy(input.data, packet->getData(), input.length);
        input.addr = source < 3 ? 0x7f000001+source : 0x7f0000ee;
        input.port = source < 3 ? 12340+source : 22345;
        packet->deleteInstance(); ref->deleteInstance();
        manager->doRelay();
        CHECK_EQ(input.length, 0);
    }
};
NetFileAnnounceCommandMsg *announcement() {
    NetFileAnnounceCommandMsg *msg = newInstance(NetFileAnnounceCommandMsg);
    msg->setPlayerID(0); msg->setFileID(42); msg->setID(42); msg->setExecutionFrame(0);
    msg->setPortableFilename("Maps/Synthetic/Synthetic.map"); msg->setPlayerMask(0);
    return msg;
}
NetCommandRef *chunk(const std::vector<UnsignedByte> &raw, unsigned offset, unsigned length,
    unsigned index, unsigned count, unsigned source) {
    NetWrapperCommandMsg *msg = newInstance(NetWrapperCommandMsg);
    msg->setPlayerID(0); msg->setID(100+index); msg->setWrappedCommandID(42);
    msg->setNumChunks(count); msg->setChunkNumber(index); msg->setTotalDataLength((UnsignedInt)raw.size());
    msg->setDataOffset(offset); msg->setData(const_cast<UnsignedByte *>(raw.data())+offset,length);
    NetCommandRef *ref = NEW_NETCOMMANDREF(msg); msg->detach();
    ref->setSourceMask(1u<<source); ref->setRelay(0);
    return ref;
}
}

TEST(unknown_endpoints_and_nonhosts_cannot_announce_host_files)
{
    Fixture f;
    NetFileAnnounceCommandMsg *msg = announcement();
    for (unsigned source : {8u,1u,2u}) {
        f.deliver(msg,source);
        CHECK(f.manager->s_fileCommandMap.empty());
        for (unsigned slot=0;slot<MAX_SLOTS;++slot) CHECK_EQ(f.manager->m_lastHeardFrom[slot],123u);
        for (unsigned slot=0;slot<3;++slot)
            CHECK(f.manager->m_connections[slot]->isQueueEmpty());
    }
    // The same real packet from the host is accepted and acknowledged.
    f.deliver(msg,0);
    CHECK(f.manager->s_fileCommandMap.find(42)!=f.manager->s_fileCommandMap.end());
    CHECK(f.manager->m_connections[0]->isQueueEmpty()==FALSE);
    msg->detach();
}

TEST(reassembled_host_commands_keep_all_physical_sources)
{
    Fixture f;
    NetFileAnnounceCommandMsg *msg = announcement();
    NetCommandRef *ref = NEW_NETCOMMANDREF(msg); ref->setRelay(0);
    std::vector<UnsignedByte> raw(Writer::GetBufferSizeNeededForCommand(msg));
    Writer::FillBufferWithCommand(raw.data(),ref); ref->deleteInstance(); msg->detach();
    for (unsigned source : {2u,0u}) {
        NetCommandRef *part = chunk(raw,0,(unsigned)raw.size(),0,1,source);
        f.manager->m_netCommandWrapperList->processWrapper(part); part->deleteInstance();
        NetCommandList *ready = f.manager->m_netCommandWrapperList->getReadyCommands();
        CHECK(ready->getFirstMessage()!=NULL);
        if (ready->getFirstMessage()) {
            CHECK_EQ(ready->getFirstMessage()->getSourceMask(),1u<<source);
            CHECK_EQ(f.manager->receiveSourceAllowed(ready->getFirstMessage()),source==0);
        }
        ready->deleteInstance();
    }
    const unsigned half=(unsigned)raw.size()/2;
    NetCommandRef *first=chunk(raw,0,half,0,2,0),*second=chunk(raw,half,(unsigned)raw.size()-half,1,2,2);
    f.manager->m_netCommandWrapperList->processWrapper(first);
    f.manager->m_netCommandWrapperList->processWrapper(second);
    first->deleteInstance();second->deleteInstance();
    NetCommandList *ready=f.manager->m_netCommandWrapperList->getReadyCommands();
    CHECK(ready->getFirstMessage()!=NULL);
    if(ready->getFirstMessage()) CHECK(!f.manager->receiveSourceAllowed(ready->getFirstMessage()));
    ready->deleteInstance();
    CHECK(f.manager->s_fileCommandMap.empty());
}

TEST(historical_frames_need_an_actual_resend_request_and_stay_in_its_window)
{
    Fixture f;
    NetFrameCommandMsg *msg=newInstance(NetFrameCommandMsg);msg->setPlayerID(0);msg->setExecutionFrame(100);
    NetCommandRef *ref=NEW_NETCOMMANDREF(msg);ref->setSourceMask(1u<<1);ref->setRelay(0);
    CHECK(!f.manager->receiveSourceAllowed(ref));
    f.manager->requestFrameDataResend(1,100);
    CHECK(f.manager->receiveSourceAllowed(ref));
    msg->setExecutionFrame(99);CHECK(!f.manager->receiveSourceAllowed(ref));
    msg->setExecutionFrame(100+MAX_FRAMES_AHEAD);CHECK(!f.manager->receiveSourceAllowed(ref));
    msg->setExecutionFrame(100+MAX_FRAMES_AHEAD-1);CHECK(f.manager->receiveSourceAllowed(ref));
    // Current router forwarding needs no historical request, including after succession.
    ref->setSourceMask(1u<<2);CHECK(f.manager->receiveSourceAllowed(ref));
    f.manager->m_packetRouterSlot=1;ref->setSourceMask(1u<<1);CHECK(f.manager->receiveSourceAllowed(ref));
    ref->deleteInstance();msg->detach();
}

TEST(quitting_connections_can_finish_acknowledgements_but_cannot_send_new_controls)
{
    Fixture f;
    NetChatCommandMsg *pending=newInstance(NetChatCommandMsg);
    pending->setPlayerID(3);pending->setID(77);pending->setExecutionFrame(0);
    f.manager->m_connections[1]->sendNetCommandMsg(pending,1u<<1);pending->detach();
    CHECK(!f.manager->m_connections[1]->isQueueEmpty());
    f.manager->m_connections[1]->setQuitting();
    NetAckStage1CommandMsg *ack=newInstance(NetAckStage1CommandMsg);
    ack->setPlayerID(1);ack->setOriginalPlayerID(3);ack->setCommandID(77);
    NetCommandRef *ref=NEW_NETCOMMANDREF(ack);ref->setSourceMask(1u<<1);ref->setRelay(0);
    CHECK(f.manager->receiveSourceAllowed(ref));
    // Exercise the endpoint path as well, without a UDP socket or game data.
    f.deliver(ack,1);
    CHECK(f.manager->m_connections[1]->isQueueEmpty());
    CHECK(f.manager->m_lastHeardFrom[1]!=123u);
    ack->setPlayerID(0);CHECK(!f.manager->receiveSourceAllowed(ref));
    ref->deleteInstance();ack->detach();
    NetFileAnnounceCommandMsg *file=announcement();
    file->setPlayerID(1);
    ref=NEW_NETCOMMANDREF(file);ref->setSourceMask(1u<<1);ref->setRelay(0);
    CHECK(!f.manager->receiveSourceAllowed(ref));
    ref->deleteInstance();file->detach();
}

TEST(network_role_and_payload_checks_survive_relay_and_router_succession)
{
    Fixture f;
    NetRunAheadCommandMsg *run=newInstance(NetRunAheadCommandMsg);
    run->setPlayerID(1);run->setFrameRate(30);run->setRunAhead(6);
    NetCommandRef *ref=NEW_NETCOMMANDREF(run);ref->setSourceMask(1u<<1);ref->setRelay(0);
    CHECK(!f.manager->receiveSourceAllowed(ref)); // endpoint identity alone is insufficient
    run->setPlayerID(2);ref->setSourceMask(1u<<2);CHECK(f.manager->receiveSourceAllowed(ref));
    run->setFrameRate(0);CHECK(!f.manager->receiveSourceAllowed(ref));
    run->setFrameRate(255);CHECK(f.manager->receiveSourceAllowed(ref));
    run->setRunAhead(0);CHECK(!f.manager->receiveSourceAllowed(ref));
    run->setRunAhead(MAX_FRAMES_AHEAD);CHECK(!f.manager->receiveSourceAllowed(ref));
    run->setRunAhead(6);
    // A queued command from a former authorized router can still arrive via its successor.
    f.manager->m_routerCommandAuthors=1u<<2;f.manager->m_packetRouterSlot=1;
    ref->setSourceMask(1u<<1);CHECK(f.manager->receiveSourceAllowed(ref));
    ref->deleteInstance();run->detach();
    NetPlayerLeaveCommandMsg *leave=newInstance(NetPlayerLeaveCommandMsg);
    leave->setPlayerID(1);leave->setLeavingPlayerID(0);
    ref=NEW_NETCOMMANDREF(leave);ref->setSourceMask(1u<<1);ref->setRelay(0);
    CHECK(!f.manager->receiveSourceAllowed(ref));
    CHECK_EQ(f.manager->processPlayerLeave(leave),PLAYERLEAVECODE_UNKNOWN);
    leave->setLeavingPlayerID(1);CHECK(f.manager->receiveSourceAllowed(ref));
    ref->deleteInstance();leave->detach();
}
