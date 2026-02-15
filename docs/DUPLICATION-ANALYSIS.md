# PDN Duplication Analysis & Anti-Bloat Playbook

> Findings from codebase audit — Feb 2026
> ~2,000 lines of copy-paste boilerplate identified across 7 minigames.

## Executive Summary

All 7 FDN minigames (Ghost Runner, Signal Echo, Spike Vector, Cipher Path,
Exploit Sequencer, Breach Defense, Firewall Decrypt) follow an identical
state flow: Intro → Show → Gameplay → Evaluate → Win/Lose. The states
were implemented by duplicating code across games, changing only:

- Game title text strings
- LED state constants
- Hard-mode detection logic (config param thresholds)
- Game-specific evaluate logic

Everything else is structurally identical.

## Duplication Hotspot Map

```
SEVERITY    COPIES   LINES/COPY   TOTAL    WHAT
────────────────────────────────────────────────────────────────
CRITICAL    ×7       65 lines     455      populateStateMap() boilerplate
CRITICAL    ×7       50 lines     350      Win state (onStateMounted)
CRITICAL    ×7       45 lines     315      Intro state (full lifecycle)
CRITICAL    ×7       45 lines     315      Lose state (full lifecycle)
HIGH        ×7       30 lines     210      Evaluate state (framework)
MEDIUM      ×7        6 lines      42      seedRng() (identical)
MEDIUM      ×6       20 lines     120      Show state display rendering
MEDIUM      ×5       15 lines      75      Button callback registration
────────────────────────────────────────────────────────────────
                              TOTAL: ~1,882 lines
```

## Hotspot #1: populateStateMap() — 455 duplicated lines

Every game has this exact same wiring pattern. Only class names differ.

**Ghost Runner** (`src/game/ghost-runner/ghost-runner.cpp:9-72`):
```cpp
void GhostRunner::populateStateMap() {
    seedRng();
    GhostRunnerIntro* intro = new GhostRunnerIntro(this);
    GhostRunnerShow* show = new GhostRunnerShow(this);
    GhostRunnerGameplay* gameplay = new GhostRunnerGameplay(this);
    GhostRunnerEvaluate* evaluate = new GhostRunnerEvaluate(this);
    GhostRunnerWin* win = new GhostRunnerWin(this);
    GhostRunnerLose* lose = new GhostRunnerLose(this);
    intro->addTransition(new StateTransition(std::bind(&GhostRunnerIntro::transitionToShow, intro), show));
    // ... 8 more identical transition wiring calls ...
    stateMap.push_back(intro);    // 0
    stateMap.push_back(show);     // 1
    // ... etc
}
```

**Signal Echo** (`src/game/signal-echo/signal-echo.cpp:11-68`):
```cpp
void SignalEcho::populateStateMap() {
    seedRng();
    EchoIntro* intro = new EchoIntro(this);
    EchoShowSequence* showSequence = new EchoShowSequence(this);
    // ... exact same pattern, different class names ...
}
```

**Refactoring opportunity**: Template or macro-based state map wiring.

## Hotspot #2: Win State — 350 duplicated lines

Side-by-side comparison of two win states (95% identical):

```
GHOST RUNNER (ghost-runner-win.cpp)    │  SIGNAL ECHO (echo-win.cpp)
───────────────────────────────────────│──────────────────────────────
transitionToIntroState = false;        │  transitionToIntroState = false;
auto& session = game->getSession();    │  auto& session = game->getSession();
auto& config = game->getConfig();      │
                                       │
int zoneWidth = config.targetZoneEnd   │  bool isHard =
    - config.targetZoneStart;          │    (game->getConfig().allowedMistakes
bool isHard = (config.missesAllowed    │        <= 1 &&
    <= 1 && zoneWidth <= 16);          │     game->getConfig().sequenceLength
                                       │        >= 8);
                                       │
MiniGameOutcome winOutcome;            │  MiniGameOutcome winOutcome;
winOutcome.result = MiniGameResult::   │  winOutcome.result = MiniGameResult::
    WON;                               │      WON;
winOutcome.score = session.score;      │  winOutcome.score = session.score;
winOutcome.hardMode = isHard;          │  winOutcome.hardMode = isHard;
game->setOutcome(winOutcome);          │  game->setOutcome(winOutcome);
                                       │
PDN->getDisplay()->invalidateScreen(); │  PDN->getDisplay()->invalidateScreen();
PDN->getDisplay()->setGlyphMode(       │  PDN->getDisplay()->setGlyphMode(
    FontMode::TEXT)                    │      FontMode::TEXT)
    ->drawText("RUN COMPLETE", 10, 25) │      ->drawText("ACCESS GRANTED",5,25)
                                       │
// ... LED animation (identical) ...   │  // ... LED animation (identical) ...
// ... haptics (identical) ...         │  // ... haptics (identical) ...
// ... timer (identical) ...           │  // ... timer (identical) ...
```

**What differs**: Only the `isHard` detection logic (2-3 lines) and
the display text string (1 line). Everything else is byte-for-byte identical.

## Hotspot #3: Intro State — 315 duplicated lines

All 7 intros follow this exact template:
1. Reset session + seedRng
2. Display game title text
3. Start idle LED animation
4. Set intro timer

**What differs**: Title text, subtitle text, LED state constant name.

