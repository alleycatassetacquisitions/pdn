#include "game/spike-vector/spike-vector-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "SpikeVectorGameplay";

// Layout constants
static constexpr int HUD_HEIGHT = 8;
static constexpr int CONTROLS_HEIGHT = 8;
static constexpr int WALL_UNIT = 20;  // wallWidth (6) + wallSpacing (14)
static constexpr int CURSOR_X = 8;    // X position of player cursor

SpikeVectorGameplay::SpikeVectorGameplay(SpikeVector* game) : State(SPIKE_GAMEPLAY) {
    this->game = game;
}

SpikeVectorGameplay::~SpikeVectorGameplay() {
    game = nullptr;
}

void SpikeVectorGameplay::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;
    hitFlashActive = false;
    hitFlashCount = 0;

    auto& config = game->getConfig();
    auto& session = game->getSession();

    LOG_I(TAG, "Gameplay started for level %d", session.currentLevel + 1);

    // Set up button callbacks for cursor movement
    // Note: We track button state in the callback since there's no release callback
    parameterizedCallbackFunction upPress = [](void* ctx) {
        auto* state = static_cast<SpikeVectorGameplay*>(ctx);
        auto& sess = state->game->getSession();
        auto& cfg = state->game->getConfig();
        if (sess.cursorPosition > 0) sess.cursorPosition--;
        sess.primaryPressed = true;
    };

    parameterizedCallbackFunction downPress = [](void* ctx) {
        auto* state = static_cast<SpikeVectorGameplay*>(ctx);
        auto& sess = state->game->getSession();
        auto& cfg = state->game->getConfig();
        if (sess.cursorPosition < cfg.numPositions - 1) sess.cursorPosition++;
        sess.secondaryPressed = true;
    };

    PDN->getPrimaryButton()->setButtonPress(upPress, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(downPress, this, ButtonInteraction::CLICK);

    // Start LED chase animation
    AnimationConfig animConfig;
    animConfig.type = AnimationType::VERTICAL_CHASE;
    animConfig.speed = 8;
    animConfig.curve = EaseCurve::LINEAR;
    animConfig.initialState = SPIKE_VECTOR_GAMEPLAY_STATE;
    animConfig.loopDelayMs = 0;
    animConfig.loop = true;
    PDN->getLightManager()->startAnimation(animConfig);

    // Start wall movement timer
    int speedMs = speedMsForLevel(config, session.currentLevel);
    moveTimer.setTimer(speedMs);
}

void SpikeVectorGameplay::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Handle hit flash animation
    if (hitFlashActive && hitFlashTimer.expired()) {
        hitFlashCount++;
        if (hitFlashCount >= 4) {  // 2 flashes = 4 toggles
            hitFlashActive = false;
            hitFlashCount = 0;
            PDN->getHaptics()->off();
        } else {
            hitFlashTimer.setTimer(50);  // 50ms toggle interval
        }
    }

    // Clear button pressed flags after a frame (since we don't have release callbacks)
    if (session.primaryPressed) session.primaryPressed = false;
    if (session.secondaryPressed) session.secondaryPressed = false;

    // Advance formation on timer
    if (moveTimer.expired()) {
        session.formationX--;  // Move left 1px

        // Check for collisions with walls that just passed the cursor
        handleCollisions(PDN);

        // Check if all walls have passed (level complete)
        int numWalls = session.gapPositions.size();
        int lastWallX = session.formationX + (numWalls - 1) * WALL_UNIT;
        if (lastWallX + config.wallWidth < 0) {
            // Last wall is fully off screen
            session.levelComplete = true;
            transitionToEvaluateState = true;
            return;
        }

        // Reset timer for next movement
        int speedMs = speedMsForLevel(config, session.currentLevel);
        moveTimer.setTimer(speedMs);
    }

    // Render frame
    renderFrame(PDN);
}

void SpikeVectorGameplay::onStateDismounted(Device* PDN) {
    moveTimer.invalidate();
    hitFlashTimer.invalidate();
    transitionToEvaluateState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getHaptics()->off();
}

bool SpikeVectorGameplay::transitionToEvaluate() {
    return transitionToEvaluateState;
}

void SpikeVectorGameplay::handleCollisions(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Check the next wall in sequence
    if (session.nextWallIndex >= (int)session.gapPositions.size()) {
        return;  // All walls already evaluated
    }

    int wallX = session.formationX + session.nextWallIndex * WALL_UNIT;

    // Wall "arrives" when its left edge reaches or passes the cursor
    if (wallX <= CURSOR_X) {
        // Evaluate collision for this wall
        int gapPos = session.gapPositions[session.nextWallIndex];

        if (session.cursorPosition == gapPos) {
            // Dodge — player was at the gap
            session.score += 100;
            LOG_I(TAG, "Dodge! Wall %d, gap at %d, score: %d",
                  session.nextWallIndex, gapPos, session.score);
        } else {
            // Hit — player was not at the gap
            session.hits++;
            triggerHitFeedback(PDN);
            LOG_I(TAG, "Hit! Wall %d, gap at %d (cursor at %d), hits: %d/%d",
                  session.nextWallIndex, gapPos, session.cursorPosition,
                  session.hits, config.hitsAllowed);
        }

        session.nextWallIndex++;
    }
}

