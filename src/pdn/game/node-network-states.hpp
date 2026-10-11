#pragma once

#include "device/pdn.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "state/connect-state.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/node-match-manager.hpp"
#include "device/wireless-manager.hpp"
#include "device/drivers/light-interface.hpp"
#include <string>

namespace NodeNetworkStateId {
    constexpr int SLEEP = 40;
    constexpr int AWAKEN_SEQUENCE = 41;
    constexpr int IDLE = 42;
    constexpr int DUEL_COUNTDOWN = 43;
    constexpr int DUEL = 44;
    constexpr int DUEL_PUSHED = 45;
    constexpr int DUEL_RECEIVED_RESULT = 46;
    constexpr int DUEL_RESULT = 47;
    constexpr int WIN = 48;
    constexpr int LINK_RETRY = 49;
    constexpr int UPLOAD_MATCHES = 50;
}

class NodeSleep : public TypedState<PDN> {
public:
    explicit NodeSleep(Player* player);
    ~NodeSleep() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToAwakenSequence();

private:
    bool transitionToAwakenSequenceState = false;
    SimpleTimer dormantTimer;
    Player* player = nullptr;
    bool breatheUp = true;
    int ledBrightness = 0;
    float pwm_val = 0.0f;
    static constexpr unsigned long SLEEP_DURATION = 60000UL;
};

class NodeAwakenSequence : public TypedState<PDN> {
public:
    explicit NodeAwakenSequence(Player* player);
    ~NodeAwakenSequence() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToIdle();

private:
    static constexpr int AWAKEN_THRESHOLD = 20;
    SimpleTimer activationSequenceTimer;
    int activateMotorCount = 0;
    bool activateMotor = false;
    const int activationStepDuration = 100;
    Player* player = nullptr;
};

class NodeIdle : public ConnectState<PDN> {
public:
    NodeIdle(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeIdle() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToDuelCountdown();
    void renderStats(PDN* pdn);

private:
    Player* player = nullptr;
    NodeMatchManager* matchManager = nullptr;
    bool matchInitialized = false;
    bool displayIsDirty = false;
    int statsIndex = 0;
    int statsCount = 4;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    SimpleTimer matchInitializationTimer;
    static constexpr int MATCH_INITIALIZATION_TIMEOUT = 1000;
};

class NodeDuelCountdown : public ConnectState<PDN> {
public:
    NodeDuelCountdown(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeDuelCountdown() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool shallWeBattle();
    bool disconnectedBackToIdle();

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
        CountdownStage(CountdownStep step, unsigned long countdownTimerMs);
        CountdownStep step;
        unsigned long countdownTimer = 0;
        AnimationConfig animationConfig;
    };

    ImageType getImageIdForStep(CountdownStep step);

    Player* player = nullptr;
    NodeMatchManager* matchManager = nullptr;
    SimpleTimer countdownTimer;
    SimpleTimer hapticTimer;
    static constexpr int HAPTIC_DURATION = 75;
    static constexpr int HAPTIC_INTENSITY = 255;
    bool doBattle = false;
    int currentStepIndex = 0;
};

class NodeDuel : public ConnectState<PDN> {
public:
    NodeDuel(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeDuel() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToIdle();
    bool transitionToDuelPushed();
    bool transitionToDuelReceivedResult();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    Player* player = nullptr;
    NodeMatchManager* matchManager = nullptr;
    bool transitionToDuelPushedState = false;
    bool transitionToDuelReceivedResultState = false;
    bool transitionToIdleState = false;
    SimpleTimer duelTimer;
    static constexpr int DUEL_TIMEOUT = 4000;
};

class NodeDuelPushed : public ConnectState<PDN> {
public:
    NodeDuelPushed(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeDuelPushed() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToDuelResult();
    bool disconnectedBackToIdle();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    Player* player = nullptr;
    NodeMatchManager* matchManager = nullptr;
    SimpleTimer gracePeriodTimer;
    static constexpr int DUEL_RESULT_GRACE_PERIOD = 900;
};

class NodeDuelReceivedResult : public ConnectState<PDN> {
public:
    NodeDuelReceivedResult(Player* player, NodeMatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeDuelReceivedResult() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToDuelResult();
    bool disconnectedBackToIdle();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SimpleTimer buttonPushGraceTimer;
    bool transitionToDuelResultState = false;
    static constexpr int BUTTON_PUSH_GRACE_PERIOD = 750;
    Player* player = nullptr;
    NodeMatchManager* matchManager = nullptr;
};

class NodeDuelResult : public TypedState<PDN> {
public:
    NodeDuelResult(Player* player, NodeMatchManager* matchManager);
    ~NodeDuelResult() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToWin();
    bool transitionToLinkRetry();

private:
    Player* player = nullptr;
    NodeMatchManager* matchManager = nullptr;
    bool syncSuccess = false;
    bool linkFailed = false;
};

class NodeWin : public TypedState<PDN> {
public:
    NodeWin(Player* player, NodeMatchManager* matchManager);
    ~NodeWin() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool resetGame();
    bool isTerminalState() override;

private:
    Player* player = nullptr;
    NodeMatchManager* matchManager = nullptr;
    SimpleTimer winTimer;
    bool reset = false;
};

class NodeLinkRetry : public TypedState<PDN> {
public:
    NodeLinkRetry(Player* player, NodeMatchManager* matchManager);
    ~NodeLinkRetry() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToIdle();

private:
    Player* player = nullptr;
    NodeMatchManager* matchManager = nullptr;
    SimpleTimer retryTimer;
    bool goIdle = false;
};

class NodeUploadMatches : public TypedState<PDN> {
public:
    NodeUploadMatches(Player* player, WirelessManager* wirelessManager, NodeMatchManager* matchManager);
    ~NodeUploadMatches() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToSleep();
    void attemptUpload();
    void showLoadingGlyphs(PDN* pdn);

private:
    Player* player = nullptr;
    WirelessManager* wirelessManager = nullptr;
    NodeMatchManager* matchManager = nullptr;
    SimpleTimer uploadMatchesTimer;
    bool transitionToSleepState = false;
    bool shouldRetryUpload = false;
    int matchUploadRetryCount = 0;
    std::string matchesJson;
    static constexpr int UPLOAD_MATCHES_TIMEOUT = 10000;
};
