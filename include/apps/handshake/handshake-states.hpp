#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"

// State IDs for handshake states - must match values in QuickdrawStateId
constexpr int HANDSHAKE_INITIATE_STATE = 9;
constexpr int BOUNTY_SEND_CC_STATE = 10;
constexpr int HUNTER_SEND_ID_STATE = 11;

// Base class for all handshake states
class BaseHandshakeState : public State {
public:
    bool transitionToIdle() {
        return isTimedOut();
    }

protected:
    static SimpleTimer* handshakeTimeout;
    static bool timeoutInitialized;
    static const int timeout = 20000;

    explicit BaseHandshakeState(int stateId) : State(stateId) {}
    ~BaseHandshakeState() override = default;

    static void initTimeout() {
        if (!handshakeTimeout) handshakeTimeout = new SimpleTimer();
        handshakeTimeout->setTimer(timeout);
        timeoutInitialized = true;
    }

    static bool isTimedOut() {
        if (!timeoutInitialized || !handshakeTimeout) return false;
        handshakeTimeout->updateTime();
        return handshakeTimeout->expired();
    }

    static void resetTimeout() {
        timeoutInitialized = false;
        handshakeTimeout->invalidate();
    }
};

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
    const int delay = 100;
    bool transitionToConnectionSuccessfulState = false;
};

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
