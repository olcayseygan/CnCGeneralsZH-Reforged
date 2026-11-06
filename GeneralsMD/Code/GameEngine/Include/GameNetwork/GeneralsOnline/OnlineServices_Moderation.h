#pragma once

#include <string>

enum class EOnlineModerationAction
{
	BAN,
	KICK
};

void ShowLoginBanDialog(const std::string& reason);
void ShowChatRateLimitNotice(const std::string& reason, const std::string& scopeType);
void HandleModerationDisconnect(EOnlineModerationAction action, const std::string& reason);
void HandleModerationNotice(const std::string& actionType, const std::string& reason, const std::string& scopeType);
