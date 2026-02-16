# Wave 19: Post-Redesign Duplication Audit

**Date**: 2026-02-16
**Context**: Post-Wave 18 visual redesigns (7 minigames redesigned)
**Issue**: #167 (Code Duplication Across Minigames)
**Baseline**: Wave 13 (8,613 duplicate tokens, 104 duplicate blocks)

---

## Executive Summary

After analyzing all 7 FDN minigames post-redesign, I've identified **massive code duplication** across intro, win, and lose states. The pattern is consistent: each minigame has ~90% identical boilerplate in lifecycle states, with only minor variations in:
- Display text (game title, win/lose messages)
- LED animation states
- Hard mode detection criteria

**Key Finding**: ~60-75 lines per minigame (across intro/win/lose) are extractable to common base classes, representing **~450-525 lines of duplicated code** across the 7 minigames.

---

## Minigame Line Counts

Total lines per minigame (all `.cpp` files):

| Minigame | Total Lines | Intro | Win | Lose | Main Logic |
|----------|-------------|-------|-----|------|------------|
| Signal Echo | 547 | 63 | 79 | 72 | 333 |
| Ghost Runner | 669 | 66 | 87 | 74 | 442 |
| Spike Vector | 629 | 64 | 85 | 79 | 401 |
| Cipher Path | 612 | 64 | 79 | 73 | 396 |
| Exploit Sequencer | 664 | 64 | 83 | 77 | 440 |
| Breach Defense | 435 | 59 | 67 | 62 | 247 |
| Firewall Decrypt | 547 | 61 | 84 | 79 | 323 |
| **TOTAL** | **4,103** | **441** | **564** | **516** | **2,582** |

**Duplication Hotspot**: Intro/Win/Lose states account for **1,521 lines** (~37% of total minigame code), with **~80-90% similarity** across games.

---

## Duplication Clusters

### Cluster 1: **Intro State Boilerplate** (7 occurrences, ~95% similar)

**Pattern**:
```cpp
void [Game]Intro::onStateMounted(Device* PDN) {
    transitionToShowState = false;

    // Reset session
    game->getSession().reset();
    game->resetGame();

    // Set start time
    PlatformClock* clock = SimpleTimer::getPlatformClock();
    game->setStartTime(clock != nullptr ? clock->milliseconds() : 0);

    // Seed RNG
    game->seedRng(game->getConfig().rngSeed);

    // Display title screen (ONLY VARIABLE PART)
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("[GAME TITLE]", X, Y)
        ->drawText("[TAGLINE]", X, Y);
    PDN->getDisplay()->render();

    // Idle LED animation (VARIABLE: state constant)
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = [GAME]_IDLE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Start intro timer
    introTimer.setTimer(INTRO_DURATION_MS);
}

void [Game]Intro::onStateLoop(Device* PDN) {
    if (introTimer.expired()) {
        transitionToShowState = true;
    }
}

void [Game]Intro::onStateDismounted(Device* PDN) {
    introTimer.invalidate();
    transitionToShowState = false;
}
```

**Occurrences**:
- `src/game/signal-echo/echo-intro.cpp:13-62`
- `src/game/ghost-runner/ghost-runner-intro.cpp:16-65`
- `src/game/spike-vector/spike-vector-intro.cpp:16-63`
- `src/game/cipher-path/cipher-path-intro.cpp:16-63`
- `src/game/exploit-sequencer/exploit-sequencer-intro.cpp:16-63`
- `src/game/breach-defense/breach-defense-intro.cpp:15-58`
- `src/game/firewall-decrypt/decrypt-intro.cpp:16-61`

**Variable Elements**:
- Display title text (2 lines)
- LED state constant (1 identifier)
- Transition method name (`transitionToShow()` vs `transitionToScan()`)

**Extractable**: ~55 lines per game × 7 = **~385 lines** → Template method pattern with virtual hooks for title/LED state.

---

