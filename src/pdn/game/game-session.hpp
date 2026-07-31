#pragma once

#include "game/quickdraw-states.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/match-manager.hpp"
#include "game/shootout-manager.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"

/// Owns the managers the gameplay apps share and the wireless plumbing that
/// feeds them, and sits above every app so neither is tied to one state machine.
///
/// sync() must run on each platform tick, not from a state: only the mounted app
/// receives an onStateLoop, so a retry machine driven from inside a state stalls
/// the moment the device swaps apps — silently, because nothing on the wire says
/// a retransmit was skipped. Device::setTickCallback is the seam that runs it.
class GameSession {
public:
    /// Builds the shared managers and wires every RDC callback and ESP-NOW
    /// handler that feeds them. The device and its wireless manager outlive the
    /// session, so the destructor empties every slot holding `this`.
    GameSession(Player* player,
                Device* pdn,
                QuickdrawWirelessManager* quickdrawWirelessManager,
                SymbolWirelessManager* symbolWirelessManager);
    /// Drops the callbacks that capture `this`, then frees the managers it owns.
    ~GameSession();

    /// Drives the chain-duel and shootout retry machines and the periodic retry
    /// stats line. One call per platform tick.
    void sync();

    /// The manager bundle every gameplay state is constructed from.
    GameContext getContext();

    /// The MatchManager the registration app writes fetched player data into.
    MatchManager* getMatchManager();

    /// Published by SupporterReady while it is mounted, so an inbound chain game
    /// event reaches it without the session knowing which app holds it.
    void setActiveSupporterReady(SupporterReady* supporterReady);

private:
    void onChainStateChanged();
    void onChainGameEventPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onChainGameEventAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onChainConfirmPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    /// Champion-side enrolment of a supporter this device shares no cable with.
    void onChainJoinPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onRoleAnnouncePacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onRoleAnnounceAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onShootoutCommandPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onShootoutCommandAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void logRetryStats();

    Player* player = nullptr;
    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;
    SymbolWirelessManager* symbolWirelessManager = nullptr;
    MatchManager* matchManager = nullptr;
    ChainDuelManager* chainDuelManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;
    SupporterReady* activeSupporterReady = nullptr;

    // Every STATS_LOG_INTERVAL_MS we emit one line of the chain-duel retry
    // counters under the "STATS" tag. Intended for venue deployment: `cat`ing the
    // serial port gives a rolling view of retry-machine health.
    SimpleTimer statsLogTimer;
    static constexpr unsigned long STATS_LOG_INTERVAL_MS = 5000;

    // Diagnostic: track isLoop() transitions to expose ring re-formation timing.
    bool lastIsLoop = false;
};
