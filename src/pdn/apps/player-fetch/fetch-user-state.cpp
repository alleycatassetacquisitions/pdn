#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/drivers/logger.hpp"

static const char TAG[] = "FetchUserState";

FetchUserState::FetchUserState(PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::FETCH_USER)
    , playerProvider(playerProvider) {}

FetchUserState::~FetchUserState() { playerProvider = nullptr; }

void FetchUserState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - waiting for the FDN lookup of PIN %s",
          playerProvider->getRegistrationPin());
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToPinEntryState = false;
    showLoadingGlyphs(pdn);
    fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT_MS);
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
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToPinEntryState = false;
    fetchTimer.invalidate();
    failurePageTimer.invalidate();
}

void FetchUserState::onPlayerFound() {
    if (!fetchTimer.isRunning()) {
        LOG_W(TAG, "Player found with no lookup in flight - ignored");
        return;
    }
    LOG_I(TAG, "Found player: %s", playerProvider->getPlayer()->getServerPlayerName());
    fetchTimer.invalidate();
    transitionToWelcomeState = true;
}

void FetchUserState::onPlayerLookupFailed() {
    if (!fetchTimer.isRunning()) {
        LOG_W(TAG, "Lookup failure with no lookup in flight - ignored");
        return;
    }
    LOG_E(TAG, "Player lookup failed");
    fetchTimer.invalidate();
    failurePagePending = true;
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
