#include "apps/player-fetch/player-fetch-states.hpp"
#include <cstring>
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/firmware-update-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/quickdraw-resources.hpp"
#include "id-generator.hpp"

static const char TAG[] = "FetchUserState";

FetchUserState::FetchUserState(WirelessManager* wirelessManager, PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::FETCH_USER)
    , wirelessManager(wirelessManager)
    , playerProvider(playerProvider) {}

FetchUserState::~FetchUserState() {
    if (wirelessManager != nullptr) {
        wirelessManager->clearEspNowPacketHandler(PktType::kPlayerProvision);
    }
    wirelessManager = nullptr;
    playerProvider = nullptr;
    mountedPdn = nullptr;
}

void FetchUserState::onStateMounted(PDN* pdn) {
    mountedPdn = pdn;
    if (playerProvider != nullptr) {
        playerProvider->clear();
    }
    sinceMount.setTimer(0);
    firmwareUpdateManager = pdn->getFirmwareUpdateManager();
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(true);
    }

    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToFirmwareUpdateState = false;
    failurePageTimer.invalidate();

    WirelessManager* wm = wirelessManager != nullptr ? wirelessManager : pdn->getWirelessManager();
    if (wm != nullptr) {
        wm->setEspNowPacketHandler(
        PktType::kPlayerProvision,
        [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<FetchUserState*>(ctx)->onProvisionPacket(fromMac, data, dataLen);
        },
        this);
    }

    showLoadingGlyphs(pdn);
}

void FetchUserState::onStateLoop(PDN* pdn) {
    const unsigned long heldMs = pdn->getSecondaryButton()->longPressedMillis();
    if (heldMs >= PLAYER_FETCH_FIRMWARE_HOLD_MS && heldMs <= sinceMount.getElapsedTime()) {
        transitionToFirmwareUpdateState = true;
    }

    if (failurePagePending) {
        beginFailurePage(pdn);
        failurePagePending = false;
    }

    if (showingFailurePage) {
        failurePageTimer.updateTime();
        if (failurePageTimer.expired()) {
            showingFailurePage = false;
            failurePageTimer.invalidate();
            showLoadingGlyphs(pdn);
        }
        return;
    }

    if (PlatformClock* clock = SimpleTimer::getPlatformClock()) {
        if (clock->milliseconds() % 50 == 0) {
            showLoadingGlyphs(pdn);
        }
    }
}

void FetchUserState::onStateDismounted(PDN* pdn) {
    WirelessManager* wm = wirelessManager != nullptr ? wirelessManager : pdn->getWirelessManager();
    if (wm != nullptr) {
        wm->clearEspNowPacketHandler(PktType::kPlayerProvision);
    }
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToFirmwareUpdateState = false;
    failurePageTimer.invalidate();
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(false);
    }
    mountedPdn = nullptr;
}

bool FetchUserState::isConnectedFdnMac(const uint8_t* mac) const {
    if (mountedPdn == nullptr || mac == nullptr) {
        return false;
    }
    RemoteDeviceCoordinator* rdc = mountedPdn->getRemoteDeviceCoordinator();
    if (rdc == nullptr) {
        return false;
    }
    for (SerialIdentifier jack : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK,
                                  SerialIdentifier::INPUT_JACK_SECONDARY}) {
        if (rdc->getJackStatus(jack) == JackStatus::CONNECTED &&
            rdc->getPeerDeviceType(jack) == DeviceType::FDN) {
            const uint8_t* peer = rdc->getPeerMac(jack);
            if (peer != nullptr && std::memcmp(peer, mac, 6) == 0) {
                return true;
            }
        }
    }
    return false;
}

void FetchUserState::onProvisionPacket(const uint8_t* fromMac, const uint8_t* data,
                                       size_t dataLen) {
    if (!isConnectedFdnMac(fromMac)) {
        LOG_W(TAG, "Ignoring PlayerProvisionPacket from non-cabled sender");
        return;
    }
    if (data == nullptr || dataLen < sizeof(PlayerProvisionPacket)) {
        LOG_W(TAG, "Ignoring truncated PlayerProvisionPacket (%zu bytes)", dataLen);
        return;
    }

    PlayerProvisionPacket pkt{};
    std::memcpy(&pkt, data, sizeof(pkt));

    if (pkt.status == static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED)) {
        LOG_W(TAG, "FDN reported LOOKUP_FAILED");
        failurePagePending = true;
        return;
    }

    Player decodedPlayer{};
    if (!FdnProvisioningManager::applyProvisionPacket(pkt, decodedPlayer)) {
        LOG_W(TAG, "Ignoring invalid PlayerProvisionPacket payload");
        return;
    }

    if (playerProvider != nullptr && playerProvider->getPlayer() != nullptr) {
        Player* player = playerProvider->getPlayer();
        FdnProvisioningManager::applyProvisionPacket(pkt, *player);
        uint32_t seed = 0;
        std::memcpy(&seed, player->getServerPlayerId().bytes, sizeof(seed));
        srand(static_cast<unsigned int>(seed));
        player->getSymbol()->setRandomSymbol();
        showingFailurePage = false;
        failurePagePending = false;
        failurePageTimer.invalidate();
        transitionToWelcomeState = true;
    }
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

bool FetchUserState::transitionToFirmwareUpdate() const { return transitionToFirmwareUpdateState; }
