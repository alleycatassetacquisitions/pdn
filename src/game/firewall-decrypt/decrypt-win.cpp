#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "device/drivers/logger.hpp"
#include <cstdio>
#include <string>

static const char* TAG = "DecryptWin";

DecryptWin::DecryptWin(FirewallDecrypt* game) : State(DECRYPT_WIN) {
    this->game = game;
}

DecryptWin::~DecryptWin() {
    game = nullptr;
}

void DecryptWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    // Determine if this was hard mode
    bool isHard = (game->getConfig().numCandidates >= 10 &&
                   game->getConfig().numRounds >= 4);

    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = session.score;
    winOutcome.hardMode = isHard;
    game->setOutcome(winOutcome);

    LOG_I(TAG, "DECRYPTED! Score: %d, Hard: %s",
          session.score, isHard ? "yes" : "no");

    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("DECRYPTED!", 15, 25);

    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "Score: %d", session.score);
    PDN->getDisplay()->drawText(scoreStr, 30, 50);
    PDN->getDisplay()->render();

    // Green LED sweep
    AnimationConfig config;
    config.type = AnimationType::VERTICAL_CHASE;
    config.speed = 5;
    config.curve = EaseCurve::EASE_IN_OUT;
    config.initialState = FIREWALL_DECRYPT_WIN_STATE;
    config.loopDelayMs = 500;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void DecryptWin::onStateLoop(Device* PDN) {
    if (winTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void DecryptWin::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool DecryptWin::transitionToIntro() {
    return transitionToIntroState;
}

bool DecryptWin::isTerminalState() const {
    return game->getConfig().managedMode;
}
