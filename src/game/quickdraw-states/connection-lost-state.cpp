#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "device/drivers/logger.hpp"
#include "device/device-types.hpp"

static const char* TAG = "ConnectionLost";

ConnectionLost::ConnectionLost(Player* player) :
    State(CONNECTION_LOST),
    player(player)
{
}

ConnectionLost::~ConnectionLost() {
    player = nullptr;
}

void ConnectionLost::onStateMounted(Device* PDN) {
    LOG_W(TAG, "Connection lost - serial timeout");

    transitionToIdleState = false;
    blinkCount = 0;

    // Display "SIGNAL LOST" message
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
    PDN->getDisplay()->drawText("SIGNAL LOST", 10, 25);
    PDN->getDisplay()->render();

    // Start display timer (3 seconds)
    displayTimer.setTimer(DISPLAY_DURATION_MS);

    // Start LED blink timer
    ledTimer.setTimer(LED_BLINK_INTERVAL_MS);

    // Start with LED on (red transmit light)
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.loop = false;
    config.speed = 0;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
    PDN->getLightManager()->startAnimation(config);
}

void ConnectionLost::onStateLoop(Device* PDN) {
    // Update timers
    displayTimer.updateTime();
    ledTimer.updateTime();

    // Handle LED blinking (fast red pulse - 3 blinks)
    if (ledTimer.expired() && blinkCount < BLINK_COUNT * 2) {
        blinkCount++;

        AnimationConfig config;
        config.type = AnimationType::IDLE;
        config.loop = false;
        config.speed = 0;
        config.initialState = LEDState();

        if (blinkCount % 2 == 0) {
            // Even count: turn LED on (red)
            config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(255, 0, 0), 255);
        } else {
            // Odd count: turn LED off
            config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(0, 0, 0), 0);
        }

        PDN->getLightManager()->startAnimation(config);
        ledTimer.setTimer(LED_BLINK_INTERVAL_MS);
    }

    // Check if display timer expired
    if (displayTimer.expired()) {
        transitionToIdleState = true;
    }
}

void ConnectionLost::onStateDismounted(Device* PDN) {
    displayTimer.invalidate();
    ledTimer.invalidate();

    // Stop animation and clear LEDs
    PDN->getLightManager()->stopAnimation();
    PDN->getLightManager()->clear();

    // Clear any pending challenge data
    player->clearPendingChallenge();
}

bool ConnectionLost::transitionToIdle() {
    return transitionToIdleState;
}
