#include "PreRTS.h"

#include "GameNetwork/GeneralsOnline/OnlineBridge.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/HTTP/HTTPManager.h"

#include "Common/GameState.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/Transport.h"

#include <deque>
#include <set>

NGMPGame* TheNGMPGame = nullptr;

namespace
{
	/// The slots and the connections address a player by IP. Over the mesh there is none, so each player gets
	/// a 10.x.x.x made from the user ID, which the route below turns back into the ID.
	/// ponytail: 24 bits of the ID, so two users 16 million apart collide in one lobby; a table in the slot
	/// list if the service ever gets near that.
	UnsignedInt addressForUser(int64_t userID)
	{
		return 0x0A000000 | (UnsignedInt)(userID & 0xFFFFFF);
	}

	std::string asciiOf(const std::string& utf8)
	{
		return utf8_to_local(utf8);
	}

	/// Game packets over the mesh, in place of Transport's UDP socket.
	class OnlineRoute : public TransportRoute
	{
	public:
		Int send(const unsigned char* buf, UnsignedInt len, UnsignedInt ip, UnsignedShort) override
		{
			NetworkMesh* mesh = NGMP_OnlineServicesManager::GetNetworkMesh();
			auto user = m_users.find(ip);
			if (mesh == nullptr || user == m_users.end())
				return 0;
			mesh->SendGamePacket((void*)buf, len, user->second);
			return (Int)len;
		}

		Int recv(unsigned char* buf, UnsignedInt len, sockaddr_in* from) override
		{
			if (m_inbox.empty())
				return 0;
			const Packet& packet = m_inbox.front();
			const UnsignedInt size = min((UnsignedInt)packet.bytes.size(), len);
			memcpy(buf, packet.bytes.data(), size);
			from->sin_family = AF_INET;
			from->sin_addr.s_addr = htonl(packet.ip);
			from->sin_port = htons(8888);
			m_inbox.pop_front();
			return (Int)size;
		}

		void arrived(int64_t userID, const unsigned char* data, UnsignedInt size)
		{
			m_users[addressForUser(userID)] = userID;
			m_inbox.push_back({ addressForUser(userID), std::vector<unsigned char>(data, data + size) });
		}

		void know(int64_t userID) { m_users[addressForUser(userID)] = userID; }

	private:
		struct Packet
		{
			UnsignedInt ip;
			std::vector<unsigned char> bytes;
		};
		std::map<UnsignedInt, int64_t> m_users;
		std::deque<Packet> m_inbox;
	};

	OnlineRoute s_route;

	class OnlinePeerQueue;
	OnlinePeerQueue* s_queue = nullptr;

	void endSession()
	{
		TheTransportRoute = nullptr;
		NGMP_OnlineServicesManager::DestroyInstance();
	}

	class OnlinePeerQueue : public GameSpyPeerMessageQueueInterface
	{
	public:
		void startThread(void) override {}
		void endThread(void) override {}
		Bool isThreadRunning(void) override { return TRUE; }
		Bool isConnected(void) override { return NGMP_OnlineServicesManager::GetInstance() != nullptr; }

		/// The screens end a session by tearing GameSpy down, so the service goes with this queue.
		~OnlinePeerQueue() override
		{
			s_queue = nullptr;
			endSession();
		}

		Bool isConnecting(void) override { return FALSE; }
		SerialAuthResult getSerialAuthResult(void) override { return SERIAL_OK; }

		void addRequest(const PeerRequest& req) override;
		Bool getRequest(PeerRequest&) override { return FALSE; }

		void addResponse(const PeerResponse& resp) override { m_responses.push_back(resp); }
		Bool getResponse(PeerResponse& resp) override
		{
			if (m_responses.empty())
				return FALSE;
			resp = m_responses.front();
			m_responses.pop_front();
			return TRUE;
		}

		void attach();
		void tick();

