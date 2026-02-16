# Wave 19 — Post-Redesign Duplication Audit

**Date**: 2026-02-16
**Agent**: Agent 05
**Context**: Post-Wave 18 analysis after 7 visual redesigns
**Issue**: #167 (Code Duplication Tracking)

## Executive Summary

Wave 18 completed 7 visual redesigns across all FDN minigames. This audit assesses the current duplication landscape and identifies remaining extraction opportunities. **Key finding: `seedRng()` has been successfully extracted to the MiniGame base class, eliminating 42 lines of duplication.** However, significant structural duplication remains in state lifecycle patterns.

### Current Metrics (Post-Wave 18)

| Minigame | Total Lines (src) | States | Notes |
|----------|------------------|--------|-------|
| Ghost Runner | 1,064 | 6 | Intro→Show→Gameplay→Evaluate→Win/Lose |
| Cipher Path | 1,221 | 6 | Intro→Show→Gameplay→Evaluate→Win/Lose |
| Signal Echo | 766 | 6 | Intro→Show→Input→Evaluate→Win/Lose |
| Spike Vector | 808 | 6 | Intro→Show→Gameplay→Evaluate→Win/Lose |
| Exploit Sequencer | 845 | 6 | Intro→Show→Gameplay→Evaluate→Win/Lose |
| Breach Defense | 616 | 4 | Intro→Gameplay→Win/Lose (simplified) |
| Firewall Decrypt | 725 | 5 | Intro→Scan→Evaluate→Win/Lose |
| **Total** | **6,045** | **39 states** | Avg 863 lines per game |

## Progress from Wave 18

### ✅ Improvements Shipped

1. **seedRng() Extracted** (42 lines saved)
   - Previously: 7 identical implementations across all games
   - Now: Single implementation in `MiniGame::seedRng(unsigned long rngSeed)` (minigame.hpp:69-75)
   - Usage: `seedRng(config.rngSeed)` in each game's `populateStateMap()`

2. **Signal Echo Renderer Created** (signal-echo-resources.hpp:144-296)
   - Dedicated `SignalEchoRenderer` namespace with reusable drawing helpers
   - 150 lines of extracted rendering logic
   - Functions: `getLayout()`, `drawUpArrow()`, `drawDownArrow()`, `drawSlotEmpty()`, `drawSlotFilled()`, `drawAllSlots()`, `drawHUD()`, `drawSeparators()`, `drawControls()`, `drawProgressBar()`
   - Demonstrates the target pattern for other games

### ⚠️ Remaining Duplication Hotspots

## Duplication Cluster 1: Intro States (High Similarity: ~85%)

All 7 intro states follow an identical pattern with only text/LED differences.

**Pattern Template**:
```cpp
void GameIntro::onStateMounted(Device* PDN) {
    transitionToNextState = false;

    // 1. Reset session (IDENTICAL across all games)
    game->getSession().reset();
    game->resetGame();

    // 2. Set start time (IDENTICAL)
    PlatformClock* clock = SimpleTimer::getPlatformClock();
    game->setStartTime(clock != nullptr ? clock->milliseconds() : 0);

    // 3. Display title (DIFFERS: text strings only)
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("GAME TITLE", x, y)
        ->drawText("Subtitle", x, y);
    PDN->getDisplay()->render();

    // 4. Start LED animation (DIFFERS: LED state constant only)
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 16;
    config.curve = EaseCurve::LINEAR;
    config.initialState = GAME_IDLE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    // 5. Start intro timer (IDENTICAL)
    introTimer.setTimer(INTRO_DURATION_MS);
}
```

**Example Comparison**:

