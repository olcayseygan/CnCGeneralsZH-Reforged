#pragma once

#include "NGMP_include.h"
#include "NetworkMesh.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "OnlineServices_Init.h"
#include "Common/MultiplayerSettings.h"

#include <chrono>
#include <optional>

extern NGMPGame* TheNGMPGame;

enum class EChatMessageType
{
	CHAT_MESSAGE_TYPE_NETWORK_ROOM,
	CHAT_MESSAGE_TYPE_LOBBY
};
static Color DetermineSystemNoticeColor(bool bWarning = false, bool bError = false)
{
	if (bError)
	{
		return GameMakeColor(255, 94, 94, 255);
	}
	if (bWarning)
	{
		return GameMakeColor(255, 194, 15, 255);
	}
	return GameMakeColor(192, 192, 192, 255);
}

static Color DetermineColorForChatMessage(EChatMessageType chatMessageType, Bool isPublic, bool bAction, bool bAdmin, bool bIsNameChange, int lobbySlot = -1)
{
	Color style = GameMakeColor(255, 255, 255, 255);

	// TODO_NGMP: Support owner chat again
	Bool isOwner = false;

	if (isPublic && bAction)
	{
		style = (isOwner) ? GameSpyColor[GSCOLOR_CHAT_OWNER_EMOTE] : GameSpyColor[GSCOLOR_CHAT_EMOTE];
	}
    else if (isPublic && bIsNameChange)
    {
        style = GameMakeColor(127, 127, 127, 255);
    }
	else if (isPublic)
	{
		// use lobby colors
		if (chatMessageType == EChatMessageType::CHAT_MESSAGE_TYPE_LOBBY)
		{
			if (lobbySlot == -1)
			{
				return GameMakeColor(255, 255, 255, 255);
			}
			else
			{
				if (TheNGMPGame)
				{
					GameSlot* pSlot = TheNGMPGame->getSlot(lobbySlot);

					if (pSlot != nullptr)
					{
						int numColors = TheMultiplayerSettings->getNumColors();
						int color = pSlot->getColor();
						if (color > -1 && color < numColors)
						{
							MultiplayerColorDefinition* def = TheMultiplayerSettings->getColor(color);
							style = def->getColor();
						}
					}
				}
			}
		}
		else
		{
			if (bAdmin)
			{
				style = GameMakeColor(0, 162, 232, 255);
			}
			else
			{
				style = (isOwner) ? GameSpyColor[GSCOLOR_CHAT_OWNER] : GameSpyColor[GSCOLOR_CHAT_NORMAL];
			}
		}
	}
	else if (bAction)
	{
		style = (isOwner) ? GameSpyColor[GSCOLOR_CHAT_PRIVATE_OWNER_EMOTE] : GameSpyColor[GSCOLOR_CHAT_PRIVATE_EMOTE];
	}
	else
	{
		style = (isOwner) ? GameSpyColor[GSCOLOR_CHAT_PRIVATE_OWNER] : GameSpyColor[GSCOLOR_CHAT_PRIVATE];
	}

	// filters language
//  if( TheGlobalData->m_languageFilterPref )
//  {
	//TheLanguageFilter->filterLine(msg);
	//	}

	return style;
}

struct NGMP_RoomInfo
{
	int numMembers;
	int maxMembers;
};

class NetworkRoomMember : public NetworkMemberBase
{
public:
	bool IsValid() const { return user_id != -1; }
};

struct RoomSelectionResult
{
	std::optional<uint64_t> requestID;
	std::optional<int> selectedRoomID;
	std::optional<int> effectiveRoomID;
	std::optional<int> rejectedRoomID;
	std::string error;
};

class NGMP_OnlineServices_RoomsInterface
{
public:
	NGMP_OnlineServices_RoomsInterface();

	void GetRoomList(std::function<void(bool)> cb);

	void JoinRoom(int roomIndex);

	void LeaveRoom()
	{
		m_CurrentRoomIndex = -1;
		m_EffectiveRoomID.reset();
		m_PendingRoomChange.reset();
		m_bRoomSelectionResultsSupported = false;
		m_vecRooms.clear();

		std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
		if (pWS != nullptr)
		{
			pWS->SendData_LeaveNetworkRoom();
		}
	}

	std::function<void(const std::string& strMessage, bool bIsAction)> m_OnRawChat = nullptr;
	std::function<void(UnicodeString strMessage, Color color)> m_OnChatCallback = nullptr;
	void RegisterForChatCallback(std::function<void(UnicodeString strMessage, Color color)> cb)
	{
		m_OnChatCallback = cb;
	}

	void DeregisterForChatCallback()
	{
		m_OnChatCallback = nullptr;
	}

	std::function<void()> m_RosterNeedsRefreshCallback = nullptr;
	mutable std::mutex m_rosterCallbackMutex;
	void RegisterForRosterNeedsRefreshCallback(std::function<void()> cb)
	{
		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		m_RosterNeedsRefreshCallback = cb;
	}

	void DeregisterForRosterNeedsRefreshCallback()
	{
		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		m_RosterNeedsRefreshCallback = nullptr;
	}

	void RegisterForRoomChangedCallback(std::function<void(int, bool)> cb)
	{
		m_RoomChangedCallback = std::move(cb);
	}

	void DeregisterForRoomChangedCallback()
	{
		m_RoomChangedCallback = nullptr;
	}

	NetworkRoomMember* GetRoomMemberFromIndex(int index)
	{
		if (m_mapMembers.size() > index)
		{
			auto it = m_mapMembers.begin();
			std::advance(it, index);
			return &it->second;
		}

		return nullptr;
	}

	NetworkRoomMember* GetRoomMemberFromID(int64_t puid)
	{
		if (m_mapMembers.contains(puid))
		{
			return &m_mapMembers[puid];
		}

		return nullptr;
	}

	std::unordered_map<uint64_t, NetworkRoomMember>& GetMembersListForCurrentRoom();

	// Chat
	void SendChatMessageToCurrentRoom(UnicodeString& strChatMsg, bool bIsAction);

	void ResetCachedRoomData()
	{
		m_mapMembers.clear();
	
		std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
		if (m_RosterNeedsRefreshCallback != nullptr)
		{
			m_RosterNeedsRefreshCallback();
		}
	}

	void Tick();

	const std::vector<NetworkRoom>& GetGroupRooms() const { return m_vecRooms; }

	void OnRosterUpdated(std::unordered_map<uint64_t, NetworkRoomMember> mapMembers, const RoomSelectionResult& selectionResult);
	bool SupportsModerationCommands() const { return m_bSupportsModerationCommands; }

	int GetCurrentRoomIndex() const { return m_CurrentRoomIndex; }

private:
	struct PendingRoomChange
	{
		int roomIndex;
		int roomID;
		uint64_t requestID;
		std::chrono::steady_clock::time_point deadline;
	};

	int m_CurrentRoomIndex = -1;
	std::optional<int> m_EffectiveRoomID;
	std::optional<PendingRoomChange> m_PendingRoomChange;
	uint64_t m_NextRoomChangeRequestID = 1;
	bool m_bRoomSelectionResultsSupported = false;
	bool m_bSupportsModerationCommands = false;
	std::function<void(int, bool)> m_RoomChangedCallback = nullptr;
	void ReportRoomJoinFailure(const std::string& error);

	std::vector<NetworkRoom> m_vecRooms;

	std::unordered_map<uint64_t, NetworkRoomMember> m_mapMembers = std::unordered_map<uint64_t, NetworkRoomMember>();
};
