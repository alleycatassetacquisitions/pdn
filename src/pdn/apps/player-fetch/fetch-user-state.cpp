#include "apps/player-fetch/player-fetch-states.hpp"
#include <cstring>
#include "alleycat-server/player-provision-codec.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/quickdraw-resources.hpp"

static const char TAG[] = "FetchUserState";

FetchUserState::FetchUserState(PdnProvisioningClient* provisioningClient,
                               PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::FETCH_USER)
    , provisioningClient(provisioningClient)
    , playerProvider(playerProvider) {}

FetchUserState::~FetchUserState() {
    provisioningClient = nullptr;
    playerProvider = nullptr;
    remoteDeviceCoordinator = nullptr;
}

void FetchUserState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - requesting provisioning for PIN %s",
          playerProvider->getRegistrationPin());
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToPinEntryState = false;
    requestEverSent = false;
    remoteDeviceCoordinator = pdn->getRemoteDeviceCoordinator();
    provisioningClient->setOnReply(
    [this](const uint8_t* fromMac, const PlayerProvisionPacket& pkt) {
        onProvisionPacket(fromMac, pkt);
    });
    showLoadingGlyphs(pdn);
    fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT_MS);
    requestProvisionFromCabledFdn();
}

void FetchUserState::onStateLoop(PDN* pdn) {
    if (failurePagePending) {
        beginFailurePage(pdn);
        failurePagePending = false;
    }

    if (showingFailurePage) {
        failurePageTimer.updateTime();
        if (failurePageTimer.expired()) {
            transitionToPinEntryState = true;
        }
        return;
    }

    // The FDN may be cabled after the PIN was entered, or deaf on Wi-Fi when it was sent.
    if (!requestEverSent || provisioningClient->isRequestAbandoned()) {
        requestProvisionFromCabledFdn();
    }

    fetchTimer.updateTime();
    if (fetchTimer.expired()) {
        LOG_W(TAG, "Provisioning timed out");
        beginFailurePage(pdn);
    } else if (fetchTimer.isRunning()) {
        if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            showLoadingGlyphs(pdn);
        }
    }
}

void FetchUserState::onStateDismounted(PDN* pdn) {
    provisioningClient->setOnReply(nullptr);
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToPinEntryState = false;
    requestEverSent = false;
    fetchTimer.invalidate();
    failurePageTimer.invalidate();
}

void FetchUserState::requestProvisionFromCabledFdn() {
    const uint8_t* fdnMac = remoteDeviceCoordinator != nullptr
                            ? remoteDeviceCoordinator->getConnectedPeerMac(DeviceType::FDN)
                            : nullptr;
    if (fdnMac == nullptr) {
        return;
    }
    provisioningClient->sendRequest(fdnMac, playerProvider->getRegistrationPin());
    // The reply window runs from the first request, which can go out after the PIN was entered.
    if (!requestEverSent) {
        fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT_MS);
    }
    requestEverSent = true;
}

void FetchUserState::onProvisionPacket(const uint8_t* fromMac, const PlayerProvisionPacket& pkt) {
    if (remoteDeviceCoordinator == nullptr ||
        !remoteDeviceCoordinator->isConnectedPeer(DeviceType::FDN, fromMac)) {
        LOG_W(TAG, "Ignoring PlayerProvisionPacket from non-cabled sender");
        return;
    }

    if (strncmp(pkt.pin, playerProvider->getRegistrationPin(), sizeof(pkt.pin) - 1) != 0) {
        LOG_W(TAG, "Ignoring PlayerProvisionPacket for another PIN");
        return;
    }

    if (pkt.status == static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED)) {
        LOG_E(TAG, "FDN reported the lookup failed");
        fetchTimer.invalidate();
        failurePagePending = true;
        return;
    }

    Player* player = playerProvider->getPlayer();
    if (!PlayerProvisionCodec::decode(pkt, *player)) {
        LOG_W(TAG, "Ignoring invalid PlayerProvisionPacket payload");
        return;
    }

    LOG_I(TAG, "Provisioned player: %s", player->getServerPlayerName());
    fetchTimer.invalidate();
    transitionToWelcomeState = true;
}

void FetchUserState::beginFailurePage(PDN* pdn) {
    fetchTimer.invalidate();
    showingFailurePage = true;
    failurePageTimer.setTimer(FETCH_FAILURE_PAGE_TIMEOUT_MS);
    showAssetNotFoundPage(pdn);
}

void FetchUserState::showAssetNotFoundPage(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen();
    pdn->getDisplay()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText("Unable to", 12, 26)
    ->drawText("Locate Asset", 0, 42)
    ->render();
}

void FetchUserState::showLoadingGlyphs(PDN* pdn) { renderLoadingScreen(pdn->getDisplay()); }

bool FetchUserState::transitionToWelcome() const { return transitionToWelcomeState; }

bool FetchUserState::transitionToPinEntry() const { return transitionToPinEntryState; }
