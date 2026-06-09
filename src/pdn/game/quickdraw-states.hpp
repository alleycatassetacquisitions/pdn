#pragma once

#include "device/pdn.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "utils/debounced-condition.hpp"
#include "state/state.hpp"
#include "state/connect-state.hpp"
#include "wireless/resender.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include "game/quickdraw-resources.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <string>
#include "device/remote-device-coordinator.hpp"
#include "game/chain-manager.hpp"
#include "game/shootout-manager.hpp"

// Duel grace windows are coupled, not independent. A presser in DuelPushed must
// outlast the opponent's worst-case NEVER_PRESSED: the opponent's full
// button-push grace + reliable retransmit budget + margin. Below that our grace
// expires before their NEVER_PRESSED lands and the duel voids even when their
// outcome (didn't press -> loses) is unambiguous. Deriving one from the other
// keeps them from drifting apart when either is tuned.
namespace quickdraw_timing {
    constexpr int BUTTON_PUSH_GRACE_PERIOD_MS = 750;
    constexpr int RELIABLE_RETRY_BUDGET_MS =
        static_cast<int>(Resender::RetryPolicy{}.totalBudgetMs());
    constexpr int DUEL_RESULT_GRACE_MARGIN_MS = 250;
    constexpr int DUEL_RESULT_GRACE_PERIOD_MS =
        BUTTON_PUSH_GRACE_PERIOD_MS + RELIABLE_RETRY_BUDGET_MS + DUEL_RESULT_GRACE_MARGIN_MS;
}

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
    SYMBOL = 28,
    SYMBOL_MATCHED = 29,
};

class Sleep : public TypedState<PDN> {
public:
    explicit Sleep(Player* player);
    ~Sleep();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
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

class AwakenSequence : public TypedState<PDN> {
public:
    explicit AwakenSequence(Player* player);
    ~AwakenSequence();
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
    Player* player;
};

class Idle : public ConnectState<PDN> {
public:
    Idle(Player *player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainManager* chainManager);
    ~Idle();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToDuelCountdown();
    bool transitionToSupporterReady();
    void renderStats(PDN* pdn);
    bool transitionToSymbol();

private:
    Player *player;
    MatchManager* matchManager;
    ChainManager* chainManager;
    bool displayIsDirty = false;
    int statsIndex = 0;
    int statsCount = 7;
    size_t lastPosseCount = 0;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    bool transitionToSymbolState = false;

    // void serialEventCallbacks(const std::string& message);
};

class SupporterReady : public ConnectState<PDN> {
public:
    SupporterReady(Player *player, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainManager* chainManager);
    ~SupporterReady();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
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
    PDN* cachedPDN = nullptr;

    void startLEDs(PDN* pdn, bool armed, bool confirmed);

private:
    ChainManager* chainManager;
    bool transitionToIdleFlag = false;
    int lastProcessedResult_ = 0;  // main-task-only; mirrors lastResult

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;
};



class DuelCountdown : public ConnectState<PDN> {
public:
    DuelCountdown(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainManager* chainManager);
    ~DuelCountdown();

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
    ChainManager* chainManager;
};

class ShootoutManager;

class Duel : public ConnectState<PDN> {
public:
    Duel(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainManager* chainManager, ShootoutManager* shootoutManager);
    ~Duel();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
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
    ChainManager* chainManager;
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

// Shared base for the two half-resolved duel states (local press sent:
// DuelPushed; opponent's result arrived first: DuelReceivedResult), both
// waiting on matchResultsAreIn. On dismount the match is cleared only on a
// debounced disconnect (the same condition that routes back to idle): an
// instantaneous check would wipe a voided/resolved match on a single dropped
// HELLO tick during the void->DuelResult commit, exactly the flaky-link case
// the void path exists for.
class DuelWaitState : public ConnectState<PDN> {
public:
    DuelWaitState(MatchManager* matchManager,
                  RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : ConnectState<PDN>(remoteDeviceCoordinator, stateId),
          matchManager(matchManager) {}

    bool disconnectedBackToIdle() { return isPersistentlyDisconnected(); }

protected:
    void clearMatchOnDebouncedDisconnect() {
        if (isPersistentlyDisconnected()) {
            matchManager->clearCurrentMatch();
        }
    }

    MatchManager* matchManager;
};

class DuelPushed : public DuelWaitState {
public:
    DuelPushed(MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~DuelPushed();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToDuelResult();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SimpleTimer gracePeriodTimer;
};

class DuelReceivedResult : public DuelWaitState {
public:
    DuelReceivedResult(MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~DuelReceivedResult();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToDuelResult();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SimpleTimer buttonPushGraceTimer;
};

class DuelResult : public TypedState<PDN> {
public:
    DuelResult(Player* player, MatchManager* matchManager, ShootoutManager* shootoutManager);
    ~DuelResult();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool transitionToWin();
    bool transitionToLose();
    // Shootout takes priority over the regular Win/Lose paths when a tournament
    // is active; reportLocalWin fires once on the spectator transition so the
    // bracket advances exactly once per match.
    bool transitionToShootoutSpectator();
    bool transitionToShootoutEliminated();
    // Voided duels (reliable-send abandoned, or receiver-side grace expired
    // without result) skip win/lose entirely and return to Idle.
    bool transitionToIdleOnVoid();
    // Voiding inside a shootout aborts the tournament: neither duelist can
    // broadcast a MATCH_RESULT, so the bracket cannot advance.
    bool transitionToShootoutAbortOnVoid();

private:
    Player* player;
    MatchManager* matchManager;
    ShootoutManager* shootoutManager;
    bool wonBattle = false;
    bool captured = false;
    bool voided = false;
};

// Terminal win/lose screen. The two outcomes differ only by values (state id,
// result image, chain event, animation), so they share one class; `won`
// selects them. Construct one instance per outcome.
class DuelOutcome : public TypedState<PDN> {
public:
    DuelOutcome(Player *player, ChainManager* chainManager, MatchManager* matchManager, bool won);
    ~DuelOutcome();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool resetGame();
    bool isTerminalState() override;

private:
    SimpleTimer outcomeTimer = SimpleTimer();
    Player *player;
    ChainManager* chainManager;
    MatchManager* matchManager;
    bool won_;
};

class UploadMatchesState : public TypedState<PDN> {
public:
    UploadMatchesState(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager);
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

// Aborts the tournament once the topology has settled into a non-loop (ring
// break). Debounced because connectivity flickers for a tick or two on real
// hardware.
class LoopBreakAbort {
public:
    static constexpr unsigned long kLoopBreakDebounceMs = 500;