	private:
		void publishRooms();
		void refreshLobbyList();
		void publishLobbyList(const std::vector<LobbyEntry>& lobbies);
		void diffRoster(const std::map<int64_t, std::string>& now, RoomType roomType);
		void playerResponse(PeerResponse& resp, int64_t userID, const std::string& name, RoomType roomType);
		void rosterChangedInRoom();
		void rosterChangedInLobby();
		void chatArrived(const std::string& message, bool isAction);
		void sendUtm(const PeerRequest& req, bool toRoom);
		void flushUtms();
		void pumpMesh();
		void utmArrived(int64_t userID, const unsigned char* data, UnsignedInt size);
		void leaveLobbyNow();
		int64_t userFromNick(const std::string& nick) const;
		std::string nickFromUser(int64_t userID) const;

		std::deque<PeerResponse> m_responses;
		std::map<int64_t, std::string> m_roster;
		std::map<int64_t, std::deque<std::string>> m_pendingUtms;
		Bool m_listActive = FALSE;
		Bool m_leaveWhenMatchEnds = FALSE;
		Bool m_inLobby = FALSE;
		UnsignedInt m_nextListRefresh = 0;
		std::string m_listSignature;
		std::string m_mapPublished;
	};

	/// Only the local player's name is ever looked up; stats live with the ladder, and that is not offered.
	class OnlineStatsQueue : public GameSpyPSMessageQueueInterface
	{
	public:
		void startThread(void) override {}
		void endThread(void) override {}
		Bool isThreadRunning(void) override { return TRUE; }
		void addRequest(const PSRequest&) override {}
		Bool getRequest(PSRequest&) override { return FALSE; }
		void addResponse(const PSResponse&) override {}
		Bool getResponse(PSResponse&) override { return FALSE; }
		void trackPlayerStats(PSPlayerStats) override {}
		PSPlayerStats findPlayerStatsByID(Int) override { return PSPlayerStats(); }
	};

	void showFailure(const UnicodeString& body)
	{
		ClearGSMessageBoxes();
		MessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), body, OnlineBridge_BackToMainMenu);
	}

	void loginFinished(ELoginResult result)
	{
		if (result == ELoginResult::UserCancelled)
		{
			OnlineBridge_BackToMainMenu();
			return;
		}
		if (result != ELoginResult::Success)
		{
			showFailure(TheGameText->fetch("GUI:GSDisconReason4"));
			return;
		}

		NGMP_OnlineServices_AuthInterface* auth = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
		SetUpGameSpy("", "");
		const UnsignedInt localIP = addressForUser(auth->GetUserID());
		TheGameSpyInfo->setLocalName(asciiOf(auth->GetDisplayName()).c_str());
		TheGameSpyInfo->setLocalProfileID((Int)auth->GetUserID());
		TheGameSpyInfo->setLocalBaseName(asciiOf(auth->GetDisplayName()).c_str());
		TheGameSpyInfo->setLocalIPs(localIP, localIP);
		TheGameSpyInfo->setMaxMessagesPerUpdate(100);
		s_route.know(auth->GetUserID());
		TheTransportRoute = &s_route;
		s_queue->attach();
		TheShell->push(AsciiString("Menus/WOLWelcomeMenu.wnd"));
	}
}

bool OnlineBridge_IsConfigured()
{
	return getenv("ZHR_ONLINE_API") != nullptr;
}

void OnlineBridge_BeginLogin()
{
	ClearGSMessageBoxes();
	NGMP_OnlineServicesManager::CreateInstance();
	NGMP_OnlineServicesManager* manager = NGMP_OnlineServicesManager::GetInstance();
	manager->Init();
	manager->StartVersionCheck([](bool bSuccess, bool bNeedsUpdate)
		{
			if (!bSuccess)
			{
				showFailure(TheGameText->fetch("GUI:GSDisconReason4"));
				return;
			}
			if (bNeedsUpdate)
			{
				showFailure(UnicodeString(L"The online service does not accept this version of the game yet."));
				return;
			}

			NGMP_OnlineServicesManager::GetInstance()->GetAndParseServiceConfig([]()
				{
					NGMP_OnlineServices_AuthInterface* auth = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
					auth->RegisterForLoginCallback(loginFinished);
					auth->BeginLogin();
				});
		});
}

void OnlineBridge_BackToMainMenu()
{
	endSession();
	CancelPatchCheckCallbackAndReopenDropdown();
}

