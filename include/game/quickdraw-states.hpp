#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "utils/debounced-condition.hpp"
#include "state/state.hpp"
#include "state/connect-state.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "game/quickdraw-resources.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <queue>
#include <string>
#include "device/remote-device-coordinator.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/shootout-manager.hpp"

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
    SUPPORTER_READY = 21,
    SHOOTOUT_PROPOSAL = 22,
    SHOOTOUT_BRACKET_REVEAL = 23,
    SHOOTOUT_SPECTATOR = 24,
    SHOOTOUT_ELIMINATED = 25,
    SHOOTOUT_FINAL_STANDINGS = 26,
    SHOOTOUT_ABORTED = 27,
    // Symbol-match IDs sit after the shootout block so neither protocol's
    // wire-visible state IDs collide. Main introduced SYMBOL=22/SYMBOL_MATCHED=23
    // in parallel with shootout taking 22-27; they were renumbered here on merge.
    SYMBOL = 28,
    SYMBOL_MATCHED = 29,
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
    Idle(Player *player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainDuelManager* chainDuelManager);
    ~Idle();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToDuelCountdown();
    bool transitionToSupporterReady();
    void renderStats(Device *PDN);
    bool transitionToSymbol();

private:
    Player *player;
    MatchManager* matchManager;
    ChainDuelManager* chainDuelManager;
    bool matchInitialized = false;
    bool displayIsDirty = false;
    int statsIndex = 0;
    int statsCount = 7;
    size_t lastPosseCount = 0;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    SimpleTimer matchInitializationTimer;
    const int MATCH_INITIALIZATION_TIMEOUT = 1000;

    bool transitionToSymbolState = false;

    // void serialEventCallbacks(const std::string& message);
};

class SupporterReady : public ConnectState {
public:
    SupporterReady(Player *player, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainDuelManager* chainDuelManager);
    ~SupporterReady();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToIdle();

    // Called by Quickdraw's chain-game-event packet handler.
    void onChainGameEventReceived(uint8_t event_type, const uint8_t* senderMac);

public:
    // Button callback in onStateMounted uses these via `this` capture.
    // Atomic because the ESP-NOW packet callback (WiFi task on hardware) and
    // the button press / state-loop callbacks (main task) read and write them
    // concurrently.
    Player *player;
    std::atomic<bool> buttonArmed{false};
    std::atomic<bool> hasConfirmed{false};
    std::atomic<bool> displayIsDirty{true};
    std::atomic<bool> ledsAreDirty{true};
    std::atomic<int> lastResult{0};
    // resultClearTimer is touched only by onStateLoop (main task). The
    // packet handler (WiFi task) communicates via the atomic lastResult;
    // onStateLoop detects transitions and manages the timer here.
    SimpleTimer resultClearTimer;
    Device *cachedPDN = nullptr;

    void startLEDs(Device *PDN, bool armed, bool confirmed);

private:
    ChainDuelManager* chainDuelManager;
    bool transitionToIdleFlag = false;
    int lastProcessedResult_ = 0;  // main-task-only; mirrors lastResult

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;
};



class DuelCountdown : public ConnectState {
public:
    DuelCountdown(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainDuelManager* chainDuelManager);
    ~DuelCountdown();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
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
    ChainDuelManager* chainDuelManager;
};

class ShootoutManager;

class Duel : public ConnectState {
public:
    Duel(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainDuelManager* chainDuelManager, ShootoutManager* shootoutManager);
    ~Duel();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToIdle();
    bool transitionToDuelPushed();
    bool transitionToDuelReceivedResult();
    bool transitionToShootoutSpectator();
    bool transitionToShootoutEliminated();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    Player* player;
    MatchManager* matchManager;
    ChainDuelManager* chainDuelManager;
    ShootoutManager* shootoutManager;
    parameterizedCallbackFunction buttonPress;
    bool transitionToDuelPushedState = false;
    bool transitionToDuelReceivedResultState = false;
    bool transitionToIdleState = false;
    bool transitionToShootoutSpectatorState = false;
    bool transitionToShootoutEliminatedState = false;
    SimpleTimer duelTimer;
    const int DUEL_TIMEOUT = 4000;
};

