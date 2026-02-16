#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>

static const char* TAG = "CipherPathShow";

CipherPathShow::CipherPathShow(CipherPath* game) : State(CIPHER_SHOW) {
    this->game = game;
}

CipherPathShow::~CipherPathShow() {
    game = nullptr;
}

void CipherPathShow::onStateMounted(Device* PDN) {
    transitionToGameplayState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Generate path and noise tiles for this round
    game->generatePath();
    game->generateNoise();

    // Reset flow state
    session.flowTileIndex = 0;
    session.flowPixelInTile = 0;
    session.flowActive = false;
    session.cursorPathIndex = 0;

    LOG_I(TAG, "Round %d of %d", session.currentRound + 1, config.rounds);

    // Render the grid preview
    renderShowScreen(PDN);

    // Haptic pulse feedback
    PDN->getHaptics()->setIntensity(100);

    // Start show timer (2 seconds preview)
    showTimer.setTimer(SHOW_DURATION_MS);
}

void CipherPathShow::onStateLoop(Device* PDN) {
    if (showTimer.expired()) {
        PDN->getHaptics()->off();
        transitionToGameplayState = true;
    }
}

void CipherPathShow::onStateDismounted(Device* PDN) {
    showTimer.invalidate();
    transitionToGameplayState = false;
    PDN->getHaptics()->off();
}

bool CipherPathShow::transitionToGameplay() {
    return transitionToGameplayState;
}

/*
 * Render show screen: grid with scrambled tiles + round pips (hard mode).
 * Grid is centered on screen with HUD panel on the right.
 */
void CipherPathShow::renderShowScreen(Device* PDN) {
    auto& config = game->getConfig();
    auto& session = game->getSession();

    PDN->getDisplay()->invalidateScreen();

    // Calculate grid position (center-left on screen)
    int gridWidth = config.cols * CELL_PITCH + 1;
    int gridHeight = config.rows * CELL_PITCH + 1;
    int gridX = 4;  // Left margin
    int gridY = (64 - gridHeight) / 2;  // Vertically centered

    // Draw top bar with round pips (hard mode only)
    if (config.rounds > 1) {
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT);
        int pipX = 4;
        for (int i = 0; i < config.rounds; i++) {
            const char* pip = (i < session.currentRound) ? "\x07" : "\x08";  // Filled / empty circle
            PDN->getDisplay()->drawText(pip, pipX, 3);
            pipX += 8;
        }
    }

    // Draw the grid
    renderGrid(PDN, gridX, gridY);

    // Right HUD panel: game title (vertical or small)
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawText("CIPHER", gridX + gridWidth + 8, 20)
        ->drawText("PATH", gridX + gridWidth + 8, 32);

    PDN->getDisplay()->render();
}

/*
 * Render the wire grid with all tiles in their current (scrambled) orientations.
 */
void CipherPathShow::renderGrid(Device* PDN, int gridX, int gridY) {
    auto& config = game->getConfig();

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
            renderTile(PDN, cellX, cellY, cellIndex);
        }
    }
}

/*
 * Render a single tile at the given screen position.
 */
void CipherPathShow::renderTile(Device* PDN, int cellX, int cellY, int cellIndex) {
    auto& session = game->getSession();

    int tileType = session.tileType[cellIndex];
    int rotation = session.tileRotation[cellIndex];

    if (tileType == TILE_EMPTY) return;  // Skip empty cells

    // Determine wire width: path tiles are bold (2px), noise tiles are thin (1px)
    bool isPath = (session.pathOrder[cellIndex] != -1);
    int wireWidth = isPath ? WIRE_WIDTH : NOISE_WIDTH;

    renderWireTile(PDN, cellX, cellY, tileType, rotation, wireWidth);
}

/*
 * Render a wire tile with the specified type, rotation, and wire width.
 * Draws wire segments using drawBox primitives. Uses direction bitmask
 * from getTileConnections() to determine which segments to draw.
 */
void CipherPathShow::renderWireTile(Device* PDN, int x, int y, int tileType, int rotation, int wireWidth) {
    int connections = getTileConnections(tileType, rotation);

    PDN->getDisplay()->setDrawColor(1);  // White wire on black background

    // Wire center position within 8x8 cell (centered at pixels 3-4 for 2px wire)
    int centerStart = (CELL_INNER - wireWidth) / 2;  // 3 for 2px, 3.5->3 for 1px
    int centerEnd = centerStart + wireWidth;

    // Draw wire segments based on connection bitmask
    if (connections & DIR_UP) {
        // Vertical segment from top edge to center
        PDN->getDisplay()->drawBox(x + centerStart, y, wireWidth, centerEnd);
    }
    if (connections & DIR_RIGHT) {
        // Horizontal segment from center to right edge
        PDN->getDisplay()->drawBox(x + centerStart, y + centerStart, CELL_INNER - centerStart, wireWidth);
    }
    if (connections & DIR_DOWN) {
        // Vertical segment from center to bottom edge
        PDN->getDisplay()->drawBox(x + centerStart, y + centerStart, wireWidth, CELL_INNER - centerStart);
    }
    if (connections & DIR_LEFT) {
        // Horizontal segment from left edge to center
        PDN->getDisplay()->drawBox(x, y + centerStart, centerEnd, wireWidth);
    }
}
