#include "game/konami-metagame/konami-metagame-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "game/color-profiles.hpp"
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <cmath>

static const char* TAG = "KonamiHardModeProgress";

KonamiHardModeProgress::KonamiHardModeProgress(Player* player) :
    State(35),
    player(player)
{
}

KonamiHardModeProgress::~KonamiHardModeProgress() {
    player = nullptr;
}

void KonamiHardModeProgress::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Konami Hard Mode Progress mounted");

    transitionToIdleState = false;
    displayIsDirty = true;

    // Initialize random seed
    srand(static_cast<unsigned>(time(nullptr)));

    // Count stolen powers
    stolenPowers = countStolenPowers();
    LOG_I(TAG, "Stolen powers: %d / 7", stolenPowers);

    displayTimer.setTimer(DISPLAY_DURATION_MS);
    ledTimer.setTimer(50);

    // Set button callbacks
    parameterizedCallbackFunction anyButtonCallback = [](void* ctx) {
        auto* state = static_cast<KonamiHardModeProgress*>(ctx);
        LOG_I(TAG, "Button pressed — transitioning to Idle");
        state->transitionToIdleState = true;
    };
    PDN->getPrimaryButton()->setButtonPress(anyButtonCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(anyButtonCallback, this, ButtonInteraction::CLICK);

    // Single haptic pulse
    PDN->getHaptics()->setIntensity(150);

    renderProgressDisplay(PDN);
}

void KonamiHardModeProgress::onStateLoop(Device* PDN) {
    displayTimer.updateTime();
    ledTimer.updateTime();

    // Update LED pulse
    if (ledTimer.expired()) {
        updateLedPulse(PDN);
        ledTimer.setTimer(50);
    }

    // Auto-dismiss after timeout
    if (displayTimer.expired()) {
        transitionToIdleState = true;
    }

    // Re-render if needed
    if (displayIsDirty) {
        renderProgressDisplay(PDN);
        displayIsDirty = false;
    }
}

void KonamiHardModeProgress::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();
    ledTimer.invalidate();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool KonamiHardModeProgress::transitionToIdle() {
    return transitionToIdleState;
}

void KonamiHardModeProgress::renderProgressDisplay(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    // Header
    PDN->getDisplay()->drawText("BOON ACTIVE", 30, 12);

    // Powers stolen with progress bar
    std::stringstream ss;
    ss << "POWERS: " << stolenPowers << " / 7";
    std::string progressText = ss.str();
    PDN->getDisplay()->drawText(progressText.c_str(), 16, 30);

    // Noise-fill progress bar
    float progress = stolenPowers / 7.0f;
    noiseFillBar(PDN, 20, 36, 88, 10, progress);

    // Prompt
    const char* prompt = (stolenPowers >= 7) ? "COWBOY COMPLETE!" : "RETURN TO FDNs";
    int promptX = (stolenPowers >= 7) ? 10 : 16;
    PDN->getDisplay()->drawText(prompt, promptX, 56);

    PDN->getDisplay()->render();
}

void KonamiHardModeProgress::noiseFillBar(Device* PDN, int x, int y, int w, int h, float progress) {
    // Draw frame
    PDN->getDisplay()->drawFrame(x, y, w, h);

    // Fill with boxes to simulate noise (since we can't draw individual pixels)
    int fillWidth = static_cast<int>((w - 2) * progress);
    for (int py = y + 2; py < y + h - 2; py += 2) {
        for (int px = x + 2; px < x + 2 + fillWidth; px += 2) {
            if (rand() % 2 == 0) {
                PDN->getDisplay()->drawBox(px, py, 1, 1);
            }
        }
    }
}

void KonamiHardModeProgress::updateLedPulse(Device* PDN) {
    static int phase = 0;

    // Gold pulse (Konami color)
    float brightness = 0.5f + 0.5f * std::sin(phase * 0.1f);
    int r = static_cast<int>(255 * brightness);
    int g = static_cast<int>(204 * brightness);  // Gold = (255, 204, 0)
    int b = 0;

    LEDColor color(r, g, b);

    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::IDLE;
    ledConfig.loop = false;

    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(color, static_cast<int>(brightness * 255));
        state.rightLights[i] = LEDState::SingleLEDState(color, static_cast<int>(brightness * 255));
    }
    state.transmitLight = LEDState::SingleLEDState(color, static_cast<int>(brightness * 255));

    ledConfig.initialState = state;
    PDN->getLightManager()->startAnimation(ledConfig);

    phase++;
}

int KonamiHardModeProgress::countStolenPowers() const {
    int count = 0;

    // Check each game's hard mode completion
    if (player->hasColorProfile(FdnGameType::SIGNAL_ECHO)) count++;
    if (player->hasColorProfile(FdnGameType::GHOST_RUNNER)) count++;
    if (player->hasColorProfile(FdnGameType::SPIKE_VECTOR)) count++;
    if (player->hasColorProfile(FdnGameType::FIREWALL_DECRYPT)) count++;
    if (player->hasColorProfile(FdnGameType::CIPHER_PATH)) count++;
    if (player->hasColorProfile(FdnGameType::EXPLOIT_SEQUENCER)) count++;
    if (player->hasColorProfile(FdnGameType::BREACH_DEFENSE)) count++;

    return count;
}
