#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "CipherPathShow";

CipherPathShow::CipherPathShow(CipherPath* game) : State(CIPHER_SHOW) {
    this->game = game;
}

CipherPathShow::~CipherPathShow() {
    game = nullptr;
}

void CipherPathShow::onStateMounted(Device* PDN) {
    transitionToGameplayState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Generate cipher for this round
    game->generateCipher();

    // Reset per-round state
    session.playerPosition = 0;
    session.movesUsed = 0;

    LOG_I(TAG, "Round %d of %d", session.currentRound + 1, config.rounds);

    // Display round info
    std::string roundText = "Round " + std::to_string(session.currentRound + 1)
                          + " of " + std::to_string(config.rounds);
    std::string moveText = "Moves: 0/" + std::to_string(config.moveBudget);

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(roundText.c_str(), 10, 20)
        ->drawText(moveText.c_str(), 10, 45);
    PDN->getDisplay()->render();

    // Haptic pulse feedback
    PDN->getHaptics()->setIntensity(100);

    // Start show timer
    showTimer.setTimer(SHOW_DURATION_MS);
}

void CipherPathShow::onStateLoop(Device* PDN) {
    if (showTimer.expired()) {
        PDN->getHaptics()->off();
        transitionToGameplayState = true;
    }
}

void CipherPathShow::onStateDismounted(Device* PDN) {
    showTimer.invalidate();
    transitionToGameplayState = false;
    PDN->getHaptics()->off();
}

bool CipherPathShow::transitionToGameplay() {
    return transitionToGameplayState;
}