void OnlineBridge_Tick()
{
	NGMP_OnlineServicesManager* manager = NGMP_OnlineServicesManager::GetInstance();
	if (manager == nullptr)
		return;
	manager->Tick();
	if (s_queue != nullptr)
		s_queue->tick();
}

GameSpyPeerMessageQueueInterface* OnlineBridge_NewPeerQueue()
{
	s_queue = NEW OnlinePeerQueue;
	return s_queue;
}

GameSpyPSMessageQueueInterface* OnlineBridge_NewPSQueue()
{
	return NEW OnlineStatsQueue;
}

//-----------------------------------------------------------------------------

void OnlinePeerQueue::attach()
{
	auto* rooms = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	auto* lobby = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

	rooms->RegisterForRoomChangedCallback([this](int roomIndex, bool)
		{
			auto* roomsNow = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
			PeerResponse resp;
			resp.peerResponseType = PeerResponse::PEERRESPONSE_JOINGROUPROOM;
			resp.joinGroupRoom.id = roomsNow->GetGroupRooms()[roomIndex].GetRoomID();
			resp.joinGroupRoom.ok = TRUE;
			addResponse(resp);
		});
	rooms->RegisterForRosterNeedsRefreshCallback([this]() { rosterChangedInRoom(); });
	rooms->m_OnRawChat = [this](const std::string& message, bool isAction) { chatArrived(message, isAction); };
	lobby->m_OnRawChat = [this](const std::string& message, bool isAction) { chatArrived(message, isAction); };
	lobby->RegisterForRosterNeedsRefreshCallback([this]() { rosterChangedInLobby(); });

	lobby->RegisterForGameStartPacket([this]()
		{
			// the host started itself when it asked for this
			if (TheGameSpyInfo->amIHost())
				return;
			PeerResponse resp;
			resp.peerResponseType = PeerResponse::PEERRESPONSE_GAMESTART;
			addResponse(resp);
		});

	lobby->RegisterForCreateLobbyCallback([this](bool ok)
		{
			m_inLobby = ok;
			PeerResponse resp;
			resp.peerResponseType = PeerResponse::PEERRESPONSE_CREATESTAGINGROOM;
			resp.createStagingRoom.result = ok ? PEERJoinSuccess : PEERJoinFailed;
			addResponse(resp);
		});

	lobby->RegisterForJoinLobbyCallback([this](EJoinLobbyResult result)
		{
			auto* lobbyNow = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			const Bool ok = result == EJoinLobbyResult::JoinLobbyResult_Success;
			m_inLobby = ok;
			PeerResponse resp;
			resp.peerResponseType = PeerResponse::PEERRESPONSE_JOINSTAGINGROOM;
			resp.joinStagingRoom.ok = ok ? PEERTrue : PEERFalse;
			resp.joinStagingRoom.isHostPresent = TRUE;
			resp.joinStagingRoom.result = result == EJoinLobbyResult::JoinLobbyResult_BadPassword ? PEERBadPassword
				: result == EJoinLobbyResult::JoinLobbyResult_FullRoom ? PEERFullRoom : PEERJoinFailed;
			resp.joinStagingRoom.id = (Int)lobbyNow->GetCurrentLobby().lobbyID;
			if (ok)
			{
				Int slot = 0;
				for (const LobbyMemberEntry& member : lobbyNow->GetMembersListForCurrentRoom())
				{
					if (member.IsHuman() && slot < MAX_SLOTS)
						resp.stagingRoomPlayerNames[slot++] = asciiOf(member.display_name);
				}
				rosterChangedInLobby();
			}
			addResponse(resp);
		});

	rooms->GetRoomList([this](bool ok)
		{
			if (ok)
				publishRooms();
			else
			{
				PeerResponse resp;
				resp.peerResponseType = PeerResponse::PEERRESPONSE_DISCONNECT;
				resp.discon.reason = DISCONNECT_COULDNOTCONNECT;
				addResponse(resp);
			}
		});
}

