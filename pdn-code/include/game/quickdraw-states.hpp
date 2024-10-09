#pragma once

#include "handshake-machine.hpp"
#include "../player.hpp"
#include "simple-timer.hpp"
#include "../state-machine/state.hpp"
#include "../comms_constants.hpp"


// Quickdraw States

enum QuickdrawStateId {
    SLEEP = 0,
    AWAKEN_SEQUENCE = 1,
    IDLE = 2,
    HANDSHAKE = 3,
    CONNECTION_SUCCESSFUL = 4,
    DUEL_COUNTDOWN = 5,
    DUEL = 6,
    WIN = 7,
    LOSE = 8
};


class Sleep : public State {
public:
    Sleep(bool isHunter, long debugDelay);

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToAwakenSequence();

private:
    SimpleTimer dormantTimer;

    bool isHunter = false;
    unsigned long defaultDelay = 5000;
    unsigned long bountyDelay[2] = {300000, 900000};
    unsigned long overchargeDelay[2] = {180000, 300000};
    unsigned long debugDelay = 3000;
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
    SimpleTimer activationSequenceTimer;
    byte activateMotorCount = 0;
    bool activateMotor = true;
    const int activationStepDuration = 100;
};

class Idle : public State {
public:
    Idle(bool isHunter);

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    void ledAnimation(Device *PDN);

    bool transitionToHandshake();

private:
    bool transitionToHandshakeState = false;
    const float smoothingPoints = 255;
    byte ledBrightness = 65;
    float pwm_val = 0;
    bool breatheUp = true;
    long idleLEDBreak = 5000;
    CRGBPalette16 currentPalette;
    bool isHunter = false;
};

class Handshake : public State {
public:
    Handshake(Player *player);

    ~Handshake();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToIdle();

    bool transitionToConnectionSuccessful();

private:
    Player *player;
    SimpleTimer handshakeTimeout;
    long timeout = 5000;
    HandshakeStateMachine *stateMachine;
    bool resetToActivated = false;
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
    DuelCountdown();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool shallWeBattle();

private:
    SimpleTimer countdownTimer;
    bool doBattle = false;
    const byte COUNTDOWN_STAGES = 4;
    byte countdownStage = COUNTDOWN_STAGES;
    int FOUR = 2000;
    int THREE = 2000;
    int TWO = 1000;
    int ONE = 3000;
};

/*
 * TODO: Add logic for spending LED here.
 */
class Duel : public State {
public:
    Duel();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToActivated();

    bool transitionToWin();

    bool transitionToLose();

    static void ButtonPress(Device *PDN) {
        PDN->writeString(&ZAP);
    }

private:
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
    Player *player;
    bool reset = false;
};
