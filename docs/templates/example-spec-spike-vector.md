# Example Implementation Spec: Spike Vector Visual Redesign

**Purpose**: This is a retrospective example showing what a complete Phase 2 Implementation Spec should look like. This spec was NOT written for the actual PR #269, but demonstrates the level of detail required for future visual redesign tasks.

**Feature**: Transform Spike Vector from text-only debug output into a side-scrolling wall gauntlet with visual feedback and progressive difficulty.

**Issue**: #223
**PR**: #269 (merged)
**Quality Assessment**: Medium — PR body described features but lacked exact code snippets and commit breakdown.

---

## Implementation Spec

### Overview
Convert Spike Vector gameplay from text readouts (`Pos: 2  Wall: 45/100  Gap: 4`) to a visual side-scrolling wall gauntlet. Each level spawns 5-12 walls (depending on difficulty) that slide left as a formation. The player navigates a right-pointing triangle cursor through gaps using [UP]/[DOWN] buttons. Visual feedback includes lane tick marks, button press indicators, level clear flash with progress pips, and hit feedback (screen flash + haptic).

**Success Criteria:**
- All walls render as 6px-wide vertical bars with one gap per wall
- Walls slide left at progressive speeds (60ms/px → 15ms/px)
- Player cursor is a right-pointing triangle on the left edge
- Button presses invert [UP]/[DOWN] labels
- Level clear shows flashing progress pips (●●●○○)
- Hit triggers XOR flash + haptic pulse

### Files Changed

| File | Change Type | Description | Commit |
|------|-------------|-------------|--------|
| `include/game/spike-vector.hpp` | Modified | Add `SpikeVectorConfig` struct, `SpikeVectorSession` fields for multi-wall | 1 |
| `src/game/spike-vector.cpp` | Modified | Implement `getConfigForLevel()`, `generateGapPositions()` | 1 |
| `include/game/quickdraw-states/spike-vector-gameplay.hpp` | Modified | Add `formationX`, `currentWallIndex`, animation state fields | 1 |
| `src/game/quickdraw-states/spike-vector-gameplay.cpp` | Modified | Full rendering rewrite — HUD, lanes, walls, cursor, buttons, flash | 2 |
| `include/cli/cli-device.hpp` | Modified | Add `TRIANGLE_SPRITE_XBM` constant | 2 |
| `test/test_cli/spike-vector-tests.hpp` | Created | 12 new test functions | 3 |
| `test/test_cli/spike-vector-reg-tests.cpp` | Modified | 12 new TEST_F registrations | 3 |

### Code Changes

#### `include/game/spike-vector.hpp`

**Add new structs:**

```cpp
struct SpikeVectorConfig {
    int numLanes;          // 5 (easy) or 7 (hard)
    int wallCount;         // 5-12 depending on level
    int speedLevel;        // 1-8 abstract speed
    int maxGapDistance;    // Max lane delta between consecutive gaps
    int hitsAllowed;       // 3 (easy) or 1 (hard)
};

struct SpikeVectorSession {
    int level;                    // 1-5
    int hits;                     // Hit counter
    int score;                    // Dodge score
    std::vector<int> gapPositions;  // NEW: gap position per wall
    int wallsPassed;              // NEW: count of walls that moved off-screen
    bool levelComplete;           // NEW: true when all walls passed
};
```

**Rationale:** Multi-wall gameplay requires tracking gap positions for each wall (not just one gap per level) and counting walls passed to determine level completion.

---

#### `src/game/spike-vector.cpp`

**Add `getConfigForLevel()` method:**

```cpp
SpikeVectorConfig SpikeVector::getConfigForLevel(int level, bool isHard) const {
    SpikeVectorConfig config;

    if (isHard) {
        config.numLanes = 7;
        config.hitsAllowed = 1;
        config.maxGapDistance = 6;  // Full range

        // Hard mode: 8-12 walls, speed 4-8
        config.wallCount = 7 + level;      // Level 1: 8, Level 5: 12
        config.speedLevel = 3 + level;     // Level 1: 4, Level 5: 8
    } else {
        config.numLanes = 5;
        config.hitsAllowed = 3;

        // Easy mode: 5-8 walls, speed 1-5, scaling max gap
        config.wallCount = 4 + level;      // Level 1: 5, Level 5: 9
        config.speedLevel = level;         // Level 1: 1, Level 5: 5

        // Easy max gap scales: 2→2→3→3→4
        if (level <= 2) config.maxGapDistance = 2;
        else if (level <= 4) config.maxGapDistance = 3;
        else config.maxGapDistance = 4;
    }

    return config;
}
```

