#pragma once

#include "player.hpp"
#include "simple-timer.hpp"
#include "state.hpp"
#include <FastLED.h>
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/wireless-manager.hpp"
#include <queue>

using namespace std;

// Quickdraw States

enum QuickdrawStateId {
    PLAYER_REGISTRATION = 0,
    FETCH_USER_DATA = 1,
    CONFIRM_OFFLINE = 2,
    CHOOSE_ROLE = 3,
    ALLEGIANCE_PICKER = 4,
    WELCOME_MESSAGE = 5,
    SLEEP = 6,
    AWAKEN_SEQUENCE = 7,
    IDLE = 8,
    HANDSHAKE_INITIATE_STATE = 9,
    BOUNTY_SEND_CC_STATE = 10,
    HUNTER_SEND_ID_STATE = 11,
    CONNECTION_SUCCESSFUL = 12,
    DUEL_COUNTDOWN = 13,
    DUEL = 14,
    WIN = 15,
    LOSE = 16
};

class PlayerRegistration : public State {
public:
    PlayerRegistration(Player* player);
    ~PlayerRegistration();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToUserFetch();


private:
    bool transitionToUserFetchState = false;
    bool shouldRender = false;
    Player* player;
    int currentDigit = 0;
    int currentDigitIndex = 0;
    static constexpr int DIGIT_COUNT = 4;
    int inputId[DIGIT_COUNT] = {0, 0, 0, 0};
};

class FetchUserDataState : public State {
public:
    FetchUserDataState(Player* player, WirelessManager* wirelessManager);
    ~FetchUserDataState();

    bool transitionToConfirmOffline();
    bool transitionToWelcomeMessage();
    void showLoadingGlyphs(Device *PDN);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

private:
    bool transitionToConfirmOfflineState = false;
    bool transitionToWelcomeMessageState = false;
    WirelessManager* wirelessManager;
    bool isFetchingUserData = false;
    Player* player;
    SimpleTimer userDataFetchTimer;
    const int USER_DATA_FETCH_TIMEOUT = 10000;
};

class ConfirmOfflineState : public State {
public:
    ConfirmOfflineState(Player* player);
    ~ConfirmOfflineState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToChooseRole();
    bool transitionToPlayerRegistration();
    void renderUi(Device *PDN);
    int getDigitGlyphForIDIndex(int index);

private:
    Player* player;
    int uiPage = 0;
    const int UI_PAGE_COUNT = 3;
    const int UI_PAGE_TIMEOUT = 3000;
    bool finishedPaging = false;
    int menuIndex = 0;
    bool displayIsDirty = false;
    const int MENU_ITEM_COUNT = 2;
    SimpleTimer uiPageTimer;
    bool transitionToChooseRoleState = false;
    bool transitionToPlayerRegistrationState = false;
    parameterizedCallbackFunction confirmCallback;
    parameterizedCallbackFunction cancelCallback;

};

class ChooseRoleState : public State {
public:
    ChooseRoleState(Player* player);
    ~ChooseRoleState();
    
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToAllegiancePicker();
    void renderUi(Device *PDN);

private:
    Player* player;
    bool transitionToAllegiancePickerState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};

class AllegiancePickerState : public State {
public:
    AllegiancePickerState(Player* player);
    ~AllegiancePickerState();
    
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToWelcomeMessage();
    void renderUi(Device *PDN);

private:
    Player* player;
    bool transitionToWelcomeMessageState = false;
    bool displayIsDirty = false;
    int currentAllegiance = 0;
    const Allegiance allegianceArray[4] = {Allegiance::HELIX, Allegiance::ENDLINE, Allegiance::ALLEYCAT, Allegiance::RESISTANCE};
};

class WelcomeMessage : public State {
public:
    WelcomeMessage(Player* player);
    ~WelcomeMessage();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void renderWelcomeMessage();
    bool transitionToGameplay();

private:
    Player* player;
    SimpleTimer welcomeMessageTimer;
    bool transitionToAwakenSequenceState = false;
    const int WELCOME_MESSAGE_TIMEOUT = 5000;
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
    AwakenSequence(Player* player);
    ~AwakenSequence();
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
    Player* player;
};

class Idle : public State {
public:
    Idle(Player *player, WirelessManager* wirelessManager);

    ~Idle();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool transitionToHandshake();

private:
    Player *player;
    WirelessManager* wirelessManager;
    bool transitionToHandshakeState = false;
    bool sendMacAddress = false;
    bool waitingForMacAddress = false;

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
    Win(Player *player, WirelessManager* wirelessManager);

    ~Win();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool resetGame();

    bool isTerminalState() override;

private:
    SimpleTimer winTimer = SimpleTimer();
    WirelessManager* wirelessManager;
    Player *player;
    bool reset = false;
};

/*
 * TODO: Add Score change display.
 */
class Lose : public State {
public:
    Lose(Player *player, WirelessManager* wirelessManager);

    ~Lose();

    void onStateMounted(Device *PDN) override;

    void onStateLoop(Device *PDN) override;

    void onStateDismounted(Device *PDN) override;

    bool resetGame();

    bool isTerminalState() override;

private:
    SimpleTimer loseTimer = SimpleTimer();
    WirelessManager* wirelessManager;
    Player *player;
    bool reset = false;
};

// Base class for all handshake states
class BaseHandshakeState : public State {

public:
    // Common transition to idle if timeout occurs
    bool transitionToIdle() {
        return isTimedOut();
    }

protected:
    static SimpleTimer handshakeTimeout;
    static bool timeoutInitialized;
    static const int timeout = 20000; // 20 seconds timeout
    
    BaseHandshakeState(QuickdrawStateId stateId) : State(stateId) {
    }
    
    ~BaseHandshakeState() {
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
};

class HandshakeInitiateState : public BaseHandshakeState {
public:
    HandshakeInitiateState(Player *player);
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
    BountySendConnectionConfirmedState(Player* player);
    ~BountySendConnectionConfirmedState();
    void onQuickdrawCommandReceived(QuickdrawCommand command);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToConnectionSuccessful();

private:
    Player* player;
    SimpleTimer delayTimer;
    const int delay = 100;
    bool transitionToConnectionSuccessfulState = false;
};

class HunterSendIdState : public BaseHandshakeState {
public:
    HunterSendIdState(Player *player);
    ~HunterSendIdState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void onQuickdrawCommandReceived(QuickdrawCommand command);
    bool transitionToConnectionSuccessful();

private:
    Player* player;
    SimpleTimer delayTimer;
    const int delay = 100;
    bool transitionToConnectionSuccessfulState = false;
};