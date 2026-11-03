#pragma once

// The seam between the online screens and the lobby service. The screens already talk to the network
// through the GameSpy queue interfaces, so the service sits behind those: lobby, staging room and chat keep
// their own code, and what used to be a GameSpy thread is OnlinePeerQueue. Everything here runs on the main
// thread and compiles only when the libraries are there (ZH_ONLINE, Tools/online-libs.ps1).
//
// The service is named by the environment, never in this repository. Nothing happens unless ZHR_ONLINE_API
// is set, and the Online button then behaves as it always did:
//   ZHR_ONLINE_API     base of the REST endpoints, ending in /contract/<n>
//   ZHR_ONLINE_LOGIN   the page a player signs in on; unset means a stand-in that approves codes by itself
//   ZHR_ONLINE_STUN    STUN list, as the GameNetworkingSockets config takes it
//   ZHR_ONLINE_TURN    TURN list, same form

class GameSpyPeerMessageQueueInterface;
class GameSpyPSMessageQueueInterface;

bool OnlineBridge_IsConfigured();

/// Online button: version check, then login, then the welcome screen. Reports failure in a message box and
/// hands the main menu back through CancelPatchCheckCallbackAndReopenDropdown.
void OnlineBridge_BeginLogin();

/// Drops the session and reopens the main menu's dropdown.
void OnlineBridge_BackToMainMenu();

/// One engine pass. Costs one pointer test unless a login has started.
void OnlineBridge_Tick();

GameSpyPeerMessageQueueInterface* OnlineBridge_NewPeerQueue();
GameSpyPSMessageQueueInterface* OnlineBridge_NewPSQueue();
