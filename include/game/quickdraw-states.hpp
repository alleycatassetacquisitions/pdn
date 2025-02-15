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
    HANDSHAKE = 3,
    CONNECTION_SUCCESSFUL = 4,
    DUEL_COUNTDOWN = 5,
    DUEL = 6,
    WIN = 7,
    LOSE = 8
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
    Idle(Player* player);

    ~Idle();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    void ledAnimation(Device *PDN);

    bool transitionToHandshake();

private:
    Player* player;
    bool transitionToHandshakeState = false;
    int ledChaseIndex = 2;
    const float smoothingPoints = 255;
    byte ledBrightness[3] = {160, 80, 0};
    float pwmValues[3] = {0, 0, 0};
    bool breatheUp = true;
    long idleLEDBreak = 5000;
    CRGBPalette16 currentPalette;
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