    LoopBreakAbort(ShootoutManager* shootout, ChainManager* chainManager)
        : shootout_(shootout), chainManager_(chainManager) {}

    void tick() {
        bool ringSettledOpen = chainManager_ && chainManager_->isRingSettledOpen();
        if (debounce_.heldFor(ringSettledOpen, kLoopBreakDebounceMs)) {
            shootout_->abortTournament();
        }
    }

    void reset() { debounce_.reset(); }

private:
    ShootoutManager* shootout_;
    ChainManager* chainManager_;
    DebouncedCondition debounce_;
};

class ShootoutProposal : public TypedState<PDN> {
public:
    ShootoutProposal(ShootoutManager* shootout, ChainManager* chainManager);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToBracketReveal();
    bool transitionToIdle();
    bool transitionToAborted();

private:
    ShootoutManager* shootout_;
    bool shouldGoToReveal_ = false;
    bool shouldGoToIdle_ = false;
    bool shouldGoToAborted_ = false;
    LoopBreakAbort loopBreakAbort_;
};

class ShootoutBracketReveal : public TypedState<PDN> {
public:
    ShootoutBracketReveal(ShootoutManager* shootout, ChainManager* chainManager);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToDuelCountdown();
    bool transitionToSpectator();
    bool transitionToAborted();
    bool transitionToIdle();

private:
    ShootoutManager* shootout_;
    bool shouldGoToDuelCountdown_ = false;
    bool shouldGoToSpectator_ = false;
    bool shouldGoToAborted_ = false;
    bool shouldGoToIdle_ = false;
    LoopBreakAbort loopBreakAbort_;
};

class ShootoutSpectator : public TypedState<PDN> {
public:
    explicit ShootoutSpectator(ShootoutManager* shootout);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

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

// Passive hold screen shared by elimination and abort. The two differ only by
// values (state id, text, dwell): dwellMs == 0 holds until the tournament
// phase moves on; dwellMs > 0 times out to Idle and clears the tournament on
// exit. Construct one instance per screen.
class ShootoutOutcome : public TypedState<PDN> {
public:
    ShootoutOutcome(QuickdrawStateId id, ShootoutManager* shootout,
                    const char* heading, int headingY,
                    const char* detail, int detailY,
                    unsigned long dwellMs);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToFinalStandings();
    bool transitionToAborted();
    bool transitionToIdle();

    static constexpr unsigned long ABORTED_DISPLAY_MS = 2000;

private:
    ShootoutManager* shootout_;
    const char* heading_;
    int headingY_;
    const char* detail_;
    int detailY_;
    unsigned long dwellMs_;
    SimpleTimer displayTimer_;
    bool shouldGoToFinalStandings_ = false;
    bool shouldGoToAborted_ = false;
    bool shouldGoToIdle_ = false;
};

class ShootoutFinalStandings : public TypedState<PDN> {
public:
    ShootoutFinalStandings(ShootoutManager* shootout, ChainManager* chainManager);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool isTerminalState() override;

    bool transitionToIdle();

private:
    ShootoutManager* shootout_;
    ChainManager* chainManager_;
    SimpleTimer displayTimer_;
    bool shouldGoToIdle_ = false;
    static constexpr unsigned long STANDINGS_DISPLAY_MS = 10000;
};

class SymbolState : public ConnectState<PDN> {
public:
    SymbolState(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager);
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
    const uint8_t* fdnMac = nullptr;
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

    void advanceSymbolRender(PDN* pdn);
    void sendSymbolToFDN();
    void onSymbolMatchCommandReceived(SymbolMatchCommand command);
};

class SymbolMatched : public ConnectState<PDN> {
public:
    SymbolMatched(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, SymbolWirelessManager* symbolWirelessManager);
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
