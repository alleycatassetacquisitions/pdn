#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-resources.hpp"
#include "device/drivers/logger.hpp"
#include <cstdlib>
#include <algorithm>

static const char* TAG = "SpikeVectorShow";

SpikeVectorShow::SpikeVectorShow(SpikeVector* game) : State(SPIKE_SHOW) {
    this->game = game;
}

SpikeVectorShow::~SpikeVectorShow() {
    game = nullptr;
}

void SpikeVectorShow::onStateMounted(Device* PDN) {
    transitionToGameplayState = false;
    flashCount = 0;
    flashState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Generate gap positions array for this level
    int numWalls = wallsForLevel(config, session.currentLevel);
    int maxGapDistance = maxGapForLevel(config, session.currentLevel);

    session.gapPositions.clear();
    session.gapPositions.reserve(numWalls);

    // First gap is random
    session.gapPositions.push_back(rand() % config.numPositions);

    // Generate subsequent gaps with bounded distance
    for (int i = 1; i < numWalls; i++) {
        int prevGap = session.gapPositions[i - 1];
        int minGap = 1;  // Min gap distance (consecutive gaps can't be in same lane)
        int maxGap = maxGapDistance;

        // Generate a random delta within [minGap, maxGap]
        int delta = (rand() % maxGap) + minGap;
        int direction = (rand() % 2) ? 1 : -1;

        int candidate = prevGap + (delta * direction);

        // Clamp to valid range [0, numPositions-1]
        if (candidate < 0) candidate = 0;
        if (candidate >= config.numPositions) candidate = config.numPositions - 1;

        // If clamped to same as previous, flip direction and try again
        if (candidate == prevGap) {
            candidate = prevGap - (delta * direction);
            if (candidate < 0) candidate = 0;
            if (candidate >= config.numPositions) candidate = config.numPositions - 1;
        }

        session.gapPositions.push_back(candidate);
    }

    // Reset per-level state
    session.formationX = 128;  // Start off-screen right
    session.nextWallIndex = 0;
    session.levelComplete = false;
    session.cursorPosition = config.startPosition;

    LOG_I(TAG, "Level %d/%d: %d walls, max gap dist %d, speed %dms",
          session.currentLevel + 1, config.levels, numWalls, maxGapDistance,
          speedMsForLevel(config, session.currentLevel));

    // Start timers
    showTimer.setTimer(SHOW_DURATION_MS);
    flashTimer.setTimer(FLASH_INTERVAL_MS);

    // Haptic pulse feedback
    PDN->getHaptics()->setIntensity(100);
}

void SpikeVectorShow::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Handle flash animation for level progress pips
    if (flashTimer.expired() && flashCount < 6) {  // 3 cycles = 6 toggles
        flashState = !flashState;
        flashCount++;
        flashTimer.setTimer(FLASH_INTERVAL_MS);
    }

    // Render level progress pips with flash
    PDN->getDisplay()->invalidateScreen();

    // Title
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("LEVEL", 44, 15);

    // Level pips (8px squares, 4px spacing)
    int pipWidth = 8;
    int totalWidth = config.levels * (pipWidth + 4) - 4;
    int startX = (128 - totalWidth) / 2;
    int y = 25;

    for (int i = 0; i < config.levels; i++) {
        int px = startX + i * (pipWidth + 4);
        if (i < session.currentLevel) {
            // Completed — filled square
            PDN->getDisplay()->drawBox(px, y, pipWidth, pipWidth);
        } else if (i == session.currentLevel && flashState) {
            // Current level — flashing
            PDN->getDisplay()->drawBox(px, y, pipWidth, pipWidth);
        } else {
            // Incomplete — outline only
            PDN->getDisplay()->drawFrame(px, y, pipWidth, pipWidth);
        }
    }

    // Lives remaining (hearts)
    int livesRemaining = config.hitsAllowed - session.hits;
    int heartsY = 45;
    for (int i = 0; i < config.hitsAllowed; i++) {
        int hx = 44 + i * 12;
        if (i < livesRemaining) {
            // Filled heart (solid block for simplicity)
            PDN->getDisplay()->drawBox(hx, heartsY, 8, 8);
        } else {
            // Empty heart (outline)
            PDN->getDisplay()->drawFrame(hx, heartsY, 8, 8);
        }
    }

    PDN->getDisplay()->render();

    // Transition when timer expires
    if (showTimer.expired()) {
        PDN->getHaptics()->off();
        transitionToGameplayState = true;
    }
}

void SpikeVectorShow::onStateDismounted(Device* PDN) {
    showTimer.invalidate();
    flashTimer.invalidate();
    transitionToGameplayState = false;
    PDN->getHaptics()->off();
}

bool SpikeVectorShow::transitionToGameplay() {
    return transitionToGameplayState;
}