**Add `generateGapPositions()` method:**

```cpp
std::vector<int> SpikeVector::generateGapPositions(int wallCount, int numLanes, int maxGapDistance) {
    std::vector<int> gaps;
    gaps.reserve(wallCount);

    // First gap: random position
    gaps.push_back(rand() % numLanes);

    // Subsequent gaps: bounded delta from previous
    for (int i = 1; i < wallCount; i++) {
        int prev = gaps[i - 1];
        int delta = (rand() % maxGapDistance) + 1;  // 1 to maxGapDistance
        int direction = (rand() % 2) ? 1 : -1;
        int candidate = prev + (delta * direction);

        // Clamp to valid range
        if (candidate < 0) candidate = 0;
        if (candidate >= numLanes) candidate = numLanes - 1;

        // If clamped to same position, try opposite direction
        if (candidate == prev) {
            candidate = prev - (delta * direction);
            if (candidate < 0) candidate = 0;
            if (candidate >= numLanes) candidate = numLanes - 1;
        }

        gaps.push_back(candidate);
    }

    return gaps;
}
```

**Rationale:** Gap generation ensures consecutive gaps differ by at least 1 lane (forces player movement) and at most `maxGapDistance` (scales difficulty).

---

#### `include/game/quickdraw-states/spike-vector-gameplay.hpp`

**Add to private fields:**

```cpp
private:
    int cursorPosition;        // Existing
    SpikeVectorConfig config;  // NEW: level configuration

    // Multi-wall tracking
    int formationX;            // NEW: X position of leftmost wall
    int currentWallIndex;      // NEW: index of next wall to evaluate

    // Animation state
    bool levelClearActive;     // NEW: true during level clear flash
    unsigned long levelClearStart;  // NEW: timestamp for flash start

    // Button press feedback
    bool primaryPressed;       // NEW: tracks [UP] button state
    bool secondaryPressed;     // NEW: tracks [DOWN] button state

    // Speed mapping
    int getMillisPerPixel() const;  // NEW: convert speedLevel to ms/px
```

**Rationale:** Track formation position, wall evaluation state, and animation state for level clear and button feedback.

---

#### `src/game/quickdraw-states/spike-vector-gameplay.cpp`

**Modify `onStateMounted()`:**

```cpp
void SpikeVectorGameplay::onStateMounted() {
    SpikeVector* game = static_cast<SpikeVector*>(PDN->getGame());
    SpikeVectorSession* session = game->getSession();

    // Get level configuration
    bool isHard = (PDN->getRole() == Role::HUNTER);
    config = game->getConfigForLevel(session->level, isHard);

    // Initialize cursor at center lane
    cursorPosition = config.numLanes / 2;

    // Generate gap positions for all walls
    session->gapPositions = game->generateGapPositions(
        config.wallCount,
        config.numLanes,
        config.maxGapDistance
    );

    // Initialize formation off-screen right
    formationX = 128;  // Start just off right edge
    currentWallIndex = 0;
    session->wallsPassed = 0;
    session->levelComplete = false;

    // Animation state
    levelClearActive = false;
    levelClearStart = 0;
    primaryPressed = false;
    secondaryPressed = false;

    // Register button handlers
    PDN->getButton()->onPrimary([this]() {
        if (cursorPosition > 0) cursorPosition--;
        primaryPressed = true;
    });

    PDN->getButton()->onSecondary([this]() {
        if (cursorPosition < config.numLanes - 1) cursorPosition++;
        secondaryPressed = true;
    });
}
```

**Add `getMillisPerPixel()` helper:**

```cpp
int SpikeVectorGameplay::getMillisPerPixel() const {
    // Map speedLevel [1-8] to ms/px [60, 50, 40, 35, 30, 25, 20, 15]
    const int speedMap[] = {60, 50, 40, 35, 30, 25, 20, 15};
    int index = config.speedLevel - 1;
    if (index < 0) index = 0;
    if (index >= 8) index = 7;
    return speedMap[index];
}
```

