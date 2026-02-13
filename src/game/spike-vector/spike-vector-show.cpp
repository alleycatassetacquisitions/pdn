#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-resources.hpp"
#include "device/drivers/logger.hpp"
#include <cstdlib>
#include <string>

static const char* TAG = "SpikeVectorShow";

SpikeVectorShow::SpikeVectorShow(SpikeVector* game) : State(SPIKE_SHOW) {
    this->game = game;
}

SpikeVectorShow::~SpikeVectorShow() {
    game = nullptr;
}

void SpikeVectorShow::onStateMounted(Device* PDN) {
    transitionToGameplayState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Generate gap position for this wave
    session.gapPosition = rand() % config.numPositions;

    // Reset wall state for this wave
    session.wallPosition = 0;
    session.wallArrived = false;
    session.cursorPosition = config.startPosition;

    LOG_I(TAG, "Wave %d of %d, gap at %d", session.currentWave + 1, config.waves, session.gapPosition);

    // Display wave info
    PDN->getDisplay()->invalidateScreen();

    std::string waveStr = "Wave " + std::to_string(session.currentWave + 1) +
                          " of " + std::to_string(config.waves);
    int livesRemaining = config.hitsAllowed - session.hits;
    std::string livesStr = "Lives: " + std::to_string(livesRemaining);

    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(waveStr.c_str(), 15, 25)
        ->drawText(livesStr.c_str(), 30, 50);
    PDN->getDisplay()->render();

    // Haptic pulse feedback
    PDN->getHaptics()->setIntensity(100);

    showTimer.setTimer(SHOW_DURATION_MS);
}

void SpikeVectorShow::onStateLoop(Device* PDN) {
    if (showTimer.expired()) {
        PDN->getHaptics()->off();
        transitionToGameplayState = true;
    }
}

void SpikeVectorShow::onStateDismounted(Device* PDN) {
    showTimer.invalidate();
    transitionToGameplayState = false;
    PDN->getHaptics()->off();
}

bool SpikeVectorShow::transitionToGameplay() {
    return transitionToGameplayState;
}
