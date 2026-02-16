#include "game/konami-metagame/konami-metagame-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "game/progress-manager.hpp"
#include "game/color-profiles.hpp"
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <sstream>
#include <iomanip>

static const char* TAG = "KonamiScanSequence";

KonamiScanSequence::KonamiScanSequence(Player* player) :
    State(33),
    player(player)
{
}

KonamiScanSequence::~KonamiScanSequence() {
    player = nullptr;
}

void KonamiScanSequence::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Konami Scan Sequence mounted");

    transitionToMasteryCompleteState = false;
    transitionToHardModeProgressState = false;
    transitionToFdnDisplayState = false;
    displayIsDirty = true;
    currentPhase = ScanPhase::TYPEWRITER_SCANNING;
    animationStep = 0;

    // Initialize random seed
    srand(static_cast<unsigned>(time(nullptr)));

    // Generate hex ID (8 chars)
    std::stringstream ss;
    for (int i = 0; i < 8; i++) {
        ss << std::hex << (rand() % 16);
    }
    hexId = ss.str();

    // Determine status message based on player progress
    determineStatusMessage();

    scanTimer.setTimer(TOTAL_SCAN_DURATION_MS);
    animationTimer.setTimer(TYPEWRITER_CHAR_MS);

    // Clear display
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->render();

    // Start LED animation
    updateLedPulse(PDN);
}

void KonamiScanSequence::onStateLoop(Device* PDN) {
    scanTimer.updateTime();
    animationTimer.updateTime();

    // Render current animation frame
    if (displayIsDirty || animationTimer.expired()) {
        renderScanAnimation(PDN);
        displayIsDirty = false;
        animationTimer.setTimer(30);  // 30ms frame rate
    }

    // Update LED pulse
    updateLedPulse(PDN);

    // Check if scan complete
    if (scanTimer.expired() && currentPhase == ScanPhase::COMPLETE) {
        // Route to appropriate next state
        if (hasAllColorPalettes()) {
            transitionToMasteryCompleteState = true;
        } else if (player->hasHardModeUnlocked() && hasAllKonamiButtons()) {
            transitionToHardModeProgressState = true;
        } else {
            transitionToFdnDisplayState = true;
        }
    }
}

void KonamiScanSequence::onStateDismounted(Device* PDN) {
    scanTimer.invalidate();
    animationTimer.invalidate();
}

bool KonamiScanSequence::transitionToMasteryComplete() {
    return transitionToMasteryCompleteState;
}

bool KonamiScanSequence::transitionToHardModeProgress() {
    return transitionToHardModeProgressState;
}

bool KonamiScanSequence::transitionToFdnDisplay() {
    return transitionToFdnDisplayState;
}

void KonamiScanSequence::determineStatusMessage() {
    if (hasAllColorPalettes()) {
        statusMessage = "MASTERY DETECTED";
    } else if (hasAllKonamiButtons()) {
        statusMessage = "ACCESS VERIFIED";
    } else {
        statusMessage = "INSUFFICIENT ACCESS";
    }
}

void KonamiScanSequence::renderScanAnimation(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    unsigned long elapsed = TOTAL_SCAN_DURATION_MS - scanTimer.getElapsedTime();

    // Phase 1: Typewriter "SCANNING..." (0-400ms)
    if (elapsed < 400) {
        int chars = (elapsed / TYPEWRITER_CHAR_MS);
        typewriterReveal(PDN, "> SCANNING...", 4, 10, chars);
        currentPhase = ScanPhase::TYPEWRITER_SCANNING;
    }
    // Phase 2: Scramble ID (400-800ms)
    else if (elapsed < 800) {
        PDN->getDisplay()->drawText("> SCANNING...", 4, 10);

        float progress = (elapsed - 400.0f) / SCRAMBLE_DURATION_MS;
        scrambleDecode(PDN, "> ID: " + hexId, 4, 24, progress);
        currentPhase = ScanPhase::SCRAMBLE_ID;
    }
    // Phase 3: Noise bar (800-1200ms)
    else if (elapsed < 1200) {
        PDN->getDisplay()->drawText("> SCANNING...", 4, 10);
        PDN->getDisplay()->drawText(("> ID: " + hexId).c_str(), 4, 24);

        float progress = (elapsed - 800.0f) / 400.0f;
        noiseFillBar(PDN, 30, 28, 80, 8, progress);
        currentPhase = ScanPhase::NOISE_BAR;
    }
    // Phase 4: Scramble status (1200-1600ms)
    else if (elapsed < 1600) {
        PDN->getDisplay()->drawText("> SCANNING...", 4, 10);
        PDN->getDisplay()->drawText(("> ID: " + hexId).c_str(), 4, 24);
        PDN->getDisplay()->drawFrame(30, 28, 80, 8);

        float progress = (elapsed - 1200.0f) / SCRAMBLE_DURATION_MS;
        scrambleDecode(PDN, "> STATUS: " + statusMessage, 4, 50, progress);
        currentPhase = ScanPhase::SCRAMBLE_STATUS;
    }
    // Phase 5: Complete (1600-2000ms)
    else {
        PDN->getDisplay()->drawText("> SCANNING...", 4, 10);
        PDN->getDisplay()->drawText(("> ID: " + hexId).c_str(), 4, 24);
        PDN->getDisplay()->drawFrame(30, 28, 80, 8);
        PDN->getDisplay()->drawText(("> STATUS: " + statusMessage).c_str(), 4, 50);
        currentPhase = ScanPhase::COMPLETE;
    }

    PDN->getDisplay()->render();
}

