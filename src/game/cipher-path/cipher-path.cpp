#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include "game/cipher-path/cipher-path-resources.hpp"

void CipherPath::populateStateMap() {
    seedRng(config.rngSeed);

    CipherPathIntro* intro = new CipherPathIntro(this);
    CipherPathShow* show = new CipherPathShow(this);
    CipherPathGameplay* gameplay = new CipherPathGameplay(this);
    CipherPathEvaluate* evaluate = new CipherPathEvaluate(this);
    CipherPathWin* win = new CipherPathWin(this);
    CipherPathLose* lose = new CipherPathLose(this);

    // Intro -> Show
    intro->addTransition(
        new StateTransition(
            std::bind(&CipherPathIntro::transitionToShow, intro),
            show));

    // Show -> Gameplay
    show->addTransition(
        new StateTransition(
            std::bind(&CipherPathShow::transitionToGameplay, show),
            gameplay));

    // Gameplay -> Evaluate
    gameplay->addTransition(
        new StateTransition(
            std::bind(&CipherPathGameplay::transitionToEvaluate, gameplay),
            evaluate));

    // Evaluate -> Show (next round)
    evaluate->addTransition(
        new StateTransition(
            std::bind(&CipherPathEvaluate::transitionToShow, evaluate),
            show));

    // Evaluate -> Win
    evaluate->addTransition(
        new StateTransition(
            std::bind(&CipherPathEvaluate::transitionToWin, evaluate),
            win));

    // Evaluate -> Lose
    evaluate->addTransition(
        new StateTransition(
            std::bind(&CipherPathEvaluate::transitionToLose, evaluate),
            lose));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&CipherPathWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&CipherPathLose::transitionToIntro, lose),
            intro));

    // Push order: intro(0), show(1), gameplay(2), evaluate(3), win(4), lose(5)
    stateMap.push_back(intro);
    stateMap.push_back(show);
    stateMap.push_back(gameplay);
    stateMap.push_back(evaluate);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void CipherPath::resetGame() {
    MiniGame::resetGame();
    session.reset();
}

/*
 * Generate path using random walk with directional bias toward target.
 * Algorithm: Start at (0,0), end at (cols-1, rows-1). At each step,
 * 60% chance to move toward target (reducing Manhattan distance),
 * 40% chance to pick random unvisited neighbor. Backtrack on dead ends.
 * Result: a meandering but solvable path from input to output terminal.
 */