**Rewrite `onStateLoop()` (full rendering):**

```cpp
void SpikeVectorGameplay::onStateLoop() {
    SpikeVector* game = static_cast<SpikeVector*>(PDN->getGame());
    SpikeVectorSession* session = game->getSession();

    PDN->getDisplay()->invalidateScreen();

    // === Level Clear Animation ===
    if (levelClearActive) {
        renderLevelClearFlash(session);
        return;  // Skip normal gameplay rendering
    }

    // === HUD Bar ===
    renderHUD(session);

    // === Top Separator ===
    PDN->getDisplay()->drawBox(0, 8, 128, 1);

    // === Game Area ===
    int laneHeight = (config.numLanes == 5) ? 9 : 6;
    int gameAreaY = 9;

    // Lane tick marks (left edge, between lanes)
    for (int i = 1; i < config.numLanes; i++) {
        int y = gameAreaY + i * laneHeight;
        PDN->getDisplay()->drawBox(0, y, 3, 1);
    }

    // Player cursor (right-pointing triangle)
    int cursorY = gameAreaY + cursorPosition * laneHeight + (laneHeight - 7) / 2;
    PDN->getDisplay()->drawXBMP(2, cursorY, 5, 7, TRIANGLE_SPRITE_XBM);

    // Walls (6px wide, 14px spacing)
    for (int i = 0; i < config.wallCount; i++) {
        int wallX = formationX + i * 20;  // 20px per wall unit (6px + 14px gap)

        // Only render if wall is on screen
        if (wallX >= -6 && wallX < 128) {
            int gapLane = session->gapPositions[i];

            // Draw wall segments (all lanes except gap)
            for (int lane = 0; lane < config.numLanes; lane++) {
                if (lane != gapLane) {
                    int y = gameAreaY + lane * laneHeight;
                    PDN->getDisplay()->drawBox(wallX, y, 6, laneHeight);
                }
            }
        }
    }

    // === Bottom Separator ===
    PDN->getDisplay()->drawBox(0, 55, 128, 1);

    // === Controls Bar ===
    renderControls();

    // === Movement Logic ===
    static unsigned long lastMoveTime = 0;
    unsigned long now = millis();
    if (now - lastMoveTime >= getMillisPerPixel()) {
        formationX--;  // Move formation left by 1px
        lastMoveTime = now;
    }

    // === Collision Detection ===
    // Check if current wall has reached the cursor
    if (currentWallIndex < config.wallCount) {
        int wallX = formationX + currentWallIndex * 20;
        if (wallX <= 2) {  // Wall left edge at cursor X
            int gapLane = session->gapPositions[currentWallIndex];
            if (cursorPosition == gapLane) {
                // Dodge success
                session->score += 100;
            } else {
                // Hit
                session->hits++;
                triggerHitFeedback();

                // Check lose condition
                if (session->hits > config.hitsAllowed) {
                    transition("SpikeVectorEvaluate");
                    return;
                }
            }
            currentWallIndex++;
        }
    }

    // === Level Complete Check ===
    if (formationX + (config.wallCount - 1) * 20 + 6 < 0) {
        // All walls off-screen
        session->levelComplete = true;
        levelClearActive = true;
        levelClearStart = millis();
    }

    // Clear button press flags (reset on next frame)
    primaryPressed = false;
    secondaryPressed = false;
}
```

**Add `renderHUD()` helper:**

```cpp
void SpikeVectorGameplay::renderHUD(SpikeVectorSession* session) {
    // Level counter (left)
    char levelText[16];
    snprintf(levelText, sizeof(levelText), "L:%d/5", session->level);
    PDN->getDisplay()->drawStr(2, 6, levelText);

    // Lives (center) — filled hearts for remaining, empty for lost
    int heartsX = 50;
    for (int i = 0; i < config.hitsAllowed; i++) {
        const char* heart = (i < session->hits) ? "\u2661" : "\u2665";  // Empty / Filled
        PDN->getDisplay()->drawUTF8(heartsX, 6, heart);
        heartsX += 8;
    }

    // Difficulty label (right)
    const char* diffLabel = (config.numLanes == 5) ? "EASY" : "HARD";
    PDN->getDisplay()->drawStr(100, 6, diffLabel);
}
```

