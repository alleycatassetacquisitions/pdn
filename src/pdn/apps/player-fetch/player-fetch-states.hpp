#pragma once

#include "device/pdn.hpp"
#include "state/state.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

namespace PlayerFetchStateId {
constexpr int FETCH_USER = 1;
constexpr int WELCOME = 4;
constexpr int FIRMWARE_UPDATE = 31;
}  // namespace PlayerFetchStateId

constexpr unsigned long PLAYER_FETCH_FIRMWARE_HOLD_MS = 5000;
constexpr int FETCH_FAILURE_PAGE_TIMEOUT_MS = 3000;
constexpr int WELCOME_MESSAGE_TIMEOUT_MS = 5000;

class FetchUserState : public TypedState<PDN> {
public:
    /** Waits for cabled FDN-to-PDN PlayerProvisionPacket over PktType::kPlayerProvision. */
    FetchUserState(WirelessManager* wirelessManager, PlayerProvider* playerProvider);
    /** @copydoc TypedState::~TypedState */
    ~FetchUserState() override;

    /** @return true when a valid provisioned profile was stored and welcome should mount. */
    bool transitionToWelcome() const;
    /** @return true when secondary-button OTA hold completed while waiting for provisioning. */
    bool transitionToFirmwareUpdate() const;
    /** @return true while displaying the 3000 ms asset-not-found screen. */
    bool isShowingFailurePage() const { return showingFailurePage; }
    /** @copydoc TypedState::onStateMounted */
    void onStateMounted(PDN* pdn) override;
    /** @copydoc TypedState::onStateLoop */
    void onStateLoop(PDN* pdn) override;
    /** @copydoc TypedState::onStateDismounted */
    void onStateDismounted(PDN* pdn) override;

    /** Handles an inbound PktType::kPlayerProvision frame. */
    void onProvisionPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);

private:
    bool isConnectedFdnMac(const uint8_t* mac) const;
    void showLoadingGlyphs(PDN* pdn);
    void showAssetNotFoundPage(PDN* pdn);
    void beginFailurePage(PDN* pdn);

    WirelessManager* wirelessManager = nullptr;
    PlayerProvider* playerProvider = nullptr;
    PDN* mountedPdn = nullptr;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    SimpleTimer sinceMount;
    SimpleTimer failurePageTimer;
    bool transitionToWelcomeState = false;
    bool transitionToFirmwareUpdateState = false;
    bool showingFailurePage = false;
    bool failurePagePending = false;
};

class PlayerFetchWelcome : public TypedState<PDN> {
public:
    /** Short welcome screen before awaken. */
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