### Cluster 2: **Win State Boilerplate** (7 occurrences, ~90% similar)

**Pattern**:
```cpp
void [Game]Win::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Determine hard mode (VARIABLE: criteria)
    bool isHard = [GAME_SPECIFIC_CRITERIA];

    // Set outcome
    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = session.score;
    winOutcome.hardMode = isHard;
    game->setOutcome(winOutcome);

    // Display victory screen (VARIABLE: text)
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("[WIN MESSAGE]", X, Y);

    std::string scoreStr = "Score: " + std::to_string(session.score);
    PDN->getDisplay()->drawText(scoreStr.c_str(), 30, 50);
    PDN->getDisplay()->render();

    // Win LED animation (VARIABLE: state constant)
    AnimationConfig config;
    config.type = AnimationType::VERTICAL_CHASE;
    config.speed = 5;
    config.curve = EaseCurve::EASE_IN_OUT;
    config.initialState = [GAME]_WIN_STATE;
    config.loopDelayMs = 500;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // Celebration haptic
    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void [Game]Win::onStateLoop(Device* PDN) {
    if (winTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void [Game]Win::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool [Game]Win::isTerminalState() const {
    return game->getConfig().managedMode;
}
```

**Occurrences**:
- `src/game/signal-echo/echo-win.cpp:13-78`
- `src/game/ghost-runner/ghost-runner-win.cpp:17-86`
- `src/game/spike-vector/spike-vector-win.cpp:17-84`
- `src/game/cipher-path/cipher-path-win.cpp:16-78`
- `src/game/exploit-sequencer/exploit-sequencer-win.cpp:17-82`
- `src/game/breach-defense/breach-defense-win.cpp:15-66`
- `src/game/firewall-decrypt/decrypt-win.cpp:18-83`

**Variable Elements**:
- Hard mode criteria (1-2 lines per game)
- Win message text (1 line)
- LED state constant (1 identifier)

**Extractable**: ~65 lines per game × 7 = **~455 lines** → Template method with virtual `isHardMode()` and display hooks.

---

### Cluster 3: **Lose State Boilerplate** (7 occurrences, ~92% similar)

**Pattern**:
```cpp
void [Game]Lose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    // Set outcome
    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    // Display defeat screen (VARIABLE: text)
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("[LOSE MESSAGE]", X, Y);
    PDN->getDisplay()->render();

    // Lose LED animation (VARIABLE: state constant, animation type)
    AnimationConfig config;
    config.type = AnimationType::IDLE; // or LOSE
    config.speed = 8;
    config.curve = EaseCurve::LINEAR;
    config.initialState = [GAME]_LOSE_STATE;
    config.loopDelayMs = 0;
    config.loop = true; // or false
    PDN->getLightManager()->startAnimation(config);

    // Heavy haptic buzz
    PDN->getHaptics()->setIntensity(255); // or max()

    loseTimer.setTimer(LOSE_DISPLAY_MS);
}

void [Game]Lose::onStateLoop(Device* PDN) {
    if (loseTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void [Game]Lose::onStateDismounted(Device* PDN) {
    loseTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool [Game]Lose::isTerminalState() const {
    return game->getConfig().managedMode;
}
```

**Occurrences**:
- `src/game/signal-echo/echo-lose.cpp:13-71`
- `src/game/ghost-runner/ghost-runner-lose.cpp:16-73`
- `src/game/spike-vector/spike-vector-lose.cpp:17-78`
- `src/game/cipher-path/cipher-path-lose.cpp:16-72`
- `src/game/exploit-sequencer/exploit-sequencer-lose.cpp:17-76`
- `src/game/breach-defense/breach-defense-lose.cpp:15-61`
- `src/game/firewall-decrypt/decrypt-lose.cpp:17-78`

**Variable Elements**:
- Lose message text (1 line)
- LED state constant + animation type (2 lines)

**Extractable**: ~60 lines per game × 7 = **~420 lines** → Template method with display/LED hooks.

---