**Add `renderControls()` helper:**

```cpp
void SpikeVectorGameplay::renderControls() {
    int y = 62;

    // [UP] label (left)
    if (primaryPressed) {
        PDN->getDisplay()->setDrawColor(1);
        PDN->getDisplay()->drawBox(2, 56, 24, 8);  // Inverted background
        PDN->getDisplay()->setDrawColor(0);        // Dark text on light bg
        PDN->getDisplay()->drawStr(4, y, "[UP]");
        PDN->getDisplay()->setDrawColor(1);        // Restore
    } else {
        PDN->getDisplay()->drawStr(4, y, "[UP]");
    }

    // [DOWN] label (right)
    if (secondaryPressed) {
        PDN->getDisplay()->setDrawColor(1);
        PDN->getDisplay()->drawBox(100, 56, 26, 8);
        PDN->getDisplay()->setDrawColor(0);
        PDN->getDisplay()->drawStr(102, y, "[DOWN]");
        PDN->getDisplay()->setDrawColor(1);
    } else {
        PDN->getDisplay()->drawStr(102, y, "[DOWN]");
    }
}
```

**Add `renderLevelClearFlash()` helper:**

```cpp
void SpikeVectorGameplay::renderLevelClearFlash(SpikeVectorSession* session) {
    unsigned long elapsed = millis() - levelClearStart;
    int flashCount = elapsed / 200;  // Toggle every 200ms

    if (flashCount >= 6) {  // 3 full blinks (1.2 seconds)
        levelClearActive = false;

        // Advance to next level or end game
        if (session->level < 5) {
            session->level++;
            session->levelComplete = false;
            transition("SpikeVectorGameplay");  // Re-mount for next level
        } else {
            transition("SpikeVectorEvaluate");  // Game complete
        }
        return;
    }

    // Draw progress pips (●●●○○ for 3/5 complete)
    int x = 40;
    int y = 28;
    for (int i = 0; i < 5; i++) {
        if (i < session->level) {
            // Completed pip — newly completed one flashes
            if (i == session->level - 1 && (flashCount % 2 == 0)) {
                PDN->getDisplay()->drawFrame(x, y, 8, 8);  // Empty (flash off)
            } else {
                PDN->getDisplay()->drawBox(x, y, 8, 8);    // Filled
            }
        } else {
            PDN->getDisplay()->drawFrame(x, y, 8, 8);  // Uncompleted
        }
        x += 12;
    }
}
```

**Add `triggerHitFeedback()` helper:**

```cpp
void SpikeVectorGameplay::triggerHitFeedback() {
    // XOR screen flash (invert entire display)
    PDN->getDisplay()->setDrawColor(2);  // XOR mode
    PDN->getDisplay()->drawBox(0, 0, 128, 64);
    PDN->getDisplay()->setDrawColor(1);  // Restore normal mode

    // Haptic pulse
    PDN->getHaptic()->pulse(100);

    // Red LED flash
    PDN->getLight()->setColor(255, 0, 0);
    PDN->getLight()->show();
}
```

**Rationale:** Full rendering rewrite implements all visual feedback requirements. Each helper function has a single responsibility (HUD, controls, flash, hit).

---

#### `include/cli/cli-device.hpp`

**Add XBM sprite data:**

```cpp
// Right-pointing triangle sprite (5x7 pixels)
// Visual: ►
const unsigned char TRIANGLE_SPRITE_XBM[] = {
    0x01, 0x03, 0x07, 0x0F, 0x07, 0x03, 0x01
};
```

**Rationale:** XBM format is compatible with U8g2's `drawXBMP()`. Sprite is small (5 bytes) and clearly indicates direction.

---

## Testing Spec

### Test File Structure

- **Test functions**: `test/test_cli/spike-vector-tests.hpp`
- **TEST_F registrations**: `test/test_cli/spike-vector-reg-tests.cpp`

### Test Functions

#### Test 1: Gap positions respect max distance

