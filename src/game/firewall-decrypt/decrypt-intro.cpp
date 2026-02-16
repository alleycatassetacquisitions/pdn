#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/ui-clean-minimal.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "DecryptIntro";

DecryptIntro::DecryptIntro(FirewallDecrypt* game) : State(DECRYPT_INTRO) {
    this->game = game;
}

DecryptIntro::~DecryptIntro() {
    game = nullptr;
}

void DecryptIntro::onStateMounted(Device* PDN) {
    transitionToScanState = false;

    LOG_I(TAG, "Firewall Decrypt intro");

    // Reset game state for fresh play
    game->resetGame();

    PlatformClock* clock = SimpleTimer::getPlatformClock();
    game->setStartTime(clock != nullptr ? clock->milliseconds() : 0);
    game->seedRng(game->getConfig().rngSeed);
    game->setupRound();

    auto display = PDN->getDisplay();
    display->invalidateScreen();
    display->setGlyphMode(FontMode::TEXT);

    // Outer border frame (firewall terminal window)
    display->drawFrame(0, 0, 128, 64);

    // Title block with double border
    BoldRetroUI::drawBorderedFrame(display, 20, 14, 88, 22);
    display->drawText("FIREWALL", 32, 26);
    display->drawText("DECRYPT", 35, 34);

    // Subtitle
    BoldRetroUI::drawCenteredText(display, ">> BREACH MODE <<", 46);

    // Instruction
    display->drawText("FIND THE KEY", 22, 58);

    display->render();

    // Green idle animation
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = FIREWALL_DECRYPT_IDLE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    introTimer.setTimer(INTRO_DURATION_MS);
}

void DecryptIntro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToScanState = true;
    }
}

void DecryptIntro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToScanState = false;
}

bool DecryptIntro::transitionToScan() {
    return transitionToScanState;
}