## Additional Duplication Patterns

### Pattern 4: **Config Initialization** (Not Yet Analyzed)

Each minigame has a config struct with similar initialization patterns. Requires deeper analysis to quantify.

### Pattern 5: **Session Reset Logic** (Partially Analyzed)

Session structs have similar `reset()` methods. Appears in `game->getSession().reset()` calls across all games.

### Pattern 6: **Score Display Rendering** (Medium Priority)

Score display logic (`std::to_string(session.score)` + `drawText`) appears in win states. Could extract to utility function.

---

## Recommended Extractions

### Priority 1: **Base Outcome State Classes** (HIGH IMPACT)

**Effort**: 3-5 hours
**Savings**: ~1,260 lines
**Files to Create**:
- `include/game/base-intro-state.hpp`
- `src/game/base-intro-state.cpp`
- `include/game/base-win-state.hpp`
- `src/game/base-win-state.cpp`
- `include/game/base-lose-state.hpp`
- `src/game/base-lose-state.cpp`

**Approach**:
1. Create template base classes with virtual hooks:
   - `BaseIntroState::getGameTitle()`, `getTagline()`, `getIdleLEDState()`
   - `BaseWinState::getWinMessage()`, `getWinLEDState()`, `isHardMode()`
   - `BaseLoseState::getLoseMessage()`, `getLoseLEDState()`
2. Refactor each minigame's intro/win/lose to inherit from base classes
3. Override only the virtual hooks with game-specific values

**Risk**: Medium (touches all minigames, requires careful testing)

**Validation**:
- All 7 minigames must pass existing CLI tests
- Hardware validation on at least 2 games

---

### Priority 2: **Score Display Utility** (QUICK WIN)

**Effort**: 30 minutes
**Savings**: ~21 lines (7 games × 3 lines)
**Files to Create**:
- `include/game/display-utils.hpp` (add `renderScoreAtBottom()` function)

**Approach**:
```cpp
void renderScoreAtBottom(Display* display, int score, int y = 50) {
    std::string scoreStr = "Score: " + std::to_string(score);
    display->drawText(scoreStr.c_str(), 30, y);
}
```

Replace all instances of score display boilerplate with single function call.

**Risk**: Low (pure addition, no breaking changes)

---

### Priority 3: **Common Timer Constants** (DOCUMENTATION)

**Effort**: 1 hour
**Savings**: Maintenance clarity (no line reduction)
**Issue**: Each game defines `INTRO_DURATION_MS`, `WIN_DISPLAY_MS`, `LOSE_DISPLAY_MS` independently.

**Approach**:
- Centralize constants in `include/game/minigame-constants.hpp`
- Document rationale for any deviations

**Risk**: Very Low (cosmetic refactor)

---

## Before/After Metrics

### Current State (Post-Wave 18)

| Metric | Value |
|--------|-------|
| Total minigame lines | 4,103 |
| Intro/Win/Lose lines | 1,521 (37%) |
| Estimated duplicate lines | ~1,260 (31%) |
| Duplicate tokens (Wave 13 baseline) | 8,613 |
| Duplicate blocks (Wave 13 baseline) | 104 |

### Projected State (After Priority 1 + 2)

| Metric | Value | Change |
|--------|-------|--------|
| Total minigame lines | ~2,864 | -1,239 (-30%) |
| Intro/Win/Lose lines | ~282 | -1,239 (-81%) |
| Estimated duplicate lines | ~200 | -1,060 (-84%) |
| Lines saved | 1,239 | — |

**Note**: Duplicate token/block metrics will be recalculated after extraction (expect 60-70% reduction).

---

## Implementation Strategy

### Phase 1: Base Classes (Week 1)
1. Create `BaseIntroState`, `BaseWinState`, `BaseLoseState`
2. Refactor **Signal Echo** as proof-of-concept
3. Run full test suite, validate on hardware
4. Document lessons learned

