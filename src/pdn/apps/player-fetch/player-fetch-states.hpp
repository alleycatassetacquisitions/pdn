#pragma once

#include "device/pdn.hpp"
#include "state/state.hpp"
#include "game/player.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "game/quickdraw-resources.hpp"
#include "apps/player-fetch/player-fetch-session.hpp"

class FirmwareUpdateManager;

namespace PlayerFetchStateId {
constexpr int PIN_ENTRY = 0;
constexpr int FETCH_USER = 1;
constexpr int CHOOSE_TEAM = 2;
constexpr int DEMO_STUB = 3;
constexpr int WELCOME = 4;
constexpr int AWAKEN = 5;
constexpr int FIRMWARE_UPDATE = 31;
}  // namespace PlayerFetchStateId

constexpr const char DEMO_PIN[] = "0000";
constexpr unsigned long PLAYER_FETCH_FIRMWARE_HOLD_MS = 5000;
constexpr int USER_DATA_FETCH_TIMEOUT_MS = 10000;
constexpr int FETCH_FAILURE_PAGE_TIMEOUT_MS = 3000;
constexpr int WELCOME_MESSAGE_TIMEOUT_MS = 5000;

class PinEntryState : public TypedState<PDN> {
public:
    /** Pairing-code entry for PlayerFetch. */
    explicit PinEntryState(Player* player);
    /** @copydoc TypedState::~TypedState */
    ~PinEntryState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when a non-demo PIN is ready for protobuf fetch. */
    bool transitionToFetchUser() const;
    /** @return true when PIN @ref DEMO_PIN was entered. */
    bool transitionToChooseTeam() const;
    /** @return true when secondary-button OTA hold completed on pin entry. */
    bool transitionToFirmwareUpdate() const;

private:
    bool transitionToFetchUserState = false;
    bool transitionToChooseTeamState = false;
    bool transitionToFirmwareUpdateState = false;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    SimpleTimer sinceMount;
    bool shouldRender = false;
    Player* player = nullptr;
    int currentDigit = 0;
    int currentDigitIndex = 0;
    static constexpr int DIGIT_COUNT = 4;
    int inputId[DIGIT_COUNT] = {0, 0, 0, 0};
};

class FetchUserState : public TypedState<PDN> {
public:
    /** Protobuf device player lookup for the entered PIN. */
    FetchUserState(Player* player, WirelessManager* wirelessManager, PlayerFetchSession* session);
    /** @copydoc TypedState::~TypedState */
    ~FetchUserState() override;

    /** @return true when @ref DevicePlayer was stored and welcome should mount. */
    bool transitionToWelcome() const;
    /** @return true when fetch failed and the asset-not-found page finished. */
    bool transitionToPinEntry() const;
    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;

private:
    void fetchDevicePlayer();
    void showLoadingGlyphs(PDN* pdn);
    void showAssetNotFoundPage(PDN* pdn);
    void beginFailurePage(PDN* pdn);

    Player* player = nullptr;
    WirelessManager* wirelessManager = nullptr;
    PlayerFetchSession* session = nullptr;
    SimpleTimer fetchTimer;
    SimpleTimer failurePageTimer;
    bool transitionToWelcomeState = false;
    bool transitionToPinEntryState = false;
    bool showingFailurePage = false;
    bool failurePagePending = false;
};

class ChooseTeamState : public TypedState<PDN> {
public:
    /** Demo Hunter/Bounty picker for @ref DEMO_PIN. */
    ChooseTeamState(Player* player, PlayerFetchSession* session);
    /** @copydoc TypedState::~TypedState */
    ~ChooseTeamState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when demo team selection should mount the stub state. */
    bool transitionToDemoStub() const;

private:
    void renderUi(PDN* pdn) const;

    Player* player = nullptr;
    PlayerFetchSession* session = nullptr;
    bool transitionToDemoStubState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};

class DemoStubState : public TypedState<PDN> {
public:
    /** Placeholder demo mode after choose-team. */
    explicit DemoStubState(Player* player);
    /** @copydoc TypedState::~TypedState */
    ~DemoStubState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the stub should return to pin entry. */
    bool transitionToPinEntry() const;

private:
    Player* player = nullptr;
    bool transitionToPinEntryState = false;
};

class PlayerFetchWelcome : public TypedState<PDN> {
public:
    /** Short welcome screen before awaken. */
    PlayerFetchWelcome(Player* player, PlayerFetchSession* session);
    /** @copydoc TypedState::~TypedState */
    ~PlayerFetchWelcome() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the welcome timer expired and awaken should mount. */
    bool transitionToAwaken() const;

private:
    Player* player = nullptr;
    PlayerFetchSession* session = nullptr;
    SimpleTimer welcomeMessageTimer;
    bool transitionToAwakenState = false;
};

class PlayerFetchAwakenSequence : public TypedState<PDN> {
public:
    /** In-app awaken animation after welcome. */
    explicit PlayerFetchAwakenSequence(Player* player, PlayerFetchSession* session);
    /** @copydoc TypedState::~TypedState */
    ~PlayerFetchAwakenSequence() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the haptic awaken sequence finished. */
    bool transitionToComplete() const;

private:
    static constexpr int AWAKEN_THRESHOLD = 20;
    Player* player = nullptr;
    PlayerFetchSession* session = nullptr;
    SimpleTimer activationSequenceTimer;
    int activateMotorCount = 0;
    bool activateMotor = false;
    const int activationStepDuration = 100;
};
