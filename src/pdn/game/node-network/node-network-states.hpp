#pragma once

#include "device/drivers/light-interface.hpp"
#include "device/pdn.hpp"
#include "image.hpp"
#include "game/node-network/match-manager-v2.hpp"
#include "game/node-network/node-network-stats.hpp"
#include "game/player.hpp"
#include "state/connect-state.hpp"
#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/symbol-wireless-manager.hpp"

enum NodeNetworkStateId {
    NODE_IDLE = 40,
    NODE_DUEL_COUNTDOWN = 41,
    NODE_DUEL = 42,
    NODE_WIN = 43,
    NODE_LINK_RETRY = 44,
    NODE_UPLOAD_MATCHES = 45,
    NODE_SLEEP = 46,
    NODE_AWAKEN = 47,
    NODE_SCORE = 48,
    NODE_ERROR = 49,
};

class NodeIdle : public ConnectState<PDN> {
public:
    NodeIdle(Player* player, MatchManagerV2* matchManager, NodeNetworkStats* stats,
             RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeIdle() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToDuelCountdown();
    bool transitionToSymbol();

protected:
    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    static constexpr unsigned long MATCH_OFFER_TIMEOUT_MS = 1000;

    /** What idle currently sees on the cable; only used to log changes once. */
    enum class LinkView : uint8_t { UNSET, NONE, INITIATOR, RESPONDER, IGNORED };

    void renderStats(PDN* pdn);
    void logLinkViewIfChanged(LinkView view);
    /** @return true when a PDN peer is fully handshaken (CONNECTED, not chained) on @p jack. */
    bool isPdnLinkedOn(SerialIdentifier jack);
    /** Derives link side from the single linked jack and, as initiator, offers a duel. */
    void updateLinkSideAndMatchStart();

    Player* player = nullptr;
    MatchManagerV2* matchManager = nullptr;
    NodeNetworkStats* stats = nullptr;
    int statsIndex = 0;
    bool displayIsDirty = true;
    bool transitionToSymbolState = false;
    SimpleTimer offerTimer;
    LinkView lastLinkView = LinkView::UNSET;
};

/**
 * Initiator: measures the clock offset, then sends the round. Responder: waits for the round.
 * Both count down to the shared step 0; any setup failure aborts the round uncounted.
 */
class NodeDuelCountdown : public ConnectState<PDN> {
public:
    NodeDuelCountdown(MatchManagerV2* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeDuelCountdown() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool shallWeBattle();
    bool disconnectedBackToIdle();

    /** @return true once the round was aborted, here or by the opponent. */
    bool abortedToError();

protected:
    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    enum class Phase : uint8_t { SYNCING, AWAITING_SEQUENCE, COUNTING };
    enum class CountdownStep : int { THREE = 3, TWO = 2, ONE = 1, BATTLE = 0 };

    static CountdownStep stepFor(int32_t msUntilStart);
    static ImageType imageFor(CountdownStep step);
    static AnimationConfig animationConfigFor(CountdownStep step);
    /** Shows the image, restarts the lights, and arms the haptics for @p step. */
    void showStep(PDN* pdn, CountdownStep step);
    void loopSyncing();
    void loopCounting(PDN* pdn);

    MatchManagerV2* matchManager = nullptr;
    Phase phase = Phase::SYNCING;
    CountdownStep shownStep = CountdownStep::THREE;
    uint8_t probesSent = 0;
    /** Syncing: spacing of probes, then the settle wait. Awaiting: the SEQUENCE deadline. */
    SimpleTimer phaseTimer;
    SimpleTimer hapticTimer;
    bool doBattle = false;
    static constexpr int HAPTIC_DURATION = 75;
    static constexpr int HAPTIC_INTENSITY = 255;
};

/**
 * Plays the shared schedule: step i runs from msUntilStep(i) == 0 until its window closes on
 * each device's clock. A lone presser sees NOW while the other player sees the prompt; when both
 * press, each sees their own prompt. Pressed buttons are drawn inverted. Each presser judges
 * their own presses and reports them.
 */
class NodeDuel : public ConnectState<PDN> {
public:
    NodeDuel(MatchManagerV2* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeDuel() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    /** @return true once the round has concluded. */
    bool transitionToScore();

    bool disconnectedBackToIdle();

    /** @return true once the round was aborted, here or by the opponent. */
    bool abortedToError();

protected:
    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    void onButtonPressed(ButtonIdentifier button);
    void showStep(PDN* pdn);
    void drawStep(PDN* pdn);
    bool showsNow() const;
    uint8_t pressesToShow() const;
    void endStepDisplay(PDN* pdn);
    void drawStepResult(PDN* pdn, uint8_t step);
    void refreshStepResult(PDN* pdn);
    void runSchedule(PDN* pdn);
    void finishAsInitiator();
    void finishAsResponder();
    void concludeRound();

    MatchManagerV2* matchManager = nullptr;
    uint8_t currentStep = 0;
    bool stepShown = false;
    bool stepDisplayEnded = false;
    bool localPressJudged = false;
    uint8_t localPressCount = 0;
    uint8_t drawnPressCount = 0;
    bool roundConcluded = false;
    uint8_t resultScreenStep = 0;
    bool resultScreenPending = false;
    SimpleTimer hapticTimer;
    static constexpr int HAPTIC_DURATION = 75;
    static constexpr int HAPTIC_INTENSITY = 200;
    /** Left edge of the second image of a two-button prompt; the first starts at 0. */
    static constexpr int SECOND_PROMPT_X = 64;
};

/** Shows "Error / Retrying" after an uncounted abort, then returns to idle. */
class NodeError : public TypedState<PDN> {
public:
    /** @param matchManager whose abort flag is lowered on dismount. */
    explicit NodeError(MatchManagerV2* matchManager);

