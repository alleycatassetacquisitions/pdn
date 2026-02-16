#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"

/*
 * State map push order determines skipToState indices:
 * 0=Intro, 1=Show, 2=Gameplay, 3=Evaluate, 4=Win, 5=Lose
 */
void GhostRunner::populateStateMap() {
    seedRng(config.rngSeed);

    GhostRunnerIntro* intro = new GhostRunnerIntro(this);
    GhostRunnerShow* show = new GhostRunnerShow(this);
    GhostRunnerGameplay* gameplay = new GhostRunnerGameplay(this);
    GhostRunnerEvaluate* evaluate = new GhostRunnerEvaluate(this);
    GhostRunnerWin* win = new GhostRunnerWin(this);
    GhostRunnerLose* lose = new GhostRunnerLose(this);

    // Intro -> Show
    intro->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerIntro::transitionToShow, intro),
            show));

    // Show -> Gameplay
    show->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerShow::transitionToGameplay, show),
            gameplay));

    // Gameplay -> Evaluate
    gameplay->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerGameplay::transitionToEvaluate, gameplay),
            evaluate));

    // Evaluate -> Show (next round)
    evaluate->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerEvaluate::transitionToShow, evaluate),
            show));

    // Evaluate -> Win
    evaluate->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerEvaluate::transitionToWin, evaluate),
            win));

    // Evaluate -> Lose
    evaluate->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerEvaluate::transitionToLose, evaluate),
            lose));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerLose::transitionToIntro, lose),
            intro));

    stateMap.push_back(intro);      // index 0
    stateMap.push_back(show);       // index 1
    stateMap.push_back(gameplay);   // index 2
    stateMap.push_back(evaluate);   // index 3
    stateMap.push_back(win);        // index 4
    stateMap.push_back(lose);       // index 5
}

void GhostRunner::resetGame() {
    MiniGame::resetGame();
    session.reset();
    session.livesRemaining = config.lives;
}

/*
 * Generate a maze using recursive backtracker (DFS) algorithm.
 * Creates a perfect maze (exactly one path between any two cells).
 * Walls are stored as 4-bit bitmasks per cell.
 */
void GhostRunner::generateMaze(int round) {
    // Initialize all cells with all walls
    for (int i = 0; i < config.rows * config.cols; i++) {
        session.walls[i] = WALL_UP | WALL_RIGHT | WALL_DOWN | WALL_LEFT;
    }

    // Remove outer walls for cells on the edges (optional, but keeps maze within bounds)
    // Actually, we want walls on the outer edge, so skip this step

    // Recursive backtracker (DFS) maze generation
    std::vector<int> stack;
    bool visited[7 * 5] = {false};

    int startIdx = config.startRow * config.cols + config.startCol;
    stack.push_back(startIdx);
    visited[startIdx] = true;

    while (!stack.empty()) {
        int currentIdx = stack.back();
        int row = currentIdx / config.cols;
        int col = currentIdx % config.cols;

        // Find unvisited neighbors
        std::vector<int> neighbors;
        std::vector<int> directions;

        // UP
        if (row > 0 && !visited[(row - 1) * config.cols + col]) {
            neighbors.push_back((row - 1) * config.cols + col);
            directions.push_back(DIR_UP);
        }
        // RIGHT
        if (col < config.cols - 1 && !visited[row * config.cols + (col + 1)]) {
            neighbors.push_back(row * config.cols + (col + 1));
            directions.push_back(DIR_RIGHT);
        }
        // DOWN
        if (row < config.rows - 1 && !visited[(row + 1) * config.cols + col]) {
            neighbors.push_back((row + 1) * config.cols + col);
            directions.push_back(DIR_DOWN);
        }
        // LEFT
        if (col > 0 && !visited[row * config.cols + (col - 1)]) {
            neighbors.push_back(row * config.cols + (col - 1));
            directions.push_back(DIR_LEFT);
        }

        if (neighbors.empty()) {
            stack.pop_back();
        } else {
            // Choose random neighbor
            int choice = rand() % neighbors.size();
            int neighborIdx = neighbors[choice];
            int direction = directions[choice];

            // Remove wall between current and neighbor
            session.walls[currentIdx] &= ~(1 << direction);

            // Remove opposite wall from neighbor
            int oppositeDir = (direction + 2) % 4; // opposite direction
            session.walls[neighborIdx] &= ~(1 << oppositeDir);

            visited[neighborIdx] = true;
            stack.push_back(neighborIdx);
        }
    }
}

/*
 * Find solution path from start to exit using BFS.
 * Stores the path as a sequence of directions in solutionPath.
 */
void GhostRunner::findSolution() {
    int startIdx = config.startRow * config.cols + config.startCol;
    int exitIdx = config.exitRow * config.cols + config.exitCol;

    std::vector<int> queue;
    bool visited[7 * 5] = {false};
    int parent[7 * 5];
    int parentDir[7 * 5];

    for (int i = 0; i < 7 * 5; i++) {
        parent[i] = -1;
        parentDir[i] = -1;
    }

    queue.push_back(startIdx);
    visited[startIdx] = true;
    int queueHead = 0;

    while (queueHead < static_cast<int>(queue.size())) {
        int currentIdx = queue[queueHead++];
        if (currentIdx == exitIdx) break;

        int row = currentIdx / config.cols;
        int col = currentIdx % config.cols;

        // Check all 4 directions
        for (int dir = 0; dir < 4; dir++) {
            // Check if there's a wall in this direction
            if (session.walls[currentIdx] & (1 << dir)) continue;

            int nextRow = row;
            int nextCol = col;
            if (dir == DIR_UP) nextRow--;
            else if (dir == DIR_RIGHT) nextCol++;
            else if (dir == DIR_DOWN) nextRow++;
            else if (dir == DIR_LEFT) nextCol--;

            // Bounds check
            if (nextRow < 0 || nextRow >= config.rows ||
                nextCol < 0 || nextCol >= config.cols) continue;

            int nextIdx = nextRow * config.cols + nextCol;
            if (!visited[nextIdx]) {
                visited[nextIdx] = true;
                parent[nextIdx] = currentIdx;
                parentDir[nextIdx] = dir;
                queue.push_back(nextIdx);
            }
        }
    }

    // Reconstruct path
    session.solutionLength = 0;
    int current = exitIdx;
    while (current != startIdx && parent[current] != -1) {
        session.solutionPath[session.solutionLength++] = parentDir[current];
        current = parent[current];
    }

    // Reverse path (it's backward from exit to start)
    for (int i = 0; i < session.solutionLength / 2; i++) {
        int temp = session.solutionPath[i];
        session.solutionPath[i] = session.solutionPath[session.solutionLength - 1 - i];
        session.solutionPath[session.solutionLength - 1 - i] = temp;
    }
}
