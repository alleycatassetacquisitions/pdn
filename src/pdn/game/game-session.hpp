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
 * getContext() hands those managers out by value, so an app built from one — and
 * any state of that app which keeps a manager — holds a pointer the session frees.
 * Destroy the apps before the session. No state destructor dereferences one today,
 * so that is ordering hygiene rather than a live use-after-free.
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
    GameContext getContext();

private:
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
