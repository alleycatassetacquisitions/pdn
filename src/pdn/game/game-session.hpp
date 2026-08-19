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
#include <array>

/// Owns the managers the gameplay apps share and the wireless plumbing that
/// feeds them, and sits above every app so neither is tied to one state machine.
///
/// getContext() hands those managers out by value, so an app built from one — and
/// any state of that app which keeps a manager — holds a pointer the session frees.
/// Destroy the apps before the session. No state destructor dereferences one today,
/// so that is ordering hygiene rather than a live use-after-free.
///
/// sync() must run on each platform tick, not from a state: only the mounted app
/// receives an onStateLoop, so a retry machine driven from inside a state stalls
/// the moment the device swaps apps — silently, because nothing on the wire says
/// a retransmit was skipped. Device::setTickCallback is the seam that runs it.
class GameSession {
public:
    /// Builds the shared managers, wires every RDC callback and ESP-NOW handler
    /// that feeds them, and installs its own per-tick pump. The device and its
    /// wireless manager outlive the session, so the destructor empties every slot
    /// holding `this`.
    GameSession(Player* player,
                Device* pdn,
                QuickdrawWirelessManager* quickdrawWirelessManager,
                SymbolWirelessManager* symbolWirelessManager);
    /// Drops the callbacks that capture `this`, then frees the managers it owns.
    ~GameSession();

    /// Drives the chain-duel and shootout retry machines and the periodic retry
    /// stats line. Installed on the device's tick seam by the constructor.
    void sync();

    /// The manager bundle every gameplay state is constructed from.
    GameContext getContext();

private:
    /// The mounted SupporterReady, or null when another state holds the device.
    /// Read from the mounted app rather than published into the session: which
    /// state is running is the app's fact, and a second copy of it can drift.
    SupporterReady* getMountedSupporterReady();

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

    /// One row per packet type this session answers for. The constructor
    /// registers every row and the destructor clears every row, so the two lists
    /// cannot drift apart the way two hand-written ones can.
    struct PacketRoute {
        PktType type;
        PeerCommsInterface::PacketCallback handler;
    };
    static const std::array<PacketRoute, 8>& packetRoutes();

    /// Wraps a member handler in the C-style callback the radio takes. The member
    /// is a template parameter, so the pointer-to-member call resolves at compile
    /// time; the row still stores a std::function, as the lambdas it replaced did.
    template <void (GameSession::*Handler)(const uint8_t*, const uint8_t*, size_t)>
    static void dispatchTo(const uint8_t* fromMac, const uint8_t* data,
                           const size_t dataLen, void* ctx) {
        (static_cast<GameSession*>(ctx)->*Handler)(fromMac, data, dataLen);
    }

    Player* player = nullptr;
    Device* pdn = nullptr;
    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;
    SymbolWirelessManager* symbolWirelessManager = nullptr;
    MatchManager* matchManager = nullptr;
    ChainDuelManager* chainDuelManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;

    // Every STATS_LOG_INTERVAL_MS we emit one line of the chain-duel retry
    // counters under the "STATS" tag. Intended for venue deployment: `cat`ing the
    // serial port gives a rolling view of retry-machine health.
    SimpleTimer statsLogTimer;
    static constexpr unsigned long STATS_LOG_INTERVAL_MS = 5000;

    // Diagnostic: track isLoop() transitions to expose ring re-formation timing.
    bool lastIsLoop = false;
};
