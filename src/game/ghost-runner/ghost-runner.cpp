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
 * Generate a pattern of notes for the given round.
 * Uses seeded RNG for deterministic patterns.
 * Applies constraints:
 * - Minimum spacing between notes (prevents impossible patterns)
 * - Maximum simultaneous notes based on difficulty
 * - Easy: sequential only (dualLaneChance = 0)
 * - Hard: overlapping dual-lane allowed
 */
std::vector<Note> GhostRunner::generatePattern(int round) {
    std::vector<Note> pattern;

    // Calculate speed for this round (speed increases per round in hard mode)
    float speedMultiplier = 1.0f;
    for (int i = 0; i < round; i++) {
        speedMultiplier *= config.speedRampPerRound;
    }

    int effectiveSpeed = static_cast<int>(config.ghostSpeedMs / speedMultiplier);
    if (effectiveSpeed < 20) effectiveSpeed = 20; // minimum speed cap

    // Minimum spacing between notes (in pixels)
    int minSpacing = config.hitZoneWidthPx + 30;

    int currentX = 128; // start offscreen right

    for (int i = 0; i < config.notesPerRound; i++) {
        // Determine note type
        float typeRoll = static_cast<float>(rand()) / RAND_MAX;
        NoteType type = (typeRoll < config.holdNoteChance) ? NoteType::HOLD : NoteType::PRESS;

        // Determine lane
        Lane lane = ((rand() % 2) == 0) ? Lane::UP : Lane::DOWN;

        Note note;
        note.type = type;
        note.lane = lane;
        note.xPosition = currentX;
        note.active = true;

        if (type == NoteType::HOLD) {
            // Calculate hold length based on duration and speed
            int holdPixels = config.holdDurationMs / effectiveSpeed;
            note.holdLength = holdPixels;
        }

        pattern.push_back(note);

        // Check for dual-lane note (only if not last note)
        if (i < config.notesPerRound - 1) {
            float dualRoll = static_cast<float>(rand()) / RAND_MAX;
            if (dualRoll < config.dualLaneChance) {
                // Add simultaneous note in opposite lane
                Note dualNote = note;
                dualNote.lane = (lane == Lane::UP) ? Lane::DOWN : Lane::UP;
                pattern.push_back(dualNote);
                i++; // count this as a note
            }
        }

        // Advance position for next note
        if (type == NoteType::HOLD) {
            currentX += note.holdLength + minSpacing;
        } else {
            currentX += minSpacing;
        }
    }

    return pattern;
}