void SpikeVectorGameplay::triggerHitFeedback(Device* PDN) {
    // Screen flash (XOR mode)
    hitFlashActive = true;
    hitFlashCount = 0;
    hitFlashTimer.setTimer(50);

    // Haptic pulse
    PDN->getHaptics()->setIntensity(255);

    // Note: Red LED flash would require override functionality not currently in LightManager
    // The screen flash and haptic provide sufficient feedback
}

void SpikeVectorGameplay::renderFrame(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();

    renderHUD(PDN);

    // Top separator
    PDN->getDisplay()->drawBox(0, HUD_HEIGHT, 128, 1);

    renderGameArea(PDN);

    // Bottom separator
    PDN->getDisplay()->drawBox(0, 64 - CONTROLS_HEIGHT - 1, 128, 1);

    renderControls(PDN);

    // Apply hit flash if active
    if (hitFlashActive && (hitFlashCount % 2 == 1)) {
        PDN->getDisplay()->setDrawColor(2);  // XOR mode
        PDN->getDisplay()->drawBox(0, 0, 128, 64);
        PDN->getDisplay()->setDrawColor(1);  // Restore normal mode
    }

    PDN->getDisplay()->render();
}

void SpikeVectorGameplay::renderHUD(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Level counter (left)
    char levelStr[16];
    snprintf(levelStr, sizeof(levelStr), "L:%d/%d", session.currentLevel + 1, config.levels);
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(levelStr, 2, 7);

    // Lives as hearts (center)
    int livesRemaining = config.hitsAllowed - session.hits;
    int heartX = 48;
    for (int i = 0; i < config.hitsAllowed; i++) {
        int hx = heartX + i * 10;
        if (i < livesRemaining) {
            // Filled heart (4x4 box for simplicity)
            PDN->getDisplay()->drawBox(hx, 2, 4, 4);
        } else {
            // Empty heart (outline)
            PDN->getDisplay()->drawFrame(hx, 2, 4, 4);
        }
    }

    // Difficulty label (right)
    const char* diffLabel = (config.numPositions == 5) ? "EASY" : "HARD";
    PDN->getDisplay()->drawText(diffLabel, 100, 7);
}

void SpikeVectorGameplay::renderGameArea(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    int gameAreaTop = HUD_HEIGHT + 1;
    int gameAreaHeight = 64 - HUD_HEIGHT - CONTROLS_HEIGHT - 2;  // Subtract separators
    int laneHeight = gameAreaHeight / config.numPositions;

    // Lane tick marks (left edge)
    for (int lane = 0; lane < config.numPositions - 1; lane++) {
        int tickY = gameAreaTop + (lane + 1) * laneHeight;
        PDN->getDisplay()->drawBox(0, tickY, 3, 1);
    }

    // Player cursor (right-pointing triangle)
    int cursorY = gameAreaTop + session.cursorPosition * laneHeight + (laneHeight - CURSOR_SPRITE.height) / 2;
    PDN->getDisplay()->drawImage(CURSOR_SPRITE, CURSOR_X - CURSOR_SPRITE.width, cursorY);

    // Walls (multi-wall formation)
    for (int w = 0; w < (int)session.gapPositions.size(); w++) {
        int wx = session.formationX + w * WALL_UNIT;

        // Skip walls that are off-screen
        if (wx < -config.wallWidth || wx > 128) continue;

        int gapPos = session.gapPositions[w];

        // Draw wall segments (all lanes except the gap)
        for (int lane = 0; lane < config.numPositions; lane++) {
            if (lane == gapPos) continue;  // Gap — don't draw

            int ly = gameAreaTop + lane * laneHeight;
            PDN->getDisplay()->drawBox(wx, ly, config.wallWidth, laneHeight);
        }
    }
}

void SpikeVectorGameplay::renderControls(Device* PDN) {
    auto& session = game->getSession();
    int cy = 64 - CONTROLS_HEIGHT;

    // [UP] button indicator
    if (session.primaryPressed) {
        // Inverted
        PDN->getDisplay()->drawBox(2, cy + 1, 24, CONTROLS_HEIGHT - 2);
        PDN->getDisplay()->setDrawColor(0);
        PDN->getDisplay()->drawText("UP", 6, cy + CONTROLS_HEIGHT - 2);
        PDN->getDisplay()->setDrawColor(1);
    } else {
        PDN->getDisplay()->drawText("[UP]", 2, cy + CONTROLS_HEIGHT - 2);
    }

    // [DOWN] button indicator
    if (session.secondaryPressed) {
        // Inverted
        PDN->getDisplay()->drawBox(96, cy + 1, 30, CONTROLS_HEIGHT - 2);
        PDN->getDisplay()->setDrawColor(0);
        PDN->getDisplay()->drawText("DN", 100, cy + CONTROLS_HEIGHT - 2);
        PDN->getDisplay()->setDrawColor(1);
    } else {
        PDN->getDisplay()->drawText("[DOWN]", 96, cy + CONTROLS_HEIGHT - 2);
    }
}
