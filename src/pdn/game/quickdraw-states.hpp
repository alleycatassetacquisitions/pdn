#pragma once

#include "device/pdn.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "state/connect-state.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include "game/quickdraw-resources.hpp"
#include <cstdio>
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
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once the last of the AWAKEN_THRESHOLD steps has run. */
    bool transitionToIdle() const;

private:
    static constexpr int AWAKEN_THRESHOLD = 20;
    SimpleTimer activationSequenceTimer;
    int activateMotorCount = 0;
    const int activationStepDuration = 100;
    Player* player = nullptr;
};

class Idle : public ConnectState<PDN> {
public:
    explicit Idle(const GameContext& ctx);

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
    ShootoutManager* shootoutManager;
    bool matchInitialized = false;
    bool displayIsDirty = false;
    int statsIndex = 0;
    int lastScreenIndex = 8;
    static constexpr int NAME_SCREEN_INDEX = 0;
    static constexpr size_t NAME_LINE_LIMIT = 3;
    static constexpr int NAME_LABEL_BASELINE = 12;
    static constexpr int NAME_FIRST_LINE_BASELINE = 28;
    static constexpr int NAME_LINE_SPACING = 16;

    void renderPlayerName(PDN* pdn);

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    SimpleTimer matchInitializationTimer;
    // The hunter must outlast the whole handshake: its SEND_MATCH_ID and the bounty's
    // MATCH_ID_ACK can each take a full retransmit span to land, and a bounty whose
    // ack lands after the hunter gave up would count down alone.
    static constexpr int MATCH_INITIALIZATION_TIMEOUT =
    2 * static_cast<int>(Resender::RETRANSMIT_SPAN_MS) + 250;
};

class SupporterReady : public TypedState<PDN> {
public:
    explicit SupporterReady(const GameContext& ctx);
    ~SupporterReady();

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once this device is no longer a supporter in a posse. */
    bool transitionToIdle() const;

    /** A chain game event from the challenger this device follows, via
     * PosseManager::setOnGameEvent. */
    void onChainGameEventReceived(uint8_t eventType);

public:
    // Plain members: the button driver and the ESP-NOW driver both dispatch
    // from exec() on the main task, the same one that runs onStateLoop.
    Player *player;
    bool buttonArmed = false;
    bool hasConfirmed = false;
    bool displayIsDirty = true;
    bool ledsAreDirty = true;
    int lastResult = 0;
    SimpleTimer resultClearTimer;
    PDN* cachedPDN = nullptr;

    void startLEDs(PDN* pdn, bool armed, bool confirmed);

private:
    static constexpr unsigned long RESULT_CLEAR_MS = 8000;

    PosseManager* posseManager;
    bool transitionToIdleFlag = false;
};

/** Draws the stats card over a Win or Lose screen: own time, boost and penalty if any, opponent
 * time. */
inline void renderLastMatchStats(Display* display, const LastMatchDisplay& stats) {
    if (!stats.hasData) return;
    const bool hasBoost = stats.boostMs > 0;
    const bool hasPenalty = stats.penaltyMs > 0;
    // One buffer serves every line: drawText copies into the frame buffer at once.
    char line[28];
    display->drawImage(Image(image_stats_card, 60, 64, 0, 0));
    snprintf(line, sizeof(line), "You:%4ld", stats.myTimeMs);
    display->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)->drawText(line, 2, 12);
    if (hasBoost) {
        snprintf(line, sizeof(line), "Boost: %lu",
                 stats.boostMs / PosseManager::BOOST_PER_SUPPORTER_MS);
        display->drawText(line, 2, hasPenalty ? 24 : 28);
    }
    if (hasPenalty) {
        snprintf(line, sizeof(line), "Pen:+%lu", stats.penaltyMs);
        display->drawText(line, 2, hasBoost ? 36 : 28);
    }
    snprintf(line, sizeof(line), "Opp:%4ld", stats.opponentTimeMs);
    display->drawText(line, 2, (hasBoost && hasPenalty) ? 48 : 44);
}