void KonamiScanSequence::typewriterReveal(Device* PDN, const std::string& text, int x, int y, int charsRevealed) {
    std::string revealed = text.substr(0, std::min(charsRevealed, static_cast<int>(text.length())));
    PDN->getDisplay()->drawText(revealed.c_str(), x, y);
}

void KonamiScanSequence::scrambleDecode(Device* PDN, const std::string& target, int x, int y, float progress) {
    std::string output;
    int revealedChars = static_cast<int>(target.length() * progress);

    for (size_t i = 0; i < target.length(); i++) {
        if (i < static_cast<size_t>(revealedChars)) {
            output += target[i];
        } else {
            output += getRandomChar();
        }
    }

    PDN->getDisplay()->drawText(output.c_str(), x, y);
}

void KonamiScanSequence::noiseFillBar(Device* PDN, int x, int y, int w, int h, float progress) {
    // Draw frame
    PDN->getDisplay()->drawFrame(x, y, w, h);

    // Fill with boxes to simulate noise (since we can't draw individual pixels with this API)
    int fillWidth = static_cast<int>(w * progress);
    for (int py = y + 2; py < y + h - 2; py += 2) {
        for (int px = x + 2; px < x + fillWidth - 2; px += 2) {
            if (rand() % 2 == 0) {
                PDN->getDisplay()->drawBox(px, py, 1, 1);
            }
        }
    }
}

void KonamiScanSequence::updateLedPulse(Device* PDN) {
    // Pulse animation config
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::IDLE;
    ledConfig.loop = false;

    unsigned long elapsed = TOTAL_SCAN_DURATION_MS - scanTimer.getElapsedTime();
    float phase = (elapsed % 1000) / 1000.0f;
    int brightness = static_cast<int>(128 + 127 * std::sin(phase * 3.14159f * 2));

    LEDColor color;
    if (hasAllColorPalettes()) {
        // Gold for mastery
        color = LEDColor(brightness, static_cast<int>(brightness * 0.8), 0);
    } else if (hasAllKonamiButtons()) {
        // Green for verified
        color = LEDColor(0, brightness, 0);
    } else {
        // Red for insufficient
        color = LEDColor(brightness, 0, 0);
    }

    LEDState state;
    for (int i = 0; i < 9; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(color, brightness);
        state.rightLights[i] = LEDState::SingleLEDState(color, brightness);
    }
    state.transmitLight = LEDState::SingleLEDState(color, brightness);

    ledConfig.initialState = state;
    PDN->getLightManager()->startAnimation(ledConfig);
}

char KonamiScanSequence::getRandomChar() const {
    const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    return chars[rand() % (sizeof(chars) - 1)];
}

bool KonamiScanSequence::hasAllColorPalettes() const {
    // Check if player has all 7 color palettes unlocked
    return player->hasColorProfile(FdnGameType::SIGNAL_ECHO) &&
           player->hasColorProfile(FdnGameType::GHOST_RUNNER) &&
           player->hasColorProfile(FdnGameType::SPIKE_VECTOR) &&
           player->hasColorProfile(FdnGameType::FIREWALL_DECRYPT) &&
           player->hasColorProfile(FdnGameType::CIPHER_PATH) &&
           player->hasColorProfile(FdnGameType::EXPLOIT_SEQUENCER) &&
           player->hasColorProfile(FdnGameType::BREACH_DEFENSE);
}

bool KonamiScanSequence::hasAllKonamiButtons() const {
    return player->hasKonamiButton(FdnGameType::SIGNAL_ECHO) &&
           player->hasKonamiButton(FdnGameType::GHOST_RUNNER) &&
           player->hasKonamiButton(FdnGameType::SPIKE_VECTOR) &&
           player->hasKonamiButton(FdnGameType::FIREWALL_DECRYPT) &&
           player->hasKonamiButton(FdnGameType::CIPHER_PATH) &&
           player->hasKonamiButton(FdnGameType::EXPLOIT_SEQUENCER) &&
           player->hasKonamiButton(FdnGameType::BREACH_DEFENSE);
}

bool KonamiScanSequence::hasHardMode() const {
    return player->hasHardModeUnlocked();
}
