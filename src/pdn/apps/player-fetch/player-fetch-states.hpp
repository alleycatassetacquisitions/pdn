#pragma once

#include "device/pdn.hpp"
#include "state/state.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "game/player-provider.hpp"
#include "game/player-types.hpp"
#include "wireless/reliable-channel.hpp"

#include <array>
#include <optional>

class FirmwareUpdateManager;

namespace PlayerFetchStateId {
constexpr int AWAIT_BAND = 0;
constexpr int CHOOSE_TEAM = 2;
constexpr int WELCOME = 4;
constexpr int FIRMWARE_UPDATE = 31;
}  // namespace PlayerFetchStateId

constexpr unsigned long PLAYER_FETCH_FIRMWARE_HOLD_MS = 5000;
constexpr unsigned long PLAYER_FETCH_DEMO_HOLD_MS = 5000;
// Runs from the first piece (record key or profile) accepted; the wait for the tap has no limit.
constexpr unsigned long REGISTRATION_ATTEMPT_TIMEOUT_MS = 10000;
constexpr int REGISTRATION_FAILURE_PAGE_TIMEOUT_MS = 3000;
constexpr int WELCOME_MESSAGE_TIMEOUT_MS = 5000;

/** The FDN a registration attempt is bound to, and the jack it is Connected on. */
struct RegistrationFdn {
    std::array<uint8_t, 6> mac{};
    SerialIdentifier jack = SerialIdentifier::INPUT_JACK;
};

class AwaitBandState : public TypedState<PDN> {
public:
    /**
     * Waiting screen of an unprovisioned PDN: asks to be cabled to an FDN, then,
     * cabled, for a band tap there while that FDN says this jack has its
     * registration turn, and to wait otherwise. Takes the record key (jack) and
     * profile (radio) that FDN pushes, showing a failure page for
     * REGISTRATION_FAILURE_PAGE_TIMEOUT_MS when an attempt fails. Sends nothing
     * to ask for them. Constructing claims the kPlayerProvision packet type on
     * @p wirelessManager.
     */
    AwaitBandState(WirelessManager* wirelessManager, PlayerProvider* playerProvider);

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true when the delivered profile was applied and welcome should mount. */
    bool transitionToWelcome() const;
    /** @return true when the primary-button demo hold completed. */
    bool transitionToChooseTeam() const;
    /** @return true when the secondary-button firmware hold completed. */
    bool transitionToFirmwareUpdate() const;

private:
    // Which of the three waiting screens is up: no FDN cabled, an FDN cabled
    // that has not said this PDN has its turn, and one that has.
    enum class WaitingScreen : uint8_t {
        CABLE_TO_FDN,
        PLEASE_WAIT,
        TAP_BAND,
    };

    void onProvisionPacket(const uint8_t* fromMac, const PlayerProvisionPacket& pkt);
    void onApplicationFrame(SerialIdentifier jack, ApplicationFrameKind kind,
                            const ApplicationPayload& payload);
    void onJackChange(SerialIdentifier jack, bool connected);
    bool isAttemptOpen() const;
    bool findFdnJack(const uint8_t* fdnMac, SerialIdentifier& jack) const;
    void beginAttempt(const uint8_t* fdnMac, SerialIdentifier jack);
    void completeRegistrationIfReady();
    void endAttempt();
    void failAttempt();
    void renderFailurePage(PDN* pdn);
    void dismissFailurePage(PDN* pdn);
    bool isHoldComplete(unsigned long heldMs, unsigned long holdMs);
    WaitingScreen getWaitingScreenForCable() const;
    void renderWaitingScreen(PDN* pdn) const;

    ReliableChannel<PlayerProvisionPacket> provisionChannel;
    PlayerProvider* playerProvider = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    std::optional<RegistrationFdn> attemptFdn;
    std::optional<PlayerProvisionPacket> heldProfile;
    SimpleTimer attemptTimer;
    SimpleTimer sinceMount;
    SimpleTimer failurePageTimer;
    WaitingScreen waitingScreen = WaitingScreen::CABLE_TO_FDN;
    bool failurePageIsShowing = false;
    bool failurePageIsDirty = false;
    bool transitionToWelcomeState = false;
    bool transitionToChooseTeamState = false;
    bool transitionToFirmwareUpdateState = false;
};

class ChooseTeamState : public TypedState<PDN> {
public:
    /** Demo Hunter/Bounty picker, reached by the demo hold on the waiting screen. */
    explicit ChooseTeamState(PlayerProvider* playerProvider);

    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;
    /** @return true once a demo role is chosen and the hub should mount. */
    bool transitionToHub() const;

private:
    void renderUi(PDN* pdn) const;

    PlayerProvider* playerProvider = nullptr;
    bool transitionToHubState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};

class PlayerFetchWelcome : public TypedState<PDN> {
public:
    /** Short welcome screen before awaken. */
    explicit PlayerFetchWelcome(PlayerProvider* playerProvider);

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
