#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "CipherPathLose";

CipherPathLose::CipherPathLose(CipherPath* game) : State(CIPHER_LOSE) {
    this->game = game;
}

CipherPathLose::~CipherPathLose() {
    game = nullptr;
}

void CipherPathLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    LOG_I(TAG, "CIRCUIT BREAK (score=%d)", session.score);

    // Initial spark animation: full screen flash
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setDrawColor(1);
    PDN->getDisplay()->drawBox(0, 0, 128, 64);  // Full white flash
    PDN->getDisplay()->render();

    // Lose LED animation
    AnimationConfig animConfig;
    animConfig.type = AnimationType::IDLE;
    animConfig.speed = 12;
    animConfig.curve = EaseCurve::LINEAR;
    animConfig.initialState = CIPHER_PATH_LOSE_STATE;
    animConfig.loopDelayMs = 0;
    animConfig.loop = true;
    PDN->getLightManager()->startAnimation(animConfig);

    // Full haptic surge (electrical shock effect)
    PDN->getHaptics()->setIntensity(255);

    loseTimer.setTimer(LOSE_DISPLAY_MS);
    sparkTimer.setTimer(SPARK_FLASH_MS);
    sparkFlashCount = 0;
}

void CipherPathLose::onStateLoop(Device* PDN) {
    // Spark flash animation (3 flashes total)
    if (sparkFlashCount < 3 && sparkTimer.expired()) {
        sparkFlashCount++;

        // Toggle screen inversion for spark effect
        if (sparkFlashCount % 2 == 1) {
            // Flash: white screen
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->setDrawColor(1);
            PDN->getDisplay()->drawBox(0, 0, 128, 64);
            PDN->getDisplay()->render();
        } else {
            // Back to text display
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
                ->drawText("CIRCUIT", 20, 20)
                ->drawText("BREAK", 30, 40);
            PDN->getDisplay()->render();
        }

        sparkTimer.setTimer(SPARK_FLASH_MS);
    }

    // Final text display after sparks
    if (sparkFlashCount == 3 && sparkTimer.expired()) {
        PDN->getDisplay()->invalidateScreen();
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("CIRCUIT", 20, 20)
            ->drawText("BREAK", 30, 40);
        PDN->getDisplay()->render();
        sparkFlashCount = 4;  // Prevent re-rendering
    }

    if (loseTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void CipherPathLose::onStateDismounted(Device* PDN) {
    loseTimer.invalidate();
    sparkTimer.invalidate();
    transitionToIntroState = false;
    sparkFlashCount = 0;
    PDN->getHaptics()->off();
}

bool CipherPathLose::transitionToIntro() {
    return transitionToIntroState;
}

bool CipherPathLose::isTerminalState() const {
    return game->getConfig().managedMode;
}