void OnlinePeerQueue::publishRooms()
{
	auto* rooms = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	for (const NetworkRoom& room : rooms->GetGroupRooms())
	{
		PeerResponse resp;
		resp.peerResponseType = PeerResponse::PEERRESPONSE_GROUPROOM;
		resp.groupRoom.id = room.GetRoomID();
		resp.groupRoom.numWaiting = 0;
		resp.groupRoom.maxWaiting = 1000;
		resp.groupRoom.numGames = 0;
		resp.groupRoom.numPlaying = 0;
		resp.groupRoomName = asciiOf(to_utf8(room.GetRoomDisplayName().str()));
		addResponse(resp);
	}

	// group room 0 ends the list for the welcome screen
	PeerResponse last;
	last.peerResponseType = PeerResponse::PEERRESPONSE_GROUPROOM;
	last.groupRoom.id = 0;
	last.groupRoom.numWaiting = 0;
	last.groupRoom.maxWaiting = 0;
	last.groupRoom.numGames = 0;
	last.groupRoom.numPlaying = 0;
	addResponse(last);
}

void OnlinePeerQueue::addRequest(const PeerRequest& req)
{
	auto* rooms = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	auto* lobby = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	std::shared_ptr<WebSocket> socket = NGMP_OnlineServicesManager::GetWebSocket();

	switch (req.peerRequestType)
	{
	case PeerRequest::PEERREQUEST_LOGOUT:
		m_listActive = FALSE;
		break;

	case PeerRequest::PEERREQUEST_JOINGROUPROOM:
	{
		const std::vector<NetworkRoom>& list = rooms->GetGroupRooms();
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (list[i].GetRoomID() == req.groupRoom.id)
				rooms->JoinRoom((int)i);
		}
		break;
	}

	case PeerRequest::PEERREQUEST_LEAVEGROUPROOM:
		rooms->LeaveRoom();
		m_roster.clear();
		break;

	case PeerRequest::PEERREQUEST_MESSAGEROOM:
	{
		UnicodeString text(req.text.c_str());
		if (m_inLobby)
			lobby->SendChatMessageToCurrentLobby(text, req.message.isAction != FALSE);
		else
			rooms->SendChatMessageToCurrentRoom(text, req.message.isAction != FALSE);
		break;
	}

	case PeerRequest::PEERREQUEST_STARTGAMELIST:
		m_listActive = TRUE;
		m_nextListRefresh = 0;
		m_listSignature.clear();
		break;

	case PeerRequest::PEERREQUEST_STOPGAMELIST:
		m_listActive = FALSE;
		break;

	case PeerRequest::PEERREQUEST_CREATESTAGINGROOM:
	{
		// the group room is left behind on the service too, or its chat follows the player into the game
		rooms->LeaveRoom();
		m_roster.clear();

		AsciiString portableMap = TheGameState->realMapPathToPortableMapPath(TheGameSpyGame->getMap());
		portableMap.toLower();
		const MapMetaData* map = TheMapCache->findMap(TheGameSpyGame->getMap());
		const Bool official = map != nullptr && map->m_isOfficial;
		lobby->CreateLobby(UnicodeString(req.text.c_str()),
			map != nullptr ? map->m_displayName : UnicodeString(L"Map"), portableMap, official != FALSE,
			map != nullptr ? map->m_numPlayers : MAX_SLOTS, TheGameSpyGame->oldFactionsOnly() != FALSE,
			req.stagingRoomCreation.useStats != FALSE, TheGameSpyGame->getStartingCash().countMoney(),
			!req.password.empty(), req.password, req.stagingRoomCreation.allowObservers != FALSE);
		break;
	}

	case PeerRequest::PEERREQUEST_JOINSTAGINGROOM:
	{
		rooms->LeaveRoom();
		m_roster.clear();
		lobby->JoinLobby(lobby->GetLobbyFromID(req.stagingRoom.id), req.password);
		break;
	}

	case PeerRequest::PEERREQUEST_LEAVESTAGINGROOM:
		// The screens leave the chat channel as a match starts, and over GameSpy that left the game's own
		// sockets running. Here the lobby owns the mesh the match runs on.
		if (TheNetwork != nullptr)
			m_leaveWhenMatchEnds = TRUE;
		else
			leaveLobbyNow();
		break;

	case PeerRequest::PEERREQUEST_SETGAMEOPTIONS:
		// what the list shows only changes with the map; everything else travels in the slot list
		if (m_inLobby && lobby->IsHost() && m_mapPublished != req.gameOptsMapName)
		{
			m_mapPublished = req.gameOptsMapName;
			const MapMetaData* map = TheMapCache->findMap(TheGameSpyGame->getMap());
			lobby->UpdateCurrentLobby_Map(map != nullptr ? map->m_displayName : UnicodeString(L"Map"),
				AsciiString(req.gameOptsMapName.c_str()), map != nullptr && map->m_isOfficial,
				map != nullptr ? map->m_numPlayers : MAX_SLOTS);
		}
		{
			PeerRequest slotList = req;
			slotList.id = "SL/";
			sendUtm(slotList, true);
		}
		break;

	case PeerRequest::PEERREQUEST_UTMROOM:
		if (req.UTM.isStagingRoom)
			sendUtm(req, true);
		break;

	case PeerRequest::PEERREQUEST_UTMPLAYER:
		if (req.UTM.isStagingRoom)
			sendUtm(req, false);
		break;

	case PeerRequest::PEERREQUEST_STARTGAME:
		socket->SendData_StartGame();
		break;

	default:
		// private messages, quick match, pushed stats: the service either has no such thing or this fork
		// does not offer it
		break;
	}
}

