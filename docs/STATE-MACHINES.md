# PDN State Machines

This document provides comprehensive state machine diagrams for all gameplay systems in the PDN (Portable Data Node). Each state machine uses the standard State Pattern with lifecycle methods (`onStateMounted`, `onStateLoop`, `onStateDismounted`).

## Table of Contents

1. [Quickdraw Main State Machine](#quickdraw-main-state-machine)
2. [FDN Game Flow](#fdn-game-flow)
3. [Minigames](#minigames)
   - [Signal Echo](#signal-echo)
   - [Firewall Decrypt](#firewall-decrypt)
   - [Ghost Runner](#ghost-runner)
   - [Spike Vector](#spike-vector)
   - [Cipher Path](#cipher-path)
   - [Exploit Sequencer](#exploit-sequencer)
   - [Breach Defense](#breach-defense)

---

## Quickdraw Main State Machine

The Quickdraw game is the primary game mode featuring 28 states covering registration, role selection, idle state, dueling, and FDN encounters.

```mermaid
stateDiagram-v2
    [*] --> PlayerRegistration

    PlayerRegistration: PlayerRegistration (0)
    PlayerRegistration: Enter 4-digit ID
    PlayerRegistration --> FetchUserData: ID entered

    FetchUserData: FetchUserData (1)
    FetchUserData: Fetch player data from server
    FetchUserData --> ConfirmOffline: No network/not found
    FetchUserData --> WelcomeMessage: Player found
    FetchUserData --> UploadMatches: Player found + unsynced data
    FetchUserData --> PlayerRegistration: Retry

    ConfirmOffline: ConfirmOffline (2)
    ConfirmOffline: Confirm offline play
    ConfirmOffline --> ChooseRole: Confirm
    ConfirmOffline --> PlayerRegistration: Cancel

    ChooseRole: ChooseRole (3)
    ChooseRole: Select Hunter or Bounty
    ChooseRole --> AllegiancePicker: Role selected

    AllegiancePicker: AllegiancePicker (4)
    AllegiancePicker: Choose allegiance
    AllegiancePicker --> WelcomeMessage: Allegiance selected

    WelcomeMessage: WelcomeMessage (5)
    WelcomeMessage: Display welcome screen
    WelcomeMessage --> AwakenSequence: Timer expires (5s)

    AwakenSequence: AwakenSequence (7)
    AwakenSequence: Haptic activation sequence
    AwakenSequence --> Idle: Sequence complete

    Idle: Idle (8)
    Idle: Ready for duel or FDN
    Idle --> HandshakeInitiate: Cable connected
    Idle --> FdnDetected: FDN detected on serial
    Idle --> ColorProfilePicker: Long press secondary button

    HandshakeInitiate: HandshakeInitiate (9)
    HandshakeInitiate: Detect role via cable
    HandshakeInitiate --> BountySendCC: Bounty role
    HandshakeInitiate --> HunterSendId: Hunter role
    HandshakeInitiate --> Idle: Timeout (20s)

    BountySendCC: BountySendCC (10)
    BountySendCC: Bounty sends connection confirmed
    BountySendCC --> ConnectionSuccessful: Hunter ID received
    BountySendCC --> Idle: Timeout (20s)

    HunterSendId: HunterSendId (11)
    HunterSendId: Hunter sends ID
    HunterSendId --> ConnectionSuccessful: CC received
    HunterSendId --> Idle: Timeout (20s)

    ConnectionSuccessful: ConnectionSuccessful (12)
    ConnectionSuccessful: Flash lights celebration
    ConnectionSuccessful --> DuelCountdown: Alert count threshold

    DuelCountdown: DuelCountdown (13)
    DuelCountdown: 3-2-1 countdown with haptics
    DuelCountdown --> Duel: Countdown complete

    Duel: Duel (14)
    Duel: Wait for button press
    Duel --> DuelPushed: Local button pressed
    Duel --> DuelReceivedResult: Opponent result received
    Duel --> Idle: Timeout (4s)

    DuelPushed: DuelPushed (15)
    DuelPushed: Sent result, wait for opponent
    DuelPushed --> DuelResult: Grace period expires (900ms)

    DuelReceivedResult: DuelReceivedResult (16)
    DuelReceivedResult: Got opponent result first
    DuelReceivedResult --> DuelResult: Grace period expires (750ms)

    DuelResult: DuelResult (17)
    DuelResult: Calculate winner
    DuelResult --> Win: Player won
    DuelResult --> Lose: Player lost

    Win: Win (18)
    Win: Victory celebration
    Win --> UploadMatches: Timer expires

    Lose: Lose (19)
    Lose: Defeat screen
    Lose --> UploadMatches: Timer expires

    UploadMatches: UploadMatches (20)
    UploadMatches: Sync matches + FDN results
    UploadMatches --> Sleep: Upload complete
    UploadMatches --> PlayerRegistration: Upload failed (invalid ID)

    Sleep: Sleep (6)
    Sleep: Breathing LED animation
    Sleep --> AwakenSequence: Button press or timer (60s)

    FdnDetected: FdnDetected (21)
    FdnDetected: Handshake with FDN, launch minigame
    FdnDetected --> KonamiPuzzle: All 7 rewards unlocked
    FdnDetected --> FdnReencounter: Already completed
    FdnDetected --> FdnComplete: Game paused/resumed
    FdnDetected --> ConnectionLost: Serial disconnect
    FdnDetected --> Idle: Timeout (10s)

    FdnReencounter: FdnReencounter (25)
    FdnReencounter: Re-encounter prompt (HARD/EASY/SKIP)
    FdnReencounter --> FdnComplete: Game paused/resumed
    FdnReencounter --> Idle: Skip or timeout (15s)

    FdnComplete: FdnComplete (22)
    FdnComplete: Display minigame outcome
    FdnComplete --> ColorProfilePrompt: Won hard mode (new unlock)
    FdnComplete --> Idle: Timer expires (3s)

    ColorProfilePrompt: ColorProfilePrompt (23)
    ColorProfilePrompt: Equip new color palette?
    ColorProfilePrompt --> Idle: Choice made or timeout (10s)

    ColorProfilePicker: ColorProfilePicker (24)
    ColorProfilePicker: Browse unlocked palettes
    ColorProfilePicker --> Idle: Selection confirmed

    KonamiPuzzle: KonamiPuzzle (26)
    KonamiPuzzle: Enter Konami Code
    KonamiPuzzle --> Idle: Complete or exit

    ConnectionLost: ConnectionLost (27)
    ConnectionLost: Serial cable disconnected
    ConnectionLost --> Idle: Timer expires (3s)
```

### State Descriptions

- **PlayerRegistration (0)**: Player enters 4-digit ID using buttons
- **FetchUserData (1)**: Attempts to fetch player data from server via WiFi
- **ConfirmOffline (2)**: Prompts for confirmation to play offline when network unavailable
- **ChooseRole (3)**: Select Hunter or Bounty role (offline only)
- **AllegiancePicker (4)**: Choose faction allegiance (offline only)
- **WelcomeMessage (5)**: Brief welcome screen with player info
- **Sleep (6)**: Low-power idle state with breathing LED
- **AwakenSequence (7)**: Haptic motor activation sequence
- **Idle (8)**: Main waiting state, displays stats and waits for connection
- **HandshakeInitiate (9)**: Cable connection detected, determine roles
- **BountySendCC (10)**: Bounty sends connection confirmation
- **HunterSendId (11)**: Hunter sends player ID
- **ConnectionSuccessful (12)**: Handshake complete, flash celebration
- **DuelCountdown (13)**: 3-2-1-BATTLE countdown with haptics
- **Duel (14)**: Active duel state waiting for button press
- **DuelPushed (15)**: Player pressed button, waiting for opponent
- **DuelReceivedResult (16)**: Received opponent's result first
- **DuelResult (17)**: Calculate and display outcome
- **Win (18)**: Victory celebration with LED/haptic effects
- **Lose (19)**: Defeat screen
- **UploadMatches (20)**: Sync match data and FDN results to server
- **FdnDetected (21)**: FDN encounter detected, perform handshake and launch minigame
- **FdnComplete (22)**: Display minigame outcome and update progress
- **ColorProfilePrompt (23)**: Prompt to equip newly unlocked color palette
- **ColorProfilePicker (24)**: Browse and select color palettes (manual entry)
- **FdnReencounter (25)**: Re-encounter prompt for already-beaten FDN
- **KonamiPuzzle (26)**: Meta-game puzzle unlocked when all 7 rewards collected
- **ConnectionLost (27)**: Error state when serial connection drops during FDN encounter

---

## FDN Game Flow

The FDN (Fixed Data Node) is an NPC device that hosts minigames. It has 4 states managing the interaction lifecycle.

```mermaid
stateDiagram-v2
    [*] --> NpcIdle

    NpcIdle: NpcIdle (0)
    NpcIdle: Broadcast FDN message on serial
    NpcIdle --> NpcHandshake: Received player MAC (smac)

    NpcHandshake: NpcHandshake (1)
    NpcHandshake: Send acknowledgment (fack)
    NpcHandshake --> NpcGameActive: Player ready signal received
    NpcHandshake --> NpcIdle: Timeout (10s)

    NpcGameActive: NpcGameActive (2)
    NpcGameActive: Minigame is active (managed by Device)
    NpcGameActive --> NpcReceiveResult: Player sends result
    NpcGameActive --> NpcIdle: Inactivity timeout (30s)

    NpcReceiveResult: NpcReceiveResult (3)
    NpcReceiveResult: Display player result, log to storage
    NpcReceiveResult --> NpcIdle: Timer expires (3s)
```

### State Descriptions

- **NpcIdle (0)**: Broadcasts FDN announcement (game type + reward) on serial every 500ms
- **NpcHandshake (1)**: Receives player MAC address, sends acknowledgment (fack), waits for ready signal
- **NpcGameActive (2)**: Minigame is active on player device (NPC mostly idle, monitors for disconnection)
- **NpcReceiveResult (3)**: Receives and displays player's game result, logs to FdnResultManager

---

## Minigames

All minigames follow a common 6-state pattern: **Intro → Show → Gameplay → Evaluate → Win/Lose**. The Evaluate state acts as a router to determine next steps based on round outcomes.

### Signal Echo

Memory game where player watches and repeats a sequence of UP/DOWN signals.

```mermaid
stateDiagram-v2
    [*] --> EchoIntro

    EchoIntro: EchoIntro (100)
    EchoIntro: Title screen "SIGNAL ECHO"
    EchoIntro --> EchoShowSequence: Timer expires (2s)

    EchoShowSequence: EchoShowSequence (101)
    EchoShowSequence: Display sequence one signal at a time
    EchoShowSequence --> EchoPlayerInput: Sequence complete

    EchoPlayerInput: EchoPlayerInput (102)
    EchoPlayerInput: Player reproduces sequence
    EchoPlayerInput --> EchoEvaluate: Input complete or too many mistakes

    EchoEvaluate: EchoEvaluate (103)
    EchoEvaluate: Check round outcome
    EchoEvaluate --> EchoShowSequence: Next round
    EchoEvaluate --> EchoWin: All rounds complete
    EchoEvaluate --> EchoLose: Too many mistakes

    EchoWin: EchoWin (104)
    EchoWin: Victory screen "ACCESS GRANTED"
    EchoWin --> EchoIntro: Standalone mode restart
    EchoWin --> [*]: Managed mode (return to Quickdraw)

    EchoLose: EchoLose (105)
    EchoLose: Defeat screen "SIGNAL LOST"
    EchoLose --> EchoIntro: Standalone mode restart
    EchoLose --> [*]: Managed mode (return to Quickdraw)
```

**Gameplay**: Player watches a sequence of UP/DOWN signals, then reproduces them using primary (UP) and secondary (DOWN) buttons. Each round adds complexity. Mistakes are tracked; exceeding the limit results in failure.

---

### Firewall Decrypt

Address-matching puzzle game with time pressure.

```mermaid
stateDiagram-v2
    [*] --> DecryptIntro

    DecryptIntro: DecryptIntro (200)
    DecryptIntro: Title screen "FIREWALL DECRYPT"
    DecryptIntro --> DecryptScan: Timer expires (2s)

    DecryptScan: DecryptScan (201)
    DecryptScan: Display target + candidate list
    DecryptScan --> DecryptEvaluate: Selection confirmed or timeout

    DecryptEvaluate: DecryptEvaluate (202)
    DecryptEvaluate: Check selection
    DecryptEvaluate --> DecryptScan: Next round
    DecryptEvaluate --> DecryptWin: All rounds complete
    DecryptEvaluate --> DecryptLose: Wrong selection or timeout

    DecryptWin: DecryptWin (203)
    DecryptWin: Victory screen "DECRYPTED!"
    DecryptWin --> DecryptIntro: Standalone mode restart
    DecryptWin --> [*]: Managed mode (return to Quickdraw)

    DecryptLose: DecryptLose (204)
    DecryptLose: Defeat screen "FIREWALL INTACT"
    DecryptLose --> DecryptIntro: Standalone mode restart
    DecryptLose --> [*]: Managed mode (return to Quickdraw)
```

**Gameplay**: Player is shown a target address and must select the matching address from a scrollable list of candidates. Primary button scrolls, secondary button confirms. Wrong selection or timeout results in failure.

---

### Ghost Runner

Timing-based reflex game where player catches a moving ghost.

```mermaid
stateDiagram-v2
    [*] --> GhostRunnerIntro

    GhostRunnerIntro: GhostRunnerIntro (300)
    GhostRunnerIntro: Title screen "GHOST RUNNER"
    GhostRunnerIntro --> GhostRunnerShow: Timer expires (2s)

    GhostRunnerShow: GhostRunnerShow (303)
    GhostRunnerShow: Display round info
    GhostRunnerShow --> GhostRunnerGameplay: Timer expires (1.5s)

    GhostRunnerGameplay: GhostRunnerGameplay (304)
    GhostRunnerGameplay: Ghost moves across screen
    GhostRunnerGameplay --> GhostRunnerEvaluate: Button press or timeout

    GhostRunnerEvaluate: GhostRunnerEvaluate (305)
    GhostRunnerEvaluate: Check timing accuracy
    GhostRunnerEvaluate --> GhostRunnerShow: Next round
    GhostRunnerEvaluate --> GhostRunnerWin: All rounds complete
    GhostRunnerEvaluate --> GhostRunnerLose: Too many misses

    GhostRunnerWin: GhostRunnerWin (301)
    GhostRunnerWin: Victory screen "RUN COMPLETE"
    GhostRunnerWin --> GhostRunnerIntro: Standalone mode restart
    GhostRunnerWin --> [*]: Managed mode (return to Quickdraw)

    GhostRunnerLose: GhostRunnerLose (302)
    GhostRunnerLose: Defeat screen "GHOST CAUGHT"
    GhostRunnerLose --> GhostRunnerIntro: Standalone mode restart
    GhostRunnerLose --> [*]: Managed mode (return to Quickdraw)
```

**Gameplay**: A ghost moves across the screen at a steady pace. Player must press the primary button when the ghost is in the target zone. Presses outside the zone count as strikes. Too many strikes result in failure.

---

### Spike Vector

Dodge-based reaction game with advancing spike walls.

```mermaid
stateDiagram-v2
    [*] --> SpikeVectorIntro

    SpikeVectorIntro: SpikeVectorIntro (400)
    SpikeVectorIntro: Title screen "SPIKE VECTOR"
    SpikeVectorIntro --> SpikeVectorShow: Timer expires (2s)

    SpikeVectorShow: SpikeVectorShow (403)
    SpikeVectorShow: Display wave info, generate gap
    SpikeVectorShow --> SpikeVectorGameplay: Timer expires (1.5s)

    SpikeVectorGameplay: SpikeVectorGameplay (404)
    SpikeVectorGameplay: Wall advances, player positions cursor
    SpikeVectorGameplay --> SpikeVectorEvaluate: Wall reaches end

    SpikeVectorEvaluate: SpikeVectorEvaluate (405)
    SpikeVectorEvaluate: Check if cursor matches gap
    SpikeVectorEvaluate --> SpikeVectorShow: Next wave
    SpikeVectorEvaluate --> SpikeVectorWin: All waves cleared
    SpikeVectorEvaluate --> SpikeVectorLose: Too many hits

    SpikeVectorWin: SpikeVectorWin (401)
    SpikeVectorWin: Victory screen "VECTOR CLEAR"
    SpikeVectorWin --> SpikeVectorIntro: Standalone mode restart
    SpikeVectorWin --> [*]: Managed mode (return to Quickdraw)

    SpikeVectorLose: SpikeVectorLose (402)
    SpikeVectorLose: Defeat screen "SPIKE IMPACT"
    SpikeVectorLose --> SpikeVectorIntro: Standalone mode restart
    SpikeVectorLose --> [*]: Managed mode (return to Quickdraw)
```

**Gameplay**: A spike wall with a random gap advances toward the player. Player uses primary (UP) and secondary (DOWN) buttons to position their cursor. If the cursor matches the gap position when the wall arrives, the player dodges successfully. Hits are counted; too many result in failure.

---

### Cipher Path

Navigation puzzle where player decodes directional ciphers.

```mermaid
stateDiagram-v2
    [*] --> CipherPathIntro

    CipherPathIntro: CipherPathIntro (500)
    CipherPathIntro: Title screen "CIPHER PATH"
    CipherPathIntro --> CipherPathShow: Timer expires (2s)

    CipherPathShow: CipherPathShow (503)
    CipherPathShow: Display round info, generate cipher
    CipherPathShow --> CipherPathGameplay: Timer expires (1.5s)

    CipherPathGameplay: CipherPathGameplay (504)
    CipherPathGameplay: Player decodes and navigates
    CipherPathGameplay --> CipherPathEvaluate: Exit reached or moves exhausted

    CipherPathEvaluate: CipherPathEvaluate (505)
    CipherPathEvaluate: Check if exit reached
    CipherPathEvaluate --> CipherPathShow: Next round
    CipherPathEvaluate --> CipherPathWin: All rounds complete
    CipherPathEvaluate --> CipherPathLose: Budget exhausted

    CipherPathWin: CipherPathWin (501)
    CipherPathWin: Victory screen "PATH DECODED"
    CipherPathWin --> CipherPathIntro: Standalone mode restart
    CipherPathWin --> [*]: Managed mode (return to Quickdraw)

    CipherPathLose: CipherPathLose (502)
    CipherPathLose: Defeat screen "PATH LOST"
    CipherPathLose --> CipherPathIntro: Standalone mode restart
    CipherPathLose --> [*]: Managed mode (return to Quickdraw)
```

**Gameplay**: Player is given a cipher that encodes a sequence of UP/DOWN movements. They must decode it and navigate from start to exit within a move budget. Correct moves advance position, wrong moves waste budget. Failing to reach the exit results in failure.

---

### Exploit Sequencer

Rhythm-based QTE (Quick Time Event) game with scrolling symbols.

```mermaid
stateDiagram-v2
    [*] --> ExploitSeqIntro

    ExploitSeqIntro: ExploitSeqIntro (600)
    ExploitSeqIntro: Title screen "EXPLOIT SEQUENCER"
    ExploitSeqIntro --> ExploitSeqShow: Timer expires (2s)

    ExploitSeqShow: ExploitSeqShow (603)
    ExploitSeqShow: Display exploit info
    ExploitSeqShow --> ExploitSeqGameplay: Timer expires (1.5s)

    ExploitSeqGameplay: ExploitSeqGameplay (604)
    ExploitSeqGameplay: Symbol scrolls across marker
    ExploitSeqGameplay --> ExploitSeqEvaluate: Button press or timeout

    ExploitSeqEvaluate: ExploitSeqEvaluate (605)
    ExploitSeqEvaluate: Check timing accuracy
    ExploitSeqEvaluate --> ExploitSeqShow: Next exploit
    ExploitSeqEvaluate --> ExploitSeqWin: All exploits complete
    ExploitSeqEvaluate --> ExploitSeqLose: Too many fails

    ExploitSeqWin: ExploitSeqWin (601)
    ExploitSeqWin: Victory screen "EXPLOIT DONE"
    ExploitSeqWin --> ExploitSeqIntro: Standalone mode restart
    ExploitSeqWin --> [*]: Managed mode (return to Quickdraw)

    ExploitSeqLose: ExploitSeqLose (602)
    ExploitSeqLose: Defeat screen "EXPLOIT FAILED"
    ExploitSeqLose --> ExploitSeqIntro: Standalone mode restart
    ExploitSeqLose --> [*]: Managed mode (return to Quickdraw)
```

**Gameplay**: A symbol scrolls from left to right across a marker. Player must press primary button when the symbol aligns with the marker (within a timing window). Successful hits score points, misses or timeouts increment fails. Too many fails result in failure.

---

### Breach Defense

Lane-defense game where player blocks incoming threats.

```mermaid
stateDiagram-v2
    [*] --> BreachDefenseIntro

    BreachDefenseIntro: BreachDefenseIntro (700)
    BreachDefenseIntro: Title screen "BREACH DEFENSE"
    BreachDefenseIntro --> BreachDefenseShow: Timer expires (2s)

    BreachDefenseShow: BreachDefenseShow (703)
    BreachDefenseShow: Display threat info, generate lane
    BreachDefenseShow --> BreachDefenseGameplay: Timer expires (1.5s)

    BreachDefenseGameplay: BreachDefenseGameplay (704)
    BreachDefenseGameplay: Threat advances, player moves shield
    BreachDefenseGameplay --> BreachDefenseEvaluate: Threat reaches end

    BreachDefenseEvaluate: BreachDefenseEvaluate (705)
    BreachDefenseEvaluate: Check if shield blocked threat
    BreachDefenseEvaluate --> BreachDefenseShow: Next threat
    BreachDefenseEvaluate --> BreachDefenseWin: All threats blocked
    BreachDefenseEvaluate --> BreachDefenseLose: Too many breaches

    BreachDefenseWin: BreachDefenseWin (701)
    BreachDefenseWin: Victory screen "BREACH BLOCKED"
    BreachDefenseWin --> BreachDefenseIntro: Standalone mode restart
    BreachDefenseWin --> [*]: Managed mode (return to Quickdraw)

    BreachDefenseLose: BreachDefenseLose (702)
    BreachDefenseLose: Defeat screen "BREACH OPEN"
    BreachDefenseLose --> BreachDefenseIntro: Standalone mode restart
    BreachDefenseLose --> [*]: Managed mode (return to Quickdraw)
```

**Gameplay**: A threat advances through one of multiple lanes toward the player. Player uses primary (UP) and secondary (DOWN) buttons to move their shield between lanes. If the shield is in the correct lane when the threat arrives, it's blocked. Unblocked threats count as breaches; too many result in failure.

---

## State Lifecycle Pattern

All states follow the same lifecycle pattern defined in the State base class:

1. **onStateMounted(Device* PDN)** - Called once when entering the state
   - Initialize timers, variables, callbacks
   - Register button handlers
   - Display initial UI
   - Play sounds/haptics

2. **onStateLoop(Device* PDN)** - Called repeatedly while state is active
   - Update timers
   - Check transition conditions
   - Update animations/LEDs
   - Handle input

3. **onStateDismounted(Device* PDN)** - Called once when leaving the state
   - Clean up resources
   - Unregister callbacks
   - Clear display
   - Stop animations

Additionally, some states implement:

- **onStatePaused(Device* PDN)** - Called when switching to another app (e.g., launching minigame)
- **onStateResumed(Device* PDN, Snapshot* snapshot)** - Called when returning from another app
- **isTerminalState()** - Returns true for Win/Lose states to trigger app switching

---

## State Transition Conditions

Transitions are evaluated in `onStateLoop()` and registered as `StateTransition` objects binding condition functions to target states. The StateMachine evaluates registered transitions in order and executes the first one that returns true.

Common transition patterns:

- **Timer-based**: `SimpleTimer::expired()` after a fixed duration
- **Input-based**: Button press detected via callback
- **Condition-based**: Game state check (e.g., all rounds complete)
- **Timeout-based**: Fallback when expected input not received
- **Message-based**: Serial or ESP-NOW message received

Example from DuelCountdown:
```cpp
// Transition registered in Quickdraw::populateStateMap()
duelCountdown->addTransition(
    new StateTransition(
        std::bind(&DuelCountdown::shallWeBattle, duelCountdown),
        duel));

// Condition checked in DuelCountdown::onStateLoop()
bool DuelCountdown::shallWeBattle() {
    return doBattle;  // Set to true when countdown completes
}
```

---

## State ID Ranges

To avoid collisions, different games use different ID ranges:

| Game | ID Range | Enum Prefix |
|------|----------|-------------|
| Quickdraw | 0-27 | `QUICKDRAW_` or none |
| Signal Echo | 100-105 | `ECHO_` |
| Firewall Decrypt | 200-204 | `DECRYPT_` |
| Ghost Runner | 300-305 | `GHOST_` |
| Spike Vector | 400-405 | `SPIKE_` |
| Cipher Path | 500-505 | `CIPHER_` |
| Exploit Sequencer | 600-605 | `EXPLOIT_` |
| Breach Defense | 700-705 | `BREACH_` |
| FDN Game | 0-3 | `NPC_` |

---

## Testing State Machines

State machines are tested in the CLI simulator using the native test suite. Key testing patterns:

1. **State transitions**: Verify correct state reached after condition
2. **Lifecycle calls**: Ensure mounted/loop/dismounted called correctly
3. **Timer behavior**: Fast-forward time to test timeouts
4. **Input handling**: Simulate button presses
5. **Edge cases**: Timeouts, disconnections, invalid inputs

Example test structure:
```cpp
TEST_F(MyGameTestSuite, transitionsFromIntroToShow) {
    // Arrange: Device starts in Intro state
    EXPECT_EQ(device->game->getCurrentStateId(), GAME_INTRO);

    // Act: Advance time past intro duration
    device->clockDriver->advanceTime(2000);
    device->game->update(device->pdn);

    // Assert: Transitioned to Show state
    EXPECT_EQ(device->game->getCurrentStateId(), GAME_SHOW);
}
```

---

*Last Updated: 2026-02-14*
