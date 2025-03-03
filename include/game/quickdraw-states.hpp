#pragma once

#include "handshake-machine.hpp"
#include "player.hpp"
#include "simple-timer.hpp"
#include "state.hpp"
#include <FastLED.h>
#include <queue>

using namespace std;

// Quickdraw States

enum QuickdrawStateId {
    SLEEP = 0,
    AWAKEN_SEQUENCE = 1,
    IDLE = 2,
    HANDSHAKE_INITIATE = 3,
    BOUNTY_SEND_CC = 4,
    HUNTER_SEND_ID = 5,
    BOUNTY_SEND_ACK = 6,
    HUNTER_SEND_ACK = 7,
    HANDSHAKE_STARTING_LINE = 8,
    CONNECTION_SUCCESSFUL = 9,
    DUEL_COUNTDOWN = 10,
    DUEL = 11,
    WIN = 12,
    LOSE = 13
};


class Sleep : public State {
public:
    Sleep(Player* player);

    ~Sleep();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToAwakenSequence();

private:
    SimpleTimer dormantTimer;
    Player* player;

    bool breatheUp = true;
    int ledBrightness = 0;
    float pwm_val = 0.0;
    static constexpr int smoothingPoints = 255;

    static constexpr unsigned long defaultDelay = 2500;
    static constexpr unsigned long bountyDelay[2] = {300000, 900000};
    static constexpr unsigned long overchargeDelay[2] = {180000, 300000};
};

/*
 * TODO: Could this become a more generic alarm state?
 */
class AwakenSequence : public State {
public:
    AwakenSequence();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToIdle();

private:
    static constexpr int AWAKEN_THRESHOLD = 20;
    SimpleTimer activationSequenceTimer;
    int activateMotorCount = 0;
    bool activateMotor = false;
    const int activationStepDuration = 100;
};

class Idle : public State {
public:
    Idle(Player *player);

    ~Idle();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToHandshake();

private:
    Player *player;
    bool transitionToHandshakeState = false;
    bool sendMacAddress = false;
    bool waitingForMacAddress = false;
    uint8_t ledBrightness = 0;
    bool breatheUp = true;
    CRGBPalette16 currentPalette;

    void serialEventCallbacks(string message);

    void ledAnimation(Device *PDN);
};

/*
 * TODO: Lockdown gets cleared in this state.
 */
class ConnectionSuccessful : public State {
public:
    ConnectionSuccessful(Player *player);

    ~ConnectionSuccessful();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToCountdown();

private:
    Player *player;
    bool lightsOn = true;
    int flashDelay = 400;
    byte transitionThreshold = 12;
    byte alertCount = 0;
};

/*
 * TODO: User Powerup prompt.
 */
class DuelCountdown : public State {
public:
    DuelCountdown(Player* player);

    ~DuelCountdown();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool shallWeBattle();

private:

    enum class CountdownStep {
        THREE = 3,
        TWO = 2,
        ONE = 1,
        BATTLE = 0
    };

    struct CountdownStage {
        CountdownStage(CountdownStep step, unsigned long countdownTimer, byte ledBrightness) {
            this->step = step;
            this->countdownTimer = countdownTimer;
            this->ledBrightness = ledBrightness;
        }

        CountdownStep step;
        unsigned long countdownTimer = 0;
        byte ledBrightness = 0;
    };

    ImageType getImageIdForStep(CountdownStep step);

    Player* player;
    SimpleTimer countdownTimer;
    bool doBattle = false;
    const CountdownStage THREE = CountdownStage(CountdownStep::THREE, 2000, 255);
    const CountdownStage TWO = CountdownStage(CountdownStep::TWO, 2000, 155);
    const CountdownStage ONE = CountdownStage(CountdownStep::ONE, 3000, 75);
    const CountdownStage BATTLE = CountdownStage(CountdownStep::BATTLE, 0, 0);
    const CountdownStage countdownQueue[4] = {THREE, TWO, ONE, BATTLE};
    int currentStepIndex = 0;
};

/*
 * TODO: Add logic for spending LED here.
 */
