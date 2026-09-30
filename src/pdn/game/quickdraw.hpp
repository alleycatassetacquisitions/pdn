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
#include "wireless/symbol-wireless-manager.hpp"

class FirmwareUpdateManager;
class FirmwareStoreInterface;

constexpr size_t MATCH_SIZE = sizeof(Match);

constexpr int QUICKDRAW_APP_ID = 1;

class Quickdraw : public StateMachine {
public:
    /// Constructs and owns matchManager/chainDuelManager/shootoutManager_/firmwareUpdateManager
    /// and wires their ESP-NOW packet handlers. firmwareStore backs firmwareUpdateManager's
    /// flash slot; the caller owns and outlives it (main.cpp constructs it alongside the
    /// other drivers).
    Quickdraw(Player* player, Device* pdn, QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager, SymbolWirelessManager* symbolWirelessManager, FirmwareStoreInterface* firmwareStore);
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

    /// The firmware-update manager this Quickdraw owns, so main.cpp's loop()
    /// can pump its sync() whatever app is active. Null when constructed
    /// without a firmware store (the headless simulator).
    FirmwareUpdateManager* getFirmwareUpdateManager();

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
    SymbolWirelessManager* symbolWirelessManager;
    RemoteDebugManager* remoteDebugManager;
    SupporterReady* supporterReadyState = nullptr;
    ChainDuelManager* chainDuelManager = nullptr;
    ShootoutManager* shootoutManager_ = nullptr;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;

    // Every kStatsLogIntervalMs we emit one LOG_I line with the current retry
    // counters from both RDC and CDM. Intended for venue deployment: `cat`ing
    // the serial port gives a rolling view of retry-machine health. Parseable
    // by scripts/chain_status.sh (prefix: "STATS").
    SimpleTimer statsLogTimer_;
    static constexpr unsigned long kStatsLogIntervalMs = 5000;

    // Diagnostic: track isLoop() transitions to expose ring re-formation timing.
    bool lastIsLoop_ = false;
};
