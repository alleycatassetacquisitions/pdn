#include "game/breach-defense/breach-defense-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "device/drivers/logger.hpp"
#include <cstdio>
#include <cstdlib>

static const char* TAG = "BreachDefenseGameplay";

/*
 * Display layout constants for 128x64 OLED
 */
static constexpr int SCREEN_WIDTH = 128;
static constexpr int SCREEN_HEIGHT = 64;

// HUD region (top)
static constexpr int HUD_TOP = 0;
static constexpr int HUD_HEIGHT = 8;
static constexpr int HUD_SEPARATOR_Y = 8;

// Game area (middle)
static constexpr int GAME_AREA_TOP = 9;
static constexpr int GAME_AREA_BOTTOM = 54;
static constexpr int GAME_AREA_HEIGHT = 46;
static constexpr int GAME_SEPARATOR_Y = 55;

// Controls region (bottom)
static constexpr int CONTROLS_TOP = 56;
static constexpr int CONTROLS_HEIGHT = 8;

// Shield (Pong paddle)
static constexpr int SHIELD_X = 2;
static constexpr int SHIELD_WIDTH = 6;

// Defense line (Pong net, rotated)
static constexpr int DEFENSE_LINE_X = 8;

// Threat spawn and travel
static constexpr int THREAT_SPAWN_X = 124;

BreachDefenseGameplay::BreachDefenseGameplay(BreachDefense* game) : State(BREACH_GAMEPLAY) {
    this->game = game;
}

BreachDefenseGameplay::~BreachDefenseGameplay() {
    game = nullptr;
}

void BreachDefenseGameplay::onStateMounted(Device* PDN) {
    transitionToWinState = false;
    transitionToLoseState = false;

    LOG_I(TAG, "Combo gameplay started — %d lanes, %d threats",
          game->getConfig().numLanes, game->getConfig().totalThreats);

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Seed RNG for threat lane generation
    game->seedRng(config.rngSeed);

    // Set up button callbacks for shield movement
    parameterizedCallbackFunction upCb = [](void* ctx) {
        auto* state = static_cast<BreachDefenseGameplay*>(ctx);
        auto& sess = state->game->getSession();
        if (sess.shieldLane > 0) {
            sess.shieldLane--;
        }
    };
    PDN->getPrimaryButton()->setButtonPress(upCb, this, ButtonInteraction::CLICK);

    parameterizedCallbackFunction downCb = [](void* ctx) {
        auto* state = static_cast<BreachDefenseGameplay*>(ctx);
        auto& sess = state->game->getSession();
        if (sess.shieldLane < state->game->getConfig().numLanes - 1) {
            sess.shieldLane++;
        }
    };
    PDN->getSecondaryButton()->setButtonPress(downCb, this, ButtonInteraction::CLICK);

    // Chase LED animation during gameplay
    PDN->getLightManager()->startAnimation({
        AnimationType::VERTICAL_CHASE, true, 4, EaseCurve::LINEAR,
        LEDState(), 0
    });

    // Spawn first threat immediately
    if (session.nextSpawnIndex < config.totalThreats) {
        session.threats[0].lane = rand() % config.numLanes;
        session.threats[0].position = 0;
        session.threats[0].active = true;
        session.nextSpawnIndex++;
        LOG_I(TAG, "Spawned threat 0 in lane %d", session.threats[0].lane);
    }

    // Start spawn timer for next threat
    spawnTimer.setTimer(config.spawnIntervalMs);

    // Start threat advancement timer
    for (int i = 0; i < 3; i++) {
        threatTimers[i].setTimer(config.threatSpeedMs);
    }
}

