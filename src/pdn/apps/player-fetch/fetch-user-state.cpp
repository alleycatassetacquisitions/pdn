#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "alleycat-server/player-api.hpp"
#include "device/drivers/logger.hpp"

static const char TAG[] = "FetchUserState";

FetchUserState::FetchUserState(WirelessManager* wirelessManager, PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::FETCH_USER)
    , wirelessManager(wirelessManager)
    , playerProvider(playerProvider) {}

FetchUserState::~FetchUserState() {
    wirelessManager = nullptr;
    playerProvider = nullptr;
}

void FetchUserState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - fetching device player for PIN %s",
          playerProvider->getRegistrationPin());
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToPinEntryState = false;
    showLoadingGlyphs(pdn);
    fetchDevicePlayer();
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

    fetchTimer.updateTime();
    if (fetchTimer.expired()) {
        LOG_W(TAG, "Device player fetch timed out");
        beginFailurePage(pdn);
    } else if (fetchTimer.isRunning()) {
        if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            showLoadingGlyphs(pdn);
        }
    }
}

void FetchUserState::onStateDismounted(PDN* pdn) {
    pdn->getWirelessManager()->enablePeerCommsMode();
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToPinEntryState = false;
    fetchTimer.invalidate();
    failurePageTimer.invalidate();
}

void FetchUserState::fetchDevicePlayer() {
    fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT_MS);
    PlayerApi::getDevicePlayerByPin(
    wirelessManager, playerProvider->getRegistrationPin(),
    [this](const PlayerV2& fetchedPlayer) {
        LOG_I(TAG, "Fetched device player: %s", fetchedPlayer.name);
        playerProvider->setPlayer(fetchedPlayer);
        fetchTimer.invalidate();
        transitionToWelcomeState = true;
    },
    [this](const WirelessErrorInfo& error) {
        LOG_E(TAG, "Device player fetch failed: %s (code: %d)", error.message.c_str(),
              static_cast<int>(error.code));
        fetchTimer.invalidate();
        failurePagePending = true;
    });
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
