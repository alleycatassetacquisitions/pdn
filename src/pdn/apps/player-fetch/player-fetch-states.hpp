#pragma once

#include <array>

#include "apps/player-fetch/band-registration.hpp"
#include "device/pdn.hpp"
#include "state/connect-state.hpp"
#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

namespace PlayerFetchStateId {
constexpr int AWAIT_BAND = 0;
constexpr int FETCH_USER = 1;
constexpr int CHOOSE_TEAM = 2;
constexpr int WELCOME = 4;
constexpr int FIRMWARE_UPDATE = 31;
}  // namespace PlayerFetchStateId

constexpr unsigned long PLAYER_FETCH_FIRMWARE_HOLD_MS = 5000;
constexpr unsigned long PLAYER_FETCH_DEMO_HOLD_MS = 5000;
constexpr int USER_DATA_FETCH_TIMEOUT_MS = 10000;
constexpr int FETCH_FAILURE_PAGE_TIMEOUT_MS = 3000;
constexpr int WELCOME_MESSAGE_TIMEOUT_MS = 5000;

class AwaitBandState : public ConnectState<PDN> {
public:
    /**
     * The band waiting screen. Opens @p bandRegistration for its tenure; while it is up,
     * @p firmwareUpdateManager (may be null) takes updates.
     */
    AwaitBandState(RemoteDeviceCoordinator* remoteDeviceCoordinator, PlayerProvider* playerProvider,
                   BandRegistration* bandRegistration,
                   FirmwareUpdateManager* firmwareUpdateManager);
    /** @copydoc TypedState::~TypedState */
    ~AwaitBandState() override;

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true once the cabled FDN delivered the first piece of a registration. */
    bool transitionToFetchUser() const;
    /** @return true when the primary-button demo hold completed. */
    bool transitionToChooseTeam() const;
    /** @return true when the secondary-button firmware hold completed. */
    bool transitionToFirmwareUpdate() const;

protected:
    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    void onJackState(SerialIdentifier jack, const JackConnectionState& state);
    bool isHoldComplete(unsigned long heldMs, unsigned long holdMs);
    void renderWaitingScreen(PDN* pdn) const;

    PlayerProvider* playerProvider = nullptr;
    BandRegistration* bandRegistration = nullptr;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    SimpleTimer sinceMount;
    // Whether each jack's Connected FDN says the turn is open, from its jack events.
    std::array<bool, RemoteDeviceCoordinator::HELLO_JACKS.size()> turnIsOpenByJack{};
    bool displayIsDirty = false;
    bool transitionToFetchUserState = false;
    bool transitionToChooseTeamState = false;
    bool transitionToFirmwareUpdateState = false;
};

/**
 * Waits, for up to USER_DATA_FETCH_TIMEOUT_MS from the first piece, for the rest of a band
 * registration an FDN started, and applies it through the two lookup events below.
 */
class FetchUserState : public TypedState<PDN> {
public:
    /** Loading screen while @p bandRegistration waits for the rest of its attempt. */
    FetchUserState(PlayerProvider* playerProvider, BandRegistration* bandRegistration);
    /** @copydoc TypedState::~TypedState */
    ~FetchUserState() override;

    /**
     * The FDN delivered @p found with its record key; applies its registration profile to the
     * provider's player, seeds the random generator from it and leads to Welcome. Ignored,
     * writing nothing, when no lookup is outstanding (timed out or already answered).
     */
    void onPlayerFound(const Player& found);
    /**
     * The FDN could not find the player. Shows the failure page, then returns to the waiting
     * screen. Ignored when no lookup is outstanding (timed out or already answered).
     */
    void onPlayerLookupFailed();
    /** @return true when the found player's profile is stored and welcome should mount. */
    bool transitionToWelcome() const;
    /** @return true when the lookup failed or timed out and the failure page finished. */
    bool transitionToAwaitBand() const;
    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;

private:
    void takeAnswer();
    void showLoadingGlyphs(PDN* pdn);
    void showAssetNotFoundPage(PDN* pdn);
    void beginFailurePage(PDN* pdn);

    PlayerProvider* playerProvider = nullptr;
    BandRegistration* bandRegistration = nullptr;
    SimpleTimer fetchTimer;
    SimpleTimer failurePageTimer;
    bool transitionToWelcomeState = false;
    bool transitionToAwaitBandState = false;
    bool showingFailurePage = false;
    bool failurePagePending = false;
    bool answerChanged = false;
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
