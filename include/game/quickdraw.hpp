#pragma once

#include "device/device.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "apps/player-registration/player-registration.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "device/drivers/storage-interface.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/shootout-manager.hpp"
#include <memory>

constexpr size_t MATCH_SIZE = sizeof(Match);

constexpr int QUICKDRAW_APP_ID = 1;

class Quickdraw : public StateMachine {
public:
    Quickdraw(Player *player, Device *PDN, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager);
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
    void onStateLoop(Device *PDN) override;

private:
    void onChainStateChanged();

    std::vector<Match> matches;
    int numMatches = 0;
    MatchManager* matchManager;
    Player *player;
    WirelessManager* wirelessManager;
    StorageInterface* storageManager;
    PeerCommsInterface* peerComms;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    RemoteDebugManager* remoteDebugManager;
    SupporterReady* supporterReadyState = nullptr;
    ChainDuelManager* chainDuelManager = nullptr;
    std::unique_ptr<ShootoutManager> shootoutManager_;

    // Every kStatsLogIntervalMs we emit one LOG_I line with the current retry
    // counters from both RDC and CDM. Intended for venue deployment: `cat`ing
    // the serial port gives a rolling view of retry-machine health. Parseable
    // by scripts/chain_status.sh (prefix: "STATS").
    SimpleTimer statsLogTimer_;
    static constexpr unsigned long kStatsLogIntervalMs = 5000;
};