| Aspect | Ghost Runner (64 lines) | Signal Echo (62 lines) | Breach Defense (58 lines) |
|--------|------------------------|------------------------|---------------------------|
| Reset session | Lines 22-23 (identical) | Lines 17-18 (identical) | Lines 21-22 (identical) |
| Set start time | Lines 25-26 (identical) | Lines 20-21 (identical) | Lines 24-25 (identical) |
| Title text | "GHOST RUNNER" | "CYPHER RECALL" | "BREACH DEFENSE" |
| LED state | GHOST_RUNNER_IDLE_STATE | SIGNAL_ECHO_IDLE_STATE | LEDState() (default) |
| Timer setup | Line 47 (identical) | Line 45 (identical) | Line 41 (identical) |

**Lines duplicated**: ~55 lines × 7 games = **~385 lines**

**File References**:
- ghost-runner-intro.cpp:16-48
- echo-intro.cpp:13-46
- breach-defense-intro.cpp:15-42
- cipher-path-intro.cpp (similar pattern)
- spike-vector-intro.cpp (similar pattern)
- exploit-sequencer-intro.cpp (similar pattern)
- decrypt-intro.cpp (similar pattern)

## Duplication Cluster 2: Win States (High Similarity: ~90%)

Win states are almost structurally identical except for:
1. Victory message text (1 line)
2. Hard mode detection logic (2-5 lines)
3. LED animation state constant (1 line)

**Pattern Template**:
```cpp
void GameWin::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();
    auto& config = game->getConfig();

    // Hard mode detection (DIFFERS: 2-5 lines, game-specific logic)
    bool isHard = (config.someParam >= threshold && config.otherParam <= limit);

    // Create outcome (IDENTICAL structure)
    MiniGameOutcome winOutcome;
    winOutcome.result = MiniGameResult::WON;
    winOutcome.score = session.score;
    winOutcome.hardMode = isHard;
    game->setOutcome(winOutcome);

    // Display victory (DIFFERS: text string only)
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("VICTORY TEXT", x, y);
    [optional: draw score]
    PDN->getDisplay()->render();

    // Win LED animation (DIFFERS: LED state constant only)
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::VERTICAL_CHASE;
    ledConfig.speed = 5;
    ledConfig.curve = EaseCurve::EASE_IN_OUT;
    ledConfig.initialState = GAME_WIN_STATE;
    ledConfig.loopDelayMs = 500;
    ledConfig.loop = true;
    PDN->getLightManager()->startAnimation(ledConfig);

    // Celebration haptic (IDENTICAL)
    PDN->getHaptics()->setIntensity(200);

    winTimer.setTimer(WIN_DISPLAY_MS);
}

void GameWin::onStateLoop(Device* PDN) {
    if (winTimer.expired()) {
        PDN->getHaptics()->off();
        if (!game->getConfig().managedMode) {
            transitionToIntroState = true;
        } else {
            PDN->returnToPreviousApp();
        }
    }
}

void GameWin::onStateDismounted(Device* PDN) {
    winTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool GameWin::isTerminalState() const {
    return game->getConfig().managedMode;
}
```

**Example Hard Mode Detection Differences**:

| Game | Hard Mode Logic (File:Line) |
|------|----------------------------|
| Ghost Runner | `config.cols >= 7 && config.rows >= 5` (ghost-runner-win.cpp:24) |
| Signal Echo | `config.sequenceLength >= 11 && config.numSequences >= 5` (echo-win.cpp:19-20) |
| Breach Defense | `config.numLanes >= 5 && config.missesAllowed <= 1` (breach-defense-win.cpp:21) |
| Cipher Path | (needs inspection) |
| Spike Vector | (needs inspection) |
| Exploit Sequencer | (needs inspection) |
| Firewall Decrypt | (needs inspection) |

**Lines duplicated**: ~75 lines × 7 games = **~525 lines** (excluding 3-5 lines of game-specific hard mode logic per game)

**File References**:
- ghost-runner-win.cpp:17-86
- echo-win.cpp:13-79
- breach-defense-win.cpp:15-67

## Duplication Cluster 3: Lose States (Moderate Similarity: ~75%)

