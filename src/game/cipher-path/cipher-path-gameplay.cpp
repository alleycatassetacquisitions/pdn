#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "CipherPathGameplay";

CipherPathGameplay::CipherPathGameplay(CipherPath* game) : State(CIPHER_GAMEPLAY) {
    this->game = game;
}

CipherPathGameplay::~CipherPathGameplay() {
    game = nullptr;
}

void CipherPathGameplay::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;
    needsEvaluation = false;
    circuitBroken = false;
    displayIsDirty = true;
    cursorVisible = true;

    LOG_I(TAG, "Gameplay started");

    auto& config = game->getConfig();
    auto& session = game->getSession();

    // Start flow active
    session.flowActive = true;
    session.flowTileIndex = 0;
    session.flowPixelInTile = 0;

    // Start LED animation — emerald electrical chase
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::VERTICAL_CHASE;
    ledConfig.speed = 20;
    ledConfig.curve = EaseCurve::LINEAR;
    ledConfig.initialState = CIPHER_PATH_GAMEPLAY_STATE;
    ledConfig.loopDelayMs = 0;
    ledConfig.loop = true;
    PDN->getLightManager()->startAnimation(ledConfig);

    // Set up flow timer based on difficulty
    int flowSpeed = config.flowSpeedMs - (session.currentRound * config.flowSpeedDecayMs);
    flowTimer.setTimer(flowSpeed);

    // Set up cursor blink timer
    cursorBlinkTimer.setTimer(300);

    // Button callbacks: UP = navigate to next tile, DOWN = rotate current tile
    parameterizedCallbackFunction upCallback = [](void* ctx) {
        auto* state = static_cast<CipherPathGameplay*>(ctx);
        auto& sess = state->game->getSession();

        // Move cursor to next path tile
        sess.cursorPathIndex++;
        if (sess.cursorPathIndex >= sess.pathLength) {
            sess.cursorPathIndex = 0;  // Wrap to first tile
        }

        // Haptic feedback
        state->displayIsDirty = true;
    };

    parameterizedCallbackFunction downCallback = [](void* ctx) {
        auto* state = static_cast<CipherPathGameplay*>(ctx);
        auto& sess = state->game->getSession();

        // Find current cursor cell index
        int cursorCellIndex = -1;
        for (int i = 0; i < 35; i++) {
            if (sess.pathOrder[i] == sess.cursorPathIndex) {
                cursorCellIndex = i;
                break;
            }
        }

        if (cursorCellIndex == -1) return;  // Safety check

        // Don't rotate terminals (input/output are fixed)
        if (sess.cursorPathIndex == 0 || sess.cursorPathIndex == sess.pathLength - 1) {
            return;
        }

        // Rotate tile 90° clockwise
        sess.tileRotation[cursorCellIndex] = (sess.tileRotation[cursorCellIndex] + 1) % 4;

        state->displayIsDirty = true;
    };

    PDN->getPrimaryButton()->setButtonPress(upCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(downCallback, this, ButtonInteraction::CLICK);

    renderGameplayScreen(PDN);
}

void CipherPathGameplay::onStateLoop(Device* PDN) {
    auto& session = game->getSession();

    // Check for evaluation transition (win/lose)
    if (needsEvaluation) {
        transitionToEvaluateState = true;
        return;
    }

    // Advance electricity flow
    if (session.flowActive && flowTimer.expired()) {
        advanceFlow(PDN);

        // Reset flow timer
        auto& config = game->getConfig();
        int flowSpeed = config.flowSpeedMs - (session.currentRound * config.flowSpeedDecayMs);
        flowTimer.setTimer(flowSpeed);

        displayIsDirty = true;
    }

    // Cursor blink animation
    if (cursorBlinkTimer.expired()) {
        cursorVisible = !cursorVisible;
        cursorBlinkTimer.setTimer(300);
        displayIsDirty = true;
    }

    // Redraw if dirty
    if (displayIsDirty) {
        renderGameplayScreen(PDN);
        displayIsDirty = false;
    }
}

void CipherPathGameplay::onStateDismounted(Device* PDN) {
    transitionToEvaluateState = false;
    needsEvaluation = false;
    circuitBroken = false;
    displayIsDirty = false;
    flowTimer.invalidate();
    cursorBlinkTimer.invalidate();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getHaptics()->off();
}

bool CipherPathGameplay::transitionToEvaluate() {
    return transitionToEvaluateState;
}

/*
 * Advance electricity flow by one pixel.
 * Checks for circuit completion (reached output terminal) or circuit break (disconnection).
 */