### Phase 2: Rollout (Week 2)
1. Apply pattern to Ghost Runner, Spike Vector
2. Test + validate
3. Apply to Cipher Path, Exploit Sequencer
4. Test + validate

### Phase 3: Final Games (Week 2-3)
1. Apply to Breach Defense, Firewall Decrypt
2. Full regression testing (all games)
3. Update baselines
4. Hardware validation on 3+ devices

### Phase 4: Utilities (Week 3)
1. Extract score display utility
2. Centralize constants
3. Document patterns in developer guide

---

## Design Decisions

### Why Template Method Over Composition?

**Considered**: Strategy pattern (pass display config objects)
**Chosen**: Template method (virtual hooks in base class)

**Rationale**:
- Minigame states already inherit from `State` base class
- Template method preserves existing architecture
- Virtual calls have negligible performance impact (~1-2 CPU cycles)
- Cleaner call sites (no config object construction)

### Why Not Extract LED State Constants?

**Issue**: Each game defines LED state constants in separate resource files
**Decision**: Keep them separate (for now)

**Rationale**:
- LED states are part of game identity (color schemes, animation patterns)
- Centralizing them would create a god-object resource file
- Future work: Consider LED state builder pattern if more games are added

---

## Testing Impact

### New Tests Required

None! Existing tests already validate state lifecycle behavior. After refactoring:
- All existing CLI tests must pass unchanged
- Hardware smoke tests on 2+ games to verify LED/haptic behavior

### Test Files to Monitor

- `test/test_cli/signal-echo-tests.hpp`
- `test/test_cli/ghost-runner-tests.hpp`
- `test/test_cli/spike-vector-tests.hpp`
- `test/test_cli/cipher-path-tests.hpp`
- `test/test_cli/exploit-seq-tests.hpp`
- `test/test_cli/breach-defense-tests.hpp`
- `test/test_cli/fdn-protocol-tests.hpp`

---

## Lessons Learned from Wave 18 Redesigns

### What Went Well

1. **Consistent patterns emerged** — All 7 redesigns converged on a similar structure, making extraction feasible
2. **Visual clarity improved** — Clean title screens and outcome displays are now uniform
3. **LED/haptic patterns standardized** — Win = VERTICAL_CHASE + 200 intensity, Lose = IDLE/LOSE + 255 intensity

### What Could Improve

1. **Duplication introduced** — Each redesign copied boilerplate from previous games instead of extracting first
2. **Hard mode detection diverged** — No common interface for determining hard mode eligibility
3. **Display positioning inconsistent** — Some games use (10, 25) for win text, others use (5, 25) or (15, 25)

### Recommendations for Future Waves

1. **Extract before scaling** — If 3+ games need the same feature, extract first
2. **Define interfaces early** — Hard mode, score display, timer durations should be formalized
3. **Leverage existing patterns** — `ui-clean-minimal.hpp` exists but wasn't used consistently

---

## Files for Future Analysis

These files were not analyzed in this audit but may contain duplication:

- `src/game/*/[game]-gameplay.cpp` — Core gameplay loop logic
- `src/game/*/[game]-show.cpp` — Sequence display states
- `src/game/*/[game]-evaluate.cpp` — Input validation states
- `include/game/*/[game]-resources.hpp` — LED state and timing constants
- `include/game/*/[game].hpp` — Config and session struct definitions

---

## Conclusion

The Wave 18 redesigns successfully unified the visual experience across all FDN minigames, but introduced **~1,260 lines of duplicated code** (~31% of total minigame LOC). This duplication is highly extractable due to its consistency.

**Next Steps**:
1. Create GitHub issue for Priority 1 extraction (base state classes)
2. Assign to Wave 20 or 21 (after current sprint work completes)
3. Update this report after extraction to measure actual savings

**Long-Term Vision**:
- Minigame framework with base classes for intro/gameplay/outcome states
- Declarative config-driven game definition (title, LED states, hard mode criteria)
- Reduced maintenance burden when adding new minigames
