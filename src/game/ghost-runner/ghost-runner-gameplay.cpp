#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"
#include "device/drivers/logger.hpp"
#include <string>
#include <algorithm>

static const char* TAG = "GhostRunnerGameplay";

// Helper function to check if note is in hit zone
static bool isInHitZone(int noteX, int hitZoneX, int hitZoneWidth) {
    return (noteX >= hitZoneX && noteX <= hitZoneX + hitZoneWidth);
}

// Helper function to check if note is in perfect zone
static bool isInPerfectZone(int noteX, int hitZoneX, int hitZoneWidth, int perfectWidth) {
    int centerX = hitZoneX + hitZoneWidth / 2;
    int perfectStart = centerX - perfectWidth / 2;
    int perfectEnd = centerX + perfectWidth / 2;
    return (noteX >= perfectStart && noteX <= perfectEnd);
}

// Helper function to grade a press
static NoteGrade gradePress(int noteX, const GhostRunnerConfig& config) {
    int hitZoneX = config.HIT_ZONE_X;
    int hitZoneWidth = config.hitZoneWidthPx;
    int perfectWidth = config.perfectZonePx;

    if (!isInHitZone(noteX, hitZoneX, hitZoneWidth)) {
        return NoteGrade::MISS;
    }

    if (isInPerfectZone(noteX, hitZoneX, hitZoneWidth, perfectWidth)) {
        return NoteGrade::PERFECT;
    }

    return NoteGrade::GOOD;
}

GhostRunnerGameplay::GhostRunnerGameplay(GhostRunner* game) : State(GHOST_GAMEPLAY) {
    this->game = game;
}

GhostRunnerGameplay::~GhostRunnerGameplay() {
    game = nullptr;
}

