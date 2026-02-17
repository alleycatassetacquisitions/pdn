#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "game/match-manager.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"

/*
 * BaseHandshakeState - Base class for all handshake states
 * Provides shared timeout mechanism across handshake flow
 */
class BaseHandshakeState : public State {
public:
    bool transitionToIdle() {
        return isTimedOut();
    }

protected:
    static SimpleTimer handshakeTimeout;
    static bool timeoutInitialized;
    static const int timeout = 20000;

    explicit BaseHandshakeState(QuickdrawStateId stateId) : State(stateId) {}
    ~BaseHandshakeState() override = default;

    static void initTimeout() {
        handshakeTimeout.setTimer(timeout);
        timeoutInitialized = true;
    }

    static bool isTimedOut() {
        if (!timeoutInitialized) return false;
        handshakeTimeout.updateTime();
        return handshakeTimeout.expired();
    }

    static void resetTimeout() {
        timeoutInitialized = false;
        handshakeTimeout.invalidate();
    }
};

/*
 * HandshakeInitiateState - Initiates handshake between two players
 */
class HandshakeInitiateState : public BaseHandshakeState {
public:
    explicit HandshakeInitiateState(Player *player);
    ~HandshakeInitiateState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToBountySendCC();
    bool transitionToHunterSendId();

private:
    Player* player;
    SimpleTimer handshakeSettlingTimer;
    const int HANDSHAKE_SETTLE_TIME = 500;
    bool transitionToBountySendCCState = false;
    bool transitionToHunterSendIdState = false;
};

/*
 * BountySendConnectionConfirmedState - Bounty confirms connection
 */
class BountySendConnectionConfirmedState : public BaseHandshakeState {
public:
    BountySendConnectionConfirmedState(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager);
    ~BountySendConnectionConfirmedState();
    void onQuickdrawCommandReceived(QuickdrawCommand command);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToConnectionSuccessful();

private:
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    SimpleTimer delayTimer;
    SimpleTimer retryTimer;
    const int delay = 100;
    const int retryTimeout = 3000;
    const int maxRetries = 3;
    int retryCount = 0;
    bool waitingForHunterReady = false;
    bool transitionToConnectionSuccessfulState = false;
};

/*
 * HunterSendIdState - Hunter sends ID to bounty
 */
class HunterSendIdState : public BaseHandshakeState {
public:
    HunterSendIdState(Player *player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager);
    ~HunterSendIdState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void onQuickdrawCommandReceived(QuickdrawCommand command);
    bool transitionToConnectionSuccessful();

private:
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    SimpleTimer delayTimer;
    const int delay = 100;
    bool transitionToConnectionSuccessfulState = false;
};
