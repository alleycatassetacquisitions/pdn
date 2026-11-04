#pragma once

#include "game/posse-manager.hpp"
#include "game/match-manager.hpp"
#include "game/shootout-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include <array>

/**
 * Bundle of the shared game-wide managers a state may need. Built by the
 * GameSession that owns them and handed to every state so a new manager is a
 * one-line addition here rather than a constructor change across every state.
 * All pointers are non-owning; states read only the fields they use.
 */
struct GameContext {
    Player* player = nullptr;
    MatchManager* matchManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    PosseManager* posseManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;
    SymbolWirelessManager* symbolWirelessManager = nullptr;
    WirelessManager* wirelessManager = nullptr;
};

/**
 * Owns the managers the gameplay apps share and the wireless plumbing that
 * feeds them, and sits above every app so neither is tied to one state machine.
 *
 * getContext() hands those managers out by value, so an app built from one — and
 * any state of that app which keeps a manager — holds a pointer the session frees.
 * Destroy the apps before the session. No state destructor dereferences one today,
 * so that is ordering hygiene rather than a live use-after-free.
 *
 * sync() must run on each platform tick, not from a state: only the mounted app
 * receives an onStateLoop, so a retry machine driven from inside a state stalls
 * the moment the device swaps apps — silently, because nothing on the wire says
 * a retransmit was skipped. Device::setTickCallback is the seam that runs it.
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

    /** The manager bundle every gameplay state is constructed from. */
    GameContext getContext();

private:
    void onChainGameEventPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onChainGameEventAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onShootoutCommandPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void onShootoutCommandAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);
    void logRetryStats();

    /**
     * One row per packet type this session answers for. The constructor
     * registers every row and the destructor clears every row, so the two lists
     * cannot drift apart the way two hand-written ones can.
     */
    struct PacketRoute {
        PktType type;
        PeerCommsInterface::PacketCallback handler;
    };
    static const std::array<PacketRoute, 4>& packetRoutes();

    /**
     * Wraps a member handler in the C-style callback the radio takes. The member
     * is a template parameter, so the pointer-to-member call resolves at compile
     * time; the row still stores a std::function, as the lambdas it replaced did.
     */
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

    SimpleTimer statsLogTimer;
    static constexpr unsigned long STATS_LOG_INTERVAL_MS = 5000;

    // Diagnostic: track isInRing() transitions to expose ring re-formation timing.
    bool lastIsInRing = false;
};
