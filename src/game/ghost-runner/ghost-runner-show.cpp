#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "GhostRunnerShow";

GhostRunnerShow::GhostRunnerShow(GhostRunner* game) : State(GHOST_SHOW) {
    this->game = game;
}

GhostRunnerShow::~GhostRunnerShow() {
    game = nullptr;
}

void GhostRunnerShow::onStateMounted(Device* PDN) {
    transitionToGameplayState = false;
    showingMaze = true;
    traceIndex = 0;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Reset round state
    session.cursorRow = config.startRow;
    session.cursorCol = config.startCol;
    session.currentDirection = DIR_RIGHT;
    session.stepsUsed = 0;
    session.bonkCount = 0;
    session.livesRemaining = config.lives; // reset lives each round

    LOG_I(TAG, "Round %d of %d - Generating maze %dx%d",
          session.currentRound + 1, config.rounds, config.cols, config.rows);

    // Generate maze and find solution
    game->generateMaze(session.currentRound);
    game->findSolution();

    LOG_I(TAG, "Solution length: %d moves", session.solutionLength);

    // Calculate preview time (shrinks per round)
    float shrinkFactor = 1.0f;
    for (int i = 0; i < session.currentRound; i++) {
        shrinkFactor *= config.previewShrinkPerRound;
    }
    int mazeTime = static_cast<int>(config.previewMazeMs * shrinkFactor);
    int traceTime = static_cast<int>(config.previewTraceMs * shrinkFactor);

    // Ensure minimum times
    if (mazeTime < 500) mazeTime = 500;
    if (traceTime < 500) traceTime = 500;

    mazePreviewTimer.setTimer(mazeTime);

    // Set up trace animation (100ms per step)
    traceStepTimer.setTimer(100);
    tracePreviewTimer.setTimer(traceTime);

    // Light pulse haptic feedback
    PDN->getHaptics()->setIntensity(128);
}

void GhostRunnerShow::onStateLoop(Device* PDN) {
    if (showingMaze) {
        // Phase 1: Show maze with walls
        drawMaze(PDN, true);

        if (mazePreviewTimer.expired()) {
            showingMaze = false;
            traceIndex = 0;
            LOG_I(TAG, "Transitioning to solution trace");
        }
    } else {
        // Phase 2: Trace solution path
        auto& session = game->getSession();

        // Animate cursor along solution path
        if (traceStepTimer.expired() && traceIndex < session.solutionLength) {
            traceIndex++;
            traceStepTimer.setTimer(100);
        }

        drawMaze(PDN, true); // show maze during trace
        drawSolutionText(PDN);

        // Draw animated cursor position
        int cursorRow = game->getConfig().startRow;
        int cursorCol = game->getConfig().startCol;
        for (int i = 0; i < traceIndex; i++) {
            int dir = session.solutionPath[i];
            if (dir == DIR_UP) cursorRow--;
            else if (dir == DIR_RIGHT) cursorCol++;
            else if (dir == DIR_DOWN) cursorRow++;
            else if (dir == DIR_LEFT) cursorCol--;
        }

        // Draw cursor at current trace position
        auto& config = game->getConfig();
        int cellW = 128 / config.cols;
        int cellH = 46 / config.rows;
        int cursorX = 1 + cursorCol * cellW + cellW / 4;
        int cursorY = 10 + cursorRow * cellH + cellH / 4;
        PDN->getDisplay()->setDrawColor(2) // XOR for visibility
            ->drawBox(cursorX, cursorY, cellW / 2, cellH / 2);

        PDN->getDisplay()->render();

        if (tracePreviewTimer.expired()) {
            PDN->getHaptics()->off();
            transitionToGameplayState = true;
        }
    }
}

void GhostRunnerShow::onStateDismounted(Device* PDN) {
    mazePreviewTimer.invalidate();
    tracePreviewTimer.invalidate();
    traceStepTimer.invalidate();
    transitionToGameplayState = false;
    PDN->getHaptics()->off();
}

bool GhostRunnerShow::transitionToGameplay() {
    return transitionToGameplayState;
}

/*
 * Draw the maze grid and walls.
 * showWalls: if true, draw wall lines; if false, only draw grid outlines.
 */
void GhostRunnerShow::drawMaze(Device* PDN, bool showWalls) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    PDN->getDisplay()->invalidateScreen();

    // HUD: Round and lives (y 0-8)
    std::string roundStr = "R" + std::to_string(session.currentRound + 1) +
                           "/" + std::to_string(config.rounds);
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(roundStr.c_str(), 2, 2);

    // Draw lives
    for (int i = 0; i < config.lives; i++) {
        int lx = 90 + i * 8;
        if (i < session.livesRemaining) {
            PDN->getDisplay()->setDrawColor(1)->drawBox(lx, 2, 5, 5);
        } else {
            PDN->getDisplay()->setDrawColor(1)->drawFrame(lx, 2, 5, 5);
        }
    }

    // Separator line (y 9)
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
        // Draw walls
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

    // Draw start marker (filled square)
    int startX = 1 + config.startCol * cellW + cellW / 3;
    int startY = 10 + config.startRow * cellH + cellH / 3;
    PDN->getDisplay()->setDrawColor(1)->drawBox(startX, startY, cellW / 3, cellH / 3);

    // Draw exit marker (outlined square)
    int exitX = 1 + config.exitCol * cellW + cellW / 3;
    int exitY = 10 + config.exitRow * cellH + cellH / 3;
    PDN->getDisplay()->setDrawColor(1)->drawFrame(exitX, exitY, cellW / 3, cellH / 3);

    // Bottom text (y 56-63)
    if (showingMaze) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("MEMORIZE THE PATH", 10, 57);
    }

    PDN->getDisplay()->render();
}

/*
 * Draw compressed solution sequence as text (e.g., "3> 1v 2< 1v 4>")
 */
void GhostRunnerShow::drawSolutionText(Device* PDN) {
    auto& session = game->getSession();

    // Compress solution into run-length segments
    std::string sequence;
    if (session.solutionLength > 0) {
        int count = 1;
        int currentDir = session.solutionPath[0];

        for (int i = 1; i <= session.solutionLength; i++) {
            if (i < session.solutionLength && session.solutionPath[i] == currentDir) {
                count++;
            } else {
                // Output segment
                sequence += std::to_string(count);
                if (currentDir == DIR_UP) sequence += "^";
                else if (currentDir == DIR_RIGHT) sequence += ">";
                else if (currentDir == DIR_DOWN) sequence += "v";
                else if (currentDir == DIR_LEFT) sequence += "<";
                sequence += " ";

                if (i < session.solutionLength) {
                    currentDir = session.solutionPath[i];
                    count = 1;
                }
            }
        }
    }

    // Draw sequence at bottom
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(sequence.c_str(), 2, 57);
}
