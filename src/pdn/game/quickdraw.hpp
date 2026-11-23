#pragma once

#include "device/device.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "apps/player-fetch/player-fetch.hpp"
#include "game/player-provider.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/shootout-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"

class DeviceTestWirelessManager;
class FirmwareUpdateManager;

constexpr size_t MATCH_SIZE = sizeof(Match);

constexpr int QUICKDRAW_APP_ID = 1;

class Quickdraw : public StateMachine {
public:
    /** Builds the game managers and registers their packet handlers. */
    Quickdraw(PlayerProvider* playerProvider, Device* pdn,
              QuickdrawWirelessManager* quickdrawWirelessManager,
              RemoteDebugManager* remoteDebugManager, SymbolWirelessManager* symbolWirelessManager);
    ~Quickdraw();

    void populateStateMap() override;

    // Static entry points for ESP-NOW packet handlers. Route to the
    // current state if it's SupporterReady (for game events) or to the
    // MatchManager/champion-side confirm tracker (for confirms).
    void onChainGameEventPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onChainGameEventAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onChainConfirmPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onRoleAnnouncePacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onRoleAnnounceAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onShootoutCommandPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onShootoutCommandAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    /** @copydoc StateMachine::onStateLoop */
    void onStateLoop(Device* pdn) override;

private:
    void onChainStateChanged();

    std::vector<Match> matches;
    int numMatches = 0;
    PlayerProvider* playerProvider = nullptr;
    WirelessManager* wirelessManager;
    StorageInterface* storageManager;
    PeerCommsInterface* peerComms;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    Player* player;
    MatchManager* matchManager;
    ChainDuelManager* chainDuelManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    SymbolWirelessManager* symbolWirelessManager;
    RemoteDebugManager* remoteDebugManager;
    SupporterReady* supporterReadyState = nullptr;

    // Diagnostic: track isLoop() transitions to expose ring re-formation timing.
    bool lastIsLoop_ = false;
};
