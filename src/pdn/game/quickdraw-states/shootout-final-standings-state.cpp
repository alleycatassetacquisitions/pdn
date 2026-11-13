#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include <cstdio>
#include <cstring>

ShootoutFinalStandings::ShootoutFinalStandings(const GameContext& ctx)
    : TypedState<PDN>(SHOOTOUT_FINAL_STANDINGS)
    , shootout_(ctx.shootoutManager)
    , remoteDeviceCoordinator(ctx.remoteDeviceCoordinator) {}

ShootoutFinalStandings::~ShootoutFinalStandings() {
    if (remoteDeviceCoordinator) remoteDeviceCoordinator->setOnRingMembershipChange(nullptr, this);
}

void ShootoutFinalStandings::onStateMounted(PDN* pdn) {
    pdn->getLightManager()->stopAnimation();
    auto winner = shootout_->getTournamentWinner();
    const uint8_t* selfMac = pdn->getWirelessManager()->getMacAddress();
    bool iWon = selfMac && memcmp(winner.data(), selfMac, 6) == 0;
    std::string winnerName = shootout_->getNameForMac(winner.data());
    const char* header = iWon ? "VICTORY" : "DEFEAT";
    auto* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText(header, 20);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("winner:", 40);
    d->drawCenteredText(winnerName.c_str(), 55);
    d->render();

    if (remoteDeviceCoordinator == nullptr) return;
    remoteDeviceCoordinator->setOnRingMembershipChange(
    [this](bool inRing) { transitionToSleepState = !inRing; }, this);
    // The event reports flips only, so a ring that opened before this mount is read here.
    transitionToSleepState = !remoteDeviceCoordinator->isInRing();
}

void ShootoutFinalStandings::onStateDismounted(PDN* pdn) {
    if (remoteDeviceCoordinator) remoteDeviceCoordinator->setOnRingMembershipChange(nullptr, this);
    // Held until the screen is done with them: the winner's MAC and the name
    // table this screen reads outlive the final match, and shouldEnterProposal
    // refuses any phase but IDLE, so the next ring cannot propose until this runs.
    if (shootout_) shootout_->resetToIdle();
    transitionToSleepState = false;
}

bool ShootoutFinalStandings::transitionToSleep() const { return transitionToSleepState; }