```cpp
void testGapPositionsBoundedByMaxDistance(SpikeVectorTestSuite* suite) {
    // Arrange: configure level with maxGapDistance = 3, numLanes = 5
    suite->setupGame(false);  // easy mode
    auto game = suite->getGame();
    auto config = game->getConfigForLevel(2, false);
    ASSERT_EQ(config.maxGapDistance, 2);  // Easy level 2

    // Act: generate gap positions for 6 walls
    std::vector<int> gaps = game->generateGapPositions(6, 5, 2);

    // Assert: every consecutive pair differs by 1-2 lanes
    ASSERT_EQ(gaps.size(), 6);
    for (size_t i = 1; i < gaps.size(); i++) {
        int delta = std::abs(gaps[i] - gaps[i-1]);
        ASSERT_GE(delta, 1) << "Consecutive gaps at same position";
        ASSERT_LE(delta, 2) << "Gap distance exceeds max";
    }
}
```

#### Test 2: Walls slide left at correct speed

```cpp
void testWallsSlideLeftAtCorrectSpeed(SpikeVectorTestSuite* suite) {
    // Arrange: start game at level 1 (easy = speed 1 = 60ms/px)
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto state = suite->getGameplayState();
    int initialX = state->formationX;

    // Act: advance time by 180ms (should move 3 pixels)
    suite->advanceTime(180);
    suite->tickOnce();

    // Assert: formationX decreased by 3
    ASSERT_EQ(state->formationX, initialX - 3);
}
```

#### Test 3: Collision detected when wall reaches cursor

```cpp
void testCollisionDetectedAtCursorPosition(SpikeVectorTestSuite* suite) {
    // Arrange: spawn wall with gap at lane 2, cursor at lane 1 (miss)
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto state = suite->getGameplayState();
    auto session = suite->getSession();

    state->cursorPosition = 1;
    session->gapPositions[0] = 2;  // Gap in different lane
    state->formationX = 3;  // Wall about to reach cursor at X=2

    // Act: advance one tick (wall reaches cursor)
    suite->advanceTime(60);
    suite->tickOnce();

    // Assert: hit registered
    ASSERT_EQ(session->hits, 1);
}
```

#### Test 4: Dodge success when cursor in gap

```cpp
void testDodgeSuccessWhenCursorInGap(SpikeVectorTestSuite* suite) {
    // Arrange: cursor at lane 2, gap at lane 2 (match)
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto state = suite->getGameplayState();
    auto session = suite->getSession();

    state->cursorPosition = 2;
    session->gapPositions[0] = 2;  // Gap matches cursor
    state->formationX = 3;

    // Act: advance one tick
    suite->advanceTime(60);
    suite->tickOnce();

    // Assert: no hit, score increased
    ASSERT_EQ(session->hits, 0);
    ASSERT_EQ(session->score, 100);
}
```

#### Test 5: Level clear flash triggers when all walls pass

```cpp
void testLevelClearFlashTriggersWhenWallsPass(SpikeVectorTestSuite* suite) {
    // Arrange: single-wall level, wall already off-screen
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto state = suite->getGameplayState();
    auto config = suite->getGame()->getConfigForLevel(1, false);

    // Manually set formationX so all walls are off-screen
    state->formationX = -30;  // Well off left edge

    // Act: tick once to check level complete condition
    suite->tickOnce();

    // Assert: levelClearActive is true
    ASSERT_TRUE(state->levelClearActive);
}
```

#### Test 6: Level clear flash advances to next level after 1.2 seconds

```cpp
void testLevelClearFlashAdvancesToNextLevel(SpikeVectorTestSuite* suite) {
    // Arrange: trigger level clear flash
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto state = suite->getGameplayState();
    auto session = suite->getSession();

    state->levelClearActive = true;
    state->levelClearStart = suite->getCurrentTime();
    int startLevel = session->level;

    // Act: advance 1.3 seconds (6 flashes at 200ms each)
    for (int i = 0; i < 7; i++) {
        suite->advanceTime(200);
        suite->tickOnce();
    }

    // Assert: level incremented, back in gameplay state
    ASSERT_EQ(session->level, startLevel + 1);
    ASSERT_FALSE(state->levelClearActive);
}
```

#### Test 7: Button press inverts UP label

