#pragma once

#include "game/quickdraw-states.hpp"
#include "game/posse-manager.hpp"
#include "game/match-manager.hpp"
#include "game/shootout-manager.hpp"
#include "game/record-relay.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include <array>

/**
 * Owns the managers and wireless plumbing the gameplay apps share. Destroy the
 * apps before the session: they hold pointers to its managers. sync() must run on
 * each platform tick, not from a state: only the mounted app gets an onStateLoop.
 */
class GameSession {
public:
    /**
     * Builds the shared managers, wires every RDC callback and ESP-NOW handler
     * that feeds them, and installs its own per-tick pump. The device and its
     * wireless manager outlive the session, so the destructor empties every slot
     * holding `this`.
     */
    GameSession(Player* player,
                Device* pdn,
                SymbolWirelessManager* symbolWirelessManager);
    /** Drops the callbacks that capture `this`, then frees the managers it owns. */
    ~GameSession();

    /** Drives the session's retry machines and the periodic retry stats line. */
    void sync();

    /**
     * Hands the record relay the source of this device's crash records; null
     * detaches it. Not owned, and it must outlive the session.
     */
    void setCrashRecordSource(RecordSource<CrashRecordTraits>* crashRecordSource);

    /** The manager bundle every gameplay state is constructed from. */
    GameContext getContext();

private:
    /** The mounted SupporterReady, or null when another state holds the device. */
    SupporterReady* getMountedSupporterReady();

    void onChainGameEventPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onChainGameEventAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onShootoutCommandPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onShootoutCommandAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void logRetryStats();

    /** One row per packet type; the constructor registers and the destructor clears every row. */
    struct PacketRoute {
        PktType type;
        PeerCommsInterface::PacketCallback handler;
    };
    static const std::array<PacketRoute, 4>& packetRoutes();

    /** Adapts a member handler to the radio's C-style callback; ctx is the GameSession. */
    template <void (GameSession::*Handler)(const uint8_t*, const uint8_t*, size_t)>
    static void dispatchTo(const uint8_t* fromMac, const uint8_t* data,
                           const size_t dataLen, void* ctx) {
        (static_cast<GameSession*>(ctx)->*Handler)(fromMac, data, dataLen);
    }

    Player* player = nullptr;
    Device* pdn = nullptr;
    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    SymbolWirelessManager* symbolWirelessManager = nullptr;
    MatchManager* matchManager = nullptr;
    PosseManager* posseManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;
    RecordRelay* recordRelay = nullptr;

    SimpleTimer statsLogTimer;
    static constexpr unsigned long STATS_LOG_INTERVAL_MS = 5000;

    // Diagnostic: track isInRing() transitions to expose ring re-formation timing.
    bool lastIsInRing = false;
};