void OnlinePeerQueue::leaveLobbyNow()
{
	m_leaveWhenMatchEnds = FALSE;
	m_inLobby = FALSE;
	m_mapPublished.clear();
	m_roster.clear();
	m_pendingUtms.clear();
	NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>()->LeaveCurrentLobby();
}

void OnlinePeerQueue::tick()
{
	NGMP_OnlineServicesManager* manager = NGMP_OnlineServicesManager::GetInstance();

	if (manager->IsPendingFullTeardown())
	{
		manager->ConsumePendingFullTeardown();
		PeerResponse resp;
		resp.peerResponseType = PeerResponse::PEERRESPONSE_DISCONNECT;
		resp.discon.reason = DISCONNECT_LOSTCON;
		addResponse(resp);
		return;
	}

	if (m_leaveWhenMatchEnds && TheNetwork == nullptr)
		leaveLobbyNow();

	pumpMesh();
	flushUtms();

	auto* lobby = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	const UnsignedInt now = timeGetTime();
	if (m_listActive && (lobby->IsLobbyListDirty() || now >= m_nextListRefresh))
	{
		m_nextListRefresh = now + 5000;
		refreshLobbyList();
	}
}

void OnlinePeerQueue::refreshLobbyList()
{
	NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>()->SearchForLobbies(nullptr,
		[this](std::vector<LobbyEntry> lobbies) { publishLobbyList(lobbies); });
}

void OnlinePeerQueue::publishLobbyList(const std::vector<LobbyEntry>& lobbies)
{
	// The service's list is not split by build. A lobby of another build could only refuse the join, so it
	// is not shown; a lobby with another INI is, the room list says why it can't be joined.
	std::string signature;
	std::vector<const LobbyEntry*> shown;
	for (const LobbyEntry& lobby : lobbies)
	{
		if (lobby.exe_crc != TheGlobalData->m_exeCRC)
			continue;
		shown.push_back(&lobby);
		signature += std::format("{}|{}|{}|{}|{}|", lobby.lobbyID, lobby.name, lobby.map_path, lobby.current_players, lobby.max_players);
		for (const LobbyMemberEntry& member : lobby.members)
			signature += member.display_name + ",";
	}
	if (signature == m_listSignature && !signature.empty())
		return;
	m_listSignature = signature;

	PeerResponse clear;
	clear.peerResponseType = PeerResponse::PEERRESPONSE_STAGINGROOM;
	clear.stagingRoom.action = PEER_CLEAR;
	addResponse(clear);

	for (const LobbyEntry* lobby : shown)
	{
		PeerResponse resp;
		resp.peerResponseType = PeerResponse::PEERRESPONSE_STAGINGROOM;
		memset(&resp.stagingRoom, 0, sizeof(resp.stagingRoom));
		resp.stagingRoom.action = PEER_ADD;
		resp.stagingRoom.id = (Int)lobby->lobbyID;
		resp.stagingRoom.isStaging = TRUE;
		resp.stagingRoom.requiresPassword = lobby->passworded;
		resp.stagingRoom.allowObservers = lobby->allow_observers;
		resp.stagingRoom.useStats = lobby->track_stats;
		resp.stagingRoom.exeCRC = lobby->exe_crc;
		resp.stagingRoom.iniCRC = lobby->ini_crc;
		resp.stagingRoom.numPlayers = lobby->current_players;
		resp.stagingRoom.maxPlayers = lobby->max_players;
		resp.stagingRoom.percentComplete = 100;
		resp.stagingServerName = from_utf8(lobby->name).c_str();
		resp.stagingRoomMapName = lobby->map_path;
		Int slot = 0;
		for (const LobbyMemberEntry& member : lobby->members)
		{
			if (member.IsHuman() && slot < MAX_SLOTS)
			{
				resp.stagingRoom.profileID[slot] = (Int)member.user_id;
				resp.stagingRoomPlayerNames[slot++] = asciiOf(member.display_name);
			}
		}
		addResponse(resp);
	}

	PeerResponse done;
	done.peerResponseType = PeerResponse::PEERRESPONSE_STAGINGROOMLISTCOMPLETE;
	addResponse(done);
}

