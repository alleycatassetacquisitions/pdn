# API Conventions

This document provides a comprehensive reference for PDN API conventions, naming patterns, and development practices. It serves as a quick-reference guide for agents and developers working on the PDN codebase.

## Table of Contents

- [Naming Conventions](#naming-conventions)
- [State Machine Patterns](#state-machine-patterns)
- [Header Organization](#header-organization)
- [Test Patterns](#test-patterns)
- [Game Structure](#game-structure)
- [PR Conventions](#pr-conventions)

---

## Naming Conventions

### File Naming

**Pattern**: `kebab-case` for all files

```
ghost-runner.hpp
state-machine.cpp
cli-device.hpp
fdn-detected-state.cpp
```

**Resources files**: Use `-resources.hpp` suffix for game resource definitions
```
ghost-runner-resources.hpp
signal-echo-resources.hpp
```

**State files**: Use `-states.hpp` suffix for game state definitions
```
ghost-runner-states.hpp
quickdraw-states.hpp
```

**Test files**: Use `-tests.hpp` suffix for test headers
```
ghost-runner-tests.hpp
signal-echo-tests.hpp
```

**Test registration files**: Use `-reg-tests.cpp` suffix for TEST_F registration files
```
ghost-runner-reg-tests.cpp
spike-vector-reg-tests.cpp
```

### Class Naming

**Pattern**: `PascalCase` for all classes, structs, and enums

```cpp
class StateMachine { };
class GhostRunner : public MiniGame { };
struct MiniGameOutcome { };
enum class MiniGameResult { };
```

### Method Naming

**Pattern**: `camelCase` for all methods and functions

```cpp
void onStateMounted(Device* PDN);
void onStateLoop(Device* PDN);
int getUserID() const;
const char* getDisplayName() const;
void seedRng(unsigned long seed);
```

**Lifecycle methods**: Standard names for state lifecycle
```cpp
void onStateMounted(Device* PDN);    // Setup phase
void onStateLoop(Device* PDN);       // Execution phase
void onStateDismounted(Device* PDN); // Cleanup phase
```

**Pausing/resuming**: For games that support pausing
```cpp
std::unique_ptr<Snapshot> onStatePaused(Device* PDN);
void onStateResumed(Device* PDN, Snapshot* snapshot);
```

### Member Variables

**Pattern**: `camelCase`, no prefix

```cpp
class GhostRunner {
private:
    GhostRunnerConfig config;
    GhostRunnerSession session;
    MiniGameOutcome outcome;
};
```

**Test suite members**: Use trailing underscore for fixture members
```cpp
class GhostRunnerTestSuite : public testing::Test {
    DeviceInstance device_;
    GhostRunner* game_ = nullptr;
};
```

### Constants and Enums

**Constants**: `SCREAMING_SNAKE_CASE`
```cpp
static constexpr int INTRO_DURATION_MS = 2000;
static constexpr int WIN_DISPLAY_MS = 3000;
constexpr int GHOST_RUNNER_APP_ID = 4;
```

**Enum types**: `PascalCase`
```cpp
enum class MiniGameResult : uint8_t { };
enum class GameType { };
enum GhostRunnerStateId { };
```

**Enum values**: `SCREAMING_CASE`
```cpp
enum class MiniGameResult : uint8_t {
    IN_PROGRESS = 0,
    WON = 1,
    LOST = 2,
};

enum GhostRunnerStateId {
    GHOST_INTRO    = 300,
    GHOST_WIN      = 301,
    GHOST_LOSE     = 302,
    GHOST_SHOW     = 303,
    GHOST_GAMEPLAY = 304,
    GHOST_EVALUATE = 305,
};
```

---

## State Machine Patterns

### State Lifecycle

Every state follows a three-phase lifecycle:

```cpp
class MyState : public State {
public:
    // Phase 1: Setup (called once when entering state)
    void onStateMounted(Device* PDN) override {
        // Initialize timers
        // Register callbacks
        // Set up hardware
        // Reset transition flags
    }

    // Phase 2: Execution (called repeatedly in main loop)
    void onStateLoop(Device* PDN) override {
        // Main game logic
        // Update state
        // Check conditions for transitions
    }

    // Phase 3: Cleanup (called once when leaving state)
    void onStateDismounted(Device* PDN) override {
        // Stop timers
        // Detach callbacks
        // Reset hardware
        // Clean up resources
    }
};
```

**Critical rules:**
- `onStateMounted()` is called exactly once when entering the state
- `onStateLoop()` is called 1-N times while in the state
- `onStateDismounted()` is called exactly once before transitioning away
- Always clean up in `onStateDismounted()` — no resource leaks

### State Transitions

States define transition conditions using `StateTransition` objects:

```cpp
class GhostRunnerIntro : public State {
public:
    void onStateMounted(Device* PDN) override {
        introTimer.setDuration(INTRO_DURATION_MS);
        transitionToShowState = false;
    }

    void onStateLoop(Device* PDN) override {
        if (introTimer.isComplete()) {
            transitionToShowState = true;
        }
    }

    bool transitionToShow() {
        return transitionToShowState;
    }

private:
    SimpleTimer introTimer;
    bool transitionToShowState = false;
};
```

**Transition registration:**
```cpp
void GhostRunner::populateStateMap() {
    auto* intro = new GhostRunnerIntro(this);
    auto* show = new GhostRunnerShow(this);

    // Register transition: intro → show
    intro->addTransition(new StateTransition(
        [intro]() { return intro->transitionToShow(); },
        show
    ));

    stateMap.push_back(intro);
    stateMap.push_back(show);
}
```

**Transition pattern:**
1. Define boolean transition flags as private members
2. Reset flags to `false` in `onStateMounted()`
3. Set flags to `true` in `onStateLoop()` when condition met
4. Create transition getter methods that return the flag value
5. Register transitions in `populateStateMap()` using lambda captures

### StateMachine Composition (IS-A-State)

`StateMachine` extends `State`, creating an IS-A-State relationship. This allows state machines to be nested as states within other state machines.

```cpp
class StateMachine : public State {
public:
    explicit StateMachine(int stateId) : State(stateId) {}

    // StateMachine overrides State lifecycle methods
    void onStateMounted(Device* PDN) override {
        initialize(PDN);  // Initialize and mount first state
    }

    void onStateLoop(Device* PDN) override {
        currentState->onStateLoop(PDN);    // Delegate to current state
        checkStateTransitions();           // Check for state changes
        if (stateChangeReady) {
            commitState(PDN);              // Execute transition
        }
    }

    void onStateDismounted(Device* PDN) override {
        currentState->onStateDismounted(PDN);
    }
};
```

**Key methods:**
- `populateStateMap()`: Pure virtual, must be implemented by derived classes
- `initialize(Device*)`: Mounts the first state in `stateMap`
- `skipToState(Device*, int)`: Jump to a specific state by index (useful for testing)
- `checkStateTransitions()`: Check if any transition condition is met
- `commitState(Device*)`: Execute state transition (dismount old, mount new)

**Example: MiniGame extends StateMachine**
```cpp
class MiniGame : public StateMachine {
public:
    MiniGame(int stateId, GameType gameType, const char* displayName) :
        StateMachine(stateId),
        gameType(gameType),
        displayName(displayName)
    { }

    // Each minigame implements populateStateMap()
    virtual void populateStateMap() = 0;
};

class GhostRunner : public MiniGame {
public:
    void populateStateMap() override {
        // Create and link all states
    }
};
```

### Timer Patterns

Use `SimpleTimer` for delays and timeouts:

```cpp
#include "utils/simple-timer.hpp"

class MyState : public State {
public:
    void onStateMounted(Device* PDN) override {
        myTimer.setDuration(2000);  // 2 second duration
    }

    void onStateLoop(Device* PDN) override {
        if (myTimer.isComplete()) {
            // Timer expired, do something
        }
    }

private:
    SimpleTimer myTimer;
};
```

**SimpleTimer API:**
- `setDuration(int ms)`: Set timer duration in milliseconds
- `isComplete()`: Returns true when timer has elapsed
- `reset()`: Restart the timer
- Timer starts automatically when `setDuration()` is called

**Important:** SimpleTimer uses the global platform clock. In tests, set the clock:
```cpp
SimpleTimer::setPlatformClock(device_.clockDriver);
```

---

## Header Organization

### Include Guards

Always use `#pragma once`:

```cpp
#pragma once

// Your code here
```

**DO NOT use** `#ifndef`/`#define`/`#endif` guards.

### Include Order

Organize includes in this order (with blank lines between groups):

```cpp
#pragma once

// 1. Standard library headers
#include <vector>
#include <string>
#include <memory>

// 2. Project headers (relative to include/)
#include "state/state-machine.hpp"
#include "device/device.hpp"
#include "game/minigame.hpp"

// 3. Forward declarations (if needed)
class GhostRunner;
```

**Include path convention:** Always use paths relative to `include/` directory
```cpp
#include "game/ghost-runner/ghost-runner.hpp"
#include "state/state-machine.hpp"
#include "utils/simple-timer.hpp"
```

---

## Test Patterns

### Test File Structure

CLI tests use a **split pattern**:

1. **Test logic** goes in `.hpp` headers
2. **TEST_F registrations** go in per-domain `.cpp` files

**Example: ghost-runner-tests.hpp**
```cpp
#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "game/ghost-runner/ghost-runner.hpp"

using namespace cli;

// Test suite fixture
class GhostRunnerTestSuite : public testing::Test {
public:
    void SetUp() override {
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createGameDevice(0, "ghost-runner");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        game_ = static_cast<GhostRunner*>(device_.game);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    DeviceInstance device_;
    GhostRunner* game_ = nullptr;
};

// Test function (not TEST_F macro)
void ghostRunnerIntroSeedsRng(GhostRunnerTestSuite* suite) {
    suite->game_->getSession().score = 999;
    suite->game_->skipToState(suite->device_.pdn, 0);

    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.score, 0);
}

#endif // NATIVE_BUILD
```

**Example: ghost-runner-reg-tests.cpp**
```cpp
#ifdef NATIVE_BUILD

#include "ghost-runner-tests.hpp"

// TEST_F registration (links test function to GTest)
TEST_F(GhostRunnerTestSuite, IntroSeedsRng) {
    ghostRunnerIntroSeedsRng(this);
}

#endif // NATIVE_BUILD
```

### Fixture Naming

**Pattern**: `<GameName>TestSuite` extending `testing::Test`

```cpp
class GhostRunnerTestSuite : public testing::Test { };
class SpikeVectorTestSuite : public testing::Test { };
class SignalEchoTestSuite : public testing::Test { };
```

### Test Function Naming

**Pattern**: `camelCase` starting with component/action being tested

```cpp
void ghostRunnerIntroSeedsRng(GhostRunnerTestSuite* suite);
void ghostRunnerCorrectPressInTargetZone(GhostRunnerTestSuite* suite);
void ghostRunnerEvaluateRoutesToWin(GhostRunnerTestSuite* suite);
```

**TEST_F naming**: PascalCase, descriptive
```cpp
TEST_F(GhostRunnerTestSuite, IntroSeedsRng);
TEST_F(GhostRunnerTestSuite, CorrectPressInTargetZone);
TEST_F(GhostRunnerTestSuite, EvaluateRoutesToWin);
```

### Setup and Teardown

**Always reset singletons** to prevent test pollution:

```cpp
void SetUp() override {
    // Reset all singleton state
    SerialCableBroker::resetInstance();
    MockHttpServer::resetInstance();
    SimpleTimer::resetClock();

    // Create device
    device_ = DeviceFactory::createGameDevice(0, "ghost-runner");

    // Set platform clock for timers
    SimpleTimer::setPlatformClock(device_.clockDriver);
}

void TearDown() override {
    // Destroy device
    DeviceFactory::destroyDevice(device_);

    // Clean up singleton state
    SerialCableBroker::resetInstance();
    MockHttpServer::resetInstance();
    SimpleTimer::resetClock();
}
```

### Singleton Cleanup

**Critical:** Always reset singletons in both `SetUp()` and `TearDown()`:

```cpp
SerialCableBroker::resetInstance();
MockHttpServer::resetInstance();
SimpleTimer::resetClock();
```

Failure to reset singletons causes state pollution between tests.

### Helper Methods

Common patterns for test fixtures:

```cpp
class MyTestSuite : public testing::Test {
public:
    // Advance simulation by N ticks
    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    // Advance simulation by N ticks with time delay
    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            device_.clockDriver->advance(delayMs);
            device_.pdn->loop();
        }
    }

    // Skip to a specific game state
    void advanceToState(int stateIndex) {
        game_->skipToState(device_.pdn, stateIndex);
        device_.pdn->loop();
    }
};
```

### Test Structure (AAA Pattern)

Follow Arrange-Act-Assert:

```cpp
void myTestFunction(MyTestSuite* suite) {
    // Arrange: Set up test conditions
    suite->game_->getConfig().ghostSpeedMs = 50;
    suite->game_->getConfig().rounds = 4;

    // Act: Execute the operation
    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tickWithTime(10, 15);

    // Assert: Verify the outcome
    auto& session = suite->game_->getSession();
    ASSERT_EQ(session.score, 100);
    ASSERT_EQ(session.strikes, 0);
}
```

### Test Registration Files

Each domain has its own registration `.cpp` file:

| Registration File | Domain |
|---|---|
| `cli-tests-main.cpp` | `main()` entry point (gtest/gmock) |
| `cli-core-tests.cpp` | Broker, HTTP, Serial, Peer, Button, Light, Display |
| `fdn-protocol-tests.cpp` | FDN Protocol, FDN Game, CLI FDN Device |
| `signal-echo-tests.cpp` | Signal Echo gameplay + difficulty |
| `fdn-integration-tests.cpp` | Integration, Complete, Progress, Challenge |
| `firewall-decrypt-tests.cpp` | Firewall Decrypt (all sections) |
| `progression-e2e-tests.cpp` | E2E, Re-encounter, Color Context |
| `ghost-runner-reg-tests.cpp` | Ghost Runner minigame |
| `spike-vector-reg-tests.cpp` | Spike Vector minigame |
| `cipher-path-reg-tests.cpp` | Cipher Path minigame |
| `exploit-seq-reg-tests.cpp` | Exploit Sequencer minigame |
| `breach-defense-reg-tests.cpp` | Breach Defense minigame |

**Rule:** Add TEST_F macros to YOUR game's registration file only. Do not edit other games' files.

---

## Game Structure

### MinigameConfig

Every minigame defines a config struct:

```cpp
struct GhostRunnerConfig {
    int ghostSpeedMs = 50;        // Gameplay parameter
    int screenWidth = 100;        // Display parameter
    int targetZoneStart = 40;     // Game rule
    int targetZoneEnd = 60;       // Game rule
    int rounds = 4;               // Victory condition
    int missesAllowed = 2;        // Defeat condition
    unsigned long rngSeed = 0;    // 0 = random, nonzero = deterministic
    bool managedMode = false;     // Standalone vs FDN-managed
};
```

**Preset factory functions:**
```cpp
inline GhostRunnerConfig makeGhostRunnerEasyConfig() {
    GhostRunnerConfig c;
    c.ghostSpeedMs = 50;
    c.targetZoneStart = 35;
    c.targetZoneEnd = 65;  // Wide zone (easy)
    c.rounds = 4;
    c.missesAllowed = 3;
    return c;
}

const GhostRunnerConfig GHOST_RUNNER_EASY = makeGhostRunnerEasyConfig();
```

**Key fields:**
- `rngSeed`: Set to `0` for random behavior (production), nonzero for deterministic tests
- `managedMode`: `false` for standalone play, `true` when launched by FDN system

### MinigameSession

Session struct tracks runtime game state:

```cpp
struct GhostRunnerSession {
    int ghostPosition = 0;
    int currentRound = 0;
    int strikes = 0;
    int score = 0;
    bool playerPressed = false;

    void reset() {
        ghostPosition = 0;
        currentRound = 0;
        strikes = 0;
        score = 0;
        playerPressed = false;
    }
};
```

**Session data is mutable** during gameplay and reset between games.

### MiniGameOutcome

Outcome struct defined in `game/minigame.hpp`:

```cpp
struct MiniGameOutcome {
    MiniGameResult result = MiniGameResult::IN_PROGRESS;
    int score = 0;
    bool hardMode = false;
    uint32_t startTimeMs = 0;

    bool isComplete() const {
        return result != MiniGameResult::IN_PROGRESS;
    }
};

enum class MiniGameResult : uint8_t {
    IN_PROGRESS = 0,
    WON = 1,
    LOST = 2,
};
```

**Set outcome in terminal states:**
```cpp
void GhostRunnerWin::onStateMounted(Device* PDN) override {
    MiniGameOutcome outcome;
    outcome.result = MiniGameResult::WON;
    outcome.score = session.score;
    outcome.hardMode = (config.missesAllowed < 2);
    game->setOutcome(outcome);
}
```

### Standard State Flow

Most minigames follow this flow:

```
Intro → Show → Gameplay → Evaluate → Win/Lose
         ↑                   ↓
         └───────────────────┘
         (loop for rounds)
```

**Intro**: Title screen, seed RNG, reset session
**Show**: Display round info (brief pause)
**Gameplay**: Main game loop, player interaction
**Evaluate**: Check round outcome, update score/strikes
**Win**: Victory screen, set outcome, return/loop
**Lose**: Defeat screen, set outcome, return/loop

### State IDs

Each game uses a unique ID range to avoid collisions:

```cpp
// Ghost Runner: 300-399
enum GhostRunnerStateId {
    GHOST_INTRO    = 300,
    GHOST_WIN      = 301,
    GHOST_LOSE     = 302,
    GHOST_SHOW     = 303,
    GHOST_GAMEPLAY = 304,
    GHOST_EVALUATE = 305,
};

// Spike Vector: 400-499
enum SpikeVectorStateId {
    SPIKE_INTRO    = 400,
    SPIKE_WIN      = 401,
    // ...
};
```

**Allocation:**
- Quickdraw main game: 0-99
- FDN system: 100-199
- Konami metagame: 200-299
- Ghost Runner: 300-399
- Spike Vector: 400-499
- Cipher Path: 500-599
- Exploit Sequencer: 600-699
- Breach Defense: 700-799

### Managed vs Standalone Mode

**Standalone mode** (`managedMode = false`):
- Terminal states (Win/Lose) loop back to Intro
- Self-contained gameplay loop

**Managed mode** (`managedMode = true`):
- Launched by FDN system
- Terminal states call `Device::returnToPreviousApp()`
- Outcome is read by FDN for progression tracking

```cpp
void GhostRunnerWin::onStateLoop(Device* PDN) override {
    if (winTimer.isComplete()) {
        if (game->getConfig().managedMode) {
            PDN->returnToPreviousApp();  // Return to FDN
        } else {
            transitionToIntroState = true;  // Loop to intro
        }
    }
}
```

### RNG Seeding

Seed RNG in intro state using `MiniGame::seedRng()`:

```cpp
void GhostRunnerIntro::onStateMounted(Device* PDN) override {
    game->seedRng(game->getConfig().rngSeed);
    game->resetGame();
    // ...
}
```

**Pattern:**
- If `rngSeed != 0`, use that seed (deterministic for tests)
- If `rngSeed == 0`, use default (random in production)

---

## PR Conventions

### Branch Naming

**Multi-VM workflow:**
```
wave{N}/{VM}-{description}
```
Examples:
```
wave13/07-api-conventions
wave12/03-state-transition-tests
```

**Legacy pattern:**
```
feature/description
fix/bug-description
```

### Commit Message Format

```
Brief description (#PR_NUMBER)
```

**Examples:**
```
PDN CLI Tool (#62)
Driver Update Bug Fixes (#61)
Wave 12: State transition edge case tests (#190)
Wave 13: Fix #169 — API conventions and agent reference docs
```

**Rules:**
- Brief, imperative mood ("Add feature" not "Added feature")
- Include PR number in parentheses
- NO co-authored-by lines
- Each commit = one feature + its tests (don't separate)

### PR Title Format

**Wave-based workflow:**
```
Wave {N}: {description} (#{issue})
```

**Standard workflow:**
```
Brief description (#{issue})
```

**Examples:**
```
Wave 13: Fix #169 — API conventions and agent reference docs
Wave 12: State transition edge case tests (#190)
Fix authentication timeout bug (#142)
```

### PR Body Template

```markdown
Fixes #{issue_number}.

{Brief description of changes}

## Changes
- {Bulleted list of key changes}
- {What was added/fixed/improved}

## Testing
- {What tests were added}
- {How to verify the changes}
```

**Example:**
```markdown
Fixes #169.

Adds API-CONVENTIONS.md documenting naming conventions, state machine patterns, test patterns, PR conventions, and game structure.

## Changes
- Created comprehensive API reference in docs/API-CONVENTIONS.md
- Documented all naming conventions (files, classes, methods, enums)
- Documented state machine lifecycle and transition patterns
- Documented test structure and fixture patterns
- Documented game structure and minigame patterns

## Testing
- Documentation-only change, no code changes
- Verified all markdown renders correctly
- Cross-referenced with actual codebase for accuracy
```

### CI Checks

All PRs must pass:

1. **Core Unit Tests** (`pio test -e native`)
2. **CLI Unit Tests** (`pio test -e native_cli_test`)
3. **ESP32-S3 Release Build** (`pio run -e esp32-s3_release`)
4. **Native CLI Build** (`pio run -e native_cli`)
5. **Duplicate File Check**
6. **Merge Conflict Marker Check**

### PR Review Flow

1. Create draft PR: `gh pr create --draft`
2. Wait for CI checks to pass
3. Mark ready for review: `gh pr ready`
4. Assign reviewer: `gh pr edit --add-assignee FinallyEve`
5. Add label: `gh pr edit --add-label review-requested`

**DO NOT use `--add-reviewer`** (author can't review own PR).

### GitHub API Rate Limits

Space out API calls. If you get HTTP 429:
- Wait 30+ seconds before retrying
- Don't fire 5+ calls in rapid succession
- Use single commands that do multiple things (`gh pr create --title --body --assignee`)

---

## Quick Reference Summary

**File naming:** `kebab-case.hpp`, `kebab-case.cpp`
**Class naming:** `PascalCase`
**Method naming:** `camelCase`
**Constants:** `SCREAMING_SNAKE_CASE`
**Enum values:** `SCREAMING_CASE`

**State lifecycle:**
1. `onStateMounted()` — setup
2. `onStateLoop()` — execute (1-N times)
3. `onStateDismounted()` — cleanup

**Test pattern:**
- Test logic in `.hpp` headers
- TEST_F registrations in per-domain `.cpp` files
- Always reset singletons in `SetUp()` and `TearDown()`

**Game structure:**
- Config struct (immutable settings)
- Session struct (mutable runtime state)
- Outcome struct (result tracking)
- Standard flow: Intro → Show → Gameplay → Evaluate → Win/Lose

**PR workflow:**
1. Create branch with wave naming
2. Write feature + tests together
3. Run all tests locally
4. Create draft PR
5. Wait for CI
6. Mark ready, assign reviewer, add label

---

*Last Updated: 2026-02-15*
