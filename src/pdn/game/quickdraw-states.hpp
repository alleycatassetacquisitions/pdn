#pragma once

#include "device/pdn.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "utils/debounced-condition.hpp"
#include "state/state.hpp"
#include "state/connect-state.hpp"
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
#include "game/posse-manager.hpp"
#include "game/shootout-manager.hpp"

/**
 * Bundle of the shared game-wide managers a state may need. Built by the
 * GameSession that owns them and handed to every state so a new manager is a
 * one-line addition here rather than a constructor change across every state.
 * All pointers are non-owning; states read only the fields they use.
 */
struct GameContext {
    Player* player = nullptr;
    MatchManager* matchManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    PosseManager* posseManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;
    SymbolWirelessManager* symbolWirelessManager = nullptr;
    WirelessManager* wirelessManager = nullptr;
};

enum QuickdrawStateId {
    SLEEP = 6,
    AWAKEN_SEQUENCE = 7,
    IDLE = 8,
    MATCH_COUNTDOWN = 13,
    MATCH = 14,
    MATCH_PUSHED = 15,
    MATCH_RECEIVED_RESULT = 16,
    MATCH_RESULT = 17,
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

class Sleep : public TypedState<PDN> {
public:
    explicit Sleep(const GameContext& ctx);
    ~Sleep();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToAwakenSequence();

private:
    bool transitionToAwakenSequenceState = false;
    SimpleTimer dormantTimer;
    Player* player;
    static constexpr unsigned long SLEEP_DURATION = 60000UL;
};

class AwakenSequence : public TypedState<PDN> {
public:
    explicit AwakenSequence(const GameContext& ctx);
    ~AwakenSequence() override;
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

class Idle : public ConnectState<PDN> {
public:
    explicit Idle(const GameContext& ctx);
    ~Idle();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once MatchManager has initialized a match and countdown can begin. */
    bool transitionToMatchCountdown();
    bool transitionToSupporterReady();
    void renderStats(PDN* pdn);
    bool transitionToSymbol();

private:
    Player *player;
    MatchManager* matchManager;
    PosseManager* posseManager;
    bool matchInitialized = false;
    bool displayIsDirty = false;
    int statsIndex = 0;
    int statsCount = 7;
    size_t lastPosseCount = 0;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    SimpleTimer matchInitializationTimer;
    const int MATCH_INITIALIZATION_TIMEOUT = 1000;

    // void serialEventCallbacks(const std::string& message);
};

class SupporterReady : public ConnectState<PDN> {
public:
    explicit SupporterReady(const GameContext& ctx);
    ~SupporterReady();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToIdle();

    // Called by GameSession's chain-game-event packet handler.
    void onChainGameEventReceived(uint8_t event_type, const uint8_t* senderMac);

public:
    // Button callback in onStateMounted uses these via `this` capture.
    Player *player;
    std::atomic<bool> buttonArmed{false};
    std::atomic<bool> hasConfirmed{false};
    std::atomic<bool> displayIsDirty{true};
    std::atomic<bool> ledsAreDirty{true};
    std::atomic<int> lastResult{0};
    SimpleTimer resultClearTimer;
    PDN* cachedPDN = nullptr;

    void startLEDs(PDN* pdn, bool armed, bool confirmed);

private:
    PosseManager* posseManager;
    bool transitionToIdleFlag = false;
    int lastProcessedResult_ = 0;  // main-task-only; mirrors lastResult

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;
};

/**
 * A match state returns to Idle on a persistent disconnect, but never while a
 * tournament is live — the shootout's own ring-break/ABORT teardown owns that
 * path. The debounce ages on wall clock even when unsampled, so a run started
 * before the tournament went live would fire the instant the shootout ends;
 * reset it while active so a fresh full window is always required afterward.
 */
inline bool matchReturnsToIdle(ConnectState<PDN>& matchState, ShootoutManager* shootout) {
    if (shootout && shootout->active()) {
        matchState.resetDisconnectDebounce();
        return false;
    }
    return matchState.isPersistentlyDisconnected();
}

class MatchCountdown : public ConnectState<PDN> {
public:
    /** Constructs the pre-match 3-2-1 countdown state. */
    explicit MatchCountdown(const GameContext& ctx);
    /** Destroys the countdown state. */
    ~MatchCountdown() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once the countdown has reached BATTLE. */
    bool shallWeBattle() const;

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
            this->animationConfig.loop = false;
            this->animationConfig.speed = 16;