void CipherPath::generatePath() {
    auto& sess = session;
    auto& cfg = config;

    // Clear session state
    for (int i = 0; i < 35; i++) {
        sess.tileType[i] = TILE_EMPTY;
        sess.tileRotation[i] = 0;
        sess.correctRotation[i] = 0;
        sess.pathOrder[i] = -1;
    }
    sess.pathLength = 0;
    sess.flowTileIndex = 0;
    sess.flowPixelInTile = 0;
    sess.flowActive = false;
    sess.cursorPathIndex = 0;

    // Path generation via random walk
    int path[35];          // Grid indices along the path
    bool visited[35];      // Track which cells have been visited
    for (int i = 0; i < 35; i++) visited[i] = false;

    int pathLen = 0;
    int targetX = cfg.cols - 1;
    int targetY = cfg.rows - 1;
    int targetIndex = targetY * cfg.cols + targetX;

    path[pathLen++] = 0;   // Start at (0, 0)
    visited[0] = true;
    int currentIndex = 0;

    // Random walk until we reach target
    while (currentIndex != targetIndex) {
        int cx = currentIndex % cfg.cols;
        int cy = currentIndex / cfg.cols;

        // Find unvisited neighbors (up, right, down, left)
        int neighbors[4];
        int neighborCount = 0;

        // Up
        if (cy > 0 && !visited[currentIndex - cfg.cols]) {
            neighbors[neighborCount++] = currentIndex - cfg.cols;
        }
        // Right
        if (cx < cfg.cols - 1 && !visited[currentIndex + 1]) {
            neighbors[neighborCount++] = currentIndex + 1;
        }
        // Down
        if (cy < cfg.rows - 1 && !visited[currentIndex + cfg.cols]) {
            neighbors[neighborCount++] = currentIndex + cfg.cols;
        }
        // Left
        if (cx > 0 && !visited[currentIndex - 1]) {
            neighbors[neighborCount++] = currentIndex - 1;
        }

        if (neighborCount == 0) {
            // Dead end — backtrack
            pathLen--;
            if (pathLen == 0) break;  // Shouldn't happen, but safety check
            currentIndex = path[pathLen - 1];
            continue;
        }

        // Choose next cell: 60% bias toward target, 40% random
        int nextIndex;
        if (rand() % 100 < 60) {
            // Pick neighbor that reduces Manhattan distance to target
            int bestDist = 9999;
            int bestNeighbor = neighbors[0];
            for (int i = 0; i < neighborCount; i++) {
                int nx = neighbors[i] % cfg.cols;
                int ny = neighbors[i] / cfg.cols;
                int dist = (targetX - nx > 0 ? targetX - nx : nx - targetX)
                         + (targetY - ny > 0 ? targetY - ny : ny - targetY);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestNeighbor = neighbors[i];
                }
            }
            nextIndex = bestNeighbor;
        } else {
            // Random neighbor
            nextIndex = neighbors[rand() % neighborCount];
        }

        path[pathLen++] = nextIndex;
        visited[nextIndex] = true;
        currentIndex = nextIndex;
    }

    // Assign tile types and rotations based on path
    sess.pathLength = pathLen;
    for (int i = 0; i < pathLen; i++) {
        int idx = path[i];
        sess.pathOrder[idx] = i;

        int x = idx % cfg.cols;
        int y = idx / cfg.cols;

        // Determine entry and exit directions
        int entryDir = -1;  // -1 = none (input terminal)
        int exitDir = -1;   // -1 = none (output terminal)

        if (i > 0) {
            int prevIdx = path[i - 1];
            int px = prevIdx % cfg.cols;
            int py = prevIdx / cfg.cols;
            if (px < x) entryDir = 3;  // Entry from left (DIR_LEFT)
            else if (px > x) entryDir = 1;  // Entry from right (DIR_RIGHT)
            else if (py < y) entryDir = 0;  // Entry from up (DIR_UP)
            else entryDir = 2;  // Entry from down (DIR_DOWN)
        }

        if (i < pathLen - 1) {
            int nextIdx = path[i + 1];
            int nx = nextIdx % cfg.cols;
            int ny = nextIdx / cfg.cols;
            if (nx < x) exitDir = 3;  // Exit to left
            else if (nx > x) exitDir = 1;  // Exit to right
            else if (ny < y) exitDir = 0;  // Exit to up
            else exitDir = 2;  // Exit to down
        }

        // Assign tile type and correct rotation
        if (i == 0) {
            // Input terminal (endpoint pointing toward next tile)
            sess.tileType[idx] = TILE_ENDPOINT;
            sess.correctRotation[idx] = exitDir;
        } else if (i == pathLen - 1) {
            // Output terminal (endpoint pointing back toward previous tile)
            sess.tileType[idx] = TILE_ENDPOINT;
            // Endpoint needs to point INTO the cell, so invert entry direction
            sess.correctRotation[idx] = (entryDir + 2) % 4;
        } else {
            // Internal path tile
            if ((entryDir + 2) % 4 == exitDir) {
                // Straight tile (opposite edges)
                sess.tileType[idx] = TILE_STRAIGHT;
                // Rotation: align entry to DIR_LEFT (base rotation)
                sess.correctRotation[idx] = (entryDir - 3 + 4) % 4;
            } else {
                // Elbow tile (adjacent edges)
                sess.tileType[idx] = TILE_ELBOW;
                // Base elbow: DIR_UP | DIR_RIGHT (connections 0x03)
                // Find rotation where entry and exit match
                for (int rot = 0; rot < 4; rot++) {
                    int conn = getTileConnections(TILE_ELBOW, rot);
                    int entryBit = 1 << entryDir;
                    int exitBit = 1 << exitDir;
                    if ((conn & entryBit) && (conn & exitBit)) {
                        sess.correctRotation[idx] = rot;
                        break;
                    }
                }
            }
        }

        // Scramble non-terminal tiles
        if (i == 0 || i == pathLen - 1) {
            // Terminals are FIXED (always correct orientation)
            sess.tileRotation[idx] = sess.correctRotation[idx];
        } else {
            // Scramble: rotate by 1, 2, or 3 steps (never 0)
            int offset = 1 + (rand() % 3);
            sess.tileRotation[idx] = (sess.correctRotation[idx] + offset) % 4;
        }
    }
}

/*
 * Add noise tiles to non-path cells. Noise tiles are thin (1px) decorative
 * wire segments that create visual complexity. They don't carry electricity
 * and the cursor skips them. noisePercent (30% easy, 40% hard) of non-path
 * cells get random wire segments.
 */
void CipherPath::generateNoise() {
    auto& sess = session;
    auto& cfg = config;

    int totalCells = cfg.cols * cfg.rows;
    for (int i = 0; i < totalCells; i++) {
        // Skip path tiles
        if (sess.pathOrder[i] != -1) continue;

        // noisePercent chance to place a noise tile
        if (rand() % 100 < cfg.noisePercent) {
            // Random tile type: straight, elbow, t-junction, or cross
            int type = 1 + (rand() % 4);  // 1-4 (STRAIGHT to CROSS)
            sess.tileType[i] = type;
            sess.tileRotation[i] = rand() % 4;
            sess.correctRotation[i] = 0;  // Noise tiles don't have a "correct" rotation
        }
    }
}
