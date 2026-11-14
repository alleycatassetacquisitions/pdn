#include "apps/player-fetch/player-fetch-states.hpp"
#include <cstring>
#include "alleycat-server/player-provision-codec.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/quickdraw-resources.hpp"

static const char TAG[] = "FetchUserState";

static_assert(RECORD_KEY_SIZE == APPLICATION_PAYLOAD_LEN,
              "the record key is the whole application frame payload");

FetchUserState::FetchUserState(PdnProvisioningClient* provisioningClient,
                               PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::FETCH_USER)
    , provisioningClient(provisioningClient)
    , playerProvider(playerProvider) {}

void FetchUserState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - requesting provisioning for PIN %s",
          playerProvider->getRegistrationPin());
    remoteDeviceCoordinator = pdn->getRemoteDeviceCoordinator();
    // Only a key accepted during this attempt may complete it.
    playerProvider->getRecordKey()->clear();
    remoteDeviceCoordinator->setOnApplicationFrame(
    [this](SerialIdentifier jack, const ApplicationPayload& payload) {
        onApplicationFrame(jack, payload);
    },
    this);
    remoteDeviceCoordinator->setOnJackChange(
    [this](SerialIdentifier jack, bool connected) { onJackChange(jack, connected); }, this);
    provisioningClient->setOnReply(
    [this](const uint8_t* fromMac, const PlayerProvisionPacket& pkt) {
        onProvisionPacket(fromMac, pkt);
    });
    showLoadingGlyphs(pdn);
    fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT_MS);
    requestProvisionFromCabledFdn();
}

void FetchUserState::onStateLoop(PDN* pdn) {
    if (isAttemptLive()) {
        // The FDN may be cabled after the PIN was entered, or deaf on Wi-Fi when it was sent.
        if (!requestedFdn.has_value() || provisioningClient->isRequestAbandoned()) {
            requestProvisionFromCabledFdn();
        }

        fetchTimer.updateTime();
        if (fetchTimer.expired()) {
            const char* missing = "neither record key nor profile arrived";
            if (heldProfile.has_value()) {
                missing = "no record key arrived";
            } else if (playerProvider->getRecordKey()->isHeld()) {
                missing = "no profile arrived";
            }
            LOG_E(TAG, "Provisioning timed out: %s", missing);
            failAttempt();
        } else if (fetchTimer.isRunning()) {
            if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
                showLoadingGlyphs(pdn);
            }
        }
    }

    if (failurePagePending) {
        beginFailurePage(pdn);
        failurePagePending = false;
    }

    if (showingFailurePage) {
        failurePageTimer.updateTime();
        if (failurePageTimer.expired()) {
            transitionToPinEntryState = true;
        }
    }
}

void FetchUserState::onStateDismounted(PDN* pdn) {
    provisioningClient->setOnReply(nullptr);
    remoteDeviceCoordinator->setOnApplicationFrame(nullptr, this);
    remoteDeviceCoordinator->setOnJackChange(nullptr, this);
    if (!transitionToWelcomeState) {
        endAttemptWithoutProfile();
    }
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToPinEntryState = false;
    requestedFdn.reset();
    failurePageTimer.invalidate();
}

bool FetchUserState::isAttemptLive() const {
    return !showingFailurePage && !failurePagePending && !transitionToWelcomeState;
}

void FetchUserState::requestProvisionFromCabledFdn() {
    const uint8_t* fdnMac = remoteDeviceCoordinator->getConnectedPeerMac(DeviceType::FDN);
    if (fdnMac == nullptr) {
        return;
    }
    provisioningClient->sendRequest(fdnMac, playerProvider->getRegistrationPin());
    // The reply window runs from the first request, which can go out after the PIN was entered.
    if (!requestedFdn.has_value()) {
        fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT_MS);
    }
    requestedFdn.emplace();
    memcpy(requestedFdn->mac.data(), fdnMac, requestedFdn->mac.size());
    for (SerialIdentifier jack : RemoteDeviceCoordinator::HELLO_JACKS) {
        if (remoteDeviceCoordinator->isConnectedPeer(jack, DeviceType::FDN, fdnMac)) {
            requestedFdn->jack = jack;
            break;
        }
    }
}

void FetchUserState::onProvisionPacket(const uint8_t* fromMac, const PlayerProvisionPacket& pkt) {
    if (!isAttemptLive()) return;
    if (!remoteDeviceCoordinator->isConnectedPeer(DeviceType::FDN, fromMac)) {
        LOG_W(TAG, "Ignoring PlayerProvisionPacket from non-cabled sender");
        return;
    }

    if (strncmp(pkt.pin, playerProvider->getRegistrationPin(), sizeof(pkt.pin) - 1) != 0) {
        LOG_W(TAG, "Ignoring PlayerProvisionPacket for another PIN");
        return;
    }

    if (pkt.status == static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED)) {
        LOG_E(TAG, "FDN reported the lookup failed");
        failAttempt();
        return;
    }

    // The key crosses the jack while the profile crosses the radio, so either
    // can land first; the profile waits here until both are in hand.
    heldProfile = pkt;
    completeRegistrationIfReady();
}

void FetchUserState::onApplicationFrame(SerialIdentifier jack, const ApplicationPayload& payload) {
    if (!isAttemptLive()) return;
    // A frame carries no source, so the jack's Connected peer is all that says who wrote it.
    if (!requestedFdn.has_value() || !remoteDeviceCoordinator->isConnectedPeer(
                                     jack, DeviceType::FDN, requestedFdn->mac.data())) {
        LOG_W(TAG, "Ignoring application frame from a jack without the FDN that was sent the PIN");
        return;
    }

    playerProvider->getRecordKey()->set(payload.data());
    completeRegistrationIfReady();
}

void FetchUserState::onJackChange(SerialIdentifier jack, bool connected) {
    if (connected || !isAttemptLive() || !requestedFdn.has_value() || jack != requestedFdn->jack) {
        return;
    }
    // A profile is taken only from a cabled FDN, so an attempt whose FDN has left
    // cannot finish; end it now rather than at the time limit.
    LOG_E(TAG, "FDN disconnected before the registration completed");
    failAttempt();
}

void FetchUserState::completeRegistrationIfReady() {
    if (!heldProfile.has_value() || !playerProvider->getRecordKey()->isHeld()) return;

    Player* player = playerProvider->getPlayer();
    if (!PlayerProvisionCodec::decode(*heldProfile, *player)) {
        LOG_W(TAG, "Ignoring invalid PlayerProvisionPacket payload");
        heldProfile.reset();
        return;
    }

    LOG_I(TAG, "Provisioned player: %s", player->getServerPlayerName());
    heldProfile.reset();
    fetchTimer.invalidate();
    transitionToWelcomeState = true;
}

void FetchUserState::endAttemptWithoutProfile() {
    fetchTimer.invalidate();
    heldProfile.reset();
    playerProvider->getRecordKey()->clear();
}

void FetchUserState::failAttempt() {
    endAttemptWithoutProfile();
    failurePagePending = true;
}

void FetchUserState::beginFailurePage(PDN* pdn) {
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
