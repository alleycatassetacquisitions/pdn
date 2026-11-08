#include "apps/player-fetch/player-fetch-states.hpp"
#include <cstring>
#include "alleycat-server/player-provision-codec.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/quickdraw-resources.hpp"

static const char TAG[] = "FetchUserState";
// 2025-01-01T00:00:00Z, the floor the HTTP Date parser accepts.
static constexpr int64_t MIN_PROVISIONED_UNIX_MS = 1735689600000LL;

FetchUserState::FetchUserState(WirelessManager* wirelessManager, PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::FETCH_USER)
    , wirelessManager(wirelessManager)
    , playerProvider(playerProvider) {}

FetchUserState::~FetchUserState() {
    wirelessManager = nullptr;
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
    requestSent = false;
    remoteDeviceCoordinator = pdn->getRemoteDeviceCoordinator();
    wirelessManager->setEspNowPacketHandler(
    PktType::kPlayerProvision,
    [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
        static_cast<FetchUserState*>(ctx)->onProvisionPacket(fromMac, data, dataLen);
    },
    this);
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

    // The FDN may be cabled after the PIN was entered.
    if (!requestSent) {
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
    wirelessManager->clearEspNowPacketHandler(PktType::kPlayerProvision);
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToPinEntryState = false;
    requestSent = false;
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
    PlayerProvisionRequestPacket request{};
    strncpy(request.pin, playerProvider->getRegistrationPin(), sizeof(request.pin) - 1);
    requestSent = wirelessManager->sendEspNowData(fdnMac, PktType::kPlayerProvisionRequest,
                                                  reinterpret_cast<const uint8_t*>(&request),
                                                  sizeof(request)) >= 0;
}

void FetchUserState::onProvisionPacket(const uint8_t* fromMac, const uint8_t* data,
                                       size_t dataLen) {
    if (remoteDeviceCoordinator == nullptr ||
        !remoteDeviceCoordinator->isConnectedPeer(DeviceType::FDN, fromMac)) {
        LOG_W(TAG, "Ignoring PlayerProvisionPacket from non-cabled sender");
        return;
    }
    if (data == nullptr || dataLen != sizeof(PlayerProvisionPacket)) {
        LOG_W(TAG, "Ignoring PlayerProvisionPacket of %zu bytes", dataLen);
        return;
    }

    PlayerProvisionPacket pkt{};
    std::memcpy(&pkt, data, sizeof(pkt));

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

    // A lookup answer without a usable Date header leaves the FDN on its boot epoch; keep ours.
    PlatformClock* clock = SimpleTimer::getPlatformClock();
    if (clock != nullptr && pkt.serverUnixMs >= MIN_PROVISIONED_UNIX_MS) {
        clock->setUnixMilliseconds(pkt.serverUnixMs);
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
