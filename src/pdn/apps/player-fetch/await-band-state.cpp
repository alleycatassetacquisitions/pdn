#include "apps/player-fetch/player-fetch-states.hpp"
#include <cstring>
#include "alleycat-server/player-provision-codec.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/firmware-update-manager.hpp"
#include "device/remote-device-coordinator.hpp"

static const char TAG[] = "AwaitBandState";

static_assert(RECORD_KEY_SIZE == APPLICATION_PAYLOAD_LEN,
              "the record key is the whole application frame payload");

AwaitBandState::AwaitBandState(WirelessManager* wirelessManager, PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::AWAIT_BAND)
    , provisionChannel(wirelessManager, PktType::kPlayerProvision)
    , playerProvider(playerProvider) {}

void AwaitBandState::onStateMounted(PDN* pdn) {
    // Only a key and profile accepted on this screen may register the PDN.
    playerProvider->clear();
    sinceMount.setTimer(0);
    firmwareUpdateManager = pdn->getFirmwareUpdateManager();
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(true);
    }

    remoteDeviceCoordinator = pdn->getRemoteDeviceCoordinator();
    remoteDeviceCoordinator->setOnApplicationFrame(
    [this](SerialIdentifier jack, const ApplicationPayload& payload) {
        onApplicationFrame(jack, payload);
    },
    this);
    remoteDeviceCoordinator->setOnJackChange(
    [this](SerialIdentifier jack, bool connected) { onJackChange(jack, connected); }, this);
    provisionChannel.onReceive([this](const uint8_t* fromMac, const PlayerProvisionPacket& pkt) {
        onProvisionPacket(fromMac, pkt);
    });

    waitingScreen = getWaitingScreenForCable();
    renderWaitingScreen(pdn);
}

void AwaitBandState::onStateLoop(PDN* pdn) {
    if (isHoldComplete(pdn->getSecondaryButton()->longPressedMillis(),
                       PLAYER_FETCH_FIRMWARE_HOLD_MS)) {
        transitionToFirmwareUpdateState = true;
    }
    if (isHoldComplete(pdn->getPrimaryButton()->longPressedMillis(), PLAYER_FETCH_DEMO_HOLD_MS)) {
        transitionToChooseTeamState = true;
    }

    if (failurePageIsShowing) {
        if (failurePageIsDirty) {
            renderFailurePage(pdn);
        }
        failurePageTimer.updateTime();
        if (failurePageTimer.expired()) {
            dismissFailurePage(pdn);
        }
    }

    if (attemptFdn.has_value() && isAttemptOpen()) {
        attemptTimer.updateTime();
        if (attemptTimer.expired()) {
            LOG_E(TAG, "Registration timed out: %s",
                  heldProfile.has_value() ? "no record key arrived" : "no profile arrived");
            failAttempt();
            renderFailurePage(pdn);
        }
    }

    // The failure page stays up for its full time; the waiting screen it returns to
    // is drawn from the cable and turn at that point.
    if (failurePageIsShowing) return;
    const WaitingScreen screen = getWaitingScreenForCable();
    if (screen != waitingScreen) {
        waitingScreen = screen;
        renderWaitingScreen(pdn);
    }
}

void AwaitBandState::onStateDismounted(PDN* pdn) {
    provisionChannel.onReceive(nullptr);
    remoteDeviceCoordinator->setOnApplicationFrame(nullptr, this);
    remoteDeviceCoordinator->setOnJackChange(nullptr, this);
    if (!transitionToWelcomeState) {
        endAttempt();
    }
    attemptFdn.reset();
    failurePageTimer.invalidate();
    failurePageIsShowing = false;
    failurePageIsDirty = false;
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(false);
    }
    waitingScreen = WaitingScreen::CABLE_TO_FDN;
    transitionToWelcomeState = false;
    transitionToChooseTeamState = false;
    transitionToFirmwareUpdateState = false;
}

bool AwaitBandState::isAttemptOpen() const {
    return !transitionToWelcomeState && !failurePageIsShowing;
}

bool AwaitBandState::isHoldComplete(unsigned long heldMs, unsigned long holdMs) {
    // The pressed time counts from press-down, so a hold carried in from the last
    // screen is longer than this one has been up and does not count.
    return heldMs >= holdMs && heldMs <= sinceMount.getElapsedTime();
}

bool AwaitBandState::findFdnJack(const uint8_t* fdnMac, SerialIdentifier& jack) const {
    for (SerialIdentifier candidate : RemoteDeviceCoordinator::HELLO_JACKS) {
        if (remoteDeviceCoordinator->isConnectedPeer(candidate, DeviceType::FDN, fdnMac)) {
            jack = candidate;
            return true;
        }
    }
    return false;
}

void AwaitBandState::beginAttempt(const uint8_t* fdnMac, SerialIdentifier jack) {
    attemptFdn.emplace();
    memcpy(attemptFdn->mac.data(), fdnMac, attemptFdn->mac.size());
    attemptFdn->jack = jack;
    attemptTimer.setTimer(REGISTRATION_ATTEMPT_TIMEOUT_MS);
}

