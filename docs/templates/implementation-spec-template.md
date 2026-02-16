# Implementation Spec Template

**Purpose**: This template defines the structure for Phase 2 Implementation Specs. The SeniorEngineer (Agent 02) produces this document to bridge design plans (Phase 1) to code implementation (Phase 3).

**Key Principle**: A JuniorEngineer agent should be able to implement the feature from this spec alone without making architectural decisions or asking clarifying questions.

---

## Implementation Spec

### Overview
Brief summary of what's being implemented (2-3 sentences). State the goal, the approach, and what success looks like.

### Files Changed

| File | Change Type | Description | Commit |
|------|-------------|-------------|--------|
| `path/to/file.hpp` | Modified | Brief description of what changes in this file | 1 |
| `path/to/file.cpp` | Modified | Brief description | 1 |
| `test/test_cli/new-test.hpp` | Created | Test function definitions | 2 |
| `test/test_cli/game-reg-tests.cpp` | Modified | TEST_F registrations for new tests | 2 |

### Code Changes

For each file, provide **exact before/after code snippets** showing what changes:

#### `include/game/quickdraw-states/example-state.hpp`

**Before:**
```cpp
class ExampleState : public QuickdrawState {
public:
    ExampleState();
    void onStateMounted() override;
    void onStateLoop() override;
    void onStateDismounted() override;

private:
    int counter;
};
```

**After:**
```cpp
class ExampleState : public QuickdrawState {
public:
    ExampleState();
    void onStateMounted() override;
    void onStateLoop() override;
    void onStateDismounted() override;

private:
    int counter;
    bool animationActive;  // NEW: tracks flash animation state
    unsigned long animationStart;  // NEW: flash start timestamp
};
```

**Rationale:** Add animation state tracking for level clear flash. `animationActive` controls the toggle, `animationStart` marks when the flash began.

---

#### `src/game/quickdraw-states/example-state.cpp`

**Add to `onStateMounted()`:**
```cpp
void ExampleState::onStateMounted() {
    counter = 0;
    animationActive = false;  // NEW
    animationStart = 0;       // NEW
}
```

**Modify `onStateLoop()` (rendering section):**
```cpp
void ExampleState::onStateLoop() {
    // ... existing code ...

    // NEW: Level clear flash animation
    if (animationActive) {
        unsigned long elapsed = millis() - animationStart;
        int flashCount = elapsed / 200;  // Toggle every 200ms
        if (flashCount >= 6) {  // 3 full blinks (6 toggles)
            animationActive = false;
            transition("NextState");
            return;
        }

        // Draw progress pips (example: 3/5 complete)
        int x = 40;
        int y = 28;
        for (int i = 0; i < 5; i++) {
            if (i < 3) {
                // Completed pips — newly completed one flashes
                if (i == 2 && (flashCount % 2 == 0)) {
                    PDN->getDisplay()->drawFrame(x, y, 8, 8);  // Empty
                } else {
                    PDN->getDisplay()->drawBox(x, y, 8, 8);    // Filled
                }
            } else {
                PDN->getDisplay()->drawFrame(x, y, 8, 8);  // Uncompleted
            }
            x += 12;
        }
    }

    // ... rest of existing code ...
}
```

**Rationale:** Flash animation uses elapsed time to toggle between filled/empty pip for the newly completed level. Runs for 1.2 seconds (6 toggles × 200ms), then transitions to next state.

---

### New Structs/Enums

If the implementation requires new data structures, provide complete definitions:

#### `ExampleConfig` struct (in `example-game.hpp`)

```cpp
struct ExampleConfig {
    int maxWalls;          // Number of walls per level
    int speed;             // ms per pixel movement
    int maxGapDistance;    // Max gap position delta between consecutive walls
    int hitsAllowed;       // Lives before game over
};
```

**Usage:** Populated in `ExampleGame::getConfigForLevel(int level)` — returns different values based on level and difficulty.

---

### Algorithm Explanations

For complex logic, explain the algorithm in pseudocode or plain English before showing the code:

#### Gap Generation Algorithm

**Goal:** Generate gap positions for N walls such that consecutive gaps differ by 1-4 lanes (easy) or 1-6 lanes (hard).

**Steps:**
1. First gap: random position in [0, numLanes-1]
2. For each subsequent gap:
   - Choose a delta in [1, maxGapDistance]
   - Choose a direction: +1 or -1
   - Compute `candidate = previous + (delta * direction)`
   - Clamp to valid lane range [0, numLanes-1]
   - If clamped to same position as previous, flip direction and try again
3. Store all gap positions in array

**Implementation:** See `generateGapPositions()` in `example-game.cpp`

---

## Testing Spec

### Test File Structure