void GhostRunnerGameplay::onStateMounted(Device* PDN) {
    transitionToEvaluateState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Generate pattern for this round if not already generated
    if (session.currentPattern.empty()) {
        session.currentPattern = game->generatePattern(session.currentRound);
        session.currentNoteIndex = 0;
    }

    session.upPressed = false;
    session.downPressed = false;
    session.upHoldActive = false;
    session.downHoldActive = false;
    session.upHoldNoteIndex = -1;
    session.downHoldNoteIndex = -1;

    LOG_I(TAG, "Gameplay started, round %d, %zu notes",
          session.currentRound, session.currentPattern.size());

    // Set up UP button (PRIMARY) callback for press
    parameterizedCallbackFunction upPressCallback = [](void* ctx) {
        auto* state = static_cast<GhostRunnerGameplay*>(ctx);
        auto& sess = state->game->getSession();
        sess.upPressed = true;
    };

    // Set up UP button release callback using RELEASE interaction
    parameterizedCallbackFunction upReleaseCallback = [](void* ctx) {
        auto* state = static_cast<GhostRunnerGameplay*>(ctx);
        auto& sess = state->game->getSession();
        sess.upHoldActive = false;
    };

    // Set up DOWN button (SECONDARY) callback for press
    parameterizedCallbackFunction downPressCallback = [](void* ctx) {
        auto* state = static_cast<GhostRunnerGameplay*>(ctx);
        auto& sess = state->game->getSession();
        sess.downPressed = true;
    };

    // Set up DOWN button release callback using RELEASE interaction
    parameterizedCallbackFunction downReleaseCallback = [](void* ctx) {
        auto* state = static_cast<GhostRunnerGameplay*>(ctx);
        auto& sess = state->game->getSession();
        sess.downHoldActive = false;
    };

    PDN->getPrimaryButton()->setButtonPress(upPressCallback, this, ButtonInteraction::CLICK);
    PDN->getPrimaryButton()->setButtonPress(upReleaseCallback, this, ButtonInteraction::RELEASE);
    PDN->getSecondaryButton()->setButtonPress(downPressCallback, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(downReleaseCallback, this, ButtonInteraction::RELEASE);

    // Start chase animation
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::VERTICAL_CHASE;
    ledConfig.speed = 8;
    ledConfig.curve = EaseCurve::EASE_OUT;
    ledConfig.initialState = GHOST_RUNNER_GAMEPLAY_STATE;
    ledConfig.loop = true;
    PDN->getLightManager()->startAnimation(ledConfig);

    // Start movement timer
    ghostStepTimer.setTimer(config.ghostSpeedMs);
}

void GhostRunnerGameplay::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Process button presses
    if (session.upPressed) {
        session.upPressed = false;
        // Find nearest active note in UP lane
        int nearestIdx = -1;
        int nearestDist = 9999;
        for (size_t i = 0; i < session.currentPattern.size(); i++) {
            auto& note = session.currentPattern[i];
            if (note.lane == Lane::UP && note.active) {
                int dist = std::abs(note.xPosition - config.HIT_ZONE_X);
                if (dist < nearestDist) {
                    nearestDist = dist;
                    nearestIdx = i;
                }
            }
        }

        if (nearestIdx >= 0) {
            auto& note = session.currentPattern[nearestIdx];
            note.grade = gradePress(note.xPosition, config);

            if (note.type == NoteType::HOLD && note.grade != NoteGrade::MISS) {
                // Start hold tracking
                session.upHoldActive = true;
                session.upHoldNoteIndex = nearestIdx;
            } else {
                // Mark note as processed
                note.active = false;
                session.currentNoteIndex++;

                // Update score and stats
                if (note.grade == NoteGrade::PERFECT) {
                    session.score += 100;
                    session.perfectCount++;
                } else if (note.grade == NoteGrade::GOOD) {
                    session.score += 50;
                    session.goodCount++;
                } else {
                    session.missCount++;
                    session.livesRemaining--;
                }
            }
        }
    }

    if (session.downPressed) {
        session.downPressed = false;
        // Find nearest active note in DOWN lane
        int nearestIdx = -1;
        int nearestDist = 9999;
        for (size_t i = 0; i < session.currentPattern.size(); i++) {
            auto& note = session.currentPattern[i];
            if (note.lane == Lane::DOWN && note.active) {
                int dist = std::abs(note.xPosition - config.HIT_ZONE_X);
                if (dist < nearestDist) {
                    nearestDist = dist;
                    nearestIdx = i;
                }
            }
        }

        if (nearestIdx >= 0) {
            auto& note = session.currentPattern[nearestIdx];
            note.grade = gradePress(note.xPosition, config);

            if (note.type == NoteType::HOLD && note.grade != NoteGrade::MISS) {
                // Start hold tracking
                session.downHoldActive = true;
                session.downHoldNoteIndex = nearestIdx;
            } else {
                // Mark note as processed
                note.active = false;
                session.currentNoteIndex++;

                // Update score and stats
                if (note.grade == NoteGrade::PERFECT) {
                    session.score += 100;
                    session.perfectCount++;
                } else if (note.grade == NoteGrade::GOOD) {
                    session.score += 50;
                    session.goodCount++;
                } else {
                    session.missCount++;
                    session.livesRemaining--;
                }
            }
        }
    }

    // Check hold releases
    if (!session.upHoldActive && session.upHoldNoteIndex >= 0) {
        auto& note = session.currentPattern[session.upHoldNoteIndex];
        // Check if hold was released at correct position
        int tailX = note.xPosition - note.holdLength;
        if (tailX < config.HIT_ZONE_X + config.hitZoneWidthPx) {
            // Released too early
            note.grade = NoteGrade::MISS;
            session.missCount++;
            session.livesRemaining--;
        }
        note.active = false;
        session.currentNoteIndex++;
        session.upHoldNoteIndex = -1;
    }

    if (!session.downHoldActive && session.downHoldNoteIndex >= 0) {
        auto& note = session.currentPattern[session.downHoldNoteIndex];
        // Check if hold was released at correct position
        int tailX = note.xPosition - note.holdLength;
        if (tailX < config.HIT_ZONE_X + config.hitZoneWidthPx) {
            // Released too early
            note.grade = NoteGrade::MISS;
            session.missCount++;
            session.livesRemaining--;
        }
        note.active = false;
        session.currentNoteIndex++;
        session.downHoldNoteIndex = -1;
    }

    // Move notes on timer
    if (ghostStepTimer.expired()) {
        // Move all notes left by 1 pixel
        for (auto& note : session.currentPattern) {
            note.xPosition--;
        }

        // Check for missed notes (passed the hit zone without being hit)
        for (auto& note : session.currentPattern) {
            if (note.active && note.xPosition < config.HIT_ZONE_X - config.hitZoneWidthPx) {
                note.grade = NoteGrade::MISS;
                note.active = false;
                session.missCount++;
                session.livesRemaining--;
                session.currentNoteIndex++;
            }
        }

        // Restart timer
        ghostStepTimer.setTimer(config.ghostSpeedMs);
    }

    // Render display
    PDN->getDisplay()->invalidateScreen();

    // Draw HUD (y 0-9)
    std::string roundStr = "R" + std::to_string(session.currentRound + 1) +
                           "/" + std::to_string(config.rounds);
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(roundStr.c_str(), 2, 2);

    std::string scoreStr = "S:" + std::to_string(session.score);
    PDN->getDisplay()->drawText(scoreStr.c_str(), 50, 2);

    // Draw lives (simple rectangles)
    for (int i = 0; i < config.lives; i++) {
        int lx = 100 + i * 8;
        if (i < session.livesRemaining) {
            PDN->getDisplay()->setDrawColor(1)->drawBox(lx, 2, 5, 5);
        } else {
            PDN->getDisplay()->setDrawColor(1)->drawFrame(lx, 2, 5, 5);
        }
    }

    // Draw lanes and hit zones
    // UP lane (y 10-30)
    PDN->getDisplay()->setDrawColor(1)
        ->drawFrame(0, config.UP_LANE_Y, 128, config.UP_LANE_HEIGHT);
    PDN->getDisplay()->drawFrame(config.HIT_ZONE_X, config.UP_LANE_Y + 2,
                                  config.hitZoneWidthPx, config.UP_LANE_HEIGHT - 4);

    // Divider (y 31-32)
    PDN->getDisplay()->drawBox(0, config.DIVIDER_Y, 128, 2);

    // DOWN lane (y 33-53)
    PDN->getDisplay()->drawFrame(0, config.DOWN_LANE_Y, 128, config.DOWN_LANE_HEIGHT);
    PDN->getDisplay()->drawFrame(config.HIT_ZONE_X, config.DOWN_LANE_Y + 2,
                                  config.hitZoneWidthPx, config.DOWN_LANE_HEIGHT - 4);

    // Draw notes
    for (const auto& note : session.currentPattern) {
        if (!note.active && note.grade == NoteGrade::NONE) continue; // not yet visible

        int noteY = (note.lane == Lane::UP) ?
                    config.UP_LANE_Y + config.UP_LANE_HEIGHT / 2 - 4 :
                    config.DOWN_LANE_Y + config.DOWN_LANE_HEIGHT / 2 - 4;

        if (note.type == NoteType::PRESS) {
            // Draw arrow (simple triangle using boxes)
            PDN->getDisplay()->setDrawColor(1)
                ->drawBox(note.xPosition, noteY, 8, 8);
        } else {
            // Draw hold note (arrow head + bar)
            PDN->getDisplay()->setDrawColor(1)
                ->drawBox(note.xPosition, noteY, 8, 8);
            int tailX = note.xPosition - note.holdLength;
            if (tailX < note.xPosition) {
                PDN->getDisplay()->drawBox(tailX, noteY + 3, note.holdLength, 2);
            }
        }
    }

    // Draw controls hint (y 54-63)
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("UP:P  DN:S", 30, 56);

    PDN->getDisplay()->render();

    // Check for round end
    bool allProcessed = true;
    for (const auto& note : session.currentPattern) {
        if (note.active) {
            allProcessed = false;
            break;
        }
    }

    if (allProcessed || session.livesRemaining <= 0) {
        transitionToEvaluateState = true;
    }
}

void GhostRunnerGameplay::onStateDismounted(Device* PDN) {
    ghostStepTimer.invalidate();
    transitionToEvaluateState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool GhostRunnerGameplay::transitionToEvaluate() {
    return transitionToEvaluateState;
}