void AwaitBandState::onProvisionPacket(const uint8_t* fromMac, const PlayerProvisionPacket& pkt) {
    if (!isAttemptOpen()) return;
    if (attemptFdn.has_value()) {
        if (memcmp(fromMac, attemptFdn->mac.data(), attemptFdn->mac.size()) != 0) {
            LOG_W(TAG, "Ignoring PlayerProvisionPacket from an FDN other than the attempt's");
            return;
        }
    } else {
        SerialIdentifier jack = SerialIdentifier::INPUT_JACK;
        if (!findFdnJack(fromMac, jack)) {
            LOG_W(TAG, "Ignoring PlayerProvisionPacket from a sender not Connected on a jack");
            return;
        }
        beginAttempt(fromMac, jack);
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

void AwaitBandState::onApplicationFrame(SerialIdentifier jack, const ApplicationPayload& payload) {
    if (!isAttemptOpen()) return;
    // A frame carries no source, so the jack's Connected peer is all that says who wrote it.
    if (attemptFdn.has_value()) {
        if (jack != attemptFdn->jack || !remoteDeviceCoordinator->isConnectedPeer(
                                        jack, DeviceType::FDN, attemptFdn->mac.data())) {
            LOG_W(TAG, "Ignoring application frame from a jack without the attempt's FDN");
            return;
        }
    } else {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(jack);
        if (peerMac == nullptr ||
            !remoteDeviceCoordinator->isConnectedPeer(jack, DeviceType::FDN, peerMac)) {
            LOG_W(TAG, "Ignoring application frame from a jack without a Connected FDN");
            return;
        }
        beginAttempt(peerMac, jack);
    }

    playerProvider->getRecordKey()->set(payload.data());
    completeRegistrationIfReady();
}

void AwaitBandState::onJackChange(SerialIdentifier jack, bool connected) {
    if (connected || !isAttemptOpen() || !attemptFdn.has_value() || jack != attemptFdn->jack) {
        return;
    }
    // A profile is taken only from a cabled FDN, so an attempt whose FDN has left
    // cannot finish; end it now rather than at the time limit.
    LOG_E(TAG, "FDN disconnected before the registration completed");
    failAttempt();
}

void AwaitBandState::completeRegistrationIfReady() {
    if (!heldProfile.has_value() || !playerProvider->getRecordKey()->isHeld()) return;

    Player* player = playerProvider->getPlayer();
    if (!PlayerProvisionCodec::decode(*heldProfile, *player)) {
        LOG_W(TAG, "Ignoring invalid PlayerProvisionPacket payload");
        heldProfile.reset();
        return;
    }

    LOG_I(TAG, "Provisioned player: %s", player->getServerPlayerName());
    player->applyRngSeedFromServerPlayerId();
    heldProfile.reset();
    attemptTimer.invalidate();
    transitionToWelcomeState = true;
}

void AwaitBandState::endAttempt() {
    attemptTimer.invalidate();
    heldProfile.reset();
    playerProvider->getRecordKey()->clear();
}

void AwaitBandState::failAttempt() {
    endAttempt();
    // Failures land from radio and jack callbacks, which have no PDN to draw with;
    // the page is drawn on the next loop tick, but pieces are refused from now.
    failurePageIsShowing = true;
    failurePageIsDirty = true;
    failurePageTimer.setTimer(REGISTRATION_FAILURE_PAGE_TIMEOUT_MS);
}

void AwaitBandState::renderFailurePage(PDN* pdn) {
    pdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText("Unable to", 12, 26)
    ->drawText("Locate Asset", 0, 42)
    ->render();
    failurePageIsDirty = false;
}

void AwaitBandState::dismissFailurePage(PDN* pdn) {
    failurePageTimer.invalidate();
    failurePageIsShowing = false;
    attemptFdn.reset();
    playerProvider->clear();
    waitingScreen = getWaitingScreenForCable();
    renderWaitingScreen(pdn);
}

AwaitBandState::WaitingScreen AwaitBandState::getWaitingScreenForCable() const {
    WaitingScreen screen = WaitingScreen::CABLE_TO_FDN;
    for (SerialIdentifier jack : RemoteDeviceCoordinator::HELLO_JACKS) {
        if (remoteDeviceCoordinator->getJackStatus(jack) != JackStatus::CONNECTED ||
            remoteDeviceCoordinator->getPeerDeviceType(jack) != DeviceType::FDN ||
            remoteDeviceCoordinator->getPeerMac(jack) == nullptr) {
            continue;
        }
        screen = WaitingScreen::PLEASE_WAIT;
        // An older FDN's context carries no turn and reads NOT_SENT: never "Tap band".
        size_t length = 0;
        const uint8_t* bytes = remoteDeviceCoordinator->getPeerProfile(jack, length);
        if (bytes == nullptr || length < sizeof(FdnProfile)) continue;
        FdnProfile fdn{};
        memcpy(&fdn, bytes, sizeof(fdn));
        if (fdn.registrationTurn == RegistrationTurn::OPEN) return WaitingScreen::TAP_BAND;
    }
    return screen;
}

void AwaitBandState::renderWaitingScreen(PDN* pdn) const {
    pdn->getDisplay()->invalidateScreen()->setGlyphMode(FontMode::TEXT);
    if (waitingScreen == WaitingScreen::TAP_BAND) {
        pdn->getDisplay()->drawText("Tap band", 16, 26)->drawText("at FDN", 28, 42);
    } else if (waitingScreen == WaitingScreen::PLEASE_WAIT) {
        pdn->getDisplay()->drawText("Please", 28, 26)->drawText("wait", 40, 42);
    } else {
        pdn->getDisplay()->drawText("Cable to", 16, 26)->drawText("FDN", 44, 42);
    }
    pdn->getDisplay()->render();
}

bool AwaitBandState::transitionToWelcome() const { return transitionToWelcomeState; }

bool AwaitBandState::transitionToChooseTeam() const { return transitionToChooseTeamState; }

bool AwaitBandState::transitionToFirmwareUpdate() const { return transitionToFirmwareUpdateState; }