void BreachDefenseGameplay::onStateLoop(Device* PDN) {
    auto& session = game->getSession();
    auto& config = game->getConfig();

    // SPAWN CHECK — fixed rhythm combo pipeline
    if (spawnTimer.expired() && session.nextSpawnIndex < config.totalThreats) {
        // Count active threats
        int activeCount = 0;
        for (int i = 0; i < 3; i++) {
            if (session.threats[i].active) activeCount++;
        }

        // Find inactive slot and spawn if under maxOverlap
        if (activeCount < config.maxOverlap) {
            for (int i = 0; i < 3; i++) {
                if (!session.threats[i].active) {
                    session.threats[i].lane = rand() % config.numLanes;
                    session.threats[i].position = 0;
                    session.threats[i].active = true;
                    LOG_I(TAG, "Spawned threat %d in lane %d",
                          session.nextSpawnIndex, session.threats[i].lane);
                    session.nextSpawnIndex++;
                    break;
                }
            }
        }

        spawnTimer.setTimer(config.spawnIntervalMs);
    }

    // ADVANCE THREATS
    for (int i = 0; i < 3; i++) {
        if (!session.threats[i].active) continue;

        if (threatTimers[i].expired()) {
            session.threats[i].position++;

            // INLINE EVALUATION at defense line
            if (session.threats[i].position >= config.threatDistance) {
                bool blocked = (session.shieldLane == session.threats[i].lane);

                if (blocked) {
                    // BLOCKED — XOR flash + light haptic
                    session.score += 100;
                    PDN->getHaptics()->setIntensity(150);
                    LOG_I(TAG, "BLOCKED! Threat %d. Score: %d", i, session.score);

                    // TODO: Lane XOR flash (2-3 frames) — requires draw color state management
                } else {
                    // BREACH — full invert + heavy haptic
                    session.breaches++;
                    PDN->getHaptics()->setIntensity(255);
                    LOG_I(TAG, "BREACH! Threat %d. Breaches: %d/%d",
                          i, session.breaches, config.missesAllowed);

                    // TODO: Lane invert flash (3 frames) — requires draw color state management
                }

                // Resolve threat
                session.threats[i].active = false;
                session.threatsResolved++;

                // Turn off haptic after brief pulse
                PDN->getHaptics()->off();
            } else {
                // Reset timer for next step
                threatTimers[i].setTimer(config.threatSpeedMs);
            }
        }
    }

    // END CONDITION CHECK
    if (session.breaches > config.missesAllowed) {
        transitionToLoseState = true;
    } else if (session.threatsResolved >= config.totalThreats) {
        transitionToWinState = true;
    }

    // RENDER — continuous every frame
    drawFrame(PDN);
}

void BreachDefenseGameplay::onStateDismounted(Device* PDN) {
    spawnTimer.invalidate();
    for (int i = 0; i < 3; i++) {
        threatTimers[i].invalidate();
    }
    transitionToWinState = false;
    transitionToLoseState = false;
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    PDN->getHaptics()->off();
}

bool BreachDefenseGameplay::transitionToWin() {
    return transitionToWinState;
}

bool BreachDefenseGameplay::transitionToLose() {
    return transitionToLoseState;
}

/*
 * Helper functions for rendering
 */

// Calculate lane Y position (center of lane)
static int getLaneCenterY(int lane, int numLanes) {
    if (numLanes == 3) {
        // Easy: 3 lanes, each ~15px tall
        int laneHeight = 15;
        return GAME_AREA_TOP + (lane * (laneHeight + 1)) + (laneHeight / 2);
    } else {
        // Hard: 5 lanes, each ~9px tall
        int laneHeight = 9;
        return GAME_AREA_TOP + (lane * (laneHeight + 1)) + (laneHeight / 2);
    }
}

// Calculate shield Y position (top-left corner)
static int getShieldY(int lane, int numLanes) {
    if (numLanes == 3) {
        // Easy: shield height 12px (lane 15px - 2px margin)
        int laneHeight = 15;
        int shieldHeight = 12;
        return GAME_AREA_TOP + (lane * (laneHeight + 1)) + ((laneHeight - shieldHeight) / 2);
    } else {
        // Hard: shield height 6px (lane 9px - 2px margin)
        int laneHeight = 9;
        int shieldHeight = 6;
        return GAME_AREA_TOP + (lane * (laneHeight + 1)) + ((laneHeight - shieldHeight) / 2);
    }
}

// Calculate threat X position from position progress
static int getThreatX(int position, int threatDistance) {
    // Map position [0, threatDistance] to x [THREAT_SPAWN_X, DEFENSE_LINE_X]
    int travelDistance = THREAT_SPAWN_X - DEFENSE_LINE_X;
    return THREAT_SPAWN_X - ((position * travelDistance) / threatDistance);
}

void BreachDefenseGameplay::drawFrame(Device* PDN) {
    auto* display = PDN->getDisplay();
    auto& session = game->getSession();
    auto& config = game->getConfig();

    display->invalidateScreen();

    // HUD — progress bar, lives, score
    drawHUD(PDN);

    // Separators
    display->drawBox(0, HUD_SEPARATOR_Y, SCREEN_WIDTH, 1);
    display->drawBox(0, GAME_SEPARATOR_Y, SCREEN_WIDTH, 1);

    // Game area — lanes, defense line, shield, threats
    drawLanes(PDN);
    drawDefenseLine(PDN);
    drawShield(PDN);
    drawThreats(PDN);

    // Controls bar
    drawControls(PDN);

    display->render();
}