```cpp
void testButtonPressInvertsUPLabel(SpikeVectorTestSuite* suite) {
    // Arrange: enter gameplay
    suite->setupGame(false);
    suite->advanceToGameplay();

    // Act: press primary button
    suite->pressPrimary();
    suite->tickOnce();

    // Assert: display received drawBox call for inverted background at y=56, height=8
    auto& calls = suite->getDisplayCalls();
    bool foundInvertedBox = false;
    for (const auto& call : calls) {
        if (call.type == "drawBox" && call.y == 56 && call.height == 8 && call.x < 30) {
            foundInvertedBox = true;
            break;
        }
    }
    ASSERT_TRUE(foundInvertedBox) << "No inverted box drawn for [UP] button press";
}
```

#### Test 8: Button press inverts DOWN label

```cpp
void testButtonPressInvertsDOWNLabel(SpikeVectorTestSuite* suite) {
    // Arrange: enter gameplay
    suite->setupGame(false);
    suite->advanceToGameplay();

    // Act: press secondary button
    suite->pressSecondary();
    suite->tickOnce();

    // Assert: display received drawBox call for inverted background at y=56, x > 90
    auto& calls = suite->getDisplayCalls();
    bool foundInvertedBox = false;
    for (const auto& call : calls) {
        if (call.type == "drawBox" && call.y == 56 && call.height == 8 && call.x > 90) {
            foundInvertedBox = true;
            break;
        }
    }
    ASSERT_TRUE(foundInvertedBox) << "No inverted box drawn for [DOWN] button press";
}
```

#### Test 9: Hit triggers haptic feedback

```cpp
void testHitTriggersHapticFeedback(SpikeVectorTestSuite* suite) {
    // Arrange: collision setup
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto state = suite->getGameplayState();
    auto session = suite->getSession();

    state->cursorPosition = 1;
    session->gapPositions[0] = 2;  // Miss
    state->formationX = 3;

    // Act: trigger collision
    suite->advanceTime(60);
    suite->tickOnce();

    // Assert: haptic pulse called
    auto& hapticCalls = suite->getHapticCalls();
    ASSERT_FALSE(hapticCalls.empty());
    ASSERT_EQ(hapticCalls[0].duration, 100);
}
```

#### Test 10: Hit triggers LED red flash

```cpp
void testHitTriggersLEDRedFlash(SpikeVectorTestSuite* suite) {
    // Arrange: collision setup
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto state = suite->getGameplayState();
    auto session = suite->getSession();

    state->cursorPosition = 1;
    session->gapPositions[0] = 2;
    state->formationX = 3;

    // Act: trigger collision
    suite->advanceTime(60);
    suite->tickOnce();

    // Assert: LED set to red (255, 0, 0)
    auto& lightCalls = suite->getLightCalls();
    bool foundRed = false;
    for (const auto& call : lightCalls) {
        if (call.type == "setColor" && call.r == 255 && call.g == 0 && call.b == 0) {
            foundRed = true;
            break;
        }
    }
    ASSERT_TRUE(foundRed) << "Red LED flash not triggered on hit";
}
```

#### Test 11: Easy mode spawns 5 walls at level 1

```cpp
void testEasyModeLevel1Spawns5Walls(SpikeVectorTestSuite* suite) {
    // Arrange: start easy mode level 1
    suite->setupGame(false);
    suite->advanceToGameplay();
    auto session = suite->getSession();

    // Assert: gapPositions array has 5 entries
    ASSERT_EQ(session->gapPositions.size(), 5);
}
```

#### Test 12: Hard mode spawns 8 walls at level 1

```cpp
void testHardModeLevel1Spawns8Walls(SpikeVectorTestSuite* suite) {
    // Arrange: start hard mode level 1
    suite->setupGame(true);  // hard mode
    suite->advanceToGameplay();
    auto session = suite->getSession();

    // Assert: gapPositions array has 8 entries
    ASSERT_EQ(session->gapPositions.size(), 8);
}
```

### TEST_F Registrations

**File:** `test/test_cli/spike-vector-reg-tests.cpp`