Lose states have more variation than win states due to different animation effects:
- **Simple pattern** (Ghost Runner, Breach Defense): Basic text + red LED fade
- **Animated pattern** (Signal Echo): Scramble animation before message
- **Flash pattern** (Cipher Path): Spark flash animation

**Simple Lose Pattern** (5 games use this):
```cpp
void GameLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;

    auto& session = game->getSession();

    // Create outcome (IDENTICAL)
    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = false;
    game->setOutcome(loseOutcome);

    // Display failure message (DIFFERS: text only)
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("FAILURE TEXT", x, y);
    PDN->getDisplay()->render();

    // Red fade LED (DIFFERS: LED state constant only)
    AnimationConfig ledConfig;
    ledConfig.type = AnimationType::IDLE;
    ledConfig.speed = 8;
    ledConfig.curve = EaseCurve::LINEAR;
    ledConfig.initialState = GAME_LOSE_STATE;
    ledConfig.loopDelayMs = 0;
    ledConfig.loop = true;
    PDN->getLightManager()->startAnimation(ledConfig);

    // Strong haptic (IDENTICAL)
    PDN->getHaptics()->max();

    loseTimer.setTimer(LOSE_DISPLAY_MS);
}
```

**Lines duplicated (simple pattern)**: ~65 lines × 5 games = **~325 lines**

**Special Cases**:
- Signal Echo: 111 lines (includes scramble animation + 2-phase timer logic)
- Cipher Path: 112 lines (includes spark flash animation)

**File References**:
- ghost-runner-lose.cpp:16-74
- echo-lose.cpp:14-111 (complex)
- cipher-path-lose.cpp:16-112 (complex)
- breach-defense-lose.cpp (needs inspection)

## Duplication Cluster 4: populateStateMap() (Moderate Similarity: ~80%)

Every game has nearly identical state map wiring. Only class names and state constants differ.

**Pattern**:
```cpp
void Game::populateStateMap() {
    seedRng(config.rngSeed);  // ✅ NOW UNIFIED (was duplicated)

    // Create states (DIFFERS: class names only)
    GameIntro* intro = new GameIntro(this);
    GameShow* show = new GameShow(this);
    GameGameplay* gameplay = new GameGameplay(this);
    GameEvaluate* evaluate = new GameEvaluate(this);
    GameWin* win = new GameWin(this);
    GameLose* lose = new GameLose(this);

    // Wire transitions (IDENTICAL structure, different class names)
    intro->addTransition(
        new StateTransition(
            std::bind(&GameIntro::transitionToShow, intro),
            show));

    // ... 5-10 more identical transition wiring calls ...

    // Push to state map (IDENTICAL structure)
    stateMap.push_back(intro);
    stateMap.push_back(show);
    // ... etc
}
```

**Example Comparison**:

| Game | Lines | States | Transition Count |
|------|-------|--------|------------------|
| Ghost Runner | 72 | 6 | 8 transitions |
| Signal Echo | 68 | 6 | 8 transitions |
| Breach Defense | 45 | 4 | 6 transitions |

**Lines duplicated**: ~60 lines × 7 games = **~420 lines**

**File References**:
- ghost-runner.cpp:9-72
- signal-echo.cpp:11-68
- breach-defense.cpp:4-45

## Duplication Cluster 5: State Transition Methods (Low Value)

All states have identical transition method patterns:
```cpp
bool GameState::transitionToNext() {
    return transitionToNextState;
}
```

**Lines duplicated**: ~3 lines × 39 states = **~117 lines**

While technically duplicated, these are small and extraction may not improve readability.

## Visual Redesign Impact Analysis

Wave 18 focused on **visual redesigns** for all 7 minigames. Based on code inspection:

### Changes Observed:
1. **New rendering patterns**: Some games (Signal Echo) gained dedicated renderer namespaces
2. **Improved display layouts**: HUD, separators, progress bars standardized
3. **Better LED color palettes**: More expressive LED states for feedback
4. **Enhanced animations**: Intro/lose states have richer visual feedback

