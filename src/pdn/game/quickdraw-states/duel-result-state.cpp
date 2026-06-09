#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"

#define DUEL_RESULT_TAG "DUEL_RESULT"

DuelResult::DuelResult(Player* player, MatchManager* matchManager, ShootoutManager* shootoutManager) : TypedState<PDN>(QuickdrawStateId::DUEL_RESULT) {
    this->player = player;
    this->matchManager = matchManager;
    this->shootoutManager = shootoutManager;
}

DuelResult::~DuelResult() {
    LOG_I(DUEL_RESULT_TAG, "Duel result state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
    this->shootoutManager = nullptr;
}

void DuelResult::onStateMounted(PDN* pdn) {
    LOG_I(DUEL_RESULT_TAG, "Duel result state mounted");

    if (matchManager->isVoided()) {
        LOG_W(DUEL_RESULT_TAG, "Match voided; routing to Idle");
        voided = true;
        matchManager->finalizeMatch();
        pdn->getHaptics()->setIntensity(0);
        pdn->getDisplay()->invalidateScreen()->render();
        return;
    }

    // Shootout matches are venue-local: they route to spectator/eliminated
    // below and never persist, so they must not move lifetime win/loss/streak
    // counters. wonBattle/captured are still set so the result screen renders.
    bool countsTowardCareer = !matchManager->currentMatchIsShootout();

    if(matchManager->didWin()) {
        wonBattle = true;
        if (countsTowardCareer) {
            player->incrementMatchesPlayed();
            player->incrementWins();
            player->incrementStreak();
        }
    } else {
        captured = true;
        if (countsTowardCareer) {
            player->incrementMatchesPlayed();
            player->resetStreak();
            player->incrementLosses();
        }
    }

    pdn->getHaptics()->setIntensity(0);

    matchManager->finalizeMatch();

    pdn->getDisplay()->invalidateScreen()->render();
}

void DuelResult::onStateLoop(PDN* pdn) {
}

void DuelResult::onStateDismounted(PDN* pdn) {
    LOG_I(DUEL_RESULT_TAG, "Duel result state dismounted - Cleaning up");

    LOG_I(DUEL_RESULT_TAG, "State before reset - wonBattle: %d, captured: %d",
             wonBattle, captured);

    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();

    wonBattle = false;
    captured = false;
    voided = false;
    pdn->getLightManager()->stopAnimation();
}

bool DuelResult::transitionToIdleOnVoid() {
    if (!voided) return false;
    // A void while the tournament is still in flight is handled by
    // transitionToShootoutAbortOnVoid (it aborts, then everyone routes out).
    // Everything else falls to Idle: no shootout, or a terminal ENDED/ABORTED
    // tournament. Routing this device to Idle touches no shared state, so it
    // does not clobber the standings other devices are viewing.
    if (shootoutManager && shootoutManager->active()) {
        // ABORTED is routed to the abort screen by the phaseIsAborted transition
        // registered ahead of this one, so only the ENDED terminal phase needs
        // an Idle escape here.
        if (shootoutManager->getPhase() != ShootoutManager::Phase::ENDED) return false;
    }
    return true;
}

bool DuelResult::transitionToShootoutAbortOnVoid() {
    if (!shootoutManager || !voided) return false;
    // Only abort while the tournament is still in flight. ENDED/ABORTED are
    // terminal phases whose UI must not be clobbered by a late-arriving void.
    auto phase = shootoutManager->getPhase();
    if (phase == ShootoutManager::Phase::IDLE
        || phase == ShootoutManager::Phase::ENDED
        || phase == ShootoutManager::Phase::ABORTED) return false;
    LOG_W(DUEL_RESULT_TAG, "Voided shootout duel; aborting tournament");
    shootoutManager->abortTournament();
    return true;
}

bool DuelResult::transitionToWin() {
    if (shootoutManager && shootoutManager->active()) return false;
    if (wonBattle) {
        LOG_I(DUEL_RESULT_TAG, "Transitioning to win state");
    }
    return wonBattle;
}

bool DuelResult::transitionToLose() {
    if (shootoutManager && shootoutManager->active()) return false;
    if (captured) {
        LOG_I(DUEL_RESULT_TAG, "Transitioning to lose state");
    }
    return captured;
}

bool DuelResult::transitionToShootoutSpectator() {
    if (voided) return false;
    if (!shootoutManager || !shootoutManager->active()) return false;
    if (!wonBattle) return false;
    shootoutManager->reportLocalWin();
    return true;
}

bool DuelResult::transitionToShootoutEliminated() {
    // A voided match has no winner or loser; it must never read as an
    // elimination (voided leaves wonBattle=false, which would otherwise fall
    // through to the Eliminated screen even in a terminal ENDED/ABORTED phase).
    if (voided) return false;
    if (!shootoutManager || !shootoutManager->active()) return false;
    return !wonBattle;
}