```cpp
TEST_F(SpikeVectorTestSuite, GapPositionsBoundedByMaxDistance) {
    testGapPositionsBoundedByMaxDistance(this);
}

TEST_F(SpikeVectorTestSuite, WallsSlideLeftAtCorrectSpeed) {
    testWallsSlideLeftAtCorrectSpeed(this);
}

TEST_F(SpikeVectorTestSuite, CollisionDetectedAtCursorPosition) {
    testCollisionDetectedAtCursorPosition(this);
}

TEST_F(SpikeVectorTestSuite, DodgeSuccessWhenCursorInGap) {
    testDodgeSuccessWhenCursorInGap(this);
}

TEST_F(SpikeVectorTestSuite, LevelClearFlashTriggersWhenWallsPass) {
    testLevelClearFlashTriggersWhenWallsPass(this);
}

TEST_F(SpikeVectorTestSuite, LevelClearFlashAdvancesToNextLevel) {
    testLevelClearFlashAdvancesToNextLevel(this);
}

TEST_F(SpikeVectorTestSuite, ButtonPressInvertsUPLabel) {
    testButtonPressInvertsUPLabel(this);
}

TEST_F(SpikeVectorTestSuite, ButtonPressInvertsDOWNLabel) {
    testButtonPressInvertsDOWNLabel(this);
}

TEST_F(SpikeVectorTestSuite, HitTriggersHapticFeedback) {
    testHitTriggersHapticFeedback(this);
}

TEST_F(SpikeVectorTestSuite, HitTriggersLEDRedFlash) {
    testHitTriggersLEDRedFlash(this);
}

TEST_F(SpikeVectorTestSuite, EasyModeLevel1Spawns5Walls) {
    testEasyModeLevel1Spawns5Walls(this);
}

TEST_F(SpikeVectorTestSuite, HardModeLevel1Spawns8Walls) {
    testHardModeLevel1Spawns8Walls(this);
}
```

### Expected Test Count

After implementation:
- **Total tests**: 175 (existing) + 12 (new) = 187
- **All tests pass**: 187/187

---

## Commit Breakdown

### Commit 1: Config structs and gap generation
**Message:** `feat: spike vector — multi-wall config and gap generation (#223)`

**Files:**
- `include/game/spike-vector.hpp` — Add `SpikeVectorConfig`, `SpikeVectorSession` fields
- `src/game/spike-vector.cpp` — Implement `getConfigForLevel()`, `generateGapPositions()`
- `include/game/quickdraw-states/spike-vector-gameplay.hpp` — Add `formationX`, animation state fields

**Why this commit:** Establishes data structures and level configuration logic. No visual changes, but code compiles and tests pass. The API surface is ready for rendering implementation.

### Commit 2: Full visual rendering
**Message:** `feat: spike vector — side-scrolling wall gauntlet rendering (#223)`

**Files:**
- `src/game/quickdraw-states/spike-vector-gameplay.cpp` — Rewrite `onStateLoop()`, add all render helpers
- `include/cli/cli-device.hpp` — Add `TRIANGLE_SPRITE_XBM`

**Why this commit:** All visual changes in one atomic commit. Builds on Commit 1's config. Easier to review as a single "rendering rewrite" than mixed with config changes.

### Commit 3: Tests
**Message:** `test: spike vector — visual redesign gameplay tests (#223)`

**Files:**
- `test/test_cli/spike-vector-tests.hpp` — 12 new test functions
- `test/test_cli/spike-vector-reg-tests.cpp` — 12 new TEST_F registrations

**Why this commit:** Separate test commit allows independent review of test coverage. Tests verify both Commit 1 (config/gap generation) and Commit 2 (rendering/collision).

---

## Verification

### 1. Build Check
```bash
pio run -e native_cli
```
**Expected:** Build succeeds with no warnings.

### 2. Test Check
```bash
pio test -e native_cli_test
```
**Expected:** All tests pass. Output should show `187/187` or higher.

### 3. Manual Verification
```bash
.pio/build/native_cli/program 2
```

**In the CLI simulator:**
```
cable 0 1
mode 0 easy
play 0 spike-vector
state 0
```

**Verify:**
- [ ] Walls slide left from right edge
- [ ] Multiple walls visible simultaneously (5 for easy level 1)
- [ ] Player cursor is a right-pointing triangle on left edge
- [ ] Pressing [UP]/[DOWN] inverts button labels
- [ ] Collision detected when wall reaches cursor
- [ ] Hit triggers: screen flash (XOR), haptic pulse, red LED
- [ ] Level clear shows progress pips (●●●○○) with flashing newly-completed pip
- [ ] Flash lasts ~1.2 seconds, then advances to next level
- [ ] Lane tick marks visible between lanes

