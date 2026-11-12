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
 * Owns the managers the gameplay apps share, above every app so none is tied to
 * one state machine.
 *
 * getContext() exposes those managers as raw pointers, so an app built from it —
 * and any state of that app which keeps a manager — holds a pointer the session
 * frees. Destroy the apps before the session: ~SupporterReady dereferences
 * posseManager when destroyed mounted, so the order is load-bearing.
 *
 * sync() runs on each platform tick through Device::setTickCallback, not from a
 * state: only the mounted app receives an onStateLoop, and a tournament has to
 * keep advancing while the device is in the match app for a bracket match.
 */
class GameSession {
public:
    /**
     * Builds the shared managers, wires the session-level callbacks, and
     * installs its own per-tick pump. The device outlives the session, so the
     * destructor empties every slot holding `this`.
     */
    GameSession(Player* player,
                Device* pdn,
                SymbolWirelessManager* symbolWirelessManager);
    /** Drops the callbacks that capture `this`, then frees the managers it owns. */
    ~GameSession();

    /** Drives the managers' per-tick logic and the periodic retry stats line. */
    void sync();

    /** The manager bundle every gameplay state is constructed from. */
    const GameContext& getContext() const { return context; }

private:
    void logRetryStats();

    Device* pdn = nullptr;
    WirelessManager* wirelessManager = nullptr;
    // Owns the match, posse and shootout managers in it; the other pointers are
    // borrowed.
    GameContext context;

    SimpleTimer statsLogTimer;
    static constexpr unsigned long STATS_LOG_INTERVAL_MS = 5000;

    // Diagnostic: track isInRing() transitions to expose ring re-formation timing.
    bool lastIsInRing = false;
};