void CipherPathGameplay::advanceFlow(Device* PDN) {
    auto& session = game->getSession();

    session.flowPixelInTile++;

    // Check if flow has reached the exit edge of the current tile (~9 pixels per tile)
    if (session.flowPixelInTile >= 9) {
        // Move to next tile on path
        int nextTileIndex = session.flowTileIndex + 1;

        // Check if we've reached the output terminal (last tile on path)
        if (nextTileIndex >= session.pathLength) {
            // CIRCUIT COMPLETE — WIN
            session.flowActive = false;
            setNeedsEvaluation();
            return;
        }

        // Find current and next cell indices
        int currentCellIndex = -1;
        int nextCellIndex = -1;
        for (int i = 0; i < 35; i++) {
            if (session.pathOrder[i] == session.flowTileIndex) currentCellIndex = i;
            if (session.pathOrder[i] == nextTileIndex) nextCellIndex = i;
        }

        // Check connection between current tile and next tile
        if (!checkConnection(currentCellIndex, nextCellIndex)) {
            // CIRCUIT BREAK — LOSE
            session.flowActive = false;
            setCircuitBreak();
            setNeedsEvaluation();
            return;
        }

        // Advance to next tile
        session.flowTileIndex = nextTileIndex;
        session.flowPixelInTile = 0;
    }
}

/*
 * Check if two tiles are properly connected (wire segments align at shared edge).
 * Returns true if electricity can flow from fromIndex to toIndex.
 */
bool CipherPathGameplay::checkConnection(int fromIndex, int toIndex) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    int fromX = fromIndex % config.cols;
    int fromY = fromIndex / config.cols;
    int toX = toIndex % config.cols;
    int toY = toIndex / config.cols;

    // Determine which edge of fromIndex connects to which edge of toIndex
    int fromExitDir = -1;
    int toEntryDir = -1;

    if (toX > fromX) {
        // toIndex is to the right of fromIndex
        fromExitDir = 1;  // DIR_RIGHT (exit right edge of fromIndex)
        toEntryDir = 3;   // DIR_LEFT (enter left edge of toIndex)
    } else if (toX < fromX) {
        // toIndex is to the left of fromIndex
        fromExitDir = 3;  // DIR_LEFT
        toEntryDir = 1;   // DIR_RIGHT
    } else if (toY > fromY) {
        // toIndex is below fromIndex
        fromExitDir = 2;  // DIR_DOWN
        toEntryDir = 0;   // DIR_UP
    } else {
        // toIndex is above fromIndex
        fromExitDir = 0;  // DIR_UP
        toEntryDir = 2;   // DIR_DOWN
    }

    // Get connection bitmasks for both tiles
    int fromType = session.tileType[fromIndex];
    int fromRotation = session.tileRotation[fromIndex];
    int fromConnections = getTileConnections(fromType, fromRotation);

    int toType = session.tileType[toIndex];
    int toRotation = session.tileRotation[toIndex];
    int toConnections = getTileConnections(toType, toRotation);

    // Check if fromIndex has an exit on the correct edge
    if (!(fromConnections & (1 << fromExitDir))) {
        return false;
    }

    // Check if toIndex has an entry on the correct edge
    if (!(toConnections & (1 << toEntryDir))) {
        return false;
    }

    return true;
}

/*
 * Render the gameplay screen: grid with energized tiles, flow front, cursor highlight, HUD.
 */
void CipherPathGameplay::renderGameplayScreen(Device* PDN) {
    auto& config = game->getConfig();
    auto& session = game->getSession();

    PDN->getDisplay()->invalidateScreen();

    // Calculate grid position
    int gridWidth = config.cols * CELL_PITCH + 1;
    int gridHeight = config.rows * CELL_PITCH + 1;
    int gridX = 4;
    int gridY = (64 - gridHeight) / 2;

    // Draw top bar: round pips + flow progress bar
    if (config.rounds > 1) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
        int pipX = 4;
        for (int i = 0; i < config.rounds; i++) {
            const char* pip = (i < session.currentRound) ? "\x07" : (i == session.currentRound) ? "\x07" : "\x08";
            PDN->getDisplay()->drawText(pip, pipX, 3);
            pipX += 8;
        }
    }

    // Flow progress bar (top-right corner)
    int barWidth = 40;
    int barHeight = 4;
    int barX = 128 - barWidth - 4;
    int barY = 2;
    int fillWidth = (session.flowTileIndex * barWidth) / (session.pathLength > 0 ? session.pathLength : 1);
    PDN->getDisplay()->setDrawColor(1);
    PDN->getDisplay()->drawFrame(barX, barY, barWidth, barHeight);
    PDN->getDisplay()->drawBox(barX + 1, barY + 1, fillWidth, barHeight - 2);

    // Draw grid
    renderGrid(PDN, gridX, gridY);

    // Draw bottom bar: button labels
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawText("\x1E next", 4, 60)
        ->drawText("\x1F rotate", 70, 60);

    PDN->getDisplay()->render();
}

/*
 * Render the wire grid with energized tiles, flow front, and cursor highlight.
 */
