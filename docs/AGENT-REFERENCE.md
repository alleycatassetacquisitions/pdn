# PDN Agent Reference — API Cheat Sheet & Architecture Guide

> Quick-reference for all Claude agents working on the PDN codebase.
> Covers architecture, APIs, coding conventions, and common patterns.

## Architecture Block Diagram

```
┌──────────────────────────────────────────────────────────────┐
│                     APPLICATION LAYER                        │
│                                                              │
│  ┌─────────────┐  ┌───────────┐  ┌────────────────────────┐ │
│  │  Quickdraw   │  │  Konami   │  │   FDN MiniGames (×7)   │ │
│  │ StateMachine │  │ MetaGame  │  │                        │ │
│  │              │  │           │  │ SignalEcho  GhostRunner │ │
│  │ 20+ states   │  │ 5 states  │  │ SpikeVector CipherPath │ │
│  │ (idle,duel,  │  │ (entry,   │  │ ExploitSeq  BreachDef  │ │
│  │  handshake,  │  │  display, │  │ FirewallDecrypt        │ │
│  │  fdn, etc.)  │  │  result)  │  │                        │ │
│  └──────┬───────┘  └─────┬─────┘  └───────────┬────────────┘ │
│         │                │                     │              │
│         ▼                ▼                     ▼              │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │              StateMachine Base (state-machine.hpp)        │ │
│  │  populateStateMap() → initialize() → loop:               │ │
│  │    mount → onStateLoop → checkTransitions → dismount     │ │
│  └──────────────────────┬───────────────────────────────────┘ │
│                         │                                     │
│  ┌──────────────────────┴───────────────────────────────────┐ │
│  │               State Base (state.hpp)                      │ │
│  │  onStateMounted() · onStateLoop() · onStateDismounted()   │ │
│  │  onStatePaused() · onStateResumed() · isTerminalState()   │ │
│  │  addTransition(StateTransition*)                          │ │
│  └──────────────────────────────────────────────────────────┘ │
└──────────────────────────┬───────────────────────────────────┘
                           │
┌──────────────────────────┴───────────────────────────────────┐
│                   DEVICE / HAL LAYER                          │
│                                                               │
│  Device (abstract) → PDN (concrete)                           │
│                                                               │
│  ┌─────────┐ ┌────────┐ ┌──────────┐ ┌────────────────────┐  │
│  │ Display │ │ Button │ │ Haptics  │ │   LightManager     │  │
│  │ ·invalidate │·setButtonPress│·setIntensity│·startAnimation│  │
│  │ ·setGlyph   │·CLICK/PRESS  │·max()      │·AnimationConfig│  │
│  │ ·drawText   │·LONG_PRESS   │·off()      │                │  │
│  │ ·drawXBMP   │              │            │                │  │
│  │ ·render     │              │            │                │  │
│  └─────────┘ └────────┘ └──────────┘ └────────────────────┘  │
│  ┌──────────┐ ┌────────────┐ ┌─────────┐ ┌────────────────┐  │
│  │  Serial  │ │ PeerComms  │ │  HTTP   │ │    Storage     │  │
│  └──────────┘ └────────────┘ └─────────┘ └────────────────┘  │
│                                                               │
│  Platform-Specific:  ESP32-S3 drivers  │  Native CLI drivers  │
└───────────────────────────────────────────────────────────────┘
```

## MiniGame State Flow (All 7 Games)

```
Every minigame follows the EXACT same state sequence:

  ┌─────────┐     ┌──────┐     ┌──────────┐     ┌──────────┐
  │  Intro  │────▶│ Show │────▶│ Gameplay │────▶│ Evaluate │
  └─────────┘     └──────┘     └──────────┘     └──────────┘
       ▲              ▲                              │
       │              └──────── next round ──────────┤
       │                                             │
       │         ┌──────┐                  ┌────────┐│
       └─────────│ Win  │◀─── all rounds ──┤        ││
       │         └──────┘     completed    │        ││
       │                                   │        ▼│
       │         ┌──────┐                  │  strikes > allowed
       └─────────│ Lose │◀─────────────────┘        │
                 └──────┘                            │

  Terminal behavior:
    managedMode=true  → PDN->returnToPreviousApp()
    managedMode=false → loop back to Intro
```

## API Quick Reference

### State Lifecycle

```cpp
// Every state implements these (override from State base):
void onStateMounted(Device* PDN);     // Called once on entry — setup
void onStateLoop(Device* PDN);        // Called every tick — game logic
void onStateDismounted(Device* PDN);  // Called once on exit — cleanup

// Optional:
std::unique_ptr<Snapshot> onStatePaused(Device* PDN);  // App switching
void onStateResumed(Device* PDN, Snapshot*);            // Resume from pause
bool isTerminalState() const;                           // true = app exits
```

### Display (Fluent API)

```cpp
PDN->getDisplay()
    ->invalidateScreen()                    // Clear buffer
    ->setGlyphMode(FontMode::TEXT)          // Set font mode
    ->drawText("TITLE", 10, 25)            // Draw at x,y
    ->drawText("subtitle", 10, 50)
    ->render();                             // Flush to screen

// XBM images:
PDN->getDisplay()->drawXBMP(x, y, width, height, bitmapData);
```

### Button Input

```cpp
// Lambda callback pattern:
parameterizedCallbackFunction callback = [](void* ctx) {
    auto* state = static_cast<MyState*>(ctx);
    state->handleInput();
};
PDN->getPrimaryButton()->setButtonPress(callback, this, ButtonInteraction::CLICK);
PDN->getSecondaryButton()->setButtonPress(callback, this, ButtonInteraction::CLICK);

// Interactions: CLICK, PRESS, LONG_PRESS
```

