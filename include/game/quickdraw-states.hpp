#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "game/quickdraw-resources.hpp"
#include <cstdlib>
#include <queue>
#include <string>

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
    DUEL_PUSHED = 15,
    DUEL_RECEIVED_RESULT = 16,
    DUEL_RESULT = 17,
    WIN = 18,
    LOSE = 19,
    UPLOAD_MATCHES = 20
};

class PlayerRegistration : public State {
public:
    PlayerRegistration(Player* player, HttpClientInterface* httpClient, MatchManager* matchManager);
    ~PlayerRegistration();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToUserFetch();

private:
    bool transitionToUserFetchState = false;
    bool shouldRender = false;
    Player* player;
    HttpClientInterface* httpClient;
    MatchManager* matchManager;
    int currentDigit = 0;
    int currentDigitIndex = 0;
    static constexpr int DIGIT_COUNT = 4;
    int inputId[DIGIT_COUNT] = {0, 0, 0, 0};
};

class FetchUserDataState : public State {
public:
    FetchUserDataState(Player* player, HttpClientInterface* httpClient);
    ~FetchUserDataState();

    bool transitionToConfirmOffline();
    bool transitionToWelcomeMessage();
    bool transitionToUploadMatches();
    bool transitionToPlayerRegistration();
    void showLoadingGlyphs(Device *PDN);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    
private:
    bool transitionToPlayerRegistrationState = false;
    bool transitionToConfirmOfflineState = false;
    bool transitionToWelcomeMessageState = false;
    bool transitionToUploadMatchesState = false;
    HttpClientInterface* httpClient;
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
    const Allegiance allegianceArray[4] = {Allegiance::ALLEYCAT, Allegiance::ENDLINE, Allegiance::HELIX, Allegiance::RESISTANCE};
};

class WelcomeMessage : public State {
public:
    WelcomeMessage(Player* player);
    ~WelcomeMessage();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void renderWelcomeMessage(Device *PDN);
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
    bool transitionToAwakenSequenceState = false;
    SimpleTimer dormantTimer;
    Player* player;
    bool breatheUp = true;
    int ledBrightness = 0;
    float pwm_val = 0.0;
    static constexpr int smoothingPoints = 255;
    static constexpr unsigned long SLEEP_DURATION = 60000UL;
};

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
    Idle(Player *player);
    ~Idle();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToHandshake();
    void cycleStats(Device *PDN);

private:
    Player *player;
    bool transitionToHandshakeState = false;
    bool sendMacAddress = false;
    bool waitingForMacAddress = false;
    bool displayIsDirty = false;
    int statsIndex = 0;
    int statsCount = 5;

    void serialEventCallbacks(std::string message);
    void ledAnimation(Device *PDN);
};

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
    uint8_t transitionThreshold = 12;
    uint8_t alertCount = 0;
};

class DuelCountdown : public State {
public:
    DuelCountdown(Player* player, MatchManager* matchManager);
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
        CountdownStage(CountdownStep step, unsigned long countdownTimer) {
            this->step = step;
            this->countdownTimer = countdownTimer;
            this->animationConfig.type = AnimationType::COUNTDOWN;
            this->animationConfig.loop = false;
            this->animationConfig.speed = 16;

            if(step == CountdownStep::THREE) {
                this->animationConfig.initialState = COUNTDOWN_THREE_STATE;
            } else if(step == CountdownStep::TWO) {
                this->animationConfig.initialState = COUNTDOWN_TWO_STATE;
            } else if(step == CountdownStep::ONE) {
                this->animationConfig.initialState = COUNTDOWN_ONE_STATE;
            } else if(step == CountdownStep::BATTLE) {
                this->animationConfig.initialState = COUNTDOWN_DUEL_STATE;
            }
        }

        CountdownStep step;
        unsigned long countdownTimer = 0;
        AnimationConfig animationConfig;
    };

    ImageType getImageIdForStep(CountdownStep step);

    Player* player;
    SimpleTimer countdownTimer;
    SimpleTimer hapticTimer;
    const int HAPTIC_DURATION = 75;
    const int HAPTIC_INTENSITY = 255;
    bool doBattle = false;
    const CountdownStage THREE = CountdownStage(CountdownStep::THREE, 2000);
    const CountdownStage TWO = CountdownStage(CountdownStep::TWO, 2000);
    const CountdownStage ONE = CountdownStage(CountdownStep::ONE, 2000);
    const CountdownStage BATTLE = CountdownStage(CountdownStep::BATTLE, 0);
    const CountdownStage countdownQueue[4] = {THREE, TWO, ONE, BATTLE};
    int currentStepIndex = 0;
    MatchManager* matchManager;
};

class Duel : public State {
public:
    Duel(Player* player, MatchManager* matchManager);
    ~Duel();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void onQuickdrawCommandReceived(QuickdrawCommand command);
    bool transitionToIdle();
    bool transitionToDuelPushed();
    bool transitionToDuelReceivedResult();

private:
    Player* player;
    MatchManager* matchManager;
    parameterizedCallbackFunction buttonPress;
    bool transitionToDuelPushedState = false;
    bool transitionToIdleState = false;
    bool transitionToDuelReceivedResultState = false;
    SimpleTimer duelTimer;
    const int DUEL_TIMEOUT = 4000;
};

class DuelPushed : public State {
public:
    DuelPushed(Player* player, MatchManager* matchManager);
    ~DuelPushed();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void onQuickdrawCommandReceived(QuickdrawCommand command);
    bool transitionToDuelResult();

private:
    Player* player;
    MatchManager* matchManager;
    SimpleTimer gracePeriodTimer;
    const int DUEL_RESULT_GRACE_PERIOD = 900;
};

class DuelReceivedResult : public State {
public:
    DuelReceivedResult(Player* player, MatchManager* matchManager);
    ~DuelReceivedResult();

    void onStateMounted(Device *PDN) override;  
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;   
    bool transitionToDuelResult();

private:
    SimpleTimer buttonPushGraceTimer;
    bool transitionToDuelResultState = false;
    const int BUTTON_PUSH_GRACE_PERIOD = 750;
    Player* player;
    MatchManager* matchManager;
};

class DuelResult : public State {
public:
    DuelResult(Player* player, MatchManager* matchManager);
    ~DuelResult();

    void onStateMounted(Device *PDN) override;  
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;   
    bool transitionToWin();
    bool transitionToLose();    
    
private:
    Player* player;
    MatchManager* matchManager;
    bool wonBattle = false;
    bool captured = false;
};

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
public:
    bool transitionToIdle() {
        return isTimedOut();
    }

protected:
    static SimpleTimer* handshakeTimeout;
    static bool timeoutInitialized;
    static const int timeout = 20000;
    
    BaseHandshakeState(QuickdrawStateId stateId) : State(stateId) {}
    ~BaseHandshakeState() {}
    
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

class UploadMatchesState : public State {
public:
    UploadMatchesState(Player* player, HttpClientInterface* httpClient, MatchManager* matchManager);
    ~UploadMatchesState();
    
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToSleep();
    void showLoadingGlyphs(Device *PDN);
    bool transitionToPlayerRegistration();
    void routeToNextState();
    void attemptUpload();

private:
    Player* player;
    HttpClientInterface* httpClient;
    MatchManager* matchManager;
    SimpleTimer uploadMatchesTimer;
    int matchUploadRetryCount = 0;
    const int UPLOAD_MATCHES_TIMEOUT = 10000;
    std::string matchesJson;
    bool transitionToSleepState = false;
    bool transitionToPlayerRegistrationState = false;
    bool shouldRetryUpload = false;
};
