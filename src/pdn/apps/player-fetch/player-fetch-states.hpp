#pragma once

#include "device/pdn.hpp"
#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

namespace PlayerFetchStateId {
constexpr int PIN_ENTRY = 0;
constexpr int FETCH_USER = 1;
constexpr int CHOOSE_TEAM = 2;
constexpr int WELCOME = 4;
constexpr int FIRMWARE_UPDATE = 31;
}  // namespace PlayerFetchStateId

constexpr unsigned long PLAYER_FETCH_FIRMWARE_HOLD_MS = 5000;
constexpr int USER_DATA_FETCH_TIMEOUT_MS = 10000;
constexpr int FETCH_FAILURE_PAGE_TIMEOUT_MS = 3000;
constexpr int WELCOME_MESSAGE_TIMEOUT_MS = 5000;

class PinEntryState : public TypedState<PDN> {
public:
    /** Pairing-code entry; while it is up, @p firmwareUpdateManager (may be null) takes updates. */
    PinEntryState(PlayerProvider* playerProvider, FirmwareUpdateManager* firmwareUpdateManager);
    /** @copydoc TypedState::~TypedState */
    ~PinEntryState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true once four digits are entered; every PIN leads to the role picker. */
    bool transitionToChooseTeam() const;
    /** @return true when secondary-button OTA hold completed on pin entry. */
    bool transitionToFirmwareUpdate() const;

private:
    bool transitionToChooseTeamState = false;
    bool transitionToFirmwareUpdateState = false;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    SimpleTimer sinceMount;
    bool shouldRender = false;
    PlayerProvider* playerProvider = nullptr;
    int currentDigit = 0;
    int currentDigitIndex = 0;
    static constexpr int DIGIT_COUNT = 4;
    int inputId[DIGIT_COUNT] = {0, 0, 0, 0};
};

/**
 * Waits for an FDN to look the entered PIN up. Not in the PlayerFetch flow yet: the
 * registration project (#485) wires it in and calls the two lookup events below.
 */
class FetchUserState : public TypedState<PDN> {
public:
    /** Loading screen while the FDN lookup for the stored PIN is outstanding. */
    explicit FetchUserState(PlayerProvider* playerProvider);
    /** @copydoc TypedState::~TypedState */
    ~FetchUserState() override;

    /**
     * Applies @p found's registration profile and leads to Welcome; ignored, writing nothing,
     * when no lookup is outstanding.
     */
    void onPlayerFound(const Player& found);
    /** Leads to the failure page, then PIN entry; ignored when no lookup is outstanding. */
    void onPlayerLookupFailed();
    /** @return true when the found player's profile is stored and welcome should mount. */
    bool transitionToWelcome() const;
    /** @return true when the lookup failed or timed out and the failure page finished. */
    bool transitionToPinEntry() const;
    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;

private:
    void showLoadingGlyphs(PDN* pdn);
    void showAssetNotFoundPage(PDN* pdn);
    void beginFailurePage(PDN* pdn);

    PlayerProvider* playerProvider = nullptr;
    SimpleTimer fetchTimer;
    SimpleTimer failurePageTimer;
    bool transitionToWelcomeState = false;
    bool transitionToPinEntryState = false;
    bool showingFailurePage = false;
    bool failurePagePending = false;
};

class ChooseTeamState : public TypedState<PDN> {
public:
    /** Role picker: MAC test UUID and "Test" if @p testIdentity, else nil UUID and "Demo". */
    ChooseTeamState(PlayerProvider* playerProvider, bool testIdentity);
    /** @copydoc TypedState::~TypedState */
    ~ChooseTeamState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true once a role is confirmed and the welcome screen should mount. */
    bool transitionToWelcome() const;

private:
    void renderUi(PDN* pdn) const;

    PlayerProvider* playerProvider = nullptr;
    const uint8_t* macAddress = nullptr;
    bool testIdentity = false;
    bool transitionToWelcomeState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};

class PlayerFetchWelcome : public TypedState<PDN> {
public:
    /** Short welcome screen with the player's name before the hub. */
    explicit PlayerFetchWelcome(PlayerProvider* playerProvider);
    /** @copydoc TypedState::~TypedState */
    ~PlayerFetchWelcome() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the welcome timer expires. */
    bool transitionToAwaken() const;

private:
    PlayerProvider* playerProvider = nullptr;
    SimpleTimer welcomeMessageTimer;
    bool transitionToAwakenState = false;
};
