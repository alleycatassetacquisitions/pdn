# PDN API Reference

Complete API documentation for the Portable Data Node embedded game system.

## Table of Contents

- [Core Classes](#core-classes)
  - [PDN (Device)](#pdn-device)
  - [Player](#player)
  - [StateMachine](#statemachine)
  - [State](#state)
- [Game Systems](#game-systems)
  - [Quickdraw](#quickdraw)
  - [FdnGame](#fdngame)
  - [MiniGame](#minigame)
  - [ProgressManager](#progressmanager)
  - [MatchManager](#matchmanager)
- [Minigame Classes](#minigame-classes)
  - [SignalEcho](#signalecho)
  - [GhostRunner](#ghostrunner)
  - [SpikeVector](#spikevector)
  - [FirewallDecrypt](#firewalldecrypt)
  - [CipherPath](#cipherpath)
  - [ExploitSequencer](#exploitsequencer)
  - [BreachDefense](#breachdefense)
- [Utility Classes](#utility-classes)
  - [LightManager](#lightmanager)
- [CLI Commands](#cli-commands)

---

## Core Classes

### PDN (Device)

The main device abstraction layer providing hardware access.

**Location:** `include/device/pdn.hpp`

#### Methods

```cpp
static PDN* createPDN(DriverConfig& driverConfig);
```
Factory method to create a PDN instance with platform-specific drivers.

**Parameters:**
- `driverConfig`: Configuration specifying which driver implementations to use (ESP32-S3 or Native)

**Returns:** Pointer to initialized PDN instance

---

```cpp
int begin();
```
Initialize all hardware drivers and prepare the device for operation.

**Returns:** 0 on success, error code otherwise

---

```cpp
void loop();
```
Main device loop. Must be called repeatedly from the Arduino `loop()` function.

---

```cpp
Display* getDisplay();
Haptics* getHaptics();
Button* getPrimaryButton();
Button* getSecondaryButton();
LightManager* getLightManager();
HttpClientInterface* getHttpClient();
PeerCommsInterface* getPeerComms();
StorageInterface* getStorage();
WirelessManager* getWirelessManager();
```
Hardware accessor methods returning driver interfaces.

**Example:**
```cpp
PDN* device = PDN::createPDN(config);
device->begin();
device->getDisplay()->drawText(0, 0, "Hello");
device->loop();
```

---

### Player

Represents a player with stats, progression, and state.

**Location:** `include/game/player.hpp`

#### Enums

```cpp
enum class Allegiance {
    ALLEYCAT = 0,
    ENDLINE = 1,
    HELIX = 2,
    RESISTANCE = 3
};
```

#### Constructor

```cpp
Player(const std::string& id, Allegiance allegiance, bool isHunter);
```

**Parameters:**
- `id`: Unique player identifier
- `allegiance`: Faction affiliation
- `isHunter`: Role in Quickdraw duels

#### Core Methods

```cpp
std::string getUserID() const;
void setUserID(char* newId);

Allegiance getAllegiance() const;
void setAllegiance(Allegiance allegiance);

bool isHunter() const;
void setIsHunter(bool isHunter);
void toggleHunter();
```

#### Match Statistics

```cpp
int getMatchesPlayed();
int getWins();
int getLosses();
int getStreak();

void incrementMatchesPlayed();
void incrementWins();
void incrementLosses();
void incrementStreak();
void resetStreak();

unsigned long getLastReactionTime();
unsigned long getAverageReactionTime();
void addReactionTime(unsigned long reactionTime);
```

#### Konami Progression

The Konami system tracks which of 7 minigame buttons the player has unlocked.

```cpp
uint8_t getKonamiProgress() const;
void setKonamiProgress(uint8_t progress);

void unlockKonamiButton(uint8_t buttonIndex);  // buttonIndex: 0-6
bool hasUnlockedButton(uint8_t buttonIndex) const;
bool hasAllKonamiButtons() const;
bool isKonamiComplete() const;

bool hasKonamiBoon() const;
void setKonamiBoon(bool boon);
```

**Example:**
```cpp
player->unlockKonamiButton(0);  // Unlock Signal Echo (UP button)
if (player->hasAllKonamiButtons()) {
    player->setKonamiBoon(true);  // Auto-boon when all 7 collected
}
```

#### Color Profile System

Players earn color profiles by beating minigames on hard mode.

```cpp
int getEquippedColorProfile() const;  // -1 = default, 0-6 = GameType
void setEquippedColorProfile(int gameTypeValue);

void addColorProfileEligibility(int gameTypeValue);
bool hasColorProfileEligibility(int gameTypeValue) const;
const std::set<int>& getColorProfileEligibility() const;
```

#### Per-Game Attempt Tracking

```cpp
void incrementEasyAttempts(GameType gameType);
void incrementHardAttempts(GameType gameType);

uint8_t getEasyAttempts(GameType gameType) const;
uint8_t getHardAttempts(GameType gameType) const;
```

**Example:**
```cpp
player->incrementHardAttempts(GameType::GHOST_RUNNER);
if (wonHardMode) {
    player->addColorProfileEligibility(static_cast<int>(GameType::GHOST_RUNNER));
}
```

---

### StateMachine

Base class for game state machines. Manages state transitions and lifecycle.

**Location:** `include/state/state-machine.hpp`

#### Lifecycle Methods

```cpp
void initialize(Device* PDN);
```
Populates state map and mounts the first state.

---

```cpp
void onStateLoop(Device* PDN) override;
```
Executes current state's loop, checks transitions, commits state changes.

---

```cpp
bool skipToState(Device* PDN, int stateIndex);
```
Jump directly to a specific state by index (useful for testing).

**Returns:** true if successful, false if index out of range

#### State Management

```cpp
State* getCurrentState();
```
Returns pointer to currently active state.

---

```cpp
virtual void populateStateMap() = 0;
```
Pure virtual method. Derived classes must implement to populate `stateMap` with states.

**Example:**
```cpp
void MyGame::populateStateMap() {
    stateMap.push_back(new IntroState(this));
    stateMap.push_back(new GameplayState(this));
    stateMap.push_back(new ResultState(this));
}
```

---

### State

Base class for individual states within a state machine.

**Location:** `include/state/state.hpp`

#### Lifecycle Methods

Every state must implement these three methods:

```cpp
virtual void onStateMounted(Device* PDN);
```
Called once when entering this state. Initialize resources here.

---

```cpp
virtual void onStateLoop(Device* PDN);
```
Called every frame while in this state. Main logic goes here.

---

```cpp
virtual void onStateDismounted(Device* PDN);
```
Called once when leaving this state. Clean up resources here.

#### Transition Management

```cpp
void addTransition(StateTransition transition);
State* checkTransitions();
```

**Example:**
```cpp
class GameplayState : public State {
    void onStateMounted(Device* PDN) override {
        PDN->getDisplay()->drawText(0, 0, "Game Started!");
        timer.start();
    }

    void onStateLoop(Device* PDN) override {
        if (PDN->getPrimaryButton()->wasPressed()) {
            score++;
        }
    }

    void onStateDismounted(Device* PDN) override {
        timer.stop();
        PDN->getDisplay()->clear();
    }
};
```

---

## Game Systems

### Quickdraw

Main game state machine for the Quickdraw dueling system.

**Location:** `include/game/quickdraw.hpp`

**App ID:** `QUICKDRAW_APP_ID = 1`

#### Constructor

```cpp
Quickdraw(Player* player, Device* PDN,
          QuickdrawWirelessManager* quickdrawWirelessManager,
          RemoteDebugManager* remoteDebugManager);
```

#### Methods

```cpp
void populateStateMap() override;
```
Initializes all 27+ Quickdraw states (registration, idle, handshake, duel, FDN, Konami, etc.)

---

```cpp
ProgressManager* getProgressManager() const;
```
Returns the progress manager for NVS persistence and server sync.

---

```cpp
static Image getImageForAllegiance(Allegiance allegiance, ImageType whichImage);
```
Utility method to get faction-specific imagery.

#### State Flow

The Quickdraw state machine includes:
1. **PlayerRegistration** (0) - Initial setup
2. **FetchUserData** (1) - Server sync
3. **ConfirmOffline** (2) - Offline mode prompt
4. **ChooseRole** (3) - Hunter/Bounty selection
5. **AllegiancePicker** (4) - Faction selection
6. **WelcomeMessage** (5) - Intro screen
7. **Sleep** (6) - Low power mode
8. **AwakenSequence** (7) - Wake up animation
9. **Idle** (8) - Main menu
10. **HandshakeInitiate** (9) - Connection setup
11-17. **Duel states** - Handshake, countdown, duel, results
18-19. **Win/Lose** - Match outcome
20. **UploadMatches** - Server sync
21-25. **FDN states** - NPC challenges
26. **KonamiPuzzle** - Meta-game

---

### FdnGame

Wrapper state machine for NPC/FDN minigame challenges.

**Location:** `include/game/fdn-game.hpp`

**App ID:** `FDN_GAME_APP_ID = 10`

#### Constructor

```cpp
FdnGame(GameType gameType, KonamiButton reward);
```

**Parameters:**
- `gameType`: Which minigame to run (0-6)
- `reward`: Which Konami button this challenge unlocks

#### Methods

```cpp
GameType getGameType() const;
KonamiButton getReward() const;

bool getLastResult() const;
void setLastResult(bool won);

int getLastScore() const;
void setLastScore(int score);
```

**Purpose:** FdnGame delegates to the actual minigame state machine and tracks results for the parent Quickdraw game.

---

### MiniGame

Abstract base class for all 7 minigames.

**Location:** `include/game/minigame.hpp`

#### Enums

```cpp
enum class MiniGameResult : uint8_t {
    IN_PROGRESS = 0,
    WON = 1,
    LOST = 2,
};

struct MiniGameOutcome {
    MiniGameResult result = MiniGameResult::IN_PROGRESS;
    int score = 0;
    bool hardMode = false;
    bool isComplete() const { return result != MiniGameResult::IN_PROGRESS; }
};
```

#### Constructor

```cpp
MiniGame(int stateId, GameType gameType, const char* displayName);
```

#### Methods

```cpp
GameType getGameType() const;
const char* getDisplayName() const;

const MiniGameOutcome& getOutcome() const;
bool isGameComplete() const;
void setOutcome(const MiniGameOutcome& newOutcome);

virtual void resetGame();
```

**Example:**
```cpp
class MyMiniGame : public MiniGame {
public:
    MyMiniGame() : MiniGame(APP_ID, GameType::MY_GAME, "My Game") {}

    void populateStateMap() override {
        stateMap.push_back(new MyIntroState(this));
        stateMap.push_back(new MyGameplayState(this));
        stateMap.push_back(new MyWinState(this));
        stateMap.push_back(new MyLoseState(this));
    }
};
```

---

### ProgressManager

Manages Konami progression persistence to NVS and server sync.

**Location:** `include/game/progress-manager.hpp`

#### Methods

```cpp
void initialize(Player* player, StorageInterface* storage);
```

---

```cpp
void saveProgress();
void loadProgress();
void clearProgress();
```

**NVS Keys:**
- `"konami"` - uint8_t bitmask of unlocked buttons
- `"konami_boon"` - uint8_t (0/1) puzzle complete flag
- `"color_profile"` - uint8_t equipped profile (gameType + 1, 0 = none)
- `"color_elig"` - uint8_t bitmask of eligible profiles
- `"easy_att_N"` - uint8_t easy mode attempts for game N
- `"hard_att_N"` - uint8_t hard mode attempts for game N
- `"synced"` - uint8_t (0/1) whether uploaded to server

---

```cpp
bool hasUnsyncedProgress() const;
bool isSynced() const;
void markUnsynced();
```

---

```cpp
void syncProgress(HttpClientInterface* httpClient);
```
Uploads progress to server via HTTP PUT `/api/progress`. No-op if already synced or offline.

**JSON Payload:**
```json
{
  "konami": 127,
  "boon": true,
  "profile": 3,
  "colorEligibility": 127,
  "easyAttempts": [1,2,3,4,5,6,7],
  "hardAttempts": [0,1,0,2,0,1,0]
}
```

---

```cpp
void downloadAndMergeProgress(const std::string& jsonResponse);
```
Merges server data with local using conflict resolution:
- **Bitmask fields** (konami, colorEligibility): union merge (bitwise OR)
- **Attempt counts**: max-wins (keep higher value per game/difficulty)

**Example:**
```cpp
ProgressManager* pm = quickdraw->getProgressManager();
pm->saveProgress();
if (httpClient->isConnected()) {
    pm->syncProgress(httpClient);
}
```

---

### MatchManager

Manages duel matches, storage, and result synchronization.

**Location:** `include/game/match-manager.hpp`

#### Methods

```cpp
void initialize(Player* player, StorageInterface* storage,
                PeerCommsInterface* peerComms,
                QuickdrawWirelessManager* quickdrawWirelessManager);
```

---

```cpp
Match* createMatch(const std::string& match_id,
                   const std::string& hunter_id,
                   const std::string& bounty_id);
```
Creates a new active match.

---

```cpp
Match* receiveMatch(const Match& match);
```
Initializes a match received from another player.

---

```cpp
bool setHunterDrawTime(unsigned long hunter_time_ms);
bool setBountyDrawTime(unsigned long bounty_time_ms);
void setDuelLocalStartTime(unsigned long local_start_time_ms);
```

---

```cpp
bool didWin();
bool matchResultsAreIn();
```

---

```cpp
bool finalizeMatch();
```
Saves match to storage and removes from active state.

---

```cpp
Match* getCurrentMatch() const;
void clearCurrentMatch();
```

---

```cpp
std::string toJson();
void clearStorage();
size_t getStoredMatchCount();
```

---

## Minigame Classes

All 7 minigames share a common pattern:

1. **Intro State** (2s) - Title screen, seed RNG
2. **Show State** (1.5s) - Round/wave info
3. **Gameplay State** - Player interaction
4. **Evaluate State** - Check win/loss
5. **Win/Lose States** (3s) - Terminal outcome

### SignalEcho

Memory/pattern replication game (Simon Says).

**Location:** `include/game/signal-echo/`

**App ID:** 2 | **GameType:** `SIGNAL_ECHO`

#### Configuration

```cpp
struct SignalEchoConfig {
    uint32_t sequenceLength = 4;      // Starting sequence length
    uint32_t numSequences = 3;         // Total rounds
    uint32_t displaySpeedMs = 600;     // Signal display duration
    uint32_t timeLimitMs = 0;          // Per-round timeout (0 = none)
    bool cumulative = false;           // Grow sequence each round
    uint32_t allowedMistakes = 2;      // Strike limit
    uint32_t rngSeed = 0;              // For deterministic generation
    bool managedMode = true;           // FDN integration
};
```

#### Difficulty Presets

**EASY:** 4 signals, 3 rounds, 600ms display, 2 mistakes
**HARD:** 5 signals, 5 rounds, 400ms display, 1 mistake

#### Controls

- **PRIMARY** = UP signal
- **SECONDARY** = DOWN signal

#### Win Condition

Complete all rounds without exceeding allowed mistakes.

---

### GhostRunner

Reaction/timing game - catch the scrolling ghost.

**Location:** `include/game/ghost-runner/`

**App ID:** 4 | **GameType:** `GHOST_RUNNER`

#### Configuration

```cpp
struct GhostRunnerConfig {
    uint32_t ghostSpeedMs = 50;        // Scroll speed (lower = faster)
    uint32_t targetZoneStart = 35;     // Left edge of target zone
    uint32_t targetZoneEnd = 65;       // Right edge of target zone
    uint32_t screenWidth = 100;        // Total scroll distance
    uint32_t rounds = 4;               // Number of catches required
    uint32_t missesAllowed = 3;        // Strike limit
    uint32_t rngSeed = 0;
    bool managedMode = true;
};
```

#### Difficulty Presets

**EASY:** speedMs=50, zone 35-65 (30 units wide), 4 rounds, 3 misses
**HARD:** speedMs=30 (faster), zone 42-58 (16 units narrow), 6 rounds, 1 miss

#### Controls

- **PRIMARY** = Attempt catch (hit if in zone, miss if outside)

#### Win Condition

Successfully catch ghost in all rounds without exceeding allowed misses.

---

### SpikeVector

Reaction/dodging game - align cursor with gap in advancing walls.

**Location:** `include/game/spike-vector/`

**App ID:** 5 | **GameType:** `SPIKE_VECTOR`

#### Configuration

```cpp
struct SpikeVectorConfig {
    uint32_t approachSpeedMs = 40;     // Wall advance speed
    uint32_t trackLength = 100;        // Distance wall travels
    uint32_t numPositions = 5;         // Number of lanes (0 to numPositions-1)
    uint32_t waves = 5;                // Total waves to survive
    uint32_t hitsAllowed = 3;          // Strike limit
    uint32_t rngSeed = 0;
    bool managedMode = true;
};
```

#### Difficulty Presets

**EASY:** speedMs=40, 5 lanes, 5 waves, 3 hits allowed
**HARD:** speedMs=20 (faster), 7 lanes, 8 waves, 1 hit allowed

#### Controls

- **PRIMARY** = Move cursor UP (decrease position)
- **SECONDARY** = Move cursor DOWN (increase position)

#### Win Condition

Survive all waves without exceeding allowed hits.

---

### FirewallDecrypt

Pattern recognition/puzzle - identify target MAC address among decoys.

**Location:** `include/game/firewall-decrypt/`

**App ID:** 3 | **GameType:** `FIREWALL_DECRYPT`

#### Configuration

```cpp
struct FirewallDecryptConfig {
    uint32_t numCandidates = 5;        // Number of options
    uint32_t numRounds = 3;            // Total rounds
    float similarity = 0.2f;           // Decoy similarity (0.0-1.0)
    uint32_t timeLimitMs = 0;          // Per-round timeout (0 = none)
    uint32_t rngSeed = 0;
    bool managedMode = true;
};
```

#### Difficulty Presets

**EASY:** 5 candidates, 3 rounds, similarity=0.2 (obvious), no time limit
**HARD:** 10 candidates, 4 rounds, similarity=0.5+ (subtle), 15s timeout

#### Controls

- **PRIMARY** = Scroll down through candidates
- **SECONDARY** = Confirm selection

#### Win Condition

Correctly identify target in all rounds (one wrong selection = instant loss).

---

### CipherPath

Puzzle/pathfinding - solve cipher sequence to reach exit.

**Location:** `include/game/cipher-path/`

**App ID:** 6 | **GameType:** `CIPHER_PATH`

#### Configuration

```cpp
struct CipherPathConfig {
    uint32_t gridSize = 6;             // Path length (0 to gridSize-1)
    uint32_t moveBudget = 12;          // Total moves allowed
    uint32_t rounds = 2;               // Number of paths to solve
    uint32_t rngSeed = 0;
    bool managedMode = true;
};
```

#### Difficulty Presets

**EASY:** gridSize=6, moveBudget=12 (2x path length), 2 rounds
**HARD:** gridSize=10, moveBudget=14 (1.4x path length), 4 rounds

#### Controls

- **PRIMARY** = Guess UP at current position
- **SECONDARY** = Guess DOWN at current position

#### Win Condition

Reach exit (position == gridSize-1) within move budget for all rounds.

---

### ExploitSequencer

QTE/timing game - press at the right moment as symbol scrolls.

**Location:** `include/game/exploit-sequencer/`

**App ID:** 7 | **GameType:** `EXPLOIT_SEQUENCER`

#### Configuration

```cpp
struct ExploitSequencerConfig {
    uint32_t scrollSpeedMs = 40;       // Symbol scroll speed
    uint32_t timingWindow = 15;        // Hit detection tolerance
    uint32_t markerPosition = 50;      // Fixed timing marker (center)
    uint32_t scrollLength = 100;       // Total scroll distance
    uint32_t exploitsPerSeq = 2;       // Exploits per sequence
    uint32_t sequences = 2;            // Total sequences
    uint32_t failsAllowed = 3;         // Strike limit
    uint32_t rngSeed = 0;
    bool managedMode = true;
};
```

#### Difficulty Presets

**EASY:** speedMs=40, window=15 (wide), 2 exploits/seq, 2 sequences, 3 fails
**HARD:** speedMs=25 (faster), window=6 (narrow), 4 exploits/seq, 4 sequences, 1 fail

#### Controls

- **PRIMARY** = Attempt exploit (must be within timing window of marker)

#### Win Condition

Successfully execute all exploits across all sequences without exceeding allowed fails.

---

### BreachDefense

Reaction/defense game - position shield to block threats in lanes.

**Location:** `include/game/breach-defense/`

**App ID:** 8 | **GameType:** `BREACH_DEFENSE`

#### Configuration

```cpp
struct BreachDefenseConfig {
    uint32_t numLanes = 3;             // Number of threat lanes
    uint32_t threatSpeedMs = 40;       // Threat approach speed
    uint32_t threatDistance = 100;     // Total approach distance
    uint32_t totalThreats = 6;         // Number of threats to survive
    uint32_t missesAllowed = 3;        // Strike limit
    uint32_t rngSeed = 0;
    bool managedMode = true;
};
```

#### Difficulty Presets

**EASY:** 3 lanes, speedMs=40, 6 threats, 3 misses allowed
**HARD:** 5 lanes, speedMs=20 (faster), 12 threats, 1 miss allowed

#### Controls

- **PRIMARY** = Move shield UP (decrease lane)
- **SECONDARY** = Move shield DOWN (increase lane)

#### Win Condition

Block all threats without exceeding allowed misses.

---

## Utility Classes

### LightManager

Manages LED animations and color palettes.

**Location:** `include/device/light-manager.hpp`

#### Methods

```cpp
LightManager(LightStrip& pdnLights);
void loop();
```

---

```cpp
void startAnimation(AnimationConfig config);
void stopAnimation();
void pauseAnimation();
void resumeAnimation();
```

---

```cpp
void setGlobalBrightness(uint8_t brightness);  // 0-255
void clear();
```

---

```cpp
bool isAnimating() const;
bool isPaused() const;
bool isAnimationComplete() const;
AnimationType getCurrentAnimation() const;
```

**Example:**
```cpp
AnimationConfig config;
config.type = AnimationType::PULSE;
config.palette = ColorPalette::FIRE;
config.durationMs = 2000;
config.brightness = 128;

lightManager->startAnimation(config);
```

---

## CLI Commands

The PDN CLI simulator provides commands for device control and inspection.

### Navigation

- `list`, `ls` - List all devices and connections
- `select <id>`, `sel <id>` - Select device by ID or index

### Button Simulation

- `b [dev]`, `button [dev]`, `click [dev]` - PRIMARY button click
- `l [dev]`, `long [dev]`, `longpress [dev]` - PRIMARY button long press
- `b2 [dev]`, `button2 [dev]`, `click2 [dev]` - SECONDARY button click
- `l2 [dev]`, `long2 [dev]`, `longpress2 [dev]` - SECONDARY button long press

### Device Management

- `add [hunter|bounty|npc <game>|challenge <game>]` - Add device
- `reboot [dev]`, `restart [dev]` - Restart device to FetchUserData
- `role [dev|all]` - Show device role (Hunter/Bounty)
- `state [dev]`, `st [dev]` - Show current state

### Connectivity

- `cable <devA> <devB>`, `connect <devA> <devB>`, `c <devA> <devB>` - Connect serial cable
- `cable -d <devA> <devB>`, `cable disconnect <devA> <devB>` - Disconnect cable
- `cable -l`, `cable list` - List all cable connections
- `peer <src> <dst|broadcast> <type> [hexdata]` - Send ESP-NOW packet
- `inject <dst> <type> [hexdata]` - Inject packet from external source

### Display

- `mirror [on|off]`, `m [on|off]` - Toggle display mirror (braille rendering)
- `captions [on|off]`, `cap [on|off]` - Toggle state captions
- `display [on|off]`, `d [on|off]` - Toggle both mirror and captions

### Progression

- `games` - List all available minigames
- `stats [dev]`, `info [dev]` - Show player statistics
- `progress [dev]`, `prog [dev]` - Show Konami progress grid
- `colors [dev]`, `profiles [dev]` - Show color profile eligibility
- `konami [dev] [value]` - Show/set Konami progress bitmask

### System

- `help`, `h`, `?` - Show quick help
- `help2` - Show extended command list
- `quit`, `q`, `exit` - Exit simulator

### Startup Flags

```bash
# Launch options
./program [N]                    # Create N devices
./program --script <file>        # Run demo script
./program --npc <game>           # Spawn NPC device
./program --game <game>          # Launch minigame directly
./program --auto-cable           # Auto-cable first player to first NPC

# Examples
./program 3                      # 3 devices (hunter, bounty, hunter)
./program --npc ghost-runner --auto-cable
./program --game signal-echo     # Standalone Signal Echo
```

### Valid Game Names

For `--npc`, `--game`, `add npc`, and `add challenge`:
- `ghost-runner`
- `spike-vector`
- `firewall-decrypt`
- `cipher-path`
- `exploit-sequencer`
- `breach-defense`
- `signal-echo`
- `quickdraw` (for `--game` only)

---

## Build Environments

**5 PlatformIO Environments:**

| Environment | Purpose | Command |
|---|---|---|
| `native` | Core unit tests | `pio test -e native` |
| `native_cli_test` | CLI integration tests | `pio test -e native_cli_test` |
| `native_cli` | Simulator build | `pio run -e native_cli` |
| `esp32-s3_debug` | Hardware debug build | `pio run -e esp32-s3_debug` |
| `esp32-s3_release` | Hardware production build | `pio run -e esp32-s3_release` |

**Important:** `native_cli` is build-only, not a test environment. Always use `native_cli_test` for running CLI tests.

---

## Further Reading

- [ARCHITECTURE.md](ARCHITECTURE.md) - Component diagrams and build system
- [GAME-MECHANICS.md](GAME-MECHANICS.md) - Detailed gameplay rules
- [STATE-MACHINES.md](STATE-MACHINES.md) - State flow diagrams
- [HACKATHON-REPORT.md](HACKATHON-REPORT.md) - Development timeline and stats
- [CLAUDE.md](../CLAUDE.md) - Development guide for AI agents

---

*Last Updated: 2026-02-14*