- **Test functions**: `test/test_cli/example-game-tests.hpp`
- **TEST_F registrations**: `test/test_cli/example-game-reg-tests.cpp`

### Test Functions

For each test, provide **full source code** including setup, action, and assert:

#### Test: Level clear flash animation

**File:** `test/test_cli/example-game-tests.hpp`

```cpp
void testLevelClearFlashAnimation(ExampleTestSuite* suite) {
    // Arrange: enter ExampleState with level complete flag set
    suite->setupGame(false);  // easy mode
    suite->advanceToState("ExampleState");
    suite->setState()->setLevelComplete(true);

    // Act: run state mount, then 6 ticks at 200ms intervals
    suite->tickOnce();  // Mount
    for (int i = 0; i < 6; i++) {
        suite->advanceTime(200);
        suite->tickOnce();
    }

    // Assert: state transitioned to NextState after 1.2 seconds
    ASSERT_EQ(suite->getCurrentStateName(), "NextState");
}
```

#### Test: Gap positions respect max distance constraint

**File:** `test/test_cli/example-game-tests.hpp`

```cpp
void testGapPositionsBoundedByMaxDistance(ExampleTestSuite* suite) {
    // Arrange: configure level with maxGapDistance = 3, numLanes = 5
    suite->setupGame(false);
    auto config = suite->getGame()->getConfigForLevel(2);
    ASSERT_EQ(config.maxGapDistance, 3);

    // Act: generate gap positions for 8 walls
    std::vector<int> gaps = suite->getGame()->generateGapPositions(8, 5, 3);

    // Assert: every consecutive pair differs by 1-3 lanes
    for (size_t i = 1; i < gaps.size(); i++) {
        int delta = std::abs(gaps[i] - gaps[i-1]);
        ASSERT_GE(delta, 1) << "Consecutive gaps at same position";
        ASSERT_LE(delta, 3) << "Gap distance exceeds max";
    }
}
```

#### Test: Button press inverts control label

**File:** `test/test_cli/example-game-tests.hpp`

```cpp
void testButtonPressInvertsLabel(ExampleTestSuite* suite) {
    // Arrange: enter gameplay state
    suite->setupGame(false);
    suite->advanceToGameplay();

    // Act: press primary button
    suite->pressPrimary();
    suite->tickOnce();

    // Assert: display received drawBox call for inverted background
    auto& calls = suite->getDisplayCalls();
    bool foundInvertedBox = false;
    for (const auto& call : calls) {
        if (call.type == "drawBox" && call.y >= 55 && call.height <= 8) {
            foundInvertedBox = true;
            break;
        }
    }
    ASSERT_TRUE(foundInvertedBox) << "No inverted box drawn for button press";
}
```

### TEST_F Registrations

**File:** `test/test_cli/example-game-reg-tests.cpp`

```cpp
TEST_F(ExampleTestSuite, LevelClearFlashAnimation) {
    testLevelClearFlashAnimation(this);
}

TEST_F(ExampleTestSuite, GapPositionsBoundedByMaxDistance) {
    testGapPositionsBoundedByMaxDistance(this);
}

TEST_F(ExampleTestSuite, ButtonPressInvertsLabel) {
    testButtonPressInvertsLabel(this);
}
```

### Expected Test Count

After implementation, running `pio test -e native_cli_test` should show:
- **Total tests**: 175 (existing) + 8 (new) = 183
- **All tests pass**: 183/183

---

## Commit Breakdown

### Commit 1: Struct changes and state machine wiring
**Message:** `feat: example game — add animation state and config fields (#NNN)`

**Files:**
- `include/game/example-game.hpp` — Add `ExampleConfig` struct
- `include/game/quickdraw-states/example-state.hpp` — Add `animationActive`, `animationStart` fields
- `src/game/example-game.cpp` — Implement `getConfigForLevel()`, `generateGapPositions()`
- `src/game/quickdraw-states/example-state.cpp` — Add stub animation logic (no-op)

**Why this commit:** Establishes the data structures and API surface. No visual changes yet, but the code compiles and tests pass.

---

### Commit 2: Rendering implementation
**Message:** `feat: example game — implement level clear flash and wall rendering (#NNN)`

**Files:**
- `src/game/quickdraw-states/example-state.cpp` — Implement full flash animation, wall rendering, button indicators
- `include/cli/cli-device.hpp` — Add triangle sprite XBM data (if shared)

**Why this commit:** All visual changes in one place. Builds on Commit 1's data structures.

---

### Commit 3: Tests
**Message:** `test: example game — add visual redesign tests (#NNN)`

**Files:**
- `test/test_cli/example-game-tests.hpp` — 8 new test functions
- `test/test_cli/example-game-reg-tests.cpp` — 8 new TEST_F registrations

**Why this commit:** Tests verify the implementation. Separate commit allows independent review of test coverage.

