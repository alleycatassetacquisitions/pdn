#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/drivers/logger.hpp"

static const char TAG[] = "FetchUserState";

FetchUserState::FetchUserState(PlayerProvider* playerProvider, BandRegistration* bandRegistration)
    : TypedState<PDN>(PlayerFetchStateId::FETCH_USER)
    , playerProvider(playerProvider)
    , bandRegistration(bandRegistration) {}

FetchUserState::~FetchUserState() {
    playerProvider = nullptr;
    bandRegistration = nullptr;
}

void FetchUserState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - waiting for the rest of the band registration");
    showingFailurePage = false;
    failurePagePending = false;
    transitionToWelcomeState = false;
    transitionToAwaitBandState = false;
    showLoadingGlyphs(pdn);
    fetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT_MS);
    // The piece that led here, or the whole pair, may already be in.
    bandRegistration->setOnAnswerChanged([this]() { answerChanged = true; });
    answerChanged = true;
}

void FetchUserState::onStateLoop(PDN* pdn) {
    if (answerChanged) {
        answerChanged = false;
        takeAnswer();
    }

    if (failurePagePending) {
        beginFailurePage(pdn);
        failurePagePending = false;
    }

    if (showingFailurePage) {
        failurePageTimer.updateTime();
        if (failurePageTimer.expired()) {
            transitionToAwaitBandState = true;
        }
        return;
    }

    fetchTimer.updateTime();
    if (fetchTimer.expired()) {
        LOG_W(TAG, "Band registration timed out with only part of it delivered");
        beginFailurePage(pdn);
    } else if (fetchTimer.isRunning()) {
        if (SimpleTimer::getPlatformClock()->milliseconds() % 50 == 0) {
            showLoadingGlyphs(pdn);
        }
    }
}

void FetchUserState::onStateDismounted(PDN* pdn) {
    bandRegistration->setOnAnswerChanged(nullptr);
    // The key stays only with the profile it came with.
    bandRegistration->close(transitionToWelcomeState);
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    showingFailurePage = false;
    failurePagePending = false;
    answerChanged = false;
    transitionToWelcomeState = false;
    transitionToAwaitBandState = false;
    fetchTimer.invalidate();
    failurePageTimer.invalidate();
}

void FetchUserState::takeAnswer() {
    if (bandRegistration->getAnswer() == BandRegistration::Answer::FOUND) {
        onPlayerFound(*bandRegistration->getFoundPlayer());
    } else if (bandRegistration->getAnswer() == BandRegistration::Answer::FAILED) {
        onPlayerLookupFailed();
    }
}

void FetchUserState::onPlayerFound(const Player& found) {
    if (!fetchTimer.isRunning()) {
        LOG_W(TAG, "Player found with no lookup in flight - ignored");
        return;
    }
    LOG_I(TAG, "Found player: %s", found.getServerPlayerName());
    Player* player = playerProvider->getPlayer();
    player->applyRegistrationProfile(found.getServerPlayerId(), found.getServerPlayerName(),
                                     found.getRegistrationTeam(),
                                     found.getRegistrationAffiliations());
    player->applyRngSeedFromServerPlayerId();
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
    // Whatever part of the pair arrived goes now, not when the page ends.
    bandRegistration->close(false);
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

bool FetchUserState::transitionToAwaitBand() const { return transitionToAwaitBandState; }