    /** Does not own the match manager. */
    ~NodeError() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;

    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;

    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;

    /** @return true after the display time. */
    bool transitionToIdle();

    static constexpr unsigned long DISPLAY_DURATION_MS = 1500;

private:
    MatchManagerV2* matchManager = nullptr;
    SimpleTimer displayTimer;
};

/** Shows "Score / # / 5" on both devices before upload or the retry screen. */
class NodeScore : public TypedState<PDN> {
public:
    /** @param matchManager source of the last round's score and success. */
    explicit NodeScore(MatchManagerV2* matchManager);

    /** Does not own the match manager. */
    ~NodeScore() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;

    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;

    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;

    /** @return true after the display time when the round succeeded. */
    bool transitionToWin();

    /** @return true after the display time when the round failed. */
    bool transitionToLinkRetry();

private:
    MatchManagerV2* matchManager = nullptr;
    SimpleTimer displayTimer;
    static constexpr unsigned long DISPLAY_DURATION_MS = 2500;
};

class NodeWin : public TypedState<PDN> {
public:
    explicit NodeWin(MatchManagerV2* matchManager);
    ~NodeWin() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToUpload();

private:
    MatchManagerV2* matchManager = nullptr;
    SimpleTimer winTimer;
};

class NodeLinkRetry : public TypedState<PDN> {
public:
    NodeLinkRetry(MatchManagerV2* matchManager);
    ~NodeLinkRetry() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToIdle();
    bool transitionToSleep();

private:
    MatchManagerV2* matchManager = nullptr;
    SimpleTimer displayTimer;
};

class NodeUploadMatches : public TypedState<PDN> {
public:
    NodeUploadMatches(WirelessManager* wirelessManager, MatchManagerV2* matchManager,
                      NodeNetworkStats* stats);
    ~NodeUploadMatches() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToSleep();

private:
    void attemptUpload(PDN* pdn);

    WirelessManager* wirelessManager = nullptr;
    MatchManagerV2* matchManager = nullptr;
    NodeNetworkStats* stats = nullptr;
    SimpleTimer uploadTimer;
    int retryCount = 0;
    bool transitionToSleepState = false;
    bool shouldRetryUpload = false;
    uint8_t uploadPeerMac[6] = {};
    static constexpr int UPLOAD_TIMEOUT_MS = 10000;
};

class NodeSleep : public TypedState<PDN> {
public:
    NodeSleep(MatchManagerV2* matchManager);
    ~NodeSleep() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToAwaken();

private:
    MatchManagerV2* matchManager = nullptr;
    SimpleTimer dormantTimer;
    bool transitionToAwakenState = false;
    static constexpr unsigned long SLEEP_DURATION_MS = 60000UL;
};

class NodeAwaken : public TypedState<PDN> {
public:
    NodeAwaken();
    ~NodeAwaken() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToIdle();

private:
    SimpleTimer activationSequenceTimer;
    int activateMotorCount = 0;
    bool activateMotor = false;
    static constexpr int AWAKEN_THRESHOLD = 20;
    static constexpr int ACTIVATION_STEP_MS = 100;
};