void OnlinePeerQueue::playerResponse(PeerResponse& resp, int64_t userID, const std::string& name, RoomType roomType)
{
	const UnsignedInt ip = addressForUser(userID);
	resp.nick = asciiOf(name);
	memset(&resp.player, 0, sizeof(resp.player));
	resp.player.profileID = (Int)userID;
	resp.player.roomType = roomType;
	resp.player.IP = htonl(ip);
	resp.player.internalIP = ip;
	resp.player.externalIP = ip;
}

void OnlinePeerQueue::diffRoster(const std::map<int64_t, std::string>& now, RoomType roomType)
{
	const int64_t self = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>()->GetUserID();

	for (const auto& [userID, name] : m_roster)
	{
		if (now.find(userID) == now.end())
		{
			PeerResponse resp;
			resp.peerResponseType = PeerResponse::PEERRESPONSE_PLAYERLEFT;
			playerResponse(resp, userID, name, roomType);
			addResponse(resp);
		}
	}
	for (const auto& [userID, name] : now)
	{
		if (m_roster.find(userID) == m_roster.end())
		{
			s_route.know(userID);
			PeerResponse resp;
			resp.peerResponseType = userID == self ? PeerResponse::PEERRESPONSE_PLAYERINFO : PeerResponse::PEERRESPONSE_PLAYERJOIN;
			playerResponse(resp, userID, name, roomType);
			addResponse(resp);
		}
	}
	m_roster = now;
}

void OnlinePeerQueue::rosterChangedInRoom()
{
	if (m_inLobby)
		return;
	std::map<int64_t, std::string> now;
	for (const auto& [userID, member] : NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>()->GetMembersListForCurrentRoom())
		now[(int64_t)userID] = member.display_name;
	diffRoster(now, GroupRoom);
}

void OnlinePeerQueue::rosterChangedInLobby()
{
	std::map<int64_t, std::string> now;
	for (const LobbyMemberEntry& member : NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>()->GetMembersListForCurrentRoom())
	{
		if (member.IsHuman())
			now[member.user_id] = member.display_name;
	}
	diffRoster(now, StagingRoom);
}

/// The service formats a line as "[name] text", or "name text" for an action. The screens want them apart.
void OnlinePeerQueue::chatArrived(const std::string& message, bool isAction)
{
	PeerResponse resp;
	resp.peerResponseType = PeerResponse::PEERRESPONSE_MESSAGE;
	memset(&resp.message, 0, sizeof(resp.message));
	resp.message.isAction = isAction;

	std::string nick, text;
	if (isAction)
	{
		for (const auto& [userID, name] : m_roster)
		{
			if (message.rfind(name + " ", 0) == 0)
			{
				nick = name;
				text = message.substr(name.size() + 1);
				resp.message.profileID = (Int)userID;
			}
		}
	}
	else if (!message.empty() && message[0] == '[')
	{
		const size_t close = message.find("] ");
		if (close != std::string::npos)
		{
			nick = message.substr(1, close - 1);
			text = message.substr(close + 2);
			const int64_t userID = userFromNick(asciiOf(nick));
			resp.message.profileID = (Int)userID;
		}
	}
	if (nick.empty())
		return;

	resp.nick = asciiOf(nick);
	resp.text = from_utf8(text).c_str();
	addResponse(resp);
}

