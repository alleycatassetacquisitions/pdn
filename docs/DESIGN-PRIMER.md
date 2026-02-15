# PDN Design Primer — Agent Reference Guide

> **Target Audience**: Claude agents working on the FinallyEve/pdn fork
> **Based On**: Upstream `alleycatassetacquisitions/pdn` analysis (PRs #81, #90, #80, #67, #62)
> **Authors Studied**: cyon1c (core architect), evelynn-walker, tire-fire
> **Last Updated**: 2026-02-15

## Table of Contents

1. [Architecture: The App-as-State Pattern](#1-architecture-the-app-as-state-pattern)
2. [File Organization](#2-file-organization)
3. [Naming Conventions](#3-naming-conventions)
4. [State Lifecycle](#4-state-lifecycle)
5. [Transition Mechanism](#5-transition-mechanism)
6. [Constructor Patterns](#6-constructor-patterns)
7. [Include Patterns](#7-include-patterns)
8. [Forbidden Patterns](#8-forbidden-patterns)
9. [FDN MiniGame Pattern](#9-fdn-minigame-pattern)
10. [Base State Templates (Future)](#10-base-state-templates-future)
11. [KonamiMetaGame Integration](#11-konamimetagame-integration)
12. [Progress Persistence](#12-progress-persistence)
13. [CLI Test Patterns](#13-cli-test-patterns)
14. [Upstream PR #90 — Forward-Looking](#14-upstream-pr-90--forward-looking)
15. [State ID Allocation Plan](#15-state-id-allocation-plan)
16. [Current Examples: Good vs Bad](#16-current-examples-good-vs-bad)

---

## 1. Architecture: The App-as-State Pattern

The PDN codebase uses **nestable state machines** where `StateMachine extends State`. This enables hierarchical composition:

```
Device → AppConfig[StateMachine] → StateMap[State]
```

### Key Concepts

- **Device** holds the app pool and orchestrates app switching
- **AppConfig** registers apps with their IDs
- **StateMachine** is both a state container AND a state itself
- **State** defines lifecycle methods and transition predicates

### Example from Our Code

```cpp
// include/game/konami-metagame/konami-metagame.hpp
constexpr int KONAMI_METAGAME_APP_ID = 9;

class KonamiMetaGame : public StateMachine {
public:
    explicit KonamiMetaGame(Player* player);
    void populateStateMap() override;
private:
    Player* player;
};
```

**Location**: `include/game/konami-metagame/konami-metagame.hpp:18-30`

---

## 2. File Organization

### Current Structure (FinallyEve Fork)

```
include/
├── state/           # Core state machine (state.hpp, state-machine.hpp, state-types.hpp)
├── device/          # Hardware abstraction (device.hpp, pdn.hpp, drivers/)
├── game/            # All game logic (Quickdraw + FDN minigames)
│   ├── quickdraw-states/    # Quickdraw state implementations
│   ├── signal-echo/         # Signal Echo minigame
│   ├── ghost-runner/        # Ghost Runner minigame
│   ├── spike-vector/        # Spike Vector minigame
│   ├── cipher-path/         # Cipher Path minigame
│   ├── exploit-sequencer/   # Exploit Sequencer minigame
│   ├── breach-defense/      # Breach Defense minigame
│   ├── firewall-decrypt/    # Firewall Decrypt minigame
│   ├── konami-metagame/     # Konami progression system
│   └── konami-states/       # Konami code entry states
└── apps/            # (Future: PR #90 alignment)
```

### Upstream Structure (alleycatacq/pdn, PR #90)

```
include/apps/
├── player-registration/
│   ├── player-registration.hpp
│   └── player-registration-states.hpp
├── handshake/
│   ├── handshake.hpp
│   └── handshake-states.hpp
└── quickdraw/
    ├── quickdraw.hpp
    └── quickdraw-states.hpp
```

**Rule**: One logical domain = one directory. States live with their app.

**Our Status**: We're ahead on features but diverged on structure. New apps should follow `include/apps/<name>/` for upstream alignment.

---

## 3. Naming Conventions

| Element | Convention | Example | File Location |
|---------|-----------|---------|---------------|
| Classes | PascalCase | `PlayerRegistration`, `MatchManager` | `include/game/quickdraw-states.hpp:55` |
| Enums (State IDs) | SHOUTY_SNAKE_CASE | `FDN_DETECTED`, `KONAMI_PUZZLE` | `include/game/quickdraw-states.hpp:21-52` |
| Member vars | camelCase | `transitionToUserFetchState`, `currentDigit` | `include/game/quickdraw-states.hpp:66-70` |
| Methods | camelCase | `onStateMounted()`, `transitionToCountdown()` | All state classes |
| Files | kebab-case | `ghost-runner.hpp`, `konami-metagame-states.hpp` | `include/game/` |
| Constants | constexpr + SCREAMING_SNAKE | `constexpr int GHOST_RUNNER_APP_ID = 4;` | `include/game/ghost-runner/ghost-runner.hpp:7` |
| App IDs | constexpr int | Allocated contiguously (see §15) | Per-app header |
| State IDs | enum values | Contiguous within app, 0-32 for Quickdraw, 30+ for FDN | `include/game/quickdraw-states.hpp:20-52` |

### DO/DON'T Examples

| ✅ DO | ❌ DON'T |
|------|---------|
| `constexpr int SIGNAL_ECHO_APP_ID = 2;` | `#define APP_ID 2` |
| `bool transitionToNextState = false;` | `bool _transitionToNextState;` (no underscore prefix) |
| `enum QuickdrawStateId { IDLE = 8 }` | `const int IDLE = 8;` (use enum for state IDs) |
| `class GhostRunner : public MiniGame` | `class ghost_runner` (PascalCase for classes) |

---

## 4. State Lifecycle

**MANDATORY**: All states must implement these three methods:

```cpp
void onStateMounted(Device *PDN) override;    // Setup: timers, callbacks, register serials
void onStateLoop(Device *PDN) override;       // Execute: check transitions, update state, render
void onStateDismounted(Device *PDN) override; // Teardown: invalidate timers, reset hardware
```

### Example: PlayerRegistration State

**Location**: `include/game/quickdraw-states.hpp:55-74`

```cpp
class PlayerRegistration : public State {
public:
    PlayerRegistration(Player* player, MatchManager* matchManager);
    ~PlayerRegistration();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToUserFetch();

private:
    bool transitionToUserFetchState = false;
    bool shouldRender = false;
    Player* player;
    MatchManager* matchManager;
    int currentDigit = 0;
    int currentDigitIndex = 0;
    static constexpr int DIGIT_COUNT = 4;
    int inputId[DIGIT_COUNT] = {0, 0, 0, 0};
};
```

### Lifecycle Rules

1. **onStateMounted**: Called once when entering state
   - Register button callbacks
   - Start timers
   - Initialize display
   - Set LED states
   - Register serial message handlers

2. **onStateLoop**: Called repeatedly (every Device::loop())
   - Check transition predicates
   - Update game logic
   - Render UI
   - Process timers

3. **onStateDismounted**: Called once when leaving state
   - Invalidate timers
   - Unregister callbacks
   - Clear display
   - Reset hardware state

---

## 5. Transition Mechanism

### Pattern

```cpp
// Transition predicate pattern (in State subclass):
bool transitionToNextState = false;  // Private flag

bool transitionToB() { return transitionToNextState; }  // Public predicate

// In populateStateMap() (in StateMachine subclass):
stateA->addTransition(new StateTransition(
    std::bind(&StateA::transitionToB, stateA), stateB
));
```

### Example from Our Code

**Location**: `include/game/quickdraw-states.hpp:76-100`

```cpp
class FetchUserDataState : public State {
public:
    FetchUserDataState(Player* player, WirelessManager* wirelessManager, ...);

    bool transitionToConfirmOffline();
    bool transitionToWelcomeMessage();
    bool transitionToUploadMatches();
    bool transitionToPlayerRegistration();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

private:
    bool transitionToPlayerRegistrationState = false;
    bool transitionToConfirmOfflineState = false;
    bool transitionToWelcomeMessageState = false;
    bool transitionToUploadMatchesState = false;
    // ... other members
};
```

**Key Rule**: Transition predicates are `bool` methods named `transitionToXYZ()`. They're bound via `std::bind` in `populateStateMap()`.

---

## 6. Constructor Patterns

### States Take Dependencies as Pointers

```cpp
// ✅ GOOD: States take dependencies as pointers
class MyState : public State {
public:
    explicit MyState(Player* player, MatchManager* matchManager)
        : State(MY_STATE_ID), player(player), matchManager(matchManager) {}
private:
    Player* player;
    MatchManager* matchManager;
};
```

### Device is ALWAYS a Parameter, Never Stored

```cpp
// ✅ GOOD: Device passed as parameter
void onStateMounted(Device *PDN) override {
    PDN->getDisplay()->clearDisplay();
}

// ❌ BAD: Don't store as member
class MyState : public State {
private:
    Device *PDN;  // NEVER DO THIS
};
```

### Example from Our Code

**Location**: `include/game/ghost-runner/ghost-runner.hpp` (inferred from test suite)

```cpp
// Test suite shows correct pattern
class GhostRunnerTestSuite : public testing::Test {
    void SetUp() override {
        device_ = DeviceFactory::createGameDevice(0, "ghost-runner");
        game_ = static_cast<GhostRunner*>(device_.game);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();  // Device passed to loop, not stored
        }
    }

    DeviceInstance device_;
    GhostRunner* game_ = nullptr;
};
```

**Location**: `test/test_cli/ghost-runner-tests.hpp:22-59`

---

## 7. Include Patterns

```cpp
#pragma once  // Always use pragma once, not #ifndef guards

// Order: project → utility → standard library
#include "state/state-machine.hpp"
#include "apps/my-app/my-app-states.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include <vector>
#include <string>
#include <memory>
```

### Example from Our Code

**Location**: `include/game/minigame.hpp:1-7`

```cpp
#pragma once

#include <cstdint>
#include <cstdlib>
#include "state/state-machine.hpp"
#include "device/device-types.hpp"
```

---

## 8. Forbidden Patterns

| ❌ Don't | ✅ Do Instead | Reason |
|---------|--------------|--------|
| Store `Device *PDN` as member | Pass as parameter to lifecycle methods | Device lifetime != State lifetime |
| Call `initialize()` in constructor | Separate method, called via `onStateMounted()` | Hardware not ready during construction |
| Use `sleep()` or blocking loops | Use `SimpleTimer` with state machine integration | Blocks entire device loop |
| Use global/static state | Use `Snapshot` pattern for persistence | Breaks testability, can't parallelize |
| Expose internal states | Expose high-level predicates (PR #90 pattern) | Encapsulation, upstream alignment |
| Use `#ifndef` header guards | Use `#pragma once` | Simpler, faster compilation |
| Use exceptions in embedded code | Return bool predicates, use defensive nullchecks | Exceptions disabled on ESP32 |
| Mix config and session state | Separate structs (see §9) | Replayability without reconstruction |

---

## 9. FDN MiniGame Pattern

All FDN minigames extend `MiniGame`, which provides:

- Game identity (`GameType`, display name)
- Outcome tracking (`MiniGameOutcome`)
- Configure/reset pattern for replayability
- RNG seeding for deterministic tests

### MiniGame Base Class

**Location**: `include/game/minigame.hpp:35-84`

```cpp
class MiniGame : public StateMachine {
public:
    MiniGame(int stateId, GameType gameType, const char* displayName) :
      StateMachine(stateId),
      gameType(gameType),
      displayName(displayName)
    {
    }

    virtual ~MiniGame() = default;

    GameType getGameType() const { return gameType; }
    const char* getDisplayName() const { return displayName; }

    const MiniGameOutcome& getOutcome() const { return outcome; }
    bool isGameComplete() const { return outcome.isComplete(); }

    void setOutcome(const MiniGameOutcome& newOutcome) {
        outcome = newOutcome;
    }

    virtual void resetGame() {
        outcome = MiniGameOutcome{};
        // Note: startTimeMs will be set by intro states via setStartTime()
    }

    void setStartTime(uint32_t timeMs) {
        outcome.startTimeMs = timeMs;
    }

    void seedRng(unsigned long rngSeed) {
        if (rngSeed != 0) {
            srand(static_cast<unsigned int>(rngSeed));
        } else {
            srand(static_cast<unsigned int>(0));
        }
    }

protected:
    MiniGameOutcome outcome;

private:
    GameType gameType;
    const char* displayName;
};
```

### Config vs Session Pattern

**Location**: `include/game/signal-echo/signal-echo.hpp:13-78`

```cpp
// Config: Tuning variables for difficulty (immutable during game)
struct SignalEchoConfig {
    int sequenceLength = 4;
    int numSequences = 3;
    int displaySpeedMs = 600;
    int timeLimitMs = 0;
    bool cumulative = false;
    int allowedMistakes = 2;
    unsigned long rngSeed = 0;
    bool managedMode = false;
};

// Session: Current game state (reset between games)
struct SignalEchoSession {
    std::vector<bool> currentSequence;
    int currentRound = 0;
    int inputIndex = 0;
    int mistakes = 0;
    int score = 0;

    void reset() {
        currentSequence.clear();
        currentRound = 0;
        inputIndex = 0;
        mistakes = 0;
        score = 0;
    }
};

class SignalEcho : public MiniGame {
public:
    explicit SignalEcho(SignalEchoConfig config);
    void populateStateMap() override;
    void resetGame() override;

    SignalEchoConfig& getConfig() { return config; }
    SignalEchoSession& getSession() { return session; }

private:
    SignalEchoConfig config;
    SignalEchoSession session;
};
```

### Standard MiniGame State Flow

```
Intro → Show → Gameplay → Evaluate → Win/Lose
```

1. **Intro**: Seeds RNG, shows title, transitions to Show
2. **Show**: Displays round info, transitions to Gameplay
3. **Gameplay**: Main game loop, sets outcome on completion
4. **Evaluate**: Checks outcome, transitions to Win or Lose
5. **Win/Lose**: Terminal states, awards buttons/boons, returns to KMG or loops

---

## 10. Base State Templates (Future)

**Status**: Planned refactoring (see `docs/issues/issue-refactor-base-states.md`)

**Problem**: ~1,400 lines of duplicated boilerplate across 7 FDN minigames

**Solution**: Extract templates for common state patterns:

- `BaseIntroState` — Title display, RNG seeding, transition to Show
- `BaseShowState` — Round info display, countdown, transition to Gameplay
- `BaseWinState` — Victory display, LED animation, score display
- `BaseLoseState` — Defeat display, LED animation, retry prompt
- `BaseEvaluateState` — Outcome checking, transition to Win/Lose

### Current Duplication Example

**All 7 games have nearly identical Win states** (~50 lines each):

```cpp
// Ghost Runner Win State (simplified)
void GhostRunnerWin::onStateMounted(Device *PDN) {
    PDN->getDisplay()->clearDisplay();
    PDN->getDisplay()->setFont(FONT_LARGE);
    PDN->getDisplay()->drawStr(32, 20, "VICTORY!");
    PDN->getDisplay()->drawStr(20, 40, game->getConfig().hardMode ? "HARD" : "EASY");
    // ... 40 more lines of LED, score, button registration
}
```

**This pattern repeats in**:
- `include/game/ghost-runner/ghost-runner-states.hpp`
- `include/game/signal-echo/signal-echo-states.hpp`
- `include/game/spike-vector/spike-vector-states.hpp`
- `include/game/cipher-path/cipher-path-states.hpp`
- `include/game/exploit-sequencer/exploit-sequencer-states.hpp`
- `include/game/breach-defense/breach-defense-states.hpp`
- `include/game/firewall-decrypt/firewall-decrypt-states.hpp`

**Future Pattern** (after refactoring):

```cpp
// Base template (to be created)
template <typename TGame>
class BaseWinState : public State {
protected:
    virtual const char* victoryText() = 0;
    virtual bool isHardMode() = 0;
    // ... common logic in base
};

// Game-specific override (10 lines instead of 50)
class GhostRunnerWin : public BaseWinState<GhostRunner> {
protected:
    const char* victoryText() override { return "GHOST ESCAPED!"; }
    bool isHardMode() override { return game->getConfig().hardMode; }
};
```

---

## 11. KonamiMetaGame Integration

The **KonamiMetaGame** is a meta-layer StateMachine that manages FDN minigame progression.

### Flow

1. Player encounters FDN → triggers app switch from Quickdraw
2. First encounter: Easy mode → Win → Button awarded → konamiProgress updated
3. Button replay: No new rewards
4. All 7 buttons → Konami code entry → 13 inputs → hard mode unlocked
5. Hard mode → Win → Boon awarded → colorProfileEligibility updated
6. Mastery replay → Mode select → Launch game with correct difficulty

### KonamiMetaGame Registration

**Location**: `include/game/konami-metagame/konami-metagame.hpp:18-30`

```cpp
constexpr int KONAMI_METAGAME_APP_ID = 9;

class KonamiMetaGame : public StateMachine {
public:
    explicit KonamiMetaGame(Player* player);
    ~KonamiMetaGame() override = default;

    void populateStateMap() override;

private:
    Player* player;
};
```

### State Flow Example

```
FdnDetectedState (Quickdraw)
    ↓ (app switch)
KonamiMetaGame
    ↓ (first encounter)
SignalEcho (easy mode)
    ↓ (win)
KonamiButtonAwarded
    ↓ (app switch back)
FdnCompleteState (Quickdraw)
```

---

## 12. Progress Persistence

Progress is persisted to NVS (Non-Volatile Storage) and synced via HTTP.

### ProgressManager Pattern

**Location**: `include/game/progress-manager.hpp:21-323`

```cpp
class ProgressManager {
public:
    void initialize(Player* player, StorageInterface* storage);

    void saveProgress();     // Write to NVS
    void loadProgress();     // Read from NVS
    void clearProgress();    // Wipe NVS

    void syncProgress(HttpClientInterface* httpClient);  // Upload to server
    void downloadAndMergeProgress(const std::string& jsonResponse);  // Merge server data

    bool isSynced() const;
    void markUnsynced();
};
```

### NVS Keys

| Key | Type | Description |
|-----|------|-------------|
| `konami` | uint8_t | Bitmask of unlocked Konami buttons (7 bits) |
| `konami_boon` | uint8_t | Konami puzzle complete (0/1) |
| `color_profile` | uint8_t | Equipped color profile (gameType + 1, 0 = none) |
| `color_elig` | uint8_t | Bitmask of eligible color profiles (7 bits) |
| `easy_att_{i}` | uint8_t | Easy mode attempt count for game i (0-6) |
| `hard_att_{i}` | uint8_t | Hard mode attempt count for game i (0-6) |
| `synced` | uint8_t | Progress uploaded to server (0/1) |

### Conflict Resolution (Server Merge)

**Location**: `include/game/progress-manager.hpp:215-317`

- **Bitmask fields** (konami, colorEligibility): Union merge (bitwise OR)
- **Attempt counts**: Max-wins (keep higher value for each game/difficulty)
- **Backward compatibility**: If fields missing, don't overwrite local data

---

## 13. CLI Test Patterns

CLI tests use a **split pattern** — test logic lives in `.hpp` headers, `TEST_F` registrations live in per-domain `.cpp` files.

### Test File Structure

```
test/test_cli/
├── ghost-runner-tests.hpp          # Test functions
├── ghost-runner-reg-tests.cpp      # TEST_F registrations
├── signal-echo-tests.hpp           # Test functions
├── signal-echo-reg-tests.cpp       # TEST_F registrations (not created yet)
├── ...
└── cli-tests-main.cpp              # main() entry point
```

### Registration Files by Domain

| Registration File | Domain |
|---|---|
| `cli-tests-main.cpp` | `main()` entry point (gtest/gmock) |
| `cli-core-tests.cpp` | Broker, HTTP, Serial, Peer, Button, Light, Display, Command, Role, Reboot |
| `fdn-protocol-tests.cpp` | FDN Protocol, FDN Game, CLI FDN Device, Device Extensions |
| `fdn-integration-tests.cpp` | Integration, Complete, Progress, Challenge, Switching, Color Profile |
| `progression-e2e-tests.cpp` | E2E, Re-encounter, Color Context, Bounty Role |
| `ghost-runner-reg-tests.cpp` | Ghost Runner minigame |
| `spike-vector-reg-tests.cpp` | Spike Vector minigame |
| `cipher-path-reg-tests.cpp` | Cipher Path minigame |
| `exploit-seq-reg-tests.cpp` | Exploit Sequencer minigame |
| `breach-defense-reg-tests.cpp` | Breach Defense minigame |

### Test Suite Pattern

**Location**: `test/test_cli/ghost-runner-tests.hpp:22-59`

```cpp
class GhostRunnerTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createGameDevice(0, "ghost-runner");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        game_ = static_cast<GhostRunner*>(device_.game);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            device_.clockDriver->advance(delayMs);
            device_.pdn->loop();
        }
    }

    DeviceInstance device_;
    GhostRunner* game_ = nullptr;
};
```

### Test Function Pattern

**Location**: `test/test_cli/ghost-runner-tests.hpp` (function body)

```cpp
// In ghost-runner-tests.hpp — the test function:
void ghostRunnerEasyConfigPresets(GhostRunnerTestSuite* suite) {
    auto& config = suite->game_->getConfig();
    EXPECT_EQ(config.roundCount, 3);
    EXPECT_EQ(config.targetZoneMs, 150);
    // ... assertions
}
```

### Test Registration Pattern

**Location**: `test/test_cli/ghost-runner-reg-tests.cpp:13-15`

```cpp
// In ghost-runner-reg-tests.cpp — the TEST_F registration:
TEST_F(GhostRunnerTestSuite, EasyConfigPresets) {
    ghostRunnerEasyConfigPresets(this);
}
```

### Adding a New Test

1. **Add test function to `.hpp` header**:
   ```cpp
   void myNewTestFunction(GhostRunnerTestSuite* suite) {
       // test logic here
   }
   ```

2. **Add TEST_F registration to corresponding `.cpp` file**:
   ```cpp
   TEST_F(GhostRunnerTestSuite, MyNewTest) {
       myNewTestFunction(this);
   }
   ```

**Important**: Add `TEST_F` macros to YOUR game's registration file only. Do not edit other games' files.

---

## 14. Upstream PR #90 — Forward-Looking

**PR #90 Context**: Upstream is formalizing the sub-app pattern to improve encapsulation.

### Current Problem

Parent apps check internal state directly:

```cpp
// ❌ BAD: Parent checks internal state directly
if (handshake->getCurrentState()->getStateId() == CONNECTION_SUCCESSFUL) {
    // ...
}
```

### PR #90 Solution

Apps expose **high-level predicates**, not internal state:

```cpp
// ✅ GOOD: App exposes a predicate
bool HandshakeApp::readyForCountdown() {
    return (currentState->getStateId() == CONNECTION_SUCCESSFUL
            && static_cast<ConnectionSuccessful*>(currentState)->transitionToCountdown());
}

// Parent calls predicate
if (handshake->readyForCountdown()) {
    // ...
}
```

### Snapshot Pattern for State Persistence

**PR #90 introduces** `onStatePaused()` and `onStateResumed()`:

```cpp
std::unique_ptr<Snapshot> onStatePaused(Device *PDN);   // Save state data
void onStateResumed(Device *PDN, Snapshot* snapshot);    // Restore state data
```

This enables:
- Pausing sub-apps without losing state
- App switching with resume capability
- Testable state serialization

### Our Status

- **KonamiMetaGame** follows this pattern (app switching, sub-app management)
- **FDN minigames** partially follow (managedMode for terminal state behavior)
- **Need**: Expose predicates from KMG to Quickdraw (e.g., `bool KMG::gameComplete()`)

---

## 15. State ID Allocation Plan

### Current Allocation

| App | App ID | State ID Range | Notes |
|-----|--------|----------------|-------|
| Quickdraw | 0 | 0-32 | Legacy, non-contiguous |
| Signal Echo | 2 | 100-105 | FDN minigame |
| Firewall Decrypt | 3 | 200-209 | FDN minigame |
| Ghost Runner | 4 | 300-305 | FDN minigame |
| Spike Vector | 5 | 400-405 | FDN minigame |
| Cipher Path | 6 | 500-505 | FDN minigame |
| Exploit Sequencer | 7 | 600-605 | FDN minigame |
| Breach Defense | 8 | 700-705 | FDN minigame |
| KonamiMetaGame | 9 | 900-915 | Progression system |

**Pattern**: Each app gets 100-range block (e.g., Ghost Runner = 300-399)

### Upstream Pattern (PR #90)

Upstream uses **contiguous allocation within each app**:

```cpp
enum PlayerRegistrationStateId {
    PLAYER_REGISTRATION_IDLE = 0,
    PLAYER_REGISTRATION_INPUT = 1,
    PLAYER_REGISTRATION_CONFIRM = 2
};

enum HandshakeStateId {
    HANDSHAKE_IDLE = 0,
    HANDSHAKE_INITIATE = 1,
    HANDSHAKE_SUCCESS = 2
};
```

**Our Divergence**: We allocated non-overlapping ranges to avoid collisions during development. Upstream can use overlapping ranges because states are scoped to their app.

### Alignment Strategy

**Option 1**: Keep current allocation (no collision risk, easier debugging)
**Option 2**: Migrate to contiguous (upstream alignment, but requires careful state ID refactoring)

**Recommendation**: Keep current allocation. It's not hurting anything, and refactoring state IDs across 9 apps + 180 CLI tests is high-risk for low reward.

---

## 16. Current Examples: Good vs Bad

### ✅ GOOD: MiniGame Constructor Pattern

**Location**: `include/game/ghost-runner/ghost-runner.hpp` (inferred)

```cpp
class GhostRunner : public MiniGame {
public:
    explicit GhostRunner(GhostRunnerConfig config) :
        MiniGame(GHOST_RUNNER_APP_ID, GameType::GHOST_RUNNER, "GHOST RUNNER"),
        config(config)
    {
    }

    void populateStateMap() override;
    void resetGame() override;

private:
    GhostRunnerConfig config;
    GhostRunnerSession session;
};
```

**Why Good**:
- Calls `MiniGame` constructor with identity
- Takes config struct (not Device pointer)
- Separates config from session state

---

### ✅ GOOD: Transition Predicate Pattern

**Location**: `include/game/quickdraw-states.hpp:76-100`

```cpp
class FetchUserDataState : public State {
public:
    bool transitionToConfirmOffline();
    bool transitionToWelcomeMessage();
    bool transitionToUploadMatches();
    bool transitionToPlayerRegistration();

private:
    bool transitionToPlayerRegistrationState = false;
    bool transitionToConfirmOfflineState = false;
    bool transitionToWelcomeMessageState = false;
    bool transitionToUploadMatchesState = false;
};
```

**Why Good**:
- Private flags + public predicates
- Named `transitionToXYZ()`
- Multiple transitions from one state (branching logic)

---

### ✅ GOOD: Progress Persistence Pattern

**Location**: `include/game/progress-manager.hpp:31-62`

```cpp
void saveProgress() {
    if (!storage || !player) return;
    storage->writeUChar("konami", player->getKonamiProgress());
    storage->writeUChar("konami_boon", player->hasKonamiBoon() ? 1 : 0);
    // ... save all fields
    storage->writeUChar("synced", 0);
    synced = false;
}
```

**Why Good**:
- Defensive nullchecks
- Atomic save (all fields updated together)
- Marks unsynced after save
- NVS keys are string constants (not magic numbers)

---

### ❌ BAD: Storing Device Pointer (Hypothetical)

```cpp
// DON'T DO THIS
class MyState : public State {
public:
    void onStateMounted(Device *PDN) override {
        this->device = PDN;  // ❌ BAD
    }

private:
    Device* device;  // ❌ BAD
};
```

**Why Bad**:
- Device lifetime != State lifetime
- Creates dangling pointer risk
- Breaks testability (can't mock Device easily)

**Fix**: Pass `Device *PDN` as parameter to all lifecycle methods.

---

### ❌ BAD: Blocking Sleep in State (Hypothetical)

```cpp
// DON'T DO THIS
void MyState::onStateLoop(Device *PDN) {
    PDN->getDisplay()->drawStr(0, 0, "Waiting...");
    delay(5000);  // ❌ BAD: Blocks entire device loop
    transitionFlag = true;
}
```

**Why Bad**:
- Blocks entire device (no button input, no wireless, no other states)
- Misses state transitions
- Can't test without waiting 5 seconds

**Fix**: Use `SimpleTimer`:

```cpp
void MyState::onStateMounted(Device *PDN) {
    waitTimer.start(5000);
}

void MyState::onStateLoop(Device *PDN) {
    if (waitTimer.hasFired()) {
        transitionFlag = true;
    }
}
```

---

### ❌ BAD: Exposing Internal State (Pre-PR #90)

```cpp
// DON'T DO THIS (old pattern)
if (handshake->getCurrentState()->getStateId() == CONNECTION_SUCCESSFUL) {
    // Parent knows too much about handshake internals
}
```

**Why Bad**:
- Breaks encapsulation
- Tight coupling (parent depends on handshake state IDs)
- Can't refactor handshake without breaking parent

**Fix** (PR #90 pattern):

```cpp
// Handshake exposes predicate
bool HandshakeApp::readyForCountdown() {
    return (currentState->getStateId() == CONNECTION_SUCCESSFUL
            && static_cast<ConnectionSuccessful*>(currentState)->transitionToCountdown());
}

// Parent calls predicate
if (handshake->readyForCountdown()) {
    // ...
}
```

---

## Quick Reference Checklist

When creating a new state:

- [ ] Extends `State` (or `MiniGame` for games)
- [ ] Implements `onStateMounted()`, `onStateLoop()`, `onStateDismounted()`
- [ ] Transition predicates are `bool transitionToXYZ()` methods
- [ ] Constructor takes dependencies as pointers, NOT Device pointer
- [ ] Uses `SimpleTimer` instead of blocking delays
- [ ] Invalidates timers in `onStateDismounted()`
- [ ] Uses `#pragma once` for header guard
- [ ] PascalCase class name, camelCase methods, SHOUTY_SNAKE state IDs
- [ ] File named `kebab-case.hpp`

When creating a new app:

- [ ] Extends `StateMachine`
- [ ] Defines `constexpr int MY_APP_ID = N;`
- [ ] Implements `populateStateMap()`
- [ ] States live in same directory as app (`include/apps/<name>/` for new apps)
- [ ] Exposes high-level predicates, not internal state (PR #90 pattern)
- [ ] Uses `Snapshot` pattern if pausable/resumable

When writing CLI tests:

- [ ] Test functions in `.hpp` header
- [ ] `TEST_F` registrations in per-domain `.cpp` file
- [ ] SetUp resets singletons (SerialCableBroker, MockHttpServer, SimpleTimer)
- [ ] TearDown destroys device and resets singletons
- [ ] Uses `tick()` helper, not raw `loop()` calls
- [ ] Each test is independent (no shared state between tests)

---

**End of Design Primer**
