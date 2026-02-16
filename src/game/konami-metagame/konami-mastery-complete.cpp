#include "game/konami-metagame/konami-metagame-states.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "game/color-profiles.hpp"
#include <cstdlib>
#include <ctime>
#include <cmath>

static const char* TAG = "KonamiMasteryComplete";

KonamiMasteryComplete::KonamiMasteryComplete(Player* player) :
    State(34),
    player(player)
{
}

KonamiMasteryComplete::~KonamiMasteryComplete() {
    player = nullptr;
}

void KonamiMasteryComplete::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Konami Mastery Complete mounted — COWBOY MESSAGE");

    transitionToIdleState = false;
    displayIsDirty = true;
    currentPhase = MasteryPhase::BLACK_SCREEN;
    animationStep = 0;
    typewriterChars = 0;

    // Initialize random seed
    srand(static_cast<unsigned>(time(nullptr)));

    animationTimer.setTimer(BLACK_SCREEN_MS);

    // Set button callbacks
    parameterizedCallbackFunction anyButtonCallback = [](void* ctx) {
        auto* state = static_cast<KonamiMasteryComplete*>(ctx);
        LOG_I(TAG, "Button pressed — transitioning to Idle");
        state->transitionToIdleState = true;
    };
    PDN->getPrimaryButton()->setButtonPress(anyButtonCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(anyButtonCallback, this, ButtonInteraction::CLICK);

    // Start with black screen
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->render();
}

void KonamiMasteryComplete::onStateLoop(Device* PDN) {
    animationTimer.updateTime();

    if (displayIsDirty || animationTimer.expired()) {
        renderMasteryDisplay(PDN);
        displayIsDirty = false;
    }
}

void KonamiMasteryComplete::onStateDismounted(Device* PDN) {
    animationTimer.invalidate();
    ledTimer.invalidate();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool KonamiMasteryComplete::transitionToIdle() {
    return transitionToIdleState;
}

void KonamiMasteryComplete::renderMasteryDisplay(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

    switch (currentPhase) {
        case MasteryPhase::BLACK_SCREEN:
            // Already cleared
            if (animationTimer.expired()) {
                currentPhase = MasteryPhase::TYPEWRITER_KONAMI;
                typewriterChars = 0;
                animationTimer.setTimer(TYPEWRITER_CHAR_MS);
            }
            break;

        case MasteryPhase::TYPEWRITER_KONAMI: {
            const std::string text = "KONAMI HAS SEEN YOU.";

            if (animationTimer.expired()) {
                typewriterChars++;

                // Micro-haptic pulse per character
                if (typewriterChars <= static_cast<int>(text.length())) {
                    PDN->getHaptics()->setIntensity(50);
                }

                if (typewriterChars > static_cast<int>(text.length())) {
                    currentPhase = MasteryPhase::SCRAMBLE_COWBOY;
                    animationStep = 0;
                    animationTimer.setTimer(30);
                } else {
                    animationTimer.setTimer(TYPEWRITER_CHAR_MS);
                }
            }

            typewriterReveal(PDN, text, 4, 30, typewriterChars);
            break;
        }

        case MasteryPhase::SCRAMBLE_COWBOY: {
            const std::string text1 = "KONAMI HAS SEEN YOU.";
            const std::string text2 = "MAY FORTUNE FAVOR";
            const std::string text3 = "YOU, COWBOY";

            if (animationTimer.expired()) {
                animationStep++;

                if (animationStep >= SCRAMBLE_DURATION_MS / 30) {
                    currentPhase = MasteryPhase::RAINBOW_SWEEP;
                    ledTimer.setTimer(50);
                    animationTimer.setTimer(50);
                }
                animationTimer.setTimer(30);
            }

            // Show first line fully
            PDN->getDisplay()->drawText(text1.c_str(), 4, 20);

            // Scramble-decode second and third lines
            float progress = animationStep / (SCRAMBLE_DURATION_MS / 30.0f);
            scrambleDecode(PDN, text2, 4, 36, progress);
            scrambleDecode(PDN, text3, 4, 48, progress);
            break;
        }

        case MasteryPhase::RAINBOW_SWEEP:
        case MasteryPhase::COMPLETE: {
            if (currentPhase != MasteryPhase::COMPLETE && animationTimer.expired()) {
                currentPhase = MasteryPhase::COMPLETE;
            }

            PDN->getDisplay()->drawText("KONAMI HAS SEEN YOU.", 4, 20);
            PDN->getDisplay()->drawText("MAY FORTUNE FAVOR", 4, 36);
            PDN->getDisplay()->drawText("YOU, COWBOY", 4, 48);

            // Draw rainbow bar at bottom
            drawRainbowBar(PDN);

            // Update rainbow LED sweep
            if (ledTimer.expired()) {
                updateRainbowLeds(PDN);
                ledTimer.setTimer(50);
            }
            break;
        }
    }

    PDN->getDisplay()->render();
}

void KonamiMasteryComplete::typewriterReveal(Device* PDN, const std::string& text, int x, int y, int charsRevealed) {
    std::string revealed = text.substr(0, std::min(charsRevealed, static_cast<int>(text.length())));
    PDN->getDisplay()->drawText(revealed.c_str(), x, y);
}

void KonamiMasteryComplete::scrambleDecode(Device* PDN, const std::string& target, int x, int y, float progress) {
    std::string output;
    int revealedChars = static_cast<int>(target.length() * progress);

    for (size_t i = 0; i < target.length(); i++) {
        if (i < static_cast<size_t>(revealedChars)) {
            output += target[i];
        } else if (target[i] == ' ') {
            output += ' ';
        } else {
            output += getRandomChar();
        }
    }

    PDN->getDisplay()->drawText(output.c_str(), x, y);
}

void KonamiMasteryComplete::drawRainbowBar(Device* PDN) {
    // Draw 7 colored segments (represented as boxes on monochrome display)
    int segmentWidth = 128 / 7;
    for (int i = 0; i < 7; i++) {
        int x = i * segmentWidth;
        PDN->getDisplay()->drawBox(x, 58, segmentWidth - 1, 6);
    }
}

void KonamiMasteryComplete::updateRainbowLeds(Device* PDN) {
    static int offset = 0;

    // Rainbow colors for all 7 games (representative colors)
    LEDColor colors[] = {
        LEDColor(255, 0, 255),    // Signal Echo - Magenta
        LEDColor(0, 255, 255),    // Ghost Runner - Cyan
        LEDColor(255, 100, 0),    // Spike Vector - Orange
        LEDColor(0, 255, 100),    // Firewall Decrypt - Green
        LEDColor(100, 100, 255),  // Cipher Path - Blue
        LEDColor(255, 255, 0),    // Exploit Sequencer - Yellow
        LEDColor(255, 0, 0)       // Breach Defense - Red
    };

    // Create LED state with cycling colors
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::IDLE;
    ledConfig.loop = false;

    LEDState state;
    for (int i = 0; i < 9; i++) {
        LEDColor color = colors[(i + offset) % 7];
        state.leftLights[i] = LEDState::SingleLEDState(color, 255);
        state.rightLights[i] = LEDState::SingleLEDState(color, 255);
    }
    state.transmitLight = LEDState::SingleLEDState(LEDColor(255, 255, 255), 255);

    ledConfig.initialState = state;
    PDN->getLightManager()->startAnimation(ledConfig);

    offset = (offset + 1) % 7;
}

char KonamiMasteryComplete::getRandomChar() const {
    const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%";
    return chars[rand() % (sizeof(chars) - 1)];
}
