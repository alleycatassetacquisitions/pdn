#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "GhostRunnerGameplay";

GhostRunnerGameplay::GhostRunnerGameplay(GhostRunner* game) : State(GHOST_GAMEPLAY) {
    this->game = game;
}

GhostRunnerGameplay::~GhostRunnerGameplay() {
    game = nullptr;
}

void GhostRunnerGameplay::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;
    displayDirty = true;

    auto& session = game->getSession();
    LOG_I(TAG, "Gameplay started - navigate from memory");

    // PRIMARY button callback — cycle direction clockwise
    parameterizedCallbackFunction turnCallback = [](void* ctx) {
        auto* state = static_cast<GhostRunnerGameplay*>(ctx);
        auto& session = state->game->getSession();
        session.currentDirection = (session.currentDirection + 1) % 4;
        // 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT
        state->displayDirty = true;
        LOG_I(TAG, "Turned to direction %d", session.currentDirection);
    };

    // SECONDARY button callback — move in current direction
    parameterizedCallbackFunction moveCallback = [](void* ctx) {
        auto* state = static_cast<GhostRunnerGameplay*>(ctx);
        auto& session = state->game->getSession();
        auto& config = state->game->getConfig();

        int nextRow = session.cursorRow;
        int nextCol = session.cursorCol;

        switch (session.currentDirection) {
            case DIR_UP:    nextRow--; break;
            case DIR_RIGHT: nextCol++; break;
            case DIR_DOWN:  nextRow++; break;
            case DIR_LEFT:  nextCol--; break;
        }

        // Bounds check
        if (nextRow < 0 || nextRow >= config.rows ||
            nextCol < 0 || nextCol >= config.cols) {
            state->bonkTriggered = true;
            return;
        }

        // Wall check
        if (state->hasWall(session.cursorRow, session.cursorCol,
                           session.currentDirection)) {
            state->bonkTriggered = true;
            return;
        }

        // Valid move
        session.cursorRow = nextRow;
        session.cursorCol = nextCol;
        session.stepsUsed++;
        state->displayDirty = true;

        LOG_I(TAG, "Moved to (%d,%d), steps: %d", nextRow, nextCol, session.stepsUsed);

        // Check if reached exit
        if (nextRow == config.exitRow && nextCol == config.exitCol) {
            LOG_I(TAG, "Exit reached!");
            state->transitionToEvaluateState = true;
        }
    };

    PDN->getPrimaryButton()->setButtonPress(turnCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(moveCallback, this, ButtonInteraction::CLICK);

    // Start chase animation
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::VERTICAL_CHASE;
    ledConfig.speed = 8;
    ledConfig.curve = EaseCurve::EASE_OUT;
    ledConfig.initialState = GHOST_RUNNER_GAMEPLAY_STATE;
    ledConfig.loop = true;
    PDN->getLightManager()->startAnimation(ledConfig);
}

void GhostRunnerGameplay::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Process bonk trigger from callback
    if (bonkTriggered) {
        triggerBonk(PDN);
        bonkTriggered = false;
    }

    // Check for bonk flash expiration
    if (session.mazeFlashActive && mazeFlashTimer.expired()) {
        session.mazeFlashActive = false;
        displayDirty = true;
        PDN->getHaptics()->off();
    }

    // Render display (only when dirty to save CPU)
    if (displayDirty) {
        drawMaze(PDN, session.mazeFlashActive);

        // HUD: Round, lives, step counter, direction
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

        std::string roundStr = "R" + std::to_string(session.currentRound + 1) +
                               "/" + std::to_string(config.rounds);
        PDN->getDisplay()->drawText(roundStr.c_str(), 2, 2);

        // Draw lives
        for (int i = 0; i < config.lives; i++) {
            int lx = 50 + i * 8;
            if (i < session.livesRemaining) {
                PDN->getDisplay()->setDrawColor(1)->drawBox(lx, 2, 5, 5);
            } else {
                PDN->getDisplay()->setDrawColor(1)->drawFrame(lx, 2, 5, 5);
            }
        }

        // Step counter
        std::string stepStr = "S:" + std::to_string(session.stepsUsed);
        PDN->getDisplay()->drawText(stepStr.c_str(), 80, 2);

        // Direction indicator
        std::string dirStr = std::string("D:") + getDirectionArrow(session.currentDirection);
        PDN->getDisplay()->drawText(dirStr.c_str(), 110, 2);

        // Bottom controls hint
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("[TURN]      [MOVE]", 10, 57);

        PDN->getDisplay()->render();
        displayDirty = false;
    }

    // Check for game over
    if (session.livesRemaining <= 0) {
        transitionToEvaluateState = true;
    }
}

