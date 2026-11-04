#pragma once

#include "game/quickdraw-states.hpp"
#include "game/posse-manager.hpp"
#include "game/match-manager.hpp"
#include "game/shootout-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"

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
    /** Builds the shared managers, wires session-level callbacks, and installs the per-tick pump;
     * ~GameSession() clears every slot holding `this`. */
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
    /**
     * The mounted SupporterReady, or null when another state holds the device.
     * Read from the mounted app rather than published into the session: which
     * state is running is the app's fact, and a second copy of it can drift.
     */
    SupporterReady* getMountedSupporterReady();

    void logRetryStats();

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