### LED Animations

```cpp
AnimationConfig config;
config.type = AnimationType::IDLE;          // or VERTICAL_CHASE
config.speed = 16;                          // Animation speed
config.curve = EaseCurve::LINEAR;           // or EASE_IN_OUT, EASE_OUT
config.initialState = GAME_IDLE_STATE;      // LEDState array constant
config.loopDelayMs = 0;                     // Delay between loops
config.loop = true;                         // Repeat animation
PDN->getLightManager()->startAnimation(config);
```

### Haptics

```cpp
PDN->getHaptics()->setIntensity(200);   // 0-255
PDN->getHaptics()->max();               // Full intensity
PDN->getHaptics()->off();               // Stop
```

### Timer

```cpp
SimpleTimer myTimer;

// In onStateMounted:
myTimer.setTimer(DURATION_MS);

// In onStateLoop:
if (myTimer.expired()) { /* transition */ }

// In onStateDismounted:
myTimer.invalidate();
```

### State Transitions

```cpp
// In populateStateMap():
intro->addTransition(
    new StateTransition(
        std::bind(&IntroState::transitionToShow, intro),  // condition
        show));                                            // target state

// Transition condition = bool getter on the source state:
bool IntroState::transitionToShow() {
    return transitionToShowState;  // set to true when ready
}
```

### MiniGame Config/Session Pattern

```cpp
// Config: difficulty tuning (set once, read-only during game)
struct GameConfig {
    int rounds = 5;
    int missesAllowed = 3;
    int rngSeed = 0;
    bool managedMode = false;  // true when launched from Quickdraw
};

// Session: per-game runtime state (reset each game)
struct GameSession {
    int score = 0;
    int currentRound = 0;
    int strikes = 0;
    void reset() { score = 0; currentRound = 0; strikes = 0; }
};

// Difficulty presets:
inline GameConfig makeEasyConfig() { return {5, 3, 0, false}; }
inline GameConfig makeHardConfig() { return {8, 1, 0, false}; }
const GameConfig GAME_EASY = makeEasyConfig();
const GameConfig GAME_HARD = makeHardConfig();
```

### MiniGame Outcome

```cpp
MiniGameOutcome outcome;
outcome.result = MiniGameResult::WON;    // or LOST, IN_PROGRESS
outcome.score = session.score;
outcome.hardMode = isHard;
game->setOutcome(outcome);
```

## Coding Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Classes | PascalCase | `GhostRunner`, `StateMachine` |
| Methods | camelCase | `onStateMounted()`, `getConfig()` |
| Constants | SCREAMING_SNAKE_CASE | `INTRO_DURATION_MS`, `MAX_ROUNDS` |
| State IDs | SCREAMING_SNAKE_CASE | `GHOST_INTRO`, `ECHO_WIN` |
| File names | kebab-case | `ghost-runner-win.cpp` |
| Headers | .hpp extension | `ghost-runner-states.hpp` |
| Header guards | `#pragma once` | |
| Indentation | 4 spaces (no tabs) | |
| Braces | Same line (K&R) | `void foo() {` |
| Log tags | `static const char* TAG = "ClassName";` | |

## Commit Message Format

```
{type}: {description} (#{issue})

Types: feat, fix, chore, refactor, docs
Examples:
  feat: KonamiCodeEntry + Accept/Reject states (#121)
  fix: resolve CLI test SIGSEGV — platform clock ordering (#146)
```

## File Organization

```
include/game/{game-name}/
    {game}.hpp              # Config, Session, class definition, presets
    {game}-states.hpp       # State class declarations, state ID enum
    {game}-resources.hpp    # LED states, timing constants, XBM data

src/game/{game-name}/
    {game}.cpp              # populateStateMap(), resetGame(), seedRng()
    {game}-intro.cpp        # Intro state implementation
    {game}-show.cpp         # Show state implementation
    {game}-gameplay.cpp     # Gameplay state implementation
    {game}-evaluate.cpp     # Evaluate state implementation
    {game}-win.cpp          # Win state implementation
    {game}-lose.cpp         # Lose state implementation
```

## Test Conventions

### test_core/ (Unit Tests)
- Test logic in `.hpp` headers
- All registered in `test/test_core/tests.cpp`
- Fixtures extend GoogleTest `::testing::Test`

### test_cli/ (CLI Tests)
- **Split pattern**: logic in `.hpp`, registrations in per-domain `.cpp`
- Add TEST_F to your game's registration file ONLY

```cpp
// In {game}-tests.hpp:
void myTestFunction(GameTestSuite* suite) {
    // test logic
}

// In {game}-reg-tests.cpp:
TEST_F(GameTestSuite, MyTest) {
    myTestFunction(this);
}
```

## Build Commands

```bash
pio run -e native_cli                    # Build CLI simulator
.pio/build/native_cli/program 2          # Run with 2 devices
pio test -e native                       # Core unit tests
pio test -e native_cli_test              # CLI tests
pio run -e esp32-s3_release              # Hardware build

# DO NOT: pio test -e native_cli (build-only env, will fail)
```

## CLI Simulator Commands

```bash
cable 0 1          # Connect device 0 to device 1
press 0 primary    # Button press on device 0
state              # Show all device states
progress 0         # Show Konami progress
stats 0            # Show player stats
help               # All commands
```

---
*Generated by Agent 01 — Last Updated: 2026-02-15*