class MatchCountdown : public TypedState<PDN> {
public:
    /** Constructs the pre-match 3-2-1 countdown state. */
    explicit MatchCountdown(const GameContext& ctx);

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once the countdown has reached BATTLE. */
    bool shallWeBattle() const;

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

class MatchState : public TypedState<PDN> {
public:
    /** Constructs the active match draw-window state. */
    explicit MatchState(const GameContext& ctx);

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

private:
    Player* player;
    MatchManager* matchManager;
    PosseManager* posseManager;
    ShootoutManager* shootoutManager;
    bool transitionToMatchPushedState = false;
    bool transitionToMatchReceivedResultState = false;
    bool transitionToIdleState = false;
    bool transitionToShootoutSpectatorState = false;
    bool transitionToShootoutEliminatedState = false;
    SimpleTimer matchTimer;
    static constexpr int MATCH_TIMEOUT = 4000;
};

class MatchPushed : public TypedState<PDN> {
public:
    /** Constructs the post-press grace-period state. */
    explicit MatchPushed(const GameContext& ctx);

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once both results are present or the post-press grace period expires. */
    bool transitionToMatchResult();

private:
    MatchManager* matchManager;
    SimpleTimer gracePeriodTimer;
    // Covers the opponent's 750ms BUTTON_PUSH_GRACE_PERIOD in MatchReceivedResult,
    // a 1500ms reliable-send retry budget for their NEVER_PRESSED, and 250ms of
    // entry/schedule margin.
    static constexpr int MATCH_RESULT_GRACE_PERIOD = 2500;
};

class MatchReceivedResult : public TypedState<PDN> {
public:
    /** Constructs the state waiting for a local counter-press after receiving peer result. */
    explicit MatchReceivedResult(const GameContext& ctx);

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    /** True once the counter-press arrives or the grace period expires. */
    bool transitionToMatchResult();

private:
    SimpleTimer buttonPushGraceTimer;
    bool transitionToMatchResultState = false;
    const int BUTTON_PUSH_GRACE_PERIOD = 750;
    MatchManager* matchManager;
};

class MatchResult : public TypedState<PDN> {
public:
    /** Constructs the match outcome evaluation state. */
    explicit MatchResult(const GameContext& ctx);

    void onStateMounted(PDN* pdn) override;
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

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool resetGame();

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

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;
    bool resetGame();

private:
    SimpleTimer loseTimer = SimpleTimer();
    Player *player;
    PosseManager* posseManager;
    MatchManager* matchManager;
    bool reset = false;
};

class ShootoutProposal : public TypedState<PDN> {
public:
    /** Constructs the confirm-to-join screen of a tournament. */
    explicit ShootoutProposal(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    /** True while the tournament is revealing its bracket. */
    bool transitionToBracketReveal() const;

private:
    ShootoutManager* shootoutManager = nullptr;
    ShootoutPhaseSubscription phaseSubscription;
    bool transitionToBracketRevealState = false;
};

class ShootoutBracketReveal : public TypedState<PDN> {
public:
    /** Constructs the bracket-reveal screen. */
    explicit ShootoutBracketReveal(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    /** True when the match that has started names this device as a player. */
    bool transitionToMatchCountdown() const;
    /** True when the match that has started is between two other devices. */
    bool transitionToSpectator() const;

private:
    ShootoutManager* shootoutManager = nullptr;
    ShootoutPhaseSubscription phaseSubscription;
    bool transitionToMatchCountdownState = false;
    bool transitionToSpectatorState = false;
};

class ShootoutSpectator : public TypedState<PDN> {
public:
    /** Constructs the screen a surviving player watches other matches from. */
    explicit ShootoutSpectator(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    /** True when the match that has started names this device as a player. */
    bool transitionToMatchCountdown() const;
    /** True once the tournament has a winner. */
    bool transitionToFinalStandings() const;

private:
    void onPhaseEntered(ShootoutManager::Phase phase);
    void renderMatchPair(PDN* pdn);

    ShootoutManager* shootoutManager = nullptr;
    ShootoutPhaseSubscription phaseSubscription;
    bool transitionToMatchCountdownState = false;
    bool transitionToFinalStandingsState = false;
    bool displayIsDirty = false;
};

class ShootoutEliminated : public TypedState<PDN> {
public:
    /** Constructs the screen a knocked-out player waits on. */
    explicit ShootoutEliminated(const GameContext& ctx);
    void onStateMounted(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    /** True once the tournament has a winner. */
    bool transitionToFinalStandings() const;

private:
    ShootoutManager* shootoutManager = nullptr;
    ShootoutPhaseSubscription phaseSubscription;
    bool transitionToFinalStandingsState = false;
};

class ShootoutFinalStandings : public TypedState<PDN> {
public:
    explicit ShootoutFinalStandings(const GameContext& ctx);
    /** Drops the ring-membership handler if destroyed while mounted, since it holds `this`. */
    ~ShootoutFinalStandings() override;
    void onStateMounted(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    /** True once this device is no longer in the ring. */
    bool transitionToSleep() const;

private:
    ShootoutManager* shootout_;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    bool transitionToSleepState = false;
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

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

    bool transitionToIdle();
    bool transitionToSymbolMatched();

private:
    Player* player;
    SymbolWirelessManager* symbolWirelessManager;
    PDN* mountedPdn = nullptr;
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

    void onOutputJackState(SerialIdentifier jack, bool connected);
    void advanceSymbolRender(PDN* pdn);
    void sendSymbolToFDN();
    void onSymbolMatchCommandReceived(SymbolMatchCommand command);
};

class SymbolMatched : public ConnectState<PDN> {
public:
    explicit SymbolMatched(const GameContext& ctx);

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