### Code Size Impact:
- **Breach Defense**: Smallest at 616 lines (simplified 4-state flow)
- **Cipher Path**: Largest at 1,221 lines (complex path rendering + spark animation)
- **Average game size**: 863 lines

The redesigns **added visual richness** but did not significantly reduce structural duplication. This is expected — visual improvements are orthogonal to state lifecycle refactoring.

## Extraction Recommendations

### Priority 1: BaseIntroState Template (HIGH IMPACT, MEDIUM RISK)

**Savings**: ~385 lines
**Effort**: 2-3 days
**Risk**: Medium (requires careful template design)

Create a templated base intro state:

```cpp
template <typename GameType>
class BaseIntroState : public State {
protected:
    GameType* game;
    SimpleTimer introTimer;
    bool transitionToNextState = false;

    // Game-specific overrides (3 methods)
    virtual const char* getGameTitle() const = 0;
    virtual const char* getGameSubtitle() const = 0;
    virtual const LEDState* getIdleLEDState() const = 0;

    // Common lifecycle (handled by base)
    void onStateMounted(Device* PDN) override {
        transitionToNextState = false;
        game->getSession().reset();
        game->resetGame();

        PlatformClock* clock = SimpleTimer::getPlatformClock();
        game->setStartTime(clock != nullptr ? clock->milliseconds() : 0);

        PDN->getDisplay()->invalidateScreen();
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText(getGameTitle(), 10, 20)
            ->drawText(getGameSubtitle(), 10, 45);
        PDN->getDisplay()->render();

        AnimationConfig config;
        config.type = AnimationType::IDLE;
        config.speed = 16;
        config.curve = EaseCurve::LINEAR;
        config.initialState = *getIdleLEDState();
        config.loopDelayMs = 0;
        config.loop = true;
        PDN->getLightManager()->startAnimation(config);

        introTimer.setTimer(INTRO_DURATION_MS);
    }

    void onStateLoop(Device* PDN) override {
        if (introTimer.expired()) {
            transitionToNextState = true;
        }
    }

    void onStateDismounted(Device* PDN) override {
        introTimer.invalidate();
        transitionToNextState = false;
    }
};

// Game-specific intro: only 10 lines
class GhostRunnerIntro : public BaseIntroState<GhostRunner> {
    const char* getGameTitle() const override { return "GHOST RUNNER"; }
    const char* getGameSubtitle() const override { return "Navigate from memory."; }
    const LEDState* getIdleLEDState() const override { return &GHOST_RUNNER_IDLE_STATE; }
};
```

**Benefits**:
- Reduce 7 intro files from ~60 lines to ~10 lines each
- Guarantee consistent intro behavior
- New minigames: just override 3 methods

**Challenges**:
- Some intros have game-specific setup (e.g., Signal Echo generates sequence in intro)
- Need to design for extensibility (optional hooks?)

### Priority 2: BaseWinState Template (HIGH IMPACT, MEDIUM RISK)

**Savings**: ~525 lines
**Effort**: 3-4 days
**Risk**: Medium (hard mode logic varies)

Create a templated base win state:

```cpp
template <typename GameType>
class BaseWinState : public State {
protected:
    GameType* game;
    SimpleTimer winTimer;
    bool transitionToIntroState = false;

    // Game-specific overrides (4 methods)
    virtual const char* getVictoryText() const = 0;
    virtual const LEDState* getWinLEDState() const = 0;
    virtual bool calculateHardMode() const = 0;
    virtual void drawVictoryScreen(Device* PDN, int score) const {
        // Default implementation: just title
        PDN->getDisplay()->invalidateScreen();
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText(getVictoryText(), 15, 20);

        std::string scoreStr = "Score: " + std::to_string(score);
        PDN->getDisplay()->drawText(scoreStr.c_str(), 30, 40);
        PDN->getDisplay()->render();
    }

    // Common lifecycle
    void onStateMounted(Device* PDN) override {
        transitionToIntroState = false;

        auto& session = game->getSession();
        bool isHard = calculateHardMode();

        MiniGameOutcome winOutcome;
        winOutcome.result = MiniGameResult::WON;
        winOutcome.score = session.score;
        winOutcome.hardMode = isHard;
        game->setOutcome(winOutcome);

        drawVictoryScreen(PDN, session.score);

        AnimationConfig ledConfig;
        ledConfig.type = AnimationType::VERTICAL_CHASE;
        ledConfig.speed = 5;
        ledConfig.curve = EaseCurve::EASE_IN_OUT;
        ledConfig.initialState = *getWinLEDState();
        ledConfig.loopDelayMs = 500;
        ledConfig.loop = true;
        PDN->getLightManager()->startAnimation(ledConfig);

        PDN->getHaptics()->setIntensity(200);
        winTimer.setTimer(WIN_DISPLAY_MS);
    }

    void onStateLoop(Device* PDN) override {
        if (winTimer.expired()) {
            PDN->getHaptics()->off();
            if (!game->getConfig().managedMode) {
                transitionToIntroState = true;
            } else {
                PDN->returnToPreviousApp();
            }
        }
    }

    void onStateDismounted(Device* PDN) override {
        winTimer.invalidate();
        transitionToIntroState = false;
        PDN->getHaptics()->off();
    }

    bool isTerminalState() const override {
        return game->getConfig().managedMode;
    }
};

// Game-specific win: only 8 lines
class GhostRunnerWin : public BaseWinState<GhostRunner> {
    const char* getVictoryText() const override { return "MAZE CLEARED"; }
    const LEDState* getWinLEDState() const override { return &GHOST_RUNNER_WIN_STATE; }
    bool calculateHardMode() const override {
        auto& c = game->getConfig();
        return (c.cols >= 7 && c.rows >= 5);
    }
};
```

**Benefits**:
- Reduce 7 win files from ~75 lines to ~8 lines each
- Centralize win behavior (haptic, timer, terminal state logic)
- Hard mode logic becomes explicit and reviewable

### Priority 3: BaseLoseState Template (MEDIUM IMPACT, HIGHER RISK)

**Savings**: ~325 lines (simple pattern only)
**Effort**: 2-3 days
**Risk**: Medium-High (2 games have complex animations)

**Challenge**: Signal Echo and Cipher Path have custom lose animations that don't fit a simple template. Options:
1. Extract only the 5 simple lose states
2. Design a more flexible base with animation hooks
3. Leave complex lose states as-is, only template simple ones

**Recommended**: Start with simple pattern extraction (5 games), leave Signal Echo and Cipher Path as custom implementations.

### Priority 4: Renderer Namespaces for Other Games (MEDIUM IMPACT, LOW RISK)

**Savings**: ~200-300 lines across games
**Effort**: 1-2 days per game
**Risk**: Low (rendering logic is isolated)

**Model**: Signal Echo's `SignalEchoRenderer` namespace (signal-echo-resources.hpp:144-296)

Games that would benefit from dedicated renderers:
1. **Ghost Runner**: Maze drawing, HUD, solution path rendering (currently in ghost-runner-show.cpp:132-254)
2. **Cipher Path**: Grid rendering, path visualization
3. **Spike Vector**: Spike pattern display
4. **Breach Defense**: Lane rendering, breach indicators

**Pattern**:
```cpp
// game-resources.hpp
namespace GameRenderer {
    struct Layout { /* layout params */ };

    inline Layout getLayout(GameConfig config) { /* ... */ }
    inline void drawHUD(Display* d, int round, int lives) { /* ... */ }
    inline void drawGameArea(Display* d, GameSession& session) { /* ... */ }
    inline void drawControls(Display* d) { /* ... */ }
}
```

**Benefits**:
- Isolate rendering logic from state machine logic
- Easier to test rendering separately
- Facilitates visual redesigns without touching state code