void CipherPathGameplay::renderGrid(Device* PDN, int gridX, int gridY) {
    auto& config = game->getConfig();
    auto& session = game->getSession();

    // Draw grid frame and lines
    PDN->getDisplay()->setDrawColor(1);
    PDN->getDisplay()->drawFrame(gridX, gridY, config.cols * CELL_PITCH + 1, config.rows * CELL_PITCH + 1);

    // Draw vertical grid lines
    for (int col = 1; col < config.cols; col++) {
        int x = gridX + col * CELL_PITCH;
        PDN->getDisplay()->drawBox(x, gridY + 1, 1, config.rows * CELL_PITCH - 1);
    }

    // Draw horizontal grid lines
    for (int row = 1; row < config.rows; row++) {
        int y = gridY + row * CELL_PITCH;
        PDN->getDisplay()->drawBox(gridX + 1, y, config.cols * CELL_PITCH - 1, 1);
    }

    // Draw all tiles
    for (int row = 0; row < config.rows; row++) {
        for (int col = 0; col < config.cols; col++) {
            int cellIndex = row * config.cols + col;
            int cellX = gridX + col * CELL_PITCH + 1;
            int cellY = gridY + row * CELL_PITCH + 1;

            // Determine if this tile is the cursor position
            int cursorCellIndex = -1;
            for (int i = 0; i < 35; i++) {
                if (session.pathOrder[i] == session.cursorPathIndex) {
                    cursorCellIndex = i;
                    break;
                }
            }
            bool isCursor = (cellIndex == cursorCellIndex) && cursorVisible;

            // Determine if this tile is energized (electricity has passed through)
            int tilePathIndex = session.pathOrder[cellIndex];
            bool isEnergized = (tilePathIndex != -1 && tilePathIndex < session.flowTileIndex);

            // Determine if this is the flow front tile
            int flowPixel = (tilePathIndex == session.flowTileIndex) ? session.flowPixelInTile : -1;

            renderTile(PDN, cellX, cellY, cellIndex, isCursor, isEnergized, flowPixel);
        }
    }
}

/*
 * Render a single tile with cursor highlight, energization, and flow front effects.
 */
void CipherPathGameplay::renderTile(Device* PDN, int cellX, int cellY, int cellIndex, bool isCursor, bool isEnergized, int flowPixel) {
    auto& session = game->getSession();

    int tileType = session.tileType[cellIndex];
    int rotation = session.tileRotation[cellIndex];

    if (tileType == TILE_EMPTY) {
        // Draw cursor highlight on empty cell if cursor is here (shouldn't happen, but safety)
        if (isCursor) {
            PDN->getDisplay()->setDrawColor(2);  // XOR for blinking cursor
            PDN->getDisplay()->drawFrame(cellX, cellY, CELL_INNER, CELL_INNER);
        }
        return;
    }

    // Determine wire width
    bool isPath = (session.pathOrder[cellIndex] != -1);
    int wireWidth = isPath ? WIRE_WIDTH : NOISE_WIDTH;

    // Render tile (with energization inversion if needed)
    bool invert = isEnergized;
    renderWireTile(PDN, cellX, cellY, tileType, rotation, wireWidth, invert);

    // Cursor highlight (XOR frame around cell)
    if (isCursor) {
        PDN->getDisplay()->setDrawColor(2);  // XOR
        PDN->getDisplay()->drawFrame(cellX - 1, cellY - 1, CELL_INNER + 2, CELL_INNER + 2);
    }
}

/*
 * Render a wire tile with optional inversion (for energized tiles).
 * Inversion: white cell background with black wire channels (glowing effect).
 */
void CipherPathGameplay::renderWireTile(Device* PDN, int x, int y, int tileType, int rotation, int wireWidth, bool invert) {
    int connections = getTileConnections(tileType, rotation);

    if (invert) {
        // Energized tile: fill cell with white, draw black wire channels
        PDN->getDisplay()->setDrawColor(1);
        PDN->getDisplay()->drawBox(x, y, CELL_INNER, CELL_INNER);
        PDN->getDisplay()->setDrawColor(0);  // Black wire channels
    } else {
        // Normal tile: black background, white wire
        PDN->getDisplay()->setDrawColor(1);
    }

    // Wire center position
    int centerStart = (CELL_INNER - wireWidth) / 2;
    int centerEnd = centerStart + wireWidth;

    // Draw wire segments
    if (connections & DIR_UP) {
        PDN->getDisplay()->drawBox(x + centerStart, y, wireWidth, centerEnd);
    }
    if (connections & DIR_RIGHT) {
        PDN->getDisplay()->drawBox(x + centerStart, y + centerStart, CELL_INNER - centerStart, wireWidth);
    }
    if (connections & DIR_DOWN) {
        PDN->getDisplay()->drawBox(x + centerStart, y + centerStart, wireWidth, CELL_INNER - centerStart);
    }
    if (connections & DIR_LEFT) {
        PDN->getDisplay()->drawBox(x, y + centerStart, centerEnd, wireWidth);
    }

    // Reset draw color to normal
    PDN->getDisplay()->setDrawColor(1);
}
