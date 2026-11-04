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

class NodeDuelCountdown : public ConnectState<PDN> {
public:
    NodeDuelCountdown(MatchManagerV2* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeDuelCountdown() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool shallWeBattle();
    bool disconnectedBackToIdle();

protected:
    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    enum class CountdownStep : int { THREE = 3, TWO = 2, ONE = 1, BATTLE = 0 };

    struct CountdownStage {
        CountdownStep step;
        unsigned long countdownTimer;
    };

    static CountdownStage stageAt(int index);
    static ImageType imageFor(CountdownStep step);
    static AnimationConfig animationConfigFor(CountdownStep step);
    /** Shows the image, restarts the lights, and arms the timer and haptics for @p stage. */
    void showStep(PDN* pdn, const CountdownStage& stage);

    MatchManagerV2* matchManager = nullptr;
    SimpleTimer countdownTimer;
    SimpleTimer hapticTimer;
    int currentStepIndex = 0;
    bool doBattle = false;
    static constexpr int HAPTIC_DURATION = 75;
    static constexpr int HAPTIC_INTENSITY = 255;
};

class NodeDuel : public ConnectState<PDN> {
public:
    NodeDuel(MatchManagerV2* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~NodeDuel() override;

    void onStateMounted(PDN* pdn) override;
    void onStateLoop(PDN* pdn) override;
    void onStateDismounted(PDN* pdn) override;

    bool transitionToWin();
    bool transitionToLinkRetry();
    bool disconnectedBackToIdle();

protected:
    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    void onDrawButton(PDN* pdn);

    MatchManagerV2* matchManager = nullptr;
    SimpleTimer duelTimer;
    unsigned long duelStartMs = 0;
    bool outcomeReady = false;
    bool syncSuccess = false;
    static constexpr int DUEL_TIMEOUT = 4000;
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