---

## Verification

After implementing, run these checks:

### 1. Build Check
```bash
pio run -e native_cli
```
**Expected:** Build succeeds with no warnings.

### 2. Test Check
```bash
pio test -e native_cli_test
```
**Expected:** All tests pass. Output should show `183/183` (or higher if other tests were added).

### 3. Manual Verification
```bash
.pio/build/native_cli/program 2
```
**In the CLI simulator:**
```
cable 0 1
mode 0 easy        # Set device 0 to easy mode
play 0 example     # Start ExampleGame on device 0
press 0 primary    # Test button press indicator
state 0            # Verify state transitions
```

**Check:**
- [ ] Walls slide left smoothly
- [ ] Button press inverts [UP]/[DOWN] labels
- [ ] Level clear flash plays for ~1.2 seconds
- [ ] Collision detection triggers on wall arrival
- [ ] Screen flash + haptic on hit

### 4. Diff Verification

Compare your PR diff against this Implementation Spec:
- [ ] Every file in the "Files Changed" table appears in your diff
- [ ] No unexpected files modified (check `git status`)
- [ ] No disabled tests in your changes
- [ ] Commit structure matches the breakdown (3 commits)

---

## Non-Goals

**What NOT to change** (Junior should ask before touching these):

- [ ] Other games' test registration files (e.g., `ghost-runner-reg-tests.cpp`)
- [ ] Shared enum values in `device-types.hpp` (e.g., `Allegiance`, `Role`)
- [ ] State machine topology (adding new state machine types)
- [ ] Core driver interfaces (e.g., `Display`, `Button`, `Light`)
- [ ] CLI device infrastructure (`cli-device.hpp`, `cli-device.cpp`)
- [ ] Build configuration (`platformio.ini`)

**If you need to modify a shared file**, stop and ask Agent 02 or the orchestrator first.

---

## Boundaries (Three-Tier System)

### Always Do
- Follow the code snippets exactly as written in this spec
- Use `git commit --no-verify` to bypass pre-commit hook
- Run `pio test -e native_cli_test` after every change
- Add TEST_F registrations to YOUR game's registration file only
- Use existing patterns from other minigames (check `ghost-runner-tests.hpp` for examples)

### Ask First
- Modifying shared files listed in "Non-Goals"
- Adding new dependencies or libraries
- Changing state machine behavior beyond what's specified
- Adding new CLI commands or device capabilities
- Refactoring code outside the scope of this feature

### Never Do
- Delete existing tests without explicit approval
- Modify other games' test files
- Disable tests instead of fixing them
- Commit `wifi_credentials.ini` or other ignored files
- Use `git push --force` to main/master
- Merge your PR (only @FinallyEve merges after human review)

---

## Design Decisions

### Why flash animation instead of static message?
Visual feedback is core to the redesign. A static "Level 3 Complete" message doesn't convey progress or momentum. The flashing pip (●) provides kinetic confirmation of achievement.

### Why 6px wall thickness?
Thinner walls (4px) were too subtle on the OLED. Thicker walls (8px) left too little space for gaps. 6px is the sweet spot — clearly visible, allows 5+ lanes comfortably.

### Why right-pointing triangle instead of a square?
Directionality. The game is a side-scroller — walls move left, player dodges. A triangle (►) visually reinforces the left-to-right player perspective. A square or circle is directionless.

### Why 14px wall spacing?
Guarantees the player can see the next gap before the current wall arrives. At 6px wall width, 14px spacing ensures at least one full gap (9px tall) is visible ahead for planning.

---

## Lessons Learned (Post-Implementation)

*This section is filled in by the JuniorEngineer after implementation is complete. Document any surprises, API quirks, or implementation details that weren't obvious from the spec.*

**Example entries:**
- `drawGlyph()` requires UTF-8 encoding for filled circle — used `"\u25CF"` for ● and `"\u25CB"` for ○
- Flash animation timing was off by 100ms due to frame rate variance — used `millis()` instead of frame counter
- Wall collision check needed `<= CURSOR_X + 2` instead of `<= CURSOR_X` to account for triangle width

---

**End of Implementation Spec Template**

---

## Usage Notes for SeniorEngineer (Agent 02)

1. **Copy this template** into your issue or plan document under a `## Implementation Spec` heading.
2. **Fill in every section** with exact code snippets, test function bodies, and commit breakdown.
3. **Verify completeness** using the checklist in issue #277.
4. **Label the issue `agent-ready`** only after all sections are complete.
5. **Update "Lessons Learned"** during Phase 5 review based on Junior's feedback.

This spec is the **contract** between design (Phase 1) and implementation (Phase 3). If the Junior asks clarifying questions, the spec was incomplete — fix it and iterate.