int64_t OnlinePeerQueue::userFromNick(const std::string& nick) const
{
	for (const auto& [userID, name] : m_roster)
	{
		if (asciiOf(name) == nick)
			return userID;
	}
	return -1;
}

std::string OnlinePeerQueue::nickFromUser(int64_t userID) const
{
	auto member = m_roster.find(userID);
	return member == m_roster.end() ? std::string() : asciiOf(member->second);
}

/// A UTM is the room's way of saying something to the screens of the others: the host's slot list, a
/// player's accept, a kick. They travel on the mesh, on the channel the anticheat plugin has upstream, and
/// wait for a connection that is still coming up.
void OnlinePeerQueue::sendUtm(const PeerRequest& req, bool toRoom)
{
	std::string wire;
	wire += toRoom ? '\1' : '\0';
	wire += req.id;
	wire += '\0';
	wire += req.options;

	if (toRoom)
	{
		const int64_t self = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>()->GetUserID();
		for (const auto& [userID, name] : m_roster)
		{
			if (userID != self)
				m_pendingUtms[userID].push_back(wire);
		}
	}
	else
	{
		const int64_t userID = userFromNick(req.nick);
		if (userID != -1)
			m_pendingUtms[userID].push_back(wire);
	}
	flushUtms();
}

void OnlinePeerQueue::flushUtms()
{
	NetworkMesh* mesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if (mesh == nullptr)
		return;
	for (auto& [userID, waiting] : m_pendingUtms)
	{
		PlayerConnection* connection = mesh->GetConnectionForUser(userID);
		if (connection == nullptr || connection->GetState() != EConnectionState::CONNECTED_DIRECT)
			continue;
		while (!waiting.empty())
		{
			connection->SendACPacket(waiting.front().data(), (uint32_t)waiting.front().size());
			waiting.pop_front();
		}
	}
}

void OnlinePeerQueue::pumpMesh()
{
	NetworkMesh* mesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if (mesh == nullptr)
		return;
	for (auto& [userID, connection] : mesh->GetAllConnections())
	{
		if (connection.m_hSteamConnection == k_HSteamNetConnection_Invalid)
			continue;
		SteamNetworkingMessage_t* messages[255];
		const int count = connection.Recv(messages);
		for (int i = 0; i < count; ++i)
		{
			const unsigned char* data = (const unsigned char*)messages[i]->GetData();
			const UnsignedInt size = (UnsignedInt)messages[i]->m_cbSize;
			if (size > 0 && data[0] == (unsigned char)ENetworkChannel::NETWORK_CHANNEL_GAME)
				s_route.arrived(userID, data + 1, size - 1);
			else if (size > 0 && data[0] == (unsigned char)ENetworkChannel::NETWORK_CHANNEL_AC)
				utmArrived(userID, data + 1, size - 1);
			messages[i]->Release();
		}
	}
}

void OnlinePeerQueue::utmArrived(int64_t userID, const unsigned char* data, UnsignedInt size)
{
	const std::string wire((const char*)data, size);
	const size_t split = wire.find('\0', 1);
	if (size < 2 || split == std::string::npos)
		return;

	PeerResponse resp;
	resp.peerResponseType = wire[0] ? PeerResponse::PEERRESPONSE_ROOMUTM : PeerResponse::PEERRESPONSE_PLAYERUTM;
	resp.nick = nickFromUser(userID);
	resp.command = wire.substr(1, split - 1);
	if (!resp.command.empty() && resp.command.back() == '/')
		resp.command.pop_back();
	resp.commandOptions = wire.substr(split + 1);
	addResponse(resp);
}

/// Called by the lobby interface when the service removes this player from the lobby.
void OnKickedFromLobby()
{
}
