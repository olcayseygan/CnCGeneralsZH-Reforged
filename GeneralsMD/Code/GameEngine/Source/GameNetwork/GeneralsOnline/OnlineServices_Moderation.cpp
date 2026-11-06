#include "GameNetwork/GeneralsOnline/OnlineServices_Moderation.h"

#include "GameNetwork/GeneralsOnline/NGMP_include.h"
#include "GameNetwork/GeneralsOnline/OnlineBridge.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_Init.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_LobbyInterface.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_RoomsInterface.h"
#include "GameNetwork/GameSpyOverlay.h"

#include <chrono>
#include <map>
#include <stdexcept>

namespace
{
	std::wstring GetNormalizedReason(const std::string& reason)
	{
		try
		{
			return NormalizeSingleLineText(from_utf8(reason));
		}
		catch (const std::range_error&)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[MODERATION]: Ignoring an invalid UTF-8 reason");
			return std::wstring();
		}
	}

	void AppendReason(std::wstring& message, const std::string& reason)
	{
		const std::wstring normalizedReason = GetNormalizedReason(reason);
		if (!normalizedReason.empty())
		{
			message += L"\n\nReason: ";
			message += normalizedReason;
		}
	}

	void ShowModerationDialog(
		UnicodeString title,
		std::wstring message,
		const std::string& reason,
		GameWinMsgBoxFunc closeCallback)
	{
		AppendReason(message, reason);
		GSMessageBoxOk(title, UnicodeString(message.c_str()), closeCallback);
	}

	void ShowBanDialog(const std::string& reason, GameWinMsgBoxFunc closeCallback)
	{
		ShowModerationDialog(
			UnicodeString(L"Banned"),
			L"You have been banned from the online service.",
			reason,
			closeCallback);
	}

	void ShowKickDialog(const std::string& reason)
	{
		ShowModerationDialog(
			UnicodeString(L"Kicked"),
			L"You have been kicked from the online service.",
			reason,
			nullptr);
	}
}

void ShowLoginBanDialog(const std::string& reason)
{
	ShowBanDialog(reason, OnlineBridge_BackToMainMenu);
}

void ShowChatRateLimitNotice(const std::string& reason, const std::string& scopeType)
{
	using namespace std::chrono;

	const std::wstring normalizedReason = GetNormalizedReason(reason);
	if (normalizedReason.empty())
	{
		return;
	}

	using NoticeKey = std::pair<std::string, std::wstring>;
	static std::map<NoticeKey, steady_clock::time_point> lastNotices;

	const auto now = steady_clock::now();
	const auto window = seconds(9);
	for (auto it = lastNotices.begin(); it != lastNotices.end();)
	{
		if (now - it->second >= window)
		{
			it = lastNotices.erase(it);
		}
		else
		{
			++it;
		}
	}

	const NoticeKey key(scopeType, normalizedReason);
	if (lastNotices.find(key) != lastNotices.end())
	{
		return;
	}
	lastNotices[key] = now;

	UnicodeString message(normalizedReason.c_str());
	const Color color = DetermineSystemNoticeColor(true, false);
	if (scopeType == "lobby")
	{
		NGMP_OnlineServices_LobbyInterface* lobbyInterface =
			NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if (lobbyInterface != nullptr && lobbyInterface->m_OnChatCallback != nullptr)
		{
			lobbyInterface->m_OnChatCallback(message, color);
		}
		return;
	}

	NGMP_OnlineServices_RoomsInterface* roomsInterface =
		NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
	if (roomsInterface != nullptr && roomsInterface->m_OnChatCallback != nullptr)
	{
		roomsInterface->m_OnChatCallback(message, color);
	}
}

void HandleModerationDisconnect(EOnlineModerationAction action, const std::string& reason)
{
	NGMP_OnlineServicesManager* manager = NGMP_OnlineServicesManager::GetInstance();
	if (manager == nullptr || IsModerationTeardownReason(manager->GetTeardownReason()))
	{
		return;
	}

	switch (action)
	{
	case EOnlineModerationAction::BAN:
		manager->SetPendingFullTeardown(EGOTearDownReason::MODERATION_BAN);
		ShowBanDialog(reason, nullptr);
		break;

	case EOnlineModerationAction::KICK:
		manager->SetPendingFullTeardown(EGOTearDownReason::MODERATION_KICK);
		ShowKickDialog(reason);
		break;
	}
}

void HandleModerationNotice(const std::string& actionType, const std::string& reason, const std::string& scopeType)
{
	if (actionType == "ban")
	{
		HandleModerationDisconnect(EOnlineModerationAction::BAN, reason);
	}
	else if (actionType == "kick")
	{
		HandleModerationDisconnect(EOnlineModerationAction::KICK, reason);
	}
	else if (actionType == "rate_limit")
	{
		ShowChatRateLimitNotice(reason, scopeType);
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Unknown moderation notice: %s", actionType.c_str());
	}
}