### Priority 5: populateStateMap() Helper (LOW-MEDIUM IMPACT, MEDIUM RISK)

**Savings**: ~420 lines
**Effort**: 3-5 days
**Risk**: Medium (need to handle varying state counts: 4-6 states per game)

**Challenge**: Games have different numbers of states:
- Breach Defense: 4 states (Intro → Gameplay → Win/Lose)
- Signal Echo: 6 states (Intro → Show → Input → Evaluate → Win/Lose)
- Ghost Runner: 6 states (Intro → Show → Gameplay → Evaluate → Win/Lose)

**Possible approach**: State factory pattern with variadic templates or builder pattern.

**Recommendation**: **DEFER** until more games converge on a standard state count. Current variation makes a universal helper complex.

## Updated Duplication Baseline

### Duplication Metrics (Current)

| Category | Lines Duplicated | Priority |
|----------|------------------|----------|
| Intro States | ~385 | HIGH |
| Win States | ~525 | HIGH |
| Lose States (simple) | ~325 | MEDIUM |
| populateStateMap() | ~420 | LOW-MEDIUM |
| State transition methods | ~117 | LOW |
| **Total** | **~1,772 lines** | — |

### Previous Baseline (Wave 13, pre-extraction)
- Total duplicate tokens: 8,613
- Total duplicate blocks: 104
- Timestamp: 2026-02-15

### Improvements Since Wave 13
- **seedRng() extracted**: -42 lines
- **Signal Echo renderer created**: -150 lines (isolated to one game)
- **Total reduction**: ~192 lines

### Estimated Savings from Recommended Extractions
- Priority 1 (BaseIntroState): -385 lines
- Priority 2 (BaseWinState): -525 lines
- Priority 3 (BaseLoseState simple): -325 lines
- Priority 4 (Renderers): -250 lines
- **Total potential**: **-1,485 lines** (~24% of current codebase)

## Implementation Roadmap

### Phase 1: Low-Risk Extraction (Weeks 1-2)
1. Create renderer namespaces for Ghost Runner, Cipher Path (following Signal Echo pattern)
2. Extract common display helper functions (HUD, separators, progress bars)

### Phase 2: Template Base States (Weeks 3-6)
1. BaseIntroState template (week 3-4)
2. BaseWinState template (week 5-6)
3. Test all 7 games after each extraction

### Phase 3: Lose State Consolidation (Weeks 7-8)
1. BaseLoseState for 5 simple games
2. Document Signal Echo & Cipher Path as "custom lose animations"

### Phase 4: State Map Refactoring (Weeks 9-10, optional)
1. Evaluate state map helper feasibility after template extractions
2. Only proceed if games have converged on common patterns

## Testing Strategy

For each extraction:
1. **Unit tests**: Verify state lifecycle for all 7 games
2. **CLI playthroughs**: Full game sessions for each minigame
3. **Visual regression**: Compare screenshots before/after (Wave 18 baselines)
4. **Binary size**: Track flash usage (should decrease)
5. **Build time**: Measure compilation time (templates may increase)

## Risk Mitigation

1. **Incremental rollout**: Extract one template at a time, merge when stable
2. **Feature flags**: Use `#ifdef LEGACY_STATES` to keep old implementations during transition
3. **Parallel implementations**: Run both old and new in tests, compare outcomes
4. **Rollback plan**: Keep old state files in `deprecated/` until full validation

## Conclusion

Wave 18's visual redesigns improved game presentation without addressing structural duplication. The codebase is now **mature enough for aggressive consolidation**. With `seedRng()` already extracted, the next logical step is **templated base states** for intro/win/lose, which would eliminate ~1,235 lines of boilerplate while improving maintainability.

**Recommended next wave**: Extract `BaseIntroState` and `BaseWinState` templates (combined savings: 910 lines, ~15% reduction).

---

**Generated by Agent 05 — Wave 19**
**Last Updated**: 2026-02-16
