#pragma once

#include <cstdint>

enum class EConnectionState : uint8_t
{
	NOT_CONNECTED,
	CONNECTING_DIRECT,
	FINDING_ROUTE,
	CONNECTED_DIRECT,
	CONNECTION_FAILED,
	CONNECTION_DISCONNECTED
};

enum class ENetworkChannels : uint8_t
{
	Game = 0,
	Anticheat,
	Signalling,
	Ping,
	Pong
};

// Their client loads an anticheat plugin here. Those plugins are x86 and this build is 64-bit, so there
// is none, and every plugin path in the ported code is dead. This stays so those call sites keep their
// upstream shape and a later merge from upstream stays readable.
class AnticheatPlugInterface
{
public:
	static bool DoesACPluginProvideSecureGameTransport() { return false; }
	static void StartSignalling(const char*, int64_t, uint16_t) {}
	static void DisconnectPlayer(const char*) {}
	static void DisconnectAll() {}
	static void AC_NetworkMessageArrived(int64_t, const void*, uint32_t) {}
	static int SendPacket(const char*, int64_t, const void*, uint32_t, ENetworkChannels) { return 0; }
	static bool IsConnectionRelayed(const char*) { return false; }
	static int GetConnectionLatencyForUser(const char*) { return 0; }
	static void Authenticate() {}
	static void BeginSession() {}
	static void EndSession() {}
	static int GetAnticheatIdentifier() { return 0; }
	static void RegisterPlayer(int64_t) {}
	static void DeregisterPlayer(int64_t) {}
	static void Tick() {}
};
