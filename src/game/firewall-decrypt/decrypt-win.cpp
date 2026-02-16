#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/ui-clean-minimal.hpp"
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

    auto display = PDN->getDisplay();
    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // Outer frame
    display->drawFrame(0, 0, 128, 64);

    // Title block
    BoldRetroUI::drawBorderedFrame(display, 20, 14, 88, 22);
    display->drawText("ACCESS", 38, 24);
    display->drawText("GRANTED", 35, 32);

    // Subtitle
    BoldRetroUI::drawCenteredText(display, "FIREWALL DOWN", 46);

    // Score
    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "Score: %d", session.score);
    BoldRetroUI::drawCenteredText(display, scoreStr, 58);

    display->render();

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