void BreachDefenseGameplay::drawHUD(Device* PDN) {
    auto* display = PDN->getDisplay();
    auto& session = game->getSession();
    auto& config = game->getConfig();

    display->setGlyphMode(FontMode::TEXT);

    // Progress bar (left, ~60px wide)
    int barWidth = 60;
    int filledWidth = (session.threatsResolved * barWidth) / config.totalThreats;
    int remainingWidth = barWidth - filledWidth;

    if (filledWidth > 0) {
        display->drawBox(2, 1, filledWidth, 5);
    }
    if (remainingWidth > 0) {
        display->drawFrame(2 + filledWidth, 1, remainingWidth, 5);
    }

    // Lives pips (center-right, ~70px from left)
    int pipX = 70;
    int remaining = config.missesAllowed - session.breaches;
    for (int i = 0; i < config.missesAllowed; i++) {
        if (i < remaining) {
            // Filled pip (life remaining)
            display->drawBox(pipX + (i * 5), 2, 3, 3);
        } else {
            // Empty pip (life lost)
            display->drawFrame(pipX + (i * 5), 2, 3, 3);
        }
    }

    // Score (right-aligned)
    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "%d", session.score);
    display->drawText(scoreStr, SCREEN_WIDTH - 30, 7);
}

void BreachDefenseGameplay::drawLanes(Device* PDN) {
    auto* display = PDN->getDisplay();
    auto& config = game->getConfig();

    // Draw horizontal dotted dividers between lanes
    if (config.numLanes == 3) {
        // Easy: 2 dividers at y=24, y=40
        int dividerY1 = GAME_AREA_TOP + 15;
        int dividerY2 = GAME_AREA_TOP + 31;
        for (int x = 0; x < SCREEN_WIDTH; x += 6) {
            display->drawBox(x, dividerY1, 2, 1);
            display->drawBox(x, dividerY2, 2, 1);
        }
    } else {
        // Hard: 4 dividers
        for (int lane = 0; lane < config.numLanes - 1; lane++) {
            int dividerY = GAME_AREA_TOP + ((lane + 1) * 10) - 1;
            for (int x = 0; x < SCREEN_WIDTH; x += 6) {
                display->drawBox(x, dividerY, 2, 1);
            }
        }
    }
}

void BreachDefenseGameplay::drawDefenseLine(Device* PDN) {
    auto* display = PDN->getDisplay();

    // Vertical dashed line at DEFENSE_LINE_X
    for (int y = GAME_AREA_TOP; y < GAME_AREA_BOTTOM; y += 8) {
        display->drawBox(DEFENSE_LINE_X, y, 1, 4);
    }
}

void BreachDefenseGameplay::drawShield(Device* PDN) {
    auto* display = PDN->getDisplay();
    auto& session = game->getSession();
    auto& config = game->getConfig();

    int shieldY = getShieldY(session.shieldLane, config.numLanes);
    int shieldHeight = (config.numLanes == 3) ? 12 : 6;

    display->drawBox(SHIELD_X, shieldY, SHIELD_WIDTH, shieldHeight);
}

void BreachDefenseGameplay::drawThreats(Device* PDN) {
    auto* display = PDN->getDisplay();
    auto& session = game->getSession();
    auto& config = game->getConfig();

    int threatSize = (config.numLanes == 3) ? 4 : 3;

    for (int i = 0; i < 3; i++) {
        if (!session.threats[i].active) continue;

        int threatX = getThreatX(session.threats[i].position, config.threatDistance);
        int threatY = getLaneCenterY(session.threats[i].lane, config.numLanes) - (threatSize / 2);

        display->drawBox(threatX, threatY, threatSize, threatSize);
    }
}

void BreachDefenseGameplay::drawControls(Device* PDN) {
    auto* display = PDN->getDisplay();

    display->setGlyphMode(FontMode::TEXT);

    // Up button indicator (left)
    display->drawFrame(2, CONTROLS_TOP + 1, 10, 6);
    display->drawText("^", 5, CONTROLS_TOP + 6);

    // Down button indicator (right)
    display->drawFrame(SCREEN_WIDTH - 12, CONTROLS_TOP + 1, 10, 6);
    display->drawText("v", SCREEN_WIDTH - 9, CONTROLS_TOP + 6);
}
