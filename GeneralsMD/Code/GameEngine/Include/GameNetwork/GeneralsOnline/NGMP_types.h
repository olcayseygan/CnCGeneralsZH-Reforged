#pragma once

class NetworkMemberBase
{
public:
	int64_t user_id = -1;
	std::string display_name;

	bool m_bIsHost = false;

	bool m_bIsReady = false;

	bool m_bIsAdmin = false;

	// Precomputed lowercase key for fast case-insensitive sorting
	std::string sort_key;
};

enum class ELoginResult
{
    Success,
    Failed,
    UserCancelled
};