class DuelPushed : public ConnectState {
public:
    DuelPushed(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
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
    SimpleTimer gracePeriodTimer;
    const int DUEL_RESULT_GRACE_PERIOD = 900;
};

class DuelReceivedResult : public ConnectState {
public:
    DuelReceivedResult(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
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
};

class DuelResult : public State {
public:
    DuelResult(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager, ShootoutManager* shootoutManager);
    ~DuelResult();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToWin();
    bool transitionToLose();
    // Shootout takes priority over the regular Win/Lose paths when a tournament
    // is active; reportLocalWin fires once on the spectator transition so the
    // bracket advances exactly once per match.
    bool transitionToShootoutSpectator();
    bool transitionToShootoutEliminated();

private:
    Player* player;
    MatchManager* matchManager;
    QuickdrawWirelessManager* quickdrawWirelessManager;
    ShootoutManager* shootoutManager;
    bool wonBattle = false;
    bool captured = false;
};

class Win : public State {
public:
    Win(Player *player, ChainDuelManager* chainDuelManager, MatchManager* matchManager);
    ~Win();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool resetGame();
    bool isTerminalState() override;

private:
    SimpleTimer winTimer = SimpleTimer();
    Player *player;
    ChainDuelManager* chainDuelManager;
    MatchManager* matchManager;
    bool reset = false;
};

class Lose : public State {
public:
    Lose(Player *player, ChainDuelManager* chainDuelManager, MatchManager* matchManager);
    ~Lose();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool resetGame();
    bool isTerminalState() override;

private:
    SimpleTimer loseTimer = SimpleTimer();
    Player *player;
    ChainDuelManager* chainDuelManager;
    MatchManager* matchManager;
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

static constexpr unsigned long kLoopBreakDebounceMs = 500;

class ShootoutProposal : public State {
public:
    ShootoutProposal(ShootoutManager* shootout, ChainDuelManager* chainDuelManager);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool transitionToBracketReveal();
    bool transitionToIdle();
    bool transitionToAborted();

private:
    ShootoutManager* shootout_;
    ChainDuelManager* chainDuelManager_;
    bool shouldGoToReveal_ = false;
    bool shouldGoToIdle_ = false;
    bool shouldGoToAborted_ = false;
    DebouncedCondition loopBreakDebounce_;
};

class ShootoutBracketReveal : public State {
public:
    ShootoutBracketReveal(ShootoutManager* shootout, ChainDuelManager* chainDuelManager);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool transitionToDuelCountdown();
    bool transitionToSpectator();
    bool transitionToAborted();
    bool transitionToIdle();

private:
    ShootoutManager* shootout_;
    ChainDuelManager* chainDuelManager_;
    bool shouldGoToDuelCountdown_ = false;
    bool shouldGoToSpectator_ = false;
    bool shouldGoToAborted_ = false;
    bool shouldGoToIdle_ = false;
    DebouncedCondition loopBreakDebounce_;
};

class ShootoutSpectator : public State {
public:
    explicit ShootoutSpectator(ShootoutManager* shootout);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool transitionToDuelCountdown();
    bool transitionToFinalStandings();
    bool transitionToAborted();

private:
    ShootoutManager* shootout_;
    bool shouldGoToDuelCountdown_ = false;
    bool shouldGoToFinalStandings_ = false;
    bool shouldGoToAborted_ = false;
    std::array<uint8_t, 6> lastDisplayedA_{};
    std::array<uint8_t, 6> lastDisplayedB_{};
};

class ShootoutEliminated : public State {
public:
    explicit ShootoutEliminated(ShootoutManager* shootout);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool transitionToFinalStandings();
    bool transitionToAborted();

private:
    ShootoutManager* shootout_;
    bool shouldGoToFinalStandings_ = false;
    bool shouldGoToAborted_ = false;
};

class ShootoutFinalStandings : public State {
public:
    ShootoutFinalStandings(ShootoutManager* shootout, ChainDuelManager* chainDuelManager);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool isTerminalState() override;

    bool transitionToSleep();

private:
    ShootoutManager* shootout_;
    ChainDuelManager* chainDuelManager_;
    bool shouldGoToSleep_ = false;
};

class ShootoutAborted : public State {
public:
    explicit ShootoutAborted(ShootoutManager* shootout);
    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool transitionToIdle();

private:
    ShootoutManager* shootout_;
    SimpleTimer displayTimer_;
    bool shouldGoToIdle_ = false;
    static constexpr unsigned long ABORTED_DISPLAY_MS = 2000;
};

class SymbolState : public ConnectState {
public:
    SymbolState(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager);
    ~SymbolState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    bool transitionToIdle();
    bool transitionToSymbolMatched();

private:
    Player* player;
    MatchManager* matchManager;
    SymbolWirelessManager* symbolWirelessManager;
    Device* mountedPdn = nullptr;
    uint8_t* fdnMac = nullptr;
    /// PDN jack cabled to the FDN (OUTPUT = primary side toward FDN, INPUT = aux side toward FDN).
    SerialIdentifier pdnJackToFdn = SerialIdentifier::OUTPUT_JACK;
    SymbolId fdnSymbol;

    SimpleTimer renderTimer;
    const int RENDER_TIMEOUT = 500;
    SimpleTimer bufferTimer;
    const int BUFFER_TIMEOUT = 500;
    SimpleTimer hapticPulseTimer;
    const int HAPTIC_PULSE_DURATION = 100;

    bool toggleSymbol = true;
    bool symbolSent = false;
    bool hapticPulseActive = false;
    bool matchReady = false;
    bool transitionToIdleState = false;
    bool transitionToSymbolMatchedState = false;

    AnimationConfig cfg{};

    void renderSymbolScreen(Device *PDN);
    void advanceSymbolRender(Device* PDN);
    void sendSymbolToFDN();
    void onSymbolMatchCommandReceived(SymbolMatchCommand command);
};

class SymbolMatched : public ConnectState {
public:
    SymbolMatched(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager);
    ~SymbolMatched();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    bool transitionToSymbol();
    bool transitionToIdle();

private:
    Player* player;
    SymbolWirelessManager* symbolWirelessManager;

    bool transitionToSymbolState = false;
    bool transitionToIdleState = false;
    bool toggleBlink = true;
    bool holdSteadySymbol = false;

    SimpleTimer blinkTimer;
    const int BLINK_INTERVAL = 0.15 * 1000;
    SimpleTimer successTimer;
    const int SUCCESS_TIMEOUT = 3 * 1000;

    AnimationConfig cfg{};

    void renderSymbolScreen(Device *PDN);
    void onSymbolMatchCommandReceived(SymbolMatchCommand command);
};