## Hotspot #4: Lose State — 315 duplicated lines

Same as Win but with:
- `MiniGameResult::LOST` instead of `WON`
- `hardMode = false` always
- Different display text
- `haptics->max()` instead of `setIntensity(200)`
- IDLE animation instead of VERTICAL_CHASE

## Hotspot #5: seedRng() — 42 duplicated lines

100% identical across all 7 games:
```cpp
void GameClass::seedRng() {
    if (config.rngSeed != 0) {
        srand(static_cast<unsigned int>(config.rngSeed));
    } else {
        srand(static_cast<unsigned int>(0));
    }
}
```
This should live in `MiniGame` base class.

---

## Recommended Refactoring Strategy

### Phase 1: Extract seedRng to MiniGame base (Low risk, high clarity)

Move `seedRng()` into `MiniGame` base class. It's 100% identical in all
7 games. Requires adding `rngSeed` to a common config interface or
making it a virtual accessor.

### Phase 2: Template base states for Win/Lose/Intro

Create base state templates that handle the common lifecycle:

```cpp
// Conceptual — BaseWinState handles 95% of win logic
template <typename GameType>
class BaseWinState : public State {
protected:
    GameType* game;
    SimpleTimer winTimer;
    bool transitionToIntroState = false;

    // Subclass overrides only these:
    virtual const char* victoryText() const = 0;
    virtual const LEDState* winLedState() const = 0;
    virtual bool isHardMode() const = 0;

    // Everything else is handled by the base:
    void onStateMounted(Device* PDN) override { /* common logic */ }
    void onStateLoop(Device* PDN) override { /* common logic */ }
    void onStateDismounted(Device* PDN) override { /* common logic */ }
    bool isTerminalState() const override {
        return game->getConfig().managedMode;
    }
};

// Game-specific win: only 3 overrides
class GhostRunnerWin : public BaseWinState<GhostRunner> {
    const char* victoryText() const override { return "RUN COMPLETE"; }
    const LEDState* winLedState() const override { return GHOST_RUNNER_WIN_STATE; }
    bool isHardMode() const override {
        auto& c = game->getConfig();
        return c.missesAllowed <= 1 && (c.targetZoneEnd - c.targetZoneStart) <= 16;
    }
};
```

### Phase 3: Generalized populateStateMap

Extract the standard 6-state wiring into a helper that takes state
factories as parameters, eliminating 455 lines of boilerplate.

### Phase 4: Display rendering utilities

Common patterns like "title + score" rendering, LED animation setup,
and button callback registration can be extracted into small utilities.

---

## Anti-Bloat Tooling

### Tools to Install

```bash
# Binary size profiler (analyze where flash goes)
sudo apt install -y bloaty
bloaty .pio/build/native_cli/program -d compileunits -n 20
bloaty .pio/build/native_cli/program -d symbols -n 30

# Copy-Paste Detector (find duplicated code blocks)
# Requires Java runtime
wget https://github.com/pmd/pmd/releases/download/pmd_releases%2F7.0.0/pmd-dist-7.0.0-bin.zip
unzip pmd-dist-7.0.0-bin.zip
./pmd-bin-7.0.0/bin/cpd --minimum-tokens 50 --language cpp --dir src/ --dir include/

# Static analysis (built into PlatformIO)
pio check -e native --tool cppcheck

# Include hygiene
sudo apt install -y iwyu
```

### Build Flags to Consider

```ini
# platformio.ini — size reduction flags
build_flags =
    -Os                      ; Optimize for size (not -O2)
    -ffunction-sections      ; Each function in own section
    -fdata-sections          ; Each global in own section
    -fno-rtti                ; Save ~10-20KB (no dynamic_cast)

link_flags =
    -Wl,--gc-sections        ; Strip unreferenced code
```

### Emerging C++ Patterns (C++17, already available)

**std::variant for type-safe state machines:**
```cpp
using GameState = std::variant<Idle, Ready, Draw, Result>;
// Compiler enforces exhaustive handling — no forgotten states
```

**constexpr if for platform config:**
```cpp
template <typename Config>
void init() {
    if constexpr (Config::HAS_DISPLAY) {
        display_.begin();  // Dead branch eliminated at compile time
    }
}
```

**Policy-based design for hardware abstraction:**
```cpp
template <typename FeedbackPolicy, typename DisplayPolicy>
class QuickdrawGame { /* compose behavior at compile time */ };
```

---

## Priority Action Items

| # | Action | Impact | Risk | Lines Saved |
|---|--------|--------|------|-------------|
| 1 | Move seedRng() to MiniGame base | Low | Very Low | 42 |
| 2 | Create BaseWinState template | High | Medium | ~300 |
| 3 | Create BaseLoseState template | High | Medium | ~270 |
| 4 | Create BaseIntroState template | High | Medium | ~270 |
| 5 | Generalize populateStateMap | High | Medium | ~400 |
| 6 | Extract display render helpers | Medium | Low | ~100 |
| 7 | Run Bloaty for size baseline | Diagnostic | None | — |
| 8 | Run CPD for full duplication report | Diagnostic | None | — |

**Estimated total reduction: ~1,400 lines** while improving maintainability
and making new minigame creation a 50-line task instead of 400+.

---
*Generated by Agent 01 — Last Updated: 2026-02-15*