            if(step == CountdownStep::THREE) {
                this->animationConfig.initialState = COUNTDOWN_THREE_STATE;
            } else if(step == CountdownStep::TWO) {
                this->animationConfig.initialState = COUNTDOWN_TWO_STATE;
            } else if(step == CountdownStep::ONE) {
                this->animationConfig.initialState = COUNTDOWN_ONE_STATE;
            } else if(step == CountdownStep::BATTLE) {
                this->animationConfig.initialState = COUNTDOWN_MATCH_STATE;
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
    PosseManager* posseManager;
};

class ShootoutManager;

class MatchState : public ConnectState<PDN> {
public:
    /** Constructs the active match draw-window state. */
    explicit MatchState(const GameContext& ctx);
    /** Destroys the active match state. */
    ~MatchState() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True when the match timed out without a button press in standalone play. */
    bool transitionToIdle() const;
    /** True when the local player pressed the draw button during the active window. */
    bool transitionToMatchPushed() const;
    /** True when the remote peer's match result arrived before a local button press. */
    bool transitionToMatchReceivedResult() const;
    /** True when the active shootout match forfeits in this device's favor on timeout. */
    bool transitionToShootoutSpectator() const;
    /** True when the active shootout match forfeits against this device on timeout. */
    bool transitionToShootoutEliminated() const;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    Player* player;
    MatchManager* matchManager;
    PosseManager* posseManager;
    ShootoutManager* shootoutManager;
    parameterizedCallbackFunction buttonPress;
    bool transitionToMatchPushedState = false;
    bool transitionToMatchReceivedResultState = false;
    bool transitionToIdleState = false;
    bool transitionToShootoutSpectatorState = false;
    bool transitionToShootoutEliminatedState = false;
    SimpleTimer matchTimer;
    static constexpr int MATCH_TIMEOUT = 4000;
};

class MatchPushed : public ConnectState<PDN> {
public:
    /** Constructs the post-press grace-period state. */
    explicit MatchPushed(const GameContext& ctx);
    /** Destroys the post-press grace-period state. */
    ~MatchPushed() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once both results are present or the post-press grace period expires. */
    bool transitionToMatchResult();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    Player* player;
    MatchManager* matchManager;
    SimpleTimer gracePeriodTimer;
    static constexpr int MATCH_RESULT_GRACE_PERIOD = 900;
};

class MatchReceivedResult : public ConnectState<PDN> {
public:
    /** Constructs the state waiting for a local counter-press after receiving peer result. */
    explicit MatchReceivedResult(const GameContext& ctx);
    /** Destroys the received-result grace-period state. */
    ~MatchReceivedResult() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once the counter-press arrives or the grace period expires. */
    bool transitionToMatchResult();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SimpleTimer buttonPushGraceTimer;
    bool transitionToMatchResultState = false;
    const int BUTTON_PUSH_GRACE_PERIOD = 750;
    Player* player;
    MatchManager* matchManager;
};

class MatchResult : public TypedState<PDN> {
public:
    /** Constructs the match outcome evaluation state. */
    explicit MatchResult(const GameContext& ctx);
    /** Destroys the match outcome evaluation state. */
    ~MatchResult() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToWin();
    bool transitionToLose();
    // Shootout takes priority over the regular Win/Lose paths when a tournament
    // is active. This predicate reports the win as a side effect, so it must stay
    // idempotent — reportLocalWin's own guard is what makes that true.
    bool transitionToShootoutSpectator();
    bool transitionToShootoutEliminated();

private:
    Player* player;
    MatchManager* matchManager;
    ShootoutManager* shootoutManager;
    bool wonBattle = false;
    bool captured = false;
};

class Win : public TypedState<PDN> {
public:
    explicit Win(const GameContext& ctx);
    ~Win();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool resetGame();
    bool isTerminalState() override;

private:
    SimpleTimer winTimer = SimpleTimer();
    Player *player;
    PosseManager* posseManager;
    MatchManager* matchManager;
    bool reset = false;
};

class Lose : public TypedState<PDN> {
public:
    explicit Lose(const GameContext& ctx);
    ~Lose();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool resetGame();
    bool isTerminalState() override;

private:
    SimpleTimer loseTimer = SimpleTimer();
    Player *player;
    PosseManager* posseManager;
    MatchManager* matchManager;
    bool reset = false;
};

class UploadMatchesState : public TypedState<PDN> {
public:
    explicit UploadMatchesState(const GameContext& ctx);
    ~UploadMatchesState();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToSleep();
    void showLoadingGlyphs(PDN* pdn);
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

class ShootoutProposal : public TypedState<PDN> {
public:
    explicit ShootoutProposal(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToBracketReveal();

private:
    ShootoutManager* shootoutManager = nullptr;

    bool shouldGoToReveal_ = false;
};

class ShootoutBracketReveal : public TypedState<PDN> {
public:
    explicit ShootoutBracketReveal(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    /** True when MATCH_START names this device as one of the two players. */
    bool transitionToMatchCountdown() const;
    bool transitionToSpectator();

private:
    ShootoutManager* shootoutManager = nullptr;

    bool shouldGoToMatchCountdown = false;
    bool shouldGoToSpectator_ = false;
};

class ShootoutSpectator : public TypedState<PDN> {
public:
    explicit ShootoutSpectator(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    /** True when the next MATCH_START names this device as one of the two players. */
    bool transitionToMatchCountdown() const;
    bool transitionToFinalStandings();

private:
    ShootoutManager* shootout;
    bool shouldGoToMatchCountdown = false;
    bool shouldGoToFinalStandings_ = false;
    std::array<uint8_t, 6> lastDisplayedA_{};
    std::array<uint8_t, 6> lastDisplayedB_{};
};

class ShootoutEliminated : public TypedState<PDN> {
public:
    explicit ShootoutEliminated(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToFinalStandings();

private:
    ShootoutManager* shootout_;
    bool shouldGoToFinalStandings_ = false;
};

class ShootoutFinalStandings : public TypedState<PDN> {
public:
    explicit ShootoutFinalStandings(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool isTerminalState() override;

    bool transitionToSleep();

private:
    ShootoutManager* shootout_;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    bool shouldGoToSleep_ = false;
};

class ShootoutAborted : public TypedState<PDN> {
public:
    explicit ShootoutAborted(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToIdle();

private:
    ShootoutManager* shootout_;
    SimpleTimer displayTimer_;
    bool shouldGoToIdle_ = false;
    static constexpr unsigned long ABORTED_DISPLAY_MS = 2000;
};

class SymbolState : public ConnectState<PDN> {
public:
    explicit SymbolState(const GameContext& ctx);
    ~SymbolState();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    bool transitionToIdle();
    bool transitionToSymbolMatched();

private:
    Player* player;
    MatchManager* matchManager;
    SymbolWirelessManager* symbolWirelessManager;
    PDN* mountedPdn = nullptr;
    uint8_t* fdnMac = nullptr;
    /** PDN jack cabled to the FDN (OUTPUT = primary side toward FDN, INPUT = aux side toward FDN). */
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

    void renderSymbolScreen(PDN* pdn);
    void advanceSymbolRender(PDN* pdn);
    void sendSymbolToFDN();
    void onSymbolMatchCommandReceived(SymbolMatchCommand command);
};

class SymbolMatched : public ConnectState<PDN> {
public:
    explicit SymbolMatched(const GameContext& ctx);
    ~SymbolMatched();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

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

    void renderSymbolScreen(PDN* pdn);
    void onSymbolMatchCommandReceived(SymbolMatchCommand command);
};