void GhostRunnerGameplay::onStateDismounted(Device* PDN) {
    mazeFlashTimer.invalidate();
    cursorFlashTimer.invalidate();
    transitionToEvaluateState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getHaptics()->off();
}

bool GhostRunnerGameplay::transitionToEvaluate() {
    return transitionToEvaluateState;
}

/*
 * Check if there's a wall in the given direction from the given cell.
 */
bool GhostRunnerGameplay::hasWall(int row, int col, int direction) {
    auto& config = game->getConfig();
    auto& session = game->getSession();
    int idx = row * config.cols + col;
    return (session.walls[idx] & (1 << direction)) != 0;
}

/*
 * Trigger bonk: lose life, flash maze, haptic buzz.
 */
void GhostRunnerGameplay::triggerBonk(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    session.livesRemaining--;
    session.bonkCount++;

    LOG_I(TAG, "BONK! Lives remaining: %d", session.livesRemaining);

    // Flash maze visible
    session.mazeFlashActive = true;
    mazeFlashTimer.setTimer(config.bonkFlashMs);

    // Strong haptic buzz
    PDN->getHaptics()->setIntensity(255);

    displayDirty = true;
}

/*
 * Draw the maze grid and walls.
 * showWalls: if true, draw wall lines; if false, only draw grid outlines.
 */
void GhostRunnerGameplay::drawMaze(Device* PDN, bool showWalls) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    PDN->getDisplay()->invalidateScreen();

    // HUD separator line (y 9)
    PDN->getDisplay()->setDrawColor(1)->drawBox(0, 9, 128, 1);

    // Maze area (y 10-55, 46px tall)
    int cellW = 128 / config.cols;
    int cellH = 46 / config.rows;

    // Draw grid cell outlines (always visible for orientation)
    PDN->getDisplay()->setDrawColor(1);
    for (int r = 0; r <= config.rows; r++) {
        int y = 10 + r * cellH;
        PDN->getDisplay()->drawBox(0, y, 128, 1);
    }
    for (int c = 0; c <= config.cols; c++) {
        int x = c * cellW;
        PDN->getDisplay()->drawBox(x, 10, 1, 46);
    }

    if (showWalls) {
        // Draw walls (during bonk flash)
        for (int r = 0; r < config.rows; r++) {
            for (int c = 0; c < config.cols; c++) {
                int idx = r * config.cols + c;
                uint8_t walls = session.walls[idx];

                int x = c * cellW;
                int y = 10 + r * cellH;

                // Draw thicker lines for walls (use drawBox with 2px thickness)
                if (walls & WALL_UP) {
                    PDN->getDisplay()->drawBox(x, y, cellW, 2);
                }
                if (walls & WALL_DOWN) {
                    PDN->getDisplay()->drawBox(x, y + cellH - 1, cellW, 2);
                }
                if (walls & WALL_LEFT) {
                    PDN->getDisplay()->drawBox(x, y, 2, cellH);
                }
                if (walls & WALL_RIGHT) {
                    PDN->getDisplay()->drawBox(x + cellW - 1, y, 2, cellH);
                }
            }
        }
    }

    // Draw exit marker (always visible)
    int exitX = 1 + config.exitCol * cellW + cellW / 3;
    int exitY = 10 + config.exitRow * cellH + cellH / 3;
    PDN->getDisplay()->setDrawColor(1)->drawFrame(exitX, exitY, cellW / 3, cellH / 3);

    // Draw cursor (player position)
    int cursorX = 1 + session.cursorCol * cellW + cellW / 4;
    int cursorY = 10 + session.cursorRow * cellH + cellH / 4;
    PDN->getDisplay()->setDrawColor(1)->drawBox(cursorX, cursorY, cellW / 2, cellH / 2);

    // Draw direction arrow inside cursor (if space allows)
    const char* arrow = getDirectionArrow(session.currentDirection);
    if (cellW >= 12) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->setDrawColor(2) // XOR for visibility
            ->drawText(arrow, cursorX + 1, cursorY + 1);
    }

    // Bottom separator line (y 56)
    PDN->getDisplay()->setDrawColor(1)->drawBox(0, 56, 128, 1);
}

/*
 * Get direction arrow character.
 */
const char* GhostRunnerGameplay::getDirectionArrow(int dir) {
    switch (dir) {
        case DIR_UP:    return "^";
        case DIR_RIGHT: return ">";
        case DIR_DOWN:  return "v";
        case DIR_LEFT:  return "<";
        default:        return "?";
    }
}