class Duel : public State {
public:
    Duel(Player* player);

    ~Duel();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToActivated();

    bool transitionToWin();

    bool transitionToLose();

private:
    Player* player;
    SimpleTimer duelTimer;
    bool captured = false;
    bool wonBattle = false;
    const int DUEL_TIMEOUT = 4000;
};


/*
 * TODO: Implement Score update here.
 * TODO: Allow for score multipliers here.
 * TODO: Add Score change display.
 */
class Win : public State {
public:
    Win(Player *player);

    ~Win();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool resetGame();

    bool isTerminalState() override;

private:
    SimpleTimer winTimer = SimpleTimer();
    Player *player;
    bool reset = false;
};

/*
 * TODO: Add Score change display.
 */
class Lose : public State {
public:
    Lose(Player *player);

    ~Lose();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool resetGame();

    bool isTerminalState() override;

private:
    SimpleTimer loseTimer = SimpleTimer();
    Player *player;
    bool reset = false;
};

// Base class for all handshake states
class BaseHandshakeState : public State {
protected:
    Player *player;
    static SimpleTimer handshakeTimeout;
    static bool timeoutInitialized;
    static const int timeout = 20000; // 20 seconds timeout
    
    BaseHandshakeState(QuickdrawStateId stateId, Player *player) : State(stateId) {
        this->player = player;
        if (!timeoutInitialized) {
            handshakeTimeout.setTimer(timeout);
            timeoutInitialized = true;
        }
    }
    
    ~BaseHandshakeState() {
        player = nullptr;
    }
    
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
    }
    
    // Common transition to idle if timeout occurs
    bool transitionToIdle() {
        return isTimedOut();
    }
};

class HandshakeInitiate : public BaseHandshakeState {
public:
    HandshakeInitiate(Player *player) : BaseHandshakeState(HANDSHAKE_INITIATE, player) {}

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToBountySendCC();

    bool transitionToHunterSendId();

private:
    SimpleTimer handshakeSettlingTimer;
    const int HANDSHAKE_SETTLE_TIME = 500;
    bool transitionToBountySendCCState = false;
    bool transitionToHunterSendIdState = false;
};

class BountySendCC : public BaseHandshakeState {
public:
    BountySendCC(Player *player) : BaseHandshakeState(BOUNTY_SEND_CC, player) {}

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToBountySendAck();

private:
    SimpleTimer delayTimer;
    const int delay = 100;
    bool transitionToBountySendAckState = false;
};

class BountySendAck : public BaseHandshakeState {
public:
    BountySendAck(Player *player) : BaseHandshakeState(BOUNTY_SEND_ACK, player) {}

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;
    
    void onQuickdrawCommandReceived(QuickdrawCommand command);

    bool transitionToHandshakeStartingLine();

private:
    SimpleTimer delayTimer;
    const int delay = 100;
    bool transitionToHandshakeStartingLineState = false;
};

class HunterSendId : public BaseHandshakeState {
public:
    HunterSendId(Player *player) : BaseHandshakeState(HUNTER_SEND_ID, player) {}

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;
    
    void onQuickdrawCommandReceived(QuickdrawCommand command);

    bool transitionToHunterSendAck();

private:
    SimpleTimer delayTimer;
    const int delay = 100;
    bool transitionToHunterSendAckState = false;
};

class HunterSendAck : public BaseHandshakeState {
public:
    HunterSendAck(Player *player) : BaseHandshakeState(HUNTER_SEND_ACK, player) {}

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;
    
    void onQuickdrawCommandReceived(QuickdrawCommand command);

    bool transitionToHandshakeStartingLine();

private:
    SimpleTimer delayTimer;
    const int delay = 100;
    bool transitionToHandshakeStartingLineState = false;
};

class HandshakeStartingLine : public BaseHandshakeState {
public:
    HandshakeStartingLine(Player *player) : BaseHandshakeState(HANDSHAKE_STARTING_LINE, player) {}

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToConnectionSuccessful();

private:
    SimpleTimer delayTimer;
    const int delay = 500;
    bool transitionToConnectionSuccessfulState = false;
};
