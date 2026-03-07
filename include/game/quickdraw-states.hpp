#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "state/connect-state.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "game/quickdraw-resources.hpp"
#include <cstdlib>
#include <queue>
#include <string>
#include "device/remote-device-coordinator.hpp"
#include "game/chain-context.hpp"

class SerialManager;

enum QuickdrawStateId {
    SLEEP = 6,
    AWAKEN_SEQUENCE = 7,
    IDLE = 8,
    DUEL_COUNTDOWN = 13,
    DUEL = 14,
    DUEL_PUSHED = 15,
    DUEL_RECEIVED_RESULT = 16,
    DUEL_RESULT = 17,
    WIN = 18,
    LOSE = 19,
    UPLOAD_MATCHES = 20,
    CHAIN_DETECTION = 21,
    SUPPORTER_READY = 22,
};

class Sleep : public State {
public:
    explicit Sleep(Player* player);
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
    explicit AwakenSequence(Player* player);
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

class Idle : public ConnectState {
public:
    Idle(Player *player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~Idle();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToDuelCountdown();
    void cycleStats(Device *PDN);

private:
    Player *player;
    MatchManager* matchManager;
    bool matchInitialized = false;
    bool displayIsDirty = false;
    int statsIndex = 0;
    int statsCount = 5;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    SimpleTimer matchInitializationTimer;
    const int MATCH_INITIALIZATION_TIMEOUT = 1000;

    // void serialEventCallbacks(const std::string& message);
};



class DuelCountdown : public ConnectState {
public:
    DuelCountdown(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainContext* chainContext = nullptr);
    ~DuelCountdown();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool shallWeBattle();
    bool disconnectedBackToIdle();
    bool countdownStarted();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

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
    ChainContext* chainContext_;
    bool countdownStarted_ = false;
    SerialManager* serialManager_ = nullptr;
};

class Duel : public ConnectState {
public:
    Duel(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainContext* chainContext = nullptr);
    ~Duel();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToIdle();
    bool transitionToDuelPushed();
    bool transitionToDuelReceivedResult();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    Player* player;
    MatchManager* matchManager;
    ChainContext* chainContext_;
    parameterizedCallbackFunction buttonPress;
    bool transitionToDuelPushedState = false;
    bool transitionToDuelReceivedResultState = false;
    bool transitionToIdleState = false;
    SimpleTimer duelTimer;
    const int DUEL_TIMEOUT = 4000;
    SerialManager* serialManager_ = nullptr;
};

class DuelPushed : public ConnectState {
public:
    DuelPushed(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainContext* chainContext = nullptr);
    ~DuelPushed();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToDuelResult();
    bool disconnectedBackToIdle();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    Player* player;
    MatchManager* matchManager;
    ChainContext* chainContext_;
    SimpleTimer gracePeriodTimer;
    const int DUEL_RESULT_GRACE_PERIOD = 900;
};

class DuelReceivedResult : public ConnectState {
public:
    DuelReceivedResult(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainContext* chainContext = nullptr);
    ~DuelReceivedResult();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToDuelResult();
    bool disconnectedBackToIdle();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SimpleTimer buttonPushGraceTimer;
    bool transitionToDuelResultState = false;
    const int BUTTON_PUSH_GRACE_PERIOD = 750;
    Player* player;
    MatchManager* matchManager;
    ChainContext* chainContext_;
};

class DuelResult : public State {
public:
    DuelResult(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager, ChainContext* chainContext = nullptr);
    ~DuelResult();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToWin();
    bool transitionToLose();

private:
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    ChainContext* chainContext_;
    bool wonBattle = false;
    bool captured = false;
};

class Win : public State {
public:
    explicit Win(Player *player);
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
    explicit Lose(Player *player);
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

class UploadMatchesState : public State {
public:
    UploadMatchesState(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager);
    ~UploadMatchesState();
    
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToSleep();
    void showLoadingGlyphs(Device *PDN);
    void attemptUpload();

private:
    Player* player;
    WirelessManager* wirelessManager;
    MatchManager* matchManager;
    SimpleTimer uploadMatchesTimer;
    int matchUploadRetryCount = 0;
    const int UPLOAD_MATCHES_TIMEOUT = 10000;
    std::string matchesJson;
    bool transitionToSleepState = false;
    bool shouldRetryUpload = false;
};

class ChainDetectionState : public State {
public:
    ChainDetectionState(ChainContext* context, RemoteDeviceCoordinator* rdc);
    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool detectionComplete();
    bool isSupporterRole();
    bool isChampionRole();
private:
    ChainContext* context_;
    RemoteDeviceCoordinator* rdc_;
};

class SupporterReadyState : public State {
public:
    SupporterReadyState(ChainContext* context, RemoteDeviceCoordinator* rdc);
    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToWin();
    bool transitionToLose();
    bool transitionToIdle();
    bool receivedCountdown();
    void sendInitialConfirm();
private:
    ChainContext* context_;
    RemoteDeviceCoordinator* rdc_;
    SerialManager* serialManager_ = nullptr;
    bool hasPressed_ = false;
    bool transitionToWin_ = false;
    bool transitionToLose_ = false;
    bool transitionToIdle_ = false;
    bool receivedCountdown_ = false;
};