**Test progression:**
```
# Easy mode — 5 levels
press 0 primary     # Move up
press 0 secondary   # Move down
# Complete all 5 levels

# Hard mode — 7 lanes, faster speed
mode 1 hard
play 1 spike-vector
```

### 4. Diff Verification

Compare your PR diff against this Implementation Spec:
- [ ] Every file in "Files Changed" table appears in diff
- [ ] 3 commits total
- [ ] No disabled tests
- [ ] No unexpected files modified

---

## Non-Goals

**What NOT to change:**

- [ ] Other minigames' test files (`ghost-runner-reg-tests.cpp`, etc.)
- [ ] Shared enums in `device-types.hpp`
- [ ] State machine topology (don't add new state types)
- [ ] CLI device infrastructure (`cli-device.hpp` collision handling, etc.)
- [ ] Other games' rendering code

**If you need to modify shared code**, ask Agent 02 first.

---

## Boundaries

### Always Do
- Follow the exact code snippets provided
- Use `--no-verify` to bypass pre-commit hook
- Run `pio test -e native_cli_test` after every change
- Add TEST_F macros to `spike-vector-reg-tests.cpp` only

### Ask First
- Modifying shared files (cli-device.hpp, device-types.hpp)
- Changing state machine behavior outside this spec
- Adding new dependencies

### Never Do
- Delete existing tests
- Modify other games' test registration files
- Disable tests instead of fixing them
- Merge your PR (only @FinallyEve does this)

---

## Design Decisions

### Why multi-wall formations instead of one wall per wave?
Single-wall gameplay is trivial — the player moves to the gap and waits. Multi-wall formations create continuous decision-making: "Can I make it to lane 4 in time for the next wall, or should I take a safe gap closer to my current position?"

### Why 6px wall thickness?
4px walls were too thin to see clearly on the OLED. 8px walls crowded the screen. 6px is the sweet spot — clearly visible, allows 5-7 lanes comfortably.

### Why right-pointing triangle instead of square?
Directionality. The game is side-scrolling (walls move left), so the player's forward-facing perspective is left-to-right. A triangle (►) reinforces this visual metaphor. A square or circle has no directionality.

### Why 14px wall spacing?
Ensures the player can always see the next gap before the current wall arrives. At 6px wall width + 14px spacing, at least one full gap (9px tall for easy mode) is visible ahead, allowing planning.

### Why flashing progress pips instead of static text?
Kinetic feedback. A static "Level 3 Complete" message is passive. The flashing pip (●) provides visual momentum — you can *see* progress being made.

### Why XOR flash for hit feedback instead of solid white?
XOR mode (`setDrawColor(2)`) inverts the entire screen instantly without clearing — walls become gaps, gaps become walls. This creates a jarring "glitch" effect that's more impactful than a white flash overlay.

---

## Lessons Learned

*This section would be filled in by the JuniorEngineer after implementation. For this retrospective example, we can infer what challenges might have been encountered:*

- **Wall spacing math:** Initially tried 16px spacing, but walls extended too far off-screen. Reduced to 14px to keep formations tighter.
- **XOR flash timing:** Had to immediately restore draw color after XOR flash, or all subsequent rendering inverted. Added `setDrawColor(1)` right after.
- **Button press flag lifecycle:** Originally cleared flags in button callbacks, but this caused missed presses if the loop ran faster than button release. Moved flag clear to end of `onStateLoop()`.
- **Level clear transition edge case:** If player completes level 5, `levelClearFlash` tried to transition to `SpikeVectorGameplay` again instead of `SpikeVectorEvaluate`. Fixed with `if (session->level < 5)` check.

---

## What Makes This a Good Example?

This spec demonstrates:

1. **Complete code snippets** — every method body shown in full, not just "add animation logic here"
2. **Exact test function source code** — setup/action/assert with line-by-line explanation
3. **Clear commit breakdown** — 3 commits with explicit file-to-commit mapping
4. **Verification checklist** — CLI commands to run, manual checks to perform
5. **Non-goals section** — tells the Junior what NOT to touch
6. **Design decisions with rationale** — explains *why* each choice was made
7. **Three-tier boundaries** — Always/Ask/Never system prevents scope creep

If PR #269 had been implemented from this spec, the Junior would not have needed to make any architectural decisions — just follow the snippets line-by-line.

---

**End of Example Spec**
